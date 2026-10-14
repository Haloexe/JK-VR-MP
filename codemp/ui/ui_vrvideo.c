/*
===========================================================================
OpenJK VR (multiplayer) - the display settings on the Video page of Setup
(the main menu's "setup_menu" and the in-game "ingame_setup"), for playing
on a monitor, widescreen ones included:

  Display Mode       Windowed / Fullscreen / Borderless Window
                     ui_r_fullscreen 0 / 1 / 2 -> r_fullscreen, r_noborder
  Video Mode         the display's own resolution ("Desktop"), every one the
                     monitor has (r_availableModes), widescreen included, and
                     Custom - the display the window's on (listed again as
                     the page opens, if it's been moved to another)
                     ui_r_resolution "desktop" / "WxH" / "custom" -> r_mode -2,
                     or r_mode -1 with r_customwidth / r_customheight
  Custom Resolution  the size Custom is, "1920 x 1080": click it to type a new
                     one ("2560x1080", "2560 1080"...), which picks Custom
                     (typing nothing leaves it as it was). Dim while Video Mode
                     is something else.
                     ui_r_customres

The stock pages offer only the 4:3 resolutions of 2003 and a fullscreen
switch. When the menus load, those two items are turned into these - found
by their settings (ui_r_mode, ui_r_fullscreen), so a changed menu that
keeps them gets these too - and the Custom Resolution row goes in under
Video Mode: the rows below it move down a row, or on a page without the
room for that (the in-game one) close up a little. A borderless window at
the desktop's resolution covers the screen (shared/sdl/sdl_window.cpp). The
quality presets no longer change the resolution or the display mode
(ui_main.c), so choosing those doesn't make the preset "Custom" either.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "ui_local.h"

extern void		Menu_UpdatePosition( menuDef_t *menu );
extern void		Menu_ShowItemByName( menuDef_t *menu, const char *p, qboolean bShow );
extern void		Item_ValidateTypeData( itemDef_t *item );
extern qboolean	Item_TextField_Editing( const itemDef_t *item );

#define VRD_MIN_W			640		// smaller resolutions aren't in the list
#define VRD_MIN_H			480
#define VRD_CUSTOM_MIN_W	320		// ...nor typed in (r_mode 0's)
#define VRD_CUSTOM_MIN_H	240
#define VRD_CUSTOM_MAX		16384	// (as big as graphics cards draw)
#define VRD_CHARS			13		// "16384 x 16384"
#define VRD_ROW_MIN			13.0f	// rows can't be closer together than this (the text)
#define VRD_DIM				0.45f	// how faint the custom row is while Video Mode isn't Custom
#define VRD_MAX_ROWS		32
#define VRD_DESKTOP			"desktop"
#define VRD_CUSTOM			"custom"
#define VRD_FIELD			"vrd_customres"		// the custom row
#define VRD_TYPENEW			"vrCustomResType"		// (uiScript: clicked to type a size)
#define VRD_MODE_WINDOWED	0		// ui_r_fullscreen
#define VRD_MODE_FULLSCREEN	1
#define VRD_MODE_BORDERLESS	2

static const char *vrdPages[] = { "setup_menu", "ingame_setup" };

// r_mode's own table (shared/sdl/sdl_window.cpp, r_vidModes)
static const int vrdStockModes[][2] = {
	{ 320, 240 }, { 400, 300 }, { 512, 384 }, { 640, 480 }, { 800, 600 }, { 960, 720 }, { 1024, 768 },
	{ 1152, 864 }, { 1280, 1024 }, { 1600, 1200 }, { 2048, 1536 }, { 856, 480 }, { 2400, 600 }
};

// when the engine doesn't list the monitor's (another engine): the usual ones
static const int vrdCommonModes[][2] = {
	{ 3840, 2160 }, { 3440, 1440 }, { 2560, 1600 }, { 2560, 1440 }, { 2560, 1080 }, { 1920, 1200 }, { 1920, 1080 },
	{ 1680, 1050 }, { 1600, 1200 }, { 1600, 900 }, { 1440, 900 }, { 1366, 768 }, { 1280, 1024 }, { 1280, 800 },
	{ 1280, 720 }, { 1024, 768 }, { 800, 600 }
};

static struct {
	qboolean	converted;		// a page has these items (so what they say is applied)
	int			count;			// [0] is the desktop's (and there's room left for Custom)
	int			w[MAX_MULTI_CVARS], h[MAX_MULTI_CVARS];
	char		desktop[MAX_CVAR_VALUE_STRING];	// what they're from: r_desktopResolution...
	char		modes[MAX_STRING_CHARS];		// ...and r_availableModes (the display the window's on)
} vrdRes;

static struct {
	qboolean	on[ARRAY_LEN( vrdPages )];		// the page has the custom row
	vec4_t		color[ARRAY_LEN( vrdPages )];	// ...in this colour (while it's what's chosen)
	char		typed[MAX_CVAR_VALUE_STRING];	// what's in it, as last seen
	char		was[MAX_CVAR_VALUE_STRING];		// ...before it was clicked to type in
	qboolean	editing;						// it was being typed in, last frame
} vrdCustom;

// "1920x1080" -> 1920, 1080
static qboolean UI_VRD_ParseRes( const char *s, int *w, int *h ) {
	return (qboolean)( s && sscanf( s, "%dx%d", w, h ) == 2 && *w > 0 && *h > 0 );
}

// What's typed in the custom row: two numbers, whatever's between them.
static qboolean UI_VRD_ParseTyped( const char *s, int *w, int *h ) {
	return (qboolean)( s && sscanf( s, "%d%*[^0-9]%d", w, h ) == 2 && *w > 0 && *h > 0 );
}

// The resolution set now: the desktop's (r_mode -2), or its size.
static qboolean UI_VRD_Current( int *w, int *h ) {
	const int mode = (int)trap->Cvar_VariableValue( "r_mode" );

	*w = *h = 0;
	if ( mode == -2 ) {
		return qtrue;
	}
	if ( mode == -1 ) {
		*w = (int)trap->Cvar_VariableValue( "r_customwidth" );
		*h = (int)trap->Cvar_VariableValue( "r_customheight" );
	} else if ( mode >= 0 && mode < (int)ARRAY_LEN( vrdStockModes ) ) {
		*w = vrdStockModes[mode][0];
		*h = vrdStockModes[mode][1];
	}
	return qfalse;
}

static void UI_VRD_Add( int w, int h ) {
	int i;

	if ( w < VRD_MIN_W || h < VRD_MIN_H || vrdRes.count >= MAX_MULTI_CVARS - 1 ) {
		return;
	}
	for ( i = 1; i < vrdRes.count; i++ ) {
		if ( vrdRes.w[i] == w && vrdRes.h[i] == h ) {
			return;
		}
	}
	vrdRes.w[vrdRes.count] = w;
	vrdRes.h[vrdRes.count] = h;
	vrdRes.count++;
}

// Whether a size is in the list (other than as the desktop's).
static qboolean UI_VRD_Listed( int w, int h ) {
	int i;

	for ( i = 1; i < vrdRes.count; i++ ) {
		if ( vrdRes.w[i] == w && vrdRes.h[i] == h ) {
			return qtrue;
		}
	}
	return qfalse;
}

// The resolutions to offer: the desktop's first, then the monitor's, largest
// first. (Any other is Custom.)
static void UI_VRD_BuildList( void ) {
	char buf[MAX_STRING_CHARS];
	const char *p;
	int w, h, i, j, dw = 0, dh = 0;
	const qboolean converted = vrdRes.converted;

	memset( &vrdRes, 0, sizeof( vrdRes ) );
	vrdRes.converted = converted;
	trap->Cvar_VariableStringBuffer( "r_desktopResolution", vrdRes.desktop, sizeof( vrdRes.desktop ) );
	UI_VRD_ParseRes( vrdRes.desktop, &dw, &dh );
	vrdRes.w[0] = dw;
	vrdRes.h[0] = dh;
	vrdRes.count = 1;

	trap->Cvar_VariableStringBuffer( "r_availableModes", vrdRes.modes, sizeof( vrdRes.modes ) );
	Q_strncpyz( buf, vrdRes.modes, sizeof( buf ) );
	for ( p = buf; *p; ) {
		while ( *p == ' ' ) {
			p++;
		}
		if ( UI_VRD_ParseRes( p, &w, &h ) ) {
			UI_VRD_Add( w, h );
		}
		while ( *p && *p != ' ' ) {
			p++;
		}
	}
	if ( vrdRes.count == 1 ) {
		for ( i = 0; i < (int)ARRAY_LEN( vrdCommonModes ); i++ ) {
			if ( !dw || ( vrdCommonModes[i][0] <= dw && vrdCommonModes[i][1] <= dh ) ) {
				UI_VRD_Add( vrdCommonModes[i][0], vrdCommonModes[i][1] );
			}
		}
	}

	// largest first
	for ( i = 2; i < vrdRes.count; i++ ) {
		for ( j = i; j > 1 && ( vrdRes.w[j] > vrdRes.w[j - 1] || ( vrdRes.w[j] == vrdRes.w[j - 1] && vrdRes.h[j] > vrdRes.h[j - 1] ) ); j-- ) {
			int t = vrdRes.w[j]; vrdRes.w[j] = vrdRes.w[j - 1]; vrdRes.w[j - 1] = t;
			t = vrdRes.h[j]; vrdRes.h[j] = vrdRes.h[j - 1]; vrdRes.h[j - 1] = t;
		}
	}
}

/*
--------------------------------------------------------------------------------
The items
--------------------------------------------------------------------------------
*/

