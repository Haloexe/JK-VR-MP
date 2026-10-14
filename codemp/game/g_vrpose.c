/*
===========================================================================
OpenJK VR (multiplayer) - the pose command (the poses: bg_vrpose.c).

  pose sit|cross|kneel|lie   take that pose: sit (on a chair or a bench),
                             sit cross-legged, kneel, lie down
  pose stand                 get up (as moving does); so does asking for the
                             pose you're in

The pose wheel (cg_vrpose.c) sends it; anyone can type it. A pose puts the
sabers away (as the game's meditation does). The server's info says it
takes it ("vrPose").

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "g_local.h"

extern qboolean BG_InKnockDown( int anim );

#define VRPOSE_AGAIN	1000	// ms before another pose

void G_VRPoseInit( void ) {
	trap->Cvar_Register( NULL, "vrPose", "1", CVAR_SERVERINFO | CVAR_ROM );
}

// Why ent can't take a pose now (NULL: they can).
static const char *G_VRPoseRefusal( gentity_t *ent ) {
	const gclient_t *cl = ent->client;
	const playerState_t *ps = &cl->ps;

	if ( ps->pm_type != PM_NORMAL || ent->health < 1 || ps->stats[STAT_HEALTH] < 1 ) {
		return "Not now";
	}
	if ( ps->m_iVehicleNum || ps->emplacedIndex ) {
		return "Not while riding or at a gun";
	}
	if ( ps->groundEntityNum == ENTITYNUM_NONE ) {
		return "Get your feet on the ground first";
	}
	if ( ( ps->pm_flags & PMF_DUCKED ) && !BG_VRPose( ps ) && !BG_VRPosePhase( ps, bgAllAnims[ent->localAnimIndex].anims, NULL, NULL ) ) {
		return "Stand up first";
	}
	if ( ps->saberLockTime >= level.time || ps->forceHandExtend != HANDEXTEND_NONE || ps->weaponTime > 0 ||
		BG_InKnockDown( ps->legsAnim ) || BG_InRoll( (playerState_t *)ps, ps->legsAnim ) ||
		( ps->legsTimer > 0 && !BG_VRPosePhase( ps, bgAllAnims[ent->localAnimIndex].anims, NULL, NULL ) ) ) {
		return "Not now";	// (fighting, Force-pushing, getting up off the floor...)
	}
	return NULL;
}

static void G_VRPoseSay( gentity_t *ent, const char *text ) {
	trap->SendServerCommand( ent-g_entities, va( "cp \"%s\n\"", text ) );
}

// The sabers away, as for the game's meditation.
static void G_VRPoseSabersAway( gentity_t *ent ) {
	gclient_t *cl = ent->client;

	if ( cl->ps.weapon != WP_SABER || cl->ps.saberHolstered >= 2 ) {
		return;
	}
	if ( cl->ps.saberHolstered == 1 && cl->saber[1].model[0] ) {
		G_Sound( ent, CHAN_WEAPON, cl->saber[1].soundOff );	// (the second of two)
	} else if ( cl->ps.saberHolstered == 0 ) {
		G_Sound( ent, CHAN_WEAPON, cl->saber[0].soundOff );
	}
	cl->ps.saberHolstered = 2;
}

// Whether ent is in a pose (or getting down into it or up out of it).
qboolean G_VRPosed( gentity_t *ent ) {
	return (qboolean)( ent && ent->client && ent->s.number < MAX_CLIENTS &&
		BG_VRPosePhase( &ent->client->ps, bgAllAnims[ent->localAnimIndex].anims, NULL, NULL ) != VRPHASE_NONE );
}

// The way a posed body faces, for the game's own model of it (w_saber.c:
// what blades meet): as everyone sees it - a VR player's body as their
// headset turns it, anyone else's the way they faced going down (their
// head looks about, the body doesn't).
void G_VRPoseBodyAngles( gentity_t *ent, vec3_t angles ) {
	float yaw;

	if ( !G_VRPosed( ent ) ) {
		return;
	}
	if ( !G_VRSaberBodyYaw( ent->s.number, &yaw ) ) {
		yaw = ent->client->vrPoseYaw;
	}
	angles[PITCH] = angles[ROLL] = 0.0f;
	angles[YAW] = yaw;
}

void G_VRPoseStandUp( gentity_t *ent ) {
	if ( ent && ent->client && BG_VRPose( &ent->client->ps ) ) {
		BG_VRPoseStandUp( &ent->client->ps, bgAllAnims[ent->localAnimIndex].anims );
	}
}

void Cmd_VRPose_f( gentity_t *ent ) {
	char arg[MAX_TOKEN_CHARS];
	const char *refusal;
	int pose;

	trap->Argv( 1, arg, sizeof( arg ) );
	pose = BG_VRPoseNamed( arg );
	if ( pose == VRPOSE_NONE || pose == BG_VRPose( &ent->client->ps ) ) {
		if ( !pose && arg[0] && Q_stricmp( arg, "stand" ) ) {
			trap->SendServerCommand( ent-g_entities, "print \"usage: pose sit|cross|kneel|lie|stand\n\"" );
			return;
		}
		G_VRPoseStandUp( ent );	// (stand, or the pose they're in again)
		return;
	}
	refusal = G_VRPoseRefusal( ent );
	if ( refusal ) {
		G_VRPoseSay( ent, refusal );
		return;
	}
	if ( ent->client->vrPoseTime && level.time - ent->client->vrPoseTime >= 0 && level.time - ent->client->vrPoseTime < VRPOSE_AGAIN ) {
		return;	// (one a second at most)
	}
	if ( !BG_VRPoseStart( &ent->client->ps, bgAllAnims[ent->localAnimIndex].anims, pose ) ) {
		G_VRPoseSay( ent, "Your character can't do that pose" );
		return;
	}
	ent->client->vrPoseYaw = ent->client->ps.viewangles[YAW];
	ent->client->vrPoseTime = level.time;
	G_VRPoseSabersAway( ent );
}
