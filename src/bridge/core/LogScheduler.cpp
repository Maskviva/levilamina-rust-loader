/** bridge/LogScheduler.cpp — logging, scheduling, game stats (ABI v1–v2). */
#include "bridge/Api.h"
#include "bridge/Common.h"

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "ll/api/io/LogLevel.h"
#include "ll/api/io/Logger.h"
#include "ll/api/service/GamingStatus.h"

// Server build schedules onto the server thread; client build schedules onto
// the client thread. Both executors have the same interface (execute /
// executeAfter), inherited from ll::coro::Executor.
#ifdef LEVI_RS_TARGET_CLIENT
#include "ll/api/thread/ClientThreadExecutor.h"
#define LEVI_RS_THREAD_EXEC ll::thread::ClientThreadExecutor
#else
#include "ll/api/thread/ServerThreadExecutor.h"
#define LEVI_RS_THREAD_EXEC ll::thread::ServerThreadExecutor
#endif

#include "mc/world/level/Level.h"
#include "mc/world/level/Tick.h"
#include "mc/world/level/TickDeltaTimeManager.h"

#include "RustMod.h"

namespace levi_rs::bridge
{
    void api_log(LeviRsModHandle mod, int32_t level, LeviRsStr msg)
    {
        LEVI_RS_API_GUARD_BEGIN
            if (!mod) return;
            auto& logger = asMod(mod)->getLogger();
            switch (static_cast<ll::io::LogLevel>(level))
            {
            case ll::io::LogLevel::Fatal:
                logger.fatal("{}", msg);
                break;
            case ll::io::LogLevel::Error:
                logger.error("{}", msg);
                break;
            case ll::io::LogLevel::Warn:
                logger.warn("{}", msg);
                break;
            case ll::io::LogLevel::Debug:
                logger.debug("{}", msg);
                break;
            case ll::io::LogLevel::Trace:
                logger.trace("{}", msg);
                break;
            case ll::io::LogLevel::Off:
                break;
            case ll::io::LogLevel::Info:
            default:
                logger.info("{}", msg);
                break;
            }
        LEVI_RS_API_GUARD_END_VOID
    }

    int32_t api_gaming_status()
    {
        LEVI_RS_API_GUARD_BEGIN
            return static_cast<int32_t>(ll::getGamingStatus());
        LEVI_RS_API_GUARD_END
    }

    /* ───────────────────── mod-scoped task table ─────────────────────
     * Why this exists: a scheduled task is a raw function pointer into a mod's
     * dylib. If the mod unloads before the task runs, firing it jumps into
     * freed memory. The legacy `schedule` / `schedule_after` slots cannot fix
     * this — they never learn who scheduled the task — so the mod-aware slots
     * below register every task in a loader-owned table first.
     *
     * The discipline mirrors bridge/Forms.cpp exactly: the executor closure
     * captures only a weak_ptr<RustMod> plus an integer ticket, never the
     * callback itself. At fire time we take the ticket out of the table; if
     * it is gone (unload cleared it) or the mod is gone/disabled, we return
     * without touching the dylib.
     *
     * Deliberately NOT holding the CancellableCallback returned by
     * executeAfter: dropping the last reference to one from inside its own
     * invocation destroys the std::function that is currently running. Letting
     * the timer expire into a dead ticket costs one no-op wakeup and has no
     * such hazard. */
    namespace
    {
        struct PendingTask
        {
            RustMod* mod = nullptr; // identity only; never dereferenced blind
            LeviRsTaskCb cb = nullptr;
            void* user = nullptr;
        };

        std::mutex gTaskMutex;
        std::unordered_map<uint64_t, PendingTask> gPendingTasks;
        uint64_t gNextTaskId = 1;

        uint64_t registerTask(RustMod* mod, LeviRsTaskCb cb, void* user)
        {
            std::lock_guard lock(gTaskMutex);
            uint64_t id = gNextTaskId++;
            gPendingTasks[id] = PendingTask{mod, cb, user};
            return id;
        }

        /** Take the ticket and fire exactly once, or drop it silently. Runs on
         *  the server/client thread. The lock is released before calling into
         *  Rust: mod code may re-enter schedule_* from inside a task. */
        void runTask(std::weak_ptr<RustMod> const& weakMod, uint64_t id)
        {
            PendingTask task;
            {
                std::lock_guard lock(gTaskMutex);
                auto it = gPendingTasks.find(id);
                if (it == gPendingTasks.end()) return; // cleared at unload, or cancelled
                task = it->second;
                gPendingTasks.erase(it);
            }
            auto mod = weakMod.lock();
            if (!mod || mod.get() != task.mod) return; // mod gone; dylib may be unmapped
            if (!mod->isEnabled()) return; // muted while disabled
            if (task.cb) task.cb(task.user);
        }

