/*
===========================================================================
OpenJK VR (multiplayer) - cgame side.

The engine renders the headset view and handles the controllers; the cgame
only needs to know where the hands are so it can draw things in them. Every
function here does nothing unless the engine is the OpenJK VR engine with a
headset running, so this cgame also works unchanged in desktop OpenJK.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "cg_local.h"
#include "game/bg_vrgun.h"

// Whether the engine is the OpenJK VR engine this cgame was built for: only
// then do the VR members of trap->ext exist at all.
qboolean CG_VR_Engine( void ) {
	static qboolean checked = qfalse;
	static qboolean supported = qfalse;

	if ( !checked ) {
		char tag[64];
		trap->Cvar_VariableStringBuffer( "vr_engine", tag, sizeof( tag ) );
		supported = ( !strcmp( tag, VR_ENGINE_TAG ) && trap->ext.VR_GetClientState ) ? qtrue : qfalse;
		checked = qtrue;
	}
	return supported;
}

// Returns the engine's VR state, or NULL when not playing in a headset.
const vrClientState_t *CG_VR( void ) {
	const vrClientState_t *vr;

	if ( !CG_VR_Engine() ) {
		return NULL;
	}

	vr = trap->ext.VR_GetClientState();
	return ( vr && vr->active ) ? vr : NULL;
}

// The headset's position/orientation in the world (for sound, culling).
qboolean CG_VR_HeadPose( vec3_t origin, matrix3_t axis ) {
	const vrClientState_t *vr = CG_VR();
	vec3_t ho, ha[3];
	int i;

	if ( !vr ) {
		return qfalse;
	}
	VectorCopy( vr->headOrigin, ho );
	for ( i = 0; i < 3; i++ ) {
		VectorCopy( vr->headAxis[i], ha[i] );
	}
	VR_ToWorld( vr, cg.refdef.vieworg, cg.refdef.viewaxis, ho, ha, origin, axis );
	return qtrue;
}

// The hand guns go in: the right, or the left when you play left-handed
// (vr_leftHanded, the engine's setting - it aims them with that hand too).
int CG_VR_GunHand( void ) {
	return vr_leftHanded.integer ? VR_HAND_LEFT : VR_HAND_RIGHT;
}

// True when the gun should be drawn in (and aimed with) the gun hand.
qboolean CG_VR_HandAim( void ) {
	const vrClientState_t *vr = CG_VR();
	return ( vr && vr->handAim && vr->handValid[CG_VR_GunHand()] ) ? qtrue : qfalse;
}

// A colour across the whole screen (CG_Draw2D's tints: the Force's, water's, a
// fall's fade), under the 2D drawn after it. In a headset the 2D is the HUD's
// screen in front of you, where that alone is a coloured square: there the
// engine fills the rest of your view round it with the same colour too
// (ext.VR_TintView; CG_VR first - an older engine's ext ends before it).
void CG_FillScreen( const vec4_t color ) {
	CG_FillRect( 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, color );
	if ( CG_VR() && trap->ext.VR_TintView ) {
		trap->ext.VR_TintView( color );
	}
}

// What's where your eyes are, for water's, slime's and lava's tints: in a
// headset, at your head - your eyes are drawn from it, up to Head Reach from
// the game's (and further crouched) - else where the game has them.
int CG_VR_ViewContents( void ) {
	vec3_t head;
	matrix3_t axis;

	if ( CG_VR_HeadPose( head, axis ) ) {
		return CG_PointContents( head, -1 );
	}
	return cg.refdef.viewContents;
}

/*
=================
CG_VR_PlaceWeaponInHand

Moves the first-person weapon's "hand" model so the gun sits in the gun
hand's controller and points along its aim ray. The hand model is invisible;
it only carries the "tag_weapon" the gun is mounted on. Its tag offset
(which normally puts the gun in front of the camera) is cancelled using
frame 0, so recoil/reload animations still move the gun.
=================
*/
void CG_VR_PlaceWeaponInHand( refEntity_t *hand, vec3_t angles ) {
	const vrClientState_t *vr = CG_VR();
	const int h = CG_VR_GunHand();
	vec3_t gripLocal, aimLocal[3];
	vec3_t gripWorld, aimWorld[3];
	orientation_t tag;
	int i;

	if ( !vr ) {
		return;
	}

	// position from the grip pose (the palm), direction from the aim pose
	// (where the shots go)
	VectorCopy( vr->gripOrigin[h], gripLocal );
	for ( i = 0; i < 3; i++ ) {
		VectorCopy( vr->aimAxis[h][i], aimLocal[i] );
	}
	// (the engine already applied vr_gunPitch to the aim axis, so the gun
	// and the shots stay lined up)
	VR_ToWorld( vr, cg.refdef.vieworg, cg.refdef.viewaxis, gripLocal, aimLocal, gripWorld, aimWorld );

	// The gun is mounted with gun.axis = tag.axis * hand.axis and
	// gun.origin = hand.origin + tag.origin * hand.axis. Solve for the hand so
	// the gun (at animation frame 0) has exactly the aim axis at the grip.
	VectorCopy( gripWorld, hand->origin );
	AxisCopy( aimWorld, hand->axis );
	if ( hand->hModel && trap->R_LerpTag( &tag, hand->hModel, 0, 0, 1.0f, "tag_weapon" ) ) {
		int j, k;
		for ( i = 0; i < 3; i++ ) {
			for ( j = 0; j < 3; j++ ) {
				hand->axis[i][j] = 0.0f;
				for ( k = 0; k < 3; k++ ) {
					hand->axis[i][j] += tag.axis[k][i] * aimWorld[k][j];	// tag^T * aim
				}
			}
		}
		for ( i = 0; i < 3; i++ ) {
			VectorMA( hand->origin, -tag.origin[i], hand->axis[i], hand->origin );
		}
	}

	// player-tunable offsets (game units: forward, left - out from your palm
	// in the left hand, as in the right - and up)
	VectorMA( hand->origin, cg_vrGunX.value, aimWorld[0], hand->origin );
	VectorMA( hand->origin, ( h == VR_HAND_LEFT ) ? -cg_vrGunY.value : cg_vrGunY.value, aimWorld[1], hand->origin );
	VectorMA( hand->origin, cg_vrGunZ.value, aimWorld[2], hand->origin );

	VectorCopy( gripWorld, hand->lightingOrigin );
	vectoangles( aimWorld[0], angles );
}

