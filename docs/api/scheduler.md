# Scheduler — 调度

> 状态：✅ 已支持。

把任务投递回**服务器线程**执行。这是后台线程（Tokio 任务、AI agent 等）影响游戏世界的唯一入口。**线程安全**。

| API | 作用 |
| --- | --- |
| `Scheduler::run(f)` | 尽快在服务器线程上执行闭包 `f` |
| `Scheduler::run_after(delay, f)` | 延迟 `delay` 后在服务器线程上执行闭包 `f` |

## 相关类型

| 类型 | 说明 |
| --- | --- |
| `Duration` | 标准库 `std::time::Duration`，用于 `run_after` 的延迟 |
