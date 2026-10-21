/*
===========================================================================
OpenJK VR (multiplayer) - holsters: your weapons hang on your body

Four holsters (bg_vrholster.h): your right hip, your left hip, and over
your right and left shoulders (what's in those hangs on your back). Each
holds one weapon, any weapon; your saber starts on your right hip, a
second one on your left, the shoulders empty. Reach a hand to one - within
15 cm of what hangs at a hip, over the shoulder for a shoulder's - and you
feel a tick (a firmer one if it holds something) where a squeeze would do
something there; squeeze that hand's grip and:

- an empty hand takes what's in it: a hilt where your hand closed on it,
  the right way round or reversed as you took it, unlit (the quick draw:
  the trigger held as you take it lights it as it comes out); a gun goes
  to your gun hand (only that hand takes guns);
- a hand holding something puts it in an empty holster (a saber's blade
  must be out first: never put away lit);
- a hand holding something at a full one swaps them.

A squeeze at your other hand's hilt takes hold of it as ever, even beside a
holster (cg_vrbody.c, CG_VRB_TwoHand); elsewhere the grips are Use and the
Force, as ever.

One weapon at a time, as the game has it: taking a gun puts a saber in hand
back in its holster, taking a saber puts the gun back. The stick still
switches weapons: the new one to your hand, what you held back to its
holster if it came from one. Your hands can be empty: with a saber, the
game keeps it chosen and unlit, and the triggers do nothing until you take
something (BUTTON_VR_HOLSTERS: this build's servers keep a VR player's
saber unlit as it comes out of its holster); without one, the game's fists.

Two sabers: either can be taken first. It fights alone, in any single style
(BUTTON_VR_ONESABER), and the game makes it your first saber: the server
changes them over (GENCMD_VR_SABERSWAP), and when it says so ("vs" in your
info) your saber1/saber2 and color1/color2 change over with them, and so do
the holsters they hang in. Take the other too: two. Put one away: one. A
squeeze at a holster meanwhile (both taken at once, say) is done the moment
they have. A siege class's sabers don't change over: its second comes out
only with the first in hand, and the first goes back only after it.

What's where is kept (cg_vrHolsterLayout): after dying, each weapon you get
back hangs where it hung. Everyone on this build sees what's in your
holsters (userinfo "vrh", sent no more often than every 2.5 s: the server
shows what you have of it), and everyone's hilts hang on their belts while
not in hand - desktop players' and bots' too (CS_PLAYERS "hs"). A cloaked
player's hang on no one.

Only on a server of this build that has them ("vrHolsters 1"), in VR, your
VR body and its sabers drawn in first person (cg_vrBody, cg_vrSaber, not
cg_thirdPerson), with cg_vrHolsters on; elsewhere weapons switch as ever
and nothing hangs. The rules go with every command from the first one
(spawned, what you're given is in its holster - watching, loading, a menu
up: as your hands are, or empty).

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "cg_local.h"
#include "game/bg_vrholster.h"

#define VRH_REACH_HIP		0.15f	// m: a hand this close to what hangs at a hip is at that holster
#define VRH_REACH_SHOULDER	0.22f	// m: ...this close to what hangs on your back, reached over the shoulder (not in front of it)
#define VRH_SEND_MS			2500	// what's in your holsters goes out with your info no more often than this
#define VRH_SWAP_MS			5000	// your sabers changing over: given up on if the server hasn't by then
#define VRH_DRAW_MS			1500	// a quick draw waits this long at most for the game to have the saber out
#define VRH_WANT_MS			2000	// what you took out is in your hand this long at most before the game has it there
#define VRH_FOLLOW_MS		5000	// ...and the game's switch to it, this long after, is still to what you took out
#define VRH_MAX_MODELS		64		// standalone models of what hangs (each saber hilt, gun - everyone's)...
#define VRH_MODEL_KEEP		1000	// ...the one least lately drawn, not this lately, makes way for another

// what each holster can show
typedef enum {
	VRH_KIND_NONE,
	VRH_KIND_SABER,
	VRH_KIND_PISTOL,
	VRH_KIND_RIFLE,
	VRH_KIND_BOMB
} vrhKind_t;

// where something hangs: its model's place and axes, and the point a hand takes it at
typedef struct {
	qboolean	valid;
	vec3_t		origin, axis[3];
	vec3_t		grab;			// ...where a hand reaches it...
	vec3_t		anchor;			// ...the tag it's by (a shoulder's: reached from behind it, not below)
	vec3_t		up, fwd;		// the body's up there (the marker's ring goes round it), and forward
	vec3_t		blade;			// a hilt: the way its (first) blade points
	float		lo, hi;			// ...and where along it hands can hold it, from its origin
} vrhHang_t;

// a model drawn on its own (not bolted to anything): a saber's hilt, a gun
typedef struct {
	char		path[MAX_QPATH];
	int			skin;
	void		*g2;
	int			blade[2];		// a hilt's blade bolts (-1: none)
	int			flash;			// a gun's muzzle bolt
	int			used;			// cg.time it was last asked for
} vrhModel_t;

static vrhModel_t	vrhModels[VRH_MAX_MODELS];
static int			vrhNumModels;

// a player model's tags (or bones) where things hang: *hip_r, *hip_l,
// *shldr_r, *shldr_l, *back; pelvis, thoracic (a model without the tags)
enum { VRH_TAG_HIP_R, VRH_TAG_HIP_L, VRH_TAG_SHLDR_R, VRH_TAG_SHLDR_L, VRH_TAG_BACK, VRH_BONE_PELVIS, VRH_BONE_THORACIC, VRH_NUM_TAGS };
static const char *vrhTagNames[VRH_NUM_TAGS] = { "*hip_r", "*hip_l", "*shldr_r", "*shldr_l", "*back", "pelvis", "thoracic" };

typedef struct {
	void		*g2;			// the player model asked...
	int			time;			// ...then (cg.time)...
	int			asked;			// ...for these...
	int			bolt[VRH_NUM_TAGS];	// ...each one's bolt (-1: not on it)
} vrhTags_t;

static vrhTags_t	vrhTags[MAX_CLIENTS];

static struct {
	qboolean	on;							// holsters in play for you this frame
	int			layout[VR_NUM_HOLSTERS];	// what each holster holds (as kept: cg_vrHolsterLayout)
	int			layoutCount;				// cg_vrHolsterLayout's modificationCount as read
	vrhHang_t	hang[VR_NUM_HOLSTERS];		// where each holster's item hangs on your body, as last drawn...
	int			hangTime;					// ...then (cg.time)
	int			weapon;						// the weapon as last seen (ps.weapon)
	int			want;						// the weapon you took out or left in hand (cg.weaponSelect)...
	int			wantTime;					// ...then (cg.time; 0: none): it's the one in hand till the game's switch is done
	int			at[2];						// the holster each hand is at, where a squeeze does something (-1: none)
	int			zones;						// hands whose squeeze takes, puts or swaps now
	qboolean	grabWas[2];					// that hand's grip was held as a grab last frame
	int			swapTime;					// cg.time you asked for your sabers to change over (0: not waiting)
	int			swapsSeen;					// how often they've changed over, as the server last said ("vs")...
	qboolean	swapsKnown;					// ...as this game has it (from its first frame)
	int			later[2];					// a squeeze of that hand at a holster while they changed over: the holster + 1 (0: none)
	int			drawTime;					// cg.time a quick draw is waiting since (0: none)
	int			frameTime;					// cg.time the hands were last worked out for
	char		sent[32];					// what went out with your info ("vrh")
	int			sentTime;
	vmCvar_t	vrh;						// the userinfo cvar
	qhandle_t	ringShader;					// the markers' (base/shaders/openjkvr.shader)
} vrh;

static int CG_VRH_Want( void );

/*
================================================================================
What's what
================================================================================
*/

