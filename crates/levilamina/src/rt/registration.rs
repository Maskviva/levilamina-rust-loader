use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::Mutex;

use crate::error::{Error, Result};
use crate::logger::Logger;
use crate::runtime::ModContext;
use crate::sys;

pub trait LeviMod: Sized + 'static {
    fn on_load(ctx: &ModContext) -> Result<Self>;
    fn on_enable(&mut self, _ctx: &ModContext) -> Result<()> {
        Ok(())
    }
    fn on_disable(&mut self, _ctx: &ModContext) -> Result<()> {
        Ok(())
    }
    fn on_unload(&mut self, _ctx: &ModContext) -> Result<()> {
        Ok(())
    }
}

#[doc(hidden)]
pub struct ModSlot<T: LeviMod>(pub Mutex<Option<T>>);

unsafe impl<T: LeviMod> Sync for ModSlot<T> {}

#[doc(hidden)]
pub unsafe fn __init_runtime(api: *const sys::LeviRsApi, handle: sys::LeviRsModHandle) -> bool {
    if api.is_null() {
        return false;
    }

    let api: &'static sys::LeviRsApi = &*api;

    // Generation check. `abi_version` moves ONLY on a non-additive change (a
    // field reordered, removed, or re-signed) -- see the rule at the top of
    // LeviRsAbi.h. A loader below our version therefore speaks a table that is
    // genuinely not a prefix of ours, and nothing can be safely called.
    // 目标标记优先于一切数值比较：跨目标的 loader 的表根本不是我们这张表的
    // 前缀（条件块 client_*/md_* 在结构体中段，它们之后的整条尾部都会平移）。
    // 下面的 struct_size 检查抓不到这件事 —— 客户端构建的 mod 比服务端 loader
    // 小一槽，尺寸比较恰好通过，然后每一次尾部调用都错开一槽。
    if (api.abi_version & sys::LEVI_RS_ABI_TARGET_MASK) != sys::LEVI_RS_ABI_TARGET_TAG {
        return false;
    }
    let loader_abi = api.abi_version & !sys::LEVI_RS_ABI_TARGET_MASK;
    if loader_abi < sys::LEVI_RS_ABI_VERSION {
        return false;
    }

    // Core floor, and nothing more.
    //
    // This deliberately does NOT require `struct_size >= size_of::<LeviRsApi>()`.
    // That check compared the size of the struct *definition* against the
    // loader's table, so merely rebuilding a mod against a newer crate made it
    // refuse every older loader -- even when the mod called nothing new. The
    // table is append-only and therefore prefix-compatible, so the honest
    // question is per-slot ("is the function I am about to call present?"),
    // not per-struct. `has_slot` answers it, and `require_slot!` enforces it
    // at each late-added call site.
    //
    // What still has to hold is that the core exists at all: a table that
    // doesn't even reach the v1 slots is not a levilamina table.
    if (api.struct_size as usize) < sys::LEVI_RS_API_CORE_SIZE {
        return false;
    }
    crate::runtime::set_runtime(api, handle)
}

#[doc(hidden)]
pub fn __lifecycle<T: LeviMod>(slot: &'static ModSlot<T>, stage: u8) -> bool {
    let ctx = ModContext::new();
    let run = || -> Result<()> {
        let mut guard = slot
            .0
            .lock()
            .map_err(|_| Error("mod state poisoned".into()))?;
        let Some(instance) = guard.as_mut() else {
            return Err(Error("mod instance missing".into()));
        };
        match stage {
            1 => instance.on_enable(&ctx),
            2 => instance.on_disable(&ctx),
            3 => {
                instance.on_unload(&ctx)?;
                *guard = None;
                Ok(())
            }
            _ => Ok(()),
        }
    };
    match catch_unwind(AssertUnwindSafe(run)) {
        Ok(Ok(())) => true,
        Ok(Err(e)) => {
            Logger::get().error(&format!("lifecycle error: {e}"));
            false
        }
        Err(_) => {
            Logger::get().error("panic in lifecycle hook");
            false
        }
    }
}

#[doc(hidden)]
pub fn __load<T: LeviMod>(slot: &'static ModSlot<T>) -> bool {
    let ctx = ModContext::new();
    match catch_unwind(AssertUnwindSafe(|| T::on_load(&ctx))) {
        Ok(Ok(instance)) => {
            *slot.0.lock().unwrap() = Some(instance);
            true
        }
        Ok(Err(e)) => {
            Logger::get().error(&format!("on_load failed: {e}"));
            false
        }
        Err(_) => {
            Logger::get().error("panic in on_load");
            false
        }
    }
}

#[macro_export]
macro_rules! register_mod {
    ($ty:ty) => {
        #[doc(hidden)]
        static __LEVI_RS_SLOT: $crate::ModSlot<$ty> =
            $crate::ModSlot(::std::sync::Mutex::new(None));

        #[no_mangle]
        pub unsafe extern "C" fn levi_rs_main(
            api: *const $crate::sys::LeviRsApi,
            handle: $crate::sys::LeviRsModHandle,
            out: *mut $crate::sys::LeviRsModVTable,
        ) -> bool {
            if out.is_null() || !$crate::__init_runtime(api, handle) {
                return false;
            }
            if !$crate::__load::<$ty>(&__LEVI_RS_SLOT) {
                return false;
            }
            unsafe extern "C" fn on_enable(_: *mut ::core::ffi::c_void) -> bool {
                $crate::__lifecycle::<$ty>(&__LEVI_RS_SLOT, 1)
            }
            unsafe extern "C" fn on_disable(_: *mut ::core::ffi::c_void) -> bool {
                $crate::__lifecycle::<$ty>(&__LEVI_RS_SLOT, 2)
            }
            unsafe extern "C" fn on_unload(_: *mut ::core::ffi::c_void) -> bool {
                $crate::__lifecycle::<$ty>(&__LEVI_RS_SLOT, 3)
            }
            (*out) = $crate::sys::LeviRsModVTable {
                abi_version: $crate::sys::LEVI_RS_ABI_TAGGED_VERSION,
                instance: ::core::ptr::null_mut(),
                on_enable: Some(on_enable),
                on_disable: Some(on_disable),
                on_unload: Some(on_unload),
            };
            true
        }
    };
}
