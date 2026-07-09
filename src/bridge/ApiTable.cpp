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

            /* ── v4 §A world read/write & clock ── */
            /* get_block           */ api_get_block,
            /* set_block           */ api_set_block,
            /* get_time            */ api_get_time,
            /* set_time            */ api_set_time,
            /* set_weather         */ api_set_weather,

            /* ── v4 §B player management ── */
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

            /* ── v4 §C actors ── */
            /* list_actors         */ api_list_actors,
            /* actor_snapshot      */ api_actor_snapshot,
            /* actor_get_num       */ api_actor_get_num,
            /* actor_get_str       */ api_actor_get_str,
            /* actor_action        */ api_actor_action,
            /* spawn_mob           */ api_spawn_mob,
            /* explode             */ api_explode,

            /* ── v4 §D blocks & block entities ── */
            /* block_get_num       */ api_block_get_num,
            /* block_get_str       */ api_block_get_str,
            /* block_action        */ api_block_action,
            /* block_entity_snbt   */ api_block_entity_snbt,

            /* ── v4 §E items & containers ── */
            /* item_get_num        */ api_item_get_num,
            /* item_get_str        */ api_item_get_str,
            /* item_transform      */ api_item_transform,
            /* container_size      */ api_container_size,
            /* container_get_item  */ api_container_get_item,
            /* container_set_item  */ api_container_set_item,
            /* container_add_item  */ api_container_add_item,
            /* container_remove_item */ api_container_remove_item,
            /* container_clear     */ api_container_clear,

            /* ── v4 §F scoreboard ── */
            /* scoreboard_op       */ api_scoreboard_op,

            /* ── v4 §G forms ── */
            /* form_send           */ api_form_send,

            /* ── v4 §H parameterized commands & enums ── */
            /* register_command_ex        */ api_register_command_ex,
            /* register_command_enum      */ api_register_command_enum,
            /* register_command_soft_enum */ api_register_command_soft_enum,
            /* update_command_soft_enum   */ api_update_command_soft_enum,

            /* ── v4 §I nbt / kvdb / system / server ── */
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
        };
    } // namespace

    const LeviRsApi* getBridgeApi() { return &gApi; }

    namespace detail
    {
        void onRustModGone(RustMod* mod)
        {
            bridge::commandsOnRustModGone(mod);
            bridge::formsOnRustModGone(mod);
            bridge::kvdbOnRustModGone(mod);
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
