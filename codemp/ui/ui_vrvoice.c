/*
===========================================================================
OpenJK VR (multiplayer) - the voice chat settings, on the Sound page of
Setup (the main menu's "setup_menu" and the in-game "ingame_setup").

The items are in ui/vr_voice.menu. When the menus load, they're put on the
page below the sound settings already there, which move closer together to
make room (EAX, which this build can't do, makes way). Where everything
goes and how its text looks follow the page's own sound settings, so they
fit the stock menus and most changed ones; on a short page (the in-game
one) the rows close up a little. A page with no room for them at all (one a
menu mod has filled) gets a "Voice Chat Settings" button instead, which
opens them on a panel of their own (laid out in ui/vr_voice.menu):

  Voice Chat          Off / On                          cl_voice
  Voice Chat Volume   how loud the others are           cl_voiceVolume
  Microphone          the microphones there are         cl_voiceMic (feeder 64)
  Input Mode          Voice Activity / Push to Talk     cl_voiceMode
  Voice Activation    how loud you are now, and how loud you have to be to
                      be heard (click or drag to set)   cl_voiceThreshold (ownerdraw 400)
  Mic Boost           louder or quieter                 cl_voiceGain

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "ui_local.h"

extern qboolean	Menu_Parse( int handle, menuDef_t *menu );
extern void		Menu_UpdatePosition( menuDef_t *menu );

#define VRV_MENU_FILE		"ui/vr_voice.menu"
#define VRV_MAX_MICS		10		// cl_voiceMic0..9
#define VRV_LIST_ROWS		4		// microphones the list shows at once (fewer if the page is short)
#define VRV_MAX_ROWS		24
#define VRV_ROW_ITEMS		8
#define VRV_SLIDER_H		18.0f	// a slider's thumb needs this much of a row
#define VRV_PANEL			"vr_voice"	// ui/vr_voice.menu's menuDef: the panel, for a page with no room

static const char *vrvPages[] = { "setup_menu", "ingame_setup" };

/*
--------------------------------------------------------------------------------
The microphones (the list)
--------------------------------------------------------------------------------
*/

static struct {
	int			readTime;		// uiDC.realTime they were read (once a frame)
	qboolean	read;
	int			count;
	char		name[VRV_MAX_MICS + 2][MAX_CVAR_VALUE_STRING];	// [0] "": the system's default
	int			current;		// the one cl_voiceMic names
	qboolean	missing;		// ...isn't connected (it's the last, added to show it)
	// for fitting their names into the list (the narrowest one laid out)
	float		textW, scale;
	int			font;
} vrvMics;

static void UI_VRV_ReadMics( void ) {
	char chosen[MAX_CVAR_VALUE_STRING];
	int i, n;

	if ( vrvMics.read && vrvMics.readTime == uiInfo.uiDC.realTime ) {
		return;
	}
	vrvMics.read = qtrue;
	vrvMics.readTime = uiInfo.uiDC.realTime;
	trap->Cvar_VariableStringBuffer( "cl_voiceMic", chosen, sizeof( chosen ) );
	n = Com_Clampi( 0, VRV_MAX_MICS, (int)trap->Cvar_VariableValue( "cl_voiceMicCount" ) );

	vrvMics.count = 1;
	vrvMics.name[0][0] = '\0';
	vrvMics.current = 0;
	vrvMics.missing = qfalse;
	for ( i = 0; i < n; i++ ) {
		char *name = vrvMics.name[vrvMics.count];
		trap->Cvar_VariableStringBuffer( va( "cl_voiceMic%i", i ), name, MAX_CVAR_VALUE_STRING );
		if ( !name[0] ) {
			continue;
		}
		if ( chosen[0] && !strcmp( name, chosen ) ) {
			vrvMics.current = vrvMics.count;
		}
		vrvMics.count++;
	}
	if ( chosen[0] && !vrvMics.current ) {
		// chosen, but not plugged in (the default one is used meanwhile)
		Q_strncpyz( vrvMics.name[vrvMics.count], chosen, MAX_CVAR_VALUE_STRING );
		vrvMics.current = vrvMics.count++;
		vrvMics.missing = qtrue;
	}
}

int UI_VRVoice_FeederCount( void ) {
	UI_VRV_ReadMics();
	return vrvMics.count;
}

