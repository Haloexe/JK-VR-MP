/*
===========================================================================
OpenJK VR (multiplayer) - driving an AT-ST from its cockpit, in VR: your
game's side (the rules the server goes by too: bg_vrwalker.h, g_vrwalker.c;
the cockpit's models and pictures, the renderer's own: tr_vratst.cpp - the
AT-ST Cockpit Test study's, vr/atst).

Driving an AT-ST in VR on a server that lets you ("vrWalker 1" in its
serverinfo), Cockpit on (cg_vrCockpit):
- you sit in its head, in the cockpit: in the left seat, looking out of its
  left viewport - not watching it from behind. The AT-ST isn't drawn in your
  view (you're in it): its cockpit is - the cabin, the plain skin round it,
  the visors over the viewports.
- the cockpit goes where the AT-ST goes, as it faces - but not as its head
  sways from side to side and back and forth as it strides: only as it rises
  and falls, and that smoothed (VRK_RISE_SECONDS).
- the way you face as you get in is its front (and as you Recenter): your
  view turns as it turns, and else only with your head (the engine's side:
  ext.VR_SetCockpit).
- the yoke in front of you drives it: squeeze a grip with your hand on one
  of its grips to take hold of it, push it forward to walk - as fast as it's
  pushed - pull it back to back up, twist it to turn (the right grip pushed
  forward, the left pulled back, turns it left: standing, slower). Holding
  neither, the sticks drive it: the moving stick walks it and backs it up,
  the turning stick turns it. The yoke goes as it's driven, your hands on
  it as they hold it, and else where they are.
- the right trigger fires the main guns (left-handed: the left), the other
  trigger - or A - the second weapon. They aim where you look, as far round
  as the AT-ST lets them (bg_vrwalker.h): a dot where they'll hit.
- either grip squeezed away from the yoke, held a moment, gets you out:
  one press of Use, none more till it's let go (the engine's: held on, the
  game's Use would put you straight back in). No Force gestures in it.
- its six screens show how it is and what's round it as it goes: its
  armour and legs, its guns, the ground going by under it, who's before its
  guns, what's said, which way it faces and how fast (CG_VRK_Screens).
On a server without it (or Cockpit off) you drive it as the game has it,
watching it from behind: it turns toward where you look - as you do out of
VR, always. Your game says it goes by these rules to every server ("vrw 1"
in your userinfo), VR or not: the server takes the yoke from no other game
(and out of VR your engine never sends it - not even from a key bound to
its button).

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "cg_local.h"
#include "game/bg_vrwalker.h"

#define VRK_RISE_SECONDS	0.35f	// how its head rises and falls as it strides, smoothed this much (a time constant)
#define VRK_GRIP_REACH		4.5f	// a hand this near a yoke grip (units: some 13 cm) takes hold of it as its grip's squeezed...
#define VRK_GRIP_LEAVE		12.0f	// ...and lets go of it this far from it (else as it's let go of)
#define VRK_PUSH_RANGE		3.4f	// the hands this far ahead of where they took hold (some 10 cm): pushed all the way
#define VRK_TWIST_RANGE		3.4f	// one grip this far ahead of where it was and the other as far back: twisted all the way
#define VRK_DEAD			0.12f	// ...under this much of either, none (the hands aren't still)
#define VRK_TILT_MAX		22.0f	// the yoke pushed all the way tips this far (degrees)...
#define VRK_TWIST_MAX		28.0f	// ...twisted all the way, turns this far
#define VRK_SETTLE_SECONDS	0.1f	// let go of, or driven by the sticks, it eases to where it's driven (a time constant)
#define VRK_WORD_MS			100		// zones said this lately are still offered

static struct {
	qboolean	registered;
	qhandle_t	cockpit, yoke, handModel;	// (models/vr/atst: the renderer's own)
	vec3_t		eye, pivot, origin, neck;	// the cockpit's tags: its eyes, the yoke's pivot, the AT-ST's origin and the bone
											// its head turns on, standing (vr/atst/make_cockpit.py)
	vec3_t		grip[2];		// the yoke's: its grips (VR_HAND_*), from its pivot

	int			time;			// (worked out for this cg.time)
	qboolean	on;				// in the cockpit this frame
	int			vehNum;			// of this AT-ST...
	void		*ghoul2;		// ...its model, the bolt on the bone its head turns on (its neck)...
	int			neckBolt;
	float		rise;			// ...how high that is over its origin, smoothed (units)
	int			riseTime;		// as of (cg.time; 0: not yet - in it afresh)
	vec3_t		place;			// the cockpit's origin in the world...
	vec3_t		axis[3];		// ...its axes
	float		yaw;			// which way it faces (the AT-ST's yaw)
	float		viewYaw;		// the game's view in it (its yaw, your head's from its front)

	// the yoke
	qboolean	held[2];		// a hand holds its grip (taken hold of where offered)...
	qboolean	grabWas[2];		// ...the hand's grab as it was last frame
	vec3_t		hold[2];		// ...the hand from its grip as it took hold (the cockpit's axes)
	vec3_t		hand[2];		// the hands where they are (the cockpit's frame)...
	vec3_t		handAxis[2][3];	// ...and their axes (the world's)
	qboolean	handValid[2];
	int			zones;			// hands a squeeze takes hold of a grip with now...
	int			zoneTime;		// ...as of
	float		push, twist;	// the yoke as it's drawn (-1..1: forward, left)
	float		move, turn;		// what drives the AT-ST (-1..1: walking, turning left) - told the engine
	int			yokeTime;		// (cg.time it was eased to)
	qboolean	told;			// the engine's been told you sit in it

	// its steps (CG_VRK_Steps), and its walk as the log has it (CG_VRK_LogWalk)
	sfxHandle_t	stepSound[VR_WALKER_STEP_SOUNDS];	// (the game's own)
	int			stepFrame;		// its walk's frame as last seen (-1: not seen)...
	int			stepFrameTime;	// ...as of (cg.time; 0: not walking)
	int			stepTime;		// a step last played (cg.time)
	int			steps;			// steps played...
	int			stepsGame;		// ...and the game's own for them not (CG_VR_WalkerStepSound) - since...
	int			walkLogTime;	// ...this (cg.time; 0: not yet)
	int			walkLogLegs;	// its legs' animation, as logged
	float		legFrame;		// its walk's frame as last seen, between frames (less than 0: not seen)

	// its screens (CG_VRK_Screens)
	vrCockpitScreens_t	screens;	// what they show (said to the engine every frame)...
	int			screensTime;	// ...worked out for this cg.time (0: not yet - in it afresh)
	float		ground;			// metres walked (round again every so often: only where in a metre it is shows)
	float		stride;			// where its legs are in their stride (0..1)
	float		walk;			// how much it walks, eased
	float		voice;			// how loud the voices are that you hear, eased
	int			target;			// who's before its guns (ENTITYNUM_NONE: no one)
	char		chat[2][VR_SCREEN_LINE];	// the last two things said (CG_VR_WalkerChat), the latest last
} vrk;

static void CG_VRK_Screens( centity_t *veh, const vrClientState_t *vr );

extern stringID_table_t animTable[MAX_ANIMATIONS + 1];

/*
================================================================================

The cockpit, where it is

================================================================================
*/

