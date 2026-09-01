use std::ffi::c_void;
use std::panic::{catch_unwind, AssertUnwindSafe};

use crate::ffi::{r, s};
use crate::logger::Logger;
use crate::nbt::NbtValue;
use crate::{rt, sys};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum EventPriority {
    Highest = 0,
    High = 1,
    Normal = 2,
    Low = 3,
    Lowest = 4,
}

pub struct Listener {
    raw: sys::LeviRsListenerHandle,
    cb: *mut EventCallback,
}

pub(crate) type EventCallback = Box<dyn FnMut(&mut EventRef)>;

impl Listener {
    pub(crate) fn new(raw: sys::LeviRsListenerHandle, cb: *mut EventCallback) -> Listener {
        Listener { raw, cb }
    }

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
                (rt.api.unsubscribe_event)(rt.handle(), self.raw);
                drop(Box::from_raw(self.cb));
            }
        }
    }
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct PlayerIdentity {
    pub name: String,
    pub xuid: String,
    pub uuid: String,
}

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

    pub fn id(&self) -> &str {
        self.id
    }

    pub fn snbt(&self) -> &str {
        self.snbt
    }

    pub fn value(&self) -> crate::Result<NbtValue> {
        NbtValue::parse(self.replacement.as_deref().unwrap_or(self.snbt))
    }

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

    pub fn player_handle(&self) -> Option<crate::Player> {
        let ident = self.player()?;
        if !ident.xuid.is_empty() {
            Some(crate::Player::by_xuid(ident.xuid))
        } else {
            Some(crate::Player::by_name(ident.name))
        }
    }

    pub fn set_snbt(&mut self, snbt: impl Into<String>) {
        self.replacement = Some(snbt.into());
    }

    pub fn set_value(&mut self, value: &NbtValue) {
        self.replacement = Some(value.to_snbt());
    }

    pub fn cancel(&mut self) {
        if let Ok(mut v) = self.value() {
            if v.insert("cancelled", NbtValue::Byte(1)) {
                self.replacement = Some(v.to_snbt());
                return;
            }
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

pub mod names;
