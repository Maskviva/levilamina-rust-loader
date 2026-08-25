#include "bridge/Common.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#ifdef LEVI_RS_FEATURE_MORE_DIMENSIONS
#include "more_dimensions/include/dim/CustomDimensionConfig.h"
#include "more_dimensions/include/base/NativeDimensions.h"

#include "mc/util/BidirectionalUnorderedMap.h"
#include "mc/world/level/dimension/VanillaDimensions.h"
#endif

#include <cmath>
#include <cstdint>
#include <mutex>
#include <unordered_set>

#include "ll/api/io/Logger.h"
#include "ll/api/io/LoggerRegistry.h"
#include "ll/api/service/GamingStatus.h"

// Server build uses ll::service::getLevel() from Bedrock.h; client build
// uses ll::service::getMultiPlayerLevel() from TargetedBedrock.h (src-client/).
// CommandRegistrar / ServerLevel / ServerCommandOrigin are server-only.
#ifdef LEVI_RS_TARGET_CLIENT
#include "ll/api/service/TargetedBedrock.h"
#else
#include "ll/api/service/Bedrock.h"
#include "ll/api/command/CommandRegistrar.h"
#include "mc/server/ServerLevel.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandOutputMessage.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/server/commands/ServerCommandOrigin.h"
#endif

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/platform/UUID.h"
#include "mc/world/Container.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/SaveContext.h"
#include "mc/world/item/SaveContextFactory.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/provider/ActorEquipment.h"
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
#ifdef LEVI_RS_TARGET_CLIENT
            auto level = ll::service::getMultiPlayerLevel();
#else
            auto level = ll::service::getLevel();
#endif
            return level ? &*level : nullptr;
        }

        // 定义在本文件靠后的位置，这里先声明。
        ll::io::Logger& bridgeLogger();

        namespace
        {
            /**
             * 同一个 id 只抱怨一次。这些错误一旦发生就会每 tick 复现，
             * 不去重的话日志会被冲垮，反而看不见第一条。
             */
            bool firstComplaintFor(int32_t dimId)
            {
                static std::mutex                  mtx;
                static std::unordered_set<int32_t> seen;
                std::lock_guard                    lock{mtx};
                return seen.insert(dimId).second;
            }
        } // namespace

        BlockSource* blockSourceOf(int32_t dimId)
        {
            auto* level = levelReady();
            if (!level) return nullptr;

            // 已经建好的维度：直接拿。
            if (auto dim = level->getDimension(DimensionType{dimId}).lock())
            {
                return &dim->getBlockSourceFromMainChunkSource();
            }

            // 没人进过的自定义维度只存在于注册表里，Dimension 对象还没被创建。
            // 要往里写方块（或者传送人进去）就得先把它逼出来。

#ifdef LEVI_RS_FEATURE_MORE_DIMENSIONS
            if (dimId >= 3)
            {
                auto const name = ::more_dimensions::dimensionNameOf(dimId);
                if (name.empty())
                {
                    if (firstComplaintFor(dimId))
                    {
                        bridgeLogger().error(
                            "维度 {} 不在 loader 的注册台账里。本次启动注册成功的自定义维度：{}",
                            dimId,
                            ::more_dimensions::describeRegisteredDimensions()
                        );
                    }
                    return nullptr;
                }

                // 按名字走引擎原生的 DimensionManager::getOrCreateDimension。
                //
                // 这是 26.20 上唯一可靠的路径：按 id 的那个重载要先经
                // NameIdStore 反查名字，而只有真正由 serverRegisterCustomDimension
                // 注册过的维度才在那张表里。之前的代码只走按 id 的路径，于是
                // 手抄注册出来的 id 3 永远建不出维度 —— 这就是"传送失败"的直接原因。
                if (auto* nativeDim = ::more_dimensions::native::getOrCreateByName(name))
                {
                    return &nativeDim->getBlockSourceFromMainChunkSource();
                }

                if (firstComplaintFor(dimId))
                {
                    bridgeLogger().error(
                        "维度 '{}'（id {}）没能被创建出来：DimensionManager::getOrCreateDimension 返回空。"
                        "引擎侧 active={}。多半是这个维度从来没有真正在引擎里注册过 —— "
                        "检查启动日志里 more_dimensions 的注册结果。",
                        name,
                        dimId,
                        ::more_dimensions::native::isActive(dimId)
                    );
                }
                return nullptr;
            }
#endif

            // 原版维度（以及关掉 more_dimensions 的构建）。
            if (dimensionSelector(dimId).empty()) return nullptr;
            auto dim = level->getOrCreateDimension(DimensionType{dimId}).lock();
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
            // Armour and hand slots ARE real Containers — they just aren't reached
            // through Player. `ActorEquipment::getArmorContainer(EntityContext&)` and
            // `getHandContainer(...)` hand back a SimpleContainer, which derives from
            // Container, so the whole existing item read/write path works unchanged.
            //
            // The previous note here claimed these were "equipment slots, not
            // Container objects" and pointed at the actor snapshot NBT instead. That
            // was wrong on both counts: they are Containers, and the snapshot NBT is
            // the *save* representation — stale by however long it's been since the
            // last flush. LegacyScriptEngine does it this way
            // (PlayerAPI.cpp: getArmor -> ActorEquipment::getArmorContainer).
            case 2: // armour: head, torso, legs, feet
                return &ActorEquipment::getArmorContainer(p->getEntityContext());
            case 3: // hands: slot 0 = main hand, slot 1 = offhand
                return &ActorEquipment::getHandContainer(p->getEntityContext());
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
                // 玩家可以往输入框里粘贴带换行/制表的文本；裸的控制字符塞进
                // SNBT 字面量里，解析端要么读错要么直接报错。
                switch (c)
                {
                case '"':
                case '\\':
                    out.push_back('\\');
                    out.push_back(c);
                    break;
                case '\n':
                    out += "\\n";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                default:
                    out.push_back(c);
                    break;
                }
            }
            return out;
        }

        bool addressOwnedBy(void const* moduleBase, void const* fn)
        {
            if (!moduleBase || !fn) return false;
#ifdef _WIN32
            HMODULE owner = nullptr;
            if (!::GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(fn),
                &owner
            ))
            {
                return false;
            }
            return static_cast<void const*>(owner) == moduleBase;
