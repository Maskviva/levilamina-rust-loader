/** bridge/Events.cpp — event subscription (ABI v1), migrated verbatim. */
#include "bridge/Api.h"
#include "bridge/Common.h"

#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "ll/api/event/DynamicListener.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/Listener.h"

// Command events (ExecutingCommandEvent / ExecutedCommandEvent) are server-only.
#ifndef LEVI_RS_TARGET_CLIENT
#include "ll/api/event/command/ExecuteCommandEvent.h"
#include "mc/server/commands/CommandOrigin.h"
#endif

#include "mc/deps/nbt/CompoundTag.h"
#include "mc/platform/UUID.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"

#include "RustMod.h"

namespace levi_rs::bridge
{
    namespace
    {
        /**
         * Resolve an event id, allowing a unique suffix match for ergonomics.
         *
         * # Why this dedupes by name
         *
         * `bus.events()` yields **(mod, id) pairs**, not distinct ids: every mod
         * that has registered an emitter for an event contributes its own entry.
         * The previous version counted entries, so as soon as a second mod on the
         * server touched the same event, a perfectly unambiguous name resolved to
         * "ambiguous" and subscription failed.
         *
         * That failure mode was silent in the worst possible way: the caller sees
         * one `Err`, and a mod that registers its listeners with `?` loses every
         * listener after that point. A land-protection mod ends up with exactly
         * one guard armed and no idea why.
         *
         * Two entries with the *same* name are not ambiguous. Only genuinely
         * different names are.
         */
        std::optional<ll::event::EventId> resolveEventId(std::string_view wanted)
        {
            auto& bus = ll::event::EventBus::getInstance();
            if (bus.hasEvent(ll::event::EventIdView{wanted}))
            {
                return ll::event::EventId{wanted};
            }
            std::optional<ll::event::EventId> hit;
            for (auto&& [modName, id] : bus.events())
            {
                std::string_view name = id.name;
                bool match =
                    name.size() > wanted.size() && name.ends_with(wanted)
                    && (name[name.size() - wanted.size() - 1] == ':' || name[name.size() - wanted.size() - 1] == '.');
                if (match || name == wanted)
                {
                    // Same name from another mod's registration — not ambiguity.
                    if (hit && std::string_view(hit->name) != name) return std::nullopt;
                    if (!hit) hit.emplace(ll::event::EventId{name});
                }
            }
            return hit;
        }

        /**
         * Log every registered id that looks related to `wanted`, so a failed
         * subscription says what the engine actually calls the event instead of
         * just "unknown". One line, only on the failure path.
         */
        void reportSimilarEvents(RustMod* mod, std::string_view wanted)
        {
            // Take the longest trailing CamelCase word as the needle: for
            // "PlayerPlacingBlockEvent" that is "Block", which is loose enough to
            // catch a rename and tight enough not to dump the whole registry.
            std::string needle;
            for (size_t i = 0; i < wanted.size(); ++i)
            {
                if (i && std::isupper(static_cast<unsigned char>(wanted[i]))) needle.clear();
                needle.push_back(wanted[i]);
            }
            if (needle.size() < 3) needle = std::string(wanted.substr(0, 6));

            std::string found;
            size_t n = 0;
            for (auto&& [modName, id] : ll::event::EventBus::getInstance().events())
            {
                std::string_view name = id.name;
                if (name.find(needle) == std::string_view::npos) continue;
                if (found.find(name) != std::string::npos) continue; // dedupe
                if (n++) found += ", ";
                found += name;
                if (n >= 12) break;
            }
            if (found.empty()) found = "(registry has nothing containing '" + needle + "')";
            mod->getLogger().error("subscribe_event: ids containing '{}': {}", needle, found);
        }
    } // namespace

