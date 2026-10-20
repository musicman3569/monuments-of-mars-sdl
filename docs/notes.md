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
| MARSn.CF1   | 7,947  | PCX title screen ("by Todd Replogle"), palette 0        |
| MARSn.CF2   | 4,377  | PCX main menu, palette 1 bright                         |
| MARSn.IGS   | 16,600 / 16,700 | The tile and sprite set (see Graphics). `LoadIGS` freads 0x413C = 16,700 bytes into 2354:0766; MARS1's file is 100 bytes short |
| MARSn.HS    |        | High scores: text lines of score + name, CR LF          |
| MARSn.SV1/2 |        | Saved game (SV2 holds the score as text)                |
| MARSn.KBD / .SPD |   | Probably keyboard and speed settings                    |

## Levels

- Built into the EXE, not loaded from a file: **20 levels x 240 bytes** at `2226:0025`
  (12 rows x 20 columns, one byte per tile). `main` copies the current one with
  `movmem(2226:0025 + level*0xF0, 2354:8809, 0xF0)`.
- Level 1 shows stars (74-77) in the top rows and hills and ground (79/7A, slopes 7B/7C) at
  the bottom.
- `DrawLevel` (1054:31f3) reads the map row-major (index 0..239). The first guess here, that
  rows were shifted by a byte, was wrong: rendered, level 1's hills simply run off both edges.
- Most bytes are drawn as `tile[b]`. These are **entities** instead (spawn points in lists
  of 3-byte records `type, byte col, row`), and are not drawn as tiles:
  04, 06, 46, 49, 4A, 4B, 55, 6E, and **65 = the player's start** (saved to 2354:05DC/05DE).
  55, 56-59 and 6F are entities *and* drawn.
- Bytes 09-46, 56-59, 6E, 70 and 74-77 also go into a list at 2354:C9A8 (`type, col, row`)
  with a flag at 2354:CC00 (0 for the stars 74-78): probably the cells that animate or can
  be collected or destroyed.
- 18 and 19 come in pairs on a row: 18 marks the left end, 19 the right end, stored as
  `width, col, row` at 2354:4947. Probably patrol or platform ranges.
- In the other volumes the level segment is 2241 (MARS2, MARS3) and 224D (MARS4); the tool
  finds it by matching DrawLevel's `mov ax,seg / mov es,ax / mov al,es:[bx+25h]`.

## Graphics

- **CGA 320x200, 4 colours.** Gameplay uses **palette 0, low intensity** (black / green / red /
  brown): `SetVideoMode` (1054:0a36) does INT 10h AH=0 then AH=0Bh BH=0 BL=0 (black
  background, no intensity) and BH=1 BL=0 (palette 0). `SetVideoModePal1Bright` (1054:0aa7)
  picks palette 1 bright, for the main menu.
- PCX palettes match the ZSoft rule for byte 19 (bit 6 = palette, bit 5 = intensity):
  CF1 = 0x00 (palette 0), CF2 = 0xF0 (palette 1 bright). This also matches Pharaoh's Tomb's
  0x40 = palette 1.
- **Tiles (.IGS):** 129 tiles of 16x16 (MARS1), 128 bytes each; tile n is at IGS + n*0x80.
  The first 64 bytes are the 8 even scanlines and the next 64 the 8 odd ones, matching CGA's
  two banks. Each scanline is 8 bytes, alternating `mask, pixels` (4 of each, 2 bpp, MSB
  leftmost). Mask bits 11 = see-through. The same tile set holds the terrain, the player's
  frames, enemies and the "M A R S" letters, so there is no separate sprite format.
- **Blitters** all take `(tile far *, row, byte column)`, where row counts scanline pairs
  (0..0x5F step 8) and byte column is 4 bytes per tile (0..0x4F):
  - opaque (copies pixels, ignores the mask): `BlitTile_Buf1` -> back buffer 2354:4983,
    `BlitTile_Buf2` -> 2354:8A26, `BlitTile_Screen` -> B800 + 0x140
  - masked: `dst = (dst & mask) | pixels`: `BlitMasked_Buf2` and many near-copies after it
    (1054:1d7d, 2092, 21e8, ...)
- Both back buffers are 16,000-byte copies of CGA memory (bank 0 at +0, bank 1 at +0x2000).
- **Collision reads the framebuffer**, as in Pharaoh's Tomb: `BlitMasked_Buf2_Collide`
  samples 8 bytes of the buffer around the sprite into flags (2354:492A-4931) before drawing.
  The port needs a real 2 bpp buffer that game logic can read back.
