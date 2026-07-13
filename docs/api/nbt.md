# Nbt — NBT 读写

> 状态：✅ 已支持。除了事件数据、方块/实体扫描结果以 SNBT 文本跨 FFI 传递（见 [Event](/api/event)/[World](/api/world)）之外，背后的 `CompoundTag`/`CompoundTagVariant` 对象模型也已封装成 Rust 里可直接操作的结构化对象（`Nbt`），无需手工拼 / 解析 SNBT 字符串。
>
> **接口来源**：`mc/deps/nbt/` 下的一族真实类型：`Tag`（基类）、`CompoundTag`（复合标签）、`CompoundTagVariant`（类型擦除的"任意 NBT 值"包装，`CompoundTag` 内部就是靠它存字段值）、以及各叶子类型 `ByteTag`/`ShortTag`/`IntTag`/`Int64Tag`/`FloatTag`/`DoubleTag`/`StringTag`/`ByteArrayTag`/`IntArrayTag`/`ListTag`。命名沿用 LSE 风格（snake_case）。
>
> **设计特点**：`ListTag` 直接继承 `std::vector<UniqueTagPtr>`，`StringTag` 直接继承 `std::string`——叶子/列表标签就是对应的标准容器/类型本身，不是外面包一层。`CompoundTagVariant` 则更像 `nlohmann::json`：`is_object()`/`is_string()`/`is_number()` 这类判断，加一个模板化的 `get<T>()` 取具体类型。

## 标签类型

原生 `Tag::Type` 一共 12 种（没有独立的 Long Array 类型）：

| Type | 说明 |
| --- | --- |
| `End` | 空/终止标记 |
| `Byte` | 单字节（也用来表示布尔值：0/1） |
| `Short` | 16 位整数 |
| `Int` | 32 位整数 |
| `Int64` | 64 位整数 |
| `Float` | 32 位浮点 |
| `Double` | 64 位浮点 |
| `ByteArray` | 字节数组 |
| `String` | 字符串 |
| `List` | 同类型标签的有序列表 |
| `Compound` | 具名字段的映射（最常打交道的一种） |
| `IntArray` | 32 位整数数组 |

## Compound（复合标签）

对应 `CompoundTag`。内部就是一个有序 `map<字符串, 值>`（`CompoundTag::mTags`）。

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `NbtValue::parse(snbt)` | 解析一段 SNBT 文本，失败时带错误信息 | `CompoundTag::fromSnbt`（返回 `Expected<CompoundTag>`，不是裸指针/`Option`） |
| `tag.to_snbt(format?, indent?)` | 序列化为 SNBT 文本 | `Tag::toSnbt` |
| `Nbt::from_binary(bytes, little_endian=true)` / `tag.to_binary(little_endian=true)` | 与二进制 NBT 互转（存档格式） | `CompoundTag::fromBinaryNbt` / `toBinaryNbt` |
| `Nbt::from_network_binary(bytes)` / `tag.to_network_binary()` | 与网络传输用的二进制 NBT 互转（和存档格式不完全相同） | `CompoundTag::fromNetworkNbt` / `toNetworkNbt` |
| `tag.get(key)` / `tag[key]` | 按键取值（不存在则按需插入空值） | `CompoundTag::operator[]` / `at` |
| `tag.contains(key)` / `contains_as(key, type)` | 是否含有该键（可选校验类型） | `CompoundTag::contains` |
| `tag.set(key, value)` | 写入一个字段 | 对 `mTags` 赋值（经由 `operator[]`） |
| `tag.erase(key)` | 删除一个字段 | `CompoundTag::erase` |
| `tag.rename(key, new_key)` | 重命名一个字段 | `CompoundTag::rename` |
| `tag.append(other)` | 合并另一个 Compound 的字段进来 | `CompoundTag::append` |
| `tag.clone()` | 深拷贝 | `CompoundTag::clone` |
| `tag.size()` / `tag.is_empty()` | 字段数量 / 是否为空 | `CompoundTag::size` / `empty` |
| `tag.keys()` | 遍历全部键 | 基于 `CompoundTag` 的迭代器 |

## Value（任意 NBT 值）

对应 `CompoundTagVariant`——`Compound` 里每个字段、`List` 里每个元素，实际存的都是这个类型。

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `value.kind()` | 返回具体是上面 12 种里的哪一种 | `CompoundTagVariant::index` / `getId` |
| `value.is_null()` / `is_object()` / `is_array()` / `is_string()` / `is_number()` / `is_number_int()` / `is_number_float()` / `is_boolean()` / `is_binary()` | 类型判断 | 对应的 `is_xxx()` 系列 |
| `value.as_compound()` / `as_list()` / `as_str()` / `as_i64()` / `as_f64()` / `as_bool()` | 按类型取值（类型不符时返回 `None`） | `CompoundTagVariant::get<T>()` |
| `value[key]` | 当值持有 Compound 时，直接按键取子字段（无需先手动转换类型） | `CompoundTagVariant::operator[]` |
| `Nbt::compound_value(fields)` | 直接构造一个 Compound 类型的值 | `CompoundTagVariant::object(...)` |
| `Nbt::array_value(items)` | 直接构造一个 List 类型的值 | `CompoundTagVariant::array(...)` |

> 现有桥接内部已经在用这套真实 API：把玩家真实身份拼进事件数据时，调用的正是 `CompoundTagVariant::object({...})` 和 `value.is_string()`/`get<CompoundTag>()`（见 `bridge/Common.cpp` 的 `enrichWithPlayer`）。

## List（列表标签）

对应 `ListTag`，直接继承 `std::vector<UniqueTagPtr>`，天然具备容器的全部能力（长度、遍历、下标）。

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `list.push(value)` | 追加一项 | `ListTag::add` |
| `list.get(index)` | 按下标取值 | `ListTag::get` |
| `list.clone()` | 深拷贝整个列表 | `ListTag::copyList` |
| `list.len()` | 元素个数 | 继承自 `std::vector` |

## 相关类型

| 类型 | 说明 |
| --- | --- |
| `SnbtFormat` | SNBT 输出格式（紧凑 `Minimize` / 便于阅读的 `PrettyFilePrint` 等） |
| `Tag::Type` | 见上方"标签类型"表 |

