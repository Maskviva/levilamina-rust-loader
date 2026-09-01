/**
 * bridge/KvDbApi.cpp — key-value database (ABI v5 §I).
 *
 * The one resource-style handle in the ABI, so the ownership rules are
 * explicit: the loader news every KeyValueDB into a registry keyed by mod;
 * kvdb_close (or the Rust Drop) closes it; unload force-closes leftovers
 * with a warning. All operations are guarded by one mutex, making the whole
 * family thread-safe by contract — mods may hit their DB from background tasks.
 * Paths are confined to the mod's own data directory.
 */
#include "bridge/Api.h"
#include "bridge/Common.h"

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "ll/api/data/KeyValueDB.h"
#include "ll/api/io/LoggerRegistry.h"
#include "ll/api/utils/ErrorUtils.h"

#include "RustMod.h"

namespace levi_rs::bridge
{
    namespace
    {
        struct KvEntry
        {
            RustMod* mod = nullptr;
            std::unique_ptr<ll::data::KeyValueDB> db;
        };

        std::mutex gKvMutex;
        std::unordered_map<uint64_t, KvEntry> gKvDbs;
        uint64_t gNextKvId = 1;

        KvEntry* entryOf(LeviRsKvDbHandle h)
        {
            auto id = reinterpret_cast<uint64_t>(h);
            auto it = gKvDbs.find(id);
            return it == gKvDbs.end() ? nullptr : &it->second;
        }

        /** Confine `rel` under the mod's data directory; empty on escape attempts. */
        std::filesystem::path confinedPath(RustMod* mod, std::string_view rel)
        {
            if (rel.empty()) return {};
            std::filesystem::path p{std::u8string{rel.begin(), rel.end()}};
            if (p.is_absolute()) return {};
            for (auto const& part : p)
            {
                if (part == "..") return {};
            }
            return mod->getDataDir() / p;
        }
    } // namespace

    LeviRsKvDbHandle api_kvdb_open(LeviRsModHandle modHandle, LeviRsStr path, bool createIfMissing)
    {
        LEVI_RS_API_GUARD_BEGIN
            auto* mod = asMod(modHandle);
            if (!mod) return nullptr;
            auto full = confinedPath(mod, std::string_view{path});
            if (full.empty())
            {
                mod->getLogger().error("kvdb_open: path must be relative and stay inside the mod data dir");
                return nullptr;
            }
            try
            {
                std::error_code ec;
                std::filesystem::create_directories(full.parent_path(), ec);
                // 4-arg ctor: (path, createIfMiss, fixIfError, bloomFilterBit); 0 = no bloom filter.
                auto db = std::make_unique<ll::data::KeyValueDB>(full, createIfMissing, false, 0);
                std::lock_guard lock(gKvMutex);
                uint64_t id = gNextKvId++;
                gKvDbs[id] = KvEntry{mod, std::move(db)};
                return reinterpret_cast<LeviRsKvDbHandle>(id);
            }
            catch (...)
            {
                mod->getLogger().error("kvdb_open: failed to open '{}'", std::string_view{path});
                static auto logger = ll::io::LoggerRegistry::getInstance().getOrCreate("levi_rs::bridge");
                ll::error_utils::printCurrentException(*logger);
                return nullptr;
            }
        LEVI_RS_API_GUARD_END
    }

    void api_kvdb_close(LeviRsKvDbHandle h)
    {
        LEVI_RS_API_GUARD_BEGIN
            std::lock_guard lock(gKvMutex);
            gKvDbs.erase(reinterpret_cast<uint64_t>(h));
        LEVI_RS_API_GUARD_END_VOID
    }

    bool api_kvdb_get(LeviRsKvDbHandle h, LeviRsStr key, void* ctx, LeviRsStrSink sink)
    {
        LEVI_RS_API_GUARD_BEGIN
            if (!sink) return false;
            std::lock_guard lock(gKvMutex);
            auto* e = entryOf(h);
            if (!e) return false;
            auto value = e->db->get(std::string_view{key});
            if (!value) return false;
            sink(ctx, *value);
            return true;
        LEVI_RS_API_GUARD_END
    }

    bool api_kvdb_set(LeviRsKvDbHandle h, LeviRsStr key, LeviRsStr value)
    {
        LEVI_RS_API_GUARD_BEGIN
            std::lock_guard lock(gKvMutex);
            auto* e = entryOf(h);
            if (!e) return false;
            return e->db->set(std::string_view{key}, std::string_view{value});
        LEVI_RS_API_GUARD_END
    }

    bool api_kvdb_del(LeviRsKvDbHandle h, LeviRsStr key)
    {
        LEVI_RS_API_GUARD_BEGIN
            std::lock_guard lock(gKvMutex);
            auto* e = entryOf(h);
            if (!e) return false;
            return e->db->del(std::string_view{key});
        LEVI_RS_API_GUARD_END
    }

    bool api_kvdb_has(LeviRsKvDbHandle h, LeviRsStr key)
    {
        LEVI_RS_API_GUARD_BEGIN
            std::lock_guard lock(gKvMutex);
            auto* e = entryOf(h);
            if (!e) return false;
            return e->db->has(std::string_view{key});
        LEVI_RS_API_GUARD_END
    }

    bool api_kvdb_is_empty(LeviRsKvDbHandle h)
    {
        LEVI_RS_API_GUARD_BEGIN
            std::lock_guard lock(gKvMutex);
            auto* e = entryOf(h);
            if (!e) return true;
            return e->db->empty();
        LEVI_RS_API_GUARD_END
    }

    void api_kvdb_iter(LeviRsKvDbHandle h, void* ctx, LeviRsKvSink sink)
    {
        LEVI_RS_API_GUARD_BEGIN
            if (!sink) return;
            std::lock_guard lock(gKvMutex);
            auto* e = entryOf(h);
            if (!e) return;
            for (auto&& [key, value] : e->db->iter())
            {
                sink(ctx, key, value);
            }
        LEVI_RS_API_GUARD_END_VOID
    }

    void kvdbOnRustModGone(RustMod* mod)
    {
        std::lock_guard lock(gKvMutex);
        size_t leaked = 0;
        for (auto it = gKvDbs.begin(); it != gKvDbs.end();)
        {
            if (it->second.mod == mod)
            {
                it = gKvDbs.erase(it);
                ++leaked;
            }
            else
            {
                ++it;
            }
        }
        if (leaked > 0)
        {
            mod->getLogger().warn("kvdb: force-closed {} database(s) left open at unload", leaked);
        }
    }
} // namespace levi_rs::bridge
