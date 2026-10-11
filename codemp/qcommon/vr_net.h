/*
===========================================================================
OpenJK VR (multiplayer) - the saber pose that rides along with each usercmd.

Written by the client right after each usercmd it sends, only to servers
that put "sv_vrx" in their systeminfo, and then in clc_moveVR /
clc_moveNoDeltaVR messages, so the server always knows whether to read it.

Format (VR_POSE_PROTOCOL 1):
	1 bit		valid (nothing else follows if 0)
	16 bits		trackYaw
	1 bit		viewOffset follows (else it is zero)
	[48 bits	viewOffset x, y, z]
	per saber:	VR_POSE_MAX_BLADES bits of blade mask
				if any blade: grip x, y, z
	per blade:	base x, y, z
				dir pitch, yaw		16 bits each
Positions are 16 bits signed each, in 1/32 units.

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

static inline void VR_WriteUserPose( msg_t *msg, const vrUserPose_t *pose ) {
	if ( !pose || !pose->valid ) {
		MSG_WriteBits( msg, 0, 1 );
		return;
	}
	MSG_WriteBits( msg, 1, 1 );
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
	for ( int s = 0; s < VR_POSE_MAX_SABERS; s++ ) {
		const int mask = pose->bladeMask[s] & ( ( 1 << VR_POSE_MAX_BLADES ) - 1 );
		MSG_WriteBits( msg, mask, VR_POSE_MAX_BLADES );
		if ( !mask ) {
			continue;
		}
		VR_WritePoseCoord( msg, pose->grip[s][0] );
		VR_WritePoseCoord( msg, pose->grip[s][1] );
		VR_WritePoseCoord( msg, pose->grip[s][2] );
		for ( int b = 0; b < VR_POSE_MAX_BLADES; b++ ) {
			if ( !( mask & ( 1 << b ) ) ) {
				continue;
			}
			const vrBladePose_t *bp = &pose->blade[s][b];
			vec3_t angles;
			VR_WritePoseCoord( msg, bp->base[0] );
			VR_WritePoseCoord( msg, bp->base[1] );
			VR_WritePoseCoord( msg, bp->base[2] );
			vectoangles( bp->dir, angles );
			VR_WritePoseAngle( msg, angles[PITCH] );
			VR_WritePoseAngle( msg, angles[YAW] );
		}
	}
}

static inline void VR_ReadUserPose( msg_t *msg, vrUserPose_t *pose ) {
	Com_Memset( pose, 0, sizeof( *pose ) );
	if ( !MSG_ReadBits( msg, 1 ) ) {
		return;
	}
	pose->valid = qtrue;
	pose->trackYaw = VR_ReadPoseAngle( msg );
	if ( MSG_ReadBits( msg, 1 ) ) {
		pose->viewOffset[0] = VR_ReadPoseCoord( msg );
		pose->viewOffset[1] = VR_ReadPoseCoord( msg );
		pose->viewOffset[2] = VR_ReadPoseCoord( msg );
	}
	for ( int s = 0; s < VR_POSE_MAX_SABERS; s++ ) {
		pose->bladeMask[s] = MSG_ReadBits( msg, VR_POSE_MAX_BLADES ) & ( ( 1 << VR_POSE_MAX_BLADES ) - 1 );
		if ( !pose->bladeMask[s] ) {
			continue;
		}
		pose->grip[s][0] = VR_ReadPoseCoord( msg );
		pose->grip[s][1] = VR_ReadPoseCoord( msg );
		pose->grip[s][2] = VR_ReadPoseCoord( msg );
		for ( int b = 0; b < VR_POSE_MAX_BLADES; b++ ) {
			if ( !( pose->bladeMask[s] & ( 1 << b ) ) ) {
				continue;
			}
			vrBladePose_t *bp = &pose->blade[s][b];
			vec3_t angles;
			bp->base[0] = VR_ReadPoseCoord( msg );
			bp->base[1] = VR_ReadPoseCoord( msg );
			bp->base[2] = VR_ReadPoseCoord( msg );
			angles[PITCH] = VR_ReadPoseAngle( msg );
			angles[YAW] = VR_ReadPoseAngle( msg );
			angles[ROLL] = 0.0f;
			AngleVectors( angles, bp->dir, NULL, NULL );
		}
	}
}
