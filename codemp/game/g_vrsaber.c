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
  - Blocking is whatever your blade physically stops. Animated attackers
	(bots, players without VR) collide with the VR blade through the stock
	saber code, which sees the VR blade as that player's saber.
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
#define VR_UNITS_PER_METRE	34.0f	// (default vr_worldScale) for the m/s in prints and the weight cvars

typedef struct vrSaberSample_s {
	int				time;			// usercmd serverTime (clamped like ClientThink_real does)
	vec3_t			cam;			// the camera the hands were seen from (world)
	float			trackYaw;		// world yaw minus the player's room yaw
	int				teleport;		// the player's EF_TELEPORT_BIT then
	int				bladeMask[VR_POSE_MAX_SABERS];
	int				breakMask[VR_POSE_MAX_SABERS];	// blades that don't carry on from the pose before
	vec3_t			grip[VR_POSE_MAX_SABERS];		// the hand holding the saber (world)
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
	int				throughEnt;			// last guard a heavy blow broke through
	int				throughTime;
	struct {
		int			ent;
		int			time;				// level.time (0: unused)
	}				cuts[VR_SABER_CUTS];	// recent cuts, by victim
	qboolean		activitySet;
	vec3_t			activity;			// where in the room a hand last moved to (g_inactivity)
} vrSaberState_t;

static vrSaberState_t	vrSabers[MAX_CLIENTS];
static qboolean			vrPosesAvailable;	// the engine hands out poses (sv_vrx, vr_engine)

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

enum { VRS_LIGHT, VRS_MEDIUM, VRS_HEAVY, VRS_NUM_WEIGHTS };

// (the defaults of g_vrSaberLight / Medium / Heavy: 1.8 5.3 40 150 and so on)
static const vrSaberWeight_t vrSaberDefaults[VRS_NUM_WEIGHTS] = {
	{ "light",	1.8f * VR_UNITS_PER_METRE, 5.3f * VR_UNITS_PER_METRE,  40.0f, 150, 1 },
	{ "medium",	2.5f * VR_UNITS_PER_METRE, 7.1f * VR_UNITS_PER_METRE,  65.0f, 220, 2 },
	{ "heavy",	3.2f * VR_UNITS_PER_METRE, 8.8f * VR_UNITS_PER_METRE, 100.0f, 300, 3 },
};
static vrSaberWeight_t vrSaberWeights[VRS_NUM_WEIGHTS] = {
	{ "light",	1.8f * VR_UNITS_PER_METRE, 5.3f * VR_UNITS_PER_METRE,  40.0f, 150, 1 },
	{ "medium",	2.5f * VR_UNITS_PER_METRE, 7.1f * VR_UNITS_PER_METRE,  65.0f, 220, 2 },
	{ "heavy",	3.2f * VR_UNITS_PER_METRE, 8.8f * VR_UNITS_PER_METRE, 100.0f, 300, 3 },
};

