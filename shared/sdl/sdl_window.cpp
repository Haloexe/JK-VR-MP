/*
===========================================================================
Copyright (C) 2005 - 2015, ioquake3 contributors
Copyright (C) 2013 - 2015, OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

#include <SDL.h>
#include <SDL_syswm.h>
#include "qcommon/qcommon.h"
#include "rd-common/tr_types.h"
#include "sys/sys_local.h"
#include "sdl_icon.h"

enum rserr_t
{
	RSERR_OK,

	RSERR_INVALID_FULLSCREEN,
	RSERR_INVALID_MODE,

	RSERR_UNKNOWN
};

static SDL_Window *screen = NULL;
static SDL_GLContext opengl_context;
static float displayAspect;

cvar_t *r_sdlDriver;
cvar_t *r_allowSoftwareGL;

// Window cvars
cvar_t	*r_fullscreen = 0;
cvar_t	*r_noborder;
cvar_t	*r_centerWindow;
cvar_t	*r_customwidth;
cvar_t	*r_customheight;
cvar_t	*r_swapInterval;
cvar_t	*r_stereo;
cvar_t	*r_mode;
cvar_t	*r_displayRefresh;

// Window surface cvars
cvar_t	*r_stencilbits;
cvar_t	*r_depthbits;
cvar_t	*r_colorbits;
cvar_t	*r_ignorehwgamma;
cvar_t  *r_ext_multisample;

#ifdef OPENJK_VR
/*
OpenJK VR: the window on a desktop with more than one display.

- Its picture (the client area) is the renderer's size, exactly. With
  per-monitor DPI awareness (the manifest) a window's frame is as thick as its
  display's scaling makes it, but SDL 2.0.12 works frames out at the main
  display's (AdjustWindowRectEx): on a display scaled otherwise the picture
  came out a few pixels off, and a window dragged onto one kept its size while
  Windows changed its frame, so the picture no longer filled it. Either is
  put right once the window's settled (WIN_FitPicture) - a window it won't
  take for (one across two displays, which its own new size moves from one
  to the other) is left as it is till it's moved.
- It opens where it was last time (vid_xpos, vid_ypos: its picture's top left
  corner on the desktop, kept as it moves) when that's on a display now, on
  that display whatever it is - a vid_restart makes a new window, which before
  went back to the first display. One with a title bar goes where it was (in
  the middle, after one that covered the display), on the display as far as it
  fits in the part the taskbar leaves, its title bar on the screen when not.
- One the size of its display ("Desktop", r_mode -2: windowed or borderless)
  takes the size of the display it's moved to: a vid_restart once it's settled
  there (after a map's loaded; not while a demo or video is being recorded).
*/

#ifdef _WIN32
#define WIN_FRAMES	// the window's frame and place through Windows itself
#endif

#define WIN_SETTLE_MSEC		250		// a window that's moved is looked at once it's been still this long
#define WIN_POLL_MSEC		1000	// ...and its picture's size anyway, this often
#define WIN_FITS			3		// fix-ups of its picture's size in a row, before it's left as it is till it's moved
#define WIN_RESTART_MSEC	5000	// a vid_restart of its own no sooner than this after the last
#define WIN_RESTARTS		3		// ...and no more than this many in a row,
#define WIN_RESTARTS_MSEC	60000	// ...each this soon after the one before

cvar_t	*vid_xpos;
cvar_t	*vid_ypos;

static struct {
	int			wantW, wantH;	// its picture's size, the renderer's (0: none kept - a headset's mirror)
	qboolean	desktop;		// the size of its display (r_mode -2)
	int			sizedFor;		// ...that display
	int			display;		// the display it was on, when last looked at
	int			changed;		// when it last moved or changed size (Sys_Milliseconds); 0: looked at since
	int			polled;			// when its picture's size was last looked at anyway
	int			fits;			// fix-ups of its picture's size in a row...
	int			fitX, fitY;		// ...where it was (its top left corner: a fix-up keeps that)
#ifdef WIN_FRAMES
	HWND		hwnd;
#endif
} win;

static int winRestarted = -WIN_RESTARTS_MSEC;	// when it last had a vid_restart of its own
static int winRestartsClose;					// ...and how many it's had in a row
#endif

/*
** R_GetModeInfo
*/
typedef struct vidmode_s
{
    const char *description;
    int         width, height;
} vidmode_t;

const vidmode_t r_vidModes[] = {
    { "Mode  0: 320x240",		320,	240 },
    { "Mode  1: 400x300",		400,	300 },
    { "Mode  2: 512x384",		512,	384 },
    { "Mode  3: 640x480",		640,	480 },
    { "Mode  4: 800x600",		800,	600 },
    { "Mode  5: 960x720",		960,	720 },
    { "Mode  6: 1024x768",		1024,	768 },
    { "Mode  7: 1152x864",		1152,	864 },
    { "Mode  8: 1280x1024",		1280,	1024 },
    { "Mode  9: 1600x1200",		1600,	1200 },
    { "Mode 10: 2048x1536",		2048,	1536 },
    { "Mode 11: 856x480 (wide)", 856,	 480 },
    { "Mode 12: 2400x600(surround)",2400,600 }
};
static const int	s_numVidModes = ARRAY_LEN( r_vidModes );

#define R_MODE_FALLBACK (4) // 640x480

qboolean R_GetModeInfo( int *width, int *height, int mode ) {
	const vidmode_t	*vm;

    if ( mode < -1 ) {
        return qfalse;
	}
	if ( mode >= s_numVidModes ) {
		return qfalse;
	}

	if ( mode == -1 ) {
		*width = r_customwidth->integer;
		*height = r_customheight->integer;
		return qtrue;
	}

	vm = &r_vidModes[mode];

    *width  = vm->width;
    *height = vm->height;

    return qtrue;
}

/*
** R_ModeList_f
*/
static void R_ModeList_f( void )
{
	int i;

	Com_Printf( "\n" );
	Com_Printf( "Mode -2: Use desktop resolution\n" );
	Com_Printf( "Mode -1: Use r_customWidth and r_customHeight variables\n" );
	for ( i = 0; i < s_numVidModes; i++ )
	{
		Com_Printf( "%s\n", r_vidModes[i].description );
	}
	Com_Printf( "\n" );
}

/*
===============
GLimp_Minimize

Minimize the game so that user is back at the desktop
===============
*/
#ifdef OPENJK_VR
// client/cl_vr.cpp
qboolean VR_WantSession( void );
qboolean VR_CreateSession( int *eyeWidth, int *eyeHeight );
void VR_DestroySession( void );
#endif

void GLimp_Minimize(void)
{
	SDL_MinimizeWindow( screen );
}

