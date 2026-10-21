/*
===========================================================================
OpenJK VR (multiplayer) - your fingers, from what your controllers sense.

The humanoid skeleton every model shares moves three fingers a hand: the
thumb, the index finger, and the other three together. Each follows yours
as far as the controller can tell (vrFingerSense_t, qcommon/vr_public.h),
from 0 (open, straight, lifted) to 1 (closed, curled, down):

  the thumb        touching a button, the stick, the thumb rest (a
                   trackpad): down; near them (Touch Plus, Touch Pro): half
                   way; lifted: up. A controller that can't tell: down.
  the index        off the trigger: straight - or as curled as the
                   controller can tell (Touch Plus, Touch Pro), or a little
                   (near it); resting on it: half curled, closing to a fist
                   as it's pulled - and with the other three: a finger
                   resting on the trigger of a hand squeezing its grip is
                   in the fist, without pulling it. A controller that can't
                   tell: resting on it.
  the other three  the grip: a light squeeze closes them (half its travel);
                   loose, they're relaxed round the controller - open, once
                   the thumb and the index finger are lifted too.

So the gestures are there without picking them:

  open hand    thumb up, index straight, the rest open: loosen the grip,
               lift your finger and thumb
  fist         a light squeeze, finger resting on the trigger, thumb down
  point        a light squeeze, finger off the trigger
  thumbs up    a light squeeze, finger on the trigger, thumb lifted
  finger gun   a light squeeze, finger and thumb lifted
  relaxed      the grip loose, thumb on the stick, finger resting
  claw         a half squeeze, finger and thumb near (Touch Plus, Pro)

None of them does anything else: Use and the Force take a grip pressed
nearly all the way (vr_gripPress, client/cl_vr.cpp).

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#pragma once

#include "qcommon/vr_public.h"

#define VR_FINGER_SQUEEZE_FROM	0.05f	// the grip squeezed past this starts closing the other three...
#define VR_FINGER_SQUEEZE_SHUT	0.5f	// ...and this far has them shut (a light squeeze)
#define VR_FINGER_RELAXED		0.35f	// the other three, loose round a controller
#define VR_FINGER_REST			0.5f	// the index finger resting on the trigger: half curled
#define VR_FINGER_INDEX_NEAR	0.25f	// ...near it: a quarter
#define VR_FINGER_THUMB_NEAR	0.5f	// the thumb near the buttons: half way down
#define VR_FINGER_PULLED		0.05f	// a trigger pulled past this has a finger on it, whatever its sensor says

static inline float VR_FingerClamp( float f ) {
	if ( !( f > 0.0f ) ) {
		return 0.0f;	// (and NaN)
	}
	return ( f < 1.0f ) ? f : 1.0f;
}

// Whether the index finger is on the trigger: its touch sensor (or pulling
// it); a controller that can't tell, yes.
static inline int VR_FingerOnTrigger( const vrFingerSense_t *s ) {
	if ( !( s->sensed & ( VR_SENSE_TRIGGER_TOUCH | VR_SENSE_INDEX_CURL ) ) ) {
		return 1;
	}
	return ( ( s->sensed & s->touched & VR_SENSE_TRIGGER_TOUCH ) || s->trigger > VR_FINGER_PULLED ) ? 1 : 0;
}

// How far each finger is closed (VR_FINGER_*, 0..1), from what the
// controller senses.
static inline void VR_FingerCurls( const vrFingerSense_t *s, float out[VR_NUM_FINGERS] ) {
	const float trigger = VR_FingerClamp( s->trigger );
	const float squeeze = VR_FingerClamp( s->squeeze );
	const int on = VR_FingerOnTrigger( s );
	float thumb, rest, others, open;

	// the thumb
	if ( s->sensed & VR_SENSE_THUMB_TOUCH ) {
		if ( s->touched & VR_SENSE_THUMB_TOUCH ) {
			thumb = 1.0f;
		} else if ( s->sensed & s->touched & VR_SENSE_THUMB_NEAR ) {
			thumb = VR_FINGER_THUMB_NEAR;
		} else {
			thumb = 0.0f;
		}
	} else {
		thumb = 1.0f;
	}

	// the index finger, before it's pulled
	if ( on ) {
		rest = VR_FINGER_REST;
	} else if ( s->sensed & VR_SENSE_INDEX_CURL ) {
		rest = VR_FINGER_REST * VR_FingerClamp( s->indexCurl );
	} else if ( s->sensed & s->touched & VR_SENSE_TRIGGER_NEAR ) {
		rest = VR_FINGER_INDEX_NEAR;
	} else {
		rest = 0.0f;
	}

	// the other three: the grip - loose, relaxed, unless the thumb and the
	// index finger are lifted as well (an open hand)
	open = ( 1.0f - thumb ) * ( 1.0f - rest / VR_FINGER_REST );
	others = VR_FingerClamp( ( squeeze - VR_FINGER_SQUEEZE_FROM ) / ( VR_FINGER_SQUEEZE_SHUT - VR_FINGER_SQUEEZE_FROM ) );
	if ( others < VR_FINGER_RELAXED * ( 1.0f - open ) ) {
		others = VR_FINGER_RELAXED * ( 1.0f - open );
	}

	out[VR_FINGER_THUMB] = thumb;
	out[VR_FINGER_INDEX] = on ? VR_FINGER_REST + ( 1.0f - VR_FINGER_REST ) * ( ( trigger > others ) ? trigger : others ) : rest;
	out[VR_FINGER_OTHERS] = others;
}
