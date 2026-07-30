/**
 * bridge/World.cpp — world reading & writing (ABI v3 migrated + v5 §A/§D
 * blocks + explode).
 *
 * Block "handles" are (dimension, position) — resolved against the live
 * BlockSource on every call. Block writes go through /setblock (decision #3).
 */
#include "bridge/Api.h"
#include "bridge/Common.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/phys/AABB.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/SaveContext.h"
#include "mc/world/item/SaveContextFactory.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/dimension/Dimension.h"

namespace levi_rs::bridge
{
    bool api_spawn_particle(int32_t dimension, LeviRsStr effectName, double x, double y, double z)
    {
        auto* level = levelReady();
        if (!level) return false;
        auto dim = level->getDimension(DimensionType{dimension}).lock();
        if (!dim) return false;
        level->spawnParticleEffect(std::string{effectName}, Vec3{(float)x, (float)y, (float)z}, dim.get());
        return true;
    }

    LeviRsPlayerPos api_get_player_position(LeviRsStr name)
    {
        LeviRsPlayerPos out{0.0, 0.0, 0.0, 0, false};
        // Unified with the v5 player identity model: resolvePlayer matches
        // getRealName() first, then falls back to getNameTag() (display name) —
        // for normal players the two are identical, so v3 behaviour is preserved.
        Player* p = resolvePlayer(LeviRsPlayerSel{0, name});
        if (!p) return out;
        auto pos = p->getPosition();
        out.x = pos.x;
        out.y = pos.y;
        out.z = pos.z;
        out.dimension = static_cast<int>(p->getDimensionId());
        out.found = true;
        return out;
    }

    bool api_scan_region(
        int32_t dimension,
        int32_t x1,
        int32_t y1,
        int32_t z1,
        int32_t x2,
        int32_t y2,
        int32_t z2,
        void* ctx,
        LeviRsBlockSink blocksSink,
        LeviRsEntitySink entitiesSink
    )
    {
        auto* level = levelReady();
        if (!level) return false;
        auto* bs = blockSourceOf(dimension);
        if (!bs) return false;

        int minX = std::min(x1, x2), maxX = std::max(x1, x2);
        int minY = std::min(y1, y2), maxY = std::max(y1, y2);
        int minZ = std::min(z1, z2), maxZ = std::max(z1, z2);

        // Blocks: walk every cell in the box (bottom-to-top, then x, then z).
        if (blocksSink)
        {
            for (int y = minY; y <= maxY; ++y)
            {
                for (int x = minX; x <= maxX; ++x)
                {
                    for (int z = minZ; z <= maxZ; ++z)
                    {
                        auto const& block = bs->getBlock(BlockPos{x, y, z});
                        // This LL version has no getSerializationId() accessor; read
                        // the public mSerializationId member directly (same tag:
                        // {name, states, version}).
                        std::string snbt = block.mSerializationId.get().toSnbt(SnbtFormat::Minimize);
                        blocksSink(ctx, x, y, z, block.getTypeName(), snbt);
                    }
                }
            }
        }

        // Entities: filter the runtime actor list by the box, bin into cells.
        if (entitiesSink)
        {
            for (auto* actor : level->getRuntimeActorList())
            {
                if (!actor) continue;
                if (static_cast<int>(actor->getDimensionId()) != dimension) continue;
                auto pos = actor->getPosition();
                int ex = (int)std::floor(pos.x);
                int ey = (int)std::floor(pos.y);
                int ez = (int)std::floor(pos.z);
                if (ex < minX || ex > maxX || ey < minY || ey > maxY || ez < minZ || ez > maxZ) continue;
                CompoundTag tag;
                actor->save(tag);
                std::string snbt = tag.toSnbt(SnbtFormat::Minimize);
                entitiesSink(ctx, ex, ey, ez, actor->getTypeName(), snbt);
            }
        }
        return true;
    }

    // ───────────────────────── v5 §A: single-block read/write ─────────────────────────

    bool api_get_block(int32_t dim, int32_t x, int32_t y, int32_t z, void* ctx, LeviRsBlockSink sink)
    {
        auto* bs = blockSourceOf(dim);
        if (!bs || !sink) return false;
        auto const& block = bs->getBlock(BlockPos{x, y, z});
        std::string snbt = block.mSerializationId.get().toSnbt(SnbtFormat::Minimize);
        sink(ctx, x, y, z, block.getTypeName(), snbt);
        return true;
    }

