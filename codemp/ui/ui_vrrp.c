/*
===========================================================================
OpenJK VR (multiplayer) - the Roleplay menu (ui/vr_rp.menu): its pages,
what each shows as things stand, its lists, and what its buttons do.

Opened from the wheel's Roleplay ring, the ROLEPLAY tab under the in-game
bar (where the server has roleplay), or "ui_openmenu vr_rp". The game
(cg_vrrp.c) keeps where things stand in ui_rp... cvars - the server
(ui_rpServer: 0 none, 1 off, 2 on, 3 not working - its rp/server.txt
can't be read: no logging in, it says so), the account, its characters, the
one played, the server's last word - and does what needs the server when
asked: "rpui <what>", run from here.

  Characters   logged out: log in, or make an account (the password twice) -
               roleplay on or off; logged in: your characters (a list,
               feeder 65) - play one, stop, delete one (asked first), make
               one - log out, a new password
  Editor       the character you play: faction, description, bio (Save);
               its Force - light, dark or Grey, as its setup has it (where
               it may not be Grey, what that counts as), or a Force Master
               - and Force Powers, the game's Force menu (what's set there
               while you play it is its own: g_vrrp.c; Grey and a Force
               Master's there: ui_vrforce.c), while the server keeps its
               setup with it (rpf, the game's: cg_vrrp.c); its look (the
               Profile), its companions (the Companions menu: ui_vrcomp.c)
  People       who's here (feeder 66), with their faction and description,
               and the bio of the one picked; for an admin, on a server that
               has them (its vrRP says), what their character is - a Grey
               Jedi, a Force Master, or neither - and the buttons that make
               it one (rp force)
  Chat & cards speech bubbles, cards, remembering your login; how chat goes
  Help
  Server       the owner's: roleplay on or off, characters per account, how
               far talk reaches, whether running out of health downs players
               - straight to the game you host, by rcon to another server;
               Grey Jedi (off, any character, those an admin picks) and
               Force Masters (off, those an admin picks);
               Companions: how many come out with a player, how many on the
               server at once, whether they may look as players make them;
               and Admins: an account made an admin, or not (rp admin: only
               the server's console takes it - yours, hosting; another
               server's by rcon, its password typed - and it says so)
  Admin        an admin's only (its button at the top: ui_rpAdmin, from the
               server's rpadmin): every NPC and vehicle the game's files have
               (feeder 67, a box to find one) to spawn in front of you - an
               NPC with the Faction picked (its file's, the Heroes', the
               Villains', neutral, a creature: on a server that takes one,
               as rpadmin says - ui_rpAdminSides); kill
               those you spawned, or every NPC; god mode, no-clip, no target
               (each as the server says it is); give all; All Force
               Powers (every power at level 3: give forcepowers - on a
               server that gives them, as it says); and, on a server that
               has them (it says how they go: ui_rpInvAble, cg_vrrp.c), its
               Invasion part - who invades (Reborn, Stormtroopers, Rebels,
               Jedi, or an NPC type by name), their Faction (as a spawn's,
               but neutral), waves (or till it's stopped),
               a wave's, how many up at once, the next wave once one's
               beaten or on a timer, the wait; Start, Stop all; how each
               invasion on goes, as the server says (ui_rpInvLine<n>). On a
               server that takes them (rpadmin: ui_rpAdminWars) an
               invasion's of up to three NPC types, mixed in every wave or
               each so many a wave (How many), and its Mode - Invasion, or
               War: each side's lineup (Lineup of: the Heroes', the
               Villains', the Creatures' - two at least), the same settings
               (each side's), Start war, Stop war; its first line the war
               they make (ui_rpWarSum)

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
#define VRRP_FORCE			"vr_rp_force"		// its items: on the game's Force menu (ui_vrforce.c)
#define VRRP_FORCE_ITEM		"vrf_note"
#define VRRP_CHARS_MAX		10
#define VRRP_PEOPLE_AGAIN	8000	// while People's up, the cards asked for again this often (ms)
#define VRRP_MESSAGE_TIME	10000	// a message (ui_rpMessage) shown this long (ms)
#define VRRP_NPCS_MAX		4096	// NPCs and vehicles the Admin page lists, at most (NPC packs and all)
#define VRRP_NPC_NAME		64
#define VRRP_NPC_FILE_MAX	0x40000	// a file of them read: one as big, left out (as the server leaves an NPC file out)
#define VRRP_COMPS_MOST		64		// companions out on the server at once, the most the page sets

enum { RPP_CHARS, RPP_EDIT, RPP_PEOPLE, RPP_CHAT, RPP_HELP, RPP_SERVER, RPP_ADMIN, RPP_PAGES };
enum { RPS_SETTINGS, RPS_ADMINS, RPS_COMPANIONS };	// the Server page's parts
enum { RPA_CHEATS, RPA_INVASION };	// the Admin page's parts
#define VRRP_INV_KINDS		4		// the Invasion part's who: its presets' words (then one by name: ui_rpInvType)
#define VRRP_INV_NOBODY		5		// a lineup's row, nobody (else) in it: a war's first, the side's not in the war
#define VRRP_INV_ROWS		3		// a lineup's rows (the server takes 4 NPC types an invasion: g_vrinvade.c)
#define VRRP_INV_EACH_MOST	32		// how many of one a wave, at most (as the server's)
#define VRRP_WAR_SIDES		3
#define VRRP_WAR_SUM_MOST	80		// the War mode's first line, cut at this ("..."): within the page
static const char *vrrpInvaders[VRRP_INV_KINDS] = { "reborn", "stormtroopers", "rebels", "jedi" };
static const char *vrrpInvaderTitles[VRRP_INV_KINDS] = { "Reborn", "Stormtroopers", "Rebels", "Jedi" };
// a war's sides (as the game sends them on: light, dark, creatures - cg_vrrp.c): their rows' cvars (ui_rpWar<side>Kind<n>...),
// what the page calls them
static const char *vrrpWarCvars[VRRP_WAR_SIDES] = { "Light", "Dark", "Creatures" };
static const char *vrrpWarTitles[VRRP_WAR_SIDES] = { "Heroes", "Villains", "Creatures" };

// The Admin page's Faction for an NPC (ui_rpAdmSide, a spawn's; ui_rpInvSide,
// an invasion's): its words for the game (cg_vrrp.c), by the row's value - 0,
// none: its NPC file's.
static const char *vrrpSideWords[] = { "", "light", "dark", "neutral", "creature" };

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
	int			srvPart;		// the Server page's part shown: RPS_*
	int			admPart;		// the Admin page's: RPA_*
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
// there's room. (The Companions menu's button goes on the Profile so too.)
void UI_VRRP_Move( const char *from, const char *to, const char *first, qboolean free ) {
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
	UI_VRRP_Move( VRRP_FORCE, "ingame_playerforce", VRRP_FORCE_ITEM, qfalse );	// (shown with both sides' powers: ui_vrforce.c)
}

// Whether the Force setup you have is the character's you play, the server
// keeping it with the character (rpf: cg_vrrp.c).
static qboolean UI_VRRP_ForceKept( void ) {
	char rpf[MAX_CVAR_VALUE_STRING];

	UI_VRRP_Str( "rpf", rpf, sizeof( rpf ) );
	return (qboolean)( rpf[0] != '\0' );
}

// The menus started, not in a game (ui_main.c): your own Force setup back, if
// the game last stopped playing a roleplay character's - the game puts it back
// as it stops (cg_vrrp.c), but that isn't saved when you quit, or crash.
void UI_VRRP_ForceOwnBack( void ) {
	char own[MAX_CVAR_VALUE_STRING];

	UI_VRRP_Str( "cg_rpOwnForce", own, sizeof( own ) );
	if ( own[0] ) {
		trap->Cvar_Set( "forcepowers", own );
		trap->Cvar_Set( "cg_rpOwnForce", "" );
	}
}

// The Roleplay menu opened (on a page: -1, the one it was on).
void UI_VRRP_Open( int page ) {
	if ( page >= 0 ) {
		vrrp.page = Com_Clampi( 0, RPP_PAGES - 1, page );
	}
	Menus_ActivateByName( VRRP_MENU );
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
	static char text[VRRP_NPC_FILE_MAX + 1];
	fileHandle_t f = 0;
	const char *p;
	int len, i;

	len = trap->FS_Open( path, &f, FS_READ );
	if ( !f ) {
		return;
	}
	if ( len <= 0 || len >= VRRP_NPC_FILE_MAX ) {
		trap->FS_Close( f );
		return;	// (an NPC file this big the server leaves out: NPC_LoadParms)
	}
	trap->FS_Read( text, len, f );
	trap->FS_Close( f );
	text[len] = '\0';
	if ( !vehicle ) {	// (an NPC file as the server reads one, and the part of it it keeps: NPC_LoadParms)
		len = BG_VRNPCFileText( text, len );
		text[BG_VRBlocksInStep( text, len, NULL, 0 )] = '\0';
	}
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
	static char files[0x40000];	// (every file there can be: 4096, MAX_FOUND_FILES - NPC packs' and all)
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
The Invasion part's lineups: an invasion's, each side's of a war
--------------------------------------------------------------------------------
*/