// The cockpit's models, and where their tags are (the renderer's own: there
// on every server).
static void CG_VRK_Register( void ) {
	static const char *const tags[4] = { "tag_eye", "tag_yoke", "tag_origin", "tag_thoracic" };
	float *const at[4] = { vrk.eye, vrk.pivot, vrk.origin, vrk.neck };
	orientation_t tag;
	int i;

	if ( vrk.registered ) {
		return;
	}
	vrk.registered = qtrue;
	for ( i = 0; i < VR_WALKER_STEP_SOUNDS; i++ ) {
		vrk.stepSound[i] = trap->S_RegisterSound( va( "sound/chars/atst/fs_atst_0%i.wav", i + 1 ) );	// (its steps: CG_VRK_Steps)
	}
	vrk.cockpit = trap->R_RegisterModel( "models/vr/atst/cockpit.md3" );
	vrk.yoke = trap->R_RegisterModel( "models/vr/atst/yoke.md3" );
	vrk.handModel = trap->R_RegisterModel( "models/vr/atst/hand.md3" );
	for ( i = 0; i < 4; i++ ) {
		if ( !vrk.cockpit || !trap->R_LerpTag( &tag, vrk.cockpit, 0, 0, 1.0f, tags[i] ) ) {
			vrk.cockpit = 0;	// (an older renderer: no cockpit)
			return;
		}
		VectorCopy( tag.origin, at[i] );
	}
	for ( i = 0; i < 2; i++ ) {
		if ( !vrk.yoke || !trap->R_LerpTag( &tag, vrk.yoke, 0, 0, 1.0f, i == VR_HAND_LEFT ? "tag_lgrip" : "tag_rgrip" ) ) {
			vrk.cockpit = 0;
			return;
		}
		VectorCopy( tag.origin, vrk.grip[i] );
	}
}

// As the game's loaded (CG_RegisterGraphics): the cockpit's models, ready.
void CG_VR_WalkerRegister( void ) {
	vrk.registered = qfalse;
	CG_VRK_Register();
}

// The AT-ST you drive, if you drive one from its cockpit now: in VR, on a
// server that lets you (and Cockpit on), alive, in your own game.
static centity_t *CG_VRK_Walker( void ) {
	const playerState_t *ps = &cg.predictedPlayerState;
	centity_t *veh;

	if ( !cg_vrCockpit.integer || !cgs.vrWalker || !CG_VR() || !CG_VR_Engine() || !trap->ext.VR_SetCockpit || !cg.snap ||
		cg.demoPlayback || ( cg.snap->ps.pm_flags & PMF_FOLLOW ) || ps->clientNum != cg.clientNum || ps->stats[STAT_HEALTH] <= 0 ||
		ps->pm_type == PM_SPECTATOR || ps->pm_type == PM_INTERMISSION || ps->m_iVehicleNum <= 0 || ps->m_iVehicleNum >= ENTITYNUM_WORLD ) {
		return NULL;
	}
	veh = &cg_entities[ps->m_iVehicleNum];
	if ( !veh->m_pVehicle || !BG_VRWalkerCockpit( veh->m_pVehicle->m_pVehicleInfo ) || veh->currentState.owner != cg.clientNum ||
		!veh->ghoul2 ) {
		return NULL;	// (not an AT-ST, or someone else drives it)
	}
	CG_VRK_Register();
	return vrk.cockpit ? veh : NULL;
}

// Whether you'd sit in the cockpit this frame (cg_view.c: your view from it,
// not from behind).
qboolean CG_VR_WalkerWanted( void ) {
	return (qboolean)( CG_VRK_Walker() != NULL );
}

// How high the AT-ST's head is over its origin now, as its animation has it:
// the bone it turns on, "thoracic" (its neck - the head goes with it, 20
// above it, but for how its driver's look tips it, BG_G2ATSTAngles, which
// doesn't move that bone's own pivot) - qfalse if that can't be told.
static qboolean CG_VRK_HeadRise( centity_t *veh, float *rise ) {
	mdxaBone_t m;
	vec3_t angles, at;

	if ( vrk.ghoul2 != veh->ghoul2 ) {
		vrk.ghoul2 = veh->ghoul2;
		vrk.neckBolt = trap->G2API_AddBolt( veh->ghoul2, 0, "thoracic" );
	}
	if ( vrk.neckBolt < 0 ) {
		return qfalse;
	}
	VectorSet( angles, 0.0f, vrk.yaw, 0.0f );
	if ( !trap->G2API_GetBoltMatrix( veh->ghoul2, 0, vrk.neckBolt, &m, angles, cg.predictedVehicleState.origin, cg.time,
		cgs.gameModels, veh->modelScale ) ) {
		return qfalse;
	}
	BG_GiveMeVectorFromMatrix( &m, ORIGIN, at );
	*rise = at[2] - cg.predictedVehicleState.origin[2];
	return qtrue;
}