    bool api_set_block(int32_t dim, int32_t x, int32_t y, int32_t z, LeviRsStr blockSpec)
    {
        if (!blockSourceOf(dim)) return false;
        // Dimension-targeted via /execute in — the command path keeps this stable
        // across BDS versions (decision #3).
        //
        // Custom dimensions (MoreDimensions, ids >= 3) used to be rejected here,
        // so nothing could ever write a block into one.
        auto const target = dimensionSelector(dim);
        if (target.empty()) return false;
        std::string cmd = "execute in " + target + " run setblock " + std::to_string(x) + " "
            + std::to_string(y) + " " + std::to_string(z) + " " + std::string{blockSpec};
        return runConsoleCommand(cmd);
    }

    // ───────────────────────── v5 §D: block properties ─────────────────────────

    namespace
    {
        Block const* blockAt(int32_t dim, int32_t x, int32_t y, int32_t z, BlockSource** bsOut = nullptr)
        {
            auto* bs = blockSourceOf(dim);
            if (!bs) return nullptr;
            if (bsOut) *bsOut = bs;
            return &bs->getBlock(BlockPos{x, y, z});
        }
    } // namespace

    bool api_block_get_num(int32_t dim, int32_t x, int32_t y, int32_t z, int32_t prop, double* out)
    {
        BlockSource* bs = nullptr;
        auto const* block = blockAt(dim, x, y, z, &bs);
        if (!block || !out) return false;
        switch (prop)
        {
        case LEVI_RS_BPROP_IS_AIR:
            *out = block->isAir() ? 1.0 : 0.0;
            return true;
        case LEVI_RS_BPROP_DATA:
            *out = static_cast<double>(block->getData());
            return true;
        case LEVI_RS_BPROP_BLOCK_ITEM_ID:
            *out = static_cast<double>(block->getBlockItemId());
            return true;
        case LEVI_RS_BPROP_IS_CRAFTING_BLOCK:
        case LEVI_RS_BPROP_IS_INTERACTIVE_BLOCK:
            // Block::isCraftingBlock() / isInteractiveBlock() are not present in
            // the auto-generated headers for every BDS 26.20.x point release
            // (they track Mojang's exported symbols, which shifted between
            // 26.20.0 and 26.20.2). Report "unsupported" rather than fail to
            // compile; the safe layer surfaces this as an error for these two
            // properties only.
            return false;
        case LEVI_RS_BPROP_HAS_BLOCK_ENTITY:
            *out = (bs->getBlockEntity(BlockPos{x, y, z}) != nullptr) ? 1.0 : 0.0;
            return true;
        /* ── v5 additive: block gap fill ── */
        case LEVI_RS_BPROP_LIGHT:
            *out = static_cast<double>(block->getLight().mValue);
            return true;
        case LEVI_RS_BPROP_LIGHT_EMISSION:
            *out = static_cast<double>(block->getLightEmission().mValue);
            return true;
        case LEVI_RS_BPROP_DESTROY_SPEED:
            *out = static_cast<double>(block->getDestroySpeed());
            return true;
        case LEVI_RS_BPROP_EXPLOSION_RESISTANCE:
            *out = static_cast<double>(block->getExplosionResistance());
            return true;
        case LEVI_RS_BPROP_FRICTION:
            *out = static_cast<double>(block->getFriction());
            return true;
        case LEVI_RS_BPROP_IS_CONTAINER:
            *out = block->isContainerBlock() ? 1.0 : 0.0;
            return true;
        case LEVI_RS_BPROP_IS_DOOR:
            *out = block->isDoorBlock() ? 1.0 : 0.0;
            return true;
        case LEVI_RS_BPROP_IS_FENCE:
            *out = block->isFenceBlock() ? 1.0 : 0.0;
            return true;
        case LEVI_RS_BPROP_IS_RAIL:
            *out = block->isRailBlock() ? 1.0 : 0.0;
            return true;
        case LEVI_RS_BPROP_IS_SLAB:
            *out = block->isSlabBlock() ? 1.0 : 0.0;
            return true;
        case LEVI_RS_BPROP_IS_STAIR:
            *out = block->isStairBlock() ? 1.0 : 0.0;
            return true;
        case LEVI_RS_BPROP_IS_WALL:
            *out = block->isWallBlock() ? 1.0 : 0.0;
            return true;
        case LEVI_RS_BPROP_IS_CROP:
            *out = block->isCropBlock() ? 1.0 : 0.0;
            return true;
        case LEVI_RS_BPROP_IS_UNBREAKABLE:
            *out = block->isUnbreakable() ? 1.0 : 0.0;
            return true;
        case LEVI_RS_BPROP_REDSTONE_SIGNAL:
            *out = static_cast<double>(bs->getBlock(BlockPos{x, y, z}).getDirectSignal(*bs, BlockPos{x, y, z}, 0));
            return true;
        case LEVI_RS_BPROP_COMPARATOR_SIGNAL:
            // getComparatorSignal(BlockSource&, BlockPos const&, uchar dir)
            // — dir=0 (down) is a safe default; callers needing a specific
            // direction should use the block-action API.
            *out = static_cast<double>(block->getComparatorSignal(*bs, BlockPos{x, y, z}, 0));
            return true;
        case LEVI_RS_BPROP_IS_SIGNAL_SOURCE:
            *out = block->isSignalSource() ? 1.0 : 0.0;
            return true;
        case LEVI_RS_BPROP_VARIANT:
            *out = static_cast<double>(block->getVariant());
            return true;
        case LEVI_RS_BPROP_BURN_ODDS:
            *out = static_cast<double>(block->getBurnOdds());
            return true;
        case LEVI_RS_BPROP_FLAME_ODDS:
            *out = static_cast<double>(block->getFlameOdds());
            return true;
        case LEVI_RS_BPROP_BOUNCINESS:
            // getBounciness(IConstBlockSource const&, BlockPos const&) — needs
            // the region for context-dependent bounciness (e.g. slime blocks).
            *out = static_cast<double>(block->getBounciness(*bs, BlockPos{x, y, z}));
            return true;
        case LEVI_RS_BPROP_IS_SOLID:
            *out = block->isSolid() ? 1.0 : 0.0;
            return true;
        case LEVI_RS_BPROP_REQUIRES_TOOL:
            *out = block->requiresCorrectToolForDrops() ? 1.0 : 0.0;
            return true;
        default:
            return false;
        }
    }

