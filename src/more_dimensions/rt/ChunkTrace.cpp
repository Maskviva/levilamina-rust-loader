#include "more_dimensions/include/dim/ChunkTrace.h"
#include "more_dimensions/include/base/NativeDimensions.h"

#include <atomic>
#include <cstdlib>
#include <mutex>
#include <map>
#include <set>
#include <string>
#include <tuple>

#include "ll/api/io/Logger.h"
#include "ll/api/io/LoggerRegistry.h"
#include "ll/api/memory/Hook.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/network/packet/DimensionDataPacket.h"
#include "mc/network/packet/LevelChunkPacket.h"
#include "mc/network/packet/SubChunkPacket.h"
#include "mc/network/packet/SubChunkRequestPacket.h"
#include "mc/platform/Result.h"
#include "mc/server/ChunkPositionAndDimension.h"
#include "mc/server/NetworkChunkPublisher.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/ChunkPos.h"
#include "mc/world/level/chunk/ChunkState.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/level/dimension/Dimension.h"

namespace more_dimensions
{
    namespace
    {
        ll::io::Logger& logger()
        {
            static auto log = ll::io::LoggerRegistry::getInstance().getOrCreate("chunk_trace");
            return *log;
        }

        bool traceEnabled()
        {
            static bool const on = []
            {
                auto const* v = std::getenv("MORE_DIMENSIONS_TRACE_CHUNK");
                return v && v[0] == '1';
            }();
            return on;
        }

        /** 只看这个维度；-1 表示全都看；默认 -2 表示"只看自定义维度(>=3)"。 */
        int traceDimFilter()
        {
            static int const dim = []
            {
                auto const* v = std::getenv("MORE_DIMENSIONS_TRACE_CHUNK_DIM");
                if (!v) return -2;
                try
                {
                    return std::stoi(v);
                }
                catch (...)
                {
                    return -2;
                }
            }();
            return dim;
        }

        /**
         * tryChangeState 的**失败**分支要不要打。
         *
         * 默认不打。理由是上一份日志实测出来的：一次 17 秒的进服，
         * 这个分支贡献了 24186 行 ERROR，全部集中在开头 2 秒；紧接着整个
         * 服务器（不只是区块流水线，是所有日志源）静默了 13 秒。也就是说
         * 这个追踪器本身把它要观测的东西压垮了 —— 拿它测出来的"区块加载
         * 停在原点附近"是观测效应，不是被观测的现象。
         *
         * 而且这些行本来就不是错误：区块源会挨个探测整条状态机，
         * tryChangeState 返回 false 的意思只是"当前不在这个状态"。
         *
         * MORE_DIMENSIONS_TRACE_CHUNK_FAIL=1 可以把它们打开，但要有心理准备。
         */
        /*
         * 这里原本有一个 bypassSubChunkRequests() 开关和一个挂在
         * LevelChunk::doesClientNeedToRequestSubchunks() 上的 hook，用来让自定义
         * 维度退回"整列下发"的老路径。**已经删掉，因为那个挂载点是错的。**
         *
         * 那个函数在 LevelChunk.h 里被 `#ifdef LL_PLAT_C` 包着 —— 它是**客户端侧**
         * 的查询。符号在 bedrock_server.exe 的导出表里所以能编能链，但服务端组包
         * 的路径根本不调用它，hook 装上去也永远不触发（实测：那行"已绕开"的提示
         * 一次都没打，[levelchunk] 里 需客户端再请求 仍然是 1）。
         *
         * 教训：在这套头文件里挑挂载点，先看有没有 LL_PLAT_C / LL_PLAT_S 包着。
         * 服务端能用的是没有平台宏、或者只被 LL_PLAT_S 包着的那些。
         */

        bool traceFailures()
        {
            static bool const on = []
            {
                auto const* v = std::getenv("MORE_DIMENSIONS_TRACE_CHUNK_FAIL");
                return v && v[0] == '1';
            }();
            return on;
        }

        bool wanted(int dimId)
        {
            int const f = traceDimFilter();
            if (f == -1) return true;
            if (f == -2) return dimId >= 3;
            return dimId == f;
        }

