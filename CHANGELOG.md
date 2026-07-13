# Changelog

## 26.20.0

### Added (additive, `struct_size`-gated — ABI stays v5; table slot 94)

- **`Player::tell(msg, MessageType)`** — send a message of a specific
  `TextPacketType`, the equivalent of LSE's `player.tell(msg, type)`. New ABI
  entry `player_send_message_typed` builds a `TextPacket` with a `MessageOnly`
  body of the requested type and sends it to the player's connection; the new
  `MessageType` enum exposes `Raw`/`Chat`/`Translate`/`Popup`/`JukeboxPopup`/
  `Tip`/`SystemMessage`/`Whisper`/`Announcement`/`TextObject*`. Out-of-range
  falls back to Raw. `Player::send_message` stays the plain Raw/Chat path.
  (Verified against 26.20.0: `TextPacketPayload::MessageOnly{mType,mMessage}`
  and the `mBody` variant are present and unguarded.)

### Fixed

- **[build] Contributor-reported compile/link breakage after a local rename.**
  A local edit renamed the `runConsoleCommand` *definition* to `executeCommand`
  without updating its declaration in `Common.h` or its 13 call sites → LNK2001
  (unresolved `runConsoleCommand`) plus a knock-on C2447 in `Common.cpp`.
  Reverted the definition to `runConsoleCommand` (one site vs fourteen; also
  avoids colliding with `CommandRegistrar::executeCommand`). The companion
  `WorldInfo.cpp` include-path fix (`mc/platform/UUID.h`) was already applied
  by the contributor.

## v1.9.4

### Fixed

- **[build] `HopperEvents.cpp` C3535/C2440**: `NativeMod::current()` returns
  `std::shared_ptr<NativeMod>`, not a raw pointer, so the discriminator log's
  `auto* self = …` failed to deduce. Hold it by value (`auto self`); `operator
  bool` and `->` both work.

## v1.9.3

### Fixed

- **[P0 crash] HopperTransferEvent detour did virtual dispatch on the wrong
  class.** `Container::setItem` has a trivial body, so MSVC ICF folds it with
  chest/barrel/furnace/dropper's same-shaped `setItem` onto one address;
  hooking it entered the detour for those actors too, and a virtual call
  through their `this` read a vptr at the Hopper Container-subobject offset →
  garbage → DEP jump → crash ~30 s after any mod subscribed. Fix: a type guard
  BEFORE any virtual dispatch — `getType() != BlockActorType::Hopper` bails
  out. `getType()` is `MCFOLD` (non-virtual), reads `BlockActor::mType` at the
  primary base (offset 0), defined once on `BlockActor` (no overrides), so it's
  safe on any block actor even if itself folded. The before-state read uses the
  non-virtual `$getItem`; slot is bounds-checked. Includes a one-shot
  discriminator log to distinguish ICF folding (fixed) from a this-thunk
  mismatch (would leave counters empty → change hook target).

### Changed

- **[P1] `SpawningMobEvent` (and any event) now decodes an embedded
  `ActorDefinitionIdentifier`.** LL's generic reflection emits the mob
  identifier as a bare-pointer stub, so mods couldn't read the mob type. The
  event-enrichment pass is now a single `enrichEventData` that, on one copy,
  splices in both `_player` (as before) and `_identifier`
  `{full,namespace,name}` (e.g. `minecraft:zombie`). Fields are
  `TypedStorage<string>` object wrappers read via `.get()`; the pointer is
  sanity-gated (non-null, aligned) and only its string fields are read — no
  virtual calls (the HopperEvents lesson). `enrichWithPlayer` stays a thin
  alias.

### Not doing (with rationale)

- **[P3] Host CPU/RAM accessor** for `/td sys`: left to the mod side. It's
  platform syscalls (`GlobalMemoryStatusEx`/PDH on Windows), which would drag
  `psapi`/`pdh` link deps into the clean bridge build and fork per-OS. A
  mod-side `sysinfo` crate (pure Rust, cross-platform) is the better home;
  LL's `SystemUtils` exposes no memory/CPU (confirmed on 26.20.0).

## v1.9.2

### Fixed

- **[build] `SimPlayer.cpp` C2027 / C2668.** `api_sim_list` dereferenced
  `Level*` (`level->forEachPlayer`) without including `Level.h` (only a forward
  decl was in scope) — the v1.8 `sim_list` addition introduced the dereference.
  Added `mc/world/level/Level.h`. And `simulateLookAt(Vec3{…})` was ambiguous
  between the `(Vec3&)` and `(Vec3&, LookDuration = Instant)` overloads — pinned
  by passing `sim::LookDuration::Instant`. Swept the other `simulate*` calls;
  the rest disambiguate by parameter type/arity.

