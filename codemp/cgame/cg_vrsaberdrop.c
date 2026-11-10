/*
===========================================================================
OpenJK VR (multiplayer) - your saber let go of: dropped, taken up again,
called (the server's side: g_vrsaberdrop.c).

On a server that drops them ("vrSaberDrop 1" in its serverinfo), with your
holsters in play:

- Holding the grip (Saber Grip: Hold), let go of your saber anywhere but at
  your side (cg_vrholster.c, CG_VRH_AtSide: about a hip's holster - or at
  the holster it's kept in, or an empty one it would go in) and it drops (GENCMD_VR_SABERDROP) - your first saber, alone in your hands (one,
  or a staff: of two in hand, either goes back to its holster as before),
  never a siege class's, nor in a saber lock or a duel's countdown. It stays
  in your hand till the server has it down (VRSD_DROP_MS at most: then, as
  before, back to its holster); down, your hand's empty.
- Down (dropped, or knocked away: the server's saber has no entity in your
  hands, ps.saberInFlight), its hilt lies there for everyone - the entity
  the server says is yours (entityState otherEntityNum2: your client number
  and one). An empty hand of yours:
    at its hilt (VRSD_TAKE_REACH from it), a squeeze takes it up
    (GENCMD_VR_SABERTAKE) - a firm tick as the hand gets there;
    pointed at it (the controller's line, or the one from your eyes through
    the hand, within VRSD_CALL_AIM degrees of its middle - it lights up a
    little), a squeeze calls it (GENCMD_VR_SABERCALL_L, _R) - a light tick
    as the hand comes onto it.
  Those squeezes are grabs (the engine's: VR_SetGrabZones), not Use nor the
  Force. Not a hand holding something, nor one a holster, your other
  hand's hilt or your gun has first.
- Back (the server's saber in hand again): in the hand that took it up -
  where it closed on its hilt, the way round it closed on it - or in the
  hand that called it (lands there: the usual way round), if that's still
  empty; else on your belt (left lying, it came back by itself). Unlit,
  either way: its trigger lights it.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "cg_local.h"

#define VRSD_DROP_MS		600		// let go of: in your hand till the server has it down, this long at most
#define VRSD_TAKE_MS		1000	// taken up: back in that hand if the server has it back within this long...
#define VRSD_CALL_MS		6000	// ...called: in that hand if it's back within this long
#define VRSD_TAKE_REACH		0.12f	// m: a hand this near its hilt takes it up
#define VRSD_CALL_AIM		12.0f	// degrees: pointed this near its middle calls it...
#define VRSD_CALL_NEAR		0.3f	// m: ...from no nearer than this (nearer, the hand takes it)...
#define VRSD_CALL_RANGE		120.0f	// m: ...nor further than this (as the server takes a call)

enum {
	VRSD_ZONE_NONE,
	VRSD_ZONE_TAKE,		// at its hilt: a squeeze takes it up
	VRSD_ZONE_CALL		// pointed at it: a squeeze calls it
};

static struct {
	int			frameTime;
	qboolean	down;				// down as last seen (CG_VR_SaberAway)
	int			ent;				// its entity (-1: not seen)
	vec3_t		hiltA, hiltB;		// its hilt (world), pommel end to emitter end...
	vec3_t		tag, blade;			// ...from where its tag holds it, along its blade
	float		lo, hi;
	qboolean	hilt;				// (worked out this frame)
	int			zone[2];			// VRSD_ZONE_* for each hand...
	int			zones;				// ...offered the engine (bit per hand)
	int			zoneTime;
	qboolean	grabWas[2];
	int			dropTime, dropHand;	// let go of (GENCMD_VR_SABERDROP asked): cg.time, the hand
	int			takeTime, takeHand;	// taken up (GENCMD_VR_SABERTAKE asked)...
	float		takeT, takeSign;	// ...where the hand closed on it, the way round
	int			callTime, callHand;	// called (GENCMD_VR_SABERCALL_*)
} vrsd;

/*
--------------------------------------------------------------------------------
When
--------------------------------------------------------------------------------
*/

