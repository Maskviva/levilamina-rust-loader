//! Client facade. All callbacks run on the **client thread**;
//! `schedule` / `schedule_after` are thread-safe.

use std::ffi::c_void;
use std::panic::{catch_unwind, AssertUnwindSafe};

use crate::logger::Logger;

/// Mirrors `ll::GamingStatus`.
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

/// Handle to the client. Methods run on the client thread unless noted.
#[derive(Clone, Copy)]
pub struct Client(());

impl Client {
    /// Thread-safe accessor.
    pub fn get() -> Client {
        Client(())
    }
}

mod events;
mod input;
mod status;

/// Registered key binding. Drop to unregister.
pub use input::{KeyAction, KeyBinding};

/// Marker for the local `ClientInstance` (held inside the bridge).
#[derive(Debug, Clone, Copy)]
pub struct ClientInstance;
