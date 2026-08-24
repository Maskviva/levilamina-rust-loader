/**
 * bridge/Services.cpp — cross-mod **service registry** (query-style calls).
 *
 * # Why this is not the bus
 *
 * The bus (Bus.cpp) is one-way broadcast: publish and move on, nobody has to be
 * listening, return values are meaningless. A query is the opposite shape on
 * every axis:
 *
 * | | bus | service |
 * |---|---|---|
 * | providers per name | any number | **exactly one** |
 * | nobody registered | normal | an error the caller must handle |
 * | return value | none | the whole point |
 * | ordering | undefined, must not matter | there is only one callee |
 *
 * Two providers answering `plot:can` is not "both get to run" — it is an
 * ambiguous answer, and the caller has no way to pick. So registration is
 * **exclusive**: the second registrar for a name is refused, loudly. A silent
 * last-wins would mean the answer depends on mod load order, which nobody
 * controls and which changes when an unrelated mod is installed.
 *
 * # Ownership: same discipline as everything asynchronous here
 *
 * `RustModManager::unload` calls `FreeLibrary`. A mod holding another mod's
 * function pointer is a crash waiting for the next call, and the crash lands in
 * the *caller*, with nothing in the log pointing at the mod that just left. So
 * the loader owns the table, entries are keyed by ticket, and the call path
 * holds a `weak_ptr<RustMod>` and revalidates immediately before it calls.
 * Forms.cpp, the mod-scoped scheduler and Bus.cpp all landed here already.
 *
 * # The loader does not parse anything
 *
 * `request` and `reply` are opaque UTF-8 the two mods agree on out of band.
 * The loader defining a schema would mean every provider has to satisfy it and
 * the loader has to version it — for zero benefit, since the loader never looks
 * inside. Same call as the bus payload.
 *
 * # Synchronous, on the caller's thread
 *
 * `service_call` runs the provider inline and returns its answer. There is no
 * timeout and there will not be one: a provider that blocks blocks the server
 * thread, exactly like any other callback, and pretending otherwise (by
 * returning "timed out" while the callback keeps running) would be worse than
 * the hang — it would hand the caller a wrong answer *and* leave the provider
 * running.
 *
 * # Loops
 *
 * A depth cap, same as the bus. A → B → A terminates instead of growing the
 * stack until the server dies. Self-calls are refused outright: a mod calling
 * its own service is going through two FFI hops and a mutex to reach a function
 * it can call directly, and when it *is* a loop it is the shape that produces
 * the least legible stack.
 */
#include "bridge/Api.h"
#include "bridge/Common.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ll/api/io/Logger.h"

#include "RustMod.h"

namespace levi_rs::bridge
{
    namespace
    {
        /** Longest accepted service name. Same reasoning as the bus topic cap:
         *  long enough for `some-long-mod:some-query`, short enough that a
         *  garbage pointer read as a string cannot become a huge map key. */
        constexpr size_t kMaxName = 128;

        /** Nested call limit. A → B → A is depth 2; anything past this is a
         *  loop, not a call chain. */
        constexpr int kMaxDepth = 8;

        struct Service
        {
            RustMod* mod = nullptr; // identity only; never dereferenced blind
            std::string name;
            LeviRsServiceCb cb = nullptr;
            void* user = nullptr;
        };

        std::mutex gMutex;
        /** registration id -> service */
        std::unordered_map<uint64_t, Service> gServices;
        /** name -> registration id. Exactly one, by construction. */
        std::unordered_map<std::string, uint64_t> gByName;
        uint64_t gNextId = 1;

        /** Per-thread nesting depth. Thread-local, not global: two threads
         *  calling concurrently are not a loop, and a shared counter would
         *  make them look like one. */
        thread_local int gDepth = 0;

        struct DepthGuard
        {
            DepthGuard() { ++gDepth; }
            ~DepthGuard() { --gDepth; }
        };

        /** Say "too deep" once per service, then stay quiet — a loop runs at
         *  CPU speed and logging every hit turns a bug into an outage. */
        void warnDepthOnce(std::string const& name)
        {
            static std::mutex mu;
            static std::unordered_map<std::string, bool> seen;
            std::lock_guard lock(mu);
            if (seen[name]) return;
            seen[name] = true;
            bridgeLogger().error(
                "service registry: '{}' exceeded call depth {} — refusing the innermost call. "
                "This is a call loop: the provider of this service calls it again, directly or "
                "through another service that leads back here.",
                name, kMaxDepth
            );
        }
    } // namespace

    uint64_t api_service_register(
        LeviRsModHandle modHandle, LeviRsStr nameRaw, LeviRsServiceCb cb, void* user)
    {
        auto* mod = asMod(modHandle);
        if (!mod || !cb) return 0;

        std::string name{std::string_view{nameRaw}};
        if (name.empty() || name.size() > kMaxName) return 0;

        std::lock_guard lock(gMutex);
        if (auto it = gByName.find(name); it != gByName.end())
        {
            // Refuse, and name the incumbent. "Registration failed" without
            // saying who already holds it sends the reader looking through
            // their own code for a double-register that is not there.
            auto const& held = gServices[it->second];
            char const* holder = held.mod ? held.mod->getName().c_str() : "?";
            mod->getLogger().error(
                "service_register('{}') refused: already provided by '{}'. Service names are "
                "exclusive — two providers would make the answer depend on mod load order.",
                name, holder
            );
            return 0;
        }

        uint64_t const id = gNextId++;
        gServices.emplace(id, Service{mod, name, cb, user});
        gByName.emplace(name, id);
        return id;
    }

