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

/// 跨 mod 事件总线的订阅回调。
///
/// `topic` / `payload` 只在调用期间有效，要留就自己拷。返回值是**否决位**，
/// 且只对 `bus_publish_vetoable` 有意义：`true` = 拒绝，`false` = 没意见。
/// 没有「把拒绝翻回同意」的路径 —— 订阅者只能收紧不能放宽，否则谁最后跑谁说了算，
/// 而订阅顺序不是任何一方控制得了的。
pub type LeviRsBusCb =
    unsafe extern "C" fn(user: *mut c_void, topic: LeviRsStr, payload: LeviRsStr) -> bool;

/// 跨 mod **服务注册**的提供方回调（查询式调用，和总线的单向广播是两回事）。
///
/// 写一次 `reply(ctx, ..)` 并返回 `true`；返回 `false` 表示失败，先写进去的东西
/// 会作为**错误文本**交给调用方 —— 这正是「没有这块地皮」和「数据库挂了」能在
/// 调用点被分开的原因。
///
/// `name` / `request` 只在调用期间有效，要留就自己拷。同步执行在**调用方线程**上。
pub type LeviRsServiceCb = unsafe extern "C" fn(
    user: *mut c_void,
    name: LeviRsStr,
    request: LeviRsStr,
    ctx: *mut c_void,
    reply: LeviRsStrSink,
) -> bool;

/// `service_call` 的返回码。和 `LeviRsAbi.h` 的 `LEVI_RS_SERVICE_*` 逐值对应。
pub const LEVI_RS_SERVICE_OK: i32 = 0;
/// 没有人提供这个名字（或者提供方已卸载 / 被停用）。
pub const LEVI_RS_SERVICE_NOT_FOUND: i32 = 1;
/// 提供方返回了 false，应答里是它的错误信息。
pub const LEVI_RS_SERVICE_ERROR: i32 = 2;
/// 名字非法、调用了自己的服务、或者撞上调用深度上限。
pub const LEVI_RS_SERVICE_REFUSED: i32 = 3;

/// Opaque handle to an open key-value database owned by the loader.
pub type LeviRsKvDbHandle = *mut c_void;

// ── Packet interception ───────────────────────────────────────────────
// Mirrors the `LeviRsPacket*` block in `LeviRsAbi.h`.

/// `LeviRsPacketEvent::direction`, and the bit positions used by `dir_mask`.
pub const LEVI_RS_PKT_INBOUND: i32 = 0;
pub const LEVI_RS_PKT_OUTBOUND: i32 = 1;

pub const LEVI_RS_PKT_MASK_INBOUND: i32 = 1 << LEVI_RS_PKT_INBOUND;
pub const LEVI_RS_PKT_MASK_OUTBOUND: i32 = 1 << LEVI_RS_PKT_OUTBOUND;

/// `LeviRsPacketCb` return value. Anything else is treated as PASS.
pub const LEVI_RS_PKT_PASS: i32 = 0;
pub const LEVI_RS_PKT_REPLACE: i32 = 1;
pub const LEVI_RS_PKT_DROP: i32 = 2;

/// One intercepted packet. Every pointer is borrowed for the callback only.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct LeviRsPacketEvent {
    pub struct_size: u32,
    pub direction: i32,
    /// `NetworkIdentifier::getHash()` — stable for the connection's lifetime
    /// and available before a `Player` exists.
    pub conn_id: u64,
    /// "host:port".
    pub address: LeviRsStr,
    pub packet_id: i32,
    pub sender_sub_id: u8,
    pub target_sub_id: u8,
    /// Packet body, header excluded. Null only when `body_len` is 0.
    pub body: *const u8,
    pub body_len: usize,
}

/// Mutable header fields, pre-filled from the event. Only applied on REPLACE.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct LeviRsPacketEdit {
    pub struct_size: u32,
    pub packet_id: i32,
    pub sender_sub_id: u8,
    pub target_sub_id: u8,
}

/// Drop via `packet_hook_unregister` / `packet_conn_hook_unregister`.
pub type LeviRsPacketHookHandle = *mut c_void;

/// Packet interceptor. Call `replace(replace_ctx, ptr, len)` with the new
/// BODY and return `LEVI_RS_PKT_REPLACE` to rewrite.
pub type LeviRsPacketCb = unsafe extern "C" fn(
    user: *mut c_void,
    ev: *const LeviRsPacketEvent,
    edit: *mut LeviRsPacketEdit,
    replace_ctx: *mut c_void,
    replace: LeviRsBytesSink,
) -> i32;

/// Connection lifecycle: `opened` is true on accept, false on close.
pub type LeviRsConnCb =
    unsafe extern "C" fn(user: *mut c_void, conn_id: u64, address: LeviRsStr, opened: bool);

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