## v1.9.1

### Changed

- **`world` and `server` are now `pub mod`** (were private, reachable only via
  the crate-root re-exports). Makes `levilamina::world::VillageInfo` /
  `levilamina::server::Server` work, not just the root paths. Root re-exports
  unchanged, so existing imports keep working — this only *adds* reachable
  paths. Verified no private type (`error`/`ffi`/`logger` internals) leaks into
  either module's public signatures.

## v1.9

### Added (additive, `struct_size`-gated — ABI stays v5; table slots 92–93)

- **Read-only world data** (ROADMAP §5), new `src/bridge/WorldInfo.cpp`:
  - `villages` → `Server::villages(dim) -> Vec<VillageInfo>`: walks the
    dimension's `VillageManager::mVillages`, emitting `{uuid, center, bounds,
    poi_count}` per village. Unblocks `/village`.
  - `structures_near` → `Server::structures_near(dim, x, y, z, radius) ->
    Vec<StructureInfo>`: reads `LevelChunk::mSpawningAreas` (hardcoded spawn
    areas: nether fortress / witch hut / ocean monument / pillager outpost) for
    the loaded chunks intersecting the radius, emitting `{type, bounds}`. Loaded
    chunks only — a read-only query never force-loads. Unblocks `/hsa`.
  - New typed structs `VillageInfo` / `StructureInfo` / `Bounds` in `world`.
    Villager enumeration deliberately omitted (POI weak_ptr arrays keyed by
    role — fragile/version-sensitive; POI count is the stable signal).

## v1.8

### Added (additive, `struct_size`-gated — ABI stays v5; table slots 90–91)

- **Re-acquire simulated players by name**, closing the handle-lifetime gap in
  the v1.7 SimPlayer landing (a bot persists across a restart, but the spawn
  handle didn't — nothing could re-drive or see it):
  - `sim_is` → `Server::is_simulated(name) -> bool` (same `isSimulatedPlayer()`
    check `sim_do` gates on).
  - `sim_list` → `Server::list_sim_players() -> Vec<SimPlayer>` (filters the
    existing `forEachPlayer` enumeration to bots).
  - `SimPlayer::by_name` is now `pub`; `Server::sim_player(name)` rebuilds a
    handle. So `/self list` and post-restart control work without the mod
    caching handles.

## v1.7

### Added (additive, `struct_size`-gated — ABI stays v5; table slots 86–89)

- **Per-subsystem profiler** (ROADMAP §3): `profile_begin` / `profile_take`,
  exposed as `Server::begin_profile(ticks)` / `take_profile() ->
  Option<NbtValue>`. Five timing detours (Level/Dimension tick, redstone, chunk
  block ticks, block entities) share the tick-hook lifecycle and coexist with
  tick control on `Level::$tick` at a higher hook priority so each executed tick
  is measured once. Report buckets are inclusive wall times (nested
  subsystems) — presented side by side, not summed.
- **Simulated players** (ROADMAP §7): `sim_spawn` + `sim_do`, exposed as
  `Server::spawn_sim_player(...) -> SimPlayer` and the `SimPlayer` verb methods
  (move/navigate/look/mine/place/attack/use/drop/sneak/fly/chat/…). The bot is
  a real `ServerPlayer`, so the whole per-player API works on it via
  `SimPlayer::player()`. Verbs are multiplexed over one ABI entry (`sim_do`
  takes an action string + SNBT args) — new verbs need no ABI bump; gated on
  `isSimulatedPlayer()` so a real player can't be puppeted.

### Refactored

- **`src/bridge/Hooks.cpp` → `src/bridge/hooks/`**: one concern per TU —
  `HookEvents.{h,cpp}` (registry + dispatch + ABI plumbing), `TickControl.cpp`,
  `HopperEvents.cpp`, `DestroyEvents.cpp`, `Profiler.cpp`. Hook events
  self-register via a file-scope `HookEventRegistrar`, so adding one is a single
  new TU that touches no shared header, `Events.cpp`, or table.

## v1.6

### Added (additive, `struct_size`-gated — ABI stays v5; table slots 82–85)

The curated hook surface (ROADMAP §12), first slice — the bridge owns every
native detour once; mods only ever see a safe control API or an ordinary
`subscribe_event` id.

- **Per-connection packet delivery** (`src/bridge/Packets.cpp`), two layers
  sharing one delivery helper:
  - `send_packet` (slot 82) — raw primitive: any `MinecraftPacketIds` + a
    wire-format body, deserialised (`MinecraftPackets::createPacket` +
    `Packet::read`, rejecting parse failures / trailing bytes) and sent to one
    player (`Player::sendNetworkPacket`). Exposed as
    `Player::send_packet(packet_id, body)`.
  - (`spawn_particle_for`, the typed derivation, shipped in v1.5.)
- **Tick control** (slots 83–85): `tick_freeze` / `tick_step` / `tick_warp`,
  exposed as `Server::set_tick_freeze` / `step_ticks` / `set_tick_warp`. One
  detour on `Level::$tick`, installed lazily on first control call, never
  unpatched (a control call can arrive from a command handler executing inside
  the tick). Freeze stops mobs/blocks/redstone/time; warp supports fractional
  slow-motion via an accumulator. Unblocks `/tick` (ROADMAP §2).
- **Bridge-hook events** (0 new ABI slots): synthetic ids matched by name in
  `subscribe_event`, like the command events.
  - `HopperTransferEvent` (detour on `HopperBlockActor::$setItem`), payload
    `{x,y,z,slot,item,count,old_item,old_count}` — before/after stack so
    subscribers compute the flow delta. Unblocks `/counter` (ROADMAP §5).
  - `PlayerStartDestroyBlockEvent` (detour on `GameMode::startDestroyBlock`),
    dispatched *before* the destroy logic — the autotool timing LL's
    completion-time `PlayerDestroyBlockEvent` can't provide (ROADMAP §10).
  - Installed on the first subscriber (unused = zero cost); dispatch snapshots
    the subscriber list so callbacks may (un)subscribe during dispatch; mod
    unload detaches subscribers; `list_events` reports these ids.

### Fixed

- **Command-event subscription ordering.** `api_subscribe_event` called
  `resolveEventId` first, which relies on the dynamic registry; command events
  (`ExecutingCommandEvent` / `ExecutedCommandEvent`) are never registered there,
  so it returned early and the typed-listener code below was dead. Command
  events are now matched by name up front and routed straight to
  `emplaceListener`, bypassing `resolveEventId` + `DynamicListener`.

### Note on ROADMAP scope

Re-verified against 26.20.0: LeviLamina already ships the player/world/entity/
spawn events ROADMAP §4/§10 assumed missing (`PlayerDestroyBlockEvent`,
`PlayerInteractBlockEvent`, `PlayerPlaceBlockEvent`, `SpawningMobEvent` with
natural-spawn/pos/type payload, `BlockChangedEvent`, `ServerLevelTickEvent`, …).
They flow through the existing DynamicListener path — mods subscribe today, no
bridge work. The genuine gaps were tick *control*, hopper metering, the
start-destroy timing, and RNG (deferred: upstream's `Random::_genRandInt32`
no longer exists in 26.20.0; needs fresh research before detouring a hot path).

## v1.5

### Added (additive, `struct_size`-gated — ABI stays v5; table slot 81)

- **Per-player particle rendering** (ROADMAP §1): `spawn_particle_for`, exposed
  as `Server::spawn_particle_for(player, dim, effect, x, y, z)`. Sends a single
  `SpawnParticleEffectPacket` to one player's connection
  (`Player::sendNetworkPacket`) instead of `Level::spawnParticleEffect`'s
  dimension-wide broadcast — nobody else receives it. The first entry that
  talks to a single client connection directly, and the template the v1.6
  `send_packet` primitive generalised. Makes `/slime`-style outlines
  per-player. Mods rebuilt against the new `levilamina-sys` require this loader
  or newer (load-time `struct_size` check); older mods keep loading unchanged.

## v1.0.0

The "everything" release: ABI v5 grows the bridge from 18 to 80 entry points and
the safe crate from one file to a full module tree. All new fields are appended
(v1-v3 prefix byte-identical), but `LEVI_RS_ABI_VERSION` bumps to 4 - loader
**and** mods must be rebuilt together.

### Object model

Handles are identifiers, never pointers (re-resolved on every call):
`Player` = selector (name / xuid / uuid), `Entity` = `ActorUniqueID`,
`Block` = (dimension, position), `ItemStack` = pure SNBT value object,
`Container` = owner + which. Nothing a mod holds can dangle.

### New APIs (ABI v5)

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
