/*
===========================================================================
OpenJK VR (multiplayer) - holsters: what a VR player's four holsters hold,
as their game says (userinfo "vrh": cg_vrholster.c) and the server tells
everyone (CS_PLAYERS "vh": g_vrholster.c). Four codes, "a,b,c,d": the right
hip, the left hip, the right shoulder (the item hangs on the back) and the
left one. 0: empty; 1 or 2: their first or second saber (saber[0],
saber[1]); 16 + WP_*: that gun or explosive.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#ifndef BG_VRHOLSTER_H
#define BG_VRHOLSTER_H

enum {
	VR_HOLSTER_HIP_R,
	VR_HOLSTER_HIP_L,
	VR_HOLSTER_SHOULDER_R,
	VR_HOLSTER_SHOULDER_L,
	VR_NUM_HOLSTERS
};

#define VR_HOLSTER_EMPTY	0
#define VR_HOLSTER_SABER	1		// + 0 or 1: the first saber or the second
#define VR_HOLSTER_WEAPON	16		// + WP_*: a weapon

// Whether weapon w can hang in a holster: a gun, an explosive, the stun
// baton - not the saber (its hilts are the sabers' own codes), fists, or a
// weapon that's part of the level.
static QINLINE qboolean BG_VRHolsterWeapon( int w ) {
	return (qboolean)( w > WP_NONE && w < WP_NUM_WEAPONS && w != WP_SABER && w != WP_MELEE && w != WP_EMPLACED_GUN &&
		w != WP_TURRET );
}

// Whether c is something a holster can hold (or empty).
static QINLINE qboolean BG_VRHolsterItem( int c ) {
	return (qboolean)( c == VR_HOLSTER_EMPTY || c == VR_HOLSTER_SABER || c == VR_HOLSTER_SABER + 1 ||
		( c >= VR_HOLSTER_WEAPON && BG_VRHolsterWeapon( c - VR_HOLSTER_WEAPON ) ) );
}

// "a,b,c,d" into the four codes: anything that isn't an item, empty; an item
// a second time, empty there (each is in one holster at most). qfalse:
// nothing said (no holsters: every one empty).
static QINLINE qboolean BG_VRHolsterParse( const char *s, int out[VR_NUM_HOLSTERS] ) {
	int i, j;

	for ( i = 0; i < VR_NUM_HOLSTERS; i++ ) {
		out[i] = VR_HOLSTER_EMPTY;
	}
	if ( !s || !*s ) {
		return qfalse;
	}
	for ( i = 0; i < VR_NUM_HOLSTERS && *s; i++ ) {
		int c = 0, digits = 0;
		while ( *s >= '0' && *s <= '9' ) {
			if ( digits < 3 ) {
				c = c * 10 + ( *s - '0' );
			}
			digits++;
			s++;
		}
		out[i] = ( digits && digits <= 3 && BG_VRHolsterItem( c ) ) ? c : VR_HOLSTER_EMPTY;
		for ( j = 0; j < i; j++ ) {
			if ( out[i] != VR_HOLSTER_EMPTY && out[j] == out[i] ) {
				out[i] = VR_HOLSTER_EMPTY;
			}
		}
		while ( *s && *s != ',' ) {
			s++;
		}
		if ( *s == ',' ) {
			s++;
		}
	}
	return qtrue;
}

#endif // BG_VRHOLSTER_H
