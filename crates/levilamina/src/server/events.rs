//! `Server` event subscription and task scheduling.

use super::*;
use crate::error::{Error, Result};
use crate::event::{event_trampoline, EventCallback, EventPriority, EventRef, Listener};
use crate::ffi::{collect_strs, s};
use crate::{rt, sys};
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
    pub fn schedule(&self, f: impl FnOnce() + Send + 'static) {
        let boxed: *mut TaskOnce = Box::into_raw(Box::new(Some(Box::new(f))));
        unsafe { (rt().api.schedule)(task_trampoline, boxed.cast()) }
    }

    /// Run a closure on the server thread after `delay`. Thread-safe.
    pub fn schedule_after(&self, delay: Duration, f: impl FnOnce() + Send + 'static) {
        let boxed: *mut TaskOnce = Box::into_raw(Box::new(Some(Box::new(f))));
        unsafe {
            (rt().api.schedule_after)(task_trampoline, boxed.cast(), delay.as_millis() as u64)
        }
    }
}
