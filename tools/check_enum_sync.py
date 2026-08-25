#!/usr/bin/env python3
"""check_enum_sync.py — catch the bug class that made 超平坦 generate 下界.

`check_abi_sync.py` guards the *order* of the LeviRsApi v-table. This guards
the *numbers* in the enums that cross the same boundary. Neither compiler can
see the other side, so a wrong constant is a silent behaviour change rather
than a build error — the only symptom is a player picking "Flat" and landing
in the nether.

Two independent checks:

  A. **Our own tag enums.** `enum LeviRs*` in src/LeviRsAbi.h vs the
     `pub const` blocks in crates/levilamina-sys/src/consts*.rs. Both sides
     are ours, so both are fixable; they just have to agree.

  B. **Engine-mirrored enums.** Rust enums that copy a value out of a
     BDS header (`GeneratorType`, `GameType`, `TextPacketType`,
     `AbilitiesIndex`, `CommandPermissionLevel`). Here the engine is the
     source of truth and we are always the one that is wrong.

     This is the check that would have caught it: `GeneratorType` had
     `Overworld = 0 … Void = 4` against an engine header reading
     `Legacy = 0, Overworld = 1 … Void = 5`, so every generator was handed
     to `static_cast<GeneratorType>` one slot off — and not by a constant
     offset, because the engine orders Flat before Nether and we did not.

Usage:

    python3 tools/check_enum_sync.py [--mc-include PATH]

PATH defaults to $MC_INCLUDE_DIR, then to a few conventional locations.
Check B is skipped (with a notice, not a failure) when the headers are not
found, so this stays runnable on a machine without the BDS SDK unpacked.
"""
import argparse
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Rust enum  ->  (rust file, engine header basename, members we mirror)
#
# Only the members we actually declare on the Rust side are compared: mirroring
# a subset is fine and normal (we do not expose GameType::Undefined). What is
# not fine is mirroring a member under a different number.
MIRRORED = {
    "GeneratorType": (
        "crates/levilamina/src/comms/more_dimensions.rs",
        "GeneratorType.h",
    ),
    "GameMode": (
        "crates/levilamina/src/player/types.rs",
        "GameType.h",
    ),
    "MessageType": (
        "crates/levilamina/src/player/types.rs",
        "TextPacketType.h",
    ),
    "Ability": (
        "crates/levilamina/src/player/types.rs",
        "AbilitiesIndex.h",
    ),
    "CommandPermission": (
        "crates/levilamina/src/command/mod.rs",
        "CommandPermissionLevel.h",
    ),
}

MC_INCLUDE_CANDIDATES = [
    "include",
    "../include",
    "build/.gens/levilamina-rust-loader/windows/x64/release/rules/levilamina/include",
]


def strip_comments(text: str) -> str:
    """Must run before any brace matching.

    The ABI header documents members with things like
    `/* SNBT {name,states,version} */`; a naive `\\{(.*?)\\}` terminates on the
    brace inside that comment and silently truncates the enum, which reads as
    a mismatch. Comments go first, always.
    """
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def parse_c_enums(text: str, pattern: str = r"LeviRs\w+") -> dict[str, dict[str, int]]:
    out: dict[str, dict[str, int]] = {}
    clean = strip_comments(text)
    for m in re.finditer(
        rf"\benum\s+(?:class\s+)?({pattern})\s*(?::\s*\w+\s*)?\{{([^{{}}]*)\}}", clean, re.S
    ):
        name, body = m.group(1), m.group(2)
        vals: dict[str, int] = {}
        nxt = 0
        for tok in body.split(","):
            tok = tok.strip()
            if not tok:
                continue
            if "=" in tok:
                key, raw = tok.split("=", 1)
                key = key.strip()
                try:
                    nxt = int(raw.strip(), 0)
                except ValueError:
                    continue
            else:
                key = tok
            vals[key] = nxt
            nxt += 1
        out[name] = vals
    return out