        char const* stateName(ChunkState s)
        {
            switch (s)
            {
            case ChunkState::Unloaded: return "Unloaded";
            case ChunkState::Generating: return "Generating";
            case ChunkState::Generated: return "Generated";
            case ChunkState::StructurePostProcessing: return "StructurePostProcessing";
            case ChunkState::StructurePostProcessed: return "StructurePostProcessed";
            case ChunkState::DecorationPostProcessing: return "DecorationPostProcessing";
            case ChunkState::DecorationPostProcessed: return "DecorationPostProcessed";
            case ChunkState::CheckingForReplacementData: return "CheckingForReplacementData";
            case ChunkState::NeighborAwareUpgradeNeeded: return "NeighborAwareUpgradeNeeded";
            case ChunkState::NeighborAwareUpgrading: return "NeighborAwareUpgrading";
            case ChunkState::NeedsLighting: return "NeedsLighting";
            case ChunkState::Lighting: return "Lighting";
            case ChunkState::LightingFinished: return "LightingFinished";
            case ChunkState::Loaded: return "Loaded";
            case ChunkState::Invalid: return "Invalid";
            default: return "?";
            }
        }

        /**
         * 从 LevelChunk 取维度 id。
         *
         * 用 mDimension 成员而不是 getDimension() —— 后者在 26.20 的 SDK 里标了
         * MCFOLD（被链接器折叠过），拿它的地址不可靠。getDimensionId() 是虚函数，
         * 正常虚调用，没问题。
         */
        int dimIdOf(LevelChunk const& lc)
        {
            try
            {
                // 隐式转换成 Dimension&，和 PlotDimension 里 `auto& level = mLevel;`
                // 的用法一致；TypedStorage 装引用时不保证有 operator->。
                Dimension& dim = lc.mDimension;
                return dim.getDimensionId().value();
            }
            catch (...)
            {
                return -999;
            }
        }

        std::string dimLabel(int dimId)
        {
            auto name = dimensionNameOf(dimId);
            return name.empty() ? std::to_string(dimId) : (name + "(" + std::to_string(dimId) + ")");
        }

        /** 只是为了在日志里能看出"总共动了多少块"，排查时很有用。 */
        std::atomic<uint64_t> gTransitions{0};
        std::atomic<uint64_t> gCreated{0};
        std::atomic<uint64_t> gLoaded{0};
        std::atomic<uint64_t> gSendOk{0};
        std::atomic<uint64_t> gSendFail{0};
        std::atomic<uint64_t> gLevelChunkPkts{0};
        std::atomic<uint64_t> gSubChunkPkts{0};
    } // namespace

    /*
     * 区块被创建 —— 也就是"有人请求了这个坐标"。
     *
     * 这是判断故障在请求侧还是生成侧的分水岭：
     *   远处坐标**没有**这行  -> 根本没人要，问题在 ChunkViewSource / 玩家半径 /
     *                            view-distance，跟生成器无关
     *   有这行但后面卡住      -> 请求到了，卡在状态机里，往下看状态跃迁
     */
    LL_TYPE_INSTANCE_HOOK(
        LevelChunkCtorTraceHook,
        HookPriority::Normal,
        LevelChunk,
        &LevelChunk::$ctor,
        void*,
        ::Dimension& dimension,
        ::ChunkPos const& cp,
        bool readOnly,
        ::SubChunkInitMode initBlocks,
        bool initializeMetaData,
        ::LevelChunkBlockActorStorage::TrackingMode blockActorTrackingMode
    )
    {
        auto* ret =
            origin(dimension, cp, readOnly, initBlocks, initializeMetaData, blockActorTrackingMode);

        int const dimId = dimension.getDimensionId().value();
        if (wanted(dimId))
        {
            auto const n = gCreated.fetch_add(1) + 1;
            logger().info(
                "[create] dim={} chunk=({}, {}) readOnly={} 累计={}",
                dimLabel(dimId), cp.x, cp.z, readOnly ? 1 : 0, n
            );
        }
        return ret;
    }

