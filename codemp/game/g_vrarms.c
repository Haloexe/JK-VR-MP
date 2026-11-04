/*
===========================================================================
OpenJK VR (multiplayer) - a VR player's arms for the games without the VR
build.

Games of another build (any stock-based one) can't read a VR player's avatar
(svc_vrAvatars): they draw them in the game's own saber stance, the saber
where that stance swings it. But every game turns up to four bones of a
player the server names, on top of the animation (the entity's boneIndex1..4,
boneAngles1..4, boneOrient; POSTMULT - cgame CG_G2ServerBoneAngles). So for
each VR player holding their saber this works out, every frame:

- a held torso pose, a fist in each hand: BOTH_STAND6 (fists up in front),
  or for both hands on a hilt held further out BOTH_P6_S6_T_ - each the same
  on every frame (the bones turn on top of the animation, and a game that
  only now sees them - in its view at last, respawned, teleported - starts
  its torso's from the first frame) - while their legs stand, walk, run,
  jump or crouch (standing: upright, BOTH_STAND5, as the VR builds draw
  them, not in a saber stance); anything else (a roll, a flip, a knockdown,
  sitting) shows as ever. Their body faces the way their VR body does (its
  yaw: the entity's angles, for those games alone), their head nods as
  theirs does.
- what those games then have under the four bones: a copy of the player's
  model ("the stand-in"), animated exactly as they animate it (cg_players.c:
  the same animations, started when they start them, the same blends, the
  same BG_G2PlayerAngles with the same memory of where the legs faced).
- the bones: the right upper arm and forearm (rhumerus, rradius) put the
  fist where the saber's own tag holds it - the elbow round the line from
  the shoulder, the forearm rolled so the hilt runs along the knuckles as a
  fist holds one, as far as a forearm turns on its upper arm (-55..95 degrees
  off the pose's own, no faster than 600 a second), the elbow hanging as an
  arm's does; the saber's own bone (rhang_tag_bone) turns it exactly as
  theirs; the left upper arm (lhumerus) swings that arm to their left hand,
  or the hilt where it reaches it (their second saber: as near its way as it
  turns).

The engine sends this only to the games that can't draw the avatar - in
their copies of the player's entity, never into the entity itself
(ext.VR_SetArmsView, sv_snapshot.cpp). A game keeps a bone turned until it's
sent other angles for it, so a player whose arms have been sent is then sent
the same bones unturned for as long as they're in the game (through a
map_restart too: those games keep the players' models).

The four bones change once a snapshot (the games don't blend them): the arms
move at the server's frame rate.

g_vrArmsForAll 0: off (those games show the stance, as before).

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "g_local.h"
#include "ghoul2/G2.h"

extern qboolean BG_InDeathAnim( int anim );
extern qboolean BG_FlippingAnim( int anim );
extern qboolean PM_RunningAnim( int anim );
extern qboolean PM_WalkingAnim( int anim );
extern qboolean PM_JumpingAnim( int anim );
extern void BG_SaberStartTransAnim( int clientNum, int saberAnimLevel, int weapon, int anim, float *animSpeed, int broken );

#define VRA_POSE				BOTH_STAND6		// the held torso pose (the first of vraPoses)

// The held poses (each a fist in each hand, its torso near upright as the
// other games draw it, and still: every frame of it the same): the first held
// with one hand on the hilt; with both, the one whose left arm - only its
// upper arm turned - best reaches the hilt from its shoulder (its fist to its
// shoulder: the humanoid skeleton's).
static const struct {
	int			anim;
	float		reach;
} vraPoses[] = {
	{ BOTH_STAND6, 10.3f },				// (fists up in front, the left arm well bent: a guard, a hilt held close)
	{ BOTH_P6_S6_T_, 18.9f },			// (its left arm near straight: a hilt held further out)
};
#define VRA_NUM_POSES			( (int)ARRAY_LEN( vraPoses ) )
#define VRA_SWAP_GAIN			1.5f	// another pose only for one whose hand falls this much nearer the hilt (units)...
#define VRA_SWAP_HOLD			300		// ...and no sooner than this after the last change (ms)

// (the engine passes on no animation from VR_ARMS_ANIMS on: the games' own number of them)
typedef char vraAnimsAsTheGames[( VR_ARMS_ANIMS == MAX_TOTALANIMATIONS ) ? 1 : -1];

// the right arm (vrArmsRight)
#define VRA_FIT_ELBOW			10.0f	// the fist's fit (deg^2) against the elbow off where an arm hangs it (units^2)
#define VRA_FIT_STAY			20.0f	// ...and against the elbow off where it was, carried on as that moved (units^2)
#define VRA_FIT_ELBOW_RATE		0.04f	// ...and the elbow goes no faster than where an arm hangs it by more than this (units a ms)
#define VRA_TWIST_LO			-55.0f	// the forearm turned on its upper arm, off the game's own resting arm: this far one way...
#define VRA_TWIST_HI			95.0f	// ...this far the other (palm down to palm up)...
#define VRA_TWIST_RATE			600.0f	// ...and no faster than this (deg a second)
#define VRA_PSI_STEP			6.0f	// the elbow tried round its circle every this many degrees...
#define VRA_PSI_FINE			1.5f	// ...then this finely either side of the best
#define VRA_ELBOW_DOWN			1.0f	// where an arm hangs its elbow: down...
#define VRA_ELBOW_OUT			0.35f	// ...out to its side...
#define VRA_ELBOW_BACK			0.15f	// ...and a little back

// the left arm (vrArmsLeft)
#define VRA_HILT_LO				3.0f	// a second hand holds the hilt at least this far below where the first's tag holds it...
#define VRA_HILT_FAR			14.0f	// ...and at most this far (a hand that can't reach nearer slides down it)
#define VRA_SLIDE_RATE			80.0f	// ...and slides along it no faster than this (units a second)
#define VRA_LEFT_STEPS			36		// its elbow tried round the line to its hand this many times...
#define VRA_LEFT_RATE			900.0f	// ...and goes round it no faster than this (deg a second)
#define VRA_LEFT_ELBOW_UP		0.5f	// an elbow raised (per unit of the arm's turn round, squared) counts this much against the fit

#define VRA_GAP					250		// ms: arms not worked out for longer than this start afresh (no rates)

// The forearm's turn on its upper arm is measured off the game's own resting
// arm (the last frame of BOTH_FORCE_PROTECT): this is how its forearm lies
// on its upper arm (each bone's own frame), and the forearm's own line, in
// its frame. And the elbow's hinge, in the upper arm's frame (from
// BOTH_FORCE_ABSORB_END's well bent elbow). The humanoid skeleton's, which
// every humanoid model shares.
static const float vraTwistRest[3][3] = {
	{ 0.369218f, -0.000302f, 0.929705f },
	{ -0.905748f, 0.226152f, 0.360388f },
	{ -0.210641f, -0.974808f, 0.083087f },
};
static const vec3_t vraTwistAxis = { 1.0f, 0.000562f, 0.000432f };
static const vec3_t vraHinge = { 0.001005f, 0.892429f, -0.451186f };

// How a fist holds a hilt, as the VR build puts a saber in it: along the
// knuckles, from the little finger's to the index finger's (toward the
// blade), tipped 12 degrees toward the fingers - in the fist's frame (x the
// little finger's knuckle to the index finger's, z the wrist to the knuckles
// square to x, y = z x x). The game's own tag on a fist hangs a saber square
// to that, out through the palm.
static const vec3_t vraGripKnuckles = { 0.977802f, 0.0f, 0.209529f };

typedef float vraMat_t[3][3];		// a turn: rows by columns, acting on column vectors (its columns: a frame's axes)

// the bones of the stand-in this needs (bolts)
enum {
	VRAB_MOTION,
	VRAB_RHUMERUS, VRAB_RRADIUS, VRAB_RHAND, VRAB_RTAG, VRAB_RD2, VRAB_RD4,
	VRAB_LHUMERUS, VRAB_LRADIUS, VRAB_LHAND, VRAB_LTAG, VRAB_LD2, VRAB_LD4,
	VRAB_NUM
};
static const char *const vraBoltNames[VRAB_NUM] = {
	"Motion",
	"rhumerus", "rradius", "rhand", "rhang_tag_bone", "r_d2_j1", "r_d4_j1",
	"lhumerus", "lradius", "lhand", "lhang_tag_bone", "l_d2_j1", "l_d4_j1",
};

// the four bones sent
static const char *const vraSentBones[VR_ARMS_BONES] = { "rhumerus", "rradius", "rhang_tag_bone", "lhumerus" };

typedef struct {
	vec3_t		origin;
	vraMat_t	R;
} vraFrame_t;

typedef struct {
	// the stand-in: the player's model as the other builds animate it
	void		*g2;
	void		*g2From;			// (the model it was copied from: copied again when that changes)
	int			bolt[VRAB_NUM];
	qboolean	animsSet;			// what they last started on its legs and torso (as cgame CG_RunLerpFrame does)...
	int			legsAnim, legsFlip, torsoAnim, torsoFlip, brokenLimbs;
	float		legsSpeed, torsoSpeed;
	// ...and BG_G2PlayerAngles' memory (theirs is per player)
	qboolean	tYawing, tPitching, lYawing;
	float		tYawAngle, tPitchAngle, lYawAngle;
	int			corrTime, lookTime, superSmooth;
	vec3_t		lastHeadAngles, turAngles;
	int			anglesTime;

	// what's sent
	qboolean	active;				// the held pose and turned arms
	int			activeTime;			// (level.time they were last worked out)
	int			torsoFlipHeld;		// the torso flip sent with the pose (kept, so it never restarts)
	qboolean	everSent;			// bones were sent: from now on they're sent, unturned when not active
	vrArmsView_t view;

	// the arms as they were last worked out, in the body's own space (turned
	// with it, from its origin): the right forearm and elbow, where an arm
	// hangs that elbow, the forearm's turn; the left upper arm; where the left
	// hand held the hilt
	qboolean	haveRight;
	vraMat_t	rightRF;
	vec3_t		rightE, rightElv;
	float		rightTwist;
	qboolean	haveLeft;
	vraMat_t	leftRU;
	qboolean	haveSlide;
	float		slide;
	// the held pose (vraPoses), since when; the left shoulder as it last was (the body's space)
	int			pose;
	int			poseSince;
	qboolean	haveShoulderL;
	vec3_t		shoulderL;
} vrArms_t;

static vrArms_t		vrArms[MAX_CLIENTS];
static int			vraBoneIndex[VR_ARMS_BONES];
static qboolean		vraEngine;				// the engine takes it (ext.VR_SetArmsView)
static qboolean		vraEngineChecked;

/*
--------------------------------------------------------------------------------
Turns
--------------------------------------------------------------------------------
*/

