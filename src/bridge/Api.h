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

#include "LeviRsAbi.h"

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

        /* ── SysInfo.cpp ── */
        bool api_sys_info_str(int32_t prop, void* ctx, LeviRsStrSink sink);
        bool api_sys_get_env(LeviRsStr name, void* ctx, LeviRsStrSink sink);
        bool api_sys_set_env(LeviRsStr name, LeviRsStr value);
        bool api_sys_is_wine();
    } // namespace bridge
} // namespace levi_rs
