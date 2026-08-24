# Bus — 跨模组事件总线

模组之间广播消息。「发生了什么」用这个；「问一个问题」用 [Service](/api/core/service)。

```rust
use levilamina::bus;

// 听别人的
bus::subscribe("plot:enter", |_topic, payload| {
    println!("有人进了地皮：{payload}");
    false      // 不否决
})?.forget();

// 发自己的
bus::publish("mymod:started", r#"{"version":"1.0"}"#);
```

## 为什么不能「模组 A 直接导出一个函数」

因为做不到。卸载模组会调 `FreeLibrary`，直接交给别的模组的回调指针在订阅方被卸载的那一刻就指向了未映射内存——而崩溃发生在**发布方**，日志里没有任何线索指向那个已经离开的模组。

所以订阅表由加载器持有，每次派发都重新校验归属模组，模组走了就整组丢掉。这和表单、调度任务用的是同一套纪律。

## API

| API | 说明 |
| --- | --- |
| `bus::subscribe(topic, handler)` | 订阅，返回 `Result<Subscription>` |
| `bus::publish(topic, payload) -> u32` | 广播，返回跑了几个订阅者 |
| `bus::publish_vetoable(topic, payload) -> Vetoable` | 广播并收集否决位 |
| `bus::subscriber_count(topic) -> u32` | 当前有几个订阅者 |
| `sub.forget()` | 活到模组卸载 |
| `sub.id() -> u64` | 订阅 id，只用于日志 |

处理函数签名是 `FnMut(&str, &str) -> bool`：`(topic, payload) -> 是否否决`。只是观察就返回 `false`。

## 主题命名

普通字符串，最长 128 字节。**一定要加前缀**——`plot:enter`，不是 `enter`。

没有注册表也没有保留字，所以裸主题名就是在等第二个有同样想法的模组来撞车。

## 载荷格式自己定

加载器**从不解析** `payload`。和对接的人商量好格式（JSON 最常见，SNBT 和这套 ABI 更搭），版本管理也自己来。

加载器定义 schema 意味着又多了一样必须在独立发布的模组之间同步升级的东西。

## 两条要先知道的规则

### 收不到自己发的

想通知自己有函数调用可以用。而且自发自收是唯一一种**没有任何深度限制能区分出来**的循环形状。

### 订阅者只能收紧，不能放松

`publish_vetoable` 把任何订阅者返回的 `true` 当作否决，**没有任何东西能把否决翻回批准**：

```rust
let v = bus::publish_vetoable("plot:can_build", payload);
if v.refused {
    // 有人不同意
}
println!("送到了 {} 个订阅者", v.delivered);
```

```rust
pub struct Vetoable { pub refused: bool, pub delivered: u32 }
```

否则就变成"最后跑的那个说了算"，而订阅顺序不是任何一方能控制的。

另外**所有订阅者都会跑完**，没有短路——只观察的模组看到的事件流不会因为别人否决了就缺一块。

## 什么时候检查 subscriber_count

构造载荷很贵的时候值得先查一下。载荷很便宜就别查了——这个检查本身要拿一次锁。
