/**
 * bridge/Packets.cpp — per-connection packet delivery (additive, ABI v5,
 * struct_size-gated).
 *
 * Two layers:
 *   - api_send_packet: the raw primitive. Any MinecraftPacketIds + a
 *     wire-format body, deserialised into a real packet object and handed to
 *     ONE player's connection. This is the escape hatch that makes every
 *     "just send a packet" feature possible without further bridge work.
 *   - api_spawn_particle_for: a typed derivation of the same send path. It
 *     constructs SpawnParticleEffectPacket in C++ (version-safe: no wire
 *     format crosses the FFI) and reuses the same delivery helper.
 *
 * Deliberately NOT exposed: broadcast variants (Level already broadcasts;
 * mods can loop players when they truly mean "everyone"), and receiving /
 * intercepting packets (that is hook territory — see ROADMAP §12).
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
