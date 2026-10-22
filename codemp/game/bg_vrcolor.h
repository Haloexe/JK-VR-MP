/*
===========================================================================
OpenJK VR (multiplayer) - saber colours: any colour for each saber (RGB),
and a black core - a dark core inside the coloured glow.

A player picks them on the Saber Colour page (ui_vrcolor.c), which sets
their userinfo:
  saberRGB1, saberRGB2   "rrggbb" (a leading '#' allowed; empty: the stock
                         colour, color1/color2 as ever)
  saberCore1, saberCore2 1: a black core
and color1/color2 to the stock colour nearest, which the normal game shows.
A server of this build ("vrSaberRGB 1" in its serverinfo: g_vrcolor.c) tells
everyone in their player info (CS_PLAYERS):
  sc1, sc2               "rrggbb", "rrggbbk" with a black core, "k" for a
                         black core on the stock colour; none: as ever
and every game of this build draws them (cg_players.c, CG_DoSaber).

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#ifndef BG_VRCOLOR_H
#define BG_VRCOLOR_H

#define VR_SC_GLOW_FLOOR	64		// no blade glows dimmer than this: its brightest of red, green and blue (of 255)

// A black core's blade (cg_players.c, CG_DoSaber; ui_saber.c, UI_DoSaber):
#define VR_SC_GLOW_WIDER	1.1f	// its glow this much wider than a white core's,
#define VR_SC_CORE_WIDER	1.15f	// its core this much wider,
#define VR_SC_RIM_WIDE		2.2f	// its bright edge this many times the core's width
#define VR_SC_DEPTH_AHEAD	3.0f	// where its core is, for what's drawn after it: this much nearer the eye (a quarter of the way at most)
// Black cores are drawn in turns, farthest first (tr_vrsaber.cpp has each
// turn's shaders: gfx/vr/bc<n>_glow, _rim, _core and _depth, n 0 to
// VR_SC_RANKS - 1), so a nearer blade always goes over a farther dark core.
#define VR_SC_RANKS			12

typedef struct {
	qboolean	rgb;			// a colour of its own (else the stock colour)
	byte		color[3];		// it: red, green, blue
	qboolean	blackCore;		// a dark core inside the glow
} vrBladeColor_t;

// One hex digit's value (-1: not one).
static QINLINE int BG_VRSC_Hex( char c ) {
	if ( c >= '0' && c <= '9' ) {
		return c - '0';
	}
	if ( c >= 'a' && c <= 'f' ) {
		return c - 'a' + 10;
	}
	if ( c >= 'A' && c <= 'F' ) {
		return c - 'A' + 10;
	}
	return -1;
}

// "rrggbb" or "#rrggbb", upper or lower case, and nothing else: its colour.
static QINLINE qboolean BG_VRSC_ParseRGB( const char *s, byte out[3] ) {
	int i;

	if ( !s ) {
		return qfalse;
	}
	if ( *s == '#' ) {
		s++;
	}
	for ( i = 0; i < 6; i++ ) {
		if ( BG_VRSC_Hex( s[i] ) < 0 ) {
			return qfalse;
		}
	}
	if ( s[6] ) {
		return qfalse;
	}
	for ( i = 0; i < 3; i++ ) {
		out[i] = (byte)( BG_VRSC_Hex( s[i * 2] ) * 16 + BG_VRSC_Hex( s[i * 2 + 1] ) );
	}
	return qtrue;
}

// A saber's colour as the server tells it (sc1, sc2): "rrggbb", "rrggbbk",
// "k" or none. Anything else (a '#' before the digits too, which the server
// never sends): the stock colour with a white core.
static QINLINE void BG_VRSC_Parse( const char *s, vrBladeColor_t *out ) {
	char rgb[8];
	int len;

	memset( out, 0, sizeof( *out ) );
	if ( !s || !*s ) {
		return;
	}
	len = (int)strlen( s );
	if ( len == 1 ) {
		out->blackCore = (qboolean)( s[0] == 'k' );
		return;
	}
	if ( len == 7 && s[6] == 'k' ) {
		Q_strncpyz( rgb, s, 7 );
		if ( BG_VRSC_ParseRGB( rgb, out->color ) ) {
			out->rgb = out->blackCore = qtrue;
		}
		return;
	}
	if ( len == 6 && BG_VRSC_ParseRGB( s, out->color ) ) {
		out->rgb = qtrue;
	}
}

// A saber's colour as the server tells it ("" for the stock colour with a
// white core: nothing to tell); out at least 8 long.
static QINLINE void BG_VRSC_Format( const vrBladeColor_t *in, char *out, int size ) {
	if ( in->rgb ) {
		Com_sprintf( out, size, "%02x%02x%02x%s", in->color[0], in->color[1], in->color[2], in->blackCore ? "k" : "" );
	} else {
		Q_strncpyz( out, in->blackCore ? "k" : "", size );
	}
}

// A saber's colour from its settings (userinfo saberRGB1 and saberCore1, or
// the 2s): a colour that isn't six hex digits is the stock one.
static QINLINE void BG_VRSC_FromSettings( const char *rgb, const char *core, vrBladeColor_t *out ) {
	memset( out, 0, sizeof( *out ) );
	out->rgb = BG_VRSC_ParseRGB( rgb, out->color );
	out->blackCore = (qboolean)( core && atoi( core ) > 0 );
}

// The stock colour nearest a colour, by its hue - what the normal game shows
// instead (a stock colour's own light colour gives that colour); pale ones
// too (pink: red). Hardly a colour at all (grey, white, black): blue, the
// game's own default.
static QINLINE int BG_VRSC_Nearest( const byte c[3] ) {
	const float r = c[0], g = c[1], b = c[2];
	const float mx = Q_max( r, Q_max( g, b ) ), mn = Q_min( r, Q_min( g, b ) ), d = mx - mn;
	float h;

	if ( mx <= 0.0f || d < mx * 0.1f ) {
		return SABER_BLUE;
	}
	if ( mx == r ) {
		h = 60.0f * ( g - b ) / d;
	} else if ( mx == g ) {
		h = 60.0f * ( ( b - r ) / d + 2.0f );
	} else {
		h = 60.0f * ( ( r - g ) / d + 4.0f );
	}
	if ( h < 0.0f ) {
		h += 360.0f;
	}
	// (halfway between the stock colours' hues: red 0, orange 27, yellow 60,
	// green 120, blue 225, purple 292)
	if ( h < 15.0f || h >= 326.0f ) {
		return SABER_RED;
	}
	if ( h < 45.0f ) {
		return SABER_ORANGE;
	}
	if ( h < 90.0f ) {
		return SABER_YELLOW;
	}
	if ( h < 172.5f ) {
		return SABER_GREEN;
	}
	if ( h < 258.5f ) {
		return SABER_BLUE;
	}
	return SABER_PURPLE;
}

// A colour glowing no dimmer than the floor: its brightest of red, green
// and blue raised to VR_SC_GLOW_FLOOR if it's below, the others with it
// (black: an even grey) - so no blade's ever near invisible.
static QINLINE void BG_VRSC_Floor( const byte in[3], byte out[3] ) {
	const int mx = Q_max( in[0], Q_max( in[1], in[2] ) );
	int i;

	for ( i = 0; i < 3; i++ ) {
		if ( mx >= VR_SC_GLOW_FLOOR ) {
			out[i] = in[i];
		} else if ( mx <= 0 ) {
			out[i] = VR_SC_GLOW_FLOOR;
		} else {
			out[i] = (byte)( in[i] * VR_SC_GLOW_FLOOR / mx );
		}
	}
}

// A black core's turn among the others (0 drawn first) by how far it is
// from the eye: farther ones first, each turn half as far again as the next
// (8, 11, 16, 23, 32 ... 256 units and beyond). Two in one turn are drawn in
// any order; that's two at much the same distance.
static QINLINE int BG_VRSC_Rank( float dist ) {
	float edge = 8.0f;
	int nearer = 0;

	while ( nearer < VR_SC_RANKS - 1 && dist >= edge ) {
		edge *= 1.41421356f;
		nearer++;
	}
	return VR_SC_RANKS - 1 - nearer;
}

#endif // BG_VRCOLOR_H
