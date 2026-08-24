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

pub fn on_before(callback: impl FnMut(&MoneyEvent) -> bool + Send + 'static) -> BeforeGuard {
    {
        let mut slot = before_slot().lock().unwrap_or_else(|p| p.into_inner());
        *slot = Some(Box::new(callback));
    }

    unsafe { (rt().api.money_listen_before_event)(before_trampoline) };
    BeforeGuard(())
}

pub fn on_after(callback: impl FnMut(&MoneyEvent) + Send + 'static) -> AfterGuard {
    {
        let mut slot = after_slot().lock().unwrap_or_else(|p| p.into_inner());
        *slot = Some(Box::new(callback));
    }
    unsafe { (rt().api.money_listen_after_event)(after_trampoline) };
    AfterGuard(())
}

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
