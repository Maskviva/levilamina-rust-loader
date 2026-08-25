/**
 * bridge/Api.h — internal declarations for every api_* entry point.
 *
 * Each domain file (LogScheduler / Events / Commands / …) implements its
 * slice; ApiTable.cpp is the only file that cares about field ORDER and
 * assembles the LeviRsApi table from these. Adding a new API = declare it
 * here, implement it in its domain file, append it to the table.
 */
#pragma once

#include <string_view>
#include <vector>
#include "LeviRsAbi.h"

// BDS 的这两个在全局命名空间，不是 levi_rs:: 下的。
class Block;
class BlockChangeContext;

namespace levi_rs
{
    class RustMod;


    namespace bridge
    {
        /* ── LogScheduler.cpp ── */
        void api_log(LeviRsModHandle mod, int32_t level, LeviRsStr msg);
        int32_t api_gaming_status();
        void api_schedule(LeviRsTaskCb cb, void* user);
        void api_schedule_after(LeviRsTaskCb cb, void* user, uint64_t delayMs);
        /* Mod-scoped replacements: tasks are dropped if their owner unloads. */
        uint64_t api_schedule_for(LeviRsModHandle mod, LeviRsTaskCb cb, void* user);
        uint64_t api_schedule_after_for(LeviRsModHandle mod, LeviRsTaskCb cb, void* user, uint64_t delayMs);
        bool api_schedule_cancel(LeviRsModHandle mod, uint64_t taskId);
        uint32_t api_schedule_pending_count(LeviRsModHandle mod);
        /** Drop every task still queued for `mod`. Called from onRustModGone. */
        void schedulerOnRustModGone(RustMod* mod);
        uint64_t api_get_current_tick();
        double api_get_tick_delta_time();
        int32_t api_get_player_count();
        bool api_get_sim_paused();

        /* ── Events.cpp ── */
        LeviRsListenerHandle
        api_subscribe_event(LeviRsModHandle mod, LeviRsStr eventId, int32_t priority, LeviRsEventCb cb, void* user);
        bool api_unsubscribe_event(LeviRsModHandle mod, LeviRsListenerHandle handle);
        void api_list_events(void* ctx, LeviRsStrSink sink);

        /* ── Commands.cpp ── */
        bool api_execute_command(LeviRsStr cmd, void* ctx, LeviRsCmdOutputSink sink);
        bool api_register_command(
            LeviRsModHandle mod,
            LeviRsStr name,
            LeviRsStr description,
            int32_t permission,
            LeviRsCommandCb cb,
            void* user
        );
        bool api_register_command_ex(
            LeviRsModHandle mod,
            LeviRsStr name,
            LeviRsStr description,
            int32_t permission,
            LeviRsStr overloadsSnbt,
            LeviRsCommandCb cb,
            void* user
        );
        bool api_register_command_enum(LeviRsStr name, LeviRsStr valuesSnbt);
        bool api_register_command_soft_enum(LeviRsStr name, LeviRsStr valuesSnbt);
        bool api_update_command_soft_enum(LeviRsStr name, int32_t op, LeviRsStr valuesSnbt);
        void commandsOnRustModGone(RustMod* mod);

        /* ── Server.cpp ── */
        bool api_get_time(int64_t* out);
        bool api_set_time(int64_t t);
        bool api_set_weather(int32_t weather);
        bool api_get_difficulty(int32_t* out);
        bool api_set_difficulty(int32_t d);
        bool api_get_seed(int64_t* out);
        bool api_game_rule_get(LeviRsStr name, void* ctx, LeviRsStrSink sink);
        bool api_game_rule_set(LeviRsStr name, LeviRsStr value);
        bool api_server_info_str(int32_t prop, void* ctx, LeviRsStrSink sink);
        bool api_spawn_particle_for(
            LeviRsPlayerSel sel, int32_t dimension, LeviRsStr effectName, double x, double y, double z);
        bool api_send_packet(LeviRsPlayerSel sel, int32_t packetId, uint8_t const* body, size_t bodyLen);

        /* ── PacketHooks.cpp — raw wire-format interception ── */
        LeviRsPacketHookHandle
        api_packet_hook_register(LeviRsModHandle mod, int32_t dirMask, LeviRsPacketCb cb, void* user);
        bool api_packet_hook_unregister(LeviRsModHandle mod, LeviRsPacketHookHandle handle);
        LeviRsPacketHookHandle api_packet_conn_hook_register(LeviRsModHandle mod, LeviRsConnCb cb, void* user);
        bool api_packet_conn_hook_unregister(LeviRsModHandle mod, LeviRsPacketHookHandle handle);
        /** Drop every interceptor owned by a mod that is going away. */
        void packetHooksOnRustModGone(RustMod* mod);

