/*
===========================================================================
OpenJK VR (multiplayer) - how heavy a VR player's blade is in the hand, for
the server (g_vrsaber.c) and the player's own game (cg_vrbody.c).

Each blade has a feel: blue, yellow or red (the saber style; Tavion's swings
as blue, Desann's as red), the staff, or two sabers. The heavier it feels,
the slower it can turn in the hand (cg_vrbody.c holds it back), so the
blade moves slower - and the speeds the server's cuts need come down by the
same share, so a full swing still does full damage. Two sabers turn as fast
as they did, and need the speeds they did. The server's weights, which
decide clashes and how hard a cut can be, are still the styles' (the staff
and two sabers are medium): the staff is heavier in feel only.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#ifndef BG_VRSABER_H
#define BG_VRSABER_H

// EV_SABER_BLOCK's generic1: a VR blade deflected a bolt (g_vrsaber.c
// VRS_BoltEvent; cg_vrbody.c feels it in the hand)
#define VR_EVENT_BOLT	1

typedef enum {
	VR_FEEL_BLUE,		// the fast style (and Tavion's)
	VR_FEEL_YELLOW,		// medium (and any other)
	VR_FEEL_RED,		// strong (and Desann's)
	VR_FEEL_STAFF,
	VR_FEEL_DUAL,		// two sabers: each of them
	VR_NUM_FEELS
} vrFeel_t;

// A blade's feel, from the saber style.
static QINLINE int BG_VRFeel( int saberAnimLevel ) {
	switch ( saberAnimLevel ) {
	case SS_FAST:
	case SS_TAVION:
		return VR_FEEL_BLUE;
	case SS_STRONG:
	case SS_DESANN:
		return VR_FEEL_RED;
	case SS_STAFF:
		return VR_FEEL_STAFF;
	case SS_DUAL:
		return VR_FEEL_DUAL;
	default:
		return VR_FEEL_YELLOW;
	}
}

// How fast a blade of that feel turns in the hand at most (degrees a
// second, in one hand; two hands turn it a fifth faster)...
static QINLINE float BG_VRFeelTurn( int feel ) {
	switch ( feel ) {
	case VR_FEEL_BLUE:	return 2000.0f;
	case VR_FEEL_RED:	return 490.0f;
	case VR_FEEL_STAFF:	return 540.0f;
	case VR_FEEL_DUAL:	return 750.0f;
	default:			return 620.0f;
	}
}

// ...and how fast it turned when the server's cut speeds were set (its
// g_vrSaberLight / Medium / Heavy defaults).
static QINLINE float BG_VRFeelTurnWas( int feel ) {
	switch ( feel ) {
	case VR_FEEL_BLUE:	return 2600.0f;
	case VR_FEEL_RED:	return 550.0f;
	default:			return 750.0f;
	}
}

// The share of its weight's cut speeds a blade of that feel needs: the
// share of the speed it had that it has.
static QINLINE float BG_VRFeelCutScale( int feel ) {
	return BG_VRFeelTurn( feel ) / BG_VRFeelTurnWas( feel );
}

#endif // BG_VRSABER_H
