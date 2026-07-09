# Entity — 实体对象

> 状态：🧩 规划（读写生命值等少数能力已在 [World](/api/world)/[Player](/api/player) 里以桥接函数形式支持；完整句柄属规划）。
>
> **接口来源**：本页方法对应原生 C++ 类 `Actor`（`mc/world/actor/Actor.h`）与 `Mob`（`mc/world/actor/Mob.h`，继承自 `Actor`）的公开成员函数——即真实存在于 LeviLamina SDK 头文件里、可被调用的方法（不含引擎内部的虚函数插桩，即头文件里以 `$` 开头或前导下划线的条目）。命名沿用 LSE 的简洁风格（snake_case）。
>
> **继承关系**：原生是 `Actor → Mob → Player`。本页覆盖 `Actor`/`Mob` 两层，即除玩家外的一切实体（生物、载具、掉落物等）通用的方法；`Player` 在下一层文档中给出玩家独有的部分，会附带"继承自 Entity"的说明，不重复列出这里的内容。
>
> 获取：从事件回调，或 `Entity::get(id)` / `Entity::all()` / `Entity::in_range(from, to, range)` / `Entity::spawn_mob(name, pos)`。

以下针对一个实体句柄 `entity`。每行标注对应的原生方法名，便于对照原生头文件核实。

> 和 [Player](/api/player) 一样，`Entity` 句柄是一个轻量标识符（`ActorUniqueID`），不是缓存的原生指针；每次调用都按这个 id 重新解析一次，解析不到就安全失败。见[内存安全与生命周期](/advanced/memory-safety)。

## 获取与生成

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `Entity::get(id)` | 按 `ActorUniqueID` / 运行时 id 获取实体 | `Actor::tryGetFromEntity` |
| `Entity::all()` | 获取全部已加载实体 | 遍历 `Level::getRuntimeActorList` |
| `Entity::in_range(from, to, range)` | 获取指定范围内的实体 | `Actor::fetchNearbyActorsSorted` |
| `Entity::spawn_mob(name, pos)` | 在坐标生成生物 | — |
| `Entity::load_mob(nbt, pos)` | 用 NBT 在坐标生成生物 | — |
| `Entity::clone_mob(entity, pos)` | 复制实体到坐标 | `Actor::clone` |

## 身份

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `entity.type_name()` | 标准类型名，如 `"minecraft:zombie"` | `Actor::getTypeName` |
| `entity.is_player()` | 是否为玩家 | `Actor::isPlayer` |
| `entity.is_type(type)` | 是否属于给定 `ActorType` | `Actor::isType` |
| `entity.has_type(types)` | 是否匹配给定类型掩码 | `Actor::hasType` |
| `entity.has_family(family)` | 是否属于给定实体族（如 `"monster"`） | `Actor::hasFamily` |
| `entity.dimension_id()` | 所在维度 | `Actor::getDimensionId` |
| `entity.has_dimension()` | 是否已关联维度 | `Actor::hasDimension` |
| `entity.runtime_id()` | 运行时 id | `Actor::getRuntimeID` |
| `entity.has_runtime_id()` | 是否已分配运行时 id | `Actor::hasRuntimeID` |
| `entity.unique_id()` | 持久化唯一 id（不存在则创建） | `Actor::getOrCreateUniqueID` |
| `entity.is_simulated_player()` | 是否为模拟玩家 | `Actor::isSimulatedPlayer` |

## 位置与移动

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `entity.feet_pos()` | 脚部坐标 | `Actor::getFeetPos` |
| `entity.feet_block_pos()` | 脚部所在方块坐标 | `Actor::getFeetBlockPos` |
| `entity.velocity()` | 当前速度矢量 | `Actor::getVelocity` |
| `entity.apply_impulse(vec)` | 施加一次冲量 | `Actor::applyImpulse` |
| `entity.teleport(pos, dim?, rot?)` | 传送（可跨维度，触发正常传送流程） | `Actor::teleport` |
| `entity.set_pos(pos)` | 直接写入坐标（无副作用的底层设置） | `Actor::setPos` |
| `entity.move_to(pos, rot)` | 直接写入坐标 + 朝向 | `Actor::moveTo` |
| `entity.move(delta)` | 按位移量移动一步 | `Actor::move` |
| `entity.chorus_fruit_teleport(range)` | 紫颂果式随机传送 | `Actor::chorusFruitTeleport` |

