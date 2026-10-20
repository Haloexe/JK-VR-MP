/*
===========================================================================
OpenJK VR (multiplayer) - roleplay, stage 3: companions. A character keeps
up to RP_COMP_MAX NPCs of its own (g_vrrp.c keeps them with it: a name, the
NPC type it fights as, a look, colours, whether it comes out), and while its
player plays it, roleplay on, the ones ticked come out with them - up to
g_rpCompanions (at most RP_COMP_OUT_MAX), the server as a whole at most
g_rpCompanionsMax - a moment after the player spawns, on their faction's
side; and go when the player dies, logs out, switches characters, leaves,
goes to spectate, or roleplay goes off. A companion and its owner (and its
fellow) can't hurt each other. Not in Siege or the duel game types.

FIGHTS AS: an NPC type of the people (bg_vrcomp.c: no droids, beasts or
vehicles), one the NPC files have with a player model on the humanoid
skeleton every look shares.

LOOKS. "" is its NPC type as the type comes; else a player model, a skin or
a custom character's parts (bg_vrcomp.c), set as the NPC's model as it's
spawned (G_RPCompLook, asked by NPC_ParseParms) - if the server lets
companions look as they like (g_rpCompanionModels 1), has the model, on the
humanoid skeleton (another would take the server down: its model.glm's
first bytes say, read - never loaded), and the skin; else it comes as its
type does. Each look new to the level is a model configstring every player
keeps till the map changes, or a skin told beside one (too long for it)
that every player's game seeing it keeps: a player's companions bring
RPC_NEW_LOOKS new ones a level at most (then as their types come).

ROOM. A companion comes out only where the level has room for it: entities
G_Spawn would hand out, configstrings of each kind, and room in the
gamestate every player's game holds them all in (MAX_GAMESTATE_CHARS - run
past it, and every player's dropped): RPC_ROOM_CHARS kept free. Its entity
is one an NPC's had before where there's one (G_RPCompEntity: the game
keeps an NPC's memory with its slot for good, and companions come and go).

ORDERS (the wheel's Companions ring, cg_vrpose.c: rpcomp order <what>) go
to both - or, the player looking at one of theirs as they give it, to that
one: Follow me (walk with you, never fighting), Formation (behind you,
beside you, an escort: one ahead, one behind), Attack (whoever you're
looking at), Defend me (fight whoever hurts you or them, or goes after you:
what they do unordered), Hold here, Come to me (to just behind you, every
RPC_COME_WAIT at most; and a knocked-out one whose wait is over, back),
Pose with me (your pose - sitting, on the floor - once they're at their
spots and still, or standing when you do).

MOVING, by the player, not the map (no waypoints, so on any map): a spot
for each, worked out from where the player is and the way they face (their
walk's way, or still, their body's: a VR player's as the headset turns it,
so looking about doesn't send them circling - turned past RPC_TURN for
RPC_TURN_TIME, and the spots turn); where the way to its spot can't be
walked straight (up a step at a time, no gap), along the player's trail (a
point every RPC_TRAIL_STEP they walk, the last RPC_TRAIL of them: every one
somewhere they stood). A spot in a wall or over nothing: single file on
the trail behind, the one nearer them along it first; neither (their back
to a wall, the trail cut): where it is, near them, else toward them.
Running when the player runs or it's fallen behind, walking otherwise; round
a body in its way, never into lava or slime, nor off a drop (slower near
one); there (or by someone standing there), facing the player's way. Where
the trail can't be walked (a lift, a train, a jump up a ledge, a drop - but
down one it can walk straight, as stairs run down flying, it follows - a
teleport, lava; the floor gone from where it's going), stuck RPC_STUCK
(RPC_CUT_STUCK at the trail's cut), or further than RPC_FAR: to a point of
the trail just behind the player (the player in the air a moment, behind
where they left the ground), out of their sight where there's room. The
player riding something, they wait where they are.

FIGHTING, with the NPCs' own combat (G_RPCompThink returns qfalse to
NPC_RunBehavior: a saber's, a Jedi's; anything else, a trooper's), on a
leash: no further than RPC_LEASH from the player (Hold here: RPC_HOLD_LEASH
from where they hold - one with a gun or thermals taking on what's as far
off as following), then back. By its weapon (G_RPC_Weapon), as a squad's
commander would send it, no nearer the leash's end than RPC_GUN_MARGIN: a
gun with no clear shot steps aside till it has one (G_RPC_ShotSpot), else
goes after its enemy (a disruptor's fired as it comes, never sniping; a
rocket, never nearer than RPC_ROCKET_NEAR); thermals go after a clear
throw, lobbed to burst at their target's feet (G_RPCompLob), never within
RPC_THROW_NEAR of the thrower - fists for anyone that near; fists and a
baton go after their enemy till a blow would land (G_RPC_Blow), swinging
only then. Getting nowhere at its enemy RPC_FIGHT_STUCK (a wall, a ledge,
lava in its way, its leash's end), it lets them be RPC_FIGHT_IGNORE, or
till they come at it: back to its owner. Their first shot comes as soon as a
hero's NPC's would (G_AttackDelay), a villain's companions' too; and they
back their owner up against a saber, where the game's NPCs hold their fire.
Who they may fight is theirs alone (G_RPCompEnemy, asked through
G_RPNPCEnemy): never their owner, the other of theirs, anyone down, anyone
while their owner duels, or their owner's teammates (a team game, friendly
fire off); Defend me, whoever hurt their owner or them in RPC_HURT_TIME (a
duel's blows aside), or an NPC going after them; Attack, the one they were
set on too; Follow me, nobody. To the NPCs and the players they're on their
owner's side; neither they nor their owner can hurt one another, or turn
the Force on one another (w_force.c), nor teammates and theirs.

KNOCKED OUT, not lost: one whose health runs out falls; RPC_BODY later it's
gone, and it can be called again (Come to me, or the player spawning) after
RPC_KNOCKED_WAIT - that companion (by its name), whatever character's played
meanwhile. Put away hurt (unticked, or changed), it comes out again as hurt
for RPC_KNOCKED_WAIT (till its owner's next life); changed while it's
fighting or hurt lately, it's changed once that's over. Its owner down, one
of theirs - one getting them up already, else one not fighting, else the
nearer - comes and gets them up, as it follows them (its way to them, round
anyone in it, or brought to just by them): RPC_HELP_TIME there, as a hand of
their faction does (g_vrdown.c).

The players' games are told (configstring CS_VR_COMPANIONS + owner) which
entities are whose companions, their names, and a look whose skin is too
long for the model's configstring to carry (it carries the default:
BG_VRCompSkinInCs) - "<entity>\<name>\<look>", '\' between, the look ""
but for those; and an owner, getting up by one of theirs: rpup <entity>
<owner> <time | 0 stopped | -1 got them up> (cg_vrdown.c).

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "g_local.h"
#include "b_local.h"

extern void G_UcmdMoveForDir( gentity_t *self, usercmd_t *cmd, vec3_t dir );
extern void AI_DeleteSelfFromGroup( gentity_t *self );
extern void WP_DeactivateSaber( gentity_t *self, qboolean clearLength );
extern qboolean BG_InKnockDown( int anim );
extern gentity_t *NPC_Spawn_Do( gentity_t *ent );
extern void NPC_Begin( gentity_t *ent );
extern qboolean NPC_SpotWouldTelefrag( gentity_t *npc );
extern char NPCParms[];		// (NPC_stats.c: the NPC files, read)
extern float pm_friction;		// (bg_pmove.c: a body's on the ground - moving at v, it slides v / this further, stopping)
extern void ChangeWeapon( gentity_t *ent, int newWeapon );	// (NPC_combat.c: an NPC's weapon, and how fast it fires it)

#define RPC_SPAWN_DELAY		500		// ms after their owner spawns, they come
#define RPC_RETRY			1000	// ms before another try at bringing one out (nowhere for it)
#define RPC_ROOM_WAIT		10000	// ms before another, the level out of room for it
#define RPC_BODY			10000	// ms a knocked-out companion lies there...
#define RPC_KNOCKED_WAIT	120000	// ...and before it can be called again
#define RPC_COME_WAIT		10000	// ms between Come to me's
#define RPC_FULL_TELL		30000	// ms between telling a player the server has as many out as it allows
#define RPC_HURT_TIME		10000	// ms after hurting a player or theirs, they're fair game (Defend me)
#define RPC_SCAN			250		// ms between looks about for whom to fight
#define RPC_HOLSTER			3000	// ms after a fight, a saber goes out
#define RPC_HELP_TIME		3000	// ms getting their owner up (as a hand does, g_vrdown.c)
#define RPC_HELP_REACH		60.0f	// near enough to them for it
#define RPC_TRAIL_STEP		17.0f	// a point of the trail every 0.5 m...
#define RPC_TRAIL			150		// ...the last 75 m of it
#define RPC_BEHIND			51.0f	// the spots: 1.5 m back...
#define RPC_PAIR			24.0f	// ...two side by side, this far either side...
#define RPC_BESIDE			41.0f	// ...1.2 m to each side...
#define RPC_ESCORT			68.0f	// ...2 m ahead, 2 m behind
#define RPC_ARRIVED			16.0f	// at its spot, near as this...
#define RPC_LEAVE			48.0f	// ...and off again further than this
#define RPC_LAG				96.0f	// further from it than this, it runs
#define RPC_TURN			45.0f	// the player turned this far...
#define RPC_TURN_TIME		1000	// ...this long: the spots turn
#define RPC_MOVING			40.0f	// faster than this, the player's walking (and their way is their walk's)
#define RPC_RUNNING			180.0f	// faster than this, running
#define RPC_LEASH			510.0f	// fighting, no further than 15 m from them...
#define RPC_HOLD_LEASH		170.0f	// ...or 5 m from where they hold (a gun there shoots at what's as far off as following)
#define RPC_GUN_MARGIN		80.0f	// a gun going after a clear shot stops this short of its leash (its way on brings it nearer)
#define RPC_BLOW			8.0f	// fists' or a baton's blow: this far on from where it begins (MELEE_RANGE, STUN_BATON_RANGE: G_RPC_Blow)
#define RPC_THROW_NEAR		160.0f	// a thermal never thrown at anyone nearer, nor to burst nearer (its blast, 128 round, would reach the thrower): fists...
#define RPC_THROW_BACK		224.0f	// ...till they're further than this
#define RPC_THROW_SPEED		900.0f	// a thermal thrown no faster (TD_VELOCITY: a player's, held to the full)
#define RPC_LOB_STEPS		8		// a thermal's arc, looked along in this many steps...
#define RPC_LOB_LANDS		48.0f	// ...ending this near the feet of whom it's thrown at (or on them)
#define RPC_LOB_KEEP		1000	// ms a thermal's aim's kept: its enemy gone as it lets go, there still (not at its own feet)
#define RPC_SPOT_STEP		64.0f	// a gun with no clear shot looks for one this far to its side, and further (G_RPC_ShotSpot)
#define RPC_SPOT_LOOK		500		// ms between such looks (one found, it's going there meanwhile, while its shot from there's clear)...
#define RPC_SPOT_THERE		8.0f	// ...there, this near it - its shot clear in RPC_SPOT_LOOK, or it's on elsewhere...
#define RPC_SPOT_GO			1500	// ...and there in this many ms (or someone's standing there): else elsewhere
#define RPC_ROCKET_NEAR		192.0f	// a rocket's never fired at anyone nearer (its blast, 160 round, would reach the shooter - NPC_BSST_Attack's own 128 wouldn't keep it out)
#define RPC_FIGHT_STUCK		2000	// ms going after its enemy, on its feet, getting nowhere (a wall, a ledge, lava in its way)...
#define RPC_FIGHT_MOVED		16.0f	// ...moved less than this...
#define RPC_FIGHT_IGNORE	5000	// ...it lets them be this long...
#define RPC_FIGHT_SHIFT		64.0f	// ...or till they've come this much nearer (G_RPC_Ignored)
#define RPC_FAR				1360.0f	// 40 m from them: to just behind them
#define RPC_STUCK			5000	// ms getting nowhere: to just behind them
#define RPC_CUT_STUCK		1000	// ms getting nowhere, the trail cut just ahead (a lift gone up with them, a jump): to them
#define RPC_STUCK_MOVE		16.0f	// moved less than this in a second, wanting to: getting nowhere
#define RPC_TELE_WAIT		2000	// ms between going to them that way
#define RPC_PASS			24.0f	// a trail point reached, this near
#define RPC_PATH			250		// ms between looks along the trail for a way straight on
#define RPC_NEAR			256.0f	// not on the trail: a point of it this near, it can walk to...
#define RPC_NEAR_TRIES		8		// ...the nearest this many looked at...
#define RPC_NEAR_SPREAD		2		// ...none within this many points of a nearer one
#define RPC_LOOK			2048.0f	// whom a player looks at, at most this far (an order's)
#define RPC_ADDRESS			0.985f	// looking at one of theirs: cos of 10 degrees off it
#define RPC_ROOM_ENTS		64		// entities kept free (each companion takes up to 3)
#define RPC_ROOM_MODELS		16		// model configstrings kept free (a companion can take 3, a look of its own 1 more)
#define RPC_ROOM_SOUNDS		8		// sound configstrings kept free (its sound sets: 4)
#define RPC_ROOM_FX			2		// effect configstrings kept free
#define RPC_ROOM_CHARS		3072	// room kept in the gamestate (for the level's own, and what a companion brings)
#define RPC_NEW_LOOKS		8		// looks new to the level a player's companions bring (each a configstring, or a skin told beside one, kept till the map changes)
#define RPC_TOLD_KEPT		256		// looks told beside their models' configstrings this level, kept (past that: each new again)
#define RPC_MODELS_KEPT		64		// player models asked whether they're on the humanoid skeleton, kept
#define RPC_HELP_HEIGHT		48.0f	// getting their owner up: no further above or below them than this
#define RPC_WALK_FAR		512.0f	// further than this, a way isn't looked along (it's the trail's, or a teleport's)
#define RPC_WALK_STEP		16.0f	// a way's floor looked at this often along it (a stair's depth)...
#define RPC_STEP_UP			( STEPSIZE + 1.0f )	// ...up no more than a step from one to the next (and a little: where a body stands, it's a touch above the floor)
#define RPC_DETOUR			16.0f	// round a body in its way: this far past its side
#define RPC_FILE_SURE		16.0f	// single file, the nearer its owner by this much: the nearer place (else by their order)
#define RPC_WAIT_NEAR		128.0f	// no spot for it (their back to a wall, their trail cut): this near them, it waits where it is
#define RPC_AIR_TRAIL		1500	// ms the player's in the air (a jump, a fall) their trail's still theirs: where they left the ground (not swimming, a jetpack's)
#define RPC_UNWALKABLE		( CONTENTS_LAVA | CONTENTS_SLIME )	// what's no floor to walk on, nor anywhere to be
#define RPC_EDGE			( RPC_WALK_STEP + 8.0f )	// lava, slime, a drop ahead: it stops this short of where it was seen (every RPC_WALK_STEP)...
#define RPC_SLOW			0.1f	// ...near enough it'd go slower than this of its speed: there, it stops

#define RPC_MASK			( CONTENTS_SOLID | CONTENTS_PLAYERCLIP | CONTENTS_TERRAIN )	// what a body can't go through, bodies aside (an NPC moves as a player does: MASK_PLAYERSOLID)

enum { RPCO_DEFEND, RPCO_FOLLOW, RPCO_ATTACK, RPCO_HOLD, RPCO_POSE };
enum { RPCF_BEHIND, RPCF_BESIDE, RPCF_ESCORT, RPCF_NUM };
static const char *rpcFormations[RPCF_NUM] = { "behind you", "beside you", "an escort, one ahead and one behind" };

typedef struct {
	vec3_t		at;
	qboolean	cut;			// not walked to from the point before (a lift, a jump, a drop, a teleport): not followed (but down a way it can walk)
} rpcPoint_t;

typedef struct {
	int			ent;			// its NPC (-1: not out)
	int			slot;			// the character's companion it is
	char		type[RP_COMP_TYPE_SIZE];	// as it came out: the NPC's own type (its NPC_type points here)
	char		look[MAX_QPATH];	// the look it was given, and colours (another: it comes out anew)
	char		worn[MAX_QPATH];	// the look it came out in ("": as its type comes - the server's not letting looks, say)
	byte		rgb[3];
	int			order;			// RPCO_*
	int			target;			// Attack's (-1: none)
	vec3_t		hold;			// Hold here's spot
	int			knockedAt;		// level.time it was knocked out (0: it's up)
	int			trailAt;		// the trail point it's making for (its number; -1: its spot, straight)
	int			pathTime;		// the last look along the trail for a way straight on
	int			noWayTime;		// a look along it found no way then (0: it found one) - not looked again for RPC_PATH
	int			clearTime;		// not begun, someone where it is: the last look for somewhere free (not again for RPC_PATH)
	qboolean	arrived;		// at its spot...
	qboolean	arrivedBy;		// ...by someone standing on it: there while they are
	vec3_t		lastAt;			// where it was...
	int			lastAtTime;		// ...then
	int			stuckSince;		// getting nowhere since (0: it's getting on)
	int			teleTime;		// last brought to its owner
	int			scanTime;		// last looked about for whom to fight
	int			fightEnd;		// a fight ended (0: none since it came out)
	int			helpFrom;		// level.time it began getting its owner up (0: it isn't)
	int			helpTold;		// what its owner was told of it (rpup: since; 0 nothing)
	int			walkSpeed, runSpeed;	// its own (following, it keeps up with a player)
	int			hurtAt;			// level.time it was last hurt
	int			startHealth;	// its health to start with, per mille of its most (0: as it comes - full)
	vec3_t		lobAt;			// where its thermal's aimed (its enemy's feet)...
	int			lobTime;		// ...then (0: never)
	int			spotTime;		// a gun's last look for a clear shot from beside it...
	vec3_t		spotAt;			// ...where it's going for one...
	int			spotEnemy;		// ...at whom (their entity + 1; 0: nowhere)...
	int			spotThere;		// ...there since (0: not there)
	int			chaseSince;		// going after its enemy, getting nowhere, since (0: it isn't)...
	vec3_t		chaseAt;		// ...there...
	int			chaseEnemy;		// ...them (their entity + 1)
	int			ignoreEnemy;	// whom it lets be, having got nowhere going after them (their entity + 1; 0: nobody)...
	int			ignoreUntil;	// ...till then...
	vec3_t		ignoreAt;		// ...or till they've moved from there
} rpcOut_t;

typedef struct {
	int			charIndex;		// the character whose companions are out (G_RPCompChar; -1: none)
	int			knockGen;		// the character knocked[] and failed[] are of
	int			comeAfter;		// level.time they may come out (a moment after their owner spawns)
	int			tryAfter;		// level.time to try again (nowhere for one last time)
	int			knocked[RP_COMP_MAX];	// level.time a knocked-out one may be called again (0: it isn't knocked out)...
	char		knockedName[RP_COMP_MAX][64];	// ...that one (a character's other companion in its place isn't)
	int			savedHealth[RP_COMP_MAX];	// one put away hurt: its health, per mille of its most...
	int			savedAt[RP_COMP_MAX];	// ...then (0: none)
	char		failed[RP_COMP_MAX][RP_COMP_TYPE_SIZE];	// what one couldn't come as (it isn't tried again)
	int			newLooks;		// looks new to the level their companions brought
	rpcOut_t	out[RP_COMP_OUT_MAX];
	int			formation;		// RPCF_*
	float		faceYaw;		// the way the spots face...
	int			turnSince;		// ...turning since (0: not turning)
	int			comeTime;		// the last Come to me
	rpcPoint_t	trail[RPC_TRAIL];	// where they walked: point n at trail[n % RPC_TRAIL]...
	int			trailNext;		// ...the next one's number...
	int			trailCount;		// ...the last this many of them kept
	int			airSince;		// off the ground since (0: on it)
	int			trailTele;		// their teleport bit as its newest point was set (another: they've been taken somewhere since)
	int			hurtBy[4];		// who hurt them or theirs lately (entity + 1)...
	int			hurtTime[4];	// ...then
	int			fullTold, roomTold;	// level.time told the server's out of room for more
	char		cs[MAX_STRING_CHARS];	// their companions' configstring, as set
} rpcOwner_t;

static rpcOwner_t rpco[MAX_CLIENTS];
static const rpComp_t *rpcSpawning;	// the companion being spawned (its look: G_RPCompLook)

// player models asked whether they're on the humanoid skeleton (this level)
static struct {
	char		model[MAX_QPATH];
	qboolean	ok;
} rpcModels[RPC_MODELS_KEPT];
static int rpcNumModels;

// looks told beside their models' configstrings this level (G_RPC_Tell): a
// skin each, every player's game that sees it keeps till the map changes
static char rpcTold[RPC_TOLD_KEPT][MAX_QPATH];
static int rpcNumTold;

extern gNPC_t		*gNPCPtrs[MAX_GENTITIES];	// (NPC_spawn.c, g_utils.c: an NPC's memory, kept with its slot)
extern gclient_t	*gClPtrs[MAX_GENTITIES];

/*
================================================================================
Small things
================================================================================
*/

