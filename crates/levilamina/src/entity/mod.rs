//! Entity handles: `ActorUniqueID`s resolved via `Level::fetchEntity` on
//! every call. Obtain them from [`crate::Server::spawn_mob`],
//! [`Entity::list`], command selector args, or [`crate::Player::as_entity`].

use crate::error::{Error, Result};
use crate::ffi::{call_out_str, s};
use crate::{rt, sys};

/// A `(id, type_name)` pair from [`Entity::list`].
#[derive(Debug, Clone)]
pub struct EntityId {
    pub id: i64,
    pub type_name: String,
}

/// An entity handle (ActorUniqueID). Copy-cheap; can never dangle.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct Entity {
    id: sys::LeviRsActorId,
}

/// `Actor` 是 [`Entity`] 的别名 —— 同一个类型，两个名字。
///
/// 原生 C++ 那边这个类叫 `Actor`（`Player : Mob : Actor`），LSE 和大部分
/// 基岩版插件文档也跟着叫 Actor；本 crate 早期选了 `Entity`。两个名字都会
/// 出现在别人写的教程里，于是「Entity 和 Actor 是不是两个东西」成了新手最
/// 常问的问题之一。
///
/// 它们**就是同一个东西**：
///
/// ```no_run
/// # use levilamina::prelude::*;
/// # fn demo(player: &Player) -> Result<()> {
/// let a: Actor = player.get_actor()?;
/// let b: Entity = player.as_entity()?;
/// assert_eq!(a, b);
/// # Ok(())
/// # }
/// ```
///
/// 写新代码用哪个都行。习惯 LSE / C++ 命名的用 `Actor`，
/// 习惯本 crate 旧文档的用 `Entity`。
pub type Actor = Entity;

mod actions;
mod gap_fill;
mod query;

impl Entity {
    fn gone(&self) -> Error {
        Error(format!(
            "entity {} not found (despawned or unloaded)",
            self.id
        ))
    }

    fn get_num(&self, prop: i32) -> Result<f64> {
        let mut out = 0.0f64;
        let ok = unsafe { (rt().api.actor_get_num)(self.id, prop, &mut out) };
        if ok {
            Ok(out)
        } else {
            Err(self.gone())
        }
    }

    fn get_str(&self, prop: i32) -> Result<String> {
        call_out_str(|ctx, sink| unsafe { (rt().api.actor_get_str)(self.id, prop, ctx, sink) })
            .ok_or_else(|| self.gone())
    }

    fn action(&self, action: i32, sarg: &str, a: f64, b: f64, c: f64) -> Result<Option<String>> {
        let mut out: Option<String> = None;
        let ok = unsafe {
            (rt().api.actor_action)(
                self.id,
                action,
                s(sarg),
                a,
                b,
                c,
                (&mut out as *mut Option<String>).cast(),
                crate::ffi::set_string,
            )
        };
        if ok {
            Ok(out)
        } else {
            Err(Error("entity gone or action unsupported".into()))
        }
    }
}
