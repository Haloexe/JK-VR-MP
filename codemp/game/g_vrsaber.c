/*
===========================================================================
OpenJK VR (multiplayer) - physical lightsaber combat.

A VR player's client sends the pose of the saber blades in their hands with
every usercmd (see qcommon/vr_public.h). For those players the server
fights with those blades instead of the animated ones:

  - The blade is where the hand put it (but the hilt never goes through a
	wall, and walls stop the blade). Between server frames the damage sweep
	goes through every pose that arrived, so fast swings don't skip through
	people.
  - Damage comes from how fast the blade moves where it hits, measured in
	the player's real room (so running, snap turning or the camera easing
	down a step doesn't cut), and only as far as the arm drives it (a flick
	of the wrist barely cuts). The saber's weight, picked by the saber style
	(fast = light, medium, strong = heavy), sets how fast a cut has to be
	and how hard it hits.
  - A blade that meets another blade stops there and can't cut until it has
	recovered; the other side gets the usual parry, bounce or knock-away.
	The stronger blow (weight, plus a full-speed swing) wins: the loser
	recovers longer, and a heavy blow that out-weighs a guard by far breaks
	it and carries on through, for half damage.
  - Whichever blade it is (dual sabers, a staff's two ends), a player cuts
	the same opponent at most once in VR_SABER_CUT_REPEAT ms.
  - A saber held with both hands swings one weight heavier (a heavy one
	counts one stronger in clashes), and either hand drives the swing.
  - Blocking is whatever your blade physically stops, both ways: blades
	meet only where their cores touch, at the same moment (every blade is
	followed through the frame, a VR one pose by pose, an animated one
	between its frames). An animated blade (bots, players without VR)
	that meets a VR blade bounces off it (G_VRSaberBlocksAnimated); the
	saber's box round a VR blade stops nothing. Bots, NPCs and players
	without VR see a VR swing coming and raise their guard into its path
	(G_VRSaberGuard: their automatic block) - it has to meet the blade to
	stop it.
  - The attack button doesn't swing a saber held in VR (the client drops
	it, and so does the server).

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "g_local.h"
#include "bg_saga.h"
#include "w_saber.h"

// from w_saber.c
void WP_SaberDamageAdd( int trVictimEntityNum, vec3_t trDmgDir, vec3_t trDmgSpot, int trDmg, qboolean doDismemberment, int knockBackFlags );
void WP_SaberBounceSound( gentity_t *ent, int saberNum, int bladeNum );
void WP_SaberBlockNonRandom( gentity_t *self, vec3_t hitloc, qboolean missileBlock );
int WP_SaberCanBlock( gentity_t *self, vec3_t point, int dflags, int mod, qboolean projectile, int attackStr );
qboolean WP_VR_G2TraceCollide( trace_t *tr, vec3_t start, vec3_t end, vec3_t mins, vec3_t maxs );
qboolean WP_VR_SaberAttacking( gentity_t *ent );
void WP_VR_BreakParry( gentity_t *victim, vec3_t point );
void WP_VR_KnockAttack( gentity_t *attacker );
qboolean PM_SaberInBrokenParry( int move );
qboolean BG_SabersOff( playerState_t *ps );
qboolean WP_SaberBladeUseSecondBladeStyle( saberInfo_t *saber, int bladeNum );
void G_Knockdown( gentity_t *victim );

#define VR_SABER_SAMPLES	32		// poses kept per player (about a third of a second)
#define VR_SABER_MIN_GAP	4		// ms: a pose closer than this to the last one isn't kept
#define VR_SABER_GAP		150		// ms between poses that we don't sweep across
#define VR_SABER_LATCH		2000	// ms a player still counts as fighting in VR without poses (lag)
#define VR_SABER_FRESH		200		// ms the newest blade pose is used for
#define VR_SABER_REACH		64.0f	// a hilt or hand can't be further than this from the camera
#define VR_SABER_VIEW_XY	16.0f	// the camera's smoothing can't take it further than this sideways...
#define VR_SABER_VIEW_Z		48.0f	// ...or up and down from the eye
#define VR_SABER_FAST		1.5f	// units/ms: faster than a player or a hand really moves
#define VR_SABER_TELEPORT	48.0f	// the camera jumping this much more than VR_SABER_FAST: a teleport
#define VR_SABER_JUMP		8.0f	// a blade jumping this much more than VR_SABER_FAST: a glitch
#define VR_SABER_SPEED_MS	100		// window the swing speed is measured over
#define VR_SABER_LEVER		3.0f	// a blade point counts at most this many times the hand's speed:
									// flicking the wrist with the arm still barely cuts
#define VR_SABER_STEP		4.0f	// max blade travel per collision step (units)
#define VR_SABER_MAX_STEPS	16		// collision steps from one pose to the next...
#define VR_SABER_FRAME_STEPS 48		// ...and per blade per server frame
#define VR_SABER_CUT_REPEAT	300		// ms before any of a player's blades cuts the same victim again
#define VR_SABER_CUTS		8		// victims remembered for that
#define VR_SABER_THROUGH_MS	200		// a blow that broke a guard cuts for half damage this long
#define VR_SABER_BURN		1		// damage of a blade just touching someone
#define VR_SABER_ACTIVITY	8.0f	// moving a hand this far counts as activity (g_inactivity)
#define VR_SABER_TWO_HANDS	40.0f	// two hands on one hilt are no further apart than this (the client lets
									// go 24 from where it holds, which can be a hilt-length from the first)
#define VR_UNITS_PER_METRE	34.0f	// (default vr_worldScale) for the m/s in prints and the weight cvars
#define VR_SABER_CORE		1.5f	// a blade's core: two blades meet where their cores touch (not their glow)
#define VR_SABER_BLOCK_MS	250		// an animated swing stopped on a VR blade cuts nothing this long
#define VR_SABER_GUARD_AHEAD 0.25f	// s: how far ahead a bot sees a VR swing coming at it...
#define VR_SABER_GUARD_NEAR	18.0f	// ...this near its body...
#define VR_SABER_GUARD_AT	15.0f	// ...and guards where it gets this near
#define VR_UNLAG_MAX		250		// ms: the furthest back a VR player's view of the others is taken
#define VR_UNLAG_SLACK		25		// ms: their blade meeting the VR blade this long after the blow still stopped it
#define VR_UNLAG_LEAD		VR_UNLAG_SLACK	// ms of a VR player's moves past a blow on them before it lands
									// (as far as a meeting that still stops it can come after it)
#define VR_UNLAG_REACH		300.0f	// players this near a VR player are taken back for the cuts of their sweep

typedef struct vrSaberSample_s {
	int				time;			// usercmd serverTime (clamped like ClientThink_real does)
	vec3_t			cam;			// the camera the hands were seen from (world)
	float			trackYaw;		// world yaw minus the player's room yaw
	int				teleport;		// the player's EF_TELEPORT_BIT then
	int				bladeMask[VR_POSE_MAX_SABERS];
	int				breakMask[VR_POSE_MAX_SABERS];	// blades that don't carry on from the pose before
	vec3_t			grip[VR_POSE_MAX_SABERS];		// the hand holding the saber (world)
	qboolean		twoHanded[VR_POSE_MAX_SABERS];	// the other hand holds it too...
	vec3_t			grip2[VR_POSE_MAX_SABERS];		// ...here (world; else the same as grip)
	vec3_t			base[VR_POSE_MAX_SABERS][VR_POSE_MAX_BLADES];	// world
	vec3_t			dir[VR_POSE_MAX_SABERS][VR_POSE_MAX_BLADES];
} vrSaberSample_t;

typedef struct vrSaberState_s {
	vrSaberSample_t	sample[VR_SABER_SAMPLES];	// ring buffer, newest at [head]
	int				head;
	int				count;				// consecutive samples
	qboolean		inVR;				// a saber is in the player's VR hands (as of the newest pose)
	int				poseTime;			// level.time the newest pose arrived
	int				sweptTime[VR_POSE_MAX_SABERS][VR_POSE_MAX_BLADES];	// sample time the damage sweep reached
	int				recoverTime;		// after a clash the blades cut nothing until then
	int				clashEnt;			// last clash (sparks are debounced)
	int				clashTime;
	int				clashSeen;			// (a VR blade's) last level.time the blades were seen together
	int				throughEnt;			// last guard a heavy blow broke through
	int				throughTime;
	struct {
		int			ent;
		int			time;				// level.time (0: unused)
	}				cuts[VR_SABER_CUTS];	// recent cuts, by victim
	int				hitTime[MAX_CLIENTS];	// (lag compensation) the last time the blades hurt each player
	int				hitVrTime[MAX_CLIENTS];	// (level.time), and the player's command time of it
	qboolean		activitySet;
	vec3_t			activity;			// where in the room a hand last moved to (g_inactivity)
	struct {							// the swing under way (VRS_TrackSwing)
		qboolean	on;
		qboolean	cutting;			// fast enough to cut (the whoosh went out)
		int			start, lastFast;	// sample times
		float		peak, peakBlade, peakHand;	// units/s (the speed it cuts with; the blade's; the hand's)
		int			weight;				// vrSaberWeights index at the peak
		qboolean	twoHanded;
		int			cuts, clashes;
	}				swing;
} vrSaberState_t;

static vrSaberState_t	vrSabers[MAX_CLIENTS];
static qboolean			vrPosesAvailable;	// the engine hands out poses (sv_vrx, vr_engine)
static qboolean			vrsAvatarSet[MAX_CLIENTS];	// an avatar was handed to the engine (VRS_TakeAvatar)

static void VRS_ResetUnlag( int clientNum );			// (lag compensation, below)
static void VRS_RecordHistory( void );
static void VRS_SettlePending( void );
static void VRS_CancelPending( int attacker, int victim, int since, int swing );
static int VRS_SwingId( int c );
static int VRS_HistBladeAt( int c, int s, int b, float t, const vec3_t nowBase, const vec3_t nowTip, int nowTime,
	vec3_t base, vec3_t tip, int *attacking, int *swing );

/*
--------------------------------------------------------------------------------
Weights: the saber style picks how heavy the blade swings
--------------------------------------------------------------------------------
*/

typedef struct vrSaberWeight_s {
	const char	*name;
	float		minSpeed;	// units/s where a swing starts to cut
	float		fullSpeed;	// units/s for full damage
	float		maxDamage;
	int			recover;	// ms the blade can't cut after an even clash
	int			strength;	// in clashes: 1 light .. 3 heavy (+1 for a full-speed swing)
} vrSaberWeight_t;

enum {
	VRS_LIGHT, VRS_MEDIUM, VRS_HEAVY,
	VRS_NUM_WEIGHTS,
	VRS_HEAVY2 = VRS_NUM_WEIGHTS,		// heavy, held with both hands: one stronger in clashes
	VRS_NUM_ALL_WEIGHTS
};

// (the defaults of g_vrSaberLight / Medium / Heavy: 1.8 5.3 25 150 and so on.
// A full-speed heavy blow takes 50 of a player's 100-125 health and armour:
// it takes two or three of those, or more slower cuts, to bring someone down.)
static const vrSaberWeight_t vrSaberDefaults[VRS_NUM_WEIGHTS] = {
	{ "light",	1.8f * VR_UNITS_PER_METRE, 5.3f * VR_UNITS_PER_METRE, 25.0f, 150, 1 },
	{ "medium",	2.5f * VR_UNITS_PER_METRE, 7.1f * VR_UNITS_PER_METRE, 40.0f, 220, 2 },
	{ "heavy",	3.2f * VR_UNITS_PER_METRE, 8.8f * VR_UNITS_PER_METRE, 50.0f, 300, 3 },
};
static vrSaberWeight_t vrSaberWeights[VRS_NUM_ALL_WEIGHTS] = {
	{ "light",	1.8f * VR_UNITS_PER_METRE, 5.3f * VR_UNITS_PER_METRE, 25.0f, 150, 1 },
	{ "medium",	2.5f * VR_UNITS_PER_METRE, 7.1f * VR_UNITS_PER_METRE, 40.0f, 220, 2 },
	{ "heavy",	3.2f * VR_UNITS_PER_METRE, 8.8f * VR_UNITS_PER_METRE, 50.0f, 300, 3 },
	{ "heavy",	3.2f * VR_UNITS_PER_METRE, 8.8f * VR_UNITS_PER_METRE, 50.0f, 300, 4 },
};

// The cvars' defaults (as g_xcvar.h has them), and what they were before -
// when blows did two thirds more and a full-speed heavy one killed, and then
// when a heavy one did 60: a config that saved an old one gets today's.
#define VRS_OLD_DEFAULTS	2
static const char *vrSaberCvarNames[VRS_NUM_WEIGHTS] = { "g_vrSaberLight", "g_vrSaberMedium", "g_vrSaberHeavy" };
static const char *vrSaberCvarDefaults[VRS_NUM_WEIGHTS] = { "1.8 5.3 25 150", "2.5 7.1 40 220", "3.2 8.8 50 300" };
static const char *vrSaberOldDefaults[VRS_NUM_WEIGHTS][VRS_OLD_DEFAULTS] = {
	{ "1.8 5.3 40 150", NULL },
	{ "2.5 7.1 65 220", NULL },
	{ "3.2 8.8 100 300", "3.2 8.8 60 300" },
};

// The server's g_vrSaberLight / Medium / Heavy changed: "cut from (m/s)
// full damage at (m/s) max damage recover (ms)", e.g. "2.5 7.1 40 220".
void G_VRSaberWeightsChanged( void ) {
	const vmCvar_t *cv[VRS_NUM_WEIGHTS] = { &g_vrSaberLight, &g_vrSaberMedium, &g_vrSaberHeavy };
	int i;

	for ( i = 0; i < VRS_NUM_WEIGHTS; i++ ) {
		float cutFrom, fullAt, maxDamage;
		int recover, k;
		qboolean old = qfalse;

		vrSaberWeights[i] = vrSaberDefaults[i];
		if ( !cv[i]->string[0] ) {
			continue;
		}
		for ( k = 0; k < VRS_OLD_DEFAULTS; k++ ) {
			if ( vrSaberOldDefaults[i][k] && !strcmp( cv[i]->string, vrSaberOldDefaults[i][k] ) ) {
				old = qtrue;
			}
		}
		if ( old ) {
			// an old default, saved in a config: today's instead
			trap->Cvar_Set( vrSaberCvarNames[i], vrSaberCvarDefaults[i] );
			continue;
		}
		if ( sscanf( cv[i]->string, "%f %f %f %d", &cutFrom, &fullAt, &maxDamage, &recover ) != 4 ||
			!( cutFrom > 0.0f ) || !( fullAt > cutFrom ) || !( maxDamage >= 1.0f ) || recover < 0 || recover > 5000 ) {
			Com_Printf( S_COLOR_YELLOW "VR saber: g_vrSaber%c%s should be \"cut-from-m/s full-at-m/s max-damage recover-ms\", "
				"e.g. \"%.1f %.1f %.0f %d\"; using that\n", vrSaberDefaults[i].name[0] - 32, vrSaberDefaults[i].name + 1,
				vrSaberDefaults[i].minSpeed / VR_UNITS_PER_METRE, vrSaberDefaults[i].fullSpeed / VR_UNITS_PER_METRE,
				vrSaberDefaults[i].maxDamage, vrSaberDefaults[i].recover );
			continue;
		}
		vrSaberWeights[i].minSpeed = cutFrom * VR_UNITS_PER_METRE;
		vrSaberWeights[i].fullSpeed = fullAt * VR_UNITS_PER_METRE;
		vrSaberWeights[i].maxDamage = maxDamage;
		vrSaberWeights[i].recover = recover;
	}
	vrSaberWeights[VRS_HEAVY2] = vrSaberWeights[VRS_HEAVY];
	vrSaberWeights[VRS_HEAVY2].strength = vrSaberWeights[VRS_HEAVY].strength + 1;
}

// The weight the saber style gives.
static int VRS_StyleWeight( const gclient_t *cl ) {
	switch ( cl->ps.fd.saberAnimLevel ) {
	case SS_FAST:
	case SS_TAVION:
		return VRS_LIGHT;
	case SS_STRONG:
	case SS_DESANN:
		return VRS_HEAVY;
	case SS_DUAL:	// dual sabers: two medium blades
	case SS_STAFF:
	default:
		return VRS_MEDIUM;
	}
}

static qboolean VRS_TwoHanded( gentity_t *ent );
static void VRS_TrackSwing( gentity_t *ent, vrSaberState_t *vs );
static qboolean VRS_BladeOn( const gclient_t *cl, int s, int b );

// The weight a player's saber swings with: the style's, one heavier held
// with both hands.
static const vrSaberWeight_t *VRS_Weight( gentity_t *ent ) {
	int w = VRS_StyleWeight( ent->client );

	if ( VRS_TwoHanded( ent ) ) {
		w = ( w >= VRS_HEAVY ) ? VRS_HEAVY2 : w + 1;
	}
	return &vrSaberWeights[w];
}

static float VRS_SpeedScale( void ) {
	return Q_max( 0.1f, g_vrSaberSpeedScale.value );
}

/*
--------------------------------------------------------------------------------
Geometry
--------------------------------------------------------------------------------
*/

static void VRS_Lerp( const vec3_t a, float f, const vec3_t b, vec3_t out ) {
	out[0] = a[0] + f * ( b[0] - a[0] );
	out[1] = a[1] + f * ( b[1] - a[1] );
	out[2] = a[2] + f * ( b[2] - a[2] );
}

// Unit vector a fraction f of the way from a to b (both unit vectors).
static void VRS_SlerpDir( const vec3_t a, const vec3_t b, float f, vec3_t out ) {
	const float d = Com_Clamp( -1.0f, 1.0f, DotProduct( a, b ) );
	vec3_t axis;

	if ( d > 0.9999f ) {
		VRS_Lerp( a, f, b, out );
		VectorNormalize( out );
		return;
	}
	CrossProduct( a, b, axis );
	if ( VectorNormalize( axis ) < 0.0001f ) {
		PerpendicularVector( axis, a );	// opposite: turn about any perpendicular
	}
	RotatePointAroundVector( out, axis, a, RAD2DEG( acosf( d ) ) * f );
	VectorNormalize( out );
}

// Closest points between segments p1-q1 and p2-q2; returns their distance.
static float VRS_SegSegClosest( const vec3_t p1, const vec3_t q1, const vec3_t p2, const vec3_t q2, vec3_t c1, vec3_t c2 ) {
	const float eps = 0.0001f;
	vec3_t d1, d2, r;
	float a, e, f, s, t;

	VectorSubtract( q1, p1, d1 );
	VectorSubtract( q2, p2, d2 );
	VectorSubtract( p1, p2, r );
	a = DotProduct( d1, d1 );
	e = DotProduct( d2, d2 );
	f = DotProduct( d2, r );

	if ( a <= eps && e <= eps ) {
		s = t = 0.0f;
	} else if ( a <= eps ) {
		s = 0.0f;
		t = Com_Clamp( 0.0f, 1.0f, f / e );
	} else {
		const float c = DotProduct( d1, r );
		if ( e <= eps ) {
			t = 0.0f;
			s = Com_Clamp( 0.0f, 1.0f, -c / a );
		} else {
			const float b = DotProduct( d1, d2 );
			const float denom = a * e - b * b;
			s = ( denom > eps ) ? Com_Clamp( 0.0f, 1.0f, ( b * f - c * e ) / denom ) : 0.0f;
			t = ( b * s + f ) / e;
			if ( t < 0.0f ) {
				t = 0.0f;
				s = Com_Clamp( 0.0f, 1.0f, -c / a );
			} else if ( t > 1.0f ) {
				t = 1.0f;
				s = Com_Clamp( 0.0f, 1.0f, ( b - c ) / a );
			}
		}
	}
	VectorMA( p1, s, d1, c1 );
	VectorMA( p2, t, d2, c2 );
	return Distance( c1, c2 );
}