// Where the cockpit is this frame (once a frame - both eyes alike): where the
// AT-ST is, as it faces (as your game moves it), and as high as its head is
// (its rise and fall, smoothed) - and the game's view in it.
static void CG_VRK_Place( centity_t *veh, const vrClientState_t *vr ) {
	const float standing = vrk.neck[2] - vrk.origin[2];	// (its neck over its origin, standing)
	vec3_t angles;
	float rise;

	if ( vrk.time == cg.time && vrk.on && vrk.vehNum == veh->currentState.number ) {
		return;
	}
	if ( !vrk.on || vrk.vehNum != veh->currentState.number ) {
		vrk.riseTime = 0;	// (in it afresh)
		vrk.held[0] = vrk.held[1] = qfalse;
		vrk.push = vrk.twist = vrk.move = vrk.turn = 0.0f;
		vrk.yokeTime = 0;
		vrk.steps = vrk.stepsGame = vrk.walkLogTime = 0;
		vrk.stepFrame = -1;
		vrk.stepFrameTime = vrk.stepTime = 0;
		vrk.legFrame = -1.0f;
		vrk.screensTime = 0;
		vrk.ground = vrk.stride = vrk.walk = vrk.voice = 0.0f;
		vrk.target = ENTITYNUM_NONE;
		CG_VR_Log( "walker: in the cockpit of %i\n", veh->currentState.number );
	}
	vrk.on = qtrue;
	vrk.vehNum = veh->currentState.number;
	vrk.time = cg.time;
	vrk.yaw = AngleNormalize180( cg.predictedVehicleState.viewangles[YAW] );

	// its head's rise and fall, smoothed
	if ( CG_VRK_HeadRise( veh, &rise ) ) {
		if ( !vrk.riseTime || cg.time - vrk.riseTime < 0 || cg.time - vrk.riseTime > 1000 ) {
			vrk.rise = rise;
		} else {
			vrk.rise += ( rise - vrk.rise ) * ( 1.0f - expf( -( cg.time - vrk.riseTime ) * 0.001f / VRK_RISE_SECONDS ) );
		}
		vrk.riseTime = cg.time;
	} else if ( !vrk.riseTime ) {
		vrk.rise = standing;
	}

	// the cockpit: its tag_origin at the AT-ST's, raised as its head is
	VectorSet( angles, 0.0f, vrk.yaw, 0.0f );
	AnglesToAxis( angles, vrk.axis );
	VectorCopy( cg.predictedVehicleState.origin, vrk.place );
	VectorMA( vrk.place, -vrk.origin[0], vrk.axis[0], vrk.place );
	VectorMA( vrk.place, -vrk.origin[1], vrk.axis[1], vrk.place );
	vrk.place[2] += -vrk.origin[2] + ( vrk.rise - standing );

	// the game's view in it: its yaw, your head's from its front
	vrk.viewYaw = vrk.yaw;
	if ( vr && vr->cockpit ) {
		vrk.viewYaw = AngleNormalize180( vrk.yaw + AngleSubtract( vr->referenceYaw, vr->cockpitYaw ) );
	}
}

/*
=================
CG_VR_WalkerView

The view, as it's placed (CG_CalcViewValues): in the cockpit, from its
eyes, its way - qfalse if you're not in one.
=================
*/
qboolean CG_VR_WalkerView( void ) {
	centity_t *veh = CG_VRK_Walker();
	int i;

	if ( !veh ) {
		vrk.on = qfalse;
		return qfalse;
	}
	CG_VRK_Place( veh, CG_VR() );
	VectorCopy( vrk.place, cg.refdef.vieworg );
	for ( i = 0; i < 3; i++ ) {
		VectorMA( cg.refdef.vieworg, vrk.eye[i], vrk.axis[i], cg.refdef.vieworg );
	}
	cg.refdef.viewangles[PITCH] = cg.predictedPlayerState.viewangles[PITCH];
	cg.refdef.viewangles[YAW] = vrk.viewYaw;
	cg.refdef.viewangles[ROLL] = 0.0f;
	return qtrue;
}

// Whether you sit in the cockpit this frame (as the view was placed).
qboolean CG_VR_InCockpit( void ) {
	return (qboolean)( vrk.on && vrk.time == cg.time );
}

// Whether an entity is the AT-ST you're in: it's not drawn in your view
// (cg_players.c - only in a mirror, as your own body).
qboolean CG_VR_WalkerHides( const centity_t *cent ) {
	return (qboolean)( CG_VR_InCockpit() && cent->currentState.number == vrk.vehNum );
}

/*
================================================================================

The yoke

================================================================================
*/

// A point in the world in the cockpit's frame (or a way: axes only).
static void CG_VRK_ToCockpit( const vec3_t world, vec3_t out, qboolean point ) {
	vec3_t d;

	if ( point ) {
		VectorSubtract( world, vrk.place, d );
	} else {
		VectorCopy( world, d );
	}
	out[0] = DotProduct( d, vrk.axis[0] );
	out[1] = DotProduct( d, vrk.axis[1] );
	out[2] = DotProduct( d, vrk.axis[2] );
}

// ...and back.
static void CG_VRK_ToWorld( const vec3_t local, vec3_t out ) {
	VectorCopy( vrk.place, out );
	VectorMA( out, local[0], vrk.axis[0], out );
	VectorMA( out, local[1], vrk.axis[1], out );
	VectorMA( out, local[2], vrk.axis[2], out );
}

// The yoke's turn as it's pushed and twisted (its axes in the cockpit's
// frame): tipped forward about its pivot's y, then turned about its own up.
static void CG_VRK_YokeAxes( float push, float twist, vec3_t axis[3] ) {
	const float t = DEG2RAD( push * VRK_TILT_MAX ), w = DEG2RAD( twist * VRK_TWIST_MAX );
	const float ct = cosf( t ), st = sinf( t ), cw = cosf( w ), sw = sinf( w );

	// tip: x' = x ct + z st, z' = -x st + z ct (its top forward); turn: x' = x cw - y sw, y' = x sw + y cw
	VectorSet( axis[0], ct * cw, sw, -st * cw );
	VectorSet( axis[1], -ct * sw, cw, st * sw );
	VectorSet( axis[2], st, 0.0f, ct );
}

// Where a grip is (the cockpit's frame), the yoke as it's pushed and twisted.
static void CG_VRK_GripAt( int h, float push, float twist, vec3_t out ) {
	vec3_t axis[3];
	int i;

	CG_VRK_YokeAxes( push, twist, axis );
	VectorCopy( vrk.pivot, out );
	for ( i = 0; i < 3; i++ ) {
		VectorMA( out, vrk.grip[h][i], axis[i], out );
	}
}

// The hand whose stick moves you (the engine's vr_swapSticks: the right; else
// the left) - the other's turns you.
static int CG_VRK_MoveStickHand( void ) {
	char buf[16];

	trap->Cvar_VariableStringBuffer( "vr_swapSticks", buf, sizeof( buf ) );
	return atoi( buf ) ? VR_HAND_RIGHT : VR_HAND_LEFT;
}

static float CG_VRK_Dead( float v ) {
	if ( fabsf( v ) < VRK_DEAD ) {
		return 0.0f;
	}
	return ( v - ( v > 0.0f ? VRK_DEAD : -VRK_DEAD ) ) / ( 1.0f - VRK_DEAD );
}

