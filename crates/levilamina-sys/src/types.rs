use core::ffi::c_void;

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

#[repr(C)]
#[derive(Clone, Copy)]
pub struct LeviRsPlayerPos {
    pub x: f64,
    pub y: f64,
    pub z: f64,
    pub dimension: i32,
    pub found: bool,
}

pub type LeviRsBlockSink = unsafe extern "C" fn(
    ctx: *mut c_void,
    x: i32,
    y: i32,
    z: i32,
    name: LeviRsStr,
    snbt: LeviRsStr,
);

pub type LeviRsEntitySink = unsafe extern "C" fn(
    ctx: *mut c_void,
    x: i32,
    y: i32,
    z: i32,
    kind: LeviRsStr,
    snbt: LeviRsStr,
);

#[repr(C)]
#[derive(Clone, Copy)]
pub struct LeviRsPlayerSel {
    pub kind: i32,
    pub value: LeviRsStr,
}

pub type LeviRsActorId = i64;

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

pub type LeviRsBytesSink = unsafe extern "C" fn(ctx: *mut c_void, data: *const u8, len: usize);

pub type LeviRsKvSink = unsafe extern "C" fn(ctx: *mut c_void, key: LeviRsStr, value: LeviRsStr);

pub type LeviRsActorSink =
    unsafe extern "C" fn(ctx: *mut c_void, id: LeviRsActorId, type_name: LeviRsStr);

pub type LeviRsFormResultCb = unsafe extern "C" fn(user: *mut c_void, result_snbt: LeviRsStr);

pub type LeviRsBusCb =
    unsafe extern "C" fn(user: *mut c_void, topic: LeviRsStr, payload: LeviRsStr) -> bool;

pub type LeviRsServiceCb = unsafe extern "C" fn(
    user: *mut c_void,
    name: LeviRsStr,
    request: LeviRsStr,
    ctx: *mut c_void,
    reply: LeviRsStrSink,
) -> bool;

pub const LEVI_RS_SERVICE_OK: i32 = 0;

pub const LEVI_RS_SERVICE_NOT_FOUND: i32 = 1;

pub const LEVI_RS_SERVICE_ERROR: i32 = 2;

pub const LEVI_RS_SERVICE_REFUSED: i32 = 3;

pub type LeviRsKvDbHandle = *mut c_void;

pub const LEVI_RS_PKT_INBOUND: i32 = 0;
pub const LEVI_RS_PKT_OUTBOUND: i32 = 1;

pub const LEVI_RS_PKT_MASK_INBOUND: i32 = 1 << LEVI_RS_PKT_INBOUND;
pub const LEVI_RS_PKT_MASK_OUTBOUND: i32 = 1 << LEVI_RS_PKT_OUTBOUND;

pub const LEVI_RS_PKT_PASS: i32 = 0;
pub const LEVI_RS_PKT_REPLACE: i32 = 1;
pub const LEVI_RS_PKT_DROP: i32 = 2;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct LeviRsPacketEvent {
    pub struct_size: u32,
    pub direction: i32,

    pub conn_id: u64,

    pub address: LeviRsStr,
    pub packet_id: i32,
    pub sender_sub_id: u8,
    pub target_sub_id: u8,

    pub body: *const u8,
    pub body_len: usize,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct LeviRsPacketEdit {
    pub struct_size: u32,
    pub packet_id: i32,
    pub sender_sub_id: u8,
    pub target_sub_id: u8,
}

pub type LeviRsPacketHookHandle = *mut c_void;

pub type LeviRsPacketCb = unsafe extern "C" fn(
    user: *mut c_void,
    ev: *const LeviRsPacketEvent,
    edit: *mut LeviRsPacketEdit,
    replace_ctx: *mut c_void,
    replace: LeviRsBytesSink,
) -> i32;

pub type LeviRsConnCb =
    unsafe extern "C" fn(user: *mut c_void, conn_id: u64, address: LeviRsStr, opened: bool);

#[cfg(feature = "client")]
pub type LeviRsKeyHandle = *mut c_void;

#[cfg(feature = "client")]
pub type LeviRsKeyAction = i32;

#[cfg(feature = "client")]
pub type LeviRsFocusImpact = i32;

#[cfg(feature = "client")]
pub type LeviRsKeyCb =
    unsafe extern "C" fn(user: *mut c_void, action: LeviRsKeyAction, impact: LeviRsFocusImpact);

pub const LEVI_RS_LANE_OK: i32 = 0;

pub const LEVI_RS_LANE_NOT_FOUND: i32 = 1;

pub const LEVI_RS_LANE_FINGERPRINT: i32 = 2;

pub const LEVI_RS_LANE_REFUSED: i32 = 3;

pub const LEVI_RS_LANE_PROTOCOL: u32 = 1;

pub type LeviRsLaneRefFn = unsafe extern "C" fn(data: *mut c_void);

#[repr(C)]
#[derive(Clone, Copy)]
pub struct LeviRsLaneDesc {
    pub struct_size: u32,
    pub protocol: u32,
    pub fingerprint: u64,
    pub data: *mut c_void,
    pub vtable: *const c_void,
    pub retain: Option<LeviRsLaneRefFn>,
    pub release: Option<LeviRsLaneRefFn>,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct LeviRsLaneRef {
    pub struct_size: u32,
    pub lease: u64,
    pub fingerprint: u64,
    pub data: *mut c_void,
    pub vtable: *const c_void,

    pub alive: *const u32,
}

impl Default for LeviRsLaneRef {
    fn default() -> Self {
        LeviRsLaneRef {
            struct_size: size_of::<LeviRsLaneRef>() as u32,
            lease: 0,
            fingerprint: 0,
            data: core::ptr::null_mut(),
            vtable: core::ptr::null(),
            alive: core::ptr::null(),
        }
    }
}
