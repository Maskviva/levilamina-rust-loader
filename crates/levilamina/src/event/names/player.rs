//! Player event ids (verified against the pinned LeviLamina headers).
//!
//! Every constant is also re-exported flat from [`crate::event::names`], so
//! both `names::PLAYER_CHAT` and `names::player::PLAYER_CHAT` resolve to the
//! same string. Any unique suffix subscribes; using these constants keeps you
//! off the "class was renamed upstream" treadmill.

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

/// **Bridge-hook event, not an LL bus event.**
///
/// This constant used to sit in the list above with the rest, under a doc
/// comment claiming everything here was "verified against the pinned
/// LeviLamina headers". It was not. There is no `PlayerDropItemEvent` under
/// `ll/api/event/player/` — the only thing with that name is vanilla's
/// `mc/world/events/PlayerDropItemEvent.h`, a `PlayerGameplayEvent<void>`
/// variant that is neither on the bus nor cancellable.
///
/// So `subscribe_event` on it resolved to nothing and returned an error, and
/// every drop went unprotected while the constant sat there looking official.
/// A wrong name in a list of verified names is worse than a missing one.
///
/// It is now backed by `hooks/DropItemEvent.cpp`, which hooks **two** paths:
/// `Player::drop` (the Q key) and `ComplexInventoryTransaction::handle`
/// (dragging a stack out of the open inventory screen). **Cancellable.**
///
/// Payload: `{x, y, z, dim, item, randomly, viaInventoryUi, _player}`.
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

/// Fired just before a player is moved to another dimension, from
/// `Level::requestPlayerChangeDimension` — the single funnel every transfer
/// goes through (portals, commands, plugin teleports).
///
/// Payload carries `from` and `to` dimension ids. Dispatched **before** the
/// move, so the player is still in `from` when your callback runs: that is what
/// makes "save the state belonging to the world being left" possible.
///
/// Observe-only — subscribing does not let you cancel the transfer.
pub const PLAYER_CHANGE_DIMENSION: &str = "PlayerChangeDimensionEvent";

/// Fired when a player is about to open a container (chest, furnace, hopper,
/// …), from `VanillaServerGameplayEventListener::onEvent`.
///
/// **Cancellable** — `EventRef::cancel()` refuses the open. This is the hook a
/// land-protection plugin needs: without it a visitor can empty someone's
/// chests without ever breaking a block.
///
/// Payload: `{x, y, z, dim, containerType, _player}`.
pub const PLAYER_OPEN_CONTAINER: &str = "PlayerOpenContainerEvent";

/// Fired when a player is about to use the held item **on a block**, from
/// `GameMode::useItemOn`.
///
/// **Cancellable** — `EventRef::cancel()` refuses the use: no block placed, no
/// mob spawned, no bucket emptied, and the item is not consumed.
///
/// This is the funnel `PlayerInteractBlockEvent` is often mistaken for. Spawn
/// eggs, buckets, flint & steel, ender pearls and item-driven block placement
/// all come through here, so a land-protection plugin that watches only
/// interact/place events lets every one of them through.
///
/// Payload: `{x, y, z, dim, face, item, isFirstEvent, _player}` — `x/y/z` flat
/// integers, `item` the item's type name (e.g. `"minecraft:sheep_spawn_egg"`).
///
/// `isFirstEvent` distinguishes the initial click from the repeats: holding
/// right-click re-fires this call many times per second on Windows clients.
pub const PLAYER_USE_ITEM_ON: &str = "PlayerUseItemOnEvent";

/// Fired when a player is about to right-click an **entity**, from
/// `Player::interact` — the same hook point LegacyScriptEngine uses for
/// `onPlayerInteractEntity`.
///
/// **Cancellable** — `EventRef::cancel()` refuses the interaction (the arm
/// still swings, so a refused click doesn't read to the player as a dropped
/// packet).
///
/// This is the entity-side counterpart to [`PLAYER_USE_ITEM_ON`], and it is a
/// much bigger hole than its usual nickname ("the shear-sheep event") suggests.
/// Everything below reaches the world through here and through nothing else:
///
/// shearing · dyeing · milking · leashing · name tags · saddling · opening a
/// horse/llama/donkey inventory · villager trading · feeding and breeding ·
/// putting a chest on a llama
///
/// Payload: `{x, y, z, dim, target, targetIsPlayer, item, _player}` — `x/y/z`
/// is the **target's** position, not the player's, because that is where the
/// permission question applies. `item` lets a subscriber split shears / lead /
/// dye / food into separate actions instead of one blanket permission.
pub const PLAYER_INTERACT_ENTITY: &str = "PlayerInteractEntityEvent";

