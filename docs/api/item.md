# Item — 物品对象

> 状态：✅ 已支持。
>
> **接口来源**：本页方法对应原生 C++ 类 `ItemStackBase`（`mc/world/item/ItemStackBase.h`，绝大部分方法在这里）与其叶子类 `ItemStack`（`mc/world/item/ItemStack.h`，新增网络/耐久相关的少量方法）。排除引擎内部虚函数插桩（`$` 前缀）与 Mojang 自行标记为内部的方法（前导下划线）。命名沿用 LSE 风格（snake_case）。
>
> **两个容易搞混的地方**（都已按真实签名核实过）：
> 1. **堆叠数量没有 getter**——`mCount` 是公开成员字段（`uchar`），原生没有 `getCount()` 方法，只有 `add`/`remove`/`set`/`setStackSize` 这些修改它的方法；桥接会把字段读出来做成 `count()`。
> 2. **完整 NBT 的读写不对称**：读出整个物品用 `ItemStackBase::save(ctx)`；反过来"用一整份 NBT 构造/替换一个物品"用的是 `ItemStack::fromTag(tag)`（静态工厂），而 `setUserData(tag)` 只覆盖物品的"用户数据"子标签（lore/附魔/展示名等），并不是整个物品的通用 NBT 写入口。
>
> 获取：从事件回调 / 玩家物品栏，或 `ItemStack::create(name, count)` / `ItemStack::from_snbt(snbt)` / `ItemStack::empty()`。

以下针对一个物品句柄 `item`。

## 构造

| API | 作用 | 原生对应 | 状态 |
| --- | --- | --- | :---: |
| `ItemStack::create(name, count)` | 按类型名创建一个新物品堆 | 桥接内部按名查类型并构造 `ItemStack` | ✅ |
| `ItemStack::from_snbt(snbt)` | 从一份 SNBT 文本构造物品 | 解析 SNBT 后走 `ItemStack::fromTag` | ✅ |
| `ItemStack::empty()` | 空物品堆 | `ItemStack::EMPTY_ITEM` | ✅ |

## 名称

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `item.type_name()` | 标准类型名，如 `"minecraft:diamond_sword"` | `ItemStackBase::getTypeName` |
| `item.custom_name()` / `set_custom_name(name)` | 玩家用铁砧改的自定义名 | `ItemStackBase::getCustomName` / `setCustomName` |
| `item.hover_name()` | 悬浮提示里实际显示的名字（含格式） | `ItemStackBase::getHoverName` |
| `item.has_custom_hover_name()` | 是否带有自定义悬浮名 | `ItemStackBase::hasCustomHoverName` |
| `item.reset_hover_name()` | 清除自定义悬浮名 | `ItemStackBase::resetHoverName` |

## 数量与堆叠

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `item.count()` | 当前堆叠数量 | `ItemStackBase::mCount`（字段） |
| `item.add(n)` / `remove(n)` / `set(n)` | 增加 / 减少 / 直接设置数量 | `ItemStackBase::add` / `remove` / `set` |
| `item.set_stack_size(n)` | 直接设置堆叠数量（`uchar`） | `ItemStackBase::setStackSize` |
| `item.max_stack_size()` | 该物品的最大堆叠数 | `ItemStackBase::getMaxStackSize` |
| `item.is_stackable()` | 是否可堆叠 | `ItemStackBase::isStackable` |
| `item.is_stacked_by_data()` | 是否按数据值区分堆叠（如药水） | `ItemStackBase::isStackedByData` |

## 耐久

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `item.damage_value()` / `set_damage_value(n)` | 已损耗的耐久 | `ItemStackBase::getDamageValue` / `setDamageValue` |
| `item.max_damage()` | 最大耐久 | `ItemStackBase::getMaxDamage` |
| `item.is_damageable()` | 是否为可损耗耐久的物品 | `ItemStackBase::isDamageableItem` |
| `item.is_damaged()` | 当前是否已产生损耗 | `ItemStackBase::isDamaged` |
| `item.hurt_and_break(delta, owner?)` | 施加一次耐久损耗，归零则触发损坏 | `ItemStackBase::hurtAndBreak` |
| `item.remove_damage_value()` | 清除损耗（完全修复） | `ItemStackBase::removeDamageValue` |

## 身份比较与查询

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `item.is_null()` | 是否为空槽位 | `ItemStackBase::isNull` |
| `item.matches(other)` | 是否为同种物品（忽略数量） | `ItemStackBase::matchesItem` |
| `item.same_item(id, aux)` | 是否匹配给定 id + 数据值 | `ItemStackBase::sameItem` |
| `item.has_same_aux_value(other)` | 数据值（aux）是否相同 | `ItemStackBase::hasSameAuxValue` |
| `item.aux_value()` | 数据值 | `ItemStackBase::getAuxValue` |
| `item.id()` / `id_aux()` | 数字 id / id+aux 组合 | `ItemStackBase::getId` / `getIdAux` |

