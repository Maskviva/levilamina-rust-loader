// Bridge functions exposing MoreDimensions API to Rust via FFI.
// All functions run on the server thread (called from Rust safe layer).
#include "more_dimensions/include/dim/CustomDimensionConfig.h"
#include "more_dimensions/include/dim/CustomDimensionManager.h"
#include "more_dimensions/include/dim/DimensionRules.h"
#include "more_dimensions/include/plot/PlotConfine.h"
#include "more_dimensions/include/plot/PlotDimension.h"
#include "more_dimensions/include/plot/PlotLayout.h"
#include "more_dimensions/include/base/SimpleCustomDimension.h"

// snbtEscape：名字和 sNbt 都可能带引号和反斜杠，不转义的话一条畸形数据
// 会把整个 JSON 数组毁掉。
#include "bridge/Common.h"

#include <cstdint>
#include <string>
#include <string_view>

#include "magic_enum.hpp"

#include "ll/api/io/Logger.h"
#include "ll/api/io/LoggerRegistry.h"

#include "mc/util/BidirectionalUnorderedMap.h"
#include "mc/world/level/GeneratorType.h"
#include "mc/world/level/dimension/DimensionType.h"
#include "mc/world/level/dimension/VanillaDimensions.h"

#include "LeviRsAbi.h"
#include "bridge/Api.h"

namespace more_dimensions::bridge
{
    namespace
    {
        ll::io::Logger& logger()
        {
            static auto log = ll::io::LoggerRegistry::getInstance().getOrCreate("more_dimensions");
            return *log;
        }
    } // namespace

    // Add a SimpleCustomDimension with the given name, seed, and generator type.
    //
    // generatorType is ::GeneratorType **verbatim** — 1=Overworld, 2=Flat,
    // 3=Nether, 4=TheEnd, 5=Void. It is not a private numbering of ours, and
    // this comment used to claim it was ("0=Overworld, 1=Nether, 2=TheEnd,
    // 3=Flat, 4=Void"), which is off by one against the engine in two
    // directions at once: it turned Flat into Nether and Void into TheEnd.
    // Callers picking "超平坦" got a nether world.
    //
    // Legacy(0) and Undefined(6) are rejected rather than passed through:
    // neither has an arm in SimpleCustomDimension::createGenerator, so both
    // would land in the default branch and quietly build a void world.
    //
    // Returns the assigned dimension id (>=3 on success, -1 on failure).
    //
    // Idempotent: calling it again with the same name on a later boot returns
    // the same id (CustomDimensionManager reuses the entry persisted in
    // configs/levilamina-rust-loader/dimensions.json). Callers should register
    // unconditionally on every startup rather than probing first.
    int32_t api_md_add_simple_dimension(LeviRsStr name, uint32_t seed, int32_t generatorTypeInt)
    {
        try
        {
            switch (static_cast<GeneratorType>(generatorTypeInt))
            {
            case GeneratorType::Overworld:
            case GeneratorType::Flat:
            case GeneratorType::Nether:
            case GeneratorType::TheEnd:
            case GeneratorType::Void:
                break;
            default:
                logger().error(
                    "add_simple_dimension('{}') 拒绝：generatorType={} 不是受支持的 ::GeneratorType"
                    "（可用值 1=Overworld 2=Flat 3=Nether 4=TheEnd 5=Void）。"
                    "这个维度**没有**被创建 —— 与其建出一个生成器不对的世界，不如现在失败，"
                    "因为生成器名会被写进 dimensions.json，之后改不回来。",
                    std::string{name},
                    generatorTypeInt
                );
                return -1;
            }
            auto genType = static_cast<GeneratorType>(generatorTypeInt);
            logger().info(
                "add_simple_dimension('{}')：seed={} generatorType={}({})",
                std::string{name},
                seed,
                magic_enum::enum_name(genType),
                generatorTypeInt
            );
            auto id = CustomDimensionManager::getInstance().addDimension<SimpleCustomDimension>(
                std::string{name}, seed, genType
            );
            return id.value();
        }
        catch (std::exception const& e)
        {
            logger().error("add_simple_dimension('{}') 失败: {}", std::string{name}, e.what());
            return -1;
        }
        catch (...)
        {
            logger().error("add_simple_dimension('{}') 失败: 未知异常", std::string{name});
            return -1;
        }
    }