// Whether this server drops sabers let go of, takes them up and calls them -
// and they're yours to (holsters in play, in VR, this engine).
qboolean CG_VR_SaberDropServed( void ) {
	return (qboolean)( CG_VRH_On() && CG_VR_Engine() && trap->ext.VR_GenericCmd &&
		atoi( Info_ValueForKey( CG_ConfigString( CS_SERVERINFO ), "vrSaberDrop" ) ) > 0 );
}

// Whether your first saber is down (dropped, knocked away - not thrown: no
// saber entity in your hands), on a server that has it yours to take up or
// call: not in your hand, nor in its holster.
qboolean CG_VR_SaberAway( void ) {
	const playerState_t *ps = &cg.predictedPlayerState;

	return (qboolean)( ps->saberInFlight && !ps->saberEntityNum && CG_VR_SaberDropServed() );
}

// Whether your hands are yours to do anything with, as the server has it
// (g_vrsaberdrop.c, VRD_Playing): alive, on foot or on a jetpack - not down
// (roleplay), not held in someone's Grip, not riding something.
static qboolean CG_VRSD_Playing( void ) {
	const playerState_t *ps = &cg.predictedPlayerState;

	return (qboolean)( ps->stats[STAT_HEALTH] > 0 && !( ps->eFlags & EF_DEAD ) && ( ps->pm_type == PM_NORMAL ||
		ps->pm_type == PM_JETPACK ) && !ps->m_iVehicleNum && !CG_RP_IsDown() );
}

/*
--------------------------------------------------------------------------------
Let go of
--------------------------------------------------------------------------------
*/

// Holding the grip, hand h let go of saber s (cg_vrholster.c, CG_VRH_GripFrame:
// a moment's slack past): qtrue - it drops (asked of the server); qfalse - as
// before, back to its holster.
qboolean CG_VR_SaberDropLetGo( const vrClientState_t *vr, int h, int s ) {
	const playerState_t *ps = &cg.predictedPlayerState;

	if ( !CG_VR_SaberDropServed() || !CG_VRH_GripHolds() || s != 0 || CG_VR_SaberHand( 1 ) >= 0 ) {
		return qfalse;	// (or the second of two, or both in hand: back in their holsters)
	}
	if ( cgs.gametype == GT_SIEGE && cgs.clientinfo[cg.clientNum].siegeIndex != -1 ) {
		return qfalse;	// (a siege class's)
	}
	if ( ps->weapon != WP_SABER || ps->saberInFlight || ps->saberLockTime >= cg.time || ps->duelTime >= cg.time || !CG_VRSD_Playing() ) {
		return qfalse;	// (or down, say: as ever, back in its holster - the server wouldn't drop it)
	}
	if ( CG_VRH_AtSide( vr, h, s ) ) {
		return qfalse;	// (at your side, or at a holster it goes in: back in a holster)
	}
	trap->ext.VR_GenericCmd( GENCMD_VR_SABERDROP );
	vrsd.dropTime = cg.time ? cg.time : 1;
	vrsd.dropHand = h;
	CG_VR_Feel( h, 0.3f, 20, 0.0f, 0 );	// (a soft bump: let go)
	CG_VR_Log( "saber: let go of in the %s hand - dropped\n", h == VR_HAND_LEFT ? "left" : "right" );
	return qtrue;
}

/*
--------------------------------------------------------------------------------
Down
--------------------------------------------------------------------------------
*/

// Its entity: the one the server says is yours, in this snapshot (-1: none).
static int CG_VRSD_Find( void ) {
	int i;

	if ( !cg.snap ) {
		return -1;
	}
	if ( vrsd.ent >= 0 ) {
		const centity_t *c = &cg_entities[vrsd.ent];
		if ( c->currentValid && c->currentState.eType == ET_MISSILE && c->currentState.weapon == WP_SABER &&
			c->currentState.otherEntityNum2 == cg.clientNum + 1 ) {
			return vrsd.ent;
		}
	}
	for ( i = 0; i < cg.snap->numEntities; i++ ) {
		const entityState_t *es = &cg.snap->entities[i];
		if ( es->eType == ET_MISSILE && es->weapon == WP_SABER && es->otherEntityNum2 == cg.clientNum + 1 ) {
			return es->number;
		}
	}
	return -1;
}

