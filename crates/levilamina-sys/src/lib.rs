//! Raw FFI declarations mirroring `bridge/src/LeviRsAbi.h` (ABI v1).
//!
//! This crate contains no logic — only `#[repr(C)]` types. Keep it in
//! lockstep with the C header: fields are append-only, never reordered.
//!
//! You almost certainly want the safe `levilamina` crate instead.

#![no_std]
#![allow(non_camel_case_types)]

use core::ffi::c_void;

pub const LEVI_RS_ABI_VERSION: u32 = 1;
pub const LEVI_RS_MAIN_SYMBOL: &str = "levi_rs_main";

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

/// Function table handed to the Rust mod. Mirrors `LeviRsApi`.
#[repr(C)]
pub struct LeviRsApi {
    pub abi_version: u32,
    pub struct_size: u32,

    /// level: -1=Off, 0=Fatal, 1=Error, 2=Warn, 3=Info, 4=Debug, 5=Trace. Thread-safe.
    pub log: unsafe extern "C" fn(mod_: LeviRsModHandle, level: i32, msg: LeviRsStr),
    /// 0=Default, 1=Starting, 2=Running, 3=Stopping. Thread-safe.
    pub gaming_status: unsafe extern "C" fn() -> i32,
    /// Queue onto the server thread ASAP. Thread-safe.
    pub schedule: unsafe extern "C" fn(cb: LeviRsTaskCb, user: *mut c_void),
    /// Queue onto the server thread after `delay_ms`. Thread-safe.
    pub schedule_after: unsafe extern "C" fn(cb: LeviRsTaskCb, user: *mut c_void, delay_ms: u64),

    /// Server thread only. priority 0..4 (Highest..Lowest), 2 = Normal.
    pub subscribe_event: unsafe extern "C" fn(
        mod_: LeviRsModHandle,
        event_id: LeviRsStr,
        priority: i32,
        cb: LeviRsEventCb,
        user: *mut c_void,
    ) -> LeviRsListenerHandle,
    /// Server thread only.
    pub unsubscribe_event:
        unsafe extern "C" fn(mod_: LeviRsModHandle, listener: LeviRsListenerHandle) -> bool,
    /// Server thread only.
    pub list_events: unsafe extern "C" fn(ctx: *mut c_void, sink: LeviRsStrSink),

    /// Server thread only. Executes as console (Owner).
    pub execute_command:
        unsafe extern "C" fn(cmd: LeviRsStr, ctx: *mut c_void, sink: LeviRsCmdOutputSink) -> bool,
    /// Server thread only, call during on_enable. permission 0..4.
    pub register_command: unsafe extern "C" fn(
        mod_: LeviRsModHandle,
        name: LeviRsStr,
        description: LeviRsStr,
        permission: i32,
        cb: LeviRsCommandCb,
        user: *mut c_void,
    ) -> bool,
    // ABI v2+: append new fields here only.
}

/// Filled in by the Rust mod inside `levi_rs_main`. Mirrors `LeviRsModVTable`.
#[repr(C)]
pub struct LeviRsModVTable {
    pub abi_version: u32,
    pub instance: *mut c_void,
    pub on_enable: Option<unsafe extern "C" fn(instance: *mut c_void) -> bool>,
    pub on_disable: Option<unsafe extern "C" fn(instance: *mut c_void) -> bool>,
    pub on_unload: Option<unsafe extern "C" fn(instance: *mut c_void) -> bool>,
}
