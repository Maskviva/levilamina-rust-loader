//! Core FFI types: string view, handles, callbacks, sinks, selector structs.
//! Mirrors the corresponding typedefs in `LeviRsAbi.h`.

use core::ffi::c_void;

/// UTF-8 string view. Not guaranteed NUL-terminated.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct LeviRsStr {
    pub ptr: *const u8,
    pub len: usize,
}

pub type LeviRsModHandle = *mut c_void;
pub type LeviRsListenerHandle = *mut c_void;

pub type LeviRsTaskCb = unsafe extern "C" fn(user: *mut c_void);
pub type LeviRsStrSink = unsafe extern "C" fn(ctx: *mut c_void, s: LeviRsStr);

pub type LeviRsEventCb = unsafe extern "C" fn(
    user: *mut c_void,
    event_id: LeviRsStr,
    snbt: LeviRsStr,
    write_ctx: *mut c_void,
    write_back: LeviRsStrSink,
);

pub type LeviRsCommandCb = unsafe extern "C" fn(
    user: *mut c_void,
    args: LeviRsStr,
    origin_name: LeviRsStr,
    out_ctx: *mut c_void,
    out_success: LeviRsStrSink,
    out_error: LeviRsStrSink,
);

pub type LeviRsCmdOutputSink =
    unsafe extern "C" fn(ctx: *mut c_void, success: bool, output: LeviRsStr);

/// A player's feet position + dimension. Mirrors `LeviRsPlayerPos`.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct LeviRsPlayerPos {
    pub x: f64,
    pub y: f64,
    pub z: f64,
    pub dimension: i32,
    pub found: bool,
}

/// Block sink: one call per cell during scan_region. snbt = full block serialization.
pub type LeviRsBlockSink = unsafe extern "C" fn(
    ctx: *mut c_void,
    x: i32,
    y: i32,
    z: i32,
    name: LeviRsStr,
    snbt: LeviRsStr,
);

/// Entity sink: one call per entity found. x,y,z = the containing block cell.
pub type LeviRsEntitySink = unsafe extern "C" fn(
    ctx: *mut c_void,
    x: i32,
    y: i32,
    z: i32,
    kind: LeviRsStr,
    snbt: LeviRsStr,
);

/// Player selector: kind 0 = name, 1 = xuid, 2 = uuid. Mirrors `LeviRsPlayerSel`.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct LeviRsPlayerSel {
    pub kind: i32,
    pub value: LeviRsStr,
}

/// ActorUniqueID raw value. 0 never resolves.
pub type LeviRsActorId = i64;

/// Container reference: which 0=inventory 1=ender_chest 2=armor 3=offhand 4=block.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct LeviRsContainerRef {
    pub which: i32,
    pub player: LeviRsPlayerSel,
    pub dim: i32,
    pub x: i32,
    pub y: i32,
    pub z: i32,
}

/// Raw byte sink (binary NBT). Bytes valid only within the call frame.
pub type LeviRsBytesSink = unsafe extern "C" fn(ctx: *mut c_void, data: *const u8, len: usize);
/// Key/value sink (kvdb_iter).
pub type LeviRsKvSink = unsafe extern "C" fn(ctx: *mut c_void, key: LeviRsStr, value: LeviRsStr);
/// Actor sink (list_actors).
pub type LeviRsActorSink =
    unsafe extern "C" fn(ctx: *mut c_void, id: LeviRsActorId, type_name: LeviRsStr);
/// Form result callback: fires once, on the server thread, with result SNBT.
pub type LeviRsFormResultCb = unsafe extern "C" fn(user: *mut c_void, result_snbt: LeviRsStr);

/// Opaque handle to an open key-value database owned by the loader.
pub type LeviRsKvDbHandle = *mut c_void;

// Client-only types (client feature).

/// Drop via `client_unregister_key`.
#[cfg(feature = "client")]
pub type LeviRsKeyHandle = *mut c_void;

/// 0 = released (up), 1 = pressed (down).
#[cfg(feature = "client")]
pub type LeviRsKeyAction = i32;

/// Mirrors `::FocusImpact`: 0=None, 1=Priority, 2=Always.
#[cfg(feature = "client")]
pub type LeviRsFocusImpact = i32;

/// Runs on the client thread.
#[cfg(feature = "client")]
pub type LeviRsKeyCb =
    unsafe extern "C" fn(user: *mut c_void, action: LeviRsKeyAction, impact: LeviRsFocusImpact);