static vrhKind_t CG_VRH_Kind( int item ) {
	if ( item == VR_HOLSTER_SABER || item == VR_HOLSTER_SABER + 1 ) {
		return VRH_KIND_SABER;
	}
	switch ( item - VR_HOLSTER_WEAPON ) {
	case WP_STUN_BATON:
	case WP_BRYAR_PISTOL:
	case WP_BRYAR_OLD:
	case WP_DEMP2:
		return VRH_KIND_PISTOL;
	case WP_BLASTER:
	case WP_DISRUPTOR:
	case WP_BOWCASTER:
	case WP_REPEATER:
	case WP_FLECHETTE:
	case WP_ROCKET_LAUNCHER:
	case WP_CONCUSSION:
		return VRH_KIND_RIFLE;
	case WP_THERMAL:
	case WP_TRIP_MINE:
	case WP_DET_PACK:
		return VRH_KIND_BOMB;
	default:
		return VRH_KIND_NONE;
	}
}

// Whether you have that item (a saber: your game knows its model; a gun:
// it's yours, ammo or not).
static qboolean CG_VRH_Have( int item ) {
	const clientInfo_t *ci = &cgs.clientinfo[cg.clientNum];
	const playerState_t *ps = &cg.predictedPlayerState;

	if ( item == VR_HOLSTER_SABER || item == VR_HOLSTER_SABER + 1 ) {
		return (qboolean)( ( ps->stats[STAT_WEAPONS] & ( 1 << WP_SABER ) ) && ci->saber[item - VR_HOLSTER_SABER].model[0] );
	}
	if ( item >= VR_HOLSTER_WEAPON && BG_VRHolsterWeapon( item - VR_HOLSTER_WEAPON ) ) {
		return (qboolean)( ps->stats[STAT_WEAPONS] & ( 1 << ( item - VR_HOLSTER_WEAPON ) ) );
	}
	return qfalse;
}

// What your hand h holds now (VR_HOLSTER_*; empty: nothing) - as you took it
// out or put it away, the game's switch to it or from it still under way.
static int CG_VRH_InHand( int h ) {
	const int w = CG_VRH_Want();
	int s;

	if ( BG_VRHolsterWeapon( w ) ) {
		return ( h == CG_VR_GunHand() ) ? VR_HOLSTER_WEAPON + w : VR_HOLSTER_EMPTY;
	}
	if ( w == WP_SABER ) {
		for ( s = 0; s < MAX_SABERS; s++ ) {
			if ( CG_VR_SaberHand( s ) == h && CG_VRH_Have( VR_HOLSTER_SABER + s ) ) {
				return VR_HOLSTER_SABER + s;
			}
		}
	}
	return VR_HOLSTER_EMPTY;
}

// Whether item is out of its holster (in a hand).
static qboolean CG_VRH_Out( int item ) {
	return (qboolean)( item != VR_HOLSTER_EMPTY && ( CG_VRH_InHand( VR_HAND_LEFT ) == item ||
		CG_VRH_InHand( VR_HAND_RIGHT ) == item ) );
}

// The weapon the game has out while your hands are empty: the saber, kept
// unlit in its holster - or without one, your fists (WP_NONE: neither).
static int CG_VRH_EmptyHanded( void ) {
	const int weapons = cg.predictedPlayerState.stats[STAT_WEAPONS];

	if ( CG_VRH_Have( VR_HOLSTER_SABER ) ) {
		return WP_SABER;
	}
	return ( weapons & ( 1 << WP_MELEE ) ) ? WP_MELEE : WP_NONE;
}

// Every saber in your hands back in its holster.
static void CG_VRH_PutSabers( void ) {
	int s;

	for ( s = 0; s < MAX_SABERS; s++ ) {
		if ( CG_VR_SaberHand( s ) >= 0 ) {
			CG_VR_PutSaber( s );
		}
	}
}

/*
================================================================================
The layout, kept
================================================================================
*/

static void CG_VRH_DefaultLayout( int out[VR_NUM_HOLSTERS] ) {
	out[VR_HOLSTER_HIP_R] = VR_HOLSTER_SABER;
	out[VR_HOLSTER_HIP_L] = VR_HOLSTER_SABER + 1;
	out[VR_HOLSTER_SHOULDER_R] = VR_HOLSTER_EMPTY;
	out[VR_HOLSTER_SHOULDER_L] = VR_HOLSTER_EMPTY;
}

// cg_vrHolsterLayout, as set (the menu's Reset Holsters, the console) - or
// as first read.
static void CG_VRH_ReadLayout( void ) {
	if ( vrh.layoutCount == cg_vrHolsterLayout.modificationCount && vrh.layoutCount ) {
		return;
	}
	vrh.layoutCount = cg_vrHolsterLayout.modificationCount;
	if ( !BG_VRHolsterParse( cg_vrHolsterLayout.string, vrh.layout ) ) {
		CG_VRH_DefaultLayout( vrh.layout );
	}
}

static void CG_VRH_WriteLayout( void ) {
	const char *s = va( "%i,%i,%i,%i", vrh.layout[0], vrh.layout[1], vrh.layout[2], vrh.layout[3] );

	if ( strcmp( s, cg_vrHolsterLayout.string ) ) {
		trap->Cvar_Set( "cg_vrHolsterLayout", s );
		trap->Cvar_Update( &cg_vrHolsterLayout );
	}
	vrh.layoutCount = cg_vrHolsterLayout.modificationCount;
}

// item into holster i, out of any other.
static void CG_VRH_Hang( int i, int item ) {
	int j;

	for ( j = 0; j < VR_NUM_HOLSTERS; j++ ) {
		if ( j != i && vrh.layout[j] == item ) {
			vrh.layout[j] = VR_HOLSTER_EMPTY;
		}
	}
	vrh.layout[i] = item;
	CG_VRH_WriteLayout();
}

/*
================================================================================
Where they hang
================================================================================
*/

// A standalone copy of a model (cached: when there are as many as can be,
// the one least lately drawn - not in the last second - makes way).
static vrhModel_t *CG_VRH_Model( const char *path, int skin, qboolean saber ) {
	vrhModel_t *m;
	int i, oldest = -1;

	if ( !path || !path[0] ) {
		return NULL;
	}
	for ( i = 0; i < vrhNumModels; i++ ) {
		m = &vrhModels[i];
		if ( m->skin == skin && !Q_stricmp( m->path, path ) ) {
			m->used = cg.time;
			return ( m->g2 && trap->G2_HaveWeGhoul2Models( m->g2 ) ) ? m : NULL;
		}
		if ( ( cg.time - m->used > VRH_MODEL_KEEP || cg.time - m->used < 0 ) && ( oldest < 0 || m->used - vrhModels[oldest].used < 0 ) ) {
			oldest = i;
		}
	}
	if ( vrhNumModels < VRH_MAX_MODELS ) {
		m = &vrhModels[vrhNumModels++];
	} else if ( oldest >= 0 ) {
		m = &vrhModels[oldest];
		if ( m->g2 ) {
			trap->G2API_CleanGhoul2Models( &m->g2 );
		}
	} else {
		return NULL;	// (every one drawn just now)
	}
	memset( m, 0, sizeof( *m ) );
	m->used = cg.time;
	Q_strncpyz( m->path, path, sizeof( m->path ) );
	m->skin = skin;
	m->blade[0] = m->blade[1] = m->flash = -1;
	trap->G2API_InitGhoul2Model( &m->g2, path, 0, skin, 0, 0, 0 );
	if ( !m->g2 ) {
		return NULL;
	}
	if ( skin ) {
		trap->G2API_SetSkin( m->g2, 0, skin, skin );
	}
	if ( saber ) {
		m->blade[0] = trap->G2API_AddBolt( m->g2, 0, "*blade1" );
		if ( m->blade[0] < 0 ) {
			m->blade[0] = trap->G2API_AddBolt( m->g2, 0, "*flash" );	// (an old saber)
		}
		m->blade[1] = trap->G2API_AddBolt( m->g2, 0, "*blade2" );
	} else {
		m->flash = trap->G2API_AddBolt( m->g2, 0, "*flash" );
	}
	return trap->G2_HaveWeGhoul2Models( m->g2 ) ? m : NULL;
}

