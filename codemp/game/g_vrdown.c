/*
===========================================================================
OpenJK VR (multiplayer) - roleplay, stage 2: down, not dead; and NPCs
taking sides.

DOWN. With roleplay on and g_rpDowned 1 (Free For All and Team FFA), a
player playing a character whose health runs out goes down where they
stand instead of dying: flat on their back (the pose VRPOSE_DOWNED,
bg_vrpose.c, which keeps them still in their own game's prediction too),
able to talk but not to move, fight, use things or the Force. Nothing
harms them there - there's no finishing blow - bar a map's own hazards
(pits, lava, slime, water, crushers, kill zones, its lasers), which kill
as ever. There's no timer: they stay down until
  - someone of their faction gets them up: looks at them, near enough, and
    holds use (in the headset, the right grip) for RP_HELP_TIME; they get
    up with RP_UP_HEALTH health;
  - they give up: hold attack for RP_GIVEUP_TIME (a press after going down:
    one held as they went down doesn't count), Give up on the wheel, or
    "rp giveup" - and die, as dying is now (the death is whoever put them
    down's, and its way: a kill like any other), to respawn as ever;
  - it no longer applies to them - roleplay or g_rpDowned off, or they no
    longer play a character: they get up.
Bots, players not playing a character and anyone riding something die as
ever. A private duel's loser goes down instead (g_vrduel.c).

NPCS. Each NPC is on a side (playerTeam): the light side (NPCTEAM_PLAYER:
Jedi, Rebels), the dark side (NPCTEAM_ENEMY: Sith, Reborn, Imperials,
mercenaries), neutral (droids, civilians); monsters (NPCTEAM_FREE) have
none. With roleplay on, a player playing a character is on one too, by
faction - Heroes the light side's, Villains the dark side's, Neutral the
neutrals' - and NPCs go by that (G_RPNPCEnemy, asked by the NPC code
wherever it takes or keeps an enemy):
                   Heroes          Villains        Neutral
  light side       beside you      attack you      leave you be
  dark side        attack you      beside you      leave you be
  neutral          leave you be    leave you be    leave you be
Attacking an NPC whose side was leaving you be makes that side fight back
for the rest of that life (rpProvoked). Nobody's NPCs harm someone who's
down. Monsters, bots, and players not playing a character are left as ever.

The players' games (cg_vrrp.c) are told, if they run this build:
  rpdn <client> <faction>       client is down (faction: theirs), or up (-1)
  rpup <helper> <client> <time> helper is getting client up since time; 0:
                                stopped; -1: got them up
They know who they are themselves from their own player state.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "g_local.h"
#include "b_local.h"

extern char *modNames[MOD_MAX];	// (g_combat.c)

#define RP_HELP_TIME	3000	// ms holding use on someone down gets them up...
#define RP_UP_HEALTH	25		// ...with this much health
#define RP_GIVEUP_TIME	1500	// ms holding attack, down, gives up
#define RP_HELP_REACH	128.0f	// a helper's eyes are at most this far from the middle of someone down...
#define RP_HELP_WIDE	40.0f	// ...looking this close to it
#define RP_SEE_DOWN		12.0f	// the middle of someone down: this far under where they are (on the floor)

static int rpDownTold[MAX_CLIENTS];	// what everyone was last told of each client: down (their faction) or up (-1)
static qboolean rpDownInit;

/*
================================================================================
Small things
================================================================================
*/

static const animation_t *G_RP_Anims( const gentity_t *ent ) {
	return bgAllAnims[ent->localAnimIndex].anims;
}

static qboolean G_RP_InGame( const gentity_t *ent ) {
	return (qboolean)( ent && ent->inuse && ent->client && ent->client->pers.connected == CON_CONNECTED &&
		ent->client->sess.sessionTeam != TEAM_SPECTATOR && !( ent->client->ps.pm_flags & PMF_FOLLOW ) );
}

// A command for a player's game, if it runs this build.
static void G_RP_Tell( int clientNum, const char *text ) {
	if ( clientNum >= 0 && clientNum < MAX_CLIENTS && g_entities[clientNum].inuse && g_entities[clientNum].client &&
		g_entities[clientNum].client->pers.connected == CON_CONNECTED && G_RPCapable( clientNum ) ) {
		trap->SendServerCommand( clientNum, text );
	}
}