    /*
     * 每一次状态跃迁。正常一块地会走完：
     *
     *   Unloaded -> Generating -> Generated -> StructurePostProcessing ->
     *   StructurePostProcessed -> DecorationPostProcessing ->
     *   DecorationPostProcessed -> ... -> Loaded
     *
     * 客户端只会收到走到最后的那些。停在哪一步，这里就看得见。
     */
    LL_TYPE_INSTANCE_HOOK(
        LevelChunkChangeStateTraceHook,
        HookPriority::Normal,
        LevelChunk,
        &LevelChunk::changeState,
        void,
        ::ChunkState from,
        ::ChunkState to
    )
    {
        int const dimId = dimIdOf(*this);
        if (wanted(dimId))
        {
            auto const& cp = mPosition.get();
            auto const cur = mLoadState->load();
            auto const n = gTransitions.fetch_add(1) + 1;
            if (to == ChunkState::Loaded) gLoaded.fetch_add(1);
            logger().info(
                "[state ] dim={} chunk=({}, {}) {} -> {}（当前 {}）累计={}",
                dimLabel(dimId), cp.x, cp.z, stateName(from), stateName(to), stateName(cur), n
            );
            if (cur != from)
            {
                logger().warn(
                    "[state!] dim={} chunk=({}, {}) 期望从 {} 跃迁，实际当前是 {} —— 这次跃迁会丢",
                    dimLabel(dimId), cp.x, cp.z, stateName(from), stateName(cur)
                );
            }
        }
        origin(from, to);
    }

    LL_TYPE_INSTANCE_HOOK(
        LevelChunkTryChangeStateTraceHook,
        HookPriority::Normal,
        LevelChunk,
        &LevelChunk::tryChangeState,
        bool,
        ::ChunkState from,
        ::ChunkState to
    )
    {
        int const dimId = dimIdOf(*this);
        auto const cur = mLoadState->load();
        bool const ok = origin(from, to);
        if (wanted(dimId))
        {
            auto const& cp = mPosition.get();
            if (ok)
            {
                logger().info(
                    "[try   ] dim={} chunk=({}, {}) {} -> {} 成功",
                    dimLabel(dimId), cp.x, cp.z, stateName(from), stateName(to)
                );
            }
            else if (traceFailures())
            {
                // 注意级别是 debug 不是 error：探测失败是状态机的正常返回。
                logger().debug(
                    "[try  -] dim={} chunk=({}, {}) {} -> {} 未跃迁（当前状态是 {}）",
                    dimLabel(dimId), cp.x, cp.z, stateName(from), stateName(to), stateName(cur)
                );
            }
        }
        return ok;
    }

    /*
     * ─────────────── 发送侧：Loaded 之后到底有没有出门 ───────────────
     *
     * 前几轮的追踪全部停在 `Loaded`，而 `Loaded` 只代表**服务端**这块地准备好了，
     * 不代表它被塞进过 LevelChunkPacket。这两件事之间还隔着 NetworkChunkPublisher：
     * 它按玩家位置维护一个发送区域，每 tick 从队列里挑几块序列化发出去。
     *
     * 所以「服务端一路 Loaded、客户端一片空白」这个组合，光看上面那些 hook 是
     * 永远分不出下面两种情况的：
     *
     *   有 [send ] 行  -> 包发出去了，问题在客户端（维度定义、高度范围、
     *                     子区块请求），服务端这边可以收工
     *   没有 [send ] 行 -> 根本没往外发，问题在 publisher（区域中心/半径、
     *                     维度 id 对不上、玩家没被认成在这个维度里）
     *
     * 这一对 hook 就是为了把这个岔路口测出来。
     */
    LL_TYPE_INSTANCE_HOOK(
        NetworkChunkPublisherSendTraceHook,
        HookPriority::Normal,
        NetworkChunkPublisher,
        &NetworkChunkPublisher::_sendQueuedChunk,
        bool,
        ::ChunkPositionAndDimension const& queuedChunk,
        ::ClientBlobCache::Server::TransferBuilder* cachedTransfer
    )
    {
        bool const ok = origin(queuedChunk, cachedTransfer);

        int const dimId = queuedChunk.mType->value();
        if (wanted(dimId))
        {
            auto const& cp = queuedChunk.mPos.get();
            if (ok)
            {
                auto const n = gSendOk.fetch_add(1) + 1;
                logger().info(
                    "[send  ] dim={} chunk=({}, {}) 已发往客户端 累计={}",
                    dimLabel(dimId), cp.x, cp.z, n
                );
            }
            else
            {
                // 返回 false 通常是「这块还没准备好，下个 tick 再来」，单次不是错误；
                // 但如果某一块反复出现在这里而始终等不到上面那行，它就是发不出去。
                auto const n = gSendFail.fetch_add(1) + 1;
                logger().info(
                    "[send -] dim={} chunk=({}, {}) 本次没发出（排队中）累计={}",
                    dimLabel(dimId), cp.x, cp.z, n
                );
            }
        }
        return ok;
    }

