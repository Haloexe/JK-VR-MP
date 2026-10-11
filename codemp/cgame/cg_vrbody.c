/*
===========================================================================
OpenJK VR (multiplayer) - your own body in first person.

While you play in a headset with a gun out, your character's model is drawn
around you: it stands under your head, turns with you, and its arms reach
for your controllers. Nothing here is tied to one character. Every frame the
model's own skeleton is measured (shoulders, elbows, wrists, knuckles, eyes),
so any model built on the Jedi Academy humanoid skeleton - the stock
characters and custom models alike - gets arms that fit its proportions.

How the arms work: the game animates the model as usual, then the upper arm
and forearm bones are re-aimed with Ghoul2 "post-multiply" bone overrides
(an extra rotation in the bone's own frame) so the hand lands on the
controller. Hand and fingers hang off the forearm in this skeleton, so the
forearm is also twisted to turn the hand the way the controller is turned,
while the fingers keep playing the game's grip animation.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "cg_local.h"
#include "ghoul2/G2.h"

extern qboolean BG_GetRootSurfNameWithVariant( void *ghoul2, const char *rootSurfName, char *returnSurfName, int returnSize );

#define VRB_TURN_OFF		0x00000100	// G2SURFACEFLAG_NODESCENDANTS: surface and its children off
#define VRB_HEAD_BIT		( 1 << ( G2_MODELPART_HEAD - 10 ) )

enum {
	VRB_UPPER,		// upper arm (shoulder joint)
	VRB_FORE,		// forearm (elbow joint)
	VRB_HAND,		// hand (wrist joint)
	VRB_THUMB,		// thumb base
	VRB_INDEX,		// index finger knuckle
	VRB_LITTLE,		// ring/little finger knuckle
	VRB_NUM_ARM_BONES
};

static const char *const vrbArmBones[2][VRB_NUM_ARM_BONES] = {
	{ "lhumerus", "lradius", "lhand", "l_d1_j1", "l_d2_j1", "l_d4_j1" },	// VR_HAND_LEFT
	{ "rhumerus", "rradius", "rhand", "r_d1_j1", "r_d2_j1", "r_d4_j1" },	// VR_HAND_RIGHT
};

// G2API_GetBoltMatrix hands game code its axes relabelled ("this 90 degree
// offset thing", G2_API.cpp). The overrides need the bone's real axes. The
// relabelling is undone one way below; the arm solver also checks its own
// result against the skeleton and settles on whichever of these makes the
// hand land where it was sent (see CG_VRB_Arms), so a wrong guess here can
// only cost a few frames, never leave the arms twisted.
#define VRB_NUM_CONVENTIONS	3

// how strongly elbows are pulled down/out, against following the hand's turn
static float vrbElbowPull = 0.3f;

typedef float vrbMat_t[3][3];		// rotation, row-major, acts on column vectors

typedef struct {
	vec3_t		origin;
	vec3_t		axis[3];			// the bone's own axes in the world
} vrbBone_t;

typedef struct {
	qboolean	solved;
	qboolean	exact;				// well-bent arm in reach: the check below is meaningful
	vec3_t		elbow, wrist;		// where the solution puts them
} vrbArmResult_t;

static struct {
	qboolean	init;

	// body yaw, kept in VR tracking space so snap/smooth turning carries the
	// body along with the view
	float		yawT;
	float		worldYaw;
	int			yawTime;
	int			lastActive;

	// what was changed on which Ghoul2 instance, to undo it
	void		*armsG2;
	void		*headG2;
	char		headSurf[MAX_QPATH];
	void		*weaponG2;

	// where the entity was moved this frame (undone in CG_VR_BodyEndFrame)
	centity_t	*shiftedEnt;
	vec3_t		shift;

	// per arm
	vec3_t		pole[2];			// smoothed elbow direction, body-local
	qboolean	poleValid[2];
	int			poleTime[2];		// cg.time it was last advanced
	vec3_t		hinge[2];			// elbow hinge axis in the upper arm's frame
	qboolean	hingeValid[2];
	int			hingeConvention[2];

	// bone axis convention (see above); -1 while still checking
	int			convention;
	int			votes[VRB_NUM_CONVENTIONS];
	int			checks;
	int			checkTime;			// the eyes run the cgame twice per frame: count once
	int			lastBest;
	int			nextCheck;
	int			badChecks;
} vrb;

static void CG_VRB_Init( void ) {
	if ( !vrb.init ) {
		memset( &vrb, 0, sizeof( vrb ) );
		vrb.convention = -1;
		vrb.init = qtrue;
	}
}

/*
=============================================================================

small rotation helpers

=============================================================================
*/

static void VRB_Apply( vrbMat_t q, const vec3_t v, vec3_t out ) {
	vec3_t t;
	t[0] = q[0][0] * v[0] + q[0][1] * v[1] + q[0][2] * v[2];
	t[1] = q[1][0] * v[0] + q[1][1] * v[1] + q[1][2] * v[2];
	t[2] = q[2][0] * v[0] + q[2][1] * v[1] + q[2][2] * v[2];
	VectorCopy( t, out );
}

// out = a * b (b is applied first)
static void VRB_Mul( vrbMat_t a, vrbMat_t b, vrbMat_t out ) {
	vrbMat_t t;
	int r, c;
	for ( r = 0; r < 3; r++ ) {
		for ( c = 0; c < 3; c++ ) {
			t[r][c] = a[r][0] * b[0][c] + a[r][1] * b[1][c] + a[r][2] * b[2][c];
		}
	}
	memcpy( out, t, sizeof( t ) );
}

