/**
 * bridge/Commands.cpp — command execution & registration (ABI v1 + v5 §H).
 *
 * Bedrock cannot unregister commands, so every executor closure is owned by
 * the loader and consults a mutable binding table; a binding whose mod
 * unloads is nulled and answers with an error instead of dangling.
 */
#include "bridge/Api.h"
#include "bridge/Common.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/command/runtime/ParamKind.h"
#include "ll/api/command/runtime/RuntimeCommand.h"
#include "ll/api/command/runtime/RuntimeOverload.h"
#include "ll/api/utils/ErrorUtils.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/platform/UUID.h"
#include "mc/server/ServerLevel.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandOutputMessage.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/server/commands/CommandPosition.h"
#include "mc/server/commands/CommandPositionFloat.h"
#include "mc/server/commands/CommandRawText.h"
#include "mc/server/commands/CommandSelector.h"
#include "mc/server/commands/CommandSelectorResults.h"
#include "mc/server/commands/ServerCommandOrigin.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/Level.h"

#include "RustMod.h"

namespace levi_rs::bridge
{
    bool api_execute_command(LeviRsStr cmd, void* ctx, LeviRsCmdOutputSink sink)
    {
        auto* level = levelReady();
        if (!level) return false;

        ServerCommandOrigin origin{
            "Server",
            static_cast<ServerLevel&>(*level),
            CommandPermissionLevel::Owner,
            0 // overworld; command selectors/positions can address other dimensions
        };
        auto output = ll::command::CommandRegistrar::getServerInstance().executeCommand(cmd, origin);
        if (sink)
        {
            // NOTE: verify against your LL version — CommandOutput exposes
            // mSuccessCount; combined text is easiest via the localized dump.
            std::string text;
            for (auto const& msg : output.mMessages)
            {
                if (!text.empty()) text += '\n';
                text += msg.mMessageId;
                for (auto const& param : msg.mParams)
                {
                    text += ' ';
                    text += param;
                }
            }
            sink(ctx, output.mSuccessCount > 0, text);
        }
        return true;
    }

    namespace
    {
        struct CommandBinding
        {
            RustMod* mod = nullptr;
            LeviRsCommandCb cb = nullptr;
            void* user = nullptr;
        };

        std::mutex gCmdMutex;
        std::unordered_map<std::string, std::shared_ptr<CommandBinding>> gCommands;

        /** Reserve `cmdName` for `mod`; returns the binding, or nullptr if taken by
         *  another live mod. `freshlyRegistered` = the Bedrock-side command must
         *  still be created (first time this name is seen). */
        std::shared_ptr<CommandBinding>
        claimBinding(std::string const& cmdName, RustMod* mod, LeviRsCommandCb cb, void* user, bool& freshlyRegistered)
        {
            std::lock_guard lock(gCmdMutex);
            auto [it, inserted] = gCommands.try_emplace(cmdName, std::make_shared<CommandBinding>());
            auto binding = it->second;
            bool rebind = !inserted && (binding->mod == nullptr || binding->mod == mod);
            if (!inserted && !rebind) return nullptr; // taken by another live mod
            binding->mod = mod;
            binding->cb = cb;
            binding->user = user;
            freshlyRegistered = inserted;
            return binding;
        }

        /** SNBT identity+position of a command origin: {name,type,dim,x,y,z}. */
        std::string originSnbt(CommandOrigin const& origin)
        {
            std::string out = "{name:\"" + snbtEscape(origin.getName()) + "\"";
            out += ",type:" + std::to_string(static_cast<int>(origin.getOriginType()));
            if (auto* entity = origin.getEntity())
            {
                auto pos = entity->getPosition();
                out += ",dim:" + std::to_string(static_cast<int>(entity->getDimensionId()));
                out += ",x:" + std::to_string(pos.x) + ",y:" + std::to_string(pos.y) + ",z:" + std::to_string(pos.z) +
                    "d";
            }
            out += "}";
            return out;
        }
    } // namespace

