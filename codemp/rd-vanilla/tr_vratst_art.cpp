/*
===========================================================================
OpenJK VR (multiplayer) - the AT-ST cockpit's pictures, drawn as the AT-ST
Cockpit Test study draws them on its page (its canvases: the same shapes,
colours and random numbers, so the same pictures), for the renderer to make
its images of at start (tr_vratst.cpp) - nothing read from a file.

A picture is drawn into a canvas of its own size (RGBA, 0..1, the colours
as written - not lightened or darkened for blending, as a page's canvas
blends them), each shape over what's there (source-over), its edges as
much as it covers of each pixel. Then it's made bytes for the image.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "tr_vratst_art.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*
================================================================================

The canvas

================================================================================
*/

typedef struct {
	float	r, g, b, a;
} vraColor_t;

typedef struct {
	int			w, h;
	float		*px;		// RGBA a pixel, top row first
	float		sx, sy;		// the drawing's units to the canvas's pixels (a screen's drawn as its page has it, at this size)
} vraCanvas_t;

static vraColor_t VRA_Rgba( int r, int g, int b, float a ) {
	vraColor_t c = { r / 255.0f, g / 255.0f, b / 255.0f, a };
	return c;
}

static vraColor_t VRA_Hex( unsigned hex ) {
	return VRA_Rgba( ( hex >> 16 ) & 0xff, ( hex >> 8 ) & 0xff, hex & 0xff, 1.0f );
}

static void VRA_Open( vraCanvas_t *c, int w, int h, float pageW, float pageH ) {
	c->w = w;
	c->h = h;
	c->px = (float *)calloc( (size_t)w * h * 4, sizeof( float ) );
	c->sx = w / pageW;
	c->sy = h / pageH;
}

// A pixel covered this much (0..1) by colour col, over what's there.
static void VRA_Blend( vraCanvas_t *c, int x, int y, vraColor_t col, float cover ) {
	float *p, sa, da, oa;

	if ( x < 0 || y < 0 || x >= c->w || y >= c->h || cover <= 0.0f ) {
		return;
	}
	sa = col.a * ( cover > 1.0f ? 1.0f : cover );
	if ( sa <= 0.0f ) {
		return;
	}
	p = c->px + ( (size_t)y * c->w + x ) * 4;
	da = p[3];
	oa = sa + da * ( 1.0f - sa );
	if ( oa <= 0.0f ) {
		return;
	}
	p[0] = ( col.r * sa + p[0] * da * ( 1.0f - sa ) ) / oa;
	p[1] = ( col.g * sa + p[1] * da * ( 1.0f - sa ) ) / oa;
	p[2] = ( col.b * sa + p[2] * da * ( 1.0f - sa ) ) / oa;
	p[3] = oa;
}

static float VRA_Overlap( float a0, float a1, float b0, float b1 ) {
	const float lo = a0 > b0 ? a0 : b0, hi = a1 < b1 ? a1 : b1;
	return hi > lo ? hi - lo : 0.0f;
}

// fillRect, in canvas pixels: each pixel as much as the rectangle covers of it.
static void VRA_RectPx( vraCanvas_t *c, float x, float y, float w, float h, vraColor_t col ) {
	int px, py;

	if ( w < 0.0f ) {
		x += w;
		w = -w;
	}
	if ( h < 0.0f ) {
		y += h;
		h = -h;
	}
	for ( py = (int)floorf( y ); py < (int)ceilf( y + h ); py++ ) {
		const float cy = VRA_Overlap( (float)py, py + 1.0f, y, y + h );
		for ( px = (int)floorf( x ); px < (int)ceilf( x + w ); px++ ) {
			VRA_Blend( c, px, py, col, cy * VRA_Overlap( (float)px, px + 1.0f, x, x + w ) );
		}
	}
}

// ...in the drawing's units.
static void VRA_Rect( vraCanvas_t *c, float x, float y, float w, float h, vraColor_t col ) {
	VRA_RectPx( c, x * c->sx, y * c->sy, w * c->sx, h * c->sy, col );
}

static void VRA_Fill( vraCanvas_t *c, vraColor_t col ) {
	VRA_RectPx( c, 0.0f, 0.0f, (float)c->w, (float)c->h, col );
}

#define VRA_SUB		4		// samples across a pixel, each way, for the edges of what isn't square to it

// A shape (inside: whether a point, in canvas pixels, is in it) over the
// pixels in a box, each as much as it covers of it.
typedef int ( *vraInside_t )( const void *shape, float x, float y );

static void VRA_Shape( vraCanvas_t *c, const void *shape, vraInside_t inside, float x0, float y0, float x1, float y1, vraColor_t col ) {
	int px, py, i, j;

	for ( py = (int)floorf( y0 ); py < (int)ceilf( y1 ); py++ ) {
		for ( px = (int)floorf( x0 ); px < (int)ceilf( x1 ); px++ ) {
			int n = 0;
			for ( j = 0; j < VRA_SUB; j++ ) {
				for ( i = 0; i < VRA_SUB; i++ ) {
					n += inside( shape, px + ( i + 0.5f ) / VRA_SUB, py + ( j + 0.5f ) / VRA_SUB ) ? 1 : 0;
				}
			}
			VRA_Blend( c, px, py, col, n / (float)( VRA_SUB * VRA_SUB ) );
		}
	}
}

typedef struct {
	float	x, y, r, w;		// the middle, the radius, the stroke's width (0: filled)
} vraCircle_t;

static int VRA_InCircle( const void *shape, float x, float y ) {
	const vraCircle_t *s = (const vraCircle_t *)shape;
	const float d = sqrtf( ( x - s->x ) * ( x - s->x ) + ( y - s->y ) * ( y - s->y ) );

	if ( s->w > 0.0f ) {
		return fabsf( d - s->r ) <= s->w * 0.5f;
	}
	return d <= s->r;
}

// arc + fill (w 0), or arc + stroke (w its lineWidth), in the drawing's units.
static void VRA_Circle( vraCanvas_t *c, float x, float y, float r, float w, vraColor_t col ) {
	vraCircle_t s;
	float reach;

	s.x = x * c->sx;
	s.y = y * c->sy;
	s.r = r * c->sx;
	s.w = w * c->sx;
	reach = s.r + s.w * 0.5f + 1.0f;
	VRA_Shape( c, &s, VRA_InCircle, s.x - reach, s.y - reach, s.x + reach, s.y + reach, col );
}

typedef struct {
	float	x0, y0, x1, y1, w;
} vraLine_t;

static int VRA_InLine( const void *shape, float x, float y ) {
	const vraLine_t *s = (const vraLine_t *)shape;
	const float dx = s->x1 - s->x0, dy = s->y1 - s->y0;
	const float len2 = dx * dx + dy * dy;
	float t, ex, ey;

	if ( len2 <= 0.0f ) {
		return 0;
	}
	t = ( ( x - s->x0 ) * dx + ( y - s->y0 ) * dy ) / len2;
	if ( t < 0.0f || t > 1.0f ) {
		return 0;	// (butt ends)
	}
	ex = s->x0 + t * dx - x;
	ey = s->y0 + t * dy - y;
	return ex * ex + ey * ey <= s->w * s->w * 0.25f;
}

// moveTo, lineTo, stroke, in the drawing's units.
static void VRA_Line( vraCanvas_t *c, float x0, float y0, float x1, float y1, float w, vraColor_t col ) {
	vraLine_t s;
	float r;

	s.x0 = x0 * c->sx;
	s.y0 = y0 * c->sy;
	s.x1 = x1 * c->sx;
	s.y1 = y1 * c->sy;
	s.w = w * c->sx;
	r = s.w * 0.5f + 1.0f;
	VRA_Shape( c, &s, VRA_InLine, ( s.x0 < s.x1 ? s.x0 : s.x1 ) - r, ( s.y0 < s.y1 ? s.y0 : s.y1 ) - r,
		( s.x0 > s.x1 ? s.x0 : s.x1 ) + r, ( s.y0 > s.y1 ? s.y0 : s.y1 ) + r, col );
}