void CG_VRH_Shutdown( void ) {
	int i;

	for ( i = 0; i < vrhNumModels; i++ ) {
		if ( vrhModels[i].g2 ) {
			trap->G2API_CleanGhoul2Models( &vrhModels[i].g2 );
		}
	}
	memset( vrhModels, 0, sizeof( vrhModels ) );
	vrhNumModels = 0;
	memset( vrhTags, 0, sizeof( vrhTags ) );
}

// The model item hangs as, for player n.
static vrhModel_t *CG_VRH_ItemModel( int n, int item ) {
	const clientInfo_t *ci = &cgs.clientinfo[n];

	if ( item == VR_HOLSTER_SABER || item == VR_HOLSTER_SABER + 1 ) {
		const saberInfo_t *sa = &ci->saber[item - VR_HOLSTER_SABER];
		return sa->model[0] ? CG_VRH_Model( sa->model, sa->skin, qtrue ) : NULL;
	}
	if ( item >= VR_HOLSTER_WEAPON && BG_VRHolsterWeapon( item - VR_HOLSTER_WEAPON ) ) {
		const gitem_t *it = BG_FindItemForWeapon( (weapon_t)( item - VR_HOLSTER_WEAPON ) );
		CG_RegisterWeapon( item - VR_HOLSTER_WEAPON );
		return ( it && it->world_model[0] ) ? CG_VRH_Model( it->world_model[0], 0, qfalse ) : NULL;
	}
	return NULL;
}

// A bolt's place and axes on a model drawn on its own at rest (its own frame).
static qboolean CG_VRH_BoltRest( void *g2, int bolt, vec3_t origin, vec3_t axis[3] ) {
	vec3_t zero = { 0.0f, 0.0f, 0.0f };
	mdxaBone_t m;

	if ( bolt < 0 || !trap->G2API_GetBoltMatrix( g2, 0, bolt, &m, zero, zero, cg.time, cgs.gameModels, zero ) ) {
		return qfalse;
	}
	BG_GiveMeVectorFromMatrix( &m, ORIGIN, origin );
	if ( axis ) {
		BG_GiveMeVectorFromMatrix( &m, POSITIVE_X, axis[0] );
		BG_GiveMeVectorFromMatrix( &m, POSITIVE_Y, axis[1] );
		BG_GiveMeVectorFromMatrix( &m, POSITIVE_Z, axis[2] );
	}
	return qtrue;
}

// A player's tag (or bone) in the world: where it is.
static qboolean CG_VRH_Point( centity_t *cent, int bolt, const vec3_t angles, vec3_t out ) {
	mdxaBone_t m;

	if ( bolt < 0 || !trap->G2API_GetBoltMatrix( cent->ghoul2, 0, bolt, &m, angles, cent->lerpOrigin, cg.time, cgs.gameModels,
			cent->modelScale ) ) {
		return qfalse;
	}
	BG_GiveMeVectorFromMatrix( &m, ORIGIN, out );
	return qtrue;
}

// A player's tag (or bone) k (VRH_TAG_*): its bolt on their model, asked for
// afresh each frame - G2API_AddBolt gives the one there is, so a model made
// anew (even where the last one was) never leaves a stale one behind (-1: not
// on it).
static int CG_VRH_Bolt( centity_t *cent, int k ) {
	vrhTags_t *t = &vrhTags[cent->currentState.number];

	if ( t->g2 != cent->ghoul2 || t->time != cg.time ) {
		t->g2 = cent->ghoul2;
		t->time = cg.time;
		t->asked = 0;
	}
	if ( !( t->asked & ( 1 << k ) ) ) {
		t->bolt[k] = trap->G2API_AddBolt( cent->ghoul2, 0, vrhTagNames[k] );
		t->asked |= 1 << k;
	}
	return t->bolt[k];
}