static void VRA_Identity( vraMat_t M ) {
	memset( M, 0, sizeof( vraMat_t ) );
	M[0][0] = M[1][1] = M[2][2] = 1.0f;
}

static void VRA_Copy( vraMat_t out, vraMat_t in ) {
	memcpy( out, in, sizeof( vraMat_t ) );
}

// out = A B
static void VRA_Mul( vraMat_t A, vraMat_t B, vraMat_t out ) {
	vraMat_t t;
	int r, c;
	for ( r = 0; r < 3; r++ ) {
		for ( c = 0; c < 3; c++ ) {
			t[r][c] = A[r][0] * B[0][c] + A[r][1] * B[1][c] + A[r][2] * B[2][c];
		}
	}
	VRA_Copy( out, t );
}

// out = A^T B
static void VRA_MulTA( vraMat_t A, vraMat_t B, vraMat_t out ) {
	vraMat_t t;
	int r, c;
	for ( r = 0; r < 3; r++ ) {
		for ( c = 0; c < 3; c++ ) {
			t[r][c] = A[0][r] * B[0][c] + A[1][r] * B[1][c] + A[2][r] * B[2][c];
		}
	}
	VRA_Copy( out, t );
}

// out = A B^T
static void VRA_MulTB( vraMat_t A, vraMat_t B, vraMat_t out ) {
	vraMat_t t;
	int r, c;
	for ( r = 0; r < 3; r++ ) {
		for ( c = 0; c < 3; c++ ) {
			t[r][c] = A[r][0] * B[c][0] + A[r][1] * B[c][1] + A[r][2] * B[c][2];
		}
	}
	VRA_Copy( out, t );
}

static void VRA_Apply( vraMat_t M, const vec3_t v, vec3_t out ) {
	vec3_t t;
	t[0] = M[0][0] * v[0] + M[0][1] * v[1] + M[0][2] * v[2];
	t[1] = M[1][0] * v[0] + M[1][1] * v[1] + M[1][2] * v[2];
	t[2] = M[2][0] * v[0] + M[2][1] * v[1] + M[2][2] * v[2];
	VectorCopy( t, out );
}

static void VRA_ApplyT( vraMat_t M, const vec3_t v, vec3_t out ) {
	vec3_t t;
	t[0] = M[0][0] * v[0] + M[1][0] * v[1] + M[2][0] * v[2];
	t[1] = M[0][1] * v[0] + M[1][1] * v[1] + M[2][1] * v[2];
	t[2] = M[0][2] * v[0] + M[1][2] * v[1] + M[2][2] * v[2];
	VectorCopy( t, out );
}

// the turn by angle (radians) about unit axis k
static void VRA_AxisAngle( const vec3_t k, float a, vraMat_t M ) {
	const float c = cosf( a ), s = sinf( a ), t = 1.0f - c;
	M[0][0] = t * k[0] * k[0] + c;			M[0][1] = t * k[0] * k[1] - s * k[2];	M[0][2] = t * k[0] * k[2] + s * k[1];
	M[1][0] = t * k[0] * k[1] + s * k[2];	M[1][1] = t * k[1] * k[1] + c;			M[1][2] = t * k[1] * k[2] - s * k[0];
	M[2][0] = t * k[0] * k[2] - s * k[1];	M[2][1] = t * k[1] * k[2] + s * k[0];	M[2][2] = t * k[2] * k[2] + c;
}

// the least turn taking a's way to b's
static void VRA_Align( const vec3_t a0, const vec3_t b0, vraMat_t M ) {
	vec3_t a, b, v;
	float c;

	VectorCopy( a0, a );
	VectorCopy( b0, b );
	if ( VectorNormalize( a ) < 1e-6f || VectorNormalize( b ) < 1e-6f ) {
		VRA_Identity( M );
		return;
	}
	CrossProduct( a, b, v );
	c = DotProduct( a, b );
	if ( c < -0.9999f ) {
		// (half a turn: about any line square to a)
		vec3_t p;
		PerpendicularVector( p, a );
		VRA_AxisAngle( p, M_PI, M );
		return;
	}
	{
		const float k = 1.0f / ( 1.0f + c );
		M[0][0] = c + k * v[0] * v[0];			M[0][1] = -v[2] + k * v[0] * v[1];	M[0][2] = v[1] + k * v[0] * v[2];
		M[1][0] = v[2] + k * v[0] * v[1];		M[1][1] = c + k * v[1] * v[1];		M[1][2] = -v[0] + k * v[1] * v[2];
		M[2][0] = -v[1] + k * v[0] * v[2];		M[2][1] = v[0] + k * v[1] * v[2];	M[2][2] = c + k * v[2] * v[2];
	}
}

// re-squared (its first column kept, the second straightened against it)
static void VRA_Square( vraMat_t M ) {
	vec3_t a, b, c;
	a[0] = M[0][0]; a[1] = M[1][0]; a[2] = M[2][0];
	b[0] = M[0][1]; b[1] = M[1][1]; b[2] = M[2][1];
	if ( VectorNormalize( a ) < 1e-6f ) {
		VRA_Identity( M );
		return;
	}
	VectorMA( b, -DotProduct( a, b ), a, b );
	if ( VectorNormalize( b ) < 1e-6f ) {
		PerpendicularVector( b, a );
	}
	CrossProduct( a, b, c );
	M[0][0] = a[0]; M[1][0] = a[1]; M[2][0] = a[2];
	M[0][1] = b[0]; M[1][1] = b[1]; M[2][1] = b[2];
	M[0][2] = c[0]; M[1][2] = c[1]; M[2][2] = c[2];
}

// the part of a's turn about unit axis k (degrees, -180..180)
static float VRA_TwistAbout( vraMat_t D, const vec3_t k ) {
	const float w = 0.5f * sqrtf( Q_max( 0.0f, 1.0f + D[0][0] + D[1][1] + D[2][2] ) );
	vec3_t v;
	const float q = 1.0f / ( 4.0f * Q_max( w, 1e-9f ) );
	v[0] = ( D[2][1] - D[1][2] ) * q;
	v[1] = ( D[0][2] - D[2][0] ) * q;
	v[2] = ( D[1][0] - D[0][1] ) * q;
	return RAD2DEG( 2.0f * atan2f( DotProduct( v, k ), w ) );
}