typedef struct {
	float	x[4], y[4];		// corners, one way round
} vraQuad_t;

static int VRA_InQuad( const void *shape, float x, float y ) {
	const vraQuad_t *s = (const vraQuad_t *)shape;
	int i, pos = 0, neg = 0;

	for ( i = 0; i < 4; i++ ) {
		const int j = ( i + 1 ) & 3;
		const float e = ( s->x[j] - s->x[i] ) * ( y - s->y[i] ) - ( s->y[j] - s->y[i] ) * ( x - s->x[i] );
		if ( e > 0.0f ) {
			pos++;
		} else if ( e < 0.0f ) {
			neg++;
		}
	}
	return !( pos && neg );
}

// save, translate( tx, ty ), rotate( ang ), fillRect( x, y, w, h ), restore -
// in canvas pixels.
static void VRA_TurnedRectPx( vraCanvas_t *c, float tx, float ty, float ang, float x, float y, float w, float h, vraColor_t col ) {
	const float cs = cosf( ang ), sn = sinf( ang );
	const float lx[4] = { x, x + w, x + w, x }, ly[4] = { y, y, y + h, y + h };
	float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
	vraQuad_t s;
	int i;

	for ( i = 0; i < 4; i++ ) {
		s.x[i] = tx + lx[i] * cs - ly[i] * sn;
		s.y[i] = ty + lx[i] * sn + ly[i] * cs;
		x0 = s.x[i] < x0 ? s.x[i] : x0;
		y0 = s.y[i] < y0 ? s.y[i] : y0;
		x1 = s.x[i] > x1 ? s.x[i] : x1;
		y1 = s.y[i] > y1 ? s.y[i] : y1;
	}
	VRA_Shape( c, &s, VRA_InQuad, x0, y0, x1, y1, col );
}

// createRadialGradient( x, y, 0, x, y, r ) from col to clear (each part of the
// colour on its own - alpha not multiplied in - as a page's are), filling the
// square round it (fillRect( x - r, y - r, 2r, 2r )): canvas pixels.
static void VRA_GlowPx( vraCanvas_t *c, float x, float y, float r, vraColor_t col ) {
	int px, py;

	for ( py = (int)floorf( y - r ); py < (int)ceilf( y + r ); py++ ) {
		const float cy = VRA_Overlap( (float)py, py + 1.0f, y - r, y + r );
		for ( px = (int)floorf( x - r ); px < (int)ceilf( x + r ); px++ ) {
			const float dx = px + 0.5f - x, dy = py + 0.5f - y;
			const float t = sqrtf( dx * dx + dy * dy ) / r;
			vraColor_t g;
			if ( t >= 1.0f ) {
				continue;
			}
			g.r = col.r * ( 1.0f - t );
			g.g = col.g * ( 1.0f - t );
			g.b = col.b * ( 1.0f - t );
			g.a = col.a * ( 1.0f - t );
			VRA_Blend( c, px, py, g, cy * VRA_Overlap( (float)px, px + 1.0f, x - r, x + r ) );
		}
	}
}

// The canvas as the image's bytes (RGBA)...
static void VRA_ToBytes( const vraCanvas_t *c, unsigned char *out ) {
	size_t i, n = (size_t)c->w * c->h * 4;

	for ( i = 0; i < n; i++ ) {
		float v = c->px[i];
		v = v < 0.0f ? 0.0f : ( v > 1.0f ? 1.0f : v );
		out[i] = (unsigned char)( v * 255.0f + 0.5f );
	}
}

// ...and let go of.
static void VRA_Close( vraCanvas_t *c, unsigned char *out ) {
	VRA_ToBytes( c, out );
	free( c->px );
	c->px = NULL;
}

/*
================================================================================

The page's random numbers: rng( seed ), the same numbers in the same order

================================================================================
*/

typedef struct {
	unsigned int	s;
} vraRng_t;

static double VRA_Next( vraRng_t *r ) {
	r->s = r->s * 1664525u + 1013904223u;
	return r->s / 4294967296.0;
}

static float VRA_R( vraRng_t *r ) {
	return (float)VRA_Next( r );
}

// speckle( g, size, r, n, light, dark )
static void VRA_Speckle( vraCanvas_t *c, int size, vraRng_t *r, int n, vraColor_t light, vraColor_t dark ) {
	int i;

	for ( i = 0; i < n; i++ ) {
		const vraColor_t col = VRA_R( r ) < 0.5f ? light : dark;
		const float x = VRA_R( r ) * size, y = VRA_R( r ) * size;
		const float w = 1.0f + VRA_R( r ) * 2.0f, h = 1.0f + VRA_R( r ) * 2.0f;
		VRA_Rect( c, x, y, w, h, col );
	}
}

/*
================================================================================

The pictures

================================================================================
*/

// plating( base, seam, seamLight, seed ): the hull's panels.
void VRA_DrawPlating( unsigned char *out, int size, int which ) {
	const int page = 512;	// (drawn as the page's 512 square, at any size)
	vraCanvas_t c;
	vraRng_t r;
	vraColor_t base, seam, seamLight;
	int k, row, col, s2;
	float step, q;

	if ( which == VRA_PLATING_OUTSIDE ) {	// hullOutTex
		base = VRA_Hex( 0xa9ada4 );
		seam = VRA_Rgba( 20, 22, 18, 0.5f );
		seamLight = VRA_Rgba( 255, 255, 255, 0.12f );
		r.s = 21;
	} else {								// hullTex
		base = VRA_Hex( 0x959a91 );
		seam = VRA_Rgba( 10, 12, 9, 0.55f );
		seamLight = VRA_Rgba( 255, 255, 255, 0.10f );
		r.s = 7;
	}
	VRA_Open( &c, size, size, (float)page, (float)page );
	VRA_Fill( &c, base );
	VRA_Speckle( &c, page, &r, 9000, VRA_Rgba( 255, 255, 255, 0.035f ), VRA_Rgba( 0, 0, 0, 0.05f ) );
	for ( k = 0; k < 18; k++ ) {	// wear and grime
		const float x = VRA_R( &r ) * page, y = VRA_R( &r ) * page, rad = 30.0f + VRA_R( &r ) * 120.0f;
		const vraColor_t g = VRA_R( &r ) < 0.6f ? VRA_Rgba( 20, 18, 12, 0.16f ) : VRA_Rgba( 255, 250, 235, 0.06f );
		VRA_GlowPx( &c, x * c.sx, y * c.sy, rad * c.sx, g );
	}
	step = page / 2.0f;
	for ( row = 0; row < 2; row++ ) {	// panel seams, staggered
		const float y0 = row * step;
		const float off = row ? step / 2.0f : 0.0f;
		VRA_Rect( &c, 0.0f, y0, (float)page, 2.0f, seam );
		VRA_Rect( &c, 0.0f, y0 + 2.0f, (float)page, 1.0f, seamLight );
		for ( col = 0; col < 2; col++ ) {
			const float x0 = fmodf( col * step + off, (float)page );
			VRA_Rect( &c, x0, y0, 2.0f, step, seam );
			VRA_Rect( &c, x0 + 2.0f, y0, 1.0f, step, seamLight );
			for ( q = 12.0f; q < step; q += 22.0f ) {	// rivets along the seams
				VRA_Circle( &c, x0 + 7.0f, y0 + q, 2.2f, 0.0f, VRA_Rgba( 0, 0, 0, 0.28f ) );
				VRA_Circle( &c, x0 + 6.4f, y0 + q - 0.6f, 1.1f, 0.0f, VRA_Rgba( 255, 255, 255, 0.18f ) );
			}
		}
	}
	for ( s2 = 0; s2 < 40; s2++ ) {	// scratches
		const float a = 0.04f + VRA_R( &r ) * 0.06f;
		const float sx = VRA_R( &r ) * page, sy = VRA_R( &r ) * page, ang = VRA_R( &r ) * (float)M_PI, len = 6.0f + VRA_R( &r ) * 26.0f;
		VRA_Line( &c, sx, sy, sx + cosf( ang ) * len, sy + sinf( ang ) * len, 1.0f, VRA_Rgba( 255, 255, 255, a ) );
	}
	VRA_Close( &c, out );
}

