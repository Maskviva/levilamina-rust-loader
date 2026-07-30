/**
 * bridge/PacketHooks.cpp — raw wire-format packet interception.
 *
 * # What this hooks, and why these two functions
 *
 * A Bedrock packet crosses several layers on its way out:
 *
 *     Packet object
 *       -> NetworkSystem::send            serialises header + body
 *       -> NetworkSystem::_sendInternal   (id, packet, std::string data)
 *       -> BatchedNetworkPeer::sendPacket appends [uvarint len][data]
 *       -> CompressedNetworkPeer          compresses the batch
 *       -> EncryptedNetworkPeer           encrypts
 *       -> RakNet
 *
 * and the mirror image on the way in, ending at
 * `NetworkConnection::receivePacket`, which pulls ONE already-decrypted,
 * already-decompressed, already-de-batched packet out of the peer chain per
 * call (that de-batching is `BatchedNetworkPeer::_receivePacket` walking
 * `mIncomingData`).
 *
 * `_sendInternal` and `receivePacket` are therefore the narrowest points where
 * a packet exists as plain bytes and as exactly one packet. Hooking higher
 * (Packet::write) would mean re-serialising; hooking lower (the peers) would
 * mean unpacking batches and fighting compression. Neither buys anything.
 *
 * # The header
 *
 * Both directions carry the same leading unsigned varint:
 *
 *     bits 0..9   packet id  (MinecraftPacketIds)
 *     bits 10..11 sender sub client id
 *     bits 12..13 target sub client id
 *
 * The bridge decodes it, hands subscribers the BODY, and re-encodes from
 * `LeviRsPacketEdit` on the way back out. Callers never touch varint framing,
 * and remapping a packet id is an assignment instead of byte surgery.
 *
 * On the outbound side the decoded id is cross-checked against
 * `packet.getId()`. They can only disagree if this layout assumption stops
 * holding on some future BDS — in which case we log once and pass everything
 * through untouched rather than corrupt the stream.
 *
 * # Locking
 *
 * The rest of this bridge asserts "everything runs on the server thread, so
 * the registry needs no locking". That is not safe to assume here:
 * `enableAsyncFlush` exists and the send path is reachable from more than one
 * place. So the registry sits behind a shared_mutex, and dispatch takes a
 * SNAPSHOT of shared_ptrs and then releases it before calling anything. That
 * buys two things at once — a callback may (un)register itself mid-dispatch,
 * and an entry freed by another thread can't be pulled out from under us.
 *
 * # Lifecycle
 *
 * Detours install lazily on the first subscriber and are never unpatched: an
 * unsubscribe can arrive from inside the hooked function, where unpatching is
 * unsafe. Idle hooks fast-path to origin behind one atomic load.
 */
#include "bridge/Api.h"
#include "bridge/Common.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ll/api/io/Logger.h"
#include "ll/api/memory/Hook.h"

#include "mc/network/NetworkConnection.h"
#include "mc/network/NetworkIdentifier.h"
#include "mc/network/NetworkPeer.h"
#include "mc/network/NetworkSystem.h"
#include "mc/network/Packet.h"

#include "RustMod.h"

namespace levi_rs::bridge
{
    namespace
    {
        /* ───────────────────────── registry ───────────────────────── */

        struct PacketSub
        {
            RustMod* mod;
            int32_t dirMask;
            LeviRsPacketCb cb;
            void* user;
        };

        struct ConnSub
        {
            RustMod* mod;
            LeviRsConnCb cb;
            void* user;
        };

        using PacketSubs = std::vector<std::shared_ptr<PacketSub>>;
        using ConnSubs = std::vector<std::shared_ptr<ConnSub>>;

        std::shared_mutex& registryLock()
        {
            static std::shared_mutex m;
            return m;
        }

        PacketSubs& packetSubs()
        {
            static PacketSubs v;
            return v;
        }

        ConnSubs& connSubs()
        {
            static ConnSubs v;
            return v;
        }

