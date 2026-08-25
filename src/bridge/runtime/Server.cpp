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
        // 原生。读取侧本来就是 level->getTime()，写入侧却绕一圈命令 ——
        // 同一个属性两条路，失败方式还不一样。
        auto* level = levelReady();
        if (!level) return false;
        level->setTime(static_cast<int>(t));
        return true;
    }

    bool api_set_weather(int32_t weather)
    {
        auto* level = levelReady();
        if (!level) return false;
        // updateWeather(雨强度, 雨持续, 雷强度, 雷持续)。持续时间给 0 表示由
        // 引擎自己按默认规则续 —— 和 /weather 不带秒数时的行为一致。
        switch (weather)
        {
        case 1: level->updateWeather(1.0f, 0, 0.0f, 0); return true;
        case 2: level->updateWeather(1.0f, 0, 1.0f, 0); return true;
        case 0: level->updateWeather(0.0f, 0, 0.0f, 0); return true;
        default: return false;
        }
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
        auto* level = levelReady();
        if (!level) return false;
        if (d < 0 || d > 3) return false;
        level->setDifficulty(static_cast<::SharedTypes::Legacy::Difficulty>(d));
        return true;
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
            out = "{type:\"int\",value:" + snbtNum(rule.getInt()) + "}";
            break;
        case GameRule::Type::Float:
            {
                // No getFloat() accessor in this LL version; read the public variant.
                auto const& var = rule.mValue.get();
                float f = std::holds_alternative<float>(var) ? std::get<float>(var) : 0.0f;
                out = "{type:\"float\",value:" + snbtNum(f) + "f}";
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
        auto* level = levelReady();
        if (!level) return false;
        // 这一条**故意保留命令路径**。
        //
        // GameRules 只暴露了 getBool/getInt/getFloat 和 nameToGameRuleIndex，
        // 写入侧公开的只有带下划线的 _setGameRule —— 那是内部接口，签名跨版本
        // 不稳，而且绕过它会漏掉 GameRulesChangedPacket 的广播（客户端不会知道
        // 规则变了）。`/gamerule` 会把这些都做对。
        //
        // 先用 nameToGameRuleIndex 验一次名字，这样至少「规则名拼错」能和
        // 「命令执行失败」区分开 —— 那正是命令路径最难查的地方。
        std::string const rule{name};
        if (!level->getGameRules().hasRule(level->getGameRules().nameToGameRuleIndex(rule)))
        {
            return false;
        }
        return runConsoleCommand("gamerule " + rule + " " + std::string{value});
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
            sink(ctx, snbtNum(SharedConstants::NetworkProtocolVersion()));
            return true;
        default:
            return false;
        }
    }
} // namespace levi_rs::bridge
