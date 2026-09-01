/**
 * bridge/GapFill.cpp — ABI v5 additive gap-fill stubs (struct_size-gated).
 *
 * 34 dedicated functions appended to LeviRsApi as additive fields. Without
 * implementations the table slots would be NULL (value-initialized), crashing
 * the Rust side on any call. These stubs return false / -1 / 0 — the Rust
 * safe layer surfaces that as Err("unsupported"). Real logic is implemented
 * inline where the MC/LL API is straightforward; the rest remain stubs until
 * the corresponding BDS API is confirmed.
 */
#include "bridge/Api.h"
#include "bridge/Common.h"

#include <cmath>
#include <string>
#include <vector>

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/phys/AABB.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/shared_types/legacy/EquipmentSlot.h"
#include "mc/network/NetworkPeer.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/storage/LevelStorage.h"
#include "mc/world/level/storage/db_helpers/Category.h"
#include "mc/world/level/BlockSource.h"
#include "mc/network/NetworkIdentifier.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/PlayerSleepStatus.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/phys/HitResult.h"
#include "mc/world/item/enchanting/ItemEnchants.h"
#include "mc/world/item/enchanting/EnchantmentInstance.h"

namespace levi_rs::bridge
{
    /* ── Player: equipment, cooldown, network (dedicated fns) ── */

    bool api_player_get_carried_item(LeviRsPlayerSel sel, void* ctx, LeviRsStrSink sink)
    {
        LEVI_RS_API_GUARD_BEGIN
            Player* p = resolvePlayer(sel);
            if (!p || !sink) return false;
            sink(ctx, itemToSnbt(p->getCarriedItem()));
            return true;
        LEVI_RS_API_GUARD_END
    }

    bool api_player_get_item(LeviRsPlayerSel sel, int32_t slot, void* ctx, LeviRsStrSink sink)
    {
        LEVI_RS_API_GUARD_BEGIN
            Player* p = resolvePlayer(sel);
            if (!p || !sink || slot < 0) return false;
            auto& inv = p->getInventory();
            if (slot >= inv.getContainerSize()) return false;
            sink(ctx, itemToSnbt(inv.getItem(slot)));
            return true;
        LEVI_RS_API_GUARD_END
    }

    bool api_player_set_item(LeviRsPlayerSel sel, int32_t slot, LeviRsStr item_snbt)
    {
        LEVI_RS_API_GUARD_BEGIN
            Player* p = resolvePlayer(sel);
            if (!p || slot < 0) return false;
            auto opt = itemFromSnbt(std::string_view{item_snbt});
            if (!opt) return false;
            auto& inv = p->getInventory();
            if (slot >= inv.getContainerSize()) return false;
            inv.setItem(slot, std::move(*opt));
            return true;
        LEVI_RS_API_GUARD_END
    }

    bool api_player_get_equipment(LeviRsPlayerSel sel, void* ctx, LeviRsStrSink sink)
    {
        LEVI_RS_API_GUARD_BEGIN
            Player* p = resolvePlayer(sel);
            if (!p || !sink) return false;
            // slot: 0=mainhand 1=offhand 2=helmet 3=chestplate 4=leggings 5=boots.
            // Actor::getEquippedSlot(EquipmentSlot) is the unified reader for all
            // six slots; it returns ItemStack const& (empty item when the slot is
            // empty, so itemToSnbt yields "{}").
            namespace Equip = ::SharedTypes::Legacy;
            std::string out = "[";
            out += "{\"slot\":0,\"item\":" + itemToSnbt(p->getEquippedSlot(Equip::EquipmentSlot::Mainhand)) + "}";
            out += ",{\"slot\":1,\"item\":" + itemToSnbt(p->getEquippedSlot(Equip::EquipmentSlot::Offhand)) + "}";
            out += ",{\"slot\":2,\"item\":" + itemToSnbt(p->getEquippedSlot(Equip::EquipmentSlot::Head)) + "}";
            out += ",{\"slot\":3,\"item\":" + itemToSnbt(p->getEquippedSlot(Equip::EquipmentSlot::Torso)) + "}";
            out += ",{\"slot\":4,\"item\":" + itemToSnbt(p->getEquippedSlot(Equip::EquipmentSlot::Legs)) + "}";
            out += ",{\"slot\":5,\"item\":" + itemToSnbt(p->getEquippedSlot(Equip::EquipmentSlot::Feet)) + "}";
            out += "]";
            sink(ctx, out);
            return true;
        LEVI_RS_API_GUARD_END
    }