// The yoke, once a frame: your hands where they are, a squeeze at a grip
// taken (and a hand let go), where it's pushed and twisted - by your hands
// on it, or else the sticks - and so what drives the AT-ST.
static void CG_VRK_Yoke( const vrClientState_t *vr ) {
	const int offered = ( cg.time - vrk.zoneTime >= 0 && cg.time - vrk.zoneTime <= VRK_WORD_MS ) ? vrk.zones : 0;
	const int moveHand = CG_VRK_MoveStickHand();
	const float dt = vrk.yokeTime ? Com_Clamp( 0.0f, 0.1f, ( cg.time - vrk.yokeTime ) * 0.001f ) : 0.0f;
	float push = 0.0f, twist = 0.0f, ahead[2] = { 0.0f, 0.0f };
	vec3_t grip, rest;
	int h, held = 0;

	vrk.yokeTime = cg.time;

	// your hands, in the cockpit
	for ( h = 0; h < 2; h++ ) {
		vrk.handValid[h] = (qboolean)( vr && vr->handValid[h] );
		if ( vrk.handValid[h] ) {
			vec3_t world, ax[3];
			int i;
			for ( i = 0; i < 3; i++ ) {
				VectorCopy( vr->gripAxis[h][i], ax[i] );
			}
			VR_ToWorld( vr, cg.refdef.vieworg, cg.refdef.viewaxis, vr->gripOrigin[h], ax, world, vrk.handAxis[h] );
			CG_VRK_ToCockpit( world, vrk.hand[h], qtrue );
		}
	}

	// a squeeze where it was offered: that grip's, till it's let go (or the hand pulled away)
	for ( h = 0; h < 2; h++ ) {
		const qboolean grabbing = (qboolean)( vr && vr->grabbing[h] );
		CG_VRK_GripAt( h, vrk.push, vrk.twist, grip );
		if ( vrk.held[h] && ( !grabbing || !vrk.handValid[h] || Distance( vrk.hand[h], grip ) > VRK_GRIP_LEAVE ) ) {
			vrk.held[h] = qfalse;
			CG_VR_Log( "walker: your %s hand let go of the yoke\n", ( h == VR_HAND_LEFT ) ? "left" : "right" );
		} else if ( !vrk.held[h] && grabbing && !vrk.grabWas[h] && ( offered & ( 1 << h ) ) && vrk.handValid[h] ) {
			vrk.held[h] = qtrue;
			VectorSubtract( vrk.hand[h], grip, vrk.hold[h] );
			CG_VR_BlockGrab( h );
			CG_VR_Feel( h, 0.5f, 30, 0.15f, 100 );
			CG_VR_Log( "walker: your %s hand took hold of the yoke\n", ( h == VR_HAND_LEFT ) ? "left" : "right" );
		}
		vrk.grabWas[h] = grabbing;
	}

	// where your hands have it: pushed as far as they've gone forward (both:
	// as far as both have), twisted as one's gone ahead of the other
	for ( h = 0; h < 2; h++ ) {
		if ( !vrk.held[h] ) {
			continue;
		}
		CG_VRK_GripAt( h, 0.0f, 0.0f, rest );
		ahead[h] = vrk.hand[h][0] - vrk.hold[h][0] - rest[0];
		push += ahead[h];
		held++;
	}
	if ( held ) {
		push = Com_Clamp( -1.0f, 1.0f, push / held / VRK_PUSH_RANGE );
		if ( held == 2 ) {
			twist = Com_Clamp( -1.0f, 1.0f, ( ahead[VR_HAND_RIGHT] - ahead[VR_HAND_LEFT] ) * 0.5f / VRK_TWIST_RANGE );
		}
		vrk.push = push;
		vrk.twist = twist;
		vrk.move = CG_VRK_Dead( push );
		vrk.turn = CG_VRK_Dead( twist );
	} else {
		// the sticks: the moving one walks it (up) and backs it up, the turning
		// one turns it (pushed right: right) - the yoke easing to where they have it
		const float ease = dt > 0.0f ? 1.0f - expf( -dt / VRK_SETTLE_SECONDS ) : 1.0f;
		vrk.move = vr ? Com_Clamp( -1.0f, 1.0f, vr->stick[moveHand][1] ) : 0.0f;
		vrk.turn = vr ? Com_Clamp( -1.0f, 1.0f, -vr->stick[moveHand ^ 1][0] ) : 0.0f;
		vrk.push += ( vrk.move - vrk.push ) * ease;
		vrk.twist += ( vrk.turn - vrk.twist ) * ease;
	}

	// where a squeeze takes hold of a grip now: a hand at its own (none at a
	// grip it holds already)
	vrk.zones = 0;
	for ( h = 0; h < 2; h++ ) {
		if ( vrk.held[h] || !vrk.handValid[h] ) {
			continue;
		}
		CG_VRK_GripAt( h, vrk.push, vrk.twist, grip );
		if ( Distance( vrk.hand[h], grip ) <= VRK_GRIP_REACH ) {
			vrk.zones |= 1 << h;
		}
	}
	vrk.zoneTime = cg.time;
}

// Hands whose squeeze takes hold of a yoke grip now (bit per hand): the
// engine's told (VR_SetGrabZones).
int CG_VR_WalkerZones( void ) {
	if ( !CG_VR_InCockpit() ) {
		return 0;
	}
	return ( cg.time - vrk.zoneTime >= 0 && cg.time - vrk.zoneTime <= VRK_WORD_MS ) ? vrk.zones : 0;
}

// The buttons your commands carry (VR_SetCmdButtons): in the cockpit, your
// yoke drives it (bg_vrwalker.h).
int CG_VR_WalkerButtons( void ) {
	return CG_VR_InCockpit() ? BUTTON_VR_YOKE : 0;
}

// The hand whose trigger fires the main guns in the cockpit (the other's the
// second weapon) - -2 out of it.
int CG_VR_WalkerWeaponHand( void ) {
	return CG_VR_InCockpit() ? CG_VR_GunHand() : -2;
}

/*
================================================================================

What's drawn

================================================================================
*/

static void CG_VRK_AddModel( qhandle_t model, const vec3_t origin, vec3_t axis[3], const vec3_t lightAt ) {
	refEntity_t ent;

	memset( &ent, 0, sizeof( ent ) );
	ent.reType = RT_MODEL;
	ent.hModel = model;
	VectorCopy( origin, ent.origin );
	VectorCopy( origin, ent.oldorigin );
	AxisCopy( axis, ent.axis );
	VectorCopy( lightAt, ent.lightingOrigin );
	ent.renderfx = RF_LIGHTING_ORIGIN | RF_NOSHADOW | RF_FIRST_PERSON;	// (lit as one, where your head is; only in your eyes)
	trap->R_AddRefEntityToScene( &ent );
}

