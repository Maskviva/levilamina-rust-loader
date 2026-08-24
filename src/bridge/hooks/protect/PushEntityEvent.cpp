/**
 * bridge/hooks/PushEntityEvent.cpp — "PlayerPushEntityEvent": a player is about
 * to shove another entity by walking into it, and it **can be cancelled**.
 *
 * # Why this is a protection and not a nicety
 *
 * Every other entity protection in this bridge covers a *click*. Pushing needs
 * no click: a visitor who cannot break, place, interact or attack can still
 * walk into a plot and herd the owner's animals out of their pen, shove their
 * boats into the void, or nudge armour stands and item frames' contents around.
 * It is the one griefing method that survives a fully locked-down plot, and it
 * leaves nothing in any log.
 *
 * # Hook point
 *
 * `PushableByEntityUtility::skipPush(Actor& owner, Actor& other)` — a free
 * function in a namespace, so `LL_STATIC_HOOK` rather than the instance macro.
 * It is the engine's own "should this push be skipped" question, which means
 * returning `true` is an outcome every caller already handles correctly. That
 * is worth more than it sounds: refusing inside the push itself would leave the
 * two entities overlapping with the collision unresolved.
 *
 * `skipPush` is not overloaded, unlike `push` (which has both an `Actor&,Vec3`
 * and an `Actor&,Actor&,bool` form and would need a disambiguating cast inside
 * a macro argument). One less thing to get wrong.
 *
 * # Which one is the player
 *
 * Do not assume. Collision resolution runs from both sides — the cow's tick
 * pushes the player and the player's tick pushes the cow — so a player can
 * arrive as `owner` or as `other` depending on which entity is being ticked.
 * Checking only one side gives a protection that works about half the time,
 * which is worse than none because it tests as "mostly working".
 *
 * The permission is evaluated at the **pushed entity's** position: the question
 * is whether this person may disturb things *there*, and a player standing just
 * outside a plot border can push an animal that is inside it.
 *
 * # Player-vs-player pushes are left alone
 *
 * If both sides are players this hook does nothing. Player collision is normal
 * movement, and blocking it asymmetrically (A may push B, B may not push A)
 * produces rubber-banding that looks like lag, not like a protection.
 *
 * # Throttling
 *
 * This runs every tick for every overlapping pair. See DecisionThrottle.h for
 * why the cache is mandatory rather than an optimisation.
 *
 * # Payload
 *
 * ```text
 * {eventId, x, y, z, dim, target, _player:{name,xuid,uuid}}
 * ```
 */
#include "bridge/Common.h"
#include "bridge/hooks/DecisionThrottle.h"
#include "bridge/hooks/HookEvents.h"

#include <string>
#include <unordered_map>

#include "ll/api/memory/Hook.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/util/PushableByEntityUtility.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"

namespace levi_rs::bridge
{
    namespace
    {
        HookEventDef& pushDef(); // fwd

        std::unordered_map<std::string, ThrottledDecision>& pushCache()
        {
            static std::unordered_map<std::string, ThrottledDecision> c;
            return c;
        }

        LL_STATIC_HOOK(
            PlayerPushEntityHook,
            ll::memory::HookPriority::Normal,
            &::PushableByEntityUtility::skipPush,
            bool,
            ::Actor& owner,
            ::Actor& other)
        {
            auto& def = pushDef();
            if (!def.live()) return origin(owner, other);

            // Either side may be the player — see the file header.
            ::Actor* pusher = nullptr;
            ::Actor* target = nullptr;
            if (owner.isPlayer() && !other.isPlayer())
            {
                pusher = &owner;
                target = &other;
            }
            else if (other.isPlayer() && !owner.isPlayer())
            {
                pusher = &other;
                target = &owner;
            }
            else
            {
                // Neither is a player, or both are. Mob-vs-mob is world
                // behaviour; player-vs-player is normal movement.
                return origin(owner, other);
            }

            auto& p = *static_cast<Player*>(pusher);

            std::string key;
            try
            {
                key = p.getXuid();
            }
            catch (...)
            {
                key.clear();
            }
            if (key.empty()) key = p.getRealName(); // offline-mode servers

            auto const& tpos = target->getPosition();
            int const   x    = static_cast<int>(tpos.x);
            int const   y    = static_cast<int>(tpos.y);
            int const   z    = static_cast<int>(tpos.z);
            int const   dim  = static_cast<int>(target->getDimensionId());

            long long const now = throttleNowMs();
            bool            cached = false;
            if (throttleLookup(pushCache(), key, x, y, z, dim, now, cached))
            {
                return cached ? true : origin(owner, other);
            }

            std::string targetName;
            try
            {
                targetName = target->getTypeName();
            }
            catch (...)
            {
                targetName.clear();
            }

            std::string snbt = "{\"eventId\":\"PlayerPushEntityEvent\""
                               ",\"x\":" + std::to_string(x)
                             + ",\"y\":" + std::to_string(y)
                             + ",\"z\":" + std::to_string(z)
                             + ",\"dim\":" + std::to_string(dim)
                             + ",\"target\":\"" + snbtEscape(targetName)
                             + "\",\"_player\":{\"name\":\"" + snbtEscape(p.getRealName())
                             + "\",\"xuid\":\"" + snbtEscape(p.getXuid())
                             + "\",\"uuid\":\"" + snbtEscape(p.getUuid().asString()) + "\"}}";

            bool const cancelled = dispatchHookEventCancellable(def, snbt);
            throttleStore(pushCache(), key, x, y, z, dim, now, cancelled);

            // true == "skip this push", which is exactly what cancelling means.
            return cancelled ? true : origin(owner, other);
        }

        HookEventDef gDef{"PlayerPushEntityEvent", [] { PlayerPushEntityHook::hook(); }};
        HookEventDef& pushDef() { return gDef; }

        HookEventRegistrar gReg{gDef};
    } // namespace
} // namespace levi_rs::bridge
