/**
 * bridge/hooks/TickControl.cpp — carpet-style world-clock control
 * (tick_freeze / tick_step / tick_warp), backed by ONE detour on
 * Level::$tick. Lifecycle rules in HookEvents.h apply: installed lazily on
 * the first control call, never unpatched (control calls arrive from command
 * handlers executing INSIDE the tick), idle cost = one predictable branch.
 */
#include "bridge/Api.h"

#include <cstdint>

#include "ll/api/memory/Hook.h"

#include "mc/world/level/Level.h"

namespace levi_rs::bridge
{
    // ───────────────────────── 1. tick control ─────────────────────────

    namespace
    {
        /** Server-thread only (control calls and the hook both run there). */
        struct TickState
        {
            bool hooked = false;
            bool frozen = false;
            double warp = 1.0; // ticks per real frame; fractional = slow motion
            double acc = 0.0;  // fractional accumulator for warp
            uint32_t pendingSteps = 0;
        };
        TickState gTick;

        LL_TYPE_INSTANCE_HOOK(
            LevelTickHook,
            ll::memory::HookPriority::Normal,
            Level,
            &Level::$tick,
            void)
        {
            auto& st = gTick;
            if (st.frozen)
            {
                // Frozen: run only explicitly queued step frames.
                uint32_t n = st.pendingSteps;
                st.pendingSteps = 0;
                for (uint32_t i = 0; i < n; ++i) origin();
                return;
            }
            if (st.warp == 1.0)
            {
                origin(); // fast path: hook installed but idle
                return;
            }
            // Warp: accumulate fractional ticks; >1 runs extra frames back to
            // back (speed-up), <1 skips frames (slow motion).
            st.acc += st.warp;
            int n = static_cast<int>(st.acc);
            st.acc -= n;
            for (int i = 0; i < n; ++i) origin();
        }

        void ensureTickHooked()
        {
            if (!gTick.hooked)
            {
                LevelTickHook::hook();
                gTick.hooked = true;
            }
        }
    } // namespace

    bool api_tick_freeze(bool on)
    {
        if (!on && !gTick.hooked) return true; // nothing to undo
        ensureTickHooked();
        gTick.frozen = on;
        if (!on) gTick.pendingSteps = 0;
        return true;
    }

    bool api_tick_step(uint32_t n)
    {
        if (n == 0) return false;
        if (!gTick.hooked || !gTick.frozen) return false; // stepping only makes sense while frozen
        gTick.pendingSteps += n;
        return true;
    }

    bool api_tick_warp(double factor)
    {
        if (!(factor > 0.0) || factor > 100.0) return false; // rejects NaN too
        if (factor == 1.0 && !gTick.hooked) return true;     // nothing to undo
        ensureTickHooked();
        gTick.warp = factor;
        if (factor == 1.0) gTick.acc = 0.0;
        return true;
    }
} // namespace levi_rs::bridge
