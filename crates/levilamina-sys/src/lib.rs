//! Raw FFI declarations mirroring `src/LeviRsAbi.h` (ABI v5).
//! Keep in lockstep with the C header: fields are append-only, never reordered.

#![no_std]
#![allow(non_camel_case_types)]

#[cfg(all(feature = "server", feature = "client"))]
compile_error!(
    "The `server` and `client` features are mutually exclusive. \
     Enable exactly one of them."
);
#[cfg(not(any(feature = "server", feature = "client")))]
compile_error!(
    "You must enable exactly one of the `server` or `client` features \
     (default is `server`)."
);
// more_dimensions appends FFI fields to LeviRsApi (LEVI_RS_FEATURE_MORE_DIMENSIONS).
// The C++ loader only defines that macro for server builds (xmake.lua:
// `more_dims = ... and (not is_client)`), so enabling it with `client` would
// add Rust-side fields absent from the C++ struct — a struct_size/ABI mismatch.
#[cfg(all(feature = "client", feature = "more_dimensions"))]
compile_error!(
    "The `more_dimensions` feature is server-only; it cannot be combined with `client`."
);

/// ABI version. Compatibility is a *range*: loader accepts mod version in
/// [LEVI_RS_ABI_MIN_SUPPORTED, loader_version]; mod accepts loader with
/// version >= this and struct_size >= expected. See `__init_runtime`.
pub const LEVI_RS_ABI_VERSION: u32 = 5;
pub const LEVI_RS_MAIN_SYMBOL: &str = "levi_rs_main";

pub mod api;
pub mod consts;
pub mod money;
pub mod types;
pub mod vtable;

pub use api::LeviRsApi;
pub use consts::*;
pub use money::{LLMoneyCallback, LLMoneyEvent};
pub use types::*;
pub use vtable::{LeviRsMainFn, LeviRsModVTable};