// floorTex: tread plate.
void VRA_DrawFloor( unsigned char *out, int size ) {
	const int page = 256;
	vraCanvas_t c;
	vraRng_t r;
	int x, y;

	r.s = 3;
	VRA_Open( &c, size, size, (float)page, (float)page );
	VRA_Fill( &c, VRA_Hex( 0x5e6461 ) );
	for ( y = 0; y < page; y += 16 ) {
		for ( x = 0; x < page; x += 16 ) {
			const float tx = ( x + ( ( y / 16 ) % 2 ? 8 : 0 ) + 4 ) * c.sx, ty = ( y + 8 ) * c.sy;
			const float ang = ( ( x + y ) / 16 ) % 2 ? 0.7f : -0.7f;
			VRA_TurnedRectPx( &c, tx, ty, ang, -5.0f * c.sx, -1.2f * c.sx, 10.0f * c.sx, 2.4f * c.sx, VRA_Rgba( 255, 255, 255, 0.12f ) );
			VRA_TurnedRectPx( &c, tx, ty, ang, -5.0f * c.sx, 1.2f * c.sx, 10.0f * c.sx, 1.0f * c.sx, VRA_Rgba( 0, 0, 0, 0.35f ) );
		}
	}
	VRA_Speckle( &c, page, &r, 3000, VRA_Rgba( 255, 255, 255, 0.04f ), VRA_Rgba( 0, 0, 0, 0.08f ) );
	VRA_Close( &c, out );
}

// seatTex: the seats' cloth, stitched round the edge.
void VRA_DrawSeat( unsigned char *out, int size ) {
	const int page = 256;
	vraCanvas_t c;
	vraRng_t r;
	int y;
	float d;

	r.s = 11;
	VRA_Open( &c, size, size, (float)page, (float)page );
	VRA_Fill( &c, VRA_Hex( 0x5b5047 ) );
	for ( y = 0; y < page; y += 2 ) {
		VRA_Rect( &c, 0.0f, (float)y, (float)page, 1.0f, ( y % 4 ) ? VRA_Rgba( 255, 255, 255, 0.025f ) : VRA_Rgba( 0, 0, 0, 0.06f ) );
	}
	VRA_Speckle( &c, page, &r, 2500, VRA_Rgba( 255, 240, 220, 0.04f ), VRA_Rgba( 0, 0, 0, 0.08f ) );
	// strokeRect( 10.5, 10.5, size - 21, size - 21 ), dashed 3 on, 3 off, round
	// from its top left corner: a pixel wide, through the pixels at 10 and 245
	for ( d = 0.0f; d < 4.0f * ( page - 21 ); d += 6.0f ) {
		float on;
		for ( on = d; on < d + 3.0f && on < 4.0f * ( page - 21 ); on += 1.0f ) {
			const float side = (float)( page - 21 );
			const int edge = (int)( on / side );
			const float along = on - edge * side;
			float px, py;
			switch ( edge ) {
			case 0:	px = 10.0f + along;			py = 10.0f;				break;
			case 1:	px = 10.0f + side;			py = 10.0f + along;		break;
			case 2:	px = 10.0f + side - along;	py = 10.0f + side;		break;
			default: px = 10.0f;				py = 10.0f + side - along;	break;
			}
			VRA_Rect( &c, px, py, 1.0f, 1.0f, VRA_Rgba( 0, 0, 0, 0.45f ) );
		}
	}
	VRA_Close( &c, out );
}

// A blur of a mask (alpha, canvas pixels) by a Gaussian of this sigma: a
// page's shadowBlur ( blur / 2 ).
static void VRA_BlurMask( float *mask, int w, int h, float sigma ) {
	const int rad = (int)ceilf( sigma * 3.0f );
	float *k = (float *)malloc( sizeof( float ) * ( 2 * rad + 1 ) );
	float *tmp = (float *)calloc( (size_t)w * h, sizeof( float ) );
	float sum = 0.0f;
	int x, y, i;

	for ( i = -rad; i <= rad; i++ ) {
		k[i + rad] = expf( -( i * i ) / ( 2.0f * sigma * sigma ) );
		sum += k[i + rad];
	}
	for ( i = 0; i <= 2 * rad; i++ ) {
		k[i] /= sum;
	}
	for ( y = 0; y < h; y++ ) {
		for ( x = 0; x < w; x++ ) {
			float v = 0.0f;
			for ( i = -rad; i <= rad; i++ ) {
				const int xx = x + i;
				if ( xx >= 0 && xx < w ) {
					v += mask[y * w + xx] * k[i + rad];
				}
			}
			tmp[y * w + x] = v;
		}
	}
	for ( y = 0; y < h; y++ ) {
		for ( x = 0; x < w; x++ ) {
			float v = 0.0f;
			for ( i = -rad; i <= rad; i++ ) {
				const int yy = y + i;
				if ( yy >= 0 && yy < h ) {
					v += tmp[yy * w + x] * k[i + rad];
				}
			}
			mask[y * w + x] = v;
		}
	}
	free( tmp );
	free( k );
}

// sightTex: the gun sight on your viewport's glass - an amber ring, its ticks
// and a dot, glowing (shadowBlur 10). Its colour goes on with its alpha
// already in it: it's added to the view (blendFunc GL_ONE GL_ONE).
void VRA_DrawSight( unsigned char *out, int size ) {
	static const float ticks[4][4] = { { 128, 20, 128, 70 }, { 128, 186, 128, 236 }, { 20, 128, 70, 128 }, { 186, 128, 236, 128 } };
	const vraColor_t amber = VRA_Hex( 0xffb44a );
	vraCanvas_t c;
	float *mask, *glow;
	size_t i, n;
	int t;

	VRA_Open( &c, size, size, 256.0f, 256.0f );
	// (the shapes alone, white: their mask)
	VRA_Circle( &c, 128.0f, 128.0f, 78.0f, 4.0f, VRA_Hex( 0xffffff ) );
	for ( t = 0; t < 4; t++ ) {
		VRA_Line( &c, ticks[t][0], ticks[t][1], ticks[t][2], ticks[t][3], 3.0f, VRA_Hex( 0xffffff ) );
	}
	VRA_Circle( &c, 128.0f, 128.0f, 4.0f, 0.0f, VRA_Hex( 0xffffff ) );
	n = (size_t)size * size;
	mask = (float *)malloc( n * sizeof( float ) );
	glow = (float *)malloc( n * sizeof( float ) );
	for ( i = 0; i < n; i++ ) {
		mask[i] = glow[i] = c.px[i * 4 + 3];
	}
	VRA_BlurMask( glow, size, size, 5.0f * c.sx );
	// the glow under the shapes, both amber
	for ( i = 0; i < n; i++ ) {
		const float a = mask[i] + glow[i] * ( 1.0f - mask[i] );
		c.px[i * 4 + 0] = amber.r * a;
		c.px[i * 4 + 1] = amber.g * a;
		c.px[i * 4 + 2] = amber.b * a;
		c.px[i * 4 + 3] = a;
	}
	free( mask );
	free( glow );
	VRA_Close( &c, out );
}

// A material of one colour (frame, metal, rubber...): an image of it.
void VRA_DrawSolid( unsigned char *out, int size, unsigned rgb ) {
	int i;

	for ( i = 0; i < size * size; i++ ) {
		out[i * 4 + 0] = ( rgb >> 16 ) & 0xff;
		out[i * 4 + 1] = ( rgb >> 8 ) & 0xff;
		out[i * 4 + 2] = rgb & 0xff;
		out[i * 4 + 3] = 255;
	}
}

