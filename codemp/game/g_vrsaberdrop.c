/*
===========================================================================
OpenJK VR (multiplayer) - a VR player's saber let go of, the server's side
(their game's: cg_vrsaberdrop.c).

A VR player with holsters who holds their saber by its grip (Saber Grip:
Hold) and opens their hand on it anywhere but at their side drops it: their
game says so (GENCMD_VR_SABERDROP). Only on a server saying so ("vrSaberDrop
1" in its serverinfo: g_vrSaberDrop and g_vrHolsters on), only their first
saber (alone in their hands: one saber, or a staff) and never a siege
class's, nor in a saber lock or a duel's countdown.

- It goes out (with its sound) and falls from where their hand held it, the
  way it held it, going as fast and as their hand went and turning as it
  turned (no faster than VRD_MAX_SPEED, nor turning faster than
  VRD_MAX_SPIN). It bounces off what it meets, a clink if it's hard, and
  comes to rest lying down. Everyone sees it - the stock game's knocked
  away saber (an ET_MISSILE with its hilt's model), its place and angles
  given every frame (TR_INTERPOLATE), so every game draws it as it lies.
- Theirs again (back as the game has it, unlit - what their game does with
  it: in the hand that took it or called it, else on their belt):
    a hand of theirs at its hilt (VRD_TAKE_REACH: their view of it is a
    moment old) as they squeeze it - GENCMD_VR_SABERTAKE;
    called (GENCMD_VR_SABERCALL_L, _R: an empty hand pointed at it and
    squeezed): it flies to that hand, unlit, faster as it goes, and is in it
    as it gets there - or, meeting a wall on the way, falls there;
    left lying VRD_BELT_MS: back (on their belt).
- A VR player's saber knocked away the stock game's way (out of the air,
  thrown) is theirs to take up or call the same way, in place of the
  trigger calling it back. Not theirs to (no longer in VR with holsters):
  as the stock game has it.
- Who took it up, called it or let go of it, and how fast: g_vrSaberLog.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "g_local.h"

#define VRD_HIST			8		// each hand's latest places kept (one a command)
#define VRD_VEL_MS			60		// a hand's speed: over this long (ms)...
#define VRD_HELD_MS			400		// ...the saber in it this lately to be let go of
#define VRD_MAX_SPEED		900.0f	// units a second: let go of no faster than this...
#define VRD_MAX_SPIN		18.0f	// ...nor turning faster than this (radians a second)
#define VRD_BOUNCE			0.4f	// its speed into what it meets, what's left (back off it)...
#define VRD_SLIDE			0.7f	// ...and along it...
#define VRD_SPIN_KEEP		0.5f	// ...and of its turning
#define VRD_REST_SPEED		50.0f	// off the ground slower than this, it lies there...
#define VRD_SETTLE_MS		200		// ...turned flat over this long
#define VRD_CLINK_SPEED		90.0f	// a bounce this hard clinks
#define VRD_BELT_MS			20000	// left lying this long: theirs again (on their belt)
#define VRD_TAKE_REACH		24.0f	// a hand this near its hilt takes it up (units)
#define VRD_CALL_RANGE		4096.0f	// called from no further than this
#define VRD_CALL_START		300.0f	// called: off at this speed...
#define VRD_CALL_ACCEL		2400.0f	// ...faster by this each second...
#define VRD_CALL_SPEED		1000.0f	// ...up to this...
#define VRD_CALL_CATCH		12.0f	// ...in the hand this near it
#define VRD_CALL_SPIN		10.0f	// (end over end on the way: radians a second)
#define VRD_CALL_MS			4000	// a flight this long and it falls where it is
#define VRD_BOX				2.0f	// what it meets: a box this big round where its tag holds it
#define VRD_POMMEL			6.5f	// a hilt's span when its model doesn't say (from where its tag holds it)...
#define VRD_EMITTER			6.0f	// ...to here
#define VRD_UNITS_PER_METRE	34.0f	// (the default vr_worldScale) for the speeds logged

enum {
	VRD_NONE,			// in hand, thrown, or not ours
	VRD_FALLING,
	VRD_LYING,
	VRD_CALLED
};

typedef struct {
	// each hand's latest places (world), a command apart
	int			histTime[VRD_HIST];		// usercmd serverTime (clamped as ClientThink_real does)
	vec3_t		histPos[VRD_HIST][2];
	vec3_t		histDir[VRD_HIST][2];	// ...along the blade of their first saber, in it then
	qboolean	histValid[VRD_HIST][2];
	qboolean	histSaber[VRD_HIST][2];
	int			histHead, histCount;
	// their first saber in a hand lately
	int			heldTime;				// level.time (0: not lately)
	int			heldHand;
	vec3_t		heldGrip;				// the fist (world)
	vec3_t		heldDir;				// along the blade
	vec3_t		heldUp;					// the controller's up
	float		heldHilt;				// the fist along the hilt from where its tag holds it
	// their saber down
	int			state;					// VRD_*
	int			ent;
	int			since;					// level.time it went down
	int			moveTime;				// ...and was last moved
	vec3_t		vel;					// units a second
	vec3_t		ax[3];					// its model's axes (world)
	vec3_t		spin;					// turning: radians a second about a world axis (its length)
	vec3_t		ownDir, ownFace;		// its model's own blade way and face (its frame)
	int			settleTime;				// lying since (turned flat over VRD_SETTLE_MS)...
	vec3_t		settleFrom[3], settleTo[3];
	int			callHand;				// called to this hand (VR_HAND_*)...
	int			callTime;				// ...at this level.time...
	float		callSpeed;				// ...going this fast now
} vrDrop_t;

static vrDrop_t	vrDrops[MAX_CLIENTS];
static int		vrdClink[2];

void saberReactivate( gentity_t *saberent, gentity_t *saberOwner );	// (w_saber.c)
void SaberUpdateSelf( gentity_t *ent );
void SaberGotHit( gentity_t *self, gentity_t *other, trace_t *trace );
void SaberBounceSound( gentity_t *self, gentity_t *other, trace_t *trace );
void DownedSaberThink( gentity_t *saberent );
void WP_SaberAddG2Model( gentity_t *saberent, const char *saberModel, qhandle_t saberSkin );
void WP_SaberRemoveG2Model( gentity_t *saberent );
qboolean BG_SabersOff( playerState_t *ps );		// (bg_pmove.c)

/*
--------------------------------------------------------------------------------
On or off
--------------------------------------------------------------------------------
*/

