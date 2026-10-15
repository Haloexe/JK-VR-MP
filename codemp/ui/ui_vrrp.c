/*
===========================================================================
OpenJK VR (multiplayer) - the Roleplay menu (ui/vr_rp.menu): its pages,
what each shows as things stand, its lists, and what its buttons do.

Opened from the wheel's Roleplay ring, the ROLEPLAY tab under the in-game
bar (where the server has roleplay), or "ui_openmenu vr_rp". The game
(cg_vrrp.c) keeps where things stand in ui_rp... cvars - the server
(ui_rpServer: 0 none, 1 off, 2 on), the account, its characters, the
one played, the server's last word - and does what needs the server when
asked: "rpui <what>", run from here.

  Characters   logged out: log in, or make an account (the password twice) -
               roleplay on or off; logged in: your characters (a list,
               feeder 65) - play one, stop, delete one (asked first), make
               one - log out, a new password
  Editor       the character you play: faction, description, bio (Save)
  People       who's here (feeder 66), with their faction and description,
               and the bio of the one picked
  Chat & cards speech bubbles, cards, remembering your login; how chat goes
  Help
  Server       the owner's: roleplay on or off, characters per account, how
               far talk reaches, whether running out of health downs players
               - straight to the game you host, by rcon to another server;
               and Admins: an account made an admin, or not (rp admin)
  Admin        an admin's only (its button at the top: ui_rpAdmin, from the
               server's rpadmin): every NPC and vehicle the game's files have
               (feeder 67, a box to find one) to spawn in front of you; kill
               those you spawned, or every NPC; god mode, no-clip, no target
               (each as the server says it is); give all

Also: Roleplay on Create game (g_rp, in a free row of its settings).

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "ui_local.h"

extern void		Menu_UpdatePosition( menuDef_t *menu );
void			UI_ParseMenu( const char *menuFile );

#define VRRP_FILE			"ui/vr_rp.menu"
#define VRRP_MENU			"vr_rp"
#define VRRP_TAB			"vr_rp_tab"			// its items: under the in-game bar
#define VRRP_TAB_ITEM		"vr_rp_tabbutton"
#define VRRP_CREATE			"vr_rp_create"		// its items: on Create game
#define VRRP_CREATE_ITEM	"vr_rp_createrow"
#define VRRP_CHARS_MAX		10
#define VRRP_PEOPLE_AGAIN	8000	// while People's up, the cards asked for again this often (ms)
#define VRRP_MESSAGE_TIME	10000	// a message (ui_rpMessage) shown this long (ms)
#define VRRP_NPCS_MAX		1024	// NPCs and vehicles the Admin page lists, at most
#define VRRP_NPC_NAME		64
#define VRRP_NPC_FILE_MAX	65536	// a file of them read (more of one: cut there)

enum { RPP_CHARS, RPP_EDIT, RPP_PEOPLE, RPP_CHAT, RPP_HELP, RPP_SERVER, RPP_ADMIN, RPP_PAGES };

static const char *vrrpFactions[] = { "Heroes", "Villains", "Neutral" };

// The cvars the password fields type into: emptied whenever the menu
// opens or shuts (what's typed there and not sent doesn't stay).
static const char *vrrpSecrets[] = { "ui_rpLoginPassword", "ui_rpLoginAgain", "ui_rpPassword", "ui_rpNewPassword", "ui_rpNewAgain" };

static struct {
	int			page;			// RPP_*
	qboolean	newAccount;		// making an account (the password twice)
	qboolean	password;		// a new password
	int			deleting;		// asking whether to delete this character (-1: not)
	int			peopleTime;		// the cards last asked for (uiDC.realTime; 0: not)
	char		message[MAX_CVAR_VALUE_STRING];	// the message shown, and since when
	int			messageTime;
	char		editFor[MAX_CVAR_VALUE_STRING];	// the character the Editor's fields were asked for
	int			numPeople;		// who's here, as the list has them
	int			people[MAX_CLIENTS];
	char		names[MAX_CLIENTS][MAX_QPATH];
	qboolean	srvAdmins;		// the Server page shows Admins (making an account one)
	char		adminFind[MAX_CVAR_VALUE_STRING];	// what the Admin page's list was lit from the top for
} vrrp = { RPP_CHARS, qfalse, qfalse, -1, 0 };

static void UI_VRRP_Page( int page );

// The Admin page's list: the NPCs and vehicles the game's files define
// (ext_data/NPCs/*.npc, ext_data/vehicles/*.veh: each a name and a braced
// block, as the game reads them - NPC_ParseParms, VEH_LoadVehicle), read
// once; and those its find box matches.
typedef struct {
	char		name[VRRP_NPC_NAME];
	qboolean	vehicle;
} vrrpNPC_t;

static struct {
	qboolean	read;
	int			count;
	vrrpNPC_t	list[VRRP_NPCS_MAX];
	int			numShown;
	short		shown[VRRP_NPCS_MAX];
	qboolean	filtered;					// shown[] is for find...
	char		find[MAX_CVAR_VALUE_STRING];	// ...as typed then
} vrrpNPCs;

/*
--------------------------------------------------------------------------------
Small things
--------------------------------------------------------------------------------
*/

static void UI_VRRP_Exec( const char *text ) {
	trap->Cmd_ExecuteText( EXEC_APPEND, text );
}

static int UI_VRRP_Int( const char *cvar ) {
	return (int)trap->Cvar_VariableValue( cvar );
}

static void UI_VRRP_Str( const char *cvar, char *out, int size ) {
	trap->Cvar_VariableStringBuffer( cvar, out, size );
}

static void UI_VRRP_Forget( void ) {
	int i;

	for ( i = 0; i < (int)ARRAY_LEN( vrrpSecrets ); i++ ) {
		trap->Cvar_Set( vrrpSecrets[i], "" );
	}
}

