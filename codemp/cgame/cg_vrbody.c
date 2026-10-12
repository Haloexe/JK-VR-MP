/*
===========================================================================
OpenJK VR (multiplayer) - your own body in first person.

While you play in a headset with a gun or the saber out, your character's
model is drawn around you: it stands under your head, turns with you, and
its hands are on your controllers, gripping what they hold. Nothing here is
tied to one character. Every frame the model's own skeleton is measured
(shoulders, elbows, wrists, knuckles, the "held item" tag, eyes), so any
model built on the Jedi Academy humanoid skeleton - the stock characters and
custom models alike - gets arms that fit its proportions.

How the arms work: the game animates the model as usual, then
  - the upper arm and forearm bones are re-aimed with Ghoul2 "post-multiply"
    bone overrides (an extra rotation in the bone's own frame) so the wrist
    lands where the hand must be,
  - the hand is turned at the wrist with an engine bone transform. In this
    skeleton the finger bones hang off the forearm next to the hand bone, so
    the hand and every finger bone are turned together about the wrist
    (which bones need it is checked per model, see CG_VRB_ProbeHands).
Where the hand must be comes from what it holds: a gun's grip is put in the
hand exactly the way the game holds it in third person (the hand's "*r_hand"
tag is where a weapon model's origin sits), and the saber's blade points
along your fist, like a real hilt.

Both hands on one saber: squeezing the left grip with the left hand at the
right hand's hilt takes hold of it (the engine then doesn't use the Force
with that press, see VR_SetGrabZones). The left hand closes around the hilt
and slides along it with your hand; the blade follows both wrists and,
the further apart the hands, the line through them, so pushing with one
hand and pulling with the other levers it round. Letting go of the grip, or
pulling the hand well away, lets go.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "cg_local.h"
#include "ghoul2/G2.h"

#define VRB_TURN_OFF		0x00000100	// G2SURFACEFLAG_NODESCENDANTS: surface and its children off
#define VRB_HEAD_BIT		( 1 << ( G2_MODELPART_HEAD - 10 ) )

enum {
	VRB_UPPER,		// upper arm (shoulder joint)
	VRB_FORE,		// forearm (elbow joint)
	VRB_HAND,		// hand (wrist joint)
	VRB_THUMB,		// thumb base
	VRB_INDEX,		// index finger knuckle
	VRB_LITTLE,		// ring/little finger knuckle
	VRB_TAG,		// where a held item sits (a weapon model's origin)
	VRB_NUM_ARM_BONES
};

static const char *const vrbArmBones[2][VRB_NUM_ARM_BONES] = {
	{ "lhumerus", "lradius", "lhand", "l_d1_j1", "l_d2_j1", "l_d4_j1", "*l_hand" },	// VR_HAND_LEFT
	{ "rhumerus", "rradius", "rhand", "r_d1_j1", "r_d2_j1", "r_d4_j1", "*r_hand" },	// VR_HAND_RIGHT
};

// Everything that makes up a hand; the first one is the hand bone itself.
#define VRB_MAX_HAND_BONES	17
static const char *const vrbHandBones[2][VRB_MAX_HAND_BONES] = {
	{ "lhand", "lhang_tag_bone",
	  "l_d1_j1", "l_d1_j2", "l_d1_j3", "l_d2_j1", "l_d2_j2", "l_d2_j3", "l_d3_j1", "l_d3_j2", "l_d3_j3",
	  "l_d4_j1", "l_d4_j2", "l_d4_j3", "l_d5_j1", "l_d5_j2", "l_d5_j3" },
	{ "rhand", "rhang_tag_bone",
	  "r_d1_j1", "r_d1_j2", "r_d1_j3", "r_d2_j1", "r_d2_j2", "r_d2_j3", "r_d3_j1", "r_d3_j2", "r_d3_j3",
	  "r_d4_j1", "r_d4_j2", "r_d4_j3", "r_d5_j1", "r_d5_j2", "r_d5_j3" },
};

// The fingers that close round a hilt: thumb, index, and the rest (the
// humanoid skeleton moves the last three together), each a first and a
// second joint, as indices into vrbHandBones.
#define VRB_FINGERS			3
static const int vrbFingerJ1[VRB_FINGERS] = { 2, 5, 11 };	// r_d1_j1, r_d2_j1, r_d4_j1 (j2 is the next one)

// G2API_GetBoltMatrix hands game code its axes relabelled ("this 90 degree
// offset thing", G2_API.cpp). The overrides need the bone's real axes. The
// relabelling is undone one way below; the arm solver also checks its own
// result against the skeleton and settles on whichever of these makes the
// hand land where it was sent (see CG_VRB_Arms), so a wrong guess here can
// only cost a few frames, never leave the arms twisted.
#define VRB_NUM_CONVENTIONS	3

// how strongly elbows are pulled down/out, against following the hand's turn
static float vrbElbowPull = 0.4f;

typedef float vrbMat_t[3][3];		// rotation, row-major, acts on column vectors

typedef struct {
	vec3_t		origin;
	vec3_t		axis[3];			// the bone's own axes in the world
} vrbBone_t;

// what a hand holds
enum {
	VRB_GRIP_FREE,					// nothing: the hand rests on the controller
	VRB_GRIP_GUN,					// the gun drawn at the controller (cg_weapons.c)
	VRB_GRIP_SABER,					// a saber bolted to this hand
	VRB_GRIP_HILT					// the other hand's saber, held with this one too
};

typedef struct {
	int			kind;
	vec3_t		origin;				// GUN: the gun model's origin (its grip); HILT: where on the hilt
	vec3_t		axis[3];			// GUN: the gun model's axes; HILT: the controller's
	vec3_t		dir;				// HILT: the way the hilt runs through the fist (toward the thumb)...
	float		blend;				// ...followed this much (0: the hand just rests on the controller)
	int			saberModel;			// SABER: its Ghoul2 model index on the player
} vrbGrip_t;

// both hands on one saber
#define VRB_GRAB_REACH		5.0f	// a hand this close to the other hand's hilt can take hold of it
#define VRB_GRAB_BREAK		24.0f	// ...and lets go only when pulled this far from its place on it
#define VRB_HAND_GAP		3.5f	// two hands on a hilt are at least a hand's width apart
#define VRB_HILT_BELOW		6.5f	// a second hand on a single saber holds it this far below the first at most
#define VRB_TWO_HAND_IN		0.08f	// seconds the hands take to settle onto the hilt together...
#define VRB_TWO_HAND_OUT	0.15f	// ...and to come apart again

typedef struct {
	qboolean	solved;
	qboolean	exact;				// well-bent arm in reach: the check below is meaningful
	vec3_t		elbow, wrist;		// where the solution puts them
	qboolean	tagTarget;			// the held-item tag was sent to an exact spot:
	vec3_t		tagOrigin;
	vec3_t		tagAxis[3];
	float		reachExcess;		// wanted shoulder->wrist distance minus the arm's reach
	vec3_t		reachDir;			// shoulder -> wrist
} vrbArmResult_t;

typedef struct vrbState_s {
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
	void		*wristG2;

	// where the entity was moved this frame (undone in CG_VR_BodyEndFrame)
	centity_t	*shiftedEnt;
	vec3_t		shift;

	// leaning in: how far the body is brought forward (toward your eyes) so
	// its arms reach where yours do
	float		lean;
	float		leanTarget;
	int			leanTime;
	int			leanSetTime;

	// which bones turn with the hand at the wrist (CG_VRB_ProbeHands)
	qboolean	probed;
	int			probeRetry;			// cg.time to try again after a failed probe
	void		*probeG2;
	char		probeModel[MAX_QPATH];
	qboolean	wristOk[2];
	qboolean	handBoneTurn[2][VRB_MAX_HAND_BONES];
	qboolean	fingerUnderJ1[2][3];	// a finger's second joint hangs off its first (VRB_FINGERS)
	void		*curlG2;				// fingers curled round a hilt on this instance (CG_VRB_CurlFingers)
	float		curlLog[2][3];			// (the log: how far each finger closed, degrees...
	float		curlOff[2];				// ...and how far the hand was from the hilt's axis; -1: not holding one)
	qboolean	tagMoves[2];			// the held-item tag hangs on the tag bone: a saber can be moved in the hand (CG_VRB_PlaceSaber)
	int			curlGeoTime[2];			// (...next time to log the hand round the hilt in full)

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

	// the VR body drawn this frame (CG_VR_BodyPose)
	centity_t	*bodyCent;
	int			bodyTime;

	// the hands put on the controllers (CG_VRB_SolveArm)
	int			armTime[2];			// cg.time they last were
	vec3_t		armGrip[2];			// world: the controller grip the hand holds then

	// both hands on the saber (CG_VRB_TwoHand, once per frame)
	int			twoHandTime;		// cg.time it was worked out for
	qboolean	twoHand;			// the left hand holds the right hand's saber too
	qboolean	twoHandBroke;		// pulled away: not again until the grip is let go
	float		twoHandSign;		// -1: the left hand holds it the other way round (this hold)
	float		twoHandSide;		// +1: the left hand above the right one (toward the blade), -1 below
	float		twoHandT;			// where along the hilt it holds it (from the right hand, toward the blade)
	vec3_t		twoHandGrip[3];		// how it lies round the hilt: the left controller's axes in the hilt's frame
	float		twoHandBlend;		// 0..1: how far the hold has taken the hands over (eases in and out)
	qboolean	twoHandArms;		// the arms are to show it this frame (twoHandBlend > 0)
	float		twoHandEase;		// ...this much
	vec3_t		twoHandDir;			// where the blade points, from both hands
	vec3_t		twoHandPoint;		// where the left hand holds the hilt
	vec3_t		twoHandRel[2];		// both, in the right controller's frame (to carry them while letting go)
	vec3_t		twoHandHand;		// the left hand's place, the controller eased onto the hilt
	vec3_t		twoHandAxis[3];		// the left hand's frame (a controller gripping the hilt)
	vec3_t		twoHandBladeLocal;	// the blade in the frame of the hand tag it is bolted to (raw axes)
	vec3_t		twoHandGrip2;		// the left controller's grip (world)
	int			grabZones;			// hands that could take hold of something now (engine)
	void		*fistG2[2];			// that hand's fingers are closed round a hilt on this instance
	void		*bonesLoggedG2;		// the log has said which finger bones this model has
	vec3_t		twoHandFist;		// the way the hilt runs through the left fist, as drawn

	// the blade's weight (CG_VRB_BladeInertia, once per frame)
	int			bladeTime;
	struct {
		qboolean	valid;
		int			time;
		vec3_t		dir;			// the blade as drawn, in your room
		vec3_t		vel;			// how it turns (rad/s, axis * rate)
		int			holdUntil;		// stopped on another blade until then
		vec3_t		out;			// the blade as drawn this frame (world)
		int			outTime;
	}			blade[2];

	// the whoosh of your swings (CG_VRB_SwingSound)
	struct {
		vec3_t		prev[VR_POSE_MAX_SABERS][VR_POSE_MAX_BLADES];	// room: a point two thirds along each blade
		int			prevMask[VR_POSE_MAX_SABERS];
		int			prevTime;
		float		speed;			// m/s, smoothed
		float		lag;			// degrees the blade trailed the hand by at most, since the last whoosh
		qboolean	armed;
		int			lastTime;		// the last whoosh
	}			swing;

	// the debug log (cg_vrLog)
	int			logTime;
	int			logTorso;
	float		logTip;
	int			logPrinted;

	// another player's body (CG_VRB_OtherUsable): their avatar this frame,
	// the hands offsets from where the player is drawn (worked out once a
	// frame: at this time, between these snapshots)
	int			avatarTime, avatarSnap, avatarNext;
	qboolean	avatarValid;
	vrAvatar_t	avatar;

	// your body: what the arm solver held each hand to this frame - the
	// avatar everyone else sees (CG_VRB_SolveArm, CG_VR_BodyEndFrame)
	vrAvatarHand_t	sent[2];
	int			sentTime[2];
} vrbState_t;

// The body being posed: yours, or - while CG_VRB_OtherArms and the rest pose
// another VR player's from their avatar - theirs. Every vrb.x is that one's.
static vrbState_t	vrbSelf;
static vrbState_t	vrbOthers[MAX_CLIENTS];
static vrbState_t	*vrbCur = &vrbSelf;
#define vrb			( *vrbCur )

// While another player's body is posed: their avatar (in the world), which
// stands in for the headset and controllers. NULL while yours is.
static const vrAvatar_t	*vrbRemote;

// Physical lightsaber: the blades in your hands this frame, reported to the
// engine (which sends them to the server with every usercmd)
static struct {
	int				time;			// cg.time they were collected for
	vrUserPose_t	pose;			// offsets from the camera, world axes
	vec3_t			base[VR_POSE_MAX_SABERS][VR_POSE_MAX_BLADES];	// world, to feel clashes
	vec3_t			tip[VR_POSE_MAX_SABERS][VR_POSE_MAX_BLADES];
	int				wallFeelTime;
} vrbSaber;

/*
--------------------------------------------------------------------------------
The VR log (cg_vrLog 1): vrdebug.log next to the settings, saying what your
body and saber did - two-handed holds, swings, clashes, and the animation and
fingers of the hand holding the saber - for working out what feels wrong.
--------------------------------------------------------------------------------
*/

static fileHandle_t	vrbLogFile;
static qboolean		vrbLogTried;

static void QDECL CG_VRB_Log( const char *fmt, ... ) {
	char text[1024];
	va_list ap;
	int n;

	if ( !cg_vrLog.integer ) {
		return;
	}
	if ( !vrbLogFile ) {
		qtime_t now;
		char version[128];
		if ( vrbLogTried ) {
			return;
		}
		vrbLogTried = qtrue;
		if ( trap->FS_Open( "vrdebug.log", &vrbLogFile, FS_APPEND_SYNC ) < 0 || !vrbLogFile ) {
			vrbLogFile = 0;
			return;
		}
		trap->RealTime( &now );
		trap->Cvar_VariableStringBuffer( "version", version, sizeof( version ) );
		n = Com_sprintf( text, sizeof( text ), "\n=== %04i-%02i-%02i %02i:%02i:%02i  %s  map %s ===\n", now.tm_year + 1900,
			now.tm_mon + 1, now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec, version, cgs.mapname );
		trap->FS_Write( text, n, vrbLogFile );
	}
	n = Com_sprintf( text, sizeof( text ), "%7.2f ", cg.time * 0.001f );
	va_start( ap, fmt );
	Q_vsnprintf( text + n, sizeof( text ) - n, fmt, ap );
	va_end( ap );
	trap->FS_Write( text, strlen( text ), vrbLogFile );
}

// (the log) A copy of a model file next to the log (vrdump/), once: the
// body, its skeleton and the saber, so the hands can be studied in detail
// away from the game. Returns the size copied (0: none, or already there).
static int CG_VRB_DumpFile( const char *path, char *head, int headSize ) {
	static char buf[65536];
	fileHandle_t in = 0, out = 0;
	int len, left, n, copied = 0;

	if ( !path[0] ) {
		return 0;
	}
	len = trap->FS_Open( va( "vrdump/%s", path ), &in, FS_READ );
	if ( in ) {
		trap->FS_Close( in );
		in = 0;
	}
	if ( len > 0 && !head ) {
		return 0;	// (copied before)
	}
	len = trap->FS_Open( path, &in, FS_READ );
	if ( !in || len <= 0 || len > 64 * 1024 * 1024 ) {
		if ( in ) {
			trap->FS_Close( in );
		}
		CG_VRB_Log( "model files: couldn't read %s\n", path );
		return 0;
	}
	if ( trap->FS_Open( va( "vrdump/%s", path ), &out, FS_WRITE ) < 0 || !out ) {
		trap->FS_Close( in );
		return 0;
	}
	for ( left = len; left > 0; left -= n ) {
		n = Q_min( left, (int)sizeof( buf ) );
		trap->FS_Read( buf, n, in );
		if ( head && left == len ) {
			memcpy( head, buf, Q_min( n, headSize ) );
		}
		trap->FS_Write( buf, n, out );
		copied += n;
	}
	trap->FS_Close( in );
	trap->FS_Close( out );
	CG_VRB_Log( "model files: copied %s (%d bytes) to vrdump\n", path, copied );
	return copied;
}

static void CG_VRB_DumpModel( const clientInfo_t *ci, const centity_t *cent ) {
	static char done[MAX_QPATH];
	char glm[MAX_QPATH], head[136], anim[65];
	char *slash;

	if ( !cg_vrLog.integer || !ci->modelName[0] || !Q_stricmp( done, ci->modelName ) ) {
		return;
	}
	Q_strncpyz( done, ci->modelName, sizeof( done ) );
	Com_sprintf( glm, sizeof( glm ), "models/players/%s/model.glm", ci->modelName );
	memset( head, 0, sizeof( head ) );
	if ( CG_VRB_DumpFile( glm, head, sizeof( head ) ) >= (int)sizeof( head ) ) {
		// the skeleton it uses (mdxmHeader_t: ident, version, name[64], animName[64])
		Q_strncpyz( anim, head + 72, sizeof( anim ) );
		if ( anim[0] ) {
			CG_VRB_DumpFile( va( "%s.gla", anim ), NULL, 0 );
			slash = strrchr( anim, '/' );
			if ( slash ) {
				*slash = '\0';
				CG_VRB_DumpFile( va( "%s/animation.cfg", anim ), NULL, 0 );
			}
		}
	}
	CG_VRB_DumpFile( ci->saber[0].model, NULL, 0 );
	CG_VRB_DumpFile( ci->saber[1].model, NULL, 0 );
	{
		// (and which frames the hands hold a hilt with: CG_VRB_Fist)
		const animation_t *anims = bgAllAnims[cent->localAnimIndex].anims;
		CG_VRB_Log( "model files: %s, saber %s; fists from BOTH_SABERDUAL_STANCE frame %d (%d frames), "
			"BOTH_SABERSTAFF_STANCE %d, BOTH_STAND2 %d\n", ci->modelName, ci->saber[0].model,
			anims[BOTH_SABERDUAL_STANCE].firstFrame, anims[BOTH_SABERDUAL_STANCE].numFrames,
			anims[BOTH_SABERSTAFF_STANCE].firstFrame, anims[BOTH_STAND2].firstFrame );
	}
}

