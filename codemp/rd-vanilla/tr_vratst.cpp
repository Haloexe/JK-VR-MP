/*
===========================================================================
OpenJK VR (multiplayer) - the AT-ST cockpit, built into the renderer: its
models, the pictures on them and their shaders, there on every server - a
pure one (sv_pure 1) reads no loose files - with nothing to download, as
the sabers' are (tr_vrsaber.cpp). The cockpit's the AT-ST Cockpit Test
study's: its shapes (vr/atst/cockpit.json, made into the models by
vr/atst/make_cockpit.py: tr_vratst_models.h) and its pictures, drawn as its
page draws them (tr_vratst_art.cpp). Your game draws it round you as you
drive an AT-ST in VR (cg_vrwalker.c).

  models/vr/atst/cockpit.md3    the cabin, the skin round it, the visors
  models/vr/atst/yoke.md3       the pilot's yoke
  models/vr/atst/hand.md3       a gloved fist: your hands in it
  models/vr/atst/<material>     their shaders: lit as the world is where the
                                AT-ST stands, and by the cabin's own lights a
                                little; the lamps and screens lit up, the
                                viewports' glass faint, the gun sight on it
                                added to what's behind
They're asked for by name (RE_RegisterModel), and come from here first, as
a file of the same name would (RE_RegisterModels_GetDiskFile); the shaders'
text goes before the files', as the sabers' does.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "tr_local.h"
#include "../qcommon/vr_public.h"	// (vrCockpitScreens_t)
#include "tr_vratst_art.h"
#include "tr_vratst_models.h"
#include <zlib.h>

/*
================================================================================

The models

================================================================================
*/

static int R_VR_Base64Value( int c ) {
	if ( c >= 'A' && c <= 'Z' ) return c - 'A';
	if ( c >= 'a' && c <= 'z' ) return c - 'a' + 26;
	if ( c >= '0' && c <= '9' ) return c - '0' + 52;
	if ( c == '+' ) return 62;
	if ( c == '/' ) return 63;
	return -1;
}

// The bytes written out in base64 in these pieces, into out (max bytes at
// most): how many.
static int R_VR_Base64( const char *const *text, byte *out, int max ) {
	unsigned int bits = 0;
	int have = 0, n = 0;

	for ( ; *text; text++ ) {
		const char *s;
		for ( s = *text; *s; s++ ) {
			const int v = R_VR_Base64Value( (unsigned char)*s );
			if ( v < 0 ) {
				continue;	// ('=' at the end)
			}
			bits = ( bits << 6 ) | v;
			have += 6;
			if ( have >= 8 ) {
				have -= 8;
				if ( n >= max ) {
					return -1;
				}
				out[n++] = (byte)( bits >> have );
			}
		}
	}
	return n;
}

/*
=================
R_VR_AtstModel

The cockpit's model of this name, if it's one: its MD3 file's bytes in a
buffer as a file's are read (Z_Malloc'd: the model's kept in it, or it's
let go of with ri.FS_FreeFile) - qfalse if it isn't one (or can't be made:
said, and then it's as if there were no such file).
=================
*/
qboolean R_VR_AtstModel( const char *name, void **buffer ) {
	size_t i;

	for ( i = 0; i < ARRAY_LEN( vrAtstModels ); i++ ) {
		const vrAtstModel_t *m = &vrAtstModels[i];
		byte *packed, *file;
		uLongf size = (uLongf)m->size;

		if ( Q_stricmp( name, m->name ) ) {
			continue;
		}
		packed = (byte *)Z_Malloc( m->packed, TAG_TEMP_WORKSPACE, qfalse );
		file = (byte *)Z_Malloc( m->size, TAG_FILESYS, qfalse );
		if ( R_VR_Base64( m->text, packed, m->packed ) != m->packed ||
			uncompress( (Bytef *)file, &size, (const Bytef *)packed, (uLong)m->packed ) != Z_OK || size != (uLongf)m->size ) {
			ri.Printf( PRINT_WARNING, "R_VR_AtstModel: %s couldn't be made\n", m->name );
			Z_Free( packed );
			Z_Free( file );
			return qfalse;
		}
		Z_Free( packed );
		*buffer = file;
		return qtrue;
	}
	return qfalse;
}

/*
================================================================================

The pictures and the shaders

================================================================================
*/

#define VRT_PLATING		512		// the hull's and the skin's pictures' size (the page's)
#define VRT_CLOTH		256		// the floor's, the seats', the sight's
#define VRT_SOLID		8		// a colour's
#define VRT_SCREEN_W	512		// a screen's: half as tall (the small ones' half as wide too)
#define VRT_SCREEN_H	256

