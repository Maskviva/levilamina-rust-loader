//! `Server` per-subsystem MSPT profiler.

use super::*;
use crate::error::{Error, Result};
use crate::ffi::call_out_str;
use crate::nbt::NbtValue;
use crate::rt;

impl Server {
    /// Start a per-subsystem MSPT profiling window of `ticks` level ticks
    /// (`1..=12000`) — carpet-style `/prof`. Poll [`take_profile`](Self::take_profile)
    /// afterwards for the report. Errors if `ticks` is out of range or a
    /// window is already running. Server thread only.
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

    /// Poll for the finished profiling report as an [`NbtValue`] compound.
    /// Returns `Ok(None)` while a window is still sampling or none was armed;
    /// `Ok(Some(report))` exactly once when a window completes.
    ///
    /// Shape: `{ticks, buckets:{level_tick:{us,calls}, dimension_tick, redstone,
    /// chunk_blocks, block_entities}}`. Bucket times are **inclusive** (nested
    /// subsystems run inside outer ones) — present them side by side, don't sum.
    pub fn take_profile(&self) -> Result<Option<NbtValue>> {
        let snbt = call_out_str(|ctx, sink| unsafe { (rt().api.profile_take)(ctx, sink) });
        match snbt {
            Some(text) => Ok(Some(NbtValue::parse(&text)?)),
            None => Ok(None),
        }
    }
}
