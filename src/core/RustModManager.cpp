#include "RustModManager.h"

#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ll/api/mod/NativeMod.h"
#include "ll/api/utils/StringUtils.h"

#include "BridgeApi.h"
#include "RustMod.h"

namespace levi_rs
{
    using ll::mod::Manifest;

    namespace
    {
        RustModManager* gInstance = nullptr;
    }

    RustModManager::RustModManager() : ModManager(RustModManagerName)
    {
        gInstance = this;
    }

    RustModManager::~RustModManager() { gInstance = nullptr; }

    RustModManager* RustModManager::instance() { return gInstance; }

    ll::Expected<> RustModManager::load(Manifest manifest)
    {
        auto mod = std::make_shared<RustMod>(std::move(manifest));

        std::error_code ec;
        auto modDir = ll::mod::getModsRoot() / ll::string_utils::sv2u8sv(mod->getName());
        if (auto c = std::filesystem::canonical(modDir, ec); ec.value() == 0)
        {
            modDir = c;
        }
        else
        {
            modDir = modDir.lexically_normal();
        }
        auto entry = modDir / ll::string_utils::sv2u8sv(mod->getManifest().entry);

        if (auto e = mod->lib.load(entry); e)
        {
            return ll::makeExceptionError(std::make_exception_ptr(*e));
        }

        auto main = mod->lib.getAddress<LeviRsMainFn>(LEVI_RS_MAIN_SYMBOL);
        if (!main)
        {
            (void)mod->lib.free();
            return ll::makeStringError(
                "'" + mod->getName() + "' does not export " LEVI_RS_MAIN_SYMBOL " (is it a levilamina-rs cdylib?)"
            );
        }

        mod->vtable = LeviRsModVTable{};
        if (!main(getBridgeApi(), static_cast<LeviRsModHandle>(mod.get()), &mod->vtable))
        {
            (void)mod->lib.free();
            return ll::makeStringError("'" + mod->getName() + "' " LEVI_RS_MAIN_SYMBOL " returned false");
        }
        // ABI compatibility is a RANGE, not exact equality. Additive-only
        // evolution (fields appended, never reordered/removed) means a newer
        // loader can run an older mod: the mod calls a byte-identical prefix
        // of our table and never reaches the trailing slots it doesn't know.
        //
        //   too new  (mod_abi > loader)  → the mod may call table slots we
        //                                  don't have. Refuse; user updates
        //                                  the loader.
        //   too old  (mod_abi < floor)   → predates a non-additive break, so
        //                                  our table is NOT a prefix of what
        //                                  the mod expects. Refuse; user
        //                                  rebuilds the mod.
        //   in range (floor ≤ mod_abi ≤ loader) → safe, load it.
        //
        // The opposite skew (older loader, newer mod) is caught on the mod
        // side by __init_runtime's `struct_size` check, so we don't need to.
        const uint32_t modAbi = mod->vtable.abi_version;
        if (modAbi > LEVI_RS_ABI_VERSION)
        {
            (void)mod->lib.free();
            return ll::makeStringError(
                "'" + mod->getName() + "' was built against levilamina-rs ABI v"
                + std::to_string(modAbi) + ", but this loader only speaks up to v"
                + std::to_string(LEVI_RS_ABI_VERSION) + " — update the levilamina-rust-loader mod"
            );
        }
        if (modAbi < LEVI_RS_ABI_MIN_SUPPORTED)
        {
            (void)mod->lib.free();
            return ll::makeStringError(
                "'" + mod->getName() + "' was built against levilamina-rs ABI v"
                + std::to_string(modAbi) + ", which is older than the minimum this loader supports (v"
                + std::to_string(LEVI_RS_ABI_MIN_SUPPORTED) + ") — rebuild the mod against a newer levilamina crate"
            );
        }
        if (modAbi != LEVI_RS_ABI_VERSION)
        {
            // Accepted, but note the skew so version-mismatch reports in the
            // wild are easy to spot. The mod runs against a strict superset
            // of the table it was built for.
            if (auto self = ll::mod::NativeMod::current())
            {
                self->getLogger().info(
                    "'{}' was built against ABI v{}; loader provides v{} (additive superset) — loading",
                    mod->getName(),
                    modAbi,
                    LEVI_RS_ABI_VERSION
                );
            }
        }

        // Wire Mod lifecycle callbacks to the Rust vtable. ModManager's default
        // enable()/disable() invoke these (see ll/api/mod/ModManager.cpp).
        mod->onEnable([](ll::mod::Mod& self)
        {
            auto& rust = static_cast<RustMod&>(self);
            rust.commandsMuted = false;
            auto* fn = rust.vtable.on_enable;
            return fn ? fn(rust.vtable.instance) : true;
        });
        mod->onDisable([](ll::mod::Mod& self)
        {
            auto& rust = static_cast<RustMod&>(self);
            const bool ok = rust.vtable.on_disable ? rust.vtable.on_disable(rust.vtable.instance) : true;
            rust.commandsMuted = true;
            return ok;
        });

        addMod(mod->getName(), mod);
        return {};
    }

    ll::Expected<> RustModManager::unload(std::string_view name)
    {
        const auto mod = std::static_pointer_cast<RustMod>(getMod(name));
        if (!mod)
        {
            return ll::makeStringError("mod not found");
        }
        if (mod->vtable.on_unload && !mod->vtable.on_unload(mod->vtable.instance))
        {
            return ll::makeStringError("'" + std::string(name) + "' refused to unload");
        }
        mod->commandsMuted = true;
        mod->listeners.clear(); // detach all event listeners before the dylib goes away
        detail::onRustModGone(mod.get());
        if (const auto e = mod->lib.free(); e)
        {
            return ll::makeExceptionError(std::make_exception_ptr(*e));
        }
        eraseMod(name);
        return {};
    }

    /* ───────────────────── runtime mod control ───────────────────── */

    ll::Expected<> RustModManager::controlLoad(Manifest manifest)
    {
        const std::string name = manifest.name;
        if (auto e = load(std::move(manifest)); !e)
        {
            return e;
        }
        // LeviLamina's own flow is load → enable; ModManager::load only brings
        // the dylib up and runs levi_rs_main. Without this the mod sits loaded
        // but disabled, its commands muted and on_enable never fired.
        if (auto e = enable(name); !e)
        {
            // Roll back rather than leaving a half-live mod behind: a loaded
            // but never-enabled mod still owns its dylib and its listeners.
            (void)unload(name);
            return e;
        }
        return {};
    }

    ll::Expected<> RustModManager::controlUnload(std::string_view name)
    {
        const auto mod = std::static_pointer_cast<RustMod>(getMod(name));
        if (!mod)
        {
            return ll::makeStringError("'" + std::string(name) + "' is not loaded");
        }
        // disable() first so on_disable actually runs; unload() alone only
        // calls on_unload and the mod would never see its disable stage.
        if (mod->isEnabled())
        {
            if (auto e = disable(name); !e)
            {
                return e;
            }
        }
        return unload(name);
    }

    void const* RustModManager::moduleBase(std::string_view name) const
    {
        const auto mod = std::static_pointer_cast<RustMod>(getMod(name));
        if (!mod) return nullptr;
        return mod->lib.handle(); // HandleT is void*; qualification conversion only
    }

    std::vector<std::string> RustModManager::loadedNames() const
    {
        std::vector<std::string> out;
        for (auto& mod : mods())
        {
            out.push_back(mod.getName());
        }
        return out;
    }
} // namespace levi_rs
