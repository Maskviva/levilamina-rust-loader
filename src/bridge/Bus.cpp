/**
 * bridge/Bus.cpp — cross-mod event bus (additive, ABI v5, struct_size-gated).
 *
 * # Why the loader owns the table
 *
 * The obvious design — mod A exports a "subscribe" function, mod B hands it a
 * callback — cannot be made safe here. `RustModManager::unload` calls
 * `FreeLibrary`, so the moment B unloads, A is holding a function pointer into
 * an unmapped dylib and the next publish is a crash with no diagnostic. Every
 * asynchronous surface in this bridge has already had to solve this once
 * (Forms.cpp, the mod-scoped scheduler in LogScheduler.cpp), and the answer is
 * the same each time: the loader keeps the table, entries are keyed by ticket,
 * the fire path holds a `weak_ptr<RustMod>` and revalidates before it calls.
 *
 * # What the loader does not do
 *
 * It does not parse the payload. `topic` and `payload` are opaque UTF-8 that
 * the two mods agree on out-of-band. A loader-defined schema would mean every
 * publisher has to satisfy it, and the loader has to version it — for zero
 * benefit, since the loader has no use for the contents.
 *
 * # Loops
 *
 * Two independent guards, because they catch different shapes:
 *
 *   - a mod never receives its own publishes. A mod that wants to notify
 *     itself has a direct function call; self-delivery is the one loop that
 *     no depth limit can tell apart from real work.
 *   - a depth cap on nested dispatch catches A → B → A. Hitting it drops the
 *     innermost publish and logs once, rather than growing the stack until
 *     the server dies.
 *
 * # Dispatch and the lock
 *
 * The subscriber list is snapshotted under the lock and every callback is
 * invoked with the lock **released**. Subscribers routinely re-enter
 * (subscribe, unsubscribe, publish something else), and holding the lock
 * across a call into a dylib would deadlock the server thread on the first one
 * that does. Each entry is re-validated by ticket immediately before it is
 * called, so a subscription removed *during* a dispatch is never invoked.
 */
#include "bridge/Api.h"
#include "bridge/Common.h"

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ll/api/io/Logger.h"

#include "RustMod.h"

namespace levi_rs::bridge
{
    namespace
    {
        /** Longest accepted topic. Long enough for `some-long-mod:some-event`,
         *  short enough that a garbage pointer read as a string cannot turn
         *  into a multi-megabyte map key. */
        constexpr size_t kMaxTopic = 128;

        /** Nested dispatch limit. 8 is far more than any real chain (publish →
         *  handler → publish is depth 2); anything deeper is a loop. */
        constexpr int kMaxDepth = 8;

        struct Subscription
        {
            RustMod* mod = nullptr; // identity only; never dereferenced blind
            std::string topic;
            LeviRsBusCb cb = nullptr;
            void* user = nullptr;
        };

        std::mutex gBusMutex;
        std::unordered_map<uint64_t, Subscription> gSubs;
        /** topic -> subscription ids, so publish does not scan the whole table.
         *  Kept in sync with gSubs under the same lock. */
        std::unordered_map<std::string, std::vector<uint64_t>> gByTopic;
        uint64_t gNextSubId = 1;

        /** Per-thread nesting depth. Thread-local rather than a global counter:
         *  two threads publishing concurrently are not a loop, and a shared
         *  counter would make them look like one. */
        thread_local int gDepth = 0;

        struct DepthGuard
        {
            DepthGuard() { ++gDepth; }
            ~DepthGuard() { --gDepth; }
        };

        /** Say "too deep" once per topic, then stay quiet. A loop fires as fast
         *  as the CPU allows; logging every hit turns a bug into an outage. */
        void warnDepthOnce(std::string const& topic)
        {
            static std::mutex mu;
            static std::unordered_map<std::string, bool> seen;
            std::lock_guard lock(mu);
            if (seen[topic]) return;
            seen[topic] = true;
            bridgeLogger().error(
                "cross-mod bus: topic '{}' exceeded nesting depth {} — dropping the innermost "
                "publish. This is a publish loop: some subscriber of this topic publishes it "
                "again (directly, or via another topic that leads back here).",
                topic, kMaxDepth
            );
        }

        /** Snapshot the ids subscribed to `topic`, excluding `publisher`'s own. */
        std::vector<uint64_t> idsFor(std::string const& topic, RustMod* publisher)
        {
            std::vector<uint64_t> out;
            std::lock_guard lock(gBusMutex);
            auto it = gByTopic.find(topic);
            if (it == gByTopic.end()) return out;
            out.reserve(it->second.size());
            for (uint64_t id : it->second)
            {
                auto s = gSubs.find(id);
                if (s == gSubs.end()) continue;
                if (s->second.mod == publisher) continue; // no self-delivery
                out.push_back(id);
            }
            return out;
        }