    int32_t api_player_get_cooldown(LeviRsPlayerSel sel, LeviRsStr item_name)
    {
        LEVI_RS_API_GUARD_BEGIN
            Player* p = resolvePlayer(sel);
            if (!p) return -1;
            // getItemCooldownLeft takes a HashedString category; string literals
            // convert implicitly. Returns ticks remaining (0 = not on cooldown).
            return p->getItemCooldownLeft(HashedString{std::string{item_name}});
            // -1 是这一族约定的失败值（见 ClientStubs 里的同名桩）；0 会被当成真实答案。
        LEVI_RS_API_GUARD_END_VAL(-1)
    }

    bool api_player_start_cooldown(LeviRsPlayerSel sel, LeviRsStr item_name, int32_t ticks)
    {
        LEVI_RS_API_GUARD_BEGIN
            Player* p = resolvePlayer(sel);
            if (!p) return false;
            // startItemCooldown(HashedString const&, int tickDuration, bool updateClient)
            p->startItemCooldown(HashedString{std::string{item_name}}, ticks, true);
            return true;
        LEVI_RS_API_GUARD_END
    }

    bool api_player_get_network_status(LeviRsPlayerSel sel, void* ctx, LeviRsStrSink sink)
    {
        LEVI_RS_API_GUARD_BEGIN
            Player* p = resolvePlayer(sel);
            if (!p || !sink) return false;
            // getNetworkStatus() returns std::optional<NetworkPeer::NetworkStatus>;
            // a missing value means the peer is gone (disconnecting). The fields
            // mCurrentPing/mAveragePing are wrapped chrono::milliseconds → use ->count().
            auto opt = p->getNetworkStatus();
            if (!opt) return false;
            auto const& ns = *opt;
            std::string snbt = "{ping:" + snbtNum(ns.mCurrentPing->count());
            snbt += ",avg_ping:" + snbtNum(ns.mAveragePing->count());
            snbt += ",packet_loss:" + snbtNum(ns.mCurrentPacketLoss);
            snbt += ",avg_packet_loss:" + snbtNum(ns.mAveragePacketLoss);
            snbt += ",max_bps:" + snbtNum(ns.mApproximateMaxBps) + "}";
            sink(ctx, snbt);
            return true;
        LEVI_RS_API_GUARD_END
    }

    /* ── Actor: relationships, equipment, effects, geometry ── */

    bool api_actor_get_vehicle(LeviRsActorId id, LeviRsActorId* out)
    {
        LEVI_RS_API_GUARD_BEGIN
            Actor* a = resolveActor(id);
            if (!a || !out) return false;
            auto* v = a->getVehicle();
            if (!v) return false;
            *out = v->getOrCreateUniqueID().rawID;
            return true;
        LEVI_RS_API_GUARD_END
    }

    bool api_actor_get_first_passenger(LeviRsActorId id, LeviRsActorId* out)
    {
        LEVI_RS_API_GUARD_BEGIN
            Actor* a = resolveActor(id);
            if (!a || !out) return false;
            // Actor has no getPassengers(); getFirstPassenger() returns the head
            // passenger directly (nullptr when none are riding).
            Actor* p = a->getFirstPassenger();
            if (!p) return false;
            *out = p->getOrCreateUniqueID().rawID;
            return true;
        LEVI_RS_API_GUARD_END
    }

    bool api_actor_get_owner(LeviRsActorId id, LeviRsActorId* out)
    {
        LEVI_RS_API_GUARD_BEGIN
            Actor* a = resolveActor(id);
            if (!a || !out) return false;
            auto* owner = a->getOwner();
            if (!owner) return false;
            *out = owner->getOrCreateUniqueID().rawID;
            return true;
        LEVI_RS_API_GUARD_END
    }

