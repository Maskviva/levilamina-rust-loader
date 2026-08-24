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
pub struct Client(());

impl Client {
    pub fn get() -> Client {
        Client(())
    }
}

mod events;
mod input;
mod status;

pub use input::{KeyAction, KeyBinding};

#[derive(Debug, Clone, Copy)]
pub struct ClientInstance;