        /**
         * Fire one subscription by ticket. Returns the veto bit; `ran` is set
         * when the callback actually executed.
         *
         * The lookup and the call are separated on purpose: the entry is copied
         * out under the lock, the lock is dropped, and only then does control
         * cross into the dylib.
         */
        bool fireOne(uint64_t id, std::string_view topic, std::string_view payload, bool& ran)
        {
            ran = false;
            Subscription sub;
            {
                std::lock_guard lock(gBusMutex);
                auto it = gSubs.find(id);
                // Gone: unsubscribed by an earlier subscriber in this same
                // dispatch, or its mod unloaded mid-dispatch.
                if (it == gSubs.end()) return false;
                sub = it->second;
            }
            if (!sub.cb || !sub.mod) return false;

            std::weak_ptr<RustMod> weakMod;
            try
            {
                weakMod = sub.mod->shared_from_this();
            }
            catch (...)
            {
                return false;
            }
            auto mod = weakMod.lock();
            if (!mod || mod.get() != sub.mod) return false; // dylib may be unmapped
            if (!mod->isEnabled()) return false;            // muted while disabled

            ran = true;
            return sub.cb(sub.user, topic, payload);
        }

        bool publishImpl(
            LeviRsModHandle modHandle, LeviRsStr topicRaw, LeviRsStr payloadRaw, uint32_t* outDelivered)
        {
            if (outDelivered) *outDelivered = 0;

            std::string topic{std::string_view{topicRaw}};
            if (topic.empty() || topic.size() > kMaxTopic) return false;

            if (gDepth >= kMaxDepth)
            {
                warnDepthOnce(topic);
                return false;
            }
            DepthGuard depth;

            auto* publisher = modHandle ? asMod(modHandle) : nullptr;
            auto ids = idsFor(topic, publisher);

            std::string_view payload{payloadRaw};
            bool vetoed = false;
            uint32_t delivered = 0;
            for (uint64_t id : ids)
            {
                bool ran = false;
                // No short-circuit on veto: observers must see a consistent
                // stream regardless of whether an earlier subscriber refused.
                // Skipping the rest would make what a mod observes depend on
                // subscriber order, which no mod controls.
                bool v = fireOne(id, topic, payload, ran);
                if (ran)
                {
                    ++delivered;
                    vetoed = vetoed || v;
                }
            }
            if (outDelivered) *outDelivered = delivered;
            return vetoed;
        }
    } // namespace

    uint64_t api_bus_subscribe(LeviRsModHandle modHandle, LeviRsStr topicRaw, LeviRsBusCb cb, void* user)
    {
        if (!cb || !modHandle) return 0;
        auto* raw = asMod(modHandle);
        if (!raw) return 0;

        std::string topic{std::string_view{topicRaw}};
        if (topic.empty() || topic.size() > kMaxTopic) return 0;

        // Refuse a mod that is not owned by a shared_ptr yet: without one there
        // is no weak_ptr to revalidate against later, and an unvalidated call
        // into a dylib is exactly what this table exists to prevent.
        try
        {
            (void)raw->shared_from_this();
        }
        catch (...)
        {
            return 0;
        }

        std::lock_guard lock(gBusMutex);
        uint64_t id = gNextSubId++;
        gSubs[id] = Subscription{raw, topic, cb, user};
        gByTopic[topic].push_back(id);
        return id;
    }

    bool api_bus_unsubscribe(LeviRsModHandle modHandle, uint64_t subId)
    {
        if (!modHandle || subId == 0) return false;
        auto* raw = asMod(modHandle);

        std::lock_guard lock(gBusMutex);
        auto it = gSubs.find(subId);
        // Scoped to the caller: one mod must not be able to silence another.
        if (it == gSubs.end() || it->second.mod != raw) return false;

        auto byTopic = gByTopic.find(it->second.topic);
        if (byTopic != gByTopic.end())
        {
            auto& v = byTopic->second;
            v.erase(std::remove(v.begin(), v.end(), subId), v.end());
            if (v.empty()) gByTopic.erase(byTopic);
        }
        gSubs.erase(it);
        return true;
    }

    uint32_t api_bus_publish(LeviRsModHandle modHandle, LeviRsStr topic, LeviRsStr payload)
    {
        uint32_t delivered = 0;
        (void)publishImpl(modHandle, topic, payload, &delivered);
        return delivered;
    }

    bool api_bus_publish_vetoable(
        LeviRsModHandle modHandle, LeviRsStr topic, LeviRsStr payload, uint32_t* outDelivered)
    {
        return publishImpl(modHandle, topic, payload, outDelivered);
    }

    uint32_t api_bus_subscriber_count(LeviRsStr topicRaw)
    {
        std::string topic{std::string_view{topicRaw}};
        if (topic.empty()) return 0;
        std::lock_guard lock(gBusMutex);
        auto it = gByTopic.find(topic);
        return it == gByTopic.end() ? 0u : static_cast<uint32_t>(it->second.size());
    }

    void busOnRustModGone(RustMod* mod)
    {
        std::lock_guard lock(gBusMutex);
        for (auto it = gSubs.begin(); it != gSubs.end();)
        {
            if (it->second.mod != mod)
            {
                ++it;
                continue;
            }
            auto byTopic = gByTopic.find(it->second.topic);
            if (byTopic != gByTopic.end())
            {
                auto& v = byTopic->second;
                uint64_t id = it->first;
                v.erase(std::remove(v.begin(), v.end(), id), v.end());
                if (v.empty()) gByTopic.erase(byTopic);
            }
            it = gSubs.erase(it);
        }
    }
} // namespace levi_rs::bridge