/// Fired when a player is about to mount a vehicle or animal, from
/// `Actor::canAddPassenger` — the vehicle's own veto, which is why refusing
/// here is safe: every caller already handles "no".
///
/// **Cancellable.** Covers horses, boats, minecarts, pigs, striders, llamas.
///
/// Payload: `{x, y, z, dim, vehicle, _player}` — `x/y/z` is the **vehicle's**
/// position. Only dispatched when the would-be rider is a player; mob mounts
/// are not a permission question and some farms produce them every tick.
pub const PLAYER_RIDE: &str = "PlayerRideEvent";

/// Fired when a player is about to launch a projectile.
///
/// **Cancellable** — cancelling means no projectile entity is created. Note
/// that ammo is **not** refunded: by the time this fires the arrow has already
/// left the inventory, so cancelling stops the shot, not the consumption.
///
/// Backed by three native hooks, because there are three paths and no single
/// one of them sees all projectiles: `BedrockSpawner::spawnProjectile` (the
/// common case), `TridentItem::releaseUsing` (tridents never reach the
/// spawner), and `CrossbowItem::_shootFirework` (firework rockets).
///
/// This is why `PlayerUseItemEvent` was never sufficient for throw protection:
/// it fires from `GameMode::useItem`, which covers snowball / egg / ender pearl
/// / splash potion / wind charge but not the charge-and-release path used by
/// bow, crossbow and trident.
///
/// Only player-launched projectiles are dispatched — dispenser and mob
/// projectiles are world behaviour, not permissions.
///
/// Payload: `{x, y, z, dim, projectile, _player}`.
pub const PLAYER_SPAWN_PROJECTILE: &str = "PlayerSpawnProjectileEvent";

/// Fired when a player standing on a pressure plate or in a tripwire is about
/// to trigger it, from `shouldTriggerEntityInside`.
///
/// **Cancellable** — cancelling means the plate never presses and no redstone
/// signal is emitted.
///
/// Unlike every other player event, this one has no *action* behind it: walking
/// is the action and the trigger is a side effect. That is why no vanilla or LL
/// "player pressed a plate" event exists to subscribe to.
///
/// **Already throttled on the native side.** The underlying virtual runs every
/// tick for every entity inside the block, so the bridge caches each decision
/// per (player, block position) for 250 ms before dispatching again. A
/// subscriber does not need to add its own rate limiting, and should not assume
/// one dispatch equals one step.
///
/// Payload: `{x, y, z, dim, kind, _player}` where `kind` is `"pressure_plate"`
/// or `"tripwire"`.
pub const PLAYER_STEP_ON_PRESSURE_PLATE: &str = "PlayerStepOnPressurePlateEvent";

/// Fired when a player is about to shove another entity by walking into it,
/// from `PushableByEntityUtility::skipPush`.
///
/// **Cancellable** — cancelling means the push is skipped.
///
/// This is the one griefing method that survives a fully locked-down plot: a
/// visitor who cannot break, place, interact or attack can still herd the
/// owner's animals out of their pen or shove their boats away, and it leaves
/// nothing in any log.
///
/// Only player-vs-entity is dispatched. Mob-vs-mob is world behaviour, and
/// player-vs-player is left alone on purpose — blocking that asymmetrically
/// (A may push B, B may not push A) produces rubber-banding that reads as lag.
///
/// **Already throttled on the native side** (250 ms per player per position),
/// like `PLAYER_STEP_ON_PRESSURE_PLATE`. One dispatch is not one push.
///
/// Payload: `{x, y, z, dim, target, _player}` — `x/y/z` is the **pushed
/// entity's** position, because that is where the permission question applies.
pub const PLAYER_PUSH_ENTITY: &str = "PlayerPushEntityEvent";
