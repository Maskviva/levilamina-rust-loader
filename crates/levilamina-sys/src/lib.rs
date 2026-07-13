//! Raw FFI declarations mirroring `src/LeviRsAbi.h` (ABI v5).
//!
//! This crate contains no logic — only `#[repr(C)]` types. Keep it in
//! lockstep with the C header: fields are append-only, never reordered.
//! `tools/check_abi_sync.py` cross-checks the field order of this file,
//! the C header, and the loader's table initializer.
//!
//! You almost certainly want the safe `levilamina` crate instead.

#![no_std]
#![allow(non_camel_case_types)]

use core::ffi::c_void;

pub const LEVI_RS_ABI_VERSION: u32 = 5;
pub const LEVI_RS_MAIN_SYMBOL: &str = "levi_rs_main";

/// UTF-8 string view. Not guaranteed NUL-terminated.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct LeviRsStr {
    pub ptr: *const u8,
    pub len: usize,
}

pub type LeviRsModHandle = *mut c_void;
pub type LeviRsListenerHandle = *mut c_void;

pub type LeviRsTaskCb = unsafe extern "C" fn(user: *mut c_void);
pub type LeviRsStrSink = unsafe extern "C" fn(ctx: *mut c_void, s: LeviRsStr);

pub type LeviRsEventCb = unsafe extern "C" fn(
    user: *mut c_void,
    event_id: LeviRsStr,
    snbt: LeviRsStr,
    write_ctx: *mut c_void,
    write_back: LeviRsStrSink,
);

pub type LeviRsCommandCb = unsafe extern "C" fn(
    user: *mut c_void,
    args: LeviRsStr,
    origin_name: LeviRsStr,
    out_ctx: *mut c_void,
    out_success: LeviRsStrSink,
    out_error: LeviRsStrSink,
);

pub type LeviRsCmdOutputSink =
    unsafe extern "C" fn(ctx: *mut c_void, success: bool, output: LeviRsStr);

// ── ABI v3: world reading ──

/// A player's feet position + dimension. Mirrors `LeviRsPlayerPos`.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct LeviRsPlayerPos {
    pub x: f64,
    pub y: f64,
    pub z: f64,
    pub dimension: i32,
    pub found: bool,
}

/// Block sink: one call per cell during scan_region. snbt = full block serialization.
pub type LeviRsBlockSink = unsafe extern "C" fn(
    ctx: *mut c_void,
    x: i32,
    y: i32,
    z: i32,
    name: LeviRsStr,
    snbt: LeviRsStr,
);

/// Entity sink: one call per entity found. x,y,z = the containing block cell.
pub type LeviRsEntitySink = unsafe extern "C" fn(
    ctx: *mut c_void,
    x: i32,
    y: i32,
    z: i32,
    kind: LeviRsStr,
    snbt: LeviRsStr,
);

// ── ABI v5: types ──

/// Player selector: kind 0 = name, 1 = xuid, 2 = uuid. Mirrors `LeviRsPlayerSel`.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct LeviRsPlayerSel {
    pub kind: i32,
    pub value: LeviRsStr,
}

/// ActorUniqueID raw value. 0 never resolves.
pub type LeviRsActorId = i64;

/// Container reference: which 0=inventory 1=ender_chest 2=armor 3=offhand 4=block.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct LeviRsContainerRef {
    pub which: i32,
    pub player: LeviRsPlayerSel,
    pub dim: i32,
    pub x: i32,
    pub y: i32,
    pub z: i32,
}

/// Raw byte sink (binary NBT). Bytes valid only within the call frame.
pub type LeviRsBytesSink = unsafe extern "C" fn(ctx: *mut c_void, data: *const u8, len: usize);
/// Key/value sink (kvdb_iter).
pub type LeviRsKvSink = unsafe extern "C" fn(ctx: *mut c_void, key: LeviRsStr, value: LeviRsStr);
/// Actor sink (list_actors).
pub type LeviRsActorSink =
    unsafe extern "C" fn(ctx: *mut c_void, id: LeviRsActorId, type_name: LeviRsStr);