// The game you're in is hosted here (the Server page needs no rcon).
static qboolean UI_VRRP_Local( void ) {
	return (qboolean)( UI_VRRP_Int( "sv_running" ) != 0 );
}

static int UI_VRRP_NumChars( void ) {
	return Com_Clampi( 0, VRRP_CHARS_MAX, UI_VRRP_Int( "ui_rpChars" ) );
}

static menuDef_t *UI_VRRP_Menu( void ) {
	return Menus_FindByName( VRRP_MENU );
}

/*
--------------------------------------------------------------------------------
Its menus: read in once, and its items on the in-game bar and Create game
--------------------------------------------------------------------------------
*/

// Whether an item that matters (one with text, a setting, a list or
// something the game draws - not a background or a highlight) lies over the
// box x, y, w, h (in the menu).
static qboolean UI_VRRP_Taken( const menuDef_t *menu, float x, float y, float w, float h ) {
	int i;

	for ( i = 0; i < menu->itemCount; i++ ) {
		const itemDef_t *it = menu->items[i];
		const rectDef_t *r = &it->window.rectClient;
		if ( !it->cvar && !it->text && it->type != ITEM_TYPE_LISTBOX && it->type != ITEM_TYPE_OWNERDRAW ) {
			continue;	// (a backdrop, a highlight)
		}
		if ( r->x < x + w && r->x + r->w > x && r->y < y + h && r->y + r->h > y ) {
			return qtrue;
		}
	}
	return qfalse;
}

// The items of a menu that's never shown (at 0, 0: where they say is on the
// screen) put on another menu - if they aren't there yet, and (free) where
// there's room.
static void UI_VRRP_Move( const char *from, const char *to, const char *first, qboolean free ) {
	menuDef_t *src = Menus_FindByName( from ), *dst = Menus_FindByName( to );
	int i;

	if ( !src || !dst || !src->itemCount || Menu_FindItemByName( dst, first ) ) {
		return;
	}
	if ( dst->itemCount + src->itemCount > MAX_MENUITEMS ) {
		return;
	}
	if ( free ) {
		for ( i = 0; i < src->itemCount; i++ ) {
			const rectDef_t *r = &src->items[i]->window.rectClient;
			if ( ( src->items[i]->text || src->items[i]->cvar ) &&
				UI_VRRP_Taken( dst, r->x - dst->window.rect.x, r->y - dst->window.rect.y, r->w, r->h ) ) {
				Com_Printf( "The menu %s has no room for Roleplay\n", to );
				return;
			}
		}
	}
	for ( i = 0; i < src->itemCount; i++ ) {
		itemDef_t *it = src->items[i];
		it->window.rectClient.x -= dst->window.rect.x;	// (in the other menu)
		it->window.rectClient.y -= dst->window.rect.y;
		it->parent = dst;
		dst->items[dst->itemCount++] = it;
	}
	src->itemCount = 0;	// (they're the other's now)
	Menu_UpdatePosition( dst );
}

// When the menus load (ui_main.c, each set): the Roleplay menu, and its
// items on the in-game bar and Create game.
void UI_VRRP_AddMenus( void ) {
	if ( !Menus_FindByName( VRRP_MENU ) ) {
		UI_ParseMenu( VRRP_FILE );
		if ( !Menus_FindByName( VRRP_MENU ) ) {
			Com_Printf( S_COLOR_YELLOW "%s not found: no Roleplay menu\n", VRRP_FILE );
			return;
		}
	}
	UI_VRRP_Move( VRRP_TAB, "ingame", VRRP_TAB_ITEM, qfalse );
	UI_VRRP_Move( VRRP_CREATE, "createserver", VRRP_CREATE_ITEM, qtrue );
}

/*
--------------------------------------------------------------------------------
The lists
--------------------------------------------------------------------------------
*/

// Who's here, as the people list has them.
static void UI_VRRP_People( void ) {
	char info[MAX_INFO_STRING];
	int i;

	vrrp.numPeople = 0;
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		trap->GetConfigString( CS_PLAYERS + i, info, sizeof( info ) );
		if ( !info[0] ) {
			continue;
		}
		vrrp.people[vrrp.numPeople] = i;
		Q_strncpyz( vrrp.names[vrrp.numPeople], Info_ValueForKey( info, "n" ), sizeof( vrrp.names[0] ) );
		vrrp.numPeople++;
	}
}

// What the game knows of a person's card (ui_rpWhoF<client>, ui_rpWhoD<client>):
// their faction (-1: no character, they play themselves; -2: not known yet)
// and description.
static int UI_VRRP_Who( int client, char *desc, int size ) {
	char faction[16];

	UI_VRRP_Str( va( "ui_rpWhoF%i", client ), faction, sizeof( faction ) );
	if ( desc ) {
		UI_VRRP_Str( va( "ui_rpWhoD%i", client ), desc, size );
	}
	return faction[0] ? Com_Clampi( -1, 2, atoi( faction ) ) : -2;
}

// A name as the game's files have one: letters, digits, _ - . (as the
// game's command to spawn it takes it whole: cg_vrrp.c's CG_RP_NPCName).
static qboolean UI_VRRP_NPCNameOk( const char *name ) {
	const char *p;

	if ( !name[0] || (int)strlen( name ) >= VRRP_NPC_NAME ) {
		return qfalse;
	}
	for ( p = name; *p; p++ ) {
		if ( !( ( *p >= 'a' && *p <= 'z' ) || ( *p >= 'A' && *p <= 'Z' ) || ( *p >= '0' && *p <= '9' ) || *p == '_' || *p == '-' || *p == '.' ) ) {
			return qfalse;
		}
	}
	return qtrue;
}

