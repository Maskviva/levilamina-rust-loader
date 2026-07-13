# Container — 容器对象

> 状态：✅ 已支持。
>
> **接口来源**：本页方法对应原生 C++ 抽象基类 `Container`（`mc/world/Container.h`）。"容器"在原生里覆盖箱子、桶、玩家物品栏、盔甲栏、末影箱等一切槽位集合——它们都实现同一个 `Container` 接口，模组代码拿到任意一种容器时都按这套方法调用，不用关心具体是哪一种。
>
> **与 Entity/Player 页不同的一点**：这里列出的方法**大多数原生就是 `virtual`**（公开、非 `$` 前缀），因为 `Container` 本身是抽象接口，`getItem`/`setItem`/`getContainerSize` 等甚至是纯虚函数（`= 0`），必须由具体容器类实现——但这正是模组应该调用的方法，虚函数分发就是"箱子/物品栏/末影箱共用同一套调用方式"的实现机制，不是需要绕开的内部细节（这点和 Entity 页排除的引擎生命周期虚函数不同）。
>
> 获取：从事件回调，或经由玩家/实体句柄（`player.inventory()`、`player.ender_chest()` 等，见 [Player](/api/player)）；箱子等"带容器的方块"要经由其方块实体（[Objects](/api/objects) 的 `BlockEntity`，尚未落实绑定）取得。

以下针对一个容器句柄 `container`。

## 基本信息

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `container.size()` | 槽位总数 | `Container::getContainerSize`（纯虚） |
| `container.max_stack_size()` | 该容器允许的单槽最大堆叠数 | `Container::getMaxStackSize`（纯虚） |
| `container.empty_slots_count()` | 空槽数量 | `Container::getEmptySlotsCount` |
| `container.is_empty()` | 整个容器是否为空 | `Container::isEmpty` |

## 读写槽位

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `container.item(slot)` | 只读方式获取某槽物品 | `Container::getItem`（纯虚） |
| `container.item_mut(slot)` | 获取某槽物品的**可写引用**，用于原地修改（如改数量/耐久） | `Container::getItemNonConst`（LeviLamina 扩展） |
| `container.set_item(slot, item)` | 设置某槽物品（整体替换） | `Container::setItem`（纯虚） |

## 添加 / 移除

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `container.add_item(item)` | 放入物品（自动找位置/堆叠），返回是否完全放下 | `Container::addItem` |
| `container.add_item_with_force_balance(item)` | 同上，但强制在网络层同步平衡 | `Container::addItemWithForceBalance` |
| `container.add_item_to_first_empty_slot(item)` | 只放入第一个空槽（不与已有堆叠合并） | `Container::addItemToFirstEmptySlot` |
| `container.has_room_for(item)` | 是否有空间放入（不实际放入） | `Container::hasRoomForItem` |
| `container.remove_item(slot, count)` | 从某槽减少指定数量 | `Container::removeItem` |
| `container.remove_all_items()` | 清空容器 | `Container::removeAllItems` |
| `container.remove_all_items_with_force_balance()` | 清空并强制网络同步 | `Container::removeAllItemsWithForceBalance` |

## 查找

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `container.slots()` | 全部槽位物品的只读列表 | `Container::getSlots` |
| `container.slot_copies()` | 全部槽位物品的拷贝列表 | `Container::getSlotCopies` |
| `container.first_empty_slot()` | 第一个空槽的下标 | `Container::firstEmptySlot` |
| `container.first_item()` | 第一个非空槽的下标 | `Container::firstItem` |
| `container.find_first_slot_for(item)` | 正向查找匹配该物品的槽位 | `Container::findFirstSlotForItem` |
| `container.reverse_find_first_slot_for(item)` | 反向查找匹配该物品的槽位 | `Container::reverseFindFirstSlotForItem` |
| `container.count_matching(item)` | 统计匹配该物品的总数量 | `Container::getItemCount` |

## 名称

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `container.set_custom_name(name)` | 设置容器自定义名（如命名过的箱子） | `Container::setCustomName` |
| `container.has_custom_name()` | 是否带有自定义名 | `Container::hasCustomName` |