// Your game goes by the barrel rule (bg_vrgun.h): it says so to every server
// ("vrb" in your userinfo) - only one going by it too fires from your barrel.
void CG_VR_Init( void ) {
	trap->Cvar_Register( NULL, "vrb", XSTRING( VR_BARREL_RULE ), CVAR_USERINFO | CVAR_ROM | CVAR_TEMP );
}

// Whether this server fires your shots from your gun's barrel, by the rule
// you go by ("vrGunBarrel" in its serverinfo: an earlier build's, or none -
// from your eye).
void CG_VR_ParseServerinfo( const char *info ) {
	cgs.vrGunBarrel = (qboolean)( atoi( Info_ValueForKey( info, "vrGunBarrel" ) ) == VR_BARREL_RULE );
}

static void CG_VR_BarrelTrace( trace_t *tr, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int pass, int mask ) {
	CG_Trace( tr, start, mins, maxs, end, pass, mask );
}

/*
=================
CG_VR_AddAimDot

A small crosshair floating where your shots will land. The 2D crosshair is
hidden in hand-aim mode because it would sit in the middle of your view, not
where the gun points. Added once the scene's built (cg_view.c, after
CG_VR_BodyEndFrame): other VR players are back where the server has them,
not where their bodies are drawn - as the server's shot meets them.
=================
*/
void CG_VR_AddAimDot( void ) {
	refEntity_t ent;
	trace_t tr;
	vec3_t eye, start, fwd, end, grip, muzzle, barrel;
	float dist;

	if ( !CG_VR_HandAim() || !cg_drawCrosshair.integer || !cg_vrAimDot.integer ) {
		return;
	}

	// Trace the way the server fires: from the player's eye along the
	// usercmd angles (the camera itself may be third person or kicked) -
	// from your gun's muzzle on a server that fires from it, as it finds it:
	// your avatar's gun hand, room all the way (bg_vrgun.h, g_vrgun.c)
	VectorCopy( cg.predictedPlayerState.origin, eye );
	eye[2] += cg.predictedPlayerState.viewheight;
	VectorCopy( eye, start );
	AngleVectors( cg.predictedPlayerState.viewangles, fwd, NULL, NULL );
	if ( cgs.vrGunBarrel && BG_VRBarrelWeapon( cg.predictedPlayerState.weapon ) && !cg.predictedPlayerState.zoomMode &&
		!cg.predictedPlayerState.m_iVehicleNum && !cg.predictedPlayerState.emplacedIndex && CG_VR_AvatarGun( CG_VR_GunHand() ) &&
		CG_VR_HeldWeaponBarrel( cg.predictedPlayerState.weapon, grip, muzzle, barrel ) && DotProduct( barrel, fwd ) >= VR_BARREL_AGREE &&
		BG_VRBarrelClear( CG_VR_BarrelTrace, cg.predictedPlayerState.clientNum, eye, grip, muzzle ) ) {
		VectorCopy( muzzle, start );
	}
	VectorMA( start, 8192.0f, fwd, end );
	CG_Trace( &tr, start, NULL, NULL, end, cg.predictedPlayerState.clientNum, MASK_SHOT );

	dist = Distance( cg.refdef.vieworg, tr.endpos );
	if ( dist < 8.0f ) {
		return;
	}

	memset( &ent, 0, sizeof( ent ) );
	ent.reType = RT_SPRITE;
	VectorMA( tr.endpos, -1.0f, fwd, ent.origin );
	ent.radius = Q_max( 0.25f, dist * 0.008f * cg_vrAimDotSize.value );	// constant size on screen
	ent.customShader = cgs.media.crosshairShader[Com_Clampi( 1, NUM_CROSSHAIRS, cg_drawCrosshair.integer ) - 1];
	ent.shaderRGBA[0] = ent.shaderRGBA[1] = ent.shaderRGBA[2] = ent.shaderRGBA[3] = 255;
	ent.renderfx = RF_NOSHADOW;
	trap->R_AddRefEntityToScene( &ent );
}
