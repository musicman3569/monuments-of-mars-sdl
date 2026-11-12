# Monuments of Mars port

A personal learning project: reverse engineering **Monuments of Mars** (Todd Replogle / Apogee,
1990-91, re-released as freeware in 2009; DOS, Turbo C, CGA) with Ghidra, then porting it to
run natively on modern systems in C11 and SDL2.

| | |
|:---:|:---:|
| ![The title screen](docs/screenshots/title.png) | ![The main menu](docs/screenshots/main-menu.png) |
| The title screen | The main menu |
| ![Volume 1, level 1](docs/screenshots/mars1-level-01.png) | ![Volume 2, level 15](docs/screenshots/mars2-level-15.png) |
| Volume 1, level 1: the surface | Volume 2, level 15 |

![Volume 1, level 5 with the CRT filter](docs/screenshots/mars1-level-05-crt.png)
*Volume 1, level 5, with the port's CRT filter*

## Layout

| Path        | Contents                                                        |
|-------------|-----------------------------------------------------------------|
| `src/`      | The port: C11 + SDL2                                            |
| `game/`     | Original game files (not committed, copy them in yourself)      |
| `unpacked/` | Decompressed EXEs produced by `tools/unlzexe.py` (not committed) |
| `tools/`    | Python helpers: unpacker, Ghidra dump/rename/annotate, graphics extractor |
| `analysis/` | Ghidra function names (`MARS1.names.txt`)                       |
| `docs/`     | Reverse engineering notes (`notes.md`) and the port's progress (`implementation-progress.md`) |
| `packaging/` | The release builds: the Windows zip, the Linux AppImage and the Miyoo Mini's OnionUI port (the last two in Docker) |

## The port

The port contains no game assets. At startup it reads everything from your own `MARSn.EXE`
(packed or unpacked) and the files beside it: the level maps, terminal actions, font and menu
screens from the EXE, the tiles from `MARSn.IGS` and the pictures from `MARSn.CF1` / `.CF2`.
The game logic is reimplemented in C from the original's machine code, routine by routine,
keeping the CGA's own memory layout so the blitters and collision tests could be ported
unchanged.

**The whole game is there**, all four volumes: every level, enemy, switch, terminal and sound
effect, the title screen and its Genus PCX transitions, the menus, story and instructions, the
high scores, saved games, the ending and the cheats, compatible with the original's `.HS`,
`.KBD`, `.SPD`, `.SV1` and `.SV2` files. The only differences:

- **Punctuation** uses the port's own 8x8 font, standing in for the IBM BIOS font, which can't
  be shipped. Letters and digits are drawn in the game's own font.
- **Quitting** shows the ordering info as the original does, but not its final text-mode
  shareware notice; the window just closes. A missing `.CF1` / `.CF2` gives an error box rather
  than the text-mode order form.
- **After the ending screen** the port goes straight to the high scores. The original, through
  a glitch, first flashes a junk "LEVEL 21" screen built from leftover data for a moment; there
  is no real level 21, and nothing of the ending is left out.
- **Message screens** (the rescue message, the stone face, the ending) wait for a key to be
  pressed. The original's also ended on a key let go, so a key held as one came up closed it.
- **Esc** in a game opens the port's pause menu instead of asking QUIT (Y/N); Q still asks.
- **Ordering Info.** on the main menu is replaced by **Options** (lettered into the menu
  picture from its own letters).
- **Config Keyboard** (C on the main menu) is replaced by a **Controls** screen, where every
  game action gets two keys and two controller buttons of your choice (see below). A `.KBD`
  setup letter still gives the walk, jump and fire keys until you change them there.

Port extras: a pause menu and an Options screen, rebindable keys, game controller support,
three presets
(including **modern** physics at 120 steps a second), smooth movement, a CRT filter, full
screen and screenshots (see below). The port starts with the **modern** preset, every enhancement and the
CRT filter on; `--classic` (or `preset = classic` in `mars.ini`) plays the original exactly.