        /** Shared body for both scheduling entry points. */
        uint64_t submit(LeviRsModHandle modHandle, LeviRsTaskCb cb, void* user, bool delayed, uint64_t delayMs)
        {
            if (!cb || !modHandle) return 0;
            auto* raw = asMod(modHandle);
            if (!raw) return 0;

            std::weak_ptr<RustMod> weakMod;
            try
            {
                weakMod = raw->shared_from_this();
            }
            catch (...)
            {
                return 0; // not owned by a shared_ptr yet — refuse rather than risk it
            }

            uint64_t id = registerTask(raw, cb, user);
            auto fire = [weakMod, id] { runTask(weakMod, id); };

            if (delayed)
            {
                // Executor::Duration = steady_clock::duration; milliseconds convert implicitly.
                (void)LEVI_RS_THREAD_EXEC::getDefault().executeAfter(fire, std::chrono::milliseconds(delayMs));
            }
            else
            {
                LEVI_RS_THREAD_EXEC::getDefault().execute(fire);
            }
            return id;
        }
    } // namespace

    void api_schedule(LeviRsTaskCb cb, void* user)
    {
        LEVI_RS_API_GUARD_BEGIN
            // Legacy, owner-less slot: kept for ABI compatibility with mods built
            // before schedule_for existed. Unavoidably unsafe across unload — such
            // a mod must not be marked reload_safe.
            if (!cb) return;
            LEVI_RS_THREAD_EXEC::getDefault().execute([cb, user] { cb(user); });
        LEVI_RS_API_GUARD_END_VOID
    }

    void api_schedule_after(LeviRsTaskCb cb, void* user, uint64_t delayMs)
    {
        LEVI_RS_API_GUARD_BEGIN
            if (!cb) return;
            // Fire-and-forget: the returned CancellableCallback is intentionally dropped.
            (void)LEVI_RS_THREAD_EXEC::getDefault().executeAfter(
                [cb, user] { cb(user); },
                std::chrono::milliseconds(delayMs)
            );
        LEVI_RS_API_GUARD_END_VOID
    }

    uint64_t api_schedule_for(LeviRsModHandle mod, LeviRsTaskCb cb, void* user)
    {
        LEVI_RS_API_GUARD_BEGIN
            return submit(mod, cb, user, false, 0);
        LEVI_RS_API_GUARD_END
    }

    uint64_t api_schedule_after_for(LeviRsModHandle mod, LeviRsTaskCb cb, void* user, uint64_t delayMs)
    {
        LEVI_RS_API_GUARD_BEGIN
            return submit(mod, cb, user, true, delayMs);
        LEVI_RS_API_GUARD_END
    }

    bool api_schedule_cancel(LeviRsModHandle mod, uint64_t taskId)
    {
        LEVI_RS_API_GUARD_BEGIN
            if (!mod || taskId == 0) return false;
            auto* raw = asMod(mod);
            std::lock_guard lock(gTaskMutex);
            auto it = gPendingTasks.find(taskId);
            // Scoped to the caller: one mod must not be able to cancel another's work.
            if (it == gPendingTasks.end() || it->second.mod != raw) return false;
            gPendingTasks.erase(it);
            return true;
        LEVI_RS_API_GUARD_END
    }

    uint32_t api_schedule_pending_count(LeviRsModHandle mod)
    {
        LEVI_RS_API_GUARD_BEGIN
            if (!mod) return 0;
            auto* raw = asMod(mod);
            std::lock_guard lock(gTaskMutex);
            uint32_t n = 0;
            for (auto const& [id, task] : gPendingTasks)
            {
                if (task.mod == raw) ++n;
            }
            return n;
        LEVI_RS_API_GUARD_END
    }

    void schedulerOnRustModGone(RustMod* mod)
    {
        // Any task still in the table would call into the dylib we are about
        // to unmap. Drop them all. The `user` payloads leak: only the mod's
        // own code could free them, and that code is going away.
        size_t dropped = 0;
        {
            std::lock_guard lock(gTaskMutex);
            for (auto it = gPendingTasks.begin(); it != gPendingTasks.end();)
            {
                if (it->second.mod == mod)
                {
                    it = gPendingTasks.erase(it);
                    ++dropped;
                }
                else
                {
                    ++it;
                }
            }
        }
        if (dropped > 0)
        {
            bridgeLogger().warn(
                "[scheduler] '{}' 卸载时仍有 {} 个待执行任务被丢弃 —— "
                "该 mod 应在 on_disable/on_unload 里自己取消定时器（schedule_cancel）",
                mod ? mod->getName() : std::string{"?"},
                dropped
            );
        }
    }

    uint64_t api_get_current_tick()
    {
        LEVI_RS_API_GUARD_BEGIN
            auto* level = levelReady();
            if (!level) return 0;
            return level->getCurrentTick().tickID;
        LEVI_RS_API_GUARD_END
    }

    double api_get_tick_delta_time()
    {
        LEVI_RS_API_GUARD_BEGIN
            auto* level = levelReady();
            if (!level) return -1.0;
            return level->getTickDeltaTimeManager()->mTickDeltaTime;
        LEVI_RS_API_GUARD_END_VAL(-1.0)
    }

    int32_t api_get_player_count()
    {
        LEVI_RS_API_GUARD_BEGIN
            auto* level = levelReady();
            if (!level) return 0;
            return static_cast<int32_t>(level->getActivePlayerCount());
        LEVI_RS_API_GUARD_END
    }

    bool api_get_sim_paused()
    {
        LEVI_RS_API_GUARD_BEGIN
            auto* level = levelReady();
            if (!level) return true; // safe default: treat as paused if unknown
            return level->getSimPaused();
        LEVI_RS_API_GUARD_END
    }
} // namespace levi_rs::bridge
