//! `Server` status & clock readouts: gaming status, tick, TPS, player count.

use super::*;
use crate::error::{Error, Result};
use crate::rt;

impl Server {
    pub fn gaming_status(&self) -> GamingStatus {
        match unsafe { (rt().api.gaming_status)() } {
            1 => GamingStatus::Starting,
            2 => GamingStatus::Running,
            3 => GamingStatus::Stopping,
            _ => GamingStatus::Default,
        }
    }

    /// Current server tick (the `tickID` from `Level::getCurrentTick()`).
    /// Returns `Err` when the level is not ready. Server thread only.
    pub fn get_current_tick(&self) -> Result<u64> {
        let tick = unsafe { (rt().api.get_current_tick)() };
        if tick == 0 {
            // tickID 0 is plausible at startup; differentiate by also
            // checking delta_time as a readiness signal.
            let dt = unsafe { (rt().api.get_tick_delta_time)() };
            if dt < 0.0 {
                return Err(Error("level not ready".into()));
            }
        }
        Ok(tick)
    }

    /// Seconds taken by the last tick (0.05 s at a healthy 20 TPS).
    /// Use [`Server::get_tps`] for a human-friendly TPS value.
    /// Returns `Err` when unavailable. Server thread only.
    pub fn get_tick_delta_time(&self) -> Result<f64> {
        let dt = unsafe { (rt().api.get_tick_delta_time)() };
        if dt < 0.0 {
            return Err(Error("level not ready".into()));
        }
        Ok(dt)
    }

    /// Calculated TPS, capped at the vanilla 20.0.
    ///
    /// The underlying `mTickDeltaTime` is the time of the last tick **in
    /// seconds** (0.05 s at a healthy 20 TPS), so TPS = 1.0 / dt — not
    /// 1000.0 / dt. A tick can momentarily be faster than 50 ms, which would
    /// give >20; the game never runs faster than 20 TPS, so we clamp.
    /// Returns `Err` when the level is not ready. Server thread only.
    pub fn get_tps(&self) -> Result<f64> {
        let dt = self.get_tick_delta_time()?;
        if dt <= 0.0 {
            return Err(Error("invalid tick delta time".into()));
        }
        Ok((1.0 / dt).min(20.0))
    }

    /// Number of currently connected players.
    /// Returns `Err` when the level is not ready. Server thread only.
    pub fn get_active_player_count(&self) -> Result<i32> {
        let count = unsafe { (rt().api.get_player_count)() };
        // When the level is not ready, player_count would be 0 which is
        // indistinguishable. Cross-check with tick info.
        let dt = unsafe { (rt().api.get_tick_delta_time)() };
        if dt < 0.0 && count == 0 {
            return Err(Error("level not ready".into()));
        }
        Ok(count)
    }

    /// Whether the simulation is currently paused.
    /// Returns `Err` when unavailable. Server thread only.
    pub fn is_sim_paused(&self) -> Result<bool> {
        let dt = unsafe { (rt().api.get_tick_delta_time)() };
        if dt < 0.0 {
            return Err(Error("level not ready".into()));
        }
        Ok(unsafe { (rt().api.get_sim_paused)() })
    }
}
