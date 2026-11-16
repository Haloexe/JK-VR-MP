/*
===========================================================================
OpenJK VR (multiplayer) - roleplay, stage 3, the player's side (the
server's is game/g_vrcomp.c): companions.

YOURS, FOR THE COMPANIONS PAGE. The server tells us the companions of the
character we play: rpcmps <count | -1: no character> <how many may come
out>, then rpcmp <slot> <out> <seconds before it can come again> "<name>"
"<type>" "<look>" <r> <g> <b> each. The menus read them in ui_rpComp*
cvars (CG_RPC_UI; ui_rpCompsGen counts the lists told, so the page knows
one came - a companion saved), and the page changes them with "rpui comp
<what>" (CG_RP_CompUI), which go to the server as rpcomp.

WHOSE. The configstring CS_VR_COMPANIONS + owner says which entities are a
player's companions, their names, and a look whose skin was too long for
a model's configstring ("<entity>\<name>\<look>", '\' between; the look ""
but for those - on a server before CS_VR_COMPLOOKS, its model's
configstring has the model's default skin). A companion you look at shows
its card (cg_vrrp.c: its name over its head, whose it is, its side); the
wheel's Companions ring (cg_vrpose.c) gives your orders, greyed with none
of yours out; one getting you up (rpup: cg_vrdown.c) goes by its name.

LOOKS. Every game is sent a companion as its NPC type comes, its type's
model and skin (a game without a look's model would go down drawing it);
its look of its own is told beside, to us (CS_VR_COMPLOOKS: "<entity>\<look>"
each, '\' between, as many in each of them as fit - out, or knocked out
while its body's there). We draw it in its look where this PC has the
model (in the look's skin, else - not here - as it's sent where that's the
same model, else the model's own), else as its type comes
(cg_players.c: CG_G2AnimEntModelLoad).

STAND-INS. A companion's model this PC hasn't got (its look's, and its
type's), or one not on the humanoid skeleton (another skeleton wants
animations of its own, which may not be here either), is drawn as a
stand-in for its kind - a Jedi, a trooper, anyone else - and never not at
all; so is any other NPC whose model this PC hasn't got at all, on any
server (CG_RP_NpcModel). On a server before CS_VR_COMPLOOKS, a look told
beside its model's configstring (WHOSE, above) is drawn in its skin.

VOICES. A companion drawn in its look, on a model not its type's, speaks
as that look's character does - as a player of its model, and its NPC's
lines as the game's own NPC of that character has them - not as the type it
fights as (CG_RP_CompSounds); drawn as its type comes, as its type does.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "cg_local.h"

#define RPC_NAME_BUF		64
#define RPC_MODELS			64		// models checked for stand-ins, kept

// what the wheel's orders send (cg_vrpose.c's slots, in order)
static const char *rpcOrders[] = { "follow", "formation", "attack", "defend", "hold", "come", "pose" };

static struct {
	int			count;				// the companions of the character we play (-1: none, or not told)
	int			most;				// how many may come out
	int			gen;				// the lists told (ui_rpCompsGen)
	struct {
		char		name[RPC_NAME_BUF];
		char		type[RP_COMP_TYPE_SIZE];
		char		look[MAX_QPATH];
		int			rgb[3];
		qboolean	out;
		int			back;			// cg.time it can come again (0: it isn't knocked out)
		int			waitShown;		// the seconds the menus show (-1: not told yet)
	} comp[RP_COMP_MAX];
} cgc;

// a player's companions out, as their configstring says
typedef struct {
	char	cs[MAX_STRING_CHARS];	// the configstring, as read
	int		count;
	int		ent[RP_COMP_OUT_MAX];
	char	name[RP_COMP_OUT_MAX][RPC_NAME_BUF];
	char	look[RP_COMP_OUT_MAX][MAX_QPATH];	// one whose skin its model's configstring couldn't carry ("": none)
} cgCompsOf_t;

static cgCompsOf_t cgCompsOf[MAX_CLIENTS];

// the companions in looks of their own, as their configstrings say (CS_VR_COMPLOOKS)
static struct {
	char	cs[RP_COMP_LOOKS_CS][MAX_STRING_CHARS];	// the configstrings, as read
	int		count;
	int		ent[RP_COMP_OUT_MAX * MAX_CLIENTS];
	char	look[RP_COMP_OUT_MAX * MAX_CLIENTS][MAX_QPATH];
} cgcLooks;

static struct {
	char		model[MAX_QPATH];
	qboolean	there;		// its model.glm's on this PC...
	qboolean	ok;			// ...on the humanoid skeleton
} cgcModels[RPC_MODELS];
static int cgcNumModels;

/*
================================================================================
Whose
================================================================================
*/

