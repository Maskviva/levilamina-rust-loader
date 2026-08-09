//! Cross-mod **service registry** — named request/response calls between mods.
//!
//! # This is not the bus
//!
//! [`crate::bus`] is one-way broadcast: you publish and move on, nobody has to
//! be listening, and a return value would be meaningless because any number of
//! subscribers may run in an order nobody controls. A query is the opposite on
//! every axis:
//!
//! | | bus | service |
//! |---|---|---|
//! | providers per name | any number | **exactly one** |
//! | nobody registered | normal | an error you must handle |
//! | return value | none | the entire point |
//! | ordering | undefined, must not matter | there is only one callee |
//!
//! Use the bus to announce that something happened. Use a service to ask a
//! question.
//!
//! # Registration is exclusive
//!
//! Two mods answering `plot:can` is not "both run" — it is an ambiguous answer
//! with no way for the caller to pick one. [`register`] therefore **fails** if
//! the name is taken, and the loader logs which mod holds it. A silent
//! last-wins would make the answer depend on mod load order, which nobody
//! controls and which changes the day someone installs an unrelated mod.
//!
//! # Namespace your names
//!
//! `plot:can`, not `can`. There is no registry and no reservation, so a bare
//! name is a collision waiting for the second mod with the same idea — and
//! because registration is exclusive, that collision is a hard failure at
//! startup rather than a subtle one at runtime.
//!
//! # Synchronous, no timeout
//!
//! The provider runs inline on your thread and its answer comes back from
//! [`call`]. There is no timeout and there will not be one: a provider that
//! blocks blocks the server thread exactly like any other callback, and
//! returning "timed out" while the callback kept running would hand you a wrong
//! answer *and* leave the provider running.
//!
//! # You cannot call your own service
//!
//! [`call`] refuses it. A mod reaching its own provider is going through two
//! FFI hops and a mutex to get to a function it can call directly, and when it
//! *is* a loop it is the shape that produces the least legible stack. Cross-mod
//! loops (A → B → A) are caught by a depth cap instead.
//!
//! ```no_run
//! use levilamina::service;
//!
//! // Answer questions from other mods.
//! service::register("mymod:ping", |_name, request| {
//!     if request.is_empty() {
//!         return Err("expected a name".into());
//!     }
//!     Ok(format!(r#"{{"pong":"{request}"}}"#))
//! })?
//! .forget();
//!
//! // Ask another mod a question.
//! match service::call("plot:at", r#"{"dim":3,"x":10,"z":-4}"#) {
//!     Ok(reply) => println!("{reply}"),
//!     Err(service::CallError::NotFound) => println!("no plot plugin loaded"),
//!     Err(e) => println!("call failed: {e}"),
//! }
//! # Ok::<(), levilamina::Error>(())
//! ```

use std::ffi::c_void;
use std::fmt;
use std::panic::{catch_unwind, AssertUnwindSafe};

use crate::error::{Error, Result};
use crate::ffi::{r, s};
use crate::logger::Logger;
use crate::{rt, sys};

/// A provider handler: `(name, request) -> Ok(reply) | Err(message)`.
///
/// `Err` is delivered to the caller as [`CallError::Provider`] carrying the
/// message, which is what makes "no such plot" distinguishable from "the
/// database is down" at the call site.
type ServiceCallback = Box<dyn FnMut(&str, &str) -> std::result::Result<String, String>>;

/// A live registration. Dropping it unregisters (RAII); call
/// [`Registration::forget`] to keep it for the lifetime of the mod.
///
/// Deliberately shaped like [`crate::bus::Subscription`] and [`crate::Listener`]
/// — one lifetime idiom for every callback-holding handle in this crate is
/// worth more than a marginally better one used in a third of them.
pub struct Registration {
    id: u64,
    cb: *mut ServiceCallback,
}

impl Registration {
    /// The registration id the loader assigned. Only useful for logging.
    pub fn id(&self) -> u64 {
        self.id
    }

    /// Keep this registration for as long as the mod is loaded. Leaks the
    /// closure, which is exactly right for a provider that should never stop
    /// answering; the loader drops the entry on unload.
    pub fn forget(mut self) {
        self.id = 0;
        self.cb = std::ptr::null_mut();
        std::mem::forget(self);
    }
}

impl Drop for Registration {
    fn drop(&mut self) {
        if self.id == 0 {
            return;
        }
        let rt = rt();
        unsafe {
            // Free the closure only if the loader confirms it removed the
            // entry. If it did not (already dropped at unload, say), the
            // pointer may still be reachable from a call in flight, and freeing
            // it here would be the use-after-free this module exists to avoid.
            if (rt.api.service_unregister)(rt.handle, self.id) {
                drop(Box::from_raw(self.cb));
            }
        }
    }
}