// the angle between units (degrees)
static float VRA_Between( const vec3_t a, const vec3_t b ) {
	return RAD2DEG( acosf( Com_Clamp( -1.0f, 1.0f, DotProduct( a, b ) ) ) );
}

// a unit square to unit k, as near v as there is (qfalse: v runs along k)
static qboolean VRA_Square2( const vec3_t v, const vec3_t k, vec3_t out ) {
	VectorMA( v, -DotProduct( v, k ), k, out );
	return (qboolean)( VectorNormalize( out ) > 1e-4f );
}

// the angle (radians) turning a to b about unit k, both square to it
static float VRA_AngleAbout( const vec3_t a, const vec3_t b, const vec3_t k ) {
	vec3_t c;
	CrossProduct( a, b, c );
	return atan2f( DotProduct( c, k ), DotProduct( a, b ) );
}

/*
--------------------------------------------------------------------------------
The stand-in: the player's model as the other builds animate it
--------------------------------------------------------------------------------
*/

static void VRA_FreeModel( vrArms_t *va ) {
	if ( va->g2 && trap->G2API_HaveWeGhoul2Models( va->g2 ) ) {
		trap->G2API_CleanGhoul2Models( &va->g2 );
	}
	va->g2 = NULL;
	va->g2From = NULL;
	va->animsSet = qfalse;
}

// A copy of the player's model (theirs with their sabers on it, as the other
// games have it), with the bolts this reads.
static qboolean VRA_Model( vrArms_t *va, gentity_t *ent ) {
	int b;

	if ( va->g2 && va->g2From == ent->ghoul2 ) {
		return qtrue;
	}
	VRA_FreeModel( va );
	if ( !ent->ghoul2 || !trap->G2API_HaveWeGhoul2Models( ent->ghoul2 ) ) {
		return qfalse;
	}
	trap->G2API_DuplicateGhoul2Instance( ent->ghoul2, &va->g2 );
	if ( !va->g2 ) {
		return qfalse;
	}
	va->g2From = ent->ghoul2;
	for ( b = 0; b < VRAB_NUM; b++ ) {
		va->bolt[b] = trap->G2API_AddBolt( va->g2, 0, vraBoltNames[b] );
		if ( va->bolt[b] < 0 && b != VRAB_MOTION ) {
			VRA_FreeModel( va );	// (not a humanoid's skeleton)
			return qfalse;
		}
	}
	// (its arms as the animation has them: nothing of the server's own on them)
	for ( b = 0; b < VR_ARMS_BONES; b++ ) {
		trap->G2API_SetBoneAngles( va->g2, 0, vraSentBones[b], vec3_origin, BONE_ANGLES_POSTMULT, POSITIVE_Z, NEGATIVE_Y,
			POSITIVE_X, NULL, 0, level.time );
	}
	va->animsSet = qfalse;
	return qtrue;
}

// The frame a bone's animation is on now (-1: none).
static float VRA_BoneFrame( vrArms_t *va, const char *bone ) {
	float frame = -1.0f, speed;
	int start, end, flags;
	if ( !trap->G2API_GetBoneAnim( va->g2, bone, level.time, &frame, &start, &end, &flags, &speed, NULL, 0 ) ) {
		return -1.0f;
	}
	return frame;
}

// An animation started on the stand-in's legs or torso the way the other
// games start it (cg_players.c CG_SetLerpFrameAnimation, its defaults).
static void VRA_StartAnim( vrArms_t *va, gentity_t *ent, const entityState_t *es, int anim, float speedMult, qboolean torso,
	int oldAnim ) {
	const animation_t *a = &bgAllAnims[ent->localAnimIndex].anims[anim];
	float animSpeed;
	int flags = BONE_ANIM_OVERRIDE_FREEZE, blendTime = 100, first, last, begin = -1;

	if ( !a->frameLerp ) {
		return;
	}
	animSpeed = 50.0f / a->frameLerp;
	if ( a->loopFrames != -1 ) {
		flags = BONE_ANIM_OVERRIDE_LOOP;
	}
	if ( animSpeed < 0 ) {
		last = a->firstFrame;
		first = a->firstFrame + a->numFrames;
	} else {
		first = a->firstFrame;
		last = a->firstFrame + a->numFrames;
	}
	flags |= BONE_ANIM_BLEND;	// (cg_animBlend 1)
	if ( BG_InDeathAnim( anim ) || ( oldAnim != -1 && BG_InDeathAnim( oldAnim ) ) ) {
		flags &= ~BONE_ANIM_BLEND;
	} else if ( BG_FlippingAnim( anim ) || ( oldAnim != -1 && BG_FlippingAnim( oldAnim ) ) ) {
		blendTime = 200;
	}
	animSpeed *= speedMult;
	BG_SaberStartTransAnim( es->number, es->fireflag, es->weapon, anim, &animSpeed, es->brokenLimbs );
	if ( !torso && es->torsoAnim == es->legsAnim ) {
		// (the torso already runs it: its frame's picked up)
		const float frame = VRA_BoneFrame( va, "lower_lumbar" );
		if ( frame >= first && frame <= last ) {
			begin = (int)frame;
		}
	} else if ( torso && es->torsoAnim == es->legsAnim && first <= last && va->torsoAnim != anim ) {
		const float frame = VRA_BoneFrame( va, "model_root" );
		if ( frame >= a->firstFrame && frame <= a->firstFrame + a->numFrames ) {
			begin = (int)frame;
		}
	}
	trap->G2API_SetBoneAnim( va->g2, 0, torso ? "lower_lumbar" : "model_root", first, last, flags, animSpeed, level.time,
		begin, blendTime );
	// (a humanoid's Motion bone plays it too if it's the torso's, as theirs does - BG_G2ClientSpineAngles takes the
	// Motion bone's turn back off the spine)
	if ( es->torsoAnim == anim ) {
		trap->G2API_SetBoneAnim( va->g2, 0, "Motion", first, last, flags, animSpeed, level.time, begin, blendTime );
	}
}

// The stand-in's legs and torso run what the other games run for the entity
// they're sent (cg_players.c CG_PlayerAnimation, CG_RunLerpFrame).
static void VRA_Animate( vrArms_t *va, gentity_t *ent, const entityState_t *es ) {
	float speed = 1.0f;

	if ( es->legsAnim < 0 || es->legsAnim >= MAX_ANIMATIONS || es->torsoAnim < 0 || es->torsoAnim >= MAX_ANIMATIONS ) {
		return;
	}
	if ( PM_RunningAnim( es->legsAnim ) || PM_WalkingAnim( es->legsAnim ) ) {
		if ( es->forcePowersActive & ( 1 << FP_RAGE ) ) {
			speed = 1.3f;
		} else if ( es->forcePowersActive & ( 1 << FP_SPEED ) ) {
			speed = 1.7f;
		}
	}
	if ( !va->animsSet || es->legsAnim != va->legsAnim || es->legsFlip != va->legsFlip || es->brokenLimbs != va->brokenLimbs ||
		speed != va->legsSpeed ) {
		VRA_StartAnim( va, ent, es, es->legsAnim, speed, qfalse, va->animsSet ? va->legsAnim : -1 );
		va->legsAnim = es->legsAnim;
		va->legsFlip = es->legsFlip;
		va->legsSpeed = speed;
	}
	speed = ( es->forcePowersActive & ( 1 << FP_RAGE ) ) ? 1.7f : 1.0f;
	if ( !va->animsSet || es->torsoAnim != va->torsoAnim || es->torsoFlip != va->torsoFlip || es->brokenLimbs != va->brokenLimbs ||
		speed != va->torsoSpeed ) {
		VRA_StartAnim( va, ent, es, es->torsoAnim, speed, qtrue, va->animsSet ? va->torsoAnim : -1 );
		va->torsoAnim = es->torsoAnim;
		va->torsoFlip = es->torsoFlip;
		va->torsoSpeed = speed;
	}
	va->brokenLimbs = es->brokenLimbs;
	va->animsSet = qtrue;
}

