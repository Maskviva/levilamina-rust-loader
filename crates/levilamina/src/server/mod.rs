use std::ffi::c_void;
use std::panic::{catch_unwind, AssertUnwindSafe};

use crate::logger::Logger;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum GamingStatus {
    Default,
    Starting,
    Running,
    Stopping,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Weather {
    Clear = 0,
    Rain = 1,
    Thunder = 2,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SoftEnumOp {
    Set = 0,
    Add = 1,
    Remove = 2,
}

pub(crate) type TaskOnce = Option<Box<dyn FnOnce() + Send>>;

pub use crate::runtime::TaskId;

pub(crate) unsafe extern "C" fn task_trampoline(user: *mut c_void) {
    let mut boxed: Box<TaskOnce> = Box::from_raw(user.cast());
    if let Some(f) = boxed.take() {
        if catch_unwind(AssertUnwindSafe(f)).is_err() {
            Logger::get().error("panic in scheduled task");
        }
    }
}

#[derive(Clone, Copy)]
pub struct Server(());

impl Server {
    pub fn get() -> Server {
        Server(())
    }
}

mod ops;
mod run;
pub use run::{fill, ticking};
mod sel;
pub(crate) use sel::dimsel;
mod world;