// Where its hilt is this frame (world): as its entity's drawn (its model's own
// way round: cg_vrholster.c, CG_VRH_SaberFrame).
static qboolean CG_VRSD_Hilt( void ) {
	const centity_t *c;
	vec3_t ownDir, ownFace, ax[3];

	vrsd.hilt = qfalse;
	if ( vrsd.ent < 0 ) {
		return qfalse;
	}
	c = &cg_entities[vrsd.ent];
	if ( !CG_VRH_SaberFrame( 0, ownDir, ownFace, &vrsd.lo, &vrsd.hi ) ) {
		VectorSet( ownDir, 0.0f, 0.0f, 1.0f );
		vrsd.lo = -6.5f;
		vrsd.hi = 6.0f;
	}
	AnglesToAxis( c->lerpAngles, ax );
	VectorScale( ax[0], ownDir[0], vrsd.blade );
	VectorMA( vrsd.blade, ownDir[1], ax[1], vrsd.blade );
	VectorMA( vrsd.blade, ownDir[2], ax[2], vrsd.blade );
	if ( VectorNormalize( vrsd.blade ) < 0.5f ) {
		return qfalse;
	}
	VectorCopy( c->lerpOrigin, vrsd.tag );
	VectorMA( vrsd.tag, vrsd.lo, vrsd.blade, vrsd.hiltA );
	VectorMA( vrsd.tag, vrsd.hi, vrsd.blade, vrsd.hiltB );
	vrsd.hilt = qtrue;
	return qtrue;
}

// How far p is from its hilt, and how far along it from where its tag holds
// it (lo..hi) the nearest place on it is.
static float CG_VRSD_HiltDist( const vec3_t p, float *along ) {
	vec3_t rel, q;
	float t;

	VectorSubtract( p, vrsd.tag, rel );
	t = Com_Clamp( vrsd.lo, vrsd.hi, DotProduct( rel, vrsd.blade ) );
	VectorMA( vrsd.tag, t, vrsd.blade, q );
	if ( along ) {
		*along = t;
	}
	return Distance( p, q );
}

// Whether hand h points at its middle: the controller's line, or the one from
// your eyes through the hand - within VRSD_CALL_AIM, VRSD_CALL_NEAR to
// VRSD_CALL_RANGE away.
static qboolean CG_VRSD_Points( const vrClientState_t *vr, int h, const vec3_t grip ) {
	vec3_t aim[3], world[3], head[3], eye, mid, to, line;
	float dist;
	int i;

	VectorAdd( vrsd.hiltA, vrsd.hiltB, mid );
	VectorScale( mid, 0.5f, mid );
	VectorSubtract( mid, grip, to );
	dist = VectorNormalize( to );
	if ( dist < VRSD_CALL_NEAR * vr->worldScale || dist > VRSD_CALL_RANGE * vr->worldScale ) {
		return qfalse;
	}
	for ( i = 0; i < 3; i++ ) {
		VectorCopy( vr->aimAxis[h][i], aim[i] );
	}
	VR_ToWorld( vr, cg.refdef.vieworg, cg.refdef.viewaxis, NULL, aim, NULL, world );
	if ( DotProduct( world[0], to ) >= cosf( DEG2RAD( VRSD_CALL_AIM ) ) ) {
		return qtrue;
	}
	// (from your eyes through the hand)
	for ( i = 0; i < 3; i++ ) {
		VectorCopy( vr->headAxis[i], head[i] );
	}
	VR_ToWorld( vr, cg.refdef.vieworg, cg.refdef.viewaxis, vr->headOrigin, head, eye, world );
	VectorSubtract( grip, eye, line );
	if ( VectorNormalize( line ) < VRSD_CALL_NEAR * vr->worldScale ) {
		return qfalse;	// (the hand by your face: no line through it)
	}
	VectorSubtract( mid, eye, to );
	VectorNormalize( to );
	return (qboolean)( DotProduct( line, to ) >= cosf( DEG2RAD( VRSD_CALL_AIM ) ) );
}