// The NPCs (or vehicles) a file of them defines, into the list.
static void UI_VRRP_ReadNPCFile( const char *path, qboolean vehicle ) {
	static char text[VRRP_NPC_FILE_MAX];
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
	while ( p && vrrpNPCs.count < VRRP_NPCS_MAX ) {
		char name[VRRP_NPC_NAME];
		const char *tok = COM_ParseExt( &p, qtrue );
		qboolean ok;
		if ( !tok[0] ) {
			break;
		}
		ok = UI_VRRP_NPCNameOk( tok );	// (now: the parser's token is the block's next)
		Q_strncpyz( name, tok, sizeof( name ) );
		if ( !SkipBracedSection( &p, 0 ) ) {
			break;	// (its block never ends: the rest's broken)
		}
		if ( !ok ) {
			continue;
		}
		for ( i = 0; i < vrrpNPCs.count; i++ ) {
			if ( vrrpNPCs.list[i].vehicle == vehicle && !Q_stricmp( vrrpNPCs.list[i].name, name ) ) {
				break;	// (the first of a name is the one spawned)
			}
		}
		if ( i < vrrpNPCs.count ) {
			continue;
		}
		Q_strncpyz( vrrpNPCs.list[vrrpNPCs.count].name, name, VRRP_NPC_NAME );
		vrrpNPCs.list[vrrpNPCs.count].vehicle = vehicle;
		vrrpNPCs.count++;
	}
}

static int QDECL UI_VRRP_NPCOrder( const void *a, const void *b ) {
	const vrrpNPC_t *x = (const vrrpNPC_t *)a, *y = (const vrrpNPC_t *)b;

	if ( x->vehicle != y->vehicle ) {
		return x->vehicle ? 1 : -1;	// (NPCs first, then vehicles)
	}
	return Q_stricmp( x->name, y->name );
}

// The game's NPCs and vehicles, read once (a menu set loaded again keeps them).
static void UI_VRRP_ReadNPCs( void ) {
	static char files[16384];
	static const struct { const char *dir, *ext; qboolean vehicle; } kinds[] = {
		{ "ext_data/NPCs", ".npc", qfalse },
		{ "ext_data/vehicles", ".veh", qtrue },
	};
	int k, i, count;
	const char *p;

	if ( vrrpNPCs.read ) {
		return;
	}
	vrrpNPCs.read = qtrue;
	vrrpNPCs.count = 0;
	for ( k = 0; k < (int)ARRAY_LEN( kinds ); k++ ) {
		count = trap->FS_GetFileList( kinds[k].dir, kinds[k].ext, files, sizeof( files ) );
		for ( i = 0, p = files; i < count && *p; i++, p += strlen( p ) + 1 ) {
			UI_VRRP_ReadNPCFile( va( "%s/%s", kinds[k].dir, p ), kinds[k].vehicle );
		}
	}
	qsort( vrrpNPCs.list, vrrpNPCs.count, sizeof( vrrpNPCs.list[0] ), UI_VRRP_NPCOrder );
	vrrpNPCs.filtered = qfalse;
}

// Those the find box (ui_rpAdminFind) matches, as the list shows them: how many.
static int UI_VRRP_NPCs( void ) {
	char find[MAX_CVAR_VALUE_STRING];
	int i;

	UI_VRRP_ReadNPCs();
	UI_VRRP_Str( "ui_rpAdminFind", find, sizeof( find ) );
	if ( vrrpNPCs.filtered && !strcmp( find, vrrpNPCs.find ) ) {
		return vrrpNPCs.numShown;
	}
	Q_strncpyz( vrrpNPCs.find, find, sizeof( vrrpNPCs.find ) );
	vrrpNPCs.filtered = qtrue;
	vrrpNPCs.numShown = 0;
	for ( i = 0; i < vrrpNPCs.count; i++ ) {
		if ( !find[0] || Q_stristr( vrrpNPCs.list[i].name, find ) ) {
			vrrpNPCs.shown[vrrpNPCs.numShown++] = (short)i;
		}
	}
	return vrrpNPCs.numShown;
}

int UI_VRRP_FeederCount( int feeder ) {
	if ( feeder == FEEDER_VR_RPCHARS ) {
		return UI_VRRP_NumChars();
	}
	if ( feeder == FEEDER_VR_RPPEOPLE ) {
		UI_VRRP_People();
		return vrrp.numPeople;
	}
	if ( feeder == FEEDER_VR_RPNPCS ) {
		return UI_VRRP_NPCs();
	}
	return 0;
}

const char *UI_VRRP_FeederText( int feeder, int index, int column ) {
	static char text[MAX_CVAR_VALUE_STRING];

	text[0] = '\0';
	if ( feeder == FEEDER_VR_RPCHARS && index >= 0 && index < UI_VRRP_NumChars() ) {
		switch ( column ) {
		case 0:
			UI_VRRP_Str( va( "ui_rpName%i", index ), text, sizeof( text ) );
			break;
		case 1:
			UI_VRRP_Str( va( "ui_rpFaction%i", index ), text, sizeof( text ) );
			break;
		default:
			if ( index == UI_VRRP_Int( "ui_rpActive" ) ) {
				Q_strncpyz( text, "Playing", sizeof( text ) );
			}
			break;
		}
	} else if ( feeder == FEEDER_VR_RPPEOPLE && index >= 0 && index < vrrp.numPeople ) {
		const int client = vrrp.people[index];
		int faction;
		switch ( column ) {
		case 0:
			Q_strncpyz( text, vrrp.names[index], sizeof( text ) );
			break;
		case 1:
			faction = UI_VRRP_Who( client, NULL, 0 );
			Q_strncpyz( text, ( faction >= 0 ) ? vrrpFactions[faction] : ( faction == -1 ) ? "as themselves" : "", sizeof( text ) );
			break;
		default:
			UI_VRRP_Who( client, text, sizeof( text ) );
			break;
		}
	} else if ( feeder == FEEDER_VR_RPNPCS && index >= 0 && index < UI_VRRP_NPCs() ) {
		const vrrpNPC_t *npc = &vrrpNPCs.list[vrrpNPCs.shown[index]];
		Q_strncpyz( text, column == 0 ? npc->name : npc->vehicle ? "Vehicle" : "NPC", sizeof( text ) );
	}
	return text;
}