/// Form result callback: fires once, on the server thread, with result SNBT.
pub type LeviRsFormResultCb = unsafe extern "C" fn(user: *mut c_void, result_snbt: LeviRsStr);

/// Opaque handle to an open key-value database owned by the loader.
pub type LeviRsKvDbHandle = *mut c_void;

// ── v5 property / action keys (append-only; unknown values → call returns false) ──

// LeviRsPlayerNumProp
pub const PPROP_GAME_TYPE: i32 = 0;
pub const PPROP_LEVEL: i32 = 1;
pub const PPROP_EXPERIENCE: i32 = 2;
pub const PPROP_HUNGER: i32 = 3;
pub const PPROP_SATURATION: i32 = 4;
pub const PPROP_EXHAUSTION: i32 = 5;
pub const PPROP_XP_NEEDED_NEXT_LEVEL: i32 = 6;
pub const PPROP_LUCK: i32 = 7;
pub const PPROP_SELECTED_SLOT: i32 = 8;
pub const PPROP_IS_OPERATOR: i32 = 9;
pub const PPROP_CAN_USE_OPERATOR_BLOCKS: i32 = 10;
pub const PPROP_IS_FLYING: i32 = 11;
pub const PPROP_CAN_JUMP: i32 = 12;
pub const PPROP_IS_EMOTING: i32 = 13;
pub const PPROP_IS_IN_RAID: i32 = 14;
pub const PPROP_IS_HURT: i32 = 15;
pub const PPROP_IS_SCOPING: i32 = 16;
pub const PPROP_CAN_SLEEP: i32 = 17;
pub const PPROP_HAS_RESPAWN_POSITION: i32 = 18;
pub const PPROP_CLIENT_SUB_ID: i32 = 19;
pub const PPROP_CAN_USE_ABILITY: i32 = 20;

// LeviRsPlayerStrProp
pub const PSTR_REAL_NAME: i32 = 0;
pub const PSTR_UUID: i32 = 1;
pub const PSTR_XUID: i32 = 2;
pub const PSTR_IP_AND_PORT: i32 = 3;
pub const PSTR_LOCALE_CODE: i32 = 4;
pub const PSTR_NAME_TAG: i32 = 5;

// LeviRsPlayerAction
pub const PACT_SET_ABILITY: i32 = 0;
pub const PACT_CAN_USE_ABILITY: i32 = 1;
pub const PACT_SET_SELECTED_SLOT: i32 = 2;
pub const PACT_GIVE_ITEM: i32 = 3;
pub const PACT_SET_SPAWN_POINT: i32 = 4;
pub const PACT_CLEAR_TITLE: i32 = 5;
pub const PACT_SET_TITLE: i32 = 6;

// LeviRsActorNumProp
pub const APROP_POS_X: i32 = 0;
pub const APROP_POS_Y: i32 = 1;
pub const APROP_POS_Z: i32 = 2;
pub const APROP_ROT_PITCH: i32 = 3;
pub const APROP_ROT_YAW: i32 = 4;
pub const APROP_DIMENSION: i32 = 5;
pub const APROP_HEALTH: i32 = 6;
pub const APROP_MAX_HEALTH: i32 = 7;
pub const APROP_IS_ALIVE: i32 = 8;
pub const APROP_IS_ON_GROUND: i32 = 9;
pub const APROP_IS_IN_WATER: i32 = 10;
pub const APROP_IS_IN_LAVA: i32 = 11;
pub const APROP_IS_ON_FIRE: i32 = 12;
pub const APROP_IS_INVISIBLE: i32 = 13;
pub const APROP_IS_SNEAKING: i32 = 14;
pub const APROP_IS_BABY: i32 = 15;
pub const APROP_IS_RIDING: i32 = 16;
pub const APROP_IS_TAME: i32 = 17;
pub const APROP_SPEED: i32 = 18;

// LeviRsActorStrProp
pub const ASTR_TYPE_NAME: i32 = 0;
pub const ASTR_NAME_TAG: i32 = 1;

