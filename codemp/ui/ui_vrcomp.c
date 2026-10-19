/*
===========================================================================
OpenJK VR (multiplayer) - the Companions menu (ui/vr_comp.menu): the
companions of the character you play, up to RP_COMP_MAX - made, changed and
deleted here - and which of them come out with you (RP_COMP_OUT_MAX at
most).

Opened from the Profile menu (its COMPANIONS button, put on it here, where
the server has roleplay) or the Roleplay menu's Editor page. The game
(cg_vrcomp.c) keeps them in ui_rpComp... cvars as the server tells it
(ui_rpComps: how many, -1 with no character played; ui_rpCompName<n>, Type,
Look, Color, Out, Wait; ui_rpCompsGen: how many times it was told), and
sends what the page asks: "rpui comp save|del|out <slot> ...".

  the list     your companions (feeder 68) and which come out: New,
               Change, Delete (asked first), Comes out (a tick for the one
               picked); the one picked shown in the preview
  making one   its name; what it fights as (feeder 69: the game's NPC types
               of people - bg_vrcomp.c - with a box to find one); its look:
               as its type comes, a player model (feeder 70: those the
               Profile shows, on the humanoid skeleton), or a custom
               character (species, colour, head, torso, legs: the Profile's
               own lists, feeders 19 to 23, on the ui_char_ cvars - put
               back as the player had them when the menu shuts); Save waits
               for the server's word (back to the list once it's taken it;
               its message says why if not), Cancel
  the preview  the companion picked, or being made, as it will look

Where it can't be used, it says why: no roleplay on the server, roleplay
off, no companions there, not logged in, no character played (and, for the
last two, the way to the Roleplay menu).

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "ui_local.h"

extern qboolean	ItemParse_asset_model_go( itemDef_t *item, const char *name, int *runTimeLength );
extern qboolean	ItemParse_model_g2skin_go( itemDef_t *item, const char *skinName );
extern void		UI_ClearG2Pointer( void *ghoul2 );
void			UI_ParseMenu( const char *menuFile );

#define VRC_FILE			"ui/vr_comp.menu"
#define VRC_MENU			"vr_comp"
#define VRC_PROFILE			"ingame_player"
#define VRC_PROFILE_ITEMS	"vr_comp_profile"	// its items: on the Profile menu
#define VRC_PROFILE_ITEM	"vr_comp_profilebutton"
#define VRC_TYPES_MAX		1024	// NPC types read (each name's first), at most
#define VRC_NPC_FILE_MAX	65536	// a file of them read (more of one: cut there)
#define VRC_MODELS_MAX		256		// player models asked whether they're on the humanoid skeleton, kept
#define VRC_SAVE_WAIT		5000	// Save: the server's word waited for (ms)
#define VRC_MESSAGE_TIME	10000	// a message (ui_rpMessage) shown this long (ms)
#define VRC_SAVING			"Saving..."

enum { VRC_LIST, VRC_EDIT, VRC_OWN_EDIT };			// the page's parts: the list, making one, its custom look
enum { VRC_AS_IT_COMES, VRC_MODEL, VRC_OWN };		// a look: as its type comes, a player model, a custom character
enum { OWN_MODEL, OWN_HEAD, OWN_TORSO, OWN_LEGS, OWN_PARTS };

static const char *vrcLookWords[] = { "As it comes", "A model", "Custom" };

// what an NPC fights with (its file's "weapon"), as the list says it
static const struct { const char *name, *word; } vrcWeapons[] = {
	{ "WP_SABER", "saber" }, { "WP_STUN_BATON", "baton" }, { "WP_MELEE", "fists" }, { "WP_BRYAR_PISTOL", "pistol" },
	{ "WP_BRYAR_OLD", "pistol" }, { "WP_BLASTER", "blaster" }, { "WP_DISRUPTOR", "disruptor" }, { "WP_BOWCASTER", "bowcaster" },
	{ "WP_REPEATER", "repeater" }, { "WP_DEMP2", "DEMP 2" }, { "WP_FLECHETTE", "flechette" }, { "WP_ROCKET_LAUNCHER", "rockets" },
	{ "WP_THERMAL", "thermals" }, { "WP_TRIP_MINE", "mines" }, { "WP_DET_PACK", "det packs" }, { "WP_CONCUSSION", "concussion" },
};

// The ui_char_ cvars the Profile's custom character is made on (the custom
// look's here): as the player had them, put back as the menu shuts.
static const char *vrcCharCvars[] = { "ui_char_model", "ui_char_skin_head", "ui_char_skin_torso", "ui_char_skin_legs",
	"ui_char_color_red", "ui_char_color_green", "ui_char_color_blue" };

// What a companion can fight as: the NPC types the game's files define
// (ext_data/NPCs/*.npc, as the server reads them: each name's first, in the
// files' order), each with its class, weapon and look as it comes; those a
// companion may be (usable: a person's class, a model on the humanoid
// skeleton), and those of them the find box matches.
typedef struct {
	char		name[RP_COMP_TYPE_SIZE];
	qboolean	usable;
	const char	*kind;			// its class, as the page says it (bg_vrcomp.c)
	const char	*weapon;
	char		model[MAX_QPATH];
	char		skin[MAX_QPATH];
} vrcType_t;

static struct {
	qboolean	read;
	int			count;
	vrcType_t	list[VRC_TYPES_MAX];
	int			numUsable;
	int			numShown;
	short		shown[VRC_TYPES_MAX];
	qboolean	filtered;					// shown[] is for find...
	char		find[MAX_CVAR_VALUE_STRING];	// ...as typed then
} vrcTypes;

// The models a companion can look like: those the Profile lists
// (uiInfo.q3HeadNames: "model/skin", each with its picture) on the humanoid
// skeleton.
static struct {
	qboolean	built;
	int			count;
	short		head[MAX_Q3PLAYERMODELS];
} vrcLooks;

static struct {
	char		model[MAX_QPATH];
	qboolean	ok;
} vrcModels[VRC_MODELS_MAX];
static int vrcNumModels;

static struct {
	int			part;			// VRC_LIST...
	int			from;			// where Back goes: 0 the Profile, 1 the Roleplay menu
	int			picked;			// the companion picked in the list (-1: there are none)
	int			deleting;		// asking whether to delete this one (-1: not)
	// the one being made
	int			slot;			// (a new one: the next)
	char		editFor[MAX_CVAR_VALUE_STRING];	// the character it's for (another played: dropped)
	char		type[RP_COMP_TYPE_SIZE];	// what it fights as ("": not chosen yet)
	int			kind;			// its look's: VRC_AS_IT_COMES...
	char		model[MAX_QPATH];	// a model look: "model/skin"
	char		own[OWN_PARTS][MAX_QPATH];	// a custom look: species, head, torso, legs...
	int			ownRgb[3];		// ...and its colour
	int			bodyPart;		// the custom look's list shown: head, torso, legs
	qboolean	saving;			// Save sent, and the server's word not come yet (Save waits)
	qboolean	saveSent;		// Save sent: when the server tells the list again with it there (it's taken it), back to the list...
	int			saveGen;		// ...the lists told then (ui_rpCompsGen)...
	int			saveTime;		// ...for this long at most...
	char		saveLook[MAX_QPATH];	// ...the look sent (and vrc.type)...
	char		saveName[MAX_CVAR_VALUE_STRING];	// ...and its name
	// the player's
	char		kept[ARRAY_LEN( vrcCharCvars )][MAX_QPATH];
	qboolean	keptValid;
	// shown
	char		shown[MAX_QPATH * 2];	// what the preview has ("": nothing)
	char		message[MAX_CVAR_VALUE_STRING];
	int			messageTime;
} vrc = { VRC_LIST, 0, -1, -1 };

/*
--------------------------------------------------------------------------------
Small things
--------------------------------------------------------------------------------
*/

static void UI_VRC_Exec( const char *text ) {
	trap->Cmd_ExecuteText( EXEC_APPEND, text );
}

static int UI_VRC_Int( const char *cvar ) {
	return (int)trap->Cvar_VariableValue( cvar );
}

static void UI_VRC_Str( const char *cvar, char *out, int size ) {
	trap->Cvar_VariableStringBuffer( cvar, out, size );
}

// A cvar set, if it isn't so already.
static void UI_VRC_Set( const char *cvar, const char *value ) {
	char now[MAX_CVAR_VALUE_STRING];

	UI_VRC_Str( cvar, now, sizeof( now ) );
	if ( strcmp( now, value ) ) {
		trap->Cvar_Set( cvar, value );
	}
}

static menuDef_t *UI_VRC_Menu( void ) {
	return Menus_FindByName( VRC_MENU );
}

// How many companions the character played has (-1: none played, or not told yet).
static int UI_VRC_Count( void ) {
	return Com_Clampi( -1, RP_COMP_MAX, UI_VRC_Int( "ui_rpComps" ) );
}

