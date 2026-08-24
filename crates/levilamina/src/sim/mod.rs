use crate::error::{Error, Result};
use crate::ffi::s;
use crate::player::Player;
use crate::{rt, sys};

#[derive(Debug, Clone)]
pub struct SimPlayer {
    name: String,
}

mod actions;

impl SimPlayer {
    fn sel(&self) -> sys::LeviRsPlayerSel {
        sys::LeviRsPlayerSel {
            kind: 0,
            value: s(&self.name),
        }
    }

    fn act(&self, verb: &str, args: &str) -> Result<()> {
        let ok = unsafe { (rt().api.sim_do)(self.sel(), s(verb), s(args)) };
        if ok {
            Ok(())
        } else {
            Err(Error(format!(
                "sim '{}': '{verb}' rejected (offline/despawned, bad args, or unsupported)",
                self.name
            )))
        }
    }

    fn pos_args(x: f64, y: f64, z: f64, extra: &[(&str, f64)]) -> String {
        let mut out = format!("{{\"x\":{x},\"y\":{y},\"z\":{z}");
        for (k, v) in extra {
            out.push_str(&format!(",\"{k}\":{v}"));
        }
        out.push('}');
        out
    }

    pub fn by_name(name: impl Into<String>) -> SimPlayer {
        SimPlayer { name: name.into() }
    }

    pub fn name(&self) -> &str {
        &self.name
    }

    pub fn player(&self) -> Player {
        Player::by_name(&self.name)
    }
}