// A name cut short with "..." to fit the list.
static const char *UI_VRV_Fit( const char *text ) {
	static char fit[MAX_CVAR_VALUE_STRING + 32];
	char cut[MAX_CVAR_VALUE_STRING + 32];
	int len;

	Q_strncpyz( fit, text, sizeof( fit ) );
	if ( vrvMics.textW <= 0.0f || uiInfo.uiDC.textWidth( fit, vrvMics.scale, vrvMics.font ) <= vrvMics.textW ) {
		return fit;
	}
	Q_strncpyz( cut, text, sizeof( cut ) );
	for ( len = (int)strlen( cut ) - 1; len > 3; len-- ) {
		cut[len] = '\0';
		Com_sprintf( fit, sizeof( fit ), "%s...", cut );
		if ( uiInfo.uiDC.textWidth( fit, vrvMics.scale, vrvMics.font ) <= vrvMics.textW ) {
			break;
		}
	}
	return fit;
}

const char *UI_VRVoice_FeederText( int index ) {
	char text[MAX_CVAR_VALUE_STRING + 32];

	UI_VRV_ReadMics();
	if ( index < 0 || index >= vrvMics.count ) {
		return "";
	}
	Com_sprintf( text, sizeof( text ), "%s%s%s", ( index == vrvMics.current ) ? S_COLOR_GREEN : "",
		index ? vrvMics.name[index] : "System default",
		( vrvMics.missing && index == vrvMics.count - 1 ) ? " (not connected)" : "" );
	return UI_VRV_Fit( text );
}

qboolean UI_VRVoice_FeederSelection( int index ) {
	UI_VRV_ReadMics();
	if ( index < 0 || index >= vrvMics.count ) {
		return qfalse;
	}
	if ( index != vrvMics.current ) {
		trap->Cvar_Set( "cl_voiceMic", index ? vrvMics.name[index] : "" );
		vrvMics.read = qfalse;
	}
	return qtrue;
}

/*
--------------------------------------------------------------------------------
The level meter (ownerdraw UI_VR_VOICE_METER), which also sets the voice
activation level
--------------------------------------------------------------------------------
*/

static struct {
	float		x, w;			// where its bar was last drawn
	float		shown;			// the level shown (falls back slowly)
	qboolean	dragging;		// the mouse button went down on it and is held
	qboolean	drawn;			// drawn since the last frame began
	int			told;			// what cl_voiceMenu was last set to (-1: not yet)
} vrvMeter = { 0.0f, 0.0f, 0.0f, qfalse, qfalse, -1 };

static void UI_VRV_SetThresholdAt( float cursorX ) {
	if ( vrvMeter.w > 0.0f ) {
		const float t = Com_Clamp( 0.0f, 1.0f, ( cursorX - vrvMeter.x ) / vrvMeter.w );
		trap->Cvar_Set( "cl_voiceThreshold", va( "%.2f", t ) );
	}
}

void UI_VRVoice_DrawMeter( float x, float y, float w, float h, float textY, float special, float scale, vec4_t color, int textStyle, int iMenuFont ) {
	static const vec4_t back = { 0.0f, 0.0f, 0.12f, 0.75f };
	static const vec4_t quiet = { 0.4f, 0.45f, 0.95f, 1.0f };
	static const vec4_t heard = { 0.25f, 0.95f, 0.35f, 1.0f };
	static const vec4_t mark = { 1.0f, 0.682f, 0.0f, 1.0f };
	const qboolean on = ( trap->Cvar_VariableValue( "cl_voice" ) != 0.0f ) ? qtrue : qfalse;
	const int status = (int)trap->Cvar_VariableValue( "cl_voiceMicStatus" );
	const qboolean ptt = ( (int)trap->Cvar_VariableValue( "cl_voiceMode" ) == 1 ) ? qtrue : qfalse;
	const float level = Com_Clamp( 0.0f, 1.0f, trap->Cvar_VariableValue( "cl_voiceLevel" ) );
	const float threshold = Com_Clamp( 0.0f, 1.0f, trap->Cvar_VariableValue( "cl_voiceThreshold" ) );
	const float bw = ( special >= 32.0f ) ? special : 128.0f;
	const float bh = Com_Clamp( 6.0f, 12.0f, h - 4.0f );
	const float by = y + 2.0f;
	const float dt = Com_Clamp( 0.0f, 0.25f, uiInfo.uiDC.frameTime * 0.001f );
	const char *note = NULL;
	qboolean loud;

	vrvMeter.x = x;
	vrvMeter.w = bw;
	vrvMeter.drawn = qtrue;

	// the peak of the last tenth of a second, falling back gently
	vrvMeter.shown = ( level >= vrvMeter.shown ) ? level : Q_max( level, vrvMeter.shown - 1.2f * dt );

	if ( !on ) {
		note = "Voice chat is off";
	} else if ( status < 0 ) {
		note = "No microphone found";
	}
	if ( note ) {
		vec4_t dim;
		VectorSet4( dim, color[0], color[1], color[2], color[3] * 0.7f );
		uiInfo.uiDC.drawText( x, y + textY, scale, dim, note, 0, 0, textStyle, iMenuFont );
		return;
	}

	loud = ptt ? ( trap->Cvar_VariableValue( "cl_voiceSending" ) != 0.0f ) : ( vrvMeter.shown >= threshold && vrvMeter.shown > 0.0f );
	uiInfo.uiDC.fillRect( x, by, bw, bh, back );
	if ( vrvMeter.shown > 0.0f ) {
		uiInfo.uiDC.fillRect( x, by, bw * vrvMeter.shown, bh, loud ? heard : quiet );
	}
	uiInfo.uiDC.drawRect( x, by, bw, bh, 1.0f, color );
	if ( !ptt ) {
		// how loud you have to be to be heard
		uiInfo.uiDC.fillRect( x + bw * threshold - 1.0f, by - 2.0f, 2.0f, bh + 4.0f, mark );
	}
	uiInfo.uiDC.setColor( NULL );
}

