/*
===========================================================================
OpenJK VR (multiplayer) - the Force hand, in the client game.

Your Force hand's gestures are the engine's (qcommon/vr_force.h: Force
gestures, vr_forceGestures); here they're felt, in that hand (gestures
off, the Force grip's):

  reaching out      a light buzz as the Force grip goes in, then a faint
                    hum while it's held
  aimed at someone  a light tick as Grip's aim lands on someone new
  Push              a thump
  Pull              a tug
  a quick squeeze   a tick (Use Force: the power picked)
  Grip              a rough strain while you hold someone, heavier the
                    faster they're moved (with your hand: g_vrforce.c) -
                    carried along as you go, or you going off from them,
                    it's as still
  a throw           a swoosh as whom you held is let go of flying (as fast
                    as a throw, and as fast from you: running with them,
                    no - nor you flung off from them)
  Lightning         a crackle while it's going
  Drain             a steady pull while it's going

and the places (Body Gestures, vr_forceBody: the engine's too):

  held still in one a soft tick (one with a power of yours there)
  Heal              two slow, soft pulses, like a heartbeat
  Rage              a deep rumble fading over half a second
  Protect           a firm press - and a faint hum while it lasts
  Absorb            a rising buzz
  Seeing            one soft ping
  Team Heal, Team   two quick pulses
  Energize
  one refused       one short, dull buzz (no Force left, still waiting,
                    nothing to heal...: CG_VRF_CastWorks)
  one ended         a soft release (Protect, Absorb, Rage or Seeing going:
                    cast again, it's turned off - but in the moment after
                    one starts, the game won't: refused)

Push and Pull are felt if the game would do them now (you know them; not
with someone gripped, Lightning or Drain going, the last Push's wait not
over, a swing going...: CG_VRF_ThrowWorks); Grip, Lightning and Drain while
the game has them going (your player state), however you started them, and
Protect's hum too - and none of it while you watch (following someone,
spectating, a demo: their Force isn't yours). The log (cg_vrLog) says what
the hand did - and where it was in a place, or held still close in by you
but in none of them (for fitting the places to you).

On a server that aims the Force from the hand ("vrForce" in its
serverinfo: game/g_vrforce.c) it comes out of it, too: Lightning and
Drain from that hand, the way it points - onto whom the server snaps it,
as near as this can tell (CG_VR_ForceSnap) - Push's ripple and Grip's
glow round it (yours, in the headset, too); yours, drawn as your VR body,
and another VR player's, from their avatar (cg_players.c). On one aiming it
as this build does (VR_FORCE_SERVED), your hand aims it (CG_VR_ForceAim):
along where the controller points - and, held up in view, where your hand is
as you see it: someone on either line, anywhere on them, is whom it goes
straight at (your avatar's forceDir, which the server snaps the same way).
Whom your Grip would catch glows while you hold the Force grip out
(cg_vrForceGlow).

And the wheel's Force ring (cg_vrpose.c) picks the Force powers that
aren't Push, Pull, Grip, Lightning or Drain - the one the Force grip's
quick squeeze uses, as X and Y step through them all (yours: not while
you watch).

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "cg_local.h"
#include "qcommon/vr_force.h"	// (the gestures' own: how Grip wants the hand held - CG_VRF_GripPose)

/*
--------------------------------------------------------------------------------
What the Force hand feels - and what's become of your squeeze, and your Grip
--------------------------------------------------------------------------------
*/

static struct {
	qboolean	init;
	int			pushes, pulls, taps;	// the engine's counts, as last seen
	qboolean	reaching;
	int			held;
	int			active;					// your Force powers going, as last seen (the log)
	int			ticked, tickedTime;		// whom Grip's aim was last on (-1: nobody yet), and when (a tick: someone new)
	int			casts, place;			// the places' powers cast (the engine's count), and the place held still in, as
										// last seen
	int			castPower;				// a place's power cast last: which (-1: none yet)...
	int			castStart;				// ...when (cg.time)...
	int			castHow;				// ...what it does (CGF_REFUSED...)...
	int			castStep;				// ...its pulses felt so far
	vec3_t		stillAt;				// the Force grip held, the hand held still close in, in no place: where it settled...
	int			stillTime;				// ...when (0: it isn't)...
	qboolean	stillSaid;				// ...said in the log
	int			lockActive;				// CGF_LOCKERS going, as last seen...
	int			lockStart;				// ...and when one last started (cg.time; 0: none seen)
} cgf = { qfalse, 0, 0, 0, qfalse, 0, 0, -1, 0, 0, 0, -1, 0, 0, 0, { 0.0f, 0.0f, 0.0f }, 0, qfalse, 0, 0 };

#define CGF_TICK_AGAIN	600		// ms: Grip's aim back on whom it was on within this long - no one new (no tick)
#define CGF_REFUSED		0		// what a place's power cast does now (CG_VRF_CastWorks): nothing...
#define CGF_WORKS		1		// ...it works...
#define CGF_ENDS		2		// ...it was going: it's turned off
#define CGF_PROTECT_HUM	0.05f	// Protect's hum while it lasts
#define CGF_LOCKERS		( ( 1 << FP_SPEED ) | ( 1 << FP_TELEPATHY ) | ( 1 << FP_RAGE ) | ( 1 << FP_PROTECT ) | ( 1 << FP_ABSORB ) | \
							( 1 << FP_SEE ) )	// one of these starting...
#define CGF_LOCK_MS		1500	// ...the game won't turn Protect, Absorb, Rage or Seeing off for this long (ms)

// Where your Force hand aimed (CG_VR_ForceAim), a frame of it.
typedef struct {
	int			time;		// cg.time it aimed (0: never)
	vec3_t		dir;		// where your Force went (world, unit)
	int			power;		// whose rules it was aimed by (-1: none)...
	int			pick;		// ...at whom (-1: nobody)
} cgfAim_t;

