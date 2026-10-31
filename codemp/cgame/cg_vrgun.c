/*
===========================================================================
OpenJK VR (multiplayer) - both hands on your gun.

Your other hand (the one without the gun: the left, or the right when you
play left-handed - your Force hand) takes hold of the gun in your gun hand
too. Put it on the gun - a rifle's barrel, anywhere from a hand's width
ahead of your gun hand to just past the muzzle; a pistol's under your gun
hand, round it - and squeeze its grip: that squeeze takes hold of the gun
(the engine's told the hand's there, VR_SetGrabZones: it's a grab, not the
Force or Use). Let go of the grip and it lets go.

  rifles            the E-11, the disruptor, the bowcaster, the repeater,
                    the DEMP2, the flechette, the rocket launcher and the
                    concussion rifle: both your hands aim it - the shots
                    too, on any server (the engine: VR_SetGunHold, the aim
                    yours). It's held where your other hand took hold of it
                    (taking hold doesn't move it, on its bore or off it): it
                    turns about your gun hand as your other hand goes round
                    it - along the barrel, it doesn't
  pistols           the Bryar pistols: steadied, aiming as your gun hand
                    points it
  the rest          the stun baton, thermal detonators, trip mines and det
                    packs stay in one hand

Your other hand's trigger still alt-fires as it holds it. It's not your
Force hand meanwhile: let go to use the Force. Nor does that squeeze take
hold of anything else till it's let go (CG_VR_BlockGrab: the gun put away
and a saber out, its hilt).

A rifle slips out of your other hand if your hands come too near each other
along the line it's held along (5 cm - 10 turned far round), your other
hand's pulled well past the muzzle, or the gun's turned right round from
where your gun hand points - past 120 degrees, or where it can't follow your
other hand (VRG_SLIP_*); short of that it holds on, your gun hand needn't
turn with it (the engine turns the gun the least way onto the line). A
pistol slips if your other hand's pulled away from under your gun hand. It
takes hold again only once the grip's let go and squeezed again. Putting the
gun away or switching guns, a scope, a menu, dying: it lets go. (It needs
the gun aimed with your hand: Gun Aim, CG_VR_HandAim.)

What you feel: a firm tick in your other hand as it takes hold (a soft one
in your gun hand), a soft one as it lets go, a buzz as it slips. What you
see: your fist where it holds a rifle, closed along the barrel (cg_vrbody.c,
as on a saber's hilt), settling onto it; your hand round your gun hand as
you hold a pistol. Everyone else sees your gun aimed as you aim it, your
other hand where you hold it. The log (vrdebug.log): taking hold - where -
letting go and slipping, and why, how long it held it and how far round it
turned it from where your gun hand points.

Two-Handed Guns on Setup > Controls, VR View (cg_vrTwoHandGuns) turns it
off: your other hand's grip does what it would without it (the Force, a
holster).
===========================================================================
*/

#include "cg_local.h"

#define VRG_REACH			0.10f	// metres: your other hand this near a rifle's bore (or the place under your gun hand,
									// a pistol's) takes hold of it as its grip's squeezed...
#define VRG_AHEAD			0.08f	// ...a rifle's from this far ahead of your gun hand's grip...
#define VRG_PAST			0.06f	// ...to this far past its muzzle...
#define VRG_MUZZLE_MIN		0.20f	// ...(or past here, the muzzle nearer, or not found)
#define VRG_CUP_DROP		0.07f	// metres: a pistol's taken round your gun hand from this far below its grip
#define VRG_SLIP_NEAR		0.05f	// metres along the line it's held along: your hands nearer each other than this, a
									// rifle slips...
#define VRG_SLIP_NEAR_TURNED 0.10f	// ...or than this, the gun turned further round than...
#define VRG_SLIP_NEAR_TURN	60.0f	// ...this (degrees: the engine's near fade's no steadier)...
#define VRG_SLIP_PAST		0.25f	// ...your other hand further than this past the muzzle...
#define VRG_SLIP_ROUND		120.0f	// ...the gun turned further than this from where your gun hand points (degrees), or
#define VRG_SLIP_LEAN		0.6f	// where it can't follow your other hand: every way through it nearer fitting than this
									// (the engine's lean: 0, one way; 1, any way; under 0, turned away from the line's
									// offset; over 1, past it - VR_GunBore), or...