// the rotation taking orthonormal frame "from" onto "to" (from[i] -> to[i])
static void VRB_FrameMap( vec3_t from[3], vec3_t to[3], vrbMat_t q ) {
	int r, c;
	for ( r = 0; r < 3; r++ ) {
		for ( c = 0; c < 3; c++ ) {
			q[r][c] = to[0][r] * from[0][c] + to[1][r] * from[1][c] + to[2][r] * from[2][c];
		}
	}
}

// shortest rotation taking unit vector a onto unit vector b
static void VRB_Arc( const vec3_t a, const vec3_t b, vrbMat_t q ) {
	vec3_t v;
	const float c = DotProduct( a, b );
	int i;

	if ( c < -0.9999f ) {
		// half turn about any axis perpendicular to a
		vec3_t p;
		PerpendicularVector( p, a );
		for ( i = 0; i < 3; i++ ) {
			q[i][0] = 2.0f * p[i] * p[0];
			q[i][1] = 2.0f * p[i] * p[1];
			q[i][2] = 2.0f * p[i] * p[2];
			q[i][i] -= 1.0f;
		}
		return;
	}

	// Rodrigues with K = [a x b]: q = I + K + K^2 / (1 + c)
	CrossProduct( a, b, v );
	{
		const float k = 1.0f / ( 1.0f + c );
		q[0][0] = 1.0f - k * ( v[1] * v[1] + v[2] * v[2] );
		q[1][1] = 1.0f - k * ( v[0] * v[0] + v[2] * v[2] );
		q[2][2] = 1.0f - k * ( v[0] * v[0] + v[1] * v[1] );
		q[0][1] = -v[2] + k * v[0] * v[1];
		q[1][0] =  v[2] + k * v[0] * v[1];
		q[0][2] =  v[1] + k * v[0] * v[2];
		q[2][0] = -v[1] + k * v[0] * v[2];
		q[1][2] = -v[0] + k * v[1] * v[2];
		q[2][1] =  v[0] + k * v[1] * v[2];
	}
}

static void VRB_RotateFrame( vrbMat_t q, vec3_t in[3], vec3_t out[3] ) {
	VRB_Apply( q, in[0], out[0] );
	VRB_Apply( q, in[1], out[1] );
	VRB_Apply( q, in[2], out[2] );
}

// Makes a frame orthonormal without changing its handedness (a mirrored
// bone stays mirrored, so the override maths below stays exact).
static qboolean VRB_Orthonormalize( vec3_t a[3] ) {
	vec3_t c;
	float hand;

	CrossProduct( a[0], a[1], c );
	hand = DotProduct( c, a[2] );
	if ( VectorNormalize( a[0] ) < 1e-6f ) {
		return qfalse;
	}
	VectorMA( a[1], -DotProduct( a[1], a[0] ), a[0], a[1] );
	if ( VectorNormalize( a[1] ) < 1e-6f ) {
		return qfalse;
	}
	CrossProduct( a[0], a[1], a[2] );
	if ( hand < 0.0f ) {
		VectorScale( a[2], -1.0f, a[2] );
	}
	return qtrue;
}

// Inverse of AnglesToAxis, including straight up/down.
static void VRB_AxisToAngles( vec3_t a[3], vec3_t angles ) {
	const float len = sqrtf( a[0][0] * a[0][0] + a[0][1] * a[0][1] );

	if ( len > 1e-5f ) {
		angles[PITCH] = RAD2DEG( atan2f( -a[0][2], len ) );
		angles[YAW] = RAD2DEG( atan2f( a[0][1], a[0][0] ) );
		angles[ROLL] = RAD2DEG( atan2f( a[1][2], a[2][2] ) );
	} else {
		// yaw and roll are the same axis here: put it all in roll (yaw 0)
		const float sp = ( a[0][2] < 0.0f ) ? 1.0f : -1.0f;
		angles[PITCH] = sp * 90.0f;
		angles[YAW] = 0.0f;
		angles[ROLL] = RAD2DEG( atan2f( a[1][0] * sp, a[1][1] ) );
	}
}

static float VRB_SmoothStep( float lo, float hi, float x ) {
	const float t = Com_Clamp( 0.0f, 1.0f, ( x - lo ) / ( hi - lo ) );
	return t * t * ( 3.0f - 2.0f * t );
}

/*
=============================================================================

skeleton access

=============================================================================
*/

