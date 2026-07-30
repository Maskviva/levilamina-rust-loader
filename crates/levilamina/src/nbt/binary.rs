//! Binary NBT conversion via the engine's CompoundTag codec (ABI v5 §I).
//!
//! The SNBT object model is pure Rust (see [`super`]), but the on-disk and
//! network binary NBT formats are version-specific to the engine. These two
//! methods delegate to the bridge so the byte layout always matches the
//! running server.

use std::ffi::c_void;

use super::NbtValue;
use crate::error::{Error, Result};
use crate::ffi::{call_out_str, s};
use crate::rt;

/// Binary NBT format selector.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum NbtBinaryFormat {
    /// Disk format (little-endian, name-prefixed root compound).
    Disk = 0,
    /// Network format (big-endian, no root name).
    Network = 1,
}

/// Sink that copies bytes into a `Vec<u8>` behind `ctx`.
unsafe extern "C" fn set_bytes(ctx: *mut c_void, data: *const u8, len: usize) {
    if data.is_null() || len == 0 {
        (*ctx.cast::<Vec<u8>>()).clear();
        return;
    }
    let slice = std::slice::from_raw_parts(data, len);
    *ctx.cast::<Vec<u8>>() = slice.to_vec();
}

impl super::NbtValue {
    /// Serialize to binary NBT bytes using the engine's codec.
    ///
    /// Round-trips through the bridge's `CompoundTag::fromSnbt` +
    /// `toBinaryNbt`/`toNetworkNbt`, so the output matches what the server
    /// writes to disk or sends over the network.
    pub fn to_binary(&self, fmt: NbtBinaryFormat) -> Result<Vec<u8>> {
        let snbt = self.to_snbt();
        let mut out: Vec<u8> = Vec::new();
        let ok = unsafe {
            (rt().api.nbt_snbt_to_binary)(
                s(&snbt),
                fmt as i32,
                (&mut out as *mut Vec<u8>).cast(),
                set_bytes,
            )
        };
        if ok {
            Ok(out)
        } else {
            Err(Error("nbt_snbt_to_binary failed (invalid SNBT?)".into()))
        }
    }

    /// Parse binary NBT bytes into a [`NbtValue`] using the engine's codec.
    ///
    /// Delegates to `CompoundTag::fromBinaryNbt` / `fromNetworkNbt`, then
    /// re-parses the resulting SNBT through the pure-Rust parser so the
    /// returned value is a normal `NbtValue` with full accessor support.
    pub fn from_binary(bytes: &[u8], fmt: NbtBinaryFormat) -> Result<NbtValue> {
        let snbt = call_out_str(|ctx, sink| unsafe {
            (rt().api.nbt_binary_to_snbt)(bytes.as_ptr(), bytes.len(), fmt as i32, ctx, sink)
        })
        .ok_or_else(|| Error("nbt_binary_to_snbt failed (truncated/invalid NBT?)".into()))?;
        NbtValue::parse(&snbt)
    }
}