// LeviRsActorAction
pub const AACT_KILL: i32 = 0;
pub const AACT_DESPAWN: i32 = 1;
pub const AACT_HEAL: i32 = 2;
pub const AACT_SET_ON_FIRE: i32 = 3;
pub const AACT_TELEPORT: i32 = 4;
pub const AACT_SET_NAME_TAG: i32 = 5;
pub const AACT_ADD_TAG: i32 = 6;
pub const AACT_REMOVE_TAG: i32 = 7;
pub const AACT_HAS_TAG: i32 = 8;
pub const AACT_ADD_EFFECT: i32 = 9;
pub const AACT_REMOVE_EFFECT: i32 = 10;
pub const AACT_CLEAR_EFFECTS: i32 = 11;
pub const AACT_HURT: i32 = 12;
pub const AACT_ATTRIBUTE_GET: i32 = 13;

// LeviRsBlockNumProp
pub const BPROP_IS_AIR: i32 = 0;
pub const BPROP_DATA: i32 = 1;
pub const BPROP_BLOCK_ITEM_ID: i32 = 2;
pub const BPROP_IS_CRAFTING_BLOCK: i32 = 3;
pub const BPROP_IS_INTERACTIVE_BLOCK: i32 = 4;
pub const BPROP_HAS_BLOCK_ENTITY: i32 = 5;

// LeviRsBlockStrProp
pub const BSTR_TYPE_NAME: i32 = 0;
pub const BSTR_SNBT: i32 = 1;
pub const BSTR_DESCRIPTION_ID: i32 = 2;
pub const BSTR_DEBUG_STRING: i32 = 3;
pub const BSTR_TAGS: i32 = 4;

// LeviRsBlockAction
pub const BACT_HAS_TAG: i32 = 0;

// LeviRsItemNumProp
pub const IPROP_COUNT: i32 = 0;
pub const IPROP_MAX_STACK_SIZE: i32 = 1;
pub const IPROP_AUX_VALUE: i32 = 2;
pub const IPROP_ID: i32 = 3;
pub const IPROP_DAMAGE: i32 = 4;
pub const IPROP_IS_NULL: i32 = 5;
pub const IPROP_IS_BLOCK: i32 = 6;
pub const IPROP_IS_ENCHANTED: i32 = 7;
pub const IPROP_IS_ARMOR: i32 = 8;
pub const IPROP_IS_DAMAGEABLE: i32 = 9;
pub const IPROP_IS_DAMAGED: i32 = 10;

// LeviRsItemStrProp
pub const ISTR_TYPE_NAME: i32 = 0;
pub const ISTR_NAME: i32 = 1;
pub const ISTR_CUSTOM_NAME: i32 = 2;
pub const ISTR_RAW_NAME_ID: i32 = 3;

// LeviRsItemOp
pub const IOP_SET_CUSTOM_NAME: i32 = 0;
pub const IOP_SET_DAMAGE: i32 = 1;
pub const IOP_SET_COUNT: i32 = 2;
pub const IOP_SET_LORE: i32 = 3;

// LeviRsScoreboardOp
pub const SB_ADD_OBJECTIVE: i32 = 0;
pub const SB_REMOVE_OBJECTIVE: i32 = 1;
pub const SB_LIST_OBJECTIVES: i32 = 2;
pub const SB_GET_SCORE: i32 = 3;
pub const SB_SET_SCORE: i32 = 4;
pub const SB_ADD_SCORE: i32 = 5;
pub const SB_REDUCE_SCORE: i32 = 6;
pub const SB_RESET_SCORE: i32 = 7;
pub const SB_SET_DISPLAY: i32 = 8;
pub const SB_CLEAR_DISPLAY: i32 = 9;

// LeviRsSysInfoProp
pub const SYS_OS_NAME: i32 = 0;
pub const SYS_OS_VERSION: i32 = 1;
pub const SYS_LOCALE: i32 = 2;
pub const SYS_LOCAL_TIME: i32 = 3;

// LeviRsServerInfoProp
pub const SRV_BDS_VERSION: i32 = 0;
pub const SRV_PROTOCOL_VERSION: i32 = 1;

