#include "bridge/Common.h"

#include <cmath>
#include <cstdint>
#include <unordered_set>

#include "ll/api/service/Bedrock.h"
#include "ll/api/service/GamingStatus.h"
#include "ll/api/command/CommandRegistrar.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/platform/UUID.h"
#include "mc/server/ServerLevel.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandOutputMessage.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/server/commands/ServerCommandOrigin.h"
#include "mc/world/Container.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/SaveContext.h"
#include "mc/world/item/SaveContextFactory.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/inventory/EnderChestContainer.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/dimension/Dimension.h"

#include "RustMod.h"

namespace levi_rs
{
    RustMod* asMod(LeviRsModHandle h) { return static_cast<RustMod*>(h); }

    namespace bridge
    {
        double nbtToDouble(CompoundTagVariant const& val, double def)
        {
            if (val.is_number_float()) return static_cast<double>(val);
            if (val.is_number_integer()) return static_cast<double>(static_cast<int64_t>(val));
            return def;
        }

        Level* levelReady()
        {
            auto level = ll::service::getLevel();
            return level ? &*level : nullptr;
        }

        BlockSource* blockSourceOf(int32_t dimId)
        {
            auto* level = levelReady();
            if (!level) return nullptr;
            auto dim = level->getDimension(DimensionType{dimId}).lock();
            if (!dim) return nullptr;
            return &dim->getBlockSourceFromMainChunkSource();
        }

        Player* resolvePlayer(LeviRsPlayerSel sel)
        {
            auto* level = levelReady();
            if (!level || sel.value.empty()) return nullptr;
            std::string_view wanted = sel.value;

            Player* found = nullptr;
            level->forEachPlayer([&](Player& p)
            {
                bool hit = false;
                switch (sel.kind)
                {
                case 0: // account name
                    hit = (p.getRealName() == wanted);
                    break;
                case 1: // xuid
                    hit = (p.getXuid() == wanted);
                    break;
                case 2: // uuid
                    hit = (p.getUuid().asString() == wanted);
                    break;
                default:
                    break;
                }
                if (hit)
                {
                    found = &p;
                    return false;
                }
                return true;
            });
            if (!found && sel.kind == 0)
            {
                // Second pass: display name (nametag plugins etc.).
                level->forEachPlayer([&](Player& p)
                {
                    if (std::string_view{p.getNameTag()} == wanted)
                    {
                        found = &p;
                        return false;
                    }
                    return true;
                });
            }
            return found;
        }

        Actor* resolveActor(LeviRsActorId id)
        {
            auto* level = levelReady();
            if (!level || id == 0) return nullptr;
            ActorUniqueID uid{};
            uid.rawID = id;
            return level->fetchEntity(uid, /*getRemoved*/ false);
        }

        Container* resolveContainer(LeviRsContainerRef ref)
        {
            if (ref.which == 4)
            {
                // Block container (chest / hopper / …) at (dim, pos).
                auto* bs = blockSourceOf(ref.dim);
                if (!bs) return nullptr;
                auto* be = bs->getBlockEntity(BlockPos{ref.x, ref.y, ref.z});
                if (!be) return nullptr;
                return be->getContainer();
            }
            Player* p = resolvePlayer(ref.player);
            if (!p) return nullptr;
            switch (ref.which)
            {
            case 0: // main inventory
                return &p->getInventory();
            case 1:
                {
                    // ender chest
                    auto ec = p->getEnderChestContainer();
                    return ec ? ec.as_ptr() : nullptr;
                }
            // Armor (2) and offhand (3) are equipment slots, not Container objects,
            // in this engine version; they stay unsupported here (return nullptr and
            // let the safe layer surface "unsupported"). Their read path is the
            // actor snapshot NBT ("Armor"/"Offhand" lists).
            default:
                return nullptr;
            }
        }

        std::string snbtEscape(std::string_view s)
        {
            std::string out;
            out.reserve(s.size() + 2);
            for (char c : s)
            {
                if (c == '"' || c == '\\') out.push_back('\\');
                out.push_back(c);
            }
            return out;
        }

        std::string itemToSnbt(ItemStack const& item)
        {
            auto ctx = SaveContextFactory::createCloneSaveContext();
            auto tag = item.save(*ctx);
            if (!tag) return "{}";
            return tag->toSnbt(SnbtFormat::Minimize);
        }

        std::optional<ItemStack> itemFromSnbt(std::string_view snbt)
        {
            auto tag = CompoundTag::fromSnbt(snbt);
            if (!tag) return std::nullopt;
            return ItemStack::fromTag(*tag);
        }

        // Live player addresses. Only pointers found here get dereferenced.
        static std::unordered_set<uintptr_t> livePlayerAddrs()
        {
            std::unordered_set<uintptr_t> addrs;
            auto* level = levelReady();
            if (!level) return addrs;
            level->forEachPlayer([&](Player& p)
            {
                addrs.insert(reinterpret_cast<uintptr_t>(&p));
                return true;
            });
            return addrs;
        }