#define VRG_SLIP_OFF		2.0f	// ...the line held off your gun hand's grip, against how far apart, times the tangent of
									// half its turn, past this - the engine gives the aim back from there (VR_GunHoldFits)
#define VRG_SLIP_CUP		0.18f	// metres: a pistol slips with your other hand this far from under your gun hand
#define VRG_EASE			0.1f	// seconds your other hand takes to settle onto a rifle's barrel, and to come off it
#define VRG_WORD_MS			100		// ms the module's word on your hands lasts (not worked out since: none)

static struct {
	int			how;			// VR_GUN_HOLD_*: how your other hand holds your gun (NONE: it doesn't)...
	int			hand;			// ...which hand that is...
	int			weapon;			// ...the gun it holds...
	float		bore[2];		// ...the line it's held along from your gun hand's grip, to its left and up (metres): its
								// bore, as far off it as your other hand took hold...
	float		hold[2];		// ...that: where your other hand took hold of it off its bore, to its left and up (a pistol: none)
	int			since;			// ...the cg.time it took hold...
	float		turned;			// ...and the furthest round it's turned it since from where your gun hand points (degrees)
	int			zones;			// hands whose squeeze takes hold of your gun now (bit per hand)...
	int			zoneTime;		// ...as of this cg.time
	qboolean	grabWas[2];		// each hand's grip held as a grab last frame
	qboolean	blocked[2];		// it slipped out of that hand: its grip takes hold again only once let go
	float		ease;			// how far your other hand's settled onto a rifle's barrel (0..1)...
	int			easeHand;		// ...that hand
	int			time;			// cg.time last worked out
} vrg = { VR_GUN_HOLD_NONE, -1, WP_NONE, { 0.0f, 0.0f }, { 0.0f, 0.0f }, 0, 0.0f, 0, 0, { qfalse, qfalse }, { qfalse, qfalse }, 0.0f, -1,
	0 };

// How a gun's held with both hands (VR_GUN_HOLD_*).
static int CG_VRG_Kind( int weapon ) {
	switch ( weapon ) {
	case WP_BRYAR_PISTOL:
	case WP_BRYAR_OLD:
		return VR_GUN_HOLD_STEADY;
	case WP_BLASTER:
	case WP_DISRUPTOR:
	case WP_BOWCASTER:
	case WP_REPEATER:
	case WP_DEMP2:
	case WP_FLECHETTE:
	case WP_ROCKET_LAUNCHER:
	case WP_CONCUSSION:
		return VR_GUN_HOLD_AIM;
	default:
		return VR_GUN_HOLD_NONE;	// (fists, the saber, the stun baton, thermal detonators, trip mines, det packs)
	}
}

// (the log's)
static const char *CG_VRG_Name( int weapon ) {
	switch ( weapon ) {
	case WP_BRYAR_PISTOL:		return "Bryar pistol";
	case WP_BRYAR_OLD:			return "old Bryar pistol";
	case WP_BLASTER:			return "E-11";
	case WP_DISRUPTOR:			return "disruptor";
	case WP_BOWCASTER:			return "bowcaster";
	case WP_REPEATER:			return "repeater";
	case WP_DEMP2:				return "DEMP2";
	case WP_FLECHETTE:			return "flechette";
	case WP_ROCKET_LAUNCHER:	return "rocket launcher";
	case WP_CONCUSSION:			return "concussion rifle";
	default:					return "gun";
	}
}

static const char *CG_VRG_HandName( int h ) {
	return ( h == VR_HAND_LEFT ) ? "left" : "right";
}

// Where your gun's muzzle is from your gun hand's grip, in the gun's own axes
// (metres: along it, to its left, up) - as it's drawn in your hand
// (CG_VR_HeldWeaponMuzzle). Not found: none to its sides, VRG_MUZZLE_MIN ahead.
static void CG_VRG_Muzzle( const vrClientState_t *vr, int weapon, vec3_t m ) {
	vec3_t u;

	if ( vr->worldScale > 0.0f && CG_VR_HeldWeaponMuzzle( weapon, u ) ) {
		VectorScale( u, 1.0f / vr->worldScale, m );
	} else {
		VectorSet( m, VRG_MUZZLE_MIN, 0.0f, 0.0f );
	}
}

