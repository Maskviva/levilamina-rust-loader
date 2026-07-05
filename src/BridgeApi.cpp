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

inline LeviRsStr toStr(std::string_view sv) { return LeviRsStr{sv.data(), sv.size()}; }
inline std::string_view fromStr(LeviRsStr s) { return {s.ptr, s.len}; }

RustMod* asMod(LeviRsModHandle h) { return static_cast<RustMod*>(h); }

// 给玩家事件补上真实身份。
//
// 很多 LeviLamina 事件把 Player& 序列化成一个反射存根 self:{_type_:Player,_pointer_:...}，
// 里面没有名字、xuid、uuid。Rust 侧要用这些，所以序列化后先找这个存根，把指针跟当前
// 在线玩家列表比对（绝不解引用没验证过的指针），确认是真玩家再解引用取身份，拼一个
// _player 塞回 SNBT。这样进服、聊天、死亡、命令等带玩家的事件在 Rust 侧都能直接拿到身份。

// 收集当前所有在线玩家的地址，只解引用确认在列表里的指针
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

// 在 minimize SNBT 里找 _type_:Player 存根对应的 _pointer_，没有就返回 0。
// minimize 格式没空格，_pointer_ 和 _type_:Player 在同一个 {} 块里，位置稳定。
uintptr_t findPlayerPointer(std::string_view snbt) {
    size_t searchFrom = 0;
    while (true) {
        size_t typePos = snbt.find("_type_:Player", searchFrom);
        if (typePos == std::string_view::npos) return 0;

        // Locate the enclosing object's start, then find _pointer_ within it.
        size_t braceStart = snbt.rfind('{', typePos);
        size_t ptrPos     = snbt.find("_pointer_:", braceStart == std::string_view::npos ? 0 : braceStart);
        if (ptrPos != std::string_view::npos && ptrPos < typePos + 64) {
            size_t numStart = ptrPos + std::string_view("_pointer_:").size();
            uintptr_t value = 0;
            size_t    i     = numStart;
            for (; i < snbt.size() && snbt[i] >= '0' && snbt[i] <= '9'; ++i) {
                value = value * 10 + static_cast<uintptr_t>(snbt[i] - '0');
            }
            if (i > numStart) return value;
        }
        searchFrom = typePos + 1;
    }
}

// 事件 SNBT 里如果嵌了在线玩家的指针，就拼一个带真实 name/xuid/uuid 的 _player 进去。
std::string enrichWithPlayer(std::string snbt) {
    uintptr_t addr = findPlayerPointer(snbt);
    if (addr == 0) return snbt;

    // 安全闸：只解引用当前在线玩家的指针
    auto addrs = livePlayerAddrs();
    if (addrs.find(addr) == addrs.end()) return snbt;

    auto* player = reinterpret_cast<Player*>(addr);
    if (!player) return snbt;

    // 指针上面已经跟在线列表比对过，这里直接调是安全的（项目关了异常，靠这个校验兜底）
    std::string name = player->getRealName();
    std::string xuid = player->getXuid();
    std::string uuid = player->getUuid().asString();

    // SNBT 字符串字面量里的引号要转义
    auto esc = [](std::string const& s) {
        std::string out;
        out.reserve(s.size() + 2);
        for (char c : s) {
            if (c == '"' || c == '\\') out.push_back('\\');
            out.push_back(c);
        }
        return out;
    };

    // 在最后一个 } 前插入 ,_player:{name:"..",xuid:"..",uuid:".."}
    size_t lastBrace = snbt.rfind('}');
    if (lastBrace == std::string::npos) return snbt;
    std::string inject = ",_player:{name:\"" + esc(name) + "\",xuid:\"" + esc(xuid)
                       + "\",uuid:\"" + esc(uuid) + "\"}";
    snbt.insert(lastBrace, inject);
    return snbt;
}

// ───────────────────────── logging ─────────────────────────