        /* ── hooks/TickControl.cpp ── */
        bool api_tick_freeze(bool on);
        bool api_tick_step(uint32_t n);
        bool api_tick_warp(double factor);

        /* ── hooks/Profiler.cpp ── */
        bool api_profile_begin(uint32_t ticks);
        bool api_profile_take(void* ctx, LeviRsStrSink sink);

        /* ── hooks/HookEvents.cpp ── */
        /* bridge-hook events (not ABI slots): plumbing used by Events.cpp and
         * RustModManager to route synthetic hook-backed event ids. Individual
         * events self-register from hooks/{Hopper,Destroy,…}Events.cpp. */
        LeviRsListenerHandle
        hookEventSubscribe(RustMod* mod, std::string_view eventId, LeviRsEventCb cb, void* user);
        bool hookEventUnsubscribe(RustMod* mod, LeviRsListenerHandle handle);
        void hookEventDropMod(RustMod* mod);
        void hookEventList(void* ctx, LeviRsStrSink sink);

        /* ── SimPlayer.cpp ── */
        bool api_sim_spawn(LeviRsStr name, int32_t dimension, double x, double y, double z);
        bool api_sim_do(LeviRsPlayerSel sel, LeviRsStr action, LeviRsStr args_snbt);
        bool api_sim_is(LeviRsPlayerSel sel);
        void api_sim_list(void* ctx, LeviRsStrSink name_sink);

        /* ── WorldInfo.cpp ── */
        void api_villages(int32_t dimension, void* ctx, LeviRsStrSink snbt_sink);
        void api_structures_near(
            int32_t dimension, int32_t x, int32_t y, int32_t z, int32_t radius, void* ctx,
            LeviRsStrSink snbt_sink);

        /* ── World.cpp ── */
        bool api_spawn_particle(int32_t dimension, LeviRsStr effectName, double x, double y, double z);
        LeviRsPlayerPos api_get_player_position(LeviRsStr name);
        bool api_scan_region(
            int32_t dimension,
            int32_t x1,
            int32_t y1,
            int32_t z1,
            int32_t x2,
            int32_t y2,
            int32_t z2,
            void* ctx,
            LeviRsBlockSink blocksSink,
            LeviRsEntitySink entitiesSink
        );
        bool api_get_block(int32_t dim, int32_t x, int32_t y, int32_t z, void* ctx, LeviRsBlockSink sink);
        bool api_set_block(int32_t dim, int32_t x, int32_t y, int32_t z, LeviRsStr blockSpec);
        bool api_block_get_num(int32_t dim, int32_t x, int32_t y, int32_t z, int32_t prop, double* out);
        bool api_block_get_str(int32_t dim, int32_t x, int32_t y, int32_t z, int32_t prop, void* ctx,
                               LeviRsStrSink sink);
        bool api_block_action(
            int32_t dim,
            int32_t x,
            int32_t y,
            int32_t z,
            int32_t action,
            LeviRsStr sarg,
            void* ctx,
            LeviRsStrSink out
        );
        bool api_block_entity_snbt(int32_t dim, int32_t x, int32_t y, int32_t z, void* ctx, LeviRsStrSink sink);
        bool api_explode(
            int32_t dim,
            double x,
            double y,
            double z,
            float radius,
            float maxResistance,
            LeviRsActorId source,
            bool fire,
            bool breaksBlocks,
            bool allowUnderwater
        );

        /* ── Players.cpp ── */
        void api_list_players(void* ctx, LeviRsStrSink snbtSink);
        bool api_player_resolve(LeviRsPlayerSel sel, LeviRsActorId* out);
        bool api_player_send_message(LeviRsPlayerSel sel, LeviRsStr msg);
        bool api_player_send_message_typed(LeviRsPlayerSel sel, LeviRsStr msg, int32_t type);
        bool api_player_disconnect(LeviRsPlayerSel sel, LeviRsStr reason);
        void api_broadcast_message(LeviRsStr msg);
        bool api_player_set_gamemode(LeviRsPlayerSel sel, int32_t mode);
        bool api_player_teleport(LeviRsPlayerSel sel, int32_t dim, double x, double y, double z);
        bool api_player_get_num(LeviRsPlayerSel sel, int32_t prop, double* out);
        bool api_player_get_str(LeviRsPlayerSel sel, int32_t prop, void* ctx, LeviRsStrSink sink);
        bool api_player_set_num(LeviRsPlayerSel sel, int32_t prop, double v);
        bool api_player_action(
            LeviRsPlayerSel sel,
            int32_t action,
            LeviRsStr sarg,
            double a,
            double b,
            double c,
            void* ctx,
            LeviRsStrSink out
        );