qboolean UI_VRRP_FeederSelection( int feeder, int index ) {
	if ( feeder == FEEDER_VR_RPCHARS ) {
		vrrp.deleting = -1;	// (another picked: not that one)
		return qtrue;
	}
	return (qboolean)( feeder == FEEDER_VR_RPPEOPLE || feeder == FEEDER_VR_RPNPCS );
}

// A list's pick is the row it shows lit (a list always lights one): kept
// on a row there is, and read (-1: the list's empty).
static int UI_VRRP_Pick( menuDef_t *menu, const char *name, int count ) {
	itemDef_t *list = Menu_FindItemByName( menu, name );

	if ( !list || count <= 0 ) {
		return -1;
	}
	list->cursorPos = Com_Clampi( 0, count - 1, list->cursorPos );
	return list->cursorPos;
}

// Your character picked in the list (-1: you've none).
static int UI_VRRP_PickedChar( void ) {
	menuDef_t *menu = UI_VRRP_Menu();

	return menu ? UI_VRRP_Pick( menu, "rp_in_list", UI_VRRP_NumChars() ) : -1;
}

// The NPC or vehicle picked in the Admin page's list (NULL: the list's empty).
static const vrrpNPC_t *UI_VRRP_PickedNPC( void ) {
	menuDef_t *menu = UI_VRRP_Menu();
	const int n = menu ? UI_VRRP_Pick( menu, "rp_adm_list", UI_VRRP_NPCs() ) : -1;

	return ( n >= 0 ) ? &vrrpNPCs.list[vrrpNPCs.shown[n]] : NULL;
}

// The one picked in the people list: where they are in it (-1: no one's here).
static int UI_VRRP_PickedPerson( void ) {
	menuDef_t *menu = UI_VRRP_Menu();

	if ( !menu ) {
		return -1;
	}
	UI_VRRP_People();
	return UI_VRRP_Pick( menu, "rp_people_list", vrrp.numPeople );
}

// A list's pick set, and the list scrolled to it.
static void UI_VRRP_SetPick( menuDef_t *menu, const char *name, int index ) {
	itemDef_t *list = Menu_FindItemByName( menu, name );
	listBoxDef_t *lb;
	int shown;

	if ( !list || list->type != ITEM_TYPE_LISTBOX || !list->typeData.listbox ) {
		return;
	}
	lb = list->typeData.listbox;
	list->cursorPos = index > 0 ? index : 0;
	shown = ( lb->elementHeight > 0 ) ? (int)( ( list->window.rect.h - 2 ) / lb->elementHeight ) : 1;
	lb->startPos = ( list->cursorPos < shown ) ? 0 : list->cursorPos - shown + 1;
}

/*
--------------------------------------------------------------------------------
Every frame, while it's up: what's shown, as things stand
--------------------------------------------------------------------------------
*/

// Whether an item of this group is shown now (-1: the group isn't one of
// the pages', so whatever it is stays). Logging in, roleplay on or off (off:
// it says so, above): it changes nothing else then, but an admin's cheats.
static int UI_VRRP_Shown( const char *group, int server, qboolean in, qboolean playing, qboolean admin ) {
	const qboolean chars = (qboolean)( vrrp.page == RPP_CHARS );

	if ( !group || Q_stricmpn( group, "rp_", 3 ) ) {
		return -1;
	}
	if ( !Q_stricmp( group, "rp_out_on" ) ) {
		return chars && !in && server == 2;
	}
	if ( !Q_stricmp( group, "rp_out" ) ) {
		return chars && !in && server != 0;
	}
	if ( !Q_stricmp( group, "rp_off" ) ) {
		return chars && !in && server != 2;
	}
	if ( !Q_stricmp( group, "rp_login" ) ) {
		return chars && !in && server != 0 && !vrrp.newAccount;
	}
	if ( !Q_stricmp( group, "rp_new" ) ) {
		return chars && !in && server != 0 && vrrp.newAccount;
	}
	if ( !Q_stricmp( group, "rp_admintab" ) ) {
		return admin;
	}
	if ( !Q_stricmp( group, "rp_admin" ) ) {
		return vrrp.page == RPP_ADMIN;	// (no longer an admin: another page - UI_VRRP_Frame)
	}
	if ( !Q_stricmp( group, "rp_in" ) ) {
		return chars && in && !vrrp.password;
	}
	if ( !Q_stricmp( group, "rp_in_act" ) ) {
		return chars && in && !vrrp.password && vrrp.deleting < 0;
	}
	if ( !Q_stricmp( group, "rp_in_del" ) ) {
		return chars && in && !vrrp.password && vrrp.deleting >= 0;
	}
	if ( !Q_stricmp( group, "rp_pw" ) ) {
		return chars && in && vrrp.password;
	}
	if ( !Q_stricmp( group, "rp_edit" ) ) {
		return vrrp.page == RPP_EDIT && playing;
	}
	if ( !Q_stricmp( group, "rp_edit_none" ) ) {
		return vrrp.page == RPP_EDIT && !playing;
	}
	if ( !Q_stricmp( group, "rp_people" ) ) {
		return vrrp.page == RPP_PEOPLE;
	}
	if ( !Q_stricmp( group, "rp_chat" ) ) {
		return vrrp.page == RPP_CHAT;
	}
	if ( !Q_stricmp( group, "rp_help" ) ) {
		return vrrp.page == RPP_HELP;
	}
	if ( !Q_stricmp( group, "rp_server" ) ) {
		return vrrp.page == RPP_SERVER;
	}
	if ( !Q_stricmp( group, "rp_srv_remote" ) ) {
		return vrrp.page == RPP_SERVER && !UI_VRRP_Local();
	}
	if ( !Q_stricmp( group, "rp_srv_set" ) ) {
		return vrrp.page == RPP_SERVER && !vrrp.srvAdmins;
	}
	if ( !Q_stricmp( group, "rp_srv_adm" ) ) {
		return vrrp.page == RPP_SERVER && vrrp.srvAdmins;
	}
	return 0;
}

