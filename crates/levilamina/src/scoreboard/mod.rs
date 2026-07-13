//! Scoreboard access. Score identities are "fake player" names — the same
//! namespace vanilla `/scoreboard players` uses, so everything here lines up
//! with what ops see in-game.

use crate::error::{Error, Result};
use crate::ffi::{call_out_str, s};
use crate::nbt::NbtValue;
use crate::{rt, sys};

/// An objective's identity from [`Scoreboard::objectives`].
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Objective {
    pub name: String,
    pub display_name: String,
}

/// Display slots for [`Scoreboard::set_display`].
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DisplaySlot {
    Sidebar,
    List,
    BelowName,
}

impl DisplaySlot {
    fn as_str(self) -> &'static str {
        match self {
            DisplaySlot::Sidebar => "sidebar",
            DisplaySlot::List => "list",
            DisplaySlot::BelowName => "belowname",
        }
    }
}

/// The world scoreboard. Zero-sized; all state lives in the engine.
#[derive(Clone, Copy)]
pub struct Scoreboard(());

mod ops;

impl Scoreboard {
    pub fn get() -> Scoreboard {
        Scoreboard(())
    }

    fn op(&self, op: i32, a: &str, b: &str, n: i64) -> Option<String> {
        call_out_str(|ctx, sink| unsafe { (rt().api.scoreboard_op)(op, s(a), s(b), n, ctx, sink) })
    }

    fn op_bool(&self, op: i32, a: &str, b: &str, n: i64, what: &str) -> Result<()> {
        let mut out: Option<String> = None;
        let ok = unsafe {
            (rt().api.scoreboard_op)(
                op,
                s(a),
                s(b),
                n,
                (&mut out as *mut Option<String>).cast(),
                crate::ffi::set_string,
            )
        };
        if ok {
            Ok(())
        } else {
            Err(Error(format!("scoreboard: {what} failed")))
        }
    }
}
