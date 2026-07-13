//! Entity handles: `ActorUniqueID`s resolved via `Level::fetchEntity` on
//! every call. Obtain them from [`crate::Server::spawn_mob`],
//! [`Entity::list`], command selector args, or [`crate::Player::as_entity`].

use crate::error::{Error, Result};
use crate::ffi::{call_out_str, s};
use crate::{rt, sys};

/// A `(id, type_name)` pair from [`Entity::list`].
#[derive(Debug, Clone)]
pub struct EntityId {
    pub id: i64,
    pub type_name: String,
}

/// An entity handle (ActorUniqueID). Copy-cheap; can never dangle.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct Entity {
    id: sys::LeviRsActorId,
}

mod actions;
mod query;

impl Entity {
    fn gone(&self) -> Error {
        Error(format!(
            "entity {} not found (despawned or unloaded)",
            self.id
        ))
    }

    fn get_num(&self, prop: i32) -> Result<f64> {
        let mut out = 0.0f64;
        let ok = unsafe { (rt().api.actor_get_num)(self.id, prop, &mut out) };
        if ok {
            Ok(out)
        } else {
            Err(self.gone())
        }
    }

    fn get_str(&self, prop: i32) -> Result<String> {
        call_out_str(|ctx, sink| unsafe { (rt().api.actor_get_str)(self.id, prop, ctx, sink) })
            .ok_or_else(|| self.gone())
    }

    fn action(&self, action: i32, sarg: &str, a: f64, b: f64, c: f64) -> Result<Option<String>> {
        let mut out: Option<String> = None;
        let ok = unsafe {
            (rt().api.actor_action)(
                self.id,
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
            Err(Error("entity gone or action unsupported".into()))
        }
    }
}
