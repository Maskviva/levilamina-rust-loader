use std::process::Command;

fn fnv1a(seed: u64, bytes: &[u8]) -> u64 {
    let mut h = seed;
    for &b in bytes {
        h ^= b as u64;
        h = h.wrapping_mul(0x0000_0100_0000_01b3);
    }
    h
}

fn main() {
    let rustc = std::env::var("RUSTC").unwrap_or_else(|_| "rustc".into());
    let version = Command::new(&rustc)
        .arg("-vV")
        .output()
        .ok()
        .and_then(|o| String::from_utf8(o.stdout).ok())
        .unwrap_or_else(|| {
            println!(
                "cargo:warning=levilamina: 拿不到 rustc 版本，Rust 高速公路将始终降级为 JSON 通道"
            );
            format!("unknown-{:?}", std::time::SystemTime::now())
        });

    let mut h: u64 = 0xcbf2_9ce4_8422_2325;
    h = fnv1a(h, version.as_bytes());
    for key in [
        "TARGET",
        "PROFILE",
        "OPT_LEVEL",
        "DEBUG",
        "CARGO_CFG_TARGET_POINTER_WIDTH",
        "CARGO_CFG_TARGET_ENDIAN",
        "CARGO_CFG_TARGET_ENV",
        // ── 下面几项是后补的。少了它们，指纹会对两个布局或行为其实不同的
        //    构建给出「相同」，而整条 lane 的安全前提就是「指纹相同 ⇒ 可以
        //    直接递函数指针」。
        //
        // 编译期 flag。-C target-cpu / -C target-feature 会改变 ABI，
        // -Z randomize-layout 直接把字段顺序打乱 —— 这些在其余因子全都相同
        // 的情况下也能让两边的 repr(Rust) 表布局不一致。
        "CARGO_ENCODED_RUSTFLAGS",
        // panic 策略。这条不是布局问题而是行为问题，也更隐蔽：
        // lane::guard() 靠 catch_unwind 兜住提供方表项里的 panic，可如果提供
        // 方是 panic=abort 编出来的，catch_unwind 根本没有机会运行，整个
        // 进程直接死。没有这一项，指纹会判定「匹配」，快车道照开。
        "CARGO_CFG_PANIC",
        // 这两项影响 repr(Rust) 的字段排布与对齐选择。
        "CARGO_CFG_TARGET_FEATURE",
        "CARGO_CFG_TARGET_ARCH",
    ] {
        h = fnv1a(h, key.as_bytes());
        h = fnv1a(h, std::env::var(key).unwrap_or_default().as_bytes());
    }

    h = fnv1a(h, env!("CARGO_PKG_VERSION").as_bytes());

    println!("cargo:rustc-env=LEVI_RS_TOOLCHAIN_FP={h}");
    println!("cargo:rerun-if-changed=build.rs");
    println!("cargo:rerun-if-env-changed=RUSTC");
}