// One of them, as the game has it (ui_rpComp<what><slot>).
static void UI_VRC_Comp( int slot, const char *what, char *out, int size ) {
	UI_VRC_Str( va( "ui_rpComp%s%i", what, slot ), out, size );
}

// Its colour: "r g b" (white where it isn't one).
static void UI_VRC_CompRgb( int slot, int rgb[3] ) {
	char text[64];
	int k;

	UI_VRC_Comp( slot, "Color", text, sizeof( text ) );
	rgb[0] = rgb[1] = rgb[2] = 255;
	if ( sscanf( text, "%d %d %d", &rgb[0], &rgb[1], &rgb[2] ) != 3 ) {
		rgb[0] = rgb[1] = rgb[2] = 255;
	}
	for ( k = 0; k < 3; k++ ) {
		rgb[k] = Com_Clampi( 0, 255, rgb[k] );
	}
}

// A look's kind: "" as its type comes, a custom character's three parts, or a model.
static int UI_VRC_KindOf( const char *look ) {
	return !look[0] ? VRC_AS_IT_COMES : strchr( look, '|' ) ? VRC_OWN : VRC_MODEL;
}

// The server's companions: how many come out with a player (0: none, or a
// server older than them), and whether they may look as players make them
// (its serverinfo's "vrRP": g_vrrp.c); and its game type.
static void UI_VRC_Server( int *most, qboolean *looks, int *gametype ) {
	char info[MAX_INFO_STRING];
	int on, chars, range, downed, comps, compsMax, models;

	info[0] = '\0';
	trap->GetConfigString( CS_SERVERINFO, info, sizeof( info ) );
	*gametype = atoi( Info_ValueForKey( info, "g_gametype" ) );
	if ( sscanf( Info_ValueForKey( info, "vrRP" ), "%d %d %d %d %d %d %d", &on, &chars, &range, &downed, &comps, &compsMax, &models ) == 7 ) {
		*most = Com_Clampi( 0, RP_COMP_OUT_MAX, comps );
		*looks = (qboolean)( models != 0 );
		return;
	}
	*most = 0;
	*looks = qfalse;
}

// Why the page can't be used here (NULL: it can), and a second line; toRoleplay:
// the Roleplay menu is where to go.
static const char *UI_VRC_Why( const char **second, qboolean *toRoleplay ) {
	const int server = Com_Clampi( 0, 3, UI_VRC_Int( "ui_rpServer" ) );
	char account[64], playing[MAX_CVAR_VALUE_STRING];
	int most, gametype;
	qboolean looks;

	*second = "";
	*toRoleplay = qfalse;
	if ( server == 0 ) {
		return "This server doesn't have roleplay, so there are no companions here.";
	}
	if ( server == 3 ) {
		return "Roleplay isn't working on this server right now.";
	}
	if ( server == 1 ) {
		*second = "Its owner can turn it on, on the Roleplay menu's Server page.";
		return "Roleplay is off on this server right now: no companions.";
	}
	UI_VRC_Server( &most, &looks, &gametype );
	if ( most <= 0 ) {
		*second = "Its owner can let them come, on the Roleplay menu's Server page.";
		return "This server doesn't have companions.";
	}
	UI_VRC_Str( "ui_rpAccount", account, sizeof( account ) );
	UI_VRC_Str( "ui_rpPlaying", playing, sizeof( playing ) );
	if ( !account[0] ) {
		*toRoleplay = qtrue;
		*second = "Companions are a character's: log in, and play one of yours.";
		return "Log in to your roleplay account first.";
	}
	if ( !playing[0] || UI_VRC_Count() < 0 ) {
		*toRoleplay = qtrue;
		*second = "Companions are a character's: each of yours has its own.";
		return "Play one of your characters first.";
	}
	return NULL;
}

/*
--------------------------------------------------------------------------------
What a companion can fight as, and look like
--------------------------------------------------------------------------------
*/

// Whether a player model's on the humanoid skeleton every companion's on
// (its model.glm's first bytes name the skeleton: bg_vrcomp.c - the server
// asks the same of a companion's model, g_vrcomp.c, and refuses another),
// asked once.
static qboolean UI_VRC_Humanoid( const char *model ) {
	char path[MAX_QPATH];
	byte head[BG_VRCOMP_GLM_HEAD];
	fileHandle_t f = 0;
	int i, len;
	qboolean ok = qfalse;

	for ( i = 0; i < vrcNumModels; i++ ) {
		if ( !Q_stricmp( vrcModels[i].model, model ) ) {
			return vrcModels[i].ok;
		}
	}
	if ( model[0] && !strchr( model, '.' ) && !strchr( model, ':' ) && !strchr( model, '\\' ) && !strchr( model, '/' ) && strlen( model ) <= 32 ) {
		Com_sprintf( path, sizeof( path ), "models/players/%s/model.glm", model );
		len = trap->FS_Open( path, &f, FS_READ );
		if ( f ) {
			if ( len >= (int)sizeof( head ) && trap->FS_Read( head, sizeof( head ), f ) == (int)sizeof( head ) ) {
				ok = BG_VRCompGlmHumanoid( head, sizeof( head ) );
			}
			trap->FS_Close( f );
		}
	}
	if ( vrcNumModels < VRC_MODELS_MAX ) {
		Q_strncpyz( vrcModels[vrcNumModels].model, model, sizeof( vrcModels[0].model ) );
		vrcModels[vrcNumModels].ok = ok;
		vrcNumModels++;
	}
	return ok;
}

static const char *UI_VRC_WeaponWord( const char *weapon ) {
	int i;

	for ( i = 0; i < (int)ARRAY_LEN( vrcWeapons ); i++ ) {
		if ( !Q_stricmp( weapon, vrcWeapons[i].name ) ) {
			return vrcWeapons[i].word;
		}
	}
	return "";
}

// The NPC types a file of them defines, into the list (as the server reads
// one: its name, then a braced block - "class", "playerModel", "customSkin",
// "weapon" on its lines, inner blocks skipped).
static void UI_VRC_ReadTypeFile( const char *path ) {
	static char text[VRC_NPC_FILE_MAX];
	fileHandle_t f = 0;
	const char *p;
	int len, i;

	len = trap->FS_Open( path, &f, FS_READ );
	if ( !f ) {
		return;
	}
	if ( len <= 0 ) {
		trap->FS_Close( f );
		return;
	}
	if ( len >= (int)sizeof( text ) ) {
		len = (int)sizeof( text ) - 1;	// (one this big: what's past it, left out)
	}
	trap->FS_Read( text, len, f );
	trap->FS_Close( f );
	text[len] = '\0';
	p = text;
	COM_BeginParseSession( path );
	while ( p && vrcTypes.count < VRC_TYPES_MAX ) {
		vrcType_t *t = &vrcTypes.list[vrcTypes.count];
		char cls[64], weapon[64];
		const char *tok = COM_ParseExt( &p, qtrue );
		qboolean ok, ended = qfalse;

		if ( !tok[0] ) {
			break;
		}
		memset( t, 0, sizeof( *t ) );
		ok = BG_VRCompTypeOk( tok );
		Q_strncpyz( t->name, tok, sizeof( t->name ) );
		Q_strncpyz( t->skin, "default", sizeof( t->skin ) );
		cls[0] = weapon[0] = '\0';
		tok = COM_ParseExt( &p, qtrue );
		if ( strcmp( tok, "{" ) ) {
			break;	// (not a block: the rest's broken)
		}
		while ( p ) {
			tok = COM_ParseExt( &p, qtrue );
			if ( !tok[0] ) {
				break;
			}
			if ( !strcmp( tok, "}" ) ) {
				ended = qtrue;
				break;
			}
			if ( !strcmp( tok, "{" ) ) {
				if ( !SkipBracedSection( &p, 1 ) ) {
					break;
				}
			} else if ( !Q_stricmp( tok, "class" ) ) {
				Q_strncpyz( cls, COM_ParseExt( &p, qfalse ), sizeof( cls ) );
			} else if ( !Q_stricmp( tok, "playerModel" ) ) {
				Q_strncpyz( t->model, COM_ParseExt( &p, qfalse ), sizeof( t->model ) );
			} else if ( !Q_stricmp( tok, "customSkin" ) ) {
				Q_strncpyz( t->skin, COM_ParseExt( &p, qfalse ), sizeof( t->skin ) );
			} else if ( !Q_stricmp( tok, "weapon" ) ) {
				Q_strncpyz( weapon, COM_ParseExt( &p, qfalse ), sizeof( weapon ) );
			} else {
				SkipRestOfLine( &p );
			}
		}
		if ( !ended ) {
			break;	// (its block never ends: the rest's broken)
		}
		if ( !ok ) {
			continue;
		}
		for ( i = 0; i < vrcTypes.count; i++ ) {
			if ( !Q_stricmp( vrcTypes.list[i].name, t->name ) ) {
				break;	// (the first of a name is the one spawned)
			}
		}
		if ( i < vrcTypes.count ) {
			continue;
		}
		t->kind = BG_VRCompClassWord( cls );
		t->weapon = UI_VRC_WeaponWord( weapon );
		t->usable = (qboolean)( t->kind && t->model[0] && BG_VRCompLookOk( va( "%s/%s", t->model, t->skin[0] ? t->skin : "default" ) ) &&
			UI_VRC_Humanoid( t->model ) );	// (its model and skin as the server takes them: g_vrcomp.c)
		vrcTypes.count++;
	}
}