// Where hand off is from your gun's bore by your gun hand's grip, as your gun
// hand (gun) alone aims it (m: its muzzle, CG_VRG_Muzzle): along it, to its
// left and up (metres).
static void CG_VRG_FromBore( const vrClientState_t *vr, int gun, int off, const vec3_t m, vec3_t at ) {
	vec3_t q, d;
	int i;

	VectorMA( vr->gripOrigin[gun], m[1], vr->gunAimAxis[1], q );
	VectorMA( q, m[2], vr->gunAimAxis[2], q );
	VectorSubtract( vr->gripOrigin[off], q, d );
	for ( i = 0; i < 3; i++ ) {
		at[i] = DotProduct( d, vr->gunAimAxis[i] );
	}
}

// Where hand off is on your gun as your gun hand alone aims it - a rifle's,
// from its bore by your gun hand's grip (CG_VRG_FromBore): how far along it
// (along) and how far off it; a pistol's (kind: VR_GUN_HOLD_STEADY), how far
// from under your gun hand.
static float CG_VRG_Where( const vrClientState_t *vr, int kind, int gun, int off, const vec3_t m, float *along ) {
	vec3_t q, at;

	if ( kind == VR_GUN_HOLD_STEADY ) {
		VectorMA( vr->gripOrigin[gun], -VRG_CUP_DROP, vr->gunAimAxis[2], q );
		*along = 0.0f;
		return Distance( vr->gripOrigin[off], q );
	}
	CG_VRG_FromBore( vr, gun, off, m, at );
	*along = at[0];
	return sqrtf( at[1] * at[1] + at[2] * at[2] );
}

// Whether hand off's where a squeeze takes hold of your gun (VRG_REACH).
static qboolean CG_VRG_InReach( const vrClientState_t *vr, int kind, int gun, int off, const vec3_t m, float *along ) {
	const float d = CG_VRG_Where( vr, kind, gun, off, m, along );

	if ( d > VRG_REACH ) {
		return qfalse;
	}
	return ( kind == VR_GUN_HOLD_STEADY || ( *along >= VRG_AHEAD && *along <= Q_max( m[0], VRG_MUZZLE_MIN ) + VRG_PAST ) ) ?
		qtrue : qfalse;
}

