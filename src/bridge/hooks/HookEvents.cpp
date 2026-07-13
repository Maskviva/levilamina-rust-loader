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
            cb(user, id, snbt, &w, [](void*, LeviRsStr) {});
        }
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
                if (static_cast<LeviRsListenerHandle>(it->get()) == handle && (*it)->mod == mod)
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