// A player's companions out (their configstring read again if it's changed;
// none where roleplay isn't on - another server's configstrings there may
// be anything).
static const cgCompsOf_t *CG_RPC_Of( int owner ) {
	cgCompsOf_t *c = &cgCompsOf[owner];
	const char *cs = CG_RP_On() ? CG_ConfigString( CS_VR_COMPANIONS + owner ) : "";
	char buf[MAX_STRING_CHARS];
	char *p;

	if ( !strcmp( cs, c->cs ) ) {
		return c;
	}
	Q_strncpyz( c->cs, cs, sizeof( c->cs ) );
	c->count = 0;
	Q_strncpyz( buf, cs, sizeof( buf ) );
	p = buf;
	while ( *p && c->count < RP_COMP_OUT_MAX ) {
		char *field[3];
		int i, ent;
		if ( !strchr( p, '\\' ) ) {
			break;	// (an entity with nothing after it)
		}
		for ( i = 0; i < 3; i++ ) {
			char *end = strchr( p, '\\' );
			field[i] = p;
			if ( end ) {
				*end = '\0';
				p = end + 1;
			} else {
				p += strlen( p );
			}
		}
		ent = atoi( field[0] );
		if ( ent >= MAX_CLIENTS && ent < ENTITYNUM_MAX_NORMAL ) {
			c->ent[c->count] = ent;
			Q_strncpyz( c->name[c->count], field[1], sizeof( c->name[0] ) );
			Q_strncpyz( c->look[c->count], BG_VRCompLookOk( field[2] ) ? field[2] : "", sizeof( c->look[0] ) );
			c->count++;
		}
	}
	return c;
}

// Whose companion an entity is (-1: nobody's).
int CG_RP_CompOwner( int entNum ) {
	int i, k;

	if ( entNum < MAX_CLIENTS || entNum >= ENTITYNUM_MAX_NORMAL ) {
		return -1;
	}
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		const cgCompsOf_t *c = CG_RPC_Of( i );
		for ( k = 0; k < c->count; k++ ) {
			if ( c->ent[k] == entNum ) {
				return i;
			}
		}
	}
	return -1;
}

// A companion's name (NULL: the entity isn't one).
const char *CG_RP_CompName( int entNum ) {
	const int owner = CG_RP_CompOwner( entNum );
	const cgCompsOf_t *c;
	int k;

	if ( owner < 0 ) {
		return NULL;
	}
	c = CG_RPC_Of( owner );
	for ( k = 0; k < c->count; k++ ) {
		if ( c->ent[k] == entNum ) {
			return c->name[k][0] ? c->name[k] : "A companion";
		}
	}
	return NULL;
}

// The companions in looks of their own (their configstrings read again if
// one's changed; none where roleplay isn't on).
static void CG_RPC_Looks( void ) {
	qboolean changed = qfalse;
	int n;

	for ( n = 0; n < RP_COMP_LOOKS_CS; n++ ) {
		const char *cs = CG_RP_On() ? CG_ConfigString( CS_VR_COMPLOOKS + n ) : "";
		if ( strcmp( cs, cgcLooks.cs[n] ) ) {
			Q_strncpyz( cgcLooks.cs[n], cs, sizeof( cgcLooks.cs[0] ) );
			changed = qtrue;
		}
	}
	if ( !changed ) {
		return;
	}
	cgcLooks.count = 0;
	for ( n = 0; n < RP_COMP_LOOKS_CS; n++ ) {
		cgcLooks.count = BG_VRCompLooksRead( cgcLooks.cs[n], cgcLooks.ent, cgcLooks.look, cgcLooks.count, (int)ARRAY_LEN( cgcLooks.ent ) );
	}
}

// A companion's look of its own ("": none, or the entity isn't one) - out,
// or knocked out (its body's still there).
const char *CG_RP_CompLook( int entNum ) {
	int k;

	CG_RPC_Looks();
	for ( k = 0; k < cgcLooks.count; k++ ) {
		if ( cgcLooks.ent[k] == entNum ) {
			return cgcLooks.look[k];
		}
	}
	return "";
}

// A companion's look told beside its configstring (a skin too long for it:
// "" - none, or the entity isn't one).
static const char *CG_RPC_ToldLook( int entNum ) {
	const int owner = CG_RP_CompOwner( entNum );
	const cgCompsOf_t *c;
	int k;

	if ( owner < 0 ) {
		return "";
	}
	c = CG_RPC_Of( owner );
	for ( k = 0; k < c->count; k++ ) {
		if ( c->ent[k] == entNum ) {
			return c->look[k];
		}
	}
	return "";
}

// How many of yours are out.
int CG_RP_CompsUp( void ) {
	return ( cg.clientNum >= 0 && cg.clientNum < MAX_CLIENTS ) ? CG_RPC_Of( cg.clientNum )->count : 0;
}

// How many of yours are ticked to come out (as the server last said).
static int CG_RPC_Ticked( void ) {
	int k, n = 0;

	for ( k = 0; k < cgc.count && k < RP_COMP_MAX; k++ ) {
		n += cgc.comp[k].out ? 1 : 0;
	}
	return n;
}

