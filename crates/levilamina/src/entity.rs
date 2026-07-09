//! Entity handles: `ActorUniqueID`s resolved via `Level::fetchEntity` on
//! every call. Obtain them from [`crate::Server::spawn_mob`],
//! [`Entity::list`], command selector args, or [`crate::Player::as_entity`].

use crate::error::{Error, Result};
use crate::ffi::{call_out_str, s};
use crate::nbt::NbtValue;
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

impl Entity {
    /// Wrap a raw ActorUniqueID (from a snapshot, a command selector arg, …).
    pub fn from_id(id: i64) -> Entity {
        Entity { id }
    }

    /// Enumerate live entities; `dimension = None` for all dimensions.
    pub fn list(dimension: Option<i32>) -> Vec<EntityId> {
        use std::ffi::c_void;
        unsafe extern "C" fn sink(ctx: *mut c_void, id: sys::LeviRsActorId, name: sys::LeviRsStr) {
            (*ctx.cast::<Vec<EntityId>>()).push(EntityId {
                id,
                type_name: crate::ffi::r(name).to_owned(),
            });
        }
        let mut out: Vec<EntityId> = Vec::new();
        unsafe {
            (rt().api.list_actors)(
                dimension.unwrap_or(-1),
                (&mut out as *mut Vec<EntityId>).cast(),
                sink,
            )
        };
        out
    }

    pub fn id(&self) -> i64 {
        self.id
    }

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

    /// Does the id currently resolve to a live entity?
    pub fn exists(&self) -> bool {
        let mut out = 0.0f64;
        unsafe { (rt().api.actor_get_num)(self.id, sys::APROP_IS_ALIVE, &mut out) }
    }

    /// Full `Actor::save` NBT snapshot as a structured value.
    pub fn snapshot(&self) -> Result<NbtValue> {
        let snbt =
            call_out_str(|ctx, sink| unsafe { (rt().api.actor_snapshot)(self.id, ctx, sink) })
                .ok_or_else(|| self.gone())?;
        NbtValue::parse(&snbt)
    }

    // ── properties ──

    pub fn type_name(&self) -> Result<String> {
        self.get_str(sys::ASTR_TYPE_NAME)
    }
    pub fn name_tag(&self) -> Result<String> {
        self.get_str(sys::ASTR_NAME_TAG)
    }
    pub fn pos(&self) -> Result<(f64, f64, f64)> {
        Ok((
            self.get_num(sys::APROP_POS_X)?,
            self.get_num(sys::APROP_POS_Y)?,
            self.get_num(sys::APROP_POS_Z)?,
        ))
    }
    /// (pitch, yaw) in degrees.
    pub fn rotation(&self) -> Result<(f64, f64)> {
        Ok((
            self.get_num(sys::APROP_ROT_PITCH)?,
            self.get_num(sys::APROP_ROT_YAW)?,
        ))
    }
    pub fn dimension_id(&self) -> Result<i32> {
        self.get_num(sys::APROP_DIMENSION).map(|v| v as i32)
    }
    pub fn health(&self) -> Result<i32> {
        self.get_num(sys::APROP_HEALTH).map(|v| v as i32)
    }
    pub fn max_health(&self) -> Result<i32> {
        self.get_num(sys::APROP_MAX_HEALTH).map(|v| v as i32)
    }
    /// Speed in meters per second.
    pub fn speed(&self) -> Result<f64> {
        self.get_num(sys::APROP_SPEED)
    }
    pub fn is_alive(&self) -> Result<bool> {
        self.get_num(sys::APROP_IS_ALIVE).map(|v| v != 0.0)
    }
    pub fn is_on_ground(&self) -> Result<bool> {
        self.get_num(sys::APROP_IS_ON_GROUND).map(|v| v != 0.0)
    }
    pub fn is_in_water(&self) -> Result<bool> {
        self.get_num(sys::APROP_IS_IN_WATER).map(|v| v != 0.0)
    }
    pub fn is_in_lava(&self) -> Result<bool> {
        self.get_num(sys::APROP_IS_IN_LAVA).map(|v| v != 0.0)
    }
    pub fn is_on_fire(&self) -> Result<bool> {
        self.get_num(sys::APROP_IS_ON_FIRE).map(|v| v != 0.0)
    }
    pub fn is_invisible(&self) -> Result<bool> {
        self.get_num(sys::APROP_IS_INVISIBLE).map(|v| v != 0.0)
    }
    pub fn is_sneaking(&self) -> Result<bool> {
        self.get_num(sys::APROP_IS_SNEAKING).map(|v| v != 0.0)
    }
    pub fn is_baby(&self) -> Result<bool> {
        self.get_num(sys::APROP_IS_BABY).map(|v| v != 0.0)
    }
    pub fn is_riding(&self) -> Result<bool> {
        self.get_num(sys::APROP_IS_RIDING).map(|v| v != 0.0)
    }
    pub fn is_tame(&self) -> Result<bool> {
        self.get_num(sys::APROP_IS_TAME).map(|v| v != 0.0)
    }