// Does segment p-q pass through triangle v0 v1 v2?
static qboolean VRS_SegTri( const vec3_t p, const vec3_t q, const vec3_t v0, const vec3_t v1, const vec3_t v2, vec3_t hit ) {
	vec3_t dir, e1, e2, h, s, qv;
	float a, f, u, v, t;

	VectorSubtract( q, p, dir );
	VectorSubtract( v1, v0, e1 );
	VectorSubtract( v2, v0, e2 );
	CrossProduct( dir, e2, h );
	a = DotProduct( e1, h );
	if ( fabsf( a ) < 0.000001f ) {
		return qfalse;
	}
	f = 1.0f / a;
	VectorSubtract( p, v0, s );
	u = f * DotProduct( s, h );
	if ( u < 0.0f || u > 1.0f ) {
		return qfalse;
	}
	CrossProduct( s, e1, qv );
	v = f * DotProduct( dir, qv );
	if ( v < 0.0f || u + v > 1.0f ) {
		return qfalse;
	}
	t = f * DotProduct( e2, qv );
	if ( t < 0.0f || t > 1.0f ) {
		return qfalse;
	}
	VectorMA( p, t, dir, hit );
	return qtrue;
}

// Our blade moved from base0-tip0 to base1-tip1: did it meet the blade o0-o1
// (closer than radius at the end, or passing through it on the way)?
static qboolean VRS_BladesMeet( const vec3_t base0, const vec3_t tip0, const vec3_t base1, const vec3_t tip1,
	const vec3_t o0, const vec3_t o1, float radius, vec3_t point ) {
	vec3_t c1, c2;

	if ( VRS_SegSegClosest( base1, tip1, o0, o1, c1, c2 ) < radius ) {
		VRS_Lerp( c1, 0.5f, c2, point );
		return qtrue;
	}
	if ( VRS_SegTri( o0, o1, base0, tip0, tip1, point ) || VRS_SegTri( o0, o1, base0, tip1, base1, point ) ) {
		return qtrue;
	}
	return qfalse;
}

// A blade from base towards tip, only as far as it gets before a wall.
static void VRS_ClipBlade( int passEnt, const vec3_t base, const vec3_t tip, vec3_t out ) {
	trace_t tr;

	trap->Trace( &tr, base, NULL, NULL, tip, passEnt, MASK_SOLID, qfalse, 0, 0 );
	if ( tr.allsolid ) {
		VectorCopy( base, out );	// all in the wall
	} else {
		VectorCopy( tr.endpos, out );
	}
}

static void VRS_ClampLength( vec3_t v, float max ) {
	const float len = VectorLength( v );

	if ( len > max ) {
		VectorScale( v, max / len, v );
	}
}

// cam + ofs, but no further than arm's reach from the camera or the eye.
static void VRS_ClampReach( const vec3_t eye, const vec3_t cam, vec3_t ofs, vec3_t out ) {
	vec3_t d;

	VRS_ClampLength( ofs, VR_SABER_REACH );
	VectorAdd( cam, ofs, out );
	VectorSubtract( out, eye, d );
	VRS_ClampLength( d, VR_SABER_REACH );
	VectorAdd( eye, d, out );
}

/*
--------------------------------------------------------------------------------
Poses
--------------------------------------------------------------------------------
*/

void G_VRSaberInit( void ) {
	char tag[64];

	memset( vrSabers, 0, sizeof( vrSabers ) );
	memset( vrsAvatarSet, 0, sizeof( vrsAvatarSet ) );
	VRS_ResetUnlag( -1 );

	// Only this engine has gameImport_t.ext: it sets vr_engine itself (never
	// sent to clients, so a stock engine never has it) and offers sv_vrx.
	trap->Cvar_VariableStringBuffer( "vr_engine", tag, sizeof( tag ) );
	vrPosesAvailable = (qboolean)( !strcmp( tag, VR_ENGINE_TAG ) &&
		trap->Cvar_VariableIntegerValue( "sv_vrx" ) == VR_POSE_PROTOCOL );

	trap->Cvar_Register( NULL, "vrSaber", "0", CVAR_SERVERINFO | CVAR_ROM );
	G_VRSaberPublish();
}

// "vrSaber 1" in the serverinfo tells VR clients that this server fights
// with their real blades, so they stop sending the attack button with them
// (and server browsers can show it).
qboolean G_VRSaberOn( void ) {
	return (qboolean)( vrPosesAvailable && g_vrSaber.integer > 0 );
}

void G_VRSaberPublish( void ) {
	trap->Cvar_Set( "vrSaber", G_VRSaberOn() ? "1" : "0" );
}

void G_VRSaberClientReset( int clientNum ) {
	if ( clientNum >= 0 && clientNum < MAX_CLIENTS ) {
		memset( &vrSabers[clientNum], 0, sizeof( vrSabers[clientNum] ) );
		VRS_ResetUnlag( clientNum );
		if ( vrPosesAvailable && trap->ext.VR_SetAvatar ) {
			trap->ext.VR_SetAvatar( clientNum, NULL );
		}
		vrsAvatarSet[clientNum] = qfalse;
	}
}

static vrSaberState_t *VRS_State( gentity_t *ent ) {
	const int n = ent - g_entities;
	return ( n >= 0 && n < MAX_CLIENTS ) ? &vrSabers[n] : NULL;
}

// The i-th newest pose.
static vrSaberSample_t *VRS_Sample( vrSaberState_t *vs, int i ) {
	return &vs->sample[( vs->head - i + VR_SABER_SAMPLES ) % VR_SABER_SAMPLES];
}

// A world point of a pose, relative to its camera and turned back into the
// player's real room: only real arm movement changes it.
static void VRS_RoomOffset( const vrSaberSample_t *smp, const vec3_t point, vec3_t out ) {
	const float c = cosf( DEG2RAD( -smp->trackYaw ) );
	const float sn = sinf( DEG2RAD( -smp->trackYaw ) );
	vec3_t rel;

	VectorSubtract( point, smp->cam, rel );
	out[0] = rel[0] * c - rel[1] * sn;
	out[1] = rel[0] * sn + rel[1] * c;
	out[2] = rel[2];
}

/*
--------------------------------------------------------------------------------
The avatar: what everyone else sees of a VR player (qcommon/vr_public.h)
--------------------------------------------------------------------------------
*/

// Is this player's VR body to be shown to the others at all? (Not dead,
// spectating, following someone, or between maps.)
static qboolean VRS_AvatarShown( gentity_t *ent ) {
	const gclient_t *cl = ent->client;

	if ( !ent->inuse || !cl || cl->pers.connected != CON_CONNECTED || ent->health < 1 ) {
		return qfalse;
	}
	if ( cl->sess.sessionTeam == TEAM_SPECTATOR || ( cl->ps.pm_flags & PMF_FOLLOW ) || ( cl->ps.eFlags & EF_DEAD ) ) {
		return qfalse;
	}
	if ( cl->ps.pm_type == PM_SPECTATOR || cl->ps.pm_type == PM_DEAD || cl->ps.pm_type == PM_INTERMISSION ||
		cl->ps.pm_type == PM_SPINTERMISSION ) {
		return qfalse;
	}
	return qtrue;
}

static float		vrsBodyYaw[MAX_CLIENTS];	// the body's yaw in the avatar handed to the engine

// Which way a VR player's body faces, as everyone draws it (their avatar).
qboolean G_VRSaberBodyYaw( int clientNum, float *yaw ) {
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS || !vrsAvatarSet[clientNum] ) {
		return qfalse;
	}
	*yaw = vrsBodyYaw[clientNum];
	return qtrue;
}

static void VRS_ClearAvatar( int clientNum ) {
	if ( vrsAvatarSet[clientNum] ) {
		trap->ext.VR_SetAvatar( clientNum, NULL );
		vrsAvatarSet[clientNum] = qfalse;
	}
}

// Unit vector, or the fallback if it's nonsense.
static void VRS_SafeDir( vec3_t v, const vec3_t fallback ) {
	if ( !( VectorNormalize( v ) > 0.5f ) ) {
		VectorCopy( fallback, v );
	}
}

// The avatar that came with this usercmd (offsets from the camera the player
// saw their hands from), within reach, for the other players (the engine
// sends it with their snapshots). The hands go as offsets from the player's
// origin before this command moves it - where the body was when they saw
// them - so they move with the body wherever it is drawn.
static void VRS_TakeAvatar( gentity_t *ent, const vrUserPose_t *pose ) {
	static const vec3_t fwd = { 1.0f, 0.0f, 0.0f };
	const int clientNum = ent - g_entities;
	gclient_t *cl = ent->client;
	vrAvatar_t a;
	vec3_t eye, cam, view;
	float len;
	int h;

	if ( !vrPosesAvailable || !trap->ext.VR_SetAvatar ) {
		return;	// (an engine without it: never touch its ext)
	}
	if ( !pose || !pose->avatar.valid || !VRS_AvatarShown( ent ) ) {
		VRS_ClearAvatar( clientNum );
		return;
	}
	a = pose->avatar;
	a.valid = qtrue;

	// the camera the hands were seen from: the eye, moved only by the
	// camera's own smoothing (as for the blades)
	VectorCopy( cl->ps.origin, eye );
	eye[2] += cl->ps.viewheight;
	VectorCopy( pose->viewOffset, view );
	len = sqrtf( view[0] * view[0] + view[1] * view[1] );
	if ( len > VR_SABER_VIEW_XY ) {
		view[0] *= VR_SABER_VIEW_XY / len;
		view[1] *= VR_SABER_VIEW_XY / len;
	}
	view[2] = Com_Clamp( -VR_SABER_VIEW_Z, VR_SABER_VIEW_Z, view[2] );
	VectorAdd( eye, view, cam );

	a.bodyYaw = AngleNormalize360( a.bodyYaw );
	for ( h = 0; h < 3; h++ ) {
		a.headAngles[h] = AngleNormalize180( a.headAngles[h] );
	}
	len = sqrtf( a.shift[0] * a.shift[0] + a.shift[1] * a.shift[1] );
	if ( len > 48.0f ) {
		a.shift[0] *= 48.0f / len;	// (the body never wanders off from where the player is)
		a.shift[1] *= 48.0f / len;
	}
	a.shift[2] = Com_Clamp( -48.0f, 48.0f, a.shift[2] );

	for ( h = 0; h < 2; h++ ) {
		vrAvatarHand_t *hand = &a.hand[h];
		vec3_t ofs;
		if ( hand->grip <= VR_GRIP_NONE || hand->grip >= VR_NUM_GRIPS ||
			( !G_VRSaberOn() && ( hand->grip == VR_GRIP_SABER || hand->grip == VR_GRIP_HILT ) ) ) {
			// (with the game's own saber combat the animation swings it, as
			// everyone's blows are judged)
			memset( hand, 0, sizeof( *hand ) );
			continue;
		}
		VectorCopy( hand->origin, ofs );
		VRS_ClampReach( eye, cam, ofs, hand->origin );
		VRS_SafeDir( hand->axis[0], fwd );
		VectorMA( hand->axis[2], -DotProduct( hand->axis[2], hand->axis[0] ), hand->axis[0], hand->axis[2] );
		if ( !( VectorNormalize( hand->axis[2] ) > 0.5f ) ) {
			PerpendicularVector( hand->axis[2], hand->axis[0] );
		}
		CrossProduct( hand->axis[2], hand->axis[0], hand->axis[1] );
		VRS_SafeDir( hand->dir, hand->axis[0] );
		hand->blend = Com_Clamp( 0.0f, 1.0f, hand->blend == hand->blend ? hand->blend : 0.0f );
		VectorSubtract( hand->origin, cl->ps.origin, hand->origin );
	}
	trap->ext.VR_SetAvatar( clientNum, &a );
	vrsAvatarSet[clientNum] = qtrue;
	vrsBodyYaw[clientNum] = a.bodyYaw;
}

// The game's poses for standing still - most of them the saber stances, which
// crouch (the model's eyes 6 to 8 units under the eye point).
static qboolean VRS_StandingAnim( const gclient_t *cl, int anim ) {
	switch ( anim ) {
	case BOTH_STAND1:
	case BOTH_STAND1IDLE1:
	case BOTH_STAND2:
	case BOTH_STAND2IDLE1:
	case BOTH_STAND2IDLE2:
	case BOTH_STAND1TO2:
	case BOTH_STAND2TO1:
	case BOTH_SABERFAST_STANCE:
	case BOTH_SABERSLOW_STANCE:
	case BOTH_SABERDUAL_STANCE:
	case BOTH_SABERSTAFF_STANCE:
		return qtrue;
	default:
		// (a saber's own stance, from its .sab)
		return (qboolean)( ( cl->saber[0].readyAnim != -1 && anim == cl->saber[0].readyAnim ) ||
			( cl->saber[1].readyAnim != -1 && anim == cl->saber[1].readyAnim ) );
	}
}

// How the model hit tests use (G_UpdateClientAnims) is posed for a VR player
// whose body everyone draws from their avatar: as cg_vrbody.c's
// CG_VR_BodyAnim draws it - upright through flips and rolls, as they are in
// their headset, and standing up straight while they stand still (its head
// where theirs is, hands on it where theirs are) - so blades meet the body
// where it's seen.
void G_VRSaberBodyAnim( gentity_t *ent, int *legsAnim, int *torsoAnim ) {
	const gclient_t *cl = ent->client;
	const animation_t *anims;
	int n = ent->s.number, anim;
	qboolean roll;

	if ( !cl || n < 0 || n >= MAX_CLIENTS || !vrsAvatarSet[n] || !VRS_AvatarShown( ent ) ) {
		return;
	}
	if ( cl->ps.m_iVehicleNum || cl->ps.weapon == WP_EMPLACED_GUN || ( cl->ps.eFlags2 & EF2_HELD_BY_MONSTER ) ||
		ent->s.heldByClient ) {
		return;	// (the game's own posing wins, as it does where the body is drawn)
	}
	anims = ( ent->localAnimIndex >= 0 ) ? bgAllAnims[ent->localAnimIndex].anims : NULL;
	if ( !anims || ent->localAnimIndex > 1 ) {
		return;	// (not a humanoid)
	}
	roll = BG_InRoll( (playerState_t *)&cl->ps, *legsAnim );
	if ( roll || BG_FlippingAnim( *legsAnim ) || BG_InSpecialJump( *legsAnim ) ) {
		anim = roll ? BOTH_CROUCH1IDLE : BOTH_FORCEINAIR1;
		if ( !anims[anim].numFrames ) {
			anim = BOTH_INAIR1;
			if ( !anims[anim].numFrames ) {
				return;
			}
		}
		*legsAnim = *torsoAnim = anim;
		return;
	}
	anim = anims[BOTH_STAND5].numFrames ? BOTH_STAND5 : BOTH_STAND1;
	if ( !anims[anim].numFrames ) {
		return;
	}
	if ( VRS_StandingAnim( cl, *legsAnim ) ) {
		*legsAnim = anim;
	}
	if ( VRS_StandingAnim( cl, *torsoAnim ) ) {
		*torsoAnim = anim;
	}
}

// Once a frame, after everyone moved: animated blades' cuts on VR players
// that waited for their moves land (or were blocked); where everyone and
// their animated blades are now is kept (lag compensation); and avatars of
// players who died, started spectating or left the game go (their animation
// shows again).
void G_VRSaberEndFrame( void ) {
	int i;

	if ( !vrPosesAvailable ) {
		return;
	}
	VRS_SettlePending();
	VRS_RecordHistory();
	if ( !trap->ext.VR_SetAvatar ) {
		return;
	}
	for ( i = 0; i < level.maxclients && i < MAX_CLIENTS; i++ ) {
		if ( vrsAvatarSet[i] && !VRS_AvatarShown( &g_entities[i] ) ) {
			VRS_ClearAvatar( i );
		}
	}
}