// Where each empty hand's squeeze takes it up or calls it, a tick in the hand
// as it gets there; a squeeze there (the engine's grab: offered last frame),
// asked of the server.
static void CG_VRSD_Hands( const vrClientState_t *vr ) {
	const int claimed = CG_VR_TwoHandZones() | CG_VRH_Zones() | CG_VR_GunZones() | CG_VR_WallZones();
	const int offered = ( cg.time - vrsd.zoneTime >= 0 && cg.time - vrsd.zoneTime <= 100 ) ? vrsd.zones : 0;
	int h;

	vrsd.zones = 0;
	vrsd.zoneTime = cg.time;
	for ( h = 0; h < 2; h++ ) {
		const qboolean grabbing = (qboolean)( vr->grabbing[h] );
		const int was = vrsd.zone[h];
		vec3_t grip, ax[3];
		float along;

		vrsd.zone[h] = VRSD_ZONE_NONE;
		if ( grabbing && !vrsd.grabWas[h] && ( offered & ( 1 << h ) ) && was != VRSD_ZONE_NONE ) {
			// a squeeze where it was offered
			if ( was == VRSD_ZONE_TAKE && vrsd.hilt && CG_VR_HandGrip( h, grip, ax ) ) {
				CG_VRSD_HiltDist( grip, &along );
				vrsd.takeT = along;
				vrsd.takeSign = ( DotProduct( ax[0], vrsd.blade ) < 0.0f ) ? -1.0f : 1.0f;	// (ax[0]: through the fist toward the thumb)
				vrsd.takeTime = cg.time ? cg.time : 1;
				vrsd.takeHand = h;
				trap->ext.VR_GenericCmd( GENCMD_VR_SABERTAKE );
				CG_VR_Feel( h, 0.6f, 30, 0.2f, 60 );	// (a bump: in your hand)
				CG_VR_Log( "saber: taken up in the %s hand\n", h == VR_HAND_LEFT ? "left" : "right" );
			} else if ( was == VRSD_ZONE_CALL ) {
				vrsd.callTime = cg.time ? cg.time : 1;
				vrsd.callHand = h;
				trap->ext.VR_GenericCmd( h == VR_HAND_LEFT ? GENCMD_VR_SABERCALL_L : GENCMD_VR_SABERCALL_R );
				CG_VR_Feel( h, 0.45f, 80, 0.25f, 200 );	// (a pull)
				CG_VR_Log( "saber: called to the %s hand\n", h == VR_HAND_LEFT ? "left" : "right" );
			}
		}
		vrsd.grabWas[h] = grabbing;
		if ( !vrsd.hilt || !vr->handValid[h] || ( claimed & ( 1 << h ) ) || !CG_VRH_HandEmpty( h ) ||
			!CG_VR_HandGrip( h, grip, ax ) ) {
			continue;
		}
		if ( vrsd.takeTime && cg.time - vrsd.takeTime >= 0 && cg.time - vrsd.takeTime < VRSD_TAKE_MS ) {
			continue;	// (taken up: on its way back)
		}
		if ( vrsd.callTime && h == vrsd.callHand && cg.time - vrsd.callTime >= 0 && cg.time - vrsd.callTime < VRSD_CALL_MS ) {
			continue;	// (called to this hand: on its way)
		}
		if ( CG_VRSD_HiltDist( grip, NULL ) <= VRSD_TAKE_REACH * vr->worldScale ) {
			vrsd.zone[h] = VRSD_ZONE_TAKE;
		} else if ( CG_VRSD_Points( vr, h, grip ) ) {
			vrsd.zone[h] = VRSD_ZONE_CALL;
		} else {
			continue;
		}
		vrsd.zones |= 1 << h;
		if ( vrsd.zone[h] != was ) {
			CG_VR_Feel( h, ( vrsd.zone[h] == VRSD_ZONE_TAKE ) ? 0.35f : 0.18f, 18, 0.0f, 0 );
		}
	}
}

// Pointed at to be called: it lights up a little (in each eye's scene).
static void CG_VRSD_Light( void ) {
	vec3_t mid;

	if ( !vrsd.hilt || ( vrsd.zone[0] != VRSD_ZONE_CALL && vrsd.zone[1] != VRSD_ZONE_CALL ) ) {
		return;
	}
	VectorAdd( vrsd.hiltA, vrsd.hiltB, mid );
	VectorScale( mid, 0.5f, mid );
	trap->R_AddLightToScene( mid, 48.0f, 0.35f, 0.6f, 1.0f );
}

