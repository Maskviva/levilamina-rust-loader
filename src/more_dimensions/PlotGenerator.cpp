#include "more_dimensions/CompleteBaseTypes.h"
#include "more_dimensions/PlotGenerator.h"
#include "more_dimensions/Macros.h"

#include <cstdlib>
#include <stdexcept>
#include <string>

#include "ll/api/io/Logger.h"
#include "ll/api/io/LoggerRegistry.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/ChunkPos.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/biome/registry/BiomeRegistry.h"
#include "mc/world/level/biome/source/FixedBiomeSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockVolume.h"
#include "mc/world/level/block/VanillaBlockTypeIds.h"
#include "mc/world/level/block/registry/BlockTypeRegistry.h"
#include "mc/world/level/chunk/ChunkState.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/level/dimension/Dimension.h"

namespace more_dimensions
{
    namespace
    {
        constexpr int kChunkWidth  = PlotLayout::kChunkWidth;
        constexpr int kTotalHeight = PlotLayout::kTotalHeight;
        constexpr int kColumns     = kChunkWidth * kChunkWidth;
        constexpr int kBufferSize  = kColumns * kTotalHeight;

        // Minecraft 的区块内存布局是 XZY：索引 = (x*16 + z)*totalHeight + y
        [[nodiscard]] constexpr int bufferIndex(int x, int yIdx, int z)
        {
            return (x * kChunkWidth + z) * kTotalHeight + yIdx;
        }

        /** 把整个 y 层填成同一个方块。 */
        void fillLayer(std::vector<Block const*>& buf, Block const* block, int yIdx)
        {
            for (int i = 0; i < kColumns; ++i) buf[static_cast<size_t>(yIdx + i * kTotalHeight)] = block;
        }

        void fillRange(std::vector<Block const*>& buf, Block const* block, int fromIdx, int toIdxExclusive)
        {
            for (int y = fromIdx; y < toIdxExclusive; ++y) fillLayer(buf, block, y);
        }

        ll::io::Logger& logger()
        {
            static auto log = ll::io::LoggerRegistry::getInstance().getOrCreate("more_dimensions");
            return *log;
        }

        /** 和 ChunkTrace 共用同一个开关，见 ChunkTrace.h。 */
        bool traceChunk()
        {
            static bool const on = []
            {
                auto const* v = std::getenv("MORE_DIMENSIONS_TRACE_CHUNK");
                return v && v[0] == '1';
            }();
            return on;
        }

        // getDefaultBlockState 找不到时返回的是 air 而不是 null，所以传
        // logNotFound=true，让配错的方块 id 在日志里留痕，而不是静默变空气。
        Block const* lookupBlock(HashedString const& id)
        {
            return &BlockTypeRegistry::get().getDefaultBlockState(id, true);
        }


    } // namespace

    PlotGenerator::PlotGenerator(
        Dimension& dimension, uint seed, Json::Value const& options, PlotLayout const& layout
    )
    : FlatWorldGenerator(dimension, seed, options), mLayout(layout)
    {
        mLayout.clamp();

        auto& level = dimension.mLevel;
        mBiome      = level.getBiomeRegistry().lookupByName(mLayout.biome);
        if (!mBiome) {
            // 群系名写错不该让整个服务器起不来，退回平原。
            logger().warn("未知生物群系 '{}'，回退到 minecraft:plains", mLayout.biome);
            mBiome = level.getBiomeRegistry().lookupByName("minecraft:plains");
        }
        if (mBiome) mBiomeSource = std::make_unique<FixedBiomeSource>(*mBiome);

        mAirBlock     = lookupBlock(HashedString{"minecraft:air"});
        mBedrockBlock = lookupBlock(VanillaBlockTypeIds::Bedrock());
        mFloorBlock   = lookupBlock(HashedString{mLayout.floorBlock});
        mFillBlock    = lookupBlock(HashedString{mLayout.fillBlock});
        mRoadBlock    = lookupBlock(HashedString{mLayout.roadBlock});
        mBorderBlock  = lookupBlock(HashedString{mLayout.borderBlock});

        mFloorIndexY  = mLayout.floorY - PlotLayout::kMinY;
        mBorderIndexY = mFloorIndexY + 1;
    }

    PlotGenerator::ThreadBuffer& PlotGenerator::acquireBuffer()
    {
        static thread_local ThreadBuffer buf;
        if (buf.blocks.size() != static_cast<size_t>(kBufferSize)) {
            buf.blocks.assign(static_cast<size_t>(kBufferSize), nullptr);
            buf.owner = nullptr;
        }
        if (buf.owner != static_cast<void const*>(this)) {
            refillStatic(buf);
            buf.owner = static_cast<void const*>(this);
        }
        return buf;
    }