// Called for every usercmd of a real client, before it is thought (the
// player state still has the eye point the pose was rendered with).
void G_VRSaberUsercmd( gentity_t *ent, usercmd_t *ucmd ) {
	const int clientNum = ent - g_entities;
	gclient_t *cl = ent->client;
	vrSaberState_t *vs;
	vrSaberSample_t *smp;
	const vrSaberSample_t *prev;
	vrUserPose_t pose;
	vec3_t eye, view;
	float len;
	int s, b, t, dt;
	qboolean jumped;

	if ( clientNum < 0 || clientNum >= MAX_CLIENTS || !cl ) {
		return;
	}
	vs = &vrSabers[clientNum];
	if ( !vrPosesAvailable || !trap->ext.VR_GetUserPose( clientNum, &pose ) ) {
		memset( &pose, 0, sizeof( pose ) );
	}
	VRS_TakeAvatar( ent, &pose );	// (what everyone else sees, saber or not)
	if ( !G_VRSaberOn() || !pose.valid ) {
		vs->count = 0;		// the stream broke: never sweep across the gap
		vs->inVR = qfalse;	// and the client says there's no saber in its VR hands
		return;
	}

	// A lit saber in the player's VR hands: the arm swings it, the attack
	// button doesn't. (The client already drops it from the command.)
	ucmd->buttons &= ~BUTTON_ATTACK;
	vs->inVR = qtrue;
	vs->poseTime = level.time;

	// (the clamp ClientThink_real puts on the command's time)
	t = Com_Clampi( level.time - 1000, level.time + 200, ucmd->serverTime );
	prev = vs->count ? VRS_Sample( vs, 0 ) : NULL;
	if ( prev && t < prev->time - 1000 ) {
		vs->count = 0;	// the clock went back (restart): start over
		memset( vs->sweptTime, 0, sizeof( vs->sweptTime ) );
		prev = NULL;
	}
	if ( prev && t - prev->time < VR_SABER_MIN_GAP ) {
		return;	// not newer, or too close to the last pose to be worth keeping
	}

	VectorCopy( cl->ps.origin, eye );
	eye[2] += cl->ps.viewheight;

	// the camera the hands were seen from: the eye, moved only by the
	// camera's own smoothing
	VectorCopy( pose.viewOffset, view );
	len = sqrtf( view[0] * view[0] + view[1] * view[1] );
	if ( len > VR_SABER_VIEW_XY ) {
		view[0] *= VR_SABER_VIEW_XY / len;
		view[1] *= VR_SABER_VIEW_XY / len;
	}
	view[2] = Com_Clamp( -VR_SABER_VIEW_Z, VR_SABER_VIEW_Z, view[2] );

	vs->head = ( vs->head + 1 ) % VR_SABER_SAMPLES;
	if ( vs->count < VR_SABER_SAMPLES ) {
		vs->count++;
	}
	smp = VRS_Sample( vs, 0 );	// (never prev: that is the one before)
	memset( smp, 0, sizeof( *smp ) );
	smp->time = t;
	VectorAdd( eye, view, smp->cam );
	smp->trackYaw = pose.trackYaw;
	smp->teleport = cl->ps.eFlags & EF_TELEPORT_BIT;
	dt = prev ? t - prev->time : 0;

	// teleported or respawned: nothing carries on from before
	jumped = (qboolean)( prev && ( prev->teleport != smp->teleport ||
		Distance( prev->cam, smp->cam ) > VR_SABER_TELEPORT + VR_SABER_FAST * dt ) );

	for ( s = 0; s < VR_POSE_MAX_SABERS; s++ ) {
		const int mask = pose.bladeMask[s] & ( ( 1 << VR_POSE_MAX_BLADES ) - 1 );
		vec3_t ofs;

		if ( !mask ) {
			continue;
		}
		VectorCopy( pose.grip[s], ofs );
		VRS_ClampReach( eye, smp->cam, ofs, smp->grip[s] );
		VectorCopy( smp->grip[s], smp->grip2[s] );
		if ( pose.twoHanded[s] && s == 0 && !( pose.bladeMask[1] & ( ( 1 << VR_POSE_MAX_BLADES ) - 1 ) ) ) {
			// the other hand on this hilt too (it can't be holding another saber)
			vec3_t grip2;
			VectorCopy( pose.grip2[s], ofs );
			VRS_ClampReach( eye, smp->cam, ofs, grip2 );
			if ( Distance( grip2, smp->grip[s] ) <= VR_SABER_TWO_HANDS ) {
				VectorCopy( grip2, smp->grip2[s] );
				smp->twoHanded[s] = qtrue;
			}
		}

		for ( b = 0; b < VR_POSE_MAX_BLADES; b++ ) {
			vec3_t dir;
			trace_t tr;

			if ( !( mask & ( 1 << b ) ) ) {
				continue;
			}
			VectorCopy( pose.blade[s][b].dir, dir );
			if ( !( VectorNormalize( dir ) > 0.5f ) ) {
				continue;	// nonsense
			}
			VectorCopy( dir, smp->dir[s][b] );

			// within arm's reach, and a hand can go through a wall but the
			// hilt stays on this side of it
			VectorCopy( pose.blade[s][b].base, ofs );
			VRS_ClampReach( eye, smp->cam, ofs, smp->base[s][b] );
			trap->Trace( &tr, eye, NULL, NULL, smp->base[s][b], clientNum, MASK_SOLID, qfalse, 0, 0 );
			if ( tr.fraction < 1.0f && !tr.startsolid && !tr.allsolid ) {
				VectorCopy( tr.endpos, smp->base[s][b] );
			}
			smp->bladeMask[s] |= 1 << b;

			// a blade that jumped (faster than any hand) doesn't carry on
			if ( jumped ) {
				smp->breakMask[s] |= 1 << b;
			} else if ( prev && ( prev->bladeMask[s] & ( 1 << b ) ) ) {
				vec3_t p0, p1;
				VRS_RoomOffset( prev, prev->base[s][b], p0 );
				VRS_RoomOffset( smp, smp->base[s][b], p1 );
				if ( Distance( p0, p1 ) > VR_SABER_JUMP + VR_SABER_FAST * dt ) {
					smp->breakMask[s] |= 1 << b;
				}
			}
		}
	}

	// moving a hand counts as being active (g_inactivity), like walking or
	// attacking does
	for ( s = 0; s < VR_POSE_MAX_SABERS; s++ ) {
		vec3_t room;

		if ( !smp->bladeMask[s] ) {
			continue;
		}
		VRS_RoomOffset( smp, smp->grip[s], room );
		if ( !vs->activitySet || Distance( room, vs->activity ) > VR_SABER_ACTIVITY ) {
			VectorCopy( room, vs->activity );
			vs->activitySet = qtrue;
			if ( g_inactivity.integer ) {
				cl->inactivityTime = level.time + g_inactivity.integer * 1000;
				cl->inactivityWarning = qfalse;
			}
			if ( cl->idleTime < level.time ) {
				cl->idleTime = level.time;	// no idle flourishes in the middle of a fight
			}
		}
		break;
	}

	VRS_TrackSwing( ent, vs );
}

// Whether this player fights with a saber in their VR hands: no automatic
// blocks, saber locks or disarms for them. (Still true for a moment when no
// poses come in, through lag, so lag doesn't hand out automatic blocks.)
qboolean G_VRSaberClient( gentity_t *ent ) {
	vrSaberState_t *vs;

	if ( !ent || !ent->inuse || !ent->client || !G_VRSaberOn() ) {
		return qfalse;
	}
	vs = VRS_State( ent );
	if ( !vs || !vs->inVR || level.time - vs->poseTime > VR_SABER_LATCH ) {
		return qfalse;
	}
	if ( ent->client->ps.weapon != WP_SABER || ent->health < 1 ) {
		return qfalse;
	}
	return qtrue;
}

// Whether this player's blades are where their VR hands put them right now.
qboolean G_VRSaberActive( gentity_t *ent ) {
	vrSaberState_t *vs;

	if ( !G_VRSaberClient( ent ) ) {
		return qfalse;
	}
	vs = VRS_State( ent );
	return (qboolean)( vs->count && level.time - vs->poseTime <= VR_SABER_FRESH );
}

// The newest pose of a blade held in VR, if there is one.
qboolean G_VRSaberBlade( gentity_t *ent, int saberNum, int bladeNum, vec3_t base, vec3_t dir ) {
	vrSaberState_t *vs;
	const vrSaberSample_t *smp;

	if ( saberNum < 0 || saberNum >= VR_POSE_MAX_SABERS || bladeNum < 0 || bladeNum >= VR_POSE_MAX_BLADES ) {
		return qfalse;
	}
	if ( !G_VRSaberActive( ent ) ) {
		return qfalse;
	}
	vs = VRS_State( ent );
	smp = VRS_Sample( vs, 0 );
	if ( !( smp->bladeMask[saberNum] & ( 1 << bladeNum ) ) ) {
		return qfalse;
	}
	VectorCopy( smp->base[saberNum][bladeNum], base );
	VectorCopy( smp->dir[saberNum][bladeNum], dir );
	return qtrue;
}

// Whether this player holds their saber with both hands right now.
static qboolean VRS_TwoHanded( gentity_t *ent ) {
	vrSaberState_t *vs;

	if ( !G_VRSaberActive( ent ) ) {
		return qfalse;
	}
	vs = VRS_State( ent );
	return VRS_Sample( vs, 0 )->twoHanded[0];
}

/*
--------------------------------------------------------------------------------
How fast a blade moves
--------------------------------------------------------------------------------
*/

enum { VRS_POINT_BLADE, VRS_POINT_GRIP, VRS_POINT_GRIP2 };

// The poses of one blade that carry on from each other, newest first: back
// to where the blade appeared, or jumped (teleport, tracking glitch).
static int VRS_BladeHistory( vrSaberState_t *vs, int s, int b, const vrSaberSample_t **out ) {
	int i, n = 0;

	for ( i = 0; i < vs->count; i++ ) {
		const vrSaberSample_t *smp = VRS_Sample( vs, i );
		if ( !( smp->bladeMask[s] & ( 1 << b ) ) ) {
			break;
		}
		out[n++] = smp;
		if ( smp->breakMask[s] & ( 1 << b ) ) {
			break;
		}
	}
	return n;
}

// The two poses of a blade's history around time t, and how far t is from
// the older to the newer.
static qboolean VRS_Bracket( const vrSaberSample_t **hist, int n, float t, const vrSaberSample_t **older,
	const vrSaberSample_t **newer, float *f ) {
	int i;

	*older = *newer = NULL;
	*f = 0.0f;
	for ( i = 0; i < n; i++ ) {
		if ( hist[i]->time <= t ) {
			*older = hist[i];
			break;
		}
		*newer = hist[i];
	}
	if ( !*older ) {
		return qfalse;	// t is before the blade's history
	}
	if ( *newer && ( *newer )->time > ( *older )->time ) {
		*f = Com_Clamp( 0.0f, 1.0f, ( t - ( *older )->time ) / (float)( ( *newer )->time - ( *older )->time ) );
	} else {
		*newer = *older;
	}
	return qtrue;
}

// Where a point 'along' the blade (or a hand holding it) was at time t, in
// the player's room (see VRS_RoomOffset). The second hand only while it was
// on the hilt.
static qboolean VRS_RoomPoint( const vrSaberSample_t **hist, int n, int s, int b, float t, float along, int which, vec3_t out ) {
	const vrSaberSample_t *newer, *older;
	vec3_t p[2];
	float f;
	int k;

	if ( !VRS_Bracket( hist, n, t, &older, &newer, &f ) ) {
		return qfalse;
	}
	if ( which == VRS_POINT_GRIP2 && ( !older->twoHanded[s] || !newer->twoHanded[s] ) ) {
		return qfalse;
	}
	for ( k = 0; k < 2; k++ ) {
		const vrSaberSample_t *smp = k ? newer : older;
		vec3_t point;
		if ( which == VRS_POINT_GRIP ) {
			VectorCopy( smp->grip[s], point );
		} else if ( which == VRS_POINT_GRIP2 ) {
			VectorCopy( smp->grip2[s], point );
		} else {
			VectorMA( smp->base[s][b], along, smp->dir[s][b], point );
		}
		VRS_RoomOffset( smp, point, p[k] );
	}
	VRS_Lerp( p[0], f, p[1], out );
	return qtrue;
}

// Which way the blade pointed at time t, in the player's room.
static qboolean VRS_RoomDir( const vrSaberSample_t **hist, int n, int s, int b, float t, vec3_t out ) {
	const vrSaberSample_t *newer, *older;
	vec3_t d[2];
	float f;
	int k;

	if ( !VRS_Bracket( hist, n, t, &older, &newer, &f ) ) {
		return qfalse;
	}
	for ( k = 0; k < 2; k++ ) {
		const vrSaberSample_t *smp = k ? newer : older;
		const float c = cosf( DEG2RAD( -smp->trackYaw ) );
		const float sn = sinf( DEG2RAD( -smp->trackYaw ) );
		d[k][0] = smp->dir[s][b][0] * c - smp->dir[s][b][1] * sn;
		d[k][1] = smp->dir[s][b][0] * sn + smp->dir[s][b][1] * c;
		d[k][2] = smp->dir[s][b][2];
	}
	VRS_Lerp( d[0], f, d[1], out );
	return (qboolean)( VectorNormalize( out ) > 0.1f );
}

// How fast that point moved over the last moment before t (units/s, in the
// player's room). The second hand only counts while it held the hilt the
// whole time, and only as it moved across the blade: sliding along the hilt
// doesn't move it.
static float VRS_PointSpeed( const vrSaberSample_t **hist, int n, int s, int b, float t, float along, int which ) {
	vec3_t p0, p1, d;
	float t0 = t - VR_SABER_SPEED_MS;

	if ( !n || !VRS_RoomPoint( hist, n, s, b, t, along, which, p1 ) ) {
		return 0.0f;
	}
	if ( !VRS_RoomPoint( hist, n, s, b, t0, along, which, p0 ) ) {
		// shorter history: from the oldest pose of this blade
		t0 = hist[n - 1]->time;
		if ( t - t0 < 20.0f || !VRS_RoomPoint( hist, n, s, b, t0, along, which, p0 ) ) {
			return 0.0f;
		}
	}
	VectorSubtract( p1, p0, d );
	if ( which == VRS_POINT_GRIP2 ) {
		vec3_t a0, a1, axis;
		if ( !VRS_RoomDir( hist, n, s, b, t0, a0 ) || !VRS_RoomDir( hist, n, s, b, t, a1 ) ) {
			return 0.0f;
		}
		VectorAdd( a0, a1, axis );
		if ( VectorNormalize( axis ) > 0.1f ) {
			VectorMA( d, -DotProduct( d, axis ), axis, d );
		}
	}
	return VectorLength( d ) / ( ( t - t0 ) * 0.001f );
}

// The speed a blade point hits with: how fast it moved, but no more than a
// few times what the hand did, so a flick of the wrist with a still arm
// barely cuts (the tip of a long blade moves fast even then). With both
// hands on the hilt, the faster hand drives it (one pushing, the other
// pulling is a real swing).
static float VRS_SwingSpeed( vrSaberState_t *vs, int s, int b, float t, float along ) {
	const vrSaberSample_t *hist[VR_SABER_SAMPLES];
	const int n = VRS_BladeHistory( vs, s, b, hist );
	const float pointSpeed = VRS_PointSpeed( hist, n, s, b, t, along, VRS_POINT_BLADE );
	const float handSpeed = Q_max( VRS_PointSpeed( hist, n, s, b, t, 0.0f, VRS_POINT_GRIP ),
		VRS_PointSpeed( hist, n, s, b, t, 0.0f, VRS_POINT_GRIP2 ) );

	return Q_min( pointSpeed, handSpeed * VR_SABER_LEVER );
}

/*
--------------------------------------------------------------------------------
Swings: told to everyone (the whoosh) and written to the log
--------------------------------------------------------------------------------
*/

#define VR_SABER_SWING_START	0.5f	// a swing starts at this much of the speed a cut needs...
#define VR_SABER_SWING_END		0.35f	// ...and ends when it has been slower than this...
#define VR_SABER_SWING_REST		120		// ...for this long (ms)

static void VRS_EndSwing( gentity_t *ent, vrSaberState_t *vs ) {
	if ( g_vrSaberLog.integer && vs->swing.weight >= 0 && vs->swing.weight < VRS_NUM_ALL_WEIGHTS ) {
		const vrSaberWeight_t *w = &vrSaberWeights[vs->swing.weight];
		const float u = 1.0f / VR_UNITS_PER_METRE, scale = VRS_SpeedScale();
		G_LogPrintf( "VRSaber: swing %i \"%s\" %s%s: %.1f m/s (blade %.1f, hand %.1f; cuts from %.1f, full at %.1f), "
			"%i ms, %i cut%s, %i clash%s\n", (int)( ent - g_entities ), ent->client->pers.netname, w->name,
			vs->swing.twoHanded ? " two-handed" : "", vs->swing.peak * u, vs->swing.peakBlade * u, vs->swing.peakHand * u,
			w->minSpeed * scale * u, w->fullSpeed * scale * u, vs->swing.lastFast - vs->swing.start,
			vs->swing.cuts, vs->swing.cuts == 1 ? "" : "s", vs->swing.clashes, vs->swing.clashes == 1 ? "" : "es" );
	}
	memset( &vs->swing, 0, sizeof( vs->swing ) );
}

// After every pose: how fast the blades swing (where they cut best, two
// thirds along), so a swing's whoosh goes out to the other players and the
// log says how hard each swing was.
static void VRS_TrackSwing( gentity_t *ent, vrSaberState_t *vs ) {
	const vrSaberSample_t *smp = VRS_Sample( vs, 0 );
	const vrSaberWeight_t *w = VRS_Weight( ent );
	const float minSpeed = w->minSpeed * VRS_SpeedScale();
	const vrSaberSample_t *hist[VR_SABER_SAMPLES];
	float best = 0.0f, bestBlade = 0.0f, bestHand = 0.0f;
	int s, b;

	for ( s = 0; s < VR_POSE_MAX_SABERS; s++ ) {
		for ( b = 0; b < VR_POSE_MAX_BLADES; b++ ) {
			float along, blade, hand, speed;
			int n;
			if ( !( smp->bladeMask[s] & ( 1 << b ) ) || !VRS_BladeOn( ent->client, s, b ) ) {
				continue;
			}
			n = VRS_BladeHistory( vs, s, b, hist );
			along = 0.66f * ent->client->saber[s].blade[b].lengthMax;
			blade = VRS_PointSpeed( hist, n, s, b, smp->time, along, VRS_POINT_BLADE );
			hand = Q_max( VRS_PointSpeed( hist, n, s, b, smp->time, 0.0f, VRS_POINT_GRIP ),
				VRS_PointSpeed( hist, n, s, b, smp->time, 0.0f, VRS_POINT_GRIP2 ) );
			speed = Q_min( blade, hand * VR_SABER_LEVER );
			if ( speed > best ) {
				best = speed;
				bestBlade = blade;
				bestHand = hand;
			}
		}
	}

	if ( !vs->swing.on ) {
		if ( best < VR_SABER_SWING_START * minSpeed ) {
			return;
		}
		memset( &vs->swing, 0, sizeof( vs->swing ) );
		vs->swing.on = qtrue;
		vs->swing.start = vs->swing.lastFast = smp->time;
	}
	if ( best >= VR_SABER_SWING_END * minSpeed ) {
		vs->swing.lastFast = smp->time;
	}
	if ( best > vs->swing.peak ) {
		vs->swing.peak = best;
		vs->swing.peakBlade = bestBlade;
		vs->swing.peakHand = bestHand;
		vs->swing.weight = (int)( w - vrSaberWeights );
		vs->swing.twoHanded = VRS_TwoHanded( ent );
	}
	if ( !vs->swing.cutting && best >= minSpeed ) {
		// fast enough to cut: everyone else hears it (the player's own game
		// already played it; bots notice it as an attack)
		vs->swing.cutting = qtrue;
		G_AddEvent( ent, EV_SABER_ATTACK, 1 + (int)( w - vrSaberWeights ) );
	}
	if ( smp->time - vs->swing.lastFast > VR_SABER_SWING_REST || smp->time - vs->swing.start > 3000 ) {
		VRS_EndSwing( ent, vs );
	}
}

/*
--------------------------------------------------------------------------------
Blade against blade
--------------------------------------------------------------------------------
*/

// Is this blade of that player lit and held?
static qboolean VRS_BladeOn( const gclient_t *cl, int s, int b ) {
	const qboolean dual = cl->saber[1].model[0] ? qtrue : qfalse;

	if ( !cl->saber[s].model[0] || b >= cl->saber[s].numBlades || cl->saber[s].blade[b].lengthMax <= 0.0f ) {
		return qfalse;
	}
	if ( s == 0 && cl->ps.saberInFlight ) {
		return qfalse;	// thrown
	}
	if ( dual ) {
		if ( cl->ps.saberHolstered > 1 || ( s == 1 && cl->ps.saberHolstered == 1 ) ) {
			return qfalse;
		}
	} else if ( cl->saber[0].numBlades > 1 ) {
		if ( cl->ps.saberHolstered > 1 || ( b > 0 && cl->ps.saberHolstered == 1 ) ) {
			return qfalse;
		}
	} else if ( cl->ps.saberHolstered ) {
		return qfalse;
	}
	return qtrue;
}

static int VRS_Strength( const vrSaberWeight_t *w, float speed ) {
	return w->strength + ( speed >= w->fullSpeed * VRS_SpeedScale() ? 1 : 0 );
}

// Does a blow (strength, at speed) break the guard or attack it meets
// (theirs) and carry on through? Only a heavy blade swung at full speed,
// and only one two or more stronger; anything less is parried.
static qboolean VRS_Breaks( const vrSaberWeight_t *w, float speed, int strength, int theirs ) {
	return (qboolean)( ( w == &vrSaberWeights[VRS_HEAVY] || w == &vrSaberWeights[VRS_HEAVY2] ) &&
		speed >= w->fullSpeed * VRS_SpeedScale() && strength >= theirs + 2 );
}

