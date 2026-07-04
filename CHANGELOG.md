# Changelog

## v0.1.3

- ABI v2: add server stats API
  - `get_current_tick` — current tick ID (`Level::getCurrentTick()`)
  - `get_tick_delta_time` — ms between ticks, TPS = 1000.0 / delta_time
  - `get_player_count` — active player count (`Level::getActivePlayerCount()`)
  - `get_sim_paused` — whether simulation is paused (`Level::getSimPaused()`)
- Safe Rust: `Server::get_current_tick()`, `get_tick_delta_time()`, `get_tps()`,
  `get_active_player_count()`, `is_sim_paused()`
- Bump `LEVI_RS_ABI_VERSION` to 2 (additive: new fields appended to `LeviRsApi`)

## v0.1.2

- Fix: make `__init_runtime` unsafe to satisfy clippy::not_unsafe_ptr_arg_deref
- Add tooth.json for Lip package manager support

## v0.1.1

- Initial release: C++ loader mod + levilamina / levilamina-sys Rust crates
- Event bus via SNBT, command execution, scheduling, per-mod logging
