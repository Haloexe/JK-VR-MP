/*
===========================================================================
OpenJK VR (multiplayer) - invasions: an admin's waves of NPCs, coming for
the players their side fights - roleplay on or off; and wars: each
faction's waves coming for the others' where the admin stands.

OF ONE TYPE OR SEVERAL (its lineup: RPI_KINDS at most, of one side - its
file's, or the admin's pick for them all): each wave has of each its own
number of it (reborn:2), or - mixed, as they are unless they say - a share
of how many a wave (per) with the others mixed, evenly (what won't split,
one more each, in turn from a wave to the next); they come out in turn, the
one with most still to come first (mixed in, not one after another of a
type).

WARS (rp war: one at once): two or three factions - the light side, the
dark side, creatures - each a lineup, all of it on its faction (picked:
G_RPSideBegin), each as an invasion of its side (rpi[s].war) with the same
waves: where the admin stands, each faction comes in from its own way round
them (rpiWar.way: the first ahead of them - one of a faction they're not
of - the rest evenly round), out of everyone's sight where it can, where it
can walk straight there (rpiWarRings: 12.5 to 20 m), coming for the others'
(G_RPI_Nearest: the nearest of the others' up, or a player its side fights -
players by their faction, as ever) - each side's NPCs every other side's
foes, not their own side's, as the game's AI has it too (G_RPWarEnemy: its
NPCs fight one other side, a creature whichever it went for last). A side
whose last wave's beaten is out; one left standing, it's won - everyone
told, its NPCs gone RPI_WAR_AFTER on (none left: a draw). Waves 0: till
it's stopped (rp war stop, or any of its invasions stopped).

WHO THEY FIGHT: as their side has it - their NPC type's (its file's
playerTeam), or the one the admin picked (side: they take it as they begin,
G_RPSideBegin - a neutral type too) - and the NPCs' sides do (g_vrdown.c):
the dark side's (Reborn, troopers) go
after heroes, the light side's (Rebels, Jedi) villains, creatures (rancors,
wampas) everyone; a player playing no character - roleplay off, or not
playing one - or a bot, the dark side's go after, the light side's stand
beside (G_RPSideEnemy). Nobody down, nobody in a duel (nothing harms them
but whom they duel). Other NPCs as ever: the light side's
fight the dark side's, another invasion's too. Players' companions go after
those who go after their owner.

ONE A SIDE at once - the dark side's, the light side's, creatures' - so
Rebels can come to the heroes' side as Reborn come at them. Each has:
waves (0: until it's stopped), how many a wave, how many up at once at
most (every invasion's together RPI_MOST), and when the next wave comes:
wait seconds after one's beaten, or every wait seconds (RPI_EVERY_LEAST at
least; and not before the last has all come out). Its last wave beaten,
it's over; any of it still up RPI_LEFTOVER after the last has all come out
goes. Nobody in the game it's after, none of it fighting and the other
side's invasion not on, RPI_IDLE: it's over (Rebels by players their side
doesn't fight, the Reborn gone) - nobody in the game at all, it waits.

WHERE: each one comes out for one of the players it's after (the one fewest
are out for), 17.5 to 30 m from them (rpiRings), on their level or a stair
or two up or down, where a body of its type fits (its file's width and
height: a rancor's no person's) - out of everyone's sight where it can walk
straight to them (no waypoints: on any map); else out of sight not as far
(12.5 to 16.3 m); else out of sight with nothing between; and only then, all
those looked at, in sight; near (8 to 11 m) last of all (rpiPasses) - one
that fights up close where it can walk to them, in sight or near, before
where it can't (it'd get nowhere). Each of an invasion's for a player is
looked for from further round them than the last for them, a ring further
out or in (RPI_SPREAD): they come from all round them, near and far - not
one after another behind them (the first for them, behind them); in sight,
from behind them as ever, every ring one way before the next (the least in
their sight first, wherever they're looking). Each looking's for one player
(another's from the start). Looking costs traces: so many a
frame, every invasion's together (RPI_TRACES_FRAME); a player with nowhere
near them a while not come out for. Nobody it's after in the game: by
someone in it, to wait - it goes after whoever comes. Nobody in the game at
all: the wave waits. Once it's begun (NPC_Begin), it's after that player,
then, with nobody, whoever's nearest of those it's after. One getting
nowhere RPI_STUCK - not moving, its enemy out of its reach - or further than
RPI_LOST from anyone it's after, out of everyone's sight, goes, and comes
out again somewhere better. Bodies go RPI_BODY on - or sooner, more than
RPI_BODIES lying there (the game's own leave some for good).

ROOM: an invader comes out only where the level has room (G_RPCompRoom: the
companions' rule - configstrings, the gamestate - and RPI_ROOM_ENTS entities
left free), in a slot an NPC's had before where there's one
(G_RPCompEntity), its type kept with the invasion, not a string spent a
spawn (G_RPInvadeSpawner). One going lets go of whoever it holds, and is
let go of (a rancor's: G_RPLetGo).

TOLD: everyone, each wave as it comes and the end, on their screen and in
their console; the log, every start, wave and end (an admin's command too, as
their cheats are; anyone else's try, refused). Admins' games of protocol RP_PROTOCOL_INVADE and later,
how each goes (rpinv <count> "<line>"..., as it changes, every RPI_TELL at
most): the Admin page's Invasion part.

Not in Siege or the duel game types (their rounds); held in the intermission.

  rp invasion [status]
  rp invasion start <who> [waves <n>] [per <n>] [cap <n>] [cleared <s> | every <s>]
                   [side dark | light | creatures]
  rp invasion stop [all | dark | light | creatures | <who>]
  rp war [status]
  rp war start <side> <who> <side> <who> [<side> <who>] [waves <n>] [per <n>]
               [cap <n>] [cleared <s> | every <s>] [at <client>]
  rp war stop
<who>: an NPC type (any the NPC files have, but a vehicle or - its side not
picked - a neutral one: a droid, a civilian - it'd fight nobody), or reborn,
stormtroopers, rebels, jedi (rpiPresets) - or several, a comma between
(reborn,stormtroopers), each mixed or its own number a wave after a colon
(reborn:2). A side: villains, heroes too (G_RPSideWord); file, its file's.
A war's where the admin stands - at <client>, where they do (rcon's must
say). An admin's, or the console's (rcon).

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "g_local.h"
#include "b_local.h"

extern char NPCParms[];		// (NPC_stats.c: the NPC files, read)
extern gentity_t *NPC_Spawn_Do( gentity_t *ent );
extern void NPC_Begin( gentity_t *ent );
extern void AI_DeleteSelfFromGroup( gentity_t *self );
extern char *ConcatArgs( int start );

#define RPI_SIDES			3		// invasions at once: one a side
#define RPI_MOST			32		// invaders up on the server at once, every invasion's together (each an NPC the server runs)
#define RPI_TRACK			64		// an invasion's NPCs kept track of: up, and bodies (RPI_MOST + RPI_BODIES + RPI_PER_FRAME at most)
#define RPI_WAVES_MOST		50		// waves an invasion has at most (0: until it's stopped)
#define RPI_WAIT_MOST		600		// s between waves at most
#define RPI_CLEAR			136.0f	// never nearer anyone in the game than 4 m (a body wider than a person's: its width more)
#define RPI_APART			34.0f	// nor nearer another not begun yet than this past both their bodies (its body's not there till it has)
#define RPI_SIGHT_COS		0.34f	// in someone's sight: within 70 degrees of where they look (a headset's wide)...
#define RPI_SEE_FAR			2048.0f	// ...no further than 60 m, nothing between
#define RPI_PER_FRAME		2		// invaders an invasion brings out a frame at most
#define RPI_TRACES_FRAME	400		// traces every invasion's looking takes a frame, all told (a place begun's looked at whole: a little more)
#define RPI_UNDER			1024.0f	// a player in the air: the floor this far under them, where a way to them ends
#define RPI_AVOID			2000	// ms a player with nowhere near them for one isn't come out for (another is)
#define RPI_ROOM_ENTS		128		// entities left free for the level, one to come out (each takes 3 at most: itself, its saber, its goal)
#define RPI_ROOM_WAIT		5000	// ms before looking again, the level out of room
#define RPI_IDLE			60000	// ms with nobody in the game it's after, none of it fighting and the other side's invasion not on: it's over
#define RPI_EVERY_LEAST		5		// s between waves on a timer at least (a wave a frame would flood everyone's games)
#define RPI_BODIES			16		// invaders' bodies lying there at most, every invasion's together: more, the oldest goes
#define RPI_BEGIN_WAIT		2000	// ms one that hasn't begun (someone where it'd stand) waits: then it comes out elsewhere
#define RPI_SCAN			500		// ms between an invader's looks for whom it's after
#define RPI_STUCK_STEP		1000	// ms between looks at whether one's getting anywhere...
#define RPI_STUCK_MOVE		48.0f	// ...moved less than this since...
#define RPI_STUCK			8000	// ...this long, its enemy out of its reach: out of everyone's sight, elsewhere
#define RPI_REACH			96.0f	// a saber's, fists', a creature's: its enemy this much past its body's edge is in reach
#define RPI_LOST			2720.0f	// further than 80 m from anyone it's after, RPI_STUCK: elsewhere
#define RPI_BODY			10000	// ms an invader's body lies there
#define RPI_LEFTOVER		120000	// ms after the last wave's all come out, those still up go
#define RPI_TELL			1000	// ms between admins' status lines at most
#define RPI_KINDS			4		// NPC types an invasion's of at most (its lineup)
#define RPI_EACH_MOST		32		// a type's own number a wave at most (reborn:32)
#define RPI_TITLE_SIZE		160		// what an invasion's called: its types' together ("Stormtrooper, Reborn and Rebel")
#define RPI_WAR_AFTER		15000	// ms a war's winners stay, the war won (then they go)
#define RPI_LOOK_WAR		-3		// (lookFor: its looking's for its way into the war)

enum { RPI_DARK, RPI_LIGHT, RPI_CREATURES };
static const int rpiTeams[RPI_SIDES] = { NPCTEAM_ENEMY, NPCTEAM_PLAYER, NPCTEAM_FREE };
static const char *rpiSideNames[RPI_SIDES] = { "dark side", "light side", "creatures" };
static const char *rpiSideWords[RPI_SIDES] = { "dark", "light", "creatures" };
static const char *rpiTags[RPI_SIDES] = { "vri_dark", "vri_light", "vri_creatures" };	// (their targetname: npc kill vri_dark)

// what an invasion's waiting for, bringing more out (as admins are told)
enum { RPIW_NONE, RPIW_NOBODY, RPIW_ROOM, RPIW_NOWHERE };

// The presets: a word, what the invasion's called, the types it's first of
// (the stock files'), else the first of its class on its side whose name has
// stem in it, else the first of its class on its side.
typedef struct {
	const char	*word;
	const char	*title;
	const char	*types[4];
	const char	*cls;
	int			team;
	const char	*stem;
} rpiPreset_t;
static const rpiPreset_t rpiPresets[] = {
	{ "reborn",			"Reborn",		{ "reborn", NULL },							"CLASS_REBORN",			NPCTEAM_ENEMY,	"reborn" },
	{ "stormtroopers",	"Stormtrooper",	{ "stormtrooper", NULL },					"CLASS_STORMTROOPER",	NPCTEAM_ENEMY,	"trooper" },
	{ "rebels",			"Rebel",		{ "rebel", "rebel2", NULL },				"CLASS_REBEL",			NPCTEAM_PLAYER,	"rebel" },
	{ "jedi",			"Jedi",			{ "jedi", "jedi2", "jeditrainer", NULL },	"CLASS_JEDI",			NPCTEAM_PLAYER,	"jedi" },
};

typedef struct {
	int			ent;			// its NPC (-1: none)
	int			tag;			// its rpInvader (G_RPI_Npc: that one, not another since in its slot)
	int			kind;			// which of the invasion's types it is (kinds[])
	int			target;			// the player it came out for (or by: nobody it's after in the game then; -1, into a war)
	int			bornAt;			// level.time it was spawned
	qboolean	begun;			// NPC_Begin's run: set going
	int			diedAt;			// level.time its health ran out (0: it's up)
	int			scanTime;		// the last look for whom it's after...
	int			near;			// ...the nearest (their client number; -1: nobody)...
	float		nearDist;		// ...how far
	vec3_t		lastAt;			// where it was...
	int			lastAtTime;		// ...then
	int			stuckSince;		// getting nowhere since (0: it's getting on)
} rpiInvader_t;

// One of the NPC types an invasion's of.
typedef struct {
	char		type[RP_COMP_TYPE_SIZE];	// its NPC type, lower case (its NPCs' NPC_type points here)
	char		title[RP_COMP_TYPE_SIZE];	// what it's called
	vec3_t		mins, maxs;		// its NPCs' bodies (its type's width and height, as NPC_ParseParms has them)
	qboolean	close;			// its NPCs fight up close (a saber, fists, a creature's claws: its file's weapon) - else a gun
	int			each;			// how many of it a wave (0: mixed - a share of per, with the others mixed)
	int			toCome;			// of this wave, still to come out
	int			again;			// to come out again
} rpiKind_t;

typedef struct {
	qboolean	on;
	rpiKind_t	kinds[RPI_KINDS];	// what it's of (its lineup)...
	int			numKinds;		// ...how many
	int			lastKind;		// the one that came out last (the next of as many to come: the one after it)
	char		title[RPI_TITLE_SIZE];	// what it's called (its types' together)
	qboolean	picked;			// its side the admin's pick (its NPCs take it as they begin: G_RPSideBegin), not its file's
	qboolean	war;			// a side of the war (rpiWar): comes in where it's fought, for the others'
	qboolean	lost;			// ...its last wave beaten: out of it (on, its bodies lying there, till the war's over)
	int			endAt;			// level.time the war's winners go (0: not yet)
	int			waves;			// 0: until it's stopped
	int			per;			// a wave's mixed ones' (all of them, of one type mixed)
	int			cap;			// up at once at most
	qboolean	timed;			// a wave every wait s (else wait s after one's beaten)
	int			wait;			// s
	int			wave;			// waves begun
	int			toCome;			// of this wave, still to come out (each type's, together)
	int			again;			// to come out again (getting nowhere, lost, never begun: each type's, together)
	int			waveAt;			// level.time this wave began
	int			arrivedAt;		// level.time it had all come out (0: not yet)
	int			beatenAt;		// level.time it was beaten (0: not)
	int			killed;			// its invaders killed, all told
	int			waiting;		// RPIW_*: what it's waiting for, bringing more out
	int			tryAfter;		// level.time to look again (the level out of room)
	int			idleSince;		// level.time since nobody in the game it's after, and none of it fighting (0: there is, it is)
	int			lookFor;		// the player its looking's for (-1: none yet; RPI_LOOK_WAR, its way into the war)...
	qboolean	lookClose;		// ...for one fighting up close (another, from the start: the places' order is another)...
	int			pass;			// ...the places it'll take (rpiPasses)...
	int			passLeft;		// ...those of them still to look at...
	int			turn;			// ...and where its looking's got to (the next place looked at)
	int			warSpread;		// how many came out into the war (where the next is looked for from: G_RPI_WarPlaceAt)
	int			warAvoid;		// level.time it may look again, there being nowhere on its way in
	int			warPass[2];		// the passes the last into the war came in by this wave, by a gun and up close (the next looks from there: the
								// better ones looked at all the way round, and none, just now - most often all in sight, a war watched)
	int			spread[MAX_CLIENTS];	// how many of its came out for each player, RPI_SPREAD_EVERY round (where the next for them is looked for from: G_RPI_PlaceAt)
	int			avoid[MAX_CLIENTS];	// level.time each player may be come out for again (nowhere near them till then)
	rpiInvader_t	out[RPI_TRACK];
} rpiInvasion_t;

static rpiInvasion_t rpi[RPI_SIDES];

// The war (one at once): its sides (each an invasion, war), where it's fought.
typedef struct {
	qboolean	on;
	int			sides;			// 1 << RPI_* each side in it
	vec3_t		at;				// where the admin stood (their origin)...
	vec3_t		floor;			// ...the floor under them (where the ways in end)
	float		way[RPI_SIDES];	// the way round there each side comes in from (a yaw)
	int			startedAt;		// level.time
} rpiWar_t;
static rpiWar_t rpiWar;

static int rpiSerial;			// the last invader's number (its rpInvader: number << 2 | side + 1)
static int rpiTellTime;			// admins' status last made...
static char rpiStatus[MAX_STRING_CHARS];	// ...as it was...
static int rpiStatusSeq;		// ...its number (another each change)...
static int rpiTold[MAX_CLIENTS];	// ...and the one each admin's game was told (-1: none)
static int rpiTraces;			// traces the invasions have taken this frame of their own (G_RPI_Spent: RPI_TRACES_FRAME)...
static unsigned int rpiCompTraces;	// ...and the companions' rules had taken, as it began
static int rpiFirst;			// the invasion that brings out first this frame (each in turn)
static int rpiRoomAt = -1;		// level.time the level's room was looked at (-1: look again)...
static qboolean rpiRoomOk;		// ...and whether there was some

/*
================================================================================
Small things
================================================================================
*/

