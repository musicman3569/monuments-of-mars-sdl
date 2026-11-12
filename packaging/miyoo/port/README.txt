Monuments of Mars for the Miyoo Mini (Plus) under OnionUI
=========================================================

A port of Todd Replogle and Apogee's 1990-91 platformer, reimplemented in C from the original's
machine code. It holds none of the game's files: it reads them from the freeware release.

Installing
----------
1. Get marsfree.zip (Apogee's freeware release, all four volumes), for example from
   https://dukeworld.com/2001-current/apogee%20freeware/ , and copy the files in it
   (MARS1.EXE, MARS1.IGS, MARS1.CF1, MARS1.CF2 and the same for MARS2 to MARS4) into this
   folder, Roms/PORTS/Games/Monuments of Mars. Keep their names in capitals.
2. On the Miyoo, open Ports and run "~Import ports": "Monuments of Mars" then appears under
   Platform. (Ports must be turned on in Apps > Package Manager > Verified.)

With more than one volume here, the game starts with a list of them to choose from; quitting a
volume from its main menu comes back to the list, and B there leaves.

The high scores (MARSn.HS), the saved games (MARSn.SV1 / .SV2) and the port's settings and
controls (mars.ini) are kept in this folder.

Controls
--------
                 In a game                           Menus and questions
D-pad            walk (left / right)                 move
B                jump                                back (Esc), N
Y                fire
A or X           use a switch, lever or terminal     pick (Enter), Y (A)
Start            the pause menu (Continue, Options,  pick (Enter), Y
                 Quit to Menu, Quit Game)
Select           quit (to the main menu, asks first) back (Esc), N
L1 / R1          save / restore (asks first)
Menu             OnionUI's game switcher

B, Y and A stand where the A, X and B of an Xbox pad are, so they do what those do in the PC
version. The Controls screen (C in the main menu) changes them; L2 and R2 can be given an action
there too.

A new high score's name is picked with the d-pad: up and down change the letter under the
cursor, right takes it and moves on, left or B goes back, and A or Start ends.

The game starts with the modern physics (120 steps a second, a pixel at a time), smooth
movement and the quick taps fix on. The Options screen (main menu or pause menu) switches
them; with the physics set to classic it plays as the original did. The PC version's CRT
filter isn't in this build. The cheats need a keyboard.