// Whether going down is how it goes here now.
qboolean G_RPDownOn( void ) {
	return (qboolean)( G_RPOn() && g_rpDowned.integer && ( level.gametype == GT_FFA || level.gametype == GT_TEAM ) );
}

qboolean G_RPDowned( const gentity_t *ent ) {
	return (qboolean)( ent && ent->client && ent->s.number >= 0 && ent->s.number < MAX_CLIENTS && ent->client->rpDowned );
}

// A map's own hazards kill as ever, down or not: somewhere no hand reaches.
static qboolean G_RP_Hazard( const gentity_t *targ, int mod ) {
	switch ( mod ) {
	case MOD_WATER:
	case MOD_SLIME:
	case MOD_LAVA:
	case MOD_CRUSH:
	case MOD_TELEFRAG:
	case MOD_TRIGGER_HURT:
	case MOD_TARGET_LASER:
	case MOD_SUICIDE:
	case MOD_TEAM_CHANGE:
		return qtrue;
	case MOD_FALLING:
		return (qboolean)( targ->client && targ->client->ps.fallingToDeath );	// (a pit: a fall that just hurts downs you)
	default:
		return qfalse;
	}
}

// Whether a player whose health is running out goes down (rather than dying).
static qboolean G_RP_GoesDown( const gentity_t *targ, int mod ) {
	const gclient_t *cl = targ->client;

	if ( !cl || targ->s.number < 0 || targ->s.number >= MAX_CLIENTS || !G_RPDownOn() || level.intermissiontime ) {
		return qfalse;
	}
	if ( ( targ->r.svFlags & SVF_BOT ) || G_RPFaction( targ->s.number ) < 0 ) {
		return qfalse;	// (bots as ever; and no faction, no one to get them up)
	}
	if ( cl->sess.sessionTeam == TEAM_SPECTATOR || cl->ps.m_iVehicleNum || ( cl->ps.eFlags2 & EF2_HELD_BY_MONSTER ) ) {
		return qfalse;	// (riding, or in a monster's jaws: as ever)
	}
	return (qboolean)!G_RP_Hazard( targ, mod );
}

// Whether someone down stays down (qfalse: it no longer applies to them).
static qboolean G_RP_StaysDown( const gentity_t *ent ) {
	return (qboolean)( G_RPDownOn() && G_RP_InGame( ent ) && ent->health > 0 && G_RPFaction( ent->s.number ) >= 0 );
}

// The sabers off (going down; and kept off, down).
static void G_RP_SabersAway( gentity_t *ent, qboolean quiet ) {
	gclient_t *cl = ent->client;

	if ( cl->ps.weapon != WP_SABER || cl->ps.saberHolstered >= 2 ) {
		return;
	}
	if ( !quiet ) {
		if ( cl->ps.saberHolstered == 1 && cl->saber[1].model[0] ) {
			G_Sound( ent, CHAN_WEAPON, cl->saber[1].soundOff );
		} else if ( cl->ps.saberHolstered == 0 && cl->saber[0].soundOff ) {
			G_Sound( ent, CHAN_WEAPON, cl->saber[0].soundOff );
		}
	}
	cl->ps.saberHolstered = 2;
}

// Who put ent down, for dying of it: still that one (a player still in
// the game since, an NPC still there), or the world.
static gentity_t *G_RP_DownedBy( gentity_t *ent ) {
	const gclient_t *cl = ent->client;
	gentity_t *by;

	if ( cl->rpDownBy < 0 || cl->rpDownBy >= ENTITYNUM_MAX_NORMAL ) {
		return &g_entities[ENTITYNUM_WORLD];
	}
	by = &g_entities[cl->rpDownBy];
	if ( by == ent ) {
		return ent;
	}
	if ( cl->rpDownBy < MAX_CLIENTS ) {
		if ( !by->inuse || !by->client || by->client->pers.connected != CON_CONNECTED || by->client->pers.enterTime != cl->rpDownByTime ) {
			return &g_entities[ENTITYNUM_WORLD];
		}
		return by;
	}
	return ( by->inuse && by->client && by->s.eType == ET_NPC ) ? by : &g_entities[ENTITYNUM_WORLD];
}