/*
================================================================================
The wheel's Companions ring
================================================================================
*/

// Why the ring's of no use (NULL: it is).
const char *CG_RP_CompsWhy( void ) {
	if ( !CG_RP_On() ) {
		return "Roleplay is off";
	}
	if ( CG_RP_CompsAllowed() <= 0 ) {
		return "This server has no companions";
	}
	if ( CG_RP_MyFaction() < 0 ) {
		return "Play a character first";
	}
	return NULL;
}

// Why an order can't be given now (NULL: it can).
const char *CG_RP_CompOrderWhy( int order ) {
	const char *why = CG_RP_CompsWhy();

	if ( why ) {
		return why;
	}
	if ( order == 5 ) {	// (Come to me: those out, and one knocked out whose wait's over)
		return ( CG_RP_CompsUp() || CG_RPC_Ticked() ) ? NULL : "Tick companions to come out: Profile, Companions";
	}
	return CG_RP_CompsUp() ? NULL : "None of your companions are out";
}

void CG_RP_CompOrder( int order ) {
	if ( order >= 0 && order < (int)ARRAY_LEN( rpcOrders ) ) {
		CG_RP_Send( va( "rpcomp order %s", rpcOrders[order] ) );
	}
}

/*
================================================================================
The Companions page: what it shows (ui_rpComp*), what it does (rpui comp)
================================================================================
*/

// ui_rpCompWait<i>: a knocked-out one's wait, as the page shows it.
static void CG_RPC_UIWait( int i ) {
	const int left = ( i < cgc.count && cgc.comp[i].back ) ? ( cgc.comp[i].back - cg.time + 999 ) / 1000 : 0;
	const int secs = ( left > 0 ) ? left : 0;

	if ( secs == cgc.comp[i].waitShown ) {
		return;
	}
	cgc.comp[i].waitShown = secs;
	trap->Cvar_Set( va( "ui_rpCompWait%i", i ), secs ? va( "Knocked out: back in %i:%02i", secs / 60, secs % 60 ) : "" );
}

static void CG_RPC_UI( int i ) {
	const qboolean has = (qboolean)( i < cgc.count );

	trap->Cvar_Set( va( "ui_rpCompName%i", i ), has ? cgc.comp[i].name : "" );
	trap->Cvar_Set( va( "ui_rpCompType%i", i ), has ? cgc.comp[i].type : "" );
	trap->Cvar_Set( va( "ui_rpCompLook%i", i ), has ? cgc.comp[i].look : "" );
	trap->Cvar_Set( va( "ui_rpCompColor%i", i ), has ? va( "%i %i %i", cgc.comp[i].rgb[0], cgc.comp[i].rgb[1], cgc.comp[i].rgb[2] ) : "" );
	trap->Cvar_Set( va( "ui_rpCompOut%i", i ), ( has && cgc.comp[i].out ) ? "1" : "0" );
	cgc.comp[i].waitShown = -1;
	CG_RPC_UIWait( i );
}

static void CG_RPC_UIAll( void ) {
	int i;

	trap->Cvar_Set( "ui_rpComps", va( "%i", cgc.count ) );
	trap->Cvar_Set( "ui_rpCompsOut", va( "%i", cgc.most ) );
	for ( i = 0; i < RP_COMP_MAX; i++ ) {
		CG_RPC_UI( i );
	}
}

// rpcmps <count | -1> <how many may come out>
void CG_RP_CompsCmd( void ) {
	int i;

	cgc.count = Com_Clampi( -1, RP_COMP_MAX, atoi( CG_Argv( 1 ) ) );
	cgc.most = Com_Clampi( 0, RP_COMP_OUT_MAX, atoi( CG_Argv( 2 ) ) );
	for ( i = Q_max( cgc.count, 0 ); i < RP_COMP_MAX; i++ ) {
		memset( &cgc.comp[i], 0, sizeof( cgc.comp[0] ) );
	}
	CG_RPC_UIAll();
	cgc.gen = ( cgc.gen + 1 ) & 0xffff;
	trap->Cvar_Set( "ui_rpCompsGen", va( "%i", cgc.gen ) );
}

// rpcmp <slot> <out> <seconds before it can come again> "<name>" "<type>" "<look>" <r> <g> <b>
void CG_RP_CompCmd( void ) {
	const int i = atoi( CG_Argv( 1 ) );
	const int wait = atoi( CG_Argv( 3 ) );
	int k;

	if ( trap->Cmd_Argc() < 10 || i < 0 || i >= RP_COMP_MAX || i >= cgc.count ) {
		return;
	}
	cgc.comp[i].out = (qboolean)( atoi( CG_Argv( 2 ) ) == 1 );
	cgc.comp[i].back = ( wait > 0 ) ? cg.time + Q_min( wait, 3600 ) * 1000 : 0;
	trap->Cmd_Argv( 4, cgc.comp[i].name, sizeof( cgc.comp[i].name ) );
	trap->Cmd_Argv( 5, cgc.comp[i].type, sizeof( cgc.comp[i].type ) );
	trap->Cmd_Argv( 6, cgc.comp[i].look, sizeof( cgc.comp[i].look ) );
	for ( k = 0; k < 3; k++ ) {
		cgc.comp[i].rgb[k] = Com_Clampi( 0, 255, atoi( CG_Argv( 7 + k ) ) );
	}
	CG_RPC_UI( i );
}

