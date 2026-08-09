//! The ABI function table — mirrors `LeviRsApi` in `LeviRsAbi.h`.
//! FIELD ORDER IS THE ABI — append-only, verified by tools/check_abi_sync.py.

use core::ffi::c_void;

use crate::money::LLMoneyCallback;
use crate::types::*;

/// Function table handed to the Rust mod. Mirrors `LeviRsApi`.
#[repr(C)]
pub struct LeviRsApi {
    pub abi_version: u32,
    pub struct_size: u32,

    pub log: unsafe extern "C" fn(mod_: LeviRsModHandle, level: i32, msg: LeviRsStr),
    pub gaming_status: unsafe extern "C" fn() -> i32,
    pub schedule: unsafe extern "C" fn(cb: LeviRsTaskCb, user: *mut c_void),
    pub schedule_after: unsafe extern "C" fn(cb: LeviRsTaskCb, user: *mut c_void, delay_ms: u64),

    pub subscribe_event: unsafe extern "C" fn(
        mod_: LeviRsModHandle,
        event_id: LeviRsStr,
        priority: i32,
        cb: LeviRsEventCb,
        user: *mut c_void,
    ) -> LeviRsListenerHandle,
    pub unsubscribe_event:
        unsafe extern "C" fn(mod_: LeviRsModHandle, listener: LeviRsListenerHandle) -> bool,
    pub list_events: unsafe extern "C" fn(ctx: *mut c_void, sink: LeviRsStrSink),

    pub execute_command:
        unsafe extern "C" fn(cmd: LeviRsStr, ctx: *mut c_void, sink: LeviRsCmdOutputSink) -> bool,
    pub register_command: unsafe extern "C" fn(
        mod_: LeviRsModHandle,
        name: LeviRsStr,
        description: LeviRsStr,
        permission: i32,
        cb: LeviRsCommandCb,
        user: *mut c_void,
    ) -> bool,

    pub get_current_tick: unsafe extern "C" fn() -> u64,
    pub get_tick_delta_time: unsafe extern "C" fn() -> f64,
    pub get_player_count: unsafe extern "C" fn() -> i32,
    pub get_sim_paused: unsafe extern "C" fn() -> bool,

    pub spawn_particle: unsafe extern "C" fn(
        dimension: i32,
        effect_name: LeviRsStr,
        x: f64,
        y: f64,
        z: f64,
    ) -> bool,
    pub get_player_position: unsafe extern "C" fn(name: LeviRsStr) -> LeviRsPlayerPos,
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
    pub set_weather: unsafe extern "C" fn(weather: i32) -> bool,

    pub list_players: unsafe extern "C" fn(ctx: *mut c_void, snbt_sink: LeviRsStrSink),
    pub player_resolve: unsafe extern "C" fn(sel: LeviRsPlayerSel, out: *mut LeviRsActorId) -> bool,
    pub player_send_message: unsafe extern "C" fn(sel: LeviRsPlayerSel, msg: LeviRsStr) -> bool,
    pub player_disconnect: unsafe extern "C" fn(sel: LeviRsPlayerSel, reason: LeviRsStr) -> bool,
    pub broadcast_message: unsafe extern "C" fn(msg: LeviRsStr),
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

    pub item_get_num: unsafe extern "C" fn(item_snbt: LeviRsStr, prop: i32, out: *mut f64) -> bool,
    pub item_get_str: unsafe extern "C" fn(
        item_snbt: LeviRsStr,
        prop: i32,
        ctx: *mut c_void,
        sink: LeviRsStrSink,
    ) -> bool,
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