static int QDECL UI_VRC_TypeOrder( const void *a, const void *b ) {
	const vrcType_t *x = (const vrcType_t *)a, *y = (const vrcType_t *)b;
	int d;

	if ( x->usable != y->usable ) {
		return x->usable ? -1 : 1;
	}
	if ( !x->usable ) {
		return 0;
	}
	d = Q_stricmp( x->kind, y->kind );	// (by kind: the Jedi together, the troopers...)
	return d ? d : Q_stricmp( x->name, y->name );
}

// The game's NPC types, read once (a menu set loaded again keeps them):
// those a companion may be first, by kind.
static void UI_VRC_ReadTypes( void ) {
	static char files[16384];
	int i, count;
	const char *p;

	if ( vrcTypes.read ) {
		return;
	}
	vrcTypes.read = qtrue;
	vrcTypes.count = vrcTypes.numUsable = 0;
	count = trap->FS_GetFileList( "ext_data/NPCs", ".npc", files, sizeof( files ) );
	for ( i = 0, p = files; i < count && *p; i++, p += strlen( p ) + 1 ) {
		UI_VRC_ReadTypeFile( va( "ext_data/NPCs/%s", p ) );
	}
	for ( i = 0; i < vrcTypes.count; i++ ) {
		vrcTypes.numUsable += vrcTypes.list[i].usable ? 1 : 0;
	}
	qsort( vrcTypes.list, vrcTypes.count, sizeof( vrcTypes.list[0] ), UI_VRC_TypeOrder );
	vrcTypes.filtered = qfalse;
}

// Those the find box (ui_rpCompFind) matches - by name or kind - as the list shows them: how many.
static int UI_VRC_Types( void ) {
	char find[MAX_CVAR_VALUE_STRING];
	int i;

	UI_VRC_ReadTypes();
	UI_VRC_Str( "ui_rpCompFind", find, sizeof( find ) );
	if ( vrcTypes.filtered && !strcmp( find, vrcTypes.find ) ) {
		return vrcTypes.numShown;
	}
	Q_strncpyz( vrcTypes.find, find, sizeof( vrcTypes.find ) );
	vrcTypes.filtered = qtrue;
	vrcTypes.numShown = 0;
	for ( i = 0; i < vrcTypes.numUsable; i++ ) {
		const vrcType_t *t = &vrcTypes.list[i];
		if ( !find[0] || Q_stristr( t->name, find ) || Q_stristr( t->kind, find ) ) {
			vrcTypes.shown[vrcTypes.numShown++] = (short)i;
		}
	}
	return vrcTypes.numShown;
}

// A type a companion may be, by its name (NULL: there's none such here).
static const vrcType_t *UI_VRC_Type( const char *name ) {
	int i;

	UI_VRC_ReadTypes();
	for ( i = 0; name[0] && i < vrcTypes.numUsable; i++ ) {
		if ( !Q_stricmp( vrcTypes.list[i].name, name ) ) {
			return &vrcTypes.list[i];
		}
	}
	return NULL;
}

// Where a type is in the list as shown (-1: it isn't).
static int UI_VRC_TypeShown( const char *name ) {
	const int count = UI_VRC_Types();
	int i;

	for ( i = 0; name[0] && i < count; i++ ) {
		if ( !Q_stricmp( vrcTypes.list[vrcTypes.shown[i]].name, name ) ) {
			return i;
		}
	}
	return -1;
}

// The models the Profile lists that a companion can look like (once
// they're listed: the menus' list is made after they're read in).
static void UI_VRC_Looks( void ) {
	int i;

	if ( vrcLooks.built || uiInfo.q3HeadCount <= 0 ) {
		return;
	}
	vrcLooks.built = qtrue;
	vrcLooks.count = 0;
	for ( i = 0; i < uiInfo.q3HeadCount && i < MAX_Q3PLAYERMODELS; i++ ) {
		char model[MAX_QPATH], skin[MAX_QPATH];
		if ( !BG_VRCompLookOk( uiInfo.q3HeadNames[i] ) ) {
			continue;
		}
		BG_VRCompLookSplit( uiInfo.q3HeadNames[i], model, sizeof( model ), skin, sizeof( skin ) );
		if ( UI_VRC_Humanoid( model ) ) {
			vrcLooks.head[vrcLooks.count++] = (short)i;
		}
	}
}

// A model look as the list writes it: "model/skin" ("model": its default skin).
static void UI_VRC_ModelLook( const char *look, char *out, int size ) {
	if ( look[0] && !strchr( look, '/' ) ) {
		Com_sprintf( out, size, "%s/default", look );
	} else {
		Q_strncpyz( out, look, size );
	}
}

// Where a model look is in the list (-1: it isn't).
static int UI_VRC_LookShown( const char *look ) {
	char want[MAX_QPATH];
	int i;

	UI_VRC_Looks();
	UI_VRC_ModelLook( look, want, sizeof( want ) );
	for ( i = 0; want[0] && i < vrcLooks.count; i++ ) {
		if ( !Q_stricmp( uiInfo.q3HeadNames[vrcLooks.head[i]], want ) ) {
			return i;
		}
	}
	return -1;
}

/*
--------------------------------------------------------------------------------
The lists
--------------------------------------------------------------------------------
*/

int UI_VRComp_FeederCount( int feeder ) {
	if ( feeder == FEEDER_VR_RPCOMPS ) {
		return Q_max( 0, UI_VRC_Count() );
	}
	if ( feeder == FEEDER_VR_RPCOMPTYPES ) {
		return UI_VRC_Types();
	}
	if ( feeder == FEEDER_VR_RPCOMPLOOKS ) {
		UI_VRC_Looks();
		return vrcLooks.count;
	}
	return 0;
}

const char *UI_VRComp_FeederText( int feeder, int index, int column ) {
	static char text[MAX_CVAR_VALUE_STRING];

	text[0] = '\0';
	if ( feeder == FEEDER_VR_RPCOMPS && index >= 0 && index < UI_VRC_Count() ) {
		char type[RP_COMP_TYPE_SIZE];
		const vrcType_t *t;
		switch ( column ) {
		case 0:
			UI_VRC_Comp( index, "Name", text, sizeof( text ) );
			break;
		case 1:
			UI_VRC_Comp( index, "Type", type, sizeof( type ) );
			t = UI_VRC_Type( type );
			Q_strncpyz( text, t ? t->kind : type, sizeof( text ) );
			break;
		default:
			UI_VRC_Comp( index, "Wait", text, sizeof( text ) );
			if ( text[0] ) {
				Q_strncpyz( text, "Knocked out", sizeof( text ) );
			} else {
				UI_VRC_Comp( index, "Out", text, sizeof( text ) );
				Q_strncpyz( text, atoi( text ) ? "Comes out" : "", sizeof( text ) );
			}
			break;
		}
	} else if ( feeder == FEEDER_VR_RPCOMPTYPES && index >= 0 && index < UI_VRC_Types() ) {
		const vrcType_t *t = &vrcTypes.list[vrcTypes.shown[index]];
		Q_strncpyz( text, column == 0 ? t->name : column == 1 ? t->kind : t->weapon, sizeof( text ) );
	} else if ( feeder == FEEDER_VR_RPCOMPLOOKS && index >= 0 && index < UI_VRComp_FeederCount( feeder ) ) {
		Q_strncpyz( text, uiInfo.q3HeadNames[vrcLooks.head[index]], sizeof( text ) );
	}
	return text;
}

// A model's picture, as the Profile's (models/players/<model>/icon_<skin>; kept with its).
qhandle_t UI_VRComp_FeederImage( int feeder, int index ) {
	int head;

	if ( feeder != FEEDER_VR_RPCOMPLOOKS || index < 0 || index >= UI_VRComp_FeederCount( feeder ) ) {
		return 0;
	}
	head = vrcLooks.head[index];
	if ( !uiInfo.q3HeadIcons[head] ) {
		char model[MAX_QPATH], skin[MAX_QPATH];
		BG_VRCompLookSplit( uiInfo.q3HeadNames[head], model, sizeof( model ), skin, sizeof( skin ) );
		uiInfo.q3HeadIcons[head] = trap->R_RegisterShaderNoMip( va( "models/players/%s/icon_%s", model, skin ) );
	}
	return uiInfo.q3HeadIcons[head];
}