// A word to whoever asked: an admin (the Admin page's line, and their
// console), or the console.
static void G_RPI_Say( const gentity_t *ent, const char *text ) {
	if ( ent ) {
		G_RPMessage( ent - g_entities, text );
	} else {
		trap->Print( "%s\n", text );
	}
}

// A line for whoever asked, in their console.
static void G_RPI_Print( const gentity_t *ent, const char *text ) {
	if ( ent ) {
		trap->SendServerCommand( ent - g_entities, va( "print \"%s\n\"", text ) );
	} else {
		trap->Print( "%s\n", text );
	}
}

// Everyone told, on their screen and in their console.
static void G_RPI_Announce( const char *screen, const char *console ) {
	char cp[MAX_STRING_CHARS], print[MAX_STRING_CHARS];

	Com_sprintf( cp, sizeof( cp ), "cp \"%s\n\"", screen );
	Com_sprintf( print, sizeof( print ), "print \"%s\n\"", console );
	trap->SendServerCommand( -1, cp );
	trap->SendServerCommand( -1, print );
}

// Whether a player's in the game: here, playing (not spectating or following).
static qboolean G_RPI_InGame( const gentity_t *ent ) {
	return (qboolean)( ent && ent->inuse && ent->client && ent->s.number >= 0 && ent->s.number < MAX_CLIENTS &&
		ent->client->pers.connected == CON_CONNECTED && ent->client->sess.sessionTeam != TEAM_SPECTATOR &&
		!( ent->client->ps.pm_flags & PMF_FOLLOW ) && ent->client->tempSpectate < level.time );
}

// Whether a player's alive in the game (down's alive).
static qboolean G_RPI_Alive( const gentity_t *ent ) {
	return (qboolean)( G_RPI_InGame( ent ) && ent->health > 0 && ent->client->ps.pm_type != PM_DEAD && ent->client->ps.pm_type != PM_SPECTATOR );
}

// Whether an invader of side s goes after ent, a player: alive in the game,
// not down, not in a duel (G_RPSideEnemy), not hidden (no target), its side
// against them.
static qboolean G_RPI_Fair( int s, const gentity_t *ent ) {
	int r;

	if ( !G_RPI_Alive( ent ) || ( ent->flags & FL_NOTARGET ) || ent->client->rpDowned ) {
		return qfalse;
	}
	r = G_RPSideEnemy( rpiTeams[s], qtrue, ent );
	return ( r >= 0 ) ? (qboolean)r : (qboolean)( rpiTeams[s] == NPCTEAM_FREE );	// (creatures: anyone - roleplay doesn't say of them)
}

// Whether ent's a player in the game an invasion's there for - one it goes
// after, or would: down, dead a moment, in a duel, hidden (it isn't over).
static qboolean G_RPI_Foe( int s, const gentity_t *ent ) {
	int r;

	if ( !G_RPI_InGame( ent ) ) {
		return qfalse;
	}
	r = G_RPSideRule( rpiTeams[s], qtrue, ent );
	return ( r >= 0 ) ? (qboolean)r : (qboolean)( rpiTeams[s] == NPCTEAM_FREE );
}

// Where a player's look starts and goes: a VR player's headset, or where they aim.
static void G_RPI_Look( const gentity_t *ent, vec3_t eye, vec3_t dir ) {
	if ( G_VRHeadGaze( ent, eye, dir ) ) {
		return;
	}
	VectorCopy( ent->client->ps.origin, eye );
	eye[2] += ent->client->ps.viewheight;
	AngleVectors( ent->client->ps.viewangles, dir, NULL, NULL );
}

// Whether a line from a to b is clear of the world (what a look reaches).
static qboolean G_RPI_Sees( const vec3_t a, const vec3_t b, int pass ) {
	trace_t tr;

	trap->Trace( &tr, a, NULL, NULL, b, pass, MASK_SOLID, qfalse, 0, 0 );
	return (qboolean)( tr.fraction >= 1.0f && !tr.startsolid );
}

// Whether anyone in the game sees a body standing at at, top high over it
// (its box's top: a person's DEFAULT_MAXS_2): its head or its middle - a tall
// one's half way up too - within RPI_SIGHT_COS of where they look,
// RPI_SEE_FAR at most, with nothing between.
static qboolean G_RPI_Seen( const vec3_t at, float top ) {
	const float head = top - 6.0f;	// (a person's: DEFAULT_VIEWHEIGHT - 2)
	const int points = ( head > 64.0f ) ? 3 : 2;
	int i, p;

	for ( i = 0; i < level.maxclients; i++ ) {
		const gentity_t *ent = &g_entities[i];
		vec3_t eye, dir;
		if ( !G_RPI_InGame( ent ) ) {
			continue;
		}
		G_RPI_Look( ent, eye, dir );
		for ( p = 0; p < points; p++ ) {
			vec3_t spot, d;
			float dist;
			VectorCopy( at, spot );
			spot[2] += ( p == 0 ) ? head : ( p == 1 ) ? 0.0f : head * 0.5f;	// (its head, its middle, half way up)
			VectorSubtract( spot, eye, d );
			dist = VectorNormalize( d );
			if ( dist > RPI_SEE_FAR || DotProduct( d, dir ) < RPI_SIGHT_COS ) {
				continue;
			}
			rpiTraces++;
			if ( G_RPI_Sees( eye, spot, i ) ) {
				return qtrue;
			}
		}
	}
	return qfalse;
}

/*
================================================================================
What it's of
================================================================================
*/

// An NPC type's block, from its opening brace (p just past its name), read
// as NPC_ParseParms reads it - a line it doesn't know passed over (a brace
// in it too), its first closing brace its end: its class, side (its
// playerTeam: NPCTEAM_*, NPCTEAM_FREE where it doesn't say, as the game
// leaves it), model, body (its width and height: a person's where it
// doesn't say) and whether it fights up close (its weapon a saber, fists, a
// baton - or none: a creature's claws). qfalse: it's broken.
static qboolean G_RPI_Block( const char **p, char *cls, int clsSize, int *team, char *model, int modelSize, int *width, int *height, qboolean *close ) {
	const char *tok;

	cls[0] = model[0] = '\0';
	*team = NPCTEAM_FREE;
	*width = 15;
	*height = DEFAULT_MAXS_2 - DEFAULT_MINS_2;
	*close = qtrue;
	tok = COM_ParseExt( p, qtrue );
	if ( strcmp( tok, "{" ) ) {
		return qfalse;
	}
	while ( 1 ) {
		tok = COM_ParseExt( p, qtrue );
		if ( !tok[0] ) {
			return qfalse;
		}
		if ( !strcmp( tok, "}" ) ) {
			return qtrue;
		}
		if ( !Q_stricmp( tok, "class" ) ) {
			Q_strncpyz( cls, COM_ParseExt( p, qfalse ), clsSize );
		} else if ( !Q_stricmp( tok, "playerModel" ) ) {
			Q_strncpyz( model, COM_ParseExt( p, qfalse ), modelSize );
		} else if ( !Q_stricmp( tok, "playerTeam" ) ) {
			const char *t = COM_ParseExt( p, qfalse );
			*team = !Q_stricmp( t, "TEAM_PLAYER" ) ? NPCTEAM_PLAYER : !Q_stricmp( t, "TEAM_ENEMY" ) ? NPCTEAM_ENEMY :
				!Q_stricmp( t, "TEAM_NEUTRAL" ) ? NPCTEAM_NEUTRAL : NPCTEAM_FREE;
		} else if ( !Q_stricmp( tok, "weapon" ) ) {
			const char *t = COM_ParseExt( p, qfalse );
			*close = (qboolean)( !t[0] || !Q_stricmp( t, "WP_NONE" ) || !Q_stricmp( t, "WP_SABER" ) || !Q_stricmp( t, "WP_MELEE" ) || !Q_stricmp( t, "WP_STUN_BATON" ) );
		} else if ( !Q_stricmp( tok, "width" ) || !Q_stricmp( tok, "height" ) ) {
			int *which = Q_stricmp( tok, "width" ) ? height : width;	// (before the next token's read over this one)
			const char *t = COM_ParseExt( p, qfalse );
			if ( t[0] ) {
				*which = atoi( t );	// (COM_ParseInt's)
			}
		} else {
			SkipRestOfLine( p );
		}
	}
}

// Where the NPC files' type of this name is (any case; the first of it, as
// the game takes it): just past its name (name: as written there) - NULL,
// none.
static const char *G_RPI_TypeAt( const char *type, char *name, int nameSize ) {
	const char *p = NPCParms, *tok;

	if ( !BG_VRCompTypeOk( type ) ) {
		return NULL;
	}
	COM_BeginParseSession( "rp invasion type" );
	while ( 1 ) {
		tok = COM_ParseExt( &p, qtrue );
		if ( !tok[0] ) {
			return NULL;
		}
		if ( !Q_stricmp( tok, type ) ) {
			if ( name ) {
				Q_strncpyz( name, tok, nameSize );
			}
			return p;
		}
		SkipBracedSection( &p, 0 );
	}
}

// The NPC type the files have by this name: its name as written, class, side,
// model and body (qfalse: none).
static qboolean G_RPI_TypeInfo( const char *type, char *name, int nameSize, char *cls, int clsSize, int *team, char *model, int modelSize, int *width, int *height,
	qboolean *close ) {
	const char *p = G_RPI_TypeAt( type, name, nameSize );

	return p ? G_RPI_Block( &p, cls, clsSize, team, model, modelSize, width, height, close ) : qfalse;
}

// The first NPC type of a class on a side with a model (stem: whose name has
// it in it, any case; NULL: any name) - into name (qfalse: none).
static qboolean G_RPI_TypeOf( const char *cls, int team, const char *stem, char *name, int size ) {
	const char *p = NPCParms, *tok;

	COM_BeginParseSession( "rp invasion types" );
	while ( 1 ) {
		char got[RP_COMP_TYPE_SIZE], low[RP_COMP_TYPE_SIZE], gotCls[64], model[MAX_QPATH];
		const char *block;
		int gotTeam, width, height;
		qboolean close;
		tok = COM_ParseExt( &p, qtrue );
		if ( !tok[0] ) {
			return qfalse;
		}
		Q_strncpyz( got, tok, sizeof( got ) );
		Q_strncpyz( low, tok, sizeof( low ) );
		Q_strlwr( low );
		block = p;
		if ( !G_RPI_Block( &p, gotCls, sizeof( gotCls ), &gotTeam, model, sizeof( model ), &width, &height, &close ) ) {
			return qfalse;	// (the rest's broken)
		}
		p = block;
		SkipBracedSection( &p, 0 );	// (past the whole of it, as the game looks past a type - braces in it too)
		if ( ( !stem || strstr( low, stem ) ) && BG_VRCompTypeOk( got ) && !Q_stricmp( gotCls, cls ) && gotTeam == team && model[0] &&
			G_RPI_TypeAt( got, NULL, 0 ) == block ) {	// (the first of its name, as the game takes it: not one after another of that name)
			Q_strncpyz( name, got, size );
			return qtrue;
		}
	}
}

// Whether the server has a file.
static qboolean G_RPI_FileThere( const char *path ) {
	fileHandle_t f = 0;
	const int len = trap->FS_Open( path, &f, FS_READ );

	if ( f ) {
		trap->FS_Close( f );
	}
	return (qboolean)( f && len > 0 );
}

// Whether the server has a player model (models/players/<model>/model.glm)
// and its animations: on the humanoid skeleton, or one whose own the server
// has (its skeleton's animation.cfg, as SetupGameGhoul2Model reads it - one
// it can't read takes the level down, as it would a spawn's). 0: it has; 1:
// no model; 2: no animations (*anims: whose).
static int G_RPI_ModelThere( const char *model, char *anims, int animsSize ) {
	byte head[BG_VRCOMP_GLM_HEAD];
	char gla[MAX_QPATH], *slash;
	fileHandle_t f = 0;
	int len;
	qboolean read = qfalse;

	anims[0] = '\0';
	if ( !model[0] || strchr( model, '.' ) || strchr( model, ':' ) || strchr( model, '\\' ) || strchr( model, '/' ) || strlen( model ) > 32 ) {
		return 1;
	}
	len = trap->FS_Open( va( "models/players/%s/model.glm", model ), &f, FS_READ );
	if ( !f || len <= 0 ) {
		if ( f ) {
			trap->FS_Close( f );
		}
		return 1;
	}
	if ( len >= (int)sizeof( head ) && trap->FS_Read( head, sizeof( head ), f ) == (int)sizeof( head ) && !memcmp( head, "2LGM", 4 ) ) {
		read = qtrue;
	}
	trap->FS_Close( f );
	if ( !read ) {
		return 2;	// (not a model the game can load)
	}
	Q_strncpyz( gla, (const char *)head + 8 + MAX_QPATH, sizeof( gla ) );	// (its skeleton's name: BG_VRCompGlmHumanoid)
	if ( !gla[0] || strstr( gla, "players/_humanoid/" ) ) {
		return 0;
	}
	slash = Q_strrchr( gla, '/' );
	if ( !slash ) {
		return 2;
	}
	*slash = '\0';
	Com_sprintf( anims, animsSize, "%s/animation.cfg", gla );
	return G_RPI_FileThere( anims ) ? 0 : 2;
}

