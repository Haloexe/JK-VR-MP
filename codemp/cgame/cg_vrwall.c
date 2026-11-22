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

- An empty hand within reach of anything solid you can see (VR_CLIMB_TOUCH
  every way round it, however high or low, whichever way it's turned - not
  the sky, not the invisible: bg_pmove.c BG_VRClimbHandAt), on the ground or
  off it, in water or out, takes hold of it with a squeeze - or, off the
  ground or your other hand holding on, squeezed already as it gets there
  (the engine catches hold: VR_GRAB_CATCH) - where it is then (its hold:
  vrw.anchor). A light tick in the hand as it comes within reach: a squeeze
  there takes hold.
- While a hand holds on, your body moves to keep it there: pull down and
  you go up, push away and you go back, along and you shimmy - each frame,
  how fast your hands move you (the hand's own movement, and what's left to
  catch up: VRW_TAU) goes to the engine (VR_SetClimb), which sends it in
  your commands' moves with the grip (vr_public.h VR_CLIMB_*); the game
  moves you by it, as the server does (no falling meanwhile, nor the sticks
  moving you). Hand over hand: the hand that took hold last leads, the
  other one (still holding on) following along - no tug between them.
- A hand pulled further than VRW_SLACK from its hold (your body stopped by
  something, your view turned) slips: its hold comes along with it. A hand
  nowhere near anything (VR_CLIMB_HOLD) a while (VRW_LOSE_MS) loses its hold
  - the server lets go of you a little later, holding on to nothing.
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

#define VRW_LOSE_MS		1000	// climbing, holding on: a hand nowhere near anything this long loses its hold (VR_CLIMB_HOLD;
								// the server lets go of you at 1500, VR_CLIMB_HOLD_SERVER)
#define VRW_TICK_MS		300		// a hand's tick as it comes within reach of something: no more often than this
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
	vec3_t		anchor[2];		// where the hand holds on (world) - your body keeps it there (the leading hand's)
	int			grabTime[2];	// when it took hold (cg.time): the last to take hold leads
	int			touchTime[2];	// the hand last at something (cg.time)
	int			tickTime[2];	// its tick, coming within reach of something, last (cg.time)
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