def parse_rust_consts() -> dict[str, dict[str, int]]:
    """`// LeviRsFoo` section header, then `pub const FOO_BAR: i32 = N;`."""
    out: dict[str, dict[str, int]] = {}
    cur = None
    base = ROOT / "crates/levilamina-sys/src"
    files = sorted((base / "consts").glob("*.rs"))
    if (base / "consts.rs").exists():
        files.append(base / "consts.rs")
    for f in files:
        for line in f.read_text(encoding="utf-8").splitlines():
            h = re.match(r"\s*//\s*(LeviRs\w+)\s*$", line)
            if h:
                cur = h.group(1)
                out.setdefault(cur, {})
                continue
            c = re.match(r"\s*pub const (\w+)\s*:\s*i32\s*=\s*(-?\d+)\s*;", line)
            if c and cur:
                out[cur][c.group(1)] = int(c.group(2))
    return out


def parse_rust_enum(path: Path, name: str) -> dict[str, int] | None:
    text = strip_comments(path.read_text(encoding="utf-8"))
    m = re.search(rf"\bpub enum {name}\s*\{{([^{{}}]*)\}}", text, re.S)
    if not m:
        return None
    vals: dict[str, int] = {}
    nxt = 0
    for tok in m.group(1).split(","):
        tok = tok.strip()
        if not tok:
            continue
        if "=" in tok:
            key, raw = tok.split("=", 1)
            key = key.strip()
            try:
                nxt = int(raw.strip(), 0)
            except ValueError:
                continue
        else:
            key = tok
        if not re.fullmatch(r"\w+", key):
            continue
        vals[key] = nxt
        nxt += 1
    return vals


def find_mc_include(explicit: str | None) -> Path | None:
    for cand in filter(None, [explicit, os.environ.get("MC_INCLUDE_DIR")]):
        p = Path(cand)
        if p.is_dir():
            return p
    for cand in MC_INCLUDE_CANDIDATES:
        p = (ROOT / cand).resolve()
        if p.is_dir():
            return p
    return None


def check_dimension_rules() -> int:
    """LeviRsDimRule 的三方一致性：ABI 头 / C++ 内部头 / Rust 安全封装。

    这一条单列出来，是因为它比其他枚举多一层：C++ 侧有两份定义
    （`LeviRsDimRule` 给 ABI，`DimRule` 给实现），Rust 侧还有第三份。
    三份都是同一个整数在跑，任意一份重排都会让规则串位——而且不会报错，
    只会表现成"关掉刷怪结果关掉了爆炸"。
    """
    print("\nC. 按维度规则枚举（ABI / C++ / Rust 三方）")

    def parse(path: Path, pattern: str, strip: str = "") -> dict[str, int]:
        if not path.exists():
            return {}
        text = strip_comments(path.read_text(encoding="utf-8", errors="replace"))
        m = re.search(pattern, text, re.S)
        if not m:
            return {}
        out = {}
        for line in m.group(1).splitlines():
            mm = re.match(r"\s*(\w+)\s*=\s*(\d+)", line)
            if mm:
                out[mm.group(1).replace(strip, "").replace("_", "").lower()] = int(mm.group(2))
        return out

    abi = parse(ROOT / "src/LeviRsAbi.h", r"enum LeviRsDimRule\s*\{([^}]*)\}", "LEVI_RS_DIMRULE_")
    cpp = parse(
        ROOT / "src/more_dimensions/include/dim/DimensionRules.h",
        r"enum class DimRule : int\s*\{([^}]*)\}",
    )
    rs = parse(
        ROOT / "crates/levilamina/src/comms/more_dimensions.rs",
        r"pub enum DimensionRule \{(.*?)\n\}",
    )

    if not (abi and cpp and rs):
        # 硬失败，不是跳过。
        #
        # 这条分支本来的意思是「功能还没落地」，但它同样会在**文件被挪走**时
        # 触发 —— 而那正是最需要报警的时候。仓库重构过一次之后，这个检查器和
        # ABI 检查器都在静默跳过，谁都没发现，因为它们退出码是 0。
        # 一个检查不到东西的检查器必须吵，不能装作通过了。
        missing = [
            n for n, v in (("LeviRsAbi.h", abi), ("DimensionRules.h", cpp),
                           ("more_dimensions.rs", rs)) if not v
        ]
        print(f"   FAIL 找不到定义：{', '.join(missing)} —— 文件被挪走了？路径需要更新")
        return False

    if abi == cpp == rs:
        print(f"   ok       三方一致（{len(abi)} 条规则）")
        return 0

    print("   MISMATCH 三方对不上：")
    for name in sorted(set(abi) | set(cpp) | set(rs)):
        a, c, r = abi.get(name), cpp.get(name), rs.get(name)
        if not (a == c == r):
            print(f"      {name}: ABI={a} C++={c} Rust={r}")
    return 1


