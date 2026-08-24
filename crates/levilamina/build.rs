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
    ] {
        h = fnv1a(h, key.as_bytes());
        h = fnv1a(h, std::env::var(key).unwrap_or_default().as_bytes());
    }

    h = fnv1a(h, env!("CARGO_PKG_VERSION").as_bytes());

    println!("cargo:rustc-env=LEVI_RS_TOOLCHAIN_FP={h}");
    println!("cargo:rerun-if-changed=build.rs");
    println!("cargo:rerun-if-env-changed=RUSTC");
}
