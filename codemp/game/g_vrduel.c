/*
===========================================================================
OpenJK VR (multiplayer) - private duels (Free For All, Holocron, Jedi
Master): as many at a time as there are pairs to fight them. Each pair is
on its own - their blows, Force and blades reach only each other (g_combat.c,
w_force.c, w_saber.c, g_weapon.c, g_vrsaber.c look at duelIndex) - and what
came of it goes to the two of them, and to everyone's console rather than
everyone's screen.

  engage_duel        (the client's, through the usercmd) challenge whoever
                     you're looking at, as ever
  vrduel <client>    challenge that player - what a VR player's saber
                     salute sends (cg_vrduel.c) - if they're near enough
                     (DUEL_CHALLENGE_RANGE) and in sight

A challenge stands for DUEL_CHALLENGE_TIME (twice the original: a salute
back takes a moment longer than a key), though the challenger's saber stays
as it is only as long as ever (ps.duelTime: no lighting it to strike whoever
lowers their guard); challenging someone else instead replaces it (not
straight away: one challenge every DUEL_RECHALLENGE_TIME at most, about as
often as ever). Whoever is challenged takes it up by challenging back: both
sabers go out, and on again two seconds later, when it starts. Bots take up
the challenges put to them (bot_acceptDuels). The server's info says it
takes vrduel ("vrDuel" 1; 0 while g_privateDuel is off).

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "g_local.h"

#define DUEL_CHALLENGE_TIME		10000	// ms a challenge stands (5000 in the original)...
#define DUEL_CHALLENGE_LOCK		5000	// ...the challenger's saber staying as it is for this long of it (as ever)
#define DUEL_RECHALLENGE_TIME	3000	// ms before a challenge to someone else instead
#define DUEL_CHALLENGE_RANGE	512.0f	// how far one challenged by name may be (a look reaches 256)
#define DUEL_BREAK_RANGE		1024.0f	// a duel ends with the two this far apart
#define DUEL_START_TIME			2000	// ms the sabers are out before it starts
#define DUEL_BOT_REACTION		400		// ms before a bot takes a challenge up...
#define DUEL_BOT_REACTION_MORE	700		// ...and up to this much more (bot by bot)

void G_PrivateDuelInit( void ) {
	trap->Cvar_Register( NULL, "vrDuel", "0", CVAR_SERVERINFO | CVAR_ROM );
	G_PrivateDuelPublish();
}

// "vrDuel 1" in the serverinfo: this server takes vrduel <client> (a VR
// salute's challenge); 0: there are no private duels here (g_privateDuel 0).
void G_PrivateDuelPublish( void ) {
	trap->Cvar_Set( "vrDuel", g_privateDuel.integer ? "1" : "0" );
}

// Whether private duels can be had here.
static qboolean G_PrivateDuels( gentity_t *ent ) {
	if ( !g_privateDuel.integer ) {
		return qfalse;
	}
	if ( level.gametype == GT_DUEL || level.gametype == GT_POWERDUEL || level.gametype >= GT_TEAM ) {
		// rather pointless in the duel modes; none in team modes
		if ( ent ) {
			trap->SendServerCommand( ent-g_entities, va( "print \"%s\n\"", G_GetStringEdString( "MP_SVGAME", "NODUEL_GAMETYPE" ) ) );
		}
		return qfalse;
	}
	return qtrue;
}

// Whether a player is free to fight one: in the game and alive, saber in
// hand (not thrown), not in a duel already.
static qboolean G_DuelFree( const gentity_t *ent ) {
	const gclient_t *cl = ent ? ent->client : NULL;

	if ( !ent || !ent->inuse || !cl || cl->pers.connected != CON_CONNECTED ) {
		return qfalse;
	}
	if ( cl->sess.sessionTeam == TEAM_SPECTATOR || ( cl->ps.pm_flags & PMF_FOLLOW ) ) {
		return qfalse;
	}
	if ( ent->health < 1 || cl->ps.stats[STAT_HEALTH] < 1 ) {
		return qfalse;
	}
	return (qboolean)( cl->ps.weapon == WP_SABER && !cl->ps.saberInFlight && !cl->ps.duelInProgress );
}

// The one challenged to a duel... by a look: whoever is right in front of
// them (as ever, a look reaches 256 units)...
static gentity_t *G_DuelLookedAt( gentity_t *ent ) {
	trace_t tr;
	vec3_t forward, fwdOrg;

	AngleVectors( ent->client->ps.viewangles, forward, NULL, NULL );
	fwdOrg[0] = ent->client->ps.origin[0] + forward[0]*256;
	fwdOrg[1] = ent->client->ps.origin[1] + forward[1]*256;
	fwdOrg[2] = ( ent->client->ps.origin[2] + ent->client->ps.viewheight ) + forward[2]*256;
	trap->Trace( &tr, ent->client->ps.origin, NULL, NULL, fwdOrg, ent->s.number, MASK_PLAYERSOLID, qfalse, 0, 0 );
	if ( tr.fraction != 1 && tr.entityNum < MAX_CLIENTS ) {
		return &g_entities[tr.entityNum];
	}
	return NULL;
}

// ...or by name (a VR salute: whom they looked at, with their head), if
// that player is near enough and in sight.
static gentity_t *G_DuelNamed( gentity_t *ent, int clientNum ) {
	gentity_t *other;
	vec3_t eye, otherEye;
	trace_t tr;

	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return NULL;
	}
	other = &g_entities[clientNum];
	if ( other == ent || !other->inuse || !other->client ) {
		return NULL;
	}
	VectorCopy( ent->client->ps.origin, eye );
	eye[2] += ent->client->ps.viewheight;
	VectorCopy( other->client->ps.origin, otherEye );
	otherEye[2] += other->client->ps.viewheight;
	if ( Distance( eye, otherEye ) > DUEL_CHALLENGE_RANGE ) {
		return NULL;
	}
	trap->Trace( &tr, eye, NULL, NULL, otherEye, ent->s.number, MASK_SOLID, qfalse, 0, 0 );
	if ( tr.fraction < 1.0f && tr.entityNum != clientNum ) {
		return NULL;	// (a wall between them)
	}
	return other;
}

// Whether ent's challenge to clientNum stands.
static qboolean G_DuelChallengeStands( const gentity_t *ent, int clientNum ) {
	const gclient_t *cl = ent->client;

	return (qboolean)( cl->vrDuelChallenge && cl->ps.duelIndex == clientNum &&
		level.time - cl->vrDuelChallenge <= DUEL_CHALLENGE_TIME );
}

// The sabers out until it starts.
static void G_DuelHolster( gentity_t *ent ) {
	gclient_t *cl = ent->client;

	if ( cl->ps.saberHolstered ) {
		return;
	}
	if ( cl->saber[0].soundOff ) {
		G_Sound( ent, CHAN_AUTO, cl->saber[0].soundOff );
	}
	if ( cl->saber[1].soundOff && cl->saber[1].model[0] ) {
		G_Sound( ent, CHAN_AUTO, cl->saber[1].soundOff );
	}
	cl->ps.weaponTime = 400;
	cl->ps.saberHolstered = 2;
}

// The challenge gesture, but for VR players (their arms are their own: they
// saluted).
static void G_DuelGesture( gentity_t *ent ) {
	if ( !G_VRSaberClient( ent ) ) {
		ent->client->ps.forceHandExtend = HANDEXTEND_DUELCHALLENGE;
		ent->client->ps.forceHandExtendTime = level.time + 1000;
	}
}

// ent takes up the challenge challenger put to it: the duel's on.
static void G_StartPrivateDuel( gentity_t *ent, gentity_t *challenger ) {
	trap->SendServerCommand( -1, va( "print \"%s %s %s!\n\"", challenger->client->pers.netname,
		G_GetStringEdString( "MP_SVGAME", "PLDUELACCEPT" ), ent->client->pers.netname ) );

	ent->client->ps.duelInProgress = qtrue;
	challenger->client->ps.duelInProgress = qtrue;
	ent->client->ps.duelIndex = challenger->s.number;
	challenger->client->ps.duelIndex = ent->s.number;
	ent->client->ps.duelTime = level.time + DUEL_START_TIME;
	challenger->client->ps.duelTime = level.time + DUEL_START_TIME;
	ent->client->vrDuelChallenge = challenger->client->vrDuelChallenge = 0;	// (both taken up)

	G_AddEvent( ent, EV_PRIVATE_DUEL, 1 );
	G_AddEvent( challenger, EV_PRIVATE_DUEL, 1 );

	// up on their feet for it (g_vrpose.c)
	G_VRPoseStandUp( ent );
	G_VRPoseStandUp( challenger );

	// sabers out, until the duel starts (then they come on to look cool)
	G_DuelHolster( ent );
	G_DuelHolster( challenger );

	G_DuelGesture( ent );
}

// ent challenges challenged.
static void G_ChallengeToDuel( gentity_t *ent, gentity_t *challenged ) {
	// in private: only the duel itself is announced
	trap->SendServerCommand( challenged-g_entities, va( "cp \"%s %s\n\"", ent->client->pers.netname,
		G_GetStringEdString( "MP_SVGAME", "PLDUELCHALLENGE" ) ) );
	trap->SendServerCommand( ent-g_entities, va( "cp \"%s %s\n\"", G_GetStringEdString( "MP_SVGAME", "PLDUELCHALLENGED" ),
		challenged->client->pers.netname ) );

	G_DuelGesture( ent );
	ent->client->ps.duelIndex = challenged->s.number;
	ent->client->ps.duelTime = level.time + DUEL_CHALLENGE_LOCK;
	ent->client->vrDuelChallenge = level.time;
}

// ent challenges challenged to a private duel (NULL: whoever ent is looking
// at) - or, if challenged has challenged ent and it stands, takes it up.
void G_EngageDuel( gentity_t *ent, gentity_t *challenged ) {
	if ( !G_PrivateDuels( ent ) || !G_DuelFree( ent ) ) {
		return;
	}
	if ( !challenged ) {
		challenged = G_DuelLookedAt( ent );
	}
	if ( !challenged || challenged == ent || !G_DuelFree( challenged ) ) {
		return;
	}
	if ( G_DuelChallengeStands( challenged, ent->s.number ) ) {
		G_StartPrivateDuel( ent, challenged );
		return;
	}
	if ( G_DuelChallengeStands( ent, challenged->s.number ) ) {
		return;	// (challenged them already)
	}
	if ( ent->client->vrDuelChallenge && level.time - ent->client->vrDuelChallenge < DUEL_RECHALLENGE_TIME ) {
		return;	// (challenged someone else a moment ago)
	}
	G_ChallengeToDuel( ent, challenged );
}

// "vrduel <client>": a VR player's saber salute at that player (cg_vrduel.c).
void Cmd_VRDuel_f( gentity_t *ent ) {
	char arg[MAX_TOKEN_CHARS];
	gentity_t *challenged;

	if ( trap->Argc() < 2 ) {
		G_EngageDuel( ent, NULL );
		return;
	}
	trap->Argv( 1, arg, sizeof( arg ) );
	if ( !arg[0] || !Q_isanumber( arg ) ) {
		return;
	}
	challenged = G_DuelNamed( ent, atoi( arg ) );
	if ( challenged ) {
		G_EngageDuel( ent, challenged );
	}
}

// What came of a duel: on the two duellists' screens, and in everyone's
// console (with more than one duel at a time, not on everyone's screen).
static void G_DuelResult( gentity_t *a, gentity_t *b, const char *text ) {
	char msg[MAX_STRING_CHARS];

	Q_strncpyz( msg, text, sizeof( msg ) );	// (text: va's, as these are)
	trap->SendServerCommand( a-g_entities, va( "cp \"%s\n\"", msg ) );
	trap->SendServerCommand( b-g_entities, va( "cp \"%s\n\"", msg ) );
	trap->SendServerCommand( -1, va( "print \"%s\n\"", msg ) );
}

// Ends a duellist's duel (their saber theirs again at once, if it ended
// before it started).
static void G_DuelOver( gentity_t *ent ) {
	ent->client->ps.duelInProgress = 0;
	ent->client->ps.duelTime = 0;
	G_AddEvent( ent, EV_PRIVATE_DUEL, 0 );
}

// Ends the duel of these two.
static void G_EndPrivateDuel( gentity_t *ent, gentity_t *duelAgainst ) {
	G_DuelOver( ent );
	G_DuelOver( duelAgainst );
}

// Lights a duellist's sabers when the duel starts.
static void G_DuelUnholster( gentity_t *ent ) {
	gclient_t *cl = ent->client;

	if ( cl->ps.weapon != WP_SABER || !cl->ps.saberHolstered || !cl->ps.duelTime ) {
		return;
	}
	cl->ps.saberHolstered = 0;
	if ( cl->saber[0].soundOn ) {
		G_Sound( ent, CHAN_AUTO, cl->saber[0].soundOn );
	}
	if ( cl->saber[1].soundOn ) {
		G_Sound( ent, CHAN_AUTO, cl->saber[1].soundOn );
	}
	G_AddEvent( ent, EV_PRIVATE_DUEL, 2 );
	cl->ps.duelTime = 0;
}

// Every frame (ClientThink) for a player in a private duel: until it starts
// they stand still with their sabers out; then the sabers come on; it ends
// when one of them is down (the winner gets their health back) or they're
// too far apart.
void G_PrivateDuelThink( gentity_t *ent, usercmd_t *ucmd ) {
	gclient_t *client = ent->client;
	gentity_t *duelAgainst;

	if ( !client->ps.duelInProgress ) {
		return;
	}
	duelAgainst = &g_entities[client->ps.duelIndex];

	if ( client->ps.duelTime < level.time ) {
		// bring out the sabers
		G_DuelUnholster( ent );
		if ( duelAgainst && duelAgainst->client && duelAgainst->inuse ) {
			G_DuelUnholster( duelAgainst );
		}
	} else {
		client->ps.speed = 0;
		client->ps.basespeed = 0;
		ucmd->forwardmove = 0;
		ucmd->rightmove = 0;
		ucmd->upmove = 0;
	}

	if ( !duelAgainst || !duelAgainst->client || !duelAgainst->inuse || duelAgainst->client->ps.duelIndex != ent->s.number ) {
		G_DuelOver( ent );	// (they've gone)
	} else if ( duelAgainst->health < 1 || duelAgainst->client->ps.stats[STAT_HEALTH] < 1 ) {
		G_EndPrivateDuel( ent, duelAgainst );

		// the winner gets full health, providing he's still alive
		if ( ent->health > 0 && client->ps.stats[STAT_HEALTH] > 0 ) {
			if ( ent->health < client->ps.stats[STAT_MAX_HEALTH] ) {
				client->ps.stats[STAT_HEALTH] = ent->health = client->ps.stats[STAT_MAX_HEALTH];
			}
			if ( g_spawnInvulnerability.integer ) {
				client->ps.eFlags |= EF_INVULNERABLE;
				client->invulnerableTimer = level.time + g_spawnInvulnerability.integer;
			}
			G_DuelResult( ent, duelAgainst, va( "%s %s %s!", client->pers.netname,
				G_GetStringEdString( "MP_SVGAME", "PLDUELWINNER" ), duelAgainst->client->pers.netname ) );
		} else {
			// a draw: both went down in the same frame
			G_DuelResult( ent, duelAgainst, va( "%s, %s: %s", client->pers.netname, duelAgainst->client->pers.netname,
				G_GetStringEdString( "MP_SVGAME", "PLDUELTIE" ) ) );
		}
	} else if ( Distance( client->ps.origin, duelAgainst->client->ps.origin ) >= DUEL_BREAK_RANGE ) {
		G_EndPrivateDuel( ent, duelAgainst );

		// (theirs alone to know)
		trap->SendServerCommand( ent-g_entities, va( "print \"%s\n\"", G_GetStringEdString( "MP_SVGAME", "PLDUELSTOP" ) ) );
		trap->SendServerCommand( duelAgainst-g_entities, va( "print \"%s\n\"", G_GetStringEdString( "MP_SVGAME", "PLDUELSTOP" ) ) );
	}
}

// A bot challenged to a private duel takes it up a moment later (it sees
// it), when both still can and they're near enough (it would end at once
// otherwise). Returns whom it's duelling, if it took one up.
gentity_t *G_BotTakeUpDuel( gentity_t *bot ) {
	static int answerTime[MAX_CLIENTS], answerTo[MAX_CLIENTS];
	const int b = bot - g_entities;
	gentity_t *challenger = NULL;
	int i;

	if ( b < 0 || b >= MAX_CLIENTS ) {
		return NULL;
	}
	if ( bot_acceptDuels.integer && G_PrivateDuels( NULL ) && G_DuelFree( bot ) ) {
		for ( i = 0; i < MAX_CLIENTS && !challenger; i++ ) {
			gentity_t *c = &g_entities[i];
			if ( c != bot && c->inuse && c->client && !c->client->ps.duelInProgress && G_DuelChallengeStands( c, b ) ) {
				challenger = c;
			}
		}
	}
	if ( !challenger ) {
		answerTime[b] = 0;
		return NULL;
	}
	if ( !answerTime[b] || answerTo[b] != challenger->s.number ) {
		answerTime[b] = level.time + DUEL_BOT_REACTION + ( b * 151 ) % DUEL_BOT_REACTION_MORE;
		answerTo[b] = challenger->s.number;
		return NULL;
	}
	if ( level.time < answerTime[b] ) {
		return NULL;
	}
	answerTime[b] = 0;
	if ( !G_DuelFree( challenger ) || Distance( bot->client->ps.origin, challenger->client->ps.origin ) >= DUEL_BREAK_RANGE * 0.75f ) {
		return NULL;
	}
	G_StartPrivateDuel( bot, challenger );
	return challenger;
}