// Text a menu typed, as it goes in a command: no quotes, backslashes,
// semicolons or control characters.
static void CG_RPC_Clean( const char *in, char *out, int size ) {
	int n = 0;

	for ( ; in && *in && n < size - 1; in++ ) {
		const unsigned char c = (unsigned char)*in;
		if ( c < 32 || c == '"' || c == '\\' || c == ';' ) {
			continue;
		}
		out[n++] = (char)c;
	}
	out[n] = '\0';
}

/*
rpui comp <what>: the Companions page's buttons.
  save <slot>        the companion the page made (ui_rpCompEdit{Name,Type,
                     Look,R,G,B}) to the server, as that slot (the next: a
                     new one)
  del <slot>
  out <slot> <0|1>   whether it comes out with you
*/
void CG_RP_CompUI( void ) {
	char what[16];
	const int slot = atoi( CG_Argv( 3 ) );

	trap->Cmd_Argv( 2, what, sizeof( what ) );
	if ( cgc.count < 0 ) {
		trap->Cvar_Set( "ui_rpMessage", "Companions are a character's: play one first" );
		return;
	}
	if ( !CG_Argv( 3 )[0] || slot < 0 || slot >= RP_COMP_MAX || slot > cgc.count ) {
		return;
	}
	if ( !Q_stricmp( what, "save" ) ) {
		char name[MAX_CVAR_VALUE_STRING], type[MAX_CVAR_VALUE_STRING], look[MAX_CVAR_VALUE_STRING], text[MAX_CVAR_VALUE_STRING];
		int rgb[3], k;
		static const char *colours[3] = { "ui_rpCompEditR", "ui_rpCompEditG", "ui_rpCompEditB" };
		trap->Cvar_VariableStringBuffer( "ui_rpCompEditName", text, sizeof( text ) );
		CG_RPC_Clean( text, name, sizeof( name ) );
		trap->Cvar_VariableStringBuffer( "ui_rpCompEditType", text, sizeof( text ) );
		CG_RPC_Clean( text, type, sizeof( type ) );
		trap->Cvar_VariableStringBuffer( "ui_rpCompEditLook", text, sizeof( text ) );
		CG_RPC_Clean( text, look, sizeof( look ) );
		for ( k = 0; k < 3; k++ ) {
			trap->Cvar_VariableStringBuffer( colours[k], text, sizeof( text ) );
			rgb[k] = text[0] ? Com_Clampi( 0, 255, atoi( text ) ) : 255;
		}
		if ( !name[0] ) {
			trap->Cvar_Set( "ui_rpMessage", "Give it a name" );
			return;
		}
		if ( !type[0] ) {
			trap->Cvar_Set( "ui_rpMessage", "Choose what it fights as" );
			return;
		}
		CG_RP_Send( va( "rpcomp set %i \"%s\" \"%s\" \"%s\" %i %i %i", slot + 1, name, type, look, rgb[0], rgb[1], rgb[2] ) );
	} else if ( !Q_stricmp( what, "del" ) ) {
		if ( slot < cgc.count ) {
			CG_RP_Send( va( "rpcomp del %i", slot + 1 ) );
		}
	} else if ( !Q_stricmp( what, "out" ) ) {
		if ( slot < cgc.count ) {
			CG_RP_Send( va( "rpcomp out %i %i", slot + 1, atoi( CG_Argv( 4 ) ) ? 1 : 0 ) );
		}
	}
}

/*
================================================================================
Stand-ins (cg_players.c: CG_G2AnimEntModelLoad)
================================================================================
*/

// Whether a model's here (there), and on the humanoid skeleton (as its
// model.glm's first bytes say: bg_vrcomp.c) - kept: asked once.
static qboolean CG_RPC_Model( const char *path, qboolean *there ) {
	byte head[BG_VRCOMP_GLM_HEAD];
	fileHandle_t f = 0;
	int i, len;
	qboolean ok = qfalse;

	for ( i = 0; i < cgcNumModels; i++ ) {
		if ( !Q_stricmp( cgcModels[i].model, path ) ) {
			*there = cgcModels[i].there;
			return cgcModels[i].ok;
		}
	}
	len = trap->FS_Open( path, &f, FS_READ );
	*there = (qboolean)( f != 0 );
	if ( f ) {
		if ( len >= (int)sizeof( head ) && trap->FS_Read( head, sizeof( head ), f ) == (int)sizeof( head ) ) {
			ok = BG_VRCompGlmHumanoid( head, sizeof( head ) );
		}
		trap->FS_Close( f );
	}
	if ( cgcNumModels < RPC_MODELS ) {
		Q_strncpyz( cgcModels[cgcNumModels].model, path, sizeof( cgcModels[0].model ) );
		cgcModels[cgcNumModels].there = *there;
		cgcModels[cgcNumModels].ok = ok;
		cgcNumModels++;
	}
	return ok;
}