// The cockpit, its yoke and your hands on it (each eye's scene).
static void CG_VRK_Draw( void ) {
	vec3_t lightAt, origin, yokeAxis[3], local[3], axis[3], at;
	int h, i, j;

	CG_VRK_ToWorld( vrk.eye, lightAt );
	CG_VRK_AddModel( vrk.cockpit, vrk.place, vrk.axis, lightAt );

	// the yoke, pushed and twisted
	CG_VRK_YokeAxes( vrk.push, vrk.twist, local );
	for ( i = 0; i < 3; i++ ) {
		for ( j = 0; j < 3; j++ ) {
			yokeAxis[i][j] = local[i][0] * vrk.axis[0][j] + local[i][1] * vrk.axis[1][j] + local[i][2] * vrk.axis[2][j];
		}
	}
	CG_VRK_ToWorld( vrk.pivot, origin );
	CG_VRK_AddModel( vrk.yoke, origin, yokeAxis, lightAt );

	// your hands: on the grips they hold (round them, along them), else where they are
	for ( h = 0; h < 2; h++ ) {
		if ( !vrk.handValid[h] || !vrk.handModel ) {
			continue;
		}
		if ( vrk.held[h] ) {
			CG_VRK_GripAt( h, vrk.push, vrk.twist, at );
			CG_VRK_ToWorld( at, origin );
			VectorCopy( yokeAxis[2], axis[0] );		// (a fist's along what it holds: the grip's up)
			VectorCopy( yokeAxis[1], axis[1] );
			VectorScale( yokeAxis[0], -1.0f, axis[2] );
		} else {
			CG_VRK_ToWorld( vrk.hand[h], origin );
			AxisCopy( vrk.handAxis[h], axis );
		}
		CG_VRK_AddModel( vrk.handModel, origin, axis, lightAt );
	}
}

/*
================================================================================

Its steps

The AT-ST's steps as you hear them in its cockpit: the game's own (two a
stride: its animevents.cfg's), played here as its legs pass the frames its
feet come down on (bg_vrwalker.h) - walking, running, backing up, turning on
the spot - not as the game plays them, which in the cockpit didn't always
come (CG_VR_WalkerStepSound: those not played, so none twice). Its animation's
frame not to be told, or held a moment, as it walks: a step every half of its
stride's time all the same.

================================================================================
*/

#define VRK_STEP_STUCK_MS	300		// its walk's frame held this long, walking: its animation's not going

// The AT-ST's walk - the cycle its legs play walking, running, backing up and
// turning on the spot: its first frame, how many, how long each is (ms; less
// than 0: played back) - as its animation.cfg has it, for this animation of
// its (else the game's: 114, 38, 50).
static void CG_VRK_Walk( const centity_t *veh, int anim, int *first, int *count, int *lerp ) {
	*first = 114;
	*count = 38;
	*lerp = 50;
	if ( veh->localAnimIndex >= 0 && veh->localAnimIndex < MAX_ANIM_FILES && bgAllAnims[veh->localAnimIndex].anims &&
		anim >= 0 && anim < MAX_ANIMATIONS ) {
		const animation_t *a = &bgAllAnims[veh->localAnimIndex].anims[anim];
		if ( a->numFrames > 1 && a->frameLerp ) {
			*first = a->firstFrame;
			*count = a->numFrames;
			*lerp = a->frameLerp;
		}
	}
}

static qboolean CG_VRK_Walking( int anim ) {
	return (qboolean)( anim == BOTH_WALK1 || anim == BOTH_RUN1 || anim == BOTH_WALKBACK1 || anim == BOTH_RUNBACK1 );
}

// Whether a sound of an animation's (CG_PlayerAnimEventDo) is the cockpit's
// to play instead: a step of the AT-ST you drive from its cockpit - one on a
// frame of its walk (CG_VRK_Steps plays them). Counted for the log.
qboolean CG_VR_WalkerStepSound( const centity_t *cent, int keyFrame ) {
	int first, count, lerp;

	if ( !cent || !CG_VR_InCockpit() || cent->currentState.number != vrk.vehNum ) {
		return qfalse;
	}
	CG_VRK_Walk( cent, BOTH_RUN1, &first, &count, &lerp );
	if ( keyFrame < first || keyFrame >= first + count ) {
		return qfalse;	// (not a step: its fall's, ...)
	}
	vrk.stepsGame++;
	return qtrue;
}

static void CG_VRK_Step( const centity_t *veh ) {
	const sfxHandle_t s = vrk.stepSound[Q_irand( 0, VR_WALKER_STEP_SOUNDS - 1 )];

	if ( s ) {
		trap->S_StartSound( NULL, veh->currentState.number, CHAN_AUTO, s );	// (where the game plays them: from it)
	}
	vrk.steps++;
	vrk.stepTime = cg.time;
}

// Its steps, once a frame in the cockpit.
static void CG_VRK_Steps( centity_t *veh ) {
	const int legs = veh->currentState.legsAnim;
	int first, count, lerp, frame = -1;
	qboolean back;
	float f;

	vrk.legFrame = -1.0f;
	if ( !CG_VRK_Walking( legs ) || !veh->ghoul2 ) {
		vrk.stepFrame = -1;
		vrk.stepFrameTime = 0;
		return;
	}
	CG_VRK_Walk( veh, legs, &first, &count, &lerp );
	back = (qboolean)( lerp < 0 );
	if ( trap->G2API_GetBoneFrame( veh->ghoul2, "model_root", cg.time, &f, cgs.gameModels, 0 ) ) {
		frame = (int)floorf( f );
		if ( frame < first || frame >= first + count ) {
			frame = -1;	// (blending in from another animation)
		} else {
			vrk.legFrame = f;	// (its feet on the GROUND screen: CG_VRK_Screens)
		}
	}
	if ( !vrk.stepFrameTime || cg.time - vrk.stepFrameTime < 0 ) {
		vrk.stepFrame = frame;	// (walking afresh: from here)
		vrk.stepFrameTime = cg.time ? cg.time : 1;
		return;
	}
	if ( frame >= 0 && frame != vrk.stepFrame ) {
		if ( vrk.stepFrame >= 0 && ( BG_VRWalkerStepPassed( vrk.stepFrame, frame, first + VR_WALKER_STEP_1, back, first, count ) ||
			BG_VRWalkerStepPassed( vrk.stepFrame, frame, first + VR_WALKER_STEP_2, back, first, count ) ) ) {
			CG_VRK_Step( veh );
		}
		vrk.stepFrame = frame;
		vrk.stepFrameTime = cg.time ? cg.time : 1;
		return;
	}
	// its frame not to be told, or held: by time
	if ( cg.time - vrk.stepFrameTime > VRK_STEP_STUCK_MS && ( cg.time - vrk.stepTime < 0 || cg.time - vrk.stepTime >= count * abs( lerp ) / 2 ) ) {
		CG_VRK_Step( veh );
	}
}