qboolean UI_VRVoice_MeterKey( int key ) {
	float t = trap->Cvar_VariableValue( "cl_voiceThreshold" );

	if ( (int)trap->Cvar_VariableValue( "cl_voiceMode" ) == 1 ) {
		return qfalse;	// (push to talk: there's nothing to set)
	}
	switch ( key ) {
	case A_MOUSE1:
		if ( vrvMeter.w <= 0.0f || uiInfo.uiDC.cursorx < vrvMeter.x - 6.0f || uiInfo.uiDC.cursorx > vrvMeter.x + vrvMeter.w + 6.0f ) {
			return qfalse;	// (on its name)
		}
		UI_VRV_SetThresholdAt( uiInfo.uiDC.cursorx );
		vrvMeter.dragging = qtrue;
		return qtrue;
	case A_CURSOR_RIGHT:
	case A_KP_6:
	case A_MWHEELUP:
		t += 0.02f;
		break;
	case A_CURSOR_LEFT:
	case A_KP_4:
	case A_MWHEELDOWN:
		t -= 0.02f;
		break;
	default:
		return qfalse;
	}
	trap->Cvar_Set( "cl_voiceThreshold", va( "%.2f", Com_Clamp( 0.0f, 1.0f, t ) ) );
	return qtrue;
}

/*
--------------------------------------------------------------------------------
Every frame the menus are up
--------------------------------------------------------------------------------
*/

void UI_VRVoice_Frame( void ) {
	const menuDef_t *panel = Menus_FindByName( VRV_PANEL );
	int m;

	// while the panel is up, the page under it hasn't the mouse (the row that
	// had it goes dark, and its description goes)
	if ( panel && ( panel->window.flags & WINDOW_VISIBLE ) ) {
		for ( m = 0; m < (int)ARRAY_LEN( vrvPages ); m++ ) {
			menuDef_t *page = Menus_FindByName( vrvPages[m] );
			if ( page && ( page->window.flags & WINDOW_VISIBLE ) ) {
				Menu_HandleMouseMove( page, -1000.0f, -1000.0f );
			}
		}
	}

	// the engine opens the microphone for the meter while it's up (and lists
	// the microphones)
	if ( vrvMeter.told != ( vrvMeter.drawn ? 1 : 0 ) ) {
		vrvMeter.told = vrvMeter.drawn ? 1 : 0;
		trap->Cvar_Set( "cl_voiceMenu", vrvMeter.told ? "1" : "0" );
	}
	vrvMeter.drawn = qfalse;

	if ( vrvMeter.dragging && !trap->Key_IsDown( A_MOUSE1 ) ) {
		vrvMeter.dragging = qfalse;
	}
	for ( m = 0; m <= (int)ARRAY_LEN( vrvPages ); m++ ) {
		menuDef_t *menu = Menus_FindByName( m < (int)ARRAY_LEN( vrvPages ) ? vrvPages[m] : VRV_PANEL );
		itemDef_t *list, *meter;

		if ( !menu || !( menu->window.flags & WINDOW_VISIBLE ) ) {
			continue;
		}

		// the list shows the microphone chosen as selected, wherever it was
		// chosen (here, voice_mic, or the default after it was unplugged)
		list = Menu_FindItemByName( menu, "vr_voicemic" );
		if ( list && list->type == ITEM_TYPE_LISTBOX && list->typeData.listbox ) {
			listBoxDef_t *lb = list->typeData.listbox;
			UI_VRV_ReadMics();
			if ( list->cursorPos != vrvMics.current ) {
				const int rows = ( lb->elementHeight > 0.0f ) ? Q_max( 1, (int)( ( list->window.rect.h - 2.0f ) / lb->elementHeight ) ) : 1;
				list->cursorPos = vrvMics.current;
				if ( vrvMics.current < lb->startPos ) {
					lb->startPos = vrvMics.current;
				} else if ( vrvMics.current >= lb->startPos + rows ) {
					lb->startPos = vrvMics.current - rows + 1;
				}
			}
		}

		// the meter's name says what it does
		meter = Menu_FindItemByName( menu, "vr_voicelevel" );
		if ( meter ) {
			const qboolean ptt = ( (int)trap->Cvar_VariableValue( "cl_voiceMode" ) == 1 ) ? qtrue : qfalse;
			const char *text = ptt ? "Microphone Level" : "Voice Activation";
			if ( !meter->text || strcmp( meter->text, text ) ) {
				meter->text = text;
				meter->textRect.w = meter->textRect.h = 0.0f;	// (measured again)
			}
			meter->descText = ptt ? "How loud your microphone is (you're heard while +voice is held)" :
				"How loud you are now; you're heard when you go past the mark - click or drag to move it";
			if ( vrvMeter.dragging && ( meter->window.flags & WINDOW_VISIBLE ) && !ptt ) {
				UI_VRV_SetThresholdAt( uiInfo.uiDC.cursorx );
			}
		}
	}
}

