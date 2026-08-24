/**
 * bridge/hooks/InteractEntityEvent.cpp — "PlayerInteractEntityEvent": a player
 * is about to right-click an **entity**, and it **can be cancelled**.
 *
 * # Why this hook exists
 *
 * `PlayerUseItemOnEvent` (UseItemOnEvent.cpp) covers "use held item **on a
 * block**". There was no equivalent for entities, and the plugin's guesses
 * (`PlayerInteractEntityEvent`, `PlayerInteractActorEvent`) resolve to nothing
 * on the LL bus. Everything reachable by right-clicking a mob was unprotected:
 *
 *   shearing sheep · dyeing sheep · milking cows · leashing · name tags ·
 *   saddling · opening a horse/llama/donkey inventory · villager trading ·
 *   feeding and breeding · putting a chest on a llama
 *
 * "Shearing isn't blocked" is the visible symptom; the hole is much wider than
 * shears, and it is the same hole for every one of those.
 *
 * # Hook point
 *
 * `Player::interact(Actor&, Vec3 const&)` — the same point LegacyScriptEngine
 * uses for `onPlayerInteractEntity` (`InteractEntityHook` in
 * `lse/events/PlayerEvents.cpp`). It sits above `Actor::interactPreventDefault`,
 * so it also catches interactions handled entirely inside the mob's own
 * component code — which is exactly where shearing lives.
 *
 * # Cancelling
 *
 * `InteractionResult` is two bit flags. LSE returns `{false, true}`: the
 * interaction did not succeed, but the arm still swings. Keeping the swing is
 * deliberate — a refused interaction that also eats the animation reads to the
 * player as a dropped packet, and they click again, and again. Let the arm move
 * and let the "no permission" tip explain why nothing happened.
 *
 * # Payload
 *
 * ```text
 * {eventId, x, y, z, dim, target, targetIsPlayer, item, _player:{name,xuid,uuid}}
 * ```
 *
 * `x/y/z` is the **target entity's** position (that is where the permission
 * question applies — see RideEvent.cpp for the same reasoning), `target` its
 * type name (e.g. `"minecraft:sheep"`), and `item` the held item's type name so
 * the subscriber can split shears / lead / dye / food into separate actions
 * rather than lumping them all under one "interact entity" permission.
 */
#include "bridge/Common.h"
#include "bridge/hooks/HookEvents.h"

#include <string>

#include "ll/api/memory/Hook.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/world/item/ItemStack.h"

namespace levi_rs::bridge
{
    namespace
    {
        HookEventDef& interactEntityDef(); // fwd

        LL_TYPE_INSTANCE_HOOK(
            PlayerInteractEntityHook,
            ll::memory::HookPriority::Normal,
            Player,
            &Player::interact,
            ::InteractionResult,
            ::Actor& actor,
            ::Vec3 const& location)
        {
            auto& def = interactEntityDef();
            if (!def.live())
            {
                return origin(actor, location);
            }

            std::string targetName;
            try
            {
                targetName = actor.getTypeName();
            }
            catch (...)
            {
                targetName.clear();
            }

            std::string itemName;
            try
            {
                ::ItemStack const& held = this->getSelectedItem();
                if (!held.isNull()) itemName = held.getTypeName();
            }
            catch (...)
            {
                itemName.clear();
            }

            auto const& pos = actor.getPosition();
            std::string snbt = "{\"eventId\":\"PlayerInteractEntityEvent\""
                               ",\"x\":" + std::to_string(static_cast<int>(pos.x))
                             + ",\"y\":" + std::to_string(static_cast<int>(pos.y))
                             + ",\"z\":" + std::to_string(static_cast<int>(pos.z))
                             + ",\"dim\":" + std::to_string(static_cast<int>(actor.getDimensionId()))
                             + ",\"targetIsPlayer\":" + (actor.isPlayer() ? "1" : "0")
                             + ",\"target\":\"" + snbtEscape(targetName)
                             + "\",\"item\":\"" + snbtEscape(itemName)
                             + "\",\"_player\":{\"name\":\"" + snbtEscape(this->getRealName())
                             + "\",\"xuid\":\"" + snbtEscape(this->getXuid())
                             + "\",\"uuid\":\"" + snbtEscape(this->getUuid().asString()) + "\"}}";

            if (dispatchHookEventCancellable(def, snbt))
            {
                ::InteractionResult refused{};
                refused.mSuccess = false;
                refused.mSwing   = true; // see "Cancelling" above
                return refused;
            }
            return origin(actor, location);
        }

        HookEventDef gDef{"PlayerInteractEntityEvent", [] { PlayerInteractEntityHook::hook(); }};
        HookEventDef& interactEntityDef() { return gDef; }

        HookEventRegistrar gReg{gDef};
    } // namespace
} // namespace levi_rs::bridge