// The server's g_vrSaberLight / Medium / Heavy changed: "cut from (m/s)
// full damage at (m/s) max damage recover (ms)", e.g. "2.5 7.1 65 220".
void G_VRSaberWeightsChanged( void ) {
	const vmCvar_t *cv[VRS_NUM_WEIGHTS] = { &g_vrSaberLight, &g_vrSaberMedium, &g_vrSaberHeavy };
	int i;

	for ( i = 0; i < VRS_NUM_WEIGHTS; i++ ) {
		float cutFrom, fullAt, maxDamage;
		int recover;

		vrSaberWeights[i] = vrSaberDefaults[i];
		if ( !cv[i]->string[0] ) {
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
}

static const vrSaberWeight_t *VRS_Weight( const gclient_t *cl ) {
	switch ( cl->ps.fd.saberAnimLevel ) {
	case SS_FAST:
	case SS_TAVION:
		return &vrSaberWeights[VRS_LIGHT];
	case SS_STRONG:
	case SS_DESANN:
		return &vrSaberWeights[VRS_HEAVY];
	case SS_DUAL:	// dual sabers: two medium blades
	case SS_STAFF:
	default:
		return &vrSaberWeights[VRS_MEDIUM];
	}
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
	if ( !G_VRSaberOn() || !trap->ext.VR_GetUserPose( clientNum, &pose ) || !pose.valid ) {
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

/*
--------------------------------------------------------------------------------
How fast a blade moves
--------------------------------------------------------------------------------
*/

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

// Where a point 'along' the blade (or the hand holding it, if grip) was at
// time t, in the player's room (see VRS_RoomOffset).
static qboolean VRS_RoomPoint( const vrSaberSample_t **hist, int n, int s, int b, float t, float along, qboolean grip, vec3_t out ) {
	const vrSaberSample_t *newer = NULL, *older = NULL;
	vec3_t p[2];
	float f = 0.0f;
	int i, k;

	for ( i = 0; i < n; i++ ) {
		if ( hist[i]->time <= t ) {
			older = hist[i];
			break;
		}
		newer = hist[i];
	}
	if ( !older ) {
		return qfalse;	// t is before the blade's history
	}
	if ( newer && newer->time > older->time ) {
		f = Com_Clamp( 0.0f, 1.0f, ( t - older->time ) / (float)( newer->time - older->time ) );
	} else {
		newer = older;
	}

	for ( k = 0; k < 2; k++ ) {
		const vrSaberSample_t *smp = k ? newer : older;
		vec3_t point;
		if ( grip ) {
			VectorCopy( smp->grip[s], point );
		} else {
			VectorMA( smp->base[s][b], along, smp->dir[s][b], point );
		}
		VRS_RoomOffset( smp, point, p[k] );
	}
	VRS_Lerp( p[0], f, p[1], out );
	return qtrue;
}

// How fast that point moved over the last moment before t (units/s, in the
// player's room).
static float VRS_PointSpeed( const vrSaberSample_t **hist, int n, int s, int b, float t, float along, qboolean grip ) {
	vec3_t p0, p1;
	float t0 = t - VR_SABER_SPEED_MS;

	if ( !n || !VRS_RoomPoint( hist, n, s, b, t, along, grip, p1 ) ) {
		return 0.0f;
	}
	if ( !VRS_RoomPoint( hist, n, s, b, t0, along, grip, p0 ) ) {
		// shorter history: from the oldest pose of this blade
		t0 = hist[n - 1]->time;
		if ( t - t0 < 20.0f || !VRS_RoomPoint( hist, n, s, b, t0, along, grip, p0 ) ) {
			return 0.0f;
		}
	}
	return Distance( p0, p1 ) / ( ( t - t0 ) * 0.001f );
}

// The speed a blade point hits with: how fast it moved, but no more than a
// few times what the hand did, so a flick of the wrist with a still arm
// barely cuts (the tip of a long blade moves fast even then).
static float VRS_SwingSpeed( vrSaberState_t *vs, int s, int b, float t, float along ) {
	const vrSaberSample_t *hist[VR_SABER_SAMPLES];
	const int n = VRS_BladeHistory( vs, s, b, hist );
	const float pointSpeed = VRS_PointSpeed( hist, n, s, b, t, along, qfalse );
	const float handSpeed = VRS_PointSpeed( hist, n, s, b, t, 0.0f, qtrue );

	return Q_min( pointSpeed, handSpeed * VR_SABER_LEVER );
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
	vec3_t		base, tip;
	float		radius;
} vrsOtherBlade_t;

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

// Our blade, swinging at this speed, hit the other blade: the stronger blow
// wins (weight, plus one for a full-speed swing); a heavy blow two or more
// stronger breaks the guard and goes on through.
static vrsClash_t VRS_ClashReact( gentity_t *self, const vrsOtherBlade_t *ob, vec3_t point, float speed ) {
	gentity_t *other = ob->ent;
	const vrSaberWeight_t *w = VRS_Weight( self->client );
	const vrSaberWeight_t *ow = VRS_Weight( other->client );
	const int strength = VRS_Strength( w, speed );
	const qboolean heavy = (qboolean)( w == &vrSaberWeights[VRS_HEAVY] );
	vrSaberState_t *ovs;

	if ( G_VRSaberClient( other ) && ( ovs = VRS_State( other ) ) != NULL ) {
		// both in VR, and both sweeps see this contact: the faster blade's
		// owner decides, and their blade recovers the other way round
		const float theirSpeed = VRS_BladeSpeedAt( ovs, ob, point );
		const int theirs = VRS_Strength( ow, theirSpeed );
		vrsClash_t result;
		int recover;

		if ( theirSpeed > speed ) {
			return VRS_CLASH_HELD;
		}
		if ( heavy && strength >= theirs + 2 ) {
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
		if ( ovs->recoverTime < level.time + recover ) {
			ovs->recoverTime = level.time + recover;
		}
		return result;
	}

	{
		const qboolean attacking = WP_VR_SaberAttacking( other );
		const int theirs = ow->strength + ( attacking ? 1 : 0 );

		if ( attacking ) {
			if ( strength > theirs ) {
				WP_VR_KnockAttack( other );							// their swing is knocked away
				return ( heavy && strength >= theirs + 2 ) ? VRS_CLASH_THROUGH : VRS_CLASH_WON;
			}
			if ( strength == theirs ) {
				other->client->ps.saberBlocked = BLOCKED_ATK_BOUNCE;	// both bounce off
				return VRS_CLASH_EVEN;
			}
			return VRS_CLASH_LOST;									// theirs was heavier: we reel
		}
		if ( strength >= theirs + 2 ) {
			WP_VR_BreakParry( other, point );						// a heavy blow breaks their guard
			return heavy ? VRS_CLASH_THROUGH : VRS_CLASH_WON;
		}
		WP_SaberBlockNonRandom( other, point, qfalse );				// they parry
		return VRS_CLASH_EVEN;
	}
}

// Our blade met another blade at point: sparks, and the stronger blow wins.
// Returns qtrue if ours carries on (it broke their guard), qfalse if it
// stops there.
static qboolean VRS_Clash( gentity_t *self, int s, int b, const vrsOtherBlade_t *ob, vec3_t point, const vec3_t moveDir, float speed ) {
	gentity_t *other = ob->ent;
	vrSaberState_t *vs = VRS_State( self );
	const vrSaberWeight_t *w = VRS_Weight( self->client );
	const qboolean swing = (qboolean)( speed >= w->minSpeed * VRS_SpeedScale() );
	const vrSaberState_t *ovs;
	vrsClash_t result;
	int recover = 0;

	if ( vs->clashEnt == other->s.number && level.time - vs->clashTime < 150 ) {
		// the same clash going on (blades held together only spark now and
		// then); a blow that broke through keeps going
		return (qboolean)( vs->throughEnt == other->s.number && level.time - vs->throughTime < VR_SABER_THROUGH_MS );
	}

	// sparks, unless the other VR player's sweep already reported this
	ovs = G_VRSaberClient( other ) ? VRS_State( other ) : NULL;
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
	vs->clashTime = level.time;

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
	if ( g_vrSaberDebug.integer ) {
		Com_Printf( "VR saber: %s's %s blade met %s's at %.1f m/s: %s\n", self->client->pers.netname, w->name,
			other->client ? other->client->pers.netname : "?", speed / VR_UNITS_PER_METRE, vrsClashNames[result] );
	}
	return (qboolean)( result == VRS_CLASH_THROUGH );
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

static void VRS_Hit( gentity_t *self, int s, int b, gentity_t *victim, vec3_t point, const vec3_t base, const vec3_t dir,
	float length, float t, const vec3_t moveDir ) {
	vrSaberState_t *vs = VRS_State( self );
	const vrSaberWeight_t *w = VRS_Weight( self->client );
	const float minSpeed = w->minSpeed * VRS_SpeedScale();
	const float fullSpeed = w->fullSpeed * VRS_SpeedScale();
	saberInfo_t *saber = &self->client->saber[s];
	const qboolean style2 = WP_SaberBladeUseSecondBladeStyle( saber, b );
	vec3_t rel;
	float along, speed, dmg;
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
		float frac;
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

	if ( victim->client ) {
		self->client->ps.saberEventFlags |= ( self->enemy == victim ) ? SEF_HITENEMY : SEF_HITOBJECT;
	}
	if ( g_vrSaberDebug.integer && dmg > VR_SABER_BURN ) {
		Com_Printf( "VR saber: %s cut %s at %.1f m/s with a %s blade: %d damage\n", self->client->pers.netname,
			victim->client ? victim->client->pers.netname : ( victim->classname ? victim->classname : "?" ), speed / VR_UNITS_PER_METRE,
			w->name, (int)( dmg + 0.5f ) );
	}
}

/*
--------------------------------------------------------------------------------
The sweep
--------------------------------------------------------------------------------
*/

#define VRS_MAX_OTHER_BLADES 64

// Lit blades of other players near the swept area (as far as they reach
// before a wall).
static int VRS_GatherBlades( gentity_t *self, const vec3_t mins, const vec3_t maxs, vrsOtherBlade_t *out, int max ) {
	int list[MAX_GENTITIES];
	int i, n, num = 0;
	vec3_t bmins, bmaxs;

	for ( i = 0; i < 3; i++ ) {
		bmins[i] = mins[i] - 64.0f;	// a blade reaches out of its owner's box
		bmaxs[i] = maxs[i] + 64.0f;
	}
	n = trap->EntitiesInBox( bmins, bmaxs, list, MAX_GENTITIES );
	for ( i = 0; i < n && num < max; i++ ) {
		gentity_t *other = &g_entities[list[i]];
		int s, b;

		if ( other == self || !other->inuse || !other->client || other->health < 1 ) {
			continue;
		}
		if ( other->client->ps.weapon != WP_SABER || BG_SabersOff( &other->client->ps ) ) {
			continue;
		}
		if ( PM_SaberInBrokenParry( other->client->ps.saberMove ) ) {
			continue;	// a broken guard lets swings through
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
		for ( s = 0; s < MAX_SABERS && num < max; s++ ) {
			for ( b = 0; b < MAX_BLADES && num < max; b++ ) {
				bladeInfo_t *blade;
				vec3_t tip;
				if ( !VRS_BladeOn( other->client, s, b ) ) {
					continue;
				}
				blade = &other->client->saber[s].blade[b];
				if ( level.time - blade->storageTime > 200 ) {
					continue;	// not updated lately
				}
				out[num].ent = other;
				out[num].saber = s;
				out[num].blade = b;
				VectorCopy( blade->muzzlePoint, out[num].base );
				VectorMA( blade->muzzlePoint, blade->lengthMax, blade->muzzleDir, tip );
				VRS_ClipBlade( other->s.number, out[num].base, tip, out[num].tip );
				out[num].radius = blade->radius;
				num++;
			}
		}
	}
	return num;
}

// Sweep one blade from pose A to pose B. Returns qfalse if it stopped on
// another blade.
static qboolean VRS_SweepSegment( gentity_t *self, int s, int b, const vrSaberSample_t *A, const vrSaberSample_t *B,
	const vrsOtherBlade_t *others, int numOthers, int *stepsLeft ) {
	vrSaberState_t *vs = VRS_State( self );
	const float length = self->client->saber[s].blade[b].lengthMax;
	const float radius = self->client->saber[s].blade[b].radius;
	vec3_t tipA, tipB, prevBase, prevTip, prevFullTip;
	float travel;
	int steps, k, i;

	VectorMA( A->base[s][b], length, A->dir[s][b], tipA );
	VectorMA( B->base[s][b], length, B->dir[s][b], tipB );
	travel = Q_max( Distance( A->base[s][b], B->base[s][b] ), Distance( tipA, tipB ) );
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
		vec3_t base, dir, fullTip, tip, moveDir, point, bmins, bmaxs;
		trace_t tr;

		VRS_Lerp( A->base[s][b], f, B->base[s][b], base );
		VRS_SlerpDir( A->dir[s][b], B->dir[s][b], f, dir );
		VectorMA( base, length, dir, fullTip );
		VRS_ClipBlade( self->s.number, base, fullTip, tip );	// walls stop the blade
		VectorSubtract( fullTip, prevFullTip, moveDir );
		VectorNormalize( moveDir );

		// another blade in the way stops this one (unless it breaks through)
		for ( i = 0; i < numOthers; i++ ) {
			if ( VRS_BladesMeet( prevBase, prevTip, base, tip, others[i].base, others[i].tip,
				radius + others[i].radius, point ) ) {
				vec3_t rel;
				float along;
				VectorSubtract( point, base, rel );
				along = Com_Clamp( 0.0f, length, DotProduct( rel, dir ) );
				if ( !VRS_Clash( self, s, b, &others[i], point, moveDir, VRS_SwingSpeed( vs, s, b, t, along ) ) ) {
					return qfalse;	// stopped here
				}
			}
		}

		// bodies along the blade
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

		VectorCopy( base, prevBase );
		VectorCopy( tip, prevTip );
		VectorCopy( fullTip, prevFullTip );
	}
	return qtrue;
}

// Damage and clashes for one held blade, through every pose since the last
// server frame (called from WP_SaberPositionUpdate instead of the stock
// saber damage traces).
void G_VRSaberSweep( gentity_t *self, int saberNum, int bladeNum ) {
	vrSaberState_t *vs = VRS_State( self );
	const vrSaberSample_t *hist[VR_SABER_SAMPLES];
	vrsOtherBlade_t others[VRS_MAX_OTHER_BLADES];
	vec3_t mins, maxs;
	float length;
	int i, n, num, numOthers, swept, stepsLeft = VR_SABER_FRAME_STEPS;

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
	numOthers = VRS_GatherBlades( self, mins, maxs, others, VRS_MAX_OTHER_BLADES );

	for ( i = num; i > 0; i-- ) {
		const vrSaberSample_t *A = hist[i];
		const vrSaberSample_t *B = hist[i - 1];
		if ( B->time - A->time > VR_SABER_GAP ) {
			continue;	// lag: don't sweep across the gap
		}
		if ( !VRS_SweepSegment( self, saberNum, bladeNum, A, B, others, numOthers, &stepsLeft ) ) {
			break;	// stopped on another blade
		}
	}
}