// What a player's told of their companions: on their screen, and the console.
static void G_RPC_Say( int clientNum, const char *text ) {
	trap->SendServerCommand( clientNum, va( "print \"%s\n\"", text ) );
	trap->SendServerCommand( clientNum, va( "cp \"%s\n\"", text ) );
}

static rpcPoint_t *G_RPC_At( rpcOwner_t *o, int n ) {
	return &o->trail[n % RPC_TRAIL];
}

// The trail's oldest point's number, and its newest's.
static int G_RPC_Oldest( const rpcOwner_t *o ) {
	return o->trailNext - o->trailCount;
}

static int G_RPC_Newest( const rpcOwner_t *o ) {
	return o->trailNext - 1;
}

// A companion's owner: client number (-1: it's nobody's).
static int G_RPC_OwnerNum( const gentity_t *ent ) {
	const int n = ent ? ent->rpCompOwner - 1 : -1;

	return ( n >= 0 && n < MAX_CLIENTS ) ? n : -1;
}

// out's NPC, if it's still there (in use, its owner's, and the one that came
// out: its type is out's own string - NPC_Spawn_Do; NULL: gone).
static gentity_t *G_RPC_Npc( int clientNum, const rpcOut_t *out ) {
	gentity_t *npc;

	if ( out->ent < MAX_CLIENTS || out->ent >= ENTITYNUM_MAX_NORMAL ) {
		return NULL;
	}
	npc = &g_entities[out->ent];
	return ( npc->inuse && npc->rpCompOwner == clientNum + 1 && npc->NPC_type == out->type && npc->client && npc->NPC ) ? npc : NULL;
}

// Which of its owner's it is (-1: it isn't one now).
static int G_RPC_Which( const gentity_t *ent ) {
	const int n = G_RPC_OwnerNum( ent );
	int k;

	if ( n < 0 ) {
		return -1;
	}
	for ( k = 0; k < RP_COMP_OUT_MAX; k++ ) {
		if ( rpco[n].out[k].ent == ent->s.number && ent->NPC_type == rpco[n].out[k].type ) {
			return k;
		}
	}
	return -1;
}

const gentity_t *G_RPCompOwner( const gentity_t *ent ) {
	const int n = G_RPC_OwnerNum( ent );

	return ( n >= 0 && G_RPC_Which( ent ) >= 0 ) ? &g_entities[n] : NULL;
}

// Whose side ent is on, companions as their owners: a client number (-1:
// nobody's - an NPC as ever, anything else).
static int G_RPC_Boss( const gentity_t *ent ) {
	if ( !ent ) {
		return -1;
	}
	if ( ent->s.number >= 0 && ent->s.number < MAX_CLIENTS ) {
		return ent->s.number;
	}
	return G_RPCompOwner( ent ) ? G_RPC_OwnerNum( ent ) : -1;
}

// The way the owner faces, for the spots: their walk's, or still, their body's.
static float G_RPC_OwnerYaw( const gentity_t *owner ) {
	const playerState_t *ps = &owner->client->ps;
	float yaw;

	if ( sqrtf( ps->velocity[0] * ps->velocity[0] + ps->velocity[1] * ps->velocity[1] ) > RPC_MOVING ) {
		return vectoyaw( ps->velocity );
	}
	if ( G_VRSaberBodyYaw( owner->s.number, &yaw ) ) {
		return yaw;	// (a VR player's body, as their headset turns it)
	}
	return ps->viewangles[YAW];
}

// Where a player's look starts and goes: a VR player's headset, or where they aim.
static void G_RPC_Look( const gentity_t *ent, vec3_t eye, vec3_t dir ) {
	if ( G_VRHeadGaze( ent, eye, dir ) ) {
		return;
	}
	VectorCopy( ent->client->ps.origin, eye );
	eye[2] += ent->client->ps.viewheight;
	AngleVectors( ent->client->ps.viewangles, dir, NULL, NULL );
}

// How many companions are up (out, not knocked out) of a player's.
static int G_RPC_Up( int clientNum ) {
	int k, n = 0;

	for ( k = 0; k < RP_COMP_OUT_MAX; k++ ) {
		n += ( rpco[clientNum].out[k].ent >= 0 && !rpco[clientNum].out[k].knockedAt ) ? 1 : 0;
	}
	return n;
}

/*
================================================================================
Where a body goes
================================================================================
*/

static const vec3_t rpcMins = { -15, -15, DEFAULT_MINS_2 }, rpcMaxs = { 15, 15, DEFAULT_MAXS_2 };
static const vec3_t rpcStepMins = { -15, -15, DEFAULT_MINS_2 + STEPSIZE };	// (a body raised a step: what's in its way, walking)

// Whether a body fits at at (pass: whose it'd be - its own isn't in its
// way), standing on something not far under it (not lava, slime) - set down
// on it.
static qboolean G_RPC_Fits( vec3_t at, int pass ) {
	trace_t tr;
	vec3_t down;

	trap->Trace( &tr, at, rpcMins, rpcMaxs, at, pass, RPC_MASK | CONTENTS_BODY, qfalse, 0, 0 );
	if ( tr.startsolid || tr.allsolid ) {
		return qfalse;
	}
	VectorCopy( at, down );
	down[2] -= 48.0f;
	trap->Trace( &tr, at, rpcMins, rpcMaxs, down, pass, RPC_MASK | RPC_UNWALKABLE, qfalse, 0, 0 );
	if ( tr.startsolid || tr.allsolid || tr.fraction >= 1.0f || ( tr.contents & RPC_UNWALKABLE ) ) {
		return qfalse;	// (nothing to stand on: a ledge, a pit, lava)
	}
	VectorCopy( tr.endpos, at );
	return qtrue;
}

// The floor under at, as a body standing on it has its origin there (*z):
// looked for with the body raised a step, so the edge of a stair it's over
// is under it, not in it. 1: found; 0: nothing near enough under it (a gap,
// a drop - or lava, slime); -1: the raised body's in something there
// (nothing learnt).
static int G_RPC_Floor( const vec3_t at, int pass, float *z ) {
	trace_t tr;
	vec3_t down;

	VectorCopy( at, down );
	down[2] -= 48.0f + 2.0f * STEPSIZE;
	trap->Trace( &tr, at, rpcStepMins, rpcMaxs, down, pass, RPC_MASK | RPC_UNWALKABLE, qfalse, 0, 0 );
	if ( ( tr.startsolid || tr.fraction < 1.0f ) && ( tr.contents & RPC_UNWALKABLE ) ) {
		return 0;
	}
	if ( tr.startsolid ) {
		return -1;
	}
	if ( tr.fraction >= 1.0f ) {
		return 0;
	}
	*z = tr.endpos[2] + STEPSIZE;
	return 1;
}

// Whether there's nothing at all near under at, where a body stood (a
// train, a lift it was on moved off: lava, slime there's something).
static qboolean G_RPC_Gone( const vec3_t at, int pass ) {
	trace_t tr;
	vec3_t down;

	VectorCopy( at, down );
	down[2] -= 48.0f + 2.0f * STEPSIZE;
	trap->Trace( &tr, at, rpcStepMins, rpcMaxs, down, pass, RPC_MASK | RPC_UNWALKABLE, qfalse, 0, 0 );
	return (qboolean)( !tr.startsolid && tr.fraction >= 1.0f );
}

// Whether a body can walk straight from from to to: nothing in the way
// above a step's height, floor all along (no gap, no drop), and nowhere up
// more than a step at a time (a straight line over a ledge's edge isn't a
// way up it), nor down off a sheer drop.
static qboolean G_RPC_Walkable( const vec3_t from, const vec3_t to, int pass ) {
	trace_t tr;
	vec3_t d, at;
	float len, s, floorZ = from[2], z;

	if ( !( Distance( from, to ) <= RPC_WALK_FAR ) ) {
		return qfalse;	// (the trail's way, or a teleport's: not this many traces - or nowhere at all)
	}
	trap->Trace( &tr, from, rpcStepMins, rpcMaxs, to, pass, RPC_MASK, qfalse, 0, 0 );
	if ( tr.startsolid || tr.fraction < 1.0f ) {
		return qfalse;
	}
	if ( G_RPC_Floor( from, pass, &z ) > 0 ) {
		floorZ = z;	// (from in the air: the floor under it)
	}
	VectorSubtract( to, from, d );
	len = VectorNormalize( d );
	for ( s = RPC_WALK_STEP; ; s += RPC_WALK_STEP ) {
		int found;
		// the floor here, and at the end (the raised body clear all along:
		// the way above was)
		if ( s >= len ) {
			VectorCopy( to, at );
		} else {
			VectorMA( from, s, d, at );
		}
		found = G_RPC_Floor( at, pass, &z );
		if ( !found ) {
			return qfalse;	// (a gap, or a drop)
		}
		if ( found > 0 ) {
			if ( z - floorZ > RPC_STEP_UP ) {
				return qfalse;	// (up more than a step)
			}
			if ( floorZ - z > 48.0f + STEPSIZE ) {
				return qfalse;	// (down further than a body looks for its floor: a sheer drop)
			}
			floorZ = z;
		}
		if ( s >= len ) {
			return qtrue;
		}
	}
}

// Whether another's body (not pass's) stands where a body at at would.
static qboolean G_RPC_Taken( const vec3_t at, int pass ) {
	trace_t tr;

	trap->Trace( &tr, at, rpcMins, rpcMaxs, at, pass, CONTENTS_BODY, qfalse, 0, 0 );
	return (qboolean)( tr.startsolid || tr.allsolid );
}

// Whether a line from a to b is clear of what a body can't go through (the
// two on one side of any wall - player clip's a wall too).
static qboolean G_RPC_Clear( const vec3_t a, const vec3_t b, int pass ) {
	trace_t tr;

	trap->Trace( &tr, a, NULL, NULL, b, pass, RPC_MASK, qfalse, 0, 0 );
	return (qboolean)( tr.fraction >= 1.0f && !tr.startsolid );
}

// Whether a line from a to b is clear of the world (what a look reaches).
static qboolean G_RPC_Sees( const vec3_t a, const vec3_t b, int pass ) {
	trace_t tr;

	trap->Trace( &tr, a, NULL, NULL, b, pass, MASK_SOLID, qfalse, 0, 0 );
	return (qboolean)( tr.fraction >= 1.0f && !tr.startsolid );
}

// Whether a companion is by its spot with someone standing on it: near it
// (RPC_LEAVE), and on its side of any wall - there, as near as it gets.
static qboolean G_RPC_By( const gentity_t *npc, const vec3_t spot, float dist ) {
	return (qboolean)( dist <= RPC_LEAVE && G_RPC_Taken( spot, npc->s.number ) && G_RPC_Clear( npc->r.currentOrigin, spot, npc->s.number ) );
}

// A companion's spot (k of count), by the way the owner's spots face (pass:
// the companion): qfalse - in a wall, over nothing, or not to be walked to
// from the owner (single file, then).
static qboolean G_RPC_Spot( const gentity_t *owner, const rpcOwner_t *o, int k, int count, int pass, vec3_t spot ) {
	const vec3_t facing = { 0.0f, o->faceYaw, 0.0f };
	vec3_t fwd, right;

	AngleVectors( facing, fwd, right, NULL );
	VectorCopy( owner->client->ps.origin, spot );
	switch ( o->formation ) {
	case RPCF_BESIDE:
		VectorMA( spot, ( count < 2 || k == 1 ) ? RPC_BESIDE : -RPC_BESIDE, right, spot );
		break;
	case RPCF_ESCORT:
		VectorMA( spot, ( k == 0 ) ? RPC_ESCORT : -RPC_ESCORT, fwd, spot );
		break;
	default:
		VectorMA( spot, -RPC_BEHIND, fwd, spot );
		if ( count > 1 ) {
			VectorMA( spot, ( k == 0 ) ? -RPC_PAIR : RPC_PAIR, right, spot );
		}
		break;
	}
	return (qboolean)( G_RPC_Fits( spot, pass ) && G_RPC_Walkable( owner->client->ps.origin, spot, owner->s.number ) );
}

// Whether the trail's newest point is where the owner is (not where they
// were a moment before they were taken somewhere): they've not been
// teleported since, and it's by them - or they're in the air a moment (it's
// where they left the ground: a jump, a fall; not a swim, a jetpack's flight).
static qboolean G_RPC_TrailHere( const gentity_t *owner, rpcOwner_t *o ) {
	const playerState_t *ps = &owner->client->ps;
	float d;

	if ( !o->trailCount || ( ps->eFlags & EF_TELEPORT_BIT ) != o->trailTele ) {
		return qfalse;
	}
	d = Distance( G_RPC_At( o, G_RPC_Newest( o ) )->at, ps->origin );
	return (qboolean)( d <= RPC_TRAIL_STEP * 4.0f ||
		( ps->groundEntityNum == ENTITYNUM_NONE && o->airSince && level.time - o->airSince <= RPC_AIR_TRAIL && d <= RPC_WALK_FAR ) );
}

// The number of the trail's point about dist back along it from its newest
// (-1: it's not that long - back to a cut in it, where it wasn't walked).
static int G_RPC_TrailBack( const gentity_t *owner, rpcOwner_t *o, float dist ) {
	const int oldest = G_RPC_Oldest( o );
	float gone = 0.0f;
	int n;

	if ( !G_RPC_TrailHere( owner, o ) ) {
		return -1;
	}

	for ( n = G_RPC_Newest( o ); n > oldest && !G_RPC_At( o, n )->cut; n-- ) {
		gone += Distance( G_RPC_At( o, n )->at, G_RPC_At( o, n - 1 )->at );
		if ( gone >= dist ) {
			return n - 1;
		}
	}
	return -1;
}

// Single file: the place on the trail behind the owner of the rank'th of
// theirs (qfalse: no trail that long).
static qboolean G_RPC_FileSpot( const gentity_t *owner, rpcOwner_t *o, int rank, vec3_t spot ) {
	const int n = G_RPC_TrailBack( owner, o, RPC_BEHIND * ( rank + 1 ) );

	if ( n < 0 ) {
		return qfalse;
	}
	VectorCopy( G_RPC_At( o, n )->at, spot );
	return qtrue;
}

// How far a companion has to go to its owner: along their trail from the
// point of it it's by (round corners, as it goes - the newest of those about
// as near it, where the trail's come back that way), else straight.
static float G_RPC_WayLeft( const gentity_t *owner, rpcOwner_t *o, const gentity_t *npc ) {
	const int oldest = G_RPC_Oldest( o ), newest = G_RPC_Newest( o );
	float best = 0.0f, left;
	int n, m;

	if ( !o->trailCount ) {
		return Distance( npc->r.currentOrigin, owner->client->ps.origin );
	}
	for ( n = newest; n >= oldest; n-- ) {
		const float d = Distance( npc->r.currentOrigin, G_RPC_At( o, n )->at );
		if ( n == newest || d < best ) {
			best = d;
		}
	}
	for ( m = newest; m > oldest && Distance( npc->r.currentOrigin, G_RPC_At( o, m )->at ) > best + RPC_TRAIL_STEP; m-- );
	left = Distance( npc->r.currentOrigin, G_RPC_At( o, m )->at );
	for ( n = m; n < newest; n++ ) {
		left += Distance( G_RPC_At( o, n )->at, G_RPC_At( o, n + 1 )->at );
	}
	return left + Distance( G_RPC_At( o, newest )->at, owner->client->ps.origin );
}

// Its rank in single file (0: just behind its owner): the nearer them of two
// - the way it has to go, round a corner too - takes the nearer place, by
// RPC_FILE_SURE at least (else by their order): neither to go past the other
// where there's no room to.
static int G_RPC_FileRank( const gentity_t *owner, rpcOwner_t *o, int k, const gentity_t *npc ) {
	const float mine = G_RPC_WayLeft( owner, o, npc );
	int j, rank = 0;

	for ( j = 0; j < RP_COMP_OUT_MAX; j++ ) {
		const gentity_t *other = ( j == k || o->out[j].knockedAt ) ? NULL : G_RPC_Npc( owner->s.number, &o->out[j] );
		float theirs;
		if ( !other ) {
			continue;
		}
		theirs = G_RPC_WayLeft( owner, o, other );
		if ( theirs < mine - RPC_FILE_SURE || ( fabsf( theirs - mine ) <= RPC_FILE_SURE && j < k ) ) {
			rank++;
		}
	}
	return rank;
}

