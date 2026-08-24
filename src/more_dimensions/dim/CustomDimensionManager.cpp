#include "more_dimensions/include/dim/CustomDimensionManager.h"
#include "more_dimensions/include/dim/ChunkTrace.h"
#include "more_dimensions/include/dim/DimensionRules.h"
#include "more_dimensions/include/dim/CustomDimensionConfig.h"
#include "more_dimensions/include/dim/DimensionHeight.h"
#include "more_dimensions/include/base/NativeDimensions.h"
#include "more_dimensions/include/base/SimpleCustomDimension.h"

#include "magic_enum.hpp"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <string_view>
#include <unordered_set>

#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/io/Logger.h"
#include "ll/api/io/LoggerRegistry.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/memory/Memory.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/server/DedicatedServer.h"
#include "mc/world/level/GeneratorType.h"
#include "mc/server/PropertiesSettings.h"
#include "mc/util/BidirectionalUnorderedMap.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/dimension/VanillaDimensions.h"
#include "mc/world/level/storage/LevelStorage.h"

namespace more_dimensions
{
    namespace
    {
        ll::io::Logger& logger()
        {
            static auto log = ll::io::LoggerRegistry::getInstance().getOrCreate("more_dimensions");
            return *log;
        }

        // 引擎原生注册需要一份 DimensionDefinition（高度范围 + 生成器类型）。
        //
        // 高度**不再从 payload 里读**。之前是 readHeight(nbt, "heightMin", -64)，
        // 而 PlotDimension / SimpleCustomDimension 的构造函数里是写死的常量：
        // payload 里一旦出现 heightMin，客户端拿到的定义和服务端实际发的区块
        // 几何就会对不上，表现是进维度加载完直接闪退。两边现在都取
        // DimensionHeight.h 里那一份常量。
        //
        // 真要做"每个维度不同高度"，得让 Dimension 的构造函数也从同一份
        // payload 取值，而不是只改这一头。

        /**
         * SimpleCustomDimension 把生成器类型以 magic_enum 名字的形式存进
         * payload；PlotDimension 没有这一项（它自己接管 createGenerator）。
         *
         * 这条注释以前写的是"这个值**不**决定地形长什么样"。对 PlotDimension
         * 成立，对 SimpleCustomDimension **不成立** —— 它的 createGenerator 就是
         * 拿这个值做 switch 的。所以这里读不到时的回退不是无害的：它会决定玩家
         * 进去看到的是平坦、下界还是虚空，因此每一次回退都要出声。
         *
         * 另外要记住：这个名字是维度**第一次创建时**由 generateNewData 写下的，
         * 之后再也不会被参数覆盖。建错了就只能改这个文件或者删维度重建。
         */
        GeneratorType readGeneratorType(CompoundTag const& nbt)
        {
            if (!nbt.contains("generatorType"))
            {
                // PlotDimension 本来就不写这一项，属于正常情况，用 debug 级别。
                logger().debug("维度数据里没有 generatorType，按 Flat 处理");
                return GeneratorType::Flat;
            }
            std::string stored;
            try
            {
                auto const name = static_cast<std::string_view>(nbt.at("generatorType"));
                stored.assign(name);
                if (auto parsed = magic_enum::enum_cast<GeneratorType>(name)) return *parsed;
            }
            catch (...)
            {
            }
            logger().error(
                "维度数据里的 generatorType='{}' 无法解析，退回 Flat —— 地形会和创建时选的不一样",
                stored
            );
            return GeneratorType::Flat;
        }
    } // namespace

    namespace CustomDimensionHookList
    {
        LL_TYPE_STATIC_HOOK(
            VanillaDimensionsConvertPointHook,
            HookPriority::Normal,
            VanillaDimensions,
            VanillaDimensions::convertPointBetweenDimensions,
            bool,
            Vec3 const&                    oldPos,
            Vec3&                          toPos,
            DimensionType                  oldDim,
            DimensionType                  toDim,
            DimensionConversionData const& data
        )
        {
            if (oldDim <= 2 && toDim <= 2) return origin(oldPos, toPos, oldDim, toDim, data);
            toPos = oldPos;
            return true;
        };

