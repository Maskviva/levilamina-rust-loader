# Player — 玩家对象

> 状态：🧩 规划（列表/消息/广播/踢出/生命值/游戏模式/传送等少数能力已在当前桥接支持，见下方各表的"现状"提示）；完整句柄属规划。
>
> **接口来源**：本页方法对应原生 C++ 类 `Player`（`mc/world/actor/player/Player.h`），排除引擎内部虚函数插桩（`$` 前缀）与 Mojang 自己标记为内部的方法（前导下划线，如 `_addLevels`）。命名沿用 LSE 风格（snake_case）。
>
> **继承关系**：原生 `Player : public Mob`，`Mob : public Actor`。也就是说 [Entity](/api/entity) 页列出的一切（位置、生命值、效果、Tag、骑乘、装备只读、环境状态……）**玩家全部具备**，本页只列 `Player` 相对 `Mob`/`Actor` 新增的部分，不重复列出继承来的内容。
>
> 获取：从事件回调，或 `Player::get(info)` / `Player::list()`（按名字 / XUID / UUID 查找单个在线玩家，或枚举全部在线玩家）。

以下针对一个玩家句柄 `player`。每行标注对应的原生方法名。

> `Player` 句柄本身只是一个轻量标识符（名字/xuid/uuid 其一），并不是缓存的原生指针——每次调用 `player.xxx()`，桥接内部都会按这个标识符重新在当前在线玩家里查一次再操作，查不到就安全地返回失败，不会有悬垂指针风险。细节见 [内存安全与生命周期](/advanced/memory-safety)。

## 获取与枚举

| API | 作用 | 原生对应 | 状态 |
| --- | --- | --- | :---: |
| `Player::list()` | 枚举全部在线玩家（名字/xuid/uuid/维度/坐标） | `Level::forEachPlayer` | ✅ |
| `Player::get(info)` | 按名字 / XUID / UUID 查找一个在线玩家 | 桥接内部复用 `Player::list()` 按条件过滤 | 🧩 |

## 身份与网络

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `player.real_name()` | 真实名（不随改名变动） | `Player::getRealName` |
| `player.uuid()` | 玩家 UUID | `Player::getUuid` |
| `player.ip_and_port()` | 客户端 IP:端口 | `Player::getIPAndPort` |
| `player.network_status()` | 网络状态（延迟等） | `Player::getNetworkStatus` |
| `player.locale_code()` | 客户端语言，如 `"zh_CN"` | `Player::getLocaleCode` |
| `player.client_sub_id()` | 分屏/多用户场景下的子客户端 id | `Player::getClientSubId` |
| `player.connection_request()` | 登录时的连接请求信息 | `Player::getConnectionRequest` |
| `player.is_operator()` | 是否为 OP（不考虑自定义权限） | `Player::isOperator` |
| `player.can_use_operator_blocks()` | 能否使用指令方块等 OP 专属方块 | `Player::canUseOperatorBlocks` |

## 消息与连接

| API | 作用 | 原生对应 | 现状 |
| --- | --- | --- | :---: |
| `player.send_message(msg)` | 发送一条消息给该玩家 | `Player::sendMessage` | ✅ |
| `player.disconnect(reason)` | 以给定理由断开连接 | `Player::disconnect` | ✅ |
| `Player::broadcast(msg)` | 向所有在线玩家广播 | （逐个调用 `sendMessage`） | ✅ |

## 能力 Ability

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `player.set_ability(index, value)` | 设置一项能力（飞行/无敌/建造权限等） | `Player::setAbility` |
| `player.can_use_ability(index)` | 是否拥有某项能力 | `Player::canUseAbility` |

## 游戏模式

| API | 作用 | 原生对应 | 现状 |
| --- | --- | --- | :---: |
| `player.game_type()` | 读取当前游戏模式 | `Player::getPlayerGameType` | 🧩（读取未桥接，写入已支持） |
| `player.set_gamemode(mode)` | 设置游戏模式 | 原生的直接设置器是内部方法（`_setPlayerGameType`），桥接改走 `/gamemode` 命令 | ✅ |

## 属性：经验 / 饥饿 / 等级

