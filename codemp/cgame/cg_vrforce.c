/*
===========================================================================
OpenJK VR (multiplayer) - the Force hand, in the client game.

Your Force hand's gestures are the engine's (qcommon/vr_force.h: Force
gestures, vr_forceGestures); here they're felt, in that hand (gestures
off, the Force grip's):

  reaching out      a light buzz as the Force grip goes in, then a faint
                    hum while it's held
  Push              a thump
  Pull              a tug
  a quick squeeze   a tick (Use Force: the power picked)
  Grip              a rough strain while you hold someone
  Lightning         a crackle while it's going
  Drain             a steady pull while it's going

Push and Pull are felt if you know them and the game would do them now
(not with someone gripped, Lightning or Drain going, the last Push's
wait not over, a swing going...: CG_VRF_ThrowWorks); Grip, Lightning and
Drain while the game has them going (your player state), however you
started them - and none of it while you watch (following someone,
spectating, a demo: their Force isn't yours). The log (cg_vrLog) says
what the hand did.

On a server that aims the Force from the hand ("vrForce" in its
serverinfo: game/g_vrforce.c) it comes out of it, too: Lightning and
Drain from that hand, the way it points - onto whom the server snaps it,
as near as this can tell (CG_VR_ForceSnap) - Push's ripple and Grip's
glow round it (yours, in the headset, too); yours, drawn as your VR body,
and another VR player's, from their avatar (cg_players.c).

And the wheel's Force ring (cg_vrpose.c) picks the Force powers that
aren't gestures - the one the Force grip's quick squeeze uses, as X and Y
step through them all (yours: not while you watch).

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "cg_local.h"

/*
--------------------------------------------------------------------------------
What the Force hand feels
--------------------------------------------------------------------------------
*/

static struct {
	qboolean	init;
	int			pushes, pulls, taps;	// the engine's counts, as last seen
	qboolean	reaching;
	int			held;
	int			active;					// your Force powers going, as last seen (the log)
} cgf;

// Whether you're watching, not playing: following someone (the player state's
// theirs), spectating, a demo - no Force of your own to feel or pick. (cg.snap:
// there.)
static qboolean CG_VRF_Watching( void ) {
	return (qboolean)( cg.demoPlayback || ( cg.snap->ps.pm_flags & PMF_FOLLOW ) || cg.snap->ps.persistant[PERS_TEAM] == TEAM_SPECTATOR );
}

extern qboolean BG_InKnockDownOnly( int anim );
extern int forcePowerNeeded[NUM_FORCE_POWER_LEVELS][NUM_FORCE_POWERS];

// Whether a Push or a Pull (power) would do anything now, as far as your player
// state says - the game's own reasons it does nothing (ForceThrow's,
// WP_ForcePowerUsable's): it can't be used now (BG_CanUseFPNow: a ysalamiri,
// a duel, a vehicle, a broken arm...; down; in a pose; not enough Force),
// your hand busy (someone gripped, Lightning or Drain going, the last Push or
// Pull still out, a choke, a taunt...; knocked down, unless getting up), a
// swing going, the last Push's or Pull's wait not over.
static qboolean CG_VRF_ThrowWorks( const playerState_t *ps, int power ) {
	const int level = ps->fd.forcePowerLevel[power];

	if ( level <= FORCE_LEVEL_0 || level >= NUM_FORCE_POWER_LEVELS || ps->fd.forcePower < forcePowerNeeded[level][power] ||
		!BG_CanUseFPNow( cgs.gametype, (playerState_t *)ps, cg.time, (forcePowers_t)power ) || CG_RP_IsDown() || CG_VR_Posed() ) {
		return qfalse;
	}
	if ( ps->weaponTime > 0 || ps->powerups[PW_DISINT_4] > cg.time ||
		( ps->fd.forcePowersActive & ( ( 1 << FP_GRIP ) | ( 1 << FP_LIGHTNING ) | ( 1 << FP_DRAIN ) ) ) ) {
		return qfalse;
	}
	return (qboolean)( ps->forceHandExtend == HANDEXTEND_NONE ||
		( ps->forceHandExtend == HANDEXTEND_KNOCKDOWN && !BG_InKnockDownOnly( ps->legsAnim ) ) );
}

