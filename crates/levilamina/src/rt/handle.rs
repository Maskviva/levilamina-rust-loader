use core::ffi::c_void;
use core::sync::atomic::{AtomicPtr, Ordering};

/// S1：原来 Runtime / KvDb / PacketHook 各写一对 `unsafe impl` 断言 Send 与 Sync
/// 来让 `*mut c_void` 过编译。这里换成 `AtomicPtr`：它只声明「指针**值**可以
/// 跨线程搬运」，不替被指对象担保线程安全 —— 那一层由 loader 的 api_* 入口
/// 自己负责（它们本就允许任意线程调用，见审计 W13），而 Rust 侧每一次真正
/// 使用句柄的地方仍然是显式的 `unsafe { (api.xxx)(handle.get(), ..) }`。
#[repr(transparent)]
pub(crate) struct Handle(AtomicPtr<c_void>);

impl Handle {
    pub(crate) const fn new(p: *mut c_void) -> Handle {
        Handle(AtomicPtr::new(p))
    }

    pub(crate) fn get(&self) -> *mut c_void {
        self.0.load(Ordering::Relaxed)
    }

    pub(crate) fn is_null(&self) -> bool {
        self.get().is_null()
    }
}
