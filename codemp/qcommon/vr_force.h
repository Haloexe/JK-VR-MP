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
                                up: that's Drain coming) - if it's yours
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

and, moved so (Body Gestures too):

  waved across before your face   Mind Trick (a server running this build:
                                  level 1's at whoever it's waved across)
  swung down and back past your   Speed
  hip, fast

Each place is a box measured from your eyes the way your head faces
(VRF_FromEyes, vrfPlaces), the hand turned as it says. Held still there -
settled slower than VRF_STILL, never off more than VRF_PLACE_DRIFT from
where it settled - for vr_forceHold seconds (VRF_HOLD_*), its power's cast;
the trigger pulled there casts its dark twin at once (and not Lightning) -
though with the palm turned up (VRF_PALM_UP), the trigger's Drain, as
anywhere but over your head. Settled in a place, the hand stays in it a
little out of it (a tremble at its edge, the trigger's tug, the palm
turned up a little on your chest: VRF_PLACE_KEEP, VRF_TURN_KEEP) - unless
it's well in another of yours (VRF_PLACE_MARGIN, VRF_TURN_MARGIN: on your
chest, the palm a little further up than VRF_PALM_UP, the belly's; at your
belly, a little less, the chest's - no flicker where two meet).
A place's power spends the squeeze: nothing more - no Push, Pull, Grip,
trigger, nor another place - till it's squeezed afresh. After a Push, a
Pull, the trigger or Grip in a squeeze, a place held still casts nothing
in it - but its trigger still casts its twin, and the hand's still said
to settle in a place with a twin of yours (Lightning there by a hair,
say: moved in, it's settled - and pulled again, the twin); Grip gives way
to it as it does to Lightning. Only your powers count (vrfInput_t.known:
the game's forcePowersKnown): a place with none of yours is no place -
nothing's held there, and the trigger's Lightning there as anywhere; one
with only its dark twin (Rage, say) is the trigger's alone. A place of
yours, held still, is never a Grip, and a hand high over your head never
is.

A wave (VRF_Wave): across before your face - no further in front of your
eyes than VRF_WAVE_FAR, no further above or below them than VRF_WAVE_HIGH
- VRF_WAVE_ACROSS side to side within VRF_WAVE_MS, nor much in or out
(VRF_WAVE_DEPTH), the hand itself moving across (VRF_WAVE_MOVE: not your
head turned, nor you turning round or walking, the hand still on you), its
palm turned out at its middle (VRF_WAVE_PALM) - aimed from your eyes
through the hand there. A swing (VRF_Swing): from your side at your waist
or above (VRF_SWING_FRONT, VRF_SWING_TOP), down and back VRF_SWING_RUN, all
within VRF_SWING_MS, as fast as a thrust on the way, to past your hip
(VRF_SWING_PAST, VRF_SWING_LOW). A motion's power, as a place's, comes only
first thing in a squeeze and spends it; a power you don't have, nothing.
A thrust that may be one waits for it - with Speed yours, back and down,
low by you (VRF_BACK, VRF_SWING_SINK); with Mind Trick yours, a thrust or a
yank across before your face (VRF_WAVE_WAY): the motion, its power; not -
the hand slowed, gone from where the motion goes, VRF_PEND_MS up (lost from
sight a moment, it waits on), the trigger pulled, the Force grip let go -
the Push or the Pull it was, then, the way it went in the world (the game
turning you meanwhile: vrfInput_t.turn).

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
Force held a moment (+useforce: VRF_TAP_HOLD_MS), a place's or a motion's
power its generic command (as force_heal, force_rage, force_protect,
force_absorb, force_seeing, force_healother, force_forcepowerother,
force_distract, force_speed). A server running this build aims them from
the hand (game/g_vrforce.c): Push and Pull the way they went, a level 1
Mind Trick the way the wave's aimed, the rest where it points.

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
#define VRF_SAMPLES			128		// where it's been (VRF_WAVE_MS of them, read as often as 250 times a second)

// The motions (Body Gestures): a wave across before your face, Mind Trick
// (VRF_Wave)...
#define VRF_WAVE_ACROSS		0.20f	// m: the hand across this far, side to side...
#define VRF_WAVE_MOVE		0.15f	// ...the hand itself moved across this far from your eyes, as your head faced when it
									// set off (not your head turned, nor you turning round or walking, the hand still on you)...
#define VRF_WAVE_MS			500		// ...within this long...
#define VRF_WAVE_HIGH		0.15f	// ...never further above or below your eyes than this...
#define VRF_WAVE_NEAR		0.12f	// ...in front of them at least this far...
#define VRF_WAVE_FAR		0.45f	// ...and no further (your arm held out, aiming, isn't waving)...
#define VRF_WAVE_WIDE		0.35f	// ...nor further to either side of them...
#define VRF_WAVE_DEPTH		0.10f	// ...going in or out no more than this as it goes...
#define VRF_WAVE_PALM		0.25f	// ...its palm turned out this far (ahead, the way your head faces) at its middle
#define VRF_WAVE_WAY		0.7f	// a thrust or a yank this much across (1: straight across), the hand before your face:
									// maybe a wave's - Mind Trick yours, with Body Gestures - it waits for it (VRF_PEND_MS)
// ...and a swing down and back past your hip, Speed (VRF_Swing)
#define VRF_SWING_RUN		0.25f	// m: down and back this far...
#define VRF_SWING_BACK		0.15f	// ...of it, back at least this far, and down this far (not an arm swinging as you walk)...
#define VRF_SWING_DROP		0.05f
#define VRF_SWING_MS		400		// ...within this long - going as fast as a thrust (vr_forceThrust), back, on the way...
#define VRF_SWING_TOP		-0.65f	// ...from no lower below your eyes than this (your waist, or above: not hanging down)...
#define VRF_SWING_FRONT		0.25f	// ...nor further in front of them than this (from your side: not a hand held out, lowered)...
#define VRF_SWING_EASE		0.01f	// ...setting off as it was last within this of there (a hand at rest sways a little)...
#define VRF_SWING_LOW		-0.35f	// ...to below this (under your chest)...
#define VRF_SWING_PAST		-0.08f	// ...behind this (past your hip: a hand hanging at your side is a few centimetres
									// behind your eyes)...
#define VRF_SWING_SIDE		-0.05f	// ...and out to the hand's own side this far at least (not across you)
#define VRF_BACK			0.5f	// a thrust's way back this much, level (1: straight back)...
#define VRF_SWING_SINK		0.1f	// ...and down this much (1: straight down), under your chest (VRF_SWING_LOW): maybe a
									// swing - Speed yours, with Body Gestures - it waits for it...
#define VRF_PEND_MS			300		// ...a Push or a Pull that may be a motion done the way it went, the motion not come:
									// the hand slowed, gone from where the motion goes, or this long after

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
#define VRF_PALM_TURN		0.4f	// the palm turned out (ahead, the way your head faces) this far (1: straight)...
#define VRF_PALM_IN			0.5f	// ...or back toward you this far, and not up (VRF_PALM_UP: up, it's the belly's)
#define VRF_PLACE_KEEP		0.03f	// the place it's settled in is kept while it's no further out of it than this (m)...
#define VRF_TURN_KEEP		0.15f	// ...and its palm no further from turned as it wants than this...
#define VRF_PLACE_MARGIN	0.01f	// ...unless it's in another of yours, this far in (m)...
#define VRF_TURN_MARGIN		0.05f	// ...and its palm this much further up or down than that one wants (no flicker where
											// the chest and the belly meet)
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
	float		turn;		// how far the game's turned the world from your room (degrees: the pose's trackYaw)
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
	vec3_t		thrust;		// a Push's way, or a Pull's back toward where it was yanked from (unit)...
	float		speed;		// ...how fast it went (metres a second)
	int			held;		// VR_FORCE_*: what's held down
	int			cast;		// VRF_CAST: the power cast (FP_*)...
	qboolean	aimed;		// ...aimed the way thrust says (Mind Trick's wave: from your eyes through the hand at its
							// middle)
	int			place;		// the place the hand's held still in now, one with a power of yours there (VRF_PLACE_*;
							// VRF_PLACE_NONE: not)
} vrfOutput_t;

// A place (Body Gestures): a box measured from your eyes, the way your head
// faces (VRF_FromEyes) - the hand in it turned as it says - and its powers.
// Where the hand is, is where the controller's grip is: at your temple, beside
// your headset (its side some 9 or 10 cm out from your eyes), that's 13 cm
// out or more. The boxes are as players held their hands in play (the VR
// log's "held still" lines): on your chest the hand's hardly in front of your
// eyes - looking down at it, they're over it - and a palm turned up is the
// belly's from as high as your chest. The chest's box is in the belly's: how
// the palm's turned tells them apart. No two places overlap otherwise (the
// face's starts where the temple's ends, and below it the chest's and the
// belly's).
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
	{ "on your chest",	{ -0.10f, 0.20f },	{ -0.17f, 0.17f },	{ -0.44f, -0.14f },	VRF_TURN_IN,	FP_HEAL,		FP_RAGE },
	{ "before your face",	{ 0.12f, 0.35f },	{ -0.15f, 0.15f },	{ -0.14f, 0.15f },	VRF_TURN_OUT,	FP_PROTECT,		-1 },
	{ "at your belly",	{ -0.20f, 0.25f },	{ -0.20f, 0.20f },	{ -0.62f, -0.14f },	VRF_TURN_UP,	FP_ABSORB,		-1 },
	{ "at your temple",	{ -0.10f, 0.12f },	{ 0.08f, 0.22f },	{ -0.10f, 0.10f },	VRF_TURN_ANY,	FP_SEE,			-1 },
	{ "over your head",	{ -0.25f, 0.30f },	{ -0.35f, 0.35f },	{ 0.20f, 1.0f },	VRF_TURN_ANY,	FP_TEAM_HEAL,	FP_TEAM_FORCE },
};

