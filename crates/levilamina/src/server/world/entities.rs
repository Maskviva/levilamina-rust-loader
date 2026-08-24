use super::*;
use crate::entity::Entity;
use crate::error::{Error, Result};
use crate::ffi::s;
use crate::{rt, sys};

impl Server {
    pub fn spawn_mob(&self, dim: i32, type_name: &str, x: f64, y: f64, z: f64) -> Result<Entity> {
        let mut id: sys::LeviRsActorId = 0;
        let ok = unsafe { (rt().api.spawn_mob)(dim, s(type_name), x, y, z, &mut id) };
        if ok {
            Ok(Entity::from_id(id))
        } else {
            Err(Error(format!("spawn_mob('{type_name}') failed")))
        }
    }

    #[allow(clippy::too_many_arguments)]
    pub fn explode(
        &self,
        dim: i32,
        x: f64,
        y: f64,
        z: f64,
        radius: f32,
        source: Option<&Entity>,
        fire: bool,
        breaks_blocks: bool,
    ) -> Result<()> {
        let ok = unsafe {
            (rt().api.explode)(
                dim,
                x,
                y,
                z,
                radius,
                f32::MAX,
                source.map(|e| e.id()).unwrap_or(0),
                fire,
                breaks_blocks,
                false,
            )
        };
        if ok {
            Ok(())
        } else {
            Err(Error("explode failed (level/dimension not ready?)".into()))
        }
    }
}
