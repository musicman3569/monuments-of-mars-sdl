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
| MARSn.HS    |        | High scores: 10 lines of `<score><name>` (decimal then the name, no separator), CR LF |
| MARSn.SV1   | 2      | Saved game: the level (0-based) and its starting shots, as bytes |
| MARSn.SV2   |        | The level's starting score as text, `%lu` CR LF         |
| MARSn.KBD   | 1      | The key setup letter (see Input)                         |
| MARSn.SPD   | 0      | Empty: only says the special notice has been shown      |

High scores: `LoadHighScores` (1054:7526) reads ten times `fscanf("%lu")`, `fscanf("%[^\n]s")`.
An empty name doesn't match, so that entry keeps its old name (empty at startup). Without a
file it uses a built-in table (the same in all four volumes): Roger Wilco 110250, Commander
Keen 84900, Todd Replogle 67350, Scott Miller 46500, Capt. Comic 27400, George Broussard 150,
then four zeros. `CheckHighScore` inserts a score above the first lower entry; its shift writes
an eleventh entry past both tables (harmless). Names are typed with `InputName`: up to 20 of
A-Z, 0-9, space . ! ? (upper-cased); an extended key's scancode is typed as a letter (the
arrows give H K M P).

Saves keep the values from the **start** of the level, so restoring restarts that level.
`RestoreGame` does nothing without SV1 (and would read a NULL file without SV2).

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
   for a key, clear the screen. If `MARS1.SPD` doesn't exist, create it (it stays empty; only
   its existence matters) and show `ShowSpecialNotice` (drawn line by line, stopping early at
   a key press; then it waits for a key without taking it).
3. Save INT 9 and INT 1Ch; install the timer ISR (1054:7aba) on INT 1Ch and a `ctrlbrk`
   handler (1054:7953).
4. **Menu loop** (1054:7b77): reset state, `SetVideoModePal1Bright(4)`, put back the BIOS
   INT 9, `MainMenu`, then install the game's keyboard ISR (1054:0499) on INT 9.

   `MainMenu` (1054:6eb8) first calls `LoadKeyConfig`, then loops: palette 1 bright, CF2 with a
   random Genus effect, flush the keyboard, `getch`:
   J / Enter / Space play (5 shots); S `ShowStory`, I `ShowInstructions` (four pages, then the
   special notice), O `ShowOrderingInfo`, C `ConfigKeyboard`, H `ShowHighScores` with the
   player walking on both sides until a key; Caps Lock + `>` plays with 20 shots; Esc / Q:
   `ShowOrderingInfo`, then text mode 3, the BIOS keyboard flags put back, a shareware notice
   drawn with `DrawStringTextMode` (each line revealed over 10 passes of random colours), the
   timer vector restored, `exit`. [C92B] is set to 5 / 10 on starting but never read.
   If CF2 (or CF1, in TitleScreen) can't be loaded, it prints an "incomplete version" order
   form in text mode and exits.

   S and R in game draw an "ARE YOU SURE (Y/N)" box, then `printf("?")`: DOS's teletype output
   puts the question mark at the BIOS cursor, right after "(Y/N) ". R answered N skips the rest
   of that frame.
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

There is no joystick support in the original. The port's game-controller support
(`src/gamepad.h`) works by pressing keys: scancodes through the ISR above in game, so it
behaves exactly like the keyboard, including a held button's code staying in [C8AA].

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
| 14 / 15    | 14 / 15  | the Mars surface: the top row of the outdoor levels' ground, alternating along it (see below) |
| 80         | 80       | the stone face's pedestal (MARS4 level 20), plain scenery                 |
| 81 / 82    | 81 / -   | entered (`CheckLevelExit`): MARS1 shows the rescue message; MARS2-4 the **face etched in stone** (see below). Only MARS4 level 20 uses one (81, the face on its pedestal); 82 is never used |

Everything else is plain scenery: drawn, and solid if it has pixels where the player samples.

`FindSpecialTileAt` returns 0 ("plain wall") for boulders, 14, 15, a closed door and the
switches; the player can only walk into a pixel-solid spot when it returns another code.