void WIN_Present( window_t *window )
{
	if ( window->api == GRAPHICS_API_OPENGL )
	{
		SDL_GL_SwapWindow(screen);

		if ( r_swapInterval->modified )
		{
			r_swapInterval->modified = qfalse;
			if ( SDL_GL_SetSwapInterval( r_swapInterval->integer ) == -1 )
			{
				Com_DPrintf( "SDL_GL_SetSwapInterval failed: %s\n", SDL_GetError() );
			}
		}
	}

	if ( r_fullscreen->modified )
	{
		bool	fullscreen;
		bool	needToToggle;
		bool	sdlToggled = qfalse;

		// Find out the current state
		fullscreen = (SDL_GetWindowFlags( screen ) & SDL_WINDOW_FULLSCREEN) != 0;

		if ( r_fullscreen->integer && Cvar_VariableIntegerValue( "in_nograb" ) )
		{
			Com_Printf( "Fullscreen not allowed with in_nograb 1\n" );
			Cvar_Set( "r_fullscreen", "0" );
			r_fullscreen->modified = qfalse;
		}

		// Is the state we want different from the current state?
		needToToggle = !!r_fullscreen->integer != fullscreen;

		if ( needToToggle )
		{
			sdlToggled = SDL_SetWindowFullscreen( screen, r_fullscreen->integer ) >= 0;

			// SDL_WM_ToggleFullScreen didn't work, so do it the slow way
			if ( !sdlToggled )
				Cbuf_AddText( "vid_restart\n" );

			IN_Restart();
		}

		r_fullscreen->modified = qfalse;
	}
}

/*
===============
GLimp_CompareModes
===============
*/
static int GLimp_CompareModes( const void *a, const void *b )
{
	const float ASPECT_EPSILON = 0.001f;
	SDL_Rect *modeA = (SDL_Rect *)a;
	SDL_Rect *modeB = (SDL_Rect *)b;
	float aspectA = (float)modeA->w / (float)modeA->h;
	float aspectB = (float)modeB->w / (float)modeB->h;
	int areaA = modeA->w * modeA->h;
	int areaB = modeB->w * modeB->h;
	float aspectDiffA = fabs( aspectA - displayAspect );
	float aspectDiffB = fabs( aspectB - displayAspect );
	float aspectDiffsDiff = aspectDiffA - aspectDiffB;

	if( aspectDiffsDiff > ASPECT_EPSILON )
		return 1;
	else if( aspectDiffsDiff < -ASPECT_EPSILON )
		return -1;
	else
		return areaA - areaB;
}

/*
===============
GLimp_DetectAvailableModes
===============
*/
static bool GLimp_DetectAvailableModes( int display = -1 )	// OpenJK VR: (a display, else the window's)
{
	int i, j;
	char buf[ MAX_STRING_CHARS ] = { 0 };
	SDL_Rect *modes;
	int numModes = 0;

	if ( display < 0 )
		display = SDL_GetWindowDisplayIndex( screen );
	if ( display < 0 )
	{
		Com_Printf( S_COLOR_YELLOW "WARNING: Couldn't get window display index, no resolutions detected: %s\n", SDL_GetError() );
		return false;
	}

	SDL_DisplayMode windowMode;

	if( SDL_GetWindowDisplayMode( screen, &windowMode ) < 0
#ifdef OPENJK_VR
		// (none for a window bigger than any of its display's modes - one
		// where it was last time, on a smaller display now: the desktop's
		// format will do, it's only that that's used. Not for fullscreen: a
		// mode no display has fails, as ever, for r_mode 4)
		&& ( ( SDL_GetWindowFlags( screen ) & SDL_WINDOW_FULLSCREEN ) || SDL_GetDesktopDisplayMode( display, &windowMode ) < 0 )
#endif
		)
	{
		Com_Printf( S_COLOR_YELLOW "WARNING: Couldn't get window display mode, no resolutions detected (%s).\n", SDL_GetError() );
		return false;
	}

	int numDisplayModes = SDL_GetNumDisplayModes( display );
	if ( numDisplayModes < 0 )
		Com_Error( ERR_FATAL, "SDL_GetNumDisplayModes() FAILED (%s)", SDL_GetError() );

	modes = (SDL_Rect *)SDL_calloc( (size_t)numDisplayModes, sizeof( SDL_Rect ) );
	if ( !modes )
		Com_Error( ERR_FATAL, "Out of memory" );

	for( i = 0; i < numDisplayModes; i++ )
	{
		SDL_DisplayMode mode;

		if( SDL_GetDisplayMode( display, i, &mode ) < 0 )
			continue;

		if( !mode.w || !mode.h )
		{
			Com_Printf( "Display supports any resolution\n" );
			SDL_free( modes );
			return true;
		}

		if( windowMode.format != mode.format )
			continue;

		// SDL can give the same resolution with different refresh rates.
		// Only list resolution once.
		for( j = 0; j < numModes; j++ )
		{
			if( mode.w == modes[ j ].w && mode.h == modes[ j ].h )
				break;
		}

		if( j != numModes )
			continue;

		modes[ numModes ].w = mode.w;
		modes[ numModes ].h = mode.h;
		numModes++;
	}

	if( numModes > 1 )
		qsort( modes, numModes, sizeof( SDL_Rect ), GLimp_CompareModes );

	for( i = 0; i < numModes; i++ )
	{
		const char *newModeString = va( "%ux%u ", modes[ i ].w, modes[ i ].h );

		if( strlen( newModeString ) < (int)sizeof( buf ) - strlen( buf ) )
			Q_strcat( buf, sizeof( buf ), newModeString );
		else
			Com_Printf( "Skipping mode %ux%u, buffer too small\n", modes[ i ].w, modes[ i ].h );
	}

	if( *buf )
	{
		buf[ strlen( buf ) - 1 ] = 0;
		Com_Printf( "Available modes: '%s'\n", buf );
		Cvar_Set( "r_availableModes", buf );
	}

	SDL_free( modes );
	return true;
}

#ifdef OPENJK_VR
// The display a point on the desktop is on (-1: none)
static int WIN_DisplayAt( int x, int y )
{
	const int n = SDL_GetNumVideoDisplays();

	for ( int i = 0; i < n; i++ )
	{
		SDL_Rect b;

		if ( SDL_GetDisplayBounds( i, &b ) == 0 && x >= b.x && y >= b.y && x < b.x + b.w && y < b.y + b.h )
			return i;
	}
	return -1;
}

// The display most of a rect on the desktop is on, as Windows has a window's
// (-1: none of it's on one), and how much of it (*area)
static int WIN_DisplayMostOf( const SDL_Rect *r, int *area )
{
	const int n = SDL_GetNumVideoDisplays();
	int best = -1, bestArea = 0;

	for ( int i = 0; i < n; i++ )
	{
		SDL_Rect b, on;

		if ( SDL_GetDisplayBounds( i, &b ) == 0 && SDL_IntersectRect( r, &b, &on ) && on.w * on.h > bestArea )
		{
			best = i;
			bestArea = on.w * on.h;
		}
	}
	if ( area )
		*area = bestArea;
	return best;
}