        /* ── Actors.cpp ── */
        void api_list_actors(int32_t dim, void* ctx, LeviRsActorSink sink);
        bool api_actor_snapshot(LeviRsActorId id, void* ctx, LeviRsStrSink snbtSink);
        bool api_actor_get_num(LeviRsActorId id, int32_t prop, double* out);
        bool api_actor_get_str(LeviRsActorId id, int32_t prop, void* ctx, LeviRsStrSink sink);
        bool api_actor_action(
            LeviRsActorId id,
            int32_t action,
            LeviRsStr sarg,
            double a,
            double b,
            double c,
            void* ctx,
            LeviRsStrSink out
        );
        bool api_spawn_mob(int32_t dim, LeviRsStr typeName, double x, double y, double z, LeviRsActorId* out);

        /* ── Items.cpp ── */
        bool api_item_get_num(LeviRsStr itemSnbt, int32_t prop, double* out);
        bool api_item_get_str(LeviRsStr itemSnbt, int32_t prop, void* ctx, LeviRsStrSink sink);
        bool api_item_transform(LeviRsStr itemSnbt, int32_t op, LeviRsStr sarg, double narg, void* ctx,
                                LeviRsStrSink out);

        /* ── Containers.cpp ── */
        bool api_container_size(LeviRsContainerRef ref, int32_t* out);
        bool api_container_get_item(LeviRsContainerRef ref, int32_t slot, void* ctx, LeviRsStrSink sink);
        bool api_container_set_item(LeviRsContainerRef ref, int32_t slot, LeviRsStr itemSnbt);
        bool api_container_add_item(LeviRsContainerRef ref, LeviRsStr itemSnbt);
        bool api_container_remove_item(LeviRsContainerRef ref, int32_t slot, int32_t count);
        bool api_container_clear(LeviRsContainerRef ref);
        /** Resend a player-owned container to its owner (ABI additive tail). */
        bool api_container_refresh(LeviRsContainerRef ref);

        /* ── Bus.cpp — cross-mod event bus ── */
        uint64_t api_bus_subscribe(LeviRsModHandle mod, LeviRsStr topic, LeviRsBusCb cb, void* user);
        bool api_bus_unsubscribe(LeviRsModHandle mod, uint64_t subId);
        uint32_t api_bus_publish(LeviRsModHandle mod, LeviRsStr topic, LeviRsStr payload);
        bool api_bus_publish_vetoable(
            LeviRsModHandle mod, LeviRsStr topic, LeviRsStr payload, uint32_t* outDelivered);
        uint32_t api_bus_subscriber_count(LeviRsStr topic);
        /** Drop every subscription owned by a mod that is going away. */
        void busOnRustModGone(RustMod* mod);

        /* ── Services.cpp — cross-mod service registry (query-style calls) ── */
        uint64_t api_service_register(
            LeviRsModHandle mod, LeviRsStr name, LeviRsServiceCb cb, void* user);
        bool api_service_unregister(LeviRsModHandle mod, uint64_t regId);
        int32_t api_service_call(
            LeviRsModHandle mod, LeviRsStr name, LeviRsStr request, void* ctx, LeviRsStrSink reply);
        void api_service_list(void* ctx, LeviRsStrSink sink);
        /** Drop every service provided by a mod that is going away. */
        void servicesOnRustModGone(RustMod* mod);

        /* ── Lane.cpp — Rust 高速公路（Rust-to-Rust fast lane）── */
        uint64_t api_lane_publish(LeviRsModHandle mod, LeviRsStr name, LeviRsLaneDesc const* desc);
        bool api_lane_unpublish(LeviRsModHandle mod, uint64_t pubId);
        int32_t api_lane_acquire(
            LeviRsModHandle mod, LeviRsStr name, uint64_t wantFingerprint, LeviRsLaneRef* out);
        bool api_lane_release(LeviRsModHandle mod, uint64_t leaseId);
        void api_lane_list(void* ctx, LeviRsStrSink sink);
        /** 归还它持有的租约，撤销它发布的车道。必须在 FreeLibrary 之前跑。 */
        void laneOnRustModGone(RustMod* mod);
        /** 该 mod 是否有车道正停在调用中；返回车道名，没有则 nullptr。
         *  卸载前必须查，见 Lane.cpp 里的说明。 */
        char const* laneModBusyName(RustMod* mod);