这几项在原生里不是各自独立的字段，而是统一走**通用 Attribute 系统**：`Player::LEVEL()` / `EXPERIENCE()` / `HUNGER()` / `SATURATION()` / `EXHAUSTION()` 各自返回一个 `Attribute` 键，配合 [Entity](/api/entity) 已有的 `Actor::getAttribute(attribute)` 取到一个带 `getCurrentValue()` / `setCurrentValue(value)` 的句柄来读写。

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `player.level()` / `set_level(n)` | 经验等级 | `getAttribute(Player::LEVEL())` |
| `player.experience()` / `set_experience(n)` | 当前经验值 | `getAttribute(Player::EXPERIENCE())` |
| `player.hunger()` / `set_hunger(n)` | 饥饿值 | `getAttribute(Player::HUNGER())` |
| `player.saturation()` / `set_saturation(n)` | 饱和度 | `getAttribute(Player::SATURATION())` |
| `player.exhaustion()` / `set_exhaustion(n)` | 疲劳值 | `getAttribute(Player::EXHAUSTION())` |
| `player.xp_needed_for_next_level()` | 升到下一级所需经验（专用方法，非 Attribute） | `Player::getXpNeededForNextLevel` |
| `player.cause_food_exhaustion(amount)` | 直接施加一次疲劳消耗 | `Player::causeFoodExhaustion` |
| `player.luck()` | 幸运值 | `Player::getLuck` |

## 物品栏与容器

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `player.inventory()` | 主物品栏，返回 `Container` | `Player::getInventory` |
| `player.ender_chest()` | 末影箱，返回 `Option<Container>` | `Player::getEnderChestContainer` |
| `player.give_item(item)` | 给予物品并刷新客户端显示 | `Player::addAndRefresh` |
| `player.selected_slot()` | 当前选中的快捷栏槽位 | `Player::getSelectedItemSlot` |
| `player.set_selected_slot(slot)` | 切换选中槽位 | `Player::setSelectedSlot` |
| `player.set_selected_item(item)` | 直接设置选中槽位的物品 | `Player::setSelectedItem` |
| `player.current_active_shield()` | 当前生效的盾牌物品 | `Player::getCurrentActiveShield` |

> 详细的容器读写方法见 [Container](/api/container)。

## 物品使用与冷却

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `player.start_using_item(item, duration)` | 开始持续使用一件物品（如拉弓） | `Player::startUsingItem` |
| `player.stop_using_item()` | 中断使用 | `Player::stopUsingItem` |
| `player.release_using_item()` | 松开使用（触发释放效果，如射箭） | `Player::releaseUsingItem` |
| `player.complete_using_item()` | 使用完成（如吃完食物） | `Player::completeUsingItem` |
| `player.use_selected_item(method, consume)` | 直接对选中物品触发一次使用 | `Player::useSelectedItem` |
| `player.eat(item)` | 吃下指定物品 | `Player::eat` |
| `player.item_cooldown_left(category)` | 某冷却类别剩余时间 | `Player::getItemCooldownLeft` |
| `player.is_item_on_cooldown(category)` | 某冷却类别是否仍在冷却 | `Player::isItemOnCooldown` |
| `player.start_item_cooldown(item, update_client)` | 手动开始一次物品冷却 | `Player::startItemCooldown` |

## 战斗与交互

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `player.attack(target, cause)` | 主动攻击目标实体 | `Player::attack` |
| `player.interact(target, location)` | 与目标实体交互（如右键） | `Player::interact` |
| `player.can_be_seen_on_map()` | 是否会在地图物品上显示 | `Player::canBeSeenOnMap` |
| `player.attack_hit_sound()` / `attack_miss_sound()` | 攻击命中 / 落空音效 | `Player::getAttackHitSound` / `getAttackMissSound` |

