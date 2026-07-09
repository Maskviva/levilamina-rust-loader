#pragma once

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
    };
} // namespace levi_rs
