/*
===========================================================================
OpenJK VR (multiplayer)

Shared state between the engine's OpenXR layer (client/cl_vr.cpp) and the
renderer. The engine owns the OpenXR session and fills this struct once per
eye; the renderer only reads it, so any cgame/ui module (base or mod) works
unmodified.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#pragma once

typedef struct vrRenderState_s {
	// True only while the engine is rendering a frame for the headset.
	qboolean	active;

	// True while the engine has its own framebuffer bound (always during
	// active, and also for the flat fallback view while the headset is idle).
	// The renderer must not touch glDrawBuffer or present the window then.
	qboolean	offscreen;

	// True when the frame is a flat "screen" (menus, console, loading,
	// connecting). The renderer then behaves exactly like desktop OpenJK and
	// the engine shows the result on a floating quad.
	qboolean	screenLayer;

	// Eye currently being rendered (0 = left, 1 = right).
	int			eye;

	// Per-eye projection, as tangents of the half-angles (left/down negative).
	float		tanLeft[2], tanRight[2], tanUp[2], tanDown[2];

	// Per-eye orientation in tracking space, Quake convention
	// (axis[0] forward, axis[1] left, axis[2] up).
	vec3_t		eyeAxis[2][3];

	// Per-eye position in tracking space, Quake convention, metres, relative
	// to the recentred origin (positional tracking already clamped).
	vec3_t		eyeOrigin[2];

	// Tracking-space yaw (degrees) that corresponds to the game's view yaw.
	// The renderer rotates the eye poses by (gameYaw - referenceYaw).
	float		referenceYaw;

	// Game units per metre.
	float		worldScale;

	// The player is zoomed (disruptor scope, binoculars); the renderer then
	// magnifies the eye image by normalFov / refdef fov.
	qboolean	zoomed;
	float		normalFov;

	// Rectangle (eye-buffer pixels, origin top-left) the 640x480 2D layer
	// (HUD, crosshair, scoreboard) is mapped into, per eye.
	int			hudX[2], hudY[2], hudW[2], hudH[2];

	// The HUD on your wrist: true while the frame being rendered is its own
	// (once more after the eyes, for the status alone). The 2D layer is then
	// a wristWidth x wristHeight picture of its own, bound by the engine and
	// cleared to nothing (alpha 0): all the width of the 640x480 screen, from
	// wristTop to wristBottom of its height, what's drawn widened toward where
	// it's anchored (RE_Anchor2D) to stay in proportion - its alpha what
	// covers what's behind, its colour premultiplied by it. No scene is drawn
	// in it.
	qboolean	wristPass;
	int			wristWidth, wristHeight;
	float		wristTop, wristBottom;
} vrRenderState_t;