// Where the other games draw the entity they're sent, its scale, and how
// its body turns (BG_G2PlayerAngles, as cg_players.c CG_G2PlayerAngles calls
// it): the stand-in's spine and head turned as theirs, its angles (turAngles).
static void VRA_Place( vrArms_t *va, gentity_t *ent, const entityState_t *es, vec3_t origin, vec3_t scale ) {
	vec3_t lookAngles, legsAngles;
	matrix3_t legs;
	entityState_t s = *es;
	int frametime = level.time - va->anglesTime;

	BG_EvaluateTrajectory( &es->pos, level.time, origin );
	VectorClear( scale );
	if ( es->iModelScale ) {
		scale[0] = scale[1] = scale[2] = es->iModelScale / 100.0f;
		if ( scale[2] != 1.0f ) {
			origin[2] += 24.0f * ( scale[2] - 1.0f );
		}
	}
	if ( !va->anglesTime || frametime <= 0 || frametime > 200 ) {
		// (afresh: facing where it faces)
		frametime = level.time - va->anglesTime;
		frametime = ( frametime > 0 && frametime <= 200 ) ? frametime : FRAMETIME;
		va->tYawAngle = va->lYawAngle = AngleMod( es->apos.trBase[YAW] );
		va->tPitchAngle = 0.0f;
		va->tYawing = va->tPitching = va->lYawing = qfalse;
		va->corrTime = va->superSmooth = 0;
		VectorClear( va->lastHeadAngles );
	}
	va->anglesTime = level.time;
	if ( es->hasLookTarget && es->lookTarget >= 0 && es->lookTarget < ENTITYNUM_WORLD ) {
		VectorSubtract( g_entities[es->lookTarget].r.currentOrigin, origin, lookAngles );
		vectoangles( lookAngles, lookAngles );
		va->lookTime = level.time + 1000;
	} else {
		VectorCopy( es->apos.trBase, lookAngles );
	}
	lookAngles[PITCH] = 0.0f;
	BG_G2PlayerAngles( va->g2, va->bolt[VRAB_MOTION], &s, level.time, origin, (float *)es->apos.trBase, legs, legsAngles,
		&va->tYawing, &va->tPitching, &va->lYawing, &va->tYawAngle, &va->tPitchAngle, &va->lYawAngle, frametime,
		va->turAngles, scale, va->legsAnim, va->torsoAnim, &va->corrTime, lookAngles, va->lastHeadAngles, va->lookTime, NULL,
		&va->superSmooth );
}

// A bone of the stand-in in the world: its origin and its own axes (the
// "90 degree offset thing" G2API_GetBoltMatrix puts on them undone).
static qboolean VRA_Bone( vrArms_t *va, int model, int bolt, const vec3_t origin, const vec3_t scale, vraFrame_t *f ) {
	mdxaBone_t m;
	int r;

	if ( bolt < 0 || !trap->G2API_GetBoltMatrix( va->g2, model, bolt, &m, va->turAngles, origin, level.time, NULL,
		(float *)scale ) ) {
		return qfalse;
	}
	for ( r = 0; r < 3; r++ ) {
		f->origin[r] = m.matrix[r][3];
		f->R[r][0] = m.matrix[r][1];
		f->R[r][1] = -m.matrix[r][0];
		f->R[r][2] = m.matrix[r][2];
	}
	VRA_Square( f->R );
	return qtrue;
}

// The way a saber hilt runs through a fist (toward the blade), as a fist
// holds one (vraGripKnuckles): from the fist's wrist and knuckles.
static qboolean VRA_KnuckleWay( const vec3_t wrist, const vec3_t index, const vec3_t little, vec3_t out ) {
	vec3_t x, z, y, mid;

	VectorSubtract( index, little, x );
	if ( VectorNormalize( x ) < 1e-3f ) {
		return qfalse;
	}
	VectorAdd( index, little, mid );
	VectorScale( mid, 0.5f, mid );
	VectorSubtract( mid, wrist, z );
	if ( !VRA_Square2( z, x, z ) ) {
		return qfalse;
	}
	CrossProduct( z, x, y );
	VectorScale( x, vraGripKnuckles[0], out );
	VectorMA( out, vraGripKnuckles[1], y, out );
	VectorMA( out, vraGripKnuckles[2], z, out );
	return (qboolean)( VectorNormalize( out ) > 0.5f );
}

/*
--------------------------------------------------------------------------------
The body's own space (the arms' memory turns and moves with it)
--------------------------------------------------------------------------------
*/

typedef struct {
	vec3_t		origin;
	vraMat_t	R;				// body -> world: the turn by its yaw
	vec3_t		fwd, right, up;
} vraBody_t;

static void VRA_BodyFrame( const vec3_t origin, float yaw, vraBody_t *b ) {
	const vec3_t up = { 0.0f, 0.0f, 1.0f };
	VectorCopy( origin, b->origin );
	VRA_AxisAngle( up, DEG2RAD( yaw ), b->R );
	VectorSet( b->fwd, b->R[0][0], b->R[1][0], b->R[2][0] );
	VectorSet( b->right, -b->R[0][1], -b->R[1][1], -b->R[2][1] );
	VectorCopy( up, b->up );
}

static void VRA_ToBody( const vraBody_t *b, const vec3_t world, vec3_t out ) {
	vec3_t d;
	VectorSubtract( world, b->origin, d );
	VRA_ApplyT( (float (*)[3])b->R, d, out );
}

static void VRA_FromBody( const vraBody_t *b, const vec3_t body, vec3_t out ) {
	VRA_Apply( (float (*)[3])b->R, body, out );
	VectorAdd( out, b->origin, out );
}

/*
--------------------------------------------------------------------------------
The right arm: the fist where the saber's tag holds it, round the hilt
--------------------------------------------------------------------------------
*/

typedef struct {
	// from the stand-in
	vec3_t		S;				// the shoulder (rhumerus)
	vraMat_t	RU0;			// the upper arm as the pose has it
	vraMat_t	RF0;			// the forearm as the pose has it
	float		Lu, Lf;			// the upper arm's length; the forearm's, to where the tag holds the saber
	vec3_t		u;				// the elbow, in the upper arm's frame
	vec3_t		pf;				// the tag's point, in the forearm's frame...
	vec3_t		bf;				// ...the hilt's way through the fist...
	vec3_t		fdir;			// ...and the forearm's own line
	float		alongF;			// (pf's way along that line)
	// wanted
	vec3_t		p;				// where the saber's tag goes
	vec3_t		d;				// the way the knuckles run (the blade's, or reversed: away from it)
	vec3_t		elv;			// where an arm hangs the elbow
	// worked out round the elbow's circle
	vec3_t		k, x, y, W;
	float		along, hh;
	qboolean	haveStay;
	vec3_t		stay;
	vraMat_t	Rref;
	qboolean	haveTwist;
	float		twistWas, dt;
} vraRight_t;

typedef struct {
	float		cost, fit, twist;
	vec3_t		E;
	vraMat_t	RF, RU;
} vraTry_t;

// The upper arm reaching the elbow E, turned about its own line so the
// elbow's hinge lies square to the forearm (RF) and the upper arm - the
// least turn from the pose's otherwise.
static void VRA_Upper( const vraRight_t *j, const vec3_t E, vraMat_t RF, vraMat_t RU ) {
	vec3_t e, a, n, h, hp, f;
	vraMat_t A, T;
	float sn;

	VectorSubtract( E, j->S, e );
	VectorNormalize( e );
	VectorScale( j->u, 1.0f / j->Lu, a );
	VRA_Apply( (float (*)[3])j->RU0, a, a );
	VRA_Align( a, e, A );
	VRA_Mul( A, (float (*)[3])j->RU0, RU );
	VRA_Apply( RF, j->fdir, f );
	CrossProduct( e, f, n );
	sn = VectorNormalize( n );
	VRA_Apply( RU, vraHinge, h );
	if ( sn > 1e-4f && VRA_Square2( h, e, hp ) ) {
		const float ang = VRA_AngleAbout( hp, n, e ) * Com_Clamp( 0.0f, 1.0f, ( sn - 0.05f ) / 0.15f );
		VRA_AxisAngle( e, ang, T );
		VRA_Mul( T, RU, RU );
	}
	VRA_Square( RU );
}

// How far the forearm is turned on its upper arm, about its own line, off the
// resting arm's (degrees).
static float VRA_Twist( vraMat_t RU, vraMat_t RF ) {
	vraMat_t D, rest;
	int r, c;
	for ( r = 0; r < 3; r++ ) {
		for ( c = 0; c < 3; c++ ) {
			rest[r][c] = vraTwistRest[r][c];
		}
	}
	VRA_Square( rest );
	VRA_MulTA( RU, RF, D );
	VRA_MulTA( rest, D, D );
	return VRA_TwistAbout( D, vraTwistAxis );
}