// Whether the server takes lineups - several NPC types an invasion, how many
// a wave of each - and wars (it says so: rpadmin - ui_rpAdminWars, cg_vrrp.c).
static qboolean UI_VRRP_Wars( void ) {
	return (qboolean)( UI_VRRP_Int( "ui_rpAdminWars" ) == 1 );
}

// The Invasion part's in War mode (Mode: a server without wars, Invasion).
static qboolean UI_VRRP_WarMode( void ) {
	return (qboolean)( UI_VRRP_Wars() && UI_VRRP_Int( "ui_rpInvMode" ) == 1 );
}

// The war's side whose lineup the rows show (Lineup of).
static int UI_VRRP_WarSide( void ) {
	return Com_Clampi( 0, VRRP_WAR_SIDES - 1, UI_VRRP_Int( "ui_rpWarSide" ) );
}

// A lineup's row's cvar - what's Kind (who: a preset, by name, nobody), Type
// (the NPC type by name) or Each (how many a wave: 0, mixed) - of row n of
// an invasion's (side -1: ui_rpInvKind, ui_rpInvKind1...) or a war side's
// (ui_rpWarLightKind0...).
static void UI_VRRP_RowCvar( int side, int n, const char *what, char *out, int size ) {
	if ( side >= 0 ) {
		Com_sprintf( out, size, "ui_rpWar%s%s%i", vrrpWarCvars[side], what, n );
	} else if ( n ) {
		Com_sprintf( out, size, "ui_rpInv%s%i", what, n );
	} else {
		Com_sprintf( out, size, "ui_rpInv%s", what );
	}
}

static int UI_VRRP_RowInt( int side, int n, const char *what ) {
	char cvar[MAX_QPATH];

	UI_VRRP_RowCvar( side, n, what, cvar, sizeof( cvar ) );
	return UI_VRRP_Int( cvar );
}

// Whether row n has someone in it: a preset or an NPC type by name.
static qboolean UI_VRRP_RowSomeone( int side, int n ) {
	const int kind = UI_VRRP_RowInt( side, n, "Kind" );

	return (qboolean)( kind >= 0 && kind <= VRRP_INV_KINDS );
}

// The rows of a lineup in it: the first ones with someone in each (a row
// after one with nobody, not shown, isn't); one at most where the server
// takes one NPC type an invasion.
static int UI_VRRP_Rows( int side ) {
	const int most = UI_VRRP_Wars() ? VRRP_INV_ROWS : 1;
	int n;

	for ( n = 0; n < most && UI_VRRP_RowSomeone( side, n ); n++ ) {
	}
	return n;
}

