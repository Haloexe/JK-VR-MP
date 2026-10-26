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
                                as the grip is (not with the palm turned
                                up: that's Drain coming)
  its trigger                   Lightning, where it points - with the palm
                                turned up, Drain - as long as it's held
  a quick squeeze               the Force power picked (the wheel's Force
                                ring, X and Y): Use Force, a moment

Close in by you and held still - or its trigger pulled there - it casts
the rest of the Force powers you have (Body Gestures, vr_forceBody):

  on your chest, palm toward you  Heal; the trigger there, Rage
  before your face, palm out      Protect (the trigger: Lightning, as
                                  anywhere it points)
  at your belly, palm up          Absorb (the trigger: Drain, as with the
                                  palm up anywhere)
  at your temple                  Seeing
  high over your head             Team Heal; the trigger there, Team
                                  Energize

Each place is a box measured from your eyes the way your head faces
(VRF_FromEyes, vrfPlaces), the hand turned as it says. Held still there -
settled slower than VRF_STILL, never off more than VRF_PLACE_DRIFT from
where it settled - for vr_forceHold seconds (VRF_HOLD_*), its power's cast;
the trigger pulled there casts its dark twin at once (and not Lightning).
A place's power spends the squeeze: nothing more - no Push, Pull, Grip,
trigger, nor another place - till it's squeezed afresh; and after a Push,
a Pull, the trigger or Grip in a squeeze, no place casts in it. Only your
powers count (vrfInput_t.known: the game's forcePowersKnown): a place with
none of yours is no place - nothing's held there, and the trigger's
Lightning there as anywhere; one with only its dark twin (Rage, say) is
the trigger's alone. A place of yours, held still, is never a Grip, and a
hand high over your head never is.

Grip comes only first thing in a squeeze (after a Push, a Pull or the
trigger, squeeze afresh) - and while the game has someone gripped, or
Lightning or Drain going, its own rule has a Push or a Pull do nothing.
(A Grip that's caught nobody, held through a Push or a Pull, catches no
one after it on a server running this build: game/g_vrforce.c.)
A Pull reaches out first - the hand put out, not thrust (a thrust out is a
Push, and no Pull comes after one till the hand's been at rest - slow, in
by you or hanging down - or the Force grip's squeezed afresh). A thrust
steeply up (VRF_UP) isn't a Push: a hand raised fast to your face, your
temple or over your head is going to a place. The trigger
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
Force held a moment (+useforce: VRF_TAP_HOLD_MS), a place's power its
generic command (as force_heal, force_rage, force_protect, force_absorb,
force_seeing, force_healother, force_forcepowerother). A server running
this build aims them from the hand (game/g_vrforce.c): Push and Pull the
way they went, the rest where it points.

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
#define VRF_UP				0.82f	// ...with Body Gestures, a thrust's no steeper up than 35 degrees off straight up (a hand
										// raised to your face, your temple, over your head: no Push)
#define VRF_PULL_MS			700		// a Pull: yanked in within this long of reaching out
#define VRF_SETTLE			0.5f	// metres a second: after a Push or a Pull, slower than this...
#define VRF_GAP_MS			250		// ...and this long after it, before another (or Grip)
#define VRF_STILL			0.4f	// metres a second: held still...
#define VRF_GRIP_MS			350		// ...out, this long (the palm not up: VRF_PALM_UP): Grip (a moment past a quick squeeze's)
#define VRF_TAP_MS			300		// the Force grip let go within this, the hand moved...
#define VRF_TAP_MOVE		0.10f	// ...less than this, nothing done: a quick squeeze...
#define VRF_TAP_HOLD_MS		150		// ...holds Use Force this long (the server sees it, whatever its pace)
#define VRF_PALM_UP			0.5f	// the palm this far up (1: straight up) as the trigger's pulled: Drain (and held out so,
										// no Grip)
#define VRF_OPEN			0.3f	// Open hand: the grip squeezed less than this, the trigger pulled less
#define VRF_SPEED_MS		40		// how fast the hand goes: over this long at least...
#define VRF_SPEED_MAX_MS	150		// ...from no further back than this (else it isn't known)
#define VRF_SAMPLES			64		// where it's been (VRF_RUN_MS of them, read as often as 200 times a second)

// The places (Body Gestures, vr_forceBody: vrfPlaces) - and how long a hand
// held still in one takes to cast its power (vr_forceHold, seconds)
#define VRF_PLACE_NONE		0
#define VRF_PLACE_CHEST		1		// Heal; the trigger, Rage
#define VRF_PLACE_FACE		2		// Protect
#define VRF_PLACE_BELLY		3		// Absorb
#define VRF_PLACE_TEMPLE	4		// Seeing
#define VRF_PLACE_OVERHEAD	5		// Team Heal; the trigger, Team Energize
#define VRF_PLACES			6
#define VRF_HOLD_DEFAULT	0.4f
#define VRF_HOLD_MIN		0.3f	// (no shorter than a quick squeeze, VRF_TAP_MS: one held so long is a place's)
#define VRF_HOLD_MAX		0.8f
#define VRF_PLACE_DRIFT		0.06f	// held still in a place: never off further than this from where it settled (m)
#define VRF_PALM_TURN		0.4f	// the palm turned out (ahead, the way your head faces) or back toward you this far
										// (1: straight)
#define VRF_TURN_ANY		0		// a place's hand turned (vrfPlace_t.turn) any way...
#define VRF_TURN_OUT		1		// ...its palm out, ahead...
#define VRF_TURN_IN			2		// ...back toward you...
#define VRF_TURN_UP			3		// ...up (VRF_PALM_UP)

// what's done (vrfOutput_t.events)
#define VRF_PUSH			1
#define VRF_PULL			2
#define VRF_TAP				4
#define VRF_CAST			8		// a place's power: vrfOutput_t.cast

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
	vec3_t		palm;		// which way its palm faces (unit: palm[2] is palmUp)
	qboolean	grip;		// its grip held as the Force grip (the gestures' - not a grab)
	qboolean	trigger;	// its trigger held as the gestures' (pulled while the Force grip was)
	qboolean	open;		// the hand open: its grip loose, its trigger not pulled
	float		thrust;		// vr_forceThrust (metres a second)
	qboolean	body;		// vr_forceBody: the places (Body Gestures)...
	float		hold;		// ...and vr_forceHold: how long one's held still for its power (seconds)
	int			known;		// the Force powers you have (1 << FP_*: your player state's forcePowersKnown)
} vrfInput_t;

// What's done now, and what's held.
typedef struct {
	int			events;		// VRF_PUSH, VRF_PULL, VRF_TAP, VRF_CAST
	vec3_t		thrust;		// a Push's way, or a Pull's back toward where it was yanked from (unit)
	float		speed;		// ...how fast it went (metres a second)
	int			held;		// VR_FORCE_*: what's held down
	int			cast;		// VRF_CAST: the power cast (FP_*)
	int			place;		// the place the hand's held still in now, one with a power of yours there (VRF_PLACE_*;
							// VRF_PLACE_NONE: not)
} vrfOutput_t;

// A place (Body Gestures): a box measured from your eyes, the way your head
// faces (VRF_FromEyes) - the hand in it turned as it says - and its powers.
// Where the hand is, is where the controller's grip is: at your temple, beside
// your headset (its side some 9 or 10 cm out from your eyes), that's 13 cm
// out or more.
typedef struct {
	const char	*name;		// where it is (the log's: "on your chest")
	float		ahead[2];	// from..to (m): in front of your eyes...
	float		side[2];	// ...out to the hand's own side (the left hand's left, the right hand's right)...
	float		up[2];		// ...above them
	int			turn;		// VRF_TURN_*
	int			power;		// held still there: this power (FP_*)...
	int			twin;		// ...the trigger there: this one (-1: the trigger's own, as anywhere)
} vrfPlace_t;

static const vrfPlace_t vrfPlaces[VRF_PLACES] = {
	{ "nowhere",		{ 0.0f, 0.0f },		{ 0.0f, 0.0f },		{ 0.0f, 0.0f },		VRF_TURN_ANY,	-1,				-1 },
	{ "on your chest",	{ 0.05f, 0.25f },	{ -0.15f, 0.15f },	{ -0.45f, -0.25f },	VRF_TURN_IN,	FP_HEAL,		FP_RAGE },
	{ "before your face",	{ 0.15f, 0.35f },	{ -0.15f, 0.15f },	{ -0.15f, 0.15f },	VRF_TURN_OUT,	FP_PROTECT,		-1 },
	{ "at your belly",	{ 0.15f, 0.35f },	{ -0.20f, 0.20f },	{ -0.65f, -0.45f },	VRF_TURN_UP,	FP_ABSORB,		-1 },
	{ "at your temple",	{ -0.10f, 0.12f },	{ 0.08f, 0.22f },	{ -0.10f, 0.10f },	VRF_TURN_ANY,	FP_SEE,			-1 },
	{ "over your head",	{ -0.25f, 0.30f },	{ -0.35f, 0.35f },	{ 0.20f, 1.0f },	VRF_TURN_ANY,	FP_TEAM_HEAL,	FP_TEAM_FORCE },
};

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
	qboolean	placed;			// a place's power cast this squeeze: nothing more till it's squeezed afresh
	int			place;			// the place of yours it's held still in (VRF_PLACE_*; VRF_PLACE_NONE: none)...
	int			placeTime;		// ...since (ms)...
	vec3_t		placePos;		// ...settled there
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

// How long a hand held still in a place takes to cast its power, in ms, as
// vr_forceHold says (VRF_HOLD_MIN..VRF_HOLD_MAX seconds; NaN: VRF_HOLD_DEFAULT).
static inline int VRF_Hold( float hold ) {
	if ( hold != hold ) {
		hold = VRF_HOLD_DEFAULT;
	}
	hold = ( hold < VRF_HOLD_MIN ) ? VRF_HOLD_MIN : ( hold > VRF_HOLD_MAX ) ? VRF_HOLD_MAX : hold;
	return (int)( hold * 1000.0f + 0.5f );
}

// Whether you have Force power p (FP_*; -1: none, never).
static inline qboolean VRF_Has( const vrfInput_t *in, int p ) {
	return (qboolean)( p >= 0 && p < NUM_FORCE_POWERS && ( in->known & ( 1 << p ) ) );
}

// Where the hand is from your eyes, the way your head faces: [0] in front,
// [1] out to the hand's own side (the left hand's left, the right hand's
// right), [2] up (m).
static inline void VRF_FromEyes( const vrfInput_t *in, vec3_t out ) {
	const float yaw = DEG2RAD( in->headYaw ), c = cosf( yaw ), s = sinf( yaw );
	vec3_t d;

	VectorSubtract( in->pos, in->head, d );
	out[0] = d[0] * c + d[1] * s;
	out[1] = d[1] * c - d[0] * s;
	if ( in->hand == VR_HAND_RIGHT ) {
		out[1] = -out[1];
	}
	out[2] = d[2];
}

// How far the hand's palm faces out ahead, the way your head faces, level:
// 1 straight ahead .. -1 straight back toward you.
static inline float VRF_PalmAhead( const vrfInput_t *in ) {
	const float yaw = DEG2RAD( in->headYaw );

	return in->palm[0] * cosf( yaw ) + in->palm[1] * sinf( yaw );
}

// The place the hand's in (vrfPlaces: the box, the hand turned as it says),
// whoever's powers are there - none with Body Gestures off, or the hand not
// tracked now (no reading).
static inline int VRF_PlaceOf( const vrfInput_t *in ) {
	vec3_t e;
	float ahead;
	int i;

	if ( !in->body || !in->tracked ) {
		return VRF_PLACE_NONE;
	}
	VRF_FromEyes( in, e );
	ahead = VRF_PalmAhead( in );
	for ( i = VRF_PLACE_NONE + 1; i < VRF_PLACES; i++ ) {
		const vrfPlace_t *p = &vrfPlaces[i];
		if ( e[0] < p->ahead[0] || e[0] > p->ahead[1] || e[1] < p->side[0] || e[1] > p->side[1] ||
			e[2] < p->up[0] || e[2] > p->up[1] ) {
			continue;
		}
		if ( ( p->turn == VRF_TURN_OUT && ahead < VRF_PALM_TURN ) || ( p->turn == VRF_TURN_IN && ahead > -VRF_PALM_TURN ) ||
			( p->turn == VRF_TURN_UP && in->palmUp < VRF_PALM_UP ) ) {
			continue;
		}
		return i;
	}
	return VRF_PLACE_NONE;
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
	qboolean known, reachingOut, mine;
	int at;

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
	// the place it's in (Body Gestures), and whether a power of yours is there
	at = VRF_PlaceOf( in );
	mine = (qboolean)( VRF_Has( in, vrfPlaces[at].power ) || VRF_Has( in, vrfPlaces[at].twin ) );

	// the Force grip squeezed: reaching out. Let go: all it held goes - and,
	// nothing done, let go quickly where it was squeezed, a quick squeeze; the
	// squeeze over, a place's power cast in it no longer stops anything (Open
	// hand's Push and Pull).
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
		st->placed = qfalse;
	}

	// Push and Pull: reaching out - or, Open hand, the hand open - a thrust
	// or a yank, fast and far enough, settled since the last (not after a
	// place's power in the squeeze)
	if ( known && speed >= VRF_Thrust( in->thrust ) && st->settled && !st->placed &&
		( st->reaching || ( in->mode == VRF_MODE_OPEN && in->open ) ) &&
		( !st->throwTime || in->time - st->throwTime >= VRF_GAP_MS ) ) {
		vec3_t dir;
		float along, least, most;
		VectorScale( v, 1.0f / speed, dir );
		along = DotProduct( dir, way );
		VRF_OutRange( st, &least, &most );
		if ( along >= VRF_ALONG && dir[2] >= VRF_DOWN && ( !in->body || dir[2] <= VRF_UP ) && dist - least >= VRF_RUN ) {
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

	// the trigger pulled, reaching out: in a place with a dark twin of yours,
	// that, at once - once a squeeze, nothing done in it yet (after anything,
	// nothing there: squeeze afresh). Elsewhere, Lightning - with the palm up,
	// Drain - held as long as it is (and Grip gives way to it). After a place's
	// power, nothing.
	if ( in->trigger && !st->triggerWas && st->reaching && !st->placed ) {
		if ( VRF_Has( in, vrfPlaces[at].twin ) ) {
			if ( !st->done ) {
				out->events |= VRF_CAST;
				out->cast = vrfPlaces[at].twin;
				st->placed = qtrue;
			}
		} else {
			st->held = ( in->palmUp >= VRF_PALM_UP ) ? VR_FORCE_DRAIN : VR_FORCE_LIGHTNING;
		}
		st->done = qtrue;
	}
	if ( !in->trigger ) {
		st->held &= ~( VR_FORCE_LIGHTNING | VR_FORCE_DRAIN );
	}
	st->triggerWas = in->trigger;

	// Grip: reaching out, nothing done yet this squeeze (once a Push, a Pull or
	// the trigger is, not till it's squeezed afresh), held out still a moment -
	// the palm not turned up (Drain's to come), not in a place of yours, nor
	// over your head - then held as long as the Force grip is
	if ( st->reaching && !st->done ) {
		if ( known && speed < VRF_STILL && reachingOut && in->palmUp < VRF_PALM_UP && !mine && at != VRF_PLACE_OVERHEAD ) {
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

	// a place of yours, held still - reaching out, nothing done yet this
	// squeeze: settled there slower than VRF_STILL, staying while it's never off
	// further than VRF_PLACE_DRIFT from where it settled (further, but slow: it
	// settles afresh). Held so in->hold seconds (VRF_Hold), its power - if it's
	// yours (one with only its twin is the trigger's). No reading: as it was -
	// and nothing cast till there's one (the hand may be anywhere meanwhile).
	if ( !st->reaching || st->done ) {
		st->place = VRF_PLACE_NONE;
	} else if ( known ) {
		if ( mine && st->place == at && Distance( in->pos, st->placePos ) <= VRF_PLACE_DRIFT ) {
			;	// (still there)
		} else if ( mine && speed < VRF_STILL ) {
			st->place = at;
			st->placeTime = in->time;
			VectorCopy( in->pos, st->placePos );
		} else {
			st->place = VRF_PLACE_NONE;
		}
	}
	if ( known && st->place && VRF_Has( in, vrfPlaces[st->place].power ) && in->time - st->placeTime >= VRF_Hold( in->hold ) ) {
		out->events |= VRF_CAST;
		out->cast = vrfPlaces[st->place].power;
		st->placed = st->done = qtrue;
		st->place = VRF_PLACE_NONE;
	}
	out->place = st->place;
	out->held = st->held;
}