// The elbow at psi round its circle: the forearm reaching the tag's point
// from there, rolled to fit the fist round the hilt, its turn on the upper
// arm kept within what an arm's is (and as fast: that bounds how fast the
// fist rolls); the cost of it all.
static void VRA_RightTry( const vraRight_t *j, float psi, vraTry_t *t ) {
	vec3_t f, a, h0, hp, dp, kf;
	vraMat_t A, T;
	float roll = 0.0f, tw, lo, hi, over, off;
	int it;

	VectorMA( j->S, j->along, j->k, t->E );
	VectorMA( t->E, j->hh * cosf( psi ), j->x, t->E );
	VectorMA( t->E, j->hh * sinf( psi ), j->y, t->E );
	VectorSubtract( j->W, t->E, f );
	VectorNormalize( f );
	VectorScale( j->pf, 1.0f / j->Lf, a );
	VRA_Apply( (float (*)[3])j->Rref, a, a );
	VRA_Align( a, f, A );
	VRA_Mul( A, (float (*)[3])j->Rref, t->RF );
	VRA_Apply( t->RF, j->bf, h0 );
	if ( VRA_Square2( h0, f, hp ) && VRA_Square2( j->d, f, dp ) ) {
		roll = VRA_AngleAbout( hp, dp, f );
	}
	VRA_AxisAngle( f, roll, T );
	VRA_Mul( T, t->RF, t->RF );
	VRA_Square( t->RF );
	VRA_Upper( j, t->E, t->RF, t->RU );
	// (its turn on the upper arm brought within an arm's - the nearer way
	// round to the nearer end: it's never near half a turn while within)
	lo = VRA_TWIST_LO;
	hi = VRA_TWIST_HI;
	if ( j->haveTwist ) {
		lo = Q_max( lo, j->twistWas - VRA_TWIST_RATE * j->dt );
		hi = Q_min( hi, j->twistWas + VRA_TWIST_RATE * j->dt );
		if ( lo > hi ) {
			lo = hi = Com_Clamp( VRA_TWIST_LO, VRA_TWIST_HI, j->twistWas );
		}
	}
	for ( it = 0; it < 4; it++ ) {
		tw = VRA_Twist( t->RU, t->RF );
		if ( tw >= lo && tw <= hi ) {
			break;
		}
		over = AngleNormalize180( tw - lo );
		if ( fabsf( AngleNormalize180( tw - hi ) ) < fabsf( over ) ) {
			over = AngleNormalize180( tw - hi );
		}
		VRA_AxisAngle( f, -DEG2RAD( over ) / Q_max( j->alongF, 0.3f ), T );
		VRA_Mul( T, t->RF, t->RF );
		VRA_Square( t->RF );
		VRA_Upper( j, t->E, t->RF, t->RU );
	}
	t->twist = VRA_Twist( t->RU, t->RF );
	VRA_Apply( t->RF, j->bf, kf );
	t->fit = VRA_Between( kf, j->d );
	t->cost = t->fit * t->fit + VRA_FIT_ELBOW * DistanceSquared( t->E, j->elv );
	if ( j->haveStay ) {
		off = Distance( t->E, j->stay );
		t->cost += VRA_FIT_STAY * off * off;
		if ( off > VRA_FIT_ELBOW_RATE * j->dt * 1000.0f ) {
			t->cost += 1e6f * ( 1.0f + off );	// (never a jump)
		}
	}
}

/*
--------------------------------------------------------------------------------
Working it out
--------------------------------------------------------------------------------
*/

// Whether a VR player's arms are worked out for the other builds now: their
// avatar's saber hand on their first saber, their legs upright (standing,
// walking, running, jumping, crouching) and nothing of the game's own on
// their body.
static qboolean VRA_Wanted( gentity_t *ent, const vrAvatar_t *av, int *saberHand ) {
	const gclient_t *cl = ent->client;
	const entityState_t *es = &ent->s;
	const animation_t *anims;
	int h, legs = es->legsAnim, torso = es->torsoAnim;

	*saberHand = -1;
	for ( h = 0; h < 2; h++ ) {
		if ( av->hand[h].grip == VR_GRIP_SABER && av->hand[h].saber == 0 ) {
			*saberHand = h;
		}
	}
	if ( *saberHand < 0 || es->weapon != WP_SABER || ent->localAnimIndex < 0 || ent->localAnimIndex > 1 ) {
		return qfalse;
	}
	anims = bgAllAnims[ent->localAnimIndex].anims;
	if ( !anims || !anims[VRA_POSE].numFrames || legs < 0 || legs >= MAX_ANIMATIONS || torso < 0 || torso >= MAX_ANIMATIONS ) {
		return qfalse;
	}
	if ( cl->ps.m_iVehicleNum || es->m_iVehicleNum || cl->ps.weapon == WP_EMPLACED_GUN || es->heldByClient ||
		( cl->ps.eFlags2 & EF2_HELD_BY_MONSTER ) || es->forceFrame || cl->ps.saberLockFrame || es->brokenLimbs ||
		( es->eFlags & ( EF_DEAD | EF_RAG ) ) || G_VRPosed( ent ) ) {
		return qfalse;
	}
	if ( es->iModelScale && ( es->iModelScale < 50 || es->iModelScale > 200 ) ) {
		return qfalse;
	}
	// (their legs upright: never a roll, a flip, a special jump or attack, a
	// knockdown or getting up, a lock or a death - the game plays those on the
	// whole body)
	if ( G_VRSaberStandingAnim( ent, legs ) || PM_WalkingAnim( legs ) || PM_RunningAnim( legs ) || PM_JumpingAnim( legs ) ) {
		return qtrue;
	}
	switch ( legs ) {
	case BOTH_STAND5:
	case BOTH_STAND3:
	case BOTH_STAND4:
	case BOTH_CROUCH1:
	case BOTH_CROUCH1IDLE:
	case BOTH_CROUCH1WALK:
	case BOTH_CROUCH1WALKBACK:
	case BOTH_STRAFE_LEFT1:
	case BOTH_STRAFE_RIGHT1:
	case BOTH_TURN_LEFT1:
	case BOTH_TURN_RIGHT1:
	case BOTH_INAIR1:
		return qtrue;
	default:
		return qfalse;
	}
}