// Each frame (cg_vrbody.c, CG_VR_BodyEndFrame, before the engine's told where
// squeezes take hold): your saber gone down, come back, or still down - and
// where your hands take it up or call it.
void CG_VR_SaberDropFrame( const vrClientState_t *vr ) {
	const qboolean down = CG_VR_SaberAway();
	int h;

	if ( vrsd.frameTime == cg.time ) {
		CG_VRSD_Light();	// (second eye: as worked out - its scene lit the same)
		return;
	}
	vrsd.frameTime = cg.time;
	vrsd.hilt = qfalse;

	if ( !CG_VRH_On() ) {
		// holsters not in play a moment (the headset off, say): all as it was
		// meanwhile - nothing gone down or come back of it
		vrsd.zones = 0;
		vrsd.zone[0] = vrsd.zone[1] = VRSD_ZONE_NONE;
		return;
	}
	if ( down && !vrsd.down ) {
		// down: your hands empty of it
		if ( CG_VR_SaberHand( 0 ) >= 0 ) {
			CG_VR_PutSaber( 0 );
		}
		vrsd.ent = -1;
		vrsd.dropTime = 0;
		CG_VR_Log( "saber: down\n" );
	} else if ( !down && vrsd.down ) {
		// back: the hand that took it up, or called it (still empty) - else on your belt
		const qboolean taken = (qboolean)( vrsd.takeTime && cg.time - vrsd.takeTime >= 0 && cg.time - vrsd.takeTime < VRSD_TAKE_MS );
		const qboolean called = (qboolean)( vrsd.callTime && cg.time - vrsd.callTime >= 0 && cg.time - vrsd.callTime < VRSD_CALL_MS );
		const playerState_t *ps = &cg.predictedPlayerState;
		const qboolean alive = (qboolean)( ps->stats[STAT_HEALTH] > 0 && !( ps->eFlags & EF_DEAD ) && ps->pm_type != PM_DEAD );
		h = taken ? vrsd.takeHand : called ? vrsd.callHand : -1;
		if ( alive && h >= 0 && CG_VRH_HandEmpty( h ) && CG_VR_SaberHand( 0 ) < 0 ) {
			CG_VRH_SaberToHand( h, taken ? vrsd.takeT : 0.0f, taken ? vrsd.takeSign : 1.0f );
			CG_VR_Feel( h, 0.6f, 40, 0.2f, 80 );	// (in your hand)
			CG_VR_Log( "saber: back in the %s hand\n", h == VR_HAND_LEFT ? "left" : "right" );
		} else {
			CG_VR_Log( "saber: back on your belt\n" );
		}
		vrsd.takeTime = vrsd.callTime = 0;
	}
	vrsd.down = down;

	// let go of, the server not dropping it after all: as before, back to its holster
	if ( vrsd.dropTime && !down && ( cg.time - vrsd.dropTime < 0 || cg.time - vrsd.dropTime >= VRSD_DROP_MS ) ) {
		const int dh = vrsd.dropHand;
		vrsd.dropTime = 0;
		if ( CG_VR_SaberHand( 0 ) == dh && !CG_VRH_GripHeld( dh ) ) {
			CG_VR_Log( "saber: not dropped - back to its holster\n" );
			CG_VRH_LetGoSaber( dh, 0 );
		}
	}

	if ( !down || !vr || !CG_VRSD_Playing() ) {
		// (nothing down; or down, say - the server takes nothing up nor calls it
		// meanwhile: no grab at it)
		vrsd.zones = 0;
		vrsd.zone[0] = vrsd.zone[1] = VRSD_ZONE_NONE;
		vrsd.grabWas[0] = vr ? vr->grabbing[0] : qfalse;
		vrsd.grabWas[1] = vr ? vr->grabbing[1] : qfalse;
		return;
	}
	vrsd.ent = CG_VRSD_Find();
	CG_VRSD_Hilt();
	CG_VRSD_Hands( vr );
	CG_VRSD_Light();
}

// The hands whose squeeze takes it up or calls it (the engine's grabs).
int CG_VR_SaberDropZones( void ) {
	return ( cg.time - vrsd.zoneTime >= 0 && cg.time - vrsd.zoneTime <= 100 ) ? vrsd.zones : 0;
}

// A map begun: nothing down, nothing asked.
void CG_VR_SaberDropInit( void ) {
	memset( &vrsd, 0, sizeof( vrsd ) );
	vrsd.ent = -1;
}