// The NPC type an invasion's of for who (an NPC type, or a preset), what
// it's called, its side (RPI_*: pick's - the admin's - or, -1, its file's),
// its body (width, height) and whether it fights up close: qtrue - else why
// it won't do (in why).
static qboolean G_RPI_Resolve( const char *who, int pick, char *type, int typeSize, char *title, int titleSize, int *side, int *width, int *height, qboolean *close,
	char *why, int whySize ) {
	char name[RP_COMP_TYPE_SIZE], cls[64], model[MAX_QPATH];
	int team = NPCTEAM_FREE, i, k;
	const rpiPreset_t *pre = NULL;

	if ( !who[0] ) {
		Q_strncpyz( why, "Say who invades: reborn, stormtroopers, rebels, jedi, or an NPC type", whySize );
		return qfalse;
	}
	if ( !BG_VRCompTypeOk( who ) ) {
		Q_strncpyz( why, "An NPC type's name is letters, numbers, _ - and .", whySize );
		return qfalse;
	}
	if ( G_RPI_TypeInfo( who, name, sizeof( name ), cls, sizeof( cls ), &team, model, sizeof( model ), width, height, close ) ) {
		Q_strncpyz( title, name, titleSize );	// (as its file has it)
	} else {
		for ( i = 0; i < (int)ARRAY_LEN( rpiPresets ) && !pre; i++ ) {
			if ( !Q_stricmp( who, rpiPresets[i].word ) ) {
				pre = &rpiPresets[i];
			}
		}
		if ( !pre ) {
			Com_sprintf( why, whySize, "There's no NPC type %s on this server", who );
			return qfalse;
		}
		name[0] = '\0';
		for ( k = 0; k < (int)ARRAY_LEN( pre->types ) && pre->types[k] && !name[0]; k++ ) {
			char got[RP_COMP_TYPE_SIZE];
			if ( G_RPI_TypeInfo( pre->types[k], got, sizeof( got ), cls, sizeof( cls ), &team, model, sizeof( model ), width, height, close ) && model[0] && team != NPCTEAM_NEUTRAL ) {
				Q_strncpyz( name, got, sizeof( name ) );
			}
		}
		if ( !name[0] && !G_RPI_TypeOf( pre->cls, pre->team, pre->stem, name, sizeof( name ) ) && !G_RPI_TypeOf( pre->cls, pre->team, NULL, name, sizeof( name ) ) ) {
			Com_sprintf( why, whySize, "This server's NPC files have no %s", pre->word );
			return qfalse;
		}
		if ( !G_RPI_TypeInfo( name, name, sizeof( name ), cls, sizeof( cls ), &team, model, sizeof( model ), width, height, close ) ) {
			Com_sprintf( why, whySize, "This server's NPC files have no %s", pre->word );	// (can't be: just found)
			return qfalse;
		}
		Q_strncpyz( title, pre->title, titleSize );
	}
	if ( !Q_stricmp( cls, "CLASS_VEHICLE" ) ) {
		Com_sprintf( why, whySize, "%s is a vehicle: pick one of the people, a droid or a creature", name );
		return qfalse;
	}
	if ( team == NPCTEAM_NEUTRAL && pick < 0 ) {
		Com_sprintf( why, whySize, "%s is on nobody's side: it'd fight no one (pick it one: side dark, light or creatures)", name );
		return qfalse;
	}
	if ( !model[0] ) {
		Com_sprintf( why, whySize, "%s has no model: it couldn't come out", name );
		return qfalse;
	}
	{
		char anims[MAX_QPATH];
		const int there = G_RPI_ModelThere( model, anims, sizeof( anims ) );
		if ( there == 1 ) {
			Com_sprintf( why, whySize, "This server doesn't have %s's model (%s)", name, model );
			return qfalse;
		}
		if ( there == 2 ) {
			Com_sprintf( why, whySize, "This server can't animate %s: its model's skeleton has no animations here (%s)", name, anims[0] ? anims : model );
			return qfalse;
		}
	}
	*side = ( pick >= 0 ) ? pick : ( team == NPCTEAM_ENEMY ) ? RPI_DARK : ( team == NPCTEAM_PLAYER ) ? RPI_LIGHT : RPI_CREATURES;
	Q_strncpyz( type, name, typeSize );
	Q_strlwr( type );
	return qtrue;
}

static int G_RPI_Number( const char *text, int low, int high );

// What an invasion of these is called: its types' titles together ("Reborn",
// "Stormtrooper and Reborn", "Stormtrooper, Reborn and Rebel").
static void G_RPI_Title( const rpiKind_t *kinds, int n, char *out, int size ) {
	int k;

	out[0] = '\0';
	for ( k = 0; k < n; k++ ) {
		Q_strcat( out, size, kinds[k].title );
		Q_strcat( out, size, ( k < n - 2 ) ? ", " : ( k == n - 2 ) ? " and " : "" );
	}
}

// An invasion's lineup for who - an NPC type or a preset, or several, a comma
// between (reborn,stormtroopers), each mixed or its own number a wave after a
// colon (reborn:2) - into kinds (n of them): their side (pick's, the admin's;
// else theirs - one for them all) and what it's called: qtrue - else why it
// won't do (in why).
static qboolean G_RPI_Lineup( const char *who, int pick, rpiKind_t *kinds, int *n, int *side, char *title, int titleSize, char *why, int whySize ) {
	const char *p = who;

	*n = 0;
	*side = -1;
	while ( 1 ) {
		char entry[MAX_TOKEN_CHARS];
		const char *comma = strchr( p, ',' );
		char *colon;
		int len = comma ? (int)( comma - p ) : (int)strlen( p ), each = 0, s = 0, width = 15, height = DEFAULT_MAXS_2 - DEFAULT_MINS_2, i;
		qboolean close = qtrue;
		rpiKind_t *kd;

		len = Q_min( len, (int)sizeof( entry ) - 1 );
		memcpy( entry, p, len );
		entry[len] = '\0';
		colon = strchr( entry, ':' );
		if ( colon ) {
			*colon = '\0';
			if ( ( each = G_RPI_Number( colon + 1, 1, RPI_EACH_MOST ) ) < 0 ) {
				Com_sprintf( why, whySize, "How many of one a wave is 1 to %i, after a colon: reborn:2", RPI_EACH_MOST );
				return qfalse;
			}
		}
		if ( ( comma || *n ) && !entry[0] ) {
			Q_strncpyz( why, "Say who invades, a comma between each: reborn,stormtroopers", whySize );	// (one's empty: "reborn,,jedi")
			return qfalse;
		}
		if ( *n >= RPI_KINDS ) {
			Com_sprintf( why, whySize, "An invasion's of %i NPC types at most", RPI_KINDS );
			return qfalse;
		}
		kd = &kinds[*n];
		memset( kd, 0, sizeof( *kd ) );
		if ( !G_RPI_Resolve( entry, pick, kd->type, sizeof( kd->type ), kd->title, sizeof( kd->title ), &s, &width, &height, &close, why, whySize ) ) {
			return qfalse;
		}
		for ( i = 0; i < *n; i++ ) {
			if ( !strcmp( kinds[i].type, kd->type ) ) {
				Com_sprintf( why, whySize, "%s is in it twice: say it once - how many a wave of it after a colon (%s:2)", kd->title, entry );
				return qfalse;
			}
		}
		if ( *side >= 0 && s != *side ) {
			char before[RPI_TITLE_SIZE];
			G_RPI_Title( kinds, *n, before, sizeof( before ) );
			Com_sprintf( why, whySize, "%s %s the %s's and %s the %s's: an invasion's one side's - pick it for them all (side dark, light or creatures), "
				"or set them on each other (rp war)", before, ( *n > 1 ) ? "are" : "is", rpiSideNames[*side], kd->title, rpiSideNames[s] );
			return qfalse;
		}
		*side = s;
		width = Com_Clampi( 1, 256, width );	// (its body as NPC_ParseParms makes it: the bottom a person's, always)
		height = Com_Clampi( 8, 512, height );
		VectorSet( kd->mins, -width, -width, DEFAULT_MINS_2 );
		VectorSet( kd->maxs, width, width, height + DEFAULT_MINS_2 );
		kd->close = close;
		kd->each = each;
		( *n )++;
		if ( !comma ) {
			break;
		}
		p = comma + 1;
	}
	G_RPI_Title( kinds, *n, title, titleSize );
	return qtrue;
}

// How many a wave of an invasion's has: each type's own, and per for the mixed ones.
static int G_RPI_WaveSize( const rpiInvasion_t *inv ) {
	int k, n = 0;
	qboolean mixed = qfalse;

	for ( k = 0; k < inv->numKinds; k++ ) {
		n += inv->kinds[k].each;
		mixed = (qboolean)( mixed || !inv->kinds[k].each );
	}
	return n + ( mixed ? inv->per : 0 );
}

// A wave of an invasion's to come: of each type, its own number, or its share
// of per with the others mixed - evenly; what won't split, one more each, in
// turn from a wave to the next (inv->wave: this one's, from 1).
static void G_RPI_WaveOf( rpiInvasion_t *inv ) {
	int k, mixed = 0, m = 0, share, extra, first;

	for ( k = 0; k < inv->numKinds; k++ ) {
		mixed += inv->kinds[k].each ? 0 : 1;
	}
	share = mixed ? inv->per / mixed : 0;
	extra = mixed ? inv->per % mixed : 0;
	first = mixed ? ( inv->wave - 1 ) % mixed : 0;
	inv->toCome = 0;
	for ( k = 0; k < inv->numKinds; k++ ) {
		rpiKind_t *kd = &inv->kinds[k];
		if ( kd->each ) {
			kd->toCome = kd->each;
		} else {
			kd->toCome = share + ( ( ( m - first + mixed ) % mixed < extra ) ? 1 : 0 );
			m++;
		}
		inv->toCome += kd->toCome;
	}
}

// The type of an invasion's to come out next: one to come again first; else
// of the wave's, the one with most still to come (of as many, the first after
// the last to come out: mixed in, not one after another of a type). -1: none.
static int G_RPI_NextKind( const rpiInvasion_t *inv ) {
	int i, best = -1;

	for ( i = 0; i < inv->numKinds; i++ ) {
		if ( inv->kinds[i].again > 0 ) {
			return i;
		}
	}
	for ( i = 1; i <= inv->numKinds; i++ ) {
		const int k = ( inv->lastKind + i ) % inv->numKinds;
		if ( inv->kinds[k].toCome > 0 && ( best < 0 || inv->kinds[k].toCome > inv->kinds[best].toCome ) ) {
			best = k;
		}
	}
	return best;
}

/*
================================================================================
Its NPCs
================================================================================
*/

// An invader's NPC, if it's still that one (in use, its number, its type:
// NULL - gone).
static gentity_t *G_RPI_Npc( int s, const rpiInvader_t *o ) {
	gentity_t *npc;

	if ( o->ent < MAX_CLIENTS || o->ent >= ENTITYNUM_MAX_NORMAL ) {
		return NULL;
	}
	npc = &g_entities[o->ent];
	return ( npc->inuse && npc->rpInvader == o->tag && o->kind >= 0 && o->kind < rpi[s].numKinds && npc->NPC_type == rpi[s].kinds[o->kind].type &&
		npc->client && npc->NPC ) ? npc : NULL;
}

// Whether ent's an invader (one of an invasion's NPCs: up, or a body).
qboolean G_RPInvader( const gentity_t *ent ) {
	return (qboolean)( ent && ( ent->rpInvader & 3 ) && ent->NPC );
}

// Whether an invader's to fight ent in a war (G_RPNPCEnemy): 1, it's of
// another of the war's sides (each fights every other); 0, of its own;
// -1, the war has nothing to say - not both of its sides' (whom players
// fight is as their side has it: by their faction).
int G_RPWarEnemy( const gentity_t *npc, const gentity_t *ent ) {
	int a, b;

	if ( !rpiWar.on || !G_RPInvader( npc ) || !G_RPInvader( ent ) ) {
		return -1;
	}
	a = ( npc->rpInvader & 3 ) - 1;
	b = ( ent->rpInvader & 3 ) - 1;
	if ( a < 0 || a >= RPI_SIDES || b < 0 || b >= RPI_SIDES || !rpi[a].war || !rpi[b].war ) {
		return -1;
	}
	return ( a != b ) ? 1 : 0;
}

// NPC_Spawn_Do: whether a spawner's an invasion's (its NPC type, kept with
// the invasion, isn't copied: no string spent a spawn; its NPC, a slot an
// NPC's had before - G_RPCompEntity).
qboolean G_RPInvadeSpawner( const gentity_t *spawner ) {
	return (qboolean)( spawner && ( spawner->rpInvader & 3 ) && !spawner->NPC );
}

// An invader gone, now: its NPC (or body) freed, and what it held - whoever
// it holds, whoever holds it, let go of (G_RPLetGo).
static void G_RPI_Free( gentity_t *npc ) {
	G_RPLetGo( npc );
	if ( npc->NPC ) {
		if ( npc->health > 0 && npc->NPC->combatPoint >= 0 ) {
			NPC_FreeCombatPoint( npc->NPC->combatPoint, qfalse );	// (a body let go of its own as it died: another may hold it now)
			npc->NPC->combatPoint = -1;
		}
		if ( npc->NPC->group ) {
			AI_DeleteSelfFromGroup( npc );
		}
		if ( npc->NPC->tempGoal ) {
			G_FreeEntity( npc->NPC->tempGoal );
			npc->NPC->tempGoal = NULL;
		}
	}
	G_FreeEntity( npc );
}

// How many of an invasion's are up.
static int G_RPI_Up( int s ) {
	int k, n = 0;

	for ( k = 0; k < RPI_TRACK; k++ ) {
		n += ( rpi[s].out[k].ent >= 0 && !rpi[s].out[k].diedAt ) ? 1 : 0;
	}
	return n;
}

// ...and of every invasion's.
static int G_RPI_UpAll( void ) {
	int s, n = 0;

	for ( s = 0; s < RPI_SIDES; s++ ) {
		n += rpi[s].on ? G_RPI_Up( s ) : 0;
	}
	return n;
}

// Every NPC of an invasion's side gone: up, bodies, any it lost track of.
static void G_RPI_FreeAll( int s ) {
	int i;

	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *e = &g_entities[i];
		if ( e->inuse && e->NPC && ( e->rpInvader & 3 ) == s + 1 ) {
			G_RPI_Free( e );
		}
	}
	for ( i = 0; i < RPI_TRACK; i++ ) {
		rpi[s].out[i].ent = -1;
	}
}

// How many of an invasion's are up for a player (after them, or by them).
static int G_RPI_OutFor( int s, int client ) {
	int k, n = 0;

	for ( k = 0; k < RPI_TRACK; k++ ) {
		n += ( rpi[s].out[k].ent >= 0 && !rpi[s].out[k].diedAt && rpi[s].out[k].target == client ) ? 1 : 0;
	}
	return n;
}

// The player the next invader comes out for: of those it's after, the one
// fewest are out for; none - of those in the game, the one fewest are by (it
// waits there); none with somewhere near them lately (avoid) - -2; nobody in
// the game - -1.
static int G_RPI_PickTarget( int s ) {
	int i, best = -1, bestN = 0, host = -1, hostN = 0;
	qboolean anyone = qfalse, fair = qfalse;

	for ( i = 0; i < level.maxclients; i++ ) {
		const gentity_t *ent = &g_entities[i];
		int n;
		if ( !G_RPI_Alive( ent ) ) {
			continue;
		}
		anyone = qtrue;
		fair = (qboolean)( fair || G_RPI_Fair( s, ent ) );
		if ( level.time - rpi[s].avoid[i] < 0 ) {
			continue;	// (nowhere near them a moment ago)
		}
		n = G_RPI_OutFor( s, i );
		if ( host < 0 || n < hostN ) {
			host = i;
			hostN = n;
		}
		if ( G_RPI_Fair( s, ent ) && ( best < 0 || n < bestN ) ) {
			best = i;
			bestN = n;
		}
	}
	if ( best >= 0 ) {
		return best;
	}
	if ( fair || host < 0 ) {
		return anyone ? -2 : -1;	// (those it's after: nowhere near them yet - not by someone else, waiting)
	}
	return host;
}

