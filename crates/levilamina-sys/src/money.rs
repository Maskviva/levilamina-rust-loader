use crate::types::LeviRsStr;

#[repr(i32)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum LLMoneyEvent {
    Set = 0,
    Add = 1,
    Reduce = 2,
    Trans = 3,
}

pub type LLMoneyCallback =
    unsafe extern "C" fn(type_: LLMoneyEvent, from: LeviRsStr, to: LeviRsStr, value: i64) -> bool;
