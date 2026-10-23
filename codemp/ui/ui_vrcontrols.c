/*
===========================================================================
OpenJK VR (multiplayer) - the VR sections of Setup > Controls (the main
menu's "controlsMenu" and the in-game "ingame_controls"): VR Movement and VR
View, the settings for moving about and seeing in a headset.

When the menus load, two buttons go under the page's own (Movement,
Interaction, Weapons, Force Powers, Mouse/Joystick, Other), looking like
them, and the settings in ui/vr_controls.menu go where the page shows the
others', laid out as its Other settings are and looking like them. Choosing
one shows its settings and hides whatever the page's own buttons show;
choosing any of those (or opening the page) hides them again. The page is
otherwise left as it is.

  VR Movement
    Turning           snap or smooth                       vr_turnMode 0/1
    Snap Angle        15, 30, 45, 60, 90 degrees           vr_turnAngle
    Turn Speed        smooth turning, 60-360 degrees/s     vr_turnSpeed
    Snap Fade         snap turns dip the view to dark      vr_snapFade 0/1
    Comfort Vignette  the edges of your view darken as     vr_vignette 0-3
                      you move: off, low, medium, high
    Move Direction    where you look, or where the hand    vr_moveDirection 0/1
                      holding the moving stick points
    Always Run        the game's own                       cl_run
    Swap Sticks       move with the right, turn with the   vr_swapSticks
                      left (cl_vr.cpp)
    Stick Deadzone    0.05-0.5                             vr_deadzone
    Recenter          (a button)                           vr_recenter
  VR View
    Head Movement     your view follows your head          vr_positional
    Head Reach        how far, 0.1-0.5 m                   vr_positionalMax
    World Scale       28-40 units a metre                  vr_worldScale
    HUD Distance      1-6 m                                vr_hudDepth
    HUD Size          30-90 degrees                        vr_hudFov
    Menu Distance     1.5-6 m                              vr_menuDistance
    Menu Size         1-4 m                                vr_menuHeight
    Gun Aim           your hand, or your head              vr_handAim
    Gun Angle         -30-30 degrees                       vr_gunPitch
    Resolution        70%-140%, from when the game next    vr_resolutionScale
                      starts (or after vid_restart)
    Desktop View      the headset's view in the window     vr_mirror

They're the engine's settings (but Always Run, the game's): all there
before the menus load, and taking effect at once - but Resolution, which
the headset's picture takes when it's made. (The menus set it as they set
any: at once - the engine's latching only holds back the console's - and
the engine reads it only then.)

A page with no room for the buttons or the settings (one a menu mod has
changed) is left as it is, and says so in the console; the settings still
work there.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "ui_local.h"

extern qboolean	Menu_Parse( int handle, menuDef_t *menu );
extern void		Menu_UpdatePosition( menuDef_t *menu );

#define VRC_MENU_FILE		"ui/vr_controls.menu"
#define VRC_BACKGROUND		"setup_background"		// what the page shows its settings on
#define VRC_REF_CVAR		"cl_run"				// the Other settings' Always Run: what the settings look like, and where
#define VRC_MAX_ROWS		16
#define VRC_MAX_NAMES		32						// what the page's buttons show and hide
#define VRC_MAX_BUTTONS		16
#define VRC_ROW_MIN			12.0f					// rows can't be closer together than this
#define VRC_ROW_GAP			2.0f					// rows made shorter to fit are this far apart
#define VRC_SECTIONS		2

static const char *vrcPages[] = { "controlsMenu", "ingame_controls" };

static const struct {
	const char	*menu;		// its settings: this menuDef in ui/vr_controls.menu
	const char	*group;		// their group, on the page
	const char	*button;	// the page's button for them, and the light behind it
	const char	*glow;
	const char	*bar;		// the bars behind them ("<bar><n>")
	const char	*text;
	const char	*desc;
} vrcSections[VRC_SECTIONS] = {
	{ "vr_controls_move", "vrmove", "vrmovebutton", "vrmovebutton_glow", "vrmove_highlight", "VR Movement",
		"Turning, moving and the sticks in VR, comfort settings - and recentering" },
	{ "vr_controls_view", "vrview", "vrviewbutton", "vrviewbutton_glow", "vrview_highlight", "VR View",
		"Your view in VR: head movement, the HUD, the menu screen, gun aim, resolution" },
};

/*
--------------------------------------------------------------------------------
What the page's own buttons do
--------------------------------------------------------------------------------
*/