// A lineup's row's items (groups rp_inv_s<n>, an invasion's, and
// rp_war<side>_s<n>, a war side's - its Type box's a t after, its How
// many's an n): shown on the Invasion part in their mode (a war side's, the
// side Lineup of picks), a row with someone in every row before it - a
// server that takes several; its Type box with An NPC type, by name picked,
// its How many with someone in it. -1: not one of them.
static int UI_VRRP_RowShown( const char *group ) {
	int side = -1, n, shownRows;
	const char *p;

	if ( !Q_stricmpn( group, "rp_inv_s", 8 ) ) {
		p = group + 8;
	} else if ( !Q_stricmpn( group, "rp_war", 6 ) && group[6] >= '0' && group[6] < '0' + VRRP_WAR_SIDES && !Q_stricmpn( group + 7, "_s", 2 ) ) {
		side = group[6] - '0';
		p = group + 9;
	} else {
		return -1;
	}
	if ( p[0] < '0' || p[0] >= '0' + VRRP_INV_ROWS || ( p[1] && ( ( p[1] != 't' && p[1] != 'n' ) || p[2] ) ) ) {
		return -1;
	}
	n = p[0] - '0';
	if ( vrrp.page != RPP_ADMIN || vrrp.admPart != RPA_INVASION || ( side >= 0 ) != UI_VRRP_WarMode() || ( side >= 0 && side != UI_VRRP_WarSide() ) ) {
		return 0;
	}
	shownRows = Q_min( UI_VRRP_Rows( side ) + 1, UI_VRRP_Wars() ? VRRP_INV_ROWS : 1 );
	if ( n >= shownRows ) {
		return 0;
	}
	if ( p[1] == 't' ) {
		return UI_VRRP_RowInt( side, n, "Kind" ) == VRRP_INV_KINDS;
	}
	if ( p[1] == 'n' ) {
		return UI_VRRP_Wars() && UI_VRRP_RowSomeone( side, n );
	}
	return 1;
}

// What a lineup's rows make (side -1, an invasion's; a war side's) - an NPC
// type or a preset each, a comma between, how many a wave of one after a
// colon (reborn:2,jedi; several, and how many, only on a server that takes
// them): how many NPC types it's of (0: nobody - a war's side not in it);
// -1, a row's NPC type isn't one, or one's in it twice: ui_rpMessage says so
// (whose: the side's, "Villains: ").
static int UI_VRRP_Lineup( int side, const char *whose, char *who, int size ) {
	char names[VRRP_INV_ROWS][VRRP_NPC_NAME];
	const int rows = UI_VRRP_Rows( side );
	int n, i;

	who[0] = '\0';
	for ( n = 0; n < rows; n++ ) {
		const int kind = UI_VRRP_RowInt( side, n, "Kind" );
		const int each = UI_VRRP_Wars() ? Com_Clampi( 0, VRRP_INV_EACH_MOST, UI_VRRP_RowInt( side, n, "Each" ) ) : 0;
		if ( kind < VRRP_INV_KINDS ) {
			Q_strncpyz( names[n], vrrpInvaders[kind], sizeof( names[n] ) );
		} else {
			char cvar[MAX_QPATH], typed[MAX_CVAR_VALUE_STRING];	// (one too long: seen whole, and refused)
			UI_VRRP_RowCvar( side, n, "Type", cvar, sizeof( cvar ) );
			UI_VRRP_Str( cvar, typed, sizeof( typed ) );
			if ( !UI_VRRP_NPCNameOk( typed ) ) {
				trap->Cvar_Set( "ui_rpMessage", va( "%sType the NPC type's name: letters, numbers, _ - and .", whose ) );
				return -1;
			}
			Q_strncpyz( names[n], typed, sizeof( names[n] ) );
		}
		for ( i = 0; i < n; i++ ) {
			if ( !Q_stricmp( names[i], names[n] ) ) {
				trap->Cvar_Set( "ui_rpMessage", va( "%s%s is in it twice: pick it once - How many, how many a wave of it", whose,
					( kind < VRRP_INV_KINDS ) ? vrrpInvaderTitles[kind] : names[n] ) );
				return -1;
			}
		}
		Q_strcat( who, size, n ? "," : "" );
		Q_strcat( who, size, names[n] );
		if ( each ) {
			Q_strcat( who, size, va( ":%i", each ) );
		}
	}
	return rows;
}

// The War mode's first line (ui_rpWarSum): the war its lineups make, each
// side's NPC types against the others' - where you stand - or what it needs yet.
static void UI_VRRP_WarSum( void ) {
	char sides[VRRP_WAR_SIDES][MAX_CVAR_VALUE_STRING], sum[MAX_CVAR_VALUE_STRING];
	int s, n, in = 0;

	for ( s = 0; s < VRRP_WAR_SIDES; s++ ) {
		const int rows = UI_VRRP_Rows( s );
		if ( !rows ) {
			continue;
		}
		Com_sprintf( sides[in], sizeof( sides[in] ), "%s (", vrrpWarTitles[s] );
		for ( n = 0; n < rows; n++ ) {
			const int kind = UI_VRRP_RowInt( s, n, "Kind" );
			char name[VRRP_NPC_NAME];
			if ( kind < VRRP_INV_KINDS ) {
				Q_strncpyz( name, vrrpInvaderTitles[kind], sizeof( name ) );
			} else {
				char cvar[MAX_QPATH];
				UI_VRRP_RowCvar( s, n, "Type", cvar, sizeof( cvar ) );
				UI_VRRP_Str( cvar, name, sizeof( name ) );
				if ( !name[0] ) {
					Q_strncpyz( name, "an NPC type", sizeof( name ) );
				}
			}
			Q_strcat( sides[in], sizeof( sides[in] ), va( "%s%s", n ? ", " : "", name ) );
		}
		Q_strcat( sides[in], sizeof( sides[in] ), ")" );
		in++;
	}
	if ( in < 2 ) {
		Q_strncpyz( sum, "War: two sides at least - pick who fights for each in Lineup of", sizeof( sum ) );
	} else if ( in == 2 ) {
		Com_sprintf( sum, sizeof( sum ), "War: %s against %s, where you stand", sides[0], sides[1] );
		if ( (int)strlen( sum ) > VRRP_WAR_SUM_MOST ) {
			Com_sprintf( sum, sizeof( sum ), "War: %s against %s", sides[0], sides[1] );	// (too long: just the sides)
		}
	} else {
		Com_sprintf( sum, sizeof( sum ), "War: %s, %s and %s, each against the others", sides[0], sides[1], sides[2] );
		if ( (int)strlen( sum ) > VRRP_WAR_SUM_MOST ) {
			Com_sprintf( sum, sizeof( sum ), "War: %s, %s and %s", sides[0], sides[1], sides[2] );
		}
	}
	if ( (int)strlen( sum ) > VRRP_WAR_SUM_MOST ) {
		strcpy( sum + VRRP_WAR_SUM_MOST - 3, "..." );
	}
	trap->Cvar_Set( "ui_rpWarSum", sum );
}