## 睡眠与重生点

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `player.set_spawn_point(pos, dim, spawn_block)` | 设置出生点 | `Player::setSpawnPoint` |
| `player.can_sleep()` | 当前是否能睡觉 | `Player::canSleep` |
| `player.check_bed(region, pos)` | 校验床铺是否可用 | `Player::checkBed` |
| `player.sleep_rotation()` | 睡觉时的朝向 | `Player::getSleepRotation` |
| `player.has_respawn_position()` | 是否设置了重生点 | `Player::hasRespawnPosition` |
| `player.set_bed_respawn_position(pos)` | 设置床铺重生点 | `Player::setBedRespawnPosition` |
| `player.set_respawn_position(pos, dim)` | 设置重生点 + 维度 | `Player::setRespawnPosition` |
| `player.set_respawn_position_candidate()` | 标记当前位置为候选重生点 | `Player::setRespawnPositionCandidate` |
| `player.set_spawn_block_respawn_position(pos, dim)` | 以出生方块形式设置重生点 | `Player::setSpawnBlockRespawnPosition` |
| `player.expected_spawn_position()` / `expected_spawn_dimension_id()` | 预期出生坐标 / 维度 | `Player::getExpectedSpawnPosition` / `getExpectedSpawnDimensionId` |
| `player.recheck_spawn_position()` | 重新校验出生点有效性 | `Player::recheckSpawnPosition` |
| `player.reset_player_level()` | 重置等级（常用于死亡流程） | `Player::resetPlayerLevel` |

## 移动与姿态

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `player.is_flying()` | 是否正在飞行 | `Player::isFlying` |
| `player.can_jump()` | 当前是否能跳跃 | `Player::canJump` |
| `player.is_emoting()` | 是否正在使用表情动作 | `Player::isEmoting` |
| `player.stop_gliding()` | 停止鞘翅滑翔 | `Player::stopGliding` |
| `player.try_start_gliding()` | 尝试开始鞘翅滑翔 | `Player::tryStartGliding` |

## 其他状态

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `player.is_hidden_from(mob)` | 对指定生物是否隐身/不可见 | `Player::isHiddenFrom` |
| `player.is_equipment_hidden()` | 装备外观是否隐藏 | `Player::isEquipmentHidden` |
| `player.is_in_raid()` | 是否正处于袭击事件中 | `Player::isInRaid` |
| `player.is_forced_respawn()` | 是否处于强制重生流程 | `Player::isForcedRespawn` |
| `player.is_hurt()` | 是否处于受伤状态 | `Player::isHurt` |
| `player.is_scoping()` | 是否正在用望远镜瞄准 | `Player::isScoping` |
| `player.has_resource(item)` | 背包中是否有指定资源 | `Player::hasResource` |

## 附录：其余原生方法

以下是 `Player` 中确实存在、目前简化层还没有对应封装的原生方法，按主题分组，均附一行说明（不含继承自 `Actor`/`Mob` 的部分，那些见 [Entity](/api/entity) 页的附录）。

**出生点、死亡位置与维度切换**

| 原生方法 | 作用 |
| --- | --- |
| `checkAndFixSpawnPosition` | （静态）在候选区域内校正并求出一个可用的出生点坐标 |
| `checkAndFixSpawnPosition_DEPRECATED` | （静态，已弃用）旧版的出生点校正算法 |
| `isDangerousVolumeForSpawn` | （静态）指定包围盒范围内是否存在不适合出生的危险方块 |
| `isDangerousVolumeForSpawnFromSave` | （静态）读档场景下的出生危险区域判定 |
| `isDangerousVolume_DEPRECATED` | （静态，已弃用）旧版危险区域判定，可选是否把岩浆计入 |
| `checkSpawnBlock` | 校验当前出生点方块是否仍然可用 |
| `fireDimensionChangedEvent` | 触发“维度已切换”事件（从哪个维度到哪个维度） |
| `loadLastDeathLocation` | 从存档标签读取上次死亡位置 |
| `saveLastDeathLocation` | 把上次死亡位置写入存档标签 |
| `setLastDeathDimension` | 设置上次死亡所在维度 |
| `setLastDeathPos` | 设置上次死亡坐标 |
| `setHasDied` | 设置“已死亡过”标志位 |
| `updatePlayerGameTypeEntityData` | （静态）按 ECS 上下文同步游戏模式相关的实体数据 |

**移动与网络同步（内部）**

| 原生方法 | 作用 |
| --- | --- |
| `handleMovePlayerPacket` | 处理客户端上行的移动数据包 |
| `is2DPositionRelevant` | 指定维度+坐标的水平位置是否仍然相关（用于移动校验） |
| `checkNeedAutoJump` | 按输入方向判断是否需要自动跳跃 |
| `setChunkRadius` | 设置该玩家的区块加载半径 |

