//! Host system information & environment variables. Thread-safe.

use crate::error::{Error, Result};
use crate::ffi::{call_out_str, s};
use crate::nbt::NbtValue;
use crate::{rt, sys};

/// Wall-clock local time from the host OS.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct LocalTime {
    pub year: i32,
    /// 1–12.
    pub month: i32,
    pub day: i32,
    pub hour: i32,
    pub minute: i32,
    pub second: i32,
    pub ms: i32,
}

/// OS name, e.g. `Windows`.
pub fn os_name() -> Result<String> {
    info(sys::SYS_OS_NAME)
}

/// OS version string.
pub fn os_version() -> Result<String> {
    info(sys::SYS_OS_VERSION)
}

/// System locale code, e.g. `zh_CN`.
pub fn locale() -> Result<String> {
    info(sys::SYS_LOCALE)
}

/// Current local time.
pub fn local_time() -> Result<LocalTime> {
    let raw = info(sys::SYS_LOCAL_TIME)?;
    let v = NbtValue::parse(&raw)?;
    let field = |name: &str| v.get(name).and_then(|x| x.as_i64()).unwrap_or(0) as i32;
    Ok(LocalTime {
        year: field("year"),
        month: field("month"),
        day: field("day"),
        hour: field("hour"),
        minute: field("minute"),
        second: field("second"),
        ms: field("ms"),
    })
}

/// An environment variable's value (empty string when unset).
pub fn env(name: &str) -> String {
    call_out_str(|ctx, sink| unsafe { (rt().api.sys_get_env)(s(name), ctx, sink) })
        .unwrap_or_default()
}

pub fn set_env(name: &str, value: &str) -> Result<()> {
    let ok = unsafe { (rt().api.sys_set_env)(s(name), s(value)) };
    if ok {
        Ok(())
    } else {
        Err(Error(format!("set_env('{name}') failed")))
    }
}

/// Whether the server runs under Wine.
pub fn is_wine() -> bool {
    unsafe { (rt().api.sys_is_wine)() }
}

fn info(prop: i32) -> Result<String> {
    call_out_str(|ctx, sink| unsafe { (rt().api.sys_info_str)(prop, ctx, sink) })
        .ok_or_else(|| Error("sys_info unavailable".into()))
}
