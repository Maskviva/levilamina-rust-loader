use super::*;

#[cfg(test)]
mod tests {
    use super::*;

    struct A;
    #[repr(C)]
    #[derive(Clone, Copy)]
    struct TableA {
        f: unsafe extern "C" fn(LaneData) -> i32,
    }
    impl LaneContract for A {
        const NAME: &'static str = "test:a:v1";
        const VERSION: u32 = 1;
        type Table = TableA;
    }

    struct B;
    impl LaneContract for B {
        const NAME: &'static str = "test:b:v1";
        const VERSION: u32 = 1;
        type Table = TableA;
    }

    struct A2;
    impl LaneContract for A2 {
        const NAME: &'static str = "test:a:v1";
        const VERSION: u32 = 2;
        type Table = TableA;
    }

    #[test]
    fn fingerprint_is_stable_within_one_build() {
        assert_eq!(fingerprint::<A>(), fingerprint::<A>());
    }

    #[test]
    fn name_participates() {
        assert_ne!(fingerprint::<A>(), fingerprint::<B>());
    }

    #[test]
    fn version_participates() {
        assert_ne!(fingerprint::<A>(), fingerprint::<A2>());
    }

    #[test]
    fn fingerprint_is_never_the_reserved_zero() {
        assert_ne!(fingerprint::<A>(), 0);
        assert_ne!(fingerprint::<B>(), 0);
    }

    #[test]
    fn toolchain_was_filled_in_by_build_rs() {
        assert_ne!(TOOLCHAIN, 0);
    }

    #[test]
    fn lane_str_round_trips() {
        let owned = String::from("你好 lane");
        let v = LaneStr::new(&owned);
        assert_eq!(unsafe { v.as_str() }, "你好 lane");
        assert_eq!(unsafe { LaneStr::EMPTY.as_str() }, "");
    }

    #[test]
    fn lane_slice_round_trips() {
        let items = [1u32, 2, 3];
        let v = LaneSlice::new(&items);
        assert_eq!(unsafe { v.as_slice() }, &[1, 2, 3]);
        let empty: LaneSlice<u32> = LaneSlice::new(&[]);
        assert!(unsafe { empty.as_slice() }.is_empty());
    }
}
