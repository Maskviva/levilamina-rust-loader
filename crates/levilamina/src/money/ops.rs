use std::ffi::c_void;
use std::time::Duration;

use crate::error::{Error, Result};
use crate::ffi::{collect_strs, s, set_string};
use crate::rt;

pub fn get(xuid: &str) -> i64 {
    unsafe { (rt().api.get_money)(s(xuid)) }
}

pub fn set(xuid: &str, amount: i64) -> Result<()> {
    check(unsafe { (rt().api.set_money)(s(xuid), amount) }, || {
        format!("money::set('{xuid}', {amount}) failed (LLMoney absent or before-event cancelled)")
    })
}

pub fn add(xuid: &str, delta: i64) -> Result<()> {
    check(unsafe { (rt().api.add_money)(s(xuid), delta) }, || {
        format!("money::add('{xuid}', {delta}) failed")
    })
}

pub fn reduce(xuid: &str, delta: i64) -> Result<()> {
    check(unsafe { (rt().api.reduce_money)(s(xuid), delta) }, || {
        format!("money::reduce('{xuid}', {delta}) failed")
    })
}

pub fn transfer(from: &str, to: &str, amount: i64, note: &str) -> Result<()> {
    check(
        unsafe { (rt().api.trans_money)(s(from), s(to), amount, s(note)) },
        || format!("money::transfer('{from}' -> '{to}', {amount}) failed"),
    )
}

pub fn history(xuid: &str, within: Duration) -> String {
    let mut out: Option<String> = None;
    unsafe {
        (rt().api.money_get_hist)(
            s(xuid),
            duration_secs_i32(within),
            (&mut out as *mut Option<String>).cast::<c_void>(),
            set_string,
        )
    };
    out.unwrap_or_default()
}

pub fn clear_history_older_than(older_than: Duration) {
    unsafe { (rt().api.money_clear_hist)(duration_secs_i32(older_than)) };
}

pub fn clear_all_history() {
    unsafe { (rt().api.money_clear_hist)(0) };
}

pub fn ranking(top_n: u16) -> Vec<(String, i64)> {
    let lines = collect_strs(|ctx, sink| unsafe { (rt().api.money_ranking)(top_n, ctx, sink) });
    lines
        .into_iter()
        .filter_map(|line| {
            let (xuid, bal) = line.rsplit_once(':')?;
            let bal = bal.trim().parse::<i64>().ok()?;
            Some((xuid.to_owned(), bal))
        })
        .collect()
}

fn check(ok: bool, msg: impl FnOnce() -> String) -> Result<()> {
    if ok {
        Ok(())
    } else {
        Err(Error(msg()))
    }
}

fn duration_secs_i32(d: Duration) -> i32 {
    d.as_secs().min(i32::MAX as u64) as i32
}
