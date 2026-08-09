# Money — 经济

对接 **LegacyMoney**（LLMoney）插件。这是一个**可选依赖**。

```rust
use levilamina::money;

let bal = money::get("2535400000000000");
money::add("2535400000000000", 100)?;
money::transfer("xuid_a", "xuid_b", 50, "买了个东西")?;
```

## 没装 LegacyMoney 会怎样

优雅降级，不会崩：

- 读操作返回 `0` / 空字符串
- 写操作返回 `Err`
- 注册监听是静默空操作

第一次有 money 调用发现后端不在时，加载器会在控制台打**一次**警告，提示服主装 / 启用 `LegacyMoney`。

**所以这里的 `Err` 是预期内的**，不是 bug。如果你的模组把经济当硬需求，记得在 `manifest.json` 的 `dependencies` 里加 `{ "name": "LegacyMoney" }`，让 LeviLamina 保证加载顺序。

## 操作

| API | 返回 | 说明 |
| --- | --- | --- |
| `money::get(xuid)` | `i64` | 余额 |
| `money::set(xuid, amount)` | `Result<()>` | 设为某值 |
| `money::add(xuid, delta)` | `Result<()>` | 加 |
| `money::reduce(xuid, delta)` | `Result<()>` | 减 |
| `money::transfer(from, to, amount, note)` | `Result<()>` | 转账 |
| `money::ranking(top_n)` | `Vec<(String, i64)>` | 排行榜 |

**身份是 XUID**，不是玩家名。名字会变。

## 流水

| API | 说明 |
| --- | --- |
| `money::history(xuid, within)` | 一段时间内的流水，返回 `String` |
| `money::clear_history_older_than(older_than)` | 清理旧流水 |
| `money::clear_all_history()` | 全清 |

`within` / `older_than` 是 `std::time::Duration`。

## 监听变动

```rust
use levilamina::money;

// 变动前，返回 false 拒绝这次操作
money::on_before(|ev| {
    println!("{:?} {} -> {} : {}", ev.kind, ev.from, ev.to, ev.amount);
    true          // 放行
}).forget();

// 变动后
money::on_after(|ev| {
    println!("成交 {}", ev.amount);
}).forget();
```

```rust
pub struct MoneyEvent<'a> {
    pub kind: MoneyEventKind,
    pub from: &'a str,
    pub to: &'a str,
    pub amount: i64,
}
```

字符串是**借用调用帧的**——要留到回调之外就 `to_owned()`。

::: danger 整个进程只有一个 before 槽和一个 after 槽
LLMoney 全局只保存**一个** before 回调和**一个** after 回调，不是每个监听者一个。注册第二个同类回调会**覆盖掉第一个**——包括别的模组注册的那个。

这就是为什么这两个函数返回的是一个普通的 guard（`Drop` 时清空槽位）而不是每监听者一个的句柄。多个模组都想监听经济变动的话，得自己商量出一个协调机制，或者走 [Bus](/api/bus)。
:::

## 线程

LLMoney 跑在服务器线程上，读写和注册都在那里做。回调也总是在服务器线程触发。
