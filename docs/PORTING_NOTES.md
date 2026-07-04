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
