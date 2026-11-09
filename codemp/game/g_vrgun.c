/*
===========================================================================
OpenJK VR (multiplayer) - shots from the barrel of a VR player's gun, the
server's side (the rule both sides go by: bg_vrgun.h).

- "vrGunBarrel" in the serverinfo: VR_BARREL_RULE with g_vrGunBarrel on
  and VR players' poses coming in (this engine, offering sv_vrx); 0
  otherwise - and as the game goes (G_ShutdownGame: another game module's
  never said to). A VR player's game of this build draws its aim dot from
  the barrel only on a server saying so.
- Only a player whose game says it goes by the rule too ("vrb" in their
  userinfo) fires from the barrel: an earlier build's aim dot is drawn from
  the eye, and so its shots still go - bots, desktop players and NPCs as
  ever.
- Their gun in their hand, as their avatar has it (this command's:
  G_VRSaberAvatar), the hand they hold guns in (G_VRGunHandLeft), aimed
  with it: not zoomed, in a vehicle or on an emplaced gun, the barrel the
  way they aim (VR_BARREL_AGREE). Its muzzle is its world model's, as their
  game draws it: each weapon's loaded the first time it's fired so, a map.
- The shot starts there (in whole units, as a bolt's start is:
  BG_VRBarrelPlace) with room for it all the way from their eye
  (BG_VRBarrelClear), the way they aim (FireWeapon's forward): every
  weapon's shot from the muzzle - the disruptor's too, which the game fires
  from the eye - a thermal detonator's, a trip mine's and a detpack's throw
  (WP_DropDetPack: not drawn back as the game's is). WP_TraceSetStart and
  W_TraceSetStart leave it be: found clear already, for the biggest shot.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "g_local.h"
#include "bg_vrgun.h"

typedef struct {
	qboolean	tried;		// its model's been tried, this map...
	qboolean	known;		// ...and loaded
	vec3_t		flash;		// its muzzle, in the model's own axes from its origin (its grip)
	vec3_t		barrel;		// the way its barrel runs in them (its "*flash"'s +X)
} vrGunModel_t;

static vrGunModel_t	vrGunModels[WP_NUM_WEAPONS];
static qboolean		vrGunRule[MAX_CLIENTS];	// their game goes by the rule too ("vrb": G_VRGunUserinfo)

// (after G_VRSaberInit: whether poses come in)
void G_VRGunInit( void ) {
	trap->Cvar_Register( NULL, "vrGunBarrel", "0", CVAR_SERVERINFO | CVAR_ROM );
	G_VRGunPublish();
}

// "vrGunBarrel" in the serverinfo follows g_vrGunBarrel - with VR players'
// poses coming in, or there's no gun in their hands to fire from.
void G_VRGunPublish( void ) {
	trap->Cvar_Set( "vrGunBarrel", ( g_vrGunBarrel.integer > 0 && G_VRPosesAvailable() ) ? XSTRING( VR_BARREL_RULE ) : "0" );
}

// The game going (a map's end, the server's): none said till it's back.
void G_VRGunShutdown( void ) {
	trap->Cvar_Set( "vrGunBarrel", "0" );
}

// Whether a player's game goes by this server's rule ("vrb" in their
// userinfo: ClientUserinfoChanged).
void G_VRGunUserinfo( gentity_t *ent, const char *userinfo ) {
	vrGunRule[ent->s.number] = (qboolean)( atoi( Info_ValueForKey( userinfo, "vrb" ) ) == VR_BARREL_RULE );
}

// A gun's world model's muzzle, as a player's game finds it
// (cg_weapons.c CG_VR_HeldWeaponPose): its "*flash" where its model's at
// rest - or, without one, VR_BARREL_NO_FLASH along the model's +X (where
// its flash is drawn). NULL: no model (the weapon a BG_VRBarrelWeapon).
static const vrGunModel_t *G_VRGunModel( int weapon ) {
	vrGunModel_t *m = &vrGunModels[weapon];

	if ( !m->tried ) {
		void *g2 = NULL;

		m->tried = qtrue;
		trap->G2API_InitGhoul2Model( &g2, BG_FindItemForWeapon( (weapon_t)weapon )->world_model[0], 0, 0, 0, 0, 0 );
		if ( g2 && trap->G2API_HaveWeGhoul2Models( g2 ) ) {
			vec3_t zero = { 0.0f, 0.0f, 0.0f };
			const int bolt = trap->G2API_AddBolt( g2, 0, "*flash" );
			mdxaBone_t mat;

			VectorSet( m->barrel, 1.0f, 0.0f, 0.0f );
			VectorScale( m->barrel, VR_BARREL_NO_FLASH, m->flash );
			if ( bolt >= 0 && trap->G2API_GetBoltMatrix( g2, 0, bolt, &mat, zero, zero, level.time, NULL, zero ) ) {
				BG_GiveMeVectorFromMatrix( &mat, ORIGIN, m->flash );
				BG_GiveMeVectorFromMatrix( &mat, POSITIVE_X, m->barrel );
				if ( !( VectorNormalize( m->barrel ) >= 0.5f ) ) {
					VectorSet( m->barrel, 1.0f, 0.0f, 0.0f );
				}
			}
			m->known = qtrue;
		}
		if ( g2 ) {
			trap->G2API_CleanGhoul2Models( &g2 );
		}
	}
	return m->known ? m : NULL;
}

static void G_VRGunTrace( trace_t *tr, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int pass, int mask ) {
	trap->Trace( tr, start, mins, maxs, end, pass, mask, qfalse, 0, 0 );
}

// Where a VR player's shot starts, from their gun's barrel (FireWeapon and
// WP_DropDetPack, after CalcMuzzlePoint: forward the way they aim) - qfalse:
// from the muzzle the game has (the eye's), as ever - or with anything in
// the way of the gun.
qboolean G_VRGunMuzzle( gentity_t *ent, const vec3_t forward, vec3_t muzzle ) {
	const int n = ent->s.number;
	const vrGunModel_t *m;
	gclient_t *cl;
	vrAvatar_t a;
	vrAvatarHand_t *hand;
	vec3_t grip, at, dir, eye;

	if ( g_vrGunBarrel.integer <= 0 || n >= MAX_CLIENTS || ( ent->r.svFlags & SVF_BOT ) || !vrGunRule[n] ) {
		return qfalse;	// (an NPC's, a bot's - or their game an earlier build's)
	}
	cl = ent->client;
	if ( !BG_VRBarrelWeapon( cl->ps.weapon ) || cl->ps.zoomMode || cl->ps.m_iVehicleNum || cl->ps.emplacedIndex ) {
		return qfalse;	// (aimed with their head, or not by them)
	}
	if ( !G_VRSaberAvatar( n, &a ) ) {
		return qfalse;
	}
	hand = &a.hand[G_VRGunHandLeft( ent ) ? VR_HAND_LEFT : VR_HAND_RIGHT];
	if ( hand->grip != VR_GRIP_GUN || !( m = G_VRGunModel( cl->ps.weapon ) ) ) {
		return qfalse;
	}
	// (the avatar's hands: off where they were when it came - moved with them since)
	VectorAdd( cl->ps.origin, hand->origin, grip );
	BG_VRBarrelPlace( grip, hand->axis, m->flash, m->barrel, at, dir );
	VectorCopy( cl->ps.origin, eye );
	eye[2] += cl->ps.viewheight;
	if ( DotProduct( dir, forward ) < VR_BARREL_AGREE || !BG_VRBarrelClear( G_VRGunTrace, n, eye, grip, at ) ) {
		return qfalse;
	}
	VectorCopy( at, muzzle );
	return qtrue;
}
