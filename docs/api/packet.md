# Packet — 抓包 / 改包

> 状态：✅ **已支持**（ABI 仍为 v5，四个槽位追加在 `Future additive fields` 锚点处）。
> 双向拦截原始线格式字节：`ModContext::packets()` 拿到 [`Packets`]，`intercept` 挂拦截器，
> `on_connection` 观察连接开闭。
>
> ⚠️ **这是本 crate 里唯一不保证跑在游戏线程上的回调。** 动世界之前先读完「线程模型」一节。

这一层在其他所有 API 之下。[Event](/api/event) 给你的是反序列化好的 `CompoundTag`，
[Server](/api/server) 给你的是玩家、方块这类游戏对象；而拦截器看到的就是这个包即将写进
socket 的样子——一个 id 加一段字节。

典型用途：跨版本协议翻译、抓包调试、包级反作弊、给旧客户端补字段。

## 交付单位：一个包

一次回调 = 一个包。loader 在调用前把开头那个 varint 包头**拆掉**，回调结束后再**装回去**：

```
线上格式：  [uvarint header][body...]
                  ↓ loader 解开
回调看到：  packet_id / sender_sub_id / target_sub_id  +  body()
                  ↓ loader 装回
线上格式：  [uvarint header][新 body...]
```

包头里的位域布局（第 0..9 位是包 id，第 10..11 位是 sender sub id，第 12..13 位是 target
sub id）由 loader 负责，所以：

- `body()` 拿到的是**纯 body**，不含包头；
- `set_body()` 写回的也必须是**纯 body**，**绝对不要**自己加长度前缀；
- 换包 id 是 `set_packet_id(n)` 一句赋值，不用做字节手术。

批处理（batch）和压缩都在钩子**下面**：`BatchedNetworkPeer` 负责把入站批拆开、把出站包
重新打批。所以回调永远不会看到一个 batch，也永远不需要处理长度前缀。

## 拦截 API

| API | 作用 |
| --- | --- |
| `ModContext::packets() -> Packets` | 拿到入口。两个构建目标（server / client）都有，因为这些槽位排在 client-only 块之前 |
| `Packets::intercept(dir, handler) -> Result<PacketHook>` | 挂一个拦截器。`dir` 取 `Direction::Inbound` / `Outbound` / `Both` |
| `Packets::on_connection(handler) -> Result<PacketHook>` | 观察连接打开 / 关闭 |
| `PacketHook::forget()` | 让拦截器活到 mod 卸载（丢弃 `PacketHook` 则自动摘钩，RAII，和 `Listener` 一致） |

`handler` 签名是 `Fn(&mut PacketCtx) -> Verdict + Send + Sync + 'static`。
注意是 `Fn` 而不是 `FnMut`——原因见下面的线程模型。

### PacketCtx

| 方法 | 说明 |
| --- | --- |
| `direction() -> Direction` | 本包的方向，永远不会是 `Both` |
| `conn_id() -> u64` | 连接标识（`NetworkIdentifier::getHash()`），整条连接生命周期内稳定，**且在 `Player` 存在之前就可用**——这正好是登录阶段改包需要的 |
| `address() -> &str` | `"host:port"` |
| `packet_id() -> i32` | `MinecraftPacketIds` 值，已反映 `set_packet_id` 的修改 |
| `sender_sub_id()` / `target_sub_id() -> u8` | 分屏子客户端 id |
| `body() -> &[u8]` | 包体（不含包头）。若已调用 `set_body`，返回的是暂存的新内容 |
| `set_body(&[u8])` | 替换包体 |
| `set_packet_id(i32)` | 改写包 id（保留 body） |
| `set_sender_sub_id(u8)` / `set_target_sub_id(u8)` | 改写子客户端 id |

### Verdict

| 值 | 含义 |
| --- | --- |
| `Verdict::Forward` | 放行，带上通过 `PacketCtx` 做的所有修改 |
| `Verdict::Drop` | 丢包。对端收不到；入站方向上收包循环会直接取下一个包 |

只改字段但返回 `Forward` 就够了，不需要额外声明「我改过了」——loader 会根据是否调用过
setter 决定要不要重建这个包。