/*
================================================================================

The screens: as the page draws them, its state at the start - standing,
nothing in the sight (VRA_DrawScreen) - and as the cockpit goes (what it's
told: VRA_DrawScreenLive). What on a screen doesn't change is drawn once and
kept (VRA_OpenScreen); what does, over it, each time.

Their writing in a small square-dotted face of its own, five dots across and
seven down, as tall as the page's letters stand at that size (its capitals,
0.73 of the size) and as far apart (0.6 of it): capitals (small letters
written as them), figures and some marks - no more (a gap where another is).

================================================================================
*/

static const struct {
	char			ch;
	unsigned char	rows[7];	// top first, the left dot the highest bit (of 5)
} vraFont[] = {
	{ 'A', { 0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 } },
	{ 'B', { 0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e } },
	{ 'C', { 0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e } },
	{ 'D', { 0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e } },
	{ 'E', { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f } },
	{ 'F', { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10 } },
	{ 'G', { 0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f } },
	{ 'H', { 0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 } },
	{ 'I', { 0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e } },
	{ 'J', { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0c } },
	{ 'K', { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 } },
	{ 'L', { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f } },
	{ 'M', { 0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11 } },
	{ 'N', { 0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11 } },
	{ 'O', { 0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e } },
	{ 'P', { 0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10 } },
	{ 'Q', { 0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d } },
	{ 'R', { 0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11 } },
	{ 'S', { 0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e } },
	{ 'T', { 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 } },
	{ 'U', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e } },
	{ 'V', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04 } },
	{ 'W', { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a } },
	{ 'X', { 0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11 } },
	{ 'Y', { 0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04 } },
	{ 'Z', { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f } },
	{ '0', { 0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e } },
	{ '1', { 0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e } },
	{ '2', { 0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f } },
	{ '3', { 0x1f, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0e } },
	{ '4', { 0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02 } },
	{ '5', { 0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e } },
	{ '6', { 0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e } },
	{ '7', { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 } },
	{ '8', { 0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e } },
	{ '9', { 0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c } },
	{ '%', { 0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03 } },
	{ '-', { 0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00 } },
	{ '/', { 0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x00 } },
	{ '.', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c } },
	{ ',', { 0x00, 0x00, 0x00, 0x00, 0x0c, 0x04, 0x08 } },
	{ ':', { 0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x0c, 0x00 } },
	{ ';', { 0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x04, 0x08 } },
	{ '!', { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 } },
	{ '?', { 0x0e, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04 } },
	{ '\'', { 0x0c, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00 } },
	{ '"', { 0x0a, 0x0a, 0x0a, 0x00, 0x00, 0x00, 0x00 } },
	{ '(', { 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02 } },
	{ ')', { 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08 } },
	{ '[', { 0x0e, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0e } },
	{ ']', { 0x0e, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0e } },
	{ '<', { 0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02 } },
	{ '>', { 0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08 } },
	{ '+', { 0x00, 0x04, 0x04, 0x1f, 0x04, 0x04, 0x00 } },
	{ '=', { 0x00, 0x00, 0x1f, 0x00, 0x1f, 0x00, 0x00 } },
	{ '_', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f } },
	{ '*', { 0x00, 0x04, 0x15, 0x0e, 0x15, 0x04, 0x00 } },
	{ '#', { 0x0a, 0x0a, 0x1f, 0x0a, 0x1f, 0x0a, 0x0a } },
	{ '&', { 0x0c, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0d } },
	{ '@', { 0x0e, 0x11, 0x01, 0x0d, 0x15, 0x15, 0x0e } },
	{ '$', { 0x04, 0x0f, 0x14, 0x0e, 0x05, 0x1e, 0x04 } },
};

#define VRA_GLYPHS	( (int)( sizeof( vraFont ) / sizeof( vraFont[0] ) ) )

// A character's glyph (-1: the face hasn't it - a space, say).
static int VRA_Glyph( int ch ) {
	int f;

	if ( ch >= 'a' && ch <= 'z' ) {
		ch -= 'a' - 'A';
	}
	for ( f = 0; f < VRA_GLYPHS; f++ ) {
		if ( vraFont[f].ch == ch ) {
			return f;
		}
	}
	return -1;
}

// fillText( text, x, y ) at a size (px), the baseline at y: the drawing's units
// - no more than max characters of it (less than 0: all). Each dot a little
// wider than it's tall (VRA_TEXT_BOLD), running into the next: the page's
// letters are semi-bold.
#define VRA_TEXT_BOLD	1.35f
static void VRA_TextN( vraCanvas_t *c, const char *text, int max, float x, float y, float sizePx, vraColor_t col ) {
	const float dot = sizePx * 0.73f / 7.0f;	// (a capital's height in seven dots)
	const float advance = sizePx * 0.6f;
	int i, row, bit;

	for ( i = 0; text[i] && ( max < 0 || i < max ); i++, x += advance ) {
		const int f = VRA_Glyph( (unsigned char)text[i] );
		if ( f < 0 ) {
			continue;
		}
		for ( row = 0; row < 7; row++ ) {
			for ( bit = 0; bit < 5; bit++ ) {
				// (a run of dots in a row as one rectangle: no seams where they overlap)
				if ( ( vraFont[f].rows[row] & ( 0x10 >> bit ) ) && ( bit == 0 || !( vraFont[f].rows[row] & ( 0x20 >> bit ) ) ) ) {
					int end = bit;
					while ( end < 4 && ( vraFont[f].rows[row] & ( 0x10 >> ( end + 1 ) ) ) ) {
						end++;
					}
					VRA_Rect( c, x + ( advance - ( 4.0f + VRA_TEXT_BOLD ) * dot ) * 0.5f + bit * dot, y - ( 7 - row ) * dot,
						( end - bit + VRA_TEXT_BOLD ) * dot, dot, col );
				}
			}
		}
	}
}

static void VRA_Text( vraCanvas_t *c, const char *text, float x, float y, float sizePx, vraColor_t col ) {
	VRA_TextN( c, text, -1, x, y, sizePx, col );
}

// How many characters fit across a width at a size.
static int VRA_TextFits( float width, float sizePx ) {
	return (int)floorf( width / ( sizePx * 0.6f ) + 0.001f );
}

// frame( g, w, h, ink ): the screen's dark glass and the rule round it.
static void VRA_ScreenFrame( vraCanvas_t *c, float w, float h, vraColor_t ink ) {
	const float lw = 2.0f;

	VRA_Fill( c, VRA_Hex( 0x060805 ) );
	ink.a *= 0.35f;
	VRA_Rect( c, 8.0f - lw / 2, 8.0f - lw / 2, w - 16.0f + lw, lw, ink );	// strokeRect( 8, 8, w - 16, h - 16 )
	VRA_Rect( c, 8.0f - lw / 2, h - 8.0f - lw / 2, w - 16.0f + lw, lw, ink );
	VRA_Rect( c, 8.0f - lw / 2, 8.0f + lw / 2, lw, h - 16.0f - lw, ink );
	VRA_Rect( c, w - 8.0f - lw / 2, 8.0f + lw / 2, lw, h - 16.0f - lw, ink );
}

