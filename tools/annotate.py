#!/usr/bin/env python3
"""Print one function from a ghidra_dump.py listing with readable call targets and strings.

Usage: python annotate.py DISASM.txt UNPACKED.EXE ADDRESS [--data-seg 2354]

GhidraMCP prints branch targets as linear addresses dressed up as segment 1000/2000/3000
(`CALLF 0x1000:801b` is linear 0x1801b). This script converts each target back to the
segment:offset of the function that contains it and appends that function's name. It also
shows the C string behind DS-relative pointer arguments (`PUSH DS` / `MOV AX,offset`).
Read-only: it only reads the dump and the EXE.
"""

import argparse
import bisect
import re
import struct


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("disasm")
    ap.add_argument("exe")
    ap.add_argument("address")
    ap.add_argument("--data-seg", default="2354", help="the game's data segment (Ghidra numbering)")
    args = ap.parse_args()

    funcs = {}  # linear start -> (seg:off, name)
    bodies = {}
    cur = None
    for line in open(args.disasm, encoding="utf-8"):
        line = line.rstrip("\n")
        if line.startswith("== "):
            name, addr = line[3:].split(" @ ")
            s, o = addr.split(":")
            funcs[int(s, 16) * 16 + int(o, 16)] = (addr, name)
            cur = addr
            bodies[cur] = []
        elif cur:
            bodies[cur].append(line)
    starts = sorted(funcs)

    with open(args.exe, "rb") as f:
        d = f.read()
    image = d[struct.unpack_from("<H", d, 8)[0] * 16:]
    ds_lin = (int(args.data_seg, 16) - 0x1000) * 16

    def string_at(off):
        p = ds_lin + off
        if p >= len(image):
            return None
        end = image.find(b"\0", p, p + 200)
        if end <= p:
            return None
        s = image[p:end]
        if all(32 <= c < 127 or c in (9, 10, 13) for c in s) and len(s) >= 2:
            return s.decode("latin-1").replace("\r", "\\r").replace("\n", "\\n")
        return None

    def target(m):
        lin = int(m.group(1), 16) * 16 + int(m.group(2), 16)
        i = bisect.bisect_right(starts, lin) - 1
        if i < 0:
            return m.group(0)
        addr, name = funcs[starts[i]]
        s = int(addr.split(":")[0], 16)
        real = f"{s:04x}:{lin - s * 16:04x}"
        return f"{real} <{name}>" if starts[i] == lin else f"{real} <{name}+{lin - starts[i]:x}>"

    addr = args.address.lower()
    if addr not in bodies:
        raise SystemExit(f"no function starts at {addr} in {args.disasm}")
    print(f"== {dict(funcs.values())[addr]} @ {addr}")
    prev = ""
    for line in bodies[addr]:
        text = re.sub(r"0x([123]000):([0-9a-f]{4})\b", target, line)
        m = re.search(r"MOV AX,0x([0-9a-f]+)$", line)
        if m and prev.endswith("PUSH DS"):
            s = string_at(int(m.group(1), 16))
            if s:
                text += f'    ; "{s}"'
        print(text)
        prev = line


if __name__ == "__main__":
    main()