        /* MoreDimensionsBridge.cpp — plot-boundary confinement data.
         *
         * These live in `levi_rs::bridge` rather than `more_dimensions::bridge`
         * with the rest of the md_* family for one reason: their ABI slots sit
         * in the **common additive tail**, so ApiTable.cpp names them on the
         * client build too. Same arrangement as api_player_send_title —
         * server definition in a server-only TU, client stub in
         * ClientStubs.cpp. Putting the slots inside the md #ifdef block instead
         * would have shifted every field of the tail below it. */
        /** 枚举已注册的自定义维度。槽位同样在公共追加尾部，所以声明在这里
         *  而不是 `more_dimensions::bridge` —— 客户端构建也要有这个符号。 */
        void api_md_list_dimensions(void* ctx, LeviRsStrSink sink);
        void api_md_set_plot_grid(int32_t dimension, int32_t plotSize, int32_t roadWidth);
        void api_md_clear_plot_grid(int32_t dimension);
        void api_md_set_plot_merges(int32_t dimension, int32_t const* entries, int32_t count);

        /* Packets.cpp — native SetTitlePacket (replaces the /title console path) */
        bool api_player_send_title(
            LeviRsPlayerSel sel,
            int32_t type,
            LeviRsStr text,
            int32_t fadeInTicks,
            int32_t stayTicks,
            int32_t fadeOutTicks
        );

        /* ── ScoreboardApi.cpp ── */
        bool api_scoreboard_op(int32_t op, LeviRsStr a, LeviRsStr b, int64_t n, void* ctx, LeviRsStrSink out);

        /* ── Forms.cpp ── */
        bool api_form_send(
            LeviRsModHandle mod,
            LeviRsPlayerSel sel,
            int32_t kind,
            LeviRsStr formSnbt,
            LeviRsFormResultCb cb,
            void* user
        );
        void formsOnRustModGone(RustMod* mod);

        /* ── NbtApi.cpp ── */
        bool api_nbt_snbt_to_binary(LeviRsStr snbt, int32_t fmt, void* ctx, LeviRsBytesSink sink);
        bool api_nbt_binary_to_snbt(uint8_t const* data, size_t len, int32_t fmt, void* ctx, LeviRsStrSink sink);

        /* ── KvDbApi.cpp ── */
        LeviRsKvDbHandle api_kvdb_open(LeviRsModHandle mod, LeviRsStr path, bool createIfMissing);
        void api_kvdb_close(LeviRsKvDbHandle h);
        bool api_kvdb_get(LeviRsKvDbHandle h, LeviRsStr key, void* ctx, LeviRsStrSink sink);
        bool api_kvdb_set(LeviRsKvDbHandle h, LeviRsStr key, LeviRsStr value);
        bool api_kvdb_del(LeviRsKvDbHandle h, LeviRsStr key);
        bool api_kvdb_has(LeviRsKvDbHandle h, LeviRsStr key);
        bool api_kvdb_is_empty(LeviRsKvDbHandle h);
        void api_kvdb_iter(LeviRsKvDbHandle h, void* ctx, LeviRsKvSink sink);
        void kvdbOnRustModGone(RustMod* mod);

        /* —— Money.cpp —— */
        using LLMoneyEvent = ::LeviRsApi::LLMoneyEvent;
        using LLMoneyCallback = ::LeviRsApi::LLMoneyCallback;

        long long api_get_money(LeviRsStr xuid);

        bool api_set_money(LeviRsStr xuid, long long money);
        bool api_add_money(LeviRsStr xuid, long long money);
        bool api_reduce_money(LeviRsStr xuid, long long money);
        bool api_trans_money(LeviRsStr from, LeviRsStr to, long long val, LeviRsStr note = "");

        void api_money_get_hist(LeviRsStr xuid, int timediff, void* ctx, LeviRsStrSink sink);
        void api_money_clear_hist(int difftime = 0);