    bool api_actor_get_target(LeviRsActorId id, LeviRsActorId* out)
    {
        LEVI_RS_API_GUARD_BEGIN
            Actor* a = resolveActor(id);
            if (!a || !out) return false;
            auto* target = a->getTarget();
            if (!target) return false;
            *out = target->getOrCreateUniqueID().rawID;
            return true;
        LEVI_RS_API_GUARD_END
    }

    bool api_actor_get_equipped_item(LeviRsActorId id, int32_t slot, void* ctx, LeviRsStrSink sink)
    {
        LEVI_RS_API_GUARD_BEGIN
            Actor* a = resolveActor(id);
            if (!a || !sink || slot < 0 || slot > 5) return false;
            // slot: 0=mainhand 1=offhand 2=helmet 3=chestplate 4=leggings 5=boots.
            // Actor::getEquippedSlot covers all six via EquipmentSlot.
            namespace Equip = ::SharedTypes::Legacy;
            auto es = static_cast<Equip::EquipmentSlot>(slot);
            sink(ctx, itemToSnbt(a->getEquippedSlot(es)));
            return true;
        LEVI_RS_API_GUARD_END
    }

    bool api_actor_set_equipped_item(LeviRsActorId id, int32_t slot, LeviRsStr item_snbt)
    {
        LEVI_RS_API_GUARD_BEGIN
            Actor* a = resolveActor(id);
            if (!a || slot < 0 || slot > 5) return false;
            auto opt = itemFromSnbt(std::string_view{item_snbt});
            if (!opt) return false;
            namespace Equip = ::SharedTypes::Legacy;
            auto es = static_cast<Equip::EquipmentSlot>(slot);
            a->setEquippedSlot(es, *opt);
            return true;
        LEVI_RS_API_GUARD_END
    }

    bool api_actor_get_effects(LeviRsActorId id, void* ctx, LeviRsStrSink sink)
    {
        LEVI_RS_API_GUARD_BEGIN
            Actor* a = resolveActor(id);
            if (!a || !sink) return false;
            // getAllEffects() returns vector<MobEffectInstance> const&. Each
            // instance exposes getId(), getAmplifier(), getDuration().getValue()
            // (optional — empty when infinite).
            std::string out = "[";
            bool first = true;
            for (auto const& e : a->getAllEffects())
            {
                if (!first) out += ",";
                first = false;
                auto dur = e.getDuration().getValue();
                out += "{id:" + snbtNum(e.getId());
                out += ",amp:" + snbtNum(e.getAmplifier());
                out += ",duration:" + (dur ? snbtNum(*dur) : "-1");
                out += "}";
            }
            out += "]";
            sink(ctx, out);
            return true;
        LEVI_RS_API_GUARD_END
    }

    bool api_actor_get_status_flag(LeviRsActorId id, int32_t flag_index)
    {
        LEVI_RS_API_GUARD_BEGIN
            Actor* a = resolveActor(id);
            if (!a) return false;
            return a->getStatusFlag(static_cast<ActorFlags>(flag_index));
        LEVI_RS_API_GUARD_END
    }

    bool api_actor_set_status_flag(LeviRsActorId id, int32_t flag_index, bool value)
    {
        LEVI_RS_API_GUARD_BEGIN
            Actor* a = resolveActor(id);
            if (!a) return false;
            a->setStatusFlag(static_cast<ActorFlags>(flag_index), value);
            return true;
        LEVI_RS_API_GUARD_END
    }

    bool api_actor_trace_ray(
        LeviRsActorId id, float max_dist, bool include_actors, bool include_blocks,
        void* ctx, LeviRsStrSink sink)
    {
        LEVI_RS_API_GUARD_BEGIN
            Actor* a = resolveActor(id);
            if (!a || !sink) return false;
            // Actor::traceRay(tMax, includeActor, includeBlock, blockCheckFn).
            // Returns HitResult with mType (Tile/Entity/NoHit), mPos, mEntity.
            auto hr = a->traceRay(max_dist, include_actors, include_blocks);
            std::string out = "{type:" + snbtNum(static_cast<int>(hr.mType));
            out += ",pos:[" + snbtNum(hr.mPos.x) + "," + snbtNum(hr.mPos.y)
                + "," + snbtNum(hr.mPos.z) + "]";
            out += "}";
            sink(ctx, out);
            return true;
        LEVI_RS_API_GUARD_END
    }

