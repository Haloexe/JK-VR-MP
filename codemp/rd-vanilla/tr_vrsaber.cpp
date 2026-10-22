/*
===========================================================================
OpenJK VR (multiplayer) - the renderer's own images and shaders for what the
VR game draws: blades of any colour, black cores (cg_players.c, CG_DoSaber;
ui_saber.c, UI_DoSaber) and the holster rings (cg_vrholster.c).

They're made here, not read from files, so they're there on every server -
a pure one (sv_pure 1) reads no loose files - with nothing to download:
- the images are drawn at start, among the renderer's own (*white, ...);
- the shaders' text goes before the shader files' as they're read (so a
  file's of the same name - base/shaders/openjkvr.shader, which an older
  build put there - doesn't stand in their place).

The blade (RT_SABER_GLOW: sprites along it; RT_LINE: one quad down its
middle, s across it, t from 0 at its tip to 1 at its base) is tinted by the
entity's colour (rgbGen vertex): the images are white.
  gfx/vr/saber_glow       the glow, tinted
  gfx/vr/saber_core       the core: tinted edges, a white middle
  gfx/vr/holster_ring     the holsters' markers (each corner's own colour
                          and see-through-ness)
A blade with a black core is drawn as a white core's is, but for its
middle: dark, as wide as a white core's white, ending a little short of
the blade's tip, rounded off, so the blade's colour closes round it. It's
drawn in its turn among the others (its distance: bg_vrcolor.h,
BG_VRSC_Rank), farthest first - glow, core, depth, then the next turn's -
after the blended surfaces (SS_BLEND0: glass, water) and before the
additive ones (SS_BLEND1: every other blade, trails, sparks):
  gfx/vr/bc<n>_glow       its glow, tinted
  gfx/vr/bc<n>_core       its core: tinted edges, the dark middle blended
                          over them (drawn dark into the glow pass's
                          picture too, so the bloom added over the blade
                          afterwards doesn't light the middle back up)
  gfx/vr/bc<n>_depth      where the dark middle is, nothing drawn: a little
                          nearer the eye than it (cg_players.c), so what's
                          drawn after and behind it - a farther blade, its
                          own trail - doesn't show through it, while a
                          nearer blade goes over it
The dark core can't be sorted simply after every glow: blades write no
depth, so it would go over a nearer blade too.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "tr_local.h"

#define VRS_SIZE		64		// (each image's width and height - but the dark core's)
#define VRS_DARK_LEN	512		// (the dark core's height: down the blade, fine enough for its rounded end)
#define VRS_RANKS		12		// black cores' turns (bg_vrcolor.h's VR_SC_RANKS)

static float VRS_Smooth( float e0, float e1, float x ) {
	float t = ( x - e0 ) / ( e1 - e0 );
	if ( t < 0.0f ) {
		t = 0.0f;
	} else if ( t > 1.0f ) {
		t = 1.0f;
	}
	return t * t * ( 3.0f - 2.0f * t );
}

// The glow sprite's brightness at d (0 its middle, 1 its edge).
float R_VR_SaberGlow( float d ) {
	if ( d >= 1.0f ) {
		return 0.0f;
	}
	return expf( -( d / 0.42f ) * ( d / 0.42f ) ) * ( 1.0f - d * d );
}

// Down the blade, the core rounds off over its last few texels at the tip.
float R_VR_SaberTip( float t ) {
	return VRS_Smooth( 0.0f, 3.0f / VRS_SIZE, t );
}

// Across the core (s 0..1): the white middle, the tinted edges.
float R_VR_SaberWhite( float s ) {
	const float x = ( s - 0.5f ) / 0.17f;
	return expf( -x * x );
}

float R_VR_SaberEdge( float s ) {
	return 0.85f * powf( Q_max( 0.0f, sinf( (float)M_PI * s ) ), 1.2f );
}

// A black core's dark middle (s across the core, t down it from its tip):
// near black across the middle, soft at its sides - as wide as a white
// core's white - ending a little short of the blade's tip (of its length),
// rounded off.
#define VRS_DARK_WIDE	0.55f	// (its half-width, of the core's)
#define VRS_DARK_SHORT	0.012f	// (its end, this far from the blade's tip)
#define VRS_DARK_ROUND	0.02f	// (rounding off over this)

float R_VR_SaberDark( float s, float t ) {
	const float x = fabsf( s - 0.5f ) * 2.0f;
	float w = 1.0f;

	if ( t < VRS_DARK_SHORT ) {
		return 0.0f;
	}
	if ( t < VRS_DARK_SHORT + VRS_DARK_ROUND ) {
		const float a = ( VRS_DARK_SHORT + VRS_DARK_ROUND - t ) / VRS_DARK_ROUND;
		w = sqrtf( Q_max( 0.0f, 1.0f - a * a ) );
		if ( w <= 0.0f ) {
			return 0.0f;
		}
	}
	return 0.95f * ( 1.0f - VRS_Smooth( 0.55f, 1.0f, x / ( VRS_DARK_WIDE * w ) ) );
}

static byte VRS_Byte( float v ) {
	return (byte)Com_Clampi( 0, 255, (int)( v * 255.0f + 0.5f ) );
}

// R_CreateBuiltinImages: the blade images (kept for good, as the renderer's
// own are: a name starting '*', no '/').
void R_VR_CreateSaberImages( void ) {
	static byte glow[VRS_SIZE][VRS_SIZE][4], white[VRS_SIZE][VRS_SIZE][4], edge[VRS_SIZE][VRS_SIZE][4],
		dark[VRS_DARK_LEN][VRS_SIZE][4];
	int x, y;

	for ( y = 0; y < VRS_SIZE; y++ ) {
		const float t = ( y + 0.5f ) / VRS_SIZE, tip = R_VR_SaberTip( t );
		for ( x = 0; x < VRS_SIZE; x++ ) {
			const float s = ( x + 0.5f ) / VRS_SIZE;
			const float dx = s * 2.0f - 1.0f, dy = t * 2.0f - 1.0f;
			const byte g = VRS_Byte( R_VR_SaberGlow( sqrtf( dx * dx + dy * dy ) ) );
			const byte w = VRS_Byte( R_VR_SaberWhite( s ) * tip );
			const byte e = VRS_Byte( R_VR_SaberEdge( s ) * tip );
			glow[y][x][0] = glow[y][x][1] = glow[y][x][2] = glow[y][x][3] = g;
			white[y][x][0] = white[y][x][1] = white[y][x][2] = white[y][x][3] = w;
			edge[y][x][0] = edge[y][x][1] = edge[y][x][2] = edge[y][x][3] = e;
		}
	}
	for ( y = 0; y < VRS_DARK_LEN; y++ ) {
		const float t = ( y + 0.5f ) / VRS_DARK_LEN;
		for ( x = 0; x < VRS_SIZE; x++ ) {
			dark[y][x][0] = dark[y][x][1] = 6;
			dark[y][x][2] = 7;
			dark[y][x][3] = VRS_Byte( R_VR_SaberDark( ( x + 0.5f ) / VRS_SIZE, t ) );
		}
	}
	R_CreateImage( "*vrsaberglow", (byte *)glow, VRS_SIZE, VRS_SIZE, GL_RGBA, qtrue, qfalse, qfalse, GL_CLAMP );
	R_CreateImage( "*vrsaberwhite", (byte *)white, VRS_SIZE, VRS_SIZE, GL_RGBA, qtrue, qfalse, qfalse, GL_CLAMP );
	R_CreateImage( "*vrsaberedge", (byte *)edge, VRS_SIZE, VRS_SIZE, GL_RGBA, qtrue, qfalse, qfalse, GL_CLAMP );
	R_CreateImage( "*vrsaberdark", (byte *)dark, VRS_SIZE, VRS_DARK_LEN, GL_RGBA, qtrue, qfalse, qfalse, GL_CLAMP );
}

// A black core's turn's shaders' sort: n the turn (0 first), part 0 its
// glow, 1 its core, 2 its depth.
float R_VR_BlackCoreSort( int n, int part ) {
	return (float)SS_BLEND0 + 0.1f + n * 0.06f + part * 0.01f;
}

// ScanAndLoadShaderFiles: the shaders' text, before the files'.
const char *R_VR_BuiltinShaderText( void ) {
	static char text[12288];
	int n;

	Q_strncpyz( text,
		"gfx/vr/saber_glow\n"
		"{\n"
		"	cull none\n"
		"	nopicmip\n"
		"	{\n"
		"		clampmap *vrsaberglow\n"
		"		blendFunc GL_ONE GL_ONE\n"
		"		rgbGen vertex\n"
		"		glow\n"
		"	}\n"
		"}\n"
		"gfx/vr/saber_core\n"
		"{\n"
		"	cull none\n"
		"	nopicmip\n"
		"	{\n"
		"		clampmap *vrsaberedge\n"
		"		blendFunc GL_ONE GL_ONE\n"
		"		rgbGen vertex\n"
		"		glow\n"
		"	}\n"
		"	{\n"
		"		clampmap *vrsaberwhite\n"
		"		blendFunc GL_ONE GL_ONE\n"
		"		rgbGen identity\n"
		"		glow\n"
		"	}\n"
		"}\n"
		"gfx/vr/holster_ring\n"
		"{\n"
		"	cull none\n"
		"	nopicmip\n"
		"	{\n"
		"		map $whiteimage\n"
		"		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA\n"
		"		rgbGen vertex\n"
		"		alphaGen vertex\n"
		"	}\n"
		"}\n", sizeof( text ) );
	for ( n = 0; n < VRS_RANKS; n++ ) {
		Q_strcat( text, sizeof( text ), va(
			"gfx/vr/bc%i_glow\n"
			"{\n"
			"	cull none\n"
			"	nopicmip\n"
			"	sort %.2f\n"
			"	{\n"
			"		clampmap *vrsaberglow\n"
			"		blendFunc GL_ONE GL_ONE\n"
			"		rgbGen vertex\n"
			"		glow\n"
			"	}\n"
			"}\n"
			"gfx/vr/bc%i_core\n"
			"{\n"
			"	cull none\n"
			"	nopicmip\n"
			"	sort %.2f\n"
			"	{\n"
			"		clampmap *vrsaberedge\n"
			"		blendFunc GL_ONE GL_ONE\n"
			"		rgbGen vertex\n"
			"		glow\n"
			"	}\n"
			"	{\n"
			"		clampmap *vrsaberdark\n"
			"		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA\n"
			"		rgbGen identity\n"
			"		glow\n"
			"	}\n"
			"}\n",
			n, R_VR_BlackCoreSort( n, 0 ), n, R_VR_BlackCoreSort( n, 1 ) ) );
		Q_strcat( text, sizeof( text ), va(
			"gfx/vr/bc%i_depth\n"
			"{\n"
			"	cull none\n"
			"	nopicmip\n"
			"	sort %.2f\n"
			"	{\n"
			"		clampmap *vrsaberdark\n"
			"		blendFunc GL_ZERO GL_ONE\n"
			"		alphaFunc GE128\n"
			"		depthWrite\n"
			"		rgbGen identity\n"
			"	}\n"
			"}\n",
			n, R_VR_BlackCoreSort( n, 2 ) ) );
	}
	return text;
}