// Whether a model's here, on the humanoid skeleton.
static qboolean CG_RPC_ModelOk( const char *path ) {
	qboolean there;
	return CG_RPC_Model( path, &there );
}

// Whether a model's here at all (on whatever skeleton).
static qboolean CG_RPC_ModelThere( const char *path ) {
	qboolean there;
	CG_RPC_Model( path, &there );
	return there;
}

// A stand-in for an NPC's kind, for a model this PC hasn't got: a Jedi, a
// trooper, anyone else (or, those not here either, the game's own model) -
// into modelName, and its skin.
static int CG_RPC_StandIn( const centity_t *cent, char *modelName, int size ) {
	const char *standIn;
	char path[MAX_QPATH];

	switch ( cent->currentState.NPC_class ) {
	case CLASS_JEDI:
	case CLASS_LUKE:
	case CLASS_KYLE:
	case CLASS_REBORN:
	case CLASS_DESANN:
	case CLASS_TAVION:
	case CLASS_SHADOWTROOPER:
		standIn = "jedi_hm";
		break;
	case CLASS_STORMTROOPER:
	case CLASS_SWAMPTROOPER:
	case CLASS_IMPERIAL:
	case CLASS_IMPWORKER:
	case CLASS_COMMANDO:
		standIn = "stormtrooper";
		break;
	default:
		standIn = "rebel";
		break;
	}
	Com_sprintf( path, sizeof( path ), "models/players/%s/model.glm", standIn );
	if ( !CG_RPC_ModelOk( path ) ) {
		standIn = DEFAULT_MODEL;	// (always here)
		Com_sprintf( path, sizeof( path ), "models/players/%s/model.glm", standIn );
	}
	Q_strncpyz( modelName, path, size );
	return trap->R_RegisterSkin( va( "models/players/%s/model_default.skin", standIn ) );
}

// A companion's model, as CG_G2AnimEntModelLoad has it ("models/players/<model>/model.glm",
// its skin taken off - as the server sends it every game: as its type comes):
// its look of its own instead, where that's here; else as it's sent, where
// that's here; else a stand-in for its kind - and its skin. The look it's
// drawn in on a model not its type's, into drawnLook ("": as its type comes,
// in its type's model, or a stand-in): its voice (CG_RP_CompSounds).
int CG_RP_CompModel( const centity_t *cent, char *modelName, int size, int skinID, char *drawnLook, int lookSize ) {
	const char *look = CG_RP_CompLook( cent->currentState.number );
	const char *told = look[0] ? "" : CG_RPC_ToldLook( cent->currentState.number );	// (a server before CS_VR_COMPLOOKS)
	char path[MAX_QPATH];

	if ( drawnLook && lookSize > 0 ) {
		drawnLook[0] = '\0';
	}
	if ( look[0] ) {
		char model[MAX_QPATH], skin[MAX_QPATH];
		BG_VRCompLookSplit( look, model, sizeof( model ), skin, sizeof( skin ) );
		Com_sprintf( path, sizeof( path ), "models/players/%s/model.glm", model );
		if ( CG_RPC_ModelOk( path ) ) {
			qhandle_t h = trap->R_RegisterSkin( strchr( skin, '|' ) ? va( "models/players/%s/|%s", model, skin ) :
				va( "models/players/%s/model_%s.skin", model, skin ) );
			if ( !h ) {	// (its skin's not here: as it's sent where that's the same model, else the model's own)
				h = !Q_stricmp( path, modelName ) ? skinID : trap->R_RegisterSkin( va( "models/players/%s/model_default.skin", model ) );
			}
			if ( drawnLook && lookSize > 0 && Q_stricmp( path, modelName ) ) {
				Q_strncpyz( drawnLook, look, lookSize );	// (its type's model: its type's voice is its)
			}
			Q_strncpyz( modelName, path, size );
			return h;
		}
	}
	if ( CG_RPC_ModelOk( modelName ) ) {
		if ( told[0] ) {
			// its skin, told beside (too long for its model's configstring,
			// which has the default): the skin the look names, on its model
			char model[MAX_QPATH], skin[MAX_QPATH];
			qhandle_t h;
			BG_VRCompLookSplit( told, model, sizeof( model ), skin, sizeof( skin ) );
			Com_sprintf( path, sizeof( path ), "models/players/%s/model.glm", model );
			if ( !Q_stricmp( path, modelName ) ) {
				h = trap->R_RegisterSkin( strchr( skin, '|' ) ? va( "models/players/%s/|%s", model, skin ) : va( "models/players/%s/model_%s.skin", model, skin ) );
				if ( h ) {
					return h;
				}
			}
		}
		return skinID;
	}
	return CG_RPC_StandIn( cent, modelName, size );
}