        void api_money_listen_before_event(LLMoneyCallback callback);
        void api_money_listen_after_event(LLMoneyCallback callback);
        /** 摘掉这个 mod 名下的 money 监听器。从 onRustModGone 调。 */
        void moneyOnRustModGone(RustMod* mod);

        /** 序列化方块 NBT（`{name,states,version}`）→ Block const*，失败 nullptr。
         *  会跑引擎的版本升级表，老存档里的方块能被正确升级。 */
        Block const* blockFromSnbt(std::string_view snbt);

        /** 方块名（可省 `minecraft:` 前缀）→ 默认状态，**认不出返回 nullptr**。
         *  不返回占位方块 —— 否则 `//set 拼错的名字` 会安静地刷掉整片地区。 */
        Block const* defaultBlockNamed(std::string_view name);

        /** 方块编辑的变更来源，和 //set 用同一个（commandsChange）。
         *  setBlock / destroyBlock 的最后一个参数要它，而且是**引用不是指针**。 */
        BlockChangeContext blockEditContext();

        /* ── 液体层（含水方块）── */
        bool api_get_extra_block(int32_t dim, int32_t x, int32_t y, int32_t z,
                                 void* ctx, LeviRsStrSink sink);
        bool api_set_extra_block(int32_t dim, int32_t x, int32_t y, int32_t z,
                                 LeviRsStr blockSpec, int32_t updateFlags);

        void api_money_ranking(unsigned short num, void* ctx, LeviRsStrSink sink);

        /* ── SysInfo.cpp ── */
        bool api_sys_info_str(int32_t prop, void* ctx, LeviRsStrSink sink);
        bool api_sys_get_env(LeviRsStr name, void* ctx, LeviRsStrSink sink);
        bool api_sys_set_env(LeviRsStr name, LeviRsStr value);
        bool api_sys_is_wine();

        /* ── GapFill.cpp — ABI v5 additive gap-fill (struct_size-gated) ── */

        /* Player: equipment, cooldown, network */
        bool api_player_get_carried_item(LeviRsPlayerSel sel, void* ctx, LeviRsStrSink sink);
        bool api_player_get_item(LeviRsPlayerSel sel, int32_t slot, void* ctx, LeviRsStrSink sink);
        bool api_player_set_item(LeviRsPlayerSel sel, int32_t slot, LeviRsStr item_snbt);
        bool api_player_get_equipment(LeviRsPlayerSel sel, void* ctx, LeviRsStrSink sink);
        int32_t api_player_get_cooldown(LeviRsPlayerSel sel, LeviRsStr item_name);
        bool api_player_start_cooldown(LeviRsPlayerSel sel, LeviRsStr item_name, int32_t ticks);
        bool api_player_get_network_status(LeviRsPlayerSel sel, void* ctx, LeviRsStrSink sink);

        /* Actor: relationships, equipment, effects, geometry */
        bool api_actor_get_vehicle(LeviRsActorId id, LeviRsActorId* out);
        bool api_actor_get_first_passenger(LeviRsActorId id, LeviRsActorId* out);
        bool api_actor_get_owner(LeviRsActorId id, LeviRsActorId* out);
        bool api_actor_get_target(LeviRsActorId id, LeviRsActorId* out);
        bool api_actor_get_equipped_item(LeviRsActorId id, int32_t slot, void* ctx, LeviRsStrSink sink);
        bool api_actor_set_equipped_item(LeviRsActorId id, int32_t slot, LeviRsStr item_snbt);
        bool api_actor_get_effects(LeviRsActorId id, void* ctx, LeviRsStrSink sink);
        bool api_actor_get_status_flag(LeviRsActorId id, int32_t flag_index);
        bool api_actor_set_status_flag(LeviRsActorId id, int32_t flag_index, bool value);
        bool api_actor_trace_ray(
            LeviRsActorId id, float max_dist, bool include_actors, bool include_blocks,
            void* ctx, LeviRsStrSink sink);
        bool api_actor_distance_to(LeviRsActorId id, LeviRsActorId other, double* out);
        bool api_actor_get_aabb(LeviRsActorId id, void* ctx, LeviRsStrSink sink);
        bool api_actor_clone(LeviRsActorId id, int32_t dim, double x, double y, double z, LeviRsActorId* out);