/*
Where holster i's item hangs on a player drawn at angles with the body's
axes (forward, left, up: as the model's legs are drawn), from the tags on
their model. The hips': out from the tag, below the belt - a hilt hangs
there, emitter up (cg_vrHiltsDown 1: down) and a little forward, its lower
end out from the leg; a gun along the thigh, muzzle down; something to throw
at the belt as it is. The shoulders': on the back behind the shoulder blade,
clear of the back (the *back tag is on it), the torso's own way round
(turned from the legs in a stance) - a hilt up past the shoulder, its top
toward it; a gun across the back from it, muzzle down toward the other hip;
reached over the shoulder, toward the top (out->grab). On every model in the
real skeleton's poses (stage 3: hang.py) they hang clear of the body, bar a
cape or a jetpack.
*/
#define VRH_HIP_OUT			3.5f	// a hip's item hangs this far out from its tag...
#define VRH_HIP_DOWN		3.0f	// ...and this far below it (a hilt's middle)
#define VRH_BACK_GAP		2.5f	// a shoulder's this far off the back...
#define VRH_BACK_UP			3.0f	// ...its middle this far above the shoulder
static qboolean CG_VRH_HangPose( centity_t *cent, const vec3_t angles, vec3_t body[3], int i, int item, int n, vrhHang_t *out ) {
	const vrhKind_t kind = CG_VRH_Kind( item );
	const qboolean shoulder = (qboolean)( i == VR_HOLSTER_SHOULDER_R || i == VR_HOLSTER_SHOULDER_L );
	const float side = ( i == VR_HOLSTER_HIP_R || i == VR_HOLSTER_SHOULDER_R ) ? -1.0f : 1.0f;	// (along the body's left)
	vrhModel_t *m;
	vec3_t tag, at, dir, face, ownDir, ownFace, ownOrigin, own[3], a3, b3, L, B, U, F;
	qboolean onTag;
	int k;

	memset( out, 0, sizeof( *out ) );
	if ( cent->currentState.number < 0 || cent->currentState.number >= MAX_CLIENTS || !cent->ghoul2 ) {
		return qfalse;
	}
	// where: the model's tag, or near the bone it's on
	onTag = CG_VRH_Point( cent, CG_VRH_Bolt( cent, shoulder ? ( side < 0.0f ? VRH_TAG_SHLDR_R : VRH_TAG_SHLDR_L ) :
		( side < 0.0f ? VRH_TAG_HIP_R : VRH_TAG_HIP_L ) ), angles, tag );
	if ( !onTag ) {
		if ( !CG_VRH_Point( cent, CG_VRH_Bolt( cent, shoulder ? VRH_BONE_THORACIC : VRH_BONE_PELVIS ), angles, tag ) ) {
			return qfalse;
		}
		VectorMA( tag, side * ( shoulder ? 5.0f : 6.0f ), body[1], tag );
	}
	VectorCopy( body[1], L );
	VectorScale( body[0], -1.0f, B );
	VectorCopy( body[2], U );
	if ( shoulder ) {
		vec3_t sr, sl, back, mid;
		float depth = onTag ? 6.5f : 4.5f;	// (no *back tag: about this far behind the shoulder's tag, or the thoracic bone)
		if ( CG_VRH_Point( cent, CG_VRH_Bolt( cent, VRH_TAG_SHLDR_R ), angles, sr ) &&
			CG_VRH_Point( cent, CG_VRH_Bolt( cent, VRH_TAG_SHLDR_L ), angles, sl ) &&
			CG_VRH_Point( cent, CG_VRH_Bolt( cent, VRH_TAG_BACK ), angles, back ) ) {
			// the torso's own way round: across the shoulders; back to the *back tag, level
			vec3_t l, bk;
			VectorSubtract( sl, sr, l );
			VectorAdd( sl, sr, mid );
			VectorScale( mid, 0.5f, mid );
			VectorSubtract( back, mid, bk );
			if ( VectorNormalize( l ) > 0.1f ) {
				VectorMA( bk, -DotProduct( bk, l ), l, bk );
				VectorMA( bk, -DotProduct( bk, body[2] ), body[2], bk );
				if ( VectorNormalize( bk ) > 0.1f ) {
					VectorCopy( l, L );
					VectorCopy( bk, B );
					CrossProduct( L, B, U );
					VectorNormalize( U );
					VectorSubtract( back, tag, mid );
					depth = DotProduct( mid, B );
				}
			}
		}
		VectorMA( tag, depth + VRH_BACK_GAP, B, at );
		VectorMA( at, VRH_BACK_UP, U, at );
		VectorMA( at, -side, L, at );	// (in toward the spine)
		VectorMA( at, 1.0f, U, out->grab );
		VectorMA( out->grab, -2.0f, B, out->grab );	// (up and toward the shoulder: reached over it)
	} else {
		VectorMA( tag, side * VRH_HIP_OUT, L, at );
		VectorMA( at, -VRH_HIP_DOWN, U, at );
		VectorCopy( at, out->grab );
	}
	VectorScale( B, -1.0f, F );
	VectorCopy( at, out->origin );
	VectorCopy( tag, out->anchor );
	VectorCopy( U, out->up );
	VectorCopy( F, out->fwd );
	if ( item == VR_HOLSTER_EMPTY ) {
		out->valid = qtrue;	// (where something goes in)
		return qtrue;
	}
	if ( kind == VRH_KIND_NONE || !( m = CG_VRH_ItemModel( n, item ) ) ) {
		return qfalse;
	}
	// which way: the item's own long way along dir, its face toward face
	switch ( kind ) {
	case VRH_KIND_SABER:
		// (its line's top: toward the shoulder; at a hip, forward and the lower end out from the leg - the emitter up it, or down)
		VectorCopy( U, dir );
		if ( shoulder ) {
			VectorMA( dir, side * 0.25f, L, dir );
			VectorMA( dir, 0.3f, F, dir );
		} else {
			VectorMA( dir, 0.27f, F, dir );
			VectorMA( dir, -side * 0.1f, L, dir );
		}
		if ( cg_vrHiltsDown.integer ) {
			VectorScale( dir, -1.0f, dir );
		}
		VectorScale( L, side, face );
		break;
	case VRH_KIND_PISTOL:
	case VRH_KIND_RIFLE:
		// (its side on you, its top up toward your neck on your back, forward
		// along the thigh - the grip back, not into you)
		if ( shoulder ) {
			VectorScale( U, -0.75f, dir );
			VectorMA( dir, -side * 0.65f, L, dir );
			VectorMA( dir, 0.3f, B, dir );
			VectorCopy( U, face );
		} else {
			VectorScale( U, -1.0f, dir );
			VectorMA( dir, 0.18f, F, dir );
			VectorCopy( F, face );
		}
		break;
	default:
		VectorCopy( F, dir );
		VectorCopy( U, face );
		break;
	}
	VectorNormalize( dir );
	VectorMA( face, -DotProduct( face, dir ), dir, face );
	if ( VectorNormalize( face ) < 0.1f ) {
		PerpendicularVector( face, dir );
	}
	// the item's own long way and face, at rest: a hilt's blade, a gun's muzzle
	if ( kind == VRH_KIND_SABER ) {
		if ( !CG_VRH_BoltRest( m->g2, m->blade[0], ownOrigin, own ) ) {
			return qfalse;
		}
		VectorScale( own[1], -1.0f, ownDir );	// (a blade runs along its bolt's -Y)
		VectorCopy( own[0], ownFace );
	} else if ( kind != VRH_KIND_BOMB && CG_VRH_BoltRest( m->g2, m->flash, ownOrigin, own ) ) {
		VectorCopy( own[0], ownDir );	// (the muzzle's +X)
		VectorSet( ownFace, 0.0f, 0.0f, 1.0f );
	} else {
		VectorSet( ownDir, 1.0f, 0.0f, 0.0f );
		VectorSet( ownFace, 0.0f, 0.0f, 1.0f );
	}
	if ( VectorNormalize( ownDir ) < 0.5f ) {
		return qfalse;
	}
	VectorMA( ownFace, -DotProduct( ownFace, ownDir ), ownDir, ownFace );
	if ( VectorNormalize( ownFace ) < 0.1f ) {
		PerpendicularVector( ownFace, ownDir );
	}
	// the turn taking (ownDir, ownFace) onto (dir, face) - the model drawn
	// with these axes (its own frame as its bolts at rest are given in:
	// CG_VR_HeldWeaponPose does the same), its origin (a hilt's: where a
	// hand's tag holds it; a gun's: its grip) at the place
	CrossProduct( ownDir, ownFace, a3 );
	CrossProduct( dir, face, b3 );
	for ( k = 0; k < 3; k++ ) {
		VectorScale( dir, ownDir[k], out->axis[k] );
		VectorMA( out->axis[k], ownFace[k], face, out->axis[k] );
		VectorMA( out->axis[k], a3[k], b3, out->axis[k] );
	}
	if ( kind == VRH_KIND_SABER ) {
		vec3_t e2, ax2[3];
		float emitter;
		VectorCopy( dir, out->blade );
		// along it from its origin: to its emitter, and a staff's other one
		emitter = DotProduct( ownOrigin, ownDir );
		out->hi = Q_max( 0.0f, emitter - 1.5f );
		out->lo = -6.5f;
		if ( CG_VRH_BoltRest( m->g2, m->blade[1], e2, ax2 ) ) {
			const float e = DotProduct( e2, ownDir );
			if ( e < 0.0f ) {
				out->lo = e + 1.5f;
			}
		}
		out->lo = Q_min( 0.0f, out->lo );
		// the middle of the hilt at the place
		VectorMA( at, -0.5f * ( out->lo + out->hi ), dir, out->origin );
	}
	out->valid = qtrue;
	return qtrue;
}

/*
================================================================================
Drawn on everyone
================================================================================
*/

// What hangs in player n's holster i now (VR_HOLSTER_*; empty: nothing): yours
// as you have it; another VR player's as their info says (not what's in their
// hands); anyone else's hilts on their belt while the saber isn't in hand -
// the first on the right hip, a second on the left ("hs").
static int CG_VRH_Hanging( centity_t *cent, int i ) {
	const int n = cent->currentState.number;
	const clientInfo_t *ci = &cgs.clientinfo[n];
	int item, s;

	if ( n == cg.clientNum && vrh.on ) {
		item = vrh.layout[i];
		return ( CG_VRH_Have( item ) && !CG_VRH_Out( item ) ) ? item : VR_HOLSTER_EMPTY;
	}
	if ( ci->vrHasHolsters ) {
		item = ci->vrHolsters[i];
		if ( item >= VR_HOLSTER_WEAPON ) {
			return ( cent->currentState.weapon == item - VR_HOLSTER_WEAPON ) ? VR_HOLSTER_EMPTY : item;
		}
		if ( item == VR_HOLSTER_SABER || item == VR_HOLSTER_SABER + 1 ) {
			s = item - VR_HOLSTER_SABER;
			if ( !ci->saber[s].model[0] || ci->vrHasSabers <= s ) {
				return VR_HOLSTER_EMPTY;	// (a saber not had: none - a second, with one)
			}
			if ( cent->currentState.weapon != WP_SABER ) {
				return item;	// (another weapon out: every hilt hangs)
			}
			if ( ( s == 0 && cent->currentState.saberInFlight ) || !CG_VR_BodyActive( cent ) ) {
				return VR_HOLSTER_EMPTY;	// (thrown; or drawn by the game, in its hands)
			}
			return CG_VR_OtherHoldsSaber( n, s ) ? VR_HOLSTER_EMPTY : item;
		}
		return VR_HOLSTER_EMPTY;
	}
	// everyone else: their hilts, on their belt
	if ( ci->vrHasSabers <= 0 || cent->currentState.weapon == WP_SABER ) {
		return VR_HOLSTER_EMPTY;
	}
	if ( i == VR_HOLSTER_HIP_R && ci->saber[0].model[0] ) {
		return VR_HOLSTER_SABER;
	}
	if ( i == VR_HOLSTER_HIP_L && ci->vrHasSabers >= 2 && ci->saber[1].model[0] ) {
		return VR_HOLSTER_SABER + 1;
	}
	return VR_HOLSTER_EMPTY;
}

