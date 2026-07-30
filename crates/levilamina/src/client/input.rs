//! Client input: key binding registration. Wraps `ll::input::KeyRegistry`.

use std::ffi::c_void;
use std::panic::{catch_unwind, AssertUnwindSafe};

use crate::client::Client;
use crate::ffi::{call_out_str, s};
use crate::logger::Logger;
use crate::{rt, sys};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum KeyAction {
    Up = 0,
    Down = 1,
}

impl KeyAction {
    pub fn is_down(&self) -> bool {
        matches!(self, KeyAction::Down)
    }
}

type Cb = Box<dyn Fn(KeyAction) + Send + 'static>;

struct KeyUserData {
    down: Option<Cb>,
    up: Option<Cb>,
}

/// Registered key binding. Drop to unregister.
pub struct KeyBinding {
    handle: sys::LeviRsKeyHandle,
}

impl KeyBinding {
    /// `down` / `up` fire on the client thread. Either may be `None`.
    pub fn register(
        _client: &Client,
        name: &str,
        key_codes: &[i32],
        allow_remap: bool,
        down: Option<impl Fn(KeyAction) + Send + 'static>,
        up: Option<impl Fn(KeyAction) + Send + 'static>,
    ) -> Option<KeyBinding> {
        let user = Box::into_raw(Box::new(KeyUserData {
            down: down.map(|f| Box::new(f) as Cb),
            up: up.map(|f| Box::new(f) as Cb),
        }));

        let handle = unsafe {
            (rt().api.client_register_key)(
                rt().handle,
                s(name),
                key_codes.as_ptr(),
                key_codes.len() as i32,
                allow_remap,
                key_down_trampoline,
                key_up_trampoline,
                user as *mut c_void,
            )
        };

        if handle.is_null() {
            unsafe { drop(Box::from_raw(user)) };
            None
        } else {
            Some(KeyBinding { handle })
        }
    }

    pub fn key_codes(&self) -> Vec<i32> {
        call_out_str(|ctx, sink| unsafe { (rt().api.client_get_key_codes)(self.handle, ctx, sink) })
            .and_then(|s| {
                if s.is_empty() {
                    None
                } else {
                    s.trim_matches(|c| c == '[' || c == ']')
                        .split(',')
                        .filter_map(|n| n.trim().parse::<i32>().ok())
                        .collect::<Vec<_>>()
                        .into()
                }
            })
            .unwrap_or_default()
    }
}

impl Drop for KeyBinding {
    fn drop(&mut self) {
        unsafe {
            (rt().api.client_unregister_key)(self.handle);
        }
    }
}

unsafe extern "C" fn key_down_trampoline(user: *mut c_void, action: i32, _impact: i32) {
    fire(user, action);
}

unsafe extern "C" fn key_up_trampoline(user: *mut c_void, action: i32, _impact: i32) {
    fire(user, action);
}

unsafe fn fire(user: *mut c_void, action: i32) {
    let user_ref = &*(user as *const KeyUserData);
    let ka = if action == 1 {
        KeyAction::Down
    } else {
        KeyAction::Up
    };
    let cb = if ka.is_down() {
        &user_ref.down
    } else {
        &user_ref.up
    };
    if let Some(f) = cb.as_ref() {
        if catch_unwind(AssertUnwindSafe(|| f(ka))).is_err() {
            Logger::get().error("panic in key callback");
        }
    }
}
