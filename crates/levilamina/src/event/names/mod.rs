//! Corrected event ids (verified against the pinned LeviLamina headers),
//! grouped by domain.
//!
//! Two ways to reach every constant, both valid:
//! - **grouped**: [`names::player::PLAYER_CHAT`](player::PLAYER_CHAT) —
//!   discoverable, scales as events are added;
//! - **flat**: `names::PLAYER_CHAT` — the original path, kept working by the
//!   re-exports below so existing mods need no change.
//!
//! Any unique suffix subscribes; using these constants keeps you off the
//! "class was renamed upstream" treadmill.

pub mod mob;
pub mod player;
pub mod server;

pub use mob::*;
pub use player::*;
pub use server::*;