## 示例：把客户端协议号改成服务端的

跨版本适配的第一步：客户端在 `RequestNetworkSettings`(193) 和 `Login`(1) 里报自己的协议号，
BDS 一看不匹配就直接踢人。先记下真实值，再盖成服务端的：

```rust
use std::collections::HashMap;
use std::sync::Mutex;

use levilamina::packet::{ConnectionState, Direction, Verdict};
use levilamina::prelude::*;

const REQUEST_NETWORK_SETTINGS: i32 = 193;
const LOGIN: i32 = 1;
const SERVER_PROTOCOL: i32 = 800;

struct MyMod {
    /// conn_id -> 客户端真实协议号。回调可能在别的线程上跑，所以要加锁。
    clients: &'static Mutex<HashMap<u64, i32>>,
}

fn install(ctx: &ModContext, clients: &'static Mutex<HashMap<u64, i32>>) -> Result<()> {
    ctx.packets()
        .intercept(Direction::Inbound, move |p| {
            if p.packet_id() != REQUEST_NETWORK_SETTINGS && p.packet_id() != LOGIN {
                return Verdict::Forward;
            }
            // 两个包的协议号都是开头 4 字节的大端 i32。
            let body = p.body();
            if body.len() < 4 {
                return Verdict::Forward;
            }
            let client_proto = i32::from_be_bytes([body[0], body[1], body[2], body[3]]);
            clients.lock().unwrap().insert(p.conn_id(), client_proto);

            let mut patched = body.to_vec();
            patched[..4].copy_from_slice(&SERVER_PROTOCOL.to_be_bytes());
            p.set_body(&patched);
            Verdict::Forward
        })?
        .forget();

    // 连接断开时清掉状态，否则这个表会一直长。
    ctx.packets()
        .on_connection(move |conn_id, _addr, state| {
            if state == ConnectionState::Closed {
                clients.lock().unwrap().remove(&conn_id);
            }
        })?
        .forget();

    Ok(())
}
```

## 线程模型

本 crate 其他所有回调都跑在游戏线程上。**这里不是。** 入站回调跑在实际抽这条连接的线程上，
出站回调跑在实际发包的线程上。实践中通常就是服务器线程，但 async flush 的存在意味着这一点
无法保证，ABI 不会承诺它做不到的事。

两个后果，都由签名而不是「希望你记得看文档」来兜住：

1. handler 是 `Fn + Send + Sync`，所以共享状态得放进 `Mutex`（或原子量），
   而不是靠捕获可变引用；
2. 这里**不能碰世界**。要读写方块、找玩家、发消息，用
   [`Server::schedule`](/api/scheduler) 把活儿丢回游戏线程。

## 多个拦截器

多个拦截器按**注册顺序**串联，后一个看到的是前一个的输出。第一个返回 `Drop` 的生效，
后面的不再执行。回调内部注册 / 反注册（包括摘掉自己）是安全的：分发前会先对订阅表做快照。

## 性能注意事项

拦截器坐在整个服务端最热的循环上。

- **不感兴趣就尽快 `Forward`。** 没有任何订阅者调用 setter 时，loader 不会复制包体——
  一个区块包有几十 KB，而绝大多数包是原样转发的。所以先看 `packet_id()` 再决定要不要解析。
- **不要每包打日志。** 想调试就先按包 id 过滤。
- **解析要惰性。** 只在确实要改的那几个包上做反序列化。

## 稳定性与失效保护

出站方向上，loader 会把自己解出来的包 id 和 `Packet::getId()` 对一次。两者不一致说明这个
BDS 版本的包头布局变了——此时 loader 只会**打一条错误日志然后全部放行**，不会拿一个错误的
假设去改字节流。看到这条日志请报 issue，并且在修好之前不要指望翻译生效。

## 相关

- [Event — 事件监听](/api/event)：拿到的是反序列化后的事件，不是字节
- [Scheduler — 调度](/api/scheduler)：从拦截器回到游戏线程的唯一途径
- [ABI 设计](/advanced/abi)：为什么这四个槽位追加在那个锚点上、以及为什么 ABI 仍是 v5