    pub scoreboard_op: unsafe extern "C" fn(
        op: i32,
        a: LeviRsStr,
        b: LeviRsStr,
        n: i64,
        ctx: *mut c_void,
        out: LeviRsStrSink,
    ) -> bool,
    pub form_send: unsafe extern "C" fn(
        mod_: LeviRsModHandle,
        sel: LeviRsPlayerSel,
        kind: i32,
        form_snbt: LeviRsStr,
        cb: LeviRsFormResultCb,
        user: *mut c_void,
    ) -> bool,

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
    pub update_command_soft_enum:
        unsafe extern "C" fn(name: LeviRsStr, op: i32, values_snbt: LeviRsStr) -> bool,

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
    pub game_rule_get:
        unsafe extern "C" fn(name: LeviRsStr, ctx: *mut c_void, sink: LeviRsStrSink) -> bool,
    pub game_rule_set: unsafe extern "C" fn(name: LeviRsStr, value: LeviRsStr) -> bool,
    pub server_info_str:
        unsafe extern "C" fn(prop: i32, ctx: *mut c_void, sink: LeviRsStrSink) -> bool,
    pub spawn_particle_for: unsafe extern "C" fn(
        sel: LeviRsPlayerSel,
        dimension: i32,
        effect_name: LeviRsStr,
        x: f64,
        y: f64,
        z: f64,
    ) -> bool,
    pub send_packet: unsafe extern "C" fn(
        sel: LeviRsPlayerSel,
        packet_id: i32,
        body: *const u8,
        body_len: usize,
    ) -> bool,
    pub tick_freeze: unsafe extern "C" fn(on: bool) -> bool,
    pub tick_step: unsafe extern "C" fn(n: u32) -> bool,
    pub tick_warp: unsafe extern "C" fn(factor: f64) -> bool,
    pub profile_begin: unsafe extern "C" fn(ticks: u32) -> bool,
    pub profile_take: unsafe extern "C" fn(ctx: *mut c_void, sink: LeviRsStrSink) -> bool,
    pub sim_spawn:
        unsafe extern "C" fn(name: LeviRsStr, dimension: i32, x: f64, y: f64, z: f64) -> bool,
    pub sim_do:
        unsafe extern "C" fn(sel: LeviRsPlayerSel, action: LeviRsStr, args_snbt: LeviRsStr) -> bool,
    pub sim_is: unsafe extern "C" fn(sel: LeviRsPlayerSel) -> bool,
    pub sim_list: unsafe extern "C" fn(ctx: *mut c_void, name_sink: LeviRsStrSink),
    pub villages: unsafe extern "C" fn(dimension: i32, ctx: *mut c_void, snbt_sink: LeviRsStrSink),
    #[allow(clippy::too_many_arguments)]
    pub structures_near: unsafe extern "C" fn(
        dimension: i32,
        x: i32,
        y: i32,
        z: i32,
        radius: i32,
        ctx: *mut c_void,
        snbt_sink: LeviRsStrSink,
    ),
    pub player_send_message_typed:
        unsafe extern "C" fn(sel: LeviRsPlayerSel, msg: LeviRsStr, type_: i32) -> bool,

    pub get_money: unsafe extern "C" fn(xuid: LeviRsStr) -> i64,
    pub set_money: unsafe extern "C" fn(xuid: LeviRsStr, money: i64) -> bool,
    pub add_money: unsafe extern "C" fn(xuid: LeviRsStr, money: i64) -> bool,
    pub reduce_money: unsafe extern "C" fn(xuid: LeviRsStr, money: i64) -> bool,
    pub trans_money:
        unsafe extern "C" fn(from: LeviRsStr, to: LeviRsStr, val: i64, note: LeviRsStr) -> bool,
    pub money_get_hist:
        unsafe extern "C" fn(xuid: LeviRsStr, timediff: i32, ctx: *mut c_void, sink: LeviRsStrSink),
    pub money_clear_hist: unsafe extern "C" fn(difftime: i32),
    pub money_listen_before_event: unsafe extern "C" fn(callback: LLMoneyCallback),
    pub money_listen_after_event: unsafe extern "C" fn(callback: LLMoneyCallback),
    pub money_ranking: unsafe extern "C" fn(num: u16, ctx: *mut c_void, sink: LeviRsStrSink),