// The scanlines over the canvas's rows r0..r1 (not r1) - the page's: across it,
// a unit high, every third, VRA_Rect( c, 0, y, w, 1, rgba( 0, 0, 0, 0.22 ) ),
// last of all - and those rows made bytes (as VRA_ToBytes makes them), in one
// go: the canvas as it was, not drawn on.
#define VRA_SCANLINE_A	0.22f
static void VRA_ScanlinesToBytes( const vraCanvas_t *c, float pw, float ph, unsigned char *out, int r0, int r1 ) {
	const float xw = 0.0f + pw * c->sx;	// (the lines' right end, in the canvas's pixels: as VRA_Rect has it)
	const int cols = (int)ceilf( xw ) < c->w ? (int)ceilf( xw ) : c->w;
	int r, k, x, i;

	r0 = r0 < 0 ? 0 : r0;
	r1 = r1 > c->h ? c->h : r1;
	for ( r = r0; r < r1; r++ ) {
		const float *p = c->px + (size_t)r * c->w * 4;
		unsigned char *o = out + (size_t)r * c->w * 4;
		float cy[4];
		int n = 0;
		// the lines over this row (one, as it is, at most: they're further apart than a pixel's height)
		for ( k = (int)floorf( ( r / c->sy - 1.0f ) / 3.0f ) - 1; n < 4; k++ ) {
			float y, y0, y1;
			if ( k < 0 ) {
				continue;
			}
			y = 3.0f * k;
			if ( !( y < ph ) ) {
				break;
			}
			y0 = y * c->sy;
			y1 = y0 + 1.0f * c->sy;
			if ( y0 >= r + 1.0f ) {
				break;
			}
			if ( r >= (int)floorf( y0 ) && r < (int)ceilf( y1 ) ) {
				const float cover = VRA_Overlap( (float)r, r + 1.0f, y0, y1 );
				if ( cover > 0.0f ) {
					cy[n++] = cover;
				}
			}
		}
		if ( !n ) {	// (none: the row as it is)
			for ( i = 0; i < c->w * 4; i++ ) {
				const float b = p[i] < 0.0f ? 0.0f : ( p[i] > 1.0f ? 1.0f : p[i] );
				o[i] = (unsigned char)( b * 255.0f + 0.5f );
			}
			continue;
		}
		// (one line over an opaque pixel all across it - near enough all of
		// them - as below comes to its colour times 1 - sa, alpha 1: so, quicker)
		const float sa1 = VRA_SCANLINE_A * ( cy[0] > 1.0f ? 1.0f : cy[0] ), k1 = 1.0f - sa1;
		const int quick = ( n == 1 && sa1 > 0.0f && sa1 + 1.0f * ( 1.0f - sa1 ) == 1.0f );
		for ( x = 0; x < c->w; x++, p += 4, o += 4 ) {
			float v[4] = { p[0], p[1], p[2], p[3] };
			if ( quick && p[3] == 1.0f && x < cols && VRA_Overlap( (float)x, x + 1.0f, 0.0f, xw ) == 1.0f ) {
				for ( i = 0; i < 3; i++ ) {
					const float b = v[i] * k1;
					o[i] = (unsigned char)( ( b < 0.0f ? 0.0f : ( b > 1.0f ? 1.0f : b ) ) * 255.0f + 0.5f );
				}
				o[3] = 255;
				continue;
			}
			if ( x < cols ) {
				const float cx = VRA_Overlap( (float)x, x + 1.0f, 0.0f, xw );
				for ( i = 0; i < n; i++ ) {	// (as VRA_Blend has black over it)
					const float cover = cy[i] * cx;
					float sa, da, oa;
					if ( cover <= 0.0f ) {
						continue;
					}
					sa = VRA_SCANLINE_A * ( cover > 1.0f ? 1.0f : cover );
					if ( sa <= 0.0f ) {
						continue;
					}
					da = v[3];
					oa = sa + da * ( 1.0f - sa );
					if ( oa <= 0.0f ) {
						continue;
					}
					v[0] = ( 0.0f * sa + v[0] * da * ( 1.0f - sa ) ) / oa;
					v[1] = ( 0.0f * sa + v[1] * da * ( 1.0f - sa ) ) / oa;
					v[2] = ( 0.0f * sa + v[2] * da * ( 1.0f - sa ) ) / oa;
					v[3] = oa;
				}
			}
			for ( i = 0; i < 4; i++ ) {
				const float b = v[i] < 0.0f ? 0.0f : ( v[i] > 1.0f ? 1.0f : v[i] );
				o[i] = (unsigned char)( b * 255.0f + 0.5f );
			}
		}
	}
}

// bars( g, x, y, w, n, filled, ink )
static void VRA_Bars( vraCanvas_t *c, float x, float y, float w, int n, int filled, vraColor_t ink ) {
	const float cw = w / n;
	int i;

	for ( i = 0; i < n; i++ ) {
		VRA_Rect( c, x + i * cw + 2.0f, y, cw - 4.0f, 16.0f, i < filled ? ink : VRA_Rgba( 255, 255, 255, 0.08f ) );
	}
}

// strokeRect( x, y, w, h ) a lineWidth wide
static void VRA_StrokeRect( vraCanvas_t *c, float x, float y, float w, float h, float lw, vraColor_t ink ) {
	VRA_Rect( c, x - lw / 2, y - lw / 2, w + lw, lw, ink );
	VRA_Rect( c, x - lw / 2, y + h - lw / 2, w + lw, lw, ink );
	VRA_Rect( c, x - lw / 2, y + lw / 2, lw, h - lw, ink );
	VRA_Rect( c, x + w - lw / 2, y + lw / 2, lw, h - lw, ink );
}

#define VRA_AMBER		0xffb44a
#define VRA_GREEN		0x63f2a1
#define VRA_RED			0xff5a43

#define VRA_GRID		40.0f	// the ground's squares (a metre)
#define VRA_GROUND_TOP	64.0f	// its box, from here down to 18 above the screen's foot (20 in from its sides)
#define VRA_TAPE		1.4f	// the heading's tape: so far a degree
#define VRA_CHAT_SIZE	14.0f	// what's said
#define VRA_WAVE_REST	2.0f	// the voice's wave this high with no one talking (as high again as loud as can be)

// The page a screen's drawn as (its units: the drawing's).
static void VRA_ScreenPage( int which, float *pw, float *ph ) {
	*pw = 512.0f;
	*ph = 256.0f;
	switch ( which ) {
	case VRA_SCREEN_HULL:		*pw = 512.0f; *ph = 300.0f; break;
	case VRA_SCREEN_WEAPONS:	*pw = 512.0f; *ph = 260.0f; break;
	case VRA_SCREEN_GROUND:		*pw = 512.0f; *ph = 260.0f; break;
	case VRA_SCREEN_TARGET:		*pw = 512.0f; *ph = 288.0f; break;
	case VRA_SCREEN_COMMS:		*pw = 320.0f; *ph = 256.0f; break;
	case VRA_SCREEN_HEADING:	*pw = 320.0f; *ph = 256.0f; break;
	}
}

void VRA_ScreenStart( vraScreenState_t *st ) {
	memset( st, 0, sizeof( *st ) );
	st->armour = 100;
	st->ready = 1;
	st->heat = 3;			// (1 + 3 * (0.5 + 0.5 sin 0), rounded)
	st->second = 6;
	st->secondMax = VRA_SECOND_BARS;
	st->voice = 1.0f;		// (its wave as high as the page's)
}

static int VRA_Clampi( int v, int lo, int hi ) {
	return v < lo ? lo : ( v > hi ? hi : v );
}

static float VRA_Clampf( float v, float lo, float hi ) {
	return v < lo ? lo : ( v > hi ? hi : v );
}

// (The live parts, worked out from a state: what's drawn, and what tells
// whether it's changed.)

static int VRA_LegBars( const vraScreenState_t *st ) {
	return ( VRA_Clampi( st->armour, 0, 100 ) + 19 ) / 20;
}

static int VRA_SecondBars( const vraScreenState_t *st ) {
	const int second = VRA_Clampi( st->second, 0, 99999 ), most = VRA_Clampi( st->secondMax, 0, 99999 );

	if ( most <= 0 || second <= 0 ) {
		return 0;
	}
	return VRA_Clampi( ( second * VRA_SECOND_BARS + most - 1 ) / most, 0, VRA_SECOND_BARS );
}

// The ground's lines across, from the top of its box: where the first is
// (0..VRA_GRID) - the ground gone by under it.
static float VRA_GroundOffset( const vraScreenState_t *st ) {
	float off = fmodf( st->ground * VRA_GRID, VRA_GRID );

	if ( !( off >= 0.0f ) ) {	// (and NaN: none)
		off = ( off < 0.0f ) ? off + VRA_GRID : 0.0f;
	}
	return off >= VRA_GRID ? 0.0f : off;
}