// A bone (or tag) of the player's model in the world. G2API_AddBolt returns
// the existing bolt when there is one, so asking every frame never leaves a
// stale index behind when the model changes.
static qboolean CG_VRB_Bone( centity_t *cent, const char *name, int convention, vrbBone_t *out ) {
	mdxaBone_t m;
	vec3_t c[3];
	int bolt, i;

	bolt = trap->G2API_AddBolt( cent->ghoul2, 0, name );
	if ( bolt < 0 ) {
		return qfalse;
	}
	if ( !trap->G2API_GetBoltMatrix( cent->ghoul2, 0, bolt, &m, cent->turAngles, cent->lerpOrigin,
			cg.time, cgs.gameModels, cent->modelScale ) ) {
		return qfalse;
	}
	for ( i = 0; i < 3; i++ ) {
		out->origin[i] = m.matrix[i][3];
		c[0][i] = m.matrix[i][0];
		c[1][i] = m.matrix[i][1];
		c[2][i] = m.matrix[i][2];
	}

	switch ( convention ) {
	default:
	case 0:	// undo GetBoltMatrix's relabelling (new x = -old y, new y = old x)
		VectorCopy( c[1], out->axis[0] );
		VectorScale( c[0], -1.0f, out->axis[1] );
		VectorCopy( c[2], out->axis[2] );
		break;
	case 1:	// as returned
		VectorCopy( c[0], out->axis[0] );
		VectorCopy( c[1], out->axis[1] );
		VectorCopy( c[2], out->axis[2] );
		break;
	case 2:	// the other way round
		VectorScale( c[1], -1.0f, out->axis[0] );
		VectorCopy( c[0], out->axis[1] );
		VectorCopy( c[2], out->axis[2] );
		break;
	}
	return VRB_Orthonormalize( out->axis );
}

// Sets a bone's post-multiply override so its axes become "want", given the
// axes "cur" it would have without the override (both world space).
static void CG_VRB_SetBone( void *g2, const char *bone, vec3_t cur[3], vec3_t want[3] ) {
	vec3_t r[3], angles;
	int i, k;

	// the override R works in the bone's own frame: want[i] = sum_k R[i][k] cur[k]
	for ( i = 0; i < 3; i++ ) {
		for ( k = 0; k < 3; k++ ) {
			r[i][k] = DotProduct( want[i], cur[k] );
		}
	}
	VRB_AxisToAngles( r, angles );
	// up = +Z, left = -Y, forward = +X: Ghoul2 builds exactly AnglesToAxis(angles)
	trap->G2API_SetBoneAngles( g2, 0, bone, angles, BONE_ANGLES_POSTMULT,
		POSITIVE_Z, NEGATIVE_Y, POSITIVE_X, NULL, 0, cg.time );
}

static void CG_VRB_ResetArms( void *g2 ) {
	static const vec3_t zero = { 0.0f, 0.0f, 0.0f };
	int h;

	for ( h = 0; h < 2; h++ ) {
		trap->G2API_SetBoneAngles( g2, 0, vrbArmBones[h][VRB_UPPER], zero, BONE_ANGLES_POSTMULT,
			POSITIVE_Z, NEGATIVE_Y, POSITIVE_X, NULL, 0, cg.time );
		trap->G2API_SetBoneAngles( g2, 0, vrbArmBones[h][VRB_FORE], zero, BONE_ANGLES_POSTMULT,
			POSITIVE_Z, NEGATIVE_Y, POSITIVE_X, NULL, 0, cg.time );
	}
}

/*
=============================================================================

when, and which way the body faces

=============================================================================
*/

qboolean CG_VR_BodyActive( centity_t *cent ) {
	const playerState_t *ps = &cg.predictedPlayerState;

	if ( !cg_vrBody.integer || !CG_VR() || !cg.snap ) {
		return qfalse;
	}
	if ( !CG_VR_HandAim() ) {
		// no gun in the VR hand (duel modes, a demo, vr_handAim 0, right
		// controller lost): the stock first-person view
		return qfalse;
	}
	if ( cent->currentState.number != ps->clientNum || ( cg.snap->ps.pm_flags & PMF_FOLLOW ) ) {
		return qfalse;	// someone else, or watching someone else
	}
	if ( cg.renderingThirdPerson || ps->pm_type == PM_SPECTATOR || ps->pm_type == PM_INTERMISSION ||
		ps->persistant[PERS_TEAM] == TEAM_SPECTATOR || ps->zoomMode ) {
		return qfalse;
	}
	if ( !cent->ghoul2 || cent->localAnimIndex > 1 ) {
		return qfalse;	// not a humanoid model
	}
	if ( ( cent->currentState.eFlags & ( EF_DEAD | EF_RAG | EF_NODRAW ) ) || cent->isRagging ||
		( cent->currentState.eFlags2 & EF2_HELD_BY_MONSTER ) || cent->currentState.heldByClient ||
		cent->currentState.m_iVehicleNum || cent->currentState.weapon == WP_EMPLACED_GUN ||
		cent->currentState.weapon == WP_SABER ) {
		return qfalse;	// the game's own posing (or the stock saber view) wins
	}
	return qtrue;
}

