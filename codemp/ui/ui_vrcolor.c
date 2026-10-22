/*
===========================================================================
OpenJK VR (multiplayer) - the Saber Colour page (vr/ui/vr_saber.menu): any
colour for each saber, and a black core (bg_vrcolor.h).

It opens from the RGB Colors button on the Lightsaber page - in game
(ingame_saber) and in the main menu (saberMenu) - and from Setup's VR page.
It colours one saber at a time (First Saber; Second Saber with two): Red,
Green and Blue sliders (0-255), a Hex field, the six stock colours, a White
or Black core, the blade shown as it'll look. Its Apply sets them
(saberRGB1/2, saberCore1/2, and color1/2 to the stock colour nearest,
which the normal game shows) - and the Lightsaber page's colours with them.

The Lightsaber page goes on as ever: a swatch there picks a stock colour
(the saber's own colour gone once its Apply Changes sets it), and its
Apply Changes sets the colours of their own as they stand.

The page is in front of the one it opened from (a popup: the pointer's
its alone), which is left as it was for when it closes. In game it takes
that one's place: the page under it shows nothing while it's open. Over a
main menu page (one that fills the screen) it's a box of its own, that
page's sabers hidden while it's there (they'd turn around it).

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "ui_local.h"

extern void		Menu_ShowItemByName( menuDef_t *menu, const char *p, qboolean bShow );
void			UI_ParseMenu( const char *menuFile );

#define VRSC_FILE			"ui/vr_saber.menu"
#define VRSC_MENU			"vr_saber"
#define VRSC_FILL			"vrsc_fill"				// the page's box, filled in: over a main menu page
#define VRSC_INGAME_ITEMS	"vr_saber_ingame"		// its button, for the in-game Lightsaber page...
#define VRSC_MAIN_ITEMS		"vr_saber_main"			// ...and the main menu's
#define VRSC_INGAME_BUTTON	"vr_saber_rgb"
#define VRSC_MAIN_BUTTON	"vr_saber_rgb2"

typedef struct {
	vrBladeColor_t	c;		// its colour of its own (c.rgb) and black core
	int				stock;	// its stock colour (SABER_*): when it has none of its own
} vrscSaber_t;

static struct {
	vrscSaber_t	work[2];		// the Lightsaber page's, as they stand there (until its Apply Changes)
	qboolean	workLoaded;
	vrscSaber_t	page[2];		// the Saber Colour page's (until its Apply)
	int			saber;			// the saber it's colouring: 0 or 1
	int			shown[3];		// the sliders as last set or seen
	char		shownHex[16];	// the Hex field as last set or seen
	int			dual;			// ui_sc_dual as set: there's a second saber
	itemDef_t	*hidden[MAX_MENUITEMS];	// what's hidden of the page under it, while it's open
	int			numHidden;
} vrsc;

static const char *vrscSliders[3] = { "ui_sc_r", "ui_sc_g", "ui_sc_b" };

// The stock colours as the game names them, and as their light shines (the
// colour each swatch puts on the sliders; bg_vrcolor.h's nearest of each is
// itself).
static const char *vrscStockNames[NUM_SABER_COLORS] = { "red", "orange", "yellow", "green", "blue", "purple" };
static const byte vrscStockRGB[NUM_SABER_COLORS][3] = {
	{ 255, 51, 51 }, { 255, 128, 26 }, { 255, 255, 51 }, { 51, 255, 51 }, { 51, 102, 255 }, { 230, 51, 255 } };

static int UI_VRSC_Stock( int c ) {
	return ( c >= 0 && c < NUM_SABER_COLORS ) ? c : SABER_BLUE;
}

// Both sabers' colours as they're set.
static void UI_VRSC_FromSettings( vrscSaber_t out[2] ) {
	int n;

	for ( n = 0; n < 2; n++ ) {
		char rgb[MAX_CVAR_VALUE_STRING], core[MAX_CVAR_VALUE_STRING];
		trap->Cvar_VariableStringBuffer( n ? "saberRGB2" : "saberRGB1", rgb, sizeof( rgb ) );
		trap->Cvar_VariableStringBuffer( n ? "saberCore2" : "saberCore1", core, sizeof( core ) );
		BG_VRSC_FromSettings( rgb, core, &out[n].c );
		out[n].stock = UI_VRSC_Stock( (int)trap->Cvar_VariableValue( n ? "color2" : "color1" ) );
	}
}

// Sets both sabers' colours: each one's own (or none), its core, and its
// stock colour - the nearest to its own - the Lightsaber page's with it.
static void UI_VRSC_Set( const vrscSaber_t s[2] ) {
	int n;

	for ( n = 0; n < 2; n++ ) {
		const int stock = s[n].c.rgb ? BG_VRSC_Nearest( s[n].c.color ) : UI_VRSC_Stock( s[n].stock );
		char rgb[16];
		if ( s[n].c.rgb ) {
			Com_sprintf( rgb, sizeof( rgb ), "%02x%02x%02x", s[n].c.color[0], s[n].c.color[1], s[n].c.color[2] );
		} else {
			rgb[0] = '\0';
		}
		trap->Cvar_Set( n ? "saberRGB2" : "saberRGB1", rgb );
		trap->Cvar_Set( n ? "saberCore2" : "saberCore1", s[n].c.blackCore ? "1" : "0" );
		trap->Cvar_Set( n ? "color2" : "color1", va( "%i", stock ) );
		trap->Cvar_Set( n ? "g_saber2_color" : "g_saber_color", vrscStockNames[stock] );
		trap->Cvar_Set( n ? "ui_saber2_color" : "ui_saber_color", vrscStockNames[stock] );
	}
}

// The page's tabs: the saber it's colouring white, the other as the game's
// unchosen buttons are.
static void UI_VRSC_Tabs( void ) {
	menuDef_t *menu = Menus_FindByName( VRSC_MENU );
	int n;

	for ( n = 0; n < 2; n++ ) {
		itemDef_t *it = menu ? Menu_FindItemByName( menu, n ? "vrsc_saber2" : "vrsc_saber1" ) : NULL;
		if ( it ) {
			if ( n == vrsc.saber ) {
				VectorSet4( it->window.foreColor, 1.0f, 1.0f, 1.0f, 1.0f );
			} else {
				VectorSet4( it->window.foreColor, 0.65f, 0.65f, 1.0f, 1.0f );
			}
		}
	}
}

// The page's rows as the saber it's colouring has them: its own colour, or
// its stock one's on the sliders.
static void UI_VRSC_Show( void ) {
	const vrscSaber_t *s = &vrsc.page[vrsc.saber];
	const byte *rgb = s->c.rgb ? s->c.color : vrscStockRGB[UI_VRSC_Stock( s->stock )];
	int k;

	for ( k = 0; k < 3; k++ ) {
		vrsc.shown[k] = rgb[k];
		trap->Cvar_Set( vrscSliders[k], va( "%i", rgb[k] ) );
	}
	Com_sprintf( vrsc.shownHex, sizeof( vrsc.shownHex ), "%02X%02X%02X", rgb[0], rgb[1], rgb[2] );
	trap->Cvar_Set( "ui_sc_hex", vrsc.shownHex );
	trap->Cvar_Set( "ui_sc_core", s->c.blackCore ? "1" : "0" );
	trap->Cvar_Set( "ui_sc_saber", va( "%i", vrsc.saber + 1 ) );
	UI_VRSC_Tabs();
}

// Whether there's a second saber (ui_saber2, as the Lightsaber page has it).
static qboolean UI_VRSC_Dual( void ) {
	char saber2[MAX_QPATH];

	trap->Cvar_VariableStringBuffer( "ui_saber2", saber2, sizeof( saber2 ) );
	return (qboolean)( saber2[0] && Q_stricmp( saber2, "none" ) );
}

static qboolean UI_VRSC_Shown( void ) {
	const menuDef_t *menu = Menus_FindByName( VRSC_MENU );
	return (qboolean)( menu && ( menu->window.flags & WINDOW_VISIBLE ) );
}

// What's been moved or typed on the page is the saber's colour of its own
// (the sliders moved: theirs, the Hex field following; else six hex digits
// typed: theirs, the sliders following), and its core what's chosen. Each
// frame, and before the page's buttons do anything (so what changed the
// same frame counts).
static void UI_VRSC_Sync( void ) {
	vrscSaber_t *s = &vrsc.page[vrsc.saber];
	char hex[MAX_CVAR_VALUE_STRING];
	int k, now[3];
	qboolean moved = qfalse;

	for ( k = 0; k < 3; k++ ) {
		now[k] = Com_Clampi( 0, 255, (int)( trap->Cvar_VariableValue( vrscSliders[k] ) + 0.5f ) );
		if ( now[k] != vrsc.shown[k] ) {
			moved = qtrue;
		}
	}
	if ( moved ) {
		for ( k = 0; k < 3; k++ ) {
			s->c.color[k] = (byte)now[k];
			vrsc.shown[k] = now[k];
		}
		s->c.rgb = qtrue;
		Com_sprintf( vrsc.shownHex, sizeof( vrsc.shownHex ), "%02X%02X%02X", now[0], now[1], now[2] );
		trap->Cvar_Set( "ui_sc_hex", vrsc.shownHex );
	} else {
		trap->Cvar_VariableStringBuffer( "ui_sc_hex", hex, sizeof( hex ) );
		if ( strcmp( hex, vrsc.shownHex ) ) {
			byte c[3];
			Q_strncpyz( vrsc.shownHex, hex, sizeof( vrsc.shownHex ) );
			if ( BG_VRSC_ParseRGB( hex, c ) ) {
				for ( k = 0; k < 3; k++ ) {
					s->c.color[k] = c[k];
					vrsc.shown[k] = c[k];
					trap->Cvar_Set( vrscSliders[k], va( "%i", c[k] ) );
				}
				s->c.rgb = qtrue;
			}
		}
	}
	s->c.blackCore = (qboolean)( trap->Cvar_VariableValue( "ui_sc_core" ) > 0.0f );
}

// Each frame: whether there's a second saber, for the page; and on the page,
// what's been moved or typed (UI_VRSC_Sync).
void UI_VRColor_Frame( void ) {
	const int dual = UI_VRSC_Dual() ? 1 : 0;

	if ( dual != vrsc.dual || (int)trap->Cvar_VariableValue( "ui_sc_dual" ) != dual ) {
		vrsc.dual = dual;
		trap->Cvar_Set( "ui_sc_dual", dual ? "1" : "0" );
	}
	if ( !UI_VRSC_Shown() ) {
		return;
	}
	if ( !dual && vrsc.saber ) {
		vrsc.saber = 0;		// (no second saber now: the first)
		UI_VRSC_Show();
	}
	UI_VRSC_Sync();
}

// What was hidden of the page under it shown again (as it closes, however it
// does: the page under it's as it was - though closed with the rest, in game,
// it's closed too).
static void UI_VRSC_Uncover( void ) {
	int i;

	for ( i = 0; i < vrsc.numHidden; i++ ) {
		vrsc.hidden[i]->window.flags |= WINDOW_VISIBLE;
	}
	vrsc.numHidden = 0;
}

// Opens the page in front of the one with the focus (that opened it), and
// hides what of that one would show around it: in game, all of it; on a
// main menu page, its sabers (its box filled in instead). Closing it gives
// the focus back to that one (the menus' own stack of them).
static void UI_VRSC_Open( void ) {
	menuDef_t *page = Menus_FindByName( VRSC_MENU ), *under = Menu_GetFocused();
	int i;

	if ( !page ) {
		return;
	}
	UI_VRSC_Uncover();
	if ( under == page ) {
		under = NULL;	// (open already)
	}
	if ( under ) {
		Menu_HandleMouseMove( under, -1000.0f, -1000.0f );	// (the pointer off it: its highlights out)
	}
	Menu_ShowItemByName( page, VRSC_FILL, (qboolean)( under && under->fullScreen ) );
	Menus_ActivateByName( VRSC_MENU );
	for ( i = 0; under && i < under->itemCount; i++ ) {
		itemDef_t *it = under->items[i];
		if ( ( it->window.flags & WINDOW_VISIBLE ) && ( !under->fullScreen || it->type == ITEM_TYPE_MODEL ) ) {
			it->window.flags &= ~WINDOW_VISIBLE;
			vrsc.hidden[vrsc.numHidden++] = it;
		}
	}
}

// Back to the page it opened from, as it was.
static void UI_VRSC_Back( void ) {
	menuDef_t *page = Menus_FindByName( VRSC_MENU ), *under;

	if ( !page || !( page->window.flags & WINDOW_VISIBLE ) ) {
		return;
	}
	UI_VRSC_Uncover();
	Menus_CloseByName( VRSC_MENU );
	under = Menu_GetFocused();
	if ( under ) {
		Menu_HandleMouseMove( under, (float)uiInfo.uiDC.cursorx, (float)uiInfo.uiDC.cursory );
	}
}

// uiScript vrSaberColor ...
//   open <1: from the Lightsaber page, as it stands there; 0: as set>
//                        opens the page
//   saber <1 or 2>       colour that one
//   pick <SABER_*>       the page's stock colour swatch: that, none of its own
//   apply                the page's Apply
//   back                 closes it: back to the page it opened from
//   closed               (its onClose: however it closes)
//   stock <1 or 2> <SABER_*>   a Lightsaber page's swatch (added to its own
//                        action): that saber's stock colour, none of its own
qboolean UI_VRColor_Script( const char *name, char **args ) {
	const char *what;
	int n, c;

	if ( Q_stricmp( name, "vrSaberColor" ) ) {
		return qfalse;
	}
	if ( !String_Parse( args, &what ) ) {
		return qtrue;
	}
	if ( !Q_stricmp( what, "open" ) ) {
		if ( Int_Parse( args, &n ) && n && vrsc.workLoaded ) {
			memcpy( vrsc.page, vrsc.work, sizeof( vrsc.page ) );
		} else {
			UI_VRSC_FromSettings( vrsc.page );
		}
		vrsc.saber = 0;
		UI_VRSC_Show();
		UI_VRSC_Open();
	} else if ( !Q_stricmp( what, "back" ) ) {
		UI_VRSC_Back();
	} else if ( !Q_stricmp( what, "closed" ) ) {
		UI_VRSC_Uncover();
	} else if ( !Q_stricmp( what, "saber" ) ) {
		if ( Int_Parse( args, &n ) ) {
			if ( UI_VRSC_Shown() ) {
				UI_VRSC_Sync();		// (the one it was colouring: as it stands)
			}
			vrsc.saber = ( n == 2 && UI_VRSC_Dual() ) ? 1 : 0;
			UI_VRSC_Show();
		}
	} else if ( !Q_stricmp( what, "pick" ) ) {
		if ( Int_Parse( args, &c ) ) {
			if ( UI_VRSC_Shown() ) {
				UI_VRSC_Sync();		// (its core: as it stands)
			}
			vrsc.page[vrsc.saber].c.rgb = qfalse;
			vrsc.page[vrsc.saber].stock = UI_VRSC_Stock( c );
			UI_VRSC_Show();
		}
	} else if ( !Q_stricmp( what, "apply" ) ) {
		if ( UI_VRSC_Shown() ) {
			UI_VRSC_Sync();
		}
		UI_VRSC_Set( vrsc.page );
		memcpy( vrsc.work, vrsc.page, sizeof( vrsc.work ) );
		vrsc.workLoaded = qtrue;
	} else if ( !Q_stricmp( what, "stock" ) ) {
		if ( Int_Parse( args, &n ) && Int_Parse( args, &c ) && n >= 1 && n <= 2 && vrsc.workLoaded ) {
			vrsc.work[n - 1].c.rgb = qfalse;
			vrsc.work[n - 1].stock = UI_VRSC_Stock( c );
		}
	}
	return qtrue;
}

// getsabercvars (a Lightsaber page opened): its colours as they're set.
void UI_VRColor_StockOpened( void ) {
	UI_VRSC_FromSettings( vrsc.work );
	vrsc.workLoaded = qtrue;
}

// updatesabercvars (its Apply Changes, which sets color1 and color2 itself):
// the colours of their own and cores as they stand there.
void UI_VRColor_StockApplied( void ) {
	int n;

	if ( !vrsc.workLoaded ) {
		return;
	}
	for ( n = 0; n < 2; n++ ) {
		const vrscSaber_t *s = &vrsc.work[n];
		char rgb[16];
		if ( s->c.rgb ) {
			Com_sprintf( rgb, sizeof( rgb ), "%02x%02x%02x", s->c.color[0], s->c.color[1], s->c.color[2] );
		} else {
			rgb[0] = '\0';
		}
		trap->Cvar_Set( n ? "saberRGB2" : "saberRGB1", rgb );
		trap->Cvar_Set( n ? "saberCore2" : "saberCore1", s->c.blackCore ? "1" : "0" );
	}
}

// The saber previews (ui_saber.c): a saber's colour of its own and black core
// as the page they're on has them - the Saber Colour page's (its stock colour
// too, into stock), else the Lightsaber page's as they stand (its stock colour
// is its own: ui_saber_color, stock left as it is).
void UI_VRColor_Blade( int saber, vrBladeColor_t *out, int *stock ) {
	const int n = saber ? 1 : 0;

	if ( UI_VRSC_Shown() ) {
		*out = vrsc.page[n].c;
		*stock = UI_VRSC_Stock( vrsc.page[n].stock );
	} else if ( vrsc.workLoaded ) {
		*out = vrsc.work[n].c;
	} else {
		vrscSaber_t s[2];
		UI_VRSC_FromSettings( s );
		*out = s[n].c;
	}
}

// Whether a menu has room for the items of another (at 0, 0: where they say
// is on the screen) - nothing there that's words, a setting, a list or a
// button.
static qboolean UI_VRSC_Room( const menuDef_t *dst, const menuDef_t *src ) {
	int i, j;

	for ( i = 0; i < src->itemCount; i++ ) {
		const rectDef_t *a = &src->items[i]->window.rectClient;
		const float ax = a->x - dst->window.rect.x, ay = a->y - dst->window.rect.y;	// (in the other menu)
		if ( src->items[i]->window.flags & WINDOW_DECORATION ) {
			continue;
		}
		for ( j = 0; j < dst->itemCount; j++ ) {
			const itemDef_t *it = dst->items[j];
			const rectDef_t *b = &it->window.rectClient;
			if ( ( it->window.flags & WINDOW_DECORATION ) || ( !it->text && !it->cvar && it->type != ITEM_TYPE_LISTBOX &&
				it->type != ITEM_TYPE_BUTTON && it->type != ITEM_TYPE_OWNERDRAW ) ) {
				continue;	// (a backdrop, a highlight, a preview)
			}
			if ( ax < b->x + b->w && ax + a->w > b->x && ay < b->y + b->h && ay + a->h > b->y ) {
				return qfalse;
			}
		}
	}
	return qtrue;
}

// A Lightsaber page gets the RGB Colors button where there's room for it,
// and its swatches say which stock colour they pick.
static void UI_VRSC_AddTo( const char *items, const char *to, const char *button ) {
	static const char *swatches[NUM_SABER_COLORS] = { "redicon", "orangeicon", "yellowicon", "greenicon", "blueicon", "purpleicon" };
	menuDef_t *src = Menus_FindByName( items ), *dst = Menus_FindByName( to );
	int n, c;

	if ( !dst ) {
		return;
	}
	if ( src && src->itemCount && !Menu_FindItemByName( dst, button ) ) {
		if ( UI_VRSC_Room( dst, src ) ) {
			UI_VRRP_Move( items, to, button, qfalse );
		} else {
			Com_Printf( "The menu %s has no room for the RGB Colors button\n", to );
		}
	}
	for ( n = 0; n < 2; n++ ) {
		for ( c = 0; c < NUM_SABER_COLORS; c++ ) {
			char name[MAX_QPATH], script[1024];
			itemDef_t *it;
			Com_sprintf( name, sizeof( name ), "%s%s", swatches[c], n ? "2" : "" );
			it = Menu_FindItemByName( dst, name );
			if ( !it || ( it->action && strstr( it->action, "vrSaberColor" ) ) ) {
				continue;
			}
			Com_sprintf( script, sizeof( script ), "%s ; uiScript vrSaberColor stock %i %i ; ", it->action ? it->action : "", n + 1, c );
			it->action = String_Alloc( script );
		}
	}
}

// When the menus load (ui_main.c, each set): the page, and its button on the
// Lightsaber pages.
void UI_VRColor_AddMenus( void ) {
	if ( !Menus_FindByName( VRSC_MENU ) ) {
		UI_ParseMenu( VRSC_FILE );
		if ( !Menus_FindByName( VRSC_MENU ) ) {
			Com_Printf( S_COLOR_YELLOW "%s not found: no Saber Colour page\n", VRSC_FILE );
			return;
		}
	}
	UI_VRSC_AddTo( VRSC_INGAME_ITEMS, "ingame_saber", VRSC_INGAME_BUTTON );
	UI_VRSC_AddTo( VRSC_MAIN_ITEMS, "saberMenu", VRSC_MAIN_BUTTON );
	vrsc.workLoaded = qfalse;
	vrsc.numHidden = 0;		// (the menus are new)
}
