# Data — 配置 / 数据库 / 经济 / 玩家数据

> 状态：🧩 规划。
>
> **接口来源说明**：这一页四个子概念里，只有"键值数据库"对应 LeviLamina 真实提供的类；配置文件、SQL 数据库、经济系统在原生/LeviLamina 里都**没有**对应实现——如实说明清楚，而不是假装它们也有原生依据。

## KvDb — 键值数据库

> 唯一真正原生的一块。**接口来源**：`ll::data::KeyValueDB`（`ll/api/data/KeyValueDB.h`），基于 LevelDB 实现的本地键值存储，LeviLamina 自带，不需要额外依赖。

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `KvDb::open(path, create_if_missing?, fix_if_error?, bloom_filter_bits?)` | 打开（或创建）一个键值数据库 | `KeyValueDB::KeyValueDB` |
| `db.get(key)` | 读取（不存在则为空） | `KeyValueDB::get` |
| `db.has(key)` | 是否存在该键 | `KeyValueDB::has` |
| `db.set(key, value)` | 写入 | `KeyValueDB::set` |
| `db.delete(key)` | 删除 | `KeyValueDB::del` |
| `db.is_empty()` | 数据库是否为空 | `KeyValueDB::empty` |
| `db.iter()` | 遍历全部键值对 | `KeyValueDB::iter` |

> 键和值都是字符串；存结构化数据（如一个玩家的多项设置）需要自己序列化成 JSON/SNBT 字符串再存入。

## ConfigFile — 配置文件

> **不对应原生/LeviLamina 类**。这本来就是"读一个人类可编辑的配置文件"这种通用需求，直接用 Rust 生态处理即可：JSON 配置用 `serde_json`（配合 `serde::Deserialize`/`Serialize` 定义配置结构体），INI 用 `ini`/`configparser` 之类的 crate，文件读写用标准库 `std::fs`。不需要、也没有必要为此专门扩展桥接——这是纯 Rust 侧的事，模组在自己的 `Cargo.toml` 里加依赖就行。

## Database — 关系型数据库（SQL）

> **不对应原生/LeviLamina 类**。需要 SQLite/MySQL 这类关系型数据库时，直接在模组的 `Cargo.toml` 里加 `rusqlite`（SQLite）或 `sqlx`/`mysql`（MySQL）这类 Rust crate，正常按 Rust 的方式使用，同样不经过桥接。

## Economy — 经济系统

> **不是原生概念**，Minecraft 本身没有玩家货币系统。这类功能是模组作者自己的业务逻辑，通常的做法是：用上面的 `KvDb`（或一个 SQL 数据库）存"玩家 id → 余额"这张表，自己实现存取款/转账的业务规则。没有、也不需要一个专门的原生 `Money` API。

## PlayerData — 玩家绑定数据

> **不是一个独立的原生系统**，是"用 `KvDb` 存以玩家 uuid 为键的数据"这个模式的简称。也可以选择把数据写进玩家自身的 NBT——[Entity](/api/entity) 页附录里的 `save`（对应原生 `Actor::save`）就是"把整个实体序列化成 NBT"的真实方法，读写自定义数据可以走这条路径，但目前还没有专门包装成一个简单的 get/set 接口，两种路径都可行，看数据要不要跟随存档迁移。
