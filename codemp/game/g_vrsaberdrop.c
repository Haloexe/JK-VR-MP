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
- Let go of lit mid-swing - the hand swinging VRD_THROW_SPEED or faster in
  their room (walking, jumping or turning doesn't swing it) - with Saber
  Throw to use and the Force for it (g_vrSaberThrow on): thrown. Off from
  where the hand held it, the way the hand swung, faster the faster it
  swung (VRD_THROW_SCALE times, VRD_THROW_MIN to _MAX), spinning, lit: the
  game's thrown saber (its cuts on whoever it passes, what blocks it, what
  it meets - w_saber.c), flying our way. Straight out, as far as their
  Saber Throw takes it (vrdThrowRange - nearer, thrown softly), then round
  and back like a boomerang - slowing as it turns, faster facing home -
  into the hand it left: caught, lit. Meeting something on the way out it
  comes back from there; it comes back through anything (as the game's).
  Called meanwhile (the hand it left squeezed - GENCMD_VR_SABERCALL_L, _R):
  back now. A blade it meets knocks it out of the air (w_saber.c,
  g_vrsaber.c G_VRSaberStrikesThrown): down, theirs to take up or call.
- A VR player's saber knocked away the stock game's way (out of the air,
  thrown) is theirs to take up or call the same way, in place of the
  trigger calling it back. Not theirs to (no longer in VR with holsters):
  as the stock game has it.
- Who took it up, called it, let go of it or threw it, and how fast:
  g_vrSaberLog.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "g_local.h"
#include "w_saber.h"

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

#define VRD_THROW_SPEED		85.0f	// units a second (2.5 m/s): let go of lit, the hand swinging this fast in their room, it's thrown...
#define VRD_THROW_SCALE		3.0f	// ...off at this many times the hand's speed...
#define VRD_THROW_MIN		450.0f	// ...no slower than this...
#define VRD_THROW_MAX		900.0f	// ...nor faster than this
#define VRD_THROW_OUT_MS	1000	// out this long at most - nor further than their Saber Throw takes it:
static const float vrdThrowRange[NUM_FORCE_POWER_LEVELS] = { 0.0f, 384.0f, 576.0f, 768.0f };
#define VRD_THROW_TURN		8.0f	// back: turning toward the hand this fast (radians a second)...
#define VRD_THROW_HOME		96.0f	// ...faster nearer than this (half as far: twice as fast - it never circles the hand)
#define VRD_THROW_APEX		250.0f	// facing away from the hand it slows to this...
#define VRD_THROW_BACK		700.0f	// ...facing home it comes this fast (as the game's saber comes back)...
#define VRD_THROW_BACK3		900.0f	// ...or this, Saber Throw 3 (as the game's too)
#define VRD_THROW_ACCEL		3000.0f	// (faster or slower by this each second)
#define VRD_THROW_CATCH		16.0f	// in the hand this near it
#define VRD_THROW_MAX_MS	8000	// out this long all told: in the hand all the same
#define VRD_THROW_DAMAGE	30		// a cut on whoever it passes (as the game's thrown saber's: SABER_THROWN_HIT_DAMAGE)

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
	vec3_t		histRoom[VRD_HIST][2];	// ...off where they stood, their room's way round (walking, a turn doesn't move it)
	float		histYaw[VRD_HIST];		// (their room's turn then: the pose's trackYaw)
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

// A VR player's saber thrown (let go of lit mid-swing): flown our way - the
// game's thrown saber all else (w_saber.c saberFirstThrown, saberBackToOwner
// ask G_VRSaberThrownOut, _Back first).
typedef struct {
	int			ent;			// the saber out (0: none)
	int			time;			// level.time it was thrown...
	int			hand;			// ...from this hand (VR_HAND_*): back to it
	int			rank;			// their Saber Throw then
	vec3_t		from;			// where it left the hand
	float		range;			// out no further than this
	qboolean	back;			// on its way back
	int			moveTime;		// level.time it last moved
	vec3_t		vel;			// units a second
} vrThrow_t;

static vrThrow_t	vrThrows[MAX_CLIENTS];

void saberReactivate( gentity_t *saberent, gentity_t *saberOwner );	// (w_saber.c)
void SaberUpdateSelf( gentity_t *ent );
void SaberGotHit( gentity_t *self, gentity_t *other, trace_t *trace );
void SaberBounceSound( gentity_t *self, gentity_t *other, trace_t *trace );
void DownedSaberThink( gentity_t *saberent );
void WP_SaberAddG2Model( gentity_t *saberent, const char *saberModel, qhandle_t saberSkin );
void WP_SaberRemoveG2Model( gentity_t *saberent );
void saberFirstThrown( gentity_t *saberent );
void saberBackToOwner( gentity_t *saberent );
void thrownSaberTouch( gentity_t *saberent, gentity_t *other, trace_t *trace );
extern int saberSpinSound;
qboolean BG_SabersOff( playerState_t *ps );		// (bg_pmove.c)

/*
--------------------------------------------------------------------------------
On or off
--------------------------------------------------------------------------------
*/

void G_VRSaberDropInit( void ) {
	memset( vrDrops, 0, sizeof( vrDrops ) );
	memset( vrThrows, 0, sizeof( vrThrows ) );
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
		memset( &vrThrows[clientNum], 0, sizeof( vrThrows[clientNum] ) );
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

// The avatar that came with a command (G_VRSaberUsercmd, after it's taken;
// trackYaw: its pose's): where each hand is (world, and off where they stand
// in their room), and the saber in one.
void G_VRSaberDropUsercmd( gentity_t *ent, const usercmd_t *ucmd, float trackYaw ) {
	const int n = ent ? ent->s.number : -1;
	vrDrop_t *d;
	vrAvatar_t a;
	float c, s;
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
	if ( vrThrows[n].ent && ( !ent->client->ps.saberInFlight || ent->client->ps.saberEntityNum != vrThrows[n].ent ) ) {
		vrThrows[n].ent = 0;	// (no longer out as we threw it: back, knocked down, they died...)
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
	d->histYaw[k] = ( trackYaw == trackYaw ) ? trackYaw : 0.0f;	// (NaN)
	c = cosf( DEG2RAD( -d->histYaw[k] ) );
	s = sinf( DEG2RAD( -d->histYaw[k] ) );
	for ( h = 0; h < 2; h++ ) {
		const vrAvatarHand_t *hand = &a.hand[h];
		d->histValid[k][h] = (qboolean)( hand->grip != VR_GRIP_NONE );
		d->histSaber[k][h] = (qboolean)( hand->grip == VR_GRIP_SABER && hand->saber == 0 );
		VectorAdd( ent->client->ps.origin, hand->origin, d->histPos[k][h] );
		d->histRoom[k][h][0] = hand->origin[0] * c - hand->origin[1] * s;	// (their room's way round)
		d->histRoom[k][h][1] = hand->origin[0] * s + hand->origin[1] * c;
		d->histRoom[k][h][2] = hand->origin[2];
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

// Hand h's place about VRD_VEL_MS before its latest (the history's slot; -1:
// the hand not there that long, nor 8 ms).
static int VRD_HandSince( const vrDrop_t *d, int h ) {
	const int k1 = d->histHead;
	int i, k0 = -1;

	if ( d->histCount < 2 || !d->histValid[k1][h] ) {
		return -1;
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
	return ( k0 >= 0 && d->histTime[k1] - d->histTime[k0] >= 8 ) ? k0 : -1;
}

// How hand h went lately (world, units a second): over about VRD_VEL_MS to its
// latest place - and how the saber in it turned (radians a second about a
// world axis), from the last two places it held it.
static void VRD_HandMotion( const vrDrop_t *d, int h, vec3_t vel, vec3_t spin ) {
	const int k1 = d->histHead, k0 = VRD_HandSince( d, h );
	int i, s1 = -1, s0 = -1;

	VectorClear( vel );
	VectorClear( spin );
	if ( d->histCount < 2 || !d->histValid[k1][h] ) {
		return;
	}
	if ( k0 >= 0 ) {
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

// How hand h swung lately (units a second, the world's way round as their
// room's turned now): over about VRD_VEL_MS, as it moved off where they stand
// in their room - walking, jumping, falling or turning doesn't swing it.
static void VRD_HandSwing( const vrDrop_t *d, int h, vec3_t vel ) {
	const int k1 = d->histHead, k0 = VRD_HandSince( d, h );
	vec3_t room;
	float c, s;

	VectorClear( vel );
	if ( k0 < 0 ) {
		return;
	}
	VectorSubtract( d->histRoom[k1][h], d->histRoom[k0][h], room );
	VectorScale( room, 1000.0f / ( d->histTime[k1] - d->histTime[k0] ), room );
	c = cosf( DEG2RAD( d->histYaw[k1] ) );
	s = sinf( DEG2RAD( d->histYaw[k1] ) );
	vel[0] = room[0] * c - room[1] * s;
	vel[1] = room[0] * s + room[1] * c;
	vel[2] = room[2];
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
Thrown
--------------------------------------------------------------------------------
*/

// Where its tag goes as it leaves their hand: where the hand held it - a hand
// in a wall: from their eyes to it, short of the wall.
static void VRD_LetGoPlace( gentity_t *ent, const vrDrop_t *d, vec3_t tag ) {
	gclient_t *cl = ent->client;
	vec3_t eye, mins, maxs;
	trace_t tr;

	VectorMA( d->heldGrip, -d->heldHilt, d->heldDir, tag );
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
}

// Whether their saber, let go of lit mid-swing, is thrown: the server throws
// them (g_vrSaberThrow), their saber can be (the game's say: not a staff's two
// blades lit, say; nor straight after a throw), they have Saber Throw and can
// use it now (no ysalamiri), and the Force for it.
static qboolean VRD_CanThrow( gentity_t *ent ) {
	gclient_t *cl = ent->client;
	const int rank = cl->ps.fd.forcePowerLevel[FP_SABERTHROW];

	return (qboolean)( g_vrSaberThrow.integer > 0 && rank > FORCE_LEVEL_0 && rank < NUM_FORCE_POWER_LEVELS && cl->ps.saberCanThrow &&
		BG_CanUseFPNow( level.gametype, &cl->ps, level.time, FP_SABERTHROW ) && cl->ps.fd.forcePower >= forcePowerNeeded[rank][FP_SABERTHROW] );
}

// Let go of lit mid-swing: thrown - off from where the hand held it, the way
// the hand swung, spinning: the game's thrown saber, flown our way
// (G_VRSaberThrownOut, _Back).
static void VRD_Throw( gentity_t *ent, gentity_t *saberent, vrDrop_t *d, const vec3_t swing, float swingSpeed ) {
	gclient_t *cl = ent->client;
	vrThrow_t *t = &vrThrows[ent->s.number];
	const int rank = Com_Clampi( FORCE_LEVEL_1, FORCE_LEVEL_3, cl->ps.fd.forcePowerLevel[FP_SABERTHROW] );
	const float speed = Com_Clamp( VRD_THROW_MIN, VRD_THROW_MAX, swingSpeed * VRD_THROW_SCALE );
	vec3_t dir, tag, angles;

	VectorScale( swing, 1.0f / swingSpeed, dir );
	VRD_LetGoPlace( ent, d, tag );
	vectoangles( d->heldDir, angles );

	cl->ps.fd.forcePower = Q_max( 0, cl->ps.fd.forcePower - forcePowerNeeded[rank][FP_SABERTHROW] );
	cl->ps.saberInFlight = qtrue;
	cl->ps.saberEntityState = 1;	// (out: WP_SaberPositionUpdate doesn't throw it again)
	cl->ps.saberDidThrowTime = level.time;
	cl->dangerTime = level.time;
	cl->ps.eFlags &= ~EF_INVULNERABLE;
	cl->invulnerableTimer = 0;

	// the game's thrown saber (as WP_SaberPositionUpdate throws it)
	saberent->r.svFlags &= ~SVF_NOCLIENT;
	G_SetOrigin( saberent, tag );
	VectorCopy( tag, saberent->s.origin );
	saberent->s.pos.trType = TR_LINEAR;
	VectorScale( dir, speed, saberent->s.pos.trDelta );
	saberent->s.pos.trTime = level.time;
	saberent->s.apos.trType = TR_LINEAR;
	VectorCopy( angles, saberent->s.apos.trBase );
	VectorSet( saberent->s.apos.trDelta, 0.0f, 800.0f, 0.0f );
	saberent->s.apos.trTime = level.time;
	VectorCopy( angles, saberent->s.angles );
	VectorCopy( angles, saberent->r.currentAngles );
	saberent->s.saberInFlight = qtrue;	// (lit, spinning)
	saberent->s.eType = ET_GENERAL;
	saberent->s.eFlags = 0;
	saberent->s.otherEntityNum2 = 0;
	WP_SaberAddG2Model( saberent, cl->saber[0].model, cl->saber[0].skin );
	saberent->s.modelGhoul2 = 127;	// (its owner's drawn with it: cg_players.c)
	saberent->parent = ent;
	saberent->think = saberFirstThrown;
	saberent->nextthink = level.time;
	saberent->damage = VRD_THROW_DAMAGE;
	saberent->methodOfDeath = MOD_SABER;
	saberent->splashMethodOfDeath = MOD_SABER;
	saberent->s.solid = 2;
	saberent->r.contents = CONTENTS_LIGHTSABER;
	saberent->clipmask = MASK_PLAYERSOLID | CONTENTS_LIGHTSABER;
	saberent->genericValue5 = 0;
	VectorSet( saberent->r.mins, SABERMINS_X, SABERMINS_Y, SABERMINS_Z );
	VectorSet( saberent->r.maxs, SABERMAXS_X, SABERMAXS_Y, SABERMAXS_Z );
	saberent->s.genericenemyindex = ent->s.number + 1024;
	saberent->touch = thrownSaberTouch;
	saberent->s.weapon = WP_SABER;
	saberent->s.loopSound = cl->saber[0].spinSound ? cl->saber[0].spinSound : saberSpinSound;
	saberent->s.loopIsSoundset = qfalse;
	trap->LinkEntity( (sharedEntity_t *)saberent );

	memset( t, 0, sizeof( *t ) );
	t->ent = saberent->s.number;
	t->time = t->moveTime = level.time;
	t->hand = d->heldHand;
	t->rank = rank;
	VectorCopy( tag, t->from );
	VectorScale( dir, speed, t->vel );
	t->range = Q_min( vrdThrowRange[rank], speed * VRD_THROW_OUT_MS * 0.001f );
	d->heldTime = 0;	// (gone from the hand)
	if ( g_vrSaberLog.integer ) {
		G_LogPrintf( "VRSaber: throw %i \"%s\" from the %s hand, swung at %.1f m/s - off at %.1f m/s, out %.1f m at most (Saber Throw %i)\n",
			ent->s.number, cl->pers.netname, t->hand == VR_HAND_LEFT ? "left" : "right", swingSpeed / VRD_UNITS_PER_METRE,
			speed / VRD_UNITS_PER_METRE, t->range / VRD_UNITS_PER_METRE, rank );
	}
}

// The throw of owner's that this saber is, out now (NULL: none - the game's
// own throw, say).
static vrThrow_t *VRD_Thrown( gentity_t *saberent, gentity_t *owner ) {
	vrThrow_t *t;

	if ( !saberent || !owner || !owner->client || owner->s.number < 0 || owner->s.number >= MAX_CLIENTS ) {
		return NULL;
	}
	t = &vrThrows[owner->s.number];
	return ( t->ent && t->ent == saberent->s.number && owner->client->ps.saberInFlight ) ? t : NULL;
}

// Whether this thrown saber came from VR hands - a throw of ours, or the
// game's own throw by a player in VR (their avatar handed over lately; no
// lit saber in their VR hands now, so G_VRSaberClient won't say): any blade
// it meets knocks it out of the air (w_saber.c).
qboolean G_VRSaberThrownFromVR( gentity_t *saberent, gentity_t *owner ) {
	vrAvatar_t av;

	if ( VRD_Thrown( saberent, owner ) ) {
		return qtrue;
	}
	return (qboolean)( owner && owner->client && owner->s.number >= 0 && owner->s.number < MAX_CLIENTS && !( owner->r.svFlags & SVF_BOT ) &&
		G_VRSaberAvatar( owner->s.number, &av ) );
}

// The time since it last moved (s: no more than a tenth).
static float VRD_ThrowStep( vrThrow_t *t ) {
	const float dt = Com_Clamp( 0.0f, 0.1f, ( level.time - t->moveTime ) * 0.001f );

	t->moveTime = level.time;
	return dt;
}

// Where it is now: given to everyone going on as it goes (the game's
// thrown saber's way: TR_LINEAR, from here now).
static void VRD_ThrownPlace( gentity_t *saberent, const vrThrow_t *t, const vec3_t origin ) {
	VectorCopy( origin, saberent->r.currentOrigin );
	VectorCopy( origin, saberent->s.pos.trBase );
	VectorCopy( t->vel, saberent->s.pos.trDelta );
	saberent->s.pos.trType = TR_LINEAR;
	saberent->s.pos.trTime = level.time;
	trap->LinkEntity( (sharedEntity_t *)saberent );
}

// Back from here, going on as it goes: round, like a boomerang.
static void VRD_TurnBack( gentity_t *saberent, vrThrow_t *t ) {
	t->back = qtrue;
	saberent->think = saberBackToOwner;
	saberent->nextthink = level.time;
}

// Where it comes back to: the hand it left (where it holds - else their
// eyes, a little low).
static void VRD_ThrowTarget( gentity_t *owner, const vrThrow_t *t, vec3_t target ) {
	vrAvatar_t av;

	if ( G_VRSaberAvatar( owner->s.number, &av ) && av.hand[t->hand].grip != VR_GRIP_NONE ) {
		VectorAdd( owner->client->ps.origin, av.hand[t->hand].origin, target );
	} else {
		VectorCopy( owner->client->ps.origin, target );
		target[2] += owner->client->ps.viewheight - 16.0f;
	}
}

// Unit v turned toward unit 'to' by at most 'turn' radians - going straight
// away, round level (a right hand's throw to the left, a left hand's to the
// right, as a boomerang curves).
static void VRD_TurnToward( vec3_t v, const vec3_t to, float turn, int hand ) {
	static const vec3_t up = { 0.0f, 0.0f, 1.0f };
	const float ang = acosf( Com_Clamp( -1.0f, 1.0f, DotProduct( v, to ) ) );
	vec3_t axis, out;

	if ( ang <= turn ) {
		VectorCopy( to, v );
		return;
	}
	CrossProduct( v, to, axis );
	if ( VectorNormalize( axis ) < 1e-3f ) {
		if ( fabsf( DotProduct( v, up ) ) > 0.9f ) {
			PerpendicularVector( axis, v );
		} else {
			VectorCopy( up, axis );
		}
		if ( hand == VR_HAND_LEFT ) {
			VectorNegate( axis, axis );
		}
	}
	RotatePointAroundVector( out, axis, v, RAD2DEG( turn ) );
	if ( VectorNormalize2( out, v ) < 0.5f ) {
		VectorCopy( to, v );
	}
}

// Caught: in the hand it left, lit (the game's catch; their game puts it in
// that hand - it never left it there: cg_vrholster.c).
static void VRD_Catch( gentity_t *saberent, gentity_t *owner, vrThrow_t *t ) {
	gclient_t *cl = owner->client;

	G_Sound( saberent, CHAN_AUTO, G_SoundIndex( "sound/weapons/saber/saber_catch.wav" ) );
	cl->ps.saberInFlight = qfalse;
	cl->ps.saberEntityState = 0;
	cl->ps.saberEntityNum = saberent->s.number;
	cl->ps.saberCanThrow = qfalse;
	cl->ps.saberThrowDelay = level.time + 300;
	saberent->touch = SaberGotHit;
	saberent->think = SaberUpdateSelf;
	saberent->genericValue5 = 0;
	saberent->nextthink = level.time + 50;
	saberent->s.saberInFlight = qfalse;
	saberent->s.loopSound = 0;
	saberent->s.loopIsSoundset = qfalse;
	saberent->r.svFlags |= SVF_NOCLIENT;
	saberent->r.contents = CONTENTS_LIGHTSABER;
	saberent->clipmask = MASK_PLAYERSOLID | CONTENTS_LIGHTSABER;
	WP_SaberRemoveG2Model( saberent );
	if ( g_vrSaberLog.integer ) {
		G_LogPrintf( "VRSaber: caught %i \"%s\" in the %s hand after %.1f s\n", owner->s.number, cl->pers.netname,
			t->hand == VR_HAND_LEFT ? "left" : "right", ( level.time - t->time ) * 0.001f );
	}
	t->ent = 0;
}

// A VR player's throw on its way out (saberFirstThrown, its owner there and
// alive): straight on as it was thrown, stopped by what it meets (cut or
// blocked, as the game's - and back from there: thrownSaberTouch); back from
// as far as it goes (vrdThrowRange by their Saber Throw - nearer, thrown
// softly: no more than VRD_THROW_OUT_MS on), or as their Saber Throw can't
// be used. -1: not one (the game's own throw); else the game's cuts on
// whoever it passes, as it's going (saberCheckRadiusDamage's 'returning': 0
// out, 2 back lit) - or -2: nothing more this frame (it met something).
int G_VRSaberThrownOut( gentity_t *saberent, gentity_t *owner ) {
	vrThrow_t *t = VRD_Thrown( saberent, owner );
	vec3_t end;
	trace_t tr;
	float dt;

	if ( !t ) {
		return -1;
	}
	if ( t->back ) {
		saberent->think = saberBackToOwner;	// (called back meanwhile)
		return G_VRSaberThrownBack( saberent, owner );
	}
	saberent->nextthink = level.time;
	if ( Distance( saberent->r.currentOrigin, t->from ) >= t->range || !BG_CanUseFPNow( level.gametype, &owner->client->ps, level.time, FP_SABERTHROW ) ) {
		VRD_TurnBack( saberent, t );
		return G_VRSaberThrownBack( saberent, owner );
	}
	dt = VRD_ThrowStep( t );
	VectorMA( saberent->r.currentOrigin, dt, t->vel, end );
	trap->Trace( &tr, saberent->r.currentOrigin, saberent->r.mins, saberent->r.maxs, end, owner->s.number, MASK_PLAYERSOLID, qfalse, 0, 0 );
	if ( tr.startsolid || tr.allsolid || tr.fraction < 1.0f ) {
		const int hit = ( tr.entityNum >= 0 && tr.entityNum < ENTITYNUM_WORLD ) ? tr.entityNum : ENTITYNUM_WORLD;
		if ( !tr.startsolid && !tr.allsolid ) {
			VRD_ThrownPlace( saberent, t, tr.endpos );
		}
		thrownSaberTouch( saberent, &g_entities[hit], &tr );	// (stopped there; what it met cut, or it blocked - back from there)
		return -2;
	}
	VRD_ThrownPlace( saberent, t, end );
	return 0;
}

// A VR player's throw on its way back (saberBackToOwner, its owner there and
// alive): turning toward the hand it left (VRD_THROW_TURN, faster near it) -
// slowing as it faces away, faster facing home - and into it as it gets
// there (caught); back through anything in its way, as the game's saber
// comes back (who it passes is cut). As G_VRSaberThrownOut.
int G_VRSaberThrownBack( gentity_t *saberent, gentity_t *owner ) {
	vrThrow_t *t = VRD_Thrown( saberent, owner );
	vec3_t target, want, heading, end;
	float dt, dist, speed, facing, wantSpeed, turn, back;

	if ( !t ) {
		return -1;
	}
	saberent->nextthink = level.time;
	dt = VRD_ThrowStep( t );
	if ( !t->back ) {
		t->back = qtrue;	// (it met something - thrownSaberTouch: back from a stop there, as the game's)
		VectorClear( t->vel );
	}
	owner->client->ps.saberEntityNum = saberent->s.number;
	saberent->r.contents = CONTENTS_LIGHTSABER;
	VRD_ThrowTarget( owner, t, target );
	VectorSubtract( target, saberent->r.currentOrigin, want );
	dist = VectorNormalize( want );
	speed = VectorLength( t->vel );
	if ( speed > 1e-3f ) {
		VectorScale( t->vel, 1.0f / speed, heading );
	} else {
		VectorCopy( want, heading );
	}
	if ( dist < 1e-3f ) {
		VectorCopy( heading, want );
	}
	// into the hand as it gets there (or out too long, anyway)
	if ( dist <= VRD_THROW_CATCH || ( DotProduct( heading, want ) > 0.5f && dist <= VRD_THROW_CATCH + speed * dt ) ||
		level.time - t->time >= VRD_THROW_MAX_MS ) {
		VRD_Catch( saberent, owner, t );
		return -2;
	}
	// turning toward it - faster near it: it never circles it
	turn = VRD_THROW_TURN * dt;
	if ( dist < VRD_THROW_HOME ) {
		turn *= VRD_THROW_HOME / Q_max( dist, 1.0f );
	}
	VRD_TurnToward( heading, want, turn, t->hand );
	// slowing facing away, faster facing home - a little slower right by the hand, into it
	back = ( t->rank >= FORCE_LEVEL_3 ) ? VRD_THROW_BACK3 : VRD_THROW_BACK;
	facing = DotProduct( heading, want );
	wantSpeed = ( facing > 0.0f ) ? VRD_THROW_APEX + ( back - VRD_THROW_APEX ) * facing : VRD_THROW_APEX;
	if ( dist < 64.0f ) {
		wantSpeed = Q_min( wantSpeed, back - 200.0f );
	} else if ( dist < 128.0f ) {
		wantSpeed = Q_min( wantSpeed, back - 150.0f );
	}
	if ( speed < wantSpeed ) {
		speed = Q_min( wantSpeed, speed + VRD_THROW_ACCEL * dt );
	} else {
		speed = Q_max( wantSpeed, speed - VRD_THROW_ACCEL * dt );
	}
	VectorScale( heading, speed, t->vel );
	VectorMA( saberent->r.currentOrigin, dt, t->vel, end );
	VRD_ThrownPlace( saberent, t, end );
	return 2;	// (lit, spinning: it cuts on its way back as on its way out)
}

/*
--------------------------------------------------------------------------------
What their game asks
--------------------------------------------------------------------------------
*/

// GENCMD_VR_SABERDROP: let go of: down it goes, from their hand as it held it
// - or let go of lit mid-swing: thrown.
void G_VRSaberDropCmd( gentity_t *ent ) {
	gclient_t *cl = ent ? ent->client : NULL;
	vrDrop_t *d;
	gentity_t *saberent;
	vec3_t tag, face, swing;
	float speed, spin, swingSpeed;
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
	lit = (qboolean)!BG_SabersOff( &cl->ps );
	VRD_HandSwing( d, d->heldHand, swing );
	swingSpeed = VectorLength( swing );
	if ( lit && swingSpeed >= VRD_THROW_SPEED && VRD_CanThrow( ent ) ) {
		VRD_Throw( ent, saberent, d, swing, swingSpeed );	// (lit, mid-swing)
		return;
	}
	// out, with its sound
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
	VRD_LetGoPlace( ent, d, tag );
	VRD_Place( saberent, d, tag );
	if ( g_vrSaberLog.integer ) {
		G_LogPrintf( "VRSaber: drop %i \"%s\" %s at %.1f m/s (swung at %.1f)\n", ent->s.number, cl->pers.netname, lit ? "lit" : "unlit",
			VectorLength( d->vel ) / VRD_UNITS_PER_METRE, swingSpeed / VRD_UNITS_PER_METRE );
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
// goes to that hand. Thrown, still going out (the hand it left squeezed):
// back now.
void G_VRSaberCallCmd( gentity_t *ent, int hand ) {
	vrDrop_t *d;
	vrThrow_t *t;
	gentity_t *saberent;

	if ( !VRD_Allowed( ent ) || !VRD_Playing( ent ) || hand < VR_HAND_LEFT || hand > VR_HAND_RIGHT ) {
		return;
	}
	t = &vrThrows[ent->s.number];
	if ( t->ent && ( saberent = &g_entities[t->ent] )->inuse && VRD_Thrown( saberent, ent ) ) {
		if ( !t->back && saberent->think == saberFirstThrown ) {
			VRD_TurnBack( saberent, t );
			G_Sound( ent, CHAN_BODY, G_SoundIndex( "sound/weapons/force/pull.wav" ) );
			if ( g_vrSaberLog.integer ) {
				G_LogPrintf( "VRSaber: recall %i \"%s\" after %.1f s, %.1f m out\n", ent->s.number, ent->client->pers.netname,
					( level.time - t->time ) * 0.001f, Distance( saberent->r.currentOrigin, t->from ) / VRD_UNITS_PER_METRE );
			}
		}
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
