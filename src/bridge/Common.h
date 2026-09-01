/**
 * bridge/Common.h — shared internals for the levi_rs bridge.
 *
 * Everything here enforces the two project-wide disciplines:
 *   1. readiness guard first (levelReady),
 *   2. handles are identifiers, re-resolved on every call (resolvePlayer /
 *      resolveActor / resolveContainer) — never cached native pointers.
 */
#pragma once

#include <charconv>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>

#include "LeviRsAbi.h"

#include "ll/api/io/Logger.h"

namespace ll::io
{
    class Logger;
}

class Actor;
class BlockSource;
class CompoundTag;
class CompoundTagVariant;
class Container;
class ItemStack;
class Level;
class Player;

namespace levi_rs
{
    class RustMod;

    RustMod* asMod(LeviRsModHandle h);

    namespace bridge
    {
        /**
         * Safely extract a double from an NBT variant, bypassing the C++20
         * std::integral constraint that causes ByteTag/IntTag -> double
         * static_casts to silently return 0.
         */
        double nbtToDouble(CompoundTagVariant const& val, double def = 0.0);

        /** Level pointer if the world is usable, nullptr otherwise. */
        Level* levelReady();

        /** BlockSource of a dimension, nullptr if the dimension isn't loaded. */
        BlockSource* blockSourceOf(int32_t dim);

        /**
         * Re-resolve a player selector against the live player list.
         *   kind 0 (name): exact Player::getRealName() match first, then a second
         *                  pass on Actor::getNameTag() (display name).
         *   kind 1 (xuid) / kind 2 (uuid): exact match.
         * Returns nullptr when nobody matches — the caller reports failure.
         */
        Player* resolvePlayer(LeviRsPlayerSel sel);

        /** Re-resolve an ActorUniqueID against the live actor list (players included). */
        Actor* resolveActor(LeviRsActorId id);

        /** Resolve a container reference ("owner + which"). nullptr on any failure. */
        Container* resolveContainer(LeviRsContainerRef ref);

        /** SNBT-escape a string for embedding in hand-built SNBT ("..\"..\\.."). */
        std::string snbtEscape(std::string_view s);

        /**
         * 给手拼 SNBT 用的数值格式化：精确、最短往返、不看 locale。
         *
         * `std::to_string(double)` 是 `sprintf("%f")`，两个毛病都会**损坏
         * SNBT**，而不只是难看：
         *   - 固定六位小数，`1e-7` 变成 "0.000000"，大坐标直接丢掉小数部分；
         *   - 小数点跟随全局 C locale，任何插件调一次 setlocale，浮点全变
         *     "1,5"，对端解析器拒绝整个文档。
         * `to_chars` 两个都没有。整型也走它，保证 emit 出来的数字只有一种拼法。
         *
         * 非有限值降级成 "0"：emit "nan"/"inf" 会让整条 payload 解析失败，
         * 丢的是整个事件而不是一个字段。
         */
        template <typename T>
            requires std::is_arithmetic_v<T>
        std::string snbtNum(T v)
        {
            if constexpr (std::is_floating_point_v<T>)
            {
                if (!std::isfinite(v)) return "0";
            }
            char buf[40];
            auto [end, ec] = std::to_chars(buf, buf + sizeof(buf), v);
            if (ec != std::errc{}) return "0";
            return std::string(buf, end);
        }

        /**
         * `fn`（任意代码地址）是否落在 `moduleBase` 这个模块里。
         *
         * 这是那些「没有 owner 的遗留槽位」——早于 mod-scoped 约定、只收一个
         * 裸函数指针的那些——在卸载时唯一能恢复归属的办法：问操作系统这个指针
         * 属于哪个模块，和正在离开的 mod 的 dylib 基址比。没有它，这类回调会
         * 活过 FreeLibrary，下一次派发跳进未映射内存。
         */
        bool addressOwnedBy(void const* moduleBase, void const* fn);

        /**
         * 事件订阅句柄的 id 源，由动态监听器路径（Events.cpp）和桥接钩子路径
         * （hooks/）共用，两者因此永远不会发出同一个句柄。
         *
         * 两边原来都返回订阅对象的地址。那不安全：退订释放对象后，下一次订阅
         * 完全可能拿到同一地址，于是一个过期句柄会匹配上另一条订阅——**退掉
         * 别人的监听器，还不报错**。id 单调递增、永不复用，过期句柄只会匹配失败。
         *
         * 永不返回 0（ABI 的「失败」值）。和订阅本身一样，仅服务器/客户端线程。
         */
        std::uint64_t nextListenerId();
        LeviRsListenerHandle listenerHandleOf(std::uint64_t id);
        std::uint64_t listenerIdOf(LeviRsListenerHandle handle);