    // ABI v5 Additive — API gap fill (struct_size-gated). Append only.
    pub player_get_carried_item:
        unsafe extern "C" fn(sel: LeviRsPlayerSel, ctx: *mut c_void, sink: LeviRsStrSink) -> bool,
    pub player_get_item: unsafe extern "C" fn(
        sel: LeviRsPlayerSel,
        slot: i32,
        ctx: *mut c_void,
        sink: LeviRsStrSink,
    ) -> bool,
    pub player_set_item:
        unsafe extern "C" fn(sel: LeviRsPlayerSel, slot: i32, item_snbt: LeviRsStr) -> bool,
    pub player_get_equipment:
        unsafe extern "C" fn(sel: LeviRsPlayerSel, ctx: *mut c_void, sink: LeviRsStrSink) -> bool,
    pub player_get_cooldown:
        unsafe extern "C" fn(sel: LeviRsPlayerSel, item_name: LeviRsStr) -> i32,
    pub player_start_cooldown:
        unsafe extern "C" fn(sel: LeviRsPlayerSel, item_name: LeviRsStr, ticks: i32) -> bool,
    pub player_get_network_status:
        unsafe extern "C" fn(sel: LeviRsPlayerSel, ctx: *mut c_void, sink: LeviRsStrSink) -> bool,
    pub actor_get_vehicle: unsafe extern "C" fn(id: LeviRsActorId, out: *mut LeviRsActorId) -> bool,
    pub actor_get_first_passenger:
        unsafe extern "C" fn(id: LeviRsActorId, out: *mut LeviRsActorId) -> bool,
    pub actor_get_owner: unsafe extern "C" fn(id: LeviRsActorId, out: *mut LeviRsActorId) -> bool,
    pub actor_get_target: unsafe extern "C" fn(id: LeviRsActorId, out: *mut LeviRsActorId) -> bool,
    pub actor_get_equipped_item: unsafe extern "C" fn(
        id: LeviRsActorId,
        slot: i32,
        ctx: *mut c_void,
        sink: LeviRsStrSink,
    ) -> bool,
    pub actor_set_equipped_item:
        unsafe extern "C" fn(id: LeviRsActorId, slot: i32, item_snbt: LeviRsStr) -> bool,
    pub actor_get_effects:
        unsafe extern "C" fn(id: LeviRsActorId, ctx: *mut c_void, sink: LeviRsStrSink) -> bool,
    pub actor_get_status_flag: unsafe extern "C" fn(id: LeviRsActorId, flag_index: i32) -> bool,
    pub actor_set_status_flag:
        unsafe extern "C" fn(id: LeviRsActorId, flag_index: i32, value: bool) -> bool,
    pub actor_trace_ray: unsafe extern "C" fn(
        id: LeviRsActorId,
        max_dist: f32,
        include_actors: bool,
        include_blocks: bool,
        ctx: *mut c_void,
        sink: LeviRsStrSink,
    ) -> bool,
    pub actor_distance_to:
        unsafe extern "C" fn(id: LeviRsActorId, other: LeviRsActorId, out: *mut f64) -> bool,
    pub actor_get_aabb:
        unsafe extern "C" fn(id: LeviRsActorId, ctx: *mut c_void, sink: LeviRsStrSink) -> bool,
    pub actor_clone: unsafe extern "C" fn(
        id: LeviRsActorId,
        dim: i32,
        x: f64,
        y: f64,
        z: f64,
        out: *mut LeviRsActorId,
    ) -> bool,
    pub block_get_state: unsafe extern "C" fn(
        dim: i32,
        x: i32,
        y: i32,
        z: i32,
        state_name: LeviRsStr,
        ctx: *mut c_void,
        sink: LeviRsStrSink,
    ) -> bool,
    pub block_set_state: unsafe extern "C" fn(
        dim: i32,
        x: i32,
        y: i32,
        z: i32,
        state_name: LeviRsStr,
        value: LeviRsStr,
    ) -> bool,
    pub block_get_collision_shape: unsafe extern "C" fn(
        dim: i32,
        x: i32,
        y: i32,
        z: i32,
        ctx: *mut c_void,
        sink: LeviRsStrSink,
    ) -> bool,
    pub item_get_enchants:
        unsafe extern "C" fn(item_snbt: LeviRsStr, ctx: *mut c_void, sink: LeviRsStrSink) -> bool,
    pub item_set_enchants: unsafe extern "C" fn(
        item_snbt: LeviRsStr,
        enchants_snbt: LeviRsStr,
        ctx: *mut c_void,
        out: LeviRsStrSink,
    ) -> bool,
    pub item_matches: unsafe extern "C" fn(a: LeviRsStr, b: LeviRsStr) -> bool,
    pub item_get_user_data:
        unsafe extern "C" fn(item_snbt: LeviRsStr, ctx: *mut c_void, sink: LeviRsStrSink) -> bool,
    pub level_get_biome: unsafe extern "C" fn(
        dim: i32,
        x: i32,
        y: i32,
        z: i32,
        ctx: *mut c_void,
        sink: LeviRsStrSink,
    ) -> bool,
    pub level_get_default_spawn:
        unsafe extern "C" fn(x: *mut i32, y: *mut i32, z: *mut i32) -> bool,
    pub level_set_default_spawn: unsafe extern "C" fn(x: i32, y: i32, z: i32) -> bool,
    pub level_save: unsafe extern "C" fn() -> bool,
    pub level_get_sleep_status: unsafe extern "C" fn(ctx: *mut c_void, sink: LeviRsStrSink) -> bool,
    pub level_update_weather: unsafe extern "C" fn(
        rain_level: f32,
        rain_time: i32,
        lightning_level: f32,
        lightning_time: i32,
    ) -> bool,
    pub level_find_path: unsafe extern "C" fn(
        id: LeviRsActorId,
        x: i32,
        y: i32,
        z: i32,
        ctx: *mut c_void,
        sink: LeviRsStrSink,
    ) -> bool,
    // ── Packet interception (v5 additive, struct_size-gated) ──
    // Raw wire-format hooks in both directions. Delivery unit is one packet;
    // the loader decodes/re-encodes the varint header, so callbacks see and
    // return a bare BODY. See `LeviRsAbi.h` for the full contract, and
    // `levilamina::packet` for the safe wrapper.
    pub packet_hook_register: unsafe extern "C" fn(
        mod_: LeviRsModHandle,
        dir_mask: i32,
        cb: LeviRsPacketCb,
        user: *mut c_void,
    ) -> LeviRsPacketHookHandle,
    pub packet_hook_unregister:
        unsafe extern "C" fn(mod_: LeviRsModHandle, handle: LeviRsPacketHookHandle) -> bool,
    pub packet_conn_hook_register: unsafe extern "C" fn(
        mod_: LeviRsModHandle,
        cb: LeviRsConnCb,
        user: *mut c_void,
    ) -> LeviRsPacketHookHandle,
    pub packet_conn_hook_unregister:
        unsafe extern "C" fn(mod_: LeviRsModHandle, handle: LeviRsPacketHookHandle) -> bool,
    // Future additive fields: append here only.

