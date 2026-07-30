//! Client status, local player, and scheduling.

use std::time::Duration;

use crate::client::{task_trampoline, Client, GamingStatus, TaskOnce};
use crate::error::Result;
use crate::ffi::call_out_str;
use crate::player::Player;
use crate::rt;

impl Client {
    /// Thread-safe.
    pub fn gaming_status(&self) -> GamingStatus {
        let raw = unsafe { (rt().api.gaming_status)() };
        match raw {
            1 => GamingStatus::Starting,
            2 => GamingStatus::Running,
            3 => GamingStatus::Stopping,
            _ => GamingStatus::Default,
        }
    }

    pub fn is_in_level(&self) -> bool {
        unsafe { (rt().api.client_is_in_level)() }
    }

    pub fn local_player(&self) -> Result<Player> {
        let name =
            call_out_str(|ctx, sink| unsafe { (rt().api.client_get_local_player)(ctx, sink) })
                .ok_or_else(|| {
                    crate::error::Error("not in level or local player unavailable".into())
                })?;
        Ok(Player::by_name(name))
    }

    pub fn screen_name(&self) -> Result<String> {
        call_out_str(|ctx, sink| unsafe { (rt().api.client_get_screen_name)(ctx, sink) })
            .ok_or_else(|| crate::error::Error("screen name unavailable".into()))
    }

    /// Thread-safe: marshals work onto the client thread.
    pub fn schedule<F>(&self, f: F)
    where
        F: FnOnce() + Send + 'static,
    {
        let boxed: Box<TaskOnce> = Box::new(Some(Box::new(f)));
        unsafe {
            (rt().api.schedule)(task_trampoline, Box::into_raw(boxed).cast());
        }
    }

    /// Thread-safe.
    pub fn schedule_after<F>(&self, delay: Duration, f: F)
    where
        F: FnOnce() + Send + 'static,
    {
        let boxed: Box<TaskOnce> = Box::new(Some(Box::new(f)));
        unsafe {
            (rt().api.schedule_after)(
                task_trampoline,
                Box::into_raw(boxed).cast(),
                delay.as_millis() as u64,
            );
        }
    }

    pub fn current_tick(&self) -> u64 {
        unsafe { (rt().api.get_current_tick)() }
    }

    pub fn tick_delta_time(&self) -> f64 {
        unsafe { (rt().api.get_tick_delta_time)() }
    }
}