typedef struct {
	int			time;
	vec3_t		pos;
	float		out;		// how far from its shoulder it was...
	vec3_t		eyes;		// ...where from your eyes, the way your head faced (VRF_FromEyes)...
	float		yaw;		// ...which way that was (degrees)...
	float		palm;		// ...and how far its palm faced out ahead (VRF_PalmAhead)
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
	qboolean	placed;			// a place's power (or a motion's) cast this squeeze: nothing more till it's squeezed afresh
	int			place;			// the place of yours it's held still in (VRF_PLACE_*; VRF_PLACE_NONE: none)...
	int			placeTime;		// ...since (ms)...
	vec3_t		placePos;		// ...settled there
	int			swingTime;		// the hand last going back as fast as a thrust (a swing's: ms; 0: not yet)
	int			pending;		// a Push or a Pull waiting for the motion it may be (VRF_PUSH, VRF_PULL; 0: none)...
	qboolean	pendWave;		// ...the wave (else the swing)...
	int			pendTime;		// ...since (ms)...
	vec3_t		pendWay;		// ...its way (vrfOutput_t.thrust's)...
	float		pendTurn;		// ...as the game had turned you then (vrfInput_t.turn)...
	float		pendSpeed;		// ...and how fast it went
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

// Whether place p (vrfPlaces) has a power of yours: held still there, or
// its trigger's (a place with none of yours is no place).
static inline qboolean VRF_Mine( const vrfInput_t *in, int p ) {
	return (qboolean)( VRF_Has( in, vrfPlaces[p].power ) || VRF_Has( in, vrfPlaces[p].twin ) );
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

// Whether the hand's in place p (from your eyes, e; its palm ahead): in its
// box grown by grow (m), turned as it wants less ease. Both less than 0, well
// in it: its box shrunk - and how far its palm's up stricter (where the
// chest's and the belly's meet: the rest meet in their boxes). Back toward
// you, the palm's not up: that's the belly's.
static inline qboolean VRF_InPlace( const vrfInput_t *in, const vec3_t e, float ahead, int p, float grow, float ease ) {
	const vrfPlace_t *pl = &vrfPlaces[p];

	if ( e[0] < pl->ahead[0] - grow || e[0] > pl->ahead[1] + grow || e[1] < pl->side[0] - grow || e[1] > pl->side[1] + grow ||
		e[2] < pl->up[0] - grow || e[2] > pl->up[1] + grow ) {
		return qfalse;
	}
	switch ( pl->turn ) {
	case VRF_TURN_OUT:
		return (qboolean)( ahead >= VRF_PALM_TURN - Q_max( ease, 0.0f ) );
	case VRF_TURN_IN:
		return (qboolean)( ahead <= -( VRF_PALM_IN - Q_max( ease, 0.0f ) ) && in->palmUp < VRF_PALM_UP + ease );
	case VRF_TURN_UP:
		return (qboolean)( in->palmUp >= VRF_PALM_UP - ease );
	default:
		return qtrue;
	}
}

// The place the hand's in (vrfPlaces), whoever's powers are there - keep, the
// one it's settled in (VRF_PLACE_NONE: none), as long as it's near enough
// (VRF_PLACE_KEEP, VRF_TURN_KEEP: a hand held at an edge, trembling, the
// trigger pulled, stays in it) and not well in another place of yours
// (VRF_PLACE_MARGIN, VRF_TURN_MARGIN: on your chest, the palm a little
// further up than VRF_PALM_UP, the belly's; at your belly, a little less,
// the chest's; where the face and the temple meet, or the face and the
// chest and the belly, 1 cm in - none flickering into another). None with
// Body Gestures off, or the hand not tracked now (no reading).
static inline int VRF_PlaceOf( const vrfInput_t *in, int keep ) {
	vec3_t e;
	float ahead;
	qboolean keeps;
	int i;

	if ( !in->body || !in->tracked ) {
		return VRF_PLACE_NONE;
	}
	VRF_FromEyes( in, e );
	ahead = VRF_PalmAhead( in );
	keeps = (qboolean)( keep > VRF_PLACE_NONE && keep < VRF_PLACES && VRF_InPlace( in, e, ahead, keep, VRF_PLACE_KEEP, VRF_TURN_KEEP ) );
	for ( i = VRF_PLACE_NONE + 1; i < VRF_PLACES; i++ ) {
		if ( keeps ? ( VRF_Mine( in, i ) && VRF_InPlace( in, e, ahead, i, -VRF_PLACE_MARGIN, -VRF_TURN_MARGIN ) ) :
			VRF_InPlace( in, e, ahead, i, 0.0f, 0.0f ) ) {
			return i;
		}
	}
	return keeps ? keep : VRF_PLACE_NONE;
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

// Whether a hand there (from your eyes, e: VRF_FromEyes) is before your face,
// as a wave goes: in front of your eyes, not far to either side, not far above
// or below them.
static inline qboolean VRF_WaveSpot( const vec3_t e ) {
	return (qboolean)( e[0] >= VRF_WAVE_NEAR && e[0] <= VRF_WAVE_FAR && fabsf( e[1] ) <= VRF_WAVE_WIDE && fabsf( e[2] ) <= VRF_WAVE_HIGH );
}

// A wave across before your face, the hand there now (the newest reading):
// before your face (VRF_WaveSpot) every reading back to one VRF_WAVE_ACROSS to
// the side of it, within VRF_WAVE_MS and since the Force grip was squeezed
// (st->reachTime) - never further in or out than VRF_WAVE_DEPTH as it went,
// the hand itself moved across from your eyes VRF_WAVE_MOVE that way, as your
// head faced then (your head turned, the hand held still, isn't a wave; nor
// you turning round further than it, the hand going round with you; walking,
// it goes along with you) - and its palm turned out (VRF_WAVE_PALM) halfway
// across. qtrue, and where it's aimed (aim): from your eyes through the hand
// there, halfway (unit).
static inline qboolean VRF_Wave( const vrfState_t *st, const vrfInput_t *in, vec3_t aim ) {
	const vrfSample_t *now = VRF_Sample( st, 0 ), *mid, *start;
	float nearest, furthest, half, turned, moved;
	int i, from = 0;

	if ( !st->count ) {
		return qfalse;
	}
	nearest = furthest = now->eyes[0];
	for ( i = 0; i < st->count; i++ ) {
		const vrfSample_t *s = VRF_Sample( st, i );
		if ( now->time - s->time > VRF_WAVE_MS || s->time < st->reachTime || !VRF_WaveSpot( s->eyes ) ) {
			break;
		}
		nearest = Q_min( nearest, s->eyes[0] );
		furthest = Q_max( furthest, s->eyes[0] );
		if ( furthest - nearest > VRF_WAVE_DEPTH ) {
			break;
		}
		if ( fabsf( s->eyes[1] - now->eyes[1] ) >= VRF_WAVE_ACROSS ) {
			from = i;
			break;
		}
	}
	if ( !from ) {
		return qfalse;
	}
	start = VRF_Sample( st, from );
	turned = DEG2RAD( now->yaw - start->yaw );
	moved = ( ( in->hand == VR_HAND_RIGHT ) ? -now->eyes[0] : now->eyes[0] ) * sinf( turned ) + now->eyes[1] * cosf( turned ) - start->eyes[1];
	if ( ( now->eyes[1] < start->eyes[1] ? -moved : moved ) < VRF_WAVE_MOVE ) {
		return qfalse;
	}
	half = ( start->eyes[1] + now->eyes[1] ) * 0.5f;
	mid = now;
	for ( i = 1; i < from; i++ ) {
		if ( fabsf( VRF_Sample( st, i )->eyes[1] - half ) < fabsf( mid->eyes[1] - half ) ) {
			mid = VRF_Sample( st, i );
		}
	}
	if ( mid->palm < VRF_WAVE_PALM ) {
		return qfalse;
	}
	VectorSubtract( mid->pos, in->head, aim );
	return (qboolean)( VectorNormalize( aim ) > 0.0f );
}

// A swing down and back past your hip, the hand there now (the newest reading):
// past your hip - under your chest (VRF_SWING_LOW), behind VRF_SWING_PAST, not
// across you (VRF_SWING_SIDE) - from where it was furthest forward since the
// Force grip was squeezed, within VRF_SWING_MS before it last went back as fast
// as a thrust (st->swingTime), that on the way: from your side at your waist or
// above (VRF_SWING_TOP, VRF_SWING_FRONT: not a hand held out in front,
// lowered), back at least VRF_SWING_BACK, down at least VRF_SWING_DROP,
// VRF_SWING_RUN in all - within VRF_SWING_MS of setting off from there (last
// near it, VRF_SWING_EASE, before going back fast).
static inline qboolean VRF_Swing( const vrfState_t *st ) {
	const vrfSample_t *now = VRF_Sample( st, 0 ), *top = now, *set;
	vec3_t d;
	int i, n;

	if ( !st->count || now->eyes[2] > VRF_SWING_LOW || now->eyes[0] > VRF_SWING_PAST || now->eyes[1] < VRF_SWING_SIDE ) {
		return qfalse;
	}
	for ( n = 1; n < st->count; n++ ) {
		const vrfSample_t *s = VRF_Sample( st, n );
		if ( st->swingTime - s->time > VRF_SWING_MS || s->time < st->reachTime ) {
			break;
		}
		if ( s->eyes[0] > top->eyes[0] ) {
			top = s;
		}
	}
	set = top;
	for ( i = 1; i < n; i++ ) {
		const vrfSample_t *s = VRF_Sample( st, i );
		if ( s->time <= st->swingTime && s->eyes[0] >= top->eyes[0] - VRF_SWING_EASE ) {
			set = s;
			break;
		}
	}
	VectorSubtract( now->eyes, top->eyes, d );
	return (qboolean)( st->swingTime >= top->time && now->time - set->time <= VRF_SWING_MS && top->eyes[2] >= VRF_SWING_TOP &&
		top->eyes[0] <= VRF_SWING_FRONT && -d[0] >= VRF_SWING_BACK && -d[2] >= VRF_SWING_DROP && VectorLength( d ) >= VRF_SWING_RUN );
}

// How far a way goes back, level, the way your head faces (a unit way: 1,
// straight back).
static inline float VRF_Back( const vrfInput_t *in, const vec3_t way ) {
	const float yaw = DEG2RAD( in->headYaw );

	return -( way[0] * cosf( yaw ) + way[1] * sinf( yaw ) );
}

// Whether a thrust that way (unit) may be a swing's (Body Gestures): Speed
// yours, back (VRF_BACK) and down (VRF_SWING_SINK) - the hand under your chest
// (VRF_SWING_LOW: else it's no swing's, VRF_Update finds at once).
static inline qboolean VRF_SwingWay( const vrfInput_t *in, const vec3_t dir ) {
	return (qboolean)( VRF_Has( in, FP_SPEED ) && VRF_Back( in, dir ) >= VRF_BACK && dir[2] <= -VRF_SWING_SINK );
}

// Whether a thrust or a yank that way (unit), the hand where it is now (the
// newest reading), may be a wave's (Body Gestures): Mind Trick yours, across
// (VRF_WAVE_WAY, the way your head faces), the hand before your face
// (VRF_WaveSpot).
static inline qboolean VRF_WaveWay( const vrfState_t *st, const vrfInput_t *in, const vec3_t dir ) {
	const float yaw = DEG2RAD( in->headYaw );

	return (qboolean)( VRF_Has( in, FP_TELEPATHY ) && fabsf( dir[1] * cosf( yaw ) - dir[0] * sinf( yaw ) ) >= VRF_WAVE_WAY && st->count &&
		VRF_WaveSpot( VRF_Sample( st, 0 )->eyes ) );
}

// A Push or a Pull (event: VRF_PUSH, VRF_PULL) done now, that way (unit), that
// fast: the hand settled (VRF_SETTLE) and VRF_GAP_MS on before another, no
// Grip, place's power or motion after it this squeeze - and after a Push,
// bringing the hand back in isn't a Pull. Nothing's waiting for a motion then.
static inline void VRF_Throw( vrfState_t *st, vrfOutput_t *out, int event, const vec3_t way, float speed, int time ) {
	out->events |= event;
	VectorCopy( way, out->thrust );
	out->speed = speed;
	if ( event == VRF_PUSH ) {
		st->pullArmed = qfalse;
	}
	st->throwTime = time;
	st->settled = qfalse;
	st->done = qtrue;
	st->pending = 0;
}

// The Push or the Pull waiting for a motion (st->pending) done now - the way it
// went in the world (the game turning you meanwhile: in->turn), as fast.
static inline void VRF_ThrowWaiting( vrfState_t *st, const vrfInput_t *in, vrfOutput_t *out ) {
	const float a = DEG2RAD( st->pendTurn - in->turn ), c = cosf( a ), s = sinf( a );
	vec3_t way;

	way[0] = st->pendWay[0] * c - st->pendWay[1] * s;
	way[1] = st->pendWay[0] * s + st->pendWay[1] * c;
	way[2] = st->pendWay[2];
	VRF_Throw( st, out, st->pending, way, st->pendSpeed, in->time );
}

// The Force hand read again: what it does.
static inline void VRF_Update( vrfState_t *st, const vrfInput_t *in, vrfOutput_t *out ) {
	vec3_t shoulder, way, v;
	vrfSample_t *s;
	float dist, speed = 0.0f;
	qboolean known, reachingOut, mine, ready;
	int at;

	memset( out, 0, sizeof( *out ) );
	if ( !in->free || in->mode <= VRF_MODE_OFF || in->mode > VRF_MODE_OPEN ) {
		VRF_Clear( st );	// (all let go - a Push or a Pull waiting for a motion too: the menu's open, say, or the hand lost a while)
		return;
	}

	// where it is: how far out from its shoulder, which way (a reading at the
	// same time as the last replaces it; the clock gone back, the rest go - and
	// what was going on in them: a Push or a Pull waiting for a motion is done,
	// none can come from them). The hand not tracked now - where it was last
	// seen isn't where it is - it's no reading: nothing's worked out from it
	// (how fast it goes, whether it's out), the readings before kept - seen
	// again, how fast it went meanwhile is from where it was last seen (no
	// further back than VRF_SPEED_MAX_MS).
	VRF_Shoulder( in, shoulder );
	VectorSubtract( in->pos, shoulder, way );
	dist = VectorNormalize( way );
	if ( st->count && in->time < VRF_Sample( st, 0 )->time ) {
		if ( st->pending ) {
			VRF_ThrowWaiting( st, in, out );
		}
		st->count = 0;
		st->swingTime = 0;
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
		VRF_FromEyes( in, s->eyes );
		s->yaw = in->headYaw;
		s->palm = VRF_PalmAhead( in );
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
	// the place it's in (Body Gestures) - the one it's settled in kept while it's
	// near - and whether a power of yours is there
	at = VRF_PlaceOf( in, st->place );
	mine = VRF_Mine( in, at );

	// the Force grip squeezed: reaching out. Let go: all it held goes - a Push
	// or a Pull waiting for a motion is done (no motion's coming now) - and,
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
		if ( st->pending ) {
			VRF_ThrowWaiting( st, in, out );
		}
		if ( !st->done && in->time - st->reachTime <= VRF_TAP_MS && Distance( in->pos, st->reachPos ) < VRF_TAP_MOVE ) {
			out->events |= VRF_TAP;
		}
		st->held = 0;
		st->placed = qfalse;
	}

	// Push and Pull: reaching out - or, Open hand, the hand open - a thrust
	// or a yank, fast and far enough, settled since the last (not after a
	// place's power in the squeeze); with Body Gestures, a thrust not steeply up
	// (a hand raised to a place). One that may be a motion's - a swing's
	// (VRF_SwingWay), a wave's (VRF_WaveWay) - reaching out, nothing done yet
	// this squeeze, waits for it, the way it went then, how fast (below: Body
	// Gestures off, no motion can come - it's done at once)
	if ( known && speed >= VRF_Thrust( in->thrust ) && st->settled && !st->placed && !st->pending &&
		( st->reaching || ( in->mode == VRF_MODE_OPEN && in->open ) ) &&
		( !st->throwTime || in->time - st->throwTime >= VRF_GAP_MS ) ) {
		vec3_t dir, thrown;
		float along, least, most;
		int event = 0;
		qboolean wave = qfalse, swing = qfalse;
		VectorScale( v, 1.0f / speed, dir );
		along = DotProduct( dir, way );
		VRF_OutRange( st, &least, &most );
		if ( along >= VRF_ALONG && dir[2] >= VRF_DOWN && ( !in->body || dir[2] <= VRF_UP ) && dist - least >= VRF_RUN ) {
			event = VRF_PUSH;
			VectorCopy( dir, thrown );
			swing = VRF_SwingWay( in, dir );
			wave = VRF_WaveWay( st, in, dir );
		} else if ( along <= -VRF_ALONG && -dir[2] >= VRF_DOWN && most - dist >= VRF_RUN && st->pullArmed && st->outTime &&
			in->time - st->outTime <= VRF_PULL_MS ) {
			event = VRF_PULL;
			VectorScale( dir, -1.0f, thrown );
			wave = VRF_WaveWay( st, in, dir );
		}
		if ( event && st->reaching && !st->done && ( wave || swing ) ) {
			st->pending = event;
			st->pendWave = wave;
			st->pendTime = in->time;
			VectorCopy( thrown, st->pendWay );
			st->pendTurn = in->turn;
			st->pendSpeed = speed;
		} else if ( event ) {
			VRF_Throw( st, out, event, thrown, speed, in->time );
		}
	}

	// the motions (Body Gestures): reaching out, nothing done yet this squeeze -
	// in the readings it's had since it was squeezed - a wave across before your
	// face, Mind Trick, aimed from your eyes through the hand halfway across
	// (VRF_Wave); a swing down and back past your hip, Speed (VRF_Swing: going
	// back as fast as a thrust on the way) - each if it's yours, spending the
	// squeeze as a place's power does
	if ( known && speed >= VRF_Thrust( in->thrust ) && VRF_Back( in, v ) >= VRF_BACK * speed ) {
		st->swingTime = in->time;	// (before the squeeze, it's no swing's: VRF_Swing's from where it was since)
	}
	if ( in->body && st->reaching && !st->done ) {
		if ( VRF_Has( in, FP_TELEPATHY ) && VRF_Wave( st, in, out->thrust ) ) {
			out->events |= VRF_CAST;
			out->cast = FP_TELEPATHY;
			out->aimed = qtrue;
			st->placed = st->done = qtrue;
		} else if ( VRF_Has( in, FP_SPEED ) && VRF_Swing( st ) ) {
			out->events |= VRF_CAST;
			out->cast = FP_SPEED;
			st->placed = st->done = qtrue;
		}
	}

	// a Push or a Pull waiting for the motion it may be: the motion cast, it's
	// gone; the motion not coming - Body Gestures off, the hand slowed down or
	// gone from where the motion goes (before your face, a wave; under your
	// chest, a swing), or VRF_PEND_MS up (the hand lost meanwhile, the wait
	// goes on) - it's done now, the way it went (the Force grip let go or the
	// trigger pulled, it's done then: there)
	if ( st->pending ) {
		if ( st->placed ) {
			st->pending = 0;
		} else if ( !in->body || ( known && speed < VRF_SETTLE ) ||
			( st->pendWave ? !VRF_WaveSpot( VRF_Sample( st, 0 )->eyes ) : VRF_Sample( st, 0 )->eyes[2] > VRF_SWING_LOW ) ||
			in->time - st->pendTime >= VRF_PEND_MS ) {
			VRF_ThrowWaiting( st, in, out );
		}
	}

	// the trigger pulled, reaching out: in a place with a dark twin of yours,
	// that, at once - whatever else the squeeze has done (Lightning there by a
	// hair, say: the trigger's meant) - but with the palm turned up, not the
	// chest's (kept there turned up a little: Drain, as come to so); kept in
	// one with none (your belly, the palm come back toward you), the place
	// it's in as it is. Elsewhere, Lightning - with the palm up, Drain - held
	// as long as it is. Grip gives way to either. After a place's power,
	// nothing. A Push or a Pull waiting for a motion is done first (the game's
	// Lightning, going, would stop it).
	if ( in->trigger && !st->triggerWas && st->reaching && !st->placed ) {
		const int twinAt = VRF_Has( in, vrfPlaces[at].twin ) ? at : VRF_PlaceOf( in, VRF_PLACE_NONE );
		if ( st->pending ) {
			VRF_ThrowWaiting( st, in, out );
		}
		if ( VRF_Has( in, vrfPlaces[twinAt].twin ) && ( vrfPlaces[twinAt].turn != VRF_TURN_IN || in->palmUp < VRF_PALM_UP ) ) {
			out->events |= VRF_CAST;
			out->cast = vrfPlaces[twinAt].twin;
			st->placed = qtrue;
			st->held = 0;
		} else {
			st->held = ( in->palmUp >= VRF_PALM_UP ) ? VR_FORCE_DRAIN : VR_FORCE_LIGHTNING;
		}
		st->done = qtrue;
	}
	if ( !in->trigger ) {
		st->held &= ~( VR_FORCE_LIGHTNING | VR_FORCE_DRAIN );
	}
	st->triggerWas = in->trigger;

	// Grip, if it's yours: reaching out, nothing done yet this squeeze (once a
	// Push, a Pull or the trigger is, not till it's squeezed afresh), held out
	// still a moment - the palm not turned up (Drain's to come), not in a place
	// of yours, nor over your head - then held as long as the Force grip is
	if ( st->reaching && !st->done && VRF_Has( in, FP_GRIP ) ) {
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

	// a place of yours, held still - reaching out: settled there slower than
	// VRF_STILL, staying while it's never off further than VRF_PLACE_DRIFT from
	// where it settled (further, but slow: it settles afresh). Nothing done yet
	// this squeeze, held so in->hold seconds (VRF_Hold), its power - if it's
	// yours (one with only its twin is the trigger's). Something done (a Push,
	// a Pull, the trigger, Grip), only a place with a twin of yours is settled
	// in - for its trigger, its power not cast; a place's power cast, none. No
	// reading: as it was - and nothing cast till there's one (the hand may be
	// anywhere meanwhile).
	ready = st->done ? VRF_Has( in, vrfPlaces[at].twin ) : mine;
	if ( !st->reaching || st->placed ) {
		st->place = VRF_PLACE_NONE;
	} else if ( known ) {
		if ( ready && st->place == at && Distance( in->pos, st->placePos ) <= VRF_PLACE_DRIFT ) {
			;	// (still there)
		} else if ( ready && speed < VRF_STILL ) {
			st->place = at;
			st->placeTime = in->time;
			VectorCopy( in->pos, st->placePos );
		} else {
			st->place = VRF_PLACE_NONE;
		}
	}
	if ( known && st->place && !st->done && VRF_Has( in, vrfPlaces[st->place].power ) && in->time - st->placeTime >= VRF_Hold( in->hold ) ) {
		out->events |= VRF_CAST;
		out->cast = vrfPlaces[st->place].power;
		st->placed = st->done = qtrue;
		st->place = VRF_PLACE_NONE;
	}
	out->place = st->place;
	out->held = st->held;
}