// Whether your gun's still held in hand off, both hands tracked - else why it
// slipped, and how far (said). A rifle's held along the line through that hand
// where it took hold (its bore, vrg.hold off it), the engine turning the gun
// the least way onto it - worked out as the engine does (VR_GunBore): how far
// along that line your hands are apart; how far round from where your gun hand
// points the gun is (noted: vrg.turned) and how near every way through your
// other hand comes to fitting (lean). A pistol: how far your other hand's from
// under your gun hand.
static qboolean CG_VRG_Holds( const vrClientState_t *vr, int kind, int gun, int off, const vec3_t m, char *said, int size ) {
	const float bl = m[1] + vrg.hold[0], bu = m[2] + vrg.hold[1], r2 = bl * bl + bu * bu;
	vec3_t d, o;
	float along, apart, den, lambda, cosine, round;

	if ( kind == VR_GUN_HOLD_STEADY ) {
		apart = CG_VRG_Where( vr, kind, gun, off, m, &along );
		if ( apart > VRG_SLIP_CUP ) {
			Com_sprintf( said, size, "pulled away from your gun hand, %.0f cm", apart * 100.0f );
			return qfalse;
		}
		return qtrue;
	}
	VectorSubtract( vr->gripOrigin[off], vr->gripOrigin[gun], d );
	VectorScale( vr->gunAimAxis[1], bl, o );
	VectorMA( o, bu, vr->gunAimAxis[2], o );
	apart = DotProduct( d, d ) - r2;
	apart = ( apart > 0.0f ) ? sqrtf( apart ) : 0.0f;
	if ( apart < VRG_SLIP_NEAR ) {
		Com_sprintf( said, size, "your hands together, %.0f cm apart along it", apart * 100.0f );
		return qfalse;
	}
	// (how far round, as the engine turns it - past where any way fits too, its
	// cosine (1 + it) = den / (apart - lambda), both one sign; none to tell,
	// right round)
	den = DotProduct( vr->gunAimAxis[0], d ) + apart;
	lambda = ( fabsf( den ) > 1e-6f ) ? ( DotProduct( o, d ) - r2 ) / den : apart;
	cosine = ( fabsf( apart - lambda ) > 1e-6f ) ? Com_Clamp( -1.0f, 1.0f, den / ( apart - lambda ) - 1.0f ) : -1.0f;
	round = RAD2DEG( acosf( cosine ) );
	vrg.turned = Q_max( vrg.turned, round );
	if ( apart < VRG_SLIP_NEAR_TURNED && round > VRG_SLIP_NEAR_TURN ) {
		Com_sprintf( said, size, "your hands together, %.0f cm apart along it", apart * 100.0f );
		return qfalse;
	}
	if ( round > VRG_SLIP_ROUND ) {
		Com_sprintf( said, size, "turned right round, %.0f degrees from where your gun hand points", round );
		return qfalse;
	}
	if ( lambda > VRG_SLIP_LEAN * apart || ( cosine > -1.0f &&
			sqrtf( r2 ) * sqrtf( ( 1.0f - cosine ) / ( 1.0f + cosine ) ) > VRG_SLIP_OFF * apart ) ) {
		Com_sprintf( said, size, "round where it can't follow your hand, %.0f degrees from where your gun hand points", round );
		return qfalse;
	}
	if ( apart > Q_max( m[0], VRG_MUZZLE_MIN ) + VRG_SLIP_PAST ) {
		Com_sprintf( said, size, "pulled past its muzzle, %.0f cm past it", ( apart - m[0] ) * 100.0f );
		return qfalse;
	}
	return qtrue;
}

// The engine told (every frame): your other hand on your gun, how, its bore.
static void CG_VRG_Say( void ) {
	if ( CG_VR_Engine() && trap->ext.VR_SetGunHold ) {
		trap->ext.VR_SetGunHold( vrg.how ? vrg.hand : -1, vrg.how, vrg.bore[0], vrg.bore[1] );
	}
}

// Your other hand lets go of your gun (slipped: as it slips, why - its grip
// then takes hold again only once it's let go; else let go, why if not its
// grip let go).
static void CG_VRG_LetGo( const char *slipped, const char *why ) {
	const int h = vrg.hand;
	char held[96];

	if ( vrg.how == VR_GUN_HOLD_AIM ) {
		Com_sprintf( held, sizeof( held ), " - held %.1f s, turned up to %.0f degrees from where your gun hand points",
			( cg.time - vrg.since ) * 0.001f, vrg.turned );
	} else {
		Com_sprintf( held, sizeof( held ), " - held %.1f s", ( cg.time - vrg.since ) * 0.001f );
	}
	if ( slipped ) {
		CG_VR_Feel( h, 0.45f, 60, 0.0f, 0 );
		CG_VR_Log( "two hands: the %s slipped out of your %s hand (%s)%s\n", CG_VRG_Name( vrg.weapon ), CG_VRG_HandName( h ),
			slipped, held );
		vrg.blocked[h] = qtrue;
	} else {
		CG_VR_Feel( h, 0.3f, 20, 0.0f, 0 );
		CG_VR_Log( "two hands: your %s hand let go of the %s%s%s%s%s\n", CG_VRG_HandName( h ), CG_VRG_Name( vrg.weapon ),
			why ? " (" : "", why ? why : "", why ? ")" : "", held );
	}
	vrg.how = VR_GUN_HOLD_NONE;
	vrg.bore[0] = vrg.bore[1] = 0.0f;
}

// Why your gun's gone from your other hand's hold, holding it (the log's): put
// away (switching from it, the switch begun), Two-Handed Guns off, its scope,
// or otherwise not aimed with your hand (a menu, dying).
static const char *CG_VRG_Gone( const playerState_t *ps, int weapon ) {
	if ( weapon != vrg.weapon || ps->weaponstate == WEAPON_DROPPING ) {
		return "put away";
	}
	if ( !cg_vrTwoHandGuns.integer ) {
		return "Two-Handed Guns off";
	}
	return ps->zoomMode ? "its scope" : "out of your hand";
}

