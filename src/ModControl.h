#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "ll/api/Expected.h"
#include "ll/api/mod/Manifest.h"

namespace levi_rs::mod_control
{
    /**
     * One `mods/<dir>/manifest.json` with `"type": "rust"`, as found on disk.
     *
     * Read straight from the file rather than from ll::mod::Manifest because
     * `reloadSafe` has no home in that struct. LeviLamina's reflection
     * deserializer walks the STRUCT's members looking them up in the JSON — it
     * never walks the JSON's keys — so an extra top-level field is silently
     * ignored by LeviLamina and is safe to add to any manifest.
     */
    struct Candidate
    {
        std::string name;
        std::string entry;
        std::string version;
        std::vector<std::string> dependencies;
        /** `"reload_safe": true` at the top level of manifest.json. */
        bool reloadSafe = false;
        /** Non-empty when the manifest exists but is unusable.
         *  Named `problem` rather than `error` so it never reads as the
         *  ll::Expected error channel at a call site. */
        std::string problem;
    };

    /** Re-read mods/ from disk. This is what `/llr list` calls, and it is the
     *  only way a directory added after server start becomes visible.
     *
     *  Deliberately cacheless: every query hits the disk. A cache here would
     *  buy nothing (these commands are typed by hand, not called in a loop)
     *  and could serve a stale entry point for a dll that was just rebuilt --
     *  exactly the case this command exists to support. */
    std::vector<Candidate> rescan();

    /** Look one up on disk by name, bypassing the cache (never stale). */
    ll::Expected<Candidate> readCandidate(std::string_view name);

    /** Build an ll::mod::Manifest from a candidate's manifest.json. */
    ll::Expected<ll::mod::Manifest> readManifest(std::string_view name);

    /**
     * Every loaded mod that declares `name` in its `dependencies`, transitively,
     * in an order safe to unload (dependents before their dependencies).
     * Covers native C++ mods too — they can depend on a rust mod just as easily.
     */
    std::vector<std::string> dependentsOf(std::string_view name);

    /** True if `name` is loaded and managed by us (a rust mod). */
    bool isRustMod(std::string_view name);

    /** True if `name` is loaded at all, by any manager. */
    bool isLoadedAnywhere(std::string_view name);

    /** Register `/llr`. Server builds only; call once from enable(). */
    void registerCommand();
} // namespace levi_rs::mod_control
