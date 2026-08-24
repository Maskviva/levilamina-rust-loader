/**
 * bridge/SimPlayer.cpp — simulated ("fake") players, ROADMAP §7.
 *
 * Two ABI entries only:
 *   - sim_spawn(name, dim, x, y, z): SimulatedPlayer::create (LeviLamina's
 *     convenience wrapper). The result is a real ServerPlayer with that name,
 *     so EVERY existing per-player API (teleport, health, inventory, kick,
 *     send_message, position, …) works on it through the usual name selector
 *     — no duplicate surface needed.
 *   - sim_do(sel, action, args_snbt): a multiplexed verb dispatcher over the
 *     simulate* family. New verbs are added HERE, bridge-side, without
 *     growing the ABI table — the action vocabulary is data, not layout.
 *     Gated on Actor::isSimulatedPlayer() so a real player can never be
 *     puppeted. args is SNBT ({} / "" for verbs without parameters).
 *
 * Verbs (args in braces, defaults after '='):
 *   despawn | stop | jump | attack | interact | use_item | drop | respawn
 *   move_to{x,y,z,speed=1,face_target=1}      direct movement
 *   navigate_to{x,y,z,speed=4.3}              pathfinding movement
 *   look_at{x,y,z}
 *   destroy_block{x,y,z,face=1} | destroy_look{hand=5.5} | stop_destroy
 *   interact_block{x,y,z,face=1}
 *   sneak{on=1} | fly{on=1}
 *   chat{msg}
 */
#include "bridge/Api.h"
#include "bridge/Common.h"

#include <cmath>
#include <string>
#include <string_view>

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/scripting/modules/gametest/ScriptNavigationResult.h"
#include "mc/scripting/modules/minecraft/ScriptFacing.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/server/sim/LookDuration.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/Level.h"

namespace levi_rs::bridge
{
    namespace
    {
        double argD(CompoundTag const& t, std::string_view key, double def)
        {
            if (!t.contains(key)) return def;
            return nbtToDouble(t.at(key), def);
        }

        bool argB(CompoundTag const& t, std::string_view key, bool def)
        {
            if (!t.contains(key)) return def;
            return nbtToDouble(t.at(key), def != 0.0) != 0.0;
        }

        std::string argS(CompoundTag const& t, std::string_view key)
        {
            if (!t.contains(key)) return {};
            return std::string{static_cast<std::string_view>(t.at(key))};
        }

        BlockPos argBlockPos(CompoundTag const& t)
        {
            return BlockPos{
                static_cast<int>(std::floor(argD(t, "x", 0))),
                static_cast<int>(std::floor(argD(t, "y", 0))),
                static_cast<int>(std::floor(argD(t, "z", 0))),
            };
        }

        ::ScriptModuleMinecraft::ScriptFacing argFace(CompoundTag const& t)
        {
            int f = static_cast<int>(argD(t, "face", 1)); // default: Up
            if (f < 0 || f > 5) f = 1;
            return static_cast<::ScriptModuleMinecraft::ScriptFacing>(f);
        }
    } // namespace

    bool api_sim_spawn(LeviRsStr name, int32_t dimension, double x, double y, double z)
    {
        if (!levelReady()) return false;
        auto sp = SimulatedPlayer::create(
            std::string{name},
            Vec3{(float)x, (float)y, (float)z},
            DimensionType{dimension}
        );
        return static_cast<bool>(sp);
    }

    bool api_sim_is(LeviRsPlayerSel sel)
    {
        Player* p = resolvePlayer(sel);
        return p && p->isSimulatedPlayer();
    }

    void api_sim_list(void* ctx, LeviRsStrSink nameSink)
    {
        auto* level = levelReady();
        if (!level || !nameSink) return;
        // Reuse the existing player-enumeration primitive, filtered to bots.
        // Names only (not full summaries): callers rebuild a SimPlayer handle
        // from the name, then reach the full player surface through it.
        level->forEachPlayer([&](Player& p)
        {
            if (p.isSimulatedPlayer()) nameSink(ctx, p.getRealName());
            return true;
        });
    }

    bool api_sim_do(LeviRsPlayerSel sel, LeviRsStr action, LeviRsStr argsSnbt)
    {
        Player* p = resolvePlayer(sel);
        if (!p || !p->isSimulatedPlayer()) return false; // never puppet a real player
        auto* sim = static_cast<SimulatedPlayer*>(p);

        // Parse args once; "" and "{}" both mean "no parameters".
        CompoundTag args;
        std::string_view raw = argsSnbt;
        if (!raw.empty())
        {
            auto parsed = CompoundTag::fromSnbt(raw);
            if (!parsed) return false; // malformed args: refuse, don't guess
            args = std::move(*parsed);
        }

        std::string_view verb = action;

        if (verb == "despawn")
        {
            sim->simulateDisconnect();
            return true;
        }
        if (verb == "stop")
        {
            sim->simulateStopMoving();
            sim->simulateStopUsingItem();
            sim->simulateStopBuild();
            sim->simulateStopInteracting();
            sim->simulateStopDestroyingBlock();
            return true;
        }
        if (verb == "jump") return sim->simulateJump();
        if (verb == "attack") return sim->simulateAttack();
        if (verb == "interact") return sim->simulateInteract();
        if (verb == "use_item") return sim->simulateUseItem();
        if (verb == "drop") return sim->simulateDropSelectedItem();
        if (verb == "respawn") return sim->simulateRespawn();
        if (verb == "move_to")
        {
            Vec3 pos{
                (float)argD(args, "x", 0),
                (float)argD(args, "y", 0),
                (float)argD(args, "z", 0)};
            sim->simulateMoveToLocation(pos, (float)argD(args, "speed", 1.0), argB(args, "face_target", true));
            return true;
        }
        if (verb == "navigate_to")
        {
            Vec3 pos{
                (float)argD(args, "x", 0),
                (float)argD(args, "y", 0),
                (float)argD(args, "z", 0)};
            static_cast<void>(sim->simulateNavigateToLocation(pos, (float)argD(args, "speed", 4.3)));
            return true;
        }
        if (verb == "look_at")
        {
            // Three overloads exist ((Vec3&), (Vec3&, LookDuration), and a
            // BlockPos one); a bare Vec3 is ambiguous between the first two.
            // Pass LookDuration explicitly to pin the Vec3 overload — Instant
            // matches the no-duration version's "snap to look" semantics.
            sim->simulateLookAt(
                Vec3{
                    (float)argD(args, "x", 0),
                    (float)argD(args, "y", 0),
                    (float)argD(args, "z", 0)},
                ::sim::LookDuration::Instant);
            return true;
        }
        if (verb == "destroy_block") return sim->simulateDestroyBlock(argBlockPos(args), argFace(args));
        if (verb == "destroy_look") return sim->simulateDestroyLookAt((float)argD(args, "hand", 5.5));
        if (verb == "stop_destroy")
        {
            sim->simulateStopDestroyingBlock();
            return true;
        }
        if (verb == "interact_block") return sim->simulateInteract(argBlockPos(args), argFace(args));
        if (verb == "sneak")
        {
            return argB(args, "on", true) ? sim->simulateSneaking() : sim->simulateStopSneaking();
        }
        if (verb == "fly")
        {
            if (argB(args, "on", true))
                sim->simulateFly();
            else
                sim->simulateStopFlying();
            return true;
        }
        if (verb == "chat")
        {
            auto msg = argS(args, "msg");
            if (msg.empty()) return false;
            sim->simulateChat(msg);
            return true;
        }

        return false; // unknown verb
    }
} // namespace levi_rs::bridge
