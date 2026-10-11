OpenJK VR - Jedi Academy multiplayer in a VR headset
=====================================================

An OpenJK-based multiplayer client that renders Jedi Academy in stereo 3D
on a PC VR headset (OpenXR). Current state (milestone 2):

  * Stereo 3D with the headset's own field of view and lens geometry
  * Head tracking (rotation + limited positional movement)
  * VR controllers: thumbstick movement, snap/smooth turning, buttons
  * Guns are held in and aimed with your right hand, with a 3D aim dot
  * Your own body in first person: it stands under you, turns with you,
    and its arms and hands follow your controllers, gripping what they
    hold (works with any player model on the standard skeleton, custom
    models included)
  * First-person lightsaber: the hilt in your hand, the blade where your
    controller points
  * Saber, fists and scopes aim where you look
  * HUD on a floating plane in front of you
  * Menus, console and loading screens on a floating screen
  * The desktop window mirrors the left eye

Not yet: other players seeing your arm movements, shots leaving the gun
barrel and saber hits where your blade really is (all need the VR server
mod), two-handed weapons.


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


CONTROLS (Quest / Touch layout; other controllers are similar)
--------
  Left stick           move (in the direction you look)
  Left stick click     walk (hold)
  Right stick L/R      turn (snap 45 degrees; vr_turnMode 1 = smooth)
  Right stick up/down  next / previous weapon
  Right stick click    change saber style
  Right trigger        attack
  Left trigger         alternate attack
  Right grip           use (doors, switches, pick-ups)
  Left grip            use the selected Force power
  A / B                jump / crouch
  X / Y                previous / next Force power
  Left menu button     game menu (use the mouse in menus for now)

With a gun out you aim with your right hand: the gun is in your hand (the
complete third-person model, so it looks right from every angle) and a
small crosshair floats where your shots will land. With the saber, fists or
a scope you aim where you look. Keyboard and mouse still work; the mouse
turns your body.

YOUR BODY: look down and you see your character. Its hands are on your
controllers, its elbows bend on their own, its wrists bend, and its torso
turns with your head (it follows once you look more than 45 degrees to the
side, and straightens out while you walk). The head is hidden so it never
gets in your eyes. Your character's model is measured every frame, so a
small or a tall model, the stock characters or a custom one all get arms
that fit. A gun sits in the right hand exactly the way the game holds it
in third person, fingers around the grip. When you reach further than your
character's arm, the body leans in a little so the hand still gets there.

LIGHTSABER: with the saber out you stay in first person. The hilt is in
your right hand (a second saber in your left) and the blade points along
your fist, the way you would hold a real hilt, so it follows every turn
of your wrist. Attacks, blocks and Force powers work as usual; for now the
game still decides hits from its own swing animations (the VR server mod
will make your real blade count). During flips, cartwheels and rolls your
body and blade are hidden for that moment. Fists, saber locks, knockdowns
and vehicles still use the third-person camera.

The controller buttons are ordinary keys named AUX16-AUX27, so you can
rebind them in the console, e.g.  bind AUX23 force_throw
  AUX16 right trigger   AUX20 A   AUX24 left stick click
  AUX17 left trigger    AUX21 B   AUX25 right stick click
  AUX18 right grip      AUX22 X   AUX26 right stick up
  AUX19 left grip       AUX23 Y   AUX27 right stick down

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
  vr_handAim 1           0 = guns aim with your head too
  vr_turnMode 0          0 = snap turn, 1 = smooth turn
  vr_turnAngle 45        snap turn angle
  vr_turnSpeed 180       smooth turn speed (degrees per second)
  vr_moveDirection 0     1 = move where your left hand points
  vr_deadzone 0.2        thumbstick deadzone
  cg_vrAimDot 1          the floating crosshair (cg_vrAimDotSize to resize)
  cg_vrGunX/Y/Z 0        nudge the gun in your hand (forward/left/up units)
  cg_vrWorldGun 1        0 = hold the old first-person gun models instead
  vr_gunPitch 0          tilt the gun (and its aim) in your hand, + = up
  cg_vrBody 1            0 = no body in first person (classic view)
  cg_vrBodyArms 1        0 = body without tracked arms (animated arms)
  cg_vrBodyBack 4        how far (units) the body stands behind your eyes;
                         smaller = closer to you, but you may see the neck
                         when looking straight down
  cg_vrBodyTwist 45      how far you can turn your head before the torso
                         follows (degrees)
  cg_vrHandX/Y/Z 0       nudge an empty hand or the saber hilt on the
                         controller (forward/inward/up units); a gun's
                         grip follows cg_vrGunX/Y/Z
  cg_vrHandPitch 0       tilt an empty hand on the controller (degrees)
  cg_vrBodyWrist 1       0 = stiff wrists (the forearm turns the hand)
  cg_vrSaber 1           0 = saber in third person, as in the stock game
  cg_vrSaberPitch 0      tilt the blade forward from your fist (degrees)
  cg_vrBodyDebug 0       1 = log what the body found on your model,
                         2 = also log how exactly the hand grips the gun,
                         3 = also show the body's own copy of the gun