**Why 14 and 15 answer 0.** DrawLevel lists every byte from 09 to 46 as a special with a plain
range check, and the surface tiles 14 / 15 fall inside it (MARS1 levels 1-3 and 20, a few
levels of each other volume). Whatever FindSpecialTileAt answers other than 0 is something
the player may be inside: the wall tests let them walk on into it, and the step-up (feet in
the floor) only lifts them out when it answers 0. Without the exception they sink into the
surface: a port build without it (a throwaway, never committed) changed 26 of the 240 classic
regression runs, nearly all with the player 1-3 rows lower, standing in the ground up to the
head on MARS1 level 1.

**Code 81: the face etched in stone (MARS2-4).** CheckLevelExit tests 81 and 82 in every
volume. MARS1 calls ShowRescueMessage for them, on every frame the player is in the cell.
MARS2-4 call a routine of their own instead (image 0491D in MARS2, 04987 in MARS4), once per
level: a flag at DS:05A6, set there and cleared by DrawLevel. It shows, in the alternate font,
"YOU HAVE DISCOVERED AND UNUSUAL STONE / ETCHED IN THE SHAPE OF A HUMAN FACE!" and the
decision to bring it back to NASA (rows 1-23, colours 1 / 2 / 3 by paragraph; MARS4 also
draws the blank rows, in colour 0, and fixes "SIMULAR" and "AN ALIEN RACE"), then "PRESS ANY
KEY TO CONTINUE." on row 24, delay(200), waits for a key and goes back to the normal font.
MARS4 then erases the stone: DrawLevel records the last 81's byte column and row at
DS:8A27 / 8A28 as it draws, and the handler draws tile 0 there in back buffer 1
(BlitTile_Buf1, its call at image 01E3E, MARS1's 1054:18a4 moved 5Ah like MARS4's other
blitters), so the player has taken it. MARS2 and MARS3 have the routine and the text but no
level with an 81. In MARS4 it is the last thing on level 20: the face sits on its pedestal
(80) on the hilltop between the start and the exit.
`TouchTile` can run up to five times a frame (the air, both walls, ground, step-up checks),
so holding Enter on a lever or a 09/0A switch can flip it more than once.

Some levels start the player above a shaft with spikes at the bottom (8 and 13 in MARS1): an
idle player dies over and over; you have to steer right while falling.

The text screens (story, instructions, ordering) are straight-line code: DrawText with
constant strings, SetHighFont, ClearScreen, SetVideoMode, BlitTile_Screen, a call to
ShowSpecialNotice, `getch`. Their text and layout differ per volume. The early helpers are at
the same offsets in every volume's code segment (SetHighFont 0001, DrawText 0028, ClearScreen
0471, SetVideoMode 0A36 / 0AA7); BlitTile_Screen moves (1B72, 1BCC in MARS4).

### Genus PCX Effects (the title and menu transitions)

The library has three settings: the effect type (`SetType`, 0-9), a granularity (1-64, default
8) and a delay between steps (0-500, default 0). `ShowPcx(variant, x, y, image)` (1c09:0006)
sets up locals in its own stack frame (the picture's size and edges, half sizes, the row
granularity, the column granularity rounded down to whole bytes, the delay, the video page),
then jumps to the type's routine, which uses that frame. Every step copies a rectangle of the
picture with `PutImageRect`, then calls `Delay(delay)`. The delay unit is `CalibrateDelay`'s
loop count for 1/100 of 18 ticks (about 9.9 ms); before calibrating it's a fixed 500 loops.

- **TitleScreen**: granularity 16, calibrate, type 9, variant 0, delay 0 (so only drawing time
  paces it).
- **MainMenu**: granularity 8, delay 1; each time `rand() % 5` picks a type: 6, 1, 3, 7 or 0;
  then `rand() % 4` (`% 2` for type 1) the variant: type 6 / 3 / 0: 4, 8, 1, 2; type 1: 1, 2;
  type 7: 6, 10, 5, 9. That's Turbo C's `rand()`, the same sequence as the drips.