static const char *G_RP_Who( const gentity_t *ent ) {
	if ( ent && ent->client ) {
		return ent->client->pers.netname;
	}
	if ( ent && ent->s.number != ENTITYNUM_WORLD && ent->classname ) {
		return ent->classname;
	}
	return "the world";
}

/*
================================================================================
Going down, getting up, giving up
================================================================================
*/

// G_Damage, before anything's done: someone down takes no more harm, bar
// a map's hazards.
qboolean G_RPDownGuard( gentity_t *targ, int mod ) {
	return (qboolean)( G_RPDowned( targ ) && !G_RP_Hazard( targ, mod ) );
}

// G_Damage, a player's health run out: down instead of dying? (qtrue: down)
qboolean G_RPDown( gentity_t *targ, gentity_t *inflictor, gentity_t *attacker, int damage, int mod ) {
	gclient_t *cl = targ->client;
	int i;

	if ( G_RPDowned( targ ) || !G_RP_GoesDown( targ, mod ) ) {
		return qfalse;	// (down already, what still reaches them kills: a hazard; player_die says they're up)
	}
	cl->rpDowned = qtrue;
	cl->rpDownTime = level.time;
	cl->rpDownBy = attacker ? attacker->s.number : ENTITYNUM_WORLD;
	cl->rpDownByTime = ( attacker && attacker->client && attacker->s.number < MAX_CLIENTS ) ? attacker->client->pers.enterTime : 0;
	cl->rpDownMod = mod;
	cl->rpDownDamage = damage;
	cl->rpGiveUpFrom = 0;
	cl->rpAttackFree = qfalse;
	targ->health = cl->ps.stats[STAT_HEALTH] = 1;

	BG_VRPoseDown( &cl->ps, (animation_t *)G_RP_Anims( targ ) );
	cl->vrPoseYaw = cl->ps.viewangles[YAW];	// (the way they went down: g_vrpose.c)
	G_RP_SabersAway( targ, qfalse );
	if ( cl->jetPackOn ) {
		Jetpack_Off( targ );
	}
	for ( i = 0; i < NUM_FORCE_POWERS; i++ ) {
		if ( cl->ps.fd.forcePowersActive & ( 1 << i ) ) {
			WP_ForcePowerStop( targ, (forcePowers_t)i );
		}
	}
	G_LogPrintf( "Roleplay: %s" S_COLOR_WHITE " is down (%s" S_COLOR_WHITE ", %s)\n", cl->pers.netname,
		G_RP_Who( attacker ), ( mod >= 0 && mod < MOD_MAX ) ? modNames[mod] : "?" );
	return qtrue;
}

// helper stops getting whom they were getting up (done: got them up)
static void G_RP_HelpOver( gentity_t *helper, qboolean done ) {
	gclient_t *cl = helper->client;
	const int who = cl->rpHelping - 1;
	char text[64];

	if ( !cl->rpHelping ) {
		return;
	}
	cl->rpHelping = 0;
	Com_sprintf( text, sizeof( text ), "rpup %i %i %i", helper->s.number, who, done ? -1 : 0 );
	G_RP_Tell( helper->s.number, text );
	G_RP_Tell( who, text );
}

// ent gets up (helper: who got them up; NULL: it no longer applies to them).
static void G_RP_GetUp( gentity_t *ent, gentity_t *helper ) {
	gclient_t *cl = ent->client;
	const int most = cl->ps.stats[STAT_MAX_HEALTH] > 0 ? cl->ps.stats[STAT_MAX_HEALTH] : 100;

	cl->rpDowned = qfalse;
	cl->rpGiveUpFrom = 0;
	cl->rpAttackFree = qfalse;
	if ( ent->health < RP_UP_HEALTH ) {
		ent->health = Q_min( RP_UP_HEALTH, most );
	}
	cl->ps.stats[STAT_HEALTH] = ent->health;
	BG_VRPoseStandUp( &cl->ps, (animation_t *)G_RP_Anims( ent ) );	// (the game's getting up off the floor)
	if ( helper ) {
		G_LogPrintf( "Roleplay: %s" S_COLOR_WHITE " got %s" S_COLOR_WHITE " up\n", helper->client->pers.netname, cl->pers.netname );
	} else {
		G_LogPrintf( "Roleplay: %s" S_COLOR_WHITE " got up (no longer down here)\n", cl->pers.netname );
	}
}