    /*
     * 发送区域本身。中心点和半径决定了 publisher 愿意发哪些块 ——
     * 半径异常小、或者中心点跟玩家实际位置对不上，都会表现成「远处永远不出现」。
     */
    LL_TYPE_INSTANCE_HOOK(
        NetworkChunkPublisherMoveRegionTraceHook,
        HookPriority::Normal,
        NetworkChunkPublisher,
        &NetworkChunkPublisher::moveRegion,
        void,
        ::BlockPos const& position,
        uint blockRadius,
        ::Vec3 const& direction,
        float minDistance
    )
    {
        // 不用 getChunksSentSinceStart()：那个方法在头文件里被 LL_PLAT_S 包着，
        // 本工程从不定义这个宏，引用它会不会编译得过取决于构建配置。自己数更稳。
        logger().info(
            "[region] 发送区域中心=({}, {}, {}) 半径={}格(≈{}区块) 本次会话已发出={}",
            position.x, position.y, position.z,
            blockRadius, blockRadius / 16,
            gSendOk.load()
        );
        origin(position, blockRadius, direction, minDistance);
    }

    /*
     * ────────── 客户端到底被告知了什么 ──────────
     *
     * DimensionDataPacket 是客户端认识自定义维度的**唯一**渠道：它把整个
     * DimensionDefinitionGroup 序列化过去，客户端由此知道有哪些维度、各自多高、
     * 用哪种生成器、维度 id 是多少。缺了它，或者里面的数值不对，客户端收到这个
     * 维度的区块就只能丢掉 —— 服务端这边一切正常，玩家看到一片空白。
     *
     * 这个包每个玩家登录时只发一次。原本为了「服务端发了 vs 客户端认不认」这条
     * 分界线默认就开着，但现在所有区块追踪（含本 hook）统一收进
     * MORE_DIMENSIONS_TRACE_CHUNK=1 后面，默认关闭，需要排查时才开。
     *
     * 要重点核对的：
     *   - 三个自定义维度在不在列表里（不在 = 根本没告诉客户端）
     *   - id 是不是 1000/1001/1002，有没有 -1（-1 = 回写那步没生效）
     *   - 高度是不是 -64..320，和 DimensionHeight.h 以及 Dimension 构造时传的
     *     那一份完全一致（对不上 = 客户端按错误的高度分配缓冲，子区块全部错位）
     */
    LL_TYPE_INSTANCE_HOOK(
        DimensionDataPacketWriteTraceHook,
        HookPriority::Normal,
        DimensionDataPacket,
        &DimensionDataPacket::$write,
        void,
        ::BinaryStream& stream
    )
    {
        try
        {
            auto const& defs = *mDimensionDefinitionGroup->mDimensionDefinitions;
            logger().info("[dimdata] 正在向客户端发送维度定义表，共 {} 条：", defs.size());
            for (auto const& entry : defs)
            {
                // 注意标量成员（int / 枚举）直接用，没有 .get()：ll::TypedStorage
                // 只有装类类型时才是个带 .get()/operator-> 的包装，装标量时它就是
                // 那个标量本身。mDimensionType 是 struct 所以保持 ->。
                logger().info(
                    "[dimdata]   '{}' id={} 高度={}..{} 生成器={}",
                    entry.first,
                    entry.second.mDimensionType->value(),
                    entry.second.mHeightMinimum,
                    entry.second.mHeightMaximum,
                    static_cast<int>(entry.second.mGeneratorType)
                );
            }
            if (defs.empty())
            {
                logger().error(
                    "[dimdata] 定义表是空的 —— 客户端不会知道任何自定义维度，"
                    "这些维度的区块发过去也会被丢掉"
                );
            }
        }
        catch (...)
        {
            logger().warn("[dimdata] 读取维度定义表失败（不影响发包本身）");
        }
        origin(stream);
    }

