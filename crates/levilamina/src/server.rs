//! The [`Server`] facade: status, scheduling, events, commands, clock,
//! weather, difficulty, game rules, world read/write, spawning, and server
//! info. v0.x methods are unchanged; v1.0.0 adds the sections marked below.

use std::ffi::c_void;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::time::Duration;

use crate::command::{CommandBuilder, CommandInvocation, CommandPermission, CommandResult};
use crate::entity::Entity;
use crate::error::{Error, Result};
use crate::event::{event_trampoline, EventCallback, EventPriority, EventRef, Listener};
use crate::ffi::{call_out_str, r, s};
use crate::logger::Logger;
use crate::nbt::NbtValue;
use crate::player::PlayerInfo;
use crate::world::{BlockInfo, EntityInfo, PlayerPos, Scan};
use crate::{rt, sys};

/// Mirrors `ll::GamingStatus`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum GamingStatus {
    Default,
    Starting,
    Running,
    Stopping,
}

/// Weather states for [`Server::set_weather`].
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Weather {
    Clear = 0,
    Rain = 1,
    Thunder = 2,
}

pub(crate) type TaskOnce = Option<Box<dyn FnOnce() + Send>>;

pub(crate) unsafe extern "C" fn task_trampoline(user: *mut c_void) {
    let mut boxed: Box<TaskOnce> = Box::from_raw(user.cast());
    if let Some(f) = boxed.take() {
        if catch_unwind(AssertUnwindSafe(f)).is_err() {
            Logger::get().error("panic in scheduled task");
        }
    }
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
        Ok(Listener::new(raw, cb))
    }

    /// Enumerate all registered event ids. Server thread only.
    pub fn list_events(&self) -> Vec<String> {
        crate::ffi::collect_strs(|ctx, sink| unsafe { (rt().api.list_events)(ctx, sink) })
    }

    /// Register `/name [args…]` taking one raw-text argument. The handler
    /// lives for the whole server lifetime (Bedrock cannot unregister
    /// commands). Call from `on_enable`. For typed parameters use
    /// [`Server::command`] instead.
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
            type CommandCallback = Box<dyn FnMut(&CommandInvocation)>;
            let cb = &mut *user.cast::<CommandCallback>();
            let inv = CommandInvocation {
                args: r(args),
                origin: r(origin),
                out_ctx,
                out_success,
                out_error,
            };
            if catch_unwind(AssertUnwindSafe(|| cb(&inv))).is_err() {
                Logger::get().error("panic in command handler");
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

    // ───── parameterized commands (v1.0.0) ─────

    /// Start building a parameterized command with typed overloads.
    /// See [`CommandBuilder`] for a full example. Call from `on_enable`.
    pub fn command(
        &self,
        name: &str,
        description: &str,
        permission: CommandPermission,
    ) -> CommandBuilder {
        CommandBuilder::new(name, description, permission)
    }

    /// Register a hard enum for `ParamType::Enum` parameters:
    /// `server.register_command_enum("warp_action", &[("add", 0), ("remove", 1)])`.
    pub fn register_command_enum(&self, name: &str, values: &[(&str, u64)]) -> Result<()> {
        let list = NbtValue::List(
            values
                .iter()
                .map(|(v, idx)| {
                    NbtValue::List(vec![
                        NbtValue::String((*v).to_owned()),
                        NbtValue::Long(*idx as i64),
                    ])
                })
                .collect(),
        );
        let mut spec = NbtValue::compound();
        spec.insert("values", list);
        let ok = unsafe { (rt().api.register_command_enum)(s(name), s(&spec.to_snbt())) };
        if ok {
            Ok(())
        } else {
            Err(Error(format!("register_command_enum('{name}') failed")))
        }
    }

    /// Register a soft enum (suggestions only; free text still accepted).
    pub fn register_command_soft_enum(&self, name: &str, values: &[&str]) -> Result<()> {
        let mut spec = NbtValue::compound();
        spec.insert(
            "values",
            NbtValue::List(
                values
                    .iter()
                    .map(|v| NbtValue::String((*v).to_owned()))
                    .collect(),
            ),
        );
        let ok = unsafe { (rt().api.register_command_soft_enum)(s(name), s(&spec.to_snbt())) };
        if ok {
            Ok(())
        } else {
            Err(Error(format!(
                "register_command_soft_enum('{name}') failed"
            )))
        }
    }

    /// Update a soft enum's values: `op` semantics — replace all / add / remove.
    pub fn update_command_soft_enum(
        &self,
        name: &str,
        op: SoftEnumOp,
        values: &[&str],
    ) -> Result<()> {
        let mut spec = NbtValue::compound();
        spec.insert(
            "values",
            NbtValue::List(
                values
                    .iter()
                    .map(|v| NbtValue::String((*v).to_owned()))
                    .collect(),
            ),
        );
        let ok =
            unsafe { (rt().api.update_command_soft_enum)(s(name), op as i32, s(&spec.to_snbt())) };
        if ok {
            Ok(())
        } else {
            Err(Error(format!("update_command_soft_enum('{name}') failed")))
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

    /// Seconds taken by the last tick (0.05 s at a healthy 20 TPS).
    /// Use [`Server::get_tps`] for a human-friendly TPS value.
    /// Returns `Err` when unavailable. Server thread only.
    pub fn get_tick_delta_time(&self) -> Result<f64> {
        let dt = unsafe { (rt().api.get_tick_delta_time)() };
        if dt < 0.0 {
            return Err(Error("level not ready".into()));
        }
        Ok(dt)
    }

    /// Calculated TPS, capped at the vanilla 20.0.
    ///
    /// The underlying `mTickDeltaTime` is the time of the last tick **in
    /// seconds** (0.05 s at a healthy 20 TPS), so TPS = 1.0 / dt — not
    /// 1000.0 / dt. A tick can momentarily be faster than 50 ms, which would
    /// give >20; the game never runs faster than 20 TPS, so we clamp.
    /// Returns `Err` when the level is not ready. Server thread only.
    pub fn get_tps(&self) -> Result<f64> {
        let dt = self.get_tick_delta_time()?;
        if dt <= 0.0 {
            return Err(Error("invalid tick delta time".into()));
        }
        Ok((1.0 / dt).min(20.0))
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

    // ───── world reading (ABI v3) ─────

    /// Spawn a particle effect at a world coordinate. Server thread only.
    /// `dim`: 0 = overworld, 1 = nether, 2 = the end.
    pub fn spawn_particle(&self, dim: i32, effect: &str, x: f64, y: f64, z: f64) -> Result<()> {
        let ok = unsafe { (rt().api.spawn_particle)(dim, s(effect), x, y, z) };
        if ok {
            Ok(())
        } else {
            Err(Error("level/dimension not ready".into()))
        }
    }

    /// Feet position + dimension of a connected player, by name.
    /// Returns `None` if no such player is online. Server thread only.
    pub fn player_position(&self, name: &str) -> Option<PlayerPos> {
        let p = unsafe { (rt().api.get_player_position)(s(name)) };
        if p.found {
            Some(PlayerPos {
                x: p.x,
                y: p.y,
                z: p.z,
                dim: p.dimension,
            })
        } else {
            None
        }
    }

    /// Scan a cuboid region (corners inclusive, order-independent) into a
    /// [`Scan`]: one [`crate::ScanLayer`] per Y level, each a 2-D grid of
    /// [`crate::Cell`]s holding the block and any entities in that cell.
    /// Server thread only.
    pub fn scan_region(&self, dim: i32, a: (i32, i32, i32), b: (i32, i32, i32)) -> Result<Scan> {
        let min = (a.0.min(b.0), a.1.min(b.1), a.2.min(b.2));
        let max = (a.0.max(b.0), a.1.max(b.1), a.2.max(b.2));
        let mut scan = Scan::new(min, max);

        // The sinks push into `scan` via a raw pointer valid only for this call.
        unsafe extern "C" fn block_sink(
            ctx: *mut c_void,
            x: i32,
            y: i32,
            z: i32,
            name: sys::LeviRsStr,
            snbt: sys::LeviRsStr,
        ) {
            let scan = &mut *ctx.cast::<Scan>();
            if let Some(cell) = scan.cell_mut(x, y, z) {
                cell.block = BlockInfo {
                    name: r(name).to_owned(),
                    snbt: r(snbt).to_owned(),
                };
            }
        }
        unsafe extern "C" fn entity_sink(
            ctx: *mut c_void,
            x: i32,
            y: i32,
            z: i32,
            kind: sys::LeviRsStr,
            snbt: sys::LeviRsStr,
        ) {
            let scan = &mut *ctx.cast::<Scan>();
            if let Some(cell) = scan.cell_mut(x, y, z) {
                cell.entities.push(EntityInfo {
                    kind: r(kind).to_owned(),
                    snbt: r(snbt).to_owned(),
                });
            }
        }

        let ok = unsafe {
            (rt().api.scan_region)(
                dim,
                min.0,
                min.1,
                min.2,
                max.0,
                max.1,
                max.2,
                (&mut scan as *mut Scan).cast(),
                block_sink,
                entity_sink,
            )
        };
        if ok {
            Ok(scan)
        } else {
            Err(Error("level/dimension not ready".into()))
        }
    }

    // ───── world writing & clock (v1.0.0) ─────

    /// Read one block: `(type_name, serialization SNBT)`. Prefer
    /// [`crate::Block::at`] for the full property surface.
    pub fn get_block(&self, dim: i32, x: i32, y: i32, z: i32) -> Result<(String, String)> {
        unsafe extern "C" fn sink(
            ctx: *mut c_void,
            _x: i32,
            _y: i32,
            _z: i32,
            name: sys::LeviRsStr,
            snbt: sys::LeviRsStr,
        ) {
            *ctx.cast::<Option<(String, String)>>() =
                Some((r(name).to_owned(), r(snbt).to_owned()));
        }
        let mut out: Option<(String, String)> = None;
        let ok = unsafe {
            (rt().api.get_block)(
                dim,
                x,
                y,
                z,
                (&mut out as *mut Option<(String, String)>).cast(),
                sink,
            )
        };
        if !ok {
            return Err(Error("get_block: level/dimension not ready".into()));
        }
        out.ok_or_else(|| Error("get_block: no result".into()))
    }

    /// Replace one block. `spec` is anything `/setblock` accepts.
    pub fn set_block(&self, dim: i32, x: i32, y: i32, z: i32, spec: &str) -> Result<()> {
        let ok = unsafe { (rt().api.set_block)(dim, x, y, z, s(spec)) };
        if ok {
            Ok(())
        } else {
            Err(Error(format!("set_block failed for '{spec}'")))
        }
    }

    /// World time in ticks (`Level::getTime`).
    pub fn time(&self) -> Result<i64> {
        let mut out = 0i64;
        let ok = unsafe { (rt().api.get_time)(&mut out) };
        if ok {
            Ok(out)
        } else {
            Err(Error("level not ready".into()))
        }
    }

    /// `/time set <t>`.
    pub fn set_time(&self, t: i64) -> Result<()> {
        let ok = unsafe { (rt().api.set_time)(t) };
        if ok {
            Ok(())
        } else {
            Err(Error("set_time failed".into()))
        }
    }

    pub fn set_weather(&self, weather: Weather) -> Result<()> {
        let ok = unsafe { (rt().api.set_weather)(weather as i32) };
        if ok {
            Ok(())
        } else {
            Err(Error("set_weather failed".into()))
        }
    }

    /// Difficulty raw value: 0=peaceful 1=easy 2=normal 3=hard.
    pub fn difficulty(&self) -> Result<i32> {
        let mut out = 0i32;
        let ok = unsafe { (rt().api.get_difficulty)(&mut out) };
        if ok {
            Ok(out)
        } else {
            Err(Error("level not ready".into()))
        }
    }

    pub fn set_difficulty(&self, d: i32) -> Result<()> {
        let ok = unsafe { (rt().api.set_difficulty)(d) };
        if ok {
            Ok(())
        } else {
            Err(Error("set_difficulty failed (0..=3?)".into()))
        }
    }

    /// The world seed.
    pub fn seed(&self) -> Result<i64> {
        let mut out = 0i64;
        let ok = unsafe { (rt().api.get_seed)(&mut out) };
        if ok {
            Ok(out)
        } else {
            Err(Error("level not ready".into()))
        }
    }

    /// Read a game rule: `{type:"bool"|"int"|"float", value:…}` decoded to a
    /// structured value.
    pub fn game_rule(&self, name: &str) -> Result<NbtValue> {
        let raw = call_out_str(|ctx, sink| unsafe { (rt().api.game_rule_get)(s(name), ctx, sink) })
            .ok_or_else(|| Error(format!("unknown game rule '{name}'")))?;
        NbtValue::parse(&raw)
    }

    /// `/gamerule <name> <value>` — the engine validates both.
    pub fn set_game_rule(&self, name: &str, value: &str) -> Result<()> {
        let ok = unsafe { (rt().api.game_rule_set)(s(name), s(value)) };
        if ok {
            Ok(())
        } else {
            Err(Error(format!("set_game_rule('{name}') failed")))
        }
    }

    // ───── spawning & destruction (v1.0.0) ─────

    /// Spawn a mob; returns its [`Entity`] handle.
    /// `type_name`: e.g. `"minecraft:zombie"`.
    pub fn spawn_mob(&self, dim: i32, type_name: &str, x: f64, y: f64, z: f64) -> Result<Entity> {
        let mut id: sys::LeviRsActorId = 0;
        let ok = unsafe { (rt().api.spawn_mob)(dim, s(type_name), x, y, z, &mut id) };
        if ok {
            Ok(Entity::from_id(id))
        } else {
            Err(Error(format!("spawn_mob('{type_name}') failed")))
        }
    }

    /// Create an explosion. `source`: optional entity credited with the blast.
    #[allow(clippy::too_many_arguments)]
    pub fn explode(
        &self,
        dim: i32,
        x: f64,
        y: f64,
        z: f64,
        radius: f32,
        source: Option<&Entity>,
        fire: bool,
        breaks_blocks: bool,
    ) -> Result<()> {
        let ok = unsafe {
            (rt().api.explode)(
                dim,
                x,
                y,
                z,
                radius,
                f32::MAX, // max_resistance: no artificial cap
                source.map(|e| e.id()).unwrap_or(0),
                fire,
                breaks_blocks,
                false,
            )
        };
        if ok {
            Ok(())
        } else {
            Err(Error("explode failed (level/dimension not ready?)".into()))
        }
    }

    // ───── player enumeration & server info (v1.0.0) ─────

    /// Every online player (identity + position). Same data as
    /// [`crate::Player::list`].
    pub fn list_players(&self) -> Vec<PlayerInfo> {
        crate::player::Player::list()
    }

    /// BDS game version, e.g. `1.26.x`.
    pub fn bds_version(&self) -> Result<String> {
        call_out_str(|ctx, sink| unsafe {
            (rt().api.server_info_str)(sys::SRV_BDS_VERSION, ctx, sink)
        })
        .ok_or_else(|| Error("server_info unavailable".into()))
    }

    /// Network protocol version.
    pub fn protocol_version(&self) -> Result<i32> {
        call_out_str(|ctx, sink| unsafe {
            (rt().api.server_info_str)(sys::SRV_PROTOCOL_VERSION, ctx, sink)
        })
        .and_then(|v| v.parse().ok())
        .ok_or_else(|| Error("server_info unavailable".into()))
    }
}

/// How [`Server::update_command_soft_enum`] changes the value set.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SoftEnumOp {
    /// Replace the whole value set.
    Set = 0,
    Add = 1,
    Remove = 2,
}
