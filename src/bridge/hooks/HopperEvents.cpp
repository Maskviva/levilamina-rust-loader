/**
 * bridge/hooks/HopperEvents.cpp — "HopperTransferEvent": fires on every
 * hopper slot write (HopperBlockActor::setItem), i.e. whenever items enter
 * or leave a hopper. Payload carries the before/after stack so subscribers
 * compute the delta (increase = items flowed in). No dimension field:
 * setItem has no BlockSource in scope; watchers key on the position they
 * registered. Lifecycle rules in HookEvents.h apply.
 */
#include "bridge/Common.h"
#include "bridge/hooks/HookEvents.h"

#include <atomic>
#include <string>

#include "ll/api/memory/Hook.h"
#include "ll/api/mod/NativeMod.h"

#include "mc/world/Container.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/level/block/actor/HopperBlockActor.h"

namespace levi_rs::bridge
{
    namespace
    {
        HookEventDef& hopperDef(); // fwd — the hook body needs it

        LL_TYPE_INSTANCE_HOOK(
            HopperSetItemHook,
            ll::memory::HookPriority::Normal,
            HopperBlockActor,
            &HopperBlockActor::$setItem,
            void,
            int slot,
            ::ItemStack const& item)
        {
            auto& def = hopperDef();
            if (!def.live())
            {
                origin(slot, item); // installed but idle
                return;
            }

            // GUARD — must run BEFORE any this-> virtual dispatch.
            //
            // Container::setItem has a trivial body, so MSVC's ICF (identical
            // COMDAT folding) very likely folds it with chest/barrel/furnace/
            // dropper's same-shaped setItem onto ONE code address. Hooking that
            // address means this detour is also entered for those actors, where
            // `this` is e.g. ChestBlockActor* — and a virtual call through it
            // would read a vptr at the *Hopper* Container-subobject offset ⇒
            // garbage vptr ⇒ DEP jump ⇒ crash.
            //
            // getType() is MCFOLD (non-virtual), reads BlockActor::mType at the
            // PRIMARY base (offset 0), and is defined once on BlockActor (no
            // container overrides it) — safe for any block actor even if it too
            // is folded, because it means the same thing for all of them.
            // Everything after the guard passes is a genuine HopperBlockActor,
            // so the later non-virtual $getItem / getPosition are safe.
            if (this->getType() != ::BlockActorType::Hopper)
            {
                // Discriminator: if the crash was ICF folding, this fires for
                // chest/furnace/etc. and the guard fixes it. If instead it was
                // a this-adjustor-thunk mismatch, getType() reads garbage and
                // this fires with a nonsense value — and /td counter would then
                // receive NO events, telling us to change hook target instead.
                static std::atomic<bool> logged{false};
                if (!logged.exchange(true))
                {
                    // NativeMod::current() returns shared_ptr<NativeMod> —
                    // hold it by value (operator bool / -> both work).
                    if (auto self = ll::mod::NativeMod::current())
                    {
                        self->getLogger().debug(
                            "HopperTransferEvent guard rejected a non-hopper actor (getType={}). "
                            "Expected with ICF folding; if counters stay empty, the hook target "
                            "needs changing.",
                            static_cast<int>(this->getType()));
                    }
                }
                origin(slot, item);
                return;
            }
            if (slot < 0 || slot >= 5) // a hopper has exactly 5 slots; harden anyway
            {
                origin(slot, item);
                return;
            }

            // Before-state, then let the write happen, then report.
            int oldCount = 0;
            std::string oldName;
            {
                // $getItem: the non-virtual implementation — no vtable walk.
                ItemStack const& prev = this->$getItem(slot);
                oldCount = prev.mCount;
                if (oldCount > 0) oldName = prev.getTypeName();
            }
            origin(slot, item);

            int newCount = item.mCount;
            std::string newName = newCount > 0 ? item.getTypeName() : std::string{};
            BlockPos const& pos = this->getPosition();

            std::string snbt = "{\"eventId\":\"HopperTransferEvent\""
                               ",\"x\":" + std::to_string(pos.x)
                             + ",\"y\":" + std::to_string(pos.y)
                             + ",\"z\":" + std::to_string(pos.z)
                             + ",\"slot\":" + std::to_string(slot)
                             + ",\"item\":\"" + snbtEscape(newName)
                             + "\",\"count\":" + std::to_string(newCount)
                             + ",\"old_item\":\"" + snbtEscape(oldName)
                             + "\",\"old_count\":" + std::to_string(oldCount) + "}";
            dispatchHookEvent(def, snbt);
        }

        HookEventDef gDef{"HopperTransferEvent", [] { HopperSetItemHook::hook(); }};
        HookEventDef& hopperDef() { return gDef; }

        HookEventRegistrar gReg{gDef};
    } // namespace
} // namespace levi_rs::bridge
