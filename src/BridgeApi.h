#pragma once

#include "LeviRsAbi.h"

namespace levi_rs
{
    class RustMod;

    /** The singleton function table handed to every Rust mod. */
    const LeviRsApi* getBridgeApi();

    namespace detail
    {
        /** Release everything owned on behalf of a mod being unloaded:
         * command bindings (nulled), pending form tickets (cleared), and any
         * KvDb handles left open (force-closed with a warning). */
        void onRustModGone(RustMod* mod);
    } // namespace detail
} // namespace levi_rs