#define CGF_AIM_FRESH	100		// ms: where your Force hand aimed this long ago is where it aims (its effects, the glow)
#define CGF_CAUGHT_MS	1000	// ms: your Grip going, whom its aim went at this lately is whom it caught
#define CGF_GRIP_WAIT	3000	// ms: a Grip over, no other for this long (WP_ForcePowerStop: forceGripUseTime)
#define CGF_STRAIN		0.35f	// Grip's strain, whom it holds still...
#define CGF_STRAIN_MOST	0.8f	// ...going this fast or faster...
#define CGF_STRAIN_FULL	400.0f	// ...(units a second)

// Where your Force hand aims, and what's become of your squeeze and your Grip
// (CG_VRF_Track).
static struct {
	cgfAim_t	now, was;			// this frame's aim (or the last one's), and the one before it (CG_VRF_Drawn)
	int			gripWho, gripTime;	// whom Grip's aim last went at (-1: nobody yet), and when
	qboolean	spent;				// the Force grip held - a Push, a Pull, Lightning, Drain or a place's power since it was:
								// no Grip of it...
	int			sqPushes, sqPulls, sqCasts;	// ...(the engine's counts as it was)
	qboolean	gripping;			// your Grip going (the game's word), as last seen...
	int			caught;				// ...whom it caught, as near as this can tell (-1: not known)...
	int			gripOver;			// ...when one last ended (as the game begins: long enough ago)...
	int			letGoTime;			// ...when that was seen (CG_VR_ForceFrame feels it: 0, felt)...
	float		letGoSpeed;			// ...how fast whom it caught was moved then (CG_VRF_Speed: units a second)
} cgfa = { { 0, { 0.0f, 0.0f, 0.0f }, -1, -1 }, { 0, { 0.0f, 0.0f, 0.0f }, -1, -1 }, -1, 0, qfalse, 0, 0, 0, qfalse, -1, -CGF_GRIP_WAIT, 0, 0.0f };

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
// WP_ForcePowerUsable's): it can't be used now (not known; BG_CanUseFPNow: a
// ysalamiri, a duel, a vehicle, a broken arm...; down; in a pose; not enough
// Force - your levels aren't sent you, but a Push and a Pull cost the same at
// every level), your hand busy (someone gripped, Lightning or Drain going, the
// last Push or Pull still out, a choke, a taunt...; knocked down, unless
// getting up), a swing going, the last Push's or Pull's wait not over.
static qboolean CG_VRF_ThrowWorks( const playerState_t *ps, int power ) {
	if ( !( ps->fd.forcePowersKnown & ( 1 << power ) ) || ps->fd.forcePower < forcePowerNeeded[FORCE_LEVEL_1][power] ||
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

// How fast someone else is moved, as the snapshot your own state comes from has
// them (CG_PredictPlayerState: the next, if it predicted from that one - its
// time cg.physicsTime - else the one it's at), so as your Grip's seen over
// they're seen as it let them go: as fast as they go, or as fast from you,
// whichever's less - carried along as you go, as still; you flung off from
// them, as still too (units a second). 0: not known (nobody, not in it).
static float CG_VRF_Speed( int n ) {
	const snapshot_t *snap = ( cg.nextSnap && cg.physicsTime == cg.nextSnap->serverTime ) ? cg.nextSnap : cg.snap;
	int i;

	for ( i = 0; snap && i < snap->numEntities; i++ ) {
		if ( snap->entities[i].number == n ) {
			vec3_t v;
			VectorSubtract( snap->entities[i].pos.trDelta, snap->ps.velocity, v );
			return Q_min( VectorLength( snap->entities[i].pos.trDelta ), VectorLength( v ) );
		}
	}
	return 0.0f;
}

// Your squeeze and your Grip as this frame has them (CG_VR_ForceAim's and
// CG_VR_ForceFrame's, whichever comes first - twice is once): the Force grip
// held, a Push, a Pull, Lightning, Drain or a place's power since it was
// spends it - no Grip of it (the gestures'; a Push's or a Pull's, the server's
// too: g_vrforce.c's vrGripSpent) till it's squeezed afresh; your Grip going
// (the game's word), whom it caught - whom Grip's aim went at lately - and
// when it's over (its wait), whom it caught going how fast then.
static void CG_VRF_Track( const vrClientState_t *vr, const playerState_t *ps ) {
	const qboolean gripping = (qboolean)( ( ps->fd.forcePowersActive & ( 1 << FP_GRIP ) ) != 0 );

	if ( !vr->force.reaching ) {
		cgfa.spent = qfalse;
		cgfa.sqPushes = vr->force.pushes;
		cgfa.sqPulls = vr->force.pulls;
		cgfa.sqCasts = vr->force.casts;
	} else if ( vr->force.pushes != cgfa.sqPushes || vr->force.pulls != cgfa.sqPulls || vr->force.casts != cgfa.sqCasts ||
		( vr->force.held & ( VR_FORCE_LIGHTNING | VR_FORCE_DRAIN ) ) ) {
		cgfa.spent = qtrue;
	}
	if ( gripping && !cgfa.gripping ) {
		cgfa.caught = ( cg.time - cgfa.gripTime >= 0 && cg.time - cgfa.gripTime <= CGF_CAUGHT_MS ) ? cgfa.gripWho : -1;
	} else if ( !gripping && cgfa.gripping ) {
		cgfa.gripOver = cgfa.letGoTime = cg.time;
		cgfa.letGoSpeed = CG_VRF_Speed( cgfa.caught );
	}
	cgfa.gripping = gripping;
}

// Whether your Grip would catch anyone now, as far as your player state says -
// the game's own reasons it wouldn't (ForceGrip's, WP_ForcePowerUsable's): it
// can't be used now (not known; BG_CanUseFPNow; down; in a pose; not enough
// Force - your level isn't sent you: level 1's cost, the least), your hand busy
// (a Push or a Pull still out, a choke, a taunt, knocked down...), a swing
// going, the last Grip's wait not over; or this squeeze spent (CG_VRF_Track).
static qboolean CG_VRF_GripWorks( const playerState_t *ps ) {
	if ( !( ps->fd.forcePowersKnown & ( 1 << FP_GRIP ) ) || ps->fd.forcePower < forcePowerNeeded[FORCE_LEVEL_1][FP_GRIP] ||
		!BG_CanUseFPNow( cgs.gametype, (playerState_t *)ps, cg.time, FP_GRIP ) || CG_RP_IsDown() || CG_VR_Posed() ) {
		return qfalse;
	}
	if ( ps->weaponTime > 0 || ps->forceHandExtend != HANDEXTEND_NONE || cgfa.spent ) {
		return qfalse;
	}
	return (qboolean)( cg.time - cgfa.gripOver >= CGF_GRIP_WAIT || cg.time - cgfa.gripOver < 0 );
}

// A setting of the engine's, as a number (none: not there) - or a whole
// number, as the engine reads one (cvar_t's integer: atoi).
static float CG_VRF_Setting( const char *name, float none ) {
	char buf[32];

	trap->Cvar_VariableStringBuffer( name, buf, sizeof( buf ) );
	return buf[0] ? (float)atof( buf ) : none;
}

static int CG_VRF_SettingInt( const char *name ) {
	char buf[32];

	trap->Cvar_VariableStringBuffer( name, buf, sizeof( buf ) );
	return atoi( buf );
}

// Your Force hand (h) as the gestures read it (qcommon/vr_force.h): its
// controller's pose as the engine reads it - from your head, in metres - its
// palm (cl_vr.cpp's: the left palm faces the controller's right, the right
// its left); the places on or off as the engine has them (vr_forceBody), and
// your powers as your player state says (as the engine's snapshot).
static void CG_VRF_Input( const vrClientState_t *vr, int h, vrfInput_t *in ) {
	vec3_t head[3];
	int i;

	memset( in, 0, sizeof( *in ) );
	in->hand = h;
	in->tracked = qtrue;
	in->body = (qboolean)( CG_VRF_SettingInt( "vr_forceBody" ) != 0 );
	in->known = cg.predictedPlayerState.fd.forcePowersKnown;
	VectorCopy( vr->gripOrigin[h], in->pos );
	VectorCopy( vr->headOrigin, in->head );
	for ( i = 0; i < 3; i++ ) {
		VectorCopy( vr->headAxis[i], head[i] );
	}
	in->headYaw = VR_AxisYaw( head );
	VectorScale( vr->gripAxis[h][1], ( h == VR_HAND_LEFT ) ? -1.0f : 1.0f, in->palm );
	in->palmUp = in->palm[2];
}

// How far out the hand is from its shoulder (VRF_Shoulder), and whether
// that's reaching out as the gestures have it (VRF_REACH, not hanging down).
static qboolean CG_VRF_Out( const vrfInput_t *in ) {
	vec3_t shoulder, way;

	VRF_Shoulder( in, shoulder );
	VectorSubtract( in->pos, shoulder, way );
	return (qboolean)( VectorNormalize( way ) >= VRF_REACH && way[2] > VRF_HANGING );
}

// Whether your Force hand (h) is held as the gestures' Grip wants it
// (qcommon/vr_force.h's VRF_Update): reaching out from its shoulder, not
// hanging down - its palm not turned up (Drain's coming), not in a place of
// yours, nor over your head. Not whether it's still, nor long enough: the
// glow shows whom it would catch as you aim.
static qboolean CG_VRF_GripPose( const vrClientState_t *vr, int h ) {
	vrfInput_t in;
	int at;

	CG_VRF_Input( vr, h, &in );
	at = VRF_PlaceOf( &in );
	return (qboolean)( CG_VRF_Out( &in ) && in.palmUp < VRF_PALM_UP && at != VRF_PLACE_OVERHEAD &&
		!VRF_Has( &in, vrfPlaces[at].power ) && !VRF_Has( &in, vrfPlaces[at].twin ) );
}

// The least Force a power costs, at any level (forcePowerNeeded: your level
// isn't sent you - and most of the places' powers cost less the higher it is).
static int CG_VRF_LeastCost( int power ) {
	int level, least = forcePowerNeeded[FORCE_LEVEL_1][power];

	for ( level = FORCE_LEVEL_2; level <= FORCE_LEVEL_3; level++ ) {
		least = Q_min( least, forcePowerNeeded[level][power] );
	}
	return least;
}

// What a place's power (Body Gestures) does now, as far as your player state
// says - the game's own reasons it does nothing (ForceHeal's, ForceRage's...,
// WP_ForcePowerUsable's), as it has them: CGF_REFUSED - dead; going already
// (Heal, the team powers - or Protect, Absorb, Rage or Seeing within 1.5 s of
// one of them, Speed or Mind Trick starting: w_force.c's
// forceAllowDeactivateTime, not sent you - as your game saw it start,
// CGF_LOCK_MS); not known, can't be used now (BG_CanUseFPNow: a ysalamiri, a
// duel, a vehicle...; down; in a pose, but for Heal), not enough Force (the
// least any level costs), nothing to heal, Rage's wait not over or your health
// too low for it, the team powers out of a team game; CGF_ENDS - Protect,
// Absorb, Rage or Seeing going, cast again: turned off, whatever else would
// stop it starting; else CGF_WORKS (the team powers whether or not anyone
// near you needs them: your game can't tell).
static int CG_VRF_CastWorks( const playerState_t *ps, int power ) {
	if ( power < 0 || power >= NUM_FORCE_POWERS || ps->stats[STAT_HEALTH] <= 0 ) {
		return CGF_REFUSED;
	}
	if ( ps->fd.forcePowersActive & ( 1 << power ) ) {
		if ( power != FP_PROTECT && power != FP_ABSORB && power != FP_RAGE && power != FP_SEE ) {
			return CGF_REFUSED;
		}
		return ( cgf.lockStart && cg.time - cgf.lockStart >= 0 && cg.time - cgf.lockStart < CGF_LOCK_MS ) ? CGF_REFUSED : CGF_ENDS;
	}
	if ( !( ps->fd.forcePowersKnown & ( 1 << power ) ) || CG_RP_IsDown() || ( power != FP_HEAL && CG_VR_Posed() ) ||
		!BG_CanUseFPNow( cgs.gametype, (playerState_t *)ps, cg.time, (forcePowers_t)power ) || ps->fd.forcePower < CG_VRF_LeastCost( power ) ) {
		return CGF_REFUSED;
	}
	switch ( power ) {
	case FP_HEAL:
		return ( ps->stats[STAT_HEALTH] < ps->stats[STAT_MAX_HEALTH] ) ? CGF_WORKS : CGF_REFUSED;
	case FP_RAGE:
		return ( ps->fd.forceRageRecoveryTime < cg.time && ps->stats[STAT_HEALTH] >= 10 ) ? CGF_WORKS : CGF_REFUSED;
	case FP_TEAM_HEAL:
	case FP_TEAM_FORCE:
		return ( cgs.gametype >= GT_TEAM ) ? CGF_WORKS : CGF_REFUSED;
	default:
		return CGF_WORKS;
	}
}

// How a place's power was cast (the log's).
static const char *CG_VRF_CastHow( int power ) {
	switch ( power ) {
	case FP_HEAL:		return "held still on your chest";
	case FP_RAGE:		return "the trigger on your chest";
	case FP_PROTECT:	return "held still before your face";
	case FP_ABSORB:		return "held still at your belly";
	case FP_SEE:		return "held still at your temple";
	case FP_TEAM_HEAL:	return "held still over your head";
	case FP_TEAM_FORCE:	return "the trigger over your head";
	default:			return "somehow";
	}
}

// A place's power, cast (cgf.castPower): its feel, played out over the frames
// after it - or, refused, a short dull buzz; ended, a soft release.
static void CG_VRF_CastFeel( int h ) {
	const int t = cg.time - cgf.castStart;

	if ( cgf.castPower < 0 ) {
		return;
	}
	if ( t < 0 ) {
		cgf.castPower = -1;	// (the clock gone back: it's over)
		return;
	}
	if ( cgf.castHow != CGF_WORKS ) {
		if ( !cgf.castStep ) {
			if ( cgf.castHow == CGF_ENDS ) {
				CG_VR_Feel( h, 0.25f, 15, 0.1f, 100 );
			} else {
				CG_VR_Feel( h, 0.45f, 90, 0.0f, 0 );
			}
			cgf.castStep = 1;
		}
		return;
	}
	switch ( cgf.castPower ) {
	case FP_HEAL:		// two slow, soft pulses, like a heartbeat
		if ( cgf.castStep == 0 ) {
			CG_VR_Feel( h, 0.45f, 60, 0.2f, 140 );
			cgf.castStep = 1;
		} else if ( cgf.castStep == 1 && t >= 280 ) {
			CG_VR_Feel( h, 0.35f, 60, 0.15f, 160 );
			cgf.castStep = 2;
		}
		break;
	case FP_RAGE:		// a deep rumble fading over half a second
		if ( t < 500 ) {
			CG_VR_FeelSteady( h, 0.9f * ( 1.0f - t / 500.0f ), 0.6f );
		}
		break;
	case FP_PROTECT:	// a firm press (and its hum while it lasts: CG_VR_ForceFrame)
		if ( cgf.castStep == 0 ) {
			CG_VR_Feel( h, 0.8f, 80, 0.25f, 220 );
			cgf.castStep = 1;
		}
		break;
	case FP_ABSORB:		// a rising buzz
		if ( t < 350 ) {
			CG_VR_FeelSteady( h, 0.1f + 0.6f * t / 350.0f, 0.3f );
		}
		break;
	case FP_SEE:		// one soft ping
		if ( cgf.castStep == 0 ) {
			CG_VR_Feel( h, 0.35f, 25, 0.15f, 120 );
			cgf.castStep = 1;
		}
		break;
	case FP_TEAM_HEAL:	// two quick pulses
	case FP_TEAM_FORCE:
		if ( cgf.castStep == 0 ) {
			CG_VR_Feel( h, 0.6f, 40, 0.0f, 0 );
			cgf.castStep = 1;
		} else if ( cgf.castStep == 1 && t >= 130 ) {
			CG_VR_Feel( h, 0.6f, 40, 0.0f, 0 );
			cgf.castStep = 2;
		}
		break;
	default:
		break;
	}
}

// Where your Force hand is, for the log: from your eyes, the way your head
// faces (VRF_FromEyes), and how its palm's turned.
static const char *CG_VRF_Where( const vrfInput_t *in ) {
	vec3_t e;

	VRF_FromEyes( in, e );
	return va( "%.2f m in front of your eyes, %.2f out to its side, %.2f up; its palm %.2f ahead, %.2f up", e[0] + 0.0f, e[1] + 0.0f,
		e[2] + 0.0f, VRF_PalmAhead( in ) + 0.0f, in->palmUp + 0.0f );	// (+ 0: no "-0.00")
}

// The Force grip held, nothing held down, your Force hand held still close in
// by you - not reaching out, in none of the places - as long as a place takes
// (vr_forceHold): where, in the log, once while it stays (for fitting the
// places to you). Body Gestures off, none of it.
static void CG_VRF_StillNowhere( const vrClientState_t *vr, int h ) {
	vrfInput_t in;

	if ( !vr->force.reaching || vr->force.held || vr->force.hand != h || !vr->handValid[h] ) {
		cgf.stillTime = 0;
		return;
	}
	CG_VRF_Input( vr, h, &in );
	if ( !in.body || CG_VRF_Out( &in ) || VRF_PlaceOf( &in ) != VRF_PLACE_NONE ) {
		cgf.stillTime = 0;
		return;
	}
	if ( !cgf.stillTime || Distance( in.pos, cgf.stillAt ) > VRF_PLACE_DRIFT ) {
		VectorCopy( in.pos, cgf.stillAt );
		cgf.stillTime = cg.time;
		cgf.stillSaid = qfalse;
		return;
	}
	if ( !cgf.stillSaid && cg.time - cgf.stillTime >= VRF_Hold( CG_VRF_Setting( "vr_forceHold", VRF_HOLD_DEFAULT ) ) ) {
		CG_VR_Log( "force: held still close in, in no place - %s\n", CG_VRF_Where( &in ) );
		cgf.stillSaid = qtrue;
	}
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
	int h, i, active, glow;

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
		cgf.casts = vr->force.casts;
		cgf.place = vr->force.place;
		cgf.castPower = -1;
		cgf.ticked = -1;
		cgf.lockActive = ps->fd.forcePowersActive & CGF_LOCKERS;	// (when they started isn't known)
		cgf.init = qtrue;
	}
	CG_VRF_Track( vr, ps );
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

	// Grip's aim landing on someone new (CG_VR_ForceAim, this frame - not aimed:
	// your body not drawn as yours): a tick - back on whom it was on a moment
	// ago, no one new (it wavered off them)
	glow = ( cgfa.now.time == cg.time && cgfa.now.power == FP_GRIP ) ? cgfa.now.pick : -1;
	if ( glow >= 0 ) {
		if ( glow != cgf.ticked || cg.time - cgf.tickedTime > CGF_TICK_AGAIN || cg.time - cgf.tickedTime < 0 ) {
			CG_VR_Feel( h, 0.2f, 12, 0.0f, 0 );
			CG_VR_Log( "force: Grip aimed at %i\n", glow );
		}
		cgf.ticked = glow;
		cgf.tickedTime = cg.time;
	}

	// what it did (a Push or a Pull the game won't do now: not felt)
	if ( vr->force.pushes != cgf.pushes ) {
		const qboolean known = (qboolean)( ( ps->fd.forcePowersKnown & ( 1 << FP_PUSH ) ) != 0 ), works = CG_VRF_ThrowWorks( ps, FP_PUSH );
		if ( works ) {
			CG_VR_Feel( h, 1.0f, 70, 0.6f, 140 );
		}
		CG_VR_Log( "force: Push, thrust at %.1f m/s%s\n", vr->force.speed,
			!known ? " (you don't know it)" : !works ? " (the game won't now)" : "" );
		cgf.pushes = vr->force.pushes;
	}
	if ( vr->force.pulls != cgf.pulls ) {
		const qboolean known = (qboolean)( ( ps->fd.forcePowersKnown & ( 1 << FP_PULL ) ) != 0 ), works = CG_VRF_ThrowWorks( ps, FP_PULL );
		if ( works ) {
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

	// the places (Body Gestures): held still in one with a power of yours there,
	// a soft tick; a power cast there, its feel (CG_VRF_CastFeel) - and where
	// the hand was, in the log (fitting the places to you). (Speed, Mind Trick,
	// Rage, Protect, Absorb or Seeing starting: the game won't turn the places'
	// ones off for a moment - CG_VRF_CastWorks.)
	if ( ( ps->fd.forcePowersActive & CGF_LOCKERS ) & ~cgf.lockActive ) {
		cgf.lockStart = cg.time;
	}
	cgf.lockActive = ps->fd.forcePowersActive & CGF_LOCKERS;
	if ( vr->force.place != cgf.place ) {
		if ( vr->force.place > VRF_PLACE_NONE && vr->force.place < VRF_PLACES && vr->handValid[h] ) {
			vrfInput_t in;
			CG_VRF_Input( vr, h, &in );
			CG_VR_Feel( h, 0.15f, 10, 0.0f, 0 );
			CG_VR_Log( "force: held still %s - %s\n", vrfPlaces[vr->force.place].name, CG_VRF_Where( &in ) );
		}
		cgf.place = vr->force.place;
	}
	if ( vr->force.casts != cgf.casts ) {
		cgf.castPower = vr->force.cast;
		cgf.castStart = cg.time;
		cgf.castHow = CG_VRF_CastWorks( ps, cgf.castPower );
		cgf.castStep = 0;
		CG_VR_Log( "force: %s, %s%s\n", CG_VR_ForcePowerName( cgf.castPower ), CG_VRF_CastHow( cgf.castPower ),
			cgf.castHow == CGF_ENDS ? " - turned off" : cgf.castHow == CGF_REFUSED ? " (the game won't now)" : "" );
		cgf.casts = vr->force.casts;
	}
	CG_VRF_CastFeel( h );
	CG_VRF_StillNowhere( vr, h );

	// what's going (the game's word) - Grip's strain heavier the faster whom it
	// holds goes (moved with your hand), and whom it held let go of flying, a
	// swoosh; Protect's faint hum
	if ( ps->fd.forcePowersActive & ( 1 << FP_PROTECT ) ) {
		CG_VR_FeelSteady( h, CGF_PROTECT_HUM, 0.2f );
	}
	active = ps->fd.forcePowersActive & ( ( 1 << FP_GRIP ) | ( 1 << FP_LIGHTNING ) | ( 1 << FP_DRAIN ) | ( 1 << FP_HEAL ) |
		( 1 << FP_RAGE ) | ( 1 << FP_PROTECT ) | ( 1 << FP_ABSORB ) | ( 1 << FP_SEE ) );
	if ( active & ( 1 << FP_GRIP ) ) {
		CG_VR_FeelSteady( h, CGF_STRAIN + ( CGF_STRAIN_MOST - CGF_STRAIN ) * Com_Clamp( 0.0f, 1.0f, CG_VRF_Speed( cgfa.caught ) / CGF_STRAIN_FULL ),
			0.6f );
	}
	if ( cgfa.letGoTime == cg.time && cgfa.letGoSpeed >= VR_FORCE_GRIP_THROWN ) {
		CG_VR_Feel( h, 0.7f, 30, 0.3f, 180 );
		CG_VR_Log( "force: Grip let %i go flying, at %.0f a second\n", cgfa.caught, cgfa.letGoSpeed );
	}
	cgfa.letGoTime = 0;
	if ( active & ( 1 << FP_LIGHTNING ) ) {
		CG_VR_FeelSteady( h, Q_flrand( 0.35f, 0.9f ), 0.5f );
	}
	if ( active & ( 1 << FP_DRAIN ) ) {
		CG_VR_FeelSteady( h, 0.3f, 0.1f );
	}
	for ( i = 0; i < NUM_FORCE_POWERS; i++ ) {
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

// What the server says of VR players' Force ("vrForce" in its serverinfo:
// game/g_vrforce.c) - 0, the game's own; from their hand, VR_FORCE_SERVED as
// this build aims it (1, one from before).
static int CG_VRF_Served( void ) {
	return atoi( Info_ValueForKey( CG_ConfigString( CS_SERVERINFO ), "vrForce" ) );
}

// Whether the server aims VR players' Force from their Force hand.
qboolean CG_VR_ForceServed( void ) {
	return (qboolean)( CG_VRF_Served() > 0 );
}

// Where your Force hand aimed as this frame draws it (your effects, the glow):
// as of the frame before - this frame's comes after the left eye's drawn, and
// the right eye draws as the left one did - lately (CGF_AIM_FRESH), else NULL.
static const cgfAim_t *CG_VRF_Drawn( void ) {
	const cgfAim_t *a = ( cgfa.now.time == cg.time ) ? &cgfa.was : &cgfa.now;

	if ( !a->time || cg.time - a->time < 0 || cg.time - a->time > CGF_AIM_FRESH ) {
		return NULL;
	}
	return a;
}

// Where a player's Force comes out for its effects (cg_players.c): their
// Force hand (VR_HAND_*) and the way it points (world axes) - yours, drawn as
// your VR body (where your hand aimed it, CG_VR_ForceAim: lately - else the
// controller's line), or another VR player's, from their avatar - on a server
// that aims it from there. -1: the game's own (the left hand, the way they face).
int CG_VR_ForceFx( centity_t *cent, vec3_t dir ) {
	const vrClientState_t *vr;
	const cgfAim_t *a;
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
	a = CG_VRF_Drawn();
	if ( a ) {
		VectorCopy( a->dir, dir );
		return h;
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

// Someone's body, where the Force aimed at them goes: the line up through the
// middle of their box (VR_ForceBodySpan, VR_ForceBodyPoint) - and where they
// stand.
typedef struct {
	float	x, y, bottom, top;
	vec3_t	origin;
} cgfBody_t;

static void CG_VRF_Body( const vec3_t origin, float lo, float hi, cgfBody_t *body ) {
	VectorCopy( origin, body->origin );
	body->x = origin[0];
	body->y = origin[1];
	VR_ForceBodySpan( origin[2], lo, hi, &body->bottom, &body->top );
}

// Someone's body as they're drawn: you (or whom you follow) as your game has
// you, anyone else as the snapshot does - their box as the server put it.
// qfalse: none (not in the snapshot; not solid - no body the server's Force
// finds, CONTENTS_BODY).
static qboolean CG_VRF_BodyOf( int n, cgfBody_t *body ) {
	const entityState_t *es;

	if ( n == cg.predictedPlayerState.clientNum ) {
		const playerState_t *ps = &cg.predictedPlayerState;
		CG_VRF_Body( ps->origin, DEFAULT_MINS_2, ( ps->pm_flags & PMF_DUCKED ) ? CROUCH_MAXS_2 : DEFAULT_MAXS_2, body );	// (a player's box)
		return qtrue;
	}
	if ( n < 0 || n >= ENTITYNUM_MAX_NORMAL || !cg_entities[n].currentValid ) {
		return qfalse;
	}
	es = &cg_entities[n].currentState;
	if ( !es->solid || es->solid == SOLID_BMODEL ) {
		return qfalse;
	}
	CG_VRF_Body( cg_entities[n].lerpOrigin, -(float)( ( es->solid >> 8 ) & 255 ), (float)( ( ( es->solid >> 16 ) & 255 ) - 32 ), body );
	return qtrue;
}

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
// can tell (g_vrforce.c's VRF_Snap): someone alive and drawn, with a body
// (CG_VRF_BodyOf), the power working on them (ForcePowerUsableOn: not down,
// not in a duel, no ysalamiri; an NPC not the shooter's companion nor, in
// Siege, any but a vehicle - a vehicle, Lightning alone; Grip, no one
// absorbing it), not on the shooter's side; CGF_HIDDEN, hidden from the
// shooter (cloaked, or tricking them, while their Seeing isn't going) - with
// their body.
static int CG_VRF_Target( int shooter, int power, int n, cgfBody_t *body ) {
	int eFlags, powerups, trick[4], active;
	qboolean duel;

	if ( n == shooter || !CG_VRF_BodyOf( n, body ) ) {
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
		active = ps->fd.forcePowersActive;
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
		active = es->forcePowersActive;
	}
	if ( ( eFlags & ( EF_DEAD | EF_NODRAW ) ) || duel || ( powerups & ( 1 << PW_YSALAMIRI ) ) ||
		( cgs.gametype == GT_CTY && ( powerups & ( ( 1 << PW_REDFLAG ) | ( 1 << PW_BLUEFLAG ) ) ) ) ||
		( power == FP_GRIP && ( active & ( 1 << FP_ABSORB ) ) ) || CG_RP_DownBody( &cg_entities[n] ) || CG_VRF_SameSide( n, shooter ) ) {
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

#define CGF_STICK	3.0f	// degrees: whom your Force hand's aimed at stays so till another's this much nearer a line - or
							// they're this far past the cone
// Whether a Grip from 'from' along line holds someone standing at spot, below
// level 3 (DoGripAction's InFront: the line's way and the way to them, side
// to side alone, within VR_FORCE_GRIP_FRONT).
static qboolean CG_VRF_InFront( const vec3_t from, const vec3_t line, const vec3_t spot ) {
	vec3_t side, flat;

	VectorSubtract( spot, from, side );
	side[2] = 0.0f;
	VectorNormalize( side );
	VectorSet( flat, line[0], line[1], 0.0f );
	VectorNormalize( flat );
	return (qboolean)( DotProduct( flat, side ) > VR_FORCE_GRIP_FRONT );
}

// Whom a VR player's Force (power) from 'from' along any of lines[0..count-1]
// goes at, as near as this can tell (g_vrforce.c's VRF_Snap, along each):
// someone it works on (CG_VRF_Target) - anywhere on them (VR_ForceBodyPoint) -
// within VR_FORCE_SNAP degrees of a line (Grip, VR_FORCE_SNAP_GRIP: a line
// that would hold them, CG_VRF_InFront), within range of 'from' (Grip: and of
// the shooter) and in sight of it (Grip's, as its catch sees - and where they
// stand, of where the shooter does), the one nearest a line - keep, whom it
// went at last, that much nearer (CGF_STICK):
// at, the point of them it goes at. -1: nobody - or, nearest, someone hidden
// from the shooter (never snapped onto, nor off).
static int CG_VRF_Nearest( int shooter, int power, const vec3_t from, float range, vec3_t lines[], int count, int keep, vec3_t at ) {
	const float *where = ( shooter == cg.predictedPlayerState.clientNum ) ? cg.predictedPlayerState.origin :
		cg_entities[shooter].lerpOrigin;
	float best = ( power == FP_GRIP ) ? VR_FORCE_SNAP_GRIP : VR_FORCE_SNAP;	// (degrees off a line: the nearest so far)
	int i, l, pick = -1, kind = CGF_NONE;

	for ( i = -1; i < cg.snap->numEntities; i++ ) {
		const int n = ( i < 0 ) ? cg.predictedPlayerState.clientNum : cg.snap->entities[i].number;	// (-1: you)
		cgfBody_t body;
		vec3_t point, to;
		trace_t tr;
		float dot = -1.0f, off;
		const int k = CG_VRF_Target( shooter, power, n, &body );

		if ( k == CGF_NONE || ( power == FP_GRIP && DistanceSquared( body.origin, where ) > range * range ) ) {
			continue;	// (Grip: no one further from them than it reaches)
		}
		for ( l = 0; l < count; l++ ) {
			vec3_t p;
			float d, t;
			if ( power == FP_GRIP && !CG_VRF_InFront( from, lines[l], body.origin ) ) {
				continue;	// (Grip: a line that wouldn't hold them - steeply over or under it, off to the side: let go at once)
			}
			VR_ForceBodyPoint( from, lines[l], body.x, body.y, body.bottom, body.top, p );
			VectorSubtract( p, from, to );
			d = VectorNormalize( to );
			if ( d < 1.0f || d > range ) {
				continue;
			}
			t = DotProduct( to, lines[l] );
			if ( t > dot ) {
				dot = t;
				VectorCopy( p, point );
			}
		}
		off = RAD2DEG( acosf( Com_Clamp( -1.0f, 1.0f, dot ) ) ) - ( ( n == keep ) ? CGF_STICK : 0.0f );
		if ( off >= best ) {
			continue;
		}
		CG_Trace( &tr, from, NULL, NULL, point, shooter, ( power == FP_GRIP ) ? MASK_PLAYERSOLID : MASK_SHOT );
		if ( tr.fraction < 1.0f && tr.entityNum != n ) {
			continue;	// (out of sight of the hand)
		}
		if ( power == FP_GRIP ) {
			CG_Trace( &tr, where, NULL, NULL, body.origin, shooter, MASK_PLAYERSOLID );
			if ( tr.fraction < 1.0f && tr.entityNum != n ) {
				continue;	// (where they stand out of sight of the shooter's: a held Grip lets go at once - DoGripAction)
			}
		}
		best = off;
		VectorCopy( point, at );
		pick = n;
		kind = k;
	}
	return ( kind == CGF_SEEN ) ? pick : -1;	// (hidden: the line as it is)
}

// Where a VR player's Lightning or Drain (power) goes from 'from' (cg_players.c
// draws it so): onto whom the server snaps it (g_vrforce.c), as near as this
// can tell (CG_VRF_Nearest): dir becomes the way to them (nobody, or someone
// hidden nearest: as it was).
void CG_VR_ForceSnap( int shooter, int power, const vec3_t from, float range, vec3_t dir ) {
	vec3_t lines[1], at;

	if ( !cg.snap ) {
		return;
	}
	VectorCopy( dir, lines[0] );
	if ( CG_VRF_Nearest( shooter, power, from, range, lines, 1, -1, at ) >= 0 ) {
		VectorSubtract( at, from, dir );
		VectorNormalize( dir );
	}
}

/*
--------------------------------------------------------------------------------
Where your Force hand aims
--------------------------------------------------------------------------------
On a server that aims the Force from the hand as this build does, your hand
aims it, along two lines: where the controller points, and - your hand held
up in view, out from your eyes - where it is as you see it (the line from
your eyes through it). Who's on either (CG_VRF_Nearest) is whom it goes
straight at: your avatar's forceDir, which the server snaps along too
(g_vrforce.c). Whom it goes at is the power's - the trigger's Lightning or
Drain held, theirs; else, the Force grip held (Grip, or not yet: the hand
held out as Grip's gesture wants, CG_VRF_GripPose), Grip's, if it can catch
anyone now (CG_VRF_GripWorks): whom it would, who glows. Nobody: the
controller's line. Someone caught, it's held along whichever line is nearer
them (CG_VRF_HeldLine): a held Grip goes where the hand points.
*/

#define CGF_AIM_VIEW	30.0f	// degrees: your hand this near where you look is up in view...
#define CGF_AIM_NEAR	0.2f	// ...this far out from your eyes (metres): aimed with as you see it too

// Where your Force comes from, as the server has it (g_vrforce.c's VRF_From):
// your hand, where it's drawn - or, the way to it from your eyes blocked (a
// wall, a door, someone), just short of that.
static void CG_VRF_From( const vec3_t hand, vec3_t from ) {
	const playerState_t *ps = &cg.predictedPlayerState;
	vec3_t eye, back;
	trace_t tr;

	VectorCopy( ps->origin, eye );
	eye[2] += ps->viewheight;
	CG_Trace( &tr, eye, NULL, NULL, hand, ps->clientNum, MASK_SHOT );
	if ( tr.startsolid ) {
		VectorCopy( eye, from );
	} else if ( tr.fraction < 1.0f ) {
		VectorSubtract( eye, tr.endpos, back );
		VectorNormalize( back );
		VectorMA( tr.endpos, 1.0f, back, from );
	} else {
		VectorCopy( hand, from );
	}
}

// Your Grip held (someone caught): along whichever of lines[0..count-1] is
// nearest whom it caught (cgfa.caught, as they're drawn) - of those that hold
// them below level 3 (CG_VRF_InFront), first - straight along it, not at them:
// a held Grip goes where the hand points (DoGripAction - level 3 floats them
// along it). Whom it caught not known (nor drawn): dir as it is.
static void CG_VRF_HeldLine( const vec3_t from, vec3_t lines[], int count, vec3_t dir ) {
	cgfBody_t body;
	float best = -9.0f;
	int l;

	if ( !CG_VRF_BodyOf( cgfa.caught, &body ) ) {
		return;
	}
	for ( l = 0; l < count; l++ ) {
		vec3_t p, to;
		float t;
		VR_ForceBodyPoint( from, lines[l], body.x, body.y, body.bottom, body.top, p );
		VectorSubtract( p, from, to );
		VectorNormalize( to );
		t = DotProduct( to, lines[l] ) + ( CG_VRF_InFront( from, lines[l], body.origin ) ? 4.0f : 0.0f );
		if ( t > best ) {
			best = t;
			VectorCopy( lines[l], dir );
		}
	}
}

// Where your Force hand aims (CG_VRB_FillAvatar, your avatar going out): hand,
// where it's drawn, and the controller's line (world, unit). dir: where your
// Force goes (world, unit).
void CG_VR_ForceAim( const vrClientState_t *vr, const vec3_t hand, const vec3_t controller, vec3_t dir ) {
	const playerState_t *ps = &cg.predictedPlayerState;
	vec3_t head[3], eye, world[3], from, lines[2], at;
	qboolean out, seen;
	int i, power = -1, n = -1;
	float range = 0.0f;

	if ( cg.secondEye && cgfa.now.time == cg.time ) {
		VectorCopy( cgfa.now.dir, dir );	// (the right eye: as the left one had it)
		return;
	}
	VectorCopy( controller, dir );
	if ( !vr || !cg.snap ) {
		return;
	}
	CG_VRF_Track( vr, ps );

	// the lines it aims along: the controller's - and, your hand out from your
	// eyes, the one from them through it: up in view, whom it goes at is found
	// along that too; a Grip held, it's held along it in view or not (no jump
	// as your hand leaves it)
	VectorCopy( controller, lines[0] );
	for ( i = 0; i < 3; i++ ) {
		VectorCopy( vr->headAxis[i], head[i] );
	}
	VR_ToWorld( vr, cg.refdef.vieworg, cg.refdef.viewaxis, vr->headOrigin, head, eye, world );
	VectorSubtract( hand, eye, lines[1] );
	out = (qboolean)( VectorNormalize( lines[1] ) >= CGF_AIM_NEAR * vr->worldScale );
	seen = (qboolean)( out && DotProduct( lines[1], world[0] ) >= cosf( DEG2RAD( CGF_AIM_VIEW ) ) );
	CG_VRF_From( hand, from );

	// whom it's aimed at - the power's (Grip's: held out as its gesture wants,
	// or held already, nobody caught)
	if ( CG_VRF_Served() >= VR_FORCE_SERVED && !CG_VRF_Watching() ) {
		const int active = ps->fd.forcePowersActive;
		if ( vr->force.held & VR_FORCE_LIGHTNING ) {
			power = FP_LIGHTNING;
			range = CG_VR_ForceReach( FP_LIGHTNING, ( active & ( 1 << FP_LIGHTNING ) ) ? ps->activeForcePass : FORCE_LEVEL_1 );
		} else if ( vr->force.held & VR_FORCE_DRAIN ) {
			power = FP_DRAIN;	// (going: its level and 3 - w_force.c)
			range = CG_VR_ForceReach( FP_DRAIN, ( active & ( 1 << FP_DRAIN ) ) ? ps->activeForcePass - FORCE_LEVEL_3 : FORCE_LEVEL_1 );
		} else if ( active & ( 1 << FP_GRIP ) ) {
			CG_VRF_HeldLine( from, lines, out ? 2 : 1, dir );
		} else if ( ( ( vr->force.held & VR_FORCE_GRIP ) || ( vr->force.reaching && CG_VRF_GripPose( vr, vr->force.hand ) ) ) &&
			CG_VRF_GripWorks( ps ) ) {
			power = FP_GRIP;
			range = VR_FORCE_GRIP_REACH;
		}
	}
	if ( power >= 0 ) {
		n = CG_VRF_Nearest( ps->clientNum, power, from, range, lines, seen ? 2 : 1, ( cgfa.now.power == power ) ? cgfa.now.pick : -1, at );
		if ( n >= 0 ) {
			VectorSubtract( at, from, dir );
			VectorNormalize( dir );
		}
	}
	if ( cgfa.now.time != cg.time ) {
		cgfa.was = cgfa.now;
	}
	cgfa.now.time = cg.time;
	VectorCopy( dir, cgfa.now.dir );
	cgfa.now.power = power;
	cgfa.now.pick = n;
	if ( power == FP_GRIP && n >= 0 ) {
		cgfa.gripWho = n;
		cgfa.gripTime = cg.time;
	}
}

// Whether cent glows: whom your Grip would catch, as this frame draws your
// Force hand's aim (CG_VRF_Drawn) - cg_vrForceGlow.
qboolean CG_VR_ForceGlow( const centity_t *cent ) {
	const cgfAim_t *a = CG_VRF_Drawn();

	return (qboolean)( cg_vrForceGlow.integer && a && a->power == FP_GRIP && cent && cent->currentState.number == a->pick );
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