    bool api_service_unregister(LeviRsModHandle modHandle, uint64_t regId)
    {
        auto* mod = asMod(modHandle);
        if (!mod || regId == 0) return false;

        std::lock_guard lock(gMutex);
        auto it = gServices.find(regId);
        if (it == gServices.end()) return false;
        // Scoped to the caller: one mod must not be able to unregister
        // another's service. Same rule as bus_unsubscribe and schedule_cancel.
        if (it->second.mod != mod) return false;
        gByName.erase(it->second.name);
        gServices.erase(it);
        return true;
    }

    int32_t api_service_call(
        LeviRsModHandle modHandle,
        LeviRsStr nameRaw,
        LeviRsStr requestRaw,
        void* ctx,
        LeviRsStrSink reply)
    {
        std::string name{std::string_view{nameRaw}};
        if (name.empty() || name.size() > kMaxName) return LEVI_RS_SERVICE_REFUSED;

        if (gDepth >= kMaxDepth)
        {
            warnDepthOnce(name);
            return LEVI_RS_SERVICE_REFUSED;
        }

        auto* caller = modHandle ? asMod(modHandle) : nullptr;

        // Copy the entry out under the lock; cross into the dylib with the lock
        // released. Providers routinely re-enter (they call other services,
        // publish on the bus, register forms), and holding the lock across a
        // call into another mod deadlocks the server thread on the first one
        // that does.
        Service svc;
        {
            std::lock_guard lock(gMutex);
            auto byName = gByName.find(name);
            if (byName == gByName.end()) return LEVI_RS_SERVICE_NOT_FOUND;
            auto it = gServices.find(byName->second);
            if (it == gServices.end()) return LEVI_RS_SERVICE_NOT_FOUND;
            svc = it->second;
        }
        if (!svc.cb || !svc.mod) return LEVI_RS_SERVICE_NOT_FOUND;
        if (caller && svc.mod == caller) return LEVI_RS_SERVICE_REFUSED; // no self-calls

        // Revalidate through weak_ptr immediately before the call: the provider
        // may have unloaded since the lookup, and the ticket table is only
        // cleaned up on the unload path.
        std::weak_ptr<RustMod> weakMod;
        try
        {
            weakMod = svc.mod->shared_from_this();
        }
        catch (...)
        {
            return LEVI_RS_SERVICE_NOT_FOUND;
        }
        auto provider = weakMod.lock();
        if (!provider || provider.get() != svc.mod) return LEVI_RS_SERVICE_NOT_FOUND;

        // # 这里**不能**查 `isEnabled()`
        //
        // 查过，而且和 Lane.cpp 是同一个坑：LeviLamina 的 `ModManager::enable()`
        // 先调 `onEnable` 回调、**回调返回之后**才把状态翻成 Enabled，而且整个
        // load 阶段所有 mod 都还没 enable。
        //
        // 后果是：一个 mod 在自己的 `on_load` 里调 `service::call` 去探测别的
        // mod，**必然**得到 NOT_FOUND —— 哪怕对方的服务早就注册好了。
        //
        // 而「服务注册在 on_load，好让消费方在自己的 on_load 里也能探测」正是
        // 这套服务机制的设计目标。查 enabled 把这个目标整个抵消掉了：
        // 注册那一半能用，调用那一半永远失败。
        //
        // 真正要防的是「别调进一段已经 unmap 的代码」，而那件事由上面那两行
        // （weak_ptr 复核 + 指针相等）挡着，和 enabled 无关。一个已经加载、
        // 只是还没 enable 的 mod，它的代码段是映射着的，回调指针是有效的。
        //
        // 「被禁用的 mod 应该表现为不存在」这个想法本身没错，但它必须由
        // 提供方在自己的 `on_disable` 里注销服务来表达 —— 那是它的决定。
        // 由 loader 代劳的代价是把整个 load 阶段的互相探测全部关掉。

        DepthGuard depth;
        bool ok = false;
        try
        {
            ok = svc.cb(svc.user, name, std::string_view{requestRaw}, ctx, reply);
        }
        catch (...)
        {
            // A provider that throws across the FFI boundary is already
            // undefined behaviour on the Rust side; catching here at least
            // keeps the *caller* alive and gives it a status it can act on.
            bridgeLogger().error("service '{}' threw across the FFI boundary", name);
            return LEVI_RS_SERVICE_ERROR;
        }
        return ok ? LEVI_RS_SERVICE_OK : LEVI_RS_SERVICE_ERROR;
    }

    void api_service_list(void* ctx, LeviRsStrSink sink)
    {
        std::string out = "[";
        {
            std::lock_guard lock(gMutex);
            bool first = true;
            for (auto const& [name, id] : gByName)
            {
                auto it = gServices.find(id);
                if (it == gServices.end()) continue;
                if (!first) out += ',';
                first = false;
                char const* owner = it->second.mod ? it->second.mod->getName().c_str() : "?";
                out += "{\"name\":\"";
                out += snbtEscape(name);
                out += "\",\"mod\":\"";
                out += snbtEscape(owner);
                out += "\"}";
            }
        }
        out += ']';
        if (sink) sink(ctx, out);
    }

    void servicesOnRustModGone(RustMod* mod)
    {
        if (!mod) return;
        std::lock_guard lock(gMutex);
        for (auto it = gServices.begin(); it != gServices.end();)
        {
            if (it->second.mod == mod)
            {
                gByName.erase(it->second.name);
                it = gServices.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
} // namespace levi_rs::bridge
