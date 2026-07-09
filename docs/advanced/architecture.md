# 架构与 ABI 设计

本页说明整个项目怎么分层、模组如何被加载，以及"为什么长这样"。API 具体有什么见 [API 参考](/api/overview)；契约细节与演进规则见 [ABI 契约与演进](/advanced/abi)。

## 四层架构

```
┌─────────────────────────────────────────┐
│ ① LeviLamina / BDS 原生 C++                │  真正的游戏引擎
│    Actor / Player / Level / Block / …      │  （API 各页对照的真实类）
└─────────────────────────────────────────┘
                    ▲  普通 C++ 调用
┌─────────────────────────────────────────┐
│ ② levilamina-rust-loader（C++ 桥接）        │  BridgeApi / RustModManager / RustMod
│    实现 LeviRsApi 函数表，加载 Rust cdylib   │  src/bridge/*.cpp
└─────────────────────────────────────────┘
                    ▲  LeviRsAbi.h 定义的 C ABI
┌─────────────────────────────────────────┐
│ ③ levilamina-sys（Rust 原始 FFI 镜像）       │  #[repr(C)]，无逻辑，#![no_std]
└─────────────────────────────────────────┘
                    ▲  普通 Rust 调用
┌─────────────────────────────────────────┐
│ ④ levilamina（Rust 安全封装）                │  Server / Player / Entity / …
│    模组作者实际写代码用的这一层              │  （API 参考描述的就是这层）
└─────────────────────────────────────────┘
```

模组作者只碰第 ④ 层。第 ③ 层存在的唯一理由是让第 ④ 层不用手写 `unsafe extern "C"` 签名——它就是 `LeviRsAbi.h` 逐字段翻译成 Rust 的 `#[repr(C)]` 类型，本身不包含任何逻辑。真正调用游戏引擎（第①层）的代码全部在第②层的 C++ 桥接里。

这个分层直接决定了"加新 API"要动几个地方：声明加进 `LeviRsAbi.h`（②的契约）→ 在 `src/bridge/*.cpp` 里实现（②调用①）→ 在 `levilamina-sys` 里镜像类型（③）→ 在 `levilamina` 里包一层安全接口（④，也就是 API 参考文档写的东西）。四层都要动，但每层职责单一，改起来不会互相牵连。

## ABI 契约（概要）

`LeviRsAbi.h` 是加载器与模组之间那份契约**唯一的真相来源**——`levilamina-sys` 逐字段镜像它，谁都不能单独改。核心是 `LeviRsApi`：一张扁平的函数指针表，在模组加载时一次性交给 Rust 侧，指针在模组整个生命周期内有效。版本用 `abi_version`（大版本，必须一致）+ `struct_size`（前向兼容检查）两个数字把关，演进规则只有一条：**新字段永远追加在表尾，从不重排、从不删除**。

完整的契约规则书——字符串与内存约定、线程契约、panic 策略、演进规则与版本史——单独成页：[ABI 契约与演进](/advanced/abi)。

## 字符串跨界：一个"验证假设"的案例

`LeviRsStr` 就是 `std::string_view` 的别名——不是自定义结构体，而是直接复用它的内存布局 `{指针, 长度}`。问题是：这个 `{ptr, len}` 布局并不是 C++ 标准保证的，只是 MSVC STL 当前版本的实现细节（甚至可能因编译配置——比如是否开 checked iterators——而变化）。Rust 那边 `levilamina-sys::LeviRsStr` 必须独立声明一份 `#[repr(C)]` 镜像，两边的内存布局要是对不上，跨界传字符串时读到的指针/长度就是错位的，属于静默数据损坏，比崩溃更难排查。

应对方式是**不假设，运行时验证**：`leviRsVerifyStrLayout()` 在加载器启动、任何 Rust 模组加载之前跑一次，把一个已知内容的 `string_view` 按 `{const char*, size_t}` 重新解释，比较解释出来的指针/长度是否和 `.data()`/`.size()` 一致；同时编译期还有一个 `static_assert` 兜底。两道检查任何一道失败就拒绝加载，不会让一个错误的布局假设悄悄带病运行。

这条思路——**不确定的跨语言假设，用运行时探测替代默默相信**——在这个项目里不是孤例，见[内存安全与生命周期](/advanced/memory-safety)里"句柄"设计用的是同一种谨慎。

## 模组生命周期

C++ 侧（`RustModManager::load`）：`dlopen` 目标 cdylib → 找到导出符号 `levi_rs_main` → 调用它，传入 API 表指针 + 一个不透明句柄 + 一个待填充的 vtable 输出参数 → 检查 `vtable.abi_version` 是否匹配 → 把 `onEnable`/`onDisable` 接到 `vtable.on_enable`/`on_disable` 上。

Rust 侧（`register_mod!` 宏展开）：宏生成 `levi_rs_main` 导出函数本身，内部把 `LeviMod::on_load/on_enable/on_disable/on_unload` 这几个用户实现的钩子，通过一个 `ModSlot<T>`（本质是 `Mutex<Option<T>>` 静态变量）串起来。这个 slot 被故意标了 `unsafe impl Sync`——因为它**只会在服务器线程上被访问**（所有生命周期钩子调用都来自 C++ 桥接，而桥接保证这些调用发生在服务器线程），所以即使模组状态里持有非 `Send`/`Sync` 的资源（比如 `Listener`）也是安全的，只是这份安全性是靠"调用时机的约定"保证的，不是编译器自动推出来的——这也是为什么[内存安全与生命周期](/advanced/memory-safety)要把线程规则单独讲清楚。

## 为什么没有独立的 `mc::` 全局对象

这是一条具体的设计决策，记在这里作为"分层设计怎么落到安全 Rust API 形状"的例子（决策记录见[设计取舍记录](/advanced/decisions)）。

ABI 层本身只有**一张**函数表和**一个**不透明的 mod 句柄，从没有"多个独立全局单例"这个概念。已经实现的 `Server::execute_command`/`scan_region`/`player_position` 等，全部是同一个零大小 `Server` 类型上的方法。文档里用 `Category::method()` 给一个很大的平坦方法表分组，只是**呈现方式**，不代表底层真的要有一个个独立的全局对象——`mc::get_player(...)` 这种写法，是从 LSE（JS/Lua 脚本引擎，全局函数很自然）照搬过来的，跟 Rust 里"要用什么就要有个明确的持有者/入口"的习惯对不上。

区分规则很简单：

- **需要"当前活着的服务器/世界"作为上下文**（在线玩家、已加载实体、坐标处的方块状态、生成一个新实体）→ 挂在 `Server` 上，跟 `player_position`/`scan_region` 这些已有方法一致。
- **纯粹构造一个值，不需要活跃的服务器上下文**（`Item::new(name, count)` 造一个新物品堆、`Nbt::parse(snbt)` 解析一段文本）→ 该值类型自己的关联函数，就像 `std::fs::File::open(..)` 是 `File` 自己的关联函数，不需要一个全局 `Filesystem` 对象一样。

按这条规则，`Player::get(info)`/`Entity::spawn_mob(...)`/`World::get_block(...)` 背后仍然统一走 `Server`；`Item::new(...)`/`Nbt::parse(...)` 是各自类型的构造函数。两种都不需要、也不会新增一个平行于 `Server` 的全局对象。