// A foot's swing (0: the left, 1: the right): more than 0, up off the ground.
static float VRA_Swing( const vraScreenState_t *st, int foot ) {
	return sinf( ( st->stride + foot * 0.5f ) * (float)M_PI * 2.0f ) * VRA_Clampf( st->walk, 0.0f, 1.0f );
}

// Where the target's box is across the screen: on its bearing, or sweeping
// side to side for one.
static float VRA_TargetX( const vraScreenState_t *st, float pw ) {
	if ( st->target[0] ) {
		return pw / 2.0f + VRA_Clampf( st->bearing, -1.0f, 1.0f ) * ( pw / 2.0f - 46.0f );
	}
	return 46.0f + ( pw - 92.0f ) * ( 0.5f + 0.5f * sinf( st->t * 0.9f ) );
}

// A message as lines (at most max, as many characters as fit in each:
// broken where it has a space, the last cut short - ".." - if there's more).
static int VRA_Wrap( const char *text, int per, int max, char lines[2][VRA_LINE] ) {
	int n = 0;

	if ( per > VRA_LINE - 1 ) {
		per = VRA_LINE - 1;
	}
	while ( *text == ' ' ) {
		text++;
	}
	while ( *text && n < max && per > 2 ) {
		const int len = (int)strlen( text );
		const int last = ( n == max - 1 && len > per );
		const int room = last ? per - 2 : per;
		int cut = len, i;
		if ( len > room ) {
			cut = room;
			for ( i = room; i > room / 2; i-- ) {
				if ( text[i] == ' ' ) {
					cut = i;	// (at a space, if one's not far back)
					break;
				}
			}
		}
		memcpy( lines[n], text, (size_t)cut );
		lines[n][cut] = '\0';
		if ( last ) {
			strcat( lines[n], ".." );
		}
		n++;
		text += cut;
		while ( *text == ' ' ) {
			text++;
		}
	}
	return n;
}

// What's said, as the screen shows it: two lines, the latest message all of
// it that fits in both, else the one before it too (a line of it, fainter).
static int VRA_CommsLines( const vraScreenState_t *st, float pw, char lines[2][VRA_LINE], int dim[2] ) {
	const int per = VRA_TextFits( pw - 40.0f, VRA_CHAT_SIZE );
	char older[2][VRA_LINE];
	int n = VRA_Wrap( st->comms[1], per, 2, lines );

	dim[0] = dim[1] = 0;
	if ( !n ) {
		return VRA_Wrap( st->comms[0], per, 2, lines );	// (said the other way round: the one there is)
	}
	if ( n == 1 && VRA_Wrap( st->comms[0], per, 1, older ) ) {
		memcpy( lines[1], lines[0], sizeof( lines[1] ) );
		memcpy( lines[0], older[0], sizeof( lines[0] ) );
		dim[0] = 1;
		n = 2;
	}
	return n;
}

static float VRA_WaveHeight( const vraScreenState_t *st ) {
	return VRA_WAVE_REST + ( 14.0f - VRA_WAVE_REST ) * VRA_Clampf( st->voice, 0.0f, 1.0f );
}

// What doesn't change: drawn once (VRA_OpenScreen).
static void VRA_ScreenBase( vraCanvas_t *c, int which, float pw, float ph ) {
	const vraColor_t AMBER = VRA_Hex( VRA_AMBER ), GREEN = VRA_Hex( VRA_GREEN );
	const vraColor_t amberDim = VRA_Rgba( 255, 180, 74, 0.7f );

	switch ( which ) {
	case VRA_SCREEN_HULL:
		VRA_ScreenFrame( c, pw, ph, AMBER );
		VRA_Text( c, "HULL", 28.0f, 50.0f, 28.0f, AMBER );
		VRA_StrokeRect( c, 190.0f, 76.0f, 132.0f, 92.0f, 3.0f, AMBER );	// the cabin from above, and where the legs join it
		VRA_Circle( c, 230.0f, 196.0f, 12.0f, 3.0f, AMBER );
		VRA_Circle( c, 282.0f, 196.0f, 12.0f, 3.0f, AMBER );
		VRA_Text( c, "LEFT LEG", 28.0f, 226.0f, 18.0f, AMBER );
		VRA_Text( c, "RIGHT LEG", 350.0f, 226.0f, 18.0f, AMBER );
		break;
	case VRA_SCREEN_WEAPONS:
		VRA_ScreenFrame( c, pw, ph, AMBER );
		VRA_Text( c, "WEAPONS", 28.0f, 48.0f, 28.0f, AMBER );
		VRA_Text( c, "MAIN GUNS", 28.0f, 96.0f, 19.0f, AMBER );
		VRA_Text( c, "HEAT", 28.0f, 134.0f, 19.0f, AMBER );
		VRA_Text( c, "SECOND WEAPON", 28.0f, 180.0f, 19.0f, AMBER );
		VRA_Text( c, "TRIGGER  FIRE   THUMB  SECOND", 28.0f, 240.0f, 19.0f, VRA_Rgba( 255, 180, 74, 0.6f ) );
		break;
	case VRA_SCREEN_GROUND: {
		const float top = VRA_GROUND_TOP, bottom = ph - 18.0f, cx = pw / 2.0f;
		float x;
		VRA_ScreenFrame( c, pw, ph, AMBER );
		VRA_Text( c, "GROUND", 28.0f, 48.0f, 28.0f, AMBER );
		VRA_Text( c, "CAM 2  LOOKING DOWN", 262.0f, 48.0f, 18.0f, amberDim );
		// the ground, a metre to a square: its lines along (clipped to its box:
		// 20..w-20 - those going by across, VRA_ScreenLive)
		for ( x = cx - VRA_GRID * 6.0f; x <= cx + VRA_GRID * 6.0f; x += VRA_GRID ) {
			if ( x >= 20.0f && x <= pw - 20.0f ) {
				VRA_Rect( c, x - 1.0f, top, 2.0f, bottom - top, VRA_Rgba( 255, 180, 74, 0.22f ) );
			}
		}
		break;
	}
	case VRA_SCREEN_TARGET: {
		const float hz = ph * 0.62f;
		float x;
		VRA_ScreenFrame( c, pw, ph, GREEN );
		VRA_Line( c, 20.0f, hz, pw - 20.0f, hz, 2.0f, VRA_Rgba( 99, 242, 161, 0.45f ) );
		for ( x = 40.0f; x < pw - 20.0f; x += 40.0f ) {
			VRA_Line( c, x, hz - 6.0f, x, hz + 6.0f, 2.0f, VRA_Rgba( 99, 242, 161, 0.45f ) );
		}
		// where the guns aim (the box over it: the same colour, either way)
		VRA_Line( c, pw / 2.0f - 16.0f, ph * 0.45f, pw / 2.0f + 16.0f, ph * 0.45f, 3.0f, GREEN );
		VRA_Line( c, pw / 2.0f, ph * 0.45f - 16.0f, pw / 2.0f, ph * 0.45f + 16.0f, 3.0f, GREEN );
		break;
	}
	case VRA_SCREEN_COMMS:
		VRA_ScreenFrame( c, pw, ph, GREEN );
		VRA_Text( c, "COMMS", 20.0f, 44.0f, 24.0f, GREEN );
		VRA_Text( c, "CH 2  OPEN", 20.0f, 76.0f, 17.0f, GREEN );
		break;
	case VRA_SCREEN_HEADING:
		VRA_ScreenFrame( c, pw, ph, AMBER );
		VRA_Text( c, "HEADING", 20.0f, 44.0f, 24.0f, AMBER );
		VRA_Rect( c, 28.0f, 204.0f, 2.0f, 16.0f, AMBER );	// where it faces, on the tape
		break;
	}
}

