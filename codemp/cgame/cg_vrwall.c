/*
===========================================================================
OpenJK VR (multiplayer) - hanging on to a wall, and climbing: your game's
side (the rules both sides go by: bg_pmove.c PM_VRWallGrip, PM_VRClimb; the
server's: g_vrwall.c).

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

Climbing (a server saying "vrWallGrip 2", cg_vrClimb on - CG_VR_Climbs):

- An empty hand at anything solid you can see (not the sky, not the
  invisible: CG_VRW_At), on the ground or off it, in water or out, takes
  hold of it with a squeeze - where it is then (its hold: vrw.anchor).
- While a hand holds on, your body moves to keep it there: pull down and
  you go up, push away and you go back, along and you shimmy - each frame,
  how fast your hands move you (the hand's own movement, and what's left to
  catch up: VRW_TAU) goes to the engine (VR_SetClimb), which sends it in
  your commands' moves with the grip (vr_public.h VR_CLIMB_*); the game
  moves you by it, as the server does (no falling meanwhile, nor the sticks
  moving you). Hand over hand: each hand holds on where it took hold.
- A hand pulled further than VRW_SLACK from its hold (your body stopped by
  something, your view turned) slips: its hold comes along with it. A hand
  at nothing a while (VRW_LOSE_MS) loses its hold - the server lets go of
  you a little later, holding on to nothing.
- Pushing on into a low wall (its top no higher than VRW_LEDGE over your
  feet, somewhere to stand there): you're lifted up on to it - your holds
  coming up with you meanwhile, not pulling you back down.
- Letting go of everything, you go on as your hands moved you: let go
  mid-pull, a fling (Force Jump adds to it: bg_pmove.c PM_VRClimbFling).
  Jump pushes off. Knocked off (hit, Force pushed, jumped off: the game let
  go of you, your grip still held), your hands' holds are lost till
  squeezed afresh.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "cg_local.h"

#define VRW_TOUCH		8.0f	// a hand this near a wall (units past it, reaching from your eyes) is at it
#define VRW_WORD_MS		100		// zones said this lately are still offered

#define VRW_HOLD_TOUCH	16.0f	// climbing, holding on: a hand this near something is still at it (the server's own: g_vrwall.c)
#define VRW_LOSE_MS		1000	// ...at nothing this long, it loses its hold (the server lets go of you at 1500)
#define VRW_SLACK		6.0f	// climbing: a hand kept this near its hold - pulled further, the hold slips with it
#define VRW_TAU			0.08f	// seconds: your body catches up with your hands' holds this quickly (what's left of it)
#define VRW_LEDGE		48.0f	// a ledge's top no higher than this over your feet: pushing on into it lifts you on to it...
#define VRW_AHEAD		4.0f	// ...pushing on into a wall this near (your box: as the game moves you)...
#define VRW_LIFT		1.5f	// ...as fast as this many times how fast you push on into it...
#define VRW_LIFT_MIN	20.0f	// ...pushing no slower than this (units a second), more into it than along it
#define VRW_LIFT_HOLD	150		// ms your holds keep coming up with you after a lift (it's carried you up meanwhile)
#define VRW_WALKABLE	0.7f	// (a slope you could stand on: the game's MIN_WALK_NORMAL)

static struct {
	int			time;			// (worked out this frame: cg.time)
	int			zones;			// hands at a wall a squeeze takes hold of (bit per hand)...
	int			zoneTime;		// ...as of then
	qboolean	held[2];		// the hand's grip on a wall, squeezed there and held since
	qboolean	grabWas[2];		// the hand's grab as it was last frame (a new one: taken here)

	// climbing
	qboolean	climbs;			// climbing this frame (CG_VRW_Climbs)
	qboolean	spent[2];		// held, but its hold lost since (knocked off, jumped off, at nothing): none till squeezed afresh
	vec3_t		anchor[2];		// where the hand holds on (world) - your body keeps it there
	int			touchTime[2];	// the hand last at something (cg.time)
	vec3_t		gripWas[2];		// the hand as tracked last frame (render space)...
	int			gripTime[2];	// ...as of then (cg.time; 0: none)
	vrClimb_t	climb;			// how fast your hands move you, said with your buttons (VR_SetClimb)
	qboolean	holdingWas;		// a hand of yours held on last frame
	qboolean	lifting;		// lifting you on to a ledge (as last worked out)...
	int			liftTime;		// ...last lifted then (cg.time; 0: not)
} vrw;

// Whether this server hangs you on walls you grip, and you play in VR -
// alive, your own view, in a game.
static qboolean CG_VRW_On( void ) {
	const playerState_t *ps = &cg.predictedPlayerState;

	return (qboolean)( cgs.vrWallGrip && CG_VR() && CG_VR_Engine() && cg.snap && !cg.demoPlayback && !( cg.snap->ps.pm_flags & PMF_FOLLOW ) &&
		ps->clientNum == cg.clientNum && ps->pm_type == PM_NORMAL && ps->stats[STAT_HEALTH] > 0 &&
		ps->persistant[PERS_TEAM] != TEAM_SPECTATOR && !ps->m_iVehicleNum );
}

// Whether the game moves you as a climber (the server too: g_vrwall.c
// G_VRClimbs) - your cg_vrClimb (your userinfo's) on a server that climbs.
// Prediction goes by it (cg_predict.c), in VR or not.
qboolean CG_VR_Climbs( void ) {
	return (qboolean)( cg_vrClimb.integer > 0 && cgs.vrWallGrip >= 2 );
}

// Whether you climb now: the game moves you so, you're in VR (CG_VRW_On), and
// the engine can send how fast your hands move you.
static qboolean CG_VRW_Climbs( void ) {
	return (qboolean)( CG_VR_Climbs() && CG_VRW_On() && trap->ext.VR_SetClimb );
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

// Climbing: whether a hand at hand (world) is at something you could climb -
// reached from your eyes, something solid at the hand or within touch past it,
// not the sky nor anything you can't see (as the server has it: g_vrwall.c).
static qboolean CG_VRW_At( const vec3_t hand, float touch ) {
	vec3_t dir, end;
	trace_t tr;

	VectorSubtract( hand, cg.refdef.vieworg, dir );
	if ( VectorNormalize( dir ) < 1.0f ) {
		return qfalse;
	}
	VectorMA( hand, touch, dir, end );
	CG_Trace( &tr, cg.refdef.vieworg, NULL, NULL, end, cg.clientNum, VR_WALLGRIP_MASK );
	return (qboolean)( tr.fraction < 1.0f && !tr.startsolid && !( tr.surfaceFlags & ( SURF_SKY | SURF_NODRAW ) ) );
}

// Whether a hand of yours holds on, climbing.
static qboolean CG_VRW_Holding( void ) {
	return (qboolean)( ( vrw.held[0] && !vrw.spent[0] ) || ( vrw.held[1] && !vrw.spent[1] ) );
}

// A hand's hold lost (knocked off, at nothing): held, till squeezed afresh.
static void CG_VRW_Lose( int h, const char *why ) {
	if ( vrw.held[h] && !vrw.spent[h] ) {
		vrw.spent[h] = qtrue;
		vrw.gripTime[h] = 0;
		CG_VR_Log( "climbing: your %s hand lost its hold (%s)\n", ( h == VR_HAND_LEFT ) ? "left" : "right", why );
	}
}

// Pushing on into a low wall (vel: how fast your hands move you), its top no
// higher than VRW_LEDGE over your feet, room to rise to it and to stand
// there: how fast you're lifted up on to it (0: you aren't) - as the game
// moves your box (PM_VRClimbMove).
static float CG_VRW_Lift( const vec3_t vel ) {
	const playerState_t *ps = &cg.predictedPlayerState;
	vec3_t mins, maxs, dir, end, top;
	trace_t tr;
	float speed, push;

	VectorSet( dir, vel[0], vel[1], 0.0f );
	speed = VectorNormalize( dir );
	if ( speed < VRW_LIFT_MIN ) {
		return 0.0f;
	}
	VectorSet( mins, -15.0f, -15.0f, MINS_Z );
	VectorSet( maxs, 15.0f, 15.0f, ( ps->pm_flags & ( PMF_DUCKED | PMF_ROLLING ) ) ? ps->crouchheight : ps->standheight );
	VectorMA( ps->origin, VRW_AHEAD, dir, end );
	CG_Trace( &tr, ps->origin, mins, maxs, end, ps->clientNum, MASK_PLAYERSOLID );
	if ( tr.allsolid || tr.startsolid || tr.fraction >= 1.0f || fabsf( tr.plane.normal[2] ) >= VRW_WALKABLE ) {
		return 0.0f;	// (nothing right there, or not a wall)
	}
	push = -( vel[0] * tr.plane.normal[0] + vel[1] * tr.plane.normal[1] );
	if ( push < VRW_LIFT_MIN || push < 0.5f * speed ) {
		return 0.0f;	// (along it more than into it: shimmying)
	}
	VectorCopy( ps->origin, top );
	top[2] += VRW_LEDGE;
	CG_Trace( &tr, ps->origin, mins, maxs, top, ps->clientNum, MASK_PLAYERSOLID );
	if ( tr.allsolid || tr.startsolid ) {
		return 0.0f;
	}
	VectorCopy( tr.endpos, top );	// (as high as there's room to rise)
	VectorMA( top, VRW_AHEAD, dir, end );
	CG_Trace( &tr, top, mins, maxs, end, ps->clientNum, MASK_PLAYERSOLID );
	if ( tr.allsolid || tr.startsolid || tr.fraction < 1.0f ) {
		return 0.0f;	// (higher than that: a wall to climb, not a ledge)
	}
	VectorCopy( end, top );
	end[2] = ps->origin[2];
	CG_Trace( &tr, top, mins, maxs, end, ps->clientNum, MASK_PLAYERSOLID );
	if ( tr.allsolid || tr.startsolid || tr.fraction >= 1.0f || tr.plane.normal[2] < VRW_WALKABLE ) {
		return 0.0f;	// (nothing to stand on there)
	}
	return Q_min( VRW_LIFT * push, VR_CLIMB_MAX );
}

// Climbing, once a frame (CG_VR_WallFrame): how fast your hands move you
// (vrw.climb) - to keep each hand that holds on at its hold. The hand's own
// movement (as tracked, since last frame: your view turning doesn't count)
// and what's left to catch up (VRW_TAU) - a hand pulled further than
// VRW_SLACK from its hold slipping, its hold coming with it.
static void CG_VRW_Climb( const vrClientState_t *vr ) {
	const float turn = vr ? DEG2RAD( VR_AxisYaw( cg.refdef.viewaxis ) - vr->referenceYaw ) : 0.0f;
	const float c = cosf( turn ), s = sinf( turn );
	vec3_t hand[2], err[2], ff, vel, at;
	qboolean holds[2] = { qfalse, qfalse };
	float lift, speed;
	int h, n = 0;

	VectorClear( vrw.climb.vel );
	VectorClear( ff );
	for ( h = 0; h < 2; h++ ) {
		float len;

		if ( !vrw.held[h] || vrw.spent[h] ) {
			vrw.gripTime[h] = 0;
			continue;
		}
		if ( !vr || !vr->handValid[h] || !CG_VR_HandGrip( h, hand[h], NULL ) ) {
			vrw.gripTime[h] = 0;	// (not tracked a moment: holding on as it was)
			continue;
		}
		// still at something now and then (the server's word too)
		if ( CG_VR_AvatarFreeHand( h, at ) && CG_VRW_At( at, VRW_HOLD_TOUCH ) ) {
			vrw.touchTime[h] = cg.time;
		} else if ( cg.time - vrw.touchTime[h] > VRW_LOSE_MS || cg.time - vrw.touchTime[h] < 0 ) {
			CG_VRW_Lose( h, "at nothing" );
			CG_VR_Feel( h, 0.3f, 30, 0.1f, 80 );
			continue;
		}
		VectorSubtract( vrw.anchor[h], hand[h], err[h] );
		len = VectorLength( err[h] );
		if ( len > VRW_SLACK ) {
			VectorMA( vrw.anchor[h], -( 1.0f - VRW_SLACK / len ), err[h], vrw.anchor[h] );
			VectorScale( err[h], VRW_SLACK / len, err[h] );
		}
		// the hand's own movement since last frame, as tracked (render space,
		// metres: into the world's way round and units)
		if ( vrw.gripTime[h] && cg.time - vrw.gripTime[h] > 0 && cg.time - vrw.gripTime[h] <= 100 ) {
			const float k = vr->worldScale * 1000.0f / ( cg.time - vrw.gripTime[h] );
			vec3_t d;

			VectorSubtract( vr->gripOrigin[h], vrw.gripWas[h], d );
			ff[0] -= ( d[0] * c - d[1] * s ) * k;
			ff[1] -= ( d[0] * s + d[1] * c ) * k;
			ff[2] -= d[2] * k;
		}
		VectorCopy( vr->gripOrigin[h], vrw.gripWas[h] );
		vrw.gripTime[h] = cg.time;
		holds[h] = qtrue;
		n++;
	}
	vrw.lifting = qfalse;
	if ( !n ) {
		vrw.liftTime = 0;
		return;
	}
	VectorClear( vel );
	for ( h = 0; h < 2; h++ ) {
		if ( holds[h] ) {
			VectorMA( vel, 1.0f / VRW_TAU, err[h], vel );
		}
	}
	VectorAdd( vel, ff, vel );
	VectorScale( vel, 1.0f / n, vel );

	// pushing on into a low wall: lifted on to it, your holds coming up with
	// you (not pulling you back down - nor a moment after: VRW_LIFT_HOLD)
	lift = CG_VRW_Lift( vel );
	if ( lift > 0.0f ) {
		vrw.liftTime = cg.time ? cg.time : 1;
	}
	if ( vrw.liftTime && cg.time - vrw.liftTime >= 0 && cg.time - vrw.liftTime < VRW_LIFT_HOLD ) {
		vrw.lifting = qtrue;
		for ( h = 0; h < 2; h++ ) {
			if ( holds[h] && err[h][2] < 0.0f ) {
				vrw.anchor[h][2] -= err[h][2];
				vel[2] -= err[h][2] / ( VRW_TAU * n );
			}
		}
		vel[2] = Q_max( vel[2], lift );
	} else {
		vrw.liftTime = 0;
	}
	speed = VectorLength( vel );
	if ( speed > VR_CLIMB_MAX ) {
		VectorScale( vel, VR_CLIMB_MAX / speed, vel );
	}
	VectorCopy( vel, vrw.climb.vel );
}

/*
=================
CG_VR_WallFrame

Once a frame, as it ends (CG_VR_BodyEndFrame: your avatar's hands worked
out, your gun's grabs too), before the engine's told where a squeeze takes
hold of something: a squeeze at a wall taken, a grip let go of, and where a
squeeze takes hold of a wall now - climbing, how fast your hands move you.
=================
*/
void CG_VR_WallFrame( const vrClientState_t *vr ) {
	const playerState_t *ps = &cg.predictedPlayerState;
	const int others = CG_VR_TwoHandZones() | CG_VRH_Zones();
	const int offered = ( cg.time - vrw.zoneTime >= 0 && cg.time - vrw.zoneTime <= VRW_WORD_MS ) ? vrw.zones : 0;
	const qboolean on = CG_VRW_On();
	const qboolean climbs = (qboolean)( on && CG_VRW_Climbs() );
	vec3_t normal, hand;
	qboolean wall = qfalse;
	int h;

	if ( vrw.time == cg.time ) {
		return;	// (the second eye: as worked out)
	}
	vrw.time = cg.time;
	if ( climbs && !vrw.climbs ) {
		// (climbing from now - the server's said so, or you've turned it on -
		// a hand already holding on holds on where it is)
		for ( h = 0; h < 2; h++ ) {
			if ( vrw.held[h] && CG_VR_HandGrip( h, vrw.anchor[h], NULL ) ) {
				vrw.touchTime[h] = cg.time;
				vrw.gripTime[h] = 0;
			}
		}
	}
	vrw.climbs = climbs;

	// climbing, knocked off - the game let go of you, your grip still held (a
	// hit, a push, jumping off, holding on to nothing a while): your hands'
	// holds lost till squeezed afresh
	if ( climbs && CG_VRW_Holding() && ( ps->pm_flags & ( PMF_VR_WALLGRIP | PMF_STUCK_TO_WALL ) ) == PMF_VR_WALLGRIP ) {
		CG_VR_Log( "climbing: you were let go of (knocked off, jumped off)\n" );
		CG_VRW_Lose( VR_HAND_LEFT, "let go of" );
		CG_VRW_Lose( VR_HAND_RIGHT, "let go of" );
	}

	for ( h = 0; h < 2; h++ ) {
		const qboolean grabbing = (qboolean)( vr && vr->grabbing[h] );
		if ( !grabbing || !on ) {
			if ( vrw.held[h] && !grabbing ) {
				CG_VR_Log( "wall: your %s hand let go\n", ( h == VR_HAND_LEFT ) ? "left" : "right" );
			}
			vrw.held[h] = qfalse;
			vrw.spent[h] = qfalse;
			vrw.gripTime[h] = 0;
		} else if ( !vrw.grabWas[h] && ( offered & ( 1 << h ) ) ) {
			// a squeeze where it was offered (the engine grabbed for it): yours,
			// till it's let go - nothing else takes hold with it
			vrw.held[h] = qtrue;
			vrw.spent[h] = qfalse;
			vrw.gripTime[h] = 0;
			CG_VR_BlockGrab( h );
			CG_VR_Feel( h, 0.6f, 40, 0.2f, 120 );
			if ( climbs && CG_VR_HandGrip( h, vrw.anchor[h], NULL ) ) {
				vrw.touchTime[h] = cg.time;
				CG_VR_Log( "climbing: your %s hand took hold\n", ( h == VR_HAND_LEFT ) ? "left" : "right" );
			} else {
				CG_VR_Log( "wall: your %s hand took hold\n", ( h == VR_HAND_LEFT ) ? "left" : "right" );
			}
		}
		vrw.grabWas[h] = grabbing;
	}

	// climbing: how fast your hands move you - letting go of everything, you
	// go on as they moved you last (a fling, quick enough: the game's)
	if ( climbs ) {
		const qboolean holding = CG_VRW_Holding();

		if ( vrw.holdingWas && !holding && VectorLength( vrw.climb.vel ) >= 60.0f ) {
			CG_VR_Log( "climbing: you let go going %.0f units a second (%.0f up)\n", VectorLength( vrw.climb.vel ), vrw.climb.vel[2] );
		}
		CG_VRW_Climb( vr );
		vrw.holdingWas = holding;
	} else {
		VectorClear( vrw.climb.vel );
		vrw.holdingWas = qfalse;
		vrw.lifting = qfalse;
		vrw.liftTime = 0;
	}

	// where a squeeze takes hold now: an empty hand, in no other grab's zone,
	// at a wall you could hang on to - climbing, at anything you can climb
	vrw.zones = 0;
	if ( on && vr && !climbs ) {
		wall = CG_VRW_Wall( normal );
	}
	for ( h = 0; on && vr && h < 2; h++ ) {
		if ( vrw.held[h] || !vr->handValid[h] || ( others & ( 1 << h ) ) || !CG_VR_AvatarFreeHand( h, hand ) ) {
			continue;
		}
		if ( climbs ? CG_VRW_At( hand, VRW_TOUCH ) : ( wall && CG_VRW_HandAt( hand, normal ) ) ) {
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
// BUTTON_VR_WALLGRIP while a hand holds on to a wall (climbing: while one
// holds on - not one whose hold's lost).
int CG_VR_WallButtons( void ) {
	if ( !( vrw.held[0] || vrw.held[1] ) || cg.time - vrw.time < 0 || cg.time - vrw.time > VRW_WORD_MS || !CG_VRW_On() ) {
		return 0;
	}
	if ( vrw.climbs && !CG_VRW_Holding() ) {
		return 0;
	}
	return BUTTON_VR_WALLGRIP;
}

// Climbing, how fast your hands move you, said to the engine with your
// buttons (VR_SetClimb: every command with your grip carries it) - NULL, you
// don't climb now (your grip, if any, is hanging on's).
const vrClimb_t *CG_VR_WallClimb( void ) {
	if ( !vrw.climbs || cg.time - vrw.time < 0 || cg.time - vrw.time > VRW_WORD_MS || !CG_VRW_Climbs() ) {
		return NULL;
	}
	return &vrw.climb;
}

// A map begun: nothing held.
void CG_VR_WallInit( void ) {
	memset( &vrw, 0, sizeof( vrw ) );
}
