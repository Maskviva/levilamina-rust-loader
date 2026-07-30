//! # levilamina
//!
//! Write [LeviLamina](https://github.com/LiteLDev/LeviLamina) mods for
//! Minecraft Bedrock (server **or** client) in safe Rust.
//!
//! Requires the `levilamina-rust-loader` mod (the C++ bridge from this
//! repository) to be installed. Your mod is a plain `cdylib`:
//!
//! ```toml
//! [lib]
//! crate-type = ["cdylib"]
//! ```
//!
//! Pick a side via cargo features (mutually exclusive — default is `server`):
//!
//! ```toml
//! [dependencies]
//! levilamina = { version = "26.20.4", default-features = false, features = ["server"] }
//! # …or for a client mod:
//! # levilamina = { version = "26.20.4", default-features = false, features = ["client"] }
//! ```
//!
//! ```no_run
//! use levilamina::prelude::*;
//!
//! struct MyMod;
//!
//! impl LeviMod for MyMod {
//!     fn on_load(ctx: &ModContext) -> Result<Self> {
//!         ctx.logger().info("hello from Rust!");
//!         Ok(MyMod)
//!     }
//!
//!     fn on_enable(&mut self, ctx: &ModContext) -> Result<()> {
//!         let logger = ctx.logger();
//!         ctx.server()
//!             .subscribe_event("PlayerChatEvent", EventPriority::Normal, move |ev| {
//!                 logger.info(&format!("chat event: {}", ev.snbt()));
//!             })?
//!             .forget(); // keep for the lifetime of the mod
//!         Ok(())
//!     }
//! }
//!
//! levilamina::register_mod!(MyMod);
//! ```
//!
//! ## Threading model
//!
//! Every callback (lifecycle, events, commands, forms, scheduled tasks) runs
//! on the **game thread** (server thread or client thread, depending on the
//! selected feature). [`Server::schedule`] / [`Server::schedule_after`]
//! are the main thread-safe entry points and are how background threads
//! (Tokio tasks, AI agents, …) marshal work back into the game. The
//! [`KvDb`] and [`system`] families are also thread-safe.
//!
//! ## Object model (v1.0.0)
//!
//! Handles are **identifiers, not pointers** — a [`Player`] is a selector
//! resolved against the live player list on every call, an [`Entity`] is an
//! `ActorUniqueID`, a [`Block`] is `(dimension, position)`, and an
//! [`ItemStack`] is a pure SNBT value object. Nothing you hold can dangle;
//! at worst a call returns `Err` because the target is gone.

// The `server` and `client` features are mutually exclusive — exactly one
// must be enabled. Enforced at compile time so a misconfigured mod fails
// fast instead of producing a mixed-up ABI.
#[cfg(all(feature = "server", feature = "client"))]
compile_error!(
    "The `server` and `client` features are mutually exclusive. \
     Enable exactly one of them."
);
#[cfg(not(any(feature = "server", feature = "client")))]
compile_error!(
    "You must enable exactly one of the `server` or `client` features \
     (default is `server`)."
);

pub use levilamina_sys as sys;

// Shared API (both server and client).
pub mod block;
pub mod container;
pub mod entity;
mod error;
pub mod event;
mod ffi;
pub mod item;
pub mod kvdb;
mod logger;
pub mod nbt;
pub mod player;
mod registration;
mod runtime;
pub mod system;
pub mod types;
pub mod world;

// ── Server-only API ───────────────────────────────────────────────────
#[cfg(feature = "server")]
pub mod command;
#[cfg(feature = "server")]
pub mod gui;
#[cfg(feature = "server")]
pub mod money;
#[cfg(feature = "server")]
pub mod scoreboard;
#[cfg(feature = "server")]
pub mod server;
#[cfg(feature = "server")]
pub mod sim;

// Custom dimensions (reimplements LiteLDev/MoreDimensions). The C++ loader
// always compiles this in for server builds; this cargo feature only gates
// whether the Rust safe-layer is available. Server build only.
#[cfg(all(feature = "server", feature = "more_dimensions"))]
pub mod more_dimensions;

// ── Client-only API ───────────────────────────────────────────────────
#[cfg(feature = "client")]
pub mod client;

pub use block::Block;
pub use container::Container;
pub use entity::{Entity, EntityId};
pub use error::{Error, Result};
pub use event::{EventPriority, EventRef, Listener, PlayerIdentity};
pub use item::ItemStack;
pub use kvdb::KvDb;
pub use logger::{LogLevel, Logger};
pub use nbt::NbtValue;
pub use player::{Ability, GameMode, MessageType, Player, PlayerInfo};
pub use registration::{__init_runtime, __lifecycle, __load, LeviMod, ModSlot};
pub use runtime::ModContext;
pub use world::{
    BlockInfo, Bounds, Cell, EntityInfo, PlayerPos, Scan, ScanLayer, StructureInfo, VillageInfo,
};

// ── Server-only re-exports ────────────────────────────────────────────
#[cfg(feature = "server")]
pub use command::{
    CommandBuilder, CommandInvocation, CommandInvocationEx, CommandOrigin, CommandPermission,
    CommandResult, OverloadBuilder, ParamType,
};
#[cfg(feature = "server")]
pub use gui::{CustomFormBuilder, FormResponse, FormValue, ModalFormBuilder, SimpleFormBuilder};
#[cfg(feature = "server")]
pub use money::{MoneyEvent, MoneyEventKind};
#[cfg(feature = "server")]
pub use scoreboard::{DisplaySlot, Objective, Scoreboard};
#[cfg(feature = "server")]
pub use server::{GamingStatus, Server, SoftEnumOp, Weather};
#[cfg(feature = "server")]
pub use sim::SimPlayer;

// ── Client-only re-exports ────────────────────────────────────────────
#[cfg(feature = "client")]
pub use client::{Client, ClientInstance, GamingStatus};

// Re-exported so every `use crate::{rt, sys}` in sibling modules keeps
// resolving after the runtime split.
pub(crate) use runtime::rt;

pub mod prelude {
    //! Everything most mods need, in one `use`.
    pub use crate::{
        register_mod, Ability, Block, BlockInfo, Cell, Entity, EntityInfo, EventPriority, EventRef,
        GameMode, ItemStack, KvDb, LeviMod, Listener, LogLevel, Logger, MessageType, ModContext,
        NbtValue, Player, PlayerInfo, PlayerPos, Result, Scan, ScanLayer,
    };

    // Server-only prelude items.
    #[cfg(feature = "server")]
    pub use crate::{
        CommandBuilder, CommandInvocation, CommandInvocationEx, CommandPermission, Container,
        DisplaySlot, FormResponse, FormValue, GamingStatus, Objective, ParamType, Scoreboard,
        Server, SimPlayer, SoftEnumOp, Weather,
    };

    // Opt-in: only when `more_dimensions` is enabled (implies server).
    #[cfg(feature = "more_dimensions")]
    pub use crate::more_dimensions::GeneratorType;

    // Client-only prelude items.
    #[cfg(feature = "client")]
    pub use crate::{client::KeyAction, Client, ClientInstance, Container};
}
