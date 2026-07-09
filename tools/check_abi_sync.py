#!/usr/bin/env python3
"""check_abi_sync.py — three-way ABI consistency check.

Compares, in order:
  1. field order of the LeviRsApi struct in src/LeviRsAbi.h  (source of truth)
  2. the /* name */ initializer comments in src/bridge/ApiTable.cpp
  3. the `pub name:` fields of LeviRsApi in crates/levilamina-sys/src/lib.rs

A mismatch in any pair is an ABI break that the compilers cannot catch
(C++ positional aggregate init + Rust's independent mirror), so run this
before every commit that touches the ABI:

    python3 tools/check_abi_sync.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def header_fields() -> list[str]:
    text = (ROOT / "src/LeviRsAbi.h").read_text(encoding="utf-8")
    m = re.search(r"typedef struct LeviRsApi \{(.*?)\} LeviRsApi;", text, re.S)
    if not m:
        sys.exit("LeviRsAbi.h: cannot find `typedef struct LeviRsApi { … }`")
    body = m.group(1)
    # strip comments
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    body = re.sub(r"//[^\n]*", "", body)
    fields = []
    # data members: `uint32_t abi_version;` / `uint32_t struct_size;`
    for mm in re.finditer(r"^\s*uint32_t\s+(\w+);", body, re.M):
        fields.append(mm.group(1))
    # function pointers: `ret (*name)(args);`  (may span lines)
    for mm in re.finditer(r"\(\s*\*\s*(\w+)\s*\)\s*\(", body):
        fields.append(mm.group(1))
    return fields


def table_fields() -> list[str]:
    text = (ROOT / "src/bridge/ApiTable.cpp").read_text(encoding="utf-8")
    m = re.search(r"const LeviRsApi gApi\{(.*?)\};", text, re.S)
    if not m:
        sys.exit("ApiTable.cpp: cannot find `const LeviRsApi gApi{ … };`")
    fields = []
    for mm in re.finditer(r"/\*\s*([\w]+)\s*\*/", m.group(1)):
        fields.append(mm.group(1))
    return fields


def sys_fields() -> list[str]:
    text = (ROOT / "crates/levilamina-sys/src/lib.rs").read_text(encoding="utf-8")
    m = re.search(r"pub struct LeviRsApi \{(.*?)\n\}", text, re.S)
    if not m:
        sys.exit("levilamina-sys: cannot find `pub struct LeviRsApi { … }`")
    fields = []
    for mm in re.finditer(r"^\s*pub\s+(\w+)\s*:", m.group(1), re.M):
        fields.append(mm.group(1))
    # Rust escapes the `mod` keyword as mod_ — normalize back for comparison.
    return [f[:-1] if f.endswith("_") and f != "struct_size" else f for f in fields]


def diff(name_a: str, a: list[str], name_b: str, b: list[str]) -> bool:
    if a == b:
        print(f"OK   {name_a} == {name_b}  ({len(a)} fields)")
        return True
    print(f"FAIL {name_a} != {name_b}")
    for i in range(max(len(a), len(b))):
        fa = a[i] if i < len(a) else "<missing>"
        fb = b[i] if i < len(b) else "<missing>"
        marker = "   " if fa == fb else ">> "
        print(f"  {marker}{i:3d}  {fa:<28} | {fb}")
    return False


def main() -> int:
    hdr = header_fields()
    tbl = table_fields()
    rs = sys_fields()
    ok = diff("LeviRsAbi.h", hdr, "ApiTable.cpp", tbl)
    ok &= diff("LeviRsAbi.h", hdr, "levilamina-sys", rs)
    if ok:
        print(f"\nABI v-table in sync across all three definitions ({len(hdr)} fields).")
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
