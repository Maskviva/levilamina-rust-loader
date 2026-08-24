use std::ffi::c_void;

use crate::sys;

pub(crate) fn s(text: &str) -> sys::LeviRsStr {
    sys::LeviRsStr {
        ptr: text.as_ptr(),
        len: text.len(),
    }
}

pub(crate) unsafe fn r<'a>(raw: sys::LeviRsStr) -> &'a str {
    if raw.ptr.is_null() {
        return "";
    }
    std::str::from_utf8_unchecked(std::slice::from_raw_parts(raw.ptr, raw.len))
}

pub(crate) unsafe extern "C" fn push_string(ctx: *mut c_void, item: sys::LeviRsStr) {
    (*ctx.cast::<Vec<String>>()).push(r(item).to_owned());
}

pub(crate) unsafe extern "C" fn collect_bytes(ctx: *mut c_void, item: sys::LeviRsStr) {
    if item.ptr.is_null() {
        return;
    }
    let slice = std::slice::from_raw_parts(item.ptr, item.len);
    (*ctx.cast::<Vec<Vec<u8>>>()).push(slice.to_vec());
}

pub(crate) unsafe extern "C" fn set_string(ctx: *mut c_void, item: sys::LeviRsStr) {
    *ctx.cast::<Option<String>>() = Some(r(item).to_owned());
}

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

pub(crate) fn collect_strs(f: impl FnOnce(*mut c_void, sys::LeviRsStrSink)) -> Vec<String> {
    let mut out: Vec<String> = Vec::new();
    f((&mut out as *mut Vec<String>).cast(), push_string);
    out
}
