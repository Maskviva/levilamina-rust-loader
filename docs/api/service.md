# Service — 跨模组服务

具名的请求 / 响应调用。**问一个问题拿一个答案**。

```rust
use levilamina::service;

// 提供服务
service::register("mymod:ping", |_name, request| {
    if request.is_empty() { return Err("需要一个名字".into()); }
    Ok(format!(r#"{{"pong":"{request}"}}"#))
})?.forget();

// 调用别人的
match service::call("plot:at", r#"{"dim":3,"x":10,"z":-4}"#) {
    Ok(reply) => println!("{reply}"),
    Err(service::CallError::NotFound) => println!("没装地皮插件"),
    Err(e) => println!("调用失败：{e}"),
}
```

## 和 Bus 的分工

| | [Bus](/api/bus) | Service |
| --- | --- | --- |
| 同名提供者 | 任意多个 | **恰好一个** |
| 没人注册时 | 正常 | 一个必须处理的错误 |
| 返回值 | 没有 | 就是重点 |
| 顺序 | 未定义，且必须无所谓 | 只有一个被调方 |

总线用来宣布「发生了什么」，服务用来问「一个问题」。

## API

| API | 说明 |
| --- | --- |
| `service::register(name, handler)` | 注册，返回 `Result<Registration>` |
| `service::call(name, request)` | 调用，返回 `Result<String, CallError>` |
| `service::exists(name) -> bool` | 现在有没有人提供 |
| `service::list_json() -> String` | 全部服务，原始 JSON `[{"name":…,"mod":…}]` |
| `reg.forget()` | 活到模组卸载 |

处理函数签名：`FnMut(&str, &str) -> Result<String, String>`，即 `(name, request) -> Ok(响应) | Err(错误说明)`。

## 注册是排他的

两个模组都提供 `plot:can` 不是"都跑"，而是一个没法让调用方挑选的歧义答案。所以 `register` **直接失败**，加载器日志里会写清楚是谁占着这个名字。

静默的"后者覆盖前者"会让答案取决于模组加载顺序——那个顺序没人控制，而且任何人装一个无关模组的那天它就变了。

## 命名空间

`plot:can`，不是 `can`。因为注册排他，裸名字撞车会变成**启动时的硬失败**，而不是运行时一个微妙的错误。这算是好事。

## 同步执行，没有超时

提供方在你的线程上直接跑完，答案从 `call` 返回。

**不会有超时机制**：会阻塞的提供方阻塞服务器线程的方式和任何别的回调完全一样，而返回"超时了"却让回调继续跑，等于同时给了你一个错误答案和一个还在运行的提供方。

## 不能调自己的服务

`call` 会拒绝。自己调自己等于绕两次 FFI 加一把互斥锁去到一个本来直接调用就行的函数，而当它真的构成循环时，栈是最难读的那种形状。

跨模组的循环（A → B → A）由深度上限拦住。

## CallError

```rust
pub enum CallError {
    NotFound,           // 没人提供这个服务
    Provider(String),   // 提供方返回了 Err
    Refused,            // 被拒绝（自调用、深度超限）
    Unknown(i32),
}
```

::: tip NotFound 是要处理的情况，不是要报错的情况
提供方模组没装就是 `NotFound`。这是"对方不在"，不是"出问题了"——照常走降级逻辑，别往日志里打 error。
:::

## exists 是有竞态的

提供方可能在 `exists` 和 `call` 之间卸载。用它来决定"要不要费劲构造一个昂贵的请求"可以，用它当保护伞不行——`NotFound` 该处理还是要处理。
