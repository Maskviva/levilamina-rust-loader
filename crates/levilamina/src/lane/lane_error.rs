use core::ffi::c_void;
use core::marker::PhantomData;
use core::sync::atomic::{AtomicU32, Ordering};
use std::fmt;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::Arc;

use crate::error::{Error, Result};
use crate::ffi::s;
use crate::logger::Logger;
use crate::{rt, sys};

pub const PROTOCOL: u32 = sys::LEVI_RS_LANE_PROTOCOL;

pub(crate) const fn parse_u64(sv: &str) -> u64 {
    let b = sv.as_bytes();
    let mut i = 0usize;
    let mut v = 0u64;
    while i < b.len() {
        v = v.wrapping_mul(10).wrapping_add((b[i] - b'0') as u64);
        i += 1;
    }
    v
}

pub const TOOLCHAIN: u64 = parse_u64(env!("LEVI_RS_TOOLCHAIN_FP"));

pub(crate) const FNV_OFFSET: u64 = 0xcbf2_9ce4_8422_2325;
pub(crate) const FNV_PRIME: u64 = 0x0000_0100_0000_01b3;

pub(crate) const fn mix_bytes(mut h: u64, bytes: &[u8]) -> u64 {
    let mut i = 0usize;
    while i < bytes.len() {
        h ^= bytes[i] as u64;
        h = h.wrapping_mul(FNV_PRIME);
        i += 1;
    }
    h
}

pub(crate) const fn mix_u64(h: u64, v: u64) -> u64 {
    mix_bytes(h, &v.to_le_bytes())
}

pub trait LaneContract: 'static {
    const NAME: &'static str;

    const VERSION: u32;

    const CRATE_VERSION: &'static str;

    type Table: Copy + 'static;
}

pub(crate) fn type_ident_hash<T: 'static>() -> u64 {
    mix_bytes(FNV_OFFSET, core::any::type_name::<T>().as_bytes())
}