// A row picked (qfalse: there's no such row - a click past the last - and the list stays as it was).
qboolean UI_VRComp_FeederSelection( int feeder, int index ) {
	if ( index < 0 || index >= UI_VRComp_FeederCount( feeder ) ) {
		return qfalse;
	}
	if ( feeder == FEEDER_VR_RPCOMPS ) {
		vrc.picked = index;
		vrc.deleting = -1;	// (another picked: not that one)
		return qtrue;
	}
	if ( feeder == FEEDER_VR_RPCOMPTYPES ) {
		Q_strncpyz( vrc.type, vrcTypes.list[vrcTypes.shown[index]].name, sizeof( vrc.type ) );
		return qtrue;
	}
	if ( feeder == FEEDER_VR_RPCOMPLOOKS ) {
		Q_strncpyz( vrc.model, uiInfo.q3HeadNames[vrcLooks.head[index]], sizeof( vrc.model ) );
		vrc.kind = VRC_MODEL;
		return qtrue;
	}
	return qfalse;
}

// A list's row lit (-1: none - the one chosen isn't in it as shown).
static void UI_VRC_Light( menuDef_t *menu, const char *name, int index ) {
	itemDef_t *list = Menu_FindItemByName( menu, name );

	if ( list && list->type == ITEM_TYPE_LISTBOX ) {
		list->cursorPos = index;
	}
}

// ...and scrolled to.
static void UI_VRC_ScrollTo( menuDef_t *menu, const char *name, int index ) {
	itemDef_t *list = Menu_FindItemByName( menu, name );
	listBoxDef_t *lb;
	int shown;

	if ( !list || list->type != ITEM_TYPE_LISTBOX || !list->typeData.listbox ) {
		return;
	}
	lb = list->typeData.listbox;
	list->cursorPos = index;
	if ( list->window.flags & WINDOW_HORIZONTAL ) {
		shown = ( lb->elementWidth > 0 ) ? (int)( ( list->window.rect.w - 2 ) / lb->elementWidth ) : 1;
	} else {
		shown = ( lb->elementHeight > 0 ) ? (int)( ( list->window.rect.h - 2 ) / lb->elementHeight ) : 1;
	}
	shown = Q_max( 1, shown );
	if ( index < 0 ) {
		lb->startPos = 0;
	} else if ( index < lb->startPos ) {
		lb->startPos = index;
	} else if ( index >= lb->startPos + shown ) {
		lb->startPos = index - shown + 1;
	}
}

/*
--------------------------------------------------------------------------------
A custom look, on the Profile's lists (the ui_char_ cvars)
--------------------------------------------------------------------------------
*/

static int UI_VRC_Species( const char *name ) {
	int i;

	for ( i = 0; i < uiInfo.playerSpeciesCount; i++ ) {
		if ( !Q_stricmp( uiInfo.playerSpecies[i].Name, name ) ) {
			return i;
		}
	}
	return -1;
}

// Where a part is in one of a species' lists (-1: it isn't).
static int UI_VRC_Part( const skinName_t *list, int count, const char *name ) {
	int i;

	for ( i = 0; list && i < count; i++ ) {
		if ( !Q_stricmp( list[i].name, name ) ) {
			return i;
		}
	}
	return -1;
}

// A custom look made sure of: a species there is, and parts of its (the
// first where one isn't); qfalse: there are no species.
static qboolean UI_VRC_OwnSure( char own[OWN_PARTS][MAX_QPATH] ) {
	const playerSpeciesInfo_t *s;
	int species = UI_VRC_Species( own[OWN_MODEL] );

	if ( uiInfo.playerSpeciesCount <= 0 || !uiInfo.playerSpecies ) {
		return qfalse;
	}
	if ( species < 0 ) {
		species = 0;
		own[OWN_HEAD][0] = own[OWN_TORSO][0] = own[OWN_LEGS][0] = '\0';
	}
	s = &uiInfo.playerSpecies[species];
	Q_strncpyz( own[OWN_MODEL], s->Name, MAX_QPATH );
	if ( UI_VRC_Part( s->SkinHead, s->SkinHeadCount, own[OWN_HEAD] ) < 0 ) {
		Q_strncpyz( own[OWN_HEAD], s->SkinHeadCount > 0 ? s->SkinHead[0].name : "head_a1", MAX_QPATH );
	}
	if ( UI_VRC_Part( s->SkinTorso, s->SkinTorsoCount, own[OWN_TORSO] ) < 0 ) {
		Q_strncpyz( own[OWN_TORSO], s->SkinTorsoCount > 0 ? s->SkinTorso[0].name : "torso_a1", MAX_QPATH );
	}
	if ( UI_VRC_Part( s->SkinLeg, s->SkinLegCount, own[OWN_LEGS] ) < 0 ) {
		Q_strncpyz( own[OWN_LEGS], s->SkinLegCount > 0 ? s->SkinLeg[0].name : "lower_a1", MAX_QPATH );
	}
	return qtrue;
}

// A custom look ("model/head|torso|lower") into its parts (qfalse: it isn't one).
static qboolean UI_VRC_OwnFrom( const char *look, char own[OWN_PARTS][MAX_QPATH] ) {
	char model[MAX_QPATH], skin[MAX_QPATH];
	char *a, *b;

	if ( UI_VRC_KindOf( look ) != VRC_OWN ) {
		return qfalse;
	}
	BG_VRCompLookSplit( look, model, sizeof( model ), skin, sizeof( skin ) );
	a = strchr( skin, '|' );
	b = a ? strchr( a + 1, '|' ) : NULL;
	if ( !a || !b ) {
		return qfalse;
	}
	*a = *b = '\0';
	Q_strncpyz( own[OWN_MODEL], model, MAX_QPATH );
	Q_strncpyz( own[OWN_HEAD], skin, MAX_QPATH );
	Q_strncpyz( own[OWN_TORSO], a + 1, MAX_QPATH );
	Q_strncpyz( own[OWN_LEGS], b + 1, MAX_QPATH );
	return qtrue;
}

// The custom look onto the Profile's lists (ui_char_; the species', and its
// parts lit), to be changed there.
static void UI_VRC_OwnToLists( menuDef_t *menu ) {
	const playerSpeciesInfo_t *s;
	int species;

	if ( !UI_VRC_OwnSure( vrc.own ) ) {
		return;
	}
	species = Q_max( 0, UI_VRC_Species( vrc.own[OWN_MODEL] ) );
	s = &uiInfo.playerSpecies[species];
	uiInfo.playerSpeciesIndex = species;
	trap->Cvar_Set( "ui_char_model", vrc.own[OWN_MODEL] );
	trap->Cvar_Set( "ui_char_skin_head", vrc.own[OWN_HEAD] );
	trap->Cvar_Set( "ui_char_skin_torso", vrc.own[OWN_TORSO] );
	trap->Cvar_Set( "ui_char_skin_legs", vrc.own[OWN_LEGS] );
	trap->Cvar_Set( "ui_char_color_red", va( "%i", vrc.ownRgb[0] ) );
	trap->Cvar_Set( "ui_char_color_green", va( "%i", vrc.ownRgb[1] ) );
	trap->Cvar_Set( "ui_char_color_blue", va( "%i", vrc.ownRgb[2] ) );
	UI_VRC_ScrollTo( menu, "headlistbox", UI_VRC_Part( s->SkinHead, s->SkinHeadCount, vrc.own[OWN_HEAD] ) );
	UI_VRC_ScrollTo( menu, "torsolistbox", UI_VRC_Part( s->SkinTorso, s->SkinTorsoCount, vrc.own[OWN_TORSO] ) );
	UI_VRC_ScrollTo( menu, "lowerlistbox", UI_VRC_Part( s->SkinLeg, s->SkinLegCount, vrc.own[OWN_LEGS] ) );
	UI_VRC_ScrollTo( menu, "colorbox", -1 );	// (its colours are scripts: none lit till one's chosen)
}

// ...and back, as they've been changed.
static void UI_VRC_OwnFromLists( void ) {
	int k;

	UI_VRC_Str( "ui_char_model", vrc.own[OWN_MODEL], MAX_QPATH );
	UI_VRC_Str( "ui_char_skin_head", vrc.own[OWN_HEAD], MAX_QPATH );
	UI_VRC_Str( "ui_char_skin_torso", vrc.own[OWN_TORSO], MAX_QPATH );
	UI_VRC_Str( "ui_char_skin_legs", vrc.own[OWN_LEGS], MAX_QPATH );
	for ( k = 0; k < 3; k++ ) {
		vrc.ownRgb[k] = Com_Clampi( 0, 255, UI_VRC_Int( vrcCharCvars[4 + k] ) );
	}
}