// The lines of text the menu shows (ui_rp... cvars), as things stand.
static void UI_VRRP_Lines( int server, const char *account, const char *playing, qboolean admin ) {
	char faction[32], name[MAX_CVAR_VALUE_STRING];
	const int most = UI_VRRP_Int( "ui_rpMost" ), count = UI_VRRP_NumChars();

	UI_VRRP_Str( "ui_rpPlayingFaction", faction, sizeof( faction ) );
	if ( server == 0 ) {
		trap->Cvar_Set( "ui_rpStatus", "This server doesn't have roleplay" );
	} else if ( server == 1 ) {
		trap->Cvar_Set( "ui_rpStatus", account[0] ? va( "Roleplay is off on this server right now (you're logged in as %s)", account ) :
			"Roleplay is off on this server right now" );
	} else if ( !account[0] ) {
		trap->Cvar_Set( "ui_rpStatus", "Not logged in: you play as yourself" );
	} else if ( playing[0] ) {
		trap->Cvar_Set( "ui_rpStatus", va( "%s, playing %s" S_COLOR_WHITE " (%s)", account, playing, faction ) );
	} else {
		trap->Cvar_Set( "ui_rpStatus", va( "%s, not playing a character", account ) );
	}
	if ( server == 0 ) {
		trap->Cvar_Set( "ui_rpOffLine1", "This server doesn't run OpenJK VR's roleplay: you play as yourself." );
		trap->Cvar_Set( "ui_rpOffLine2", "" );
	} else {
		trap->Cvar_Set( "ui_rpOffLine1", "Roleplay is off here for now: everyone plays as themselves." );
		trap->Cvar_Set( "ui_rpOffLine2", "You can still log in. The owner can turn it on, on the Server page." );
	}
	trap->Cvar_Set( "ui_rpAccountLine", va( "Account: %s%s", account, admin ? " (admin)" : "" ) );
	if ( vrrp.page == RPP_ADMIN && admin ) {
		const int shown = UI_VRRP_NPCs();
		trap->Cvar_Set( "ui_rpAdminCountLine", vrrpNPCs.find[0] ? va( "%i of %i found", shown, vrrpNPCs.count ) :
			va( "%i NPCs and vehicles", vrrpNPCs.count ) );
		trap->Cvar_Set( "ui_rpAdminGodLine", UI_VRRP_Int( "ui_rpAdminGod" ) ? "On" : "Off" );
		trap->Cvar_Set( "ui_rpAdminNoclipLine", UI_VRRP_Int( "ui_rpAdminNoclip" ) ? "On" : "Off" );
		trap->Cvar_Set( "ui_rpAdminNotargetLine", UI_VRRP_Int( "ui_rpAdminNotarget" ) ? "On" : "Off" );
	}
	trap->Cvar_Set( "ui_rpCountLine", most > 0 ? va( "%i of %i characters", count, most ) : va( "%i characters", count ) );
	trap->Cvar_Set( "ui_rpEditLine", va( "Playing %s", playing ) );
	trap->Cvar_Set( "ui_rpServerLine", server == 2 ? "Roleplay is on here" : server == 1 ? "Roleplay is off here" :
		"This server doesn't have roleplay" );
	if ( vrrp.deleting >= 0 ) {
		UI_VRRP_Str( va( "ui_rpName%i", vrrp.deleting ), name, sizeof( name ) );
		trap->Cvar_Set( "ui_rpDeleteLine", va( "Delete %s" S_COLOR_WHITE "? It can't be undone.", name ) );
	}
}