// (cg_players.c, CG_Player: the player drawn - the body's axes as drawn, how
// it's drawn) What hangs on them: a model drawn at each holster that holds
// something. Yours, where each holster is (for your hands to find).
void CG_VRH_AddHolstered( centity_t *cent, vec3_t body[3], int renderfx ) {
	const int n = cent->currentState.number;
	const qboolean mine = (qboolean)( n == cg.clientNum );
	int i;

	if ( n < 0 || n >= MAX_CLIENTS || !cent->ghoul2 || cent->localAnimIndex > 1 ||
		( cent->currentState.eFlags & ( EF_DEAD | EF_NODRAW ) ) || !cg_vrHolsterDraw.integer ) {
		return;
	}
	if ( !mine && ( cent->currentState.powerups & ( 1 << PW_CLOAKED ) ) ) {
		return;	// (cloaked: nothing gives them away - yours, you see where)
	}
	for ( i = 0; i < VR_NUM_HOLSTERS; i++ ) {
		const int item = CG_VRH_Hanging( cent, i );
		vrhHang_t h;
		refEntity_t ent;
		vrhModel_t *m;

		if ( mine ) {
			// (an empty holster too: where things go in)
			if ( !CG_VRH_HangPose( cent, cent->turAngles, body, i, item, n, &vrh.hang[i] ) ) {
				CG_VRH_HangPose( cent, cent->turAngles, body, i, VR_HOLSTER_EMPTY, n, &vrh.hang[i] );
			}
			vrh.hangTime = cg.time;
		}
		if ( item == VR_HOLSTER_EMPTY || !( m = CG_VRH_ItemModel( n, item ) ) ) {
			continue;
		}
		if ( mine ) {
			h = vrh.hang[i];
		} else if ( !CG_VRH_HangPose( cent, cent->turAngles, body, i, item, n, &h ) ) {
			continue;
		}
		if ( !h.valid ) {
			continue;
		}
		memset( &ent, 0, sizeof( ent ) );
		ent.reType = RT_MODEL;
		ent.ghoul2 = m->g2;
		ent.radius = 32.0f;
		VectorCopy( h.origin, ent.origin );
		VectorCopy( h.origin, ent.oldorigin );
		VectorCopy( cent->lerpOrigin, ent.lightingOrigin );
		AxisCopy( h.axis, ent.axis );
		ent.renderfx = renderfx;	// (as the body's: first person, mirrors, shadows)
		ent.modelScale[0] = ent.modelScale[1] = ent.modelScale[2] = 1.0f;
		trap->R_AddRefEntityToScene( &ent );
	}
}

// A faint ring round point p, square to the axis n, of radius r.
static void CG_VRH_Ring( const vec3_t p, const vec3_t n, float r, float alpha ) {
	polyVert_t v[4];
	vec3_t a, b, u0, u1;
	int i, k;
	const int segs = 20;
	const float w = 0.12f;

	PerpendicularVector( a, n );
	CrossProduct( n, a, b );
	for ( i = 0; i < segs; i++ ) {
		const float t0 = 2.0f * M_PI * i / segs, t1 = 2.0f * M_PI * ( i + 1 ) / segs;
		VectorScale( a, cosf( t0 ), u0 );
		VectorMA( u0, sinf( t0 ), b, u0 );
		VectorScale( a, cosf( t1 ), u1 );
		VectorMA( u1, sinf( t1 ), b, u1 );
		VectorMA( p, r * ( 1.0f - w ), u0, v[0].xyz );
		VectorMA( p, r * ( 1.0f + w ), u0, v[1].xyz );
		VectorMA( p, r * ( 1.0f + w ), u1, v[2].xyz );
		VectorMA( p, r * ( 1.0f - w ), u1, v[3].xyz );
		for ( k = 0; k < 4; k++ ) {
			v[k].st[0] = v[k].st[1] = 0.5f;
			v[k].modulate[0] = v[k].modulate[1] = v[k].modulate[2] = 255;
			v[k].modulate[3] = (byte)Com_Clamp( 0.0f, 255.0f, alpha * 255.0f );
		}
		trap->R_AddPolysToScene( vrh.ringShader, 4, v, 1 );
	}
}

// Your holsters' markers (cg_vrHolsterMarkers: 0 none, 1 where a hand is
// near, 2 always): a faint ring round each - brighter where a squeeze would
// do something now.
static void CG_VRH_AddMarkers( const vrClientState_t *vr ) {
	int i, h;

	if ( !cg_vrHolsterMarkers.integer || vrh.hangTime != cg.time || !vrh.ringShader ) {
		return;	// (no ring's shader - a pure server doesn't read the game folder's loose files - no rings)
	}
	for ( i = 0; i < VR_NUM_HOLSTERS; i++ ) {
		const vrhHang_t *g = &vrh.hang[i];
		float near = 0.0f, reach;
		qboolean live = qfalse;
		if ( !g->valid ) {
			continue;
		}
		reach = vr->worldScale * ( ( i == VR_HOLSTER_SHOULDER_R || i == VR_HOLSTER_SHOULDER_L ) ? VRH_REACH_SHOULDER :
			VRH_REACH_HIP );
		for ( h = 0; h < 2; h++ ) {
			vec3_t pos;
			if ( CG_VR_HandGrip( h, pos, NULL ) ) {
				near = Q_max( near, 1.0f - Distance( pos, g->grab ) / ( 2.5f * reach ) );
			}
			live |= (qboolean)( vrh.at[h] == i );
		}
		if ( cg_vrHolsterMarkers.integer == 1 && near <= 0.0f && !live ) {
			continue;
		}
		CG_VRH_Ring( g->grab, g->up, 0.4f * reach, live ? 0.5f : 0.08f + 0.17f * Q_max( 0.0f, near ) );
	}
}

/*
================================================================================
Your hands at your holsters
================================================================================
*/

// Whether holsters apply to you: cg_vrHolsters on, on a server of this build
// that has them, this build's VR engine, your VR body and its sabers drawn in
// first person (cg_vrBody, cg_vrSaber, not cg_thirdPerson: what hangs is
// placed on that body, and the sabers are in its hands), not a demo - in VR
// this moment or not (a menu up: what's in your hands stays so).
static qboolean CG_VRH_Apply( void ) {
	return (qboolean)( cg_vrHolsters.integer && cgs.vrHolsters && CG_VR_Engine() && trap->ext.VR_SetCmdButtons &&
		trap->ext.VR_GenericCmd && cg_vrBody.integer && cg_vrSaber.integer && !cg_thirdPerson.integer && !cg.demoPlayback );
}

// Whether you're playing: in the game (not loading), not watching someone, not
// a spectator, not the intermission.
static qboolean CG_VRH_Playing( void ) {
	const playerState_t *ps = &cg.predictedPlayerState;

	return (qboolean)( cg.snap && !( cg.snap->ps.pm_flags & PMF_FOLLOW ) && ps->persistant[PERS_TEAM] != TEAM_SPECTATOR &&
		ps->pm_type != PM_SPECTATOR && ps->pm_type != PM_INTERMISSION );
}

// Whether holsters are in play for you: they apply, in VR, playing.
qboolean CG_VRH_On( void ) {
	return (qboolean)( CG_VRH_Apply() && CG_VR() && CG_VRH_Playing() );
}

// Whether your sabers can change over (GENCMD_VR_SABERSWAP): not a siege
// class's (the class says which is which).
static qboolean CG_VRH_CanSwap( void ) {
	return (qboolean)!( cgs.gametype == GT_SIEGE && cgs.clientinfo[cg.clientNum].siegeIndex != -1 );
}

// Whether the weapon you took out or put away (vrh.want) is still asked for
// (cg.weaponSelect: not changed since - the stick, a pickup), within ms of it.
static qboolean CG_VRH_Wanting( int ms ) {
	return (qboolean)( vrh.wantTime && cg.time - vrh.wantTime >= 0 && cg.time - vrh.wantTime < ms &&
		cg.weaponSelect == vrh.want && ( cg.predictedPlayerState.stats[STAT_WEAPONS] & ( 1 << vrh.want ) ) );
}

// The weapon you want in hand: what you last took out or put away is asked
// for at once (cg.weaponSelect), the game's switch following a moment later -
// till then, that; else the game's.
static int CG_VRH_Want( void ) {
	return CG_VRH_Wanting( VRH_WANT_MS ) ? vrh.want : cg.predictedPlayerState.weapon;
}