/// Function table handed to the Rust mod. Mirrors `LeviRsApi`.
/// FIELD ORDER IS THE ABI — append-only, verified by tools/check_abi_sync.py.
#[repr(C)]
pub struct LeviRsApi {
    pub abi_version: u32,
    pub struct_size: u32,

    /// level: -1=Off, 0=Fatal, 1=Error, 2=Warn, 3=Info, 4=Debug, 5=Trace. Thread-safe.
    pub log: unsafe extern "C" fn(mod_: LeviRsModHandle, level: i32, msg: LeviRsStr),
    /// 0=Default, 1=Starting, 2=Running, 3=Stopping. Thread-safe.
    pub gaming_status: unsafe extern "C" fn() -> i32,
    /// Queue onto the server thread ASAP. Thread-safe.
    pub schedule: unsafe extern "C" fn(cb: LeviRsTaskCb, user: *mut c_void),
    /// Queue onto the server thread after `delay_ms`. Thread-safe.
    pub schedule_after: unsafe extern "C" fn(cb: LeviRsTaskCb, user: *mut c_void, delay_ms: u64),

    /// Server thread only. priority 0..4 (Highest..Lowest), 2 = Normal.
    pub subscribe_event: unsafe extern "C" fn(
        mod_: LeviRsModHandle,
        event_id: LeviRsStr,
        priority: i32,
        cb: LeviRsEventCb,
        user: *mut c_void,
    ) -> LeviRsListenerHandle,
    /// Server thread only.
    pub unsubscribe_event:
        unsafe extern "C" fn(mod_: LeviRsModHandle, listener: LeviRsListenerHandle) -> bool,
    /// Server thread only.
    pub list_events: unsafe extern "C" fn(ctx: *mut c_void, sink: LeviRsStrSink),

    /// Server thread only. Executes as console (Owner).
    pub execute_command:
        unsafe extern "C" fn(cmd: LeviRsStr, ctx: *mut c_void, sink: LeviRsCmdOutputSink) -> bool,
    /// Server thread only, call during on_enable. permission 0..4.
    pub register_command: unsafe extern "C" fn(
        mod_: LeviRsModHandle,
        name: LeviRsStr,
        description: LeviRsStr,
        permission: i32,
        cb: LeviRsCommandCb,
        user: *mut c_void,
    ) -> bool,

    /// Current server tick (tickID). Returns 0 when level is not ready. Server thread only.
    pub get_current_tick: unsafe extern "C" fn() -> u64,
    /// Milliseconds between last two ticks. TPS = 1000.0 / delta_time. -1.0 if unavailable. Server thread only.
    pub get_tick_delta_time: unsafe extern "C" fn() -> f64,
    /// Number of currently connected players. Server thread only.
    pub get_player_count: unsafe extern "C" fn() -> i32,
    /// Whether the simulation is currently paused. Server thread only.
    pub get_sim_paused: unsafe extern "C" fn() -> bool,

    // ── ABI v3 ──
    /// Spawn a particle effect at a world coordinate. Server thread only.
    pub spawn_particle: unsafe extern "C" fn(
        dimension: i32,
        effect_name: LeviRsStr,
        x: f64,
        y: f64,
        z: f64,
    ) -> bool,
    /// Look up a connected player's feet position + dimension by name. Server thread only.
    pub get_player_position: unsafe extern "C" fn(name: LeviRsStr) -> LeviRsPlayerPos,
    /// Scan a cuboid region; blocks_sink per cell, entities_sink per entity. Server thread only.
    #[allow(clippy::too_many_arguments)]
    pub scan_region: unsafe extern "C" fn(
        dimension: i32,
        x1: i32,
        y1: i32,
        z1: i32,
        x2: i32,
        y2: i32,
        z2: i32,
        ctx: *mut c_void,
        blocks_sink: LeviRsBlockSink,
        entities_sink: LeviRsEntitySink,
    ) -> bool,