void api_log(LeviRsModHandle mod, int32_t level, LeviRsStr msg) {
    if (!mod) return;
    auto& logger = asMod(mod)->getLogger();
    auto  sv     = fromStr(msg);
    switch (static_cast<ll::io::LogLevel>(level)) {
    case ll::io::LogLevel::Fatal:
        logger.fatal("{}", sv);
        break;
    case ll::io::LogLevel::Error:
        logger.error("{}", sv);
        break;
    case ll::io::LogLevel::Warn:
        logger.warn("{}", sv);
        break;
    case ll::io::LogLevel::Debug:
        logger.debug("{}", sv);
        break;
    case ll::io::LogLevel::Trace:
        logger.trace("{}", sv);
        break;
    case ll::io::LogLevel::Off:
        break;
    case ll::io::LogLevel::Info:
    default:
        logger.info("{}", sv);
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

    auto resolved = resolveEventId(fromStr(eventId));
    if (!resolved) {
        mod->getLogger().error("subscribe_event: unknown or ambiguous event id '{}'", fromStr(eventId));
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
            std::string snbt = enrichWithPlayer(data.toSnbt(SnbtFormat::Minimize));

            struct WriteCtx {
                CompoundTag* data;
                bool         written = false;
            } wctx{&data};

            cb(
                user,
                toStr(idName),
                toStr(snbt),
                &wctx,
                [](void* c, LeviRsStr newSnbt) {
                    auto* w = static_cast<WriteCtx*>(c);
                    if (auto tag = CompoundTag::fromSnbt(std::string_view{newSnbt.ptr, newSnbt.len}); tag) {
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

    // command 命名空间的事件（ExecutingCommandEvent 执行前 / ExecutedCommandEvent
    // 执行后）不走上面这条 DynamicListener 通路——LeviLamina 内部只对 typed
    // listener 派发，通用监听订阅了也收不到回调。这里按解析出来的具体类型另外
    // 挂一个 typed listener，触发时手动拼一份等价的 SNBT，直接调用上面同一个
    // cb——Rust 侧完全无感，还是走通用回调那条路。
    //
    // 两个都是 final 类，可以直接作为模板参数（它们共同的基类 ExecuteCommandEvent
    // 不是 final，不能拿来监听，会编译报错 "Only final classes can be listen"）。
    auto dispatchCommand = [cb, user, idName](
        std::string const& playerName,
        std::string const& xuid,
        std::string const& uuid,
        std::string const& command
    ) {
        if (playerName.empty()) return; // 控制台/其他来源，跳过

        auto esc = [](std::string const& s) {
            std::string out;
            out.reserve(s.size() + 2);
            for (char c : s) {
                if (c == '"' || c == '\\') out.push_back('\\');
                out.push_back(c);
            }
            return out;
        };
        std::string snbt = "{eventId:\"" + idName
                         + "\",name:\"" + esc(playerName)
                         + "\",command:\"" + esc(command)
                         + "\",_player:{name:\"" + esc(playerName)
                         + "\",xuid:\"" + esc(xuid)
                         + "\",uuid:\"" + esc(uuid) + "\"}}";

        CompoundTag dummy;
        struct WriteCtx { CompoundTag* data; bool written = false; } wctx{&dummy};
        cb(user, toStr(idName), toStr(snbt), &wctx,
           [](void*, LeviRsStr) { /* write-back 忽略 */ });
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
                // 基类 ExecuteCommandEvent::commandContext() 返回 const 引用；
                // mOrigin 是指针成员，指针本身 const 但指向的对象不是，
                // 照样能调用非 const 的 getEntity()。
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
        sink(ctx, toStr(id.name));
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
    auto output = ll::command::CommandRegistrar::getServerInstance().executeCommand(fromStr(cmd), origin);
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
        sink(ctx, output.mSuccessCount > 0, toStr(text));
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
    std::string cmdName{fromStr(name)};

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
            std::string{fromStr(description)},
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
                    toStr(args),
                    toStr(originName),
                    &output,
                    [](void* c, LeviRsStr s) { static_cast<CommandOutput*>(c)->success(std::string{s.ptr, s.len}); },
                    [](void* c, LeviRsStr s) { static_cast<CommandOutput*>(c)->error(std::string{s.ptr, s.len}); }
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