// Where the window was last time (*x, *y: its picture's top left corner), if
// that's on a display now: the display most of a window this mode's size there
// is on - for one the size of its display (r_mode -2), a display most of a
// window its size there would be on itself - or else the one its corner's on.
// -1: none.
static int WIN_SavedPlace( int mode, int *x, int *y )
{
	SDL_Rect r;
	int display = -1, best = 0;

	if ( !vid_xpos->string[0] || !vid_ypos->string[0] )
		return -1;
	*x = r.x = vid_xpos->integer;
	*y = r.y = vid_ypos->integer;
	if ( R_GetModeInfo( &r.w, &r.h, mode ) )
	{
		display = WIN_DisplayMostOf( &r, NULL );
	}
	else
	{
		for ( int i = 0; i < SDL_GetNumVideoDisplays(); i++ )
		{
			SDL_DisplayMode m;
			int area;

			if ( SDL_GetDesktopDisplayMode( i, &m ) != 0 )
				continue;
			r.w = m.w;
			r.h = m.h;
			if ( WIN_DisplayMostOf( &r, &area ) == i && area > best )
			{
				display = i;
				best = area;
			}
		}
	}
	return display >= 0 ? display : WIN_DisplayAt( *x, *y );
}

// Whether a display has a fullscreen mode this mode's size (or bigger)
static qboolean WIN_ModeFits( int mode, int display )
{
	SDL_DisplayMode m;
	int w, h;

	if ( !R_GetModeInfo( &w, &h, mode ) )
		return qtrue;	// (the display's own size)
	for ( int i = 0; i < SDL_GetNumDisplayModes( display ); i++ )
	{
		if ( SDL_GetDisplayMode( display, i, &m ) == 0 && m.w >= w && m.h >= h )
			return qtrue;
	}
	return qfalse;
}

// Where SDL puts the picture of a window that size asked for at x, y: in the
// middle of a display for SDL_WINDOWPOS_UNDEFINED or _CENTERED
static void WIN_Resolve( int *x, int *y, int w, int h )
{
	const qboolean atX = (qboolean)( SDL_WINDOWPOS_ISUNDEFINED( *x ) || SDL_WINDOWPOS_ISCENTERED( *x ) );
	const qboolean atY = (qboolean)( SDL_WINDOWPOS_ISUNDEFINED( *y ) || SDL_WINDOWPOS_ISCENTERED( *y ) );
	SDL_Rect b;
	int d;

	if ( !atX && !atY )
		return;
	d = ( atX ? *x : *y ) & 0xFFFF;
	if ( d >= SDL_GetNumVideoDisplays() )
		d = 0;
	if ( SDL_GetDisplayBounds( d, &b ) != 0 )
		return;
	if ( atX )
		*x = b.x + ( b.w - w ) / 2;
	if ( atY )
		*y = b.y + ( b.h - h ) / 2;
}

// The display the window's on, and its place and size (*b): on Windows the
// monitor most of it is on, as Windows has it (its DPI is that monitor's) -
// -1 for one SDL doesn't know of (plugged in since SDL started); else the one
// its middle's on, as SDL has it
static int WIN_DisplayOfWindow( SDL_Rect *b )
{
	int display;
#ifdef WIN_FRAMES
	HMONITOR monitor = MonitorFromWindow( win.hwnd, MONITOR_DEFAULTTONEAREST );
	MONITORINFO mi;

	mi.cbSize = sizeof( mi );
	if ( monitor && GetMonitorInfo( monitor, &mi ) && mi.rcMonitor.right > mi.rcMonitor.left && mi.rcMonitor.bottom > mi.rcMonitor.top )
	{
		b->x = mi.rcMonitor.left;
		b->y = mi.rcMonitor.top;
		b->w = mi.rcMonitor.right - mi.rcMonitor.left;
		b->h = mi.rcMonitor.bottom - mi.rcMonitor.top;
		return WIN_DisplayAt( b->x + b->w / 2, b->y + b->h / 2 );
	}
#endif
	SDL_DisplayMode mode;

	display = SDL_GetWindowDisplayIndex( screen );
	if ( display < 0 || SDL_GetDisplayBounds( display, b ) != 0 )
		return -1;
	if ( SDL_GetDesktopDisplayMode( display, &mode ) == 0 && mode.w > 0 && mode.h > 0 )
	{
		b->w = mode.w;
		b->h = mode.h;
	}
	return display;
}

// What the Video menu offers for the display the window's on
// (r_desktopResolution, r_availableModes)
static void WIN_DisplayCvars( int display )
{
	SDL_DisplayMode desktopMode;

	if ( SDL_GetDesktopDisplayMode( display, &desktopMode ) == 0 && desktopMode.w > 0 && desktopMode.h > 0 )
	{
		displayAspect = (float)desktopMode.w / (float)desktopMode.h;
		Cvar_Set( "r_desktopResolution", va( "%dx%d", desktopMode.w, desktopMode.h ) );
	}
	Cvar_Set( "r_availableModes", "" );
	GLimp_DetectAvailableModes( display );
}

#ifdef WIN_FRAMES
// Whether the window's picture is that size (or can't be looked at: minimized)
static qboolean WIN_PictureIs( int w, int h )
{
	RECT r;

	if ( !win.hwnd || IsIconic( win.hwnd ) || !GetClientRect( win.hwnd, &r ) || r.right <= r.left || r.bottom <= r.top )
		return qtrue;
	return (qboolean)( r.right - r.left == w && r.bottom - r.top == h );
}

// Gives the window's picture the renderer's size. Asked for a size, SDL makes
// the window that plus the frame it works out: so, the size plus what the real
// frame has over SDL's.
static void WIN_FitPicture( void )
{
	RECT outer, client, sdlFrame = { 0, 0, 0, 0 };
	int w, h;

	if ( WIN_PictureIs( win.wantW, win.wantH ) || !GetWindowRect( win.hwnd, &outer ) || !GetClientRect( win.hwnd, &client ) )
		return;
	w = client.right - client.left;
	h = client.bottom - client.top;
	if ( !( SDL_GetWindowFlags( screen ) & SDL_WINDOW_BORDERLESS ) )
		AdjustWindowRectEx( &sdlFrame, (DWORD)GetWindowLong( win.hwnd, GWL_STYLE ), FALSE, 0 );	// (as SDL does)
	Com_DPrintf( "The window's picture is %dx%d, not %dx%d: fitting it\n", w, h, win.wantW, win.wantH );
	SDL_SetWindowSize( screen,
		win.wantW + ( outer.right - outer.left - w ) - ( sdlFrame.right - sdlFrame.left ),
		win.wantH + ( outer.bottom - outer.top - h ) - ( sdlFrame.bottom - sdlFrame.top ) );
}