    // ── ABI v5 §A world read/write & clock (server thread only unless noted) ──
    pub get_block: unsafe extern "C" fn(
        dim: i32,
        x: i32,
        y: i32,
        z: i32,
        ctx: *mut c_void,
        sink: LeviRsBlockSink,
    ) -> bool,
    pub set_block:
        unsafe extern "C" fn(dim: i32, x: i32, y: i32, z: i32, block_spec: LeviRsStr) -> bool,
    pub get_time: unsafe extern "C" fn(out: *mut i64) -> bool,
    pub set_time: unsafe extern "C" fn(t: i64) -> bool,
    /// 0=clear 1=rain 2=thunder.
    pub set_weather: unsafe extern "C" fn(weather: i32) -> bool,

    // ── §B player management ──
    pub list_players: unsafe extern "C" fn(ctx: *mut c_void, snbt_sink: LeviRsStrSink),
    pub player_resolve: unsafe extern "C" fn(sel: LeviRsPlayerSel, out: *mut LeviRsActorId) -> bool,
    pub player_send_message: unsafe extern "C" fn(sel: LeviRsPlayerSel, msg: LeviRsStr) -> bool,
    pub player_disconnect: unsafe extern "C" fn(sel: LeviRsPlayerSel, reason: LeviRsStr) -> bool,
    pub broadcast_message: unsafe extern "C" fn(msg: LeviRsStr),
    /// 0=survival 1=creative 2=adventure 6=spectator.
    pub player_set_gamemode: unsafe extern "C" fn(sel: LeviRsPlayerSel, mode: i32) -> bool,
    pub player_teleport:
        unsafe extern "C" fn(sel: LeviRsPlayerSel, dim: i32, x: f64, y: f64, z: f64) -> bool,
    pub player_get_num:
        unsafe extern "C" fn(sel: LeviRsPlayerSel, prop: i32, out: *mut f64) -> bool,
    pub player_get_str: unsafe extern "C" fn(
        sel: LeviRsPlayerSel,
        prop: i32,
        ctx: *mut c_void,
        sink: LeviRsStrSink,
    ) -> bool,
    pub player_set_num: unsafe extern "C" fn(sel: LeviRsPlayerSel, prop: i32, v: f64) -> bool,
    #[allow(clippy::too_many_arguments)]
    pub player_action: unsafe extern "C" fn(
        sel: LeviRsPlayerSel,
        action: i32,
        sarg: LeviRsStr,
        a: f64,
        b: f64,
        c: f64,
        ctx: *mut c_void,
        out: LeviRsStrSink,
    ) -> bool,

    // ── §C actors ──
    /// dim = -1 for all dimensions.
    pub list_actors: unsafe extern "C" fn(dim: i32, ctx: *mut c_void, sink: LeviRsActorSink),
    pub actor_snapshot:
        unsafe extern "C" fn(id: LeviRsActorId, ctx: *mut c_void, snbt_sink: LeviRsStrSink) -> bool,
    pub actor_get_num: unsafe extern "C" fn(id: LeviRsActorId, prop: i32, out: *mut f64) -> bool,
    pub actor_get_str: unsafe extern "C" fn(
        id: LeviRsActorId,
        prop: i32,
        ctx: *mut c_void,
        sink: LeviRsStrSink,
    ) -> bool,
    #[allow(clippy::too_many_arguments)]
    pub actor_action: unsafe extern "C" fn(
        id: LeviRsActorId,
        action: i32,
        sarg: LeviRsStr,
        a: f64,
        b: f64,
        c: f64,
        ctx: *mut c_void,
        out: LeviRsStrSink,
    ) -> bool,
    pub spawn_mob: unsafe extern "C" fn(
        dim: i32,
        type_name: LeviRsStr,
        x: f64,
        y: f64,
        z: f64,
        out: *mut LeviRsActorId,
    ) -> bool,
    #[allow(clippy::too_many_arguments)]
    pub explode: unsafe extern "C" fn(
        dim: i32,
        x: f64,
        y: f64,
        z: f64,
        radius: f32,
        max_resistance: f32,
        source: LeviRsActorId,
        fire: bool,
        breaks_blocks: bool,
        allow_underwater: bool,
    ) -> bool,

