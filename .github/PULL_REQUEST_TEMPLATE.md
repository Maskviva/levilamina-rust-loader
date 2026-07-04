## What does this PR do?



## Which issues does this PR resolve?



## ABI impact

- [ ] No change to `LeviRsAbi.h` / `LeviRsApi` / `LeviRsModVTable`
- [ ] Additive change (new field/function appended at the end) — `struct_size` still gates it, `abi_version` unchanged
- [ ] Breaking change — `LEVI_RS_ABI_VERSION` bumped and called out below

If the ABI changed, confirm `src/LeviRsAbi.h` and
`crates/levilamina-sys/src/lib.rs` were updated in this same PR (see
[`docs/DESIGN.md`](../docs/DESIGN.md) §8).

## Checklist before merging

Thank you for your contribution to the repository.
Before submitting this PR, please make sure:

- [ ] `xmake` builds clean (no new errors or warnings)
- [ ] `cargo build --workspace` and `cargo test --workspace` pass
- [ ] `cargo fmt --all -- --check` and `cargo clippy --workspace --all-targets` are clean
- [ ] Your C++ follows the [LeviLamina C++ Style Guide](https://github.com/LiteLDev/LeviLamina/wiki/CPP-Style-Guide)
- [ ] You have tested the change against a running server
- [ ] You have not used code without a license, and added attribution for any third-party code
