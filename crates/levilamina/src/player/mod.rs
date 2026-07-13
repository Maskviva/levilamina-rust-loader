//! Player handles: selectors (name / xuid / uuid) resolved against the live
//! player list on every call — never cached pointers.

use crate::error::{Error, Result};
use crate::ffi::{call_out_str, collect_strs, s};
use crate::nbt::NbtValue;
use crate::{rt, sys};

mod actions;
mod inventory;
mod query;
mod types;

pub use types::{Ability, GameMode, MessageType, PlayerInfo};

#[derive(Debug, Clone)]
enum Selector {
    Name(String),
    Xuid(String),
    Uuid(String),
}

/// A player handle. Cheap to clone; resolved on every call, so it can never
/// dangle — calls after the player leaves simply return `Err`.
#[derive(Debug, Clone)]
pub struct Player {
    sel: Selector,
}

impl Player {
    /// By account name (exact `getRealName()` match, falling back to the
    /// display name). Prefer [`Player::by_xuid`] for long-lived storage.
    pub fn by_name(name: impl Into<String>) -> Player {
        Player {
            sel: Selector::Name(name.into()),
        }
    }

    /// Alias for [`Player::by_name`], matching the docs' `Player::get`.
    pub fn get(name: impl Into<String>) -> Player {
        Player::by_name(name)
    }

    pub fn by_xuid(xuid: impl Into<String>) -> Player {
        Player {
            sel: Selector::Xuid(xuid.into()),
        }
    }

    pub fn by_uuid(uuid: impl Into<String>) -> Player {
        Player {
            sel: Selector::Uuid(uuid.into()),
        }
    }

    /// Every online player: identity + position.
    pub fn list() -> Vec<PlayerInfo> {
        let lines = collect_strs(|ctx, sink| unsafe { (rt().api.list_players)(ctx, sink) });
        lines
            .iter()
            .filter_map(|line| {
                let v = NbtValue::parse(line).ok()?;
                Some(PlayerInfo {
                    name: v.get("name")?.as_str()?.to_owned(),
                    xuid: v
                        .get("xuid")
                        .and_then(|x| x.as_str())
                        .unwrap_or("")
                        .to_owned(),
                    uuid: v
                        .get("uuid")
                        .and_then(|x| x.as_str())
                        .unwrap_or("")
                        .to_owned(),
                    dimension: v.get("dim").and_then(|x| x.as_i64()).unwrap_or(0) as i32,
                    pos: (
                        v.get("x").and_then(|x| x.as_f64()).unwrap_or(0.0),
                        v.get("y").and_then(|x| x.as_f64()).unwrap_or(0.0),
                        v.get("z").and_then(|x| x.as_f64()).unwrap_or(0.0),
                    ),
                })
            })
            .collect()
    }

    /// `sendMessage` to every online player.
    pub fn broadcast(msg: &str) {
        unsafe { (rt().api.broadcast_message)(s(msg)) }
    }

    pub(crate) fn ffi_sel(&self) -> sys::LeviRsPlayerSel {
        let (kind, value) = match &self.sel {
            Selector::Name(v) => (0, v.as_str()),
            Selector::Xuid(v) => (1, v.as_str()),
            Selector::Uuid(v) => (2, v.as_str()),
        };
        sys::LeviRsPlayerSel {
            kind,
            value: s(value),
        }
    }

    fn gone(&self) -> Error {
        Error(format!("player not online: {:?}", self.sel))
    }

    fn get_num(&self, prop: i32) -> Result<f64> {
        let mut out = 0.0f64;
        let ok = unsafe { (rt().api.player_get_num)(self.ffi_sel(), prop, &mut out) };
        if ok {
            Ok(out)
        } else {
            Err(self.gone())
        }
    }

    fn get_str(&self, prop: i32) -> Result<String> {
        call_out_str(|ctx, sink| unsafe {
            (rt().api.player_get_str)(self.ffi_sel(), prop, ctx, sink)
        })
        .ok_or_else(|| self.gone())
    }

    fn set_num(&self, prop: i32, v: f64) -> Result<()> {
        let ok = unsafe { (rt().api.player_set_num)(self.ffi_sel(), prop, v) };
        if ok {
            Ok(())
        } else {
            Err(Error("player offline or property not settable".into()))
        }
    }

    fn action(&self, action: i32, sarg: &str, a: f64, b: f64, c: f64) -> Result<Option<String>> {
        let mut out: Option<String> = None;
        let ok = unsafe {
            (rt().api.player_action)(
                self.ffi_sel(),
                action,
                s(sarg),
                a,
                b,
                c,
                (&mut out as *mut Option<String>).cast(),
                crate::ffi::set_string,
            )
        };
        if ok {
            Ok(out)
        } else {
            Err(Error("player offline or action unsupported".into()))
        }
    }
}
