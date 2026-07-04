#pragma once

#include <memory>
#include <vector>

#include "ll/api/event/ListenerBase.h"
#include "ll/api/mod/Manifest.h"
#include "ll/api/mod/Mod.h"
#include "ll/api/utils/SystemUtils.h"

#include "LeviRsAbi.h"

namespace levi_rs {

inline constexpr std::string_view RustModManagerName = "rust";

/**
 * A mod of type "rust": a plain Rust cdylib loaded through the LeviRsAbi
 * function-table contract instead of the C++ native mod ABI.
 */
class RustMod : public ll::mod::Mod, public std::enable_shared_from_this<RustMod> {
public:
    explicit RustMod(ll::mod::Manifest manifest) : Mod(std::move(manifest)) {}

    ll::sys_utils::DynamicLibrary lib;
    LeviRsModVTable               vtable{};
    /** Keeps DynamicListeners alive; cleared on unload. */
    std::vector<std::shared_ptr<ll::event::ListenerBase>> listeners;
    /** Muted when disabled so already-registered commands become no-ops. */
    bool commandsMuted = false;
};

} // namespace levi_rs
