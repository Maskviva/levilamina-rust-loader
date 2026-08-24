mod listen;
mod ops;

pub use listen::{on_after, on_before, AfterGuard, BeforeGuard};
pub use ops::{
    add, clear_all_history, clear_history_older_than, get, history, ranking, reduce, set, transfer,
};

pub use crate::sys::LLMoneyEvent as MoneyEventKind;

#[derive(Debug)]
pub struct MoneyEvent<'a> {
    pub kind: MoneyEventKind,

    pub from: &'a str,

    pub to: &'a str,

    pub amount: i64,
}
