// Server-only API stubs for the client build (target_type=client).
// ApiTable.cpp references these symbols; they return false/0/nullptr to
// signal "unsupported on client" (Rust safe layer maps to Err).
#include "bridge/Api.h"

namespace levi_rs::bridge
{
    bool api_execute_command(LeviRsStr, void*, LeviRsCmdOutputSink) { return false; }
    bool api_register_command(LeviRsModHandle, LeviRsStr, LeviRsStr, int32_t, LeviRsCommandCb, void*) { return false; }
    bool api_register_command_ex(LeviRsModHandle, LeviRsStr, LeviRsStr, int32_t, LeviRsStr, LeviRsCommandCb, void*) { return false; }
    bool api_register_command_enum(LeviRsStr, LeviRsStr) { return false; }
    bool api_register_command_soft_enum(LeviRsStr, LeviRsStr) { return false; }
    bool api_update_command_soft_enum(LeviRsStr, int32_t, LeviRsStr) { return false; }
    void commandsOnRustModGone(RustMod*) {}

    bool api_get_time(int64_t*) { return false; }
    bool api_set_time(int64_t) { return false; }
    bool api_set_weather(int32_t) { return false; }
    bool api_get_difficulty(int32_t*) { return false; }
    bool api_set_difficulty(int32_t) { return false; }
    bool api_get_seed(int64_t*) { return false; }
    bool api_game_rule_get(LeviRsStr, void*, LeviRsStrSink) { return false; }
    bool api_game_rule_set(LeviRsStr, LeviRsStr) { return false; }
    bool api_server_info_str(int32_t, void*, LeviRsStrSink) { return false; }

    bool api_spawn_particle_for(LeviRsPlayerSel, int32_t, LeviRsStr, double, double, double) { return false; }
    bool api_send_packet(LeviRsPlayerSel, int32_t, uint8_t const*, size_t) { return false; }

    bool api_tick_freeze(bool) { return false; }
    bool api_tick_step(uint32_t) { return false; }
    bool api_tick_warp(double) { return false; }

    bool api_profile_begin(uint32_t) { return false; }
    bool api_profile_take(void*, LeviRsStrSink) { return false; }

    LeviRsListenerHandle hookEventSubscribe(RustMod*, std::string_view, LeviRsEventCb, void*) { return nullptr; }
    bool hookEventUnsubscribe(RustMod*, LeviRsListenerHandle) { return false; }
    void hookEventDropMod(RustMod*) {}
    void hookEventList(void*, LeviRsStrSink) {}

    bool api_sim_spawn(LeviRsStr, int32_t, double, double, double) { return false; }
    bool api_sim_do(LeviRsPlayerSel, LeviRsStr, LeviRsStr) { return false; }
    bool api_sim_is(LeviRsPlayerSel) { return false; }
    void api_sim_list(void*, LeviRsStrSink) {}

    void api_villages(int32_t, void*, LeviRsStrSink) {}
    void api_structures_near(int32_t, int32_t, int32_t, int32_t, int32_t, void*, LeviRsStrSink) {}

    bool api_scoreboard_op(int32_t, LeviRsStr, LeviRsStr, int64_t, void*, LeviRsStrSink) { return false; }

    bool api_form_send(LeviRsModHandle, LeviRsPlayerSel, int32_t, LeviRsStr, LeviRsFormResultCb, void*) { return false; }
    void formsOnRustModGone(RustMod*) {}

    long long api_get_money(LeviRsStr) { return 0; }
    bool api_set_money(LeviRsStr, long long) { return false; }
    bool api_add_money(LeviRsStr, long long) { return false; }
    bool api_reduce_money(LeviRsStr, long long) { return false; }
    bool api_trans_money(LeviRsStr, LeviRsStr, long long, LeviRsStr) { return false; }
    void api_money_get_hist(LeviRsStr, int, void*, LeviRsStrSink) {}
    void api_money_clear_hist(int) {}
    void api_money_listen_before_event(LLMoneyCallback) {}
    void api_money_listen_after_event(LLMoneyCallback) {}
    void api_money_ranking(unsigned short, void*, LeviRsStrSink) {}