/*
--------------------------------------------------------------------------------
Every frame, while it's up: what's shown, as things stand
--------------------------------------------------------------------------------
*/

static qboolean UI_VRRP_ServerRoles( int *grey, int *master );

// Whether an item of this group is shown now (-1: the group isn't one of
// the pages', so whatever it is stays). Logging in, roleplay on or off (off:
// it says so, above): it changes nothing else then, but an admin's cheats.
// Where roleplay isn't working (3), no logging in: it says why.
static int UI_VRRP_Shown( const char *group, int server, qboolean in, qboolean playing, qboolean admin ) {
	const qboolean chars = (qboolean)( vrrp.page == RPP_CHARS );
	const qboolean logs = (qboolean)( server == 1 || server == 2 );	// (logging in works here)

	if ( !group || Q_stricmpn( group, "rp_", 3 ) ) {
		return -1;
	}
	if ( !Q_stricmp( group, "rp_out_on" ) ) {
		return chars && !in && server == 2;
	}
	if ( !Q_stricmp( group, "rp_out" ) ) {
		return chars && !in && logs;
	}
	if ( !Q_stricmp( group, "rp_off" ) ) {
		return chars && !in && server != 2;
	}
	if ( !Q_stricmp( group, "rp_login" ) ) {
		return chars && !in && logs && !vrrp.newAccount;
	}
	if ( !Q_stricmp( group, "rp_new" ) ) {
		return chars && !in && logs && vrrp.newAccount;
	}
	if ( !Q_stricmp( group, "rp_admintab" ) ) {
		return admin;
	}
	if ( !Q_stricmp( group, "rp_admin" ) ) {
		return vrrp.page == RPP_ADMIN && vrrp.admPart == RPA_CHEATS;	// (no longer an admin: another page - UI_VRRP_Frame)
	}
	if ( !Q_stricmp( group, "rp_admin_force" ) ) {	// (All Force Powers: a server that gives them)
		int grey, master;
		return vrrp.page == RPP_ADMIN && vrrp.admPart == RPA_CHEATS && UI_VRRP_ServerRoles( &grey, &master );
	}
	if ( !Q_stricmp( group, "rp_admin_inv" ) ) {	// (the Invasion part's button: a server that has them)
		return vrrp.page == RPP_ADMIN && vrrp.admPart == RPA_CHEATS && UI_VRRP_Int( "ui_rpInvAble" ) == 1;
	}
	if ( !Q_stricmp( group, "rp_adm_inv" ) ) {
		return vrrp.page == RPP_ADMIN && vrrp.admPart == RPA_INVASION;
	}
	if ( !Q_stricmp( group, "rp_adm_inv_im" ) ) {	// (its Invasion mode's)
		return vrrp.page == RPP_ADMIN && vrrp.admPart == RPA_INVASION && !UI_VRRP_WarMode();
	}
	if ( !Q_stricmp( group, "rp_adm_inv_wm" ) ) {	// (its War mode's)
		return vrrp.page == RPP_ADMIN && vrrp.admPart == RPA_INVASION && UI_VRRP_WarMode();
	}
	if ( !Q_stricmp( group, "rp_adm_inv_wars" ) ) {	// (Mode: a server that has wars)
		return vrrp.page == RPP_ADMIN && vrrp.admPart == RPA_INVASION && UI_VRRP_Wars();
	}
	if ( !Q_stricmp( group, "rp_adm_inv_none" ) ) {	// (no invasion on)
		return vrrp.page == RPP_ADMIN && vrrp.admPart == RPA_INVASION && UI_VRRP_Int( "ui_rpInvOn" ) != 1 && !UI_VRRP_WarMode();
	}
	if ( !Q_stricmp( group, "rp_adm_war_none" ) ) {	// (no war on - nor an invasion)
		return vrrp.page == RPP_ADMIN && vrrp.admPart == RPA_INVASION && UI_VRRP_Int( "ui_rpInvOn" ) != 1 && UI_VRRP_WarMode();
	}
	{	// (a lineup's rows)
		const int row = UI_VRRP_RowShown( group );
		if ( row >= 0 ) {
			return row;
		}
	}
	if ( !Q_stricmp( group, "rp_admin_side" ) ) {	// (a spawn's Faction: a server that takes one - it says so, rpadmin)
		return vrrp.page == RPP_ADMIN && vrrp.admPart == RPA_CHEATS && UI_VRRP_Int( "ui_rpAdminSides" ) == 1;
	}
	if ( !Q_stricmp( group, "rp_adm_inv_side" ) ) {	// (an invasion's: a war's sides are its lineups')
		return vrrp.page == RPP_ADMIN && vrrp.admPart == RPA_INVASION && UI_VRRP_Int( "ui_rpAdminSides" ) == 1 && !UI_VRRP_WarMode();
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
	if ( !Q_stricmp( group, "rp_edit_kept" ) ) {	// (its Force setup kept with it: the one you have - cg_vrrp.c)
		return vrrp.page == RPP_EDIT && playing && UI_VRRP_ForceKept();
	}
	if ( !Q_stricmp( group, "rp_edit_lone" ) ) {	// (not: roleplay off, or an older server)
		return vrrp.page == RPP_EDIT && playing && !UI_VRRP_ForceKept();
	}
	if ( !Q_stricmp( group, "rp_people" ) ) {
		return vrrp.page == RPP_PEOPLE;
	}
	if ( !Q_stricmp( group, "rp_people_admin" ) ) {	// (making characters Grey Jedi, Force Masters: a server that has them)
		int grey, master;
		return vrrp.page == RPP_PEOPLE && admin && UI_VRRP_ServerRoles( &grey, &master );
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
		return vrrp.page == RPP_SERVER && vrrp.srvPart == RPS_SETTINGS;
	}
	if ( !Q_stricmp( group, "rp_srv_adm" ) ) {
		return vrrp.page == RPP_SERVER && vrrp.srvPart == RPS_ADMINS;
	}
	if ( !Q_stricmp( group, "rp_srv_comp" ) ) {
		return vrrp.page == RPP_SERVER && vrrp.srvPart == RPS_COMPANIONS;
	}
	return 0;
}

// What the server you're on has of Grey Jedi (0 none, 1 any character, 2
// those an admin picks) and Force Masters (0, 1), as its serverinfo says
// (vrRP: g_vrrp.c). qfalse: a server older than them (none) - nor does it
// give every Force power (give forcepowers).
static qboolean UI_VRRP_ServerRoles( int *grey, int *master ) {
	char info[MAX_INFO_STRING];
	int v[9];

	info[0] = '\0';
	trap->GetConfigString( CS_SERVERINFO, info, sizeof( info ) );
	if ( sscanf( Info_ValueForKey( info, "vrRP" ), "%d %d %d %d %d %d %d %d %d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8] ) == 9 ) {
		*grey = Com_Clampi( 0, 2, v[7] );
		*master = v[8] ? 1 : 0;
		return qtrue;
	}
	*grey = *master = 0;
	return qfalse;
}

// The Editor's Force line (ui_rpEditForce): what the character's of - a Force
// Master (the server says: ui_rpForceMay); its setup's side (forcepowers: the
// character's own while you play one, as the server says - cg_vrrp.c), Grey,
// or where it may not be Grey, what that counts as (5 light, 6 dark: as the
// server has it, UI_VRForce_SideMost); light, as the Force menu takes one that
// isn't a setup.
static int UI_VRRP_ForceLine( void ) {
	char setup[MAX_CVAR_VALUE_STRING];
	int levels[NUM_FORCE_POWERS], i;
	qboolean ended = qfalse;

	if ( UI_VRForce_May() & BG_FORCE_MASTER ) {
		return 4;
	}
	UI_VRRP_Str( "forcepowers", setup, sizeof( setup ) );
	if ( !setup[0] || setup[1] != '-' || setup[3] != '-' || ( setup[2] != '0' + FORCE_DARKSIDE && setup[2] != '0' + FORCE_GREYSIDE ) ) {
		return FORCE_LIGHTSIDE;
	}
	if ( setup[2] == '0' + FORCE_DARKSIDE ) {
		return FORCE_DARKSIDE;
	}
	if ( UI_VRForce_May() & BG_FORCE_GREY ) {
		return FORCE_GREYSIDE;
	}
	for ( i = 0; i < NUM_FORCE_POWERS; i++ ) {
		const char c = ended ? '\0' : setup[4 + i];
		ended = (qboolean)( ended || !c );
		levels[i] = ( c >= '0' && c <= '3' ) ? c - '0' : 0;
	}
	return ( UI_VRForce_SideMost( levels ) == FORCE_DARKSIDE ) ? 6 : 5;
}

// The People page's Force line, for an admin (ui_rpPeopleForce): what an admin
// made the character of the one picked, as their card says (ui_rpWhoR<client>:
// sent an admin - cg_vrrp.c), and whether the server has that now.
static void UI_VRRP_PeopleForce( void ) {
	const int n = UI_VRRP_PickedPerson();
	const char *line;
	char role[16];
	int grey, master;

	if ( n < 0 ) {
		trap->Cvar_Set( "ui_rpPeopleForce", "" );
		return;
	}
	UI_VRRP_ServerRoles( &grey, &master );
	UI_VRRP_Str( va( "ui_rpWhoR%i", vrrp.people[n] ), role, sizeof( role ) );
	switch ( UI_VRRP_Who( vrrp.people[n], NULL, 0 ) ) {
	case -1:
		line = "Plays as themselves";
		break;
	case -2:
		line = "Not known yet";
		break;
	default:
		if ( !role[0] ) {
			line = "Not known yet";
		} else if ( atoi( role ) == 2 ) {
			line = master ? "Now: Force Master" : "Now: Force Master (off here)";
		} else if ( atoi( role ) == 1 ) {
			line = ( grey == 2 ) ? "Now: Grey Jedi" : ( grey == 1 ) ? "Now: Grey Jedi (any may be here)" : "Now: Grey Jedi (off here)";
		} else {
			line = "Now: Light or Dark";
		}
		break;
	}
	trap->Cvar_Set( "ui_rpPeopleForce", line );
}

// The lines of text the menu shows (ui_rp... cvars), as things stand.
static void UI_VRRP_Lines( int server, const char *account, const char *playing, qboolean admin ) {
	char faction[32], name[MAX_CVAR_VALUE_STRING];
	const int most = UI_VRRP_Int( "ui_rpMost" ), count = UI_VRRP_NumChars();

	UI_VRRP_Str( "ui_rpPlayingFaction", faction, sizeof( faction ) );
	if ( server == 0 ) {
		trap->Cvar_Set( "ui_rpStatus", "This server doesn't have roleplay" );
	} else if ( server == 3 ) {
		trap->Cvar_Set( "ui_rpStatus", "Roleplay isn't working on this server right now" );
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
	} else if ( server == 3 ) {
		trap->Cvar_Set( "ui_rpOffLine1", "Roleplay isn't working here: the server can't read its rp/server.txt." );
		trap->Cvar_Set( "ui_rpOffLine2", "No logging in until its owner mends it (its console says how)." );
	} else {
		trap->Cvar_Set( "ui_rpOffLine1", "Roleplay is off here for now: everyone plays as themselves." );
		trap->Cvar_Set( "ui_rpOffLine2", "You can still log in. The owner can turn it on, on the Server page." );
	}
	trap->Cvar_Set( "ui_rpAccountLine", va( "Account: %s%s", account, admin ? " (admin)" : "" ) );
	// who makes admins: the server's console alone - yours, hosting (and rcon there too, if the game
	// has an rcon password: one cvar, rconPassword, the game's and the server's - typed for another
	// server on this page, say); another server's by rcon (a player's own rp admin: turned away, g_vrrp.c)
	if ( UI_VRRP_Local() ) {
		char rcon[MAX_CVAR_VALUE_STRING];
		UI_VRRP_Str( "rconPassword", rcon, sizeof( rcon ) );
		trap->Cvar_Set( "ui_rpAdminWhoLine", rcon[0] ? "You host this game: only you, or someone with its rcon password, can make admins." :
			"You host this game, so only you can make admins here." );
	} else {
		trap->Cvar_Set( "ui_rpAdminWhoLine", "Only someone with this server's rcon password (above) can make admins." );
	}
	if ( vrrp.page == RPP_ADMIN && admin && vrrp.admPart == RPA_INVASION && UI_VRRP_WarMode() ) {
		UI_VRRP_WarSum();
	}
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
	trap->Cvar_Set( "ui_rpEditForce", va( "%i", UI_VRRP_ForceLine() ) );
	if ( vrrp.page == RPP_PEOPLE && admin ) {
		UI_VRRP_PeopleForce();
	}
	trap->Cvar_Set( "ui_rpServerLine", server == 2 ? "Roleplay is on here" : server == 1 ? "Roleplay is off here" :
		server == 3 ? "Roleplay isn't working here: rp/server.txt can't be read (see the console)" : "This server doesn't have roleplay" );
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
	server = Com_Clampi( 0, 3, UI_VRRP_Int( "ui_rpServer" ) );
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
	if ( vrrp.admPart == RPA_INVASION && UI_VRRP_Int( "ui_rpInvAble" ) != 1 ) {
		vrrp.admPart = RPA_CHEATS;	// (a server without them - told nothing yet: the cheats)
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

// The Faction picked in its row (cvar): its word for the game (vrrpSideWords)
// - "": none, its NPC file's (a server that takes none: the row's not there).
static const char *UI_VRRP_Side( const char *cvar ) {
	const int n = UI_VRRP_Int( cvar );

	if ( UI_VRRP_Int( "ui_rpAdminSides" ) != 1 || n <= 0 || n >= (int)ARRAY_LEN( vrrpSideWords ) ) {
		return "";
	}
	return vrrpSideWords[n];
}

// What was set here last, or the default.
static void UI_VRRP_Keep( const char *cvar, const char *def ) {
	char now[16];

	UI_VRRP_Str( cvar, now, sizeof( now ) );
	if ( !now[0] ) {
		trap->Cvar_Set( cvar, def );
	}
}

// A setting of the game you host, for the Server page (its default where
// the game's not made it yet), kept between low and high.
static void UI_VRRP_LocalKeep( const char *cvar, const char *setting, const char *def, int low, int high ) {
	char value[16];

	UI_VRRP_Str( setting, value, sizeof( value ) );
	trap->Cvar_Set( cvar, va( "%i", Com_Clampi( low, high, atoi( value[0] ? value : def ) ) ) );
}

// The settings of the server you're on, for the Server page: as its
// serverinfo has them ("vrRP": on, characters, talk range, down,
// companions out with each player, on the server at once, their looks -
// g_vrrp.c); a server older than this doesn't say (or not all): what was
// set here last, or the defaults.
static void UI_VRRP_FromServer( void ) {
	char info[MAX_INFO_STRING];
	int on, chars, range, downed, comps, compsMax, models, grey, master;
	int got;

	info[0] = 0;
	trap->GetConfigString( CS_SERVERINFO, info, sizeof( info ) );
	got = sscanf( Info_ValueForKey( info, "vrRP" ), "%d %d %d %d %d %d %d %d %d", &on, &chars, &range, &downed, &comps, &compsMax, &models,
		&grey, &master );
	if ( got >= 4 ) {
		trap->Cvar_Set( "ui_rpSrvChars", va( "%i", Com_Clampi( 1, VRRP_CHARS_MAX, chars ) ) );
		trap->Cvar_Set( "ui_rpSrvRange", va( "%i", range ) );
		trap->Cvar_Set( "ui_rpSrvDowned", downed ? "1" : "0" );
	} else {
		UI_VRRP_Keep( "ui_rpSrvChars", "5" );
		UI_VRRP_Keep( "ui_rpSrvRange", "510" );
		UI_VRRP_Keep( "ui_rpSrvDowned", "1" );
	}
	if ( got >= 7 ) {
		trap->Cvar_Set( "ui_rpSrvComps", va( "%i", Com_Clampi( 0, RP_COMP_OUT_MAX, comps ) ) );
		trap->Cvar_Set( "ui_rpSrvCompsMax", va( "%i", Com_Clampi( 0, VRRP_COMPS_MOST, compsMax ) ) );
		trap->Cvar_Set( "ui_rpSrvCompModels", models ? "1" : "0" );
	} else {
		UI_VRRP_Keep( "ui_rpSrvComps", "2" );
		UI_VRRP_Keep( "ui_rpSrvCompsMax", "16" );
		UI_VRRP_Keep( "ui_rpSrvCompModels", "1" );
	}
	if ( got == 9 ) {
		trap->Cvar_Set( "ui_rpSrvGrey", va( "%i", Com_Clampi( 0, 2, grey ) ) );
		trap->Cvar_Set( "ui_rpSrvMaster", master ? "1" : "0" );
	} else {
		UI_VRRP_Keep( "ui_rpSrvGrey", "0" );
		UI_VRRP_Keep( "ui_rpSrvMaster", "0" );
	}
	// (VR players' arms for the other builds: its own key - g_vrarms.c's g_vrArmsForAll)
	{
		const char *arms = Info_ValueForKey( info, "g_vrArmsForAll" );
		if ( arms[0] ) {
			trap->Cvar_Set( "ui_rpSrvArms", atoi( arms ) ? "1" : "0" );
		} else {
			UI_VRRP_Keep( "ui_rpSrvArms", "1" );
		}
	}
}

// A page shown: what it needs, got.
static void UI_VRRP_Page( int page ) {
	vrrp.page = Com_Clampi( 0, RPP_PAGES - 1, page );
	vrrp.newAccount = vrrp.password = qfalse;
	vrrp.srvPart = RPS_SETTINGS;
	vrrp.admPart = RPA_CHEATS;
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
			UI_VRRP_LocalKeep( "ui_rpSrvComps", "g_rpCompanions", "2", 0, RP_COMP_OUT_MAX );
			UI_VRRP_LocalKeep( "ui_rpSrvCompsMax", "g_rpCompanionsMax", "16", 0, VRRP_COMPS_MOST );
			UI_VRRP_LocalKeep( "ui_rpSrvCompModels", "g_rpCompanionModels", "1", 0, 1 );
			UI_VRRP_LocalKeep( "ui_rpSrvGrey", "g_rpGrey", "0", 0, 2 );
			UI_VRRP_LocalKeep( "ui_rpSrvMaster", "g_rpForceMaster", "0", 0, 1 );
			UI_VRRP_LocalKeep( "ui_rpSrvArms", "g_vrArmsForAll", "1", 0, 1 );
		} else {
			UI_VRRP_FromServer();	// (another server's: as its serverinfo has them)
		}
		break;
	case RPP_ADMIN:
		UI_VRRP_Keep( "ui_rpAdmSide", "0" );	// (a spawn's Faction: its NPC file's)
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
	} else if ( !Q_stricmp( what, "force" ) ) {
		Menus_CloseByName( VRRP_MENU );	// (to the game's own Force menu: what's set there while playing one is the character's)
		Menus_ActivateByName( "ingame_playerforce" );
	} else if ( !Q_stricmp( what, "companions" ) ) {
		Menus_CloseByName( VRRP_MENU );	// (to the Companions menu: its Back comes here)
		UI_VRComp_Open( 1 );
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
			UI_VRRP_ServerSet( "g_rpGrey", va( "%i", Com_Clampi( 0, 2, UI_VRRP_Int( "ui_rpSrvGrey" ) ) ) );
			UI_VRRP_ServerSet( "g_rpForceMaster", UI_VRRP_Int( "ui_rpSrvMaster" ) ? "1" : "0" );
			UI_VRRP_ServerSet( "g_vrArmsForAll", UI_VRRP_Int( "ui_rpSrvArms" ) ? "1" : "0" );
		} else if ( !Q_stricmp( how, "compApply" ) ) {
			UI_VRRP_ServerSet( "g_rpCompanions", va( "%i", Com_Clampi( 0, RP_COMP_OUT_MAX, UI_VRRP_Int( "ui_rpSrvComps" ) ) ) );
			UI_VRRP_ServerSet( "g_rpCompanionsMax", va( "%i", Com_Clampi( 0, VRRP_COMPS_MOST, UI_VRRP_Int( "ui_rpSrvCompsMax" ) ) ) );
			UI_VRRP_ServerSet( "g_rpCompanionModels", UI_VRRP_Int( "ui_rpSrvCompModels" ) ? "1" : "0" );
		} else if ( !Q_stricmp( how, "admins" ) || !Q_stricmp( how, "settings" ) || !Q_stricmp( how, "companions" ) ) {
			vrrp.srvPart = !Q_stricmp( how, "admins" ) ? RPS_ADMINS : !Q_stricmp( how, "companions" ) ? RPS_COMPANIONS : RPS_SETTINGS;
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
			const char *side = npc->vehicle ? "" : UI_VRRP_Side( "ui_rpAdmSide" );	// (a vehicle takes none)
			UI_VRRP_Exec( va( "rpui admin spawn %s %s%s%s\n", npc->name, npc->vehicle ? "vehicle" : "npc", side[0] ? " " : "", side ) );
		} else {
			trap->Cvar_Set( "ui_rpMessage", vrrpNPCs.count ? "Nothing in the list matches what's in Find" : "No NPCs found in the game's files" );
		}
	} else if ( !Q_stricmp( what, "admin" ) ) {
		const char *how;
		if ( String_Parse( args, &how ) && ( !Q_stricmp( how, "killmine" ) || !Q_stricmp( how, "killall" ) || !Q_stricmp( how, "god" ) ||
			!Q_stricmp( how, "noclip" ) || !Q_stricmp( how, "notarget" ) || !Q_stricmp( how, "give" ) || !Q_stricmp( how, "forcepowers" ) ) ) {
			UI_VRRP_Exec( va( "rpui admin %s\n", how ) );
		}
	} else if ( !Q_stricmp( what, "invasion" ) ) {
		// the Admin page's Invasion part (an admin's: rpui admin invasion - cg_vrrp.c, g_vrinvade.c)
		const char *how;
		if ( !String_Parse( args, &how ) ) {
			return qtrue;
		}
		if ( !Q_stricmp( how, "open" ) ) {
			char cvar[MAX_QPATH];
			int s;
			vrrp.admPart = RPA_INVASION;
			UI_VRRP_Keep( "ui_rpInvMode", "0" );	// (an invasion of Reborn, mixed; a war of Rebels against Stormtroopers, mixed)
			UI_VRRP_Keep( "ui_rpWarSide", "0" );
			for ( n = 0; n < VRRP_INV_ROWS; n++ ) {
				for ( s = -1; s < VRRP_WAR_SIDES; s++ ) {
					UI_VRRP_RowCvar( s, n, "Kind", cvar, sizeof( cvar ) );
					UI_VRRP_Keep( cvar, n ? va( "%i", VRRP_INV_NOBODY ) : ( s < 0 ) ? "0" : ( s == 0 ) ? "2" : ( s == 1 ) ? "1" : va( "%i", VRRP_INV_NOBODY ) );
					UI_VRRP_RowCvar( s, n, "Each", cvar, sizeof( cvar ) );
					UI_VRRP_Keep( cvar, "0" );
				}
			}
			UI_VRRP_Keep( "ui_rpInvWaves", "4" );	// (4 waves of 6, 16 up at most, the next 20 s after one's beaten)
			UI_VRRP_Keep( "ui_rpInvPer", "6" );
			UI_VRRP_Keep( "ui_rpInvCap", "16" );
			UI_VRRP_Keep( "ui_rpInvNext", "0" );
			UI_VRRP_Keep( "ui_rpInvWait", "20" );
			UI_VRRP_Keep( "ui_rpInvSide", "0" );	// (its NPC file's)
			trap->Cvar_Set( "ui_rpMessage", "" );
		} else if ( !Q_stricmp( how, "back" ) ) {
			vrrp.admPart = RPA_CHEATS;
			trap->Cvar_Set( "ui_rpMessage", "" );
		} else if ( !Q_stricmp( how, "stop" ) ) {
			UI_VRRP_Exec( "rpui admin invasion stop\n" );
		} else if ( !Q_stricmp( how, "start" ) ) {
			char who[MAX_CVAR_VALUE_STRING];
			n = UI_VRRP_Lineup( -1, "", who, sizeof( who ) );
			if ( n < 0 ) {
				return qtrue;	// (it says why)
			}
			if ( !n ) {
				trap->Cvar_Set( "ui_rpMessage", "Pick who invades" );
				return qtrue;
			}
			{
				const char *side = UI_VRRP_Side( "ui_rpInvSide" );
				UI_VRRP_Exec( va( "rpui admin invasion start %s %i %i %i %s %i%s%s\n", who, UI_VRRP_Int( "ui_rpInvWaves" ), UI_VRRP_Int( "ui_rpInvPer" ),
					UI_VRRP_Int( "ui_rpInvCap" ), UI_VRRP_Int( "ui_rpInvNext" ) ? "every" : "cleared", UI_VRRP_Int( "ui_rpInvWait" ), side[0] ? " " : "", side ) );
			}
		}
	} else if ( !Q_stricmp( what, "war" ) ) {
		// its War mode (rpui admin war - cg_vrrp.c, g_vrinvade.c): each side's lineup ("-": not in it), the settings the invasions'
		const char *how;
		if ( !String_Parse( args, &how ) || !UI_VRRP_Wars() ) {
			return qtrue;
		}
		if ( !Q_stricmp( how, "stop" ) ) {
			UI_VRRP_Exec( "rpui admin war stop\n" );
		} else if ( !Q_stricmp( how, "start" ) ) {
			char who[VRRP_WAR_SIDES][MAX_CVAR_VALUE_STRING];
			int s, sides = 0;
			for ( s = 0; s < VRRP_WAR_SIDES; s++ ) {
				char whose[32];
				Com_sprintf( whose, sizeof( whose ), "%s: ", vrrpWarTitles[s] );
				n = UI_VRRP_Lineup( s, whose, who[s], sizeof( who[s] ) );
				if ( n < 0 ) {
					trap->Cvar_Set( "ui_rpWarSide", va( "%i", s ) );	// (the side it's in, shown - it says why)
					return qtrue;
				}
				if ( n ) {
					sides++;
				} else {
					Q_strncpyz( who[s], "-", sizeof( who[s] ) );
				}
			}
			if ( sides < 2 ) {
				trap->Cvar_Set( "ui_rpMessage", "A war's of two sides at least: pick who fights for another in Lineup of" );
				return qtrue;
			}
			UI_VRRP_Exec( va( "rpui admin war start %s %s %s %i %i %i %s %i\n", who[0], who[1], who[2], UI_VRRP_Int( "ui_rpInvWaves" ), UI_VRRP_Int( "ui_rpInvPer" ),
				UI_VRRP_Int( "ui_rpInvCap" ), UI_VRRP_Int( "ui_rpInvNext" ) ? "every" : "cleared", UI_VRRP_Int( "ui_rpInvWait" ) ) );
		}
	} else if ( !Q_stricmp( what, "people" ) ) {
		// an admin's: the character of the one picked a Grey Jedi, a Force Master, or neither (rp force)
		const char *how;
		if ( String_Parse( args, &how ) && ( !Q_stricmp( how, "grey" ) || !Q_stricmp( how, "master" ) || !Q_stricmp( how, "normal" ) ) ) {
			if ( ( n = UI_VRRP_PickedPerson() ) < 0 ) {
				trap->Cvar_Set( "ui_rpMessage", "No one's here" );
			} else if ( UI_VRRP_Who( vrrp.people[n], NULL, 0 ) == -1 ) {
				trap->Cvar_Set( "ui_rpMessage", va( "%s" S_COLOR_WHITE " plays as themselves: no character to make one", vrrp.names[n] ) );
			} else {
				UI_VRRP_Exec( va( "rpui admin force %i %s\n", vrrp.people[n], how ) );
			}
		}
	}
	return qtrue;
}
