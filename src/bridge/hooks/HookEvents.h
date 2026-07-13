/**
 * bridge/hooks/HookEvents.h — the registry behind "bridge-hook events":
 * synthetic event ids backed by native detours, subscribed through the
 * ordinary subscribe_event ABI (matched by name, like the command events).
 *
 * Module layout (one concern per TU, self-registering — adding a hook event
 * never touches this header, Events.cpp, or any table):
 *
 *     hooks/HookEvents.h/.cpp   registry + dispatch + ABI plumbing
 *     hooks/TickControl.cpp     Level::tick detour + tick_freeze/step/warp
 *     hooks/HopperEvents.cpp    "HopperTransferEvent"
 *     hooks/DestroyEvents.cpp   "PlayerStartDestroyBlockEvent"
 *     hooks/Profiler.cpp        Level/Dimension/LevelChunk timing detours
 *
 * Shared lifecycle rules (every hook file follows them):
 *  - detours install lazily (first subscriber / first control call) and are
 *    NEVER unpatched: un-subscription can arrive from inside the hooked
 *    function itself, where unpatching is unsafe. Idle hooks fast-path to
 *    origin behind one subs-empty / not-armed branch.
 *  - everything runs on the server thread (all ABI calls and all hooked
 *    functions do), so the registry needs no locking.
 */
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "LeviRsAbi.h"

namespace levi_rs
{
    class RustMod;

    namespace bridge
    {
        struct HookSub
        {
            RustMod* mod;
            LeviRsEventCb cb;
            void* user;
        };

        struct HookEventDef
        {
            std::string_view name;
            /** Installs the native detour; called once, on the first subscriber. */
            void (*install)();
            bool installed = false;
            std::vector<std::unique_ptr<HookSub>> subs;

            /** Hook bodies fast-path on this. */
            bool live() const { return !subs.empty(); }
        };

        /**
         * Self-registration: each hook TU holds its HookEventDef as a static
         * and registers it via a file-scope registrar object. Consumed only
         * at runtime (subscribe), so static-init order across TUs is a
         * non-issue.
         */
        struct HookEventRegistrar
        {
            explicit HookEventRegistrar(HookEventDef& def);
        };

        /**
         * Deliver one SNBT payload to every subscriber of `def`.
         * Snapshot-safe: callbacks may (un)subscribe during dispatch — a
         * self-unsubscribing callback still receives the current event,
         * subscribers added mid-dispatch start with the next one.
         * Hook events are observe-only; the write-back sink is a no-op.
         */
        void dispatchHookEvent(HookEventDef& def, std::string const& snbt);
    } // namespace bridge
} // namespace levi_rs
