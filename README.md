# OpenJK VR

**Star Wars Jedi Knight: Jedi Academy multiplayer in a PC VR headset**, with a
lightsaber you really swing. Built on [OpenJK](https://github.com/JACoders/OpenJK).

- **Real lightsaber combat:** your blade cuts, blocks and clashes where you
  actually swing it, harder the faster you swing. Saber styles become the
  blade's weight (light, medium, heavy), you can grip a saber with both hands,
  and the controllers rumble with every hit.
- **Your body in VR:** you see your character's body under you. Its arms,
  elbows and shoulders follow your controllers naturally, and it works with any
  player model on the standard skeleton, custom models included.
- **Multiplayer that shows it:** other players see you as you really move
  (head, arms, hands and saber), and lag compensation judges blades as each
  player saw them.
- **Proximity voice chat:** talk to the players near you and hear them from
  where they stand.
- **Guns in your hand:** aim with your hand, with a 3D aim dot.
- **Dedicated server** for Windows and Linux, so you can run your own.

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
(`codemp/cgame/cg_vrbody.c`: body, arms and haptics) and the server game
(`codemp/game/g_vrsaber.c`: saber combat). The "HOW IT WORKS" section of
`vr/README-VR.txt` has the details.

## Credits and licence

- Based on [OpenJK](https://github.com/JACoders/OpenJK) by the JACoders
  community, licensed under GPLv2 ([`LICENSE.txt`](LICENSE.txt)). OpenJK's own
  readme is in [`README-OpenJK.md`](README-OpenJK.md).
- The OpenXR session code started from Team Beef's
  [JKXR](https://github.com/Team-Beef-Studios/JKXR) (GPLv2).
- OpenXR headers and loader: Khronos Group, "Apache-2.0 OR MIT", used here
  under MIT ([`lib/openxr/LICENSE-MIT`](lib/openxr/LICENSE-MIT)).
- Voice chat uses the [Opus](https://opus-codec.org) codec (BSD,
  [`lib/opus/COPYING`](lib/opus/COPYING)).

This is a fan project. It is not affiliated with or endorsed by Lucasfilm,
Disney, Raven Software or Activision. Star Wars and Jedi Academy are
trademarks of their respective owners.
