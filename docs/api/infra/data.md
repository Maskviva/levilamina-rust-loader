# Data — 持久化

## KvDb — 键值存储

底层是 LevelDB，**限制在模组自己的数据目录里**。

```rust
use levilamina::prelude::*;

let db = KvDb::open("plots")?;         // → bds/data_mods/<你的模组>/plots
db.set("plot:1", r#"{"owner":"steve"}"#)?;
if let Some(v) = db.get("plot:1") { println!("{v}"); }
```

| API | 返回 | 说明 |
| --- | --- | --- |
| `KvDb::open(path)` | `Result<KvDb>` | 打开或创建 |
| `KvDb::open_existing(path)` | `Result<KvDb>` | 必须已存在，否则失败 |
| `db.get(key)` | `Option<String>` | `None` = 键不存在 |
| `db.set(key, value)` | `Result<()>` | |
| `db.del(key)` | `Result<()>` | |
| `db.has(key)` | `bool` | |
| `db.is_empty()` | `bool` | |
| `db.iter()` | `Vec<(String, String)>` | 全部键值对 |

`path` 必须是**相对路径**，最终落在 `bds/data_mods/<模组名>/<path>`。

::: tip KvDb 是线程安全的
桥接内部用一把互斥锁保护每次操作，所以可以放心分享给后台线程。这在整个 SDK 里是少数几个线程安全的东西之一。
:::

::: warning iter() 会把整个库读进内存
库大了就别用。把键设计成有结构的（`plot:世界:编号`），然后直接 `get`。
:::

关闭是 `Drop` 时自动做的；模组卸载时还开着的库会被加载器强制关掉并打一条警告。

## 配置文件

**没有封装**，用标准库就行：

```rust
use std::fs;
use serde::{Deserialize, Serialize};

#[derive(Serialize, Deserialize, Default)]
struct Config { max_plots: u32 }

let dir = std::path::Path::new("plugins/my-mod");
fs::create_dir_all(dir)?;
let path = dir.join("config.json");

let cfg: Config = if path.exists() {
    serde_json::from_str(&fs::read_to_string(&path)?)?
} else {
    let d = Config::default();
    fs::write(&path, serde_json::to_string_pretty(&d)?)?;
    d
};
```

::: tip 配置读失败应该让模组加载失败
静默用默认值硬跑是很糟的失败模式：服主改了配置发现没生效，而日志里什么都没有。在 `on_load` 里直接返回 `Err`，模组不加载，控制台有明确的错误。
:::

## 目录约定

| 用途 | 位置 |
| --- | --- |
| 模组本体 + manifest | `plugins/<模组名>/` |
| KvDb 数据 | `bds/data_mods/<模组名>/` |
| 加载器自己的配置 | `configs/levilamina-rust-loader/` |

## 经济

余额、转账、流水在 [Money](/api/actor/money)。
