#!/usr/bin/env python3
"""
Verify the three hand-synchronised ABI sites agree, in ORDER:

  1. src/LeviRsAbi.h            -- the C struct field declarations
  2. src/bridge/ApiTable.cpp    -- the positional initialiser
  3. crates/levilamina-sys/src/api.rs -- the Rust #[repr(C)] mirror

The table is positional: a field inserted in one place but not the others
makes Rust call a neighbouring function pointer with no diagnostic at all,
which is why this check exists.

Also enforces the conditional-block invariant: the md_* fields must be gated
on `not(feature = "client")` in Rust, NOT on `feature = "more_dimensions"`.
The C++ server build compiles the md block in unconditionally (xmake.lua:
`more_dims = not is_client`), so a Rust struct that omits it would misplace
every field that follows.

Usage: python3 tools/check_abi_sync.py [repo_root]
Exit code 0 = in sync.
"""
import re
import sys
from pathlib import Path

root = Path(sys.argv[1] if len(sys.argv) > 1 else '.')


def fail(msg):
    print(f'FAIL: {msg}')
    sys.exit(1)


# ---- 1. C header: function-pointer fields of LeviRsApi ----------------------
hdr = (root / 'src/LeviRsAbi.h').read_text(encoding='utf-8')
m = re.search(r'typedef struct LeviRsApi\b.*?\n\{(.*?)\n\} LeviRsApi;', hdr, re.S)
if not m:
    fail('could not locate the LeviRsApi struct body in LeviRsAbi.h')
body = m.group(1)

# strip comments so names inside prose don't register as fields
body_nc = re.sub(r'/\*.*?\*/', '', body, flags=re.S)
body_nc = re.sub(r'//[^\n]*', '', body_nc)

# `RET (*name)(args);` -- the only shape used for table slots.
# Skip nested `typedef RET (*Name)(...)` declarations (e.g. LLMoneyCallback at
# LeviRsAbi.h:701): they live inside the struct body but are types, not slots.
c_fields = []
for line in body_nc.splitlines():
    if 'typedef' in line:
        continue
    c_fields += re.findall(r'\(\s*\*\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)\s*\(', line)

# ---- 2. ApiTable.cpp: the positional initialiser ----------------------------
tbl = (root / 'src/bridge/ApiTable.cpp').read_text(encoding='utf-8')
# every `/* name */ value,` comment marks one slot, in order
t_fields = re.findall(r'/\*\s*([a-z_][a-z0-9_]*)\s*\*/', tbl)

# ---- 3. api.rs: the Rust mirror --------------------------------------------
rs = (root / 'crates/levilamina-sys/src/api.rs').read_text(encoding='utf-8')
m = re.search(r'pub struct LeviRsApi \{(.*?)\n\}', rs, re.S)
if not m:
    fail('could not locate `pub struct LeviRsApi` in api.rs')
rs_body = m.group(1)
rs_body_nc = re.sub(r'///[^\n]*', '', rs_body)
rs_body_nc = re.sub(r'//[^\n]*', '', rs_body_nc)
rs_fields = re.findall(r'pub\s+([a-z_][a-z0-9_]*)\s*:', rs_body_nc)

# the two scalars lead the struct in Rust but are not function pointers
for scalar in ('abi_version', 'struct_size'):
    if scalar in rs_fields:
        rs_fields.remove(scalar)
# ApiTable.cpp does not carry /* */ markers for the two scalars either
t_fields = [f for f in t_fields if f not in ('abi_version', 'struct_size')]

problems = []


def compare(a_name, a, b_name, b):
    if a == b:
        return
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            problems.append(
                f'{a_name} vs {b_name}: first divergence at slot {i}: '
                f'{a_name}={x!r} but {b_name}={y!r}')
            return
    longer, shorter = (a_name, b_name) if len(a) > len(b) else (b_name, a_name)
    extra = a[len(b):] if len(a) > len(b) else b[len(a):]
    problems.append(f'{longer} has {len(extra)} extra trailing slot(s) '
                    f'{shorter} lacks: {extra}')


compare('LeviRsAbi.h', c_fields, 'ApiTable.cpp', t_fields)
compare('LeviRsAbi.h', c_fields, 'api.rs', rs_fields)

# ---- 4. conditional-block invariant ----------------------------------------
for line_no, line in enumerate(rs_body.splitlines(), 1):
    if 'cfg(feature = "more_dimensions")' in line:
        problems.append(
            f'api.rs LeviRsApi body line {line_no}: md fields must be gated on '
            f'`cfg(not(feature = "client"))`, not `cfg(feature = '
            f'"more_dimensions")` -- the C++ server build always compiles the '
            f'md block in, so gating on the cargo feature truncates the struct '
            f'and misaligns every field after it.')

# ---- 5. nothing may follow the conditional blocks except the common tail ----
tail = body.split('#endif')[-1]
if 'Common additive tail' not in body:
    problems.append('LeviRsAbi.h: the "Common additive tail" marker is gone; '
                    'new slots have nowhere safe to go.')

if problems:
    for p in problems:
        print('FAIL:', p)
    sys.exit(1)

print(f'ABI in sync: {len(c_fields)} function-table slots across all 3 sites.')
print(f'  last 6 slots: {c_fields[-6:]}')