    bool api_actor_distance_to(LeviRsActorId id, LeviRsActorId other, double* out)
    {
        LEVI_RS_API_GUARD_BEGIN
            Actor* a = resolveActor(id);
            Actor* b = resolveActor(other);
            if (!a || !b || !out) return false;
            auto pa = a->getPosition();
            auto pb = b->getPosition();
            float dx = pa.x - pb.x, dy = pa.y - pb.y, dz = pa.z - pb.z;
            *out = std::sqrt(static_cast<double>(dx * dx + dy * dy + dz * dz));
            return true;
        LEVI_RS_API_GUARD_END
    }

    bool api_actor_get_aabb(LeviRsActorId id, void* ctx, LeviRsStrSink sink)
    {
        LEVI_RS_API_GUARD_BEGIN
            Actor* a = resolveActor(id);
            if (!a || !sink) return false;
            auto aabb = a->getAABB();
            std::string snbt = "{min:[" + snbtNum(aabb.min.x) + "," + snbtNum(aabb.min.y) + ","
                + snbtNum(aabb.min.z) + "],max:[" + snbtNum(aabb.max.x) + ","
                + snbtNum(aabb.max.y) + "," + snbtNum(aabb.max.z) + "]}";
            sink(ctx, snbt);
            return true;
        LEVI_RS_API_GUARD_END
    }

    bool api_actor_clone(LeviRsActorId id, int32_t dim, double x, double y, double z, LeviRsActorId* out)
    {
        LEVI_RS_API_GUARD_BEGIN
            Actor* a = resolveActor(id);
            if (!a || !out) return false;
            // Actor::clone(Vec3 const& pos, optional<DimensionType>) returns
            // optional_ref<Actor>. The cloned actor inherits NBT state (health,
            // equipment, name, etc.); caller decides where it lands.
            auto opt = a->clone(Vec3{(float)x, (float)y, (float)z},
                                DimensionType{dim});
            if (!opt) return false;
            *out = opt->getOrCreateUniqueID().rawID;
            return true;
        LEVI_RS_API_GUARD_END
    }

    /* ── Block: state get/set, collision shape ── */

    bool api_block_get_state(
        int32_t dim, int32_t x, int32_t y, int32_t z, LeviRsStr state_name,
        void* ctx, LeviRsStrSink sink)
    {
        LEVI_RS_API_GUARD_BEGIN
            auto* bs = blockSourceOf(dim);
            if (!bs || !sink) return false;
            auto const& block = bs->getBlock(BlockPos{x, y, z});
            // Look up the BlockState by name through BlockType, then read its
            // value. getState<T> is templated on the value type; we use int as
            // the common denominator — state values are integers in BDS's
            // state model (booleans, enums, ints all collapse to int).
            auto const* state = block.getBlockType().getBlockState(HashedString{std::string{state_name}});
            if (!state) return false;
            auto v = block.getState<int>(*state);
            if (!v) return false;
            sink(ctx, snbtNum(*v));
            return true;
        LEVI_RS_API_GUARD_END
    }

