# Changelog

## Unreleased

### Added

- **`PlayerUseItemOnEvent`** (`bridge/hooks/UseItemOnEvent.cpp`) — a
  **cancellable** hook on `GameMode::useItemOn`, fired when a player is about to
  use the held item on a block.

  This is the funnel `PlayerInteractBlockEvent` is often mistaken for: spawn
  eggs, buckets, flint & steel, ender pearls and item-driven block placement all
  reach the world through `useItemOn`, so a land-protection mod that watches only
  interact/place events lets every one of them through. Cancelling returns an
  `InteractionResult` with both flags clear — nothing is placed or spawned and
  the item is not consumed.

  Payload: `{x, y, z, dim, face, item, isFirstEvent, _player}`. `x/y/z` are flat
  integers, matching the other hook events here. `isFirstEvent` is passed through
  because holding right-click re-fires this call many times per second on Windows
  clients, and subscribers that want to act once per click need to tell the first
  from the repeats.

### Changed

- **`enrichEventData` now puts the player's position into `_player`** as
  `{x, y, z}` named integers, alongside the existing name/xuid/uuid.

  Every event that carries a player now has a position that is readable without
  knowing the event's own field layout. That matters more than it sounds:
  `ll::reflection` serialises `BlockPos` / `Vec3` through the `IsVectorBase`
  overload, which builds a JSON **array**, so consumers that only understand
  `{x,y,z}` silently read nothing out of an event's own position field and —
  if they treat "no position" as "don't check" — fail open.

## 26.20.4

### Added (additive enum constants — ABI stays v5; no new function-pointer slots)

- **API surface parity with the C++ LL/MC headers.** The existing generic
  getters/setters/actions (`player_get_num`, `player_action`, `actor_get_str`,
  `block_get_num`, `item_transform`, `scoreboard_op`, …) now cover the full
  property set exposed by the C++ `LeviLamina` + Bedrock headers, by extending
  the ABI enum constants that select which property an call targets. No new
  `LeviRsApi` function-pointer slots were added — the new capabilities ride on
  the pre-existing generic dispatch, so **ABI stays v5** and older mods keep
  loading unchanged. New constants are append-only (existing values never
  renumbered), grouped by domain for browseability:
    - **[header/sys]** `LeviRsPlayerNumProp` / `LeviRsPlayerStrProp` /
      `LeviRsPlayerAction` — 69 constants covering game type, level, experience,
      abilities, inventory, coords, OP level, IP, device, skin, etc.
    - **[header/sys]** `LeviRsActorNumProp` / `LeviRsActorStrProp` /
      `LeviRsActorAction` — 83 constants covering position, health, attributes,
      family, name tag, NBT, kill/despawn/teleport/addEffect/rayTrace, etc.
    - **[header/sys]** `LeviRsBlockNumProp` / `LeviRsItemNumProp` / `LeviRsItemOp` /
      `LeviRsScoreboardOp` / `LeviRsSysProp` / `LeviRsServerProp` — 110 constants
      covering block states, item metadata, item transforms, scoreboard ops,
      system info, server info, etc.

### Added (additive — ABI v5, struct_size-gated function pointers)

- **34 dedicated gap-fill functions** appended to `LeviRsApi` as additive
  fields (older loaders with smaller `struct_size` simply don't see them;
  unknown enum keys return `false`). All implementations live in
  `src/bridge/GapFill.cpp`; stubs return safe defaults where the BDS API
  is not yet confirmed.
    - **Player** (7): `player_get_carried_item`, `player_get_item`,
      `player_set_item`, `player_get_equipment`, `player_get_cooldown`,
      `player_start_cooldown`, `player_get_network_status`.
    - **Actor** (13): `actor_get_vehicle`, `actor_get_first_passenger`,
      `actor_get_owner`, `actor_get_target`, `actor_get_equipped_item`,
      `actor_set_equipped_item`, `actor_get_effects`,
      `actor_get_status_flag`, `actor_set_status_flag`, `actor_trace_ray`,
      `actor_distance_to`, `actor_get_aabb`, `actor_clone`.
    - **Block** (3): `block_get_state`, `block_set_state`,
      `block_get_collision_shape`.
    - **Item** (4): `item_get_enchants`, `item_set_enchants`,
      `item_matches`, `item_get_user_data`.
    - **Level** (7): `level_get_biome`, `level_get_default_spawn`,
      `level_set_default_spawn`, `level_save`, `level_get_sleep_status`,
      `level_update_weather`, `level_find_path`.
- **Rust safe wrappers** for all 34 gap-fill functions, in 5 new files
  (each ≤200 lines): `player/gap_fill.rs`, `entity/gap_fill.rs`,
  `block/gap_fill.rs`, `item/gap_fill.rs`, `server/world/gap_fill.rs`.
- **NBT binary codec safe wrappers** (`nbt/binary.rs`): `NbtValue::to_binary`
  and `NbtValue::from_binary` now expose the engine's `CompoundTag`
  binary/network NBT codec to Rust mods (previously the FFI functions
  `nbt_snbt_to_binary` / `nbt_binary_to_snbt` had no safe wrapper).
  New `NbtBinaryFormat` enum (`Disk` / `Network`).
