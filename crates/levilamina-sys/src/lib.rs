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

/// 目标标记，放在每个 `abi_version` 的最高位。
///
/// `LeviRsApi` 中段有两个互斥的条件块（客户端构建的 `client_*`、服务端构建的
/// `md_*`），它们后面那条 common additive tail 的偏移因此随目标而变。客户端
/// 构建的 mod 的表比服务端 loader 少一槽，于是光看 `struct_size` 这个配对会被
/// 放行，而尾部的每一次调用都错开一槽，没有任何诊断。
///
/// 两侧在做任何数值比较之前先比这个标记是否完全相等。服务端构建它是 0，所以
/// 现存的服务端 mod 不受影响。
pub const LEVI_RS_ABI_TARGET_MASK: u32 = 0x8000_0000;

#[cfg(feature = "client")]
pub const LEVI_RS_ABI_TARGET_TAG: u32 = LEVI_RS_ABI_TARGET_MASK;
#[cfg(not(feature = "client"))]
pub const LEVI_RS_ABI_TARGET_TAG: u32 = 0;

/// 两侧填进 `abi_version` 的值：版本号 OR 上标记。
pub const LEVI_RS_ABI_TAGGED_VERSION: u32 = LEVI_RS_ABI_VERSION | LEVI_RS_ABI_TARGET_TAG;

/// Byte size of the smallest `LeviRsApi` a loader may present and still be
/// usable at all: the header plus every ABI v1 slot, ending at
/// `register_command`.
///
/// This is the ONLY struct-size floor the handshake enforces. Everything
/// appended after v1 is checked per slot instead, at the point of use --
/// see `levilamina::require_slot!`. Refusing a whole loader because its
/// table is shorter than `size_of::<LeviRsApi>()` would reject loaders that
/// have every slot the mod actually calls.
///
/// Derived rather than hard-coded so it cannot drift: whatever the compiler
/// says the offset of the last v1 field is, plus that field's size.
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
