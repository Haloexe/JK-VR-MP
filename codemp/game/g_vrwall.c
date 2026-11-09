/*
===========================================================================
OpenJK VR (multiplayer) - hanging on to a wall, the server's side (the rule
both sides go by: bg_pmove.c PM_VRWallGrip; a player's game's: cg_vrwall.c).

- "vrWallGrip 1" in the serverinfo: g_vrWallGrip on and VR players' poses
  coming in (this engine, offering sv_vrx) - the hand that grips is seen
  with each command. 0 otherwise, and as the game goes (G_ShutdownGame).
  A VR player's game of this build takes hold of walls only on a server
  saying so.
- A command saying a hand grips a wall (BUTTON_VR_WALLGRIP) goes by as it
  is from a player in VR whose hand, as they take hold, is at a wall: one
  of their empty hands (their avatar that came with it: G_VRSaberAvatar)
  has something solid at it or just past it, reached from their eyes. Not
  from anyone else - bots, desktop players (+button15), a server with it
  off: the grip's taken out of the command.
- Hanging on, getting hurt or Force pushed or pulled knocks them off, as
  the game's own debug melee wall hold (g_debugMelee): g_combat.c, w_force.c.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "g_local.h"

#define VRW_TOUCH		16.0f	// a hand this near a wall (units past it, reached from the eyes) is at it (theirs: 8 - their hand as
								// they saw it a moment before)

void G_VRWallInit( void ) {
	trap->Cvar_Register( NULL, "vrWallGrip", "0", CVAR_SERVERINFO | CVAR_ROM );
	G_VRWallPublish();
}

// "vrWallGrip" in the serverinfo follows g_vrWallGrip - with VR players'
// poses coming in, or there's no hand seen at a wall.
void G_VRWallPublish( void ) {
	trap->Cvar_Set( "vrWallGrip", ( g_vrWallGrip.integer > 0 && G_VRPosesAvailable() ) ? "1" : "0" );
}

// The game going (a map's end, the server's): none said till it's back.
void G_VRWallShutdown( void ) {
	trap->Cvar_Set( "vrWallGrip", "0" );
}

// Whether one of a VR player's empty hands is at a wall, as their avatar
// that came with this command has it (its hands from where they are).
static qboolean G_VRWallHandAt( gentity_t *ent ) {
	const gclient_t *cl = ent->client;
	vrAvatar_t a;
	vec3_t eye, hand, dir, end;
	trace_t tr;
	int h;

	if ( !G_VRSaberAvatar( ent->s.number, &a ) ) {
		return qfalse;
	}
	VectorCopy( cl->ps.origin, eye );
	eye[2] += cl->ps.viewheight;
	for ( h = 0; h < 2; h++ ) {
		if ( a.hand[h].grip != VR_GRIP_FREE ) {
			continue;
		}
		VectorAdd( cl->ps.origin, a.hand[h].origin, hand );
		VectorSubtract( hand, eye, dir );
		if ( VectorNormalize( dir ) < 1.0f ) {
			continue;
		}
		VectorMA( hand, VRW_TOUCH, dir, end );
		trap->Trace( &tr, eye, NULL, NULL, end, ent->s.number, VR_WALLGRIP_MASK, qfalse, 0, 0 );
		if ( tr.fraction < 1.0f && !tr.startsolid ) {
			return qtrue;
		}
	}
	return qfalse;
}

// A command as this server takes it (ClientThink_real, before it moves them):
// a hand's grip on a wall only from a VR player of this build whose hand is
// at one as they take hold - hanging on, or let go of it since with the grip
// still held (PMF_VR_WALLGRIP), it goes by as it is.
void G_VRWallCmd( gentity_t *ent, usercmd_t *ucmd ) {
	if ( !( ucmd->buttons & BUTTON_VR_WALLGRIP ) ) {
		return;
	}
	if ( g_vrWallGrip.integer <= 0 || !ent->client || ( ent->r.svFlags & SVF_BOT ) || ent->s.number < 0 || ent->s.number >= MAX_CLIENTS ||
		!G_VRPosesAvailable() ) {
		ucmd->buttons &= ~BUTTON_VR_WALLGRIP;
		return;
	}
	if ( ent->client->ps.pm_flags & PMF_VR_WALLGRIP ) {
		return;
	}
	if ( !G_VRWallHandAt( ent ) ) {
		ucmd->buttons &= ~BUTTON_VR_WALLGRIP;
	}
}