- `tools/extract_gfx.py` writes everything to `gfx/` (not committed):
  `tiles game/MARS1.IGS gfx/mars1`, `levels unpacked/MARS1.EXE game/MARS1.IGS gfx/mars1`
  (also a .txt hex map per level; entity cells outlined in white), and
  `pcx game/MARS1.CF1 out.png` (palette from the header unless `--palette` is given).

## Program flow

`main` (1054:7adb) doesn't decompile, so read it with
`python tools/annotate.py analysis/MARS1.disasm.txt unpacked/MARS1.EXE 1054:7adb`, which
turns GhidraMCP's linear branch targets back into named `seg:off` and shows string arguments.

1. Startup: `nosound`; remember the BIOS keyboard flags (0040:0017) and clear Caps Lock;
   `LoadIGS("mars1.igs")`; `SetHighFont(0006)`; `SetVideoMode(4)`.
2. `TitleScreen`: `LoadHighScores`, CF1 with a Genus effect, "VOL. 1 - FIRST CONTACT", wait
   for a key. If `MARS1.SPD` doesn't exist, show `ShowSpecialNotice` and create it (it stays
   empty; only its existence matters).
3. Save INT 9 and INT 1Ch; install the timer ISR (1054:7aba) on INT 1Ch and a `ctrlbrk`
   handler (1054:7953).
4. **Menu loop** (1054:7b77): reset state, `SetVideoModePal1Bright(4)`, put back the BIOS
   INT 9, `MainMenu`, then install the game's keyboard ISR (1054:0499) on INT 9.
5. **Level start** (1054:7c1a): `SetVideoMode(4)`, draw "LEVEL" / "SCORE" status line, copy
   the level map to 2354:8809, `DrawNumber(level+1)`, `DrawLevel`, `DrawShots`.
6. **Frame loop** (1054:7c9a). The keyboard ISR leaves the last scancode in `[2354:C8AA]`:

   | Scancode | Key    | Action                                                  |
   |----------|--------|---------------------------------------------------------|
   | 10 / 01  | Q / Esc| "PAUSED / QUIT (Y/N)"; Y ends the game                  |
   | 1F       | S      | "SAVING.. ARE YOU SURE (Y/N)" -> `SaveGame`             |
   | 13       | R      | "RESTORING. ARE YOU SURE" -> `RestoreGame`, redraw level|
   | 3B       | F1     | sound on/off ([05D7]) with a banner                     |
   | 3F       | F5     | pause until F5 again                                    |
   | 40       | F6     | give up: back to the level start (as if killed)         |

   Then, every frame: `CopyBackground`, `AnimateTiles`, `DrawObjects6E`, `UpdateEnemies49`,
   `UpdateEnemies4A`, `UpdateEnemies4B`, `UpdatePatrollers`, `UpdatePlayer`. If the game is
   over ([05D5]) -> `CheckHighScore` -> back to the menu loop. Otherwise
   `UpdateObjects6E`, `UpdateEntity46`, `PresentFrame`, `CheckLevelExit`, **wait for the
   timer ISR to set [CCD1]** (the frame clock), handle death ([05D6] -> `FlashBackground`,
   `RestartLevel`), `CheatLevelSkip`. A level change ([CCE9]) jumps back to step 5.

### Game state (DS 2354)

| Address    | What                                                                   |
|------------|------------------------------------------------------------------------|
| 05D5       | game over / finished flag                                              |
| 05D6       | player died (enemies set it; F6 too)                                   |
| 05D7       | sound on                                                               |
| 05D8       | shots (5 at the start; 0756 keeps the count from the level's start)     |
| 05DB       | level, 0-based (20 levels)                                             |
| 05DC/05DE  | player x (byte column) / y (scanline pair)                             |
| 4871 (long)| score; 0757/0759 = score at the level's start                          |
| 8803/8806  | respawn position                                                       |
| 8809       | the current level map (240 bytes)                                      |
| C8AA       | last keyboard scancode                                                 |
| CCD1       | frame tick from the timer ISR                                          |
| CCD6       | far pointer to 0040:0017 (BIOS keyboard flags)                          |

There are no lives: dying restarts the level, with shots and score reset to what they were
when the level started.

### Cheats

- Main menu, **Caps Lock on + `>`**: start with 20 shots.
- In game, **Caps Lock on + PgDn / `**: next / previous level (`CheatLevelSkip`).
