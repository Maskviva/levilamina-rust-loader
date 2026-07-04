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
//! Every callback (lifecycle, events, commands, scheduled tasks) runs on the
//! **server thread**. [`Server::schedule`] / [`Server::schedule_after`] are the
//! only thread-safe entry points and are how background threads (Tokio tasks,
//! AI agents, …) marshal work back into the game.

use std::ffi::c_void;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::{Mutex, OnceLock};
use std::time::Duration;

pub use levilamina_sys as sys;

pub mod prelude {
    pub use crate::{
        register_mod, CommandInvocation, CommandPermission, EventPriority, EventRef, GamingStatus,
        LeviMod, Listener, LogLevel, Logger, ModContext, Result, Server,
    };
}

// ───────────────────────── errors ─────────────────────────

#[derive(Debug)]
pub struct Error(pub String);

impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(&self.0)
    }
}
impl std::error::Error for Error {}

impl Error {
    pub fn new(msg: impl std::fmt::Display) -> Self {
        Error(msg.to_string())
    }
}

impl From<String> for Error {
    fn from(s: String) -> Self {
        Error(s)
    }
}
impl From<&str> for Error {
    fn from(s: &str) -> Self {
        Error(s.to_owned())
    }
}

pub type Result<T> = std::result::Result<T, Error>;

// ───────────────────────── enums ─────────────────────────

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum LogLevel {
    Fatal = 0,
    Error = 1,
    Warn = 2,
    Info = 3,
    Debug = 4,
    Trace = 5,
}

/// Mirrors `ll::event::EventPriority`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum EventPriority {
    Highest = 0,
    High = 1,
    Normal = 2,
    Low = 3,
    Lowest = 4,
}

/// Mirrors `CommandPermissionLevel`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CommandPermission {
    Any = 0,
    GameDirectors = 1,
    Admin = 2,
    Host = 3,
    Owner = 4,
}

/// Mirrors `ll::GamingStatus`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum GamingStatus {
    Default,
    Starting,
    Running,
    Stopping,
}

// ───────────────────────── ffi helpers ─────────────────────────

fn s(text: &str) -> sys::LeviRsStr {
    sys::LeviRsStr {
        ptr: text.as_ptr(),
        len: text.len(),
    }
}

/// # Safety: `raw` must point at valid UTF-8 for `len` bytes (bridge contract),
/// and the returned borrow must not outlive the FFI call that produced `raw`.
unsafe fn r<'a>(raw: sys::LeviRsStr) -> &'a str {
    if raw.ptr.is_null() {
        return "";
    }
    std::str::from_utf8_unchecked(std::slice::from_raw_parts(raw.ptr, raw.len))
}

struct Runtime {
    api: &'static sys::LeviRsApi,
    handle: sys::LeviRsModHandle,
}
// The handle is only ever dereferenced by the bridge on the server thread.
unsafe impl Send for Runtime {}
unsafe impl Sync for Runtime {}

static RUNTIME: OnceLock<Runtime> = OnceLock::new();

fn rt() -> &'static Runtime {
    RUNTIME
        .get()
        .expect("levilamina runtime not initialized (register_mod! missing?)")
}

// ───────────────────────── public API objects ─────────────────────────

/// Per-mod logger routed through LeviLamina's logging system. Thread-safe.
#[derive(Clone, Copy)]
pub struct Logger(());

impl Logger {
    pub fn log(&self, level: LogLevel, msg: &str) {
        let rt = rt();
        unsafe { (rt.api.log)(rt.handle, level as i32, s(msg)) }
    }
    pub fn info(&self, msg: &str) {
        self.log(LogLevel::Info, msg);
    }
    pub fn warn(&self, msg: &str) {
        self.log(LogLevel::Warn, msg);
    }
    pub fn error(&self, msg: &str) {
        self.log(LogLevel::Error, msg);
    }
    pub fn debug(&self, msg: &str) {
        self.log(LogLevel::Debug, msg);
    }
    pub fn trace(&self, msg: &str) {
        self.log(LogLevel::Trace, msg);
    }
}