        // Find an embedded pointer stub in an event's tag: a top-level field
        // holding a compound with `_type_ == typeName` and a numeric
        // `_pointer_`. LL's generic reflection emits these for non-serialisable
        // fields (Player&, ActorDefinitionIdentifier const&, …). Returns 0 if
        // absent. Read-only; never dereferences.
        static uintptr_t findPointerOfType(CompoundTag const& data, std::string_view typeName)
        {
            for (auto const& entry : data.mTags)
            {
                auto const& value = entry.second;
                if (!value.is_object()) continue;
                auto const& obj = value.get<CompoundTag>();
                if (!obj.contains("_type_") || !obj.contains("_pointer_")) continue;

                auto const& typeVar = obj.at("_type_");
                if (!typeVar.is_string() || std::string_view(typeVar) != typeName) continue;

                auto const& ptrVar = obj.at("_pointer_");
                if (!ptrVar.is_number()) continue;
                return static_cast<uintptr_t>(static_cast<int64_t>(ptrVar));
            }
            return 0;
        }

        static uintptr_t findPlayerPointer(CompoundTag const& data)
        {
            return findPointerOfType(data, "Player");
        }

        // Single enrichment pass for the generic event path. Walks the event's
        // reflected pointer stubs and splices in decoded fields on ONE copy,
        // serialising once:
        //   Player&                    → `_player` {name,xuid,uuid}
        //   ActorDefinitionIdentifier& → `_identifier` {full,namespace,name}
        // Each injection is independent and best-effort; an event with neither
        // stub serialises unchanged. Read-only, no virtual calls on the decoded
        // pointers (Player uses accessor methods; identifier reads fields).
        std::string enrichEventData(CompoundTag const& data)
        {
            CompoundTag copy = data;
            bool changed = false;

            // Player: only dereference pointers of currently-online players.
            if (uintptr_t addr = findPlayerPointer(data); addr != 0)
            {
                auto addrs = livePlayerAddrs();
                if (addrs.find(addr) != addrs.end())
                {
                    if (auto* player = reinterpret_cast<Player*>(addr))
                    {
                        copy["_player"] = CompoundTagVariant::object(
                            {
                                {"name", CompoundTagVariant(player->getRealName())},
                                {"xuid", CompoundTagVariant(player->getXuid())},
                                {"uuid", CompoundTagVariant(player->getUuid().asString())}
                            }
                        );
                        changed = true;
                    }
                }
            }

            // ActorDefinitionIdentifier: sanity-gate the pointer, read the three
            // std::string fields (TypedStorage object wrappers → .get()).
            if (uintptr_t addr = findPointerOfType(data, "ActorDefinitionIdentifier");
                addr >= 0x10000 && (addr & 0x7) == 0)
            {
                if (auto* id = reinterpret_cast<ActorDefinitionIdentifier*>(addr))
                {
                    copy["_identifier"] = CompoundTagVariant::object(
                        {
                            {"full", CompoundTagVariant(id->mFullName.get())},
                            {"namespace", CompoundTagVariant(id->mNamespace.get())},
                            {"name", CompoundTagVariant(id->mIdentifier.get())}
                        }
                    );
                    changed = true;
                }
            }

            return (changed ? copy : data).toSnbt(SnbtFormat::Minimize);
        }

        std::string enrichWithPlayer(CompoundTag const& data)
        {
            return enrichEventData(data);
        }

        bool runConsoleCommand(std::string const& cmd)
        {
            auto* level = levelReady();
            if (!level) return false;
            ServerCommandOrigin origin{
                "Server",
                static_cast<ServerLevel&>(*level),
                CommandPermissionLevel::Owner,
                0
            };
            auto output = ll::command::CommandRegistrar::getServerInstance().executeCommand(cmd, origin);
            return output.mSuccessCount > 0;
        }

        char const* dimensionName(int dim)
        {
            switch (dim)
            {
            case 1:
                return "nether";
            case 2:
                return "the_end";
            default:
                return "overworld";
            }
        }

        std::string playerSummarySnbt(Player& p)
        {
            auto pos = p.getPosition();
            std::string out = "{name:\"" + snbtEscape(p.getRealName())
                + "\",xuid:\"" + snbtEscape(p.getXuid())
                + "\",uuid:\"" + snbtEscape(p.getUuid().asString())
                + "\",dim:" + std::to_string(static_cast<int>(p.getDimensionId()))
                + ",x:" + std::to_string(pos.x)
                + ",y:" + std::to_string(pos.y)
                + ",z:" + std::to_string(pos.z) + "d}";
            return out;
        }
    } // namespace bridge
} // namespace levi_rs
