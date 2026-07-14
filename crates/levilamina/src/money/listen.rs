//! LLMoney before/after event listeners.
//!
//! Single-slot per kind — LLMoney itself keeps one before-callback and one
//! after-callback total for the whole process, so this module mirrors that
//! model. Registering replaces; drop the returned guard to clear.

use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::{Mutex, OnceLock};

use crate::ffi::r;
use crate::logger::Logger;
use crate::{rt, sys};

use super::MoneyEvent;

type BeforeCb = Box<dyn FnMut(&MoneyEvent) -> bool + Send>;
type AfterCb = Box<dyn FnMut(&MoneyEvent) + Send>;

static BEFORE_SLOT: OnceLock<Mutex<Option<BeforeCb>>> = OnceLock::new();
static AFTER_SLOT: OnceLock<Mutex<Option<AfterCb>>> = OnceLock::new();

fn before_slot() -> &'static Mutex<Option<BeforeCb>> {
    BEFORE_SLOT.get_or_init(|| Mutex::new(None))
}
fn after_slot() -> &'static Mutex<Option<AfterCb>> {
    AFTER_SLOT.get_or_init(|| Mutex::new(None))
}

/// Fire a callback *before* every money change. Return `true` to allow,
/// `false` to cancel. Drop the [`BeforeGuard`] to un-listen, or `.forget()`
/// to keep it for the mod's lifetime.
///
/// A panic in the callback is caught, logged, and treated as *allow* — a
/// broken handler shouldn't block every economy operation. Reentrancy is
/// safe: a nested `money::*` call sees an empty slot and proceeds.
pub fn on_before(callback: impl FnMut(&MoneyEvent) -> bool + Send + 'static) -> BeforeGuard {
    {
        let mut slot = before_slot().lock().unwrap_or_else(|p| p.into_inner());
        *slot = Some(Box::new(callback));
    }
    // Idempotent: the C++ side keeps one LLMoney registration; this just
    // (re)points it at our static trampoline.
    unsafe { (rt().api.money_listen_before_event)(before_trampoline) };
    BeforeGuard(())
}

/// Fire a callback *after* every money change. Return value has no effect
/// (the change already happened). Panics are caught and logged. Same
/// single-slot semantics as [`on_before`].
pub fn on_after(callback: impl FnMut(&MoneyEvent) + Send + 'static) -> AfterGuard {
    {
        let mut slot = after_slot().lock().unwrap_or_else(|p| p.into_inner());
        *slot = Some(Box::new(callback));
    }
    unsafe { (rt().api.money_listen_after_event)(after_trampoline) };
    AfterGuard(())
}

/// RAII handle: clears the before-event slot on drop. Call
/// [`BeforeGuard::forget`] to keep the callback for the whole mod lifetime.
pub struct BeforeGuard(());
impl BeforeGuard {
    pub fn forget(self) {
        std::mem::forget(self);
    }
}
impl Drop for BeforeGuard {
    fn drop(&mut self) {
        let mut slot = before_slot().lock().unwrap_or_else(|p| p.into_inner());
        *slot = None;
    }
}

/// RAII handle: clears the after-event slot on drop. Call
/// [`AfterGuard::forget`] to keep the callback for the whole mod lifetime.
pub struct AfterGuard(());
impl AfterGuard {
    pub fn forget(self) {
        std::mem::forget(self);
    }
}
impl Drop for AfterGuard {
    fn drop(&mut self) {
        let mut slot = after_slot().lock().unwrap_or_else(|p| p.into_inner());
        *slot = None;
    }
}

// Trampolines. We take the callback out of its slot, invoke it, and put it
// back — never holding the slot's lock across user code. Otherwise a nested
// `money::*` call from the closure would deadlock, and re-registration
// would block. Side effect: a nested invocation sees an empty slot and
// returns `true` (allow) — the safest default for a single-slot API.

unsafe extern "C" fn before_trampoline(
    kind: sys::LLMoneyEvent,
    from: sys::LeviRsStr,
    to: sys::LeviRsStr,
    value: i64,
) -> bool {
    let taken = {
        let mut slot = before_slot().lock().unwrap_or_else(|p| p.into_inner());
        slot.take()
    };
    let Some(mut cb) = taken else { return true };

    let ev = MoneyEvent {
        kind,
        from: r(from),
        to: r(to),
        amount: value,
    };
    let result = catch_unwind(AssertUnwindSafe(|| cb(&ev))).unwrap_or_else(|_| {
        Logger::get().error("panic in money::on_before handler (allowing the change)");
        true
    });

    // Put it back — unless the user (re)registered while we were running,
    // in which case drop ours to honor the replacement.
    let mut slot = before_slot().lock().unwrap_or_else(|p| p.into_inner());
    if slot.is_none() {
        *slot = Some(cb);
    }
    result
}

unsafe extern "C" fn after_trampoline(
    kind: sys::LLMoneyEvent,
    from: sys::LeviRsStr,
    to: sys::LeviRsStr,
    value: i64,
) -> bool {
    let taken = {
        let mut slot = after_slot().lock().unwrap_or_else(|p| p.into_inner());
        slot.take()
    };
    if let Some(mut cb) = taken {
        let ev = MoneyEvent {
            kind,
            from: r(from),
            to: r(to),
            amount: value,
        };
        if catch_unwind(AssertUnwindSafe(|| cb(&ev))).is_err() {
            Logger::get().error("panic in money::on_after handler (ignored)");
        }
        let mut slot = after_slot().lock().unwrap_or_else(|p| p.into_inner());
        if slot.is_none() {
            *slot = Some(cb);
        }
    }
    true
}