// A window with a title bar: its picture where it was asked to be (SDL puts the
// window as if its frame were as thick as at the system's scaling)
static void WIN_MovePicture( int x, int y )
{
	POINT at = { 0, 0 };
	RECT r;

	if ( !win.hwnd || !ClientToScreen( win.hwnd, &at ) || !GetWindowRect( win.hwnd, &r ) || ( at.x == x && at.y == y ) )
		return;
	SetWindowPos( win.hwnd, NULL, r.left + x - at.x, r.top + y - at.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE );
}

// A window with a title bar, on its display: all of it in the part the taskbar
// leaves when it fits there, else its top left corner there (its title bar on
// the screen)
static void WIN_KeepOnDisplay( int display )
{
	SDL_Rect area;
	RECT r;
	int x, y;

	if ( !win.hwnd || display < 0 || SDL_GetDisplayUsableBounds( display, &area ) != 0 || area.w <= 0 || area.h <= 0
		|| !GetWindowRect( win.hwnd, &r ) )
		return;
	x = ( r.right - r.left >= area.w ) ? area.x : Com_Clampi( area.x, area.x + area.w - ( r.right - r.left ), r.left );
	y = ( r.bottom - r.top >= area.h ) ? area.y : Com_Clampi( area.y, area.y + area.h - ( r.bottom - r.top ), r.top );
	if ( x != r.left || y != r.top )
		SetWindowPos( win.hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE );
}
#endif

// A new window (still hidden unless fullscreen): its picture the renderer's
// size, one with a title bar where it was asked to be (its picture at x, y)
// and kept on its display, then shown - and what's kept of it, noted
static void WIN_Place( const glconfig_t *glConfig, int mode, qboolean fullscreen, int display, int x, int y )
{
	Com_Memset( &win, 0, sizeof( win ) );
	win.sizedFor = display;
	// (a fullscreen one too: Alt+Enter makes it a window without a new one)
	win.wantW = glConfig->vidWidth;
	win.wantH = glConfig->vidHeight;
	win.desktop = (qboolean)( mode == -2 );
#ifdef WIN_FRAMES
	SDL_SysWMinfo info;
	SDL_VERSION( &info.version );
	if ( SDL_GetWindowWMInfo( screen, &info ) && info.subsystem == SDL_SYSWM_WINDOWS )
		win.hwnd = info.info.win.window;
#endif
	if ( !fullscreen )
	{
#ifdef WIN_FRAMES
		const qboolean titled = (qboolean)!( SDL_GetWindowFlags( screen ) & SDL_WINDOW_BORDERLESS );
		WIN_FitPicture();
		if ( titled )
		{
			WIN_MovePicture( x, y );
			WIN_KeepOnDisplay( display );
		}
		// (moved onto another display than it was made on, it may have
		// another frame: fitted again)
		for ( int i = 0; i < 2 && !WIN_PictureIs( win.wantW, win.wantH ); i++ )
		{
			WIN_FitPicture();
			if ( titled )
				WIN_KeepOnDisplay( display );
		}
#endif
		SDL_ShowWindow( screen );
	}
	win.display = SDL_GetWindowDisplayIndex( screen );
}

/*
===============
WIN_WindowEvent

The window's events (sdl_input.cpp)
===============
*/
void WIN_WindowEvent( const SDL_WindowEvent *e )
{
	if ( !screen || e->windowID != SDL_GetWindowID( screen ) )
		return;

	switch ( e->event )
	{
		case SDL_WINDOWEVENT_MOVED:
			// where it is, for next time (not where a fullscreen one is - the
			// corner of its display - but where it was before that)
			if ( !( SDL_GetWindowFlags( screen ) & ( SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MINIMIZED ) ) )
			{
				Cvar_Set( "vid_xpos", va( "%d", e->data1 ) );
				Cvar_Set( "vid_ypos", va( "%d", e->data2 ) );
			}
			// fall through
		case SDL_WINDOWEVENT_RESIZED:
		case SDL_WINDOWEVENT_SIZE_CHANGED:
		case SDL_WINDOWEVENT_SHOWN:
		case SDL_WINDOWEVENT_RESTORED:
			win.changed = Q_max( 1, Sys_Milliseconds() );
			break;

		default:
			break;
	}
}

/*
===============
WIN_WindowFrame

Every frame (sdl_input.cpp): once the window's settled after it's moved or
changed size, its picture's size put right, and one the size of its display
on a display of another size made again, that one's size.
  loading    a map's loading or the like: that waits till it's done
  recording  a demo or video's being recorded: that isn't done (it'd stop it)
===============
*/
void WIN_WindowFrame( qboolean loading, qboolean recording )
{
	const int now = Sys_Milliseconds();
	int display;

	if ( !screen || !win.wantW || ( SDL_GetWindowFlags( screen ) & ( SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN ) ) )
		return;

#ifdef WIN_FRAMES
	// (Windows can change a frame without the window moving - its display's
	// scaling changed: a look now and then anyway)
	if ( !win.changed && now - win.polled >= WIN_POLL_MSEC )
	{
		win.polled = now;
		if ( !WIN_PictureIs( win.wantW, win.wantH ) )
			win.changed = Q_max( 1, now - WIN_SETTLE_MSEC );
	}
#endif
	if ( !win.changed || now - win.changed < WIN_SETTLE_MSEC )
		return;

	SDL_Rect b = { 0, 0, 0, 0 };
	const int on = WIN_DisplayOfWindow( &b );

	// (what the Video page offers: that display's)
	display = on >= 0 ? on : SDL_GetWindowDisplayIndex( screen );
	if ( display >= 0 && display != win.display )
	{
		win.display = display;
		WIN_DisplayCvars( display );
	}

	if ( win.desktop )
	{
		if ( on != win.sizedFor && b.w > 0 && b.h > 0 && ( b.w != win.wantW || b.h != win.wantH ) )
		{
			if ( loading )
				return;
			if ( now - winRestarted >= WIN_RESTARTS_MSEC )
				winRestartsClose = 0;
			if ( !recording && winRestartsClose < WIN_RESTARTS )
			{
				if ( now - winRestarted < WIN_RESTART_MSEC )
					return;	// (not this soon after the last: in a moment)
				Com_Printf( "The window's on a %dx%d display now: making it that size\n", b.w, b.h );
				// (the new window goes on the display this is the corner of -
				// SDL knows of one plugged in since once it starts again)
				Cvar_Set( "vid_xpos", va( "%d", b.x ) );
				Cvar_Set( "vid_ypos", va( "%d", b.y ) );
				winRestarted = now;
				winRestartsClose++;
				win.changed = 0;
				Cbuf_AddText( "vid_restart\n" );
				return;
			}
			Com_Printf( "The window's on a %dx%d display now: vid_restart makes it that size\n", b.w, b.h );
		}
		win.sizedFor = on;
	}

#ifdef WIN_FRAMES
	if ( !WIN_PictureIs( win.wantW, win.wantH ) )
	{
		RECT r;

		if ( GetWindowRect( win.hwnd, &r ) && ( r.left != win.fitX || r.top != win.fitY ) )
		{
			win.fitX = r.left;
			win.fitY = r.top;
			win.fits = 0;	// (moved since: worth trying again)
		}
		if ( win.fits < WIN_FITS )
		{
			win.fits++;
			WIN_FitPicture();
			win.changed = Q_max( 1, now );	// (and a look again once it's settled)
			return;
		}
	}
	else
	{
		win.fits = 0;
	}
#endif
	win.changed = 0;
}
#endif