// The torso turns with your head, but lazily: it only has to follow once
// you look more than cg_vrBodyTwist degrees away, and drifts after your
// head otherwise (quickly while you move).
static float CG_VRB_BodyYaw( const vrClientState_t *vr ) {
	vec3_t head[3];
	float headYaw, diff, maxTwist, rate, dt;
	const float speed = sqrtf( cg.predictedPlayerState.velocity[0] * cg.predictedPlayerState.velocity[0] +
		cg.predictedPlayerState.velocity[1] * cg.predictedPlayerState.velocity[1] );

	if ( vrb.yawTime == cg.time ) {
		return vrb.worldYaw;
	}

	VectorCopy( vr->headAxis[0], head[0] );
	VectorCopy( vr->headAxis[1], head[1] );
	VectorCopy( vr->headAxis[2], head[2] );
	headYaw = VR_AxisYaw( head );

	if ( !vrb.yawTime || cg.time < vrb.lastActive || cg.time - vrb.lastActive > 500 ) {
		vrb.yawT = headYaw;		// (re)starting: face where you look
	}
	dt = Com_Clamp( 0.0f, 0.1f, ( cg.time - vrb.yawTime ) * 0.001f );

	maxTwist = Com_Clamp( 0.0f, 90.0f, cg_vrBodyTwist.value );
	diff = AngleSubtract( headYaw, vrb.yawT );
	if ( diff > maxTwist ) {
		vrb.yawT = headYaw - maxTwist;
	} else if ( diff < -maxTwist ) {
		vrb.yawT = headYaw + maxTwist;
	}
	rate = ( speed > 20.0f ) ? 6.0f : 1.0f;
	vrb.yawT = AngleNormalize360( vrb.yawT + AngleSubtract( headYaw, vrb.yawT ) * ( 1.0f - expf( -rate * dt ) ) );

	// tracking space -> world, exactly as the renderer turns the view
	vrb.worldYaw = AngleNormalize360( vrb.yawT + VR_AxisYaw( cg.refdef.viewaxis ) - vr->referenceYaw );
	vrb.yawTime = cg.time;
	vrb.lastActive = cg.time;
	return vrb.worldYaw;
}

qboolean CG_VR_BodyAngles( centity_t *cent, vec3_t angles ) {
	const vrClientState_t *vr;

	CG_VRB_Init();
	if ( !CG_VR_BodyActive( cent ) || !( vr = CG_VR() ) ) {
		return qfalse;
	}
	// level torso (pitch 0): aiming up or down moves the arms, not the spine
	VectorSet( angles, 0.0f, CG_VRB_BodyYaw( vr ), 0.0f );
	return qtrue;
}

/*
=============================================================================

arms

=============================================================================
*/