// Somewhere a companion (pass) can come to its owner: just behind them on
// their trail (back to a cut in it, at most: they didn't walk there), out of
// their sight where there's room (else in it), else round about them,
// behind first.
static qboolean G_RPC_NearOwner( gentity_t *owner, rpcOwner_t *o, int pass, vec3_t at ) {
	const int oldest = G_RPC_Oldest( o );
	vec3_t eye, dir, d, p;
	float gone = 0.0f;
	int n, i, pick = -1;

	G_RPC_Look( owner, eye, dir );
	for ( n = G_RPC_TrailHere( owner, o ) ? G_RPC_Newest( o ) : oldest; n > oldest && gone < RPC_ESCORT * 3.0f && !G_RPC_At( o, n )->cut; n-- ) {
		gone += Distance( G_RPC_At( o, n )->at, G_RPC_At( o, n - 1 )->at );
		if ( gone < RPC_BEHIND ) {
			continue;
		}
		VectorCopy( G_RPC_At( o, n - 1 )->at, p );
		if ( !G_RPC_Fits( p, pass ) ) {
			continue;
		}
		VectorSubtract( p, eye, d );
		if ( DotProduct( d, dir ) < 0.0f || !G_RPC_Sees( eye, p, owner->s.number ) ) {
			VectorCopy( p, at );
			return qtrue;	// (out of their sight)
		}
		if ( pick < 0 ) {
			pick = n - 1;
		}
	}
	if ( pick >= 0 ) {
		VectorCopy( G_RPC_At( o, pick )->at, at );
		if ( G_RPC_Fits( at, pass ) ) {
			return qtrue;
		}
	}
	for ( i = 0; i < 8; i++ ) {
		const vec3_t a = { 0.0f, o->faceYaw + 180.0f + ( ( i & 1 ) ? 1.0f : -1.0f ) * 45.0f * (float)( ( i + 1 ) / 2 ), 0.0f };
		vec3_t f;
		AngleVectors( a, f, NULL, NULL );
		VectorMA( owner->client->ps.origin, RPC_BEHIND * 1.2f, f, at );
		if ( G_RPC_Fits( at, pass ) && G_RPC_Clear( owner->client->ps.origin, at, owner->s.number ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

/*
================================================================================
The owner's trail, and the way their spots face
================================================================================
*/

static void G_RPC_TrailClear( rpcOwner_t *o ) {
	o->trailNext = o->trailCount = 0;
	o->airSince = 0;
}

// A point of the owner's trail every RPC_TRAIL_STEP they go, where they
// stand; one that can't be walked to from the one before is cut (a lift, a
// train, a turning platform; up or down a ledge; a drop; a jump over a gap or
// a long one; a teleport; lava, slime).
static void G_RPC_TrailFrame( gentity_t *owner, rpcOwner_t *o ) {
	const playerState_t *ps = &owner->client->ps;
	rpcPoint_t *last = o->trailCount ? G_RPC_At( o, G_RPC_Newest( o ) ) : NULL;
	rpcPoint_t *p;

	if ( ps->groundEntityNum == ENTITYNUM_NONE ) {
		if ( !o->airSince ) {
			o->airSince = level.time ? level.time : 1;
		}
		return;	// (the air's no place to walk to)
	}
	if ( last && Distance( last->at, ps->origin ) < RPC_TRAIL_STEP ) {
		o->airSince = 0;
		return;
	}
	p = G_RPC_At( o, o->trailNext );
	VectorCopy( ps->origin, p->at );
	p->cut = qfalse;
	if ( last ) {
		const float dxy = sqrtf( ( p->at[0] - last->at[0] ) * ( p->at[0] - last->at[0] ) + ( p->at[1] - last->at[1] ) * ( p->at[1] - last->at[1] ) );
		const float dz = p->at[2] - last->at[2];
		const gentity_t *under = ( ps->groundEntityNum >= 0 && ps->groundEntityNum < ENTITYNUM_MAX_NORMAL ) ? &g_entities[ps->groundEntityNum] : NULL;
		// (riding: a lift, a train on its way, a turning platform - someone's
		// head isn't: they're walked round, as anyone in the way is)
		const qboolean riding = (qboolean)( under && under->s.eType == ET_MOVER &&
			( under->s.pos.trType != TR_STATIONARY || under->s.apos.trType != TR_STATIONARY ) );
		// (up or down steeper than stairs: a step up - just up one, a
		// stair's not a ledge - or a ledge)
		p->cut = (qboolean)( dxy > RPC_TRAIL_STEP * 4.0f || ( ps->eFlags & EF_TELEPORT_BIT ) != o->trailTele || riding ||
			( owner->waterlevel && ( owner->watertype & RPC_UNWALKABLE ) ) ||
			( fabsf( dz ) > dxy + 8.0f && !G_RPC_Walkable( last->at, p->at, owner->s.number ) ) ||
			( o->airSince && ( level.time - o->airSince > 600 || !G_RPC_Walkable( last->at, p->at, owner->s.number ) ) ) );
	}
	o->trailNext++;
	if ( o->trailCount < RPC_TRAIL ) {
		o->trailCount++;
	}
	o->trailTele = ps->eFlags & EF_TELEPORT_BIT;
	o->airSince = 0;
}

// The spots' way: the owner's, once they've turned past RPC_TURN for RPC_TURN_TIME (at once, walking).
static void G_RPC_FaceFrame( gentity_t *owner, rpcOwner_t *o ) {
	const playerState_t *ps = &owner->client->ps;
	const float yaw = G_RPC_OwnerYaw( owner );
	const qboolean walking = (qboolean)( sqrtf( ps->velocity[0] * ps->velocity[0] + ps->velocity[1] * ps->velocity[1] ) > RPC_MOVING );

	if ( walking ) {
		o->faceYaw = yaw;
		o->turnSince = 0;
	} else if ( fabsf( AngleDelta( yaw, o->faceYaw ) ) < RPC_TURN ) {
		o->turnSince = 0;
	} else if ( !o->turnSince ) {
		o->turnSince = level.time ? level.time : 1;
	} else if ( level.time - o->turnSince >= RPC_TURN_TIME ) {
		o->faceYaw = yaw;
		o->turnSince = 0;
	}
}

/*
================================================================================
What they fight as, and how they look
================================================================================
*/

// An NPC type as the NPC files have it: its class, model and skin (qfalse:
// there's no such type, or NPC_ParseParms wouldn't spawn it - no player
// model, a file cut short).
static qboolean G_RPC_TypeInfo( const char *type, char *cls, int clsSize, char *model, int modelSize, char *skin, int skinSize ) {
	const char *p = NPCParms, *tok;

	cls[0] = model[0] = '\0';
	Q_strncpyz( skin, "default", skinSize );
	if ( !BG_VRCompTypeOk( type ) ) {
		return qfalse;
	}
	COM_BeginParseSession( "rp companion type" );
	while ( 1 ) {
		tok = COM_ParseExt( &p, qtrue );
		if ( !tok[0] ) {
			return qfalse;
		}
		if ( !Q_stricmp( tok, type ) ) {
			break;
		}
		SkipBracedSection( &p, 0 );
	}
	tok = COM_ParseExt( &p, qtrue );
	if ( strcmp( tok, "{" ) ) {
		return qfalse;
	}
	while ( 1 ) {
		tok = COM_ParseExt( &p, qtrue );
		if ( !tok[0] ) {
			return qfalse;
		}
		if ( !strcmp( tok, "}" ) ) {
			break;
		}
		if ( !strcmp( tok, "{" ) ) {
			SkipBracedSection( &p, 1 );
		} else if ( !Q_stricmp( tok, "class" ) ) {
			Q_strncpyz( cls, COM_ParseExt( &p, qfalse ), clsSize );
		} else if ( !Q_stricmp( tok, "playerModel" ) ) {
			Q_strncpyz( model, COM_ParseExt( &p, qfalse ), modelSize );
		} else if ( !Q_stricmp( tok, "customSkin" ) ) {
			Q_strncpyz( skin, COM_ParseExt( &p, qfalse ), skinSize );
		} else {
			SkipRestOfLine( &p );
		}
	}
	return (qboolean)( model[0] != '\0' );
}

// Whether a file's on the server.
static qboolean G_RPC_FileThere( const char *path ) {
	fileHandle_t f = 0;
	const int len = trap->FS_Open( path, &f, FS_READ );

	if ( f ) {
		trap->FS_Close( f );
	}
	return (qboolean)( f && len > 0 );
}

// Whether a player model's on the server, on the humanoid skeleton (one on
// another can't be a companion: SetupGameGhoul2Model would take its
// animations from the skeleton's, and drop the level where there are none)
// - as its model.glm's first bytes say: read, not loaded (a player's asking
// loads nothing), and kept.
static qboolean G_RPC_ModelOk( const char *model ) {
	char path[MAX_QPATH];
	byte head[BG_VRCOMP_GLM_HEAD];
	fileHandle_t f = 0;
	int i, len;
	qboolean ok = qfalse;

	if ( !model[0] || strchr( model, '.' ) || strchr( model, ':' ) || strchr( model, '\\' ) || strchr( model, '/' ) || strlen( model ) > 32 ) {
		return qfalse;
	}
	for ( i = 0; i < rpcNumModels; i++ ) {
		if ( !Q_stricmp( rpcModels[i].model, model ) ) {
			return rpcModels[i].ok;
		}
	}
	Com_sprintf( path, sizeof( path ), "models/players/%s/model.glm", model );
	len = trap->FS_Open( path, &f, FS_READ );
	if ( f ) {
		if ( len >= (int)sizeof( head ) && trap->FS_Read( head, sizeof( head ), f ) == (int)sizeof( head ) ) {
			ok = BG_VRCompGlmHumanoid( head, sizeof( head ) );
		}
		trap->FS_Close( f );
	}
	if ( rpcNumModels < RPC_MODELS_KEPT ) {
		Q_strncpyz( rpcModels[rpcNumModels].model, model, sizeof( rpcModels[0].model ) );
		rpcModels[rpcNumModels].ok = ok;
		rpcNumModels++;
	}
	return ok;
}

// Whether a model's skin is on the server: a skin's file, or each of a
// custom character's three parts' (head|torso|lower).
static qboolean G_RPC_SkinOk( const char *model, const char *skin ) {
	char parts[MAX_QPATH], *p, *next;

	if ( !strchr( skin, '|' ) ) {
		return G_RPC_FileThere( va( "models/players/%s/model_%s.skin", model, skin[0] ? skin : "default" ) );
	}
	Q_strncpyz( parts, skin, sizeof( parts ) );
	for ( p = parts; p; p = next ) {
		next = strchr( p, '|' );
		if ( next ) {
			*next++ = '\0';
		}
		if ( !p[0] || !G_RPC_FileThere( va( "models/players/%s/%s.skin", model, p ) ) ) {
			return qfalse;
		}
	}
	return qtrue;
}

// The model configstring an NPC with this model and skin takes (as
// SetupGameGhoul2Model makes it), and whether it's there already.
static qboolean G_RPC_ModelCsThere( const char *model, const char *skin ) {
	char want[MAX_QPATH * 2], cs[MAX_STRING_CHARS];
	int i;

	Com_sprintf( want, sizeof( want ), "models/players/%s/model.glm*%s", model, ( skin[0] && BG_VRCompSkinInCs( model, skin ) ) ? skin : "default" );
	for ( i = 1; i < MAX_MODELS; i++ ) {
		trap->GetConfigstring( CS_MODELS + i, cs, sizeof( cs ) );
		if ( !cs[0] ) {
			return qfalse;
		}
		if ( !strcmp( cs, want ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

// Whether a look's skin is one told beside its model's configstring (too
// long for it: BG_VRCompSkinInCs) that's new to the level - every player's
// game that sees it keeps it till the map changes, as a configstring.
static qboolean G_RPC_ToldNew( const char *model, const char *skin, const char *look ) {
	int i;

	if ( BG_VRCompSkinInCs( model, skin ) ) {
		return qfalse;	// (its configstring carries it: nothing told)
	}
	for ( i = 0; i < rpcNumTold; i++ ) {
		if ( !Q_stricmp( rpcTold[i], look ) ) {
			return qfalse;	// (as a player's game keeps skins: whatever their letters' case)
		}
	}
	return qtrue;
}

// How many of a kind of configstring are free, as many as need at most
// (they're taken in order: the free ones are the last).
static int G_RPC_CsFree( int start, int max, int need ) {
	char s[MAX_QPATH];
	int i, n = 0;

	for ( i = max - 1; i > 0 && n < need; i-- ) {
		trap->GetConfigstring( start + i, s, sizeof( s ) );
		if ( s[0] ) {
			break;
		}
		n++;
	}
	return n;
}

// Whether G_Spawn would hand out a free entity slot: not one freed this last
// second (but in the level's first moments).
static qboolean G_RPC_SlotFree( const gentity_t *e ) {
	return (qboolean)( !e->inuse && ( e->freetime <= level.startTime + 2000 || level.time - e->freetime >= 1000 ) );
}

// What the configstrings take of a gamestate (as a player's game counts it:
// each and its end; past MAX_GAMESTATE_CHARS, every player's dropped).
static int G_RPC_GameStateChars( void ) {
	static char cs[BIG_INFO_STRING];
	int i, n = 1;

	for ( i = 0; i < MAX_CONFIGSTRINGS; i++ ) {
		trap->GetConfigstring( i, cs, sizeof( cs ) );
		if ( cs[0] ) {
			n += (int)strlen( cs ) + 1;
		}
	}
	return n;
}

// Whether the level has room for another companion: entities (as G_Spawn
// hands them out - near the most there are, it opens no new ones and drops
// the level), what it registers - models, sounds, effects: run out of
// those, and the level's dropped - and room in the gamestate.
static qboolean G_RPC_Room( void ) {
	int i, n = ENTITYNUM_MAX_NORMAL - level.num_entities;	// (never opened yet)

	for ( i = MAX_CLIENTS; i < level.num_entities && n < RPC_ROOM_ENTS; i++ ) {
		if ( G_RPC_SlotFree( &g_entities[i] ) ) {
			n++;
		}
	}
	return (qboolean)( n >= RPC_ROOM_ENTS && G_RPC_CsFree( CS_MODELS, MAX_MODELS, RPC_ROOM_MODELS ) >= RPC_ROOM_MODELS &&
		G_RPC_CsFree( CS_SOUNDS, MAX_SOUNDS, RPC_ROOM_SOUNDS ) >= RPC_ROOM_SOUNDS && G_RPC_CsFree( CS_EFFECTS, MAX_FX, RPC_ROOM_FX ) >= RPC_ROOM_FX &&
		G_RPC_GameStateChars() + RPC_ROOM_CHARS < MAX_GAMESTATE_CHARS );
}

// NPC_Spawn_Do: a companion's entity - a slot an NPC's had before where
// there's one (the game keeps an NPC's memory with its slot for good, and
// companions come and go: a slot new to NPCs every time would use it up),
// else as any's.
gentity_t *G_RPCompEntity( void ) {
	int i;

	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		gentity_t *e = &g_entities[i];
		if ( gNPCPtrs[i] && gClPtrs[i] && G_RPC_SlotFree( e ) ) {
			G_InitGentity( e );
			return e;
		}
	}
	return G_Spawn();
}

// The look a companion of a player's comes out with: its own, if it may (as
// its type comes, "": the server keeps them so, it isn't here, or it'd be
// a look new to the level past their RPC_NEW_LOOKS - said once).
static const char *G_RPC_LookFor( int clientNum, const rpComp_t *c ) {
	char model[MAX_QPATH], skin[MAX_QPATH];
	rpcOwner_t *o = &rpco[clientNum];
	qboolean toldNew;

	if ( !c->look[0] || !g_rpCompanionModels.integer || !BG_VRCompLookOk( c->look ) ) {
		return "";
	}
	BG_VRCompLookSplit( c->look, model, sizeof( model ), skin, sizeof( skin ) );
	if ( !G_RPC_ModelOk( model ) || !G_RPC_SkinOk( model, skin ) ) {
		return "";
	}
	toldNew = G_RPC_ToldNew( model, skin, c->look );
	if ( !G_RPC_ModelCsThere( model, skin ) || toldNew ) {
		if ( o->newLooks >= RPC_NEW_LOOKS ) {
			if ( o->newLooks == RPC_NEW_LOOKS ) {
				o->newLooks++;	// (said once)
				G_RPC_Say( clientNum, va( "Your companions have brought %i new looks to this map: %s" S_COLOR_WHITE " comes as its type does until the next",
					RPC_NEW_LOOKS, c->name ) );
			}
			return "";
		}
		o->newLooks++;
		if ( toldNew && rpcNumTold < RPC_TOLD_KEPT ) {
			Q_strncpyz( rpcTold[rpcNumTold++], c->look, sizeof( rpcTold[0] ) );	// (told from now on: G_RPC_Tell)
		}
	}
	return c->look;
}

// Why type won't do for a companion to fight as (NULL: it will).
const char *G_RPCompTypeProblem( const char *type ) {
	char cls[64], model[MAX_QPATH], skin[MAX_QPATH], look[MAX_QPATH * 2];

	if ( !G_RPC_TypeInfo( type, cls, sizeof( cls ), model, sizeof( model ), skin, sizeof( skin ) ) ) {
		return "Pick what it fights as from the list";
	}
	if ( !BG_VRCompClassOk( cls ) ) {
		return "A companion fights as one of the people: no droids, beasts or vehicles";
	}
	// its own model and skin: as a look would be (names the game takes), and
	// short enough for SetupGameGhoul2Model's buffer and the configstring
	// together (its skin isn't told beside)
	Com_sprintf( look, sizeof( look ), "%s/%s", model, skin[0] ? skin : "default" );
	if ( !BG_VRCompLookOk( look ) || !BG_VRCompSkinInCs( model, skin ) || !G_RPC_ModelOk( model ) ) {
		return "That one can't be a companion on this server";
	}
	return NULL;
}

// Why look won't do for a companion (NULL: it will).
const char *G_RPCompLookProblem( const char *look ) {
	char model[MAX_QPATH], skin[MAX_QPATH];

	if ( !BG_VRCompLookOk( look ) ) {
		return "That look's name isn't one the game takes";
	}
	if ( !look[0] ) {
		return NULL;
	}
	if ( !g_rpCompanionModels.integer ) {
		return "This server keeps companions looking as their type comes: choose As it comes";
	}
	BG_VRCompLookSplit( look, model, sizeof( model ), skin, sizeof( skin ) );
	if ( !G_RPC_ModelOk( model ) ) {
		return "This server doesn't have that model (or it isn't a person's)";
	}
	if ( !G_RPC_SkinOk( model, skin ) ) {
		return "This server doesn't have that skin for the model";
	}
	return NULL;
}

// NPC_ParseParms, as it sets the model up: the look of the companion being
// spawned, if it has one of its own (G_RPC_Spawn).
void G_RPCompLook( gentity_t *npc, char *model, int modelSize, char *skin, int skinSize ) {
	if ( !rpcSpawning || !rpcSpawning->look[0] ) {
		return;
	}
	BG_VRCompLookSplit( rpcSpawning->look, model, modelSize, skin, skinSize );
	if ( !BG_VRCompSkinInCs( model, skin ) ) {
		Q_strncpyz( skin, "default", skinSize );	// (too long for its configstring: the players' games are told it beside - G_RPC_Tell)
	}
}

// NPC_Spawn_Do: whether a spawner's a companion's (its NPC type, kept with
// it, isn't copied: no string spent a spawn).
qboolean G_RPCompSpawner( const gentity_t *spawner ) {
	return (qboolean)( spawner && spawner->rpCompOwner > 0 );
}

/*
================================================================================
Coming out, and going
================================================================================
*/

// Whether a player's companions may be out: roleplay on, here, in the game
// (not spectating), alive or down, playing a character.
static qboolean G_RPC_Wanted( int clientNum ) {
	const gentity_t *ent = &g_entities[clientNum];
	const gclient_t *cl = ent->client;

	if ( !G_RPOn() || g_rpCompanions.integer <= 0 || level.gametype == GT_SIEGE || level.gametype == GT_DUEL ||
		level.gametype == GT_POWERDUEL || clientNum >= level.maxclients ) {
		return qfalse;
	}
	return (qboolean)( ent->inuse && cl && cl->pers.connected == CON_CONNECTED && cl->sess.sessionTeam != TEAM_SPECTATOR &&
		!( cl->ps.pm_flags & PMF_FOLLOW ) && !( ent->r.svFlags & SVF_BOT ) && ( ent->health > 0 || cl->rpDowned ) &&
		cl->ps.pm_type != PM_DEAD && cl->ps.pm_type != PM_SPECTATOR && G_RPCompChar( clientNum ) >= 0 );
}

// Whether a character's companion (slot s) is knocked out, still waiting:
// that one - by its name (another character's in its place since isn't).
static qboolean G_RPC_Knocked( const rpcOwner_t *o, int s, const rpComp_t *c ) {
	return (qboolean)( o->knocked[s] && c && !Q_stricmp( o->knockedName[s], c->name ) );
}

// The companions a player's character brings out: their slots, at most
// g_rpCompanions (knocked-out ones still waiting, and ones that couldn't
// come as what they are, left in).
static int G_RPC_WantedSlots( int clientNum, int *slots ) {
	const rpcOwner_t *o = &rpco[clientNum];
	const int most = Com_Clampi( 0, RP_COMP_OUT_MAX, g_rpCompanions.integer );
	int s, n = 0;

	for ( s = 0; s < RP_COMP_MAX && n < most; s++ ) {
		const rpComp_t *c = G_RPComp( clientNum, s );
		if ( c && c->out && !G_RPC_Knocked( o, s, c ) && Q_stricmp( o->failed[s], c->type ) ) {
			slots[n++] = s;
		}
	}
	return n;
}

// Companions out on the server.
static int G_RPC_Count( void ) {
	int i, k, n = 0;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		for ( k = 0; k < RP_COMP_OUT_MAX; k++ ) {
			n += ( rpco[i].out[k].ent >= 0 ) ? 1 : 0;
		}
	}
	return n;
}

// The NPCs' side a companion of a player takes: their faction's.
static void G_RPC_Sides( gentity_t *npc, int owner ) {
	const int faction = G_RPFaction( owner );
	const int side = ( faction == RPF_HEROES ) ? NPCTEAM_PLAYER : ( faction == RPF_VILLAINS ) ? NPCTEAM_ENEMY : NPCTEAM_NEUTRAL;
	const gclient_t *cl = g_entities[owner].client;

	npc->client->playerTeam = (npcteam_t)side;
	npc->s.teamowner = side;
	npc->client->enemyTeam = ( side == NPCTEAM_ENEMY ) ? NPCTEAM_PLAYER : NPCTEAM_ENEMY;
	npc->client->ps.persistant[PERS_TEAM] = side;	// (as NPC_Begin has it)
	if ( level.gametype >= GT_TEAM && cl ) {
		npc->client->sess.sessionTeam = cl->sess.sessionTeam;	// (their team's, for who may hurt whom)
		npc->alliedTeam = cl->sess.sessionTeam;	// (the game's own: turrets, sentries and the like leave it be)
	}
}

// A companion's alt-fire: a thermal's only - it bursts where it lands, at
// its target's feet or on them (G_RPCompLob), not bouncing on; anything else
// as it comes (fists the thrower takes up: a punch, not a kick or a grab, with
// g_debugMelee on). (An NPC file's altFire never lasts: NPC_DefaultScriptFlags.)
static void G_RPC_AltFire( gentity_t *npc ) {
	if ( npc->client->ps.weapon == WP_THERMAL ) {
		npc->NPC->scriptFlags |= SCF_ALT_FIRE;
	} else {
		npc->NPC->scriptFlags &= ~SCF_ALT_FIRE;
	}
}

// How a companion goes about things: by its owner, not as an NPC of the
// level - no squads, alerts, chatter or mind tricks, enemies only as
// G_RPCompEnemy says (fighting: chasing them); holding its fire only as
// G_RPC_Weapon says; alt-firing only as G_RPC_AltFire does.
static void G_RPC_Flags( gentity_t *npc, qboolean fighting ) {
	gNPC_t *info = npc->NPC;

	info->scriptFlags |= SCF_NO_GROUPS | SCF_NO_MIND_TRICK | SCF_IGNORE_ALERTS | SCF_NO_COMBAT_TALK | SCF_NO_ALERT_TALK;
	info->scriptFlags &= ~( SCF_LOOK_FOR_ENEMIES | SCF_RUNNING | SCF_WALKING | SCF_FORCED_MARCH | SCF_DONT_FIRE );
	G_RPC_AltFire( npc );
	if ( fighting ) {
		info->scriptFlags |= SCF_CHASE_ENEMIES;
	} else {
		info->scriptFlags &= ~SCF_CHASE_ENEMIES;
	}
	info->aiFlags |= NPCAI_NO_SLOWDOWN;
	info->defaultBehavior = BS_DEFAULT;	// (some come cinematic: NPC_SetMiscDefaultData)
	if ( info->behaviorState == BS_CINEMATIC ) {
		info->behaviorState = BS_DEFAULT;
	}
	if ( info->tempBehavior == BS_CINEMATIC ) {
		info->tempBehavior = BS_DEFAULT;
	}
	if ( info->group ) {
		AI_DeleteSelfFromGroup( npc );
	}
}

// The players' games told whose companions are whose (CS_VR_COMPANIONS + owner).
static void G_RPC_Tell( int clientNum ) {
	rpcOwner_t *o = &rpco[clientNum];
	char cs[MAX_STRING_CHARS];
	int k;

	cs[0] = '\0';
	for ( k = 0; k < RP_COMP_OUT_MAX; k++ ) {
		const rpComp_t *c;
		char model[MAX_QPATH], skin[MAX_QPATH];
		if ( o->out[k].ent < 0 || o->out[k].knockedAt ) {
			continue;
		}
		c = G_RPComp( clientNum, o->out[k].slot );
		BG_VRCompLookSplit( o->out[k].worn, model, sizeof( model ), skin, sizeof( skin ) );
		Q_strcat( cs, sizeof( cs ), va( "%s%i\\%s\\%s", cs[0] ? "\\" : "", o->out[k].ent, c ? c->name : "",
			( o->out[k].worn[0] && !BG_VRCompSkinInCs( model, skin ) ) ? o->out[k].worn : "" ) );
	}
	if ( strcmp( cs, o->cs ) ) {
		Q_strncpyz( o->cs, cs, sizeof( o->cs ) );
		trap->SetConfigstring( CS_VR_COMPANIONS + clientNum, cs );
	}
}

// What its owner's game is told of a companion getting them up: now, since
// when (0: it isn't; -1: it got them up) - as it changes.
static void G_RPC_HelpTell( int clientNum, rpcOut_t *out, int now ) {
	if ( now > 0 ? now == out->helpTold : !out->helpTold ) {
		return;	// (as told; or nothing told, nothing to take back)
	}
	out->helpTold = ( now > 0 ) ? now : 0;
	if ( G_RPCapable( clientNum ) && out->ent >= 0 ) {
		trap->SendServerCommand( clientNum, va( "rpup %i %i %i", out->ent, clientNum, now ) );
	}
}

// Brings companion slot out for a player, as their k'th of count (qfalse: it couldn't come).
static qboolean G_RPC_Spawn( int clientNum, int k, int slot, int count ) {
	static char tags[MAX_CLIENTS][16];
	gentity_t *owner = &g_entities[clientNum], *sp, *npc;
	rpcOwner_t *o = &rpco[clientNum];
	rpcOut_t *out = &o->out[k];
	const rpComp_t *c = G_RPComp( clientNum, slot );
	rpComp_t as;
	vec3_t at;

	if ( !c || !c->name[0] ) {
		return qfalse;
	}
	if ( !G_RPC_Spot( owner, o, k, count, ENTITYNUM_NONE, at ) && !( G_RPC_FileSpot( owner, o, k, at ) && G_RPC_Fits( at, ENTITYNUM_NONE ) ) &&
		!G_RPC_NearOwner( owner, o, ENTITYNUM_NONE, at ) ) {
		return qfalse;	// (nowhere a body fits: once there's room)
	}
	if ( G_RPCompTypeProblem( c->type ) ) {
		Q_strncpyz( o->failed[slot], c->type, sizeof( o->failed[0] ) );	// (not again, as that)
		G_RPC_Say( clientNum, va( "%s" S_COLOR_WHITE " can't come out as %s on this server: choose another for it to fight as", c->name, c->type ) );
		return qfalse;
	}
	as = *c;
	Q_strncpyz( as.look, G_RPC_LookFor( clientNum, c ), sizeof( as.look ) );

	memset( out, 0, sizeof( *out ) );
	out->ent = -1;
	out->slot = slot;
	Q_strncpyz( out->type, c->type, sizeof( out->type ) );
	Q_strncpyz( out->look, c->look, sizeof( out->look ) );	// (as it was set: a change brings it out anew)
	Q_strncpyz( out->worn, as.look, sizeof( out->worn ) );
	memcpy( out->rgb, c->rgb, sizeof( out->rgb ) );
	out->order = RPCO_DEFEND;
	out->target = -1;
	out->trailAt = -1;
	if ( o->savedAt[slot] && level.time - o->savedAt[slot] < RPC_KNOCKED_WAIT ) {
		out->startHealth = o->savedHealth[slot];	// (put away hurt a moment ago: as hurt, once it's begun - G_RPC_StartHealth)
	}
	o->savedAt[slot] = 0;

	Com_sprintf( tags[clientNum], sizeof( tags[0] ), "vrc_%i", clientNum );
	sp = G_Spawn();
	sp->classname = "rp_companion_spawner";
	sp->think = G_FreeEntity;	// (NPC_Spawn_Do frees it - but where it gives up half way)
	sp->nextthink = level.time + FRAMETIME;
	VectorCopy( at, sp->s.origin );
	G_SetOrigin( sp, at );
	sp->s.angles[YAW] = o->faceYaw;
	sp->NPC_type = out->type;			// (the NPC's own: G_RPCompSpawner)
	sp->NPC_targetname = tags[clientNum];
	sp->count = 1;
	sp->rpCompOwner = clientNum + 1;
	rpcSpawning = &as;
	npc = NPC_Spawn_Do( sp );
	rpcSpawning = NULL;
	if ( npc && ( !npc->NPC || !npc->client ) ) {
		G_FreeEntity( npc );	// (half made: NPC_Spawn_Do ran out of something)
		npc = NULL;
	}
	if ( !npc ) {
		Q_strncpyz( o->failed[slot], c->type, sizeof( o->failed[0] ) );
		G_LogPrintf( "Roleplay: %s" S_COLOR_WHITE "'s companion %s couldn't come out (NPC type %s)\n", owner->client->pers.netname, c->name, c->type );
		G_RPC_Say( clientNum, va( "%s" S_COLOR_WHITE " couldn't come out as %s: choose another for it to fight as", c->name, c->type ) );
		return qfalse;
	}
	npc->rpCompOwner = clientNum + 1;
	out->ent = npc->s.number;
	if ( as.look[0] ) {
		npc->client->ps.customRGBA[0] = as.rgb[0];
		npc->client->ps.customRGBA[1] = as.rgb[1];
		npc->client->ps.customRGBA[2] = as.rgb[2];
		npc->client->ps.customRGBA[3] = 255;
	}
	G_RPC_Sides( npc, clientNum );
	G_RPC_Flags( npc, qfalse );
	out->walkSpeed = npc->NPC->stats.walkSpeed;
	out->runSpeed = npc->NPC->stats.runSpeed;
	VectorCopy( at, out->lastAt );
	out->lastAtTime = level.time;
	G_LogPrintf( "Roleplay: %s" S_COLOR_WHITE "'s companion %s came out (%s%s%s)\n", owner->client->pers.netname, c->name, c->type,
		as.look[0] ? ", " : "", as.look );
	return qtrue;
}

// A companion gone (despawned: its owner's gone, it was changed, or it was
// knocked out and its body's time is up).
static void G_RPC_Remove( int clientNum, int k ) {
	rpcOut_t *out = &rpco[clientNum].out[k];
	gentity_t *npc = G_RPC_Npc( clientNum, out );

	G_RPC_HelpTell( clientNum, out, 0 );
	if ( npc ) {
		if ( npc->NPC->combatPoint >= 0 ) {
			NPC_FreeCombatPoint( npc->NPC->combatPoint, qfalse );
		}
		if ( npc->NPC->group ) {
			AI_DeleteSelfFromGroup( npc );
		}
		if ( npc->NPC->tempGoal ) {
			G_FreeEntity( npc->NPC->tempGoal );
			npc->NPC->tempGoal = NULL;
		}
		G_FreeEntity( npc );
	}
	out->ent = -1;
	out->knockedAt = 0;
}

// One put away (unticked, changed) hurt: its health kept, to come out as hurt.
static void G_RPC_KeepHealth( int clientNum, int k ) {
	rpcOwner_t *o = &rpco[clientNum];
	const rpcOut_t *out = &o->out[k];
	const gentity_t *npc = G_RPC_Npc( clientNum, out );
	const int most = npc ? npc->client->ps.stats[STAT_MAX_HEALTH] : 0;

	if ( !npc || out->knockedAt || out->slot < 0 || out->slot >= RP_COMP_MAX ) {
		return;
	}
	if ( out->startHealth > 0 ) {
		o->savedHealth[out->slot] = out->startHealth;	// (put away before it began: as it came)
		o->savedAt[out->slot] = level.time ? level.time : 1;
		return;
	}
	if ( most <= 0 || npc->health >= most ) {
		return;
	}
	o->savedHealth[out->slot] = Com_Clampi( 1, 999, npc->health * 1000 / most );
	o->savedAt[out->slot] = level.time ? level.time : 1;
}

// One out, not begun yet: NPC_Begin waits while someone stands where it
// would (one of theirs come to its spot by it - a body's not there till it's
// begun - its owner stepped back): somewhere else, free as NPC_Begin sees it
// (its spot, single file, by its owner), it begins at NPC_Begin's next try.
static void G_RPC_BeginClear( int clientNum, int k ) {
	gentity_t *owner = &g_entities[clientNum];
	rpcOwner_t *o = &rpco[clientNum];
	rpcOut_t *out = &o->out[k];
	gentity_t *npc = G_RPC_Npc( clientNum, out );
	vec3_t at, was;
	int i;

	if ( !npc || npc->think != NPC_Begin || !owner->client || ( out->clearTime && level.time - out->clearTime < RPC_PATH ) || !NPC_SpotWouldTelefrag( npc ) ) {
		return;
	}
	out->clearTime = level.time ? level.time : 1;
	VectorCopy( npc->r.currentOrigin, was );
	for ( i = 0; i < 3; i++ ) {
		if ( !( ( i == 0 ) ? G_RPC_Spot( owner, o, k, G_RPC_Up( clientNum ), npc->s.number, at ) :
			( i == 1 ) ? ( G_RPC_FileSpot( owner, o, k, at ) && G_RPC_Fits( at, npc->s.number ) ) : G_RPC_NearOwner( owner, o, npc->s.number, at ) ) ) {
			continue;
		}
		G_SetOrigin( npc, at );
		VectorCopy( at, npc->client->ps.origin );
		trap->LinkEntity( (sharedEntity_t *)npc );
		if ( !NPC_SpotWouldTelefrag( npc ) ) {
			VectorCopy( at, out->lastAt );
			return;
		}
	}
	G_SetOrigin( npc, was );	// (nowhere free: it waits where it was)
	VectorCopy( was, npc->client->ps.origin );
	trap->LinkEntity( (sharedEntity_t *)npc );
}

// One put away hurt a moment ago, out again: its health as it was, once it's
// begun (NPC_Begin, a moment after it's spawned, gives it its most).
static void G_RPC_StartHealth( gentity_t *npc, rpcOut_t *out ) {
	const int most = npc->client->ps.stats[STAT_MAX_HEALTH];

	if ( out->startHealth > 0 && npc->health > 0 && most > 0 ) {
		npc->health = npc->client->ps.stats[STAT_HEALTH] = Q_max( 1, most * out->startHealth / 1000 );
		out->startHealth = 0;
	}
}

static void G_RPC_RemoveAll( int clientNum ) {
	int k;

	for ( k = 0; k < RP_COMP_OUT_MAX; k++ ) {
		if ( rpco[clientNum].out[k].ent >= 0 ) {
			G_RPC_Remove( clientNum, k );
		}
	}
	G_RPC_Tell( clientNum );
}

// Knocked-out companions whose wait is over: free to come again.
static void G_RPC_Recall( int clientNum ) {
	int s;

	for ( s = 0; s < RP_COMP_MAX; s++ ) {
		if ( rpco[clientNum].knocked[s] && level.time - rpco[clientNum].knocked[s] >= 0 ) {
			rpco[clientNum].knocked[s] = 0;
		}
	}
}

// A player's companions as they should be: those no longer wanted (or
// changed - what they fight as, look or colours: a name's only told) go,
// those wanted come out (a moment after the player spawned, where there's
// room).
static void G_RPC_Reconcile( int clientNum ) {
	rpcOwner_t *o = &rpco[clientNum];
	const qboolean wanted = G_RPC_Wanted( clientNum );
	const int gen = G_RPCompChar( clientNum );
	const int want = wanted ? gen : -1;
	int slots[RP_COMP_OUT_MAX], n = 0, k, i;

	if ( gen >= 0 && gen != o->knockGen ) {
		memset( o->failed, 0, sizeof( o->failed ) );	// (another character's companions: tried again; a knocked-out one waits, by its name)
		o->knockGen = gen;
	}
	if ( want != o->charIndex ) {
		G_RPC_RemoveAll( clientNum );
		o->charIndex = want;
	}
	if ( wanted ) {
		n = G_RPC_WantedSlots( clientNum, slots );
	}
	for ( k = 0; k < RP_COMP_OUT_MAX; k++ ) {
		rpcOut_t *out = &o->out[k];
		const rpComp_t *c;
		qboolean keep = qfalse;
		if ( out->ent < 0 ) {
			continue;
		}
		if ( !G_RPC_Npc( clientNum, out ) ) {
			G_RPC_HelpTell( clientNum, out, 0 );
			out->ent = -1;	// (gone from under us: the level's)
			out->knockedAt = 0;
			continue;
		}
		if ( out->knockedAt ) {
			if ( level.time - out->knockedAt >= RPC_BODY || !wanted ) {
				G_RPC_Remove( clientNum, k );
			}
			continue;
		}
		G_RPC_BeginClear( clientNum, k );
		G_RPC_StartHealth( G_RPC_Npc( clientNum, out ), out );
		c = G_RPComp( clientNum, out->slot );
		for ( i = 0; i < n; i++ ) {
			keep = (qboolean)( keep || slots[i] == out->slot );
		}
		if ( !keep || !c ) {
			G_RPC_KeepHealth( clientNum, k );
			G_RPC_Remove( clientNum, k );
		} else if ( Q_stricmp( c->type, out->type ) || strcmp( c->look, out->look ) || memcmp( c->rgb, out->rgb, sizeof( out->rgb ) ) ) {
			const gentity_t *npc = G_RPC_Npc( clientNum, out );
			if ( npc && ( npc->enemy || ( out->hurtAt && level.time - out->hurtAt < RPC_HURT_TIME ) ) ) {
				continue;	// (changed while it's fighting: once that's over - no fresh one mid-fight)
			}
			G_RPC_KeepHealth( clientNum, k );
			G_RPC_Remove( clientNum, k );
		}
	}
	for ( i = 0; i < n && !level.intermissiontime && !level.intermissionQueued && level.time - o->comeAfter >= 0 && level.time - o->tryAfter >= 0; i++ ) {
		int free = -1;
		qboolean there = qfalse;
		for ( k = 0; k < RP_COMP_OUT_MAX; k++ ) {
			if ( o->out[k].ent >= 0 && o->out[k].slot == slots[i] ) {
				there = qtrue;
			} else if ( o->out[k].ent < 0 && free < 0 ) {
				free = k;
			}
		}
		if ( there || free < 0 ) {
			continue;
		}
		if ( G_RPC_Count() >= g_rpCompanionsMax.integer ) {
			if ( !o->fullTold || level.time - o->fullTold >= RPC_FULL_TELL ) {
				o->fullTold = level.time ? level.time : 1;
				G_RPC_Say( clientNum, "The server has as many companions out as it allows: yours wait for room" );
			}
			o->tryAfter = level.time + RPC_RETRY;
			break;
		}
		if ( !G_RPC_Room() ) {
			if ( !o->roomTold || level.time - o->roomTold >= RPC_FULL_TELL ) {
				o->roomTold = level.time ? level.time : 1;
				G_RPC_Say( clientNum, "This level has no room for more companions just now: yours wait" );
			}
			o->tryAfter = level.time + RPC_ROOM_WAIT;
			break;
		}
		if ( !G_RPC_Spawn( clientNum, free, slots[i], n ) ) {
			o->tryAfter = level.time + RPC_RETRY;
			break;
		}
	}
	G_RPC_Tell( clientNum );
}

/*
================================================================================
Who they fight
================================================================================
*/

// Whether two players are on one side as the game type has it: a team
// game's teammates, friendly fire off (they and theirs don't fight).
static qboolean G_RPC_Teammates( int a, int b ) {
	return (qboolean)( a >= 0 && b >= 0 && a != b && level.gametype >= GT_TEAM && !g_friendlyFire.integer &&
		g_entities[a].client && g_entities[b].client && g_entities[a].client->sess.sessionTeam == g_entities[b].client->sess.sessionTeam );
}

// G_Damage: someone hurt targ (a player, or a companion) - fair game for
// that player's companions a while.
void G_RPCompHurt( gentity_t *targ, gentity_t *attacker ) {
	const int boss = G_RPC_Boss( targ );
	const gclient_t *bc = ( boss >= 0 ) ? g_entities[boss].client : NULL;
	rpcOwner_t *o;
	int i, pick = 0;

	if ( boss < 0 || !bc || !attacker || !attacker->client || attacker == targ || G_RPC_Boss( attacker ) == boss ||
		G_RPC_Teammates( boss, G_RPC_Boss( attacker ) ) ) {
		return;
	}
	if ( bc->ps.duelInProgress && bc->ps.duelIndex == attacker->s.number ) {
		return;	// (a duel's blows are the duellists' own: no grudge after)
	}
	o = &rpco[boss];
	if ( targ->s.number >= MAX_CLIENTS && G_RPC_Which( targ ) >= 0 ) {
		o->out[G_RPC_Which( targ )].hurtAt = level.time ? level.time : 1;
	}
	for ( i = 0; i < (int)ARRAY_LEN( o->hurtBy ); i++ ) {
		if ( o->hurtBy[i] == attacker->s.number + 1 ) {
			pick = i;
			break;
		}
		if ( o->hurtTime[i] - o->hurtTime[pick] < 0 ) {
			pick = i;	// (the longest ago)
		}
	}
	o->hurtBy[pick] = attacker->s.number + 1;
	o->hurtTime[pick] = level.time ? level.time : 1;
}

// Whether ent hurt a player or theirs lately.
static qboolean G_RPC_HurtBy( const rpcOwner_t *o, const gentity_t *ent ) {
	int i;

	for ( i = 0; i < (int)ARRAY_LEN( o->hurtBy ); i++ ) {
		if ( o->hurtBy[i] == ent->s.number + 1 && level.time - o->hurtTime[i] < RPC_HURT_TIME ) {
			return qtrue;
		}
	}
	return qfalse;
}

// Whether companion npc may have ent as its enemy: 1 yes, 0 no (-1: npc
// isn't a player's companion).
int G_RPCompEnemy( const gentity_t *npc, const gentity_t *ent ) {
	const int k = ( npc && npc->s.number >= MAX_CLIENTS ) ? G_RPC_Which( npc ) : -1;
	const int boss = ( k >= 0 ) ? G_RPC_OwnerNum( npc ) : -1;
	const gentity_t *owner;
	const rpcOut_t *out;

	if ( boss < 0 ) {
		return -1;
	}
	owner = &g_entities[boss];
	if ( !ent || !ent->inuse || !ent->client || ent == npc || G_RPC_Boss( ent ) == boss || G_RPDowned( ent ) || ent->health <= 0 ||
		ent->client->ps.duelInProgress || ( owner->client && owner->client->ps.duelInProgress ) ) {
		return 0;	// (their owner, the other of theirs, someone down; a duel's the duellists' alone)
	}
	if ( ent->client->sess.sessionTeam == TEAM_SPECTATOR || ( ent->flags & FL_NOTARGET ) || G_RPC_Teammates( boss, G_RPC_Boss( ent ) ) ) {
		return 0;
	}
	out = &rpco[boss].out[k];
	if ( out->order == RPCO_FOLLOW ) {
		return 0;
	}
	if ( out->order == RPCO_ATTACK && out->target == ent->s.number ) {
		return 1;
	}
	if ( G_RPC_HurtBy( &rpco[boss], ent ) ) {
		return 1;
	}
	// an NPC going after them (a player's enemy is who killed them last, not whom they're after)
	return ( ent->s.number >= MAX_CLIENTS && ent->NPC && ent->enemy && ent->enemy->inuse && G_RPC_Boss( ent->enemy ) == boss ) ? 1 : 0;
}

// G_Damage, ForcePowerUsableOn: whether attacker may not hurt targ (or
// turn the Force on them) - a player and their companions, or theirs one
// another; teammates' and theirs as teammates are (friendly fire off).
qboolean G_RPCompNoHarm( const gentity_t *targ, const gentity_t *attacker ) {
	const int a = G_RPC_Boss( attacker ), t = G_RPC_Boss( targ );

	if ( !targ || !attacker || targ == attacker || a < 0 || t < 0 || ( targ->s.number < MAX_CLIENTS && attacker->s.number < MAX_CLIENTS ) ) {
		return qfalse;	// (players and players: as the game has it)
	}
	return (qboolean)( a == t || G_RPC_Teammates( a, t ) );
}

// G_RPProvoke: whose a blow is - a companion's, its owner's (their side's grudges).
gentity_t *G_RPCompBlame( gentity_t *attacker ) {
	const int n = ( attacker && attacker->s.number >= MAX_CLIENTS ) ? G_RPC_Boss( attacker ) : -1;

	return ( n >= 0 ) ? &g_entities[n] : attacker;
}

/*
================================================================================
Knocked out
================================================================================
*/

// player_die: a companion's health ran out - knocked out: its body's gone
// RPC_BODY on, and it can be called again RPC_KNOCKED_WAIT after.
void G_RPCompDied( gentity_t *self ) {
	const int k = ( self && self->s.number >= MAX_CLIENTS ) ? G_RPC_Which( self ) : -1;
	const int n = ( k >= 0 ) ? G_RPC_OwnerNum( self ) : -1;
	const rpComp_t *c;
	rpcOut_t *out;

	if ( n < 0 ) {
		return;
	}
	out = &rpco[n].out[k];
	if ( out->knockedAt ) {
		return;
	}
	out->knockedAt = level.time ? level.time : 1;
	c = G_RPComp( n, out->slot );
	rpco[n].knocked[out->slot] = level.time + RPC_KNOCKED_WAIT;
	Q_strncpyz( rpco[n].knockedName[out->slot], c ? c->name : "", sizeof( rpco[n].knockedName[0] ) );
	rpco[n].savedAt[out->slot] = 0;
	G_RPC_HelpTell( n, out, 0 );
	out->helpFrom = 0;
	G_RPC_Say( n, va( "%s" S_COLOR_WHITE " is knocked out: you can call them again in %i minutes", c ? c->name : "Your companion",
		RPC_KNOCKED_WAIT / 60000 ) );
	G_LogPrintf( "Roleplay: %s" S_COLOR_WHITE "'s companion %s was knocked out\n", g_entities[n].client ? g_entities[n].client->pers.netname : "",
		c ? c->name : "" );
	G_RPC_Tell( n );
	G_RPSendComps( n );	// (the Companions page shows the wait)
}

// Seconds before a knocked-out companion can be called again (0: it can, or it isn't knocked out).
int G_RPCompWait( int clientNum, int slot ) {
	int until;

	if ( clientNum < 0 || clientNum >= MAX_CLIENTS || slot < 0 || slot >= RP_COMP_MAX || !G_RPC_Knocked( &rpco[clientNum], slot, G_RPComp( clientNum, slot ) ) ) {
		return 0;
	}
	until = rpco[clientNum].knocked[slot];
	return ( until - level.time > 0 ) ? ( until - level.time + 999 ) / 1000 : 0;
}

/*
================================================================================
Their think (NPC_RunBehavior): following, orders, getting their owner up
================================================================================
*/

// Out of the fight: the NPC's own fighting let go of.
static void G_RPC_Disengage( gentity_t *npc, rpcOut_t *out ) {
	if ( npc->enemy ) {
		G_ClearEnemy( npc );
		out->fightEnd = level.time ? level.time : 1;
	}
	npc->NPC->tempBehavior = BS_DEFAULT;
	npc->NPC->behaviorState = BS_DEFAULT;
	npc->NPC->goalEntity = NULL;
	out->spotEnemy = 0;	// (its spot for a shot: not for the next fight)
	out->chaseSince = 0;	// (nor the time it's been getting nowhere)
	if ( npc->NPC->combatPoint >= 0 ) {
		NPC_FreeCombatPoint( npc->NPC->combatPoint, qfalse );
		npc->NPC->combatPoint = -1;
	}
}

// Its way to goal round a body in it (its owner, the other of theirs, anyone
// standing there): past the body's side - the side it's on, else the other
// (neither: it pushes on, and getting nowhere a while, it's brought to them).
// Its last goal (its spot, where it holds) with someone standing on it, near
// enough that by them it's there: up to them.
static void G_RPC_Detour( const gentity_t *npc, vec3_t goal, qboolean last ) {
	trace_t tr;
	const gentity_t *b;
	vec3_t dir, side, off;
	int i;

	trap->Trace( &tr, npc->r.currentOrigin, rpcMins, rpcMaxs, goal, npc->s.number, CONTENTS_BODY, qfalse, 0, 0 );
	if ( tr.startsolid || tr.fraction >= 1.0f || tr.entityNum < 0 || tr.entityNum >= ENTITYNUM_MAX_NORMAL ) {
		return;
	}
	b = &g_entities[tr.entityNum];
	if ( last && sqrtf( ( goal[0] - b->r.currentOrigin[0] ) * ( goal[0] - b->r.currentOrigin[0] ) + ( goal[1] - b->r.currentOrigin[1] ) * ( goal[1] - b->r.currentOrigin[1] ) ) <=
		RPC_LEAVE - ( b->r.maxs[0] + rpcMaxs[0] ) ) {
		return;	// (they stand where it's going: up to them)
	}
	VectorSubtract( goal, npc->r.currentOrigin, dir );
	dir[2] = 0.0f;
	if ( VectorNormalize( dir ) < 1.0f ) {
		return;
	}
	VectorSet( side, -dir[1], dir[0], 0.0f );	// (the way's left)
	VectorSubtract( npc->r.currentOrigin, b->r.currentOrigin, off );
	for ( i = 0; i < 2; i++ ) {
		const float sign = ( ( DotProduct( off, side ) >= 0.0f ) == ( i == 0 ) ) ? 1.0f : -1.0f;
		vec3_t at;
		VectorMA( b->r.currentOrigin, sign * ( b->r.maxs[0] + rpcMaxs[0] + RPC_DETOUR ), side, at );
		at[2] = npc->r.currentOrigin[2];
		if ( G_RPC_Fits( at, npc->s.number ) && G_RPC_Walkable( npc->r.currentOrigin, at, npc->s.number ) ) {
			VectorCopy( at, goal );
			return;
		}
	}
}

// How much of its speed a body can go at, its way dir to where it stops far
// off (run: running): lava, slime or a drop ahead (no floor near under a
// body, the floor followed - stairs, a slope), no faster than it can stop
// short of it - as far as its slide stopping (v / friction) and its move till
// its next think, short of where it was seen; at its edge, 0. Looked at as
// far as it could go before it stops; a gap no wider than the looks apart
// (one look with no floor) it runs over, as a player does - but past where
// it stops, any gap's one. Something in its way there (a wall, a ledge up):
// no further looked at. In lava already, or in the air: on, as it's going
// (out of it).
static float G_RPC_Ahead( const gentity_t *npc, const vec3_t dir, float far, qboolean run ) {
	const float speed = sqrtf( npc->client->ps.velocity[0] * npc->client->ps.velocity[0] + npc->client->ps.velocity[1] * npc->client->ps.velocity[1] );
	const float top = Q_max( speed, run ? npc->NPC->stats.runSpeed * 1.3f : (float)npc->NPC->stats.walkSpeed );	// (as fast as it'll go this think: ClientThink_real's)
	const float per = 1.0f / pm_friction + FRAMETIME / 1000.0f, look = top * per + RPC_EDGE;
	vec3_t ahead;
	float d, z = npc->r.currentOrigin[2], gap = -1.0f;

	VectorCopy( npc->r.currentOrigin, ahead );
	ahead[2] += DEFAULT_MINS_2 + 1.0f;	// (its feet)
	if ( npc->client->ps.groundEntityNum == ENTITYNUM_NONE || ( trap->PointContents( ahead, npc->s.number ) & RPC_UNWALKABLE ) ) {
		return 1.0f;
	}
	for ( d = RPC_WALK_STEP; ; d += RPC_WALK_STEP ) {
		const float at = Q_min( d, look );
		float fz;
		int found;
		VectorMA( npc->r.currentOrigin, at, dir, ahead );
		ahead[2] = z;
		found = G_RPC_Floor( ahead, npc->s.number, &fz );
		if ( found < 0 ) {
			break;	// (in something there: no further that way)
		}
		if ( !found ) {
			if ( gap < 0.0f ) {
				gap = at;
			}
			if ( gap < at || at >= far ) {
				break;	// (a gap from gap on: two looks with no floor, or past where it stops)
			}
		} else {
			gap = -1.0f;
			z = fz;
		}
		if ( at >= look ) {
			break;
		}
	}
	return ( gap < 0.0f ) ? 1.0f : Q_max( 0.0f, ( gap - RPC_EDGE ) / per / Q_max( top, 1.0f ) );
}

// This think's move toward goal (run: running; last: its last goal, not a
// point on its way), turning to it as it goes (round a body in the way; not
// into lava, slime or off a drop). qfalse: stopped at the edge of one (on
// the way to goal, or past it), its way on that way gone.
static qboolean G_RPC_MoveTo( gentity_t *npc, const vec3_t to, qboolean run, qboolean last ) {
	vec3_t dir, goal;
	float far, scale;

	VectorCopy( to, goal );
	G_RPC_Detour( npc, goal, last );
	VectorSubtract( goal, npc->r.currentOrigin, dir );
	dir[2] = 0.0f;
	if ( ( far = VectorNormalize( dir ) ) < 1.0f ) {
		return qtrue;
	}
	NPCS.NPCInfo->desiredYaw = vectoyaw( dir );
	NPCS.NPCInfo->desiredPitch = 0.0f;
	scale = G_RPC_Ahead( npc, dir, ( last && VectorCompare( goal, to ) ) ? far : 65536.0f, run );	// (a point it runs on past, round a body: not where it stops)
	if ( scale < RPC_SLOW ) {
		return qfalse;	// (there: no further)
	}
	NPCS.NPCInfo->combatMove = qtrue;	// (where it's pointed, turning as it goes: no stopping to turn)
	G_UcmdMoveForDir( npc, &NPCS.ucmd, dir );
	if ( scale < 1.0f ) {
		NPCS.ucmd.forwardmove = (signed char)( NPCS.ucmd.forwardmove * scale );
		NPCS.ucmd.rightmove = (signed char)( NPCS.ucmd.rightmove * scale );
	}
	if ( !run ) {
		NPCS.ucmd.buttons |= BUTTON_WALKING;
	}
	return qtrue;
}

// Its speeds: following, quick enough to keep up with a player (walking, a
// player's walk; running, a little past their run); fighting, its own.
static void G_RPC_Speeds( gentity_t *npc, const rpcOut_t *out, qboolean following ) {
	npc->NPC->stats.walkSpeed = following ? Q_max( out->walkSpeed, 250 ) : out->walkSpeed;
	npc->NPC->stats.runSpeed = following ? Q_max( out->runSpeed, 220 ) : out->runSpeed;
}

// To somewhere just behind its owner (Come to me; stuck, too far, a trail
// it can't walk): qfalse - nowhere for it.
static qboolean G_RPC_Teleport( gentity_t *owner, rpcOwner_t *o, gentity_t *npc, rpcOut_t *out ) {
	vec3_t at;

	out->teleTime = level.time ? level.time : 1;	// (tried: not again for a while, room or not)
	if ( !G_RPC_NearOwner( owner, o, npc->s.number, at ) ) {
		return qfalse;
	}
	trap->UnlinkEntity( (sharedEntity_t *)npc );
	G_SetOrigin( npc, at );
	VectorCopy( at, npc->client->ps.origin );
	VectorClear( npc->client->ps.velocity );
	npc->client->ps.eFlags ^= EF_TELEPORT_BIT;
	trap->LinkEntity( (sharedEntity_t *)npc );
	npc->NPC->goalEntity = NULL;
	npc->NPC->consecutiveBlockedMoves = 0;
	out->trailAt = -1;
	out->noWayTime = 0;
	out->stuckSince = 0;
	out->arrived = qfalse;
	out->teleTime = level.time ? level.time : 1;
	VectorCopy( at, out->lastAt );
	out->lastAtTime = level.time;
	return qtrue;
}

// Whether the trail it's on is cut just ahead (a lift gone up with them, a
// jump: its last point before the cut, maybe, out of its reach).
static qboolean G_RPC_CutAhead( rpcOwner_t *o, const rpcOut_t *out ) {
	return (qboolean)( o->trailCount && out->trailAt >= G_RPC_Oldest( o ) && out->trailAt < G_RPC_Newest( o ) && G_RPC_At( o, out->trailAt + 1 )->cut );
}

// Whether it's been getting nowhere long enough to go to its owner that way
// (sooner, the trail cut just ahead).
static qboolean G_RPC_StuckLong( rpcOwner_t *o, const rpcOut_t *out ) {
	return (qboolean)( out->stuckSince && level.time - out->stuckSince >= ( G_RPC_CutAhead( o, out ) ? RPC_CUT_STUCK : RPC_STUCK ) );
}

// Getting nowhere: since when (moved less than RPC_STUCK_MOVE in a second, wanting to go).
static void G_RPC_StuckFrame( gentity_t *npc, rpcOut_t *out, qboolean going ) {
	if ( level.time - out->lastAtTime < 1000 ) {
		return;
	}
	if ( level.time - out->lastAtTime > 1500 ) {
		out->stuckSince = 0;	// (not following a while - holding, fighting: from here)
	} else if ( going && Distance( npc->r.currentOrigin, out->lastAt ) < RPC_STUCK_MOVE ) {
		if ( !out->stuckSince ) {
			out->stuckSince = out->lastAtTime ? out->lastAtTime : 1;
		}
	} else {
		out->stuckSince = 0;
	}
	VectorCopy( npc->r.currentOrigin, out->lastAt );
	out->lastAtTime = level.time;
}

// Where it heads, following: its spot, straight; else along the trail - the
// newest point it can walk straight to, and on along it from there (down
// past a cut, where it can walk straight down). qfalse: no way (a cut in the
// trail ahead of it it can't follow, no point it can get to, or the floor
// gone from the point it's going to - a train, a lift they were on moved
// off).
static qboolean G_RPC_Goal( rpcOwner_t *o, gentity_t *npc, rpcOut_t *out, const vec3_t spot, vec3_t goal ) {
	const int oldest = G_RPC_Oldest( o ), newest = G_RPC_Newest( o );
	int n, step, tries;

	if ( G_RPC_Walkable( npc->r.currentOrigin, spot, npc->s.number ) ) {
		out->trailAt = -1;
		VectorCopy( spot, goal );
		return qtrue;
	}
	if ( !o->trailCount ) {
		return qfalse;
	}
	if ( out->trailAt >= 0 && ( out->trailAt < oldest || out->trailAt > newest ) ) {
		out->trailAt = -1;	// (fallen off its end)
	}
	if ( out->trailAt < 0 && out->noWayTime && level.time - out->noWayTime < RPC_PATH ) {
		return qfalse;	// (no way a moment ago: not looked for again yet - a look's many traces)
	}
	if ( out->trailAt < 0 || level.time - out->pathTime >= RPC_PATH ) {
		// a way on along it, straight: the newest point it can walk to (a
		// few tried, from the newest back)
		out->pathTime = level.time;
		for ( n = newest, step = 1, tries = 0; n >= oldest && n > out->trailAt && tries < 6; n -= step, step *= 2, tries++ ) {
			if ( G_RPC_Walkable( npc->r.currentOrigin, G_RPC_At( o, n )->at, npc->s.number ) ) {
				out->trailAt = n;
				break;
			}
		}
	}
	if ( out->trailAt < 0 ) {
		// not on it yet: the nearest point near it it can walk to - the
		// nearest few looked at, nearest first (not the newest few: those
		// can all be beyond a wall from it, round a bend it isn't round yet),
		// none by a nearer one (its stretch beyond a wall, the next tried)
		int near[RPC_NEAR_TRIES], numNear = 0, i;
		while ( numNear < RPC_NEAR_TRIES ) {
			int best = -1;
			float bestDist = RPC_NEAR;
			for ( n = newest; n >= oldest; n-- ) {
				const float d = Distance( npc->r.currentOrigin, G_RPC_At( o, n )->at );
				if ( d > bestDist ) {
					continue;
				}
				for ( i = 0; i < numNear && ( near[i] - n > RPC_NEAR_SPREAD || n - near[i] > RPC_NEAR_SPREAD ); i++ ) {
				}
				if ( i == numNear ) {
					best = n;
					bestDist = d;
				}
			}
			if ( best < 0 ) {
				break;
			}
			near[numNear++] = best;
		}
		for ( i = 0; i < numNear; i++ ) {
			if ( G_RPC_Walkable( npc->r.currentOrigin, G_RPC_At( o, near[i] )->at, npc->s.number ) ) {
				out->trailAt = near[i];
				break;
			}
		}
		if ( out->trailAt < 0 ) {
			out->noWayTime = level.time ? level.time : 1;
			return qfalse;
		}
	}
	out->noWayTime = 0;
	if ( out->trailAt < newest ) {
		// there, or past it toward the next (run by it, or a body there
		// before it) with the way to the next clear: on to the next
		const float *at = G_RPC_At( o, out->trailAt )->at, *next = G_RPC_At( o, out->trailAt + 1 )->at;
		const float past = ( npc->r.currentOrigin[0] - at[0] ) * ( next[0] - at[0] ) + ( npc->r.currentOrigin[1] - at[1] ) * ( next[1] - at[1] );
		if ( Distance( npc->r.currentOrigin, at ) < RPC_PASS || ( past > 0.0f && G_RPC_Walkable( npc->r.currentOrigin, next, npc->s.number ) ) ) {
			if ( !G_RPC_At( o, out->trailAt + 1 )->cut ) {
				out->trailAt++;
			} else if ( next[2] < at[2] - STEPSIZE && G_RPC_Walkable( npc->r.currentOrigin, next, npc->s.number ) ) {
				out->trailAt++;	// (a way down they came flying - stairs they ran down - it can walk straight: down after them)
			} else {
				return qfalse;	// (a lift, a jump, a drop next it can't walk down: it can't follow there)
			}
		}
	}
	if ( G_RPC_Gone( G_RPC_At( o, out->trailAt )->at, npc->s.number ) ) {
		// (as far as it goes: stopped short of the edge, on the ground)
		if ( npc->client->ps.groundEntityNum != ENTITYNUM_NONE ) {
			npc->client->ps.velocity[0] = npc->client->ps.velocity[1] = 0.0f;
		}
		return qfalse;
	}
	VectorCopy( G_RPC_At( o, out->trailAt )->at, goal );
	return qtrue;
}

// Whether a companion can take a pose now: still, on its feet, on the ground, not busy.
static qboolean G_RPC_CanPose( const gentity_t *npc ) {
	const playerState_t *ps = &npc->client->ps;

	return (qboolean)( ps->pm_type == PM_NORMAL && ps->groundEntityNum != ENTITYNUM_NONE && !npc->enemy &&
		ps->velocity[0] * ps->velocity[0] + ps->velocity[1] * ps->velocity[1] < 100.0f && npc->NPC->currentSpeed <= 0 &&
		!BG_InKnockDown( ps->legsAnim ) && ps->legsTimer <= 0 && ps->weaponTime <= 0 && ps->saberLockTime < level.time &&
		ps->forceHandExtend == HANDEXTEND_NONE && !( ps->pm_flags & PMF_DUCKED ) );
}

// Its owner's pose (there: at its spot; sitting, it sits on the floor -
// there's no seat behind it), or up out of one.
static void G_RPC_Pose( gentity_t *owner, gentity_t *npc, qboolean there ) {
	const int want = BG_VRPose( &owner->client->ps ), have = BG_VRPose( &npc->client->ps );
	const int pose = ( want == VRPOSE_SIT ) ? VRPOSE_CROSS : want;
	animation_t *anims = bgAllAnims[npc->localAnimIndex].anims;

	if ( there && want > VRPOSE_NONE && want < VRPOSE_CHOSEN ) {
		if ( have != pose && ( have || G_RPC_CanPose( npc ) ) && BG_VRPoseStart( &npc->client->ps, anims, pose ) ) {
			if ( npc->client->ps.weapon == WP_SABER && npc->client->ps.saberHolstered < 2 ) {
				WP_DeactivateSaber( npc, qfalse );	// (as a player's are, posing)
			}
		}
	} else if ( have > VRPOSE_NONE && have < VRPOSE_CHOSEN ) {
		BG_VRPoseStandUp( &npc->client->ps, anims );
	}
}

// Whether it's getting into, in, or up out of a pose (moving waits).
static qboolean G_RPC_Posed( const gentity_t *npc ) {
	return (qboolean)( BG_VRPosePhase( &npc->client->ps, bgAllAnims[npc->localAnimIndex].anims, NULL, NULL ) != VRPHASE_NONE );
}

// Which of a player's gets them up, down: one at it already, else one not
// fighting, else the nearer (-1: none of theirs is up).
static int G_RPC_Helper( int clientNum ) {
	const rpcOwner_t *o = &rpco[clientNum];
	const gentity_t *owner = &g_entities[clientNum];
	float best = 0.0f;
	int k, pick = -1;

	for ( k = 0; k < RP_COMP_OUT_MAX; k++ ) {
		const rpcOut_t *out = &o->out[k];
		const gentity_t *npc = out->knockedAt ? NULL : G_RPC_Npc( clientNum, out );
		float score;
		if ( !npc || npc->health <= 0 ) {
			continue;
		}
		if ( out->helpFrom ) {
			return k;
		}
		score = Distance( npc->r.currentOrigin, owner->client->ps.origin ) + ( npc->enemy ? 100000.0f : 0.0f );
		if ( pick < 0 || score < best ) {
			pick = k;
			best = score;
		}
	}
	return pick;
}

// Getting its owner up (they're down): to them - as it follows them, straight
// or along their trail (round anyone in its way); stuck, far off, or no way,
// to just by them - then, by them (on their level, in sight), RPC_HELP_TIME
// there.
static void G_RPC_Help( gentity_t *owner, rpcOwner_t *o, gentity_t *npc, rpcOut_t *out ) {
	vec3_t d, eye, goal;
	float dz, dist;

	VectorSubtract( owner->client->ps.origin, npc->r.currentOrigin, d );
	dz = d[2];
	d[2] = 0.0f;
	dist = VectorLength( d );
	VectorCopy( npc->r.currentOrigin, eye );
	eye[2] += npc->client->ps.viewheight;
	if ( dist > RPC_HELP_REACH || fabsf( dz ) > RPC_HELP_HEIGHT || !G_RPC_Clear( eye, owner->client->ps.origin, npc->s.number ) ) {
		out->helpFrom = 0;
		G_RPC_HelpTell( owner->s.number, out, 0 );
		G_RPC_StuckFrame( npc, out, qtrue );
		if ( level.time - out->teleTime >= RPC_TELE_WAIT && ( dist > RPC_FAR || G_RPC_StuckLong( o, out ) ) ) {
			G_RPC_Teleport( owner, o, npc, out );
			return;
		}
		if ( G_RPC_Goal( o, npc, out, owner->client->ps.origin, goal ) ) {
			if ( !G_RPC_MoveTo( npc, goal, qtrue, (qboolean)( out->trailAt < 0 ) ) && out->trailAt >= 0 && !G_RPC_CutAhead( o, out ) ) {
				out->trailAt = -1;	// (stopped at an edge on its way to that point of the trail: another looked for - not the last before a cut)
			}
		} else if ( level.time - out->teleTime >= RPC_TELE_WAIT ) {
			G_RPC_Teleport( owner, o, npc, out );
		}
		return;
	}
	out->trailAt = -1;
	out->stuckSince = 0;
	NPCS.NPCInfo->desiredYaw = vectoyaw( d );
	NPCS.NPCInfo->desiredPitch = 0.0f;
	if ( !out->helpFrom ) {
		out->helpFrom = level.time ? level.time : 1;
	} else if ( level.time - out->helpFrom >= RPC_HELP_TIME ) {
		const rpComp_t *c = G_RPComp( owner->s.number, out->slot );
		out->helpFrom = 0;
		G_RPCompGetUp( owner, c ? c->name : "Their companion" );	// (g_vrdown.c)
		G_RPC_HelpTell( owner->s.number, out, -1 );
		return;
	}
	G_RPC_HelpTell( owner->s.number, out, out->helpFrom );
}

// Whether it lets someone be a while, having got nowhere going after them
// (G_RPC_Weapon): till RPC_FIGHT_IGNORE's up, or they've come at it -
// RPC_FIGHT_SHIFT nearer where it holds or its owner (anchor) than they were,
// or, on their feet, up or down more than a step from there (off a ledge, a
// crate) - not for pacing about where they are.
static qboolean G_RPC_Ignored( const rpcOut_t *out, const gentity_t *e, const vec3_t anchor ) {
	return (qboolean)( out->ignoreEnemy == e->s.number + 1 && level.time < out->ignoreUntil &&
		Distance( e->r.currentOrigin, anchor ) > Distance( out->ignoreAt, anchor ) - RPC_FIGHT_SHIFT &&
		!( e->client && e->client->ps.groundEntityNum != ENTITYNUM_NONE && fabsf( e->r.currentOrigin[2] - out->ignoreAt[2] ) > STEPSIZE ) );
}

// Looks about for whom to fight: whoever's after them, or hurt them
// lately, near enough and in sight (every RPC_SCAN) - not one it lets be.
static gentity_t *G_RPC_Scan( gentity_t *npc, rpcOut_t *out, const vec3_t anchor, float leash ) {
	int i;

	if ( level.time - out->scanTime < RPC_SCAN ) {
		return NULL;
	}
	out->scanTime = level.time;
	for ( i = 0; i < level.num_entities; i++ ) {
		gentity_t *e = &g_entities[i];
		if ( !e->inuse || !e->client || e == npc || Distance( e->r.currentOrigin, anchor ) > leash || G_RPC_Ignored( out, e, anchor ) ||
			G_RPCompEnemy( npc, e ) != 1 || !G_RPC_Sees( npc->r.currentOrigin, e->r.currentOrigin, npc->s.number ) ) {
			continue;
		}
		return e;
	}
	return NULL;
}

// Whether a weapon's a gun: fired at what it's aimed at (not a saber, fists
// or a baton, a thermal thrown over what's in the way, nor a mine or charge
// laid; nor a turret's or an emplaced gun's).
static qboolean G_RPC_Gun( int weapon ) {
	switch ( weapon ) {
	case WP_BRYAR_PISTOL:
	case WP_BLASTER:
	case WP_DISRUPTOR:
	case WP_BOWCASTER:
	case WP_REPEATER:
	case WP_DEMP2:
	case WP_FLECHETTE:
	case WP_ROCKET_LAUNCHER:
	case WP_CONCUSSION:
	case WP_BRYAR_OLD:
		return qtrue;
	default:
		return qfalse;
	}
}

// Whether a weapon's fists or a baton: a blow at whom's in reach.
static qboolean G_RPC_Fist( int weapon ) {
	return (qboolean)( weapon == WP_MELEE || weapon == WP_STUN_BATON );
}

// Whether a companion throws thermals - its type's weapon (fists too, given
// it by G_RPC_Weapon for anyone near).
static qboolean G_RPC_Thrower( const gentity_t *npc ) {
	return (qboolean)( ( npc->client->ps.stats[STAT_WEAPONS] & ( 1 << WP_THERMAL ) ) != 0 );
}

// Whether a companion fights at range - a gun, or thermals: holding, it takes
// on what's as far off as following (G_RPCompThink).
static qboolean G_RPC_Ranged( const gentity_t *npc ) {
	return (qboolean)( G_RPC_Gun( npc->client->ps.weapon ) || G_RPC_Thrower( npc ) );
}

static const vec3_t rpcLobMins = { -3.0f, -3.0f, -3.0f }, rpcLobMaxs = { 3.0f, 3.0f, 3.0f };	// (a thermal's: WP_FireThermalDetonator)

// Where a thermal's thrown at someone: their feet.
static void G_RPC_Feet( const gentity_t *target, vec3_t at ) {
	VectorSet( at, target->r.currentOrigin[0], target->r.currentOrigin[1], target->r.currentOrigin[2] + target->r.mins[2] + 4.0f );
}

// A thermal thrown from start to land at (on targetNum, if it's on the way):
// vel - along the lowest of a few arcs that gets there no faster than
// RPC_THROW_SPEED with nothing else in its way, bursting (an alt-fired one
// bursts on whatever it hits) no nearer the thrower than RPC_THROW_NEAR
// (qfalse: none does - vel as it was).
static qboolean G_RPC_Lob( const gentity_t *npc, const vec3_t start, const vec3_t at, int targetNum, vec3_t vel ) {
	static const float angles[] = { 30.0f, 45.0f, 60.0f };
	vec3_t flat, from, to, v;
	float d, h;
	int a, i;

	VectorSubtract( at, start, flat );
	h = flat[2];
	flat[2] = 0.0f;
	d = VectorNormalize( flat );
	if ( d < 1.0f ) {
		return qfalse;
	}
	for ( a = 0; a < (int)ARRAY_LEN( angles ); a++ ) {
		const float c = cosf( DEG2RAD( angles[a] ) ), s = sinf( DEG2RAD( angles[a] ) ), rise = d * s / c - h;
		float speed, flight;
		trace_t tr;

		if ( rise <= 0.0f ) {
			continue;	// (too high for this arc)
		}
		speed = sqrtf( DEFAULT_GRAVITY * d * d / ( 2.0f * c * c * rise ) );
		if ( !( speed <= RPC_THROW_SPEED ) ) {
			continue;	// (too far for this arc)
		}
		VectorScale( flat, speed * c, v );
		v[2] = speed * s;
		flight = d / ( speed * c );
		// its arc, a step at a time - two past their feet: down to the floor there
		VectorCopy( start, from );
		for ( i = 1; i <= RPC_LOB_STEPS + 2; i++ ) {
			const float t = flight * i / RPC_LOB_STEPS;
			VectorMA( start, t, v, to );
			to[2] -= 0.5f * DEFAULT_GRAVITY * t * t;
			trap->Trace( &tr, from, rpcLobMins, rpcLobMaxs, to, npc->s.number, MASK_SHOT, qfalse, 0, 0 );
			if ( tr.startsolid || tr.fraction < 1.0f ) {
				break;
			}
			VectorCopy( to, from );
		}
		if ( i <= RPC_LOB_STEPS + 2 && !tr.startsolid && ( tr.entityNum == targetNum ||
			( DistanceHorizontal( tr.endpos, at ) <= RPC_LOB_LANDS && fabsf( tr.endpos[2] - at[2] ) <= RPC_LOB_LANDS ) ) &&
			Distance( tr.endpos, npc->r.currentOrigin ) >= RPC_THROW_NEAR ) {
			VectorCopy( v, vel );
			return qtrue;	// (there: on them, or the floor at their feet - its blast not reaching the thrower)
		}
	}
	return qfalse;
}

// WP_FireThermalDetonator: a companion's thermal, lobbed at its enemy
// (G_RPC_Lob) - an NPC's throw is never held as long as it'd need to be; its
// enemy gone as it lets go (the throw goes 50 ms after the fight's last
// look: NPC_Think), where it was aiming. qfalse: no throw that's safe now
// (something come into its way since, say) - the thermal's not thrown.
qboolean G_RPCompLob( const gentity_t *ent, const vec3_t start, vec3_t vel ) {
	const int k = ( ent && ent->rpCompOwner ) ? G_RPC_Which( ent ) : -1;
	const int n = ( k >= 0 ) ? G_RPC_OwnerNum( ent ) : -1;
	rpcOut_t *out;

	if ( n < 0 ) {
		return qtrue;	// (not a companion's: as the game throws it)
	}
	out = &rpco[n].out[k];
	if ( ent->enemy && ent->enemy->inuse && ent->enemy->client ) {
		G_RPC_Feet( ent->enemy, out->lobAt );
		out->lobTime = level.time ? level.time : 1;
		return G_RPC_Lob( ent, start, out->lobAt, ent->enemy->s.number, vel );
	}
	if ( out->lobTime && level.time - out->lobTime <= RPC_LOB_KEEP ) {
		return G_RPC_Lob( ent, start, out->lobAt, ENTITYNUM_NONE, vel );
	}
	return qfalse;
}

// A thrower's weapon, to its fists or back (an NPC's never weighed against
// what it has: PM_Weapon fires what it holds) - alt-firing as that weapon
// does at once (its first throw back not a bouncing one).
static void G_RPC_Arm( gentity_t *npc, int weapon ) {
	ChangeWeapon( npc, weapon );
	if ( weapon == WP_MELEE ) {
		npc->NPC->burstSpacing = 0;	// (blow on blow, as an NPC of fists has it: ChangeWeapon would leave the thermal's)
	}
	G_RPC_AltFire( npc );
}

// Whether a gun companion standing at at would have a clear shot at its
// enemy, and sight of them, as the NPCs' fighting would find (NPC_ShotEntity,
// NPC_ClearLOS4): its gun held facing them as CalcMuzzlePoint has it, the
// shot a little round (a blaster's), nothing in it at the start (a wall
// there, a body); a rocket's no nearer them than RPC_ROCKET_NEAR.
static qboolean G_RPC_ShotFrom( gentity_t *npc, gentity_t *enemy, const vec3_t at ) {
	static const vec3_t shotMins = { -2.0f, -2.0f, -2.0f }, shotMaxs = { 2.0f, 2.0f, 2.0f };
	const float *off = WP_MuzzlePoint[npc->client->ps.weapon];
	vec3_t toward, muzzle, chest, eyes;
	trace_t tr;

	if ( npc->client->ps.weapon == WP_ROCKET_LAUNCHER && Distance( at, enemy->r.currentOrigin ) < RPC_ROCKET_NEAR ) {
		return qfalse;
	}
	VectorSubtract( enemy->r.currentOrigin, at, toward );
	toward[2] = 0.0f;
	VectorNormalize( toward );
	VectorMA( at, off[0], toward, muzzle );
	muzzle[0] += off[1] * toward[1];	// (its right: forward turned a quarter clockwise)
	muzzle[1] -= off[1] * toward[0];
	muzzle[2] += npc->client->ps.viewheight + off[2];
	CalcEntitySpot( enemy, SPOT_CHEST, chest );
	trap->Trace( &tr, muzzle, shotMins, shotMaxs, chest, npc->s.number, MASK_SHOT, qfalse, 0, 0 );
	if ( tr.startsolid || tr.entityNum != enemy->s.number ) {
		return qfalse;
	}
	VectorCopy( at, eyes );
	eyes[2] += npc->client->ps.viewheight;
	return G_ClearLOS3( npc, eyes, enemy );
}

// A spot near a gun companion with a clear shot at its enemy from there
// (G_RPC_ShotFrom: round its owner, the other of theirs, a pillar, a
// doorway's side): a step to either side of its line to them - its left
// first - two, three, then a step and a half back to either side, straight
// back, and twice that; on the floor there, nobody standing there, one it
// can walk straight to (G_RPC_Walkable: no wall, gap or drop on the way - an
// NPC goes round a wall only by the map's waypoints, which most maps
// haven't), no nearer its leash's end than RPC_GUN_MARGIN - a rocket's, a
// little further from its enemy than RPC_ROCKET_NEAR (qfalse: none).
static qboolean G_RPC_ShotSpot( gentity_t *npc, gentity_t *enemy, const vec3_t anchor, float leash, vec3_t spot ) {
	static const float steps[][2] = { { 1, 0 }, { -1, 0 }, { 2, 0 }, { -2, 0 }, { 3, 0 }, { -3, 0 }, { 1, 1.5f }, { -1, 1.5f }, { 0, 1.5f }, { 0, 3 } };	// (aside, back)
	vec3_t toward, side;
	int i;

	VectorSubtract( enemy->r.currentOrigin, npc->r.currentOrigin, toward );
	toward[2] = 0.0f;
	if ( VectorNormalize( toward ) < 1.0f ) {
		return qfalse;	// (straight above or below it)
	}
	VectorSet( side, -toward[1], toward[0], 0.0f );
	for ( i = 0; i < (int)ARRAY_LEN( steps ); i++ ) {
		float z;

		VectorMA( npc->r.currentOrigin, steps[i][0] * RPC_SPOT_STEP, side, spot );
		VectorMA( spot, -steps[i][1] * RPC_SPOT_STEP, toward, spot );
		if ( Distance( spot, anchor ) > leash - RPC_GUN_MARGIN || G_RPC_Floor( spot, npc->s.number, &z ) <= 0 ) {
			continue;
		}
		spot[2] = z;
		if ( npc->client->ps.weapon == WP_ROCKET_LAUNCHER && Distance( spot, enemy->r.currentOrigin ) < RPC_ROCKET_NEAR + RPC_SPOT_THERE + 16.0f ) {
			continue;	// (there, but for a step, too near to fire at them: RPC_SPOT_THERE)
		}
		if ( !G_RPC_Taken( spot, npc->s.number ) && G_RPC_ShotFrom( npc, enemy, spot ) && G_RPC_Walkable( npc->r.currentOrigin, spot, npc->s.number ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

// Whether a companion's fists or baton would land a blow on its enemy, swung
// now facing them (as the NPCs' fighting has it, at them): WP_FireMelee's,
// WP_FireStunBaton's - from its eyes' height less 6, 20 on and 4 to its
// right, RPC_BLOW further, a box 6 round - on them, nothing else first (one
// crouching or below it too; not through a door, glass, a body).
static qboolean G_RPC_Blow( const gentity_t *npc, gentity_t *enemy ) {
	static const vec3_t blowMins = { -6.0f, -6.0f, -6.0f }, blowMaxs = { 6.0f, 6.0f, 6.0f };
	vec3_t from, chest, dir, start, end;
	trace_t tr;

	VectorCopy( npc->client->ps.origin, from );
	from[2] += npc->client->ps.viewheight - 6.0f;
	CalcEntitySpot( enemy, SPOT_CHEST, chest );
	VectorSubtract( chest, from, dir );
	VectorNormalize( dir );
	VectorMA( from, 20.0f, dir, start );
	if ( dir[0] || dir[1] ) {
		const float flat = sqrtf( dir[0] * dir[0] + dir[1] * dir[1] );
		start[0] += 4.0f * dir[1] / flat;	// (its right: forward turned a quarter clockwise)
		start[1] -= 4.0f * dir[0] / flat;
	}
	VectorMA( start, RPC_BLOW, dir, end );
	trap->Trace( &tr, start, blowMins, blowMaxs, end, npc->s.number, MASK_SHOT, qfalse, 0, 0 );
	return (qboolean)( tr.entityNum == enemy->s.number );
}

// Fighting by its weapon, with the NPCs' own fighting (NPC_BSST_Attack - a
// trooper's, every companion's but a saber's: G_RPCompThink). A trooper moves
// only where its squad's commander sends it (ST_Commander), and a companion
// is in no squad (G_RPC_Flags): it would stand where the fight found it,
// often its spot just behind its owner - never firing past them (the NPCs'
// fighting won't shoot through one of its side), nor past the other of
// theirs, nor round a wall; fists swung at the air. So, no nearer the end of
// its leash than RPC_GUN_MARGIN:
//   a gun with no clear shot goes where it has one, near it (G_RPC_ShotSpot,
// every RPC_SPOT_LOOK), else after its enemy as a commander sends a lone
// trooper that has none (scouting: ST_HuntEnemy); a rocket, never fired
// nor going nearer than RPC_ROCKET_NEAR; with one, it stands there and
// shoots;
//   thermals go after their enemy, the arc to them clear (G_RPC_Lob), and
// none thrown till it is - nor at anyone within RPC_THROW_NEAR: fists for
// them, till they're further than RPC_THROW_BACK;
//   fists or a baton go after their enemy till a blow would land
// (G_RPC_Blow), swinging only then;
// all on its own feet (G_RPC_MoveTo: round bodies, never into lava nor off
// an edge - the NPCs' goals go anywhere, and stop 31 short: NAV_HitNavGoal),
// no goal for the NPCs' fighting to move it by - it faces its enemy, firing
// on the way if a shot comes clear. Getting nowhere at them RPC_FIGHT_STUCK
// on its feet - a wall, a ledge, a counter, lava or a gap in its way (no way
// round but by the map's waypoints), its leash's end, a rocket too near
// them - it lets them be (G_RPC_Ignored) - qfalse: out of the fight, back to
// its owner. Not the Jedi's (their own way).
static qboolean G_RPC_Weapon( gentity_t *npc, rpcOut_t *out, gentity_t *enemy, const vec3_t anchor, float leash ) {
	gNPC_t *info = npc->NPC;
	const float dist = Distance( npc->r.currentOrigin, enemy->r.currentOrigin );
	const qboolean nearRocket = (qboolean)( npc->client->ps.weapon == WP_ROCKET_LAUNCHER && dist < RPC_ROCKET_NEAR );
	const int chased = out->chaseSince;	// (getting nowhere since - kept only while it's after them still: below)
	qboolean ready;

	if ( npc->client->NPC_class == CLASS_JEDI || npc->client->NPC_class == CLASS_REBORN || npc->client->NPC_class == CLASS_BOBAFETT ||
		npc->client->ps.weapon == WP_SABER ) {
		return qtrue;	// (the Jedi's fighting, its own way: NPC_RunBehavior)
	}
	out->chaseSince = 0;
	if ( G_RPC_Thrower( npc ) ) {
		if ( npc->client->ps.weapon == WP_THERMAL && dist < RPC_THROW_NEAR ) {
			G_RPC_Arm( npc, WP_MELEE );
		} else if ( npc->client->ps.weapon == WP_MELEE && ( dist > RPC_THROW_BACK || ( dist >= RPC_THROW_NEAR && chased && level.time - chased >= RPC_FIGHT_STUCK / 2 ) ) ) {
			G_RPC_Arm( npc, WP_THERMAL );	// (further off again - or no nearer, its fists getting nowhere at them: a throw that's safe)
		}
	}
	if ( G_RPC_Fist( npc->client->ps.weapon ) ) {
		ready = (qboolean)( G_RPC_Blow( npc, enemy ) && NPC_ClearLOS4( enemy ) && NPC_ShotEntity( enemy, NULL ) == enemy->s.number );	// (and its fighting swings: it sees them, nothing in the way)
	} else if ( G_RPC_Gun( npc->client->ps.weapon ) || npc->client->ps.weapon == WP_THERMAL ) {
		ready = (qboolean)( NPC_ClearLOS4( enemy ) && NPC_ShotEntity( enemy, NULL ) == enemy->s.number && !nearRocket );
		if ( ready && npc->client->ps.weapon == WP_THERMAL ) {
			vec3_t muzzle, at, vel;
			CalcEntitySpot( npc, SPOT_WEAPON, muzzle );
			G_RPC_Feet( enemy, at );
			ready = G_RPC_Lob( npc, muzzle, at, enemy->s.number, vel );
			if ( ready ) {
				VectorCopy( at, out->lobAt );	// (where it's thrown, its enemy gone as it lets go: G_RPCompLob)
				out->lobTime = level.time ? level.time : 1;
			}
		}
	} else {
		return qtrue;	// (with nothing, nothing)
	}
	if ( !ready && ( !G_RPC_Gun( npc->client->ps.weapon ) || nearRocket ) ) {
		info->scriptFlags |= SCF_DONT_FIRE;	// (no blow at the air, no thermal into a wall, no rocket at its own feet)
	}
	if ( ready ) {
		info->goalEntity = NULL;	// (a clear shot or throw now, or a blow that lands: from here - ST_CheckMoveState stands it)
		return qtrue;
	}
	if ( G_RPC_Gun( npc->client->ps.weapon ) ) {
		// going to a spot with a clear shot from it still: on there (no goal for
		// the NPCs' fighting to move it by - it faces its enemy, firing on the
		// way if a shot comes clear)
		if ( out->spotEnemy == enemy->s.number + 1 && G_RPC_ShotFrom( npc, enemy, out->spotAt ) &&
			( out->spotThere || ( level.time - out->spotTime < RPC_SPOT_GO && !G_RPC_Taken( out->spotAt, npc->s.number ) ) ) ) {
			if ( DistanceHorizontal( npc->r.currentOrigin, out->spotAt ) > RPC_SPOT_THERE && G_RPC_MoveTo( npc, out->spotAt, qtrue, qtrue ) ) {
				out->spotThere = 0;
				info->goalEntity = NULL;
				return qtrue;
			}
			if ( !out->spotThere ) {
				out->spotThere = level.time ? level.time : 1;
			}
			if ( level.time - out->spotThere < RPC_SPOT_LOOK ) {
				info->goalEntity = NULL;	// (there: a moment to face them and fire)
				return qtrue;
			}
		}
		out->spotEnemy = 0;	// (there with no shot after all, its shot from there gone, someone there, not there in time, another enemy)
		out->spotThere = 0;
		if ( level.time - out->spotTime >= RPC_SPOT_LOOK ) {
			out->spotTime = level.time;
			if ( G_RPC_ShotSpot( npc, enemy, anchor, leash, out->spotAt ) ) {
				out->spotEnemy = enemy->s.number + 1;
				G_RPC_MoveTo( npc, out->spotAt, qtrue, qtrue );
				info->goalEntity = NULL;
				return qtrue;
			}
		}
	}
	// after them, on its own feet - round bodies, never into lava nor off an
	// edge (the NPCs' own going walks anywhere: NPC_MoveToGoal) - but no
	// nearer its leash's end than RPC_GUN_MARGIN (back in toward them, yes), a
	// rocket no nearer them: there it stands; getting nowhere RPC_FIGHT_STUCK
	// (knocked down or gripped meanwhile: from when it's up, or let go), it
	// lets them be
	info->goalEntity = NULL;
	if ( chased && out->chaseEnemy == enemy->s.number + 1 && Distance( npc->r.currentOrigin, out->chaseAt ) < RPC_FIGHT_MOVED &&
		!BG_InKnockDown( npc->client->ps.legsAnim ) && npc->client->ps.fd.forceGripBeingGripped <= level.time ) {
		if ( level.time - chased >= RPC_FIGHT_STUCK ) {
			out->ignoreEnemy = enemy->s.number + 1;
			out->ignoreUntil = level.time + RPC_FIGHT_IGNORE;
			VectorCopy( enemy->r.currentOrigin, out->ignoreAt );
			G_RPC_Disengage( npc, out );
			return qfalse;
		}
		out->chaseSince = chased;
	} else {
		out->chaseSince = level.time ? level.time : 1;
		out->chaseEnemy = enemy->s.number + 1;
		VectorCopy( npc->r.currentOrigin, out->chaseAt );
	}
	if ( !nearRocket && ( Distance( npc->r.currentOrigin, anchor ) <= leash - RPC_GUN_MARGIN ||
		( enemy->r.currentOrigin[0] - npc->r.currentOrigin[0] ) * ( anchor[0] - npc->r.currentOrigin[0] ) +
		( enemy->r.currentOrigin[1] - npc->r.currentOrigin[1] ) * ( anchor[1] - npc->r.currentOrigin[1] ) > 0.0f ) ) {
		G_RPC_MoveTo( npc, enemy->r.currentOrigin, qtrue, qtrue );
	}
	return qtrue;
}

// NPC_RunBehavior: a companion's think (qtrue: it's done; qfalse - it's
// fighting: the NPC's own fighting from here, as team and bState say).
qboolean G_RPCompThink( int *team, int *bState ) {
	gentity_t *npc = NPCS.NPC, *owner, *enemy;
	rpcOwner_t *o;
	rpcOut_t *out;
	vec3_t spot, goal, anchor;
	const int k = G_RPC_Which( npc );
	const int n = ( k >= 0 ) ? G_RPC_OwnerNum( npc ) : -1;
	float dist, leash, reach;
	qboolean run, helper;

	if ( n < 0 ) {
		// (no one's now - lost track of: gone next frame, never an NPC of the level)
		npc->think = G_FreeEntity;
		npc->nextthink = level.time + FRAMETIME;
		return qtrue;
	}
	owner = &g_entities[n];
	o = &rpco[n];
	out = &o->out[k];
	if ( out->knockedAt || !owner->client ) {
		return qtrue;
	}
	G_RPC_Sides( npc, n );
	helper = (qboolean)( owner->client->rpDowned && G_RPC_Helper( n ) == k );

	// the fight: its own, on the leash - itself no further from them (or
	// where it holds) than the leash, its enemy no further than reach (a gun
	// or thermals holding take on what's as far off as following)
	VectorCopy( ( out->order == RPCO_HOLD ) ? out->hold : owner->client->ps.origin, anchor );
	leash = ( out->order == RPCO_HOLD ) ? RPC_HOLD_LEASH : RPC_LEASH;
	reach = G_RPC_Ranged( npc ) ? RPC_LEASH : leash;
	if ( out->order == RPCO_ATTACK ) {
		gentity_t *t = ( out->target >= 0 && out->target < ENTITYNUM_MAX_NORMAL ) ? &g_entities[out->target] : NULL;
		if ( !t || !t->inuse || !t->client || t->health <= 0 || G_RPDowned( t ) ) {
			out->order = RPCO_DEFEND;	// (done with it)
			out->target = -1;
		} else if ( !helper && npc->enemy != t && !G_RPC_Ignored( out, t, anchor ) && Distance( t->r.currentOrigin, anchor ) < leash ) {
			G_SetEnemy( npc, t );
		}
	}
	enemy = npc->enemy;
	if ( enemy && ( helper || G_RPCompEnemy( npc, enemy ) != 1 || Distance( npc->r.currentOrigin, anchor ) > leash ||
		Distance( enemy->r.currentOrigin, anchor ) > reach * 1.5f || G_RPC_Ignored( out, enemy, anchor ) ) ) {
		G_RPC_Disengage( npc, out );	// (not theirs to fight, its owner to get up, off the leash, or one it lets be - hurt by them, say: back)
		enemy = NULL;
	}
	if ( !enemy && !helper && out->order != RPCO_FOLLOW && !owner->client->ps.m_iVehicleNum && Distance( npc->r.currentOrigin, anchor ) <= leash ) {
		enemy = G_RPC_Scan( npc, out, anchor, reach );	// (not off its leash: back first - else taken on, let go, taken on...)
		if ( enemy ) {
			G_SetEnemy( npc, enemy );
			enemy = npc->enemy;
		}
	}
	if ( enemy ) {
		// fighting: as NPCs do (a neutral one too - the neutrals' ways are droids')
		G_RPC_Flags( npc, qtrue );
		G_RPC_Speeds( npc, out, qfalse );
		enemy = G_RPC_Weapon( npc, out, enemy, anchor, leash ) ? enemy : NULL;	// (NULL: getting nowhere at them, it lets them be - back)
	}
	if ( enemy ) {
		if ( out->helpFrom ) {
			out->helpFrom = 0;
			G_RPC_HelpTell( n, out, 0 );
		}
		out->arrived = qfalse;
		out->trailAt = -1;
		out->stuckSince = 0;
		if ( *team == NPCTEAM_NEUTRAL || npc->client->ps.weapon == WP_THERMAL || npc->client->ps.weapon == WP_STUN_BATON ) {
			*team = NPCTEAM_PLAYER;	// (a villain's with thermals or a baton too: a trooper's fighting, as a hero's - the grenadiers' would chase off its leash, its throw and blow held back for good)
		}
		if ( *bState == BS_CINEMATIC ) {
			*bState = BS_DEFAULT;
		}
		return qfalse;
	}
	G_RPC_Flags( npc, qfalse );
	G_RPC_Speeds( npc, out, qtrue );
	npc->NPC->goalEntity = NULL;
	out->chaseSince = 0;	// (not fighting: the time it was getting nowhere, not for the next fight - its enemy dropped by the NPCs' own think, say)
	if ( npc->client->ps.weapon == WP_SABER && npc->client->ps.saberHolstered < 2 && level.time - out->fightEnd >= RPC_HOLSTER ) {
		WP_DeactivateSaber( npc, qfalse );	// (the fight's over)
	}
	if ( npc->client->ps.weapon == WP_MELEE && G_RPC_Thrower( npc ) ) {
		G_RPC_Arm( npc, WP_THERMAL );	// (and its thermals again)
	}
	NPCS.NPCInfo->desiredPitch = 0.0f;

	// their owner down: up again
	if ( helper ) {
		G_RPC_Pose( owner, npc, qfalse );
		if ( !G_RPC_Posed( npc ) ) {
			G_RPC_Help( owner, o, npc, out );
		}
		NPC_UpdateAngles( qtrue, qtrue );
		return qtrue;
	}
	if ( out->helpFrom ) {
		out->helpFrom = 0;
		G_RPC_HelpTell( n, out, 0 );
	}

	// holding: there
	if ( out->order == RPCO_HOLD ) {
		const float d = Distance( npc->r.currentOrigin, out->hold );
		if ( d > RPC_ARRIVED * 2.0f && !G_RPC_Posed( npc ) && !G_RPC_By( npc, out->hold, d ) ) {
			G_RPC_MoveTo( npc, out->hold, qtrue, qtrue );	// (back to it; someone standing on it, by them)
		}
		NPC_UpdateAngles( qtrue, qtrue );
		return qtrue;
	}

	// their owner riding something: waiting where they are
	if ( owner->client->ps.m_iVehicleNum ) {
		out->trailAt = -1;
		out->stuckSince = 0;
		NPC_UpdateAngles( qtrue, qtrue );
		return qtrue;
	}

	// following: its spot, or single file (or, neither there - their back to a
	// wall, their trail cut - where it is, near them; else toward them)
	if ( !G_RPC_Spot( owner, o, k, G_RPC_Up( n ), npc->s.number, spot ) && !G_RPC_FileSpot( owner, o, G_RPC_FileRank( owner, o, k, npc ), spot ) ) {
		VectorCopy( ( Distance( npc->r.currentOrigin, owner->client->ps.origin ) > RPC_WAIT_NEAR ) ? owner->client->ps.origin : npc->r.currentOrigin, spot );
	}
	dist = Distance( npc->r.currentOrigin, spot );
	G_RPC_StuckFrame( npc, out, (qboolean)( dist > RPC_LEAVE && !G_RPC_Posed( npc ) ) );
	if ( level.time - out->teleTime >= RPC_TELE_WAIT && ( Distance( npc->r.currentOrigin, owner->client->ps.origin ) > RPC_FAR || G_RPC_StuckLong( o, out ) ) ) {
		G_RPC_Pose( owner, npc, qfalse );
		G_RPC_Teleport( owner, o, npc, out );
		NPC_UpdateAngles( qtrue, qtrue );
		return qtrue;
	}
	if ( ( out->arrived && !out->arrivedBy ) ? dist <= RPC_LEAVE : ( dist <= RPC_ARRIVED || G_RPC_By( npc, spot, dist ) ) ) {
		// (there - or by someone standing on its spot, while they are)
		out->arrivedBy = (qboolean)( dist > RPC_ARRIVED );
		out->arrived = qtrue;
		out->trailAt = -1;
		out->stuckSince = 0;
		NPCS.NPCInfo->desiredYaw = o->faceYaw;	// (there: facing their owner's way)
		G_RPC_Pose( owner, npc, (qboolean)( out->order == RPCO_POSE ) );
		NPC_UpdateAngles( qtrue, qtrue );
		return qtrue;
	}
	out->arrived = qfalse;
	G_RPC_Pose( owner, npc, qfalse );	// (off to its spot: up first)
	if ( G_RPC_Posed( npc ) ) {
		NPC_UpdateAngles( qtrue, qtrue );
		return qtrue;	// (getting up)
	}
	if ( !G_RPC_Goal( o, npc, out, spot, goal ) ) {
		if ( level.time - out->teleTime < RPC_TELE_WAIT || !G_RPC_Teleport( owner, o, npc, out ) ) {
			if ( !out->stuckSince ) {
				out->stuckSince = level.time ? level.time : 1;	// (no way, and nowhere by them: as stuck)
			}
		}
		NPC_UpdateAngles( qtrue, qtrue );
		return qtrue;
	}
	{
		const playerState_t *ps = &owner->client->ps;
		const float speed = sqrtf( ps->velocity[0] * ps->velocity[0] + ps->velocity[1] * ps->velocity[1] );
		run = (qboolean)( speed > RPC_RUNNING || dist > RPC_LAG || out->trailAt >= 0 );
	}
	if ( !G_RPC_MoveTo( npc, goal, run, (qboolean)( out->trailAt < 0 ) ) && out->trailAt >= 0 && !G_RPC_CutAhead( o, out ) ) {
		out->trailAt = -1;	// (stopped at an edge on its way to that point of the trail: another looked for - not the last before a cut)
	}
	NPC_UpdateAngles( qtrue, qtrue );
	return qtrue;
}

/*
================================================================================
Orders (the wheel's Companions ring): rpcomp order <what>
================================================================================
*/

// Which of theirs a player looks at (-1: none of them - an order's to all).
static int G_RPC_LookedAt( gentity_t *ent ) {
	const int clientNum = ent->s.number;
	const rpcOwner_t *o = &rpco[clientNum];
	vec3_t eye, dir, d;
	float best = RPC_ADDRESS;
	int k, pick = -1;

	G_RPC_Look( ent, eye, dir );
	for ( k = 0; k < RP_COMP_OUT_MAX; k++ ) {
		const gentity_t *npc = o->out[k].knockedAt ? NULL : G_RPC_Npc( clientNum, &o->out[k] );
		vec3_t mid;
		float len, c;
		if ( !npc ) {
			continue;
		}
		VectorCopy( npc->r.currentOrigin, mid );
		mid[2] += 16.0f;
		VectorSubtract( mid, eye, d );
		len = VectorNormalize( d );
		c = DotProduct( d, dir );
		if ( len < RPC_LOOK && c > best && G_RPC_Sees( eye, mid, clientNum ) ) {
			best = c;
			pick = k;
		}
	}
	return pick;
}

// Whom a player looks at, to be attacked: someone (a player, an NPC) not
// theirs, up and fighting fit (NULL: nobody).
static gentity_t *G_RPC_Target( gentity_t *ent ) {
	vec3_t eye, dir, end;
	trace_t tr;
	int pass = ent->s.number, i;

	G_RPC_Look( ent, eye, dir );
	VectorMA( eye, RPC_LOOK, dir, end );
	for ( i = 0; i < 4; i++ ) {
		gentity_t *hit;
		trap->Trace( &tr, eye, NULL, NULL, end, pass, MASK_SHOT, qfalse, 0, 0 );
		if ( tr.fraction >= 1.0f || tr.entityNum < 0 || tr.entityNum >= ENTITYNUM_MAX_NORMAL ) {
			return NULL;
		}
		hit = &g_entities[tr.entityNum];
		if ( G_RPC_Boss( hit ) == ent->s.number ) {
			VectorCopy( tr.endpos, eye );	// (one of theirs in the way: past it)
			pass = tr.entityNum;
			continue;
		}
		return ( hit->client && hit->health > 0 && !G_RPDowned( hit ) ) ? hit : NULL;
	}
	return NULL;
}

// "Sergeant Vos", "Sergeant Vos and Kira": whom an order's for.
static const char *G_RPC_Names( int clientNum, int only ) {
	static char text[160];
	const rpcOwner_t *o = &rpco[clientNum];
	int k, n = 0;

	text[0] = '\0';
	for ( k = 0; k < RP_COMP_OUT_MAX; k++ ) {
		const rpComp_t *c;
		if ( o->out[k].ent < 0 || o->out[k].knockedAt || ( only >= 0 && k != only ) ) {
			continue;
		}
		c = G_RPComp( clientNum, o->out[k].slot );
		Q_strcat( text, sizeof( text ), va( "%s%s" S_COLOR_WHITE, n ? " and " : "", c ? c->name : "Your companion" ) );
		n++;
	}
	return text;
}

void G_RPCompOrder( gentity_t *ent ) {
	const int clientNum = ent->s.number;
	rpcOwner_t *o;
	char what[32];
	int k, only, up;
	gentity_t *target = NULL;

	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return;
	}
	o = &rpco[clientNum];
	trap->Argv( 2, what, sizeof( what ) );
	if ( !G_RPC_Wanted( clientNum ) ) {
		G_RPC_Say( clientNum, "Companions come out with a character, roleplay on" );
		return;
	}
	up = G_RPC_Up( clientNum );
	if ( !Q_stricmp( what, "come" ) ) {
		int slots[RP_COMP_OUT_MAX], n, i, called = 0;
		if ( o->comeTime && level.time - o->comeTime < RPC_COME_WAIT ) {
			G_RPC_Say( clientNum, "Wait a moment before calling them again" );
			return;
		}
		G_RPC_Recall( clientNum );	// (knocked-out ones whose wait is over: back)
		n = G_RPC_WantedSlots( clientNum, slots );
		for ( i = 0; i < n; i++ ) {
			qboolean there = qfalse;
			for ( k = 0; k < RP_COMP_OUT_MAX; k++ ) {
				there = (qboolean)( there || ( o->out[k].ent >= 0 && o->out[k].slot == slots[i] ) );
			}
			called += there ? 0 : 1;
		}
		if ( !up && !called ) {
			G_RPC_Say( clientNum, "You have no companions to call" );
			return;
		}
		o->comeTime = level.time ? level.time : 1;
		only = G_RPC_LookedAt( ent );
		for ( k = 0; k < RP_COMP_OUT_MAX; k++ ) {
			rpcOut_t *out = &o->out[k];
			gentity_t *npc = out->knockedAt ? NULL : G_RPC_Npc( clientNum, out );
			if ( !npc || ( only >= 0 && k != only ) ) {
				continue;
			}
			if ( out->order == RPCO_HOLD ) {
				out->order = RPCO_DEFEND;
			}
			G_RPC_Disengage( npc, out );
			G_RPC_Pose( ent, npc, qfalse );
			G_RPC_Teleport( ent, o, npc, out );
		}
		o->tryAfter = 0;	// (one called back comes now)
		G_RPC_Say( clientNum, up ? va( "%s: coming to you", G_RPC_Names( clientNum, only ) ) : "Your companions are coming" );
		return;
	}
	if ( !up ) {
		G_RPC_Say( clientNum, "You have no companions out" );
		return;
	}
	if ( !Q_stricmp( what, "formation" ) ) {
		o->formation = ( o->formation + 1 ) % RPCF_NUM;
		G_RPC_Say( clientNum, va( "Formation: %s", rpcFormations[o->formation] ) );
		return;
	}
	only = G_RPC_LookedAt( ent );
	if ( !Q_stricmp( what, "attack" ) ) {
		target = G_RPC_Target( ent );
		if ( !target ) {
			G_RPC_Say( clientNum, "Look at whom they should attack" );
			return;
		}
		only = -1;	// (looking at the one to attack, not at one of theirs)
	} else if ( Q_stricmp( what, "follow" ) && Q_stricmp( what, "defend" ) && Q_stricmp( what, "hold" ) && Q_stricmp( what, "pose" ) ) {
		trap->SendServerCommand( clientNum, "print \"usage: rpcomp order follow|formation|attack|defend|hold|come|pose\n\"" );
		return;
	}
	for ( k = 0; k < RP_COMP_OUT_MAX; k++ ) {
		rpcOut_t *out = &o->out[k];
		gentity_t *npc = out->knockedAt ? NULL : G_RPC_Npc( clientNum, out );
		if ( !npc || ( only >= 0 && k != only ) ) {
			continue;
		}
		out->target = -1;
		if ( !Q_stricmp( what, "follow" ) ) {
			out->order = RPCO_FOLLOW;
			G_RPC_Disengage( npc, out );
		} else if ( !Q_stricmp( what, "defend" ) ) {
			out->order = RPCO_DEFEND;
		} else if ( !Q_stricmp( what, "hold" ) ) {
			out->order = RPCO_HOLD;
			VectorCopy( npc->r.currentOrigin, out->hold );
		} else if ( !Q_stricmp( what, "pose" ) ) {
			out->order = RPCO_POSE;
		} else {
			out->order = RPCO_ATTACK;
			out->target = target->s.number;
			out->ignoreEnemy = 0;	// (one it let be, getting nowhere at them: at them again)
			G_RPC_Disengage( npc, out );
		}
	}
	if ( target ) {
		G_RPC_Say( clientNum, va( "%s: attacking %s", G_RPC_Names( clientNum, only ),
			( target->s.number < MAX_CLIENTS && target->client->pers.netname[0] ) ? target->client->pers.netname : "them" ) );
	} else {
		G_RPC_Say( clientNum, va( "%s: %s", G_RPC_Names( clientNum, only ), !Q_stricmp( what, "follow" ) ? "following you" :
			!Q_stricmp( what, "defend" ) ? "defending you" : !Q_stricmp( what, "hold" ) ? "holding here" : "posing with you" ) );
	}
}

/*
================================================================================
Every frame, starting and stopping
================================================================================
*/

// ClientSpawn: a player's in (again) - their companions come a moment on
// (knocked-out ones whose wait is over too: spawning calls them).
void G_RPCompOwnerSpawned( gentity_t *ent ) {
	const int n = ent ? ent->s.number : -1;

	if ( n < 0 || n >= MAX_CLIENTS ) {
		return;
	}
	rpco[n].comeAfter = level.time + RPC_SPAWN_DELAY;
	rpco[n].tryAfter = 0;
	rpco[n].faceYaw = ent->client ? ent->client->ps.viewangles[YAW] : 0.0f;
	rpco[n].turnSince = 0;
	G_RPC_TrailClear( &rpco[n] );
	G_RPC_Recall( n );
	memset( rpco[n].savedAt, 0, sizeof( rpco[n].savedAt ) );	// (a new life: theirs come out fresh)
}

// g_vrrp.c: a companion of the character a player plays deleted - one out
// goes; the others' numbers move up.
void G_RPCompDeleted( int clientNum, int slot ) {
	rpcOwner_t *o;
	int k, s;

	if ( clientNum < 0 || clientNum >= MAX_CLIENTS || slot < 0 || slot >= RP_COMP_MAX ) {
		return;
	}
	o = &rpco[clientNum];
	for ( k = 0; k < RP_COMP_OUT_MAX; k++ ) {
		if ( o->out[k].ent < 0 ) {
			continue;
		}
		if ( o->out[k].slot == slot ) {
			G_RPC_Remove( clientNum, k );
		} else if ( o->out[k].slot > slot ) {
			o->out[k].slot--;
		}
	}
	for ( s = slot; s < RP_COMP_MAX - 1; s++ ) {
		o->knocked[s] = o->knocked[s + 1];
		Q_strncpyz( o->knockedName[s], o->knockedName[s + 1], sizeof( o->knockedName[0] ) );
		o->savedHealth[s] = o->savedHealth[s + 1];
		o->savedAt[s] = o->savedAt[s + 1];
		Q_strncpyz( o->failed[s], o->failed[s + 1], sizeof( o->failed[0] ) );
	}
	o->knocked[RP_COMP_MAX - 1] = o->savedAt[RP_COMP_MAX - 1] = 0;
	o->knockedName[RP_COMP_MAX - 1][0] = o->failed[RP_COMP_MAX - 1][0] = '\0';
	G_RPC_Tell( clientNum );
}

// G_RPClientDisconnect: their companions go with them, and nothing of
// theirs is left for whoever comes next.
void G_RPCompOwnerGone( int clientNum ) {
	int k;

	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return;
	}
	G_RPC_RemoveAll( clientNum );
	memset( &rpco[clientNum], 0, sizeof( rpco[0] ) );
	rpco[clientNum].charIndex = rpco[clientNum].knockGen = -1;
	for ( k = 0; k < RP_COMP_OUT_MAX; k++ ) {
		rpco[clientNum].out[k].ent = -1;
	}
	trap->SetConfigstring( CS_VR_COMPANIONS + clientNum, "" );
}

void G_RPCompFrame( void ) {
	int i;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		gentity_t *ent = &g_entities[i];
		G_RPC_Reconcile( i );
		if ( rpco[i].charIndex >= 0 && ent->inuse && ent->client ) {
			G_RPC_TrailFrame( ent, &rpco[i] );
			G_RPC_FaceFrame( ent, &rpco[i] );
		} else if ( rpco[i].trailCount ) {
			G_RPC_TrailClear( &rpco[i] );	// (not walked meanwhile: none of it's theirs to follow next)
		}
	}
}

void G_RPCompInit( void ) {
	int i, k;

	memset( rpco, 0, sizeof( rpco ) );
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		rpco[i].charIndex = rpco[i].knockGen = -1;
		for ( k = 0; k < RP_COMP_OUT_MAX; k++ ) {
			rpco[i].out[k].ent = -1;
		}
		trap->SetConfigstring( CS_VR_COMPANIONS + i, "" );
	}
	rpcSpawning = NULL;
	rpcNumModels = 0;
	rpcNumTold = 0;
}
