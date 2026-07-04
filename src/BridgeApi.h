#pragma once

#include "LeviRsAbi.h"

namespace levi_rs {

class RustMod;

/** The singleton function table handed to every Rust mod. */
const LeviRsApi* getBridgeApi();

namespace detail {
/** Invalidate command bindings that point at a mod being unloaded. */
void onRustModGone(RustMod* mod);
} // namespace detail

} // namespace levi_rs
