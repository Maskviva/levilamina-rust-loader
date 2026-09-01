use std::ffi::c_void;
use std::panic::{catch_unwind, AssertUnwindSafe};

use crate::error::{Error, Result};
use crate::ffi::{r, s};
use crate::logger::Logger;
use crate::{rt, sys};

type BusCallback = Box<dyn FnMut(&str, &str) -> bool>;

pub struct Subscription {
    id: u64,
    cb: *mut BusCallback,
}

impl Subscription {
    pub fn id(&self) -> u64 {
        self.id
    }

    pub fn forget(mut self) {
        self.id = 0;
        self.cb = std::ptr::null_mut();
        std::mem::forget(self);
    }
}

impl Drop for Subscription {
    fn drop(&mut self) {
        if self.id == 0 {
            return;
        }
        let rt = rt();
        unsafe {
            if (rt.api.bus_unsubscribe)(rt.handle(), self.id) {
                drop(Box::from_raw(self.cb));
            }
        }
    }
}

unsafe extern "C" fn bus_trampoline(
    user: *mut c_void,
    topic: sys::LeviRsStr,
    payload: sys::LeviRsStr,
) -> bool {
    let cb = &mut *user.cast::<BusCallback>();
    let (t, p) = (r(topic), r(payload));
    match catch_unwind(AssertUnwindSafe(|| cb(t, p))) {
        Ok(v) => v,
        Err(_) => {
            Logger::get().error(&format!("panic in bus handler for topic '{t}'"));

            false
        }
    }
}

pub fn subscribe(topic: &str, f: impl FnMut(&str, &str) -> bool + 'static) -> Result<Subscription> {
    let boxed: *mut BusCallback = Box::into_raw(Box::new(Box::new(f) as BusCallback));
    let rt = rt();
    let id = unsafe { (rt.api.bus_subscribe)(rt.handle(), s(topic), bus_trampoline, boxed.cast()) };
    if id == 0 {
        unsafe { drop(Box::from_raw(boxed)) };
        return Err(Error(format!("bus: subscribe to '{topic}' refused")));
    }
    Ok(Subscription { id, cb: boxed })
}

pub fn publish(topic: &str, payload: &str) -> u32 {
    let rt = rt();
    unsafe { (rt.api.bus_publish)(rt.handle(), s(topic), s(payload)) }
}

pub fn publish_vetoable(topic: &str, payload: &str) -> Vetoable {
    let mut delivered: u32 = 0;
    let rt = rt();
    let refused =
        unsafe { (rt.api.bus_publish_vetoable)(rt.handle(), s(topic), s(payload), &mut delivered) };
    Vetoable { refused, delivered }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Vetoable {
    pub refused: bool,

    pub delivered: u32,
}

pub fn subscriber_count(topic: &str) -> u32 {
    let rt = rt();
    unsafe { (rt.api.bus_subscriber_count)(s(topic)) }
}