On first start the VR build turns off view bobbing (cg_bobPitch/Roll/Up);
the camera dip on landing is also skipped in the headset.
Settings and downloads live in Documents\My Games\OpenJK-VR, separate from
regular OpenJK.

Camera: guns, the lightsaber and force powers are first person. The game
itself switches to a third-person camera (behind your character) with fists
out, in saber locks, while knocked down, in vehicles, on emplaced guns,
while watching another player and when dead.
Head tracking still works there: you look around from the third-person
camera. "cg_thirdPerson 1" gives third person all the time.


KNOWN LIMITATIONS
-----------------
  * Shots on normal servers still start at your eye, not the gun barrel
    (they go exactly where the aim dot shows). The VR server mod will fire
    them from the gun.
  * Other players see an animated character facing where your gun points;
    your tracked arms and head are only on your own screen until the VR
    server mod sends them.
  * The game picks your leg animation from the gun's direction, so with
    the gun pointed far to the side of where you walk, the legs may strafe.
  * If your real arm reaches much further than the character's, the hand
    stops at full stretch while the gun stays in your real hand.
  * The lightsaber blade you see follows your hand, but hits and blocks
    still come from the game's swing animations on normal servers.
  * Two-handed weapons are held in the right hand only (for now).
  * Mirrors show your character without a head (it is hidden for your view).
  * Menus need the mouse (no laser pointer yet).
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
VR lives in the engine (openjk.x86_64.exe), the renderer
(rd-vanilla_x86_64.dll) and the client game module (base\cgamex86_64.dll,
for the gun in your hand and your body). The server side is stock OpenJK,
which is why it works on ordinary servers. Source files:

  codemp/client/cl_vr.cpp      OpenXR session, frame loop, tracking, aim
  codemp/qcommon/vr_public.h   state the engine shares with the cgame
  codemp/rd-common/tr_vr.h     state shared with the renderer
  codemp/rd-vanilla/tr_scene.cpp   per-eye camera, HUD placement
  codemp/rd-vanilla/tr_main.cpp    asymmetric per-eye projection
  codemp/cgame/cg_vr.c         hands, aim dot
  codemp/cgame/cg_vrbody.c     your body: placement, facing, arm IK
  shared/sdl/sdl_window.cpp    session creation after the GL context
  vr/build-win64.sh            how these binaries were built

The arm IK re-aims the upper arm and forearm with Ghoul2 bone overrides
after the game's animation, and turns the hand at the wrist with a new
engine bone transform (G2API_SetBoneVRTransform), so any model on the
_humanoid skeleton works. cg_vrBodyDebug 1 prints what the solver's
self-checks find on your model.

Build (Linux, cross-compiling for Windows with llvm-mingw):  vr/build-win64.sh
Build (Windows, Visual Studio 2022): run CreateVisualStudio2022Projects.bat,
build the "openjk.x86_64" project (x64, Release), and copy
lib/openxr/bin/x64/libopenxr_loader.dll next to the exe.

Licence: GPLv2 (OpenJK). The OpenXR session code started from Team Beef's
JKXR (GPLv2). The OpenXR loader is Apache-2.0 (lib/openxr/LICENSE).
