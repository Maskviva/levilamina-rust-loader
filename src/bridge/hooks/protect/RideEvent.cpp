/**
 * bridge/hooks/RideEvent.cpp — "PlayerRideEvent": a player is about to mount a
 * horse / boat / minecart / pig / strider, and it **can be cancelled**.
 *
 * # Why this hook exists
 *
 * The plugin guessed at `PlayerStartRidingEvent` / `PlayerRidingEvent` /
 * `PlayerMountEvent`. None of the three exists on the LL bus. The vanilla
 * `ActorStartRidingEvent` does exist (`mc/world/events/`), but it is an
 * `ActorGameplayEvent` notification: it fires *after* the mount and cannot
 * refuse it.
 *
 * # Hook point: canAddPassenger, not startRiding
 *
 * `Actor::startRiding` looks like the obvious choice and is the wrong one.
 * LegacyScriptEngine hooks `Actor::canAddPassenger` (`ActorRideHook` in
 * `lse/events/EntityEvents.cpp`) and that is the better point, because it is
 * the *vehicle's* veto: the engine asks the vehicle whether it will accept this
 * passenger, and answering "no" is a normal, fully-supported outcome that every
 * caller already handles. Refusing inside `startRiding` instead means lying to
 * a function that has already decided the mount is going to happen.
 *
 * Note the inversion that comes with it: `this` is the **vehicle** and the
 * argument is the **rider**. Getting these backwards gives a protection that
 * checks the boat's permissions instead of the player's, which "works" in
 * testing (both are usually on the same plot) and fails exactly at plot
 * borders.
 *
 * # Position: the vehicle
 *
 * A player standing just outside a plot border can right-click a boat inside
 * it. The permission question is "may this person use that vehicle", answered
 * where the vehicle is — so `x/y/z` is the vehicle's position.
 *
 * # Payload
 *
 * ```text
 * {eventId, x, y, z, dim, vehicle, _player:{name,xuid,uuid}}
 * ```
 *
 * `vehicle` is the entity type name, e.g. `"minecraft:boat"`.
 */
#include "bridge/Common.h"
#include "bridge/hooks/HookEvents.h"

#include <string>

#include "ll/api/memory/Hook.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"

namespace levi_rs::bridge
{
    namespace
    {
        HookEventDef& rideDef(); // fwd

        LL_TYPE_INSTANCE_HOOK(
            PlayerRideHook,
            ll::memory::HookPriority::Normal,
            Actor,
            &Actor::$canAddPassenger,
            bool,
            ::Actor& passenger)
        {
            auto& def = rideDef();
            // Two fast paths, cheapest first: nobody subscribed, or the rider
            // is a mob. Mob mounts (zombie on a chicken, villager in a boat)
            // are not a permission question and some farms produce them every
            // tick.
            if (!def.live() || !passenger.isPlayer())
            {
                return origin(passenger);
            }

            auto& p = *static_cast<Player*>(&passenger);

            std::string vehicleName;
            try
            {
                vehicleName = this->getTypeName();
            }
            catch (...)
            {
                vehicleName.clear();
            }

            auto const& pos = this->getPosition();
            std::string snbt = "{\"eventId\":\"PlayerRideEvent\""
                               ",\"x\":" + std::to_string(static_cast<int>(pos.x))
                             + ",\"y\":" + std::to_string(static_cast<int>(pos.y))
                             + ",\"z\":" + std::to_string(static_cast<int>(pos.z))
                             + ",\"dim\":" + std::to_string(static_cast<int>(this->getDimensionId()))
                             + ",\"vehicle\":\"" + snbtEscape(vehicleName)
                             + "\",\"_player\":{\"name\":\"" + snbtEscape(p.getRealName())
                             + "\",\"xuid\":\"" + snbtEscape(p.getXuid())
                             + "\",\"uuid\":\"" + snbtEscape(p.getUuid().asString()) + "\"}}";

            if (dispatchHookEventCancellable(def, snbt))
            {
                return false; // the vehicle declines this passenger
            }
            return origin(passenger);
        }

        HookEventDef gDef{"PlayerRideEvent", [] { PlayerRideHook::hook(); }};
        HookEventDef& rideDef() { return gDef; }

        HookEventRegistrar gReg{gDef};
    } // namespace
} // namespace levi_rs::bridge