| Type | Routine | What it does                                                               |
|------|---------|-----------------------------------------------------------------------------|
| 0    | 1c34:000a | strips one after another: 8 top down, 4 bottom up, 2 left to right, 1 right to left |
| 1    | 1c66:0002 | from the middle outwards: 2 sideways, otherwise up and down              |
| 3    | 1c4d:000e | the picture slides in: 8 from the top, 4 from the bottom, 2 from the left, 1 from the right |
| 6    | 1ca5:000e | column and row strips together: a cross growing from the middle; 2 closes in from the edges |
| 7    | 1cd0:0080 | square cells, one diagonal per step from a corner: 10 top left, 5 bottom right, 6 bottom left, 9 top right |
| 9    | 1d05:0000 | square cells in random order (the library's own RNG, seeded from the clock); the cell size doubles until there are fewer than 2048 cells |

`PutImageRect` for CGA (1a0d:036e) works in whole bytes: the source and destination columns are
`x / 4` and the width `(x2 - x1 + 1) / 4`, rounded down, with no clipping. That's why a strip
asked for as g + 1 pixels wide is g wide, and why type 3 variant 1's last step, which asks for
x1 = 1, still lands on column 0. The library could abort on Esc (if [0F44] is set), but the
game never turns that on.

### The volumes' texts

MARS2 and MARS3 have MARS1's rescue message and ending, line for line. MARS4 has its own,
which the port follows (`show_rescue_message`, `show_ending`) (read with a throwaway decoder of the routines' SetHighFont / DrawText calls, which gives
MARS1's exactly as the port draws them):

- **Rescue message** (ShowRescueMessage, image 01058 in both): MARS1 draws 20 rows, row 1
  blank, the text from row 2, "PRESS ANY KEY TO CONTINUE." on row 19; colours 1 / 3 / 1 / 3
  by paragraph, blank rows in colour 8. MARS4's text is shorter ("THESE ARE THE FINAL
  MISSING ASTRONAUT EXPLORERS...", "YOUR MISSION IS ALMOST COMPLETE...") but it draws 24
  rows: the text on rows 2-14 (colours 1 / 3 / 2 / 3), blank rows 15-23, the key prompt on
  row 24.
- **Ending** (image 0122C in MARS4, 011E4 in MARS1): MARS4's is "A SUCCESSFUL MISSION!"
  (row 1, col 9, colour 3, normal font), then in the alternate font: rows 3-4 colour 3,
  "*** CONGRATULATIONS ***" row 6 col 8 colour 1, rows 8-9 colour 2, the four volumes rows
  10-13 col 6 colour 1, rows 15-18 colour 2, "BE SURE AND PLAY / THE THOR TRILOGY" rows
  20-21 col 12 colour 1, rows 22-23 cols 3 / 5 colour 3, and the key prompt row 24 in the
  normal font, colour 2. There is no "YOUR JOURNEY CONTINUES" in MARS4.

### Still open

- Effect types 2, 4, 5 and 8 of the Genus library (the game doesn't use them).

## Timing

The timer ISR at 1054:7aba (on INT 1Ch) is just `inc byte [CCD1]`, then it chains to the old
handler. The frame loop waits until [CCD1] >= 1 and clears it, so the game runs at **one frame
per BIOS tick, 18.2 Hz**, unless a frame takes longer than a tick.

That, with x in whole bytes, is why movement looks rough: the player moves 4 px on 2 frames
of 3 (0, 4, 8, 8, 12, 16, 16 ...: 48 px/s in uneven hops), the robots 4 px on 1 frame of 2,
the ceiling crawlers and horizontal platforms 4 px on 1 of 3, the vertical platforms 2 px on 1
of 2. UpdatePlayer also skips walking *and* jumping on the third frame (`step_ctr`), so a jump
pressed then waits a frame.

## The display layer (port enhancement, display.smooth)

Only the picture changes; the game, its buffers and its collision samples run exactly as in
classic. `src/smooth.h` has the details.

**What's moving.** A frame is built in back buffer 2: CopyBackground (buffer 1, the level),
opaque blits (erasing a boulder or a pickup), masked blits (everything that moves, the stars,
the enemy explosion), PresentFrame. gfx.c reports each step, so smooth.c keeps buffer 2 without
its masked tiles (the background) and the masked tiles in order. An opaque blit drawn after a
masked tile replaces some of its bytes (the next boulder's erase clips the one just moved
next to it; a pickup erased beside the player clips the player); those bytes are recorded per
tile (`cut`) and left out when it's drawn, and a tile cut entirely is dropped. The address
arithmetic is the game's, so a tile at column 4Dh-4Fh runs on into the next row as it does
there. Check: in `--script` runs, the background with every tile drawn at the game's position
is compared with buffer 2 after every frame (`smooth_check`; a mismatch is printed). 240 runs
(3 scripts x 20 levels x 4 volumes) match on every frame.

**Following the game.** game.c tags the blits of things that move (`smooth_id`: player 1,
vertical platforms 100h+i, horizontal 200h+i, walkers 300h + kind * 40h + i, drip 400h, bolt
401h, shot 402h, boulders 500h+i). The window shows each at a position that chases the game's:
at its average speed over the last 6 frames (the cadences above are 2 or 3 frames long, so 6
covers both), plus 2 px per frame for every px it is further behind than 2 frames of that
speed. A simulation of the cadences picked this over plain interpolation, which still stops
between hops, and over smoothing the samples, which lags 2-3 frames: walking shows at 2.7-3.6
px per frame (the game's average is 2.67), about one frame (55 ms) behind, never past where
the game has it. Moves of more than 16 px across or 24 down jump, as do level starts, restarts
and restores (`smooth_reset`), and a thing not drawn in a frame is forgotten (a new drip or
shot starts where it is).

**Showing it.** While the playfield in video memory is what PresentFrame left there, the window
draws the background (with the status line from video memory) and the tiles as 16x16 textures
at fractional positions, clipped to the playfield, at the monitor's refresh rate (at most
240 Hz). Vsync is off, as in ptomb-port: waiting for it inside the game's own short delay()s
(the sound effects run within a frame) would stretch them and slow the game; on Windows the
compositor keeps the window tear-free. Anything else drawing on the playfield (banners, the
door, the death explosion, the rescue text) shows video memory as it is until the next frame.

## Quick taps (port enhancement, input.quick_taps)

The frame reads the ISR's held flags and [C8AA] as it starts, so in classic a key pressed and
let go within one frame (55 ms) is missed. With quick_taps, `plat_frame_input_begin` makes a
jump, fire, left or right key that was pressed since the last frame and is no longer held count
as held for the frame (jump for 2 frames, since UpdatePlayer ignores it on every third; fire for
1, so a shot that hits a wall at once can't be followed by a second), and Enter's make code
stand in [C8AA] for it; `plat_frame_input_end` takes them back unless the key changed meanwhile.
Only Enter of the scancode keys: pause (F5) and the Y/N questions wait for [C8AA] to change,
which a made-up press would confuse.

## Modern physics (port enhancement, game.physics = modern)

The `modern` preset rewrites how things move, not what the levels or rules are. Everything
that moves keeps a float position in pixels (y in back-buffer scanlines) and moves at its
original average speed, in M_HZ = 120 fixed steps a second (`run_level_modern`, the "modern
physics" section of game.c). Drawing is separate: whenever the display can show a picture,
`m_render` draws every thing between its last two positions with a pixel-exact masked blit
(`blit_masked_px`) into a copy of back buffer 1, and puts the playfield into video memory.
So the window gets a new picture at the monitor's rate while the game runs at 120 steps
whatever the display does, and a slow device draws less often without the game changing.

**Speeds** (px per original frame, a step being 18.2 / 120 = 0.15 of one): walking 8/3,
vertical platforms 1, horizontal 4/3, the crawler (49) 4/3, robots and floor walkers (4A,
4B) 2, drips 2, the bolt 4, the shot 8, boulders falling 2 (sliding off an edge, 4 across).
The jump keeps the original's two parts as straight lines through its steps: gravity
`min(4, t / 2)` rows per frame, t counting frames in the air (FallV >> 1), and the jump's
push `13/3 - t / 6` (the (9 - JumpT) >> 1 steps, JumpT counting every 3 frames). That rises
14 rows (28 px), the original's height, in the same time: a scripted standing jump lands on
the same rows frame for frame, and a running one at the same places.

**Collision** keeps the original's test points, in pixels and at every step: walls are the
column 3 px in from either side of the sprite (where its outer bytes end), over scanlines
4-10; the ground the scanline under the feet (x+4..x+11); feet in the floor scanlines
11-15 (stepped up at once, up to 5 px a step, which is how slopes and a ledge reached at a
jump's top are climbed); the ceiling the scanline above the head. A pixel that isn't black is
solid, plus the platforms' and boulders' own pixels. Falls and rises move a scanline at a time
and stop at the ground or the ceiling, so landings are exact (the original sinks up to 3 rows
into the floor, then climbs out a row a frame). Walking down a slope or a small step keeps
to it rather than falling. The lowest the player goes is row 58h, not the original's clamp of
54h: the original clamps before its gravity, which carries it up to 4 rows further, and exits
at the bottom of an open shaft (level 4) sit in the map's last row, reached only at 58h.

Kept from the original, because the levels depend on it: boulders and drips test only the
even scanline under them, as the original's 2-row steps do; tested on every scanline, the
level 14 boulders catch on the letters they should fall past and fail to cover the spikes.
The vertical platforms likewise test their turning marks at their row's even scanline, and
the marks are looked for in buffer 2 as the original has it then: the level with the boulders
drawn over it, so a boulder's solid top turns a platform (the level 13 platform over the stack
of boulders). Tested on every scanline, the other level 13 platform turned 12 px early on the
odd scanlines of the ledge's underside.
The touching rules (FindSpecialTileAt, TouchTile), scores, the turret, the shot's map-cell
rules (`shot_hit`) and the level exits are the original's code, fed with positions rounded
to its byte columns and rows. So is the pose: the jump picture (1F / 25) is set every step in
the air, and every step, keys or not, a picture past the walk's last (frame_end - 1) goes
back to standing (frame_first), which is how a landing shows standing again. Unlike the
original, a level starts with frame_end set for the way the player faces: the original's is
1E until the first turn, so it walks on three pictures until then, and modern's walk (four
pictures, by distance) would show 1B for two of them.

**Pickups don't get in the way** (the card 0C, +3 shots 0E, gems 3A, the letters 56-59, the
hidden gem 5F): the player's own tests (ground, head, walls, step-up; `W_BODY`) skip pixels
inside a pickup still in the level, and a jump carries on through them (the original ends the
jump on touching one, and its pixels are a ceiling, a wall or a floor like any other).
Everything else (boulders, enemies, drips, the shot) still sees them as the original does.
With their pixels open a fall can't land on one to take it, so `m_take_pickups` takes any the
player overlaps every step, by FindSpecialTileAt's reach. Seen in the regression runs: a
running jump on MARS1 level 12 now rises its full 14 rows and takes two letters (the original
stops at the first), and the level 13 shaft's +3 shots is taken on the way down instead of
being stood on.

**Enemies and drips kill on touching pictures** (`m_touches_player`): a pixel of theirs over
one of the player's. The original's tests are boxes around the two: 3 byte columns and 8 rows
for the crawler and the robot, 2 columns for the floor walker and the drip, and 5 rows more
on the frame a jump starts (`big_hitbox`), so a jump under a hovering robot (MARS1 level 5)
killed with the robot still 10 px or so above the head. Now it takes the robot's legs on the
head. The other way round, the floor walker now kills when its picture meets the player's
from the side, up to 10 px apart, where the original's box needed under 6: an idle player at
the start of MARS4 level 8, which drops them on one's beat, dies a little more often. The
turret's bolt and the player's shot keep the original's boxes.

**Other differences**: a jump needs a fresh press of the jump key (the original jumps again on
landing while it's held; at 120 steps a second under a low ceiling that was a new jump, and
its sound, every step), and a press up to 2 frames (110 ms) before landing jumps on landing;
so does a shot (the original fires again as soon as a shot ends while fire is held), a press
up to 2 frames before the last shot ends firing as it does;
turning doesn't cost a frame; standing on a platform moves with it
exactly (the original rides horizontal ones and is pushed up by vertical ones through the
step-up); walking into a boulder pushes it at walking speed when nothing is on its other side, kept
exactly against the player (moved separately, the two round to whole pixels on different
steps and the boulder's edge lifts the feet)
(the original recognises the player's sprite bytes beside it); the enemy explosion plays its 5
tiles over 5 frames instead of drawing them all in one; Enter acts once per press (the
original acts every frame it is held, with a 200 ms pause to slow that down, which modern
drops); the rescue message shows on entering its cell rather than every frame on it. Sound
effects no longer stop the game: between `plat_sfx_begin` and `plat_sfx_end` the tones and
delays are written down and played by the audio thread. Animations (doors, blocks, the
rings, the death explosion) and messages still stop it, as in the original; the step loop
then carries on rather than catching up.

**Checked** with `--script` (frame counts stay the original's frames, about 6.6 steps
each): standing and running jumps land on the original's rows; walking matches to within a
frame; riding the level 18 platforms matches; the level 14 boulders end up where they do in
classic; all 240 regression runs play without trouble.

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
