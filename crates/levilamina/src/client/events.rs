//! Client event subscription. Reuses the same `subscribe_event` FFI as
//! server events; only the event-id strings differ.

use crate::client::Client;
use crate::error::{Error, Result};
use crate::event::{event_trampoline, EventCallback, EventPriority, EventRef, Listener};
use crate::ffi::s;
use crate::rt;

/// Canonical client event IDs (mirrors `ll::event::client::*` class names).
pub mod events {
    pub const CLIENT_START_JOIN_LEVEL: &str = "ClientStartJoinLevelEvent";
    pub const CLIENT_JOIN_LEVEL: &str = "ClientJoinLevelEvent";
    pub const CLIENT_EXIT_LEVEL: &str = "ClientExitLevelEvent";
    pub const CLIENT_CANCEL_JOIN_LEVEL: &str = "ClientCancelJoinLevelEvent";
    pub const CLIENT_LEVEL_TICK: &str = "ClientLevelTickEvent";
    pub const KEY_INPUT: &str = "KeyInputEvent";
    pub const MOUSE_INPUT: &str = "MouseInputEvent";
    pub const BEFORE_UI_RENDER: &str = "BeforeUIRenderEvent";
    pub const AFTER_UI_RENDER: &str = "AfterUIRenderEvent";
    pub const CLIENT_COMMAND_REGISTER: &str = "ClientCommandRegisterEvent";
}

impl Client {
    /// Client thread only. Returns a [`Listener`] that unsubscribes on drop.
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

    /// Client thread only.
    pub fn list_events(&self) -> Vec<String> {
        crate::ffi::collect_strs(|ctx, sink| unsafe { (rt().api.list_events)(ctx, sink) })
    }
}
