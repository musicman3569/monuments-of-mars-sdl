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
- Genus **PCX Programmer's Toolkit 3.56** and **PCX Effects 1.04**, the same libraries
  Pharaoh's Tomb uses.

## Data files

| File        | Size   | Notes                                                   |
|-------------|--------|---------------------------------------------------------|
| MARSn.CF1   | 7,947  | PCX (v5, RLE, 2 bpp) screen; name is in the EXE         |
| MARSn.CF2   | 4,377  | PCX screen                                              |
| MARSn.IGS   | 16,600 / 16,700 | Unknown yet; the EXE opens it as `marsn.igs`  |
| MARSn.HS    |        | High scores: text lines of score + name, CR LF          |
| MARSn.SV1/2 |        | Saved game (SV2 holds the score as text)                |
| MARSn.KBD / .SPD |   | Probably keyboard and speed settings                    |
