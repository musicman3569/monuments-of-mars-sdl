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
  the bottom. DrawLevel doesn't draw the stars; `DrawStars` (1054:74bf, called by UpdatePlayer)
  draws them masked into back buffer 2 every frame, after the player, so they're on top.
- `DrawLevel` (1054:31f3) reads the map row-major (index 0..239). The first guess here, that
  rows were shifted by a byte, was wrong: rendered, level 1's hills simply run off both edges.
- Most bytes are drawn as `tile[b]`. These are **entities** instead (spawn points in lists
  of 3-byte records `type, byte col, row`), and are not drawn as tiles:
  04, 06, 46, 49, 4A, 4B, 55, 6E, and **65 = the player's start** (saved to 2354:05DC/05DE).
  55, 56-59 and 6F are entities *and* drawn.
- Bytes 09-46, 56-59, 6E, 70 and 74-77 also go into the **specials** list at 2354:C9A8
  (`code, col, row`, count [C92F]) with an active flag at 2354:CC00 (0 for the stars): what
  `FindSpecialTileAt` / `TouchTile` act on, and what AnimateTiles animates (34-46).
- 18 and 19 come in pairs on a row: 18 marks the left end, 19 the right end, stored as
  `width, col, row` at 2354:4947: the dripping ceilings.
- The game edits the level **in the level segment** (switches, levers, blocks, gems) and puts
  it back from its copy at DS:8809 when the level ends, restarts or the game ends.
- The byte at level segment + 0Ch + level is that level's **terminal action** (code 70); MARS1
  uses 2 on levels 11, 12 and 19, 3 on levels 13 and 15, 4 on level 20.
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

   Then, every frame: `CopyBackground`, `AnimateTiles`, `DrawBoulders`, `UpdateEnemies49`,
   `UpdateEnemies4A`, `UpdateEnemies4B`, `UpdateDrips`, clear [C92E], `UpdatePlayer` (which
   runs the platforms and the shot). If the game is over ([05D5]) -> `CheckHighScore` ->
   back to the menu loop. Otherwise `UpdateBoulders`, `UpdateTurret`, `PresentFrame`,
   `CheckLevelExit`, **wait for the timer ISR to set [CCD1]** (the frame clock), handle death
   ([05D6] -> `FlashBackground`, `RestartLevel`), `CheatLevelSkip`. A level change ([CCE9])
   jumps back to step 5.

   The order matters for collisions: everything that can block or kill the player is drawn
   into back buffer 2 before `UpdatePlayer` samples it. Scancode [C8AA] is never cleared, so
   a held key acts on every frame (F6 kills again on the restarted level while held).

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
| 8803/8806  | where the player died (RestartLevel draws the explosion there)          |
| 8809       | the current level map (240 bytes)                                      |
| C8AA       | last keyboard scancode                                                 |
| CCD1       | frame tick from the timer ISR                                          |
| CCD6       | far pointer to 0040:0017 (BIOS keyboard flags)                          |

There are no lives: dying restarts the level, with shots and score reset to what they were
when the level started.

### Cheats

