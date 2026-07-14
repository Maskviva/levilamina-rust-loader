//! Money reads, writes, history, and ranking. Single-shot FFI calls with
//! no shared state — all listener/trampoline machinery lives in `super::listen`.

use std::ffi::c_void;
use std::time::Duration;

use crate::error::{Error, Result};
use crate::ffi::{collect_strs, s, set_string};
use crate::rt;

// ── Reads ─────────────────────────────────────────────────────────────

/// Balance for `xuid`. Missing account or LLMoney absent → `0`.
pub fn get(xuid: &str) -> i64 {
    unsafe { (rt().api.get_money)(s(xuid)) }
}

// ── Writes ────────────────────────────────────────────────────────────

/// Overwrite `xuid`'s balance.
pub fn set(xuid: &str, amount: i64) -> Result<()> {
    check(unsafe { (rt().api.set_money)(s(xuid), amount) }, || {
        format!("money::set('{xuid}', {amount}) failed (LLMoney absent or before-event cancelled)")
    })
}

/// Add to `xuid`'s balance. LLMoney rejects a negative `delta`.
pub fn add(xuid: &str, delta: i64) -> Result<()> {
    check(unsafe { (rt().api.add_money)(s(xuid), delta) }, || {
        format!("money::add('{xuid}', {delta}) failed")
    })
}

/// Subtract from `xuid`'s balance. Fails if the balance would go negative.
pub fn reduce(xuid: &str, delta: i64) -> Result<()> {
    check(unsafe { (rt().api.reduce_money)(s(xuid), delta) }, || {
        format!("money::reduce('{xuid}', {delta}) failed")
    })
}

/// Transfer `amount` from `from` to `to`, tagging the ledger with `note`.
/// Fails on insufficient balance or backend error. Pass `""` for no note.
pub fn transfer(from: &str, to: &str, amount: i64, note: &str) -> Result<()> {
    check(
        unsafe { (rt().api.trans_money)(s(from), s(to), amount, s(note)) },
        || format!("money::transfer('{from}' -> '{to}', {amount}) failed"),
    )
}

// ── History ───────────────────────────────────────────────────────────

/// Raw transfer history for `xuid` within the last `within` (LLMoney's own
/// serialization — typically newline-separated `timestamp | from | to |
/// amount | note` records). Empty when nothing matches, and also empty if
/// LLMoney isn't loaded — the two cases can't be distinguished.
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

/// Purge history older than `older_than`. `Duration::ZERO` wipes everything —
/// prefer [`clear_all_history`] for that intent so the call site reads
/// unambiguously.
pub fn clear_history_older_than(older_than: Duration) {
    unsafe { (rt().api.money_clear_hist)(duration_secs_i32(older_than)) };
}

/// Purge every history record.
pub fn clear_all_history() {
    unsafe { (rt().api.money_clear_hist)(0) };
}

// ── Ranking ───────────────────────────────────────────────────────────

/// Top-`n` accounts as `(xuid, balance)`, richest first. Lines the loader
/// can't parse as `"xuid:balance"` are silently dropped.
pub fn ranking(top_n: u16) -> Vec<(String, i64)> {
    let lines = collect_strs(|ctx, sink| unsafe { (rt().api.money_ranking)(top_n, ctx, sink) });
    lines
        .into_iter()
        .filter_map(|line| {
            // rsplit so an xuid containing ':' (shouldn't happen, but…)
            // doesn't confuse the balance suffix.
            let (xuid, bal) = line.rsplit_once(':')?;
            let bal = bal.trim().parse::<i64>().ok()?;
            Some((xuid.to_owned(), bal))
        })
        .collect()
}

// ── Helpers ───────────────────────────────────────────────────────────

fn check(ok: bool, msg: impl FnOnce() -> String) -> Result<()> {
    if ok {
        Ok(())
    } else {
        Err(Error(msg()))
    }
}

/// LLMoney takes seconds as `int` (`i32` on MSVC). Clamp instead of truncating
/// so a giant `Duration` doesn't silently become a small negative i32.
fn duration_secs_i32(d: Duration) -> i32 {
    d.as_secs().min(i32::MAX as u64) as i32
}
