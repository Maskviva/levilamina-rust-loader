use crate::error::{Error, Result};
use crate::rt;
use crate::server::*;

impl Server {
    pub fn set_tick_freeze(&self, on: bool) -> Result<()> {
        let ok = unsafe { (rt().api.tick_freeze)(on) };
        if ok {
            Ok(())
        } else {
            Err(Error("tick_freeze rejected".into()))
        }
    }

    pub fn step_ticks(&self, n: u32) -> Result<()> {
        let ok = unsafe { (rt().api.tick_step)(n) };
        if ok {
            Ok(())
        } else {
            Err(Error(
                "step_ticks: clock not frozen (call set_tick_freeze(true) first) or n == 0".into(),
            ))
        }
    }

    pub fn set_tick_warp(&self, factor: f64) -> Result<()> {
        let ok = unsafe { (rt().api.tick_warp)(factor) };
        if ok {
            Ok(())
        } else {
            Err(Error(format!(
                "set_tick_warp: factor {factor} out of range (0, 100]"
            )))
        }
    }
}