The port reads `MARSn.HS`, `MARSn.KBD`, `MARSn.SPD`, `MARSn.SV1` and `MARSn.SV2` from the
folder the EXE is in, and **writes** them there as the original does: a new high score, the
speed check and the saved games (S in game). The controls are saved in `mars.ini` instead.

### Getting the game

The port needs the original game files, which Apogee released as freeware in 2009. They come
as `marsfree.zip` (201 KB, all four volumes) from either of these:

- [DOS Games Archive: Monuments of Mars](https://www.dosgamesarchive.com/download/monuments-of-mars)
  (the game's page, which can also run it in the browser), or its
  [direct download](https://www.dosgamesarchive.com/file/monuments-of-mars/marsfree)
- [Dukeworld: Apogee freeware archive](https://dukeworld.com/2001-current/apogee%20freeware/)
  (a folder of Apogee's freeware releases; pick `marsfree.zip`)

Unzip it next to `mars.exe` (for a release download) or into `game/` (when building from
source). The port needs `MARSn.EXE`, `MARSn.IGS`, `MARSn.CF1` and `MARSn.CF2` for each volume
you play; the other files are created as you play.

### Build (Windows, MSYS2 mingw64)

Needs the MSYS2 packages `mingw-w64-x86_64-gcc`, `-cmake`, `-ninja` and `-SDL2`. From a shell
with `C:\msys64\mingw64\bin` on `PATH`:

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

That produces `build/mars.exe` with `SDL2.dll` copied next to it.

### The Windows zip

```
packaging/windows/build-zip.sh
```

run from an MSYS2 MINGW64 shell or Git Bash (with the packages above; MSYS2 in `C:\msys64`, or
`MINGW_PREFIX` set to another mingw64 folder), builds `dist/MonumentsOfMars-win64.zip`: a
`MonumentsOfMars` folder with the stripped `mars.exe` (with its icon and version information),
`SDL2.dll`, this README and the licences. The script stops if either program needs a DLL that
is neither in the zip nor part of Windows.

### Build (Linux)

The same commands, with GCC or Clang, CMake, Ninja and SDL2 from the package manager (on Arch:
`pacman -S --needed base-devel cmake ninja sdl2-compat`). That produces `build/mars`. The game's
file names can be in any case (`MARS1.EXE` or `mars1.exe`).

### The Linux AppImage (Steam Deck, Bazzite and other distributions)

```
packaging/linux/build-appimage.sh
```

builds `dist/MonumentsOfMars-x86_64.AppImage` in a Docker container (Ubuntu 22.04, with SDL2
2.32 built from source and bundled), so it runs on any x86-64 Linux with glibc 2.34 or later:
SteamOS 3, Bazzite, Fedora, Ubuntu 22.04 and later, Arch. Only Docker is needed on the build
machine. The AppImage holds the port, SDL2 and their licences (in
`usr/share/doc/monuments-of-mars/`) only; the game's files stay outside it.

To play, put the AppImage in a folder with the files from `marsfree.zip` (or with a `game`
folder holding them), make it executable (`chmod +x`, or Properties > Permissions in a file
manager) and run it. On a Steam Deck or Bazzite, add it to Steam in Desktop Mode (Steam > Games
> Add a Non-Steam Game to My Library > Browse) to play it in Gaming Mode; the Deck's controls
are a game controller. `mars.ini` is kept beside the AppImage, and the scores and saves beside
the game's files, so both folders must be writable.

### The Miyoo Mini (Plus) under OnionUI

```
packaging/miyoo/build-onion-port.sh
```

cross-compiles the port in a Docker container and builds `dist/MonumentsOfMars-OnionUI.zip`,
laid out as OnionUI's [Ports Collection](https://github.com/OnionUI/Ports-Collection) lays out
its engine ports. The container has the Miyoo Mini toolchain from shauninman's
[union-miyoomini-toolchain](https://github.com/shauninman/union-miyoomini-toolchain) (GCC 8.3,
glibc 2.28) and XK9274's sdl2_miyoo, an SDL 2.0.20 with drivers for the handheld's display,
sound and buttons, which goes into the zip; its upstream is gone, so its prebuilt library and
headers are kept in [`packaging/miyoo/sdl2-miyoo/`](packaging/miyoo/sdl2-miyoo/README.md) (the
same copy as Realms of Chaos's). Only Docker is needed on the build machine.

To install it, extract the zip to the root of the SD card, copy the files from `marsfree.zip`
into `Roms/PORTS/Games/Monuments of Mars/`, then in Ports run "~Import ports": Monuments of Mars
appears under Platform. The volume selector comes first; the scores, saves and `mars.ini` are
kept in that folder too.

The build for the handheld (CMake's `MARS_MIYOO`) starts in the modern preset as the PC does,
every enhancement on but the CRT filter. It shows the picture doubled to 640 x 400 with
black bars, has no CRT filter or full screen switch (Options leaves them out), and takes the
high score name from the d-pad: up and down pick the letter under the cursor, right moves on,
left or B goes back, A or Start ends. The buttons keep the places of an Xbox pad's in a game,
and the handheld's own labels in the menus and questions; the Controls screen calls them by
the handheld's names (L1, R2, SELECT):

| Button | In a game | Menus and questions |
|--------|-----------|---------------------|
| D-pad | walk | move |
| B | jump | Esc (back), N |
| Y | fire | |
| A or X | use (Enter) | A: Enter, Y |
| Start | the pause menu | Enter, Y |
| Select | quit (Q) | Esc, N |
| L1 / R1 | save / restore | |

### Run

```
build/mars.exe game/MARS1.EXE
```

Run `mars.exe game/MARS2.EXE` (and so on) for the other volumes. Without an EXE on the command
line, the port opens a volume selector listing the `MARSn.EXE` files it finds in the current
folder, next to `mars.exe` (or the AppImage) or in a `game` folder there (1 First Contact, 2 The Pyramid, 3 The Fortress, 4 The Face):
choose one with the arrows, the d-pad or a digit and Enter or A. Quitting a volume from its main
menu comes back to the selector; Esc there leaves. With only one volume, it starts straight away; with none, a message lists the folders it looked in.

With a release download, put the game files next to `mars.exe` and double-click it. The game starts full screen; F11 or
Alt+Enter switches to a window, and `--window` starts in one.

Menu keys as in the original: J (or Enter) to journey onward, I instructions, S story so far,
O options, C controls, H high scores, Q quit. The port adds a highlight you move with
the arrow keys, the d-pad or the stick, and choose with Enter, Space or A.

Game keys (the defaults: the original's, with its keyboard setup D; C in the main menu changes
them):

| Key | Action |
|-----|--------|
| Left / Right, or Alt / Ctrl | walk |
| Space | jump |
| Left Shift | fire |
| Enter | use a switch, lever or terminal you're standing at |
| S / R | save / restore |
| F1 | sound on/off |
| F5 | pause |
| F6 | give up (lose a life and restart the level) |
| Q, then Y | quit to the main menu |
| Caps Lock on + PgDn / `` ` `` | cheat: next / previous level |
| Caps Lock on + `>` (main menu) | cheat: start with 20 shots |

Game controllers stand in for the keyboard (the original had no joystick support). Most
current pads (Xbox, PlayStation, Switch Pro, 8BitDo and others) are recognised automatically
and can be plugged in at any time; for anything else, put a
[`gamecontrollerdb.txt`](https://github.com/mdqinc/SDL_GameControllerDB) next to `mars.exe`.
Button names are Xbox ones; the in-game buttons are the defaults, which the Controls screen
changes (LT, RT and the stick clicks can be bound too):

| Control | In game | Menus and text screens | Y/N questions |
|---------|---------|------------------------|---------------|
| Left stick / d-pad | walk | up / down choose (main menu) | |
| A | jump | Enter | Y |
| X | fire | | |
| B or Y | use (Enter) | B: Esc | B: N |
| Start | pause menu | Enter | Y |
| Back / View | quit (Q) | Esc | N |
| LB / RB | save / restore | | |

The high score name and the cheats need the keyboard.

**Controls** (C in the main menu) shows every game action (walk left / right, jump, fire, use,
the pause menu, save, restore, quit, pause, sound, give up) with two keyboard keys and two
controller buttons. Move with the arrows, the d-pad or the stick; Enter or A, then press the
new key or button (Esc or Start cancels); Del or X clears one; Esc or B leaves, saving them in
`mars.ini`'s `[keys]` and `[pad]` sections. A key or button can only do one thing, so binding
it takes it from wherever it was. Esc and Start always open the pause menu and the left stick
always walks. While you play, a key bound to nothing that the game itself would read as an
action (Space once jump is elsewhere, say) does nothing; the menus, the Y/N questions and the
pause menu still take the usual keys.

Port keys:

| Key | Action |
|-----|--------|
| Esc | pause menu: Continue, Options, Quit to Menu, Quit Game |
| F4 | smooth movement on/off (`display.smooth`, classic physics only) |
| Alt+C | CRT filter on/off (`display.crt`) |
| F11 or Alt+Enter | full screen on/off |
| F12 | save a screenshot, `shotNNN.png`, in the current folder |

### Settings

The port has three presets:

- **modern** (the default): the game rewritten to run at 120 steps a second, with every
  enhancement on, the CRT filter included.
- **enhanced**: the original's rules and timing, with smooth movement and quick taps on.
- **classic**: the original game exactly, quirks and all. The default for runs without a
  window (`--screenshot`, `--script`, `--show`).

Its settings:

- **Modern physics** (`game.physics = modern`): the original moves things 4 pixels across at
  a time, 18 times a second. Modern moves everything a pixel at a time at 120 steps a second
  at the same average speeds, and draws at your monitor's refresh rate. The levels and rules
  are the same, and the original's collision points are kept, now tested at every pixel. Sound
  effects no longer stop the game. A few of the original's quirks are fixed along the way:
  - Landings are exact (the original sinks up to 3 rows into the floor and climbs back out).
  - Pickups don't block you, and a jump carries on through them instead of stopping on one.
  - Enemies and drips kill only when their pixels touch yours, not on the original's boxes,
    which reached well past the pictures (a robot hovering 10 px above your head killed you).
  - A jump needs a fresh press of the jump key, and so does a shot. A press just before
    landing still jumps on landing.
  - Enter acts once per press.
  - Only you can push a boulder (in the original a robot could push one too), and not into
    an enemy on its other side.
  - You stand on the moving platforms and ride them exactly.

  `docs/notes.md` ("Modern physics") lists every difference and why.
- **Smooth movement** (`display.smooth`): with classic physics, moving things glide between
  the game's steps at your monitor's refresh rate. Only the picture changes; the game plays
  exactly as without it. F4 switches it while playing.
- **Quick taps** (`input.quick_taps`): a key or button tapped and let go between two of the
  game's frames still counts. The original only sees what is held as a frame starts, so a
  quick tap of jump or Enter could be missed.
- **CRT filter** (`display.crt`, on in modern): the CRT look of RetroArch's
  `2xsal-level2-crt` shader preset. The picture is doubled with 2xSaL's smoothed edges, then
  scaled to the window under a shadow mask. Alt+C switches it.
- **Full screen** (`display.fullscreen`, on in every preset).

Choose a preset with `--classic`, `--enhanced` or `--modern`, or in a `mars.ini` file next to
`mars.exe`, which can also change single settings:

```ini
[game]
preset = modern       ; or enhanced, or classic
physics = modern      ; or classic (the original's 18 steps a second)

[display]
smooth = on           ; things glide between the game's steps (classic physics). F4 switches
crt = on              ; the CRT filter. Alt+C switches
fullscreen = on       ; F11 or Alt+Enter switches

[input]
quick_taps = on       ; a tap between two frames still counts (off in classic)

[keys]                ; the Controls screen's: two keys per action, - for none
jump = SPACE, Z
fire = LSHIFT, -

[pad]
jump = A, -
fire = X, RT
```

`mars --print-settings > mars.ini` writes a complete file with every setting explained. The
command line overrides the file: `--set display.crt=on`, for example. The Options screen (in
the main menu and the pause menu) switches physics, smooth movement, the CRT filter, full
screen and quick taps at once, and saves the ones you changed into `mars.ini`, keeping
everything else in the file as it was. F4, Alt+C and F11 change the current run only.

### Command line

```
mars [MARSn.EXE] [options]
```

| Option | What it does |
|--------|--------------|
| `--classic` / `--enhanced` / `--modern` | choose the preset (overrides `mars.ini`) |
| `--set section.key=value` | change one setting, e.g. `--set game.physics=modern` (can be repeated) |
| `--config FILE` | read settings from FILE instead of `mars.ini` next to `mars.exe` |
| `--print-settings` | print the current settings as a `mars.ini` and exit |
| `--crt` | start with the CRT filter on |
| `--window` | start in a window |
| `--palette 0\|1` | force CGA palette 0 or 1 (the game itself uses 0 for play and 1 for the menu) |
| `--bright` | use the high intensity palette |
| `--level N` | skip the title, play level N first, then go on to the menu |

For testing and development (these run without a window, and read `mars.ini` only when
`--config` names it):

| Option | What it does |
|--------|--------------|
| `--screenshot out.png` | write the title screen, or with `--level` that level's first frame, to a PNG and exit |
| `--script "..."` | play a level with scripted keys, then print the level, position, shots, score and deaths |
| `--show SCREEN` | run one menu screen (`title`, `menu`, `story`, `instructions`, `ordering`, `keys`, `hiscores`, `notice`), writing `<out>-N.png` (from `--screenshot`) at every wait for a key |

A script is a list of tokens, each a number of frames followed by the keys held for them:
`l r j f e` (left, right, jump, fire, Enter), or `S` / `R` (save / restore, answering Y). A
number alone waits. For example, `--level 2 --script "30 10r 12 1rf 40"`. With
`--screenshot`, the last frame is written too.

## Reverse engineering workflow

The shipped `MARSn.EXE` files are compressed with LZEXE 0.91, so Ghidra only sees the
decompression stub. Unpack them first:

```
python tools/unlzexe.py game/MARS1.EXE unpacked/MARS1.EXE
```

Then import `unpacked/MARS1.EXE` into Ghidra (format: Old-style DOS Executable (MZ)), run
auto-analysis, and apply the known names with
`python tools/ghidra_rename.py analysis/MARS1.names.txt` (needs GhidraMCP running).
`tools/ghidra_dump.py` dumps the whole disassembly, and `tools/annotate.py` prints one
function readably, for the ones the decompiler can't handle (like `main`). The game was built
with Turbo C in the huge memory model; `docs/notes.md` has the segment map, every decoded
routine and table, and the GhidraMCP quirks to watch for.

## License

The port's source code and tools are released under the MIT License (see `LICENSE`), except
`src/crt.c`, which translates two GPL shaders from RetroArch's collection (`2xsal-level2.slang`
by guest(r), and `dotmask.slang` by cgwg, Timothy Lottes and torridgristle) and is under the GNU
General Public License, version 2 or later (see `LICENSE-GPL-2.0.txt`). A build of the port
includes it, so the program as a whole is distributed under the GPL.

The Miyoo Mini build also holds sdl2_miyoo, under the GNU GPL version 3 (its license goes into
the zip), so that build as a whole is under GPL-3.0.

That covers this repository only: Monuments of Mars itself is (c) 1990-91 Scenerio Software
and Apogee Software, distributed as freeware, and none of its files are included here.