// (CG_Shutdown)
void CG_VR_Shutdown( void ) {
	if ( vrbLogFile ) {
		trap->FS_Close( vrbLogFile );
		vrbLogFile = 0;
	}
	vrbLogTried = qfalse;
}

// The weight your saber swings with, as the server reckons it
// (g_vrsaber.c): the style's - and, if withHands, one heavier held with both
// hands (heavy stays heavy).
enum { VRB_LIGHT, VRB_MEDIUM, VRB_HEAVY };
static const char *const vrbWeightNames[3] = { "light", "medium", "heavy" };

static qboolean CG_VRB_TwoHanded( void );
static void CG_VRB_LogHands( centity_t *cent );

static int CG_VRB_Weight( qboolean withHands ) {
	int w;

	switch ( cg.predictedPlayerState.fd.saberAnimLevel ) {
	case SS_FAST:
	case SS_TAVION:
		w = VRB_LIGHT;
		break;
	case SS_STRONG:
	case SS_DESANN:
		w = VRB_HEAVY;
		break;
	default:
		w = VRB_MEDIUM;	// (dual sabers and the staff too)
		break;
	}
	if ( withHands && w < VRB_HEAVY && CG_VRB_TwoHanded() ) {
		w++;
	}
	return w;
}

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

static void VRB_Identity( vrbMat_t q ) {
	memset( q, 0, sizeof( vrbMat_t ) );
	q[0][0] = q[1][1] = q[2][2] = 1.0f;
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

// unit vector a fraction f of the way from unit vector a to unit vector b
static void VRB_Slerp( const vec3_t a, const vec3_t b, float f, vec3_t out ) {
	const float c = Com_Clamp( -1.0f, 1.0f, DotProduct( a, b ) );
	const float th = acosf( c ), s = sinf( th );

	f = Com_Clamp( 0.0f, 1.0f, f );
	if ( s > 1e-3f ) {
		VectorScale( a, sinf( ( 1.0f - f ) * th ) / s, out );
		VectorMA( out, sinf( f * th ) / s, b, out );
	} else if ( c > 0.0f ) {
		VectorScale( a, 1.0f - f, out );	// (the same way)
		VectorMA( out, f, b, out );
	} else {
		vec3_t p;							// (opposite: round any perpendicular)
		PerpendicularVector( p, a );
		VectorScale( a, cosf( f * M_PI ), out );
		VectorMA( out, sinf( f * M_PI ), p, out );
	}
	if ( VectorNormalize( out ) < 1e-6f ) {
		VectorCopy( a, out );
	}
}

static void VRB_RotZ( const vec3_t in, float deg, vec3_t out ) {
	const float r = DEG2RAD( deg ), c = cosf( r ), sn = sinf( r );
	const float x = in[0] * c - in[1] * sn, y = in[0] * sn + in[1] * c;

	out[0] = x;
	out[1] = y;
	out[2] = in[2];
}

// v turned about the unit axis k by a radians
static void VRB_Rotate( const vec3_t v, const vec3_t k, float a, vec3_t out ) {
	const float c = cosf( a ), sn = sinf( a ), kv = DotProduct( k, v );
	vec3_t kxv, t;

	CrossProduct( k, v, kxv );
	VectorScale( v, c, t );
	VectorMA( t, sn, kxv, t );
	VectorMA( t, kv * ( 1.0f - c ), k, out );
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

static void CG_VRB_AxesFromBolt( const mdxaBone_t *m, int convention, vec3_t origin, vec3_t axis[3] ) {
	vec3_t c[3];
	int i;

	for ( i = 0; i < 3; i++ ) {
		origin[i] = m->matrix[i][3];
		c[0][i] = m->matrix[i][0];
		c[1][i] = m->matrix[i][1];
		c[2][i] = m->matrix[i][2];
	}
	switch ( convention ) {
	default:
	case 0:	// undo GetBoltMatrix's relabelling (new x = -old y, new y = old x)
		VectorCopy( c[1], axis[0] );
		VectorScale( c[0], -1.0f, axis[1] );
		VectorCopy( c[2], axis[2] );
		break;
	case 1:	// as returned
		VectorCopy( c[0], axis[0] );
		VectorCopy( c[1], axis[1] );
		VectorCopy( c[2], axis[2] );
		break;
	case 2:	// the other way round
		VectorScale( c[1], -1.0f, axis[0] );
		VectorCopy( c[0], axis[1] );
		VectorCopy( c[2], axis[2] );
		break;
	}
}

// A bone (or tag) of the player's model in the world. G2API_AddBolt returns
// the existing bolt when there is one, so asking every frame never leaves a
// stale index behind when the model changes.
static qboolean CG_VRB_Bone( centity_t *cent, const char *name, int convention, vrbBone_t *out ) {
	mdxaBone_t m;
	int bolt;

	bolt = trap->G2API_AddBolt( cent->ghoul2, 0, name );
	if ( bolt < 0 ) {
		return qfalse;
	}
	if ( !trap->G2API_GetBoltMatrix( cent->ghoul2, 0, bolt, &m, cent->turAngles, cent->lerpOrigin,
			cg.time, cgs.gameModels, cent->modelScale ) ) {
		return qfalse;
	}
	CG_VRB_AxesFromBolt( &m, convention, out->origin, out->axis );
	return VRB_Orthonormalize( out->axis );
}

// A bone's position in the model's own (unscaled) space - the space the
// engine's bone transforms work in.
static qboolean CG_VRB_ModelSpaceBone( void *g2, int bolt, vec3_t out ) {
	static vec3_t zero = { 0.0f, 0.0f, 0.0f };
	mdxaBone_t m;

	if ( bolt < 0 || !trap->G2API_GetBoltMatrix( g2, 0, bolt, &m, zero, zero, cg.time, cgs.gameModels, zero ) ) {
		return qfalse;
	}
	VectorSet( out, m.matrix[0][3], m.matrix[1][3], m.matrix[2][3] );
	return qtrue;
}

// world -> the model's own space, as GetBoltMatrix maps it the other way
static void CG_VRB_WorldToModel( centity_t *cent, const vec3_t world, vec3_t model ) {
	matrix3_t ax;
	vec3_t d;
	int i;

	AnglesToAxis( cent->turAngles, ax );
	VectorSubtract( world, cent->lerpOrigin, d );
	for ( i = 0; i < 3; i++ ) {
		model[i] = DotProduct( d, ax[i] );
		if ( cent->modelScale[i] ) {
			model[i] /= cent->modelScale[i];
		}
	}
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

static void CG_VRB_SetHandBones( void *g2, int h, const mdxaBone_t *m ) {
	int k;

	if ( !trap->ext.VR_G2SetBoneTransform ) {
		return;
	}
	for ( k = 0; k < VRB_MAX_HAND_BONES; k++ ) {
		if ( vrb.handBoneTurn[h][k] ) {
			trap->ext.VR_G2SetBoneTransform( g2, 0, vrbHandBones[h][k], m );
		}
	}
}

// Back to the animated pose: no arm overrides, wrists straight.
static void CG_VRB_ClearCurl( void *g2 );

static void CG_VRB_ResetArms( void *g2 ) {
	static const vec3_t zero = { 0.0f, 0.0f, 0.0f };
	static const mdxaBone_t identity = { { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 } } };
	int h;

	CG_VRB_ClearCurl( g2 );

	for ( h = 0; h < 2; h++ ) {
		trap->G2API_SetBoneAngles( g2, 0, vrbArmBones[h][VRB_UPPER], zero, BONE_ANGLES_POSTMULT,
			POSITIVE_Z, NEGATIVE_Y, POSITIVE_X, NULL, 0, cg.time );
		trap->G2API_SetBoneAngles( g2, 0, vrbArmBones[h][VRB_FORE], zero, BONE_ANGLES_POSTMULT,
			POSITIVE_Z, NEGATIVE_Y, POSITIVE_X, NULL, 0, cg.time );
		if ( vrb.wristG2 == g2 && vrb.probeG2 == g2 ) {
			CG_VRB_SetHandBones( g2, h, &identity );
		}
	}
}

/*
=================
CG_VRB_ProbeHands

Finds out, once per model, which bones have to be turned for the hand to
turn at the wrist: the hand bone, plus every finger/tag bone that does not
already follow it. The hand bone is shifted with a test transform; whatever
moves along is its child. (In Jedi Academy's skeleton the fingers are the
forearm's children, so they all get turned with it.)
=================
*/
static void CG_VRB_ProbeHands( centity_t *cent ) {
	const clientInfo_t *ci = &cgs.clientinfo[cent->currentState.number];
	void *g2 = cent->ghoul2;
	mdxaBone_t test = { { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 8 } } };	// shift up 8 units
	vec3_t before[VRB_MAX_HAND_BONES], after;
	int bolt[VRB_MAX_HAND_BONES];
	int h, k, turned = 0;

	if ( vrb.probed && vrb.probeG2 == g2 && !Q_stricmp( vrb.probeModel, ci->modelName ) &&
		( ( vrb.wristOk[0] && vrb.wristOk[1] ) || cg.time < vrb.probeRetry ) ) {
		return;
	}
	if ( !vrbRemote ) {
		CG_VRB_DumpModel( ci, cent );
	}
	vrb.probed = qtrue;
	vrb.probeRetry = cg.time + 2000;	// (only matters if a hand failed)
	vrb.probeG2 = g2;
	Q_strncpyz( vrb.probeModel, ci->modelName, sizeof( vrb.probeModel ) );
	vrb.wristG2 = NULL;

	for ( h = 0; h < 2; h++ ) {
		vrb.wristOk[h] = qfalse;
		vrb.tagMoves[h] = qfalse;
		memset( vrb.handBoneTurn[h], 0, sizeof( vrb.handBoneTurn[h] ) );
		if ( !trap->ext.VR_G2SetBoneTransform ) {
			continue;
		}
		for ( k = 0; k < VRB_MAX_HAND_BONES; k++ ) {
			bolt[k] = trap->G2API_AddBolt( g2, 0, vrbHandBones[h][k] );
			if ( bolt[k] >= 0 && !CG_VRB_ModelSpaceBone( g2, bolt[k], before[k] ) ) {
				bolt[k] = -1;
			}
		}
		if ( bolt[0] < 0 || !trap->ext.VR_G2SetBoneTransform( g2, 0, vrbHandBones[h][0], &test ) ) {
			continue;
		}
		if ( CG_VRB_ModelSpaceBone( g2, bolt[0], after ) && Distance( before[0], after ) > 4.0f ) {
			vrb.wristOk[h] = qtrue;
			vrb.handBoneTurn[h][0] = qtrue;
			for ( k = 1; k < VRB_MAX_HAND_BONES; k++ ) {
				if ( bolt[k] >= 0 && CG_VRB_ModelSpaceBone( g2, bolt[k], after ) && Distance( before[k], after ) < 1.0f ) {
					vrb.handBoneTurn[h][k] = qtrue;		// didn't follow the hand: turn it too
				}
			}
		}
		trap->ext.VR_G2SetBoneTransform( g2, 0, vrbHandBones[h][0], NULL );

		if ( vrb.wristOk[h] ) {
			// A bone hanging off another chosen one (a finger's second joint
			// under its first, say) would be moved twice: shift them all once
			// and drop whatever moved double.
			int pass;
			for ( pass = 0; pass < VRB_MAX_HAND_BONES; pass++ ) {
				qboolean dropped = qfalse;
				CG_VRB_SetHandBones( g2, h, &test );
				for ( k = 1; k < VRB_MAX_HAND_BONES; k++ ) {
					if ( vrb.handBoneTurn[h][k] && CG_VRB_ModelSpaceBone( g2, bolt[k], after ) &&
						Distance( before[k], after ) > 12.0f ) {
						vrb.handBoneTurn[h][k] = qfalse;
						trap->ext.VR_G2SetBoneTransform( g2, 0, vrbHandBones[h][k], NULL );	// (not cleared below now)
						dropped = qtrue;
						break;		// one at a time: its parent may be the next to go
					}
				}
				CG_VRB_SetHandBones( g2, h, NULL );
				if ( !dropped ) {
					break;
				}
			}
			for ( k = 0, turned = 0; k < VRB_MAX_HAND_BONES; k++ ) {
				turned += vrb.handBoneTurn[h][k] ? 1 : 0;
			}
		}

		// each finger: does its second joint hang off its first? (curling it
		// round a hilt turns them one after the other, CG_VRB_CurlFingers)
		{
			int f;
			for ( f = 0; f < VRB_FINGERS; f++ ) {
				const int j1 = vrbFingerJ1[f], j2 = j1 + 1;
				vrb.fingerUnderJ1[h][f] = qfalse;
				if ( bolt[j1] < 0 || bolt[j2] < 0 ||
					!trap->ext.VR_G2SetBoneTransform( g2, 0, vrbHandBones[h][j1], &test ) ) {
					continue;
				}
				if ( CG_VRB_ModelSpaceBone( g2, bolt[j2], after ) && Distance( before[j2], after ) > 4.0f ) {
					vrb.fingerUnderJ1[h][f] = qtrue;
				}
				trap->ext.VR_G2SetBoneTransform( g2, 0, vrbHandBones[h][j1], NULL );
			}
		}

		// does the held-item tag hang on the tag bone? (then a saber can be
		// moved to where the fist holds it, CG_VRB_PlaceSaber)
		vrb.tagMoves[h] = qfalse;
		if ( vrb.wristOk[h] && bolt[1] >= 0 ) {
			const int tag = trap->G2API_AddBolt( g2, 0, vrbArmBones[h][VRB_TAG] );
			vec3_t t0, t1;
			if ( CG_VRB_ModelSpaceBone( g2, tag, t0 ) && trap->ext.VR_G2SetBoneTransform( g2, 0, vrbHandBones[h][1], &test ) ) {
				vrb.tagMoves[h] = ( CG_VRB_ModelSpaceBone( g2, tag, t1 ) && Distance( t0, t1 ) > 4.0f ) ? qtrue : qfalse;
				trap->ext.VR_G2SetBoneTransform( g2, 0, vrbHandBones[h][1], NULL );
			}
		}

		if ( vrb.wristOk[h] && cg_vrBodyDebug.integer ) {
			// does the held-item tag go along? (it should: it is part of the hand)
			const int tag = trap->G2API_AddBolt( g2, 0, vrbArmBones[h][VRB_TAG] );
			vec3_t t0, t1;
			qboolean follows = qfalse;
			if ( CG_VRB_ModelSpaceBone( g2, tag, t0 ) ) {
				CG_VRB_SetHandBones( g2, h, &test );
				follows = ( CG_VRB_ModelSpaceBone( g2, tag, t1 ) && Distance( t0, t1 ) > 7.0f ) ? qtrue : qfalse;
				CG_VRB_SetHandBones( g2, h, NULL );
			}
			Com_Printf( "VR body: %s %s hand: %d bone(s) turn at the wrist, item tag %s\n", ci->modelName,
				h == VR_HAND_RIGHT ? "right" : "left", turned, follows ? "goes along" : S_COLOR_YELLOW "STAYS (guns may sit off the hand)" );
		}
	}
}

// Turns the hand (and the bones that make it up) by world rotation D about
// the world point "wrist".
// A turn R about the world point p, as the model-space transform Ghoul2 puts
// on top of a bone (G2API_SetBoneVRTransform).
static void CG_VRB_ModelTurn( centity_t *cent, vrbMat_t R, const vec3_t p, mdxaBone_t *m ) {
	matrix3_t ax;
	vec3_t col, q;
	int r, c;

	AnglesToAxis( cent->turAngles, ax );
	for ( c = 0; c < 3; c++ ) {
		VRB_Apply( R, ax[c], col );
		for ( r = 0; r < 3; r++ ) {
			m->matrix[r][c] = DotProduct( ax[r], col );
		}
	}
	CG_VRB_WorldToModel( cent, p, q );
	for ( r = 0; r < 3; r++ ) {
		m->matrix[r][3] = q[r] - ( m->matrix[r][0] * q[0] + m->matrix[r][1] * q[1] + m->matrix[r][2] * q[2] );
	}
}

// a after b
static void VRB_Compose( const mdxaBone_t *a, const mdxaBone_t *b, mdxaBone_t *out ) {
	mdxaBone_t t;
	int r, c;

	for ( r = 0; r < 3; r++ ) {
		for ( c = 0; c < 4; c++ ) {
			t.matrix[r][c] = a->matrix[r][0] * b->matrix[0][c] + a->matrix[r][1] * b->matrix[1][c] +
				a->matrix[r][2] * b->matrix[2][c] + ( c == 3 ? a->matrix[r][3] : 0.0f );
		}
	}
	*out = t;
}

// Turns the hand about the wrist (and says how, in model space: m).
static qboolean CG_VRB_TurnWrist( centity_t *cent, int h, vrbMat_t D, const vec3_t wrist, mdxaBone_t *m ) {
	if ( !vrb.wristOk[h] || vrb.probeG2 != cent->ghoul2 ) {
		return qfalse;
	}
	CG_VRB_ModelTurn( cent, D, wrist, m );
	CG_VRB_SetHandBones( cent->ghoul2, h, m );
	vrb.wristG2 = cent->ghoul2;
	return qtrue;
}

/*
=================
CG_VRB_CurlFingers

A hand holding a hilt closes its fingers round it. The animations hold a
hilt loosely or not at all (running, jumping, and the second hand on a
hilt), and the humanoid fist they do have (BOTH_SABERDUAL_STANCE) is
clenched round something half as thick as a saber's hilt: its fingertips
are inside the hilt, and the models' own hand tags put the hilt through the
palm. So the hilt goes where a real hand holds a handle - just under the
palm, in the hole the fingers close round (CG_VRB_GripLine, from the hand's
own bones, so the same on every model) - and each finger - thumb, index,
the rest - is bent at its first joint and then its second, about lines
along the hilt, the way that finger bends, until it lies round the hilt's
real thickness: closed further if it stands off it, opened if it's inside.
=================
*/

#define VRB_FINGER_R		0.85f	// finger bones lie this far from the hilt's axis (the hilt plus a finger's own half thickness)...
#define VRB_FINGER_DIP		0.62f	// ...a straight bone may cut its curve down to this (the flesh is round)
#define VRB_CURL_NEAR		1.0f	// a fist this near the hilt's axis closes fully round it...
#define VRB_CURL_FAR		2.5f	// ...and not at all from this far (an arm that couldn't reach it)
#define VRB_HAND_LENGTH		3.35f	// the humanoid hand's wrist -> knuckles, which the numbers below go with

// Where a hilt runs through the right fist, and which way (toward the
// thumb), in the hand's frame (VRB_HandFrame); a left hand is the mirror
// image (its frame's third axis comes out of the palm).
static const vec3_t vrbGripPoint = { 2.45f, 0.0f, -0.9f };
static const vec3_t vrbGripDir = { 0.228f, 0.973f, 0.0f };

/*
The hand's own frame, from where its bones are: [0] wrist -> knuckles, [1]
across the knuckles toward the thumb, [2] out of the back of a right hand.
*/
static qboolean VRB_HandFrame( const vec3_t wrist, const vec3_t thumb, const vec3_t index, const vec3_t little,
	vec3_t ax[3], float *length ) {
	vec3_t kmid, tmp;

	VectorAdd( index, little, kmid );
	VectorScale( kmid, 0.5f, kmid );
	VectorSubtract( kmid, wrist, ax[0] );
	VectorSubtract( index, little, ax[1] );
	*length = VectorNormalize( ax[0] );
	if ( *length < 0.01f ) {
		return qfalse;
	}
	VectorMA( ax[1], -DotProduct( ax[1], ax[0] ), ax[0], ax[1] );
	if ( VectorNormalize( ax[1] ) < 0.01f ) {
		return qfalse;
	}
	VectorSubtract( thumb, kmid, tmp );
	if ( DotProduct( tmp, ax[1] ) < 0.0f ) {
		VectorScale( ax[1], -1.0f, ax[1] );
	}
	CrossProduct( ax[0], ax[1], ax[2] );
	return qtrue;
}

// Where the hilt runs through hand h's fist, and which way, from its frame
static void VRB_GripLine( int h, const vec3_t wrist, vec3_t ax[3], float length, vec3_t o, vec3_t dir ) {
	const float mirror = ( h == VR_HAND_RIGHT ) ? 1.0f : -1.0f;
	const float scale = Com_Clamp( 0.5f, 2.0f, length / VRB_HAND_LENGTH );

	VectorMA( wrist, scale * vrbGripPoint[0], ax[0], o );
	VectorMA( o, scale * vrbGripPoint[1], ax[1], o );
	VectorMA( o, scale * mirror * vrbGripPoint[2], ax[2], o );
	VectorScale( ax[0], vrbGripDir[0], dir );
	VectorMA( dir, vrbGripDir[1], ax[1], dir );
	VectorMA( dir, mirror * vrbGripDir[2], ax[2], dir );
	VectorNormalize( dir );
}

// p's offset from the hilt's axis (o, u), square to it
static void VRB_AxisOffset( const vec3_t p, const vec3_t o, const vec3_t u, vec3_t out ) {
	VectorSubtract( p, o, out );
	VectorMA( out, -DotProduct( out, u ), u, out );
}

static float CG_VRB_AxisDistance( const vec3_t p, const vec3_t o, const vec3_t u ) {
	vec3_t rel;

	VRB_AxisOffset( p, o, u, rel );
	return VectorLength( rel );
}

// How near the segment a-b comes to the hilt's axis
static float VRB_SegmentAxisDistance( const vec3_t a, const vec3_t b, const vec3_t o, const vec3_t u ) {
	vec3_t pa, pb, d, c;
	float len2, t;

	VRB_AxisOffset( a, o, u, pa );
	VRB_AxisOffset( b, o, u, pb );
	VectorSubtract( pb, pa, d );
	len2 = DotProduct( d, d );
	t = ( len2 > 1e-6f ) ? Com_Clamp( 0.0f, 1.0f, -DotProduct( pa, d ) / len2 ) : 0.0f;
	VectorMA( pa, t, d, c );
	return VectorLength( c );
}

// child turned by a (radians) about the line through pivot along u
static void VRB_TurnAbout( const vec3_t pivot, const vec3_t child, const vec3_t u, float a, vec3_t out ) {
	vec3_t rel;

	VectorSubtract( child, pivot, rel );
	VRB_Rotate( rel, u, a, out );
	VectorAdd( out, pivot, out );
}

/*
How far (radians, about +u through pivot) to bend the bone pivot -> child
so its end lies VRB_FINGER_R from the hilt's axis (o, u): further the way
the finger bends (bend: +1/-1 about u) if it stands off the hilt, by up to
maxClose degrees, as long as the bone doesn't cut into the hilt (nearer
than VRB_FINGER_DIP) - and no further than it gets nearer: a fist off the
hilt (the arm not quite reaching it) can leave a finger that can't reach
it, which would otherwise curl on round past it; back the other way if
it's inside or cutting in, by up to maxOpen, till it's on it and clear.
*/
static float CG_VRB_FitJoint( const vec3_t pivot, const vec3_t child, const vec3_t o, const vec3_t u,
	float bend, float maxClose, float maxOpen ) {
	vec3_t rel, p;
	float sign, limit, deg, nearest;
	qboolean closing;

	VectorSubtract( child, pivot, rel );
	{
		// (a bone lying along the hilt would only roll about itself)
		const float len = VectorLength( rel ), along = DotProduct( rel, u );
		if ( len < 0.05f || len * len - along * along < Square( 0.3f * len ) ) {
			return 0.0f;
		}
	}
	closing = ( CG_VRB_AxisDistance( child, o, u ) > VRB_FINGER_R &&
		VRB_SegmentAxisDistance( pivot, child, o, u ) >= VRB_FINGER_DIP ) ? qtrue : qfalse;
	sign = closing ? bend : -bend;
	limit = closing ? maxClose : maxOpen;
	nearest = CG_VRB_AxisDistance( child, o, u );
	for ( deg = 1.0f; deg <= limit; deg += 1.0f ) {
		float d, g;
		VRB_TurnAbout( pivot, child, u, sign * DEG2RAD( deg ), p );
		d = CG_VRB_AxisDistance( p, o, u );
		g = VRB_SegmentAxisDistance( pivot, p, o, u );
		if ( closing ) {
			if ( g < VRB_FINGER_DIP || d > nearest ) {
				deg -= 1.0f;	// (it would cut into the hilt, or it's past its nearest: as far as it went)
				break;
			}
			if ( d <= VRB_FINGER_R ) {
				break;
			}
			nearest = d;
		} else if ( d >= VRB_FINGER_R && g >= VRB_FINGER_DIP ) {
			break;
		}
	}
	return sign * DEG2RAD( Q_min( deg, limit ) );
}

// Rotation by a radians about the unit axis u.
static void VRB_AxisAngle( const vec3_t u, float a, vrbMat_t R ) {
	static const vec3_t basis[3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
	vec3_t col;
	int r, c;

	for ( c = 0; c < 3; c++ ) {
		VRB_Rotate( basis[c], u, a, col );
		for ( r = 0; r < 3; r++ ) {
			R[r][c] = col[r];
		}
	}
}

// (the log) p about the hilt: along it, out from its axis, and round it
// from the side the back of the hand is on (degrees, about the hilt's
// thumb-ward direction)
static void VRB_HiltCoords( const vec3_t p, const vec3_t o, const vec3_t u, const vec3_t v, vec3_t out ) {
	vec3_t rel, w;

	VectorSubtract( p, o, rel );
	CrossProduct( u, v, w );
	out[0] = DotProduct( rel, u );
	VectorMA( rel, -out[0], u, rel );
	out[1] = VectorLength( rel );
	out[2] = RAD2DEG( atan2f( DotProduct( rel, w ), DotProduct( rel, v ) ) );
}

// The world move x -> R (x - from) + to, as the model-space transform Ghoul2
// puts on top of a bone.
static void CG_VRB_ModelMove( centity_t *cent, vrbMat_t R, const vec3_t from, const vec3_t to, mdxaBone_t *m ) {
	vec3_t qf, qt;
	int r;

	CG_VRB_ModelTurn( cent, R, from, m );
	CG_VRB_WorldToModel( cent, from, qf );
	CG_VRB_WorldToModel( cent, to, qt );
	for ( r = 0; r < 3; r++ ) {
		m->matrix[r][3] += qt[r] - qf[r];
	}
}

/*
The saber in hand h where the hilt runs through the fist (after the arm and
wrist are posed for it: H, the wrist turn): its tag bone moved to carry it
there from where the model's tag would. Gives that line (o, dir).
*/
static qboolean CG_VRB_PlaceSaber( centity_t *cent, int h, const mdxaBone_t *H, int saberModel, vec3_t o, vec3_t dir ) {
	vrbBone_t w, t, i, l, tag;
	mdxaBone_t m, E;
	vec3_t ax[3], blade;
	vrbMat_t R;
	float length;

	if ( !CG_VRB_Bone( cent, vrbArmBones[h][VRB_HAND], 1, &w ) || !CG_VRB_Bone( cent, vrbArmBones[h][VRB_THUMB], 1, &t ) ||
		!CG_VRB_Bone( cent, vrbArmBones[h][VRB_INDEX], 1, &i ) || !CG_VRB_Bone( cent, vrbArmBones[h][VRB_LITTLE], 1, &l ) ||
		!CG_VRB_Bone( cent, vrbArmBones[h][VRB_TAG], 1, &tag ) ||
		!VRB_HandFrame( w.origin, t.origin, i.origin, l.origin, ax, &length ) ||
		!trap->G2API_GetBoltMatrix( cent->ghoul2, saberModel, 0, &m, cent->turAngles, cent->lerpOrigin, cg.time,
			cgs.gameModels, cent->modelScale ) ) {
		return qfalse;
	}
	VRB_GripLine( h, w.origin, ax, length, o, dir );
	BG_GiveMeVectorFromMatrix( &m, NEGATIVE_Y, blade );
	if ( VectorNormalize( blade ) < 0.5f ) {
		return qfalse;
	}
	// the saber's origin (where its tag holds it) onto the line, its blade along it
	VRB_Arc( blade, dir, R );
	CG_VRB_ModelMove( cent, R, tag.origin, o, &E );
	if ( vrb.handBoneTurn[h][1] ) {
		VRB_Compose( &E, H, &E );	// (on top of the wrist turn it already has)
	}
	if ( !trap->ext.VR_G2SetBoneTransform( cent->ghoul2, 0, vrbHandBones[h][1], &E ) ) {
		return qfalse;
	}
	vrb.curlG2 = cent->ghoul2;	// (cleared with the fingers)
	return qtrue;
}

// (after the arm and wrist are posed; H: the wrist turn already on the bones
// that turn with the hand, in model space; hiltDir: along the hilt, toward
// the thumb; strength: 0..1 of the full curl)
static void CG_VRB_CurlFingers( centity_t *cent, int h, const mdxaBone_t *H, const vec3_t o, const vec3_t hiltDir, float strength ) {
	vrbBone_t b1[VRB_FINGERS], b2[VRB_FINGERS], hand;
	qboolean have[VRB_FINGERS], logGeo;
	vec3_t u, v, ax[3], holdO, holdDir;
	float length;
	int f;

	VectorCopy( hiltDir, u );
	if ( !trap->ext.VR_G2SetBoneTransform || VectorNormalize( u ) < 0.5f || strength <= 0.0f ) {
		return;
	}
	// every finger first, then the turns (setting one rebuilds the skeleton
	// for the next query, and the fingers don't move each other)
	if ( !CG_VRB_Bone( cent, vrbArmBones[h][VRB_HAND], 1, &hand ) ) {
		return;
	}
	for ( f = 0; f < VRB_FINGERS; f++ ) {
		const int j1 = vrbFingerJ1[f];
		have[f] = CG_VRB_Bone( cent, vrbHandBones[h][j1], 1, &b1[f] ) && CG_VRB_Bone( cent, vrbHandBones[h][j1 + 1], 1, &b2[f] );
	}
	// only a fist that is at the hilt closes round it (not one the arm
	// couldn't reach it with, or one beside a saber bolted to the wrist)
	if ( !have[0] || !have[1] || !have[2] || !VRB_HandFrame( hand.origin, b1[0].origin, b1[1].origin, b1[2].origin, ax, &length ) ) {
		return;
	}
	VRB_GripLine( h, hand.origin, ax, length, holdO, holdDir );
	vrb.curlOff[h] = CG_VRB_AxisDistance( holdO, o, u );
	strength *= Com_Clamp( 0.0f, 1.0f, ( VRB_CURL_FAR - vrb.curlOff[h] ) / ( VRB_CURL_FAR - VRB_CURL_NEAR ) );
	if ( strength <= 0.0f ) {
		return;
	}

	// (the log: the hand round the hilt, now and then)
	logGeo = ( !vrbRemote && cg_vrLog.integer && ( cg.time >= vrb.curlGeoTime[h] || vrb.curlGeoTime[h] - cg.time > 20000 ) ) ? qtrue : qfalse;
	if ( logGeo ) {
		VRB_AxisOffset( hand.origin, o, u, v );
		if ( VectorNormalize( v ) < 0.01f ) {
			logGeo = qfalse;
		} else {
			vec3_t c;
			vrb.curlGeoTime[h] = cg.time + 20000;
			VRB_HiltCoords( hand.origin, o, u, v, c );
			CG_VRB_Log( "%s hand round the hilt (along, out from its axis, round it; the wrist is at 0 deg): wrist %.1f %.1f %.0f,"
				" fist %.2f off the axis\n", h == VR_HAND_RIGHT ? "right" : "left", c[0], c[1], c[2], vrb.curlOff[h] );
		}
	}

	for ( f = 0; f < VRB_FINGERS; f++ ) {
		const int j1 = vrbFingerJ1[f], j2 = j1 + 1;
		vrbMat_t R1, R2;
		mdxaBone_t C1, C2, m;
		vec3_t seg, tipDir, tip, tip0, p2, rel, cr;
		float len, best = 0.0f, a1, a2, bend;
		int k, along = 0;

		VectorSubtract( b2[f].origin, b1[f].origin, seg );
		len = VectorNormalize( seg );
		if ( len < 0.2f ) {
			continue;
		}
		// the finger beyond its second joint: that joint's own axis that runs
		// the way the first joint's runs along the finger
		for ( k = 0; k < 3; k++ ) {
			const float d = DotProduct( b1[f].axis[k], seg );
			if ( fabsf( d ) > fabsf( best ) ) {
				best = d;
				along = k;
			}
		}
		VectorScale( b2[f].axis[along], best < 0.0f ? -1.0f : 1.0f, tipDir );
		VectorMA( b2[f].origin, 0.9f * len, tipDir, tip );
		VectorCopy( tip, tip0 );

		// the way this finger bends, as the fist has it (first bone to second)
		VectorSubtract( tip, b2[f].origin, rel );
		CrossProduct( seg, rel, cr );
		bend = ( DotProduct( cr, u ) >= 0.0f ) ? 1.0f : -1.0f;

		// fit the first joint (its bone ends on the hilt), then the second
		// (the rest of the finger does), then only as much of both as
		// strength says
		a1 = CG_VRB_FitJoint( b1[f].origin, b2[f].origin, o, u, bend, 90.0f, 90.0f );
		VRB_AxisAngle( u, a1, R1 );
		VectorSubtract( b2[f].origin, b1[f].origin, rel );
		VRB_Apply( R1, rel, rel );
		VectorAdd( b1[f].origin, rel, p2 );
		VectorSubtract( tip, b1[f].origin, rel );
		VRB_Apply( R1, rel, rel );
		VectorAdd( b1[f].origin, rel, tip );
		a2 = strength * CG_VRB_FitJoint( p2, tip, o, u, bend, 110.0f, 110.0f );
		if ( strength < 1.0f ) {
			a1 *= strength;
			VRB_AxisAngle( u, a1, R1 );
			VectorSubtract( b2[f].origin, b1[f].origin, rel );
			VRB_Apply( R1, rel, rel );
			VectorAdd( b1[f].origin, rel, p2 );
			VectorSubtract( tip0, b1[f].origin, rel );
			VRB_Apply( R1, rel, rel );
			VectorAdd( b1[f].origin, rel, tip );
		}
		VRB_AxisAngle( u, a2, R2 );
		CG_VRB_ModelTurn( cent, R1, b1[f].origin, &C1 );
		CG_VRB_ModelTurn( cent, R2, p2, &C2 );

		// on top of what each bone already has (the wrist turn) - the
		// second joint gets the first one's turn from it if it hangs off it
		if ( vrb.handBoneTurn[h][j1] ) {
			VRB_Compose( &C1, H, &m );
		} else {
			m = C1;
		}
		trap->ext.VR_G2SetBoneTransform( cent->ghoul2, 0, vrbHandBones[h][j1], &m );
		if ( !vrb.fingerUnderJ1[h][f] ) {
			VRB_Compose( &C2, &C1, &C2 );
			if ( vrb.handBoneTurn[h][j2] ) {
				VRB_Compose( &C2, H, &C2 );
			}
		}
		trap->ext.VR_G2SetBoneTransform( cent->ghoul2, 0, vrbHandBones[h][j2], &C2 );
		vrb.curlG2 = cent->ghoul2;
		vrb.curlLog[h][f] = RAD2DEG( ( a1 + a2 ) * bend );	// (closing: +)

		if ( logGeo ) {
			static const char *const names[VRB_FINGERS] = { "thumb", "index", "rest" };
			vec3_t c1, c2, c3, c4, c5;
			vec3_t tipAfter;
			VRB_TurnAbout( p2, tip, u, a2, tipAfter );
			VRB_HiltCoords( b1[f].origin, o, u, v, c1 );
			VRB_HiltCoords( b2[f].origin, o, u, v, c2 );
			VRB_HiltCoords( p2, o, u, v, c3 );
			VRB_HiltCoords( tip0, o, u, v, c4 );
			VRB_HiltCoords( tipAfter, o, u, v, c5 );
			CG_VRB_Log( "  %s: knuckle %.1f %.1f %.0f | middle %.1f %.1f %.0f -> %.1f %.1f %.0f | tip %.1f %.1f %.0f -> %.1f %.1f %.0f"
				" | bent %+.0f %+.0f deg (closing +) | bones %.1f long, tip along axis %d (%.2f)\n",
				names[f], c1[0], c1[1], c1[2], c2[0], c2[1], c2[2], c3[0], c3[1], c3[2], c4[0], c4[1], c4[2],
				c5[0], c5[1], c5[2], RAD2DEG( a1 * bend ), RAD2DEG( a2 * bend ), len, along, best );
		}
	}
}

// The fingers (and a saber moved in the hand) back to what the animation
// (and the wrist turn) does.
static void CG_VRB_ClearCurl( void *g2 ) {
	int h, f;

	memset( vrb.curlLog, 0, sizeof( vrb.curlLog ) );
	vrb.curlOff[0] = vrb.curlOff[1] = -1.0f;
	if ( vrb.curlG2 != g2 || !trap->ext.VR_G2SetBoneTransform ) {
		return;
	}
	for ( h = 0; h < 2; h++ ) {
		for ( f = 0; f < VRB_FINGERS; f++ ) {
			trap->ext.VR_G2SetBoneTransform( g2, 0, vrbHandBones[h][vrbFingerJ1[f]], NULL );
			trap->ext.VR_G2SetBoneTransform( g2, 0, vrbHandBones[h][vrbFingerJ1[f] + 1], NULL );
		}
		trap->ext.VR_G2SetBoneTransform( g2, 0, vrbHandBones[h][1], NULL );	// (the tag bone, CG_VRB_PlaceSaber)
	}
	vrb.curlG2 = NULL;
}

/*
=============================================================================

when, and which way the body faces

=============================================================================
*/

// Whether this is you, drawn as your VR body (apart from tumbling, below).
static qboolean CG_VRB_BodyUsable( centity_t *cent ) {
	const playerState_t *ps = &cg.predictedPlayerState;
	const qboolean saber = ( cent->currentState.weapon == WP_SABER ) ? qtrue : qfalse;

	if ( !cg_vrBody.integer || !CG_VR() || !cg.snap || cg.demoPlayback ) {
		return qfalse;
	}
	if ( !CG_VR_HandAim() && !( saber && cg_vrSaber.integer ) ) {
		// no gun in the VR hand (duel modes, a demo, vr_handAim 0, right
		// controller lost) and no first-person saber: the stock view
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
		cent->currentState.m_iVehicleNum || cent->currentState.weapon == WP_EMPLACED_GUN ) {
		return qfalse;	// the game's own posing wins
	}
	return qtrue;
}

static qboolean CG_VRB_OtherUsable( centity_t *cent );

// You, drawn as your VR body - or another player in VR, drawn as theirs.
qboolean CG_VR_BodyActive( centity_t *cent ) {
	if ( cent->currentState.number != cg.clientNum ) {
		return CG_VRB_OtherUsable( cent );
	}
	return CG_VRB_BodyUsable( cent );
}

// The animation your VR body shows (CG_PlayerAnimation). A flip, a roll or a
// wall run tumbles the whole body, which would swing it round your eyes; in
// the headset you stay upright, so your body shows the in-air pose instead
// (a crouch for a roll), and your arms, hands and saber stay where yours
// are. The game itself still flips and rolls you.
extern qboolean BG_InRollES( entityState_t *ps, int anim );

void CG_VR_BodyAnim( centity_t *cent, int *legsAnim, int *torsoAnim ) {
	const playerState_t *ps = &cg.predictedPlayerState;
	const animation_t *anims;
	qboolean roll;
	int anim;

	if ( cent->currentState.number != cg.clientNum ) {
		// (another VR player stays upright in their headset too)
		if ( !CG_VRB_OtherUsable( cent ) ) {
			return;
		}
		roll = BG_InRollES( &cent->currentState, *legsAnim );
	} else if ( CG_VRB_BodyUsable( cent ) ) {
		roll = BG_InRoll( (playerState_t *)ps, *legsAnim );
	} else {
		return;
	}
	if ( roll ) {
		anim = BOTH_CROUCH1IDLE;
	} else if ( BG_FlippingAnim( *legsAnim ) || BG_InSpecialJump( *legsAnim ) ) {
		anim = BOTH_FORCEINAIR1;
	} else {
		return;
	}
	anims = bgAllAnims[cent->localAnimIndex].anims;
	if ( !anims || !anims[anim].numFrames ) {
		anim = BOTH_INAIR1;
		if ( !anims || !anims[anim].numFrames ) {
			return;
		}
	}
	*legsAnim = *torsoAnim = anim;
}

extern qboolean BG_SabersOff( playerState_t *ps );

// A lit saber in your VR hands. Then the server fights with the blades you
// hold and the trigger doesn't swing, also while a flip or roll animates
// the body for a moment (the blades are missing from the report then).
static qboolean CG_VRB_SaberHands( void ) {
	playerState_t *ps = &cg.predictedPlayerState;

	if ( !cg.snap || !cg_vrSaber.integer || !cg_vrBodyArms.integer ) {
		return qfalse;
	}
	if ( ps->weapon != WP_SABER || ps->stats[STAT_HEALTH] <= 0 || ps->pm_type == PM_DEAD ) {
		return qfalse;
	}
	if ( ps->clientNum < 0 || ps->clientNum >= MAX_CLIENTS || !CG_VRB_BodyUsable( &cg_entities[ps->clientNum] ) ) {
		return qfalse;
	}
	if ( BG_SabersOff( ps ) ) {
		return qfalse;	// the trigger lights it
	}
	if ( ps->saberInFlight && ( !cgs.clientinfo[ps->clientNum].saber[1].model[0] || !ps->saberEntityNum ||
		ps->saberHolstered ) ) {
		// thrown or knocked away (the trigger calls it back), and no second
		// saber lit in hand
		return qfalse;
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

	if ( cent->currentState.number != cg.clientNum ) {
		// another VR player: where their body faces (level, like yours)
		if ( !CG_VRB_OtherUsable( cent ) ) {
			return qfalse;
		}
		VectorSet( angles, 0.0f, vrbOthers[cent->currentState.number].avatar.bodyYaw, 0.0f );
		return qtrue;
	}
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

// Controller h in the world, where the hand is put on it (cg_vrHandX/Y/Z).
// (Another player's: where their avatar says their hand is.)
static void CG_VRB_Controller( const vrClientState_t *vr, int h, vec3_t origin, vec3_t axis[3] ) {
	vec3_t local, localAxis[3];
	int i;

	if ( vrbRemote ) {
		VectorCopy( vrbRemote->hand[h].origin, origin );
		for ( i = 0; i < 3; i++ ) {
			VectorCopy( vrbRemote->hand[h].axis[i], axis[i] );
		}
		return;
	}
	VectorCopy( vr->gripOrigin[h], local );
	for ( i = 0; i < 3; i++ ) {
		VectorCopy( vr->gripAxis[h][i], localAxis[i] );
	}
	VR_ToWorld( vr, cg.refdef.vieworg, cg.refdef.viewaxis, local, localAxis, origin, axis );
	VectorMA( origin, cg_vrHandX.value, axis[0], origin );
	VectorMA( origin, ( h == VR_HAND_RIGHT ) ? cg_vrHandY.value : -cg_vrHandY.value, axis[1], origin );
	VectorMA( origin, cg_vrHandZ.value, axis[2], origin );
}

// Where a saber held like that controller points (along the fist, tilted
// by cg_vrSaberPitch).
static void CG_VRB_SaberDir( vec3_t axis[3], vec3_t out ) {
	const float sp = DEG2RAD( cg_vrSaberPitch.value );

	VectorScale( axis[0], cosf( sp ), out );
	VectorMA( out, -sinf( sp ), axis[2], out );
	VectorNormalize( out );
}

// What hand h holds this frame.
static void CG_VRB_Grip( centity_t *cent, int h, vrbGrip_t *grip ) {
	const clientInfo_t *ci = &cgs.clientinfo[cent->currentState.number];
	const int weapon = cent->currentState.weapon;

	memset( grip, 0, sizeof( *grip ) );
	grip->kind = VRB_GRIP_FREE;

	if ( vrbRemote ) {
		// another player's hand: what their avatar says it holds
		const vrAvatarHand_t *ah = &vrbRemote->hand[h];
		const int model = ( h == VR_HAND_RIGHT ) ? 1 : 2;
		int i;
		switch ( ah->grip ) {
		case VR_GRIP_SABER:
			if ( weapon == WP_SABER && trap->G2API_HasGhoul2ModelOnIndex( &cent->ghoul2, model ) ) {
				grip->kind = VRB_GRIP_SABER;
				grip->saberModel = model;
			}
			break;
		case VR_GRIP_HILT:
		case VR_GRIP_GUN:
			grip->kind = ( ah->grip == VR_GRIP_HILT ) ? VRB_GRIP_HILT : VRB_GRIP_GUN;
			VectorCopy( ah->origin, grip->origin );
			for ( i = 0; i < 3; i++ ) {
				VectorCopy( ah->axis[i], grip->axis[i] );
			}
			VectorCopy( ah->dir, grip->dir );
			grip->blend = ah->blend;
			break;
		default:
			break;
		}
		return;
	}

	if ( weapon == WP_SABER ) {
		// saber 1 in the right hand, a second saber in the left
		const int model = ( h == VR_HAND_RIGHT ) ? 1 : 2;
		if ( h == VR_HAND_LEFT && vrb.twoHandArms && vrb.twoHandTime == cg.time ) {
			// the left hand on the right hand's hilt (or settling onto it, or
			// coming off it)
			grip->kind = VRB_GRIP_HILT;
			VectorCopy( vrb.twoHandHand, grip->origin );
			VectorCopy( vrb.twoHandAxis[0], grip->axis[0] );
			VectorCopy( vrb.twoHandAxis[1], grip->axis[1] );
			VectorCopy( vrb.twoHandAxis[2], grip->axis[2] );
			VectorScale( vrb.bladeTime == cg.time ? vrb.twoHandFist : vrb.twoHandDir, vrb.twoHandSign, grip->dir );
			grip->blend = vrb.twoHandEase;
			return;
		}
		if ( ( h == VR_HAND_RIGHT && cent->currentState.saberInFlight ) ||
			( h == VR_HAND_LEFT && !ci->saber[1].model[0] ) ) {
			return;
		}
		if ( trap->G2API_HasGhoul2ModelOnIndex( &cent->ghoul2, model ) ) {
			grip->kind = VRB_GRIP_SABER;
			grip->saberModel = model;
		}
		return;
	}
	if ( h == VR_HAND_RIGHT && CG_VR_HandAim() ) {
		vec3_t angles;
		if ( CG_VR_HeldWeaponPose( weapon, grip->origin, angles, grip->axis ) ) {
			grip->kind = VRB_GRIP_GUN;
		}
	}
}

/*
=================
CG_VRB_TwoHand

Both hands on the saber, worked out once per frame before the arms are
solved: whether the left hand could take hold of the right hand's hilt
(told to the engine, which then doesn't use the Force when the left grip is
squeezed), whether it holds it, and then where the blade points and where
on the hilt the left hand is.
=================
*/

// A hand's fingers closed round a hilt (as in the dual-saber stance, where
// both hands hold one), or back to what the body's animation does. Whatever
// the body is doing - running, jumping, parrying, whose animations open the
// hand - a hand holding a hilt keeps hold of it.
static const char *const vrbForearm[2] = { "lradius", "rradius" };	// (the fingers are its children)

static void CG_VRB_Fist( centity_t *cent, int h, qboolean on ) {
	if ( on ) {
		const animation_t *anims = bgAllAnims[cent->localAnimIndex].anims;
		const animation_t *anim = &anims[BOTH_SABERDUAL_STANCE];
		if ( !anim->numFrames ) {
			anim = &anims[BOTH_SABERSTAFF_STANCE];
		}
		if ( !cent->ghoul2 || !anim->numFrames ) {
			return;
		}
		// (every frame: a new model may have taken the old one's instance)
		trap->G2API_SetBoneAnim( cent->ghoul2, 0, vrbForearm[h], anim->firstFrame, anim->firstFrame + 1,
			BONE_ANIM_OVERRIDE_FREEZE, 1.0f, cg.time, anim->firstFrame, 0 );
		vrb.fistG2[h] = cent->ghoul2;
	} else if ( vrb.fistG2[h] && vrb.fistG2[h] == cent->ghoul2 ) {
		trap->G2API_SetBoneAnim( cent->ghoul2, 0, vrbForearm[h], 0, 1, 0, 1.0f, cg.time, -1, 0 );
		vrb.fistG2[h] = NULL;
	}
}

// Both hands, before the arms are solved: a fist round whatever hilt each
// holds (the left one round the right hand's too, while it holds it).
static void CG_VRB_HandPoses( centity_t *cent ) {
	int h;

	for ( h = 0; h < 2; h++ ) {
		vrbGrip_t grip;
		CG_VRB_Grip( cent, h, &grip );
		CG_VRB_Fist( cent, h, (qboolean)( grip.kind == VRB_GRIP_SABER ||
			( h == VR_HAND_LEFT && vrb.twoHand && vrb.twoHandTime == cg.time ) ) );
	}
}

static float CG_VRB_SegmentDistance( const vec3_t p, const vec3_t a, const vec3_t b ) {
	vec3_t ab, ap, c;
	const float len2 = DistanceSquared( a, b );
	float f = 0.0f;

	VectorSubtract( b, a, ab );
	VectorSubtract( p, a, ap );
	if ( len2 > 0.0001f ) {
		f = Com_Clamp( 0.0f, 1.0f, DotProduct( ap, ab ) / len2 );
	}
	VectorMA( a, f, ab, c );
	return Distance( p, c );
}

// Distance from p to the stretch t[0]..t[1] of the line o + t * dir.
static float CG_VRB_RangeDistance( const vec3_t p, const vec3_t o, const vec3_t dir, const float t[2] ) {
	vec3_t a, b;

	VectorMA( o, t[0], dir, a );
	VectorMA( o, t[1], dir, b );
	return CG_VRB_SegmentDistance( p, a, b );
}

// A frame for the hilt: along it, and the right hand's left (its up if the
// hilt runs along that) square to it.
static void CG_VRB_HiltFrame( const vec3_t dir, vec3_t axR[3], vec3_t e[3] ) {
	VectorCopy( dir, e[0] );
	VectorMA( axR[1], -DotProduct( axR[1], dir ), dir, e[1] );
	if ( VectorNormalize( e[1] ) < 0.1f ) {
		VectorMA( axR[2], -DotProduct( axR[2], dir ), dir, e[1] );
		VectorNormalize( e[1] );
	}
	CrossProduct( e[0], e[1], e[2] );
}

// A frame a fraction f of the way from a to b (f 0: a).
static void VRB_FrameBlend( vec3_t a[3], vec3_t b[3], float f, vec3_t out[3] ) {
	vec3_t fwd, up;

	VectorScale( a[0], 1.0f - f, fwd );
	VectorMA( fwd, f, b[0], fwd );
	VectorScale( a[2], 1.0f - f, up );
	VectorMA( up, f, b[2], up );
	if ( VectorNormalize( fwd ) < 0.1f ) {
		VectorCopy( f < 0.5f ? a[0] : b[0], fwd );
	}
	VectorMA( up, -DotProduct( up, fwd ), fwd, up );
	if ( VectorNormalize( up ) < 0.1f ) {
		PerpendicularVector( up, fwd );
	}
	VectorCopy( fwd, out[0] );
	CrossProduct( up, fwd, out[1] );
	VectorCopy( up, out[2] );
}

static void CG_VRB_TwoHand( centity_t *cent, const vrClientState_t *vr ) {
	const clientInfo_t *ci = &cgs.clientinfo[cent->currentState.number];
	const qboolean was = vrb.twoHand;
	const float dt = Com_Clamp( 0.0f, 0.1f, ( cg.time - vrb.twoHandTime ) * 0.001f );
	vrbGrip_t gripR, gripL;
	vrbBone_t tag;
	mdxaBone_t m;
	vec3_t posR, axR[3], posL, axL[3], dirR, dirL, bladeNow, emitter, tmp;
	float tAbove[2], tBelow[2], dAbove, dBelow;
	qboolean canAbove, canBelow;
	int i;

	if ( vrb.twoHandTime == cg.time ) {
		return;	// (second eye)
	}
	vrb.twoHandTime = cg.time;
	vrb.twoHand = qfalse;
	vrb.twoHandArms = qfalse;
	vrb.grabZones = 0;
	if ( !vr->grabbing[VR_HAND_LEFT] ) {
		vrb.twoHandBroke = qfalse;	// let go of the grip: it can take hold again
	}

	// a saber in the right hand, the left hand empty, both tracked
	if ( !vr->handValid[VR_HAND_LEFT] || !vr->handValid[VR_HAND_RIGHT] || !cg_vrBodyArms.integer ||
		!cg_vrTwoHand.integer ) {
		goto off;
	}
	CG_VRB_Grip( cent, VR_HAND_RIGHT, &gripR );
	CG_VRB_Grip( cent, VR_HAND_LEFT, &gripL );
	if ( gripR.kind != VRB_GRIP_SABER || gripL.kind != VRB_GRIP_FREE ) {
		goto off;
	}

	// the hilt as the hand holds it: how far its emitters are from the hand,
	// and how the blade sits on the hand's tag (a saber bolted to the left
	// hand sits on its tag the same way)
	if ( !CG_VRB_Bone( cent, vrbArmBones[VR_HAND_RIGHT][VRB_TAG], 1, &tag ) ||
		!trap->G2API_GetBoltMatrix( cent->ghoul2, gripR.saberModel, 0, &m, cent->turAngles, cent->lerpOrigin, cg.time,
			cgs.gameModels, cent->modelScale ) ) {
		goto off;
	}
	BG_GiveMeVectorFromMatrix( &m, ORIGIN, emitter );
	BG_GiveMeVectorFromMatrix( &m, NEGATIVE_Y, bladeNow );
	if ( VectorNormalize( bladeNow ) < 0.5f ) {
		goto off;
	}
	for ( i = 0; i < 3; i++ ) {
		vrb.twoHandBladeLocal[i] = DotProduct( bladeNow, tag.axis[i] );
	}

	// Where on the hilt the left hand can hold it, along the blade from the
	// right hand and a hand's width from it: above, up to the emitter; below,
	// down a staff to its other emitter (a single hilt's pommel isn't known:
	// a hand's length or two). A stretch too short for a hand isn't offered.
	VectorSubtract( emitter, tag.origin, tmp );
	tAbove[0] = VRB_HAND_GAP;
	tAbove[1] = Q_min( 40.0f, DotProduct( tmp, bladeNow ) ) - 1.5f;
	tBelow[0] = -VRB_HILT_BELOW;
	tBelow[1] = -VRB_HAND_GAP;
	if ( ci->saber[0].numBlades > 1 && trap->G2API_GetBoltMatrix( cent->ghoul2, gripR.saberModel, 1, &m,
			cent->turAngles, cent->lerpOrigin, cg.time, cgs.gameModels, cent->modelScale ) ) {
		vec3_t blade2;
		BG_GiveMeVectorFromMatrix( &m, ORIGIN, emitter );
		BG_GiveMeVectorFromMatrix( &m, NEGATIVE_Y, blade2 );
		VectorSubtract( emitter, tag.origin, tmp );
		if ( VectorNormalize( blade2 ) > 0.5f && DotProduct( blade2, bladeNow ) < -0.5f &&
			DotProduct( tmp, bladeNow ) < 0.0f ) {
			// a staff (its second blade points back past the hand): the hilt
			// runs down to that emitter
			tBelow[0] = Q_max( -60.0f, DotProduct( tmp, bladeNow ) ) + 1.5f;
		}
	}
	canAbove = (qboolean)( tAbove[1] >= tAbove[0] );
	canBelow = (qboolean)( tBelow[1] >= tBelow[0] );
	if ( !canAbove && !canBelow ) {
		goto off;
	}

	// both controllers
	CG_VRB_Controller( vr, VR_HAND_RIGHT, posR, axR );
	CG_VRB_Controller( vr, VR_HAND_LEFT, posL, axL );
	CG_VRB_SaberDir( axR, dirR );
	CG_VRB_SaberDir( axL, dirL );
	VectorCopy( posL, vrb.twoHandGrip2 );
	dAbove = canAbove ? CG_VRB_RangeDistance( posL, posR, dirR, tAbove ) : 1e9f;
	dBelow = canBelow ? CG_VRB_RangeDistance( posL, posR, dirR, tBelow ) : 1e9f;

	if ( vr->grabbing[VR_HAND_LEFT] && !vrb.twoHandBroke ) {
		// Holding it with both hands. The blade follows both wrists, and the
		// further apart the hands, the more the line through them: one hand
		// pushing and the other pulling levers it round.
		vec3_t avg, line, dir, point;
		float sep, agree, w, t;

		if ( !was ) {
			// a new hold: which way round the left hand is on the hilt, and
			// on which side of the right one (the stretch it is nearer)
			vrb.twoHandSign = ( DotProduct( dirR, dirL ) < 0.0f ) ? -1.0f : 1.0f;
			vrb.twoHandSide = ( dAbove < dBelow ) ? 1.0f : -1.0f;
		}

		// the wrists: the right one, and the left one as far as it agrees
		VectorScale( dirL, vrb.twoHandSign, tmp );
		VectorMA( dirR, VRB_SmoothStep( -0.3f, 0.7f, DotProduct( dirR, tmp ) ), tmp, avg );
		if ( VectorNormalize( avg ) < 0.5f ) {
			VectorCopy( dirR, avg );
		}

		// the line through the hands, as far as it runs along the hilt
		VectorSubtract( posR, posL, line );
		sep = VectorNormalize( line );
		agree = DotProduct( line, avg );
		if ( agree < 0.0f ) {
			VectorScale( line, -1.0f, line );	// (the left hand nearer the blade)
			agree = -agree;
		}
		w = Com_Clamp( 0.0f, 1.0f, cg_vrTwoHandLever.value ) * VRB_SmoothStep( 2.0f, 8.0f, sep ) *
			VRB_SmoothStep( 0.3f, 0.7f, agree );
		VectorScale( avg, 1.0f - w, dir );
		VectorMA( dir, w, line, dir );
		if ( VectorNormalize( dir ) < 0.1f ) {
			VectorCopy( avg, dir );
		}

		// The left hand grips the hilt where it took hold of it: it doesn't
		// slide along it or turn round it after your hand (let go and take
		// hold again to move it).
		if ( !was ) {
			vec3_t e[3];
			int k;
			VectorSubtract( posL, posR, tmp );
			vrb.twoHandT = DotProduct( tmp, dir );
			CG_VRB_HiltFrame( dir, axR, e );
			for ( i = 0; i < 3; i++ ) {
				for ( k = 0; k < 3; k++ ) {
					vrb.twoHandGrip[i][k] = DotProduct( axL[i], e[k] );
				}
			}
		}
		if ( !( vrb.twoHandSide > 0.0f ? canAbove : canBelow ) ) {
			vrb.twoHandSide = -vrb.twoHandSide;	// (the hilt changed under it)
		}
		t = ( vrb.twoHandSide > 0.0f ) ? Com_Clamp( tAbove[0], tAbove[1], vrb.twoHandT ) :
			Com_Clamp( tBelow[0], tBelow[1], vrb.twoHandT );
		vrb.twoHandT = t;
		VectorMA( posR, t, dir, point );

		if ( Distance( posL, point ) > VRB_GRAB_BREAK ) {
			// pulled well away: it lets go (until the grip is let go and squeezed again)
			vrb.twoHandBroke = qtrue;
		} else {
			VectorCopy( dir, vrb.twoHandDir );
			VectorCopy( point, vrb.twoHandPoint );
			VectorSubtract( point, posR, tmp );
			for ( i = 0; i < 3; i++ ) {
				vrb.twoHandRel[0][i] = DotProduct( dir, axR[i] );
				vrb.twoHandRel[1][i] = DotProduct( tmp, axR[i] );
			}
			vrb.twoHand = qtrue;
		}
	}

	if ( !vrb.twoHand ) {
		// not holding it: could the left hand take hold of the hilt?
		if ( !vr->grabbing[VR_HAND_LEFT] && Q_min( dAbove, dBelow ) <= VRB_GRAB_REACH ) {
			vrb.grabZones |= 1 << VR_HAND_LEFT;
		}
		// just let go: the hold as it was, carried along by the right hand
		if ( vrb.twoHandBlend > 0.0f ) {
			VectorScale( axR[0], vrb.twoHandRel[0][0], vrb.twoHandDir );
			VectorMA( vrb.twoHandDir, vrb.twoHandRel[0][1], axR[1], vrb.twoHandDir );
			VectorMA( vrb.twoHandDir, vrb.twoHandRel[0][2], axR[2], vrb.twoHandDir );
			if ( VectorNormalize( vrb.twoHandDir ) < 0.5f ) {
				VectorCopy( dirR, vrb.twoHandDir );
			}
			VectorMA( posR, vrb.twoHandRel[1][0], axR[0], vrb.twoHandPoint );
			VectorMA( vrb.twoHandPoint, vrb.twoHandRel[1][1], axR[1], vrb.twoHandPoint );
			VectorMA( vrb.twoHandPoint, vrb.twoHandRel[1][2], axR[2], vrb.twoHandPoint );
		}
	}

	// The hands settle onto the hilt together (and come apart) over a few
	// frames, so taking hold or letting go never snaps the blade round.
	if ( vrb.twoHand ) {
		vrb.twoHandBlend = Q_min( 1.0f, vrb.twoHandBlend + dt / VRB_TWO_HAND_IN );
	} else {
		vrb.twoHandBlend = Q_max( 0.0f, vrb.twoHandBlend - dt / VRB_TWO_HAND_OUT );
	}
	if ( vrb.twoHandBlend > 0.0f ) {
		vec3_t e[3], held[3];
		int k;
		vrb.twoHandEase = VRB_SmoothStep( 0.0f, 1.0f, vrb.twoHandBlend );
		VectorScale( posL, 1.0f - vrb.twoHandEase, vrb.twoHandHand );
		VectorMA( vrb.twoHandHand, vrb.twoHandEase, vrb.twoHandPoint, vrb.twoHandHand );
		// the hand as it lies round the hilt, the controller eased onto that
		CG_VRB_HiltFrame( vrb.twoHandDir, axR, e );
		for ( i = 0; i < 3; i++ ) {
			VectorClear( held[i] );
			for ( k = 0; k < 3; k++ ) {
				VectorMA( held[i], vrb.twoHandGrip[i][k], e[k], held[i] );
			}
		}
		VRB_FrameBlend( axL, held, vrb.twoHandEase, vrb.twoHandAxis );
		vrb.twoHandArms = qtrue;
	}
	goto done;

off:
	vrb.twoHandBlend = 0.0f;
done:
	if ( vrb.twoHand ) {
		if ( !was ) {
			if ( trap->ext.VR_Haptic ) {
				trap->ext.VR_Haptic( VR_HAND_LEFT, 0.04f, 0.6f );	// took hold
			}
			CG_VRB_Log( "two hands: took hold %s the right hand (%.1f along the hilt)%s\n",
				vrb.twoHandSide > 0.0f ? "above" : "below", vrb.twoHandT, vrb.twoHandSign < 0.0f ? ", reversed" : "" );
		}
	} else {
		if ( was ) {
			if ( trap->ext.VR_Haptic ) {
				trap->ext.VR_Haptic( VR_HAND_LEFT, 0.02f, 0.3f );	// let go
			}
			CG_VRB_Log( "two hands: %s\n", vrb.twoHandBroke ? "pulled off the hilt" : "let go" );
		}
	}
}

/*
=================
CG_VRB_BladeInertia

The blade in your hand. By default it is locked to your fist (the playtest
said any trailing feels like the hilt floating in the hand). With
cg_vrSaberLag above 0 it follows your hand through a spring instead: a
light one keeps up with the quickest flick, a heavy one trails a fast swing
and carries on a touch when your hand stops, and two hands on it hold it
closer. It runs in your room, so turning and walking never swing it.
Either way another blade stops it dead for a moment, and cutting through
someone drags it (CG_VRB_BladeImpulse).
=================
*/

#define VRB_BLADE_STEP		0.002f	// s: integration step
#define VRB_BLADE_DAMPING	0.75f	// (a little overshoot when the hand stops)

static const float	vrbBladeOmega[3] = { 160.0f, 80.0f, 48.0f };	// rad/s: light, medium, heavy
static const float	vrbBladeMaxLag[3] = { 20.0f, 35.0f, 50.0f };	// degrees behind the hand at most
static const int	vrbBladeHold[3] = { 35, 55, 75 };				// ms another blade stops it for

// world yaw minus room (tracking) yaw
static float CG_VRB_RoomYaw( const vrClientState_t *vr ) {
	return VR_AxisYaw( cg.refdef.viewaxis ) - vr->referenceYaw;
}

static void CG_VRB_BladeSpring( int h, const vrClientState_t *vr, const vec3_t want, int weight, qboolean twoHands,
	vec3_t out ) {
	const float yaw = CG_VRB_RoomYaw( vr );
	const float scale = cg_vrSaberLag.value;
	float omega, maxLag, t, ang;
	vec3_t target, axis;

	VRB_RotZ( want, -yaw, target );
	if ( VectorNormalize( target ) < 0.5f ) {
		VectorCopy( want, out );
		return;
	}
	if ( !vrb.blade[h].valid || cg.time - vrb.blade[h].time < 0 || cg.time - vrb.blade[h].time > 200 ) {
		VectorCopy( target, vrb.blade[h].dir );
		VectorClear( vrb.blade[h].vel );
		vrb.blade[h].holdUntil = 0;
		vrb.blade[h].valid = qtrue;
	} else {
		if ( scale > 0.0f ) {
			omega = vrbBladeOmega[weight] / Com_Clamp( 0.1f, 4.0f, scale );
			maxLag = vrbBladeMaxLag[weight] * Com_Clamp( 0.5f, 2.0f, scale );
		} else {
			omega = 400.0f;	// locked to the fist: only clashes and cuts show
			maxLag = vrbBladeMaxLag[VRB_LIGHT];
		}
		if ( twoHands ) {
			omega *= 1.4f;
		}
		omega = Q_min( omega, 400.0f );

		if ( cg.time - vrb.blade[h].holdUntil < 0 ) {
			VectorClear( vrb.blade[h].vel );	// stopped on the other blade
		} else {
			for ( t = ( cg.time - vrb.blade[h].time ) * 0.001f; t > 0.0f; t -= VRB_BLADE_STEP ) {
				const float step = Q_min( t, VRB_BLADE_STEP );
				vec3_t acc;
				float sn, rate;
				CrossProduct( vrb.blade[h].dir, target, axis );
				sn = VectorNormalize( axis );
				ang = atan2f( sn, DotProduct( vrb.blade[h].dir, target ) );
				VectorScale( axis, omega * omega * ang, acc );
				VectorMA( acc, -2.0f * VRB_BLADE_DAMPING * omega, vrb.blade[h].vel, acc );
				VectorMA( vrb.blade[h].vel, step, acc, vrb.blade[h].vel );
				VectorMA( vrb.blade[h].vel, -DotProduct( vrb.blade[h].vel, vrb.blade[h].dir ), vrb.blade[h].dir,
					vrb.blade[h].vel );	// (it doesn't spin about itself)
				rate = VectorLength( vrb.blade[h].vel );
				if ( rate > 1e-5f ) {
					vec3_t k;
					VectorScale( vrb.blade[h].vel, 1.0f / rate, k );
					VRB_Rotate( vrb.blade[h].dir, k, rate * step, vrb.blade[h].dir );
					VectorNormalize( vrb.blade[h].dir );
				}
			}
		}

		// never further behind than this: past it the hand drags it along
		CrossProduct( vrb.blade[h].dir, target, axis );
		ang = atan2f( VectorNormalize( axis ), DotProduct( vrb.blade[h].dir, target ) );
		if ( ang > DEG2RAD( maxLag ) ) {
			VRB_Rotate( vrb.blade[h].dir, axis, ang - DEG2RAD( maxLag ), vrb.blade[h].dir );
			VectorNormalize( vrb.blade[h].dir );
			ang = DEG2RAD( maxLag );
		}
		vrb.swing.lag = Q_max( vrb.swing.lag, RAD2DEG( ang ) );	// (for the log)
	}
	vrb.blade[h].time = cg.time;
	VRB_RotZ( vrb.blade[h].dir, yaw, out );
}

static void CG_VRB_BladeInertia( centity_t *cent, const vrClientState_t *vr ) {
	const qboolean twoHands = (qboolean)( vrb.twoHand && vrb.twoHandTime == cg.time );
	const int weight = CG_VRB_Weight( qfalse );
	int h;

	if ( vrb.bladeTime == cg.time ) {
		return;	// (second eye)
	}
	vrb.bladeTime = cg.time;
	VectorCopy( vrb.twoHandDir, vrb.twoHandFist );

	for ( h = 0; h < 2; h++ ) {
		vrbGrip_t grip;
		vec3_t pos, ax[3], want, out;

		vrb.blade[h].outTime = 0;
		CG_VRB_Grip( cent, h, &grip );
		if ( grip.kind != VRB_GRIP_SABER || !vr->handValid[h] ) {
			vrb.blade[h].valid = qfalse;
			continue;
		}
		CG_VRB_Controller( vr, h, pos, ax );
		CG_VRB_SaberDir( ax, want );
		if ( h == VR_HAND_RIGHT && vrb.twoHandArms && vrb.twoHandTime == cg.time ) {
			vec3_t tmp;
			VRB_Slerp( want, vrb.twoHandDir, vrb.twoHandEase, tmp );	// (held with both hands)
			VectorCopy( tmp, want );
		}
		CG_VRB_BladeSpring( h, vr, want, weight, (qboolean)( twoHands && h == VR_HAND_RIGHT ), out );
		VectorCopy( out, vrb.blade[h].out );
		vrb.blade[h].outTime = cg.time;

		if ( h == VR_HAND_RIGHT && vrb.twoHandArms && vrb.twoHandTime == cg.time ) {
			// the left hand holds the hilt where it is drawn
			vrbMat_t R;
			vec3_t rel;
			VRB_Arc( want, out, R );
			VectorSubtract( vrb.twoHandHand, pos, rel );
			VRB_Apply( R, rel, rel );
			VectorAdd( pos, rel, vrb.twoHandHand );
			VRB_Apply( R, vrb.twoHandDir, vrb.twoHandFist );
		}
	}
}

// Your blade met another one (clash) or cut through someone (strength 0..1).
static void CG_VRB_BladeImpulse( int h, qboolean clash, float strength ) {
	const int weight = CG_VRB_Weight( qfalse );

	if ( h < 0 || h > 1 || !vrb.blade[h].valid ) {
		return;
	}
	strength = Com_Clamp( 0.0f, 1.0f, strength );
	if ( clash ) {
		vrb.blade[h].holdUntil = cg.time + (int)( vrbBladeHold[weight] * ( 0.5f + 0.5f * strength ) );
		VectorClear( vrb.blade[h].vel );
	} else {
		VectorScale( vrb.blade[h].vel, 1.0f - 0.6f * strength, vrb.blade[h].vel );
	}
}

/*
=================
CG_VRB_SolveArm

Two-bone IK for one arm, in the world, then the wrist. Reads the arm as the
game animated it (overrides reset), works out where the hand has to be and
how it has to be turned for what it holds, puts the wrist there with the
upper arm and forearm, and turns the hand at the wrist. "commit" keeps the
smoothing/hinge state (only the pass that is finally drawn does).
=================
*/
static qboolean CG_VRB_SolveArm( centity_t *cent, const vrClientState_t *vr, int h, int convention,
	float bodyYaw, qboolean commit, const vrbGrip_t *grip, vrbArmResult_t *res ) {
	vrbBone_t b[VRB_NUM_ARM_BONES];
	qboolean have[VRB_NUM_ARM_BONES];
	vec3_t gripO, gA[3];
	vec3_t S, E0, W0, kmid, Ah[3], Ad[3], anchor, Wt, u, v, E, u0, u1, f0, f1, n0, n1, c0, c1, tmp;
	vec3_t bodyFwd, bodyLeft, outward, hint, down = { 0.0f, 0.0f, -1.0f };
	vec3_t F0[3], F1[3], want[3], cur[3];
	vrbMat_t Y0, Y, Q1, A, X;
	vec3_t holdO, holdDir, sentDir;
	float a, len2, d, dmin, dmax, cosA, sinA, w, pitch, handLen = 0.0f;
	qboolean haveHinge = qfalse, anatomy = qfalse, inFist;
	int i;

	memset( res, 0, sizeof( *res ) );

	for ( i = 0; i < VRB_NUM_ARM_BONES; i++ ) {
		have[i] = CG_VRB_Bone( cent, vrbArmBones[h][i], convention, &b[i] );
	}
	if ( !have[VRB_UPPER] || !have[VRB_FORE] || !have[VRB_HAND] ) {
		return qfalse;	// this skeleton has no such arm
	}
	VectorCopy( b[VRB_UPPER].origin, S );
	VectorCopy( b[VRB_FORE].origin, E0 );
	VectorCopy( b[VRB_HAND].origin, W0 );
	a = Distance( S, E0 );
	len2 = Distance( E0, W0 );
	if ( a < 1.0f || len2 < 1.0f ) {
		return qfalse;
	}

	// the controller in the world (or, holding the other hand's hilt, a
	// controller lying on that)
	if ( grip->kind == VRB_GRIP_HILT ) {
		VectorCopy( grip->origin, gripO );
		for ( i = 0; i < 3; i++ ) {
			VectorCopy( grip->axis[i], gA[i] );
		}
	} else {
		CG_VRB_Controller( vr, h, gripO, gA );
	}

	// The hand as animated, by anatomy: wrist -> knuckles, and across the
	// knuckles from the little finger to the index finger (the thumb is on
	// that side; if a model has them the other way round, flip) - and where a
	// hilt runs through its fist.
	if ( have[VRB_THUMB] && have[VRB_INDEX] && have[VRB_LITTLE] ) {
		VectorAdd( b[VRB_INDEX].origin, b[VRB_LITTLE].origin, kmid );
		VectorScale( kmid, 0.5f, kmid );
		anatomy = VRB_HandFrame( W0, b[VRB_THUMB].origin, b[VRB_INDEX].origin, b[VRB_LITTLE].origin, Ah, &handLen );
		if ( anatomy ) {
			VRB_GripLine( h, W0, Ah, handLen, holdO, holdDir );
		}
	}
	// A hilt goes through the fist there (a saber is moved there,
	// CG_VRB_PlaceSaber), not where the model's own tag would put it.
	// (Not a saber bolted to the wrist - claws and the like: that's where it goes.)
	inFist = (qboolean)( anatomy && ( grip->kind == VRB_GRIP_HILT || ( grip->kind == VRB_GRIP_SABER &&
		vrb.tagMoves[h] && vrb.probeG2 == cent->ghoul2 && trap->ext.VR_G2SetBoneTransform &&
		grip->saberModel >= 1 && grip->saberModel <= MAX_SABERS &&
		!( cgs.clientinfo[cent->currentState.number].saber[grip->saberModel - 1].saberFlags & SFL_BOLT_TO_WRIST ) ) ) );

	// Y0 turns the animated hand to lie on the controller like a real one:
	// OpenXR's grip pose runs its -Z through the fist from the little finger
	// to the thumb, its +Y back toward the wrist (Quake axes: gA[0] = -Z,
	// gA[2] = +Y).
	if ( anatomy ) {
		VectorScale( gA[2], -1.0f, Ad[0] );
		VectorCopy( gA[0], Ad[1] );
		pitch = vrbRemote ? 0.0f : cg_vrHandPitch.value;	// (another player's avatar has theirs in already)
		if ( pitch != 0.0f ) {
			RotatePointAroundVector( tmp, Ad[1], Ad[0], -pitch );
			VectorCopy( tmp, Ad[0] );
		}
		CrossProduct( Ad[0], Ad[1], Ad[2] );
		VRB_FrameMap( Ah, Ad, Y0 );
	} else {
		VRB_Identity( Y0 );
	}

	VectorCopy( gA[0], sentDir );

	// Y: how the hand has to be turned for what it holds; Wt: where the
	// wrist then goes so the hold point lands on target
	if ( grip->kind == VRB_GRIP_GUN && have[VRB_TAG] ) {
		// Exactly the game's own hold: a gun bolted to the hand has the tag's
		// own axes. The gun drawn on its own (refEntity axis A) gets the
		// renderer's 90-degree root on top (tr_ghoul2.cpp identityMatrix), so
		// its model axes are A * R90 = ( A[1], -A[0], A[2] ).
		vec3_t gunAxis[3];
		VectorCopy( grip->axis[1], gunAxis[0] );
		VectorScale( grip->axis[0], -1.0f, gunAxis[1] );
		VectorCopy( grip->axis[2], gunAxis[2] );
		VRB_FrameMap( b[VRB_TAG].axis, gunAxis, Y );
		VectorSubtract( b[VRB_TAG].origin, W0, tmp );
		VRB_Apply( Y, tmp, tmp );
		VectorSubtract( grip->origin, tmp, Wt );
		res->tagTarget = qtrue;
		VectorCopy( grip->origin, res->tagOrigin );
		VectorCopy( gunAxis[0], res->tagAxis[0] );
		VectorCopy( gunAxis[1], res->tagAxis[1] );
		VectorCopy( gunAxis[2], res->tagAxis[2] );
	} else {
		memcpy( Y, Y0, sizeof( Y ) );
		if ( grip->kind == VRB_GRIP_SABER ) {
			// the blade along your fist (tilted by cg_vrSaberPitch), the
			// hand turned as little as that allows from lying naturally
			mdxaBone_t m;
			vec3_t bladeNow, bladeWant;
			const float sp = DEG2RAD( cg_vrSaberPitch.value );
			if ( trap->G2API_GetBoltMatrix( cent->ghoul2, grip->saberModel, 0, &m, cent->turAngles,
					cent->lerpOrigin, cg.time, cgs.gameModels, cent->modelScale ) ) {
				BG_GiveMeVectorFromMatrix( &m, NEGATIVE_Y, bladeNow );
				if ( inFist ) {
					VectorCopy( holdDir, bladeNow );	// (where it's going to be)
				}
				VectorScale( gA[0], cosf( sp ), bladeWant );
				VectorMA( bladeWant, -sinf( sp ), gA[2], bladeWant );
				if ( vrb.blade[h].outTime == cg.time ) {
					VectorCopy( vrb.blade[h].out, bladeWant );	// where its weight lets it be (CG_VRB_BladeInertia)
				} else if ( h == VR_HAND_RIGHT && vrb.twoHandArms && vrb.twoHandTime == cg.time ) {
					// held with both hands (or settling into it, or out of it)
					VectorNormalize( bladeWant );
					VRB_Slerp( bladeWant, vrb.twoHandDir, vrb.twoHandEase, tmp );
					VectorCopy( tmp, bladeWant );
				}
				if ( VectorNormalize( bladeNow ) > 0.5f && VectorNormalize( bladeWant ) > 0.5f ) {
					VRB_Apply( Y0, bladeNow, tmp );
					VRB_Arc( tmp, bladeWant, A );
					VRB_Mul( A, Y0, Y );
					VectorCopy( bladeWant, sentDir );
				}
			}
		} else if ( grip->kind == VRB_GRIP_HILT ) {
			// The other hand's hilt through this fist the way a saber of its
			// own would sit in it (bolted to its tag: the left hand of the
			// dual-saber stance), the hand turned as little as that allows
			// from resting on the controller.
			vrbBone_t own;
			vec3_t bladeNow, bladeWant;
			// (another player's hand without a fist frame: the way their hilt
			// lay in it isn't known here - their hand just rests on it)
			if ( inFist || ( !vrbRemote && CG_VRB_Bone( cent, vrbArmBones[h][VRB_TAG], 1, &own ) ) ) {
				if ( inFist ) {
					VectorCopy( holdDir, bladeNow );
				} else {
					VectorScale( own.axis[0], vrb.twoHandBladeLocal[0], bladeNow );
					VectorMA( bladeNow, vrb.twoHandBladeLocal[1], own.axis[1], bladeNow );
					VectorMA( bladeNow, vrb.twoHandBladeLocal[2], own.axis[2], bladeNow );
				}
				if ( VectorNormalize( bladeNow ) > 0.5f ) {
					VRB_Apply( Y0, bladeNow, tmp );
					VRB_Slerp( tmp, grip->dir, grip->blend, bladeWant );
					VRB_Arc( tmp, bladeWant, A );
					VRB_Mul( A, Y0, Y );
				}
			}
		}
		// the hold point (inside the fist) on the controller's grip - an empty
		// hand too, so taking hold of a hilt and letting go don't jump it
		if ( inFist || ( anatomy && grip->kind == VRB_GRIP_FREE ) ) {
			VectorCopy( holdO, anchor );
		} else if ( have[VRB_TAG] ) {
			VectorCopy( b[VRB_TAG].origin, anchor );
		} else if ( anatomy ) {
			VectorAdd( W0, kmid, anchor );
			VectorScale( anchor, 0.5f, anchor );
		} else {
			VectorCopy( W0, anchor );
		}
		VectorSubtract( anchor, W0, tmp );
		VRB_Apply( Y, tmp, tmp );
		VectorSubtract( gripO, tmp, Wt );
	}

	// reach: shoulder -> wrist distance the arm can actually make
	VectorSubtract( Wt, S, u );
	d = VectorNormalize( u );
	if ( d < 0.01f ) {
		return qfalse;
	}
	dmin = fabsf( a - len2 ) + 0.02f * ( a + len2 );
	dmax = 0.995f * ( a + len2 );
	res->reachExcess = d - dmax;
	VectorCopy( u, res->reachDir );
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

	// Forearm (the hand rides on it): the hand's turn, corrected by the least
	// turn that points the forearm at the wrist. The twist stays in the
	// forearm, like a real one.
	VRB_Apply( Y, f0, tmp );
	VRB_Arc( tmp, f1, A );
	VRB_Mul( A, Y, X );

	// overrides: upper arm first, then the forearm relative to its new parent
	VRB_RotateFrame( Q1, b[VRB_UPPER].axis, want );
	CG_VRB_SetBone( cent->ghoul2, vrbArmBones[h][VRB_UPPER], b[VRB_UPPER].axis, want );
	VRB_RotateFrame( Q1, b[VRB_FORE].axis, cur );
	VRB_RotateFrame( X, b[VRB_FORE].axis, want );
	CG_VRB_SetBone( cent->ghoul2, vrbArmBones[h][VRB_FORE], cur, want );

	// Wrist: the bend that's left (Y = D X), about the wrist
	{
		static const mdxaBone_t identity = { { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 } } };
		mdxaBone_t H = identity;
		if ( cg_vrBodyWrist.integer ) {
			vrbMat_t D;
			VRB_Apply( Y, f0, tmp );
			VRB_Arc( f1, tmp, D );
			if ( !CG_VRB_TurnWrist( cent, h, D, Wt, &H ) ) {
				H = identity;
			}
		}

		// fingers closed round whatever hilt the hand holds
		if ( grip->kind == VRB_GRIP_HILT ) {
			// (only while it holds it, closing as it settles on)
			CG_VRB_CurlFingers( cent, h, &H, grip->origin, grip->dir, vrb.twoHand ? grip->blend : 0.0f );
		} else if ( grip->kind == VRB_GRIP_SABER && inFist && CG_VRB_PlaceSaber( cent, h, &H, grip->saberModel, holdO, holdDir ) ) {
			// the saber moved into the fist, the fingers round it there
			CG_VRB_CurlFingers( cent, h, &H, holdO, holdDir, 1.0f );
		} else if ( grip->kind == VRB_GRIP_SABER && have[VRB_TAG] ) {
			vrbBone_t tag;
			mdxaBone_t m;
			vec3_t emitter, blade;
			// (a model-0 bone first: that rebuilds the skeleton the saber hangs on)
			if ( CG_VRB_Bone( cent, vrbArmBones[h][VRB_TAG], 1, &tag ) &&
				trap->G2API_GetBoltMatrix( cent->ghoul2, grip->saberModel, 0, &m, cent->turAngles,
					cent->lerpOrigin, cg.time, cgs.gameModels, cent->modelScale ) ) {
				BG_GiveMeVectorFromMatrix( &m, ORIGIN, emitter );
				BG_GiveMeVectorFromMatrix( &m, NEGATIVE_Y, blade );
				CG_VRB_CurlFingers( cent, h, &H, emitter, blade, 1.0f );
			}
		}
	}

	if ( commit ) {
		vrb.armTime[h] = cg.time;
		VectorCopy( gripO, vrb.armGrip[h] );
	}
	if ( commit && !vrbRemote ) {
		// your hand as everyone else will see it (CG_VR_BodyEndFrame): what
		// it holds, and the controller it lies on with the hand's pitch in
		vrAvatarHand_t *sent = &vrb.sent[h];
		memset( sent, 0, sizeof( *sent ) );
		VectorCopy( gripO, sent->origin );
		if ( anatomy ) {
			VectorCopy( Ad[1], sent->axis[0] );
			VectorScale( Ad[0], -1.0f, sent->axis[2] );
			CrossProduct( sent->axis[2], sent->axis[0], sent->axis[1] );
		} else {
			VectorCopy( gA[0], sent->axis[0] );
			VectorCopy( gA[1], sent->axis[1] );
			VectorCopy( gA[2], sent->axis[2] );
		}
		switch ( grip->kind ) {
		case VRB_GRIP_GUN:
			sent->grip = VR_GRIP_GUN;
			VectorCopy( grip->origin, sent->origin );
			VectorCopy( grip->axis[0], sent->axis[0] );
			VectorCopy( grip->axis[1], sent->axis[1] );
			VectorCopy( grip->axis[2], sent->axis[2] );
			break;
		case VRB_GRIP_SABER:
			sent->grip = VR_GRIP_SABER;
			VectorCopy( sentDir, sent->dir );
			break;
		case VRB_GRIP_HILT:
			if ( vrb.twoHand ) {
				sent->grip = VR_GRIP_HILT;
				VectorCopy( grip->dir, sent->dir );
				sent->blend = grip->blend;
			} else {
				sent->grip = VR_GRIP_FREE;	// (letting go: the hand coming off it, fingers open)
			}
			break;
		default:
			sent->grip = VR_GRIP_FREE;
			break;
		}
		vrb.sentTime[h] = cg.time;
	}
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
	vrbGrip_t grip;
	int h, n = 0;

	CG_VRB_ResetArms( cent->ghoul2 );
	for ( h = 0; h < 2; h++ ) {
		memset( &res[h], 0, sizeof( res[h] ) );
		if ( !vr->handValid[h] ) {
			continue;
		}
		CG_VRB_Grip( cent, h, &grip );
		if ( CG_VRB_SolveArm( cent, vr, h, convention, bodyYaw, commit, &grip, &res[h] ) ) {
			n++;
		}
	}
	return n;
}

// cg_vrBodyDebug 2: how well the held-item tag met its target
static void CG_VRB_ReportGrip( centity_t *cent, vrbArmResult_t res[2] ) {
	vrbBone_t tag;
	float ang, worst;
	int h, i;

	for ( h = 0; h < 2; h++ ) {
		if ( !res[h].tagTarget || !CG_VRB_Bone( cent, vrbArmBones[h][VRB_TAG], vrb.convention, &tag ) ) {
			continue;
		}
		worst = 0.0f;
		for ( i = 0; i < 3; i++ ) {
			ang = RAD2DEG( acosf( Com_Clamp( -1.0f, 1.0f, DotProduct( tag.axis[i], res[h].tagAxis[i] ) ) ) );
			worst = Q_max( worst, ang );
		}
		Com_Printf( "VR body: %s grip off by %.2f units, %.1f degrees\n", h == VR_HAND_RIGHT ? "right" : "left",
			Distance( tag.origin, res[h].tagOrigin ), worst );
	}
}

// When a hand can't reach forward far enough, the body leans in for the
// next frames (up to having its eyes at yours), instead of the hand falling
// short of what it holds; with reach to spare it leans back out.
static void CG_VRB_UpdateLean( vrbArmResult_t res[2], float bodyYaw ) {
	vec3_t fwd;
	float want = -1e6f, f;
	int h;

	if ( vrb.leanSetTime == cg.time ) {
		return;		// (second eye)
	}
	vrb.leanSetTime = cg.time;
	VectorSet( fwd, cosf( DEG2RAD( bodyYaw ) ), sinf( DEG2RAD( bodyYaw ) ), 0.0f );
	for ( h = 0; h < 2; h++ ) {
		f = res[h].solved ? DotProduct( res[h].reachDir, fwd ) : 0.0f;
		if ( f > 0.3f ) {
			// leaning in by x brings the wrist target about x*f closer
			want = Q_max( want, res[h].reachExcess / f );
		}
	}
	// measured with this frame's lean already in (want < 0: reach to spare)
	vrb.leanTarget = ( want > -1e5f ) ? Com_Clamp( 0.0f, Q_max( 0.0f, cg_vrBodyBack.value ), vrb.lean + want ) : 0.0f;
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
	CG_VRB_ResetArms( cent->ghoul2 );
	CG_VRB_ProbeHands( cent );
	if ( !vrbRemote ) {
		// (another player's: their avatar says, CG_VRB_OtherArms)
		CG_VRB_TwoHand( cent, vr );
		CG_VRB_BladeInertia( cent, vr );
	}
	CG_VRB_HandPoses( cent );

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
		if ( !vrbRemote ) {
			CG_VRB_UpdateLean( res, bodyYaw );
		}
		return;
	}

	if ( vrb.convention < 0 ) {
		// second eye of a frame that is still checking: same pick as the first
		CG_VRB_SolveArms( cent, vr, vrb.lastBest, bodyYaw, qtrue, res );
		return;
	}

	CG_VRB_SolveArms( cent, vr, vrb.convention, bodyYaw, qtrue, res );
	if ( !vrbRemote ) {
		CG_VRB_UpdateLean( res, bodyYaw );
	}

	// occasional re-check (a model with a different skeleton could need another convention)
	if ( cg.time >= vrb.nextCheck || cg_vrBodyDebug.integer > 1 ) {
		const float e = CG_VRB_ArmError( cent, res, &meaningful );
		vrb.nextCheck = cg.time + 2000;
		if ( cg_vrBodyDebug.integer > 1 ) {
			Com_Printf( "VR body: arm error %.2f%s\n", e, meaningful ? "" : " (not checked)" );
			CG_VRB_ReportGrip( cent, res );
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

other players' VR bodies

Another player in VR is drawn from the avatar their game sends with every
command (through a server running this build; qcommon/vr_public.h): their
body faces where theirs does and stays upright through flips like it does
for them, their head looks where they look, and their arms are posed by the
same solver as yours - their hands where theirs are, holding what they hold,
the saber along their real blade, which their blows are judged by.

=============================================================================
*/

// Two of a player's avatars, a fraction f of the way from a to b.
static void CG_VRB_BlendAvatar( const vrAvatar_t *a, const vrAvatar_t *b, float f, vrAvatar_t *out ) {
	int h, i;

	*out = ( f < 0.5f ) ? *a : *b;
	out->valid = qtrue;
	out->bodyYaw = LerpAngle( a->bodyYaw, b->bodyYaw, f );
	for ( i = 0; i < 3; i++ ) {
		out->headAngles[i] = LerpAngle( a->headAngles[i], b->headAngles[i], f );
		out->shift[i] = a->shift[i] + f * ( b->shift[i] - a->shift[i] );
	}
	for ( h = 0; h < 2; h++ ) {
		const vrAvatarHand_t *ha = &a->hand[h], *hb = &b->hand[h];
		vrAvatarHand_t *ho = &out->hand[h];
		vec3_t axA[3], axB[3];
		if ( ha->grip != hb->grip || ha->grip <= VR_GRIP_NONE || ha->grip >= VR_NUM_GRIPS ) {
			continue;	// (it took hold or let go: the nearer one; no hand: nothing to blend)
		}
		for ( i = 0; i < 3; i++ ) {
			ho->origin[i] = ha->origin[i] + f * ( hb->origin[i] - ha->origin[i] );
			VectorCopy( ha->axis[i], axA[i] );
			VectorCopy( hb->axis[i], axB[i] );
		}
		VRB_FrameBlend( axA, axB, f, ho->axis );
		if ( ha->grip == VR_GRIP_SABER || ha->grip == VR_GRIP_HILT ) {
			VRB_Slerp( ha->dir, hb->dir, f, ho->dir );
		}
		ho->blend = ha->blend + f * ( hb->blend - ha->blend );
	}
}

// A player's avatar at this frame's time, between the snapshots their
// entity is drawn between (the hands offsets from where the player is
// drawn). qfalse: they have none.
static qboolean CG_VRB_InterpolateAvatar( centity_t *cent, vrAvatar_t *out ) {
	const int n = cent->currentState.number;
	vrAvatar_t a, b;
	qboolean lerp = cent->interpolate, haveA, haveB;
	int h;

	if ( n == cg.snap->ps.clientNum ) {
		// the player you follow as a spectator, drawn from the playerstate:
		// it moves between the snapshots unless the next one jumps
		lerp = ( cg.nextSnap && cg.nextSnap->ps.clientNum == n && !cg.nextFrameTeleport ) ? qtrue : qfalse;
	}
	haveA = trap->ext.VR_GetAvatar( cg.snap->serverTime, n, &a );
	haveB = ( lerp && cg.nextSnap && trap->ext.VR_GetAvatar( cg.nextSnap->serverTime, n, &b ) ) ? qtrue : qfalse;
	if ( haveA && haveB ) {
		CG_VRB_BlendAvatar( &a, &b, cg.frameInterpolation, out );
	} else if ( haveA ) {
		*out = a;
	} else if ( haveB && cg.frameInterpolation >= 0.5f ) {
		*out = b;	// (just started)
	} else {
		return qfalse;
	}
	for ( h = 0; h < 2; h++ ) {
		if ( out->hand[h].grip <= VR_GRIP_NONE || out->hand[h].grip >= VR_NUM_GRIPS ) {
			out->hand[h].grip = VR_GRIP_NONE;
		}
	}
	return qtrue;
}

// Another player in VR, drawn as their VR body (cg_vrOthers): their avatar,
// worked out once a frame (and again if the snapshots change).
static qboolean CG_VRB_OtherUsable( centity_t *cent ) {
	const int n = cent->currentState.number;
	vrbState_t *st;

	if ( n < 0 || n >= MAX_CLIENTS || n == cg.clientNum || !cg_vrOthers.integer || !cg.snap ) {
		return qfalse;
	}
	if ( !CG_VR_Engine() || !trap->ext.VR_GetAvatar || !trap->ext.VR_G2SetBoneTransform ) {
		return qfalse;	// (an engine without them)
	}
	if ( cent->currentState.eType != ET_PLAYER || !cent->ghoul2 || cent->localAnimIndex > 1 ) {
		return qfalse;	// not a humanoid player
	}
	if ( ( cent->currentState.eFlags & ( EF_DEAD | EF_RAG | EF_NODRAW ) ) || cent->isRagging ||
		( cent->currentState.eFlags2 & EF2_HELD_BY_MONSTER ) || cent->currentState.heldByClient ||
		cent->currentState.m_iVehicleNum || cent->currentState.weapon == WP_EMPLACED_GUN ) {
		return qfalse;	// the game's own posing wins
	}
	st = &vrbOthers[n];
	if ( !st->init ) {
		vrbCur = st;
		CG_VRB_Init();
		vrbCur = &vrbSelf;
	}
	if ( st->avatarTime != cg.time || st->avatarSnap != cg.snap->serverTime ||
		st->avatarNext != ( cg.nextSnap ? cg.nextSnap->serverTime : -1 ) ) {
		st->avatarTime = cg.time;
		st->avatarSnap = cg.snap->serverTime;
		st->avatarNext = cg.nextSnap ? cg.nextSnap->serverTime : -1;
		st->avatarValid = CG_VRB_InterpolateAvatar( cent, &st->avatar );
	}
	return st->avatarValid;
}

// Where another VR player's head looks: where theirs does (the game would
// turn it toward their aim). qfalse: the game's own look.
qboolean CG_VR_BodyLook( centity_t *cent, vec3_t lookAngles ) {
	if ( cent->currentState.number == cg.clientNum || !CG_VRB_OtherUsable( cent ) ) {
		return qfalse;
	}
	VectorCopy( vrbOthers[cent->currentState.number].avatar.headAngles, lookAngles );
	return qtrue;
}

// Another VR player's body stood under their head, as their game stands it
// (undone in CG_VR_BodyEndFrame).
static void CG_VRB_OtherPose( centity_t *cent, refEntity_t *legs ) {
	const int n = cent->currentState.number;

	if ( !CG_VRB_OtherUsable( cent ) ) {
		return;
	}
	vrbCur = &vrbOthers[n];
	CG_VRB_Init();
	CG_VRB_ResetArms( cent->ghoul2 );	// (the animated pose, without last frame's arm overrides)
	vrb.armsG2 = cent->ghoul2;
	VectorAdd( cent->lerpOrigin, vrb.avatar.shift, cent->lerpOrigin );
	VectorCopy( cent->lerpOrigin, legs->origin );
	VectorCopy( cent->lerpOrigin, legs->oldorigin );
	VectorCopy( cent->lerpOrigin, legs->lightingOrigin );
	vrb.shiftedEnt = cent;
	VectorCopy( vrb.avatar.shift, vrb.shift );
	vrbCur = &vrbSelf;
}

// The avatar of the body being posed, its hands in the world: from where
// the player is drawn now - after the game's own smoothing moved them, and
// without the body's shift (CG_VRB_OtherPose).
static void CG_VRB_AvatarInWorld( centity_t *cent, vrAvatar_t *world ) {
	vec3_t base;
	int h;

	*world = vrb.avatar;
	VectorCopy( cent->lerpOrigin, base );
	if ( vrb.shiftedEnt == cent ) {
		VectorSubtract( base, vrb.shift, base );
	}
	for ( h = 0; h < 2; h++ ) {
		if ( world->hand[h].grip != VR_GRIP_NONE ) {
			VectorAdd( world->hand[h].origin, base, world->hand[h].origin );
		}
	}
}

// Another VR player's arms: the hands where theirs are, holding what they
// hold - the same solver as yours, their avatar standing in for the headset.
static void CG_VRB_OtherArms( centity_t *cent ) {
	const int n = cent->currentState.number;
	vrClientState_t vr;
	vrAvatar_t world;
	int h;

	if ( !CG_VRB_OtherUsable( cent ) ) {
		return;
	}
	vrbCur = &vrbOthers[n];
	CG_VRB_Init();
	CG_VRB_AvatarInWorld( cent, &world );
	vrbRemote = &world;
	if ( cg_vrBodyArms.integer ) {
		memset( &vr, 0, sizeof( vr ) );
		vr.active = qtrue;
		for ( h = 0; h < 2; h++ ) {
			const vrAvatarHand_t *ah = &world.hand[h];
			vr.handValid[h] = ( ah->grip != VR_GRIP_NONE ) ? qtrue : qfalse;
			// a blade points where theirs does (not where a controller would)
			if ( ah->grip == VR_GRIP_SABER ) {
				vrb.blade[h].valid = qtrue;
				VectorCopy( ah->dir, vrb.blade[h].out );
				vrb.blade[h].outTime = cg.time;
			} else {
				vrb.blade[h].valid = qfalse;
			}
		}
		// the left hand on the right hand's hilt
		vrb.twoHandTime = cg.time;
		vrb.twoHand = vrb.twoHandArms = ( vrb.avatar.hand[VR_HAND_LEFT].grip == VR_GRIP_HILT ) ? qtrue : qfalse;
		vrb.twoHandEase = vrb.twoHand ? vrb.avatar.hand[VR_HAND_LEFT].blend : 0.0f;
		CG_VRB_Arms( cent, &vr, vrb.avatar.bodyYaw );
	} else {
		CG_VRB_ResetArms( cent->ghoul2 );
		CG_VRB_Fist( cent, VR_HAND_LEFT, qfalse );
		CG_VRB_Fist( cent, VR_HAND_RIGHT, qfalse );
	}
	vrbRemote = NULL;
	vrbCur = &vrbSelf;
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

	if ( cent->currentState.number != cg.clientNum ) {
		CG_VRB_OtherPose( cent, legs );
		return;
	}
	CG_VRB_Init();
	if ( !vr || !cent->ghoul2 ) {
		return;
	}
	yaw = CG_VRB_BodyYaw( vr );
	vrb.bodyCent = cent;
	vrb.bodyTime = cg.time;

	// your eyes are the camera: no head in the way
	if ( CG_VRB_HeadSurface( cent->ghoul2, vrb.headSurf, sizeof( vrb.headSurf ) ) ) {
		trap->G2API_SetSurfaceOnOff( cent->ghoul2, vrb.headSurf, VRB_TURN_OFF );
		vrb.headG2 = cent->ghoul2;
	}

	// The gun you see is the one at your controller (cg_weapons.c), not the
	// copy bolted to the body's hand. A saber stays on the body: it is
	// drawn in the body's hand, which is on your controller.
	if ( trap->ext.VR_G2SetModelHidden ) {
		if ( cent->currentState.weapon != WP_SABER && cg_vrBodyDebug.integer != 3 ) {
			if ( trap->ext.VR_G2SetModelHidden( cent->ghoul2, 1, qtrue ) ) {
				vrb.weaponG2 = cent->ghoul2;
			}
		} else if ( vrb.weaponG2 == cent->ghoul2 ) {
			// (cg_vrBodyDebug 3 shows it, to check it sits exactly on the VR gun)
			trap->ext.VR_G2SetModelHidden( cent->ghoul2, 1, qfalse );
			vrb.weaponG2 = NULL;
		}
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
	// (leaning in eases toward what the arms asked for last frame)
	if ( vrb.leanTime != cg.time ) {
		const float dt = Com_Clamp( 0.0f, 0.1f, ( cg.time - vrb.leanTime ) * 0.001f );
		if ( cg.time - vrb.leanTime > 500 || cg.time < vrb.leanTime ) {
			vrb.lean = vrb.leanTarget = 0.0f;
		}
		vrb.lean += ( vrb.leanTarget - vrb.lean ) * ( 1.0f - expf( -dt / 0.08f ) );
		vrb.leanTime = cg.time;
	}
	if ( haveEyes && CG_VR_HeadPose( head, headAxis ) ) {
		VectorSet( fwd, cosf( DEG2RAD( yaw ) ), sinf( DEG2RAD( yaw ) ), 0.0f );
		VectorMA( head, vrb.lean - cg_vrBodyBack.value, fwd, head );
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
		// to the body (force powers from your hand, the saber) come from the
		// drawn model. CG_VR_BodyEndFrame puts it back.
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

	if ( cent->currentState.number != cg.clientNum ) {
		CG_VRB_OtherArms( cent );
		return;
	}
	if ( !vr || !cent->ghoul2 ) {
		return;
	}
	if ( cg_vrBodyArms.integer ) {
		CG_VRB_Arms( cent, vr, CG_VRB_BodyYaw( vr ) );
		CG_VRB_LogHands( cent );
	} else {
		CG_VRB_ResetArms( cent->ghoul2 );
		CG_VRB_Fist( cent, VR_HAND_LEFT, qfalse );	// (no hands to hold the hilt with)
		CG_VRB_Fist( cent, VR_HAND_RIGHT, qfalse );
		vrb.twoHand = vrb.twoHandArms = qfalse;
		vrb.twoHandBlend = 0.0f;
		vrb.blade[0].valid = vrb.blade[1].valid = qfalse;
	}
}

// The whoosh of your swings. The stock game plays it from its attack
// animations, which a swing of your arm doesn't have: here it comes from how
// fast your blade really moves (two thirds along it, in your room), heavier
// for a heavier weight, with a light tick in the hand. (Everyone else hears
// it from the server, g_vrsaber.c.)
static const float vrbWhooshSpeed[3] = { 3.0f, 3.5f, 4.0f };	// m/s: light, medium, heavy

static void CG_VRB_SwingSound( const vrClientState_t *vr ) {
	const float dt = ( cg.time - vrb.swing.prevTime ) * 0.001f;
	const float yaw = vr ? CG_VRB_RoomYaw( vr ) : 0.0f;
	float speed = 0.0f;
	vec3_t where = { 0.0f, 0.0f, 0.0f };
	int s, b, mask[VR_POSE_MAX_SABERS], hand = VR_HAND_RIGHT, weight;

	if ( !vr || vrbSaber.time != cg.time || !vrbSaber.pose.valid || vr->worldScale <= 0.0f ) {
		vrb.swing.prevMask[0] = vrb.swing.prevMask[1] = 0;
		vrb.swing.speed = 0.0f;
		vrb.swing.armed = qtrue;
		return;
	}
	for ( s = 0; s < VR_POSE_MAX_SABERS; s++ ) {
		mask[s] = vrbSaber.pose.bladeMask[s];
		for ( b = 0; b < VR_POSE_MAX_BLADES; b++ ) {
			vec3_t p, rel, room;
			if ( !( mask[s] & ( 1 << b ) ) ) {
				continue;
			}
			VectorSubtract( vrbSaber.tip[s][b], vrbSaber.base[s][b], rel );
			VectorMA( vrbSaber.base[s][b], 0.66f, rel, p );
			VectorSubtract( p, cg.refdef.vieworg, rel );
			VRB_RotZ( rel, -yaw, room );
			if ( ( vrb.swing.prevMask[s] & ( 1 << b ) ) && dt > 0.001f && dt < 0.1f ) {
				const float v = Distance( room, vrb.swing.prev[s][b] ) / dt / vr->worldScale;
				if ( v > speed && v < 30.0f ) {	// (faster: it jumped - a respawn, a flip - rather than swung)
					speed = v;
					VectorCopy( p, where );
					hand = ( s == 1 ) ? VR_HAND_LEFT : VR_HAND_RIGHT;
				}
			}
			VectorCopy( room, vrb.swing.prev[s][b] );
		}
	}
	vrb.swing.prevMask[0] = mask[0];
	vrb.swing.prevMask[1] = mask[1];
	vrb.swing.prevTime = cg.time;
	if ( dt <= 0.0f ) {
		return;
	}
	// (a little smoothing: tracking jitters)
	vrb.swing.speed += ( speed - vrb.swing.speed ) * Com_Clamp( 0.0f, 1.0f, dt / ( dt + 0.02f ) );

	weight = CG_VRB_Weight( qtrue );
	if ( !vrb.swing.armed ) {
		if ( vrb.swing.speed < 0.5f * vrbWhooshSpeed[weight] ) {
			vrb.swing.armed = qtrue;	// slowed down: the next swing whooshes again
		} else if ( cg.time - vrb.swing.lastTime > 450 && vrb.swing.speed >= 1.3f * vrbWhooshSpeed[weight] ) {
			vrb.swing.armed = qtrue;	// still swinging hard (spinning it)
		}
	}
	if ( vrb.swing.armed && vrb.swing.speed >= vrbWhooshSpeed[weight] && cg.time - vrb.swing.lastTime > 150 ) {
		const clientInfo_t *ci = &cgs.clientinfo[cg.clientNum];
		qhandle_t sfx;
		vrb.swing.armed = qfalse;
		vrb.swing.lastTime = cg.time;
		if ( ci->infoValid && ci->saber[0].swingSound[0] ) {
			sfx = ci->saber[0].swingSound[Q_irand( 0, 2 )];	// the saber's own
		} else {
			sfx = trap->S_RegisterSound( va( "sound/weapons/saber/saberhup%i.wav", 1 + 3 * weight + Q_irand( 0, 2 ) ) );
		}
		trap->S_StartSound( where, cg.clientNum, CHAN_AUTO, sfx );
		if ( trap->ext.VR_Haptic ) {
			trap->ext.VR_Haptic( hand, 0.02f, 0.1f + 0.06f * weight );
		}
		CG_VRB_Log( "swing: %s%s, %.1f m/s (the blade trailed your hand by up to %.0f deg)\n", vrbWeightNames[weight],
			CG_VRB_TwoHanded() ? " (two hands)" : "", vrb.swing.speed, vrb.swing.lag );
		vrb.swing.lag = 0.0f;
	}
}

// How far a finger's last joint (j3, else j2) is from the line through o
// along d.
static float CG_VRB_LineDistance( centity_t *cent, const char *finger, const vec3_t o, const vec3_t d ) {
	vrbBone_t b;
	vec3_t rel;

	if ( !CG_VRB_Bone( cent, va( "%s_j3", finger ), 1, &b ) && !CG_VRB_Bone( cent, va( "%s_j2", finger ), 1, &b ) ) {
		return -1.0f;
	}
	VectorSubtract( b.origin, o, rel );
	VectorMA( rel, -DotProduct( rel, d ), d, rel );
	return VectorLength( rel );
}

// The log: which animation your body is in, and how far the fingers of the
// hand holding the saber are from its hilt (a closed fist hugs it), now and
// then and whenever either changes.
extern stringID_table_t animTable[MAX_ANIMATIONS + 1];

static const char *CG_VRB_AnimName( int anim ) {
	return ( anim >= 0 && anim < MAX_ANIMATIONS && animTable[anim].name ) ? animTable[anim].name : "?";
}

static void CG_VRB_LogHands( centity_t *cent ) {
	const playerState_t *ps = &cg.predictedPlayerState;
	vrbGrip_t grip;
	vrbBone_t tag;
	mdxaBone_t m;
	vec3_t blade;
	float index, middle, left = -1.0f;

	if ( !cg_vrLog.integer || ( cg.time - vrb.logTime >= 0 && cg.time - vrb.logTime < 200 ) ) {
		return;
	}
	vrb.logTime = cg.time;
	CG_VRB_Grip( cent, VR_HAND_RIGHT, &grip );
	if ( grip.kind != VRB_GRIP_SABER ) {
		vrb.logTorso = -1;
		return;
	}
	if ( !CG_VRB_Bone( cent, vrbArmBones[VR_HAND_RIGHT][VRB_TAG], 1, &tag ) ||
		!trap->G2API_GetBoltMatrix( cent->ghoul2, grip.saberModel, 0, &m, cent->turAngles, cent->lerpOrigin, cg.time,
			cgs.gameModels, cent->modelScale ) ) {
		return;
	}
	BG_GiveMeVectorFromMatrix( &m, NEGATIVE_Y, blade );
	if ( VectorNormalize( blade ) < 0.5f ) {
		return;
	}
	if ( vrb.bonesLoggedG2 != cent->ghoul2 ) {
		// which finger bones this model has (once)
		static const char *const fingers[] = { "r_d1", "r_d2", "r_d3", "r_d4", "r_d5" };
		char have[256] = "";
		int i, j;
		vrb.bonesLoggedG2 = cent->ghoul2;
		for ( i = 0; i < 5; i++ ) {
			for ( j = 1; j <= 3; j++ ) {
				if ( trap->G2API_AddBolt( cent->ghoul2, 0, va( "%s_j%i", fingers[i], j ) ) >= 0 ) {
					Q_strcat( have, sizeof( have ), va( " %s_j%i", fingers[i], j ) );
				}
			}
		}
		CG_VRB_Log( "model: right hand bones%s\n", have[0] ? have : " (none found)" );
	}
	index = CG_VRB_LineDistance( cent, "r_d2", tag.origin, blade );
	middle = CG_VRB_LineDistance( cent, "r_d3", tag.origin, blade );
	if ( vrb.twoHand && vrb.twoHandTime == cg.time ) {
		left = CG_VRB_LineDistance( cent, "l_d2", tag.origin, blade );
	}
	if ( ps->torsoAnim != vrb.logTorso || fabsf( index - vrb.logTip ) > 0.8f || cg.time - vrb.logPrinted > 3000 ||
		cg.time - vrb.logPrinted < 0 ) {
		CG_VRB_Log( "hand: torso %s, legs %s, saber move %i, style %i, holstered %i, hand extend %i | right fingertips "
			"%.1f %.1f from the hilt%s%s\n", CG_VRB_AnimName( ps->torsoAnim ), CG_VRB_AnimName( ps->legsAnim ),
			ps->saberMove, ps->fd.saberAnimLevel, ps->saberHolstered, ps->forceHandExtend, index, middle,
			left >= 0.0f ? va( " | two hands, left fingertip %.1f", left ) : "", ps->weaponTime > 0 ? " | busy" : "" );
		CG_VRB_Log( "fingers bent round the hilt (thumb, index, rest; degrees, closing +): right %+.0f %+.0f %+.0f (fist %.1f off its axis), "
			"left %+.0f %+.0f %+.0f (%.1f off) | saber moved into the fist: right %s, left %s\n",
			vrb.curlLog[VR_HAND_RIGHT][0], vrb.curlLog[VR_HAND_RIGHT][1], vrb.curlLog[VR_HAND_RIGHT][2], vrb.curlOff[VR_HAND_RIGHT],
			vrb.curlLog[VR_HAND_LEFT][0], vrb.curlLog[VR_HAND_LEFT][1], vrb.curlLog[VR_HAND_LEFT][2], vrb.curlOff[VR_HAND_LEFT],
			vrb.tagMoves[VR_HAND_RIGHT] ? "yes" : "no (its tag doesn't hang on the tag bone)",
			vrb.tagMoves[VR_HAND_LEFT] ? "yes" : "no" );
		vrb.logTorso = ps->torsoAnim;
		vrb.logTip = index;
		vrb.logPrinted = cg.time;
	}
}

// Your body as everyone else is to see it (offsets from the camera, like the
// saber pose): which way it faces, where you look, and your hands as the arm
// solver held them this frame (a hand it didn't: the animation has it).
static void CG_VRB_FillAvatar( const vrClientState_t *vr, const vec3_t shift, vrAvatar_t *a ) {
	vec3_t head[3], world[3];
	int h, i;

	memset( a, 0, sizeof( *a ) );
	a->valid = qtrue;
	a->bodyYaw = vrb.worldYaw;
	for ( i = 0; i < 3; i++ ) {
		VectorCopy( vr->headAxis[i], head[i] );
	}
	VR_ToWorld( vr, cg.refdef.vieworg, cg.refdef.viewaxis, NULL, head, NULL, world );
	VR_AxisToAngles( world, a->headAngles );
	VectorCopy( shift, a->shift );
	for ( h = 0; h < 2; h++ ) {
		if ( vrb.sentTime[h] == cg.time && vrb.armTime[h] == cg.time ) {
			a->hand[h] = vrb.sent[h];
			VectorSubtract( a->hand[h].origin, cg.refdef.vieworg, a->hand[h].origin );
		}
	}
}

// After the frame's scene is built: the entities' real positions back (next
// frame's events and sounds use them), and the blades in your hands and your
// body to the engine.
void CG_VR_BodyEndFrame( void ) {
	const vrClientState_t *vr = CG_VR();
	vec3_t shift;
	int i;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		if ( vrbOthers[i].shiftedEnt ) {
			VectorSubtract( vrbOthers[i].shiftedEnt->lerpOrigin, vrbOthers[i].shift, vrbOthers[i].shiftedEnt->lerpOrigin );
			vrbOthers[i].shiftedEnt = NULL;
			VectorClear( vrbOthers[i].shift );
		}
	}

	VectorCopy( vrb.shift, shift );	// (where your body stood this frame: for the others)
	if ( vrb.shiftedEnt ) {
		VectorSubtract( vrb.shiftedEnt->lerpOrigin, vrb.shift, vrb.shiftedEnt->lerpOrigin );
		vrb.shiftedEnt = NULL;
		VectorClear( vrb.shift );
	}

	if ( CG_VR_Engine() && trap->ext.VR_SetUserPose ) {
		const qboolean saber = ( vr && CG_VRB_SaberHands() ) ? qtrue : qfalse;
		const qboolean body = ( vr && vrb.bodyCent && vrb.bodyTime == cg.time && CG_VRB_BodyUsable( vrb.bodyCent ) ) ? qtrue : qfalse;
		if ( saber || body ) {
			vrUserPose_t *p = &vrbSaber.pose;
			vec3_t eye;
			int s;

			if ( vrbSaber.time != cg.time || !saber ) {
				// no blade in your hands drawn this frame (a flip, a lost hand)
				memset( p, 0, sizeof( *p ) );
			}
			p->valid = saber;
			p->trackYaw = AngleNormalize180( VR_AxisYaw( cg.refdef.viewaxis ) - vr->referenceYaw );

			// the camera you see your hands from is the eye point of the
			// player state this frame is drawn for, plus its smoothing
			VectorCopy( cg.predictedPlayerState.origin, eye );
			eye[2] += cg.predictedPlayerState.viewheight;
			VectorSubtract( cg.refdef.vieworg, eye, p->viewOffset );

			// the hands holding the sabers (saber 1 right, saber 2 left), and
			// the left hand when it holds the first one too
			for ( s = 0; saber && s < VR_POSE_MAX_SABERS; s++ ) {
				const int h = ( s == 1 ) ? VR_HAND_LEFT : VR_HAND_RIGHT;
				p->twoHanded[s] = qfalse;
				if ( p->bladeMask[s] ) {
					VectorSubtract( vrb.armGrip[h], cg.refdef.vieworg, p->grip[s] );
					if ( s == 0 && vrb.twoHand && vrb.twoHandTime == cg.time ) {
						p->twoHanded[s] = qtrue;
						VectorSubtract( vrb.twoHandGrip2, cg.refdef.vieworg, p->grip2[s] );
					}
				}
			}

			// and what everyone else sees of you
			if ( body ) {
				CG_VRB_FillAvatar( vr, shift, &p->avatar );
			} else {
				memset( &p->avatar, 0, sizeof( p->avatar ) );
			}
			trap->ext.VR_SetUserPose( p );
		} else {
			vrbSaber.pose.valid = qfalse;
			trap->ext.VR_SetUserPose( NULL );
		}
	}

	CG_VRB_SwingSound( vr );

	// where a squeezed grip takes hold of something instead of using the Force
	if ( CG_VR_Engine() && trap->ext.VR_SetGrabZones ) {
		trap->ext.VR_SetGrabZones( ( vr && vrb.twoHandTime == cg.time ) ? vrb.grabZones : 0 );
	}
}

/*
================================================================================

Physical lightsaber

================================================================================
*/

// A blade drawn in your VR body's hand (CG_AddSaberBlade): where it is goes
// to the server, which fights with it (when it knows how, g_vrSaber).
void CG_VR_SaberBlade( centity_t *cent, int saberNum, int bladeNum, const vec3_t base, const vec3_t dir, float length ) {
	vrBladePose_t *bp;

	if ( cent != vrb.bodyCent || vrb.bodyTime != cg.time || length <= 0.0f ) {
		return;	// not your VR body (or not drawn as one this frame)
	}
	if ( saberNum < 0 || saberNum >= VR_POSE_MAX_SABERS || bladeNum < 0 || bladeNum >= VR_POSE_MAX_BLADES ) {
		return;
	}
	if ( vrb.armTime[saberNum == 1 ? VR_HAND_LEFT : VR_HAND_RIGHT] != cg.time ) {
		return;	// that hand isn't on its controller this frame (the animation holds the saber)
	}
	if ( vrbSaber.time != cg.time ) {
		memset( &vrbSaber.pose, 0, sizeof( vrbSaber.pose ) );
		vrbSaber.time = cg.time;
	}

	// relative to the camera you see it from
	bp = &vrbSaber.pose.blade[saberNum][bladeNum];
	VectorSubtract( base, cg.refdef.vieworg, bp->base );
	VectorCopy( dir, bp->dir );
	if ( !( VectorNormalize( bp->dir ) > 0.5f ) ) {
		return;
	}
	vrbSaber.pose.bladeMask[saberNum] |= 1 << bladeNum;

	VectorCopy( base, vrbSaber.base[saberNum][bladeNum] );
	VectorMA( base, length, bp->dir, vrbSaber.tip[saberNum][bladeNum] );
}

// Whether the left hand holds the right hand's saber too (lately).
static qboolean CG_VRB_TwoHanded( void ) {
	return (qboolean)( vrb.twoHand && cg.time - vrb.twoHandTime >= 0 && cg.time - vrb.twoHandTime < 300 );
}

// The hand holding the blade closest to point, if one is within dist.
static int CG_VRB_SaberHandNear( const vec3_t point, float dist ) {
	int s, b, hand = -1;
	float best = dist;

	if ( vrbSaber.pose.valid && cg.time - vrbSaber.time >= 0 && cg.time - vrbSaber.time < 300 ) {
		for ( s = 0; s < VR_POSE_MAX_SABERS; s++ ) {
			for ( b = 0; b < VR_POSE_MAX_BLADES; b++ ) {
				vec3_t seg, rel, closest;
				float len, f, d;
				if ( !( vrbSaber.pose.bladeMask[s] & ( 1 << b ) ) ) {
					continue;
				}
				VectorSubtract( vrbSaber.tip[s][b], vrbSaber.base[s][b], seg );
				VectorSubtract( point, vrbSaber.base[s][b], rel );
				len = DotProduct( seg, seg );
				f = ( len > 0.0f ) ? Com_Clamp( 0.0f, 1.0f, DotProduct( rel, seg ) / len ) : 0.0f;
				VectorMA( vrbSaber.base[s][b], f, seg, closest );
				d = Distance( point, closest );
				if ( d < best ) {
					best = d;
					hand = ( s == 1 ) ? VR_HAND_LEFT : VR_HAND_RIGHT;
				}
			}
		}
	}
	return hand;
}

// How hard a saber hit feels, from its event (WP_SaberDoHit: 1 = 20 damage
// or more, 2 = 5 or more, 3 = less, like a burn; 0 = not flesh).
static float CG_VRB_HitStrength( int eventParm ) {
	switch ( eventParm ) {
	case 1:		return 1.0f;
	case 2:		return 0.7f;
	case 3:		return 0.35f;
	default:	return 0.5f;
	}
}

// Feel what your saber does: cutting, clashing, being cut.
void CG_VR_SaberEventFeel( int event, const entityState_t *es ) {
	if ( !CG_VR() || !trap->ext.VR_Haptic ) {
		return;
	}
	if ( event == EV_SABER_HIT ) {
		if ( es->otherEntityNum2 == cg.clientNum && es->otherEntityNum != ENTITYNUM_NONE ) {
			// your blade cut something: a sharp tick for a light blade, a
			// thump for a heavy one, and it drags through
			static const float len[3] = { 0.035f, 0.055f, 0.085f }, amp[3] = { 0.65f, 0.8f, 1.0f };
			const int weight = CG_VRB_Weight( qtrue );
			const float strength = CG_VRB_HitStrength( es->eventParm );
			const float a = amp[weight] * strength;
			const int hand = ( es->weapon == 1 ) ? VR_HAND_LEFT : VR_HAND_RIGHT;
			trap->ext.VR_Haptic( hand, len[weight], a );
			if ( es->weapon == 0 && CG_VRB_TwoHanded() ) {
				trap->ext.VR_Haptic( VR_HAND_LEFT, len[weight], 0.7f * a );	// (both hands on it)
			}
			if ( es->eventParm ) {
				CG_VRB_BladeImpulse( hand, qfalse, strength );
			}
			CG_VRB_Log( "event: your %s blade cut %i (hit strength %i)\n", vrbWeightNames[weight], es->otherEntityNum,
				es->eventParm );
		} else if ( es->otherEntityNum == cg.clientNum ) {
			// you were cut
			const float a = 0.6f * CG_VRB_HitStrength( es->eventParm );
			trap->ext.VR_Haptic( VR_HAND_LEFT, 0.12f, a );
			trap->ext.VR_Haptic( VR_HAND_RIGHT, 0.12f, a );
		}
	} else if ( event == EV_SABER_BLOCK && es->eventParm ) {
		// sabers clashed: hard in the hand whose blade it was. A clash of your
		// blade names you and the saber (it arrives a moment after the hit,
		// so the blade may have moved on a little; other events that don't
		// name anyone say 0, so it has to be near your blade too). Someone
		// else's saber hitting yours is placed where it met your saber's
		// box, close to the blade.
		int hand;
		if ( es->otherEntityNum2 == cg.clientNum && CG_VRB_SaberHandNear( es->origin, 64.0f ) >= 0 ) {
			hand = ( es->weapon == 1 ) ? VR_HAND_LEFT : VR_HAND_RIGHT;
		} else {
			hand = CG_VRB_SaberHandNear( es->origin, 32.0f );
		}
		if ( hand >= 0 ) {
			// a hard knock, longer the heavier the blade; the blade stops
			static const float len[3] = { 0.06f, 0.09f, 0.13f };
			const int weight = CG_VRB_Weight( qtrue );
			trap->ext.VR_Haptic( hand, len[weight], 1.0f );
			if ( hand == VR_HAND_RIGHT && CG_VRB_TwoHanded() ) {
				trap->ext.VR_Haptic( VR_HAND_LEFT, len[weight], 0.8f );	// (both hands on it)
			}
			CG_VRB_BladeImpulse( hand, qtrue, 1.0f );
			CG_VRB_Log( "event: clash on your %s blade (%s)\n", vrbWeightNames[weight],
				es->otherEntityNum2 == cg.clientNum ? "your swing" : "their blade" );
		}
	}
}

// Your blade is in a wall (CG_AddSaberBlade's sparks): a grinding buzz.
void CG_VR_SaberWallFeel( centity_t *cent, int saberNum ) {
	if ( cent != vrb.bodyCent || vrb.bodyTime != cg.time || !CG_VR() || !trap->ext.VR_Haptic ) {
		return;
	}
	if ( cg.time - vrbSaber.wallFeelTime >= 0 && cg.time - vrbSaber.wallFeelTime < 60 ) {
		return;
	}
	vrbSaber.wallFeelTime = cg.time;
	trap->ext.VR_Haptic( saberNum == 1 ? VR_HAND_LEFT : VR_HAND_RIGHT, 0.03f, 0.35f );
	if ( saberNum == 0 && CG_VRB_TwoHanded() ) {
		trap->ext.VR_Haptic( VR_HAND_LEFT, 0.03f, 0.25f );
	}
}

// Undoes CG_VR_BodyPose on a model (third person, death, leaving VR...).
// Only touches what was changed on this very instance; cheap otherwise.
static void CG_VRB_Restore( centity_t *cent );

void CG_VR_BodyRestore( centity_t *cent ) {
	const int n = cent->currentState.number;

	if ( n != cg.clientNum ) {
		// another player's (their VR body, if they had one)
		if ( n >= 0 && n < MAX_CLIENTS ) {
			vrbCur = &vrbOthers[n];
			CG_VRB_Restore( cent );
			vrbCur = &vrbSelf;
		}
		return;
	}
	CG_VRB_Restore( cent );
}

static void CG_VRB_Restore( centity_t *cent ) {
	CG_VRB_Init();
	if ( !cent->ghoul2 ) {
		return;
	}
	CG_VRB_Fist( cent, VR_HAND_LEFT, qfalse );
	CG_VRB_Fist( cent, VR_HAND_RIGHT, qfalse );
	if ( vrb.armsG2 == cent->ghoul2 ) {
		CG_VRB_ResetArms( cent->ghoul2 );
		vrb.armsG2 = NULL;
		vrb.poleValid[0] = vrb.poleValid[1] = qfalse;
	}
	if ( vrb.wristG2 == cent->ghoul2 ) {
		CG_VRB_SetHandBones( cent->ghoul2, VR_HAND_LEFT, NULL );
		CG_VRB_SetHandBones( cent->ghoul2, VR_HAND_RIGHT, NULL );
		vrb.wristG2 = NULL;
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
