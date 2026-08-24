/**
 * bridge/Packets.cpp — per-connection packet delivery (additive, ABI v5,
 * struct_size-gated).
 *
 * Two layers:
 *   - api_send_packet: the raw primitive. Any MinecraftPacketIds + a
 *     wire-format body, deserialised into a real packet object and handed to
 *     ONE player's connection. This is the escape hatch that makes every
 *     "just send a packet" feature possible without further bridge work.
 *   - api_spawn_particle_for / api_player_send_title: typed derivations of the
 *     same send path. They construct the packet in C++ (version-safe: no wire
 *     format crosses the FFI) and reuse the same delivery helper.
 *
 * api_player_send_title exists because the old title route (player_action
 * opcode PACT_SET_TITLE) shelled out to `/title "<name>" title <text>`, which
 * breaks on quotes, expands selectors in the text, and cannot set timings.
 *
 * Deliberately NOT exposed: broadcast variants (Level already broadcasts;
 * mods can loop players when they truly mean "everyone").
 *
 * Reading or rewriting packets that already exist is the other half of the
 * story and lives in PacketHooks.cpp — this file only manufactures new ones.
 */
#include "bridge/Api.h"
#include "bridge/Common.h"

#include <memory>
#include <string>
#include <string_view>

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/utility/ReadOnlyBinaryStream.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/Packet.h"
#include "mc/network/packet/SetTitlePacket.h"
#include "mc/network/packet/SetTitlePacketPayload.h"
#include "mc/network/packet/SpawnParticleEffectPacket.h"
#include "mc/world/actor/player/Player.h"

namespace levi_rs::bridge
{
    namespace
    {
        /// Shared delivery: resolve the target and hand a ready packet to that
        /// single connection. Both the raw and the typed entry end here.
        bool sendToPlayer(LeviRsPlayerSel sel, Packet& pkt)
        {
            Player* p = resolvePlayer(sel);
            if (!p) return false;
            p->sendNetworkPacket(pkt);
            return true;
        }
    } // namespace

    bool api_send_packet(LeviRsPlayerSel sel, int32_t packetId, uint8_t const* body, size_t bodyLen)
    {
        if (!body && bodyLen != 0) return false;

        auto pkt = MinecraftPackets::createPacket(static_cast<MinecraftPacketIds>(packetId));
        if (!pkt) return false;

        // Deserialise the caller-supplied body into the packet object. The
        // stream borrows the bytes (copyBuffer=false) — valid for this frame.
        std::string_view raw{reinterpret_cast<char const*>(body), bodyLen};
        ReadOnlyBinaryStream stream{raw, /*copyBuffer=*/false};
        if (!pkt->read(stream)) return false;
        // The body must be *exactly* one packet: trailing garbage means the
        // caller serialised the wrong shape for this game version — refuse
        // early instead of sending a half-parsed packet to a client.
        if (!stream.ensureReadCompleted()) return false;

        return sendToPlayer(sel, *pkt);
    }

    bool api_player_send_title(
        LeviRsPlayerSel sel, int32_t type, LeviRsStr text, int32_t fadeInTicks, int32_t stayTicks,
        int32_t fadeOutTicks)
    {
        using TitleType = SetTitlePacketPayload::TitleType;

        // 6..8 are the TextObject variants; their payload constructor needs a
        // ResolvedTextObject, which has no meaning across this FFI boundary.
        // Refuse rather than silently degrade to the plain-string variant —
        // the caller asked for a different thing than it would have got.
        if (type < 0 || type > 5) return false;
        auto const kind = static_cast<TitleType>(type);

        // Either all three durations are specified or none are. A mix has no
        // defensible reading: "fade in over 5 ticks and stay for whatever the
        // client happened to have" is a bug at the call site, not a request.
        int const specified =
            (fadeInTicks >= 0 ? 1 : 0) + (stayTicks >= 0 ? 1 : 0) + (fadeOutTicks >= 0 ? 1 : 0);
        if (specified != 0 && specified != 3) return false;
        bool const withTimes = (specified == 3);

        Player* p = resolvePlayer(sel);
        if (!p) return false;

        // Times, when asked for, goes first and as its own packet — that is
        // what `/title <who> times a b c` sends, and the client applies it to
        // titles that arrive *after* it. Putting the durations only in the
        // content packet works on some versions and not others; sending the
        // Times packet is the behaviour vanilla itself relies on.
        if (withTimes)
        {
            SetTitlePacket times;
            times.mType        = TitleType::Times;
            times.mFadeInTime  = fadeInTicks;
            times.mStayTime    = stayTicks;
            times.mFadeOutTime = fadeOutTicks;
            p->sendNetworkPacket(times);
            // `type == 5` means the caller wanted only the timing change.
            if (kind == TitleType::Times) return true;
        }
        else if (kind == TitleType::Times)
        {
            // Times with no durations is a no-op request, not a valid packet.
            return false;
        }

        // ll::PayloadPacket<T> derives from T (mc/network/Packet.h:204), so the
        // payload fields live directly on the packet — same access pattern as
        // SpawnParticleEffectPacket above. No wire format is involved.
        SetTitlePacket pkt;
        pkt.mType = kind;
        if (kind == TitleType::Title || kind == TitleType::Subtitle
            || kind == TitleType::Actionbar)
        {
            pkt.mTitleText = std::string{text};
        }
        if (withTimes)
        {
            pkt.mFadeInTime  = fadeInTicks;
            pkt.mStayTime    = stayTicks;
            pkt.mFadeOutTime = fadeOutTicks;
        }
        return sendToPlayer(sel, pkt);
    }

    bool api_spawn_particle_for(
        LeviRsPlayerSel sel, int32_t dimension, LeviRsStr effectName, double x, double y, double z)
    {
        // Typed construction: the MCAPI default constructor initialises the
        // packet (serialization mode) and the payload defaults (mActorId =
        // invalid, mMolangVariables = nullopt); we fill the three fields that
        // matter. No wire format involved — survives version bumps that
        // api_send_packet callers would have to track themselves.
        SpawnParticleEffectPacket pkt;
        pkt.mVanillaDimensionId = static_cast<uchar>(dimension);
        pkt.mPos                = Vec3{(float)x, (float)y, (float)z};
        pkt.mEffectName         = std::string{effectName};
        return sendToPlayer(sel, pkt);
    }
} // namespace levi_rs::bridge