static const float rpiAways[] = { 0, 30, -30, 60, -60, 90, -90, 120, -120, 150, -150, 180 };	// (from where its looking starts: behind them, for an invasion's first)
enum { RPI_FAR, RPI_MID, RPI_NEAR };
static const float rpiRings[3][4] = {
	{ 680.0f, 850.0f, 1020.0f, 595.0f },	// (far: 20, 25, 30, 17.5 m)
	{ 510.0f, 425.0f, 553.0f, 467.0f },		// (not as far: 15, 12.5, 16.3, 13.7 m)
	{ 340.0f, 306.0f, 374.0f, 272.0f }		// (near, where nowhere further will do: 10, 9, 11, 8 m)
};
static const float rpiHeights[] = { 0.0f, 32.0f, -32.0f, 64.0f, -64.0f };	// (on their level; higher ground or lower, stairs: each set down on the floor under it)
#define RPI_ROUNDS		4	// (round again, each time a little further round: more places, before ones less good)
#define RPI_PLACES		( (int)ARRAY_LEN( rpiAways ) * (int)ARRAY_LEN( rpiRings[0] ) * RPI_ROUNDS )	// (a pass's, all told)
#define RPI_SPREAD		137.5f	// degrees round from the last's an invasion's next for a player is looked for from first (never two the same way: they come from all round them)
#define RPI_SPREAD_EVERY	144		// (137.5 degrees 144 times: 55 times round - where they began; 36 times round the rings)

// The places an invader comes out at, best first - each pass all of them,
// every way round (RPI_PLACES): out of everyone's sight, where it can walk
// straight to them, 17.5 to 30 m from them; not as far (12.5 to 16.3 m); with
// nothing between - then, those all looked at, in sight, as they were; near
// (8 to 11 m) last of all, where nowhere further will do (they'd be on them at
// once). One that fights up close (a saber, fists, claws), where it can walk
// to them - in sight, near - before where it can't walk (a gun shoots from
// there; it'd stand there, getting nowhere).
typedef struct {
	qboolean	inSight;	// someone may see it there
	int			ring;		// rpiRings' (RPI_FAR...)
	qboolean	walk;		// it can walk straight to them (else: nothing between them)
} rpiPass_t;
static const rpiPass_t rpiPasses[2][8] = {
	{ { qfalse, RPI_FAR, qtrue }, { qfalse, RPI_MID, qtrue }, { qfalse, RPI_FAR, qfalse }, { qtrue, RPI_FAR, qtrue },
	  { qtrue, RPI_MID, qtrue }, { qtrue, RPI_FAR, qfalse }, { qfalse, RPI_NEAR, qtrue }, { qtrue, RPI_NEAR, qtrue } },	// (a gun)
	{ { qfalse, RPI_FAR, qtrue }, { qfalse, RPI_MID, qtrue }, { qtrue, RPI_FAR, qtrue }, { qtrue, RPI_MID, qtrue },
	  { qfalse, RPI_NEAR, qtrue }, { qtrue, RPI_NEAR, qtrue }, { qfalse, RPI_FAR, qfalse }, { qtrue, RPI_FAR, qfalse } }	// (up close)
};
#define RPI_PASSES		( (int)ARRAY_LEN( rpiPasses[0] ) )

// The k'th place an invader might come out at, in a pass, for a player
// standing at from looking along yaw, at their height (G_RPI_PlaceOk looks
// up and down from it): in a ring of its pass's (rpiRings), round again
// RPI_ROUNDS times - from behind them for an invasion's first for them, each
// after it (spread: how many came out for them before it, RPI_SPREAD_EVERY
// round) from further round (RPI_SPREAD) and a ring further on: all round
// them, near and far, not one after another behind them - every way round
// in a ring before the next ring. In sight, from behind them all the same
// (a ring on), every ring one way before the next way round: the place
// least in their sight first - not, its looking begun in front of them, in
// front of their eyes, nor a ring's way in front before another's behind.
static void G_RPI_PlaceAt( const vec3_t from, float yaw, int spread, const rpiPass_t *pass, int k, vec3_t at ) {
	const int dirs = (int)ARRAY_LEN( rpiAways ), rings = (int)ARRAY_LEN( rpiRings[0] );
	const int d = pass->inSight ? ( k / rings ) % dirs : k % dirs;
	const int r = ( ( pass->inSight ? k : k / dirs ) % rings + spread ) % rings, round = ( k / ( dirs * rings ) ) % RPI_ROUNDS;
	const float dist = rpiRings[pass->ring][r];
	const vec3_t a = { 0.0f, yaw + 180.0f + ( pass->inSight ? 0.0f : RPI_SPREAD * (float)spread ) + rpiAways[d] + 7.0f * (float)round, 0.0f };
	vec3_t f;

	AngleVectors( a, f, NULL, NULL );
	VectorMA( from, dist, f, at );
}

// Traces the invasions' looking has taken this frame: their own, and the
// companions' rules' (bodies fitting, ways walked, lines clear).
static int G_RPI_Spent( void ) {
	return rpiTraces + (int)( G_RPCompTraces() - rpiCompTraces );
}

// Where a player stands - under them, in the air (a jump, a jetpack, no-clip:
// the floor under them, RPI_UNDER at most): where a way to them ends.
static void G_RPI_Under( const gentity_t *player, vec3_t to ) {
	static const vec3_t feetMins = { -15, -15, DEFAULT_MINS_2 }, feetMaxs = { 15, 15, 0 };	// (a person's, to their middle: crouched too)
	trace_t tr;
	vec3_t down;

	VectorCopy( player->client->ps.origin, to );
	VectorCopy( to, down );
	down[2] -= RPI_UNDER;
	rpiTraces++;
	trap->Trace( &tr, to, feetMins, feetMaxs, down, player->s.number, MASK_PLAYERSOLID & ~CONTENTS_BODY, qfalse, 0, 0 );
	if ( !tr.startsolid && !tr.allsolid && tr.fraction < 1.0f ) {
		VectorCopy( tr.endpos, to );
	}
}

// Whether a spot a body of an invasion's type fits at will do for one coming
// out for a player (from: where they are; passEnt, them) or into the war
// (from: where it's fought): nobody's near, it's not on another just out,
// nobody sees it (a pass out of sight), and it can walk straight there (to:
// where they stand, the war's floor) - or there's nothing between (a pass's
// that doesn't walk).
static qboolean G_RPI_SpotOk( const rpiKind_t *kd, const vec3_t from, int passEnt, const vec3_t to, const rpiPass_t *pass, const vec3_t at ) {
	const float clear = RPI_CLEAR + Q_max( kd->maxs[0] - 15.0f, 0.0f );
	int i, t;

	for ( i = 0; i < level.maxclients; i++ ) {
		if ( G_RPI_InGame( &g_entities[i] ) && Distance( g_entities[i].client->ps.origin, at ) < clear ) {
			return qfalse;
		}
	}
	for ( t = 0; t < RPI_SIDES; t++ ) {
		for ( i = 0; i < RPI_TRACK; i++ ) {
			const gentity_t *other = ( rpi[t].on && !rpi[t].out[i].begun ) ? G_RPI_Npc( t, &rpi[t].out[i] ) : NULL;
			if ( other && Distance( other->r.currentOrigin, at ) < other->r.maxs[0] + kd->maxs[0] + RPI_APART ) {
				return qfalse;
			}
		}
	}
	if ( !pass->inSight && G_RPI_Seen( at, kd->maxs[2] ) ) {
		return qfalse;
	}
	if ( !pass->walk ) {
		return G_RPCompClear( from, at, passEnt );
	}
	return G_RPCompWalkable( at, to, kd->mins, kd->maxs, ENTITYNUM_NONE );
}

// Whether a place will do for an invader of a type coming out for a player
// or into the war (G_RPI_SpotOk's) - on that level there, or a stair or two
// up or down (rpiHeights): each floor a body fits on there (set down on it),
// the first that will; at: where it'd stand then.
static qboolean G_RPI_PlaceOk( const rpiKind_t *kd, const vec3_t from, int passEnt, const vec3_t to, const rpiPass_t *pass, vec3_t at ) {
	float floors[ARRAY_LEN( rpiHeights )];
	int h, n = 0, f;

	for ( h = 0; h < (int)ARRAY_LEN( rpiHeights ); h++ ) {
		vec3_t spot;
		VectorCopy( at, spot );
		spot[2] += rpiHeights[h];
		if ( !G_RPCompFits( spot, kd->mins, kd->maxs, ENTITYNUM_NONE ) ) {
			continue;
		}
		for ( f = 0; f < n && fabsf( floors[f] - spot[2] ) >= 1.0f; f++ ) {
		}
		if ( f < n ) {
			continue;	// (a floor already looked at, from another height)
		}
		floors[n++] = spot[2];
		if ( G_RPI_SpotOk( kd, from, passEnt, to, pass, spot ) ) {
			VectorCopy( spot, at );
			return qtrue;
		}
		if ( !h ) {
			return qfalse;	// (a body fits on its level: there's no other floor there - higher, lower, it's that one or none)
		}
	}
	return qfalse;
}

static void G_RPI_LookAgain( rpiInvasion_t *inv );

// Where the next invader, of a type, comes out for a player (at), as far as
// this frame's looking goes: its places, the best first (rpiPasses), where
// it's got to kept with the invasion - for that player, for one that fights
// as that one does (another's, from the first: G_RPI_LookAgain). All looked
// at, and nowhere - they're avoided a while (another's come out for).
static qboolean G_RPI_Find( int s, const rpiKind_t *kd, const gentity_t *player, vec3_t at ) {
	rpiInvasion_t *inv = &rpi[s];
	vec3_t eye, dir, to;
	float yaw;

	if ( inv->lookFor != player->s.number || inv->lookClose != kd->close ) {
		G_RPI_LookAgain( inv );	// (not where another's looking got to)
		inv->lookFor = player->s.number;
		inv->lookClose = kd->close;
	}
	G_RPI_Look( player, eye, dir );
	yaw = vectoyaw( dir );
	G_RPI_Under( player, to );
	while ( G_RPI_Spent() < RPI_TRACES_FRAME ) {
		const rpiPass_t *pass = &rpiPasses[kd->close][inv->pass];
		G_RPI_PlaceAt( to, yaw, inv->spread[player->s.number], pass, inv->turn, at );	// (round where they stand: in the air, round the floor under them)
		inv->turn = ( inv->turn + 1 ) % RPI_PLACES;
		if ( G_RPI_PlaceOk( kd, player->client->ps.origin, player->s.number, to, pass, at ) ) {
			return qtrue;
		}
		if ( --inv->passLeft <= 0 ) {
			inv->passLeft = RPI_PLACES;	// (a pass all looked at ends where it began, the best first: the next's too)
			if ( ++inv->pass >= RPI_PASSES ) {
				inv->pass = 0;
				inv->avoid[player->s.number] = level.time + RPI_AVOID;	// (nowhere near them: another, a while)
				return qfalse;
			}
		}
	}
	return qfalse;
}

// The places a side comes into the war at: round where it's fought, its own
// way in (rpiWar.way) and up to 60 degrees either side of it, 12.5 to 20 m
// off (not as far, 9 to 12 m; near, 6 to 8 m: the passes' rings) - each one
// after the last that came in further along its way in, a ring further out or
// in (warSpread): a line of them, not one after another on one spot.
static const float rpiWarAways[] = { 0, 20, -20, 40, -40, 60, -60 };
static const float rpiWarRings[3][4] = {
	{ 510.0f, 595.0f, 680.0f, 425.0f },	// (far: 15, 17.5, 20, 12.5 m)
	{ 340.0f, 374.0f, 306.0f, 408.0f },	// (not as far: 10, 11, 9, 12 m)
	{ 238.0f, 272.0f, 204.0f, 255.0f }	// (near, where nowhere further will do: 7, 8, 6, 7.5 m)
};
#define RPI_WAR_PLACES	( (int)ARRAY_LEN( rpiWarAways ) * (int)ARRAY_LEN( rpiWarRings[0] ) * RPI_ROUNDS )

static void G_RPI_WarPlaceAt( int s, const rpiPass_t *pass, int k, vec3_t at ) {
	const int dirs = (int)ARRAY_LEN( rpiWarAways ), rings = (int)ARRAY_LEN( rpiWarRings[0] ), spread = rpi[s].warSpread;
	const int d = ( k % dirs + spread ) % dirs, r = ( ( k / dirs ) % rings + spread / dirs ) % rings, round = ( k / ( dirs * rings ) ) % RPI_ROUNDS;
	const vec3_t a = { 0.0f, rpiWar.way[s] + rpiWarAways[d] + 5.0f * (float)round, 0.0f };
	vec3_t f;

	AngleVectors( a, f, NULL, NULL );
	VectorMA( rpiWar.floor, rpiWarRings[pass->ring][r], f, at );
}

// Where the next of a side's, of a type, comes into the war (at), as far as
// this frame's looking goes (as G_RPI_Find for a player: where it's fought,
// its way in). All looked at, and nowhere: it waits a while.
static qboolean G_RPI_WarFind( int s, const rpiKind_t *kd, vec3_t at ) {
	rpiInvasion_t *inv = &rpi[s];

	if ( inv->lookFor != RPI_LOOK_WAR || inv->lookClose != kd->close ) {
		G_RPI_LookAgain( inv );
		inv->pass = inv->warPass[kd->close];
		inv->lookFor = RPI_LOOK_WAR;
		inv->lookClose = kd->close;
	}
	while ( G_RPI_Spent() < RPI_TRACES_FRAME ) {
		const rpiPass_t *pass = &rpiPasses[kd->close][inv->pass];
		G_RPI_WarPlaceAt( s, pass, inv->turn, at );
		inv->turn = ( inv->turn + 1 ) % RPI_WAR_PLACES;
		if ( G_RPI_PlaceOk( kd, rpiWar.at, ENTITYNUM_NONE, rpiWar.floor, pass, at ) ) {
			return qtrue;
		}
		if ( --inv->passLeft <= 0 ) {
			inv->passLeft = RPI_WAR_PLACES;
			if ( ++inv->pass >= RPI_PASSES ) {
				inv->pass = 0;
				inv->warAvoid = level.time + RPI_AVOID;	// (nowhere on its way in: a while, then again)
				return qfalse;
			}
		}
	}
	return qfalse;
}

// An invasion's looking for where the next comes out begun again, from the
// best place.
static void G_RPI_LookAgain( rpiInvasion_t *inv ) {
	inv->pass = inv->turn = 0;
	inv->passLeft = inv->war ? RPI_WAR_PLACES : RPI_PLACES;
}

// Whether the level has room for another invader (G_RPCompRoom: it reads
// every configstring) - looked at once a frame, and again after one's come out.
static qboolean G_RPI_Room( void ) {
	if ( rpiRoomAt != level.time ) {
		rpiRoomAt = level.time;
		rpiRoomOk = G_RPCompRoom( RPI_ROOM_ENTS );
	}
	return rpiRoomOk;
}

// Somewhere to keep track of another of an invasion's. NULL: none (can't be:
// RPI_MOST up at most and RPI_BODIES lying there - and a frame's dead, and
// RPI_PER_FRAME more - of RPI_TRACK kept).
static rpiInvader_t *G_RPI_Slot( int s ) {
	int k;

	for ( k = 0; k < RPI_TRACK; k++ ) {
		if ( rpi[s].out[k].ent < 0 ) {
			return &rpi[s].out[k];
		}
	}
	return NULL;
}

