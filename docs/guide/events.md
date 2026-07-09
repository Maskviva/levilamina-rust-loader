# 事件

事件是模组感知游戏世界的主通道：玩家聊天、方块破坏、服务器启动……全部以事件形式广播。本页讲**今天可用**的订阅、读取（含结构化字段与玩家句柄）、改写与取消；完整的事件清单、事件 id 常量、以及规划中的逐事件强类型访问器见 [Event 参考](/api/event)。

## 订阅一个事件

在 `on_enable` 里通过 `Server::subscribe_event` 按事件 id 订阅：

```rust
fn on_enable(&mut self, ctx: &ModContext) -> Result<()> {
    let logger = ctx.logger();
    ctx.server()
        .subscribe_event("PlayerJoinEvent", EventPriority::Normal, move |ev| {
            logger.info(&format!("[join] {}", ev.snbt()));
        })?
        .forget();
    Ok(())
}
```

三个参数：

1. **事件 id** —— 支持**唯一后缀匹配**：写 `"PlayerChatEvent"` 就够了，不必写出带命名空间的全名。进游戏执行 `/levirs events` 可导出你服务器上当前存在的全部事件 id（包括其他模组发布的事件）。
2. **优先级** —— `Highest` / `High` / `Normal` / `Low` / `Lowest`，决定多个监听器之间的调用顺序。
3. **回调** —— 收到 `EventRef`，仅在服务器线程上被调用。

`Server::list_events()` 也能在代码里拿到同一份事件 id 列表。

## 监听器的生命周期

`subscribe_event` 返回一个 `Listener`，RAII 语义：

```rust
let l = server.subscribe_event("PlayerChatEvent", EventPriority::Normal, |ev| { /* … */ })?;

// 方式一：存进模组结构体，模组禁用/析构时自动退订
self.chat_listener = Some(l);

// 方式二：整个模组生命周期都要听 → forget
// l.forget();
```

丢弃 `Listener` 即退订；`.forget()` 让它存活到模组卸载，卸载时加载器会强制解除本模组全部监听，不会有残留。

## 读取事件数据

事件数据以 **SNBT 文本**（NBT 的字符串表示，形似 JSON）交给回调，也能解析成结构化的 [NbtValue](/api/nbt)：

```rust
server.subscribe_event("PlayerChatEvent", EventPriority::Normal, move |ev| {
    let id = ev.id();       // 实际匹配到的完整事件 id
    let data = ev.snbt();   // 事件全部字段的 SNBT 文本
    logger.info(&format!("{id}: {data}"));

    // 或结构化读取某个字段
    if let Ok(v) = ev.value() {
        if let Some(msg) = v.get("message").and_then(|m| m.as_str()) {
            logger.info(&format!("聊天内容: {msg}"));
        }
    }
})?;
```

想知道某个事件里有哪些字段，最快的办法就是先订阅它、把 `snbt()` 打进日志看一眼。

### 直接取出事件里的玩家

玩家类事件和命令事件的回调，不用自己从 SNBT 里抠玩家名——`player()` 给出 `{name, xuid, uuid}` 身份，`player_handle()` 更进一步解析成可直接调用的 [Player](/api/player) 句柄：

```rust
server.subscribe_event("ExecutedCommandEvent", EventPriority::Normal, move |ev| {
    // 控制台/面板发起的命令没有玩家，player() 返回 None
    if let Some(who) = ev.player() {           // PlayerIdentity { name, xuid, uuid }
        let cmd = ev.value().ok()
            .and_then(|v| v.get("command").and_then(|c| c.as_str()).map(String::from))
            .unwrap_or_default();
        logger.info(&format!("{} 执行了 /{}", who.name, cmd));
    }
    // 需要进一步操作该玩家时，用 ev.player_handle() 拿到 Player 句柄
})?;
```

> 🧩 规划中的进一步升级：按事件类型给出**逐事件**强类型访问器（如 `chat_event.message()` 直接返回可改写的字符串、`block_changed.new_block()` 直接给出 `Block` 句柄），省去 `value().get("字段名")` 这一步。设计细节见 [Event 参考的"字段访问"一节](/api/event)。

## 修改与取消事件

- **取消**：对可取消事件（参考页各表的"可取消"列），调用 `ev.cancel()`，行为就不会发生——例如取消 `PlayerChatEvent`，这条聊天不会广播。
- **改写（文本）**：`ev.set_snbt(new_snbt)` 用修改后的 SNBT 覆盖事件数据。
- **改写（结构化）**：`ev.value()` 拿到 `NbtValue`，改完再 `ev.set_value(&v)` 写回；因为 `value()` 已包含尚未提交的改动，`value → 改 → set_value` 可以链式叠加。

```rust
server.subscribe_event("PlayerChatEvent", EventPriority::Normal, move |ev| {
    if ev.snbt().contains("bad_word") {
        ev.cancel();                 // 直接拦下这条聊天
    }
})?;

// 或改写字段而非取消
server.subscribe_event("PlayerChatEvent", EventPriority::Normal, move |ev| {
    if let Ok(mut v) = ev.value() {
        v.insert("message", NbtValue::String("(已过滤)".into()));
        ev.set_value(&v);
    }
})?;
```

> 注意：只有原生带**可变字段**的事件（如 `PlayerChatEvent` 的消息、`ActorHurtEvent` 的伤害值）改写才会真正生效——桥接对所有事件统一尝试写回，实际是否生效取决于该事件原生是否支持反序列化。哪些事件哪些字段可写，见 [Event 参考](/api/event)。

## 常用事件速查

完整清单在 [Event 参考](/api/event)，这里列最常用的一批（都可用后缀名直接订阅）：

| 事件 id | 触发时机 | 可取消 |
| --- | --- | :---: |
| `PlayerJoinEvent` | 玩家加入服务器 | ✅ |
| `PlayerDisconnectEvent` | 玩家断开连接 | — |
| `PlayerChatEvent` | 玩家发送聊天 | ✅ |
| `PlayerDieEvent` | 玩家死亡 | — |
| `PlayerDestroyBlockEvent` | 玩家破坏方块 | ✅ |
| `PlayerPlacingBlockEvent` | 玩家放置方块（前置，可取消；后置为 `PlayerPlacedBlockEvent`） | ✅ |
| `PlayerInteractBlockEvent` | 玩家与方块交互 | ✅ |
| `ActorHurtEvent` | 实体受伤 | ✅ |
| `MobDieEvent` | 生物死亡 | — |
| `ServerStartedEvent` | 服务器启动完成 | — |
| `ExecutingCommandEvent` | 任意命令执行前 | ✅ |

## 完整示例：进服欢迎

```rust
fn on_enable(&mut self, ctx: &ModContext) -> Result<()> {
    let server = ctx.server();
    ctx.server()
        .subscribe_event("PlayerJoinEvent", EventPriority::Normal, move |_ev| {
            // 写操作走命令是当前最通用的模式,见「命令」一页
            let _ = server.execute_command("say 欢迎新玩家加入!");
        })?
        .forget();
    Ok(())
}
```