// Any other NPC (nobody's companion, on any server), as CG_G2AnimEntModelLoad
// has it: as it's sent - or, its model not on this PC at all (a server's NPC
// of its own, say), a stand-in for its kind: a game would go down drawing a
// model it hasn't got. (On another skeleton, as it's sent: a droid's, a
// beast's - its own animations.)
int CG_RP_NpcModel( const centity_t *cent, char *modelName, int size, int skinID ) {
	return CG_RPC_ModelThere( modelName ) ? skinID : CG_RPC_StandIn( cent, modelName, size );
}

/*
================================================================================
Voices (cg_players.c: CG_G2AnimEntModelLoad)

A companion drawn in a look of its own, on a model not its type's, speaks as
that look's character does - not as the type it fights as (its configstrings
give it its type's voice: CG_HandleNPCSounds):
- what a player of its model says (pain, a fall, dying, a taunt...): from
  its model's sound folder, as the game finds a player's (its sounds.cfg -
  its skin's own first, sounds_<skin>.cfg: CG_LoadCISounds), else the
  folder of the NPC type found for it (below), else its model's name; and
  the game's own voice for any of those it hasn't got, a man's or, its
  sounds.cfg saying so, a woman's;
- what an NPC of that character says (in a fight, hunting someone, a Jedi's
  lines): as the game's own NPC of it has them - the NPC type whose model it
  is (the one named for it, else the first the files have that speaks),
  else whose everyday voice its sound folder is - and from its sound folder
  where that type hasn't a set of them, or none's found (a line not there:
  not said).
Drawn as its type comes (the look's model not on this PC, or its type's
model), it speaks as its type does. The NPC types' voices are read once
(the NPC files, as the server reads them: the voice of each model and of
each everyday voice's folder kept).
================================================================================
*/

#define RPC_VOICES			1024	// models' and folders' voices kept
#define RPC_VOICE_DIR		48		// a sound folder (a model) named longer: not kept
#define RPC_NPC_FILE_MAX	0x40000	// a file of NPC types read: one as big, left out (as the server leaves it out)

extern char *cg_customSoundNames[MAX_CUSTOM_SOUNDS];

typedef struct {
	char		key[RPC_VOICE_DIR];		// a model, or an everyday voice's folder...
	qboolean	folder;					// ...which
	qboolean	named;					// its type's named for it
	char		snd[4][RPC_VOICE_DIR];	// its type's sound folders: everyday (snd), in a fight (sndcombat), the rest
										// (sndextra), a Jedi's (sndjedi) - "": none
} cgcVoice_t;

static struct {
	qboolean	read;
	int			count;
	cgcVoice_t	list[RPC_VOICES];
} cgcVoices;

// The voice kept for a model (or an everyday voice's folder: folder).
static const cgcVoice_t *CG_RPC_VoiceOf( const char *key, qboolean folder ) {
	int i;

	for ( i = 0; key[0] && i < cgcVoices.count; i++ ) {
		if ( cgcVoices.list[i].folder == folder && !Q_stricmp( cgcVoices.list[i].key, key ) ) {
			return &cgcVoices.list[i];
		}
	}
	return NULL;
}

// An NPC type's voice kept for a model (or a folder) - the first type's, but
// for one named for it.
static void CG_RPC_KeepVoice( const char *key, qboolean folder, const char *name, char snd[4][RPC_VOICE_DIR] ) {
	cgcVoice_t *v = (cgcVoice_t *)CG_RPC_VoiceOf( key, folder );
	const qboolean named = (qboolean)!Q_stricmp( name, key );

	if ( !key[0] || strlen( key ) >= RPC_VOICE_DIR ) {
		return;
	}
	if ( v ) {
		if ( v->named || !named ) {
			return;
		}
	} else {
		if ( cgcVoices.count >= RPC_VOICES ) {
			return;
		}
		v = &cgcVoices.list[cgcVoices.count++];
		Q_strncpyz( v->key, key, sizeof( v->key ) );
		v->folder = folder;
	}
	v->named = named;
	memcpy( v->snd, snd, sizeof( v->snd ) );
}

