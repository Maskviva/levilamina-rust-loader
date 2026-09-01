/**
 * bridge/hooks/UseItemOnEvent.cpp — "PlayerUseItemOnEvent": a player is about
 * to use the held item **on a block**, and it **can be cancelled**.
 *
 * # Why this hook exists
 *
 * `PlayerInteractBlockEvent` is not the funnel people assume it is. Spawn
 * eggs, buckets, flint & steel, ender pearls and item-driven block placement
 * all reach the world through `GameMode::useItemOn`, and a land-protection
 * plugin that only watches interact/place events lets every one of them
 * through. That was a live hole: a visitor standing on someone else's plot
 * could empty a stack of spawn eggs onto it, and nothing in the server log
 * said a word.
 *
 * The dimension-rule layer (DimensionRules.cpp) cannot close it either, and
 * deliberately so: `Spawner::spawnMob` always allows spawns that are neither
 * natural nor from a spawner, because a player placing a mob on purpose in a
 * creative world is normal play. That decision is right for *world* rules and
 * wrong as a *permission* check — permissions need to know whose plot it is,
 * which only the Rust side knows. So this hook reports the event and lets the
 * plugin decide.
 *
 * # Cancelling
 *
 * `useItemOn` returns `InteractionResult`, two bit flags. Returning one with
 * both clear refuses the use without touching anything else: no block placed,
 * no mob spawned, no bucket emptied, and the item is not consumed.
 *
 * # Payload
 *
 * ```text
 * {eventId, x, y, z, dim, face, item, isFirstEvent, _player:{name,xuid,uuid}}
 * ```
 *
 * `x/y/z` are written **flat as integers** rather than nested, matching the
 * other hook events in this directory. That matters: LeviLamina's reflection
 * serialises `BlockPos` as a JSON *array*, and consumers that only understand
 * `{x,y,z}` silently read nothing from it.
 *
 * `isFirstEvent` is passed through untouched because it is genuinely useful
 * downstream: holding right-click on Windows re-fires this call many times per
 * second, and a subscriber that wants to act once per click needs to tell the
 * first one from the repeats.
 */
#include "bridge/Common.h"
#include "bridge/hooks/HookEvents.h"

#include <string>

#include "ll/api/memory/Hook.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/block/Block.h"

namespace levi_rs::bridge
{
    namespace
    {
        HookEventDef& useItemOnDef(); // fwd

        LL_TYPE_INSTANCE_HOOK(
            PlayerUseItemOnHook,
            ll::memory::HookPriority::Normal,
            GameMode,
            &GameMode::$useItemOn,
            ::InteractionResult,
            ::ItemStack& item,
            ::BlockPos const& at,
            uchar face,
            ::Vec3 const& hit,
            ::Block const* targetBlock,
            bool isFirstEvent)
        {
            auto& def = useItemOnDef();
            if (!def.live())
            {
                return origin(item, at, face, hit, targetBlock, isFirstEvent);
            }

            // mPlayer is `TypedStorage<8, 8, Player&>` — and that is **not** a
            // wrapper. `TypedStorageType` has a partial specialisation
            //
            //     requires(is_reference_v<T> || (is_scalar_v<T> && …))
            //     using Type = T;
            //
            // so reference members collapse to the bare reference exactly like
            // scalars do. `.get()` here would be a compile error.
            //
            // This refines the rule that came out of the ChunkTrace.cpp round:
            // it is not "scalars collapse", it is "scalars **and references**
            // collapse; only class-type values stay wrapped".
            Player& p = this->mPlayer;

            std::string itemName;
            itemName = item.getTypeName();

            std::string snbt = "{\"eventId\":\"PlayerUseItemOnEvent\""
                ",\"x\":" + snbtNum(at.x)
                + ",\"y\":" + snbtNum(at.y)
                + ",\"z\":" + snbtNum(at.z)
                + ",\"dim\":" + snbtNum(static_cast<int>(p.getDimensionId()))
                + ",\"face\":" + snbtNum(static_cast<int>(face))
                + ",\"isFirstEvent\":" + (isFirstEvent ? "1" : "0")
                + ",\"item\":\"" + snbtEscape(itemName)
                + "\",\"_player\":{\"name\":\"" + snbtEscape(p.getRealName())
                + "\",\"xuid\":\"" + snbtEscape(p.getXuid())
                + "\",\"uuid\":\"" + snbtEscape(p.getUuid().asString()) + "\"}}";

            if (dispatchHookEventCancellable(def, snbt))
            {
                // Both flags clear: the use did not happen and the client is
                // told not to play the swing animation.
                ::InteractionResult refused{};
                refused.mSuccess = false;
                refused.mSwing = false;
                return refused;
            }
            return origin(item, at, face, hit, targetBlock, isFirstEvent);
        }

        HookEventDef gDef{"PlayerUseItemOnEvent", [] { PlayerUseItemOnHook::hook(); }};
        HookEventDef& useItemOnDef() { return gDef; }

        HookEventRegistrar gReg{gDef};
    } // namespace
} // namespace levi_rs::bridge
