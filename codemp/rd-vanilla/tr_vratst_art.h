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
void VRA_DrawScreen( unsigned char *out, int w, int h, int which );
