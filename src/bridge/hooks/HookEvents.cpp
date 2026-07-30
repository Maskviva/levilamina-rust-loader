/**
 * bridge/hooks/HookEvents.cpp — registry storage, dispatch, and the four
 * plumbing functions Events.cpp / mod-unload route through
 * (hookEventSubscribe / hookEventUnsubscribe / hookEventDropMod /
 * hookEventList). See HookEvents.h for the module contract.
 */
#include "bridge/hooks/HookEvents.h"

#include <algorithm>
#include <utility>

#include "bridge/Api.h"

namespace levi_rs::bridge
{
    namespace
    {
        /** Meyers singleton: safe to fill from any TU's static registrars. */
        std::vector<HookEventDef*>& table()
        {
            static std::vector<HookEventDef*> t;
            return t;
        }

        /// 派发期间退订：只把 cb 置空，条目留在原位。dispatch 循环跳过空 cb，
        /// 结束时再统一清理。这样 user 指向的 Rust Box 在整轮派发内保持存活。
        void compact(HookEventDef& def)
        {
            std::erase_if(def.subs, [](auto& s) { return s->cb == nullptr; });
        }
    } // namespace

    HookEventRegistrar::HookEventRegistrar(HookEventDef& def) { table().push_back(&def); }

    void dispatchHookEvent(HookEventDef& def, std::string const& snbt)
    {
        bool const outermost = !def.dispatching;
        def.dispatching = true;

        // Snapshot first: a callback may (un)subscribe during dispatch,
        // mutating def.subs — iterating it directly would be UB.
        std::vector<std::pair<LeviRsEventCb, void*>> snap;
        snap.reserve(def.subs.size());
        for (auto& sub : def.subs) snap.emplace_back(sub->cb, sub->user);

        std::string id{def.name};
        struct WCtx
        {
        } w; // observe-only: write-back is a no-op
        for (auto& [cb, user] : snap)
        {
            if (!cb) continue; // 本轮内被退订了
            cb(user, id, snbt, &w, [](void*, LeviRsStr)
            {
            });
        }

        if (outermost)
        {
            def.dispatching = false;
            compact(def);
        }
    }

    bool dispatchHookEventCancellable(HookEventDef& def, std::string const& snbt)
    {
        // Same snapshot discipline as dispatchHookEvent, plus a real write-back
        // sink: a subscriber that replies with a cancelled flag vetoes the
        // action.
        //
        // The flag is looked for in the two shapes the Rust side can produce
        // (EventRef::cancel): SNBT `cancelled:1b` when the payload round-trips
        // through NbtValue, and the literal `"cancelled":1` when it takes the
        // string-replacement path. Matching only one of them would make
        // cancellation silently do nothing — which is exactly the failure that
        // would be hardest to notice, since everything else keeps working.
        std::vector<std::pair<LeviRsEventCb, void*>> snap;
        snap.reserve(def.subs.size());
        for (auto& sub : def.subs) snap.emplace_back(sub->cb, sub->user);

        std::string id{def.name};
        bool cancelled = false;
        for (auto& [cb, user] : snap)
        {
            std::string reply;
            cb(user, id, snbt, &reply, [](void* ctx, LeviRsStr v)
            {
                if (ctx) *static_cast<std::string*>(ctx) = std::string{v};
            });
            if (reply.find("cancelled:1b") != std::string::npos
                || reply.find("\"cancelled\":1") != std::string::npos
                || reply.find("cancelled:1 ") != std::string::npos)
            {
                cancelled = true;
                // Keep going: every subscriber still sees the event. Stopping
                // early would make "was I called?" depend on listener order.
            }
        }
        return cancelled;
    }

    LeviRsListenerHandle
    hookEventSubscribe(RustMod* mod, std::string_view eventId, LeviRsEventCb cb, void* user)
    {
        for (auto* def : table())
        {
            if (eventId != def->name) continue;
            if (!def->installed)
            {
                def->install();
                def->installed = true;
            }
            def->subs.push_back(std::make_unique<HookSub>(HookSub{mod, cb, user}));
            return static_cast<LeviRsListenerHandle>(def->subs.back().get());
        }
        return nullptr; // not a bridge-hook event — caller falls through
    }

    bool hookEventUnsubscribe(RustMod* mod, LeviRsListenerHandle handle)
    {
        for (auto* def : table())
        {
            for (auto it = def->subs.begin(); it != def->subs.end(); ++it)
            {
                if (static_cast<LeviRsListenerHandle>(it->get()) != handle) continue;
                if ((*it)->mod != mod) continue;
                if (def->dispatching)
                {
                    (*it)->cb = nullptr; // 打墓碑，等派发结束再删
                }
                else
                {
                    def->subs.erase(it);
                }
                return true;
            }
        }
        return false;
    }

    void hookEventDropMod(RustMod* mod)
    {
        for (auto* def : table())
        {
            if (def->dispatching)
            {
                for (auto& s : def->subs)
                {
                    if (s->mod == mod) s->cb = nullptr;
                }
            }
            else
            {
                std::erase_if(def->subs, [&](auto& s) { return s->mod == mod; });
            }
        }
    }

    void hookEventList(void* ctx, LeviRsStrSink sink)
    {
        for (auto* def : table())
        {
            sink(ctx, std::string{def->name});
        }
    }
} // namespace levi_rs::bridge
