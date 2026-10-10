OpenJK VR - Jedi Academy multiplayer in a VR headset
=====================================================

An OpenJK-based multiplayer client that renders Jedi Academy in stereo 3D
on a PC VR headset (OpenXR), with head tracking. This is milestone 1:

  * Stereo 3D with the headset's own field of view and lens geometry
  * Head tracking (rotation + limited positional movement)
  * Head-aim: you aim where you look; the mouse turns your body
  * HUD on a floating plane in front of you
  * Menus, console and loading screens on a floating screen
  * The desktop window mirrors the left eye

Not yet: motion controllers (hand-tracked guns / saber). Play with keyboard
and mouse (or a gamepad) for now.


INSTALL / RUN
-------------
The OpenJK-VR folder goes inside Jedi Academy's GameData folder:

  ...\Jedi Academy\GameData\OpenJK-VR\

It is self-contained: the original game files (jamp.exe, base\*.pk3, your
saves and configs) are not touched. To uninstall, delete the folder.

1. Connect your headset and start its PC software:
     - Valve Index / HTC Vive / WMR:  SteamVR
     - Meta Quest (Link, Air Link):   Meta Quest Link app, or SteamVR
     - Virtual Desktop:               works through its OpenXR runtime
   The headset software must be the "active OpenXR runtime"
   (SteamVR: Settings > OpenXR > Set SteamVR as OpenXR Runtime).
   Windows Mixed Reality's own runtime has no OpenGL support: use SteamVR.
2. Double-click "Launch OpenJK VR.bat".
3. Click the game window once so it has keyboard/mouse focus.

If no headset is found the game starts in normal desktop mode.


CONTROLS
--------
Look around with your head; you aim where you look. The mouse turns your
body left/right (mouse up/down does nothing in VR). Everything else is the
normal Jedi Academy keyboard layout.

Console (Shift+~) commands:
  vr_recenter        re-centre your position and the menu screen
  vr_status          print headset / runtime information


SETTINGS (console cvars, saved automatically)
---------------------------------------------
  vr_enabled 1           0 = desktop mode (then: vid_restart)
  vr_resolutionScale 1   eye resolution vs. headset recommendation (vid_restart)
  vr_worldScale 34       game units per metre (bigger = world feels smaller)
  vr_positional 1        camera follows your head movement
  vr_positionalMax 0.25  ...up to this many metres
  vr_hudFov 50           HUD size (vertical degrees)
  vr_hudDepth 3          HUD distance (metres)
  vr_menuDistance 3      menu screen distance (metres)
  vr_menuHeight 2.2      menu screen height (metres)
  vr_headAim 1           0 = aim with the mouse only; head just looks around
  vr_mirror 1            show the left eye in the desktop window

On first start the VR build turns off view bobbing (cg_bobPitch/Roll/Up).
Settings and downloads live in Documents\My Games\OpenJK-VR, separate from
regular OpenJK.

Comfort tip: with the lightsaber out, multiplayer switches to a third-person
camera. Typing "cg_fpls 1" in the console keeps a first-person view instead
(put it in base\autoexec.cfg inside Documents\My Games\OpenJK-VR to make it
stick). Third person works too, but first person is usually more comfortable.


KNOWN LIMITATIONS (milestone 1)
-------------------------------
  * Keyboard/mouse only - no motion controllers yet.
  * Full-screen 2D effects (damage flash, fades, scope overlay) only cover
    the HUD rectangle.
  * Things near the edge of your view can pop in (the game culls with its
    own, narrower field of view).
  * Name tags and the third-person dynamic crosshair are drawn at the
    game's field of view and may not line up with the 3D scene.
  * On emplaced guns and vehicles the game limits how far you can turn;
    head yaw beyond that limit is ignored.
  * Video recording (/video) records black frames in VR.
  * Mods that ship their own client DLL (cgame) need an x86_64 build of it;
    servers running server-side mods (JA+, base, etc.) work.
  * rd-vanilla only (the experimental rend2 renderer has no VR support).


HOW IT WORKS (for modders)
--------------------------
VR lives entirely in the engine (openjk.x86_64.exe) and the renderer
(rd-vanilla_x86_64.dll); the cgame/ui/game DLLs are stock OpenJK. That is
why it works on ordinary servers. Source files:

  codemp/client/cl_vr.cpp      OpenXR session, frame loop, tracking, head-aim
  codemp/rd-common/tr_vr.h     state shared with the renderer
  codemp/rd-vanilla/tr_scene.cpp   per-eye camera, HUD placement
  codemp/rd-vanilla/tr_main.cpp    asymmetric per-eye projection
  shared/sdl/sdl_window.cpp    session creation after the GL context
  vr/build-win64.sh            how these binaries were built

Build (Linux, cross-compiling for Windows with llvm-mingw):  vr/build-win64.sh
Build (Windows, Visual Studio 2022): run CreateVisualStudio2022Projects.bat,
build the "openjk.x86_64" project (x64, Release), and copy
lib/openxr/bin/x64/libopenxr_loader.dll next to the exe.

Licence: GPLv2 (OpenJK). The OpenXR session code started from Team Beef's
JKXR (GPLv2). The OpenXR loader is Apache-2.0 (lib/openxr/LICENSE).