// A menu script without its "<drop0> <drop1>" commands (wherever they are:
// the menus' scripts don't always have ";" between their commands), and with
// the word "from" in it changed to "to".
static void UI_VRD_ScriptWord( char *out, int size, const char *tok, const char *from, const char *to ) {
	if ( !strcmp( tok, ";" ) ) {
		Q_strcat( out, size, "; " );
	} else {
		Q_strcat( out, size, va( "\"%s\" ", ( from && to && !Q_stricmp( tok, from ) ) ? to : tok ) );
	}
}
static const char *UI_VRD_Script( const char *script, const char *drop0, const char *drop1, const char *from, const char *to ) {
	char out[2048], held[MAX_TOKEN_CHARS];
	const char *p = script;
	qboolean holding = qfalse, dropped = qfalse;

	if ( !script ) {
		return NULL;
	}
	out[0] = '\0';
	while ( p ) {
		const char *tok = COM_ParseExt( &p, qtrue );
		if ( !p ) {
			break;
		}
		if ( holding ) {
			holding = qfalse;
			if ( !Q_stricmp( tok, drop1 ) ) {
				dropped = qtrue;	// (and a ";" after it)
				continue;
			}
			UI_VRD_ScriptWord( out, sizeof( out ), held, from, to );
		}
		if ( dropped ) {
			dropped = qfalse;
			if ( !strcmp( tok, ";" ) ) {
				continue;
			}
		}
		if ( drop0 && drop1 && !Q_stricmp( tok, drop0 ) ) {
			Q_strncpyz( held, tok, sizeof( held ) );
			holding = qtrue;
			continue;
		}
		UI_VRD_ScriptWord( out, sizeof( out ), tok, from, to );
	}
	if ( holding ) {
		UI_VRD_ScriptWord( out, sizeof( out ), held, from, to );
	}
	return String_Alloc( out );
}

