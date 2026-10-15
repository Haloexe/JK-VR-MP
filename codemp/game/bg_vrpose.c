/*
===========================================================================
OpenJK VR (multiplayer) - poses: sitting (on a chair or a bench), sitting
cross-legged, kneeling, lying down. A player takes one with the pose
command (the pose wheel sends it; g_vrpose.c checks they can), stays in it
while they keep still, and gets up when they move, jump, crouch, attack,
use the Force or are shoved - getting down into it and up out of it with the game's own
animations. Shared by the game and the cgame, so the player's own game
predicts it exactly as the server runs it.

Which pose a player is in (or getting into) is in their player state's
userInt1..3 (one bit each, all the network carries of them); which part of
it - getting down, in it, getting up - is in their animation. In it their
eyes are lower (viewheight: from standing down to the pose's over the
getting down, and back over the getting up); their box is a crouching one
throughout (no smaller - nothing to hide in - and up again only where
there's room, as from crouching: PM_CheckDuck). They can't fight in one:
attacking gets them up, and the Force (bar healing) waits till they're up
(w_force.c).

DOWN (roleplay, g_vrdown.c): a player whose health runs out is put down by
the server - knocked flat on their back, as the game's own knockdown does -
and stays there whatever they press, until the server gets them up (a hand
from their side, or roleplay going off; giving up ends it in dying). The
same rules keep their own game's prediction still. Getting up out of it is
its own pose (RISING) for as long as the game's getting-up animation runs,
as a knockdown's getting up is the game's own and has no business being a
pose's.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "qcommon/q_shared.h"
#include "bg_public.h"

void BG_SetAnim( playerState_t *ps, animation_t *animations, int setAnimParts, int anim, int setAnimFlags );	// (bg_panimate.c)

#define VRPOSE_PIN		100		// ms: the timers in a pose are kept at this, so nothing else animates the body
#define VRPOSE_SETTLE	600		// ms getting into a pose a model has no getting-down animation for
#define VRPOSE_SHOVE	120.0f	// in one, shoved this fast (units/s): up (a blow, a push)

const bgVRPose_t bgVRPoses[VRPOSE_NUM] = {
	//	name		getting down			in it					getting up				eyes
	{ "",		0,						0,						0,						DEFAULT_VIEWHEIGHT },
	{ "sit",	BOTH_STAND5TOSIT3,		BOTH_SIT1,				BOTH_SIT3TOSTAND5,		18 },	// on a chair or a bench
	{ "cross",	BOTH_STAND5TOSIT2,		BOTH_SIT2,				BOTH_SIT2TOSTAND5,		6 },	// cross-legged, on the floor
	{ "kneel",	BOTH_STAND_TO_KNEEL,	BOTH_STAND_TO_KNEEL,	BOTH_KNEEL_TO_STAND,	20 },	// (its last frame held)
	{ "lie",	BOTH_STAND_TO_KNEEL,	BOTH_SLEEP1,			BOTH_SLEEP1GETUP,		-16 },	// on your back (down by way of kneeling)
	{ "down",	BOTH_KNOCKDOWN1,		BOTH_KNOCKDOWN1,		BOTH_GETUP1,			-16 },	// down in a fight (roleplay): flat on your back (its last frame held)
	{ "",		0,						0,						0,						DEFAULT_VIEWHEIGHT },	// (getting up out of being down: down's getting up)
};

// what gets a player up
#define VRPOSE_UP_BUTTONS	( BUTTON_ATTACK | BUTTON_ALT_ATTACK | BUTTON_FORCEPOWER | BUTTON_FORCEGRIP | \
							  BUTTON_FORCE_LIGHTNING | BUTTON_FORCE_DRAIN )
// what a player who's down can't do (talking, they can)
#define VRPOSE_DOWN_BUTTONS	( VRPOSE_UP_BUTTONS | BUTTON_USE | BUTTON_USE_HOLDABLE | BUTTON_GESTURE )

int BG_VRPose( const playerState_t *ps ) {
	return ( ps->userInt1 & 1 ) | ( ( ps->userInt2 & 1 ) << 1 ) | ( ( ps->userInt3 & 1 ) << 2 );
}

void BG_VRPoseSet( playerState_t *ps, int pose ) {
	ps->userInt1 = pose & 1;
	ps->userInt2 = ( pose >> 1 ) & 1;
	ps->userInt3 = ( pose >> 2 ) & 1;
}

int BG_VRPoseNamed( const char *name ) {
	int i;

	for ( i = VRPOSE_NONE + 1; name && i < VRPOSE_CHOSEN; i++ ) {
		if ( !Q_stricmp( name, bgVRPoses[i].name ) ) {
			return i;
		}
	}
	return VRPOSE_NONE;
}

// How long an animation runs (ms), as a held one's timer is set; 0: this
// model hasn't got it.
static int BG_VRP_AnimTime( const animation_t *animations, int anim ) {
	if ( !animations || anim <= 0 || anim >= MAX_ANIMATIONS || !animations[anim].numFrames ) {
		return 0;
	}
	return animations[anim].numFrames * abs( animations[anim].frameLerp );
}

// how far through (0..1) a timer running down from total to end is
static float BG_VRP_Through( int timer, int total, int end ) {
	if ( total <= end ) {
		return 1.0f;
	}
	return Com_Clamp( 0.0f, 1.0f, 1.0f - (float)( timer - end ) / (float)( total - end ) );
}

int BG_VRPosePhase( const playerState_t *ps, const animation_t *animations, int *poseOut, float *progressOut ) {
	const int pose = BG_VRPose( ps );
	int phase = VRPHASE_NONE, which = VRPOSE_NONE, i;
	float progress = 1.0f;

	if ( pose > VRPOSE_NONE && pose < VRPOSE_NUM && pose != VRPOSE_RISING ) {
		const bgVRPose_t *vp = &bgVRPoses[pose];
		const int downTime = BG_VRP_AnimTime( animations, vp->down );

		if ( ps->legsAnim == vp->down && vp->down != vp->hold && downTime ) {
			phase = VRPHASE_DOWN;
			progress = BG_VRP_Through( ps->legsTimer, downTime, VRPOSE_PIN );
		} else if ( ps->legsAnim == vp->hold ) {
			if ( ps->legsTimer > VRPOSE_PIN ) {
				// still getting into it: the animation it ends on (kneeling),
				// or straight in (a model with no getting-down animation)
				phase = VRPHASE_DOWN;
				progress = BG_VRP_Through( ps->legsTimer,
					( vp->down == vp->hold ) ? BG_VRP_AnimTime( animations, vp->hold ) : VRPOSE_SETTLE, VRPOSE_PIN );
			} else {
				phase = VRPHASE_HELD;
			}
		}
		if ( phase ) {
			which = pose;
		}
	} else if ( ( pose == VRPOSE_NONE || pose == VRPOSE_RISING ) && ps->legsTimer > 0 ) {
		for ( i = VRPOSE_NONE + 1; i < VRPOSE_NUM; i++ ) {
			int upTime;
			if ( i == VRPOSE_RISING || ( i == VRPOSE_DOWNED ) != ( pose == VRPOSE_RISING ) ) {
				continue;	// (out of a pose taken, its own getting up; out of being down, down's)
			}
			upTime = BG_VRP_AnimTime( animations, bgVRPoses[i].up );
			if ( ps->legsAnim == bgVRPoses[i].up && upTime ) {
				phase = VRPHASE_UP;
				which = i;
				progress = BG_VRP_Through( ps->legsTimer, upTime, 0 );
				break;
			}
		}
	}
	if ( poseOut ) {
		*poseOut = which;
	}
	if ( progressOut ) {
		*progressOut = progress;
	}
	return phase;
}

qboolean BG_VRPoseStart( playerState_t *ps, animation_t *animations, int pose ) {
	const bgVRPose_t *vp;

	if ( pose <= VRPOSE_NONE || pose >= VRPOSE_CHOSEN || !BG_VRP_AnimTime( animations, bgVRPoses[pose].hold ) ) {
		return qfalse;	// (this model can't; or no one takes that one themselves)
	}
	vp = &bgVRPoses[pose];
	BG_VRPoseSet( ps, pose );
	if ( vp->down != vp->hold && BG_VRP_AnimTime( animations, vp->down ) > VRPOSE_PIN ) {
		BG_SetAnim( ps, animations, SETANIM_BOTH, vp->down, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD | SETANIM_FLAG_RESTART );
	} else {
		BG_SetAnim( ps, animations, SETANIM_BOTH, vp->hold, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD | SETANIM_FLAG_RESTART );
		if ( vp->down != vp->hold ) {
			ps->legsTimer = ps->torsoTimer = VRPOSE_SETTLE;	// (straight in: settling into it)
		}
	}
	return qtrue;
}

void BG_VRPoseStandUp( playerState_t *ps, animation_t *animations ) {
	const int pose = BG_VRPose( ps );

	if ( pose == VRPOSE_RISING ) {
		return;	// (getting up already)
	}
	BG_VRPoseSet( ps, VRPOSE_NONE );
	ps->pm_flags |= PMF_DUCKED;	// (up only where there's room to stand: PM_CheckDuck)
	if ( pose > VRPOSE_NONE && pose < VRPOSE_NUM && BG_VRP_AnimTime( animations, bgVRPoses[pose].up ) ) {
		if ( pose == VRPOSE_DOWNED ) {
			BG_VRPoseSet( ps, VRPOSE_RISING );	// (the game's getting up off the floor: this one's)
		}
		BG_SetAnim( ps, animations, SETANIM_BOTH, bgVRPoses[pose].up, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD | SETANIM_FLAG_RESTART );
	} else {
		ps->legsTimer = ps->torsoTimer = 0;	// (the game's own animation, at once)
	}
}

// Down, nothing in hand goes off: a charge held (a thermal cooking, a
// bowcaster drawn, a lock taken) is let go of unfired - charged weapons
// fire as the button comes up, and down, it's never down - and a scope's
// put away.
static void BG_VRP_DownHands( playerState_t *ps ) {
	if ( ps->weaponstate == WEAPON_CHARGING || ps->weaponstate == WEAPON_CHARGING_ALT ) {
		ps->weaponstate = WEAPON_READY;
		ps->eFlags &= ~( EF_FIRING | EF_ALT_FIRING );
	}
	if ( ps->rocketLockIndex != ENTITYNUM_NONE || ps->rocketLockTime > 0 || ps->rocketTargetTime ) {
		BG_ClearRocketLock( ps );
	}
	if ( ps->zoomMode ) {
		ps->zoomMode = 0;
		ps->zoomLocked = qfalse;
		ps->zoomTime = ps->commandTime;
	}
}

void BG_VRPoseDown( playerState_t *ps, animation_t *animations ) {
	const bgVRPose_t *vp = &bgVRPoses[VRPOSE_DOWNED];

	BG_VRP_DownHands( ps );
	BG_VRPoseSet( ps, VRPOSE_DOWNED );
	ps->pm_flags |= PMF_DUCKED;
	ps->forceHandExtend = HANDEXTEND_NONE;	// (a knockdown, a choke: the floor now)
	ps->forceHandExtendTime = 0;
	if ( BG_VRP_AnimTime( animations, vp->hold ) && ps->legsAnim != vp->hold ) {
		BG_SetAnim( ps, animations, SETANIM_BOTH, vp->hold, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD | SETANIM_FLAG_RESTART );
	}	// (knocked flat already: as they are; a model without it: down all the same, standing)
}

qboolean BG_VRDowned( const playerState_t *ps ) {
	return (qboolean)( ps->clientNum >= 0 && ps->clientNum < MAX_CLIENTS && BG_VRPose( ps ) == VRPOSE_DOWNED );
}

// Down: there whatever happens, until the server gets them up. Kept still
// - no moving, jumping or crouching, no fighting, switching weapons, using
// things or the Force (the server takes the attack button for giving up
// first), nothing in hand going off - and kept down: what else would take
// the body over (a knockdown, a choke) doesn't, and the body's back down
// if something did.
static qboolean BG_VRP_Downed( playerState_t *ps, usercmd_t *cmd, animation_t *animations ) {
	const bgVRPose_t *vp = &bgVRPoses[VRPOSE_DOWNED];

	if ( ps->pm_type == PM_NORMAL || ps->pm_type == PM_FLOAT ) {	// (alive: a grip letting go - a moment still lifted - too)
		const int phase = BG_VRPosePhase( ps, animations, NULL, NULL );
		ps->forceHandExtend = HANDEXTEND_NONE;
		ps->forceHandExtendTime = 0;
		if ( phase == VRPHASE_HELD ) {
			ps->legsTimer = VRPOSE_PIN;
			if ( ps->torsoAnim != vp->hold && ps->torsoTimer <= 0 ) {
				BG_SetAnim( ps, animations, SETANIM_TORSO, vp->hold, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD );
			}
			if ( ps->torsoAnim == vp->hold ) {
				ps->torsoTimer = VRPOSE_PIN;
			}
		} else if ( phase == VRPHASE_NONE && BG_VRP_AnimTime( animations, vp->hold ) ) {
			BG_SetAnim( ps, animations, SETANIM_BOTH, vp->hold, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD | SETANIM_FLAG_RESTART );
		}	// (going down: it plays)
	}
	BG_VRP_DownHands( ps );
	cmd->forwardmove = cmd->rightmove = cmd->upmove = 0;
	cmd->buttons &= ~VRPOSE_DOWN_BUTTONS;
	cmd->generic_cmd = 0;	// (Force powers, the saber on: the server's, after the move)
	cmd->weapon = ps->weapon;	// (a switch would light a saber: the weapon's put back as it was)
	return qtrue;
}

qboolean BG_VRPoseMove( playerState_t *ps, usercmd_t *cmd, animation_t *animations ) {
	int pose, phase;

	if ( ps->clientNum < 0 || ps->clientNum >= MAX_CLIENTS ) {
		return qfalse;	// (players only)
	}
	if ( BG_VRPose( ps ) == VRPOSE_DOWNED ) {
		return BG_VRP_Downed( ps, cmd, animations );
	}
	phase = BG_VRPosePhase( ps, animations, &pose, NULL );
	if ( !phase ) {
		if ( BG_VRPose( ps ) ) {
			// something else took the body over (knocked down, grabbed, dead)
			BG_VRPoseSet( ps, VRPOSE_NONE );
			ps->pm_flags |= PMF_DUCKED;
		}
		return qfalse;
	}
	if ( ps->pm_type != PM_NORMAL || ps->groundEntityNum == ENTITYNUM_NONE || ps->m_iVehicleNum || ps->fd.forceGripCripple ) {
		// off the ground (pushed off a ledge), dead, on a vehicle, in someone's
		// grip: the game's own animations at once
		BG_VRPoseSet( ps, VRPOSE_NONE );
		ps->pm_flags |= PMF_DUCKED;
		ps->legsTimer = ps->torsoTimer = 0;
		return qfalse;
	}
	if ( phase == VRPHASE_DOWN || phase == VRPHASE_HELD ) {
		const bgVRPose_t *vp = &bgVRPoses[pose];
		const float shoved = ( phase == VRPHASE_HELD ) ? ps->velocity[0] * ps->velocity[0] + ps->velocity[1] * ps->velocity[1] : 0.0f;
		// (healing, the Force button keeps you there: w_force.c lets you)
		const int up = VRPOSE_UP_BUTTONS & ~( ( ps->fd.forcePowerSelected == FP_HEAL ) ? BUTTON_FORCEPOWER : 0 );
		// (a saber lit some other way - togglesaber - and a VR one swung)
		const qboolean lit = (qboolean)( ps->weapon == WP_SABER && ps->saberHolstered < 2 );
		if ( cmd->forwardmove || cmd->rightmove || cmd->upmove || ( cmd->buttons & up ) || lit ||
			shoved > VRPOSE_SHOVE * VRPOSE_SHOVE ) {
			BG_VRPoseStandUp( ps, animations );
			if ( !BG_VRPosePhase( ps, animations, NULL, NULL ) ) {
				return qfalse;	// (up at once, no getting-up animation: off they go)
			}
		} else {
			if ( phase == VRPHASE_DOWN && ps->legsAnim == vp->down && vp->down != vp->hold && ps->legsTimer <= VRPOSE_PIN ) {
				// down: into it
				BG_SetAnim( ps, animations, SETANIM_BOTH, vp->hold, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD );
				phase = VRPHASE_HELD;
			}
			if ( phase == VRPHASE_HELD ) {
				// in it as long as they keep still; the body too, once a flinch
				// (or a weapon change) is over
				ps->legsTimer = VRPOSE_PIN;
				if ( ps->torsoAnim != vp->hold && ps->torsoTimer <= 0 ) {
					BG_SetAnim( ps, animations, SETANIM_TORSO, vp->hold, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD );
				}
				if ( ps->torsoAnim == vp->hold ) {
					ps->torsoTimer = VRPOSE_PIN;
				}
			}
		}
	}
	// still while getting down, in it, or getting up
	cmd->forwardmove = cmd->rightmove = cmd->upmove = 0;
	cmd->buttons &= ~VRPOSE_UP_BUTTONS;
	return qtrue;
}

qboolean BG_VRPoseHeights( const playerState_t *ps, const animation_t *animations, int *viewheight ) {
	int pose;
	float t, eye;
	const int phase = BG_VRPosePhase( ps, animations, &pose, &t );
	const bgVRPose_t *vp;

	if ( !phase ) {
		return qfalse;
	}
	vp = &bgVRPoses[pose];
	t = t * t * ( 3.0f - 2.0f * t );	// (eased in and out)
	if ( phase == VRPHASE_UP ) {
		eye = vp->eye + ( DEFAULT_VIEWHEIGHT - vp->eye ) * t;
	} else {
		eye = DEFAULT_VIEWHEIGHT + ( vp->eye - DEFAULT_VIEWHEIGHT ) * t;
	}
	*viewheight = (int)floorf( eye + 0.5f );
	return qtrue;
}

// (a pose taken: down's are the game's own knockdown's)
qboolean BG_VRPoseAnim( int anim ) {
	int i;

	for ( i = VRPOSE_NONE + 1; i < VRPOSE_CHOSEN; i++ ) {
		if ( anim == bgVRPoses[i].down || anim == bgVRPoses[i].hold || anim == bgVRPoses[i].up ) {
			return qtrue;
		}
	}
	return qfalse;
}