    bool api_block_get_str(int32_t dim, int32_t x, int32_t y, int32_t z, int32_t prop, void* ctx, LeviRsStrSink sink)
    {
        BlockSource* bs = nullptr;
        auto const* block = blockAt(dim, x, y, z, &bs);
        if (!block || !sink || !bs) return false;
        switch (prop)
        {
        case LEVI_RS_BSTR_TYPE_NAME:
            sink(ctx, block->getTypeName());
            return true;
        case LEVI_RS_BSTR_SNBT:
            sink(ctx, block->mSerializationId.get().toSnbt(SnbtFormat::Minimize));
            return true;
        case LEVI_RS_BSTR_DESCRIPTION_ID:
            {
                sink(ctx, block->getDescriptionId());
                return true;
            }
        case LEVI_RS_BSTR_DEBUG_STRING:
            sink(ctx, block->toDebugString());
            return true;
        case LEVI_RS_BSTR_TAGS:
            {
                std::string out = "[";
                for (auto const& tag : block->mTags.get())
                {
                    out += "\"" + snbtEscape(tag.getString()) + "\",";
                }
                if (out.back() == ',') out.pop_back();
                out += "]";
                sink(ctx, out);
                return true;
            }
        /* ── v5 additive ── */
        case LEVI_RS_BSTR_STATE:
            {
                // Serialize all block states as SNBT {name:value, …}
                sink(ctx, block->mSerializationId.get().toSnbt(SnbtFormat::Minimize));
                return true;
            }
        case LEVI_RS_BSTR_COLLISION_SHAPE:
            {
                // getCollisionShape(AABB& out, IConstBlockSource const&,
                // BlockPos const&, optional_ref) — fills a single AABB and
                // returns true when the block has a collision box. Multi-box
                // shapes need BlockSource::fetchCollisionShapes; this reports
                // the primary shape only.
                AABB aabb;
                bool has = block->getCollisionShape(aabb, *bs, BlockPos{x, y, z}, nullptr);
                std::string out = has
                    ? ("[{min:[" + std::to_string(aabb.min.x) + "," + std::to_string(aabb.min.y)
                        + "," + std::to_string(aabb.min.z) + "],max:[" + std::to_string(aabb.max.x)
                        + "," + std::to_string(aabb.max.y) + "," + std::to_string(aabb.max.z) + "]}]")
                    : "[]";
                sink(ctx, out);
                return true;
            }
        case LEVI_RS_BSTR_OUTLINE_SHAPE:
            {
                // getOutline(IConstBlockSource const&, BlockPos const&, AABB& buffer)
                // — returns const ref to the buffer (so the call still works
                // when the buffer is on the stack).
                AABB buffer;
                auto const& aabb = block->getOutline(*bs, BlockPos{x, y, z}, buffer);
                std::string out = "[{min:[" + std::to_string(aabb.min.x) + "," + std::to_string(aabb.min.y) + ","
                    + std::to_string(aabb.min.z) + "],max:[" + std::to_string(aabb.max.x) + ","
                    + std::to_string(aabb.max.y) + "," + std::to_string(aabb.max.z) + "]}]";
                sink(ctx, out);
                return true;
            }
        case LEVI_RS_BSTR_DISPLAY_NAME:
            sink(ctx, block->getDisplayName());
            return true;
        default:
            return false;
        }
    }

