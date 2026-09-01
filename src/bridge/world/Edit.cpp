/**
 * bridge/Edit.cpp — 批量世界编辑的原生入口。
 *
 * # 这个文件解决的是什么
 *
 * 在它之前，Rust 侧往世界里写一个方块只有一条路：`api_set_block`，而那条路
 * 底层是 `execute in <dim> run setblock …` **一条控制台命令**（World.cpp）。
 * 于是三件事做不了：
 *
 *   1. 方块状态只能靠把序列化 NBT 翻译成 `["k"=v]` 命令语法。翻错一处 =
 *      整条命令失败 = 那一格静默不变。楼梯朝向、原木轴向、门的左右开全在
 *      这条路上丢过。
 *   2. 方块实体写不回去。`block_entity_snbt` 只读不写，箱子里的东西、
 *      告示牌的字、刷怪笼的怪，复制过去就没了。
 *   3. 实体放不回去。`spawn_mob` 只认类型名，快照里的变种 / 装备 / 年龄全丢。
 *
 * 引擎侧这三件事都有现成入口，只是没接出来：
 *
 *   | 要做的事 | 引擎入口 |
 *   |---|---|
 *   | 写方块（带状态） | `BlockSerializationUtils::tryGetBlockFromNBT` + `BlockSource::setBlock` |
 *   | 写方块实体 | `BlockSource::getBlockEntity` + `BlockActor::load` |
 *   | 从 NBT 放实体 | `ActorFactory::loadActor` + `Level::addEntity` |
 *
 * # 顺带的量级变化
 *
 * 一次 setblock 命令要过命令解析、权限检查、命令分发；`BlockSource::setBlock`
 * 是一次直接调用。这不是「快一点」，是把「两百万格要分帧跑几十秒」变成
 * 「同一批格子跑一遍就完了」。Rust 侧的分帧引擎（job.rs）仍然要留 —— 但它
 * 现在限制的是**每 tick 的时间预算**，不再是命令分发的吞吐。
 *
 * # 为什么不顺手把 `/setblock` 那条路删掉
 *
 * 因为它还有用：玩家手写的方块规格（`//set 'wool ["color"="red"]'`）走命令
 * 解析是最省事的，而且那条路已经被验证了很久。这个文件加的是**新的**入口，
 * 不是替换 —— 旧 mod 一行不改照样跑。
 */
#include "bridge/Api.h"
#include "bridge/Common.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "mc/dataloadhelper/DefaultDataLoadHelper.h"
#include "mc/dataloadhelper/NewUniqueIdsDataLoadHelper.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/ecs/gamerefs_entity/OwnerStorageEntity.h"
#include "mc/deps/game_refs/OwnerPtr.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/CompoundTagVariant.h"
#include "mc/deps/nbt/FloatTag.h"
#include "mc/deps/nbt/ListTag.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorFactory.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/block_serialization_utils/BlockSerializationUtils.h"
#include "mc/world/level/block/registry/BlockTypeRegistry.h"
#include "mc/world/phys/HitResult.h"

namespace levi_rs::bridge
{
    namespace
    {
        /**
         * 写方块时用哪个「变更来源」。
         *
         * 用 `commandsChange()` 而不是 `structureChange()`：这条路取代的正是
         * `/setblock`，保持同一个来源意味着**别的插件挂在方块变更上的钩子看到
         * 的东西不变**。换成 structure 会让一部分保护插件突然不再拦截 ——
         * 那种「换了个实现，别人的插件失效了」的坑，值得用一行注释钉住。
         */
        BlockChangeContext editContext() { return BlockChangeContext::commandsChange(); }

        /** 序列化 NBT（`{name,states,version}`）→ Block const*，失败返回 nullptr。 */
        Block const* blockFromTag(CompoundTag const& tag)
        {
            // tryGetBlockFromNBT 会顺带跑引擎的版本升级表：老存档里的
            // {name:"minecraft:wool",states:{color:…}} 能被正确升级成新方块。
            // 这正是我们要的 —— 手工解析 states 做不到这一步。
            auto pair = BlockSerializationUtils::tryGetBlockFromNBT(tag, nullptr);
            return pair.second;
        }

        /** 补上 minecraft: 前缀。注册表里的键是带命名空间的。 */
        std::string qualify(std::string_view name)
        {
            std::string s{name};
            if (s.find(':') == std::string::npos) s = "minecraft:" + s;
            return s;
        }

        /**
         * 方块名 → 默认状态。**找不到时返回 nullptr**，不返回「未知方块」。
         *
         * `getDefaultBlockState` 对不认识的名字会给一个占位方块而不是报错，
         * 于是 `//set 拼错的名字` 会安静地把整片地区填成那个占位方块。
         * 这里比对一次 type_name 把它挡住 —— 调用方拿到 false 可以回落到
         * 命令路径去拿一句真正的错误信息。
         */
        Block const* defaultBlockOf(std::string_view name)
        {
            std::string full = qualify(name);
            auto const& block = BlockTypeRegistry::get().getDefaultBlockState(HashedString{full}, false);
            if (block.getTypeName() != full) return nullptr;
            return &block;
        }
    } // namespace

