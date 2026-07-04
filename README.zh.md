# levilamina-rust-loader

![levilamina-rust-loader](https://socialify.git.ci/Maskviva/levilamina-rust-loader/image?description=1&font=Raleway&forks=1&issues=1&language=1&logo=https%3A%2F%2Fraw.githubusercontent.com%2FLiteLDev%2FLeviLamina%2Frefs%2Fheads%2Fmain%2Fdocs%2Fmain%2Fcontents%2Flogo.svg&name=1&owner=1&pattern=Circuit+Board&pulls=1&stargazers=1&theme=Auto)

![English](https://img.shields.io/badge/English-inactive?style=for-the-badge)
[![中文](https://img.shields.io/badge/简体中文-informational?style=for-the-badge)](README.zh.md)

**让 [LeviLamina](https://github.com/LiteLDev/LeviLamina) 能够加载用 Rust 编写的 Minecraft 基岩版专用服务器 mod。**

这个仓库是"引擎"本体：一个 C++ loader mod，加上跟它讲同一套 ABI 的两个 Rust crate。在服务器上装一次，之后写 Rust mod 就只需要 `cargo build`——不需要 C++ 工具链，不需要 xmake，mod 作者这边没有任何胶水代码。

想**写 mod** 而不是编译 loader？直接去
[**levilamina-mod-template-rs**](https://github.com/Maskviva/levilamina-mod-template-rs)，
只有在你想搞清楚原理或者想贡献代码时才需要回到这里。

## 工作原理

```
┌─────────────────────────── bedrock_server_mod.exe ───────────────────────────┐
│  LeviLamina (C++)                                                            │
│    └─ levilamina-rust-loader（本仓库，C++，装一次）                          │
│         • 为 manifest `"type": "rust"` 注册一个 ModManager                    │
│         • 加载你的 cdylib，调用 levi_rs_main(api, handle, out_vtable)        │
│         • 递交一份带版本号的 C 函数表（LeviRsApi）                            │
│              ├─ events   ：EventBus + DynamicListener ⇆ SNBT 字符串           │
│              ├─ commands ：RuntimeCommand 重载 / 控制台执行                   │
│              ├─ schedule ：ServerThreadExecutor（线程安全的入口）             │
│              └─ logging  ：每个 mod 各自的 LeviLamina logger                  │
│    └─ 你的 mod（纯 Rust cdylib，`"type": "rust"`）                           │
└───────────────────────────────────────────────────────────────────────────────┘
```

设计要点：

- **一等公民的 mod。** Rust mod 住在 `plugins/<name>/`，带一份普通的
  `manifest.json`，参与依赖排序，在 LeviLamina 的 mod 列表里正常显示——因为它们
  确实是被一个真正的 `ModManager` 管理的，不是从旁门偷偷塞进去的。
- **一条通用事件通道。** LeviLamina 的 `DynamicListener` 会把任意事件序列化成
  `CompoundTag`；桥接层把它转成 SNBT 来回传递，所以 Rust 可以监听**并且修改
  /取消**任何事件——包括其他 mod 发出的事件——不需要为每个事件单独写 C++
  绑定。游戏内输入 `/levirs events` 可以把服务器上所有已知事件 id 打印出来。
- **诚实的线程模型。** 所有回调都跑在服务器线程上。`Server::schedule{,_after}`
  是唯二线程安全的入口——这正是给 Tokio 后台任务、agent 之类的东西预留的桥。
- **有版本号、只增不减的 C ABI**（`src/LeviRsAbi.h` 是唯一的事实来源，被
  `crates/levilamina-sys` 逐字段镜像）。loader 和 mod 版本号对不上时会拒绝配对，
  而不是冒险产生未定义行为。
- **Panic 安全。** `levilamina` crate 里每一个 FFI 边界都包了一层
  `catch_unwind`；panic 会被记录成一条错误日志，而不是直接带崩服务器。
- **统一内存分配器。** `src/MemoryOperators.cpp` 把这个 DLL 的
  `operator new`/`delete` 接到了 LeviLamina 的统一分配器上，跟任何原生 mod
  一样——这是 loader 能被加载的前提条件。

## 仓库结构

```
src/                    C++ loader mod（编译出 levilamina-rust-loader.dll）
crates/levilamina-sys/  对 src/LeviRsAbi.h 的裸 #[repr(C)] FFI 镜像
crates/levilamina/      建立在 levilamina-sys 之上的安全 Rust API
docs/DESIGN.md          ABI 与架构决策、演进规则
docs/PORTING_NOTES.md   这个桥接层依赖的 LeviLamina API 调用点清单
xmake.lua               编译 C++ mod（仓库根目录同时也是 xmake 项目根目录）
Cargo.toml              两个 Rust crate 的 workspace（仓库根目录同时也是 cargo workspace 根目录）
```

`crates/levilamina-sys` 和 `crates/levilamina` 从这个仓库发布（而不是模板仓库），
因为它们跟 C++ loader 的版本号是锁在一起演进的——演进规则见
[`docs/DESIGN.md`](docs/DESIGN.md) 第 8 节。C 头文件和 `-sys` crate 永远在同一次
提交里一起改。

## 安装（服务器管理员视角）

1. 在你的 BDS 上安装 [LeviLamina](https://lamina.levimc.org/)。
2. 把 `levilamina-rust-loader` 的发布包放进 `plugins/levilamina-rust-loader/`
   （或者自己编译，见下文）。
3. 把 Rust mod 装进 `plugins/<mod名>/`——跟其他任何 LeviLamina mod 一样，都是一个
   `.dll` 加一份 `manifest.json`。

## 编译 loader

需要 [xmake](https://xmake.io) 和 Visual Studio 2022（或 clang-cl）工具链，跟
任何 LeviLamina 原生 mod 一样。**编译前务必把 `xmake.lua` 里
`levilamina`/`bedrockdata`/`prelink` 的版本号锁定成你服务器实际在跑的版本**——
不锁版本的话，`levilamina` 可能悄悄解析到一个比你服务器上的 LeviLamina 更新的
SDK 版本，表现出来就是一堆莫名其妙的编译报错（成员找不到、签名变了），而不是
一条清楚的版本不匹配提示。

```shell
xmake f -m release -y
xmake
```

编译产物（DLL + manifest）会出现在 `bin/`。

如果还想编译/测试这个 workspace 里的 Rust crate：

```shell
cargo build --workspace
cargo test --workspace
```

## 基于这个 loader 写 mod

大部分人应该从
[模板仓库](https://github.com/Maskviva/levilamina-mod-template-rs)
开始，而不是自己手动接线。如果你是往一个已有的 crate 里加 `levilamina`：

```toml
# Cargo.toml
[lib]
crate-type = ["cdylib"]

[dependencies]
levilamina = { version = "0.1.0", git = "https://github.com/Maskviva/levilamina-rust-loader" }
# 发布到 crates.io 之后可以换成：levilamina = "0.1"
```

```jsonc
// manifest.json —— 跟你的 .dll 一起放在 mods/<名字>/ 里
{
    "name": "my-mod",                       // 必须跟文件夹名一致
    "entry": "my_mod.dll",                  // cargo 的产物名（连字符会变下划线）
    "type": "rust",
    "platform": "server",
    "dependencies": [{ "name": "levilamina-rust-loader" }]  // 保证加载顺序
}
```

## ABI 稳定性

`LEVI_RS_ABI_VERSION` 决定兼容性。同一个大版本内，`LeviRsApi` 只会在末尾追加
新字段（用 `struct_size` 兜底判断有没有），绝不会重排或删除已有字段。完整契约
（包括线程模型、每次 FFI 调用都要遵守的字符串/内存所有权规则）见
[`docs/DESIGN.md`](docs/DESIGN.md)。

## 当前状态与路线图

v0.1 有意做成一个**范围小但正确的核心**——事件/命令/调度/日志这几个通用原语，
`execute_command` 已经能覆盖大部分原版行为，其余功能都能在这个基础上搭出来。

- [ ] **v0.2** —— 结构化 SNBT：基于 `serde` 的类型化事件视图，取代现在字符串
      级别的 `cancel()`，实现真正的 NBT 编辑
- [ ] **v0.2** —— 直接访问世界的快速通道（`get_block`/`set_block`/区域快照），
      绕开命令解析器的开销
- [ ] **v0.3** —— 玩家句柄（发消息/toast/表单）、表单 API
- [ ] **v0.3** —— Linux 支持（取决于 LeviLamina 自己的 Linux 目标进度）
- [ ] **v0.x** —— async 优先的 API 面（调度器之上的 `ServerHandle::run(async fn)`）、
      Tokio 集成示例、AI agent 示例
- [ ] 过程宏语法糖：`#[levilamina::event]`、`#[levilamina::command]`

欢迎贡献——ABI 演进规则记录在 [`docs/DESIGN.md`](docs/DESIGN.md) 里。

## 常见问题

**为什么不直接用纯 Rust 对接 LeviLamina，而要套一层 C++ 壳？**
LeviLamina 的 mod 入口是纯 C 的（`ll_mod_load` 等），但真正有用的东西——事件、
命令、协程执行器——全是现代 C++（模板、`std::function`、C++20 协程），没有 C
导出层。loader 还强制要求统一内存分配契约，只有针对它的头文件编译出来的代码
才能诚实满足这一点。用一层薄薄的、经过审查的 C++ 把这些 API 拍平成一张带版本号
的 C 表，是正确的工程解法；"纯 Rust" 意味着要手写针对 MSVC 名字修饰符号的
FFI——相当于重新实现一遍 LeviLamina。

**能在客户端用吗？** 没测试过；loader 目标是 `"platform": "server"`。
LeviLamina 的客户端支持还是比较新的领域。

**模板仓库在哪？**
[`levilamina-mod-template-rs`](https://github.com/Maskviva/levilamina-mod-template-rs)——
故意分开放，这样"新建一个 mod"始终是 GitHub 上点一下 "Use this template" 就能
完成的事，而不用 fork 这整个引擎仓库。

**协议：** 本仓库 Apache-2.0。LeviLamina 本身是 LGPL-3.0；loader mod 是以动态
链接的方式、作为一个普通 mod 去调用它的。

---

*与 Mojang、Microsoft、LeviMC 均无关联。Minecraft 是 Mojang Synergies AB 的
商标。*