/*
===============
GLimp_SetMode
===============
*/
static rserr_t GLimp_SetMode(glconfig_t *glConfig, const windowDesc_t *windowDesc, const char *windowTitle, int mode, qboolean fullscreen, qboolean noborder)
{
	int perChannelColorBits;
	int colorBits, depthBits, stencilBits;
	int samples;
	int i = 0;
	SDL_Surface *icon = NULL;
	Uint32 flags = SDL_WINDOW_SHOWN;
	SDL_DisplayMode desktopMode;
	int display = 0;
	int x = SDL_WINDOWPOS_UNDEFINED, y = SDL_WINDOWPOS_UNDEFINED;
#ifdef OPENJK_VR
	int savedX = 0, savedY = 0;
	qboolean saved = qfalse;
#endif

	if ( windowDesc->api == GRAPHICS_API_OPENGL )
	{
		flags |= SDL_WINDOW_OPENGL;
	}

	Com_Printf( "Initializing display\n");

	icon = SDL_CreateRGBSurfaceFrom(
		(void *)CLIENT_WINDOW_ICON.pixel_data,
		CLIENT_WINDOW_ICON.width,
		CLIENT_WINDOW_ICON.height,
		CLIENT_WINDOW_ICON.bytes_per_pixel * 8,
		CLIENT_WINDOW_ICON.bytes_per_pixel * CLIENT_WINDOW_ICON.width,
#ifdef Q3_LITTLE_ENDIAN
		0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000
#else
		0xFF000000, 0x00FF0000, 0x0000FF00, 0x000000FF
#endif
		);

	// If a window exists, note its display index
	if ( screen != NULL )
	{
		display = SDL_GetWindowDisplayIndex( screen );
		if ( display < 0 )
		{
			Com_DPrintf( "SDL_GetWindowDisplayIndex() failed: %s\n", SDL_GetError() );
		}
	}
#ifdef OPENJK_VR
	// OpenJK VR: else (a vid_restart's too: it shuts the window) the display
	// it was on last time
	else
	{
		const int savedDisplay = WIN_SavedPlace( mode, &savedX, &savedY );

		// (fullscreen at a size that display hasn't: on the first, as before)
		if ( savedDisplay >= 0 && ( !fullscreen || WIN_ModeFits( mode, savedDisplay ) ) )
		{
			display = savedDisplay;
			saved = qtrue;
		}
	}
#endif

	if( display >= 0 && SDL_GetDesktopDisplayMode( display, &desktopMode ) == 0 )
	{
		displayAspect = (float)desktopMode.w / (float)desktopMode.h;

		Com_Printf( "Display aspect: %.3f\n", displayAspect );

		// OpenJK VR: what "r_mode -2" gives, for the Video settings
		Cvar_Set( "r_desktopResolution", va( "%dx%d", desktopMode.w, desktopMode.h ) );
	}
	else
	{
		Com_Memset( &desktopMode, 0, sizeof( SDL_DisplayMode ) );

		Com_Printf( "Cannot determine display aspect, assuming 1.333\n" );
	}

	Com_Printf( "...setting mode %d:", mode );

	if (mode == -2)
	{
		// use desktop video resolution
		if( desktopMode.h > 0 )
		{
			glConfig->vidWidth = desktopMode.w;
			glConfig->vidHeight = desktopMode.h;
		}
		else
		{
			glConfig->vidWidth = 640;
			glConfig->vidHeight = 480;
			Com_Printf( "Cannot determine display resolution, assuming 640x480\n" );
		}

		//glConfig.windowAspect = (float)glConfig.vidWidth / (float)glConfig.vidHeight;
	}
	else if ( !R_GetModeInfo( &glConfig->vidWidth, &glConfig->vidHeight, /*&glConfig.windowAspect,*/ mode ) )
	{
		Com_Printf( " invalid mode\n" );
		SDL_FreeSurface( icon );
		return RSERR_INVALID_MODE;
	}
	Com_Printf( " %d %d\n", glConfig->vidWidth, glConfig->vidHeight);

	// Center window
	if( r_centerWindow->integer && !fullscreen )
	{
#ifdef OPENJK_VR
		// (in the middle of its display: this was the size of its display,
		// but at the first display)
		x = SDL_WINDOWPOS_CENTERED_DISPLAY( display >= 0 ? display : 0 );
		y = SDL_WINDOWPOS_CENTERED_DISPLAY( display >= 0 ? display : 0 );
#else
		x = ( desktopMode.w / 2 ) - ( glConfig->vidWidth / 2 );
		y = ( desktopMode.h / 2 ) - ( glConfig->vidHeight / 2 );
#endif
	}
#ifdef OPENJK_VR
	// OpenJK VR: where it was last time - in the middle of the display after
	// one that covered it (that was at its corner, where a picture with a title
	// bar can't be) - or else (and fullscreen) on that display
	else if( saved && !fullscreen )
	{
		SDL_Rect bounds;

		if( !noborder && SDL_GetDisplayBounds( display, &bounds ) == 0 && savedX == bounds.x && savedY == bounds.y )
		{
			x = SDL_WINDOWPOS_CENTERED_DISPLAY( display );
			y = SDL_WINDOWPOS_CENTERED_DISPLAY( display );
		}
		else
		{
			x = savedX;
			y = savedY;
		}
	}
	else if( display > 0 )
	{
		x = SDL_WINDOWPOS_UNDEFINED_DISPLAY( display );
		y = SDL_WINDOWPOS_UNDEFINED_DISPLAY( display );
	}
#endif

	// Destroy existing state if it exists
	if( opengl_context != NULL )
	{
		SDL_GL_DeleteContext( opengl_context );
		opengl_context = NULL;
	}

	if( screen != NULL )
	{
		const Uint32 oldFlags = SDL_GetWindowFlags( screen );

		SDL_GetWindowPosition( screen, &x, &y );
		Com_DPrintf( "Existing window at %dx%d before being destroyed\n", x, y );

		// OpenJK VR: from fullscreen or a borderless window to one with a title
		// bar: in the middle of the display, not where the edgeless one was
		// (from the top left corner, its title bar would be off the screen)
		if( !fullscreen && !noborder && ( oldFlags & ( SDL_WINDOW_FULLSCREEN | SDL_WINDOW_BORDERLESS ) ) )
		{
			x = SDL_WINDOWPOS_CENTERED_DISPLAY( display >= 0 ? display : 0 );
			y = SDL_WINDOWPOS_CENTERED_DISPLAY( display >= 0 ? display : 0 );
		}

		SDL_DestroyWindow( screen );
		screen = NULL;
	}

	// OpenJK VR: a borderless window goes on the display it was on - covering
	// it from its top left corner when it's that display's size or bigger
	// (borderless fullscreen: "r_noborder 1" with "r_mode -2"), else in the
	// middle of it
	if( !fullscreen && noborder && display >= 0 )
	{
		SDL_Rect bounds;

		if( SDL_GetDisplayBounds( display, &bounds ) == 0 && bounds.w > 0 && bounds.h > 0 )
		{
			if( glConfig->vidWidth >= bounds.w && glConfig->vidHeight >= bounds.h )
			{
				x = bounds.x;
				y = bounds.y;
			}
			else
			{
				x = bounds.x + ( bounds.w - glConfig->vidWidth ) / 2;
				y = bounds.y + ( bounds.h - glConfig->vidHeight ) / 2;
			}
		}
	}

	if( fullscreen )
	{
#ifdef MACOS_X
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
#else
        flags |= SDL_WINDOW_FULLSCREEN;
#endif
		glConfig->isFullscreen = qtrue;
	}
	else
	{
		if( noborder )
			flags |= SDL_WINDOW_BORDERLESS;

#ifdef OPENJK_VR
		// (shown once it's the right size, in its place: WIN_Place)
		flags = ( flags & ~SDL_WINDOW_SHOWN ) | SDL_WINDOW_HIDDEN;
#endif
		glConfig->isFullscreen = qfalse;
	}

	colorBits = r_colorbits->integer;
	if ((!colorBits) || (colorBits >= 32))
		colorBits = 24;

	if (!r_depthbits->integer)
		depthBits = 24;
	else
		depthBits = r_depthbits->integer;

	stencilBits = r_stencilbits->integer;
	samples = r_ext_multisample->integer;

	if ( windowDesc->api == GRAPHICS_API_OPENGL )
	{
		for (i = 0; i < 16; i++)
		{
			int testColorBits, testDepthBits, testStencilBits;

			// 0 - default
			// 1 - minus colorBits
			// 2 - minus depthBits
			// 3 - minus stencil
			if ((i % 4) == 0 && i)
			{
				// one pass, reduce
				switch (i / 4)
				{
					case 2 :
						if (colorBits == 24)
							colorBits = 16;
						break;
					case 1 :
						if (depthBits == 24)
							depthBits = 16;
						else if (depthBits == 16)
							depthBits = 8;
					case 3 :
						if (stencilBits == 24)
							stencilBits = 16;
						else if (stencilBits == 16)
							stencilBits = 8;
				}
			}

			testColorBits = colorBits;
			testDepthBits = depthBits;
			testStencilBits = stencilBits;

			if ((i % 4) == 3)
			{ // reduce colorBits
				if (testColorBits == 24)
					testColorBits = 16;
			}

			if ((i % 4) == 2)
			{ // reduce depthBits
				if (testDepthBits == 24)
					testDepthBits = 16;
				else if (testDepthBits == 16)
					testDepthBits = 8;
			}

			if ((i % 4) == 1)
			{ // reduce stencilBits
				if (testStencilBits == 24)
					testStencilBits = 16;
				else if (testStencilBits == 16)
					testStencilBits = 8;
				else
					testStencilBits = 0;
			}

			if (testColorBits == 24)
				perChannelColorBits = 8;
			else
				perChannelColorBits = 4;

			SDL_GL_SetAttribute( SDL_GL_RED_SIZE, perChannelColorBits );
			SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, perChannelColorBits );
			SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, perChannelColorBits );
			SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, testDepthBits );
			SDL_GL_SetAttribute( SDL_GL_STENCIL_SIZE, testStencilBits );

			SDL_GL_SetAttribute( SDL_GL_MULTISAMPLEBUFFERS, samples ? 1 : 0 );
			SDL_GL_SetAttribute( SDL_GL_MULTISAMPLESAMPLES, samples );

			if ( windowDesc->gl.majorVersion )
			{
				int compactVersion = windowDesc->gl.majorVersion * 100 + windowDesc->gl.minorVersion * 10;

				SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, windowDesc->gl.majorVersion );
				SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, windowDesc->gl.minorVersion );

				if ( windowDesc->gl.profile == GLPROFILE_ES || compactVersion >= 320 )
				{
					int profile;
					switch ( windowDesc->gl.profile )
					{
					default:
					case GLPROFILE_COMPATIBILITY:
						profile = SDL_GL_CONTEXT_PROFILE_COMPATIBILITY;
						break;

					case GLPROFILE_CORE:
						profile = SDL_GL_CONTEXT_PROFILE_CORE;
						break;

					case GLPROFILE_ES:
						profile = SDL_GL_CONTEXT_PROFILE_ES;
						break;
					}

					SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, profile );
				}
			}

			if ( windowDesc->gl.contextFlags & GLCONTEXT_DEBUG )
			{
				SDL_GL_SetAttribute( SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG );
			}

			if(r_stereo->integer)
			{
				glConfig->stereoEnabled = qtrue;
				SDL_GL_SetAttribute(SDL_GL_STEREO, 1);
			}
			else
			{
				glConfig->stereoEnabled = qfalse;
				SDL_GL_SetAttribute(SDL_GL_STEREO, 0);
			}

			SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );
			SDL_GL_SetAttribute( SDL_GL_ACCELERATED_VISUAL, !r_allowSoftwareGL->integer );

			if( ( screen = SDL_CreateWindow( windowTitle, x, y,
					glConfig->vidWidth, glConfig->vidHeight, flags ) ) == NULL )
			{
				Com_DPrintf( "SDL_CreateWindow failed: %s\n", SDL_GetError( ) );
				continue;
			}