    // 这两个原来只在本文件的匿名 namespace 里。World.cpp 的 api_set_block 从
    // 命令路径改成原生之后也要用，所以提出来共用 —— 两处解析方块的规则必须
    // 是同一套，否则 `//set` 和 `setblock` 会对同一个名字给出不同结果。
    Block const* blockFromSnbt(std::string_view snbt)
    {
        auto parsed = CompoundTag::fromSnbt(snbt);
        if (!parsed) return nullptr;
        return blockFromTag(*parsed);
    }

    Block const* defaultBlockNamed(std::string_view name) { return defaultBlockOf(name); }

    // 同上，World.cpp 的 api_set_block / POP_RESOURCE 改成原生之后也要用。
    // 保持和 //set 同一个变更来源很重要：别的插件挂在方块变更上的钩子看到的
    // 东西不能因为我们换了实现就变。
    BlockChangeContext blockEditContext() { return editContext(); }

    // ───────────────────────── 方块 ─────────────────────────

    bool api_edit_set_block_nbt(
        int32_t dim, int32_t x, int32_t y, int32_t z, LeviRsStr snbt, int32_t update_flags)
    {
        LEVI_RS_API_GUARD_BEGIN
            auto* bs = blockSourceOf(dim);
            if (!bs) return false;
            auto parsed = CompoundTag::fromSnbt(std::string_view{snbt});
            if (!parsed) return false;
            Block const* block = blockFromTag(*parsed);
            if (!block) return false;
            return bs->setBlock(BlockPos{x, y, z}, *block, update_flags, nullptr, editContext());
        LEVI_RS_API_GUARD_END
    }

    bool api_edit_set_block_states(
        int32_t dim,
        int32_t x,
        int32_t y,
        int32_t z,
        LeviRsStr name,
        LeviRsStr states_snbt,
        int32_t update_flags)
    {
        LEVI_RS_API_GUARD_BEGIN
            auto* bs = blockSourceOf(dim);
            if (!bs) return false;
            Block const* def = defaultBlockOf(std::string_view{name});
            if (!def) return false;

            std::string_view states{states_snbt};
            if (states.empty())
            {
                // 没有状态要覆盖：默认状态就是答案，一次解析都不用做。
                return bs->setBlock(BlockPos{x, y, z}, *def, update_flags, nullptr, editContext());
            }

            // 从默认方块的序列化标签出发，**只覆盖调用方给出的那几个状态**。
            //
            // 这样做而不是让调用方自己拼整个 {name,states,version}：version 必须
            // 是当前版本，而调用方没有可靠办法知道它。填错（或者不填）会让引擎
            // 把这次写入当成远古存档跑一遍升级表 —— 表现是「我明明写的是这个状态，
            // 放出来却是另一个」。
            CompoundTag tag = def->getSerializationId();
            auto extra = CompoundTag::fromSnbt(states);
            if (!extra) return false;
            auto& target = tag["states"];
            if (target.hold<CompoundTag>())
            {
                auto& base = target.get<CompoundTag>();
                for (auto const& kv : extra->mTags) base[kv.first] = kv.second;
            }
            else
            {
                tag["states"] = std::move(*extra);
            }

            Block const* block = blockFromTag(tag);
            if (!block) return false;
            return bs->setBlock(BlockPos{x, y, z}, *block, update_flags, nullptr, editContext());
        LEVI_RS_API_GUARD_END
    }

    bool api_edit_set_block_entity(int32_t dim, int32_t x, int32_t y, int32_t z, LeviRsStr snbt)
    {
        LEVI_RS_API_GUARD_BEGIN
            auto* level = levelReady();
            auto* bs = blockSourceOf(dim);
            if (!level || !bs) return false;
            BlockPos pos{x, y, z};
            auto* be = bs->getBlockEntity(pos);
            // 那一格没有方块实体 —— 调用方的顺序错了（应该先放方块再填内容），
            // 或者放的方块本来就没有方块实体。报 false，别装作成功。
            if (!be) return false;

            auto parsed = CompoundTag::fromSnbt(std::string_view{snbt});
            if (!parsed) return false;

            // 快照里的 x/y/z 是**源位置**。不改的话，某些方块实体（活塞、命令方块）
            // 会按那个坐标去找自己，结果是「内容对了，行为错了」。
            (*parsed)["x"] = x;
            (*parsed)["y"] = y;
            (*parsed)["z"] = z;

            DefaultDataLoadHelper helper{};
            be->load(*level, *parsed, helper);
            be->setChanged();
            // setChanged 只标脏。少了这一步，服务端是对的、客户端还是空箱子，
            // 直到区块重载 —— 而那时玩家早就以为复制失败了。
            be->onChanged(*bs);
            return true;
        LEVI_RS_API_GUARD_END
    }

    // ───────────────────────── 实体 ─────────────────────────

