# 快速开始

本页带你从零到第一个能跑起来的 Rust 模组。**只讲今天就能编译运行的代码**；完整的目标 API 面（含规划中的能力）见 [API 参考](/api/overview)。

## 你需要什么

| 角色 | 需要安装 |
| --- | --- |
| **服务器管理员**（只装模组） | LeviLamina + `levilamina-rust-loader` 加载器（现成的 release 即可，无需任何工具链） |
| **模组作者**（写模组） | 上面两项 + Rust 工具链（`rustup`，MSVC target）。**不需要** C++ 编译器、不需要 xmake |
| **加载器开发者**（改桥接本身） | 上面全部 + Visual Studio 2022（或 clang-cl）+ xmake，见 [高级开发](/advanced/extending) |

## 1. 安装加载器（一次性）

1. 在 BDS 上安装 [LeviLamina](https://lamina.levimc.org/)。
2. 把 `levilamina-rust-loader` 的 release（DLL + `manifest.json`）放进 `plugins/levilamina-rust-loader/`。
3. 启动服务器，日志里出现加载器加载成功即可。

之后所有 Rust 模组都只是 `plugins/<模组名>/` 下的一个 `.dll` + `manifest.json`，和其他 LeviLamina 模组完全一样地被列出、排序、启用/禁用。

## 2. 创建模组工程

推荐直接从[模板仓库](https://github.com/Maskviva/levilamina-mod-template-rs)开始。手动创建的话，一个模组就是一个普通的 `cdylib` crate：

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

再配一份 `manifest.json`（发布时和 DLL 放在同一目录）：

```jsonc
{
    "name": "my-mod",                      // 必须与 plugins/ 下的文件夹同名
    "entry": "my_mod.dll",                 // cargo 输出名（连字符 → 下划线）
    "type": "rust",                        // 关键：交给 rust 加载器处理
    "platform": "server",
    "version": "0.1.0",
    "dependencies": [
        { "name": "levilamina-rust-loader" }   // 保证加载顺序：加载器先于你的模组
    ]
}
```

`"type": "rust"` 和那条 `dependencies` 是这套机制的全部魔法：LeviLamina 按依赖拓扑排序加载，加载器先注册好 `"rust"` 类型的模组管理器，轮到你的模组时自然被它接管。

> **可选：经济功能**。只有当你用到 [`levilamina::money`](/api/money)（余额/转账/流水等）时，才需要额外装 [LegacyMoney](https://github.com/LiteLDev/LegacyMoney) 插件。它是**软依赖**——没装的话加载器照常启动，只是 `money::*` 调用会空转并在控制台警告一次。若你的模组把它当硬需求，记得在 `manifest.json` 的 `dependencies` 里也加上 `{ "name": "LegacyMoney" }`，让 LeviLamina 保证加载顺序。不碰经济功能就无需关心它。

## 3. 写第一个模组

```rust
use levilamina::prelude::*;

struct MyMod;

impl LeviMod for MyMod {
    fn on_load(ctx: &ModContext) -> Result<Self> {
        ctx.logger().info("hello from Rust!");
        Ok(MyMod)
    }

    fn on_enable(&mut self, ctx: &ModContext) -> Result<()> {
        let logger = ctx.logger();

        // 订阅玩家聊天事件
        ctx.server()
            .subscribe_event("PlayerChatEvent", EventPriority::Normal, move |ev| {
                logger.info(&format!("chat event: {}", ev.snbt()));
            })?
            .forget(); // 让监听存活到模组卸载

        // 注册一个 /hello 命令
        ctx.server().register_command(
            "hello",
            "say hello",
            CommandPermission::Any,
            |inv| inv.success("Hello from Rust!"),
        )?;

        Ok(())
    }
}

levilamina::register_mod!(MyMod);
```

要点：

- `LeviMod` 是模组的生命周期接口：`on_load`（构造）、`on_enable`、`on_disable`、`on_unload` 四个钩子，除 `on_load` 外都有默认空实现。
- `ModContext` 是钩子里的入口对象：`ctx.logger()` 拿日志器，`ctx.server()` 拿服务器句柄，之后所有能力都从 `Server` 出发。
- `register_mod!(MyMod)` 宏生成加载器需要的导出符号，一个模组写一次。

## 4. 构建与部署

```shell
cargo build --release
```

把产物部署到服务器：

```
plugins/
└── my-mod/
    ├── my_mod.dll        # target/release/my_mod.dll
    └── manifest.json
```

启动服务器，进游戏输入 `/hello`，控制台里也能看到聊天事件的日志。就这些——改代码后重新 `cargo build` + 覆盖 DLL + 重启即可。

## 5. 接下来读什么

按顺序看完初级开发这几页，就能写出绝大多数常见模组：

1. [核心概念](/guide/concepts) —— 生命周期、句柄、线程规则、错误处理。**必读**，尤其是线程规则。
2. [事件](/guide/events) —— 订阅、读取、修改、取消事件。
3. [命令](/guide/commands) —— 执行原版命令 + 注册自定义命令。
4. [世界与玩家](/guide/world) —— 粒子、玩家坐标、区域扫描，以及"一切写操作走命令"的实用模式。
5. [日志与调度](/guide/logging-scheduling) —— 以及后台线程（Tokio 等）如何安全地影响游戏。

> ⚠️ **版本提醒**：加载器的 ABI 大版本需**不低于**模组编译时的版本（加载时自动检查）——新加载器能跑旧模组，但旧加载器会拒绝更新的模组。若看到"加载器版本过旧"之类的提示，升级加载器，或用配套版本的 `levilamina` crate 重新编译模组。详见[核心概念](/guide/concepts)。