    // Add a PlotDimension: a custom dimension whose chunk generator lays out a
    // plot grid (plots / roads / borders) at generation time.
    //
    // layoutSnbt is a CompoundTag SNBT string, see PlotLayout::fromSnbt. Values
    // are clamped on this side — never trust the caller with indices that feed
    // a fixed-size chunk buffer.
    //
    // Returns the assigned dimension id (>=3) or -1 on failure.
    int32_t api_md_add_plot_dimension(LeviRsStr name, uint32_t seed, LeviRsStr layoutSnbt)
    {
        // 注意：LeviRsStr 就是 std::string_view，.data() **不是**以 \0 结尾的。
        // 之前这里用 printf("%s", name.data()) 会一路读到下一个偶然出现的 \0 ——
        // 日志里那些粘在一起、末尾带乱码方块的行就是这么来的，而且是 UB。
        logger().debug(
            "api_md_add_plot_dimension: name='{}' seed={} layout={}",
            std::string{name},
            seed,
            std::string{layoutSnbt}
        );
        try
        {
            auto layout = PlotLayout::fromSnbt(std::string{layoutSnbt});
            auto id = CustomDimensionManager::getInstance().addDimension<PlotDimension>(
                std::string{name}, seed, layout
            );
            return id.value();
        }
        catch (std::exception const& e)
        {
            logger().error("add_plot_dimension('{}') 失败: {}", std::string{name}, e.what());
            return -1;
        }
        catch (...)
        {
            logger().error("add_plot_dimension('{}') 失败: 未知异常", std::string{name});
            return -1;
        }
    }

    // Resolve a dimension name to its id. Returns -1 if not found.
    //
    // WARNING — why this is not a one-liner:
    //
    //   VanillaDimensions::fromString() returns VanillaDimensions::Undefined()
    //   for unknown names, and Undefined() is *mutated at runtime* by
    //   CustomDimensionManager::addDimension (it's kept one past the highest
    //   assigned custom id). So its numeric value always looks like a
    //   perfectly plausible dimension id, and returning it raw makes callers
    //   believe a dimension exists when it doesn't. That exact bug shipped: a
    //   caller probing for an unregistered plot dimension got back 0 (the
    //   overworld) and attached its whole plot grid to players' survival world.
    //
    //   The obvious guard — round-tripping through toString() — is WORSE: it
    //   crashes the server. The object toString() hands back does not match
    //   MSVC's std::string layout (text bytes land where _Mysize belongs), so
    //   any later consumer memcpy's with a length read out of the text itself.
    //   Observed live: a dimension named "red" produced
    //   memcpy(dst, src, 0x646572) — 'r','e','d' as a size — and killed the
    //   server inside VCRUNTIME140.
    //
    // The resolution: read DimensionMap() directly. That returns a const& to a
    // map living in the server's own memory — nothing is constructed, copied,
    // or handed back across the ABI, so the toString() failure mode does not
    // apply. It is also the map that VanillaDimensions::fromString and every
    // BDS internal consult, which is the whole point: a private mirror can
    // drift, and when it does the caller is told a live dimension does not
    // exist. That drift is what produced the "dimension 3 is not registered"
    // teleport failures.
    //
    // The loader config stays as a fallback only.
    /*
     * 按维度规则的三个入口。实现在 DimensionRules.cpp —— 这里只做 ABI 转接，
     * 不放逻辑，免得两处各判一遍。
     */
    void api_md_set_dimension_rule(int32_t dimension, int32_t rule, bool allow)
    {
        more_dimensions::setDimensionRule(dimension, rule, allow);
    }

    bool api_md_get_dimension_rule(int32_t dimension, int32_t rule, bool* outAllow)
    {
        return more_dimensions::getDimensionRule(dimension, rule, outAllow);
    }