void G_VRSaberDropInit( void ) {
	memset( vrDrops, 0, sizeof( vrDrops ) );
	vrdClink[0] = G_SoundIndex( "sound/weapons/thermal/bounce1.wav" );
	vrdClink[1] = G_SoundIndex( "sound/weapons/thermal/bounce2.wav" );
	trap->Cvar_Register( NULL, "vrSaberDrop", "0", CVAR_SERVERINFO | CVAR_ROM );
	G_VRSaberDropPublish();
}

// "vrSaberDrop" in the serverinfo: g_vrSaberDrop on, with holsters - VR
// players' games let go of sabers for this server to drop.
void G_VRSaberDropPublish( void ) {
	trap->Cvar_Set( "vrSaberDrop", ( g_vrSaberDrop.integer > 0 && g_vrHolsters.integer > 0 ) ? "1" : "0" );
}

void G_VRSaberDropShutdown( void ) {
	trap->Cvar_Set( "vrSaberDrop", "0" );
}

void G_VRSaberDropClientReset( int clientNum ) {
	if ( clientNum >= 0 && clientNum < MAX_CLIENTS ) {
		memset( &vrDrops[clientNum], 0, sizeof( vrDrops[clientNum] ) );
	}
}

// Whether a player's saber is theirs to drop, take up and call: a VR player
// with holsters (their latest command says so), on a server dropping them.
static qboolean VRD_Allowed( const gentity_t *ent ) {
	return (qboolean)( g_vrSaberDrop.integer > 0 && g_vrHolsters.integer > 0 && ent && ent->client && ent->s.number >= 0 &&
		ent->s.number < MAX_CLIENTS && !( ent->r.svFlags & SVF_BOT ) && G_VRHolsters( ent ) && G_VRPosesAvailable() );
}

// Whether they're about to do something with their hands: alive, playing (on
// foot, or flying a jetpack), not down (roleplay), not in a vehicle.
static qboolean VRD_Playing( const gentity_t *ent ) {
	const gclient_t *cl = ent->client;

	return (qboolean)( ent->health > 0 && !( cl->ps.eFlags & EF_DEAD ) &&
		( cl->ps.pm_type == PM_NORMAL || cl->ps.pm_type == PM_JETPACK ) &&
		cl->sess.sessionTeam != TEAM_SPECTATOR && !( cl->ps.pm_flags & PMF_FOLLOW ) && cl->tempSpectate < level.time &&
		!cl->ps.m_iVehicleNum && !G_RPDowned( ent ) );
}

/*
--------------------------------------------------------------------------------
Their hands, a command at a time
--------------------------------------------------------------------------------
*/

// The avatar that came with a command (G_VRSaberUsercmd, after it's taken):
// where each hand is (world), and the saber in one.
void G_VRSaberDropUsercmd( gentity_t *ent, const usercmd_t *ucmd ) {
	const int n = ent ? ent->s.number : -1;
	vrDrop_t *d;
	vrAvatar_t a;
	int t, h, k;

	if ( n < 0 || n >= MAX_CLIENTS || !ent->client ) {
		return;
	}
	d = &vrDrops[n];
	if ( !ent->client->ps.saberInFlight && d->state != VRD_NONE ) {
		// (back some other way - respawned, say: nothing of ours down)
		if ( d->ent > 0 && d->ent < ENTITYNUM_MAX_NORMAL && g_entities[d->ent].s.otherEntityNum2 == n + 1 ) {
			g_entities[d->ent].s.otherEntityNum2 = 0;
		}
		d->state = VRD_NONE;
		d->ent = 0;
	}
	if ( !G_VRSaberAvatar( n, &a ) ) {
		d->histCount = 0;
		return;
	}
	t = Com_Clampi( level.time - 1000, level.time + 200, ucmd->serverTime );
	if ( d->histCount && t - d->histTime[d->histHead] <= 0 ) {
		if ( t - d->histTime[d->histHead] < -500 ) {
			d->histCount = 0;	// (the clock went back: a restart)
		} else {
			return;	// (the same command again)
		}
	}
	k = d->histHead = ( d->histHead + 1 ) % VRD_HIST;
	d->histCount = Q_min( d->histCount + 1, VRD_HIST );
	d->histTime[k] = t;
	for ( h = 0; h < 2; h++ ) {
		const vrAvatarHand_t *hand = &a.hand[h];
		d->histValid[k][h] = (qboolean)( hand->grip != VR_GRIP_NONE );
		d->histSaber[k][h] = (qboolean)( hand->grip == VR_GRIP_SABER && hand->saber == 0 );
		VectorAdd( ent->client->ps.origin, hand->origin, d->histPos[k][h] );
		VectorCopy( hand->dir, d->histDir[k][h] );
		if ( !d->histSaber[k][h] || VectorNormalize( d->histDir[k][h] ) < 0.5f ) {
			d->histSaber[k][h] = qfalse;
			continue;
		}
		d->heldTime = level.time ? level.time : 1;
		d->heldHand = h;
		VectorCopy( d->histPos[k][h], d->heldGrip );
		VectorCopy( d->histDir[k][h], d->heldDir );
		VectorCopy( hand->axis[2], d->heldUp );
		d->heldHilt = Com_Clamp( -VR_POSE_MAX_HILT, VR_POSE_MAX_HILT, hand->hilt );
	}
}

