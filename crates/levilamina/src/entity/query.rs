//! Read-only entity queries.

use super::*;
use crate::error::Result;
use crate::ffi::call_out_str;
use crate::nbt::NbtValue;
use crate::{rt, sys};
use crate::types::PositionF64;

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

    pub fn type_name(&self) -> Result<String> {
        self.get_str(sys::ASTR_TYPE_NAME)
    }

    pub fn name_tag(&self) -> Result<String> {
        self.get_str(sys::ASTR_NAME_TAG)
    }

    pub fn pos(&self) -> Result<PositionF64> {
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
}