// Swinging at someone does what starting an attack does: spawn protection
// ends, and bots take note.
static void VRS_Attacked( gentity_t *self ) {
	self->client->dangerTime = level.time;
	self->client->ps.eFlags &= ~EF_INVULNERABLE;
	self->client->invulnerableTimer = 0;
}

typedef struct {
	gentity_t	*ent;
	int			saber, blade;
	vec3_t		base, tip;			// now (time1)...
	vec3_t		base0, tip0;		// ...and the frame before (time0): it's followed in between
	int			time0, time1;		// (level times)
	int			histClient;			// (lag compensation) an animated blade of this client, taken where
									// it was when the swinging player saw it (-1: as it is now)
} vrsOtherBlade_t;

// Where another blade was for the swinging player at their command time t:
// an animated blade where it was when they saw it (lag compensation, its
// history), any other at level time t + shift, between its last two frames.
// *attacking: whether its owner was mid attack then, *swing: which attack
// (VRS_SwingId) (-1: not known). qfalse: it wasn't there then (not lit).
static qboolean VRS_OtherBladeAt( const vrsOtherBlade_t *ob, float t, int shift, vec3_t base, vec3_t tip, int *attacking,
	int *swing ) {
	float f;

	*attacking = -1;
	*swing = -1;
	if ( ob->histClient >= 0 ) {
		const int found = VRS_HistBladeAt( ob->histClient, ob->saber, ob->blade, t, ob->base, ob->tip, ob->time1, base, tip,
			attacking, swing );
		if ( found ) {
			return (qboolean)( found > 0 );
		}
	}
	if ( !ob->time1 ) {
		return qfalse;	// (not lit now, and nothing kept of it)
	}
	t += shift;
	f = ( ob->time1 > ob->time0 ) ? Com_Clamp( 0.0f, 1.0f, ( t - ob->time0 ) / (float)( ob->time1 - ob->time0 ) ) : 1.0f;
	VRS_Lerp( ob->base0, f, ob->base, base );
	VRS_Lerp( ob->tip0, f, ob->tip, tip );
	return qtrue;
}

// How a clash ends for the blade that swung into the other one. Ours stops
// and can't cut until it has recovered (unless it broke through); a VR
// opponent's blade recovers too:
//	           ours   theirs
//	even        1x     1x
//	won         1x     2x     (bots: their parry breaks, their swing is knocked away)
//	lost        2x     1/2x
//	through     -      2x     (heavy blows only: ours carries on, for half damage)
typedef enum {
	VRS_CLASH_EVEN,
	VRS_CLASH_WON,
	VRS_CLASH_LOST,
	VRS_CLASH_THROUGH,
	VRS_CLASH_HELD		// (both in VR) theirs was the faster blade: its owner's sweep decides
} vrsClash_t;

static const char *const vrsClashNames[] = { "even", "won", "lost", "broke through", "held" };

// How fast another VR player's blade moves at a point on it.
static float VRS_BladeSpeedAt( vrSaberState_t *ovs, const vrsOtherBlade_t *ob, const vec3_t point ) {
	vec3_t dir, rel;
	float len;

	if ( ob->saber >= VR_POSE_MAX_SABERS || ob->blade >= VR_POSE_MAX_BLADES || !ovs->count ) {
		return 0.0f;
	}
	VectorSubtract( ob->tip, ob->base, dir );
	len = VectorNormalize( dir );
	VectorSubtract( point, ob->base, rel );
	return VRS_SwingSpeed( ovs, ob->saber, ob->blade, VRS_Sample( ovs, 0 )->time,
		Com_Clamp( 0.0f, len, DotProduct( rel, dir ) ) );
}

// Our blade, swinging at this speed, hit another VR player's blade: both
// sweeps see this contact, so the faster blade's owner decides - the
// stronger blow wins (weight, plus one for a full-speed swing), a heavy
// one at full speed two or more stronger goes on through (VRS_Breaks) -
// and their blade recovers the other way round. (Animated blades:
// VRS_MeetAnimated.)
static vrsClash_t VRS_ClashReact( gentity_t *self, const vrsOtherBlade_t *ob, vec3_t point, float speed ) {
	gentity_t *other = ob->ent;
	const vrSaberWeight_t *w = VRS_Weight( self );
	const vrSaberWeight_t *ow = VRS_Weight( other );
	const int strength = VRS_Strength( w, speed );
	vrSaberState_t *ovs = VRS_State( other );	// (NULL: an NPC - a jedi's own block, VRS_Hit)
	const float theirSpeed = ovs ? VRS_BladeSpeedAt( ovs, ob, point ) : 0.0f;
	const int theirs = VRS_Strength( ow, theirSpeed );
	vrsClash_t result;
	int recover;

	if ( theirSpeed > speed ) {
		return VRS_CLASH_HELD;
	}
	if ( VRS_Breaks( w, speed, strength, theirs ) ) {
		result = VRS_CLASH_THROUGH;
		recover = 2 * ow->recover;
	} else if ( strength > theirs ) {
		result = VRS_CLASH_WON;
		recover = 2 * ow->recover;
	} else if ( strength == theirs ) {
		result = VRS_CLASH_EVEN;
		recover = ow->recover;
	} else {
		result = VRS_CLASH_LOST;
		recover = ow->recover / 2;
	}
	if ( ovs && ovs->recoverTime < level.time + recover ) {
		ovs->recoverTime = level.time + recover;
	}
	return result;
}

// Our blade met another VR player's blade at point: sparks, and the
// stronger blow wins. Returns qtrue if ours carries on (it broke their
// guard), qfalse if it stops there.
static qboolean VRS_Clash( gentity_t *self, int s, int b, const vrsOtherBlade_t *ob, vec3_t point, const vec3_t moveDir, float speed ) {
	gentity_t *other = ob->ent;
	vrSaberState_t *vs = VRS_State( self );
	const vrSaberWeight_t *w = VRS_Weight( self );
	const qboolean swing = (qboolean)( speed >= w->minSpeed * VRS_SpeedScale() );
	const vrSaberState_t *ovs;
	vrsClash_t result;
	int recover = 0;

	if ( vs->clashEnt == other->s.number && level.time - vs->clashSeen < 150 ) {
		// the same clash going on: the blades held together - a VR blade
		// rests on the other one (cg_vrbody.c), sparking in the hand that
		// holds it - quietly, until a swing from there makes a new clash; a
		// blow that broke through keeps going
		vs->clashSeen = level.time;
		if ( !swing || level.time - vs->clashTime < 150 ) {
			return (qboolean)( vs->throughEnt == other->s.number && level.time - vs->throughTime < VR_SABER_THROUGH_MS );
		}
	}

	// sparks, unless the other VR player's sweep already reported this
	ovs = VRS_State( other );	// (NULL: an NPC)
	if ( !ovs || ovs->clashEnt != self->s.number || level.time - ovs->clashTime >= 150 ) {
		gentity_t *te = G_TempEntity( point, EV_SABER_BLOCK );
		VectorCopy( point, te->s.origin );
		VectorScale( moveDir, -1.0f, te->s.angles );
		if ( VectorCompare( te->s.angles, vec3_origin ) ) {
			te->s.angles[2] = 1.0f;
		}
		te->s.eventParm = 1;
		te->s.otherEntityNum2 = self->s.number;
		te->s.weapon = s;
		te->s.legsAnim = b;
		WP_SaberBounceSound( self, s, b );
	}
	vs->clashEnt = other->s.number;
	vs->clashTime = vs->clashSeen = level.time;

	if ( !swing ) {
		if ( g_vrSaberDebug.integer ) {
			Com_Printf( "VR saber: %s's blade touched %s's\n", self->client->pers.netname,
				other->client ? other->client->pers.netname : "?" );
		}
		return qfalse;
	}

	result = VRS_ClashReact( self, ob, point, speed );
	VRS_Attacked( self );
	switch ( result ) {
	case VRS_CLASH_THROUGH:
		vs->throughEnt = other->s.number;
		vs->throughTime = level.time;
		break;
	case VRS_CLASH_WON:
	case VRS_CLASH_EVEN:
		recover = w->recover;
		break;
	case VRS_CLASH_LOST:
		recover = 2 * w->recover;
		break;
	case VRS_CLASH_HELD:
		break;	// (their sweep sets how long ours recovers)
	}
	if ( recover && vs->recoverTime < level.time + recover ) {
		vs->recoverTime = level.time + recover;
	}
	if ( vs->swing.on ) {
		vs->swing.clashes++;
	}
	if ( g_vrSaberDebug.integer ) {
		Com_Printf( "VR saber: %s's %s%s blade met %s's at %.1f m/s: %s\n", self->client->pers.netname, w->name,
			VRS_TwoHanded( self ) ? " (two hands)" : "", other->client ? other->client->pers.netname : "?",
			speed / VR_UNITS_PER_METRE, vrsClashNames[result] );
	}
	if ( g_vrSaberLog.integer ) {
		G_LogPrintf( "VRSaber: clash %i \"%s\" %s%s %.1f m/s vs \"%s\": %s\n", self->s.number, self->client->pers.netname,
			w->name, VRS_TwoHanded( self ) ? " two-handed" : "", speed / VR_UNITS_PER_METRE,
			other->client ? other->client->pers.netname : "?", vrsClashNames[result] );
	}
	return (qboolean)( result == VRS_CLASH_THROUGH );
}

/*
VR blades against animated ones (bots, NPCs, players not in VR). Both sides
look for these contacts: the VR player's sweep as their blade moves
(G_VRSaberSweep), and the animated blade's check before its damage traces
(G_VRSaberBlocksAnimated). Whichever player comes first in the frame sees
the contact first and settles it; the other side goes along with that.
*/
#define VR_SABER_MET_MS		150		// a contact goes on while the blades keep touching within this

static struct {
	int			vrEnt;			// the VR player whose blade met this saber...
	int			time;			// ...when (renewed while the blades keep touching)
	qboolean	attacking;		// this saber's owner was attacking then...
	qboolean	vrSwing;		// ...the VR blade swinging
	qboolean	vrGoesOn;		// the VR blade broke through (it carries on, for half damage)
} vrsMet[MAX_GENTITIES][MAX_SABERS];

// A VR player's blade (s, b) and an animated one (other's saberNum,
// bladeNum) met at point, the VR blade moving at speed there. The animated
// blade stops: it cuts nothing for a moment. An attack meeting the VR blade
// bounces off - a stronger swing (or a much stronger guard) knocks it away,
// a much stronger attack jars the VR blade; a VR swing into a guard is
// parried, unless it's a heavy one at full speed two stronger, which
// breaks it and goes on through (VRS_Breaks). Sparks fly back from the
// faster blade (moveDir). Returns qtrue if the VR blade goes on through.
// (vrTime: when the VR player saw it, their command time; attackingAt:
// whether the other was attacking then, swingAt: which attack (VRS_SwingId),
// -1 if not known - lag compensation finds some contacts a moment after they
// happened, and their owner may have moved on to something else since: then
// only the sparks, and the blows of that attack on the VR player never land.)
static qboolean VRS_MeetAnimated( gentity_t *vr, int s, int b, gentity_t *other, int saberNum, int bladeNum,
	vec3_t point, const vec3_t moveDir, float speed, int vrTime, int attackingAt, int swingAt ) {
	vrSaberState_t *vs = VRS_State( vr );
	gclient_t *ocl = other->client;
	const vrSaberWeight_t *w = VRS_Weight( vr );
	const vrSaberWeight_t *ow = VRS_Weight( other );
	const qboolean swing = (qboolean)( speed >= w->minSpeed * VRS_SpeedScale() );
	const qboolean attackingNow = WP_VR_SaberAttacking( other );
	const qboolean attacking = ( attackingAt >= 0 ) ? (qboolean)( attackingAt > 0 ) : attackingNow;
	// (still the same attack, or guard: knock it, bounce it, parry)
	const qboolean react = (qboolean)( attacking == attackingNow &&
		( swingAt < 0 || other->s.number < 0 || other->s.number >= MAX_CLIENTS || swingAt == VRS_SwingId( other->s.number ) ) );
	const int strength = VRS_Strength( w, speed );
	const int theirs = ow->strength + ( attacking ? 1 : 0 );
	const char *result = "touched";
	const char *jarred = "";
	qboolean through = qfalse;
	int recover = 0;

	if ( saberNum < 0 || saberNum >= MAX_SABERS ) {
		return qfalse;
	}
	// a blow of theirs (that one attack) that reached the VR player after
	// this, as they saw it, never did (it waits for this: G_VRSaberDeferHit)
	VRS_CancelPending( other->s.number, vr->s.number, vrTime - VR_UNLAG_SLACK, swingAt );
	if ( react ) {
		ocl->ps.saberAttackWound = Q_max( ocl->ps.saberAttackWound, level.time + VR_SABER_BLOCK_MS );
		ocl->ps.saberIdleWound = Q_max( ocl->ps.saberIdleWound, level.time + VR_SABER_BLOCK_MS );
	}

	if ( vrsMet[other->s.number][saberNum].vrEnt == vr->s.number && level.time - vrsMet[other->s.number][saberNum].time < VR_SABER_MET_MS &&
		( !attacking || vrsMet[other->s.number][saberNum].attacking ) && ( !swing || vrsMet[other->s.number][saberNum].vrSwing ) ) {
		// the same contact: the blades still together (or the other side saw it
		// first), settled already
		vrsMet[other->s.number][saberNum].time = level.time;
		return vrsMet[other->s.number][saberNum].vrGoesOn;
	}
	vrsMet[other->s.number][saberNum].vrEnt = vr->s.number;
	vrsMet[other->s.number][saberNum].time = level.time;
	vrsMet[other->s.number][saberNum].attacking = attacking;
	vrsMet[other->s.number][saberNum].vrSwing = swing;
	vrsMet[other->s.number][saberNum].vrGoesOn = qfalse;
	vs->clashEnt = other->s.number;
	vs->clashTime = vs->clashSeen = level.time;

	// sparks, told as the swinging blade's (the VR player feels it in that hand)
	{
		gentity_t *te = G_TempEntity( point, EV_SABER_BLOCK );
		VectorCopy( point, te->s.origin );
		VectorScale( moveDir, -1.0f, te->s.angles );
		if ( VectorNormalize( te->s.angles ) < 0.001f ) {
			VectorSet( te->s.angles, 0.0f, 0.0f, 1.0f );
		}
		te->s.eventParm = 1;
		te->s.otherEntityNum2 = swing ? vr->s.number : other->s.number;
		te->s.weapon = swing ? s : saberNum;
		te->s.legsAnim = swing ? b : bladeNum;
	}
	WP_SaberBounceSound( swing ? vr : other, swing ? s : saberNum, swing ? b : bladeNum );

	if ( attacking ) {
		// their attack can't go through the VR blade
		if ( swing ? ( strength > theirs ) : ( strength >= theirs + 2 ) ) {
			if ( react ) {
				WP_VR_KnockAttack( other );
			}
			through = (qboolean)( swing && VRS_Breaks( w, speed, strength, theirs ) );
			result = through ? "knocked away, broke through" : "knocked away";
		} else {
			if ( react ) {
				ocl->ps.saberBlocked = BLOCKED_ATK_BOUNCE;
			}
			result = "bounced off";
		}
		if ( swing ) {
			recover = through ? 0 : ( strength < theirs ) ? 2 * w->recover : w->recover;	// a weaker swing reels
			jarred = ( strength < theirs ) ? ", jarring the VR blade" : "";
		} else if ( theirs >= strength + 2 ) {
			recover = w->recover;		// a much stronger attack jars the guard
			jarred = ", jarring the VR blade";
		}
	} else if ( swing ) {
		// a VR swing into their guard: only a full-speed heavy blow breaks it
		if ( VRS_Breaks( w, speed, strength, theirs ) ) {
			if ( react ) {
				WP_VR_BreakParry( other, point );
			}
			through = qtrue;
			result = "broke the guard, through";
		} else {
			if ( react ) {
				WP_SaberBlockNonRandom( other, point, qfalse );
			}
			result = "parried";
		}
		recover = through ? 0 : w->recover;
	}

	if ( through ) {
		vs->throughEnt = other->s.number;
		vs->throughTime = level.time;
	}
	if ( recover && vs->recoverTime < level.time + recover ) {
		vs->recoverTime = level.time + recover;
	}
	if ( swing ) {
		VRS_Attacked( vr );
		if ( vs->swing.on ) {
			vs->swing.clashes++;
		}
	}
	vrsMet[other->s.number][saberNum].vrGoesOn = through;

	if ( g_vrSaberDebug.integer ) {
		Com_Printf( "VR saber: %s's %s blade (%.1f m/s) and %s's%s: %s%s\n", vr->client->pers.netname, w->name,
			speed / VR_UNITS_PER_METRE, ocl->pers.netname, attacking ? " attack" : "", result, jarred );
	}
	if ( g_vrSaberLog.integer ) {
		if ( swing ) {
			G_LogPrintf( "VRSaber: clash %i \"%s\" %s%s %.1f m/s vs \"%s\" %s: %s%s\n", vr->s.number, vr->client->pers.netname,
				w->name, VRS_TwoHanded( vr ) ? " two-handed" : "", speed / VR_UNITS_PER_METRE, ocl->pers.netname,
				attacking ? "attacking" : "guarding", result, jarred );
		} else if ( attacking ) {
			G_LogPrintf( "VRSaber: block %i \"%s\" %s%s %.1f m/s stopped \"%s\": %s%s\n", vr->s.number, vr->client->pers.netname,
				w->name, VRS_TwoHanded( vr ) ? " two-handed" : "", speed / VR_UNITS_PER_METRE, ocl->pers.netname, result, jarred );
		} else {
			G_LogPrintf( "VRSaber: touch %i \"%s\" %.1f m/s, \"%s\"'s blade\n", vr->s.number, vr->client->pers.netname,
				speed / VR_UNITS_PER_METRE, ocl->pers.netname );
		}
	}
	return through;
}

// A thrown saber reaching a VR player: blocked only if one of their blades
// is in its way (stock decides with WP_SaberCanBlock). Gives the point on
// the blade and which saber.
#define VR_SABER_THROWN_REACH	32.0f	// a spinning saber hits a blade this close to its hilt

qboolean G_VRSaberStopsThrown( gentity_t *ent, const vec3_t thrown, vec3_t point, int *saberNum ) {
	float best = VR_SABER_THROWN_REACH;
	qboolean found = qfalse;
	int s, b;

	if ( !ent || !ent->client ) {
		return qfalse;
	}
	for ( s = 0; s < MAX_SABERS; s++ ) {
		for ( b = 0; b < MAX_BLADES; b++ ) {
			bladeInfo_t *blade;
			vec3_t tip, c1, c2;
			float d;
			if ( !VRS_BladeOn( ent->client, s, b ) ) {
				continue;
			}
			blade = &ent->client->saber[s].blade[b];
			if ( level.time - blade->storageTime > 200 ) {
				continue;
			}
			VectorMA( blade->muzzlePoint, blade->lengthMax, blade->muzzleDir, tip );
			VRS_ClipBlade( ent->s.number, blade->muzzlePoint, tip, tip );
			d = VRS_SegSegClosest( blade->muzzlePoint, tip, thrown, thrown, c1, c2 );
			if ( d <= best ) {
				best = d;
				VectorCopy( c1, point );
				*saberNum = s;
				found = qtrue;
			}
		}
	}
	return found;
}

