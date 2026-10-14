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

  Characters   logged out: log in, or make an account (the password twice);
               logged in: your characters (a list, feeder 65) - play one,
               stop, delete one (asked first), make one - log out, a new
               password
  Editor       the character you play: faction, description, bio (Save)
  People       who's here (feeder 66), with their faction and description,
               and the bio of the one picked
  Chat & cards speech bubbles, cards, remembering your login; how chat goes
  Help
  Server       the owner's: roleplay on or off, characters per account, how
               far talk reaches - straight to the game you host, by rcon to
               another server

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

enum { RPP_CHARS, RPP_EDIT, RPP_PEOPLE, RPP_CHAT, RPP_HELP, RPP_SERVER, RPP_PAGES };

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
} vrrp = { RPP_CHARS, qfalse, qfalse, -1, 0 };

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

int UI_VRRP_FeederCount( int feeder ) {
	if ( feeder == FEEDER_VR_RPCHARS ) {
		return UI_VRRP_NumChars();
	}
	if ( feeder == FEEDER_VR_RPPEOPLE ) {
		UI_VRRP_People();
		return vrrp.numPeople;
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
	}
	return text;
}

qboolean UI_VRRP_FeederSelection( int feeder, int index ) {
	if ( feeder == FEEDER_VR_RPCHARS ) {
		vrrp.deleting = -1;	// (another picked: not that one)
		return qtrue;
	}
	return (qboolean)( feeder == FEEDER_VR_RPPEOPLE );
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
// the pages', so whatever it is stays).
static int UI_VRRP_Shown( const char *group, int server, qboolean in, qboolean playing ) {
	const qboolean chars = (qboolean)( vrrp.page == RPP_CHARS );

	if ( !group || Q_stricmpn( group, "rp_", 3 ) ) {
		return -1;
	}
	if ( !Q_stricmp( group, "rp_out" ) ) {
		return chars && !in && server == 2;
	}
	if ( !Q_stricmp( group, "rp_off" ) ) {
		return chars && !in && server != 2;
	}
	if ( !Q_stricmp( group, "rp_login" ) ) {
		return chars && !in && server == 2 && !vrrp.newAccount;
	}
	if ( !Q_stricmp( group, "rp_new" ) ) {
		return chars && !in && server == 2 && vrrp.newAccount;
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
	return 0;
}

// The lines of text the menu shows (ui_rp... cvars), as things stand.
static void UI_VRRP_Lines( int server, const char *account, const char *playing ) {
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
		trap->Cvar_Set( "ui_rpOffLine2", "The server's owner can turn it on, on the Server page." );
	}
	trap->Cvar_Set( "ui_rpAccountLine", va( "Account: %s", account ) );
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
	qboolean in;

	if ( !menu || !( menu->window.flags & WINDOW_VISIBLE ) ) {
		return;
	}
	server = Com_Clampi( 0, 2, UI_VRRP_Int( "ui_rpServer" ) );
	UI_VRRP_Str( "ui_rpAccount", account, sizeof( account ) );
	UI_VRRP_Str( "ui_rpPlaying", playing, sizeof( playing ) );
	in = (qboolean)( account[0] != '\0' );
	if ( !in ) {
		vrrp.password = qfalse;
		vrrp.deleting = -1;
	}
	if ( vrrp.deleting >= UI_VRRP_NumChars() ) {
		vrrp.deleting = -1;	// (it went)
	}
	UI_VRRP_PickedChar();	// (the lists lit on a row there is)
	if ( vrrp.page == RPP_PEOPLE ) {
		UI_VRRP_PickedPerson();
	}
	UI_VRRP_Lines( server, account, playing );

	for ( i = 0; i < menu->itemCount; i++ ) {
		itemDef_t *it = menu->items[i];
		const int shown = UI_VRRP_Shown( it->window.group, server, in, (qboolean)( playing[0] != '\0' ) );
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

// A page shown: what it needs, got.
static void UI_VRRP_Page( int page ) {
	vrrp.page = Com_Clampi( 0, RPP_PAGES - 1, page );
	vrrp.newAccount = vrrp.password = qfalse;
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
			trap->Cvar_Set( "ui_rpSrvChars", va( "%i", Com_Clampi( 1, VRRP_CHARS_MAX, UI_VRRP_Int( "g_rpCharacters" ) ) ) );
			trap->Cvar_Set( "ui_rpSrvRange", va( "%i", UI_VRRP_Int( "g_rpTalkRange" ) ) );
		} else {
			char now[16];
			UI_VRRP_Str( "ui_rpSrvChars", now, sizeof( now ) );
			if ( !now[0] ) {
				trap->Cvar_Set( "ui_rpSrvChars", "5" );
				trap->Cvar_Set( "ui_rpSrvRange", "510" );
			}
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
		}
	}
	return qtrue;
}