#ifndef MACOS_X
			SDL_SetWindowIcon( screen, icon );
#endif

			if( fullscreen )
			{
				SDL_DisplayMode mode;

				switch( testColorBits )
				{
					case 16: mode.format = SDL_PIXELFORMAT_RGB565; break;
					case 24: mode.format = SDL_PIXELFORMAT_RGB24;  break;
					default: Com_DPrintf( "testColorBits is %d, can't fullscreen\n", testColorBits ); continue;
				}

				mode.w = glConfig->vidWidth;
				mode.h = glConfig->vidHeight;
				mode.refresh_rate = glConfig->displayFrequency = r_displayRefresh->integer;
				mode.driverdata = NULL;

				if( SDL_SetWindowDisplayMode( screen, &mode ) < 0 )
				{
					Com_DPrintf( "SDL_SetWindowDisplayMode failed: %s\n", SDL_GetError( ) );
					continue;
				}
			}

			if( ( opengl_context = SDL_GL_CreateContext( screen ) ) == NULL )
			{
				Com_Printf( "SDL_GL_CreateContext failed: %s\n", SDL_GetError( ) );
				continue;
			}

			if ( SDL_GL_SetSwapInterval( r_swapInterval->integer ) == -1 )
			{
				Com_DPrintf( "SDL_GL_SetSwapInterval failed: %s\n", SDL_GetError() );
			}

			glConfig->colorBits = testColorBits;
			glConfig->depthBits = testDepthBits;
			glConfig->stencilBits = testStencilBits;

			Com_Printf( "Using %d color bits, %d depth, %d stencil display.\n",
					glConfig->colorBits, glConfig->depthBits, glConfig->stencilBits );
			break;
		}

		if (opengl_context == NULL) {
			SDL_FreeSurface(icon);
			return RSERR_UNKNOWN;
		}
	}
	else
	{
		// Just create a regular window
		if( ( screen = SDL_CreateWindow( windowTitle, x, y,
				glConfig->vidWidth, glConfig->vidHeight, flags ) ) == NULL )
		{
			Com_DPrintf( "SDL_CreateWindow failed: %s\n", SDL_GetError( ) );
		}
		else
		{
#ifndef MACOS_X
			SDL_SetWindowIcon( screen, icon );
#endif
			if( fullscreen )
			{
				if( SDL_SetWindowDisplayMode( screen, NULL ) < 0 )
				{
					Com_DPrintf( "SDL_SetWindowDisplayMode failed: %s\n", SDL_GetError( ) );
				}
			}
		}
	}

	SDL_FreeSurface( icon );

