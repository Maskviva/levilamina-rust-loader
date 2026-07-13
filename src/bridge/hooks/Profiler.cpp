/**
 * bridge/hooks/Profiler.cpp — per-subsystem MSPT sampling (ROADMAP §3).
 *
 * profile_begin(ticks) arms a sampling window of N level ticks;
 * profile_take() polls for the finished report (SNBT). Five timing detours,
 * all following the HookEvents.h lifecycle rules (installed together on the
 * first profile_begin, never unpatched, fast-path branch while not armed):
 *
 *     level_tick       Level::$tick               (the whole frame)
 *     dimension_tick   Dimension::$tick           (per-dimension slice)
 *     redstone         Dimension::$tickRedstone
 *     chunk_blocks     LevelChunk::tickBlocks     (random/scheduled block ticks)
 *     block_entities   LevelChunk::tickBlockEntities
 *
 * Times are INCLUSIVE wall times (steady_clock): dimension_tick runs inside
 * level_tick; redstone / chunk buckets run inside dimension_tick. Report
 * them side by side, don't sum them. Coexists with TickControl's detour on
 * the same Level::$tick — LeviLamina chains hooks, and each executed tick is
 * measured once, so `/tick warp 5` shows 5× the samples with per-tick
 * numbers still true.
 */
#include "bridge/Api.h"

#include <chrono>
#include <cstdint>
#include <string>

#include "ll/api/memory/Hook.h"

#include "mc/world/level/Level.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/level/dimension/Dimension.h"

namespace levi_rs::bridge
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        struct Bucket
        {
            uint64_t ns = 0;
            uint64_t calls = 0;

            void add(Clock::duration d)
            {
                ns += static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(d).count());
                ++calls;
            }
            void reset() { ns = 0, calls = 0; }
        };

        /** Server-thread only, like all hook state. */
        struct ProfState
        {
            bool hooked = false;
            bool sampling = false;
            bool reportReady = false;
            uint32_t remaining = 0;
            uint32_t window = 0;
            Bucket levelTick, dimTick, redstone, chunkBlocks, blockEntities;
            std::string report;
        };
        ProfState gProf;

        std::string bucketSnbt(char const* name, Bucket const& b)
        {
            return std::string{"\""} + name + "\":{\"us\":" + std::to_string(b.ns / 1000)
                 + ",\"calls\":" + std::to_string(b.calls) + "}";
        }

        void finishWindow()
        {
            auto& st = gProf;
            st.sampling = false;
            st.report = "{\"ticks\":" + std::to_string(st.window)
                      + ",\"buckets\":{" + bucketSnbt("level_tick", st.levelTick)
                      + "," + bucketSnbt("dimension_tick", st.dimTick)
                      + "," + bucketSnbt("redstone", st.redstone)
                      + "," + bucketSnbt("chunk_blocks", st.chunkBlocks)
                      + "," + bucketSnbt("block_entities", st.blockEntities) + "}}";
            st.reportReady = true;
        }

        LL_TYPE_INSTANCE_HOOK(
            ProfLevelTickHook,
            ll::memory::HookPriority::High, // outermost: wraps TickControl's Normal-priority detour
            Level,
            &Level::$tick,
            void)
        {
            auto& st = gProf;
            if (!st.sampling)
            {
                origin();
                return;
            }
            auto t0 = Clock::now();
            origin();
            st.levelTick.add(Clock::now() - t0);
            if (st.remaining > 0 && --st.remaining == 0) finishWindow();
        }

        LL_TYPE_INSTANCE_HOOK(
            ProfDimensionTickHook,
            ll::memory::HookPriority::Normal,
            Dimension,
            &Dimension::$tick,
            void)
        {
            auto& st = gProf;
            if (!st.sampling)
            {
                origin();
                return;
            }
            auto t0 = Clock::now();
            origin();
            st.dimTick.add(Clock::now() - t0);
        }

        LL_TYPE_INSTANCE_HOOK(
            ProfRedstoneTickHook,
            ll::memory::HookPriority::Normal,
            Dimension,
            &Dimension::$tickRedstone,
            void)
        {
            auto& st = gProf;
            if (!st.sampling)
            {
                origin();
                return;
            }
            auto t0 = Clock::now();
            origin();
            st.redstone.add(Clock::now() - t0);
        }

        LL_TYPE_INSTANCE_HOOK(
            ProfChunkBlocksHook,
            ll::memory::HookPriority::Normal,
            LevelChunk,
            &LevelChunk::tickBlocks,
            void,
            ::BlockSource& region)
        {
            auto& st = gProf;
            if (!st.sampling)
            {
                origin(region);
                return;
            }
            auto t0 = Clock::now();
            origin(region);
            st.chunkBlocks.add(Clock::now() - t0);
        }

        LL_TYPE_INSTANCE_HOOK(
            ProfBlockEntitiesHook,
            ll::memory::HookPriority::Normal,
            LevelChunk,
            &LevelChunk::tickBlockEntities,
            void,
            ::BlockSource& region)
        {
            auto& st = gProf;
            if (!st.sampling)
            {
                origin(region);
                return;
            }
            auto t0 = Clock::now();
            origin(region);
            st.blockEntities.add(Clock::now() - t0);
        }

        void ensureProfilerHooked()
        {
            if (gProf.hooked) return;
            ProfLevelTickHook::hook();
            ProfDimensionTickHook::hook();
            ProfRedstoneTickHook::hook();
            ProfChunkBlocksHook::hook();
            ProfBlockEntitiesHook::hook();
            gProf.hooked = true;
        }
    } // namespace

    bool api_profile_begin(uint32_t ticks)
    {
        auto& st = gProf;
        if (ticks == 0 || ticks > 12000) return false; // cap: 10 minutes at 20 TPS
        if (st.sampling) return false;                 // one window at a time
        ensureProfilerHooked();
        st.levelTick.reset();
        st.dimTick.reset();
        st.redstone.reset();
        st.chunkBlocks.reset();
        st.blockEntities.reset();
        st.reportReady = false;
        st.report.clear();
        st.window = ticks;
        st.remaining = ticks;
        st.sampling = true;
        return true;
    }

    bool api_profile_take(void* ctx, LeviRsStrSink sink)
    {
        auto& st = gProf;
        if (!st.reportReady || !sink) return false;
        sink(ctx, st.report);
        st.reportReady = false;
        st.report.clear();
        return true;
    }
} // namespace levi_rs::bridge
