//! Mob and world-actor event ids.
//!
//! Re-exported flat from [`crate::event::names`], so `names::MOB_DIE` and
//! `names::mob::MOB_DIE` are the same string.

/// Pre-event (cancellable); the post-event is [`SPAWNED_MOB`].
pub const SPAWNING_MOB: &str = "SpawningMobEvent";
pub const SPAWNED_MOB: &str = "SpawnedMobEvent";
pub const MOB_HURT: &str = "MobHurtEvent";
pub const MOB_DIE: &str = "MobDieEvent";
pub const ACTOR_HURT: &str = "ActorHurtEvent";
pub const FIRE_SPREAD: &str = "FireSpreadEvent";
