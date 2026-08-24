# Log — 日志

```rust
use levilamina::prelude::*;

let logger = ctx.logger();
logger.info("模组已启用");
logger.warn("配置项 max_plots 缺失，用默认值 10");
logger.error("数据库打不开");
```

| API | 级别 |
| --- | :---: |
| `logger.fatal(msg)` — 通过 `log(LogLevel::Fatal, msg)` | 0 |
| `logger.error(msg)` | 1 |
| `logger.warn(msg)` | 2 |
| `logger.info(msg)` | 3 |
| `logger.debug(msg)` | 4 |
| `logger.trace(msg)` | 5 |
| `logger.log(level, msg)` | 指定级别 |

`Logger::get()` 可以在任何地方拿到，不必从 `ctx` 传。

## 线程安全

`Logger` 是线程安全的，后台线程里可以直接打日志。这和 `Server` 的绝大多数方法不同。

## 前缀

日志会自动带上模组名前缀，不用自己加。

```rust
logger.info("hello");
// [my-mod] hello
```

## 格式化

`&str` 参数，用 `format!`：

```rust
logger.info(&format!("加载了 {} 块地皮，耗时 {:?}", n, elapsed));
```

## 什么该打什么不该打

| 场景 | 级别 |
| --- | --- |
| 启动完成、关键状态变化 | `info` |
| 配置有问题但能继续跑 | `warn` |
| 操作失败、需要人来看 | `error` |
| 开发期调试 | `debug` / `trace` |

::: warning 别在事件回调里无条件打日志
高频事件（`HopperTransferEvent`、移动类事件、包拦截）一秒钟能触发几千次。日志 I/O 会直接拖垮 tick。要打就加采样或者节流。
:::
