# API 参考总览

这份参考描述的是 **`levilamina` crate 当前真实存在的公开 API**。

> **和旧版文档的区别。** 上一版列的是「目标设计」——其中相当一部分方法从来没有实现过（`player.connection_request()`、`player.start_using_item()`、`player.current_active_shield()` 之类），照着写会直接编译不过。这一版按代码里的 `pub fn` 逐条对齐：**列出来的都能编译，能编译的都列出来了**。

当前版本：`levilamina 26.20.4` · 对应 BDS `1.26.20` / LeviLamina `26.20.4` / ABI `v5`。

## 怎么读这份文档

- 每页顶部说明**这个类型是什么、从哪里拿到**。
- 方法表里的签名就是 Rust 签名，参数名和顺序都是真的。
- 「原生对应」一列是给从 LSE / C++ 转过来的人做对照的，不是承诺行为完全一致。
- 返回 `Result<T>` 的方法，`Err` 的含义几乎总是同一个：**目标不在了**（玩家下线、实体消失、区块没加载）。不是"出错了"，是"没找着"。

## 按功能找

### 事件与网络

| 页面 | 讲什么 |
| --- | --- |
| [Event — 事件监听](/api/infra/event) | 订阅、读取、改写、取消事件；完整事件名常量表 |
| [Packet — 抓包 / 改包](/api/infra/packet) | 收发包拦截、登录阶段改写、连接生命周期 |

### 游戏对象

| 页面 | 讲什么 |
| --- | --- |
| [Player — 玩家](/api/actor/player) | 身份、消息、能力、游戏模式、物品栏、标题栏 |
| [Actor / Entity — 实体](/api/actor/entity) | 位置、生命值、药水效果、Tag、骑乘、射线检测 |
| [Block — 方块](/api/world/block) | 读写单个方块、方块状态、方块实体 |
| [Item — 物品](/api/world/item) | `ItemStack` 值对象、附魔、自定义名与 Lore |
| [Container — 容器](/api/world/container) | 物品栏 / 末影箱 / 箱子统一读写 |
| [ScoreBoard — 计分板](/api/infra/scoreboard) | 计分项、分数增删改、显示槽 |

### 世界与服务端

| 页面 | 讲什么 |
| --- | --- |
| [Server — 服务端](/api/infra/server) | 状态、时钟、天气、难度、gamerule、生成实体、爆炸、粒子 |
| [World — 世界扫描](/api/world/world) | `scan_region` 区域扫描、村庄、结构 |
| [Command — 命令](/api/actor/command) | 执行原版命令、注册带参命令、枚举与 soft enum |
| [Dimensions — 自定义维度](/api/world/dimensions) | 注册新维度、地皮世界生成器、按维度规则 |
| [SimPlayer — 模拟玩家](/api/actor/sim) | 假人：移动、挖掘、交互、聊天 |

### 跨模组

| 页面 | 讲什么 |
| --- | --- |
| [Bus — 跨模组事件总线](/api/core/bus) | 广播「发生了什么」，一对多 |
| [Service — 跨模组服务](/api/core/service) | 问一个问题拿一个答案，一对一 |

### 数据

| 页面 | 讲什么 |
| --- | --- |
| [Nbt — NBT 读写](/api/world/nbt) | `NbtValue` 对象模型、SNBT 解析与序列化、二进制 NBT |
| [Data — 持久化](/api/infra/data) | `KvDb` 键值库、模组数据目录 |
| [Money — 经济](/api/actor/money) | LegacyMoney 集成（可选依赖） |

### 界面与运行时

| 页面 | 讲什么 |
| --- | --- |
| [Gui — 表单](/api/actor/gui) | SimpleForm / CustomForm / ModalForm |
| [Log — 日志](/api/rt/log) | 六档日志级别 |
| [Scheduler — 调度](/api/rt/scheduler) | `schedule` / `schedule_after`，后台线程回到游戏线程的唯一通道 |
| [System — 系统信息](/api/core/system) | 操作系统、区域设置、环境变量 |
| [Client — 客户端 API](/api/rt/client) | 客户端侧模组：本地玩家、按键绑定、界面状态 |
| [Objects — 其他类型](/api/core/objects) | 零散的值类型与全类型索引 |

## 三条贯穿全局的规则

### 1. 句柄是标识符，不是指针

| 类型 | 内部实际存的是 |
| --- | --- |
| `Player` | 一个选择器（名字 / XUID / UUID 三选一） |
| `Actor` / `Entity` | 一个 `ActorUniqueID`（i64） |
| `Block` | `(维度, 坐标)` |
| `ItemStack` | 一段 SNBT 文本 |
| `Container` | `(归属者, 哪一个容器)` |

每次调用都重新解析一遍。**所以句柄不可能悬垂**——最坏的情况是返回 `Err`，不是崩服。代价是每次调用都要查一次，热循环里别反复 `Player::by_name()`。

反过来说，**`Actor` 不适合跨 tick 保存**：玩家退出重进后 `ActorUniqueID` 会变。该长期存的是 `Player`（选择器），用的时候再 `.get_actor()`。

### 2. 回调都在游戏线程

生命周期钩子、事件、命令、表单回调、`schedule` 的任务——全部在服务器线程（客户端构建则是客户端线程）。

**唯一的例外是 [`packets().intercept()`](/api/infra/packet)**：它跑在网络泵所在的线程上，签名是 `Fn + Send + Sync`，里面不许碰世界。要碰就用 `Server::schedule` 弹回去。

明确线程安全的只有这几样：`Server::schedule` / `schedule_after` / `gaming_status`、`KvDb`、`system::*`、`Logger`。

### 3. panic 不会拖垮服务器

每个 FFI 入口都套了 `catch_unwind`。模组里 panic 只会在控制台打一条 error，服务器继续跑。这不是让你放心 `unwrap` 的理由，是让别人的 bug 不至于连累整台服务器。

## 还没有的东西

诚实列一下，省得你翻半天：

- **没有通用属性（Attribute）读写。** 等级 / 经验 / 饥饿 / 饱和度 / 疲劳有专用方法，其余属性没有通用入口。
- **没有物品使用生命周期**（`startUsingItem` / `releaseUsingItem` 那一套）。
- **没有配置文件封装。** 用 `serde_json` + `std::fs` 自己来，目录见 [Data](/api/infra/data)。
- **没有 HTTP 封装。** 自己引 `reqwest` 之类，在后台线程跑完用 `schedule` 回游戏线程。
- **没有权限系统。** 命令有 `CommandPermission` 五档，更细的自己实现。