    /*
     * ────────── 客户端到底要的是哪些子区块 ──────────
     *
     * 现在手上的两组数据是这样的（主世界是能正常显示的对照组）：
     *
     *   dim=0     客户端请求索引 -4..19，全部成功；每包只要 2~4 个
     *   dim=1000  客户端请求索引 -24..-32，全部越界；每包一律 27 个
     *
     * 而这个维度真正有地形的是索引 -4..4（服务端自己生成时判的就是这一段，
     * 而且全部"在范围内"）。-4..4 和 -24..-32 正好差 28，是个固定偏移，
     * 说明有一侧把维度底部当成了子区块 -28（也就是 y = -448），而不是 -4（y = -64）。
     *
     * 但**光看回包分不清是谁错的**：客户端可能本来就发了 -24..-32，也可能发的是
     * -4..4 而服务端解析成了 -24..-32。这两种情况要修的地方完全不同。
     *
     * SubChunkRequestPacket 里有 mArePositionsAbsolute —— 位置可以是绝对的，
     * 也可以是相对 mCenterPos 的偏移，而且绝对值和偏移是**两个不同的数组**
     * （mSubChunkPos / mSubChunkPosOffsets）。两个维度在这个标志上不一样的话，
     * 那就是答案。
     *
     * 每个维度只打前 6 个请求，够看了。
     */
    LL_TYPE_INSTANCE_HOOK(
        SubChunkRequestReadTraceHook,
        HookPriority::Normal,
        SubChunkRequestPacket,
        &SubChunkRequestPacket::$_read,
        ::Bedrock::Result<void>,
        ::ReadOnlyBinaryStream& stream
    )
    {
        auto result = origin(stream);
        try
        {
            int const dimId = mDimensionType->value();
            static std::mutex mtx;
            static std::map<int, int> shown;
            {
                std::lock_guard lock{mtx};
                if (shown[dimId] >= 6) return result;
                shown[dimId] += 1;
            }

            auto const& absList = mSubChunkPos.get();
            auto const& offList = mSubChunkPosOffsets.get();
            auto const& centre = mCenterPos.get();

            std::string absY;
            for (auto const& p : absList) absY += std::to_string(p.y) + " ";
            std::string offY;
            for (auto const& o : offList) offY += std::to_string(static_cast<int>(o.mY)) + " ";

            logger().info(
                "[req] dim={} 绝对坐标={} 中心=({}, {}, {}) 请求数={} "
                "绝对表{}项(y: {}) 偏移表{}项(y: {})",
                dimLabel(dimId),
                mArePositionsAbsolute ? 1 : 0,
                centre.x, centre.y, centre.z,
                mRequestCount,
                absList.size(), absY.empty() ? std::string{"-"} : absY,
                offList.size(), offY.empty() ? std::string{"-"} : offY
            );
        }
        catch (...)
        {
        }
        return result;
    }

    /*
     * ────────── 越界到底是拿什么判的 ──────────
     *
     * 上一轮量到：子区块回包 100% 是 IndexOutOfBounds。这一轮量到：维度自己的
     * mHeightRange 是 -64..320，和发给客户端的定义**完全一致**。两条都成立的话，
     * 那"越界"就不是两份高度对不上造成的，而是**送进来做判断的那个索引本身**
     * 和维度的编号基准对不上。
     *
     * 典型的对不上方式：客户端按 (y - minY) / 16 算，得到 0..23；服务端期望的是
     * 绝对子区块索引 -4..19。两边都"没错"，但是差了 4。
     *
     * 所以这里直接把做判断的那一刻打出来：送进来的索引是多少、维度的范围是多少、
     * 判成什么。看一眼就知道是不是差了个固定的偏移。
     *
     * 顺带把 isClientSideGenerationEnabled() 也打出来 —— 这是我几轮前怀疑过但
     * 一直没能确认的那个开关，Dimension 上就有现成的查询。
     *
     * 这个函数每个子区块请求都会调，一次进服上万次，所以**同样的 (维度, 索引,
     * 结果) 只打一次**，总共也就十几行。
     */
    LL_TYPE_INSTANCE_HOOK(
        DimensionSubChunkRangeTraceHook,
        HookPriority::Normal,
        Dimension,
        &Dimension::isSubChunkHeightWithinRange,
        bool,
        short const& subChunkHeight
    )
    {
        bool const ok = origin(subChunkHeight);
        try
        {
            int const dimId = getDimensionId().value();
            static std::mutex mtx;
            static std::set<std::tuple<int, int, bool>> seen;
            {
                std::lock_guard lock{mtx};
                if (!seen.insert({dimId, static_cast<int>(subChunkHeight), ok}).second) return ok;
            }
            auto const& range = mHeightRange.get();
            logger().info(
                "[range] dim={} 判定子区块索引 {} -> {}；维度范围 {}..{}（{} 个子区块，"
                "最低 {}），客户端侧生成={}",
                dimLabel(dimId),
                static_cast<int>(subChunkHeight),
                ok ? "在范围内" : "越界",
                static_cast<int>(range.mMin),
                static_cast<int>(range.mMax),
                getHeightInSubchunks(),
                getMinHeight(),
                isClientSideGenerationEnabled() ? 1 : 0
            );
        }
        catch (...)
        {
        }
        return ok;
    }