/*
=================
CG_VRB_SolveArm

Two-bone IK for one arm, in the world. Reads the arm as the game animated it
(overrides reset), works out where the elbow and wrist go, and sets the
upper arm and forearm overrides. "commit" keeps the smoothing/hinge state
(only the pass that is finally drawn does).
=================
*/
static qboolean CG_VRB_SolveArm( centity_t *cent, const vrClientState_t *vr, int h, int convention,
	float bodyYaw, qboolean commit, vrbArmResult_t *res ) {
	vrbBone_t b[VRB_NUM_ARM_BONES];
	vec3_t gripLocal, gripAxisLocal[3], grip, gA[3];
	vec3_t S, E0, W0, kmid, Ah[3], Ad[3], P, Wt, u, v, E, u0, u1, f0, f1, n0, n1, c0, c1, tmp;
	vec3_t bodyFwd, bodyLeft, outward, hint, down = { 0.0f, 0.0f, -1.0f };
	vec3_t F0[3], F1[3], want[3], cur[3];
	vrbMat_t Y, Q1, A, X;
	float a, len2, d, dmin, dmax, cosA, sinA, w, pitch;
	qboolean haveHinge = qfalse, haveFingers = qtrue;
	int i;

	memset( res, 0, sizeof( *res ) );

	for ( i = 0; i < VRB_NUM_ARM_BONES; i++ ) {
		if ( !CG_VRB_Bone( cent, vrbArmBones[h][i], convention, &b[i] ) ) {
			if ( i <= VRB_HAND ) {
				return qfalse;	// this skeleton has no such arm
			}
			haveFingers = qfalse;	// can still reach, just not turn the hand
		}
	}
	VectorCopy( b[VRB_UPPER].origin, S );
	VectorCopy( b[VRB_FORE].origin, E0 );
	VectorCopy( b[VRB_HAND].origin, W0 );
	a = Distance( S, E0 );
	len2 = Distance( E0, W0 );
	if ( a < 1.0f || len2 < 1.0f ) {
		return qfalse;
	}

	// the controller in the world
	VectorCopy( vr->gripOrigin[h], gripLocal );
	for ( i = 0; i < 3; i++ ) {
		VectorCopy( vr->gripAxis[h][i], gripAxisLocal[i] );
	}
	VR_ToWorld( vr, cg.refdef.vieworg, cg.refdef.viewaxis, gripLocal, gripAxisLocal, grip, gA );
	VectorMA( grip, cg_vrHandX.value, gA[0], grip );
	VectorMA( grip, ( h == VR_HAND_RIGHT ) ? cg_vrHandY.value : -cg_vrHandY.value, gA[1], grip );
	VectorMA( grip, cg_vrHandZ.value, gA[2], grip );

	// The hand as animated, by anatomy: wrist -> knuckles, and across the
	// knuckles from the little finger to the index finger (the thumb is on
	// that side; if a model has them the other way round, flip).
	if ( haveFingers ) {
		VectorAdd( b[VRB_INDEX].origin, b[VRB_LITTLE].origin, kmid );
		VectorScale( kmid, 0.5f, kmid );
		VectorSubtract( kmid, W0, Ah[0] );
		VectorSubtract( b[VRB_INDEX].origin, b[VRB_LITTLE].origin, Ah[1] );
		if ( VectorNormalize( Ah[0] ) < 0.01f ) {
			haveFingers = qfalse;
		} else {
			VectorMA( Ah[1], -DotProduct( Ah[1], Ah[0] ), Ah[0], Ah[1] );
			if ( VectorNormalize( Ah[1] ) < 0.01f ) {
				haveFingers = qfalse;
			}
		}
	}

	if ( haveFingers ) {
		VectorSubtract( b[VRB_THUMB].origin, kmid, tmp );
		if ( DotProduct( tmp, Ah[1] ) < 0.0f ) {
			VectorScale( Ah[1], -1.0f, Ah[1] );
		}
		CrossProduct( Ah[0], Ah[1], Ah[2] );

		// The same directions for a hand holding the controller (OpenXR grip
		// pose: its -Z runs through the fist from the little finger to the
		// thumb, +Y points back toward the wrist). Quake axes: gA[0] = -Z,
		// gA[2] = +Y.
		VectorScale( gA[2], -1.0f, Ad[0] );
		VectorCopy( gA[0], Ad[1] );
		pitch = cg_vrHandPitch.value;
		if ( pitch != 0.0f ) {
			RotatePointAroundVector( tmp, Ad[1], Ad[0], -pitch );
			VectorCopy( tmp, Ad[0] );
		}
		CrossProduct( Ad[0], Ad[1], Ad[2] );

		// Y turns the animated hand into the controller's orientation
		VRB_FrameMap( Ah, Ad, Y );

		// wrist target: the middle of the hand on the controller's grip
		VectorSubtract( kmid, W0, tmp );
		VectorScale( tmp, 0.5f, P );
		VRB_Apply( Y, P, P );
		VectorSubtract( grip, P, Wt );
	} else {
		// no finger bones: the wrist goes to the grip, the hand keeps its
		// animated turn on the forearm
		memset( Y, 0, sizeof( Y ) );
		Y[0][0] = Y[1][1] = Y[2][2] = 1.0f;
		VectorCopy( grip, Wt );
	}

	// reach: shoulder -> wrist distance the arm can actually make
	VectorSubtract( Wt, S, u );
	d = VectorNormalize( u );
	if ( d < 0.01f ) {
		return qfalse;
	}
	dmin = fabsf( a - len2 ) + 0.02f * ( a + len2 );
	dmax = 0.995f * ( a + len2 );
	d = Com_Clamp( dmin, dmax, d );
	VectorMA( S, d, u, Wt );
	cosA = Com_Clamp( -1.0f, 1.0f, ( a * a + d * d - len2 * len2 ) / ( 2.0f * a * d ) );
	sinA = sqrtf( 1.0f - cosA * cosA );

	// Which way the elbow points: where the forearm would be if the wrist
	// kept its animated bend, plus a pull down and out (elbows hang).
	VectorSet( bodyFwd, cosf( DEG2RAD( bodyYaw ) ), sinf( DEG2RAD( bodyYaw ) ), 0.0f );
	VectorSet( bodyLeft, -bodyFwd[1], bodyFwd[0], 0.0f );
	VectorScale( bodyLeft, ( h == VR_HAND_RIGHT ) ? -1.0f : 1.0f, outward );
	VectorSubtract( W0, E0, f0 );
	VectorNormalize( f0 );
	VRB_Apply( Y, f0, tmp );
	VectorMA( Wt, -len2, tmp, E );		// "natural" elbow
	VectorSubtract( E, S, hint );
	VectorNormalize( hint );
	VectorMA( down, 0.5f, outward, tmp );
	VectorNormalize( tmp );
	VectorMA( hint, vrbElbowPull, tmp, hint );
	VectorMA( hint, -DotProduct( hint, u ), u, v );
	if ( VectorLength( v ) < 1e-3f ) {
		VectorMA( down, -DotProduct( down, u ), u, v );
		if ( VectorLength( v ) < 1e-3f ) {
			VectorMA( outward, -DotProduct( outward, u ), u, v );
		}
	}
	VectorNormalize( v );

	// smooth the elbow direction a little (body-local, so turning doesn't drag it)
	{
		vec3_t local, prev;
		// (advances with time, not per call: both eyes get the same elbow)
		const float k = 1.0f - expf( -Com_Clamp( 0.0f, 0.1f, ( cg.time - vrb.poleTime[h] ) * 0.001f ) / 0.05f );
		local[0] = DotProduct( v, bodyFwd );
		local[1] = DotProduct( v, bodyLeft );
		local[2] = v[2];
		if ( vrb.poleValid[h] ) {
			VectorScale( vrb.pole[h], 1.0f - k, prev );
			VectorMA( prev, k, local, prev );
			if ( VectorNormalize( prev ) > 0.1f ) {
				vec3_t world;
				VectorScale( bodyFwd, prev[0], world );
				VectorMA( world, prev[1], bodyLeft, world );
				world[2] += prev[2];
				VectorMA( world, -DotProduct( world, u ), u, world );
				if ( VectorNormalize( world ) > 0.1f ) {
					VectorCopy( world, v );
					local[0] = DotProduct( v, bodyFwd );
					local[1] = DotProduct( v, bodyLeft );
					local[2] = v[2];
				}
			}
		}
		if ( commit ) {
			VectorCopy( local, vrb.pole[h] );
			vrb.poleValid[h] = qtrue;
			vrb.poleTime[h] = cg.time;
		}
	}

	VectorScale( u, a * cosA, E );
	VectorMA( E, a * sinA, v, E );
	VectorAdd( S, E, E );

	VectorSubtract( E, S, u1 );
	VectorNormalize( u1 );
	VectorSubtract( Wt, E, f1 );
	VectorNormalize( f1 );
	VectorSubtract( E0, S, u0 );
	VectorNormalize( u0 );

	// Upper arm: swing onto the new direction, rolled so the elbow's hinge
	// lies across the new bend (the elbow then bends the anatomical way).
	CrossProduct( u0, f0, c0 );
	if ( VectorLength( c0 ) > 0.25f ) {
		VectorNormalize( c0 );
		VectorCopy( c0, n0 );
		haveHinge = qtrue;
		if ( commit ) {
			for ( i = 0; i < 3; i++ ) {
				vrb.hinge[h][i] = DotProduct( n0, b[VRB_UPPER].axis[i] );
			}
			vrb.hingeValid[h] = qtrue;
			vrb.hingeConvention[h] = convention;
		}
	} else if ( vrb.hingeValid[h] && vrb.hingeConvention[h] == convention ) {
		// arm hangs straight in this animation: use the hinge seen before
		VectorScale( b[VRB_UPPER].axis[0], vrb.hinge[h][0], n0 );
		VectorMA( n0, vrb.hinge[h][1], b[VRB_UPPER].axis[1], n0 );
		VectorMA( n0, vrb.hinge[h][2], b[VRB_UPPER].axis[2], n0 );
		VectorMA( n0, -DotProduct( n0, u0 ), u0, n0 );
		haveHinge = ( VectorNormalize( n0 ) > 0.1f ) ? qtrue : qfalse;
	}

	VRB_Arc( u0, u1, A );
	if ( haveHinge ) {
		VRB_Apply( A, n0, n1 );				// keep the roll...
		CrossProduct( u1, f1, c1 );
		w = VRB_SmoothStep( 0.05f, 0.25f, VectorLength( c1 ) );
		if ( w > 0.0f ) {					// ...unless the new bend says otherwise
			VectorNormalize( c1 );
			VectorScale( n1, 1.0f - w, n1 );
			VectorMA( n1, w, c1, n1 );
			if ( VectorNormalize( n1 ) < 0.1f ) {
				VectorCopy( c1, n1 );
			}
		}
		VectorMA( n1, -DotProduct( n1, u1 ), u1, n1 );
		VectorNormalize( n1 );
		VectorCopy( u0, F0[0] );
		VectorCopy( n0, F0[1] );
		CrossProduct( u0, n0, F0[2] );
		VectorCopy( u1, F1[0] );
		VectorCopy( n1, F1[1] );
		CrossProduct( u1, n1, F1[2] );
		VRB_FrameMap( F0, F1, Q1 );
	} else {
		memcpy( Q1, A, sizeof( Q1 ) );
	}

	// Forearm (hand and fingers ride on it): the controller's orientation,
	// corrected by the least turn that points the forearm at the wrist.
	VRB_Apply( Y, f0, tmp );
	VRB_Arc( tmp, f1, A );
	VRB_Mul( A, Y, X );

	// overrides: upper arm first, then the forearm relative to its new parent
	VRB_RotateFrame( Q1, b[VRB_UPPER].axis, want );
	CG_VRB_SetBone( cent->ghoul2, vrbArmBones[h][VRB_UPPER], b[VRB_UPPER].axis, want );
	VRB_RotateFrame( Q1, b[VRB_FORE].axis, cur );
	VRB_RotateFrame( X, b[VRB_FORE].axis, want );
	CG_VRB_SetBone( cent->ghoul2, vrbArmBones[h][VRB_FORE], cur, want );

	res->solved = qtrue;
	res->exact = ( sinA > 0.3f ) ? qtrue : qfalse;
	VectorCopy( E, res->elbow );
	VectorCopy( Wt, res->wrist );
	return qtrue;
}