## 方块物品

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `item.is_block()` | 是否为方块类物品 | `ItemStackBase::isBlock` |
| `item.is_block_instance(block_name)` | 是否为指定方块的物品形式 | `ItemStackBase::isBlockInstance` |
| `item.block_for_rendering()` | 用于渲染的方块引用 | `ItemStackBase::getBlockForRendering` |
| `item.block_type()` | 关联的方块类型 | `ItemStackBase::getBlockType` |

## 盔甲与装备

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `item.is_armor()` | 是否为盔甲 | `ItemStackBase::isArmorItem` |
| `item.armor_slot()` | 对应的盔甲槽位 | `ItemStackBase::getArmorSlot` |
| `item.is_humanoid_armor()` | 是否为人形盔甲 | `ItemStackBase::isHumanoidArmorItem` |
| `item.is_horse_armor()` | 是否为马铠 | `ItemStackBase::isHorseArmorItem` |
| `item.is_attachable_equipment()` | 是否为可穿戴装备（非传统盔甲槽） | `ItemStackBase::isAttachableEquipment` |

## 附魔

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `item.is_enchanted()` | 是否带有附魔 | `ItemStackBase::isEnchanted` |
| `item.is_enchanting_book()` | 是否为附魔书 | `ItemStackBase::isEnchantingBook` |
| `item.remove_enchants()` | 移除全部附魔 | `ItemStackBase::removeEnchants` |
| `item.is_glint()` | 是否带附魔光效（不一定真的有附魔） | `ItemStackBase::isGlint` |
| `item.enchants()` | 读取附魔列表 | `ItemStackBase::constructItemEnchantsFromUserData` |
| `item.save_enchants(enchants)` | 写入附魔列表 | `ItemStackBase::saveEnchantsToUserData` |

## Lore（描述文字）

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `item.lore()` | 读取 Lore 各行 | `ItemStackBase::getCustomLore` |
| `item.set_lore(lines)` | 设置 Lore | `ItemStackBase::setCustomLore` |
| `item.clear_lore()` | 清除 Lore | `ItemStackBase::clearCustomLore` |

## NBT / 序列化

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `item.to_nbt()` | 序列化整个物品为 NBT | `ItemStackBase::save` |
| `item.descriptor()` | 转为网络层的物品描述符 | `ItemStackBase::getDescriptor` |
| `item.icon_info(frame, in_inventory)` | 图标渲染信息 | `ItemStackBase::getIconInfo` |

> 反向"从 NBT 构造"见上方"构造"一节的 `ItemStack::from_nbt`。

## 冷却

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `item.is_on_cooldown(player, kind)` | 对某玩家是否在冷却中 | `ItemStackBase::isOnCooldown` |
| `item.start_cooldown(player, kind)` | 对某玩家开始冷却 | `ItemStackBase::startCooldown` |

## 组件

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `item.has_component(name)` | 是否带有指定组件 | `ItemStackBase::hasComponent` |
| `item.update_component(name, data)` | 更新组件数据（JSON） | `ItemStackBase::updateComponent` |
| `item.component_item()` | 底层组件化物品定义 | `ItemStackBase::getComponentItem` |

## 特定类型判断

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `item.is_potion()` | 是否为药水类物品 | `ItemStackBase::isPotionItem` |
| `item.is_liquid_clip_item()` | 是否为可舀取液体的物品（桶等） | `ItemStackBase::isLiquidClipItem` |

## 其他

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `item.color()` | 物品颜色（如皮革护甲染色） | `ItemStackBase::getColor` |
| `item.set_repair_cost(cost)` / `base_repair_cost()` | 铁砧修复所需经验成本 | `ItemStackBase::setRepairCost` / `getBaseRepairCost` |
| `ItemStack::EMPTY` | 空物品单例 | `ItemStack::EMPTY_ITEM` |
| `item.use_on(entity, pos, face, click_pos)` | 对某实体/坐标使用该物品 | `ItemStack::useOn` |
| `item.use_as_fuel()` | 作为燃料消耗一次 | `ItemStack::useAsFuel` |
| `item.max_use_duration()` | 持续使用（如拉弓/吃食物）的最长时间 | `ItemStack::getMaxUseDuration` |

## 附录：其余原生方法

以下合并了 `ItemStackBase` 与 `ItemStack` 中确实存在、目前简化层还没有对应封装的原生方法，按主题分组，均附一行说明。

**用户数据标签常量**

| 原生方法 | 作用 |
| --- | --- |
| `TAG_CAN_DESTROY` | （静态）“可破坏方块列表”子标签的键名 |
| `TAG_CAN_PLACE_ON` | （静态）“可放置于方块列表”子标签的键名 |
| `TAG_CHARGED_ITEM` | （静态）“已装填物品”子标签的键名 |
| `TAG_DISPLAY` | （静态）“展示信息”子标签的键名（含展示名、Lore 等） |
| `TAG_DISPLAY_FILTERED_NAME` | （静态）“过滤后展示名”子标签的键名（用于聊天过滤策略） |
| `TAG_DISPLAY_NAME` | （静态）“展示名”子标签的键名 |
| `TAG_ENCHANTS` | （静态）“附魔列表”子标签的键名 |
| `TAG_LORE` | （静态）“Lore”子标签的键名 |
| `TAG_REPAIR_COST` | （静态）“修复成本”子标签的键名 |
| `TAG_STORE_CAN_DESTROY` | （静态）“可破坏方块列表”的存档序列化键名 |
| `TAG_STORE_CAN_PLACE_ON` | （静态）“可放置于方块列表”的存档序列化键名 |
| `TAG_UNBREAKABLE` | （静态）“无法破坏”子标签的键名 |