        /*
         * NOTE — do NOT add a second hook on fromSerializedInt.
         *
         * Upstream MoreDimensions registers this function twice
         * (VanillaDimensionsFromSerializedIntHook + ...HookI) because on the
         * BDS generation it targets there were two distinct overloads with the
         * same mangled shape. In the 26.20 SDK there is exactly one hookable
         * overload — `Bedrock::Result<DimensionType>(Bedrock::Result<int>&&)`.
         * The other, `DimensionType fromSerializedInt(int)`, is MCFOLD: the
         * linker folded it into an identical body elsewhere, so hooking it
         * detours unrelated functions. Registering the hookable one twice makes
         * the second detour trampoline into the first, and `origin()` stops
         * meaning what the code thinks it means.
         */
        LL_TYPE_STATIC_HOOK(
            VanillaDimensionsFromSerializedIntHook,
            HookPriority::Normal,
            VanillaDimensions,
            VanillaDimensions::fromSerializedInt,
            Bedrock::Result<DimensionType>,
            Bedrock::Result<int>&& dim
        )
        {
            // 上一版有两个问题：
            //
            //  1. 无条件 `*dim`。Bedrock::Result 里装的可能是错误，这时候解引用
            //     是未定义行为 —— 存档里一个坏字段就能让服务端崩在这里。
            //  2. 连 0/1/2 都不走 origin，等于把原版三维度的反序列化也接管了。
            //
            // 判据也换了：走 navdim 之后权威是引擎的 NameIdStore（我们的台账是
            // 它的镜像），DimensionMap 降级成给 fromString / dimensionSelector
            // 用的兜底镜像。
            if (!dim) return origin(std::move(dim));

            int const value = *dim;
            if (value >= 0 && value <= 2) return origin(std::move(dim));

            if (!dimensionNameOf(value).empty()) return DimensionType{value};
            if (VanillaDimensions::DimensionMap().mLeft.contains(DimensionType{value}))
            {
                return DimensionType{value};
            }
            return VanillaDimensions::Undefined();
        };

        LL_TYPE_INSTANCE_HOOK(
            LevelStorageLoadServerPlayerDataHook,
            HookPriority::Normal,
            LevelStorage,
            &LevelStorage::loadServerPlayerData,
            std::unique_ptr<class CompoundTag>,
            Player const& client,
            bool          isXboxLive
        )
        {
            auto result = origin(client, isXboxLive);
            if (!result) return result;

            // 玩家上次退出时所在的维度这次没注册上（配置被删、注册失败……）时，
            // 把 Y 顶到哨兵值，让引擎重新给他找落点，而不是把人放进一个不存在
            // 的维度里。
            //
            // 判据同上：先问引擎台账，DimensionMap 只兜底。原版三维度直接放行。
            if (!result->contains("DimensionId")) return result;
            if (!result->contains("Pos")) return result;

            int savedDim = 0;
            try
            {
                savedDim = static_cast<int>(result->at("DimensionId"));
            }
            catch (...)
            {
                return result;
            }

            bool const known = (savedDim >= 0 && savedDim <= 2) || !dimensionNameOf(savedDim).empty()
                            || VanillaDimensions::DimensionMap().mLeft.contains(DimensionType{savedDim});
            if (!known)
            {
                logger().warn("玩家存档里的维度 {} 当前不可用，重置落点", savedDim);
                result->at("Pos")[1] = FloatTag{0x7fff};
            }
            return result;
        }

