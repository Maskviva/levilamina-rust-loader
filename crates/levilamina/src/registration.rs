//! The [`LeviMod`] trait, the registration slot, and the bridge entry-point
//! plumbing (`__init_runtime`, `__load`, `__lifecycle`, `register_mod!`).
//!
//! These are the only symbols the generated `levi_rs_main` touches. Mod code
//! never calls them directly — it implements [`LeviMod`] and uses the
//! [`register_mod!`](crate::register_mod!) macro.

use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::Mutex;

use crate::error::{Error, Result};
use crate::logger::Logger;
use crate::runtime::ModContext;
use crate::sys;

/// Implement this and call [`register_mod!`](crate::register_mod!) once.
///
/// All hooks run on the server thread, and the mod instance is only ever
/// touched from the server thread — so it may freely hold `!Send` resources
/// such as [`crate::Listener`].
pub trait LeviMod: Sized + 'static {
    /// Called while the mod is loading. Return `Err` to abort loading.
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

// SAFETY: the slot is only ever locked and accessed on the server thread, via
// the bridge-invoked entry points (__load / __lifecycle). The instance never
// migrates between threads at runtime, so a `!Send` mod (e.g. one holding a
// `Listener`) is sound here even though `Mutex<Option<T>>` is not auto-Sync.
unsafe impl<T: LeviMod> Sync for ModSlot<T> {}

#[doc(hidden)]
pub unsafe fn __init_runtime(api: *const sys::LeviRsApi, handle: sys::LeviRsModHandle) -> bool {
    if api.is_null() {
        return false;
    }
    // SAFETY: the bridge guarantees the api table outlives the mod.
    let api: &'static sys::LeviRsApi = &*api;

    // ABI compatibility is a RANGE (mirrors the loader's RustModManager check).
    // The loader's `abi_version` must be >= the one we compiled against: a
    // newer loader exposes an additive superset of our table, so every slot we
    // know about is present and byte-identical. A loader OLDER than us may be
    // missing trailing slots we'd call — refuse it.
    //
    // (Historically some additive bumps advanced `abi_version` too, not just
    // `struct_size`, so this must be `<`, not `!=` — otherwise a v4-built mod
    // rejects a perfectly compatible v5 loader.)
    if api.abi_version < sys::LEVI_RS_ABI_VERSION {
        return false;
    }
    // The precise gate regardless of how the version was bumped: the loader's
    // actual table must be at least as large as the `LeviRsApi` this crate was
    // compiled against, or a trailing field access would read past what the
    // loader allocated.
    if (api.struct_size as usize) < core::mem::size_of::<sys::LeviRsApi>() {
        return false;
    }
    crate::runtime::set_runtime(api, handle)
}

#[doc(hidden)]
pub fn __lifecycle<T: LeviMod>(
    slot: &'static ModSlot<T>,
    stage: u8, // 1=enable, 2=disable, 3=unload
) -> bool {
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
                *guard = None; // drop the instance before the dylib unloads
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

/// Export the `levi_rs_main` entry point for a [`LeviMod`] implementation.
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
                abi_version: $crate::sys::LEVI_RS_ABI_VERSION,
                instance: ::core::ptr::null_mut(),
                on_enable: Some(on_enable),
                on_disable: Some(on_disable),
                on_unload: Some(on_unload),
            };
            true
        }
    };
}