        /**
         * Item (de)serialization across the FFI boundary — items always cross as
         * `ItemStack::save` SNBT. `itemToSnbt` produces it (empty item → "{}");
         * `itemFromSnbt` rebuilds a transient `ItemStack`, returning nullopt on
         * malformed input. Shared so Items/Containers/Players agree byte-for-byte.
         */
        std::string itemToSnbt(ItemStack const& item);
        std::optional<ItemStack> itemFromSnbt(std::string_view snbt);

        /**
         * The player-identity enrichment used by the event path: if `data` embeds a
         * live Player pointer stub, splice a `_player` {name,xuid,uuid} field into a
         * copy and serialize that; otherwise serialize `data` as-is.
         */
        std::string enrichWithPlayer(CompoundTag const& data);

        /** Run a command as server console, discarding output. True on ≥1 success. */
        bool runConsoleCommand(std::string const& cmd);

        /**
         * Vanilla dimension name ("overworld" / "nether" / "the_end") for a
         * dimension id, used to build `/execute in <dim> run …` commands.
         * Out-of-range ids fall back to "overworld".
         */
        /**
         * Vanilla dimension name for id 0/1/2.
         *
         * DEPRECATED for anything that can see a custom dimension: it returns
         * "overworld" for every unrecognised id, so a custom dim silently
         * targets the players' main world. Use dimensionSelector() instead.
         */
        char const* dimensionName(int dim);

        /**
         * `/execute in <this>` selector for ANY registered dimension, vanilla
         * or custom (MoreDimensions ids >= 3).
         *
         * Returns an EMPTY string when `dim` is not a registered dimension —
         * callers must treat that as failure and must NOT fall back to
         * overworld. Silently retargeting an unknown dimension at the main
         * world is how block writes and teleports end up corrupting the
         * survival world.
         */
        std::string dimensionSelector(int32_t dim);

        /** Serialize a player's identity + position line: {name,xuid,uuid,dim,x,y,z}. */
        std::string playerSummarySnbt(Player& p);

        ll::io::Logger& bridgeLogger();
    } // namespace bridge
} // namespace levi_rs

// W12：每一个 `api_*` 入口都是一条 Rust 的 `extern "C"` 帧的下面一层。C++ 异常穿过它是 UB
//（实际表现是 abort，而且没有任何日志）。原来 185 个入口里只有命令注册、表单、KvDb 几处包了 try。
// 用法：函数体第一行 LEVI_RS_API_GUARD_BEGIN，最后一行 LEVI_RS_API_GUARD_END（void 函数用 _VOID）。
// `return {};` 对 bool / 整数 / 指针 / 句柄 / 按值返回的结构体都是「零值」——恰好是每个入口的失败值。
#include "ll/api/utils/ErrorUtils.h"
#define LEVI_RS_API_GUARD_BEGIN try {
#define LEVI_RS_API_GUARD_END                                                                       \
    }                                                                                              \
    catch (...)                                                                                    \
    {                                                                                              \
        ll::error_utils::printCurrentException(::levi_rs::bridge::bridgeLogger());                 \
        return {};                                                                                 \
    }
// 失败值不是「零」的入口用这个：LEVI_RS_SERVICE_OK / LEVI_RS_LANE_OK **都是 0**，
// 对它们 `return {};` 等于把异常报成「调用成功」——那正是这道屏障要防的反面。
// 同理 -1 是 cooldown / chunk 系列约定的失败值。
#define LEVI_RS_API_GUARD_END_VAL(failure)                                                          \
    }                                                                                              \
    catch (...)                                                                                    \
    {                                                                                              \
        ll::error_utils::printCurrentException(::levi_rs::bridge::bridgeLogger());                 \
        return (failure);                                                                          \
    }
// 失败值不是「零」的入口用这个：LEVI_RS_SERVICE_OK / LEVI_RS_LANE_OK **都是 0**，
// 对它们 `return {};` 等于把异常报成「调用成功」——那正是这道屏障要防的反面。
// 同理 -1 / -1.0 是 cooldown、chunk、tick-delta 这几族约定的失败值。
#define LEVI_RS_API_GUARD_END_VAL(failure)                                                          \
    }                                                                                              \
    catch (...)                                                                                    \
    {                                                                                              \
        ll::error_utils::printCurrentException(::levi_rs::bridge::bridgeLogger());                 \
        return (failure);                                                                          \
    }
#define LEVI_RS_API_GUARD_END_VOID                                                                  \
    }                                                                                              \
    catch (...)                                                                                    \
    {                                                                                              \
        ll::error_utils::printCurrentException(::levi_rs::bridge::bridgeLogger());                 \
    }