    // ── actions ──

    pub fn kill(&self) -> Result<()> {
        self.action(sys::AACT_KILL, "", 0.0, 0.0, 0.0).map(|_| ())
    }
    /// Remove without death animation or drops.
    pub fn despawn(&self) -> Result<()> {
        self.action(sys::AACT_DESPAWN, "", 0.0, 0.0, 0.0)
            .map(|_| ())
    }
    pub fn heal(&self, amount: i32) -> Result<()> {
        self.action(sys::AACT_HEAL, "", amount as f64, 0.0, 0.0)
            .map(|_| ())
    }
    pub fn set_on_fire(&self, seconds: i32) -> Result<()> {
        self.action(sys::AACT_SET_ON_FIRE, "", seconds as f64, 0.0, 0.0)
            .map(|_| ())
    }
    /// Teleport within the current dimension.
    pub fn teleport(&self, x: f64, y: f64, z: f64) -> Result<()> {
        self.action(sys::AACT_TELEPORT, "", x, y, z).map(|_| ())
    }
    /// Teleport across dimensions (0=overworld 1=nether 2=the_end).
    pub fn teleport_to_dimension(&self, dimension: i32, x: f64, y: f64, z: f64) -> Result<()> {
        self.action(sys::AACT_TELEPORT, &dimension.to_string(), x, y, z)
            .map(|_| ())
    }
    pub fn set_name_tag(&self, name: &str) -> Result<()> {
        self.action(sys::AACT_SET_NAME_TAG, name, 0.0, 0.0, 0.0)
            .map(|_| ())
    }
    pub fn add_tag(&self, tag: &str) -> Result<bool> {
        Ok(self
            .action(sys::AACT_ADD_TAG, tag, 0.0, 0.0, 0.0)?
            .as_deref()
            == Some("1"))
    }
    pub fn remove_tag(&self, tag: &str) -> Result<bool> {
        Ok(self
            .action(sys::AACT_REMOVE_TAG, tag, 0.0, 0.0, 0.0)?
            .as_deref()
            == Some("1"))
    }
    pub fn has_tag(&self, tag: &str) -> Result<bool> {
        Ok(self
            .action(sys::AACT_HAS_TAG, tag, 0.0, 0.0, 0.0)?
            .as_deref()
            == Some("1"))
    }
    /// `effect`: engine effect name ("speed", "regeneration", …).
    pub fn add_effect(
        &self,
        effect: &str,
        ticks: i32,
        amplifier: i32,
        visible: bool,
    ) -> Result<()> {
        self.action(
            sys::AACT_ADD_EFFECT,
            effect,
            ticks as f64,
            amplifier as f64,
            if visible { 1.0 } else { 0.0 },
        )
        .map(|_| ())
    }
    pub fn remove_effect(&self, effect: &str) -> Result<()> {
        self.action(sys::AACT_REMOVE_EFFECT, effect, 0.0, 0.0, 0.0)
            .map(|_| ())
    }
    pub fn clear_effects(&self) -> Result<()> {
        self.action(sys::AACT_CLEAR_EFFECTS, "", 0.0, 0.0, 0.0)
            .map(|_| ())
    }
    /// Deal generic damage. v1.0.0 limitation: player targets only (routed
    /// via `/damage`); other entities return `Err`.
    pub fn hurt(&self, amount: i32) -> Result<()> {
        self.action(sys::AACT_HURT, "", amount as f64, 0.0, 0.0)
            .map(|_| ())
    }
}