// What does: drawn over that each time, as the state has it.
static void VRA_ScreenLive( vraCanvas_t *c, int which, float pw, float ph, const vraScreenState_t *st ) {
	const vraColor_t AMBER = VRA_Hex( VRA_AMBER ), GREEN = VRA_Hex( VRA_GREEN ), RED = VRA_Hex( VRA_RED );
	const vraColor_t amberDim = VRA_Rgba( 255, 180, 74, 0.7f );
	char text[VRA_LINE + 16];

	switch ( which ) {
	case VRA_SCREEN_HULL: {
		const int armour = VRA_Clampi( st->armour, 0, 100 );
		const vraColor_t legs = ( armour > 50 ) ? GREEN : ( armour > 25 ) ? AMBER : RED;
		snprintf( text, sizeof( text ), "ARMOUR %d%%", armour );
		VRA_Text( c, text, 316.0f, 50.0f, 19.0f, amberDim );
		VRA_Bars( c, 28.0f, 238.0f, 130.0f, 5, VRA_LegBars( st ), legs );
		VRA_Bars( c, 350.0f, 238.0f, 134.0f, 5, VRA_LegBars( st ), legs );
		VRA_Text( c, st->walking ? "STEP WALK" : "STEP HOLD", 200.0f, 282.0f, 18.0f, AMBER );
		break;
	}
	case VRA_SCREEN_WEAPONS: {
		const int second = VRA_Clampi( st->second, 0, 999 );
		VRA_Text( c, st->ready ? "READY" : "COOLING", 400.0f, 96.0f, 19.0f, st->ready ? GREEN : RED );
		VRA_Bars( c, 120.0f, 120.0f, 360.0f, VRA_HEAT_BARS, VRA_Clampi( st->heat, 0, VRA_HEAT_BARS ), AMBER );
		snprintf( text, sizeof( text ), "%d", second );
		VRA_Text( c, text, 462.0f + 19.0f * 0.6f * ( 1 - (int)strlen( text ) ), 180.0f, 19.0f, AMBER );	// (its last figure where the page's one is)
		VRA_Bars( c, 28.0f, 192.0f, 456.0f, VRA_SECOND_BARS, VRA_SecondBars( st ), AMBER );
		break;
	}
	case VRA_SCREEN_GROUND: {
		const float top = VRA_GROUND_TOP, bottom = ph - 18.0f, cx = pw / 2.0f;
		const vraColor_t grid = VRA_Rgba( 255, 180, 74, 0.22f );
		float y;
		int i;
		// the ground going by: its lines across (clipped to its box)
		for ( y = top + VRA_GroundOffset( st ) - VRA_GRID; y < bottom + 1.0f; y += VRA_GRID ) {
			const float y0 = ( y - 1.0f > top ) ? y - 1.0f : top, y1 = ( y + 1.0f < bottom ) ? y + 1.0f : bottom;
			if ( y1 > y0 ) {
				VRA_Rect( c, 20.0f, y0, pw - 40.0f, y1 - y0, grid );
			}
		}
		for ( i = 0; i < 2; i++ ) {	// the feet: one planted while the other swings
			const float s = i ? 1.0f : -1.0f, swing = VRA_Swing( st, i );
			const float fy = ( top + bottom ) / 2.0f + 22.0f - ( swing > 0.0f ? swing : 0.0f ) * 46.0f;
			vraColor_t ink = AMBER;
			if ( swing > 0.05f ) {
				ink.a = 0.55f;
			}
			VRA_StrokeRect( c, cx + s * 62.0f - 22.0f, fy - 30.0f, 44.0f, 60.0f, 3.0f, ink );
		}
		// where the main guns' line meets the ground
		VRA_Line( c, cx - 12.0f, top + 26.0f, cx + 12.0f, top + 26.0f, 2.0f, GREEN );
		VRA_Line( c, cx, top + 14.0f, cx, top + 38.0f, 2.0f, GREEN );
		break;
	}
	case VRA_SCREEN_TARGET: {
		const float hz = ph * 0.62f, x = VRA_TargetX( st, pw );
		const float nameX = 24.0f, rangeX = pw - 180.0f;
		VRA_StrokeRect( c, x - 26.0f, hz - 38.0f, 52.0f, 52.0f, 3.0f, GREEN );
		if ( st->target[0] ) {
			const int fits = VRA_TextFits( rangeX - 12.0f - nameX, 26.0f );
			if ( (int)strlen( st->target ) > fits ) {
				snprintf( text, sizeof( text ), "%.*s..", fits > 2 ? fits - 2 : 0, st->target );
				VRA_Text( c, text, nameX, 46.0f, 26.0f, GREEN );
			} else {
				VRA_Text( c, st->target, nameX, 46.0f, 26.0f, GREEN );
			}
			snprintf( text, sizeof( text ), "RANGE %dM", VRA_Clampi( st->range, 0, 9999 ) );
			VRA_Text( c, text, rangeX, 46.0f, 19.0f, GREEN );
		} else {
			VRA_Text( c, "NO TARGET", nameX, 46.0f, 26.0f, GREEN );
			VRA_Text( c, "RANGE ----", rangeX, 46.0f, 19.0f, GREEN );
		}
		break;
	}
	case VRA_SCREEN_COMMS: {
		const float height = VRA_WaveHeight( st );
		char lines[2][VRA_LINE];
		int dim[2], n, i;
		float x, px = 0.0f, py = 0.0f;
		n = VRA_CommsLines( st, pw, lines, dim );
		for ( i = 0; i < n; i++ ) {
			vraColor_t ink = GREEN;
			if ( dim[i] ) {
				ink.a = 0.6f;
			}
			VRA_Text( c, lines[i], 20.0f, 104.0f + i * 20.0f, VRA_CHAT_SIZE, ink );
		}
		for ( x = 20.0f; x < pw - 20.0f; x += 4.0f ) {	// the voices' wave
			const float y = ph * 0.66f + sinf( x * 0.11f + st->t * 6.0f ) * height * sinf( x * 0.023f + st->t );
			if ( x > 20.0f ) {
				VRA_Line( c, px, py, x, y, 2.0f, GREEN );
			}
			px = x;
			py = y;
		}
		break;
	}
	case VRA_SCREEN_HEADING: {
		const int heading = ( ( st->heading % 360 ) + 360 ) % 360;
		int d;
		snprintf( text, sizeof( text ), "%03d", heading );
		VRA_Text( c, text, 20.0f, 116.0f, 46.0f, AMBER );
		for ( d = 0; d < 360; d += 30 ) {	// the tape: what's ahead of it, round to the right
			const float x = 20.0f + ( fmodf( (float)( d - heading + 720 + 30 ), 360.0f ) - 30.0f ) * VRA_TAPE;
			const char *mark = ( d == 0 ) ? "N" : ( d == 90 ) ? "E" : ( d == 180 ) ? "S" : ( d == 270 ) ? "W" : NULL;
			if ( !mark ) {
				snprintf( text, sizeof( text ), "%d", d );
				mark = text;
			}
			if ( x >= 14.0f && x + strlen( mark ) * 16.0f * 0.6f <= pw - 14.0f ) {
				VRA_Text( c, mark, x, 190.0f, 16.0f, AMBER );
			}
		}
		snprintf( text, sizeof( text ), "SPEED %2d KM/H", VRA_Clampi( st->speed, 0, 999 ) );
		VRA_Text( c, text, 20.0f, 240.0f, 16.0f, AMBER );
		break;
	}
	}
}

// The rows (the page's units, from, to) each screen's live parts are drawn
// in, all across it - the rest of it as it was drawn once.
#define VRA_BANDS	3
static const float vraBands[VRA_NUM_SCREENS][VRA_BANDS][2] = {
	{ { 34.0f, 52.0f }, { 236.0f, 284.0f }, { 0.0f, 0.0f } },		// HULL: the armour; the legs, the step
	{ { 80.0f, 98.0f }, { 118.0f, 138.0f }, { 164.0f, 210.0f } },	// WEAPONS: ready; the heat; the second weapon
	{ { 62.0f, 244.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f } },			// GROUND: the ground going by, the feet
	{ { 25.0f, 48.0f }, { 137.0f, 196.0f }, { 0.0f, 0.0f } },		// TARGET: who and how far; the box
	{ { 92.0f, 186.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f } },			// COMMS: what's said, the voices' wave
	{ { 80.0f, 118.0f }, { 176.0f, 192.0f }, { 226.0f, 242.0f } },	// HEADING: which way; the tape; the speed
};

