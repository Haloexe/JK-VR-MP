/*
===========================================================================
OpenJK VR (multiplayer) - the VR page of Setup (the main menu's
"setup_menu" and the in-game "ingame_setup"): the settings for playing in a
headset.

When the menus load, a "VR" button goes under the page's own buttons
(Video, More Video, Sound...), looking like them, and the settings in
ui/vr_setup.menu go where the page shows the others', laid out as its Sound
settings are and looking like them. Choosing VR shows them and hides
whatever the page's own buttons show; choosing any of those (or opening
Setup) hides them again. The page is otherwise left as it is.

  Left-Handed     guns and the saber in the left hand, the     vr_leftHanded Yes/No
                  Force on the right grip, Use on the left
  Finger Tracking your character's fingers follow yours       cg_vrFingers Yes/No
  Grip Press      how far a grip's pressed for Use and the     vr_gripPress 60%-95%
                  Force (a lighter squeeze only curls fingers)
  Body Bends      standing still, your character bends as you  cg_vrBodyBend Yes/No
                  do, feet planted - other VR players' as they
                  do (No: upright, as it was)
  Saber Inertia   how far a blade's weight holds it back      cg_vrSaberInertia 0-3
  Saber Weight    how strongly its weight is felt as it swings cg_vrSaberWeight 0-2
  Vibration       how strongly the controllers vibrate        cg_vrHaptics 0-2
  Holsters        your weapons hang on your body              cg_vrHolsters Yes/No
  Saber Grip      Hold: the saber's in your hand while you    cg_vrGripHold 1/0
                  hold its grip (let go at your side, it's
                  holstered; elsewhere, it drops - on servers
                  that drop them); Toggle: squeezed in, out
  Holster Reach   how far from a holster a hand reaches it    cg_vrHolsterReach 0.5-2
  Hilts Hang      emitter up, or down                         cg_vrHiltsDown 0/1
  Holster Markers a faint ring at each: off, near a hand,     cg_vrHolsterMarkers 0-2
                  always
  Quick Draw      the trigger held as a saber's taken out     cg_vrQuickDraw Yes/No
                  lights it
  Reset Holsters  (a button) the saber on the right hip, a    cg_vrHolsterLayout 1,2,0,0
                  second on the left, the shoulders empty
  Saber Colors    (a button) the RGB Colors page: any colour  (ui_vrcolor.c)
                  for each saber, a black core

The sliders show their value after them, and go in tenths; with the
keyboard, the arrow keys move the one picked (ui_shared.c: showValue), and
change the Yes/No and the choices (as Enter does: Item_YesNo_HandleKey,
Item_Multi_HandleKey). They're the game's own settings (the engine's, the
cgame's), registered here too so that set from the main menu, before a
game, they're kept.

A page with no room for the button or the settings (one a menu mod has
changed) is left as it is, and says so in the console; the settings still
work there.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "ui_local.h"

extern qboolean	Menu_Parse( int handle, menuDef_t *menu );
extern void		Menu_UpdatePosition( menuDef_t *menu );

#define VRS_MENU_FILE		"ui/vr_setup.menu"
#define VRS_GROUP			"vrsetup"				// the settings, on the page
#define VRS_BUTTON			"vrsetupbutton"			// the page's button for them...
#define VRS_GLOW			"vrsetupbutton_glow"	// ...and the light behind it
#define VRS_SUBMENU			"setup_submenu"			// the page's own buttons
#define VRS_BACKGROUND		"setup_background"		// what the page shows its settings on
#define VRS_MAX_ROWS		24
#define VRS_MAX_NAMES		32						// what the page's buttons show and hide
#define VRS_ROW_MIN			14.0f					// rows can't be closer together than this
#define VRS_ROW_GAP			2.0f					// rows made shorter to fit are this far apart (a pointer between them is on neither)

static const char *vrsPages[] = { "setup_menu", "ingame_setup" };

// the settings, as the game has them (cg_xcvar.h)
static const struct {
	const char	*name;
	const char	*value;
	uint32_t	flags;
} vrsCvars[] = {
	{ "vr_leftHanded",		"0",	CVAR_ARCHIVE },
	{ "cg_vrFingers",		"1",	CVAR_ARCHIVE },
	{ "vr_gripPress",		"0.85",	CVAR_ARCHIVE },
	{ "cg_vrBodyBend",		"1",	CVAR_ARCHIVE },
	{ "cg_vrSaberInertia",	"1",	CVAR_ARCHIVE_ND },
	{ "cg_vrSaberWeight",	"1",	CVAR_ARCHIVE_ND },
	{ "cg_vrHaptics",		"1",	CVAR_ARCHIVE },
	{ "cg_vrHolsters",		"1",	CVAR_ARCHIVE },
	{ "cg_vrGripHold",		"1",	CVAR_ARCHIVE },
	{ "cg_vrHolsterReach",	"1",	CVAR_ARCHIVE },
	{ "cg_vrHiltsDown",		"0",	CVAR_ARCHIVE },
	{ "cg_vrHolsterMarkers",	"1",	CVAR_ARCHIVE },
	{ "cg_vrQuickDraw",		"1",	CVAR_ARCHIVE },
	{ "cg_vrHolsterLayout",	"1,2,0,0",	CVAR_ARCHIVE },
};

/*
--------------------------------------------------------------------------------
What the page's own buttons do
--------------------------------------------------------------------------------
*/

