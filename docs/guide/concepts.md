# 核心概念

四件事：生命周期、句柄、线程、错误。

## 生命周期

```rust
impl LeviMod for MyMod {
    fn on_load(ctx: &ModContext) -> Result<Self> { /* 构造 */ Ok(MyMod) }
    fn on_enable(&mut self, ctx: &ModContext) -> Result<()> { Ok(()) }
    fn on_disable(&mut self, ctx: &ModContext) -> Result<()> { Ok(()) }
    fn on_unload(&mut self, ctx: &ModContext) -> Result<()> { Ok(()) }
}
```

| 钩子 | 此时可以做什么 |
| --- | --- |
| `on_load` | 读配置、开数据库、建目录。**关卡还没打开** |
| `on_enable` | 注册命令 / 事件 / 维度，读世界数据 |
| `on_disable` | 停掉自己的定时任务，保存状态 |
| `on_unload` | 最后的清理 |

::: danger on_load 里不能碰世界
关卡（Level）在 `on_load` 阶段还是 null。注册维度会抛异常，读玩家列表拿到的是空的。**任何和世界有关的东西都放在 `on_enable`。**
:::

::: tip on_load 失败应该让模组加载失败
配置读不了、数据库开不了，就直接 `return Err(...)`。静默用默认值硬跑，服主看到的现象是"改了配置没生效"，而日志里一个字都没有。
:::

## 句柄是标识符，不是指针

这是整套设计的核心。

| 类型 | 内部存的 |
| --- | --- |
| `Player` | 一个选择器（名字 / XUID / UUID） |
| `Actor` / `Entity` | 一个 `ActorUniqueID` |
| `Block` | `(维度, 坐标)` |
| `ItemStack` | 一段 SNBT |
| `Container` | `(归属者, 哪一个)` |

每次调用方法时才去解析。带来三个结果：

**1. 不可能悬垂。** 目标没了就返回 `Err`，不会崩服。这不是"小心写就没事"，是**架构上做不到**。

**2. 每次调用有查找成本。** 热循环里别反复构造句柄：

```rust
// ❌ 每格查一次玩家
for i in 0..1000 {
    Player::by_name("Steve").send_message("...")?;
}

// ✅
let p = Player::by_name("Steve");
for i in 0..1000 { p.send_message("...")?; }
```

**3. 什么该存、什么不该存是有讲究的。**

| 存这个 | 别存这个 | 为什么 |
| --- | --- | --- |
| `Player`（按 XUID） | `Actor` | 玩家重进后 ActorUniqueID 会变 |
| `Player`（按 XUID） | `Player`（按名字） | 名字会改 |
| 坐标 | `Block` | `Block` 本来就是坐标，存哪个都行 |

```rust
// 长期存储
struct MyMod {
    owners: HashMap<String, Player>,     // key 是 xuid
}
// 用的时候
let actor = owners.get(&xuid).unwrap().get_actor()?;
```

## 线程规则

**所有回调都在游戏线程。** 生命周期钩子、事件、命令、表单回调、调度任务——全部。

所以模组实例可以放心持有 `!Send` 的东西（比如 `Listener`），不需要 `Arc<Mutex<>>` 包一层。

### 两个例外

**1. 包拦截器不在游戏线程。**

```rust
ctx.packets().intercept(Direction::Both, |p| {
    // 这里不在游戏线程！
    // 不能碰世界。共享状态要 Mutex / 原子类型。
    Verdict::Forward
})?.forget();
```

签名是 `Fn + Send + Sync`，编译器会逼你做对。要碰世界就 `Server::schedule` 弹回去。

**2. 这几样是线程安全的**，后台线程可以直接用：

- `Server::schedule` / `schedule_after` / `gaming_status`
- `Client::schedule` / `schedule_after` / `gaming_status`
- `KvDb` 全部方法
- `system::*` 全部
- `Logger` 全部

### 后台线程的标准写法

```rust
std::thread::spawn(move || {
    let result = 很慢的活();                  // 后台，随便慢

    Server::get().schedule(move || {          // 回到游戏线程
        Player::broadcast(&result);
    });
});
```

::: danger 别在后台线程调用世界 API
上面那张白名单之外的一切，在游戏线程之外调用都是未定义行为。可能崩，也可能更糟——静默的数据损坏。
:::

## 错误处理

```rust
pub struct Error(pub String);
pub type Result<T> = std::result::Result<T, Error>;
```

**`Err` 的含义几乎总是「目标不在了」**，不是"出错了"：

| 调用 | `Err` 意味着 |
| --- | --- |
| `player.xxx()` | 玩家下线了 |
| `actor.xxx()` | 实体消失了 |
| `block.xxx()` | 区块没加载 |
| `service::call()` | 见 `CallError` |

所以这样写是正常的，不是在吞错误：

```rust
if let Ok(hp) = actor.health() {
    // 实体还在
}

// 或者
let _ = player.send_message("hi");     // 下线了就算了
```

::: tip 什么时候该 ? 什么时候该忽略
- 后续逻辑依赖这次调用的结果 → `?` 或 `if let Ok`
- 只是"尽量做一下"（发个提示、放个粒子）→ `.ok()` / `let _ =`
- 在 `on_enable` 里注册东西失败 → `?`，让模组加载失败比半个模组跑起来强
:::

### panic 不会拖垮服务器

每个 FFI 入口都套了 `catch_unwind`。模组里 panic 只在控制台打一条 error，服务器继续跑。

这不是让你放心 `unwrap` 的理由。这是让**别人的 bug** 不至于连累整台服务器。

## RAII 句柄

四种回调句柄，行为一致：

```rust
let l = ctx.server().subscribe_event(...)?;
drop(l);          // 退订

ctx.server().subscribe_event(...)?.forget();     // 活到模组卸载
```

| 类型 | 来自 |
| --- | --- |
| `Listener` | `subscribe_event` |
| `Subscription` | `bus::subscribe` |
| `Registration` | `service::register` |
| `PacketHook` | `packets().intercept` |

99% 的情况是 `on_enable` 里注册完直接 `.forget()`。

## 版本与 ABI

加载器的 ABI 大版本必须 **>=** 模组编译时的版本，加载时自动检查。

- 新加载器 → 能跑旧模组 ✅
- 旧加载器 → 拒绝新模组 ❌

当前：`levilamina 26.20.4`，ABI `v5`，对应 BDS `1.26.20` / LeviLamina `26.20.4`。

crate 版本和 Minecraft 版本是两回事：crate 走自己的 SemVer（ABI 在 1.0.0..26.20 之间一直是 v5），而 `[package.metadata.minecraft]` 记的才是"这一版在哪个游戏 / 加载器构建上验证过"。