/// A live event subscription. Dropping it unsubscribes (RAII);
/// call [`Listener::forget`] to keep it for the lifetime of the mod.
pub struct Listener {
    raw: sys::LeviRsListenerHandle,
    cb: *mut EventCallback,
}

type EventCallback = Box<dyn FnMut(&mut EventRef)>;

impl Listener {
    /// Keep this subscription alive forever (leaks the callback — fine for
    /// listeners that should live as long as the mod).
    pub fn forget(mut self) {
        self.raw = std::ptr::null_mut();
        self.cb = std::ptr::null_mut();
        std::mem::forget(self);
    }
}

impl Drop for Listener {
    fn drop(&mut self) {
        if !self.raw.is_null() {
            let rt = rt();
            unsafe {
                (rt.api.unsubscribe_event)(rt.handle, self.raw);
                drop(Box::from_raw(self.cb)); // bridge won't call it anymore
            }
        }
    }
}

/// Event data handed to event callbacks: the event's CompoundTag as SNBT.
pub struct EventRef<'a> {
    id: &'a str,
    snbt: &'a str,
    replacement: Option<String>,
}

impl<'a> EventRef<'a> {
    /// Full event id, e.g. `ll::event::PlayerChatEvent`.
    pub fn id(&self) -> &str {
        self.id
    }
    /// Event data as SNBT (see `/levirs events` + LeviLamina event docs for fields).
    pub fn snbt(&self) -> &str {
        self.snbt
    }
    /// Replace the event data wholesale; the bridge deserializes it back into
    /// the event, which is how fields are edited and cancellable events cancelled.
    pub fn set_snbt(&mut self, snbt: impl Into<String>) {
        self.replacement = Some(snbt.into());
    }
    /// Convenience for cancellable events (v0.1: textual `cancelled` flip;
    /// a structured SNBT editor is on the roadmap).
    pub fn cancel(&mut self) {
        let base = self.replacement.as_deref().unwrap_or(self.snbt);
        if base.contains("cancelled:0b") {
            self.replacement = Some(base.replace("cancelled:0b", "cancelled:1b"));
        }
    }
}

/// Command invocation context passed to custom command handlers.
pub struct CommandInvocation<'a> {
    pub args: &'a str,
    pub origin: &'a str,
    out_ctx: *mut c_void,
    out_success: sys::LeviRsStrSink,
    out_error: sys::LeviRsStrSink,
}

impl<'a> CommandInvocation<'a> {
    pub fn success(&self, msg: &str) {
        unsafe { (self.out_success)(self.out_ctx, s(msg)) }
    }
    pub fn error(&self, msg: &str) {
        unsafe { (self.out_error)(self.out_ctx, s(msg)) }
    }
}

/// Output of [`Server::execute_command`].
#[derive(Debug, Clone)]
pub struct CommandResult {
    pub success: bool,
    pub output: String,
}

/// Handle to the server. All methods must be called on the server thread,
/// except [`Server::schedule`], [`Server::schedule_after`] and
/// [`Server::gaming_status`], which are thread-safe.
#[derive(Clone, Copy)]
pub struct Server(());

impl Server {
    /// Thread-safe accessor for use from background threads (Tokio, etc.).
    pub fn get() -> Server {
        Server(())
    }

    pub fn gaming_status(&self) -> GamingStatus {
        match unsafe { (rt().api.gaming_status)() } {
            1 => GamingStatus::Starting,
            2 => GamingStatus::Running,
            3 => GamingStatus::Stopping,
            _ => GamingStatus::Default,
        }
    }

    /// Run a closure on the server thread ASAP. Thread-safe.
    pub fn schedule(&self, f: impl FnOnce() + Send + 'static) {
        let boxed: *mut TaskOnce = Box::into_raw(Box::new(Some(Box::new(f))));
        unsafe { (rt().api.schedule)(task_trampoline, boxed.cast()) }
    }

    /// Run a closure on the server thread after `delay`. Thread-safe.
    pub fn schedule_after(&self, delay: Duration, f: impl FnOnce() + Send + 'static) {
        let boxed: *mut TaskOnce = Box::into_raw(Box::new(Some(Box::new(f))));
        unsafe {
            (rt().api.schedule_after)(task_trampoline, boxed.cast(), delay.as_millis() as u64)
        }
    }

