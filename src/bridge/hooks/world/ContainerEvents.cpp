/**
 * bridge/hooks/ContainerEvents.cpp — "PlayerOpenContainerEvent": fires when a
 * player is about to open a chest / furnace / hopper / … and **can cancel it**.
 *
 * # Why this matters
 *
 * The permission model has an `open_container` action, but until now nothing
 * fed it: the loader had no container hook, so a visitor could walk onto
 * someone's plot and empty their chests as long as they never broke a block.
 * That was the single biggest hole in the whole security model.
 *
 * # Cancelling
 *
 * `VanillaServerGameplayEventListener::onEvent` returns `EventResult`, and
 * `StopProcessing` aborts the open. Hook events in this bridge are otherwise
 * observe-only (HookEvents.h), so this file carries its own tiny cancellation
 * channel: the dispatched payload is handed to subscribers, and if any of them
 * writes back `{"cancelled":true}` the open is refused.
 *
 * That is why this uses `dispatchHookEventCancellable` rather than the plain
 * `dispatchHookEvent` — the latter's write-back sink is a no-op by design.
 *
 * Hook point taken from LegacyScriptEngine's `onOpenContainer`.
 */
#include "bridge/Common.h"
#include "bridge/hooks/HookEvents.h"

#include <string>

#include "ll/api/memory/Hook.h"

#include "mc/deps/ecs/WeakEntityRef.h"
#include "mc/server/module/VanillaServerGameplayEventListener.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/events/EventResult.h"
#include "mc/world/events/PlayerOpenContainerEvent.h"
#include "mc/world/level/BlockPos.h"

namespace levi_rs::bridge
{
    namespace
    {
        HookEventDef& openContainerDef(); // fwd

        LL_TYPE_INSTANCE_HOOK(
            PlayerOpenContainerHook,
            ll::memory::HookPriority::Normal,
            VanillaServerGameplayEventListener,
            &VanillaServerGameplayEventListener::$onEvent,
            ::EventResult,
            ::PlayerOpenContainerEvent const& ev)
        {
            auto& def = openContainerDef();
            if (!def.live())
            {
                return origin(ev);
            }

            // mPlayer is a WeakEntityRef; it may already be dead by the time we
            // look, so tryUnwrap and bail out quietly if so.
            Actor* actor = nullptr;
            auto opt = ev.mPlayer->tryUnwrap<Actor>();
            actor = opt ? &*opt : nullptr;
            if (!actor || !actor->isType(::ActorType::Player))
            {
                return origin(ev);
            }
            auto& p = *static_cast<Player*>(actor);

            auto const& pos = ev.mBlockPos.get();
            std::string snbt = "{\"eventId\":\"PlayerOpenContainerEvent\""
                ",\"x\":" + snbtNum(pos.x)
                + ",\"y\":" + snbtNum(pos.y)
                + ",\"z\":" + snbtNum(pos.z)
                + ",\"dim\":" + snbtNum(static_cast<int>(actor->getDimensionId()))
                + ",\"containerType\":" + snbtNum(static_cast<int>(ev.mContainerType))
                + ",\"_player\":{\"name\":\"" + snbtEscape(p.getRealName())
                + "\",\"xuid\":\"" + snbtEscape(p.getXuid())
                + "\",\"uuid\":\"" + snbtEscape(p.getUuid().asString()) + "\"}}";

            if (dispatchHookEventCancellable(def, snbt))
            {
                return ::EventResult::StopProcessing;
            }
            return origin(ev);
        }

        HookEventDef gDef{"PlayerOpenContainerEvent", [] { PlayerOpenContainerHook::hook(); }};
        HookEventDef& openContainerDef() { return gDef; }

        HookEventRegistrar gReg{gDef};
    } // namespace
} // namespace levi_rs::bridge