pub fn fingerprint<C: LaneContract>() -> u64 {
    let mut h = FNV_OFFSET;
    h = mix_u64(h, TOOLCHAIN);
    h = mix_u64(h, PROTOCOL as u64);
    h = mix_bytes(h, C::NAME.as_bytes());
    h = mix_u64(h, C::VERSION as u64);
    h = mix_u64(h, size_of::<C::Table>() as u64);
    h = mix_u64(h, align_of::<C::Table>() as u64);
    h = mix_u64(h, type_ident_hash::<C::Table>());
    h = mix_bytes(h, C::CRATE_VERSION.as_bytes());

    if h == 0 {
        1
    } else {
        h
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct LaneStr {
    pub ptr: *const u8,
    pub len: usize,
}

impl LaneStr {
    pub const EMPTY: LaneStr = LaneStr {
        ptr: core::ptr::NonNull::<u8>::dangling().as_ptr(),
        len: 0,
    };

    pub fn new(v: &str) -> LaneStr {
        LaneStr {
            ptr: v.as_ptr(),
            len: v.len(),
        }
    }

    /// Null pointers and invalid UTF-8 both degrade into empty strings.
    /// To distinguish between "none" and "empty", use [`LaneStr::try_as_str`].
    ///
    /// # Safety
    ///
    //Follow the same set of requirements as [`LaneStr::try_as_str`],
    // see the list over there.
    pub unsafe fn as_str<'a>(self) -> &'a str {
        self.try_as_str().unwrap_or("")
    }

    /// Returns `None` if the pointer is null, `Some("")` if `len == 0`, and `None` if the content is not valid UTF-8.
    ///
    /// # Safety
    ///
    /// `'a` is chosen by the caller (unbounded lifetime) — it has no connection to the raw pointer inside `self`, and the compiler won't check your borrow for you. The caller must ensure:
    ///
    /// - If `ptr` is non-null and `len != 0`, then `ptr` points to readable memory of at least `len` bytes for the entire `'a` period;
    /// - This memory is not modified or freed by anyone during `'a`;
    /// - `len` does not exceed `isize::MAX`.
    ///
    /// In practice: strings received across lane boundaries **are only valid for this call**.
    /// The provider can recycle them at any time. To keep them, use `to_owned()`,
    /// and don’t store the borrow in a struct or pass it to another thread.
    pub unsafe fn try_as_str<'a>(self) -> Option<&'a str> {
        if self.ptr.is_null() {
            return None;
        }
        if self.len == 0 {
            return Some("");
        }
        core::str::from_utf8(core::slice::from_raw_parts(self.ptr, self.len)).ok()
    }
}

impl<'a> From<&'a str> for LaneStr {
    fn from(v: &'a str) -> LaneStr {
        LaneStr::new(v)
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct LaneSlice<T> {
    pub ptr: *const T,
    pub len: usize,
}

impl<T> LaneSlice<T> {
    pub fn new(v: &[T]) -> LaneSlice<T> {
        LaneSlice {
            ptr: v.as_ptr(),
            len: v.len(),
        }
    }

    /// Both null pointers and `len == 0` return an empty slice.
    ///
    /// # Safety
    ///
    /// `'a` is chosen by the caller (unbounded lifetime) and has no relation to the borrow of `&self` —
    /// the compiler won't check this for you. The caller must ensure:
    ///
    /// - If `ptr` is not null and `len != 0`, then `ptr` must point to `len` consecutive,
    ///   initialized `T`s for the entire `'a` lifetime, aligned according to `T`'s requirements;
    /// - This memory must not be modified or freed by anyone during `'a`;
    /// - `len * size_of::<T>()` does not exceed `isize::MAX`.
    ///
    /// Same as [`LaneStr::try_as_str`]: slices received across lane boundaries are only valid for this call.
    pub unsafe fn as_slice<'a>(&self) -> &'a [T] {
        if self.len == 0 || self.ptr.is_null() {
            return &[];
        }
        core::slice::from_raw_parts(self.ptr, self.len)
    }
}

#[repr(transparent)]
#[derive(Clone, Copy)]
pub struct LaneData(pub *mut c_void);

pub type LaneStrSink = unsafe extern "C" fn(ctx: *mut c_void, item: LaneStr);

pub fn collect_strings(call: impl FnOnce(*mut c_void, LaneStrSink) -> u32) -> Vec<String> {
    let mut out: Vec<String> = Vec::new();
    unsafe extern "C" fn sink(ctx: *mut c_void, item: LaneStr) {
        let v = &mut *ctx.cast::<Vec<String>>();
        v.push(item.as_str().to_owned());
    }
    let n = call((&mut out as *mut Vec<String>).cast(), sink);

    if n as usize != out.len() {
        Logger::get().warn(&format!(
            "lane: 提供方声称 {n} 条，实际收到 {} 条 —— 以实际收到的为准",
            out.len()
        ));
    }
    out
}

pub fn guard<R>(fallback: R, f: impl FnOnce() -> R) -> R {
    match catch_unwind(AssertUnwindSafe(f)) {
        Ok(v) => v,
        Err(_) => {
            Logger::get().error("lane: 提供方的表项 panic 了，返回兜底值");
            fallback
        }
    }
}

pub struct Publication {
    pub(crate) id: u64,
    pub(crate) _not_send: PhantomData<*const ()>,
}

impl Publication {
    pub fn id(&self) -> u64 {
        self.id
    }

    pub fn forget(mut self) {
        self.id = 0;
        core::mem::forget(self);
    }
}

impl Drop for Publication {
    fn drop(&mut self) {
        if self.id == 0 {
            return;
        }
        let rt = rt();
        unsafe { (rt.api.lane_unpublish)(rt.handle(), self.id) };
    }
}

pub fn publish<C: LaneContract, S: Send + Sync + 'static>(
    table: &'static C::Table,
    state: Arc<S>,
) -> Result<Publication> {
    unsafe extern "C" fn retain_fn<S>(data: *mut c_void) {
        Arc::increment_strong_count(data.cast::<S>());
    }
    unsafe extern "C" fn release_fn<S>(data: *mut c_void) {
        Arc::decrement_strong_count(data.cast::<S>());
    }

    let raw = Arc::into_raw(state) as *mut c_void;
    let desc = sys::LeviRsLaneDesc {
        struct_size: size_of::<sys::LeviRsLaneDesc>() as u32,
        protocol: PROTOCOL,
        fingerprint: fingerprint::<C>(),
        data: raw,
        vtable: (table as *const C::Table).cast::<c_void>(),
        retain: Some(retain_fn::<S>),
        release: Some(release_fn::<S>),
    };

    let rt = rt();
    let id = unsafe { (rt.api.lane_publish)(rt.handle(), s(C::NAME), &desc) };
    if id == 0 {
        unsafe { drop(Arc::from_raw(raw.cast::<S>())) };
        return Err(Error(format!(
            "lane: 发布 '{}' 被拒绝（名字被别的 mod 占了、协议版本不符，或者 mod 未启用）——\
             loader 日志里写着占用者是谁",
            C::NAME
        )));
    }
    Ok(Publication {
        id,
        _not_send: PhantomData,
    })
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum LaneError {
    NotFound,

    Fingerprint { theirs: u64, ours: u64 },

    Refused,
    Unknown(i32),
}

impl LaneError {
    pub fn advice(&self) -> String {
        match self {
            LaneError::NotFound => "没有找到这条 Rust 快车道（提供方 mod 没装或未启用）——\
                 走 service 通道，功能不受影响"
                .to_string(),
            LaneError::Fingerprint { theirs, ours } => format!(
                "Rust 快车道指纹不同（对面 0x{theirs:016x} / 本地 0x{ours:016x}），已降级为 \
                 service 通道。\n  · 功能完全不受影响，只是每次调用多一次序列化。\n  \
                 · 原因几乎总是这两个 mod 不是同一套 rustc / target / profile 编出来的。\n  \
                 · 想打开快车道：用同一个工具链、同一个 profile 重编这两个 mod。"
            ),
            LaneError::Refused => {
                "Rust 快车道被拒绝（车道名非法、自己取自己的车道，或者提供方当前被禁用）"
                    .to_string()
            }
            LaneError::Unknown(c) => format!("Rust 快车道返回了未知状态码 {c}"),
        }
    }
}

impl fmt::Display for LaneError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.advice())
    }
}

impl std::error::Error for LaneError {}

impl From<LaneError> for Error {
    fn from(e: LaneError) -> Error {
        Error(e.to_string())
    }
}

pub struct Lane<C: LaneContract> {
    pub(crate) lease: u64,
    pub(crate) data: LaneData,
    pub(crate) table: *const C::Table,
    pub(crate) alive: *const AtomicU32,
    pub(crate) busy: *const AtomicU32,
    pub(crate) fingerprint: u64,
    pub(crate) _p: PhantomData<(*const C, C)>,
}

struct BusyGuard(*const AtomicU32);

impl BusyGuard {
    fn enter(cell: *const AtomicU32) -> BusyGuard {
        if !cell.is_null() {
            unsafe { (*cell).fetch_add(1, Ordering::AcqRel) };
        }
        BusyGuard(cell)
    }
}

impl Drop for BusyGuard {
    fn drop(&mut self) {
        if !self.0.is_null() {
            unsafe { (*self.0).fetch_sub(1, Ordering::AcqRel) };
        }
    }
}

impl<C: LaneContract> Lane<C> {
    pub fn is_alive(&self) -> bool {
        if self.alive.is_null() {
            return false;
        }

        unsafe { (*self.alive).load(Ordering::Acquire) != 0 }
    }

    pub fn fingerprint(&self) -> u64 {
        self.fingerprint
    }

    pub fn with<R>(&self, f: impl FnOnce(&C::Table, LaneData) -> R) -> Option<R> {
        if !self.is_alive() || self.table.is_null() {
            return None;
        }

        let _busy = BusyGuard::enter(self.busy);

        if !self.is_alive() {
            return None;
        }

        let table = unsafe { &*self.table };
        Some(f(table, self.data))
    }
}

impl<C: LaneContract> Drop for Lane<C> {
    fn drop(&mut self) {
        if self.lease == 0 {
            return;
        }
        let rt = rt();

        unsafe { (rt.api.lane_release)(rt.handle(), self.lease) };
    }
}

pub fn acquire<C: LaneContract>() -> std::result::Result<Lane<C>, LaneError> {
    let ours = fingerprint::<C>();
    let mut out = sys::LeviRsLaneRef::default();
    let rt = rt();
    let code = unsafe { (rt.api.lane_acquire)(rt.handle(), s(C::NAME), ours, &mut out) };
    match code {
        sys::LEVI_RS_LANE_OK => Ok(Lane {
            lease: out.lease,
            data: LaneData(out.data),
            table: out.vtable.cast::<C::Table>(),
            alive: out.alive.cast::<AtomicU32>(),
            busy: out.busy.cast::<AtomicU32>(),
            fingerprint: out.fingerprint,
            _p: PhantomData,
        }),
        sys::LEVI_RS_LANE_NOT_FOUND => Err(LaneError::NotFound),
        sys::LEVI_RS_LANE_FINGERPRINT => Err(LaneError::Fingerprint {
            theirs: out.fingerprint,
            ours,
        }),
        sys::LEVI_RS_LANE_REFUSED => Err(LaneError::Refused),
        other => Err(LaneError::Unknown(other)),
    }
}

pub fn list_json() -> String {
    let mut out: Option<String> = None;
    let rt = rt();
    unsafe {
        (rt.api.lane_list)(
            (&mut out as *mut Option<String>).cast(),
            crate::ffi::set_string,
        )
    };
    out.unwrap_or_else(|| "[]".to_string())
}