    /*
     * ────────── 第 1 步的包里到底装了什么 ──────────
     *
     * 前几轮的 `[send  ]` 只证明了「这个包发出去了」，没看过里面。而现在要判断的
     * 恰恰是里面：
     *
     *   mSubChunksCount = 0 且 mClientNeedsToRequestSubchunks = 1
     *       -> 这是"空壳包"，方块数据要等客户端来请求，是第 2 步的活
     *   mSubChunksCount > 0 且 mSerializedChunk 有长度
     *       -> 方块数据就在这个包里，压根不走第 2 步
     *
     * 这两种模式对应完全不同的排查方向，而我到现在都没量过是哪一种。
     * 主世界和地皮世界在这一行上如果不一样，那就是答案。
     *
     * 一块地一行，一次进服大概几百行。
     */
    LL_TYPE_INSTANCE_HOOK(
        LevelChunkPacketWriteTraceHook,
        HookPriority::Normal,
        LevelChunkPacket,
        &LevelChunkPacket::$write,
        void,
        ::BinaryStream& stream
    )
    {
        try
        {
            gLevelChunkPkts.fetch_add(1);
            int const dimId = mDimensionId->value();
            // 同样不按维度过滤：主世界是对照组。
            auto const& cp = mPos.get();
            logger().info(
                "[levelchunk] dim={} chunk=({}, {}) 子区块数={} 需客户端再请求={} "
                "请求上限={} 负载字节={} 缓存={} 缓存条目={}",
                dimLabel(dimId), cp.x, cp.z,
                mSubChunksCount,
                mClientNeedsToRequestSubchunks ? 1 : 0,
                mClientRequestSubChunkLimit,
                mSerializedChunk.get().size(),
                mCacheEnabled ? 1 : 0,
                mCacheMetadata.get().size()
            );
        }
        catch (...)
        {
            logger().warn("[levelchunk] 读取区块包失败（不影响发包本身）");
        }
        origin(stream);
    }

    /*
     * ────────── 子区块回包：客户端要地形，服务端回了什么 ──────────
     *
     * 现代基岩版发区块分两步：
     *
     *   1. LevelChunkPacket 只带"这里有一列区块"和一个 mClientNeedsToRequestSubchunks
     *      标志，**不带方块数据**；客户端据此建一列**空的**区块
     *   2. 客户端再用 SubChunkRequestPacket 逐个索要子区块，服务端用
     *      SubChunkPacket 回，方块数据在这一步才过去
     *
     * 前几轮量到的 `[send  ]` 是第 1 步。第 1 步全部成功，正好解释了现在的现象：
     * 客户端**有**这一列区块（所以羊吃草那种 UpdateBlock 能画上去，因为列存在），
     * 但列里是空的（所以看出去和虚空一个颜色）。第 2 步才是缺的那一环。
     *
     * SubChunkPacket 每一条数据自带一个结果码，引擎已经把失败原因分好类了：
     *
     *   Success(1) / SuccessAllAir(6)  正常
     *   LevelChunkDoesntExist(2)       服务端找不到这一列
     *   WrongDimension(3)              维度对不上 —— 自定义维度最可能踩的
     *   PlayerDoesntExist(4)           玩家索引失效
     *   IndexOutOfBounds(5)            子区块 y 索引超出维度高度范围
     *
     * 所以不用再猜了：跑一次，看回的是哪个码。
     *
     * 每个包打一行（一个包里可能有几十个子区块，按结果码汇总），量不大。
     */
    LL_TYPE_INSTANCE_HOOK(
        SubChunkPacketWriteTraceHook,
        HookPriority::Normal,
        SubChunkPacket,
        &SubChunkPacket::$write,
        void,
        ::BinaryStream& stream
    )
    {
        try
        {
            gSubChunkPkts.fetch_add(1);
            int const dimId = mDimensionType->value();
            // 这里**故意不走 wanted() 过滤**：主世界是唯一能正常显示的维度，
            // 它的结果码就是对照组。同一份日志里看到
            // "dim=0 成功=N" 和 "dim=aaaaaaaa(1000) 维度不对=N" 并排，
            // 结论就不需要再推理了。一个包一行，量不大。
            {
                auto const& data = mSubChunkData.get();
                int cnt[8]{};
                for (auto const& d : data)
                {
                    auto const r = static_cast<int>(d.mResult);
                    cnt[(r >= 0 && r < 8) ? r : 0]++;
                }
                auto const& c = mCenterPos.get();
                logger().info(
                    "[subchunk] dim={} 中心=({}, {}, {}) 共{}条 | "
                    "成功={} 全空气={} 无此区块={} 维度不对={} 无此玩家={} 越界={} 未定义={}",
                    dimLabel(dimId), c.x, c.y, c.z, data.size(),
                    cnt[1], cnt[6], cnt[2], cnt[3], cnt[4], cnt[5], cnt[0]
                );
                if (cnt[2] || cnt[3] || cnt[4] || cnt[5])
                {
                    logger().error(
                        "[subchunk] dim={} 有子区块请求被拒绝 —— 这些方块数据不会到客户端，"
                        "玩家看到的就是空的。看上面那行是哪一类。",
                        dimLabel(dimId)
                    );
                }
            }
        }
        catch (...)
        {
            logger().warn("[subchunk] 读取子区块回包失败（不影响发包本身）");
        }
        origin(stream);
    }