    // ── §D blocks & block entities ──
    pub block_get_num:
        unsafe extern "C" fn(dim: i32, x: i32, y: i32, z: i32, prop: i32, out: *mut f64) -> bool,
    #[allow(clippy::too_many_arguments)]
    pub block_get_str: unsafe extern "C" fn(
        dim: i32,
        x: i32,
        y: i32,
        z: i32,
        prop: i32,
        ctx: *mut c_void,
        sink: LeviRsStrSink,
    ) -> bool,
    #[allow(clippy::too_many_arguments)]
    pub block_action: unsafe extern "C" fn(
        dim: i32,
        x: i32,
        y: i32,
        z: i32,
        action: i32,
        sarg: LeviRsStr,
        ctx: *mut c_void,
        out: LeviRsStrSink,
    ) -> bool,
    pub block_entity_snbt: unsafe extern "C" fn(
        dim: i32,
        x: i32,
        y: i32,
        z: i32,
        ctx: *mut c_void,
        sink: LeviRsStrSink,
    ) -> bool,

    // ── §E items (SNBT value objects) & containers ──
    pub item_get_num: unsafe extern "C" fn(item_snbt: LeviRsStr, prop: i32, out: *mut f64) -> bool,
    pub item_get_str: unsafe extern "C" fn(
        item_snbt: LeviRsStr,
        prop: i32,
        ctx: *mut c_void,
        sink: LeviRsStrSink,
    ) -> bool,
    /// Rebuild → mutate → serialize; `out` receives the NEW item SNBT.
    pub item_transform: unsafe extern "C" fn(
        item_snbt: LeviRsStr,
        op: i32,
        sarg: LeviRsStr,
        narg: f64,
        ctx: *mut c_void,
        out: LeviRsStrSink,
    ) -> bool,
    pub container_size: unsafe extern "C" fn(ref_: LeviRsContainerRef, out: *mut i32) -> bool,
    pub container_get_item: unsafe extern "C" fn(
        ref_: LeviRsContainerRef,
        slot: i32,
        ctx: *mut c_void,
        sink: LeviRsStrSink,
    ) -> bool,
    pub container_set_item:
        unsafe extern "C" fn(ref_: LeviRsContainerRef, slot: i32, item_snbt: LeviRsStr) -> bool,
    pub container_add_item:
        unsafe extern "C" fn(ref_: LeviRsContainerRef, item_snbt: LeviRsStr) -> bool,
    pub container_remove_item:
        unsafe extern "C" fn(ref_: LeviRsContainerRef, slot: i32, count: i32) -> bool,
    pub container_clear: unsafe extern "C" fn(ref_: LeviRsContainerRef) -> bool,

    // ── §F scoreboard ──
    pub scoreboard_op: unsafe extern "C" fn(
        op: i32,
        a: LeviRsStr,
        b: LeviRsStr,
        n: i64,
        ctx: *mut c_void,
        out: LeviRsStrSink,
    ) -> bool,

    // ── §G forms ──
    /// kind: 0=SimpleForm 1=CustomForm 2=ModalForm. Callback fires once on
    /// the server thread; muted if the mod is disabled before response.
    pub form_send: unsafe extern "C" fn(
        mod_: LeviRsModHandle,
        sel: LeviRsPlayerSel,
        kind: i32,
        form_snbt: LeviRsStr,
        cb: LeviRsFormResultCb,
        user: *mut c_void,
    ) -> bool,

    // ── §H parameterized commands & enums ──
    #[allow(clippy::too_many_arguments)]
    pub register_command_ex: unsafe extern "C" fn(
        mod_: LeviRsModHandle,
        name: LeviRsStr,
        description: LeviRsStr,
        permission: i32,
        overloads_snbt: LeviRsStr,
        cb: LeviRsCommandCb,
        user: *mut c_void,
    ) -> bool,
    pub register_command_enum:
        unsafe extern "C" fn(name: LeviRsStr, values_snbt: LeviRsStr) -> bool,
    pub register_command_soft_enum:
        unsafe extern "C" fn(name: LeviRsStr, values_snbt: LeviRsStr) -> bool,
    /// op: 0=set 1=add 2=remove.
    pub update_command_soft_enum:
        unsafe extern "C" fn(name: LeviRsStr, op: i32, values_snbt: LeviRsStr) -> bool,

