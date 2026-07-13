//! Block handles: `(dimension, position)` resolved against the live
//! BlockSource on every call.

use crate::error::{Error, Result};
use crate::ffi::call_out_str;
use crate::nbt::NbtValue;
use crate::{rt, sys};

/// A block handle. Copy-cheap; re-reads the world every call, so it always
/// reflects the *current* block at that position.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct Block {
    dim: i32,
    x: i32,
    y: i32,
    z: i32,
}

mod actions;
mod query;

impl Block {
    fn unreachable(&self) -> Error {
        Error(format!(
            "block ({}, {}, {}) in dim {} unreachable (chunk unloaded / level not ready)",
            self.x, self.y, self.z, self.dim
        ))
    }

    fn get_num(&self, prop: i32) -> Result<f64> {
        let mut out = 0.0f64;
        let ok =
            unsafe { (rt().api.block_get_num)(self.dim, self.x, self.y, self.z, prop, &mut out) };
        if ok {
            Ok(out)
        } else {
            Err(self.unreachable())
        }
    }

    fn get_str(&self, prop: i32) -> Result<String> {
        call_out_str(|ctx, sink| unsafe {
            (rt().api.block_get_str)(self.dim, self.x, self.y, self.z, prop, ctx, sink)
        })
        .ok_or_else(|| self.unreachable())
    }

    /// Handle for the block at `(x, y, z)` in `dimension`
    /// (0=overworld 1=nether 2=the_end).
    pub fn at(dimension: i32, x: i32, y: i32, z: i32) -> Block {
        Block {
            dim: dimension,
            x,
            y,
            z,
        }
    }

    pub fn position(&self) -> (i32, i32, i32) {
        (self.x, self.y, self.z)
    }

    pub fn dimension(&self) -> i32 {
        self.dim
    }
}