    // Client-only (client feature). Server struct_size stops before this block.
    #[cfg(feature = "client")]
    pub client_get_local_player:
        unsafe extern "C" fn(ctx: *mut c_void, sink: LeviRsStrSink) -> bool,
    #[cfg(feature = "client")]
    pub client_is_in_level: unsafe extern "C" fn() -> bool,
    #[cfg(feature = "client")]
    pub client_get_screen_name: unsafe extern "C" fn(ctx: *mut c_void, sink: LeviRsStrSink) -> bool,
    #[cfg(feature = "client")]
    #[allow(clippy::too_many_arguments)]
    pub client_register_key: unsafe extern "C" fn(
        mod_: LeviRsModHandle,
        name: LeviRsStr,
        key_codes: *const i32,
        key_count: i32,
        allow_remap: bool,
        down_cb: LeviRsKeyCb,
        up_cb: LeviRsKeyCb,
        user: *mut c_void,
    ) -> LeviRsKeyHandle,
    #[cfg(feature = "client")]
    pub client_unregister_key: unsafe extern "C" fn(handle: LeviRsKeyHandle) -> bool,
    #[cfg(feature = "client")]
    pub client_get_key_codes: unsafe extern "C" fn(
        handle: LeviRsKeyHandle,
        ctx: *mut c_void,
        sink: LeviRsStrSink,
    ) -> bool,