    bool api_block_set_state(
        int32_t dim, int32_t x, int32_t y, int32_t z, LeviRsStr state_name, LeviRsStr value)
    {
        LEVI_RS_API_GUARD_BEGIN
            auto* bs = blockSourceOf(dim);
            if (!bs) return false;
            auto const& block = bs->getBlock(BlockPos{x, y, z});
            // trySetState returns optional_ref<Block const> — the new block
            // permutation (or empty when the state/value is invalid). The
            // caller must write it back to the world via BlockSource::setBlock.
            auto const* state = block.getBlockType().getBlockState(HashedString{std::string{state_name}});
            if (!state) return false;
            int v;
            try { v = std::stoi(std::string{value}); }
            catch (...) { return false; }
            auto opt = block.setState(*state, v);
            if (!opt) return false;
            // ── 这里以前是个假成功 ──
            // 老实现算完新的 permutation 之后直接 `return true`，注释说「BlockSource
            // 没有公开的 setBlock(Block) 重载」。**有的**：`setBlock(pos, block,
            // updateFlags, syncMsg, changeContext)` 就是公开的虚函数（BlockSource.h）。
            //
            // 后果不是「少一个功能」，是**报告成功但世界没变**：调用方看到 Ok(())，
            // 方块纹丝不动，而且没有任何日志。任何靠它还原朝向的代码都会在
            // 「代码看着没问题、方块就是不转」上耗掉一整天。
            return bs->setBlock(
                BlockPos{x, y, z}, *opt, 3, nullptr, BlockChangeContext::commandsChange());
        LEVI_RS_API_GUARD_END
    }

    bool api_block_get_collision_shape(
        int32_t dim, int32_t x, int32_t y, int32_t z, void* ctx, LeviRsStrSink sink)
    {
        LEVI_RS_API_GUARD_BEGIN
            auto* bs = blockSourceOf(dim);
            if (!bs || !sink) return false;
            BlockPos pos{x, y, z};
            auto const& block = bs->getBlock(pos);
            // Block::getCollisionShape fills a single AABB out-param and returns
            // true when the block has a collision box. Multi-box shapes need
            // BlockSource::fetchCollisionShapes; this gap-fill reports the primary
            // shape only (sufficient for most "can I walk through this" queries).
            AABB aabb;
            bool has = block.getCollisionShape(aabb, *bs, pos, nullptr);
            if (!has)
            {
                sink(ctx, "[]");
                return true;
            }
            std::string out = "[{min:[" + snbtNum(aabb.min.x) + "," + snbtNum(aabb.min.y)
                + "," + snbtNum(aabb.min.z) + "],max:[" + snbtNum(aabb.max.x) + ","
                + snbtNum(aabb.max.y) + "," + snbtNum(aabb.max.z) + "]}]";
            sink(ctx, out);
            return true;
        LEVI_RS_API_GUARD_END
    }

    /* ── Item: enchants, matching, NBT ── */

    bool api_item_get_enchants(LeviRsStr item_snbt, void* ctx, LeviRsStrSink sink)
    {
        LEVI_RS_API_GUARD_BEGIN
            auto opt = itemFromSnbt(std::string_view{item_snbt});
            if (!opt || !sink) return false;
            // constructItemEnchantsFromUserData() returns an ItemEnchants object
            // built from the item's saved NBT; getAllEnchants() flattens the
            // three activation-type vectors into one.
            auto enchants = opt->constructItemEnchantsFromUserData();
            auto list = enchants.getAllEnchants();
            std::string out = "[";
            bool first = true;
            for (auto const& e : list)
            {
                if (!first) out += ",";
                first = false;
                // mEnchantType is Enchant::Type (uchar enum); mLevel is int.
                out += "{type:" + snbtNum(static_cast<int>(static_cast<uchar>(e.mEnchantType)));
                out += ",level:" + snbtNum(e.mLevel) + "}";
            }
            out += "]";
            sink(ctx, out);
            return true;
        LEVI_RS_API_GUARD_END
    }

    bool api_item_set_enchants(
        LeviRsStr item_snbt, LeviRsStr enchants_snbt, void* ctx, LeviRsStrSink out)
    {
        LEVI_RS_API_GUARD_BEGIN
            auto opt = itemFromSnbt(std::string_view{item_snbt});
            if (!opt || !out) return false;
            // Building an ItemEnchants from individual {type,level} pairs requires
            // either: (a) a ListTag in the exact NBT format the constructor
            // expects (id+lvl pairs as compound entries), or (b) the
            // EnchantUtils::applyEnant path — neither is straightforward from a
            // public-API standpoint. For now, surface "unsupported" rather than
            // risk corrupting the item's user data. The get_enchants call above
            // is read-only and safe; setting is deferred until the enchant
            // plumbing is wired up.
            (void)enchants_snbt;
            return false;
        LEVI_RS_API_GUARD_END
    }

