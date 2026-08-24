use crate::error::{Error, Result};
use crate::rt;
use crate::server::*;

impl Server {
    pub fn gaming_status(&self) -> GamingStatus {
        match unsafe { (rt().api.gaming_status)() } {
            1 => GamingStatus::Starting,
            2 => GamingStatus::Running,
            3 => GamingStatus::Stopping,
            _ => GamingStatus::Default,
        }
    }

    pub fn get_current_tick(&self) -> Result<u64> {
        let tick = unsafe { (rt().api.get_current_tick)() };
        if tick == 0 {
            let dt = unsafe { (rt().api.get_tick_delta_time)() };
            if dt < 0.0 {
                return Err(Error("level not ready".into()));
            }
        }
        Ok(tick)
    }

    pub fn get_tick_delta_time(&self) -> Result<f64> {
        let dt = unsafe { (rt().api.get_tick_delta_time)() };
        if dt < 0.0 {
            return Err(Error("level not ready".into()));
        }
        Ok(dt)
    }

    pub fn get_tps(&self) -> Result<f64> {
        let dt = self.get_tick_delta_time()?;
        if dt <= 0.0 {
            return Err(Error("invalid tick delta time".into()));
        }
        Ok((1.0 / dt).min(20.0))
    }

    pub fn get_active_player_count(&self) -> Result<i32> {
        let count = unsafe { (rt().api.get_player_count)() };

        let dt = unsafe { (rt().api.get_tick_delta_time)() };
        if dt < 0.0 && count == 0 {
            return Err(Error("level not ready".into()));
        }
        Ok(count)
    }

    pub fn is_sim_paused(&self) -> Result<bool> {
        let dt = unsafe { (rt().api.get_tick_delta_time)() };
        if dt < 0.0 {
            return Err(Error("level not ready".into()));
        }
        Ok(unsafe { (rt().api.get_sim_paused)() })
    }
}