        /* Block: state get/set, collision shape */
        bool api_block_get_state(
            int32_t dim, int32_t x, int32_t y, int32_t z, LeviRsStr state_name,
            void* ctx, LeviRsStrSink sink);
        bool api_block_set_state(
            int32_t dim, int32_t x, int32_t y, int32_t z, LeviRsStr state_name, LeviRsStr value);
        bool api_block_get_collision_shape(
            int32_t dim, int32_t x, int32_t y, int32_t z, void* ctx, LeviRsStrSink sink);

        /* Item: enchants, matching, NBT */
        bool api_item_get_enchants(LeviRsStr item_snbt, void* ctx, LeviRsStrSink sink);
        bool api_item_set_enchants(
            LeviRsStr item_snbt, LeviRsStr enchants_snbt, void* ctx, LeviRsStrSink out);
        bool api_item_matches(LeviRsStr a, LeviRsStr b);
        bool api_item_get_user_data(LeviRsStr item_snbt, void* ctx, LeviRsStrSink sink);

        /* Level: biome, spawn, save, weather, path, sleep */
        bool api_level_get_biome(int32_t dim, int32_t x, int32_t y, int32_t z, void* ctx, LeviRsStrSink sink);
        bool api_level_get_default_spawn(int32_t* x, int32_t* y, int32_t* z);
        bool api_level_set_default_spawn(int32_t x, int32_t y, int32_t z);
        bool api_level_save();
    int32_t api_level_delete_chunk_keys(int32_t dim, int32_t chunk_x, int32_t chunk_z);
    int32_t api_level_chunks_loaded(int32_t dim, int32_t min_x, int32_t min_z, int32_t max_x, int32_t max_z);
    uint64_t api_player_conn_id(LeviRsPlayerSel who);
    int32_t api_level_chunk_keys(int32_t dim, int32_t chunk_x, int32_t chunk_z, void* ctx, LeviRsStrSink sink);
    bool api_level_delete_key(LeviRsStr key);

    /* ── 追加槽（190）── */
    int32_t api_level_set_biome(int32_t dim,
                                int32_t minX, int32_t minZ,
                                int32_t maxX, int32_t maxZ,
                                LeviRsStr biome);
        bool api_level_get_sleep_status(void* ctx, LeviRsStrSink sink);
        bool api_level_update_weather(float rain_level, int32_t rain_time, float lightning_level, int32_t lightning_time);
        bool api_level_find_path(LeviRsActorId id, int32_t x, int32_t y, int32_t z, void* ctx, LeviRsStrSink sink);

        /* ── Edit.cpp — 批量世界编辑（原生写入，不走命令）── */
        bool api_edit_set_block_nbt(
            int32_t dim, int32_t x, int32_t y, int32_t z, LeviRsStr snbt, int32_t update_flags);
        bool api_edit_set_block_states(
            int32_t dim,
            int32_t x,
            int32_t y,
            int32_t z,
            LeviRsStr name,
            LeviRsStr states_snbt,
            int32_t update_flags);
        bool api_edit_set_block_entity(
            int32_t dim, int32_t x, int32_t y, int32_t z, LeviRsStr snbt);
        bool api_edit_spawn_entity_nbt(
            int32_t dim,
            LeviRsStr snbt,
            bool use_pos,
            double x,
            double y,
            double z,
            LeviRsActorId* out);
        bool api_edit_trace_ray(
            LeviRsActorId id,
            float max_dist,
            bool include_actors,
            bool include_blocks,
            void* ctx,
            LeviRsStrSink sink);

        /* ── Client.cpp — client-only bridge (LEVI_RS_TARGET_CLIENT) ── */
#ifdef LEVI_RS_TARGET_CLIENT
        bool api_client_get_local_player(void* ctx, LeviRsStrSink sink);
        bool api_client_is_in_level();
        bool api_client_get_screen_name(void* ctx, LeviRsStrSink sink);
        LeviRsKeyHandle api_client_register_key(
            LeviRsModHandle mod,
            LeviRsStr name,
            int32_t const* key_codes,
            int32_t key_count,
            bool allow_remap,
            LeviRsKeyCb down_cb,
            LeviRsKeyCb up_cb,
            void* user
        );
        bool api_client_unregister_key(LeviRsKeyHandle handle);
        bool api_client_get_key_codes(LeviRsKeyHandle handle, void* ctx, LeviRsStrSink sink);
        /** 解除并释放这个 mod 名下的按键绑定。从 onRustModGone 调。 */
        void clientOnRustModGone(RustMod* mod);
#endif
    } // namespace bridge
} // namespace levi_rs