typedef struct {
	char		names[VRS_MAX_NAMES][MAX_QPATH];	// what they show or hide (but the background)
	int			numNames;
	qboolean	background;							// one shows the background
	char		defer[2][MAX_QPATH];				// "defer VideoSetup videowarningMenu": ask before leaving the video settings
	qboolean	deferred;
	vec4_t		normal, chosen;						// the buttons' colour, and the chosen one's
	qboolean	haveNormal, haveChosen;
} vrsButtons_t;

static void UI_VRS_AddName( vrsButtons_t *b, const char *name ) {
	int i;

	if ( !name[0] || !Q_stricmp( name, VRS_GROUP ) ) {
		return;
	}
	if ( !Q_stricmp( name, VRS_BACKGROUND ) ) {
		b->background = qtrue;
		return;
	}
	for ( i = 0; i < b->numNames; i++ ) {
		if ( !Q_stricmp( b->names[i], name ) ) {
			return;
		}
	}
	if ( b->numNames < VRS_MAX_NAMES ) {
		Q_strncpyz( b->names[b->numNames++], name, sizeof( b->names[0] ) );
	}
}

// One button's action: what it shows and hides, whether it waits on the
// video settings, and the colours it gives the buttons. (Its commands, and
// what they're given, one after another - with a ";" between or not.)
static void UI_VRS_ReadAction( vrsButtons_t *b, const itemDef_t *button ) {
	const char *p = button->action;

	while ( p ) {
		char cmd[MAX_QPATH];
		Q_strncpyz( cmd, COM_ParseExt( &p, qtrue ), sizeof( cmd ) );
		if ( !p || !cmd[0] ) {
			break;
		}
		if ( !Q_stricmp( cmd, "show" ) || !Q_stricmp( cmd, "hide" ) ) {
			UI_VRS_AddName( b, COM_ParseExt( &p, qtrue ) );
		} else if ( !Q_stricmp( cmd, "defer" ) ) {
			char d[2][MAX_QPATH];
			Q_strncpyz( d[0], COM_ParseExt( &p, qtrue ), sizeof( d[0] ) );
			Q_strncpyz( d[1], COM_ParseExt( &p, qtrue ), sizeof( d[1] ) );
			if ( p && d[0][0] && d[1][0] && !b->deferred ) {
				Q_strncpyz( b->defer[0], d[0], sizeof( b->defer[0] ) );
				Q_strncpyz( b->defer[1], d[1], sizeof( b->defer[1] ) );
				b->deferred = qtrue;
			}
		} else if ( !Q_stricmp( cmd, "setitemcolor" ) ) {
			char which[MAX_QPATH], what[MAX_QPATH];
			vec4_t c;
			int k;
			Q_strncpyz( which, COM_ParseExt( &p, qtrue ), sizeof( which ) );
			Q_strncpyz( what, COM_ParseExt( &p, qtrue ), sizeof( what ) );
			for ( k = 0; k < 4 && p; k++ ) {
				c[k] = (float)atof( COM_ParseExt( &p, qtrue ) );
			}
			if ( k < 4 || !p || Q_stricmp( what, "forecolor" ) ) {
				continue;
			}
			if ( !Q_stricmp( which, VRS_SUBMENU ) ) {
				VectorCopy4( c, b->normal );
				b->haveNormal = qtrue;
			} else if ( button->window.name && !Q_stricmp( which, button->window.name ) ) {
				VectorCopy4( c, b->chosen );
				b->haveChosen = qtrue;
			}
		}
	}
}