// Climbing: whether a hand at hand (world) is within reach of something you
// could climb (bg_pmove.c BG_VRClimbHandAt - as the server has it, with more
// reach: g_vrwall.c) - reach VR_CLIMB_TOUCH taking hold, VR_CLIMB_HOLD holding on.
static qboolean CG_VRW_At( const vec3_t hand, float reach ) {
	vec3_t dir;

	VectorSubtract( hand, cg.refdef.vieworg, dir );
	VectorNormalize( dir );
	return BG_VRClimbHandAt( CG_Trace, hand, dir, reach, cg.clientNum );
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
// (vrw.climb) - to keep the hand that took hold last at its hold (any other
// holding on follows along, its hold coming with it: no tug between them).
// The hand's own movement (as tracked, since last frame: your view turning
// doesn't count) and what's left to catch up (VRW_TAU) - a hand pulled
// further than VRW_SLACK from its hold slipping, its hold coming with it.
// Each hand holding on has to be near something now and then, or it loses
// its hold.
static void CG_VRW_Climb( const vrClientState_t *vr ) {
	const float turn = vr ? DEG2RAD( VR_AxisYaw( cg.refdef.viewaxis ) - vr->referenceYaw ) : 0.0f;
	const float c = cosf( turn ), s = sinf( turn );
	vec3_t hand[2], err, ff, vel, at;
	qboolean holds[2] = { qfalse, qfalse };
	float lift, speed, len;
	int h, lead = -1;

	VectorClear( vrw.climb.vel );
	for ( h = 0; h < 2; h++ ) {
		if ( !vrw.held[h] || vrw.spent[h] ) {
			vrw.gripTime[h] = 0;
			continue;
		}
		if ( !vr || !vr->handValid[h] || !CG_VR_HandGrip( h, hand[h], NULL ) ) {
			vrw.gripTime[h] = 0;	// (not tracked a moment: holding on as it was)
			continue;
		}
		// still near something now and then (the server's word too)
		if ( CG_VR_AvatarFreeHand( h, at ) && CG_VRW_At( at, VR_CLIMB_HOLD ) ) {
			vrw.touchTime[h] = cg.time;
		} else if ( cg.time - vrw.touchTime[h] > VRW_LOSE_MS || cg.time - vrw.touchTime[h] < 0 ) {
			CG_VRW_Lose( h, "at nothing" );
			CG_VR_Feel( h, 0.3f, 30, 0.1f, 80 );
			continue;
		}
		holds[h] = qtrue;
		if ( lead < 0 || vrw.grabTime[h] - vrw.grabTime[lead] > 0 ) {
			lead = h;
		}
	}
	vrw.lifting = qfalse;
	if ( lead < 0 ) {
		vrw.liftTime = 0;
		return;
	}
	for ( h = 0; h < 2; h++ ) {
		if ( holds[h] && h != lead ) {
			// (following along: its hold where it is - leading next, from there)
			VectorCopy( hand[h], vrw.anchor[h] );
			VectorCopy( vr->gripOrigin[h], vrw.gripWas[h] );
			vrw.gripTime[h] = 0;
		}
	}
	VectorSubtract( vrw.anchor[lead], hand[lead], err );
	len = VectorLength( err );
	if ( len > VRW_SLACK ) {
		VectorMA( vrw.anchor[lead], -( 1.0f - VRW_SLACK / len ), err, vrw.anchor[lead] );
		VectorScale( err, VRW_SLACK / len, err );
	}
	// the hand's own movement since last frame, as tracked (render space,
	// metres: into the world's way round and units)
	VectorClear( ff );
	if ( vrw.gripTime[lead] && cg.time - vrw.gripTime[lead] > 0 && cg.time - vrw.gripTime[lead] <= 100 ) {
		const float k = vr->worldScale * 1000.0f / ( cg.time - vrw.gripTime[lead] );
		vec3_t d;

		VectorSubtract( vr->gripOrigin[lead], vrw.gripWas[lead], d );
		ff[0] = -( d[0] * c - d[1] * s ) * k;
		ff[1] = -( d[0] * s + d[1] * c ) * k;
		ff[2] = -d[2] * k;
	}
	VectorCopy( vr->gripOrigin[lead], vrw.gripWas[lead] );
	vrw.gripTime[lead] = cg.time;
	VectorMA( ff, 1.0f / VRW_TAU, err, vel );

	// pushing on into a low wall: lifted on to it, your hold coming up with
	// you (not pulling you back down - nor a moment after: VRW_LIFT_HOLD)
	lift = CG_VRW_Lift( vel );
	if ( lift > 0.0f ) {
		vrw.liftTime = cg.time ? cg.time : 1;
	}
	if ( vrw.liftTime && cg.time - vrw.liftTime >= 0 && cg.time - vrw.liftTime < VRW_LIFT_HOLD ) {
		vrw.lifting = qtrue;
		if ( err[2] < 0.0f ) {
			vrw.anchor[lead][2] -= err[2];
			vel[2] -= err[2] / VRW_TAU;
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
	int h, wasZones;

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
			vrw.grabTime[h] = cg.time;	// (climbing: it leads from now)
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
	// at a wall you could hang on to - climbing, within reach of anything you
	// can climb (a light tick in the hand as it gets there)
	wasZones = vrw.zones;
	vrw.zones = 0;
	if ( on && vr && !climbs ) {
		wall = CG_VRW_Wall( normal );
	}
	for ( h = 0; on && vr && h < 2; h++ ) {
		if ( vrw.held[h] || !vr->handValid[h] || ( others & ( 1 << h ) ) || !CG_VR_AvatarFreeHand( h, hand ) ) {
			continue;
		}
		if ( climbs ? CG_VRW_At( hand, VR_CLIMB_TOUCH ) : ( wall && CG_VRW_HandAt( hand, normal ) ) ) {
			vrw.zones |= 1 << h;
			if ( climbs && !( wasZones & ( 1 << h ) ) && ( !vrw.tickTime[h] || cg.time - vrw.tickTime[h] >= VRW_TICK_MS ||
				cg.time - vrw.tickTime[h] < 0 ) ) {
				CG_VR_Feel( h, 0.2f, 20, 0.0f, 0 );
				vrw.tickTime[h] = cg.time ? cg.time : 1;
			}
		}
	}
	vrw.zoneTime = cg.time;
}

// Hands whose squeeze takes hold of a wall now (bit per hand): the engine's
// told (VR_SetGrabZones).
int CG_VR_WallZones( void ) {
	return ( cg.time - vrw.zoneTime >= 0 && cg.time - vrw.zoneTime <= VRW_WORD_MS ) ? vrw.zones : 0;
}

// ...climbing, those that catch hold squeezed already as they get there
// (VR_GRAB_CATCH( h ): what the squeeze was doing let go of) - off the
// ground, or your other hand holding on. (On the ground, holding on to
// nothing, a grip held is busy with its own work - the Force reaching out,
// Use held at a panel - and a hand near a wall doesn't take that away: a
// squeeze there takes hold.) For the engine with them.
int CG_VR_WallCatch( void ) {
	const int zones = vrw.climbs ? CG_VR_WallZones() : 0;
	const qboolean airborne = (qboolean)( cg.predictedPlayerState.groundEntityNum == ENTITYNUM_NONE );
	int h, catches = 0;

	for ( h = 0; h < 2; h++ ) {
		const int other = ( h == VR_HAND_LEFT ) ? VR_HAND_RIGHT : VR_HAND_LEFT;
		if ( ( zones & ( 1 << h ) ) && ( airborne || ( vrw.held[other] && !vrw.spent[other] ) ) ) {
			catches |= VR_GRAB_CATCH( h );
		}
	}
	return catches;
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
