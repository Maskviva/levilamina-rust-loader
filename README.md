# levilamina-rust-loader

![levilamina-rust-loader](https://socialify.git.ci/Maskviva/levilamina-rust-loader/image?description=1&font=Raleway&forks=1&issues=1&language=1&logo=https%3A%2F%2Fraw.githubusercontent.com%2FLiteLDev%2FLeviLamina%2Frefs%2Fheads%2Fmain%2Fdocs%2Fmain%2Fcontents%2Flogo.svg&name=1&owner=1&pattern=Circuit+Board&pulls=1&stargazers=1&theme=Auto)

![English](https://img.shields.io/badge/English-inactive?style=for-the-badge)
[![中文](https://img.shields.io/badge/简体中文-informational?style=for-the-badge)](README.zh.md)

**Teach [LeviLamina](https://github.com/LiteLDev/LeviLamina) to load Minecraft Bedrock Dedicated Server mods written in Rust.**

This repository is the *engine*: a C++ loader mod plus the two Rust crates
that speak its ABI. Install it once on your server, then any Rust mod is just
a `cargo build` away — no C++ toolchain, no xmake, no glue code on the mod
author's side.

Looking to **write** a mod instead of build the loader? Start from
[**levilamina-mod-template-rs**](https://github.com/Maskviva/levilamina-mod-template-rs)
and come back here only if you're curious how it works or want to contribute.

## How it works

```
┌─────────────────────────── bedrock_server_mod.exe ───────────────────────────┐
│  LeviLamina (C++)                                                            │
│    └─ levilamina-rust-loader (this repo, C++, install once)                  │
│         • registers a ModManager for manifest `"type": "rust"`               │
│         • loads your cdylib, calls `levi_rs_main(api, handle, out_vtable)`   │
│         • hands over a versioned C function table (LeviRsApi)                │
│              ├─ events   : EventBus + DynamicListener ⇆ SNBT strings         │
│              ├─ commands : RuntimeCommand overloads / console execution      │
│              ├─ schedule : ServerThreadExecutor (thread-safe entry point)    │
│              └─ logging  : per-mod LeviLamina logger                         │
│    └─ your-mod (pure Rust cdylib, `"type": "rust"`)                          │
└───────────────────────────────────────────────────────────────────────────────┘
```

Design highlights:

- **First-class mods.** Rust mods live in `mods/<name>/` with a normal
  `manifest.json`, participate in dependency sorting, and show up in
  LeviLamina's mod listing like any other mod — because they *are* managed by
  a real `ModManager`, not injected through a side door.
- **One universal event channel.** LeviLamina's `DynamicListener` serializes
  any event to a `CompoundTag`; the bridge round-trips it as SNBT, so Rust can
  observe *and mutate/cancel* every event — including events published by
  other mods — without per-event C++ bindings. Run `/levirs events` in-game to
  dump every event id available on your server.
- **Honest threading model.** Every callback runs on the server thread.
  `Server::schedule{,_after}` are the only thread-safe entry points — exactly
  the bridge you need for Tokio-based agents and services.
- **Versioned, append-only C ABI** (`src/LeviRsAbi.h` is the single source of
  truth, mirrored field-for-field by `crates/levilamina-sys`). The loader and
  a mod refuse to pair on a version mismatch instead of risking UB.
- **Panic-safe.** Every FFI boundary in the `levilamina` crate is wrapped in
  `catch_unwind`; a panicking handler logs an error instead of taking down
  the server.
- **Unified memory allocator.** `src/MemoryOperators.cpp` routes this DLL's
  `operator new`/`delete` through LeviLamina's allocator, same as any native
  mod — required for the loader to be loaded at all.

## Repository layout

```
src/                    C++ loader mod (compiles to levilamina-rust-loader.dll)
crates/levilamina-sys/  Raw #[repr(C)] FFI mirror of src/LeviRsAbi.h
crates/levilamina/      Safe, ergonomic Rust API built on levilamina-sys
docs/DESIGN.md          ABI + architecture decisions, evolution rules
docs/PORTING_NOTES.md   LeviLamina API call sites this bridge relies on
xmake.lua               Builds the C++ mod (repo root doubles as the xmake project root)
Cargo.toml              Workspace for the two Rust crates (repo root doubles as the cargo workspace root)
```

`crates/levilamina-sys` and `crates/levilamina` are published from this repo
(not the template repo) because they are versioned in lockstep with the C++
loader — see [`docs/DESIGN.md`](docs/DESIGN.md) §8 for the ABI evolution
rules. The C header and the `-sys` crate change in the same commit, always.

## Installation (server admins)

1. Install [LeviLamina](https://lamina.levimc.org/) on your BDS.
2. Drop a `levilamina-rust-loader` release into `mods/levilamina-rust-loader/`
   (or build it yourself, see below).
3. Install Rust mods into `mods/<mod-name>/` — each one is a `.dll` +
   `manifest.json`, same as any other LeviLamina mod.

## Building the loader

Requires [xmake](https://xmake.io) and a Visual Studio 2022 (or clang-cl)
toolchain, same as any LeviLamina native mod. **Pin `xmake.lua`'s
`levilamina`/`bedrockdata`/`prelink` versions to what's actually running on
your server before building** — an unpinned `levilamina` can silently
resolve to a newer SDK than your server's LeviLamina build, which shows up as
confusing compile errors (missing members, changed signatures) rather than a
clear version-mismatch message.

```shell
xmake f -m release -y
xmake
```

The built mod (DLL + manifest) lands in `bin/`.

To also build/test the Rust crates in this workspace:

```shell
cargo build --workspace
cargo test --workspace
```

## Writing a mod against this loader

Most people should start from the
[template repository](https://github.com/Maskviva/levilamina-mod-template-rs)
instead of wiring this up by hand. If you're adding `levilamina` to an
existing crate:

```toml
# Cargo.toml
[lib]
crate-type = ["cdylib"]

[dependencies]
levilamina = { version = "0.1.0", git = "https://github.com/Maskviva/levilamina-rust-loader" }
# once published to crates.io: levilamina = "0.1"
```

```jsonc
// manifest.json — goes next to your .dll in mods/<name>/
{
    "name": "my-mod",                       // must match the folder name
    "entry": "my_mod.dll",                  // cargo output (hyphens → underscores)
    "type": "rust",
    "platform": "server",
    "dependencies": [{ "name": "levilamina-rust-loader" }]  // guarantees load order
}
```

## ABI stability

`LEVI_RS_ABI_VERSION` gates compatibility. Within a major version, `LeviRsApi`
only grows (new fields appended at the end, gated by `struct_size`); it never
reorders or removes fields. See [`docs/DESIGN.md`](docs/DESIGN.md) for the
full contract, including the threading model and string/memory ownership
rules that every FFI call follows.

## Current status & roadmap

v0.1 is intentionally a **narrow, correct core** — the universal primitives
(events / commands / scheduling / logging) from which everything else can be
built, since `execute_command` already reaches most of vanilla behaviour.

- [ ] **v0.2** — structured SNBT: typed event views (`serde`-based), proper
      NBT editing instead of string-level `cancel()`
- [ ] **v0.2** — direct world access fast path (`get_block` / `set_block` /
      region snapshots) without the command parser overhead
- [ ] **v0.3** — player handles (send message/toast/form), form API
- [ ] **v0.3** — Linux support (LeviLamina's Linux target is in progress
      upstream)
- [ ] **v0.x** — async-first API surface (`ServerHandle::run(async fn)` on top
      of the scheduler), Tokio integration example, AI-agent example
- [ ] procedural macro sugar: `#[levilamina::event]`, `#[levilamina::command]`

Contributions welcome — the ABI evolution rules are documented in
[`docs/DESIGN.md`](docs/DESIGN.md).

## FAQ

**Why a C++ shim instead of pure Rust against LeviLamina?**
LeviLamina's mod entry is plain C (`ll_mod_load` etc.), but everything
useful — events, commands, coroutine executors — is modern C++ (templates,
`std::function`, C++20 coroutines) with no C export layer. The loader also
enforces a unified memory-allocation contract that only code compiled against
its headers can honestly satisfy. A thin, audited C++ layer that flattens
those APIs into a versioned C table is the correct engineering answer; "pure
Rust" would mean hand-written FFI against MSVC-mangled symbols — a
re-implementation of LeviLamina itself.

**Does this work on the client?** Untested; the loader targets
`"platform": "server"`. LeviLamina's client support is newer territory.

**Where's the mod template?**
[`levilamina-mod-template-rs`](https://github.com/Maskviva/levilamina-mod-template-rs) —
kept separate so "start a new mod" stays a one-click GitHub template, instead
of forking this whole engine repo.

**License:** Apache-2.0 (this repository). LeviLamina itself is LGPL-3.0; the
loader mod links against it dynamically as a normal mod.

---

*Not affiliated with Mojang, Microsoft, or LeviMC. Minecraft is a trademark of
Mojang Synergies AB.*
