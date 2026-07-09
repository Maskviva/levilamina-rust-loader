# Porting / First-Build Notes

This code was written by reading the LeviLamina source directly, but it has
**not** been compiled against LeviLamina yet (the authoring environment had no
Windows/MSVC/xmake toolchain). Everything below was checked against the headers;
this is the short list to confirm on your first `xmake` build, with file/line
references so you can re-check quickly if a symbol moved between LL versions.

## Verified against source (should be correct as written)

- **Custom mod type is legal.** `ModManagerRegistry::addManager()` is public
  LLAPI; `ModManagerRegistry::loadMod` resolves the manager by `manifest.type`
  at dispatch time; `ModRegistrar` topologically sorts by `dependencies`. A
  rust mod depending on `levilamina-rust-loader` therefore loads after the
  manager exists. (`ll/api/mod/ModManagerRegistry.{h,cpp}`, `ll/core/mod/ModRegistrar.cpp`)
- **Native entry contract.** Loader must export `ll_memory_operator_overrided`
  (comes for free from LL headers) and the `ll_mod_*` symbols via
  `LL_REGISTER_MOD`. (`ll/core/mod/NativeModManager.cpp:104`, `ll/api/mod/RegisterHelper.h`)
- **DynamicListener** wraps `std::function<void(CompoundTag&)>` and
  serialize→callback→deserialize round-trips the event, so mutation/cancel
  works. `Cancellable::serialize` emits the cancelled flag.
  (`ll/api/event/DynamicListener.h`, `ll/api/event/Cancellable.h`)
- **EventBus**: `addListener(ListenerPtr, EventIdView)`,
  `removeListener(ListenerPtr)`, `events()` enumerator, `hasEvent`.
  (`ll/api/event/EventBus.h`)
- **SNBT**: `Tag::toSnbt(SnbtFormat::Minimize)` and
  `CompoundTag::fromSnbt(sv) -> ll::Expected<CompoundTag>`. We `if (auto t = …)`
  and move out of it. (`mc/deps/nbt/Tag.h:15,83`, `mc/deps/nbt/CompoundTag.h:53`)
- **Runtime command**: `getOrCreateCommand(name, desc, perm)` →
  `runtimeOverload().optional("args", ParamKind::RawText).execute(Fn)` where
  `Fn = void(CommandOrigin const&, CommandOutput&, RuntimeCommand const&)`.
  Param read: `rt["args"].hold(ParamKind::RawText)` then
  `.get<ParamKind::RawText>().mText`. Enum index 11 (`RawText`) lines up
  with variant index 11 (`CommandRawText`), which has `mText`.
  (`ll/api/command/runtime/{RuntimeOverload,RuntimeCommand,ParamKind}.h`,
  `mc/server/commands/CommandRawText.h`)
- **execute_command**: `CommandRegistrar::getServerInstance().executeCommand(sv, origin)`;
  `ServerCommandOrigin(std::string const&, ServerLevel&, CommandPermissionLevel, DimensionType)`;
  `DimensionType` has an implicit `DimensionType(int)` ctor so `0` works.
  Output via `getMessages()[].getMessageId()` + `getSuccessCount()`.
  (`ll/api/command/CommandRegistrar.h:72`, `mc/server/commands/ServerCommandOrigin.h:35`,
  `mc/world/level/dimension/DimensionType.h`, `mc/server/commands/CommandOutput.h`)
- **Threading**: `ll::thread::ServerThreadExecutor::getDefault().execute(fn)` /
  `.executeAfter(fn, Duration)` where `Duration = steady_clock::duration`
  (so `std::chrono::milliseconds` converts implicitly).
  (`ll/api/thread/ServerThreadExecutor.h`, `ll/api/coro/Executor.h:22`)
- **EventPriority values are 0/100/200/300/400**, not 0..4 — the bridge maps
  the ABI's 0..4 onto these explicitly. (`ll/api/event/ListenerBase.h:14`)
- **Logger** has `fatal/error/warn/info/debug/trace`, both `fmt` and plain
  `std::string`/`IsString` overloads. We use the single-arg string overload.
  (`ll/api/io/Logger.h`)
- **Errors**: `ll::makeStringError(std::string)`,
  `ll::makeExceptionError(exception_ptr)`, and
  `ll::error_utils::printCurrentException(logger)` all exist with the
  signatures used. (`ll/api/Expected.h:107,119`, `ll/api/utils/ErrorUtils.h:45`)
- **`ll::mod::getModsRoot()`** is declared in `ll/api/mod/Mod.h:14`.

## Confirm on first build (most-likely friction points)

1. **Include paths / package layout.** `xmake` pulls LeviLamina headers from
   the `levilamina` package; make sure the `#include "mc/..."` and `#include
   "ll/..."` roots resolve. If your LL package version differs, a header may
   have moved — the file references above are your map.