    bool api_item_matches(LeviRsStr a, LeviRsStr b)
    {
        LEVI_RS_API_GUARD_BEGIN
            auto oa = itemFromSnbt(std::string_view{a});
            auto ob = itemFromSnbt(std::string_view{b});
            if (!oa || !ob) return false;
            return oa->matches(*ob);
        LEVI_RS_API_GUARD_END
    }

    bool api_item_get_user_data(LeviRsStr item_snbt, void* ctx, LeviRsStrSink sink)
    {
        LEVI_RS_API_GUARD_BEGIN
            auto opt = itemFromSnbt(std::string_view{item_snbt});
            if (!opt || !sink) return false;
            auto* ud = opt->getUserData();
            if (!ud)
            {
                sink(ctx, "{}");
                return true;
            }
            sink(ctx, ud->toSnbt(SnbtFormat::Minimize));
            return true;
        LEVI_RS_API_GUARD_END
    }

    /* ── Level: biome, spawn, save, weather, path, sleep ── */

    bool api_level_get_biome(int32_t dim, int32_t x, int32_t y, int32_t z, void* ctx, LeviRsStrSink sink)
    {
        LEVI_RS_API_GUARD_BEGIN
            auto* bs = blockSourceOf(dim);
            if (!bs || !sink) return false;
            // tryGetBiome returns Biome const* (nullable); getBiome returns a ref
            // that is never null in well-formed chunks. Use the nullable variant
            // so unloaded chunks report failure instead of dereferencing garbage.
            auto const* biome = bs->tryGetBiome(BlockPos{x, y, z});
            if (!biome) return false;
            // Biome has no getName(); the id string lives in the public mHash
            // member (a TypedStorage<HashedString>). Bind to HashedString const&
            // first, then call getString() (TypedStorage -> HashedString -> string
            // -> string_view needs an explicit hop; two implicit UDCs aren't
            // allowed).
            ::HashedString const& hash = biome->mHash;
            sink(ctx, hash.getString());
            return true;
        LEVI_RS_API_GUARD_END
    }

    bool api_level_get_default_spawn(int32_t* x, int32_t* y, int32_t* z)
    {
        LEVI_RS_API_GUARD_BEGIN
            auto* level = levelReady();
            if (!level || !x || !y || !z) return false;
            auto pos = level->getDefaultSpawn();
            *x = pos.x;
            *y = pos.y;
            *z = pos.z;
            return true;
        LEVI_RS_API_GUARD_END
    }

    bool api_level_set_default_spawn(int32_t x, int32_t y, int32_t z)
    {
        LEVI_RS_API_GUARD_BEGIN
            auto* level = levelReady();
            if (!level) return false;
            level->setDefaultSpawn(BlockPos{x, y, z});
            return true;
        LEVI_RS_API_GUARD_END
    }

    bool api_level_save()
    {
        LEVI_RS_API_GUARD_BEGIN
            auto* level = levelReady();
            if (!level) return false;
            level->save();
            return true;
        LEVI_RS_API_GUARD_END
    }

    /**
     * 拼一个区块的键前缀。
     *
     * `<chunkX:i32 LE><chunkZ:i32 LE>`，非主世界再跟 `<dimension:i32 LE>`。
     * 主世界（dim 0）**没有**那第三段 —— 加上的话前缀匹配不到任何键，
     * 调用方会以为「这个区块本来就是空的」。
     */
    std::string chunkKeyPrefix(int32_t dim, int32_t chunk_x, int32_t chunk_z)
    {
        std::string out;
        auto put_le = [&out](int32_t v)
        {
            uint32_t u = static_cast<uint32_t>(v);
            out.push_back(static_cast<char>(u & 0xFF));
            out.push_back(static_cast<char>((u >> 8) & 0xFF));
            out.push_back(static_cast<char>((u >> 16) & 0xFF));
            out.push_back(static_cast<char>((u >> 24) & 0xFF));
        };
        put_le(chunk_x);
        put_le(chunk_z);
        if (dim != 0) put_le(dim);
        return out;
    }

