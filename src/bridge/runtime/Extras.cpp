/**
 * bridge/Extras.cpp — 追加槽（190）。
 *
 *   level_set_biome    改生物群系 → worldedit 的 `/we setbiome`
 *
 * 加它是为了让上层**不必再改 gamerule**。gamerule 是**整个存档一份**的，
 * 用它做「按世界的设置」永远解释不通：服主改一个世界，另一个世界跟着变。
 *
 * # 原计划还有两格，撤掉了
 *
 * 「删实体」和「设血量」写到一半才发现 `actor_action` 的 `AACT_DESPAWN` /
 * `AACT_HEAL` 早就做了同样的事，Rust 侧连 `Entity::despawn()` /
 * `Entity::heal()` 都封装好了。
 *
 * 加独立的槽只是把一件事做两遍，而**两份实现迟早分岔** —— 到那时候
 * 「为什么这里删得掉那里删不掉」会是一个没人答得上的问题。
 *
 * 教训是：加 ABI 之前先把现有的 190 格读一遍。这个错这两轮犯了两次
 * （上一次是 `list_actors`）。
 *
 * # 为什么单独一个文件
 *
 * 追加槽的生命周期和已有的不一样 —— 它们要靠 `struct_size` 守卫，
 * 而且随时可能因为 BDS 改签名而需要单独调整。混进 Actors.cpp 的话，
 * 下次排查「哪些是新加的」要一行行看 git。
 */
#include "bridge/Api.h"
#include "bridge/Common.h"

#include <algorithm>
#include <string>

// `ChunkBlockPos.h` 在 `mc/world/level/` 下，**不是** `.../level/chunk/` ——
// 后者放的是 `LevelChunk.h`。两个名字都带 chunk，而目录不一样。
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/ChunkBlockPos.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/biome/registry/BiomeRegistry.h"
#include "mc/world/level/chunk/LevelChunk.h"

namespace levi_rs::bridge
{
    int32_t api_level_set_biome(int32_t dim,
                                int32_t minX, int32_t minZ,
                                int32_t maxX, int32_t maxZ,
                                LeviRsStr biome)
    {
        LEVI_RS_API_GUARD_BEGIN
            BlockSource* bs = blockSourceOf(dim);
            if (!bs) return 0;

            const std::string name = std::string(biome);
            if (name.empty()) return 0;

            auto* registry = levelReady() ? &levelReady()->getBiomeRegistry() : nullptr;
            if (!registry) return 0;
            Biome const* target = registry->lookupByName(name);
            // 名字不认识就**一列都不设**，而不是设成默认群系。悄悄换成平原比
            // 报"没设上"糟得多：服主看到地形变了，但不是他要的那个。
            if (!target) return 0;

            if (minX > maxX) std::swap(minX, maxX);
            if (minZ > maxZ) std::swap(minZ, maxZ);

            int32_t done = 0;
            for (int32_t x = minX; x <= maxX; ++x)
            {
                for (int32_t z = minZ; z <= maxZ; ++z)
                {
                    // 逐列拿区块。**没加载的跳过，不强加载** —— 强加载一片大区域
                    // 会让主线程停住几秒，而调用方通常在玩家附近操作，
                    // 那些区块本来就是加载的。
                    LevelChunk* chunk = bs->getChunkAt(BlockPos(x, 0, z));
                    if (!chunk) continue;
                    // 用 `setBiome2d`，不是 `setBiome3d`：契约（见 LeviRsAbi.h /
                    // api.rs / blocks.rs）写的是「按整列设」。`setBiome2d` 内部走
                    // `_setBiome(..., fillYDimension=true)`，把整列 y 都填成同一个
                    // 群系；`setBiome3d` 只写 `pos.y` 那一个采样，拿它做「整列」
                    // 会只改到列底那一格。
                    //
                    // 位置用 `ChunkBlockPos::from2D(x, z)`：列坐标本就不需要 y，它
                    // 造的是一个 y=0 的 `ChunkBlockPos`。顺带它避开了直接写
                    // `ChunkBlockPos(x, 0, z)` 编不过的坑 —— 中间那个是
                    // `ChunkLocalHeight`，不能从 `int` 隐式构造（就是这次的报错）。
                    chunk->setBiome2d(*target,
                                      ChunkBlockPos::from2D(static_cast<uint8_t>(x & 15),
                                                            static_cast<uint8_t>(z & 15)));
                    ++done;
                }
            }
            return done;
        LEVI_RS_API_GUARD_END
    }
} // namespace levi_rs::bridge
