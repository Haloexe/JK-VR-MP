/*
===========================================================================
OpenJK VR (multiplayer) - the saber pose that rides along with each usercmd.

Written by the client right after each usercmd it sends, only to servers
that put "sv_vrx" in their systeminfo, and then in clc_moveVR /
clc_moveNoDeltaVR messages, so the server always knows whether to read it.

Format (VR_POSE_PROTOCOL 3):
	1 bit		valid: a lit saber in the VR hands
	1 bit		avatar valid
	(nothing else follows if both are 0)
	16 bits		trackYaw
	1 bit		viewOffset follows (else it is zero)
	[48 bits	viewOffset x, y, z]
	if valid, per saber:
				VR_POSE_MAX_BLADES bits of blade mask
				if any blade: grip x, y, z
				              1 bit two-handed [grip2 x, y, z]
				per blade:	base x, y, z
							dir pitch, yaw		16 bits each
	if avatar valid: the avatar (below)
Positions are 16 bits signed each, in 1/32 units.

The avatar (also in svc_vrAvatars, which the server sends right after
svc_snapshot to clients that sent clc_moveVR: 8 bits count, then per player
8 bits client number and the avatar, its hands offsets from that player's
origin; with a usercmd they are offsets from the camera):
	16 bits		body yaw
	48 bits		head pitch, yaw, roll
	48 bits		shift x, y, z
	per hand (left, right):
				3 bits grip (VR_GRIP_*; nothing more for NONE)
				origin x, y, z
				axis as pitch, yaw, roll		16 bits each
				SABER, HILT: dir pitch, yaw
				HILT: 8 bits blend (0..255)

All fields are written as plain 16-bit values and sign-extended by hand on
reading, so whatever arrives decodes to the documented ranges.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#pragma once

#include "qcommon/vr_public.h"

static inline int VR_PoseQuantize( float f ) {
	if ( f != f ) {
		f = 0.0f;	// NaN
	}
	f = Com_Clamp( -VR_POSE_MAX_OFFSET, VR_POSE_MAX_OFFSET, f );
	return (int)floorf( f * 32.0f + 0.5f );
}

static inline void VR_WritePoseCoord( msg_t *msg, float f ) {
	MSG_WriteBits( msg, VR_PoseQuantize( f ) & 0xffff, 16 );
}

static inline float VR_ReadPoseCoord( msg_t *msg ) {
	int v = MSG_ReadBits( msg, 16 ) & 0xffff;
	if ( v & 0x8000 ) {
		v -= 0x10000;
	}
	return v * ( 1.0f / 32.0f );
}

static inline void VR_WritePoseAngle( msg_t *msg, float a ) {
	if ( a != a ) {
		a = 0.0f;	// NaN
	}
	MSG_WriteBits( msg, ANGLE2SHORT( a ) & 0xffff, 16 );
}

static inline float VR_ReadPoseAngle( msg_t *msg ) {
	return SHORT2ANGLE( MSG_ReadBits( msg, 16 ) & 0xffff );
}

static inline void VR_WritePoseDir( msg_t *msg, const vec3_t dir ) {
	vec3_t angles;
	vectoangles( dir, angles );
	VR_WritePoseAngle( msg, angles[PITCH] );
	VR_WritePoseAngle( msg, angles[YAW] );
}

static inline void VR_ReadPoseDir( msg_t *msg, vec3_t dir ) {
	vec3_t angles;
	angles[PITCH] = VR_ReadPoseAngle( msg );
	angles[YAW] = VR_ReadPoseAngle( msg );
	angles[ROLL] = 0.0f;
	AngleVectors( angles, dir, NULL, NULL );
}

static inline void VR_WriteAvatar( msg_t *msg, const vrAvatar_t *a ) {
	VR_WritePoseAngle( msg, a->bodyYaw );
	VR_WritePoseAngle( msg, a->headAngles[PITCH] );
	VR_WritePoseAngle( msg, a->headAngles[YAW] );
	VR_WritePoseAngle( msg, a->headAngles[ROLL] );
	VR_WritePoseCoord( msg, a->shift[0] );
	VR_WritePoseCoord( msg, a->shift[1] );
	VR_WritePoseCoord( msg, a->shift[2] );
	for ( int h = 0; h < 2; h++ ) {
		const vrAvatarHand_t *hand = &a->hand[h];
		const int grip = ( hand->grip > VR_GRIP_NONE && hand->grip < VR_NUM_GRIPS ) ? hand->grip : VR_GRIP_NONE;
		vec3_t axis[3], angles;
		MSG_WriteBits( msg, grip, 3 );
		if ( grip == VR_GRIP_NONE ) {
			continue;
		}
		VR_WritePoseCoord( msg, hand->origin[0] );
		VR_WritePoseCoord( msg, hand->origin[1] );
		VR_WritePoseCoord( msg, hand->origin[2] );
		VectorCopy( hand->axis[0], axis[0] );
		VectorCopy( hand->axis[1], axis[1] );
		VectorCopy( hand->axis[2], axis[2] );
		VR_AxisToAngles( axis, angles );
		VR_WritePoseAngle( msg, angles[PITCH] );
		VR_WritePoseAngle( msg, angles[YAW] );
		VR_WritePoseAngle( msg, angles[ROLL] );
		if ( grip == VR_GRIP_SABER || grip == VR_GRIP_HILT ) {
			VR_WritePoseDir( msg, hand->dir );
		}
		if ( grip == VR_GRIP_HILT ) {
			float b = hand->blend;
			if ( b != b ) {
				b = 0.0f;	// NaN
			}
			MSG_WriteBits( msg, (int)( Com_Clamp( 0.0f, 1.0f, b ) * 255.0f + 0.5f ), 8 );
		}
	}
}

static inline void VR_ReadAvatar( msg_t *msg, vrAvatar_t *a ) {
	Com_Memset( a, 0, sizeof( *a ) );
	a->valid = qtrue;
	a->bodyYaw = VR_ReadPoseAngle( msg );
	a->headAngles[PITCH] = VR_ReadPoseAngle( msg );
	a->headAngles[YAW] = VR_ReadPoseAngle( msg );
	a->headAngles[ROLL] = VR_ReadPoseAngle( msg );
	a->shift[0] = VR_ReadPoseCoord( msg );
	a->shift[1] = VR_ReadPoseCoord( msg );
	a->shift[2] = VR_ReadPoseCoord( msg );
	for ( int h = 0; h < 2; h++ ) {
		vrAvatarHand_t *hand = &a->hand[h];
		vec3_t angles;
		hand->grip = MSG_ReadBits( msg, 3 ) & 7;
		if ( hand->grip == VR_GRIP_NONE || hand->grip >= VR_NUM_GRIPS ) {
			hand->grip = VR_GRIP_NONE;	// (a writer never sends more for those)
			continue;
		}
		hand->origin[0] = VR_ReadPoseCoord( msg );
		hand->origin[1] = VR_ReadPoseCoord( msg );
		hand->origin[2] = VR_ReadPoseCoord( msg );
		angles[PITCH] = VR_ReadPoseAngle( msg );
		angles[YAW] = VR_ReadPoseAngle( msg );
		angles[ROLL] = VR_ReadPoseAngle( msg );
		AnglesToAxis( angles, hand->axis );
		if ( hand->grip == VR_GRIP_SABER || hand->grip == VR_GRIP_HILT ) {
			VR_ReadPoseDir( msg, hand->dir );
		}
		if ( hand->grip == VR_GRIP_HILT ) {
			hand->blend = MSG_ReadBits( msg, 8 ) * ( 1.0f / 255.0f );
		}
	}
}

static inline void VR_WriteUserPose( msg_t *msg, const vrUserPose_t *pose ) {
	const qboolean saber = ( pose && pose->valid ) ? qtrue : qfalse;
	const qboolean avatar = ( pose && pose->avatar.valid ) ? qtrue : qfalse;

	MSG_WriteBits( msg, saber, 1 );
	MSG_WriteBits( msg, avatar, 1 );
	if ( !saber && !avatar ) {
		return;
	}
	VR_WritePoseAngle( msg, pose->trackYaw );
	if ( VR_PoseQuantize( pose->viewOffset[0] ) || VR_PoseQuantize( pose->viewOffset[1] ) ||
		VR_PoseQuantize( pose->viewOffset[2] ) ) {
		MSG_WriteBits( msg, 1, 1 );
		VR_WritePoseCoord( msg, pose->viewOffset[0] );
		VR_WritePoseCoord( msg, pose->viewOffset[1] );
		VR_WritePoseCoord( msg, pose->viewOffset[2] );
	} else {
		MSG_WriteBits( msg, 0, 1 );
	}
	for ( int s = 0; saber && s < VR_POSE_MAX_SABERS; s++ ) {
		const int mask = pose->bladeMask[s] & ( ( 1 << VR_POSE_MAX_BLADES ) - 1 );
		MSG_WriteBits( msg, mask, VR_POSE_MAX_BLADES );
		if ( !mask ) {
			continue;
		}
		VR_WritePoseCoord( msg, pose->grip[s][0] );
		VR_WritePoseCoord( msg, pose->grip[s][1] );
		VR_WritePoseCoord( msg, pose->grip[s][2] );
		if ( pose->twoHanded[s] ) {
			MSG_WriteBits( msg, 1, 1 );
			VR_WritePoseCoord( msg, pose->grip2[s][0] );
			VR_WritePoseCoord( msg, pose->grip2[s][1] );
			VR_WritePoseCoord( msg, pose->grip2[s][2] );
		} else {
			MSG_WriteBits( msg, 0, 1 );
		}
		for ( int b = 0; b < VR_POSE_MAX_BLADES; b++ ) {
			if ( !( mask & ( 1 << b ) ) ) {
				continue;
			}
			const vrBladePose_t *bp = &pose->blade[s][b];
			VR_WritePoseCoord( msg, bp->base[0] );
			VR_WritePoseCoord( msg, bp->base[1] );
			VR_WritePoseCoord( msg, bp->base[2] );
			VR_WritePoseDir( msg, bp->dir );
		}
	}
	if ( avatar ) {
		VR_WriteAvatar( msg, &pose->avatar );
	}
}

static inline void VR_ReadUserPose( msg_t *msg, vrUserPose_t *pose ) {
	Com_Memset( pose, 0, sizeof( *pose ) );
	const qboolean saber = MSG_ReadBits( msg, 1 ) ? qtrue : qfalse;
	const qboolean avatar = MSG_ReadBits( msg, 1 ) ? qtrue : qfalse;
	if ( !saber && !avatar ) {
		return;
	}
	pose->valid = saber;
	pose->trackYaw = VR_ReadPoseAngle( msg );
	if ( MSG_ReadBits( msg, 1 ) ) {
		pose->viewOffset[0] = VR_ReadPoseCoord( msg );
		pose->viewOffset[1] = VR_ReadPoseCoord( msg );
		pose->viewOffset[2] = VR_ReadPoseCoord( msg );
	}
	for ( int s = 0; saber && s < VR_POSE_MAX_SABERS; s++ ) {
		pose->bladeMask[s] = MSG_ReadBits( msg, VR_POSE_MAX_BLADES ) & ( ( 1 << VR_POSE_MAX_BLADES ) - 1 );
		if ( !pose->bladeMask[s] ) {
			continue;
		}
		pose->grip[s][0] = VR_ReadPoseCoord( msg );
		pose->grip[s][1] = VR_ReadPoseCoord( msg );
		pose->grip[s][2] = VR_ReadPoseCoord( msg );
		if ( MSG_ReadBits( msg, 1 ) ) {
			pose->twoHanded[s] = qtrue;
			pose->grip2[s][0] = VR_ReadPoseCoord( msg );
			pose->grip2[s][1] = VR_ReadPoseCoord( msg );
			pose->grip2[s][2] = VR_ReadPoseCoord( msg );
		}
		for ( int b = 0; b < VR_POSE_MAX_BLADES; b++ ) {
			if ( !( pose->bladeMask[s] & ( 1 << b ) ) ) {
				continue;
			}
			vrBladePose_t *bp = &pose->blade[s][b];
			bp->base[0] = VR_ReadPoseCoord( msg );
			bp->base[1] = VR_ReadPoseCoord( msg );
			bp->base[2] = VR_ReadPoseCoord( msg );
			VR_ReadPoseDir( msg, bp->dir );
		}
	}
	if ( avatar ) {
		VR_ReadAvatar( msg, &pose->avatar );
	}
}