        /*
         * LL_AUTO_* hooks install themselves during static initialisation —
         * which is the point, since initializeHttp runs long before any
         * dimension is registered. It must therefore NOT also appear in the
         * HookRegistrar below; the old code listed it in both places, so the
         * detour was installed twice and its refcount never returned to zero on
         * unhook. See HookReg at the end of this block.
         */
        LL_AUTO_TYPE_INSTANCE_HOOK(
            PropertiesSettingsClientSideGenHook,
            HookPriority::Normal,
            DedicatedServer,
            &DedicatedServer::initializeHttp,
            void,
            PropertiesSettings const& properties
        )
        {
            auto& properties_modiy = const_cast<PropertiesSettings&>(properties);
            properties_modiy.mClientSideGenerationEnabled = false;
            return origin(properties_modiy);
        }

        using HookReg = ll::memory::HookRegistrar<
            VanillaDimensionsConvertPointHook,
            VanillaDimensionsFromSerializedIntHook,
            LevelStorageLoadServerPlayerDataHook>;
    } // namespace CustomDimensionHookList

    struct CustomDimensionManager::Impl
    {
        std::atomic<int> mNewDimensionId{3};
        std::mutex       mMapMutex;

        struct DimensionInfo
        {
            DimensionType id;
            CompoundTag   nbt;
        };

        /** name -> {id, payload} for every entry we could fully restore. */
        std::unordered_map<std::string, DimensionInfo> customDimensionMap;

        /**
         * Names whose persisted entry exists but whose SNBT payload could not
         * be parsed. The id stays claimed so it is never handed to a different
         * dimension; the payload is regenerated on the next addDimension call.
         *
         * This is the fix for the id/config desync: the old code did a bare
         * `continue` here, which dropped the entry from customDimensionMap but
         * left it in dimensionList, and then allocated ids from
         * `3 + customDimensionMap.size()`. The result was a runtime id that did
         * not match the config, and a dimensionList lookup that returned
         * nothing — which surfaced downstream as "teleport failed ... dimension
         * N is not registered".
         */
        std::unordered_map<std::string, int> salvagedIds;

        /** Every id claimed by the config, so a reload can't hand one out twice. */
        std::unordered_set<int> usedIds;

        std::unordered_set<std::string> registeredDimension;
    };

    CustomDimensionManager::CustomDimensionManager() : impl(std::make_unique<Impl>())
    {
        std::lock_guard lock{impl->mMapMutex};
        CustomDimensionConfig::setDimensionConfigPath();
        CustomDimensionConfig::loadConfigFile();

        // Ids are allocated from one past the highest id the config already
        // claims — never from the map's size. Size-based allocation collides
        // the moment any entry fails to load.
        int highest = 2;

        for (auto& [name, info] : CustomDimensionConfig::getConfig().dimensionList)
        {
            if (info.dimId < 3)
            {
                logger().error(
                    "dimension_config: '{}' has id {}, but custom dimension ids start at 3; it will be re-assigned",
                    name,
                    info.dimId
                );
                continue;
            }
            if (!impl->usedIds.insert(info.dimId).second)
            {
                logger().error(
                    "dimension_config: id {} is claimed by more than one dimension; '{}' will be re-assigned",
                    info.dimId,
                    name
                );
                continue;
            }
            highest = std::max(highest, info.dimId);

            auto nbtTag = CompoundTag::fromSnbt(info.sNbt);
            if (!nbtTag)
            {
                logger().error(
                    "dimension_config: stored data for '{}' (id {}) is unreadable; keeping the id and regenerating "
                    "the data on registration",
                    name,
                    info.dimId
                );
                impl->salvagedIds.emplace(name, info.dimId);
                continue;
            }
            impl->customDimensionMap.emplace(name, Impl::DimensionInfo{DimensionType{info.dimId}, *nbtTag});
        }

        impl->mNewDimensionId.store(highest + 1);

        // 26.20 起自定义维度由引擎原生支持（navdim），FakeDimensionId 那一整套
        // 包改写已经删除：客户端通过 DimensionDataPacket 真正认识这些维度，
        // 区块、子区块、切换都带真实维度 id 走原版流程。
        CustomDimensionHookList::HookReg::hook();

        // 排查用，默认不装；见 ChunkTrace.h。
        registerChunkTraceHooks();

        // 按维度生效的行为规则（生成 / 弹射物）。**无条件装**：没有设过规则的
        // 维度会直接 origin()，所以装上去对原版维度没有任何影响。
        registerDimensionRuleHooks();
    }

