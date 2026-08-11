//! 批量世界编辑 —— 原生写入，不经过命令解析。
//!
//! # 和 [`crate::Server::set_block`] 的关系
//!
//! `Server::set_block` 收的是 `/setblock` 的参数串，底层真的去跑了一条控制台
//! 命令。它没有被替换，因为玩家手写的方块规格（`wool ["color"="red"]`）走
//! 命令解析最省事。但凡是**程序自己读出来又写回去**的场景，都该走这里：
//!
//! | 你有的东西 | 用哪个 |
//! |---|---|
//! | `get_block` / `scan_region` 给的 snbt | [`Block::set_nbt`] |
//! | 方块名 + 一组状态（程序算出来的） | [`Block::set_states`] |
//! | 玩家手打的一串 `/setblock` 语法 | [`crate::Server::set_block`] |
//!
//! 差别不只是速度：走命令要把状态翻译成 `["k"=v]` 文本再解析回来，**任何一处
//! 翻译不准都是整条命令失败**，而失败是静默的 —— 表现只是「那一格没变」。
//! 原样送 NBT 没有这个中间环节。
//!
//! # 更新标志
//!
//! [`BlockUpdate`] 是位掩码。默认用 [`BlockUpdate::DEFAULT`]（通知邻居 + 同步
//! 客户端），它等价于 `/setblock` 的行为。批量填充可以用
//! [`BlockUpdate::NETWORK_ONLY`] 跳过邻居级联（红石不会连锁触发，水不会流），
//! 填完再对边界补一次带邻居更新的写入。
//!
//! [`BlockUpdate::NONE`] 连客户端都不通知 —— **填完必须自己想办法让客户端
//! 重新收到区块**，否则玩家看到的还是旧地形，而服务端是对的。这种「两边不
//! 一致且都不报错」的状态最难查，所以它不是默认值。

use crate::error::{Error, Result};
use crate::ffi::{call_out_str, s};
use crate::nbt::NbtValue;
use crate::{rt, sys, Block, Entity, Server};

/// `setBlock` 的更新标志位。
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct BlockUpdate(pub i32);

impl BlockUpdate {
    /// 通知邻居：红石、水流、支撑判定会跟着跑。
    pub const NEIGHBORS: BlockUpdate = BlockUpdate(1);
    /// 同步给客户端。
    pub const NETWORK: BlockUpdate = BlockUpdate(2);
    /// 两者都要 —— 和 `/setblock` 一致，绝大多数情况用它。
    pub const DEFAULT: BlockUpdate = BlockUpdate(3);
    /// 只同步客户端，不触发邻居级联。批量填充用。
    pub const NETWORK_ONLY: BlockUpdate = BlockUpdate(2);
    /// 什么都不通知。最快，但客户端不会知道。
    pub const NONE: BlockUpdate = BlockUpdate(0);
}

impl Default for BlockUpdate {
    fn default() -> Self {
        BlockUpdate::DEFAULT
    }
}

impl Block {
    /// 用**序列化 NBT** 写这个方块 —— `{name:"…",states:{…},version:…}`，
    /// 也就是 [`Block::snbt`] / [`crate::Server::get_block`] 给出来的那一串。
    ///
    /// 读出来什么样、写回去就什么样：朝向、轴向、上下半砖、开合状态都不会在
    /// 中间丢，因为中间没有「翻译成命令语法」这一步。
    pub fn set_nbt(&self, snbt: &str, update: BlockUpdate) -> Result<()> {
        let (dim, (x, y, z)) = (self.dimension(), self.position());
        let ok = unsafe { (rt().api.edit_set_block_nbt)(dim, x, y, z, s(snbt), update.0) };
        if ok {
            Ok(())
        } else {
            Err(Error(format!(
                "set_nbt 失败 ({x}, {y}, {z})：维度未就绪、SNBT 解析失败，\
                 或这个 name+states 组合在当前版本里不存在"
            )))
        }
    }

