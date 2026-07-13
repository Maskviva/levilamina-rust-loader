//! Player event ids (verified against the pinned LeviLamina headers).
//!
//! Every constant is also re-exported flat from [`crate::event::names`], so
//! both `names::PLAYER_CHAT` and `names::player::PLAYER_CHAT` resolve to the
//! same string. Any unique suffix subscribes; using these constants keeps you
//! off the "class was renamed upstream" treadmill.

pub const PLAYER_CHAT: &str = "PlayerChatEvent";
pub const PLAYER_JOIN: &str = "PlayerJoinEvent";
pub const PLAYER_CONNECT: &str = "PlayerConnectEvent";
pub const PLAYER_DISCONNECT: &str = "PlayerDisconnectEvent"; // NOT cancellable
pub const PLAYER_DIE: &str = "PlayerDieEvent";
pub const PLAYER_RESPAWN: &str = "PlayerRespawnEvent"; // NOT cancellable
pub const PLAYER_JUMP: &str = "PlayerJumpEvent"; // NOT cancellable
pub const PLAYER_SPRINT: &str = "PlayerSprintEvent"; // NOT cancellable
pub const PLAYER_SWING: &str = "PlayerSwingEvent"; // NOT cancellable
pub const PLAYER_ATTACK: &str = "PlayerAttackEvent";
pub const PLAYER_PICK_UP_ITEM: &str = "PlayerPickUpItemEvent";
pub const PLAYER_DROP_ITEM: &str = "PlayerDropItemEvent";
pub const PLAYER_USE_ITEM: &str = "PlayerUseItemEvent";
pub const PLAYER_INTERACT_BLOCK: &str = "PlayerInteractBlockEvent";
pub const PLAYER_DESTROYING_BLOCK: &str = "PlayerDestroyingBlockEvent";
pub const PLAYER_DESTROY_BLOCK: &str = "PlayerDestroyBlockEvent";
/// Pre-event (cancellable); the post-event is [`PLAYER_PLACED_BLOCK`].
pub const PLAYER_PLACING_BLOCK: &str = "PlayerPlacingBlockEvent";
pub const PLAYER_PLACED_BLOCK: &str = "PlayerPlacedBlockEvent";
/// Pre-event; the post-event is [`PLAYER_SNEAKED`].
pub const PLAYER_SNEAKING: &str = "PlayerSneakingEvent";
pub const PLAYER_SNEAKED: &str = "PlayerSneakedEvent";