// The preview's tint (the model item's: ui_char_color_*).
static void UI_VRC_Tint( const int rgb[3] ) {
	int k;

	for ( k = 0; k < 3; k++ ) {
		UI_VRC_Set( vrcCharCvars[4 + k], va( "%i", rgb[k] ) );
	}
}

/*
--------------------------------------------------------------------------------
The one being made
--------------------------------------------------------------------------------
*/

// Its look, as the server takes one ("": as its type comes), and its colour.
static void UI_VRC_EditLook( char *look, int size, int rgb[3] ) {
	rgb[0] = rgb[1] = rgb[2] = 255;
	look[0] = '\0';
	if ( vrc.kind == VRC_MODEL ) {
		Q_strncpyz( look, vrc.model, size );
	} else if ( vrc.kind == VRC_OWN ) {
		Com_sprintf( look, size, "%s/%s|%s|%s", vrc.own[OWN_MODEL], vrc.own[OWN_HEAD], vrc.own[OWN_TORSO], vrc.own[OWN_LEGS] );
		rgb[0] = vrc.ownRgb[0];
		rgb[1] = vrc.ownRgb[1];
		rgb[2] = vrc.ownRgb[2];
	}
}

// ...into the cvars the game sends it from (ui_rpCompEdit...: cg_vrcomp.c).
static void UI_VRC_EditCvars( void ) {
	char look[MAX_CVAR_VALUE_STRING];
	int rgb[3];

	UI_VRC_EditLook( look, sizeof( look ), rgb );
	UI_VRC_Set( "ui_rpCompEditType", vrc.type );
	UI_VRC_Set( "ui_rpCompEditLook", look );
	UI_VRC_Set( "ui_rpCompEditR", va( "%i", rgb[0] ) );
	UI_VRC_Set( "ui_rpCompEditG", va( "%i", rgb[1] ) );
	UI_VRC_Set( "ui_rpCompEditB", va( "%i", rgb[2] ) );
}

// Typing in one of the page's fields, stopped (it's going).
static void UI_VRC_StopTyping( void ) {
	const itemDef_t *typing = Item_TextField_Typing();

	if ( typing && typing->parent == (void *)UI_VRC_Menu() ) {
		Item_TextField_Stop();
	}
}

// Making one: a new one (slot: the next), or one there is, as it is.
static void UI_VRC_Edit( menuDef_t *menu, int slot ) {
	const qboolean has = (qboolean)( slot < UI_VRC_Count() );
	char text[MAX_CVAR_VALUE_STRING], look[MAX_QPATH];
	int rgb[3], k;

	UI_VRC_StopTyping();
	vrc.part = VRC_EDIT;
	vrc.slot = slot;
	vrc.deleting = -1;
	vrc.saving = vrc.saveSent = qfalse;
	vrc.bodyPart = 0;
	UI_VRC_Str( "ui_rpPlaying", vrc.editFor, sizeof( vrc.editFor ) );
	text[0] = vrc.type[0] = look[0] = '\0';
	rgb[0] = rgb[1] = rgb[2] = 255;
	if ( has ) {
		UI_VRC_Comp( slot, "Name", text, sizeof( text ) );
		UI_VRC_Comp( slot, "Type", vrc.type, sizeof( vrc.type ) );
		UI_VRC_Comp( slot, "Look", look, sizeof( look ) );
		UI_VRC_CompRgb( slot, rgb );
	}
	trap->Cvar_Set( "ui_rpCompEditName", text );
	trap->Cvar_Set( "ui_rpCompFind", "" );
	vrc.kind = UI_VRC_KindOf( look );
	vrc.model[0] = '\0';
	if ( vrc.kind == VRC_MODEL ) {
		UI_VRC_ModelLook( look, vrc.model, sizeof( vrc.model ) );
	}
	// a custom look: this one's, or the player's own to start from
	if ( vrc.kind == VRC_OWN && UI_VRC_OwnFrom( look, vrc.own ) ) {
		for ( k = 0; k < 3; k++ ) {
			vrc.ownRgb[k] = rgb[k];
		}
	} else {
		for ( k = 0; k < OWN_PARTS; k++ ) {
			Q_strncpyz( vrc.own[k], vrc.keptValid ? vrc.kept[k] : "", MAX_QPATH );
		}
		for ( k = 0; k < 3; k++ ) {
			vrc.ownRgb[k] = vrc.keptValid ? Com_Clampi( 0, 255, atoi( vrc.kept[4 + k] ) ) : 255;
		}
		if ( vrc.kind == VRC_OWN ) {
			vrc.kind = VRC_AS_IT_COMES;	// (a look the page can't read: as it comes, unless another's chosen)
		}
	}
	UI_VRC_OwnSure( vrc.own );
	UI_VRC_ScrollTo( menu, "vc_types", UI_VRC_TypeShown( vrc.type ) );
	UI_VRC_ScrollTo( menu, "vc_models", UI_VRC_LookShown( vrc.model ) );
	UI_VRC_EditCvars();
}

// A look's kind chosen.
static void UI_VRC_Kind( menuDef_t *menu, int kind, qboolean looks ) {
	const vrcType_t *t;

	kind = Com_Clampi( VRC_AS_IT_COMES, VRC_OWN, kind );
	if ( kind != VRC_AS_IT_COMES && !looks ) {
		trap->Cvar_Set( "ui_rpMessage", "This server keeps companions looking as their type comes" );
		return;
	}
	vrc.kind = kind;
	if ( kind == VRC_MODEL && !vrc.model[0] ) {
		// to start from: its type's model, if the list has it; else the list's first
		t = UI_VRC_Type( vrc.type );
		if ( t && !strchr( t->skin, '|' ) && UI_VRC_LookShown( va( "%s/%s", t->model, t->skin ) ) >= 0 ) {
			Com_sprintf( vrc.model, sizeof( vrc.model ), "%s/%s", t->model, t->skin );
		} else if ( UI_VRComp_FeederCount( FEEDER_VR_RPCOMPLOOKS ) > 0 ) {
			Q_strncpyz( vrc.model, uiInfo.q3HeadNames[vrcLooks.head[0]], sizeof( vrc.model ) );
		}
	}
	if ( kind == VRC_MODEL ) {
		UI_VRC_ScrollTo( menu, "vc_models", UI_VRC_LookShown( vrc.model ) );
	}
	if ( kind == VRC_OWN ) {
		if ( uiInfo.playerSpeciesCount <= 0 ) {
			vrc.kind = VRC_AS_IT_COMES;
			trap->Cvar_Set( "ui_rpMessage", "This game has no species to make a custom character of" );
			return;
		}
		vrc.part = VRC_OWN_EDIT;
		vrc.bodyPart = 0;
		UI_VRC_StopTyping();
		UI_VRC_OwnToLists( menu );
	}
	UI_VRC_EditCvars();
}

// Save: to the server, as that slot - and the page waits for its word.
static void UI_VRC_Save( void ) {
	char name[MAX_CVAR_VALUE_STRING], look[MAX_CVAR_VALUE_STRING];
	int rgb[3];
	const char *p;

	UI_VRC_StopTyping();
	UI_VRC_Str( "ui_rpCompEditName", name, sizeof( name ) );
	for ( p = name; *p == ' '; p++ ) {
	}
	if ( !*p ) {
		trap->Cvar_Set( "ui_rpMessage", "Give it a name" );
		return;
	}
	if ( !vrc.type[0] ) {
		trap->Cvar_Set( "ui_rpMessage", "Choose what it fights as, in the list" );
		return;
	}
	UI_VRC_EditLook( look, sizeof( look ), rgb );
	if ( !BG_VRCompLookOk( look ) ) {
		trap->Cvar_Set( "ui_rpMessage", "That look won't do: choose another" );
		return;
	}
	UI_VRC_EditCvars();
	vrc.saving = vrc.saveSent = qtrue;
	vrc.saveGen = UI_VRC_Int( "ui_rpCompsGen" );
	Q_strncpyz( vrc.saveLook, look, sizeof( vrc.saveLook ) );
	Q_strncpyz( vrc.saveName, name, sizeof( vrc.saveName ) );
	vrc.saveTime = uiInfo.uiDC.realTime;
	trap->Cvar_Set( "ui_rpMessage", VRC_SAVING );
	UI_VRC_Exec( va( "rpui comp save %i\n", vrc.slot ) );
}

/*
--------------------------------------------------------------------------------
Every frame, while it's up: what's shown, as things stand
--------------------------------------------------------------------------------
*/

