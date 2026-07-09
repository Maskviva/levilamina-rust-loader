# Block — 方块对象

> 状态：🧩 规划（单点读取 `World::get_block` 与放置 `World::set_block` 已在当前桥接以坐标形式支持，见 [World](/api/world)；完整句柄属规划）。
>
> **接口来源**：本页方法对应原生 C++ 类 `Block`（`mc/world/level/block/Block.h`），排除引擎内部虚函数插桩（`$` 前缀）。命名沿用 LSE 风格（snake_case）。
>
> **重要架构说明**：原生 `Block` 是**不含位置的"方块类型+状态"值对象**（flyweight）——同一种方块状态在全世界共享同一个 `Block` 实例，取方块/改方块都要另外传入 `(BlockSource, BlockPos)`。本页把"查询/操作某坐标处的方块"这类调用也放进 `block.xxx()` 的形式里，但要知道**原生对应的方法大多需要额外的 `region`/`pos` 参数**（桥接层会把持有的坐标自动带入）。方块状态的修改在原生是"查表换一个新的 `Block` 引用"而非原地改值（见下方"状态"一节）。
>
> 获取：从事件回调，或 `World::get_block(dim, pos)`（见 [World](/api/world)）。

以下针对一个方块句柄 `block`（内部已绑定坐标/维度）。

## 身份

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `block.type_name()` | 标准类型名，如 `"minecraft:stone"` | `Block::getTypeName`（内联方法，非 MCAPI） |
| `block.tags()` | 该方块的 Tag 列表（如 `"stone"`、`"diamond_pick_diggable"`） | `Block::mTags`（公开成员字段，非方法） |
| `block.data()` | 原始状态数据值 | `Block::getData`（内联方法） |
| `block.block_item_id()` | 对应的数字方块/物品 id | `Block::getBlockItemId`（内联方法） |
| `Block::find(query)` | 按 运行时id / 传统id+data / 名字 / 名字+states / NBT 六种方式之一查找方块类型 | `Block::tryGetFromRegistry`（6 个重载） |
| `block.is_air()` | 是否为空气 | `Block::isAir` |
| `block.has_tag(tag)` | 是否带有指定 Tag（等价于在 `tags()` 里查找，但走原生哈希比较更快） | `Block::hasTag` |

## 状态（进阶）

原生的方块状态是**类型化、按状态表查询**的：`Block::getState<T>(state)` 读某个具名状态（`int`/`float`/`bool`/`string` 之一），`Block::setState<T>(state, value)` 不是原地修改，而是**返回另一个代表新状态的 `Block const&`**——方块对象本身不可变。这套模板接口暂未在简化层绑定（跨 FFI 桥接模板方法需要额外的类型擦除设计）；今天读写整块状态的实际路径是 [World](/api/world) 里已支持的 `get_block`/`set_block`，两者都以完整的名字+状态 SNBT 字符串一次性表达，等效覆盖了这个能力。

## 与世界交互

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `block.may_place_at(pos, face)` | 能否在指定面放置 | `Block::mayPlace` |
| `block.can_survive_at(pos)` | 在该位置能否维持存在（如缺乏支撑会掉落/损坏） | `Block::canSurvive` |
| `block.use(player, pos, face, hit?)` | 模拟玩家对该方块的使用交互（如开箱子） | `Block::use` |
| `block.player_destroy(player, pos)` | 模拟玩家破坏该方块（触发掉落等副作用） | `Block::playerDestroy` |
| `block.second_part(pos)` | 双格方块（门/床等）另一半的坐标 | `Block::getSecondPart` |
| `block.as_item_instance(pos, with_data)` | 转换为拾取时的物品形式 | `Block::asItemInstance` |
| `block.pop_resource(pos, item)` | 在该坐标掉出一个物品实体 | `Block::popResource` |

## 形状

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `block.collision_shape(pos)` | 碰撞箱（AABB） | `Block::getCollisionShape` |
| `block.outline(pos)` | 选取框（AABB，用于渲染方块轮廓） | `Block::getOutline` |
| `block.clip(pos, from, to)` | 对该方块做一次射线检测 | `Block::clip` |