// Asks the game for weapon (what you took out, or your hands empty) - without
// the weapon bar coming up: you see it in your hand.
static void CG_VRH_Select( int weapon ) {
	vrh.want = weapon;
	vrh.wantTime = cg.time ? cg.time : 1;
	cg.weaponSelect = weapon;
}

// What hangs in holster i now (empty: nothing - or what's there is out).
static int CG_VRH_In( int i ) {
	const int item = vrh.layout[i];

	return ( CG_VRH_Have( item ) && !CG_VRH_Out( item ) ) ? item : VR_HOLSTER_EMPTY;
}

// Whether saber s's blades are lit (as your game has it now).
static qboolean CG_VRH_Lit( int s ) {
	const playerState_t *ps = &cg.predictedPlayerState;

	if ( ps->weapon != WP_SABER ) {
		return qfalse;
	}
	return ( s == 0 ) ? (qboolean)( ps->saberHolstered < 2 ) : (qboolean)( ps->saberHolstered == 0 );
}

// Whether a saber in your hands is lit.
static qboolean CG_VRH_LitInHand( void ) {
	return (qboolean)( ( CG_VR_SaberHand( 0 ) >= 0 && CG_VRH_Lit( 0 ) ) || ( CG_VR_SaberHand( 1 ) >= 0 && CG_VRH_Lit( 1 ) ) );
}

// Whether a squeeze of hand h (holding holding) at holster i does something:
// takes what's there (a gun only into your gun hand), puts what it holds in
// (a saber only with its blade out), or both (swaps).
static qboolean CG_VRH_Can( int h, int holding, int i ) {
	const int item = CG_VRH_In( i );
	const int gunHand = CG_VR_GunHand();

	if ( item >= VR_HOLSTER_WEAPON && CG_VRH_LitInHand() ) {
		return qfalse;	// (a gun out puts the sabers in your hands away: never lit)
	}
	if ( !CG_VRH_CanSwap() && ( ( item == VR_HOLSTER_SABER + 1 && ( holding == VR_HOLSTER_SABER || CG_VR_SaberHand( 0 ) < 0 ) ) ||
		( holding == VR_HOLSTER_SABER && item == VR_HOLSTER_EMPTY && CG_VR_SaberHand( 1 ) >= 0 ) ) ) {
		return qfalse;	// (a siege class's sabers don't change over: the second only with the first in hand, the first back after it)
	}
	if ( holding == VR_HOLSTER_EMPTY ) {
		return (qboolean)( item != VR_HOLSTER_EMPTY && ( item < VR_HOLSTER_WEAPON || h == gunHand ) );
	}
	if ( holding < VR_HOLSTER_WEAPON && CG_VRH_Lit( holding - VR_HOLSTER_SABER ) ) {
		return qfalse;	// (never put away lit)
	}
	if ( holding == VR_HOLSTER_SABER && cg.predictedPlayerState.saberInFlight ) {
		return qfalse;	// (thrown: not in your hand to put away)
	}
	if ( holding >= VR_HOLSTER_WEAPON && item == VR_HOLSTER_EMPTY && CG_VRH_EmptyHanded() == WP_NONE ) {
		return qfalse;	// (no saber, no fists: something stays in hand)
	}
	return (qboolean)( item == VR_HOLSTER_EMPTY || item < VR_HOLSTER_WEAPON || h == gunHand );
}

// Whether your hand (its controller's grip at pos) reaches holster i, and how
// near it is: what hangs at a hip within 15 cm; over a shoulder, within 22
// cm, the hand not in front of the shoulder nor below it.
static qboolean CG_VRH_Reaches( const vrClientState_t *vr, int i, const vec3_t pos, float *dist ) {
	const vrhHang_t *g = &vrh.hang[i];
	const qboolean shoulder = (qboolean)( i == VR_HOLSTER_SHOULDER_R || i == VR_HOLSTER_SHOULDER_L );
	vec3_t rel;

	if ( !g->valid ) {
		return qfalse;
	}
	VectorSubtract( pos, g->grab, rel );
	*dist = VectorLength( rel );
	if ( *dist > vr->worldScale * ( shoulder ? VRH_REACH_SHOULDER : VRH_REACH_HIP ) ) {
		return qfalse;
	}
	if ( shoulder ) {
		VectorSubtract( pos, g->anchor, rel );
		if ( DotProduct( rel, g->fwd ) > 1.0f || DotProduct( rel, g->up ) < -2.0f ) {
			return qfalse;
		}
	}
	return qtrue;
}

// Where each hand is at a holster a squeeze would do something at - the
// grabs the engine is to take its grip as there (a hand your other hand's
// hilt has first: cg_vrbody.c). A tick in the hand as it gets there (firmer
// for something to take).
static void CG_VRH_FindZones( const vrClientState_t *vr ) {
	const int claimed = CG_VR_TwoHandZones();
	int h, i;

	vrh.zones = 0;
	for ( h = 0; h < 2; h++ ) {
		const int was = vrh.at[h], holding = CG_VRH_InHand( h );
		float best = 1e9f, d;
		vec3_t pos;

		vrh.at[h] = -1;
		if ( ( claimed & ( 1 << h ) ) || cg.time - vrh.hangTime < 0 || cg.time - vrh.hangTime > 100 ||
			!CG_VR_HandGrip( h, pos, NULL ) ) {
			continue;
		}
		for ( i = 0; i < VR_NUM_HOLSTERS; i++ ) {
			if ( CG_VRH_Reaches( vr, i, pos, &d ) && d < best && CG_VRH_Can( h, holding, i ) ) {
				best = d;
				vrh.at[h] = i;
			}
		}
		if ( vrh.at[h] < 0 ) {
			continue;
		}
		vrh.zones |= 1 << h;
		if ( vrh.at[h] != was ) {
			CG_VR_Feel( h, ( CG_VRH_In( vrh.at[h] ) != VR_HOLSTER_EMPTY ) ? 0.35f : 0.18f, 18, 0.0f, 0 );
		}
	}
}

// Hand h takes saber s out of holster i: where its fist closed on the hilt,
// the way round it took it (the blade out of the thumb's side, or the little
// finger's: reversed) - the game's first saber, if it's the only one in hand.
static void CG_VRH_TakeSaber( const vrClientState_t *vr, int h, int s, int i ) {
	const vrhHang_t *g = &vrh.hang[i];
	vec3_t pos, ax[3], rel;
	float t = 0.0f, sign = 1.0f;

	if ( CG_VR_HandGrip( h, pos, ax ) && g->valid ) {
		VectorSubtract( pos, g->origin, rel );
		t = Com_Clamp( g->lo, g->hi, DotProduct( rel, g->blade ) );
		sign = ( DotProduct( ax[0], g->blade ) < 0.0f ) ? -1.0f : 1.0f;	// (ax[0]: through the fist toward the thumb)
	}
	CG_VR_TakeSaber( s, h, t, sign );
	if ( s == 1 && CG_VR_SaberHand( 0 ) < 0 && CG_VRH_Have( VR_HOLSTER_SABER ) ) {
		// the second saber alone in hand: the server's to make it the first
		trap->ext.VR_GenericCmd( GENCMD_VR_SABERSWAP );
		vrh.swapTime = cg.time ? cg.time : 1;
		CG_VR_Log( "holsters: your second saber taken out first - asking the server to make it the first\n" );
	}
	if ( vr->trigger[h] && cg_vrQuickDraw.integer ) {
		vrh.drawTime = cg.time ? cg.time : 1;	// (the quick draw: lit as it comes out)
	}
}