/*
=================
CG_VR_GunFrame

Both hands on your gun, worked out once a frame (at its end: CG_VR_BodyEndFrame
- your body drawn or not), before the engine's told where a squeeze takes
hold of something: whether your other hand takes hold of your gun, holds it,
lets go of it or it slips; where a squeeze takes hold of it now; and the
engine told how your hands hold it (VR_SetGunHold).
=================
*/
void CG_VR_GunFrame( const vrClientState_t *vr ) {
	const playerState_t *ps = &cg.predictedPlayerState;
	const int gun = CG_VR_GunHand(), off = ( gun == VR_HAND_LEFT ) ? VR_HAND_RIGHT : VR_HAND_LEFT;
	const int weapon = ps->weapon, kind = CG_VRG_Kind( weapon );
	const int offered = vrg.zones;	// (as the last frame ended, however long ago: the engine's grab said how fresh)
	const float dt = ( vrg.time && cg.time - vrg.time > 0 ) ? Com_Clamp( 0.0f, 0.1f, ( cg.time - vrg.time ) * 0.001f ) : 0.0f;
	qboolean able, grabbing, fresh;
	char why[128];
	vec3_t m, at;
	float along;
	int h, was;

	if ( vrg.time == cg.time ) {
		CG_VRG_Say();	// (the second eye: as worked out)
		return;
	}
	vrg.time = cg.time;
	was = vrg.how;

	// a grip let go of takes hold again
	for ( h = 0; h < 2; h++ ) {
		if ( !vr || !vr->grabbing[h] ) {
			vrg.blocked[h] = qfalse;
		}
	}

	// a gun for both hands in your gun hand, aimed with it (CG_VR_HandAim: not
	// scoped, in a menu, at an emplaced gun or in a vehicle), you alive (a
	// disintegrated body's PM_NOCLIP, not PM_DEAD: its health says) and playing
	able = (qboolean)( vr && cg_vrTwoHandGuns.integer && kind != VR_GUN_HOLD_NONE && CG_VR_HandAim() && cg.snap &&
		!( cg.snap->ps.pm_flags & PMF_FOLLOW ) && ps->persistant[PERS_TEAM] != TEAM_SPECTATOR && ps->pm_type != PM_DEAD &&
		ps->pm_type != PM_SPECTATOR && ps->pm_type != PM_INTERMISSION && ps->stats[STAT_HEALTH] > 0 );
	grabbing = (qboolean)( vr && vr->grabbing[off] );
	fresh = (qboolean)( grabbing && !vrg.grabWas[off] );
	if ( able ) {
		CG_VRG_Muzzle( vr, weapon, m );
	} else {
		VectorClear( m );
	}

	if ( vrg.how ) {
		// holding it - as long as that hand's grip's held, the same gun in your
		// gun hand
		if ( vrg.hand != off ) {
			CG_VRG_LetGo( NULL, "your hands changed over" );
		} else if ( !able || weapon != vrg.weapon ) {
			CG_VRG_LetGo( NULL, CG_VRG_Gone( ps, weapon ) );
		} else if ( !grabbing ) {
			CG_VRG_LetGo( NULL, NULL );
		} else if ( vr->handValid[off] && vr->handValid[gun] && !CG_VRG_Holds( vr, kind, gun, off, m, why, sizeof( why ) ) ) {
			CG_VRG_LetGo( why, NULL );
		} else {
			vrg.bore[0] = m[1] + vrg.hold[0];
			vrg.bore[1] = m[2] + vrg.hold[1];
		}
	} else if ( able && fresh && ( offered & ( 1 << off ) ) && !vrg.blocked[off] ) {
		// a squeeze where it was offered (the engine grabbed for it): taken hold
		// of - a rifle where your other hand is, on its bore or off it (held
		// along the line through it there: taking hold doesn't move it); that
		// squeeze takes hold of nothing else till it's let go
		CG_VRG_FromBore( vr, gun, off, m, at );
		vrg.how = kind;
		vrg.hand = off;
		vrg.weapon = weapon;
		vrg.hold[0] = ( kind == VR_GUN_HOLD_AIM ) ? at[1] : 0.0f;
		vrg.hold[1] = ( kind == VR_GUN_HOLD_AIM ) ? at[2] : 0.0f;
		vrg.bore[0] = m[1] + vrg.hold[0];
		vrg.bore[1] = m[2] + vrg.hold[1];
		vrg.since = cg.time;
		vrg.turned = 0.0f;
		CG_VR_BlockGrab( off );
		CG_VR_Feel( off, 0.5f, 35, 0.0f, 0 );
		CG_VR_Feel( gun, 0.25f, 20, 0.0f, 0 );
		if ( kind == VR_GUN_HOLD_STEADY ) {
			CG_VR_Log( "two hands: your %s hand took hold of the %s, round your gun hand\n", CG_VRG_HandName( off ),
				CG_VRG_Name( weapon ) );
		} else {
			CG_VR_Log( "two hands: your %s hand took hold of the %s's barrel, %.0f cm along it from your gun hand and %.0f cm off it "
				"(its muzzle %.0f)\n", CG_VRG_HandName( off ), CG_VRG_Name( weapon ), at[0] * 100.0f,
				sqrtf( at[1] * at[1] + at[2] * at[2] ) * 100.0f, m[0] * 100.0f );
		}
	}
	for ( h = 0; h < 2; h++ ) {
		vrg.grabWas[h] = (qboolean)( vr && vr->grabbing[h] );
	}

	// where a squeeze takes hold of it now (none while it holds it, or its grip
	// held since it slipped)
	vrg.zones = 0;
	if ( able && !vrg.how && !vrg.blocked[off] && vr->handValid[off] && vr->handValid[gun] &&
		CG_VRG_InReach( vr, kind, gun, off, m, &along ) ) {
		vrg.zones = 1 << off;
	}
	vrg.zoneTime = cg.time;

	// your other hand settling onto a rifle's barrel from as it takes hold, or
	// coming off it
	if ( vrg.how == VR_GUN_HOLD_AIM ) {
		if ( vrg.easeHand != vrg.hand ) {
			vrg.ease = 0.0f;
			vrg.easeHand = vrg.hand;
		}
		if ( was == VR_GUN_HOLD_AIM ) {
			vrg.ease = Q_min( 1.0f, vrg.ease + dt / VRG_EASE );
		}
	} else {
		vrg.ease = Q_max( 0.0f, vrg.ease - dt / VRG_EASE );
		if ( vrg.ease <= 0.0f ) {
			vrg.easeHand = -1;
		}
	}
	CG_VRG_Say();
}

