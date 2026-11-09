/*
===========================================================================
OpenJK VR (multiplayer) - shots from the barrel of a VR player's gun, for
the server (g_vrgun.c: where their shots start) and the player's own game
(cg_vr.c: the aim dot, where they'll land).

A VR player's gun is drawn in their hand (cg_weapons.c), and their game
sends where it is with every command (their avatar: its model's origin -
the grip - and axes). On a server of this build their shots leave it from
its muzzle - the weapon's world model's "*flash", or VR_BARREL_NO_FLASH
along its barrel from the grip on a model without one, where its flash is
drawn - the way they aim (the command's angles: the barrel's), when both
say they go by this rule (VR_BARREL_RULE): the player's game ("vrb" in its
userinfo) and the server ("vrGunBarrel" in its serverinfo: g_vrGunBarrel
on, VR players' poses coming in). Otherwise - and with the gun held
somewhere the aim isn't (VR_BARREL_AGREE) - from the eye, as ever.

Never from beyond a wall, nor into one: from the muzzle only with room
for the biggest shot (VR_BARREL_BOX each way) all the way from the
player's eye to their hand (the grip, no further than VR_BARREL_REACH from
the eye) and on along the gun to its muzzle - anything in the way (a wall,
someone, the gun in either; not the dead, whom the player's game never
has in its way) and the shot goes from the eye as ever. So a gun held out
past a corner or a doorframe fires from its muzzle, and one pushed into a
wall fires from the eye.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#ifndef BG_VRGUN_H
#define BG_VRGUN_H

#define VR_BARREL_RULE		1		// "vrb" and "vrGunBarrel": the rule both go by (another: each fires from the eye)
#define VR_BARREL_NO_FLASH	12.0f	// a model with no "*flash": its shots leave this far along its barrel from its grip
#define VR_BARREL_AGREE		0.9f	// the barrel this close to the aim (the cosine: 26 degrees), else shots go from the eye
#define VR_BARREL_BOX		4.0f	// the room a shot needs on its way out, each way: the biggest's (a rocket's, a thermal's: 3) and
									// a unit to spare
#define VR_BARREL_REACH		48.0f	// the hand no further than this from the eye (an arm and the camera's lag: about 1.4 m)
#define VR_BARREL_MASK		( ( MASK_SHOT & ~CONTENTS_CORPSE ) | CONTENTS_SHOTCLIP )	// what's in the way of the gun (WP_TraceSetStart's
										// walls, and bodies - not the dead: the player's game never has them in its way)

// A trace for BG_VRBarrelClear: a box from start to end, passing pass.
typedef void ( *vrBarrelTrace_t )( trace_t *tr, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int pass,
	int mask );

// Whether a weapon's shots can leave its barrel: a gun (or a thing thrown
// from the hand) - not a saber, fists, the stun baton, an emplaced gun.
static QINLINE qboolean BG_VRBarrelWeapon( int weapon ) {
	return (qboolean)( weapon > WP_NONE && weapon < WP_NUM_WEAPONS && weapon != WP_SABER && weapon != WP_MELEE &&
		weapon != WP_STUN_BATON && weapon != WP_EMPLACED_GUN );
}

// The gun's muzzle in the world, and which way its barrel runs: from where
// its model is (origin, axis: unit) and its muzzle in the model's own axes
// (flash) and its barrel's way in them (barrel: unit). The muzzle's in
// whole units, as CreateMissile puts a bolt's start (SnapVector: then left
// as it is) - the room for a shot found round the very point it starts at.
static QINLINE void BG_VRBarrelPlace( const vec3_t origin, vec3_t axis[3], const vec3_t flash, const vec3_t barrel,
	vec3_t muzzle, vec3_t dir ) {
	int i;

	VectorCopy( origin, muzzle );
	VectorClear( dir );
	for ( i = 0; i < 3; i++ ) {
		VectorMA( muzzle, flash[i], axis[i], muzzle );
		VectorMA( dir, barrel[i], axis[i], dir );
	}
	SnapVector( muzzle );
}

// Whether a shot leaves the barrel from its muzzle: the hand (grip) within
// reach of the eye, and room for it all the way from the eye to the hand
// and on along the gun to the muzzle.
static QINLINE qboolean BG_VRBarrelClear( vrBarrelTrace_t trace, int pass, const vec3_t eye, const vec3_t grip, const vec3_t muzzle ) {
	static const vec3_t mins = { -VR_BARREL_BOX, -VR_BARREL_BOX, -VR_BARREL_BOX }, maxs = { VR_BARREL_BOX, VR_BARREL_BOX, VR_BARREL_BOX };
	trace_t tr;

	if ( DistanceSquared( eye, grip ) > VR_BARREL_REACH * VR_BARREL_REACH ) {
		return qfalse;
	}
	// (begun inside something, a trace leaving it doesn't stop for it: no room there either)
	trace( &tr, eye, mins, maxs, grip, pass, VR_BARREL_MASK );
	if ( tr.startsolid || tr.fraction < 1.0f ) {
		return qfalse;
	}
	trace( &tr, grip, mins, maxs, muzzle, pass, VR_BARREL_MASK );	// (from where there's room: the hand)
	return (qboolean)( tr.fraction >= 1.0f );
}

#endif // BG_VRGUN_H