## 生命与伤害

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `entity.health()` / `max_health()` | 当前 / 最大生命值 | `Actor::getHealth` / `getMaxHealth` |
| `entity.heal(amount)` | 治疗 | `Actor::heal` |
| `entity.hurt(damage, cause?, attacker?)` | 造成伤害（简化版：伤害值 + 可选原因 + 可选攻击者） | `Actor::hurtByCause` |
| `entity.kill()` | 直接杀死 | `Actor::killed` |

> 需要精确控制击退/点燃的底层版本见附录 `Actor::hurt(source, damage, knock, ignite)`。

## 药水效果

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `entity.add_effect(id, tick, level, show_particles)` | 添加药水效果（桥接内部据参数构造 `MobEffectInstance`） | `Actor::addEffect` |
| `entity.remove_effect(id)` | 移除指定效果 | `Actor::removeEffect` |
| `entity.remove_all_effects()` | 清除全部效果 | `Actor::removeAllEffects` |
| `entity.has_effect(id)` | 是否带有指定效果 | `Actor::hasEffect` |
| `entity.get_effect(id)` | 读取指定效果的实例（不存在则 `None`） | `Actor::getEffect` |

## 火焰

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `entity.set_on_fire(seconds, has_effect=true)` | 点燃 | `Actor::setOnFire` |
| `entity.stop_fire()` | 熄灭 | `Actor::stopFire` |
| `entity.burn(damage, in_fire)` | 直接造成一次火焰伤害判定 | `Actor::burn` |

## 名称与 Tag

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `entity.name_tag()` / `set_name_tag(name)` | 头顶显示名 | `Actor::getNameTag` / `setNameTag` |
| `entity.set_name_tag_visible(visible)` | 是否显示头顶名 | `Actor::setNameTagVisible` |
| `entity.set_name(name)` | 设置内部标识名（区别于显示名） | `Actor::setName` |
| `entity.add_tag(t)` / `remove_tag(t)` / `has_tag(t)` | Tag 增删查 | `Actor::addTag` / `removeTag` / `hasTag` |

## 骑乘

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `entity.vehicle()` | 所骑乘的实体 | `Actor::getVehicle` |
| `entity.is_riding()` | 是否正在骑乘 | `Actor::isRiding` |
| `entity.has_passenger()` | 是否有乘客 | `Actor::hasPassenger` |
| `entity.first_passenger()` | 第一个乘客 | `Actor::getFirstPassenger` |
| `entity.is_passenger(other)` | `other` 是否为本实体的乘客 | `Actor::isPassenger` |
| `entity.exit_vehicle()` | 下载具 | `Actor::exitVehicle` |

## 战斗与仇恨

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `entity.target()` | 当前 AI 目标（只读） | `Actor::getTarget` |
| `entity.last_hurt_by_mob()` / `last_hurt_by_player()` | 最近的伤害来源 | `Actor::getLastHurtByMob` / `getLastHurtByPlayer` |
| `entity.can_attack(other)` | 能否攻击目标 | `Actor::canAttack` |
| `entity.can_see(other)` | 是否有目标的视线 | `Actor::canSee` |
| `entity.closer_than(other, xz, y)` | 是否在给定水平/垂直距离内 | `Actor::closerThan` |

## 装备（只读）

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `entity.armor(slot)` | 指定盔甲槽的物品（`slot`: Head/Torso/Legs/Feet/Body） | `Actor::getArmor` |
| `entity.equipped(slot)` | 指定装备槽的物品 | `Actor::getEquippedSlot` |
| `entity.offhand()` | 副手物品 | `Actor::getOffhandSlot` |
| `entity.equip_slot_count()` | 装备槽位数 | `Actor::getEquipSlots` |