// Whether an item of this group is shown now (-1: the group isn't one of
// the page's, so whatever it is stays).
static int UI_VRC_Shown( const char *group, qboolean usable, qboolean toRoleplay, qboolean looks ) {
	const qboolean list = (qboolean)( usable && vrc.part == VRC_LIST );
	const qboolean edit = (qboolean)( usable && vrc.part == VRC_EDIT );
	const qboolean own = (qboolean)( usable && vrc.part == VRC_OWN_EDIT );

	if ( !group || Q_stricmpn( group, "vc_", 3 ) ) {
		return -1;
	}
	if ( !Q_stricmp( group, "vc_why" ) ) {
		return !usable;
	}
	if ( !Q_stricmp( group, "vc_why_rp" ) ) {
		return !usable && toRoleplay;
	}
	if ( !Q_stricmp( group, "vc_list" ) ) {
		return list;
	}
	if ( !Q_stricmp( group, "vc_list_act" ) ) {
		return list && vrc.deleting < 0;
	}
	if ( !Q_stricmp( group, "vc_list_del" ) ) {
		return list && vrc.deleting >= 0;
	}
	if ( !Q_stricmp( group, "vc_backbutton" ) ) {
		return !usable || list;
	}
	if ( !Q_stricmp( group, "vc_edit" ) ) {
		return edit;
	}
	if ( !Q_stricmp( group, "vc_looks" ) ) {
		return edit && looks;
	}
	if ( !Q_stricmp( group, "vc_looks_off" ) ) {
		return edit && !looks;
	}
	if ( !Q_stricmp( group, "vc_look0" ) ) {
		return edit && vrc.kind == VRC_AS_IT_COMES;
	}
	if ( !Q_stricmp( group, "vc_look1" ) ) {
		return edit && vrc.kind == VRC_MODEL;
	}
	if ( !Q_stricmp( group, "vc_look2" ) ) {
		return edit && vrc.kind == VRC_OWN;
	}
	if ( !Q_stricmp( group, "vc_custom" ) ) {
		return own;
	}
	if ( !Q_stricmpn( group, "vc_part", 7 ) ) {
		return own && atoi( group + 7 ) == vrc.bodyPart;
	}
	if ( !Q_stricmp( group, "vc_previewback" ) ) {
		return usable;
	}
	return 0;
}

// A look, as the page says it.
static const char *UI_VRC_LookWords( const char *type, const char *look ) {
	char model[MAX_QPATH], skin[MAX_QPATH];
	const vrcType_t *t;

	switch ( UI_VRC_KindOf( look ) ) {
	case VRC_AS_IT_COMES:
		t = UI_VRC_Type( type );
		return t ? va( "As it comes: %s", t->model ) : "As its type comes";
	case VRC_MODEL:
		BG_VRCompLookSplit( look, model, sizeof( model ), skin, sizeof( skin ) );
		return Q_stricmp( skin, "default" ) ? va( "Model: %s, %s", model, skin ) : va( "Model: %s", model );
	default:
		BG_VRCompLookSplit( look, model, sizeof( model ), skin, sizeof( skin ) );
		return va( "Custom: %s", model );
	}
}

// What a type fights as, as the page says it ("Jedi - saber").
static const char *UI_VRC_TypeWords( const char *type ) {
	const vrcType_t *t = UI_VRC_Type( type );

	if ( !t ) {
		return type;
	}
	return t->weapon[0] ? va( "%s - %s", t->kind, t->weapon ) : t->kind;
}

// The lines the page shows (ui_rpComp... cvars), as things stand; and what
// the preview has (its model and skin, and its tint).
static void UI_VRC_Lines( int count, int most, int gametype, char *model, int modelSize, char *skin, int skinSize, int rgb[3] ) {
	char playing[MAX_CVAR_VALUE_STRING], name[MAX_CVAR_VALUE_STRING], type[RP_COMP_TYPE_SIZE], look[MAX_CVAR_VALUE_STRING];
	char text[MAX_CVAR_VALUE_STRING];
	int k, ticked = 0;

	model[0] = skin[0] = '\0';
	rgb[0] = rgb[1] = rgb[2] = 255;
	UI_VRC_Str( "ui_rpPlaying", playing, sizeof( playing ) );
	for ( k = 0; k < count; k++ ) {
		UI_VRC_Comp( k, "Out", text, sizeof( text ) );
		ticked += atoi( text ) ? 1 : 0;
	}

	if ( vrc.part == VRC_LIST ) {
		UI_VRC_Set( "ui_rpCompStatus", va( "%s" S_COLOR_WHITE "'s companions: %i of %i", playing, Q_max( 0, count ), RP_COMP_MAX ) );
		if ( vrc.picked >= 0 ) {
			UI_VRC_Comp( vrc.picked, "Name", name, sizeof( name ) );
			UI_VRC_Comp( vrc.picked, "Type", type, sizeof( type ) );
			UI_VRC_Comp( vrc.picked, "Look", look, sizeof( look ) );
			UI_VRC_Comp( vrc.picked, "Wait", text, sizeof( text ) );
			UI_VRC_Set( "ui_rpCompPickedLine", text[0] ? va( "%s" S_COLOR_WHITE ": %s", name, text ) : "" );
			UI_VRC_Comp( vrc.picked, "Out", text, sizeof( text ) );
			UI_VRC_Set( "ui_rpCompOutLine", atoi( text ) ? "Comes out: Yes" : "Comes out: No" );
			UI_VRC_Set( "ui_rpCompPrevName", name );
			UI_VRC_Set( "ui_rpCompPrevKind", UI_VRC_TypeWords( type ) );
			UI_VRC_Set( "ui_rpCompPrevLook", UI_VRC_LookWords( type, look ) );
			if ( vrc.deleting >= 0 ) {
				UI_VRC_Comp( vrc.deleting, "Name", text, sizeof( text ) );
				UI_VRC_Set( "ui_rpCompDelLine", va( "Delete %s" S_COLOR_WHITE "? It can't be undone.", text ) );
			}
			// the preview: as it comes out (its colour on a look of its own)
			if ( look[0] ) {
				BG_VRCompLookSplit( look, model, modelSize, skin, skinSize );
				UI_VRC_CompRgb( vrc.picked, rgb );
			} else {
				const vrcType_t *t = UI_VRC_Type( type );
				if ( t ) {
					Q_strncpyz( model, t->model, modelSize );
					Q_strncpyz( skin, t->skin, skinSize );
				}
			}
		} else {
			UI_VRC_Set( "ui_rpCompPickedLine", "No companions yet: New makes one." );
			UI_VRC_Set( "ui_rpCompOutLine", "" );
			UI_VRC_Set( "ui_rpCompPrevName", "" );
			UI_VRC_Set( "ui_rpCompPrevKind", "" );
			UI_VRC_Set( "ui_rpCompPrevLook", "" );
		}
		if ( gametype == GT_DUEL || gametype == GT_POWERDUEL || gametype == GT_SIEGE ) {
			UI_VRC_Set( "ui_rpCompOutCount", "They stay in, in this game type" );
		} else if ( most < RP_COMP_OUT_MAX ) {
			UI_VRC_Set( "ui_rpCompOutCount", va( "%i ticked: this server lets %i out", ticked, most ) );
		} else {
			UI_VRC_Set( "ui_rpCompOutCount", va( "%i of %i come out", ticked, RP_COMP_OUT_MAX ) );
		}
		return;
	}

	// making one
	UI_VRC_Str( "ui_rpCompEditName", name, sizeof( name ) );
	if ( vrc.part == VRC_OWN_EDIT ) {
		UI_VRC_Set( "ui_rpCompStatus", "Its custom look: species, colour, head, torso and legs" );
	} else if ( vrc.slot < count ) {
		UI_VRC_Comp( vrc.slot, "Name", text, sizeof( text ) );
		UI_VRC_Set( "ui_rpCompStatus", va( "Changing %s" S_COLOR_WHITE ", %s" S_COLOR_WHITE "'s companion", text, playing ) );
	} else {
		UI_VRC_Set( "ui_rpCompStatus", va( "A new companion for %s", playing ) );
	}
	UI_VRC_Set( "ui_rpCompTypeLine", vrc.type[0] ? va( "Fights as %s: %s", vrc.type, UI_VRC_TypeWords( vrc.type ) ) :
		"Choose what it fights as, in the list" );
	UI_VRC_EditLook( look, sizeof( look ), rgb );
	UI_VRC_Set( "ui_rpCompLookLine", ( vrc.kind == VRC_AS_IT_COMES && !UI_VRC_Type( vrc.type ) ) ?
		"As its type comes: choose one" : UI_VRC_LookWords( vrc.type, look ) );
	UI_VRC_Set( "ui_rpCompPrevName", name[0] ? name : "(its name)" );
	UI_VRC_Set( "ui_rpCompPrevKind", vrc.type[0] ? UI_VRC_TypeWords( vrc.type ) : "" );
	UI_VRC_Set( "ui_rpCompPrevLook", vrcLookWords[vrc.kind] );
	if ( look[0] ) {
		BG_VRCompLookSplit( look, model, modelSize, skin, skinSize );
	} else {
		const vrcType_t *t = UI_VRC_Type( vrc.type );
		if ( t ) {
			Q_strncpyz( model, t->model, modelSize );
			Q_strncpyz( skin, t->skin, skinSize );
		}
	}
}