- **C++ bridge fixes**: corrected function signatures for `Actor::burn`,
  `Actor::stopFire` (replaces non-existent `extinguishFire`),
  `Actor::removeAllPassengers`, `Player::getNetworkStatus` (handles
  `std::optional<NetworkPeer::NetworkStatus>`), `Block::getCollisionShape`,
  `BlockSource::tryGetBiome`, and `PlayerSleepStatus` field access.

### Changed (internal — no API/ABI impact)

- **[sys crate] `levilamina-sys` split into per-domain files (≤200 lines each).**
  The crate was a single 744-line `lib.rs`; it is now organised so other
  developers can find an API by browsing, not grepping:
    - `lib.rs` (29 lines) — crate root, re-exports.
    - `api.rs` (165 lines) — the `LeviRsApi` function-pointer table.
    - `types.rs` (90 lines) — `#[repr(C)]` FFI types (`LeviRsStr`, `LeviRsPlayerSel`, …).
    - `vtable.rs` / `money.rs` — mod vtable + LLMoney callback types.
    - `consts/{player,actor,world}.rs` — enum constants split by domain.
      Also removed a stale `LLMoneyEvent` import from `api.rs` (unused warning).
- **[safe crate] `levilamina` source files split to ≤200 lines each.** Same
  findability goal; no public API changed (all re-exports at the crate root and
  in `prelude` are byte-identical):
    - `lib.rs` (279→112) — extracted `runtime.rs` (Runtime/ModContext) and
      `registration.rs` (LeviMod/ModSlot/__init_runtime/register_mod!).
    - `nbt/mod.rs` (270→62) — extracted `accessors.rs` (get/path/as_*),
      `serde.rs` (SNBT writer + tests), and split `parser.rs` (267) into
      `parser/{mod,containers,scalars}.rs`.
    - `server/world.rs` (214→7+75+115+52) — split into
      `server/world/{mod,particles,blocks,entities}.rs`.

### Added (client dual-target support — ABI stays v5)