// How hand h went lately (world, units a second): over about VRD_VEL_MS to its
// latest place - and how the saber in it turned (radians a second about a
// world axis), from the last two places it held it.
static void VRD_HandMotion( const vrDrop_t *d, int h, vec3_t vel, vec3_t spin ) {
	const int k1 = d->histHead;
	int i, k0 = -1, s1 = -1, s0 = -1;

	VectorClear( vel );
	VectorClear( spin );
	if ( d->histCount < 2 || !d->histValid[k1][h] ) {
		return;
	}
	for ( i = 1; i < d->histCount; i++ ) {
		const int k = ( d->histHead - i + VRD_HIST ) % VRD_HIST;
		if ( !d->histValid[k][h] ) {
			break;
		}
		k0 = k;
		if ( d->histTime[k1] - d->histTime[k] >= VRD_VEL_MS ) {
			break;
		}
	}
	if ( k0 >= 0 && d->histTime[k1] - d->histTime[k0] >= 8 ) {
		const float dt = ( d->histTime[k1] - d->histTime[k0] ) * 0.001f;
		VectorSubtract( d->histPos[k1][h], d->histPos[k0][h], vel );
		VectorScale( vel, 1.0f / dt, vel );
	}
	// the saber's turn: its latest two places in that hand
	for ( i = 0; i < d->histCount; i++ ) {
		const int k = ( d->histHead - i + VRD_HIST ) % VRD_HIST;
		if ( !d->histSaber[k][h] ) {
			continue;
		}
		if ( s1 < 0 ) {
			s1 = k;
		} else {
			s0 = k;
			break;
		}
	}
	if ( s0 >= 0 && d->histTime[s1] - d->histTime[s0] >= 4 ) {
		const float dt = ( d->histTime[s1] - d->histTime[s0] ) * 0.001f;
		const float c = Com_Clamp( -1.0f, 1.0f, DotProduct( d->histDir[s0][h], d->histDir[s1][h] ) );
		CrossProduct( d->histDir[s0][h], d->histDir[s1][h], spin );
		if ( VectorNormalize( spin ) > 1e-4f ) {
			VectorScale( spin, acosf( c ) / dt, spin );
		} else {
			VectorClear( spin );
		}
	}
}

/*
--------------------------------------------------------------------------------
Its model's way round
--------------------------------------------------------------------------------
*/

// The way its own model's blade runs (from where its tag holds it) and its
// face, at rest (the model's own frame: as the games draw it, cg_vrholster.c's
// hilts too) - up, if its model doesn't say.
static void VRD_ModelFrame( gentity_t *saberent, vec3_t ownDir, vec3_t ownFace ) {
	static vec3_t zero = { 0.0f, 0.0f, 0.0f };
	mdxaBone_t m;
	vec3_t dir, face;
	int bolt;

	VectorSet( ownDir, 0.0f, 0.0f, 1.0f );
	VectorSet( ownFace, 1.0f, 0.0f, 0.0f );
	if ( !saberent->ghoul2 || !trap->G2API_HaveWeGhoul2Models( saberent->ghoul2 ) ) {
		return;
	}
	bolt = trap->G2API_AddBolt( saberent->ghoul2, 0, "*blade1" );
	if ( bolt < 0 ) {
		bolt = trap->G2API_AddBolt( saberent->ghoul2, 0, "*flash" );	// (an old saber)
	}
	if ( bolt < 0 || !trap->G2API_GetBoltMatrix( saberent->ghoul2, 0, bolt, &m, zero, zero, level.time, NULL, zero ) ) {
		return;
	}
	BG_GiveMeVectorFromMatrix( &m, NEGATIVE_Y, dir );	// (a blade runs along its bolt's -Y)
	BG_GiveMeVectorFromMatrix( &m, POSITIVE_X, face );
	if ( VectorNormalize( dir ) < 0.5f ) {
		return;
	}
	VectorMA( face, -DotProduct( face, dir ), dir, face );
	if ( VectorNormalize( face ) < 0.1f ) {
		PerpendicularVector( face, dir );
	}
	VectorCopy( dir, ownDir );
	VectorCopy( face, ownFace );
}

// Its model's axes in the world (each its own x, y, z), its blade along dir
// and its face toward face (square to dir).
static void VRD_Axes( const vec3_t ownDir, const vec3_t ownFace, const vec3_t dir, const vec3_t face, vec3_t ax[3] ) {
	vec3_t own3, d3;
	int k;

	CrossProduct( ownDir, ownFace, own3 );
	CrossProduct( dir, face, d3 );
	for ( k = 0; k < 3; k++ ) {
		VectorScale( dir, ownDir[k], ax[k] );
		VectorMA( ax[k], ownFace[k], face, ax[k] );
		VectorMA( ax[k], own3[k], d3, ax[k] );
	}
}