> 装备的写入走容器接口，见 [Container](/api/container)；`Mob::getAllArmorID()`（全部盔甲 id）在附录中。

## 环境与状态

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `entity.is_on_ground()` | 是否在地面 | `Actor::isOnGround` |
| `entity.is_in_water()` / `is_in_lava()` | 是否处于水 / 岩浆中 | `Actor::isInWater` / `isInLava` |
| `entity.is_in_rain()` / `is_in_snow()` | 是否处于雨 / 雪中 | `Actor::isInRain` / `isInSnow` |
| `entity.is_in_world()` | 是否仍在世界中（未被移除） | `Actor::isInWorld` |
| `entity.is_sneaking()` / `is_swimming()` | 潜行 / 游泳状态 | `Actor::isSneaking` / `isSwimming` |
| `entity.is_spectator()` / `is_creative()` / `is_adventure()` / `is_survival()` | 游戏模式判定（对生物通常恒为 false） | `Actor::isSpectator` 等 |
| `entity.is_baby()` | 是否为幼年个体 | `Actor::isBaby` |
| `entity.is_silent()` | 是否禁用其自身音效 | `Actor::isSilent` |
| `entity.is_gliding()` | 是否鞘翅滑翔中（`Mob` 扩展） | `Mob::isGliding` |
| `entity.is_able_to_move()` | 是否能够移动（`Mob` 扩展） | `Mob::isAbleToMove` |

## 其他

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `entity.play_sound(sound, pos, data=0)` | 播放音效 | `Actor::playSound` |
| `entity.eval_molang(expr)` | 求值一段 Molang 表达式 | `Actor::evalMolang` |
| `entity.trace_ray(max_dist, include_actor=true, include_block=true)` | 沿视线做一次射线检测 | `Actor::traceRay`（省略自定义过滤回调；需要过滤器时用附录中的底层版本） |
| `entity.nearby_actors(range, type_filter?)` | 按距离排序取附近实体 | `Actor::fetchNearbyActorsSorted` |
| `entity.look_at(target, y_max, x_max)` | 转向朝向目标（`Mob` 扩展） | `Mob::lookAt` |
| `entity.knockback(source, damage, xd, zd, h_power, v_power)` | 施加击退（`Mob` 扩展） | `Mob::knockback` |

## 附录：其余原生方法

以下是 `Actor` / `Mob` 中确实存在、目前简化层还没有对应封装的原生方法，按主题分组，均附一行说明；命名与原生一致，供检索与提需求参照。

### Actor（168 个）

**位置、朝向与移动细节**

| 原生方法 | 作用 |
| --- | --- |
| `getHeadPos` | 头部坐标 |
| `getInterpolatedPosition` | 渲染插值用的坐标（两个 tick 之间的过渡位置） |
| `getInterpolatedRidingPosition` | 渲染插值用的骑乘位置 |
| `getInterpolatedRotation` | 渲染插值用的朝向 |
| `getPosDeltaPerSecLength` | 每秒位移量的长度（速度标量） |
| `getViewVector` | （静态）按俯仰/偏航角计算视线方向单位向量 |
| `getViewVector2` | 按当前朝向计算视线方向（2D 形式） |
| `lerpTo` | 按给定步数向目标坐标/朝向做插值过渡 |
| `pushOutOfBlocks` | 把实体从重叠的方块中推出去 |
| `synchronousSetSize` | 同步设置碰撞体宽高 |
| `updateInsideBlock` | 刷新“当前处于哪个方块内部”的状态 |
| `getBlockPosCurrentlyStandingOn` | 当前站立所在的方块坐标 |
| `getBlocksCurrentlyStandingOn` | （静态）按碰撞箱计算站立所在的全部方块坐标 |
| `getAABB` | 碰撞用的轴对齐包围盒 |
| `getActorToWorldTransform` | 实体本地坐标到世界坐标的变换矩阵 |
| `getAttachPos` | 身上某个挂载点（如骑乘点）的世界坐标 |

**环境与液体判断**

