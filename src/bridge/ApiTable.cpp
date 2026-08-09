/**
 * bridge/ApiTable.cpp — the LeviRsApi singleton.
 *
 * THE ONLY FILE WHERE FIELD ORDER MATTERS. The initializer below must list
 * every function in exactly the order LeviRsAbi.h declares the struct
 * fields — a C++20 designated-initializer-style comment per entry keeps
 * review diffs honest, and tools/check_abi_sync.py cross-checks this file,
 * the header, and the Rust sys mirror on every change.
 */
#include "BridgeApi.h"
#include "bridge/Api.h"

#include <string_view>

#include "RustMod.h"

#ifdef LEVI_RS_FEATURE_MORE_DIMENSIONS
namespace more_dimensions::bridge
{
    int32_t api_md_add_simple_dimension(LeviRsStr name, uint32_t seed, int32_t generatorTypeInt);
    void    api_md_set_dimension_rule(int32_t dimension, int32_t rule, bool allow);
    bool    api_md_get_dimension_rule(int32_t dimension, int32_t rule, bool* outAllow);
    void    api_md_clear_dimension_rules(int32_t dimension);
    int32_t api_md_get_dimension_id(LeviRsStr name);
    int32_t api_md_add_plot_dimension(LeviRsStr name, uint32_t seed, LeviRsStr layoutSnbt);
    bool    api_md_is_available();
} // namespace more_dimensions::bridge
#endif

namespace levi_rs
{
    namespace
    {
        using namespace bridge;