    // MoreDimensions — present in every SERVER build of the loader.
    //
    // ⚠ 这里的 gate 必须是 `not(feature = "client")`，**不能**是
    // `feature = "more_dimensions"`。C++ 侧 xmake.lua 里 `more_dims = not is_client`，
    // 也就是说服务器构建**无条件**把 md 块编译进去，它根本不是可选项。
    // 如果这里跟着 `more_dimensions` feature 走，一个没开该 feature 的服务器端
    // mod 的结构体就会在 md 块处截断，于是它读到的「结构体末尾之后的字段」
    // 实际落在 md_is_available 上 —— 静默错位、无任何诊断。
    //
    // `more_dimensions` feature 仍然有意义：它 gate 的是 crate 里的**安全封装**
    // （crates/levilamina/src/more_dimensions.rs），而不是这张裸函数表的布局。
    #[cfg(not(feature = "client"))]
    pub md_is_available: unsafe extern "C" fn() -> bool,
    #[cfg(not(feature = "client"))]
    pub md_add_simple_dimension:
        unsafe extern "C" fn(name: LeviRsStr, seed: u32, generator_type: i32) -> i32,
    /// 按维度生效的行为规则。**字段顺序即 ABI** —— 必须和 LeviRsAbi.h 里
    /// 声明的顺序逐字一致，插错位置会调到相邻的函数且不会报错。
    #[cfg(not(feature = "client"))]
    pub md_set_dimension_rule: unsafe extern "C" fn(dimension: i32, rule: i32, allow: bool),
    #[cfg(not(feature = "client"))]
    pub md_get_dimension_rule:
        unsafe extern "C" fn(dimension: i32, rule: i32, out_allow: *mut bool) -> bool,
    #[cfg(not(feature = "client"))]
    pub md_clear_dimension_rules: unsafe extern "C" fn(dimension: i32),
    #[cfg(not(feature = "client"))]
    pub md_get_dimension_id: unsafe extern "C" fn(name: LeviRsStr) -> i32,
    /// 追加于 ABI v5（additive，struct_size 变大、abi_version 不变）。
    #[cfg(not(feature = "client"))]
    pub md_add_plot_dimension:
        unsafe extern "C" fn(name: LeviRsStr, seed: u32, layout_snbt: LeviRsStr) -> i32,

    // ── Common additive tail ────────────────────────────────────────────
    // 两个条件块都已闭合，这里的字段在 server / client 两种构建下偏移一致，
    // 是**唯一**可以安全追加新槽位的位置。今后新增一律加在这个块的末尾。
    //
    // 带 mod 归属的调度：旧的 `schedule` / `schedule_after` 签名里没有 mod
    // handle，所以在原理上就没法做成卸载安全的 —— mod 卸载后定时器醒来，
    // 函数指针指向已释放的 dll。下面这组把任务登记到 loader 自己的票据表里，
    // 卸载时统一丢弃。想活过 `/llr unload` / `/llr reload` 的 mod 必须走这组。
    /// 立即在服务器（或客户端）线程上跑一次，归属 `mod`。返回任务 id（>0），
    /// 0 表示被拒绝。线程安全。
    pub schedule_for:
        unsafe extern "C" fn(m: LeviRsModHandle, cb: LeviRsTaskCb, user: *mut c_void) -> u64,
    /// 同上，延迟 `delay_ms` 毫秒。返回任务 id（>0），0 表示被拒绝。
    pub schedule_after_for: unsafe extern "C" fn(
        m: LeviRsModHandle,
        cb: LeviRsTaskCb,
        user: *mut c_void,
        delay_ms: u64,
    ) -> u64,
    /// 取消本 mod 尚未执行的任务。只能取消自己的。
    pub schedule_cancel: unsafe extern "C" fn(m: LeviRsModHandle, task_id: u64) -> bool,
    /// 本 mod 还有多少任务在排队。用于在 on_disable / on_unload 里自检。
    pub schedule_pending_count: unsafe extern "C" fn(m: LeviRsModHandle) -> u32,
    /// 把玩家自己的容器重新推给客户端。容器写入只改服务端副本、不发包，
    /// 所以批量改完背包（比如跨维度传送换装备）必须调一次，否则客户端还渲染
    /// 旧物品，要玩家手点一下槽位才会刷新。整包推送，别在循环里逐槽调用。
    pub container_refresh: unsafe extern "C" fn(r: LeviRsContainerRef) -> bool,
    /// 原生 `SetTitlePacket`。**不要再用 `PACT_SET_TITLE`** —— 那条路是
    /// `runConsoleCommand("title \"名字\" title 文本")`：文本没转义（地皮名里
    /// 有引号就把命令截断）、`title` 的文本参数是 `message` 会展开选择器
    /// （地皮名叫 `@e` 就是命令注入）、而且没法设淡入/停留/淡出。
    ///
    /// `type`：0 Clear · 1 Reset · 2 Title · 3 Subtitle · 4 Actionbar · 5 Times。
    /// 6..8（TextObject 变体）需要 `ResolvedTextObject`，一律拒绝。
    ///
    /// 时长单位是**刻**。三个要么全 >= 0、要么全传 -1（沿用客户端当前值），
    /// 混着传直接拒绝 —— 半套时长没有合理解释，那是调用点的 bug。
    pub player_send_title: unsafe extern "C" fn(
        sel: LeviRsPlayerSel,
        kind: i32,
        text: LeviRsStr,
        fade_in_ticks: i32,
        stay_ticks: i32,
        fade_out_ticks: i32,
    ) -> bool,