    LeviRsListenerHandle
    api_subscribe_event(LeviRsModHandle modHandle, LeviRsStr eventId, int32_t priority, LeviRsEventCb cb, void* user)
    {
        auto* mod = asMod(modHandle);
        if (!mod || !cb) return nullptr;

        // ABI speaks 0..4 (Highest..Lowest); LeviLamina uses 0/100/200/300/400.
        ll::event::EventPriority prio;
        switch (priority)
        {
        case 0:
            prio = ll::event::EventPriority::Highest;
            break;
        case 1:
            prio = ll::event::EventPriority::High;
            break;
        case 3:
            prio = ll::event::EventPriority::Low;
            break;
        case 4:
            prio = ll::event::EventPriority::Lowest;
            break;
        case 2:
        default:
            prio = ll::event::EventPriority::Normal;
            break;
        }

        std::string_view wanted = eventId;

        // --- Command events: typed listeners only, bypass the dynamic registry ---
        // LeviLamina dispatches ExecutingCommandEvent / ExecutedCommandEvent solely
        // to typed listeners and never registers them in the dynamic event registry,
        // so resolveEventId() (which relies on hasEvent()/events()) can't find them.
        // Match by name up front and hook a typed listener directly. The callback is
        // fed a hand-built SNBT so the Rust-facing shape matches the dynamic path.
        // (Both events are final — required for the emplaceListener template param;
        // their shared base ExecuteCommandEvent isn't final and won't compile.)
        //
        // SERVER-ONLY: command events don't exist on the client build.
#ifndef LEVI_RS_TARGET_CLIENT
        bool isExecuting = wanted.find("ExecutingCommandEvent") != std::string_view::npos;
        bool isExecuted = wanted.find("ExecutedCommandEvent") != std::string_view::npos;
        if (isExecuting || isExecuted)
        {
            // The typed events live in an inline namespace (ll::event::inline
            // command), so getEventId<command::ExecutingCommandEvent> yields an
            // id string that includes the "command::" segment. But LeviLamina
            // registers the emitter under the *un-inlined* name
            // (ll::event::ExecutingCommandEvent, as shown by `/levirs events`),
            // so emplaceListener<T> — which calls addListener(res, getEventId<T>)
            // — looks up the wrong id and fails. We therefore build the typed
            // Listener<T> ourselves and register it against the real id resolved
            // from the registry via the non-template addListener overload. This
            // keeps the typed callback (which reads the player + command straight
            // off the CommandContext origin) while fixing the id mismatch.
            auto resolvedCmd = resolveEventId(eventId);
            if (!resolvedCmd)
            {
                mod->getLogger().error(
                    "subscribe_event: command event '{}' not found in registry", eventId
                );
                return nullptr;
            }
            std::string idName = resolvedCmd->name;

            // Returns true when a subscriber asked for the command to be refused.
            //
            // The write-back used to be discarded here with a
            // `/* write-back ignored */`. That silently made
            // ExecutingCommandEvent **observe-only** even though the LL event is
            // `Cancellable<ExecuteCommandEvent>`: a subscriber could call
            // `ev.cancel()`, see no error anywhere, and watch the command run.
            // Any guard built on it — a command whitelist, say — reported itself
            // as armed and did nothing, which is the worst failure shape a
            // security check has.
            auto dispatchCommand = [cb, user, idName](
                std::string const& playerName,
                std::string const& xuid,
                std::string const& uuid,
                std::string const& command
            ) -> bool
            {
                // Console, command blocks, and every other non-player origin are
                // not reported at all. That is deliberate and load-bearing for
                // callers that gate commands: refusing the console would lock a
                // server owner out of their own server with no way back in.
                if (playerName.empty()) return false;

                std::string snbt = "{\"eventId\":\"" + idName
                    + "\",\"name\":\"" + snbtEscape(playerName)
                    + "\",\"command\":\"" + snbtEscape(command)
                    + "\",\"_player\":{\"name\":\"" + snbtEscape(playerName)
                    + "\",\"xuid\":\"" + snbtEscape(xuid)
                    + "\",\"uuid\":\"" + snbtEscape(uuid) + "\"}}";

                struct WriteCtx
                {
                    bool cancelled = false;
                } wctx{};
                cb(user, idName, snbt, &wctx,
                   [](void* c, LeviRsStr newSnbt)
                   {
                       // The Rust side writes the whole event back with
                       // `cancelled` flipped (EventRef::cancel), same as the
                       // dynamic path. Only that one field is read here — there
                       // is nothing else on a command event worth writing, and
                       // rebuilding the CommandContext from SNBT is not a thing.
                       auto* w = static_cast<WriteCtx*>(c);
                       auto tag = CompoundTag::fromSnbt(newSnbt);
                       if (!tag || !tag->contains("cancelled")) return;
                       try
                       {
                           if (static_cast<uchar>(tag->at("cancelled")) != 0) w->cancelled = true;
                       }
                       catch (...)
                       {
                       }
                   });
                return wctx.cancelled;
            };

            std::shared_ptr<ll::event::ListenerBase> typedListener;
            if (isExecuting)
            {
                typedListener = ll::event::Listener<
                    ll::event::command::ExecutingCommandEvent>::create(
                    [dispatchCommand](ll::event::command::ExecutingCommandEvent& ev)
                    {
                        std::string playerName, xuid, uuid;
                        auto& ctx = ev.commandContext();
                        if (ctx.mOrigin && ctx.mOrigin->getEntity())
                        {
                            auto* entity = ctx.mOrigin->getEntity();
                            if (entity->isPlayer())
                            {
                                auto* p = static_cast<Player*>(entity);
                                playerName = p->getRealName();
                                xuid = p->getXuid();
                                uuid = p->getUuid().asString();
                            }
                        }
                        if (dispatchCommand(playerName, xuid, uuid, ctx.mCommand))
                        {
                            ev.cancel();
                        }
                    },
                    prio,
                    mod->shared_from_this()
                );
            }
            else
            {
                typedListener = ll::event::Listener<
                    ll::event::command::ExecutedCommandEvent>::create(
                    [dispatchCommand](ll::event::command::ExecutedCommandEvent& ev)
                    {
                        std::string playerName, xuid, uuid;
                        // Base ExecuteCommandEvent::commandContext() returns a const
                        // ref; mOrigin is a pointer member, so the pointer is const
                        // but the pointee isn't — non-const getEntity() still works.
                        auto const& ctx = ev.commandContext();
                        if (ctx.mOrigin && ctx.mOrigin->getEntity())
                        {
                            auto* entity = ctx.mOrigin->getEntity();
                            if (entity->isPlayer())
                            {
                                auto* p = static_cast<Player*>(entity);
                                playerName = p->getRealName();
                                xuid = p->getXuid();
                                uuid = p->getUuid().asString();
                            }
                        }
                        // ExecutedCommandEvent is not cancellable — the command
                        // has already run. The veto bit is discarded rather than
                        // logged: a subscriber that cancels here is asking for
                        // something the event cannot express, and one log line
                        // per command is a log flood, not a diagnostic.
                        (void)dispatchCommand(playerName, xuid, uuid, ctx.mCommand);
                    },
                    prio,
                    mod->shared_from_this()
                );
            }

            // Register with the real registry id (not getEventId<T>).
            if (!typedListener
                || !ll::event::EventBus::getInstance().addListener(
                    typedListener, ll::event::EventIdView{resolvedCmd->name}
                ))
            {
                mod->getLogger().error(
                    "subscribe_event: failed to register typed command listener for '{}'",
                    idName
                );
                return nullptr;
            }
            std::uint64_t id = nextListenerId();
            mod->listeners.push_back({id, typedListener});
            return listenerHandleOf(id);
        }
#endif // !LEVI_RS_TARGET_CLIENT

        // --- Bridge-hook events: synthetic ids backed by native detours ---
        // (hooks/ owns the detours; matched by name like the command
        // events above, so they never touch the dynamic registry.)
        if (auto h = hookEventSubscribe(mod, wanted, cb, user))
        {
            return h;
        }

        // --- Normal events: resolve against the registry + DynamicListener ---
        auto resolved = resolveEventId(eventId);
        if (!resolved)
        {
            mod->getLogger().error("subscribe_event: unknown or ambiguous event id '{}'", eventId);
            reportSimilarEvents(mod, wanted);
            return nullptr;
        }

        std::string idName = resolved->name;
        auto listener = ll::event::DynamicListener::create(
            [cb, user, idName](CompoundTag& data)
            {
                std::string snbt = enrichWithPlayer(data);

                struct WriteCtx
                {
                    CompoundTag*       data;
                    std::string const* snapshot; // 正是交给 cb 的那一份
                } wctx{&data, &snbt};

                cb(
                    user,
                    idName,
                    snbt,
                    &wctx,
                    [](void* c, LeviRsStr newSnbt)
                    {
                        auto* w = static_cast<WriteCtx*>(c);
                        auto edited = CompoundTag::fromSnbt(newSnbt);
                        if (!edited) return;

                        // 只写这个调用方相对**它自己拿到的那份快照**真正改动过
                        // 的字段。
                        //
                        // 整棵树替换（原来的做法）会在两个 mod 订阅同一事件时
                        // 丢更新：第二个回调拿到的是第一个编辑**之前**的样子，
                        // 把自己那份写回去就把前一个的改动还原了。不报错、不打
                        // 日志——一个聊天过滤和一个土地保护挂在同一个事件上，会
                        // 按优先级顺序互相抵消。
                        //
                        // 按快照做差量让两者互不干扰：没碰过的字段永远不写，于
                        // 是保留前一个监听器留下的值。同字段冲突仍是后写者赢，
                        // 那是任何 merge 能做到的上限。
                        //
                        // 多解析一次快照的代价只在真的写回时才付；纯观察的监听
                        // 器根本走不到这个 lambda。
                        auto base = CompoundTag::fromSnbt(*w->snapshot);
                        if (!base)
                        {
                            // 快照解析不了（它来自 toSnbt，基本不可能）。退回旧
                            // 的整树语义，总比把这次编辑丢掉强。
                            *w->data = std::move(*edited);
                            return;
                        }

                        for (auto const& [key, value] : edited->mTags)
                        {
                            auto it = base->mTags.find(key);
                            if (it == base->mTags.end() || !(it->second == value))
                            {
                                w->data->mTags[key] = value;
                            }
                        }
                        for (auto const& [key, value] : base->mTags)
                        {
                            (void)value;
                            if (!edited->mTags.contains(key)) w->data->erase(key);
                        }
                    }
                );
            },
            prio,
            mod->shared_from_this()
        );

        if (!ll::event::EventBus::getInstance().addListener(listener, ll::event::EventIdView{resolved->name}))
        {
            return nullptr;
        }
        std::uint64_t id = nextListenerId();
        mod->listeners.push_back({id, listener});
        return listenerHandleOf(id);
    }

    bool api_unsubscribe_event(LeviRsModHandle modHandle, LeviRsListenerHandle handle)
    {
        auto* mod = asMod(modHandle);
        if (!mod || !handle) return false;
        if (hookEventUnsubscribe(mod, handle)) return true; // bridge-hook events first
        auto wanted = listenerIdOf(handle);
        for (auto it = mod->listeners.begin(); it != mod->listeners.end(); ++it)
        {
            if (it->id == wanted)
            {
                bool ok = ll::event::EventBus::getInstance().removeListener(it->listener);
                mod->listeners.erase(it);
                return ok;
            }
        }
        return false;
    }

    void api_list_events(void* ctx, LeviRsStrSink sink)
    {
        if (!sink) return;
        for (auto&& [modName, id] : ll::event::EventBus::getInstance().events())
        {
            sink(ctx, id.name);
        }
        hookEventList(ctx, sink); // bridge-hook events (hooks/) are subscribable too
    }
} // namespace levi_rs::bridge
