/**
 * bridge/hooks/PressurePlateEvent.cpp — "PlayerStepOnPressurePlateEvent": a
 * player standing on a pressure plate or in a tripwire is about to trigger it,
 * and it **can be cancelled**.
 *
 * # Why this one is different from every other hook here
 *
 * Every other protection in this bridge hangs off a deliberate player *action*:
 * a click, a swing, a drop. A pressure plate has no action — walking is the
 * action, and the plate fires as a side effect. There is no player event to
 * subscribe to because, from the engine's point of view, the player did not do
 * anything. That is why the plugin's original guesses
 * (`PlayerTogglePressurePlateEvent` and friends) could never have resolved.
 *
 * # Two hook points, and why the first one alone was not enough
 *
 * The first version of this file hooked only `shouldTriggerEntityInside`,
 * copying LegacyScriptEngine's `onStepOnPressurePlate`
 * (`PressurePlateTriggerHook` in `lse/events/BlockEvents.cpp`). On this BDS
 * build that hook installs cleanly and never blocks anything: plates kept
 * working for players who had just been denied. The lesson is worth writing
 * down — *"a known-good plugin hooks it here"* is evidence that the symbol
 * exists, not evidence that it is on the path in your build.
 *
 * `entityInside` is the path. It is the standard `BlockType` callback for "an
 * entity is overlapping this block", the same one that makes cactus hurt and
 * cobweb slow you down, and for these two blocks it is what runs the trigger
 * logic:
 *
 *   `BasePressurePlateBlock::entityInside` → `checkPressed` (all plate types:
 *       stone, wood, every wood variant, both weighted plates — neither
 *       `PressurePlateBlock` nor `WeightedPressurePlateBlock` re-overrides it,
 *       so hooking the base catches all of them)
 *   `TripWireBlock::entityInside`          → `_checkPressed` → trips the hooks
 *
 * It returns void, so cancelling is simply not calling origin: the plate never
 * presses, the tripwire never trips, no redstone is emitted.
 *
 * `shouldTriggerEntityInside` is kept as a cheap early-out for builds where the
 * engine does consult it. Keeping both costs one extra cache lookup and removes
 * the dependency on which one this particular build happens to call.
 *
 * # Throttling
 *
 * Both virtuals run every tick per entity inside the block. See
 * DecisionThrottle.h for why the cache is mandatory rather than an
 * optimisation, and why it is keyed the way it is.
 *
 * # Payload
 *
 * ```text
 * {eventId, x, y, z, dim, kind, _player:{name,xuid,uuid}}
 * ```
 *
 * `kind` is `"pressure_plate"` or `"tripwire"`.
 */
#include "bridge/Common.h"
#include "bridge/hooks/DecisionThrottle.h"
#include "bridge/hooks/HookEvents.h"

#include <string>
#include <unordered_map>

#include "ll/api/memory/Hook.h"

#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/BasePressurePlateBlock.h"
#include "mc/world/level/block/TripWireBlock.h"

namespace levi_rs::bridge
{
    namespace
    {
        HookEventDef& plateDef(); // fwd

        std::unordered_map<std::string, ThrottledDecision>& plateCache()
        {
            static std::unordered_map<std::string, ThrottledDecision> c;
            return c;
        }

        /**
         * True when the trigger must be refused. Dispatches at most once per
         * kDecisionTtlMs per (player, block position).
         */
        bool refuseTrigger(::Actor& entity, ::BlockSource& region, ::BlockPos const& pos, char const* kind)
        {
            auto& def = plateDef();
            if (!def.live() || !entity.isPlayer()) return false;

            auto& p = *static_cast<Player*>(&entity);

            std::string key;
            key = p.getXuid();
            if (key.empty()) key = p.getRealName(); // offline-mode servers

            int const dim = static_cast<int>(region.getDimensionId());
            long long const now = throttleNowMs();

            bool cached = false;
            if (throttleLookup(plateCache(), key, pos.x, pos.y, pos.z, dim, now, cached))
            {
                return cached;
            }

            std::string snbt = "{\"eventId\":\"PlayerStepOnPressurePlateEvent\""
                ",\"x\":" + snbtNum(pos.x)
                + ",\"y\":" + snbtNum(pos.y)
                + ",\"z\":" + snbtNum(pos.z)
                + ",\"dim\":" + snbtNum(dim)
                + ",\"kind\":\"" + kind
                + "\",\"_player\":{\"name\":\"" + snbtEscape(p.getRealName())
                + "\",\"xuid\":\"" + snbtEscape(p.getXuid())
                + "\",\"uuid\":\"" + snbtEscape(p.getUuid().asString()) + "\"}}";

            bool const cancelled = dispatchHookEventCancellable(def, snbt);
            throttleStore(plateCache(), key, pos.x, pos.y, pos.z, dim, now, cancelled);
            return cancelled;
        }

        // ── The path that actually runs the trigger ──────────────────────────

        LL_TYPE_INSTANCE_HOOK(
            PressurePlateInsideHook,
            ll::memory::HookPriority::Normal,
            BasePressurePlateBlock,
            &BasePressurePlateBlock::$entityInside,
            void,
            ::BlockSource& region,
            ::BlockPos const& pos,
            ::Actor& entity)
        {
            if (refuseTrigger(entity, region, pos, "pressure_plate")) return;
            origin(region, pos, entity);
        }

        LL_TYPE_INSTANCE_HOOK(
            TripWireInsideHook,
            ll::memory::HookPriority::Normal,
            TripWireBlock,
            &TripWireBlock::$entityInside,
            void,
            ::BlockSource& region,
            ::BlockPos const& pos,
            ::Actor& entity)
        {
            if (refuseTrigger(entity, region, pos, "tripwire")) return;
            origin(region, pos, entity);
        }

        // ── The cheap early-out, where the engine consults it ────────────────

        LL_TYPE_INSTANCE_HOOK(
            PressurePlateShouldTriggerHook,
            ll::memory::HookPriority::Normal,
            BasePressurePlateBlock,
            &BasePressurePlateBlock::$shouldTriggerEntityInside,
            bool,
            ::BlockSource& region,
            ::BlockPos const& pos,
            ::Actor& entity)
        {
            if (refuseTrigger(entity, region, pos, "pressure_plate")) return false;
            return origin(region, pos, entity);
        }

        LL_TYPE_INSTANCE_HOOK(
            TripWireShouldTriggerHook,
            ll::memory::HookPriority::Normal,
            TripWireBlock,
            &TripWireBlock::$shouldTriggerEntityInside,
            bool,
            ::BlockSource& region,
            ::BlockPos const& pos,
            ::Actor& entity)
        {
            if (refuseTrigger(entity, region, pos, "tripwire")) return false;
            return origin(region, pos, entity);
        }

        HookEventDef gDef{
            "PlayerStepOnPressurePlateEvent",
            []
            {
                PressurePlateInsideHook::hook();
                TripWireInsideHook::hook();
                PressurePlateShouldTriggerHook::hook();
                TripWireShouldTriggerHook::hook();
            }
        };
        HookEventDef& plateDef() { return gDef; }

        HookEventRegistrar gReg{gDef};
    } // namespace
} // namespace levi_rs::bridge
