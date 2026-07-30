//! Block gap-fill API: state get/set, collision shape (ABI v5 additive).

use super::*;
use crate::error::Result;
use crate::ffi::{call_out_str, s};
use crate::rt;

impl Block {
    /// Read a block state value by name as a string
    /// (e.g. `"minecraft:cardinal_direction"` -> `"north"`).
    pub fn get_state(&self, state_name: &str) -> Result<String> {
        call_out_str(|ctx, sink| unsafe {
            (rt().api.block_get_state)(self.dim, self.x, self.y, self.z, s(state_name), ctx, sink)
        })
        .ok_or_else(|| self.unreachable())
    }

    /// Set a block state by name (e.g. `"minecraft:cardinal_direction"`, `"south"`).
    pub fn set_state(&self, state_name: &str, value: &str) -> Result<()> {
        let ok = unsafe {
            (rt().api.block_set_state)(self.dim, self.x, self.y, self.z, s(state_name), s(value))
        };
        if ok {
            Ok(())
        } else {
            Err(self.unreachable())
        }
    }

    /// Collision AABB list as SNBT: `[{min:[x,y,z], max:[x,y,z]}, …]`
    /// (empty array `[]` if the block has no collision).
    pub fn collision_shape(&self) -> Result<String> {
        call_out_str(|ctx, sink| unsafe {
            (rt().api.block_get_collision_shape)(self.dim, self.x, self.y, self.z, ctx, sink)
        })
        .ok_or_else(|| self.unreachable())
    }
}