        /**
         * Hot-path gates. Read without taking the lock: a stale `false` costs
         * one packet of missed interception during the tick a subscriber first
         * appears, and a stale `true` costs one empty snapshot. Neither is
         * worth a lock on every packet.
         */
        std::atomic<bool> gInboundLive{false};
        std::atomic<bool> gOutboundLive{false};
        std::atomic<bool> gConnLive{false};

        void refreshGatesLocked()
        {
            bool in = false;
            bool out = false;
            for (auto const& s : packetSubs())
            {
                if (s->dirMask & LEVI_RS_PKT_MASK_INBOUND) in = true;
                if (s->dirMask & LEVI_RS_PKT_MASK_OUTBOUND) out = true;
            }
            gInboundLive.store(in, std::memory_order_relaxed);
            gOutboundLive.store(out, std::memory_order_relaxed);
            gConnLive.store(!connSubs().empty(), std::memory_order_relaxed);
        }

        /**
         * Address cache. `getIPAndPort()` builds a std::string every call, and
         * the packet path is the hottest loop in the server — so resolve it
         * once per connection and hand out views into the cached copy.
         *
         * Held by shared_ptr, not by value: dispatch needs a view that stays
         * valid for the whole callback chain, and another thread may evict the
         * entry (connection closed) while that chain is running. A refcount
         * bump keeps the string alive without copying it per packet.
         *
         * Populated on connection open, dropped on close. The fallback path
         * (hook installed mid-session, so the open was missed) fills it in on
         * first sight rather than re-resolving forever.
         */
        using AddressPtr = std::shared_ptr<std::string const>;

        std::unordered_map<uint64_t, AddressPtr>& addressCache()
        {
            static std::unordered_map<uint64_t, AddressPtr> m;
            return m;
        }

        std::mutex& addressLock()
        {
            static std::mutex m;
            return m;
        }

        /** Cached "host:port" for an identifier, inserting it if unseen. */
        AddressPtr addressOf(::NetworkIdentifier const& id)
        {
            uint64_t const hash = id.getHash();
            {
                std::lock_guard<std::mutex> g{addressLock()};
                auto it = addressCache().find(hash);
                if (it != addressCache().end()) return it->second;
            }
            std::string addr;
            try
            {
                addr = id.getIPAndPort();
            }
            catch (...)
            {
                addr.clear();
            }
            auto ptr = std::make_shared<std::string const>(std::move(addr));
            std::lock_guard<std::mutex> g{addressLock()};
            // A concurrent insert is fine: same key, equivalent value. Keep
            // whichever landed first so both threads report the same pointer.
            return addressCache().emplace(hash, std::move(ptr)).first->second;
        }

        void forgetAddress(uint64_t hash)
        {
            std::lock_guard<std::mutex> g{addressLock()};
            addressCache().erase(hash);
        }

        /* ───────────────────────── header codec ───────────────────────── */

        constexpr uint32_t kPacketIdMask = 0x3FF; // bits 0..9
        constexpr uint32_t kSubIdMask = 0x3;

        /** LEB128 decode. Returns false on truncation or an over-long varint. */
        bool readUVarInt(uint8_t const* data, size_t len, size_t& pos, uint32_t& out)
        {
            uint32_t result = 0;
            uint32_t shift = 0;
            while (pos < len)
            {
                uint8_t const b = data[pos++];
                result |= static_cast<uint32_t>(b & 0x7F) << shift;
                if ((b & 0x80) == 0)
                {
                    out = result;
                    return true;
                }
                shift += 7;
                if (shift >= 35) return false; // longer than a uint32 can be
            }
            return false;
        }

        void writeUVarInt(std::string& out, uint32_t value)
        {
            while (value >= 0x80)
            {
                out.push_back(static_cast<char>((value & 0x7F) | 0x80));
                value >>= 7;
            }
            out.push_back(static_cast<char>(value));
        }

