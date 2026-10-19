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
the model's configstring ("<entity>\<name>\<look>", '\' between; the
look "" but for those: its model's configstring has the default skin). A
companion you look at shows its card (cg_vrrp.c: its name over its head,
whose it is, its side); the wheel's Companions ring (cg_vrpose.c) gives
your orders, greyed with none of yours out; one getting you up (rpup:
cg_vrdown.c) goes by its name.

STAND-INS. A companion's model this PC hasn't got, or one not on the
humanoid skeleton (another skeleton wants animations of its own, which
may not be here either), is drawn as a stand-in for its kind - a Jedi, a
trooper, anyone else - and never not at all (cg_players.c:
CG_G2AnimEntModelLoad). A look told beside (above) is drawn in its skin.

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

static struct {
	char		model[MAX_QPATH];
	qboolean	ok;
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
		trap->SendClientCommand( va( "rpcomp order %s", rpcOrders[order] ) );
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
		trap->SendClientCommand( va( "rpcomp set %i \"%s\" \"%s\" \"%s\" %i %i %i", slot + 1, name, type, look, rgb[0], rgb[1], rgb[2] ) );
	} else if ( !Q_stricmp( what, "del" ) ) {
		if ( slot < cgc.count ) {
			trap->SendClientCommand( va( "rpcomp del %i", slot + 1 ) );
		}
	} else if ( !Q_stricmp( what, "out" ) ) {
		if ( slot < cgc.count ) {
			trap->SendClientCommand( va( "rpcomp out %i %i", slot + 1, atoi( CG_Argv( 4 ) ) ? 1 : 0 ) );
		}
	}
}

/*
================================================================================
Stand-ins (cg_players.c: CG_G2AnimEntModelLoad)
================================================================================
*/

// Whether a model's here, on the humanoid skeleton (as its model.glm's first
// bytes say: bg_vrcomp.c; kept - asked once).
static qboolean CG_RPC_ModelOk( const char *path ) {
	byte head[BG_VRCOMP_GLM_HEAD];
	fileHandle_t f = 0;
	int i, len;
	qboolean ok = qfalse;

	for ( i = 0; i < cgcNumModels; i++ ) {
		if ( !Q_stricmp( cgcModels[i].model, path ) ) {
			return cgcModels[i].ok;
		}
	}
	len = trap->FS_Open( path, &f, FS_READ );
	if ( f ) {
		if ( len >= (int)sizeof( head ) && trap->FS_Read( head, sizeof( head ), f ) == (int)sizeof( head ) ) {
			ok = BG_VRCompGlmHumanoid( head, sizeof( head ) );
		}
		trap->FS_Close( f );
	}
	if ( cgcNumModels < RPC_MODELS ) {
		Q_strncpyz( cgcModels[cgcNumModels].model, path, sizeof( cgcModels[0].model ) );
		cgcModels[cgcNumModels].ok = ok;
		cgcNumModels++;
	}
	return ok;
}

// A companion's model, as CG_G2AnimEntModelLoad has it ("models/players/<model>/model.glm",
// its skin taken off): if it won't do here, a stand-in for its kind instead, and its skin.
int CG_RP_CompModel( const centity_t *cent, char *modelName, int size, int skinID ) {
	const char *standIn, *told = CG_RPC_ToldLook( cent->currentState.number );
	char path[MAX_QPATH];

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
	cgcNumModels = 0;
	cgc.count = -1;
	for ( i = 0; i < RP_COMP_MAX; i++ ) {
		cgc.comp[i].waitShown = -1;
	}
	CG_RPC_UIAll();
	trap->Cvar_Set( "ui_rpCompEditName", "" );
}
