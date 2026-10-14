/*
===========================================================================
OpenJK VR (multiplayer) - the headset's keyboard. Whenever something takes
typing - the chat line (the wheel's Say...), the console, or a text field
in a menu (the UI says which: ui_vrTyping "1 <y>", y the middle of the
field) - a keyboard comes up on the menu screen, and the controller's
pointer types on it: point at a key and pull the trigger. It's laid out as
a US keyboard: Shift (once: the next key) for capitals and the symbols
over the numbers; held, Backspace repeats. Enter is Enter; Close is
Escape, so the chat line or the console shuts and a menu field is done
with (B does the same). It goes at the bottom of the screen, or the top if
the field being typed in is low (so it isn't covered). What it types goes
in as typed keys, so everything that takes the keyboard takes it, and the
real keyboard still works as ever.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "client.h"
#include "cl_vr.h"
#include "sys/sys_public.h"

#ifdef OPENJK_VR

void Sys_QueEvent( int time, sysEventType_t type, int value, int value2, int ptrLength, void *ptr );	// (sys_main.cpp)

#define VRK_UNIT			40.6f	// a key's width (640x480 units)...
#define VRK_ROW				40.0f	// ...and height...
#define VRK_GAP				3.0f	// ...less this all round
#define VRK_ROWS			5
#define VRK_WIDTH_UNITS		15.0f	// the widest row
#define VRK_EDGE			6.0f	// round the keys
#define VRK_LABEL			16.0f	// the labels this tall
#define VRK_REPEAT_FIRST	450		// Backspace held: again after this long...
#define VRK_REPEAT			60		// ...and then every this long

enum {
	VRK_CHAR,		// types plain, or shifted with Shift
	VRK_SPACE,
	VRK_BACKSPACE,
	VRK_ENTER,
	VRK_SHIFT,
	VRK_CLOSE
};

typedef struct {
	char		plain, shifted;
	int			kind;		// VRK_*
	float		units;		// how wide
	const char	*label;		// (one that doesn't type a character)
} vrkKey_t;

typedef struct {
	float		indent;		// units in from the left
	int			count;
	vrkKey_t	key[16];
} vrkRow_t;

#define VRK_C( p, s )	{ p, s, VRK_CHAR, 1.0f, NULL }

static const vrkRow_t vrkRows[VRK_ROWS] = {
	{ 0.0f, 14, {
		VRK_C( '`', '~' ), VRK_C( '1', '!' ), VRK_C( '2', '@' ), VRK_C( '3', '#' ), VRK_C( '4', '$' ), VRK_C( '5', '%' ),
		VRK_C( '6', '^' ), VRK_C( '7', '&' ), VRK_C( '8', '*' ), VRK_C( '9', '(' ), VRK_C( '0', ')' ), VRK_C( '-', '_' ),
		VRK_C( '=', '+' ), { 0, 0, VRK_BACKSPACE, 2.0f, "Back" } } },
	{ 0.5f, 13, {
		VRK_C( 'q', 'Q' ), VRK_C( 'w', 'W' ), VRK_C( 'e', 'E' ), VRK_C( 'r', 'R' ), VRK_C( 't', 'T' ), VRK_C( 'y', 'Y' ),
		VRK_C( 'u', 'U' ), VRK_C( 'i', 'I' ), VRK_C( 'o', 'O' ), VRK_C( 'p', 'P' ), VRK_C( '[', '{' ), VRK_C( ']', '}' ),
		VRK_C( '\\', '|' ) } },
	{ 1.0f, 12, {
		VRK_C( 'a', 'A' ), VRK_C( 's', 'S' ), VRK_C( 'd', 'D' ), VRK_C( 'f', 'F' ), VRK_C( 'g', 'G' ), VRK_C( 'h', 'H' ),
		VRK_C( 'j', 'J' ), VRK_C( 'k', 'K' ), VRK_C( 'l', 'L' ), VRK_C( ';', ':' ), VRK_C( '\'', '"' ),
		{ 0, 0, VRK_ENTER, 2.0f, "Enter" } } },
	{ 0.0f, 12, {
		{ 0, 0, VRK_SHIFT, 2.0f, "Shift" },
		VRK_C( 'z', 'Z' ), VRK_C( 'x', 'X' ), VRK_C( 'c', 'C' ), VRK_C( 'v', 'V' ), VRK_C( 'b', 'B' ), VRK_C( 'n', 'N' ),
		VRK_C( 'm', 'M' ), VRK_C( ',', '<' ), VRK_C( '.', '>' ), VRK_C( '/', '?' ),
		{ 0, 0, VRK_SHIFT, 2.0f, "Shift" } } },
	{ 1.5f, 3, {
		{ 0, 0, VRK_CLOSE, 2.5f, "Close" }, { ' ', ' ', VRK_SPACE, 8.0f, "" }, { 0, 0, VRK_ENTER, 2.5f, "Enter" } } },
};

static struct {
	qboolean	shift;		// the next key's shifted
	qboolean	held;		// Backspace held down (repeating)...
	int			next;		// ...again at this time
} vrk;

// A menu's field takes typing (the UI says so, and where it is).
static qboolean VRK_UITyping( float *fieldY ) {
	const char *s = Cvar_VariableString( "ui_vrTyping" );

	if ( s[0] != '1' ) {
		return qfalse;
	}
	if ( fieldY ) {
		*fieldY = ( s[1] == ' ' ) ? (float)atof( s + 2 ) : 0.0f;
	}
	return qtrue;
}

qboolean VRK_Wanted( void ) {
	const int catcher = Key_GetCatcher();

	if ( catcher & KEYCATCH_CONSOLE ) {
		return qtrue;
	}
	if ( catcher & KEYCATCH_UI ) {
		return VRK_UITyping( NULL );
	}
	return (qboolean)( ( catcher & KEYCATCH_MESSAGE ) && cls.state == CA_ACTIVE );
}

static float VRK_Left( void ) {
	return ( SCREEN_WIDTH - VRK_WIDTH_UNITS * VRK_UNIT ) * 0.5f;
}

static float VRK_Top( void ) {
	float fieldY;

	if ( !( Key_GetCatcher() & KEYCATCH_CONSOLE ) && ( Key_GetCatcher() & KEYCATCH_UI ) && VRK_UITyping( &fieldY ) &&
		fieldY > SCREEN_HEIGHT * 0.5f ) {
		return VRK_EDGE;	// (the field's low: the keyboard goes up)
	}
	return SCREEN_HEIGHT - VRK_EDGE - VRK_ROWS * VRK_ROW;
}

// A key's box.
static void VRK_KeyRect( int r, int k, float *x, float *y, float *w, float *h ) {
	float left = VRK_Left() + vrkRows[r].indent * VRK_UNIT;
	int i;

	for ( i = 0; i < k; i++ ) {
		left += vrkRows[r].key[i].units * VRK_UNIT;
	}
	*x = left + VRK_GAP * 0.5f;
	*y = VRK_Top() + r * VRK_ROW + VRK_GAP * 0.5f;
	*w = vrkRows[r].key[k].units * VRK_UNIT - VRK_GAP;
	*h = VRK_ROW - VRK_GAP;
}

// The key at x, y (row -1: none, between keys); qfalse: not on the
// keyboard at all.
static qboolean VRK_Hit( float x, float y, int *row, int *key ) {
	const float left = VRK_Left(), top = VRK_Top();
	int r, k;

	*row = *key = -1;
	if ( x < left - VRK_EDGE || x > left + VRK_WIDTH_UNITS * VRK_UNIT + VRK_EDGE ||
		y < top - VRK_EDGE || y > top + VRK_ROWS * VRK_ROW + VRK_EDGE ) {
		return qfalse;
	}
	for ( r = 0; r < VRK_ROWS; r++ ) {
		for ( k = 0; k < vrkRows[r].count; k++ ) {
			float kx, ky, kw, kh;
			VRK_KeyRect( r, k, &kx, &ky, &kw, &kh );
			if ( x >= kx && x < kx + kw && y >= ky && y < ky + kh ) {
				*row = r;
				*key = k;
				return qtrue;
			}
		}
	}
	return qtrue;
}

static void VRK_Char( int ch ) {
	Sys_QueEvent( Sys_Milliseconds(), SE_CHAR, ch, 0, 0, NULL );
}

static void VRK_Key( int key ) {
	const int t = Sys_Milliseconds();

	Sys_QueEvent( t, SE_KEY, key, qtrue, 0, NULL );
	Sys_QueEvent( t, SE_KEY, key, qfalse, 0, NULL );
}

static void VRK_Type( const vrkKey_t *key ) {
	switch ( key->kind ) {
	case VRK_CHAR:
		VRK_Char( vrk.shift ? key->shifted : key->plain );
		vrk.shift = qfalse;
		break;
	case VRK_SPACE:
		VRK_Char( ' ' );
		break;
	case VRK_BACKSPACE:
		VRK_Char( 'h' - 'a' + 1 );	// (ctrl-h: what every field takes for it)
		vrk.held = qtrue;
		vrk.next = Sys_Milliseconds() + VRK_REPEAT_FIRST;
		break;
	case VRK_ENTER:
		VRK_Key( A_ENTER );
		vrk.shift = qfalse;
		break;
	case VRK_SHIFT:
		vrk.shift = (qboolean)!vrk.shift;
		break;
	case VRK_CLOSE:
		VRK_Key( A_ESCAPE );
		vrk.shift = qfalse;
		break;
	}
}

// The trigger pulled, pointing at x, y: qtrue if it's the keyboard's (a key
// typed, or between keys: nothing), so it isn't a click for what's under it.
qboolean VRK_Press( float x, float y ) {
	int r, k;

	if ( !VRK_Wanted() || !VRK_Hit( x, y, &r, &k ) ) {
		return qfalse;
	}
	if ( r >= 0 ) {
		VRK_Type( &vrkRows[r].key[k] );
	}
	return qtrue;
}

void VRK_Release( void ) {
	vrk.held = qfalse;
}

// Every frame: Backspace held repeats.
void VRK_Frame( void ) {
	if ( !vrk.held ) {
		return;
	}
	if ( !VRK_Wanted() ) {
		vrk.held = qfalse;
		vrk.shift = qfalse;
		return;
	}
	if ( Sys_Milliseconds() - vrk.next >= 0 ) {
		VRK_Char( 'h' - 'a' + 1 );
		vrk.next = Sys_Milliseconds() + VRK_REPEAT;
	}
}

// Its label, as it is now.
static const char *VRK_Label( const vrkKey_t *key, char *buf ) {
	if ( key->kind != VRK_CHAR ) {
		return key->label;
	}
	buf[0] = vrk.shift ? key->shifted : key->plain;
	buf[1] = '\0';
	return buf;
}

void CL_DrawMenuRect( float x, float y, float width, float height, float borderSize, vec4_t elementBackgroundColor, vec4_t elementBorderColor );

// Over everything on the menu screen; the pointer (x, y), if it's on it.
void VRK_Draw( float px, float py, qboolean pointing ) {
	vec4_t back = { 0.02f, 0.03f, 0.06f, 0.88f }, edge = { 0.4f, 0.55f, 0.85f, 0.9f };
	vec4_t keyBack = { 0.12f, 0.15f, 0.22f, 0.95f }, keyEdge = { 0.35f, 0.45f, 0.65f, 1.0f };
	vec4_t hoverBack = { 0.2f, 0.45f, 0.85f, 1.0f }, hoverEdge = { 0.8f, 0.9f, 1.0f, 1.0f };
	vec4_t litBack = { 0.55f, 0.4f, 0.1f, 1.0f };
	vec4_t white = { 1.0f, 1.0f, 1.0f, 1.0f }, dot = { 1.0f, 1.0f, 1.0f, 1.0f }, dotEdge = { 0.0f, 0.0f, 0.0f, 0.9f };
	const float left = VRK_Left(), top = VRK_Top();
	const int font = cls.menuFont;
	const float base = (float)re->Font_HeightPixels( font, 1.0f );
	const float scale = ( base > 0.0f ) ? VRK_LABEL / base : 1.0f;
	int hitRow = -1, hitKey = -1, r, k;

	if ( pointing ) {
		VRK_Hit( px, py, &hitRow, &hitKey );
	}
	CL_DrawMenuRect( left - VRK_EDGE, top - VRK_EDGE, VRK_WIDTH_UNITS * VRK_UNIT + VRK_EDGE * 2.0f, VRK_ROWS * VRK_ROW + VRK_EDGE * 2.0f,
		2.0f, back, edge );
	for ( r = 0; r < VRK_ROWS; r++ ) {
		for ( k = 0; k < vrkRows[r].count; k++ ) {
			const vrkKey_t *key = &vrkRows[r].key[k];
			const qboolean hover = (qboolean)( r == hitRow && k == hitKey );
			char buf[2];
			const char *label = VRK_Label( key, buf );
			float x, y, w, h;
			VRK_KeyRect( r, k, &x, &y, &w, &h );
			CL_DrawMenuRect( x, y, w, h, 1.0f, hover ? hoverBack : ( ( key->kind == VRK_SHIFT && vrk.shift ) ? litBack : keyBack ),
				hover ? hoverEdge : keyEdge );
			if ( label && label[0] ) {
				const int lw = re->Font_StrLenPixels( label, font, scale );
				const int lh = re->Font_HeightPixels( font, scale );
				re->Font_DrawString( (int)( x + ( w - lw ) * 0.5f ), (int)( y + ( h - lh ) * 0.5f ), label, white, font, -1, scale );
			}
		}
	}
	if ( pointing ) {
		CL_DrawMenuRect( px - 4.0f, py - 4.0f, 8.0f, 8.0f, 1.0f, dot, dotEdge );
	}
	re->SetColor( NULL );
}

#endif // OPENJK_VR
