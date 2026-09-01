use crate::error::{Error, Result};
use crate::event::{event_trampoline, EventCallback, EventPriority, EventRef, Listener};
use crate::ffi::s;
use crate::rt;
use crate::server::*;
use std::time::Duration;

impl Server {
    pub fn subscribe_event(
        &self,
        event_id: &str,
        priority: EventPriority,
        callback: impl FnMut(&mut EventRef) + 'static,
    ) -> Result<Listener> {
        let cb: *mut EventCallback = Box::into_raw(Box::new(Box::new(callback)));
        let raw = unsafe {
            (rt().api.subscribe_event)(
                rt().handle(),
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

    pub fn list_events(&self) -> Vec<String> {
        crate::ffi::collect_strs(|ctx, sink| unsafe { (rt().api.list_events)(ctx, sink) })
    }

    pub fn schedule(&self, f: impl FnOnce() + Send + 'static) -> TaskId {
        let boxed: *mut TaskOnce = Box::into_raw(Box::new(Some(Box::new(f))));
        let id = unsafe { (rt().api.schedule_for)(rt().handle(), task_trampoline, boxed.cast()) };
        if id == 0 {
            unsafe { drop(Box::from_raw(boxed)) };
        }
        TaskId(id)
    }

    pub fn schedule_after(&self, delay: Duration, f: impl FnOnce() + Send + 'static) -> TaskId {
        let boxed: *mut TaskOnce = Box::into_raw(Box::new(Some(Box::new(f))));
        let id = unsafe {
            (rt().api.schedule_after_for)(
                rt().handle(),
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

    pub fn cancel_task(&self, id: TaskId) -> bool {
        if id.0 == 0 {
            return false;
        }
        unsafe { (rt().api.schedule_cancel)(rt().handle(), id.0) }
    }

    pub fn pending_tasks(&self) -> u32 {
        unsafe { (rt().api.schedule_pending_count)(rt().handle()) }
    }
}