// A model's own direction v (its frame), in the world.
static void VRD_World( vec3_t ax[3], const vec3_t v, vec3_t out ) {
	VectorScale( ax[0], v[0], out );
	VectorMA( out, v[1], ax[1], out );
	VectorMA( out, v[2], ax[2], out );
}

// Square again (x kept, y straightened against it, z = x * y).
static void VRD_Square( vec3_t ax[3] ) {
	if ( VectorNormalize( ax[0] ) < 1e-3f ) {
		VectorSet( ax[0], 1.0f, 0.0f, 0.0f );
	}
	VectorMA( ax[1], -DotProduct( ax[1], ax[0] ), ax[0], ax[1] );
	if ( VectorNormalize( ax[1] ) < 1e-3f ) {
		PerpendicularVector( ax[1], ax[0] );
	}
	CrossProduct( ax[0], ax[1], ax[2] );
}

// The angles AnglesToAxis turns back into these axes (forward, left, up).
void G_VRAxisToAngles( vec3_t ax[3], vec3_t angles ) {
	const float flat = sqrtf( ax[0][0] * ax[0][0] + ax[0][1] * ax[0][1] );

	if ( flat > 1e-4f ) {
		angles[YAW] = RAD2DEG( atan2f( ax[0][1], ax[0][0] ) );
		angles[PITCH] = RAD2DEG( atan2f( -ax[0][2], flat ) );
		angles[ROLL] = RAD2DEG( atan2f( ax[1][2], ax[2][2] ) );
	} else {
		// (straight up or down: its yaw is its roll's)
		const float sp = ( ax[0][2] < 0.0f ) ? 1.0f : -1.0f;
		angles[PITCH] = 90.0f * sp;
		angles[YAW] = 0.0f;
		angles[ROLL] = RAD2DEG( atan2f( ax[1][0] * sp, ax[1][1] ) );
	}
}

// Turned about a world axis (spin's way) by its length times dt.
static void VRD_Turn( vec3_t ax[3], const vec3_t spin, float dt ) {
	vec3_t k, v, kxv;
	const float w = VectorLength( spin ), a = w * dt;
	float c, s;
	int i;

	if ( a < 1e-5f ) {
		return;
	}
	VectorScale( spin, 1.0f / w, k );
	c = cosf( a );
	s = sinf( a );
	for ( i = 0; i < 3; i++ ) {
		VectorCopy( ax[i], v );
		CrossProduct( k, v, kxv );
		VectorScale( v, c, ax[i] );
		VectorMA( ax[i], s, kxv, ax[i] );
		VectorMA( ax[i], DotProduct( k, v ) * ( 1.0f - c ), k, ax[i] );
	}
	VRD_Square( ax );
}

// Lying on the ground: its blade level, its face up.
static void VRD_FlatAxes( const vrDrop_t *d, vec3_t from[3], vec3_t out[3] ) {
	static const vec3_t up = { 0.0f, 0.0f, 1.0f };
	vec3_t dir;

	VRD_World( from, d->ownDir, dir );
	dir[2] = 0.0f;
	if ( VectorNormalize( dir ) < 0.1f ) {
		VRD_World( from, d->ownFace, dir );
		dir[2] = 0.0f;
		if ( VectorNormalize( dir ) < 0.1f ) {
			VectorSet( dir, 1.0f, 0.0f, 0.0f );
		}
	}
	VRD_Axes( d->ownDir, d->ownFace, dir, up, out );
}

// Where its hilt is (world): from where its tag holds it, along its blade's
// way, from lo to hi (where hands can hold it).
static void VRD_Hilt( gentity_t *saberent, gentity_t *owner, vrDrop_t *d, vec3_t a, vec3_t b ) {
	vec3_t dir;
	float lo = -VRD_POMMEL, hi = VRD_EMITTER;

	G_VRSaberHiltRange( owner, 0, &lo, &hi );
	VRD_World( d->ax, d->ownDir, dir );
	VectorMA( saberent->r.currentOrigin, lo, dir, a );
	VectorMA( saberent->r.currentOrigin, hi, dir, b );
}

// How far p is from the segment a-b.
static float VRD_SegDist( const vec3_t p, const vec3_t a, const vec3_t b ) {
	vec3_t ab, ap, q;
	const float len2 = DistanceSquared( a, b );
	float t = 0.0f;

	VectorSubtract( b, a, ab );
	VectorSubtract( p, a, ap );
	if ( len2 > 1e-4f ) {
		t = Com_Clamp( 0.0f, 1.0f, DotProduct( ap, ab ) / len2 );
	}
	VectorMA( a, t, ab, q );
	return Distance( p, q );
}

/*
--------------------------------------------------------------------------------
Down, and back
--------------------------------------------------------------------------------
*/

// Where it is (its tag's place) and lies: given to everyone as it is.
static void VRD_Place( gentity_t *saberent, vrDrop_t *d, const vec3_t origin ) {
	vec3_t angles;

	G_VRAxisToAngles( d->ax, angles );
	G_SetOrigin( saberent, (float *)origin );
	saberent->s.pos.trType = TR_INTERPOLATE;
	VectorCopy( angles, saberent->s.apos.trBase );
	VectorClear( saberent->s.apos.trDelta );
	saberent->s.apos.trType = TR_INTERPOLATE;
	saberent->s.apos.trTime = 0;
	VectorCopy( angles, saberent->r.currentAngles );
	VectorCopy( angles, saberent->s.angles );
	trap->LinkEntity( (sharedEntity_t *)saberent );
}

