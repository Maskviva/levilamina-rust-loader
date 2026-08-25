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

    /// 定义 `Table` 的那个 crate 的版本，几乎总是写
    /// `env!("CARGO_PKG_VERSION")`。
    ///
    /// 为什么不能省：指纹里代表「表的类型」的那一项是
    /// `core::any::type_name::<Table>()`，而 `type_name` **明确不保证唯一**。
    /// 契约 crate 从 1.x 升到 2.x 把 `Table` 改了，两边的 `type_name` 仍然
    /// 是同一个字符串 —— NAME 相同、VERSION 如果作者忘了改也相同、
    /// size/align 只要碰巧一样就都一样。于是指纹判定「匹配」，两个布局不同
    /// 的表被直接对接。
    ///
    /// `VERSION` 是人工纪律，漏改就是静默 UB；这一项由 `env!` 自动填，漏不掉。
    const CRATE_VERSION: &'static str;

    type Table: Copy + 'static;
}

/// 类型身份的近似值。
///
/// `core::any::type_name` 的文档明确说了输出**不保证唯一、不保证稳定**，
/// 所以这一项只是指纹的一部分而不是全部：它和 size / align /
/// `LaneContract::VERSION` / `LaneContract::CRATE_VERSION` 一起用，靠的是
/// 「同时撞上」的概率而不是任何一项自身的保证。
///
/// 特别地，它区分不出同名不同版本的类型 —— 那正是 `CRATE_VERSION` 存在的
/// 理由。
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

    /// 空串与非法 UTF-8 都会得到 `""`，两者无法区分。热路径用它，出错要诊断
    /// 用 [`LaneStr::try_as_str`]。
    pub unsafe fn as_str<'a>(self) -> &'a str {
        self.try_as_str().unwrap_or("")
    }

    /// 非法 UTF-8 时返回 `None` 而不是空串。
    ///
    /// 原来只有 `unwrap_or("")` 一种行为：跨 dylib 传来的字节坏掉时静默变成
    /// 空串，和「对方本来就传了空串」完全分不开。这条车道存在的理由就是类型
    /// 信息不丢，静默降级是它最不该有的失败方式。
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
    /// 让 `Publication` 变成 `!Send` / `!Sync`。
    ///
    /// 结构体本身只有一个 `u64`，所以默认是 `Send + Sync` —— 于是它可以被搬
    /// 进 tokio 任务、在工作线程上 drop，而 `Drop` 会调 `lane_unpublish`：那
    /// 个函数拿 loader 的全局锁、遍历租约表、**跨 dylib 调提供方的
    /// `release`**，全都是服务器线程专属的动作。
    ///
    /// `Lane<C>` 因为含裸指针天然就是 `!Send`，`Publication` 只是漏了。
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
        unsafe { (rt.api.lane_unpublish)(rt.handle, self.id) };
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
    let id = unsafe { (rt.api.lane_publish)(rt.handle, s(C::NAME), &desc) };
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
    /// 调用中计数，loader 拥有、永不释放。老 loader 不填，为空指针。
    pub(crate) busy: *const AtomicU32,
    pub(crate) fingerprint: u64,
    pub(crate) _p: PhantomData<(*const C, C)>,
}

/// 在提供方的表项里停留期间把 busy +1，无论怎么离开都 -1（含 panic 展开）。
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

        // Acquire，不是 Relaxed。写端是
        // `flag.store(0, std::memory_order_release)`（Lane.cpp:retireLane），
        // release store 配 relaxed load **不构成 synchronizes-with** —— 读到
        // 1 的时候，对 vtable / data 的写入没有可见性保证。x86 上基本观察不
        // 到，但这是一格跨 dylib 共享的原子量，不该靠平台强序活着。
        unsafe { (*self.alive).load(Ordering::Acquire) != 0 }
    }

    pub fn fingerprint(&self) -> u64 {
        self.fingerprint
    }

    /// 借出提供方的函数表跑一段代码。提供方已经走了就返回 `None`。
    ///
    /// 单靠 `is_alive()` 是不够的：它只能证明「检查的那一刻提供方还在」，管不
    /// 到检查与调用之间。全部服务器线程调用挡住了**并发**卸载，挡不住**重入**
    /// 卸载 —— `f` 里的提供方代码触发一次命令派发，那条命令把提供方卸了，
    /// `FreeLibrary` 就发生在一个仍然停在提供方代码里的栈帧下面。
    ///
    /// 所以进入之前把 loader 那格 busy 计数 +1，离开时 -1（`BusyGuard` 保证
    /// panic 展开也会减）。`RustModManager::unload` 在最前面读它，非 0 就拒绝
    /// 卸载并说明是哪条车道 —— 报一条错，而不是先卸再崩。
    pub fn with<R>(&self, f: impl FnOnce(&C::Table, LaneData) -> R) -> Option<R> {
        if !self.is_alive() || self.table.is_null() {
            return None;
        }

        let _busy = BusyGuard::enter(self.busy);

        // 二次确认。上面那次读之后、计数生效之前仍有一个窗口；先占住计数再复
        // 查，才能保证「读到还活着」和「卸载被挡下」这两件事有交叠。
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

        unsafe { (rt.api.lane_release)(rt.handle, self.lease) };
    }
}

pub fn acquire<C: LaneContract>() -> std::result::Result<Lane<C>, LaneError> {
    let ours = fingerprint::<C>();
    let mut out = sys::LeviRsLaneRef::default();
    let rt = rt();
    let code = unsafe { (rt.api.lane_acquire)(rt.handle, s(C::NAME), ours, &mut out) };
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
