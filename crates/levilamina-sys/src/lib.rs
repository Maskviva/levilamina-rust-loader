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