// The AT-ST's walk in the log (once a frame, in the cockpit): what its legs
// do, how fast it goes and how many of its steps were heard - every 2 seconds
// it moves or steps, and as its legs change (a quarter of a second apart at
// most).
static void CG_VRK_LogWalk( void ) {
	const centity_t *veh = &cg_entities[vrk.vehNum];
	const float top = ( veh->m_pVehicle && veh->m_pVehicle->m_pVehicleInfo ) ? veh->m_pVehicle->m_pVehicleInfo->speedMax : 0.0f;
	const int legs = veh->currentState.legsAnim, since = cg.time - vrk.walkLogTime;
	const char *name = GetStringForID( animTable, legs );

	if ( vrk.walkLogTime && since >= 0 ) {
		if ( legs != vrk.walkLogLegs ? ( since < 250 ) : ( since < 2000 || ( !cg.predictedVehicleState.speed && !vrk.steps && !vrk.stepsGame ) ) ) {
			return;
		}
	}
	CG_VR_Log( "walker: its legs %s (frame %i), %.0f%% of its top speed; %i steps played (the game's own for them: %i)\n",
		name ? name : "?", vrk.stepFrame, top > 0.0f ? cg.predictedVehicleState.speed * 100.0f / top : 0.0f, vrk.steps, vrk.stepsGame );
	vrk.steps = vrk.stepsGame = 0;
	vrk.walkLogTime = cg.time ? cg.time : 1;
	vrk.walkLogLegs = legs;
}

/*
=================
CG_VR_WalkerFrame

Each frame as it ends (CG_VR_BodyEndFrame: the scene built, before the
engine's told where a squeeze takes hold of something): in the cockpit, the
yoke (once a frame) and what's drawn of it (each eye's); and the engine
told whether you sit in it, which way it faces and how it's driven.
=================
*/
void CG_VR_WalkerFrame( const vrClientState_t *vr ) {
	vrCockpit_t cp;

	if ( !CG_VR_InCockpit() ) {
		vrk.held[0] = vrk.held[1] = qfalse;
		vrk.zones = 0;
		if ( vrk.told && CG_VR_Engine() && trap->ext.VR_SetCockpit ) {
			trap->ext.VR_SetCockpit( NULL );
			if ( trap->ext.VR_SetCockpitScreens ) {
				trap->ext.VR_SetCockpitScreens( NULL );
			}
			CG_VR_Log( "walker: out of the cockpit\n" );
		}
		vrk.told = qfalse;
		return;
	}
	if ( vrk.yokeTime != cg.time ) {
		CG_VRK_Yoke( vr );
		CG_VRK_Steps( &cg_entities[vrk.vehNum] );
		CG_VRK_LogWalk();
		CG_VRK_Screens( &cg_entities[vrk.vehNum], vr );
	}
	CG_VRK_Draw();

	cp.yaw = vrk.yaw;
	cp.move = vrk.move;
	cp.turn = vrk.turn;
	cp.lookPitch = cg_entities[vrk.vehNum].m_pVehicle ? cg_entities[vrk.vehNum].m_pVehicle->m_pVehicleInfo->lookPitch : 0.0f;
	trap->ext.VR_SetCockpit( &cp );
	if ( trap->ext.VR_SetCockpitScreens ) {
		trap->ext.VR_SetCockpitScreens( &vrk.screens );
	}
	vrk.told = qtrue;
}

/*
================================================================================

The guns

================================================================================
*/

// Where the AT-ST you drive from its cockpit aims its guns from, and which
// way (bg_vrwalker.h: as the server has them) - qfalse if you don't.
qboolean CG_VR_WalkerAim( int vehNum, vec3_t start, vec3_t dir ) {
	const centity_t *veh;

	if ( !CG_VR_InCockpit() || vehNum != vrk.vehNum ) {
		return qfalse;
	}
	veh = &cg_entities[vehNum];
	BG_VRWalkerEye( cg.predictedVehicleState.origin, vrk.yaw, start );
	BG_VRWalkerAim( cg.predictedPlayerState.viewangles, vrk.yaw, veh->m_pVehicle->m_pVehicleInfo->lookYaw,
		veh->m_pVehicle->m_pVehicleInfo->lookPitch, dir );
	return qtrue;
}

/*
=================
CG_VR_WalkerAimDot

In the cockpit, a small crosshair floating where the guns' shots will land
(as CG_VR_AddAimDot's for a gun in your hand): traced from its eyes the way
they aim, as the server does - added once the scene's built (cg_view.c).
=================
*/
void CG_VR_WalkerAimDot( void ) {
	refEntity_t ent;
	trace_t tr;
	vec3_t start, dir, end;
	float dist;

	if ( !cg_drawCrosshair.integer || !cg_vrAimDot.integer || !CG_VR_WalkerAim( vrk.vehNum, start, dir ) ) {
		return;
	}
	VectorMA( start, 8192.0f, dir, end );
	CG_Trace( &tr, start, NULL, NULL, end, vrk.vehNum, CONTENTS_SOLID | CONTENTS_BODY );
	dist = Distance( cg.refdef.vieworg, tr.endpos );
	if ( dist < 8.0f ) {
		return;
	}
	memset( &ent, 0, sizeof( ent ) );
	ent.reType = RT_SPRITE;
	VectorMA( tr.endpos, -1.0f, dir, ent.origin );
	ent.radius = Q_max( 0.25f, dist * 0.008f * cg_vrAimDotSize.value );	// (as big wherever it is)
	ent.customShader = cgs.media.crosshairShader[Com_Clampi( 1, NUM_CROSSHAIRS, cg_drawCrosshair.integer ) - 1];
	ent.shaderRGBA[0] = ent.shaderRGBA[1] = ent.shaderRGBA[2] = ent.shaderRGBA[3] = 255;
	ent.renderfx = RF_NOSHADOW;
	trap->R_AddRefEntityToScene( &ent );
}

/*
================================================================================

Its screens

What the cockpit's six screens show (the renderer draws them: tr_vratst.cpp,
told it through the engine - ext.VR_SetCockpitScreens), worked out once a
frame in it:
- HULL: its armour left, and whether its legs walk;
- WEAPONS: whether the main guns can fire, how hot they run (as much of
  their charge as they've spent: it comes back as they cool), and the second
  weapon's shots left;
- GROUND: the ground going by under it, a metre to a square, its feet going
  as its legs do;
- TARGET: who's before its guns - the one nearest where they aim, in sight,
  not on your side - how far off, and where;
- COMMS: the last of what's been said (the chat's), and how loud the voices
  are that you hear;
- HEADING: which way it faces, and how fast it goes.

================================================================================
*/

