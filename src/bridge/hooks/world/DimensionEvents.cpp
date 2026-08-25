/**
 * bridge/hooks/DimensionEvents.cpp — "PlayerChangeDimensionEvent": fires when a
 * player is about to be moved to another dimension.
 *
 * # Why this exists
 *
 * There was no dimension-change event, so the plugin side had to poll: remember
 * the player's dimension on join, re-read it after every teleport, and compare.
 * That misses every transfer the plugin didn't initiate — portals, other
 * plugins' teleports, `/execute in`. Anything keyed on "did the player change
 * world" (per-world inventories, per-world gamemode) silently failed for those.
 *
 * `Level::requestPlayerChangeDimension` is the single funnel every transfer goes
 * through, and `ChangeDimensionRequest` carries both the source and destination
 * dimension, so one hook covers all of them. This is the same entry point
 * LegacyScriptEngine uses for its `onChangeDim`.
 *
 * # Dispatched BEFORE origin
 *
 * Subscribers run while the player is still in the old dimension. That ordering
 * is what makes "save the inventory belonging to the world I'm leaving" work at
 * all — after origin the player is already elsewhere and their inventory may
 * have been swapped by the engine.
 *
 * Hook events are observe-only (see HookEvents.h); this does not cancel the
 * transfer.
 */
#include "bridge/Common.h"
#include "bridge/hooks/HookEvents.h"

#include <string>

#include "ll/api/memory/Hook.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/ChangeDimensionRequest.h"
#include "mc/world/level/Level.h"

namespace levi_rs::bridge
{
    namespace
    {
        HookEventDef& changeDimDef(); // fwd

        LL_TYPE_INSTANCE_HOOK(
            PlayerChangeDimensionHook,
            ll::memory::HookPriority::Normal,
            Level,
            &Level::$requestPlayerChangeDimension,
            void,
            ::Player&                   player,
            ::ChangeDimensionRequest&&  changeRequest)
        {
            auto& def = changeDimDef();
            if (!def.live())
            {
                return origin(player, std::move(changeRequest));
            }

            // Read the request BEFORE forwarding: origin() takes it by
            // rvalue reference and is free to gut it.
            int const from = changeRequest.mFromDimensionId->value();
            int const to   = changeRequest.mToDimensionId->value();

            std::string snbt = "{\"eventId\":\"PlayerChangeDimensionEvent\""
                               ",\"from\":" + snbtNum(from)
                             + ",\"to\":" + snbtNum(to)
                             + ",\"_player\":{\"name\":\"" + snbtEscape(player.getRealName())
                             + "\",\"xuid\":\"" + snbtEscape(player.getXuid())
                             + "\",\"uuid\":\"" + snbtEscape(player.getUuid().asString()) + "\"}}";
            dispatchHookEvent(def, snbt); // BEFORE origin — see file header

            return origin(player, std::move(changeRequest));
        }

        HookEventDef gDef{"PlayerChangeDimensionEvent", [] { PlayerChangeDimensionHook::hook(); }};
        HookEventDef& changeDimDef() { return gDef; }

        HookEventRegistrar gReg{gDef};
    } // namespace
} // namespace levi_rs::bridge
