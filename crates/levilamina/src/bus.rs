//! Cross-mod event bus — publish/subscribe between loaded mods.
//!
//! # Why this is not "mod A exports a function"
//!
//! It could not be. Unloading a mod calls `FreeLibrary`, so a callback handed
//! directly from one mod to another becomes a pointer into unmapped memory the
//! moment the subscriber is unloaded — and the failure is a crash inside the
//! *publisher*, with nothing in the log to connect it to the mod that left.
//!
//! So the loader owns the subscription table. It revalidates the owning mod on
//! every dispatch and drops the whole set when that mod goes away, the same
//! discipline forms and scheduled tasks already use.
//!
//! # The payload is yours
//!
//! The loader never looks inside `payload`. Pick a format with whoever you are
//! talking to (JSON is the common choice; SNBT matches the rest of this ABI)
//! and version it yourself. A loader-defined schema would be one more thing
//! that has to be migrated in lockstep across mods that ship separately.
//!
//! # Topics
//!
//! Plain strings, max 128 bytes. **Namespace them** — `plot:enter`, not
//! `enter`. There is no registry and no reservation, so an unprefixed topic is
//! a collision waiting for the second mod that has the same idea.
//!
//! # Two rules worth knowing before you design around it
//!
//! * **You never receive your own publishes.** A mod that wants to notify
//!   itself has a function call. Self-delivery is also the one loop shape no
//!   depth limit can distinguish from real work.
//! * **A subscriber can only tighten.** [`publish_vetoable`] collects `true`
//!   from any subscriber as a refusal, and nothing can turn a refusal back
//!   into an approval. Otherwise whoever ran last would decide, and subscriber
//!   order is not something either side controls.
//!
//! ```no_run
//! use levilamina::bus;
//!
//! // Listen for another mod's events.
//! bus::subscribe("plot:enter", |_topic, payload| {
//!     println!("someone walked into a plot: {payload}");
//!     false // no opinion (only meaningful for vetoable topics)
//! })?
//! .forget();
//!
//! // Announce one of your own.
//! bus::publish("mymod:started", r#"{"version":"1.0"}"#);
//! # Ok::<(), levilamina::Error>(())
//! ```

use std::ffi::c_void;
use std::panic::{catch_unwind, AssertUnwindSafe};

use crate::error::{Error, Result};
use crate::ffi::{r, s};
use crate::logger::Logger;
use crate::{rt, sys};

type BusCallback = Box<dyn FnMut(&str, &str) -> bool>;

/// A live subscription. Dropping it unsubscribes (RAII); call
/// [`Subscription::forget`] to keep it for the lifetime of the mod.
///
/// Modelled on [`crate::Listener`] deliberately — one lifetime idiom for every
/// callback-holding handle in this crate is worth more than a marginally
/// better one used in half the places.
pub struct Subscription {
    id: u64,
    cb: *mut BusCallback,
}

impl Subscription {
    /// The subscription id the loader assigned. Only useful for logging.
    pub fn id(&self) -> u64 {
        self.id
    }

    /// Keep this subscription for as long as the mod is loaded (leaks the
    /// closure, which is exactly right for a subscription that should never
    /// end early). The loader drops it on unload.
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
            // Free the closure only if the loader confirms it removed the
            // entry. If it did not (already dropped at unload, say), the
            // pointer may still be reachable from a dispatch in flight, and
            // freeing it here would be the use-after-free this whole module
            // exists to avoid.
            if (rt.api.bus_unsubscribe)(rt.handle, self.id) {
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
            // A handler that panicked did not decide anything. Reporting a
            // veto would let a crashing mod block another mod's work; report
            // "no opinion" and let the publisher proceed.
            false
        }
    }
}

/// Subscribe to `topic`. The handler returns the veto bit — see
/// [`publish_vetoable`]; return `false` if you are only observing.
///
/// Fails if the topic is empty or over 128 bytes, or if the loader refuses the
/// registration.
pub fn subscribe(topic: &str, f: impl FnMut(&str, &str) -> bool + 'static) -> Result<Subscription> {
    let boxed: *mut BusCallback = Box::into_raw(Box::new(Box::new(f) as BusCallback));
    let rt = rt();
    let id = unsafe { (rt.api.bus_subscribe)(rt.handle, s(topic), bus_trampoline, boxed.cast()) };
    if id == 0 {
        // Never registered, so the trampoline can never run and reclaim it.
        unsafe { drop(Box::from_raw(boxed)) };
        return Err(Error(format!("bus: subscribe to '{topic}' refused")));
    }
    Ok(Subscription { id, cb: boxed })
}

/// Deliver `payload` to every subscriber **in other mods**. Returns how many
/// ran; `0` just means nobody is listening, which is the normal case.
pub fn publish(topic: &str, payload: &str) -> u32 {
    let rt = rt();
    unsafe { (rt.api.bus_publish)(rt.handle, s(topic), s(payload)) }
}

/// Publish and collect the veto bit: `refused` is true when **any** subscriber
/// returned `true`.
///
/// Every subscriber still runs — there is no short-circuit — so a mod that is
/// only observing sees the same stream whether or not someone else refused.
pub fn publish_vetoable(topic: &str, payload: &str) -> Vetoable {
    let mut delivered: u32 = 0;
    let rt = rt();
    let refused =
        unsafe { (rt.api.bus_publish_vetoable)(rt.handle, s(topic), s(payload), &mut delivered) };
    Vetoable { refused, delivered }
}

/// Outcome of [`publish_vetoable`].
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Vetoable {
    /// At least one subscriber refused.
    pub refused: bool,
    /// How many subscribers ran.
    pub delivered: u32,
}

/// How many subscribers `topic` has right now, across all mods.
///
/// Worth checking before building an expensive payload — but not before a
/// cheap one, since the check is itself a lock acquisition.
pub fn subscriber_count(topic: &str) -> u32 {
    let rt = rt();
    unsafe { (rt.api.bus_subscriber_count)(s(topic)) }
}
