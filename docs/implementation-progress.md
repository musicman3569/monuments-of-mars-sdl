# Implementation progress (handoff)

Handoff for a new session. Read this, then `docs/notes.md` (the reverse-engineering
reference: every address, table and mechanic decoded so far).

## Objective

A personal learning project: reverse engineer **Monuments of Mars** (Todd Replogle / Apogee,
1990-91, freeware since 2009; DOS, Turbo C, CGA) with Ghidra + GhidraMCP, and port it to run
natively on modern systems (C11 + SDL2, Windows first). It follows the sister project
`../ptomb-port` (Pharaoh's Tomb) in layout, tools and approach. The port reimplements the game
logic in C and reads **all assets at run time from the user's own game files**. It aims to be
faithful, including original quirks, which are kept and commented rather than fixed.

The user is learning, so explain findings as you go. They **prefer CGA palette 0**
(green/red/brown), and that is what this game uses for play anyway.

## Environment (verified)

- Repo: `C:\Users\Jason\Documents\Ghidra\monuments-of-mars-port` (git, branch `main`, no
  remote). `game/` (original files), `unpacked/`, `gfx/`, `build/`, `ghidra/` and generated
  `analysis/*.disasm.txt` / `*.ann.txt` are gitignored.
- Build: MSYS2 **mingw64** (gcc, CMake, Ninja, SDL2). With
  `export PATH=/c/msys64/mingw64/bin:$PATH`: `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`
  then `cmake --build build`. The build is warning-free with `-Wall -Wextra -Wpedantic`; keep it so.
- Python: **no system Python**. Use `C:\Users\Jason\anaconda3\envs\ghidra\python.exe -I`.
- Ghidra has `unpacked/MARS1.EXE` loaded. GhidraMCP's backend is plain HTTP at
  `http://127.0.0.1:8080/`. `tools/ghidra_dump.py analysis/MARS1.disasm.txt` dumps all
  disassembly; `tools/annotate.py` prints one function readably (for the ones the decompiler
  can't do, like `main`); `tools/ghidra_rename.py analysis/MARS1.names.txt` re-applies names.

## Running

```
build/mars.exe game/MARS1.EXE                 # title screen, then play from level 1
build/mars.exe game/MARS1.EXE --level 5       # straight into level 5
build/mars.exe game/MARS1.EXE --level 5 --screenshot out.png   # its first frame, no window
build/mars.exe game/MARS1.EXE --level 2 --script "30 10r 12 1rf 40"   # scripted test run
```

The EXE may be the packed original or an unpacked one; MARSn.IGS / .CF1 must be next to it.
Keys: arrows walk, Space jumps, Ctrl fires (placeholders until the key config is ported),
Enter uses switches / levers / terminals, Q or Esc then Y quits, F1 sound, F5 pause, F6 give
up, Caps Lock + PgDn / ` skips levels (the game's cheat). F12 saves `shotNNN.png`.

`--script` (port extra, for testing) plays without a window and without waiting for the timer,
holding the keys of each token for its frame count (`l r j f e` = left, right, jump, fire,
Enter; "40" = 40 frames of nothing), then prints the level, position, shots, score and deaths.
With `--screenshot` it writes the last frame.

## Completed

- Reverse engineering: see `docs/notes.md` (program flow, graphics, levels, input, player,
  moving things, map codes). Every function the frame loop reaches is decoded and named in
  `analysis/MARS1.names.txt` (applied to Ghidra).
- Port (`src/`):
  - `gamedata` loads the EXE (unpacking LZEXE with `lzexe.c` from ptomb-port), finds the data
    and level segments from DrawLevel's code, copies the 20 level maps, the terminal actions
    and the font, reads the IGS tile set.
  - `cga` keeps video memory **in the real CGA bank layout** (plus the background colour the
    game flashes); `gfx` has the tile blitters, the two back buffers in the same layout, and
    the sampling blitters (collision probes), ported 1:1.
  - `text` is DrawText (letter/digit remapping into the game's font at DS:0006, punctuation
    from ptomb-port's own 8x8 font) and DrawNumber.
  - `level` is DrawLevel (all four passes and the entity lists) and AnimateTiles.
  - `game` is main's level start and frame loop and everything under it: UpdatePlayer (walk,
    jump, gravity, step-up, collisions), UpdatePlayerShot, both platform kinds, the three
    walkers, drips, turret and generator, boulders, stars, FindSpecialTileAt / TouchTile (all
    codes, incl. card / door, switches, levers, M-A-R-S, blocks 5B-5E, terminals 1-4, rescue
    message), CheckLevelExit (incl. the end of the volume and the ending text), RestartLevel,
    CheatLevelSkip, and the in-game keys Q/Esc, F1, F5, F6 with their banners.
  - `platform` is SDL2: window, PC/XT scancodes fed through a copy of the game's keyboard ISR,
    getch / kbhit, delay, the 18.2 Hz tick, Caps Lock, PC speaker tone.
  - `main` shows the title screen (CF1 + "VOL. n - ..." from the EXE), then plays.
- Checked with `--script`: walking over slopes, falling, the level 1 exit (-> level 2, +2000),
  dying and restarting, shooting a robot (+500), picking up +3 shots and a gem; all 20 MARS1
  levels and levels of MARS2-4 run 150 frames without trouble; the windowed game runs.

## Architectural decisions and why

- **Assets read from the user's files at run time, never embedded**, found by byte pattern
  rather than fixed offsets (the volumes' EXEs differ slightly).
- **CGA bank layout everywhere.** The game's blitters, PresentFrame and the collision samples
  all use byte offsets with odd scanlines at +2000h; keeping that layout lets them be ported
  without translation. Only `cga_to_argb` / the PNG writer convert to pixels.
- **One frame per 18.2 Hz tick**, as the original's frame loop waits for its timer ISR
  ([CCD1], incremented by 1054:7aba on INT 1Ch).
- **The level maps are a mutable copy** in `gamedata_t`: the game edits its level segment in
  place (switches, levers, blocks, gems) and restores a level from its backup at DS:8809 when
  it ends.
- **The game's variables stay globals** in `game.c`, named and commented with their DS
  addresses: the routines share them freely, and keeping that shape keeps the port checkable
  against the disassembly.
- **Blocking effects stay blocking** (delay / sound sequences, the door, the rings, the rescue
  message): `plat_delay` keeps the window alive meanwhile, so the game's single-threaded
  structure is kept as is.

## Verified vs assumed

- Verified against the code: `gfx`, `text`, `level_draw`, the status line layout, and `game`
  routine by routine (decompiler output cross-checked with the disassembly wherever Ghidra's
  output was garbled: TouchTile's jump table, UpdatePlayerShot, the rescue / ending screens).
- **Not yet compared with the original running in DOSBox.** Worth checking side by side: the
  feel of jumping and walking, the shot / boulder interactions, the drip RNG sequence.
- Not exercised by a test yet: switches / force fields, levers, the turret and its generator,
  boulders being pushed, card and door, M-A-R-S, terminals, the end of a volume.
- Deviations: the background flashes are as brief as the original's (they set and reset the
  colour back to back), so they hardly show; after level 20 the port ends the game straight
  away, where the original first draws "LEVEL 21" over a level built from the bytes after the
  level table; window close quits from any wait.
- The jump / fire keys are placeholders (Space / Ctrl) until ApplyKeyConfig (1054:6a94) is
  ported. The main menu, high scores and save / restore (S / R in game) aren't ported: the
  port goes straight from the title into the game and exits when it's over.
- The rescue and ending texts are read from the EXE by their first line and the lines after
  it; only MARS1's layout has been checked.

## Next steps (suggested order)

1. Play-test against DOSBox, especially the mechanics listed above as not exercised.
2. Key config (MARS1.KBD, ApplyKeyConfig / ConfigKeyboard).
3. Main menu (CF2, palette 1 bright, the 20-shot cheat), instructions / story / ordering
   screens, high scores (LoadHighScores, CheckHighScore, InputName), save / restore (S / R
   in game, MARS1.SV1 / .SV2), MARS1.SPD and the special notice.
4. The Genus PCX transitions on the title screen.
5. Close the open questions in `docs/notes.md` (codes 14 / 15, 81 / 82).
