/*
===========================================================================
OpenJK VR (multiplayer) - roleplay, stage 2: down, not dead; and NPCs
taking sides.

DOWN. With roleplay on and g_rpDowned 1 (Free For All and Team FFA), a
player playing a character whose health runs out goes down where they
stand instead of dying: flat on their back (the pose VRPOSE_DOWNED,
bg_vrpose.c, which keeps them still in their own game's prediction too),
able to talk but not to move, fight, switch weapons, use things or the
Force (a charge held - a thermal cooking - is let go of unfired). Nothing
harms them there - there's no finishing blow, no shove, no seeker of theirs
fights on - bar a map's own hazards, which kill as ever: a pit, its water,
lava and slime, its doors, lifts and crushers, kill zones, lasers, fire and
lightning, its breakables blowing up, a spawn or teleporter onto them
(G_RP_Hazard: by what does it and whose - a rancor's foot, a walker's, a
flamethrower, a thrown crate, a turret's or a wreck's blast aren't the
map's). Down, they bear RP_DOWN_HAZARD of a hazard first - a door's bump, a
breath of water - and what comes past that kills. There's no timer: they
stay down until
  - someone of their faction gets them up: looks at them (aims at them, or
    their headset looks: G_VRHeadGaze), near enough, and holds use (in the
    headset, the right grip) for RP_HELP_TIME; they get up with
    RP_UP_HEALTH health;
  - they give up: hold attack for RP_GIVEUP_TIME (a press after going down:
    one held as they went down doesn't count), Give up on the wheel, "rp
    giveup" or /kill - and die, as dying is now (the death is whoever put
    them down's, and its way: a kill like any other - a push off a ledge
    the pusher's), to respawn as ever; their character gone from under
    them (their account logged in somewhere else) is giving up too;
  - it no longer applies to them - roleplay or g_rpDowned off: they get up.
Bots, players not playing a character (or whose game doesn't run this
build: it would see itself standing), anyone riding something, at a gun or
in a monster's jaws die as ever, and a body stays dead. A private duel's
loser goes down instead (g_vrduel.c). Down, nothing of the character
changes: no logging in or out, no switching, deleting or changing faction;
going to spectate or another team is giving up first.

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

The players' games (cg_vrdown.c) are told, if they run this build:
  rpdn <client> <faction>       client is down (faction: theirs), or up (-1)
  rpup <helper> <client> <time> helper is getting client up since time; 0:
                                stopped; -1: got them up
(both as things change, once a frame; rpup to the two of them, at most
every RP_HELP_TELL_GAP). They know they're down themselves from their own
player state.

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
#define RP_HELP_TELL_GAP	250	// ms: what a player's hand is doing is told them at most this often (use pressed again and again)
#define RP_DOWN_HAZARD	25		// what a map's hazards take of someone down before they kill (a door's bump, a breath of water: borne)

static int rpDownTold[MAX_CLIENTS];	// what everyone was last told of each client: down (their faction) or up (-1)
static int rpHelpTold[MAX_CLIENTS];	// whom each player, and they, were last told the player's getting up (-1: nobody)...
static int rpHelpToldFrom[MAX_CLIENTS];	// ...since (level.time)
static int rpHelpToldAt[MAX_CLIENTS];	// level.time either was last told anything of it
static int rpHelpDone[MAX_CLIENTS];	// whom each player just got up (-1: nobody), for the telling
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

// A command for a player's game, if it runs this build (it said so, in
// the game: g_vrrp.c - and is no longer, gone).
static void G_RP_Tell( int clientNum, const char *text ) {
	if ( G_RPCapable( clientNum ) ) {
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
// By what does it and whose (G_Damage's inflictor and attacker - nobody's:
// the world's - and how): what a player or an NPC does - a rancor's foot, a
// walker's, a flamethrower, a crate thrown, a turret's shot - and a wreck's
// blast aren't the map's.
static qboolean G_RP_Hazard( const gentity_t *targ, const gentity_t *inflictor, const gentity_t *attacker, int dflags, int mod ) {
	const qboolean world = (qboolean)( !attacker || attacker == targ || attacker->s.number == ENTITYNUM_WORLD );

	switch ( mod ) {
	case MOD_TELEFRAG:		// (two can't stand in one place: a spawn, a teleporter)
	case MOD_TRIGGER_HURT:	// (a map's kill zone, whoever set it off)
	case MOD_TEAM_CHANGE:
		return qtrue;
	case MOD_TARGET_LASER:	// (a map's laser, whoever switched it on; not a turret's shot)
		return (qboolean)( inflictor && !Q_stricmp( inflictor->classname, "target_laser" ) );
	case MOD_WATER:
	case MOD_SLIME:
	case MOD_LAVA:
		return world;		// (under water, in a pool of it; not a flamethrower)
	case MOD_CRUSH:
		return (qboolean)( world || attacker->s.eType == ET_MOVER );	// (a door, a lift, a crusher; not a foot, nor a crate)
	case MOD_SUICIDE:		// (a map's lightning; not a wreck's blast - nobody's, all round it)
		return (qboolean)( ( world && !( dflags & DAMAGE_RADIUS ) ) || ( attacker && !Q_stricmpn( attacker->classname, "trigger_", 8 ) ) );
	case MOD_UNKNOWN:		// (a map's fire, its breakables blowing up; not someone's)
		return (qboolean)( world || !attacker->client );
	case MOD_FALLING:
		return (qboolean)( targ->client && targ->client->ps.fallingToDeath );	// (a pit: a fall that just hurts downs you)
	default:
		return qfalse;
	}
}

// Whether a player whose health is running out goes down (rather than dying).
static qboolean G_RP_GoesDown( const gentity_t *targ, const gentity_t *inflictor, const gentity_t *attacker, int dflags, int mod ) {
	const gclient_t *cl = targ->client;

	if ( !cl || targ->s.number < 0 || targ->s.number >= MAX_CLIENTS || !G_RPDownOn() || level.intermissiontime ) {
		return qfalse;
	}
	if ( cl->ps.pm_type == PM_DEAD ) {
		return qfalse;	// (a body - shot at, kicked: dead already)
	}
	if ( ( targ->r.svFlags & SVF_BOT ) || G_RPFaction( targ->s.number ) < 0 || !G_RPCapable( targ->s.number ) ) {
		return qfalse;	// (bots as ever; no faction, no one to get them up; a game that doesn't know down would see itself standing)
	}
	if ( cl->sess.sessionTeam == TEAM_SPECTATOR || cl->ps.m_iVehicleNum || cl->ps.emplacedIndex ||
		( cl->ps.eFlags2 & EF2_HELD_BY_MONSTER ) ) {
		return qfalse;	// (riding, at a gun, or in a monster's jaws: as ever)
	}
	return (qboolean)!G_RP_Hazard( targ, inflictor, attacker, dflags, mod );
}

// Whether someone down stays down (qfalse: it no longer applies to them).
static qboolean G_RP_StaysDown( const gentity_t *ent ) {
	return (qboolean)( G_RPDownOn() && G_RP_InGame( ent ) && G_RPFaction( ent->s.number ) >= 0 );
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

// Whom a death now would be credited to, as player_die works it out: a map
// turret's to whoever switched it on; a fall (not a pit), a crush (not a
// door's)... the world's to whoever pushed them into it lately. (What else
// player_die credits so - a kill zone, a map's own - are hazards: no one
// goes down of them.)
static gentity_t *G_RP_Credit( gentity_t *targ, gentity_t *inflictor, gentity_t *attacker, int mod ) {
	const gclient_t *cl = targ->client;

	if ( !attacker ) {
		attacker = &g_entities[ENTITYNUM_WORLD];
	}
	if ( inflictor && inflictor->activator && !inflictor->client && !attacker->client && inflictor->activator->client &&
		inflictor->activator->inuse && inflictor->s.weapon == WP_TURRET ) {
		attacker = inflictor->activator;
	}
	if ( ( attacker == targ || !attacker->client ) && ( mod == MOD_CRUSH || mod == MOD_FALLING ) &&
		cl->ps.otherKillerTime > level.time && cl->ps.otherKiller >= 0 && cl->ps.otherKiller < ENTITYNUM_MAX_NORMAL ) {
		attacker = &g_entities[cl->ps.otherKiller];
	}
	return attacker;
}

// Who put ent down, for dying of it: still that one (a player still in
// the game since, anything else still there, never gone since), or the
// world.
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
	return ( by->inuse && ( !by->freetime || by->freetime < cl->rpDownTime ) ) ? by : &g_entities[ENTITYNUM_WORLD];
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

// G_Damage, before anything's done: someone down takes no more harm (qtrue)
// - bar a monster's jaws (grabbed, they're eaten) and a map's hazards: of
// those they bear RP_DOWN_HAZARD, and what comes past that kills.
qboolean G_RPDownGuard( gentity_t *targ, gentity_t *inflictor, gentity_t *attacker, int damage, int dflags, int mod ) {
	gclient_t *cl;

	if ( !G_RPDowned( targ ) ) {
		return qfalse;
	}
	cl = targ->client;
	if ( cl->ps.eFlags2 & EF2_HELD_BY_MONSTER ) {
		return qfalse;
	}
	if ( !G_RP_Hazard( targ, inflictor, attacker, dflags, mod ) ) {
		return qtrue;
	}
	if ( damage < cl->rpDownHazard ) {
		cl->rpDownHazard -= damage;	// (borne)
		return qtrue;
	}
	return qfalse;
}

// G_Damage, a player's health run out: down instead of dying? (qtrue: down)
qboolean G_RPDown( gentity_t *targ, gentity_t *inflictor, gentity_t *attacker, const vec3_t point, int damage, int dflags, int mod ) {
	gclient_t *cl = targ->client;
	gentity_t *by;
	int i;

	if ( G_RPDowned( targ ) || !G_RP_GoesDown( targ, inflictor, attacker, dflags, mod ) ) {
		return qfalse;	// (down already, what still reaches them kills: a hazard; player_die says they're up)
	}
	by = G_RP_Credit( targ, inflictor, attacker, mod );
	cl->rpDowned = qtrue;
	cl->rpDownTime = level.time;
	cl->rpDownBy = (int)( by - g_entities );
	cl->rpDownByTime = ( by->client && cl->rpDownBy < MAX_CLIENTS ) ? by->client->pers.enterTime : 0;
	cl->rpDownMod = mod;
	cl->rpDownDamage = damage;
	cl->rpGiveUpFrom = 0;
	cl->rpAttackFree = qfalse;
	cl->rpDownHazard = RP_DOWN_HAZARD;
	targ->health = cl->ps.stats[STAT_HEALTH] = 1;
	if ( point ) {
		VectorCopy( point, targ->pos1 );	// (where the blow landed, for the death when they give up: G_Damage's)
	} else {
		VectorCopy( cl->ps.origin, targ->pos1 );
	}

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
		G_RP_Who( by ), ( mod >= 0 && mod < MOD_MAX ) ? modNames[mod] : "?" );
	return qtrue;
}

// helper stops getting whom they were getting up (done: got them up; the
// two of them are told next frame, G_RP_HelpTell)
static void G_RP_HelpOver( gentity_t *helper, qboolean done ) {
	gclient_t *cl = helper->client;

	if ( !cl->rpHelping ) {
		return;
	}
	if ( done ) {
		rpHelpDone[helper->s.number] = cl->rpHelping - 1;
	}
	cl->rpHelping = 0;
}

// ent gets up (helper: who got them up; NULL: it no longer applies to them).
static void G_RP_GetUp( gentity_t *ent, gentity_t *helper ) {
	gclient_t *cl = ent->client;
	const int most = cl->ps.stats[STAT_MAX_HEALTH] > 0 ? cl->ps.stats[STAT_MAX_HEALTH] : 100;

	cl->rpDowned = qfalse;
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

	if ( !G_RPDowned( ent ) || level.intermissiontime || level.intermissionQueued ) {
		return;	// (in the intermission, nobody dies: player_die; the round over, nobody scores)
	}
	by = G_RP_DownedBy( ent );
	mod = cl->rpDownMod;
	damage = cl->rpDownDamage;
	G_LogPrintf( "Roleplay: %s" S_COLOR_WHITE " gave up\n", cl->pers.netname );
	ent->health = cl->ps.stats[STAT_HEALTH] = 0;
	cl->ps.otherKillerTime = 0;	// (whose it is was worked out going down: not whoever kicked the body since - player_die)
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

// Whether a look from eye along dir reaches mid (the middle of someone
// down) near enough to give them a hand, and how far off it (squared).
static qboolean G_RP_Sees( const gentity_t *ent, const vec3_t eye, const vec3_t dir, const vec3_t mid, float *off ) {
	vec3_t d;
	trace_t tr;
	float along, dist2;

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
	return (qboolean)( tr.fraction >= 1.0f );	// (no wall between: bodies aren't solid to it)
}

// Whether ent looks at other (down) near enough to give them a hand - aims
// at them (their view: a VR player's right hand, holding a gun), or their
// headset looks at them - and how far off the middle (squared: the nearer).
static qboolean G_RP_LooksAt( const gentity_t *ent, const gentity_t *other, float *off ) {
	vec3_t eye, dir, mid;
	float o;
	qboolean seen = qfalse;

	G_RP_DownMiddle( other, mid );
	VectorCopy( ent->client->ps.origin, eye );
	eye[2] += ent->client->ps.viewheight;
	AngleVectors( ent->client->ps.viewangles, dir, NULL, NULL );
	if ( G_RP_Sees( ent, eye, dir, mid, &o ) ) {
		*off = o;
		seen = qtrue;
	}
	if ( G_VRHeadGaze( ent, eye, dir ) && G_RP_Sees( ent, eye, dir, mid, &o ) && ( !seen || o < *off ) ) {
		*off = o;
		seen = qtrue;
	}
	return seen;
}

// Whether ent can give someone down a hand now.
static qboolean G_RP_CanHelp( const gentity_t *ent ) {
	const gclient_t *cl = ent->client;

	return (qboolean)( G_RPDownOn() && G_RP_InGame( ent ) && ent->health > 0 && cl->ps.pm_type != PM_DEAD &&
		!cl->rpDowned && !cl->ps.duelInProgress && !cl->ps.m_iVehicleNum );	// (no faction: nobody's theirs to get up, G_RP_HelpWhom)
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
			cl->rpHelping = who + 1;
			cl->rpHelpFrom = level.time;
		}
		return;
	}
	if ( who >= 0 && level.time - cl->rpHelpFrom >= RP_HELP_TIME ) {
		G_RP_HelpOver( ent, qtrue );
		G_RP_GetUp( &g_entities[who], ent );
	}
}

// Down: giving up (attack held, where down still holds: mayGiveUp), and
// nothing else they press does anything - here (pers.cmd: what's used,
// TryUse...), and in the move (bg_vrpose.c).
static void G_RP_DownCommand( gentity_t *ent, usercmd_t *ucmd, qboolean mayGiveUp ) {
	gclient_t *cl = ent->client;

	if ( !( ucmd->buttons & BUTTON_ATTACK ) ) {
		cl->rpAttackFree = qtrue;
		cl->rpGiveUpFrom = 0;
	} else if ( cl->rpAttackFree && mayGiveUp ) {
		if ( !cl->rpGiveUpFrom ) {
			cl->rpGiveUpFrom = level.time;
		} else if ( level.time - cl->rpGiveUpFrom >= RP_GIVEUP_TIME ) {
			G_RPGiveUp( ent );
			if ( !G_RPDowned( ent ) ) {
				return;	// (dead: their command's their own; refused - the round over - down still)
			}
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
		G_RP_DownCommand( ent, ucmd, G_RP_StaysDown( ent ) );	// (no longer down here: G_RPDownFrame gets them up)
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

	if ( !npc || !npc->client || !ent || !ent->client ) {
		return -1;
	}
	if ( ent->client->rpDowned ) {
		return 0;	// (nobody harms someone down)
	}
	faction = G_RPFaction( ent->s.number );	// (-1: roleplay off, no character, not a player)
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
		!targ || targ == attacker || !targ->NPC || !targ->client ) {
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

// What a player's hand is doing (whom they're getting up, since when),
// told them and whom it's on as it changes - not more often than
// RP_HELP_TELL_GAP, use pressed again and again; at once when they've gone.
static void G_RP_HelpTell( int i ) {
	const gentity_t *ent = &g_entities[i];
	const qboolean here = (qboolean)( i < level.maxclients && ent->inuse && ent->client &&
		ent->client->pers.connected == CON_CONNECTED );
	const int now = here ? ent->client->rpHelping - 1 : -1;
	const int from = ( now >= 0 ) ? ent->client->rpHelpFrom : 0;
	char text[64];

	if ( now == rpHelpTold[i] && ( now < 0 || from == rpHelpToldFrom[i] ) ) {
		return;	// (nothing new)
	}
	if ( here && level.time - rpHelpToldAt[i] < RP_HELP_TELL_GAP ) {
		return;	// (told a moment ago: this waits; never told - the level's first frames run before anyone's in)
	}
	if ( rpHelpTold[i] >= 0 ) {
		Com_sprintf( text, sizeof( text ), "rpup %i %i %i", i, rpHelpTold[i], ( rpHelpDone[i] == rpHelpTold[i] ) ? -1 : 0 );
		G_RP_Tell( i, text );
		G_RP_Tell( rpHelpTold[i], text );
	}
	if ( now >= 0 ) {
		Com_sprintf( text, sizeof( text ), "rpup %i %i %i", i, now, from > 0 ? from : 1 );
		G_RP_Tell( i, text );
		G_RP_Tell( now, text );
	}
	rpHelpTold[i] = now;
	rpHelpToldFrom[i] = from;
	rpHelpToldAt[i] = level.time;
	rpHelpDone[i] = -1;
}

// Nobody told anything yet (the game started: the first frame, or a
// player's game saying hello before it).
static void G_RP_DownInit( void ) {
	int i;

	if ( rpDownInit ) {
		return;
	}
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		rpDownTold[i] = rpHelpTold[i] = rpHelpDone[i] = -1;
		rpHelpToldFrom[i] = rpHelpToldAt[i] = 0;
	}
	rpDownInit = qtrue;
}

// Someone down who no longer stays down: dead some other way (the body as
// it lies - not up), no longer down here (up), or their character gone
// from under them (logged in somewhere else: no way up - given up).
static void G_RP_DownCheck( gentity_t *ent ) {
	if ( ent->health <= 0 || ent->client->ps.pm_type == PM_DEAD ) {
		G_RPDownDied( ent );
	} else if ( level.intermissiontime || level.intermissionQueued || G_RP_StaysDown( ent ) ) {
		return;	// (as they are, the round over: it's all reset after)
	} else if ( G_RPDownOn() && G_RP_InGame( ent ) ) {
		G_LogPrintf( "Roleplay: %s" S_COLOR_WHITE " lost their character, down\n", ent->client->pers.netname );
		G_RPGiveUp( ent );
	} else {
		G_RP_GetUp( ent, NULL );
	}
}

// G_RunFrame, before the players' end of frame: sides, who no longer stays
// down, and everyone told who's down and who's getting whom up.
void G_RPDownFrame( void ) {
	int i, j;

	G_RP_DownInit();
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		gentity_t *ent = &g_entities[i];
		int now = -1;

		if ( i < level.maxclients && ent->inuse && ent->client && ent->client->pers.connected == CON_CONNECTED ) {
			G_RP_Sides( ent );
			if ( ent->client->rpDowned ) {
				G_RP_DownCheck( ent );
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
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		G_RP_HelpTell( i );
	}
}

// A player's game says it runs this build (again, after a restart): who's
// down, and whom they're getting up or who's getting them up - as told.
void G_RPDownHello( int clientNum ) {
	int i;

	G_RP_DownInit();
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		if ( rpDownTold[i] >= 0 ) {
			G_RP_Tell( clientNum, va( "rpdn %i %i", i, rpDownTold[i] ) );
		}
		if ( rpHelpTold[i] >= 0 && ( i == clientNum || rpHelpTold[i] == clientNum ) ) {
			G_RP_Tell( clientNum, va( "rpup %i %i %i", i, rpHelpTold[i], rpHelpToldFrom[i] > 0 ? rpHelpToldFrom[i] : 1 ) );
		}
	}
}