// How far the drawn elbows/wrists ended up from where they were sent.
static float CG_VRB_ArmError( centity_t *cent, vrbArmResult_t res[2], qboolean *meaningful ) {
	vrbBone_t b;
	float err = 0.0f;
	int h;

	*meaningful = qfalse;
	for ( h = 0; h < 2; h++ ) {
		if ( !res[h].solved ) {
			continue;
		}
		if ( CG_VRB_Bone( cent, vrbArmBones[h][VRB_FORE], 1, &b ) ) {
			err = Q_max( err, Distance( b.origin, res[h].elbow ) );
		}
		if ( CG_VRB_Bone( cent, vrbArmBones[h][VRB_HAND], 1, &b ) ) {
			err = Q_max( err, Distance( b.origin, res[h].wrist ) );
		}
		if ( res[h].exact ) {
			*meaningful = qtrue;
		}
	}
	return err;
}

static int CG_VRB_SolveArms( centity_t *cent, const vrClientState_t *vr, int convention, float bodyYaw,
	qboolean commit, vrbArmResult_t res[2] ) {
	int h, n = 0;

	CG_VRB_ResetArms( cent->ghoul2 );
	for ( h = 0; h < 2; h++ ) {
		memset( &res[h], 0, sizeof( res[h] ) );
		if ( vr->handValid[h] && CG_VRB_SolveArm( cent, vr, h, convention, bodyYaw, commit, &res[h] ) ) {
			n++;
		}
	}
	return n;
}