    /**
     * ⚠ **退役。** 一律返回 -1。
     *
     * 这一格原来是「列出这个区块的键并全删」一步做完，而它**在真机上把服务器
     * 打崩了** —— 崩在 C++ 侧那个 `std::vector<std::string>` 销毁的时候。
     *
     * 槽位不能删（ABI 只能追加，删了后面每一格都会错位），所以它留在这儿
     * 明确失效，功能搬到 [`api_level_chunk_keys`] + [`api_level_delete_key`]。
     *
     * 返回 -1 而不是 0：0 是「这个区块本来就是空的」，会让调用方以为抹成功了。
     */
    int32_t api_level_delete_chunk_keys(int32_t, int32_t, int32_t) { return -1; }

    /**
     * 列出一个区块的全部存档键，一个键一次回调。
     *
     * # 这里**什么都不攒**
     *
     * 上一版在 C++ 里把键收进 `std::vector<std::string>` 再逐个删，
     * 而它在真机上崩了 —— 崩在函数返回、销毁那个 vector 的时候，
     * 寄存器里看得到字符串的内联缓冲被当成了堆指针。
     *
     * 根因没定位到（跨 DLL 的 `std::string` 生命周期，没有调试器查不出来）。
     * 但那一类问题的来源是「在 C++ 侧攒一个字符串容器、跨一次虚调用、
     * 再在自己的栈上销毁它」，所以现在一个都不攒：拿到一个就交出去，
     * 容器活在 Rust 那边。
     *
     * 键是二进制的（含 0 字节），`LeviRsStr` 带长度，不靠 0 结尾。
     */
    int32_t api_level_chunk_keys(int32_t dim, int32_t chunk_x, int32_t chunk_z, void* ctx, LeviRsStrSink sink)
    {
        LEVI_RS_API_GUARD_BEGIN
            auto* level = levelReady();
            if (!level || !level->hasLevelStorage() || !sink) return -1;
            auto& storage = level->getLevelStorage();
            std::string const prefix = chunkKeyPrefix(dim, chunk_x, chunk_z);

            int32_t n = 0;
            storage.forEachKeyWithPrefix(
                prefix, ::DBHelpers::Category::Chunk,
                [ctx, sink, &n](std::string_view k, std::string_view)
                {
                    // 直接把这一条交出去。`LeviRsStr` 在 C++ 侧**就是
                    // `std::string_view`**，所以这里连一次转换都没有 ——
                    // 它只在这次回调里有效，Rust 那边负责拷走。
                    sink(ctx, k);
                    ++n;
                }
            );
            return n;
            // -1 是这一族约定的失败值（见 ClientStubs 里的同名桩）；0 会被当成真实答案。
        LEVI_RS_API_GUARD_END_VAL(-1)
    }

    /**
     * 原样删掉一个区块类别的键。
     *
     * **不解释键的内容** —— 传什么删什么，这正是抹整块之所以安全的原因：
     * 不需要懂子区块的调色板和位打包格式。
     */
    bool api_level_delete_key(LeviRsStr key)
    {
        LEVI_RS_API_GUARD_BEGIN
            auto* level = levelReady();
            if (!level || !level->hasLevelStorage() || key.empty()) return false;
            // `deleteData` 收 `std::string const&`。这个临时对象活到本语句结束，
            // 而删除是同步提交进写批的 —— 上一版的问题不在这里，在那个 vector。
            level->getLevelStorage().deleteData(std::string{key}, ::DBHelpers::Category::Chunk);
            return true;
        LEVI_RS_API_GUARD_END
    }

