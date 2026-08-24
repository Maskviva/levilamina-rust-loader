/**
 * bridge/Server.cpp — clock, weather, difficulty, seed, game rules, server
 * info (ABI v5 §A clock + §I settings).
 *
 * Reads are direct native calls; version-sensitive writes go through vanilla
 * commands (design decision #3), so they survive BDS bumps untouched.
 */
#include "bridge/Api.h"
#include "bridge/Common.h"

#include <string>
#include <variant>

#include "mc/common/Common.h"
#include "mc/common/SharedConstants.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/LevelSeed64.h"
#include "mc/world/level/storage/GameRule.h"
#include "mc/world/level/storage/GameRuleId.h"
#include "mc/world/level/storage/GameRules.h"

namespace levi_rs::bridge
{
    bool api_get_time(int64_t* out)
    {
        auto* level = levelReady();
        if (!level || !out) return false;
        *out = static_cast<int64_t>(level->getTime());
        return true;
    }

    bool api_set_time(int64_t t)
    {
        if (!levelReady()) return false;
        return runConsoleCommand("time set " + std::to_string(t));
    }

    bool api_set_weather(int32_t weather)
    {
        if (!levelReady()) return false;
        char const* name = weather == 1 ? "rain" : weather == 2 ? "thunder" : "clear";
        return runConsoleCommand(std::string{"weather "} + name);
    }

    bool api_get_difficulty(int32_t* out)
    {
        auto* level = levelReady();
        if (!level || !out) return false;
        *out = static_cast<int32_t>(level->getDifficulty());
        return true;
    }

    bool api_set_difficulty(int32_t d)
    {
        if (!levelReady()) return false;
        if (d < 0 || d > 3) return false;
        return runConsoleCommand("difficulty " + std::to_string(d));
    }

    bool api_get_seed(int64_t* out)
    {
        auto* level = levelReady();
        if (!level || !out) return false;
        *out = static_cast<int64_t>(level->getLevelSeed64().mValue);
        return true;
    }

    bool api_game_rule_get(LeviRsStr name, void* ctx, LeviRsStrSink sink)
    {
        auto* level = levelReady();
        if (!level || !sink) return false;
        auto& rules = level->getGameRules();
        GameRuleId id = rules.nameToGameRuleIndex(std::string{name});
        // NewType<int>: the raw index; out of range = unknown rule.
        int idx = id.mValue;
        auto const& list = rules.mGameRules.get();
        if (idx < 0 || static_cast<size_t>(idx) >= list.size()) return false;
        auto const& rule = list[static_cast<size_t>(idx)];

        std::string out;
        switch (rule.mType)
        {
        case GameRule::Type::Bool:
            out = std::string{"{type:\"bool\",value:"} + (rule.getBool() ? "1b" : "0b") + "}";
            break;
        case GameRule::Type::Int:
            out = "{type:\"int\",value:" + std::to_string(rule.getInt()) + "}";
            break;
        case GameRule::Type::Float:
            {
                // No getFloat() accessor in this LL version; read the public variant.
                auto const& var = rule.mValue.get();
                float f = std::holds_alternative<float>(var) ? std::get<float>(var) : 0.0f;
                out = "{type:\"float\",value:" + std::to_string(f) + "f}";
                break;
            }
        default:
            return false;
        }
        sink(ctx, out);
        return true;
    }

    bool api_game_rule_set(LeviRsStr name, LeviRsStr value)
    {
        if (!levelReady()) return false;
        // /gamerule validates the name and value; invalid input just fails.
        return runConsoleCommand("gamerule " + std::string{name} + " " + std::string{value});
    }

    bool api_server_info_str(int32_t prop, void* ctx, LeviRsStrSink sink)
    {
        if (!sink) return false;
        switch (prop)
        {
        case LEVI_RS_SRV_BDS_VERSION:
            sink(ctx, Common::getGameVersionString());
            return true;
        case LEVI_RS_SRV_PROTOCOL_VERSION:
            sink(ctx, std::to_string(SharedConstants::NetworkProtocolVersion()));
            return true;
        default:
            return false;
        }
    }
} // namespace levi_rs::bridge
