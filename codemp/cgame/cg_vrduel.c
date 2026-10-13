/*
===========================================================================
OpenJK VR (multiplayer) - challenging someone to a private duel with your
saber, as Jedi do: a salute. Hold the lit blade upright close in front of
your face, in one hand, and still, looking at a player: a light tick says
it's being taken for a salute (move and it isn't), and a moment later
(VRD_HOLD) the challenge goes out, felt in the saber hand - one a salute:
lower it and salute again for another. Whoever you challenged feels it in
both hands (on any server). They take it up by saluting you back (or
engage_duel at you): saluting back, the one who challenged you counts
before anyone else in your sight. Then the duel's on - any number of those
at a time on a server of this build, which is also told whom you saluted
(vrduel <client>; it checks they're near and in sight). Any other server
gets engage_duel, at whoever you're facing, so there it has to be someone
right in front of you and near.

None while you're in a duel, just after your blade has met another or
someone (you're fighting: a guard isn't a salute), or where there are no
private duels (team games, Duel, a server with them off); off with
cg_vrDuelSalute 0.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "cg_local.h"

#define VRD_HOLD			800		// ms the salute is held...
#define VRD_TICK			250		// ...the tick this far in
#define VRD_UPRIGHT			0.940f	// (cos 20) the blade at least this upright
#define VRD_FACE_NEAR		0.06f	// where it's at your eyes' height: this far in front of them (metres)...
#define VRD_FACE_FAR		0.35f	// (further out is a guard)
#define VRD_FACE_SIDE		0.12f	// ...and no further to the side (in front of the face, not the cheek)
#define VRD_STILL			0.08f	// it may move this far (metres) while it's held...
#define VRD_STILL_TURN		0.966f	// ...and turn this much (cos 15)
#define VRD_LOOK			0.349f	// (20 degrees, radians) whom you salute: this near your gaze (to the edge of them)...
#define VRD_BODY			16.0f	// (a player's half-width, units)
#define VRD_RANGE			480.0f	// ...and this near, eye to eye (units: a little nearer than the server takes, 512)
#define VRD_RANGE_STOCK		224.0f	// (another server: engage_duel reaches 256 from your feet, right where you face)
#define VRD_AGAIN			3000	// ms before you can challenge again (as the server takes another)...
#define VRD_AGAIN_STOCK		5000	// ...another server
#define VRD_CHALLENGED		10000	// ms a challenge to you stands (on another server, 5000)
#define VRD_FIGHTING		1500	// ms after your blade meets another (or someone): no salutes

static struct {
	int		since;		// cg.time the salute began (0: no salute)
	int		target;		// at whom
	int		hand;		// held in which hand
	vec3_t	point;		// where the blade was at your eyes' height then...
	vec3_t	dir;		// ...pointing this way
	qboolean	ticked;	// (the tick felt)
	int		sentTime;	// the last challenge...
	qboolean	sent;	// ...by this salute (it's still held)
	int		challengedBy[MAX_CLIENTS];	// cg.time each challenged you (the centre print; 0: hasn't)
} vrd;

// What the server does with a salute: 1, vrduel <client> (this build); 0,
// nothing to say so (another server: engage_duel); -1, no private duels.
static int CG_VRD_Server( void ) {
	const char *v = Info_ValueForKey( CG_ConfigString( CS_SERVERINFO ), "vrDuel" );

	if ( !v[0] ) {
		return 0;
	}
	return ( atoi( v ) > 0 ) ? 1 : -1;
}

// Whether you could challenge someone now.
static qboolean CG_VRD_Able( const vrClientState_t *vr ) {
	const playerState_t *ps = &cg.predictedPlayerState;
	int fight;

	if ( !vr || !vr->active || vr->worldScale <= 0.0f || !cg_vrDuelSalute.integer || !cg.snap ) {
		return qfalse;
	}
	if ( cgs.gametype >= GT_TEAM || cgs.gametype == GT_DUEL || cgs.gametype == GT_POWERDUEL || CG_VRD_Server() < 0 ) {
		return qfalse;	// (no private duels)
	}
	if ( ps->duelInProgress || ps->stats[STAT_HEALTH] <= 0 || ps->pm_type != PM_NORMAL || ps->weapon != WP_SABER ||
		ps->persistant[PERS_TEAM] == TEAM_SPECTATOR || ( cg.snap->ps.pm_flags & PMF_FOLLOW ) ) {
		return qfalse;
	}
	fight = CG_VR_BladeFightTime();
	if ( fight && cg.time - fight >= 0 && cg.time - fight < VRD_FIGHTING ) {
		return qfalse;	// (fighting: a guard held up isn't a salute)
	}
	return qtrue;
}

// A salute: a lit blade upright close in front of your face, in one hand
// (both on it is a guard). Where it is at your eyes' height and which way it
// points, and in which hand.
static qboolean CG_VRD_Salute( const vrClientState_t *vr, const vec3_t head, const vec3_t forward,
	vec3_t point, vec3_t dir, int *hand ) {
	const float m = vr->worldScale;
	vec3_t fwd, left, rel, base, tip;
	int s, b;

	// in front of you, across: level (your head may tilt)
	VectorSet( fwd, forward[0], forward[1], 0.0f );
	if ( VectorNormalize( fwd ) < 0.1f ) {
		return qfalse;	// (looking straight up or down)
	}
	VectorSet( left, -fwd[1], fwd[0], 0.0f );

	for ( s = 0; s < VR_POSE_MAX_SABERS; s++ ) {
		for ( b = 0; b < VR_POSE_MAX_BLADES; b++ ) {
			float length, below, along;
			qboolean twoHanded = qfalse;
			if ( !CG_VR_LitBlade( s, b, base, tip, &twoHanded ) || twoHanded ) {
				continue;
			}
			VectorSubtract( tip, base, dir );
			length = VectorNormalize( dir );
			if ( length < 1.0f || dir[2] < VRD_UPRIGHT ) {
				continue;	// (not upright)
			}
			// where it's at the height of your eyes (it has to reach them)
			below = head[2] - base[2];
			if ( below < 0.0f ) {
				continue;
			}
			along = below / dir[2];
			if ( along > length ) {
				continue;
			}
			VectorMA( base, along, dir, point );
			VectorSubtract( point, head, rel );
			if ( DotProduct( rel, fwd ) < VRD_FACE_NEAR * m || DotProduct( rel, fwd ) > VRD_FACE_FAR * m ||
				fabsf( DotProduct( rel, left ) ) > VRD_FACE_SIDE * m ) {
				continue;	// (not close in front of your face)
			}
			*hand = ( s == 1 ) ? VR_HAND_LEFT : VR_HAND_RIGHT;
			return qtrue;
		}
	}
	return qfalse;
}

// Whom you're saluting - near enough, in sight, free to fight one (alive,
// saber in hand, not in a duel): one who has just challenged you and is
// near your gaze; or else the player your gaze goes through (the nearest, if
// it goes through more than one), or else the one it passes nearest, as you
// see them. On another server only one it goes through, and near (where
// engage_duel reaches). -1: none.
static int CG_VRD_Target( const vec3_t head, const vec3_t forward, int server ) {
	const float range = ( server > 0 ) ? VRD_RANGE : VRD_RANGE_STOCK;
	const float look = ( server > 0 ) ? VRD_LOOK : 0.0f;
	const int challengeTime = ( server > 0 ) ? VRD_CHALLENGED : VRD_CHALLENGED / 2;
	float bestOff = 0.0f, bestAlong = 0.0f;
	qboolean bestChallenger = qfalse;
	int target = -1, i;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		const centity_t *cent = &cg_entities[i];
		const clientInfo_t *ci = &cgs.clientinfo[i];
		vec3_t eye, rel, gaze;
		trace_t tr;
		float along, h, v, lo, hi, off;
		qboolean challenger;

		if ( i == cg.snap->ps.clientNum || !cent->currentValid || cent->currentState.eType != ET_PLAYER ) {
			continue;
		}
		if ( ( cent->currentState.eFlags & EF_DEAD ) || cent->currentState.bolt1 || cent->currentState.weapon != WP_SABER ) {
			continue;	// (bolt1: in a duel)
		}
		if ( !ci->infoValid || ci->team == TEAM_SPECTATOR ) {
			continue;
		}
		VectorCopy( cent->lerpOrigin, eye );
		eye[2] += DEFAULT_VIEWHEIGHT;
		if ( Distance( head, eye ) > range ) {
			continue;
		}
		// where your gaze passes them, and how far off them that is as you
		// see them (none: it goes through them)
		VectorSubtract( cent->lerpOrigin, head, rel );
		along = DotProduct( rel, forward );
		if ( along < 1.0f ) {
			continue;	// (behind you)
		}
		VectorMA( head, along, forward, gaze );
		h = sqrtf( Square( gaze[0] - cent->lerpOrigin[0] ) + Square( gaze[1] - cent->lerpOrigin[1] ) );
		lo = cent->lerpOrigin[2] + DEFAULT_MINS_2;
		hi = cent->lerpOrigin[2] + DEFAULT_MAXS_2;
		v = ( gaze[2] < lo ) ? lo - gaze[2] : ( ( gaze[2] > hi ) ? gaze[2] - hi : 0.0f );
		off = atan2f( sqrtf( h * h + v * v ), along ) - atan2f( VRD_BODY, along );
		if ( off < 0.0f ) {
			off = 0.0f;
		}
		if ( off > look ) {
			continue;
		}
		challenger = (qboolean)( vrd.challengedBy[i] && cg.time - vrd.challengedBy[i] >= 0 &&
			cg.time - vrd.challengedBy[i] < challengeTime );
		if ( target >= 0 && ( ( bestChallenger && !challenger ) ||
			( challenger == bestChallenger && ( off > bestOff || ( off == bestOff && along >= bestAlong ) ) ) ) ) {
			continue;
		}
		// in sight (eye to eye, as the server sees it)
		CG_Trace( &tr, head, NULL, NULL, eye, cg.snap->ps.clientNum, MASK_SOLID );
		if ( tr.fraction < 1.0f && tr.entityNum != i ) {
			continue;
		}
		bestOff = off;
		bestAlong = along;
		bestChallenger = challenger;
		target = i;
	}
	return target;
}

// The challenge: to a server of this build, whom; to any other, at whoever
// you're facing. Felt in the hand that saluted.
static void CG_VRD_Challenge( int target, int hand, int server ) {
	if ( server > 0 ) {
		trap->SendClientCommand( va( "vrduel %i", target ) );
	} else {
		trap->SendConsoleCommand( "engage_duel\n" );
	}
	CG_VR_Feel( hand, 0.6f, 90, 0.3f, 150 );
}

// Every frame, once the blades in your hands are known (CG_VR_BodyEndFrame).
void CG_VR_DuelFrame( void ) {
	const vrClientState_t *vr = CG_VR();
	vec3_t head, axis[3], point, dir;
	int hand, target, server;

	if ( !CG_VRD_Able( vr ) || !CG_VR_HeadPose( head, axis ) || !CG_VRD_Salute( vr, head, axis[0], point, dir, &hand ) ) {
		vrd.since = 0;
		vrd.sent = qfalse;	// (lowered: the next one is a new salute)
		return;
	}
	server = CG_VRD_Server();
	if ( vrd.sent || ( vrd.sentTime && cg.time - vrd.sentTime >= 0 &&
		cg.time - vrd.sentTime < ( ( server > 0 ) ? VRD_AGAIN : VRD_AGAIN_STOCK ) ) ||
		( target = CG_VRD_Target( head, axis[0], server ) ) < 0 ) {
		vrd.since = 0;
		return;
	}
	// held still, at the same one: or it starts over
	if ( !vrd.since || target != vrd.target || hand != vrd.hand || Distance( point, vrd.point ) > VRD_STILL * vr->worldScale ||
		DotProduct( dir, vrd.dir ) < VRD_STILL_TURN ) {
		vrd.since = cg.time;
		vrd.target = target;
		vrd.hand = hand;
		vrd.ticked = qfalse;
		VectorCopy( point, vrd.point );
		VectorCopy( dir, vrd.dir );
		return;
	}
	if ( cg.time - vrd.since < VRD_HOLD ) {
		if ( !vrd.ticked && cg.time - vrd.since >= VRD_TICK ) {
			CG_VR_Feel( hand, 0.2f, 25, 0.0f, 0 );	// (taken for a salute: move and it isn't)
			vrd.ticked = qtrue;
		}
		return;
	}
	CG_VRD_Challenge( target, hand, server );
	vrd.sentTime = cg.time;
	vrd.sent = qtrue;
	vrd.since = 0;
}

// A centre print from the server: one saying you've been challenged (any
// server's reads "<name> @@@PLDUELCHALLENGE") is felt in both hands, and
// whoever it names counts first when you salute back.
void CG_VR_DuelCentrePrint( const char *text ) {
	const vrClientState_t *vr = CG_VR();
	const char *ref = text ? strstr( text, "@@@PLDUELCHALLENGE" ) : NULL;
	const char after = ref ? ref[18] : 0;
	char name[MAX_QPATH];
	int i, length;

	if ( !ref || ( after >= 'A' && after <= 'Z' ) || ( after >= 'a' && after <= 'z' ) || ( after >= '0' && after <= '9' ) || after == '_' ) {
		return;	// (not that one: "...CHALLENGED" is what the challenger reads)
	}
	// who: what comes before it ("%s %s")
	length = ref - text;
	if ( length > 0 && text[length - 1] == ' ' ) {
		length--;
	}
	if ( length > 0 && length < (int)sizeof( name ) ) {
		memcpy( name, text, length );
		name[length] = '\0';
		for ( i = 0; i < MAX_CLIENTS; i++ ) {
			if ( cgs.clientinfo[i].infoValid && !strcmp( cgs.clientinfo[i].name, name ) ) {
				vrd.challengedBy[i] = cg.time ? cg.time : 1;
			}
		}
	}
	if ( !vr || !vr->active ) {
		return;
	}
	CG_VR_Feel( VR_HAND_LEFT, 0.5f, 200, 0.25f, 150 );
	CG_VR_Feel( VR_HAND_RIGHT, 0.5f, 200, 0.25f, 150 );
}