    // ── 跨 mod 事件总线 ──
    // mod 之间不能直接互传函数指针：`RustModManager::unload` 会 FreeLibrary，
    // 订阅方一卸载，发布方手里就是个指向已释放 dylib 的指针。所以订阅表由
    // loader 持有，走和 Forms / scheduler 同一套 weak_ptr + 票据。
    // loader **不解析** payload，那是两个 mod 之间的约定。
    //
    // 一个 mod 收不到自己发的事件（自发自收是唯一一种深度限制分辨不出来的循环）；
    // 跨 mod 的 A→B→A 由嵌套深度上限兜住。
    /// 订阅。返回订阅 id（>0），0 = 失败。mod 卸载时自动清掉。
    pub bus_subscribe: unsafe extern "C" fn(
        m: LeviRsModHandle,
        topic: LeviRsStr,
        cb: LeviRsBusCb,
        user: *mut c_void,
    ) -> u64,
    /// 退订。只能退自己的。可以在回调里调（包括退掉自己）。
    pub bus_unsubscribe: unsafe extern "C" fn(m: LeviRsModHandle, sub_id: u64) -> bool,
    /// 发布给**其他** mod 的订阅者，返回实际跑了几个订阅者。返回值被忽略。
    pub bus_publish:
        unsafe extern "C" fn(m: LeviRsModHandle, topic: LeviRsStr, payload: LeviRsStr) -> u32,
    /// 同上，但收集否决位：任一订阅者返回 true 则整体返回 true。
    /// **不短路** —— 观察型订阅者必须看到一致的事件流。
    pub bus_publish_vetoable: unsafe extern "C" fn(
        m: LeviRsModHandle,
        topic: LeviRsStr,
        payload: LeviRsStr,
        out_delivered: *mut u32,
    ) -> bool,
    /// 某个 topic 当前有多少订阅者（跨全部 mod）。用来跳过「拼一个没人看的
    /// payload」的开销。
    pub bus_subscriber_count: unsafe extern "C" fn(topic: LeviRsStr) -> u32,

    // ── 地皮边界约束（LEVI_RS_DIMRULE_PISTON_CROSS_PLOT / _ENTITY_CROSS_PLOT）──
    //
    // 这两条规则要回答「这两列方块算不算同一块地皮」，而答案需要网格几何 +
    // 合并标记。提问发生在 `PistonBlockActor::_checkAttachedBlocks` 和
    // `Actor::move` 里 —— 引擎的 tick 路径，每秒几百次 —— 所以数据推过去一次、
    // 在 C++ 侧原生读，而不是每次跨 FFI 回来问。
    //
    // 注意这三个虽然叫 md_*，却在**公共尾部**而不是 md 条件块里：放进条件块会把
    // 尾部所有字段的偏移推走。客户端构建有桩（ClientStubs.cpp），是空实现。
    /// 注册 / 更新一个维度的地皮网格。`plot_size <= 0` 等价于清除。
    /// 数值在 loader 侧夹一遍 —— `cell = plot_size + road_width` 是取模的除数。
    pub md_set_plot_grid: unsafe extern "C" fn(dimension: i32, plot_size: i32, road_width: i32),
    /// 清掉一个维度的网格和合并表（世界被删、或者不再用地皮模型）。
    pub md_clear_plot_grid: unsafe extern "C" fn(dimension: i32),
    /// **整表替换**合并标记。`entries` 是 `count` 组 `(x, z, mask)`，
    /// mask = 1 北 | 2 东 | 4 南 | 8 西，和插件侧 `Plot::merged` 的下标一致。
    /// 只需要传有标记的地皮。先 `md_set_plot_grid`，否则这次推送会被丢弃。
    pub md_set_plot_merges: unsafe extern "C" fn(dimension: i32, entries: *const i32, count: i32),

