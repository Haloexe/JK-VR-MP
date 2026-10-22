/*
===========================================================================
OpenJK VR (multiplayer) - saber colours, the server's side (bg_vrcolor.h)

A player of this build sends each saber's colour of its own and black core
in their userinfo (saberRGB1/2, saberCore1/2); this server checks them and
tells everyone in the player's info (CS_PLAYERS sc1/sc2), for every game of
this build to draw. A roleplay character keeps its own (g_vrrp.c: they're
part of its look). "vrSaberRGB 1" in the serverinfo says so; g_vrSaberRGB 0
tells nobody (everyone sees stock colours, their own game too).

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "g_local.h"
#include "bg_vrcolor.h"

void G_VRColorInit( void ) {
	trap->Cvar_Register( NULL, "vrSaberRGB", "0", CVAR_SERVERINFO | CVAR_ROM );
	G_VRColorPublish();
}

// "vrSaberRGB" in the serverinfo follows g_vrSaberRGB - and so does what
// everyone's told of each player's sabers (after the first: there's nobody yet).
void G_VRColorPublish( void ) {
	int i;

	trap->Cvar_Set( "vrSaberRGB", ( g_vrSaberRGB.integer > 0 ) ? "1" : "0" );
	if ( !level.clients ) {
		return;
	}
	for ( i = 0; i < level.maxclients && i < MAX_CLIENTS; i++ ) {
		if ( g_entities[i].inuse && level.clients[i].pers.connected == CON_CONNECTED ) {
			ClientUserinfoChanged( i );
		}
	}
}

// One of their sabers' colour (saber 0 or 1) from their userinfo - or their
// roleplay character's.
static void G_VRColorOf( int clientNum, const char *userinfo, int saber, vrBladeColor_t *out ) {
	char rgb[MAX_QPATH], core[MAX_QPATH];
	const char *kRGB = saber ? "saberRGB2" : "saberRGB1", *kCore = saber ? "saberCore2" : "saberCore1";

	Q_strncpyz( rgb, Info_ValueForKey( userinfo, kRGB ), sizeof( rgb ) );
	Q_strncpyz( core, Info_ValueForKey( userinfo, kCore ), sizeof( core ) );
	G_RPLookValue( clientNum, kRGB, rgb, sizeof( rgb ) );
	G_RPLookValue( clientNum, kCore, core, sizeof( core ) );
	BG_VRSC_FromSettings( rgb, core, out );
}

// ClientUserinfoChanged: their sabers' colours in their info for everyone
// (CS_PLAYERS sc1, sc2) - nothing for a stock colour with a white core, a
// bot, or with g_vrSaberRGB off.
void G_VRColorInfo( const gentity_t *ent, const char *userinfo, char *buf, int size ) {
	int s;

	if ( g_vrSaberRGB.integer <= 0 || ( ent->r.svFlags & SVF_BOT ) || ent->s.number < 0 || ent->s.number >= MAX_CLIENTS ) {
		return;
	}
	for ( s = 0; s < 2; s++ ) {
		vrBladeColor_t c;
		char v[16];
		G_VRColorOf( ent->s.number, userinfo, s, &c );
		BG_VRSC_Format( &c, v, sizeof( v ) );
		if ( v[0] ) {
			Q_strcat( buf, size, va( "sc%i\\%s\\", s + 1, v ) );
		}
	}
}
