# ABI 契约与演进

本页是加载器与模组之间那份 C ABI 契约的完整规则书：为什么用函数表、字符串与内存怎么跨界、线程与 panic 的约定、以及版本怎么演进。契约本身的
**唯一真相来源**是 `src/LeviRsAbi.h`，由 `crates/levilamina-sys` 逐字段镜像——两者永远在同一次提交中修改。

> 分层全景（模组作者视角）见[架构与 ABI 设计](/advanced/architecture)；本页聚焦契约细节，适合要给桥接加能力、或要排查跨界问题的读者。

## 为什么是"加载器模组 + 自定义模组类型"

LeviLamina 的 `ModManagerRegistry::addManager()` 是公开 LLAPI（`ll/api/mod/ModManagerRegistry.h`，已核实），且模组加载流程在
**分发时**才按 manifest 的 `type` 解析对应管理器（轮到某个模组加载时才查 `hasManager(type)`），加载顺序则按 `dependencies`
拓扑排序——这一分发行为由 LeviLamina 内部的加载器实现（`ModRegistrar`，不在随 SDK
发布的公开头文件中，无法直接从头文件核证，但可由本加载器的实际工作方式观察验证）。于是：

1. `levilamina-rust-loader` 自身是一个普通原生模组，它的 `ll_mod_load` 为 `"rust"` 类型注册一个 `RustModManager`；
2. 每个 Rust 模组声明 `"dependencies": [{"name": "levilamina-rust-loader"}]`，保证同一轮加载中加载器先行。

这让 Rust 模组成为**一等公民**：依赖排序、启用/禁用、卸载、模组列表全部走标准机制——脚本引擎模组用的正是同一套。

## 函数表注入，而不是导入库

加载器在 Rust cdylib 里只解析**一个**符号：

```c
levi_rs_main(const LeviRsApi*, LeviRsModHandle, LeviRsModVTable*)
```

调用它，交出一张函数指针表（`LeviRsApi`），并接收模组回填的 vtable（生命周期钩子）。选这条路而不是导入库链接的理由：

- rustc（MSVC ABI）与 clang-cl 产物之间**零链接耦合**，未来在 Linux 上原样可用；
- 表指针自带版本（`abi_version` + `struct_size`），加载器/模组不匹配时**快速失败并报清晰错误**，而不是未定义行为；
- Rust 模组从不直接调用 C++ 符号、从不持有 C++ 堆内存，LeviLamina 的统一分配器契约（`ll_memory_operator_overrided`
  ）由加载器一方满足即可——Rust 在自己这一侧用自己的分配器。

## 字符串与内存约定

- 所有字符串都是 UTF-8 的 `(ptr, len)` 视图（`LeviRsStr`），**不要求 NUL 结尾**。
- 传入回调的字符串**只在当次调用期间有效**，需要保留必须拷贝。
- Rust → C++ 方向的数据用 **sink 回调**（`LeviRsStrSink`、`LeviRsCmdOutputSink`），在调用帧内被同步调用。**任何方向都没有跨边界的内存分配
  **——ABI 里刻意没有 `free` 函数。
- `LeviRsStr` 复用 `std::string_view` 的 `{ptr, len}` 布局，但该布局并非标准保证。应对是编译期 `static_assert` + 启动时
  `leviRsVerifyStrLayout()` 运行时探测，任一失败即拒绝加载（详见[架构与 ABI 设计](/advanced/architecture)）。

## 线程契约

- 所有回调（生命周期、事件、命令、调度任务）都在**服务器线程**上被调用。
- `log`、`gaming_status`、`schedule`、`schedule_after` 线程安全；后两者基于 `ll::thread::ServerThreadExecutor`
  实现，是后台线程（Tokio 运行时、agent、I/O）重返游戏的指定通道。其余一切仅限服务器线程。
- 安全层把这份契约编码进 API 形态：`subscribe_event` 等预期在钩子里调用（已在服务器线程）；`Server::get()` + `schedule`
  是文档化的跨线程模式。

## Panic 与错误策略

- **Rust 侧**：`levilamina` crate 生成的每个 `extern "C"` 入口都包 `catch_unwind`；panic 记入模组日志并转为 `false`
  /no-op。Rust 的 unwind 绝不穿越 FFI 进入 C++。
- **C++ 侧**：`register_command` 等对 LeviLamina 的调用用 try/catch 防护并经模组日志器上报；加载/卸载路径返回
  `ll::Expected<>`，错误进入标准的模组加载诊断输出。

## 版本控制：两个数字