**教育版 Agent**

| 原生方法 | 作用 |
| --- | --- |
| `getAgent` | 关联的教育版 Code Builder Agent |
| `setAgent` | 设置关联的 Agent |

**Boss 血条记账（底层）**

| 原生方法 | 作用 |
| --- | --- |
| `registerTrackedBoss` | 登记一个需要向该玩家显示血条的 Boss |
| `unRegisterTrackedBoss` | 取消登记 |
| `updateTrackedBosses` | 刷新全部已登记 Boss 的血条状态 |

**容器与物品栏内部**

| 原生方法 | 作用 |
| --- | --- |
| `canOpenContainerScreen` | 当前是否允许打开容器界面 |
| `canStackInOffhand` | 指定物品是否可以堆叠进副手槽 |
| `equippedArmorItemCanBeMoved` | 已装备的盔甲物品当前是否允许被移动 |
| `inventoryChanged` | 物品栏内容变化时的回调（含变化前后的物品与槽位） |
| `setContainerManagerModel` | 设置当前使用的容器管理模型 |
| `setPlayerUIItem` | 设置玩家 UI 专用槽位（如工作台结果槽）的物品 |
| `updateInventoryTransactions` | 刷新物品栏事务状态 |
| `tickArmor` | 结算一次盔甲相关效果（如冰霜行者、水肺） |
| `take` | 拾取指定实体（如经验球/箭）到偏好槽位 |

**方块交互与拾取范围**

| 原生方法 | 作用 |
| --- | --- |
| `getDestroyProgress` | 对指定方块的破坏进度增量 |
| `getInteractText` | 当前可交互提示文本 |
| `getItemInteractText` | 对指定物品的交互提示文本 |
| `getPickupArea` | 拾取判定用的包围盒范围 |

**物品对方块的使用**

| 原生方法 | 作用 |
| --- | --- |
| `startItemUseOn` | 开始对指定方块面使用手中物品（如放置/使用铁砧） |
| `stopItemUseOn` | 停止对指定方块的物品使用 |

**网络身份与外观**

| 原生方法 | 作用 |
| --- | --- |
| `getNetworkIdentifier` | 网络层连接标识符 |
| `getUserEntityIdentifier` | 用户实体标识组件（登录相关身份信息） |
| `setPlatformOnlineId` | 设置平台在线 id |
| `updateSkin` | 更新皮肤（含目标子客户端 id） |
| `updateEmoteMessageData` | 更新表情动作的消息数据 |

**音效**

| 原生方法 | 作用 |
| --- | --- |
| `playFallOrLandSound` | 按预期伤害与所处方块播放摔落/落地音效 |
| `playPredictiveSynchronizedSound` | 播放一个客户端预测同步的音效 |

**遥测与事件上报**

| 原生方法 | 作用 |
| --- | --- |
| `sendEventPacket` | 发送一个遥测事件数据包 |
| `broadcastPlayerSpawnedMobEvent` | 广播“玩家生成了一个生物”事件（如召唤类物品） |

**其他**

| 原生方法 | 作用 |
| --- | --- |
| `getNewEnchantmentSeed` | 重新随机一次附魔种子（用于附魔台结果重roll） |
| `setLastHurtBy` | 记录最近一次的伤害来源类型 |
| `setName` | 设置内部标识名（区别于 Entity 页已有的 `Actor::setName`——这里是 `Player` 自身的重写版本） |
| `tryGetFromEntity` | （静态）按 ECS 上下文解析出 `Player*` |

> 其中 `getAgent` / `setAgent` 是教育版 Code Builder 相关；`registerTrackedBoss` 系列是 Boss 血条底层记账（上层封装见 [Gui](/api/gui) 的 `set_boss_bar`）；`loadLastDeathLocation` / `saveLastDeathLocation` 是存档读写，走 NBT 层（见 [Nbt](/api/nbt)）比直接调用更合适。

## 相关类型

| 类型 | 说明 |
| --- | --- |
| `AbilitiesIndex` | 能力索引：飞行、无敌、建造权限等 |
| `GameType` | `Survival` / `Creative` / `Adventure` / `Spectator` |
| `NetworkStatus` | 延迟、丢包等网络状态 |