    bool api_register_command(
        LeviRsModHandle modHandle,
        LeviRsStr name,
        LeviRsStr description,
        int32_t permission,
        LeviRsCommandCb cb,
        void* user
    )
    {
        auto* mod = asMod(modHandle);
        if (!mod || !cb) return false;
        std::string cmdName{name};

        bool fresh = false;
        auto binding = claimBinding(cmdName, mod, cb, user, fresh);
        if (!binding) return false;
        if (!fresh) return true; // command itself already registered with Bedrock

        try
        {
            using namespace ll::command;
            // The runtime overload is deliberately owned by the loader
            // (NativeMod::current()), not the rust mod — Bedrock commands cannot
            // be unregistered, so the executor must outlive any rust mod. Muting
            // via the binding table keeps behaviour predictable across unloads.
            auto& handle = CommandRegistrar::getServerInstance().getOrCreateCommand(
                cmdName,
                std::string{description},
                static_cast<CommandPermissionLevel>(std::clamp<int32_t>(permission, 0, 4))
            );
            handle.runtimeOverload().optional("args", ParamKind::RawText).execute(
                [binding, cmdName](CommandOrigin const& origin, CommandOutput& output, RuntimeCommand const& rt)
                {
                    CommandBinding local;
                    {
                        std::lock_guard lock(gCmdMutex);
                        local = *binding;
                    }
                    if (!local.mod || local.mod->commandsMuted || !local.cb)
                    {
                        output.error("command '" + cmdName + "' is not available (mod disabled)");
                        return;
                    }
                    std::string args;
                    if (auto const& p = rt["args"]; p.hold(ParamKind::RawText))
                    {
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
        }
        catch (...)
        {
            ll::error_utils::printCurrentException(mod->getLogger());
            std::lock_guard lock(gCmdMutex);
            gCommands.erase(cmdName);
            return false;
        }
    }

    // ───────────────────── parameterized commands (ABI v5 §H) ─────────────────────

    namespace
    {
        /** Declared parameter, decoded from the overloads SNBT. */
        struct ParamDecl
        {
            std::string name;
            ll::command::ParamKind::Kind kind;
            std::string enumName; // for Enum / SoftEnum
            bool optional = false;
        };

        std::optional<ll::command::ParamKind::Kind> kindFromString(std::string_view s)
        {
            using K = ll::command::ParamKind::Kind;
            if (s == "int") return K::Int;
            if (s == "bool") return K::Bool;
            if (s == "float") return K::Float;
            if (s == "dimension") return K::Dimension;
            if (s == "string") return K::String;
            if (s == "enum") return K::Enum;
            if (s == "soft_enum") return K::SoftEnum;
            if (s == "actor") return K::Actor;
            if (s == "player") return K::Player;
            if (s == "block_pos") return K::BlockPos;
            if (s == "vec3") return K::Vec3;
            if (s == "raw_text") return K::RawText;
            if (s == "message") return K::Message;
            if (s == "json") return K::JsonValue;
            if (s == "item") return K::Item;
            if (s == "block_name") return K::BlockName;
            if (s == "effect") return K::Effect;
            if (s == "actor_type") return K::ActorType;
            if (s == "command") return K::Command;
            if (s == "relative_float") return K::RelativeFloat;
            if (s == "file_path") return K::FilePath;
            return std::nullopt;
        }

        /** Serialize one parsed parameter into `out` ("name:value,"), if present. */
        void appendParsedParam(
            std::string& out,
            ParamDecl const& decl,
            ll::command::RuntimeCommand const& rt,
            CommandOrigin const& origin
        )
        {
            using K = ll::command::ParamKind::Kind;
            auto const& p = rt[decl.name];
            if (!p.has_value()) return; // optional param not given

            auto key = [&](std::string const& v)
            {
                out += "\"" + snbtEscape(decl.name) + "\":" + v + ",";
            };

            switch (decl.kind)
            {
            case K::Int:
                if (p.hold(K::Int)) key(std::to_string(p.get<K::Int>()));
                break;
            case K::Bool:
                if (p.hold(K::Bool)) key(p.get<K::Bool>() ? "1b" : "0b");
                break;
            case K::Float:
                if (p.hold(K::Float)) key(std::to_string(p.get<K::Float>()) + "f");
                break;
            case K::Dimension:
                if (p.hold(K::Dimension)) key(std::to_string(static_cast<int>(p.get<K::Dimension>())));
                break;
            case K::String:
                if (p.hold(K::String)) key("\"" + snbtEscape(p.get<K::String>()) + "\"");
                break;
            case K::Enum:
                if (p.hold(K::Enum)) key("\"" + snbtEscape(p.get<K::Enum>().name) + "\"");
                break;
            case K::SoftEnum:
                if (p.hold(K::SoftEnum)) key("\"" + snbtEscape(p.get<K::SoftEnum>()) + "\"");
                break;
            case K::RawText:
                if (p.hold(K::RawText)) key("\"" + snbtEscape(p.get<K::RawText>().mText) + "\"");
                break;
            case K::Player:
                if (p.hold(K::Player))
                {
                    std::string list = "[";
                    for (auto* pl : p.get<K::Player>().results(origin))
                    {
                        if (!pl) continue;
                        list += "{name:\"" + snbtEscape(pl->getRealName())
                            + "\",xuid:\"" + snbtEscape(pl->getXuid())
                            + "\",uuid:\"" + snbtEscape(pl->getUuid().asString())
                            + "\",id:" + std::to_string(pl->getOrCreateUniqueID().rawID) + "l},";
                    }
                    if (list.back() == ',') list.pop_back();
                    list += "]";
                    key(list);
                }
                break;
            case K::Actor:
                if (p.hold(K::Actor))
                {
                    std::string list = "[";
                    for (auto* a : p.get<K::Actor>().results(origin))
                    {
                        if (!a) continue;
                        list += "{id:" + std::to_string(a->getOrCreateUniqueID().rawID)
                            + "l,type:\"" + snbtEscape(a->getTypeName()) + "\"},";
                    }
                    if (list.back() == ',') list.pop_back();
                    list += "]";
                    key(list);
                }
                break;
            case K::BlockPos:
                if (p.hold(K::BlockPos))
                {
                    // 0x7FFFFFFF selects the newest coordinate semantics.
                    auto bp = p.get<K::BlockPos>().getBlockPos(0x7FFFFFFF, origin, Vec3::ZERO());
                    key("{x:" + std::to_string(bp.x) + ",y:" + std::to_string(bp.y) + ",z:" + std::to_string(bp.z) +
                        "}");
                }
                break;
            case K::Vec3:
                if (p.hold(K::Vec3))
                {
                    auto v = p.get<K::Vec3>().getPosition(0x7FFFFFFF, origin, Vec3::ZERO());
                    key("{x:" + std::to_string(v.x) + "d,y:" + std::to_string(v.y) + "d,z:" + std::to_string(v.z) +
                        "d}");
                }
                break;
            default:
                // Exotic holders (json/message/item/block_name/effect/actor_type/…)
                // are declared and parsed by Bedrock but not serialized in v5;
                // extend here (append-only semantics: adding fields is safe).
                break;
            }
        }
    } // namespace

    bool api_register_command_ex(
        LeviRsModHandle modHandle,
        LeviRsStr name,
        LeviRsStr description,
        int32_t permission,
        LeviRsStr overloadsSnbt,
        LeviRsCommandCb cb,
        void* user
    )
    {
        auto* mod = asMod(modHandle);
        if (!mod || !cb) return false;
        std::string cmdName{name};

        // Decode {overloads:[[{name,kind,enum?,optional?}, …], …]} up front so a
        // malformed declaration fails before anything is registered with Bedrock.
        auto tag = CompoundTag::fromSnbt(std::string_view{overloadsSnbt});
        if (!tag || !tag->contains("overloads") || !tag->at("overloads").is_array())
        {
            mod->getLogger().error("register_command_ex('{}'): bad overloads SNBT", cmdName);
            return false;
        }
        std::vector<std::vector<ParamDecl>> overloads;
        for (auto const& ovlPtr : tag->at("overloads").get<ListTag>())
        {
            if (!ovlPtr || ovlPtr->getId() != Tag::Type::List) continue;
            std::vector<ParamDecl> decls;
            for (auto const& paramPtr : static_cast<ListTag const&>(*ovlPtr))
            {
                if (!paramPtr || paramPtr->getId() != Tag::Type::Compound) continue;
                auto const& po = static_cast<CompoundTag const&>(*paramPtr);
                if (!po.contains("name") || !po.contains("kind")) continue;
                ParamDecl d;
                d.name = std::string_view{po.at("name")};
                auto kind = kindFromString(std::string_view{po.at("kind")});
                if (!kind)
                {
                    mod->getLogger().error(
                        "register_command_ex('{}'): unknown param kind '{}'",
                        cmdName,
                        std::string_view{po.at("kind")}
                    );
                    return false;
                }
                d.kind = *kind;
                if (po.contains("enum")) d.enumName = std::string_view{po.at("enum")};
                if (po.contains("optional")) d.optional = static_cast<int64_t>(po.at("optional")) != 0;
                decls.push_back(std::move(d));
            }
            overloads.push_back(std::move(decls));
        }
        if (overloads.empty())
        {
            mod->getLogger().error("register_command_ex('{}'): no overloads declared", cmdName);
            return false;
        }

        bool fresh = false;
        auto binding = claimBinding(cmdName, mod, cb, user, fresh);
        if (!binding) return false;
        if (!fresh) return true; // Bedrock side already exists (rebind after reload)

        try
        {
            using namespace ll::command;
            auto& handle = CommandRegistrar::getServerInstance().getOrCreateCommand(
                cmdName,
                std::string{description},
                static_cast<CommandPermissionLevel>(std::clamp<int32_t>(permission, 0, 4))
            );
            for (size_t idx = 0; idx < overloads.size(); ++idx)
            {
                auto const& decls = overloads[idx];
                auto ovl = handle.runtimeOverload();
                for (auto const& d : decls)
                {
                    bool isEnum = d.kind == ParamKind::Enum || d.kind == ParamKind::SoftEnum;
                    // required()/optional() return RuntimeOverload& (a reference
                    // to the same `ovl` object, meant for optional chaining) and
                    // are marked [[nodiscard]]. RuntimeOverload has no operator=
                    // (only a move ctor + dtor are declared), so we can't
                    // reassign `ovl` here — explicitly discard via
                    // static_cast<void> instead to silence C4834/-Wunused-result
                    // without altering behavior.
                    if (isEnum)
                    {
                        if (d.optional) static_cast<void>(ovl.optional(d.name, d.kind, d.enumName));
                        else static_cast<void>(ovl.required(d.name, d.kind, d.enumName));
                    }
                    else
                    {
                        if (d.optional) static_cast<void>(ovl.optional(d.name, d.kind));
                        else static_cast<void>(ovl.required(d.name, d.kind));
                    }
                }
                ovl.execute(
                    [binding, cmdName, decls, idx](
                    CommandOrigin const& origin,
                    CommandOutput& output,
                    RuntimeCommand const& rt
                )
                    {
                        CommandBinding local;
                        {
                            std::lock_guard lock(gCmdMutex);
                            local = *binding;
                        }
                        if (!local.mod || local.mod->commandsMuted || !local.cb)
                        {
                            output.error("command '" + cmdName + "' is not available (mod disabled)");
                            return;
                        }
                        std::string args = "{overload:" + std::to_string(idx) + ",args:{";
                        for (auto const& d : decls)
                        {
                            appendParsedParam(args, d, rt, origin);
                        }
                        if (args.back() == ',') args.pop_back();
                        args += "}}";
                        std::string origin_ = originSnbt(origin);
                        local.cb(
                            local.user,
                            args,
                            origin_,
                            &output,
                            [](void* c, LeviRsStr s) { static_cast<CommandOutput*>(c)->success(std::string{s}); },
                            [](void* c, LeviRsStr s) { static_cast<CommandOutput*>(c)->error(std::string{s}); }
                        );
                    }
                );
            }
            return true;
        }
        catch (...)
        {
            ll::error_utils::printCurrentException(mod->getLogger());
            std::lock_guard lock(gCmdMutex);
            gCommands.erase(cmdName);
            return false;
        }
    }

    namespace
    {
        /** Decode {values:[…]} where each element is a string. */
        std::optional<std::vector<std::string>> decodeStringValues(LeviRsStr snbt)
        {
            auto tag = CompoundTag::fromSnbt(std::string_view{snbt});
            if (!tag || !tag->contains("values") || !tag->at("values").is_array()) return std::nullopt;
            std::vector<std::string> out;
            for (auto const& p : tag->at("values").get<ListTag>())
            {
                if (!p || p->getId() != Tag::Type::String) continue;
                out.emplace_back(static_cast<std::string const&>(static_cast<StringTag const&>(*p)));
            }
            return out;
        }
    } // namespace

    bool api_register_command_enum(LeviRsStr name, LeviRsStr valuesSnbt)
    {
        // {values:[["name",1L], …]} — pairs of (display, index).
        auto tag = CompoundTag::fromSnbt(std::string_view{valuesSnbt});
        if (!tag || !tag->contains("values") || !tag->at("values").is_array()) return false;
        std::vector<std::pair<std::string, uint64_t>> values;
        for (auto const& p : tag->at("values").get<ListTag>())
        {
            if (!p || p->getId() != Tag::Type::List) continue;
            auto const& pair = static_cast<ListTag const&>(*p);
            if (pair.size() < 1) continue;
            auto const& namePtr = pair[0];
            if (!namePtr || namePtr->getId() != Tag::Type::String) continue;
            uint64_t idx = values.size();
            if (pair.size() >= 2 && pair[1] && pair[1]->getId() == Tag::Type::Int64)
            {
                idx = static_cast<uint64_t>(static_cast<Int64Tag const&>(*pair[1]).data);
            }
            values.emplace_back(std::string{static_cast<std::string const&>(static_cast<StringTag const&>(*namePtr))},
                                idx);
        }
        if (values.empty()) return false;
        try
        {
            return ll::command::CommandRegistrar::getServerInstance().tryRegisterRuntimeEnum(
                std::string{name},
                std::move(values)
            );
        }
        catch (...)
        {
            return false;
        }
    }

    bool api_register_command_soft_enum(LeviRsStr name, LeviRsStr valuesSnbt)
    {
        auto values = decodeStringValues(valuesSnbt);
        if (!values) return false;
        try
        {
            return ll::command::CommandRegistrar::getServerInstance().tryRegisterSoftEnum(
                std::string{name}, std::move(*values));
        }
        catch (...)
        {
            return false;
        }
    }

    bool api_update_command_soft_enum(LeviRsStr name, int32_t op, LeviRsStr valuesSnbt)
    {
        auto values = decodeStringValues(valuesSnbt);
        if (!values) return false;
        try
        {
            auto& reg = ll::command::CommandRegistrar::getServerInstance();
            switch (op)
            {
            case 0:
                return reg.setSoftEnumValues(std::string{name}, std::move(*values));
            case 1:
                return reg.addSoftEnumValues(std::string{name}, std::move(*values));
            case 2:
                return reg.removeSoftEnumValues(std::string{name}, std::move(*values));
            default:
                return false;
            }
        }
        catch (...)
        {
            return false;
        }
    }

    void commandsOnRustModGone(RustMod* mod)
    {
        std::lock_guard lock(gCmdMutex);
        for (auto& [name, binding] : gCommands)
        {
            if (binding->mod == mod)
            {
                binding->mod = nullptr;
                binding->cb = nullptr;
                binding->user = nullptr;
            }
        }
    }
} // namespace levi_rs::bridge