/// Why a [`call`] did not produce an answer.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum CallError {
    /// Nobody provides that name — or the provider is unloaded or disabled.
    ///
    /// **This is the normal case for an optional dependency**, not an
    /// exceptional one. Treat it as "that mod is not installed" and carry on.
    NotFound,
    /// The provider ran and reported a failure. The string is its message.
    Provider(String),
    /// The loader refused: name empty or over 128 bytes, a call to your own
    /// service, or the call-depth limit (a call loop).
    Refused,
    /// A status code this crate does not know. Only reachable if the loader is
    /// newer than the crate.
    Unknown(i32),
}

impl fmt::Display for CallError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            CallError::NotFound => write!(f, "no provider for this service"),
            CallError::Provider(m) => write!(f, "provider failed: {m}"),
            CallError::Refused => {
                write!(f, "refused (bad name, self-call, or call depth exceeded)")
            }
            CallError::Unknown(c) => write!(f, "unknown service status {c}"),
        }
    }
}

impl std::error::Error for CallError {}

impl From<CallError> for Error {
    fn from(e: CallError) -> Error {
        Error(e.to_string())
    }
}

unsafe extern "C" fn service_trampoline(
    user: *mut c_void,
    name: sys::LeviRsStr,
    request: sys::LeviRsStr,
    ctx: *mut c_void,
    reply: sys::LeviRsStrSink,
) -> bool {
    let cb = &mut *user.cast::<ServiceCallback>();
    let (n, req) = (r(name), r(request));
    let out = match catch_unwind(AssertUnwindSafe(|| cb(n, req))) {
        Ok(v) => v,
        Err(_) => {
            Logger::get().error(&format!("panic in service handler for '{n}'"));
            // A panicking provider has not answered anything. Report failure
            // rather than letting the caller read an empty reply as success —
            // an empty successful answer is a legitimate value for plenty of
            // services, so it must not double as "something went wrong".
            Err("provider panicked".to_string())
        }
    };
    match out {
        Ok(body) => {
            reply(ctx, s(&body));
            true
        }
        Err(msg) => {
            reply(ctx, s(&msg));
            false
        }
    }
}

/// Register this mod as the provider of `name`.
///
/// Fails if the name is empty, over 128 bytes, or **already provided by another
/// mod** — see the module docs on why that is a hard failure rather than a
/// silent override. Check the loader log for who holds it.
pub fn register(
    name: &str,
    f: impl FnMut(&str, &str) -> std::result::Result<String, String> + 'static,
) -> Result<Registration> {
    let boxed: *mut ServiceCallback = Box::into_raw(Box::new(Box::new(f) as ServiceCallback));
    let rt = rt();
    let id =
        unsafe { (rt.api.service_register)(rt.handle, s(name), service_trampoline, boxed.cast()) };
    if id == 0 {
        // Never registered, so the trampoline can never run and reclaim it.
        unsafe { drop(Box::from_raw(boxed)) };
        return Err(Error(format!(
            "service: registering '{name}' was refused (name empty/oversized, or another mod \
             already provides it — the loader log names it)"
        )));
    }
    Ok(Registration { id, cb: boxed })
}

/// Call `name` with `request` and return the provider's reply.
///
/// [`CallError::NotFound`] is the expected answer when the providing mod is not
/// installed; that is the case to handle, not to log as an error.
pub fn call(name: &str, request: &str) -> std::result::Result<String, CallError> {
    let mut out: Option<String> = None;
    let rt = rt();
    let code = unsafe {
        (rt.api.service_call)(
            rt.handle,
            s(name),
            s(request),
            (&mut out as *mut Option<String>).cast(),
            crate::ffi::set_string,
        )
    };
    match code {
        sys::LEVI_RS_SERVICE_OK => Ok(out.unwrap_or_default()),
        sys::LEVI_RS_SERVICE_NOT_FOUND => Err(CallError::NotFound),
        sys::LEVI_RS_SERVICE_ERROR => Err(CallError::Provider(out.unwrap_or_default())),
        sys::LEVI_RS_SERVICE_REFUSED => Err(CallError::Refused),
        other => Err(CallError::Unknown(other)),
    }
}

/// One entry of [`list`].
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ServiceInfo {
    pub name: String,
    /// The mod providing it.
    pub owner: String,
}

/// Every registered service, as raw JSON: `[{"name":…,"mod":…}]`.
///
/// Returned unparsed on purpose — this crate has no JSON dependency, and adding
/// one so a diagnostic command can avoid three lines of string handling is a
/// bad trade for every mod that links it.
pub fn list_json() -> String {
    let mut out: Option<String> = None;
    let rt = rt();
    unsafe {
        (rt.api.service_list)(
            (&mut out as *mut Option<String>).cast(),
            crate::ffi::set_string,
        )
    };
    out.unwrap_or_else(|| "[]".to_string())
}

/// Whether anything currently provides `name`.
///
/// Racy by nature — the provider can unload between this call and the next
/// [`call`]. Use it to decide whether building an expensive request is worth
/// it, not as a guard: [`CallError::NotFound`] still has to be handled.
pub fn exists(name: &str) -> bool {
    // Cheapest correct probe: a real call with an empty request would run the
    // provider, which is a side effect no existence check should have.
    let needle = format!("\"name\":\"{name}\"");
    list_json().contains(&needle)
}
