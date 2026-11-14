/*
===========================================================================
OpenJK VR (multiplayer) - driving an AT-ST from its cockpit in VR: what the
server (g_vrwalker.c, WalkerNPC.c, g_weapon.c) and the driver's own game
(cg_vrwalker.c: the cockpit, its yoke, the view from it) go by alike.

A VR player driving an AT-ST sits in its head, in the cockpit (the AT-ST
Cockpit Test study's: vr/atst), and drives it with the yoke in front of
them - pushed forward it walks, pulled back it backs up, twisted it turns -
or the sticks; their view only looks round, and aims its guns. On a server
of this build that lets them ("vrWalker 1" in its serverinfo: g_vrWalker)
their commands say so (BUTTON_VR_YOKE; how far the yoke's twisted, their
rightmove: + right, - left), and then:
- it turns as the yoke has it, standing too (VR_WALKER_STAND_TURN as fast),
  never toward where they look (WalkerNPC.c);
- its guns aim where they look, from the cockpit's eyes - as far round from
  its front as the AT-ST lets its driver look (its lookYaw, lookPitch) -
  as the game's crosshair shows (g_weapon.c, cg_draw.c).
Anywhere else they drive it as the game has it: it turns toward where they
look, its guns fire where it faces.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#ifndef BG_VRWALKER_H
#define BG_VRWALKER_H

#define BUTTON_VR_YOKE			BUTTON_VR_WALLGRIP	// (the same bit: in a vehicle, never a hand on a wall - cg_vrwall.c)

// The cockpit's eyes from an AT-ST's origin, standing, facing its way: forward,
// left, up (vr/atst/make_cockpit.py, its tags: tag_eye from tag_origin)
#define VR_WALKER_EYE_FWD		40.645f
#define VR_WALKER_EYE_LEFT		13.15f
#define VR_WALKER_EYE_UP		207.2f

// How fast the yoke turns it, twisted all the way: of its turningSpeed each
// 60th of a second - as fast as it turns toward a mouse's view at full speed
// (WalkerYawAdjust: turningSpeed * 1.5, of which a fifth) - and standing,
// this much of that
#define VR_WALKER_TURN			0.3f
#define VR_WALKER_STAND_TURN	0.5f

// Whether a vehicle's one a VR driver drives from the cockpit: an AT-ST (a
// walker whose model is the game's "atst" - the cockpit's fitted to its head).
static QINLINE qboolean BG_VRWalkerCockpit( const vehicleInfo_t *info ) {
	return (qboolean)( info && info->type == VH_WALKER && info->model && !Q_stricmp( info->model, "atst" ) );
}

// The cockpit's eyes in the world: an AT-ST's at origin, facing yaw.
static QINLINE void BG_VRWalkerEye( const vec3_t origin, float yaw, vec3_t eye ) {
	const float c = cosf( DEG2RAD( yaw ) ), s = sinf( DEG2RAD( yaw ) );

	eye[0] = origin[0] + c * VR_WALKER_EYE_FWD - s * VR_WALKER_EYE_LEFT;
	eye[1] = origin[1] + s * VR_WALKER_EYE_FWD + c * VR_WALKER_EYE_LEFT;
	eye[2] = origin[2] + VR_WALKER_EYE_UP;
}

// Which way its guns aim from there: where the driver looks (viewangles), as
// far round from its front (walkerYaw) as it lets them look, and up and down.
static QINLINE void BG_VRWalkerAim( const vec3_t viewangles, float walkerYaw, float lookYaw, float lookPitch, vec3_t dir ) {
	vec3_t a;
	float yaw = AngleSubtract( viewangles[YAW], walkerYaw );
	float pitch = AngleNormalize180( viewangles[PITCH] );

	if ( lookYaw > 0.0f ) {
		yaw = Com_Clamp( -lookYaw, lookYaw, yaw );
	}
	if ( lookPitch > 0.0f ) {
		pitch = Com_Clamp( -lookPitch, lookPitch, pitch );
	}
	a[PITCH] = pitch;
	a[YAW] = walkerYaw + yaw;
	a[ROLL] = 0.0f;
	AngleVectors( a, dir, NULL, NULL );
}

#endif	// BG_VRWALKER_H