#ifdef OPENJK_VR
	if ( screen != NULL )
	{
		WIN_Resolve( &x, &y, glConfig->vidWidth, glConfig->vidHeight );
		WIN_Place( glConfig, mode, fullscreen, display, x, y );
	}
#endif

	if (!GLimp_DetectAvailableModes())
	{
		return RSERR_UNKNOWN;
	}

	return RSERR_OK;
}

/*
===============
GLimp_StartDriverAndSetMode
===============
*/
static qboolean GLimp_StartDriverAndSetMode(glconfig_t *glConfig, const windowDesc_t *windowDesc, int mode, qboolean fullscreen, qboolean noborder)
{
	rserr_t err;

	if (!SDL_WasInit(SDL_INIT_VIDEO))
	{
		const char *driverName;

		if (SDL_Init(SDL_INIT_VIDEO) == -1)
		{
			Com_Printf( "SDL_Init( SDL_INIT_VIDEO ) FAILED (%s)\n", SDL_GetError());
			return qfalse;
		}

		driverName = SDL_GetCurrentVideoDriver();

		if (!driverName)
		{
			Com_Error( ERR_FATAL, "No video driver initialized" );
			return qfalse;
		}

		Com_Printf( "SDL using driver \"%s\"\n", driverName );
		Cvar_Set( "r_sdlDriver", driverName );
	}

	if (SDL_GetNumVideoDisplays() <= 0)
	{
		Com_Error( ERR_FATAL, "SDL_GetNumVideoDisplays() FAILED (%s)", SDL_GetError() );
	}

	if (fullscreen && Cvar_VariableIntegerValue( "in_nograb" ) )
	{
		Com_Printf( "Fullscreen not allowed with in_nograb 1\n");
		Cvar_Set( "r_fullscreen", "0" );
		r_fullscreen->modified = qfalse;
		fullscreen = qfalse;
	}

	err = GLimp_SetMode(glConfig, windowDesc, CLIENT_WINDOW_TITLE, mode, fullscreen, noborder);

	switch ( err )
	{
		case RSERR_INVALID_FULLSCREEN:
			Com_Printf( "...WARNING: fullscreen unavailable in this mode\n" );
			return qfalse;
		case RSERR_INVALID_MODE:
			Com_Printf( "...WARNING: could not set the given mode (%d)\n", mode );
			return qfalse;
		case RSERR_UNKNOWN:
			Com_Printf( "...ERROR: no display modes could be found.\n" );
			return qfalse;
		default:
			break;
	}

	return qtrue;
}

