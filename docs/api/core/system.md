# System — 系统信息

主机的操作系统信息和环境变量。**全部线程安全**。

```rust
use levilamina::system;

println!("{} {}", system::os_name()?, system::os_version()?);
println!("区域设置 {}", system::locale()?);

let t = system::local_time()?;
println!("{}-{:02}-{:02} {:02}:{:02}:{:02}", t.year, t.month, t.day, t.hour, t.minute, t.second);
```

| API | 返回 | 说明 |
| --- | --- | --- |
| `system::os_name()` | `Result<String>` | 如 `Windows` |
| `system::os_version()` | `Result<String>` | 系统版本 |
| `system::locale()` | `Result<String>` | 如 `zh_CN` |
| `system::local_time()` | `Result<LocalTime>` | 本地时间 |
| `system::env(name)` | `String` | 环境变量，未设置返回空串 |
| `system::set_env(name, value)` | `Result<()>` | 设置环境变量 |
| `system::is_wine()` | `bool` | 是不是跑在 Wine 下 |

```rust
pub struct LocalTime {
    pub year: i32, pub month: i32, pub day: i32,
    pub hour: i32, pub minute: i32, pub second: i32,
    pub ms: i32,
}
```

::: tip is_wine 有什么用
Linux 上用 Wine 跑 BDS 的服务器不少。某些和文件路径、时间精度相关的行为在 Wine 下不一样，需要区别对待时用它。
:::

## 没有的东西

文件读写、进程管理、网络请求**都没有封装**——用 Rust 标准库和你喜欢的 crate 就行，它们在这个环境里正常工作。

唯一要注意的是线程规则：后台干活可以，回来碰世界必须走 [`schedule`](/api/rt/scheduler)。
