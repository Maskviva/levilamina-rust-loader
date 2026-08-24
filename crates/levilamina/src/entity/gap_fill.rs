use super::*;
use crate::error::Result;
use crate::ffi::{call_out_str, s};
use crate::rt;

impl Entity {
    pub fn vehicle(&self) -> Result<Entity> {
        let mut out: sys::LeviRsActorId = 0;
        let ok = unsafe { (rt().api.actor_get_vehicle)(self.id, &mut out) };
        if ok {
            Ok(Entity { id: out })
        } else {
            Err(self.gone())
        }
    }

    pub fn first_passenger(&self) -> Result<Entity> {
        let mut out: sys::LeviRsActorId = 0;
        let ok = unsafe { (rt().api.actor_get_first_passenger)(self.id, &mut out) };
        if ok {
            Ok(Entity { id: out })
        } else {
            Err(self.gone())
        }
    }

    pub fn owner(&self) -> Result<Entity> {
        let mut out: sys::LeviRsActorId = 0;
        let ok = unsafe { (rt().api.actor_get_owner)(self.id, &mut out) };
        if ok {
            Ok(Entity { id: out })
        } else {
            Err(self.gone())
        }
    }

    pub fn target(&self) -> Result<Entity> {
        let mut out: sys::LeviRsActorId = 0;
        let ok = unsafe { (rt().api.actor_get_target)(self.id, &mut out) };
        if ok {
            Ok(Entity { id: out })
        } else {
            Err(self.gone())
        }
    }

    pub fn equipped_item(&self, slot: i32) -> Result<String> {
        call_out_str(|ctx, sink| unsafe {
            (rt().api.actor_get_equipped_item)(self.id, slot, ctx, sink)
        })
        .ok_or_else(|| self.gone())
    }

    pub fn set_equipped_item(&self, slot: i32, item_snbt: &str) -> Result<()> {
        let ok = unsafe { (rt().api.actor_set_equipped_item)(self.id, slot, s(item_snbt)) };
        if ok {
            Ok(())
        } else {
            Err(self.gone())
        }
    }

    pub fn effects(&self) -> Result<String> {
        call_out_str(|ctx, sink| unsafe { (rt().api.actor_get_effects)(self.id, ctx, sink) })
            .ok_or_else(|| self.gone())
    }

    pub fn status_flag(&self, flag_index: i32) -> Result<bool> {
        let ok = unsafe { (rt().api.actor_get_status_flag)(self.id, flag_index) };
        Ok(ok)
    }

    pub fn set_status_flag(&self, flag_index: i32, value: bool) -> Result<()> {
        let ok = unsafe { (rt().api.actor_set_status_flag)(self.id, flag_index, value) };
        if ok {
            Ok(())
        } else {
            Err(self.gone())
        }
    }

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

    pub fn distance_to(&self, other: Entity) -> Result<f64> {
        let mut out = 0.0f64;
        let ok = unsafe { (rt().api.actor_distance_to)(self.id, other.id, &mut out) };
        if ok {
            Ok(out)
        } else {
            Err(self.gone())
        }
    }

    pub fn aabb(&self) -> Result<String> {
        call_out_str(|ctx, sink| unsafe { (rt().api.actor_get_aabb)(self.id, ctx, sink) })
            .ok_or_else(|| self.gone())
    }

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
