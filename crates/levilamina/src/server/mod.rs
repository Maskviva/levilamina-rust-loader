//! The [`Server`] facade: status, scheduling, events, commands, clock,
//! weather, difficulty, game rules, world read/write, spawning, and server
//! info.
//!
//! The surface is split by concern across the sibling modules (`status`,
//! `commands`, `events`, `world`, `time`, `tick`, `profiler`, `sim`, `data`);
//! each adds methods to the single `impl Server` defined here. This module
//! owns the shared types (`Server`, `GamingStatus`, `Weather`) and scheduling
//! plumbing.

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

/// Weather states for [`Server::set_weather`].
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Weather {
    Clear = 0,
    Rain = 1,
    Thunder = 2,
}

/// How [`Server::update_command_soft_enum`] changes the value set.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SoftEnumOp {
    /// Replace the whole value set.
    Set = 0,
    Add = 1,
    Remove = 2,
}

pub(crate) type TaskOnce = Option<Box<dyn FnOnce() + Send>>;

pub(crate) unsafe extern "C" fn task_trampoline(user: *mut c_void) {
    let mut boxed: Box<TaskOnce> = Box::from_raw(user.cast());
    if let Some(f) = boxed.take() {
        if catch_unwind(AssertUnwindSafe(f)).is_err() {
            Logger::get().error("panic in scheduled task");
        }
    }
}

/// Handle to the server. All methods must be called on the server thread,
/// except [`Server::schedule`], [`Server::schedule_after`] and
/// [`Server::gaming_status`], which are thread-safe.
#[derive(Clone, Copy)]
pub struct Server(());

impl Server {
    /// Thread-safe accessor for use from background threads (Tokio, etc.).
    pub fn get() -> Server {
        Server(())
    }
}

mod commands;
mod data;
mod events;
mod profiler;
mod sim;
mod status;
mod tick;
mod time;
mod world;