// The VR button's action: as the others', but showing these.
static const char *UI_VRS_Action( const vrsButtons_t *b ) {
	char s[2048];
	int i;

	Q_strncpyz( s, "play \"sound/interface/button1.wav\" ; ", sizeof( s ) );
	if ( b->deferred ) {
		Q_strcat( s, sizeof( s ), va( "defer \"%s\" \"%s\" ; ", b->defer[0], b->defer[1] ) );
	}
	if ( b->background ) {
		Q_strcat( s, sizeof( s ), "show " VRS_BACKGROUND " ; " );
	}
	for ( i = 0; i < b->numNames; i++ ) {
		Q_strcat( s, sizeof( s ), va( "hide \"%s\" ; ", b->names[i] ) );
	}
	Q_strcat( s, sizeof( s ), "show " VRS_GROUP " ; " );
	if ( b->haveNormal ) {
		Q_strcat( s, sizeof( s ), va( "setitemcolor " VRS_SUBMENU " forecolor %g %g %g %g ; ", b->normal[0], b->normal[1], b->normal[2], b->normal[3] ) );
	}
	Q_strcat( s, sizeof( s ), va( "setitemcolor " VRS_BUTTON " forecolor %g %g %g %g ; ",
		b->haveChosen ? b->chosen[0] : 1.0f, b->haveChosen ? b->chosen[1] : 1.0f, b->haveChosen ? b->chosen[2] : 1.0f, b->haveChosen ? b->chosen[3] : 1.0f ) );
	return String_Alloc( s );
}

// A script that also hides these.
static const char *UI_VRS_AlsoHide( const char *script ) {
	return String_Alloc( va( "%s hide " VRS_GROUP " ; ", script ? script : "" ) );
}

/*
--------------------------------------------------------------------------------
Putting them on the page
--------------------------------------------------------------------------------
*/

static menuDef_t	vrsParsed;		// ui/vr_setup.menu's settings, parsed afresh for each page

static qboolean UI_VRS_Parse( void ) {
	pc_token_t token;
	qboolean ok = qfalse;
	const int handle = trap->PC_LoadSource( VRS_MENU_FILE );

	Menu_Init( &vrsParsed );
	if ( !handle ) {
		Com_Printf( S_COLOR_YELLOW "%s not found: no VR page in Setup\n", VRS_MENU_FILE );
		return qfalse;
	}
	while ( trap->PC_ReadToken( handle, &token ) ) {
		if ( !Q_stricmp( token.string, "menudef" ) ) {
			ok = Menu_Parse( handle, &vrsParsed );
			break;
		}
	}
	trap->PC_FreeSource( handle );
	return ( ok && vrsParsed.itemCount > 0 ) ? qtrue : qfalse;
}

static qboolean UI_VRS_Insert( menuDef_t *menu, int at, itemDef_t *item ) {
	if ( !item || menu->itemCount >= MAX_MENUITEMS ) {
		return qfalse;
	}
	at = Com_Clampi( 0, menu->itemCount, at );
	memmove( &menu->items[at + 1], &menu->items[at], ( menu->itemCount - at ) * sizeof( menu->items[0] ) );
	menu->items[at] = item;
	menu->itemCount++;
	item->parent = menu;
	return qtrue;
}

static int UI_VRS_Index( const menuDef_t *menu, const itemDef_t *item ) {
	int i;
	for ( i = 0; i < menu->itemCount; i++ ) {
		if ( menu->items[i] == item ) {
			return i;
		}
	}
	return -1;
}

static qboolean UI_VRS_InGroup( const itemDef_t *it, const char *group ) {
	return ( it->window.group && !Q_stricmp( it->window.group, group ) ) ? qtrue : qfalse;
}

static qboolean UI_VRS_Overlap( const rectDef_t *a, const rectDef_t *b ) {
	return ( a->x < b->x + b->w - 0.5f && b->x < a->x + a->w - 0.5f && a->y < b->y + b->h - 0.5f && b->y < a->y + a->h - 0.5f ) ? qtrue : qfalse;
}

// The bar an item lights up while the mouse is on it ("show <name>" as the
// mouse comes onto it), if any.
static itemDef_t *UI_VRS_BarOf( menuDef_t *menu, const itemDef_t *it ) {
	const char *p = it->mouseEnter;

	while ( p ) {
		const char *t = COM_ParseExt( &p, qtrue );
		if ( !p || !t[0] ) {
			break;
		}
		if ( !Q_stricmp( t, "show" ) ) {
			itemDef_t *bar = Menu_FindItemByName( menu, COM_ParseExt( &p, qtrue ) );
			return ( bar && ( bar->window.flags & WINDOW_DECORATION ) ) ? bar : NULL;
		}
	}
	return NULL;
}