// The preview: a model and skin (as SetupGameGhoul2Model takes one: its
// name, or head|torso|lower), on the "character" item; "": nothing.
static void UI_VRC_Preview( menuDef_t *menu, const char *model, const char *skin ) {
	itemDef_t *item = Menu_FindItemByName( menu, "character" );
	char want[sizeof( vrc.shown )], path[MAX_QPATH], skinPath[MAX_QPATH];
	int len;

	if ( !item || item->type != ITEM_TYPE_MODEL ) {
		return;
	}
	if ( model[0] ) {
		Com_sprintf( want, sizeof( want ), "%s/%s", model, skin );
	} else {
		want[0] = '\0';
	}
	if ( !strcmp( want, vrc.shown ) ) {
		return;
	}
	Q_strncpyz( vrc.shown, want, sizeof( vrc.shown ) );
	if ( !want[0] || !UI_VRC_Humanoid( model ) ) {
		if ( item->ghoul2 ) {
			UI_ClearG2Pointer( item->ghoul2 );
			trap->G2API_CleanGhoul2Models( &item->ghoul2 );
		}
		item->flags &= ~ITF_G2VALID;
		return;
	}
	Com_sprintf( path, sizeof( path ), "models/players/%s/model.glm", model );
	if ( strchr( skin, '|' ) ) {
		Com_sprintf( skinPath, sizeof( skinPath ), "models/players/%s/|%s", model, skin );
	} else {
		Com_sprintf( skinPath, sizeof( skinPath ), "models/players/%s/model_%s.skin", model, skin[0] ? skin : "default" );
	}
	ItemParse_asset_model_go( item, path, &len );
	ItemParse_model_g2skin_go( item, skinPath );
}

// A name as the server keeps one (g_vrrp.c's G_RP_CleanText): no " \ ; %
// or control characters, a space for each run of spaces, none at the ends.
static void UI_VRC_KeptName( const char *in, char *out, int size ) {
	int n = 0;
	qboolean space = qfalse;

	for ( ; *in && n < size - 1; in++ ) {
		const unsigned char ch = (unsigned char)*in;
		if ( ch == '"' || ch == '\\' || ch == ';' || ch == '%' ) {
			continue;
		}
		if ( ch <= ' ' ) {
			space = qtrue;
			continue;
		}
		if ( space && n > 0 && n < size - 2 ) {
			out[n++] = ' ';
		}
		space = qfalse;
		out[n++] = (char)ch;
	}
	out[n] = '\0';
}

// Whether the slot being saved is, as the server's told it, what was sent:
// its name (as kept), what it fights as (the server writes it in small
// letters) and its look - another list told meanwhile (one knocked out, say)
// isn't the save taken.
static qboolean UI_VRC_Saved( void ) {
	char type[RP_COMP_TYPE_SIZE], look[MAX_QPATH], name[MAX_CVAR_VALUE_STRING], sent[MAX_CVAR_VALUE_STRING], kept[MAX_CVAR_VALUE_STRING];

	if ( vrc.slot >= UI_VRC_Count() ) {
		return qfalse;
	}
	UI_VRC_Comp( vrc.slot, "Type", type, sizeof( type ) );
	UI_VRC_Comp( vrc.slot, "Look", look, sizeof( look ) );
	UI_VRC_Comp( vrc.slot, "Name", name, sizeof( name ) );
	UI_VRC_KeptName( vrc.saveName, sent, sizeof( sent ) );
	UI_VRC_KeptName( name, kept, sizeof( kept ) );
	return (qboolean)( !Q_stricmp( type, vrc.type ) && !strcmp( look, vrc.saveLook ) && !strcmp( kept, sent ) );
}

void UI_VRComp_Frame( void ) {
	menuDef_t *menu = UI_VRC_Menu();
	const char *why, *why2;
	char playing[MAX_CVAR_VALUE_STRING], message[MAX_CVAR_VALUE_STRING], model[MAX_QPATH], skin[MAX_QPATH];
	qboolean toRoleplay, looks, usable;
	int count, most, gametype, rgb[3], i;

	if ( !menu || !( menu->window.flags & WINDOW_VISIBLE ) ) {
		return;
	}
	why = UI_VRC_Why( &why2, &toRoleplay );
	usable = (qboolean)( why == NULL );
	UI_VRC_Server( &most, &looks, &gametype );
	count = UI_VRC_Count();
	UI_VRC_Str( "ui_rpPlaying", playing, sizeof( playing ) );

	// Save: the server's taken it (it told the list again) - back to the
	// list, on it; or its word was no (its message, in place of Saving...:
	// what's made stays, to be put right and saved again)
	UI_VRC_Str( "ui_rpMessage", message, sizeof( message ) );
	if ( vrc.saveSent && UI_VRC_Int( "ui_rpCompsGen" ) != vrc.saveGen && UI_VRC_Saved() ) {
		vrc.saving = vrc.saveSent = qfalse;
		if ( vrc.part != VRC_LIST ) {
			UI_VRC_StopTyping();
			vrc.part = VRC_LIST;
			vrc.picked = vrc.slot;
		}
	} else if ( vrc.saveSent && uiInfo.uiDC.realTime - vrc.saveTime >= VRC_SAVE_WAIT ) {
		vrc.saving = vrc.saveSent = qfalse;
	} else if ( vrc.saving && strcmp( message, VRC_SAVING ) ) {
		vrc.saving = qfalse;
	}
	// what's made can't go on: no longer usable, another character played, the one changed gone
	if ( vrc.part != VRC_LIST && ( !usable || strcmp( playing, vrc.editFor ) || vrc.slot > count ||
		( vrc.slot == count && vrc.slot >= RP_COMP_MAX ) ) ) {
		UI_VRC_StopTyping();
		vrc.part = VRC_LIST;
		vrc.saving = vrc.saveSent = qfalse;
	}
	if ( vrc.part == VRC_OWN_EDIT && !looks ) {
		vrc.part = VRC_EDIT;
	}
	if ( vrc.part != VRC_LIST && !looks ) {
		vrc.kind = VRC_AS_IT_COMES;	// (the server keeps them so)
	}
	vrc.picked = ( count > 0 ) ? Com_Clampi( 0, count - 1, vrc.picked ) : -1;
	if ( vrc.deleting >= count || !usable || vrc.part != VRC_LIST ) {
		vrc.deleting = -1;
	}
	if ( vrc.part == VRC_OWN_EDIT ) {
		UI_VRC_OwnFromLists();	// (the Profile's lists change it)
	}
	if ( vrc.part != VRC_LIST ) {
		UI_VRC_EditCvars();
	}

	// the lines, and what the preview has
	UI_VRC_Set( "ui_rpCompWhy1", why ? why : "" );
	UI_VRC_Set( "ui_rpCompWhy2", why ? why2 : "" );
	if ( usable ) {
		UI_VRC_Lines( count, most, gametype, model, sizeof( model ), skin, sizeof( skin ), rgb );
	} else {
		UI_VRC_Set( "ui_rpCompStatus", "" );
		model[0] = skin[0] = '\0';
	}

	// the items: shown as things stand; tabs lit
	for ( i = 0; i < menu->itemCount; i++ ) {
		itemDef_t *it = menu->items[i];
		int shown = UI_VRC_Shown( it->window.group, usable, toRoleplay, looks );
		if ( it->window.name && !Q_stricmp( it->window.name, "character" ) ) {
			shown = usable && ( it->flags & ITF_G2VALID );
		}
		if ( shown > 0 ) {
			it->window.flags |= WINDOW_VISIBLE;
		} else if ( !shown ) {
			it->window.flags &= ~( WINDOW_VISIBLE | WINDOW_HASFOCUS );
			if ( Item_TextField_Typing() == it ) {
				Item_TextField_Stop();	// (what's typed doesn't go on into a field that's gone)
			}
		}
		if ( it->window.name && ( !Q_strncmp( it->window.name, "vc_look", 7 ) || !Q_strncmp( it->window.name, "vc_part", 7 ) ) &&
			it->type == ITEM_TYPE_BUTTON ) {
			// the look chosen, and the part's list shown: their buttons lit
			const int n = atoi( it->window.name + 7 );
			const qboolean lit = (qboolean)( it->window.name[3] == 'l' ? n == vrc.kind : n == vrc.bodyPart );
			if ( lit ) {
				VectorSet4( it->window.backColor, 0.2f, 0.45f, 0.85f, 0.95f );
			} else if ( !( it->window.flags & WINDOW_MOUSEOVER ) ) {
				VectorSet4( it->window.backColor, 0.0f, 0.0f, 0.3f, 0.7f );
			}
		}
		if ( it->cvar && !it->text && it->textalignment == ITEM_ALIGN_CENTER ) {
			it->textRect.w = 0.0f;	// (its text changes: centred again)
		}
	}

	// the lists lit on the ones chosen
	if ( usable ) {
		UI_VRC_Light( menu, "vc_list", vrc.picked );
		if ( vrc.part == VRC_EDIT ) {
			UI_VRC_Light( menu, "vc_types", UI_VRC_TypeShown( vrc.type ) );
			UI_VRC_Light( menu, "vc_models", vrc.kind == VRC_MODEL ? UI_VRC_LookShown( vrc.model ) : -1 );
		}
	}

	// the preview (and its tint: the custom look's lists tint it themselves)
	UI_VRC_Preview( menu, model, skin );
	if ( usable && vrc.part != VRC_OWN_EDIT ) {
		UI_VRC_Tint( rgb );
	}

	// a message, gone after a while
	if ( strcmp( message, vrc.message ) ) {
		Q_strncpyz( vrc.message, message, sizeof( vrc.message ) );
		vrc.messageTime = uiInfo.uiDC.realTime;
	} else if ( vrc.message[0] && uiInfo.uiDC.realTime - vrc.messageTime >= VRC_MESSAGE_TIME ) {
		trap->Cvar_Set( "ui_rpMessage", "" );
		vrc.message[0] = '\0';
	}
}