#else
            (void)moduleBase;
            (void)fn;
            return false;
#endif
        }

        std::uint64_t nextListenerId()
        {
            static std::uint64_t next = 1;
            return next++;
        }

        LeviRsListenerHandle listenerHandleOf(std::uint64_t id)
        {
            return reinterpret_cast<LeviRsListenerHandle>(static_cast<uintptr_t>(id));
        }

        std::uint64_t listenerIdOf(LeviRsListenerHandle handle)
        {
            return static_cast<std::uint64_t>(reinterpret_cast<uintptr_t>(handle));
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
                        // `pos` is written as a NAMED compound {x,y,z}, not as
                        // the array that ll::reflection produces for BlockPos /
                        // Vec3 (serialize_impl picks the IsVectorBase overload,
                        // which builds `J::array()`). Consumers reading a
                        // position out of an event payload therefore have to
                        // cope with both shapes, and a hand-written compound is
                        // the one that is unambiguous — so this fallback is the
                        // shape that is always readable, whatever the event's
                        // own fields look like.
                        Vec3 const& ppos = player->getPosition();
                        copy["_player"] = CompoundTagVariant::object(
                            {
                                {"name", CompoundTagVariant(player->getRealName())},
                                {"xuid", CompoundTagVariant(player->getXuid())},
                                {"uuid", CompoundTagVariant(player->getUuid().asString())},
                                {"pos", CompoundTagVariant::object(
                                    {
                                        {"x", CompoundTagVariant(static_cast<int>(std::floor(ppos.x)))},
                                        {"y", CompoundTagVariant(static_cast<int>(std::floor(ppos.y)))},
                                        {"z", CompoundTagVariant(static_cast<int>(std::floor(ppos.z)))}
                                    }
                                )}
                            }
                        );

                        // Dimension, at the top level as `dim`.
                        //
                        // This was missing, and its absence was actively
                        // harmful rather than merely incomplete: consumers
                        // default a missing `dim` to 0, so every event in a
                        // custom dimension looked like it happened in the
                        // overworld. A land-protection plugin reading that
                        // checks the wrong world's rules — it denies on the
                        // overworld and permits everywhere else.
                        //
                        // Only set when absent: some events carry their own
                        // `dim` and theirs is more specific than the player's
                        // (a block event's dimension is the block's).
                        if (!copy.contains("dim"))
                        {
                            copy["dim"] =
                                CompoundTagVariant(static_cast<int>(player->getDimensionId()));
                        }
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
#ifdef LEVI_RS_TARGET_CLIENT
            // No server console on the client build — commands are server-only.
            (void)cmd;
            return false;
#else
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
#endif
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

        /**
         * Resolve ANY registered dimension id to the name `/execute in` wants.
         * Unknown ids yield an EMPTY string and the caller must fail — never
         * fall back to overworld.
         *
         * ── Why this does NOT touch VanillaDimensions ──────────────────────
         *
         * The obvious implementation is `VanillaDimensions::toString(id)` plus
         * a `fromString(name) == id` round-trip. That crashes the server.
         *
         * `toString` is declared `LLNDAPI static std::string toString(...)`,
         * but the object it actually hands back does not match MSVC's
         * std::string layout: the returned buffer has a stack pointer where
         * `_Bx._Ptr` belongs and the text bytes where `_Mysize` belongs. So
         * `empty()` reads garbage (non-zero, looks fine), and the next call
         * that consumes the string memcpy's with a length taken from the text
         * itself. Observed live: teleporting to a dimension named "red" ran
         * `memcpy(dst, src, 0x646572)` — 0x646572 being the bytes 'r','e','d'
         * read as a size — and took down the server with an access violation
         * in VCRUNTIME140.
         *
         * (The original `api_md_get_dimension_id` got away with `fromString`
         * only because it built its own well-formed std::string from a
         * LeviRsStr. The moment a `toString` result is fed back in, it dies.)
         *
         * So: resolve from data the loader owns outright. Vanilla ids are
         * fixed, and every custom dimension the loader registers is recorded
         * in CustomDimensionConfig's dimensionList (a plain
         * std::unordered_map<std::string, DimensionInfo>) both when it is
         * first created and when it is reloaded at startup. No BDS ABI, no
         * symbol resolution, no way to get a malformed string back.
         */
        ll::io::Logger& bridgeLogger()
        {
            static auto log = ll::io::LoggerRegistry::getInstance().getOrCreate("levi_rs");
            return *log;
        }

        std::string dimensionSelector(int32_t dim)
        {
            switch (dim)
            {
            case 0:
                return "overworld";
            case 1:
                return "nether";
            case 2:
                return "the_end";
            default:
                break;
            }
            if (dim < 0) return {};

#ifdef LEVI_RS_FEATURE_MORE_DIMENSIONS
            // 数据源按可信度排序。
            //
            // 1) loader 注册台账 —— 里面记的是**引擎实际返回的 id**，注册成功
            //    时才写入，所以不会跟引擎漂移。
            if (auto name = ::more_dimensions::dimensionNameOf(dim); !name.empty()) return name;

            // 2) 引擎的 DimensionMap。台账是进程内的，热重载或者别的插件注册的
            //    维度只会出现在这里。
            {
                auto const& dimMap = ::VanillaDimensions::DimensionMap();
                auto const  hit    = dimMap.mLeft.find(DimensionType{dim});
                if (hit != dimMap.mLeft.end()) return hit->second;
            }

            // 3) 配置镜像。**只作兜底**：上一版把它当唯一数据源，一旦运行时 id
            //    跟配置对不上（旧的 addDimension 用 emplace 写配置，key 已存在时
            //    是 no-op，必然对不上），这里就返回空，调用方只能报一句语焉不详
            //    的"传送失败"。留着它是为了兼容，但命中时要吼一声。
            {
                auto const& known = ::more_dimensions::CustomDimensionConfig::getConfig().dimensionList;
                for (auto const& [name, info] : known)
                {
                    if (info.dimId != dim) continue;
                    if (firstComplaintFor(dim))
                    {
                        bridgeLogger().warn(
                            "维度 {} 只能从 loader 配置里解析出名字 '{}'，引擎侧查不到 —— "
                            "两边已经漂移，这个维度多半用不了",
                            dim,
                            name
                        );
                    }
                    return name;
                }

                if (firstComplaintFor(dim))
                {
                    std::string list;
                    for (auto const& [name, info] : known)
                    {
                        if (!list.empty()) list += ", ";
                        list += name + "=" + snbtNum(info.dimId);
                    }
                    if (list.empty()) list = "(无)";
                    bridgeLogger().warn(
                        "维度 {} 没有注册。本次启动注册成功的：{}；配置文件里记的：{}",
                        dim,
                        ::more_dimensions::describeRegisteredDimensions(),
                        list
                    );
                }
            }
#endif
            return {};
        }

        std::string playerSummarySnbt(Player& p)
        {
            auto pos = p.getPosition();
            std::string out = "{name:\"" + snbtEscape(p.getRealName())
                + "\",xuid:\"" + snbtEscape(p.getXuid())
                + "\",uuid:\"" + snbtEscape(p.getUuid().asString())
                + "\",dim:" + snbtNum(static_cast<int>(p.getDimensionId()))
                + ",x:" + snbtNum(pos.x)
                + ",y:" + snbtNum(pos.y)
                + ",z:" + snbtNum(pos.z) + "d}";
            return out;
        }
    } // namespace bridge
} // namespace levi_rs