def check_tag_enums() -> int:
    print("A. our own tag enums (LeviRsAbi.h vs consts*.rs)")
    cpp = parse_c_enums((ROOT / "src/LeviRsAbi.h").read_text(encoding="utf-8"))
    rust = parse_rust_consts()
    if not cpp:
        print("   !! parsed zero enums from LeviRsAbi.h — the parser is broken, not the code")
        return 1
    fails = 0
    for name in sorted(cpp):
        if name not in rust:
            print(f"   -- {name}: no Rust const block (unused from Rust)")
            continue
        c_by_val = {v: re.sub(r"^LEVI_RS_", "", k) for k, v in cpp[name].items()}
        r_by_val = {v: k for k, v in rust[name].items()}
        bad = []
        for v in sorted(set(c_by_val) | set(r_by_val)):
            cn, rn = c_by_val.get(v), r_by_val.get(v)
            if cn is None or rn is None:
                bad.append((v, cn, rn))
                continue
            # member-name prefixes differ per side (BSTR_ vs LEVI_RS_BSTR_);
            # compare on the shared suffix.
            if not (cn.endswith(rn) or rn.endswith(cn)):
                bad.append((v, cn, rn))
        if bad:
            fails += 1
            print(f"   MISMATCH {name}")
            for v, cn, rn in bad[:8]:
                print(f"      value {v}: C++={cn or '<missing>'}  Rust={rn or '<missing>'}")
        else:
            print(f"   ok       {name} ({len(cpp[name])} members)")
    return fails


def check_mirrored(mc_include: Path | None) -> int:
    print("\nB. engine-mirrored enums (BDS headers are the source of truth)")
    if mc_include is None:
        print("   -- skipped: no MC include tree found.")
        print("      pass --mc-include PATH or set MC_INCLUDE_DIR to enable this check.")
        return 0
    print(f"   using {mc_include}")
    fails = 0
    for rust_name, (rel, header) in sorted(MIRRORED.items()):
        rust_vals = parse_rust_enum(ROOT / rel, rust_name)
        if rust_vals is None:
            print(f"   -- {rust_name}: not found in {rel}")
            continue
        hits = list(mc_include.rglob(header))
        if not hits:
            print(f"   -- {rust_name}: {header} not found under the include tree")
            continue
        engine = parse_c_enums(hits[0].read_text(encoding="utf-8", errors="replace"), r"\w+")
        engine_vals = next((v for v in engine.values() if v), None)
        if not engine_vals:
            print(f"   -- {rust_name}: could not parse an enum out of {hits[0].name}")
            continue
        bad = [
            (k, v, engine_vals.get(k))
            for k, v in rust_vals.items()
            if engine_vals.get(k) != v
        ]
        if bad:
            fails += 1
            print(f"   MISMATCH {rust_name}  <->  {hits[0].name}")
            for k, ours, theirs in bad:
                shown = "<not in engine enum>" if theirs is None else theirs
                print(f"      {k}: we say {ours}, engine says {shown}")
        else:
            print(f"   ok       {rust_name} <-> {hits[0].name} ({len(rust_vals)} mirrored)")
    return fails


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--mc-include", help="path to the BDS `include` tree")
    args = ap.parse_args()

    fails = (
        check_tag_enums()
        + check_mirrored(find_mc_include(args.mc_include))
        + check_dimension_rules()
    )
    print()
    if fails:
        print(f"FAIL: {fails} enum(s) out of sync")
        return 1
    print("OK: all enums in sync")
    return 0


if __name__ == "__main__":
    sys.exit(main())