2. **`CommandOutput::success(std::string)`** binds to the `string_view`
   overload (there's also a variadic `fmt` overload needing ≥1 arg). If your LL
   version made the plain overload `explicit`/ambiguous, wrap as
   `output.success(std::string_view{...})`.
3. **`getModsRoot()` visibility** from inside `RustModManager.cpp` — it's a
   free function in `namespace ll::mod`; we call it unqualified inside that
   namespace. If linkage complains, qualify as `ll::mod::getModsRoot()`.
4. **`runtimeOverload()` default mod arg.** It defaults to
   `NativeMod::current()`, which inside the loader DLL is the loader mod — this
   is intended (commands must outlive rust mods). No change needed, just be
   aware when reading logs.
5. **`set_exceptions("none")` + `/EHa`.** The official template uses `/EHa`;
   if xmake's `set_exceptions` conflicts with the raw `/EHa` cxflag on your
   version, drop one of them. Exceptions must be enabled (we use try/catch).
6. **Rust cdylib name.** `hello-mod` → `hello_mod.dll`. Ensure `entry` in each
   manifest matches the actual cargo output name on your platform.

## Known v0.1 simplifications (by design, see DESIGN.md)

- `EventRef::cancel()` does a textual `cancelled:0b → cancelled:1b` flip on the
  SNBT. It works because `Cancellable` serializes that exact field, but a
  structured NBT editor is the v0.2 plan.
- Every rust command is `/<name> [raw text]`. Typed params are roadmap.
- No direct world/actor pointers yet; `execute_command` covers most needs.

---

# ABI v3 additions (world reading)

v3 appends three functions to `LeviRsApi`: `spawn_particle`,
`get_player_position`, `scan_region`. All are server-thread-only. The Rust
mirror is in `crates/levilamina-sys/src/lib.rs`; the safe wrappers and the
layered `Scan` data model are in `crates/levilamina/src/lib.rs`.

**This bumps `LEVI_RS_ABI_VERSION` 2 → 3, so the loader AND every mod must be
rebuilt.** A v2 loader will refuse a v3 mod (and vice-versa) via the existing
`abi_version` / `struct_size` checks — that's intended, not a bug.

## Source-verified API mapping (all confirmed against LeviLamina-main)

- **Level handle**: `ll::service::getLevel()` (already used by the v2 stats fns).
- **Dimension**: `level->getDimension(DimensionType{d})` returns
  `WeakRef<Dimension>`; `.lock()` yields `StackRefResult<Dimension>`, which is
  `std::shared_ptr<Dimension>` underneath — hence `!dim`, `dim.get()`, and
  `dim->…` are all valid.
- **Block read**: `dim->getBlockSourceFromMainChunkSource()` →
  `BlockSource::getBlock(BlockPos{x,y,z})` → `Block::getTypeName()`
  (`std::string const&`) + `Block::getSerializationId()` (`CompoundTag const&`,
  serialized via `toSnbt(SnbtFormat::Minimize)`, which is `const noexcept`).
- **Entity enum**: `level->getRuntimeActorList()` (`std::vector<Actor*>`),
  filtered by `Actor::getPosition()` (`Vec3 const&`) and
  `Actor::getDimensionId()`; each serialized via `Actor::save(CompoundTag&)`
  (`const`) and named via `Actor::getTypeName()`.
- **Particle**: `Level::spawnParticleEffect(std::string const&, Vec3 const&,
  Dimension*)` — we pass `dim.get()`.
- **Player position**: `Level::forEachPlayer(std::function<bool(Player&)>)`;
  return `false` to stop iterating. `Player` inherits `getPosition()`,
  `getDimensionId()`, `getNameTag()` from `Actor`.

## Confirm on first build (v3-specific friction points)

1. **`WeakRef::lock()` / `StackRefResult`**. Confirmed `StackResultStorage =
   std::shared_ptr<T>` in `GameRefs.h`, so pointer-like use is correct. If your
   LL version changed this alias, adjust `dim.get()` / `dim->` accordingly.
2. **Block state serialization.** This LL version has **no**
   `Block::getSerializationId()` accessor (older versions did). The block's
   serialization CompoundTag (`{name, states, version}`) is the public member
   `Block::mSerializationId` (a `TypedStorage`), read via `.get()` — which is
   what `api_scan_region` does: `block.mSerializationId.get().toSnbt(...)`.
   `toSnbt` is `const noexcept` on `Tag`, so it's callable on the const ref.
   If a future version restores the accessor, either form works.
3. **`spawnParticleEffect` signature.** Takes `(std::string const&, Vec3,
   Dimension*)`. Some LL versions expose a MolangVariableMap overload — the
   3-arg one is what we bind. If ambiguous, name the effect string explicitly.
4. **`forEachPlayer` return semantics.** We assume `true` = keep going,
   `false` = stop (the LL convention). Verify if your version differs.
5. **Player-name match.** `get_player_position` compares the queried
   name against `Player::getRealName()` (the account name, LLNDAPI). If your
   setup should match on the *display* name instead (nametag plugins etc.),
   switch the one comparison in `api_get_player_position` to `Actor::getNameTag()`.
   Note: this SDK has no `Player::getName()` — older references to it are stale.
6. **`getRuntimeActorList()` cost.** It allocates a vector each call; a live
   scan calls it every refresh. Fine for interactive selections; the mod caps
   live auto-scan at 32³ cells and falls back to `/rscan collect` above that.

## The layered data model (`Scan`)

`Server::scan_region(dim, a, b)` returns a `Scan`:
- `Scan.layers` — one `ScanLayer` per Y level, bottom (`min.y`) to top.
- `ScanLayer.cells[dx][dz]` — a 2-D grid, indices are offsets from the min
  corner (`dx` west→east, `dz` north→south).
- `Cell { block: BlockInfo, entities: Vec<EntityInfo> }` — a cell can hold both
  a block and entities. `BlockInfo` is name + full state SNBT; `EntityInfo` is
  type + `save()` SNBT.

So a 6-tall selection yields exactly 6 `ScanLayer`s, each a 2-D array whose
every element describes all state at that grid cell. The `region-scan` example
(`examples/region-scan/`) drives the whole flow: `/rscan pos1|pos2` to select,
`/rscan show` for the animated particle outline + live scan, `/rscan collect`
for a one-shot per-layer report. The latest live scan is exposed via
`region_scan::latest_scan()` for downstream consumers (e.g. an AI agent).