    CustomDimensionManager::~CustomDimensionManager()
    {
        unregisterDimensionRuleHooks();
        unregisterChunkTraceHooks();
        CustomDimensionHookList::HookReg::unhook();
    }

    CustomDimensionManager& CustomDimensionManager::getInstance()
    {
        static CustomDimensionManager instance{};
        return instance;
    }

    DimensionType CustomDimensionManager::getDimensionIdFromName(std::string const& dimName)
    {
        return VanillaDimensions::fromString(dimName);
    }

    DimensionType CustomDimensionManager::addDimension(
        std::string const&                  dimName,
        std::function<DimensionFactoryT>    factory,
        std::function<CompoundTag()> const& data
    )
    {
        std::lock_guard lock{impl->mMapMutex};

        if (!ll::service::getLevel())
        {
            throw std::runtime_error("Level is nullptr, cannot registry new dimension " + dimName);
        }

        Impl::DimensionInfo info;

        // ── 1. 先把业务数据（seed / layout 等）准备好 ──────────────────────
        //
        // payload 必须在分配 id 之前拿到：原生注册要从里头读高度范围和生成器
        // 类型，才能构造 DimensionDefinition。

        bool const knownLocally = impl->customDimensionMap.contains(dimName);
        if (knownLocally)
        {
            info = impl->customDimensionMap.at(dimName);
        }
        else if (auto salvaged = impl->salvagedIds.find(dimName); salvaged != impl->salvagedIds.end())
        {
            // 配置里这条的 SNBT 坏了，但 id 还留着。重新生成数据、保住 id，
            // 玩家存档里的 DimensionId 就不会失效。
            info.id  = DimensionType{salvaged->second};
            info.nbt = data();
            impl->salvagedIds.erase(salvaged);
            logger().warn("维度 '{}'：数据已丢失，重新生成，沿用 id {}", dimName, info.id.value());
        }
        else
        {
            info.nbt = data();
        }

        // ── 2. 工厂闭包必须在原生注册**之前**就位 ────────────────────────
        //
        // 这是顺序问题，也是之前"删掉 NativeDimensions 里那个 return 之后
        // 客户端一连就崩"的根因。
        //
        // serverRegisterCustomDimension 内部会走到
        // _registerCustomDimensionWithFactory -> DimensionFactory::create(name)，
        // 而 create() 是拿**名字**去 OwnerPtrFactory::mFactoryMap 里查闭包的。
        // 旧代码在 349 行先注册、401 行才写 map，新进程里那一刻 map 中根本没有
        // 这个名字：引擎在闭包缺席的情况下完成了注册，DimensionRegistry 里于是
        // 多出一条指向空/垃圾的条目，而外面那圈 catch(...) 把异常吞掉了，日志
        // 上什么都看不见。玩家一进来碰到这个维度，服务端喂出去的数据就是坏的。
        //
        // info.id 此刻可能还没定（全新维度），所以闭包捕获一个共享的
        // DimensionInfo，拿到引擎分配的 id 之后再回填；万一引擎在回填之前就
        // 回调了闭包，闭包自己再向引擎要一次 id 兜底。
        auto shared = std::make_shared<Impl::DimensionInfo>(info);

        // insert_or_assign, not emplace: a second registration in the same boot
        // (or after a hot reload) must replace the stale factory, otherwise the
        // engine builds the dimension from the previous run's closure.
        ll::service::getLevel()->getDimensionFactory().mFactoryMap.insert_or_assign(
            dimName,
            [dimName, shared, factory = std::move(factory)](DerivedDimensionArguments&& arguments) -> OwnerPtr<Dimension> {
                DimensionType id = shared->id;
                if (id.value() < 3)
                {
                    // 还没回填 —— 说明我们正处在 serverRegisterCustomDimension
                    // 内部的重入调用里。直接问引擎。
                    if (auto engineId = native::engineDimensionId(dimName))
                    {
                        id = DimensionType{*engineId};
                    }
                    else
                    {
                        logger().error(
                            "维度 '{}' 的工厂被调用时 id 还没确定，且引擎侧也查不到 —— 拒绝建维度，"
                            "不能拿一个默认值（很可能是主世界 0）去建",
                            dimName
                        );
                        return {};
                    }
                }
                return factory(DimensionFactoryInfo{arguments, shared->nbt, id});
            }
        );

        // ── 3. id 从哪来 ────────────────────────────────────────────────
        //
        // 优先让引擎分配。BDS 26.20 起 DimensionManager 自带 NameIdStore，
        // id 存进存档、由引擎持久化，`getOrCreateDimension` 也只认它 ——
        // 这正是之前"注册返回 3、传送却失败"的根因：我们只改了
        // VanillaDimensions::DimensionMap 和工厂 map，NameIdStore 里没有条目，
        // 引擎拿 id 3 反查不到名字，建不出维度。
        //
        // 原生路径失败才退回旧的手抄分配逻辑，行为不会比现在更差。

        auto const nativeId = native::registerCustomDimension(
            dimName,
            kWorldMinY,
            kWorldMaxY,
            readGeneratorType(info.nbt)
        );

        bool const usedNative = nativeId.has_value();

        if (usedNative)
        {
            if (knownLocally && info.id.value() != *nativeId)
            {
                // 引擎给的 id 跟我们配置里记的不一样：以引擎为准，并且把配置
                // 改过来。旧 id 只可能出现在上一版手抄逻辑写下的配置里，那些
                // id 从来就没在引擎侧生效过，所以没有存档兼容性问题。
                logger().warn(
                    "维度 '{}'：配置里记的 id 是 {}，引擎分配的是 {}，以引擎为准",
                    dimName,
                    info.id.value(),
                    *nativeId
                );
            }
            info.id = DimensionType{*nativeId};
            impl->usedIds.insert(*nativeId);
        }
        else if (!knownLocally && info.id.value() < 3)
        {
            int candidate = impl->mNewDimensionId.fetch_add(1);
            while (!impl->usedIds.insert(candidate).second)
            {
                candidate = impl->mNewDimensionId.fetch_add(1);
            }
            info.id = DimensionType{candidate};
            logger().error(
                "维度 '{}' 无法走引擎原生注册，退回旧路径分配 id {} —— "
                "26.20 上这条路径不足以让 getOrCreateDimension 建出维度，"
                "函数末尾的实例校验大概率会直接判定注册失败并抛出",
                dimName,
                candidate
            );
        }
        else
        {
            logger().warn("维度 '{}' 无法走引擎原生注册，沿用已有 id {}", dimName, info.id.value());
        }

        // 回填。闭包里读的是这一份，所以它必须在任何维度被真正创建之前更新。
        shared->id  = info.id;
        shared->nbt = info.nbt;

        impl->customDimensionMap.insert_or_assign(dimName, info);
        rememberDimension(dimName, info.id.value());

        ll::memory::modify(VanillaDimensions::DimensionMap(), [&](auto& dimMap) {
            // insert_or_assign on a BidirectionalUnorderedMap only overwrites
            // the two entries it touches. If this name previously mapped to a
            // different id, the old id->name entry survives in mLeft and keeps
            // resolving to a dimension that no longer exists.
            if (auto it = dimMap.mRight.find(dimName); it != dimMap.mRight.end())
            {
                if (it->second.value() != info.id.value()) dimMap.mLeft.erase(it->second);
            }
            dimMap.insert_or_assign(dimName, info.id);
        });

        // Undefined() 只有在走旧路径时才需要顶上去：那套逻辑靠"Undefined 永远
        // 大于所有已分配 id"来判断一个 id 是不是自定义维度。
        //
        // 走原生路径时**不能**动它。26.20 的引擎自己在用这个哨兵值做判断
        // （DimensionType::isCustom 等），把它改成一个看起来像真实维度的数字，
        // 引擎内部的比较就全乱了。上游 MoreDimensions 那个改写是针对没有原生
        // 支持的旧版本的补偿手段，在这一版属于有害无益。
        int const nextFree = std::max(impl->mNewDimensionId.load(), info.id.value() + 1);
        impl->mNewDimensionId.store(nextFree);
        if (!usedNative)
        {
            ll::memory::modify(VanillaDimensions::Undefined(), [&](DimensionType& uid) {
                uid = DimensionType{nextFree};
            });
        }

        impl->registeredDimension.emplace(dimName);

        // Persist. The old code only wrote when it believed the dimension was
        // new, and used emplace() — which is a no-op on an existing key. A
        // config entry that had drifted from the runtime id could therefore
        // never be corrected. Compare and write whenever anything differs.
        {
            auto& list = CustomDimensionConfig::getConfig().dimensionList;
            auto  snbt = info.nbt.toSnbt(SnbtFormat::Minimize);
            auto  cur  = list.find(dimName);
            if (cur == list.end() || cur->second.dimId != info.id.value() || cur->second.sNbt != snbt)
            {
                list.insert_or_assign(dimName, CustomDimensionConfig::DimensionInfo{info.id.value(), snbt});
                if (!CustomDimensionConfig::saveConfigFile())
                {
                    logger().error("failed to write dimension_config.json; '{}' may get a new id next boot", dimName);
                }
            }
        }

        try
        {
            ll::command::CommandRegistrar::getInstance(false).addEnumValues(
                "Dimension",
                {{dimName, info.id}},
                Bedrock::type_id<CommandRegistry, DimensionType>()
            );
        }
        catch (...)
        {
            logger().warn("维度 '{}' 注册命令枚举失败（不影响维度本身）", dimName);
        }

        try
        {
            // 只用引擎侧 NameIdStore 做判据。VanillaDimensions::fromString 在本
            // 构建有 std::string ABI 问题,对自定义维度回读成垃圾值(如
            // -1870061440),不可信,故不再用它自检。
            if (auto const engineId = native::engineDimensionId(dimName); !engineId || *engineId != info.id.value())
            {
                logger().error(
                    "维度 '{}'（id {}）在引擎的 DimensionManager 里查不到 —— 传送会失败。引擎侧回读结果：{}",
                    dimName, info.id.value(),
                    engineId ? std::to_string(*engineId) : std::string{"(未注册)"}
                );
            }
            else
            {
                logger().info("维度 '{}' 就绪：id {}，引擎侧 active={}",
                              dimName, info.id.value(), native::isActive(info.id.value()));
            }
        }
        catch (...)
        {
            logger().warn("维度 '{}' 自检抛异常（不影响维度本身）", dimName);
        }

        // 唯一的权威判据：引擎真正建出来的那个 Dimension 自报的 id。
        // 名字->id 表、DimensionMap、我们的台账、配置文件都可能各说各话，
        // 只有这个对象是玩家真正会被传送进去的东西。
        auto* probe = more_dimensions::native::getOrCreateByName(dimName);
        if (!probe)
        {
            logger().error("维度 '{}' 注册后建不出实例 —— 判定注册失败", dimName);
            throw std::runtime_error("dimension '" + dimName + "' could not be instantiated");
        }

        int const realId = probe->getDimensionId().value();
        if (realId != info.id.value())
        {
            logger().error(
                "维度 '{}' 台账 id 是 {}，但引擎建出来的实例 id 是 {} —— "
                "把玩家传进 {} 会让引擎在区块线程上抛异常直接 abort。判定注册失败。",
                dimName, info.id.value(), realId, info.id.value()
            );
            // 台账已经写脏了，回滚掉，免得 dimensionSelector 还能查到它
            rememberDimension(dimName, -1);
            throw std::runtime_error("dimension id mismatch for '" + dimName + "'");
        }

        logger().info("维度 '{}' 就绪：id {}", dimName, realId);
        return info.id;
    }
} // namespace more_dimensions