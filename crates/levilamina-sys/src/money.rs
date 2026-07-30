//! LLMoney types — mirrors `LeviRsApi::LLMoneyEvent` and `LLMoneyCallback`.

use crate::types::LeviRsStr;

/// LLMoney event kind — mirrors C++ `LeviRsApi::LLMoneyEvent`.
#[repr(i32)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum LLMoneyEvent {
    Set = 0,
    Add = 1,
    Reduce = 2,
    Trans = 3,
}

/// LLMoney before/after event callback. Returning `false` from a
/// before-event handler cancels the operation; the after-event handler's
/// return is ignored by the loader.
pub type LLMoneyCallback =
    unsafe extern "C" fn(type_: LLMoneyEvent, from: LeviRsStr, to: LeviRsStr, value: i64) -> bool;