// The materials of one colour (the study's), lit
static const struct {
	const char	*name;
	unsigned	rgb;
} vrtSolids[] = {
	{ "frame",		0x4d5452 },
	{ "metal",		0x8b908e },
	{ "rubber",		0x232323 },
	{ "red",		0xa8261c },
	{ "red_dark",	0x6e1912 },
	{ "pipe",		0x6b726d },
	{ "lamp_off",	0x2a2d2c },
	{ "glove",		0x3b3531 },		// (your hands: hand.md3 - a dark leather)
};

// ...and the lamps, lit up
static const struct {
	const char	*name;
	unsigned	rgb;
} vrtLamps[] = {
	{ "lamp_amber",	0xffa53a },
	{ "lamp_green",	0x52f09a },
	{ "lamp_red",	0xff3b2b },
	{ "lamp_white",	0xfff4e0 },
};

static const char *vrtScreens[VRA_NUM_SCREENS] = { "hull", "weapons", "ground", "target", "comms", "heading" };

// The screens as they're drawn afresh (R_VR_AtstScreensFrame)
static struct {
	image_t		*image;			// its image (R_VR_CreateAtstImages)
	vraScreen_t	*art;			// its picture, kept (made as it's first drawn afresh)
	unsigned	key;			// what it shows (VRA_ScreenKey; 0: as it started)
	int			drawn;			// as of (ri.Milliseconds)
} vrtScreen[VRA_NUM_SCREENS];
static byte		*vrtScreenPic;	// (a picture drawn to be uploaded: R_VR_ReloadImage changes it)

static int R_VR_ScreenWidth( int which ) {
	return ( which == VRA_SCREEN_COMMS || which == VRA_SCREEN_HEADING ) ? VRT_SCREEN_W / 2 : VRT_SCREEN_W;
}

/*
=================
R_VR_CreateAtstImages

Among the renderer's own images at start (R_CreateBuiltinImages): the
cockpit's pictures, *vratst_<name>.
=================
*/
void R_VR_CreateAtstImages( void ) {
	byte *buf = (byte *)Z_Malloc( VRT_PLATING * VRT_PLATING * 4, TAG_TEMP_WORKSPACE, qfalse );
	size_t i;

	VRA_DrawPlating( buf, VRT_PLATING, VRA_PLATING_INSIDE );
	R_CreateImage( "*vratst_hull", buf, VRT_PLATING, VRT_PLATING, GL_RGBA, qtrue, qtrue, qfalse, GL_REPEAT );
	VRA_DrawPlating( buf, VRT_PLATING, VRA_PLATING_OUTSIDE );
	R_CreateImage( "*vratst_hullout", buf, VRT_PLATING, VRT_PLATING, GL_RGBA, qtrue, qtrue, qfalse, GL_REPEAT );
	VRA_DrawFloor( buf, VRT_CLOTH );
	R_CreateImage( "*vratst_floor", buf, VRT_CLOTH, VRT_CLOTH, GL_RGBA, qtrue, qtrue, qfalse, GL_REPEAT );
	VRA_DrawSeat( buf, VRT_CLOTH );
	R_CreateImage( "*vratst_seat", buf, VRT_CLOTH, VRT_CLOTH, GL_RGBA, qtrue, qtrue, qfalse, GL_REPEAT );
	VRA_DrawSight( buf, VRT_CLOTH );
	R_CreateImage( "*vratst_sight", buf, VRT_CLOTH, VRT_CLOTH, GL_RGBA, qtrue, qfalse, qfalse, GL_CLAMP );
	for ( i = 0; i < ARRAY_LEN( vrtSolids ); i++ ) {
		VRA_DrawSolid( buf, VRT_SOLID, vrtSolids[i].rgb );
		R_CreateImage( va( "*vratst_%s", vrtSolids[i].name ), buf, VRT_SOLID, VRT_SOLID, GL_RGBA, qfalse, qfalse, qfalse, GL_REPEAT );
	}
	for ( i = 0; i < VRA_NUM_SCREENS; i++ ) {
		const int w = R_VR_ScreenWidth( (int)i );
		VRA_DrawScreen( buf, w, VRT_SCREEN_H, (int)i );
		vrtScreen[i].image = R_CreateImage( va( "*vratst_screen_%s", vrtScreens[i] ), buf, w, VRT_SCREEN_H, GL_RGBA, qtrue, qfalse, qfalse,
			GL_CLAMP );
		vrtScreen[i].key = 0;	// (made afresh, or kept as it was - a map's loaded: drawn afresh as it's next seen)
		vrtScreen[i].drawn = 0;
	}
	Z_Free( buf );
}

