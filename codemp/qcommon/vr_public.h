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

#define VR_ENGINE_TAG			"openjkvr-8"	// -3: ext.VR_G2SetModelHidden, -4: ext.VR_G2SetBoneTransform,
												// -5: ext.VR_SetUserPose, ext.VR_Haptic,
												// -6: ext.VR_SetGrabZones, vrClientState_t.grabbing,
												// -7: vrUserPose_t.avatar, ext.VR_SetAvatar (game),
												//     ext.VR_GetAvatar (cgame)
												// -8: ext.R_Anchor2D, ext.R_Aspect2D (cgame, ui:
												//     widescreen 2D)

// ext.R_Anchor2D( anchorX ) with anchorX this far left or further: what's
// drawn is stretched across the window again, as always (any other x is a
// place to squeeze toward, off the screen too: a point in the world there)
#define VR_ANCHOR2D_STRETCH		-100000.0f

/*
--------------------------------------------------------------------------------
Physical lightsaber: where the blades you hold really are

Every usercmd a VR client sends carries the pose of the saber blades in its
hands, so the server can fight with them (to servers that put "sv_vrx" with
VR_POSE_PROTOCOL in their systeminfo; see qcommon/vr_net.h). The server
engine hands it to the game module through gameImport_t.ext.VR_GetUserPose.

Positions are world-axis offsets from the camera the player sees the hands
from (cgame refdef.vieworg). viewOffset says where that camera is: its offset
from the eye point, which is the origin plus viewheight of the player state
the pose was rendered with (on the server: before the Pmove of the command
that carries it); it is only the camera's smoothing (crouching, stairs,
prediction errors), so it is usually zero. Directions are world-axis unit
vectors along the blades. trackYaw is the yaw that turns the player's real
room (tracking space) into the world, so the server can tell real arm
movement from snap/smooth turning, running and the camera's smoothing.

valid means a lit saber is in the player's VR hands: the arm swings it and
the attack button doesn't. Its blades can still be missing for a moment (a
flip or roll animates the body, a hitch delayed the pose).

twoHanded: the other hand holds that saber's hilt too (grip2 is that hand).
--------------------------------------------------------------------------------
*/
#define VR_POSE_PROTOCOL		3		// 2: twoHanded, grip2; 3: avatar, svc_vrAvatars
#define VR_POSE_MAX_SABERS		2		// right hand, left hand
#define VR_POSE_MAX_BLADES		2		// per saber (staffs have 2)
#define VR_POSE_MAX_OFFSET		1000.0f	// units (16-bit fixed point, 1/32 unit)

typedef struct vrBladePose_s {
	vec3_t			base;		// blade start (the muzzle point), offset from the camera
	vec3_t			dir;		// unit vector along the blade
} vrBladePose_t;

/*
--------------------------------------------------------------------------------
The VR avatar: what everyone else sees of a VR player - which way their body
faces, where they look, and their hands, holding what.

The player's game sends it with every usercmd (vrUserPose_t.avatar, offsets
from the camera like the saber pose; valid whenever their VR body is drawn,
saber or not). The server's game keeps the hands within reach and makes them
offsets from the player's origin as that usercmd found it (where their body
was when they saw their hands), so the hands move with the body, and hands
it to the engine (gameImport_t.ext.VR_SetAvatar), which sends it after each
snapshot to the other players that run this build (svc_vrAvatars, only to
clients that sent clc_moveVR). Their games pose that player's body with it,
the hands offset from where they draw the player
(cgameImport_t.ext.VR_GetAvatar). Directions are world axes.
--------------------------------------------------------------------------------
*/
enum {
	VR_GRIP_NONE,		// the hand isn't on a controller: the animation has it
	VR_GRIP_FREE,		// an empty hand on the controller
	VR_GRIP_GUN,		// holding a gun: origin and axis are the gun model's
	VR_GRIP_SABER,		// holding its own saber: dir runs along the blade
	VR_GRIP_HILT,		// on the other hand's hilt: dir runs along it through the fist
	VR_NUM_GRIPS
};

typedef struct vrAvatarHand_s {
	int			grip;			// VR_GRIP_*
	vec3_t		origin;			// where the hand holds (the controller's grip as the arm reaches for it); GUN: the gun's origin
								// (offset from the camera, then from the player's origin: see above)
	vec3_t		axis[3];		// the controller as the hand lies on it (forward along it, left, up); GUN: the gun's axes
	vec3_t		dir;			// SABER: along the blade; HILT: along the hilt, toward the thumb
	float		blend;			// HILT: how far the hold has taken the hand over (0..1)
} vrAvatarHand_t;

typedef struct vrAvatar_s {
	qboolean		valid;
	float			bodyYaw;		// where the body faces (world yaw)
	vec3_t			headAngles;		// where the head looks (pitch, yaw, roll)
	vec3_t			shift;			// how far the body is drawn from the entity origin (stood under the head)
	vrAvatarHand_t	hand[2];		// VR_HAND_LEFT, VR_HAND_RIGHT
} vrAvatar_t;

typedef struct vrUserPose_s {
	qboolean		valid;		// a lit saber is in the player's VR hands
	float			trackYaw;	// world yaw minus tracking yaw (degrees)
	vec3_t			viewOffset;	// camera minus eye point
	int				bladeMask[VR_POSE_MAX_SABERS];	// blades with a pose (bit per blade)
	vec3_t			grip[VR_POSE_MAX_SABERS];		// the hand holding the saber, offset from the camera
	qboolean		twoHanded[VR_POSE_MAX_SABERS];	// the other hand holds it too...
	vec3_t			grip2[VR_POSE_MAX_SABERS];		// ...here (offset from the camera)
	vrBladePose_t	blade[VR_POSE_MAX_SABERS][VR_POSE_MAX_BLADES];
	vrAvatar_t		avatar;		// the whole player, for everyone else (offsets from the camera)
} vrUserPose_t;

// Angles that AnglesToAxis turns back into axis (forward, left, up).
static inline void VR_AxisToAngles( vec3_t axis[3], vec3_t angles ) {
	const float len = sqrtf( axis[0][0] * axis[0][0] + axis[0][1] * axis[0][1] );

	if ( len > 1e-5f ) {
		angles[PITCH] = RAD2DEG( atan2f( -axis[0][2], len ) );
		angles[YAW] = RAD2DEG( atan2f( axis[0][1], axis[0][0] ) );
		angles[ROLL] = RAD2DEG( atan2f( axis[1][2], axis[2][2] ) );
	} else {
		// straight up or down: yaw and roll are the same turn, all in roll
		const float sp = ( axis[0][2] < 0.0f ) ? 1.0f : -1.0f;
		angles[PITCH] = sp * 90.0f;
		angles[YAW] = 0.0f;
		angles[ROLL] = RAD2DEG( atan2f( axis[1][0] * sp, axis[1][1] ) );
	}
}

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

	// the hand's grip button is held as a grab (it was pressed while the
	// cgame said the hand could grab something, ext.VR_SetGrabZones), so it
	// didn't press its bound key
	qboolean	grabbing[2];
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