## 掉落

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `container.drop_slot_content(pos, slot, randomize)` | 把某一槽的内容掉落到世界坐标 | `Container::dropSlotContent` |
| `container.drop_contents(pos, randomize)` | 把整个容器的内容掉落到世界坐标 | `Container::dropContents` |

## 漏斗交互规则

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `container.can_push_in(slot, face, item)` | 漏斗等能否从该面把物品推入此槽 | `Container::canPushInItem` |
| `container.can_pull_out(slot, face, item)` | 漏斗等能否从该面把此槽物品抽走 | `Container::canPullOutItem` |

> 修改玩家自身的容器（物品栏/盔甲栏/末影箱）后，需要调用 [Player](/api/player) 的刷新方法让客户端同步显示。

## 附录：其余原生方法

以下是 `Container` 中确实存在、目前简化层还没有对应封装的原生方法，按主题分组，均附一行说明。

**内容变化通知机制**

| 原生方法 | 作用 |
| --- | --- |
| `addContentChangeListener` | 注册一个内容变化监听器 |
| `removeContentChangeListener` | 移除一个内容变化监听器 |
| `getContainerRemovedConnector` | 获取“容器已移除”事件的订阅连接点 |
| `hasRemovedSubscribers` | 是否存在监听“容器已移除”的订阅者 |
| `setContainerChanged` | 标记指定槽位已发生变化（触发通知） |
| `setContainerMoved` | 标记容器整体发生了移动（如箱子被推走） |
| `containerRemoved` | 容器被移除时的回调（通知全部订阅者） |

**打开、关闭与初始化**

| 原生方法 | 作用 |
| --- | --- |
| `startOpen` | 记录有实体打开了该容器（纯虚，具体容器类型各自实现） |
| `stopOpen` | 记录有实体关闭了该容器 |
| `init` | 初始化容器内部状态 |
| `initializeContainerContents` | 在给定区块源上初始化容器内容 |
| `serverInitItemStackIds` | 服务端为槽位物品分配网络同步用的 id（纯虚） |

**存档与事务**

| 原生方法 | 作用 |
| --- | --- |
| `readAdditionalSaveData` | 从存档标签读取额外数据 |
| `addAdditionalSaveData` | 把额外数据写入存档标签 |
| `createTransactionContext` | 创建一个批量操作的事务上下文（提供变更回调与执行入口） |
| `isSlotDisabled` | 指定槽位当前是否被禁用（不可放取） |

**按名字查找容器类型（静态工具）**

| 原生方法 | 作用 |
| --- | --- |
| `getContainerTypeId` | 按名字查找对应的容器类型枚举值 |
| `containerTypeMap` | 容器类型枚举与名字的双向映射表 |
| `sameItemAndAuxComparator` | 按物品描述符构造一个“类型+数据值相同”的比较函数 |

**其他**

| 原生方法 | 作用 |
| --- | --- |
| `getRedstoneSignalFromContainer` | 按容器内容计算应输出的红石信号强度（比较器读数） |
| `removeCloseListener` | 移除一个“容器关闭”监听器 |
| `triggerTransactionChange` | 手动触发一次槽位变更的事务通知 |
| `setItemWithForceBalance` | 设置槽位物品，并强制在网络层同步平衡 |

> 第一组是容器内容变化的内部通知机制（观察者模式）；第二组是容器被打开/关闭/初始化时机的记账钩子；第三组是存档序列化与批量事务的底层细节；第四组是按名字查容器类型的静态工具，多用于自定义方块行为的 C++ 实现中——这几组共同点是：都属于**引擎在特定时机调用、或者实现自定义容器类型时才需要用到**的部分，不是“拿到一个容器句柄后主动去调”的日常操作。

---

**下一步预告**：Entity/Player/Block/Item/Container 这批"对象页附录补充说明"到这里全部完成。剩下还是裸名单、尚未补说明的是 ScoreBoard/Objects/Data/Gui/System 几页——但那几页目前本身就还是规划阶段的设计稿（不是从原生头文件提取的真实清单），要先把它们的正文按 Entity 这套方法重新钉实，才轮到补附录。继续说一声我就接着写。