static const char *CG_VRF_Held( int held ) {
	switch ( held ) {
	case VR_FORCE_GRIP:			return "Grip";
	case VR_FORCE_LIGHTNING:	return "Lightning";
	case VR_FORCE_DRAIN:		return "Drain";
	default:					return "nothing";
	}
}

// Once a frame, before what the hands feel goes to the controllers
// (CG_VR_BodyEndFrame).
void CG_VR_ForceFrame( const vrClientState_t *vr ) {
	const playerState_t *ps = &cg.predictedPlayerState;
	int h, i, active;

	if ( cg.secondEye ) {
		return;
	}
	if ( !vr || !cg.snap || CG_VRF_Watching() ) {
		cgf.init = qfalse;	// (what's counted meanwhile is never felt)
		return;
	}
	if ( !cgf.init ) {
		// (what the engine counted before this game began has been felt)
		memset( &cgf, 0, sizeof( cgf ) );
		cgf.pushes = vr->force.pushes;
		cgf.pulls = vr->force.pulls;
		cgf.taps = vr->force.taps;
		cgf.init = qtrue;
	}
	h = vr->force.hand;
	if ( h != VR_HAND_LEFT && h != VR_HAND_RIGHT ) {
		h = ( CG_VR_GunHand() == VR_HAND_LEFT ) ? VR_HAND_RIGHT : VR_HAND_LEFT;	// (the Force grip's)
	}

	// reaching out
	if ( vr->force.reaching && !cgf.reaching ) {
		CG_VR_Feel( h, 0.3f, 25, 0.15f, 90 );
		CG_VR_Log( "force: the %s hand reaches out\n", h == VR_HAND_RIGHT ? "right" : "left" );
	}
	if ( vr->force.reaching ) {
		CG_VR_FeelSteady( h, 0.07f, 0.5f );
	}
	cgf.reaching = vr->force.reaching;

	// what it did (a Push or a Pull the game won't do now: not felt)
	if ( vr->force.pushes != cgf.pushes ) {
		const qboolean known = (qboolean)( ( ps->fd.forcePowersKnown & ( 1 << FP_PUSH ) ) != 0 ), works = CG_VRF_ThrowWorks( ps, FP_PUSH );
		if ( known && works ) {
			CG_VR_Feel( h, 1.0f, 70, 0.6f, 140 );
		}
		CG_VR_Log( "force: Push, thrust at %.1f m/s%s\n", vr->force.speed,
			!known ? " (you don't know it)" : !works ? " (the game won't now)" : "" );
		cgf.pushes = vr->force.pushes;
	}
	if ( vr->force.pulls != cgf.pulls ) {
		const qboolean known = (qboolean)( ( ps->fd.forcePowersKnown & ( 1 << FP_PULL ) ) != 0 ), works = CG_VRF_ThrowWorks( ps, FP_PULL );
		if ( known && works ) {
			CG_VR_Feel( h, 0.75f, 50, 0.7f, 250 );
		}
		CG_VR_Log( "force: Pull, yanked at %.1f m/s%s\n", vr->force.speed,
			!known ? " (you don't know it)" : !works ? " (the game won't now)" : "" );
		cgf.pulls = vr->force.pulls;
	}
	if ( vr->force.taps != cgf.taps ) {
		CG_VR_Feel( h, 0.25f, 20, 0.0f, 0 );
		CG_VR_Log( "force: a quick squeeze - Use Force (%s)\n", CG_VR_ForcePowerName( ps->fd.forcePowerSelected ) );
		cgf.taps = vr->force.taps;
	}
	if ( vr->force.held != cgf.held ) {
		CG_VR_Log( "force: holding %s\n", CG_VRF_Held( vr->force.held ) );
		cgf.held = vr->force.held;
	}

	// what's going (the game's word)
	active = ps->fd.forcePowersActive & ( ( 1 << FP_GRIP ) | ( 1 << FP_LIGHTNING ) | ( 1 << FP_DRAIN ) );
	if ( active & ( 1 << FP_GRIP ) ) {
		CG_VR_FeelSteady( h, 0.35f, 0.6f );
	}
	if ( active & ( 1 << FP_LIGHTNING ) ) {
		CG_VR_FeelSteady( h, Q_flrand( 0.35f, 0.9f ), 0.5f );
	}
	if ( active & ( 1 << FP_DRAIN ) ) {
		CG_VR_FeelSteady( h, 0.3f, 0.1f );
	}
	for ( i = FP_GRIP; i <= FP_DRAIN; i++ ) {
		if ( ( active ^ cgf.active ) & ( 1 << i ) ) {
			CG_VR_Log( "force: %s %s\n", CG_VR_ForcePowerName( i ), ( active & ( 1 << i ) ) ? "going" : "over" );
		}
	}
	cgf.active = active;
}

