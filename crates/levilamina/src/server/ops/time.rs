use crate::error::{Error, Result};
use crate::ffi::{call_out_str, s};
use crate::nbt::NbtValue;
use crate::rt;
use crate::server::*;

impl Server {
    pub fn time(&self) -> Result<i64> {
        let mut out = 0i64;
        let ok = unsafe { (rt().api.get_time)(&mut out) };
        if ok {
            Ok(out)
        } else {
            Err(Error("level not ready".into()))
        }
    }

    pub fn set_time(&self, t: i64) -> Result<()> {
        let ok = unsafe { (rt().api.set_time)(t) };
        if ok {
            Ok(())
        } else {
            Err(Error("set_time failed".into()))
        }
    }

    pub fn set_weather(&self, weather: Weather) -> Result<()> {
        let ok = unsafe { (rt().api.set_weather)(weather as i32) };
        if ok {
            Ok(())
        } else {
            Err(Error("set_weather failed".into()))
        }
    }

    pub fn difficulty(&self) -> Result<i32> {
        let mut out = 0i32;
        let ok = unsafe { (rt().api.get_difficulty)(&mut out) };
        if ok {
            Ok(out)
        } else {
            Err(Error("level not ready".into()))
        }
    }

    pub fn set_difficulty(&self, d: i32) -> Result<()> {
        let ok = unsafe { (rt().api.set_difficulty)(d) };
        if ok {
            Ok(())
        } else {
            Err(Error("set_difficulty failed (0..=3?)".into()))
        }
    }

    pub fn seed(&self) -> Result<i64> {
        let mut out = 0i64;
        let ok = unsafe { (rt().api.get_seed)(&mut out) };
        if ok {
            Ok(out)
        } else {
            Err(Error("level not ready".into()))
        }
    }

    pub fn game_rule(&self, name: &str) -> Result<NbtValue> {
        let raw = call_out_str(|ctx, sink| unsafe { (rt().api.game_rule_get)(s(name), ctx, sink) })
            .ok_or_else(|| Error(format!("unknown game rule '{name}'")))?;
        NbtValue::parse(&raw)
    }

    pub fn set_game_rule(&self, name: &str, value: &str) -> Result<()> {
        let ok = unsafe { (rt().api.game_rule_set)(s(name), s(value)) };
        if ok {
            Ok(())
        } else {
            Err(Error(format!("set_game_rule('{name}') failed")))
        }
    }
}
