use crate::error::{Error, Result};
use crate::ffi::call_out_str;
use crate::nbt::NbtValue;
use crate::rt;
use crate::server::*;

impl Server {
    pub fn begin_profile(&self, ticks: u32) -> Result<()> {
        let ok = unsafe { (rt().api.profile_begin)(ticks) };
        if ok {
            Ok(())
        } else {
            Err(Error(
                "begin_profile: ticks out of range (1..=12000) or a window is already running"
                    .into(),
            ))
        }
    }

    pub fn take_profile(&self) -> Result<Option<NbtValue>> {
        let snbt = call_out_str(|ctx, sink| unsafe { (rt().api.profile_take)(ctx, sink) });
        match snbt {
            Some(text) => Ok(Some(NbtValue::parse(&text)?)),
            None => Ok(None),
        }
    }
}