        const LeviRsApi gApi{
            /* abi_version        */ LEVI_RS_ABI_VERSION,
            /* struct_size        */ sizeof(LeviRsApi),

            /* ── v1 ── */
            /* log                */ api_log,
            /* gaming_status      */ api_gaming_status,
            /* schedule           */ api_schedule,
            /* schedule_after     */ api_schedule_after,
            /* subscribe_event    */ api_subscribe_event,
            /* unsubscribe_event  */ api_unsubscribe_event,
            /* list_events        */ api_list_events,
            /* execute_command    */ api_execute_command,
            /* register_command   */ api_register_command,

            /* ── v2 ── */
            /* get_current_tick    */ api_get_current_tick,
            /* get_tick_delta_time */ api_get_tick_delta_time,
            /* get_player_count    */ api_get_player_count,
            /* get_sim_paused      */ api_get_sim_paused,

            /* ── v3 ── */
            /* spawn_particle      */ api_spawn_particle,
            /* get_player_position */ api_get_player_position,
            /* scan_region         */ api_scan_region,

            /* ── v5 §A world read/write & clock ── */
            /* get_block           */ api_get_block,
            /* set_block           */ api_set_block,
            /* get_time            */ api_get_time,
            /* set_time            */ api_set_time,
            /* set_weather         */ api_set_weather,

            /* ── v5 §B player management ── */
            /* list_players        */ api_list_players,
            /* player_resolve      */ api_player_resolve,
            /* player_send_message */ api_player_send_message,
            /* player_disconnect   */ api_player_disconnect,
            /* broadcast_message   */ api_broadcast_message,
            /* player_set_gamemode */ api_player_set_gamemode,
            /* player_teleport     */ api_player_teleport,
            /* player_get_num      */ api_player_get_num,
            /* player_get_str      */ api_player_get_str,
            /* player_set_num      */ api_player_set_num,
            /* player_action       */ api_player_action,

            /* ── v5 §C actors ── */
            /* list_actors         */ api_list_actors,
            /* actor_snapshot      */ api_actor_snapshot,
            /* actor_get_num       */ api_actor_get_num,
            /* actor_get_str       */ api_actor_get_str,
            /* actor_action        */ api_actor_action,
            /* spawn_mob           */ api_spawn_mob,
            /* explode             */ api_explode,

            /* ── v5 §D blocks & block entities ── */
            /* block_get_num       */ api_block_get_num,
            /* block_get_str       */ api_block_get_str,
            /* block_action        */ api_block_action,
            /* block_entity_snbt   */ api_block_entity_snbt,

            /* ── v5 §E items & containers ── */
            /* item_get_num        */ api_item_get_num,
            /* item_get_str        */ api_item_get_str,
            /* item_transform      */ api_item_transform,
            /* container_size      */ api_container_size,
            /* container_get_item  */ api_container_get_item,
            /* container_set_item  */ api_container_set_item,
            /* container_add_item  */ api_container_add_item,
            /* container_remove_item */ api_container_remove_item,
            /* container_clear     */ api_container_clear,

            /* ── v5 §F scoreboard ── */
            /* scoreboard_op       */ api_scoreboard_op,

            /* ── v5 §G forms ── */
            /* form_send           */ api_form_send,

            /* ── v5 §H parameterized commands & enums ── */
            /* register_command_ex        */ api_register_command_ex,
            /* register_command_enum      */ api_register_command_enum,
            /* register_command_soft_enum */ api_register_command_soft_enum,
            /* update_command_soft_enum   */ api_update_command_soft_enum,

            /* ── v5 §I nbt / kvdb / system / server / money ── */
            /* nbt_snbt_to_binary  */ api_nbt_snbt_to_binary,
            /* nbt_binary_to_snbt  */ api_nbt_binary_to_snbt,
            /* kvdb_open           */ api_kvdb_open,
            /* kvdb_close          */ api_kvdb_close,
            /* kvdb_get            */ api_kvdb_get,
            /* kvdb_set            */ api_kvdb_set,
            /* kvdb_del            */ api_kvdb_del,
            /* kvdb_has            */ api_kvdb_has,
            /* kvdb_is_empty       */ api_kvdb_is_empty,
            /* kvdb_iter           */ api_kvdb_iter,
            /* sys_info_str        */ api_sys_info_str,
            /* sys_get_env         */ api_sys_get_env,
            /* sys_set_env         */ api_sys_set_env,
            /* sys_is_wine         */ api_sys_is_wine,
            /* get_difficulty      */ api_get_difficulty,
            /* set_difficulty      */ api_set_difficulty,
            /* get_seed            */ api_get_seed,
            /* game_rule_get       */ api_game_rule_get,
            /* game_rule_set       */ api_game_rule_set,
            /* server_info_str     */ api_server_info_str,
            /* spawn_particle_for  */ api_spawn_particle_for,
            /* send_packet         */ api_send_packet,
            /* tick_freeze         */ api_tick_freeze,
            /* tick_step           */ api_tick_step,
            /* tick_warp           */ api_tick_warp,
            /* profile_begin       */ api_profile_begin,
            /* profile_take        */ api_profile_take,
            /* sim_spawn           */ api_sim_spawn,
            /* sim_do              */ api_sim_do,
            /* sim_is              */ api_sim_is,
            /* sim_list            */ api_sim_list,
            /* villages            */ api_villages,
            /* structures_near     */ api_structures_near,
            /* player_send_message_typed */ api_player_send_message_typed,
            /* get_money                 */ api_get_money,
            /* set_money                 */ api_set_money,
            /* add_money                 */ api_add_money,
            /* reduce_money              */ api_reduce_money,
            /* trans_money               */ api_trans_money,
            /* money_get_hist            */ api_money_get_hist,
            /* money_clear_hist          */ api_money_clear_hist,
            /* money_listen_before_event */ api_money_listen_before_event,
            /* money_listen_after_event  */ api_money_listen_after_event,
            /* money_ranking             */ api_money_ranking,

            /* ═════════════════ ABI v5 Additive — API gap fill (struct_size-gated) ═════════════════ */

            /* ── Player: equipment, cooldown, network ── */
            /* player_get_carried_item    */ api_player_get_carried_item,
            /* player_get_item            */ api_player_get_item,
            /* player_set_item            */ api_player_set_item,
            /* player_get_equipment       */ api_player_get_equipment,
            /* player_get_cooldown        */ api_player_get_cooldown,
            /* player_start_cooldown      */ api_player_start_cooldown,
            /* player_get_network_status  */ api_player_get_network_status,

            /* ── Actor: relationships, equipment, effects, geometry ── */
            /* actor_get_vehicle          */ api_actor_get_vehicle,
            /* actor_get_first_passenger  */ api_actor_get_first_passenger,
            /* actor_get_owner            */ api_actor_get_owner,
            /* actor_get_target           */ api_actor_get_target,
            /* actor_get_equipped_item    */ api_actor_get_equipped_item,
            /* actor_set_equipped_item    */ api_actor_set_equipped_item,
            /* actor_get_effects          */ api_actor_get_effects,
            /* actor_get_status_flag      */ api_actor_get_status_flag,
            /* actor_set_status_flag      */ api_actor_set_status_flag,
            /* actor_trace_ray            */ api_actor_trace_ray,
            /* actor_distance_to          */ api_actor_distance_to,
            /* actor_get_aabb             */ api_actor_get_aabb,
            /* actor_clone                */ api_actor_clone,

            /* ── Block: state get/set, collision shape ── */
            /* block_get_state            */ api_block_get_state,
            /* block_set_state            */ api_block_set_state,
            /* block_get_collision_shape  */ api_block_get_collision_shape,

            /* ── Item: enchants, matching, NBT ── */
            /* item_get_enchants          */ api_item_get_enchants,
            /* item_set_enchants          */ api_item_set_enchants,
            /* item_matches               */ api_item_matches,
            /* item_get_user_data         */ api_item_get_user_data,

            /* ── Level: biome, spawn, save, weather, path, sleep ── */
            /* level_get_biome            */ api_level_get_biome,
            /* level_get_default_spawn    */ api_level_get_default_spawn,
            /* level_set_default_spawn    */ api_level_set_default_spawn,
            /* level_save                 */ api_level_save,
            /* level_get_sleep_status     */ api_level_get_sleep_status,
            /* level_update_weather       */ api_level_update_weather,
            /* level_find_path            */ api_level_find_path,

            /* ── Packet interception (v5 additive) ── */
            /* packet_hook_register       */ api_packet_hook_register,
            /* packet_hook_unregister     */ api_packet_hook_unregister,
            /* packet_conn_hook_register  */ api_packet_conn_hook_register,
            /* packet_conn_hook_unregister*/ api_packet_conn_hook_unregister,

#ifdef LEVI_RS_TARGET_CLIENT
            /* client_get_local_player    */ api_client_get_local_player,
            /* client_is_in_level         */ api_client_is_in_level,
            /* client_get_screen_name     */ api_client_get_screen_name,
            /* client_register_key        */ api_client_register_key,
            /* client_unregister_key      */ api_client_unregister_key,
            /* client_get_key_codes       */ api_client_get_key_codes,
#endif

#ifdef LEVI_RS_FEATURE_MORE_DIMENSIONS
            /* md_is_available            */ more_dimensions::bridge::api_md_is_available,
            /* md_add_simple_dimension    */ more_dimensions::bridge::api_md_add_simple_dimension,
            // 顺序必须和 LeviRsAbi.h 里的声明顺序**逐字**一致 —— 这张表是位置
            // 对应的，插错位置会让 Rust 侧调到相邻的另一个函数，而且不会报错。
            /* md_set_dimension_rule      */ more_dimensions::bridge::api_md_set_dimension_rule,
            /* md_get_dimension_rule      */ more_dimensions::bridge::api_md_get_dimension_rule,
            /* md_clear_dimension_rules   */ more_dimensions::bridge::api_md_clear_dimension_rules,
            /* md_get_dimension_id        */ more_dimensions::bridge::api_md_get_dimension_id,
            /* md_add_plot_dimension      */ more_dimensions::bridge::api_md_add_plot_dimension,
#endif

            /* ── Common additive tail (after every #ifdef block) ── */
            /* schedule_for               */ api_schedule_for,
            /* schedule_after_for         */ api_schedule_after_for,
            /* schedule_cancel            */ api_schedule_cancel,
            /* schedule_pending_count     */ api_schedule_pending_count,
            /* container_refresh          */ api_container_refresh,
            /* player_send_title          */ api_player_send_title,
            /* bus_subscribe              */ api_bus_subscribe,
            /* bus_unsubscribe            */ api_bus_unsubscribe,
            /* bus_publish                */ api_bus_publish,
            /* bus_publish_vetoable       */ api_bus_publish_vetoable,
            /* bus_subscriber_count       */ api_bus_subscriber_count,
            /* md_set_plot_grid           */ api_md_set_plot_grid,
            /* md_clear_plot_grid         */ api_md_clear_plot_grid,
            /* md_set_plot_merges         */ api_md_set_plot_merges,
            /* service_register           */ api_service_register,
            /* service_unregister         */ api_service_unregister,
            /* service_call               */ api_service_call,
            /* service_list               */ api_service_list,

            /* ── 批量世界编辑（Edit.cpp）── */
            /* edit_set_block_nbt         */ api_edit_set_block_nbt,
            /* edit_set_block_states      */ api_edit_set_block_states,
            /* edit_set_block_entity      */ api_edit_set_block_entity,
            /* edit_spawn_entity_nbt      */ api_edit_spawn_entity_nbt,
            /* edit_trace_ray             */ api_edit_trace_ray,
        };
    } // namespace

