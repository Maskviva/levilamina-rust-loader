# System — 文件 / 网络 / 进程 / 系统信息

> 状态：✅ 已支持（系统信息与环境变量）。文件 / 网络 / 进程刻意不桥接，直接用 Rust 标准库与生态。
>
> **接口来源说明**：这四块里只有"系统信息"对应 LeviLamina 真实提供的工具函数；文件、网络、进程都**没有**对应的原生/LeviLamina 封装——这些本来就是操作系统层面的通用需求，Rust 标准库和生态已经解决得很好，不需要、也不应该再造一层桥接。

## SystemInfo — 系统信息

> 唯一真正原生的一块。**接口来源**：`ll::utils::sys_utils`（`ll/api/utils/SystemUtils.h`）。

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `SystemInfo::os_name()` | 操作系统名称 | `sys_utils::getSystemName` |
| `SystemInfo::os_version()` | 操作系统版本 | `sys_utils::getSystemVersion` |
| `SystemInfo::locale()` | 系统语言/区域代码 | `sys_utils::getSystemLocaleCode` |
| `SystemInfo::local_time()` | 本地时间（含毫秒） | `sys_utils::getLocalTime` |
| `SystemInfo::env(name)` | 读取环境变量 | `sys_utils::getEnvironmentVariable` |
| `SystemInfo::set_env(name, value)` | 设置环境变量 | `sys_utils::setEnvironmentVariable` |
| `SystemInfo::is_wine()` | 是否运行在 Wine 兼容层下 | `sys_utils::isWine` |

> 这些在 crate 里是 `system` 模块的自由函数（如 `system::os_name()`、`system::env(name)`），本页 `SystemInfo::` 前缀只作分组标识。

> CPU / 内存占用这类进程级指标，`sys_utils` 里没有找到对应封装；真要做，会是 Rust 侧用 `sysinfo` 这类 crate 实现，同样不需要桥接参与。

## File / FileSystem — 文件与目录

> **不对应原生/LeviLamina 类**。文件读写就是标准库 `std::fs`（`read_to_string`/`write`/`create_dir_all`/`remove_file` 等）能直接做的事，跟游戏引擎毫无关系，不需要经过桥接，模组里直接 `use std::fs` 即可。

## Network — 网络请求

> **不对应原生/LeviLamina 类**。HTTP 请求、WebSocket 这类需求，用 Rust 生态的 `reqwest`（HTTP）、`tokio-tungstenite`（WebSocket）等 crate 直接实现；发起请求本身不需要接触游戏引擎，同样不经过桥接。真正需要桥接的，是请求结果要**影响游戏世界**时的那一步——这时候用 [Scheduler](/api/scheduler) 的 `Scheduler::run`，把请求完成后的处理逻辑投递回服务器线程执行。

## SystemCall — 系统调用与子进程

> **不对应原生/LeviLamina 类**。执行系统命令、启动子进程，用标准库 `std::process::Command` 直接做。同样：只有"子进程的输出要拿来影响游戏"这一步需要经过 `Scheduler::run` 跨回服务器线程，发起调用本身不需要桥接参与。
