# Money — 经济（LegacyMoney 桥接）

> 状态：✅（桥接到**可选**的 LegacyMoney 插件）。
>
> **接口来源**：LegacyMoney（LLMoney）的 `extern "C"` 导出 `LLMoney_*`。加载器把它们封装成 `levilamina::money`
> 下的一组自由函数，命名与语义对标 LSE 的 `money` API。经济不是 Minecraft 原生概念，这一页对应的是社区插件 LegacyMoney。

## 可选依赖：没装也不会崩

LegacyMoney 是**延迟加载**的（`/DELAYLOAD`），所以：

- 没安装 / 没启用 LegacyMoney 时，加载器**照常启动**，不会因为缺 DLL 而拒绝加载所有 Rust 模组；
- 每个 `money::*` 调用在后端缺失时走**空转分支**：读返回 `0`、写返回 `Err`、监听注册为 no-op；
- **首次**发现后端缺失时，服务端控制台打**一次**警告：“请检查是否安装并启用了 LegacyMoney”。

加载器对后端做**双重校验**才会真正下发调用：① 模组列表里存在**已启用**的 `LegacyMoney`；② 能解析到导出符号 `LLMoney_Get`
。任一不满足即视为不可用。

> 因此：在没有 LegacyMoney 的服务器上，`money::set(...)` 返回 `Err` 是**预期行为，不是 bug**。要不要硬依赖 LegacyMoney
> 由你的模组决定——若不想依赖，可改用 [`KvDb`](/api/data#kvdb-键值数据库) 自建余额表。

## 读 / 写

| API                                                     | 作用                        | 后端缺失时  | 原生对应             |
|---------------------------------------------------------|---------------------------|--------|------------------|
| `money::get(xuid) -> i64`                               | 查询余额（账户不存在为 `0`）          | 返回 `0` | `LLMoney_Get`    |
| `money::set(xuid, amount) -> Result<()>`                | 覆盖设置余额                    | `Err`  | `LLMoney_Set`    |
| `money::add(xuid, delta) -> Result<()>`                 | 增加余额（`delta` 不可为负）        | `Err`  | `LLMoney_Add`    |
| `money::reduce(xuid, delta) -> Result<()>`              | 扣减余额（不足则失败）               | `Err`  | `LLMoney_Reduce` |
| `money::transfer(from, to, amount, note) -> Result<()>` | 转账，`note` 记入流水（无备注传 `""`） | `Err`  | `LLMoney_Trans`  |

> `xuid` 是玩家的 XUID 字符串。金额单位是 LegacyMoney 的最小单位（整数），本层不做小数换算。

## 历史 / 排名

| API                                                     | 作用                                          | 后端缺失时   | 原生对应                   |
|---------------------------------------------------------|---------------------------------------------|---------|------------------------|
| `money::history(xuid, within: Duration) -> String`      | 取该玩家在 `within` 时间窗内的流水（LegacyMoney 的原始序列化串） | 空串      | `LLMoney_GetHist`      |
| `money::clear_history_older_than(older_than: Duration)` | 清除早于 `older_than` 的流水                       | 无操作     | `LLMoney_ClearHist`    |
| `money::clear_all_history()`                            | 清除全部流水                                      | 无操作     | `LLMoney_ClearHist(0)` |
| `money::ranking(top_n: u16) -> Vec<(String, i64)>`      | 余额排行榜，由高到低的 `(xuid, 余额)`                    | 空 `Vec` | `LLMoney_Ranking`      |

> `history` 返回的是插件自己的格式串，本层不解析；`ranking` 会把 `xuid:余额` 逐行解析成元组，解析不出来的行静默丢弃。
`within` / `older_than` 以秒计并在传给插件前对 `i32::MAX` 做钳制，避免超大 `Duration` 溢出成负数。

## 事件监听（交易前 / 后）

LegacyMoney 全进程各只有**一个** before 槽和一个 after 槽，所以这两个接口返回 RAII 守卫，其 `Drop` 会清空对应槽；再次注册*
*替换**前一个。想让回调贯穿整个模组生命周期就调 `.forget()`。

| API                                              | 作用                                  | 原生对应                        |
|--------------------------------------------------|-------------------------------------|-----------------------------|
| `money::on_before(f) -> BeforeGuard`             | 每次变更**前**触发；返回 `true` 放行、`false` 取消 | `LLMoney_ListenBeforeEvent` |
| `money::on_after(f) -> AfterGuard`               | 每次变更**后**触发；返回值被忽略                  | `LLMoney_ListenAfterEvent`  |
| `BeforeGuard::forget()` / `AfterGuard::forget()` | 让回调常驻，不随守卫析构而注销                     | —                           |

回调收到 `&MoneyEvent`：`kind`（`MoneyEventKind`，即 `Set`/`Add`/`Reduce`/`Trans`）、`from`、`to`、`amount`。`from`/`to`
借用自调用帧，需要留存请自行 `to_owned()`。回调里 panic 会被捕获并记日志——`on_before` 里 panic 视为放行（不因一个坏回调卡死整个经济系统），
`on_after` 里 panic 静默吞掉。在回调里再调 `money::*` 是安全的（内部先取出回调再执行，嵌套调用不会死锁）。

```rust
use levilamina::money;

// 读 + 写
let bal = money::get("2535400000000000");
money::add("2535400000000000", 100) ?;

// 拦截大额扣款（比如实现单笔上限）
let g = money::on_before( | ev| {
use money::MoneyEventKind::Reduce;
! (matches! (ev.kind, Reduce) & & ev.amount > 10_000) // 超过上限则取消
});
// 作用域结束 g 析构 → 自动注销；想常驻就 g.forget()
```

## 线程

与 LegacyMoney 一致：注册回调与增删余额都在**服务器线程**，回调也在服务器线程触发。