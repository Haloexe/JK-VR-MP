/*
===========================================================================
OpenJK VR (multiplayer) - your fingers from their joints, where the runtime
tracks the joints of your hands (XR_EXT_hand_tracking: client/cl_vr.cpp,
vr_fingerJoints - SteamVR does, from the finger sensors of the Valve
Index's controllers and the Steam Frame's).

The humanoid skeleton every model shares moves three fingers a hand
(VR_FINGER_*, qcommon/vr_public.h). From the joints each is closed this far:

  the index finger  its three joints - the knuckle, the middle one, the
                    last - bent toward the palm, added up: none of it,
                    straight (0); VR_JOINT_FIST degrees, a fist (1)
  the other three   the middle, ring and little fingers each the same, and
                    the three of them as one (the skeleton bends them
                    together)
  the thumb         where it points: out to the thumb's side of the hand,
                    up (0) - a thumbs up, an open hand's; along the index
                    finger, or across the fingers, down (1)

Only how the joints lie to each other counts, not where the hand is or how
it's turned. The values aren't clamped: a finger bent back, or a fist
tighter than VR_JOINT_FIST, goes a little outside 0..1 (the log says how far
your runtime's hands go: cg_vrbody.c), and VR_FingerCurls clamps them
(qcommon/vr_fingers.h).

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#pragma once

#include "qcommon/vr_public.h"

// OpenXR's joints (XrHandJointEXT): the wrist, then each finger's from the
// hand out
#define VR_JOINTS				26		// (XR_HAND_JOINT_COUNT_EXT)
#define VR_JOINT_WRIST			1
#define VR_JOINT_THUMB			2		// the thumb's metacarpal, then its proximal, distal and tip
#define VR_JOINT_INDEX			6		// each finger's metacarpal, then its proximal, intermediate, distal and tip
#define VR_JOINT_MIDDLE			11
#define VR_JOINT_RING			16
#define VR_JOINT_LITTLE			21

#define VR_JOINT_STRAIGHT		10.0f	// a finger's joints bent this far, added up (degrees): straight...
#define VR_JOINT_FIST			230.0f	// ...this far: a fist
#define VR_JOINT_THUMB_UP		0.7f	// the thumb pointing this far out to its side of the hand: up...
#define VR_JOINT_THUMB_DOWN		0.0f	// ...this far: down
#define VR_JOINT_SHORTEST		0.002f	// a bone shorter than this (metres) isn't one
#define VR_JOINT_FURTHEST		1000.0f	// a joint further than this from the space's origin (metres) isn't one

// How far the finger whose metacarpal is joint first bends toward the palm at
// its three joints, added up (degrees; palm: out of the palm). *ok goes qfalse
// if it can't be told.
static inline float VR_JointBend( vec3_t pos[VR_JOINTS], int first, const vec3_t palm, qboolean *ok ) {
	vec3_t bone[4], side;
	float bend = 0.0f;
	int i;

	for ( i = 0; i < 4; i++ ) {
		VectorSubtract( pos[first + i + 1], pos[first + i], bone[i] );
		if ( VectorNormalize( bone[i] ) < VR_JOINT_SHORTEST ) {
			// (a metacarpal a runtime leaves where its knuckle is: from the wrist)
			if ( i == 0 ) {
				VectorSubtract( pos[first + 1], pos[VR_JOINT_WRIST], bone[0] );
				if ( VectorNormalize( bone[0] ) >= VR_JOINT_SHORTEST ) {
					continue;
				}
			}
			*ok = qfalse;
			return 0.0f;
		}
	}

	// it bends about the line across it: across its metacarpal, which lies
	// in the palm, square to the palm
	CrossProduct( bone[0], palm, side );
	if ( VectorNormalize( side ) < 0.1f ) {
		*ok = qfalse;
		return 0.0f;
	}
	for ( i = 1; i < 4; i++ ) {
		vec3_t a, b, c;

		VectorMA( bone[i - 1], -DotProduct( bone[i - 1], side ), side, a );
		VectorMA( bone[i], -DotProduct( bone[i], side ), side, b );
		CrossProduct( a, b, c );
		bend += atan2f( DotProduct( c, side ), DotProduct( a, b ) );	// (toward the palm: more)
	}
	return RAD2DEG( bend );
}

// How far each finger is closed (VR_FINGER_*: 0..1, not clamped) from where
// the joints of a hand are (pos: OpenXR's joints, in metres, in any of its
// spaces - their axes go as OpenXR's do, a right-handed set); right: it's the
// right hand. qfalse: they don't make a hand.
static inline qboolean VR_JointCurls( vec3_t pos[VR_JOINTS], int right, float out[VR_NUM_FINGERS] ) {
	static const int others[3] = { VR_JOINT_MIDDLE, VR_JOINT_RING, VR_JOINT_LITTLE };
	vec3_t index, little, palm, along, toThumb, thumb;
	qboolean ok = qtrue;
	float sum = 0.0f;
	int i, k;

	for ( i = VR_JOINT_WRIST; i < VR_JOINTS; i++ ) {
		for ( k = 0; k < 3; k++ ) {
			if ( !( fabsf( pos[i][k] ) < VR_JOINT_FURTHEST ) ) {
				return qfalse;	// (and NaN)
			}
		}
	}

	// the palm: out of it (the knuckles of the index and little fingers from
	// the wrist, across each other - the other way round for the left hand);
	// along it from the wrist to the middle finger's knuckle; across it from
	// the little finger's knuckle to the index finger's - toward the thumb
	VectorSubtract( pos[VR_JOINT_INDEX + 1], pos[VR_JOINT_WRIST], index );
	VectorSubtract( pos[VR_JOINT_LITTLE + 1], pos[VR_JOINT_WRIST], little );
	CrossProduct( index, little, palm );
	if ( !right ) {
		VectorScale( palm, -1.0f, palm );
	}
	if ( VectorNormalize( palm ) < VR_JOINT_SHORTEST * VR_JOINT_SHORTEST ) {
		return qfalse;
	}
	VectorSubtract( pos[VR_JOINT_MIDDLE + 1], pos[VR_JOINT_WRIST], along );
	VectorMA( along, -DotProduct( along, palm ), palm, along );
	if ( VectorNormalize( along ) < VR_JOINT_SHORTEST ) {
		return qfalse;
	}
	VectorSubtract( pos[VR_JOINT_INDEX + 1], pos[VR_JOINT_LITTLE + 1], toThumb );
	VectorMA( toThumb, -DotProduct( toThumb, palm ), palm, toThumb );
	VectorMA( toThumb, -DotProduct( toThumb, along ), along, toThumb );
	if ( VectorNormalize( toThumb ) < VR_JOINT_SHORTEST ) {
		return qfalse;
	}

	// the index finger, and the other three as one
	out[VR_FINGER_INDEX] = ( VR_JointBend( pos, VR_JOINT_INDEX, palm, &ok ) - VR_JOINT_STRAIGHT ) /
		( VR_JOINT_FIST - VR_JOINT_STRAIGHT );
	for ( i = 0; i < 3; i++ ) {
		sum += VR_JointBend( pos, others[i], palm, &ok );
	}
	out[VR_FINGER_OTHERS] = ( sum / 3.0f - VR_JOINT_STRAIGHT ) / ( VR_JOINT_FIST - VR_JOINT_STRAIGHT );

	// the thumb: where it points from its first knuckle to its tip
	VectorSubtract( pos[VR_JOINT_THUMB + 3], pos[VR_JOINT_THUMB + 1], thumb );
	if ( VectorNormalize( thumb ) < VR_JOINT_SHORTEST ) {
		return qfalse;
	}
	out[VR_FINGER_THUMB] = ( VR_JOINT_THUMB_UP - DotProduct( thumb, toThumb ) ) / ( VR_JOINT_THUMB_UP - VR_JOINT_THUMB_DOWN );
	return ok;
}