/*
--------------------------------------------------------------------------------
Blade against bodies
--------------------------------------------------------------------------------
*/

// Whether any of this player's blades cut that victim just now.
static qboolean VRS_RecentlyCut( const vrSaberState_t *vs, int victim ) {
	int i;

	for ( i = 0; i < VR_SABER_CUTS; i++ ) {
		if ( vs->cuts[i].time && vs->cuts[i].ent == victim && level.time - vs->cuts[i].time < VR_SABER_CUT_REPEAT ) {
			return qtrue;
		}
	}
	return qfalse;
}

static void VRS_NoteCut( vrSaberState_t *vs, int victim ) {
	int i, slot = 0;

	for ( i = 0; i < VR_SABER_CUTS; i++ ) {
		if ( vs->cuts[i].time && vs->cuts[i].ent == victim ) {
			slot = i;	// (that victim's entry)
			break;
		}
		if ( vs->cuts[i].time < vs->cuts[slot].time ) {
			slot = i;	// (else the oldest, or an unused one)
		}
	}
	vs->cuts[slot].ent = victim;
	vs->cuts[slot].time = level.time;
}

// The weight of a blow on whoever it cut: a medium blade nudges them, a heavy
// one shoves them back, and a heavy blow near full speed (two hands: well
// short of it) knocks them off their feet. (frac: how near full speed it was, 0..1)
static void VRS_Impact( gentity_t *self, const vrSaberWeight_t *w, gentity_t *victim, const vec3_t moveDir, float frac,
	float dmg ) {
	const int weight = (int)( w - vrSaberWeights );
	vec3_t dir, away;
	float push;

	if ( !victim->client || victim->health <= 0 || ( OnSameTeam( self, victim ) && !g_friendlySaber.integer ) ) {
		return;
	}
	if ( victim->client->ps.duelInProgress && victim->client->ps.duelIndex != self->s.number ) {
		return;
	}
	switch ( weight ) {
	case VRS_LIGHT:		push = 0.0f;					break;
	case VRS_MEDIUM:	push = 15.0f * frac;			break;
	default:			push = 25.0f + 35.0f * frac;	break;	// heavy
	}
	if ( push <= 0.0f ) {
		return;
	}
	// along the swing, and away from the attacker
	VectorSubtract( victim->r.currentOrigin, self->r.currentOrigin, away );
	away[2] = 0.0f;
	VectorNormalize( away );
	VectorSet( dir, moveDir[0], moveDir[1], 0.0f );
	VectorNormalize( dir );
	VectorMA( dir, 0.7f, away, dir );
	if ( VectorNormalize( dir ) < 0.1f ) {
		VectorCopy( away, dir );
	}
	if ( G_VRSaberClient( victim ) ) {
		push *= 0.5f;	// (gentler on someone in a headset)
	}
	G_Throw( victim, dir, push );
	if ( weight >= VRS_HEAVY && frac >= ( weight == VRS_HEAVY2 ? 0.6f : 0.8f ) && victim->health > dmg &&
		!G_VRSaberClient( victim ) ) {
		G_Knockdown( victim );
	}
}

static void VRS_Hit( gentity_t *self, int s, int b, gentity_t *victim, vec3_t point, const vec3_t base, const vec3_t dir,
	float length, float t, const vec3_t moveDir ) {
	vrSaberState_t *vs = VRS_State( self );
	const vrSaberWeight_t *w = VRS_Weight( self );
	const float minSpeed = w->minSpeed * VRS_SpeedScale();
	const float fullSpeed = w->fullSpeed * VRS_SpeedScale();
	saberInfo_t *saber = &self->client->saber[s];
	const qboolean style2 = WP_SaberBladeUseSecondBladeStyle( saber, b );
	vec3_t rel;
	float along, speed, dmg, frac = 0.0f;
	qboolean doDismember;
	int knockback = 0;

	if ( level.time < vs->recoverTime ) {
		return;	// bouncing back from a clash
	}
	if ( PM_SaberInBrokenParry( self->client->ps.saberMove ) ) {
		return;	// staggered
	}
	if ( victim->client ) {
		if ( victim->client->ps.duelInProgress && victim->client->ps.duelIndex != self->s.number ) {
			return;
		}
		if ( self->client->ps.duelInProgress && self->client->ps.duelIndex != victim->s.number ) {
			return;
		}
	}

	// how fast the blade moves where it cuts (an arm swing, not a wrist flick)
	VectorSubtract( point, base, rel );
	along = Com_Clamp( 0.0f, length, DotProduct( rel, dir ) );
	speed = VRS_SwingSpeed( vs, s, b, t, along );

	if ( speed < minSpeed ) {
		// a blade just resting on someone burns a little now and then
		// (teammates only with g_friendlySaber, as for idle blades in stock)
		if ( victim->client && OnSameTeam( self, victim ) && !g_friendlySaber.integer ) {
			return;
		}
		if ( level.time < self->client->ps.saberIdleWound ) {
			return;
		}
		self->client->ps.saberIdleWound = level.time + g_saberDmgDelay_Idle.integer;
		dmg = VR_SABER_BURN;
	} else {
		if ( VRS_RecentlyCut( vs, victim->s.number ) ) {
			return;	// still the same cut, or the other blade just cut them
		}
		frac = Com_Clamp( 0.0f, 1.0f, ( speed - minSpeed ) / ( fullSpeed - minSpeed ) );
		dmg = w->maxDamage * ( 0.25f + 0.75f * frac );
		dmg *= g_vrSaberDamageScale.value * g_saberDamageScale.value;
		dmg *= style2 ? saber->damageScale2 : saber->damageScale;
		if ( self->client->ps.brokenLimbs & ( ( 1 << BROKENLIMB_RARM ) | ( 1 << BROKENLIMB_LARM ) ) ) {
			dmg *= 0.3f;	// an arm is broken
		}
		if ( self->client->ps.isJediMaster ) {
			dmg *= 2.0f;
		}
		if ( level.gametype == GT_SIEGE && self->client->siegeClass != -1 &&
			( bgSiegeClasses[self->client->siegeClass].classflags & ( 1 << CFL_MORESABERDMG ) ) ) {
			dmg *= 2.0f;
		}
		if ( vs->throughTime && level.time - vs->throughTime < VR_SABER_THROUGH_MS ) {
			dmg *= 0.5f;	// the blow broke through a guard first
		}
		if ( !( dmg >= VR_SABER_BURN + 1 ) ) {
			dmg = VR_SABER_BURN + 1;
		}

		// servers with the old MP-style saber damage let jedi block automatically
		if ( !d_saberSPStyleDamage.integer && victim->client && !G_VRSaberClient( victim ) &&
			WP_SaberCanBlock( victim, point, 0, MOD_SABER, qfalse, VRS_Strength( w, speed ) ) ) {
			vrsOtherBlade_t ob;
			memset( &ob, 0, sizeof( ob ) );
			ob.ent = victim;
			ob.saber = ob.blade = VR_POSE_MAX_SABERS;	// (no blade of theirs to measure)
			VRS_Clash( self, s, b, &ob, point, moveDir, speed );
			return;
		}

		VRS_NoteCut( vs, victim->s.number );
		VRS_Attacked( self );

		if ( ( style2 ? saber->knockbackScale2 : saber->knockbackScale ) > 0.0f ) {
			if ( s == 0 ) {
				knockback = style2 ? DAMAGE_SABER_KNOCKBACK1_B2 : DAMAGE_SABER_KNOCKBACK1;
			} else {
				knockback = style2 ? DAMAGE_SABER_KNOCKBACK2_B2 : DAMAGE_SABER_KNOCKBACK2;
			}
		}
	}

	doDismember = (qboolean)( dmg > VR_SABER_BURN &&
		!( saber->saberFlags2 & ( style2 ? SFL2_NO_DISMEMBERMENT2 : SFL2_NO_DISMEMBERMENT ) ) );
	WP_SaberDamageAdd( victim->s.number, (float *)moveDir, point, (int)( dmg + 0.5f ), doDismember, knockback );
	if ( victim->s.number >= 0 && victim->s.number < MAX_CLIENTS ) {
		vs->hitTime[victim->s.number] = level.time;	// (when, as the player saw it: VRS_KilledFirst)
		vs->hitVrTime[victim->s.number] = (int)t;
	}

	if ( victim->client ) {
		self->client->ps.saberEventFlags |= ( self->enemy == victim ) ? SEF_HITENEMY : SEF_HITOBJECT;
	}
	if ( dmg > VR_SABER_BURN ) {
		VRS_Impact( self, w, victim, moveDir, frac, dmg );
		if ( vs->swing.on ) {
			vs->swing.cuts++;
		}
	}
	if ( g_vrSaberLog.integer && dmg > VR_SABER_BURN ) {
		G_LogPrintf( "VRSaber: cut %i \"%s\" -> \"%s\" %s%s %.1f m/s (cuts from %.1f, full at %.1f): %i damage\n",
			self->s.number, self->client->pers.netname,
			victim->client ? victim->client->pers.netname : ( victim->classname ? victim->classname : "?" ), w->name,
			VRS_TwoHanded( self ) ? " two-handed" : "", speed / VR_UNITS_PER_METRE,
			minSpeed / VR_UNITS_PER_METRE, fullSpeed / VR_UNITS_PER_METRE, (int)( dmg + 0.5f ) );
	}
	if ( g_vrSaberDebug.integer && dmg > VR_SABER_BURN ) {
		Com_Printf( "VR saber: %s cut %s at %.1f m/s with a %s%s blade: %d damage\n", self->client->pers.netname,
			victim->client ? victim->client->pers.netname : ( victim->classname ? victim->classname : "?" ), speed / VR_UNITS_PER_METRE,
			w->name, VRS_TwoHanded( self ) ? " (two hands)" : "", (int)( dmg + 0.5f ) );
	}
}

/*
--------------------------------------------------------------------------------
Lag compensation (g_vrSaberUnlag 1): a VR player's blade is judged against
the others as that player saw them. Their game draws everyone where the
server had them a moment before (the snapshots on the way, drawn a little
behind the newest) and its moves reach the server a moment later still:
20-60 ms on a LAN, more online - and a swinging saber moves a long way in
that time. So:
  - every server frame, where each player was and where their animated
    blades were is kept (the frame's snapshot, as everyone sees it);
  - a VR blade's sweep, pose by pose, meets the others' animated blades
    where they were at that pose's command time (the time of the world the
    player was looking at), and cuts their bodies where they were then;
  - an animated blade that cuts a VR player waits until that player's moves
    for the moment of the cut have come in: if their blade met that attack
    by then (their block, in their eyes), or cut its owner down before it,
    the cut never happened; else it lands.
VR blades against each other are judged as they are now.
--------------------------------------------------------------------------------
*/

#define VRS_HIST			48		// server frames kept per player (VR_UNLAG_MAX, at sv_fps up to 190)
#define VRS_HIST_BLADES		4		// animated blades per saber followed back
#define VRS_HIST_JUMP		128.0f	// a player (or their blade) this much further from one frame to the next: teleported

typedef struct {
	int			time;				// level.time of the frame (where the body is)
	int			bladeTime;			// when everyone saw the blades kept with it (VRS_BladeShownLag)
	qboolean	solid;				// alive and in the world (their body can be cut)
	qboolean	jumped;				// teleported (or respawned) since the frame before: nothing in between
	int			teleport;			// (their EF_TELEPORT_BIT)
	vec3_t		origin;				// where everyone's snapshot had them
	float		yaw;
	int			attacking;			// as the blades were placed: mid attack...
	int			swing;				// ...which one (VRS_SwingId)...
	qboolean	open;				// ...or in a broken parry (their blades stop nothing)
	int			bladeMask[MAX_SABERS];	// animated blades lit then...
	vec3_t		base[MAX_SABERS][VRS_HIST_BLADES];	// ...from here...
	vec3_t		tip[MAX_SABERS][VRS_HIST_BLADES];	// ...to here (round where the body was drawn)
} vrsHistSample_t;

static struct {
	vrsHistSample_t	s[VRS_HIST];	// ring, newest at [head]
	int				head, count;
	// as WP_SaberPositionUpdate last placed their animated blades (G_VRSaberBladeOrigin):
	int				placedTime;		// level.time
	vec3_t			placedOrigin;	// the body it placed them round (a little ahead of it, as they move)
	int				swing;			// the attack under way then, or the last one (VRS_SwingId)...
	qboolean		attacking;
	int				move;			// (the saber move)
	qboolean		open;			// (a broken parry)
	int				bladeSwing;		// ...and the one the blades placed then show (the animation is the
	qboolean		bladeAttacking;	// placement's before: G_UpdateClientAnims comes after them)
	qboolean		bladeOpen;
} vrsHist[MAX_CLIENTS];

#define VRS_MAX_PENDING		( MAX_CLIENTS * 8 )

static struct {
	qboolean	inuse;
	int			attacker, victim;
	int			time;				// level.time of the cut...
	int			seen;				// ...when everyone saw it (a bot's: a frame before, VRS_BladeShownLag)...
	int			swing;				// ...by which attack of theirs (VRS_SwingId)
	vec3_t		dir, spot;
	int			dmg, dflags;
	int			saberNum, bladeNum;
	qboolean	surfaceSet;			// the part of the body it hit (where it hit, dismemberment)
	int			surface;
} vrsPending[VRS_MAX_PENDING];

// The other players near a VR player, moved back to where that player saw
// them for the cuts of their sweep (VRS_ShiftBodies), and where they really
// are meanwhile.
static struct {
	int			num;
	qboolean	unlinked;			// (taken out of the world: not there to be cut then)
	vec3_t		currentOrigin, psOrigin;
	float		yaw;
} vrsShifted[MAX_CLIENTS];
static int		vrsNumShifted;
static float	vrsShiftTime;

static qboolean VRS_Unlag( void ) {
	return (qboolean)( g_vrSaberUnlag.integer > 0 );
}

static void VRS_ResetUnlag( int clientNum ) {
	int i;

	if ( clientNum < 0 ) {
		memset( vrsHist, 0, sizeof( vrsHist ) );
		memset( vrsPending, 0, sizeof( vrsPending ) );
		return;
	}
	if ( clientNum >= MAX_CLIENTS ) {
		return;
	}
	memset( &vrsHist[clientNum], 0, sizeof( vrsHist[clientNum] ) );
	for ( i = 0; i < VRS_MAX_PENDING; i++ ) {
		if ( vrsPending[i].inuse && ( vrsPending[i].attacker == clientNum || vrsPending[i].victim == clientNum ) ) {
			vrsPending[i].inuse = qfalse;
		}
	}
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		vrSabers[i].hitTime[clientNum] = 0;
	}
}

static vrsHistSample_t *VRS_HistSample( int c, int i ) {
	return &vrsHist[c].s[( vrsHist[c].head - i + VRS_HIST ) % VRS_HIST];
}

// Where clients draw this player at this frame's time (their snapshot).
static void VRS_DrawnOrigin( gentity_t *ent, vec3_t out ) {
	if ( ent->s.pos.trType == TR_LINEAR_STOP ) {
		BG_EvaluateTrajectory( &ent->s.pos, level.time, out );	// (g_smoothClients: carried on to now)
	} else {
		VectorCopy( ent->r.currentOrigin, out );
	}
}

// Where client c really is now (wherever a sweep has moved their body to),
// and facing.
static void VRS_LiveOrigin( int c, vec3_t origin, float *yaw ) {
	gentity_t *ent = &g_entities[c];
	int i;

	for ( i = 0; i < vrsNumShifted; i++ ) {
		if ( vrsShifted[i].num == c && !vrsShifted[i].unlinked ) {
			VectorCopy( vrsShifted[i].currentOrigin, origin );
			*yaw = vrsShifted[i].yaw;
			return;
		}
	}
	VectorCopy( ent->r.currentOrigin, origin );
	*yaw = ent->client ? ent->client->ps.viewangles[YAW] : 0.0f;
}

// Which attack client c is in now, or made last: each one that starts (or
// follows on from another) gets a new number. (As G_VRSaberBladeOrigin
// numbers them when the blades are placed.)
static int VRS_SwingId( int c ) {
	gentity_t *ent = &g_entities[c];

	if ( ent->client && WP_VR_SaberAttacking( ent ) &&
		( !vrsHist[c].attacking || vrsHist[c].move != ent->client->ps.saberMove ) ) {
		return vrsHist[c].swing + 1;
	}
	return vrsHist[c].swing;
}

// WP_SaberPositionUpdate is placing this player's animated blades, with their
// body here (a little ahead of where it is, when they move: the stock saber's
// guess at what clients see). What's kept of them goes round where everyone
// drew the body instead, tagged with the attack their animation shows: the
// one under way at the placement before this (G_UpdateClientAnims gives the
// server's model each new state after its blades are placed).
void G_VRSaberBladeOrigin( gentity_t *ent, const vec3_t origin ) {
	const int c = ent ? ent->s.number : -1;
	gclient_t *cl;

	if ( c < 0 || c >= MAX_CLIENTS || !ent->client ) {
		return;
	}
	cl = ent->client;
	vrsHist[c].bladeSwing = vrsHist[c].swing;
	vrsHist[c].bladeAttacking = vrsHist[c].attacking;
	vrsHist[c].bladeOpen = vrsHist[c].open;
	vrsHist[c].swing = VRS_SwingId( c );		// (and the attack under way now)
	vrsHist[c].attacking = WP_VR_SaberAttacking( ent );
	vrsHist[c].move = cl->ps.saberMove;
	vrsHist[c].open = PM_SaberInBrokenParry( cl->ps.saberMove );
	VectorCopy( origin, vrsHist[c].placedOrigin );
	vrsHist[c].placedTime = level.time;
}

// How long before its frame everyone saw what a player's animated blades
// show: a bot's (or NPC's) moves come after its blades are placed each frame
// (G_RunClient, NPC_Think), so its new animations reach the server's model a
// frame after the snapshot that started them on everyone's screens (a
// player's moves come in before the frame: theirs line up).
static int VRS_BladeShownLag( gentity_t *ent ) {
	if ( ( ent->r.svFlags & SVF_BOT ) || ent->s.eType == ET_NPC ) {
		return Com_Clampi( 0, 100, level.time - level.previousTime );
	}
	return 0;
}

// How far a player's animated blade moves to go round where their body was
// drawn when it was seen (body).
static void VRS_BladeOffset( gentity_t *ent, const bladeInfo_t *blade, const vec3_t body, vec3_t ofs ) {
	const int c = ent->s.number;

	VectorClear( ofs );
	if ( c >= 0 && c < MAX_CLIENTS && vrsHist[c].placedTime == blade->storageTime ) {
		VectorSubtract( body, vrsHist[c].placedOrigin, ofs );
		if ( VectorLengthSquared( ofs ) > Square( 100.0f ) ) {
			VectorClear( ofs );	// (nonsense: as it is)
		}
	}
}