// What the page does when one of these is changed, but for marking the
// quality preset "Custom" (the presets don't choose these now).
static void UI_VRD_NoPreset( itemDef_t *item ) {
	if ( item->action ) {
		item->action = UI_VRD_Script( item->action, "uiScript", "glCustom", NULL, NULL );
	}
}

// "Video Mode": the resolutions, as "desktop", "WxH" or "custom" in ui_r_resolution
static void UI_VRD_ResolutionItem( itemDef_t *item, qboolean custom ) {
	multiDef_t *multi = item->typeData.multi;
	int i, n = 0;

	for ( i = 0; i < vrdRes.count && n < MAX_MULTI_CVARS - 1; i++, n++ ) {
		if ( i == 0 ) {
			multi->cvarList[n] = String_Alloc( vrdRes.w[0] ? va( "Desktop (%i x %i)", vrdRes.w[0], vrdRes.h[0] ) : "Desktop" );
			multi->cvarStr[n] = String_Alloc( VRD_DESKTOP );
		} else {
			multi->cvarList[n] = String_Alloc( va( "%i x %i", vrdRes.w[i], vrdRes.h[i] ) );
			multi->cvarStr[n] = String_Alloc( va( "%ix%i", vrdRes.w[i], vrdRes.h[i] ) );
		}
		multi->cvarValue[n] = (float)n;
	}
	if ( custom ) {
		multi->cvarList[n] = String_Alloc( "Custom" );
		multi->cvarStr[n] = String_Alloc( VRD_CUSTOM );
		multi->cvarValue[n] = (float)n;
		n++;
	}
	multi->count = n;
	multi->strDef = qtrue;
	item->cvar = String_Alloc( "ui_r_resolution" );
	item->descText = String_Alloc( custom ? "Screen resolution: Desktop is your monitor's own, Custom is the one below"
		: "Screen resolution: Desktop is your monitor's own" );
	UI_VRD_NoPreset( item );
}

// "Display Mode": windowed, fullscreen, or a borderless window
static void UI_VRD_DisplayItem( itemDef_t *item ) {
	multiDef_t *multi = item->typeData.multi;

	multi->cvarList[0] = String_Alloc( "Windowed" );
	multi->cvarList[1] = String_Alloc( "Fullscreen" );
	multi->cvarList[2] = String_Alloc( "Borderless Window" );
	multi->cvarValue[0] = VRD_MODE_WINDOWED;
	multi->cvarValue[1] = VRD_MODE_FULLSCREEN;
	multi->cvarValue[2] = VRD_MODE_BORDERLESS;
	multi->count = 3;
	multi->strDef = qfalse;
	item->text = String_Alloc( "Display Mode" );
	item->descText = String_Alloc( "Fullscreen, a window, or a borderless window covering the screen" );
	UI_VRD_NoPreset( item );
}