// The right arm (rhumerus, rradius) and the saber's own bone
// (rhang_tag_bone), as a normal player's game turns them: M[0..2].
static qboolean VRA_Right( vrArms_t *va, const vraBody_t *body, const vraFrame_t *bone, const vec3_t bladeTag,
	const vec3_t p, const vec3_t blade, qboolean reversed, float dt, vraMat_t M[3] ) {
	vraRight_t j;
	vraTry_t best, t;
	vec3_t a, kw, centre, pref;
	vraMat_t RT1;
	float psi, psi0, r, dist;
	int i;
	const vraFrame_t *hum = &bone[VRAB_RHUMERUS], *rad = &bone[VRAB_RRADIUS], *tag = &bone[VRAB_RTAG];

	memset( &j, 0, sizeof( j ) );
	VectorCopy( hum->origin, j.S );
	VRA_Copy( j.RU0, (float (*)[3])hum->R );
	VRA_Copy( j.RF0, (float (*)[3])rad->R );
	VectorSubtract( rad->origin, hum->origin, a );
	j.Lu = VectorLength( a );
	VRA_ApplyT( j.RU0, a, j.u );
	VectorSubtract( tag->origin, rad->origin, a );
	VRA_ApplyT( j.RF0, a, j.pf );
	j.Lf = VectorLength( j.pf );
	if ( j.Lu < 1.0f || j.Lf < 1.0f ) {
		return qfalse;
	}
	if ( !VRA_KnuckleWay( bone[VRAB_RHAND].origin, bone[VRAB_RD2].origin, bone[VRAB_RD4].origin, kw ) ) {
		return qfalse;
	}
	VRA_ApplyT( j.RF0, kw, j.bf );
	VectorSubtract( bone[VRAB_RHAND].origin, rad->origin, a );
	if ( VectorNormalize( a ) < 1e-3f ) {
		return qfalse;
	}
	VRA_ApplyT( j.RF0, a, j.fdir );
	j.alongF = DotProduct( j.pf, j.fdir ) / j.Lf;
	VectorCopy( p, j.p );
	VectorScale( blade, reversed ? -1.0f : 1.0f, j.d );

	// the elbow's circle: round the line from the shoulder to the tag's point
	// (as near as the arm reaches it)
	VectorSubtract( p, j.S, j.k );
	dist = VectorNormalize( j.k );
	if ( dist < 1e-3f ) {
		VectorCopy( body->fwd, j.k );
	}
	r = Com_Clamp( fabsf( j.Lu - j.Lf ) + 1e-3f, j.Lu + j.Lf - 1e-3f, dist );
	j.along = ( j.Lu * j.Lu + r * r - j.Lf * j.Lf ) / ( 2.0f * r );
	j.hh = sqrtf( Q_max( 0.0f, j.Lu * j.Lu - j.along * j.along ) );
	PerpendicularVector( j.x, j.k );
	CrossProduct( j.k, j.x, j.y );
	VectorMA( j.S, r, j.k, j.W );

	// where an arm hangs that elbow: down, out to its side, a little back
	VectorScale( body->up, -VRA_ELBOW_DOWN, pref );
	VectorMA( pref, VRA_ELBOW_OUT, body->right, pref );
	VectorMA( pref, -VRA_ELBOW_BACK, body->fwd, pref );
	if ( !VRA_Square2( pref, j.k, pref ) && !VRA_Square2( body->fwd, j.k, pref ) ) {
		VectorCopy( j.x, pref );
	}
	VectorMA( j.S, j.along, j.k, centre );
	VectorMA( centre, j.hh, pref, j.elv );

	// from where it was (in the body's space, turned and moved with it)
	j.dt = dt;
	VRA_Copy( j.Rref, j.RF0 );
	if ( va->haveRight ) {
		vec3_t was, elvWas;
		VRA_FromBody( body, va->rightE, was );
		VRA_FromBody( body, va->rightElv, elvWas );
		VectorSubtract( j.elv, elvWas, a );
		VectorAdd( was, a, j.stay );
		j.haveStay = qtrue;
		VRA_Mul( (float (*)[3])body->R, va->rightRF, j.Rref );
		VRA_Square( j.Rref );
		j.haveTwist = qtrue;
		j.twistWas = va->rightTwist;
	}

	// the best elbow round the circle: every VRA_PSI_STEP, then finer
	best.cost = 1e30f;
	psi0 = 0.0f;
	for ( psi = 0.0f; psi < 360.0f - 0.01f; psi += VRA_PSI_STEP ) {
		VRA_RightTry( &j, DEG2RAD( psi ), &t );
		if ( t.cost < best.cost ) {
			best = t;
			psi0 = psi;
		}
	}
	for ( i = -4; i <= 4; i++ ) {
		if ( !i ) {
			continue;
		}
		VRA_RightTry( &j, DEG2RAD( psi0 + i * VRA_PSI_FINE ), &t );
		if ( t.cost < best.cost ) {
			best = t;
		}
	}

	// kept (in the body's space)
	VRA_ToBody( body, best.E, va->rightE );
	VRA_ToBody( body, j.elv, va->rightElv );
	VRA_MulTA( (float (*)[3])body->R, best.RF, va->rightRF );
	va->rightTwist = Com_Clamp( VRA_TWIST_LO, VRA_TWIST_HI, best.twist );	// (a rate's from within)
	va->haveRight = qtrue;

	// the turns on top of the pose: the upper arm, the forearm on it, and
	// the saber's own bone on that (its blade exactly theirs)
	VRA_MulTA( j.RU0, best.RU, M[0] );		// RU0^T RU
	// (the forearm, its upper arm turned, as posed on it: RU RU0^T RF0 - so
	// its turn on top of that: RF0^T RU0 RU^T RF)
	{
		vraMat_t A, B;
		VRA_MulTB( j.RU0, best.RU, A );		// RU0 RU^T
		VRA_Mul( A, best.RF, B );			// RU0 RU^T RF
		VRA_MulTA( j.RF0, B, M[1] );		// RF0^T RU0 RU^T RF
	}
	// the tag as the arm now holds it, before its own turn: RF RF0^T RT0;
	// turned the least that puts the blade's way (bladeTag, in its frame) on
	// the blade's
	{
		vec3_t want;
		vraMat_t A;
		VRA_MulTB( best.RF, j.RF0, A );
		VRA_Mul( A, (float (*)[3])tag->R, RT1 );
		VRA_ApplyT( RT1, blade, want );
		VRA_Align( bladeTag, want, M[2] );
	}
	return qtrue;
}

// The left upper arm (lhumerus) swung to put the hand on target; its elbow
// round the line to it as an arm hangs it - or, holding a hilt (way: along
// it, toward the blade), the knuckles along it - from where it was: M.
static void VRA_Left( vrArms_t *va, const vraBody_t *body, const vraFrame_t *bone, const vec3_t target, const vec3_t way,
	qboolean onHilt, float dt, vraMat_t M ) {
	const vraFrame_t *hum = &bone[VRAB_LHUMERUS];
	vec3_t v0, vt, e0, kw0, pref, ew, kw, ePrev;
	vraMat_t turn, T, RU;
	float best = 1e30f, bestPhi = 0.0f, phiPrev = 0.0f, most = -1.0f;
	qboolean havePrev = qfalse;
	int i;

	VectorSubtract( bone[VRAB_LTAG].origin, hum->origin, v0 );
	VectorSubtract( target, hum->origin, vt );
	if ( VectorNormalize( v0 ) < 1e-3f || VectorNormalize( vt ) < 1e-3f ) {
		VRA_Identity( M );
		return;
	}
	VRA_Align( v0, vt, turn );
	VectorSubtract( bone[VRAB_LRADIUS].origin, hum->origin, e0 );
	VRA_Apply( turn, e0, e0 );
	if ( !VRA_Square2( e0, vt, e0 ) ) {
		VRA_Identity( M );
		return;
	}
	if ( !VRA_KnuckleWay( bone[VRAB_LHAND].origin, bone[VRAB_LD2].origin, bone[VRAB_LD4].origin, kw0 ) ) {
		VectorClear( kw0 );
	}
	VRA_Apply( turn, kw0, kw0 );

	// where an arm hangs the elbow: down, out to its side, a little back
	VectorScale( body->up, -VRA_ELBOW_DOWN, pref );
	VectorMA( pref, -VRA_ELBOW_OUT, body->right, pref );
	VectorMA( pref, -VRA_ELBOW_BACK, body->fwd, pref );
	if ( !VRA_Square2( pref, vt, pref ) ) {
		VectorCopy( e0, pref );
	}

	// from where it was
	if ( va->haveLeft ) {
		vraMat_t was;
		vec3_t u;
		VRA_Mul( (float (*)[3])body->R, va->leftRU, was );
		VRA_Square( was );
		VectorSubtract( bone[VRAB_LRADIUS].origin, hum->origin, u );
		VRA_ApplyT( (float (*)[3])hum->R, u, u );
		VRA_Apply( was, u, ePrev );
		if ( VRA_Square2( ePrev, vt, ePrev ) ) {
			phiPrev = VRA_AngleAbout( e0, ePrev, vt );
			havePrev = qtrue;
			most = DEG2RAD( VRA_LEFT_RATE ) * dt;
		}
	}

	// round the line: the best place for the elbow
	for ( i = 0; i < VRA_LEFT_STEPS; i++ ) {
		const float phi = havePrev ? phiPrev + ( 2.0f * M_PI * i / VRA_LEFT_STEPS - M_PI ) : 2.0f * M_PI * i / VRA_LEFT_STEPS;
		float c, phiAt = phi;
		if ( most >= 0.0f ) {
			phiAt = phiPrev + Com_Clamp( -most, most, AngleNormalize180( RAD2DEG( phi - phiPrev ) ) * ( M_PI / 180.0f ) );
		}
		VRA_AxisAngle( vt, phiAt, T );
		VRA_Apply( T, e0, ew );
		// (the elbow off where an arm hangs it, on the circle: 0..2)
		c = 1.0f - DotProduct( ew, pref );
		if ( onHilt && VectorLengthSquared( kw0 ) > 0.5f ) {
			// (the knuckles along the hilt matter more; a raised elbow a little)
			VRA_Apply( T, kw0, kw );
			c = 4.0f * ( 1.0f - DotProduct( kw, way ) ) + VRA_LEFT_ELBOW_UP * c * c;
		}
		if ( c < best ) {
			best = c;
			bestPhi = phiAt;
		}
	}
	VRA_AxisAngle( vt, bestPhi, T );
	VRA_Mul( T, turn, T );
	VRA_Mul( T, (float (*)[3])hum->R, RU );
	VRA_MulTA( (float (*)[3])hum->R, RU, M );
	VRA_MulTA( (float (*)[3])body->R, RU, va->leftRU );
	va->haveLeft = qtrue;
}

