# 日志与调度

## 日志

```rust
let logger = ctx.logger();          // 或者任何地方 Logger::get()

logger.info("模组已启用");
logger.warn("配置项缺失，用默认值");
logger.error("数据库打不开");
logger.debug("详细状态：…");
```

六档：`fatal` `error` `warn` `info` `debug` `trace`。

会自动带模组名前缀，不用自己加。**线程安全**，后台线程可以直接打。

::: warning 高频回调里不要无条件打日志
`HopperTransferEvent`、移动类事件、包拦截器一秒能触发几千次。日志 I/O 会直接拖垮 tick。要打就加采样或节流。
:::

## 调度

```rust
use std::time::Duration;

// 尽快在游戏线程执行
ctx.server().schedule(|| {
    Player::broadcast("下一刻见");
});

// 延迟
let id = ctx.server().schedule_after(Duration::from_secs(5), || {
    Player::broadcast("五秒到了");
});

// 取消
ctx.server().cancel_task(id);
```

`schedule` / `schedule_after` 是**线程安全**的，这是它们存在的主要理由。

## 后台线程回到游戏

这是整个 SDK 里最重要的一个模式。

```rust
use std::thread;

thread::spawn(move || {
    // ✅ 后台线程，随便慢：网络请求、大文件、复杂计算
    let data = 很慢的活();

    // ✅ 回到游戏线程再碰世界
    Server::get().schedule(move || {
        Player::by_name("Steve").send_message(&data).ok();
    });
});
```

::: danger 后台线程能碰的只有这些
- `Server::schedule` / `schedule_after` / `gaming_status`
- `KvDb` 全部方法
- `system::*` 全部
- `Logger` 全部

**其余一切**在游戏线程之外调用都是未定义行为。可能崩，也可能更糟——静默的数据损坏。
:::

配合 Tokio 也是同一个模式：在 runtime 里跑异步，结果出来 `schedule` 回去。

## 循环任务

没有内置的"重复任务"，自己重排：

```rust
fn tick() {
    // 干活

    Server::get().schedule_after(Duration::from_secs(1), tick);
}

// on_enable 里启动
ctx.server().schedule_after(Duration::from_secs(1), tick);
```

好处是想停就不再排，不用管句柄。

::: warning 确认每次只排一个
自我重排的任务如果某条分支排了两次，就是指数增长。模组卸载时加载器会把没跑的任务丢掉，所以不会泄漏，但跑起来的服务器已经卡死了。
:::

## TaskId

```rust
let id = ctx.server().schedule_after(d, f);
if !id.is_valid() {
    // 排队失败（模组正在卸载）
}
```

id 在进程内单调递增、**永不复用**。过期的 `TaskId` 取消不了任何东西，不会误伤凑巧用了同一个数字的无关任务。

## 任务归属

任务归注册它的模组。模组卸载时还没跑的任务会被丢掉——不会在半个模组已经消失的情况下执行。

`on_disable` 里通常不需要手动取消任务，加载器会处理。需要手动取消的是那些**跨 tick 持有外部资源**的任务。

## 定时保存的常见写法

```rust
struct MyMod { dirty: Arc<AtomicBool> }

// on_enable
let dirty = self.dirty.clone();
fn autosave(dirty: Arc<AtomicBool>) {
    if dirty.swap(false, Ordering::Relaxed) {
        // 存盘
    }
    let d = dirty.clone();
    Server::get().schedule_after(Duration::from_secs(60), move || autosave(d));
}
```

比"每次改动都存盘"省事，也比"只在 `on_disable` 存"安全——服务器崩了 `on_disable` 是不跑的。