    bool api_edit_spawn_entity_nbt(
        int32_t dim,
        LeviRsStr snbt,
        bool use_pos,
        double x,
        double y,
        double z,
        LeviRsActorId* out)
    {
        LEVI_RS_API_GUARD_BEGIN
            auto* level = levelReady();
            auto* bs = blockSourceOf(dim);
            if (!level || !bs) return false;

            auto parsed = CompoundTag::fromSnbt(std::string_view{snbt});
            if (!parsed) return false;
            CompoundTag tag = std::move(*parsed);

            if (use_pos)
            {
                ListTag pos;
                pos.add(std::make_unique<FloatTag>(static_cast<float>(x)));
                pos.add(std::make_unique<FloatTag>(static_cast<float>(y)));
                pos.add(std::make_unique<FloatTag>(static_cast<float>(z)));
                tag["Pos"] = std::move(pos);
            }

            // NewUniqueIdsDataLoadHelper：把 NBT 里的 UniqueID 映射成**新的** id。
            // 这正是 /structure load 放实体时走的东西。沿用快照里的 id 会和源实体
            // 撞号，而撞号的表现是两个实体被引擎当成同一个 —— 一个凭空消失、
            // 另一个行为错乱，且没有任何日志。
            NewUniqueIdsDataLoadHelper helper{*level};
            auto owner = level->getActorFactory().loadActor(&tag, helper);
            if (!owner) return false;

            Actor* actor = level->addEntity(*bs, std::move(owner));
            if (!actor) return false;
            if (out) *out = actor->getOrCreateUniqueID().rawID;
            return true;
        LEVI_RS_API_GUARD_END
    }

    // ───────────────────────── 射线 ─────────────────────────

    bool api_edit_trace_ray(
        LeviRsActorId id,
        float max_dist,
        bool include_actors,
        bool include_blocks,
        void* ctx,
        LeviRsStrSink sink)
    {
        LEVI_RS_API_GUARD_BEGIN
            Actor* a = resolveActor(id);
            if (!a || !sink) return false;
            auto hr = a->traceRay(max_dist, include_actors, include_blocks);

            // 老的 actor_trace_ray 只发 mPos（一个浮点命中点）。命中点正好落在
            // 方块的**面**上，所以 floor() 有一半概率落到隔壁那一格 —— 任何
            // 「照着准星选方块」的功能都因此做不了。mBlock 和 mFacing 一直都在
            // HitResult 里，只是没往外发。
            std::string out = "{type:" + snbtNum(static_cast<int>(hr.mType));
            out += ",block:[" + snbtNum(hr.mBlock.x) + "," + snbtNum(hr.mBlock.y) + ","
                + snbtNum(hr.mBlock.z) + "]";
            out += ",facing:" + snbtNum(static_cast<int>(hr.mFacing));
            out += ",pos:[" + snbtNum(hr.mPos.x) + "," + snbtNum(hr.mPos.y) + ","
                + snbtNum(hr.mPos.z) + "]";

            int64_t entityId = 0;
            if (hr.mType == HitResultType::Entity)
            {
                // mEntity 是 WeakEntityRef；tryUnwrap<Actor>() 是 LL 给的安全解引用
                // （实体已经消失时返回空，而不是给一个悬垂指针）。
                if (auto hit = hr.mEntity.tryUnwrap<Actor>())
                {
                    entityId = hit->getOrCreateUniqueID().rawID;
                }
            }
            out += ",entity:" + snbtNum(entityId) + "L}";
            sink(ctx, out);
            return true;
        LEVI_RS_API_GUARD_END
    }

    // ───────────────────────── 液体层（含水） ─────────────────────────
    //
    // Bedrock 的「含水」是同一格上的第二个方块，不是方块状态：主层放楼梯，
    // 液体层放 water。get_block / set_block 只看主层，所以含水的方块复制过去
    // 水会消失 —— 主层完全正确，缺的是另一层。

    bool api_get_extra_block(
        int32_t dim, int32_t x, int32_t y, int32_t z, void* ctx, LeviRsStrSink sink)
    {
        LEVI_RS_API_GUARD_BEGIN
            if (!sink) return false;
            auto* bs = blockSourceOf(dim);
            if (!bs) return false;
            auto const& block = bs->getExtraBlock(BlockPos{x, y, z});
            // 空液体层返回的是 air，如实传出去 —— 调用方据此判断「这格没有含水」。
            sink(ctx, block.getTypeName());
            return true;
        LEVI_RS_API_GUARD_END
    }

    bool api_set_extra_block(
        int32_t dim, int32_t x, int32_t y, int32_t z, LeviRsStr blockSpec, int32_t updateFlags)
    {
        LEVI_RS_API_GUARD_BEGIN
            auto* bs = blockSourceOf(dim);
            if (!bs) return false;

            std::string_view spec{blockSpec};
            while (!spec.empty() && (spec.front() == ' ' || spec.front() == '\t')) spec.remove_prefix(1);
            if (spec.empty()) return false;

            Block const* block = spec.front() == '{' ? blockFromSnbt(spec) : defaultBlockNamed(spec);
            if (!block) return false;
            return bs->setExtraBlock(BlockPos{x, y, z}, *block, updateFlags);
        LEVI_RS_API_GUARD_END
    }
} // namespace levi_rs::bridge
