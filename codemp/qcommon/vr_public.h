/*
===========================================================================
OpenJK VR (multiplayer) - state the engine shares with the cgame module.

The engine owns the headset and controllers. A VR-aware cgame (the one that
ships with OpenJK VR) reads this every frame through
trap->ext.VR_GetClientState, after checking that the cvar "vr_engine" equals
VR_ENGINE_TAG (so it never calls into an engine that lacks the function).

Poses are in "VR render space": Quake axes (X forward, Y left, Z up), metres,
relative to the player's recentred head position, before the yaw rotation
into the game world. VR_ToWorld() below turns them into world coordinates
exactly the way the renderer places the eyes.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#pragma once

#define VR_ENGINE_TAG			"openjkvr-4"	// -3: ext.VR_G2SetModelHidden, -4: ext.VR_G2SetBoneTransform

enum {
	VR_HAND_LEFT = 0,
	VR_HAND_RIGHT = 1
};

typedef struct vrClientState_s {
	// headset running and tracked
	qboolean	active;

	// the usercmd angles come from the right hand's aim (ranged weapons),
	// so the cgame camera points where the gun points while the player
	// looks around freely
	qboolean	handAim;

	// tracking yaw (degrees) that corresponds to the game camera's yaw, and
	// game units per metre (same values the renderer uses)
	float		referenceYaw;
	float		worldScale;

	vec3_t		headOrigin;
	vec3_t		headAxis[3];

	// grip pose: centre of the palm, axis[0] along the held controller
	// aim pose: axis[0] along the pointing ray
	qboolean	handValid[2];
	vec3_t		gripOrigin[2];
	vec3_t		gripAxis[2][3];
	vec3_t		aimOrigin[2];
	vec3_t		aimAxis[2][3];
} vrClientState_t;

// Heading of a view axis; well defined even looking straight up/down.
// Shared by the engine, renderer and cgame so they always agree.
static inline float VR_AxisYaw( vec3_t axis[3] ) {
	const float hx = axis[2][2] * axis[0][0] - axis[0][2] * axis[2][0];
	const float hy = axis[2][2] * axis[0][1] - axis[0][2] * axis[2][1];
	return RAD2DEG( atan2f( hy, hx ) );
}

// VR render space -> game world, given the game camera (refdef) the frame is
// rendered for. Identical to what the renderer does for the eyes.
// (array parameters are not const so plain C callers don't need casts)
static inline void VR_ToWorld( const vrClientState_t *vr, const vec3_t camOrigin, vec3_t camAxis[3],
	const vec3_t origin, vec3_t axis[3], vec3_t outOrigin, vec3_t outAxis[3] ) {
	const float d = DEG2RAD( VR_AxisYaw( camAxis ) - vr->referenceYaw );
	const float c = cosf( d ), s = sinf( d );
	int i;

	if ( outAxis && axis ) {
		for ( i = 0; i < 3; i++ ) {
			outAxis[i][0] = axis[i][0] * c - axis[i][1] * s;
			outAxis[i][1] = axis[i][0] * s + axis[i][1] * c;
			outAxis[i][2] = axis[i][2];
		}
	}
	if ( outOrigin && origin ) {
		outOrigin[0] = camOrigin[0] + ( origin[0] * c - origin[1] * s ) * vr->worldScale;
		outOrigin[1] = camOrigin[1] + ( origin[0] * s + origin[1] * c ) * vr->worldScale;
		outOrigin[2] = camOrigin[2] + origin[2] * vr->worldScale;
	}
}