// Is anyone fighting with a saber in their VR hands? (Nothing to keep else.)
static qboolean VRS_AnyVR( void ) {
	int c;

	for ( c = 0; c < level.maxclients && c < MAX_CLIENTS; c++ ) {
		if ( G_VRSaberClient( &g_entities[c] ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

// Once a frame, after everyone moved: where each player is and where their
// animated blades are.
static void VRS_RecordHistory( void ) {
	const qboolean keep = (qboolean)( VRS_Unlag() && G_VRSaberOn() && VRS_AnyVR() );
	vec3_t tip, ofs;
	int c, s, b;

	for ( c = 0; c < level.maxclients && c < MAX_CLIENTS; c++ ) {
		gentity_t *ent = &g_entities[c];
		gclient_t *cl = ent->client;
		vrsHistSample_t *smp, *prev;
		const float *body;
		int lagShown;

		if ( !keep || !ent->inuse || !cl || cl->pers.connected != CON_CONNECTED ) {
			vrsHist[c].count = 0;
			continue;
		}
		prev = vrsHist[c].count ? VRS_HistSample( c, 0 ) : NULL;
		if ( prev && prev->time >= level.time ) {
			vrsHist[c].count = 0;	// (the clock went back: start over)
			prev = NULL;
		}
		vrsHist[c].head = ( vrsHist[c].head + 1 ) % VRS_HIST;
		if ( vrsHist[c].count < VRS_HIST ) {
			vrsHist[c].count++;
		}
		smp = VRS_HistSample( c, 0 );
		memset( smp, 0, sizeof( *smp ) );
		smp->time = level.time;
		VRS_DrawnOrigin( ent, smp->origin );
		smp->yaw = cl->ps.viewangles[YAW];
		smp->teleport = cl->ps.eFlags & EF_TELEPORT_BIT;
		smp->solid = (qboolean)( ent->r.linked && ent->health > 0 && !( cl->ps.eFlags & EF_DEAD ) &&
			cl->sess.sessionTeam != TEAM_SPECTATOR && !( cl->ps.pm_flags & PMF_FOLLOW ) );
		smp->jumped = (qboolean)( prev && ( prev->teleport != smp->teleport ||
			Distance( prev->origin, smp->origin ) > VRS_HIST_JUMP ) );	// (teleported or respawned)
		if ( vrsHist[c].placedTime == level.time ) {
			smp->attacking = vrsHist[c].bladeAttacking ? 1 : 0;	// (as the blades placed now show)
			smp->swing = vrsHist[c].bladeSwing;
			smp->open = vrsHist[c].bladeOpen;
		} else {
			smp->attacking = WP_VR_SaberAttacking( ent ) ? 1 : 0;
			smp->swing = VRS_SwingId( c );
			smp->open = PM_SaberInBrokenParry( cl->ps.saberMove );
		}
		// the blades placed this frame were seen a moment before it (a bot's),
		// round where the body was then
		lagShown = VRS_BladeShownLag( ent );
		smp->bladeTime = ( vrsHist[c].placedTime ? vrsHist[c].placedTime : level.time ) - lagShown;
		body = ( lagShown && prev ) ? prev->origin : smp->origin;
		if ( G_VRSaberClient( ent ) || !smp->solid ) {
			continue;	// (VR blades: their own poses; and nobody blocks with a blade when dead)
		}
		for ( s = 0; s < MAX_SABERS; s++ ) {
			for ( b = 0; b < VRS_HIST_BLADES; b++ ) {
				bladeInfo_t *blade;
				if ( !VRS_BladeOn( cl, s, b ) ) {
					continue;
				}
				blade = &cl->saber[s].blade[b];
				if ( level.time - blade->storageTime > 100 ) {
					continue;	// not kept up to date (not drawn)
				}
				VRS_BladeOffset( ent, blade, body, ofs );
				VectorAdd( blade->muzzlePoint, ofs, smp->base[s][b] );
				VectorMA( smp->base[s][b], blade->lengthMax, blade->muzzleDir, tip );
				VRS_ClipBlade( c, smp->base[s][b], tip, smp->tip[s][b] );	// (as far as it reaches before a wall)
				smp->bladeMask[s] |= 1 << b;
			}
		}
	}
}

// How far back a VR player's view of the others is taken for a command
// time t (no further than VR_UNLAG_MAX, nor beyond now).
static float VRS_UnlagTime( float t ) {
	return Com_Clamp( level.time - VR_UNLAG_MAX, level.time, t );
}

// The frames kept of client c around time t (blades: their bladeTime, else
// the body's): the newest at or before it (*older), and the one after that
// (*newer). Either can be NULL: t before all that's kept, or after it.
static void VRS_HistBracket( int c, float t, qboolean blades, const vrsHistSample_t **older, const vrsHistSample_t **newer ) {
	int i;

	*older = *newer = NULL;
	for ( i = 0; i < vrsHist[c].count; i++ ) {
		const vrsHistSample_t *smp = VRS_HistSample( c, i );
		if ( ( blades ? smp->bladeTime : smp->time ) <= t ) {
			*older = smp;
			return;
		}
		*newer = smp;
	}
}

// An animated blade of client c where it was at time t, as everyone saw it:
// between the frames kept around it, or past the last of them toward where
// it is now (nowBase-nowTip at nowTime, as gathered for this sweep; nowTime
// 0: not lit now). Lit or not, attacking or not, is as of the frame before t
// (a client shows a frame's state until the next). Returns 1 - with whether
// its owner was attacking then, and which attack (VRS_SwingId) - 0 if nothing
// is kept of that player, -1 if the blade wasn't there then (not lit, or in a
// broken parry: it stopped nothing).
static int VRS_HistBladeAt( int c, int s, int b, float t, const vec3_t nowBase, const vec3_t nowTip, int nowTime,
	vec3_t base, vec3_t tip, int *attacking, int *swing ) {
	const vrsHistSample_t *older, *newer, *state;
	const int bit = 1 << b;
	float f;

	if ( !VRS_Unlag() || c < 0 || c >= MAX_CLIENTS || s < 0 || s >= MAX_SABERS || b < 0 || b >= VRS_HIST_BLADES ||
		!vrsHist[c].count ) {
		return 0;
	}
	t = VRS_UnlagTime( t );
	VRS_HistBracket( c, t, qtrue, &older, &newer );
	state = older ? older : newer;	// (before all that's kept: as it was first)
	if ( !( state->bladeMask[s] & bit ) || state->open ) {
		return -1;
	}
	*attacking = state->attacking;
	*swing = state->swing;
	if ( !older ) {
		VectorCopy( newer->base[s][b], base );
		VectorCopy( newer->tip[s][b], tip );
		return 1;
	}
	if ( newer ) {
		if ( ( newer->bladeMask[s] & bit ) && newer->bladeTime > older->bladeTime &&
			Distance( older->base[s][b], newer->base[s][b] ) <= VRS_HIST_JUMP ) {
			f = Com_Clamp( 0.0f, 1.0f, ( t - older->bladeTime ) / (float)( newer->bladeTime - older->bladeTime ) );
			VRS_Lerp( older->base[s][b], f, newer->base[s][b], base );
			VRS_Lerp( older->tip[s][b], f, newer->tip[s][b], tip );
		} else {
			VectorCopy( older->base[s][b], base );	// (until it went out, or its owner teleported)
			VectorCopy( older->tip[s][b], tip );
		}
		return 1;
	}
	// past the last frame kept: toward where it is now
	if ( nowTime > older->bladeTime && Distance( older->base[s][b], nowBase ) <= VRS_HIST_JUMP ) {
		f = Com_Clamp( 0.0f, 1.0f, ( t - older->bladeTime ) / (float)( nowTime - older->bladeTime ) );
		VRS_Lerp( older->base[s][b], f, nowBase, base );
		VRS_Lerp( older->tip[s][b], f, nowTip, tip );
	} else {
		VectorCopy( older->base[s][b], base );
		VectorCopy( older->tip[s][b], tip );
	}
	return 1;
}

// Was this animated blade of client c lit at any frame kept from 'since' on?
// (The VR player may have seen it then, though it's out or thrown now.)
static qboolean VRS_HistLit( int c, int s, int b, int since ) {
	int i;

	for ( i = 0; i < vrsHist[c].count; i++ ) {
		const vrsHistSample_t *smp = VRS_HistSample( c, i );
		if ( smp->bladeMask[s] & ( 1 << b ) ) {
			return qtrue;
		}
		if ( smp->bladeTime <= since ) {
			break;
		}
	}
	return qfalse;
}

// How fast client c's animated blades moved around times t0..t1 (units/ms):
// sweeps take small enough steps to follow them.
static float VRS_HistBladeSpeed( int c, int s, int b, float t0, float t1 ) {
	float best = 0.0f;
	int i;

	for ( i = 0; i + 1 < vrsHist[c].count; i++ ) {
		const vrsHistSample_t *newer = VRS_HistSample( c, i ), *older = VRS_HistSample( c, i + 1 );
		float d;
		if ( newer->bladeTime < t0 - 50 ) {
			break;
		}
		if ( older->bladeTime > t1 + 50 || newer->bladeTime <= older->bladeTime ||
			!( newer->bladeMask[s] & older->bladeMask[s] & ( 1 << b ) ) ) {
			continue;
		}
		d = Q_max( Distance( newer->base[s][b], older->base[s][b] ), Distance( newer->tip[s][b], older->tip[s][b] ) );
		if ( d <= VRS_HIST_JUMP ) {
			best = Q_max( best, d / ( newer->bladeTime - older->bladeTime ) );
		}
	}
	return best;
}

// Where client c's body was at time t, as everyone's snapshots had it (past
// the last frame kept: toward where it is now). 1: there; 0: nothing kept;
// -1: it wasn't there to be cut then (dead, spectating, not in yet).
static int VRS_HistBodyAt( int c, float t, vec3_t origin, float *yaw ) {
	gentity_t *ent = &g_entities[c];
	const vrsHistSample_t *older, *newer;
	vec3_t now;
	float f, span, nowYaw;

	if ( !vrsHist[c].count || !ent->client ) {
		return 0;
	}
	t = VRS_UnlagTime( t );
	VRS_HistBracket( c, t, qfalse, &older, &newer );
	if ( !older ) {
		// before all that's kept: where it was first
		if ( !newer->solid ) {
			return -1;
		}
		VectorCopy( newer->origin, origin );
		*yaw = newer->yaw;
		return 1;
	}
	if ( !older->solid ) {
		return -1;
	}
	if ( newer ) {
		if ( newer->solid && !newer->jumped && newer->time > older->time ) {
			f = Com_Clamp( 0.0f, 1.0f, ( t - older->time ) / (float)( newer->time - older->time ) );
			VRS_Lerp( older->origin, f, newer->origin, origin );
			*yaw = LerpAngle( older->yaw, newer->yaw, f );
		} else {
			VectorCopy( older->origin, origin );	// (until it died, or teleported)
			*yaw = older->yaw;
		}
		return 1;
	}
	// after the last frame kept: toward where it is now (mid-frame, where the
	// snapshot to come will have it) - unless it teleported since: it was
	// still where it was, as anyone saw it
	VRS_LiveOrigin( c, now, &nowYaw );
	if ( ( ent->client->ps.eFlags & EF_TELEPORT_BIT ) != older->teleport || Distance( now, older->origin ) > VRS_HIST_JUMP ) {
		VectorCopy( older->origin, origin );
		*yaw = older->yaw;
		return 1;
	}
	span = level.time - older->time;
	f = ( span > 0.0f ) ? Com_Clamp( 0.0f, 1.0f, ( t - older->time ) / span ) : 1.0f;
	VRS_Lerp( older->origin, f, now, origin );
	*yaw = LerpAngle( older->yaw, nowYaw, f );
	return 1;
}

// The other players near the blade a VR player swept this frame (mins-maxs)
// moved back to where that player saw them at time t - out of the way if
// they weren't there to be cut then - and back again.
static void VRS_UnshiftBodies( void ) {
	int i;

	for ( i = 0; i < vrsNumShifted; i++ ) {
		gentity_t *ent = &g_entities[vrsShifted[i].num];
		if ( !vrsShifted[i].unlinked ) {
			VectorCopy( vrsShifted[i].currentOrigin, ent->r.currentOrigin );
			VectorCopy( vrsShifted[i].psOrigin, ent->client->ps.origin );
			ent->client->ps.viewangles[YAW] = vrsShifted[i].yaw;
		}
		trap->LinkEntity( (sharedEntity_t *)ent );
	}
	vrsNumShifted = 0;
}

#define VRS_SHIFT_NEAR		( 48.0f + 100.0f )	// a body this far out of a blade's sweep can't be in its way (its
												// size, and how far it moves in VR_UNLAG_MAX)

static void VRS_ShiftBodies( gentity_t *self, float t, const vec3_t mins, const vec3_t maxs ) {
	int c, k;

	if ( !VRS_Unlag() ) {
		return;
	}
	t = VRS_UnlagTime( t );
	if ( vrsNumShifted && fabsf( t - vrsShiftTime ) < 4.0f ) {
		return;	// (there already)
	}
	VRS_UnshiftBodies();
	vrsShiftTime = t;
	for ( c = 0; c < level.maxclients && c < MAX_CLIENTS; c++ ) {
		gentity_t *ent = &g_entities[c];
		vec3_t origin;
		float yaw;
		int found;

		if ( ent == self || !ent->inuse || !ent->client || !ent->r.linked || ent->health < 1 ) {
			continue;
		}
		for ( k = 0; k < 3; k++ ) {
			if ( ent->r.currentOrigin[k] < mins[k] - VRS_SHIFT_NEAR || ent->r.currentOrigin[k] > maxs[k] + VRS_SHIFT_NEAR ) {
				break;
			}
		}
		if ( k < 3 ) {
			continue;	// (nowhere near the blade)
		}
		found = VRS_HistBodyAt( c, t, origin, &yaw );
		if ( !found ) {
			continue;
		}
		if ( found > 0 && DistanceSquared( origin, ent->r.currentOrigin ) < 0.25f &&
			fabsf( AngleSubtract( yaw, ent->client->ps.viewangles[YAW] ) ) < 1.0f ) {
			continue;	// (no different)
		}
		vrsShifted[vrsNumShifted].num = c;
		vrsShifted[vrsNumShifted].unlinked = (qboolean)( found < 0 );
		VectorCopy( ent->r.currentOrigin, vrsShifted[vrsNumShifted].currentOrigin );
		VectorCopy( ent->client->ps.origin, vrsShifted[vrsNumShifted].psOrigin );
		vrsShifted[vrsNumShifted].yaw = ent->client->ps.viewangles[YAW];
		vrsNumShifted++;
		if ( found < 0 ) {
			trap->UnlinkEntity( (sharedEntity_t *)ent );
			continue;
		}
		VectorCopy( origin, ent->r.currentOrigin );
		VectorCopy( origin, ent->client->ps.origin );
		ent->client->ps.viewangles[YAW] = yaw;
		trap->LinkEntity( (sharedEntity_t *)ent );
	}
}

static void VRS_LandPending( int i );

// Does an animated blade's cut on this VR player wait for their moves?
// (w_saber.c asks once per victim before its hit effect, and keeps the
// answer for its damage.)
qboolean G_VRSaberDefersHit( gentity_t *attacker, gentity_t *victim, int saberNum ) {
	if ( !VRS_Unlag() || !G_VRSaberOn() || !attacker || !victim || attacker == victim || !attacker->client || !victim->client ) {
		return qfalse;
	}
	if ( attacker->s.number < 0 || attacker->s.number >= MAX_CLIENTS || victim->s.number < 0 || victim->s.number >= MAX_CLIENTS ) {
		return qfalse;	// (players and bots: their blades are kept; NPCs' meet the VR blades as they are)
	}
	if ( G_VRSaberClient( attacker ) ) {
		return qfalse;	// (a VR player's blade - even one whose poses lag - isn't kept)
	}
	if ( saberNum == 0 && attacker->client->ps.saberInFlight ) {
		return qfalse;	// (a thrown saber: blocked as it comes, G_VRSaberStopsThrown)
	}
	return G_VRSaberActive( victim );
}

// An animated blade cut a VR player (G_VRSaberDefersHit): the cut lands once
// their moves for this moment have come in, unless their blade met it first
// (VRS_MeetAnimated cancels it).
void G_VRSaberDeferHit( gentity_t *attacker, gentity_t *victim, const vec3_t dir, const vec3_t spot, int dmg, int dflags,
	int saberNum, int bladeNum ) {
	int i, slot = -1, oldest = -1;

	if ( !attacker || !victim || !attacker->client || dmg <= 0 ) {
		return;
	}
	for ( i = 0; i < VRS_MAX_PENDING; i++ ) {
		if ( !vrsPending[i].inuse ) {
			if ( slot < 0 ) {
				slot = i;
			}
			continue;
		}
		if ( vrsPending[i].attacker == attacker->s.number && vrsPending[i].victim == victim->s.number &&
			vrsPending[i].time == level.time ) {
			vrsPending[i].dmg += dmg;		// (another blade of theirs, the same frame)
			vrsPending[i].dflags |= dflags;
			return;
		}
		if ( oldest < 0 || vrsPending[i].time < vrsPending[oldest].time ) {
			oldest = i;
		}
	}
	if ( slot < 0 ) {
		slot = oldest;	// (full: the oldest lands at once)
		VRS_LandPending( slot );
	}
	memset( &vrsPending[slot], 0, sizeof( vrsPending[slot] ) );
	vrsPending[slot].inuse = qtrue;
	vrsPending[slot].attacker = attacker->s.number;
	vrsPending[slot].victim = victim->s.number;
	vrsPending[slot].time = level.time;
	vrsPending[slot].seen = level.time - VRS_BladeShownLag( attacker );
	vrsPending[slot].swing = vrsHist[attacker->s.number].placedTime == level.time ?
		vrsHist[attacker->s.number].bladeSwing : VRS_SwingId( attacker->s.number );	// (as the blade that cut shows)
	VectorCopy( dir, vrsPending[slot].dir );
	VectorCopy( spot, vrsPending[slot].spot );
	vrsPending[slot].dmg = dmg;
	vrsPending[slot].dflags = dflags;
	vrsPending[slot].saberNum = saberNum;
	vrsPending[slot].bladeNum = bladeNum;
	if ( victim->client && victim->client->g2LastSurfaceTime == level.time ) {
		vrsPending[slot].surfaceSet = qtrue;
		vrsPending[slot].surface = victim->client->g2LastSurfaceHit;
	}
}

// The VR player's blade met that attacker's at (their) time 'since' +
// VR_UNLAG_SLACK: that attack's cuts on them from 'since' on (as seen) never
// happened. (swing: which attack, VRS_SwingId; -1: any.)
static void VRS_CancelPending( int attacker, int victim, int since, int swing ) {
	int i;

	for ( i = 0; i < VRS_MAX_PENDING; i++ ) {
		if ( vrsPending[i].inuse && vrsPending[i].attacker == attacker && vrsPending[i].victim == victim &&
			vrsPending[i].seen >= since && ( swing < 0 || vrsPending[i].swing == swing ) ) {
			const int met = since + VR_UNLAG_SLACK - vrsPending[i].seen;	// (ms the meeting was after the cut)
			vrsPending[i].inuse = qfalse;
			if ( g_vrSaberLog.integer ) {
				G_LogPrintf( "VRSaber: blocked %i \"%s\"'s cut on \"%s\" (%i damage; their blade met it %i ms %s it reached them, as they saw it)\n",
					attacker, g_entities[attacker].client ? g_entities[attacker].client->pers.netname : "?",
					g_entities[victim].client ? g_entities[victim].client->pers.netname : "?", vrsPending[i].dmg,
					abs( met ), met > 0 ? "after" : "before" );
			}
		}
	}
}

// Did this VR player's blade cut that one down before time t (a cut of
// theirs), as the VR player saw it? Then that cut never happened.
static qboolean VRS_KilledFirst( int vr, int other, int t ) {
	const vrSaberState_t *vs = &vrSabers[vr];
	gentity_t *ent = &g_entities[other];

	if ( !ent->client || ent->health > 0 || ent->client->lasthurt_client != vr || ent->client->lasthurt_mod != MOD_SABER ) {
		return qfalse;
	}
	// (the blades' last hit on them: the one that killed them)
	return (qboolean)( vs->hitTime[other] && vs->hitTime[other] >= t - VR_UNLAG_MAX && vs->hitVrTime[other] < t );
}

// A cut that waited lands: the damage, and the hit as w_saber.c shows one.
static void VRS_LandPending( int i ) {
	gentity_t *victim = &g_entities[vrsPending[i].victim];
	gentity_t *attacker = &g_entities[vrsPending[i].attacker];
	gentity_t *te;

	vrsPending[i].inuse = qfalse;
	te = G_TempEntity( vrsPending[i].spot, EV_SABER_HIT );
	if ( te ) {
		te->s.otherEntityNum = victim->s.number;
		te->s.otherEntityNum2 = attacker->s.number;
		te->s.weapon = vrsPending[i].saberNum;
		te->s.legsAnim = vrsPending[i].bladeNum;
		VectorCopy( vrsPending[i].spot, te->s.origin );
		VectorScale( vrsPending[i].dir, -1.0f, te->s.angles );
		if ( !te->s.angles[0] && !te->s.angles[1] && !te->s.angles[2] ) {
			te->s.angles[1] = 1.0f;
		}
		te->s.eventParm = ( vrsPending[i].dmg < 5 ) ? 3 : ( vrsPending[i].dmg < 20 ) ? 2 : 1;
	}
	if ( vrsPending[i].surfaceSet && victim->client ) {
		// (where on the body it hit, as when it was made)
		victim->client->g2LastSurfaceHit = vrsPending[i].surface;
		victim->client->g2LastSurfaceTime = level.time;
	}
	G_Damage( victim, attacker, attacker, vrsPending[i].dir, vrsPending[i].spot, vrsPending[i].dmg,
		vrsPending[i].dflags, MOD_SABER );
}

// Once a frame, after every blade swept: cuts whose victim's moves have come
// in past them land (their blade didn't meet it, nor cut the attacker down
// first), in the order they were made; so do ones waited on too long (lag),
// or on someone no longer fighting in VR.
static void VRS_SettlePending( void ) {
	int due[VRS_MAX_PENDING];
	int i, j, numDue = 0;

	for ( i = 0; i < VRS_MAX_PENDING; i++ ) {
		gentity_t *victim, *attacker;
		vrSaberState_t *vs;

		if ( !vrsPending[i].inuse ) {
			continue;
		}
		victim = &g_entities[vrsPending[i].victim];
		attacker = &g_entities[vrsPending[i].attacker];
		if ( !victim->inuse || !victim->client || victim->health < 1 || !attacker->inuse || !attacker->client ) {
			vrsPending[i].inuse = qfalse;	// (gone, or already dead)
			continue;
		}
		if ( VRS_KilledFirst( vrsPending[i].victim, vrsPending[i].attacker, vrsPending[i].seen ) ) {
			vrsPending[i].inuse = qfalse;
			if ( g_vrSaberLog.integer ) {
				G_LogPrintf( "VRSaber: blocked %i \"%s\"'s cut on \"%s\" (%i damage; they had cut \"%s\" down before it reached them, as they saw it)\n",
					vrsPending[i].attacker, attacker->client->pers.netname, victim->client->pers.netname, vrsPending[i].dmg,
					attacker->client->pers.netname );
			}
			continue;
		}
		vs = &vrSabers[vrsPending[i].victim];
		if ( !VRS_Unlag() || !G_VRSaberActive( victim ) || level.time - vrsPending[i].time >= VR_UNLAG_MAX ||
			( vs->count && VRS_Sample( vs, 0 )->time >= vrsPending[i].seen + VR_UNLAG_LEAD ) ) {
			// (in the order they were made: the first blow is the one that kills)
			for ( j = numDue; j > 0 && vrsPending[due[j - 1]].seen > vrsPending[i].seen; j-- ) {
				due[j] = due[j - 1];
			}
			due[j] = i;
			numDue++;
		}
	}
	for ( i = 0; i < numDue; i++ ) {
		if ( !vrsPending[due[i]].inuse ) {
			continue;
		}
		if ( g_entities[vrsPending[due[i]].victim].health < 1 ) {
			vrsPending[due[i]].inuse = qfalse;	// (an earlier one killed them)
			continue;
		}
		VRS_LandPending( due[i] );
	}
}

/*
--------------------------------------------------------------------------------
The sweep
--------------------------------------------------------------------------------
*/

#define VRS_MAX_OTHER_BLADES 64

// Lit blades of other players near the swept area (as far as they reach
// before a wall). (With lag compensation a little further - where they were
// a moment ago counts - and a player's or bot's blades that were lit any time
// from 'since' on, though out or thrown now.)
static int VRS_GatherBlades( gentity_t *self, const vec3_t mins, const vec3_t maxs, int since, vrsOtherBlade_t *out, int max ) {
	int list[MAX_GENTITIES];
	int i, n, num = 0;
	vec3_t bmins, bmaxs;
	const qboolean unlag = VRS_Unlag();
	const float margin = unlag ? 64.0f + 96.0f : 64.0f;

	for ( i = 0; i < 3; i++ ) {
		bmins[i] = mins[i] - margin;	// a blade reaches out of its owner's box
		bmaxs[i] = maxs[i] + margin;
	}
	n = trap->EntitiesInBox( bmins, bmaxs, list, MAX_GENTITIES );
	for ( i = 0; i < n && num < max; i++ ) {
		gentity_t *other = &g_entities[list[i]];
		const int c = other->s.number;
		qboolean nowOn, kept;
		vec3_t body;
		int s, b, lagShown;

		if ( other == self || !other->inuse || !other->client ) {
			continue;
		}
		if ( OnSameTeam( self, other ) && !g_friendlySaber.integer ) {
			continue;
		}
		if ( ( self->s.eType == ET_NPC || other->s.eType == ET_NPC ) &&
			self->client->playerTeam == other->client->playerTeam && level.gametype != GT_SIEGE ) {
			continue;	// (as stock: NPCs don't fence their teammates)
		}
		if ( ( other->client->ps.duelInProgress && other->client->ps.duelIndex != self->s.number ) ||
			( self->client->ps.duelInProgress && self->client->ps.duelIndex != other->s.number ) ) {
			continue;	// a private duel: their blades only meet each other
		}
		// its blades as they are now (a broken guard lets swings through)...
		nowOn = (qboolean)( other->health >= 1 && other->client->ps.weapon == WP_SABER && !BG_SabersOff( &other->client->ps ) &&
			!PM_SaberInBrokenParry( other->client->ps.saberMove ) );
		// ...or as they were (lag compensation: a player's or bot's)
		kept = (qboolean)( unlag && c >= 0 && c < MAX_CLIENTS && !G_VRSaberClient( other ) && vrsHist[c].count );
		if ( !nowOn && !kept ) {
			continue;
		}
		// (a bot's kept blades show what everyone saw a frame before:
		// VRS_BladeShownLag - round where its body was drawn then)
		lagShown = kept ? VRS_BladeShownLag( other ) : 0;
		if ( kept ) {
			if ( lagShown ) {
				VectorCopy( VRS_HistSample( c, 0 )->origin, body );
			} else {
				VectorCopy( other->r.currentOrigin, body );	// (mid-frame: where the snapshot to come will have them)
			}
		}
		for ( s = 0; s < MAX_SABERS && num < max; s++ ) {
			for ( b = 0; b < MAX_BLADES && num < max; b++ ) {
				bladeInfo_t *blade = &other->client->saber[s].blade[b];
				const qboolean hist = (qboolean)( kept && b < VRS_HIST_BLADES );
				const qboolean on = (qboolean)( nowOn && VRS_BladeOn( other->client, s, b ) &&
					level.time - blade->storageTime <= 200 );	// (else not updated lately)
				vec3_t tip, ofs;

				if ( !on && !( hist && VRS_HistLit( c, s, b, since ) ) ) {
					continue;
				}
				memset( &out[num], 0, sizeof( out[num] ) );
				out[num].ent = other;
				out[num].saber = s;
				out[num].blade = b;
				out[num].histClient = hist ? c : -1;
				if ( on ) {
					VectorClear( ofs );
					if ( hist ) {
						VRS_BladeOffset( other, blade, body, ofs );	// (round where it was drawn, as kept)
					}
					VectorAdd( blade->muzzlePoint, ofs, out[num].base );
					VectorMA( out[num].base, blade->lengthMax, blade->muzzleDir, tip );
					VRS_ClipBlade( c, out[num].base, tip, out[num].tip );
					// (and where it was the frame before)
					out[num].time1 = blade->storageTime - ( hist ? lagShown : 0 );
					out[num].time0 = out[num].time1 - Q_max( 1, level.time - level.previousTime );
					if ( DistanceSquared( blade->muzzlePointOld, blade->muzzlePoint ) < Square( 64.0f ) &&
						VectorLengthSquared( blade->muzzleDirOld ) > 0.5f ) {
						VectorAdd( blade->muzzlePointOld, ofs, out[num].base0 );
						VectorMA( out[num].base0, blade->lengthMax, blade->muzzleDirOld, tip );
						VRS_ClipBlade( c, out[num].base0, tip, out[num].tip0 );
					} else {
						VectorCopy( out[num].base, out[num].base0 );
						VectorCopy( out[num].tip, out[num].tip0 );
					}
				}
				num++;
			}
		}
	}
	return num;
}

// Sweep one blade from pose A to pose B. *stopped: it met another blade and
// stopped (here or before): from there on it cuts nothing more this frame,
// but it's still where the player holds it - other blades it meets meet a
// guard. (otherSpeed: units/ms the fastest blade near it moves - the steps
// are fine enough for both to be followed.)
static void VRS_SweepSegment( gentity_t *self, int s, int b, const vrSaberSample_t *A, const vrSaberSample_t *B,
	const vrsOtherBlade_t *others, int numOthers, float otherSpeed, int *stepsLeft, int shift, qboolean *stopped ) {
	vrSaberState_t *vs = VRS_State( self );
	const float length = self->client->saber[s].blade[b].lengthMax;
	vec3_t tipA, tipB, prevBase, prevTip, prevFullTip;
	float travel;
	int steps, k, i;

	VectorMA( A->base[s][b], length, A->dir[s][b], tipA );
	VectorMA( B->base[s][b], length, B->dir[s][b], tipB );
	travel = Q_max( Distance( A->base[s][b], B->base[s][b] ), Distance( tipA, tipB ) );
	if ( numOthers ) {
		travel += otherSpeed * ( B->time - A->time );
	}
	steps = Com_Clampi( 1, VR_SABER_MAX_STEPS, (int)ceilf( travel / VR_SABER_STEP ) );
	if ( steps > *stepsLeft ) {
		steps = Q_max( 1, *stepsLeft );	// (no more this frame than an honest swing ever needs)
	}
	*stepsLeft -= steps;

	VectorCopy( A->base[s][b], prevBase );
	VectorCopy( tipA, prevFullTip );
	VRS_ClipBlade( self->s.number, prevBase, tipA, prevTip );

	for ( k = 1; k <= steps; k++ ) {
		const float f = (float)k / steps;
		const float t = A->time + f * ( B->time - A->time );
		const qboolean wasStopped = *stopped;
		vec3_t base, dir, fullTip, tip, moveDir, point, bmins, bmaxs;
		trace_t tr;

		VRS_Lerp( A->base[s][b], f, B->base[s][b], base );
		VRS_SlerpDir( A->dir[s][b], B->dir[s][b], f, dir );
		VectorMA( base, length, dir, fullTip );
		VRS_ClipBlade( self->s.number, base, fullTip, tip );	// walls stop the blade
		VectorSubtract( fullTip, prevFullTip, moveDir );
		VectorNormalize( moveDir );

		// another blade in the way stops this one (unless it breaks through):
		// where it was at that moment, the cores touching - every blade it
		// meets (each one met is stopped, or blocked, too)
		for ( i = 0; i < numOthers; i++ ) {
			vec3_t oBase, oTip;
			int attackingAt, swingAt;
			if ( !VRS_OtherBladeAt( &others[i], t, shift, oBase, oTip, &attackingAt, &swingAt ) ) {
				continue;	// (not there then)
			}
			if ( VRS_BladesMeet( prevBase, prevTip, base, tip, oBase, oTip, 2.0f * VR_SABER_CORE, point ) ) {
				vec3_t rel;
				float along, speed;
				qboolean goesOn;
				VectorSubtract( point, base, rel );
				along = Com_Clamp( 0.0f, length, DotProduct( rel, dir ) );
				speed = wasStopped ? 0.0f : VRS_SwingSpeed( vs, s, b, t, along );	// (stopped: a guard)
				if ( G_VRSaberClient( others[i].ent ) ) {
					goesOn = VRS_Clash( self, s, b, &others[i], point, moveDir, speed );
				} else {
					goesOn = VRS_MeetAnimated( self, s, b, others[i].ent, others[i].saber, others[i].blade, point, moveDir, speed,
						(int)t, attackingAt, swingAt );
				}
				if ( !goesOn ) {
					*stopped = qtrue;	// stopped here
				}
			}
		}

		// bodies along the blade
		if ( !*stopped ) {
			VectorSet( bmins, -1.0f, -1.0f, -1.0f );
			VectorSet( bmaxs, 1.0f, 1.0f, 1.0f );
			trap->Trace( &tr, base, bmins, bmaxs, tip, self->s.number, MASK_SHOT, qfalse, 0, 0 );
			if ( tr.entityNum < ENTITYNUM_WORLD ) {
				gentity_t *victim = &g_entities[tr.entityNum];
				if ( victim->inuse && victim->takedamage && victim != self &&
					( victim->health > 0 || !( victim->s.eFlags & EF_DISINTEGRATION ) ) &&
					( !victim->ghoul2 || WP_VR_G2TraceCollide( &tr, base, tip, bmins, bmaxs ) ) ) {
					VRS_Hit( self, s, b, victim, tr.endpos, base, dir, length, t, moveDir );
				}
			}
		}

		VectorCopy( base, prevBase );
		VectorCopy( tip, prevTip );
		VectorCopy( fullTip, prevFullTip );
	}
}

// Damage and clashes for one held blade, through every pose since the last
// server frame (called from WP_SaberPositionUpdate instead of the stock
// saber damage traces).
void G_VRSaberSweep( gentity_t *self, int saberNum, int bladeNum ) {
	vrSaberState_t *vs = VRS_State( self );
	const vrSaberSample_t *hist[VR_SABER_SAMPLES];
	vrsOtherBlade_t others[VRS_MAX_OTHER_BLADES];
	vec3_t mins, maxs;
	float length, otherSpeed = 0.0f;
	int i, n, num, numOthers, swept, stepsLeft = VR_SABER_FRAME_STEPS, shift;
	qboolean stopped = qfalse;

	if ( !vs || saberNum < 0 || saberNum >= VR_POSE_MAX_SABERS || bladeNum < 0 || bladeNum >= VR_POSE_MAX_BLADES || !vs->count ) {
		return;
	}
	swept = vs->sweptTime[saberNum][bladeNum];
	vs->sweptTime[saberNum][bladeNum] = VRS_Sample( vs, 0 )->time;

	// the poses of this blade since the last one already swept
	n = VRS_BladeHistory( vs, saberNum, bladeNum, hist );
	for ( num = 0; num < n; num++ ) {
		if ( hist[num]->time <= swept ) {
			break;
		}
	}
	if ( num >= n || num < 1 ) {
		return;	// (re)starting (sweep from here on), or nothing new
	}

	// the other blades anywhere near it this frame
	length = self->client->saber[saberNum].blade[bladeNum].lengthMax;
	ClearBounds( mins, maxs );
	for ( i = 0; i <= num; i++ ) {
		vec3_t tip;
		VectorMA( hist[i]->base[saberNum][bladeNum], length, hist[i]->dir[saberNum][bladeNum], tip );
		AddPointToBounds( hist[i]->base[saberNum][bladeNum], mins, maxs );
		AddPointToBounds( tip, mins, maxs );
	}
	numOthers = VRS_GatherBlades( self, mins, maxs, (int)VRS_UnlagTime( hist[num]->time ), others, VRS_MAX_OTHER_BLADES );
	for ( i = 0; i < numOthers; i++ ) {
		if ( others[i].time1 > others[i].time0 ) {
			otherSpeed = Q_max( otherSpeed, Q_max( Distance( others[i].base0, others[i].base ), Distance( others[i].tip0, others[i].tip ) ) /
				( others[i].time1 - others[i].time0 ) );
		}
		if ( others[i].histClient >= 0 ) {
			// (followed back in time: as fast as it moved then)
			otherSpeed = Q_max( otherSpeed, VRS_HistBladeSpeed( others[i].histClient, others[i].saber, others[i].blade,
				hist[num]->time, hist[0]->time ) );
		}
	}

	// (pose times are the player's command times: the time of the world they
	// were looking at. Lag compensation takes the others back to then; else
	// the newest pose counts as now.)
	shift = level.time - VRS_Sample( vs, 0 )->time;
	for ( i = num; i > 0; i-- ) {
		const vrSaberSample_t *A = hist[i];
		const vrSaberSample_t *B = hist[i - 1];
		if ( B->time - A->time > VR_SABER_GAP ) {
			continue;	// lag: don't sweep across the gap
		}
		if ( !stopped ) {
			VRS_ShiftBodies( self, B->time, mins, maxs );	// (once it stopped it cuts nobody)
		}
		// (once it stopped on another blade it still blocks others: it's where
		// the player holds it)
		VRS_SweepSegment( self, saberNum, bladeNum, A, B, others, numOthers, otherSpeed, &stepsLeft, shift, &stopped );
	}
	VRS_UnshiftBodies();
}

/*
--------------------------------------------------------------------------------
Animated blades against VR blades
--------------------------------------------------------------------------------
*/

// Where a VR blade was at time t (its player's command time), from its poses.
static qboolean VRS_BladeAt( const vrSaberSample_t **hist, int n, int s, int b, float t, float length, vec3_t base, vec3_t tip ) {
	const vrSaberSample_t *older, *newer;
	vec3_t dir;
	float f;

	if ( n < 1 ) {
		return qfalse;
	}
	if ( !VRS_Bracket( hist, n, t, &older, &newer, &f ) ) {
		older = newer = hist[n - 1];	// (before its history: where it started)
		f = 0.0f;
	}
	VRS_Lerp( older->base[s][b], f, newer->base[s][b], base );
	VRS_SlerpDir( older->dir[s][b], newer->dir[s][b], f, dir );
	VectorMA( base, length, dir, tip );
	return qtrue;
}

// Can this player's and that one's blades meet at all? (as VRS_GatherBlades)
static qboolean VRS_Fencing( gentity_t *a, gentity_t *b ) {
	if ( OnSameTeam( a, b ) && !g_friendlySaber.integer ) {
		return qfalse;
	}
	if ( ( a->s.eType == ET_NPC || b->s.eType == ET_NPC ) && a->client->playerTeam == b->client->playerTeam &&
		level.gametype != GT_SIEGE ) {
		return qfalse;
	}
	if ( ( a->client->ps.duelInProgress && a->client->ps.duelIndex != b->s.number ) ||
		( b->client->ps.duelInProgress && b->client->ps.duelIndex != a->s.number ) ) {
		return qfalse;
	}
	return qtrue;
}

// How far a VR blade's ends moved from time t0 to t1 (its player's command
// times), pose to pose.
static float VRS_BladeTravel( const vrSaberSample_t **hist, int n, int s, int b, float length, float t0, float t1 ) {
	vec3_t pb, pt, nb, nt;
	float travel = 0.0f;
	int i;

	if ( !VRS_BladeAt( hist, n, s, b, t0, length, pb, pt ) ) {
		return 0.0f;
	}
	for ( i = n - 1; i >= 0; i-- ) {	// (oldest first)
		if ( hist[i]->time <= t0 || hist[i]->time >= t1 ) {
			continue;
		}
		VectorCopy( hist[i]->base[s][b], nb );
		VectorMA( nb, length, hist[i]->dir[s][b], nt );
		travel += Q_max( Distance( pb, nb ), Distance( pt, nt ) );
		VectorCopy( nb, pb );
		VectorCopy( nt, pt );
	}
	VRS_BladeAt( hist, n, s, b, t1, length, nb, nt );
	return travel + Q_max( Distance( pb, nb ), Distance( pt, nt ) );
}

/*
=================
G_VRSaberBlocksAnimated

An animated blade (a bot's, or a player's without VR), followed from where
it was last frame to where it is now: does a VR player's blade stand in its
way? Then it stops there - sparks, the VR player feels it, it bounces off
(or a much stronger guard knocks it away) and cuts nothing for a moment
(VRS_MeetAnimated). w_saber.c asks before the blade's damage traces, and
skips them if so. Both blades are followed a few units at a time, so they
can't pass through each other between two looks.
=================
*/
#define VR_SABER_MEET_STEPS	32		// most looks per pair of blades per frame

qboolean G_VRSaberBlocksAnimated( gentity_t *self, int saberNum, int bladeNum, const vec3_t base, const vec3_t tip ) {
	bladeInfo_t *blade;
	vec3_t base0, tip0, mins, maxs;
	float travel;
	int t0, i;

	if ( VRS_Unlag() && self && self->s.number >= 0 && self->s.number < MAX_CLIENTS ) {
		// (lag compensation: the VR blade isn't where this one is now yet -
		// their sweep meets it where they saw it, and its cuts on them wait
		// for that: G_VRSaberDeferHit. NPCs' blades aren't kept: as they are.)
		return qfalse;
	}
	if ( !vrPosesAvailable || !G_VRSaberOn() || !self || !self->client || G_VRSaberClient( self ) ||
		saberNum < 0 || saberNum >= MAX_SABERS || bladeNum < 0 || bladeNum >= MAX_BLADES ||
		!VRS_BladeOn( self->client, saberNum, bladeNum ) ) {
		return qfalse;	// (a thrown saber: G_VRSaberStopsThrown)
	}
	blade = &self->client->saber[saberNum].blade[bladeNum];
	if ( level.time - blade->trail.lastTime > 100 || level.time <= blade->trail.lastTime ||
		DistanceSquared( blade->trail.base, base ) > Square( 64.0f ) ) {
		VectorCopy( base, base0 );	// (no last frame: as it is)
		VectorCopy( tip, tip0 );
		t0 = level.time;
	} else {
		VectorCopy( blade->trail.base, base0 );
		VectorCopy( blade->trail.tip, tip0 );
		t0 = blade->trail.lastTime;
	}
	travel = Q_max( Distance( base0, base ), Distance( tip0, tip ) );
	ClearBounds( mins, maxs );
	AddPointToBounds( base0, mins, maxs );
	AddPointToBounds( tip0, mins, maxs );
	AddPointToBounds( base, mins, maxs );
	AddPointToBounds( tip, mins, maxs );

	for ( i = 0; i < level.maxclients; i++ ) {
		gentity_t *vr = &g_entities[i];
		vrSaberState_t *vs;
		int s, b, shift;

		if ( vr == self || !G_VRSaberActive( vr ) || !VRS_Fencing( self, vr ) ) {
			continue;
		}
		if ( vr->r.absmin[0] - 64.0f > maxs[0] || vr->r.absmax[0] + 64.0f < mins[0] ||
			vr->r.absmin[1] - 64.0f > maxs[1] || vr->r.absmax[1] + 64.0f < mins[1] ||
			vr->r.absmin[2] - 64.0f > maxs[2] || vr->r.absmax[2] + 64.0f < mins[2] ) {
			continue;	// (too far for their blades to reach)
		}
		vs = VRS_State( vr );
		shift = level.time - VRS_Sample( vs, 0 )->time;
		for ( s = 0; s < VR_POSE_MAX_SABERS; s++ ) {
			for ( b = 0; b < VR_POSE_MAX_BLADES; b++ ) {
				const vrSaberSample_t *hist[VR_SABER_SAMPLES];
				const float length = vr->client->saber[s].blade[b].lengthMax;
				int n, k, steps;

				if ( !VRS_BladeOn( vr->client, s, b ) || ( n = VRS_BladeHistory( vs, s, b, hist ) ) < 1 ) {
					continue;
				}
				// both blades through the frame, a step at a time: together they
				// move no more than a step's worth between two looks
				steps = Com_Clampi( 1, VR_SABER_MEET_STEPS,
					(int)ceilf( ( travel + VRS_BladeTravel( hist, n, s, b, length, t0 - shift, level.time - shift ) ) / VR_SABER_STEP ) );
				for ( k = ( t0 == level.time ) ? steps : 0; k <= steps; k++ ) {
					const float f = (float)k / steps;
					const float t = t0 + f * ( level.time - t0 );
					vec3_t ab, at, vb, vt, c1, c2, point, rel, vdir, vb0, vt0, aMove, vMove;
					float speed;
					VRS_Lerp( base0, f, base, ab );
					VRS_Lerp( tip0, f, tip, at );
					VRS_BladeAt( hist, n, s, b, t - shift, length, vb, vt );
					if ( VRS_SegSegClosest( ab, at, vb, vt, c1, c2 ) > 2.0f * VR_SABER_CORE ) {
						continue;
					}
					VRS_Lerp( c1, 0.5f, c2, point );
					// how fast the VR blade moved there; the sparks fly back from
					// whichever blade came at the other faster
					VectorSubtract( vt, vb, vdir );
					VectorNormalize( vdir );
					VectorSubtract( point, vb, rel );
					speed = VRS_SwingSpeed( vs, s, b, t - shift, Com_Clamp( 0.0f, length, DotProduct( rel, vdir ) ) );
					VRS_BladeAt( hist, n, s, b, t0 - shift, length, vb0, vt0 );
					VRS_BladeAt( hist, n, s, b, level.time - shift, length, vb, vt );
					VectorSubtract( vt, vt0, vMove );
					VectorSubtract( tip, tip0, aMove );
					VRS_MeetAnimated( vr, s, b, self, saberNum, bladeNum, point,
						( VectorLengthSquared( vMove ) > VectorLengthSquared( aMove ) ) ? vMove : aMove, speed,
						(int)( t - shift ), -1, -1 );
					return qtrue;
				}
			}
		}
	}
	return qfalse;
}

// Is this entity the saber of a VR player, held in their hand? (Its box
// stops nothing: G_VRSaberBlocksAnimated is where its blade really is.)
qboolean G_VRSaberHeldBox( gentity_t *ent ) {
	gentity_t *owner;

	if ( !ent || ent->r.ownerNum < 0 || ent->r.ownerNum >= MAX_CLIENTS || !vrPosesAvailable ) {
		return qfalse;
	}
	owner = &g_entities[ent->r.ownerNum];
	return (qboolean)( owner->client && owner->client->ps.saberEntityNum == ent->s.number && !owner->client->ps.saberInFlight &&
		G_VRSaberActive( owner ) );
}

/*
=================
G_VRSaberGuard

A bot, an NPC or a player without VR, with the saber ready, sees a VR
player's swing coming and raises the guard into the blade's path: the
automatic block it would otherwise only make once hit (a player without VR
blocks automatically against everyone else too). Whether it sees it in
time depends on its saber defence (g_vrSaberBotGuard scales how often and
how quickly); whether the guard then stops the blade is up to where the
blade really goes.
=================
*/
static struct {
	int		vrEnt;			// the swing being watched: whose...
	int		swingStart;		// ...and which
	int		seen;			// level.time it was first seen coming
	qboolean will;			// will it guard against this one
	qboolean done;
} vrsGuard[MAX_GENTITIES];

extern qboolean PM_SaberInParry( int move );
extern qboolean PM_SaberInBounce( int move );
extern qboolean PM_SaberInKnockaway( int move );
extern qboolean BG_SaberInSpecial( int move );
extern qboolean PM_InKnockDown( playerState_t *ps );
extern qboolean BG_SuperBreakLoseAnim( int anim );
extern qboolean BG_SuperBreakWinAnim( int anim );

// Can it raise its saber into a parry now? (As stock blocks: not while busy
// with an attack, a parry or a bounce, down, rolling, kicking, using a
// force power with its hands, in a saber lock...; the parry would only wait
// until it could, and then come too late.)
static qboolean VRS_ReadyToGuard( gentity_t *self ) {
	gclient_t *cl = self->client;

	if ( self->health < 1 || cl->ps.weapon != WP_SABER || BG_SabersOff( &cl->ps ) || cl->ps.saberInFlight ||
		!cl->ps.saberEntityNum || cl->ps.weaponTime > 0 || cl->ps.weaponstate == WEAPON_RAISING ||
		( cl->saber[0].saberFlags & SFL_NOT_ACTIVE_BLOCKING ) ) {
		return qfalse;
	}
	if ( WP_VR_SaberAttacking( self ) || cl->ps.saberBlocked != BLOCKED_NONE || PM_SaberInParry( cl->ps.saberMove ) ||
		PM_SaberInBrokenParry( cl->ps.saberMove ) || PM_SaberInBounce( cl->ps.saberMove ) ||
		PM_SaberInKnockaway( cl->ps.saberMove ) || BG_SaberInSpecial( cl->ps.saberMove ) ) {
		return qfalse;
	}
	if ( cl->ps.pm_type != PM_NORMAL && cl->ps.pm_type != PM_JETPACK && cl->ps.pm_type != PM_FLOAT ) {
		return qfalse;
	}
	if ( cl->ps.forceHandExtend != HANDEXTEND_NONE || PM_InKnockDown( &cl->ps ) || BG_InRoll( &cl->ps, cl->ps.legsAnim ) ||
		BG_KickingAnim( cl->ps.legsAnim ) || BG_KickingAnim( cl->ps.torsoAnim ) ||
		BG_SuperBreakLoseAnim( cl->ps.torsoAnim ) || BG_SuperBreakWinAnim( cl->ps.torsoAnim ) ||
		cl->ps.saberLockTime > level.time || cl->ps.m_iVehicleNum ) {
		return qfalse;
	}
	if ( cl->ps.fd.forcePowersActive & ( ( 1 << FP_GRIP ) | ( 1 << FP_LIGHTNING ) | ( 1 << FP_DRAIN ) | ( 1 << FP_PUSH ) ) ) {
		return qfalse;
	}
	if ( cl->ps.fd.forcePowerDebounce[FP_SABER_DEFENSE] > level.time || cl->ps.saberBlockTime >= level.time ) {
		return qfalse;	// (can't move the saber to another parry yet)
	}
	return qtrue;
}

void G_VRSaberGuard( gentity_t *self ) {
	static const float chance[4] = { 0.0f, 0.45f, 0.65f, 0.85f };
	static const int reaction[4] = { 0, 200, 140, 90 };
	gclient_t *cl = self ? self->client : NULL;
	const float g = g_vrSaberBotGuard.value;
	vec3_t eye, low, fwd, ang;
	int defense, i;

	if ( !vrPosesAvailable || !G_VRSaberOn() || !cl || g <= 0.0f || G_VRSaberClient( self ) ) {
		return;	// (a VR player guards with their own blade)
	}
	if ( !( self->r.svFlags & SVF_BOT ) && self->s.eType != ET_NPC && ( cl->pers.cmd.buttons & BUTTON_ATTACK ) ) {
		return;	// (a player trying to swing doesn't block, as in the normal game)
	}
	if ( !VRS_ReadyToGuard( self ) ) {
		return;
	}
	defense = Com_Clampi( 0, 3, cl->ps.fd.forcePowerLevel[FP_SABER_DEFENSE] );
	if ( !defense ) {
		return;
	}

	// its body: from above its feet up to its eyes
	VectorCopy( cl->ps.origin, eye );
	eye[2] += cl->ps.viewheight;
	VectorCopy( cl->ps.origin, low );
	low[2] += self->r.mins[2] + 8.0f;
	VectorSet( ang, 0.0f, cl->ps.viewangles[YAW], 0.0f );
	AngleVectors( ang, fwd, NULL, NULL );

	for ( i = 0; i < level.maxclients; i++ ) {
		gentity_t *vr = &g_entities[i];
		vrSaberState_t *vs;
		const vrSaberWeight_t *w;
		int s, b;

		if ( vr == self || !G_VRSaberActive( vr ) || !VRS_Fencing( self, vr ) ||
			DistanceSquared( vr->r.currentOrigin, self->r.currentOrigin ) > Square( 128.0f ) ) {
			continue;
		}
		vs = VRS_State( vr );
		if ( !vs->swing.on || !vs->swing.cutting ) {
			continue;	// (not swinging hard enough to cut)
		}
		w = VRS_Weight( vr );
		for ( s = 0; s < VR_POSE_MAX_SABERS; s++ ) {
			for ( b = 0; b < VR_POSE_MAX_BLADES; b++ ) {
				const vrSaberSample_t *hist[VR_SABER_SAMPLES];
				const float length = vr->client->saber[s].blade[b].lengthMax;
				vec3_t b0, t0, b1, t1, vb, vt;
				int n, k, now;

				if ( !VRS_BladeOn( vr->client, s, b ) || ( n = VRS_BladeHistory( vs, s, b, hist ) ) < 2 ) {
					continue;
				}
				// where the blade is going: carried on as it moves now
				now = hist[0]->time;
				VRS_BladeAt( hist, n, s, b, now - 40, length, b0, t0 );
				VRS_BladeAt( hist, n, s, b, now, length, b1, t1 );
				VectorSubtract( b1, b0, vb );
				VectorSubtract( t1, t0, vt );
				VectorScale( vb, 1000.0f / 40.0f, vb );
				VectorScale( vt, 1000.0f / 40.0f, vt );
				if ( VectorLength( vt ) < w->minSpeed * VRS_SpeedScale() ) {
					continue;
				}
				for ( k = 0; k <= 5; k++ ) {
					float ahead = VR_SABER_GUARD_AHEAD * k / 5.0f;
					vec3_t pb, pt, c1, c2, flat;
					int j;
					VectorMA( b1, ahead, vb, pb );
					VectorMA( t1, ahead, vt, pt );
					if ( VRS_SegSegClosest( pb, pt, low, eye, c1, c2 ) > VR_SABER_GUARD_NEAR ) {
						continue;
					}
					if ( k > 0 ) {
						// the moment it gets to the body, between the looks: the guard
						// goes there (just in front of it, not where the blade would
						// be further on)
						float lo = VR_SABER_GUARD_AHEAD * ( k - 1 ) / 5.0f;
						for ( j = 0; j < 5; j++ ) {
							const float mid = 0.5f * ( lo + ahead );
							VectorMA( b1, mid, vb, pb );
							VectorMA( t1, mid, vt, pt );
							if ( VRS_SegSegClosest( pb, pt, low, eye, c1, c2 ) > VR_SABER_GUARD_AT ) {
								lo = mid;
							} else {
								ahead = mid;
							}
						}
						VectorMA( b1, ahead, vb, pb );
						VectorMA( t1, ahead, vt, pt );
						VRS_SegSegClosest( pb, pt, low, eye, c1, c2 );
					}
					// coming at it: from in front?
					VectorSubtract( c1, eye, flat );
					flat[2] = 0.0f;
					if ( VectorNormalize( flat ) > 0.1f && DotProduct( flat, fwd ) < -0.2f ) {
						break;	// (from behind: it doesn't see it)
					}
					if ( vrsGuard[self->s.number].vrEnt != vr->s.number || vrsGuard[self->s.number].swingStart != vs->swing.start ) {
						vrsGuard[self->s.number].vrEnt = vr->s.number;
						vrsGuard[self->s.number].swingStart = vs->swing.start;
						vrsGuard[self->s.number].seen = level.time;
						if ( VRS_Unlag() && self->s.number < MAX_CLIENTS ) {
							// (lag compensation judges its guard where the VR player saw it:
							// it takes the swing in from when they made it, not from when
							// their moves got here)
							vrsGuard[self->s.number].seen -= Com_Clampi( 0, VR_UNLAG_MAX, level.time - now );
						}
						vrsGuard[self->s.number].will = (qboolean)( Q_flrand( 0.0f, 1.0f ) < Q_min( 1.0f, chance[defense] * g ) - 0.0001f || chance[defense] * g >= 1.0f );
						vrsGuard[self->s.number].done = qfalse;
					}
					if ( !vrsGuard[self->s.number].will || vrsGuard[self->s.number].done ||
						level.time - vrsGuard[self->s.number].seen < reaction[defense] / Q_max( 0.5f, g ) ) {
						return;
					}
					// guard where it will land
					WP_SaberBlockNonRandom( self, c1, qfalse );
					vrsGuard[self->s.number].done = qtrue;
					if ( self->NPC ) {
						// (and hold it: the NPC's own fencing would put its saber
						// back down before the parry starts)
						const int hold = (int)( ahead * 1000.0f ) + 300;
						TIMER_Set( self, "parryTime", hold );
						TIMER_Set( self, "parryReCalcTime", hold );
					}
					if ( g_vrSaberLog.integer ) {
						G_LogPrintf( "VRSaber: guard %i \"%s\" raises its saber against \"%s\"'s swing %.0f ms before it lands (seen %i ms ago)\n",
							self->s.number, cl->pers.netname, vr->client->pers.netname, ahead * 1000.0f, level.time - vrsGuard[self->s.number].seen );
					}
					return;
				}
			}
		}
	}
}