#define VRK_TARGET_CONE		40.0f	// degrees: who's this near where the guns aim (and the TARGET screen's sides)...
#define VRK_TARGET_RANGE	6000.0f	// ...and this near them (units) is before them
#define VRK_TARGET_KEEP		0.8f	// the one before them already, as if this much nearer where they aim (no flicking
									// between two)
#define VRK_WALK_SECONDS	0.15f	// its feet's swing, eased this much as it starts and stops walking (a time constant)
#define VRK_VOICE_SECONDS	0.25f	// the voices' level, falling (rising: at once)

/*
=================
CG_VR_WalkerChat

What's said (everything the chat box is told, shown or not:
CG_ChatBox_AddString): the last two things, for the COMMS screen.
=================
*/
void CG_VR_WalkerChat( const char *text ) {
	char line[MAX_SAY_TEXT * 2 + 64];
	int i;

	if ( !text ) {
		return;
	}
	Q_strncpyz( line, text, sizeof( line ) );
	Q_StripColor( line );
	for ( i = 0; line[i]; i++ ) {
		if ( (unsigned char)line[i] < ' ' ) {
			line[i] = ' ';	// (its lines broken: one)
		}
	}
	for ( i = 0; line[i] == ' '; i++ ) {
	}
	if ( !line[i] ) {
		return;
	}
	memcpy( vrk.chat[0], vrk.chat[1], sizeof( vrk.chat[0] ) );
	Q_strncpyz( vrk.chat[1], line + i, sizeof( vrk.chat[1] ) );
}

// Whether another player's on your side, as far as this can tell: a team
// game's team, a Power Duel's pair.
static qboolean CG_VRK_Friend( int n ) {
	const int me = cg.clientNum;

	if ( n < 0 || n >= MAX_CLIENTS ) {
		return qfalse;
	}
	if ( n == me ) {
		return qtrue;
	}
	if ( cgs.gametype == GT_POWERDUEL ) {
		return (qboolean)( cgs.clientinfo[n].duelTeam == cgs.clientinfo[me].duelTeam );
	}
	return (qboolean)( cgs.gametype >= GT_TEAM && cgs.clientinfo[n].team == cgs.clientinfo[me].team );
}

// The middle of someone (their box's), as they're drawn this frame.
static void CG_VRK_Middle( const centity_t *cent, vec3_t out ) {
	const int solid = cent->currentState.solid;

	VectorCopy( cent->lerpOrigin, out );
	if ( solid && solid != SOLID_BMODEL ) {
		out[2] += ( ( ( ( solid >> 16 ) & 255 ) - 32 ) - ( ( solid >> 8 ) & 255 ) ) * 0.5f;
	}
}

// Who they are, as the TARGET screen has it: a player's name; a vehicle's
// driver's, or what it is; a companion's name; an NPC's kind.
static void CG_VRK_TargetName( const centity_t *cent, char *out, int size ) {
	const entityState_t *es = &cent->currentState;
	const char *name = NULL;
	int i;

	if ( es->eType == ET_PLAYER && es->number < MAX_CLIENTS ) {
		name = cgs.clientinfo[es->number].cleanname;
	} else if ( es->NPC_class == CLASS_VEHICLE && cent->m_pVehicle && cent->m_pVehicle->m_pVehicleInfo ) {
		if ( es->owner >= 0 && es->owner < MAX_CLIENTS && cgs.clientinfo[es->owner].infoValid ) {
			name = cgs.clientinfo[es->owner].cleanname;
		} else {
			switch ( cent->m_pVehicle->m_pVehicleInfo->type ) {
			case VH_WALKER:		name = "WALKER";	break;
			case VH_FIGHTER:	name = "FIGHTER";	break;
			case VH_SPEEDER:	name = "SPEEDER";	break;
			case VH_ANIMAL:		name = "ANIMAL";	break;
			default:			name = "VEHICLE";	break;
			}
		}
	} else if ( ( name = CG_RP_CompName( es->number ) ) == NULL && cent->npcClient ) {
		name = cent->npcClient->modelName;
	}
	Q_strncpyz( out, ( name && name[0] ) ? name : "CONTACT", size );
	for ( i = 0; out[i]; i++ ) {
		if ( out[i] == '_' ) {
			out[i] = ' ';
		}
	}
}

// Who's before the guns: alive, in sight, not on your side (nor yours, nor
// your side's companion or vehicle) - the one nearest where they aim.
static void CG_VRK_Target( vrCockpitScreens_t *s, float scale ) {
	vec3_t start, dir, aim, to, middle, angles;
	float bestScore = 0.0f, bestYaw = 0.0f, bestDist = 0.0f;
	int i, best = ENTITYNUM_NONE;
	trace_t tr;

	s->target[0] = '\0';
	s->range = 0;
	s->bearing = 0.0f;
	if ( !cg.snap || !CG_VR_WalkerAim( vrk.vehNum, start, dir ) ) {
		vrk.target = ENTITYNUM_NONE;
		return;
	}
	vectoangles( dir, aim );
	for ( i = 0; i < cg.snap->numEntities; i++ ) {
		const int n = cg.snap->entities[i].number;
		const centity_t *cent = &cg_entities[n];
		const entityState_t *es = &cent->currentState;
		float dist, off, score;
		if ( n == vrk.vehNum || !cent->currentValid || ( es->eFlags & EF_DEAD ) ) {
			continue;
		}
		if ( es->eType == ET_PLAYER ) {
			if ( n >= MAX_CLIENTS || es->m_iVehicleNum || ( es->powerups & ( 1 << PW_CLOAKED ) ) || CG_VRK_Friend( n ) ) {
				continue;	// (riding something: that's who's before them; you: on your side)
			}
		} else if ( es->eType == ET_NPC ) {
			const int owner = CG_RP_CompOwner( n );
			if ( owner >= 0 && CG_VRK_Friend( owner ) ) {
				continue;
			}
			if ( es->NPC_class == CLASS_VEHICLE && es->owner >= 0 && es->owner < MAX_CLIENTS && CG_VRK_Friend( es->owner ) ) {
				continue;
			}
		} else {
			continue;
		}
		CG_VRK_Middle( cent, middle );
		VectorSubtract( middle, start, to );
		dist = VectorNormalize( to );
		if ( dist < 1.0f || dist > VRK_TARGET_RANGE ) {
			continue;
		}
		off = RAD2DEG( acosf( Com_Clamp( -1.0f, 1.0f, DotProduct( to, dir ) ) ) );
		if ( off > VRK_TARGET_CONE ) {
			continue;
		}
		score = ( n == vrk.target ) ? off * VRK_TARGET_KEEP : off;
		if ( best != ENTITYNUM_NONE && score >= bestScore ) {
			continue;
		}
		CG_Trace( &tr, start, NULL, NULL, middle, vrk.vehNum, CONTENTS_SOLID );
		if ( tr.fraction < 1.0f && tr.entityNum != n ) {
			continue;	// (out of sight)
		}
		vectoangles( to, angles );
		best = n;
		bestScore = score;
		bestDist = dist;
		bestYaw = AngleSubtract( angles[YAW], aim[YAW] );
	}
	vrk.target = best;
	if ( best == ENTITYNUM_NONE ) {
		return;
	}
	CG_VRK_TargetName( &cg_entities[best], s->target, sizeof( s->target ) );
	s->range = (int)( bestDist / scale + 0.5f );
	s->bearing = Com_Clamp( -1.0f, 1.0f, -bestYaw / VRK_TARGET_CONE );	// (to its left: the screen's)
}

