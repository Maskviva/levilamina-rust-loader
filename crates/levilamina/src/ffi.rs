//! Internal FFI helpers shared by every module: string views, sink
//! trampolines, and the "call an out-string API" pattern.

use std::ffi::c_void;

use crate::sys;

/// Borrow a Rust `&str` as a `LeviRsStr` view for the duration of a call.
pub(crate) fn s(text: &str) -> sys::LeviRsStr {
    sys::LeviRsStr {
        ptr: text.as_ptr(),
        len: text.len(),
    }
}

/// # Safety
/// `raw` must point at valid UTF-8 for `len` bytes (bridge contract), and the
/// returned borrow must not outlive the FFI call that produced `raw`.
pub(crate) unsafe fn r<'a>(raw: sys::LeviRsStr) -> &'a str {
    if raw.ptr.is_null() {
        return "";
    }
    std::str::from_utf8_unchecked(std::slice::from_raw_parts(raw.ptr, raw.len))
}

/// Sink that appends each string into a `Vec<String>` behind `ctx`.
pub(crate) unsafe extern "C" fn push_string(ctx: *mut c_void, item: sys::LeviRsStr) {
    (*ctx.cast::<Vec<String>>()).push(r(item).to_owned());
}

/// Sink that overwrites an `Option<String>` behind `ctx`.
pub(crate) unsafe extern "C" fn set_string(ctx: *mut c_void, item: sys::LeviRsStr) {
    *ctx.cast::<Option<String>>() = Some(r(item).to_owned());
}

/// Run a bridge call that reports one string through a `LeviRsStrSink`.
/// Returns `None` when the call itself returns false.
pub(crate) fn call_out_str(
    f: impl FnOnce(*mut c_void, sys::LeviRsStrSink) -> bool,
) -> Option<String> {
    let mut out: Option<String> = None;
    let ok = f((&mut out as *mut Option<String>).cast(), set_string);
    if ok {
        Some(out.unwrap_or_default())
    } else {
        None
    }
}

/// Collect every string a bridge enumeration pushes through a `LeviRsStrSink`.
pub(crate) fn collect_strs(f: impl FnOnce(*mut c_void, sys::LeviRsStrSink)) -> Vec<String> {
    let mut out: Vec<String> = Vec::new();
    f((&mut out as *mut Vec<String>).cast(), push_string);
    out
}
