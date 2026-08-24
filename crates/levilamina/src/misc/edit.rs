use crate::error::{Error, Result};
use crate::ffi::{call_out_str, s};
use crate::nbt::NbtValue;
use crate::{rt, sys, Block, Entity, Server};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct BlockUpdate(pub i32);

impl BlockUpdate {
    pub const NEIGHBORS: BlockUpdate = BlockUpdate(1);

    pub const NETWORK: BlockUpdate = BlockUpdate(2);

    pub const DEFAULT: BlockUpdate = BlockUpdate(3);

    pub const NETWORK_ONLY: BlockUpdate = BlockUpdate(2);

    pub const NONE: BlockUpdate = BlockUpdate(0);
}

impl Default for BlockUpdate {
    fn default() -> Self {
        BlockUpdate::DEFAULT
    }
}

impl Block {
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

#[derive(Debug, Clone, PartialEq)]
pub struct RayHit {
    pub kind: RayHitKind,

    pub block: (i32, i32, i32),

    pub facing: i32,

    pub pos: (f64, f64, f64),

    pub entity: Option<Entity>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RayHitKind {
    Block,
    Entity,

    EntityOutOfRange,
    None,
}

impl RayHit {
    fn kind_of(raw: i64) -> RayHitKind {
        match raw {
            0 => RayHitKind::Block,
            1 => RayHitKind::Entity,
            2 => RayHitKind::EntityOutOfRange,
            _ => RayHitKind::None,
        }
    }

    pub fn block_pos(&self) -> Option<(i32, i32, i32)> {
        (self.kind == RayHitKind::Block).then_some(self.block)
    }
}

impl Entity {
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
