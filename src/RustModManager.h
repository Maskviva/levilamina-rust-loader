#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "ll/api/Expected.h"
#include "ll/api/mod/Manifest.h"
#include "ll/api/mod/ModManager.h"

namespace levi_rs
{
    /**
     * ModManager for manifest `"type": "rust"`.
     *
     * Registered by the loader mod in ll_mod_load. Because ModRegistrar resolves
     * managers at load-dispatch time (see ModManagerRegistry::loadMod) and sorts
     * mods topologically by their `dependencies`, any rust mod that declares
     *   "dependencies": [{ "name": "levilamina-rust-loader" }]
     * is guaranteed to be dispatched after this manager exists.
     */
    class RustModManager : public ll::mod::ModManager
    {
    public:
        RustModManager();
        ~RustModManager() override;

        ll::Expected<> load(ll::mod::Manifest manifest) override;
        ll::Expected<> unload(std::string_view name) override;

        /**
         * The running manager, or nullptr before ll_mod_load / after shutdown.
         * Set in the constructor, cleared in the destructor — the registry owns
         * the shared_ptr, so this is a plain observer.
         */
        static RustModManager* instance();

        /* ── Runtime mod control (backing /llr) ────────────────────────────
         * ModManagerRegistry's loadMod/unloadMod/enableMod/disableMod are all
         * PRIVATE (friended only to ModRegistrar and Mod), so there is no
         * public LeviLamina path to load a mod after startup. What we *can*
         * do is call the protected ModManager methods we inherit — which is
         * exactly what these wrappers are for.
         *
         * Consequence worth knowing: a mod brought up this way lives in this
         * manager's own table, not in LeviLamina's dependency graph. Dependency
         * checking is therefore done by ModControl.cpp walking manifests
         * directly, which has the side benefit of also catching native C++
         * mods that depend on a rust mod. */

        /** Bring up a parsed manifest: load() then enable(). */
        ll::Expected<> controlLoad(ll::mod::Manifest manifest);

        /** Take one down: disable() (fires on_disable) then unload(). */
        ll::Expected<> controlUnload(std::string_view name);

        /** Base address of a loaded mod's dylib, or nullptr if not loaded.
         *  Used to detect the Windows FreeLibrary refcount trap: if a reload
         *  hands back the SAME base address, the image was never unmapped and
         *  the "new" dll on disk was not actually picked up. */
        [[nodiscard]] void const* moduleBase(std::string_view name) const;

        /** Names of every rust mod currently loaded by this manager. */
        [[nodiscard]] std::vector<std::string> loadedNames() const;
    };
} // namespace levi_rs
