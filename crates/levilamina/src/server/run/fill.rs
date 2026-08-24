use crate::error::{Error, Result};
use crate::server::dimsel::selector;
use crate::Server;

pub type Box3D = ((i32, i32, i32), (i32, i32, i32));

pub const MAX_VOLUME: i64 = 32_768;

pub fn split_box(from: (i32, i32, i32), to: (i32, i32, i32)) -> Vec<Box3D> {
    let (x0, x1) = (from.0.min(to.0), from.0.max(to.0));
    let (y0, y1) = (from.1.min(to.1), from.1.max(to.1));
    let (z0, z1) = (from.2.min(to.2), from.2.max(to.2));
    let area = (x1 - x0 + 1) as i64 * (z1 - z0 + 1) as i64;
    if area <= 0 {
        return Vec::new();
    }

    let per = (MAX_VOLUME / area).max(1);
    let mut out = Vec::new();
    let mut y = y0 as i64;
    while y <= y1 as i64 {
        let top = (y + per - 1).min(y1 as i64);
        out.push(((x0, y as i32, z0), (x1, top as i32, z1)));
        y = top + 1;
    }
    out
}

impl Server {
    pub fn fill_blocks(
        &self,
        dim: i32,
        from: (i32, i32, i32),
        to: (i32, i32, i32),
        block: &str,
    ) -> Result<usize> {
        let sel = selector(dim).ok_or_else(|| Error(format!("维度 {dim} 没有可用的选择器")))?;
        let boxes = split_box(from, to);
        let mut n = 0;
        for (a, b) in boxes {
            let r = self.execute_command(&format!(
                "execute in {sel} run fill {} {} {} {} {} {} {block}",
                a.0, a.1, a.2, b.0, b.1, b.2
            ))?;
            if !r.success {
                return Err(Error(format!(
                    "fill {} {} {} → {} {} {} 失败：{}",
                    a.0, a.1, a.2, b.0, b.1, b.2, r.output
                )));
            }
            n += 1;
        }
        Ok(n)
    }
}

#[cfg(test)]
mod tests {
    use crate::server::fill::{split_box, MAX_VOLUME};

    fn volume(a: (i32, i32, i32), b: (i32, i32, i32)) -> i64 {
        (b.0 - a.0 + 1) as i64 * (b.1 - a.1 + 1) as i64 * (b.2 - a.2 + 1) as i64
    }

    #[test]
    fn no_piece_exceeds_the_engine_limit() {
        for (a, b) in split_box((0, -64, 0), (31, 320, 31)) {
            assert!(volume(a, b) <= MAX_VOLUME, "{a:?}..{b:?} 超了上限");
        }
    }

    #[test]
    fn the_pieces_tile_the_original_box_exactly() {
        let (a, b) = ((0, -64, 0), (31, 320, 31));
        let pieces = split_box(a, b);
        assert_eq!(
            pieces.iter().map(|(p, q)| volume(*p, *q)).sum::<i64>(),
            volume(a, b),
            "切完的总体积和原盒子对不上"
        );

        for w in pieces.windows(2) {
            assert_eq!(w[0].1 .1 + 1, w[1].0 .1, "两块之间有缝或者重叠");
        }
        assert_eq!(pieces.first().unwrap().0 .1, -64);
        assert_eq!(pieces.last().unwrap().1 .1, 320);
    }

    #[test]
    fn a_layer_larger_than_the_limit_still_produces_one_piece_per_layer() {
        let pieces = split_box((0, 0, 0), (399, 2, 399));
        assert_eq!(pieces.len(), 3, "三层就该是三块");
    }

    #[test]
    fn coordinates_given_in_any_order_work() {
        let a = split_box((31, 320, 31), (0, -64, 0));
        let b = split_box((0, -64, 0), (31, 320, 31));
        assert_eq!(a, b, "角的顺序不该影响结果");
    }

    #[test]
    fn an_empty_box_produces_nothing() {
        assert_eq!(split_box((0, 0, 0), (0, 0, 0)).len(), 1);
    }
}
