/** bridge/LogScheduler.cpp — logging, scheduling, server stats (ABI v1–v2). */
#include "bridge/Api.h"
#include "bridge/Common.h"

#include <chrono>

#include "ll/api/io/LogLevel.h"
#include "ll/api/io/Logger.h"
#include "ll/api/service/GamingStatus.h"
#include "ll/api/thread/ServerThreadExecutor.h"

#include "mc/world/level/Level.h"
#include "mc/world/level/Tick.h"
#include "mc/world/level/TickDeltaTimeManager.h"

#include "RustMod.h"

namespace levi_rs::bridge
{
    void api_log(LeviRsModHandle mod, int32_t level, LeviRsStr msg)
    {
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
    }

    int32_t api_gaming_status() { return static_cast<int32_t>(ll::getGamingStatus()); }

    void api_schedule(LeviRsTaskCb cb, void* user)
    {
        if (!cb) return;
        ll::thread::ServerThreadExecutor::getDefault().execute([cb, user] { cb(user); });
    }

    void api_schedule_after(LeviRsTaskCb cb, void* user, uint64_t delayMs)
    {
        if (!cb) return;
        // Executor::Duration = std::chrono::steady_clock::duration; milliseconds convert implicitly.
        // Fire-and-forget: the returned CancellableCallback is intentionally dropped.
        (void)ll::thread::ServerThreadExecutor::getDefault().executeAfter(
            [cb, user] { cb(user); },
            std::chrono::milliseconds(delayMs)
        );
    }

    uint64_t api_get_current_tick()
    {
        auto* level = levelReady();
        if (!level) return 0;
        return level->getCurrentTick().tickID;
    }

    double api_get_tick_delta_time()
    {
        auto* level = levelReady();
        if (!level) return -1.0;
        return level->getTickDeltaTimeManager()->mTickDeltaTime;
    }

    int32_t api_get_player_count()
    {
        auto* level = levelReady();
        if (!level) return 0;
        return static_cast<int32_t>(level->getActivePlayerCount());
    }

    bool api_get_sim_paused()
    {
        auto* level = levelReady();
        if (!level) return true; // safe default: treat as paused if unknown
        return level->getSimPaused();
    }
} // namespace levi_rs::bridge
