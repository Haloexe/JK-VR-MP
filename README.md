# OpenJK VR

**Star Wars Jedi Knight: Jedi Academy multiplayer in a PC VR headset**, with a
lightsaber you really swing. Built on [OpenJK](https://github.com/JACoders/OpenJK).

- **Real lightsaber combat:** your blade cuts, blocks and clashes where you
  actually swing it, harder the faster you swing. Blades are solid: yours
  rests on another blade and slides along it instead of passing through. Saber
  styles become the blade's weight (light, medium, heavy), you can grip a saber
  with both hands, and the controllers rumble with every hit.
- **Your body in VR:** you see your character's body under you. Its arms,
  elbows and shoulders follow your controllers naturally, and it works with any
  player model on the standard skeleton, custom models included.
- **Multiplayer that shows it:** other players see you as you really move
  (head, arms, hands and saber), and lag compensation judges blades as each
  player saw them.
- **Roleplay:** servers can switch on a roleplay mode at any time. Players log
  in to an account and play characters of their own (a name, a faction, a
  description and a bio) that everyone sees, with talk that reaches only those
  nearby, speech bubbles, `/me` and out-of-character chat, and character cards
  you see by looking at someone. Passwords never leave the player's PC.
- **A wheel and a keyboard in the headset:** hold Y for a wheel to talk, take
  a pose (sit, sit cross-legged, kneel or lie down, with your view at your
  character's eyes and your head and arms still tracked) or reach roleplay;
  a keyboard comes up whenever something takes typing. Players without VR get
  the wheel on a key.
- **Duels with a salute:** challenge someone to a private duel by saluting
  them with your lit saber; they accept by saluting back. Any number of duels
  can go on at once, each pair on its own, and bots accept challenges too.
- **Proximity voice chat:** talk to the players near you and hear them from
  where they stand.
- **Guns in your hand:** aim with your hand, with a 3D aim dot.
- **Dedicated server** for Windows and Linux, so you can run your own.
- **Plays on a monitor too:** without a headset you get a borderless window,
  every resolution your monitor has (widescreen and ultrawide included) or a
  custom one, and a HUD, menus and field of view made for widescreen.

VR players and players without VR can share a server. On ordinary servers the
VR client still works, with the game's usual animated saber combat.

## Requirements

- Jedi Academy (for example from [Steam](https://store.steampowered.com/app/6020/)
  or [GOG](https://www.gog.com/game/star_wars_jedi_knight_jedi_academy)). The
  game's own files are not included here.
- Windows 10 or 11 (64-bit) and a PC VR headset with an OpenXR runtime:
  SteamVR, Meta Quest Link / Air Link, or Virtual Desktop. Windows Mixed
  Reality headsets need SteamVR.

## Install

1. Download `OpenJK-VR.zip` from [Releases](../../releases).
2. Extract it into Jedi Academy's `GameData` folder, so you end up with
   `GameData/OpenJK-VR/`. The original game files are not touched; to
   uninstall, delete that folder.
3. Start your headset's PC software, then run **`Launch OpenJK VR.bat`**.

Controls, settings and everything else are in
[`vr/README-VR.txt`](vr/README-VR.txt), which is also in the download.

## Run a server

- **Windows:** run `Launch OpenJK VR Server.bat` from the same folder.
- **Linux (x86_64, glibc 2.34+, e.g. Ubuntu 22.04 or newer):** download
  `OpenJK-VR-Server-linux.tar.gz` from [Releases](../../releases) and add your
  own `assets0.pk3` to `assets3.pk3`.

See [`vr/server/README-SERVER.txt`](vr/server/README-SERVER.txt) for ports,
settings and commands. Players need the same OpenJK VR version as the server.

## Build

- **Windows client and server**, cross-compiled from Linux with
  [llvm-mingw](https://github.com/mstorsjo/llvm-mingw):
  `LLVM_MINGW=/opt/llvm-mingw vr/build-win64.sh`
- **Linux server:** `vr/build-linux-server.sh`

The VR code lives in the engine (`codemp/client/cl_vr.cpp`, voice chat in
`cl_voice.cpp`), the renderer (`codemp/rd-vanilla`), the client game
(`codemp/cgame/cg_vrbody.c`: body, arms, solid blades and haptics) and the server game
(`codemp/game/g_vrsaber.c`: saber combat; `g_vrrp.c`: roleplay). The "HOW IT WORKS" section of
`vr/README-VR.txt` has the details.

## Credits

OpenJK VR was made by Haloexe, developed with Claude (Anthropic). It stands
on the work of many others:

- **[OpenJK](https://github.com/JACoders/OpenJK)** by the JACoders and every
  OpenJK contributor: the engine and game code this is built on (GPLv2).
  OpenJK's own readme is in [`README-OpenJK.md`](README-OpenJK.md).
- **Raven Software**, who made Jedi Academy and released its source code under
  the GPLv2, and **id Software**, whose id Tech 3 (Quake III Arena) engine it
  grew from.
- **[Team Beef](https://www.patreon.com/teambeef)** (DrBeef, Baggyg and
  Bummser) for **[JKXR](https://github.com/Team-Beef-Studios/JKXR)**, their VR
  port of Jedi Outcast and Jedi Academy (GPLv2; lead programmer DrBeef,
  additional development by MuadDib and BaggyG). OpenJK VR's OpenXR session,
  swapchain and frame handling started from JKXR's `TBXR_Common.cpp`, and its
  floating screen for menus, the console and loading follows JKXR's
  `VR_UseScreenLayer`. JKXR showed what Jedi Knight can be in VR, with a
  motion-controlled lightsaber, guns in your hands and the game's menus in a
  headset, and this multiplayer version follows its lead. Please support their
  work.
- **The Khronos Group** for the OpenXR SDK: its headers and loader are
  "Apache-2.0 OR MIT", used here under MIT
  ([`lib/openxr/LICENSE-MIT`](lib/openxr/LICENSE-MIT)). The loader includes
  **JsonCpp** by Baptiste Lepilleur and the JsonCpp Authors.
- **[Opus](https://opus-codec.org)** by Xiph.Org, Skype, Jean-Marc Valin,
  Timothy B. Terriberry, Koen Vos and the other Opus authors, for voice chat
  (BSD, [`lib/opus/COPYING`](lib/opus/COPYING)).
- The libraries that come with OpenJK: **SDL2** (Sam Lantinga and the SDL
  contributors), **zlib** (Jean-loup Gailly and Mark Adler), **minizip**
  (Gilles Vollant), **libpng** (Cosmin Truta and the libpng authors),
  **libjpeg** (the Independent JPEG Group) and **gsl-lite** (Martin Moene).
  This software is based in part on the work of the Independent JPEG Group.
- **[llvm-mingw](https://github.com/mstorsjo/llvm-mingw)** by Martin Storsjö,
  with LLVM and mingw-w64, which builds the Windows version.

## Licence

OpenJK VR is licensed under the GPLv2 ([`LICENSE.txt`](LICENSE.txt)), like
OpenJK and JKXR. The libraries above keep their own licences, in their folders
under [`lib/`](lib/). Jedi Academy's own game files are not included: you need
your own copy of the game.

This is a fan project. It is not made, distributed or supported by Activision,
Raven Software, Lucasfilm or Disney. Star Wars, Jedi and Jedi Knight are
trademarks of Lucasfilm Ltd. and Disney.
