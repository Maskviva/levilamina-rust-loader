# Packet — 抓包 / 改包

这是整个 crate 最底下那一层。[事件](/api/event) 给你反序列化好的 `CompoundTag`，[`Server`](/api/server) 给你游戏对象，而拦截器看到的是**包本身**：一个 id 加一串字节。

```rust
use levilamina::prelude::*;
use levilamina::packet::{Direction, Verdict};

ctx.packets()
    .intercept(Direction::Both, |p| {
        if p.direction() == Direction::Inbound && p.packet_id() == 1 {
            // Login：把服务端协议号盖到客户端的上面
            let mut body = p.body().to_vec();
            body[..4].copy_from_slice(&800i32.to_be_bytes());
            p.set_body(&body);
        }
        Verdict::Forward
    })?
    .forget();
```

## 线程 —— 这一页和别处不一样

::: danger 拦截器不在游戏线程上
这个 crate 里**其他所有回调**都在游戏线程。拦截器不是：入站回调跑在连接被泵的地方，出站回调跑在发送发起的地方。实践中通常是服务器线程，但异步 flush 意味着这**不是保证**，而 ABI 不承诺它做不到的事。

两个后果，都由签名强制而不是靠你记住文档：

- 处理函数是 `Fn + Send + Sync`，共享状态得放进 `Mutex` 或原子类型，不能可变捕获；
- **里面不许碰世界**。要碰就 `Server::schedule` 弹回游戏线程。
:::

## 一次一个包

加载器在调用前把前导 varint 头剥掉、返回后重建，所以：

- `body()` 是**纯包体**
- `set_packet_id()` 换 id 不需要你算 varint
- **绝对不要自己写长度前缀**

批处理和压缩发生在钩子下面，你永远不会看到一个 batch。

## API

| API | 说明 |
| --- | --- |
| `ctx.packets()` | 拿到 `Packets` 入口 |
| `.intercept(direction, handler)` | 挂拦截器，返回 `Result<PacketHook>` |
| `.on_connection(handler)` | 观察连接开关 |
| `hook.forget()` | 一直挂着 |

`Direction`：`Inbound`（客户端→服务端）、`Outbound`、`Both`。

`Verdict`：`Forward`（放行，带上你的修改）、`Drop`（丢弃，修改无意义）。

## PacketCtx

| 读 | 说明 |
| --- | --- |
| `p.direction()` | 这个包的方向，**永远不是 `Both`** |
| `p.conn_id() -> u64` | 连接 id，`Player` 还不存在时就可用 |
| `p.address() -> &str` | `"host:port"` |
| `p.packet_id() -> i32` | `MinecraftPacketIds` 值，反映你的 `set_packet_id` |
| `p.body() -> &[u8]` | 包体（改过就是改后的） |
| `p.sender_sub_id()` / `p.target_sub_id()` | 分屏子 id |

| 写 | 说明 |
| --- | --- |
| `p.set_body(&bytes)` | 换包体 |
| `p.set_packet_id(id)` | 换 id，保留包体 |
| `p.set_sender_sub_id(v)` / `p.set_target_sub_id(v)` | |

`PacketCtx` 只在回调期间有效，要留的东西自己复制一份。

## 多个拦截器

按注册顺序串起来，每个看到前一个的输出。**第一个 `Drop` 获胜**，后面的跳过。在处理函数里注册 / 注销是安全的。

## 连接生命周期

```rust
ctx.packets().on_connection(|conn_id, state, addr| {
    match state {
        ConnectionState::Opened => { /* 建表 */ }
        ConnectionState::Closed => { /* 清表 */ }
    }
})?.forget();
```

::: tip 这是唯一可靠的清理点
没走完登录握手的连接**永远不会变成 `Player`**，所以任何玩家事件都不会为它触发。按连接维护的状态只能在这里清。
:::

## 性能

拦截器在服务器最热的循环上。

- 懒解析——先看 id，不关心就直接 `Forward`
- 不关心的包**不要复制 body**
- **不要按包打日志**

## 给单个玩家发原始包

不用拦截器也能发：

```rust
player.send_packet(packet_id, &body)?;
```

按 id 反序列化包体后只发给这个玩家。做富文本、自定义 UI 这类 SDK 没封装的东西时用它。
