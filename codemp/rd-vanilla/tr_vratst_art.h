/*
===========================================================================
OpenJK VR (multiplayer) - the AT-ST cockpit's pictures (tr_vratst_art.cpp):
each drawn into out - RGBA, a byte each, w by h (a square: size by size) -
as the AT-ST Cockpit Test study's page draws it.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#pragma once

#define VRA_PLATING_INSIDE		0	// the cabin's walls (hullTex)
#define VRA_PLATING_OUTSIDE		1	// the skin round it (hullOutTex)

enum {
	VRA_SCREEN_HULL,
	VRA_SCREEN_WEAPONS,
	VRA_SCREEN_GROUND,
	VRA_SCREEN_TARGET,
	VRA_SCREEN_COMMS,
	VRA_SCREEN_HEADING,
	VRA_NUM_SCREENS
};

void VRA_DrawPlating( unsigned char *out, int size, int which );
void VRA_DrawFloor( unsigned char *out, int size );
void VRA_DrawSeat( unsigned char *out, int size );
void VRA_DrawSight( unsigned char *out, int size );
void VRA_DrawSolid( unsigned char *out, int size, unsigned rgb );

// A screen as it stands at the start (the study's: standing, nothing in the
// sight).
void VRA_DrawScreen( unsigned char *out, int w, int h, int which );

// What the screens show as the cockpit goes (vrCockpitScreens_t, as the
// renderer has it: tr_vratst.cpp).
#define VRA_LINE			80		// a line of writing, with its end
#define VRA_HEAT_BARS		12
#define VRA_SECOND_BARS		8

typedef struct {
	float	t;				// seconds: what moves of itself (the target's sweep, the voice's wave)
	int		armour;			// % (HULL)
	int		walking;		// its legs walking
	int		ready;			// the main guns can fire (WEAPONS)
	int		heat;			// 0..VRA_HEAT_BARS
	int		second;			// the second weapon's shots left...
	int		secondMax;		// ...of
	float	ground;			// metres walked (GROUND)
	float	stride;			// 0..1: the right foot down at 0, the left at 0.5
	float	walk;			// 0..1
	char	target[VRA_LINE];	// (TARGET; empty: none)
	int		range;			// metres
	float	bearing;		// -1..1
	char	comms[2][VRA_LINE];	// what's been said, the latest last (COMMS)
	float	voice;			// 0..1
	int		heading;		// 0..359 (HEADING)
	int		speed;			// km/h
} vraScreenState_t;

// The state the screens start in (VRA_DrawScreen's).
void VRA_ScreenStart( vraScreenState_t *st );

// A screen kept, to be drawn as its state changes: what doesn't change drawn
// once, what does over it each time. NULL: no room for it.
typedef struct vraScreen_s vraScreen_t;
vraScreen_t	*VRA_OpenScreen( int w, int h, int which );
void		VRA_CloseScreen( vraScreen_t *s );
// What it'd show of a state, as a number: the same as it shows, nothing to draw.
unsigned	VRA_ScreenKey( const vraScreen_t *s, const vraScreenState_t *st );
// Drawn as that state has it, into out (RGBA, a byte each, as VRA_DrawScreen's).
void		VRA_DrawScreenLive( vraScreen_t *s, const vraScreenState_t *st, unsigned char *out );