    /** 重填与列无关的部分（基岩 / 填充 / 空气）以及 BlockVolume 视图。 */
    void PlotGenerator::refillStatic(ThreadBuffer& buf)
    {
        // 维度底部下移到 y=-512 之后，基岩不再放在缓冲区索引 0，而是留在 y=-64。
        // 它下面那一段全是空气：玩家看到的世界和以前完全一样，只是维度往下多出
        // 了 28 个空子区块，用来和客户端写死的底部（子区块 -32）对齐。
        int const bedrockIdx = PlotLayout::kBedrockY - PlotLayout::kMinY;

        fillRange(buf.blocks, mAirBlock, 0, bedrockIdx);
        fillLayer(buf.blocks, mBedrockBlock, bedrockIdx);
        fillRange(buf.blocks, mFillBlock, bedrockIdx + 1, mFloorIndexY);
        fillRange(buf.blocks, mAirBlock, mFloorIndexY, kTotalHeight);

        buf.volume                 = mPrototype;
        buf.volume->mHeight        = static_cast<uint>(kTotalHeight);
        buf.volume->mBlocks->mBegin = &*buf.blocks.begin();
        buf.volume->mBlocks->mEnd   = &*buf.blocks.end();
    }

    void PlotGenerator::loadChunk(LevelChunk& lc, bool)
    {
        if (traceChunk())
        {
            auto const& cp = lc.mPosition.get();
            logger().info(
                "[gen>  ] 区块 ({}, {}) 进入 PlotGenerator::loadChunk，当前状态编号 {}",
                cp.x, cp.z, static_cast<int>(lc.mLoadState->load())
            );
        }

        auto& buf = acquireBuffer();

        // 每个区块开工前把两层"会变"的层重置掉：上一个区块留下的路缘和
        // 道路方块不能渗到这个区块里来。
        fillLayer(buf.blocks, mFloorBlock, mFloorIndexY);
        fillLayer(buf.blocks, mAirBlock, mBorderIndexY);

        auto const& chunkPos = lc.mPosition.get();
        int const   startX   = chunkPos.x * kChunkWidth;
        int const   startZ   = chunkPos.z * kChunkWidth;
        int const   cell     = mLayout.cellSize();

        for (int x = 0; x < kChunkWidth; ++x) {
            int const ix = positiveMod(startX + x, cell);
            auto const ax = classify1D(ix, mLayout);

            for (int z = 0; z < kChunkWidth; ++z) {
                int const  iz   = positiveMod(startZ + z, cell);
                auto const az   = classify1D(iz, mLayout);
                auto const area = combine2D(ax, az);

                switch (area) {
                case PlotArea1D::Road:
                    buf.blocks[static_cast<size_t>(bufferIndex(x, mFloorIndexY, z))] = mRoadBlock;
                    break;
                case PlotArea1D::Border:
                    // 地表保持地皮方块，上面加一圈路缘
                    buf.blocks[static_cast<size_t>(bufferIndex(x, mBorderIndexY, z))] = mBorderBlock;
                    break;
                case PlotArea1D::Plot:
                    break; // 已经是地表方块 + 上方空气
                }
            }
        }

        lc.setBlockVolume(*buf.volume, 0);
        if (mBiomeSource) mBiomeSource->fillBiomes(lc, nullptr);
        lc.recomputeHeightMap(false);
        lc.setSaved();

        // 之前这里是手写的 CAS：
        //
        //     auto generating = ChunkState::Generating;
        //     lc.mLoadState->compare_exchange_strong(generating, ChunkState::Generated);
        //
        // 两个问题。一是它只换了那个原子量，而 LevelChunk::tryChangeState 是一个
        // 真正的导出函数，除了换值之外还有别的事要做（等在这块地上的那条链路要被
        // 叫醒）。二是 CAS 失败时完全静默 —— 状态但凡不是恰好 Generating，这一步
        // 就什么都没干，日志上一个字都没有，区块就永远停在那儿。
        if (!lc.tryChangeState(ChunkState::Generating, ChunkState::Generated))
        {
            logger().error(
                "区块 ({}, {}) 的 Generating -> Generated 跃迁失败，当前状态编号 {} —— 这块地不会被送到客户端",
                chunkPos.x, chunkPos.z, static_cast<int>(lc.mLoadState->load())
            );
        }
        else if (traceChunk())
        {
            logger().info("[gen   ] 区块 ({}, {}) 生成完毕", chunkPos.x, chunkPos.z);
        }
    }
} // namespace more_dimensions