struct vraScreen_s {
	int				which;
	float			pw, ph;			// its page
	vraCanvas_t		base;			// what doesn't change, drawn...
	unsigned char	*bytes;			// ...with the scanlines over it, as bytes
	int				rows[VRA_BANDS][2];	// the canvas's rows its live parts are drawn in (from, to: not it)
};

static float	*vraWork;		// a screen drawn in (one at a time)...
static size_t	vraWorkFloats;	// ...room for so many
static int		vraScreensOpen;

vraScreen_t *VRA_OpenScreen( int w, int h, int which ) {
	vraScreen_t *s;
	int b;

	if ( w <= 0 || h <= 0 || which < 0 || which >= VRA_NUM_SCREENS ) {
		return NULL;
	}
	s = (vraScreen_t *)calloc( 1, sizeof( *s ) );
	if ( !s ) {
		return NULL;
	}
	s->which = which;
	VRA_ScreenPage( which, &s->pw, &s->ph );
	VRA_Open( &s->base, w, h, s->pw, s->ph );
	s->bytes = (unsigned char *)malloc( (size_t)w * h * 4 );
	if ( !s->base.px || !s->bytes ) {
		free( s->base.px );
		free( s->bytes );
		free( s );
		return NULL;
	}
	VRA_ScreenBase( &s->base, which, s->pw, s->ph );
	VRA_ScanlinesToBytes( &s->base, s->pw, s->ph, s->bytes, 0, h );
	for ( b = 0; b < VRA_BANDS; b++ ) {
		const float y0 = vraBands[which][b][0], y1 = vraBands[which][b][1];
		if ( y1 > y0 ) {
			s->rows[b][0] = (int)floorf( y0 * s->base.sy ) - 1;
			s->rows[b][1] = (int)ceilf( y1 * s->base.sy ) + 1;
			s->rows[b][0] = s->rows[b][0] < 0 ? 0 : s->rows[b][0];
			s->rows[b][1] = s->rows[b][1] > h ? h : s->rows[b][1];
		}
	}
	vraScreensOpen++;
	return s;
}

void VRA_CloseScreen( vraScreen_t *s ) {
	if ( !s ) {
		return;
	}
	free( s->base.px );
	free( s->bytes );
	free( s );
	if ( --vraScreensOpen <= 0 ) {
		vraScreensOpen = 0;
		free( vraWork );
		vraWork = NULL;
		vraWorkFloats = 0;
	}
}

// (A number from what's drawn: FNV-1a.)
static unsigned VRA_HashInt( unsigned h, int v ) {
	int i;

	for ( i = 0; i < 4; i++ ) {
		h = ( h ^ ( ( (unsigned)v >> ( i * 8 ) ) & 0xff ) ) * 16777619u;
	}
	return h;
}

static unsigned VRA_HashText( unsigned h, const char *text ) {
	while ( *text ) {
		h = ( h ^ (unsigned char)*text++ ) * 16777619u;
	}
	return ( h ^ 0x100 ) * 16777619u;
}

// (A length on the page as a canvas's quarter pixels.)
static int VRA_Quarter( const vraScreen_t *s, float v ) {
	return (int)floorf( VRA_Clampf( v, -100000.0f, 100000.0f ) * s->base.sx * 4.0f + 0.5f );
}

unsigned VRA_ScreenKey( const vraScreen_t *s, const vraScreenState_t *st ) {
	unsigned h = 2166136261u;

	if ( !s ) {
		return 0;
	}
	h = VRA_HashInt( h, s->which );
	switch ( s->which ) {
	case VRA_SCREEN_HULL:
		h = VRA_HashInt( h, VRA_Clampi( st->armour, 0, 100 ) );
		h = VRA_HashInt( h, st->walking ? 1 : 0 );
		break;
	case VRA_SCREEN_WEAPONS:
		h = VRA_HashInt( h, st->ready ? 1 : 0 );
		h = VRA_HashInt( h, VRA_Clampi( st->heat, 0, VRA_HEAT_BARS ) );
		h = VRA_HashInt( h, VRA_Clampi( st->second, 0, 999 ) );
		h = VRA_HashInt( h, VRA_SecondBars( st ) );
		break;
	case VRA_SCREEN_GROUND: {
		int i;
		h = VRA_HashInt( h, VRA_Quarter( s, VRA_GroundOffset( st ) ) );
		for ( i = 0; i < 2; i++ ) {
			const float swing = VRA_Swing( st, i );
			h = VRA_HashInt( h, VRA_Quarter( s, swing > 0.0f ? swing * 46.0f : 0.0f ) );
			h = VRA_HashInt( h, swing > 0.05f ? 1 : 0 );
		}
		break;
	}
	case VRA_SCREEN_TARGET:
		h = VRA_HashText( h, st->target );
		h = VRA_HashInt( h, st->target[0] ? VRA_Clampi( st->range, 0, 9999 ) : -1 );
		h = VRA_HashInt( h, VRA_Quarter( s, VRA_TargetX( st, s->pw ) ) );
		break;
	case VRA_SCREEN_COMMS: {
		char lines[2][VRA_LINE];
		int dim[2], n, i;
		n = VRA_CommsLines( st, s->pw, lines, dim );
		for ( i = 0; i < n; i++ ) {
			h = VRA_HashText( h, lines[i] );
			h = VRA_HashInt( h, dim[i] );
		}
		h = VRA_HashInt( h, VRA_Quarter( s, VRA_WaveHeight( st ) ) );
		h = VRA_HashInt( h, (int)floorf( st->t * 30.0f ) );	// (the wave moves: as it does, a thirtieth of a second on)
		break;
	}
	case VRA_SCREEN_HEADING:
		h = VRA_HashInt( h, ( ( st->heading % 360 ) + 360 ) % 360 );
		h = VRA_HashInt( h, VRA_Clampi( st->speed, 0, 999 ) );
		break;
	}
	return h;
}

void VRA_DrawScreenLive( vraScreen_t *s, const vraScreenState_t *st, unsigned char *out ) {
	vraCanvas_t c;
	size_t n;
	int b;

	if ( !s ) {
		return;
	}
	c = s->base;
	n = (size_t)c.w * c.h * 4;
	memcpy( out, s->bytes, n );	// (what doesn't change, as it is)
	if ( vraWorkFloats < n ) {
		free( vraWork );
		vraWork = (float *)calloc( n, sizeof( float ) );
		vraWorkFloats = vraWork ? n : 0;
	}
	if ( !vraWork ) {
		return;
	}
	// what does, drawn in its rows (those only read back: the rest of the
	// canvas it's drawn in is another's, or as it was the last time)
	c.px = vraWork;
	for ( b = 0; b < VRA_BANDS; b++ ) {
		if ( s->rows[b][1] > s->rows[b][0] ) {
			const size_t from = (size_t)s->rows[b][0] * c.w * 4, count = (size_t)( s->rows[b][1] - s->rows[b][0] ) * c.w * 4;
			memcpy( c.px + from, s->base.px + from, count * sizeof( float ) );
		}
	}
	VRA_ScreenLive( &c, s->which, s->pw, s->ph, st );
	for ( b = 0; b < VRA_BANDS; b++ ) {
		if ( s->rows[b][1] > s->rows[b][0] ) {
			VRA_ScanlinesToBytes( &c, s->pw, s->ph, out, s->rows[b][0], s->rows[b][1] );
		}
	}
}

void VRA_DrawScreen( unsigned char *out, int w, int h, int which ) {
	vraScreen_t *s = VRA_OpenScreen( w, h, which );
	vraScreenState_t st;

	if ( !s ) {
		memset( out, 0, (size_t)w * h * 4 );
		return;
	}
	VRA_ScreenStart( &st );
	VRA_DrawScreenLive( s, &st, out );
	VRA_CloseScreen( s );
}
