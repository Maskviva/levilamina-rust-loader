# Nbt — NBT 读写

`NbtValue` 是纯 Rust 的 SNBT 对象模型。事件载荷、实体快照、命令参数、方块数据出来的都是它。

```rust
use levilamina::prelude::*;

let v = NbtValue::parse(r#"{name:"steve",hp:20.0f,tags:["a","b"]}"#)?;
assert_eq!(v.get("name").and_then(|n| n.as_str()), Some("steve"));
```

## 类型

```rust
pub enum NbtValue {
    Byte(i8), Short(i16), Int(i32), Long(i64),
    Float(f32), Double(f64),
    String(String),
    List(Vec<NbtValue>),
    Compound(BTreeMap<String, NbtValue>),
    ByteArray(Vec<i8>), IntArray(Vec<i32>), LongArray(Vec<i64>),
}
```

复合标签用 `BTreeMap`，所以序列化结果是**确定的**——diff 稳定，测试稳定。

## 解析与序列化

| API | 说明 |
| --- | --- |
| `NbtValue::parse(text)` | 解析 SNBT，接受引擎输出的完整语法（含类型化数组、两种引号） |
| `NbtValue::compound()` | 一个空复合标签 |
| `v.to_snbt() -> String` | 序列化成最小化 SNBT，键只在需要时加引号 |

## 取值

| API | 说明 |
| --- | --- |
| `v.get(key)` | 取子项，`Option<&NbtValue>` |
| `v.get_mut(key)` | 可变版本 |
| `v.path("a.b.c")` | 点号路径取值 |
| `v.index(i)` | 列表下标 |
| `v.insert(key, value) -> bool` | 插入 / 覆盖；不是复合标签返回 `false` |

投影方法：`as_i64()` `as_f64()` `as_bool()` `as_str()` `as_list()` `as_compound()` `is_compound()`

::: tip as_i64 会跨整数类型工作
`Byte` / `Short` / `Int` / `Long` 都能用 `as_i64()` 取。不用先判断具体是哪一种。`as_f64()` 同理覆盖 `Float` / `Double`。
:::

## 实战：从事件载荷里挖东西

```rust
ctx.server().subscribe_event("PlayerUseItemOnEvent", EventPriority::Normal, |ev| {
    let Ok(v) = ev.value() else { return };
    let x = v.get("x").and_then(|n| n.as_i64()).unwrap_or(0);
    let name = v.path("_player.name").and_then(|n| n.as_str()).unwrap_or("?");
    println!("{name} 在 x={x} 用了个东西");
})?.forget();
```

`path()` 比嵌套一堆 `get()` 好读得多。

## 二进制 NBT

SNBT 对象模型是纯 Rust 的，但磁盘和网络上的二进制格式是**跟着引擎版本走**的，所以这两个方法委托给桥接，字节布局永远和运行中的服务器一致。

```rust
use levilamina::nbt::NbtBinaryFormat;

let bytes = v.to_binary(NbtBinaryFormat::Disk)?;
let back  = NbtValue::from_binary(&bytes, NbtBinaryFormat::Disk)?;
```

`NbtBinaryFormat`：`Disk = 0`（存档格式）、`Network = 1`（网络格式）。

## 和 serde 的关系

这个模块**不依赖 serde**。要把 `NbtValue` 和自己的结构体互转，自己写转换函数，或者在自己的 crate 里用 `serde_json` 中转一道。

给这个 crate 加 serde 依赖意味着每个链接它的模组都要背上——为了省几行转换代码不值当。
