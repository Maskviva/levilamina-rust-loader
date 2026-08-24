---
layout: home

hero:
  name: levilamina-rs
  text: 用 Rust 编写 LeviLamina 模组
  tagline: 安全的句柄模型 · 完整的 API 参考 · 服务端与客户端双目标
  actions:
    - theme: brand
      text: 快速开始 →
      link: /guide/getting-started
    - theme: alt
      text: API 参考
      link: /api/core/overview
    - theme: alt
      text: 高级开发
      link: /advanced/architecture

features:
  - title: 🚀 初级开发
    details: 装加载器、写出第一个模组、事件 / 命令 / 世界 / 调度四大主题。全部示例可直接复制编译。
    link: /guide/getting-started
  - title: 📖 API 参考
    details: 按代码里的 pub fn 逐条对齐 —— 列出来的都能编译，能编译的都列出来了。
    link: /api/core/overview
  - title: 🔬 高级开发
    details: 四层架构、ABI 契约与演进规则、内存安全与句柄设计、扩展桥接的完整流程、设计取舍记录。
    link: /advanced/architecture
  - title: 句柄不会悬垂
    details: Player 是选择器，Actor 是 id，Block 是坐标 —— 每次调用现解析。目标没了返回 Err，不是崩服。
    link: /guide/concepts
  - title: 诚实的线程模型
    details: 所有回调在游戏线程，唯一例外是包拦截器，而它的签名会逼你做对。schedule 是后台线程回来的唯一通道。
    link: /guide/logging-scheduling
  - title: 跨模组通信
    details: 总线负责广播「发生了什么」，服务注册负责回答「一个问题」。句柄由加载器持有，卸载模组不会留下野指针。
    link: /api/core/bus
---

## 这一版文档修了什么

上一版列的是「目标设计」，里面有相当一部分方法**从来没有实现过** —— `player.connection_request()`、`player.start_using_item()`、`player.current_active_shield()` 之类，照着写会直接编译不过。`Ability` 那张索引表也是错的（`AttackMobs` 和 `AttackPlayers` 反了，`VerticalFlySpeed` 写成 15 实际是 19），而 FFI 边界上是个裸 `f64`，设错槽**不会报错，只是不生效**。

这一版按代码逐条核对重写，并补上了此前完全没有文档的几块：

- [自定义维度](/api/world/dimensions) —— 注册新维度、地皮世界生成器、**按维度**的行为规则（gamerule 做不到的那种）
- [跨模组总线](/api/core/bus) 与 [服务注册](/api/core/service)
- [模拟玩家](/api/actor/sim) —— carpet 风格的假人
- [客户端 API](/api/rt/client) —— 本地玩家、按键绑定

## 常见的第一个问题

**拿到 `Player` 之后怎么读位置和血量？**

它们在 `Actor` 那一层（原生继承链 `Player : Mob : Actor`），转过去：

```rust
let actor = player.get_actor()?;
let (x, y, z) = actor.pos()?;
let hp = actor.health()?;
```

`Actor` 是 `Entity` 的别名，同一个类型两个名字 —— 原生叫 Actor，本 crate 早期叫 Entity。详见 [Actor / Entity](/api/actor/entity)。
