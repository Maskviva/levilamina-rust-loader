/**
 * bridge/SysInfo.cpp — system information & environment (ABI v5 §I).
 * Plain OS calls; thread-safe by contract.
 */
#include "bridge/Api.h"
#include "bridge/Common.h"

#include <string>

#include "ll/api/utils/SystemUtils.h"

namespace levi_rs::bridge
{
    bool api_sys_info_str(int32_t prop, void* ctx, LeviRsStrSink sink)
    {
        if (!sink) return false;
        switch (prop)
        {
        case LEVI_RS_SYS_OS_NAME:
            sink(ctx, ll::sys_utils::getSystemName());
            return true;
        case LEVI_RS_SYS_OS_VERSION:
            sink(ctx, ll::sys_utils::getSystemVersion().to_string());
            return true;
        case LEVI_RS_SYS_LOCALE:
            sink(ctx, ll::sys_utils::getSystemLocaleCode());
            return true;
        case LEVI_RS_SYS_LOCAL_TIME:
            {
                auto t = ll::sys_utils::getLocalTime();
                // std::tm fields + ms, normalized to human values.
                std::string out = "{year:" + std::to_string(t.tm_year + 1900)
                    + ",month:" + std::to_string(t.tm_mon + 1)
                    + ",day:" + std::to_string(t.tm_mday)
                    + ",hour:" + std::to_string(t.tm_hour)
                    + ",minute:" + std::to_string(t.tm_min)
                    + ",second:" + std::to_string(t.tm_sec)
                    + ",ms:" + std::to_string(t.ms) + "}";
                sink(ctx, out);
                return true;
            }
        default:
            return false;
        }
    }

    bool api_sys_get_env(LeviRsStr name, void* ctx, LeviRsStrSink sink)
    {
        if (!sink) return false;
        auto value = ll::sys_utils::getEnvironmentVariable(std::string_view{name});
        sink(ctx, value);
        return true;
    }

    bool api_sys_set_env(LeviRsStr name, LeviRsStr value)
    {
        return ll::sys_utils::setEnvironmentVariable(std::string_view{name}, std::string_view{value});
    }

    bool api_sys_is_wine() { return ll::sys_utils::isWine(); }
} // namespace levi_rs::bridge
