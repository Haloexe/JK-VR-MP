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
- Use - its grip, away from the yoke - held a moment gets you out (the
  engine's).
On a server without it (or Cockpit off) you drive it as the game has it,
watching it from behind: it turns toward where you look.

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
} vrk;

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
			CG_VR_Log( "walker: out of the cockpit\n" );
		}
		vrk.told = qfalse;
		return;
	}
	if ( vrk.yokeTime != cg.time ) {
		CG_VRK_Yoke( vr );
	}
	CG_VRK_Draw();

	cp.yaw = vrk.yaw;
	cp.move = vrk.move;
	cp.turn = vrk.turn;
	cp.lookPitch = cg_entities[vrk.vehNum].m_pVehicle ? cg_entities[vrk.vehNum].m_pVehicle->m_pVehicleInfo->lookPitch : 0.0f;
	trap->ext.VR_SetCockpit( &cp );
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

// A map begun: in no cockpit.
void CG_VR_WalkerInit( void ) {
	vrk.on = qfalse;
	vrk.time = 0;
	vrk.ghoul2 = NULL;
	vrk.neckBolt = -1;
	vrk.held[0] = vrk.held[1] = qfalse;
	vrk.told = qfalse;
}