/*
=================
CG_VRB_Arms

Solves both arms. Until the bone axis convention is known (the first
moments in VR), each candidate is tried and the one whose result actually
lands the hands on target is drawn; once one clearly wins a few times it is
used from then on, with an occasional re-check.
=================
*/
static void CG_VRB_Arms( centity_t *cent, const vrClientState_t *vr, float bodyYaw ) {
	vrbArmResult_t res[2];
	qboolean meaningful;
	float err[VRB_NUM_CONVENTIONS];
	int c, best;

	vrb.armsG2 = cent->ghoul2;

	if ( vrb.convention < 0 && vrb.checkTime != cg.time ) {
		vrb.checkTime = cg.time;
		best = 0;
		for ( c = 0; c < VRB_NUM_CONVENTIONS; c++ ) {
			if ( !CG_VRB_SolveArms( cent, vr, c, bodyYaw, qfalse, res ) ) {
				CG_VRB_ResetArms( cent->ghoul2 );
				return;		// no hands tracked / no arm bones
			}
			err[c] = CG_VRB_ArmError( cent, res, &meaningful );
			if ( !meaningful ) {
				err[c] = -1.0f;
			}
			if ( err[c] >= 0.0f && ( err[best] < 0.0f || err[c] < err[best] ) ) {
				best = c;
			}
		}
		if ( err[best] >= 0.0f && err[best] < 0.75f ) {
			qboolean clear = qtrue;
			for ( c = 0; c < VRB_NUM_CONVENTIONS; c++ ) {
				if ( c != best && err[c] >= 0.0f && err[c] < 2.0f ) {
					clear = qfalse;
				}
			}
			if ( clear && ++vrb.votes[best] >= 3 ) {
				vrb.convention = best;
				vrb.nextCheck = cg.time + 2000;
				if ( cg_vrBodyDebug.integer ) {
					Com_Printf( "VR body: arm solver settled on bone convention %d\n", best );
				}
			}
		}
		if ( ++vrb.checks > 600 && vrb.convention < 0 ) {
			int most = 0;
			for ( c = 1; c < VRB_NUM_CONVENTIONS; c++ ) {
				if ( vrb.votes[c] > vrb.votes[most] ) {
					most = c;
				}
			}
			vrb.convention = most;
			Com_Printf( S_COLOR_YELLOW "VR body: arm self-check was inconclusive, using convention %d\n", most );
		}
		vrb.lastBest = ( vrb.convention >= 0 ) ? vrb.convention : best;
		CG_VRB_SolveArms( cent, vr, vrb.lastBest, bodyYaw, qtrue, res );
		return;
	}

	if ( vrb.convention < 0 ) {
		// second eye of a frame that is still checking: same pick as the first
		CG_VRB_SolveArms( cent, vr, vrb.lastBest, bodyYaw, qtrue, res );
		return;
	}

	CG_VRB_SolveArms( cent, vr, vrb.convention, bodyYaw, qtrue, res );

	// occasional re-check (a model with a different skeleton could need another convention)
	if ( cg.time >= vrb.nextCheck || cg_vrBodyDebug.integer > 1 ) {
		const float e = CG_VRB_ArmError( cent, res, &meaningful );
		vrb.nextCheck = cg.time + 2000;
		if ( cg_vrBodyDebug.integer > 1 ) {
			Com_Printf( "VR body: arm error %.2f%s\n", e, meaningful ? "" : " (not checked)" );
		}
		if ( meaningful ) {
			if ( e > 2.0f ) {
				if ( ++vrb.badChecks >= 3 ) {
					vrb.convention = -1;
					vrb.checks = 0;
					vrb.badChecks = 0;
					memset( vrb.votes, 0, sizeof( vrb.votes ) );
				}
			} else {
				vrb.badChecks = 0;
			}
		}
	}
}

/*
=============================================================================

putting it together

=============================================================================
*/

// The head surface: "head", or a variant ("heada", "headb"...) on models
// that have several. Looked up by existence, not visibility, every time, so
// a model change (which may reuse the same instance pointer) is picked up.
static qboolean CG_VRB_HeadSurface( void *g2, char *name, int size ) {
	int i;

	if ( trap->G2API_GetSurfaceRenderStatus( g2, 0, "head" ) != -1 ) {
		Q_strncpyz( name, "head", size );
		return qtrue;
	}
	for ( i = 0; i < 8; i++ ) {
		Com_sprintf( name, size, "head%c", 'a' + i );
		if ( trap->G2API_GetSurfaceRenderStatus( g2, 0, name ) != -1 ) {
			return qtrue;
		}
	}
	return qfalse;
}