    bool api_player_get_carried_item(LeviRsPlayerSel, void*, LeviRsStrSink) { return false; }
    bool api_player_get_item(LeviRsPlayerSel, int32_t, void*, LeviRsStrSink) { return false; }
    bool api_player_set_item(LeviRsPlayerSel, int32_t, LeviRsStr) { return false; }
    bool api_player_get_equipment(LeviRsPlayerSel, void*, LeviRsStrSink) { return false; }
    int32_t api_player_get_cooldown(LeviRsPlayerSel, LeviRsStr) { return -1; }
    bool api_player_start_cooldown(LeviRsPlayerSel, LeviRsStr, int32_t) { return false; }
    bool api_player_get_network_status(LeviRsPlayerSel, void*, LeviRsStrSink) { return false; }
    bool api_actor_get_vehicle(LeviRsActorId, LeviRsActorId*) { return false; }
    bool api_actor_get_first_passenger(LeviRsActorId, LeviRsActorId*) { return false; }
    bool api_actor_get_owner(LeviRsActorId, LeviRsActorId*) { return false; }
    bool api_actor_get_target(LeviRsActorId, LeviRsActorId*) { return false; }
    bool api_actor_get_equipped_item(LeviRsActorId, int32_t, void*, LeviRsStrSink) { return false; }
    bool api_actor_set_equipped_item(LeviRsActorId, int32_t, LeviRsStr) { return false; }
    bool api_actor_get_effects(LeviRsActorId, void*, LeviRsStrSink) { return false; }
    bool api_actor_get_status_flag(LeviRsActorId, int32_t) { return false; }
    bool api_actor_set_status_flag(LeviRsActorId, int32_t, bool) { return false; }
    bool api_actor_trace_ray(LeviRsActorId, float, bool, bool, void*, LeviRsStrSink) { return false; }
    bool api_actor_distance_to(LeviRsActorId, LeviRsActorId, double*) { return false; }
    bool api_actor_get_aabb(LeviRsActorId, void*, LeviRsStrSink) { return false; }
    bool api_actor_clone(LeviRsActorId, int32_t, double, double, double, LeviRsActorId*) { return false; }
    bool api_block_get_state(int32_t, int32_t, int32_t, int32_t, LeviRsStr, void*, LeviRsStrSink) { return false; }
    bool api_block_set_state(int32_t, int32_t, int32_t, int32_t, LeviRsStr, LeviRsStr) { return false; }
    bool api_block_get_collision_shape(int32_t, int32_t, int32_t, int32_t, void*, LeviRsStrSink) { return false; }
    bool api_item_get_enchants(LeviRsStr, void*, LeviRsStrSink) { return false; }
    bool api_item_set_enchants(LeviRsStr, LeviRsStr, void*, LeviRsStrSink) { return false; }
    bool api_item_matches(LeviRsStr, LeviRsStr) { return false; }
    bool api_item_get_user_data(LeviRsStr, void*, LeviRsStrSink) { return false; }
    bool api_level_get_biome(int32_t, int32_t, int32_t, int32_t, void*, LeviRsStrSink) { return false; }
    bool api_level_get_default_spawn(int32_t*, int32_t*, int32_t*) { return false; }
    bool api_level_set_default_spawn(int32_t, int32_t, int32_t) { return false; }
    bool api_level_save() { return false; }
    bool api_level_get_sleep_status(void*, LeviRsStrSink) { return false; }
    bool api_level_update_weather(float, int32_t, float, int32_t) { return false; }
    bool api_level_find_path(LeviRsActorId, int32_t, int32_t, int32_t, void*, LeviRsStrSink) { return false; }
} // namespace levi_rs::bridge
