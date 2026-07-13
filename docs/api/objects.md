# Objects — 其他游戏对象

## BlockEntity — 方块实体

> 状态：🧩 规划。
>
> **接口来源**：原生类叫 `BlockActor`（`mc/world/level/block/actor/BlockActor.h`），不叫 "BlockEntity"——这里沿用 LSE 的叫法作为页面标题，方法名标注仍是真实的 `BlockActor`。获取方式：`BlockSource::getBlockEntity(pos)`（真实的公开虚方法）。

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `block.block_entity()` | 获取该坐标的方块实体（没有则为空） | `BlockSource::getBlockEntity` |
| `block_entity.name()` | 类型名 | `BlockActor::getName` |
| `block_entity.display_name()` | 展示名（含格式） | `BlockActor::getDisplayName` |
| `block_entity.custom_name()` / `set_custom_name(name)` | 自定义名（如命名过的箱子） | `BlockActor::getCustomName` / `setCustomName` |
| `block_entity.container()` | 若该方块实体带容器（如箱子/漏斗），取其 `Container` | `BlockActor::getContainer` |
| `block_entity.refresh()` | 从方块源刷新自身状态 | `BlockActor::refresh` |
| `block_entity.to_nbt()` | 序列化为 NBT | `BlockActor::save` |
| `BlockActor::from_nbt(nbt)` | 从一份完整 NBT 构造方块实体 | `BlockActor::create`（静态） |

> 其余方法（`tick`/`onChanged`/`onPlace`/`onRemoved`/`onNeighborChanged` 等）和 [Block](/api/block) 页排除的情况一样，是引擎在特定时机主动调用的生命周期钩子，不是模组拿到句柄后主动去调的。

## Packet — 数据包

> 状态：部分✅——**原始发包原语已支持**；类型化的 Packet 富对象（按包种类构造、填字段）仍属 🧩 规划。
>
> **接口来源**：原生基类 `Packet`（`mc/network/Packet.h`）。具体的包（聊天消息、标题、计分板更新……）各自是独立的类，`mc/network/packet/` 下有几十个，例如 `TextPacket`、`SetTitlePacket`。

### 已支持：原始发包（逃生舱口）

| API | 作用 | 原生对应 | 状态 |
| --- | --- | --- | :---: |
| `player.send_packet(packet_id, body)` | 把一段**当前游戏版本线格式**的包体，按 `MinecraftPacketIds` 数值 id 反序列化成真实数据包对象，**只发给这一个玩家的连接** | `MinecraftPackets::createPacket` + `Packet::read` + `Player::sendNetworkPacket` | ✅ |

- 桥接会校验：玩家在线、id 可构造、包体解析成功、且**字节恰好读完**（有剩余字节说明形状不匹配当前版本，直接拒发，不会发出半解析的包）。
- ⚠️ 线格式随游戏版本变化，由调用方负责；能解析但内容不合理的包仍会被送达，可能造成客户端表现异常。**有类型化 API 时优先用类型化 API**（如 [`Server::spawn_particle_for`](/api/world) 就是在这个原语的发送路径上做的类型化派生）。

### 🧩 规划：类型化 Packet 对象

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `packet.send_to(player)` | 发送给指定玩家 | `Packet::sendTo` |
| `packet.send_to_all_clients()` | 广播给所有已连接客户端 | `Packet::sendToClients` |
| `Packet::create(id)` | 按数值 id 构造一个空的具体类型数据包，构造后填充字段 | `MinecraftPackets::createPacket` |
| `TextPacket::raw_message(text)` | 构造一条原始聊天消息包（不做名字/翻译替换） | `TextPacket::createRawMessage` |

> 绝大多数具体数据包（标题、计分板更新、Boss 血条……）都各自有自己的构造函数/字段，需要逐个按用途绑定；在被绑定之前，`player.send_packet` 是通用的绕行通道。

## Device — 设备信息

> **不对应一个独立的原生类**。早期草稿设想过一个单独的 `Device` 句柄，但核对头文件后没有找到这样的类型——设备/网络相关的信息实际分散挂在 `Player` 自己身上，都已经在 [Player](/api/player) 页里：

| 想要的信息 | 实际在哪 |
| --- | --- |
| IP、延迟、丢包 | `player.ip_and_port()` / `player.network_status()`（见 Player 页"身份与网络") |
| 客户端语言 | `player.locale_code()` |
| 登录连接信息 | `player.connection_request()` |

所以 Device 这一节不再作为独立对象出现；需要"设备信息"时直接查 Player 页对应方法即可。

## Particle — 画几何形状的粒子

> **不对应任何原生 API**。原生服务端只有单点粒子生成 `Level::spawnParticleEffect`（已作为 ✅ 的 `World::spawn_particle` 支持，见 [World](/api/world)）；"沿一条线/一个圆/一个立方体轮廓画粒子"这类几何图形，原生完全没有对应支持（客户端渲染那侧的 `ParticleEmitter` 是纯客户端内部实现，服务端模组接触不到）。
>
> 这类效果本质是**在 Rust 侧按几何计算出一串坐标，对每个坐标调用一次 `World::spawn_particle`（或只想给一个玩家看时用 `World::spawn_particle_for`）**——纯应用层逻辑，不需要新的桥接支持。仓库自带的 `examples/region-scan` 示例就是这么做的：按固定间距在选区边缘上取点、逐点生成粒子，拼出一个动画外框。需要"画线/画圆"效果时，参考那个示例即可，不必等一个专门的 `ParticleSpawner` API。
