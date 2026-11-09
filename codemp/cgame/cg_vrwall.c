/*
===========================================================================
OpenJK VR (multiplayer) - hanging on to a wall: your game's side (the rule
both sides go by: bg_pmove.c PM_VRWallGrip; the server's: g_vrwall.c).

- In mid-air, a hand with nothing in it (as your avatar has it - the
  server sees the same: CG_VR_AvatarFreeHand) reaching to a wall you could
  hang on to (BG_VRWallFind: steep, within reach of your side), a squeeze
  of its grip takes hold of the wall instead of pressing its key (the
  engine's grab: VR_SetGrabZones - your Force grip, say). The hand's other
  grabs come first: your other hand's hilt, your gun, your holsters.
- As long as that grip's held your commands say so (BUTTON_VR_WALLGRIP),
  on a server that has it ("vrWallGrip 1" in its serverinfo): the game
  hangs you on the wall, and drops you as you let go.
- A buzz in that hand as it takes hold.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "cg_local.h"

#define VRW_TOUCH		8.0f	// a hand this near a wall (units past it, reaching from your eyes) is at it
#define VRW_WORD_MS		100		// zones said this lately are still offered

static struct {
	int			time;			// (worked out this frame: cg.time)
	int			zones;			// hands at a wall a squeeze takes hold of (bit per hand)...
	int			zoneTime;		// ...as of then
	qboolean	held[2];		// the hand's grip on a wall, squeezed there and held since
	qboolean	grabWas[2];		// the hand's grab as it was last frame (a new one: taken here)
} vrw;

// Whether this server hangs you on walls you grip, and you play in VR -
// alive, your own view, in a game.
static qboolean CG_VRW_On( void ) {
	const playerState_t *ps = &cg.predictedPlayerState;

	return (qboolean)( cgs.vrWallGrip && CG_VR() && CG_VR_Engine() && cg.snap && !cg.demoPlayback && !( cg.snap->ps.pm_flags & PMF_FOLLOW ) &&
		ps->clientNum == cg.clientNum && ps->pm_type == PM_NORMAL && ps->stats[STAT_HEALTH] > 0 &&
		ps->persistant[PERS_TEAM] != TEAM_SPECTATOR && !ps->m_iVehicleNum );
}

// Whether you could take hold of a wall now: in mid-air, out of water, a wall
// in reach (its normal: out of it, toward you).
static qboolean CG_VRW_Wall( vec3_t normal ) {
	const playerState_t *ps = &cg.predictedPlayerState;
	vec3_t mins, maxs, waist;

	if ( ps->groundEntityNum != ENTITYNUM_NONE ) {
		return qfalse;
	}
	VectorSet( waist, ps->origin[0], ps->origin[1], ps->origin[2] + MINS_Z + ( ps->viewheight - MINS_Z ) / 2 );
	if ( CG_PointContents( waist, ps->clientNum ) & MASK_WATER ) {
		return qfalse;
	}
	// (your box as the game moves you: PM_VRWallGrip)
	VectorSet( mins, -15.0f, -15.0f, MINS_Z );
	VectorSet( maxs, 15.0f, 15.0f, ( ps->pm_flags & ( PMF_DUCKED | PMF_ROLLING ) ) ? ps->crouchheight : ps->standheight );
	return BG_VRWallFind( CG_Trace, ps->origin, mins, maxs, ps->clientNum, normal );
}

// Whether a hand at hand (world) is at that wall: reached from your eyes,
// the wall's face (one facing its way, within 45 degrees - not a ceiling or
// the floor by it) at the hand or just past it.
static qboolean CG_VRW_HandAt( const vec3_t hand, const vec3_t normal ) {
	vec3_t dir, end;
	trace_t tr;

	VectorSubtract( hand, cg.refdef.vieworg, dir );
	if ( VectorNormalize( dir ) < 1.0f ) {
		return qfalse;
	}
	VectorMA( hand, VRW_TOUCH, dir, end );
	CG_Trace( &tr, cg.refdef.vieworg, NULL, NULL, end, cg.clientNum, VR_WALLGRIP_MASK );
	return (qboolean)( tr.fraction < 1.0f && !tr.startsolid && DotProduct( tr.plane.normal, normal ) > 0.7f );
}

/*
=================
CG_VR_WallFrame

Once a frame, as it ends (CG_VR_BodyEndFrame: your avatar's hands worked
out, your gun's grabs too), before the engine's told where a squeeze takes
hold of something: a squeeze at a wall taken, a grip let go of, and where a
squeeze takes hold of a wall now.
=================
*/
void CG_VR_WallFrame( const vrClientState_t *vr ) {
	const int others = CG_VR_TwoHandZones() | CG_VRH_Zones();
	const int offered = ( cg.time - vrw.zoneTime >= 0 && cg.time - vrw.zoneTime <= VRW_WORD_MS ) ? vrw.zones : 0;
	const qboolean on = CG_VRW_On();
	vec3_t normal, hand;
	qboolean wall = qfalse;
	int h;

	if ( vrw.time == cg.time ) {
		return;	// (the second eye: as worked out)
	}
	vrw.time = cg.time;

	for ( h = 0; h < 2; h++ ) {
		const qboolean grabbing = (qboolean)( vr && vr->grabbing[h] );
		if ( !grabbing || !on ) {
			if ( vrw.held[h] && !grabbing ) {
				CG_VR_Log( "wall: your %s hand let go\n", ( h == VR_HAND_LEFT ) ? "left" : "right" );
			}
			vrw.held[h] = qfalse;
		} else if ( !vrw.grabWas[h] && ( offered & ( 1 << h ) ) ) {
			// a squeeze where it was offered (the engine grabbed for it): yours,
			// till it's let go - nothing else takes hold with it
			vrw.held[h] = qtrue;
			CG_VR_BlockGrab( h );
			CG_VR_Feel( h, 0.6f, 40, 0.2f, 120 );
			CG_VR_Log( "wall: your %s hand took hold\n", ( h == VR_HAND_LEFT ) ? "left" : "right" );
		}
		vrw.grabWas[h] = grabbing;
	}

	// where a squeeze takes hold of a wall now: an empty hand at one you could
	// hang on to, in no other grab's zone
	vrw.zones = 0;
	if ( on && vr ) {
		wall = CG_VRW_Wall( normal );
	}
	for ( h = 0; wall && h < 2; h++ ) {
		if ( !vrw.held[h] && vr->handValid[h] && !( others & ( 1 << h ) ) && CG_VR_AvatarFreeHand( h, hand ) &&
			CG_VRW_HandAt( hand, normal ) ) {
			vrw.zones |= 1 << h;
		}
	}
	vrw.zoneTime = cg.time;
}

// Hands whose squeeze takes hold of a wall now (bit per hand): the engine's
// told (VR_SetGrabZones).
int CG_VR_WallZones( void ) {
	return ( cg.time - vrw.zoneTime >= 0 && cg.time - vrw.zoneTime <= VRW_WORD_MS ) ? vrw.zones : 0;
}

// What your commands say of it (VR_SetCmdButtons, with the holsters'):
// BUTTON_VR_WALLGRIP while a hand holds on to a wall.
int CG_VR_WallButtons( void ) {
	if ( !( vrw.held[0] || vrw.held[1] ) || cg.time - vrw.time < 0 || cg.time - vrw.time > VRW_WORD_MS || !CG_VRW_On() ) {
		return 0;
	}
	return BUTTON_VR_WALLGRIP;
}

// A map begun: nothing held.
void CG_VR_WallInit( void ) {
	memset( &vrw, 0, sizeof( vrw ) );
}