/*
--------------------------------------------------------------------------------
Putting them on the Sound page
--------------------------------------------------------------------------------
*/

static menuDef_t	vrvParsed;		// ui/vr_voice.menu's items, parsed afresh for each menu

static qboolean UI_VRV_Parse( void ) {
	pc_token_t token;
	qboolean ok = qfalse;
	const int handle = trap->PC_LoadSource( VRV_MENU_FILE );

	Menu_Init( &vrvParsed );
	if ( !handle ) {
		Com_Printf( S_COLOR_YELLOW "%s not found: the voice chat settings aren't in the menus\n", VRV_MENU_FILE );
		return qfalse;
	}
	while ( trap->PC_ReadToken( handle, &token ) ) {
		if ( !Q_stricmp( token.string, "menudef" ) ) {
			ok = Menu_Parse( handle, &vrvParsed );
			break;
		}
	}
	trap->PC_FreeSource( handle );
	return ( ok && vrvParsed.itemCount > 0 ) ? qtrue : qfalse;
}

static itemDef_t *UI_VRV_Parsed( const char *name ) {
	int i;
	for ( i = 0; i < vrvParsed.itemCount; i++ ) {
		if ( vrvParsed.items[i]->window.name && !Q_stricmp( vrvParsed.items[i]->window.name, name ) ) {
			return vrvParsed.items[i];
		}
	}
	return NULL;
}

typedef struct {
	itemDef_t	*items[VRV_ROW_ITEMS];
	int			count;
	float		y, h;			// (in the menu)
	float		natural;		// how tall the page made it
	qboolean	slider;			// (or the meter)
	qboolean	list;			// the microphones
	qboolean	label;			// the list's name
} vrvRow_t;

// How tall the rows are: as the page has them, or (on a short page) closer
// together.
typedef struct {
	float		row;			// a name and its value
	float		slider;			// a slider (its thumb reaches below its bar)
	float		label;			// the list's name
	float		listRow;		// a microphone in the list
	int			listRows;		// how many the list shows at once
} vrvSizes_t;

static void UI_VRV_RowAdd( vrvRow_t *rows, int *numRows, itemDef_t *it ) {
	const float y = it->window.rectClient.y;
	int r;

	for ( r = 0; r < *numRows; r++ ) {
		if ( fabsf( rows[r].y - y ) < 3.0f && rows[r].count < VRV_ROW_ITEMS ) {
			break;
		}
	}
	if ( r == *numRows ) {
		if ( *numRows == VRV_MAX_ROWS ) {
			return;
		}
		memset( &rows[r], 0, sizeof( rows[r] ) );
		rows[r].y = y;
		( *numRows )++;
	}
	rows[r].items[rows[r].count++] = it;
	rows[r].natural = Q_max( rows[r].natural, it->window.rectClient.h );
	if ( it->type == ITEM_TYPE_SLIDER || it->type == ITEM_TYPE_OWNERDRAW ) {
		rows[r].slider = qtrue;
	}
}

// Every row's height, and all of theirs.
static float UI_VRV_Size( vrvRow_t *rows, int numRows, int numStock, float h0, const vrvSizes_t *s ) {
	float total = 0.0f;
	int i;

	for ( i = 0; i < numRows; i++ ) {
		vrvRow_t *r = &rows[i];
		if ( r->list ) {
			r->h = s->listRows * s->listRow + 4.0f;
		} else if ( r->label ) {
			r->h = s->label;
		} else if ( i < numStock && r->natural > h0 + 0.5f ) {
			r->h = Q_max( r->natural, r->slider ? s->slider : 0.0f );	// (more than a row: as it is)
		} else {
			r->h = r->slider ? s->slider : s->row;
		}
		total += r->h;
	}
	return total;
}