| 原生方法 | 作用 |
| --- | --- |
| `canBeginOrContinueClimbingLadder` | 能否开始/继续爬梯子 |
| `canCurrentlySwim` | 当前是否能游泳 |
| `getCurrentSwimAmount` | 当前游泳姿态的插值程度 |
| `getSwimAmount` | 指定插值系数下的游泳姿态程度 |
| `inDownwardFlowingLiquid` | 是否处于向下流动的液体中 |
| `isActorLocationInMaterial` | 身体的指定部位是否处于某种材质中 |
| `isUnderLiquid` | 是否完全没入指定材质的液体 |
| `onClimbableBlock` | 当前是否站在可攀爬方块上 |
| `canSeeDaylight` | 头顶是否能见到天光 |
| `isInClouds` | 是否处于云层中 |
| `isInPrecipitation` | 是否处于降水（雨/雪）中 |
| `isInThunderstorm` | 是否处于雷暴中 |
| `isInWaterOrRain` | 是否处于水中或雨中 |
| `isOverWater` | 正下方是否为水面 |
| `isTouchingDamageBlock` | 是否接触着会造成伤害的方块（如岩浆、仙人掌） |
| `getLiquidAABB` | （静态）按液体类型计算包围盒的液体接触范围 |

**战斗、伤害与仇恨记录**

| 原生方法 | 作用 |
| --- | --- |
| `celebrateHunt` | 播放“捕猎成功”的庆祝动作 |
| `checkFallDamage` | 结算一次坠落伤害判定 |
| `getFallDistance` | 当前累计的坠落距离 |
| `getDamageNearbyMobs` | 是否会对附近生物造成范围伤害（如爆炸生物） |
| `handleFallDamage` | 处理一次坠落伤害（含倍率与伤害来源） |
| `handleLeftoverFallDamage` | 处理坠落伤害中未被吸收的剩余部分 |
| `isAttackableGamemode` | 当前游戏模式下是否可被攻击 |
| `setLastHurtByMob` | 记录最近一次伤害来源的生物 |
| `setLastHurtByPlayer` | 记录最近一次伤害来源的玩家 |
| `setLastHurtMob` | 记录最近一次攻击的目标生物 |
| `getStrength` | 当前力量值（如骆驼/骡子的负重相关属性） |
| `setStrength` | 设置当前力量值 |
| `setStrengthMax` | 设置力量最大值 |
| `getStructuralIntegrity` | 当前结构完整度（如脚手架类结构生物） |
| `setStructuralIntegrity` | 设置结构完整度 |

**骑乘、乘客与拴绳**

| 原生方法 | 作用 |
| --- | --- |
| `evaluateSeatRotation` | 计算乘坐位置应有的朝向 |
| `getPassengerIndex` | 指定乘客在座位列表中的序号 |
| `getRidingHeight` | 骑乘时的垂直偏移高度 |
| `getVehicleRoot` | 所在载具链的根节点信息 |
| `hasPlayerPassenger` | 是否有玩家作为乘客 |
| `positionAllPassengers` | 重新摆放全部乘客的位置 |
| `positionPassenger` | 摆放指定乘客的位置 |
| `removeAllPassengers` | 移除全部乘客 |
| `stopRiding` | 下载具（可指定是否正在被摧毁/切换载具/被传送等情形） |
| `teleportPassengersTo` | 把全部乘客一并传送到目标坐标 |
| `getLeashHolder` | 拴绳另一端持有者的 id |
| `setLeashHolder` | 设置拴绳持有者 |
| `isLeashed` | 是否被拴绳牵引 |
| `hasSaddle` | 是否已装鞍 |
| `setSaddle` | 设置是否装鞍 |
| `tickLeash` | 结算一次拴绳的物理/约束 |
| `getOwner` | 所属主人（驯服类生物） |
| `getPlayerOwner` | 所属玩家主人 |
| `isChested` | 是否装有箱子（如驴/骆驼的驮箱） |
| `getChestSlots` | 驮箱的槽位数 |

**装备与物品交互**