/*
--------------------------------------------------------------------------------
Its menu: read in once, and its button on the Profile menu
--------------------------------------------------------------------------------
*/

void UI_VRComp_AddMenus( void ) {
	if ( !Menus_FindByName( VRC_MENU ) ) {
		UI_ParseMenu( VRC_FILE );
		if ( !Menus_FindByName( VRC_MENU ) ) {
			Com_Printf( S_COLOR_YELLOW "%s not found: no Companions menu\n", VRC_FILE );
			return;
		}
	}
	UI_VRRP_Move( VRC_PROFILE_ITEMS, VRC_PROFILE, VRC_PROFILE_ITEM, qtrue );
	vrc.shown[0] = '\0';	// (a menu set read again: its preview's new)
}

/*
--------------------------------------------------------------------------------
What the buttons do (uiScript vrComp <what> ...)
--------------------------------------------------------------------------------
*/

// The menu opened, from the Profile (0) or the Roleplay menu (1): Back goes there.
void UI_VRComp_Open( int from ) {
	vrc.from = from;
	Menus_ActivateByName( VRC_MENU );
}

// Back: to the menu it came from.
static void UI_VRC_Back( void ) {
	Menus_CloseByName( VRC_MENU );
	if ( vrc.from == 1 ) {
		UI_VRRP_Open( -1 );
	} else {
		Menus_ActivateByName( VRC_PROFILE );
	}
}

qboolean UI_VRComp_Script( const char *name, char **args ) {
	menuDef_t *menu = UI_VRC_Menu();
	const char *what, *why, *why2;
	char text[MAX_CVAR_VALUE_STRING];
	qboolean toRoleplay, looks;
	int n, k, most, gametype;

	if ( Q_stricmp( name, "vrComp" ) ) {
		return qfalse;
	}
	if ( !String_Parse( args, &what ) || !menu ) {
		return qtrue;
	}
	why = UI_VRC_Why( &why2, &toRoleplay );
	UI_VRC_Server( &most, &looks, &gametype );
	if ( !Q_stricmp( what, "opened" ) ) {
		// the player's custom character, kept (the custom look's made on its cvars)
		if ( !vrc.keptValid ) {
			for ( k = 0; k < (int)ARRAY_LEN( vrcCharCvars ); k++ ) {
				UI_VRC_Str( vrcCharCvars[k], vrc.kept[k], sizeof( vrc.kept[0] ) );
			}
			vrc.keptValid = qtrue;
		}
		vrc.part = VRC_LIST;
		vrc.deleting = -1;
		vrc.saving = vrc.saveSent = qfalse;
		vrc.shown[0] = '\0';
		UI_VRC_Preview( menu, "", "" );
		trap->Cvar_Set( "ui_rpMessage", "" );
		UI_VRC_ScrollTo( menu, "vc_list", vrc.picked );
	} else if ( !Q_stricmp( what, "closed" ) ) {
		UI_VRC_StopTyping();
		if ( vrc.keptValid ) {
			for ( k = 0; k < (int)ARRAY_LEN( vrcCharCvars ); k++ ) {
				trap->Cvar_Set( vrcCharCvars[k], vrc.kept[k] );
			}
			n = UI_VRC_Species( vrc.kept[0] );
			uiInfo.playerSpeciesIndex = Q_max( 0, n );
			vrc.keptValid = qfalse;
		}
		vrc.part = VRC_LIST;
	} else if ( !Q_stricmp( what, "from" ) ) {
		vrc.from = ( Int_Parse( args, &n ) && n == 1 ) ? 1 : 0;
	} else if ( !Q_stricmp( what, "back" ) ) {
		UI_VRC_Back();
	} else if ( !Q_stricmp( what, "roleplay" ) ) {
		Menus_CloseByName( VRC_MENU );
		UI_VRRP_Open( 0 );	// (its Characters page: log in, play one)
	} else if ( why ) {
		return qtrue;	// (the rest want it usable)
	} else if ( !Q_stricmp( what, "new" ) ) {
		if ( vrc.part == VRC_LIST ) {
			if ( UI_VRC_Count() >= RP_COMP_MAX ) {
				trap->Cvar_Set( "ui_rpMessage", va( "A character has %i companions at most: delete one first", RP_COMP_MAX ) );
			} else {
				UI_VRC_Edit( menu, Q_max( 0, UI_VRC_Count() ) );
			}
		}
	} else if ( !Q_stricmp( what, "change" ) ) {
		if ( vrc.part == VRC_LIST && vrc.picked >= 0 && vrc.picked < UI_VRC_Count() ) {
			UI_VRC_Edit( menu, vrc.picked );
		}
	} else if ( !Q_stricmp( what, "delete" ) ) {
		if ( vrc.part == VRC_LIST && vrc.picked >= 0 && vrc.picked < UI_VRC_Count() ) {
			vrc.deleting = vrc.picked;
		}
	} else if ( !Q_stricmp( what, "deleteYes" ) ) {
		if ( vrc.deleting >= 0 && vrc.deleting < UI_VRC_Count() ) {
			UI_VRC_Exec( va( "rpui comp del %i\n", vrc.deleting ) );
		}
		vrc.deleting = -1;
	} else if ( !Q_stricmp( what, "deleteNo" ) ) {
		vrc.deleting = -1;
	} else if ( !Q_stricmp( what, "out" ) ) {
		if ( vrc.part == VRC_LIST && vrc.picked >= 0 && vrc.picked < UI_VRC_Count() ) {
			UI_VRC_Comp( vrc.picked, "Out", text, sizeof( text ) );
			UI_VRC_Exec( va( "rpui comp out %i %i\n", vrc.picked, atoi( text ) ? 0 : 1 ) );
		}
	} else if ( !Q_stricmp( what, "look" ) ) {
		if ( vrc.part == VRC_EDIT && Int_Parse( args, &n ) ) {
			UI_VRC_Kind( menu, n, looks );
		}
	} else if ( !Q_stricmp( what, "custom" ) ) {
		if ( vrc.part == VRC_EDIT ) {
			UI_VRC_Kind( menu, VRC_OWN, looks );
		}
	} else if ( !Q_stricmp( what, "done" ) ) {
		if ( vrc.part == VRC_OWN_EDIT ) {
			UI_VRC_OwnFromLists();
			vrc.part = VRC_EDIT;
			UI_VRC_EditCvars();
		}
	} else if ( !Q_stricmp( what, "part" ) ) {
		if ( vrc.part == VRC_OWN_EDIT && Int_Parse( args, &n ) ) {
			vrc.bodyPart = Com_Clampi( 0, 2, n );
		}
	} else if ( !Q_stricmp( what, "save" ) ) {
		if ( vrc.part == VRC_EDIT && !vrc.saving ) {
			UI_VRC_Save();
		}
	} else if ( !Q_stricmp( what, "cancel" ) ) {
		if ( vrc.part == VRC_EDIT ) {
			UI_VRC_StopTyping();
			vrc.part = VRC_LIST;
			vrc.saving = vrc.saveSent = qfalse;
			trap->Cvar_Set( "ui_rpMessage", "" );
		}
	}
	return qtrue;
}