    /// 用**方块名 + 一组状态**写这个方块。`states` 为 `None` 表示全用默认状态。
    ///
    /// 只给出的键会覆盖默认值，没给的保持默认 —— 和 `/setblock` 的部分状态
    /// 语义一致。版本号由 loader 侧从该方块的默认状态取，调用方不用管
    /// （手工拼 NBT 时漏掉 `version` 会让引擎按远古存档跑一遍升级表，
    /// 把状态改成别的东西）。
    pub fn set_states(
        &self,
        name: &str,
        states: Option<&NbtValue>,
        update: BlockUpdate,
    ) -> Result<()> {
        let (dim, (x, y, z)) = (self.dimension(), self.position());
        let rendered = states.map(|v| v.to_snbt()).unwrap_or_default();
        let ok = unsafe {
            (rt().api.edit_set_block_states)(dim, x, y, z, s(name), s(&rendered), update.0)
        };
        if ok {
            Ok(())
        } else {
            Err(Error(format!(
                "set_states 失败 ({x}, {y}, {z})：方块名「{name}」不存在，或状态不合法"
            )))
        }
    }

    /// 把方块实体的 NBT 写回去 —— 箱子内容、告示牌文字、刷怪笼、旗帜图案、
    /// 蜂巢、命令方块，凡是 [`Block::block_entity`] 读得出来的都能写回去。
    ///
    /// **顺序有要求**：先把方块本身放好（[`Block::set_nbt`]），再填内容。
    /// 那一格没有方块实体时返回 `Err` —— 这通常意味着顺序反了。
    ///
    /// 快照里的 `x`/`y`/`z` 会被改写成这一格的坐标：不改的话，活塞和命令方块
    /// 这类会按记录的坐标去找自己，表现是「内容对了，行为错了」。
    pub fn set_block_entity(&self, snbt: &str) -> Result<()> {
        let (dim, (x, y, z)) = (self.dimension(), self.position());
        let ok = unsafe { (rt().api.edit_set_block_entity)(dim, x, y, z, s(snbt)) };
        if ok {
            Ok(())
        } else {
            Err(Error(format!(
                "set_block_entity 失败 ({x}, {y}, {z})：那一格没有方块实体（先放方块再填内容），或 SNBT 解析失败"
            )))
        }
    }
}

impl Server {
    /// 按**完整实体 NBT** 生成一个实体 —— [`Entity::snapshot`] 的逆操作。
    ///
    /// 变种（羊毛颜色、村民职业、马的花纹）、装备、年龄、属性、自定义名全都
    /// 跟着 NBT 回来。[`Server::spawn_mob`] 做不到这些，它只认类型名。
    ///
    /// `at` 为 `Some` 时覆盖 NBT 里的 `Pos`（粘贴时用，因为快照记的是源位置）；
    /// 为 `None` 时按 NBT 自带的位置放。
    ///
    /// UniqueID 由引擎重新分配 —— 沿用快照里的 id 会和源实体撞号，而撞号的
    /// 表现是两个实体被当成同一个：一个凭空消失、另一个行为错乱、没有日志。
    pub fn spawn_entity_nbt(
        &self,
        dim: i32,
        snbt: &str,
        at: Option<(f64, f64, f64)>,
    ) -> Result<Entity> {
        let (use_pos, (x, y, z)) = match at {
            Some(p) => (true, p),
            None => (false, (0.0, 0.0, 0.0)),
        };
        let mut id: sys::LeviRsActorId = 0;
        let ok =
            unsafe { (rt().api.edit_spawn_entity_nbt)(dim, s(snbt), use_pos, x, y, z, &mut id) };
        if ok {
            Ok(Entity::from_id(id))
        } else {
            Err(Error(
                "spawn_entity_nbt 失败：维度未就绪、SNBT 解析失败，或这不是一个可加载的实体标签"
                    .into(),
            ))
        }
    }
}

