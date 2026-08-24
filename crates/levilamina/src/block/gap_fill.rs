use super::*;
use crate::error::Result;
use crate::ffi::{call_out_str, s};
use crate::rt;

impl Block {
    pub fn get_state(&self, state_name: &str) -> Result<String> {
        call_out_str(|ctx, sink| unsafe {
            (rt().api.block_get_state)(self.dim, self.x, self.y, self.z, s(state_name), ctx, sink)
        })
        .ok_or_else(|| self.unreachable())
    }

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

    pub fn collision_shape(&self) -> Result<String> {
        call_out_str(|ctx, sink| unsafe {
            (rt().api.block_get_collision_shape)(self.dim, self.x, self.y, self.z, ctx, sink)
        })
        .ok_or_else(|| self.unreachable())
    }
}