        struct Header
        {
            uint32_t raw;
            int32_t packetId;
            uint8_t senderSubId;
            uint8_t targetSubId;
            size_t size; // bytes consumed
        };

        bool decodeHeader(std::string const& packet, Header& out)
        {
            auto const* data = reinterpret_cast<uint8_t const*>(packet.data());
            size_t pos = 0;
            uint32_t raw = 0;
            if (!readUVarInt(data, packet.size(), pos, raw)) return false;
            out.raw = raw;
            out.packetId = static_cast<int32_t>(raw & kPacketIdMask);
            out.senderSubId = static_cast<uint8_t>((raw >> 10) & kSubIdMask);
            out.targetSubId = static_cast<uint8_t>((raw >> 12) & kSubIdMask);
            out.size = pos;
            return true;
        }

        void encodeHeader(std::string& out, LeviRsPacketEdit const& edit)
        {
            uint32_t const raw = (static_cast<uint32_t>(edit.packet_id) & kPacketIdMask)
                               | ((static_cast<uint32_t>(edit.sender_sub_id) & kSubIdMask) << 10)
                               | ((static_cast<uint32_t>(edit.target_sub_id) & kSubIdMask) << 12);
            writeUVarInt(out, raw);
        }

        /* ───────────────────────── dispatch ───────────────────────── */

        /** Sink target for LEVI_RS_PKT_REPLACE. */
        struct ReplaceBuf
        {
            std::string bytes;
            bool written = false;
        };

        void replaceSink(void* ctx, uint8_t const* data, size_t len)
        {
            auto* buf = static_cast<ReplaceBuf*>(ctx);
            if (!buf) return;
            buf->written = true;
            buf->bytes.assign(reinterpret_cast<char const*>(data), len);
        }

        enum class Verdict
        {
            Pass,
            Replaced,
            Drop
        };

        /**
         * Re-entrancy guard. A subscriber is allowed to send packets from
         * inside its callback (that is half the point of having the hook);
         * without this, the resulting `_sendInternal` would recurse straight
         * back into dispatch and, in the worst case, into itself forever.
         * Packets sent from inside a callback go out untouched.
         */
        thread_local int tlDispatchDepth = 0;

        struct DepthGuard
        {
            DepthGuard() { ++tlDispatchDepth; }
            ~DepthGuard() { --tlDispatchDepth; }
        };

        PacketSubs snapshotFor(int32_t direction)
        {
            int32_t const mask = 1 << direction;
            PacketSubs out;
            std::shared_lock<std::shared_mutex> g{registryLock()};
            out.reserve(packetSubs().size());
            for (auto const& s : packetSubs())
            {
                if (s->dirMask & mask) out.push_back(s);
            }
            return out;
        }