/*
================================================================================

The screens as the cockpit goes

What the cgame says they show (vrRenderState_t.cockpitScreens: you sit in
the cockpit), each drawn afresh as that changes - as the frame begins (the
first eye's), one a frame at most, each no more than VRT_SCREEN_HZ times a
second - and uploaded over its image as it was made (its mipmaps made
again). What doesn't change on one is drawn once and kept, as it's first
drawn so (VRA_OpenScreen: one a frame, too).

================================================================================
*/

#define VRT_SCREEN_HZ	12

static float R_VR_ScreenFloat( float v, float lo, float hi ) {
	if ( !( v == v ) ) {
		return 0.0f;	// (NaN)
	}
	return v < lo ? lo : ( v > hi ? hi : v );
}

// What the cgame said, made safe to draw.
static void R_VR_ScreenState( const vrCockpitScreens_t *in, vraScreenState_t *st ) {
	memset( st, 0, sizeof( *st ) );
	st->t = ( in->time & 0x3fffffff ) * 0.001f;
	st->armour = in->armour;
	st->walking = in->walking ? 1 : 0;
	st->ready = in->ready ? 1 : 0;
	st->heat = in->heat;
	st->second = Com_Clampi( -1, 99999, in->second );
	st->secondMax = Com_Clampi( -1, 99999, in->secondMax );
	st->ground = R_VR_ScreenFloat( in->ground, -1000000.0f, 1000000.0f );
	st->stride = R_VR_ScreenFloat( in->stride, -1000.0f, 1000.0f );
	st->walk = R_VR_ScreenFloat( in->walk, 0.0f, 1.0f );
	Q_strncpyz( st->target, in->target, sizeof( st->target ) );
	st->range = in->range;
	st->bearing = R_VR_ScreenFloat( in->bearing, -1.0f, 1.0f );
	Q_strncpyz( st->comms[0], in->comms[0], sizeof( st->comms[0] ) );
	Q_strncpyz( st->comms[1], in->comms[1], sizeof( st->comms[1] ) );
	st->voice = R_VR_ScreenFloat( in->voice, 0.0f, 1.0f );
	st->heading = in->heading % 360;
	st->speed = in->speed;
}

/*
=================
R_VR_AtstScreensFrame

As a frame begins (RE_BeginFrame): in the cockpit, a screen drawn afresh
whose picture's changed (the one waiting longest), if one has.
=================
*/
void R_VR_AtstScreensFrame( void ) {
	const vrRenderState_t *vrs = ri.VR_GetRenderState ? ri.VR_GetRenderState() : NULL;
	vraScreenState_t st;
	unsigned key = 0;
	int i, pick = -1, now;

	if ( !vrs || !vrs->active || vrs->screenLayer || vrs->wristPass || vrs->eye != 0 || !vrs->cockpitScreens ) {
		return;
	}
	now = ri.Milliseconds();
	for ( i = 0; i < VRA_NUM_SCREENS; i++ ) {	// (what doesn't change on one, drawn: one a frame)
		if ( vrtScreen[i].image && !vrtScreen[i].art ) {
			vrtScreen[i].art = VRA_OpenScreen( R_VR_ScreenWidth( i ), VRT_SCREEN_H, i );
			if ( !vrtScreen[i].art ) {
				vrtScreen[i].image = NULL;	// (no room: it stays as it is)
				ri.Printf( PRINT_WARNING, "The AT-ST cockpit's %s screen can't be drawn: no room\n", vrtScreens[i] );
			}
			return;
		}
	}
	if ( !vrtScreenPic ) {
		vrtScreenPic = (byte *)malloc( VRT_SCREEN_W * VRT_SCREEN_H * 4 );
		if ( !vrtScreenPic ) {
			return;
		}
	}
	R_VR_ScreenState( vrs->cockpitScreens, &st );
	for ( i = 0; i < VRA_NUM_SCREENS; i++ ) {
		unsigned k;
		if ( !vrtScreen[i].art || ( vrtScreen[i].drawn && now - vrtScreen[i].drawn >= 0 && now - vrtScreen[i].drawn < 1000 / VRT_SCREEN_HZ ) ) {
			continue;
		}
		k = VRA_ScreenKey( vrtScreen[i].art, &st );
		if ( k == 0 ) {
			k = 1;	// (0's a screen as it started)
		}
		if ( k != vrtScreen[i].key && ( pick < 0 || now - vrtScreen[i].drawn > now - vrtScreen[pick].drawn ) ) {
			pick = i;
			key = k;
		}
	}
	if ( pick < 0 ) {
		return;
	}
	VRA_DrawScreenLive( vrtScreen[pick].art, &st, vrtScreenPic );
	R_IssuePendingRenderCommands();
	R_VR_ReloadImage( vrtScreen[pick].image, vrtScreenPic, R_VR_ScreenWidth( pick ), VRT_SCREEN_H );
	vrtScreen[pick].key = key;
	vrtScreen[pick].drawn = now ? now : 1;
}

