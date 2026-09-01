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
#include "bridge/Common.h"

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
    } // namespace

    HookEventRegistrar::HookEventRegistrar(HookEventDef& def) { table().push_back(&def); }

    void dispatchHookEvent(HookEventDef& def, std::string const& snbt)
    {
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
            cb(user, id, snbt, &w, [](void*, LeviRsStr)
            {
            });
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
            if (eventId.find(def->name) == std::string_view::npos) continue;
            if (!def->installed)
            {
                def->install();
                def->installed = true;
            }
            std::uint64_t id = nextListenerId();
            def->subs.push_back(std::make_unique<HookSub>(HookSub{mod, cb, user, id}));
            return listenerHandleOf(id);
        }
        return nullptr; // not a bridge-hook event — caller falls through
    }

    bool hookEventUnsubscribe(RustMod* mod, LeviRsListenerHandle handle)
    {
        auto wanted = listenerIdOf(handle);
        if (wanted == 0) return false;
        for (auto* def : table())
        {
            for (auto it = def->subs.begin(); it != def->subs.end(); ++it)
            {
                if ((*it)->id == wanted && (*it)->mod == mod)
                {
                    def->subs.erase(it);
                    return true;
                }
            }
        }
        return false;
    }

    void hookEventDropMod(RustMod* mod)
    {
        for (auto* def : table())
        {
            std::erase_if(def->subs, [&](auto& s) { return s->mod == mod; });
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
