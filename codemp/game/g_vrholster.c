/*
===========================================================================
OpenJK VR (multiplayer) - holsters, the server's side

A VR player whose weapons hang in holsters on their body (their game:
cg_vrholster.c) says so on every command (BUTTON_VR_HOLSTERS), and when one
of their two sabers is in hand (BUTTON_VR_ONESABER) or none is, their hands
empty (BUTTON_VR_EMPTYHANDS). The shared rules go by it (bg_pmove.c: a saber
taken out of its holster comes out unlit, and one not in hand isn't lit;
bg_saber.c: one of two lights alone), and so do the server's:

- "vrHolsters 1" in the serverinfo: this server has them (g_vrHolsters 1).
  With it off, the bits are taken off every command. A command run again
  with none come in (g_active.c, a forced update) keeps them.
- They spawn with the saber in its holster, unlit (as their latest command
  says: their first, coming in - their game sets the bits from it).
- One of two sabers lit, it's lit alone in any single style its saber
  allows (stock: two sabers with one lit fight in the fast one), and the
  style cycle picks it.
- GENCMD_VR_SABERSWAP: the second saber becomes the first - the one of two
  taken out first is the game's first, which is the one that lights alone.
  Only with one of two in hand, never a siege class's (the class says which
  is which), at most four a second.
- GENCMD_VR_SABERON: light the sabers in hand (the quick draw: the trigger
  held as a hilt is taken out). The trigger's switch, a duel's start: the
  same - none with their hands empty.
- Each of those once a command (not again when a spawn runs it again).
- What's in their holsters (userinfo "vrh": where each thing hangs) goes to
  everyone as far as they have it (CS_PLAYERS "vh"), with how often their
  sabers have changed over ("vs"), and - for everyone, desktop players and
  bots too - whether they have a saber, or two ("hs"): everyone's hilts hang
  on their belts when not in hand. A weapon got or lost: everyone told.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "g_local.h"
#include "bg_vrholster.h"

#define VR_SWAPS_A_SECOND	4	// their sabers change over no more often than this

qboolean G_SaberModelSetup( gentity_t *ent );	// (g_client.c)

void G_VRHolsterInit( void ) {
	trap->Cvar_Register( NULL, "vrHolsters", "0", CVAR_SERVERINFO | CVAR_ROM );
	G_VRHolsterPublish();
}

// "vrHolsters" in the serverinfo follows g_vrHolsters - and so does what
// everyone's told of each player's holsters and sabers.
void G_VRHolsterPublish( void ) {
	int i;

	trap->Cvar_Set( "vrHolsters", ( g_vrHolsters.integer > 0 ) ? "1" : "0" );
	for ( i = 0; i < level.maxclients && i < MAX_CLIENTS; i++ ) {
		if ( g_entities[i].inuse && level.clients && level.clients[i].pers.connected == CON_CONNECTED ) {
			ClientUserinfoChanged( i );
		}
	}
}

// A command as this server takes it (ClientThink, before anything goes by
// it): the holster bits only from a player of this build on a server with
// holsters - and one of two sabers only with two.
void G_VRHolsterCmd( gentity_t *ent, usercmd_t *ucmd ) {
	if ( g_vrHolsters.integer <= 0 || !ent->client || ( ent->r.svFlags & SVF_BOT ) || ent->s.number >= MAX_CLIENTS ||
		!( ucmd->buttons & BUTTON_VR_HOLSTERS ) ) {
		ucmd->buttons &= ~( BUTTON_VR_HOLSTERS | BUTTON_VR_ONESABER | BUTTON_VR_EMPTYHANDS );
		return;
	}
	if ( !ent->client->saber[0].model[0] || !ent->client->saber[1].model[0] || ( ucmd->buttons & BUTTON_VR_EMPTYHANDS ) ) {
		ucmd->buttons &= ~BUTTON_VR_ONESABER;
	}
}

// Whether they play with holsters, as their last command says.
qboolean G_VRHolsters( const gentity_t *ent ) {
	return (qboolean)( ent && ent->client && ( ent->client->pers.cmd.buttons & BUTTON_VR_HOLSTERS ) );
}

// Whether one of their two sabers is in hand, as their last command says.
static qboolean G_VROneSaber( const gentity_t *ent ) {
	return (qboolean)( G_VRHolsters( ent ) && ( ent->client->pers.cmd.buttons & BUTTON_VR_ONESABER ) &&
		ent->client->saber[1].model[0] );
}

// Whether they have a saber: 0, 1 or 2.
static int G_VRHasSabers( const gentity_t *ent ) {
	const gclient_t *cl = ent->client;

	if ( !cl || cl->sess.sessionTeam == TEAM_SPECTATOR || !( cl->ps.stats[STAT_WEAPONS] & ( 1 << WP_SABER ) ) ||
		!cl->saber[0].model[0] ) {
		return 0;
	}
	return cl->saber[1].model[0] ? 2 : 1;
}

// What everyone's shown of their holsters: what's in each, as far as they
// have it (a saber, a weapon they've got; a spectator, nothing).
static void G_VRHolsterShown( const gentity_t *ent, int out[VR_NUM_HOLSTERS] ) {
	const gclient_t *cl = ent->client;
	const int sabers = G_VRHasSabers( ent );
	int i;

	for ( i = 0; i < VR_NUM_HOLSTERS; i++ ) {
		const int c = cl->pers.vrHolsters[i];
		out[i] = VR_HOLSTER_EMPTY;
		if ( c == VR_HOLSTER_SABER || c == VR_HOLSTER_SABER + 1 ) {
			out[i] = ( sabers > c - VR_HOLSTER_SABER ) ? c : VR_HOLSTER_EMPTY;
		} else if ( c >= VR_HOLSTER_WEAPON && cl->sess.sessionTeam != TEAM_SPECTATOR &&
			( cl->ps.stats[STAT_WEAPONS] & ( 1 << ( c - VR_HOLSTER_WEAPON ) ) ) ) {
			out[i] = c;
		}
	}
}

// Their holsters in their info for everyone (CS_PLAYERS) told afresh - the
// rest of it as it is.
static void G_VRHolsterRefresh( gentity_t *ent ) {
	char buf[MAX_INFO_STRING];

	trap->GetConfigstring( CS_PLAYERS + ent->s.number, buf, sizeof( buf ) );
	if ( !buf[0] ) {
		return;
	}
	Info_RemoveKey( buf, "vh" );
	Info_RemoveKey( buf, "vs" );
	Info_RemoveKey( buf, "hs" );
	G_VRHolsterInfo( ent, buf, sizeof( buf ) );
	trap->SetConfigstring( CS_PLAYERS + ent->s.number, buf );
}

// Every frame: whether each player has a saber, or two, and what of their
// holsters' they have, go out with their info when that changes (a saber or
// a weapon given or taken mid-life).
void G_VRHolsterFrame( void ) {
	int i;

	if ( g_vrHolsters.integer <= 0 ) {
		return;
	}
	for ( i = 0; i < level.maxclients && i < MAX_CLIENTS; i++ ) {
		gentity_t *ent = &g_entities[i];
		int shown[VR_NUM_HOLSTERS];
		if ( !ent->inuse || !ent->client || ent->client->pers.connected != CON_CONNECTED ) {
			continue;
		}
		G_VRHolsterShown( ent, shown );
		if ( G_VRHasSabers( ent ) != ent->client->pers.vrHasSabers ||
			( ent->client->pers.vrHasHolsters && memcmp( shown, ent->client->pers.vrShown, sizeof( shown ) ) ) ) {
			G_VRHolsterRefresh( ent );
		}
	}
}

// ClientSpawn, with their weapons given and their latest command read (as
// this server takes it - coming in, their first, which their game sets the
// bits on from the first): a VR player with holsters has the saber in its
// holster, unlit; and whether they have a saber goes out with their info if
// that changed.
void G_VRHolsterSpawn( gentity_t *ent ) {
	if ( G_VRHolsters( ent ) ) {
		ent->client->ps.saberHolstered = 2;
	}
	if ( G_VRHasSabers( ent ) != ent->client->pers.vrHasSabers ) {
		G_VRHolsterRefresh( ent );
	}
}

// Whether one of two sabers lit alone fights in style: a single style, as
// far as their saber offence goes, not one their (first) saber forbids.
static qboolean G_VROneStyleOk( const gclient_t *cl, int style ) {
	const int top = Com_Clampi( SS_FAST, SS_STRONG, cl->ps.fd.forcePowerLevel[FP_SABER_OFFENSE] );

	return (qboolean)( style >= SS_FAST && style <= top && !( cl->saber[0].stylesForbidden & ( 1 << style ) ) );
}

// The style picked for one of two (fast till one is) - or, not one it can
// fight in, the nearest below it that is, else above (none: fast, as stock).
static int G_VROneStylePicked( const gclient_t *cl ) {
	int style;

	for ( style = cl->pers.vrOneStyle ? cl->pers.vrOneStyle : SS_FAST; style >= SS_FAST; style-- ) {
		if ( G_VROneStyleOk( cl, style ) ) {
			return style;
		}
	}
	for ( style = SS_FAST; style <= SS_STRONG; style++ ) {
		if ( G_VROneStyleOk( cl, style ) ) {
			return style;
		}
	}
	return SS_FAST;
}

// The single style of one of two sabers, lit (g_active.c: with two sabers
// and one lit, saberHolstered 1): stock, the fast one; a VR player with one
// of them in hand, the one they picked (not changed mid-swing).
int G_VRHolsterOneStyle( gentity_t *ent ) {
	gclient_t *cl = ent->client;
	const int now = cl->ps.fd.saberAnimLevel;

	if ( !G_VROneSaber( ent ) ) {
		return SS_FAST;
	}
	if ( cl->ps.weaponTime > 0 && G_VROneStyleOk( cl, now ) ) {
		return now;	// (the queue: CG_SaberAttackCycle's way)
	}
	return G_VROneStylePicked( cl );
}

// Cmd_SaberAttackCycle_f with one of two sabers in hand: the next single
// style it can fight in, as with one saber (stock: lights or puts out the
// second). qfalse: not so.
qboolean G_VRHolsterCycle( gentity_t *ent ) {
	gclient_t *cl = ent->client;
	int style, k;

	if ( !G_VROneSaber( ent ) ) {
		return qfalse;
	}
	style = G_VROneStylePicked( cl );
	for ( k = 0; k < SS_STRONG; k++ ) {
		style = ( style >= SS_STRONG ) ? SS_FAST : style + 1;
		if ( G_VROneStyleOk( cl, style ) ) {
			break;
		}
	}
	cl->pers.vrOneStyle = style;
	return qtrue;
}

// How the sabers light now (Cmd_ToggleSaber_f, GENCMD_VR_SABERON, a duel's
// start): one of two in hand, it alone (1); none in hand - their hands empty
// - none (2); else all (0).
int G_VRHolsterLit( gentity_t *ent ) {
	if ( G_VRHolsters( ent ) && ( ent->client->pers.cmd.buttons & BUTTON_VR_EMPTYHANDS ) ) {
		return 2;
	}
	return G_VROneSaber( ent ) ? 1 : 0;
}

// The sabers in hand (and lit, to: 0 all, 1 the first) from as they were
// (saberHolstered from): each one that lights is heard.
static void G_VRSaberLightSounds( gentity_t *ent, int from, int to ) {
	gclient_t *cl = ent->client;

	if ( from == 2 && to < 2 && cl->saber[0].soundOn ) {
		G_Sound( ent, CHAN_AUTO, cl->saber[0].soundOn );
	}
	if ( from >= 1 && to == 0 && cl->saber[1].model[0] && cl->saber[1].soundOn ) {
		G_Sound( ent, CHAN_AUTO, cl->saber[1].soundOn );
	}
}

// Lights the sabers in hand (as G_VRHolsterLit says), each one that lights
// heard. qfalse: nothing to light.
qboolean G_VRHolsterLight( gentity_t *ent ) {
	gclient_t *cl = ent->client;
	const int to = G_VRHolsterLit( ent );

	if ( cl->ps.saberHolstered <= to ) {
		return qfalse;	// (lit already - or none in hand)
	}
	G_VRSaberLightSounds( ent, cl->ps.saberHolstered, to );
	cl->ps.saberHolstered = to;
	return qtrue;
}

// Whether a holsters' generic command (GENCMD_VR_*) on this command is to be
// done: once a command - not again when the same one's run again (a spawn's
// think runs their latest command once more).
qboolean G_VRHolsterOnce( gentity_t *ent, const usercmd_t *ucmd ) {
	gclient_t *cl = ent->client;

	if ( !cl || ucmd->serverTime == cl->pers.vrGenCmdTime ) {
		return qfalse;
	}
	cl->pers.vrGenCmdTime = ucmd->serverTime;
	return qtrue;
}

// GENCMD_VR_SABERON: light the sabers in hand (the quick draw) - as the
// trigger would (Cmd_ToggleSaber_f), but only ever on.
void G_VRHolsterSaberOn( gentity_t *ent ) {
	gclient_t *cl = ent->client;

	if ( !cl || !G_VRHolsters( ent ) || G_RPDowned( ent ) || cl->ps.weapon != WP_SABER || cl->ps.saberInFlight ||
		( cl->ps.fd.forceGripCripple && cl->ps.saberHolstered ) || cl->ps.forceHandExtend != HANDEXTEND_NONE ||
		cl->ps.duelTime >= level.time || cl->ps.saberLockTime >= level.time ) {
		return;
	}
	G_VRHolsterLight( ent );
}

// Swaps two values in an info string.
static void G_VRSwapInfo( char *info, const char *a, const char *b ) {
	char va1[MAX_INFO_VALUE], vb[MAX_INFO_VALUE];

	Q_strncpyz( va1, Info_ValueForKey( info, a ), sizeof( va1 ) );
	Q_strncpyz( vb, Info_ValueForKey( info, b ), sizeof( vb ) );
	Info_SetValueForKey( info, a, vb );
	Info_SetValueForKey( info, b, va1 );
}

// GENCMD_VR_SABERSWAP: their second saber becomes their first, and the first
// the second - the one of two they took out first is the game's first (the
// one that lights alone), the other hanging in its holster, both unlit. Its
// name and colours change over with it (their userinfo too, which their own
// game then says back, so the next spawn keeps them so; a roleplay
// character's colours too), and so do the codes of the holsters they hang in.
// Only with one of the two in hand; not a siege class's sabers (the class
// says which is which: they'd change back); no more often than four times a
// second.
void G_VRHolsterSaberSwap( gentity_t *ent ) {
	gclient_t *cl = ent->client;
	const int n = ent->s.number;
	char userinfo[MAX_INFO_STRING], name[MAX_QPATH];
	saberInfo_t tmp;
	int i, codes[VR_NUM_HOLSTERS];

	if ( !cl || !G_VROneSaber( ent ) || !cl->saber[0].model[0] || cl->ps.saberInFlight || cl->ps.saberHolstered != 2 ||
		cl->ps.saberLockTime >= level.time || ( level.gametype == GT_SIEGE && cl->siegeClass != -1 ) ) {
		return;	// (a lit one would light the other: never asked)
	}
	if ( level.time - cl->pers.vrSwapTime >= 1000 || level.time - cl->pers.vrSwapTime < 0 ) {
		cl->pers.vrSwapTime = level.time;
		cl->pers.vrSwapCount = 0;
	}
	if ( cl->pers.vrSwapCount >= VR_SWAPS_A_SECOND ) {
		return;
	}
	cl->pers.vrSwapCount++;
	tmp = cl->saber[0];
	cl->saber[0] = cl->saber[1];
	cl->saber[1] = tmp;
	Q_strncpyz( name, cl->pers.saber1, sizeof( name ) );
	Q_strncpyz( cl->pers.saber1, cl->pers.saber2, sizeof( cl->pers.saber1 ) );
	Q_strncpyz( cl->pers.saber2, name, sizeof( cl->pers.saber2 ) );
	trap->GetUserinfo( n, userinfo, sizeof( userinfo ) );
	G_VRSwapInfo( userinfo, "saber1", "saber2" );
	G_VRSwapInfo( userinfo, "color1", "color2" );
	if ( BG_VRHolsterParse( Info_ValueForKey( userinfo, "vrh" ), codes ) ) {
		// (the holsters each hangs in: their game's own say changes over too, once it hears)
		for ( i = 0; i < VR_NUM_HOLSTERS; i++ ) {
			if ( codes[i] == VR_HOLSTER_SABER || codes[i] == VR_HOLSTER_SABER + 1 ) {
				codes[i] ^= 3;
			}
		}
		Info_SetValueForKey( userinfo, "vrh", va( "%i,%i,%i,%i", codes[0], codes[1], codes[2], codes[3] ) );
	}
	trap->SetUserinfo( n, userinfo );
	G_RPSwapSaberColors( n );
	cl->pers.vrSaberSwaps = ( cl->pers.vrSaberSwaps + 1 ) & 0xffff;
	G_SaberModelSetup( ent );
	ClientUserinfoChanged( n );
}

// ClientUserinfoChanged: what's in their holsters, as their game says
// ("vrh": nothing said, none - a desktop player, or holsters off).
void G_VRHolsterUserinfo( gentity_t *ent, const char *userinfo ) {
	gclient_t *cl = ent->client;

	cl->pers.vrHasHolsters = ( g_vrHolsters.integer > 0 && !( ent->r.svFlags & SVF_BOT ) &&
		BG_VRHolsterParse( Info_ValueForKey( userinfo, "vrh" ), cl->pers.vrHolsters ) ) ? qtrue : qfalse;
	if ( !cl->pers.vrHasHolsters ) {
		memset( cl->pers.vrHolsters, 0, sizeof( cl->pers.vrHolsters ) );
	}
}

// ClientUserinfoChanged (and G_VRHolsterRefresh): their holsters for everyone
// (CS_PLAYERS): "vh" what they hold, as far as they have it; "vs" how often
// their sabers have changed over (holsters on or not: their game counts on
// it); "hs" whether they have a saber or two.
void G_VRHolsterInfo( gentity_t *ent, char *buf, int size ) {
	gclient_t *cl = ent->client;
	const int *v = cl->pers.vrShown;

	cl->pers.vrHasSabers = G_VRHasSabers( ent );
	G_VRHolsterShown( ent, cl->pers.vrShown );
	if ( cl->pers.vrSaberSwaps ) {
		Q_strcat( buf, size, va( "vs\\%i\\", cl->pers.vrSaberSwaps ) );
	}
	if ( g_vrHolsters.integer <= 0 ) {
		return;
	}
	if ( cl->pers.vrHasHolsters ) {
		Q_strcat( buf, size, va( "vh\\%i,%i,%i,%i\\", v[0], v[1], v[2], v[3] ) );
	}
	if ( cl->pers.vrHasSabers ) {
		Q_strcat( buf, size, va( "hs\\%i\\", cl->pers.vrHasSabers ) );
	}
}