- **Dual-endpoint build: server + client.** The loader now builds in two
  flavours via `xmake f --target_type=client|server` (default `server`):
    - **Server** (`levilamina-rust-loader.dll`): unchanged — targets BDS,
      links `legacymoney`, includes all server-only bridge files
      (Commands, Server, Money, SimPlayer, ScoreboardApi, Forms,
      WorldInfo, Packets, GapFill, hooks/*).
    - **Client** (`levilamina-rust-loader-client.dll`): targets the MC
      Bedrock client via LeviLamina's `src-client` API (`target_type=client`
      levilamina package). Server-only source files are excluded; their
      `api_*` slots are filled by `src/bridge/ClientStubs.cpp` (no-op
      stubs returning `false`/`0`/`nullptr` — the Rust safe layer maps
      these to `Err("unsupported on client")`).
- **Client-only FFI types and function pointers** (6 slots, appended after
  the gap-fill block in `LeviRsApi`, `#ifdef LEVI_RS_TARGET_CLIENT`):
  `client_get_local_player`, `client_is_in_level`,
  `client_get_screen_name`, `client_register_key`,
  `client_unregister_key`, `client_get_key_codes`. Implemented in
  `src/bridge/Client.cpp` via `ll::input::KeyRegistry` and
  `ll::service::getClientInstance()`. All callbacks run on the **client
  thread**. The server build's `struct_size` stops before this block, so
  a server mod never sees these slots.
- **Rust SDK dual-feature gates.** `levilamina` and `levilamina-sys` crates
  now have mutually exclusive `server` / `client` features (default
  `server`). The `client` feature compiles the client FFI types
  (`LeviRsKeyHandle`, `LeviRsKeyCb`, …) and the safe `client` module
  (`Client`, `ClientInstance`, `KeyBinding`, `KeyAction`, client event
  constants). Server-only modules (`command`, `gui`, `money`,
  `scoreboard`, `server`, `sim`) are `#[cfg(feature = "server")]`-gated.
  A `compile_error!` enforces exactly-one-feature selection.
- **Thread executor adaptation.** `LogScheduler.cpp` uses
  `ClientThreadExecutor` on client builds (via `LEVI_RS_THREAD_EXEC`
  macro) instead of `ServerThreadExecutor`. `Common.cpp` uses
  `ll::service::getMultiPlayerLevel()` on client instead of `getLevel()`.
  `Events.cpp` skips server-only command events
  (`ExecutingCommandEvent` / `ExecutedCommandEvent`). `Entry.cpp` skips
  the `/levirs` debug command registration on client.

### Added (MoreDimensions — always on for server builds; ABI stays v5)

- **Custom dimensions reimplemented inline** (a Rust port of
  [LiteLDev/MoreDimensions](https://github.com/LiteLDev/MoreDimensions)),
  compiled into every server build unconditionally — no `xmake` flag is
  required (or accepted). Client builds never include it. Gated on the
  C++ side by `LEVI_RS_FEATURE_MORE_DIMENSIONS` (always defined for
  `target_type=server`) and on the Rust side by the `more_dimensions`
  cargo feature; the loader always exposes the API, and the Rust mod
  decides at runtime whether to call it.
    - **C++ side** (`src/more_dimensions/`): `SimpleCustomDimension`
      (a `Dimension` subclass supporting 5 generator types — Overworld,
      Nether, TheEnd, Flat, Void), `CustomDimensionManager` (registration +
      id assignment, persisted to `configs/levilamina-rust-loader/dimensions.json`
      for stable ids across restarts), `FakeDimensionId` / `FakeDimensionHooks`
      (rewrites the dimension id in `ChangeDimensionPacket` and related packets
      to a fake vanilla id on the wire, so the Bedrock client — which only knows
      ids 0/1/2 — renders the custom world without errors), and `MoreDimensionsBridge`
      (3 FFI functions exposing the feature to Rust).
    - **ABI** (additive, struct_size-gated, `#ifdef LEVI_RS_FEATURE_MORE_DIMENSIONS`):
      3 new `LeviRsApi` slots — `md_is_available`, `md_add_simple_dimension`,
      `md_get_dimension_id`. Appended after the client block; a loader built
      without the feature has a smaller `struct_size`, so a mod that references
      them simply gets `Err` instead of crashing.
    - **Rust safe layer** (`crates/levilamina/src/more_dimensions.rs`): a
      `more_dimensions` module gated by `#[cfg(all(feature = "server",
      feature = "more_dimensions"))]`, exposing `GeneratorType` (enum, repr i32),
      `is_available()`, `add_simple_dimension(name, seed, generator) -> Result<i32>`
      (returns the assigned id ≥ 3), and `get_dimension_id(name) -> Option<i32>`.
      `GeneratorType` is re-exported in `prelude` when the feature is on.
    - **Feature guard**: `levilamina-sys` emits a `compile_error!` if
      `more_dimensions` is combined with `client`, since the C++ macro is
      never defined for client builds (would cause a `LeviRsApi` struct_size
      / ABI mismatch).

## 26.20.1

### Changed

- **ABI version acceptance is now a range, restoring backward compatibility.**
  The loader previously required `mod_abi == LEVI_RS_ABI_VERSION` (strict
  equality), so a mod built against *any* other version — including an older,
  fully additive-compatible one — was rejected at load. Because every ABI bump
  in this project's history is additive (fields appended, never
  reordered/removed), a newer loader can safely run an older mod: the mod calls
  a byte-identical prefix of the loader's larger table.
    - **[loader]** `RustModManager::load` now accepts any mod whose
      `abi_version` is in `[LEVI_RS_ABI_MIN_SUPPORTED, LEVI_RS_ABI_VERSION]`.
      Too-new mods (built against a version the loader doesn't have) and
      below-floor mods (predating a hypothetical non-additive break) are still
      refused, each with a message pointing at the right fix (update the loader
      vs rebuild the mod). A version skew that's accepted logs an info line.
    - **[header]** New `LEVI_RS_ABI_MIN_SUPPORTED` (currently `1`) in
      `src/LeviRsAbi.h` — the single knob to raise if a future major version
      ever breaks layout non-additively.
    - **[safe crate]** `__init_runtime` relaxed symmetrically: a mod now accepts
      any loader whose `abi_version` is `>=` its own (an additive superset),
      with the pre-existing `struct_size` check remaining the precise
      forward-compat gate. Previously a v(N) mod would reject a compatible
      v(N+1) loader.

- **LLMoney / LegacyMoney is now an OPTIONAL runtime dependency.**
    - **[build]** `LegacyMoney.dll` is now **delay-loaded**
      (`/DELAYLOAD:LegacyMoney.dll` + `delayimp`), so the loader starts normally
      without it and the `LLMoney_*` thunks resolve lazily on first use.
        - **[bridge]** New `src/bridge/MoneyGuard.{h,cpp}` gates every money entry
          point behind a **dual check**: (1) `ModManagerRegistry` reports an
          *enabled* mod named `LegacyMoney`, and (2) the `LLMoney_Get` export
          actually resolves via `ll::memory::SymbolView::resolve`. The symbol probe
          is memoized; the mod-state check runs each call so disabling LegacyMoney at
          runtime is honored. On the first failure the loader logs a single
          actionable warning ("请检查是否安装并启用了 LegacyMoney") and every money
          call returns a safe default (`get_money` → 0, mutators → false, listeners →
          no-op). No exception ever crosses the C ABI.
        - **[safe crate]** `levilamina::money` docs updated: a failing `Err` on a
          server without LegacyMoney is expected, not a bug.

### Fixed

- **[tools] `check_abi_sync.py` false positive.** The struct-body scanner
  matched the nested `typedef bool (*LLMoneyCallback)(...)` as a phantom
  v-table field, shifting every subsequent field by one and reporting a
  spurious ABI break. It now strips nested `enum class` / `typedef`
  declarations before extracting fields. All three definitions verify in sync
  (104 fields).

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