// Brings an invader of an invasion's, of a type, out at at, facing yaw, for a
// player (or by them, waiting; -1, into the war). qfalse: it couldn't come
// out (its type won't).
static qboolean G_RPI_Spawn( int s, int kind, int target, const vec3_t at, float yaw ) {
	rpiInvasion_t *inv = &rpi[s];
	rpiInvader_t *o = G_RPI_Slot( s );
	gentity_t *sp, *npc;
	int tag;

	if ( !o ) {
		return qtrue;	// (can't be)
	}
	rpiSerial = ( rpiSerial + 1 ) & 0x1fffffff;
	if ( !rpiSerial ) {
		rpiSerial = 1;
	}
	tag = ( rpiSerial << 2 ) | ( s + 1 );
	sp = G_Spawn();
	sp->classname = "rp_invader_spawner";
	sp->think = G_FreeEntity;	// (NPC_Spawn_Do frees it - but where it gives up half way)
	sp->nextthink = level.time + FRAMETIME;
	VectorCopy( at, sp->s.origin );
	G_SetOrigin( sp, sp->s.origin );
	sp->s.angles[YAW] = yaw;
	sp->NPC_type = inv->kinds[kind].type;	// (the NPC's own: G_RPInvadeSpawner)
	sp->NPC_targetname = (char *)rpiTags[s];
	sp->count = 1;
	sp->rpInvader = tag;
	npc = NPC_Spawn_Do( sp );
	if ( npc && ( !npc->NPC || !npc->client ) ) {
		G_FreeEntity( npc );	// (half made: NPC_Spawn_Do ran out of something)
		npc = NULL;
	}
	if ( !npc ) {
		return qfalse;
	}
	npc->rpInvader = tag;
	npc->rpSide = inv->picked ? rpiTeams[s] + 1 : 0;	// (the side picked: as it begins - G_RPSideBegin)
	memset( o, 0, sizeof( *o ) );
	o->ent = npc->s.number;
	o->tag = tag;
	o->kind = kind;
	o->target = target;
	o->near = -1;
	o->bornAt = level.time ? level.time : 1;
	VectorCopy( at, o->lastAt );
	o->lastAtTime = level.time;
	return qtrue;
}

static void G_RPI_WarOff( const char *screen, const char *console, const char *log );

