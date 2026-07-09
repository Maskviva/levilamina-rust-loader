# 扩展桥接：新增一个 API 的完整流程

参考页里每一个 🧩 变成 ✅，走的都是同一条路。本页给出这条路的操作手册，并汇总首次编译时最可能遇到的摩擦点（全部按 LeviLamina 头文件逐条核实过，附文件定位以便符号在版本间挪动时快速复查）。

## 前置：构建环境

改桥接本身（C++ 部分）需要模组作者不需要的东西：

- [xmake](https://xmake.io) + Visual Studio 2022（或 clang-cl）工具链——与任何 LeviLamina 原生模组一致。

```shell
xmake f -m release -y
xmake                      # 产物(DLL + manifest)在 bin/
cargo build --workspace    # 顺带构建/测试两个 Rust crate
cargo test --workspace
```

> ⚠️ **构建前把 `xmake.lua` 里 `levilamina`/`bedrockdata`/`prelink` 的版本钉死到服务器实际运行的版本**。不钉版本时,包管理器可能悄悄解析到更新的 SDK,症状是一堆莫名其妙的编译错误(成员不存在、签名变了),而不是一条清晰的版本不匹配提示。

## 四层修改流程

一个新 API 要动四个地方，每层职责单一（分层背景见[架构与 ABI 设计](/advanced/architecture)）：

| 步骤 | 位置 | 做什么 |
| :---: | --- | --- |
| ① 声明契约 | `src/LeviRsAbi.h` | 在 `LeviRsApi` **表尾**追加函数指针，遵守[追加式演进规则](/advanced/abi) |
| ② 实现桥接 | `src/bridge/*.cpp` | 按领域落到对应文件（`Events`/`Commands`/`Server`/`World`/`Players`…），只有 `ApiTable.cpp` 关心字段顺序 |
| ③ 镜像类型 | `crates/levilamina-sys` | 把新签名逐字段翻成 `#[repr(C)]`，无任何逻辑；**与 ① 同一次提交** |
| ④ 安全封装 | `crates/levilamina` | 包出模组作者实际使用的安全接口（`Result`/`Option`、RAII、`catch_unwind`），并更新对应参考页 |

实现 ② 时的通用纪律：

- 回调进 Rust 的每个入口都要考虑 panic 边界（④ 层的 trampoline 统一 `catch_unwind`）；
- 字符串只传 `(ptr, len)` 视图、Rust → C++ 用 sink 回调，不引入任何跨界分配（见 [ABI 契约](/advanced/abi)）；
- 对 LeviLamina 的调用 try/catch 防护，错误经模组日志器上报。

## 原生调用点速查（已对源码核实）

桥接依赖的 LeviLamina/BDS 调用点及其出处，改动或新增时以此为地图：

**模组机制**
- 自定义模组类型合法：`ModManagerRegistry::addManager()` 公开；`loadMod` 按 `manifest.type` 分发；`ModRegistrar` 按依赖拓扑排序（`ll/api/mod/ModManagerRegistry.{h,cpp}`、`ll/core/mod/ModRegistrar.cpp`）。
- 原生入口契约：加载器需导出 `ll_memory_operator_overrided`（含 LL 头文件即自动获得）与 `LL_REGISTER_MOD` 生成的 `ll_mod_*` 符号（`ll/core/mod/NativeModManager.cpp:104`、`ll/api/mod/RegisterHelper.h`）。
- `ll::mod::getModsRoot()` 声明于 `ll/api/mod/Mod.h:14`。

**事件**
- `DynamicListener` 包装 `std::function<void(CompoundTag&)>`，序列化→回调→反序列化往返，故可改写/取消；`Cancellable::serialize` 会输出 cancelled 标志（`ll/api/event/DynamicListener.h`、`Cancellable.h`）。
- `EventBus`：`addListener(ListenerPtr, EventIdView)`、`removeListener`、`events()` 枚举、`hasEvent`（`ll/api/event/EventBus.h`）。
- **`EventPriority` 的真实取值是 0/100/200/300/400**，不是 0..4——桥接把 ABI 的 0..4 显式映射过去（`ll/api/event/ListenerBase.h:14`）。

**NBT / SNBT**
- `Tag::toSnbt(SnbtFormat::Minimize)`；`CompoundTag::fromSnbt(sv) -> ll::Expected<CompoundTag>`，用 `if (auto t = …)` 判定后 move 出来（`mc/deps/nbt/Tag.h:15,83`、`CompoundTag.h:53`）。

**命令**
- 运行时命令：`getOrCreateCommand(name, desc, perm)` → `runtimeOverload().optional("args", ParamKind::RawText).execute(Fn)`，`Fn = void(CommandOrigin const&, CommandOutput&, RuntimeCommand const&)`；参数读取 `rt["args"].hold(ParamKind::RawText)` 再 `.get<ParamKind::RawText>().mText`。枚举下标 11（`RawText`）与 variant 下标 11（`CommandRawText`）对齐（`ll/api/command/runtime/*.h`、`mc/server/commands/CommandRawText.h`）。
- 执行命令：`CommandRegistrar::getServerInstance().executeCommand(sv, origin)`；`ServerCommandOrigin(std::string const&, ServerLevel&, CommandPermissionLevel, DimensionType)`，`DimensionType` 有隐式 `int` 构造所以传 `0` 可行；输出经 `getMessages()[].getMessageId()` + `getSuccessCount()`（`ll/api/command/CommandRegistrar.h:72`、`mc/server/commands/ServerCommandOrigin.h:35`、`CommandOutput.h`）。

**线程与日志**
- `ll::thread::ServerThreadExecutor::getDefault().execute(fn)` / `.executeAfter(fn, Duration)`，`Duration = steady_clock::duration`（`std::chrono::milliseconds` 可隐转）（`ll/api/thread/ServerThreadExecutor.h`）。
- Logger 具备 `fatal/error/warn/info/debug/trace`，桥接用单参字符串重载（`ll/api/io/Logger.h`）。
- 错误工具：`ll::makeStringError`、`ll::makeExceptionError`、`ll::error_utils::printCurrentException(logger)`（`ll/api/Expected.h:107,119`、`ll/api/utils/ErrorUtils.h:45`）。

**世界读取（ABI v3 用到的）**
- Level 句柄：`ll::service::getLevel()`。
- 维度：`level->getDimension(DimensionType{d})` 返回 `WeakRef<Dimension>`；`.lock()` 得 `StackRefResult<Dimension>`（底层是 `std::shared_ptr<Dimension>`，故 `!dim` / `dim.get()` / `dim->` 均可用）。
- 方块读取：`dim->getBlockSourceFromMainChunkSource()` → `BlockSource::getBlock(BlockPos)` → `Block::getTypeName()`；方块状态经 `Block::mSerializationId`（公开的 `TypedStorage` 成员，`.get()` 取出）序列化为 SNBT——**本 LL 版本没有** `Block::getSerializationId()` 访问器（老版本有），若未来恢复两种写法皆可。
- 实体枚举：`level->getRuntimeActorList()`（`std::vector<Actor*>`，每次调用分配一次 vector，故实时扫描要控制频率与范围），按 `Actor::getPosition()` / `getDimensionId()` 过滤，`Actor::save(CompoundTag&)` 序列化、`getTypeName()` 命名。
- 粒子：`Level::spawnParticleEffect(std::string const&, Vec3 const&, Dimension*)`（存在 MolangVariableMap 重载，桥接绑定三参版本）。
- 玩家遍历：`Level::forEachPlayer(std::function<bool(Player&)>)`，返回 `false` 停止迭代。

## 首次构建核对清单

按可能性排序的摩擦点：

1. **头文件根路径**。确认 `#include "mc/..."` / `#include "ll/..."` 能从 xmake 拉取的 `levilamina` 包解析；LL 版本不同头文件可能挪位——上一节的文件定位就是复查地图。
2. **`CommandOutput::success(std::string)`** 应绑定到 `string_view` 重载（另有需 ≥1 参数的变参 fmt 重载）。若你的 LL 版本使其歧义，改写为 `output.success(std::string_view{...})`。
3. **`getModsRoot()` 可见性**：它是 `namespace ll::mod` 下的自由函数，`RustModManager.cpp` 在该命名空间内不带限定地调用；链接报错则改为全限定 `ll::mod::getModsRoot()`。
4. **`runtimeOverload()` 的默认 mod 参数**是 `NativeMod::current()`——在加载器 DLL 内即加载器模组自身。这是有意为之（命令必须比 Rust 模组活得久），读日志时留意即可。
5. **`set_exceptions("none")` 与 `/EHa`**：官方模板用 `/EHa`；若你的 xmake 版本里两者冲突，去掉其一。异常必须开启（桥接用 try/catch）。
6. **Rust cdylib 命名**：`hello-mod` → `hello_mod.dll`，manifest 的 `entry` 必须匹配平台上的实际 cargo 输出名。
7. **`WeakRef::lock()` / `StackRefResult`**：已确认 `StackResultStorage = std::shared_ptr<T>`（`GameRefs.h`）；若你的 LL 版本改了这个别名，相应调整 `dim.get()` / `dim->` 用法。
8. **`forEachPlayer` 返回语义**：假定 `true` 继续、`false` 停止（LL 惯例），版本差异时核实。
9. **玩家名匹配**：`get_player_position` 用查询名与 `Player::getRealName()`（账号真实名，LLNDAPI）比对——这是当前代码的实际行为；若你的环境要按**显示名**匹配（改名牌/前缀插件场景），把那一处比较换成继承自 `Actor` 的 `getNameTag()`。注意本 SDK 的 `Player` 上**不存在** `getName()` 方法，别照旧版资料去找它。

## 版本升级注意

追加新 ABI 能力（只动 `struct_size`）时，旧模组无需重编译；**升 `LEVI_RS_ABI_VERSION` 大版本时，加载器和所有模组都必须重建**——v2 加载器拒绝 v3 模组（反之亦然）是既有 `abi_version`/`struct_size` 检查的预期行为，不是 bug。

## 已知的 v0.1 简化（设计使然）

- `EventRef::cancel()` 是对 SNBT 做 `cancelled:0b → cancelled:1b` 的文本翻转。它能工作是因为 `Cancellable` 恰好序列化这个字段；结构化的 NBT 编辑器是 v0.2 计划（配合 [Nbt](/api/nbt) 层落地）。
- 所有 Rust 命令都是 `/<name> [raw text]`；类型化参数在路线图上（它们能干净地映射到更多 runtime overload，目标设计见 [Command 参考](/api/command)）。
- 尚无直接的世界/实体指针；`execute_command` 覆盖了大部分需求（这也是[设计取舍记录](/advanced/decisions)第 3 条选择命令行落点的原因之一）。
