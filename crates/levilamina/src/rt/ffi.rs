use std::ffi::c_void;

use crate::sys;

pub(crate) fn s(text: &str) -> sys::LeviRsStr {
    sys::LeviRsStr {
        ptr: text.as_ptr(),
        len: text.len(),
    }
}

/// W14：C++ 侧交过来的字节**最终源自客户端**（玩家名、聊天、命令输出），不能无条件信任成 UTF-8。
/// 原来是 `from_utf8_unchecked`：一个非法序列就是 UB。现在校验；非法时截到最后一个合法字节
/// （借用不变、不分配），记一条一次性告警，debug 构建下直接断言。
pub(crate) unsafe fn r<'a>(raw: sys::LeviRsStr) -> &'a str {
    if raw.ptr.is_null() {
        return "";
    }
    let bytes = std::slice::from_raw_parts(raw.ptr, raw.len);
    match std::str::from_utf8(bytes) {
        Ok(s) => s,
        Err(e) => {
            debug_assert!(false, "loader 交来的字节不是 UTF-8：{e}");
            static WARNED: std::sync::atomic::AtomicBool =
                std::sync::atomic::AtomicBool::new(false);
            if !WARNED.swap(true, std::sync::atomic::Ordering::Relaxed) {
                crate::Logger::get().warn(&format!(
                    "loader 交来的字符串不是合法 UTF-8（第 {} 字节起），已截断；这条只报一次。",
                    e.valid_up_to()
                ));
            }
            std::str::from_utf8_unchecked(&bytes[..e.valid_up_to()])
        }
    }
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
