use std::ffi::c_void;

use super::NbtValue;
use crate::error::{Error, Result};
use crate::ffi::{call_out_str, s};
use crate::rt;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum NbtBinaryFormat {
    Disk = 0,

    Network = 1,
}

unsafe extern "C" fn set_bytes(ctx: *mut c_void, data: *const u8, len: usize) {
    if data.is_null() || len == 0 {
        (*ctx.cast::<Vec<u8>>()).clear();
        return;
    }
    let slice = std::slice::from_raw_parts(data, len);
    *ctx.cast::<Vec<u8>>() = slice.to_vec();
}

impl NbtValue {
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

    pub fn from_binary(bytes: &[u8], fmt: NbtBinaryFormat) -> Result<NbtValue> {
        let snbt = call_out_str(|ctx, sink| unsafe {
            (rt().api.nbt_binary_to_snbt)(bytes.as_ptr(), bytes.len(), fmt as i32, ctx, sink)
        })
        .ok_or_else(|| Error("nbt_binary_to_snbt failed (truncated/invalid NBT?)".into()))?;
        NbtValue::parse(&snbt)
    }
}