/*
--------------------------------------------------------------------------------
Where the Force comes out
--------------------------------------------------------------------------------
*/

#define VR_FORCE_SNAP	10.0f	// degrees: someone this near the Force's line is who it goes at (the server's: g_vrforce.c)

// Whether the server aims VR players' Force from their Force hand.
qboolean CG_VR_ForceServed( void ) {
	return (qboolean)( atoi( Info_ValueForKey( CG_ConfigString( CS_SERVERINFO ), "vrForce" ) ) > 0 );
}

// Where a player's Force comes out for its effects (cg_players.c): their
// Force hand (VR_HAND_*) and the way it points (world axes) - yours, drawn as
// your VR body, or another VR player's, from their avatar - on a server that
// aims it from there. -1: the game's own (the left hand, the way they face).
int CG_VR_ForceFx( centity_t *cent, vec3_t dir ) {
	const vrClientState_t *vr;
	vec3_t aim[3], world[3];
	int h, i;

	if ( !cent || !CG_VR_ForceServed() || !CG_VR_BodyActive( cent ) ) {
		return -1;
	}
	if ( cent->currentState.number != cg.clientNum ) {
		return CG_VR_AvatarForce( cent, dir );
	}
	vr = CG_VR();
	h = vr ? vr->force.hand : -1;
	if ( ( h != VR_HAND_LEFT && h != VR_HAND_RIGHT ) || !vr->handValid[h] ) {
		return -1;
	}
	for ( i = 0; i < 3; i++ ) {
		VectorCopy( vr->aimAxis[h][i], aim[i] );
	}
	VR_ToWorld( vr, cg.refdef.vieworg, cg.refdef.viewaxis, NULL, aim, NULL, world );
	VectorCopy( world[0], dir );
	return h;
}

#define CGF_NONE	0	// (CG_VRF_Target: not someone the server would snap the Force onto...
#define CGF_SEEN	1	// ...someone it would...
#define CGF_HIDDEN	2	// ...someone hidden from the shooter: never snapped onto, nor off - nearest the line, it stays)

// Whether two players are on one side, as the server has it (OnSameTeam), as far
// as this can tell: a Power Duel's pair, a team game's team.
static qboolean CG_VRF_SameSide( int a, int b ) {
	if ( a < 0 || a >= MAX_CLIENTS || b < 0 || b >= MAX_CLIENTS ) {
		return qfalse;
	}
	if ( cgs.gametype == GT_POWERDUEL ) {
		return (qboolean)( cgs.clientinfo[a].duelTeam == cgs.clientinfo[b].duelTeam );
	}
	return (qboolean)( cgs.gametype >= GT_TEAM && cgs.clientinfo[a].team == cgs.clientinfo[b].team );
}

