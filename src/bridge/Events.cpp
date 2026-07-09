/** bridge/Events.cpp — event subscription (ABI v1), migrated verbatim. */
#include "bridge/Api.h"
#include "bridge/Common.h"

#include <optional>
#include <string>
#include <string_view>

#include "ll/api/event/DynamicListener.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/Listener.h"
#include "ll/api/event/command/ExecuteCommandEvent.h"

#include "mc/deps/nbt/CompoundTag.h"
#include "mc/platform/UUID.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"

#include "RustMod.h"

namespace levi_rs::bridge
{
    namespace
    {
        /** Resolve an event id, allowing a unique suffix match for ergonomics. */
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
                    if (hit) return std::nullopt; // ambiguous
                    hit.emplace(ll::event::EventId{name});
                }
            }
            return hit;
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

            auto dispatchCommand = [cb, user, idName](
                std::string const& playerName,
                std::string const& xuid,
                std::string const& uuid,
                std::string const& command
            )
            {
                if (playerName.empty()) return; // console or other non-player origin

                std::string snbt = "{\"eventId\":\"" + idName
                    + "\",\"name\":\"" + snbtEscape(playerName)
                    + "\",\"command\":\"" + snbtEscape(command)
                    + "\",\"_player\":{\"name\":\"" + snbtEscape(playerName)
                    + "\",\"xuid\":\"" + snbtEscape(xuid)
                    + "\",\"uuid\":\"" + snbtEscape(uuid) + "\"}}";

                CompoundTag dummy;
                struct WriteCtx
                {
                    CompoundTag* data;
                    bool written = false;
                } wctx{&dummy};
                cb(user, idName, snbt, &wctx,
                   [](void*, LeviRsStr)
                   {
                       /* write-back ignored */
                   });
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
                        dispatchCommand(playerName, xuid, uuid, ctx.mCommand);
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
                        dispatchCommand(playerName, xuid, uuid, ctx.mCommand);
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
            mod->listeners.push_back(typedListener);
            return static_cast<LeviRsListenerHandle>(typedListener.get());
        }

        // --- Normal events: resolve against the registry + DynamicListener ---
        auto resolved = resolveEventId(eventId);
        if (!resolved)
        {
            mod->getLogger().error("subscribe_event: unknown or ambiguous event id '{}'", eventId);
            return nullptr;
        }

        std::string idName = resolved->name;
        auto listener = ll::event::DynamicListener::create(
            [cb, user, idName](CompoundTag& data)
            {
                std::string snbt = enrichWithPlayer(data);

                struct WriteCtx
                {
                    CompoundTag* data;
                    bool written = false;
                } wctx{&data};

                cb(
                    user,
                    idName,
                    snbt,
                    &wctx,
                    [](void* c, LeviRsStr newSnbt)
                    {
                        auto* w = static_cast<WriteCtx*>(c);
                        if (auto tag = CompoundTag::fromSnbt(newSnbt); tag)
                        {
                            *w->data = std::move(*tag);
                            w->written = true;
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
        mod->listeners.push_back(listener);

        return static_cast<LeviRsListenerHandle>(listener.get());
    }

    bool api_unsubscribe_event(LeviRsModHandle modHandle, LeviRsListenerHandle handle)
    {
        auto* mod = asMod(modHandle);
        if (!mod || !handle) return false;
        for (auto it = mod->listeners.begin(); it != mod->listeners.end(); ++it)
        {
            if (it->get() == handle)
            {
                bool ok = ll::event::EventBus::getInstance().removeListener(*it);
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
    }
} // namespace levi_rs::bridge
