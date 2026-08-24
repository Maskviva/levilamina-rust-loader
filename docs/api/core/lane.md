# Lane — Rust 高速公路

两个 **Rust** mod 之间的直接函数表调用。「跨语言」用 [Service](/api/core/service)；「两边都是同一次工具链编出来的 Rust」才用这个。

```rust
use levilamina::lane;

// 提供方
static TABLE: PermTableV1 = PermTableV1 { check: ffi_check, check_many: ffi_check_many };
lane::publish::<PermLane, _>(&TABLE, state.clone())?.forget();

// 消费方
match lane::acquire::<PermLane>() {
    Ok(l)  => { /* 快车道 */ }
    Err(e) => { logger.warn(&e.advice()); /* 降级走 service */ }
}
```

## 和 Service 的分工

| | [Service](/api/core/service) | Lane |
| --- | --- | --- |
| 对面是谁 | 任何语言 | 只能是 Rust |
| 一次调用 | 序列化 + 解析 | 一次原子读 + 一次间接调用 |
| 类型 | 全在字符串里 | 真的类型：enum、借用切片、泛型入口 |
| 对不上时 | —— | **自动降级回 Service** |
| 默认选哪个 | **这个** | 有实测瓶颈时 |

## 收益不在「快」

一次 service 往返 1–3 µs。命令门禁那个频率下不是瓶颈。真收益是两件别的事：

- **批量。**「这个玩家对这 40 个节点分别有没有权限」在 JSON 上是 40 次往返或一个 40 字段的载荷；这里是一次调用 + 一个 `&[LaneStr]`。
- **类型。** `Result<Verdict, Denial>` 原样过去。JSON 通道上字段名从 `allowed` 打成 `allow`，`unwrap_or(false)` 的表现是**「这个人没权限」**——一个永远不会被当成 bug 报上来的现象。

## 指纹：为什么不是版本号

Rust 没有稳定 ABI。同一份契约 crate 被两个 cdylib 各编一遍，`-C metadata` 不同，`repr(Rust)` 的字段顺序**可能**不同——那不是崩溃，是静默的内存错乱。

所以比的不是版本号，是**指纹**：

| 来源 | 算进去的东西 |
| --- | --- |
| `build.rs` | rustc 完整版本串（含 commit hash）、target、profile、opt-level |
| `lane.rs` | 车道名、契约版本、`size_of` / `align_of` / `TypeId` of 函数表 |

任何一项不同 → 指纹不同 → `acquire` 返回 `Fingerprint`，**一个指针都不递出去**。

::: tip 失败模式是「慢」，不是 UB
这是这条车道敢存在的全部理由。指纹对不上就走 JSON，功能一条不少。
:::

## 三条铁律

### 1. 内存谁分配谁释放

跨界的字符串和切片只能是**借用**（`LaneStr` / `LaneSlice`），列表走 sink 回调（`collect_strings`）。两个 cdylib 在 Windows 上可能挂在不同的堆上，一边 `String` 一边 `drop` 是 UB。

### 2. 不许 unwind 跨界

每个表项入口套 `lane::guard(兜底值, || …)`。unwind 跨 FFI 是 UB，崩的是整台服务器。

### 3. 每次调用前检查存活

提供方卸载后它的**代码段被 unmap**——函数指针还在，但指向未映射内存。`Lane::with` 替你检查：

```rust
let v = lane.with(|t, d| unsafe { (t.check)(d, LaneStr::new(who), LaneStr::new(node)) });
match v {
    Some(v) => v,
    None    => self.check_via_service(who, node),   // 提供方走了，降级
}
```

存活标志是 **loader 堆上一格永不释放的内存**，所以提供方走了之后读它仍然合法——那正是它存在的理由。热路径上 loader 一行代码都不跑。

## 关于「泛型」

真泛型跨不了界：单态化发生在**实例化的那个 crate 里**，提供方的 dylib 里根本没有 `check::<YourType>` 那份代码。

能做到的是 API **看起来**是泛型的：

```rust
// SDK 里（消费方的 crate 单态化它）
pub fn check_all<'a, I: IntoIterator<Item = &'a str>>(&self, who: &Subject, nodes: I) -> Vec<Verdict>
```

擦除落在 `#[repr(C)]` 函数表上。这已经是绝大多数人想要「泛型跨界」时真正想要的东西。

## API

| API | 说明 |
| --- | --- |
| `lane::publish::<C, S>(&'static table, Arc<state>)` | 发布，返回 `Result<Publication>` |
| `lane::acquire::<C>()` | 取，返回 `Result<Lane<C>, LaneError>` |
| `lane.with(\|table, data\| …) -> Option<R>` | **唯一的调用入口**，`None` = 提供方走了 |
| `lane.is_alive()` / `lane.fingerprint()` | 诊断 |
| `lane::fingerprint::<C>()` | 本地指纹 |
| `lane::guard(兜底, \|\| …)` | 表项入口护栏 |
| `lane::collect_strings(…)` | sink → `Vec<String>` |
| `lane::list_json()` | 全部车道，原始 JSON |
| `pubn.forget()` | 活到 mod 卸载 |

## 老 loader 会怎样

这五个 ABI 槽位是**追加**的，`abi_version` 没动（`docs/DESIGN.md` §8 第 2 条）。用这个 crate 编出来的 mod 在更老的 loader 上会**直接拒绝加载**（`__init_runtime` 比 `struct_size`）——不是「车道不可用」，是整个 mod 起不来。

这是对的：一个会读那几格的 mod，在没有那几格的 loader 上只能读到越界内存。