// The voices of the NPC types a file of them defines - as the server reads
// one, the part of it it keeps (NPC_LoadParms; as the Companions page does:
// ui_vrcomp.c): each name the game's walk through them finds, then its
// block ("playerModel", "snd", "sndcombat", "sndextra", "sndjedi" on its
// lines - a folder to its first '/', as NPC_ParseParms takes it - inner
// blocks skipped).
static void CG_RPC_ReadVoiceFile( const char *path ) {
	static char text[RPC_NPC_FILE_MAX + 1];
	static const char *keys[4] = { "snd", "sndcombat", "sndextra", "sndjedi" };
	fileHandle_t f = 0;
	const char *p, *block, *q;
	int len;

	len = trap->FS_Open( path, &f, FS_READ );
	if ( !f ) {
		return;
	}
	if ( len <= 0 || len >= RPC_NPC_FILE_MAX ) {
		trap->FS_Close( f );
		return;	// (one this big the server leaves out: NPC_LoadParms)
	}
	trap->FS_Read( text, len, f );
	trap->FS_Close( f );
	text[len] = '\0';
	len = BG_VRNPCFileText( text, len );
	text[BG_VRBlocksInStep( text, len, NULL, 0 )] = '\0';
	p = text;
	COM_BeginParseSession( path );
	while ( p ) {
		char name[RP_COMP_TYPE_SIZE], model[MAX_QPATH], snd[4][RPC_VOICE_DIR];
		const char *tok = COM_ParseExt( &p, qtrue );
		int set;

		if ( !tok[0] ) {
			break;
		}
		Q_strncpyz( name, tok, sizeof( name ) );
		block = p;
		if ( strcmp( COM_ParseExt( &p, qtrue ), "{" ) ) {
			p = block;
			SkipBracedSection( &p, 0 );	// (no block: past what the game's walk takes for one - not a type)
			continue;
		}
		block = p;
		if ( !SkipBracedSection( &p, 1 ) || !p ) {
			break;	// (can't be: what the server keeps closes)
		}
		text[p - text - 1] = '\0';	// (its "}" - the walk past it: its keys read up to there)
		model[0] = '\0';
		memset( snd, 0, sizeof( snd ) );
		for ( q = block; ( tok = COM_ParseExt( &q, qtrue ) )[0]; ) {
			if ( !strcmp( tok, "{" ) ) {
				SkipBracedSection( &q, 1 );
				continue;
			}
			if ( !Q_stricmp( tok, "playerModel" ) ) {
				Q_strncpyz( model, COM_ParseExt( &q, qfalse ), sizeof( model ) );
				continue;
			}
			for ( set = 0; set < 4 && Q_stricmp( tok, keys[set] ); set++ ) {
			}
			if ( set < 4 ) {
				char folder[MAX_QPATH], *slash;
				Q_strncpyz( folder, COM_ParseExt( &q, qfalse ), sizeof( folder ) );
				if ( ( slash = strchr( folder, '/' ) ) != NULL ) {
					*slash = '\0';
				}
				Q_strncpyz( snd[set], strlen( folder ) < RPC_VOICE_DIR ? folder : "", sizeof( snd[set] ) );
				continue;
			}
			while ( COM_ParseExt( &q, qfalse )[0] ) {
			}	// (the rest of its line - SkipRestOfLine steps past a '\0': the block's end, on its last line)
		}
		if ( !snd[0][0] && !snd[1][0] && !snd[2][0] && !snd[3][0] ) {
			continue;	// (it says nothing)
		}
		CG_RPC_KeepVoice( model, qfalse, name, snd );
		CG_RPC_KeepVoice( snd[0], qtrue, name, snd );
	}
}

// The NPC types' voices, read once.
static void CG_RPC_ReadVoices( void ) {
	static char files[0x40000];	// (every file there can be: 4095, the engine's most)
	const char *p;
	int i, count;

	if ( cgcVoices.read ) {
		return;
	}
	cgcVoices.read = qtrue;
	cgcVoices.count = 0;
	count = trap->FS_GetFileList( "ext_data/NPCs", ".npc", files, sizeof( files ) );
	for ( i = 0, p = files; i < count && *p; i++, p += strlen( p ) + 1 ) {
		CG_RPC_ReadVoiceFile( va( "ext_data/NPCs/%s", p ) );
	}
	CG_VR_Log( "companions: voices read, %i models' and folders' from %i NPC files\n", cgcVoices.count, count );
}

