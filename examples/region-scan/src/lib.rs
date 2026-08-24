use levilamina::prelude::*;
use levilamina::types::{PositionF64, PositionI32 as Position};
use std::collections::HashMap;
use std::sync::{Mutex, OnceLock};
use std::time::Duration;

const OUTLINE_PARTICLE: &str = "minecraft:redstone_wire_dust_particle";

const REFRESH: Duration = Duration::from_millis(500);

const EDGE_STEP: f64 = 0.5;

const MAX_AUTO_SCAN_CELLS: usize = 32 * 32 * 32;

#[derive(Default, Clone)]
struct Selection {
    dim: i32,
    pos1: Option<Position>,
    pos2: Option<Position>,
}

impl Selection {
    fn bounds(&self) -> Option<(Position, Position)> {
        match (self.pos1, self.pos2) {
            (Some(a), Some(b)) => Some((
                (a.0.min(b.0), a.1.min(b.1), a.2.min(b.2)),
                (a.0.max(b.0), a.1.max(b.1), a.2.max(b.2)),
            )),
            _ => None,
        }
    }
}

fn selections() -> &'static Mutex<HashMap<String, Selection>> {
    static S: OnceLock<Mutex<HashMap<String, Selection>>> = OnceLock::new();
    S.get_or_init(|| Mutex::new(HashMap::new()))
}

pub fn latest_scan() -> &'static Mutex<Option<Scan>> {
    static S: OnceLock<Mutex<Option<Scan>>> = OnceLock::new();
    S.get_or_init(|| Mutex::new(None))
}

fn generation() -> &'static Mutex<u64> {
    static S: OnceLock<Mutex<u64>> = OnceLock::new();
    S.get_or_init(|| Mutex::new(0))
}

fn last_summary() -> &'static Mutex<Option<(usize, usize)>> {
    static S: OnceLock<Mutex<Option<(usize, usize)>>> = OnceLock::new();
    S.get_or_init(|| Mutex::new(None))
}

struct RegionScan;

impl LeviMod for RegionScan {
    fn on_load(ctx: &ModContext) -> Result<Self> {
        ctx.logger().info("region-scan loaded");
        Ok(RegionScan)
    }

    fn on_enable(&mut self, ctx: &ModContext) -> Result<()> {
        let logger = ctx.logger();
        ctx.server().register_command(
            "rscan",
            "Region selection + live block/entity scan",
            CommandPermission::Any,
            move |inv| handle_command(logger, inv),
        )?;
        logger.info("region-scan enabled — /rscan pos1, pos2, show, collect");
        Ok(())
    }

    fn on_disable(&mut self, ctx: &ModContext) -> Result<()> {
        *generation().lock().unwrap() += 1;
        ctx.logger().info("region-scan disabled");
        Ok(())
    }
}

fn handle_command(logger: Logger, inv: &CommandInvocation) {
    let server = Server::get();
    let sub = inv
        .args
        .split_whitespace()
        .next()
        .unwrap_or("")
        .to_lowercase();
    let player = inv.origin.to_owned();

    match sub.as_str() {
        "pos1" | "pos2" => {
            let Some(pos) = server.player_position(&player) else {
                inv.error("could not find your position (are you a player?)");
                return;
            };
            let cell = pos.block();
            let mut map = selections().lock().unwrap();
            let sel = map.entry(player.clone()).or_default();
            sel.dim = pos.dim;
            if sub == "pos1" {
                sel.pos1 = Some(cell);
            } else {
                sel.pos2 = Some(cell);
            }
            inv.success(&format!("{} set to {:?} (dim {})", sub, cell, pos.dim));
            if let Some((min, max)) = sel.bounds() {
                let (sx, sy, sz) = size_of_box(min, max);
                inv.success(&format!("selection is now {}×{}×{}", sx, sy, sz));
            }
        }

        "show" => {
            let has = selections()
                .lock()
                .unwrap()
                .get(&player)
                .and_then(|s| s.bounds())
                .is_some();
            if !has {
                inv.error("set both corners first: /rscan pos1 then /rscan pos2");
                return;
            }
            let gen = {
                let mut g = generation().lock().unwrap();
                *g += 1;
                *g
            };
            *last_summary().lock().unwrap() = None;
            inv.success("outline + live scan on");
            run_outline(logger, gen);
        }

        "hide" => {
            *generation().lock().unwrap() += 1;
            inv.success("outline + live scan off");
        }

        "collect" => {
            let bounds = selections()
                .lock()
                .unwrap()
                .get(&player)
                .and_then(|s| s.bounds());
            let dim = selections()
                .lock()
                .unwrap()
                .get(&player)
                .map(|s| s.dim)
                .unwrap_or(0);
            let Some((min, max)) = bounds else {
                inv.error("set both corners first");
                return;
            };
            match server.scan_region(dim, min, max) {
                Ok(scan) => {
                    report_scan(inv, &scan);
                    *latest_scan().lock().unwrap() = Some(scan);
                }
                Err(e) => inv.error(&format!("scan failed: {e}")),
            }
        }

        "status" => {
            let map = selections().lock().unwrap();
            match map.get(&player) {
                Some(sel) => {
                    inv.success(&format!(
                        "dim {}  pos1 {:?}  pos2 {:?}",
                        sel.dim, sel.pos1, sel.pos2
                    ));
                    if let Some((min, max)) = sel.bounds() {
                        let (sx, sy, sz) = size_of_box(min, max);
                        inv.success(&format!("box {:?}..{:?}  ({}×{}×{})", min, max, sx, sy, sz));
                    }
                }
                None => inv.success("no selection"),
            }
        }

        "clear" => {
            selections().lock().unwrap().remove(&player);
            *generation().lock().unwrap() += 1;
            inv.success("selection cleared");
        }

        _ => {
            inv.success("usage: /rscan pos1 | pos2 | show | hide | collect | status | clear");
        }
    }
}