// Whom the server would snap a VR player's Force (power) onto, as far as this
// can tell (g_vrforce.c's VRF_Snap): someone alive and drawn, the power working
// on them (ForcePowerUsableOn: not down, not in a duel, no ysalamiri; an NPC
// not the shooter's companion nor, in Siege, any but a vehicle - a vehicle,
// Lightning alone), not on the shooter's side; CGF_HIDDEN, hidden from the
// shooter (cloaked, or tricking them, while their Seeing isn't going) - with
// where their middle is drawn.
static int CG_VRF_Target( int shooter, int power, int n, vec3_t mid ) {
	int eFlags, powerups, trick[4];
	qboolean duel;

	if ( n == shooter || n < 0 || n >= ENTITYNUM_MAX_NORMAL ) {
		return CGF_NONE;
	}
	if ( n == cg.predictedPlayerState.clientNum ) {
		// you (or whom you follow), as your game has you
		const playerState_t *ps = &cg.predictedPlayerState;
		int i;
		if ( ps->pm_type == PM_SPECTATOR || ps->pm_type == PM_INTERMISSION || ps->stats[STAT_HEALTH] <= 0 ) {
			return CGF_NONE;
		}
		eFlags = ps->eFlags;
		for ( powerups = 0, i = 0; i < PW_NUM_POWERUPS; i++ ) {
			if ( ps->powerups[i] ) {
				powerups |= 1 << i;
			}
		}
		trick[0] = ps->fd.forceMindtrickTargetIndex;
		trick[1] = ps->fd.forceMindtrickTargetIndex2;
		trick[2] = ps->fd.forceMindtrickTargetIndex3;
		trick[3] = ps->fd.forceMindtrickTargetIndex4;
		duel = ps->duelInProgress;
		VectorCopy( ps->origin, mid );
		mid[2] += ( ps->pm_flags & PMF_DUCKED ) ? -4.0f : 8.0f;	// (the middle of a player's box, crouched or not)
	} else {
		const entityState_t *es = &cg_entities[n].currentState;
		if ( ( es->eType != ET_PLAYER && es->eType != ET_NPC ) ||
			( es->eType == ET_NPC && ( es->NPC_class == CLASS_VEHICLE ? power != FP_LIGHTNING : cgs.gametype == GT_SIEGE ) ) ||
			CG_RP_CompOwner( n ) == shooter ) {
			return CGF_NONE;	// (a vehicle: Lightning alone; another NPC, none in Siege - ForcePowerUsableOn)
		}
		eFlags = es->eFlags;
		powerups = es->powerups;
		trick[0] = es->trickedentindex;
		trick[1] = es->trickedentindex2;
		trick[2] = es->trickedentindex3;
		trick[3] = es->trickedentindex4;
		duel = (qboolean)( es->eType == ET_PLAYER && es->bolt1 );	// (a player in a duel: BG_PlayerStateToEntityState's)
		VectorCopy( cg_entities[n].lerpOrigin, mid );
		if ( es->solid && es->solid != SOLID_BMODEL ) {
			mid[2] += ( ( ( ( es->solid >> 16 ) & 255 ) - 32 ) - ( ( es->solid >> 8 ) & 255 ) ) * 0.5f;	// (its box, as the server put it)
		} else {
			mid[2] += 8.0f;
		}
	}
	if ( ( eFlags & ( EF_DEAD | EF_NODRAW ) ) || duel || ( powerups & ( 1 << PW_YSALAMIRI ) ) ||
		( cgs.gametype == GT_CTY && ( powerups & ( ( 1 << PW_REDFLAG ) | ( 1 << PW_BLUEFLAG ) ) ) ) ||
		CG_RP_DownBody( &cg_entities[n] ) || CG_VRF_SameSide( n, shooter ) ) {
		return CGF_NONE;
	}
	if ( ( ( powerups & ( 1 << PW_CLOAKED ) ) && !( cg_entities[shooter].currentState.forcePowersActive & ( 1 << FP_SEE ) ) ) ||
		CG_IsMindTricked( trick[0], trick[1], trick[2], trick[3], shooter ) ) {
		return CGF_HIDDEN;	// (Seeing shows them both - CG_IsMindTricked asks it too)
	}
	return CGF_SEEN;
}

// How far a VR player's Lightning or Drain reaches at a level, as the server
// has it (w_force.c): above level 2, an arc round the hand (Lightning's
// FORCE_LIGHTNING_RADIUS, Drain's MAX_DRAIN_DISTANCE); else a line, 2048 long.
float CG_VR_ForceReach( int power, int level ) {
	if ( level <= FORCE_LEVEL_2 ) {
		return 2048.0f;
	}
	return ( power == FP_DRAIN ) ? 512.0f : 300.0f;
}