    /**
     * 这一片的区块加载着吗。
     *
     * # 签名不是「两个角」，是「中心 + 半径」
     *
     * `BlockSource::hasChunksAt` 这个版本只有 `(BlockPos const&, int, bool)`
     * 一个重载 —— 传两个 `BlockPos` 编译不过。所以这里把调用方给的方框
     * 换算成中心点加半径。
     *
     * # 半径要**往里缩一格**
     *
     * 区块的加载是整块的，所以方框内部**任何一点**都能代表整块的答案。
     * 而按原样的半径查，一个 16 宽的方框会正好碰到相邻区块的第一格 ——
     * 于是「邻居加载着」会被读成「我这块加载着」，抹除就永远等不到时机。
     *
     * 缩一格之后查的严格是方框内部，答案只关于我们要问的那些区块。
     *
     * 第三个参数是 `ignoreClientChunk`：传 true，我们问的是**服务端**有没有
     * 这块地的数据 —— 客户端缓存和「卸载时会不会把键写回去」无关。
     */
    int32_t api_level_chunks_loaded(int32_t dim, int32_t min_x, int32_t min_z, int32_t max_x, int32_t max_z)
    {
        LEVI_RS_API_GUARD_BEGIN
            auto* bs = blockSourceOf(dim);
            if (!bs) return -1;
            int32_t cx = (min_x + max_x) / 2;
            int32_t cz = (min_z + max_z) / 2;
            int32_t half_x = (max_x - min_x) / 2;
            int32_t half_z = (max_z - min_z) / 2;
            int32_t r = half_x < half_z ? half_x : half_z;
            if (r > 0) --r; // 往里缩一格，别碰到邻居
            if (r < 0) r = 0;
            // y 取地面高度：区块是整列加载的，y 取多少不影响答案，但一个越界的 y
            // 会让某些版本直接返回 false。
            BlockPos at{cx, 0, cz};
            return bs->hasChunksAt(at, r, true) ? 1 : 0;
            // -1 是这一族约定的失败值（见 ClientStubs 里的同名桩）；0 会被当成真实答案。
        LEVI_RS_API_GUARD_END_VAL(-1)
    }

    /**
     * 玩家的连接号。
     *
     * **必须和 `PacketHooks.cpp` 里 `connId = id.getHash()` 算的是同一个数** ——
     * 那边是拦包回调看到的号，这边是按名字问出来的号，对不上的话
     * 「改写这个人的包」会安静地一个包都改不到。
     */
    uint64_t api_player_conn_id(LeviRsPlayerSel who)
    {
        LEVI_RS_API_GUARD_BEGIN
            Player* p = resolvePlayer(who);
            if (!p) return 0;
            return static_cast<uint64_t>(p->getNetworkIdentifier().getHash());
        LEVI_RS_API_GUARD_END
    }

    bool api_level_get_sleep_status(void* ctx, LeviRsStrSink sink)
    {
        LEVI_RS_API_GUARD_BEGIN
            auto* level = levelReady();
            if (!level || !sink) return false;
            // PlayerSleepStatus has: mSleepingPlayerCount, mRequiredSleepingPlayerCount,
            // mAbleToSleep (all scalar — TypedStorage collapses to raw int/int/bool).
            auto ss = level->getSleepStatus();
            std::string snbt = "{sleeping:" + snbtNum(ss.mSleepingPlayerCount);
            snbt += ",required:" + snbtNum(ss.mRequiredSleepingPlayerCount);
            snbt += ",able_to_sleep:" + snbtNum(ss.mAbleToSleep ? 1 : 0) + "}";
            sink(ctx, snbt);
            return true;
        LEVI_RS_API_GUARD_END
    }

    bool api_level_update_weather(float rain_level, int32_t rain_time, float lightning_level, int32_t lightning_time)
    {
        LEVI_RS_API_GUARD_BEGIN
            auto* level = levelReady();
            if (!level) return false;
            level->updateWeather(rain_level, rain_time, lightning_level, lightning_time);
            return true;
        LEVI_RS_API_GUARD_END
    }

    bool api_level_find_path(LeviRsActorId id, int32_t x, int32_t y, int32_t z, void* ctx, LeviRsStrSink sink)
    {
        LEVI_RS_API_GUARD_BEGIN
            (void)id;
            (void)x;
            (void)y;
            (void)z;
            (void)ctx;
            (void)sink;
            return false; // stub: needs PathFinder plumbing
        LEVI_RS_API_GUARD_END
    }
} // namespace levi_rs::bridge
