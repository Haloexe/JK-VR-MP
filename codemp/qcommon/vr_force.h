/*
===========================================================================
OpenJK VR (multiplayer) - the Force hand: Force gestures.

Your Force hand is the hand whose grip is the Force grip (the left; the
right, left-handed: vr_leftHanded), while it's empty - not holding a
saber, a hilt or your gun, not taking hold of one. Holding its grip in
(the Force grip, squeezed nearly all the way: vr_gripPress), it reaches
out with the Force:

  thrust it out                 Push, the way it went
  reach out, yank it back in    Pull, toward where it was yanked from
  held out still a moment       Grip: whoever it points at, held as long
                                as the grip is
  its trigger                   Lightning, where it points - with the palm
                                turned up, Drain - as long as it's held
  a quick squeeze               the Force power picked (the wheel's Force
                                ring, X and Y): Use Force, a moment

Grip comes only first thing in a squeeze (after a Push, a Pull or the
trigger, squeeze afresh) - and while the game has someone gripped, or
Lightning or Drain going, its own rule has a Push or a Pull do nothing.
(A Grip that's caught nobody, held through a Push or a Pull, catches no
one after it on a server running this build: game/g_vrforce.c.)
A Pull reaches out first - the hand put out, not thrust (a thrust out is a
Push, and no Pull comes after one till the hand's been at rest - slow, in
by you or hanging down - or the Force grip's squeezed afresh). The trigger
is the gestures' only pulled once the Force grip's held: pulled first - a
fist made all at once, it usually is - it's its own key, as ever.

vr_forceGestures: 1, Force grip held (the default), as above; 2, Open
hand, also pushes and pulls with the hand open, no grip held (loose, the
trigger not pulled); 0, Off, none of it - the Force grip is Use Force (the
power picked) as it always was. A thrust or a yank goes at least
vr_forceThrust metres a second, VRF_RUN far that way, in VRF_RUN_MS.

The engine (client/cl_vr.cpp) hands this the Force hand every time it
reads the controllers, and makes the game's own inputs of what it says,
so they work on any server: Push and Pull its generic commands (as
force_throw and force_pull), Grip, Lightning and Drain their buttons held
(as +force_grip, +force_lightning, +force_drain), a quick squeeze Use
Force held a moment (+useforce: VRF_TAP_HOLD_MS). A server running this
build aims them from the hand (game/g_vrforce.c): Push and Pull the way
they went, the rest where it points.

Positions are the controllers' own - in metres, on Quake's axes (x
forward, y left, z up): the hand where it holds its controller, your head
between your eyes.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#pragma once

#include "qcommon/vr_public.h"

#define VRF_MODE_OFF		0		// vr_forceGestures: none - the Force grip is Use Force
#define VRF_MODE_GRIP		1		// ...while the Force grip's held (the default)
#define VRF_MODE_OPEN		2		// ...and Push and Pull with the hand open too
#define VRF_MODE_DEFAULT	VRF_MODE_GRIP

#define VRF_THRUST_DEFAULT	1.6f	// metres a second: a thrust or a yank goes at least this fast (vr_forceThrust)...
#define VRF_THRUST_MIN		0.8f
#define VRF_THRUST_MAX		3.0f
#define VRF_RUN				0.12f	// ...this far out from the shoulder (or in toward it)...
#define VRF_RUN_MS			300		// ...within this long

#define VRF_SHOULDER_DOWN	0.22f	// the hand's shoulder: this far below your head (m)...
#define VRF_SHOULDER_SIDE	0.17f	// ...and this far to its side
#define VRF_REACH			0.38f	// a hand this far from its shoulder is reaching out (a Pull's start, Grip)...
#define VRF_HANGING			-0.8f	// ...unless it hangs down: the way to it this far down (37 degrees off straight down)
#define VRF_ALONG			0.5f	// a thrust within 60 degrees of straight out from the shoulder (a yank, straight in)...
#define VRF_DOWN			-0.82f	// ...and its way (where a Pull comes from) no steeper down than 35 degrees off straight down
#define VRF_PULL_MS			700		// a Pull: yanked in within this long of reaching out
#define VRF_SETTLE			0.5f	// metres a second: after a Push or a Pull, slower than this...
#define VRF_GAP_MS			250		// ...and this long after it, before another (or Grip)
#define VRF_STILL			0.3f	// metres a second: held still...
#define VRF_GRIP_MS			350		// ...out, this long: Grip
#define VRF_TAP_MS			300		// the Force grip let go within this, the hand moved...
#define VRF_TAP_MOVE		0.10f	// ...less than this, nothing done: a quick squeeze...
#define VRF_TAP_HOLD_MS		150		// ...holds Use Force this long (the server sees it, whatever its pace)
#define VRF_PALM_UP			0.5f	// the palm this far up (1: straight up) as the trigger's pulled: Drain
#define VRF_OPEN			0.3f	// Open hand: the grip squeezed less than this, the trigger pulled less
#define VRF_SPEED_MS		40		// how fast the hand goes: over this long at least...
#define VRF_SPEED_MAX_MS	150		// ...from no further back than this (else it isn't known)
#define VRF_SAMPLES			64		// where it's been (VRF_RUN_MS of them, read as often as 200 times a second)

// what's done (vrfOutput_t.events)
#define VRF_PUSH			1
#define VRF_PULL			2
#define VRF_TAP				4

// The Force hand as the controllers are read now.
typedef struct {
	int			time;		// ms
	int			mode;		// vr_forceGestures (VRF_MODE_*)
	qboolean	free;		// there is a Force hand: gestures on, the hand empty and tracked lately, in a game (else all's let go)
	qboolean	tracked;	// ...tracked now: pos is where it is (else where it was last seen, or a guess - not a reading)
	int			hand;		// which (VR_HAND_*: its shoulder's side)
	vec3_t		pos;		// where it is
	vec3_t		head;		// where your head is...
	float		headYaw;	// ...and which way it faces (degrees)
	float		palmUp;		// how far its palm faces up: -1 down .. 1 up
	qboolean	grip;		// its grip held as the Force grip (the gestures' - not a grab)
	qboolean	trigger;	// its trigger held as the gestures' (pulled while the Force grip was)
	qboolean	open;		// the hand open: its grip loose, its trigger not pulled
	float		thrust;		// vr_forceThrust (metres a second)
} vrfInput_t;

// What's done now, and what's held.
typedef struct {
	int			events;		// VRF_PUSH, VRF_PULL, VRF_TAP
	vec3_t		thrust;		// a Push's way, or a Pull's back toward where it was yanked from (unit)
	float		speed;		// ...how fast it went (metres a second)
	int			held;		// VR_FORCE_*: what's held down
} vrfOutput_t;

typedef struct {
	int			time;
	vec3_t		pos;
	float		out;		// how far from its shoulder it was
} vrfSample_t;

typedef struct {
	vrfSample_t	sample[VRF_SAMPLES];	// where it's been...
	int			count, head;			// ...(head: the newest)
	qboolean	reaching;		// the Force grip held...
	int			reachTime;		// ...since (ms)...
	vec3_t		reachPos;		// ...from there
	qboolean	done;			// something done since (a Push, a Pull, the trigger, Grip): no Grip now, and letting
								// go isn't a quick squeeze
	int			held;			// VR_FORCE_*: held down now
	qboolean	triggerWas;		// the trigger, as the last reading had it
	int			throwTime;		// the last Push or Pull (ms; 0: none yet)...
	qboolean	settled;		// ...the hand slowed down since (or none yet)
	qboolean	pullArmed;		// a Pull can start: the Force grip squeezed afresh since a Push, or the hand at rest (in,
								// or hanging down, slow) - bringing it back from a Push isn't a Pull
	int			outTime;		// the hand last reaching out (ms; 0: not yet)
	int			stillTime;		// held out still, reaching, since (ms; 0: not)
} vrfState_t;

// Nothing going on (and nothing carried over).
static inline void VRF_Clear( vrfState_t *st ) {
	memset( st, 0, sizeof( *st ) );
	st->settled = qtrue;
	st->pullArmed = qtrue;
}

// How fast a thrust or a yank goes at least, as vr_forceThrust says
// (VRF_THRUST_MIN..VRF_THRUST_MAX; NaN: VRF_THRUST_DEFAULT).
static inline float VRF_Thrust( float thrust ) {
	if ( thrust != thrust ) {
		return VRF_THRUST_DEFAULT;
	}
	return ( thrust < VRF_THRUST_MIN ) ? VRF_THRUST_MIN : ( thrust > VRF_THRUST_MAX ) ? VRF_THRUST_MAX : thrust;
}

// Where the hand's shoulder is: below your head, to its side.
static inline void VRF_Shoulder( const vrfInput_t *in, vec3_t out ) {
	const float yaw = DEG2RAD( in->headYaw );
	const float side = ( in->hand == VR_HAND_RIGHT ) ? -VRF_SHOULDER_SIDE : VRF_SHOULDER_SIDE;

	out[0] = in->head[0] - sinf( yaw ) * side;
	out[1] = in->head[1] + cosf( yaw ) * side;
	out[2] = in->head[2] - VRF_SHOULDER_DOWN;
}

static inline const vrfSample_t *VRF_Sample( const vrfState_t *st, int i ) {
	return &st->sample[( st->head - i + VRF_SAMPLES ) % VRF_SAMPLES];
}

// How fast the hand goes (over VRF_SPEED_MS at least). qfalse: it isn't
// known - no reading lately from far enough back.
static inline qboolean VRF_Velocity( const vrfState_t *st, vec3_t v ) {
	const vrfSample_t *now = VRF_Sample( st, 0 );
	int i;

	for ( i = 1; i < st->count; i++ ) {
		const vrfSample_t *then = VRF_Sample( st, i );
		const int dt = now->time - then->time;
		if ( dt >= VRF_SPEED_MS ) {
			if ( dt > VRF_SPEED_MAX_MS ) {
				return qfalse;
			}
			VectorSubtract( now->pos, then->pos, v );
			VectorScale( v, 1000.0f / dt, v );
			return qtrue;
		}
	}
	return qfalse;
}

// The nearest it's been to its shoulder, and the furthest, within VRF_RUN_MS.
static inline void VRF_OutRange( const vrfState_t *st, float *least, float *most ) {
	const vrfSample_t *now = VRF_Sample( st, 0 );
	int i;

	*least = *most = now->out;
	for ( i = 1; i < st->count; i++ ) {
		const vrfSample_t *s = VRF_Sample( st, i );
		if ( now->time - s->time > VRF_RUN_MS ) {
			break;
		}
		if ( s->out < *least ) {
			*least = s->out;
		}
		if ( s->out > *most ) {
			*most = s->out;
		}
	}
}

// The Force hand read again: what it does.
static inline void VRF_Update( vrfState_t *st, const vrfInput_t *in, vrfOutput_t *out ) {
	vec3_t shoulder, way, v;
	vrfSample_t *s;
	float dist, speed = 0.0f;
	qboolean known, reachingOut;

	memset( out, 0, sizeof( *out ) );
	if ( !in->free || in->mode <= VRF_MODE_OFF || in->mode > VRF_MODE_OPEN ) {
		VRF_Clear( st );	// (all let go)
		return;
	}

	// where it is: how far out from its shoulder, which way (a reading at the
	// same time as the last replaces it; the clock gone back, the rest go). The
	// hand not tracked now - where it was last seen isn't where it is - it's
	// no reading: nothing's worked out from it (how fast it goes, whether it's
	// out), the readings before kept - seen again, how fast it went meanwhile
	// is from where it was last seen (no further back than VRF_SPEED_MAX_MS).
	VRF_Shoulder( in, shoulder );
	VectorSubtract( in->pos, shoulder, way );
	dist = VectorNormalize( way );
	if ( st->count && in->time < VRF_Sample( st, 0 )->time ) {
		st->count = 0;
	}
	if ( in->tracked ) {
		if ( !st->count || in->time != VRF_Sample( st, 0 )->time ) {
			st->head = ( st->head + 1 ) % VRF_SAMPLES;
			if ( st->count < VRF_SAMPLES ) {
				st->count++;
			}
		}
		s = &st->sample[st->head];
		s->time = in->time;
		VectorCopy( in->pos, s->pos );
		s->out = dist;
	}
	reachingOut = (qboolean)( in->tracked && dist >= VRF_REACH && way[2] > VRF_HANGING );
	if ( reachingOut ) {
		st->outTime = in->time;
	}
	known = (qboolean)( in->tracked && VRF_Velocity( st, v ) );
	if ( known ) {
		speed = VectorLength( v );
		if ( speed < VRF_SETTLE ) {
			st->settled = qtrue;
			if ( !reachingOut ) {
				st->pullArmed = qtrue;	// (at rest)
			}
		}
	}

	// the Force grip squeezed: reaching out. Let go: all it held goes - and,
	// nothing done, let go quickly where it was squeezed, a quick squeeze.
	if ( in->grip && !st->reaching ) {
		st->reaching = qtrue;
		st->reachTime = in->time;
		VectorCopy( in->pos, st->reachPos );
		st->done = qfalse;
		st->pullArmed = qtrue;
		st->stillTime = 0;
	} else if ( !in->grip && st->reaching ) {
		st->reaching = qfalse;
		if ( !st->done && in->time - st->reachTime <= VRF_TAP_MS && Distance( in->pos, st->reachPos ) < VRF_TAP_MOVE ) {
			out->events |= VRF_TAP;
		}
		st->held = 0;
	}

	// Push and Pull: reaching out - or, Open hand, the hand open - a thrust
	// or a yank, fast and far enough, settled since the last
	if ( known && speed >= VRF_Thrust( in->thrust ) && st->settled &&
		( st->reaching || ( in->mode == VRF_MODE_OPEN && in->open ) ) &&
		( !st->throwTime || in->time - st->throwTime >= VRF_GAP_MS ) ) {
		vec3_t dir;
		float along, least, most;
		VectorScale( v, 1.0f / speed, dir );
		along = DotProduct( dir, way );
		VRF_OutRange( st, &least, &most );
		if ( along >= VRF_ALONG && dir[2] >= VRF_DOWN && dist - least >= VRF_RUN ) {
			out->events |= VRF_PUSH;
			VectorCopy( dir, out->thrust );
			st->pullArmed = qfalse;		// (bringing it back in isn't a Pull)
		} else if ( along <= -VRF_ALONG && -dir[2] >= VRF_DOWN && most - dist >= VRF_RUN &&
			st->pullArmed && st->outTime && in->time - st->outTime <= VRF_PULL_MS ) {
			out->events |= VRF_PULL;
			VectorScale( dir, -1.0f, out->thrust );
		}
		if ( out->events & ( VRF_PUSH | VRF_PULL ) ) {
			out->speed = speed;
			st->throwTime = in->time;
			st->settled = qfalse;
			st->done = qtrue;
		}
	}

	// Lightning or Drain: the trigger pulled, reaching out - Drain with the
	// palm up - held as long as it is (and Grip gives way to it)
	if ( in->trigger && !st->triggerWas && st->reaching ) {
		st->held = ( in->palmUp >= VRF_PALM_UP ) ? VR_FORCE_DRAIN : VR_FORCE_LIGHTNING;
		st->done = qtrue;
	}
	if ( !in->trigger ) {
		st->held &= ~( VR_FORCE_LIGHTNING | VR_FORCE_DRAIN );
	}
	st->triggerWas = in->trigger;

	// Grip: reaching out, nothing done yet this squeeze (once a Push, a Pull or
	// the trigger is, not till it's squeezed afresh), held out still a moment -
	// then held as long as the Force grip is
	if ( st->reaching && !st->done ) {
		if ( known && speed < VRF_STILL && reachingOut ) {
			if ( !st->stillTime ) {
				st->stillTime = in->time;
			}
		} else {
			st->stillTime = 0;
		}
		if ( st->stillTime && in->time - st->stillTime >= VRF_GRIP_MS ) {
			st->held = VR_FORCE_GRIP;
			st->done = qtrue;
		}
	}
	out->held = st->held;
}
