//! `Server` event subscription and task scheduling.

use super::*;
use crate::error::{Error, Result};
use crate::event::{event_trampoline, EventCallback, EventPriority, EventRef, Listener};
use crate::ffi::s;
use crate::rt;
use std::time::Duration;

impl Server {
    /// Subscribe to a LeviLamina event by id. A unique suffix works
    /// (`"PlayerChatEvent"`); dump all ids in-game with `/levirs events`.
    /// Server thread only.
    pub fn subscribe_event(
        &self,
        event_id: &str,
        priority: EventPriority,
        callback: impl FnMut(&mut EventRef) + 'static,
    ) -> Result<Listener> {
        let cb: *mut EventCallback = Box::into_raw(Box::new(Box::new(callback)));
        let raw = unsafe {
            (rt().api.subscribe_event)(
                rt().handle,
                s(event_id),
                priority as i32,
                event_trampoline,
                cb.cast(),
            )
        };
        if raw.is_null() {
            unsafe { drop(Box::from_raw(cb)) };
            return Err(Error(format!(
                "failed to subscribe '{event_id}' (unknown or ambiguous id?)"
            )));
        }
        Ok(Listener::new(raw, cb))
    }

    /// Enumerate all registered event ids. Server thread only.
    pub fn list_events(&self) -> Vec<String> {
        crate::ffi::collect_strs(|ctx, sink| unsafe { (rt().api.list_events)(ctx, sink) })
    }

    /// Run a closure on the server thread ASAP. Thread-safe.
    ///
    /// The task is owned by this mod: if the mod is unloaded before the task
    /// runs, the task is dropped instead of jumping into the freed dylib.
    /// Returns a [`TaskId`] that can be passed to [`Server::cancel_task`].
    pub fn schedule(&self, f: impl FnOnce() + Send + 'static) -> TaskId {
        let boxed: *mut TaskOnce = Box::into_raw(Box::new(Some(Box::new(f))));
        let id = unsafe { (rt().api.schedule_for)(rt().handle, task_trampoline, boxed.cast()) };
        if id == 0 {
            // Rejected before registration, so the trampoline will never run
            // and never reclaim the box. Reclaim it here.
            unsafe { drop(Box::from_raw(boxed)) };
        }
        TaskId(id)
    }

    /// Run a closure on the server thread after `delay`. Thread-safe.
    ///
    /// Same ownership guarantee as [`Server::schedule`]. Note that the timer
    /// itself still expires on schedule after an unload — it just finds a dead
    /// ticket and does nothing.
    pub fn schedule_after(&self, delay: Duration, f: impl FnOnce() + Send + 'static) -> TaskId {
        let boxed: *mut TaskOnce = Box::into_raw(Box::new(Some(Box::new(f))));
        let id = unsafe {
            (rt().api.schedule_after_for)(
                rt().handle,
                task_trampoline,
                boxed.cast(),
                delay.as_millis() as u64,
            )
        };
        if id == 0 {
            unsafe { drop(Box::from_raw(boxed)) };
        }
        TaskId(id)
    }

    /// Drop a task this mod scheduled, if it has not run yet. Thread-safe.
    ///
    /// Returns `true` if a pending task was actually dropped. Cancelling leaks
    /// the closure (the loader cannot run this mod's drop glue from the other
    /// side of the FFI boundary), so prefer letting short tasks run.
    pub fn cancel_task(&self, id: TaskId) -> bool {
        if id.0 == 0 {
            return false;
        }
        unsafe { (rt().api.schedule_cancel)(rt().handle, id.0) }
    }

    /// How many tasks this mod still has queued. Thread-safe.
    ///
    /// A mod that wants `"reload_safe": true` in its manifest should assert
    /// this is zero by the end of `on_unload` — a pending task at that point
    /// is work the loader has to throw away.
    pub fn pending_tasks(&self) -> u32 {
        unsafe { (rt().api.schedule_pending_count)(rt().handle) }
    }
}