// A squeeze of hand h at holster i: what it holds goes in, what's there comes out.
static void CG_VRH_Squeeze( const vrClientState_t *vr, int h, int i ) {
	const int holding = CG_VRH_InHand( h ), item = CG_VRH_In( i );

	if ( !CG_VRH_Can( h, holding, i ) ) {
		return;
	}
	CG_VR_BlockGrab( h );	// (the holster's squeeze: not a hold on your other hand's hilt too)
	if ( holding != VR_HOLSTER_EMPTY ) {
		CG_VRH_Hang( i, holding );
		if ( holding < VR_HOLSTER_WEAPON ) {
			CG_VR_PutSaber( holding - VR_HOLSTER_SABER );
		} else if ( item == VR_HOLSTER_EMPTY ) {
			CG_VRH_Select( CG_VRH_EmptyHanded() );	// (your hands empty)
		}
		CG_VR_Log( "holsters: put %i in holster %i\n", holding, i );
	}
	if ( item >= VR_HOLSTER_WEAPON ) {
		// a gun: into your gun hand, one weapon at a time - a saber in hand back in its holster
		CG_VRH_PutSabers();
		CG_VRH_Select( item - VR_HOLSTER_WEAPON );
	} else if ( item != VR_HOLSTER_EMPTY ) {
		CG_VRH_TakeSaber( vr, h, item - VR_HOLSTER_SABER, i );
		CG_VRH_Select( WP_SABER );	// (a gun in hand back in its holster)
	}
	if ( item != VR_HOLSTER_EMPTY ) {
		CG_VR_Log( "holsters: took %i out of holster %i into the %s hand\n", item, i, h == VR_HAND_LEFT ? "left" : "right" );
	}
	// one of two sabers left in hand, the second: the server's to make it the first
	if ( holding != VR_HOLSTER_EMPTY && holding < VR_HOLSTER_WEAPON && !vrh.swapTime && CG_VR_SaberHand( 0 ) < 0 &&
		CG_VR_SaberHand( 1 ) >= 0 ) {
		trap->ext.VR_GenericCmd( GENCMD_VR_SABERSWAP );
		vrh.swapTime = cg.time ? cg.time : 1;
	}
	CG_VR_Feel( h, 0.6f, 30, 0.2f, 60 );	// (a bump: out, or in)
}

/*
================================================================================
Every frame
================================================================================
*/

static void CG_VRH_SendLayout( qboolean now );

// Your sabers changed over: the server says so ("vs" in your info: each time
// they have, an odd number of times since it last said) - your saber1/saber2
// and color1/color2 change over too (so your next spawn keeps them so), and
// so do the holsters they hang in (what's in them said at once: the server's
// changed its copy over) and the hands holding them. Asked for and not done
// in a while: the second back in its holster.
static void CG_VRH_Swapped( void ) {
	const clientInfo_t *ci = &cgs.clientinfo[cg.clientNum];
	char a[MAX_QPATH], b[MAX_QPATH];
	int i;

	if ( !vrh.swapsKnown ) {
		vrh.swapsKnown = qtrue;	// (as they are when this game starts)
		vrh.swapsSeen = ci->vrSaberSwaps;
	}
	if ( ci->vrSaberSwaps == vrh.swapsSeen ) {
		if ( vrh.swapTime && ( cg.time - vrh.swapTime > VRH_SWAP_MS || cg.time - vrh.swapTime < 0 ) ) {
			vrh.swapTime = 0;
			if ( CG_VR_SaberHand( 0 ) < 0 && CG_VR_SaberHand( 1 ) >= 0 ) {
				CG_VR_PutSaber( 1 );
			}
			CG_VR_Log( "holsters: the server didn't change your sabers over - the second back in its holster\n" );
		}
		return;
	}
	i = ( ci->vrSaberSwaps - vrh.swapsSeen ) & 1;
	vrh.swapsSeen = ci->vrSaberSwaps;
	vrh.swapTime = 0;
	if ( !i ) {
		return;	// (changed over and back)
	}
	trap->Cvar_VariableStringBuffer( "saber1", a, sizeof( a ) );
	trap->Cvar_VariableStringBuffer( "saber2", b, sizeof( b ) );
	trap->Cvar_Set( "saber1", b );
	trap->Cvar_Set( "saber2", a );
	trap->Cvar_VariableStringBuffer( "color1", a, sizeof( a ) );
	trap->Cvar_VariableStringBuffer( "color2", b, sizeof( b ) );
	trap->Cvar_Set( "color1", b );
	trap->Cvar_Set( "color2", a );
	for ( i = 0; i < VR_NUM_HOLSTERS; i++ ) {
		if ( vrh.layout[i] == VR_HOLSTER_SABER ) {
			vrh.layout[i] = VR_HOLSTER_SABER + 1;
		} else if ( vrh.layout[i] == VR_HOLSTER_SABER + 1 ) {
			vrh.layout[i] = VR_HOLSTER_SABER;
		}
	}
	CG_VRH_WriteLayout();
	CG_VRH_SendLayout( qtrue );
	CG_VR_SwapSaberHolds();
	CG_VR_Log( "holsters: the server changed your sabers over - the one in hand is the first now\n" );
}

// Your hands as the game's weapon goes: a gun or fists in hand (or on their
// way), your sabers in their holsters; the saber switched to by the stick
// (not taken out of a holster), in your hands as the game has them - the
// first in your gun hand, a second in the other. Dead: everything back where
// it hangs.
static void CG_VRH_FollowWeapon( qboolean alive ) {
	const int w = cg.predictedPlayerState.weapon, was = vrh.weapon;

	if ( !alive ) {
		CG_VRH_PutSabers();
		vrh.weapon = WP_NONE;
		vrh.drawTime = 0;
		vrh.wantTime = 0;
		return;
	}
	if ( CG_VRH_Want() != WP_SABER ) {
		CG_VRH_PutSabers();
	}
	if ( w == was ) {
		return;
	}
	vrh.weapon = w;
	if ( w == vrh.want && CG_VRH_Wanting( VRH_FOLLOW_MS ) ) {
		vrh.wantTime = 0;	// (what you took out or put away, through)
		return;
	}
	vrh.wantTime = 0;
	if ( w == WP_SABER && ( BG_VRHolsterWeapon( was ) || was == WP_MELEE ) && CG_VR_SaberHand( 0 ) < 0 &&
		CG_VR_SaberHand( 1 ) < 0 ) {
		CG_VR_TakeSaber( 0, CG_VR_GunHand(), 0.0f, 1.0f );
		if ( CG_VRH_Have( VR_HOLSTER_SABER + 1 ) ) {
			CG_VR_TakeSaber( 1, CG_VR_GunHand() ^ 1, 0.0f, 1.0f );
		}
	}
	// (else - spawned, a gun, fists: as it is)
}

// Your sabers each hang somewhere: one left without a holster (what was in
// it swapped out for something else, then put back by the stick) goes on its
// own hip - or, that taken, the first holster nothing's kept for.
static void CG_VRH_HomeSabers( void ) {
	int s, i, home;

	for ( s = 0; s < MAX_SABERS; s++ ) {
		const int item = VR_HOLSTER_SABER + s;
		if ( !CG_VRH_Have( item ) || CG_VRH_Out( item ) ) {
			continue;
		}
		for ( i = 0; i < VR_NUM_HOLSTERS && vrh.layout[i] != item; i++ ) {
		}
		if ( i < VR_NUM_HOLSTERS ) {
			continue;
		}
		home = ( s == 0 ) ? VR_HOLSTER_HIP_R : VR_HOLSTER_HIP_L;
		if ( vrh.layout[home] != VR_HOLSTER_EMPTY ) {
			for ( home = 0; home < VR_NUM_HOLSTERS && vrh.layout[home] != VR_HOLSTER_EMPTY; home++ ) {
			}
		}
		if ( home < VR_NUM_HOLSTERS ) {
			CG_VRH_Hang( home, item );
			CG_VR_Log( "holsters: your %s saber had nowhere to hang - holster %i\n", s ? "second" : "first", home );
		}
	}
}