| 原生方法 | 作用 |
| --- | --- |
| `consumeItem` | 消耗一个掉落物实体（拾取吞并效果） |
| `createUpdateEquipPacket` | 构造一个装备更新数据包 |
| `dropTowards` | 朝指定方向丢出一件物品 |
| `equip` | 按装备表批量装备 |
| `equipFromEquipmentDefinition` | 按预设的装备定义自动装备 |
| `getCarriedItemInSlotPreferredBy` | 找出偏好携带指定物品的槽位当前物品 |
| `getEquipmentSlotForItem` | 指定物品应归属的装备槽位 |
| `pickUpItem` | 拾取一个掉落物实体 |
| `spawnAtLocation` | 在实体所在位置生成一个掉落物 |
| `spawnEatParticles` | 播放进食粒子效果 |
| `tryGetEquippableSlotForItem` | 尝试解析出物品可装备的槽位（找不到则为空） |
| `isWearingLeatherArmor` | 是否穿着皮革盔甲 |

**交易**

| 原生方法 | 作用 |
| --- | --- |
| `getTradeOffers` | 商人的交易选项列表 |
| `getTradingPlayer` | 当前正在与其交易的玩家 |
| `isTrading` | 是否正处于交易中 |
| `savePersistingTrade` | 把交易选项持久化保存 |
| `setTradingPlayer` | 设置当前交易对象玩家 |

**属性与动态数据**

| 原生方法 | 作用 |
| --- | --- |
| `getAttribute` | 读取指定 Attribute 的实例句柄（生命值/速度等都是走这套系统，见 Player 页说明） |
| `getAttributes` | 全部 Attribute 的集合 |
| `getOrAddDynamicProperties` | 读取（不存在则创建）自定义动态属性容器 |

**存档与序列化**

| 原生方法 | 作用 |
| --- | --- |
| `loadEntityFlags` | 从存档标签读取实体状态标志位 |
| `loadLinks` | 从存档标签读取实体间的连接关系（如船挂车） |
| `saveEntityFlags` | 把实体状态标志位写入存档标签 |
| `saveLinks` | 把实体间连接关系序列化 |
| `saveWithoutId` | 序列化实体但不含 id 字段 |
| `save` | 完整序列化实体到 NBT |
| `getLinks` | 读取当前实体间连接关系 |
| `serializationSetHealth` | 反序列化时直接写入生命值（跳过一般的伤害/治疗逻辑） |

**身份、标识与端侧判断**

| 原生方法 | 作用 |
| --- | --- |
| `getActorIdentifier` | 完整的实体定义标识符 |
| `getEntityContext` | 底层 ECS 实体上下文（引擎内部数据句柄） |
| `getEntityTypeId` | 实体类型 id（数值形式） |
| `getWeakEntity` | 指向 ECS 实体上下文的弱引用 |
| `getMetadataId` | 网络同步用的元数据 id |
| `getRedactableNameTag` | 隐私脱敏版本的名称标签（用于日志等场景） |
| `setRedactableNameTag` | 设置隐私脱敏版本的名称标签 |
| `setRuntimeID` | 设置运行时 id |
| `setUniqueID` | 设置持久化唯一 id |
| `setActorRendererId` | 设置使用的渲染器 id |
| `isClientSide` | 是否为客户端侧实例 |
| `isLocalPlayer` | 是否为本机玩家（客户端概念） |
| `isRemotePlayer` | 是否为远程玩家（客户端概念） |
| `isGlobal` | 是否为跨区块常驻的全局实体 |
| `isWorldBuilder` | 是否具有世界构建者权限标志 |
| `isDoorOpener` | 是否具备自动开门行为 |
| `isSitting` | 是否处于坐下状态 |
| `isTame` | 是否已被驯服 |
| `tryGetFromEntity` | （静态重载）按 ECS 上下文 + 注册表解析出 `Actor*` |
| `getLootTable` | 关联的战利品表 |

**效果与同步状态回调**