typedef struct {
	char		names[VRC_MAX_NAMES][MAX_QPATH];	// what they show or hide (but the background)
	int			numNames;
	itemDef_t	*buttons[VRC_MAX_BUTTONS];			// the buttons
	int			numButtons;
	vec4_t		normal, chosen;						// the buttons' colour, and the chosen one's
	qboolean	haveNormal, haveChosen;
} vrcButtons_t;

static void UI_VRC_AddName( vrcButtons_t *b, const char *name ) {
	int i;

	if ( !name[0] || !Q_stricmp( name, VRC_BACKGROUND ) ) {
		return;
	}
	for ( i = 0; i < b->numNames; i++ ) {
		if ( !Q_stricmp( b->names[i], name ) ) {
			return;
		}
	}
	if ( b->numNames < VRC_MAX_NAMES ) {
		Q_strncpyz( b->names[b->numNames++], name, sizeof( b->names[0] ) );
	}
}

// One of the page's own buttons: one that shows the page's settings on its
// background ("show setup_background" in what it does).
static qboolean UI_VRC_IsButton( const itemDef_t *it ) {
	const char *p = it->action;

	if ( it->type != ITEM_TYPE_BUTTON || !p ) {
		return qfalse;
	}
	while ( p ) {
		const char *t = COM_ParseExt( &p, qtrue );
		if ( !p || !t[0] ) {
			break;
		}
		if ( !Q_stricmp( t, "show" ) && !Q_stricmp( COM_ParseExt( &p, qtrue ), VRC_BACKGROUND ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

// One button's action: what it shows and hides, and the colours it gives the
// buttons (its own the chosen one's, any other's the rest's). (Its commands,
// and what they're given, one after another - with a ";" between or not.)
static void UI_VRC_ReadAction( vrcButtons_t *b, const itemDef_t *button ) {
	const char *p = button->action;

	while ( p ) {
		char cmd[MAX_QPATH];
		Q_strncpyz( cmd, COM_ParseExt( &p, qtrue ), sizeof( cmd ) );
		if ( !p || !cmd[0] ) {
			break;
		}
		if ( !Q_stricmp( cmd, "show" ) || !Q_stricmp( cmd, "hide" ) ) {
			UI_VRC_AddName( b, COM_ParseExt( &p, qtrue ) );
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
			if ( button->window.name && !Q_stricmp( which, button->window.name ) ) {
				VectorCopy4( c, b->chosen );
				b->haveChosen = qtrue;
			} else if ( !b->haveNormal ) {
				VectorCopy4( c, b->normal );
				b->haveNormal = qtrue;
			}
		}
	}
}

// Every button the normal colour (theirs and ours), but the chosen one.
static void UI_VRC_Colours( char *s, int size, const vrcButtons_t *b, const char *chosen ) {
	int i;

	for ( i = 0; i < b->numButtons; i++ ) {
		if ( b->buttons[i]->window.name && b->haveNormal ) {
			Q_strcat( s, size, va( "setitemcolor \"%s\" forecolor %g %g %g %g ; ", b->buttons[i]->window.name,
				b->normal[0], b->normal[1], b->normal[2], b->normal[3] ) );
		}
	}
	for ( i = 0; i < VRC_SECTIONS; i++ ) {
		const qboolean me = (qboolean)( chosen && !Q_stricmp( chosen, vrcSections[i].button ) );
		const float *c = me ? ( b->haveChosen ? b->chosen : colorWhite ) : ( b->haveNormal ? b->normal : NULL );
		if ( c ) {
			Q_strcat( s, size, va( "setitemcolor %s forecolor %g %g %g %g ; ", vrcSections[i].button, c[0], c[1], c[2], c[3] ) );
		}
	}
}

// A section's button's action: as the others', but showing its settings.
static const char *UI_VRC_Action( const vrcButtons_t *b, int section ) {
	char s[4096];
	int i;

	Q_strncpyz( s, "play \"sound/interface/button1.wav\" ; show " VRC_BACKGROUND " ; ", sizeof( s ) );
	for ( i = 0; i < b->numNames; i++ ) {
		Q_strcat( s, sizeof( s ), va( "hide \"%s\" ; ", b->names[i] ) );
	}
	for ( i = 0; i < VRC_SECTIONS; i++ ) {
		if ( i != section ) {
			Q_strcat( s, sizeof( s ), va( "hide %s ; ", vrcSections[i].group ) );
		}
	}
	Q_strcat( s, sizeof( s ), va( "show %s ; ", vrcSections[section].group ) );
	UI_VRC_Colours( s, sizeof( s ), b, vrcSections[section].button );
	return String_Alloc( s );
}

// A script that also hides ours (and gives their buttons the normal colour).
static const char *UI_VRC_AlsoHide( const char *script, const vrcButtons_t *b ) {
	char s[4096];
	int i;

	Q_strncpyz( s, script ? script : "", sizeof( s ) );
	Q_strcat( s, sizeof( s ), " " );
	for ( i = 0; i < VRC_SECTIONS; i++ ) {
		Q_strcat( s, sizeof( s ), va( "hide %s ; ", vrcSections[i].group ) );
		if ( b->haveNormal ) {
			Q_strcat( s, sizeof( s ), va( "setitemcolor %s forecolor %g %g %g %g ; ", vrcSections[i].button,
				b->normal[0], b->normal[1], b->normal[2], b->normal[3] ) );
		}
	}
	return String_Alloc( s );
}

/*
--------------------------------------------------------------------------------
Putting them on the page
--------------------------------------------------------------------------------
*/

static menuDef_t	vrcParsed[VRC_SECTIONS];	// the sections' settings, parsed afresh for each page

// The sections' menuDefs from ui/vr_controls.menu (qfalse: it's not there).
static qboolean UI_VRC_Parse( void ) {
	pc_token_t token;
	int s;
	const int handle = trap->PC_LoadSource( VRC_MENU_FILE );

	for ( s = 0; s < VRC_SECTIONS; s++ ) {
		Menu_Init( &vrcParsed[s] );
	}
	if ( !handle ) {
		Com_Printf( S_COLOR_YELLOW "%s not found: no VR sections in Controls\n", VRC_MENU_FILE );
		return qfalse;
	}
	while ( trap->PC_ReadToken( handle, &token ) ) {
		if ( !Q_stricmp( token.string, "menudef" ) ) {
			menuDef_t m;
			Menu_Init( &m );
			if ( !Menu_Parse( handle, &m ) ) {
				break;
			}
			for ( s = 0; s < VRC_SECTIONS; s++ ) {
				if ( m.window.name && !Q_stricmp( m.window.name, vrcSections[s].menu ) && !vrcParsed[s].itemCount ) {
					vrcParsed[s] = m;
				}
			}
		}
	}
	trap->PC_FreeSource( handle );
	return qtrue;
}

static qboolean UI_VRC_Insert( menuDef_t *menu, int at, itemDef_t *item ) {
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

static int UI_VRC_Index( const menuDef_t *menu, const itemDef_t *item ) {
	int i;
	for ( i = 0; i < menu->itemCount; i++ ) {
		if ( menu->items[i] == item ) {
			return i;
		}
	}
	return -1;
}

static qboolean UI_VRC_InGroup( const itemDef_t *it, const char *group ) {
	return ( it->window.group && group && !Q_stricmp( it->window.group, group ) ) ? qtrue : qfalse;
}

static qboolean UI_VRC_Overlap( const rectDef_t *a, const rectDef_t *b ) {
	return ( a->x < b->x + b->w - 0.5f && b->x < a->x + a->w - 0.5f && a->y < b->y + b->h - 0.5f && b->y < a->y + a->h - 0.5f ) ? qtrue : qfalse;
}

// The bar an item lights up while the mouse is on it ("show <name>" as the
// mouse comes onto it), if any.
static itemDef_t *UI_VRC_BarOf( menuDef_t *menu, const itemDef_t *it ) {
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
static itemDef_t *UI_VRC_CopyBar( const itemDef_t *from, const char *name, const rectDef_t *r ) {
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

// The page's Always Run: what the settings look like, and where.
static itemDef_t *UI_VRC_Ref( menuDef_t *menu ) {
	int i;

	for ( i = 0; i < menu->itemCount; i++ ) {
		itemDef_t *it = menu->items[i];
		if ( it->cvar && !Q_stricmp( it->cvar, VRC_REF_CVAR ) && it->window.group && ( it->type == ITEM_TYPE_YESNO || it->type == ITEM_TYPE_MULTI ) ) {
			return it;
		}
	}
	return NULL;
}

// A section's settings: hidden, where the reference's group's settings start,
// row by row, looking like it, each lighting a bar as it does. qfalse: no room.
typedef struct {
	itemDef_t	*rows[VRC_MAX_ROWS], *bars[VRC_MAX_ROWS];
	int			numRows;
} vrcRows_t;

static qboolean UI_VRC_Rows( menuDef_t *menu, itemDef_t *ref, int section, vrcRows_t *out ) {
	const char *pageName = menu->window.name ? menu->window.name : "a menu";
	const menuDef_t *parsed = &vrcParsed[section];
	itemDef_t *refBar, *bg;
	float top, second, bottom, pitch, rowH, barDy;
	int i, k;

	memset( out, 0, sizeof( *out ) );
	for ( i = 0; i < parsed->itemCount; i++ ) {
		if ( parsed->items[i]->window.flags & WINDOW_DECORATION ) {
			continue;
		}
		if ( out->numRows == VRC_MAX_ROWS ) {
			Com_Printf( S_COLOR_YELLOW "%s: only the first %i %s settings in %s are shown\n", pageName, VRC_MAX_ROWS, vrcSections[section].text, VRC_MENU_FILE );
			break;
		}
		out->rows[out->numRows++] = parsed->items[i];
	}
	if ( !out->numRows ) {
		Com_Printf( S_COLOR_YELLOW "%s: no %s settings in %s (\"%s\")\n", pageName, vrcSections[section].text, VRC_MENU_FILE, vrcSections[section].menu );
		return qfalse;
	}
	top = ref->window.rectClient.y;
	second = 0.0f;
	for ( i = 0; i < menu->itemCount; i++ ) {
		const itemDef_t *it = menu->items[i];
		if ( UI_VRC_InGroup( it, ref->window.group ) && !( it->window.flags & WINDOW_DECORATION ) ) {
			top = Q_min( top, it->window.rectClient.y );
		}
	}
	for ( i = 0; i < menu->itemCount; i++ ) {
		const itemDef_t *it = menu->items[i];
		if ( UI_VRC_InGroup( it, ref->window.group ) && !( it->window.flags & WINDOW_DECORATION ) && it->window.rectClient.y > top + 2.0f &&
			( second <= 0.0f || it->window.rectClient.y < second ) ) {
			second = it->window.rectClient.y;
		}
	}
	rowH = Q_max( ref->window.rectClient.h, VRC_ROW_MIN );
	pitch = ( second > 0.0f ) ? second - top : rowH + 2.0f;
	bg = Menu_FindItemByName( menu, VRC_BACKGROUND );
	bottom = ( bg && bg->window.rectClient.y + bg->window.rectClient.h > top + 100.0f ) ? bg->window.rectClient.y + bg->window.rectClient.h - 4.0f : top + 220.0f;
	if ( top + ( out->numRows - 1 ) * pitch + rowH > bottom ) {
		pitch = ( out->numRows > 1 ) ? ( bottom - top - rowH ) / ( out->numRows - 1 ) : pitch;
		if ( pitch < rowH && out->numRows > 1 ) {
			// closer together than the page's rows are tall: shorter than them
			// (their text as it is) - as short as VRC_ROW_MIN - a little apart
			rowH = Q_max( VRC_ROW_MIN, pitch - VRC_ROW_GAP );
			pitch = ( bottom - top - rowH ) / ( out->numRows - 1 );
		}
	}
	if ( pitch < rowH ) {
		Com_Printf( S_COLOR_YELLOW "%s: no room for the %s settings (they still work from the console)\n", pageName, vrcSections[section].text );
		return qfalse;
	}

	refBar = UI_VRC_BarOf( menu, ref );
	barDy = refBar ? refBar->window.rectClient.y - ref->window.rectClient.y : 0.0f;
	for ( k = 0; k < out->numRows; k++ ) {
		itemDef_t *a = out->rows[k];
		a->iMenuFont = ref->iMenuFont;
		a->textscale = ref->textscale;
		a->textStyle = ref->textStyle;
		a->alignment = ref->alignment;
		a->textalignment = ref->textalignment;
		a->textalignx = ref->textalignx;
		a->textaligny = ref->textaligny;
		VectorCopy4( ref->window.foreColor, a->window.foreColor );
		a->window.group = String_Alloc( vrcSections[section].group );
		a->window.flags &= ~WINDOW_VISIBLE;		// (shown with its button)
		a->window.rectClient.x = ref->window.rectClient.x;
		a->window.rectClient.w = ref->window.rectClient.w;
		a->window.rectClient.y = top + k * pitch;
		a->window.rectClient.h = Q_min( ref->window.rectClient.h, rowH );
		out->bars[k] = NULL;
		if ( refBar ) {
			rectDef_t br = refBar->window.rectClient;
			br.y = a->window.rectClient.y + barDy;
			out->bars[k] = UI_VRC_CopyBar( refBar, va( "%s%i", vrcSections[section].bar, k + 1 ), &br );
		}
		if ( out->bars[k] ) {
			a->mouseEnter = String_Alloc( va( "show %s%i ; ", vrcSections[section].bar, k + 1 ) );
			a->mouseExit = String_Alloc( va( "hide %s%i ; ", vrcSections[section].bar, k + 1 ) );
		} else {
			a->mouseEnter = a->mouseExit = NULL;
		}
	}
	return qtrue;
}

static void UI_VRC_AddTo( menuDef_t *menu ) {
	const char *pageName = menu->window.name ? menu->window.name : "a menu";
	itemDef_t *last = NULL, *above = NULL, *ref, *lastButton = NULL, *stockGlow;
	itemDef_t *button[VRC_SECTIONS], *glow[VRC_SECTIONS];
	vrcRows_t rows[VRC_SECTIONS];
	vrcButtons_t buttons;
	rectDef_t r[VRC_SECTIONS];
	int lastRef = -1, i, k, s;

	if ( Menu_FindItemByName( menu, vrcSections[0].button ) ) {
		return;	// (already there)
	}
	// the page's own buttons: the lowest one, the one above it, and the last
	// in the menu
	memset( &buttons, 0, sizeof( buttons ) );
	for ( i = 0; i < menu->itemCount; i++ ) {
		itemDef_t *it = menu->items[i];
		if ( !UI_VRC_IsButton( it ) ) {
			continue;
		}
		if ( buttons.numButtons < VRC_MAX_BUTTONS ) {
			buttons.buttons[buttons.numButtons++] = it;
		}
		lastButton = it;
		if ( !last || it->window.rectClient.y > last->window.rectClient.y ) {
			last = it;
		}
	}
	if ( !last ) {
		return;	// not a Controls page this knows
	}
	ref = UI_VRC_Ref( menu );
	if ( !ref ) {
		Com_Printf( S_COLOR_YELLOW "%s: no Always Run setting to put the VR settings with (they still work from the console)\n", pageName );
		return;
	}
	for ( i = 0; i < buttons.numButtons; i++ ) {
		itemDef_t *it = buttons.buttons[i];
		if ( it->window.rectClient.y < last->window.rectClient.y - 0.5f && ( !above || it->window.rectClient.y > above->window.rectClient.y ) ) {
			above = it;
		}
	}

	// the buttons: under the lowest, as far apart as it is from the one above
	// (or right under it), in the menu, clear of everything shown there
	for ( s = 0; s < VRC_SECTIONS; s++ ) {
		r[s] = last->window.rectClient;
		r[s].y += ( s + 1 ) * ( above ? last->window.rectClient.y - above->window.rectClient.y : last->window.rectClient.h );
		if ( r[s].y + r[s].h > menu->window.rect.h + 0.5f ) {
			Com_Printf( S_COLOR_YELLOW "%s: no room for the VR buttons under the others (the VR settings still work from the console)\n", pageName );
			return;
		}
		for ( i = 0; i < menu->itemCount; i++ ) {
			const itemDef_t *it = menu->items[i];
			if ( !( it->window.flags & WINDOW_DECORATION ) && ( it->window.flags & WINDOW_VISIBLE ) && UI_VRC_Overlap( &it->window.rectClient, &r[s] ) ) {
				Com_Printf( S_COLOR_YELLOW "%s: no room for the VR buttons under the others, %s is there (the VR settings still work from the console)\n", pageName,
					it->window.name ? it->window.name : "something" );
				return;
			}
		}
	}

	// the settings, each section's where the page's Other settings start
	if ( !UI_VRC_Parse() ) {
		return;
	}
	for ( s = 0; s < VRC_SECTIONS; s++ ) {
		if ( !UI_VRC_Rows( menu, ref, s, &rows[s] ) ) {
			return;
		}
	}
	k = menu->itemCount + 2 * VRC_SECTIONS;
	for ( s = 0; s < VRC_SECTIONS; s++ ) {
		k += 2 * rows[s].numRows;
	}
	if ( k > MAX_MENUITEMS ) {
		Com_Printf( S_COLOR_YELLOW "%s: no room for the VR settings (they still work from the console)\n", pageName );
		return;
	}

	// what the page's buttons do (their colours too)
	for ( i = 0; i < buttons.numButtons; i++ ) {
		UI_VRC_ReadAction( &buttons, buttons.buttons[i] );
	}

	// the buttons, looking like the lowest one, and the light behind each as
	// that one has
	stockGlow = UI_VRC_BarOf( menu, last );
	for ( s = 0; s < VRC_SECTIONS; s++ ) {
		itemDef_t *b = (itemDef_t *)UI_Alloc( sizeof( itemDef_t ) );
		button[s] = b;
		glow[s] = NULL;
		if ( !b ) {
			return;
		}
		Item_Init( b );
		b->window.name = String_Alloc( vrcSections[s].button );
		b->window.group = last->window.group;
		b->window.style = last->window.style;
		b->window.flags = last->window.flags & ( WINDOW_VISIBLE );
		b->window.rectClient = r[s];
		b->type = ITEM_TYPE_BUTTON;
		b->text = String_Alloc( vrcSections[s].text );
		b->descText = String_Alloc( vrcSections[s].desc );
		b->iMenuFont = last->iMenuFont;
		b->textscale = last->textscale;
		b->textStyle = last->textStyle;
		b->textalignment = last->textalignment;
		b->textalignx = last->textalignx;
		b->textaligny = last->textaligny;
		VectorCopy4( buttons.haveNormal ? buttons.normal : last->window.foreColor, b->window.foreColor );
		if ( stockGlow ) {
			rectDef_t gr = stockGlow->window.rectClient;
			gr.y += r[s].y - last->window.rectClient.y;
			glow[s] = UI_VRC_CopyBar( stockGlow, vrcSections[s].glow, &gr );
		}
		if ( glow[s] ) {
			b->mouseEnter = String_Alloc( va( "show %s ; ", vrcSections[s].glow ) );
			b->mouseExit = String_Alloc( va( "hide %s ; ", vrcSections[s].glow ) );
		}
		b->action = UI_VRC_Action( &buttons, s );
	}

	// what the page's own buttons do (and opening the page) hides ours
	for ( i = 0; i < buttons.numButtons; i++ ) {
		buttons.buttons[i]->action = UI_VRC_AlsoHide( buttons.buttons[i]->action, &buttons );
	}
	menu->onOpen = UI_VRC_AlsoHide( menu->onOpen, &buttons );

	// in: the settings drawn with the page's Other settings (on its
	// background), their bars behind them; the buttons (and their lights)
	// after the page's own, so the keys go from those to them
	for ( i = 0; i < menu->itemCount; i++ ) {
		if ( UI_VRC_InGroup( menu->items[i], ref->window.group ) ) {
			lastRef = i;
		}
	}
	k = lastRef + 1;
	for ( s = 0; s < VRC_SECTIONS; s++ ) {
		for ( i = 0; i < rows[s].numRows; i++ ) {
			if ( rows[s].bars[i] && UI_VRC_Insert( menu, k, rows[s].bars[i] ) ) {
				k++;
			}
		}
		for ( i = 0; i < rows[s].numRows; i++ ) {
			if ( UI_VRC_Insert( menu, k, rows[s].rows[i] ) ) {
				k++;
			}
		}
	}
	k = UI_VRC_Index( menu, lastButton ) + 1;
	for ( s = 0; s < VRC_SECTIONS; s++ ) {
		if ( glow[s] && UI_VRC_Insert( menu, k, glow[s] ) ) {
			k++;
		}
		if ( UI_VRC_Insert( menu, k, button[s] ) ) {
			k++;
		}
	}
	Menu_UpdatePosition( menu );
}

// After the menus load: the VR sections on Controls, in the main menu and in
// game. (While the menus' definitions, menudef.h, are still loaded.)
void UI_VRControls_AddPages( void ) {
	int i;

	for ( i = 0; i < (int)ARRAY_LEN( vrcPages ); i++ ) {
		menuDef_t *menu = Menus_FindByName( vrcPages[i] );
		if ( menu ) {
			UI_VRC_AddTo( menu );
		}
	}
}