## 分类判断

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `block.is_crafting_block()` | 是否为工作台类方块 | `Block::isCraftingBlock` |
| `block.is_interactive_block()` | 是否可交互（如箱子、按钮） | `Block::isInteractiveBlock` |
| `block.is_partial_block(pos)` / `is_top_partial_block(pos)` | 是否为半高/顶部半高方块 | `Block::isPartialBlock` / `isTopPartialBlock` |
| `block.is_solid_blocking_and_not_signal_source()` | 是否为"实心且非红石信号源"的方块 | `Block::isSolidBlockingBlockAndNotSignalSource` |
| `block.can_connect(other, face)` | 能否与相邻方块相连（如围栏/墙） | `Block::canConnect` |
| `block.can_fill_at_pos(pos)` | 该位置能否被液体填充 | `Block::canFillAtPos` |
| `block.breaks_falling_blocks(version)` | 沙砾等下落方块落到此处是否会被打破 | `Block::breaksFallingBlocks` |

## 文本

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `block.description_name()` | 用于描述/翻译的名称 | `Block::buildDescriptionName` |
| `block.description_id()` | 翻译 key | `Block::getDescriptionId` |
| `block.crafting_label_text()` | 合成表里的标签文字 | `Block::getCraftingLabelText` |
| `block.debug_string()` | 调试用的完整字符串表示 | `Block::toDebugString` |

## 附录：其余原生方法

以下是 `Block` 中确实存在、目前简化层还没有对应封装的原生方法，按主题分组，均附一行说明。

**方块行为生命周期钩子**

| 原生方法 | 作用 |
| --- | --- |
| `connectionUpdate` | 相邻方向发生连接关系变化时，返回应更新成的新方块状态（如围栏/墙/玻璃板的连接外观） |
| `neighborChanged` | 相邻坐标的方块发生变化时的回调 |
| `onPlace` | 该方块刚被放置时的回调（可读到放置前的方块） |
| `onStateChange` | 该方块状态发生变化时的回调 |
| `onStepOn` | 有实体踏上该方块时的回调 |
| `onStepOff` | 有实体离开该方块时的回调 |
| `onFallOn` | 有实体高空坠落砸中该方块时的回调 |
| `onActorInternalEvent` | 该坐标发生一个实体内部事件时的回调 |
| `queuedTick` | 该方块的一次排队延迟 tick（如红石信号的延迟更新） |
| `randomTick` | 该方块的一次随机 tick（如作物生长、草方块蔓延） |
| `executeItemEvent` | 对该方块执行一个具名的物品交互事件 |
| `shouldRandomTick` | 该方块是否参与随机 tick |

**状态系统配套方法**

| 原生方法 | 作用 |
| --- | --- |
| `copyState` | 把来源方块的某一项具名状态复制到当前方块，返回结果方块（原值不可变，见正文"状态"一节） |
| `copyStates` | 把来源方块的全部状态复制过来，返回结果方块 |
| `getStateFromLegacyData` | 按旧版数字数据值解析出对应的方块状态 |
| `hasState` | 是否存在指定名字的状态字段 |
| `getConnectedDirections` | 按坐标计算当前应该连接的水平方向集合 |
| `forEachState` | 遍历该方块类型定义的全部状态字段 |

**序列化与网络**

| 原生方法 | 作用 |
| --- | --- |
| `buildSerializationId` | 按目标数据版本构建序列化用的标识（用于存档升级迁移） |
| `computeRawSerializationIdHashForNetwork` | 计算序列化标识用于网络同步的哈希值 |

**其他**

| 原生方法 | 作用 |
| --- | --- |
| `BLOCK_DESCRIPTION_PREFIX` | （静态）方块描述文本使用的统一前缀常量 |
| `getOcclusionFaceShape` | 指定面的遮挡形状（用于判断相邻面是否被完全遮住，如渲染剔除） |
| `getRandomOffset` | 该坐标下用于渲染的随机偏移量（如植物类方块的位置抖动） |
| `isPreservingMediumWhenPlaced` | 放置时是否保留原介质（如水下放置某些方块后水是否留存） |
| `spawnResources` | 生成该方块被破坏时应掉落的资源 |

> 第一组是**方块行为的生命周期钩子**——原生里是引擎在方块被放置/相邻变化/被踩踏等时机主动调用它们，而不是模组持有一个 `Block` 句柄后主动去调；只有自己用 C++ 实现新方块行为时才会用到。第二组属于正文"状态（进阶）"一节提到的模板状态系统的配套方法。

## 相关类型

| 类型 | 说明 |
| --- | --- |
| `HitResult` | `clip` 的返回：命中类型与命中点 |

