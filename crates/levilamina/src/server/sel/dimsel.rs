pub(crate) fn selector(dim: i32) -> Option<String> {
    match dim {
        0 => return Some("overworld".into()),
        1 => return Some("nether".into()),
        2 => return Some("the_end".into()),
        _ => {}
    }
    if dim < 0 {
        return None;
    }
    #[cfg(feature = "more_dimensions")]
    {
        crate::more_dimensions::list_dimensions()
            .into_iter()
            .find(|d| d.dim == dim)
            .map(|d| d.name)
    }
    #[cfg(not(feature = "more_dimensions"))]
    {
        None
    }
}

#[cfg(test)]
mod tests {
    use crate::server::dimsel::selector;

    #[test]
    fn vanilla_dimensions_have_fixed_selectors() {
        assert_eq!(selector(0).as_deref(), Some("overworld"));
        assert_eq!(selector(1).as_deref(), Some("nether"));
        assert_eq!(selector(2).as_deref(), Some("the_end"));
        assert_eq!(selector(-1), None);
    }
}
