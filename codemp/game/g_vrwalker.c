/*
===========================================================================
OpenJK VR (multiplayer) - driving an AT-ST from its cockpit in VR, the
server's side (what both sides go by: bg_vrwalker.h; the driver's game:
cg_vrwalker.c).

- "vrWalker 1" in the serverinfo: g_vrWalker on. 0 with it off, and as the
  game goes (G_ShutdownGame). A VR driver's game of this build sits them
  in the cockpit only on a server saying so - one that drives the AT-ST by
  their yoke, as their own game does (WalkerNPC.c: it predicts it so).
- A command from an AT-ST's driver saying their yoke drives it
  (BUTTON_VR_YOKE) goes by as it is - its twist turns it, their view only
  aims (WalkerNPC.c, g_weapon.c). From anyone else riding something - a
  passenger, a bot, the driver of anything but an AT-ST, anyone on a server
  with it off - the yoke's taken out of the command. (On foot the same bit's
  a hand on a wall: g_vrwall.c.)

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "g_local.h"
#include "bg_vrwalker.h"

void G_VRWalkerInit( void ) {
	trap->Cvar_Register( NULL, "vrWalker", "0", CVAR_SERVERINFO | CVAR_ROM );
	G_VRWalkerPublish();
}

// "vrWalker" in the serverinfo follows g_vrWalker.
void G_VRWalkerPublish( void ) {
	trap->Cvar_Set( "vrWalker", ( g_vrWalker.integer > 0 ) ? "1" : "0" );
}

// The game going (a map's end, the server's): none said till it's back.
void G_VRWalkerShutdown( void ) {
	trap->Cvar_Set( "vrWalker", "0" );
}

// Whether a rider's yoke drives their vehicle: they drive it, it's an AT-ST,
// they're no bot, the server lets them.
qboolean G_VRWalkerYoke( gentity_t *ent ) {
	gentity_t *veh;

	if ( g_vrWalker.integer <= 0 || !ent->client || ( ent->r.svFlags & SVF_BOT ) || ent->s.number < 0 || ent->s.number >= MAX_CLIENTS ) {
		return qfalse;
	}
	if ( ent->client->ps.m_iVehicleNum <= 0 || ent->client->ps.m_iVehicleNum >= ENTITYNUM_WORLD ) {
		return qfalse;
	}
	veh = &g_entities[ent->client->ps.m_iVehicleNum];
	return (qboolean)( veh->inuse && veh->client && veh->m_pVehicle && veh->m_pVehicle->m_pPilot == (bgEntity_t *)ent &&
		BG_VRWalkerCockpit( veh->m_pVehicle->m_pVehicleInfo ) );
}

// A rider's command as this server takes it (G_VRWallCmd: ClientThink_real,
// before it goes to their vehicle): the yoke only from one whose yoke drives
// it.
void G_VRWalkerCmd( gentity_t *ent, usercmd_t *ucmd ) {
	if ( ( ucmd->buttons & BUTTON_VR_YOKE ) && !G_VRWalkerYoke( ent ) ) {
		ucmd->buttons &= ~BUTTON_VR_YOKE;
	}
}