// A decoration copied from another, its own name, where it says.
static itemDef_t *UI_VRS_CopyBar( const itemDef_t *from, const char *name, const rectDef_t *r ) {
	itemDef_t *bar = (itemDef_t *)UI_Alloc( sizeof( itemDef_t ) );

	if ( !bar ) {
		return NULL;
	}
	Item_Init( bar );
	bar->window.name = String_Alloc( name );
	bar->window.group = from->window.group;
	bar->window.style = from->window.style;
	bar->window.background = from->window.background;
	VectorCopy4( from->window.foreColor, bar->window.foreColor );
	VectorCopy4( from->window.backColor, bar->window.backColor );
	bar->window.flags = WINDOW_DECORATION;	// (shown while the mouse is on its row)
	bar->window.rectClient = *r;
	return bar;
}

// The page's effects volume: what the settings look like, and where.
static itemDef_t *UI_VRS_Ref( menuDef_t *menu ) {
	itemDef_t *ref = Menu_FindItemByName( menu, "effects_volume" );
	int i;

	for ( i = 0; !ref && i < menu->itemCount; i++ ) {
		itemDef_t *it = menu->items[i];
		if ( it->type == ITEM_TYPE_SLIDER && it->cvar && !Q_stricmp( it->cvar, "s_volume" ) ) {
			ref = it;
		}
	}
	return ( ref && ref->window.group && ref->type == ITEM_TYPE_SLIDER ) ? ref : NULL;
}