static qboolean UI_VRV_Insert( menuDef_t *menu, int at, itemDef_t *item ) {
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

// A bar that lights up behind a row while the mouse is on it, as the page's
// own highlight bars do (which are where the rows used to be).
static itemDef_t *UI_VRV_Highlight( menuDef_t *menu, int n, float y, float h, const itemDef_t *ref ) {
	const itemDef_t *stock = Menu_FindItemByName( menu, "highlight1" );
	itemDef_t *hl = (itemDef_t *)UI_Alloc( sizeof( itemDef_t ) );

	if ( !hl ) {
		return NULL;
	}
	Item_Init( hl );
	hl->window.name = String_Alloc( va( "vr_highlight%i", n ) );
	hl->window.group = String_Alloc( "highlights" );
	if ( stock ) {
		hl->window.style = stock->window.style;
		hl->window.background = stock->window.background;
		VectorCopy4( stock->window.foreColor, hl->window.foreColor );
		VectorCopy4( stock->window.backColor, hl->window.backColor );
	} else {
		hl->window.style = WINDOW_STYLE_SHADER;
		hl->window.background = uiInfo.uiDC.registerShaderNoMip( "gfx/menus/menu_blendbox" );
		VectorSet4( hl->window.foreColor, 1.0f, 1.0f, 1.0f, 1.0f );
	}
	hl->window.flags = WINDOW_DECORATION;	// (shown while the mouse is on its row)
	hl->window.rectClient.x = ref->window.rectClient.x;
	hl->window.rectClient.w = ref->window.rectClient.w;
	hl->window.rectClient.y = y;
	hl->window.rectClient.h = h;
	return hl;
}

// The list's rows, and the microphones' names cut to fit it.
static void UI_VRV_SetupList( itemDef_t *list, float rowH ) {
	listBoxDef_t *lb = list->typeData.listbox;
	const float fontH = (float)uiInfo.uiDC.textHeight( "Ay", list->textscale, list->iMenuFont );
	const float w = list->window.rectClient.w;

	lb->elementHeight = rowH;
	lb->elementWidth = w;
	list->textaligny = rowH + 2.0f + Q_max( 0.0f, ( rowH - fontH ) * 0.5f );	// (Item_ListBox_Paint draws each name a row up)
	list->textalignment = ITEM_ALIGN_LEFT;
	// names wider than the narrowest list get cut short
	if ( vrvMics.textW <= 0.0f || w - SCROLLBAR_SIZE - 10.0f < vrvMics.textW ) {
		vrvMics.textW = w - SCROLLBAR_SIZE - 10.0f;
		vrvMics.scale = list->textscale;
		vrvMics.font = list->iMenuFont;
	}
}

// (This build can't do EAX.)
static void UI_VRV_HideEax( itemDef_t **eax, int numEax ) {
	int i;
	for ( i = 0; i < numEax; i++ ) {
		eax[i]->window.group = String_Alloc( "vr_hidden" );
		eax[i]->window.flags &= ~WINDOW_VISIBLE;
	}
}

// The page's effects volume: what the voice settings look like, and where they
// go after.
static itemDef_t *UI_VRV_Ref( menuDef_t *menu ) {
	itemDef_t *ref = Menu_FindItemByName( menu, "effects_volume" );
	int i;

	for ( i = 0; !ref && i < menu->itemCount; i++ ) {
		itemDef_t *it = menu->items[i];
		if ( it->type == ITEM_TYPE_SLIDER && it->cvar && !Q_stricmp( it->cvar, "s_volume" ) ) {
			ref = it;
		}
	}
	return ( ref && ref->window.group ) ? ref : NULL;
}

// The panel they're on for a page with no room for them: ui/vr_voice.menu's
// menuDef, laid out as it is there.
static qboolean UI_VRV_Panel( void ) {
	pc_token_t token;
	menuDef_t *panel = Menus_FindByName( VRV_PANEL );
	int handle;

	if ( panel ) {
		return qtrue;
	}
	if ( Menu_Count() >= MAX_MENUS || !( handle = trap->PC_LoadSource( VRV_MENU_FILE ) ) ) {
		return qfalse;
	}
	while ( trap->PC_ReadToken( handle, &token ) ) {
		if ( !Q_stricmp( token.string, "menudef" ) ) {
			Menu_New( handle );
			break;
		}
	}
	trap->PC_FreeSource( handle );
	panel = Menus_FindByName( VRV_PANEL );
	if ( panel ) {
		itemDef_t *list = Menu_FindItemByName( panel, "vr_voicemic" );
		if ( list && list->type == ITEM_TYPE_LISTBOX && list->typeData.listbox ) {
			UI_VRV_SetupList( list, Com_Clamp( 12.0f, 24.0f, list->typeData.listbox->elementHeight ) );
		}
	}
	return panel ? qtrue : qfalse;
}

// A page with no room for them gets this instead: a button, where EAX was (or
// else under the sound settings), that opens the panel. The page is otherwise
// left as it is.
static void UI_VRV_AddButton( menuDef_t *menu, itemDef_t *ref, itemDef_t **eax, int numEax, int first, int last, float h0, float bottom ) {
	itemDef_t *spot = NULL, *button = NULL, *hl;
	rectDef_t r;
	int i;

	for ( i = 0; i < numEax; i++ ) {
		if ( eax[i]->window.name && !Q_stricmp( eax[i]->window.name, "eax" ) ) {
			spot = eax[i];
		}
	}
	if ( spot ) {
		r = spot->window.rectClient;
	} else {
		float lowest = ref->window.rectClient.y + ref->window.rectClient.h;
		for ( i = 0; i < menu->itemCount; i++ ) {
			const itemDef_t *it = menu->items[i];
			if ( it->window.group && !Q_stricmp( it->window.group, ref->window.group ) ) {
				lowest = Q_max( lowest, it->window.rectClient.y + it->window.rectClient.h );
			}
		}
		r = ref->window.rectClient;
		r.y = lowest + 4.0f;
		r.h = h0;
	}
	if ( r.y + r.h <= bottom + 2.0f && UI_VRV_Panel() ) {
		button = (itemDef_t *)UI_Alloc( sizeof( itemDef_t ) );
	}
	if ( !button ) {
		Com_Printf( S_COLOR_YELLOW "The voice chat settings don't fit on the Sound page of %s (cl_voice and the other cl_voice settings still work)\n",
			menu->window.name ? menu->window.name : "a menu" );
		return;
	}
	Item_Init( button );
	button->window.name = String_Alloc( "vr_voicebutton" );
	button->window.group = ref->window.group;
	button->window.style = WINDOW_STYLE_EMPTY;
	button->window.rectClient = r;
	button->type = ITEM_TYPE_BUTTON;
	button->text = String_Alloc( "Voice Chat Settings..." );
	button->descText = String_Alloc( "Voice chat, the microphone you talk into, and how loud you have to be to be heard" );
	button->action = String_Alloc( "play \"sound/interface/button1.wav\" ; open " VRV_PANEL " ; " );
	button->iMenuFont = ref->iMenuFont;
	button->textscale = ref->textscale;
	button->textStyle = ref->textStyle;
	button->textaligny = ref->textaligny;
	button->textalignment = ITEM_ALIGN_CENTER;
	button->textalignx = r.w * 0.5f;
	VectorCopy4( ref->window.foreColor, button->window.foreColor );

	UI_VRV_HideEax( eax, numEax );
	if ( spot ) {
		// (the page's own bar for that row lights up)
		button->mouseEnter = spot->mouseEnter;
		button->mouseExit = spot->mouseExit;
	} else {
		hl = UI_VRV_Highlight( menu, 1, r.y, r.h, ref );
		if ( hl && UI_VRV_Insert( menu, first >= 0 ? first : menu->itemCount, hl ) ) {
			if ( first >= 0 ) {
				last++;
			}
			button->mouseEnter = String_Alloc( "show vr_highlight1 ; " );
			button->mouseExit = String_Alloc( "hide vr_highlight1 ; " );
		}
	}
	UI_VRV_Insert( menu, last >= 0 ? last + 1 : menu->itemCount, button );
	Menu_UpdatePosition( menu );
}

static void UI_VRV_AddTo( menuDef_t *menu ) {
	static const char *ours[] = {
		"vr_voicechat", "vr_voicevolume", "vr_voicemiclabel", "vr_voicemic", "vr_voicemode", "vr_voicelevel", "vr_voicegain"
	};
	vrvRow_t rows[VRV_MAX_ROWS];
	vrvSizes_t sizes[6];
	itemDef_t *ref, *bg, *list = NULL, *meter = NULL, *eax[4];
	int numRows = 0, numStock, numEax = 0, first = -1, last = -1, s, i, j, k;
	float h0, th, valueX, listX, listW, meterW, top, bottom, total = 0.0f, gap, y;

	if ( Menu_FindItemByName( menu, "vr_voicechat" ) || Menu_FindItemByName( menu, "vr_voicebutton" ) ) {
		return;	// (already there)
	}
	ref = UI_VRV_Ref( menu );
	if ( !ref ) {
		return;	// not a Sound page this knows
	}
	if ( !UI_VRV_Parse() ) {
		return;
	}
	if ( menu->itemCount + (int)ARRAY_LEN( ours ) + VRV_MAX_ROWS + 1 > MAX_MENUITEMS ) {
		Com_Printf( S_COLOR_YELLOW "%s: no room for the voice chat settings\n", menu->window.name );
		return;
	}

	// the sound settings already there, row by row from the top (but EAX,
	// which goes)
	for ( i = 0; i < menu->itemCount; i++ ) {
		itemDef_t *it = menu->items[i];
		if ( !it->window.group || Q_stricmp( it->window.group, ref->window.group ) ) {
			continue;
		}
		if ( first < 0 ) {
			first = i;
		}
		last = i;
		if ( it->window.name && ( !Q_stricmp( it->window.name, "eax" ) || !Q_stricmp( it->window.name, "eax_icon" ) ) ) {
			if ( numEax < (int)ARRAY_LEN( eax ) ) {
				eax[numEax++] = it;
			}
			continue;
		}
		UI_VRV_RowAdd( rows, &numRows, it );
	}
	for ( i = 0; i < numRows; i++ ) {		// (in order)
		for ( j = i + 1; j < numRows; j++ ) {
			if ( rows[j].y < rows[i].y ) {
				const vrvRow_t t = rows[i];
				rows[i] = rows[j];
				rows[j] = t;
			}
		}
	}
	numStock = numRows;

	// ours, looking like those
	h0 = ref->window.rectClient.h >= 8.0f ? ref->window.rectClient.h : 14.0f;
	th = (float)uiInfo.uiDC.textHeight( "Ay", ref->textscale, ref->iMenuFont );
	if ( th < 6.0f ) {
		th = 13.0f;
	}
	// the values' column (where the sliders start), and the list under its
	// name, across the page (the microphones' names are long)
	valueX = ( ref->textalignment == ITEM_ALIGN_RIGHT ) ? ref->textalignx + 8.0f : ref->window.rectClient.w * 0.5f;
	meterW = Q_max( 64.0f, ref->window.rectClient.w - 4.0f - valueX );
	listX = ref->window.rectClient.x + 10.0f;
	listW = Q_max( 80.0f, ref->window.rectClient.w - 14.0f );
	for ( i = 0; i < (int)ARRAY_LEN( ours ) && numRows < VRV_MAX_ROWS; i++ ) {
		itemDef_t *a = UI_VRV_Parsed( ours[i] );
		vrvRow_t *row = &rows[numRows];
		if ( !a ) {
			continue;
		}
		a->iMenuFont = ref->iMenuFont;
		a->textscale = ref->textscale;
		a->textStyle = ref->textStyle;
		a->alignment = ref->alignment;
		a->textalignment = ref->textalignment;
		a->textalignx = ref->textalignx;
		a->textaligny = ref->textaligny;
		VectorCopy4( ref->window.foreColor, a->window.foreColor );
		a->window.group = ref->window.group;
		a->window.flags &= ~WINDOW_VISIBLE;		// (shown with the page's sound settings)
		a->mouseEnter = a->mouseExit = NULL;	// (the panel's highlight bars: the page's are below)
		a->window.rectClient.x = ref->window.rectClient.x;
		a->window.rectClient.w = ref->window.rectClient.w;
		a->window.rectClient.h = h0;
		memset( row, 0, sizeof( *row ) );
		row->items[row->count++] = a;
		row->natural = h0;
		row->slider = ( a->type == ITEM_TYPE_SLIDER || a->type == ITEM_TYPE_OWNERDRAW ) ? qtrue : qfalse;
		row->label = ( a->window.flags & WINDOW_DECORATION ) ? qtrue : qfalse;
		if ( a->type == ITEM_TYPE_LISTBOX && a->typeData.listbox ) {
			row->list = qtrue;
			list = a;
		}
		if ( a->type == ITEM_TYPE_OWNERDRAW ) {
			meter = a;
		}
		numRows++;
	}

	// down the page: as much room as there is, and no more
	top = numStock ? rows[0].y : ref->window.rectClient.y;
	bg = Menu_FindItemByName( menu, "setup_background" );
	if ( bg && bg->window.rectClient.y + bg->window.rectClient.h > top + 100.0f ) {
		bottom = bg->window.rectClient.y + bg->window.rectClient.h - 4.0f;
	} else {
		bottom = top + 220.0f;
	}

	// the rows as tall as the page's own (the list as long as there's room
	// for), or on a short page (the in-game one) closer together, as close
	// as the main menu's (rather than fewer microphones in the list)
	sizes[0].row = h0;
	sizes[0].slider = Q_max( h0, VRV_SLIDER_H );
	sizes[0].label = Q_min( h0, th + 4.0f );
	sizes[0].listRow = Com_Clamp( 12.0f, 16.0f, h0 );
	sizes[0].listRows = VRV_LIST_ROWS;
	sizes[1] = sizes[0];
	sizes[1].listRows = VRV_LIST_ROWS - 1;
	for ( s = 2; s < (int)ARRAY_LEN( sizes ); s++ ) {
		const float pad = ( s & 1 ) ? 2.0f : 3.0f;		// (a little space above and below the words)
		sizes[s].row = Q_min( h0, Q_max( 14.0f, th + pad ) );
		sizes[s].slider = VRV_SLIDER_H;
		sizes[s].label = Q_min( sizes[s].row, th + pad - 1.0f );
		sizes[s].listRow = Q_min( sizes[0].listRow, Q_max( 12.0f, th + 1.0f ) );
		sizes[s].listRows = ( s < 4 ) ? VRV_LIST_ROWS - 1 : VRV_LIST_ROWS - 2;
	}
	for ( s = 0; s < (int)ARRAY_LEN( sizes ); s++ ) {
		total = UI_VRV_Size( rows, numRows, numStock, h0, &sizes[s] );
		if ( total <= bottom - top + 2.0f ) {
			break;
		}
	}
	if ( s == (int)ARRAY_LEN( sizes ) ) {
		// (a page a mod has filled): a button for the panel instead
		UI_VRV_AddButton( menu, ref, eax, numEax, first, last, h0, bottom );
		return;
	}
	gap = ( numRows > 1 && total < bottom - top ) ? Q_min( ( bottom - top - total ) / ( numRows - 1 ), h0 ) : 0.0f;
	UI_VRV_HideEax( eax, numEax );

	y = top;
	for ( i = 0; i < numRows; i++ ) {
		rows[i].y = y;
		for ( j = 0; j < rows[i].count; j++ ) {
			itemDef_t *it = rows[i].items[j];
			it->window.rectClient.y = y;
			it->window.rectClient.h = rows[i].h;	// (no more, nor less: what the mouse is on)
			if ( it->window.border ) {
				it->window.rectClient.h -= it->window.borderSize;	// (a border moves it down by its size)
			}
			if ( it == list ) {
				it->window.rectClient.x = listX;
				it->window.rectClient.w = listW;
			}
		}
		y += rows[i].h + gap;
	}
	if ( list ) {
		UI_VRV_SetupList( list, sizes[s].listRow );
	}
	if ( meter ) {
		meter->special = meterW;	// (the meter's width: to the page's edge)
	}

	// highlight bars behind the rows (not the list, which shows its own, nor
	// the list's name)
	k = 0;
	for ( i = 0; i < numRows; i++ ) {
		itemDef_t *hl;
		if ( rows[i].list || rows[i].label ) {
			continue;
		}
		hl = UI_VRV_Highlight( menu, ++k, rows[i].y, rows[i].h, ref );
		if ( !hl || !UI_VRV_Insert( menu, first >= 0 ? first : menu->itemCount, hl ) ) {
			continue;
		}
		if ( first >= 0 ) {
			first++;	// (the highlights go behind the sound settings)
			last++;
		}
		for ( j = 0; j < rows[i].count; j++ ) {
			rows[i].items[j]->mouseEnter = String_Alloc( va( "show vr_highlight%i ; ", k ) );
			rows[i].items[j]->mouseExit = String_Alloc( va( "hide vr_highlight%i ; ", k ) );
		}
	}

	// and ours go in after the sound settings (drawn with them, under
	// anything the page puts on top)
	j = ( last >= 0 ) ? last + 1 : menu->itemCount;
	for ( i = numStock; i < numRows; i++ ) {
		for ( k = 0; k < rows[i].count; k++ ) {
			if ( UI_VRV_Insert( menu, j, rows[i].items[k] ) ) {
				j++;
			}
		}
	}
	Menu_UpdatePosition( menu );
}

// After the menus load: the voice chat settings onto every Sound page there
// is. (While the menus' definitions, menudef.h, are still loaded.)
void UI_VRVoice_AddSettings( void ) {
	int m;

	for ( m = 0; m < (int)ARRAY_LEN( vrvPages ); m++ ) {
		menuDef_t *menu = Menus_FindByName( vrvPages[m] );
		if ( menu ) {
			UI_VRV_AddTo( menu );
		}
	}
}