    /// Execute a command as the server console and collect its output.
    /// Server thread only.
    pub fn execute_command(&self, cmd: &str) -> Result<CommandResult> {
        let mut result = CommandResult {
            success: false,
            output: String::new(),
        };
        unsafe extern "C" fn sink(ctx: *mut c_void, success: bool, output: sys::LeviRsStr) {
            let res = &mut *ctx.cast::<CommandResult>();
            res.success = success;
            res.output = r(output).to_owned();
        }
        let ok = unsafe {
            (rt().api.execute_command)(s(cmd), (&mut result as *mut CommandResult).cast(), sink)
        };
        if ok {
            Ok(result)
        } else {
            Err(Error("level not ready (server still starting?)".into()))
        }
    }

    /// Subscribe to a LeviLamina event by id. A unique suffix works
    /// (`"PlayerChatEvent"`); dump all ids in-game with `/levirs events`.
    /// Server thread only.
    pub fn subscribe_event(
        &self,
        event_id: &str,
        priority: EventPriority,
        callback: impl FnMut(&mut EventRef) + 'static,
    ) -> Result<Listener> {
        let cb: *mut EventCallback = Box::into_raw(Box::new(Box::new(callback)));
        let raw = unsafe {
            (rt().api.subscribe_event)(
                rt().handle,
                s(event_id),
                priority as i32,
                event_trampoline,
                cb.cast(),
            )
        };
        if raw.is_null() {
            unsafe { drop(Box::from_raw(cb)) };
            return Err(Error(format!(
                "failed to subscribe '{event_id}' (unknown or ambiguous id?)"
            )));
        }
        Ok(Listener { raw, cb })
    }

    /// Enumerate all registered event ids. Server thread only.
    pub fn list_events(&self) -> Vec<String> {
        let mut out: Vec<String> = Vec::new();
        unsafe extern "C" fn sink(ctx: *mut c_void, item: sys::LeviRsStr) {
            (*ctx.cast::<Vec<String>>()).push(r(item).to_owned());
        }
        unsafe { (rt().api.list_events)((&mut out as *mut Vec<String>).cast(), sink) };
        out
    }

    /// Register `/name [args…]`. The handler lives for the whole server
    /// lifetime (Bedrock cannot unregister commands). Call from `on_enable`.
    pub fn register_command(
        &self,
        name: &str,
        description: &str,
        permission: CommandPermission,
        handler: impl FnMut(&CommandInvocation) + 'static,
    ) -> Result<()> {
        type CommandCallback = Box<dyn FnMut(&CommandInvocation)>;
        let cb: *mut CommandCallback = Box::into_raw(Box::new(Box::new(handler)));

        unsafe extern "C" fn trampoline(
            user: *mut c_void,
            args: sys::LeviRsStr,
            origin: sys::LeviRsStr,
            out_ctx: *mut c_void,
            out_success: sys::LeviRsStrSink,
            out_error: sys::LeviRsStrSink,
        ) {
            let cb = &mut *user.cast::<CommandCallback>();
            let inv = CommandInvocation {
                args: r(args),
                origin: r(origin),
                out_ctx,
                out_success,
                out_error,
            };
            if catch_unwind(AssertUnwindSafe(|| cb(&inv))).is_err() {
                Logger(()).error("panic in command handler");
            }
        }

        let ok = unsafe {
            (rt().api.register_command)(
                rt().handle,
                s(name),
                s(description),
                permission as i32,
                trampoline,
                cb.cast(),
            )
        };
        if ok {
            Ok(()) // callback intentionally leaked: commands live forever
        } else {
            unsafe { drop(Box::from_raw(cb)) };
            Err(Error(format!("failed to register command '{name}'")))
        }
    }

    // ───── server stats (ABI v2+) ─────

