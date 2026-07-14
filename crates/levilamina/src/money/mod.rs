//! Economy integration through the LLMoney plugin.
//!
//! LLMoney (the `LegacyMoney` mod) is an **optional** backend. When it isn't
//! installed or enabled, every operation degrades gracefully instead of
//! crashing: reads return `0` / empty strings, writes return `Err(...)`, and
//! listener registration is a silent no-op. The loader logs a one-time
//! warning on the server console the first time any money call finds the
//! backend missing (telling the operator to install/enable `LegacyMoney`), so
//! a failing `Err` here is expected — not a bug — on a server without it.
//!
//! # Threading
//! LLMoney runs on the server thread; register and mutate balances there.
//! Callbacks always fire on the server thread as well.
//!
//! # Single-slot events
//! LLMoney holds **one** before-callback and **one** after-callback total for
//! the whole process (not per-listener). Registering a second callback of
//! the same kind replaces the first — that's why [`on_before`] and
//! [`on_after`] return a plain guard whose `Drop` clears the slot rather
//! than a per-listener handle. Call `.forget()` on the guard to keep the
//! callback for the mod's lifetime.
//!
//! # Example
//! ```no_run
//! use levilamina::money;
//!
//! // Read + write
//! let bal = money::get("2535400000000000");
//! money::add("2535400000000000", 100)?;
//!
//! // Log every change; keep the listener alive for the whole mod
//! money::on_after(|ev| {
//!     println!("{:?} {} -> {}: {}", ev.kind, ev.from, ev.to, ev.amount);
//! }).forget();
//! # Ok::<(), levilamina::Error>(())
//! ```

mod listen;
mod ops;

pub use listen::{on_after, on_before, AfterGuard, BeforeGuard};
pub use ops::{
    add, clear_all_history, clear_history_older_than, get, history, ranking, reduce, set, transfer,
};

/// The kind of change dispatched to an [`on_before`] / [`on_after`] handler.
/// Alias for the raw `sys::LLMoneyEvent`.
pub use crate::sys::LLMoneyEvent as MoneyEventKind;

/// A single money-change event handed to a callback. All string slices are
/// borrowed from the FFI call frame; `to_owned()` them if you need to keep
/// them past the callback's return.
#[derive(Debug)]
pub struct MoneyEvent<'a> {
    /// Which operation triggered this event.
    pub kind: MoneyEventKind,
    /// Payer XUID for `Trans`, target XUID for `Set` / `Add` / `Reduce`
    /// (LLMoney's own convention — `from` is always populated).
    pub from: &'a str,
    /// Recipient XUID for `Trans`, empty otherwise.
    pub to: &'a str,
    /// Amount in LLMoney's smallest unit. For `Set` this is the new balance;
    /// for `Add` / `Reduce` / `Trans` it's the delta.
    pub amount: i64,
}