// The saber entity as it goes down: its hilt drawn for everyone where it is.
static void VRD_SetDown( gentity_t *saberent, gentity_t *owner ) {
	gclient_t *cl = owner->client;

	cl->ps.saberEntityNum = 0;	// (still stored in client->saberStoredIndex)
	cl->saberKnockedTime = level.time;
	cl->ps.saberInFlight = qtrue;
	cl->ps.saberEntityState = 1;
	saberent->s.saberInFlight = qfalse;
	saberent->clipmask = MASK_SOLID;
	saberent->r.contents = CONTENTS_TRIGGER;
	VectorSet( saberent->r.mins, -VRD_BOX, -VRD_BOX, -VRD_BOX );
	VectorSet( saberent->r.maxs, VRD_BOX, VRD_BOX, VRD_BOX );
	if ( !saberent->ghoul2 ) {
		WP_SaberAddG2Model( saberent, cl->saber[0].model, cl->saber[0].skin );
	}
	saberent->s.modelGhoul2 = 1;
	saberent->s.g2radius = 20;
	saberent->s.eType = ET_MISSILE;
	saberent->s.weapon = WP_SABER;
	saberent->s.eFlags = 0;
	saberent->s.otherEntityNum2 = owner->s.number + 1;	// (whose: their game picks theirs out - cg_vrsaberdrop.c)
	saberent->s.loopSound = 0;
	saberent->s.loopIsSoundset = qfalse;
	saberent->r.svFlags &= ~SVF_NOCLIENT;
	saberent->parent = owner;
	saberent->touch = NULL;
	saberent->think = DownedSaberThink;
	saberent->nextthink = level.time;
	saberent->genericValue5 = 0;
}

// Theirs again (in hand, unlit, as the stock game puts one back): what their
// game does with it - the hand that took it or called it, else their belt.
static void VRD_Back( gentity_t *saberent, gentity_t *owner, vrDrop_t *d, const char *how ) {
	gclient_t *cl = owner->client;

	cl->ps.saberEntityNum = cl->saberStoredIndex;
	saberReactivate( saberent, owner );
	saberent->touch = SaberGotHit;
	saberent->think = SaberUpdateSelf;
	saberent->genericValue5 = 0;
	saberent->nextthink = level.time;
	saberent->r.svFlags |= SVF_NOCLIENT;
	saberent->r.contents = CONTENTS_LIGHTSABER;
	saberent->clipmask = MASK_PLAYERSOLID | CONTENTS_LIGHTSABER;
	saberent->s.loopSound = 0;
	saberent->s.loopIsSoundset = qfalse;
	saberent->s.saberInFlight = qfalse;
	saberent->s.otherEntityNum2 = 0;
	saberent->flags &= ~FL_BOUNCE_HALF;	// (a knocked one's, the stock game's: not one now)
	WP_SaberRemoveG2Model( saberent );
	cl->ps.saberInFlight = qfalse;
	cl->ps.saberEntityState = 0;
	cl->ps.saberThrowDelay = level.time + 500;
	cl->ps.saberCanThrow = qfalse;
	cl->ps.saberHolstered = 2;	// (unlit: lit by the trigger once in hand)
	d->state = VRD_NONE;
	d->ent = 0;
	if ( g_vrSaberLog.integer ) {
		G_LogPrintf( "VRSaber: back %i \"%s\" %s after %.1f s\n", owner->s.number, cl->pers.netname, how,
			( level.time - d->since ) * 0.001f );
	}
}

// The stock game's way again (no longer ours to say): falling as it goes, as
// its knocked away sabers are (saberKnockDown).
static void VRD_Release( gentity_t *saberent, vrDrop_t *d ) {
	VectorSet( saberent->r.mins, -3.0f, -3.0f, -1.5f );
	VectorSet( saberent->r.maxs, 3.0f, 3.0f, 1.5f );
	saberent->touch = SaberBounceSound;	// (G_RunObject calls it on what it meets)
	saberent->bounceCount = -5;
	saberent->s.pos.trType = TR_GRAVITY;
	VectorCopy( saberent->r.currentOrigin, saberent->s.pos.trBase );
	VectorCopy( d->vel, saberent->s.pos.trDelta );
	saberent->s.pos.trTime = level.time;
	saberent->s.apos.trType = TR_STATIONARY;
	VectorCopy( saberent->r.currentAngles, saberent->s.apos.trBase );
	saberent->s.otherEntityNum2 = 0;
	saberent->flags |= FL_BOUNCE_HALF;
	trap->LinkEntity( (sharedEntity_t *)saberent );
	d->state = VRD_NONE;
	d->ent = 0;
}

// A saber knocked away the stock game's way: ours from now, falling as it
// was, its way round as the games draw it (along its way, turning).
static void VRD_Adopt( gentity_t *saberent, gentity_t *owner, vrDrop_t *d ) {
	vec3_t dir, face;

	d->state = VRD_FALLING;
	d->ent = saberent->s.number;
	d->since = d->moveTime = level.time;
	BG_EvaluateTrajectoryDelta( &saberent->s.pos, level.time, d->vel );
	if ( saberent->s.pos.trType == TR_STATIONARY || saberent->s.pos.trType == TR_INTERPOLATE ) {
		VectorClear( d->vel );
	}
	VRD_ModelFrame( saberent, d->ownDir, d->ownFace );
	VectorCopy( d->vel, dir );
	if ( VectorNormalize( dir ) < 0.5f ) {
		VectorSet( dir, 0.0f, 0.0f, 1.0f );
	}
	PerpendicularVector( face, dir );
	VRD_Axes( d->ownDir, d->ownFace, dir, face, d->ax );
	VectorSet( d->spin, flrand( -6.0f, 6.0f ), flrand( -6.0f, 6.0f ), flrand( -6.0f, 6.0f ) );
	VRD_SetDown( saberent, owner );
	VRD_Place( saberent, d, saberent->r.currentOrigin );
}

