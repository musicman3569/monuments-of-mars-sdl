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
build/mars.exe game/MARS1.EXE                 # title screen, then the main menu
build/mars.exe game/MARS1.EXE --level 5       # a game at level 5 first, then the menu
build/mars.exe game/MARS1.EXE --level 5 --screenshot out.png   # its first frame, no window
build/mars.exe game/MARS1.EXE --level 2 --script "30 10r 12 1rf 40"   # scripted test run
build/mars.exe game/MARS1.EXE --show instructions --screenshot out.png  # out-1.png .. out-5.png
```

The EXE may be the packed original or an unpacked one; MARSn.IGS / .CF1 / .CF2 must be next
to it. Like the original, the port reads and writes MARSn.HS, .KBD, .SPD, .SV1 and .SV2 there
(test with a copy of the game folder: `--show title` creates .SPD, scripted S writes saves).
Keys: the setup in MARSn.KBD (default D: Alt / Ctrl or the arrows walk, Space jumps, left
Shift fires), Enter uses switches / levers / terminals, Q or Esc then Y quits, S / R save
and restore, F1 sound, F5 pause, F6 give up, Caps Lock + PgDn / ` skips levels and Caps Lock +
`>` in the menu starts with 20 shots (the game's cheats). F12 saves `shotNNN.png`.

Game controllers (port extra, `gamepad.c`; Xbox names): left stick / d-pad walk, A jumps,
X fires, B or Y use (Enter), Start pauses (F5), Back quits (Esc), LB / RB save / restore
(S / R); at a Y/N question A / Start = Y, B / Back = N; in the menus and text screens A /
Start = Enter (the main menu's Enter plays) and B / Back = Esc. The other menu choices, the
high-score name and the cheats need the keyboard. A `gamecontrollerdb.txt` next to mars.exe
adds controller mappings.

Presets and settings (port extra, `settings.c`, like ptomb-port's): `--classic` (the default:
the original game), `--enhanced` (the original's rules with the display and input
enhancements) or `--modern` (the game run at 120 steps a second), then `mars.ini` next to
mars.exe (or `--config FILE`), then `--set section.key=value`. `--print-settings` writes a
commented mars.ini. Enhancements: `game.physics = modern` (things move a pixel at a time at
120 steps a second, drawn at the monitor's refresh rate, and sound effects don't stop the
game; see notes.md, "Modern physics"), `display.smooth` (with classic physics, moving things
glide at the monitor's refresh rate; F4 switches it while playing, and the window title says
when it's on) and `input.quick_taps` (a tap between two frames still counts).

`--script` (port extra, for testing) plays without a window and without waiting for the timer,
holding the keys of each token for its frame count (`l r j f e` = left, right, jump, fire,
Enter; S / R = save / restore, answered Y; "40" = 40 frames of nothing), then prints the
level, position, shots, score and deaths. With `--screenshot` it writes the last frame.
`--show SCREEN` (title, menu, story, instructions, ordering, keys, hiscores, notice) runs one
screen without a window, writing a numbered screenshot at every wait for a key and answering
it with Esc.

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
  - `keyconfig` is LoadKeyConfig / ApplyKeyConfig / SaveKeyConfig and the ConfigKeyboard
    screen (its text comes from the EXE).
  - `menu` is TitleScreen (incl. MARSn.SPD and the special notice) and MainMenu.
  - `effects` is the Genus PCX Effects transitions the game uses (types 0, 1, 3, 6, 7, 9 and
    all their variants), decoded from the library, with its byte-wise CGA blit.
  - `screens` runs the story, instructions and ordering screens **from the EXE** with a small
    interpreter (like ptomb-port's screenvm), finding them through MainMenu's key tests; and
    ShowSpecialNotice.
  - `hiscore` is Load / Save / Show / CheckHighScore and InputName.
  - `game` also has SaveGame / RestoreGame (S / R) and calls CheckHighScore at the end.
  - `main` is main's flow: title, then the menu loop (menu, game, high score).
  - `gamepad` (port extra; the original is keyboard-only) turns SDL game-controller buttons
    into key presses: in game make / break codes through the copy of the keyboard ISR (A and
    X press the configured jump / fire keys, so the KBD setup still applies), outside it
    characters in the BIOS buffer. Each button releases the code it pressed, and game.c
    flags the Y/N questions (`pad_set_yes_no`) so A / B answer them. Hotplugging works.
  - `settings` (port extra) is the classic / enhanced / modern presets and mars.ini, trimmed
    from ptomb-port's.
  - `game` also has modern physics (port extra, `game.physics = modern`): its own frame loop
    at 120 fixed steps a second, float positions, the original's collision points tested
    pixel by pixel, and drawing between steps at the display's rate (`blit_masked_px` in
    gfx.c). The code shared with classic only differs where `modern` is tested (one use of
    Enter per press, sound effects handed to the audio thread through `plat_sfx_begin` /
    `_end`, the rescue message on entering its cell). See notes.md, "Modern physics".
  - `smooth` (port extra, `display.smooth`) is the display layer: gfx.c reports each frame's
    CopyBackground, opaque and masked blits into buffer 2 and PresentFrame; game.c tags the
    moving things' blits (`smooth_id`); the window shows the frame's background with the
    masked tiles drawn over it as textures, the tagged ones at a position that chases the
    game's. See notes.md, "The display layer".
  - `platform` also has `input.quick_taps` (`plat_frame_input_begin` / `_end` around each
    frame) and draws smooth frames at the monitor's rate.
- Checked with `--script`: walking over slopes, falling, the level 1 exit (-> level 2, +2000),
  dying and restarting, shooting a robot (+500), picking up +3 shots and a gem; all 20 MARS1
  levels and levels of MARS2-4 run 150 frames without trouble; save / restore round trip.
- Checked with a throwaway harness: every transition type and variant the game uses ends on
  exactly the picture (CF1 and CF2); mid-transition frames match the decoded patterns.
- Game controllers: a throwaway harness driving `pad_apply` checked every mapping in each
  mode (menu, game, Y/N), releases across a mode change, a held button / stick when a game
  starts, an empty KBD setup; the scripted run is unchanged; SDL sees the user's Xbox One
  controller as a game controller; the user played with it by hand. (`--script` bypasses
  the ISR, so it can't exercise the pad.)
- Checked with `--show`: every menu screen in all four volumes (incl. MARS4's moved
  BlitTile_Screen and its built-in high-score table). With throwaway harnesses: high-score
  entry (typing, Backspace, '?', the file written back with CR LF), the key config screen.
  With real key presses sent to the window: title, high scores, a game quit with Q / Y, the
  menu's Q, ordering info, exit.

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
- Deviations: the background flashes are as brief as the original's (they set and reset the
  colour back to back), so they hardly show; after level 20 the port ends the game straight
  away, where the original first draws "LEVEL 21" over a level built from the bytes after the
  level table; window close quits from any wait.
- Left out on purpose (the user has the full version): the text-mode shareware notice on
  quitting and the text-mode "incomplete version" order form shown when CF1 / CF2 are missing
  (the port shows an error box instead).
- The transitions' speed is an estimate. The menu's delay of 1 unit per step (about 9.9 ms) is
  from the code, but the original also took as long as its drawing did, and with delay 0 (the
  title) that's all there is. The port charges 0.4 ms per blit plus 1 ms per 16,000 pixels
  (`step` in effects.c); compare with DOSBox and adjust if needed.
- In-game keys are read at the start of each frame from the ISR's last scancode, so a tap
  shorter than a frame (55 ms) can be missed, as in the original (classic). Enhanced
  (`input.quick_taps`) counts it.
- Play-tested by the user against the original: the frame loop matches.
- Modern physics: checked with scripts against classic (jumps, walking, platforms, boulders;
  see notes.md) and live at 142 pictures a second on a 144 Hz display; classic's 240
  regression runs are unchanged. Not yet play-tested by hand.
- The rescue and ending texts are read from the EXE by their first line and the lines after
  it; only MARS1's layout has been checked.

## Next steps (suggested order)

1. Compare the transitions' speed with DOSBox and tune `step` in effects.c.
2. Close the open questions in `docs/notes.md` (codes 14 / 15, 81 / 82).