/*
=================
CG_VR_BodyPose

Called from CG_Player after the game turned the model (with
CG_VR_BodyAngles' facing). Hides the head and the body's own copy of the
gun, and moves the model under the headset. The arms follow later, once the
frame's animation is set (CG_VR_BodyArms).
=================
*/
void CG_VR_BodyPose( centity_t *cent, refEntity_t *legs ) {
	const vrClientState_t *vr = CG_VR();
	vrbBone_t le, re;
	vec3_t eyes, head, headAxis[3], fwd, shift;
	float yaw, len, duck;
	qboolean haveEyes = qfalse;

	CG_VRB_Init();
	if ( !vr || !cent->ghoul2 ) {
		return;
	}
	yaw = CG_VRB_BodyYaw( vr );

	// your eyes are the camera: no head in the way
	if ( CG_VRB_HeadSurface( cent->ghoul2, vrb.headSurf, sizeof( vrb.headSurf ) ) ) {
		trap->G2API_SetSurfaceOnOff( cent->ghoul2, vrb.headSurf, VRB_TURN_OFF );
		vrb.headG2 = cent->ghoul2;
	}

	// the gun you see is the one in your VR hand (cg_weapons.c), not the
	// copy bolted to the body's hand
	if ( trap->ext.VR_G2SetModelHidden && trap->ext.VR_G2SetModelHidden( cent->ghoul2, 1, qtrue ) ) {
		vrb.weaponG2 = cent->ghoul2;
	}

	// the animated pose, without last frame's arm overrides
	CG_VRB_ResetArms( cent->ghoul2 );
	vrb.armsG2 = cent->ghoul2;

	// Stand the body under the headset: sideways, the model's eyes go just
	// behind yours (so the neck stays out of view); up and down, the body
	// rides along with the camera's own smoothing (stairs, landings) and
	// sinks when you crouch for real. Its height otherwise stays the
	// model's own, feet on the ground.
	if ( CG_VRB_Bone( cent, "leye", 1, &le ) && CG_VRB_Bone( cent, "reye", 1, &re ) ) {
		VectorAdd( le.origin, re.origin, eyes );
		VectorScale( eyes, 0.5f, eyes );
		haveEyes = qtrue;
	} else if ( CG_VRB_Bone( cent, "cranium", 1, &le ) ) {
		VectorCopy( le.origin, eyes );
		haveEyes = qtrue;
	}
	if ( haveEyes && CG_VR_HeadPose( head, headAxis ) ) {
		VectorSet( fwd, cosf( DEG2RAD( yaw ) ), sinf( DEG2RAD( yaw ) ), 0.0f );
		VectorMA( head, -cg_vrBodyBack.value, fwd, head );
		VectorSubtract( head, eyes, shift );
		shift[2] = 0.0f;
		len = VectorLength( shift );
		if ( len > 48.0f ) {
			VectorScale( shift, 48.0f / len, shift );	// never wander off
		}

		duck = 0.0f;	// the camera's duck smoothing: the crouch animation does that
		if ( cg.time - cg.duckTime >= 0 && cg.time - cg.duckTime < DUCK_TIME ) {
			duck = cg.duckChange * ( DUCK_TIME - ( cg.time - cg.duckTime ) ) / DUCK_TIME;
		}
		shift[2] = cg.refdef.vieworg[2] + duck - ( cent->lerpOrigin[2] + cg.predictedPlayerState.viewheight );
		shift[2] += Q_min( 0.0f, head[2] - cg.refdef.vieworg[2] );
		shift[2] = Com_Clamp( -48.0f, 48.0f, shift[2] );

		// Move the entity itself for the rest of the frame, so effects bolted
		// to the body (force powers from your hand...) come from the drawn
		// model. CG_VR_BodyEndFrame puts it back.
		VectorAdd( cent->lerpOrigin, shift, cent->lerpOrigin );
		VectorCopy( cent->lerpOrigin, legs->origin );
		VectorCopy( cent->lerpOrigin, legs->oldorigin );
		VectorCopy( cent->lerpOrigin, legs->lightingOrigin );
		// (the origin is recomputed before every CG_Player, so only the
		// last shift is ever in it)
		vrb.shiftedEnt = cent;
		VectorCopy( shift, vrb.shift );
	}
}

// Hands on the controllers. After CG_PlayerAnimation, so the arms are solved
// against the animation that is actually drawn this frame.
void CG_VR_BodyArms( centity_t *cent ) {
	const vrClientState_t *vr = CG_VR();

	if ( !vr || !cent->ghoul2 ) {
		return;
	}
	if ( cg_vrBodyArms.integer ) {
		CG_VRB_Arms( cent, vr, CG_VRB_BodyYaw( vr ) );
	} else {
		CG_VRB_ResetArms( cent->ghoul2 );
	}
}

// After the frame's scene is built: the entity's real position back (next
// frame's events and sounds use it).
void CG_VR_BodyEndFrame( void ) {
	if ( vrb.shiftedEnt ) {
		VectorSubtract( vrb.shiftedEnt->lerpOrigin, vrb.shift, vrb.shiftedEnt->lerpOrigin );
		vrb.shiftedEnt = NULL;
		VectorClear( vrb.shift );
	}
}

// Undoes CG_VR_BodyPose on a model (third person, death, leaving VR...).
// Only touches what was changed on this very instance; cheap otherwise.
void CG_VR_BodyRestore( centity_t *cent ) {
	CG_VRB_Init();
	if ( !cent->ghoul2 ) {
		return;
	}
	if ( vrb.armsG2 == cent->ghoul2 ) {
		CG_VRB_ResetArms( cent->ghoul2 );
		vrb.armsG2 = NULL;
		vrb.poleValid[0] = vrb.poleValid[1] = qfalse;
	}
	if ( vrb.headG2 == cent->ghoul2 ) {
		// back on, unless the head was cut off for real (the skin is
		// re-applied when the limb grows back)
		if ( !( cent->torsoBolt & VRB_HEAD_BIT ) &&
			CG_VRB_HeadSurface( cent->ghoul2, vrb.headSurf, sizeof( vrb.headSurf ) ) ) {
			trap->G2API_SetSurfaceOnOff( cent->ghoul2, vrb.headSurf, 0 );
		}
		vrb.headG2 = NULL;
	}
	if ( vrb.weaponG2 == cent->ghoul2 ) {
		if ( trap->ext.VR_G2SetModelHidden ) {
			trap->ext.VR_G2SetModelHidden( cent->ghoul2, 1, qfalse );
		}
		vrb.weaponG2 = NULL;
	}
}