/*
--------------------------------------------------------------------------------
What their game asks
--------------------------------------------------------------------------------
*/

// GENCMD_VR_SABERDROP: let go of: down it goes, from their hand as it held it.
void G_VRSaberDropCmd( gentity_t *ent ) {
	gclient_t *cl = ent ? ent->client : NULL;
	vrDrop_t *d;
	gentity_t *saberent;
	vec3_t tag, face, eye, mins, maxs;
	trace_t tr;
	float speed, spin;
	qboolean lit;

	if ( !VRD_Allowed( ent ) || !VRD_Playing( ent ) ) {
		return;
	}
	d = &vrDrops[ent->s.number];
	if ( cl->ps.weapon != WP_SABER || cl->ps.saberInFlight || !cl->ps.saberEntityNum || cl->ps.saberEntityNum != cl->saberStoredIndex ||
		!cl->saber[0].model[0] || cl->ps.saberLockTime >= level.time || cl->ps.duelTime >= level.time ||
		( level.gametype == GT_SIEGE && cl->siegeClass != -1 ) || !d->heldTime || level.time - d->heldTime > VRD_HELD_MS ||
		level.time - d->heldTime < 0 ) {
		return;
	}
	if ( cl->saber[1].model[0] && !( cl->pers.cmd.buttons & BUTTON_VR_ONESABER ) ) {
		return;	// (both sabers in their hands: they go back to their holsters)
	}
	saberent = &g_entities[cl->ps.saberEntityNum];
	if ( !saberent->inuse ) {
		return;
	}
	// out, with its sound
	lit = (qboolean)!BG_SabersOff( &cl->ps );
	cl->ps.saberHolstered = 2;
	if ( lit && cl->saber[0].soundOff ) {
		G_Sound( ent, CHAN_AUTO, cl->saber[0].soundOff );
	}

	// from where their hand held it, the way it held it, as the hand went
	VRD_SetDown( saberent, ent );
	d->state = VRD_FALLING;
	d->ent = saberent->s.number;
	d->since = d->moveTime = level.time;
	VRD_HandMotion( d, d->heldHand, d->vel, d->spin );
	speed = VectorLength( d->vel );
	if ( speed > VRD_MAX_SPEED ) {
		VectorScale( d->vel, VRD_MAX_SPEED / speed, d->vel );
	}
	spin = VectorLength( d->spin );
	if ( spin > VRD_MAX_SPIN ) {
		VectorScale( d->spin, VRD_MAX_SPIN / spin, d->spin );
	}
	VRD_ModelFrame( saberent, d->ownDir, d->ownFace );
	VectorCopy( d->heldUp, face );
	VectorMA( face, -DotProduct( face, d->heldDir ), d->heldDir, face );
	if ( VectorNormalize( face ) < 0.1f ) {
		PerpendicularVector( face, d->heldDir );
	}
	VRD_Axes( d->ownDir, d->ownFace, d->heldDir, face, d->ax );
	VectorMA( d->heldGrip, -d->heldHilt, d->heldDir, tag );

	// (a hand in a wall: from their eyes to it, short of the wall)
	VectorCopy( cl->ps.origin, eye );
	eye[2] += cl->ps.viewheight;
	VectorSet( mins, -VRD_BOX, -VRD_BOX, -VRD_BOX );
	VectorSet( maxs, VRD_BOX, VRD_BOX, VRD_BOX );
	trap->Trace( &tr, eye, mins, maxs, tag, ent->s.number, MASK_SOLID, qfalse, 0, 0 );
	if ( tr.startsolid || tr.allsolid ) {
		VectorCopy( eye, tag );
	} else if ( tr.fraction < 1.0f ) {
		VectorCopy( tr.endpos, tag );
	}
	VRD_Place( saberent, d, tag );
	if ( g_vrSaberLog.integer ) {
		G_LogPrintf( "VRSaber: drop %i \"%s\" %s at %.1f m/s\n", ent->s.number, cl->pers.netname, lit ? "lit" : "unlit",
			VectorLength( d->vel ) / VRD_UNITS_PER_METRE );
	}
}

// Their saber down now (ours: VRD_FALLING, _LYING or _CALLED), the entity.
static gentity_t *VRD_Down( gentity_t *ent, vrDrop_t *d ) {
	gclient_t *cl = ent->client;
	gentity_t *saberent;

	if ( d->state == VRD_NONE || !d->ent || !cl->ps.saberInFlight || cl->ps.saberEntityNum || d->ent != cl->saberStoredIndex ) {
		return NULL;
	}
	saberent = &g_entities[d->ent];
	return saberent->inuse ? saberent : NULL;
}

