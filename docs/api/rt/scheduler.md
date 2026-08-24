# Scheduler — 调度

把工作排到游戏线程上执行。**这是后台线程回到游戏世界的唯一通道。**

```rust
use std::time::Duration;
use levilamina::prelude::*;

// 下一 tick 执行
ctx.server().schedule(|| {
    Player::broadcast("下一刻见");
});

// 延迟执行
let id = ctx.server().schedule_after(Duration::from_secs(5), || {
    Player::broadcast("五秒到了");
});
```

| API | 返回 | 说明 |
| --- | --- | --- |
| `s.schedule(f)` | `TaskId` | 尽快在游戏线程执行。**线程安全** |
| `s.schedule_after(delay, f)` | `TaskId` | 延迟执行。**线程安全** |
| `s.cancel_task(id)` | `bool` | 取消还没跑的任务 |
| `s.pending_tasks()` | `u32` | 本模组还排着几个 |

闭包是 `FnOnce() + Send + 'static`。

## TaskId

```rust
let id = ctx.server().schedule_after(d, f);
if id.is_valid() {
    // 排上了
}
```

`TaskId::NONE` 是排队失败时返回的值。id 在进程内单调递增、**永不复用**——过期的 `TaskId` 取消不了任何东西，不会误伤某个凑巧用了同一个数字的无关任务。

## 后台线程怎么回来

```rust
use std::thread;

thread::spawn(move || {
    let data = 很慢的网络请求();          // 后台线程，随便慢

    Server::get().schedule(move || {      // 回到游戏线程
        Player::by_name("Steve").send_message(&data).ok();
    });
});
```

::: danger 后台线程里不能碰世界
除了 `schedule` / `schedule_after` / `gaming_status`、`KvDb`、`system::*` 和 `Logger`，这个 SDK 的任何东西都**必须**在游戏线程调用。在别处调用是未定义行为，表现可能是崩溃，也可能是更糟的——数据静默损坏。
:::

## 没有「重复任务」

只有一次性任务。要循环就在任务里重新排一个：

```rust
fn tick_loop() {
    // 干活

    Server::get().schedule_after(Duration::from_secs(1), tick_loop);
}
```

这样写有个好处：想停下来只要不再排就行，不用管句柄。

::: tip 别在循环里忘了退出条件
自我重排的任务在模组卸载时会被加载器丢掉，所以不会泄漏。但如果它每次都排两个，那就是指数增长——排完记得确认只排了一个。
:::

## 任务归属

任务归注册它的模组所有。模组卸载时，还没跑的任务会被丢掉，不会在半个模组已经消失的情况下执行。

## 客户端

客户端构建里这套 API 在 `Client` 上，语义相同：`ctx.client().schedule(...)`。见 [Client](/api/rt/client)。