static void UI_VRS_AddTo( menuDef_t *menu ) {
	const char *pageName = menu->window.name ? menu->window.name : "a menu";
	itemDef_t *last = NULL, *above = NULL, *ref, *button, *glow = NULL, *stockGlow = NULL, *bg, *refBar, *lastButton = NULL;
	itemDef_t *rows[VRS_MAX_ROWS], *bars[VRS_MAX_ROWS];
	vrsButtons_t buttons;
	rectDef_t r;
	float top, bottom, pitch, second, rowH, barDy;
	int numRows = 0, lastRef = -1, i, k;

	if ( Menu_FindItemByName( menu, VRS_BUTTON ) ) {
		return;	// (already there)
	}
	// the page's own buttons: the lowest one, the one above it, and the last
	// in the menu
	for ( i = 0; i < menu->itemCount; i++ ) {
		itemDef_t *it = menu->items[i];
		if ( !UI_VRS_InGroup( it, VRS_SUBMENU ) || it->type != ITEM_TYPE_BUTTON ) {
			continue;
		}
		lastButton = it;
		if ( !last || it->window.rectClient.y > last->window.rectClient.y ) {
			last = it;
		}
	}
	ref = UI_VRS_Ref( menu );
	if ( !last || !ref ) {
		return;	// not a Setup page this knows
	}
	for ( i = 0; i < menu->itemCount; i++ ) {
		itemDef_t *it = menu->items[i];
		if ( UI_VRS_InGroup( it, VRS_SUBMENU ) && it->type == ITEM_TYPE_BUTTON && it->window.rectClient.y < last->window.rectClient.y - 0.5f &&
			( !above || it->window.rectClient.y > above->window.rectClient.y ) ) {
			above = it;
		}
	}

	// the button: under the lowest, as far below it as that is below the one
	// above (or right under it), in the menu, clear of everything shown there
	r = last->window.rectClient;
	r.y += above ? last->window.rectClient.y - above->window.rectClient.y : last->window.rectClient.h;
	if ( r.y + r.h > menu->window.rect.h + 0.5f ) {
		Com_Printf( S_COLOR_YELLOW "%s: no room for a VR button under the others (the VR settings still work from the console)\n", pageName );
		return;
	}
	for ( i = 0; i < menu->itemCount; i++ ) {
		const itemDef_t *it = menu->items[i];
		if ( !( it->window.flags & WINDOW_DECORATION ) && ( it->window.flags & WINDOW_VISIBLE ) && UI_VRS_Overlap( &it->window.rectClient, &r ) ) {
			Com_Printf( S_COLOR_YELLOW "%s: no room for a VR button under the others, %s is there (the VR settings still work from the console)\n", pageName,
				it->window.name ? it->window.name : "something" );
			return;
		}
	}

	// the settings: where the Sound settings start, as far apart as they
	// are (closer, if they wouldn't all fit above the bottom of the page's
	// background), and as tall (shorter, if they still wouldn't)
	if ( !UI_VRS_Parse() ) {
		return;
	}
	top = ref->window.rectClient.y;
	second = 0.0f;
	for ( i = 0; i < menu->itemCount; i++ ) {
		const itemDef_t *it = menu->items[i];
		if ( !UI_VRS_InGroup( it, ref->window.group ) ) {
			continue;
		}
		lastRef = Q_max( lastRef, i );
		if ( it->window.flags & WINDOW_DECORATION ) {
			continue;
		}
		top = Q_min( top, it->window.rectClient.y );
	}
	for ( i = 0; i < menu->itemCount; i++ ) {
		const itemDef_t *it = menu->items[i];
		if ( UI_VRS_InGroup( it, ref->window.group ) && !( it->window.flags & WINDOW_DECORATION ) && it->window.rectClient.y > top + 2.0f &&
			( second <= 0.0f || it->window.rectClient.y < second ) ) {
			second = it->window.rectClient.y;
		}
	}
	rowH = Q_max( ref->window.rectClient.h, VRS_ROW_MIN );
	pitch = ( second > 0.0f ) ? second - top : rowH + 6.0f;
	bg = Menu_FindItemByName( menu, VRS_BACKGROUND );
	bottom = ( bg && bg->window.rectClient.y + bg->window.rectClient.h > top + 100.0f ) ? bg->window.rectClient.y + bg->window.rectClient.h - 4.0f : top + 220.0f;
	for ( i = 0; i < vrsParsed.itemCount && numRows < VRS_MAX_ROWS; i++ ) {
		if ( !( vrsParsed.items[i]->window.flags & WINDOW_DECORATION ) ) {
			rows[numRows++] = vrsParsed.items[i];
		}
	}
	if ( !numRows ) {
		return;
	}
	if ( top + ( numRows - 1 ) * pitch + rowH > bottom ) {
		pitch = ( numRows > 1 ) ? ( bottom - top - rowH ) / ( numRows - 1 ) : pitch;
		if ( pitch < rowH && numRows > 1 ) {
			// closer together than the Sound rows are tall: shorter than them
			// (their text as it is) - as short as VRS_ROW_MIN - a little apart
			rowH = Q_max( VRS_ROW_MIN, pitch - VRS_ROW_GAP );
			pitch = ( bottom - top - rowH ) / ( numRows - 1 );
		}
	}
	if ( pitch < rowH || menu->itemCount + 2 + 2 * numRows > MAX_MENUITEMS ) {
		Com_Printf( S_COLOR_YELLOW "%s: no room for the VR settings (they still work from the console)\n", pageName );
		return;
	}

	// ...looking like the Sound settings, each lighting up a bar behind it as
	// they do (as far from it as theirs)
	refBar = UI_VRS_BarOf( menu, ref );
	if ( !refBar ) {
		refBar = Menu_FindItemByName( menu, "highlight1" );
	}
	barDy = ( refBar && UI_VRS_BarOf( menu, ref ) ) ? refBar->window.rectClient.y - ref->window.rectClient.y : 0.0f;
	for ( k = 0; k < numRows; k++ ) {
		itemDef_t *a = rows[k];
		a->iMenuFont = ref->iMenuFont;
		a->textscale = ref->textscale;
		a->textStyle = ref->textStyle;
		a->alignment = ref->alignment;
		a->textalignment = ref->textalignment;
		a->textalignx = ref->textalignx;
		a->textaligny = ref->textaligny;
		VectorCopy4( ref->window.foreColor, a->window.foreColor );
		a->window.group = String_Alloc( VRS_GROUP );
		a->window.flags &= ~WINDOW_VISIBLE;		// (shown with the VR button)
		a->window.rectClient.x = ref->window.rectClient.x;
		a->window.rectClient.w = ref->window.rectClient.w;
		a->window.rectClient.y = top + k * pitch;
		a->window.rectClient.h = Q_min( ref->window.rectClient.h, rowH );
		bars[k] = NULL;
		if ( refBar ) {
			rectDef_t br = refBar->window.rectClient;
			br.y = a->window.rectClient.y + barDy;
			bars[k] = UI_VRS_CopyBar( refBar, va( "vrsetup_highlight%i", k + 1 ), &br );
		}
		if ( bars[k] ) {
			a->mouseEnter = String_Alloc( va( "show vrsetup_highlight%i ; ", k + 1 ) );
			a->mouseExit = String_Alloc( va( "hide vrsetup_highlight%i ; ", k + 1 ) );
		} else {
			a->mouseEnter = a->mouseExit = NULL;
		}
	}

	// the button, looking like the lowest one, and the light behind it as
	// that one has
	button = (itemDef_t *)UI_Alloc( sizeof( itemDef_t ) );
	if ( !button ) {
		return;
	}
	Item_Init( button );
	button->window.name = String_Alloc( VRS_BUTTON );
	button->window.group = String_Alloc( VRS_SUBMENU );
	button->window.style = last->window.style;
	button->window.flags = last->window.flags & ( WINDOW_VISIBLE );
	button->window.rectClient = r;
	button->type = ITEM_TYPE_BUTTON;
	button->text = String_Alloc( "VR" );
	button->descText = String_Alloc( "Settings for playing in VR: the hand you use, your saber's weight, the controllers' vibration, your holsters, your sabers' colors" );
	button->iMenuFont = last->iMenuFont;
	button->textscale = last->textscale;
	button->textStyle = last->textStyle;
	button->textalignment = last->textalignment;
	button->textalignx = last->textalignx;
	button->textaligny = last->textaligny;
	VectorCopy4( last->window.foreColor, button->window.foreColor );
	stockGlow = UI_VRS_BarOf( menu, last );
	if ( stockGlow ) {
		rectDef_t gr = stockGlow->window.rectClient;
		gr.y += r.y - last->window.rectClient.y;
		glow = UI_VRS_CopyBar( stockGlow, VRS_GLOW, &gr );
	}
	if ( glow ) {
		button->mouseEnter = String_Alloc( "show " VRS_GLOW " ; " );
		button->mouseExit = String_Alloc( "hide " VRS_GLOW " ; " );
	}

	// what it does: what the others do, but showing these; and what they do
	// (and opening the page) hides these
	memset( &buttons, 0, sizeof( buttons ) );
	for ( i = 0; i < menu->itemCount; i++ ) {
		itemDef_t *it = menu->items[i];
		if ( UI_VRS_InGroup( it, VRS_SUBMENU ) && it->type == ITEM_TYPE_BUTTON && it->action ) {
			UI_VRS_ReadAction( &buttons, it );
		}
	}
	button->action = UI_VRS_Action( &buttons );
	for ( i = 0; i < menu->itemCount; i++ ) {
		itemDef_t *it = menu->items[i];
		if ( UI_VRS_InGroup( it, VRS_SUBMENU ) && it->type == ITEM_TYPE_BUTTON && it->action ) {
			it->action = UI_VRS_AlsoHide( it->action );
		}
	}
	menu->onOpen = UI_VRS_AlsoHide( menu->onOpen );

	// in: the settings drawn with the page's Sound settings (on its
	// background), their bars behind them; the button (and its light) after
	// the page's own, so the keys go from those to it
	k = lastRef + 1;
	for ( i = 0; i < numRows; i++ ) {
		if ( bars[i] && UI_VRS_Insert( menu, k, bars[i] ) ) {
			k++;
		}
	}
	for ( i = 0; i < numRows; i++ ) {
		if ( UI_VRS_Insert( menu, k, rows[i] ) ) {
			k++;
		}
	}
	k = UI_VRS_Index( menu, lastButton ) + 1;
	if ( glow && UI_VRS_Insert( menu, k, glow ) ) {
		k++;
	}
	UI_VRS_Insert( menu, k, button );
	Menu_UpdatePosition( menu );
}

// After the menus load: the VR page on Setup, in the main menu and in game.
// (While the menus' definitions, menudef.h, are still loaded.)
void UI_VRSetup_AddPage( void ) {
	int i;

	for ( i = 0; i < (int)ARRAY_LEN( vrsCvars ); i++ ) {
		trap->Cvar_Register( NULL, vrsCvars[i].name, vrsCvars[i].value, vrsCvars[i].flags );
	}
	for ( i = 0; i < (int)ARRAY_LEN( vrsPages ); i++ ) {
		menuDef_t *menu = Menus_FindByName( vrsPages[i] );
		if ( menu ) {
			UI_VRS_AddTo( menu );
		}
	}
}