    // ── 跨 mod 服务注册（查询式调用）──
    //
    // 和总线是两回事：总线是单向广播（任意多订阅者、没有返回值、顺序不能有意义），
    // 这里是请求/应答（**恰好一个**提供方、返回值就是全部意义）。
    // 所以注册是**独占**的：第二个注册同名服务的会被拒绝并记日志。静默让后来者
    // 覆盖意味着答案取决于 mod 加载顺序 —— 那是没人控制得了的，而且装一个无关的
    // mod 就会变。
    /// 注册。返回注册 id（>0）；0 = 名字被占 / 空 / 过长、回调为空、mod 未知。
    pub service_register: unsafe extern "C" fn(
        m: LeviRsModHandle,
        name: LeviRsStr,
        cb: LeviRsServiceCb,
        user: *mut c_void,
    ) -> u64,
    /// 注销。只能注销自己的。
    pub service_unregister: unsafe extern "C" fn(m: LeviRsModHandle, reg_id: u64) -> bool,
    /// 调用。返回 `LEVI_RS_SERVICE_*`，应答走 `reply` sink。
    /// **不能调自己的服务** —— 那有直接函数调用可用。
    pub service_call: unsafe extern "C" fn(
        m: LeviRsModHandle,
        name: LeviRsStr,
        request: LeviRsStr,
        ctx: *mut c_void,
        reply: LeviRsStrSink,
    ) -> i32,
    /// 全部已注册服务，JSON 数组 `[{"name":…,"mod":…}]`。
    pub service_list: unsafe extern "C" fn(ctx: *mut c_void, sink: LeviRsStrSink),

    // ── 批量世界编辑（Edit.cpp）────────────────────────────────────────
    //
    // 这一组存在的理由：在它之前，写方块的唯一入口 `set_block` 底层是
    // `execute in <dim> run setblock …` 一条控制台命令。于是方块状态只能靠拼
    // 命令字符串（翻错一处 = 那一格静默不变）、方块实体写不回去、实体放不回去。
    // 引擎侧这三件事都有现成入口，这里把它们接出来。
    //
    // update_flags 位掩码：1=通知邻居 2=同步客户端 3=两者（等价 /setblock）
    // 0=都不要（批量填充最快，但填完要自己补刷新）。
    /// 用序列化 NBT（`{name,states,version}`，即 `get_block` 给的那个）写方块。
    pub edit_set_block_nbt: unsafe extern "C" fn(
        dim: i32,
        x: i32,
        y: i32,
        z: i32,
        snbt: LeviRsStr,
        update_flags: i32,
    ) -> bool,
    /// 用方块名 + 可选的部分状态写方块。`states_snbt` 为空串 = 全默认。
    /// 版本号由 loader 侧从默认状态取，调用方不用（也不该）自己填。
    pub edit_set_block_states: unsafe extern "C" fn(
        dim: i32,
        x: i32,
        y: i32,
        z: i32,
        name: LeviRsStr,
        states_snbt: LeviRsStr,
        update_flags: i32,
    ) -> bool,
    /// 把方块实体 NBT 写回去（`BlockActor::load`）。要求那一格已经是对应方块。
    pub edit_set_block_entity:
        unsafe extern "C" fn(dim: i32, x: i32, y: i32, z: i32, snbt: LeviRsStr) -> bool,
    /// 按完整实体 NBT 生成实体（`actor_snapshot` 的逆操作）。
    /// `use_pos` 为真时用 (x,y,z) 覆盖 NBT 里的 Pos。UniqueID 由引擎重新分配。
    pub edit_spawn_entity_nbt: unsafe extern "C" fn(
        dim: i32,
        snbt: LeviRsStr,
        use_pos: bool,
        x: f64,
        y: f64,
        z: f64,
        out: *mut LeviRsActorId,
    ) -> bool,
    /// 射线投射，**带方块坐标和命中面**：
    /// `{type,block:[x,y,z],facing,pos:[x,y,z],entity}`。
    pub edit_trace_ray: unsafe extern "C" fn(
        id: LeviRsActorId,
        max_dist: f32,
        include_actors: bool,
        include_blocks: bool,
        ctx: *mut c_void,
        sink: LeviRsStrSink,
    ) -> bool,
}
