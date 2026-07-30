//! `Server` world read/write, split by concern so each file fits one screen:
//!   - [`particles`] — particle broadcast / per-player, player position
//!   - [`blocks`]    — region scan, single-block get/set
//!   - [`entities`]  — mob spawning, explosions
//!
//! All methods run on the server thread.

// Re-export the parent `server` namespace so the sibling impl files can keep
// using `use super::*;` exactly like the other `server/*.rs` modules do.
use super::*;

mod blocks;
mod entities;
mod gap_fill;
mod particles;