// ent, down, gives up: they die of it, as whoever put them down's.
void G_RPGiveUp( gentity_t *ent ) {
	gclient_t *cl = ent->client;
	gentity_t *by;
	int mod, damage;

	if ( !G_RPDowned( ent ) || level.intermissiontime ) {
		return;	// (in the intermission, nobody dies: player_die)
	}
	by = G_RP_DownedBy( ent );
	mod = cl->rpDownMod;
	damage = cl->rpDownDamage;
	G_LogPrintf( "Roleplay: %s" S_COLOR_WHITE " gave up\n", cl->pers.netname );
	ent->health = cl->ps.stats[STAT_HEALTH] = 0;
	ent->enemy = by;
	player_die( ent, by, by, damage, mod );	// (player_die: no longer down)
}

// player_die: someone down dies after all (a hazard, giving up, the kill
// command, going to spectate) - down no more; and anyone they were getting
// up isn't being got up.
void G_RPDownDied( gentity_t *self ) {
	gclient_t *cl = self->client;

	if ( !cl || self->s.number < 0 || self->s.number >= MAX_CLIENTS ) {
		return;
	}
	G_RP_HelpOver( self, qfalse );
	if ( cl->rpDowned ) {
		cl->rpDowned = qfalse;
		cl->rpGiveUpFrom = 0;
		cl->rpAttackFree = qfalse;
		BG_VRPoseSet( &cl->ps, VRPOSE_NONE );	// (the body as it lies: the game's own death from there)
	}
}

/*
================================================================================
Every command: down, or giving someone a hand
================================================================================
*/

// The middle of someone down: on the floor where they lie.
static void G_RP_DownMiddle( const gentity_t *ent, vec3_t mid ) {
	VectorCopy( ent->client->ps.origin, mid );
	mid[2] -= RP_SEE_DOWN;
}

// Whether ent looks at other (down) near enough to give them a hand, and
// how far off the middle (squared).
static qboolean G_RP_LooksAt( const gentity_t *ent, const gentity_t *other, float *off ) {
	vec3_t eye, dir, mid, d;
	trace_t tr;
	float along, dist2;

	VectorCopy( ent->client->ps.origin, eye );
	eye[2] += ent->client->ps.viewheight;
	AngleVectors( ent->client->ps.viewangles, dir, NULL, NULL );
	G_RP_DownMiddle( other, mid );
	VectorSubtract( mid, eye, d );
	dist2 = VectorLengthSquared( d );
	along = DotProduct( d, dir );
	if ( along <= 0.0f || dist2 > RP_HELP_REACH * RP_HELP_REACH ) {
		return qfalse;
	}
	*off = dist2 - along * along;
	if ( *off > RP_HELP_WIDE * RP_HELP_WIDE ) {
		return qfalse;
	}
	trap->Trace( &tr, eye, NULL, NULL, mid, ent->s.number, MASK_SOLID, qfalse, 0, 0 );
	return (qboolean)( tr.fraction >= 1.0f || tr.entityNum == other->s.number );	// (no wall between)
}

// Whether ent can give someone down a hand now.
static qboolean G_RP_CanHelp( const gentity_t *ent ) {
	const gclient_t *cl = ent->client;

	return (qboolean)( G_RPDownOn() && G_RP_InGame( ent ) && ent->health > 0 && cl->ps.pm_type != PM_DEAD &&
		!cl->rpDowned && !cl->ps.duelInProgress && !cl->ps.m_iVehicleNum && G_RPFaction( ent->s.number ) >= 0 );
}

// Whom ent, holding use, is getting up: someone down, of their faction,
// they look at (the nearest their look; -1: nobody).
static int G_RP_HelpWhom( const gentity_t *ent ) {
	const int faction = G_RPFaction( ent->s.number );
	float best = 0.0f, off;
	int i, who = -1;

	for ( i = 0; i < level.maxclients; i++ ) {
		const gentity_t *other = &g_entities[i];
		if ( other == ent || !G_RPDowned( other ) || !G_RP_InGame( other ) || G_RPFaction( i ) != faction ) {
			continue;
		}
		if ( G_RP_LooksAt( ent, other, &off ) && ( who < 0 || off < best ) ) {
			best = off;
			who = i;
		}
	}
	return who;
}

