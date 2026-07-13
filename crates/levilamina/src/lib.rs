//! # levilamina
//!
//! Write [LeviLamina](https://github.com/LiteLDev/LeviLamina) mods for
//! Minecraft Bedrock Dedicated Server in safe Rust.
//!
//! Requires the `levilamina-rust-loader` mod (the C++ bridge from this
//! repository) to be installed on the server. Your mod is a plain `cdylib`:
//!
//! ```toml
//! [lib]
//! crate-type = ["cdylib"]
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
//! on the **server thread**. [`Server::schedule`] / [`Server::schedule_after`]
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

use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::{Mutex, OnceLock};

pub use levilamina_sys as sys;

pub mod block;
pub mod command;
pub mod container;
pub mod entity;
mod error;
pub mod event;
mod ffi;
pub mod gui;
pub mod item;
pub mod kvdb;
mod logger;
pub mod nbt;
pub mod player;
pub mod scoreboard;
pub mod server;
pub mod sim;
pub mod system;
pub mod world;

pub use block::Block;
pub use command::{
    CommandBuilder, CommandInvocation, CommandInvocationEx, CommandOrigin, CommandPermission,
    CommandResult, OverloadBuilder, ParamType,
};
pub use container::Container;
pub use entity::{Entity, EntityId};
pub use error::{Error, Result};
pub use event::{EventPriority, EventRef, Listener, PlayerIdentity};
pub use gui::{CustomFormBuilder, FormResponse, FormValue, ModalFormBuilder, SimpleFormBuilder};
pub use item::ItemStack;
pub use kvdb::KvDb;
pub use logger::{LogLevel, Logger};
pub use nbt::NbtValue;
pub use player::{Ability, GameMode, MessageType, Player, PlayerInfo};
pub use scoreboard::{DisplaySlot, Objective, Scoreboard};
pub use server::{GamingStatus, Server, SoftEnumOp, Weather};
pub use sim::SimPlayer;
pub use world::{
    BlockInfo, Bounds, Cell, EntityInfo, PlayerPos, Scan, ScanLayer, StructureInfo, VillageInfo,
};

pub mod prelude {
    //! Everything most mods need, in one `use`.
    pub use crate::{
        register_mod, Ability, Block, BlockInfo, Cell, CommandBuilder, CommandInvocation,
        CommandInvocationEx, CommandPermission, Container, DisplaySlot, Entity, EntityInfo,
        EventPriority, EventRef, FormResponse, FormValue, GameMode, GamingStatus, ItemStack, KvDb,
        LeviMod, Listener, LogLevel, Logger, ModContext, NbtValue, ParamType, Player, PlayerInfo,
        PlayerPos, Result, Scan, ScanLayer, Scoreboard, Server, SimPlayer, SoftEnumOp, Weather,
    };
}

// ───────────────────────── runtime plumbing ─────────────────────────

pub(crate) struct Runtime {
    pub(crate) api: &'static sys::LeviRsApi,
    pub(crate) handle: sys::LeviRsModHandle,
}
// The handle is only ever dereferenced by the bridge on the server thread.
unsafe impl Send for Runtime {}
unsafe impl Sync for Runtime {}

static RUNTIME: OnceLock<Runtime> = OnceLock::new();

pub(crate) fn rt() -> &'static Runtime {
    RUNTIME
        .get()
        .expect("levilamina runtime not initialized (register_mod! missing?)")
}

/// Everything a mod needs, passed to lifecycle hooks.
pub struct ModContext(());

impl ModContext {
    pub fn logger(&self) -> Logger {
        Logger::get()
    }
    pub fn server(&self) -> Server {
        Server::get()
    }
}

// ───────────────────────── mod trait + registration ─────────────────────────

/// Implement this and call [`register_mod!`] once.
///
/// All hooks run on the server thread, and the mod instance is only ever
/// touched from the server thread — so it may freely hold `!Send` resources
/// such as [`Listener`].
pub trait LeviMod: Sized + 'static {
    /// Called while the mod is loading. Return `Err` to abort loading.
    fn on_load(ctx: &ModContext) -> Result<Self>;
    fn on_enable(&mut self, _ctx: &ModContext) -> Result<()> {
        Ok(())
    }
    fn on_disable(&mut self, _ctx: &ModContext) -> Result<()> {
        Ok(())
    }
    fn on_unload(&mut self, _ctx: &ModContext) -> Result<()> {
        Ok(())
    }
}

#[doc(hidden)]
pub struct ModSlot<T: LeviMod>(pub Mutex<Option<T>>);