// Where a VR player's Lightning or Drain (power) goes from 'from' (cg_players.c
// draws it so): onto whom the server snaps it (g_vrforce.c), as near as this
// can tell - someone (CG_VRF_Target) within VR_FORCE_SNAP degrees of the line
// dir, within range and in sight of 'from', the one nearest it: dir becomes the
// way to their middle (nobody, or someone hidden nearest: as it was).
void CG_VR_ForceSnap( int shooter, int power, const vec3_t from, float range, vec3_t dir ) {
	float bestDot = cosf( DEG2RAD( VR_FORCE_SNAP ) );
	vec3_t best;
	qboolean found = qfalse;
	int i;

	if ( !cg.snap ) {
		return;
	}
	for ( i = -1; i < cg.snap->numEntities; i++ ) {
		const int n = ( i < 0 ) ? cg.predictedPlayerState.clientNum : cg.snap->entities[i].number;	// (-1: you)
		vec3_t mid, to;
		trace_t tr;
		float d, dot;
		const int kind = CG_VRF_Target( shooter, power, n, mid );

		if ( kind == CGF_NONE ) {
			continue;
		}
		VectorSubtract( mid, from, to );
		d = VectorNormalize( to );
		if ( d < 1.0f || d > range ) {
			continue;
		}
		dot = DotProduct( to, dir );
		if ( dot <= bestDot ) {
			continue;
		}
		CG_Trace( &tr, from, NULL, NULL, mid, shooter, MASK_SHOT );
		if ( tr.fraction < 1.0f && tr.entityNum != n ) {
			continue;	// (out of sight of the hand)
		}
		bestDot = dot;
		VectorCopy( to, best );
		found = (qboolean)( kind == CGF_SEEN );	// (hidden: the line as it is)
	}
	if ( found ) {
		VectorCopy( best, dir );
	}
}

/*
--------------------------------------------------------------------------------
The wheel's Force ring
--------------------------------------------------------------------------------
*/

// The Force powers the ring picks from - all but the gestures' (and Jump,
// and the saber's) - in the order it shows them.
static const int vrfRing[] = { FP_HEAL, FP_SPEED, FP_PROTECT, FP_ABSORB, FP_TELEPATHY, FP_SEE, FP_TEAM_HEAL, FP_RAGE,
	FP_TEAM_FORCE };

// The ones of them you know (up to max), in that order (powers: NULL, only how
// many).
int CG_VR_ForceRingPowers( int powers[], int max ) {
	int i, n = 0;

	if ( !cg.snap ) {
		return 0;
	}
	for ( i = 0; i < (int)ARRAY_LEN( vrfRing ) && n < max; i++ ) {
		if ( cg.snap->ps.fd.forcePowersKnown & ( 1 << vrfRing[i] ) ) {
			if ( powers ) {
				powers[n] = vrfRing[i];
			}
			n++;
		}
	}
	return n;
}

const char *CG_VR_ForcePowerName( int power ) {
	switch ( power ) {
	case FP_HEAL:			return "Heal";
	case FP_LEVITATION:		return "Jump";
	case FP_SPEED:			return "Speed";
	case FP_PUSH:			return "Push";
	case FP_PULL:			return "Pull";
	case FP_TELEPATHY:		return "Mind Trick";
	case FP_GRIP:			return "Grip";
	case FP_LIGHTNING:		return "Lightning";
	case FP_RAGE:			return "Rage";
	case FP_PROTECT:		return "Protect";
	case FP_ABSORB:			return "Absorb";
	case FP_TEAM_HEAL:		return "Team Heal";
	case FP_TEAM_FORCE:		return "Team Energize";
	case FP_DRAIN:			return "Drain";
	case FP_SEE:			return "Seeing";
	default:				return "nothing";
	}
}

// Why a power on the ring can't be picked now - or, power -1, why the ring
// can't open (NULL: it can).
const char *CG_VR_ForceRingWhy( int power ) {
	if ( cg.snap && CG_VRF_Watching() ) {
		return "Not while spectating";
	}
	if ( power < 0 ) {
		return CG_VR_ForceRingPowers( NULL, (int)ARRAY_LEN( vrfRing ) ) ? NULL : "You know none of these Force powers";
	}
	if ( CG_RP_IsDown() ) {
		return "Not while you're down";
	}
	if ( ( power == FP_TEAM_HEAL || power == FP_TEAM_FORCE ) && cgs.gametype < GT_TEAM ) {
		return "Only in team games";
	}
	return NULL;
}

// A power picked on the ring: the Force grip's quick squeeze (and holding Use
// Force) uses it now - as X and Y would have picked it.
void CG_VR_ForcePick( int power ) {
	if ( !cg.snap || CG_VRF_Watching() || power < 0 || power >= NUM_FORCE_POWERS || !( cg.snap->ps.fd.forcePowersKnown & ( 1 << power ) ) ) {
		return;	// (watching: the power picked is theirs)
	}
	cg.snap->ps.fd.forcePowerSelected = power;	// (as forcenext has it, till the server's word)
	cg.forceSelect = power;
	cg.forceSelectTime = cg.time;
	CG_VR_Log( "force: picked %s on the wheel\n", CG_VR_ForcePowerName( power ) );
}