    /// Current server tick (the `tickID` from `Level::getCurrentTick()`).
    /// Returns `Err` when the level is not ready. Server thread only.
    pub fn get_current_tick(&self) -> Result<u64> {
        let tick = unsafe { (rt().api.get_current_tick)() };
        if tick == 0 {
            // tickID 0 is plausible at startup; differentiate by also
            // checking delta_time as a readiness signal.
            let dt = unsafe { (rt().api.get_tick_delta_time)() };
            if dt < 0.0 {
                return Err(Error("level not ready".into()));
            }
        }
        Ok(tick)
    }

    /// Milliseconds between the last two ticks.
    /// Use [`Server::get_tps`] for a human-friendly TPS value.
    /// Returns `Err` when unavailable. Server thread only.
    pub fn get_tick_delta_time(&self) -> Result<f64> {
        let dt = unsafe { (rt().api.get_tick_delta_time)() };
        if dt < 0.0 {
            return Err(Error("level not ready".into()));
        }
        Ok(dt)
    }

    /// Calculated TPS = 1000.0 / tick_delta_time.
    /// A healthy server runs at 20.0 TPS (50 ms per tick).
    /// Returns `Err` when the level is not ready. Server thread only.
    pub fn get_tps(&self) -> Result<f64> {
        let dt = self.get_tick_delta_time()?;
        if dt <= 0.0 {
            return Err(Error("invalid tick delta time".into()));
        }
        Ok(1000.0 / dt)
    }

    /// Number of currently connected players.
    /// Returns `Err` when the level is not ready. Server thread only.
    pub fn get_active_player_count(&self) -> Result<i32> {
        let count = unsafe { (rt().api.get_player_count)() };
        // When the level is not ready, player_count would be 0 which is
        // indistinguishable. Cross-check with tick info.
        let dt = unsafe { (rt().api.get_tick_delta_time)() };
        if dt < 0.0 && count == 0 {
            return Err(Error("level not ready".into()));
        }
        Ok(count)
    }

    /// Whether the simulation is currently paused.
    /// Returns `Err` when unavailable. Server thread only.
    pub fn is_sim_paused(&self) -> Result<bool> {
        let dt = unsafe { (rt().api.get_tick_delta_time)() };
        if dt < 0.0 {
            return Err(Error("level not ready".into()));
        }
        Ok(unsafe { (rt().api.get_sim_paused)() })
    }
}

type TaskOnce = Option<Box<dyn FnOnce() + Send>>;

unsafe extern "C" fn task_trampoline(user: *mut c_void) {
    let mut boxed: Box<TaskOnce> = Box::from_raw(user.cast());
    if let Some(f) = boxed.take() {
        if catch_unwind(AssertUnwindSafe(f)).is_err() {
            Logger(()).error("panic in scheduled task");
        }
    }
}

unsafe extern "C" fn event_trampoline(
    user: *mut c_void,
    event_id: sys::LeviRsStr,
    snbt: sys::LeviRsStr,
    write_ctx: *mut c_void,
    write_back: sys::LeviRsStrSink,
) {
    let cb = &mut *user.cast::<EventCallback>();
    let mut ev = EventRef {
        id: r(event_id),
        snbt: r(snbt),
        replacement: None,
    };
    if catch_unwind(AssertUnwindSafe(|| cb(&mut ev))).is_err() {
        Logger(()).error("panic in event handler");
        return;
    }
    if let Some(new_snbt) = ev.replacement {
        write_back(write_ctx, s(&new_snbt));
    }
}

/// Everything a mod needs, passed to lifecycle hooks.
pub struct ModContext(());

impl ModContext {
    pub fn logger(&self) -> Logger {
        Logger(())
    }
    pub fn server(&self) -> Server {
        Server(())
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
    // read past what the loader actually allocated. This is a no-op today
    // (ABI v1 has no optional trailing fields) but must stay in place so the
    // first v1.x addition is safe by construction rather than by luck.
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
            Logger(()).error(&format!("lifecycle error: {e}"));
            false
        }
        Err(_) => {
            Logger(()).error("panic in lifecycle hook");
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
            Logger(()).error(&format!("on_load failed: {e}"));
            false
        }
        Err(_) => {
            Logger(()).error("panic in on_load");
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