static void G_RP_Help( gentity_t *ent, const usercmd_t *ucmd ) {
	gclient_t *cl = ent->client;
	const int who = ( ( ucmd->buttons & BUTTON_USE ) && G_RP_CanHelp( ent ) ) ? G_RP_HelpWhom( ent ) : -1;

	if ( who + 1 != cl->rpHelping ) {
		G_RP_HelpOver( ent, qfalse );
		if ( who >= 0 ) {
			char text[64];
			cl->rpHelping = who + 1;
			cl->rpHelpFrom = level.time;
			Com_sprintf( text, sizeof( text ), "rpup %i %i %i", ent->s.number, who, level.time );
			G_RP_Tell( ent->s.number, text );
			G_RP_Tell( who, text );
		}
		return;
	}
	if ( who >= 0 && level.time - cl->rpHelpFrom >= RP_HELP_TIME ) {
		G_RP_HelpOver( ent, qtrue );
		G_RP_GetUp( &g_entities[who], ent );
	}
}

// Down: giving up (attack held), and nothing else they press does anything
// (the move keeps them still: bg_vrpose.c).
static void G_RP_DownCommand( gentity_t *ent, usercmd_t *ucmd ) {
	gclient_t *cl = ent->client;

	if ( !( ucmd->buttons & BUTTON_ATTACK ) ) {
		cl->rpAttackFree = qtrue;
		cl->rpGiveUpFrom = 0;
	} else if ( cl->rpAttackFree ) {
		if ( !cl->rpGiveUpFrom ) {
			cl->rpGiveUpFrom = level.time;
		} else if ( level.time - cl->rpGiveUpFrom >= RP_GIVEUP_TIME ) {
			G_RPGiveUp( ent );
			return;
		}
	}
	ucmd->buttons &= ~( BUTTON_ATTACK | BUTTON_ALT_ATTACK | BUTTON_USE | BUTTON_USE_HOLDABLE | BUTTON_GESTURE |
		BUTTON_FORCEPOWER | BUTTON_FORCEGRIP | BUTTON_FORCE_LIGHTNING | BUTTON_FORCE_DRAIN );
	ucmd->forwardmove = ucmd->rightmove = ucmd->upmove = 0;
	ucmd->generic_cmd = 0;
	G_RP_SabersAway( ent, qtrue );
}

// ClientThink, a player's command, before the move.
void G_RPDownThink( gentity_t *ent, usercmd_t *ucmd ) {
	if ( !ent->client || ent->s.number < 0 || ent->s.number >= MAX_CLIENTS ) {
		return;
	}
	if ( ent->client->rpDowned ) {
		G_RP_HelpOver( ent, qfalse );	// (a hand they were giving: gone with them)
		if ( G_RP_StaysDown( ent ) ) {
			G_RP_DownCommand( ent, ucmd );
		}	// (if not, G_RPDownFrame gets them up)
	} else {
		G_RP_Help( ent, ucmd );
	}
}

// "rp giveup" (the wheel's Give up)
void G_RPGiveUpCommand( gentity_t *ent ) {
	if ( !G_RPDowned( ent ) ) {
		trap->SendServerCommand( ent - g_entities, "print \"You're not down\n\"" );
		return;
	}
	G_RPGiveUp( ent );
}

/*
================================================================================
NPCs taking sides
================================================================================
*/

// The NPCs' side a faction is on.
static int G_RP_Side( int faction ) {
	switch ( faction ) {
	case RPF_HEROES:	return NPCTEAM_PLAYER;
	case RPF_VILLAINS:	return NPCTEAM_ENEMY;
	default:			return NPCTEAM_NEUTRAL;
	}
}