        /**
         * Run every interested subscriber over `in` (header + body), in
         * registration order, each seeing the previous one's output.
         *
         * On Verdict::Replaced the rebuilt packet lands in `out` and `in` is
         * untouched; on Pass and Drop `out` is meaningless. Splitting input
         * from output is what lets the untranslated path stay allocation-free:
         * the body is only ever copied once a subscriber actually rewrites it,
         * which matters because a chunk packet is tens of kilobytes and the
         * overwhelming majority of packets are forwarded verbatim.
         *
         * On any malformed input the packet is left exactly as it was — a
         * translator that cannot parse something must not be able to corrupt it.
         *
         * Precondition: `out` must not alias `in`. The rebuild reads from a
         * view that may still point into `in`.
         */
        Verdict dispatch(
            int32_t direction,
            ::NetworkIdentifier const& id,
            std::string const& in,
            std::string& out,
            int32_t expectedId
        )
        {
            if (tlDispatchDepth > 0) return Verdict::Pass;

            Header header{};
            if (!decodeHeader(in, header)) return Verdict::Pass;

            // Layout sanity check on the outbound side, where we have the
            // packet object to compare against. A mismatch means the header
            // encoding assumed above no longer holds; say so once and stop
            // touching packets rather than silently mangling them.
            if (expectedId >= 0 && header.packetId != expectedId)
            {
                static std::atomic<bool> warned{false};
                if (!warned.exchange(true))
                {
                    bridgeLogger().error(
                        "[PacketHooks] 包头解析结果与 Packet::getId() 不一致"
                        "（解析得到 {}，实际 {}）。说明这个 BDS 版本的包头布局变了，"
                        "拦截已自动降级为全部放行 —— 请把这条日志报告给 loader 维护者。",
                        header.packetId,
                        expectedId
                    );
                }
                return Verdict::Pass;
            }

            auto subs = snapshotFor(direction);
            if (subs.empty()) return Verdict::Pass;

            AddressPtr const address = addressOf(id);
            uint64_t const connId = id.getHash();

            // `curPtr`/`curLen` are what the next subscriber sees. They start
            // as a view into `in` and only move into `owned` once somebody
            // rewrites the body.
            auto const* curPtr = reinterpret_cast<uint8_t const*>(in.data()) + header.size;
            size_t curLen = in.size() - header.size;
            std::string owned;

            LeviRsPacketEdit edit{
                static_cast<uint32_t>(sizeof(LeviRsPacketEdit)),
                header.packetId,
                header.senderSubId,
                header.targetSubId,
            };

            bool changed = false;
            {
                DepthGuard depth;
                for (auto const& sub : subs)
                {
                    LeviRsPacketEvent ev{
                        static_cast<uint32_t>(sizeof(LeviRsPacketEvent)),
                        direction,
                        connId,
                        LeviRsStr{*address},
                        edit.packet_id,
                        edit.sender_sub_id,
                        edit.target_sub_id,
                        curLen == 0 ? nullptr : curPtr,
                        curLen,
                    };

                    ReplaceBuf buf;
                    LeviRsPacketEdit pending = edit;
                    int32_t verdict = LEVI_RS_PKT_PASS;
                    try
                    {
                        verdict = sub->cb(sub->user, &ev, &pending, &buf, &replaceSink);
                    }
                    catch (...)
                    {
                        // A throwing callback is a bug on the mod's side, but
                        // it must not take the connection with it.
                        verdict = LEVI_RS_PKT_PASS;
                    }

                    if (verdict == LEVI_RS_PKT_DROP) return Verdict::Drop;
                    if (verdict != LEVI_RS_PKT_REPLACE) continue;

                    owned = buf.written ? std::move(buf.bytes) : std::string{};
                    curPtr = reinterpret_cast<uint8_t const*>(owned.data());
                    curLen = owned.size();
                    pending.struct_size = static_cast<uint32_t>(sizeof(LeviRsPacketEdit));
                    edit = pending;
                    changed = true;
                }
            }

            if (!changed) return Verdict::Pass;

            out.clear();
            out.reserve(curLen + 5); // 5 = max varint32 header
            encodeHeader(out, edit);
            out.append(reinterpret_cast<char const*>(curPtr), curLen);
            return Verdict::Replaced;
        }

        void dispatchConn(::NetworkIdentifier const& id, bool opened)
        {
            if (!gConnLive.load(std::memory_order_relaxed)) return;

            ConnSubs subs;
            {
                std::shared_lock<std::shared_mutex> g{registryLock()};
                subs = connSubs();
            }
            if (subs.empty()) return;

            uint64_t const connId = id.getHash();
            AddressPtr const address = addressOf(id);

            for (auto const& sub : subs)
            {
                try
                {
                    sub->cb(sub->user, connId, LeviRsStr{*address}, opened);
                }
                catch (...)
                {
                    // Same rule as the packet path: never let a mod's
                    // exception escape into the network stack.
                }
            }
        }

        /* ───────────────────────── detours ───────────────────────── */

