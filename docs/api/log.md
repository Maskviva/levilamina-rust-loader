# Log — 日志

> 状态：✅ 已支持。

通过模组自身的 LeviLamina 日志器输出。**线程安全**，可从任意线程调用。

| API | 作用 |
| --- | --- |
| `Log::info(msg)` | 以 Info 级别输出 |
| `Log::warn(msg)` | 以 Warn 级别输出 |
| `Log::error(msg)` | 以 Error 级别输出 |
| `Log::debug(msg)` | 以 Debug 级别输出 |
| `Log::trace(msg)` | 以 Trace 级别输出 |
| `Log::fatal(msg)` | 以 Fatal 级别输出 |
| `Log::log(level, msg)` | 以显式的 `LogLevel` 输出 |

## 相关类型

| 类型 | 说明 |
| --- | --- |
| `LogLevel` | 日志级别枚举：`Fatal` / `Error` / `Warn` / `Info` / `Debug` / `Trace` |
