//! `Server` tick control (carpet-style freeze / step / warp).

use super::*;
use crate::error::{Error, Result};
use crate::rt;

impl Server {
    /// Freeze (or unfreeze) the world clock — carpet-style `/tick freeze`.
    ///
    /// While frozen, mobs, block ticks, redstone and time all stop; players
    /// can still move and chat (movement is client-authoritative and the
    /// network runs outside the level tick). Backed by a bridge-owned detour
    /// on `Level::tick`, installed lazily on first use. The read-only side
    /// ([`get_tps`](Self::get_tps) / [`get_current_tick`](Self::get_current_tick))
    /// keeps working. Server thread only.
    pub fn set_tick_freeze(&self, on: bool) -> Result<()> {
        let ok = unsafe { (rt().api.tick_freeze)(on) };
        if ok {
            Ok(())
        } else {
            Err(Error("tick_freeze rejected".into()))
        }
    }

    /// While frozen: advance the world by exactly `n` frames (`/tick step n`).
    /// Errors if the clock isn't frozen or `n == 0`. Server thread only.
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

    /// Warp the world clock (`/tick warp`): `factor` in `(0, 100]`,
    /// `2.0` = double speed, `0.5` = half speed (frames are skipped via an
    /// accumulator), `1.0` restores normal. Server thread only.
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
