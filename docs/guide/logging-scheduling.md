# 日志与调度

这两组 API 是全 API 面里仅有的**线程安全**成员（外加 `Server::gaming_status`），也因此是后台线程与游戏世界之间的桥梁。

## 日志

日志走模组自己的 LeviLamina 日志器，输出自动带模组名前缀。从 `ModContext` 拿到 `Logger` 后可以随意 clone、move 进闭包、跨线程使用：

```rust
let logger = ctx.logger();

logger.info("服务已就绪");
logger.warn("配置缺少字段, 使用默认值");
logger.error("数据库连接失败");
logger.debug("tick 详情…");
logger.trace("最啰嗦的级别");
logger.log(LogLevel::Fatal, "显式指定级别");
```

级别：`Fatal` / `Error` / `Warn` / `Info` / `Debug` / `Trace`。Debug/Trace 是否显示取决于服务器的 LeviLamina 日志配置。

## 调度：把工作投递回服务器线程

```rust
let server = Server::get();

// 尽快在服务器线程上执行
server.schedule(|| {
    // 这里可以调用任何 API
});

// 延迟执行
server.schedule_after(Duration::from_secs(5), || {
    let _ = Server::get().execute_command("say 5 秒到了");
});
```

两个方法都线程安全，闭包**总是**在服务器线程上执行。

### 周期任务

没有专门的"每 N 秒"接口，用 `schedule_after` 自我续期即可（`region-scan` 示例的动画循环就是这个模式）：

```rust
fn tick_loop() {
    Server::get().schedule_after(Duration::from_millis(500), || {
        // …做事…
        tick_loop(); // 续期下一轮
    });
}
```

需要可停止的循环时，配一个"代际计数"：启动时记下当前代，每轮开头核对，代不匹配就直接返回不再续期。

## 后台线程模式（Tokio / HTTP / AI agent）

铁律回顾：**回调都在服务器线程；除日志/调度/`gaming_status` 外一切 API 只能在服务器线程调用**（详见[核心概念](/guide/concepts)）。

于是"后台干重活、结果回游戏"的标准形状是：

```rust
fn on_enable(&mut self, ctx: &ModContext) -> Result<()> {
    let logger = ctx.logger();

    std::thread::spawn(move || {
        // ① 后台线程:自由使用 reqwest/tokio/std::fs… 与游戏无关的一切
        let result = do_heavy_work();
        logger.info("后台任务完成");            // 日志线程安全,直接用

        // ② 结果要影响游戏 → 投递回服务器线程
        Server::get().schedule(move || {
            let _ = Server::get()
                .execute_command(&format!("say 任务结果: {result}"));
        });
    });

    Ok(())
}
```

反过来也一样：事件回调里不要做阻塞 I/O（会卡住整个服务器 tick），把慢活丢给后台线程/`tokio::spawn`，完成后再 `schedule` 回来。

## 文件、网络、进程：直接用 Rust 生态

配置文件、HTTP 请求、SQLite、子进程……这些与游戏引擎无关的需求**不经过桥接**，在模组的 `Cargo.toml` 里加常规 crate（`serde_json`、`reqwest`、`rusqlite`、`std::fs`、`std::process` ……）正常写 Rust 即可。唯一的纪律仍然是上面那条：结果要碰游戏时走 `schedule`。详见 [System](/api/system) 与 [Data](/api/data) 参考页的说明。