    // ── §I NBT binary, KvDb (thread-safe), system & server info ──
    /// fmt: 0=disk little-endian, 1=network.
    pub nbt_snbt_to_binary: unsafe extern "C" fn(
        snbt: LeviRsStr,
        fmt: i32,
        ctx: *mut c_void,
        sink: LeviRsBytesSink,
    ) -> bool,
    pub nbt_binary_to_snbt: unsafe extern "C" fn(
        data: *const u8,
        len: usize,
        fmt: i32,
        ctx: *mut c_void,
        sink: LeviRsStrSink,
    ) -> bool,
    pub kvdb_open: unsafe extern "C" fn(
        mod_: LeviRsModHandle,
        path: LeviRsStr,
        create_if_missing: bool,
    ) -> LeviRsKvDbHandle,
    pub kvdb_close: unsafe extern "C" fn(h: LeviRsKvDbHandle),
    pub kvdb_get: unsafe extern "C" fn(
        h: LeviRsKvDbHandle,
        key: LeviRsStr,
        ctx: *mut c_void,
        sink: LeviRsStrSink,
    ) -> bool,
    pub kvdb_set:
        unsafe extern "C" fn(h: LeviRsKvDbHandle, key: LeviRsStr, value: LeviRsStr) -> bool,
    pub kvdb_del: unsafe extern "C" fn(h: LeviRsKvDbHandle, key: LeviRsStr) -> bool,
    pub kvdb_has: unsafe extern "C" fn(h: LeviRsKvDbHandle, key: LeviRsStr) -> bool,
    pub kvdb_is_empty: unsafe extern "C" fn(h: LeviRsKvDbHandle) -> bool,
    pub kvdb_iter: unsafe extern "C" fn(h: LeviRsKvDbHandle, ctx: *mut c_void, sink: LeviRsKvSink),
    pub sys_info_str:
        unsafe extern "C" fn(prop: i32, ctx: *mut c_void, sink: LeviRsStrSink) -> bool,
    pub sys_get_env:
        unsafe extern "C" fn(name: LeviRsStr, ctx: *mut c_void, sink: LeviRsStrSink) -> bool,
    pub sys_set_env: unsafe extern "C" fn(name: LeviRsStr, value: LeviRsStr) -> bool,
    pub sys_is_wine: unsafe extern "C" fn() -> bool,
    pub get_difficulty: unsafe extern "C" fn(out: *mut i32) -> bool,
    pub set_difficulty: unsafe extern "C" fn(d: i32) -> bool,
    pub get_seed: unsafe extern "C" fn(out: *mut i64) -> bool,
    /// sink receives SNBT {type:"bool"|"int"|"float", value:…}.
    pub game_rule_get:
        unsafe extern "C" fn(name: LeviRsStr, ctx: *mut c_void, sink: LeviRsStrSink) -> bool,
    pub game_rule_set: unsafe extern "C" fn(name: LeviRsStr, value: LeviRsStr) -> bool,
    pub server_info_str:
        unsafe extern "C" fn(prop: i32, ctx: *mut c_void, sink: LeviRsStrSink) -> bool,
    /// Per-player particle packet (additive, gated by `struct_size`): sends a
    /// `SpawnParticleEffectPacket` only to the resolved player instead of the
    /// dimension-wide broadcast. False if the player can't be resolved.
    pub spawn_particle_for: unsafe extern "C" fn(
        sel: LeviRsPlayerSel,
        dimension: i32,
        effect_name: LeviRsStr,
        x: f64,
        y: f64,
        z: f64,
    ) -> bool,
    /// Raw per-connection packet send — the generic primitive
    /// `spawn_particle_for` derives from. `packet_id` is a MinecraftPacketIds
    /// value; `body` is the packet's wire-format body for the CURRENT game
    /// version (version-specific; caller's responsibility). False on offline
    /// player, unknown id, parse failure, or leftover bytes after parsing.
    pub send_packet: unsafe extern "C" fn(
        sel: LeviRsPlayerSel,
        packet_id: i32,
        body: *const u8,
        body_len: usize,
    ) -> bool,
    /// Freeze the world clock (mobs/blocks/redstone/time stop; players can
    /// still move and chat). Backed by a bridge-owned detour on `Level::tick`,
    /// installed lazily and left in place. Server thread only.
    pub tick_freeze: unsafe extern "C" fn(on: bool) -> bool,
    /// Only while frozen: queue exactly `n` extra frames. False if not frozen
    /// or `n == 0`.
    pub tick_step: unsafe extern "C" fn(n: u32) -> bool,
    /// `0 < factor <= 100`; fractional = slow motion, `1.0` restores normal.
    pub tick_warp: unsafe extern "C" fn(factor: f64) -> bool,
    /// Arm a profiling window of `ticks` level ticks (1..=12000). False if
    /// 0, too big, or already sampling.
    pub profile_begin: unsafe extern "C" fn(ticks: u32) -> bool,
    /// Poll for the finished report; true exactly once per window, sinking
    /// one SNBT report (bucket times are inclusive — don't sum them).
    pub profile_take: unsafe extern "C" fn(ctx: *mut c_void, sink: LeviRsStrSink) -> bool,
    /// Spawn a simulated player. It's a real ServerPlayer: all per-player
    /// entries work on it via the usual name selector.
    pub sim_spawn:
        unsafe extern "C" fn(name: LeviRsStr, dimension: i32, x: f64, y: f64, z: f64) -> bool,
    /// Multiplexed simulate* verb dispatcher (args as SNBT; unknown verb /
    /// malformed args / non-sim target => false).
    pub sim_do:
        unsafe extern "C" fn(sel: LeviRsPlayerSel, action: LeviRsStr, args_snbt: LeviRsStr) -> bool,
    /// True if the selector resolves to a live simulated player (re-validate
    /// a bot after a restart).
    pub sim_is: unsafe extern "C" fn(sel: LeviRsPlayerSel) -> bool,
    /// Enumerate live simulated-player names (sink receives each name).
    pub sim_list: unsafe extern "C" fn(ctx: *mut c_void, name_sink: LeviRsStrSink),
    /// Enumerate villages in a dimension (one SNBT object per village).
    pub villages: unsafe extern "C" fn(dimension: i32, ctx: *mut c_void, snbt_sink: LeviRsStrSink),
    /// Hardcoded spawn areas near a point, loaded chunks only (one SNBT
    /// object per area).
    pub structures_near: unsafe extern "C" fn(
        dimension: i32,
        x: i32,
        y: i32,
        z: i32,
        radius: i32,
        ctx: *mut c_void,
        snbt_sink: LeviRsStrSink,
    ),
    /// Send a message of a specific `TextPacketType` (see MessageType) to one
    /// player. Out-of-range type falls back to Raw.
    pub player_send_message_typed:
        unsafe extern "C" fn(sel: LeviRsPlayerSel, msg: LeviRsStr, type_: i32) -> bool,
    // Future additive fields: append here only.
}

/// Filled in by the Rust mod inside `levi_rs_main`. Mirrors `LeviRsModVTable`.
#[repr(C)]
pub struct LeviRsModVTable {
    pub abi_version: u32,
    pub instance: *mut c_void,
    pub on_enable: Option<unsafe extern "C" fn(instance: *mut c_void) -> bool>,
    pub on_disable: Option<unsafe extern "C" fn(instance: *mut c_void) -> bool>,
    pub on_unload: Option<unsafe extern "C" fn(instance: *mut c_void) -> bool>,
}

/// The single symbol every Rust mod must export (see `LEVI_RS_MAIN_SYMBOL`).
/// Mirrors `LeviRsMainFn` in the C header. Provided mainly so the loader's own
/// `GetProcAddress` cast and any mod-side signature checks share one
/// definition instead of two hand-written copies drifting apart.
pub type LeviRsMainFn = unsafe extern "C" fn(
    api: *const LeviRsApi,
    self_: LeviRsModHandle,
    out_vtable: *mut LeviRsModVTable,
) -> bool;