- Main menu, **Caps Lock on + `>`**: start with 20 shots.
- In game, **Caps Lock on + PgDn / `**: next / previous level (`CheatLevelSkip`).

## Input

The keyboard ISR at **1054:0499** isn't a Ghidra function (auto-analysis missed it and
GhidraMCP can't create one); this is decoded from its bytes:

```
in al,60h ; [C8AA] = scancode
scancode == [C8AE]        -> [C8AD] = 1 (jump)    scancode|80h -> 0
scancode == [8A25]        -> [C92C] = 1 (fire)    release -> 0
scancode == [C8EC] or 4Bh -> [CCCE] = 1 (left)    release -> 0   (4Bh = Left arrow)
scancode == [C92D] or 4Dh -> [CCD2] = 1 (right)   release -> 0   (4Dh = Right arrow)
in al,61h / toggle bit 7 (keyboard ack) / EOI / iret
```

The four configurable keys come from `MARS1.KBD`: one letter naming a fixed setup.
`LoadKeyConfig` (1054:6c07, the first thing MainMenu does) reads it, or uses 'D' when there's
no file; `ConfigKeyboard` (1054:6c5e, the menu's C) lets the player pick one and saves it
(`SaveKeyConfig`). `ApplyKeyConfig` (1054:6a94) sets the scancodes:

| Setup | Left        | Right        | Jump          | Fire          |
|-------|-------------|--------------|---------------|---------------|
| A     | Ctrl (1D)   | Alt (38)     | R.Shift (36)  | Space (39)    |
| B     | Ctrl        | Alt          | Space         | R.Shift       |
| C     | Alt         | Ctrl         | L.Shift (2A)  | Space         |
| D     | Alt         | Ctrl         | Space         | L.Shift       |
| E     | Ins (52)    | Del (53)     | Alt           | Space         |
| F     | F9 (43)     | F10 (44)     | R.Shift       | Space         |
| G     | Left (4B)   | Right (4D)   | Ctrl          | Alt           |
| X     | Left        | Right        | Space         | L.Shift       |

The arrows always walk too. ApplyKeyConfig also accepts a lower-case 'a' (as A) but no other
lower-case letter; anything else leaves the keys alone (an empty KBD file gives EOF, so no
jump or fire key at all). The menu loop zeroes the four keys before MainMenu reloads them.

Switches, levers and terminals use **Enter** (scancode 1C) while standing on them.

## Player (UpdatePlayer, 1054:3d43)

Units: x is a byte column (4 px, 0..4C), y is a scanline pair (2 px, 0..54).

- **Walking**: on 2 of every 3 frames, left/right moves x by 1 (4 px), if the
  wall-ahead flag allows it. The first press in a new direction only turns. Frames: facing
  right 1A-1F, facing left 20-25 (the 6th is the jump pose). Footstep beeps on the ground.
- **Jump**: needs jump held, on the ground and not falling. Rise speed is `(9 - t) >> 1`
  rows per frame, where t counts up every 3 frames. Apex at t = 32. Hitting a ceiling
  starts the fall.
- **Gravity**: with nothing under the feet, y += v >> 1 with v counting up to 8 (max 4 rows,
  8 px, per frame).
- **Collision** samples back buffer 2 (the frame so far, enemies included) before drawing the
  player; `BlitMasked_Buf2_Collide` stores 8 flags (non-zero byte = solid):

  | Flag      | Byte offset from the sprite's top-left | Use                        |
  |-----------|----------------------------------------|----------------------------|
  | 492A/492B | row +5, col +0 / +3                    | wall left / right          |
  | 492C/492D | row +8, col +1 / +2                    | ground below the feet      |
  | 492E/492F | row +7, col +1 / +2                    | feet in the floor: step up |
  | 4930/4931 | row -1, col +1 / +2                    | ceiling                    |

  A blocking pixel only stops walking if `FindSpecialTileAt` finds no special tile there.
- **Shooting** (`UpdatePlayerShot`): fire spawns a shot (tile 0B) at the player's x (+2 if
  facing right) when none is in flight and shots > 0. It moves 2 bytes per frame. One shot
  costs one; there's no other way to get more except the menu cheat.
- **Death** ([05D6]) comes from enemies, drips, the turret bolt and deadly tiles; F6 does the
  same. `RestartLevel` puts back the shots and score from the level's start.
- The player's movement state ([05A8], [05E0]-[05F1]) is never reset: it starts from the data
  segment's initial values (facing right, frame 1A, [05EA] = 1E until the first turn) and
  carries over between levels.
- On the frame a jump starts, [C92E] = 5, which makes the walkers' and the drip's kill box
  13 rows tall instead of 8 on the next frame.

### Moving things

Each kind of thing has its own near-copy of BlitMasked_Buf2 that first samples a few bytes of
back buffer 2 into [075B]-[0760] (offsets are rows / bytes from the tile's top-left):

| Blitter (1054:)       | Samples                                    | Used by                     |
|-----------------------|--------------------------------------------|-----------------------------|
| BlitDrip 2092         | +8/+1, +8/+2 (under)                       | drip stops on anything      |
| BlitBoulder 21e8      | +8/0, +8/+3 (under); +4/-1, +4/+4 (sides)  | boulders                    |
| BlitVPlatform 2354    | -14/0 (above a rider); +11/+1, +11/+2      | turn at an FFh byte         |
| BlitEnemy49 24ba      | -1/-1, -1/+4 (ceiling); +1/-1, +1/+4       | 49: needs a ceiling         |
| BlitEnemy4A 262f      | +1/0, +1/+3; 0/-1, 0/+4                    | 4A                          |
| BlitEnemy4B 279a      | +8/0, +8/+3 (floor); 0/-1, 0/+4 (walls)    | 4B: needs a floor           |
| BlitHPlatform 2907    | +1/-1, +1/+4                               | turn at an FFh byte         |

A sample above the top of buffer 2 reads back buffer 1's last rows, which sit just before it.

- **Vertical platforms** (04, tile 04): a row every other frame, turning at an FFh byte (4
  pixels of colour 3) or row < 8. Lever 16 -> 17 saves their states at [CCDF] and stops them.
- **Horizontal platforms** (06, also drawn with tile 04): a byte every 3rd frame, turning at an
  FFh byte or the edge; they move the player standing on them (8 rows above, within 2 bytes)
  unless the player's wall sample says no.
- **Walkers**, state 1 left / 0 right / 2 dead, 4 animation frames:
  49 (tiles 49-4C) crawls along ceilings, a byte every 3rd frame, shot within 2 bytes x 4
  rows, 500 points; 4A (4D-50, the robot) hovers, a byte every 2nd frame, turns when its top
  row touches anything, 500; 4B (51-54) walks floors, 250 (4A and 4B are shot within 3
  bytes x 6 rows). Turning skips that frame's hit tests; a walker shot dead can still kill on
  the same frame. Touch: within 2 bytes x 7 rows (+5 on a jump's first frame) for 49 / 4A,
  within 1 byte x 7 rows for 4B.
- **Drips**: one at a time, from a random byte of a random 18-19 range (Turbo C `rand()`, never
  seeded), a row per frame until there's anything under it.
- **Turret** (46): when the player is within 6 rows, fires a bolt (tile 2A) towards them, a
  byte per frame, until back buffer 1 has something in its path (half a tile down), it reaches
  an edge, a boulder, or the player. Shooting its generator (6F) destroys it: +1500, the
  turret becomes tile 6C and stops.
- **Boulders** (6E): fall when nothing is under them, slide diagonally off an edge, and are
  pushed a byte when the sampled byte beside them looks like the player's sprite (0B, 02 or
  FF on the right; 80, E0 or FF on the left). They stop the shot and the bolt.
- **The shot** (tile 0B, 2 bytes per frame) samples back buffer 1 at row +4 in front of it;
  if anything is there it looks up the map cell: switches 0F-12 flip, levers flip when shot
  from the side they lean (16 from the left, 17 from the right), 6F destroys the turret; codes
  00, 04, 06, 09-0C, 0E, 1A-42, 44, 45, 47-5D, 5F-65, 6E (boulders stop it themselves) and
  70-77 let it through; everything else stops it.

## Map codes

| Code       | Tile(s)  | What                                                                      |
|------------|----------|---------------------------------------------------------------------------|
| 04         | 04       | vertical moving platform (`UpdateVPlatforms`); lever 16/17 stops and starts them |
| 06         | 04       | horizontal moving platform, carries the player (`UpdateHPlatforms`)      |
| 09 / 0A    | 09 / 0A  | force-field switch (Enter): 09 turns the 34 fields off, 0A on             |
| 0B         | -        | (the player's shot)                                                       |
| 0C         | 0C       | key card: +5000, "CARD!" on the status line ([05DA])                      |
| 0D         | 0D       | door: opens (tiles 2F-33) if the player has the card; solid otherwise    |
| 0E         | 0E       | +3 shots, +250                                                            |
| 0F-12      | 0F-12    | switches (Enter or a shot): 0F/12 turn the 37 fields off, 10/11 on; all flip |
| 16 / 17    | 16 / 17  | lever (Enter, or a shot): stop / restart the vertical platforms         |
| 18 .. 19   | -        | left / right end of a dripping ceiling (`UpdateDrips`, drip = tile 26)   |
| 34, 37     | 34-39    | force fields (animated), **deadly** while active                         |
| 3A         | 3A-3C    | gem: +1000                                                                |
| 3D         | 3D-3F    | spikes, **deadly**                                                        |
| 40         | 40-42    | hanging electric barrier, **deadly**                                      |
| 43         | 43-45    | energy rings, **deadly**                                                  |
| 46         | 46       | turret: shoots a bolt (tile 2A) at the player, **deadly** to touch       |
| 49         | 49-4C    | ceiling crawler; shot = +500                                              |
| 4A         | 4D-50    | hovering robot; shot = +500                                               |
| 4B         | 51-54    | floor walker; shot = +250                                                 |
| 55         | 55       | energy ring holding an astronaut (list at C8EF); terminal 4 frees them (tile 1A) |
| 56-59      | 56-59    | the letters **M A R S**: 2000 each; in order, +10000 and a fanfare (`CheckLetters`) |
| 5B -> 5C   | -        | stepping on 5B (used up) makes a block (tile 01) appear at each 5C (`ShowBlocks5C`) |
| 5D -> 5E   | - / 01   | stepping on 5D makes the 5E blocks vanish (`HideBlocks5E`)                |
| 5F         | -        | hidden gem: terminal action 3 turns it into a 3A (`RevealGems`)          |
| 64         | -        | level exit (invisible): +2000 + 100 x level                               |
| 65         | -        | player start                                                               |
| 6C         | 6C       | (the destroyed turret)                                                     |
| 6E         | 6E       | boulder (`UpdateBoulders`): falls, pushed, stops shots                     |
| 6F         | 6F       | the turret's generator ([C8AB]/[C8AC]): shoot it to destroy the turret   |
| 70         | 70       | computer terminal (Enter): 1 = ShowBlocks5C, 2 = HideBlocks5E, 3 = RevealGems, 4 = free the astronauts, rescue message, +50000 |
| 74-77      | 74-77    | stars (background, twinkle), drawn over everything                       |
| 81 / 82    | -        | "YOU HAVE DEACTIVATED THE ENERGY RINGS..." (`ShowRescueMessage`), unused in MARS1 |

Everything else is plain scenery: drawn, and solid if it has pixels where the player samples.

`FindSpecialTileAt` returns 0 ("plain wall") for boulders, 14, 15, a closed door and the
switches; the player can only walk into a pixel-solid spot when it returns another code.
`TouchTile` can run up to five times a frame (the air, both walls, ground, step-up checks),
so holding Enter on a lever or a 09/0A switch can flip it more than once.

Some levels start the player above a shaft with spikes at the bottom (8 and 13 in MARS1): an
idle player dies over and over; you have to steer right while falling.

### Still open

- Codes 14 / 15 (FindSpecialTileAt ignores them; they're common ground tiles).
- What 81 / 82 are for (no MARS1 level uses them).
- Whether the other volumes' rescue and ending texts have the same number of lines.

## Timing

The timer ISR at 1054:7aba (on INT 1Ch) is just `inc byte [CCD1]`, then it chains to the old
handler. The frame loop waits until [CCD1] >= 1 and clears it, so the game runs at **one frame
per BIOS tick, 18.2 Hz**, unless a frame takes longer than a tick.

## Text (DrawText, 1054:0028)

`DrawText(color, char far *s, row, col)` (Pascal-style, callee pops 10 bytes) sets the cursor
with INT 10h AH=02h and writes each character with AH=09h, BL=color, CX=1, after remapping:
`A`-`Z` -> 82h-9Bh (c + 41h), `1`-`9` -> AAh-B2h (c + 79h), `0` -> B3h, and space plus
`* . - ' / , ) ( $ : ! " = ?` unchanged. So letters and digits come from the game's own font
(INT 1Fh -> DS:0006; `SetHighFont(01A6)` selects a second set 34h characters later), and
punctuation from the BIOS ROM font. `DrawNumber(color, width, long n, row, col)` (1054:0316)
right-aligns: it starts at `col + width - (len + 1)`.

Status line: `LEVEL` at (0, 0) with the number at col 7, `SHOTS  ` at col 10 with the count at
col 17, `SCORE` at col 27 with the score ending at col 39 (all colour 3 labels, colour 1
numbers). The title screen adds `DrawText(1, "VOL. n - ...", 20, 9)` over CF1.
