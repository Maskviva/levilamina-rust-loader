/**
 * bridge/WorldInfo.cpp — read-only world-data queries (ROADMAP §5): village
 * and hardcoded-spawn-area (HSA) inspection. Separate from World.cpp (block
 * read/write) so the "walk an internal manager and serialise it" concern
 * stays isolated from the per-block hot path.
 *
 * Both entries stream SNBT objects through a sink (one per village / per
 * area), matching the list_players / scan_region pattern. All reads are
 * observational (no game state changes) and server-thread only.
 *
 * Version note: the fields below were verified against BDS 26.20.0 headers —
 * Village exposes getBounds/getCenter/getPOICount/getUniqueID; per-chunk HSAs
 * live in LevelChunk::mSpawningAreas as {aabb, type}. Dweller enumeration is
 * intentionally omitted: villagers hang off POIInstance weak_ptr arrays keyed
 * by role, which is fragile to walk and version-sensitive — POI *count* is the
 * stable signal. If a later version needs dwellers, add it here without
 * touching the ABI shape (the payload is data, not layout).
 */
#include "bridge/Api.h"
#include "bridge/Common.h"

#include <cmath>
#include <memory>
#include <string>

#include "mc/deps/core/math/Vec3.h"
#include "mc/platform/UUID.h"
#include "mc/world/actor/ai/village/Village.h"
#include "mc/world/actor/ai/village/VillageManager.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/ChunkPos.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/level/levelgen/structure/BoundingBox.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/levelgen/v1/HardcodedSpawnAreaType.h"
#include "mc/world/phys/AABB.h"

namespace levi_rs::bridge
{
    namespace
    {
        std::string hsaTypeName(HardcodedSpawnAreaType t)
        {
            switch (t)
            {
            case HardcodedSpawnAreaType::NetherFortress:
                return "nether_fortress";
            case HardcodedSpawnAreaType::WitchHut:
                return "witch_hut";
            case HardcodedSpawnAreaType::OceanMonument:
                return "ocean_monument";
            case HardcodedSpawnAreaType::PillagerOutpost:
                return "pillager_outpost";
            case HardcodedSpawnAreaType::VillageDeprecated:
            case HardcodedSpawnAreaType::NewVillageDeprecated:
                return "village_deprecated";
            default:
                return "none";
            }
        }
    } // namespace

    void api_villages(int32_t dimension, void* ctx, LeviRsStrSink snbtSink)
    {
        auto* level = levelReady();
        if (!level || !snbtSink) return;
        auto dim = level->getDimension(DimensionType{dimension}).lock();
        if (!dim) return;

        // getVillageManager() returns unique_ptr const& (object storage — the
        // TypedStorage IS the value; no .get() gymnastics on the member).
        auto const& mgr = dim->getVillageManager();
        if (!mgr) return;

        // mVillages: unordered_map<UUID, shared_ptr<Village>> (object storage,
        // so .get() yields the map). Reading a private member of a live object
        // we hold by ref — no lifetime hazard on the server thread.
        for (auto const& [id, villagePtr] : mgr->mVillages.get())
        {
            if (!villagePtr) continue;
            Village& v = *villagePtr;
            AABB const& b = v.getBounds();
            Vec3 c = v.getCenter();

            std::string snbt = "{\"uuid\":\"" + snbtEscape(v.getUniqueID().asString())
                             + "\",\"center\":[" + std::to_string(c.x) + "," + std::to_string(c.y)
                             + "," + std::to_string(c.z) + "]"
                             + ",\"bounds\":{\"min\":[" + std::to_string(b.min.x) + ","
                             + std::to_string(b.min.y) + "," + std::to_string(b.min.z)
                             + "],\"max\":[" + std::to_string(b.max.x) + ","
                             + std::to_string(b.max.y) + "," + std::to_string(b.max.z) + "]}"
                             + ",\"poi_count\":" + std::to_string(v.getPOICount()) + "}";
            snbtSink(ctx, snbt);
        }
    }

    void api_structures_near(
        int32_t dimension, int32_t x, int32_t y, int32_t z, int32_t radius, void* ctx, LeviRsStrSink snbtSink)
    {
        auto* level = levelReady();
        if (!level || !snbtSink) return;
        if (radius < 0) return;
        auto dim = level->getDimension(DimensionType{dimension}).lock();
        if (!dim) return;

        BlockSource& region = dim->getBlockSourceFromMainChunkSource();

        // HSAs are stored per LevelChunk. Walk the chunk square covering the
        // radius (16-block chunks); only loaded chunks yield data — that's the
        // honest limit (we can't read unloaded chunks without generating them,
        // which a read-only query must not do).
        int cxMin = (x - radius) >> 4, cxMax = (x + radius) >> 4;
        int czMin = (z - radius) >> 4, czMax = (z + radius) >> 4;
        (void)y; // HSAs are full-height per chunk; y not used for selection

        for (int cx = cxMin; cx <= cxMax; ++cx)
        {
            for (int cz = czMin; cz <= czMax; ++cz)
            {
                LevelChunk* chunk = region.getChunk(ChunkPos{cx, cz});
                if (!chunk) continue; // unloaded — skip, don't force-load

                for (auto const& area : chunk->mSpawningAreas.get())
                {
                    // SpawningArea.aabb is TypedStorage over BoundingBox
                    // (object type — needs .get()); .type is a scalar enum
                    // (reference/scalar specialisation — the member IS the
                    // value). BoundingBox min/max are BlockPos (integers).
                    BoundingBox const& bb = area.aabb.get();
                    std::string snbt = "{\"type\":\"" + hsaTypeName(area.type)
                                     + "\",\"bounds\":{\"min\":[" + std::to_string(bb.min.x) + ","
                                     + std::to_string(bb.min.y) + "," + std::to_string(bb.min.z)
                                     + "],\"max\":[" + std::to_string(bb.max.x) + ","
                                     + std::to_string(bb.max.y) + "," + std::to_string(bb.max.z) + "]}}";
                    snbtSink(ctx, snbt);
                }
            }
        }
    }
} // namespace levi_rs::bridge
