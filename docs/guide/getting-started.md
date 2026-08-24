# 快速开始

从零到第一个能跑的 Rust 模组。

## 你需要什么

| 角色 | 需要装 |
| --- | --- |
| **服务器管理员**（只装模组） | LeviLamina + `levilamina-rust-loader`（现成 release，不需要任何工具链） |
| **模组作者**（写模组） | 上面两项 + Rust 工具链（`rustup`，MSVC target）。**不需要** C++ 编译器，不需要 xmake |
| **加载器开发者**（改桥接本身） | 上面全部 + Visual Studio 2022（或 clang-cl）+ xmake，见 [扩展桥接](/advanced/extending) |

## 1. 装加载器（一次性）

1. 在 BDS 上装 [LeviLamina](https://lamina.levimc.org/)。
2. 把 `levilamina-rust-loader` 的 release（DLL + `manifest.json`）放进 `plugins/levilamina-rust-loader/`。
3. 启动服务器，日志里出现加载成功即可。

之后所有 Rust 模组都只是 `plugins/<模组名>/` 下的一个 `.dll` + `manifest.json`，和别的 LeviLamina 模组一样被列出、排序、启用禁用。

## 2. 建工程

推荐从[模板仓库](https://github.com/Maskviva/levilamina-mod-template-rs)开始。手动建的话，模组就是一个普通 `cdylib`：

```toml
# Cargo.toml
[package]
name = "my-mod"
version = "0.1.0"
edition = "2021"

[lib]
crate-type = ["cdylib"]

[dependencies]
levilamina = { git = "https://github.com/Maskviva/levilamina-rust-loader" }
```

再配一份 `manifest.json`（发布时和 DLL 同目录）：

```jsonc
{
    "name": "my-mod",                      // 必须和 plugins/ 下的文件夹同名
    "entry": "my_mod.dll",                 // cargo 输出名（连字符 → 下划线）
    "type": "rust",                        // 关键：交给 rust 加载器
    "platform": "server",
    "version": "0.1.0",
    "dependencies": [
        { "name": "levilamina-rust-loader" }   // 保证加载顺序
    ]
}
```

`"type": "rust"` 加那条 `dependencies` 就是全部魔法：LeviLamina 按依赖拓扑排序加载，加载器先注册好 `"rust"` 类型的模组管理器，轮到你的模组时自然被接管。

### 可选特性

```toml
[dependencies]
# 服务端（默认）
levilamina = { version = "26.20.4" }

# 客户端模组
levilamina = { version = "26.20.4", default-features = false, features = ["client"] }

# 需要自定义维度
levilamina = { version = "26.20.4", features = ["more_dimensions"] }
```

`server` 和 `client` **互斥**，必须恰好开一个。

> **经济功能**：只有用到 [`levilamina::money`](/api/actor/money) 时才需要额外装 [LegacyMoney](https://github.com/LiteLDev/LegacyMoney)。它是**软依赖**——没装的话加载器照常启动，只是 `money::*` 调用空转并警告一次。当硬需求的话，在 `manifest.json` 的 `dependencies` 里也加上它。

## 3. 第一个模组

```rust
use levilamina::prelude::*;
use levilamina::event::names;

struct MyMod;

impl LeviMod for MyMod {
    fn on_load(ctx: &ModContext) -> Result<Self> {
        ctx.logger().info("hello from Rust!");
        Ok(MyMod)
    }

    fn on_enable(&mut self, ctx: &ModContext) -> Result<()> {
        let logger = ctx.logger();

        // 订阅玩家聊天
        ctx.server()
            .subscribe_event(names::PLAYER_CHAT, EventPriority::Normal, move |ev| {
                logger.info(&format!("chat: {}", ev.snbt()));
            })?
            .forget();                     // 活到模组卸载

        // 注册 /hello
        ctx.server().register_command(
            "hello",
            "打个招呼",
            CommandPermission::Any,
            |inv| inv.success("Hello from Rust!"),
        )?;

        Ok(())
    }
}

levilamina::register_mod!(MyMod);
```

三个要点：

- `LeviMod` 是生命周期接口：`on_load`（构造）、`on_enable`、`on_disable`、`on_unload`，除 `on_load` 外都有默认空实现。
- `ModContext` 是钩子里的入口：`ctx.logger()` 拿日志器，`ctx.server()` 拿服务器句柄，其余能力都从 `Server` 出发。
- `register_mod!(MyMod)` 生成加载器需要的导出符号，一个模组写一次。

::: warning `.forget()` 不能省
`subscribe_event` 返回的 `Listener` 一旦被 drop，订阅**立刻失效**。写成 `subscribe_event(...)?;` 就完事的话，那条监听等于没注册——编译器会给 `unused_must_use` 警告，别忽略。
:::

## 4. 构建部署

```shell
cargo build --release
```

```
plugins/
└── my-mod/
    ├── my_mod.dll        # target/release/my_mod.dll
    └── manifest.json
```

启动服务器，进游戏输入 `/hello`。改代码后重新 `cargo build` + 覆盖 DLL + 重启即可。

## 5. 拿到玩家之后

最常见的第一个疑问：**位置、生命值这些怎么读？**

`Player` 上没有，它们在 `Actor` 那一层（原生继承链是 `Player : Mob : Actor`）。转过去：

```rust
let actor = player.get_actor()?;

let (x, y, z) = actor.pos()?;
let hp = actor.health()?;
actor.add_effect("speed", 200, 1, false, true)?;
actor.add_tag("in_arena")?;
```

`Actor` 是 `Entity` 的别名，同一个东西。详见 [Actor / Entity](/api/actor/entity)。

## 6. 接下来读什么

按顺序看完这几页就能写出绝大多数常见模组：

1. [核心概念](/guide/concepts) —— 生命周期、句柄、线程规则、错误处理。**必读**，尤其是线程规则。
2. [事件](/guide/events) —— 订阅、读取、修改、取消。
3. [命令](/guide/commands) —— 执行原版命令 + 注册带参命令。
4. [世界与玩家](/guide/world) —— 方块、实体、扫描，以及"写操作走命令"这个实用模式。
5. [日志与调度](/guide/logging-scheduling) —— 后台线程怎么安全地影响游戏。

> ⚠️ **版本提醒**：加载器的 ABI 大版本需**不低于**模组编译时的版本（加载时自动检查）。新加载器能跑旧模组，旧加载器会拒绝更新的模组。看到"加载器版本过旧"就升级加载器，或用配套版本的 `levilamina` 重新编译。