// A model's sound folder, as the game finds a player's (CG_LoadCISounds):
// its skin's own sounds_<skin>.cfg (not a custom character's parts), else
// its sounds.cfg (the default skin's: else its sounds_default.cfg) - the
// folder its first line names; a woman's voice where its last line has an
// "f" (the game's way: taken out of it). qfalse: none of them here.
static qboolean CG_RPC_SoundsCfg( const char *model, const char *skin, char *folder, int size, qboolean *female ) {
	char text[MAX_QPATH];
	fileHandle_t f = 0;
	int len = 0, i;

	*female = qfalse;
	folder[0] = '\0';
	if ( !skin[0] || !Q_stricmp( skin, "default" ) ) {
		len = trap->FS_Open( va( "models/players/%s/sounds.cfg", model ), &f, FS_READ );
		if ( !f ) {
			len = trap->FS_Open( va( "models/players/%s/sounds_default.cfg", model ), &f, FS_READ );
		}
	} else {
		if ( !strchr( skin, '|' ) ) {
			len = trap->FS_Open( va( "models/players/%s/sounds_%s.cfg", model, skin ), &f, FS_READ );
		}
		if ( !f ) {
			len = trap->FS_Open( va( "models/players/%s/sounds.cfg", model ), &f, FS_READ );
		}
	}
	if ( !f ) {
		return qfalse;
	}
	len = ( len < 0 ) ? 0 : ( ( len >= (int)sizeof( text ) ) ? (int)sizeof( text ) - 1 : len );
	trap->FS_Read( text, len, f );
	trap->FS_Close( f );
	text[len] = '\0';
	for ( i = len; i >= 0 && text[i] != '\n'; i-- ) {
		if ( text[i] == 'f' ) {
			*female = qtrue;
			text[i] = '\0';
		}
	}
	for ( i = 0; text[i] && text[i] != '\r' && text[i] != '\n'; i++ ) {
	}
	text[i] = '\0';
	Q_strncpyz( folder, text, size );
	return qtrue;
}

// A companion's voice, as it's drawn: in a look of its own on a model not
// its type's (drawnLook: CG_RP_CompModel's), as that look's character
// speaks - its sound sets set here, qtrue; else qfalse - as its type comes
// (CG_HandleNPCSounds).
qboolean CG_RP_CompSounds( centity_t *cent, const char *drawnLook ) {
	clientInfo_t *ci = cent->npcClient;
	char model[MAX_QPATH], skin[MAX_QPATH], folder[MAX_QPATH];
	const char *lines[4];
	const cgcVoice_t *v;
	qboolean female, cfg;
	int set, i;

	if ( !ci || !drawnLook || !drawnLook[0] ) {
		return qfalse;
	}
	BG_VRCompLookSplit( drawnLook, model, sizeof( model ), skin, sizeof( skin ) );
	if ( !model[0] ) {
		return qfalse;
	}
	CG_RPC_ReadVoices();
	cfg = CG_RPC_SoundsCfg( model, skin, folder, sizeof( folder ), &female );
	v = CG_RPC_VoiceOf( model, qfalse );
	if ( !v ) {
		v = CG_RPC_VoiceOf( folder, qtrue );
	}
	if ( !folder[0] ) {
		Q_strncpyz( folder, ( v && v->snd[0][0] ) ? v->snd[0] : model, sizeof( folder ) );
	}

	// what a player of it says
	CG_RegisterNPCSoundSet( ci, 1, folder );
	for ( i = 0; i < MAX_CUSTOM_SOUNDS && cg_customSoundNames[i]; i++ ) {
		if ( !ci->sounds[i] ) {
			char name[MAX_QPATH];
			Q_strncpyz( name, cg_customSoundNames[i] + 1, sizeof( name ) );
			COM_StripExtension( name, name, sizeof( name ) );
			ci->sounds[i] = trap->S_RegisterSound( va( "sound/chars/%s/misc/%s", female ? "mp_generic_female" : "mp_generic_male", name ) );
		}
	}

	// what an NPC of it says
	lines[0] = folder;
	for ( set = 1; set < 4; set++ ) {
		lines[set] = ( v && v->snd[set][0] ) ? v->snd[set] : folder;
		CG_RegisterNPCSoundSet( ci, set + 1, lines[set] );
	}
	CG_VR_Log( "companion %i: in its look (%s), its voice %s%s (%s), in a fight %s, the rest %s, a Jedi's %s\n", cent->currentState.number,
		model, folder, female ? ", a woman's" : "", cfg ? "its sounds.cfg" : ( v ? "its NPC's" : "its model's name" ), lines[1], lines[2], lines[3] );
	return qtrue;
}

/*
================================================================================
Every frame, starting and stopping
================================================================================
*/

void CG_RP_CompFrame( void ) {
	int i;

	for ( i = 0; i < RP_COMP_MAX; i++ ) {
		CG_RPC_UIWait( i );	// (the page shows a wait running down)
	}
}

void CG_RP_CompInit( void ) {
	const int gen = cgc.gen;	// (it counts on: the page, still up, tells a list from one it's seen)
	int i;

	memset( &cgc, 0, sizeof( cgc ) );
	cgc.gen = gen;
	memset( cgCompsOf, 0, sizeof( cgCompsOf ) );
	memset( &cgcLooks, 0, sizeof( cgcLooks ) );
	cgcNumModels = 0;
	cgc.count = -1;
	for ( i = 0; i < RP_COMP_MAX; i++ ) {
		cgc.comp[i].waitShown = -1;
	}
	CG_RPC_UIAll();
	trap->Cvar_Set( "ui_rpCompEditName", "" );
}