// Whether npc may have ent as its enemy, as roleplay has it: 1 yes, 0 no,
// -1 roleplay doesn't say (as ever).
int G_RPNPCEnemy( const gentity_t *npc, const gentity_t *ent ) {
	int faction, side;

	if ( !npc || !npc->client || !ent || !ent->client || ent->s.number < 0 || ent->s.number >= MAX_CLIENTS || !G_RPOn() ) {
		return -1;
	}
	if ( ent->client->rpDowned ) {
		return 0;	// (nobody harms someone down)
	}
	faction = G_RPFaction( ent->s.number );
	if ( faction < 0 || ( ent->r.svFlags & SVF_BOT ) || level.gametype == GT_SIEGE ) {
		return -1;
	}
	side = npc->client->playerTeam;
	if ( side <= NPCTEAM_FREE || side >= NPCTEAM_NUM_TEAMS ) {
		return -1;	// (monsters: no side)
	}
	if ( ent->client->rpProvoked & ( 1 << side ) ) {
		return 1;	// (attacked: they fight back)
	}
	switch ( faction ) {
	case RPF_HEROES:	return ( side == NPCTEAM_ENEMY ) ? 1 : 0;
	case RPF_VILLAINS:	return ( side == NPCTEAM_PLAYER ) ? 1 : 0;
	default:			return 0;
	}
}

// G_Damage: attacker hurts an NPC whose side was leaving them be (not
// their own side's: that's forgiven, as ever) - its side fights back.
void G_RPProvoke( gentity_t *attacker, gentity_t *targ ) {
	int faction, side;

	if ( !attacker || !attacker->client || attacker->s.number < 0 || attacker->s.number >= MAX_CLIENTS ||
		!targ || targ == attacker || !targ->NPC || !targ->client || !G_RPOn() ) {
		return;
	}
	faction = G_RPFaction( attacker->s.number );
	side = targ->client->playerTeam;
	if ( faction < 0 || side <= NPCTEAM_FREE || side >= NPCTEAM_NUM_TEAMS || side == G_RP_Side( faction ) ) {
		return;
	}
	attacker->client->rpProvoked |= 1 << side;
}

// A player's side for the NPCs: by their faction, or as ever.
static void G_RP_Sides( gentity_t *ent ) {
	gclient_t *cl = ent->client;
	const int faction = ( level.gametype == GT_SIEGE || ( ent->r.svFlags & SVF_BOT ) ) ? -1 : G_RPFaction( ent->s.number );

	if ( level.gametype == GT_SIEGE ) {
		return;	// (by team: ClientSpawn)
	}
	if ( faction < 0 ) {
		cl->playerTeam = NPCTEAM_PLAYER;	// (as ClientSpawn has it)
		cl->enemyTeam = NPCTEAM_ENEMY;
	} else {
		cl->playerTeam = (npcteam_t)G_RP_Side( faction );
		cl->enemyTeam = ( faction == RPF_HEROES ) ? NPCTEAM_ENEMY : ( faction == RPF_VILLAINS ) ? NPCTEAM_PLAYER : NPCTEAM_NEUTRAL;
	}
}

/*
================================================================================
Every frame
================================================================================
*/

// G_RunFrame, before the players' end of frame: sides, who no longer stays
// down, and everyone told who's down.
void G_RPDownFrame( void ) {
	int i, j;

	if ( !rpDownInit ) {
		for ( i = 0; i < MAX_CLIENTS; i++ ) {
			rpDownTold[i] = -1;
		}
		rpDownInit = qtrue;
	}
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		gentity_t *ent = &g_entities[i];
		int now = -1;

		if ( i < level.maxclients && ent->inuse && ent->client && ent->client->pers.connected == CON_CONNECTED ) {
			G_RP_Sides( ent );
			if ( ent->client->rpDowned && !level.intermissiontime && !G_RP_StaysDown( ent ) ) {
				G_RP_GetUp( ent, NULL );
			}
			if ( ent->client->rpDowned ) {
				now = G_RPFaction( i );
				if ( now < 0 ) {
					now = ( rpDownTold[i] >= 0 ) ? rpDownTold[i] : RPF_NEUTRAL;	// (in the intermission, roleplay gone: as they were)
				}
			}
		}
		if ( now != rpDownTold[i] ) {
			rpDownTold[i] = now;
			for ( j = 0; j < level.maxclients; j++ ) {
				G_RP_Tell( j, va( "rpdn %i %i", i, now ) );
			}
		}
	}
}

// A player's game says it runs this build: who's down.
void G_RPDownHello( int clientNum ) {
	int i;

	if ( !rpDownInit ) {
		return;
	}
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		if ( rpDownTold[i] >= 0 ) {
			G_RP_Tell( clientNum, va( "rpdn %i %i", i, rpDownTold[i] ) );
		}
	}
}