fn run_outline(logger: Logger, gen: u64) {
    if *generation().lock().unwrap() != gen {
        return;
    }
    let server = Server::get();

    let sels: Vec<Selection> = selections().lock().unwrap().values().cloned().collect();
    for sel in &sels {
        let Some((min, max)) = sel.bounds() else {
            continue;
        };
        draw_outline(&server, sel.dim, min, max);

        let (sx, sy, sz) = size_of_box(min, max);
        if sx * sy * sz > MAX_AUTO_SCAN_CELLS {
            log_once(
                logger,
                (usize::MAX, usize::MAX),
                &format!(
                    "region {}×{}×{} too large for live scan — use /rscan collect",
                    sx, sy, sz
                ),
            );
            continue;
        }
        if let Ok(scan) = server.scan_region(sel.dim, min, max) {
            let sig = (scan.non_empty_count(), scan.entity_count());
            log_once(
                logger,
                sig,
                &format!("live scan: {} blocks, {} entities", sig.0, sig.1),
            );
            *latest_scan().lock().unwrap() = Some(scan);
        }
    }

    server.schedule_after(REFRESH, move || run_outline(logger, gen));
}

fn log_once(logger: Logger, sig: (usize, usize), msg: &str) {
    let mut last = last_summary().lock().unwrap();
    if *last != Some(sig) {
        *last = Some(sig);
        logger.info(msg);
    }
}

fn draw_outline(server: &Server, dim: i32, min: Position, max: Position) {
    let (x0, y0, z0) = (min.0 as f64, min.1 as f64, min.2 as f64);
    let (x1, y1, z1) = ((max.0 + 1) as f64, (max.1 + 1) as f64, (max.2 + 1) as f64);

    let c = [
        (x0, y0, z0),
        (x1, y0, z0),
        (x0, y1, z0),
        (x1, y1, z0),
        (x0, y0, z1),
        (x1, y0, z1),
        (x0, y1, z1),
        (x1, y1, z1),
    ];

    const EDGES: [(usize, usize); 12] = [
        (0, 1),
        (2, 3),
        (4, 5),
        (6, 7),
        (0, 2),
        (1, 3),
        (4, 6),
        (5, 7),
        (0, 4),
        (1, 5),
        (2, 6),
        (3, 7),
    ];
    for &(a, b) in &EDGES {
        draw_edge(server, dim, c[a], c[b]);
    }
}

fn draw_edge(server: &Server, dim: i32, a: PositionF64, b: PositionF64) {
    let (dx, dy, dz) = (b.0 - a.0, b.1 - a.1, b.2 - a.2);
    let len = (dx * dx + dy * dy + dz * dz).sqrt();
    let steps = (len / EDGE_STEP).ceil().max(1.0) as i32;
    for i in 0..=steps {
        let t = i as f64 / steps as f64;
        let _ = server.spawn_particle(
            dim,
            OUTLINE_PARTICLE,
            a.0 + dx * t,
            a.1 + dy * t,
            a.2 + dz * t,
        );
    }
}

fn report_scan(inv: &CommandInvocation, scan: &Scan) {
    let (sx, sy, sz) = scan.size();
    inv.success(&format!(
        "scanned {}×{}×{} ({} layers): {} non-empty cells, {} entities",
        sx,
        sy,
        sz,
        scan.layers.len(),
        scan.non_empty_count(),
        scan.entity_count()
    ));
    for layer in &scan.layers {
        let blocks = layer
            .cells
            .iter()
            .flatten()
            .filter(|c| !c.block.is_air())
            .count();
        let ents: usize = layer.cells.iter().flatten().map(|c| c.entities.len()).sum();
        if blocks > 0 || ents > 0 {
            inv.success(&format!(
                "  y={}: {} blocks, {} entities",
                layer.y, blocks, ents
            ));
        }
    }
}

fn size_of_box(min: Position, max: Position) -> (usize, usize, usize) {
    (
        (max.0 - min.0 + 1) as usize,
        (max.1 - min.1 + 1) as usize,
        (max.2 - min.2 + 1) as usize,
    )
}

levilamina::register_mod!(RegionScan);