// How loud the voices are that you hear (the voice chat's, the game's own):
// whoever's loudest (0..1).
static float CG_VRK_Voices( void ) {
	static const float level[5] = { 0.0f, 0.3f, 0.55f, 0.8f, 1.0f };	// (the lip sync's levels: 1..4; -1 talking but quiet)
	float loud = 0.0f;
	int i;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		int v;
		float l;
		if ( i == cg.clientNum || !cgs.clientinfo[i].infoValid ) {
			continue;
		}
		v = trap->S_GetVoiceVolume( i );
		l = ( v < 0 ) ? 0.15f : level[Com_Clampi( 0, 4, v )];
		if ( l > loud ) {
			loud = l;
		}
	}
	return loud;
}

// What its screens show, worked out (once a frame in it).
static void CG_VRK_Screens( centity_t *veh, const vrClientState_t *vr ) {
	const vehicleInfo_t *info = ( veh->m_pVehicle ) ? veh->m_pVehicle->m_pVehicleInfo : NULL;
	const playerState_t *vps = &cg.predictedVehicleState;
	const float scale = ( vr && vr->worldScale > 1.0f ) ? vr->worldScale : 34.0f;	// (units a metre)
	vrCockpitScreens_t *s = &vrk.screens;
	const int legs = veh->currentState.legsAnim;
	float dt = 0.0f, along, speed;
	int first, count, lerp;
	vec3_t angles, fwd;

	if ( vrk.screensTime && cg.time - vrk.screensTime > 0 ) {
		dt = Q_min( cg.time - vrk.screensTime, 250 ) * 0.001f;
	}
	vrk.screensTime = cg.time ? cg.time : 1;
	s->time = cg.time;

	// HULL
	s->armour = 100;
	if ( info && info->armor > 0 ) {
		s->armour = Com_Clampi( 0, 100, vps->stats[STAT_HEALTH] * 100 / info->armor );
		if ( !s->armour && vps->stats[STAT_HEALTH] > 0 ) {
			s->armour = 1;	// (not gone yet)
		}
	}
	s->walking = CG_VRK_Walking( legs );

	// WEAPONS
	s->ready = qtrue;
	s->heat = 0;
	s->second = s->secondMax = 0;
	if ( info ) {
		const int id = info->weapon[0].ID, perShot = ( id >= 0 && id < MAX_VEH_WEAPONS ) ? g_vehWeaponInfo[id].iAmmoPerShot : 1;
		if ( info->weapon[0].ammoMax > 0 ) {
			s->ready = (qboolean)( vps->ammo[0] >= perShot );
			s->heat = Com_Clampi( 0, VR_SCREEN_HEAT, (int)( ( 1.0f - (float)vps->ammo[0] / info->weapon[0].ammoMax ) * VR_SCREEN_HEAT + 0.5f ) );
		}
		s->second = Q_max( 0, vps->ammo[1] );
		s->secondMax = Q_max( 0, info->weapon[1].ammoMax );
	}

	// GROUND: the ground gone by (along the way it faces: back, backing up),
	// its feet as its legs are in their stride, the right foot down as their
	// first step comes (bg_vrwalker.h)
	VectorSet( angles, 0.0f, vrk.yaw, 0.0f );
	AngleVectors( angles, fwd, NULL, NULL );
	along = vps->velocity[0] * fwd[0] + vps->velocity[1] * fwd[1];
	speed = sqrtf( vps->velocity[0] * vps->velocity[0] + vps->velocity[1] * vps->velocity[1] );
	vrk.ground = fmodf( vrk.ground + along * dt / scale, 1000.0f );
	s->ground = vrk.ground;
	CG_VRK_Walk( veh, legs, &first, &count, &lerp );
	if ( s->walking && vrk.legFrame >= 0.0f ) {
		vrk.stride = ( vrk.legFrame - first - VR_WALKER_STEP_1 ) / count;
	} else if ( s->walking ) {
		vrk.stride += dt * 1000.0f / ( count * lerp );	// (its frame not to be told: by time - back, backing up)
	}
	vrk.stride -= floorf( vrk.stride );
	s->stride = vrk.stride;
	vrk.walk += ( ( s->walking ? 1.0f : 0.0f ) - vrk.walk ) * ( 1.0f - expf( -dt / VRK_WALK_SECONDS ) );
	s->walk = vrk.walk;

	// TARGET
	CG_VRK_Target( s, scale );

	// COMMS
	memcpy( s->comms, vrk.chat, sizeof( s->comms ) );
	{
		const float loud = CG_VRK_Voices();
		vrk.voice = ( loud >= vrk.voice ) ? loud : vrk.voice + ( loud - vrk.voice ) * ( 1.0f - expf( -dt / VRK_VOICE_SECONDS ) );
		s->voice = vrk.voice;
	}

	// HEADING (a compass's: the map's north, +Y, 0; east, +X, 90)
	s->heading = ( ( (int)floorf( 90.0f - vrk.yaw + 0.5f ) % 360 ) + 360 ) % 360;
	s->speed = (int)( speed / scale * 3.6f + 0.5f );
}

// A map begun: in no cockpit.
void CG_VR_WalkerInit( void ) {
	trap->Cvar_Register( NULL, "vrw", XSTRING( VR_WALKER_RULE ), CVAR_USERINFO | CVAR_ROM | CVAR_TEMP );	// (these rules: bg_vrwalker.h)
	vrk.on = qfalse;
	vrk.time = 0;
	vrk.ghoul2 = NULL;
	vrk.neckBolt = -1;
	vrk.held[0] = vrk.held[1] = qfalse;
	vrk.told = qfalse;
	vrk.target = ENTITYNUM_NONE;
	memset( vrk.chat, 0, sizeof( vrk.chat ) );
}
