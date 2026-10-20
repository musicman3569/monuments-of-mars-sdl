# Reverse engineering notes

Addresses are as Ghidra shows them for `unpacked/MARS1.EXE` (load base segment 1000).

## Executables

- `MARS1..4.EXE` (one per volume) are packed with **LZEXE 0.91**. `tools/unlzexe.py` unpacks them:

  ```
  python tools/unlzexe.py game/MARS1.EXE unpacked/MARS1.EXE
  ```

| EXE   | Packed | Unpacked | Relocations | Entry     | Stack     |
|-------|--------|----------|-------------|-----------|-----------|
| MARS1 | 42,433 | 147,076  | 1,253       | 0000:0000 | 22A9:00E6 |
| MARS2 | 43,160 | 148,308  | 1,258       | 0000:0000 | 22F6:00E6 |
| MARS3 | 43,208 | 148,308  | 1,258       | 0000:0000 | 22F6:00E6 |
| MARS4 | 43,384 | 148,804  | 1,258       | 0000:0000 | 2315:00E6 |

All four are nearly the same size, so they're likely one engine with different level data
built in (as in Pharaoh's Tomb).

## Compiler and libraries

- **Turbo C** (`Turbo-C - Copyright (c) 1988 Borland Intl.`), not Turbo Pascal like Pharaoh's
  Tomb. Expect C conventions: null-terminated strings, cdecl (caller pops the arguments), and
  the C0 startup code at the start of the code segment (hence entry 0000:0000).
- **Huge memory model**: every module has its own code segment *and* its own data segment, and
  each far function starts with `PUSH DS / MOV AX,seg / MOV DS,AX`. That prologue tells you
  which module a function belongs to:
  - `DS=2354`: the game (main module)
  - `DS=211d` (DGROUP): the Genus libraries
  - `DS=32xx`: a Turbo C runtime module
- Genus **PCX Programmer's Toolkit 3.56** and **PCX Effects 1.04**, the same libraries
  Pharaoh's Tomb uses.
- Function names live in `analysis/MARS1.names.txt`; apply them with `tools/ghidra_rename.py`.
  `tools/ghidra_dump.py analysis/MARS1.disasm.txt` dumps all disassembly for grepping.

### Segment map

From the call graph between segments and the DS each function loads.

| Segment     | Size   | What                                    | Evidence                                   |
|-------------|--------|-----------------------------------------|--------------------------------------------|
| 1000        | 1.3 KB | Turbo C C0 startup                      | entry; DGROUP 211d; calls main, then exit  |
| 1054        | 32 KB  | **game code** (82 functions found)      | main at 1054:7adb; DS=2354                 |
| 185b-1d2a   | ~20 KB | Genus PCX Toolkit + PCX Effects         | DS=211d; INT 10h, INT 67h (EMS)            |
| 1d4b-2117   | ~13 KB | Turbo C runtime                         | stdio, conio, dos.h; all call 1d4b (errno) |
| 211d        | 4 KB   | DGROUP (runtime and Genus data)         | Turbo C copyright at 211d:0004             |
| 2226        | 4.7 KB | **level data**, 20 x 240 bytes at 0025  | main copies level n from 2226:0025+n*0xF0  |
| 2354        | 60 KB  | game data segment (strings, buffers)    | `mars1.igs` at 2354:EE98                   |
| 3258-32a8   | small  | runtime modules' data, then the stack   | SS = 32a9                                  |

Ghidra's auto-analysis missed functions in many small segments; expect to create more by hand.

### Ghidra / GhidraMCP gotchas

- The disassembly text shows branch and call targets as **linear addresses** written as
  `1000:xxxx` / `2000:xxxx`. For example `CALLF 0x1000:801b` is linear 0x1801b = `1054:7adb`.
  Look up the real segment:offset in the bytes (`9a db 7a 54 10`) or convert from linear.
- `MOV Sreg,reg` prints with the operands swapped: `8e d8` (`MOV DS,AX`) shows as `MOV AX,DS`.
- `main` (1054:7adb) doesn't decompile (AddressOutOfBoundsException); smaller functions do.

## Data files

| File        | Size   | Notes                                                   |
|-------------|--------|---------------------------------------------------------|
| MARSn.CF1   | 7,947  | PCX (v5, RLE, 2 bpp) screen; name is in the EXE         |
| MARSn.CF2   | 4,377  | PCX screen                                              |
| MARSn.IGS   | 16,600 / 16,700 | Read whole by `LoadIGS` into 2354:0766 (fread of 0x413C = 16,700 bytes; MARS1's file is 100 bytes short). Contents unknown yet, probably graphics |
| MARSn.HS    |        | High scores: text lines of score + name, CR LF          |
| MARSn.SV1/2 |        | Saved game (SV2 holds the score as text)                |
| MARSn.KBD / .SPD |   | Probably keyboard and speed settings                    |

## Levels

- Built into the EXE, not loaded from a file: **20 levels x 240 bytes** at `2226:0025`
  (12 rows x 20 columns, one byte per tile). `main` copies the current one with
  `movmem(2226:0025 + level*0xF0, 2354:8809, 0xF0)`.
- Level 1 shows stars (74-77) in the top rows and hills and ground (79/7A, slopes 7B/7C) at
  the bottom.
- **Open question:** laid out at 20 bytes per row, each row's rightmost tile shows up in
  column 0 of the next row, as if the game indexes columns 1..20. Confirm when we find the
  code that draws the level.