/// 一次射线投射的结果。
#[derive(Debug, Clone, PartialEq)]
pub struct RayHit {
    pub kind: RayHitKind,
    /// 命中的**方块坐标**。只在 `kind == Block` 时有意义。
    pub block: (i32, i32, i32),
    /// 命中面：0=下 1=上 2=北 3=南 4=西 5=东。
    pub facing: i32,
    /// 精确命中点（落在方块的面上）。
    pub pos: (f64, f64, f64),
    /// 命中的实体，只在 `kind == Entity` 时为 `Some`。
    pub entity: Option<Entity>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RayHitKind {
    Block,
    Entity,
    /// 打到实体但超出距离。
    EntityOutOfRange,
    None,
}

impl RayHit {
    fn kind_of(raw: i64) -> RayHitKind {
        // HitResultType：0=Tile 1=Entity 2=EntityOutOfRange 3=NoHit。
        // 注意这里是**整数**：老的 `trace_ray` 文档说它发字符串，实际发的是
        // 整数，说明那条路没人走通过。
        match raw {
            0 => RayHitKind::Block,
            1 => RayHitKind::Entity,
            2 => RayHitKind::EntityOutOfRange,
            _ => RayHitKind::None,
        }
    }

    /// 命中的方块坐标（没打中方块时为 `None`）。
    pub fn block_pos(&self) -> Option<(i32, i32, i32)> {
        (self.kind == RayHitKind::Block).then_some(self.block)
    }
}

impl Entity {
    /// 从这个实体的眼睛发一条射线，**带方块坐标和命中面**。
    ///
    /// 和 [`Entity::trace_ray`] 的区别就是这两样，而这两样决定了能不能做
    /// 「照着准星选方块」：老接口只给一个浮点命中点，而命中点正好落在方块的
    /// **面**上 —— `floor()` 有一半概率落到隔壁那一格。`HitResult` 本来就带
    /// `mBlock` 和 `mFacing`，只是以前没往外发。
    pub fn trace_ray_ex(
        &self,
        max_dist: f32,
        include_actors: bool,
        include_blocks: bool,
    ) -> Result<RayHit> {
        let raw = call_out_str(|ctx, sink| unsafe {
            (rt().api.edit_trace_ray)(
                self.id(),
                max_dist,
                include_actors,
                include_blocks,
                ctx,
                sink,
            )
        })
        .ok_or_else(|| Error(format!("trace_ray_ex: 实体 {} 不在了", self.id())))?;

        let v = NbtValue::parse(&raw)?;
        let triple_i32 = |key: &str| -> (i32, i32, i32) {
            v.get(key)
                .and_then(|l| l.as_list())
                .map(|l| {
                    let g = |i: usize| l.get(i).and_then(|n| n.as_i64()).unwrap_or(0) as i32;
                    (g(0), g(1), g(2))
                })
                .unwrap_or((0, 0, 0))
        };
        let triple_f64 = |key: &str| -> (f64, f64, f64) {
            v.get(key)
                .and_then(|l| l.as_list())
                .map(|l| {
                    let g = |i: usize| l.get(i).and_then(|n| n.as_f64()).unwrap_or(0.0);
                    (g(0), g(1), g(2))
                })
                .unwrap_or((0.0, 0.0, 0.0))
        };

        let kind = RayHit::kind_of(v.get("type").and_then(|t| t.as_i64()).unwrap_or(3));
        let ent = v.get("entity").and_then(|e| e.as_i64()).unwrap_or(0);
        Ok(RayHit {
            kind,
            block: triple_i32("block"),
            facing: v.get("facing").and_then(|f| f.as_i64()).unwrap_or(0) as i32,
            pos: triple_f64("pos"),
            entity: (kind == RayHitKind::Entity && ent != 0).then(|| Entity::from_id(ent)),
        })
    }
}
