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

#[cfg(all(feature = "client", feature = "more_dimensions"))]
compile_error!(
    "The `more_dimensions` feature is server-only; it cannot be combined with `client`."
);

pub const LEVI_RS_ABI_VERSION: u32 = 5;
pub const LEVI_RS_MAIN_SYMBOL: &str = "levi_rs_main";

pub const LEVI_RS_ABI_TARGET_MASK: u32 = 0x8000_0000;

#[cfg(feature = "client")]
pub const LEVI_RS_ABI_TARGET_TAG: u32 = LEVI_RS_ABI_TARGET_MASK;
#[cfg(not(feature = "client"))]
pub const LEVI_RS_ABI_TARGET_TAG: u32 = 0;

pub const LEVI_RS_ABI_TAGGED_VERSION: u32 = LEVI_RS_ABI_VERSION | LEVI_RS_ABI_TARGET_TAG;

pub const LEVI_RS_API_CORE_SIZE: usize =
    core::mem::offset_of!(LeviRsApi, register_command) + core::mem::size_of::<usize>();

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
