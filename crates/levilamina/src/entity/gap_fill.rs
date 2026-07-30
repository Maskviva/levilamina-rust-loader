//! Entity gap-fill API: relationships, equipment, effects, geometry (v5).

use super::*;
use crate::error::Result;
use crate::ffi::{call_out_str, s};
use crate::rt;

impl Entity {
    /// The vehicle this entity is riding, if any.
    pub fn vehicle(&self) -> Result<Entity> {
        let mut out: sys::LeviRsActorId = 0;
        let ok = unsafe { (rt().api.actor_get_vehicle)(self.id, &mut out) };
        if ok {
            Ok(Entity { id: out })
        } else {
            Err(self.gone())
        }
    }

    /// The first passenger riding this entity, if any.
    pub fn first_passenger(&self) -> Result<Entity> {
        let mut out: sys::LeviRsActorId = 0;
        let ok = unsafe { (rt().api.actor_get_first_passenger)(self.id, &mut out) };
        if ok {
            Ok(Entity { id: out })
        } else {
            Err(self.gone())
        }
    }

    /// The owner of this entity (tamed mob, etc.), if any.
    pub fn owner(&self) -> Result<Entity> {
        let mut out: sys::LeviRsActorId = 0;
        let ok = unsafe { (rt().api.actor_get_owner)(self.id, &mut out) };
        if ok {
            Ok(Entity { id: out })
        } else {
            Err(self.gone())
        }
    }

    /// The current attack target, if any.
    pub fn target(&self) -> Result<Entity> {
        let mut out: sys::LeviRsActorId = 0;
        let ok = unsafe { (rt().api.actor_get_target)(self.id, &mut out) };
        if ok {
            Ok(Entity { id: out })
        } else {
            Err(self.gone())
        }
    }

    /// Equipped item at `slot` as SNBT.
    /// slot: 0=mainhand 1=offhand 2=helmet 3=chestplate 4=leggings 5=boots
    pub fn equipped_item(&self, slot: i32) -> Result<String> {
        call_out_str(|ctx, sink| unsafe {
            (rt().api.actor_get_equipped_item)(self.id, slot, ctx, sink)
        })
        .ok_or_else(|| self.gone())
    }

    /// Replace the equipped item at `slot` with an SNBT stack.
    pub fn set_equipped_item(&self, slot: i32, item_snbt: &str) -> Result<()> {
        let ok = unsafe { (rt().api.actor_set_equipped_item)(self.id, slot, s(item_snbt)) };
        if ok {
            Ok(())
        } else {
            Err(self.gone())
        }
    }

    /// Active mob effects as SNBT: `[{id, ticks, amplifier, visible}, …]`
    pub fn effects(&self) -> Result<String> {
        call_out_str(|ctx, sink| unsafe { (rt().api.actor_get_effects)(self.id, ctx, sink) })
            .ok_or_else(|| self.gone())
    }

    /// Get an ActorStatus flag by index (0-based).
    pub fn status_flag(&self, flag_index: i32) -> Result<bool> {
        let ok = unsafe { (rt().api.actor_get_status_flag)(self.id, flag_index) };
        Ok(ok)
    }

    /// Set an ActorStatus flag by index (0-based).
    pub fn set_status_flag(&self, flag_index: i32, value: bool) -> Result<()> {
        let ok = unsafe { (rt().api.actor_set_status_flag)(self.id, flag_index, value) };
        if ok {
            Ok(())
        } else {
            Err(self.gone())
        }
    }

    /// Trace a ray from the entity's eye; returns SNBT
    /// `{type:"entity"|"block"|"none", pos:[x,y,z], entity_id?, block_name?}`
    pub fn trace_ray(
        &self,
        max_dist: f32,
        include_actors: bool,
        include_blocks: bool,
    ) -> Result<String> {
        call_out_str(|ctx, sink| unsafe {
            (rt().api.actor_trace_ray)(self.id, max_dist, include_actors, include_blocks, ctx, sink)
        })
        .ok_or_else(|| self.gone())
    }

    /// Distance to another entity.
    pub fn distance_to(&self, other: Entity) -> Result<f64> {
        let mut out = 0.0f64;
        let ok = unsafe { (rt().api.actor_distance_to)(self.id, other.id, &mut out) };
        if ok {
            Ok(out)
        } else {
            Err(self.gone())
        }
    }

    /// Axis-aligned bounding box as SNBT: `{min:[x,y,z], max:[x,y,z]}`
    pub fn aabb(&self) -> Result<String> {
        call_out_str(|ctx, sink| unsafe { (rt().api.actor_get_aabb)(self.id, ctx, sink) })
            .ok_or_else(|| self.gone())
    }

    /// Clone this entity at the given position; returns the new entity's id.
    pub fn clone_at(&self, dim: i32, x: f64, y: f64, z: f64) -> Result<Entity> {
        let mut out: sys::LeviRsActorId = 0;
        let ok = unsafe { (rt().api.actor_clone)(self.id, dim, x, y, z, &mut out) };
        if ok {
            Ok(Entity { id: out })
        } else {
            Err(self.gone())
        }
    }
}