| 原生方法 | 作用 |
| --- | --- |
| `canReceiveMobEffectsFromGameplay` | 玩法层面是否允许获得药水效果（不同于原始免疫判定） |
| `onEffectAdded` | 药水效果被添加时的回调 |
| `onEffectUpdated` | 药水效果被刷新/叠加时的回调 |
| `onSynchedFlagUpdate` | 同步状态位发生变化时的回调 |
| `getStatusFlag` | 读取指定状态标志位 |
| `setStatusFlag` | 设置指定状态标志位 |
| `getMarkVariant` | 外观变体编号（如羊驼花纹） |
| `setMarkVariant` | 设置外观变体编号 |
| `setSkinID` | 设置皮肤编号 |

**生命周期与内部管理**

| 原生方法 | 作用 |
| --- | --- |
| `initParams` | 初始化渲染参数 |
| `refresh` | 刷新实体（重新拉取底层状态） |
| `refreshComponents` | 刷新组件集合 |
| `reload` | 重新加载 |
| `setAutonomous` | 设置是否自主行动（AI 开关） |
| `setBaseDefinition` | 切换实体的基础定义（如变形） |
| `setBlockTarget` | 设置目标方块坐标（如末影人搬运方块） |
| `setBreakingObstruction` | 设置是否正在破坏阻挡物 |
| `setDead` | 设置死亡标志位 |
| `isDead` | 是否已死亡 |
| `setDimension` | 设置所属维度的弱引用 |
| `setInLove` | 设置求爱对象（用于繁殖） |
| `isInLove` | 是否处于求爱状态 |
| `setInvisible` | 设置是否隐身 |
| `setLimitedLifetimeTicks` | 设置有限存活时间（tick 数） |
| `setPersistent` | 标记为不会自然消失 |
| `setPrevPosRotSetThisTick` | 标记本 tick 是否已设置过上一帧坐标/朝向 |
| `setYHeadRotations` | 设置头部左右朝向（新值与旧值，用于插值） |
| `shouldOrphan` | 在给定区块源下是否应被判定为孤立（无所属区块） |
| `shouldRender` | 是否应当被渲染 |
| `shouldTick` | 是否应当参与 tick 更新 |
| `tick` | 在指定区块源上执行一次 tick |
| `tickBlockDamage` | 结算一次方块破坏进度的 tick |
| `triggerActorRemovedEvent` | 触发“实体已移除”事件 |
| `updateDescription` | 刷新实体描述信息 |
| `updateInvisibilityStatus` | 刷新隐身状态 |
| `updateTickingData` | 刷新 tick 相关的内部数据 |
| `wobble` | 触发一次摆动效果（如史莱姆跳跃形变） |
| `isImmobile` | （静态）按 ECS 上下文判断实体是否被设为不可移动 |
| `tryTeleportTo` | 尝试传送到目标坐标（可指定是否需落地、避开液体等） |
| `isTickingEntity` | 是否为参与 tick 的实体 |

**音效、粒子与游戏事件**

| 原生方法 | 作用 |
| --- | --- |
| `playSynchronizedSound` | 播放一个跨客户端同步的音效 |
| `spawnTrailBubbles` | 生成拖尾气泡效果（如水中移动） |
| `postGameEvent` | 上报一个游戏事件（用于监听器/振动感知等系统） |
| `postSplashGameEvent` | 上报一次落水/溅落事件 |

**杂项**

| 原生方法 | 作用 |
| --- | --- |
| `buildDebugGroupInfo` | 构建调试信息文本 |
| `canFly` | 是否具备飞行能力 |
| `pushBackActionEventToActionQueue` | 把一个动作事件压回动作队列 |
| `sendActorDefinitionEventTriggered` | 上报“实体定义事件已触发” |
| `getBrightness` | 当前所处位置的亮度值 |
| `getSpeedInMetersPerSecond` | 换算为“米/秒”的移动速度 |
| `getIsExperienceDropEnabled` | 死亡时是否会掉落经验 |
| `getOnDeathExperience` | 死亡时应掉落的经验值 |
| `getLevelTimeStamp` | 记录的存档时间戳 |

### Mob（45 个，在 Actor 基础上新增）

**移动与姿态**