**命名与哈希（含隐私脱敏）**

| 原生方法 | 作用 |
| --- | --- |
| `getDescriptionId` | 翻译用的描述 id |
| `getDescriptionName` | 描述性名称 |
| `getFullNameHash` | 完整名称的哈希 |
| `getRawNameHash` | 原始名称的哈希 |
| `getRawNameId` | 原始名称 id 字符串 |
| `getRedactedCustomName` | 隐私脱敏版本的自定义名（用于日志等场景） |
| `getRedactedHoverName` | 隐私脱敏版本的悬浮提示名 |
| `getRedactedName` | 隐私脱敏版本的名称 |
| `getRendererId` | 使用的渲染器 id |

**充能物品（如已上弦的弩）**

| 原生方法 | 作用 |
| --- | --- |
| `clearChargedItem` | 清除已装填的物品 |
| `hasChargedItem` | 是否已装填物品 |
| `setChargedItem` | 设置已装填的物品（可指定是否为替换装填） |

**动态属性**

| 原生方法 | 作用 |
| --- | --- |
| `getDynamicProperties` | 读取整个自定义动态属性容器 |
| `getDynamicProperty` | 按键（及所属集合名）读取单个动态属性值 |
| `setDynamicProperty` | 按键（及所属集合名）设置单个动态属性值 |

**网络同步（进阶）**

| 原生方法 | 作用 |
| --- | --- |
| `clientInitNetId` | 客户端侧按服务端网络 id 初始化本地网络 id |
| `serverInitNetId` | 服务端侧初始化网络 id |
| `matchesNetIdVariant` | 网络 id 变体是否匹配（用于增量同步判重） |
| `getStrippedNetworkItem` | 剥离掉部分数据后、仅用于网络传输的物品副本 |
| `getNetworkUserData` | 面向网络传输的用户数据快照 |
| `loadItemStacksFromDescriptor` | （静态）按网络描述符批量还原出物品堆列表 |

**组件系统（进阶）**

| 原生方法 | 作用 |
| --- | --- |
| `addComponents` | 按一段 JSON 追加/覆盖物品组件定义 |
| `deserializeComponents` | 从二进制输入流反序列化组件数据 |

**类型与实例判断（进阶）**

| 原生方法 | 作用 |
| --- | --- |
| `isInstance` | 是否为指定名字物品的实例（可选走别名查找） |
| `isOneOfBlockInstances` | 是否为给定方块类型列表中任意一种的物品形式 |
| `isOneOfInstances` | 是否为给定物品名列表中的任意一种实例 |
| `isValidAuxValue` | 给定的数据值对该物品是否合法 |
| `isHumanoidWearableBlockItem` | 是否为人形生物可穿戴的方块类物品（如南瓜头） |

**用户数据与容器数据**

| 原生方法 | 作用 |
| --- | --- |
| `addCustomUserData` | 追加一段容器相关的自定义用户数据（如潜影盒内容） |
| `hasContainerData` | 是否携带容器数据（如已装物品的潜影盒） |
| `hasSameUserData` | 与另一个物品的用户数据是否完全一致 |
| `matchesEitherWearableCase` | 按给定用户数据判断是否匹配两种可穿戴判定情形之一 |
| `setUserData` | 整体设置用户数据子标签（覆盖 lore/附魔/展示名等，注意不是整个物品的 NBT，见正文说明） |

**其他**

| 原生方法 | 作用 |
| --- | --- |
| `getBlockTypeForRendering` | 用于渲染的方块类型（弱引用） |
| `getIdAuxEnchanted` | id + 数据值 + 是否已附魔的组合信息 |
| `getIsValidPickupTime` | 当前是否已过“刚掉落不可拾取”的保护时间 |
| `getItem` | 关联的 `Item` 类型定义 |
| `sameItemAndAuxAndBlockData` | 物品种类、数据值与关联方块数据是否都相同 |
| `sendEventTriggered` | 上报一个物品定义事件已触发（附带渲染参数） |
| `init` | 按方块类型 + 数量初始化物品（构造方块类物品的常见路径） |
| `getFormattedHovertext` | 按存档等级信息拼装出的完整格式化悬浮文本 |

> `TAG_*` 这批是"用户数据"子标签内部字段的键名常量，与正文"NBT / 序列化"一节提到的 `setUserData`/`ItemStackBase::save` 配合使用；不是需要主动调用的方法，而是拼装/解析用户数据 NBT 时用到的键名。

## 相关类型

| 类型 | 说明 |
| --- | --- |
| `ArmorSlot` | 见 [Entity](/api/entity) |
| `ItemCooldownType` | 冷却类别 |

