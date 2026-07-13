//! Block mutations.

use super::*;
use crate::error::{Error, Result};
use crate::ffi::{call_out_str, s};
use crate::nbt::NbtValue;
use crate::{rt, sys};

impl Block {
    /// Replace this block. `spec` is anything `/setblock` accepts:
    /// `"minecraft:stone"` or `"minecraft:wheat [\"growth\"=7]"`.
    pub fn set(&self, spec: &str) -> Result<()> {
        let ok =
            unsafe { (rt().api.set_block)(self.dim, self.x, self.y, self.z, crate::ffi::s(spec)) };
        if ok {
            Ok(())
        } else {
            Err(Error(format!("set_block failed for spec '{spec}'")))
        }
    }
}