| 原生方法 | 作用 |
| --- | --- |
| `calcMoveRelativeSpeed` | 按移动方式（行走/游泳/飞行等）计算相对速度 |
| `getYBodyRotation` | 身体的左右朝向 |
| `getYBodyRotationsNewOld` | 身体朝向的新旧插值对 |
| `setYBodyRotations` | 设置身体朝向（新值与旧值） |
| `snapToYBodyRot` | 瞬间对齐身体朝向（跳过插值） |
| `snapToYHeadRot` | 瞬间对齐头部朝向（跳过插值） |
| `shouldApplyWaterGravity` | 是否应套用水中重力规则 |
| `frostWalk` | 结算冰霜行者附魔的踏冰效果 |
| `getTravelType` | 当前移动方式类型（行走/游泳/飞行） |
| `getCaravanSize` | 所在队列（如驴队）的长度 |
| `getFirstCaravanHead` | 队列最前端的领队生物 |
| `emitJumpPreventedEvent` | 上报“跳跃被阻止”事件 |
| `getJumpEffectAmplifierValue` | 跳跃提升效果的加成数值 |
| `getJumpPrevention` | 当前是否处于禁止跳跃状态及原因 |
| `setJumpTicks` | 设置跳跃相关的计时器 |

**战斗与伤害计算**

| 原生方法 | 作用 |
| --- | --- |
| `calculateAttackDamage` | 按目标与设置计算最终攻击伤害 |
| `checkForPostHitDamageImmunity` | 命中后是否进入短暂伤害免疫窗口 |
| `checkTotemDeathProtection` | 结算不死图腾是否触发死亡保护 |
| `getDamageAfterDamageSensorComponentAdjustments` | 经“伤害感应”组件调整后的伤害值 |
| `getDamageAfterResistanceEffect` | 扣除抗性效果后的伤害值 |
| `getDamageCause` | 本次伤害的原因类型 |
| `getModifiedSwingDuration` | 经修正后的挥击动作时长 |
| `getAttackAnim` | 攻击动画的插值进度 |
| `getToughnessValue` | 护甲韧性数值 |
| `hurtArmor` | 对身上盔甲施加一次损耗 |
| `getExpectedFallDamage` | 预计的坠落伤害（未实际结算） |

**装备与容器**

| 原生方法 | 作用 |
| --- | --- |
| `clearMainHandSlot` | 清空主手槽位 |
| `containerChanged` | 所属容器发生变化时的回调 |
| `getArmorCoverPercentage` | 盔甲覆盖率百分比 |
| `getArmorTypeHash` | 当前盔甲组合的类型哈希 |
| `getCarriedItemKnockbackBonus` | 当前手持物品带来的击退加成 |
| `getItemSlot` | 读取指定装备槽位的物品 |
| `saveOffhand` | 序列化副手物品 |
| `sendArmorSlot` | 同步指定盔甲槽位到客户端 |
| `updateEquipment` | 刷新全部装备状态 |

**AI 与属性**

| 原生方法 | 作用 |
| --- | --- |
| `createAI` | 按目标定义列表创建 AI 行为 |
| `removeSpeedModifier` | 移除一个速度属性修饰符 |
| `resetAttributes` | 重置全部属性为默认值 |
| `setSprinting` | （静态）按属性表与同步写入器设置疾跑状态 |

**进食与其他状态**

| 原生方法 | 作用 |
| --- | --- |
| `getEatCounter` | 进食动作的计数/进度 |
| `setEatCounter` | 设置进食动作的计数/进度 |
| `setEating` | 设置是否正在进食 |
| `tickMobEffectsVisuals` | 结算药水效果的视觉表现（粒子等） |
| `updateGlidingDurability` | 结算鞘翅滑翔对耐久的消耗 |
| `tryGetFromEntity` | （静态重载）按 ECS 上下文解析出 `Mob*` |

## 相关类型

| 类型 | 说明 |
| --- | --- |
| `ArmorSlot` | `Head` / `Torso` / `Legs` / `Feet` / `Body` |
| `EffectInstance` | 药水效果实例：效果 id、剩余时长、等级 |
| `HitResult` | `trace_ray` 的返回：命中类型（实体/方块/未命中）与命中点 |

