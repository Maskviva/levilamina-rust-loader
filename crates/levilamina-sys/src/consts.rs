//! ABI enum constants — append-only, never renumber.
//! Mirrors the enums in `LeviRsAbi.h`. Unknown values → call returns false.
//!
//! Split by domain so developers can find the key they need quickly:
//!   player — Player num/str/action props
//!   actor  — Actor num/str/action props
//!   world  — Block, Item, Scoreboard, Sys, Server props

pub mod actor;
pub mod player;
pub mod world;

pub use actor::*;
pub use player::*;
pub use world::*;