    const LeviRsApi* getBridgeApi() { return &gApi; }

    namespace detail
    {
        void onRustModGone(RustMod* mod)
        {
            // Scheduler first: a queued task can re-enter any of the
            // subsystems below, so cut the task source before tearing them
            // down rather than after.
            bridge::schedulerOnRustModGone(mod);
            // Bus second, same reasoning one step removed: another mod
            // publishing during teardown would otherwise dispatch into this
            // mod's dylib after we have started dismantling it.
            bridge::busOnRustModGone(mod);
            // Services next, for the same reason one step removed: a query
            // arriving during teardown would otherwise call into this mod's
            // dylib after we have started dismantling it.
            bridge::servicesOnRustModGone(mod);
            bridge::commandsOnRustModGone(mod);
            bridge::formsOnRustModGone(mod);
            bridge::kvdbOnRustModGone(mod);
            bridge::hookEventDropMod(mod);       // detach bridge-hook event subscribers
            bridge::packetHooksOnRustModGone(mod); // detach raw packet interceptors
        }
    } // namespace detail
} // namespace levi_rs

bool leviRsVerifyStrLayout()
{
    // Read the view's raw bytes as {ptr, len} and compare to data()/size().
    // This layout is an MSVC STL detail, not standard-guaranteed — fail
    // loudly here instead of Rust silently misreading pointer/length.
    static constexpr char kProbe[] = "levi-rs-layout-probe";
    std::string_view sv(kProbe, sizeof(kProbe) - 1);

    struct RawView
    {
        const char* ptr;
        size_t len;
    };
    static_assert(sizeof(RawView) == sizeof(std::string_view));

    auto const& raw = reinterpret_cast<RawView const&>(sv);
    return raw.ptr == sv.data() && raw.len == sv.size();
}