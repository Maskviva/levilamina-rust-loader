# Changelog

## v1.0.0

The "everything" release: ABI v4 grows the bridge from 18 to 80 entry points and
the safe crate from one file to a full module tree. All new fields are appended
(v1-v3 prefix byte-identical), but `LEVI_RS_ABI_VERSION` bumps to 4 - loader
**and** mods must be rebuilt together.

### Object model

Handles are identifiers, never pointers (re-resolved on every call):
`Player` = selector (name / xuid / uuid), `Entity` = `ActorUniqueID`,
`Block` = (dimension, position), `ItemStack` = pure SNBT value object,
`Container` = owner + which. Nothing a mod holds can dangle.

### New APIs (ABI v4)

- **World & clock** - `get_block` / `set_block` (via `/setblock`), `get_time` /
  `set_time`, `set_weather`, `get_difficulty` / `set_difficulty`, `get_seed`,
  `game_rule_get` / `game_rule_set`, `explode`, `spawn_mob`
- **Players** - enumeration with identity+position, resolve to entity, messages,
  disconnect, broadcast, gamemode, cross-dimension teleport, 21 numeric props
  (attributes read AND write via `AttributeInstanceForwarder`), 6 string props,
  abilities, give-item, spawn point, titles (title / subtitle / actionbar)
- **Actors** - enumeration, `Actor::save` snapshots, 19 numeric + 2 string
  props, kill / despawn / heal / fire / teleport / name tag / tags / effects
- **Blocks** - properties (air, data, tags, description id, ...) + block-entity NBT
- **Items & containers** - item queries and transforms (custom name, damage,
  count, lore) through engine rebuild-serialize; one container code path for
  player inventories, ender chests and block containers
- **Scoreboard** - objectives CRUD, get/set/add/reduce/reset score, display slots
- **Forms** - SimpleForm / CustomForm / ModalForm builders with async callbacks;
  exactly-once delivery, muted when the mod is disabled, cleared at unload
- **Parameterized commands** - typed overloads (21 param kinds incl. player /
  actor selectors, block_pos, vec3), hard enums, soft enums (+ live updates)
- **NBT** - pure-Rust SNBT object model (`NbtValue`: parse / edit / serialize);
  binary conversions (disk & network format) through the engine codec
- **KvDb** - per-mod LevelDB confined to the mod data dir; thread-safe;
  RAII close + forced close at unload
- **System** - OS name/version, locale, local time, env vars, Wine detection;
  server info: BDS version, protocol version

### Structured events

`EventRef::value()` / `set_value()` expose event data as `NbtValue`;
`cancel()` is now a structured edit (`cancelled = 1b`) with the old textual
flip as fallback. `event::names` ships verified event-id constants
(`PlayerPlacingBlockEvent` / `PlayerPlacedBlockEvent`, `SpawningMobEvent` /
`SpawnedMobEvent`, `ConsoleOutputtingEvent` / `ConsoleOutputtedEvent`, ...).
`EventRef::player()` / `player_handle()` decode the `_player` identity block.

### Internals

- C++ bridge split from one 1900-line file into 18 per-domain modules under
  `src/bridge/`; `ApiTable.cpp` is the only file where field order matters
- `tools/check_abi_sync.py`: three-way ABI order check (C header <-> table
  initializer <-> Rust sys mirror) - run it before every ABI commit
- Unload discipline extended: command bindings nulled, pending form tickets
  cleared, leftover KvDb handles force-closed (with a warning)
- Player identity unified: every by-name lookup resolves `getRealName()` first,
  then falls back to the display name (`getNameTag()`)
- Version-sensitive writes route through vanilla commands (`/setblock`,
  `/gamemode`, `/tp`, `/time`, `/weather`, `/difficulty`, `/gamerule`,
  `/spawnpoint`, `/title`, `/damage`, `/scoreboard ... setdisplay`) so they
  survive BDS bumps untouched

## v0.1.4

- ABI v3: add world-reading API (server-thread only)
  - `spawn_particle` — spawn a particle at a world coord (`Level::spawnParticleEffect`)
  - `get_player_position` — a player's feet pos + dimension by name (`Level::forEachPlayer`)
  - `scan_region` — walk a cuboid, streaming each cell's block (name + state SNBT via
    `Block::getSerializationId().toSnbt`) and each contained entity (`Actor::save`)
- Safe Rust: `Server::spawn_particle()`, `player_position()`, `scan_region()`, plus the
  layered data model `Scan` / `ScanLayer` / `Cell` / `BlockInfo` / `EntityInfo` / `PlayerPos`
  (one 2-D array per Y level; each cell holds the block and any entities in it)
- New example `region-scan`: `/rscan` selection, animated particle outline, live scan
- Bump `LEVI_RS_ABI_VERSION` to 3 (additive; loader **and** mods must be rebuilt)

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
