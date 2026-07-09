#include "RustModManager.h"

#include <filesystem>
#include <memory>
#include <string>
#include <utility>

#include "ll/api/utils/StringUtils.h"

#include "BridgeApi.h"
#include "RustMod.h"

namespace levi_rs
{
    using ll::mod::Manifest;

    RustModManager::RustModManager() : ModManager(RustModManagerName)
    {
    }

    RustModManager::~RustModManager() = default;

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
        if (mod->vtable.abi_version != LEVI_RS_ABI_VERSION)
        {
            (void)mod->lib.free();
            return ll::makeStringError(
                "'" + mod->getName() + "' was built against levilamina-rs ABI v"
                + std::to_string(mod->vtable.abi_version) + ", loader speaks v" + std::to_string(LEVI_RS_ABI_VERSION)
            );
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
} // namespace levi_rs
