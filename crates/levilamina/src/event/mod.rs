//! Event subscription: RAII [`Listener`], [`EventRef`] with both the raw
//! SNBT view (v0.x compatible) and the structured [`NbtValue`] editor.

use std::ffi::c_void;
use std::panic::{catch_unwind, AssertUnwindSafe};

use crate::ffi::{r, s};
use crate::logger::Logger;
use crate::nbt::NbtValue;
use crate::{rt, sys};

/// Mirrors `ll::event::EventPriority`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum EventPriority {
    Highest = 0,
    High = 1,
    Normal = 2,
    Low = 3,
    Lowest = 4,
}

/// A live event subscription. Dropping it unsubscribes (RAII);
/// call [`Listener::forget`] to keep it for the lifetime of the mod.
pub struct Listener {
    raw: sys::LeviRsListenerHandle,
    cb: *mut EventCallback,
}

pub(crate) type EventCallback = Box<dyn FnMut(&mut EventRef)>;

impl Listener {
    pub(crate) fn new(raw: sys::LeviRsListenerHandle, cb: *mut EventCallback) -> Listener {
        Listener { raw, cb }
    }

    /// Keep this subscription alive forever (leaks the callback — fine for
    /// listeners that should live as long as the mod).
    pub fn forget(mut self) {
        self.raw = std::ptr::null_mut();
        self.cb = std::ptr::null_mut();
        std::mem::forget(self);
    }
}

impl Drop for Listener {
    fn drop(&mut self) {
        if !self.raw.is_null() {
            let rt = rt();
            unsafe {
                (rt.api.unsubscribe_event)(rt.handle, self.raw);
                drop(Box::from_raw(self.cb)); // bridge won't call it anymore
            }
        }
    }
}

/// The `_player` identity block the bridge splices into player-carrying
/// events (and command-event payloads).
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct PlayerIdentity {
    pub name: String,
    pub xuid: String,
    pub uuid: String,
}

/// Event data handed to event callbacks: the event's CompoundTag as SNBT.
pub struct EventRef<'a> {
    id: &'a str,
    snbt: &'a str,
    replacement: Option<String>,
}

impl<'a> EventRef<'a> {
    pub(crate) fn new(id: &'a str, snbt: &'a str) -> EventRef<'a> {
        EventRef {
            id,
            snbt,
            replacement: None,
        }
    }

    pub(crate) fn take_replacement(self) -> Option<String> {
        self.replacement
    }

    /// Full event id, e.g. `ll::event::PlayerChatEvent`.
    pub fn id(&self) -> &str {
        self.id
    }

    /// Event data as SNBT (see `/levirs events` + LeviLamina event docs for fields).
    pub fn snbt(&self) -> &str {
        self.snbt
    }

    /// Parse the event data into a structured [`NbtValue`] (pending edits
    /// included, so `value → edit → set_value` chains compose).
    pub fn value(&self) -> crate::Result<NbtValue> {
        NbtValue::parse(self.replacement.as_deref().unwrap_or(self.snbt))
    }

    /// The `_player` identity block, if the bridge attached one.
    pub fn player(&self) -> Option<PlayerIdentity> {
        let v = self.value().ok()?;
        let p = v.get("_player")?;
        Some(PlayerIdentity {
            name: p.get("name")?.as_str()?.to_owned(),
            xuid: p
                .get("xuid")
                .and_then(|x| x.as_str())
                .unwrap_or("")
                .to_owned(),
            uuid: p
                .get("uuid")
                .and_then(|x| x.as_str())
                .unwrap_or("")
                .to_owned(),
        })
    }

    /// Look up the resolved [`crate::Player`] handle for the event's player.
    pub fn player_handle(&self) -> Option<crate::Player> {
        let ident = self.player()?;
        if !ident.xuid.is_empty() {
            Some(crate::Player::by_xuid(ident.xuid))
        } else {
            Some(crate::Player::by_name(ident.name))
        }
    }

    /// Replace the event data wholesale; the bridge deserializes it back into
    /// the event, which is how fields are edited and cancellable events cancelled.
    pub fn set_snbt(&mut self, snbt: impl Into<String>) {
        self.replacement = Some(snbt.into());
    }

    /// Structured write-back: serialize `value` and stage it as the new data.
    pub fn set_value(&mut self, value: &NbtValue) {
        self.replacement = Some(value.to_snbt());
    }

    /// Cancel a cancellable event. v1.0.0: structured — parse, set
    /// `cancelled = 1b`, serialize; falls back to the v0.x textual flip if
    /// the payload doesn't parse.
    pub fn cancel(&mut self) {
        match self.value() {
            Ok(mut v) => {
                if v.insert("cancelled", NbtValue::Byte(1)) {
                    self.replacement = Some(v.to_snbt());
                    return;
                }
            }
            Err(_) => {}
        }
        let base = self.replacement.as_deref().unwrap_or(self.snbt);
        if base.contains("cancelled:0b") {
            self.replacement = Some(base.replace("cancelled:0b", "cancelled:1b"));
        }
    }
}

pub(crate) unsafe extern "C" fn event_trampoline(
    user: *mut c_void,
    event_id: sys::LeviRsStr,
    snbt: sys::LeviRsStr,
    write_ctx: *mut c_void,
    write_back: sys::LeviRsStrSink,
) {
    let cb = &mut *user.cast::<EventCallback>();
    let mut ev = EventRef::new(r(event_id), r(snbt));
    if catch_unwind(AssertUnwindSafe(|| cb(&mut ev))).is_err() {
        Logger::get().error("panic in event handler");
        return;
    }
    if let Some(new_snbt) = ev.take_replacement() {
        write_back(write_ctx, s(&new_snbt));
    }
}

/// Corrected event ids (verified against the pinned LeviLamina headers),
/// grouped by domain (`names::player`, `names::mob`, `names::server`) with
/// every constant also re-exported flat (`names::PLAYER_CHAT`) for
/// backward compatibility.
pub mod names;