// SAFETY: the slot is only ever locked and accessed on the server thread, via
// the bridge-invoked entry points (__load / __lifecycle). The instance never
// migrates between threads at runtime, so a `!Send` mod (e.g. one holding a
// `Listener`) is sound here even though `Mutex<Option<T>>` is not auto-Sync.
unsafe impl<T: LeviMod> Sync for ModSlot<T> {}

#[doc(hidden)]
pub unsafe fn __init_runtime(api: *const sys::LeviRsApi, handle: sys::LeviRsModHandle) -> bool {
    if api.is_null() {
        return false;
    }
    // SAFETY: the bridge guarantees the api table outlives the mod.
    let api: &'static sys::LeviRsApi = &*api;
    if api.abi_version != sys::LEVI_RS_ABI_VERSION {
        return false;
    }
    // DESIGN.md §8: additive ABI changes bump `struct_size`, not `abi_version`,
    // so a mod built against a newer `levilamina-sys` (expecting a larger
    // `LeviRsApi`) must refuse a loader whose table is smaller than what this
    // crate was compiled against — otherwise a trailing field access would
    // read past what the loader actually allocated.
    if (api.struct_size as usize) < core::mem::size_of::<sys::LeviRsApi>() {
        return false;
    }
    RUNTIME.set(Runtime { api, handle }).is_ok()
}

#[doc(hidden)]
pub fn __lifecycle<T: LeviMod>(
    slot: &'static ModSlot<T>,
    stage: u8, // 1=enable, 2=disable, 3=unload
) -> bool {
    let ctx = ModContext(());
    let run = || -> Result<()> {
        let mut guard = slot
            .0
            .lock()
            .map_err(|_| Error("mod state poisoned".into()))?;
        let Some(instance) = guard.as_mut() else {
            return Err(Error("mod instance missing".into()));
        };
        match stage {
            1 => instance.on_enable(&ctx),
            2 => instance.on_disable(&ctx),
            3 => {
                instance.on_unload(&ctx)?;
                *guard = None; // drop the instance before the dylib unloads
                Ok(())
            }
            _ => Ok(()),
        }
    };
    match catch_unwind(AssertUnwindSafe(run)) {
        Ok(Ok(())) => true,
        Ok(Err(e)) => {
            Logger::get().error(&format!("lifecycle error: {e}"));
            false
        }
        Err(_) => {
            Logger::get().error("panic in lifecycle hook");
            false
        }
    }
}

#[doc(hidden)]
pub fn __load<T: LeviMod>(slot: &'static ModSlot<T>) -> bool {
    let ctx = ModContext(());
    match catch_unwind(AssertUnwindSafe(|| T::on_load(&ctx))) {
        Ok(Ok(instance)) => {
            *slot.0.lock().unwrap() = Some(instance);
            true
        }
        Ok(Err(e)) => {
            Logger::get().error(&format!("on_load failed: {e}"));
            false
        }
        Err(_) => {
            Logger::get().error("panic in on_load");
            false
        }
    }
}

/// Export the `levi_rs_main` entry point for a [`LeviMod`] implementation.
#[macro_export]
macro_rules! register_mod {
    ($ty:ty) => {
        #[doc(hidden)]
        static __LEVI_RS_SLOT: $crate::ModSlot<$ty> =
            $crate::ModSlot(::std::sync::Mutex::new(None));

        #[no_mangle]
        pub unsafe extern "C" fn levi_rs_main(
            api: *const $crate::sys::LeviRsApi,
            handle: $crate::sys::LeviRsModHandle,
            out: *mut $crate::sys::LeviRsModVTable,
        ) -> bool {
            if out.is_null() || !$crate::__init_runtime(api, handle) {
                return false;
            }
            if !$crate::__load::<$ty>(&__LEVI_RS_SLOT) {
                return false;
            }
            unsafe extern "C" fn on_enable(_: *mut ::core::ffi::c_void) -> bool {
                $crate::__lifecycle::<$ty>(&__LEVI_RS_SLOT, 1)
            }
            unsafe extern "C" fn on_disable(_: *mut ::core::ffi::c_void) -> bool {
                $crate::__lifecycle::<$ty>(&__LEVI_RS_SLOT, 2)
            }
            unsafe extern "C" fn on_unload(_: *mut ::core::ffi::c_void) -> bool {
                $crate::__lifecycle::<$ty>(&__LEVI_RS_SLOT, 3)
            }
            (*out) = $crate::sys::LeviRsModVTable {
                abi_version: $crate::sys::LEVI_RS_ABI_VERSION,
                instance: ::core::ptr::null_mut(),
                on_enable: Some(on_enable),
                on_disable: Some(on_disable),
                on_unload: Some(on_unload),
            };
            true
        }
    };
}