// GENCMD_VR_SABERTAKE: a hand of theirs at its hilt squeezed it - back in
// that hand (their game's put it there, where it closed on it).
void G_VRSaberTakeCmd( gentity_t *ent ) {
	vrDrop_t *d;
	gentity_t *saberent;
	vrAvatar_t av;
	vec3_t a, b, hand;
	float best = 1e9f;
	int h;

	if ( !VRD_Allowed( ent ) || !VRD_Playing( ent ) ) {
		return;
	}
	d = &vrDrops[ent->s.number];
	if ( !( saberent = VRD_Down( ent, d ) ) || !G_VRSaberAvatar( ent->s.number, &av ) ) {
		return;
	}
	VRD_Hilt( saberent, ent, d, a, b );
	for ( h = 0; h < 2; h++ ) {
		if ( av.hand[h].grip == VR_GRIP_NONE ) {
			continue;
		}
		VectorAdd( ent->client->ps.origin, av.hand[h].origin, hand );
		best = Q_min( best, VRD_SegDist( hand, a, b ) );
	}
	if ( best > VRD_TAKE_REACH ) {
		return;	// (no hand of theirs anywhere near it)
	}
	VRD_Back( saberent, ent, d, "taken up" );
}

// GENCMD_VR_SABERCALL_L, _R: an empty hand pointed at it and squeezed - off it
// goes to that hand.
void G_VRSaberCallCmd( gentity_t *ent, int hand ) {
	vrDrop_t *d;
	gentity_t *saberent;

	if ( !VRD_Allowed( ent ) || !VRD_Playing( ent ) || hand < VR_HAND_LEFT || hand > VR_HAND_RIGHT ) {
		return;
	}
	d = &vrDrops[ent->s.number];
	if ( !( saberent = VRD_Down( ent, d ) ) || d->state == VRD_CALLED ||
		Distance( saberent->r.currentOrigin, ent->client->ps.origin ) > VRD_CALL_RANGE ) {
		return;
	}
	d->state = VRD_CALLED;
	d->callHand = hand;
	d->callTime = level.time;
	d->callSpeed = VRD_CALL_START;
	G_Sound( ent, CHAN_BODY, G_SoundIndex( "sound/weapons/force/pull.wav" ) );
	if ( g_vrSaberLog.integer ) {
		G_LogPrintf( "VRSaber: call %i \"%s\" to the %s hand from %.1f m\n", ent->s.number, ent->client->pers.netname,
			hand == VR_HAND_LEFT ? "left" : "right", Distance( saberent->r.currentOrigin, ent->client->ps.origin ) / VRD_UNITS_PER_METRE );
	}
}

/*
--------------------------------------------------------------------------------
Down: every frame
--------------------------------------------------------------------------------
*/

// Falling (or sliding): its weight, what it meets.
static void VRD_Fall( gentity_t *saberent, vrDrop_t *d, float dt ) {
	vec3_t mins, maxs, end, vn, vt;
	trace_t tr;
	float into;

	VectorSet( mins, -VRD_BOX, -VRD_BOX, -VRD_BOX );
	VectorSet( maxs, VRD_BOX, VRD_BOX, VRD_BOX );
	d->vel[2] -= g_gravity.value * dt;
	VectorMA( saberent->r.currentOrigin, dt, d->vel, end );
	trap->Trace( &tr, saberent->r.currentOrigin, mins, maxs, end, saberent->s.number, MASK_SOLID, qfalse, 0, 0 );
	VRD_Turn( d->ax, d->spin, dt );
	if ( tr.startsolid || tr.allsolid ) {
		// (in something: up out of it)
		VectorClear( d->vel );
		VectorCopy( saberent->r.currentOrigin, end );
		end[2] += 4.0f;
		VRD_Place( saberent, d, end );
		return;
	}
	if ( tr.fraction >= 1.0f ) {
		VRD_Place( saberent, d, end );
		return;
	}
	// off what it met
	into = DotProduct( d->vel, tr.plane.normal );
	VectorScale( tr.plane.normal, into, vn );
	VectorSubtract( d->vel, vn, vt );
	VectorScale( vt, VRD_SLIDE, vt );
	VectorScale( vn, -VRD_BOUNCE, vn );
	VectorAdd( vt, vn, d->vel );
	VectorScale( d->spin, VRD_SPIN_KEEP, d->spin );
	if ( -into > VRD_CLINK_SPEED ) {
		G_Sound( saberent, CHAN_AUTO, vrdClink[rand() & 1] );
	}
	if ( tr.plane.normal[2] > 0.7f && VectorLength( d->vel ) < VRD_REST_SPEED ) {
		int k;
		d->state = VRD_LYING;
		VectorClear( d->vel );
		VectorClear( d->spin );
		d->settleTime = level.time ? level.time : 1;
		for ( k = 0; k < 3; k++ ) {
			VectorCopy( d->ax[k], d->settleFrom[k] );
		}
		VRD_FlatAxes( d, d->ax, d->settleTo );
	}
	VRD_Place( saberent, d, tr.endpos );
}

// Lying there: turning flat as it settles; falling again if the ground goes.
static void VRD_Lie( gentity_t *saberent, vrDrop_t *d ) {
	vec3_t mins, maxs, below;
	trace_t tr;
	float f;
	int k;

	VectorSet( mins, -VRD_BOX, -VRD_BOX, -VRD_BOX );
	VectorSet( maxs, VRD_BOX, VRD_BOX, VRD_BOX );
	VectorCopy( saberent->r.currentOrigin, below );
	below[2] -= 2.0f;
	trap->Trace( &tr, saberent->r.currentOrigin, mins, maxs, below, saberent->s.number, MASK_SOLID, qfalse, 0, 0 );
	if ( tr.fraction >= 1.0f && !tr.startsolid ) {
		d->state = VRD_FALLING;	// (what it lay on gone)
		return;
	}
	if ( !d->settleTime ) {
		return;
	}
	f = Com_Clamp( 0.0f, 1.0f, ( level.time - d->settleTime ) / (float)VRD_SETTLE_MS );
	for ( k = 0; k < 3; k++ ) {
		VectorScale( d->settleFrom[k], 1.0f - f, d->ax[k] );
		VectorMA( d->ax[k], f, d->settleTo[k], d->ax[k] );
	}
	VRD_Square( d->ax );
	if ( f >= 1.0f ) {
		d->settleTime = 0;
	}
	VRD_Place( saberent, d, saberent->r.currentOrigin );
}