    bool api_block_action(
        int32_t dim,
        int32_t x,
        int32_t y,
        int32_t z,
        int32_t action,
        LeviRsStr sarg,
        void* ctx,
        LeviRsStrSink out
    )
    {
        BlockSource* bs = nullptr;
        auto const* block = blockAt(dim, x, y, z, &bs);
        if (!block || !bs) return false;
        switch (action)
        {
        case LEVI_RS_BACT_HAS_TAG:
            {
                bool has = block->hasTag(HashedString{std::string_view{sarg}});
                if (out) out(ctx, has ? "1" : "0");
                return true;
            }
        /* ── v5 additive ── */
        case LEVI_RS_BACT_GET_STATE:
            {
                // Block state lookup by name — not directly available via a
                // single getState(name) in this LL version. Return false
                // (unsupported) until a proper BlockState API is plumbed.
                return false;
            }
        case LEVI_RS_BACT_POP_RESOURCE:
            {
                // Pop a resource at the block position via /setblock air destroy
                // which drops the block's resources. The sarg (item SNBT) is
                // ignored — the engine handles the drop table.
                auto const target = dimensionSelector(dim);
                if (target.empty()) return false;
                return runConsoleCommand(
                    "execute in " + target + " run setblock "
                    + std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(z) + " air destroy"
                );
            }
        case LEVI_RS_BACT_AS_ITEM:
            {
                if (!out) return false;
                // asItemInstance(BlockSource&, BlockPos const&) returns an
                // ItemInstance (not an ItemStack). ItemInstance has no
                // SNBT serializer compatible with itemToSnbt's signature,
                // so serialize its user-data CompoundTag instead.
                auto item = block->asItemInstance(*bs, BlockPos{x, y, z});
                auto* ud = item.getUserData();
                out(ctx, ud ? ud->toSnbt(SnbtFormat::Minimize) : "{}");
                return true;
            }
        default:
            return false;
        }
    }

    bool api_block_entity_snbt(int32_t dim, int32_t x, int32_t y, int32_t z, void* ctx, LeviRsStrSink sink)
    {
        auto* bs = blockSourceOf(dim);
        if (!bs || !sink) return false;
        auto* be = bs->getBlockEntity(BlockPos{x, y, z});
        if (!be) return false;
        CompoundTag tag;
        auto saveCtx = SaveContextFactory::createCloneSaveContext();
        if (!be->save(tag, *saveCtx)) return false;
        sink(ctx, tag.toSnbt(SnbtFormat::Minimize));
        return true;
    }

    // ───────────────────────── v5 §C: explode ─────────────────────────

    bool api_explode(
        int32_t dim,
        double x,
        double y,
        double z,
        float radius,
        float maxResistance,
        LeviRsActorId source,
        bool fire,
        bool breaksBlocks,
        bool allowUnderwater
    )
    {
        auto* level = levelReady();
        auto* bs = blockSourceOf(dim);
        if (!level || !bs) return false;
        Actor* src = (source != 0) ? resolveActor(source) : nullptr;
        return level->explode(
            *bs,
            src,
            Vec3{(float)x, (float)y, (float)z},
            radius,
            fire,
            breaksBlocks,
            maxResistance,
            allowUnderwater
        );
    }
} // namespace levi_rs::bridge