void UI_VRRP_Frame( void ) {
	menuDef_t *menu = UI_VRRP_Menu();
	char account[64], playing[MAX_CVAR_VALUE_STRING], message[MAX_CVAR_VALUE_STRING];
	int server, i;
	qboolean in, admin;

	if ( !menu || !( menu->window.flags & WINDOW_VISIBLE ) ) {
		return;
	}
	server = Com_Clampi( 0, 2, UI_VRRP_Int( "ui_rpServer" ) );
	UI_VRRP_Str( "ui_rpAccount", account, sizeof( account ) );
	UI_VRRP_Str( "ui_rpPlaying", playing, sizeof( playing ) );
	in = (qboolean)( account[0] != '\0' );
	admin = (qboolean)( in && UI_VRRP_Int( "ui_rpAdmin" ) == 1 );
	if ( !in ) {
		vrrp.password = qfalse;
		vrrp.deleting = -1;
	}
	if ( vrrp.deleting >= UI_VRRP_NumChars() ) {
		vrrp.deleting = -1;	// (it went)
	}
	if ( vrrp.page == RPP_ADMIN && !admin ) {
		UI_VRRP_Page( RPP_CHARS );	// (no longer an admin: the first page)
	}
	UI_VRRP_PickedChar();	// (the lists lit on a row there is)
	if ( vrrp.page == RPP_PEOPLE ) {
		UI_VRRP_PickedPerson();
	}
	if ( vrrp.page == RPP_ADMIN ) {
		char find[MAX_CVAR_VALUE_STRING];
		UI_VRRP_Str( "ui_rpAdminFind", find, sizeof( find ) );
		if ( strcmp( find, vrrp.adminFind ) ) {
			Q_strncpyz( vrrp.adminFind, find, sizeof( vrrp.adminFind ) );
			UI_VRRP_NPCs();
			UI_VRRP_SetPick( menu, "rp_adm_list", 0 );	// (another find: the first it matches, lit)
		}
		UI_VRRP_PickedNPC();
	}
	UI_VRRP_Lines( server, account, playing, admin );

	for ( i = 0; i < menu->itemCount; i++ ) {
		itemDef_t *it = menu->items[i];
		const int shown = UI_VRRP_Shown( it->window.group, server, in, (qboolean)( playing[0] != '\0' ), admin );
		if ( shown > 0 ) {
			it->window.flags |= WINDOW_VISIBLE;
		} else if ( !shown ) {
			it->window.flags &= ~( WINDOW_VISIBLE | WINDOW_HASFOCUS );
			if ( Item_TextField_Typing() == it ) {
				Item_TextField_Stop();	// (what's typed doesn't go on into a field that's gone)
			}
		}
		if ( it->window.name && !Q_strncmp( it->window.name, "rp_tab", 6 ) ) {
			// the page shown: its tab lit
			const qboolean lit = (qboolean)( atoi( it->window.name + 6 ) == vrrp.page );
			if ( lit ) {
				VectorSet4( it->window.backColor, 0.2f, 0.45f, 0.85f, 0.95f );
			} else if ( !( it->window.flags & WINDOW_MOUSEOVER ) ) {
				VectorSet4( it->window.backColor, 0.0f, 0.0f, 0.3f, 0.7f );
			}
		}
		if ( it->cvar && it->type == ITEM_TYPE_TEXT && it->textalignment == ITEM_ALIGN_CENTER ) {
			it->textRect.w = 0.0f;	// (its text changes: centred again)
		}
	}

	// the Editor up, and another character played: its fields, not the last one's
	if ( vrrp.page == RPP_EDIT && strcmp( playing, vrrp.editFor ) ) {
		Q_strncpyz( vrrp.editFor, playing, sizeof( vrrp.editFor ) );
		if ( playing[0] ) {
			UI_VRRP_Exec( "rpui edit\n" );
		}
	}

	// a message, gone after a while
	UI_VRRP_Str( "ui_rpMessage", message, sizeof( message ) );
	if ( strcmp( message, vrrp.message ) ) {
		Q_strncpyz( vrrp.message, message, sizeof( vrrp.message ) );
		vrrp.messageTime = uiInfo.uiDC.realTime;
	} else if ( vrrp.message[0] && uiInfo.uiDC.realTime - vrrp.messageTime >= VRRP_MESSAGE_TIME ) {
		trap->Cvar_Set( "ui_rpMessage", "" );
		vrrp.message[0] = '\0';
	}

	// People, up: the cards asked for now and then
	if ( vrrp.page == RPP_PEOPLE && ( !vrrp.peopleTime || uiInfo.uiDC.realTime - vrrp.peopleTime >= VRRP_PEOPLE_AGAIN ) ) {
		vrrp.peopleTime = uiInfo.uiDC.realTime ? uiInfo.uiDC.realTime : 1;
		UI_VRRP_Exec( "rpui people\n" );
	}
}

/*
--------------------------------------------------------------------------------
What the buttons do (uiScript vrRP <what> ...)
--------------------------------------------------------------------------------
*/

// What was set here last, or the default.
static void UI_VRRP_Keep( const char *cvar, const char *def ) {
	char now[16];

	UI_VRRP_Str( cvar, now, sizeof( now ) );
	if ( !now[0] ) {
		trap->Cvar_Set( cvar, def );
	}
}

// The settings of the server you're on, for the Server page: as its
// serverinfo has them ("vrRP": on, characters, talk range, down -
// g_vrrp.c); a server older than this doesn't say: what was set here
// last, or the defaults.
static void UI_VRRP_FromServer( void ) {
	char info[MAX_INFO_STRING];
	int on, chars, range, downed;

	info[0] = 0;
	trap->GetConfigString( CS_SERVERINFO, info, sizeof( info ) );
	if ( sscanf( Info_ValueForKey( info, "vrRP" ), "%d %d %d %d", &on, &chars, &range, &downed ) == 4 ) {
		trap->Cvar_Set( "ui_rpSrvChars", va( "%i", Com_Clampi( 1, VRRP_CHARS_MAX, chars ) ) );
		trap->Cvar_Set( "ui_rpSrvRange", va( "%i", range ) );
		trap->Cvar_Set( "ui_rpSrvDowned", downed ? "1" : "0" );
		return;
	}
	UI_VRRP_Keep( "ui_rpSrvChars", "5" );
	UI_VRRP_Keep( "ui_rpSrvRange", "510" );
	UI_VRRP_Keep( "ui_rpSrvDowned", "1" );
}

// A page shown: what it needs, got.
static void UI_VRRP_Page( int page ) {
	vrrp.page = Com_Clampi( 0, RPP_PAGES - 1, page );
	vrrp.newAccount = vrrp.password = vrrp.srvAdmins = qfalse;
	vrrp.deleting = -1;
	vrrp.peopleTime = 0;
	trap->Cvar_Set( "ui_rpMessage", "" );
	switch ( vrrp.page ) {
	case RPP_EDIT:
		UI_VRRP_Str( "ui_rpPlaying", vrrp.editFor, sizeof( vrrp.editFor ) );
		if ( vrrp.editFor[0] ) {
			UI_VRRP_Exec( "rpui edit\n" );	// (the character's as it is now)
		}
		break;
	case RPP_SERVER:
		if ( UI_VRRP_Local() ) {
			char downed[16];
			trap->Cvar_Set( "ui_rpSrvChars", va( "%i", Com_Clampi( 1, VRRP_CHARS_MAX, UI_VRRP_Int( "g_rpCharacters" ) ) ) );
			trap->Cvar_Set( "ui_rpSrvRange", va( "%i", UI_VRRP_Int( "g_rpTalkRange" ) ) );
			UI_VRRP_Str( "g_rpDowned", downed, sizeof( downed ) );	// ("": the game's not made it yet - its default)
			trap->Cvar_Set( "ui_rpSrvDowned", ( !downed[0] || atoi( downed ) ) ? "1" : "0" );
		} else {
			UI_VRRP_FromServer();	// (another server's: as its serverinfo has them)
		}
		break;
	default:
		break;
	}
}