        /**
         * Outbound. `data` is the fully serialised packet (header + body),
         * before batching and compression. Rewriting means calling origin with
         * our own string; dropping means not calling it at all.
         */
        LL_TYPE_INSTANCE_HOOK(
            LeviRsPacketSendHook,
            ll::memory::HookPriority::Normal,
            NetworkSystem,
            &NetworkSystem::_sendInternal,
            void,
            ::NetworkIdentifier const& id,
            ::Packet const& packet,
            ::std::string const& data)
        {
            if (!gOutboundLive.load(std::memory_order_relaxed)) return origin(id, packet, data);

            int32_t expectedId = -1;
            try
            {
                expectedId = static_cast<int32_t>(packet.getId());
            }
            catch (...)
            {
                expectedId = -1;
            }

            std::string rewritten;
            switch (dispatch(LEVI_RS_PKT_OUTBOUND, id, data, rewritten, expectedId))
            {
            case Verdict::Drop:
                return;
            case Verdict::Replaced:
                return origin(id, packet, rewritten);
            case Verdict::Pass:
            default:
                return origin(id, packet, data);
            }
        }

        /**
         * Inbound. One packet per HasData. A dropped packet must not stop the
         * pump — returning NoData would strand every packet still queued for
         * this tick — so we pull the next one instead and only return when
         * something survives (or the peer runs dry).
         */
        LL_TYPE_INSTANCE_HOOK(
            LeviRsPacketReceiveHook,
            ll::memory::HookPriority::Normal,
            NetworkConnection,
            &NetworkConnection::receivePacket,
            ::NetworkPeer::DataStatus,
            ::std::string& receiveBuffer,
            ::std::shared_ptr<::std::chrono::steady_clock::time_point> const& timepointPtr)
        {
            if (!gInboundLive.load(std::memory_order_relaxed)) return origin(receiveBuffer, timepointPtr);

            // Reused across iterations so a burst of rewritten packets shares
            // one buffer instead of reallocating per packet.
            std::string rewritten;

            for (;;)
            {
                auto const status = origin(receiveBuffer, timepointPtr);
                if (status != ::NetworkPeer::DataStatus::HasData) return status;

                switch (dispatch(
                    LEVI_RS_PKT_INBOUND, this->mId.get(), receiveBuffer, rewritten, /*expectedId=*/-1
                ))
                {
                case Verdict::Drop:
                    // Do NOT return NoData here: that would strand every packet
                    // still queued for this tick. Ask the peer for the next one.
                    continue;
                case Verdict::Replaced:
                    receiveBuffer = rewritten;
                    return status;
                case Verdict::Pass:
                default:
                    return status;
                }
            }
        }

        /** Connection accepted — the earliest point a conn_id exists. */
        LL_TYPE_INSTANCE_HOOK(
            LeviRsConnOpenHook,
            ll::memory::HookPriority::Normal,
            NetworkSystem,
            &NetworkSystem::$onNewIncomingConnection,
            bool,
            ::NetworkIdentifier const& id,
            ::std::shared_ptr<::NetworkPeer>&& peer)
        {
            bool const accepted = origin(id, std::move(peer));
            if (accepted) dispatchConn(id, /*opened=*/true);
            return accepted;
        }

        /**
         * Connection closed. Notify first, then evict the address entry —
         * subscribers still want a resolvable address in their close handler.
         */
        LL_TYPE_INSTANCE_HOOK(
            LeviRsConnCloseHook,
            ll::memory::HookPriority::Normal,
            NetworkSystem,
            &NetworkSystem::$onConnectionClosed,
            void,
            ::NetworkIdentifier const& id,
            ::Connection::DisconnectFailReason const discoReason,
            ::std::string const& messageFromServer,
            ::std::string const& messageBodyOverride,
            bool skipDisconnectMessage,
            ::Json::Value const& sessionSummary)
        {
            dispatchConn(id, /*opened=*/false);
            forgetAddress(id.getHash());
            origin(id, discoReason, messageFromServer, messageBodyOverride, skipDisconnectMessage, sessionSummary);
        }