    using ChunkTraceHookReg = ll::memory::HookRegistrar<
        LevelChunkCtorTraceHook,
        LevelChunkChangeStateTraceHook,
        LevelChunkTryChangeStateTraceHook,
        NetworkChunkPublisherSendTraceHook,
        NetworkChunkPublisherMoveRegionTraceHook>;

    // 这个是单独注册的：它不属于「区块追踪」，量极小，默认就该开着。
    using DimensionDataHookReg =
    ll::memory::HookRegistrar<
        DimensionDataPacketWriteTraceHook,
        LevelChunkPacketWriteTraceHook,
        SubChunkPacketWriteTraceHook,
        SubChunkRequestReadTraceHook,
        DimensionSubChunkRangeTraceHook>;

    void registerChunkTraceHooks()
    {
        // 全部区块追踪（含维度定义表、LevelChunkPacket、SubChunkPacket 等对照类
        // 日志）默认关闭，只有设置了 MORE_DIMENSIONS_TRACE_CHUNK=1 才开启。
        if (!traceEnabled()) return;
        DimensionDataHookReg::hook();
        ChunkTraceHookReg::hook();
        logger().warn(
            "区块追踪已开启（MORE_DIMENSIONS_TRACE_CHUNK=1，维度过滤 {}）。"
            "日志量很大，排查完记得关掉。",
            traceDimFilter() == -2
                ? std::string{"仅自定义维度(>=3)"}
                : traceDimFilter() == -1
                ? std::string{"全部"}
                : std::to_string(traceDimFilter())
        );
    }

    void unregisterChunkTraceHooks()
    {
        if (!traceEnabled()) return;
        logger().info(
            "区块包统计：LevelChunkPacket {} 个，SubChunkPacket {} 个",
            gLevelChunkPkts.load(), gSubChunkPkts.load()
        );
        if (gLevelChunkPkts.load() > 0 && gSubChunkPkts.load() == 0)
        {
            logger().error(
                "发了 {} 个 LevelChunkPacket，但一个 SubChunkPacket 都没发过 —— "
                "客户端要么没来请求子区块，要么请求被别的地方吃掉了。"
                "方块数据从来没有离开过服务端。",
                gLevelChunkPkts.load()
            );
        }
        DimensionDataHookReg::unhook();
        ChunkTraceHookReg::unhook();
        logger().info(
            "区块追踪结束：创建 {} 次，状态跃迁 {} 次，到达 Loaded {} 次，"
            "发往客户端 {} 块（另有 {} 次排队未发）",
            gCreated.load(), gTransitions.load(), gLoaded.load(), gSendOk.load(), gSendFail.load()
        );
        if (gLoaded.load() > 0 && gSendOk.load() == 0)
        {
            logger().error(
                "服务端有 {} 块走到了 Loaded，但一块都没通过 NetworkChunkPublisher 发出去 —— "
                "问题在发送侧，不在生成侧。看上面的 [region] 行：中心点和半径是否合理、"
                "维度 id 是否就是这个维度。",
                gLoaded.load()
            );
        }
    }
} // namespace more_dimensions