/*
=================
R_VR_AtstShutdown

The renderer shut down: its window too (RE_Shutdown - a vid_restart, or
quitting), the screens' images with it - what's kept of them let go of.
=================
*/
void R_VR_AtstShutdown( qboolean destroyWindow ) {
	int i;

	if ( !destroyWindow ) {
		return;	// (a map's loaded: kept)
	}
	for ( i = 0; i < VRA_NUM_SCREENS; i++ ) {
		VRA_CloseScreen( vrtScreen[i].art );
		vrtScreen[i].art = NULL;
		vrtScreen[i].image = NULL;
		vrtScreen[i].key = 0;
		vrtScreen[i].drawn = 0;
	}
	free( vrtScreenPic );
	vrtScreenPic = NULL;
}

// How much the cabin's own lights light it, over what the world's do where it
// stands (a dim warm white: the dome lights, the screens) - and the red
// switches' own glow (the study's emissive), more.
#define VRT_CABIN_LIGHT		"0.24 0.23 0.21"
#define VRT_RED_GLOW		"0.40 0.25 0.23"

// A lit material: its picture as the world lights it where the AT-ST stands,
// and as the cabin's own lights do, a little.
static void R_VR_LitShader( char *text, int size, const char *name, const char *cabin ) {
	Q_strcat( text, size, va(
		"models/vr/atst/%s\n"
		"{\n"
		"	cull none\n"
		"	{\n"
		"		map *vratst_%s\n"
		"		rgbGen lightingDiffuse\n"
		"	}\n"
		"	{\n"
		"		map *vratst_%s\n"
		"		blendFunc GL_ONE GL_ONE\n"
		"		rgbGen const ( %s )\n"
		"	}\n"
		"}\n", name, name, name, cabin ) );
}

/*
=================
R_VR_AtstShaderText

The cockpit's shaders' text, before the shader files' as they're read
(ScanAndLoadShaderFiles), with the sabers'.
=================
*/
const char *R_VR_AtstShaderText( void ) {
	static char text[8192];
	size_t i;

	text[0] = '\0';
	R_VR_LitShader( text, sizeof( text ), "hull", VRT_CABIN_LIGHT );
	R_VR_LitShader( text, sizeof( text ), "hullout", VRT_CABIN_LIGHT );
	R_VR_LitShader( text, sizeof( text ), "floor", VRT_CABIN_LIGHT );
	R_VR_LitShader( text, sizeof( text ), "seat", VRT_CABIN_LIGHT );
	for ( i = 0; i < ARRAY_LEN( vrtSolids ); i++ ) {
		R_VR_LitShader( text, sizeof( text ), vrtSolids[i].name, !strcmp( vrtSolids[i].name, "red" ) ? VRT_RED_GLOW : VRT_CABIN_LIGHT );
	}
	for ( i = 0; i < ARRAY_LEN( vrtLamps ); i++ ) {
		const unsigned c = vrtLamps[i].rgb;
		Q_strcat( text, sizeof( text ), va(
			"models/vr/atst/%s\n"
			"{\n"
			"	cull none\n"
			"	{\n"
			"		map $whiteimage\n"
			"		rgbGen const ( %.3f %.3f %.3f )\n"
			"		glow\n"
			"	}\n"
			"}\n", vrtLamps[i].name, ( ( c >> 16 ) & 0xff ) / 255.0f, ( ( c >> 8 ) & 0xff ) / 255.0f, ( c & 0xff ) / 255.0f ) );
	}
	for ( i = 0; i < VRA_NUM_SCREENS; i++ ) {
		Q_strcat( text, sizeof( text ), va(
			"models/vr/atst/screen_%s\n"
			"{\n"
			"	cull none\n"
			"	nopicmip\n"
			"	{\n"
			"		clampmap *vratst_screen_%s\n"
			"		rgbGen identity\n"
			"	}\n"
			"}\n", vrtScreens[i], vrtScreens[i] ) );
	}
	Q_strcat( text, sizeof( text ),
		"models/vr/atst/glass\n"
		"{\n"
		"	cull none\n"
		"	{\n"
		"		map $whiteimage\n"
		"		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA\n"
		"		rgbGen const ( 0.659 0.769 0.800 )\n"
		"		alphaGen const 0.1\n"
		"	}\n"
		"}\n"
		"models/vr/atst/sight\n"
		"{\n"
		"	cull none\n"
		"	nopicmip\n"
		"	{\n"
		"		clampmap *vratst_sight\n"
		"		blendFunc GL_ONE GL_ONE\n"
		"		rgbGen identity\n"
		"	}\n"
		"}\n" );
	return text;
}