/*
--------------------------------------------------------------------------------
The Custom Resolution row
--------------------------------------------------------------------------------
*/

static qboolean UI_VRD_Insert( menuDef_t *menu, int at, itemDef_t *item ) {
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

// The bar a row lights up while the mouse is on it ("show <bar>" in its
// mouseEnter: one of the page's "highlights"), if any.
static itemDef_t *UI_VRD_BarOf( menuDef_t *menu, const itemDef_t *item ) {
	const char *p = item->mouseEnter;

	while ( p ) {
		const char *tok = COM_ParseExt( &p, qtrue );
		if ( !p ) {
			break;
		}
		if ( !Q_stricmp( tok, "show" ) ) {
			itemDef_t *bar = Menu_FindItemByName( menu, COM_ParseExt( &p, qtrue ) );
			if ( bar && bar->window.group && !Q_stricmp( bar->window.group, "highlights" ) ) {
				return bar;
			}
		}
	}
	return NULL;
}

// The bar for a row where it is now: the page's own if one's there, or else a
// new one like "like" (named vrd_highlight<n>), behind the page's settings.
static itemDef_t *UI_VRD_BarAt( menuDef_t *menu, const itemDef_t *like, float y, float h, int at, int *made ) {
	const rectDef_t *l = &like->window.rectClient;
	itemDef_t *bar;
	int i;

	for ( i = 0; i < menu->itemCount; i++ ) {
		const rectDef_t *b = &menu->items[i]->window.rectClient;
		if ( menu->items[i]->window.name && menu->items[i]->window.group && !Q_stricmp( menu->items[i]->window.group, "highlights" )
			&& fabsf( b->x - l->x ) < 0.5f && fabsf( b->w - l->w ) < 0.5f && fabsf( b->y - y ) < 0.5f && fabsf( b->h - h ) < 0.5f ) {
			return menu->items[i];
		}
	}
	bar = (itemDef_t *)UI_Alloc( sizeof( itemDef_t ) );
	if ( !bar ) {
		return NULL;
	}
	Item_Init( bar );
	bar->window.name = String_Alloc( va( "vrd_highlight%i", ++( *made ) ) );
	bar->window.group = String_Alloc( "highlights" );
	bar->window.style = like->window.style;
	bar->window.background = like->window.background;
	VectorCopy4( like->window.foreColor, bar->window.foreColor );
	VectorCopy4( like->window.backColor, bar->window.backColor );
	bar->window.flags = WINDOW_DECORATION;	// (shown while the mouse is on its row)
	bar->window.rectClient.x = l->x;
	bar->window.rectClient.w = l->w;
	bar->window.rectClient.y = y;
	bar->window.rectClient.h = h;
	return UI_VRD_Insert( menu, at, bar ) ? bar : NULL;
}

// An item lighting up "from" lights up "to" instead.
static void UI_VRD_Relight( itemDef_t *item, const itemDef_t *from, const itemDef_t *to ) {
	if ( from && to && from != to ) {
		item->mouseEnter = UI_VRD_Script( item->mouseEnter, NULL, NULL, from->window.name, to->window.name );
		item->mouseExit = UI_VRD_Script( item->mouseExit, NULL, NULL, from->window.name, to->window.name );
	}
}

// The row: what it's called, and the size typed in it in the page's value
// column.
static itemDef_t *UI_VRD_Field( itemDef_t *res, float y, float h ) {
	itemDef_t *f = (itemDef_t *)UI_Alloc( sizeof( itemDef_t ) );

	if ( !f ) {
		return NULL;
	}
	Item_Init( f );
	f->type = ITEM_TYPE_EDITFIELD;
	Item_ValidateTypeData( f );
	if ( !f->typeData.edit ) {
		return NULL;
	}
	f->typeData.edit->maxChars = VRD_CHARS;
	f->typeData.edit->maxPaintChars = VRD_CHARS;
	f->window.name = String_Alloc( VRD_FIELD );
	f->window.group = res->window.group;
	f->window.style = WINDOW_STYLE_EMPTY;
	f->window.flags = res->window.flags & WINDOW_VISIBLE;	// (shown with the page's other settings)
	f->window.rectClient = res->window.rectClient;
	f->window.rectClient.y = y;
	f->window.rectClient.h = h;
	VectorCopy4( res->window.foreColor, f->window.foreColor );
	f->cvar = String_Alloc( "ui_r_customres" );
	f->text = String_Alloc( "Custom Resolution" );
	f->descText = String_Alloc( "Your own resolution, for Custom: click it and type the width and the height" );
	f->textalignment = res->textalignment;
	f->textalignx = res->textalignx;
	f->textaligny = res->textaligny;
	f->textscale = res->textscale;
	f->textStyle = res->textStyle;
	f->iMenuFont = res->iMenuFont;
	f->alignment = res->alignment;
	// clicking it: as the page's settings do (Apply Changes), Custom, and a
	// new size to type
	f->action = String_Alloc( va( "%s\"setcvar\" \"ui_r_resolution\" \"" VRD_CUSTOM "\" ; \"uiScript\" \"" VRD_TYPENEW "\" ; ",
		res->action ? res->action : "" ) );
	return f;
}

// The Custom Resolution row, under Video Mode ("res"): the page's rows below
// it move down a row to make room - or if the page hasn't the room for that,
// they close up as much as they need to (the in-game page's do). No room at
// all (a menu mod's full page): no row.
static qboolean UI_VRD_AddCustom( menuDef_t *menu, itemDef_t *res ) {
	const rectDef_t r0 = res->window.rectClient;
	itemDef_t *moved[MAX_MENUITEMS], *oldBar[MAX_MENUITEMS], *bars[VRD_MAX_ROWS + 2];
	itemDef_t *resBar, *bg, *field;
	float rowY[VRD_MAX_ROWS], newY[VRD_MAX_ROWS + 2];
	float limit = 480.0f, step, rowH, dy = 0.0f, barH = 0.0f;
	int numMoved = 0, numRows = 0, first = -1, made = 0, i, j, at;
	qboolean squeeze = qfalse;
	const char *resEnter, *resExit;

	if ( Menu_FindItemByName( menu, VRD_FIELD ) ) {
		return qtrue;	// (already there: the menus were set up again)
	}
	if ( !res->window.group || menu->itemCount + 2 + VRD_MAX_ROWS + 2 > MAX_MENUITEMS ) {
		return qfalse;
	}

	// the page's rows below it, top to bottom (and the bars they light up)
	for ( i = 0; i < menu->itemCount; i++ ) {
		itemDef_t *it = menu->items[i];
		const float y = it->window.rectClient.y;
		if ( !it->window.group || Q_stricmp( it->window.group, res->window.group ) ) {
			continue;
		}
		if ( first < 0 ) {
			first = i;
		}
		if ( it == res || y < r0.y + 0.5f ) {
			continue;
		}
		for ( j = 0; j < numRows && fabsf( rowY[j] - y ) >= 1.0f; j++ ) {
		}
		if ( j == numRows ) {
			if ( numRows == VRD_MAX_ROWS ) {
				return qfalse;
			}
			rowY[numRows++] = y;
		}
		oldBar[numMoved] = UI_VRD_BarOf( menu, it );
		moved[numMoved++] = it;
	}
	for ( i = 1; i < numRows; i++ ) {
		for ( j = i; j > 0 && rowY[j] < rowY[j - 1]; j-- ) {
			const float t = rowY[j]; rowY[j] = rowY[j - 1]; rowY[j - 1] = t;
		}
	}
	resBar = UI_VRD_BarOf( menu, res );

	// as far down as the page goes: its background's bottom, or the top of
	// anything under the rows that's there with every page (Apply Changes)
	bg = Menu_FindItemByName( menu, "setup_background" );
	if ( bg && bg->window.rectClient.y <= r0.y && bg->window.rectClient.y + bg->window.rectClient.h > r0.y + r0.h * 2.0f ) {
		limit = bg->window.rectClient.y + bg->window.rectClient.h - 4.0f;
	}
	for ( i = 0; i < menu->itemCount; i++ ) {
		const itemDef_t *it = menu->items[i];
		const rectDef_t *b = &it->window.rectClient;
		if ( ( it->window.group && it->window.group[0] && Q_stricmp( it->window.group, "none" ) ) || ( it->window.flags & WINDOW_DECORATION ) ) {
			continue;	// (another page's, or a picture)
		}
		if ( b->x >= r0.x + r0.w || b->x + b->w <= r0.x || b->y < r0.y + r0.h - 0.5f ) {
			continue;
		}
		limit = Q_min( limit, b->y );
	}

	// a row more, as far apart as they are, or closer together
	step = ( numRows && rowY[0] - r0.y >= 1.0f ) ? Q_min( rowY[0] - r0.y, r0.h * 1.5f ) : r0.h;
	rowH = r0.h;
	if ( ( numRows ? rowY[numRows - 1] : r0.y ) + step + rowH > limit + 0.5f ) {
		squeeze = qtrue;
		step = floorf( ( limit - r0.y ) / ( numRows + 2 ) );
		rowH = Q_min( r0.h, step );
		if ( step < VRD_ROW_MIN ) {
			Com_Printf( S_COLOR_YELLOW "%s: no room for the Custom Resolution row (r_mode -1, r_customwidth and r_customheight still work)\n",
				menu->window.name ? menu->window.name : "A menu" );
			return qfalse;
		}
	}
	newY[0] = r0.y;
	newY[1] = r0.y + step;
	for ( i = 0; i < numRows; i++ ) {
		newY[i + 2] = squeeze ? r0.y + ( i + 2 ) * step : rowY[i] + step;
	}

	resEnter = res->mouseEnter;
	resExit = res->mouseExit;
	field = UI_VRD_Field( res, newY[1], rowH );
	if ( !field ) {
		return qfalse;
	}

	// the rows where they go now
	for ( i = 0; i < numMoved; i++ ) {
		rectDef_t *b = &moved[i]->window.rectClient;
		for ( j = 0; j < numRows && fabsf( rowY[j] - b->y ) >= 1.0f; j++ ) {
		}
		b->y = newY[j + 2];
		if ( squeeze ) {
			b->h = Q_min( b->h, step );
		}
	}
	res->window.rectClient.h = rowH;

	// and the bars behind them where they are (the page's own if they line up)
	if ( resBar ) {
		dy = resBar->window.rectClient.y - r0.y;
		barH = squeeze ? Q_min( resBar->window.rectClient.h, step ) : resBar->window.rectClient.h;
		at = first >= 0 ? first : menu->itemCount;
		for ( i = 0; i < numRows + 2; i++ ) {
			bars[i] = UI_VRD_BarAt( menu, resBar, newY[i] + dy, barH, at, &made );
		}
		UI_VRD_Relight( res, resBar, bars[0] );
		for ( i = 0; i < numMoved; i++ ) {
			for ( j = 0; j < numRows && fabsf( newY[j + 2] - moved[i]->window.rectClient.y ) >= 1.0f; j++ ) {
			}
			if ( j < numRows ) {
				UI_VRD_Relight( moved[i], oldBar[i], bars[j + 2] );
			}
		}
		// (the row's light: Video Mode's, a row down)
		if ( bars[1] ) {
			field->mouseEnter = UI_VRD_Script( resEnter, NULL, NULL, resBar->window.name, bars[1]->window.name );
			field->mouseExit = UI_VRD_Script( resExit, NULL, NULL, resBar->window.name, bars[1]->window.name );
		}
	}

	// after Video Mode (drawn after it, and next after it going down the page
	// with the keys)
	for ( at = 0; at < menu->itemCount && menu->items[at] != res; at++ ) {
	}
	UI_VRD_Insert( menu, at + 1, field );
	Menu_UpdatePosition( menu );
	return qtrue;
}

/*
--------------------------------------------------------------------------------
Hooks (ui_main.c)
--------------------------------------------------------------------------------
*/

// When the menus have loaded: the Video pages' two items become these, and
// the custom row goes in.
void UI_VRVideo_AddSettings( void ) {
	int m, i;

	vrdRes.converted = qfalse;
	UI_VRD_BuildList();
	memset( &vrdCustom, 0, sizeof( vrdCustom ) );
	trap->Cvar_VariableStringBuffer( "ui_r_customres", vrdCustom.typed, sizeof( vrdCustom.typed ) );
	for ( m = 0; m < (int)ARRAY_LEN( vrdPages ); m++ ) {
		menuDef_t *menu = Menus_FindByName( vrdPages[m] );
		itemDef_t *res = NULL;
		for ( i = 0; menu && i < menu->itemCount; i++ ) {
			itemDef_t *item = menu->items[i];
			if ( !item || item->type != ITEM_TYPE_MULTI || !item->typeData.multi || !item->cvar ) {
				continue;
			}
			if ( !Q_stricmp( item->cvar, "ui_r_mode" ) || !Q_stricmp( item->cvar, "ui_r_resolution" ) ) {
				res = res ? res : item;
			} else if ( !Q_stricmp( item->cvar, "ui_r_fullscreen" ) ) {
				UI_VRD_DisplayItem( item );
				vrdRes.converted = qtrue;
			}
		}
		if ( res ) {
			UI_VRD_NoPreset( res );
			vrdCustom.on[m] = UI_VRD_AddCustom( menu, res );
			VectorCopy4( res->window.foreColor, vrdCustom.color[m] );
			UI_VRD_ResolutionItem( res, vrdCustom.on[m] );
			vrdRes.converted = qtrue;
		}
	}
}

// The resolutions again, if the engine's are others now: the window's been
// moved to another display since they were listed.
static void UI_VRD_Relist( void ) {
	char desktop[MAX_CVAR_VALUE_STRING], modes[MAX_STRING_CHARS];
	int m, i;

	trap->Cvar_VariableStringBuffer( "r_desktopResolution", desktop, sizeof( desktop ) );
	trap->Cvar_VariableStringBuffer( "r_availableModes", modes, sizeof( modes ) );
	if ( !strcmp( desktop, vrdRes.desktop ) && !strcmp( modes, vrdRes.modes ) ) {
		return;
	}
	UI_VRD_BuildList();
	for ( m = 0; m < (int)ARRAY_LEN( vrdPages ); m++ ) {
		menuDef_t *menu = Menus_FindByName( vrdPages[m] );
		for ( i = 0; menu && i < menu->itemCount; i++ ) {
			itemDef_t *item = menu->items[i];
			if ( item && item->type == ITEM_TYPE_MULTI && item->typeData.multi && item->cvar
				&& !Q_stricmp( item->cvar, "ui_r_resolution" ) ) {
				UI_VRD_ResolutionItem( item, vrdCustom.on[m] );
				break;
			}
		}
	}
}

// Opening the Video page (UI_GetVideoSetup): what's set now.
void UI_VRVideo_GetSetup( void ) {
	int w, h, cw, ch;

	trap->Cvar_Register( NULL, "ui_r_resolution", "", CVAR_ROM | CVAR_INTERNAL );
	trap->Cvar_Register( NULL, "ui_r_customres", "", CVAR_ROM | CVAR_INTERNAL );
	if ( !vrdRes.converted ) {
		return;	// (pages without these items: the stock ui_r_mode and ui_r_fullscreen)
	}
	UI_VRD_Relist();

	// the custom size: the last one set, or the size now if it's none in the list
	cw = (int)trap->Cvar_VariableValue( "r_customwidth" );
	ch = (int)trap->Cvar_VariableValue( "r_customheight" );
	if ( UI_VRD_Current( &w, &h ) ) {
		trap->Cvar_Set( "ui_r_resolution", VRD_DESKTOP );
	} else if ( UI_VRD_Listed( w, h ) ) {
		trap->Cvar_Set( "ui_r_resolution", va( "%ix%i", w, h ) );
	} else {
		trap->Cvar_Set( "ui_r_resolution", VRD_CUSTOM );
		if ( w > 0 && h > 0 ) {
			cw = w;
			ch = h;
		}
	}
	if ( cw <= 0 || ch <= 0 ) {
		cw = vrdRes.w[0] > 0 ? vrdRes.w[0] : 1920;
		ch = vrdRes.h[0] > 0 ? vrdRes.h[0] : 1080;
	}
	Com_sprintf( vrdCustom.typed, sizeof( vrdCustom.typed ), "%i x %i", cw, ch );
	Q_strncpyz( vrdCustom.was, vrdCustom.typed, sizeof( vrdCustom.was ) );
	trap->Cvar_Set( "ui_r_customres", vrdCustom.typed );

	if ( trap->Cvar_VariableValue( "r_fullscreen" ) ) {
		trap->Cvar_Set( "ui_r_fullscreen", va( "%i", VRD_MODE_FULLSCREEN ) );
	} else {
		trap->Cvar_Set( "ui_r_fullscreen", va( "%i", trap->Cvar_VariableValue( "r_noborder" ) ? VRD_MODE_BORDERLESS : VRD_MODE_WINDOWED ) );
	}
}

// Applying it (UI_UpdateVideoSetup, before the vid_restart).
void UI_VRVideo_UpdateSetup( void ) {
	char res[MAX_CVAR_VALUE_STRING];
	int mode, w, h;

	if ( !vrdRes.converted ) {
		return;	// (pages without these items: the stock r_mode and r_fullscreen)
	}
	trap->Cvar_VariableStringBuffer( "ui_r_resolution", res, sizeof( res ) );
	if ( !Q_stricmp( res, VRD_DESKTOP ) ) {
		trap->Cvar_Set( "r_mode", "-2" );
	} else if ( !Q_stricmp( res, VRD_CUSTOM ) ) {
		char typed[MAX_CVAR_VALUE_STRING];
		trap->Cvar_VariableStringBuffer( "ui_r_customres", typed, sizeof( typed ) );
		if ( !UI_VRD_ParseTyped( typed, &w, &h ) && !UI_VRD_ParseTyped( vrdCustom.was, &w, &h ) ) {
			// (not a size: as it was)
			w = (int)trap->Cvar_VariableValue( "r_customwidth" );
			h = (int)trap->Cvar_VariableValue( "r_customheight" );
		}
		w = Com_Clampi( VRD_CUSTOM_MIN_W, VRD_CUSTOM_MAX, w );
		h = Com_Clampi( VRD_CUSTOM_MIN_H, VRD_CUSTOM_MAX, h );
		Com_sprintf( vrdCustom.typed, sizeof( vrdCustom.typed ), "%i x %i", w, h );
		Q_strncpyz( vrdCustom.was, vrdCustom.typed, sizeof( vrdCustom.was ) );
		trap->Cvar_Set( "ui_r_customres", vrdCustom.typed );
		trap->Cvar_Set( "r_mode", "-1" );
		trap->Cvar_Set( "r_customwidth", va( "%i", w ) );
		trap->Cvar_Set( "r_customheight", va( "%i", h ) );
	} else if ( UI_VRD_ParseRes( res, &w, &h ) ) {
		trap->Cvar_Set( "r_mode", "-1" );
		trap->Cvar_Set( "r_customwidth", va( "%i", w ) );
		trap->Cvar_Set( "r_customheight", va( "%i", h ) );
	}
	mode = (int)trap->Cvar_VariableValue( "ui_r_fullscreen" );
	trap->Cvar_Set( "r_fullscreen", ( mode == VRD_MODE_FULLSCREEN ) ? "1" : "0" );
	trap->Cvar_Set( "r_noborder", ( mode == VRD_MODE_BORDERLESS ) ? "1" : "0" );
}

// Every frame: a size typed into the custom row makes it what's chosen (and
// clicked but left empty, it's as it was), and the row is dim while it isn't
// what's chosen.
void UI_VRVideo_Frame( void ) {
	char typed[MAX_CVAR_VALUE_STRING], res[MAX_CVAR_VALUE_STRING];
	qboolean changed, editing;
	int m;

	if ( !vrdRes.converted ) {
		return;
	}
	trap->Cvar_VariableStringBuffer( "ui_r_customres", typed, sizeof( typed ) );
	changed = strcmp( typed, vrdCustom.typed ) ? qtrue : qfalse;
	Q_strncpyz( vrdCustom.typed, typed, sizeof( vrdCustom.typed ) );
	for ( m = 0; m < (int)ARRAY_LEN( vrdPages ); m++ ) {
		menuDef_t *menu;
		itemDef_t *field;
		if ( !vrdCustom.on[m] || !( menu = Menus_FindByName( vrdPages[m] ) ) || !( menu->window.flags & WINDOW_VISIBLE )
			|| !( field = Menu_FindItemByName( menu, VRD_FIELD ) ) ) {
			continue;
		}
		editing = Item_TextField_Editing( field );
		if ( editing && !vrdCustom.editing && typed[0] ) {
			// started typing in it some other way than a click (Enter): it
			// starts empty then too
			Q_strncpyz( vrdCustom.was, typed, sizeof( vrdCustom.was ) );
			trap->Cvar_Set( "ui_r_customres", "" );
			field->cursorPos = 0;
			field->typeData.edit->paintOffset = 0;
			typed[0] = '\0';
		}
		vrdCustom.editing = editing;
		if ( changed && editing ) {
			trap->Cvar_Set( "ui_r_resolution", VRD_CUSTOM );
			trap->Cvar_Set( "ui_r_modified", "1" );
			Menu_ShowItemByName( menu, "applyChanges", qtrue );
		}
		if ( !typed[0] && vrdCustom.was[0] && !Item_TextField_Editing( field ) ) {
			trap->Cvar_Set( "ui_r_customres", vrdCustom.was );
			Q_strncpyz( vrdCustom.typed, vrdCustom.was, sizeof( vrdCustom.typed ) );
		}
		trap->Cvar_VariableStringBuffer( "ui_r_resolution", res, sizeof( res ) );
		VectorCopy4( vrdCustom.color[m], field->window.foreColor );
		if ( Q_stricmp( res, VRD_CUSTOM ) ) {
			field->window.foreColor[3] *= VRD_DIM;
		}
	}
}

// The menus' uiScripts these have (UI_RunMenuScript).
qboolean UI_VRVideo_Script( const char *name ) {
	if ( !Q_stricmp( name, VRD_TYPENEW ) ) {
		// clicked the custom row to type a size: it starts empty (what was
		// there comes back if nothing's typed)
		char now[MAX_CVAR_VALUE_STRING];
		trap->Cvar_VariableStringBuffer( "ui_r_customres", now, sizeof( now ) );
		if ( now[0] ) {
			Q_strncpyz( vrdCustom.was, now, sizeof( vrdCustom.was ) );
		}
		trap->Cvar_Set( "ui_r_customres", "" );
		return qtrue;
	}
	return qfalse;
}