        /**
         * Install every detour once, on the first subscriber of any kind.
         * Never unhooked: an unsubscribe can arrive from inside a hook body.
         *
         * Function-local static initialisation rather than the plain `bool`
         * flag the other lazy hooks in this bridge use — those all run on the
         * server thread, and registration here does not have to.
         */
        void ensureInstalled()
        {
            static bool const installed = [] {
                LeviRsPacketSendHook::hook();
                LeviRsPacketReceiveHook::hook();
                LeviRsConnOpenHook::hook();
                LeviRsConnCloseHook::hook();
                bridgeLogger().debug("[PacketHooks] 已安装收发包 detour");
                return true;
            }();
            (void)installed;
        }
    } // namespace

    /* ───────────────────────── ABI entry points ───────────────────────── */

    LeviRsPacketHookHandle
    api_packet_hook_register(LeviRsModHandle mod, int32_t dirMask, LeviRsPacketCb cb, void* user)
    {
        if (!cb) return nullptr;
        // A zero mask would register something that can never fire; refuse it
        // instead of handing back a handle that does nothing.
        if ((dirMask & (LEVI_RS_PKT_MASK_INBOUND | LEVI_RS_PKT_MASK_OUTBOUND)) == 0) return nullptr;

        auto sub = std::make_shared<PacketSub>(PacketSub{asMod(mod), dirMask, cb, user});
        PacketSub* raw = sub.get();
        {
            std::unique_lock<std::shared_mutex> g{registryLock()};
            packetSubs().push_back(std::move(sub));
            refreshGatesLocked();
        }
        ensureInstalled();
        return static_cast<LeviRsPacketHookHandle>(raw);
    }

    bool api_packet_hook_unregister(LeviRsModHandle mod, LeviRsPacketHookHandle handle)
    {
        if (!handle) return false;
        auto* target = static_cast<PacketSub*>(handle);
        std::unique_lock<std::shared_mutex> g{registryLock()};
        auto& subs = packetSubs();
        for (auto it = subs.begin(); it != subs.end(); ++it)
        {
            if (it->get() != target) continue;
            // Ownership check: a mod may only drop its own interceptors.
            if ((*it)->mod != asMod(mod)) return false;
            subs.erase(it);
            refreshGatesLocked();
            return true;
        }
        return false;
    }

    LeviRsPacketHookHandle api_packet_conn_hook_register(LeviRsModHandle mod, LeviRsConnCb cb, void* user)
    {
        if (!cb) return nullptr;

        auto sub = std::make_shared<ConnSub>(ConnSub{asMod(mod), cb, user});
        ConnSub* raw = sub.get();
        {
            std::unique_lock<std::shared_mutex> g{registryLock()};
            connSubs().push_back(std::move(sub));
            refreshGatesLocked();
        }
        ensureInstalled();
        return static_cast<LeviRsPacketHookHandle>(raw);
    }

    bool api_packet_conn_hook_unregister(LeviRsModHandle mod, LeviRsPacketHookHandle handle)
    {
        if (!handle) return false;
        auto* target = static_cast<ConnSub*>(handle);
        std::unique_lock<std::shared_mutex> g{registryLock()};
        auto& subs = connSubs();
        for (auto it = subs.begin(); it != subs.end(); ++it)
        {
            if (it->get() != target) continue;
            if ((*it)->mod != asMod(mod)) return false;
            subs.erase(it);
            refreshGatesLocked();
            return true;
        }
        return false;
    }

    void packetHooksOnRustModGone(RustMod* mod)
    {
        std::unique_lock<std::shared_mutex> g{registryLock()};
        auto& psubs = packetSubs();
        for (auto it = psubs.begin(); it != psubs.end();)
        {
            it = ((*it)->mod == mod) ? psubs.erase(it) : it + 1;
        }
        auto& csubs = connSubs();
        for (auto it = csubs.begin(); it != csubs.end();)
        {
            it = ((*it)->mod == mod) ? csubs.erase(it) : it + 1;
        }
        refreshGatesLocked();
    }
} // namespace levi_rs::bridge