// Where on the hilt (its line through p, the blade's way d: p - s d) a hand
// reaching reach from S holds it: as near s0 as it can, no further than
// VRA_SLIDE_RATE from where it held it.
static float VRA_OnHilt( vrArms_t *va, const vec3_t S, float reach, const vec3_t p, const vec3_t d, float s0, float dt ) {
	vec3_t a;
	float b, c, disc, s = s0, lo = VRA_HILT_LO, hi = VRA_HILT_FAR;

	if ( va->haveSlide ) {
		lo = Q_max( lo, va->slide - VRA_SLIDE_RATE * dt );
		hi = Q_min( hi, va->slide + VRA_SLIDE_RATE * dt );
		if ( lo > hi ) {
			lo = hi = Com_Clamp( VRA_HILT_LO, VRA_HILT_FAR, va->slide );
		}
	}
	// |p - s d - S| = reach: s^2 - 2 s (a.d) + |a|^2 - reach^2 = 0
	VectorSubtract( p, S, a );
	b = DotProduct( a, d );
	c = DotProduct( a, a ) - reach * reach;
	disc = b * b - c;
	if ( disc >= 0.0f ) {
		const float r1 = b - sqrtf( disc ), r2 = b + sqrtf( disc );
		const qboolean in1 = (qboolean)( r1 >= lo && r1 <= hi ), in2 = (qboolean)( r2 >= lo && r2 <= hi );
		if ( in1 && in2 ) {
			s = ( fabsf( r1 - s0 ) < fabsf( r2 - s0 ) ) ? r1 : r2;
		} else if ( in1 || in2 ) {
			s = in1 ? r1 : r2;
		} else {
			// (neither within: the nearer end of the range to either)
			const float e1 = Q_min( fabsf( r1 - lo ), fabsf( r1 - hi ) ), e2 = Q_min( fabsf( r2 - lo ), fabsf( r2 - hi ) );
			s = Com_Clamp( lo, hi, ( e1 < e2 ) ? r1 : r2 );
		}
	} else {
		s = Com_Clamp( lo, hi, b );	// (it reaches no point of it: the nearest)
	}
	va->slide = s;
	va->haveSlide = qtrue;
	return s;
}

// A turn as the angles a game builds it back from (G2_Generate_Matrix with
// POSITIVE_Z, NEGATIVE_Y, POSITIVE_X: Create_Matrix, its columns AnglesToAxis').
static void VRA_Angles( vraMat_t M, vec3_t angles ) {
	vec3_t axis[3];
	int r, c;
	for ( c = 0; c < 3; c++ ) {
		for ( r = 0; r < 3; r++ ) {
			axis[c][r] = M[r][c];
		}
	}
	VR_AxisToAngles( axis, angles );
}

// This VR player's arms, worked out for the entity the other builds are
// sent (es: theirs, the held pose and its angles in it).
static qboolean VRA_Work( vrArms_t *va, gentity_t *ent, const entityState_t *es, const vrAvatar_t *av, int saberHand ) {
	vraFrame_t bone[VRAB_NUM];
	vraBody_t body;
	vec3_t origin, scale, p, blade, bladeTag, entOrigin;
	vraMat_t M[4];
	const vrAvatarHand_t *sh = &av->hand[saberHand], *oh = &av->hand[!saberHand];
	float dt;
	int b;

	VRA_Animate( va, ent, es );
	VRA_Place( va, ent, es, origin, scale );
	for ( b = VRAB_RHUMERUS; b < VRAB_NUM; b++ ) {
		if ( !VRA_Bone( va, 0, va->bolt[b], origin, scale, &bone[b] ) ) {
			return qfalse;
		}
	}
	// the blade's way, in the saber tag's own frame (where it hangs the saber
	// model it holds: its first blade's bolt)
	{
		vraFrame_t bladeBolt;
		vec3_t w;
		if ( !trap->G2API_HasGhoul2ModelOnIndex( &va->g2, 1 ) || !VRA_Bone( va, 1, 0, origin, scale, &bladeBolt ) ) {
			return qfalse;
		}
		// (the blade runs down its bolt's relabelled -Y, as the games draw
		// it: its own -X)
		VectorSet( w, -bladeBolt.R[0][0], -bladeBolt.R[1][0], -bladeBolt.R[2][0] );
		VRA_ApplyT( bone[VRAB_RTAG].R, w, bladeTag );
		if ( VectorNormalize( bladeTag ) < 0.5f ) {
			return qfalse;
		}
	}

	// where the hands are: offsets from the origin the entity is drawn at
	BG_EvaluateTrajectory( &es->pos, level.time, entOrigin );
	VRA_BodyFrame( entOrigin, es->apos.trBase[YAW], &body );
	VRA_ToBody( &body, bone[VRAB_LHUMERUS].origin, va->shoulderL );	// (for the next frame's pose)
	va->haveShoulderL = qtrue;
	VectorCopy( sh->dir, blade );
	if ( VectorNormalize( blade ) < 0.5f ) {
		return qfalse;
	}
	VectorAdd( entOrigin, sh->origin, p );
	VectorMA( p, -sh->hilt, blade, p );		// (the fist's this far along the hilt from where the tag holds it)

	dt = ( va->active && level.time - va->activeTime > 0 && level.time - va->activeTime <= VRA_GAP ) ?
		( level.time - va->activeTime ) / 1000.0f : 0.0f;
	if ( dt <= 0.0f ) {
		va->haveRight = va->haveLeft = va->haveSlide = qfalse;
	}
	if ( !VRA_Right( va, &body, bone, bladeTag, p, blade, sh->reversed, dt, M ) ) {
		return qfalse;
	}

	// the left hand: on the hilt where it reaches it, or their second saber's
	// tag, or their hand
	if ( oh->grip == VR_GRIP_HILT && oh->saber == 0 ) {
		vec3_t hand, target, a;
		float s0, reach, s;
		VectorAdd( entOrigin, oh->origin, hand );
		VectorSubtract( p, hand, a );
		s0 = DotProduct( a, blade );
		VectorSubtract( bone[VRAB_LTAG].origin, bone[VRAB_LHUMERUS].origin, a );
		reach = VectorLength( a );
		s = VRA_OnHilt( va, bone[VRAB_LHUMERUS].origin, reach, p, blade, s0, dt );
		VectorMA( p, -s, blade, target );
		VectorScale( blade, oh->reversed ? -1.0f : 1.0f, a );
		VRA_Left( va, &body, bone, target, a, qtrue, dt, M[3] );
	} else if ( oh->grip > VR_GRIP_NONE && oh->grip < VR_NUM_GRIPS ) {
		vec3_t target, way;
		va->haveSlide = qfalse;
		VectorAdd( entOrigin, oh->origin, target );
		if ( oh->grip == VR_GRIP_SABER ) {
			VectorCopy( oh->dir, way );
			if ( VectorNormalize( way ) > 0.5f ) {
				VectorMA( target, -oh->hilt, way, target );
				VRA_Left( va, &body, bone, target, way, qtrue, dt, M[3] );	// (their second saber: its knuckles along it)
			} else {
				VRA_Left( va, &body, bone, target, vec3_origin, qfalse, dt, M[3] );
			}
		} else {
			VRA_Left( va, &body, bone, target, vec3_origin, qfalse, dt, M[3] );
		}
	} else {
		va->haveSlide = va->haveLeft = qfalse;
		VRA_Identity( M[3] );		// (not on a controller: the pose has it)
	}

	for ( b = 0; b < VR_ARMS_BONES; b++ ) {
		VRA_Angles( M[b], va->view.boneAngles[b] );
	}
	return qtrue;
}