// Hands whose squeeze takes hold of your gun now (bit per hand): the engine's
// told (VR_SetGrabZones) - and holsters give way to it (CG_VR_TwoHandZones).
int CG_VR_GunZones( void ) {
	return ( cg.time - vrg.zoneTime >= 0 && cg.time - vrg.zoneTime <= VRG_WORD_MS ) ? vrg.zones : 0;
}

// Whether hand h holds your gun's barrel - or is settling onto it, or coming
// off it: ease, how far it's on it (0..1) - and the way the barrel runs (world).
qboolean CG_VR_GunBarrelHand( int h, float *ease, vec3_t dir ) {
	const vrClientState_t *vr = CG_VR();
	const int gun = CG_VR_GunHand();
	vec3_t gl, al[3], g, aim[3];
	int i;

	if ( !vr || h != vrg.easeHand || h == gun || vrg.ease <= 0.0f || cg.time - vrg.time < 0 ||
		cg.time - vrg.time > VRG_WORD_MS || !vr->handValid[gun] ) {
		return qfalse;
	}
	VectorCopy( vr->gripOrigin[gun], gl );
	for ( i = 0; i < 3; i++ ) {
		VectorCopy( vr->aimAxis[gun][i], al[i] );
	}
	VR_ToWorld( vr, cg.refdef.vieworg, cg.refdef.viewaxis, gl, al, g, aim );
	VectorCopy( aim[0], dir );
	*ease = vrg.ease;
	return qtrue;
}
