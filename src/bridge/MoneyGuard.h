/**
 * bridge/MoneyGuard.h — makes the LLMoney backend optional.
 *
 * The money API is backed by LegacyMoney's exported `LLMoney_*` functions.
 * Those live in `LegacyMoney.dll`, which is **delay-loaded** (see xmake.lua
 * `/DELAYLOAD:LegacyMoney.dll`): the loader DLL now starts fine even when
 * LegacyMoney isn't installed, and each `LLMoney_*` symbol is only resolved
 * on first use.
 *
 * moneyBackendReady() performs the DUAL verification the API contract wants
 * before any money call is dispatched:
 *
 *   1. Mod-list check — ll::mod::ModManagerRegistry sees a mod named
 *      "LegacyMoney" AND it's in the Enabled state. A present-but-disabled
 *      LegacyMoney would still export symbols, but calling into a disabled
 *      mod is a logic error, so we treat that as "not ready" too.
 *
 *   2. Symbol check — ll::memory::SymbolView::resolve() actually finds the
 *      `LLMoney_Get` export. This catches the pathological cases the mod
 *      list can't: a stale/renamed DLL, a version whose exports changed, or
 *      a delay-load stub that never got a real target. We probe one stable
 *      symbol (`LLMoney_Get`) once and cache the result — if the core getter
 *      is missing the whole family is unusable.
 *
 * On the first failure we warn ONCE (per process) with an actionable
 * message, then every money entry point returns a safe default. We never
 * throw across the C ABI and never let a delay-load failure hard-crash BDS.
 */
#pragma once

#include <string_view>

namespace levi_rs::bridge
{
    /**
     * True when both checks above pass. Cheap after the first call: the mod
     * that owns the money symbols can't be hot-swapped mid-session, so the
     * symbol probe is memoized. The (much cheaper) mod-list/state check runs
     * every time so that disabling LegacyMoney at runtime is reflected.
     *
     * Emits the "install/enable LegacyMoney" warning at most once per
     * process, the first time a check fails. Must be called on the server
     * thread (it touches the mod registry). Never throws.
     */
    bool moneyBackendReady() noexcept;
} // namespace levi_rs::bridge
