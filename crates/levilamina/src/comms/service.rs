use std::ffi::c_void;
use std::fmt;
use std::panic::{catch_unwind, AssertUnwindSafe};

use crate::error::{Error, Result};
use crate::ffi::{r, s};
use crate::logger::Logger;
use crate::{rt, sys};

type ServiceCallback = Box<dyn FnMut(&str, &str) -> std::result::Result<String, String>>;

pub struct Registration {
    id: u64,
    cb: *mut ServiceCallback,
}

impl Registration {
    pub fn id(&self) -> u64 {
        self.id
    }

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
            if (rt.api.service_unregister)(rt.handle, self.id) {
                drop(Box::from_raw(self.cb));
            }
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum CallError {
    NotFound,

    Provider(String),

    Refused,

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

pub fn register(
    name: &str,
    f: impl FnMut(&str, &str) -> std::result::Result<String, String> + 'static,
) -> Result<Registration> {
    let boxed: *mut ServiceCallback = Box::into_raw(Box::new(Box::new(f) as ServiceCallback));
    let rt = rt();
    let id =
        unsafe { (rt.api.service_register)(rt.handle, s(name), service_trampoline, boxed.cast()) };
    if id == 0 {
        unsafe { drop(Box::from_raw(boxed)) };
        return Err(Error(format!(
            "service: registering '{name}' was refused (name empty/oversized, or another mod \
             already provides it — the loader log names it)"
        )));
    }
    Ok(Registration { id, cb: boxed })
}

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

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ServiceInfo {
    pub name: String,

    pub owner: String,
}

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

pub fn exists(name: &str) -> bool {
    let needle = format!("\"name\":\"{name}\"");
    list_json().contains(&needle)
}