// The server's setting: straight to the game you host, by rcon to another.
static void UI_VRRP_ServerSet( const char *cvar, const char *value ) {
	char rcon[MAX_CVAR_VALUE_STRING];

	if ( UI_VRRP_Local() ) {
		trap->Cvar_Set( cvar, value );
		trap->Cvar_Set( "ui_rpMessage", "Done" );
		return;
	}
	UI_VRRP_Str( "rconPassword", rcon, sizeof( rcon ) );
	if ( !rcon[0] ) {
		trap->Cvar_Set( "ui_rpMessage", "Type the server's rcon password first" );
		return;
	}
	UI_VRRP_Exec( va( "rcon %s %s\n", cvar, value ) );
	trap->Cvar_Set( "ui_rpMessage", "Sent to the server (a wrong rcon password does nothing)" );
}

// A server command (rp admin): straight to the game you host, by rcon to
// another; its answer's in the console either way.
static void UI_VRRP_ServerCmd( const char *cmd ) {
	char rcon[MAX_CVAR_VALUE_STRING];

	if ( !UI_VRRP_Local() ) {
		UI_VRRP_Str( "rconPassword", rcon, sizeof( rcon ) );
		if ( !rcon[0] ) {
			trap->Cvar_Set( "ui_rpMessage", "Type the server's rcon password first" );
			return;
		}
	}
	UI_VRRP_Exec( va( "%s%s\n", UI_VRRP_Local() ? "" : "rcon ", cmd ) );
	trap->Cvar_Set( "ui_rpMessage", "Sent: the console (Shift+~) shows the server's answer" );
}

// An account's name as the server takes one: 3 to 20 letters, digits, _ or -
// (into out; the server takes it in any case; qfalse: it won't do).
static qboolean UI_VRRP_AccountName( const char *in, char *out, int size ) {
	const char *p;
	const int len = (int)strlen( in );

	if ( len < 3 || len > 20 || len >= size ) {
		return qfalse;
	}
	for ( p = in; *p; p++ ) {
		if ( !( ( *p >= 'a' && *p <= 'z' ) || ( *p >= 'A' && *p <= 'Z' ) || ( *p >= '0' && *p <= '9' ) || *p == '_' || *p == '-' ) ) {
			return qfalse;
		}
	}
	Q_strncpyz( out, in, size );
	return qtrue;
}

// Close: back to the in-game bar if it's up, out of the menus if not. (What
// was typed for a password goes as it shuts: "closed".)
static void UI_VRRP_Close( void ) {
	const menuDef_t *bar = Menus_FindByName( "ingame" );

	if ( bar && ( bar->window.flags & WINDOW_VISIBLE ) ) {
		Menus_CloseByName( VRRP_MENU );	// (back to the in-game bar)
		return;
	}
	trap->Key_SetCatcher( trap->Key_GetCatcher() & ~KEYCATCH_UI );
	trap->Key_ClearStates();
	trap->Cvar_Set( "cl_paused", "0" );
	Menus_CloseAll();
}

