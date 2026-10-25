# OpenJK VR

**Star Wars Jedi Knight: Jedi Academy multiplayer in a PC VR headset**, with a
lightsaber you really swing. Built on [OpenJK](https://github.com/JACoders/OpenJK).

> [!WARNING]
> **An early alpha whose code hasn't been reviewed by a person.** OpenJK VR
> was written with an AI assistant (Claude) and tested as it was built, but
> nobody has checked its code by hand or audited it for security, and it
> changes a game engine that talks to other computers over the internet.
> Be careful with both the game and the server:
>
> - Join servers you trust, and host games only for people you trust.
> - Use a roleplay password you don't use anywhere else.
> - Run a server as an ordinary user (never root or Administrator), on a
>   machine or VPS with nothing sensitive on it, with only its game port open
>   and a long, random rcon password. Back up its roleplay accounts.
>
> It comes with no warranty of any kind (see the [licence](#licence)).

- **Real lightsaber combat:** your blade cuts, blocks and clashes where you
  actually swing it, harder the faster you swing, and deflects blaster bolts
  where it meets them - its angle aims them, and Saber Defense 3 bends them
  back onto the shooter. Blades are solid: yours rests on another blade and
  slides along it instead of passing through. Saber styles become the blade's
  weight (light, medium, heavy), and a heavy blade lags behind a fast swing.
  Hold a saber in either hand, anywhere along the hilt, the usual way or
  reversed; pass it from hand to hand or grip it with both. The controllers
  rumble with every hit.
- **The Force in your hand:** hold the Force grip and your empty hand reaches
  out with the Force - thrust it out to Push, reach and yank it back to Pull,
  point and hold it still to Grip, pull its trigger for Lightning (palm up:
  Drain), and squeeze quickly for any other power, picked on the wheel's new
  Force ring. On a server running this build each power comes from your hand
  and goes where it points - or where your hand is as you see it: put it over
  them - (or the way you thrust), snapping onto someone within 10 degrees
  (Grip: 15, and reaching a quarter further), whoever your Grip's aimed at
  glowing, and the controller thumps, tugs, strains and crackles with it.
  Whoever your Grip holds moves with your hand - swing them round, lift them,
  draw them in or push them away (level 3; level 2 lifts and lowers), and
  fling them as you let go to throw them. Or push and pull with an open hand,
  or turn the gestures off, under VR Force in Setup > Controls.
- **Saber colours:** any RGB colour for each of your sabers, and a black core
  inside the coloured glow, picked on the RGB Colors page (Profile >
  Lightsaber, or Setup > VR) with sliders or a hex code while a preview shows
  the blade. Everyone on a server running this build sees them; elsewhere
  others see the stock colour nearest yours.
- **Holsters:** your weapons hang on your body, at your hips and over your
  shoulders, and you take them out and put them away with your hands. As it
  comes, a saber stays in your hand only while you hold the grip, and holding
  the trigger as you draw lights it as it comes out. Carry two sabers, and see
  everyone's hilts on their belts.
- **Your body in VR:** you see your character's body under you. Its arms,
  elbows and shoulders follow your controllers naturally, and its fingers
  follow yours on the controllers, so you can point, give a thumbs up or make
  a fist. Standing still, it leans, bows, turns and crouches the way you do,
  its feet planted. It works with any player model on the standard skeleton,
  custom models included.
- **Multiplayer that shows it:** other players see you as you really move
  (head, body, arms, fingers and saber), and lag compensation judges blades as
  each player saw them.
- **Roleplay:** servers can switch on a roleplay mode at any time. Players log
  in to an account and play characters of their own (a name, a faction, a
  description and a bio) that everyone sees, with talk that reaches only those
  nearby, speech bubbles, `/me` and out-of-character chat, and character cards
  you see by looking at someone. Passwords never leave the player's PC. In a
  fight, running out of health puts a character down instead of killing them,
  until someone of their faction helps them up or they give up, and NPCs take
  sides by the players' factions. Characters keep companions of their own (up
  to six, two out at a time, as it comes) that follow them, fight for them,
  get them back up and take orders from the wheel. Server owners can make
  trusted accounts admins, who get every cheat (and a menu page to spawn any
  NPC or vehicle) without cheats being turned on for everyone. Only the
  server's console or rcon can make an admin, and logging in works with
  roleplay switched off.
- **A wheel and a keyboard in the headset:** hold Y for a wheel to talk, take
  a pose (sit, sit cross-legged, kneel or lie down, with your view at your
  character's eyes and your head and arms still tracked) or reach roleplay;
  a keyboard comes up whenever something takes typing. Players without VR get
  the wheel on a key.
- **Menus in the headset:** point at them with a controller and pull the
  trigger to click (the other trigger right-clicks). Setup has a VR page for
  playing left-handed, finger tracking, Body Bends, how heavy sabers feel,
  controller rumble and the holsters.
- **Comfort settings:** Setup > Controls has VR Movement and VR View (and VR
  Force). Turn in snaps (15 to 90 degrees, 45 as it comes) or smoothly at your own speed,
  dip the view to dark as you snap turn, darken the edges of your view while
  the game moves you (a comfort vignette, Low to High: the stick, smooth
  turning, jumps and falls), move where you look or where your hand points,
  swap the sticks, set the sticks' deadzone, play seated with your character
  standing, set your height and crouch for real, and recentre. Choose whether
  your view follows your head, the world's scale, how far away and how big
  the HUD and the menu screen are, how guns aim and tilt in your hand, and
  the headset's resolution.
- **Duels with a salute:** challenge someone to a private duel by saluting
  them with your lit saber; they accept by saluting back. Any number of duels
  can go on at once, each pair on its own, and bots accept challenges too.
- **Proximity voice chat:** talk to the players near you and hear them from
  where they stand.
- **Guns in your hand:** aim with your hand (the left one if you play
  left-handed), with a 3D aim dot.
- **Dedicated server** for Windows and Linux, so you can run your own.
- **Plays on a monitor too:** without a headset you get a borderless window,
  every resolution your monitor has (widescreen and ultrawide included) or a
  custom one, and a HUD, menus and field of view made for widescreen. With
  more than one monitor it opens where it was last time, and it fits the
  monitor you move it to, whatever its Windows scaling.

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
(`codemp/cgame/cg_vrbody.c`: body, arms, fingers, bending, solid blades and haptics;
`cg_vrholster.c`: holsters) and the server game (`codemp/game/g_vrsaber.c`: saber
combat; `g_vrrp.c`: roleplay; `g_vrdown.c`: down, not dead; `g_vrcomp.c`:
companions). The "HOW IT WORKS" section of
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
