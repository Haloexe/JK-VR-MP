/*
===========================================================================
OpenJK VR (multiplayer) - hanging on to a wall and climbing, the server's
side (the rules both sides go by: bg_pmove.c PM_VRWallGrip, PM_VRClimb; a
player's game's: cg_vrwall.c).

- "vrWallGrip" in the serverinfo: 1 - g_vrWallGrip on and VR players' poses
  coming in (this engine, offering sv_vrx), the hand that grips seen with
  each command; 2 - g_vrClimb on too, climbing. 0 otherwise, and as the
  game goes (G_ShutdownGame). A VR player's game of this build takes hold
  of walls only on a server saying so, and climbs only on one saying 2 (a
  game from before climbing hangs on there).
- A command saying a hand grips a wall (BUTTON_VR_WALLGRIP) goes by as it
  is from a player in VR whose hand, as they take hold, is at a wall: one
  of their empty hands (their avatar that came with it: G_VRSaberAvatar)
  has something solid at it or just past it, reached from their eyes. Not
  from anyone else - bots, desktop players (+button15), a server with it
  off: the grip's taken out of the command.
- Climbing (a server saying 2, a player whose userinfo says cg_vrClimb 1:
  G_VRClimbs - pmove.vrClimb), the hand that takes hold is at anything
  solid they can see (not the sky, not the invisible), on the ground or
  off it; the grip's commands' moves are how fast their hands move them
  (vr_public.h VR_CLIMB_*), taken out with the grip when it is. Holding
  on, a hand of theirs must be at something every so often
  (VRW_CLIMB_GRACE), or they're let go of as though knocked off.
- Hanging on, getting hurt or Force pushed or pulled knocks them off, as
  the game's own debug melee wall hold (g_debugMelee): g_combat.c, w_force.c.
- Riding something, the same bit's a VR driver's yoke: g_vrwalker.c.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "g_local.h"

#define VRW_TOUCH		16.0f	// a hand this near a wall (units past it, reached from the eyes) is at it (theirs: 8 - their hand as
								// they saw it a moment before)
#define VRW_CLIMB_GRACE	1500	// ms: climbing, holding on with no hand of theirs at anything no longer than this (their game's
								// own: 1000)

static qboolean	vrClimbs[MAX_CLIENTS];		// they climb (cg_vrClimb in their userinfo: G_VRWallUserinfo)
static int		vrClimbTouch[MAX_CLIENTS];	// level.time a hand of theirs was last at something, climbing

void G_VRWallInit( void ) {
	memset( vrClimbs, 0, sizeof( vrClimbs ) );
	memset( vrClimbTouch, 0, sizeof( vrClimbTouch ) );
	trap->Cvar_Register( NULL, "vrWallGrip", "0", CVAR_SERVERINFO | CVAR_ROM );
	G_VRWallPublish();
}

// Whether this server climbs: g_vrWallGrip and g_vrClimb on, VR players'
// poses coming in.
static qboolean G_VRWallClimbing( void ) {
	return (qboolean)( g_vrWallGrip.integer > 0 && g_vrClimb.integer > 0 && G_VRPosesAvailable() );
}

// "vrWallGrip" in the serverinfo follows g_vrWallGrip (2: g_vrClimb too) -
// with VR players' poses coming in, or there's no hand seen at a wall.
void G_VRWallPublish( void ) {
	trap->Cvar_Set( "vrWallGrip", G_VRWallClimbing() ? "2" : ( g_vrWallGrip.integer > 0 && G_VRPosesAvailable() ) ? "1" : "0" );
}

// A player's slot taken or left: nothing of theirs kept.
void G_VRWallClientReset( int clientNum ) {
	if ( clientNum >= 0 && clientNum < MAX_CLIENTS ) {
		vrClimbs[clientNum] = qfalse;
		vrClimbTouch[clientNum] = 0;
	}
}

// ClientUserinfoChanged: whether they climb (cg_vrClimb: nothing said - a game
// from before climbing, or not in VR - they don't).
void G_VRWallUserinfo( gentity_t *ent, const char *userinfo ) {
	const int n = ent ? ent->s.number : -1;

	if ( n >= 0 && n < MAX_CLIENTS && userinfo ) {
		vrClimbs[n] = (qboolean)( atoi( Info_ValueForKey( userinfo, "cg_vrClimb" ) ) > 0 );
	}
}

// Whether a player climbs here (pmove.vrClimb): a VR player of this build who
// climbs, on a server that climbs.
qboolean G_VRClimbs( const gentity_t *ent ) {
	const int n = ent ? ent->s.number : -1;

	return (qboolean)( n >= 0 && n < MAX_CLIENTS && vrClimbs[n] && ent->client && !( ent->r.svFlags & SVF_BOT ) &&
		G_VRWallClimbing() );
}

// The game going (a map's end, the server's): none said till it's back.
void G_VRWallShutdown( void ) {
	trap->Cvar_Set( "vrWallGrip", "0" );
}

// Whether one of a VR player's empty hands is at a wall, as their avatar
// that came with this command has it (its hands from where they are) -
// climbing, at anything they could climb: not the sky, nor anything they
// can't see (as their game has it: cg_vrwall.c CG_VRW_At).
static qboolean G_VRWallHandAt( gentity_t *ent, qboolean climbing ) {
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
		if ( tr.fraction < 1.0f && !tr.startsolid && !( climbing && ( tr.surfaceFlags & ( SURF_SKY | SURF_NODRAW ) ) ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

// The grip taken out of a command - and from a player who climbs (or thinks
// this server lets them: their game sends it so), its moves with it: they're
// how fast their hands move them, never steering.
static void G_VRWallStrip( const gentity_t *ent, usercmd_t *ucmd ) {
	const int n = ent ? ent->s.number : -1;

	ucmd->buttons &= ~BUTTON_VR_WALLGRIP;
	if ( n >= 0 && n < MAX_CLIENTS && vrClimbs[n] ) {
		ucmd->forwardmove = ucmd->rightmove = ucmd->upmove = 0;
	}
}

// A command as this server takes it (ClientThink_real, before it moves them):
// a hand's grip on a wall only from a VR player of this build whose hand is
// at one as they take hold - hanging on, or let go of it since with the grip
// still held (PMF_VR_WALLGRIP), it goes by as it is. Climbing, a hand of
// theirs is at something as they take hold - and, holding on, now and then
// (VRW_CLIMB_GRACE), or they're let go of (G_LetGoOfWall: as though knocked
// off - the grip still held, as it is).
void G_VRWallCmd( gentity_t *ent, usercmd_t *ucmd ) {
	const int n = ent->s.number;

	if ( !( ucmd->buttons & BUTTON_VR_WALLGRIP ) ) {
		return;
	}
	if ( ent->client && ent->client->ps.m_iVehicleNum ) {
		G_VRWalkerCmd( ent, ucmd );	// (riding something: the bit's the yoke's - g_vrwalker.c)
		return;
	}
	if ( g_vrWallGrip.integer <= 0 || !ent->client || ( ent->r.svFlags & SVF_BOT ) || n < 0 || n >= MAX_CLIENTS ||
		!G_VRPosesAvailable() ) {
		G_VRWallStrip( ent, ucmd );
		return;
	}
	if ( G_VRClimbs( ent ) ) {
		if ( G_VRWallHandAt( ent, qtrue ) ) {
			vrClimbTouch[n] = level.time;
		} else if ( !( ent->client->ps.pm_flags & PMF_VR_WALLGRIP ) ) {
			G_VRWallStrip( ent, ucmd );	// (taking hold of nothing)
		} else if ( ( ent->client->ps.pm_flags & PMF_STUCK_TO_WALL ) && level.time - vrClimbTouch[n] > VRW_CLIMB_GRACE ) {
			G_LetGoOfWall( ent );	// (holding on to nothing a while)
		}
		return;
	}
	if ( ent->client->ps.pm_flags & PMF_VR_WALLGRIP ) {
		return;
	}
	if ( !G_VRWallHandAt( ent, qfalse ) ) {
		G_VRWallStrip( ent, ucmd );
	}
}