// The held pose for this frame: the first unless both hands hold the hilt;
// then the one whose left hand reaches it best from where their left shoulder
// was (kept unless another reaches it much better, and not changed again soon).
static int VRA_Pose( vrArms_t *va, gentity_t *ent, const vrAvatar_t *av, int saberHand, const vec3_t entOrigin, float yaw ) {
	const vrAvatarHand_t *sh = &av->hand[saberHand], *oh = &av->hand[!saberHand];
	const animation_t *anims = bgAllAnims[ent->localAnimIndex].anims;
	const float scale = ent->s.iModelScale ? ent->s.iModelScale / 100.0f : 1.0f;
	float miss[ARRAY_LEN( vraPoses )];
	vec3_t d, p, a, b, S, x;
	vraBody_t body;
	int c, want = 0;

	if ( va->pose < 0 || va->pose >= VRA_NUM_POSES || !anims[vraPoses[va->pose].anim].numFrames ) {
		va->pose = 0;
		va->poseSince = level.time;
	}
	if ( oh->grip == VR_GRIP_HILT && oh->saber == 0 ) {
		float dmin, dmax, t;
		VectorCopy( sh->dir, d );
		if ( VectorNormalize( d ) < 0.5f ) {
			return va->pose;
		}
		VectorAdd( entOrigin, sh->origin, p );
		VectorMA( p, -sh->hilt, d, p );
		VRA_BodyFrame( entOrigin, yaw, &body );
		if ( va->haveShoulderL ) {
			VRA_FromBody( &body, va->shoulderL, S );
		} else {
			VectorSet( x, 2.7f * scale, 6.0f * scale, 27.3f * scale );	// (where it is in the first, standing)
			VRA_FromBody( &body, x, S );
		}
		// the hilt where a second hand holds it: p - s d, s VRA_HILT_LO..VRA_HILT_FAR
		VectorMA( p, -VRA_HILT_LO, d, a );
		VectorMA( p, -VRA_HILT_FAR, d, b );
		VectorSubtract( S, a, x );
		t = Com_Clamp( 0.0f, VRA_HILT_FAR - VRA_HILT_LO, -DotProduct( x, d ) );
		VectorMA( a, -t, d, x );
		dmin = Distance( x, S );
		dmax = Q_max( Distance( a, S ), Distance( b, S ) );
		for ( c = 0; c < VRA_NUM_POSES; c++ ) {
			const float r = vraPoses[c].reach * scale;
			miss[c] = !anims[vraPoses[c].anim].numFrames ? 1e9f : ( r < dmin ) ? dmin - r : ( r > dmax ) ? r - dmax : 0.0f;
			if ( miss[c] < miss[want] ) {
				want = c;
			}
		}
		if ( want != va->pose && miss[va->pose] - miss[want] <= VRA_SWAP_GAIN ) {
			want = va->pose;
		}
	}
	if ( want != va->pose && level.time - va->poseSince >= VRA_SWAP_HOLD ) {
		va->pose = want;
		va->poseSince = level.time;
		va->haveSlide = qfalse;		// (the hand takes the hilt where this pose's arm reaches it)
	}
	return va->pose;
}

/*
--------------------------------------------------------------------------------
Every frame
--------------------------------------------------------------------------------
*/

static qboolean VRA_EngineTakes( void ) {
	if ( !vraEngineChecked ) {
		vraEngineChecked = qtrue;
		vraEngine = (qboolean)( G_VRPosesAvailable() && trap->ext.VR_SetArmsView != NULL );
	}
	return vraEngine;
}

static void VRA_Send( int clientNum, vrArms_t *va ) {
	trap->ext.VR_SetArmsView( clientNum, va->view.set ? &va->view : NULL );
}

// After everyone's moved and their entities are made (ClientEndFrame): each
// VR player's arms as the other builds are sent them.
void G_VRArmsFrame( void ) {
	int i, b;

	if ( !VRA_EngineTakes() ) {
		return;
	}
	for ( i = 0; i < level.maxclients && i < MAX_CLIENTS; i++ ) {
		gentity_t *ent = &g_entities[i];
		vrArms_t *va = &vrArms[i];
		vrAvatar_t av;
		int saberHand = -1;
		qboolean want;

		if ( !ent->inuse || !ent->client || ent->client->pers.connected != CON_CONNECTED ) {
			continue;
		}
		want = (qboolean)( g_vrArmsForAll.integer && G_VRSaberAvatar( i, &av ) && VRA_Wanted( ent, &av, &saberHand ) );
		if ( !want && !va->everSent && !va->g2 ) {
			continue;	// (never a VR player's arms here)
		}
		if ( !vraBoneIndex[0] ) {	// (the bones' names given out once someone's arms are: the table's small)
			for ( b = 0; b < VR_ARMS_BONES; b++ ) {
				vraBoneIndex[b] = G_BoneIndex( vraSentBones[b] );
			}
		}
		if ( !VRA_Model( va, ent ) ) {
			want = qfalse;
		}
		if ( want ) {
			entityState_t es = ent->s;
			if ( !va->active ) {
				va->torsoFlipHeld = ent->s.torsoFlip;
				va->pose = 0;
				va->poseSince = level.time;
				va->haveShoulderL = qfalse;
			}
			if ( G_VRSaberStandingAnim( ent, es.legsAnim ) && bgAllAnims[ent->localAnimIndex].anims[BOTH_STAND5].numFrames ) {
				es.legsAnim = BOTH_STAND5;	// (standing straight, as their VR body is drawn: not in a saber stance)
				es.legsFlip = qfalse;
			}
			{
				vec3_t entOrigin;
				BG_EvaluateTrajectory( &ent->s.pos, level.time, entOrigin );
				es.torsoAnim = vraPoses[VRA_Pose( va, ent, &av, saberHand, entOrigin, av.bodyYaw )].anim;
			}
			es.torsoFlip = va->torsoFlipHeld;
			es.apos.trBase[PITCH] = AngleNormalize180( ent->s.apos.trBase[PITCH] );
			es.apos.trBase[YAW] = AngleMod( av.bodyYaw );
			es.apos.trBase[ROLL] = 0.0f;
			want = VRA_Work( va, ent, &es, &av, saberHand );
			if ( want ) {
				va->view.set = qtrue;
				va->view.pose = qtrue;
				va->view.legsAnim = es.legsAnim;
				va->view.legsFlip = es.legsFlip;
				va->view.torsoAnim = es.torsoAnim;
				va->view.torsoFlip = es.torsoFlip;
				VectorCopy( es.apos.trBase, va->view.angles );
				va->active = qtrue;
				va->activeTime = level.time;
				va->everSent = qtrue;
			}
		}
		if ( !want ) {
			// the entity as it is (its anims still followed: what a pose
			// starting next blends from), the bones unturned if they were sent
			if ( va->g2 ) {
				VRA_Animate( va, ent, &ent->s );
			}
			va->active = qfalse;
			va->haveRight = va->haveLeft = va->haveSlide = qfalse;
			va->anglesTime = 0;
			va->view.set = va->everSent;
			va->view.pose = qfalse;
			for ( b = 0; b < VR_ARMS_BONES; b++ ) {
				VectorClear( va->view.boneAngles[b] );
			}
		}
		for ( b = 0; b < VR_ARMS_BONES; b++ ) {
			va->view.boneIndex[b] = vraBoneIndex[b];
		}
		va->view.boneOrient = POSITIVE_X | ( NEGATIVE_Y << 3 ) | ( POSITIVE_Z << 6 );
		if ( va->view.set ) {
			VRA_Send( i, va );
		}
	}
}

// A player connected or left: nothing of whoever had their slot.
void G_VRArmsClientReset( int clientNum ) {
	vrArms_t *va;

	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return;
	}
	va = &vrArms[clientNum];
	VRA_FreeModel( va );
	memset( va, 0, sizeof( *va ) );
	if ( VRA_EngineTakes() ) {
		trap->ext.VR_SetArmsView( clientNum, NULL );
	}
}

// A player's model or sabers were set up again: their stand-in follows.
void G_VRArmsModelChanged( gentity_t *ent ) {
	const int n = ent ? ent->s.number : -1;
	if ( n >= 0 && n < MAX_CLIENTS && ent->client ) {
		VRA_FreeModel( &vrArms[n] );
	}
}

// The game going (restart: a map_restart - the players stay, and so do their
// models in the other games, any bones turned on them: those whose arms were
// sent are sent the bones unturned after it).
void G_VRArmsShutdown( int restart ) {
	qboolean sent[MAX_CLIENTS];
	int i;
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		sent[i] = (qboolean)( restart && vrArms[i].everSent );
		VRA_FreeModel( &vrArms[i] );
	}
	memset( vrArms, 0, sizeof( vrArms ) );
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		vrArms[i].everSent = sent[i];
	}
	memset( vraBoneIndex, 0, sizeof( vraBoneIndex ) );
	vraEngineChecked = qfalse;
}