| 字段            | 语义                  | 检查规则                                                                                                                                                               |
|---------------|---------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `abi_version` | 大版本                 | **区间**判断，不是完全相等。加载器接受大版本在 `[LEVI_RS_ABI_MIN_SUPPORTED, 自身版本]` 之间的模组（新加载器能跑旧模组——追加式演进保证旧模组只调用当前表的字节相同前缀）；模组接受大版本 **≥ 自身**的加载器。太新的模组（版本高于加载器）和低于下限的模组各自被拒，并给出方向明确的提示 |
| `struct_size` | `sizeof(LeviRsApi)` | 前向兼容检查：若模组期望的表比加载器实际提供的**更大**，拒绝加载（否则模组会读到表尾之外的内存）。这是与版本号解耦的**精确闸门**——无论大版本怎么变，只要表实际字节数够，就不会越界                                                                     |

`LEVI_RS_ABI_MIN_SUPPORTED`（当前 `1`）是"低于此版本的表不再是我的前缀，拒绝"的下限旋钮。因为迄今每次变更都是追加式，所以下限是
1（全部向下兼容）；将来若某个大版本做了**非追加**的破坏性改动，把它抬到那个版本即可。

真实检查代码（`levilamina` crate 的 `__init_runtime`）：

```rust
// 加载器必须不比自己旧：更新的加载器暴露的是当前表的追加式超集
if api.abi_version < sys::LEVI_RS_ABI_VERSION {
return false;
}
// 与版本号如何递增无关的精确闸门：加载器的实际表不能比本 crate
// 编译时的 LeviRsApi 还小，否则访问表尾字段会读越界
if (api.struct_size as usize) < core::mem::size_of::<sys::LeviRsApi>() {
return false;
}
```

> **为什么是区间而不是完全相等**：历史上部分追加式变更也顺带升过 `abi_version`（v2、v3…），所以一个按 v4 编译的模组配 v5
> 加载器时，两边版本号并不相等，但 v5 的表是 v4 的严格超集，完全兼容。若坚持"完全一致"，这种本可正常工作的组合会被无谓拒绝——这正是加载器一侧从
`!=` 放宽到区间判断要解决的问题。反方向（旧加载器 + 新模组）由上面的 `struct_size` 闸门兜住。

## 演进规则（v1 → v2 → …）

1. `LeviRsApi` 与 `LeviRsModVTable` **只追加**：新函数指针永远加在表尾；已有字段从不重排、删除、改类型。
2. 追加式变更**不**升 `abi_version`，只体现在 `struct_size`；使用表尾新字段前必须检查 `struct_size`。（早期版本曾对追加也升版本号，现行策略是保持
   `abi_version` 不变、只增长 `struct_size`——两种历史都被上面的区间判断正确接纳。）
3. 破坏性变更（语义、签名、字段重排/删除）升 `LEVI_RS_ABI_VERSION`，并把 `LEVI_RS_ABI_MIN_SUPPORTED` 抬到该版本，从而拒绝布局不再兼容的旧表。
4. C 头文件与 `-sys` crate **永远在同一次提交中**同步修改。

> 为什么重排字段是不可接受的：函数指针表一旦重排，任何按旧偏移访问的调用方读到的都是**错误的函数指针**
> ——这是直接的内存安全问题，不是"版本不匹配报个错"那么轻。见[设计取舍记录](/advanced/decisions)第 2 条。

## 版本演进史

|           ABI           | 新增能力                                                                                                                                                           |
|:-----------------------:|----------------------------------------------------------------------------------------------------------------------------------------------------------------|
|           v1            | 日志、调度、事件订阅、命令执行/注册                                                                                                                                             |
|           v2            | 服务器状态（tick、TPS、在线人数、模拟是否暂停）                                                                                                                                    |
|           v3            | 世界读取（生成粒子、查玩家坐标、区域扫描）                                                                                                                                          |
|           v5            | 玩家管理（列表/消息/踢出/生命值/游戏模式/传送）+ 世界写入（读写单方块）+ 世界时钟（时间/天气）                                                                                                           |
| v5 追加（`struct_size` 门控） | 单连接发包：`send_packet`（通用原语——任意 `MinecraftPacketIds` + 当前版本线格式包体，反序列化后只发给一个玩家）与 `spawn_particle_for`（其上的类型化派生：按玩家单发粒子包，不广播）                                       |
| v5 追加（`struct_size` 门控） | 经济接口：`get`/`set`/`add`/`reduce`/`transfer`/`history`/`ranking` 及前置/后置事件监听，桥接到**可选**的 LegacyMoney 插件（见 [Money](/api/money)）。10 个表尾槽 `get_money`…`money_ranking` |

具体条目见仓库 `CHANGELOG.md`。

## v0.1 刻意不做的事

- **游戏对象的直接指针访问**（`Actor*` / `Player*` / `BlockSource*`）：需要一套句柄生命周期方案，规划为不透明的、带代际校验的
  id（当前的"标识符句柄"是它的先导，见[内存安全与生命周期](/advanced/memory-safety)）。
- **从 Rust hook 任意 BDS 函数**：原理上可经 preloader 的符号解析做到，但绕过了 LeviLamina 的抽象层，不属于本项目职责。
- **客户端平台**。