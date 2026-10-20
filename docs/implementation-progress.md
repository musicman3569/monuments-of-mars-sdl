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
build/mars.exe game/MARS1.EXE                 # title screen, then the level viewer
build/mars.exe game/MARS1.EXE --level 5       # straight to a level
build/mars.exe game/MARS1.EXE --level 5 --screenshot out.png   # no window
```

The EXE may be the packed original or an unpacked one; MARSn.IGS / .CF1 must be next to it.
In the viewer: PgUp / PgDn change level, Esc quits, F12 saves `shotNNN.png`.

## Completed

- Reverse engineering: see `docs/notes.md` (program flow, graphics, levels, input, player,
  map codes).
- Port skeleton (`src/`):
  - `gamedata` loads the EXE (unpacking LZEXE with `lzexe.c` from ptomb-port), finds the data
    and level segments from DrawLevel's code, copies the 20 level maps and the font, reads the
    IGS tile set.
  - `cga` keeps video memory **in the real CGA bank layout**; `gfx` has the tile blitters and
    the two back buffers in the same layout, ported 1:1 with the collision samples.
  - `text` is DrawText (letter/digit remapping into the game's font at DS:0006, punctuation
    from ptomb-port's own 8x8 font) and DrawNumber.
  - `level` is DrawLevel (all four passes and the entity lists) and AnimateTiles.
  - `platform` is SDL2: window, PC/XT scancodes fed through a copy of the game's keyboard ISR,
    getch / kbhit, delay, the 18.2 Hz tick, PC speaker tone.
  - `main` shows the title screen (CF1 + "VOL. n - ..." from the EXE) and a level viewer at
    18.2 frames per second.
- Checked: title screens of volumes 1 and 2, levels from all four volumes, packed and unpacked
  EXEs give identical output.

## Architectural decisions and why

- **Assets read from the user's files at run time, never embedded**, found by byte pattern
  rather than fixed offsets (the volumes' EXEs differ slightly).
- **CGA bank layout everywhere.** The game's blitters, PresentFrame and the collision samples
  all use byte offsets with odd scanlines at +2000h; keeping that layout lets them be ported
  without translation. Only `cga_to_argb` / the PNG writer convert to pixels.
- **One frame per 18.2 Hz tick**, as the original's frame loop waits for its timer ISR
  ([CCD1], incremented by 1054:7aba on INT 1Ch).
- **The level maps are a mutable copy** in `gamedata_t`: the game edits its level segment in
  place (switches, levers) and restores a level from its backup at DS:8809 when it ends.

## Verified vs assumed

- Verified against the code: everything in `gfx`, `text`, `level_draw`, the status line layout.
- **Not yet compared with the original running in DOSBox.** In particular: the code never draws
  the stars (map codes 74-77) during play; check whether level 1 really has no stars.
- `level_draw_sprites_preview` is a stand-in (first animation frame of each moving thing and
  the player facing right), not the game's code.
- The jump / fire keys are placeholders (Space / Ctrl) until ApplyKeyConfig (1054:6a94) is
  ported.

## Next steps (suggested order)

1. Port the frame loop: UpdatePlayer, the platforms, enemies, drips, turret, boulders, shots,
   TouchTile / FindSpecialTileAt, CheckLevelExit, RestartLevel. Replace the preview.
2. Key config (MARS1.KBD, ApplyKeyConfig / ConfigKeyboard) and the in-game keys (Q/Esc, S, R,
   F1, F5, F6) with their banners.
3. Main menu (CF2, palette 1 bright), instructions / story / ordering screens, high scores,
   save / restore, MARS1.SPD and the special notice.
4. Sound effects (they're inline PlaySound / delay sequences) and the Genus PCX transitions.
5. Close the open questions in `docs/notes.md` (terminal 70, codes 0D, 55, 6F, M-A-R-S bonus).