window_t WIN_Init( const windowDesc_t *windowDesc, glconfig_t *glConfig )
{
	Cmd_AddCommand("modelist", R_ModeList_f);
	Cmd_AddCommand("minimize", GLimp_Minimize);

	r_sdlDriver			= Cvar_Get( "r_sdlDriver",			"",			CVAR_ROM );
	r_allowSoftwareGL	= Cvar_Get( "r_allowSoftwareGL",	"0",		CVAR_ARCHIVE_ND|CVAR_LATCH );

	// Window cvars
	r_fullscreen		= Cvar_Get( "r_fullscreen",			"0",		CVAR_ARCHIVE|CVAR_LATCH );
	r_noborder			= Cvar_Get( "r_noborder",			"0",		CVAR_ARCHIVE|CVAR_LATCH );
	r_centerWindow		= Cvar_Get( "r_centerWindow",		"0",		CVAR_ARCHIVE|CVAR_LATCH );
	r_customwidth		= Cvar_Get( "r_customwidth",		"1600",		CVAR_ARCHIVE|CVAR_LATCH );
	r_customheight		= Cvar_Get( "r_customheight",		"1024",		CVAR_ARCHIVE|CVAR_LATCH );
	r_swapInterval		= Cvar_Get( "r_swapInterval",		"0",		CVAR_ARCHIVE_ND );
	r_stereo			= Cvar_Get( "r_stereo",				"0",		CVAR_ARCHIVE_ND|CVAR_LATCH );
	r_mode				= Cvar_Get( "r_mode",				"4",		CVAR_ARCHIVE|CVAR_LATCH );
	r_displayRefresh	= Cvar_Get( "r_displayRefresh",		"0",		CVAR_LATCH );
	Cvar_CheckRange( r_displayRefresh, 0, 240, qtrue );

	// Window render surface cvars
	r_stencilbits		= Cvar_Get( "r_stencilbits",		"8",		CVAR_ARCHIVE_ND|CVAR_LATCH );
	r_depthbits			= Cvar_Get( "r_depthbits",			"0",		CVAR_ARCHIVE_ND|CVAR_LATCH );
	r_colorbits			= Cvar_Get( "r_colorbits",			"0",		CVAR_ARCHIVE_ND|CVAR_LATCH );
	r_ignorehwgamma		= Cvar_Get( "r_ignorehwgamma",		"0",		CVAR_ARCHIVE_ND|CVAR_LATCH );
	r_ext_multisample	= Cvar_Get( "r_ext_multisample",	"0",		CVAR_ARCHIVE_ND|CVAR_LATCH );
	Cvar_Get( "r_availableModes", "", CVAR_ROM );
	Cvar_Get( "r_desktopResolution", "", CVAR_ROM );	// OpenJK VR: the display's own, e.g. "2560x1440"
#ifdef OPENJK_VR
	vid_xpos			= Cvar_Get( "vid_xpos",				"",			CVAR_ARCHIVE_ND, "Where the window was: its picture's left edge on the desktop (it opens there, on that display)" );
	vid_ypos			= Cvar_Get( "vid_ypos",				"",			CVAR_ARCHIVE_ND, "Where the window was: its picture's top edge on the desktop" );
#endif

#ifdef OPENJK_VR
	// The desktop window only mirrors the headset view, and a multisampled
	// window can't receive the mirror blit: create this window without MSAA
	// (the player's r_ext_multisample setting itself is left alone).
	const qboolean wantVR = (qboolean)( windowDesc->vrCapable && VR_WantSession() );
	const int savedMultisample = r_ext_multisample->integer;
	if ( wantVR ) {
		r_ext_multisample->integer = 0;
	}
#endif

	// Create the window and set up the context
	if(!GLimp_StartDriverAndSetMode( glConfig, windowDesc, r_mode->integer,
										(qboolean)r_fullscreen->integer, (qboolean)r_noborder->integer ))
	{
		if( r_mode->integer != R_MODE_FALLBACK )
		{
			Com_Printf( "Setting r_mode %d failed, falling back on r_mode %d\n", r_mode->integer, R_MODE_FALLBACK );

			if (!GLimp_StartDriverAndSetMode( glConfig, windowDesc, R_MODE_FALLBACK, qfalse, qfalse ))
			{
				// Nothing worked, give up
				Com_Error( ERR_FATAL, "GLimp_Init() - could not load OpenGL subsystem" );
			}
		}
	}

#ifdef OPENJK_VR
	r_ext_multisample->integer = savedMultisample;
#endif

	glConfig->deviceSupportsGamma =
		(qboolean)(!r_ignorehwgamma->integer && SDL_SetWindowBrightness( screen, 1.0f ) >= 0);

#ifdef OPENJK_VR
	{
		// The OpenGL context exists now: hook it up to the headset. If that
		// works the renderer draws at eye-buffer resolution and the window
		// becomes a mirror of the left eye.
		int eyeWidth, eyeHeight;
		if ( wantVR && VR_CreateSession( &eyeWidth, &eyeHeight ) )
		{
			glConfig->vidWidth = eyeWidth;
			glConfig->vidHeight = eyeHeight;
			// Each eye is its own frame; quad-buffered stereo isn't used.
			glConfig->stereoEnabled = qfalse;
			// Hardware gamma only affects the monitor, never the headset, so
			// let the renderer bake brightness into the textures instead.
			glConfig->deviceSupportsGamma = qfalse;
			// The headset paces the frame rate (xrWaitFrame); the desktop
			// mirror must never block on the monitor's vsync.
			SDL_GL_SetSwapInterval( 0 );
			// The mirror fills the window whatever its size (VR_Mirror), and
			// isn't made again for another display.
			win.wantW = win.wantH = 0;
		}
	}
#endif

	// This depends on SDL_INIT_VIDEO, hence having it here
	IN_Init( screen );

	// window_t is only really useful for Windows if the renderer wants to create a D3D context.
	window_t window = {};

	window.api = windowDesc->api;

#if defined(_WIN32)
	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);

	if ( SDL_GetWindowWMInfo(screen, &info) )
	{
		switch(info.subsystem) {
			case SDL_SYSWM_WINDOWS:
				window.handle = info.info.win.window;
				break;

			default:
				break;
		}
	}
#endif

	return window;
}

/*
===============
GLimp_Shutdown
===============
*/
void WIN_Shutdown( void )
{
	Cmd_RemoveCommand("modelist");
	Cmd_RemoveCommand("minimize");

	IN_Shutdown();

#ifdef OPENJK_VR
	// swapchains belong to this OpenGL context
	VR_DestroySession();
#endif

	if ( opengl_context ) {
		SDL_GL_DeleteContext( opengl_context );
		opengl_context = NULL;
	}

	if ( screen ) {
		SDL_DestroyWindow( screen );
		screen = NULL;
	}
#ifdef OPENJK_VR
	Com_Memset( &win, 0, sizeof( win ) );
#endif

	SDL_QuitSubSystem( SDL_INIT_VIDEO );
}

void GLimp_EnableLogging( qboolean enable )
{
}

void GLimp_LogComment( char *comment )
{
}

void WIN_SetGamma( glconfig_t *glConfig, byte red[256], byte green[256], byte blue[256] )
{
	Uint16 table[3][256];
	int i, j;

	if( !glConfig->deviceSupportsGamma || r_ignorehwgamma->integer > 0 )
		return;

	for (i = 0; i < 256; i++)
	{
		table[0][i] = ( ( ( Uint16 ) red[i] ) << 8 ) | red[i];
		table[1][i] = ( ( ( Uint16 ) green[i] ) << 8 ) | green[i];
		table[2][i] = ( ( ( Uint16 ) blue[i] ) << 8 ) | blue[i];
	}

#if defined(_WIN32)
	// Win2K and newer put this odd restriction on gamma ramps...
	{
		OSVERSIONINFO	vinfo;

		vinfo.dwOSVersionInfoSize = sizeof( vinfo );
		GetVersionEx( &vinfo );
		if( vinfo.dwMajorVersion >= 5 && vinfo.dwPlatformId == VER_PLATFORM_WIN32_NT )
		{
			Com_DPrintf( "performing gamma clamp.\n" );
			for( j = 0 ; j < 3 ; j++ )
			{
				for( i = 0 ; i < 128 ; i++ )
				{
					table[j][i] = Q_min(table[j][i], (128 + i) << 8);
				}

				table[j][127] = Q_min(table[j][127], 254 << 8);
			}
		}
	}
#endif

	// enforce constantly increasing
	for (j = 0; j < 3; j++)
	{
		for (i = 1; i < 256; i++)
		{
			if (table[j][i] < table[j][i-1])
				table[j][i] = table[j][i-1];
		}
	}

	if ( SDL_SetWindowGammaRamp( screen, table[0], table[1], table[2] ) < 0 )
	{
		Com_DPrintf( "SDL_SetWindowGammaRamp() failed: %s\n", SDL_GetError() );
	}
}

void *WIN_GL_GetProcAddress( const char *proc )
{
	return SDL_GL_GetProcAddress( proc );
}

qboolean WIN_GL_ExtensionSupported( const char *extension )
{
	return SDL_GL_ExtensionSupported( extension ) == SDL_TRUE ? qtrue : qfalse;
}