// The quick draw: the sabers in hand lit, once the game has the saber out and
// they're its own (not still changing over).
static void CG_VRH_QuickDraw( void ) {
	const playerState_t *ps = &cg.predictedPlayerState;

	if ( !vrh.drawTime ) {
		return;
	}
	if ( cg.time - vrh.drawTime > VRH_DRAW_MS || cg.time - vrh.drawTime < 0 ||
		( CG_VR_SaberHand( 0 ) < 0 && CG_VR_SaberHand( 1 ) < 0 ) ) {
		vrh.drawTime = 0;	// (too late; or none in hand now)
		return;
	}
	if ( vrh.swapTime || ps->weapon != WP_SABER || ps->weaponstate == WEAPON_DROPPING ) {
		return;
	}
	trap->ext.VR_GenericCmd( GENCMD_VR_SABERON );
	vrh.drawTime = 0;
}

// What's in your holsters, for everyone ("vrh"): where each thing hangs
// (what's in your hands, where it goes back; the server shows what you have)
// - no more often than every 2.5 s, nor while your sabers are changing over;
// now, once they have (the server's changed its copy over).
static void CG_VRH_SendLayout( qboolean now ) {
	char text[32];

	if ( CG_VRH_Apply() ) {
		Com_sprintf( text, sizeof( text ), "%i,%i,%i,%i", vrh.layout[0], vrh.layout[1], vrh.layout[2], vrh.layout[3] );
	} else {
		text[0] = 0;
	}
	if ( !strcmp( text, vrh.sent ) || vrh.swapTime ) {
		return;
	}
	if ( !now && vrh.sentTime && cg.time - vrh.sentTime >= 0 && cg.time - vrh.sentTime < VRH_SEND_MS ) {
		return;
	}
	Q_strncpyz( vrh.sent, text, sizeof( vrh.sent ) );
	vrh.sentTime = cg.time ? cg.time : 1;
	trap->Cvar_Set( "vrh", text );
}

// (cg_vrbody.c: once a frame, before your arms are worked out) Your hands at
// your holsters: what a squeeze of either grip there takes out, puts away or
// swaps - and what the game's doing meanwhile.
void CG_VRH_HandsFrame( const vrClientState_t *vr ) {
	const playerState_t *ps = &cg.predictedPlayerState;
	const qboolean alive = (qboolean)( ps->stats[STAT_HEALTH] > 0 && !( ps->eFlags & EF_DEAD ) && ps->pm_type != PM_DEAD );
	int h;

	if ( vrh.frameTime == cg.time ) {
		return;	// (second eye)
	}
	vrh.frameTime = cg.time;
	vrh.on = CG_VRH_On();
	CG_VRH_ReadLayout();
	CG_VRH_Swapped();
	if ( !vrh.on ) {
		vrh.zones = 0;
		vrh.at[0] = vrh.at[1] = -1;
		vrh.later[0] = vrh.later[1] = 0;
		vrh.drawTime = vrh.wantTime = 0;
		vrh.weapon = WP_NONE;
		CG_VRH_SendLayout( qfalse );
		return;
	}
	CG_VRH_FollowWeapon( alive );
	if ( alive ) {
		CG_VRH_HomeSabers();
	}
	for ( h = 0; h < 2; h++ ) {
		const qboolean grabbing = (qboolean)( vr && vr->grabbing[h] );
		if ( vrh.later[h] && !vrh.swapTime ) {
			// a squeeze while your sabers changed over (both taken at once, say): now they have
			const int i = vrh.later[h] - 1;
			vrh.later[h] = 0;
			if ( alive && vr ) {
				CG_VRH_Squeeze( vr, h, i );
			}
		}
		if ( alive && grabbing && !vrh.grabWas[h] && vrh.at[h] >= 0 && ( vrh.zones & ( 1 << h ) ) &&
			!( CG_VR_TwoHandZones() & ( 1 << h ) ) ) {
			if ( vrh.swapTime ) {
				vrh.later[h] = vrh.at[h] + 1;	// (done the moment they have)
				CG_VR_BlockGrab( h );
			} else {
				CG_VRH_Squeeze( vr, h, vrh.at[h] );
			}
		}
		vrh.grabWas[h] = grabbing;
	}
	if ( alive && vr ) {
		CG_VRH_FindZones( vr );
	} else {
		vrh.zones = 0;
		vrh.at[0] = vrh.at[1] = -1;
	}
	CG_VRH_QuickDraw();
	CG_VRH_SendLayout( qfalse );
}

// Whether holsters are in play for you now (and were as your hands were
// last worked out).
static qboolean CG_VRH_Live( void ) {
	return (qboolean)( vrh.on && CG_VRH_On() );
}

// The hands whose squeeze the engine is to take as a grab (a holster's).
int CG_VRH_Zones( void ) {
	return CG_VRH_Live() ? vrh.zones : 0;
}

// The hand whose trigger attacks (ext.VR_SetWeaponHand): with holsters, -1
// while your hands are empty (a saber chosen, none in hand), what you took
// out or put away is on its way (the game still switching), or your sabers
// are changing over; 2: two sabers in your hands, either trigger lights or
// puts out both. -2: not for holsters to say.
int CG_VRH_WeaponHand( void ) {
	const int want = CG_VRH_Want(), h0 = CG_VR_SaberHand( 0 ), h1 = CG_VR_SaberHand( 1 );

	if ( !CG_VRH_Live() ) {
		return -2;
	}
	if ( want != cg.predictedPlayerState.weapon ) {
		return -1;
	}
	if ( want != WP_SABER ) {
		return CG_VR_GunHand();	// (a gun, fists)
	}
	if ( vrh.swapTime || h0 < 0 ) {
		return -1;
	}
	return ( h1 >= 0 && h1 != h0 ) ? 2 : h0;
}

// The buttons to set on every command (ext.VR_SetCmdButtons): holster rules;
// one of two sabers in hand; your hands empty, the saber chosen - a menu up,
// as your hands were. Not playing (loading, watching, a spectator) or not in
// VR yet: as you'd spawn, hands empty - from your first command, so what
// you're given is in its holster.
int CG_VRH_CmdButtons( void ) {
	const int h0 = CG_VR_SaberHand( 0 ), h1 = CG_VR_SaberHand( 1 );
	int b;

	if ( !CG_VRH_Apply() ) {
		return 0;
	}
	b = BUTTON_VR_HOLSTERS;
	if ( !vrh.on || !CG_VRH_Playing() ) {
		return b | BUTTON_VR_EMPTYHANDS;
	}
	if ( CG_VRH_Have( VR_HOLSTER_SABER + 1 ) && ( h0 < 0 ) != ( h1 < 0 ) ) {
		b |= BUTTON_VR_ONESABER;
	}
	if ( CG_VRH_Want() == WP_SABER && h0 < 0 && h1 < 0 && !cg.predictedPlayerState.saberInFlight ) {
		b |= BUTTON_VR_EMPTYHANDS;
	}
	return b;
}

// The holsters' buttons, said to the engine for every command (cg_vrbody.c:
// each frame; cg_view.c: each frame of the loading screen; cg_main.c: as the
// game's loaded, for the first command).
void CG_VRH_SayButtons( void ) {
	if ( CG_VR_Engine() && trap->ext.VR_SetCmdButtons ) {
		trap->ext.VR_SetCmdButtons( CG_VRH_CmdButtons() );
	}
}

// The gun to draw in your gun hand (cg_weapons.c): the one you took out at
// once, the game's switch to it still under way.
int CG_VRH_HeldWeapon( int weapon ) {
	if ( !CG_VRH_Live() ) {
		return weapon;
	}
	return CG_VRH_Want();
}

// (cg_vrbody.c, after the scene is built) Your holsters' markers.
void CG_VRH_EndFrame( const vrClientState_t *vr ) {
	if ( CG_VRH_Live() && vr ) {
		CG_VRH_AddMarkers( vr );
	}
}

void CG_VRH_Init( void ) {
	memset( &vrh, 0, sizeof( vrh ) );
	vrh.at[0] = vrh.at[1] = -1;
	vrh.weapon = WP_NONE;
	trap->Cvar_Register( &vrh.vrh, "vrh", "", CVAR_USERINFO | CVAR_TEMP );
	vrh.ringShader = trap->R_RegisterShader( "gfx/vr/holster_ring" );
	trap->Cvar_Set( "vrh", "" );	// (nothing said till it's so)
	CG_VRH_ReadLayout();
}