// Whether anyone's alive in the game (a war's waves wait for someone).
static qboolean G_RPI_Anyone( void ) {
	int i;

	for ( i = 0; i < level.maxclients; i++ ) {
		if ( G_RPI_Alive( &g_entities[i] ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

// An invasion's next invaders out, a few a frame - those to come again
// first, then the wave's, its types mixed in (G_RPI_NextKind) - as many up as
// it allows (RPI_MOST all told), where the level has room, for the players
// they're for (a war's: on its way in), as far as the frame's looking goes.
static void G_RPI_Bring( int s ) {
	rpiInvasion_t *inv = &rpi[s];
	int brought = 0;

	while ( brought < RPI_PER_FRAME && ( inv->again > 0 || inv->toCome > 0 ) && level.time - inv->tryAfter >= 0 && G_RPI_Spent() < RPI_TRACES_FRAME ) {
		const gentity_t *player = NULL;
		vec3_t at, to;
		int target = -1;
		const int kind = G_RPI_NextKind( inv );
		rpiKind_t *kd;
		if ( kind < 0 ) {
			inv->toCome = inv->again = 0;	// (can't be: each type's, together)
			return;
		}
		kd = &inv->kinds[kind];
		if ( G_RPI_Up( s ) >= inv->cap || G_RPI_UpAll() >= RPI_MOST ) {
			inv->waiting = RPIW_NONE;
			return;	// (one goes down, another comes)
		}
		if ( inv->war ) {
			if ( !rpiWar.on ) {
				return;	// (the war's over: its winners stay a while, no more come)
			}
			if ( !G_RPI_Anyone() ) {
				inv->waiting = RPIW_NOBODY;
				return;
			}
			if ( level.time - inv->warAvoid < 0 ) {
				inv->waiting = RPIW_NOWHERE;
				return;
			}
			if ( !G_RPI_WarFind( s, kd, at ) ) {
				if ( level.time - inv->warAvoid < 0 ) {
					inv->waiting = RPIW_NOWHERE;
				}
				return;	// (looking on, next frame)
			}
		} else {
			target = G_RPI_PickTarget( s );
			if ( target < 0 ) {
				inv->waiting = ( target == -1 ) ? RPIW_NOBODY : RPIW_NOWHERE;
				return;
			}
			player = &g_entities[target];
			if ( !G_RPI_Find( s, kd, player, at ) ) {
				if ( level.time - inv->avoid[target] < 0 ) {
					continue;	// (nowhere near them: another - this frame, if there's looking left)
				}
				return;	// (looking on, next frame)
			}
		}
		if ( !G_RPI_Room() ) {
			inv->waiting = RPIW_ROOM;
			inv->tryAfter = level.time + RPI_ROOM_WAIT;
			G_RPI_LookAgain( inv );	// (all changed by then)
			return;
		}
		VectorSubtract( player ? player->client->ps.origin : rpiWar.at, at, to );	// (facing them, or where the war is)
		if ( !G_RPI_Spawn( s, kind, target, at, vectoyaw( to ) ) ) {
			char screen[MAX_STRING_CHARS], console[MAX_STRING_CHARS], log[MAX_STRING_CHARS];
			if ( inv->war ) {
				Com_sprintf( screen, sizeof( screen ), "The war is off" );
				Com_sprintf( console, sizeof( console ), "%s couldn't come out here: the war is off", kd->title );
				Com_sprintf( log, sizeof( log ), "War: %s couldn't come out (NPC type %s) - it's off\n", kd->title, kd->type );
				G_RPI_WarOff( screen, console, log );
				return;
			}
			G_LogPrintf( "Invasion: %s couldn't come out (NPC type %s) - it's off\n", kd->title, kd->type );
			Com_sprintf( screen, sizeof( screen ), "The %s invasion is off", inv->title );
			Com_sprintf( console, sizeof( console ), "%s couldn't come out here: the invasion is off", kd->title );
			G_RPI_Announce( screen, console );
			G_RPI_FreeAll( s );
			inv->on = qfalse;
			rpiTellTime = 0;
			return;
		}
		rpiRoomAt = -1;	// (the level's room: looked at again for the next)
		if ( kd->again > 0 ) {
			kd->again--;
			inv->again--;
		} else {
			kd->toCome--;
			inv->toCome--;
		}
		inv->lastKind = kind;
		if ( inv->war ) {
			inv->warSpread = ( inv->warSpread + 1 ) % ( (int)ARRAY_LEN( rpiWarAways ) * (int)ARRAY_LEN( rpiWarRings[0] ) );	// (the next further along its way in)
		} else {
			inv->spread[target] = ( inv->spread[target] + 1 ) % RPI_SPREAD_EVERY;	// (the next for them looked for from further round: not behind them after this one)
		}
		if ( inv->war ) {
			inv->warPass[kd->close] = inv->pass;	// (where this one was found: the next of the wave from there - G_RPI_WarFind)
		}
		G_RPI_LookAgain( inv );	// (the next from the best place: this one's taken - not begun, it's apart from it; begun, its body's there)
		if ( inv->war ) {
			inv->pass = inv->warPass[kd->close];
		}
		inv->waiting = RPIW_NONE;
		brought++;
	}
}

/*
================================================================================
Each invader, a frame
================================================================================
*/

// The nearest of those an invader's after: a player its side fights - and in
// a war, of the other sides' up (begun, alive: every other side's its foe) -
// their entity number (-1: nobody), how far.
static int G_RPI_Nearest( int s, const gentity_t *npc, float *dist ) {
	int i, t, best = -1;

	*dist = 0.0f;
	for ( i = 0; i < level.maxclients; i++ ) {
		const gentity_t *ent = &g_entities[i];
		float d;
		if ( !G_RPI_Fair( s, ent ) || !G_RPNPCEnemy( npc, ent ) ) {
			continue;
		}
		d = Distance( npc->r.currentOrigin, ent->r.currentOrigin );
		if ( best < 0 || d < *dist ) {
			best = i;
			*dist = d;
		}
	}
	if ( !rpi[s].war || !rpiWar.on ) {
		return best;
	}
	for ( t = 0; t < RPI_SIDES; t++ ) {
		if ( t == s || !rpi[t].on || !rpi[t].war ) {
			continue;
		}
		for ( i = 0; i < RPI_TRACK; i++ ) {
			const rpiInvader_t *f = &rpi[t].out[i];
			const gentity_t *foe = ( f->ent >= 0 && f->begun && !f->diedAt ) ? G_RPI_Npc( t, f ) : NULL;
			float d;
			if ( !foe || foe->health <= 0 || ( foe->flags & FL_NOTARGET ) ) {
				continue;
			}
			d = Distance( npc->r.currentOrigin, foe->r.currentOrigin );
			if ( best < 0 || d < *dist ) {
				best = foe->s.number;
				*dist = d;
			}
		}
	}
	return best;
}

// One that's begun: set going, as an NPC of its type coming for someone -
// chasing whom it's after, looking for more - after the player it came out
// for (one waiting: whoever comes); into a war, the nearest of the others'.
static void G_RPI_Begun( int s, gentity_t *npc, rpiInvader_t *o ) {
	gNPC_t *info = npc->NPC;

	info->scriptFlags |= SCF_CHASE_ENEMIES | SCF_LOOK_FOR_ENEMIES;
	info->scriptFlags &= ~( SCF_IGNORE_ALERTS | SCF_FORCED_MARCH );
	if ( info->defaultBehavior == BS_CINEMATIC ) {
		info->defaultBehavior = BS_DEFAULT;	// (some come cinematic: NPC_SetMiscDefaultData)
	}
	if ( info->behaviorState == BS_CINEMATIC ) {
		info->behaviorState = BS_DEFAULT;
	}
	if ( info->tempBehavior == BS_CINEMATIC ) {
		info->tempBehavior = BS_DEFAULT;
	}
	if ( o->target >= 0 && G_RPI_Fair( s, &g_entities[o->target] ) ) {
		G_SetEnemy( npc, &g_entities[o->target] );
	} else if ( rpi[s].war ) {
		float d;
		const int foe = G_RPI_Nearest( s, npc, &d );
		if ( foe >= 0 ) {
			G_SetEnemy( npc, &g_entities[foe] );
		}
	}
	o->begun = qtrue;
}

// Whether an invader's in a fight it can get on with: its enemy in reach (a
// saber, fists, a creature: by it; a gun: in its sight).
static qboolean G_RPI_Fighting( const gentity_t *npc ) {
	const gentity_t *enemy = npc->enemy;
	const int w = npc->client->ps.weapon;
	vec3_t eye, there;

	if ( !enemy || !enemy->inuse || !enemy->client ) {
		return qfalse;
	}
	if ( w == WP_SABER || w == WP_MELEE || w == WP_STUN_BATON || w == WP_NONE ) {
		return (qboolean)( Distance( npc->r.currentOrigin, enemy->r.currentOrigin ) <= RPI_REACH + npc->r.maxs[0] + enemy->r.maxs[0] );
	}
	VectorCopy( npc->r.currentOrigin, eye );
	eye[2] += npc->client->ps.viewheight;
	VectorCopy( enemy->r.currentOrigin, there );
	return G_RPI_Sees( eye, there, npc->s.number );
}

// An invader, a frame: gone (the level's), dead (its body: RPI_BODY on, gone),
// begun (set going), after nobody (the nearest of those it's after), getting
// nowhere or lost (out of everyone's sight: elsewhere).
static void G_RPI_Invader( int s, int k ) {
	rpiInvasion_t *inv = &rpi[s];
	rpiInvader_t *o = &inv->out[k];
	gentity_t *npc = G_RPI_Npc( s, o );

	if ( !npc ) {
		o->ent = -1;	// (gone from under us: the level's - a body it took away, a pit)
		return;
	}
	if ( o->diedAt ) {
		if ( level.time - o->diedAt >= RPI_BODY ) {
			G_RPI_Free( npc );
			o->ent = -1;
		}
		return;
	}
	if ( !o->begun ) {
		// (not begun, it's no health yet - and nothing can hurt it: no body, unseen)
		if ( npc->think != NPC_Begin ) {
			G_RPI_Begun( s, npc, o );
		} else if ( level.time - o->bornAt >= RPI_BEGIN_WAIT ) {
			G_RPI_Free( npc );	// (someone stood where it'd be all this while)
			o->ent = -1;
			inv->kinds[o->kind].again++;
			inv->again++;
		}
		return;
	}
	if ( npc->health <= 0 || npc->client->ps.pm_type == PM_DEAD ) {
		o->diedAt = level.time ? level.time : 1;
		inv->killed++;
		return;	// (its goal, its squad and its combat point the game let go of as it died: player_die)
	}
	if ( !o->scanTime || level.time - o->scanTime >= RPI_SCAN ) {
		o->scanTime = level.time ? level.time : 1;
		o->near = G_RPI_Nearest( s, npc, &o->nearDist );
		if ( !npc->enemy && o->near >= 0 ) {
			G_SetEnemy( npc, &g_entities[o->near] );
		}
	}
	// getting anywhere? (looked at once a RPI_STUCK_STEP: then, getting nowhere or lost, whether it can go)
	if ( level.time - o->lastAtTime < RPI_STUCK_STEP ) {
		return;
	}
	if ( Distance( npc->r.currentOrigin, o->lastAt ) >= RPI_STUCK_MOVE || G_RPI_Fighting( npc ) || o->near < 0 ) {
		o->stuckSince = 0;	// (moving, fighting - or nobody it's after: it waits)
		VectorCopy( npc->r.currentOrigin, o->lastAt );
	} else if ( !o->stuckSince ) {
		o->stuckSince = o->lastAtTime ? o->lastAtTime : 1;
	}
	o->lastAtTime = level.time;
	if ( o->near >= 0 && ( ( o->stuckSince && level.time - o->stuckSince >= RPI_STUCK ) || ( o->nearDist > RPI_LOST && level.time - o->bornAt >= RPI_STUCK ) ) &&
		!G_RPI_Seen( npc->r.currentOrigin, npc->r.maxs[2] ) ) {
		G_RPI_Free( npc );	// (out of everyone's sight: elsewhere, somewhere better)
		o->ent = -1;
		inv->kinds[o->kind].again++;
		inv->again++;
	}
}

/*
================================================================================
Waves
================================================================================
*/

// "wave 2 of 4", "wave 2" (until it's stopped).
static void G_RPI_WaveText( const rpiInvasion_t *inv, char *out, int size ) {
	if ( inv->waves > 0 ) {
		Com_sprintf( out, size, "wave %i of %i", inv->wave, inv->waves );
	} else {
		Com_sprintf( out, size, "wave %i", inv->wave );
	}
}

// The next wave of an invasion's (each type's of it: G_RPI_WaveOf) - everyone
// told; a war side's, after its first: its reinforcements (its first, the
// war's: G_RPI_WarStart).
static void G_RPI_NextWave( int s ) {
	rpiInvasion_t *inv = &rpi[s];
	char wave[64], screen[MAX_STRING_CHARS], console[MAX_STRING_CHARS];

	inv->wave++;
	G_RPI_WaveOf( inv );
	inv->waveAt = level.time ? level.time : 1;
	inv->arrivedAt = inv->beatenAt = 0;
	inv->warPass[0] = inv->warPass[1] = 0;	// (a war's: each wave from the best places)
	G_RPI_LookAgain( inv );
	G_RPI_WaveText( inv, wave, sizeof( wave ) );
	if ( inv->war ) {
		if ( inv->wave > 1 ) {
			Com_sprintf( screen, sizeof( screen ), "%s reinforcements\n%c%s", inv->title, toupper( wave[0] ), wave + 1 );
			Com_sprintf( console, sizeof( console ), "The %s's reinforcements, %s: %s", rpiSideNames[s], inv->title, wave );
			G_RPI_Announce( screen, console );
		}
		G_LogPrintf( "War: the %s's %s, %s (%i up)\n", rpiSideNames[s], inv->title, wave, G_RPI_Up( s ) );
	} else {
		Com_sprintf( screen, sizeof( screen ), "%s invasion\n%c%s", inv->title, toupper( wave[0] ), wave + 1 );
		Com_sprintf( console, sizeof( console ), "%s invasion: %s", inv->title, wave );
		G_RPI_Announce( screen, console );
		G_LogPrintf( "Invasion: %s, %s (%i up)\n", inv->title, wave, G_RPI_Up( s ) );
	}
	rpiTellTime = 0;
}

// An invasion gone, now, without a word (a war's sides: the war's told).
static void G_RPI_Quit( int s ) {
	G_RPI_FreeAll( s );
	rpi[s].on = rpi[s].war = rpi[s].lost = qfalse;
	rpi[s].endAt = 0;
	rpiTellTime = 0;
}

// An invasion over: everyone told (how), the log, what's left of it gone.
static void G_RPI_End( int s, const char *how, const char *by ) {
	rpiInvasion_t *inv = &rpi[s];
	char screen[MAX_STRING_CHARS], console[MAX_STRING_CHARS];

	Com_sprintf( screen, sizeof( screen ), "The %s invasion %s", inv->title, how );
	Com_sprintf( console, sizeof( console ), "The %s invasion %s (%i of them killed)", inv->title, how, inv->killed );
	G_RPI_Announce( screen, console );
	G_LogPrintf( "Invasion: %s %s%s%s - %i killed, %i wave%s\n", inv->title, how, by ? " by " : "", by ? by : "", inv->killed, inv->wave, ( inv->wave == 1 ) ? "" : "s" );
	G_RPI_Quit( s );
}

// The war's sides' killed: "the light side's 12, the dark side's 20" (of the
// sides in it - and so in the order they're in rpi).
static void G_RPI_WarKilled( char *out, int size ) {
	int s, n = 0, k = 0;

	out[0] = '\0';
	for ( s = 0; s < RPI_SIDES; s++ ) {
		n += ( rpiWar.sides & ( 1 << s ) ) ? 1 : 0;
	}
	for ( s = 0; s < RPI_SIDES; s++ ) {
		if ( !( rpiWar.sides & ( 1 << s ) ) ) {
			continue;
		}
		Q_strcat( out, size, va( "%sthe %s's %i", !k ? "" : ( k == n - 1 ) ? " and " : ", ", rpiSideNames[s], rpi[s].killed ) );
		k++;
	}
}

// The war over, now - called off, or one of it couldn't come out: everyone
// told (screen, console), the log; every side of it gone, without a word.
static void G_RPI_WarOff( const char *screen, const char *console, const char *log ) {
	int s;

	G_RPI_Announce( screen, console );
	G_LogPrintf( "%s", log );
	for ( s = 0; s < RPI_SIDES; s++ ) {
		if ( rpiWar.sides & ( 1 << s ) ) {
			G_RPI_Quit( s );
		}
	}
	memset( &rpiWar, 0, sizeof( rpiWar ) );
	rpiTellTime = 0;
}

// The war called off (by: whose command; NULL - none's).
static void G_RPI_WarCallOff( const char *by ) {
	char killed[256], console[MAX_STRING_CHARS], log[MAX_STRING_CHARS];

	G_RPI_WarKilled( killed, sizeof( killed ) );
	Com_sprintf( console, sizeof( console ), "The war is called off (killed: %s)", killed );
	Com_sprintf( log, sizeof( log ), "War: called off%s%s - killed: %s\n", by ? " by " : "", by ? by : "", killed );
	G_RPI_WarOff( "The war is called off", console, log );
}

// The war, a frame: one side left standing (its last wave not beaten), it's
// won - everyone told, its own staying RPI_WAR_AFTER (no more come), the
// others' bodies as long as bodies do; none left, a draw.
static void G_RPI_WarFrame( void ) {
	char killed[256], screen[MAX_STRING_CHARS], console[MAX_STRING_CHARS];
	int s, standing = 0, winner = -1;

	if ( !rpiWar.on ) {
		return;
	}
	for ( s = 0; s < RPI_SIDES; s++ ) {
		if ( ( rpiWar.sides & ( 1 << s ) ) && rpi[s].on && !rpi[s].lost ) {
			standing++;
			winner = s;
		}
	}
	if ( standing > 1 ) {
		return;
	}
	G_RPI_WarKilled( killed, sizeof( killed ) );
	if ( standing == 1 ) {
		Com_sprintf( screen, sizeof( screen ), "The %s %s the war", rpiSideNames[winner], ( winner == RPI_CREATURES ) ? "win" : "wins" );
		Com_sprintf( console, sizeof( console ), "The war's won by the %s's %s (killed: %s)", rpiSideNames[winner], rpi[winner].title, killed );
		G_LogPrintf( "War: won by the %s's %s - killed: %s\n", rpiSideNames[winner], rpi[winner].title, killed );
	} else {
		Q_strncpyz( screen, "The war is over: nobody's left standing", sizeof( screen ) );
		Com_sprintf( console, sizeof( console ), "The war is over: nobody's left standing (killed: %s)", killed );
		G_LogPrintf( "War: over, nobody left standing - killed: %s\n", killed );
	}
	G_RPI_Announce( screen, console );
	for ( s = 0; s < RPI_SIDES; s++ ) {
		if ( !( rpiWar.sides & ( 1 << s ) ) ) {
			continue;
		}
		if ( s == winner ) {
			int k;
			rpi[s].endAt = ( level.time + RPI_WAR_AFTER ) ? level.time + RPI_WAR_AFTER : 1;	// (they stay a while: no more come - G_RPI_Bring - nor waves)
			rpi[s].toCome = rpi[s].again = 0;
			for ( k = 0; k < rpi[s].numKinds; k++ ) {
				rpi[s].kinds[k].toCome = rpi[s].kinds[k].again = 0;
			}
		} else {
			rpi[s].lost = qtrue;	// (the rest, beaten: their bodies lie there their while - G_RPI_Frame - then they go)
		}
	}
	memset( &rpiWar, 0, sizeof( rpiWar ) );
	rpiTellTime = 0;
}

// Whether an invasion's there for something: anyone in the game it's after
// (G_RPI_Foe); one of it fighting (an NPC, say: Rebels the Reborn); the other
// side's invasion on (between its waves: Rebels by the heroes, the Reborn to
// come) - or nobody in the game at all (it waits, as its wave does).
static qboolean G_RPI_Busy( int s ) {
	qboolean anyone = qfalse;
	int i;

	if ( rpi[s].war ) {
		return qtrue;	// (a war's: over as the war is)
	}
	for ( i = 0; i < level.maxclients; i++ ) {
		if ( G_RPI_Foe( s, &g_entities[i] ) ) {
			return qtrue;
		}
		anyone = (qboolean)( anyone || G_RPI_InGame( &g_entities[i] ) );
	}
	if ( !anyone || ( s != RPI_CREATURES && rpi[RPI_DARK + RPI_LIGHT - s].on ) ) {
		return qtrue;
	}
	for ( i = 0; i < RPI_TRACK; i++ ) {
		const rpiInvader_t *o = &rpi[s].out[i];
		const gentity_t *npc = ( o->begun && !o->diedAt ) ? G_RPI_Npc( s, o ) : NULL;
		if ( npc && npc->enemy && npc->enemy->inuse && npc->enemy->health > 0 ) {
			return qtrue;
		}
	}
	return qfalse;
}

// An invasion, a frame: its invaders, its waves - the next one when it's
// time; the last beaten, nobody there for it RPI_IDLE, or its time up: over.
// A war's side: its last beaten, out of it (G_RPI_WarFrame: its bodies lie
// there till the war's over); the war won, its own gone RPI_WAR_AFTER on.
static void G_RPI_Frame( int s ) {
	rpiInvasion_t *inv = &rpi[s];
	qboolean last;
	int k;

	for ( k = 0; k < RPI_TRACK; k++ ) {
		if ( inv->out[k].ent >= 0 ) {
			G_RPI_Invader( s, k );
		}
	}
	if ( inv->endAt ) {
		if ( level.time - inv->endAt >= 0 ) {
			G_RPI_Quit( s );	// (the war's winners: gone)
		}
		return;
	}
	if ( inv->lost ) {
		for ( k = 0; k < RPI_TRACK && inv->out[k].ent < 0; k++ ) {
		}
		if ( !rpiWar.on && k >= RPI_TRACK ) {
			G_RPI_Quit( s );	// (beaten, the war over: its bodies gone, it is)
		}
		return;
	}
	if ( inv->toCome <= 0 && inv->again <= 0 && !G_RPI_Up( s ) && !inv->beatenAt ) {
		inv->beatenAt = level.time ? level.time : 1;
	}
	last = (qboolean)( inv->waves > 0 && inv->wave >= inv->waves );
	if ( last && inv->beatenAt ) {
		if ( inv->war ) {
			inv->lost = qtrue;	// (out of the war)
			G_LogPrintf( "War: the %s's %s beaten - %i killed\n", rpiSideNames[s], inv->title, inv->killed );
			rpiTellTime = 0;
			return;
		}
		G_RPI_End( s, "is beaten", NULL );
		return;
	}
	if ( G_RPI_Busy( s ) ) {
		inv->idleSince = 0;
	} else if ( !inv->idleSince ) {
		inv->idleSince = level.time ? level.time : 1;
	} else if ( level.time - inv->idleSince >= RPI_IDLE ) {
		G_RPI_End( s, "is over: nobody to fight", NULL );
		return;
	}
	if ( last && !inv->war && inv->arrivedAt && level.time - inv->arrivedAt >= RPI_LEFTOVER ) {
		G_RPI_End( s, "is over", NULL );
		return;
	}
	if ( !last && inv->toCome <= 0 && ( inv->timed ? ( level.time - inv->waveAt >= inv->wait * 1000 ) :
		( inv->beatenAt && level.time - inv->beatenAt >= inv->wait * 1000 ) ) ) {
		G_RPI_NextWave( s );
	}
}

// ...and its next invaders out.
static void G_RPI_Bringing( int s ) {
	rpiInvasion_t *inv = &rpi[s];

	G_RPI_Bring( s );
	if ( inv->on && inv->toCome <= 0 && !inv->arrivedAt ) {
		inv->arrivedAt = level.time ? level.time : 1;	// (the wave all out: from now, as admins are told this frame)
	}
}

// Invaders' bodies past RPI_BODIES, every invasion's together: the oldest
// gone, now (entities the level needs, an NPC's memory a slot keeps).
static void G_RPI_Bodies( void ) {
	while ( 1 ) {
		rpiInvader_t *oldest = NULL;
		int s, k, side = 0, n = 0;
		for ( s = 0; s < RPI_SIDES; s++ ) {
			for ( k = 0; k < RPI_TRACK; k++ ) {
				rpiInvader_t *o = &rpi[s].out[k];
				if ( o->ent >= 0 && o->diedAt ) {
					n++;
					if ( !oldest || o->diedAt - oldest->diedAt < 0 ) {
						oldest = o;
						side = s;
					}
				}
			}
		}
		if ( n <= RPI_BODIES ) {
			return;
		}
		{
			gentity_t *npc = G_RPI_Npc( side, oldest );
			if ( npc ) {
				G_RPI_Free( npc );
			}
			oldest->ent = -1;
		}
	}
}

/*
================================================================================
Admins told how they go (rpinv)
================================================================================
*/

// How an invasion goes: a line for the Admin page.
static void G_RPI_Line( int s, char *out, int size ) {
	const rpiInvasion_t *inv = &rpi[s];
	const int up = G_RPI_Up( s ), coming = inv->toCome + inv->again;
	const qboolean last = (qboolean)( inv->waves > 0 && inv->wave >= inv->waves );
	char wave[64], how[128];
	qboolean timer = qfalse;	// (a time till what's next...)
	int left = 0;				// (...in ms)

	G_RPI_WaveText( inv, wave, sizeof( wave ) );
	if ( inv->endAt ) {
		timer = qtrue;
		left = inv->endAt - level.time;
		Com_sprintf( how, sizeof( how ), "the war won, %i up - gone in", up );
	} else if ( inv->lost ) {
		Com_sprintf( how, sizeof( how ), "beaten: out of the war (%i killed)", inv->killed );
	} else if ( inv->idleSince ) {
		timer = qtrue;
		left = RPI_IDLE - ( level.time - inv->idleSince );
		Com_sprintf( how, sizeof( how ), "%i up - nobody to fight: over in", up );
	} else if ( coming && inv->waiting == RPIW_NOBODY ) {
		Q_strncpyz( how, "nobody in the game: waiting", sizeof( how ) );
	} else if ( coming && inv->waiting == RPIW_ROOM ) {
		Com_sprintf( how, sizeof( how ), "%i up - no room for more yet", up );
	} else if ( coming && inv->waiting == RPIW_NOWHERE ) {
		Com_sprintf( how, sizeof( how ), "%i up - nowhere for more yet", up );
	} else if ( coming ) {
		Com_sprintf( how, sizeof( how ), "%i up, %i to come", up, coming );
	} else if ( last && inv->war ) {
		Com_sprintf( how, sizeof( how ), "the last, %i up", up );	// (a war's: there till they're beaten)
	} else if ( last ) {
		timer = qtrue;
		left = RPI_LEFTOVER - ( inv->arrivedAt ? level.time - inv->arrivedAt : 0 );
		Com_sprintf( how, sizeof( how ), "the last, %i up - gone in", up );
	} else if ( inv->timed ) {
		timer = qtrue;
		left = inv->wait * 1000 - ( level.time - inv->waveAt );
		Com_sprintf( how, sizeof( how ), "%i up - the next in", up );
	} else if ( inv->beatenAt ) {
		timer = qtrue;
		left = inv->wait * 1000 - ( level.time - inv->beatenAt );
		Q_strncpyz( how, "beaten - the next in", sizeof( how ) );
	} else {
		Com_sprintf( how, sizeof( how ), "%i up", up );
	}
	if ( timer ) {
		left = ( Q_max( left, 0 ) + 999 ) / 1000;	// (due: 0:00 till it's come)
		Com_sprintf( out, size, "%s%.28s (%s): %s, %s %i:%02i", inv->war ? "War: " : "", inv->title, rpiSideNames[s], wave, how, left / 60, left % 60 );	// (a long name cut: the page's line)
	} else {
		Com_sprintf( out, size, "%s%.28s (%s): %s, %s", inv->war ? "War: " : "", inv->title, rpiSideNames[s], wave, how );
	}
}

// Admins' games told how the invasions go, as that changes (every RPI_TELL at
// most): rpinv <count> "<line>"... (0: none on) - to a game of protocol
// RP_PROTOCOL_INVADE or later whose player's an admin.
static void G_RPI_Tell( void ) {
	char status[MAX_STRING_CHARS], line[256];
	int s, i, n = 0;

	if ( rpiTellTime && level.time - rpiTellTime < RPI_TELL ) {
		return;
	}
	rpiTellTime = level.time ? level.time : 1;
	status[0] = '\0';
	for ( s = 0; s < RPI_SIDES; s++ ) {
		if ( rpi[s].on ) {
			G_RPI_Line( s, line, sizeof( line ) );
			n++;
			Q_strcat( status, sizeof( status ), va( " \"%s\"", line ) );
		}
	}
	{
		char cmd[MAX_STRING_CHARS];
		Com_sprintf( cmd, sizeof( cmd ), "rpinv %i%s", n, status );
		if ( strcmp( cmd, rpiStatus ) ) {
			Q_strncpyz( rpiStatus, cmd, sizeof( rpiStatus ) );
			rpiStatusSeq++;
		}
	}
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		const gentity_t *ent = &g_entities[i];
		if ( i >= level.maxclients || !ent->inuse || !ent->client || ent->client->pers.connected != CON_CONNECTED || !G_RPAdmin( i ) ||
			G_RPGameProtocol( i ) < RP_PROTOCOL_INVADE ) {
			rpiTold[i] = -1;	// (an admin's again: told)
			continue;
		}
		if ( rpiTold[i] != rpiStatusSeq ) {
			rpiTold[i] = rpiStatusSeq;
			trap->SendServerCommand( i, rpiStatus );
		}
	}
}

// A player's game said hello (this build's, again after a vid_restart, say):
// told how the invasions go, if it's an admin's, as soon as it's time.
void G_RPInvadeHello( int clientNum ) {
	if ( clientNum >= 0 && clientNum < MAX_CLIENTS ) {
		rpiTold[clientNum] = -1;
		rpiTellTime = 0;
	}
}

/*
================================================================================
rp invasion
================================================================================
*/

// The invasion who means: a side's word (G_RPSideWord's: villains too), its
// type, what it's called, its preset's word (-1: no invasion on of that;
// RPI_WHICH_BOTH: more than one - one type on two sides, a side picked).
#define RPI_WHICH_BOTH	-2
static int G_RPI_Which( const char *who ) {
	const int word = G_RPSideWord( who );
	int s, i, k, found = -1;

	for ( s = 0; s < RPI_SIDES; s++ ) {
		qboolean of = qfalse;
		if ( !rpi[s].on ) {
			continue;
		}
		if ( word == rpiTeams[s] ) {
			return s;	// (a side's word: that side's alone)
		}
		if ( !Q_stricmp( who, rpi[s].title ) ) {
			of = qtrue;
		}
		for ( k = 0; k < rpi[s].numKinds; k++ ) {	// (any of its types: by its name, what it's called, its preset's word)
			if ( !Q_stricmp( who, rpi[s].kinds[k].type ) || !Q_stricmp( who, rpi[s].kinds[k].title ) ) {
				of = qtrue;
			}
			for ( i = 0; i < (int)ARRAY_LEN( rpiPresets ); i++ ) {
				if ( !Q_stricmp( who, rpiPresets[i].word ) && !strcmp( rpi[s].kinds[k].title, rpiPresets[i].title ) ) {
					of = qtrue;
				}
			}
		}
		if ( of ) {
			if ( found >= 0 ) {
				return RPI_WHICH_BOTH;
			}
			found = s;
		}
	}
	return found;
}

// A number as typed: digits alone, low to high (-1: it isn't).
static int G_RPI_Number( const char *text, int low, int high ) {
	const char *p;
	int n;

	if ( !text[0] || strlen( text ) > 4 ) {
		return -1;
	}
	for ( p = text; *p; p++ ) {
		if ( *p < '0' || *p > '9' ) {
			return -1;
		}
	}
	n = atoi( text );
	return ( n >= low && n <= high ) ? n : -1;
}

static void G_RPI_Usage( const gentity_t *ent ) {
	G_RPI_Print( ent, "rp invasion start <who> [waves <0-50, 0 till stopped>] [per <1-32>] [cap <1-32>] [cleared <seconds> | every <5 seconds or more>]" );
	G_RPI_Print( ent, "  [side dark | light | creatures (or villains, heroes; file: its NPC file's, as without)]" );
	G_RPI_Print( ent, "  <who>: reborn, stormtroopers, rebels, jedi or an NPC type; rp invasion stop [all | dark | light | creatures | <who>]; rp invasion" );
	G_RPI_Print( ent, "  several: a comma between (reborn,stormtroopers - mixed in every wave, per of them), how many of one a wave after a colon (reborn:2); rp war" );
}

static void G_RPI_WarUsage( const gentity_t *ent ) {
	G_RPI_Print( ent, "rp war start <side> <who> <side> <who> [<side> <who>] [waves <0-50, 0 till stopped>] [per <1-32>] [cap <1-32>]" );
	G_RPI_Print( ent, "  [cleared <seconds> | every <5 seconds or more>] [at <client>] - fought where you stand (or they do)" );
	G_RPI_Print( ent, "  <side>: light (heroes), dark (villains), creatures; <who>: as rp invasion start's (reborn,stormtroopers:2); rp war stop; rp war" );
}

// One of a start's settings (key value) into set - waves, per, cap, cleared or
// every: qtrue, it's one and will do.
static qboolean G_RPI_Setting( const char *key, const char *value, rpiInvasion_t *set ) {
	int n;

	if ( !Q_stricmp( key, "waves" ) && ( n = G_RPI_Number( value, 0, RPI_WAVES_MOST ) ) >= 0 ) {
		set->waves = n;
	} else if ( !Q_stricmp( key, "per" ) && ( n = G_RPI_Number( value, 1, RPI_MOST ) ) >= 0 ) {
		set->per = n;
	} else if ( !Q_stricmp( key, "cap" ) && ( n = G_RPI_Number( value, 1, RPI_MOST ) ) >= 0 ) {
		set->cap = n;
	} else if ( ( !Q_stricmp( key, "cleared" ) || !Q_stricmp( key, "every" ) ) && ( n = G_RPI_Number( value, 0, RPI_WAIT_MOST ) ) >= 0 ) {
		set->timed = (qboolean)( !Q_stricmp( key, "every" ) );
		set->wait = set->timed ? Q_max( n, RPI_EVERY_LEAST ) : n;	// (a timer's never quicker: as it's told)
	} else {
		return qfalse;
	}
	return qtrue;
}

// A start's settings as they're told: its waves ("4 waves"), when the next
// comes, its types' NPC types ("NPC type reborn", "NPC types stormtrooper,
// reborn") and how many of each a wave (several: " (stormtrooper mixed,
// shadowtrooper 1)" - one, nothing).
static void G_RPI_Told( const rpiInvasion_t *inv, char *waves, int wavesSize, char *next, int nextSize, char *types, int typesSize, char *each, int eachSize ) {
	int k;

	if ( inv->waves > 0 ) {
		Com_sprintf( waves, wavesSize, "%i wave%s", inv->waves, ( inv->waves == 1 ) ? "" : "s" );
	} else {
		Q_strncpyz( waves, "waves till it's stopped", wavesSize );
	}
	if ( inv->timed ) {
		Com_sprintf( next, nextSize, "every %i s", inv->wait );
	} else if ( inv->wait ) {
		Com_sprintf( next, nextSize, "%i s after one's beaten", inv->wait );
	} else {
		Q_strncpyz( next, "as soon as one's beaten", nextSize );
	}
	Q_strncpyz( types, ( inv->numKinds > 1 ) ? "NPC types " : "NPC type ", typesSize );
	each[0] = '\0';
	for ( k = 0; k < inv->numKinds; k++ ) {
		Q_strcat( types, typesSize, va( "%s%s", k ? ", " : "", inv->kinds[k].type ) );
		if ( inv->numKinds > 1 ) {
			Q_strcat( each, eachSize, va( "%s%s %s", k ? ", " : " (", inv->kinds[k].type, inv->kinds[k].each ? va( "%i", inv->kinds[k].each ) : "mixed" ) );
		}
	}
	if ( each[0] ) {
		Q_strcat( each, eachSize, ")" );
	}
}

// An invasion of a side set going (as set says; its lineup kinds, n of them,
// called title; picked: its side the admin's), its first wave to come - its
// NPCs as the last's left them gone (their type's this one's now).
static void G_RPI_Begin( int side, const rpiInvasion_t *set, const rpiKind_t *kinds, int n, const char *title, qboolean picked, qboolean war ) {
	rpiInvasion_t *inv = &rpi[side];
	int a;

	G_RPI_FreeAll( side );
	*inv = *set;
	for ( a = 0; a < RPI_TRACK; a++ ) {
		inv->out[a].ent = -1;
	}
	inv->on = qtrue;
	memcpy( inv->kinds, kinds, sizeof( inv->kinds[0] ) * n );
	inv->numKinds = n;
	inv->lastKind = n - 1;	// (the first to come: the first of them)
	Q_strncpyz( inv->title, title, sizeof( inv->title ) );
	inv->picked = picked;
	inv->war = war;
	inv->lookFor = -1;
}

// rp invasion start <who> ...: one begins, its first wave now.
static void G_RPI_Start( gentity_t *ent, const char *by ) {
	char who[MAX_TOKEN_CHARS], title[RPI_TITLE_SIZE], why[512], next[64], waves[32], types[RPI_KINDS * RP_COMP_TYPE_SIZE + 16], each[RPI_KINDS * ( RP_COMP_TYPE_SIZE + 12 ) + 8];
	int side = 0, pick = -1, a, n = 0;
	const int argc = trap->Argc();
	rpiInvasion_t *inv, set;
	rpiKind_t kinds[RPI_KINDS];

	if ( level.gametype == GT_DUEL || level.gametype == GT_POWERDUEL || level.gametype == GT_SIEGE ) {
		G_RPI_Say( ent, "No invasions in Duel, Power Duel or Siege" );
		return;
	}
	trap->Argv( 3, who, sizeof( who ) );
	memset( &set, 0, sizeof( set ) );
	set.waves = 4;
	set.per = 6;
	set.cap = 16;
	set.wait = 20;
	for ( a = 4; a < argc; a += 2 ) {
		char key[32], value[32];
		int s;
		trap->Argv( a, key, sizeof( key ) );
		trap->Argv( a + 1, value, sizeof( value ) );
		if ( G_RPI_Setting( key, value, &set ) ) {
			continue;
		}
		if ( !Q_stricmp( key, "side" ) && value[0] && ( s = G_RPSideWord( value ) ) != RP_SIDE_BAD ) {
			if ( s == NPCTEAM_NEUTRAL ) {
				G_RPI_Say( ent, "An invasion's side is dark, light or creatures: a neutral one would fight no one" );
				return;
			}
			pick = ( s == RP_SIDE_FILE ) ? -1 : ( s == NPCTEAM_ENEMY ) ? RPI_DARK : ( s == NPCTEAM_PLAYER ) ? RPI_LIGHT : RPI_CREATURES;
		} else {
			G_RPI_Usage( ent );
			return;
		}
	}
	if ( !G_RPI_Lineup( who, pick, kinds, &n, &side, title, sizeof( title ), why, sizeof( why ) ) ) {
		G_RPI_Say( ent, why );
		return;
	}
	if ( rpi[side].on && rpi[side].war && !rpiWar.on ) {
		G_RPI_Quit( side );	// (the last war's, staying a while - its winners, the others' bodies: gone now)
	}
	if ( rpi[side].on ) {
		if ( rpi[side].war ) {
			Com_sprintf( why, sizeof( why ), "The war's on, the %s in it (%s): stop it first (rp war stop)", rpiSideNames[side], rpi[side].title );
		} else {
			Com_sprintf( why, sizeof( why ), "The %s invasion's on (the %s's): stop it first (rp invasion stop %s)", rpi[side].title, rpiSideNames[side], rpiSideWords[side] );
		}
		G_RPI_Say( ent, why );
		return;
	}
	if ( ent ) {
		G_RPAdminCheat( ent );	// (in the log, as an admin's cheats are)
	}
	G_RPI_Begin( side, &set, kinds, n, title, (qboolean)( pick >= 0 ), qfalse );
	inv = &rpi[side];
	G_RPI_Told( inv, waves, sizeof( waves ), next, sizeof( next ), types, sizeof( types ), each, sizeof( each ) );
	G_LogPrintf( "Invasion: %s (%s, the %s%s) started by %s - %s of %i%s, %i up at once at most, the next %s\n", title, types, rpiSideNames[side],
		inv->picked ? ", picked" : "", by, waves, G_RPI_WaveSize( inv ), each, inv->cap, next );
	Com_sprintf( why, sizeof( why ), "The %s invasion's on%s: %s of %i%s, %i up at once at most, the next %s", title,
		inv->picked ? va( " (the %s, picked)", rpiSideNames[side] ) : "", waves, G_RPI_WaveSize( inv ), each, inv->cap, next );
	G_RPI_Say( ent, why );
	G_RPI_NextWave( side );
}

// rp invasion stop [all | <who>]: over, what's left of it gone - a war's side,
// the war (and the war's winners staying: gone).
static void G_RPI_Stop( gentity_t *ent, const char *by ) {
	char who[MAX_TOKEN_CHARS], text[256];
	int s, n = 0;
	qboolean war = qfalse, winners = qfalse;

	trap->Argv( 3, who, sizeof( who ) );
	if ( who[0] && Q_stricmp( who, "all" ) ) {
		s = G_RPI_Which( who );
		if ( s == RPI_WHICH_BOTH ) {
			Com_sprintf( text, sizeof( text ), "More than one invasion on is of %s: say whose side's - rp invasion stop dark, light or creatures", who );
			G_RPI_Say( ent, text );
			return;
		}
		if ( s < 0 ) {
			Com_sprintf( text, sizeof( text ), "No invasion of %s is on", who );
			G_RPI_Say( ent, text );
			return;
		}
		if ( ent ) {
			G_RPAdminCheat( ent );
		}
		if ( rpi[s].war && rpiWar.on ) {
			G_RPI_WarCallOff( by );
			G_RPI_Say( ent, "The war's called off" );
			return;
		}
		if ( rpi[s].war ) {	// (the last war's, staying a while: its winners, the others' bodies)
			G_LogPrintf( "War: what was left of it gone (the %s's %s) - by %s\n", rpiSideNames[s], rpi[s].title, by );
			G_RPI_Quit( s );
			G_RPI_Say( ent, "What was left of the war is gone" );
			return;
		}
		G_RPI_End( s, "is called off", by );
		Com_sprintf( text, sizeof( text ), "The %s invasion's called off", rpi[s].title );
		G_RPI_Say( ent, text );
		return;
	}
	for ( s = 0; s < RPI_SIDES; s++ ) {
		n += rpi[s].on ? 1 : 0;
	}
	if ( !n ) {
		G_RPI_Say( ent, "No invasion's on" );
		return;
	}
	if ( ent ) {
		G_RPAdminCheat( ent );
	}
	if ( rpiWar.on ) {
		war = qtrue;
		G_RPI_WarCallOff( by );
	}
	n = 0;
	for ( s = 0; s < RPI_SIDES; s++ ) {
		if ( rpi[s].on && rpi[s].war ) {	// (the last war's, staying a while)
			G_LogPrintf( "War: what was left of it gone (the %s's %s) - by %s\n", rpiSideNames[s], rpi[s].title, by );
			G_RPI_Quit( s );
			winners = qtrue;
		} else if ( rpi[s].on ) {
			G_RPI_End( s, "is called off", by );
			n++;
		}
	}
	if ( war ) {
		G_RPI_Say( ent, n ? "The war and every invasion are called off" : "The war's called off" );
	} else if ( n ) {
		G_RPI_Say( ent, ( n > 1 || winners ) ? "Every invasion's called off" : "The invasion's called off" );
	} else {
		G_RPI_Say( ent, "What was left of the war is gone" );
	}
}

// An admin's (ent), or the console's (NULL: rcon) - whose, as the log has it
// (by). qfalse: not an admin's - refused, told so, the log too (what: "invasions").
static qboolean G_RPI_Whose( gentity_t *ent, const char *what, char *by, int bySize ) {
	if ( ent && !G_RPAdmin( ent - g_entities ) ) {
		G_RPI_Say( ent, va( "Only an admin starts or stops %s", what ) );
		G_LogPrintf( "%s: %s" S_COLOR_WHITE " (client %i) typed %s - refused: not an admin\n", !strcmp( what, "wars" ) ? "War" : "Invasion",
			ent->client ? ent->client->pers.netname : "", (int)( ent - g_entities ), ConcatArgs( 0 ) );
		return qfalse;
	}
	if ( ent ) {
		Com_sprintf( by, bySize, "%s" S_COLOR_WHITE " (client %i)", ent->client ? ent->client->pers.netname : "", (int)( ent - g_entities ) );
	} else {
		Q_strncpyz( by, "rcon", bySize );
	}
	return qtrue;
}

// rp invasion ...: an admin's (ent), or the console's (NULL: rcon).
void G_RPInvadeCommand( gentity_t *ent ) {
	char sub[64], by[128];
	int s, n = 0;

	if ( !G_RPI_Whose( ent, "invasions", by, sizeof( by ) ) ) {
		return;
	}
	trap->Argv( 2, sub, sizeof( sub ) );
	if ( !Q_stricmp( sub, "start" ) ) {
		G_RPI_Start( ent, by );
	} else if ( !Q_stricmp( sub, "stop" ) ) {
		G_RPI_Stop( ent, by );
	} else if ( !sub[0] || !Q_stricmp( sub, "status" ) ) {
		for ( s = 0; s < RPI_SIDES; s++ ) {
			if ( rpi[s].on ) {
				char line[256];
				G_RPI_Line( s, line, sizeof( line ) );
				G_RPI_Print( ent, line );
				n++;
			}
		}
		if ( !n ) {
			G_RPI_Print( ent, "No invasion's on" );
		}
		G_RPI_Usage( ent );
	} else {
		G_RPI_Usage( ent );
	}
}

/*
================================================================================
rp war
================================================================================
*/

// A war's sides, as told: "Rebel and Jedi against Reborn", "Rebel, Reborn and
// Rancor, each against the others" (each side's what it's called) - three, one
// of them of several NPC types: "Rebel and Jedi against Reborn against Rancor"
// (no "and" between sides, then: a side's own).
static void G_RPI_WarSides( char *out, int size ) {
	int s, n = 0, k = 0;
	qboolean several = qfalse;

	out[0] = '\0';
	for ( s = 0; s < RPI_SIDES; s++ ) {
		if ( rpiWar.sides & ( 1 << s ) ) {
			n++;
			several = (qboolean)( several || rpi[s].numKinds > 1 );
		}
	}
	for ( s = 0; s < RPI_SIDES; s++ ) {
		if ( !( rpiWar.sides & ( 1 << s ) ) ) {
			continue;
		}
		Q_strcat( out, size, va( "%s%s", !k ? "" : ( n == 2 || several ) ? " against " : ( k == n - 1 ) ? " and " : ", ", rpi[s].title ) );
		k++;
	}
	if ( n > 2 && !several ) {
		Q_strcat( out, size, ", each against the others" );
	}
}

// rp war start <side> <who> <side> <who> [<side> <who>] [settings] [at
// <client>]: the war begins where the admin stands (or the client said does),
// each side's first wave now - each side's waves as the settings say.
static void G_RPI_WarStart( gentity_t *ent, const char *by ) {
	char who[RPI_SIDES][MAX_TOKEN_CHARS], titles[RPI_SIDES][RPI_TITLE_SIZE], why[512], text[MAX_STRING_CHARS], line[MAX_STRING_CHARS];
	char next[64], waves[32], types[RPI_KINDS * RP_COMP_TYPE_SIZE + 16], each[RPI_KINDS * ( RP_COMP_TYPE_SIZE + 12 ) + 8], sides[MAX_STRING_CHARS];
	rpiKind_t kinds[RPI_SIDES][RPI_KINDS];
	int nKinds[RPI_SIDES], order[RPI_SIDES], a, s, n = 0, at = -1, own = -1, i;
	const int argc = trap->Argc();
	qboolean in[RPI_SIDES] = { qfalse, qfalse, qfalse };
	const gentity_t *where;
	rpiInvasion_t set;
	vec3_t eye, dir;
	float yaw;

	if ( level.gametype == GT_DUEL || level.gametype == GT_POWERDUEL || level.gametype == GT_SIEGE ) {
		G_RPI_Say( ent, "No wars in Duel, Power Duel or Siege" );
		return;
	}
	memset( &set, 0, sizeof( set ) );
	set.waves = 4;
	set.per = 6;
	set.cap = 16;
	set.wait = 20;
	for ( a = 3; a < argc; a += 2 ) {
		char key[32], value[MAX_TOKEN_CHARS];
		int w;
		trap->Argv( a, key, sizeof( key ) );
		trap->Argv( a + 1, value, sizeof( value ) );
		if ( G_RPI_Setting( key, value, &set ) ) {
			continue;
		}
		if ( !Q_stricmp( key, "at" ) && ( w = G_RPI_Number( value, 0, MAX_CLIENTS - 1 ) ) >= 0 ) {
			at = w;
			continue;
		}
		w = ( a + 1 < argc ) ? G_RPSideWord( key ) : RP_SIDE_BAD;
		if ( w == NPCTEAM_NEUTRAL ) {
			G_RPI_Say( ent, "A war's sides are light, dark and creatures: a neutral one would fight no one" );
			return;
		}
		if ( w == NPCTEAM_ENEMY || w == NPCTEAM_PLAYER || w == NPCTEAM_FREE ) {
			s = ( w == NPCTEAM_ENEMY ) ? RPI_DARK : ( w == NPCTEAM_PLAYER ) ? RPI_LIGHT : RPI_CREATURES;
			if ( in[s] ) {
				Com_sprintf( why, sizeof( why ), "The %s is in it twice: each side once, all of its NPC types a comma between (%s reborn,stormtroopers)", rpiSideNames[s],
					rpiSideWords[s] );
				G_RPI_Say( ent, why );
				return;
			}
			in[s] = qtrue;
			Q_strncpyz( who[s], value, sizeof( who[s] ) );
			n++;
			continue;
		}
		G_RPI_WarUsage( ent );
		return;
	}
	if ( n < 2 ) {
		G_RPI_Say( ent, "A war's of two sides or three, each its NPCs: rp war start light rebels,jedi dark reborn" );
		return;
	}
	if ( at >= 0 ) {
		where = &g_entities[at];
	} else if ( ent ) {
		where = ent;
	} else {
		G_RPI_Say( ent, "Say where it's fought: at <client> - where they stand" );
		return;
	}
	if ( at >= level.maxclients || !G_RPI_Alive( where ) ) {
		G_RPI_Say( ent, ( where == ent ) ? "You're not in the game: a war's fought where you stand" : va( "Client %i isn't in the game: a war's fought where they stand", at ) );
		return;
	}
	if ( rpiWar.on ) {
		G_RPI_Say( ent, "A war's on: stop it first (rp war stop)" );
		return;
	}
	for ( s = 0; s < RPI_SIDES; s++ ) {
		int got;
		if ( !in[s] ) {
			continue;
		}
		if ( !G_RPI_Lineup( who[s], s, kinds[s], &nKinds[s], &got, titles[s], sizeof( titles[s] ), why, sizeof( why ) ) ) {
			Com_sprintf( text, sizeof( text ), "The %s: %s", rpiSideNames[s], why );
			G_RPI_Say( ent, text );
			return;
		}
	}
	for ( s = 0; s < RPI_SIDES; s++ ) {
		if ( in[s] && rpi[s].on && !rpi[s].war ) {
			Com_sprintf( why, sizeof( why ), "The %s invasion's on (the %s's): stop it first (rp invasion stop %s)", rpi[s].title, rpiSideNames[s], rpiSideWords[s] );
			G_RPI_Say( ent, why );
			return;
		}
	}
	if ( ent ) {
		G_RPAdminCheat( ent );
	}
	for ( s = 0; s < RPI_SIDES; s++ ) {
		if ( rpi[s].on && rpi[s].war ) {
			G_RPI_Quit( s );	// (the last war's, staying a while - its winners, the others' bodies: gone now)
		}
	}
	// where: where they stand; the ways in round them - the first ahead of them, of a side they're not of (theirs, last: behind them, two
	// sides), the rest evenly round
	memset( &rpiWar, 0, sizeof( rpiWar ) );
	rpiWar.on = qtrue;
	rpiWar.startedAt = level.time ? level.time : 1;
	VectorCopy( where->client->ps.origin, rpiWar.at );
	G_RPI_Under( where, rpiWar.floor );
	G_RPI_Look( where, eye, dir );
	yaw = vectoyaw( dir );
	switch ( G_RPFaction( where->s.number ) ) {
	case RPF_HEROES:	own = RPI_LIGHT; break;
	case RPF_VILLAINS:	own = RPI_DARK; break;
	default:			own = -1; break;
	}
	for ( s = 0, i = 0; s < RPI_SIDES; s++ ) {
		if ( in[s] && s != own ) {
			order[i++] = s;
		}
	}
	if ( own >= 0 && in[own] ) {
		order[i++] = own;
	}
	for ( a = 0; a < i; a++ ) {
		rpiWar.sides |= 1 << order[a];
		rpiWar.way[order[a]] = AngleNormalize360( yaw + 360.0f * (float)a / (float)i );
	}
	for ( s = 0; s < RPI_SIDES; s++ ) {
		if ( in[s] ) {
			G_RPI_Begin( s, &set, kinds[s], nKinds[s], titles[s], qtrue, qtrue );
		}
	}
	G_RPI_WarSides( sides, sizeof( sides ) );
	G_RPI_Told( &set, waves, sizeof( waves ), next, sizeof( next ), types, sizeof( types ), each, sizeof( each ) );
	Com_sprintf( text, sizeof( text ), "War\n%s", sides );
	Com_sprintf( line, sizeof( line ), "War: %s, where %s" S_COLOR_WHITE " stands - %s each, the next %s", sides, where->client->pers.netname, waves, next );
	G_RPI_Announce( text, line );
	line[0] = '\0';
	for ( s = 0; s < RPI_SIDES; s++ ) {
		if ( !in[s] ) {
			continue;
		}
		G_RPI_Told( &rpi[s], waves, sizeof( waves ), next, sizeof( next ), types, sizeof( types ), each, sizeof( each ) );
		Q_strcat( line, sizeof( line ), va( "%sthe %s's %s (%s) - %s of %i%s, %i up at once at most", line[0] ? "; " : "", rpiSideNames[s], rpi[s].title, types, waves,
			G_RPI_WaveSize( &rpi[s] ), each, rpi[s].cap ) );
	}
	G_LogPrintf( "War: started by %s, where %s" S_COLOR_WHITE " (client %i) stands (%.0f %.0f %.0f) - %s; the next %s\n", by, where->client->pers.netname,
		where->s.number, rpiWar.at[0], rpiWar.at[1], rpiWar.at[2], line, next );
	Com_sprintf( text, sizeof( text ), "The war's on: %s - %s each, the next %s", sides, waves, next );
	G_RPI_Say( ent, text );
	for ( s = 0; s < RPI_SIDES; s++ ) {
		if ( in[s] ) {
			G_RPI_NextWave( s );
		}
	}
}

// rp war stop: the war over, called off (what was left of the last, staying
// a while - its winners, the others' bodies - gone).
static void G_RPI_WarStop( gentity_t *ent, const char *by ) {
	int s;
	qboolean left = qfalse;

	for ( s = 0; s < RPI_SIDES; s++ ) {
		left = (qboolean)( left || ( rpi[s].on && rpi[s].war ) );
	}
	if ( !rpiWar.on && !left ) {
		G_RPI_Say( ent, "No war's on" );
		return;
	}
	if ( ent ) {
		G_RPAdminCheat( ent );
	}
	if ( rpiWar.on ) {
		G_RPI_WarCallOff( by );
		G_RPI_Say( ent, "The war's called off" );
		return;
	}
	for ( s = 0; s < RPI_SIDES; s++ ) {
		if ( rpi[s].on && rpi[s].war ) {
			G_LogPrintf( "War: what was left of it gone (the %s's %s) - by %s\n", rpiSideNames[s], rpi[s].title, by );
			G_RPI_Quit( s );
		}
	}
	G_RPI_Say( ent, "What was left of the war is gone" );
}

// rp war ...: an admin's (ent), or the console's (NULL: rcon).
void G_RPWarCommand( gentity_t *ent ) {
	char sub[64], by[128];
	int s;

	if ( !G_RPI_Whose( ent, "wars", by, sizeof( by ) ) ) {
		return;
	}
	trap->Argv( 2, sub, sizeof( sub ) );
	if ( !Q_stricmp( sub, "start" ) ) {
		G_RPI_WarStart( ent, by );
	} else if ( !Q_stricmp( sub, "stop" ) ) {
		G_RPI_WarStop( ent, by );
	} else if ( !sub[0] || !Q_stricmp( sub, "status" ) ) {
		if ( !rpiWar.on ) {
			G_RPI_Print( ent, "No war's on" );
		}
		for ( s = 0; s < RPI_SIDES; s++ ) {
			if ( rpi[s].on && rpi[s].war ) {	// (what's left of the last, staying a while: there too)
				char line[256];
				G_RPI_Line( s, line, sizeof( line ) );
				G_RPI_Print( ent, line );
			}
		}
		G_RPI_WarUsage( ent );
	} else {
		G_RPI_WarUsage( ent );
	}
}

/*
================================================================================
Every frame
================================================================================
*/

void G_RPInvadeFrame( void ) {
	int s, i;

	rpiTraces = 0;
	rpiCompTraces = G_RPCompTraces();
	if ( !level.intermissiontime && !level.intermissionQueued ) {	// (held in the intermission)
		for ( s = 0; s < RPI_SIDES; s++ ) {
			if ( rpi[s].on ) {
				G_RPI_Frame( s );
			}
		}
		G_RPI_WarFrame();	// (a side of it out: one left standing, it's won)
		rpiFirst = ( rpiFirst + 1 ) % RPI_SIDES;	// (each first in turn: the frame's looking shared)
		for ( i = 0; i < RPI_SIDES; i++ ) {
			s = ( rpiFirst + i ) % RPI_SIDES;
			if ( rpi[s].on ) {
				G_RPI_Bringing( s );
			}
		}
		G_RPI_Bodies();
	}
	G_RPI_Tell();
}

void G_RPInvadeInit( void ) {
	int s, k;

	memset( rpi, 0, sizeof( rpi ) );
	memset( &rpiWar, 0, sizeof( rpiWar ) );
	for ( s = 0; s < RPI_SIDES; s++ ) {
		for ( k = 0; k < RPI_TRACK; k++ ) {
			rpi[s].out[k].ent = -1;
		}
	}
	for ( k = 0; k < MAX_CLIENTS; k++ ) {
		rpiTold[k] = -1;
	}
	rpiTellTime = 0;
	rpiStatus[0] = '\0';
	rpiStatusSeq = 0;
	rpiTraces = rpiFirst = 0;
	rpiRoomAt = -1;
}
