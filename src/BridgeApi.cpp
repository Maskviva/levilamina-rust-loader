#include "BridgeApi.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/command/runtime/ParamKind.h"
#include "ll/api/command/runtime/RuntimeCommand.h"
#include "ll/api/command/runtime/RuntimeOverload.h"
#include "ll/api/event/command/ExecuteCommandEvent.h"
#include "ll/api/event/DynamicListener.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/io/LogLevel.h"
#include "ll/api/io/Logger.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/service/GamingStatus.h"
#include "ll/api/thread/ServerThreadExecutor.h"
#include "ll/api/utils/ErrorUtils.h"

#include "mc/deps/nbt/CompoundTag.h"
#include "mc/platform/UUID.h"
#include "mc/server/ServerLevel.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandOutputMessage.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/server/commands/CommandRawText.h"
#include "mc/server/commands/ServerCommandOrigin.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/TickDeltaTimeManager.h"

#include "RustMod.h"

namespace levi_rs {

namespace {

RustMod* asMod(LeviRsModHandle h) { return static_cast<RustMod*>(h); }

// Give player-carrying events a real identity.
//
// Many LeviLamina events serialize Player& as a reflection stub —
// self:{_type_:Player,_pointer_:...} — with no name/xuid/uuid. We find that
// stub, validate the pointer against the live player list (never deref an
// unverified pointer), then splice a `_player` field with the real identity
// into the tag. Join/chat/death/command events all get identity this way.

// Live player addresses. Only pointers found here get dereferenced.
std::unordered_set<uintptr_t> livePlayerAddrs() {
    std::unordered_set<uintptr_t> addrs;
    auto level = ll::service::getLevel();
    if (!level) return addrs;
    level->forEachPlayer([&](Player& p) {
        addrs.insert(reinterpret_cast<uintptr_t>(&p));
        return true;
    });
    return addrs;
}

// Find an embedded Player pointer stub in the event's tag: a field holding a
// compound with `_type_` ("Player") and `_pointer_`. Top-level only — every
// event seen so far (join/chat/death/disconnect) has it there.
uintptr_t findPlayerPointer(CompoundTag const& data) {
    for (auto const& entry : data.mTags) {
        auto const& value = entry.second;
        if (!value.is_object()) continue;
        auto const& obj = value.get<CompoundTag>();
        if (!obj.contains("_type_") || !obj.contains("_pointer_")) continue;

        auto const& typeVar = obj.at("_type_");
        if (!typeVar.is_string() || std::string_view(typeVar) != "Player") continue;

        auto const& ptrVar = obj.at("_pointer_");
        if (!ptrVar.is_number()) continue;
        return static_cast<uintptr_t>(static_cast<int64_t>(ptrVar));
    }
    return 0;
}

// If the event embeds a live player pointer, add a `_player` field (real
// name/xuid/uuid) on a copy and serialize that. Never mutate the original
// `data` — it's fed to event.deserialize() afterward.
std::string enrichWithPlayer(CompoundTag const& data) {
    uintptr_t addr = findPlayerPointer(data);
    if (addr != 0) {
        // Safety gate: only dereference pointers of currently online players.
        auto addrs = livePlayerAddrs();
        if (addrs.find(addr) != addrs.end()) {
            if (auto* player = reinterpret_cast<Player*>(addr)) {
                CompoundTag copy = data;
                copy["_player"] = CompoundTagVariant::object({
                    {"name", CompoundTagVariant(player->getRealName())},
                    {"xuid", CompoundTagVariant(player->getXuid())},
                    {"uuid", CompoundTagVariant(player->getUuid().asString())}
                });
                return copy.toSnbt(SnbtFormat::Minimize);
            }
        }
    }
    return data.toSnbt(SnbtFormat::Minimize);
}

// ───────────────────────── logging ─────────────────────────

void api_log(LeviRsModHandle mod, int32_t level, LeviRsStr msg) {
    if (!mod) return;
    auto& logger = asMod(mod)->getLogger();
    switch (static_cast<ll::io::LogLevel>(level)) {
    case ll::io::LogLevel::Fatal:
        logger.fatal("{}", msg);
        break;
    case ll::io::LogLevel::Error:
        logger.error("{}", msg);
        break;
    case ll::io::LogLevel::Warn:
        logger.warn("{}", msg);
        break;
    case ll::io::LogLevel::Debug:
        logger.debug("{}", msg);
        break;
    case ll::io::LogLevel::Trace:
        logger.trace("{}", msg);
        break;
    case ll::io::LogLevel::Off:
        break;
    case ll::io::LogLevel::Info:
    default:
        logger.info("{}", msg);
        break;
    }
}

int32_t api_gaming_status() { return static_cast<int32_t>(ll::getGamingStatus()); }

// ───────────────────────── scheduling ─────────────────────────

void api_schedule(LeviRsTaskCb cb, void* user) {
    if (!cb) return;
    ll::thread::ServerThreadExecutor::getDefault().execute([cb, user] { cb(user); });
}

void api_schedule_after(LeviRsTaskCb cb, void* user, uint64_t delayMs) {
    if (!cb) return;
    // Executor::Duration = std::chrono::steady_clock::duration; milliseconds convert implicitly.
    // Fire-and-forget: the returned CancellableCallback is intentionally dropped.
    (void)ll::thread::ServerThreadExecutor::getDefault().executeAfter(
        [cb, user] { cb(user); },
        std::chrono::milliseconds(delayMs)
    );
}

// ───────────────────────── events ─────────────────────────

/** Resolve an event id, allowing a unique suffix match for ergonomics. */
std::optional<ll::event::EventId> resolveEventId(std::string_view wanted) {
    auto& bus = ll::event::EventBus::getInstance();
    if (bus.hasEvent(ll::event::EventIdView{wanted})) {
        return ll::event::EventId{wanted};
    }
    std::optional<ll::event::EventId> hit;
    for (auto&& [modName, id] : bus.events()) {
        std::string_view name = id.name;
        bool             match =
            name.size() > wanted.size() && name.ends_with(wanted)
            && (name[name.size() - wanted.size() - 1] == ':' || name[name.size() - wanted.size() - 1] == '.');
        if (match || name == wanted) {
            if (hit) return std::nullopt; // ambiguous
            hit.emplace(ll::event::EventId{name});
        }
    }
    return hit;
}

LeviRsListenerHandle
api_subscribe_event(LeviRsModHandle modHandle, LeviRsStr eventId, int32_t priority, LeviRsEventCb cb, void* user) {
    auto* mod = asMod(modHandle);
    if (!mod || !cb) return nullptr;

    auto resolved = resolveEventId(eventId);
    if (!resolved) {
        mod->getLogger().error("subscribe_event: unknown or ambiguous event id '{}'", eventId);
        return nullptr;
    }

    // ABI speaks 0..4 (Highest..Lowest); LeviLamina uses 0/100/200/300/400.
    ll::event::EventPriority prio;
    switch (priority) {
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

    std::string idName = resolved->name;
    auto        listener = ll::event::DynamicListener::create(
        [cb, user, idName](CompoundTag& data) {
            std::string snbt = enrichWithPlayer(data);

            struct WriteCtx {
                CompoundTag* data;
                bool         written = false;
            } wctx{&data};

            cb(
                user,
                idName,
                snbt,
                &wctx,
                [](void* c, LeviRsStr newSnbt) {
                    auto* w = static_cast<WriteCtx*>(c);
                    if (auto tag = CompoundTag::fromSnbt(newSnbt); tag) {
                        *w->data   = std::move(*tag);
                        w->written = true;
                    }
                }
            );
        },
        prio,
        mod->shared_from_this()
    );

    if (!ll::event::EventBus::getInstance().addListener(listener, ll::event::EventIdView{resolved->name})) {
        return nullptr;
    }
    mod->listeners.push_back(listener);

    // Command events (ExecutingCommandEvent / ExecutedCommandEvent) never
    // reach the DynamicListener above — LeviLamina only dispatches these to
    // typed listeners. We hook a typed listener per resolved type instead,
    // splice an equivalent SNBT by hand, and call the same `cb` — Rust sees
    // no difference.
    //
    // Both are final, required for the template param (their base
    // ExecuteCommandEvent isn't final and fails to compile with "Only final
    // classes can be listen").
    auto dispatchCommand = [cb, user, idName](
        std::string const& playerName,
        std::string const& xuid,
        std::string const& uuid,
        std::string const& command
    ) {
        if (playerName.empty()) return; // console or other non-player origin

        auto esc = [](std::string const& s) {
            std::string out;
            out.reserve(s.size() + 2);
            for (char c : s) {
                if (c == '"' || c == '\\') out.push_back('\\');
                out.push_back(c);
            }
            return out;
        };
        std::string snbt = "{\"eventId\":\"" + idName
                         + "\",\"name\":\"" + esc(playerName)
                         + "\",\"command\":\"" + esc(command)
                         + "\",\"_player\":{\"name\":\"" + esc(playerName)
                         + "\",\"xuid\":\"" + esc(xuid)
                         + "\",\"uuid\":\"" + esc(uuid) + "\"}}";

        CompoundTag dummy;
        struct WriteCtx { CompoundTag* data; bool written = false; } wctx{&dummy};
        cb(user, idName, snbt, &wctx,
           [](void*, LeviRsStr) { /* write-back ignored */ });
    };

    if (resolved->name.find("ExecutingCommandEvent") != std::string::npos) {
        auto typedListener = ll::event::EventBus::getInstance().emplaceListener<
            ll::event::command::ExecutingCommandEvent>(
            [dispatchCommand](ll::event::command::ExecutingCommandEvent& ev) {
                std::string playerName, xuid, uuid;
                auto& ctx = ev.commandContext();
                if (ctx.mOrigin && ctx.mOrigin->getEntity()) {
                    auto* entity = ctx.mOrigin->getEntity();
                    if (entity->isPlayer()) {
                        auto* p    = static_cast<Player*>(entity);
                        playerName = p->getRealName();
                        xuid       = p->getXuid();
                        uuid       = p->getUuid().asString();
                    }
                }
                dispatchCommand(playerName, xuid, uuid, ctx.mCommand);
            },
            prio,
            mod->shared_from_this()
        );
        mod->listeners.push_back(typedListener);
    } else if (resolved->name.find("ExecutedCommandEvent") != std::string::npos) {
        auto typedListener = ll::event::EventBus::getInstance().emplaceListener<
            ll::event::command::ExecutedCommandEvent>(
            [dispatchCommand](ll::event::command::ExecutedCommandEvent& ev) {
                std::string playerName, xuid, uuid;
                // Base ExecuteCommandEvent::commandContext() returns a const
                // ref; mOrigin is a pointer member, so the pointer is const
                // but the pointee isn't — non-const getEntity() still works.
                auto const& ctx = ev.commandContext();
                if (ctx.mOrigin && ctx.mOrigin->getEntity()) {
                    auto* entity = ctx.mOrigin->getEntity();
                    if (entity->isPlayer()) {
                        auto* p    = static_cast<Player*>(entity);
                        playerName = p->getRealName();
                        xuid       = p->getXuid();
                        uuid       = p->getUuid().asString();
                    }
                }
                dispatchCommand(playerName, xuid, uuid, ctx.mCommand);
            },
            prio,
            mod->shared_from_this()
        );
        mod->listeners.push_back(typedListener);
    }

    return static_cast<LeviRsListenerHandle>(listener.get());
}

bool api_unsubscribe_event(LeviRsModHandle modHandle, LeviRsListenerHandle handle) {
    auto* mod = asMod(modHandle);
    if (!mod || !handle) return false;
    for (auto it = mod->listeners.begin(); it != mod->listeners.end(); ++it) {
        if (it->get() == handle) {
            bool ok = ll::event::EventBus::getInstance().removeListener(*it);
            mod->listeners.erase(it);
            return ok;
        }
    }
    return false;
}

void api_list_events(void* ctx, LeviRsStrSink sink) {
    if (!sink) return;
    for (auto&& [modName, id] : ll::event::EventBus::getInstance().events()) {
        sink(ctx, id.name);
    }
}

// ───────────────────────── commands ─────────────────────────

bool api_execute_command(LeviRsStr cmd, void* ctx, LeviRsCmdOutputSink sink) {
    auto level = ll::service::getLevel();
    if (!level) return false;

    ServerCommandOrigin origin{
        "Server",
        static_cast<ServerLevel&>(*level),
        CommandPermissionLevel::Owner,
        0 // overworld; command selectors/positions can address other dimensions
    };
    auto output = ll::command::CommandRegistrar::getServerInstance().executeCommand(cmd, origin);
    if (sink) {
        // NOTE: verify against your LL version — CommandOutput exposes
        // mSuccessCount; combined text is easiest via the localized dump.
        std::string text;
        for (auto const& msg : output.mMessages) {
            if (!text.empty()) text += '\n';
            text += msg.mMessageId;
            for (auto const& param : msg.mParams) {
                text += ' ';
                text += param;
            }
        }
        sink(ctx, output.mSuccessCount > 0, text);
    }
    return true;
}

/**
 * Command dispatch table. Bedrock cannot unregister commands, so bindings
 * live for the whole server lifetime; a binding whose mod unloads is nulled
 * and answers with an error instead of dangling.
 */
struct CommandBinding {
    RustMod*        mod = nullptr;
    LeviRsCommandCb cb  = nullptr;
    void*           user = nullptr;
};
std::mutex                                                       gCmdMutex;
std::unordered_map<std::string, std::shared_ptr<CommandBinding>> gCommands;

bool api_register_command(
    LeviRsModHandle modHandle,
    LeviRsStr       name,
    LeviRsStr       description,
    int32_t         permission,
    LeviRsCommandCb cb,
    void*           user
) {
    auto* mod = asMod(modHandle);
    if (!mod || !cb) return false;
    std::string cmdName{name};

    std::shared_ptr<CommandBinding> binding;
    {
        std::lock_guard lock(gCmdMutex);
        auto [it, inserted] = gCommands.try_emplace(cmdName, std::make_shared<CommandBinding>());
        binding             = it->second;
        bool rebind = !inserted && (binding->mod == nullptr || binding->mod == mod);
        if (!inserted && !rebind) return false; // taken by another live mod
        binding->mod  = mod;
        binding->cb   = cb;
        binding->user = user;
        if (!inserted) return true; // command itself already registered with Bedrock
    }

    try {
        using namespace ll::command;
        // Note: the runtime overload is deliberately owned by the loader
        // (NativeMod::current()), not the rust mod — Bedrock commands cannot
        // be unregistered, so the executor must outlive any rust mod. Muting
        // via the binding table keeps behaviour predictable across unloads.
        auto& handle = CommandRegistrar::getServerInstance().getOrCreateCommand(
            cmdName,
            std::string{description},
            static_cast<CommandPermissionLevel>(std::clamp<int32_t>(permission, 0, 4))
        );
        handle.runtimeOverload().optional("args", ParamKind::RawText).execute(
            [binding, cmdName](CommandOrigin const& origin, CommandOutput& output, RuntimeCommand const& rt) {
                CommandBinding local;
                {
                    std::lock_guard lock(gCmdMutex);
                    local = *binding;
                }
                if (!local.mod || local.mod->commandsMuted || !local.cb) {
                    output.error("command '" + cmdName + "' is not available (mod disabled)");
                    return;
                }
                std::string args;
                if (auto const& p = rt["args"]; p.hold(ParamKind::RawText)) {
                    args = p.get<ParamKind::RawText>().mText;
                }
                std::string originName = origin.getName();
                local.cb(
                    local.user,
                    args,
                    originName,
                    &output,
                    [](void* c, LeviRsStr s) { static_cast<CommandOutput*>(c)->success(std::string{s}); },
                    [](void* c, LeviRsStr s) { static_cast<CommandOutput*>(c)->error(std::string{s}); }
                );
            }
        );
        return true;
    } catch (...) {
        ll::error_utils::printCurrentException(mod->getLogger());
        std::lock_guard lock(gCmdMutex);
        gCommands.erase(cmdName);
        return false;
    }
}

// ───────────────────────── server stats ─────────────────────────

uint64_t api_get_current_tick() {
    auto level = ll::service::getLevel();
    if (!level) return 0;
    return level->getCurrentTick().tickID;
}

double api_get_tick_delta_time() {
    auto level = ll::service::getLevel();
    if (!level) return -1.0;
    return level->getTickDeltaTimeManager()->mTickDeltaTime;
}

int32_t api_get_player_count() {
    auto level = ll::service::getLevel();
    if (!level) return 0;
    return static_cast<int32_t>(level->getActivePlayerCount());
}

bool api_get_sim_paused() {
    auto level = ll::service::getLevel();
    if (!level) return true; // safe default: treat as paused if unknown
    return level->getSimPaused();
}

// ───────────────────────── table ─────────────────────────

const LeviRsApi gApi{
    /* abi_version       */ LEVI_RS_ABI_VERSION,
    /* struct_size       */ sizeof(LeviRsApi),
    /* log               */ api_log,
    /* gaming_status     */ api_gaming_status,
    /* schedule          */ api_schedule,
    /* schedule_after    */ api_schedule_after,
    /* subscribe_event   */ api_subscribe_event,
    /* unsubscribe_event */ api_unsubscribe_event,
    /* list_events       */ api_list_events,
    /* execute_command   */ api_execute_command,
    /* register_command  */ api_register_command,
    /* get_current_tick  */ api_get_current_tick,
    /* get_tick_delta_time*/api_get_tick_delta_time,
    /* get_player_count  */ api_get_player_count,
    /* get_sim_paused    */ api_get_sim_paused,
};

} // namespace

const LeviRsApi* getBridgeApi() { return &gApi; }

namespace detail {
void onRustModGone(RustMod* mod) {
    std::lock_guard lock(gCmdMutex);
    for (auto& [name, binding] : gCommands) {
        if (binding->mod == mod) {
            binding->mod  = nullptr;
            binding->cb   = nullptr;
            binding->user = nullptr;
        }
    }
}
} // namespace detail

} // namespace levi_rs

bool leviRsVerifyStrLayout() {
    // Read the view's raw bytes as {ptr, len} and compare to data()/size().
    // This layout is an MSVC STL detail, not standard-guaranteed — fail
    // loudly here instead of Rust silently misreading pointer/length.
    static constexpr char kProbe[] = "levi-rs-layout-probe";
    std::string_view      sv(kProbe, sizeof(kProbe) - 1);

    struct RawView {
        const char* ptr;
        size_t      len;
    };
    static_assert(sizeof(RawView) == sizeof(std::string_view));

    auto const& raw = reinterpret_cast<RawView const&>(sv);
    return raw.ptr == sv.data() && raw.len == sv.size();
}