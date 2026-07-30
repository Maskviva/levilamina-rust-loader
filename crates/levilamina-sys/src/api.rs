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

    // MoreDimensions (more_dimensions feature; server build only).
    // Present when the loader is built with LEVI_RS_FEATURE_MORE_DIMENSIONS.
    #[cfg(feature = "more_dimensions")]
    pub md_is_available: unsafe extern "C" fn() -> bool,
    #[cfg(feature = "more_dimensions")]
    pub md_add_simple_dimension:
        unsafe extern "C" fn(name: LeviRsStr, seed: u32, generator_type: i32) -> i32,
    /// 按维度生效的行为规则。**字段顺序即 ABI** —— 必须和 LeviRsAbi.h 里
    /// 声明的顺序逐字一致，插错位置会调到相邻的函数且不会报错。
    #[cfg(feature = "more_dimensions")]
    pub md_set_dimension_rule: unsafe extern "C" fn(dimension: i32, rule: i32, allow: bool),
    #[cfg(feature = "more_dimensions")]
    pub md_get_dimension_rule:
        unsafe extern "C" fn(dimension: i32, rule: i32, out_allow: *mut bool) -> bool,
    #[cfg(feature = "more_dimensions")]
    pub md_clear_dimension_rules: unsafe extern "C" fn(dimension: i32),
    #[cfg(feature = "more_dimensions")]
    pub md_get_dimension_id: unsafe extern "C" fn(name: LeviRsStr) -> i32,
    /// 追加于 ABI v5（additive，struct_size 变大、abi_version 不变）。
    #[cfg(feature = "more_dimensions")]
    pub md_add_plot_dimension:
        unsafe extern "C" fn(name: LeviRsStr, seed: u32, layout_snbt: LeviRsStr) -> i32,
}