    void api_md_clear_dimension_rules(int32_t dimension)
    {
        more_dimensions::clearDimensionRules(dimension);
    }

    int32_t api_md_get_dimension_id(LeviRsStr name)
    {
        auto const wanted = std::string{name};
        if (wanted.empty()) return -1;

        if (wanted == "overworld") return 0;
        if (wanted == "nether") return 1;
        if (wanted == "the_end") return 2;

        {
            auto const& dimMap = ::VanillaDimensions::DimensionMap();
            auto const hit = dimMap.mRight.find(wanted);
            if (hit != dimMap.mRight.end())
            {
                auto const id = hit->second.value();
                // Undefined() is mutated at runtime to sit one past the highest
                // assigned id, so it is always a plausible-looking number. A
                // name that resolves to it is a name that is not registered.
                if (id >= 0 && id != ::VanillaDimensions::Undefined().value()) return id;
            }
        }

        auto const& list = CustomDimensionConfig::getConfig().dimensionList;
        auto const it = list.find(wanted);
        if (it == list.end()) return -1;

        logger().warn(
            "get_dimension_id('{}'): resolved from the loader config (id {}), not from the engine dimension map. "
            "The two have drifted apart.",
            wanted,
            it->second.dimId
        );
        return it->second.dimId;
    }

    // Check whether the MoreDimensions feature is available in this loader build.
    bool api_md_is_available() { return true; }
} // namespace more_dimensions::bridge

/*
 * 地皮边界约束的数据入口。
 *
 * 命名空间是 `levi_rs::bridge` 而不是上面那个 `more_dimensions::bridge`：
 * 这三个槽位落在 ABI 的**公共尾部**（放进 md 条件块里会把尾部所有字段的偏移
 * 推走），于是客户端构建的 ApiTable 也会引用它们，需要 ClientStubs.cpp 给桩。
 * 和 `api_player_send_title` 是同一套安排。
 *
 * 这里只做转接，逻辑全在 PlotConfine.cpp —— 两处各判一遍迟早会分叉。
 */
namespace levi_rs::bridge
{
    /**
     * 列出全部已注册的自定义维度。
     *
     * 数据源是 `CustomDimensionConfig` 的 `dimensionList` —— 也就是
     * `dimension_config.json` 的内存镜像。**故意读配置而不是读引擎的维度表**：
     * 配置里的 `dimId` 是引擎当初分配、然后持久化下来的那个号，重启之后仍然
     * 是同一个；引擎表里还混着原版三维度和一个会变的 `Undefined()`。
     *
     * 输出一次一条 JSON 对象，由 Rust 侧拼成数组。分条发是为了让一条畸形的
     * sNbt 只毁掉它自己那一条，而不是整个列表。
     */
    void api_md_list_dimensions(void* ctx, LeviRsStrSink sink)
    {
        if (!sink) return;
        auto const& cfg = more_dimensions::CustomDimensionConfig::getConfig();
        for (auto const& [name, info] : cfg.dimensionList)
        {
            std::string line = "{\"name\":\"" + levi_rs::bridge::snbtEscape(name)
                             + "\",\"dim\":" + std::to_string(info.dimId)
                             + ",\"snbt\":\"" + levi_rs::bridge::snbtEscape(info.sNbt) + "\"}";
            sink(ctx, line);
        }
    }

    void api_md_set_plot_grid(int32_t dimension, int32_t plotSize, int32_t roadWidth)
    {
        more_dimensions::setPlotGrid(dimension, plotSize, roadWidth);
    }

    void api_md_clear_plot_grid(int32_t dimension) { more_dimensions::clearPlotGrid(dimension); }

    void api_md_set_plot_merges(int32_t dimension, int32_t const* entries, int32_t count)
    {
        // 空表是合法输入（「这个世界一处合并都没有」），但 count>0 配空指针是
        // 调用方的 bug，别拿它去做指针算术。
        if (entries == nullptr) count = 0;
        more_dimensions::setPlotMerges(dimension, entries, count);
    }
} // namespace levi_rs::bridge