qboolean UI_VRRP_Script( const char *name, char **args ) {
	menuDef_t *menu;
	const char *what;
	int n;

	if ( Q_stricmp( name, "vrRP" ) ) {
		return qfalse;
	}
	if ( !String_Parse( args, &what ) ) {
		return qtrue;
	}
	if ( !Q_stricmp( what, "opened" ) || !Q_stricmp( what, "closed" ) ) {
		const itemDef_t *typing = Item_TextField_Typing();
		if ( typing && typing->parent == (void *)UI_VRRP_Menu() ) {
			Item_TextField_Stop();	// (left typing in it)
		}
	}
	if ( !Q_stricmp( what, "opened" ) ) {
		UI_VRRP_Forget();
		trap->Cvar_Set( "ui_rpMessage", "" );
		trap->Cvar_Set( "ui_rpBioRead", "" );
		trap->Cvar_Set( "ui_rpBioName", "" );
		if ( ( menu = UI_VRRP_Menu() ) != NULL ) {
			UI_VRRP_SetPick( menu, "rp_in_list", UI_VRRP_Int( "ui_rpActive" ) );	// (the one you play, or the first)
			UI_VRRP_SetPick( menu, "rp_people_list", 0 );
		}
		UI_VRRP_Page( vrrp.page );
	} else if ( !Q_stricmp( what, "closed" ) ) {
		UI_VRRP_Forget();
	} else if ( !Q_stricmp( what, "close" ) ) {
		UI_VRRP_Close();
	} else if ( !Q_stricmp( what, "page" ) ) {
		UI_VRRP_Page( Int_Parse( args, &n ) ? n : RPP_CHARS );
	} else if ( !Q_stricmp( what, "login" ) ) {
		UI_VRRP_Exec( "rpui login\n" );
	} else if ( !Q_stricmp( what, "newAccount" ) ) {
		vrrp.newAccount = qtrue;
		trap->Cvar_Set( "ui_rpLoginAgain", "" );
	} else if ( !Q_stricmp( what, "back" ) ) {
		vrrp.newAccount = qfalse;
		trap->Cvar_Set( "ui_rpLoginAgain", "" );
	} else if ( !Q_stricmp( what, "new" ) ) {
		UI_VRRP_Exec( "rpui new\n" );
	} else if ( !Q_stricmp( what, "logout" ) ) {
		UI_VRRP_Exec( "rpui logout\n" );
	} else if ( !Q_stricmp( what, "password" ) ) {
		vrrp.password = qtrue;
		UI_VRRP_Forget();
	} else if ( !Q_stricmp( what, "passwordGo" ) ) {
		UI_VRRP_Exec( "rpui password\n" );
		vrrp.password = qfalse;
	} else if ( !Q_stricmp( what, "passwordCancel" ) ) {
		vrrp.password = qfalse;
		UI_VRRP_Forget();
	} else if ( !Q_stricmp( what, "play" ) ) {
		if ( ( n = UI_VRRP_PickedChar() ) >= 0 ) {
			UI_VRRP_Exec( va( "rpui play %i\n", n ) );
		} else {
			trap->Cvar_Set( "ui_rpMessage", "Make a character first" );
		}
	} else if ( !Q_stricmp( what, "stop" ) ) {
		UI_VRRP_Exec( "rpui play -1\n" );
	} else if ( !Q_stricmp( what, "delete" ) ) {
		if ( ( vrrp.deleting = UI_VRRP_PickedChar() ) < 0 ) {
			trap->Cvar_Set( "ui_rpMessage", "You have no characters" );
		}
	} else if ( !Q_stricmp( what, "deleteYes" ) ) {
		if ( vrrp.deleting >= 0 && vrrp.deleting < UI_VRRP_NumChars() ) {
			UI_VRRP_Exec( va( "rpui delete %i\n", vrrp.deleting ) );
		}
		vrrp.deleting = -1;
	} else if ( !Q_stricmp( what, "deleteNo" ) ) {
		vrrp.deleting = -1;
	} else if ( !Q_stricmp( what, "newChar" ) ) {
		UI_VRRP_Exec( "rpui newchar\n" );
	} else if ( !Q_stricmp( what, "save" ) ) {
		UI_VRRP_Exec( "rpui save\n" );
	} else if ( !Q_stricmp( what, "edit" ) ) {
		UI_VRRP_Exec( "rpui edit\n" );
	} else if ( !Q_stricmp( what, "look" ) ) {
		Menus_CloseByName( VRRP_MENU );	// (to the game's own player menu)
		Menus_ActivateByName( "ingame_player" );
	} else if ( !Q_stricmp( what, "bio" ) ) {
		if ( ( n = UI_VRRP_PickedPerson() ) >= 0 ) {
			trap->Cvar_Set( "ui_rpBioName", va( "%s" S_COLOR_WHITE "'s bio", vrrp.names[n] ) );
			trap->Cvar_Set( "ui_rpBioRead", "" );
			UI_VRRP_Exec( va( "rpui bio %i\n", vrrp.people[n] ) );
		}
	} else if ( !Q_stricmp( what, "server" ) ) {
		const char *how;
		if ( !String_Parse( args, &how ) ) {
			return qtrue;
		}
		if ( !Q_stricmp( how, "on" ) ) {
			UI_VRRP_ServerSet( "g_rp", "1" );
		} else if ( !Q_stricmp( how, "off" ) ) {
			UI_VRRP_ServerSet( "g_rp", "0" );
		} else if ( !Q_stricmp( how, "apply" ) ) {
			UI_VRRP_ServerSet( "g_rpCharacters", va( "%i", Com_Clampi( 1, VRRP_CHARS_MAX, UI_VRRP_Int( "ui_rpSrvChars" ) ) ) );
			UI_VRRP_ServerSet( "g_rpTalkRange", va( "%i", Com_Clampi( 0, 100000, UI_VRRP_Int( "ui_rpSrvRange" ) ) ) );
			UI_VRRP_ServerSet( "g_rpDowned", UI_VRRP_Int( "ui_rpSrvDowned" ) ? "1" : "0" );
		} else if ( !Q_stricmp( how, "admins" ) || !Q_stricmp( how, "settings" ) ) {
			vrrp.srvAdmins = (qboolean)( !Q_stricmp( how, "admins" ) );	// (the page's other half)
			trap->Cvar_Set( "ui_rpMessage", "" );
		} else if ( !Q_stricmp( how, "adminOn" ) || !Q_stricmp( how, "adminOff" ) ) {
			char typed[MAX_CVAR_VALUE_STRING], account[32];
			UI_VRRP_Str( "ui_rpAdminAccount", typed, sizeof( typed ) );
			if ( !UI_VRRP_AccountName( typed, account, sizeof( account ) ) ) {
				trap->Cvar_Set( "ui_rpMessage", "Type the account's name: 3 to 20 letters, numbers, _ or -" );
			} else {
				UI_VRRP_ServerCmd( va( "rp admin %s %s", account, !Q_stricmp( how, "adminOn" ) ? "on" : "off" ) );
			}
		}
	} else if ( !Q_stricmp( what, "adminSpawn" ) ) {
		const vrrpNPC_t *npc = UI_VRRP_PickedNPC();
		if ( npc ) {
			UI_VRRP_Exec( va( "rpui admin spawn %s %s\n", npc->name, npc->vehicle ? "vehicle" : "npc" ) );
		} else {
			trap->Cvar_Set( "ui_rpMessage", vrrpNPCs.count ? "Nothing in the list matches what's in Find" : "No NPCs found in the game's files" );
		}
	} else if ( !Q_stricmp( what, "admin" ) ) {
		const char *how;
		if ( String_Parse( args, &how ) && ( !Q_stricmp( how, "killmine" ) || !Q_stricmp( how, "killall" ) || !Q_stricmp( how, "god" ) ||
			!Q_stricmp( how, "noclip" ) || !Q_stricmp( how, "notarget" ) || !Q_stricmp( how, "give" ) ) ) {
			UI_VRRP_Exec( va( "rpui admin %s\n", how ) );
		}
	}
	return qtrue;
}