// Called: off to the hand that called it, faster as it goes - in it as it
// gets there; meeting a wall on the way (or never getting there), it falls.
static void VRD_Fly( gentity_t *saberent, gentity_t *owner, vrDrop_t *d, float dt ) {
	static const vec3_t up = { 0.0f, 0.0f, 1.0f };
	vec3_t target, to, dir, end, mins, maxs, axis;
	vrAvatar_t av;
	trace_t tr;
	float dist, step;

	if ( G_VRSaberAvatar( owner->s.number, &av ) && av.hand[d->callHand].grip != VR_GRIP_NONE ) {
		VectorAdd( owner->client->ps.origin, av.hand[d->callHand].origin, target );
	} else {
		VectorCopy( owner->client->ps.origin, target );
		target[2] += owner->client->ps.viewheight - 16.0f;
	}
	VectorSubtract( target, saberent->r.currentOrigin, to );
	dist = VectorNormalize2( to, dir );
	d->callSpeed = Q_min( VRD_CALL_SPEED, d->callSpeed + VRD_CALL_ACCEL * dt );
	step = d->callSpeed * dt;
	if ( dist <= VRD_CALL_CATCH + step ) {
		G_Sound( owner, CHAN_AUTO, G_SoundIndex( "sound/weapons/saber/saber_catch.wav" ) );
		VRD_Back( saberent, owner, d, "called" );
		return;
	}
	VectorSet( mins, -VRD_BOX, -VRD_BOX, -VRD_BOX );
	VectorSet( maxs, VRD_BOX, VRD_BOX, VRD_BOX );
	VectorMA( saberent->r.currentOrigin, step, dir, end );
	trap->Trace( &tr, saberent->r.currentOrigin, mins, maxs, end, saberent->s.number, MASK_SOLID, qfalse, 0, 0 );
	VectorScale( dir, d->callSpeed, d->vel );
	// (end over end on the way)
	CrossProduct( dir, up, axis );
	if ( VectorNormalize( axis ) < 0.1f ) {
		PerpendicularVector( axis, dir );
	}
	VectorScale( axis, VRD_CALL_SPIN, d->spin );
	VRD_Turn( d->ax, d->spin, dt );
	if ( tr.startsolid || tr.allsolid || tr.fraction < 1.0f || level.time - d->callTime > VRD_CALL_MS ) {
		VectorScale( d->vel, 0.25f, d->vel );	// (it met something: it falls from there)
		d->state = VRD_FALLING;
		VRD_Place( saberent, d, ( tr.startsolid || tr.allsolid ) ? saberent->r.currentOrigin : tr.endpos );
		return;
	}
	VRD_Place( saberent, d, end );
}

// Whether a missile is a VR player's saber down that this moves (G_RunMissile:
// its think moves it, not a missile's path - nor does what it meets touch it).
qboolean G_VRSaberDownRun( gentity_t *ent ) {
	int owner;

	if ( !ent || ent->s.weapon != WP_SABER || !ent->neverFree || ent->s.otherEntityNum2 <= 0 ) {
		return qfalse;
	}
	owner = ent->s.otherEntityNum2 - 1;
	if ( owner < 0 || owner >= MAX_CLIENTS ) {
		return qfalse;
	}
	return (qboolean)( vrDrops[owner].state != VRD_NONE && vrDrops[owner].ent == ent->s.number );
}

// DownedSaberThink's (w_saber.c: its owner still there, alive, with it - else
// as the stock game has it): a VR player's saber down, ours to move - qfalse:
// not ours (the stock game's way: the trigger calls it back).
qboolean G_VRSaberDownThink( gentity_t *saberent, gentity_t *owner ) {
	vrDrop_t *d;
	float dt;

	if ( !owner || !owner->client || owner->s.number < 0 || owner->s.number >= MAX_CLIENTS ) {
		return qfalse;
	}
	d = &vrDrops[owner->s.number];
	if ( !VRD_Allowed( owner ) ) {
		if ( d->state != VRD_NONE && d->ent == saberent->s.number ) {
			VRD_Release( saberent, d );
		}
		return qfalse;
	}
	if ( d->state == VRD_NONE || d->ent != saberent->s.number ) {
		VRD_Adopt( saberent, owner, d );
	}
	saberent->nextthink = level.time;
	dt = Com_Clamp( 0.0f, 0.1f, ( level.time - d->moveTime ) * 0.001f );
	d->moveTime = level.time;
	if ( d->state != VRD_CALLED && level.time - d->since >= VRD_BELT_MS ) {
		VRD_Back( saberent, owner, d, "left lying" );
		return qtrue;
	}
	switch ( d->state ) {
	case VRD_FALLING:
		VRD_Fall( saberent, d, dt );
		break;
	case VRD_LYING:
		VRD_Lie( saberent, d );
		break;
	case VRD_CALLED:
		VRD_Fly( saberent, owner, d, dt );
		break;
	default:
		break;
	}
	return qtrue;
}
