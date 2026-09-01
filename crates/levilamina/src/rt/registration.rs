use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::Mutex;

use crate::error::{Error, Result};
use crate::logger::Logger;
use crate::runtime::ModContext;
use crate::sys;

/// S1：`Send` 是 `ModSlot<T>`（`Mutex<Option<T>>`）真正 `Sync` 的前提；
/// 生命周期回调可能在不同线程上进入（loader 允许 unload 与 service 调用跨线程）。
pub trait LeviMod: Sized + Send + 'static {
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

#[doc(hidden)]
pub unsafe fn __init_runtime(api: *const sys::LeviRsApi, handle: sys::LeviRsModHandle) -> bool {
    if api.is_null() {
        return false;
    }

    let api: &'static sys::LeviRsApi = &*api;

    if (api.abi_version & sys::LEVI_RS_ABI_TARGET_MASK) != sys::LEVI_RS_ABI_TARGET_TAG {
        return false;
    }
    let loader_abi = api.abi_version & !sys::LEVI_RS_ABI_TARGET_MASK;
    if loader_abi < sys::LEVI_RS_ABI_VERSION {
        return false;
    }

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
