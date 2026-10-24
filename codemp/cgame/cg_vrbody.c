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
along your fist, like a real hilt (or, passed into it from the other hand,
the way it lay in that hand as it was passed).

A saber goes in either hand: your first one in the hand you use (the right;
the left with vr_leftHanded on - and your guns with it), a second one in the
other. Both hands on one saber: squeezing the other hand's grip at the hilt
takes hold of it, anywhere from the pommel to the emitter (the engine then
doesn't use the Force or Use with that press, see VR_SetGrabZones). That
hand closes around the hilt and stays where it took hold; the blade follows
both wrists and, the further apart the hands, the line through them, so
pushing with one hand and pulling with the other levers it round. Letting go
of the grip, or pulling the hand well away, lets go. Held with both, the hand
you use leads, whichever took it first: in your other hand, it goes over to
the hand you use as both settle on the hilt, nothing jumping (lent, if it's
that hand's without its grip: let go with the hand you use, it's back in it).
Squeezing the grip of the hand that has it passes it over (holding the grip,
letting go of it does): that hand lets go, and the other one goes on
holding it - where it held it, and the way round it held it (the
blade below the fist, if it took hold of it so: the reverse grip; take it
back the other way round to turn it back) - just as it held it: the blade
pointing, and the hilt turned about itself, as they were, it moves with
that hand from then on as if gripped there (not swung round onto that
fist's line, nor turned over to the way that hand's tag would hang it).
Only the fist, the saber in it, settles from where it held the hilt onto
its controller over a moment.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "cg_local.h"
#include "ghoul2/G2.h"
#include "game/bg_vrsaber.h"
#include "qcommon/vr_fingers.h"

#define VRB_TURN_OFF		0x00000100	// G2SURFACEFLAG_NODESCENDANTS: surface and its children off
#define VRB_HEAD_BIT		( 1 << ( G2_MODELPART_HEAD - 10 ) )
#define VRB_POSE_EYE_MAX	20.0f	// (units) the camera goes at most this far to a posed body's eyes

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

// The collarbone (from the top of the breastbone; the upper arm doesn't hang
// off it in this skeleton, so it is moved along with it), and the forearm's
// twist bone, halfway down it (the game's animations give it half the hand's
// twist).
static const char *const vrbClavicle[2] = { "lclavical", "rclavical" };
static const char *const vrbForearmTwist[2] = { "lradiusX", "rradiusX" };

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
	int			saberModel;			// SABER: its Ghoul2 model index on the player...
	int			saber;				// ...which of the player's sabers it is (0, 1)...
	float		sign;				// ...held which way round (1: the blade out of the thumb's side of the fist; -1: below it)...
	float		hilt;				// ...and where along its hilt the fist is (from where its own tag holds it, toward its first blade)
} vrbGrip_t;

// both hands on one saber
#define VRB_GRAB_REACH		5.0f	// a hand this close to the other hand's hilt can take hold of it
#define VRB_GRAB_BREAK		24.0f	// ...and lets go only when pulled this far from its place on it
#define VRB_HAND_GAP		3.5f	// two hands on a hilt are at least a hand's width apart...
#define VRB_HAND_SPAN		60.0f	// ...and no more than this
#define VRB_HILT_BELOW		6.5f	// a single saber's hilt is held this far below where its tag holds it at most (the pommel)
#define VRB_HILT_TOP		1.5f	// ...and a hand's half width short of an emitter (the blade can't be taken hold of)
#define VRB_TWO_HAND_IN		0.08f	// seconds the hands take to settle onto the hilt together...
#define VRB_TWO_HAND_GAP	500		// ms not worked out (your body not drawn): a hold doesn't carry on from then
#define VRB_TWO_HAND_OUT	0.15f	// ...and to come apart again
#define VRB_PASS_MS			150		// ms the fist a saber is passed into takes to settle onto its controller
#define VRB_LET_GO_MS		100		// lent to the hand you use, its grip let go this long gives it back (a moment's slack doesn't)
#define VRB_FACE_MS			250		// a saber drawn this lately goes over to the other hand turned as it was drawn

typedef struct {
	qboolean	solved;
	qboolean	exact;				// well-bent arm in reach: the check below is meaningful
	vec3_t		elbow, wrist;		// where the solution puts them
	qboolean	tagTarget;			// the held-item tag was sent to an exact spot:
	vec3_t		tagOrigin;
	vec3_t		tagAxis[3];
	float		reachExcess;		// wanted shoulder->wrist distance minus the arm's reach
	vec3_t		reachDir;			// shoulder -> wrist
	float		wristFlex, wristRadial, pronation;	// (the log: your real hand against the forearm drawn, degrees)
	float		elbowUp;			// (the log: elbow above the shoulder, per upper arm length)
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
	void		*holsteredG2[MAX_SABERS];	// that saber's copy on the body's hand hidden: it's in its holster (cg_vrholster.c)

	// where the entity was moved this frame (undone in CG_VR_BodyEndFrame)
	centity_t	*shiftedEnt;
	vec3_t		shift;

	// leaning in: how far the body is brought forward (toward your eyes) so
	// its arms reach where yours do
	float		lean;
	float		leanTarget;
	int			leanTime;
	int			leanSetTime;

	// in a pose (bg_vrpose.c): how far the body's own eyes are above the
	// eye point (the pose's viewheight is one for every body), eased - the
	// camera goes there (CG_VR_PoseEyeAdjust)
	float		poseEye;
	float		poseEyeTarget;
	int			poseEyeTime;

	// which bones turn with the hand at the wrist (CG_VRB_ProbeHands)
	qboolean	probed;
	int			probeRetry;			// cg.time to try again after a failed probe
	void		*probeG2;
	char		probeModel[MAX_QPATH];
	qboolean	wristOk[2];
	qboolean	handBoneTurn[2][VRB_MAX_HAND_BONES];
	qboolean	fingerUnderJ1[2][3];	// a finger's second joint hangs off its first (VRB_FINGERS)
	void		*curlG2;				// fingers curled round a hilt (posed, CG_VRB_PoseFingers) on this instance (CG_VRB_CurlFingers)
	float		curlLog[2][3];			// (the log: how far each finger closed, degrees...
	float		curlOff[2];				// ...and how far the hand was from the hilt's axis; -1: not holding one)
	qboolean	tagMoves[2];			// the held-item tag hangs on the tag bone: a saber can be moved in the hand (CG_VRB_PlaceSaber)
	int			curlGeoTime[2];			// (...next time to log the hand round the hilt in full)

	// per arm
	vec3_t		pole[2];			// smoothed elbow direction, chest-local
	qboolean	poleValid[2];
	int			poleTime[2];		// cg.time it was last advanced
	vec3_t		elbowLocal[2];		// where the elbow was then, from the shoulder (chest-local)
	vec3_t		hinge[2];			// elbow hinge axis in the upper arm's frame
	qboolean	hingeValid[2];
	int			hingeConvention[2];
	float		girdle[2][2];		// the shoulder girdle: lifted, brought forward (radians, eased)
	qboolean	girdleValid[2];
	int			girdleTime[2];
	void		*extraG2;			// the shoulders / forearm twist bones were moved on this instance
	int			armLogTime;			// (the log: the arms, now and then)

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

	// the sabers in your hands (CG_VRB_Holds, once per frame; for another
	// player, their avatar says)
	struct {
		int		hand;				// VR_HAND_*
		float	sign;				// 1: the blade out of the thumb's side of the fist; -1: below it (reversed)
		float	t;					// where the fist is along the hilt: from where the saber's own tag holds it, toward its first blade
		int		passTime;			// cg.time it was passed into this hand (0: it wasn't lately)...
		vec3_t	passOfs;			// ...and where the fist held it then, from the controller (in its frame)
		// Passed into this hand, a saber lies in it the way it lay as it was
		// passed - not swung round onto the fist's line, nor turned about
		// itself to the way this hand's tag would hang it.
		qboolean	kept;			// its blade this way (yours: another player's avatar says)...
		vec3_t	keptDir;
		qboolean	keptFace;		// ...and its face this way (another player's too), as it was with
		vec3_t	keptFaceDir;		// its blade that way (wherever the blade's drawn, the face is turned
		vec3_t	keptFaceRef;		// with it from there, CG_VRB_KeptFace)...
		int		keptHand;			// ...in this hand (all in its controller's frame)...
		qboolean	keptInFist;		// ...and once it's been placed in that fist, its face in the fist's own frame
		vec3_t	keptFist;			// (VRB_HandFrame): it turns with the fist, never in it
		qboolean	keptGrip;		// the hand lies on the controller as it lay on the hilt as it took the saber over
		vec3_t	keptGripAxis[3];	// (in the controller's frame), not as the controller lies: it doesn't turn round the hilt
	}			hold[MAX_SABERS];
	// each saber as last drawn in a hand (CG_VRB_PlaceSaber): where its blade
	// pointed, and which way its face was turned (a line square to its blade
	// that turns with the hilt), in that hand's controller's frame
	vec3_t		drawnBlade[MAX_SABERS];
	vec3_t		drawnFace[MAX_SABERS];
	int			drawnTime[MAX_SABERS];
	int			drawnHand[MAX_SABERS];
	int			holdTime;			// cg.time they were last worked out for
	int			holdKey;			// what they were set up for (handedness, the sabers): when that changes, afresh
	qboolean	holdHolsters;		// ...and with holsters in play (cg_vrholster.c): when they come into play, afresh
	qboolean	grabBlocked[2];		// that hand let go of the hilt it held (passed it, or was pulled off it): it
									// doesn't take hold again until its grip is let go
	struct {
		int		time;				// cg.time that hand passed on the saber it held (CG_VRB_PassSaber)...
		vec3_t	axis[3];			// ...lying on the controller so (as it lay on the hilt, keptGrip; else as it lies)...
		vrbMat_t	turn;			// ...and its wrist turned so to hold it (both in its controller's frame): it eases
	}			letGo[2];			// off that onto its controller, open (VRB_PASS_MS)
	vrbMat_t	heldTurn[2];		// how the hand's wrist is turned to hold its saber (from lying on its controller;
	int			heldTurnTime[2];	// in the controller's frame), as of this cg.time (CG_VRB_SolveArm)
	qboolean	grabWas[2];			// its grip was held as a grab last frame
	void		*boltG2;			// the sabers (a gun) hang on the hands holding them on this instance (CG_VRB_BoltHands)

	// both hands on the saber (CG_VRB_TwoHand, once per frame)
	int			twoHandTime;		// cg.time it was worked out for
	qboolean	twoHand;			// the other hand holds the first saber too
	int			twoHandOff;			// that hand (VR_HAND_*; another player's: -1 while neither does)
	float		twoHandSign;		// -1: it holds it the other way round to the blade (this hold)
	float		twoHandSide;		// +1: it holds it above the first hand (toward the blade), -1 below
	float		twoHandT;			// where along the hilt it holds it (from the first hand, toward the blade)
	vec3_t		twoHandGrip[3];		// how it lies round the hilt: its controller's axes in the hilt's frame
	float		twoHandBlend;		// 0..1: how far the hold has taken the hands over (eases in and out)
	qboolean	twoHandPass;		// the first hand's grip squeezed to pass it over: it goes once the hands have settled
									// (a new hold starts with none)
	qboolean	twoHandLent;		// the hand you use took the lead (CG_VRB_TakeLead) of a saber that's the other hand's,
									// not held by its grip: that one holds the hilt without, and has it back as the hand
									// you use lets go...
	int			twoHandSlack;		// ...its grip let go since (cg.time; 0: held) - not long enough yet (VRB_LET_GO_MS)
	int			twoHandHeldTime;	// cg.time both hands last held it (lent, a hand's tracking lost: a moment, it goes on)
	qboolean	twoHandOffKept;		// the other hand points the blade as it held it, not along its fist (it held it first,
	vec3_t		twoHandOffDir;		// the hand you use leading now: CG_VRB_TakeLead) - this way, in its controller's frame
	float		twoHandHilt[2];		// where on the hilt hands can hold it, from where its tag holds it (as found this frame)
	qboolean	twoHandArms;		// the arms are to show it this frame (twoHandBlend > 0)
	float		twoHandEase;		// ...this much
	vec3_t		twoHandDir;			// where the blade points, from both hands
	vec3_t		twoHandPoint;		// where the other hand holds the hilt
	vec3_t		twoHandRel[2];		// both, in the first hand's controller frame (to carry them while letting go)
	vec3_t		twoHandHand;		// the other hand's place, the controller eased onto the hilt
	vec3_t		twoHandAxis[3];		// the other hand's frame (a controller gripping the hilt)
	vec3_t		twoHandBladeLocal;	// the blade in the frame of the hand tag it is bolted to (raw axes)
	vec3_t		twoHandGrip2;		// the other hand's controller grip (world)
	int			grabZones;			// hands that could take hold of something now (engine)
	void		*fistG2[2];			// that hand's fingers are closed round a hilt on this instance
	void		*bonesLoggedG2;		// the log has said which finger bones this model has
	vec3_t		twoHandFist;		// the way the hilt runs through the other fist, as drawn

	// the blade's weight (CG_VRB_BladeInertia, once per frame)
	int			bladeTime;
	struct {
		qboolean	valid;
		int			time;
		vec3_t		dir;			// the blade as drawn, in your room
		vec3_t		vel;			// how it turns (rad/s, axis * rate)
		vec3_t		hand;			// where the hand pointed it last frame, in your room
		float		behind;			// how far it trails the hand now (0..1 of as far as it can)
		int			holdUntil;		// stopped on another blade until then
		vec3_t		out;			// the blade as drawn this frame (world)
		int			outTime;
		// solid blades (CG_VRB_BladeSolid)
		vec3_t		pivot;			// last frame: the hold on the hilt it turned about (world: the controller's grip, or the
									// fist settling onto it after a pass)...
		vec3_t		last;			// ...where the blade pointed (world, as it was let)...
		float		lastYaw;		// ...and the room's turn (CG_VRB_RoomYaw)
		int			lastTime;		// (cg.time; 0: none)
		int			heldTime;		// cg.time it last rested on another blade...
		int			heldEnt;		// ...whose...
		vec3_t		heldPoint;		// ...where they touch (on the other one, world)...
		vec3_t		heldNormal;		// ...and the way from there to yours
		float		pressLag;		// degrees it trailed your hand by as it met it
		float		press;			// 0..1: how hard it's pressed on, toward slipping by (felt)
		int			slipUntil;		// it slipped by: no blade stops it until then
		int			brokeTime;		// (the log: a heavy blow went through one then)
	}			blade[2];

	// the whoosh of your swings (CG_VRB_SwingSound)
	struct {
		vec3_t		prev[VR_POSE_MAX_SABERS][VR_POSE_MAX_BLADES];	// room: a point two thirds along each blade
		int			prevMask[VR_POSE_MAX_SABERS];
		int			prevTime;
		float		speed;			// m/s, smoothed
		float		handSpeed[2];	// m/s, smoothed: the fastest blade in each hand (CG_VRB_Heft)
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

	// your fingers (cg_vrFingers, CG_VRB_EaseFingers): how far each is
	// closed (VR_FINGER_*), eased toward what your controllers sense
	float		fingers[2][VR_NUM_FINGERS];
	qboolean	fingersEased[2];
	int			fingerTime;			// cg.time they were eased to
	int			fingerSensed[2];	// (the log: what each controller senses of them, as last said, + 1; 0: not yet)

	// the body bends (cg_vrBodyBend, CG_VRB_Bend): how far (0: upright, as
	// the game's animation has it; 1: standing still, bent the way you are),
	// eased - standing in the upright stance on the ground (CG_VR_BodyAnim)
	float		bend;
	int			bendTime;			// cg.time it was eased to
	qboolean	standing;			// the legs stand in the upright stance (as of standTime)
	int			standTime;
	vec3_t		bendEyes;			// where the body's eyes go this frame (world): yours, your headset with the neck
	int			bendEyesTime;		// just behind (CG_VR_BodyPose); another player's, where their game put them (cg.time)
	float		chestTwist;			// the chest turned off the hips (degrees, + to the left), eased
	int			chestTime;
	float		footDelta[2];		// how much higher than the floor the ground under each foot is (units, eased)
	int			footTime;
	void		*bendG2;			// the hips, spine, neck, head and legs are moved on this instance
	mdxaBone_t	bendMoves[12];		// ...so, this frame (VRB_NUM_BEND_BONES: the other eye's the same)
	int			bendMovesTime;		// (cg.time)
	int			bendOkTime;			// the body last bent (cg.time): it has the bones for it, the engine the transforms
	float		bendA, bendB;		// the trunk's lean then (forward, to the left: radians; the next frame's starts from it)
	float		lookIn;				// how far the bend has the head, the game the rest (0..1, eased: CG_VRB_LookIn)
	int			lookInTime;
	// (the log, the tests) what the last bend did
	float		bendLean, bendSide;	// the trunk leaned forward, to the left (degrees)
	float		bendTwist;			// the chest turned (degrees, + to the left)
	float		bendDrop;			// the hips went down (units)
	vec3_t		bendNeck;			// where the trunk sent the neck (world; the body moved whole on top: CG_VRB_Bend)
	int			bendLogTime;
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
} vrbSaber;

// what your hands feel (below)
static void CG_VRB_Feel( int hand, float peak, int hold, float tail, int decay, float grain );
static void CG_VRB_FeelBlade( int hand, float peak, int hold, float tail, int decay, float grain );
static void CG_VRB_NoteOtherBlade( centity_t *cent, int saberNum, int bladeNum, const vec3_t base, const vec3_t dir, float length );
static void CG_VRB_BladeSolid( int h, const vrClientState_t *vr, const vec3_t pivot, const vec3_t want, vec3_t out );
static void CG_VRB_TouchFeel( void );
static void CG_VRB_Heft( void );
static void CG_VRB_FeelFrame( void );

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

// (the log) Every 10 s in a game: how smoothly the server's snapshots
// arrive, and how far behind the newest one everything is drawn - other
// players move between two snapshots, and when the next one is late they
// stop where they were last seen (which reads as jitter).
static struct {
	int		start;				// cg.time the window began (0: not yet)
	int		frames, noNext;		// frames drawn; ...with no next snapshot to move toward
	int		lastNum, snaps;		// newest snapshot number seen; how many arrived
	int		lastArrive, worstGap;	// real time (ms) the last one arrived; the longest wait
	int		pingSum, behindSum;
} vrbNet;

void CG_VR_NetStats( void ) {
	int num, time, now;

	if ( !cg_vrLog.integer || !cg.snap || cg.demoPlayback ) {
		vrbNet.start = 0;
		return;
	}
	trap->GetCurrentSnapshotNumber( &num, &time );
	now = trap->Milliseconds();
	if ( !vrbNet.start || cg.time < vrbNet.start ) {
		memset( &vrbNet, 0, sizeof( vrbNet ) );
		vrbNet.start = cg.time;
		vrbNet.lastNum = num;
		vrbNet.lastArrive = now;
		return;
	}
	if ( num != vrbNet.lastNum ) {
		vrbNet.snaps += num - vrbNet.lastNum;
		vrbNet.lastNum = num;
		vrbNet.worstGap = Q_max( vrbNet.worstGap, now - vrbNet.lastArrive );
		vrbNet.lastArrive = now;
	}
	vrbNet.frames++;
	if ( !cg.nextSnap ) {
		vrbNet.noNext++;
	}
	vrbNet.pingSum += cg.snap->ping;
	vrbNet.behindSum += time - cg.time;
	if ( cg.time - vrbNet.start >= 10000 && vrbNet.frames ) {
		const float secs = ( cg.time - vrbNet.start ) * 0.001f;
		CG_VRB_Log( "net: %.1f snapshots/s (longest wait %i ms), ping %i ms; drawn %i ms behind the newest; "
			"%.1f%% of %i frames had no newer snapshot to move toward\n", vrbNet.snaps / secs, vrbNet.worstGap,
			vrbNet.pingSum / vrbNet.frames, vrbNet.behindSum / vrbNet.frames, 100.0f * vrbNet.noNext / vrbNet.frames,
			vrbNet.frames );
		vrbNet.start = cg.time;
		vrbNet.frames = vrbNet.noNext = vrbNet.snaps = vrbNet.worstGap = vrbNet.pingSum = vrbNet.behindSum = 0;
	}
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

// How heavy your blade feels in your hand (bg_vrsaber.h): blue, yellow,
// red, the staff or two sabers - and, if withHands, one heavier held with
// both hands (blue as yellow; yellow and the staff as red).
static const char *const vrbFeelNames[VR_NUM_FEELS] = { "blue", "yellow", "red", "staff", "two-saber" };

static int CG_VRB_BladeFeel( qboolean withHands ) {
	const int feel = BG_VRFeel( cg.predictedPlayerState.fd.saberAnimLevel );

	if ( withHands && CG_VRB_TwoHanded() ) {
		switch ( feel ) {
		case VR_FEEL_BLUE:
			return VR_FEEL_YELLOW;
		case VR_FEEL_YELLOW:
		case VR_FEEL_STAFF:
		case VR_FEEL_DUAL:
			return VR_FEEL_RED;
		default:
			break;
		}
	}
	return feel;
}

// The share of its weight's swing speeds your blade needs to cut, as the
// server reckons it: a heavier-feeling blade can't move as fast, so less
// (bg_vrsaber.h).
static float CG_VRB_CutScale( void ) {
	return BG_VRFeelCutScale( BG_VRFeel( cg.predictedPlayerState.fd.saberAnimLevel ) );
}

static void CG_VRB_ResetHolds( int main );
static void CG_VRB_ResetBody( void *g2 );

static void CG_VRB_Init( void ) {
	if ( !vrb.init ) {
		memset( &vrb, 0, sizeof( vrb ) );
		vrb.convention = -1;
		vrb.holdKey = -1;		// (set up afresh the first time, CG_VRB_Holds)
		CG_VRB_ResetHolds( VR_HAND_RIGHT );
		vrb.twoHandOff = VR_HAND_LEFT;
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

// out = a * b^T (b undone, then a)
static void VRB_MulT( vrbMat_t a, vrbMat_t b, vrbMat_t out ) {
	vrbMat_t t;
	int r, c;
	for ( r = 0; r < 3; r++ ) {
		for ( c = 0; c < 3; c++ ) {
			t[r][c] = a[r][0] * b[c][0] + a[r][1] * b[c][1] + a[r][2] * b[c][2];
		}
	}
	memcpy( out, t, sizeof( t ) );
}

static void VRB_Identity( vrbMat_t q ) {
	memset( q, 0, sizeof( vrbMat_t ) );
	q[0][0] = q[1][1] = q[2][2] = 1.0f;
}

// How far R turns about the unit axis k - its twist about k, the rest of it
// a swing off k (radians, -pi..pi).
static float VRB_TwistAngle( vrbMat_t R, const vec3_t k ) {
	const float tr = R[0][0] + R[1][1] + R[2][2];
	float w, v[3], s;

	// R as a quaternion (w, v)
	if ( tr > 0.0f ) {
		s = 2.0f * sqrtf( 1.0f + tr );
		w = 0.25f * s;
		v[0] = ( R[2][1] - R[1][2] ) / s;
		v[1] = ( R[0][2] - R[2][0] ) / s;
		v[2] = ( R[1][0] - R[0][1] ) / s;
	} else if ( R[0][0] > R[1][1] && R[0][0] > R[2][2] ) {
		s = 2.0f * sqrtf( Q_max( 1e-12f, 1.0f + R[0][0] - R[1][1] - R[2][2] ) );
		w = ( R[2][1] - R[1][2] ) / s;
		v[0] = 0.25f * s;
		v[1] = ( R[0][1] + R[1][0] ) / s;
		v[2] = ( R[0][2] + R[2][0] ) / s;
	} else if ( R[1][1] > R[2][2] ) {
		s = 2.0f * sqrtf( Q_max( 1e-12f, 1.0f + R[1][1] - R[0][0] - R[2][2] ) );
		w = ( R[0][2] - R[2][0] ) / s;
		v[0] = ( R[0][1] + R[1][0] ) / s;
		v[1] = 0.25f * s;
		v[2] = ( R[1][2] + R[2][1] ) / s;
	} else {
		s = 2.0f * sqrtf( Q_max( 1e-12f, 1.0f + R[2][2] - R[0][0] - R[1][1] ) );
		w = ( R[1][0] - R[0][1] ) / s;
		v[0] = ( R[0][2] + R[2][0] ) / s;
		v[1] = ( R[1][2] + R[2][1] ) / s;
		v[2] = 0.25f * s;
	}
	if ( w < 0.0f ) {
		w = -w;		// (the same turn, the short way round)
		VectorScale( v, -1.0f, v );
	}
	return 2.0f * atan2f( DotProduct( v, k ), w );
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
		if ( vrb.extraG2 == g2 && trap->ext.VR_G2SetBoneTransform ) {
			trap->ext.VR_G2SetBoneTransform( g2, 0, vrbClavicle[h], &identity );
			trap->ext.VR_G2SetBoneTransform( g2, 0, vrbArmBones[h][VRB_UPPER], &identity );
			trap->ext.VR_G2SetBoneTransform( g2, 0, vrbForearmTwist[h], &identity );
		}
	}
}

// ...and for good: the shoulders and forearm twist bones back to the game's.
static void CG_VRB_ClearExtra( void *g2 ) {
	int h;

	if ( vrb.extraG2 != g2 || !g2 || !trap->ext.VR_G2SetBoneTransform ) {
		return;
	}
	for ( h = 0; h < 2; h++ ) {
		trap->ext.VR_G2SetBoneTransform( g2, 0, vrbClavicle[h], NULL );
		trap->ext.VR_G2SetBoneTransform( g2, 0, vrbArmBones[h][VRB_UPPER], NULL );
		trap->ext.VR_G2SetBoneTransform( g2, 0, vrbForearmTwist[h], NULL );
		vrb.girdleValid[h] = qfalse;
	}
	vrb.extraG2 = NULL;
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

// Rotation by a radians about the unit axis u (Rodrigues).
static void VRB_AxisAngle( const vec3_t u, float a, vrbMat_t R ) {
	const float c = cosf( a ), s = sinf( a ), t = 1.0f - c;

	R[0][0] = c + t * u[0] * u[0];
	R[1][1] = c + t * u[1] * u[1];
	R[2][2] = c + t * u[2] * u[2];
	R[0][1] = t * u[0] * u[1] - s * u[2];
	R[1][0] = t * u[0] * u[1] + s * u[2];
	R[0][2] = t * u[0] * u[2] + s * u[1];
	R[2][0] = t * u[0] * u[2] - s * u[1];
	R[1][2] = t * u[1] * u[2] - s * u[0];
	R[2][1] = t * u[1] * u[2] + s * u[0];
}

// A turn R (in the world) in the frame ax, as it turns ax's own axes...
static void VRB_ToFrameTurn( vec3_t ax[3], vrbMat_t R, vrbMat_t out ) {
	vec3_t t;
	int i, j;

	for ( j = 0; j < 3; j++ ) {
		VRB_Apply( R, ax[j], t );
		for ( i = 0; i < 3; i++ ) {
			out[i][j] = DotProduct( ax[i], t );
		}
	}
}

// ...and back: Rc, given in the frame ax, in the world.
static void VRB_FromFrameTurn( vec3_t ax[3], vrbMat_t Rc, vrbMat_t out ) {
	int r, c, i, j;

	for ( r = 0; r < 3; r++ ) {
		for ( c = 0; c < 3; c++ ) {
			float sum = 0.0f;
			for ( i = 0; i < 3; i++ ) {
				for ( j = 0; j < 3; j++ ) {
					sum += ax[i][r] * Rc[i][j] * ax[j][c];
				}
			}
			out[r][c] = sum;
		}
	}
}

// R turned only f of the way (0: not at all, 1: all of it) - a turn short
// of a half turn.
static void VRB_TurnPart( vrbMat_t R, float f, vrbMat_t out ) {
	const float c = Com_Clamp( -1.0f, 1.0f, ( R[0][0] + R[1][1] + R[2][2] - 1.0f ) * 0.5f );
	vec3_t axis;

	VectorSet( axis, R[2][1] - R[1][2], R[0][2] - R[2][0], R[1][0] - R[0][1] );
	if ( VectorNormalize( axis ) < 1e-6f ) {
		VRB_Identity( out );
		return;
	}
	VRB_AxisAngle( axis, acosf( c ) * f, out );
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

static qboolean CG_VRB_KeptFace( const vrbGrip_t *grip, int h, vec3_t ax[3], vec3_t fist[3], const vec3_t want, vec3_t out );

/*
The saber held in hand h (grip) where the hilt runs through the fist (after
the arm and wrist are posed for it: H, the wrist turn; ctrl, its
controller's axes): its tag bone moved to carry it there from where the
model's tag would - its blade out of the thumb's side of the fist (sign 1)
or the little finger's (-1, reversed), the fist the way along the hilt from
where its own tag holds it that hilt says. Gives that line (o, dir: toward
the thumb).

Turned about itself: its face (its blade bolt's +X, a line square to the
blade that turns with the hilt) the way it lay as it was passed into this
hand (CG_VRB_KeptFace; the pass that's drawn - commit - then keeps it in the
fist) - else as the hand's tag hangs it, turned the least that puts its
blade along the fist; held reversed, that then turned end over end about
the palm's axis (the least turn straight from the tag would be a half turn
about no axis in particular: it would spin as the hand moved). How it ends
up is noted (drawnBlade, drawnFace), for the other hand.
*/
static qboolean CG_VRB_PlaceSaber( centity_t *cent, int h, const mdxaBone_t *H, const vrbGrip_t *grip, vec3_t ctrl[3],
	qboolean commit, vec3_t o, vec3_t dir ) {
	const int s = grip->saber;
	vrbBone_t w, t, i, l, tag;
	mdxaBone_t m, E;
	vec3_t ax[3], blade, want, at, faceNow, faceWant;
	vrbMat_t R;
	float length;
	qboolean haveFace, kept = qfalse;
	int k;

	if ( !CG_VRB_Bone( cent, vrbArmBones[h][VRB_HAND], 1, &w ) || !CG_VRB_Bone( cent, vrbArmBones[h][VRB_THUMB], 1, &t ) ||
		!CG_VRB_Bone( cent, vrbArmBones[h][VRB_INDEX], 1, &i ) || !CG_VRB_Bone( cent, vrbArmBones[h][VRB_LITTLE], 1, &l ) ||
		!CG_VRB_Bone( cent, vrbArmBones[h][VRB_TAG], 1, &tag ) ||
		!VRB_HandFrame( w.origin, t.origin, i.origin, l.origin, ax, &length ) ||
		!trap->G2API_GetBoltMatrix( cent->ghoul2, grip->saberModel, 0, &m, cent->turAngles, cent->lerpOrigin, cg.time,
			cgs.gameModels, cent->modelScale ) ) {
		return qfalse;
	}
	VRB_GripLine( h, w.origin, ax, length, o, dir );
	BG_GiveMeVectorFromMatrix( &m, NEGATIVE_Y, blade );
	BG_GiveMeVectorFromMatrix( &m, POSITIVE_X, faceNow );
	if ( VectorNormalize( blade ) < 0.5f ) {
		return qfalse;
	}
	VectorMA( faceNow, -DotProduct( faceNow, blade ), blade, faceNow );
	haveFace = (qboolean)( VectorNormalize( faceNow ) > 0.5f );
	// the saber's blade along the line, the way round it's held, and its
	// origin (where its tag holds it) on it, as far back from the fist as the
	// fist is along the hilt from there
	VectorScale( dir, ( grip->sign < 0.0f ) ? -1.0f : 1.0f, want );
	VectorMA( o, -grip->hilt, want, at );
	if ( haveFace && CG_VRB_KeptFace( grip, h, ctrl, ax, want, faceWant ) ) {
		vec3_t from[3], to[3];
		kept = qtrue;
		VectorCopy( blade, from[0] );
		VectorCopy( faceNow, from[1] );
		CrossProduct( blade, faceNow, from[2] );
		VectorCopy( want, to[0] );
		VectorCopy( faceWant, to[1] );
		CrossProduct( want, faceWant, to[2] );
		VRB_FrameMap( from, to, R );
	} else {
		VRB_Arc( blade, dir, R );
		if ( grip->sign < 0.0f ) {
			// (about the palm's axis as it lies square to the fist's line)
			vrbMat_t F;
			vec3_t palm;
			VectorMA( ax[2], -DotProduct( ax[2], dir ), dir, palm );
			VectorNormalize( palm );
			VRB_AxisAngle( palm, M_PI, F );
			VRB_Mul( F, R, R );
		}
	}
	CG_VRB_ModelMove( cent, R, tag.origin, at, &E );
	if ( vrb.handBoneTurn[h][1] ) {
		VRB_Compose( &E, H, &E );	// (on top of the wrist turn it already has)
	}
	if ( !trap->ext.VR_G2SetBoneTransform( cent->ghoul2, 0, vrbHandBones[h][1], &E ) ) {
		return qfalse;
	}
	vrb.curlG2 = cent->ghoul2;	// (cleared with the fingers)
	if ( kept && commit && !vrb.hold[s].keptInFist ) {
		for ( k = 0; k < 3; k++ ) {
			vrb.hold[s].keptFist[k] = DotProduct( faceWant, ax[k] );
		}
		vrb.hold[s].keptInFist = qtrue;
	}
	if ( haveFace && s >= 0 && s < MAX_SABERS ) {
		vec3_t face;
		VRB_Apply( R, faceNow, face );
		for ( k = 0; k < 3; k++ ) {
			vrb.drawnBlade[s][k] = DotProduct( want, ctrl[k] );
			vrb.drawnFace[s][k] = DotProduct( face, ctrl[k] );
		}
		vrb.drawnTime[s] = cg.time ? cg.time : 1;
		vrb.drawnHand[s] = h;
	}
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
=================
CG_VRB_PoseFingers

An empty hand's fingers - and a gun hand's index finger - follow yours
(cg_vrFingers; qcommon/vr_fingers.h: how far each is closed, from what your
controller senses), eased so a light touch doesn't flicker; another VR
player's, as their avatar says. Each finger goes where it says in the hand's
own frame (VRB_HandFrame), whatever the body's animation has it do: the
index finger and the other three bend toward the palm at both joints, from
straight to as far as the dual-saber stance's fist has them; the thumb
swings from up (an open hand, a thumbs up) down onto the side of the index
finger - over the fingers of a fist. A gun hand's thumb and other three
hold the gun as the animation has them; its index finger lies along the
guard until it touches the trigger, then curls onto it.
=================
*/

#define VRB_FINGER_EASE		0.025f	// seconds: a finger eases toward yours (a light touch doesn't flicker)
#define VRB_FINGER_FRESH	100		// ms: your fingers eased this lately are yours as drawn
#define VRB_GUN_GUARD		0.05f	// a gun hand's index finger along the guard...
#define VRB_GUN_TRIGGER		0.45f	// ...on the trigger...
#define VRB_GUN_PULLED		0.6f	// ...pulling it all the way
#define VRB_THUMB_OVER		0.35f	// the index finger and the other three closed past this: the thumb goes over them...
#define VRB_THUMB_OVER_ALL	0.85f	// ...and this far, it's right over them (a fist)

// the index finger and the other three closed into a fist: degrees at the
// first joint and the second (VRB_FINGERS: the thumb's aren't used)
static const float vrbCurlMax[VRB_FINGERS][2] = { { 0.0f, 0.0f }, { 85.0f, 112.0f }, { 90.0f, 120.0f } };

// The thumb's bones (its first, and the rest of it) in the hand's frame:
// along the hand, toward the thumb, out of the back of the hand - up; down
// beside the index finger; down over the fingers of a fist.
static const vec3_t vrbThumbUp[2] = { { 0.60f, 0.72f, -0.35f }, { 0.55f, 0.82f, -0.15f } };
static const vec3_t vrbThumbBeside[2] = { { 0.731f, 0.303f, -0.612f }, { 0.906f, -0.044f, -0.42f } };
static const vec3_t vrbThumbOver[2] = { { 0.687f, -0.124f, -0.716f }, { 0.814f, -0.552f, -0.177f } };

// a direction given in the hand's frame (along it, toward the thumb, out of its back), in the world
static void VRB_HandDir( vec3_t ax[3], const vec3_t back, const vec3_t d, vec3_t out ) {
	VectorScale( ax[0], d[0], out );
	VectorMA( out, d[1], ax[1], out );
	VectorMA( out, d[2], back, out );
	VectorNormalize( out );
}

// how far b is turned from a about the unit axis n (both as they lie square to it; radians)
static float VRB_AngleAbout( const vec3_t a, const vec3_t b, const vec3_t n ) {
	vec3_t pa, pb, c;

	VectorMA( a, -DotProduct( a, n ), n, pa );
	VectorMA( b, -DotProduct( b, n ), n, pb );
	CrossProduct( pa, pb, c );
	return atan2f( DotProduct( c, n ), DotProduct( pa, pb ) );
}

// A finger's bones (VRB_FINGERS) as they are: its first joint, its second, and
// where its tip is - the second joint's own axis that runs the way the first
// joint's runs along the finger, as CG_VRB_CurlFingers has it.
static qboolean CG_VRB_FingerBones( centity_t *cent, int h, int f, vec3_t o1, vec3_t o2, vec3_t tip ) {
	const int j1 = vrbFingerJ1[f];
	vrbBone_t b1, b2;
	vec3_t seg;
	float len, best = 0.0f;
	int k, along = 0;

	if ( !CG_VRB_Bone( cent, vrbHandBones[h][j1], 1, &b1 ) || !CG_VRB_Bone( cent, vrbHandBones[h][j1 + 1], 1, &b2 ) ) {
		return qfalse;
	}
	VectorSubtract( b2.origin, b1.origin, seg );
	len = VectorNormalize( seg );
	if ( len < 0.2f ) {
		return qfalse;
	}
	for ( k = 0; k < 3; k++ ) {
		const float d = DotProduct( b1.axis[k], seg );
		if ( fabsf( d ) > fabsf( best ) ) {
			best = d;
			along = k;
		}
	}
	VectorCopy( b1.origin, o1 );
	VectorCopy( b2.origin, o2 );
	VectorMA( b2.origin, ( best < 0.0f ? -0.9f : 0.9f ) * len, b2.axis[along], tip );
	return qtrue;
}

// (after the arm and wrist are posed; H: the wrist turn already on the bones
// that turn with the hand, in model space; curl: how far each finger is
// closed, VR_FINGER_*; gunHand: only the index finger)
static void CG_VRB_PoseFingers( centity_t *cent, int h, const mdxaBone_t *H, const float curl[VR_NUM_FINGERS], qboolean gunHand ) {
	const float mirror = ( h == VR_HAND_RIGHT ) ? 1.0f : -1.0f;
	vrbBone_t hand;
	vec3_t o1[VRB_FINGERS], o2[VRB_FINGERS], tip[VRB_FINGERS], ax[3], back, n;
	float c[VRB_FINGERS], length;
	int f;

	if ( !trap->ext.VR_G2SetBoneTransform || !CG_VRB_Bone( cent, vrbArmBones[h][VRB_HAND], 1, &hand ) ) {
		return;
	}
	// every finger first, then the turns (setting one rebuilds the skeleton
	// for the next query, and the fingers don't move each other)
	for ( f = 0; f < VRB_FINGERS; f++ ) {
		if ( !CG_VRB_FingerBones( cent, h, f, o1[f], o2[f], tip[f] ) ) {
			return;
		}
		c[f] = ( curl[f] > 0.0f ) ? Q_min( 1.0f, curl[f] ) : 0.0f;	// (VRB_FINGERS: thumb, index, the rest - as VR_FINGER_*)
	}
	if ( !VRB_HandFrame( hand.origin, o1[0], o1[1], o1[2], ax, &length ) ) {
		return;
	}
	VectorScale( ax[2], mirror, back );		// out of the back of either hand
	VectorScale( ax[1], mirror, n );		// the index finger and the rest bend toward the palm about this

	for ( f = 0; f < VRB_FINGERS; f++ ) {
		const int j1 = vrbFingerJ1[f], j2 = j1 + 1;
		vrbMat_t R1, R2;
		mdxaBone_t C1, C2, m;
		vec3_t s1, s2, p2, rel;

		if ( gunHand && f != VR_FINGER_INDEX ) {
			continue;
		}
		VectorSubtract( o2[f], o1[f], s1 );
		VectorSubtract( tip[f], o2[f], s2 );
		VectorNormalize( s1 );
		VectorNormalize( s2 );
		if ( f == VR_FINGER_THUMB ) {
			// up, or down beside the index finger - over the fingers, as far
			// as they're closed into a fist
			const float over = Com_Clamp( 0.0f, 1.0f, ( Q_min( c[VR_FINGER_INDEX], c[VR_FINGER_OTHERS] ) - VRB_THUMB_OVER ) /
				( VRB_THUMB_OVER_ALL - VRB_THUMB_OVER ) );
			vec3_t want[2], up, beside, onTop, down;
			int i;
			for ( i = 0; i < 2; i++ ) {
				VRB_HandDir( ax, back, vrbThumbUp[i], up );
				VRB_HandDir( ax, back, vrbThumbBeside[i], beside );
				VRB_HandDir( ax, back, vrbThumbOver[i], onTop );
				VRB_Slerp( beside, onTop, over, down );
				VRB_Slerp( up, down, c[f], want[i] );
			}
			VRB_Arc( s1, want[0], R1 );
			VRB_Apply( R1, s2, rel );
			VRB_Arc( rel, want[1], R2 );
		} else {
			// both joints bent toward the palm as far as it's closed
			const float t1 = DEG2RAD( c[f] * vrbCurlMax[f][0] ), t2 = DEG2RAD( c[f] * vrbCurlMax[f][1] );
			VRB_AxisAngle( n, t1 - VRB_AngleAbout( ax[0], s1, n ), R1 );
			VRB_Apply( R1, s1, s1 );
			VRB_Apply( R1, s2, s2 );
			VRB_AxisAngle( n, t2 - VRB_AngleAbout( s1, s2, n ), R2 );
		}
		VectorSubtract( o2[f], o1[f], rel );
		VRB_Apply( R1, rel, rel );
		VectorAdd( o1[f], rel, p2 );
		CG_VRB_ModelTurn( cent, R1, o1[f], &C1 );
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
	}
}

static void CG_VRB_Grip( centity_t *cent, int h, vrbGrip_t *grip );

// (the log) what a controller senses of your fingers
static const char *CG_VRB_SensedText( int sensed ) {
	static char text[160];

	if ( !sensed ) {
		return "nothing of them (the trigger and the grip only)";
	}
	Com_sprintf( text, sizeof( text ), "%s%s%s%s%s",
		( sensed & VR_SENSE_TRIGGER_TOUCH ) ? "the trigger touched, " : "",
		( sensed & VR_SENSE_TRIGGER_NEAR ) ? "the finger near it, " : "",
		( sensed & VR_SENSE_INDEX_CURL ) ? "the finger's curl, " : "",
		( sensed & VR_SENSE_THUMB_TOUCH ) ? "the thumb on the controller, " : "",
		( sensed & VR_SENSE_THUMB_NEAR ) ? "the thumb near it, " : "" );
	text[strlen( text ) - 2] = 0;	// (", ")
	return text;
}

/*
Your fingers this frame: how far each is closed (VR_FINGER_*), eased toward
what your controllers sense (qcommon/vr_fingers.h) - a gun hand's index
finger along the guard until it touches the trigger, then curled onto it,
closing a little as it's pulled. Once a frame, before the arms are posed.
*/
static void CG_VRB_EaseFingers( centity_t *cent, const vrClientState_t *vr ) {
	const float dt = Com_Clamp( 0.0f, 0.1f, ( cg.time - vrb.fingerTime ) * 0.001f );
	const qboolean afresh = ( !vrb.fingerTime || cg.time - vrb.fingerTime > 500 || cg.time < vrb.fingerTime ) ? qtrue : qfalse;
	const float k = 1.0f - expf( -dt / VRB_FINGER_EASE );
	int h, f;

	if ( vrb.fingerTime == cg.time ) {
		return;		// (the second eye)
	}
	for ( h = 0; h < 2; h++ ) {
		const vrFingerSense_t *s = &vr->fingers[h];
		vrbGrip_t grip;
		float want[VR_NUM_FINGERS];

		VR_FingerCurls( s, want );
		CG_VRB_Grip( cent, h, &grip );
		if ( grip.kind == VRB_GRIP_GUN ) {
			want[VR_FINGER_INDEX] = VR_FingerOnTrigger( s ) ?
				VRB_GUN_TRIGGER + ( VRB_GUN_PULLED - VRB_GUN_TRIGGER ) * VR_FingerClamp( s->trigger ) : VRB_GUN_GUARD;
		}
		for ( f = 0; f < VR_NUM_FINGERS; f++ ) {
			if ( afresh || !vrb.fingersEased[h] ) {
				vrb.fingers[h][f] = want[f];
			} else {
				vrb.fingers[h][f] += k * ( want[f] - vrb.fingers[h][f] );
			}
		}
		vrb.fingersEased[h] = qtrue;

		// (the log: what this controller senses of your fingers, as it changes)
		if ( vrb.fingerSensed[h] != s->sensed + 1 ) {
			vrb.fingerSensed[h] = s->sensed + 1;
			CG_VRB_Log( "fingers: the %s controller senses %s\n", h == VR_HAND_RIGHT ? "right" : "left",
				CG_VRB_SensedText( s->sensed ) );
		}
	}
	vrb.fingerTime = cg.time;
}

// How far hand h's fingers are closed (VR_FINGER_*) on the body being posed:
// yours as eased this frame (cg_vrFingers on), another player's as their
// avatar says. qfalse: the animation has them.
static qboolean CG_VRB_HandFingers( int h, float out[VR_NUM_FINGERS] ) {
	int f;

	if ( vrbRemote ) {
		if ( !vrbRemote->hand[h].fingersValid ) {
			return qfalse;
		}
		for ( f = 0; f < VR_NUM_FINGERS; f++ ) {
			out[f] = VR_FingerClamp( vrbRemote->hand[h].fingers[f] );
		}
		return qtrue;
	}
	if ( !cg_vrFingers.integer || !vrb.fingersEased[h] || cg.time - vrb.fingerTime > VRB_FINGER_FRESH ||
		cg.time < vrb.fingerTime ) {
		return qfalse;
	}
	for ( f = 0; f < VR_NUM_FINGERS; f++ ) {
		out[f] = vrb.fingers[h][f];
	}
	return qtrue;
}

// The hand the game's own weapon - anything but a saber: a gun, an explosive
// - hangs on (CG_VRB_BoltHands): your gun hand; another player's, the hand
// their avatar holds a gun in.
static int CG_VRB_GunBoltHand( void ) {
	if ( vrbRemote ) {
		return ( vrbRemote->hand[VR_HAND_LEFT].grip == VR_GRIP_GUN && vrbRemote->hand[VR_HAND_RIGHT].grip != VR_GRIP_GUN ) ?
			VR_HAND_LEFT : VR_HAND_RIGHT;
	}
	return CG_VR_GunHand();
}

// Whether that weapon hangs on hand h, held as the game holds it - not a gun
// in the hand as drawn (VRB_GRIP_GUN), but one aimed with the head, or a
// scope's: its fingers stay round it as the animation has them.
static qboolean CG_VRB_WeaponOnHand( centity_t *cent, int h ) {
	return (qboolean)( cent->currentState.weapon != WP_SABER && cent->ghoul2 &&
		trap->G2API_HasGhoul2ModelOnIndex( &cent->ghoul2, 1 ) && h == CG_VRB_GunBoltHand() );
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
	if ( !CG_VR_HandAim() && !( saber && cg_vrSaber.integer ) &&
		!( cent->currentState.weapon == WP_MELEE && cg_vrSaber.integer && CG_VRH_On() ) ) {
		// no gun in the VR hand (duel modes, a demo, vr_handAim 0, the gun
		// hand's controller lost) and no first-person saber - nor, with
		// holsters, your own empty hands (fists: cg_vrholster.c): the stock view
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

// The game's poses for standing still - most of them the saber stances. Those
// crouch, legs bent and back hunched: the model's eyes come 6 to 8 units
// under the camera (its head is at 36 over the origin, theirs at 28 to 30),
// so hands held where your real ones are - measured from your eyes - sit high
// on its body: hands at your waist at its chest.
static qboolean CG_VRB_StandingAnim( const clientInfo_t *ci, int anim ) {
	switch ( anim ) {
	case BOTH_STAND1:
	case BOTH_STAND1IDLE1:
	case BOTH_STAND2:
	case BOTH_STAND2IDLE1:
	case BOTH_STAND2IDLE2:
	case BOTH_STAND1TO2:
	case BOTH_STAND2TO1:
	case BOTH_SABERFAST_STANCE:
	case BOTH_SABERSLOW_STANCE:
	case BOTH_SABERDUAL_STANCE:
	case BOTH_SABERSTAFF_STANCE:
		return qtrue;
	default:
		// (a saber's own stance, from its .sab)
		return (qboolean)( ci && ( ( ci->saber[0].readyAnim != -1 && anim == ci->saber[0].readyAnim ) ||
			( ci->saber[1].readyAnim != -1 && anim == ci->saber[1].readyAnim ) ) );
	}
}

// Whether a VR body's legs stand in the upright stance this frame: standing
// still, it bends (CG_VRB_BendWeight).
static void CG_VRB_NoteStanding( centity_t *cent, qboolean standing ) {
	const int n = cent->currentState.number;
	vrbState_t *st;

	if ( n < 0 || n >= MAX_CLIENTS ) {
		return;
	}
	st = ( n == cg.clientNum ) ? &vrbSelf : &vrbOthers[n];
	st->standing = standing;
	st->standTime = cg.time;
}

/*
Body Bends (cg_vrBodyBend): standing still, a VR body stands the way you do,
bending from its feet (CG_VRB_Bend). It eases in once the legs stand in the
upright stance on the ground, and out as you move off - running, jumping,
flipping, crouching with the game's crouch, a pose: the game's animation,
as before.
*/
#define VRB_BEND_IN			0.25f	// seconds the bend takes to ease in...
#define VRB_BEND_OUT		0.12f	// ...and out
#define VRB_STAND_MS		250		// (the legs said they stood this lately: CG_VR_BodyAnim)
#define VRB_CHEST_TURN		30.0f	// the chest turns this far off the hips at most (degrees: CG_VRB_ChestTwist)
#define VRB_NECK_YAW		70.0f	// the head turns off the chest this far at most: to a side,
#define VRB_NECK_DOWN		60.0f	// down,
#define VRB_NECK_UP			45.0f	// up,
#define VRB_NECK_ROLL		35.0f	// and tilted (degrees: VRB_HeadTurn)

// How far you look aside before the chest turns (degrees): as far as the
// neck turns easily (cg_vrBodyTwist), and no further than it turns at all.
static float CG_VRB_NeckEasy( void ) {
	return Com_Clamp( 0.0f, VRB_NECK_YAW, cg_vrBodyTwist.value );
}

// How far the body being posed bends this frame (0..1, eased; again in the
// same frame - the other eye - no time has passed, nothing changes): its
// legs standing (as CG_VR_BodyAnim said last frame - this is worked out
// before this frame's animation is set) on the ground, and it bending
// lately (a model with the bones for it, an engine with the transforms:
// CG_VRB_Bend). Body Bends turned off, it's upright at once (as
// CG_VRB_BendBody draws it).
static float CG_VRB_BendWeight( qboolean onGround ) {
	const qboolean standing = (qboolean)( vrb.standing && cg.time - vrb.standTime >= 0 && cg.time - vrb.standTime < VRB_STAND_MS );
	const qboolean bends = (qboolean)( vrb.bendOkTime && cg.time - vrb.bendOkTime >= 0 && cg.time - vrb.bendOkTime < VRB_STAND_MS );
	const float want = ( cg_vrBodyBend.integer && standing && onGround && bends ) ? 1.0f : 0.0f;
	const float dt = Com_Clamp( 0.0f, 0.1f, ( cg.time - vrb.bendTime ) * 0.001f );

	if ( !cg_vrBodyBend.integer || cg.time - vrb.bendTime < 0 || cg.time - vrb.bendTime > 500 ) {
		vrb.bend = 0.0f;	// (off: upright at once; starting over: it eases in)
	}
	if ( want > vrb.bend ) {
		vrb.bend = Q_min( want, vrb.bend + dt / VRB_BEND_IN );
	} else {
		vrb.bend = Q_max( want, vrb.bend - dt / VRB_BEND_OUT );
	}
	vrb.bendTime = cg.time;
	return VRB_SmoothStep( 0.0f, 1.0f, vrb.bend );
}

// ...as worked out for this frame (0 if it wasn't: the body isn't drawn bent)
static float CG_VRB_BendNow( void ) {
	return ( vrb.bendTime == cg.time ) ? VRB_SmoothStep( 0.0f, 1.0f, vrb.bend ) : 0.0f;
}

// ...and as it was lately (before this frame's is worked out)
static float CG_VRB_BendLately( void ) {
	return ( cg.time - vrb.bendTime >= 0 && cg.time - vrb.bendTime <= 500 ) ? VRB_SmoothStep( 0.0f, 1.0f, vrb.bend ) : 0.0f;
}

// The body's animation, before it is drawn: a VR body stays upright through
// flips and rolls, as you do in your headset; and standing still it stands up
// straight, as you do (its eyes then where yours are, so your hands sit on it
// where they are on you - the arms are on the controllers anyway), bending
// from there the way you do (CG_VRB_Bend). Whatever else the game plays -
// running, crouching, pain, Force gestures - stays.
void CG_VR_BodyAnim( centity_t *cent, int *legsAnim, int *torsoAnim ) {
	const playerState_t *ps = &cg.predictedPlayerState;
	const animation_t *anims = bgAllAnims[cent->localAnimIndex].anims;
	const clientInfo_t *ci = NULL;
	qboolean roll, arms;
	int anim;

	if ( cent->currentState.number != cg.clientNum ) {
		// (another VR player: their avatar's hands hold their arms)
		if ( !CG_VRB_OtherUsable( cent ) ) {
			return;
		}
		roll = BG_InRollES( &cent->currentState, *legsAnim );
		arms = qtrue;
	} else if ( CG_VRB_BodyUsable( cent ) ) {
		roll = BG_InRoll( (playerState_t *)ps, *legsAnim );
		arms = (qboolean)( cg_vrBodyArms.integer != 0 );
	} else {
		return;
	}
	CG_VRB_NoteStanding( cent, qfalse );
	if ( !anims ) {
		return;
	}
	if ( roll || BG_FlippingAnim( *legsAnim ) || BG_InSpecialJump( *legsAnim ) ) {
		anim = roll ? BOTH_CROUCH1IDLE : BOTH_FORCEINAIR1;
		if ( !anims[anim].numFrames ) {
			anim = BOTH_INAIR1;
			if ( !anims[anim].numFrames ) {
				return;
			}
		}
		*legsAnim = *torsoAnim = anim;
		return;
	}

	// standing up straight (STAND5: "back straight", its eyes at 36)
	anim = anims[BOTH_STAND5].numFrames ? BOTH_STAND5 : BOTH_STAND1;
	if ( !anims[anim].numFrames ) {
		return;
	}
	if ( cent->currentState.number >= 0 && cent->currentState.number < MAX_CLIENTS ) {
		ci = &cgs.clientinfo[cent->currentState.number];
	}
	if ( CG_VRB_StandingAnim( ci, *legsAnim ) ) {
		*legsAnim = anim;
		CG_VRB_NoteStanding( cent, qtrue );
	}
	if ( arms && CG_VRB_StandingAnim( ci, *torsoAnim ) ) {
		*torsoAnim = anim;
	}
}

// Whether your sabers are off: all of them. One of your two lit alone (the
// first), or one blade of a staff, is lit - as the server lights blades for
// VR hands (g_vrsaber.c, VRS_BladeOn), and as its game counts stock sabers
// (BG_SabersOff: it fights those in a base style for two blades, the dual
// one or the staff's; a staff whose .sab lets it fight in other styles, or a
// siege class's two sabers kept to one stance, it counts off with one blade
// lit, and there the trigger puts that blade out). Told by what you have,
// not by the player state's base style, which the server doesn't send: the
// prediction guesses that before each move it runs (cg_predict.c), so on
// the frame a move lights one of your two - the trigger's attack lights it
// - the guess is still the one from before (your style then: a single one,
// unless both were last lit together), and the lit saber would count as
// off. The trigger's attack would go out with your next command as well,
// and the server, counting it lit, would swing it: the game's swing, the
// moment you light it.
static qboolean CG_VRB_SabersOff( const playerState_t *ps, const clientInfo_t *ci ) {
	if ( ps->saberHolstered <= 0 ) {
		return qfalse;
	}
	if ( ps->saberHolstered >= 2 ) {
		return qtrue;
	}
	return (qboolean)!( ci->saber[1].model[0] || ci->saber[0].numBlades > 1 );
}

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
	if ( CG_VRB_SabersOff( ps, &cgs.clientinfo[ps->clientNum] ) ) {
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
static const animation_t *CG_VRB_Anims( void );

static float CG_VRB_BodyYaw( const vrClientState_t *vr ) {
	vec3_t head[3];
	float headYaw, diff, maxTwist, rate, dt;
	qboolean posed;
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

	// (sat, knelt or lying, the body stays as it is while you look about,
	// unless you turn right round: bg_vrpose.c)
	posed = (qboolean)( BG_VRPosePhase( &cg.predictedPlayerState, CG_VRB_Anims(), NULL, NULL ) != VRPHASE_NONE );
	// (bent, the neck turns first, then the chest, the hips last - past what
	// the neck turns easily and the chest's turn together: CG_VRB_ChestTwist;
	// as of last frame - this frame's isn't worked out yet)
	maxTwist = Com_Clamp( 0.0f, 90.0f, cg_vrBodyTwist.value );
	maxTwist = posed ? 120.0f : maxTwist + ( CG_VRB_NeckEasy() + VRB_CHEST_TURN - maxTwist ) * CG_VRB_BendLately();
	diff = AngleSubtract( headYaw, vrb.yawT );
	if ( diff > maxTwist ) {
		vrb.yawT = headYaw - maxTwist;
	} else if ( diff < -maxTwist ) {
		vrb.yawT = headYaw + maxTwist;
	}
	rate = ( speed > 20.0f ) ? 6.0f : 1.0f;
	if ( !posed ) {
		vrb.yawT = AngleNormalize360( vrb.yawT + AngleSubtract( headYaw, vrb.yawT ) * ( 1.0f - expf( -rate * dt ) ) );
	}

	// tracking space -> world, exactly as the renderer turns the view
	vrb.worldYaw = AngleNormalize360( vrb.yawT + VR_AxisYaw( cg.refdef.viewaxis ) - vr->referenceYaw );
	vrb.yawTime = cg.time;
	vrb.lastActive = cg.time;
	return vrb.worldYaw;
}

// (The game turns the body from its skeleton as animated - its "Motion" bone,
// under the pelvis: it does so before the body bends again, CG_VRB_Bend.)
qboolean CG_VR_BodyAngles( centity_t *cent, vec3_t angles ) {
	const vrClientState_t *vr;

	if ( cent->currentState.number != cg.clientNum ) {
		// another VR player: where their body faces (level, like yours)
		if ( !CG_VRB_OtherUsable( cent ) ) {
			return qfalse;
		}
		vrbCur = &vrbOthers[cent->currentState.number];
		CG_VRB_ResetBody( cent->ghoul2 );
		vrbCur = &vrbSelf;
		VectorSet( angles, 0.0f, vrbOthers[cent->currentState.number].avatar.bodyYaw, 0.0f );
		return qtrue;
	}
	CG_VRB_Init();
	if ( !CG_VR_BodyActive( cent ) || !( vr = CG_VR() ) ) {
		return qfalse;
	}
	CG_VRB_ResetBody( cent->ghoul2 );
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

/*
=================
Which hand holds which saber

Yours (CG_VRB_Holds, once a frame before the arms): the first saber in the
hand you use - the right, or the left with vr_leftHanded on - a second one
in the other, both the usual way round, where their own tags hold them;
until one is passed from hand to hand (CG_VRB_TwoHand), which keeps how the
hand that took it over held it - held with both hands, the hand you use
always has it (CG_VRB_TakeLead). Put away (another weapon), dead, thrown,
or with handedness or the sabers changed, they go back to that. With
holsters (cg_vrholster.c), a saber's in a hand only once taken out of its
holster (hand -1: on your body), and they say which.
Another player's: what their avatar's hands hold.
=================
*/

// The hand holding saber s (VR_HAND_*); -1: none (another player's avatar
// has no hand on it).
static int CG_VRB_SaberHand( int s ) {
	if ( s < 0 || s >= MAX_SABERS ) {
		return -1;
	}
	if ( vrbRemote ) {
		int h;
		for ( h = VR_HAND_RIGHT; h >= VR_HAND_LEFT; h-- ) {	// (both saying they hold it: the right does)
			if ( vrbRemote->hand[h].grip == VR_GRIP_SABER && vrbRemote->hand[h].saber == s ) {
				return h;
			}
		}
		return -1;
	}
	return vrb.hold[s].hand;
}

// The saber in hand h (0, 1 - whether there is one or not); -1: none
// (another player's; yours with holsters, both on your body).
static int CG_VRB_HandSaber( int h ) {
	if ( h < VR_HAND_LEFT || h > VR_HAND_RIGHT ) {
		return -1;
	}
	if ( vrbRemote ) {
		const vrAvatarHand_t *ah = &vrbRemote->hand[h];
		if ( ah->grip != VR_GRIP_SABER || ( ah->saber != 0 && ah->saber != 1 ) ) {
			return -1;
		}
		return ( CG_VRB_SaberHand( ah->saber ) == h ) ? ah->saber : -1;
	}
	return ( vrb.hold[0].hand == h ) ? 0 : ( vrb.hold[1].hand == h ) ? 1 : -1;	// (holsters: in neither)
}

// Both sabers back as the game first puts them: the first in the main hand
// (none lent to it: CG_VRB_TakeLead).
static void CG_VRB_ResetHolds( int main ) {
	int s;

	for ( s = 0; s < MAX_SABERS; s++ ) {
		memset( &vrb.hold[s], 0, sizeof( vrb.hold[s] ) );
		vrb.hold[s].hand = ( s == 0 ) ? main : ( main ^ 1 );
		vrb.hold[s].sign = 1.0f;
	}
	vrb.twoHandLent = qfalse;
}

// (a name, as a number)
static unsigned int VRB_Hash( const char *s ) {
	unsigned int h = 0;

	while ( *s ) {
		h = h * 31u + (unsigned int)tolower( (unsigned char)*s++ );
	}
	return h;
}

static void CG_VRB_Holds( centity_t *cent ) {
	const clientInfo_t *ci = &cgs.clientinfo[cent->currentState.number];
	const int main = CG_VR_GunHand();
	// (the sabers either way round: changed over, holsters' - cg_vrholster.c - they're still the ones in hand)
	const int key = (int)( ( (unsigned int)main + 2u * ( VRB_Hash( ci->saber[0].model ) + VRB_Hash( ci->saber[1].model ) ) ) &
		0x7fffffff );

	if ( vrb.holdTime == cg.time ) {
		return;	// (second eye)
	}
	vrb.holdTime = cg.time;
	if ( CG_VRH_On() ) {
		// holsters: in a hand once taken out, theirs to say - handedness or the
		// sabers changed, or holsters just come into play, all back on your body
		if ( key != vrb.holdKey || !vrb.holdHolsters ) {
			vrb.holdKey = key;
			vrb.holdHolsters = qtrue;
			CG_VRB_ResetHolds( main );
			vrb.hold[0].hand = vrb.hold[1].hand = -1;
		}
		CG_VRH_HandsFrame( CG_VR() );
		if ( cent->currentState.saberInFlight ) {
			vrb.hold[0].sign = 1.0f;	// (thrown: it comes back to that hand, held the usual way)
			vrb.hold[0].t = 0.0f;
			vrb.hold[0].passTime = 0;
			vrb.hold[0].kept = vrb.hold[0].keptFace = vrb.hold[0].keptGrip = qfalse;
		}
		return;
	}
	vrb.holdHolsters = qfalse;
	CG_VRH_HandsFrame( CG_VR() );	// (off: nothing said, nothing hangs)
	if ( key != vrb.holdKey || cent->currentState.weapon != WP_SABER || ( cent->currentState.eFlags & EF_DEAD ) ||
		cg.predictedPlayerState.stats[STAT_HEALTH] <= 0 || vrb.hold[0].hand < 0 || vrb.hold[1].hand < 0 ) {
		if ( vrb.holdKey != -1 && ( vrb.hold[0].hand != main || vrb.hold[0].sign < 0.0f || vrb.hold[0].t != 0.0f ) ) {
			CG_VRB_Log( "hands: the first saber back in the %s hand\n", main == VR_HAND_RIGHT ? "right" : "left" );
		}
		vrb.holdKey = key;
		CG_VRB_ResetHolds( main );
		return;
	}
	if ( cent->currentState.saberInFlight ) {
		// thrown: it comes back to that hand, held the usual way
		vrb.hold[0].sign = 1.0f;
		vrb.hold[0].t = 0.0f;
		vrb.hold[0].passTime = 0;
		vrb.hold[0].kept = vrb.hold[0].keptFace = vrb.hold[0].keptGrip = qfalse;
	}
}

// v given in the frame ax (components along ax[0], ax[1], ax[2]), in the world
static void VRB_FromFrame( vec3_t ax[3], const vec3_t v, vec3_t out ) {
	vec3_t t;

	VectorScale( ax[0], v[0], t );
	VectorMA( t, v[1], ax[1], t );
	VectorMA( t, v[2], ax[2], out );
}

// Saber s, going over to hand h (its controller's axes ax) from hand
// fromHand, turned there the way it was last drawn in that hand - as that
// hand has it now (its controller's axes now: from): its face, and the
// blade it goes with, kept in h's frame. Not drawn lately in that hand (put
// away, not moved into a fist), it hangs as h's tag has it.
static void CG_VRB_KeepFace( int s, int h, int fromHand, vec3_t from[3], vec3_t ax[3] ) {
	vec3_t face, ref;
	int i;

	vrb.hold[s].keptFace = vrb.hold[s].keptInFist = qfalse;
	VRB_FromFrame( from, vrb.drawnFace[s], face );
	VRB_FromFrame( from, vrb.drawnBlade[s], ref );
	if ( cg.time - vrb.drawnTime[s] < 0 || cg.time - vrb.drawnTime[s] > VRB_FACE_MS || vrb.drawnHand[s] != fromHand ||
		!( DotProduct( face, face ) > 0.25f ) ) {
		return;	// (or no face to keep: never drawn, that hand not on its controller)
	}
	for ( i = 0; i < 3; i++ ) {
		vrb.hold[s].keptFaceDir[i] = DotProduct( face, ax[i] );
		vrb.hold[s].keptFaceRef[i] = DotProduct( ref, ax[i] );
	}
	vrb.hold[s].keptFace = qtrue;
	vrb.hold[s].keptHand = h;
}

// A saber just passed into its hand: how far it has gone over from where it
// was as it was passed (0..1; 1: all the way, or not passed).
static float CG_VRB_PassEase( int s ) {
	int t;

	if ( vrbRemote || s < 0 || s >= MAX_SABERS || !vrb.hold[s].passTime ) {
		return 1.0f;
	}
	t = cg.time - vrb.hold[s].passTime;
	if ( t < 0 || t >= VRB_PASS_MS ) {
		vrb.hold[s].passTime = 0;
		return 1.0f;
	}
	return VRB_SmoothStep( 0.0f, 1.0f, (float)t / VRB_PASS_MS );
}

// A hand that has just passed on a saber it held lying as it lay on the hilt
// (hold[].keptGrip): how far it has eased off that onto its controller (0..1;
// 1: all the way, or not so).
static float CG_VRB_LetGoEase( int h ) {
	int t;

	if ( vrbRemote || h < VR_HAND_LEFT || h > VR_HAND_RIGHT || !vrb.letGo[h].time ) {
		return 1.0f;
	}
	t = cg.time - vrb.letGo[h].time;
	if ( t < 0 || t >= VRB_PASS_MS ) {
		vrb.letGo[h].time = 0;
		return 1.0f;
	}
	return VRB_SmoothStep( 0.0f, 1.0f, (float)t / VRB_PASS_MS );
}

// Where the blade of a saber in a hand points as the hand holds it (its
// controller's axes ax): along the fist, or the other way held reversed - or,
// passed into the hand, the way it lay in it then, for as long as it's in
// that hand (CG_VRB_PassSaber; cg_vrSaberPitch doesn't tilt it then).
// Another player's never: their avatar says where theirs points.
static void CG_VRB_HeldBladeDir( const vrbGrip_t *grip, vec3_t ax[3], vec3_t want ) {
	if ( grip->saber >= 0 && grip->saber < MAX_SABERS && vrb.hold[grip->saber].kept ) {
		VRB_FromFrame( ax, vrb.hold[grip->saber].keptDir, want );
		VectorNormalize( want );
		return;
	}
	CG_VRB_SaberDir( ax, want );
	VectorScale( want, ( grip->sign < 0.0f ) ? -1.0f : 1.0f, want );
}

// Which way the face of the saber in hand h (its controller's axes ax; the
// posed hand's own frame, VRB_HandFrame: fist) is to be turned, its blade
// drawn pointing want (world) along the fist: as it lay as it was passed
// into that hand. Placed there once, as it lies in that fist (it turns with
// the fist, never in it); till then, turned with the blade from where it
// pointed then (the least turn - however far the blade's drawn from there,
// its weight trailing it or another blade stopping it, a true turn: never
// undone). Into out (world); qfalse: as the hand's tag hangs it
// (CG_VRB_PlaceSaber).
static qboolean CG_VRB_KeptFace( const vrbGrip_t *grip, int h, vec3_t ax[3], vec3_t fist[3], const vec3_t want, vec3_t out ) {
	vec3_t ref, face;
	vrbMat_t R;

	if ( grip->saber < 0 || grip->saber >= MAX_SABERS || !vrb.hold[grip->saber].keptFace ||
		vrb.hold[grip->saber].keptHand != h ) {
		return qfalse;
	}
	if ( vrb.hold[grip->saber].keptInFist ) {
		VRB_FromFrame( fist, vrb.hold[grip->saber].keptFist, out );
		return qtrue;
	}
	VRB_FromFrame( ax, vrb.hold[grip->saber].keptFaceRef, ref );
	VRB_FromFrame( ax, vrb.hold[grip->saber].keptFaceDir, face );
	VRB_Arc( ref, want, R );
	VRB_Apply( R, face, out );
	return qtrue;
}

// What hand h holds this frame.
static void CG_VRB_Grip( centity_t *cent, int h, vrbGrip_t *grip ) {
	const clientInfo_t *ci = &cgs.clientinfo[cent->currentState.number];
	const int weapon = cent->currentState.weapon;
	int s;

	memset( grip, 0, sizeof( *grip ) );
	grip->kind = VRB_GRIP_FREE;
	grip->sign = 1.0f;

	if ( vrbRemote ) {
		// another player's hand: what their avatar says it holds
		const vrAvatarHand_t *ah = &vrbRemote->hand[h];
		int i;
		switch ( ah->grip ) {
		case VR_GRIP_SABER:
			s = CG_VRB_HandSaber( h );
			if ( weapon == WP_SABER && s >= 0 && trap->G2API_HasGhoul2ModelOnIndex( &cent->ghoul2, s + 1 ) ) {
				grip->kind = VRB_GRIP_SABER;
				grip->saberModel = s + 1;
				grip->saber = s;
				grip->sign = ah->reversed ? -1.0f : 1.0f;
				grip->hilt = ( ah->hilt == ah->hilt ) ? Com_Clamp( -VR_POSE_MAX_HILT, VR_POSE_MAX_HILT, ah->hilt ) : 0.0f;
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
		// the first saber in one hand, a second one in the other
		if ( h == vrb.twoHandOff && vrb.twoHandArms && vrb.twoHandTime == cg.time ) {
			// the other hand on the first one's hilt (or settling onto it, or
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
		s = CG_VRB_HandSaber( h );
		if ( s < 0 || ( s == 0 && cent->currentState.saberInFlight ) || ( s == 1 && !ci->saber[1].model[0] ) ) {
			return;
		}
		if ( trap->G2API_HasGhoul2ModelOnIndex( &cent->ghoul2, s + 1 ) ) {
			grip->kind = VRB_GRIP_SABER;
			grip->saberModel = s + 1;
			grip->saber = s;
			grip->sign = ( vrb.hold[s].sign < 0.0f ) ? -1.0f : 1.0f;
			grip->hilt = vrb.hold[s].t;
		}
		return;
	}
	if ( h == CG_VR_GunHand() && CG_VR_HandAim() ) {
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
solved: whether the other hand could take hold of the first saber's hilt
(told to the engine, which then doesn't press that grip's key when it's
squeezed), whether it holds it, and then where the blade points and where
on the hilt the other hand is - and whether the first hand passes it over
(its grip squeezed while both hold it: the engine is told it's a grab then
too). Held with both hands, the hand you use leads: in your other hand
first, it goes over to the hand you use once both have settled onto the
hilt, nothing moving (CG_VRB_TakeLead).
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
// holds (the other one round the first hand's too, while it holds it).
static void CG_VRB_HandPoses( centity_t *cent ) {
	int h;

	for ( h = 0; h < 2; h++ ) {
		vrbGrip_t grip;
		CG_VRB_Grip( cent, h, &grip );
		CG_VRB_Fist( cent, h, (qboolean)( grip.kind == VRB_GRIP_SABER ||
			( h == vrb.twoHandOff && vrb.twoHand && vrb.twoHandTime == cg.time ) ) );
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

// A frame for the hilt held in the first hand (grip; its controller's axes
// axM): along it (dir), and the left of that hand's fist square to it (its
// up if the hilt runs along that) - a fist holding it as it was passed into
// it (CG_VRB_HeldBladeDir) turned with it from its own line, the least, so
// the hilt doesn't run along that left however it lies in the hand.
static void CG_VRB_HiltFrame( const vrbGrip_t *grip, vec3_t axM[3], const vec3_t dir, vec3_t e[3] ) {
	vec3_t fist, held, side;
	vrbMat_t R;

	CG_VRB_SaberDir( axM, fist );
	VectorScale( fist, ( grip->sign < 0.0f ) ? -1.0f : 1.0f, fist );
	CG_VRB_HeldBladeDir( grip, axM, held );
	VRB_Arc( fist, held, R );
	VRB_Apply( R, axM[1], side );
	VectorCopy( dir, e[0] );
	VectorMA( side, -DotProduct( side, dir ), dir, e[1] );
	if ( VectorNormalize( e[1] ) < 0.1f ) {
		VectorMA( axM[2], -DotProduct( axM[2], dir ), dir, e[1] );
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

static const char *VRB_HandName( int h ) {
	return ( h == VR_HAND_RIGHT ) ? "right" : "left";
}

static void CG_VRB_BoltHands( centity_t *cent );
static float CG_VRB_RoomYaw( const vrClientState_t *vr );

// Where hand h's fist holding the first saber is drawn (pos, ax: its
// controller's grip): there - or, the saber just gone over to it, on its way
// there from where it took it over (CG_VRB_PassEase).
static void CG_VRB_FistAt( int h, const vec3_t pos, vec3_t ax[3], vec3_t out ) {
	float ease;

	VectorCopy( pos, out );
	if ( vrb.hold[0].hand != h ) {
		return;
	}
	ease = CG_VRB_PassEase( 0 );
	if ( ease < 1.0f ) {
		vec3_t ofs;
		VRB_FromFrame( ax, vrb.hold[0].passOfs, ofs );
		VectorMA( out, 1.0f - ease, ofs, out );
	}
}

// The first saber over from hand "from" to hand "to", which holds it with it
// (CG_VRB_TwoHand): "to" goes on holding it where it holds it on the hilt and
// the way round it holds it, and from then on it moves with that hand as if
// gripped there - its blade pointing, to that hand, along blade (world: where
// that hand's to hold it); the hilt turned about itself as it was drawn (as
// "from" has it this frame); the hand lying round the hilt as it did (not as
// its controller lies: it doesn't turn round the hilt as it takes it over).
// Nothing jumps: the fist, the saber in it, settles from where it's drawn
// holding the hilt onto its controller (CG_VRB_PassEase); the blade goes on
// from where it's drawn, as it's moving (its weight may have it trailing the
// hands, mid-swing: the same blade, CG_VRB_BladeSpring), and resting on
// another blade it rests on it still (CG_VRB_BladeSolid, from the new fist).
static void CG_VRB_HandOver( centity_t *cent, const vrClientState_t *vr, int from, int to, const vec3_t blade ) {
	vrbGrip_t gripF;
	vec3_t posF, axF[3], fistF, pos, ax[3], drawn, rel, e[3], held[3];
	int i, k;

	CG_VRB_Controller( vr, from, posF, axF );
	CG_VRB_FistAt( from, posF, axF, fistF );
	CG_VRB_Controller( vr, to, pos, ax );
	// where "to" holds the hilt as it's drawn: through the fist holding it (just
	// gone over to that, on its way to its controller), a blade trailing the
	// hands with the hilt (CG_VRB_BladeInertia)...
	VectorCopy( vrb.twoHandDir, drawn );
	if ( vrb.blade[from].valid ) {
		VRB_RotZ( vrb.blade[from].dir, CG_VRB_RoomYaw( vr ), drawn );
	}
	VectorMA( fistF, vrb.twoHandT, drawn, rel );
	VectorSubtract( rel, pos, rel );
	// ...and how it lies round it (as drawn, settled onto it: CG_VRB_TwoHand)
	CG_VRB_Grip( cent, from, &gripF );
	CG_VRB_HiltFrame( &gripF, axF, vrb.twoHandDir, e );
	for ( i = 0; i < 3; i++ ) {
		VectorClear( held[i] );
		for ( k = 0; k < 3; k++ ) {
			VectorMA( held[i], vrb.twoHandGrip[i][k], e[k], held[i] );
		}
	}
	vrb.letGo[to].time = 0;
	vrb.hold[0].hand = to;
	vrb.hold[0].sign = vrb.twoHandSign;
	vrb.hold[0].t = Com_Clamp( -VR_POSE_MAX_HILT, VR_POSE_MAX_HILT, vrb.hold[0].t + vrb.twoHandT );
	vrb.hold[0].passTime = cg.time ? cg.time : 1;
	for ( i = 0; i < 3; i++ ) {
		vrb.hold[0].keptDir[i] = DotProduct( blade, ax[i] );
		vrb.hold[0].passOfs[i] = DotProduct( rel, ax[i] );
		for ( k = 0; k < 3; k++ ) {
			vrb.hold[0].keptGripAxis[i][k] = DotProduct( held[i], ax[k] );
		}
	}
	vrb.hold[0].kept = vrb.hold[0].keptGrip = qtrue;
	CG_VRB_KeepFace( 0, to, from, axF, ax );
	if ( vrb.blade[from].valid ) {
		vrb.blade[to] = vrb.blade[from];
		VectorAdd( pos, rel, vrb.blade[to].pivot );	// (resting on another blade: found again from the new fist)
	}
	if ( !vrb.holdHolsters ) {
		vrb.hold[1].hand = from;	// (a second saber's in the other hand; with holsters it's where it was - in its holster)
		vrb.hold[1].sign = 1.0f;
		vrb.hold[1].t = 0.0f;
		vrb.hold[1].passTime = 0;
	}
}

// The first saber passed over from hand "from", which let go of it, to hand
// "to", which held it with it (CG_VRB_HandOver: the blade pointing where both
// hands pointed it) - and the hand letting go, if it lay round the hilt as it
// took it over, easing off that onto its controller (as everyone sees it:
// their games keep the saber's turn by that hand, CG_VRB_OtherFaces). Back:
// the hand you use letting go of a saber lent to it (CG_VRB_TakeLead) - it
// only lets go of it, as if it had never taken it over (only it feels it).
static void CG_VRB_PassSaber( centity_t *cent, const vrClientState_t *vr, int from, int to, qboolean back ) {
	int i, k;

	for ( i = 0; i < 3; i++ ) {
		for ( k = 0; k < 3; k++ ) {
			vrb.letGo[from].axis[i][k] = vrb.hold[0].keptGrip ? vrb.hold[0].keptGripAxis[i][k] : ( i == k ) ? 1.0f : 0.0f;
		}
	}
	if ( cg.time - vrb.heldTurnTime[from] >= 0 && cg.time - vrb.heldTurnTime[from] < 100 ) {
		memcpy( vrb.letGo[from].turn, vrb.heldTurn[from], sizeof( vrb.letGo[from].turn ) );
	} else {
		VRB_Identity( vrb.letGo[from].turn );
	}
	vrb.letGo[from].time = cg.time ? cg.time : 1;
	CG_VRB_HandOver( cent, vr, from, to, vrb.twoHandDir );
	vrb.grabBlocked[from] = qtrue;	// (its grip, if still squeezed, takes hold again only once that's let go)
	vrb.twoHand = vrb.twoHandArms = vrb.twoHandPass = vrb.twoHandLent = qfalse;
	vrb.twoHandBlend = 0.0f;
	vrb.twoHandOff = from;
	CG_VRB_BoltHands( cent );		// (the saber hangs on its new hand from now on)
	if ( back ) {
		CG_VRB_Feel( from, 0.3f, 20, 0.0f, 0, 0.0f );
		CG_VRB_Log( "two hands: the %s hand let go - the saber back in the %s (held %.1f along the hilt%s)\n",
			VRB_HandName( from ), VRB_HandName( to ), vrb.hold[0].t, vrb.hold[0].sign < 0.0f ? ", reversed" : "" );
		return;
	}
	CG_VRB_Feel( to, 0.5f, 35, 0.0f, 0, 0.0f );
	CG_VRB_Feel( from, 0.25f, 20, 0.0f, 0, 0.0f );
	CG_VRB_Log( "two hands: passed it from the %s hand to the %s (held %.1f along the hilt%s)\n", VRB_HandName( from ),
		VRB_HandName( to ), vrb.hold[0].t, vrb.hold[0].sign < 0.0f ? ", reversed" : "" );
}

// The way the first hand is to hold the blade for both hands to point it
// along a, the other's fist pointing d - as CG_VRB_TwoHand weighs them: that
// way, and as much of d as agrees with it (VRB_SmoothStep( -0.3, 0.7 ) of how
// far), before the line between the hands levers it. In the plane of a and d,
// on a's other side from d: d mirrored about a, the hands agreeing; nearer a
// as they don't.
static void VRB_TwoHandFirst( const vec3_t a, const vec3_t d, vec3_t out ) {
	const float c = DotProduct( a, d );
	float lo = 0.0f, hi = 1.0f, k = 0.5f, m;
	int i;

	// (k: how far they agree - as far as the way found agrees with d)
	for ( i = 0; i < 24; i++ ) {
		k = 0.5f * ( lo + hi );
		m = k * c + sqrtf( Q_max( 0.0f, 1.0f - k * k * ( 1.0f - c * c ) ) );
		if ( VRB_SmoothStep( -0.3f, 0.7f, m * c - k ) > k ) {
			lo = k;
		} else {
			hi = k;
		}
	}
	m = k * c + sqrtf( Q_max( 0.0f, 1.0f - k * k * ( 1.0f - c * c ) ) );
	VectorScale( a, m, out );
	VectorMA( out, -k, d, out );
	if ( VectorNormalize( out ) < 0.5f ) {
		VectorCopy( a, out );
	}
}

// The hand you use - the right, or the left with vr_leftHanded on - takes
// the lead (CG_VRB_TwoHand): the first saber held with both hands, in your
// other hand ("from"), it goes over to the hand you use ("to"), which leads
// from then on - the blade where its wrist points it, your other hand helping
// as far as it agrees and the line between them levering it; its trigger the
// saber's - and your other hand goes on holding the hilt where it holds it,
// the way round it holds it, pointing the blade as it held it. Nothing moves
// or turns: the hand you use takes it over where it holds it, as passing it
// over has it (CG_VRB_HandOver), holding the blade so that both hands point
// it where they do (avg: where they point it this frame, before the line
// between them levers it; VRB_TwoHandFirst - the hands agreeing, along its
// fist, as if it had held it all along), and your other hand lies on the
// hilt as it lies holding it. Lent: the saber's that other hand's still, not
// held by its grip (Saber Grip on Toggle; holding the grip, one it hasn't
// squeezed since it came to it - the stick brought it, say): it holds the
// hilt without, and has it back as the hand you use lets go.
static void CG_VRB_TakeLead( centity_t *cent, const vrClientState_t *vr, int from, int to, const vec3_t avg, qboolean lent ) {
	const float t = vrb.twoHandT, side = vrb.twoHandSide;
	vrbGrip_t grip;
	vec3_t pos, ax[3], lay[3], dir, blade, e[3];
	int i, k;

	// your other hand: how it lies on its controller holding it (as it's to
	// lie on the hilt), the way it points the blade holding it (as it's to be
	// weighed), and the way round it holds it
	CG_VRB_Controller( vr, from, pos, ax );
	CG_VRB_Grip( cent, from, &grip );
	for ( i = 0; i < 3; i++ ) {
		VectorCopy( ax[i], lay[i] );
		if ( vrb.hold[0].keptGrip ) {
			VRB_FromFrame( ax, vrb.hold[0].keptGripAxis[i], lay[i] );
		}
	}
	CG_VRB_HeldBladeDir( &grip, ax, dir );
	for ( i = 0; i < 3; i++ ) {
		vrb.twoHandOffDir[i] = DotProduct( dir, ax[i] );
	}
	VRB_TwoHandFirst( avg, dir, blade );
	CG_VRB_HandOver( cent, vr, from, to, blade );
	// your other hand on the hilt: as far from the hand you use as that is
	// from it, the other side of it, the way round it holds it, lying on it as
	// it lies
	vrb.twoHandOff = from;
	vrb.twoHandT = -t;
	vrb.twoHandSide = -side;
	vrb.twoHandSign = ( grip.sign < 0.0f ) ? -1.0f : 1.0f;
	vrb.twoHandOffKept = qtrue;
	vrb.twoHandLent = lent;
	vrb.grabBlocked[from] = qfalse;	// (its grip holds the hilt, whatever it was squeezed for - taking it out of its holster, say)
	CG_VRB_Controller( vr, to, pos, ax );
	CG_VRB_Grip( cent, to, &grip );
	CG_VRB_HiltFrame( &grip, ax, vrb.twoHandDir, e );
	for ( i = 0; i < 3; i++ ) {
		for ( k = 0; k < 3; k++ ) {
			vrb.twoHandGrip[i][k] = DotProduct( lay[i], e[k] );
		}
	}
	CG_VRB_BoltHands( cent );		// (the saber hangs on the hand you use from now on)
	CG_VRB_Log( "two hands: the %s hand leads (held %.1f along the hilt%s), the %s %s it%s\n", VRB_HandName( to ),
		vrb.hold[0].t, vrb.hold[0].sign < 0.0f ? ", reversed" : "", VRB_HandName( from ),
		vrb.twoHandSide > 0.0f ? "above" : "below", lent ? " (the saber's that one's, lent)" : "" );
}

// The hold this frame (was: held with both hands last frame; squeezed: the
// grips squeezed anew, taken as grabs). qtrue: the hand you use has just
// taken the lead - worked out again from there (again: the hilt as it was
// found this frame - the saber now hung on that hand can't be found on it
// till the skeleton's worked out afresh, and it's the same hilt).
static qboolean CG_VRB_TwoHandHold( centity_t *cent, const vrClientState_t *vr, qboolean was, const qboolean squeezed[2],
	float dt, qboolean again ) {
	const clientInfo_t *ci = &cgs.clientinfo[cent->currentState.number];
	const int main = ( vrb.hold[0].hand == VR_HAND_LEFT ) ? VR_HAND_LEFT : VR_HAND_RIGHT, off = main ^ 1;	// (holsters:
																					// none in hand - the grip check below)
	const qboolean gripHolds = CG_VRH_GripHolds();
	qboolean lent, pulled = qfalse, gave = qfalse, untracked = qfalse;
	vrbGrip_t gripM, gripO;
	vrbBone_t tag;
	mdxaBone_t m;
	vec3_t posM, axM[3], fistM, posO, axO[3], dirO, blade, bladeNow, emitter, avg, tmp;
	float lo, hi, tAbove[2], tBelow[2], dAbove, dBelow;
	qboolean canAbove, canBelow;
	int i;

	vrb.twoHand = qfalse;
	vrb.twoHandArms = qfalse;
	vrb.grabZones = 0;
	VectorClear( avg );
	if ( vrb.twoHandOff != off ) {
		// (the hands changed over - passed, or left-handed now: nothing carries on)
		vrb.twoHandOff = off;
		vrb.twoHandBlend = 0.0f;
		was = qfalse;
	}
	lent = vrb.twoHandLent;

	// the first saber in one hand, the other hand empty, both tracked
	if ( !vr->handValid[off] || !vr->handValid[main] ) {
		untracked = qtrue;	// (lent, the hold goes on as they're back)
		goto off;
	}
	if ( !cg_vrBodyArms.integer || !cg_vrTwoHand.integer ) {
		goto off;
	}
	CG_VRB_Grip( cent, main, &gripM );
	CG_VRB_Grip( cent, off, &gripO );
	if ( gripM.kind != VRB_GRIP_SABER || gripM.saber != 0 || gripO.kind != VRB_GRIP_FREE ) {
		goto off;
	}

	if ( again ) {
		lo = vrb.twoHandHilt[0];
		hi = vrb.twoHandHilt[1];
	} else {
		// the hilt as the hand holds it: where its emitters are along it from
		// where its own tag holds it, and how the blade sits on the hand's tag
		// (a saber bolted to the other hand sits on its tag the same way)
		if ( !CG_VRB_Bone( cent, vrbArmBones[main][VRB_TAG], 1, &tag ) ||
			!trap->G2API_GetBoltMatrix( cent->ghoul2, gripM.saberModel, 0, &m, cent->turAngles, cent->lerpOrigin, cg.time,
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

		// Where on the hilt hands can hold it, along it from where its tag
		// holds it (the way its first blade points): up to a hand's width
		// short of the emitter; down to the pommel - a single hilt's isn't
		// known: a hand's length or two - or down a staff to its other emitter.
		VectorSubtract( emitter, tag.origin, tmp );
		hi = DotProduct( tmp, bladeNow ) - VRB_HILT_TOP;
		lo = -VRB_HILT_BELOW;
		if ( ci->saber[0].numBlades > 1 && trap->G2API_GetBoltMatrix( cent->ghoul2, gripM.saberModel, 1, &m,
				cent->turAngles, cent->lerpOrigin, cg.time, cgs.gameModels, cent->modelScale ) ) {
			vec3_t blade2;
			BG_GiveMeVectorFromMatrix( &m, ORIGIN, emitter );
			BG_GiveMeVectorFromMatrix( &m, NEGATIVE_Y, blade2 );
			VectorSubtract( emitter, tag.origin, tmp );
			if ( VectorNormalize( blade2 ) > 0.5f && DotProduct( blade2, bladeNow ) < -0.5f &&
				DotProduct( tmp, bladeNow ) < 0.0f ) {
				// a staff (its second blade points back past the hand): the
				// hilt runs down to that emitter
				lo = DotProduct( tmp, bladeNow ) + VRB_HILT_TOP;
			}
		}
		vrb.twoHandHilt[0] = lo;
		vrb.twoHandHilt[1] = hi;
	}
	// The other hand a hand's width from the first, no more than 60 above or
	// below it. A stretch too short for a hand isn't offered. (The first hand
	// on it too - where the saber's tag holds it always is.)
	gripM.hilt = vrb.hold[0].t = Com_Clamp( Q_min( lo, 0.0f ), Q_max( hi, 0.0f ), vrb.hold[0].t );
	tAbove[0] = VRB_HAND_GAP;
	tAbove[1] = Q_min( VRB_HAND_SPAN, hi - gripM.hilt );
	tBelow[0] = Q_max( -VRB_HAND_SPAN, lo - gripM.hilt );
	tBelow[1] = -VRB_HAND_GAP;
	canAbove = (qboolean)( tAbove[1] >= tAbove[0] );
	canBelow = (qboolean)( tBelow[1] >= tBelow[0] );
	if ( !canAbove && !canBelow ) {
		goto off;
	}

	// both controllers: the hilt's way (toward the first blade, as it lies in
	// the first hand - through its fist, as that's drawn), and the other's fist
	CG_VRB_Controller( vr, main, posM, axM );
	CG_VRB_FistAt( main, posM, axM, fistM );
	CG_VRB_Controller( vr, off, posO, axO );
	CG_VRB_HeldBladeDir( &gripM, axM, blade );
	CG_VRB_SaberDir( axO, dirO );
	VectorCopy( posO, vrb.twoHandGrip2 );
	dAbove = canAbove ? CG_VRB_RangeDistance( posO, fistM, blade, tAbove ) : 1e9f;
	dBelow = canBelow ? CG_VRB_RangeDistance( posO, fistM, blade, tBelow ) : 1e9f;

	// The other hand holds it while its grip's held - or, the saber lent to
	// the hand you use, without, till it squeezes it: holding the grip, its
	// grip holds it from then on; not, it lets go (the saber's the hand you
	// use's now).
	if ( lent && squeezed[off] ) {
		vrb.twoHandLent = qfalse;
		if ( !gripHolds ) {
			vrb.grabBlocked[off] = qtrue;	// (till its grip's let go)
			gave = qtrue;
		}
	}
	if ( vrb.twoHandLent || ( vr->grabbing[off] && !vrb.grabBlocked[off] ) ) {
		// Holding it with both hands. The blade follows both wrists, and the
		// further apart the hands, the more the line through them: one hand
		// pushing and the other pulling levers it round.
		vec3_t line, dir, point;
		float sep, agree, w, t;

		if ( !was ) {
			// a new hold: which way round the other hand is on the hilt, and
			// on which side of the first one (the stretch it is nearer)
			vrb.twoHandPass = qfalse;	// (none waiting from before - let go of, or not drawn a while; a squeeze this frame
										// still passes it, once settled)
			vrb.twoHandSign = ( DotProduct( blade, dirO ) < 0.0f ) ? -1.0f : 1.0f;
			vrb.twoHandSide = ( dAbove < dBelow ) ? 1.0f : -1.0f;
			vrb.twoHandOffKept = qfalse;
		}

		// the wrists: the first one, and the other as far as it agrees (along
		// its fist, the way round it holds it; holding it as it held it first)
		if ( vrb.twoHandOffKept ) {
			VRB_FromFrame( axO, vrb.twoHandOffDir, tmp );
			VectorNormalize( tmp );
		} else {
			VectorScale( dirO, vrb.twoHandSign, tmp );
		}
		VectorMA( blade, VRB_SmoothStep( -0.3f, 0.7f, DotProduct( blade, tmp ) ), tmp, avg );
		if ( VectorNormalize( avg ) < 0.5f ) {
			VectorCopy( blade, avg );
		}

		// the line through the hands (their controllers: either way round the
		// same), as far as it runs along the hilt
		VectorSubtract( posM, posO, line );
		sep = VectorNormalize( line );
		agree = DotProduct( line, avg );
		if ( agree < 0.0f ) {
			VectorScale( line, -1.0f, line );	// (the other hand nearer the blade)
			agree = -agree;
		}
		w = Com_Clamp( 0.0f, 1.0f, cg_vrTwoHandLever.value ) * VRB_SmoothStep( 2.0f, 8.0f, sep ) *
			VRB_SmoothStep( 0.3f, 0.7f, agree );
		VectorScale( avg, 1.0f - w, dir );
		VectorMA( dir, w, line, dir );
		if ( VectorNormalize( dir ) < 0.1f ) {
			VectorCopy( avg, dir );
		}

		// The other hand grips the hilt where it took hold of it: it doesn't
		// slide along it or turn round it after your hand (let go and take
		// hold again to move it).
		if ( !was ) {
			vec3_t e[3];
			int k;
			VectorSubtract( posO, fistM, tmp );
			vrb.twoHandT = DotProduct( tmp, dir );
			CG_VRB_HiltFrame( &gripM, axM, dir, e );
			for ( i = 0; i < 3; i++ ) {
				for ( k = 0; k < 3; k++ ) {
					vrb.twoHandGrip[i][k] = DotProduct( axO[i], e[k] );
				}
			}
		}
		if ( !( vrb.twoHandSide > 0.0f ? canAbove : canBelow ) ) {
			vrb.twoHandSide = -vrb.twoHandSide;	// (the hilt changed under it)
		}
		t = ( vrb.twoHandSide > 0.0f ) ? Com_Clamp( tAbove[0], tAbove[1], vrb.twoHandT ) :
			Com_Clamp( tBelow[0], tBelow[1], vrb.twoHandT );
		vrb.twoHandT = t;
		VectorMA( fistM, t, dir, point );

		if ( Distance( posO, point ) > VRB_GRAB_BREAK && !( vrb.twoHandOffKept && CG_VRB_PassEase( 0 ) < 1.0f ) ) {
			// pulled well away: it lets go (until the grip is let go and squeezed again) - not as the fist of the hand
			// you use settles onto its controller, the lead just taken (the hilt it took over from where it was drawn,
			// trailing the hands with the blade's weight: CG_VRB_TakeLead)
			vrb.grabBlocked[off] = qtrue;
			pulled = qtrue;
		} else {
			VectorCopy( dir, vrb.twoHandDir );
			VectorCopy( point, vrb.twoHandPoint );
			VectorSubtract( point, fistM, tmp );
			for ( i = 0; i < 3; i++ ) {
				vrb.twoHandRel[0][i] = DotProduct( dir, axM[i] );
				vrb.twoHandRel[1][i] = DotProduct( tmp, axM[i] );
			}
			vrb.twoHand = qtrue;
		}
	}

	if ( vrb.twoHand ) {
		// Passing it over: held with both hands, a new squeeze of the first
		// hand's grip lets go with that hand, and the other goes on holding it
		// - once it's settled onto the hilt (squeezed as it takes hold, it
		// waits the moment that takes: the hand goes over holding the hilt as
		// it's drawn, nothing jumps). Holding the grip (cg_vrholster.c), the
		// first hand's squeeze holds it, and letting go passes it
		// (CG_VR_PassSaberOver). Lent to the hand you use, letting go of its
		// grip gives it back (VRB_LET_GO_MS on end, as holding the grip lets go
		// - a moment's slack doesn't, nor is it squeezed anew to pass it over),
		// only that hand feeling it.
		if ( squeezed[main] && !vrb.grabBlocked[main] && !gripHolds && !vrb.twoHandLent ) {
			vrb.twoHandPass = qtrue;
		}
		if ( !vrb.twoHandLent || vr->grabbing[main] ) {
			vrb.twoHandSlack = 0;
		} else if ( !vrb.twoHandSlack ) {
			vrb.twoHandSlack = cg.time ? cg.time : 1;
		}
		if ( vrb.twoHandBlend >= 1.0f ) {
			if ( vrb.twoHandSlack && !( cg.time - vrb.twoHandSlack >= 0 && cg.time - vrb.twoHandSlack < VRB_LET_GO_MS ) ) {
				CG_VRB_PassSaber( cent, vr, main, off, qtrue );
				return qfalse;
			}
			if ( vrb.twoHandPass ) {
				CG_VRB_PassSaber( cent, vr, main, off, qfalse );
				return qfalse;
			}
			if ( main != CG_VR_GunHand() && ( !gripHolds || vr->grabbing[main] || !CG_VRH_GripHeld( main ) ) ) {
				// settled, both hands holding it: the hand you use takes the lead
				// (holding the grip, not as your other hand lets go of it - that
				// passes it over; a saber that came to it any other way, lent)
				CG_VRB_TakeLead( cent, vr, main, off, avg, (qboolean)( !gripHolds || !vr->grabbing[main] ) );
				return qtrue;
			}
		}
		vrb.grabZones |= 1 << main;	// (its grip squeezed now passes it over)
		if ( vrb.twoHandLent ) {
			vrb.grabZones |= 1 << off;	// (its squeeze takes hold, or lets go: not Use or the Force)
		}
	} else {
		// not holding it: could the other hand take hold of the hilt?
		if ( !vr->grabbing[off] && Q_min( dAbove, dBelow ) <= VRB_GRAB_REACH ) {
			vrb.grabZones |= 1 << off;
		}
		// just let go: the hold as it was, carried along by the first hand
		if ( vrb.twoHandBlend > 0.0f ) {
			VRB_FromFrame( axM, vrb.twoHandRel[0], vrb.twoHandDir );
			if ( VectorNormalize( vrb.twoHandDir ) < 0.5f ) {
				VectorCopy( blade, vrb.twoHandDir );
			}
			VRB_FromFrame( axM, vrb.twoHandRel[1], tmp );
			VectorAdd( fistM, tmp, vrb.twoHandPoint );
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
		VectorScale( posO, 1.0f - vrb.twoHandEase, vrb.twoHandHand );
		VectorMA( vrb.twoHandHand, vrb.twoHandEase, vrb.twoHandPoint, vrb.twoHandHand );
		// the hand as it lies round the hilt, the controller eased onto that
		CG_VRB_HiltFrame( &gripM, axM, vrb.twoHandDir, e );
		for ( i = 0; i < 3; i++ ) {
			VectorClear( held[i] );
			for ( k = 0; k < 3; k++ ) {
				VectorMA( held[i], vrb.twoHandGrip[i][k], e[k], held[i] );
			}
		}
		VRB_FrameBlend( axO, held, vrb.twoHandEase, vrb.twoHandAxis );
		vrb.twoHandArms = qtrue;
	}
	goto done;

off:
	vrb.twoHandBlend = 0.0f;
done:
	if ( vrb.twoHand ) {
		vrb.twoHandHeldTime = cg.time;
		if ( !was ) {
			CG_VRB_Feel( off, 0.6f, 40, 0.0f, 0, 0.0f );	// took hold
			CG_VRB_Log( "two hands: the %s hand took hold %s the %s (%.1f along the hilt)%s\n", VRB_HandName( off ),
				vrb.twoHandSide > 0.0f ? "above" : "below", VRB_HandName( main ), vrb.twoHandT,
				vrb.twoHandSign < 0.0f ? ", reversed" : "" );
		}
		return qfalse;
	}
	if ( !untracked || cg.time - vrb.twoHandHeldTime < 0 || cg.time - vrb.twoHandHeldTime > VRB_TWO_HAND_GAP ) {
		vrb.twoHandLent = qfalse;	// (the hold over - a hand's tracking lost a moment, it goes on as it's back)
	}
	if ( was ) {
		CG_VRB_Feel( off, 0.3f, 20, 0.0f, 0, 0.0f );	// let go
		CG_VRB_Log( "two hands: %s\n", pulled ? "pulled off the hilt" : "let go" );
		if ( lent && !gripHolds && ( gave || pulled ) ) {
			// (lent to the hand you use, your other hand let go of it: the hand you use's now)
			CG_VRB_Feel( main, 0.5f, 35, 0.0f, 0, 0.0f );
			CG_VRB_Log( "two hands: the saber's the %s hand's now\n", VRB_HandName( main ) );
		}
	}
	return qfalse;
}

static void CG_VRB_TwoHand( centity_t *cent, const vrClientState_t *vr ) {
	static const qboolean none[2] = { qfalse, qfalse };
	const float dt = Com_Clamp( 0.0f, 0.1f, ( cg.time - vrb.twoHandTime ) * 0.001f );
	qboolean was = vrb.twoHand, squeezed[2];
	int h;

	if ( vrb.twoHandTime == cg.time ) {
		return;	// (second eye)
	}
	if ( cg.time - vrb.twoHandTime < 0 || cg.time - vrb.twoHandTime > VRB_TWO_HAND_GAP ) {
		// (not worked out for a while - your body not drawn: dead, knocked
		// down, a menu - nothing carries on from then)
		was = qfalse;
		vrb.twoHandBlend = 0.0f;
		vrb.twoHandLent = qfalse;
	}
	vrb.twoHandTime = cg.time;
	for ( h = 0; h < 2; h++ ) {
		squeezed[h] = (qboolean)( vr->grabbing[h] && !vrb.grabWas[h] );	// (a new squeeze, taken as a grab)
		vrb.grabWas[h] = vr->grabbing[h];
		if ( !vr->grabbing[h] ) {
			vrb.grabBlocked[h] = qfalse;	// let go of the grip: it can take hold again
		}
	}
	if ( CG_VRB_TwoHandHold( cent, vr, was, squeezed, dt, qfalse ) ) {
		CG_VRB_TwoHandHold( cent, vr, qtrue, none, dt, qtrue );	// (the hand you use took the lead: the hold goes on from there)
	}
}

/*
=================
CG_VRB_BladeInertia

The blade in your hand - and the hand with it (the arm turns your fist to
hold it, so the hilt never leaves the fist). It has a top speed and takes a
moment to get going and to turn back, as heavy as it feels (bg_vrsaber.h):
the blue style's hardly (2000 degrees a second), yellow's more (620), the
staff's more again (540), red's most (490); two sabers each 750 - swing
faster than that and the blade, and the fist holding it, fall behind the
controller (you feel them strain, CG_VRB_Heft), catching up as you slow.
Slow, careful moves stay exact, and it never carries on past where your
hand stops; only turning it back fast, its weight carries it on a little
first. Two hands on it turn it faster. It runs in your room, so turning
and walking never swing it. cg_vrSaberInertia scales all of it (1; 0 = the
blade exactly where the controller points it; up to 3: slower still).
Another blade stops it dead for a moment, and cutting through someone drags
it (CG_VRB_BladeImpulse) - and it can't go through another blade, it rests
on it (CG_VRB_BladeSolid).
=================
*/

#define VRB_BLADE_STEP		0.002f	// s: integration step
#define VRB_BLADE_FOLLOW	0.006f	// s: how closely it keeps to a hand turning slower than it can
#define VRB_BLADE_STRAIN	25.0f	// falling well behind, it's dragged faster by this much (rad/s per rad)
#define VRB_BLADE_TWOHANDS	1.2f	// two hands on it turn it this much faster

// by feel - blue, yellow, red, the staff, two sabers (and how fast it turns at
// most: BG_VRFeelTurn)
static const float	vrbBladeMaxAccel[VR_NUM_FEELS] = { 120000.0f, 11500.0f, 8700.0f, 9000.0f, 14000.0f };	// deg/s/s it gets going (or turns back) at most
static const float	vrbBladeMaxLag[VR_NUM_FEELS] = { 25.0f, 55.0f, 63.0f, 60.0f, 50.0f };	// degrees behind the hand at most
static const int	vrbBladeHold[VR_NUM_FEELS] = { 35, 60, 75, 68, 60 };					// ms another blade stops it for

// world yaw minus room (tracking) yaw
static float CG_VRB_RoomYaw( const vrClientState_t *vr ) {
	return VR_AxisYaw( cg.refdef.viewaxis ) - vr->referenceYaw;
}

static void CG_VRB_BladeSpring( int h, const vrClientState_t *vr, const vec3_t want, int feel, qboolean twoHands,
	vec3_t out ) {
	const float yaw = CG_VRB_RoomYaw( vr );
	const float inertia = Com_Clamp( 0.0f, 3.0f, cg_vrSaberInertia.value );
	const float dt = ( cg.time - vrb.blade[h].time ) * 0.001f;
	vec3_t target, axis;

	VRB_RotZ( want, -yaw, target );
	if ( VectorNormalize( target ) < 0.5f ) {
		VectorCopy( want, out );
		return;
	}
	if ( !vrb.blade[h].valid || dt < 0.0f || dt > 0.2f ) {
		VectorCopy( target, vrb.blade[h].dir );
		VectorCopy( target, vrb.blade[h].hand );
		VectorClear( vrb.blade[h].vel );
		vrb.blade[h].behind = 0.0f;
		vrb.blade[h].holdUntil = 0;
		vrb.blade[h].valid = qtrue;
	} else if ( dt > 0.0f ) {
		const float maxLag = DEG2RAD( vrbBladeMaxLag[feel] );
		float maxRate, maxAccel, handRate, ang;
		vec3_t handVel;
		int n, k;

		if ( inertia > 0.0f ) {
			maxRate = DEG2RAD( BG_VRFeelTurn( feel ) ) / inertia;
			maxAccel = DEG2RAD( vrbBladeMaxAccel[feel] ) / inertia;
		} else {
			maxRate = maxAccel = 1.0e5f;	// as the controller points it: only clashes and cuts show
		}
		if ( twoHands ) {
			maxRate *= VRB_BLADE_TWOHANDS;
			maxAccel *= VRB_BLADE_TWOHANDS;
		}

		// how the hand turned it since the last frame (steadily, through it)
		CrossProduct( vrb.blade[h].hand, target, handVel );
		handRate = atan2f( VectorNormalize( handVel ), DotProduct( vrb.blade[h].hand, target ) ) / dt;
		VectorScale( handVel, handRate, handVel );

		if ( cg.time - vrb.blade[h].holdUntil < 0 ) {
			VectorClear( vrb.blade[h].vel );	// stopped on the other blade
		} else {
			n = Q_max( 1, (int)ceilf( dt / VRB_BLADE_STEP ) );
			for ( k = 1; k <= n; k++ ) {
				const float step = dt / n;
				vec3_t tgt, wWant, dw;
				float e, close, lim, rate, d, accel, handOn, closing;

				// (the gap to where the hand is as this step begins: going with
				// the hand through the step, closing it, it ends where the hand
				// ends - not a step ahead)
				VRB_Slerp( vrb.blade[h].hand, target, (float)( k - 1 ) / n, tgt );
				CrossProduct( vrb.blade[h].dir, tgt, axis );
				e = atan2f( VectorNormalize( axis ), DotProduct( vrb.blade[h].dir, tgt ) );
				// keep up with the hand and close the gap - at once while it's
				// small, and no faster than lets it stop where the hand is
				// (braking a little short of as hard as it can)...
				close = Q_min( e / VRB_BLADE_FOLLOW, sqrtf( 1.6f * maxAccel * e ) );
				VectorMA( handVel, close, axis, wWant );
				// ...no faster than a blade this heavy turns (dragged faster as
				// it falls far behind)...
				lim = maxRate + VRB_BLADE_STRAIN * Q_max( 0.0f, e - 0.6f * maxLag );
				rate = VectorLength( wWant );
				if ( rate > lim ) {
					VectorScale( wWant, lim / rate, wWant );
				}
				// ...and getting to that speed no quicker than it can - but
				// slowing as hard as it takes not to carry on past a hand
				// that has stopped, or goes on (dragged along faster than it
				// turns, it can be going faster than its weight would stop it
				// in time); a hand turned back meets it, and it carries on a
				// little past where the hand turned
				VectorSubtract( wWant, vrb.blade[h].vel, dw );
				d = VectorLength( dw );
				accel = maxAccel;
				handOn = DotProduct( handVel, axis );
				closing = DotProduct( vrb.blade[h].vel, axis ) - handOn;
				if ( handOn >= 0.0f && closing > 0.0f && e > 1e-4f ) {
					accel = Q_max( accel, closing * closing / ( 2.0f * e ) );
				}
				if ( d > accel * step ) {
					VectorMA( vrb.blade[h].vel, accel * step / d, dw, vrb.blade[h].vel );
				} else {
					VectorCopy( wWant, vrb.blade[h].vel );
				}
				VectorMA( vrb.blade[h].vel, -DotProduct( vrb.blade[h].vel, vrb.blade[h].dir ), vrb.blade[h].dir,
					vrb.blade[h].vel );	// (it doesn't spin about itself)
				rate = VectorLength( vrb.blade[h].vel );
				if ( rate > 1e-5f ) {
					vec3_t kAxis, was, ahead, tgtEnd;
					VectorScale( vrb.blade[h].vel, 1.0f / rate, kAxis );
					VectorCopy( vrb.blade[h].dir, was );
					VRB_Rotate( vrb.blade[h].dir, kAxis, rate * step, vrb.blade[h].dir );
					VectorNormalize( vrb.blade[h].dir );
					// ...and whatever it weighs, it never goes on past a hand
					// that's stopped, or going its way: having caught it up,
					// it goes with it (caught up: the hand was within this
					// step's reach - not off to the side of its way, as when
					// the hand turns a corner, where it curves round to it)
					VRB_Slerp( vrb.blade[h].hand, target, (float)k / n, tgtEnd );
					CrossProduct( kAxis, was, ahead );	// (which way it went)
					if ( DotProduct( handVel, vrb.blade[h].vel ) >= 0.0f && DotProduct( ahead, tgtEnd ) > -1e-4f &&
						DotProduct( was, tgtEnd ) >= cosf( rate * step ) - 1e-5f ) {
						CrossProduct( kAxis, vrb.blade[h].dir, ahead );
						if ( DotProduct( ahead, tgtEnd ) < -1e-4f ) {
							VectorCopy( tgtEnd, vrb.blade[h].dir );
							VectorCopy( handVel, vrb.blade[h].vel );
						}
					}
				}
			}
		}

		// never further behind than this: past it the hand drags it along
		CrossProduct( vrb.blade[h].dir, target, axis );
		ang = atan2f( VectorNormalize( axis ), DotProduct( vrb.blade[h].dir, target ) );
		if ( ang > maxLag ) {
			VRB_Rotate( vrb.blade[h].dir, axis, ang - maxLag, vrb.blade[h].dir );
			VectorNormalize( vrb.blade[h].dir );
			ang = maxLag;
		}
		vrb.blade[h].behind = ( inertia > 0.0f ) ? ang / maxLag : 0.0f;
		vrb.swing.lag = Q_max( vrb.swing.lag, RAD2DEG( ang ) );	// (for the log)
		VectorCopy( target, vrb.blade[h].hand );
	}
	vrb.blade[h].time = cg.time;
	VRB_RotZ( vrb.blade[h].dir, yaw, out );
}

static void CG_VRB_BladeInertia( centity_t *cent, const vrClientState_t *vr ) {
	const qboolean twoHands = (qboolean)( vrb.twoHand && vrb.twoHandTime == cg.time );
	const int feel = CG_VRB_BladeFeel( qfalse );
	const int main = CG_VRB_SaberHand( 0 );
	int h;

	if ( vrb.bladeTime == cg.time ) {
		return;	// (second eye)
	}
	vrb.bladeTime = cg.time;
	VectorCopy( vrb.twoHandDir, vrb.twoHandFist );

	for ( h = 0; h < 2; h++ ) {
		vrbGrip_t grip;
		vec3_t pos, ax[3], want, out, fist;
		float ease;

		vrb.blade[h].outTime = 0;
		CG_VRB_Grip( cent, h, &grip );
		if ( grip.kind != VRB_GRIP_SABER || !vr->handValid[h] ) {
			vrb.blade[h].valid = qfalse;
			vrb.blade[h].lastTime = 0;
			continue;
		}
		CG_VRB_Controller( vr, h, pos, ax );
		// the hold on the hilt it turns about: the controller's grip - just
		// passed into this hand, the fist on its way there from where it held
		// the hilt (as CG_VRB_SolveArm draws it)
		VectorCopy( pos, fist );
		ease = CG_VRB_PassEase( grip.saber );
		if ( ease < 1.0f ) {
			vec3_t tmp;
			VRB_FromFrame( ax, vrb.hold[grip.saber].passOfs, tmp );
			VectorMA( fist, 1.0f - ease, tmp, fist );
		}
		CG_VRB_HeldBladeDir( &grip, ax, want );
		if ( h == main && vrb.twoHandArms && vrb.twoHandTime == cg.time ) {
			vec3_t tmp;
			VRB_Slerp( want, vrb.twoHandDir, vrb.twoHandEase, tmp );	// (held with both hands)
			VectorCopy( tmp, want );
		}
		CG_VRB_BladeSpring( h, vr, want, feel, (qboolean)( twoHands && h == main ), out );
		CG_VRB_BladeSolid( h, vr, fist, want, out );	// (another blade in the way stops it)
		VectorCopy( out, vrb.blade[h].out );
		vrb.blade[h].outTime = cg.time;

		if ( h == main && vrb.twoHandArms && vrb.twoHandTime == cg.time ) {
			// the other hand holds the hilt where it is drawn (turned with it
			// about the fist holding it)
			vrbMat_t R;
			vec3_t rel;
			VRB_Arc( want, out, R );
			VectorSubtract( vrb.twoHandHand, fist, rel );
			VRB_Apply( R, rel, rel );
			VectorAdd( fist, rel, vrb.twoHandHand );
			VRB_Apply( R, vrb.twoHandDir, vrb.twoHandFist );
		}
	}
}

// Your blade met another one (clash) or cut through someone (strength 0..1).
static void CG_VRB_BladeImpulse( int h, qboolean clash, float strength ) {
	const int feel = CG_VRB_BladeFeel( qfalse );

	if ( h < 0 || h > 1 || !vrb.blade[h].valid ) {
		return;
	}
	strength = Com_Clamp( 0.0f, 1.0f, strength );
	if ( clash ) {
		vrb.blade[h].holdUntil = cg.time + (int)( vrbBladeHold[feel] * ( 0.5f + 0.5f * strength ) );
		VectorClear( vrb.blade[h].vel );
	} else {
		VectorScale( vrb.blade[h].vel, 1.0f - 0.6f * strength, vrb.blade[h].vel );
	}
}

/*
=============================================================================

where the elbows and shoulders go

The hand is placed exactly; the elbow can be anywhere on a circle round the
line from the shoulder to the wrist. A real one goes where holding the hand
like that is easiest: it hangs (its weight), a raised arm goes up out to the
side and a little forward (the shoulder blade's plane), it keeps out of the
body, and it goes no further than the wrist and forearm can't take up: a
wrist bends only so far each way, a forearm turns palm up or down only so
far. So the elbow follows your hand's turn when it has to and hangs when it
doesn't - your real hand's turn, on the controller (a saber passed into it:
the hand lying on the controller as it lay on the hilt), not the drawn one
(a heavy blade lagging behind it, or the other hand levering it, turns that
one, and that's the wrist's to take up, not the elbow's).

The shoulder girdle helps too: the collarbone lifts the shoulder as the hand
goes up and brings it forward as the hand reaches out or across.

=============================================================================
*/

// the chest the arms hang from
typedef struct {
	vec3_t		F, L, U;			// forward, left, up
	vec3_t		spine;				// the spine at the chest (thoracic bone)
	float		scale;				// this body's size against the humanoid's
} vrbChest_t;

#define VRB_SHOULDER_WIDTH	11.88f	// the humanoid's: shoulder joint to shoulder joint
#define VRB_TORSO_BOTTOM	-17.0f	// its torso from this far below the thoracic bone...
#define VRB_TORSO_TOP		2.0f	// ...to this far above it,
#define VRB_TORSO_FWD		0.3f	// round it a little forward of the spine,
#define VRB_TORSO_DEPTH		6.3f	// this far to the front and back (with an arm's thickness)
#define VRB_TORSO_WIDTH		7.3f	// and to the sides

#define VRB_ELBOW_SAMPLES	48		// places round the circle the elbow is tried at
#define VRB_ELBOW_EASE		0.05f	// seconds an elbow takes to settle where it's going...
#define VRB_ELBOW_RATE		900.0f	// ...no faster than this round the circle (deg/s)
#define VRB_TWIST_SHARE		0.5f	// the forearm's twist bone takes this much of the hand's twist
#define VRB_GIRDLE_EASE		0.08f	// seconds the shoulders take to follow

// what makes holding the hand somewhere hard (per arm length, per radian)
static const struct {
	float	grav;					// the elbow up from hanging
	float	across, acrossFrom;		// the upper arm pointing in across the chest, past this
	float	plane, planeAz;			// a raised upper arm out of the shoulder blade's plane (deg forward of out)
	float	back, backFrom;			// the elbow far behind the body
	float	body;					// the elbow or forearm inside the torso
	float	wrist, wrist0;			// the wrist bent past its easy range; any bend
	float	twist, twist0;			// the forearm turned past its easy range; any turn
	float	still;					// the elbow moving from where it was (per arm length, squared)
	float	flex, ext, rad, uln;	// the wrist's easy range (deg): to the palm, the back, the thumb, the little finger
	float	pron, sup;				// the forearm's (deg): palm down, palm up
} vrbComfort = {
	1.0f,
	1.0f, 0.35f,
	0.6f, 35.0f,
	3.0f, 0.35f,
	40.0f,
	4.0f, 0.6f,
	3.0f, 0.1f,
	2.0f,
	55.0f, 45.0f, 15.0f, 30.0f,
	60.0f, 70.0f
};

// the shoulder girdle: lifted up to this as the hand goes up, forward as it
// reaches out or across, back as it goes behind (degrees)
#define VRB_GIRDLE_UP		20.0f
#define VRB_GIRDLE_REACH	14.0f
#define VRB_GIRDLE_ACROSS	12.0f
#define VRB_GIRDLE_BACK		12.0f

static void VRB_ToChest( const vrbChest_t *ch, const vec3_t w, vec3_t out ) {
	vec3_t t;
	t[0] = DotProduct( w, ch->F );
	t[1] = DotProduct( w, ch->L );
	t[2] = DotProduct( w, ch->U );
	VectorCopy( t, out );
}

static void VRB_FromChest( const vrbChest_t *ch, const vec3_t c, vec3_t out ) {
	vec3_t t;
	VectorScale( ch->F, c[0], t );
	VectorMA( t, c[1], ch->L, t );
	VectorMA( t, c[2], ch->U, out );
}

// The chest, from the skeleton as animated: across from shoulder to
// shoulder, up along the spine. (Without them: the way the body faces.)
static void CG_VRB_Chest( centity_t *cent, float bodyYaw, vrbChest_t *ch ) {
	vrbBone_t l, r, t, c;
	vec3_t lat, up;
	float width;

	VectorSet( ch->F, cosf( DEG2RAD( bodyYaw ) ), sinf( DEG2RAD( bodyYaw ) ), 0.0f );
	VectorSet( ch->L, -ch->F[1], ch->F[0], 0.0f );
	VectorSet( ch->U, 0.0f, 0.0f, 1.0f );
	VectorCopy( cent->lerpOrigin, ch->spine );
	ch->scale = 1.0f;
	if ( !CG_VRB_Bone( cent, vrbArmBones[VR_HAND_LEFT][VRB_UPPER], 1, &l ) ||
		!CG_VRB_Bone( cent, vrbArmBones[VR_HAND_RIGHT][VRB_UPPER], 1, &r ) ) {
		return;
	}
	VectorSubtract( l.origin, r.origin, lat );
	width = VectorNormalize( lat );
	if ( width < 1.0f ) {
		return;
	}
	ch->scale = Com_Clamp( 0.5f, 2.0f, width / VRB_SHOULDER_WIDTH );
	if ( CG_VRB_Bone( cent, "thoracic", 1, &t ) && CG_VRB_Bone( cent, "cervical", 1, &c ) ) {
		VectorSubtract( c.origin, t.origin, up );
		VectorCopy( t.origin, ch->spine );
	} else {
		VectorSet( up, 0.0f, 0.0f, 1.0f );
		VectorAdd( l.origin, r.origin, ch->spine );
		VectorScale( ch->spine, 0.5f, ch->spine );
		ch->spine[2] -= 3.0f * ch->scale;
	}
	VectorMA( up, -DotProduct( up, lat ), lat, up );
	if ( VectorNormalize( up ) < 0.1f ) {
		return;
	}
	VectorCopy( lat, ch->L );
	VectorCopy( up, ch->U );
	CrossProduct( ch->L, ch->U, ch->F );
}

// how far inside the torso p is (0 outside, up to 1 at its middle)
static float VRB_TorsoInside( const vrbChest_t *ch, const vec3_t p ) {
	vec3_t d;
	float x, y, z, e;

	VectorSubtract( p, ch->spine, d );
	z = DotProduct( d, ch->U ) / ch->scale;
	if ( z < VRB_TORSO_BOTTOM - 2.0f || z > VRB_TORSO_TOP + 2.0f ) {
		return 0.0f;
	}
	x = ( DotProduct( d, ch->F ) / ch->scale - VRB_TORSO_FWD ) / VRB_TORSO_DEPTH;
	y = DotProduct( d, ch->L ) / ch->scale / VRB_TORSO_WIDTH;
	e = sqrtf( x * x + y * y );
	if ( e >= 1.0f ) {
		return 0.0f;
	}
	return ( 1.0f - e ) * VRB_SmoothStep( VRB_TORSO_BOTTOM - 2.0f, VRB_TORSO_BOTTOM, z ) *
		( 1.0f - VRB_SmoothStep( VRB_TORSO_TOP, VRB_TORSO_TOP + 2.0f, z ) );
}

// The wrist's bend: the hand (knuckles, toward the thumb, [0] x [1]) against
// the forearm running f: toward the palm (+) and toward the thumb (+), radians.
static void VRB_WristBend( int h, const vec3_t f, vec3_t hand[3], float *flex, float *radial ) {
	const float x = DotProduct( f, hand[0] );
	// ([2] comes out of the back of a right hand, the palm of a left one)
	const float palm = ( h == VR_HAND_RIGHT ) ? DotProduct( f, hand[2] ) : -DotProduct( f, hand[2] );

	*flex = atan2f( palm, x );
	*radial = atan2f( -DotProduct( f, hand[1] ), x );
}

// How far the forearm (upper arm u, forearm f) is turned palm down (+) or up
// (-) from its middle, where the thumb points away from the upper arm - up,
// with the upper arm hanging and the forearm forward. Radians.
static float VRB_Pronation( int h, const vec3_t u, const vec3_t f, const vec3_t thumb ) {
	vec3_t tn, ta, c;
	float ang;

	VectorMA( u, -DotProduct( u, f ), f, tn );
	VectorScale( tn, -1.0f, tn );
	VectorMA( thumb, -DotProduct( thumb, f ), f, ta );
	if ( VectorNormalize( tn ) < 1e-4f || VectorNormalize( ta ) < 1e-4f ) {
		return 0.0f;
	}
	CrossProduct( tn, ta, c );
	ang = atan2f( DotProduct( c, f ), DotProduct( tn, ta ) );
	return ( h == VR_HAND_RIGHT ) ? -ang : ang;	// (palm down turns a right thumb in, a left one the other way)
}

static float VRB_Past( float x, float below, float above ) {
	if ( x > above ) {
		return x - above;
	}
	if ( x < -below ) {
		return x + below;
	}
	return 0.0f;
}

typedef struct {
	int					h;
	const vrbChest_t	*ch;
	vec3_t				S, Wt;		// shoulder, wrist
	vec3_t				out;		// outward, this arm's side
	vec3_t				hand[3];	// your hand as it really is (VRB_WristBend)
	qboolean			havePrev;
	vec3_t				prevE;		// where the elbow was last frame
	float				a;			// upper arm length
} vrbElbowJob_t;

// How hard it is to hold the hand with the elbow at E.
static float CG_VRB_ElbowCost( const vrbElbowJob_t *j, const vec3_t E ) {
	const vrbChest_t *ch = j->ch;
	vec3_t u, f, hz, pref, p;
	float c, up, fl, rd, pr, e1, e2, e3, in, t;
	int k;

	VectorSubtract( E, j->S, u );
	if ( VectorNormalize( u ) < 1e-4f ) {
		return 1e6f;
	}
	VectorSubtract( j->Wt, E, f );
	if ( VectorNormalize( f ) < 1e-4f ) {
		return 1e6f;
	}
	up = DotProduct( u, ch->U );

	// its weight
	c = vrbComfort.grav * ( up + 1.0f );
	// pointing in across the chest
	t = Q_max( 0.0f, -DotProduct( u, j->out ) - vrbComfort.acrossFrom );
	c += vrbComfort.across * t * t;
	// raised, out of the shoulder blade's plane
	VectorMA( u, -up, ch->U, hz );
	if ( VectorNormalize( hz ) > 1e-3f ) {
		const float az = DEG2RAD( vrbComfort.planeAz );
		VectorScale( j->out, cosf( az ), pref );
		VectorMA( pref, sinf( az ), ch->F, pref );
		c += vrbComfort.plane * VRB_SmoothStep( -0.6f, 0.3f, up ) * ( 1.0f - DotProduct( hz, pref ) );
	}
	// far behind
	t = Q_max( 0.0f, -DotProduct( u, ch->F ) - vrbComfort.backFrom );
	c += vrbComfort.back * t * t;
	// inside the body: the upper arm's middle and the elbow, and along the forearm
	for ( k = 0, t = 0.0f; k < 4; k++ ) {
		static const float along[4] = { 0.5f, 1.0f, 0.33f, 0.66f };
		if ( k < 2 ) {
			VectorSubtract( E, j->S, p );
			VectorMA( j->S, along[k], p, p );
		} else {
			VectorSubtract( j->Wt, E, p );
			VectorMA( E, along[k], p, p );
		}
		in = VRB_TorsoInside( ch, p );
		t += in * in;
	}
	c += vrbComfort.body * t;
	// the wrist's bend and the forearm's turn, for your hand as it is
	VRB_WristBend( j->h, f, (vec3_t *)j->hand, &fl, &rd );
	e1 = VRB_Past( fl, DEG2RAD( vrbComfort.ext ), DEG2RAD( vrbComfort.flex ) );
	e2 = VRB_Past( rd, DEG2RAD( vrbComfort.uln ), DEG2RAD( vrbComfort.rad ) );
	c += vrbComfort.wrist * ( e1 * e1 + e2 * e2 ) + vrbComfort.wrist0 * ( fl * fl + rd * rd );
	pr = VRB_Pronation( j->h, u, f, j->hand[1] );
	e3 = VRB_Past( pr, DEG2RAD( vrbComfort.sup ), DEG2RAD( vrbComfort.pron ) );
	c += vrbComfort.twist * e3 * e3 + vrbComfort.twist0 * pr * pr;
	// and not jumping about
	if ( j->havePrev ) {
		t = Distance( E, j->prevE ) / j->a;
		c += vrbComfort.still * t * t;
	}
	return c;
}

// The easiest place for the elbow round its circle (centre S + a cosA u,
// radius a sinA): v, the way from the circle's centre to it.
static void CG_VRB_ChooseElbow( const vrbElbowJob_t *j, const vec3_t u, float cosA, float sinA, vec3_t v ) {
	vec3_t e1, e2, centre, E, dir;
	float cost[VRB_ELBOW_SAMPLES], best = 0.0f, ph, c0, c1, c2, den, off = 0.0f;
	int i, bi = 0;

	VectorScale( j->ch->U, -1.0f, e1 );
	VectorMA( e1, -DotProduct( e1, u ), u, e1 );
	if ( VectorNormalize( e1 ) < 1e-3f ) {
		VectorScale( j->ch->F, -1.0f, e1 );
		VectorMA( e1, -DotProduct( e1, u ), u, e1 );
		VectorNormalize( e1 );
	}
	CrossProduct( u, e1, e2 );
	VectorMA( j->S, j->a * cosA, u, centre );
	for ( i = 0; i < VRB_ELBOW_SAMPLES; i++ ) {
		ph = 2.0f * M_PI * i / VRB_ELBOW_SAMPLES;
		VectorScale( e1, cosf( ph ), dir );
		VectorMA( dir, sinf( ph ), e2, dir );
		VectorMA( centre, j->a * sinA, dir, E );
		cost[i] = CG_VRB_ElbowCost( j, E );
		if ( !i || cost[i] < best ) {
			best = cost[i];
			bi = i;
		}
	}
	// (between the samples)
	c0 = cost[( bi + VRB_ELBOW_SAMPLES - 1 ) % VRB_ELBOW_SAMPLES];
	c1 = cost[bi];
	c2 = cost[( bi + 1 ) % VRB_ELBOW_SAMPLES];
	den = c0 - 2.0f * c1 + c2;
	if ( den > 1e-9f ) {
		off = Com_Clamp( -0.5f, 0.5f, 0.5f * ( c0 - c2 ) / den );
	}
	ph = 2.0f * M_PI * ( bi + off ) / VRB_ELBOW_SAMPLES;
	VectorScale( e1, cosf( ph ), v );
	VectorMA( v, sinf( ph ), e2, v );
}

/*
=================
CG_VRB_Girdle

The shoulder girdle for arm h: the collarbone turned about its root, lifting
the shoulder as the hand goes up, bringing it forward as the hand reaches
out or across, back as it goes behind - eased, like the rest of the body.
The collarbone carries the shoulder's flesh; the upper arm (which doesn't
hang off it in this skeleton) is turned with it, so the arm goes along.
Before the arm is solved: it then reaches from the shoulder where it is now.
=================
*/
static void CG_VRB_Girdle( centity_t *cent, int h, const vrbChest_t *ch, const vec3_t hand, qboolean commit ) {
	vrbBone_t clav, upper;
	vec3_t d, out, r0, axis;
	vrbMat_t Re, Rp, C;
	mdxaBone_t m;
	float reach, hz, hf, ho, want[2], cur[2], k, dt;
	int i;

	if ( !trap->ext.VR_G2SetBoneTransform || !CG_VRB_Bone( cent, vrbClavicle[h], 1, &clav ) ||
		!CG_VRB_Bone( cent, vrbArmBones[h][VRB_UPPER], 1, &upper ) ) {
		return;
	}
	VectorSubtract( upper.origin, clav.origin, r0 );
	if ( VectorLength( r0 ) < 1.0f ) {
		return;		// (no collarbone to speak of)
	}

	// where the hand is, from the shoulder, in arm lengths
	reach = 19.66f * ch->scale;
	VectorScale( ch->L, ( h == VR_HAND_RIGHT ) ? -1.0f : 1.0f, out );
	VectorSubtract( hand, upper.origin, d );
	hz = DotProduct( d, ch->U ) / reach;
	hf = DotProduct( d, ch->F ) / reach;
	ho = DotProduct( d, out ) / reach;
	want[0] = DEG2RAD( VRB_GIRDLE_UP ) * VRB_SmoothStep( -0.1f, 0.9f, hz );
	want[1] = DEG2RAD( VRB_GIRDLE_REACH ) * VRB_SmoothStep( 0.3f, 0.95f, hf ) +
		DEG2RAD( VRB_GIRDLE_ACROSS ) * VRB_SmoothStep( 0.0f, 0.7f, -ho ) -
		DEG2RAD( VRB_GIRDLE_BACK ) * VRB_SmoothStep( 0.15f, 0.6f, -hf );

	// eased (with time: both eyes the same)
	dt = Com_Clamp( 0.0f, 0.1f, ( cg.time - vrb.girdleTime[h] ) * 0.001f );
	if ( !vrb.girdleValid[h] || cg.time - vrb.girdleTime[h] > 500 || cg.time < vrb.girdleTime[h] ) {
		cur[0] = want[0];
		cur[1] = want[1];
	} else {
		k = 1.0f - expf( -dt / VRB_GIRDLE_EASE );
		for ( i = 0; i < 2; i++ ) {
			cur[i] = vrb.girdle[h][i] + k * ( want[i] - vrb.girdle[h][i] );
		}
	}
	if ( commit ) {
		vrb.girdle[h][0] = cur[0];
		vrb.girdle[h][1] = cur[1];
		vrb.girdleValid[h] = qtrue;
		vrb.girdleTime[h] = cg.time;
	}

	// lift (the shoulder toward up), then forward (toward the front)
	CrossProduct( r0, ch->U, axis );
	if ( VectorNormalize( axis ) < 1e-3f ) {
		return;
	}
	VRB_AxisAngle( axis, cur[0], Re );
	CrossProduct( r0, ch->F, axis );
	if ( VectorNormalize( axis ) < 1e-3f ) {
		return;
	}
	VRB_AxisAngle( axis, cur[1], Rp );
	VRB_Mul( Rp, Re, C );
	CG_VRB_ModelTurn( cent, C, clav.origin, &m );
	if ( trap->ext.VR_G2SetBoneTransform( cent->ghoul2, 0, vrbClavicle[h], &m ) ) {
		vrb.extraG2 = cent->ghoul2;
	}
	if ( trap->ext.VR_G2SetBoneTransform( cent->ghoul2, 0, vrbArmBones[h][VRB_UPPER], &m ) ) {
		vrb.extraG2 = cent->ghoul2;
	}
}

// The hand's twist about the forearm (in D, the turn at the wrist), part of
// it given to the forearm's twist bone halfway down it - as in the game's
// own animations - so a forearm turned palm up or down twists along its
// length, not all at the wrist or the elbow.
static void CG_VRB_TwistForearm( centity_t *cent, int h, vrbMat_t D, const vec3_t axis, const vec3_t elbow ) {
	vrbMat_t R;
	mdxaBone_t m;

	if ( !trap->ext.VR_G2SetBoneTransform ) {
		return;
	}
	VRB_AxisAngle( axis, VRB_TWIST_SHARE * VRB_TwistAngle( D, axis ), R );
	CG_VRB_ModelTurn( cent, R, elbow, &m );
	if ( trap->ext.VR_G2SetBoneTransform( cent->ghoul2, 0, vrbForearmTwist[h], &m ) ) {
		vrb.extraG2 = cent->ghoul2;
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
	const vrbChest_t *ch, qboolean commit, const vrbGrip_t *grip, vrbArmResult_t *res ) {
	vrbBone_t b[VRB_NUM_ARM_BONES];
	qboolean have[VRB_NUM_ARM_BONES];
	vec3_t gripO, gA[3], gH[3];
	vec3_t S, E0, W0, kmid, Ah[3], Ad[3], anchor, Wt, u, v, E, u0, u1, f0, f1, n0, n1, c0, c1, tmp;
	vec3_t F0[3], F1[3], want[3], cur[3];
	vrbMat_t Y0, Y, Q1, A, X;
	vec3_t holdO, holdDir, sentDir;
	float a, len2, d, dmin, dmax, cosA, sinA, w, pitch, handLen = 0.0f, letGo;
	qboolean haveHinge = qfalse, anatomy = qfalse, inFist, wristTurns;
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
		if ( grip->kind == VRB_GRIP_SABER ) {
			// (a saber just passed into this hand: the fist on its way from
			// where it held it to the controller)
			const float ease = CG_VRB_PassEase( grip->saber );
			if ( ease < 1.0f ) {
				VRB_FromFrame( gA, vrb.hold[grip->saber].passOfs, tmp );
				VectorMA( gripO, 1.0f - ease, tmp, gripO );
			}
		}
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

	// Your hand as it really is, lying on the controller (Ad: knuckles, toward
	// the thumb, [0] x [1]): OpenXR's grip pose runs its -Z through the fist
	// from the little finger to the thumb, its +Y back toward the wrist (Quake
	// axes: gA[0] = -Z, gA[2] = +Y). Y0 turns the animated hand onto it. (The
	// elbow goes by this one, not the hand as drawn below.) A saber passed into
	// this hand: the hand lying on the controller as it lay on the hilt as it
	// took it over (CG_VRB_PassSaber).
	for ( i = 0; i < 3; i++ ) {
		VectorCopy( gA[i], gH[i] );
		if ( grip->kind == VRB_GRIP_SABER && vrb.hold[grip->saber].keptGrip ) {
			VRB_FromFrame( gA, vrb.hold[grip->saber].keptGripAxis[i], gH[i] );
		}
	}
	letGo = ( grip->kind == VRB_GRIP_FREE ) ? CG_VRB_LetGoEase( h ) : 1.0f;
	if ( letGo < 1.0f ) {
		// (a hand that has just passed on the saber it held: easing off how it
		// lay on the hilt onto its controller - and its wrist's turn, below)
		vec3_t lay[3];
		for ( i = 0; i < 3; i++ ) {
			VRB_FromFrame( gA, vrb.letGo[h].axis[i], lay[i] );
		}
		VRB_FrameBlend( lay, gA, letGo, gH );
	}
	VectorScale( gH[2], -1.0f, Ad[0] );
	VectorCopy( gH[0], Ad[1] );
	pitch = vrbRemote ? 0.0f : cg_vrHandPitch.value;	// (another player's avatar has theirs in already)
	if ( pitch != 0.0f ) {
		RotatePointAroundVector( tmp, Ad[1], Ad[0], -pitch );
		VectorCopy( tmp, Ad[0] );
	}
	CrossProduct( Ad[0], Ad[1], Ad[2] );
	if ( anatomy ) {
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
		if ( letGo < 1.0f ) {
			// (letting go of the saber it held: its wrist easing off the turn
			// it took to hold it)
			vrbMat_t T;
			VRB_FromFrameTurn( gA, vrb.letGo[h].turn, T );
			VRB_TurnPart( T, 1.0f - letGo, T );
			VRB_Mul( T, Y0, Y );
		}
		if ( grip->kind == VRB_GRIP_SABER ) {
			// the blade where the hand holds it (CG_VRB_HeldBladeDir: along
			// your fist, tilted by cg_vrSaberPitch, out of the little finger's
			// side held reversed - or, passed into it, the way it took it), the
			// hand turned as little as that allows from lying naturally
			mdxaBone_t m;
			vec3_t bladeNow, bladeWant, fistWant;
			if ( trap->G2API_GetBoltMatrix( cent->ghoul2, grip->saberModel, 0, &m, cent->turAngles,
					cent->lerpOrigin, cg.time, cgs.gameModels, cent->modelScale ) ) {
				BG_GiveMeVectorFromMatrix( &m, NEGATIVE_Y, bladeNow );
				CG_VRB_HeldBladeDir( grip, gA, bladeWant );
				if ( vrb.blade[h].outTime == cg.time ) {
					VectorCopy( vrb.blade[h].out, bladeWant );	// where its weight lets it be (CG_VRB_BladeInertia)
				} else if ( !vrbRemote && h == CG_VRB_SaberHand( 0 ) && vrb.twoHandArms && vrb.twoHandTime == cg.time ) {
					// held with both hands (or settling into it, or out of it)
					VRB_Slerp( bladeWant, vrb.twoHandDir, vrb.twoHandEase, tmp );
					VectorCopy( tmp, bladeWant );
				}
				// the fist's line along it, the thumb toward the blade or - held
				// reversed - away from it (a saber that can't be moved in the
				// hand stays as it sits on its tag: its blade along it)
				if ( inFist ) {
					VectorCopy( holdDir, bladeNow );	// (where it's going to be)
					VectorScale( bladeWant, ( grip->sign < 0.0f ) ? -1.0f : 1.0f, fistWant );
				} else {
					VectorCopy( bladeWant, fistWant );
				}
				if ( VectorNormalize( bladeNow ) > 0.5f && VectorNormalize( fistWant ) > 0.5f &&
					VectorNormalize( bladeWant ) > 0.5f ) {
					VRB_Apply( Y0, bladeNow, tmp );
					VRB_Arc( tmp, fistWant, A );
					VRB_Mul( A, Y0, Y );
					VectorCopy( bladeWant, sentDir );
					if ( commit && !vrbRemote ) {
						VRB_ToFrameTurn( gA, A, vrb.heldTurn[h] );	// (for the hand letting go of it: CG_VRB_PassSaber)
						vrb.heldTurnTime[h] = cg.time;
					}
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

	VectorSubtract( W0, E0, f0 );
	VectorNormalize( f0 );

	// Which way the elbow points: where holding your hand like that is
	// easiest (CG_VRB_ChooseElbow), eased round the shoulder-wrist line from
	// where it was (chest-local, so turning doesn't drag it; with time, not
	// per call: both eyes get the same elbow).
	{
		const float dt = Com_Clamp( 0.0f, 0.1f, ( cg.time - vrb.poleTime[h] ) * 0.001f );
		vec3_t prev, chosen;
		qboolean havePrev = qfalse;

		if ( vrb.poleValid[h] && cg.time - vrb.poleTime[h] <= 500 && cg.time >= vrb.poleTime[h] ) {
			VRB_FromChest( ch, vrb.pole[h], prev );
			VectorMA( prev, -DotProduct( prev, u ), u, prev );
			havePrev = ( VectorNormalize( prev ) > 1e-3f ) ? qtrue : qfalse;
		}
		if ( havePrev && dt <= 0.0f ) {
			VectorCopy( prev, v );		// (the second eye)
		} else {
			vrbElbowJob_t job;
			memset( &job, 0, sizeof( job ) );
			job.h = h;
			job.ch = ch;
			job.a = a;
			VectorCopy( S, job.S );
			VectorCopy( Wt, job.Wt );
			VectorScale( ch->L, ( h == VR_HAND_RIGHT ) ? -1.0f : 1.0f, job.out );
			if ( grip->kind != VRB_GRIP_GUN ) {
				for ( i = 0; i < 3; i++ ) {
					VectorCopy( Ad[i], job.hand[i] );
				}
			} else if ( anatomy ) {
				// (a gun: the hand on it as drawn, which everyone who sees it draws the same)
				for ( i = 0; i < 3; i++ ) {
					VRB_Apply( Y, Ah[i], job.hand[i] );
				}
			} else {
				// (...and without finger bones, a hand straight on its forearm)
				vec3_t k, r;
				VectorCopy( f0, k );
				VectorSubtract( S, E0, r );
				VectorMA( r, -DotProduct( r, k ), k, r );
				if ( VectorNormalize( r ) < 1e-3f ) {
					PerpendicularVector( r, k );
				}
				VRB_Apply( Y, k, job.hand[0] );
				VRB_Apply( Y, r, job.hand[1] );
				CrossProduct( job.hand[0], job.hand[1], job.hand[2] );
			}
			if ( havePrev ) {
				VRB_FromChest( ch, vrb.elbowLocal[h], tmp );
				VectorAdd( S, tmp, job.prevE );
				job.havePrev = qtrue;
			}
			CG_VRB_ChooseElbow( &job, u, cosA, sinA, chosen );
			if ( havePrev ) {
				// round the circle toward it, eased, and no faster than an arm
				// swings an elbow round (a jump across it is a quick swing)
				const float k = 1.0f - expf( -dt / VRB_ELBOW_EASE );
				const float cap = DEG2RAD( VRB_ELBOW_RATE ) * dt;
				float ang;
				CrossProduct( prev, chosen, tmp );
				ang = k * atan2f( DotProduct( tmp, u ), DotProduct( prev, chosen ) );
				VRB_Rotate( prev, u, Com_Clamp( -cap, cap, ang ), v );
				VectorMA( v, -DotProduct( v, u ), u, v );
				if ( VectorNormalize( v ) < 1e-3f ) {
					VectorCopy( chosen, v );
				}
			} else {
				VectorCopy( chosen, v );
			}
		}

		VectorScale( u, a * cosA, E );
		VectorMA( E, a * sinA, v, E );
		VectorAdd( S, E, E );

		if ( commit ) {
			VRB_ToChest( ch, v, vrb.pole[h] );
			VectorSubtract( E, S, tmp );
			VRB_ToChest( ch, tmp, vrb.elbowLocal[h] );
			vrb.poleValid[h] = qtrue;
			vrb.poleTime[h] = cg.time;
		}
	}

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

	// Forearm: the elbow is a hinge, so the forearm turns only about it -
	// carried along by the upper arm (Q1), then bent onto the wrist. The
	// hand's twist (palm up or down) is the wrist's, below, shared along the
	// forearm by its twist bone, as in the game's own animations. (Where the
	// hand can't be turned at the wrist, the forearm takes all of its turn.)
	wristTurns = ( cg_vrBodyWrist.integer && vrb.wristOk[h] && vrb.probeG2 == cent->ghoul2 &&
		trap->ext.VR_G2SetBoneTransform ) ? qtrue : qfalse;
	if ( wristTurns ) {
		VRB_Apply( Q1, f0, tmp );
		VRB_Arc( tmp, f1, A );
		VRB_Mul( A, Q1, X );
	} else {
		VRB_Apply( Y, f0, tmp );
		VRB_Arc( tmp, f1, A );
		VRB_Mul( A, Y, X );
	}

	// overrides: upper arm first, then the forearm relative to its new parent
	VRB_RotateFrame( Q1, b[VRB_UPPER].axis, want );
	CG_VRB_SetBone( cent->ghoul2, vrbArmBones[h][VRB_UPPER], b[VRB_UPPER].axis, want );
	VRB_RotateFrame( Q1, b[VRB_FORE].axis, cur );
	VRB_RotateFrame( X, b[VRB_FORE].axis, want );
	CG_VRB_SetBone( cent->ghoul2, vrbArmBones[h][VRB_FORE], cur, want );

	// Wrist: the turn that's left (Y = D X) - its bend, and its twist about
	// the forearm, which the forearm's twist bone shares - about the wrist
	{
		static const mdxaBone_t identity = { { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 } } };
		mdxaBone_t H = identity;
		if ( wristTurns ) {
			vrbMat_t D;
			VRB_MulT( Y, X, D );
			if ( !CG_VRB_TurnWrist( cent, h, D, Wt, &H ) ) {
				H = identity;
			} else {
				CG_VRB_TwistForearm( cent, h, D, f1, E );
			}
		}

		// fingers closed round whatever hilt the hand holds - an empty hand's,
		// and a gun hand's index finger, as yours are (CG_VRB_PoseFingers)
		if ( grip->kind == VRB_GRIP_HILT && vrb.twoHand ) {
			// (only while it holds it, closing as it settles on)
			CG_VRB_CurlFingers( cent, h, &H, grip->origin, grip->dir, grip->blend );
		} else if ( grip->kind == VRB_GRIP_GUN || grip->kind == VRB_GRIP_HILT ||
			( grip->kind == VRB_GRIP_FREE && !CG_VRB_WeaponOnHand( cent, h ) ) ) {
			// (a hand coming off a hilt too: it's empty; not one the game's
			// weapon hangs on, its fingers round it)
			float curl[VR_NUM_FINGERS];
			if ( CG_VRB_HandFingers( h, curl ) ) {
				CG_VRB_PoseFingers( cent, h, &H, curl, grip->kind == VRB_GRIP_GUN );
			}
		} else if ( grip->kind == VRB_GRIP_SABER && inFist && CG_VRB_PlaceSaber( cent, h, &H, grip, gA, commit, holdO, holdDir ) ) {
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
		// (the controller with your hand on it: its pitch in, so their elbow
		// goes where yours does, whatever the model)
		VectorCopy( gripO, sent->origin );
		VectorCopy( Ad[1], sent->axis[0] );
		VectorScale( Ad[0], -1.0f, sent->axis[2] );
		CrossProduct( sent->axis[2], sent->axis[0], sent->axis[1] );
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
			sent->saber = grip->saber;
			sent->hilt = grip->hilt;
			sent->reversed = ( grip->sign < 0.0f ) ? qtrue : qfalse;
			break;
		case VRB_GRIP_HILT:
			if ( vrb.twoHand ) {
				sent->grip = VR_GRIP_HILT;
				VectorCopy( grip->dir, sent->dir );
				sent->blend = grip->blend;
				sent->saber = 0;	// (both hands hold the first saber)
			} else {
				sent->grip = VR_GRIP_FREE;	// (letting go: the hand coming off it, fingers open)
			}
			break;
		default:
			sent->grip = VR_GRIP_FREE;
			break;
		}
		// your fingers as this hand shows them (an empty hand's; a gun's index finger)
		if ( ( sent->grip == VR_GRIP_FREE && !CG_VRB_WeaponOnHand( cent, h ) ) || sent->grip == VR_GRIP_GUN ) {
			sent->fingersValid = CG_VRB_HandFingers( h, sent->fingers );
		}
		vrb.sentTime[h] = cg.time;
	}
	res->solved = qtrue;
	res->exact = ( sinA > 0.3f ) ? qtrue : qfalse;
	VectorCopy( E, res->elbow );
	VectorCopy( Wt, res->wrist );
	{
		float fl, rd;
		VRB_WristBend( h, f1, Ad, &fl, &rd );
		res->wristFlex = RAD2DEG( fl );
		res->wristRadial = RAD2DEG( rd );
		res->pronation = RAD2DEG( VRB_Pronation( h, u1, f1, Ad[1] ) );
		res->elbowUp = DotProduct( u1, ch->U );
	}
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
	vrbChest_t ch;
	vec3_t hand, ax[3];
	int h, n = 0;

	CG_VRB_ResetArms( cent->ghoul2 );
	CG_VRB_Chest( cent, bodyYaw, &ch );
	for ( h = 0; h < 2; h++ ) {
		memset( &res[h], 0, sizeof( res[h] ) );
		if ( !vr->handValid[h] ) {
			continue;
		}
		CG_VRB_Grip( cent, h, &grip );
		// the shoulder first: the arm then reaches from where it is
		if ( grip.kind == VRB_GRIP_HILT ) {
			VectorCopy( grip.origin, hand );
		} else {
			CG_VRB_Controller( vr, h, hand, ax );
		}
		CG_VRB_Girdle( cent, h, &ch, hand, commit );
		if ( CG_VRB_SolveArm( cent, vr, h, convention, &ch, commit, &grip, &res[h] ) ) {
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

// The log, now and then: how your arms were drawn - for working out what
// looks wrong. Per arm: the elbow's height (above the shoulder, in upper arm
// lengths), how your real hand is turned against the forearm drawn (wrist
// bent to the palm / to the thumb, forearm turned palm down), and the shoulder.
static void CG_VRB_LogArms( vrbArmResult_t res[2] ) {
	char text[2][128];
	int h;

	if ( vrbRemote || !cg_vrLog.integer || cg.time - vrb.armLogTime < 2000 ) {
		return;
	}
	vrb.armLogTime = cg.time;
	for ( h = 0; h < 2; h++ ) {
		if ( !res[h].solved ) {
			Q_strncpyz( text[h], "-", sizeof( text[h] ) );
			continue;
		}
		Com_sprintf( text[h], sizeof( text[h] ), "elbow %+.2f, wrist %+.0f %+.0f, turned %+.0f, shoulder up %.0f fwd %+.0f",
			res[h].elbowUp, res[h].wristFlex, res[h].wristRadial, res[h].pronation,
			RAD2DEG( vrb.girdle[h][0] ), RAD2DEG( vrb.girdle[h][1] ) );
	}
	CG_VRB_Log( "arms: right %s | left %s\n", text[VR_HAND_RIGHT], text[VR_HAND_LEFT] );
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
/*
The sabers hang on the hands holding them, and so does a gun: the game
bolts the first saber to the right hand's tag and a second one to the
left's (or to their wrists: SFL_BOLT_TO_WRIST), a gun to the right hand;
here each goes on the hand that holds it (a gun on the gun hand - another
player's, the hand their avatar holds it in). Every frame, before the arms
are set back to the animation (which makes the skeleton they hang on be
worked out afresh): a weapon the game puts on the model again hangs where
it says. CG_VRB_UnboltHands puts them back.
*/
static void CG_VRB_BoltHands( centity_t *cent ) {
	const clientInfo_t *ci = &cgs.clientinfo[cent->currentState.number];
	int s, h;

	if ( !cent->ghoul2 ) {
		return;
	}
	if ( cent->currentState.weapon == WP_SABER ) {
		for ( s = 0; s < MAX_SABERS; s++ ) {
			if ( !trap->G2API_HasGhoul2ModelOnIndex( &cent->ghoul2, s + 1 ) ) {
				continue;
			}
			h = CG_VRB_SaberHand( s );
			if ( h < 0 ) {
				h = ( s == 0 ) ? VR_HAND_RIGHT : VR_HAND_LEFT;	// (no hand on it: where the game puts it)
			}
			trap->G2API_SetBoltInfo( cent->ghoul2, s + 1, ( ( h == VR_HAND_RIGHT ) ? 0 : 1 ) +
				( ( ci->saber[s].saberFlags & SFL_BOLT_TO_WRIST ) ? 3 : 0 ) );
		}
	} else if ( trap->G2API_HasGhoul2ModelOnIndex( &cent->ghoul2, 1 ) ) {
		// (yours: your body's own copy, hidden - the gun you see is at your controller)
		h = CG_VRB_GunBoltHand();
		trap->G2API_SetBoltInfo( cent->ghoul2, 1, ( h == VR_HAND_RIGHT ) ? 0 : 1 );
	}
	vrb.boltG2 = cent->ghoul2;
}

// Your sabers in their holsters (holsters: yours, cg_vrholster.c; or another
// VR player's, their avatar's hands not on them) are drawn there: their
// copies on the body's hands hidden (and shown again otherwise).
static void CG_VRB_HideHolstered( centity_t *cent, qboolean holsters ) {
	int s;

	if ( !trap->ext.VR_G2SetModelHidden || !cent->ghoul2 ) {
		return;
	}
	for ( s = 0; s < MAX_SABERS; s++ ) {
		const qboolean hide = (qboolean)( holsters && cent->currentState.weapon == WP_SABER && CG_VRB_SaberHand( s ) < 0 &&
			!( s == 0 && cent->currentState.saberInFlight ) && !( cent->currentState.eFlags & EF_DEAD ) &&
			trap->G2API_HasGhoul2ModelOnIndex( &cent->ghoul2, s + 1 ) );	// (dead: as the game has it - nothing hangs on the dead)
		if ( hide ) {
			if ( trap->ext.VR_G2SetModelHidden( cent->ghoul2, s + 1, qtrue ) ) {
				vrb.holsteredG2[s] = cent->ghoul2;
			}
		} else if ( vrb.holsteredG2[s] == cent->ghoul2 ) {
			if ( cent->currentState.weapon == WP_SABER || cent->currentState.weapon == WP_NONE || s > 0 ) {	// (model 1 another weapon now: its own)
				trap->ext.VR_G2SetModelHidden( cent->ghoul2, s + 1, qfalse );
			}
			vrb.holsteredG2[s] = NULL;
		}
	}
}

// The game has just put a saber back on a player's model where it hangs it
// (caught: cg_players.c): on the hand holding it, if their arms hold it.
void CG_VR_BodyRebolt( centity_t *cent ) {
	const int n = cent ? cent->currentState.number : -1;

	if ( n < 0 || n >= MAX_CLIENTS || !cent->ghoul2 ) {
		return;
	}
	if ( n == cg.clientNum ) {
		if ( vrbSelf.boltG2 == cent->ghoul2 ) {
			vrbCur = &vrbSelf;
			vrbRemote = NULL;
			CG_VRB_BoltHands( cent );
		}
	} else if ( vrbOthers[n].boltG2 == cent->ghoul2 && vrbOthers[n].avatarValid ) {
		vrbCur = &vrbOthers[n];
		vrbRemote = &vrbOthers[n].avatar;
		CG_VRB_BoltHands( cent );
		vrbRemote = NULL;
		vrbCur = &vrbSelf;
	}
}

static void CG_VRB_UnboltHands( centity_t *cent ) {
	const clientInfo_t *ci = &cgs.clientinfo[cent->currentState.number];
	int s;

	if ( !cent->ghoul2 || vrb.boltG2 != cent->ghoul2 ) {
		return;
	}
	for ( s = 0; s < MAX_SABERS; s++ ) {
		if ( trap->G2API_HasGhoul2ModelOnIndex( &cent->ghoul2, s + 1 ) ) {
			trap->G2API_SetBoltInfo( cent->ghoul2, s + 1, s + ( ( cent->currentState.weapon == WP_SABER &&
				( ci->saber[s].saberFlags & SFL_BOLT_TO_WRIST ) ) ? 3 : 0 ) );
		}
	}
	vrb.boltG2 = NULL;
}

static void CG_VRB_Arms( centity_t *cent, const vrClientState_t *vr, float bodyYaw ) {
	vrbArmResult_t res[2];
	qboolean meaningful;
	float err[VRB_NUM_CONVENTIONS];
	int c, best;

	vrb.armsG2 = cent->ghoul2;
	if ( !vrbRemote ) {
		CG_VRB_Holds( cent );
	}
	CG_VRB_BoltHands( cent );
	CG_VRB_ResetArms( cent->ghoul2 );
	CG_VRB_ProbeHands( cent );
	if ( !vrbRemote ) {
		// (another player's: their avatar says, CG_VRB_OtherArms)
		CG_VRB_TwoHand( cent, vr );
		CG_VRB_BladeInertia( cent, vr );
		CG_VRB_EaseFingers( cent, vr );
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
		CG_VRB_LogArms( res );
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

the body bends (cg_vrBodyBend)

Standing still, your body stands the way you do: its feet stay where they
are, its hips over them, and it leans, bows, twists and crouches from there
to put its head where yours is - the trunk leaning from the hips up (half
of a bow at the hips themselves, the rest along the back), the knees
bending to lower the hips when you crouch for real (as low as the view
follows you down), each foot on the ground under it. Looking aside turns the
neck first, then the chest (looking further than cg_vrBodyTwist), and the
hips last (CG_VRB_BodyYaw); the chest also turns with the arms as they swing
a weapon. Running, jumping, a flip, a pose: the game's animation, the bend
eased out, and back in when you stand again (CG_VRB_BendWeight). Without
Body Bends the body stays upright and is moved whole under your head
(CG_VR_BodyPose), as before.

It's done with engine bone transforms on top of the animated pose (the
game's own bone angles stay as they are): every bone of the hips, back,
neck, head and legs is given its rigid move in the world, from where the
animation has it to where it goes, with its parent's move taken back out -
the engine puts a bone's transform on top of its parent's. Another VR
player's body bends the same way from their avatar: its eyes go where their
game put their body's eyes (vrAvatar_t.head), its head looks where they
look. Hits are still judged on the server's upright body.

=============================================================================
*/

#define VRB_CHEST_EASE		0.1f	// seconds the chest takes to follow
#define VRB_ARMS_TURN		0.5f	// it turns this much of the way round to the hands holding a weapon, once they're out
#define VRB_ARMS_NEAR		9.0f	// from it (none of it while they're this near it, hanging at the sides, all of it this
#define VRB_ARMS_OUT		15.0f	// far out, reaching or swinging: units)...
#define VRB_ARMS_SIDE		90.0f	// ...as far round as its side, none of it once they're this far round behind it
#define VRB_ARMS_BEHIND		135.0f	// (degrees round from straight ahead)
#define VRB_LOOK_EASE		0.2f	// seconds the bend takes to take the head over from the game, or to hand it back
#define VRB_LEAN_FWD		60.0f	// the trunk leans this far forward at most (degrees)...
#define VRB_LEAN_BACK		25.0f	// ...back...
#define VRB_LEAN_SIDE		30.0f	// ...and to a side
#define VRB_HIPS_BACK		0.2f	// the hips go back this much of the way the neck goes forward (and the other way round)...
#define VRB_SIT_BACK		0.3f	// ...and this much of the way they go down
#define VRB_DROP_MAX		16.0f	// the knees lower the hips this far at most (units)
#define VRB_STEP_UP			6.0f	// a foot stands on ground up to this much higher than the floor the body stands on...
#define VRB_STEP_DOWN		4.0f	// ...or this much lower...
#define VRB_FOOT_EASE		0.08f	// ...getting there in this long (seconds)

// The bones that bend, and each one's parent among them (-1: none)
enum {
	VRB_B_PELVIS, VRB_B_LOWER_BACK, VRB_B_UPPER_BACK, VRB_B_CHEST, VRB_B_NECK, VRB_B_HEAD,
	VRB_B_LTHIGH, VRB_B_LSHIN, VRB_B_LFOOT, VRB_B_RTHIGH, VRB_B_RSHIN, VRB_B_RFOOT,
	VRB_NUM_BEND_BONES
};
static const char *const vrbBendBones[VRB_NUM_BEND_BONES] = {
	"pelvis", "lower_lumbar", "upper_lumbar", "thoracic", "cervical", "cranium",
	"lfemurYZ", "ltibia", "ltalus", "rfemurYZ", "rtibia", "rtalus"
};
static const int vrbBendParent[VRB_NUM_BEND_BONES] = { -1, 0, 1, 2, 3, 4, 0, 6, 7, 0, 9, 10 };
typedef char vrbBendMovesFit[( sizeof( ( (vrbState_t *)0 )->bendMoves ) == VRB_NUM_BEND_BONES * sizeof( mdxaBone_t ) ) ? 1 : -1];
static const int vrbThigh[2] = { VRB_B_LTHIGH, VRB_B_RTHIGH };	// VR_HAND_LEFT, VR_HAND_RIGHT (the shin and foot follow)

// How much of the trunk's lean forward, its lean to a side and its twist
// each of its bones has taken by its top (pelvis, lower back, upper back,
// chest): a bow goes half at the hips, the rest along the back; a lean to a
// side along the back, a twist mostly high in it.
static const float vrbLeanShare[4] = { 0.5f, 0.7f, 0.85f, 1.0f };
static const float vrbSideShare[4] = { 0.0f, 0.4f, 0.75f, 1.0f };
static const float vrbTwistShare[4] = { 0.0f, 0.15f, 0.5f, 1.0f };

// the body as animated this frame, before it bends (world)
typedef struct {
	vec3_t	at[VRB_NUM_BEND_BONES];	// each bone's joint
	vec3_t	eyes;					// between the eyes
	vec3_t	hips;					// between the hip joints: the pelvis tilts about the line through them...
	vec3_t	hipLine;				// ...toward the left
	vec3_t	face[3];				// the way the body faces: forward, left, up (its chest and head, as the game turns them)
	float	thigh[2], shin[2];		// each leg's bones (VR_HAND_LEFT, VR_HAND_RIGHT)
} vrbBendRest_t;

extern qboolean BG_InKnockDown( int anim );

// The hips, back, neck, head and legs as the animation has them...
static void CG_VRB_ResetBody( void *g2 ) {
	static const mdxaBone_t identity = { { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 } } };
	int k;

	if ( vrb.bendG2 != g2 || !g2 || !trap->ext.VR_G2SetBoneTransform ) {
		return;
	}
	for ( k = 0; k < VRB_NUM_BEND_BONES; k++ ) {
		trap->ext.VR_G2SetBoneTransform( g2, 0, vrbBendBones[k], &identity );
	}
}

// ...and for good
static void CG_VRB_ClearBody( void *g2 ) {
	int k;

	if ( vrb.bendG2 != g2 || !g2 || !trap->ext.VR_G2SetBoneTransform ) {
		return;
	}
	for ( k = 0; k < VRB_NUM_BEND_BONES; k++ ) {
		trap->ext.VR_G2SetBoneTransform( g2, 0, vrbBendBones[k], NULL );
	}
	vrb.bendG2 = NULL;
}

// How far CG_Player lifted a player drawn bigger or smaller than they are
// (iModelScale: so the model's feet stay on the floor) - where they stand,
// their eye point and their avatar's offsets are from where they are
// without it.
static float CG_VRB_ScaleLift( const centity_t *cent ) {
	if ( cent->currentState.iModelScale && cent->currentState.NPC_class != CLASS_VEHICLE && cent->modelScale[2] &&
		cent->modelScale[2] != 1.0f ) {
		return 24.0f * ( cent->modelScale[2] - 1.0f );
	}
	return 0.0f;
}

// Between the body's eyes (or its skull, a model without eyes).
static qboolean CG_VRB_Eyes( centity_t *cent, vec3_t out ) {
	vrbBone_t le, re;

	if ( CG_VRB_Bone( cent, "leye", 1, &le ) && CG_VRB_Bone( cent, "reye", 1, &re ) ) {
		VectorAdd( le.origin, re.origin, out );
		VectorScale( out, 0.5f, out );
		return qtrue;
	}
	if ( CG_VRB_Bone( cent, "cranium", 1, &le ) ) {
		VectorCopy( le.origin, out );
		return qtrue;
	}
	return qfalse;
}

// Whether the body's head turns the way theirs does here (not with the
// game's head turn, CG_VR_BodyLook): Body Bends on, the body up - not
// knocked down or getting up, in a pose or down (roleplay).
static qboolean CG_VRB_OwnLook( centity_t *cent ) {
	return (qboolean)( cg_vrBodyBend.integer && !BG_InKnockDown( cent->currentState.legsAnim ) &&
		!BG_InKnockDown( cent->currentState.torsoAnim ) && !BG_VRPoseAnim( cent->currentState.legsAnim ) &&
		!CG_RP_DownBody( cent ) );
}

// The body as animated, measured (qfalse: a model without these bones).
static qboolean CG_VRB_BendRest( centity_t *cent, float bodyYaw, vrbBendRest_t *r ) {
	vrbBone_t b;
	int k, s;

	for ( k = 0; k < VRB_NUM_BEND_BONES; k++ ) {
		if ( !CG_VRB_Bone( cent, vrbBendBones[k], 1, &b ) ) {
			return qfalse;
		}
		VectorCopy( b.origin, r->at[k] );
	}
	if ( !CG_VRB_Eyes( cent, r->eyes ) ) {
		return qfalse;
	}
	VectorAdd( r->at[VRB_B_LTHIGH], r->at[VRB_B_RTHIGH], r->hips );
	VectorScale( r->hips, 0.5f, r->hips );
	VectorSet( r->face[0], cosf( DEG2RAD( bodyYaw ) ), sinf( DEG2RAD( bodyYaw ) ), 0.0f );
	VectorSet( r->face[1], -r->face[0][1], r->face[0][0], 0.0f );
	VectorSet( r->face[2], 0.0f, 0.0f, 1.0f );
	VectorSubtract( r->at[VRB_B_LTHIGH], r->at[VRB_B_RTHIGH], r->hipLine );
	VectorNormalize( r->hipLine );
	for ( s = 0; s < 2; s++ ) {
		r->thigh[s] = Distance( r->at[vrbThigh[s]], r->at[vrbThigh[s] + 1] );
		r->shin[s] = Distance( r->at[vrbThigh[s] + 1], r->at[vrbThigh[s] + 2] );
		if ( r->thigh[s] < 1.0f || r->shin[s] < 1.0f ) {
			return qfalse;
		}
	}
	return qtrue;
}

/*
The trunk bent: the pelvis turned forward by its share of a (the lean
forward, radians) about the line through the hip joints, and moved by
delta; each bone of the back turned by its share of a, of b (the lean to
the left) and of tau (the twist, to the left about the upright). Where each
joint at their tops goes (the lower back's, the upper back's, the chest's,
the neck's), and how each bone is turned (the pelvis, lower back, upper
back, chest).
*/
static void VRB_Trunk( const vrbBendRest_t *r, float a, float b, float tau, const vec3_t delta,
	vec3_t joint[4], vrbMat_t turn[4] ) {
	static const vec3_t up = { 0.0f, 0.0f, 1.0f };
	vec3_t from, to, seg;
	int k;

	VectorCopy( r->hips, from );
	VectorAdd( r->hips, delta, to );
	for ( k = 0; k < 4; k++ ) {
		vrbMat_t lean, twist;
		vec3_t w;
		float angle;
		VectorScale( k ? r->face[1] : r->hipLine, vrbLeanShare[k] * a, w );	// (about the left - the pelvis, its hip joints': the top goes forward)
		VectorMA( w, -vrbSideShare[k] * b, r->face[0], w );		// (about the back: the top goes left)
		angle = VectorNormalize( w );
		if ( angle > 1e-6f ) {
			VRB_AxisAngle( w, angle, lean );
		} else {
			VRB_Identity( lean );
		}
		VRB_AxisAngle( up, vrbTwistShare[k] * tau, twist );
		VRB_Mul( lean, twist, turn[k] );
		VectorSubtract( r->at[VRB_B_LOWER_BACK + k], from, seg );
		VRB_Apply( turn[k], seg, seg );
		VectorAdd( to, seg, joint[k] );
		VectorCopy( r->at[VRB_B_LOWER_BACK + k], from );
		VectorCopy( joint[k], to );
	}
}

static void VRB_TrunkNeck( const vrbBendRest_t *r, float a, float b, float tau, const vec3_t delta, vec3_t neck ) {
	vec3_t joint[4];
	vrbMat_t turn[4];

	VRB_Trunk( r, a, b, tau, delta, joint, turn );
	VectorCopy( joint[3], neck );
}

// A lean kept within how far the trunk goes: forward or back, to a side, and
// in between (an ellipse).
static void VRB_LeanLimit( float *a, float *b ) {
	const float fa = *a / DEG2RAD( ( *a > 0.0f ) ? VRB_LEAN_FWD : VRB_LEAN_BACK );
	const float fb = *b / DEG2RAD( VRB_LEAN_SIDE );
	const float e = sqrtf( fa * fa + fb * fb );

	if ( e > 1.0f ) {
		*a /= e;
		*b /= e;
	}
}

// The lean (from a, b) that puts the neck over neckTo, seen from above, the
// hips moved by delta: Newton's method (two steps get it within a hundredth
// of a unit, even from upright), within the trunk's reach.
static void VRB_LeanTo( const vrbBendRest_t *r, const vec3_t neckTo, float tau, const vec3_t delta, float *a, float *b ) {
	const float h = 0.01f;
	int it;

	for ( it = 0; it < 4; it++ ) {
		vec3_t n0, na, nb, e;
		float j00, j01, j10, j11, det, ef, el;
		VRB_TrunkNeck( r, *a, *b, tau, delta, n0 );
		VectorSubtract( neckTo, n0, e );
		ef = DotProduct( e, r->face[0] );
		el = DotProduct( e, r->face[1] );
		if ( ef * ef + el * el < 1e-4f ) {
			break;
		}
		VRB_TrunkNeck( r, *a + h, *b, tau, delta, na );
		VRB_TrunkNeck( r, *a, *b + h, tau, delta, nb );
		VectorSubtract( na, n0, na );
		VectorSubtract( nb, n0, nb );
		j00 = DotProduct( na, r->face[0] ) / h;
		j01 = DotProduct( nb, r->face[0] ) / h;
		j10 = DotProduct( na, r->face[1] ) / h;
		j11 = DotProduct( nb, r->face[1] ) / h;
		det = j00 * j11 - j01 * j10;
		if ( fabsf( det ) < 1e-3f ) {
			break;
		}
		*a += ( ef * j11 - el * j01 ) / det;
		*b += ( el * j00 - ef * j10 ) / det;
		VRB_LeanLimit( a, b );
	}
}

// How far the hips must come down for each leg to reach its foot at feet[]
// (the hips moved across by delta: only its level part counts).
static float VRB_FeetDrop( const vrbBendRest_t *r, const vec3_t delta, vec3_t feet[2] ) {
	float need = 0.0f;
	int s;

	for ( s = 0; s < 2; s++ ) {
		const float reach = 0.999f * ( r->thigh[s] + r->shin[s] );
		const vec3_t *hip = &r->at[vrbThigh[s]];
		const float x = feet[s][0] - ( (*hip)[0] + delta[0] ), y = feet[s][1] - ( (*hip)[1] + delta[1] );
		if ( x * x + y * y < reach * reach ) {
			need = Q_max( need, (*hip)[2] - feet[s][2] - sqrtf( reach * reach - x * x - y * y ) );
		}
	}
	return need;
}

/*
Where the trunk goes for the neck to be at neckTo: the hips back as the
neck goes forward (and the other way round), down - the knees bending - as
far as the neck goes down past what leaning takes it, or a foot needs to
reach its ground (and back a little as they go down); and the lean from
there (a, b, from what they are).
*/
static void VRB_SolveTrunk( const vrbBendRest_t *r, const vec3_t neckTo, float tau, vec3_t feet[2],
	float *a, float *b, vec3_t delta ) {
	vec3_t reach, neck;
	float d = 0.0f;
	int pass;

	VectorSubtract( neckTo, r->at[VRB_B_NECK], reach );
	reach[2] = 0.0f;
	for ( pass = 0; ; pass++ ) {
		VectorScale( reach, -VRB_HIPS_BACK, delta );
		VectorMA( delta, -VRB_SIT_BACK * d, r->face[0], delta );
		delta[2] = -d;
		VRB_LeanTo( r, neckTo, tau, delta, a, b );
		if ( pass == 2 ) {
			break;	// (three passes settle it within a hundredth of a unit)
		}
		VRB_TrunkNeck( r, *a, *b, tau, delta, neck );
		d = Com_Clamp( 0.0f, VRB_DROP_MAX, Q_max( d + neck[2] - neckTo[2], VRB_FeetDrop( r, delta, feet ) ) );
	}
}

/*
How the head turns to look along look (as animated it faces the way the
body does) - share of the way there from the chest's turn (chest) - no
further off the chest than a neck turns it: the look from the chest taken
as a turn about the chest's upright (the head turning on the neck), then a
nod and a tilt to a side, each kept within its reach. Only own of that: the
game turns the rest of the way, as animated (CG_VR_BodyLook; look NULL, all
of it). The head's turn, and - halfway to it from the chest's - the neck's.
*/
static void VRB_HeadTurn( const vrbBendRest_t *r, vec3_t look[3], vrbMat_t chest, float share, float own, vrbMat_t head, vrbMat_t neck ) {
	vec3_t face[3], c[3], to[3], axis, x, y;
	vrbMat_t rel, twist, swing, part;
	float yaw, nod, tilt, angle, e;
	int i;

	if ( !look || own <= 0.0f ) {
		VRB_Identity( head );
		VRB_TurnPart( chest, 0.5f, neck );
		return;
	}
	for ( i = 0; i < 3; i++ ) {
		VectorCopy( r->face[i], face[i] );
	}
	VRB_RotateFrame( chest, face, c );
	VRB_FrameMap( c, look, rel );				// (the look's turn from the chest's)
	yaw = VRB_TwistAngle( rel, c[2] );
	VRB_AxisAngle( c[2], yaw, twist );
	VRB_MulT( rel, twist, swing );				// (the rest of it: about a line square to the upright)
	VectorSet( axis, swing[2][1] - swing[1][2], swing[0][2] - swing[2][0], swing[1][0] - swing[0][1] );
	angle = atan2f( 0.5f * VectorNormalize( axis ), 0.5f * ( swing[0][0] + swing[1][1] + swing[2][2] - 1.0f ) );
	VRB_Apply( twist, c[0], x );
	VRB_Apply( twist, c[1], y );
	nod = angle * DotProduct( axis, y );		// (about the head's left: down)
	tilt = angle * DotProduct( axis, x );		// (about its forward)
	yaw = Com_Clamp( -DEG2RAD( VRB_NECK_YAW ), DEG2RAD( VRB_NECK_YAW ), yaw );
	e = sqrtf( Square( nod / DEG2RAD( ( nod > 0.0f ) ? VRB_NECK_DOWN : VRB_NECK_UP ) ) + Square( tilt / DEG2RAD( VRB_NECK_ROLL ) ) );
	if ( e > 1.0f ) {
		nod /= e;
		tilt /= e;
	}
	VRB_AxisAngle( c[2], yaw, twist );
	VRB_Apply( twist, c[0], x );
	VRB_Apply( twist, c[1], y );
	VectorScale( y, nod, axis );
	VectorMA( axis, tilt, x, axis );
	angle = VectorNormalize( axis );
	if ( angle > 1e-6f ) {
		VRB_AxisAngle( axis, angle, swing );
		VRB_Mul( swing, twist, rel );
	} else {
		memcpy( rel, twist, sizeof( vrbMat_t ) );
	}
	VRB_RotateFrame( rel, c, to );
	VRB_FrameMap( face, to, head );
	if ( share < 1.0f ) {
		VRB_MulT( head, chest, part );	// (share of the head's turn from the chest's...
		VRB_TurnPart( part, share, part );
		VRB_Mul( part, chest, head );
	}
	if ( own < 1.0f ) {
		VRB_TurnPart( head, own, head );	// (...own of it)
	}
	VRB_MulT( head, chest, part );		// (the neck: halfway to it from the chest's)
	VRB_TurnPart( part, 0.5f, part );
	VRB_Mul( part, chest, neck );
}

// Where the eyes are from the neck's joint, the neck and head turned so.
static void VRB_EyesFromNeck( const vrbBendRest_t *r, vrbMat_t head, vrbMat_t neck, vec3_t out ) {
	vec3_t a, b;

	VectorSubtract( r->at[VRB_B_HEAD], r->at[VRB_B_NECK], a );
	VectorSubtract( r->eyes, r->at[VRB_B_HEAD], b );
	VRB_Apply( neck, a, a );
	VRB_Apply( head, b, b );
	VectorAdd( a, b, out );
}

// Where the hands holding a weapon are (their middle, world): the chest
// turns toward them. qfalse: none.
static qboolean CG_VRB_WeaponHands( const vrClientState_t *vr, vec3_t out ) {
	vec3_t o, ax[3];
	int h, n = 0;

	VectorClear( out );
	for ( h = 0; h < 2; h++ ) {
		int grip;
		if ( vrbRemote ) {
			grip = vrbRemote->hand[h].grip;
		} else if ( vr && vr->handValid[h] && vrb.sentTime[h] && cg.time - vrb.sentTime[h] >= 0 &&
			cg.time - vrb.sentTime[h] < VRB_STAND_MS ) {
			grip = vrb.sent[h].grip;	// (what it held last frame)
		} else {
			continue;
		}
		if ( grip != VR_GRIP_SABER && grip != VR_GRIP_HILT && grip != VR_GRIP_GUN ) {
			continue;
		}
		CG_VRB_Controller( vr, h, o, ax );
		VectorAdd( out, o, out );
		n++;
	}
	if ( !n ) {
		return qfalse;
	}
	VectorScale( out, 1.0f / n, out );
	return qtrue;
}

/*
How far the chest turns off the hips this frame (degrees, to the left;
eased - again in the same frame, no time has passed, nothing changes), bent
w: looking further to a side than the neck turns easily (CG_VRB_NeckEasy),
the rest of the way, up to VRB_CHEST_TURN; and toward the hands holding a
weapon - part of their way round from straight ahead of it, once they're
out from the body (as a swing takes them, or a reach; not hanging at its
sides), as far round as its side (less and less of it as they go on round
behind it).
*/
static float CG_VRB_ChestTwist( const vrClientState_t *vr, vec3_t look[3], const vrbBendRest_t *r, float bodyYaw, float w ) {
	float want = 0.0f;

	if ( look && w > 0.0f ) {
		const float off = AngleSubtract( VR_AxisYaw( look ), bodyYaw );
		const float past = Com_Clamp( 0.0f, VRB_CHEST_TURN, fabsf( off ) - CG_VRB_NeckEasy() );
		vec3_t hands;
		want = vrb.lookIn * ( ( off < 0.0f ) ? -past : past );
		if ( CG_VRB_WeaponHands( vr, hands ) ) {
			vec3_t rel;
			float x, y, aside;
			VectorSubtract( hands, r->at[VRB_B_CHEST], rel );
			x = DotProduct( rel, r->face[0] );
			y = DotProduct( rel, r->face[1] );
			aside = RAD2DEG( atan2f( y, x ) );	// (how far round from straight ahead)
			want += VRB_ARMS_TURN * aside * VRB_SmoothStep( VRB_ARMS_NEAR, VRB_ARMS_OUT, sqrtf( x * x + y * y ) ) *
				( 1.0f - VRB_SmoothStep( VRB_ARMS_SIDE, VRB_ARMS_BEHIND, fabsf( aside ) ) );
		}
		want = w * Com_Clamp( -VRB_CHEST_TURN, VRB_CHEST_TURN, want );
	}
	if ( cg.time - vrb.chestTime < 0 || cg.time - vrb.chestTime > 500 || Q_isnan( vrb.chestTwist ) ) {
		vrb.chestTwist = want;	// (starting over)
	} else {
		vrb.chestTwist += ( want - vrb.chestTwist ) * ( 1.0f - expf( -( cg.time - vrb.chestTime ) * 0.001f / VRB_CHEST_EASE ) );
	}
	vrb.chestTime = cg.time;
	return vrb.chestTwist;
}

// How much higher than the floor the body stands on (floorZ) the ground
// under a foot is (0: there's none near).
static float CG_VRB_GroundUnder( centity_t *cent, const vec3_t foot, float floorZ ) {
	static const vec3_t mins = { -2.0f, -2.0f, 0.0f }, maxs = { 2.0f, 2.0f, 1.0f };
	trace_t tr;
	vec3_t start, end;

	VectorSet( start, foot[0], foot[1], floorZ + VRB_STEP_UP );
	VectorSet( end, foot[0], foot[1], floorZ - VRB_STEP_DOWN );
	CG_Trace( &tr, start, mins, maxs, end, cent->currentState.number, MASK_PLAYERSOLID );
	if ( tr.startsolid || tr.allsolid || tr.fraction >= 1.0f || tr.plane.normal[2] < 0.7f ) {
		return 0.0f;
	}
	return tr.endpos[2] - floorZ;
}

// The ground under each foot (vrb.footDelta, eased - again in the same
// frame, nothing changes), the body moved up or down by shiftZ
// (CG_VR_BodyPose) - and lifted by its scale (CG_VRB_ScaleLift): the floor
// is where the player stands.
static void CG_VRB_FeetGround( centity_t *cent, const vrbBendRest_t *r, float shiftZ ) {
	const float floorZ = cent->lerpOrigin[2] - CG_VRB_ScaleLift( cent ) - shiftZ + DEFAULT_MINS_2;
	const qboolean fresh = (qboolean)( cg.time - vrb.footTime >= 0 && cg.time - vrb.footTime <= 500 );
	int s;

	for ( s = 0; s < 2; s++ ) {
		const float want = CG_VRB_GroundUnder( cent, r->at[vrbThigh[s] + 2], floorZ );
		if ( fresh && !Q_isnan( vrb.footDelta[s] ) ) {
			vrb.footDelta[s] += ( want - vrb.footDelta[s] ) * ( 1.0f - expf( -( cg.time - vrb.footTime ) * 0.001f / VRB_FOOT_EASE ) );
		} else {
			vrb.footDelta[s] = want;	// (starting over)
		}
	}
	vrb.footTime = cg.time;
}

// Two bones from a hip at hip to a foot at foot (within reach), as long as
// they are: where the knee goes, bending the way pole points.
static void VRB_Knee( const vec3_t hip, const vec3_t foot, float thigh, float shin, const vec3_t pole, vec3_t knee ) {
	vec3_t d, p;
	float len, c;

	VectorSubtract( foot, hip, d );
	len = VectorNormalize( d );
	c = ( len > 1e-3f ) ? Com_Clamp( -1.0f, 1.0f, ( thigh * thigh + len * len - shin * shin ) / ( 2.0f * thigh * len ) ) : 1.0f;
	VectorMA( pole, -DotProduct( pole, d ), d, p );
	if ( VectorNormalize( p ) < 1e-4f ) {
		PerpendicularVector( p, d );
	}
	VectorScale( d, thigh * c, knee );
	VectorMA( knee, thigh * sqrtf( 1.0f - c * c ), p, knee );
	VectorAdd( knee, hip, knee );
}

// The inverse of a rigid model space transform.
static void VRB_Invert( const mdxaBone_t *a, mdxaBone_t *out ) {
	mdxaBone_t t;
	int r, c;

	for ( r = 0; r < 3; r++ ) {
		for ( c = 0; c < 3; c++ ) {
			t.matrix[r][c] = a->matrix[c][r];
		}
	}
	for ( r = 0; r < 3; r++ ) {
		t.matrix[r][3] = -( t.matrix[r][0] * a->matrix[0][3] + t.matrix[r][1] * a->matrix[1][3] + t.matrix[r][2] * a->matrix[2][3] );
	}
	*out = t;
}

/*
=================
CG_VRB_Bend

Bends the body being posed (vrb; vrbRemote: another player's) w of the way
(0: upright, as animated): its eyes to eyesTo (where they're to go: NULL,
not bent), its head looking along look (NULL: as animated - the game turns
it, CG_VR_BodyLook), the body facing bodyYaw (as the game turned its chest
and head: CG_VR_BodyAngles).
Another player's head turns all the way, bent or not (it looks where
theirs does); yours comes in with the bend (it isn't drawn: it only puts
your eyes where they go). The trunk bends to take w of what the head's turn
does to where the eyes are, the body moved whole the rest.
=================
*/
static void CG_VRB_Bend( centity_t *cent, const vrClientState_t *vr, const vec3_t eyesTo, vec3_t look[3], float bodyYaw, float w ) {
	vrbBendRest_t r;
	vrbMat_t turn[4], head, neck, R[VRB_NUM_BEND_BONES];
	vec3_t joint[4], delta, feet[2], eRest, eTurned, neckTo, to[VRB_NUM_BEND_BONES], from[VRB_NUM_BEND_BONES];
	mdxaBone_t G[VRB_NUM_BEND_BONES];
	const float shiftZ = ( vrb.shiftedEnt == cent ) ? vrb.shift[2] : 0.0f;
	const qboolean lately = (qboolean)( cg.time - vrb.bendMovesTime > 0 && cg.time - vrb.bendMovesTime <= 500 &&
		!Q_isnan( vrb.bendA ) && !Q_isnan( vrb.bendB ) );
	float a = lately ? vrb.bendA : 0.0f, b = lately ? vrb.bendB : 0.0f, tau, share;
	int k, s, pass;

	if ( vrb.bendMovesTime == cg.time ) {
		// (the other eye: as the first one bent it - the same frame - on the
		// model as it is now)
		for ( k = 0; k < VRB_NUM_BEND_BONES; k++ ) {
			trap->ext.VR_G2SetBoneTransform( cent->ghoul2, 0, vrbBendBones[k], &vrb.bendMoves[k] );
		}
		vrb.bendG2 = cent->ghoul2;
		return;
	}
	CG_VRB_ResetBody( cent->ghoul2 );
	if ( !CG_VRB_BendRest( cent, bodyYaw, &r ) ) {
		return;
	}
	if ( !eyesTo ) {
		w = 0.0f;
	}
	share = vrbRemote ? 1.0f : w;
	tau = DEG2RAD( CG_VRB_ChestTwist( vr, look, &r, bodyYaw, w ) );
	VectorClear( delta );
	VectorSubtract( r.eyes, r.at[VRB_B_NECK], eRest );

	// each foot where it stands, on the ground under it - not where the body
	// is moved up or down to (its eyes with the view, as it eases over a step)
	if ( w > 0.0f ) {
		CG_VRB_FeetGround( cent, &r, shiftZ );
	}
	for ( s = 0; s < 2; s++ ) {
		VectorCopy( r.at[vrbThigh[s] + 2], feet[s] );
		feet[s][2] += w * ( vrb.footDelta[s] - shiftZ );
	}

	// the trunk: twisted, then leaned and lowered to put the neck where the
	// eyes, with the head turned as it looks, need it - twice, the head's
	// turn kept within the neck's reach of the chest as it ends up (w of
	// what the head's turn does to the eyes, so nothing jumps as it eases in)
	VRB_Trunk( &r, 0.0f, 0.0f, tau, delta, joint, turn );
	for ( pass = 0; w > 0.0f && pass < 2; pass++ ) {
		VRB_HeadTurn( &r, look, turn[3], share, vrb.lookIn, head, neck );
		VRB_EyesFromNeck( &r, head, neck, eTurned );
		VectorSubtract( eTurned, eRest, neckTo );
		VectorMA( eRest, w, neckTo, neckTo );
		VectorSubtract( eyesTo, neckTo, neckTo );
		VRB_SolveTrunk( &r, neckTo, tau, feet, &a, &b, delta );
		VRB_Trunk( &r, a, b, tau, delta, joint, turn );
		VectorCopy( neckTo, vrb.bendNeck );
	}
	if ( w <= 0.0f ) {
		a = b = 0.0f;	// (not leaned)
	}
	VRB_HeadTurn( &r, look, turn[3], share, vrb.lookIn, head, neck );

	// ...and the body moved whole, its feet with it, the rest of the way
	// across (not bent, all of it): its eyes over where they go, the head
	// turned. Not up or down - its feet stay on the floor, and a nod takes
	// the eyes down, as the game's own head turn does.
	if ( eyesTo ) {
		vec3_t move;
		VRB_EyesFromNeck( &r, head, neck, eTurned );
		VectorSubtract( eRest, eTurned, move );
		VectorScale( move, 1.0f - w, move );
		move[2] = 0.0f;
		VectorAdd( delta, move, delta );
		for ( s = 0; s < 2; s++ ) {
			VectorAdd( feet[s], move, feet[s] );
		}
		VRB_Trunk( &r, a, b, tau, delta, joint, turn );
	}

	// every bone's move: the pelvis about the hips, each bone of the back,
	// the neck and the head about its joint, to where its joint goes
	memcpy( R[VRB_B_PELVIS], turn[0], sizeof( vrbMat_t ) );
	VectorCopy( r.hips, from[VRB_B_PELVIS] );
	VectorAdd( r.hips, delta, to[VRB_B_PELVIS] );
	for ( k = 1; k < 4; k++ ) {
		memcpy( R[k], turn[k], sizeof( vrbMat_t ) );
		VectorCopy( r.at[k], from[k] );
		VectorCopy( joint[k - 1], to[k] );
	}
	memcpy( R[VRB_B_NECK], neck, sizeof( vrbMat_t ) );
	VectorCopy( r.at[VRB_B_NECK], from[VRB_B_NECK] );
	VectorCopy( joint[3], to[VRB_B_NECK] );
	memcpy( R[VRB_B_HEAD], head, sizeof( vrbMat_t ) );
	VectorCopy( r.at[VRB_B_HEAD], from[VRB_B_HEAD] );
	VectorSubtract( r.at[VRB_B_HEAD], r.at[VRB_B_NECK], to[VRB_B_HEAD] );
	VRB_Apply( neck, to[VRB_B_HEAD], to[VRB_B_HEAD] );
	VectorAdd( joint[3], to[VRB_B_HEAD], to[VRB_B_HEAD] );

	// the legs: from the hips where they went to the feet, the knees bending
	// the way they point as animated (each foot as it was, only moved) - or,
	// not bent, along with the pelvis
	for ( s = 0; s < 2; s++ ) {
		const int t = vrbThigh[s];
		const float reach = 0.999f * ( r.thigh[s] + r.shin[s] );
		vec3_t hip, foot, knee, pole, d, u;
		vrbMat_t arc;
		float len;
		if ( w <= 0.0f ) {
			for ( k = 0; k < 3; k++ ) {
				memcpy( R[t + k], R[VRB_B_PELVIS], sizeof( vrbMat_t ) );
				VectorCopy( from[VRB_B_PELVIS], from[t + k] );
				VectorCopy( to[VRB_B_PELVIS], to[t + k] );
			}
			continue;
		}
		VectorSubtract( r.at[t], r.hips, hip );
		VRB_Apply( turn[0], hip, hip );
		VectorAdd( hip, to[VRB_B_PELVIS], hip );
		VectorSubtract( feet[s], hip, d );
		len = VectorLength( d );
		if ( len > reach ) {
			VectorMA( hip, reach / len, d, foot );	// (out of reach: as near as it gets)
		} else {
			VectorCopy( feet[s], foot );
		}
		VectorSubtract( r.at[t + 2], r.at[t], u );
		VectorNormalize( u );
		VectorSubtract( r.at[t + 1], r.at[t], pole );
		VectorMA( pole, -DotProduct( pole, u ), u, pole );
		if ( VectorLength( pole ) < 0.2f ) {
			CrossProduct( r.hipLine, r.face[2], pole );	// (a leg dead straight as animated: forward, the way its hip joints face)
		}
		VRB_Knee( hip, foot, r.thigh[s], r.shin[s], pole, knee );
		// the thigh swung from where it pointed to the knee, the shin with it
		// and then to the foot, the foot not turned at all
		VectorSubtract( r.at[t + 1], r.at[t], d );
		VectorSubtract( knee, hip, u );
		VectorNormalize( d );
		VectorNormalize( u );
		VRB_Arc( d, u, R[t] );
		VectorSubtract( r.at[t + 2], r.at[t + 1], d );
		VRB_Apply( R[t], d, d );
		VectorSubtract( foot, knee, u );
		VectorNormalize( d );
		VectorNormalize( u );
		VRB_Arc( d, u, arc );
		VRB_Mul( arc, R[t], R[t + 1] );
		VRB_Identity( R[t + 2] );
		for ( k = 0; k < 3; k++ ) {
			VectorCopy( r.at[t + k], from[t + k] );
		}
		VectorCopy( hip, to[t] );
		VectorCopy( knee, to[t + 1] );
		VectorCopy( foot, to[t + 2] );
	}

	// on the bones: each one's move with its parent's taken back out
	for ( k = 0; k < VRB_NUM_BEND_BONES; k++ ) {
		CG_VRB_ModelMove( cent, R[k], from[k], to[k], &G[k] );
	}
	for ( k = 0; k < VRB_NUM_BEND_BONES; k++ ) {
		mdxaBone_t inv;
		if ( vrbBendParent[k] >= 0 ) {
			VRB_Invert( &G[vrbBendParent[k]], &inv );
			VRB_Compose( &G[k], &inv, &vrb.bendMoves[k] );
		} else {
			vrb.bendMoves[k] = G[k];
		}
		trap->ext.VR_G2SetBoneTransform( cent->ghoul2, 0, vrbBendBones[k], &vrb.bendMoves[k] );
	}
	vrb.bendG2 = cent->ghoul2;
	vrb.bendMovesTime = vrb.bendOkTime = cg.time;
	vrb.bendA = a;
	vrb.bendB = b;

	// (the log, the tests)
	vrb.bendLean = RAD2DEG( a );
	vrb.bendSide = RAD2DEG( b );
	vrb.bendTwist = RAD2DEG( tau );
	vrb.bendDrop = -delta[2];
	if ( !vrbRemote && cg_vrLog.integer && cg.time - vrb.bendLogTime >= 2000 ) {
		vec3_t eyes;
		vrb.bendLogTime = cg.time;
		CG_VRB_Eyes( cent, eyes );
		CG_VRB_Log( "bend: %.2f, leaned %+.0f forward %+.0f left, chest turned %+.0f, hips down %.1f back %.1f, "
			"eyes %.1f from where they go, feet %+.1f %+.1f\n", w, vrb.bendLean, vrb.bendSide, vrb.bendTwist,
			vrb.bendDrop, -DotProduct( delta, r.face[0] ), eyesTo ? Distance( eyes, eyesTo ) : 0.0f,
			vrb.footDelta[VR_HAND_LEFT], vrb.footDelta[VR_HAND_RIGHT] );
	}
}

// The body being posed bends (before its arms are solved: they reach from
// where it puts the shoulders) - yours, from your headset; another VR
// player's (vrbRemote: their avatar, in the world), from theirs.
static void CG_VRB_BendBody( centity_t *cent, const vrClientState_t *vr ) {
	vec3_t look[3], head;
	float bodyYaw, w;
	qboolean haveLook;

	if ( !cent->ghoul2 || !trap->ext.VR_G2SetBoneTransform ) {
		return;
	}
	if ( !cg_vrBodyBend.integer ) {
		CG_VRB_ClearBody( cent->ghoul2 );	// (upright, as before)
		return;
	}
	if ( vrbRemote ) {
		AnglesToAxis( vrbRemote->headAngles, look );
		haveLook = qtrue;
		bodyYaw = vrbRemote->bodyYaw;
	} else {
		haveLook = CG_VR_HeadPose( head, look );
		bodyYaw = CG_VRB_BodyYaw( vr );
	}
	w = CG_VRB_BendNow();
	CG_VRB_Bend( cent, vr, ( vrb.bendEyesTime == cg.time ) ? vrb.bendEyes : NULL, haveLook ? look : NULL, bodyYaw, w );
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
	if ( a->forceValid && b->forceValid && a->forceHand == b->forceHand ) {
		VRB_Slerp( a->forceDir, b->forceDir, f, out->forceDir );	// (where their Force hand points, between)
	}
	out->bodyYaw = LerpAngle( a->bodyYaw, b->bodyYaw, f );
	for ( i = 0; i < 3; i++ ) {
		out->headAngles[i] = LerpAngle( a->headAngles[i], b->headAngles[i], f );
		out->shift[i] = a->shift[i] + f * ( b->shift[i] - a->shift[i] );
		out->head[i] = a->head[i] + f * ( b->head[i] - a->head[i] );
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
		if ( ha->fingersValid && hb->fingersValid ) {
			for ( i = 0; i < VR_NUM_FINGERS; i++ ) {
				ho->fingers[i] = ha->fingers[i] + f * ( hb->fingers[i] - ha->fingers[i] );
			}
		}
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

// How far the bend has a VR body's head this frame (0..1; eased - again in
// the same frame, no time passes, nothing changes): it turns it itself, the
// neck first (CG_VRB_OwnLook, CG_VRB_Bend); the game the rest of the way
// (CG_VR_BodyLook), as it's knocked down, say, or gets up. Body Bends
// turned off, none of it at once.
static float CG_VRB_LookIn( centity_t *cent, vrbState_t *st ) {
	const float want = CG_VRB_OwnLook( cent ) ? 1.0f : 0.0f;
	const float dt = Com_Clamp( 0.0f, 0.1f, ( cg.time - st->lookInTime ) * 0.001f );

	if ( !cg_vrBodyBend.integer || cg.time - st->lookInTime < 0 || cg.time - st->lookInTime > 500 ) {
		st->lookIn = want;	// (starting over)
	} else if ( want > st->lookIn ) {
		st->lookIn = Q_min( want, st->lookIn + dt / VRB_LOOK_EASE );
	} else {
		st->lookIn = Q_max( want, st->lookIn - dt / VRB_LOOK_EASE );
	}
	st->lookInTime = cg.time;
	return st->lookIn;
}

// Where the game turns a VR body's head (CG_G2PlayerAngles, from the way the
// body faces): another VR player's, where theirs looks - the game would turn
// it toward their aim, or whoever is near. With Body Bends the body turns its
// head itself (the neck first: CG_VRB_Bend) - yours too - so the game turns
// it only the rest of the way, as the bend takes it over or hands it back
// (CG_VRB_LookIn), and at once. qfalse: the game's own look.
qboolean CG_VR_BodyLook( centity_t *cent, vec3_t lookAngles ) {
	const int n = cent->currentState.number;
	vec3_t ahead;
	float own;
	int i;

	if ( n == cg.clientNum ) {
		const vrClientState_t *vr = CG_VR();
		if ( !vr || !CG_VR_BodyActive( cent ) ) {
			return qfalse;
		}
		own = CG_VRB_LookIn( cent, &vrbSelf );
		if ( own <= 0.0f ) {
			return qfalse;
		}
		VectorSet( ahead, 0.0f, CG_VRB_BodyYaw( vr ), 0.0f );	// (CG_VR_BodyAngles)
	} else if ( !CG_VRB_OtherUsable( cent ) ) {
		return qfalse;
	} else {
		own = CG_VRB_LookIn( cent, &vrbOthers[n] );
		VectorCopy( vrbOthers[n].avatar.headAngles, lookAngles );
		if ( own <= 0.0f ) {
			return qtrue;
		}
		VectorSet( ahead, 0.0f, vrbOthers[n].avatar.bodyYaw, 0.0f );
	}
	for ( i = 0; i < 3; i++ ) {
		lookAngles[i] = LerpAngle( lookAngles[i], ahead[i], own );
	}
	VectorClear( cgs.clientinfo[n].lastHeadAngles );	// (not easing there from where it had it)
	return qtrue;
}

// Another VR player's body stood under their head (undone in
// CG_VR_BodyEndFrame): its eyes go where their game put their body's eyes
// (vrAvatar_t.head) - moved whole as far as their game moved it, and bent the
// rest of the way (CG_VRB_Bend); not bent here (no Body Bends, or not
// standing still), moved whole all the way across, and up and down as far as
// their game moved it (its feet on the floor as theirs are: the animation
// here runs a little ahead of theirs or behind) - and down to their eyes,
// crouched with their body bent.
static void CG_VRB_OtherPose( centity_t *cent, refEntity_t *legs ) {
	const int n = cent->currentState.number;
	vec3_t eyes, full;

	if ( !CG_VRB_OtherUsable( cent ) ) {
		return;
	}
	vrbCur = &vrbOthers[n];
	CG_VRB_Init();
	CG_VRB_ResetArms( cent->ghoul2 );	// (the animated pose, without last frame's arm overrides - nor its bend: CG_VR_BodyAngles)
	vrb.armsG2 = cent->ghoul2;
	vrbRemote = &vrb.avatar;
	CG_VRB_HideHolstered( cent, cgs.clientinfo[n].vrHasHolsters );	// (their sabers in their holsters: cg_vrholster.c)
	vrbRemote = NULL;
	VectorCopy( vrb.avatar.shift, vrb.shift );
	VectorAdd( cent->lerpOrigin, vrb.avatar.head, vrb.bendEyes );
	vrb.bendEyes[2] -= CG_VRB_ScaleLift( cent );
	// (an older build's demo: no head - moved as their game moved it, not bent)
	vrb.bendEyesTime = VectorCompare( vrb.avatar.head, vec3_origin ) ? 0 : cg.time;
	if ( vrb.bendEyesTime && CG_VRB_Eyes( cent, eyes ) ) {
		const float w = CG_VRB_BendWeight( cent->currentState.groundEntityNum != ENTITYNUM_NONE );
		const float len = sqrtf( ( vrb.bendEyes[0] - eyes[0] ) * ( vrb.bendEyes[0] - eyes[0] ) +
			( vrb.bendEyes[1] - eyes[1] ) * ( vrb.bendEyes[1] - eyes[1] ) );
		VectorSubtract( vrb.bendEyes, eyes, full );
		if ( len > 48.0f ) {
			full[0] *= 48.0f / len;	// (as far as theirs ever goes: VRS_TakeAvatar)
			full[1] *= 48.0f / len;
		}
		full[2] = vrb.avatar.shift[2] + Com_Clamp( -VRB_DROP_MAX, 0.0f, full[2] - vrb.avatar.shift[2] );
		VectorSubtract( vrb.avatar.shift, full, vrb.shift );
		VectorMA( full, w, vrb.shift, vrb.shift );
	}
	VectorAdd( cent->lerpOrigin, vrb.shift, cent->lerpOrigin );
	VectorCopy( cent->lerpOrigin, legs->origin );
	VectorCopy( cent->lerpOrigin, legs->oldorigin );
	VectorCopy( cent->lerpOrigin, legs->lightingOrigin );
	vrb.shiftedEnt = cent;
	vrbCur = &vrbSelf;
}

// The avatar of the body being posed, its hands in the world: from where
// the player is drawn now - after the game's own smoothing moved them, and
// without the body's shift (CG_VRB_OtherPose) or a scaled model's lift.
static void CG_VRB_AvatarInWorld( centity_t *cent, vrAvatar_t *world ) {
	vec3_t base;
	int h;

	*world = vrb.avatar;
	VectorCopy( cent->lerpOrigin, base );
	if ( vrb.shiftedEnt == cent ) {
		VectorSubtract( base, vrb.shift, base );
	}
	base[2] -= CG_VRB_ScaleLift( cent );
	for ( h = 0; h < 2; h++ ) {
		if ( world->hand[h].grip != VR_GRIP_NONE ) {
			VectorAdd( world->hand[h].origin, base, world->hand[h].origin );
		}
	}
}

// Another VR player's sabers (vrb, vrbRemote: theirs): one gone over to
// their other hand lies in it turned about itself as it was last drawn - as
// the hand it was drawn in has it now (as their own game has it,
// CG_VRB_PassSaber). Not drawn lately (put away, say), or that hand off its
// controller now, as the hand's tag hangs it.
static void CG_VRB_OtherFaces( void ) {
	int s, i;

	for ( s = 0; s < MAX_SABERS; s++ ) {
		const int h = CG_VRB_SaberHand( s );
		if ( h < 0 ) {
			continue;
		}
		if ( cg.time - vrb.drawnTime[s] < 0 || cg.time - vrb.drawnTime[s] > VRB_FACE_MS ) {
			vrb.hold[s].keptFace = qfalse;
		} else if ( h != vrb.drawnHand[s] ) {
			vec3_t from[3], ax[3];
			for ( i = 0; i < 3; i++ ) {
				VectorCopy( vrbRemote->hand[h ^ 1].axis[i], from[i] );	// (no hand on it: no axes)
				VectorCopy( vrbRemote->hand[h].axis[i], ax[i] );
			}
			CG_VRB_KeepFace( s, h, h ^ 1, from, ax );
		}
	}
}

// Another VR player's arms (vrb: theirs) from their avatar, in the world.
static void CG_VRB_PoseOther( centity_t *cent, const vrAvatar_t *world ) {
	vrClientState_t vr;
	int h;

	vrbRemote = world;
	if ( cg_vrBodyArms.integer ) {
		memset( &vr, 0, sizeof( vr ) );
		vr.active = qtrue;
		for ( h = 0; h < 2; h++ ) {
			const vrAvatarHand_t *ah = &world->hand[h];
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
		CG_VRB_OtherFaces();
		// a hand on the other hand's hilt
		vrb.twoHandTime = cg.time;
		vrb.twoHandOff = ( world->hand[VR_HAND_LEFT].grip == VR_GRIP_HILT ) ? VR_HAND_LEFT :
			( world->hand[VR_HAND_RIGHT].grip == VR_GRIP_HILT ) ? VR_HAND_RIGHT : -1;
		vrb.twoHand = vrb.twoHandArms = ( vrb.twoHandOff >= 0 ) ? qtrue : qfalse;
		vrb.twoHandEase = vrb.twoHand ? world->hand[vrb.twoHandOff].blend : 0.0f;
		CG_VRB_Arms( cent, &vr, vrb.avatar.bodyYaw );
	} else {
		CG_VRB_ResetArms( cent->ghoul2 );
		CG_VRB_Fist( cent, VR_HAND_LEFT, qfalse );
		CG_VRB_Fist( cent, VR_HAND_RIGHT, qfalse );
		CG_VRB_UnboltHands( cent );	// (no hands to hold them: as the game holds them)
	}
	vrbRemote = NULL;
}

// Another VR player's arms: the hands where theirs are, holding what they
// hold - the same solver as yours, their avatar standing in for the headset.
static void CG_VRB_OtherArms( centity_t *cent ) {
	const int n = cent->currentState.number;
	vrAvatar_t world;

	if ( !CG_VRB_OtherUsable( cent ) ) {
		return;
	}
	vrbCur = &vrbOthers[n];
	CG_VRB_Init();
	CG_VRB_AvatarInWorld( cent, &world );
	vrbRemote = &world;
	CG_VRB_BendBody( cent, NULL );	// (first: the arms reach from where it puts the shoulders)
	vrbRemote = NULL;
	CG_VRB_PoseOther( cent, &world );
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

// This body's animations.
static const animation_t *CG_VRB_Anims( void ) {
	const centity_t *cent = &cg_entities[cg.predictedPlayerState.clientNum];
	return ( cent->localAnimIndex >= 0 && cent->localAnimIndex < MAX_ANIM_FILES ) ? bgAllAnims[cent->localAnimIndex].anims : NULL;
}

// In a pose held (not getting down or up), where the body's eyes are against
// the eye point; out of it, back to nothing. Eased, so the camera glides.
static void CG_VRB_PoseEyes( const centity_t *cent, const vec3_t eyes ) {
	const float dt = Com_Clamp( 0.0f, 0.1f, ( cg.time - vrb.poseEyeTime ) * 0.001f );

	if ( BG_VRPosePhase( &cg.predictedPlayerState, CG_VRB_Anims(), NULL, NULL ) == VRPHASE_HELD ) {
		vrb.poseEyeTarget = Com_Clamp( -VRB_POSE_EYE_MAX, VRB_POSE_EYE_MAX,
			eyes[2] - ( cent->lerpOrigin[2] - CG_VRB_ScaleLift( cent ) + cg.predictedPlayerState.viewheight ) );
	} else {
		vrb.poseEyeTarget = 0.0f;
	}
	if ( cg.time - vrb.poseEyeTime < 0 || cg.time - vrb.poseEyeTime > 500 ) {
		vrb.poseEye = 0.0f;	// (starting over)
	}
	vrb.poseEye += ( vrb.poseEyeTarget - vrb.poseEye ) * ( 1.0f - expf( -dt / 0.25f ) );
	vrb.poseEyeTime = cg.time;
}

// Whether you're in a pose (getting down into it, or up out of it), as your
// own game predicts it.
qboolean CG_VR_Posed( void ) {
	return (qboolean)( BG_VRPosePhase( &cg.predictedPlayerState, CG_VRB_Anims(), NULL, NULL ) != VRPHASE_NONE );
}

float CG_VR_PoseEyeAdjust( void ) {
	if ( !CG_VR() || !cg_vrBody.integer || cg.predictedPlayerState.clientNum != cg.clientNum ) {
		return 0.0f;
	}
	return vrbSelf.poseEye;
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
	vec3_t eyes, head, headAxis[3], fwd, shift;
	float yaw, len, duck;
	qboolean haveEyes;

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

	// the animated pose, without last frame's arm overrides (nor its bend:
	// CG_VR_BodyAngles, as the game turned the body)
	CG_VRB_ResetArms( cent->ghoul2 );
	vrb.armsG2 = cent->ghoul2;

	// Stand the body under the headset: sideways, the model's eyes go just
	// behind yours (so the neck stays out of view); up and down, the body
	// rides along with the camera's own smoothing (stairs, landings) and
	// sinks when you crouch for real. Its height otherwise stays the
	// model's own, feet on the ground. Standing still with Body Bends, its
	// feet stay put instead - where they'd be with your head at the view's
	// own place - and it bends to put its eyes there (CG_VRB_Bend).
	haveEyes = CG_VRB_Eyes( cent, eyes );
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
		const float w = CG_VRB_BendWeight( cg.predictedPlayerState.groundEntityNum != ENTITYNUM_NONE );
		vec3_t full, stand;
		float ride;

		VectorSet( fwd, cosf( DEG2RAD( yaw ) ), sinf( DEG2RAD( yaw ) ), 0.0f );
		VectorMA( head, vrb.lean - cg_vrBodyBack.value, fwd, head );
		VectorSubtract( head, eyes, full );
		full[2] = 0.0f;
		len = VectorLength( full );
		if ( len > 48.0f ) {
			VectorScale( full, 48.0f / len, full );	// never wander off
		}

		duck = 0.0f;	// the camera's duck smoothing: the crouch animation does that
		if ( cg.time - cg.duckTime >= 0 && cg.time - cg.duckTime < DUCK_TIME ) {
			duck = cg.duckChange * ( DUCK_TIME - ( cg.time - cg.duckTime ) ) / DUCK_TIME;
		}
		// in a pose the camera goes to the body's eyes (CG_VR_PoseEyeAdjust):
		// the body stays put, on the floor
		CG_VRB_PoseEyes( cent, eyes );
		ride = cg.refdef.vieworg[2] + duck - vrb.poseEye - ( cent->lerpOrigin[2] - CG_VRB_ScaleLift( cent ) + cg.predictedPlayerState.viewheight );
		full[2] = Com_Clamp( -48.0f, 48.0f, ride + Q_min( 0.0f, head[2] - cg.refdef.vieworg[2] ) );

		// (bent: the feet where they'd be with your head at the view's place,
		// riding along with it all the same; the eyes still go where they
		// go - everyone else's game puts them there too, CG_VRB_FillAvatar)
		VectorMA( cg.refdef.vieworg, -cg_vrBodyBack.value, fwd, stand );
		VectorSubtract( stand, eyes, stand );
		stand[2] = 0.0f;
		len = VectorLength( stand );
		if ( len > 48.0f ) {
			VectorScale( stand, 48.0f / len, stand );
		}
		stand[2] = Com_Clamp( -48.0f, 48.0f, ride );
		VectorAdd( eyes, full, vrb.bendEyes );
		vrb.bendEyesTime = cg.time;
		VectorSubtract( stand, full, shift );
		VectorMA( full, w, shift, shift );

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
	CG_VRB_BendBody( cent, vr );	// (first: the arms reach from where it puts the shoulders)
	if ( cg_vrBodyArms.integer ) {
		CG_VRB_Arms( cent, vr, CG_VRB_BodyYaw( vr ) );
		CG_VRB_LogHands( cent );
	} else {
		CG_VRB_ResetArms( cent->ghoul2 );
		CG_VRB_Fist( cent, VR_HAND_LEFT, qfalse );	// (no hands to hold the hilt with)
		CG_VRB_Fist( cent, VR_HAND_RIGHT, qfalse );
		CG_VRB_UnboltHands( cent );					// (...nor the sabers: as the game holds them)
		vrb.twoHand = vrb.twoHandArms = qfalse;
		vrb.twoHandBlend = 0.0f;
		vrb.blade[0].valid = vrb.blade[1].valid = qfalse;
	}
}

// The whoosh of your swings. The stock game plays it from its attack
// animations, which a swing of your arm doesn't have: here it comes from how
// fast your blade really moves (two thirds along it, in your room), heavier
// for a heavier weight, with a light tick in the hand(s) holding it. A blade
// that feels heavier needs a little less (it can't move as fast: as the
// speeds its cuts need, CG_VRB_CutScale). (Everyone else hears it from the
// server, g_vrsaber.c.)
static const float vrbWhooshSpeed[3] = { 3.0f, 3.5f, 4.0f };	// m/s: light, medium, heavy

static void CG_VRB_SwingSound( const vrClientState_t *vr ) {
	const float dt = ( cg.time - vrb.swing.prevTime ) * 0.001f;
	const float yaw = vr ? CG_VRB_RoomYaw( vr ) : 0.0f;
	float speed = 0.0f, handSpeed[2] = { 0.0f, 0.0f };
	vec3_t where = { 0.0f, 0.0f, 0.0f };
	int s, b, mask[VR_POSE_MAX_SABERS], hand = vrb.hold[0].hand, weight;
	float whoosh;

	if ( !vr || vrbSaber.time != cg.time || !vrbSaber.pose.valid || vr->worldScale <= 0.0f ) {
		vrb.swing.prevMask[0] = vrb.swing.prevMask[1] = 0;
		vrb.swing.speed = vrb.swing.handSpeed[0] = vrb.swing.handSpeed[1] = 0.0f;
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
			if ( ( vrb.swing.prevMask[s] & ( 1 << b ) ) && dt > 0.001f && dt < 0.1f && CG_VRB_SaberHand( s ) >= 0 ) {
				const float v = Distance( room, vrb.swing.prev[s][b] ) / dt / vr->worldScale;
				const int h = CG_VRB_SaberHand( s );
				if ( v < 30.0f ) {	// (faster: it jumped - a respawn, a flip - rather than swung)
					handSpeed[h] = Q_max( handSpeed[h], v );
					if ( v > speed ) {
						speed = v;
						VectorCopy( p, where );
						hand = h;
					}
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
	for ( s = 0; s < 2; s++ ) {
		vrb.swing.handSpeed[s] += ( handSpeed[s] - vrb.swing.handSpeed[s] ) * Com_Clamp( 0.0f, 1.0f, dt / ( dt + 0.02f ) );
	}

	weight = CG_VRB_Weight( qtrue );
	whoosh = vrbWhooshSpeed[weight] * CG_VRB_CutScale();
	if ( !vrb.swing.armed ) {
		if ( vrb.swing.speed < 0.5f * whoosh ) {
			vrb.swing.armed = qtrue;	// slowed down: the next swing whooshes again
		} else if ( cg.time - vrb.swing.lastTime > 450 && vrb.swing.speed >= 1.3f * whoosh ) {
			vrb.swing.armed = qtrue;	// still swinging hard (spinning it)
		}
	}
	if ( vrb.swing.armed && vrb.swing.speed >= whoosh && cg.time - vrb.swing.lastTime > 150 ) {
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
		{
			static const float tick[3] = { 0.16f, 0.20f, 0.22f };
			CG_VRB_FeelBlade( hand, tick[weight], 20, 0.0f, 0, 0.0f );
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
	const int main = vrb.hold[0].hand, off = main ^ 1;
	vrbGrip_t grip;
	vrbBone_t tag;
	mdxaBone_t bm;
	vec3_t blade;
	float index, middle, other = -1.0f;

	if ( !cg_vrLog.integer || ( cg.time - vrb.logTime >= 0 && cg.time - vrb.logTime < 200 ) ) {
		return;
	}
	vrb.logTime = cg.time;
	CG_VRB_Grip( cent, main, &grip );
	if ( grip.kind != VRB_GRIP_SABER ) {
		vrb.logTorso = -1;
		return;
	}
	if ( !CG_VRB_Bone( cent, vrbArmBones[main][VRB_TAG], 1, &tag ) ||
		!trap->G2API_GetBoltMatrix( cent->ghoul2, grip.saberModel, 0, &bm, cent->turAngles, cent->lerpOrigin, cg.time,
			cgs.gameModels, cent->modelScale ) ) {
		return;
	}
	BG_GiveMeVectorFromMatrix( &bm, NEGATIVE_Y, blade );
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
	index = CG_VRB_LineDistance( cent, main == VR_HAND_RIGHT ? "r_d2" : "l_d2", tag.origin, blade );
	middle = CG_VRB_LineDistance( cent, main == VR_HAND_RIGHT ? "r_d3" : "l_d3", tag.origin, blade );
	if ( vrb.twoHand && vrb.twoHandTime == cg.time ) {
		other = CG_VRB_LineDistance( cent, off == VR_HAND_RIGHT ? "r_d2" : "l_d2", tag.origin, blade );
	}
	if ( ps->torsoAnim != vrb.logTorso || fabsf( index - vrb.logTip ) > 0.8f || cg.time - vrb.logPrinted > 3000 ||
		cg.time - vrb.logPrinted < 0 ) {
		CG_VRB_Log( "hand: torso %s, legs %s, saber move %i, style %i, holstered %i, hand extend %i | %s fingertips "
			"%.1f %.1f from the hilt (held %.1f along it%s)%s%s\n", CG_VRB_AnimName( ps->torsoAnim ), CG_VRB_AnimName( ps->legsAnim ),
			ps->saberMove, ps->fd.saberAnimLevel, ps->saberHolstered, ps->forceHandExtend, VRB_HandName( main ), index, middle,
			grip.hilt, grip.sign < 0.0f ? ", reversed" : "",
			other >= 0.0f ? va( " | two hands, %s fingertip %.1f", VRB_HandName( off ), other ) : "", ps->weaponTime > 0 ? " | busy" : "" );
		CG_VRB_Log( "fingers bent round the hilt (thumb, index, rest; degrees, closing +): right %+.0f %+.0f %+.0f (fist %.1f off its axis), "
			"left %+.0f %+.0f %+.0f (%.1f off) | saber moved into the fist: right %s, left %s\n",
			vrb.curlLog[VR_HAND_RIGHT][0], vrb.curlLog[VR_HAND_RIGHT][1], vrb.curlLog[VR_HAND_RIGHT][2], vrb.curlOff[VR_HAND_RIGHT],
			vrb.curlLog[VR_HAND_LEFT][0], vrb.curlLog[VR_HAND_LEFT][1], vrb.curlLog[VR_HAND_LEFT][2], vrb.curlOff[VR_HAND_LEFT],
			vrb.tagMoves[VR_HAND_RIGHT] ? "yes" : "no (its tag doesn't hang on the tag bone)",
			vrb.tagMoves[VR_HAND_LEFT] ? "yes" : "no (its tag doesn't hang on the tag bone)" );
		vrb.logTorso = ps->torsoAnim;
		vrb.logTip = index;
		vrb.logPrinted = cg.time;
	}
}

static int vrbGazeSent;	// cg.time your avatar - where your headset looks - last went with your pose (0: never)

// Whether the server sees where your headset looks: your avatar went with
// your pose lately, to a server that reads them (sv_vrx) - roleplay's
// getting someone up looks that way as well as the way you aim
// (cg_vrdown.c; the server's G_VRHeadGaze).
qboolean CG_VRB_GazeShared( void ) {
	if ( !vrbGazeSent || cg.time - vrbGazeSent > 500 || cg.time - vrbGazeSent < 0 ) {
		return qfalse;
	}
	return (qboolean)( atoi( Info_ValueForKey( CG_ConfigString( CS_SYSTEMINFO ), "sv_vrx" ) ) == VR_POSE_PROTOCOL );
}

// The hand holding your saber saberNum (the first in the hand you use, unless
// it was passed to the other) - cg_vrduel.c: which hand salutes. -1: none.
int CG_VR_SaberHand( int saberNum ) {
	if ( saberNum < 0 || saberNum >= MAX_SABERS || !vrbSelf.init ) {
		return -1;
	}
	return vrbSelf.hold[saberNum].hand;
}

/*
--------------------------------------------------------------------------------
Your hands, for your holsters (cg_vrholster.c)
--------------------------------------------------------------------------------
*/

void QDECL CG_VR_Log( const char *fmt, ... ) {
	char text[1024];
	va_list ap;

	va_start( ap, fmt );
	Q_vsnprintf( text, sizeof( text ), fmt, ap );
	va_end( ap );
	CG_VRB_Log( "%s", text );
}

// Your controller h's grip in the world (where your hand's put on it).
qboolean CG_VR_HandGrip( int h, vec3_t origin, vec3_t axis[3] ) {
	const vrClientState_t *vr = CG_VR();
	const vrAvatar_t *remote = vrbRemote;
	vec3_t ax[3];

	if ( !vr || h < VR_HAND_LEFT || h > VR_HAND_RIGHT || !vr->handValid[h] ) {
		return qfalse;
	}
	vrbRemote = NULL;
	CG_VRB_Controller( vr, h, origin, axis ? axis : ax );
	vrbRemote = remote;
	return qtrue;
}

// Your saber s into hand h (taken out of its holster): held hilt along it from
// where its tag holds it, the usual way round (sign 1) or reversed (-1).
void CG_VR_TakeSaber( int s, int h, float hilt, float sign ) {
	vrbState_t *st = &vrbSelf;

	if ( s < 0 || s >= MAX_SABERS || h < VR_HAND_LEFT || h > VR_HAND_RIGHT ) {
		return;
	}
	memset( &st->hold[s], 0, sizeof( st->hold[s] ) );
	st->hold[s].hand = h;
	st->hold[s].sign = ( sign < 0.0f ) ? -1.0f : 1.0f;
	st->hold[s].t = Com_Clamp( -VR_POSE_MAX_HILT, VR_POSE_MAX_HILT, hilt );
	st->blade[h].valid = qfalse;	// (its weight starts afresh in that hand)
	st->blade[h].lastTime = 0;
	st->letGo[h].time = 0;
}

// Your saber s back in its holster: the hand that held it lets go, easing off
// how it held it (as passing it on does).
void CG_VR_PutSaber( int s ) {
	vrbState_t *st = &vrbSelf;
	const int h = ( s >= 0 && s < MAX_SABERS ) ? st->hold[s].hand : -1;
	int i, k;

	if ( h < VR_HAND_LEFT || h > VR_HAND_RIGHT ) {
		return;
	}
	for ( i = 0; i < 3; i++ ) {
		for ( k = 0; k < 3; k++ ) {
			st->letGo[h].axis[i][k] = st->hold[s].keptGrip ? st->hold[s].keptGripAxis[i][k] : ( i == k ) ? 1.0f : 0.0f;
		}
	}
	if ( cg.time - st->heldTurnTime[h] >= 0 && cg.time - st->heldTurnTime[h] < 100 ) {
		memcpy( st->letGo[h].turn, st->heldTurn[h], sizeof( st->letGo[h].turn ) );
	} else {
		VRB_Identity( st->letGo[h].turn );
	}
	st->letGo[h].time = cg.time ? cg.time : 1;
	memset( &st->hold[s], 0, sizeof( st->hold[s] ) );
	st->hold[s].hand = -1;
	st->hold[s].sign = 1.0f;
	st->blade[h].valid = qfalse;
	st->blade[h].lastTime = 0;
	if ( st->twoHand ) {
		st->twoHand = st->twoHandArms = st->twoHandPass = qfalse;
		st->twoHandBlend = 0.0f;
	}
	if ( s == 0 ) {
		st->twoHandLent = qfalse;	// (put away while your body's not drawn, say: not lent as it comes out again)
	}
}

// Your sabers changed over (the server's made the second the first): the
// hands holding them, and how they were last drawn, change over with them.
void CG_VR_SwapSaberHolds( void ) {
	vrbState_t *st = &vrbSelf;
	vec3_t v;
	int t;

	{
		const size_t n = sizeof( st->hold[0] );
		byte tmp[sizeof( st->hold[0] )];
		memcpy( tmp, &st->hold[0], n );
		memcpy( &st->hold[0], &st->hold[1], n );
		memcpy( &st->hold[1], tmp, n );
	}
	VectorCopy( st->drawnBlade[0], v );
	VectorCopy( st->drawnBlade[1], st->drawnBlade[0] );
	VectorCopy( v, st->drawnBlade[1] );
	VectorCopy( st->drawnFace[0], v );
	VectorCopy( st->drawnFace[1], st->drawnFace[0] );
	VectorCopy( v, st->drawnFace[1] );
	t = st->drawnTime[0];
	st->drawnTime[0] = st->drawnTime[1];
	st->drawnTime[1] = t;
	t = st->drawnHand[0];
	st->drawnHand[0] = st->drawnHand[1];
	st->drawnHand[1] = t;
}

// The hands whose squeeze takes hold of your other hand's hilt (or passes
// it over) - as last offered, a moment ago at most (your body not drawn a
// while: none): those come first.
int CG_VR_TwoHandZones( void ) {
	return ( vrbSelf.init && cg.time - vrbSelf.twoHandTime >= 0 && cg.time - vrbSelf.twoHandTime <= 100 ) ? vrbSelf.grabZones : 0;
}

// Hand h's squeeze was a holster's (cg_vrholster.c): it doesn't take hold of
// your other hand's hilt too, until its grip is let go.
void CG_VR_BlockGrab( int h ) {
	if ( h >= VR_HAND_LEFT && h <= VR_HAND_RIGHT ) {
		vrbSelf.grabBlocked[h] = qtrue;
	}
}

// Whether you hold your first saber with both hands - as last worked out, a
// moment ago at most.
qboolean CG_VR_BothHandsHold( void ) {
	return (qboolean)( vrbSelf.init && vrbSelf.twoHand && cg.time - vrbSelf.twoHandTime >= 0 && cg.time - vrbSelf.twoHandTime <= 100 );
}

// Holding the grip (cg_vrholster.c), the first hand let go of your saber
// held with both: the other goes on holding it, once it's settled onto the
// hilt (CG_VRB_TwoHand). Not (pass: no): the first hand's taken hold again
// meanwhile - it stays in that one.
void CG_VR_PassSaberOver( qboolean pass ) {
	if ( !pass ) {
		vrbSelf.twoHandPass = qfalse;
	} else if ( CG_VR_BothHandsHold() ) {
		vrbSelf.twoHandPass = qtrue;
	}
}

// Whether another VR player's avatar has a hand on their saber s.
qboolean CG_VR_OtherHoldsSaber( int clientNum, int s ) {
	const vrbState_t *st;
	int h;

	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return qfalse;
	}
	st = &vrbOthers[clientNum];
	if ( !st->avatarValid ) {
		return qfalse;
	}
	for ( h = 0; h < 2; h++ ) {
		if ( st->avatar.hand[h].grip == VR_GRIP_SABER && st->avatar.hand[h].saber == s ) {
			return qtrue;
		}
	}
	return qfalse;
}

// The hand holding your weapon, for its trigger: a saber's as your body holds
// it lately; else the gun hand.
static int CG_VRB_WeaponHand( void ) {
	const int held = CG_VRH_WeaponHand();

	if ( held != -2 ) {
		return held;	// (holsters: -1 your hands empty, 2 a saber in each - cg_vrholster.c)
	}
	if ( cg.predictedPlayerState.weapon == WP_SABER && vrbSelf.init && vrbSelf.holdTime &&
		cg.time - vrbSelf.holdTime >= 0 && cg.time - vrbSelf.holdTime < 500 ) {
		return vrbSelf.hold[0].hand;
	}
	return CG_VR_GunHand();
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
	// where your body's eyes go (your headset, the neck just behind it:
	// CG_VR_BodyPose) - theirs bends your body to put them there too
	if ( vrb.bendEyesTime == cg.time ) {
		VectorSubtract( vrb.bendEyes, cg.refdef.vieworg, a->head );
	} else {
		VR_ToWorld( vr, cg.refdef.vieworg, cg.refdef.viewaxis, vr->headOrigin, NULL, a->head, NULL );
		a->head[0] -= cg_vrBodyBack.value * cosf( DEG2RAD( vrb.worldYaw ) );	// (just behind your headset)
		a->head[1] -= cg_vrBodyBack.value * sinf( DEG2RAD( vrb.worldYaw ) );
		VectorSubtract( a->head, cg.refdef.vieworg, a->head );
	}
	for ( h = 0; h < 2; h++ ) {
		if ( vrb.sentTime[h] == cg.time && vrb.armTime[h] == cg.time ) {
			a->hand[h] = vrb.sent[h];
			VectorSubtract( a->hand[h].origin, cg.refdef.vieworg, a->hand[h].origin );
		}
	}
	// your Force hand (Force gestures on, the hand empty - and drawn so): where
	// it aims (cg_vrforce.c) - from where it's drawn, its controller's line
	h = vr->force.hand;
	if ( ( h == VR_HAND_LEFT || h == VR_HAND_RIGHT ) && vr->handValid[h] && a->hand[h].grip == VR_GRIP_FREE ) {
		for ( i = 0; i < 3; i++ ) {
			VectorCopy( vr->aimAxis[h][i], head[i] );
		}
		VR_ToWorld( vr, cg.refdef.vieworg, cg.refdef.viewaxis, NULL, head, NULL, world );
		a->forceValid = qtrue;
		a->forceHand = h;
		CG_VR_ForceAim( vr, vrb.sent[h].origin, world[0], a->forceDir );
	}
}

// Another VR player's Force hand, as their avatar has it this frame (them
// drawn as their VR body): which (VR_HAND_*), and where it points (world
// axes). -1: none.
int CG_VR_AvatarForce( centity_t *cent, vec3_t dir ) {
	const int n = cent->currentState.number;
	const vrAvatar_t *a;

	if ( n < 0 || n >= MAX_CLIENTS || n == cg.clientNum || !CG_VRB_OtherUsable( cent ) ) {
		return -1;
	}
	a = &vrbOthers[n].avatar;
	if ( !a->forceValid || ( a->forceHand != VR_HAND_LEFT && a->forceHand != VR_HAND_RIGHT ) ||
		a->hand[a->forceHand].grip != VR_GRIP_FREE ) {
		return -1;
	}
	VectorCopy( a->forceDir, dir );
	return a->forceHand;
}

static void CG_VRB_FeelSteady( int hand, float amp, float grain );

// Something lasting in a hand, this frame only (cg_vrforce.c's: mixed with
// the rest, CG_VRB_FeelFrame).
void CG_VR_FeelSteady( int hand, float amp, float grain ) {
	CG_VRB_FeelSteady( hand, amp, grain );
}

// After the frame's scene is built: the entities' real positions back (next
// frame's events and sounds use them), and the blades in your hands and your
// body to the engine.
void CG_VR_BodyEndFrame( void ) {
	const vrClientState_t *vr = CG_VR();
	vec3_t shift;
	int i;

	// does your blade meet someone (or their blade) where they're drawn?
	CG_VRB_TouchFeel();

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		if ( vrbOthers[i].shiftedEnt ) {
			VectorSubtract( vrbOthers[i].shiftedEnt->lerpOrigin, vrbOthers[i].shift, vrbOthers[i].shiftedEnt->lerpOrigin );
			vrbOthers[i].shiftedEnt = NULL;
			VectorClear( vrbOthers[i].shift );
		}
	}

	// which hand holds which of your sabers, every frame - your body drawn or
	// not (knocked down, in a saber lock, dead: the triggers go by it,
	// CG_VRB_WeaponHand; and death or another weapon puts them back)
	if ( vr && cg.snap && !( cg.snap->ps.pm_flags & PMF_FOLLOW ) && cg.snap->ps.persistant[PERS_TEAM] != TEAM_SPECTATOR ) {
		vrbCur = &vrbSelf;
		CG_VRB_Init();
		CG_VRB_Holds( &cg_entities[cg.predictedPlayerState.clientNum] );
		CG_VRB_HideHolstered( &cg_entities[cg.predictedPlayerState.clientNum], CG_VRH_On() );
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

			// the hands holding the sabers (each in either), and the other
			// hand when it holds the first one too
			for ( s = 0; saber && s < VR_POSE_MAX_SABERS; s++ ) {
				const int h = CG_VRB_SaberHand( s );
				p->twoHanded[s] = qfalse;
				p->hand[s] = ( h == VR_HAND_LEFT ) ? VR_HAND_LEFT : VR_HAND_RIGHT;
				if ( p->bladeMask[s] && h >= 0 ) {
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
				vrbGazeSent = cg.time ? cg.time : 1;	// (where your headset looks went with it: CG_VRB_GazeShared)
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
	CG_VRB_Heft();

	// where a squeezed grip takes hold of something instead of pressing its key
	if ( CG_VR_Engine() && trap->ext.VR_SetGrabZones ) {
		trap->ext.VR_SetGrabZones( ( ( vr && vrb.twoHandTime == cg.time ) ? vrb.grabZones : 0 ) | CG_VRH_Zones() |
			( CG_VRH_GripHolds() ? VR_GRAB_HOLDS : 0 ) );
	}
	// (holsters: the rules your commands go by, and their markers - cg_vrholster.c)
	CG_VRH_SayButtons();
	CG_VRH_EndFrame( vr );
	// the hand holding your weapon: its trigger attacks (lights and puts out
	// a saber), the other one alt-attacks
	if ( CG_VR_Engine() && trap->ext.VR_SetWeaponHand ) {
		trap->ext.VR_SetWeaponHand( CG_VRB_WeaponHand() );
	}

	// and what your hands feel this frame (the Force hand's with the rest:
	// cg_vrforce.c)
	CG_VR_ForceFrame( vr );
	CG_VRB_FeelFrame();
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
		CG_VRB_NoteOtherBlade( cent, saberNum, bladeNum, base, dir, length );	// (someone else's: to feel it meet yours)
		return;	// not your VR body (or not drawn as one this frame)
	}
	if ( saberNum < 0 || saberNum >= VR_POSE_MAX_SABERS || bladeNum < 0 || bladeNum >= VR_POSE_MAX_BLADES ) {
		return;
	}
	if ( CG_VRB_SaberHand( saberNum ) < 0 || vrb.armTime[CG_VRB_SaberHand( saberNum )] != cg.time ) {
		return;	// the hand holding it isn't on its controller this frame (the animation holds the saber)
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

// A blade lit in your VR hand this frame (either saber, in either hand:
// CG_VR_SaberHand), and where it is: for the salute that challenges to a
// duel (cg_vrduel.c).
qboolean CG_VR_LitBlade( int saberNum, int bladeNum, vec3_t base, vec3_t tip, qboolean *twoHanded ) {
	if ( vrbSaber.time != cg.time || saberNum < 0 || saberNum >= VR_POSE_MAX_SABERS || bladeNum < 0 ||
		bladeNum >= VR_POSE_MAX_BLADES || !( vrbSaber.pose.bladeMask[saberNum] & ( 1 << bladeNum ) ) ) {
		return qfalse;
	}
	VectorCopy( vrbSaber.base[saberNum][bladeNum], base );
	VectorCopy( vrbSaber.tip[saberNum][bladeNum], tip );
	if ( twoHanded ) {
		*twoHanded = vrbSaber.pose.twoHanded[saberNum];	// (the other hand on it too)
	}
	return qtrue;
}

// Whether the other hand holds the first saber too (lately).
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
				if ( d < best && CG_VRB_SaberHand( s ) >= 0 ) {
					best = d;
					hand = CG_VRB_SaberHand( s );
				}
			}
		}
	}
	return hand;
}

/*
--------------------------------------------------------------------------------
What your hands feel (the controllers' vibration)

Everything that buzzes a hand goes through here. Each feel has a strong
start that dies away (some with a rough texture: cutting, grinding), and
what lasts - your blade in someone, in a wall, pressed against another
blade - adds its feel every frame it goes on. Once a frame the strongest
of them goes to each controller, so they never cut each other short.

Hitting someone is felt the moment your blade meets them as you see it: a
blow as hard as your swing, then the drag of the blade going through them
while it's in them. When the game says it cut, a deeper rumble follows
(longer the heavier the blade, and a long one for a kill). Meeting another
blade knocks at once too; the game's word on it then only adds a little.
And swinging the blade through the air, its weight pulls at your hand
(CG_VRB_Heft). A second hand holding the hilt feels all of it as strongly
as the first. cg_vrHaptics scales all of it (0 turns it off).
--------------------------------------------------------------------------------
*/

#define VRB_FEELS			8		// at once, a hand
#define VRB_FEEL_PULSE		0.03f	// s: each frame's buzz (the next frame's replaces it)
#define VRB_CONTACT_GAP		120		// ms: out of someone this briefly and back in is the same cut
#define VRB_CONFIRM_WINDOW	300		// ms: the game's word on a cut or a clash comes within this
#define VRB_REACH			250.0f	// others' blades further than this from you can't meet yours
#define VRB_BLADE_TOUCH		3.5f	// blades closer than this (their cores, and a little) meet
#define VRB_OTHER_BLADES	16

// the swing speeds a blade starts to cut at and does full damage from, by
// weight (m/s, as the server's g_vrSaberLight/Medium/Heavy have them; for a
// blade that feels heavier, a share of them: CG_VRB_CutScale)
static const float	vrbCutFrom[3] = { 1.8f, 2.5f, 3.2f };
static const float	vrbFullAt[3] = { 5.3f, 7.1f, 8.8f };

typedef struct {
	int		start;			// cg.time it began (0: none)
	float	peak;			// held for 'hold' ms...
	int		hold;
	float	tail;			// ...then from this down to nothing over 'decay' ms
	int		decay;
	float	grain;			// rough as it dies away: every other frame this much weaker (0..1)
} vrbFeel_t;

static struct {
	vrbFeel_t	fx[2][VRB_FEELS];
	float		steady[2], steadyGrain[2];	// what lasts, this frame
	int			frameTime;					// cg.time the controllers were last told
	qboolean	buzzing[2];
	qboolean	grainBeat;
	// your blade in someone, as you see it (per hand)
	int			inEnt[2];					// who (-1: nobody)
	int			inTime[2];					// cg.time it was last in them
	int			blowEnt[2], blowTime[2];	// the last blow felt (the game's word folds into it)
	// your blade against another one, as you see it
	qboolean	touching[2];				// this frame
	int			knockTime[2];				// the last knock felt at once
	// where your blades were last frame (for their sweep)
	int			prevTime, prevMask[VR_POSE_MAX_SABERS];
	vec3_t		prevBase[VR_POSE_MAX_SABERS][VR_POSE_MAX_BLADES], prevTip[VR_POSE_MAX_SABERS][VR_POSE_MAX_BLADES];
	// the blade's weight as it swings (CG_VRB_Heft)
	float		heftPeak[2];				// the fastest this swing has gone (m/s)
	int			heftPeakTime[2];
	qboolean	init;
} vrbHands;

static void CG_VRB_HandsInit( void ) {
	if ( !vrbHands.init ) {
		memset( &vrbHands, 0, sizeof( vrbHands ) );
		vrbHands.inEnt[0] = vrbHands.inEnt[1] = -1;
		vrbHands.blowEnt[0] = vrbHands.blowEnt[1] = -1;
		vrbHands.init = qtrue;
	}
}

// A feel in one hand from elsewhere (cg_vrduel.c), mixed with the rest: peak
// for hold ms, then from tail down to nothing over decay ms (cg_vrHaptics
// scales it, as all of them).
void CG_VR_Feel( int hand, float peak, int hold, float tail, int decay ) {
	CG_VRB_Feel( hand, peak, hold, tail, decay, 0.0f );
}

// When your blade last met another blade or went into someone, as you saw it
// or the game said (cg.time; 0: not yet) - for the salute (cg_vrduel.c): no
// challenges in the middle of a fight.
int CG_VR_BladeFightTime( void ) {
	int t = 0, h;

	if ( !vrbHands.init ) {
		return 0;
	}
	for ( h = 0; h < 2; h++ ) {
		if ( vrbHands.touching[h] ) {
			return cg.time;
		}
		if ( vrbHands.knockTime[h] > t ) {
			t = vrbHands.knockTime[h];
		}
		if ( vrbHands.blowTime[h] > t ) {
			t = vrbHands.blowTime[h];
		}
	}
	return t;
}

// the other players' blades near you this frame (and last frame)
typedef struct {
	int		ent, saber, blade;
	int		time, prevTime;			// cg.time seen (0: free); the frame before
	vec3_t	base, tip, prevBase, prevTip;
} vrbOtherBlade_t;
static vrbOtherBlade_t	vrbOtherBlades[VRB_OTHER_BLADES];

// A feel in one hand: peak for hold ms, then from tail down to nothing over
// decay ms.
static void CG_VRB_Feel( int hand, float peak, int hold, float tail, int decay, float grain ) {
	vrbFeel_t *slot = NULL;
	int i, weakest = 0;
	float least = 99.0f;

	if ( hand < VR_HAND_LEFT || hand > VR_HAND_RIGHT || ( peak <= 0.0f && tail <= 0.0f ) ) {
		return;
	}
	CG_VRB_HandsInit();
	for ( i = 0; i < VRB_FEELS; i++ ) {
		vrbFeel_t *f = &vrbHands.fx[hand][i];
		const int t = cg.time - f->start;
		if ( !f->start || t < 0 || t >= f->hold + f->decay ) {
			slot = f;	// (free)
			break;
		}
		if ( Q_max( f->peak, f->tail ) < least ) {
			least = Q_max( f->peak, f->tail );
			weakest = i;
		}
	}
	if ( !slot ) {
		slot = &vrbHands.fx[hand][weakest];	// (all busy: the weakest goes)
	}
	slot->start = Q_max( 1, cg.time );
	slot->peak = Com_Clamp( 0.0f, 1.0f, peak );
	slot->hold = Q_max( 0, hold );
	slot->tail = Com_Clamp( 0.0f, 1.0f, tail );
	slot->decay = Q_max( 0, decay );
	slot->grain = Com_Clamp( 0.0f, 1.0f, grain );
}

// The same in the hand holding the blade - and, holding it with both, in the
// other one too, as strongly.
static void CG_VRB_FeelBlade( int hand, float peak, int hold, float tail, int decay, float grain ) {
	CG_VRB_Feel( hand, peak, hold, tail, decay, grain );
	if ( hand == vrb.hold[0].hand && CG_VRB_TwoHanded() && vrb.twoHandOff != hand ) {
		CG_VRB_Feel( vrb.twoHandOff, peak, hold, tail, decay, grain );
	}
}

// Something lasting, this frame only (in both hands holding the blade, as
// CG_VRB_FeelBlade).
static void CG_VRB_FeelSteady( int hand, float amp, float grain ) {
	int h;

	if ( hand < VR_HAND_LEFT || hand > VR_HAND_RIGHT ) {
		return;
	}
	CG_VRB_HandsInit();
	for ( h = VR_HAND_LEFT; h <= VR_HAND_RIGHT; h++ ) {
		if ( h != hand && !( hand == vrb.hold[0].hand && h == vrb.twoHandOff && CG_VRB_TwoHanded() ) ) {
			continue;
		}
		if ( amp > vrbHands.steady[h] ) {
			vrbHands.steady[h] = Com_Clamp( 0.0f, 1.0f, amp );
			vrbHands.steadyGrain[h] = Com_Clamp( 0.0f, 1.0f, grain );
		}
	}
}

// Once a frame: the strongest feel to each controller.
static void CG_VRB_FeelFrame( void ) {
	const float scale = Com_Clamp( 0.0f, 2.0f, cg_vrHaptics.value );
	int h, i;

	if ( cg.secondEye || vrbHands.frameTime == cg.time ) {
		return;
	}
	CG_VRB_HandsInit();
	vrbHands.frameTime = cg.time;
	vrbHands.grainBeat = !vrbHands.grainBeat;
	for ( h = 0; h < 2; h++ ) {
		float amp = vrbHands.steady[h] * ( vrbHands.grainBeat ? 1.0f - vrbHands.steadyGrain[h] : 1.0f );
		for ( i = 0; i < VRB_FEELS; i++ ) {
			vrbFeel_t *f = &vrbHands.fx[h][i];
			const int t = cg.time - f->start;
			float a;
			if ( !f->start ) {
				continue;
			}
			if ( t < 0 || t >= f->hold + f->decay ) {
				f->start = 0;	// (over; or the clock went back)
				continue;
			}
			if ( t < f->hold ) {
				a = f->peak;	// (solid)
			} else {
				const float k = 1.0f - (float)( t - f->hold ) / f->decay;
				a = f->tail * k * k;
				if ( vrbHands.grainBeat ) {
					a *= 1.0f - f->grain;	// (rough as it dies away)
				}
			}
			amp = Q_max( amp, a );
		}
		vrbHands.steady[h] = vrbHands.steadyGrain[h] = 0.0f;
		amp = Q_min( 1.0f, amp * scale );
		if ( !CG_VR() || !trap->ext.VR_Haptic ) {
			continue;
		}
		if ( amp > 0.02f ) {
			trap->ext.VR_Haptic( h, VRB_FEEL_PULSE, amp );
			vrbHands.buzzing[h] = qtrue;
		} else if ( vrbHands.buzzing[h] ) {
			trap->ext.VR_Haptic( h, VRB_FEEL_PULSE, 0.0f );	// (stop now)
			vrbHands.buzzing[h] = qfalse;
		}
	}
}

/*
--------------------------------------------------------------------------------
Your blade meeting someone, or someone's blade, as you see it
--------------------------------------------------------------------------------
*/

// The closest points of segments p1-q1 and p2-q2, and how far apart they are.
static float CG_VRB_SegSegClosest( const vec3_t p1, const vec3_t q1, const vec3_t p2, const vec3_t q2 ) {
	const float eps = 0.0001f;
	vec3_t d1, d2, r, c1, c2;
	float a, e, f, s, t;

	VectorSubtract( q1, p1, d1 );
	VectorSubtract( q2, p2, d2 );
	VectorSubtract( p1, p2, r );
	a = DotProduct( d1, d1 );
	e = DotProduct( d2, d2 );
	f = DotProduct( d2, r );
	if ( a <= eps && e <= eps ) {
		s = t = 0.0f;
	} else if ( a <= eps ) {
		s = 0.0f;
		t = Com_Clamp( 0.0f, 1.0f, f / e );
	} else {
		const float c = DotProduct( d1, r );
		if ( e <= eps ) {
			t = 0.0f;
			s = Com_Clamp( 0.0f, 1.0f, -c / a );
		} else {
			const float b = DotProduct( d1, d2 );
			const float denom = a * e - b * b;
			s = ( denom > eps ) ? Com_Clamp( 0.0f, 1.0f, ( b * f - c * e ) / denom ) : 0.0f;
			t = ( b * s + f ) / e;
			if ( t < 0.0f ) {
				t = 0.0f;
				s = Com_Clamp( 0.0f, 1.0f, -c / a );
			} else if ( t > 1.0f ) {
				t = 1.0f;
				s = Com_Clamp( 0.0f, 1.0f, ( b - c ) / a );
			}
		}
	}
	VectorMA( p1, s, d1, c1 );
	VectorMA( p2, t, d2, c2 );
	return Distance( c1, c2 );
}

// Does segment p-q pass through triangle v0 v1 v2?
static qboolean CG_VRB_SegTri( const vec3_t p, const vec3_t q, const vec3_t v0, const vec3_t v1, const vec3_t v2 ) {
	vec3_t dir, e1, e2, h, s, qv;
	float a, f, u, v, t;

	VectorSubtract( q, p, dir );
	VectorSubtract( v1, v0, e1 );
	VectorSubtract( v2, v0, e2 );
	CrossProduct( dir, e2, h );
	a = DotProduct( e1, h );
	if ( fabsf( a ) < 0.000001f ) {
		return qfalse;
	}
	f = 1.0f / a;
	VectorSubtract( p, v0, s );
	u = f * DotProduct( s, h );
	if ( u < 0.0f || u > 1.0f ) {
		return qfalse;
	}
	CrossProduct( s, e1, qv );
	v = f * DotProduct( dir, qv );
	if ( v < 0.0f || u + v > 1.0f ) {
		return qfalse;
	}
	t = f * DotProduct( e2, qv );
	return (qboolean)( t >= 0.0f && t <= 1.0f );
}

// A blade that moved from base0-tip0 to base1-tip1: did it meet the blade
// o0-o1 (touching it now, or passing through it on the way)?
static qboolean CG_VRB_BladeSweepMeets( const vec3_t base0, const vec3_t tip0, const vec3_t base1, const vec3_t tip1,
	const vec3_t o0, const vec3_t o1 ) {
	if ( CG_VRB_SegSegClosest( base1, tip1, o0, o1 ) < VRB_BLADE_TOUCH ) {
		return qtrue;
	}
	return (qboolean)( CG_VRB_SegTri( o0, o1, base0, tip0, tip1 ) || CG_VRB_SegTri( o0, o1, base0, tip1, base1 ) );
}

// Someone else's blade, drawn this frame (CG_AddSaberBlade): kept a frame,
// to feel it meet yours.
static void CG_VRB_NoteOtherBlade( centity_t *cent, int saberNum, int bladeNum, const vec3_t base, const vec3_t dir, float length ) {
	const int ent = cent->currentState.number;
	vrbOtherBlade_t *o = NULL;
	vec3_t d;
	int i, oldest = 0;

	if ( cg.secondEye || length <= 0.0f || ent == cg.clientNum || !CG_VR() ) {
		return;
	}
	if ( DistanceSquared( base, cg.refdef.vieworg ) > VRB_REACH * VRB_REACH ) {
		return;
	}
	for ( i = 0; i < VRB_OTHER_BLADES; i++ ) {
		vrbOtherBlade_t *b = &vrbOtherBlades[i];
		if ( b->time && b->ent == ent && b->saber == saberNum && b->blade == bladeNum ) {
			o = b;
			break;
		}
		if ( b->time < vrbOtherBlades[oldest].time ) {
			oldest = i;	// (0 - free - is oldest of all)
		}
	}
	if ( !o ) {
		o = &vrbOtherBlades[oldest];
		memset( o, 0, sizeof( *o ) );
		o->ent = ent;
		o->saber = saberNum;
		o->blade = bladeNum;
	}
	if ( o->time != cg.time ) {
		if ( o->time && cg.time - o->time > 0 && cg.time - o->time < 100 ) {
			VectorCopy( o->base, o->prevBase );
			VectorCopy( o->tip, o->prevTip );
			o->prevTime = o->time;
		} else {
			o->prevTime = 0;
		}
	}
	o->time = cg.time;
	VectorCopy( dir, d );
	VectorNormalize( d );
	VectorCopy( base, o->base );
	VectorMA( base, length, d, o->tip );
}

// Whom a blade from base to tip is in: a player's (or NPC's) model, as
// drawn, the way the server tests it (their box, then their model's
// triangles). -1: nobody.
static int CG_VRB_BladeInBody( const vec3_t base, const vec3_t tip ) {
	trace_t tr;
	centity_t *c;

	CG_Trace( &tr, base, NULL, NULL, tip, cg.clientNum, MASK_SHOT );
	if ( tr.entityNum < 0 || tr.entityNum >= ENTITYNUM_WORLD || tr.entityNum == cg.clientNum ) {
		return -1;
	}
	c = &cg_entities[tr.entityNum];
	if ( ( c->currentState.eType != ET_PLAYER && c->currentState.eType != ET_NPC ) || ( c->currentState.eFlags & EF_DEAD ) ) {
		return -1;
	}
	if ( c->ghoul2 && !CG_G2TraceCollide( &tr, NULL, NULL, base, tip ) ) {
		return -1;	// (the box, but not the body)
	}
	return c->currentState.number;
}

// A friend in a team game (whom your blade only touches).
static qboolean CG_VRB_Teammate( int ent ) {
	if ( cgs.gametype < GT_TEAM || ent < 0 || ent >= MAX_CLIENTS ) {
		return qfalse;
	}
	return (qboolean)( cgs.clientinfo[ent].infoValid && cgs.clientinfo[ent].team == cgs.clientinfo[cg.clientNum].team &&
		cgs.clientinfo[ent].team != TEAM_FREE && cgs.clientinfo[ent].team != TEAM_SPECTATOR );
}

/*
=================
CG_VRB_BladeSolid

Blades are solid to each other. Yours can't go through another one as you
see it: swung or pushed into it, it stops there and rests on it, the white
cores touching, and slides along it as your hand moves along it; take your
hand back, or round an end of it, and it comes away. Your real hand can go
on past - nothing pushes it back - so the arm turns your fist to hold the
blade where it rests, and you feel it pressed on harder the further your
hand goes; go far enough on through it (60 degrees, more if the blade was
trailing your hand as it met the other one, up to 75) and it slips by. A blow the game lets break through goes through: a heavy blade swung
at full speed, two or more stronger (VRS_Breaks). Only your blade moves:
one swung into yours (as you see it) doesn't shove it - in its owner's
game it stops on yours. Teammates' blades and other people's duels aren't
in the way (the game lets those pass too), nor are thrown sabers; and
walls and floors never are: a lightsaber cuts them. cg_vrSaberSolid 0:
blades pass through each other (the game still stops them where they met).

It works from the blades as drawn last frame. From the hold on the hilt
(the controller's grip, where the arm puts it - CG_VRB_SolveArm) your blade
lies along where it points, from its emitter to its tip, and turns about
that hold. The hold and another blade's line make a plane: when your blade
goes over from one side of it to the other, and where it crosses is on
both blades, it went through - so it's turned back, as little as it takes,
to rest on the other blade on the side it came from. (Their blade where it
was last seen: yours is drawn before theirs.)
=================
*/

#define VRB_SOLID_GAP		2.0f	// blades rest this far apart: their white cores touching (the game has them meet within 3)...
#define VRB_SOLID_TOUCH		1.15f	// ...and up to this many times as far apart they still rest on each other
#define VRB_SOLID_NEAR		4.0f	// another blade by your hand (nearer than this along your blade) doesn't stop it
#define VRB_SOLID_MIN		8.0f	// blades shorter than this (lighting up) aren't solid yet
#define VRB_SOLID_JUMP		32.0f	// the hand moved further than this in a frame (teleported)...
#define VRB_SOLID_SNAP		5.0f	// ...or the room turned this many degrees (a snap turn): it didn't sweep there
#define VRB_SOLID_SLIP		60.0f	// degrees your hand can go on through the other blade before yours slips by...
#define VRB_SOLID_SLIP_MORE	30.0f	// ...or this much more than it already had as they met (a heavy blade trailing it)...
#define VRB_SOLID_SLIP_MAX	75.0f	// ...up to this (the drawn wrist bends that far)
#define VRB_SOLID_SLIP_MS	150		// slipped by: no blade stops it for this long (it goes on through)
#define VRB_SOLID_CONTACT	100		// ms: resting on one again this soon is the same contact

// another blade in the way
typedef struct {
	int			ent;			// whose
	vec3_t		P, e;			// its base, and the way it points
	float		L;				// its length
	qboolean	before;			// where it was the frame before:
	vec3_t		P0, e0;
} vrbSolidOther_t;

// your blade resting on another
typedef struct {
	vec3_t		point;			// where they touch (on the other blade)
	vec3_t		normal;			// the way from there to yours
	float		push;			// degrees your hand points on past it, through the other blade
} vrbSolidContact_t;

// one of the blades in your hand, as drawn last frame
typedef struct {
	int			index;			// which blade of the saber
	float		sgn;			// it points the way the hilt does (1) or the other way (-1: a staff's second blade)
	float		a, len;			// from the hold on the hilt, along it: where it starts, and how long it is
	vec3_t		ofs;			// the hold from the pivot it turns about (world: CG_VRB_BladeSolid)
	vec3_t		H0, u0;			// last frame: the hold, and where the blade pointed
} vrbSolidMine_t;

typedef struct {
	vec3_t		m;				// across the plane through your hold and the other blade's line
	float		c;				// how far your blade points out of that plane (the sine)
	vec3_t		vp;				// the way it points in it
	qboolean	vpOk;
	qboolean	cross;			// it crosses the other blade's line there...
	float		k, w;			// ...this far along yours from the hold, and along theirs from its base
} vrbSolidGeom_t;

// Your blade, from the hold H pointing v, against the line of another one
// from P pointing e. qfalse: the hold is on that line.
static qboolean CG_VRB_SolidGeom( const vec3_t H, const vec3_t v, const vec3_t P, const vec3_t e, vrbSolidGeom_t *g ) {
	vec3_t r, x, t;
	float denom;

	VectorSubtract( H, P, r );
	CrossProduct( e, r, g->m );
	if ( VectorNormalize( g->m ) < 0.5f ) {
		return qfalse;
	}
	g->c = DotProduct( v, g->m );
	VectorMA( v, -g->c, g->m, g->vp );
	g->vpOk = (qboolean)( VectorNormalize( g->vp ) > 0.05f );
	g->cross = qfalse;
	g->k = g->w = 0.0f;
	if ( !g->vpOk ) {
		return qtrue;	// (pointing straight out of the plane)
	}
	// H + k vp = P + w e, in the plane
	CrossProduct( g->vp, e, x );
	denom = DotProduct( x, g->m );
	if ( fabsf( denom ) < 0.05f ) {
		return qtrue;	// (alongside the other blade: they don't cross)
	}
	VectorScale( r, -1.0f, r );
	CrossProduct( r, e, t );
	g->k = DotProduct( t, g->m ) / denom;
	CrossProduct( r, g->vp, t );
	g->w = DotProduct( t, g->m ) / denom;
	g->cross = qtrue;
	return qtrue;
}

// Is where it crosses the other blade's line on both blades?
static qboolean CG_VRB_SolidOn( const vrbSolidGeom_t *g, const vrbSolidMine_t *m, float L ) {
	return (qboolean)( g->cross && g->k >= Q_max( m->a, VRB_SOLID_NEAR ) && g->k <= m->a + m->len + 0.5f * VRB_SOLID_GAP &&
		g->w >= -0.5f * VRB_SOLID_GAP && g->w <= L + 0.5f * VRB_SOLID_GAP );
}

// Your blade m, from the hold H, along sgn * d, against another one: d
// turned as little as keeps it from going through - on the side it was on
// last frame, the cores touching. But no further out than it was if the
// other blade moved into it: one moving into yours (as you see it) doesn't
// shove it. qtrue if it rests on it (turned onto it, or just touching it),
// and how (*c; your hand pointing want).
static qboolean CG_VRB_SolidPair( const vrbSolidMine_t *m, const vec3_t H, const vrbSolidOther_t *o, const vec3_t want,
	vec3_t d, vrbSolidContact_t *c ) {
	vrbSolidGeom_t g, g0, gx;
	vec3_t v;
	float side, k, w, s, hand;

	VectorScale( d, m->sgn, v );
	if ( !CG_VRB_SolidGeom( H, v, o->P, o->e, &g ) || !g.vpOk || !CG_VRB_SolidGeom( m->H0, m->u0, o->P, o->e, &g0 ) ||
		fabsf( g0.c ) < 0.01f ) {
		return qfalse;	// (or in line with it last frame: whichever side it's on now)
	}
	side = ( g0.c > 0.0f ) ? 1.0f : -1.0f;
	if ( side * g.c < 0.0f ) {
		// it went over since: through the other blade, or round an end of it?
		const float f = g0.c / ( g0.c - g.c );
		vec3_t hx, vx;
		VectorSubtract( H, m->H0, hx );
		VectorMA( m->H0, f, hx, hx );
		VectorSubtract( v, m->u0, vx );
		VectorMA( m->u0, f, vx, vx );
		if ( VectorNormalize( vx ) < 0.1f || !CG_VRB_SolidGeom( hx, vx, o->P, o->e, &gx ) || !CG_VRB_SolidOn( &gx, m, o->L ) ) {
			return qfalse;	// round it
		}
		if ( CG_VRB_SolidOn( &g, m, o->L ) ) {
			k = g.k;
			w = g.w;
		} else {
			k = gx.k;
			w = gx.w;
		}
	} else if ( CG_VRB_SolidOn( &g, m, o->L ) ) {
		k = g.k;
		w = g.w;
	} else {
		return qfalse;
	}
	k = Com_Clamp( Q_max( m->a, VRB_SOLID_NEAR ), Q_max( m->a + m->len, VRB_SOLID_NEAR ), k );
	s = VRB_SOLID_GAP / k;
	if ( o->before ) {
		vrbSolidGeom_t gb;
		if ( CG_VRB_SolidGeom( m->H0, m->u0, o->P0, o->e0, &gb ) && side * gb.c > side * g0.c + 0.001f ) {
			s = Q_min( s, side * g0.c );	// (it came nearer yours since: yours stays where it was)
		}
	}
	if ( side * g.c >= VRB_SOLID_TOUCH * s ) {
		return qfalse;	// on its side, clear of it
	}
	if ( side * g.c < s ) {
		VectorScale( g.vp, sqrtf( 1.0f - s * s ), v );
		VectorMA( v, side * s, g.m, v );
		VectorScale( v, m->sgn, d );
	} else {
		s = side * g.c;	// (resting on it as it is)
	}
	VectorMA( o->P, Com_Clamp( 0.0f, o->L, w ), o->e, c->point );
	VectorScale( g.m, side, c->normal );
	// how far on through it your hand points (its way across the other
	// blade, not along it: a blade trailing your hand along it isn't pushed)
	hand = m->sgn * DotProduct( want, g.m );
	c->push = RAD2DEG( asinf( s ) - asinf( Com_Clamp( -1.0f, 1.0f, side * hand ) ) );
	return qtrue;
}

// Can your blade meet that one, as the game has it? Not a teammate's (a
// friendly blade passes), not someone else's in a duel, not a thrown one.
static qboolean CG_VRB_SolidFoe( const vrbOtherBlade_t *o ) {
	const centity_t *c;

	if ( o->ent < 0 || o->ent >= MAX_GENTITIES ) {
		return qfalse;
	}
	c = &cg_entities[o->ent];
	if ( ( c->currentState.eFlags & EF_DEAD ) || CG_VRB_Teammate( o->ent ) ) {
		return qfalse;
	}
	if ( o->saber == 0 && c->currentState.saberInFlight ) {
		return qfalse;	// (thrown: the game blocks those its own way)
	}
	if ( cg.predictedPlayerState.duelInProgress ) {
		return (qboolean)( o->ent == cg.predictedPlayerState.duelIndex );
	}
	return (qboolean)( o->ent >= MAX_CLIENTS || !c->currentState.bolt1 );	// (bolt1: they're in a duel)
}

// Would a blow of the blade in hand h go on through that one, as the game
// reckons it (VRS_Breaks)? A heavy blade swung at full speed, two or more
// stronger than the other (its weight, and one more if it moves as fast as
// it hits hardest: a share of its weight's full-damage speed for a blade
// that feels heavier, bg_vrsaber.h) - and the faster of the two.
static qboolean CG_VRB_SolidBreaks( int h, const vrClientState_t *vr, const vrbOtherBlade_t *o ) {
	const float speed = vrb.swing.handSpeed[h];
	int mine, theirs, theirWeight;

	if ( CG_VRB_Weight( qtrue ) != VRB_HEAVY || speed < vrbFullAt[VRB_HEAVY] * CG_VRB_CutScale() ) {
		return qfalse;
	}
	mine = VRB_HEAVY + 2;	// (heavy, and one for full speed)
	if ( CG_VRB_Weight( qfalse ) == VRB_HEAVY && CG_VRB_TwoHanded() ) {
		mine++;				// (heavy held with both hands)
	}
	switch ( cg_entities[o->ent].currentState.fireflag ) {	// (their saber style)
	case SS_FAST:
	case SS_TAVION:
		theirWeight = VRB_LIGHT;
		break;
	case SS_STRONG:
	case SS_DESANN:
		theirWeight = VRB_HEAVY;
		break;
	default:
		theirWeight = VRB_MEDIUM;
		break;
	}
	theirs = theirWeight + 1;
	if ( o->ent >= 0 && o->ent < MAX_CLIENTS && vrbOthers[o->ent].avatarValid && cg.time - vrbOthers[o->ent].avatarTime < 100 &&
		( vrbOthers[o->ent].avatar.hand[VR_HAND_LEFT].grip == VR_GRIP_HILT ||
		vrbOthers[o->ent].avatar.hand[VR_HAND_RIGHT].grip == VR_GRIP_HILT ) ) {
		theirs++;	// (a VR player's held with both hands)
	}
	if ( o->prevTime && o->time - o->prevTime > 0 && vr->worldScale > 0.0f ) {
		vec3_t p0, p1, rel;
		float v;
		VectorSubtract( o->tip, o->base, rel );
		VectorMA( o->base, 0.66f, rel, p1 );
		VectorSubtract( o->prevTip, o->prevBase, rel );
		VectorMA( o->prevBase, 0.66f, rel, p0 );
		v = Distance( p0, p1 ) / ( ( o->time - o->prevTime ) * 0.001f ) / vr->worldScale;
		if ( v >= speed ) {
			return qfalse;	// (theirs is the faster: their blow decides)
		}
		if ( v >= vrbFullAt[theirWeight] * BG_VRFeelCutScale( BG_VRFeel( cg_entities[o->ent].currentState.fireflag ) ) ) {
			theirs++;
		}
	}
	return (qboolean)( mine >= theirs + 2 );
}

// The blade in hand h, pointing out (world) as its weight lets it
// (CG_VRB_BladeSpring), from the hold on the hilt at pivot (the controller's
// grip, or the fist settling onto it after a pass), the controller pointing it
// want: out kept from going through other blades.
static void CG_VRB_BladeSolid( int h, const vrClientState_t *vr, const vec3_t pivot, const vec3_t want, vec3_t out ) {
	const int s = CG_VRB_HandSaber( h );	// (the hand's saber: the first, or a second one)
	const float yaw = CG_VRB_RoomYaw( vr );
	// last frame's blade - and the hand got here from there, not by a
	// teleport or a snap turn
	const qboolean kept = (qboolean)( vrb.blade[h].lastTime && vrb.blade[h].lastTime == vrbSaber.time &&
		cg.time - vrb.blade[h].lastTime > 0 && cg.time - vrb.blade[h].lastTime < 100 &&
		DistanceSquared( pivot, vrb.blade[h].pivot ) < Square( VRB_SOLID_JUMP ) &&
		fabsf( AngleNormalize180( yaw - vrb.blade[h].lastYaw ) ) < VRB_SOLID_SNAP );
	vrbSolidMine_t mine[VR_POSE_MAX_BLADES];
	vrbSolidOther_t others[VRB_OTHER_BLADES];
	vrbSolidContact_t rest;
	int numMine = 0, numOthers = 0, held = -1, i, b, pass;
	float push = 0.0f;
	vec3_t loose, d;

	VectorCopy( out, loose );	// (where it would be)
	VectorCopy( out, d );
	memset( &rest, 0, sizeof( rest ) );

	if ( kept && s >= 0 && cg_vrSaberSolid.integer && cg.time - vrb.blade[h].slipUntil >= 0 ) {
		// the hand's blades as drawn last frame, from the hold on the hilt -
		// and where that has them pointing as they were let last frame
		for ( b = 0; b < VR_POSE_MAX_BLADES; b++ ) {
			vrbSolidMine_t *m = &mine[numMine];
			vec3_t u, rel;
			if ( !( vrbSaber.pose.bladeMask[s] & ( 1 << b ) ) ) {
				continue;
			}
			VectorSubtract( vrbSaber.tip[s][b], vrbSaber.base[s][b], u );
			m->len = VectorNormalize( u );
			if ( m->len < VRB_SOLID_MIN ) {
				continue;
			}
			m->index = b;
			m->sgn = ( DotProduct( u, vrb.blade[h].last ) >= 0.0f ) ? 1.0f : -1.0f;
			VectorSubtract( vrbSaber.base[s][b], vrb.blade[h].pivot, rel );
			m->a = DotProduct( rel, u );
			VectorMA( vrbSaber.base[s][b], -m->a, u, m->H0 );	// (the hold: its line's point by the grip)
			VectorSubtract( m->H0, vrb.blade[h].pivot, m->ofs );
			if ( VectorLengthSquared( m->ofs ) > Square( 16.0f ) ) {
				continue;	// (the hand isn't holding it there: the animation is)
			}
			VectorScale( vrb.blade[h].last, m->sgn, m->u0 );
			numMine++;
		}
		// the other blades in the way
		for ( i = 0; i < VRB_OTHER_BLADES && numMine; i++ ) {
			const vrbOtherBlade_t *o = &vrbOtherBlades[i];
			vrbSolidOther_t *x = &others[numOthers];
			if ( o->time != vrb.blade[h].lastTime || !CG_VRB_SolidFoe( o ) ) {
				continue;	// (not drawn last frame: put away, gone)
			}
			VectorCopy( o->base, x->P );
			VectorSubtract( o->tip, o->base, x->e );
			x->L = VectorNormalize( x->e );
			if ( x->L < VRB_SOLID_MIN ) {
				continue;
			}
			x->before = qfalse;
			if ( o->prevTime && o->time - o->prevTime > 0 ) {
				VectorCopy( o->prevBase, x->P0 );
				VectorSubtract( o->prevTip, o->prevBase, x->e0 );
				x->before = (qboolean)( VectorNormalize( x->e0 ) > 1.0f );
			}
			if ( CG_VRB_SolidBreaks( h, vr, o ) ) {
				if ( cg.time - vrb.blade[h].brokeTime > 300 ) {
					CG_VRB_Log( "solid: a heavy blow at %.1f m/s goes through %i's blade\n", vrb.swing.handSpeed[h], o->ent );
				}
				vrb.blade[h].brokeTime = cg.time;
				continue;
			}
			x->ent = o->ent;
			numOthers++;
		}
		// each blade turned as little as keeps it out of each other one
		// (twice round: resting on one can push it into another)
		for ( pass = 0; pass < 2; pass++ ) {
			for ( b = 0; b < numMine; b++ ) {
				vec3_t H;
				VectorAdd( pivot, mine[b].ofs, H );
				for ( i = 0; i < numOthers; i++ ) {
					vrbSolidContact_t c;
					if ( !CG_VRB_SolidPair( &mine[b], H, &others[i], want, d, &c ) ) {
						continue;
					}
					if ( held < 0 || c.push > push ) {
						held = others[i].ent;
						push = c.push;
						rest = c;
					}
				}
			}
		}
	}

	if ( held >= 0 ) {
		float slipAt;

		if ( !vrb.blade[h].heldTime || cg.time - vrb.blade[h].heldTime > VRB_SOLID_CONTACT ) {
			// it has just met the other blade: how far your hand was on past it
			// already (a heavy blade trails a fast swing)
			vrb.blade[h].pressLag = Q_max( 0.0f, push );
			CG_VRB_Log( "solid: your blade rests on %i's (your hand %.0f deg on through it)\n", held, push );
		}
		vrb.blade[h].heldEnt = held;
		VectorCopy( rest.point, vrb.blade[h].heldPoint );
		VectorCopy( rest.normal, vrb.blade[h].heldNormal );
		slipAt = Com_Clamp( VRB_SOLID_SLIP, VRB_SOLID_SLIP_MAX, vrb.blade[h].pressLag + VRB_SOLID_SLIP_MORE );
		if ( push > slipAt ) {
			// your hand went on too far: it slips by
			vrb.blade[h].slipUntil = cg.time + VRB_SOLID_SLIP_MS;
			vrb.blade[h].heldTime = 0;
			vrb.blade[h].press = 0.0f;
			CG_VRB_FeelBlade( h, 0.7f, 25, 0.35f, 90, 0.3f );
			CG_VRB_Log( "solid: your blade slipped by %i's (your hand %.0f deg on)\n", held, push );
		} else {
			vec3_t room, axis;

			vrb.blade[h].press = Com_Clamp( 0.0f, 1.0f, ( push - vrb.blade[h].pressLag ) / Q_max( 1.0f, slipAt - vrb.blade[h].pressLag ) );
			vrb.blade[h].heldTime = cg.time;
			// the blade's weight goes on from where it rests...
			VRB_RotZ( d, -yaw, room );
			if ( VectorNormalize( room ) > 0.5f ) {
				VectorCopy( room, vrb.blade[h].dir );
			}
			// ...its turn into the other blade stopped (along it, it goes on)
			CrossProduct( loose, d, axis );
			if ( VectorNormalize( axis ) > 1e-4f ) {
				float into;
				VRB_RotZ( axis, -yaw, room );
				into = DotProduct( vrb.blade[h].vel, room );
				if ( into < 0.0f ) {
					VectorMA( vrb.blade[h].vel, -into, room, vrb.blade[h].vel );
				}
			}
			VectorCopy( d, out );
		}
	}

	// (for the next frame)
	VectorCopy( pivot, vrb.blade[h].pivot );
	VectorCopy( out, vrb.blade[h].last );
	vrb.blade[h].lastYaw = yaw;
	vrb.blade[h].lastTime = cg.time;
}

/*
=================
CG_VRB_TouchFeel

Once a frame, after the scene is built (others where they're drawn): does
a blade in your hands meet a body, or another blade? Felt at once, as you
see it - the game's word on it comes a moment later (CG_VR_SaberEventFeel).
=================
*/
static void CG_VRB_TouchFeel( void ) {
	const int weight = CG_VRB_Weight( qtrue );
	const float cutFrom = vrbCutFrom[weight] * CG_VRB_CutScale(), fullAt = vrbFullAt[weight] * CG_VRB_CutScale();
	const float speed = vrb.swing.speed;	// (m/s, two thirds along the blade: about where it meets them)
	const qboolean havePrev = (qboolean)( vrbHands.prevTime && cg.time - vrbHands.prevTime > 0 && cg.time - vrbHands.prevTime < 100 );
	// (your blade only cuts where the server fights with it - "vrSaber 1" in
	// its serverinfo; elsewhere the game's swing animations do)
	const qboolean cuts = (qboolean)( atoi( Info_ValueForKey( CG_ConfigString( CS_SERVERINFO ), "vrSaber" ) ) > 0 );
	int in[2] = { -1, -1 }, s, b, h, i;
	qboolean met[2] = { qfalse, qfalse };

	if ( cg.secondEye || vrbHands.prevTime == cg.time ) {
		return;
	}
	CG_VRB_HandsInit();
	// (a saber in your VR hands, drawn there this frame - as the pose sent
	// at the end of the frame will say: CG_VR_BodyEndFrame)
	if ( !CG_VR() || vrbSaber.time != cg.time || !CG_VRB_SaberHands() ) {
		vrbHands.inEnt[0] = vrbHands.inEnt[1] = -1;
		vrbHands.touching[0] = vrbHands.touching[1] = qfalse;
		vrbHands.prevTime = 0;
		return;
	}

	for ( s = 0; s < VR_POSE_MAX_SABERS; s++ ) {
		for ( b = 0; b < VR_POSE_MAX_BLADES; b++ ) {
			const float *base = vrbSaber.base[s][b], *tip = vrbSaber.tip[s][b];
			const qboolean swept = (qboolean)( havePrev && ( vrbHands.prevMask[s] & ( 1 << b ) ) );
			h = CG_VRB_SaberHand( s );
			if ( !( vrbSaber.pose.bladeMask[s] & ( 1 << b ) ) || h < 0 ) {
				continue;
			}

			// a body: along the blade now (and halfway back to where it was,
			// for an arm a fast swing could skip)
			if ( in[h] < 0 && cuts ) {
				in[h] = CG_VRB_BladeInBody( base, tip );
			}
			if ( in[h] < 0 && cuts && swept ) {
				vec3_t midBase, midTip;
				VectorAdd( base, vrbHands.prevBase[s][b], midBase );
				VectorScale( midBase, 0.5f, midBase );
				VectorAdd( tip, vrbHands.prevTip[s][b], midTip );
				VectorScale( midTip, 0.5f, midTip );
				in[h] = CG_VRB_BladeInBody( midBase, midTip );
			}

			// another blade: yours swept into it, or it into yours
			for ( i = 0; i < VRB_OTHER_BLADES && !met[h]; i++ ) {
				const vrbOtherBlade_t *o = &vrbOtherBlades[i];
				if ( o->time != cg.time ) {
					continue;
				}
				if ( swept ) {
					met[h] = CG_VRB_BladeSweepMeets( vrbHands.prevBase[s][b], vrbHands.prevTip[s][b], base, tip, o->base, o->tip );
				} else {
					met[h] = (qboolean)( CG_VRB_SegSegClosest( base, tip, o->base, o->tip ) < VRB_BLADE_TOUCH );
				}
				if ( !met[h] && o->prevTime ) {
					met[h] = CG_VRB_BladeSweepMeets( o->prevBase, o->prevTip, o->base, o->tip, base, tip );
				}
			}

			VectorCopy( base, vrbHands.prevBase[s][b] );
			VectorCopy( tip, vrbHands.prevTip[s][b] );
		}
		vrbHands.prevMask[s] = vrbSaber.pose.bladeMask[s];
	}
	vrbHands.prevTime = cg.time;

	for ( h = 0; h < 2; h++ ) {
		// in someone: a blow as you go in (as hard as the swing), then the
		// drag of cutting through while the blade is in them
		if ( in[h] >= 0 ) {
			const qboolean friendly = CG_VRB_Teammate( in[h] );
			const float f = Com_Clamp( 0.0f, 1.0f, ( speed - cutFrom ) / ( fullAt - cutFrom ) );
			if ( in[h] != vrbHands.inEnt[h] || cg.time - vrbHands.inTime[h] > VRB_CONTACT_GAP ) {
				if ( friendly || speed < cutFrom ) {
					CG_VRB_FeelBlade( h, 0.4f, 25, 0.25f, 70, 0.0f );	// (a touch)
				} else {
					static const int hold[3] = { 35, 47, 55 }, decay[3] = { 110, 145, 170 };
					CG_VRB_FeelBlade( h, 0.75f + 0.25f * f, hold[weight] + (int)( 20.0f * f ), 0.5f + 0.3f * f,
						decay[weight], 0.3f );
				}
				vrbHands.blowEnt[h] = in[h];
				vrbHands.blowTime[h] = cg.time;
				CG_VRB_Log( "feel: your %s blade into %i at %.1f m/s\n", vrbWeightNames[weight], in[h], speed );
			}
			CG_VRB_FeelSteady( h, friendly ? 0.2f : 0.3f + 0.4f * Com_Clamp( 0.0f, 1.0f, speed / fullAt ), 0.45f );
			vrbHands.inEnt[h] = in[h];
			vrbHands.inTime[h] = cg.time;
		} else if ( cg.time - vrbHands.inTime[h] > VRB_CONTACT_GAP ) {
			vrbHands.inEnt[h] = -1;
		}

		// against another blade: a knock as they meet (harder the faster
		// and the heavier yours), a grind while they're pressed together -
		// resting on it (CG_VRB_BladeSolid), harder the harder you push
		{
			const qboolean resting = (qboolean)( vrb.blade[h].heldTime == cg.time );
			if ( resting ) {
				met[h] = qtrue;
			}
			if ( met[h] ) {
				if ( !vrbHands.touching[h] && cg.time - vrbHands.knockTime[h] > 100 ) {
					static const int knock[3] = { 60, 75, 90 }, decay[3] = { 85, 105, 130 };
					const float hard = Com_Clamp( 0.0f, 1.0f, vrb.swing.handSpeed[h] / fullAt );
					CG_VRB_FeelBlade( h, 0.45f + 0.55f * hard, knock[weight], 0.25f + 0.3f * hard, decay[weight], 0.0f );
					vrbHands.knockTime[h] = cg.time;
					CG_VRB_Log( "feel: your %s blade met another at %.1f m/s\n", vrbWeightNames[weight], vrb.swing.handSpeed[h] );
				}
				CG_VRB_FeelSteady( h, resting ? 0.3f + 0.5f * vrb.blade[h].press : 0.4f, 0.5f );
			}
			if ( resting ) {
				// and sparks where they touch, as a blade held to a wall throws
				// (the game is quiet about blades held together: only you see these)
				trap->FX_PlayEffectID( cgs.effects.mSparks, vrb.blade[h].heldPoint, vrb.blade[h].heldNormal, -1, -1, qfalse );
			}
		}
		vrbHands.touching[h] = met[h];
	}
}

/*
=================
CG_VRB_Heft

The blade's weight in your hand as it swings. The faster it goes, the
harder it pulls, most as fast as it cuts hardest - red a deep, rough
rumble, the staff a little less, yellow less again, blue a light buzz (two
sabers each as yellow was; held with both hands, one heavier: blue as
yellow, yellow and the staff as red); swung faster than it can go, it
strains as it falls behind your hand (CG_VRB_BladeInertia); and a fast
swing stopped short, or turned back, lands its weight in your hand with a
thud. Not while it's in someone or against another blade, or just after:
those are felt as such. cg_vrSaberWeight scales it (0: weightless; up to
2).
=================
*/

#define VRB_HEFT_FROM		1.5f	// m/s (two thirds along the blade) before it pulls at all
#define VRB_HEFT_STOP_MS	220		// a fast swing that slows this quickly...
#define VRB_HEFT_STOP_FRAC	0.35f	// ...to this much of its speed has stopped short
#define VRB_HEFT_QUIET_MS	250		// after meeting someone or a blade: that's what's felt

// by feel: blue, yellow, red, the staff, two sabers
static const float	vrbHeftPull[VR_NUM_FEELS] = { 0.24f, 0.46f, 0.53f, 0.50f, 0.40f };	// at full speed
static const float	vrbHeftStop[VR_NUM_FEELS] = { 0.24f, 0.50f, 0.60f, 0.55f, 0.50f };	// a full-speed swing stopped short...
static const int	vrbHeftStopHold[VR_NUM_FEELS] = { 22, 30, 39, 35, 30 };			// ...held this long (ms)...
static const int	vrbHeftStopDecay[VR_NUM_FEELS] = { 65, 100, 140, 120, 100 };		// ...dying away over this
static const float	vrbHeftGrain[VR_NUM_FEELS] = { 0.1f, 0.3f, 0.4f, 0.35f, 0.3f };		// rougher the heavier
static const float	vrbHeftStrain[VR_NUM_FEELS] = { 0.15f, 0.40f, 0.45f, 0.43f, 0.40f };	// as far behind your hand as it can fall

static void CG_VRB_Heft( void ) {
	const float scale = Com_Clamp( 0.0f, 2.0f, cg_vrSaberWeight.value );
	const int feel = CG_VRB_BladeFeel( qtrue );
	const float full = vrbFullAt[CG_VRB_Weight( qtrue )] * CG_VRB_CutScale();
	int h;

	if ( cg.secondEye ) {
		return;
	}
	CG_VRB_HandsInit();
	for ( h = 0; h < 2; h++ ) {
		const int s = CG_VRB_HandSaber( h );	// (its own saber: the first, or a second one)
		const float v = vrb.swing.handSpeed[h];
		qboolean quiet;
		float pull;

		if ( !CG_VR() || vrbSaber.time != cg.time || !vrbSaber.pose.valid || s < 0 || !vrbSaber.pose.bladeMask[s] || scale <= 0.0f ) {
			vrbHands.heftPeak[h] = 0.0f;
			continue;
		}
		quiet = (qboolean)( vrbHands.touching[h] || vrbHands.inEnt[h] >= 0 ||
			cg.time - vrbHands.knockTime[h] < VRB_HEFT_QUIET_MS || cg.time - vrbHands.blowTime[h] < VRB_HEFT_QUIET_MS );

		// the pull of it moving: little at first, most at full speed; and
		// the strain of it falling behind your hand
		pull = Com_Clamp( 0.0f, 1.0f, ( v - VRB_HEFT_FROM ) / ( full - VRB_HEFT_FROM ) );
		pull = vrbHeftPull[feel] * pull * sqrtf( pull ) * scale;
		if ( vrb.blade[h].valid && vrb.blade[h].outTime == cg.time ) {
			pull = Q_max( pull, vrbHeftStrain[CG_VRB_BladeFeel( qfalse )] * Com_Clamp( 0.0f, 1.0f, vrb.blade[h].behind ) * scale );
		}
		if ( pull > 0.0f && !quiet ) {
			CG_VRB_FeelSteady( h, Q_min( 1.0f, pull ), vrbHeftGrain[feel] );
		}

		// a fast swing stopped short (or turned back): a thud as its weight
		// lands in your hand
		if ( v >= vrbHands.heftPeak[h] || cg.time - vrbHands.heftPeakTime[h] > VRB_HEFT_STOP_MS ) {
			vrbHands.heftPeak[h] = v;	// (faster still; or it slowed gently: a new swing from here)
			vrbHands.heftPeakTime[h] = cg.time;
		} else if ( v < VRB_HEFT_STOP_FRAC * vrbHands.heftPeak[h] && vrbHands.heftPeak[h] >= 0.55f * full ) {
			const float k = Com_Clamp( 0.0f, 1.0f, vrbHands.heftPeak[h] / full ) * scale;
			if ( !quiet ) {
				CG_VRB_FeelBlade( h, vrbHeftStop[feel] * k, vrbHeftStopHold[feel], 0.6f * vrbHeftStop[feel] * k,
					vrbHeftStopDecay[feel], vrbHeftGrain[feel] );
				CG_VRB_Log( "feel: your %s swing stopped short from %.1f m/s\n", vrbFeelNames[feel], vrbHands.heftPeak[h] );
			}
			vrbHands.heftPeak[h] = v;
			vrbHands.heftPeakTime[h] = cg.time;
		}
	}
}

/*
--------------------------------------------------------------------------------
The game's word on it (its events), and the rest
--------------------------------------------------------------------------------
*/

// How hard a saber hit feels, from its event (WP_SaberDoHit: 1 = 20 damage
// or more, 2 = 5 or more, 3 = less, like a burn; 0 = not flesh).
static float CG_VRB_HitStrength( int eventParm ) {
	switch ( eventParm ) {
	case 1:		return 1.0f;
	case 2:		return 0.75f;
	case 3:		return 0.4f;
	default:	return 0.5f;
	}
}

// The hand holding the saber an event names (its weapon: which saber).
static int CG_VRB_EventHand( int saberNum ) {
	const int h = CG_VRB_SaberHand( ( saberNum == 1 ) ? 1 : 0 );

	return ( h >= 0 ) ? h : vrb.hold[0].hand;
}

// Feel what your saber does: cutting, clashing, being cut.
void CG_VR_SaberEventFeel( int event, const entityState_t *es ) {
	if ( !CG_VR() ) {
		return;
	}
	CG_VRB_HandsInit();
	if ( event == EV_SABER_HIT ) {
		if ( es->otherEntityNum2 == cg.clientNum && es->otherEntityNum != ENTITYNUM_NONE ) {
			// your blade cut something: the bite of it - a rumble, deeper and
			// longer the heavier the blade and the harder the cut (and, if
			// your blade's meeting them wasn't felt just now, the blow too)
			static const int rumble[3] = { 170, 210, 250 }, blow[3] = { 38, 50, 60 };
			const int weight = CG_VRB_Weight( qtrue );
			const float strength = CG_VRB_HitStrength( es->eventParm );
			const int hand = CG_VRB_EventHand( es->weapon );
			qboolean felt;
			if ( hand < 0 ) {
				return;	// (in neither hand - back in its holster as the game's word on it came)
			}
			felt = (qboolean)( vrbHands.blowEnt[hand] == es->otherEntityNum &&
				cg.time - vrbHands.blowTime[hand] >= 0 && cg.time - vrbHands.blowTime[hand] < VRB_CONFIRM_WINDOW );
			if ( felt ) {
				CG_VRB_FeelBlade( hand, 0.85f * strength, 20, 0.75f * strength, rumble[weight], 0.35f );
			} else {
				CG_VRB_FeelBlade( hand, strength, blow[weight], 0.75f * strength, rumble[weight], 0.35f );
				vrbHands.blowEnt[hand] = es->otherEntityNum;
				vrbHands.blowTime[hand] = cg.time;
			}
			if ( es->eventParm ) {
				CG_VRB_BladeImpulse( hand, qfalse, strength );
			}
			CG_VRB_Log( "event: your %s blade cut %i (hit strength %i%s)\n", vrbWeightNames[weight], es->otherEntityNum,
				es->eventParm, felt ? ", felt as it met them" : "" );
		} else if ( es->otherEntityNum == cg.clientNum ) {
			// you were cut: a jolt through both hands
			const float a = CG_VRB_HitStrength( es->eventParm );
			CG_VRB_Feel( VR_HAND_LEFT, 0.8f * a, 40, 0.5f * a, 160, 0.4f );
			CG_VRB_Feel( VR_HAND_RIGHT, 0.8f * a, 40, 0.5f * a, 160, 0.4f );
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
			hand = CG_VRB_EventHand( es->weapon );
		} else {
			hand = CG_VRB_SaberHandNear( es->origin, 32.0f );
		}
		if ( hand >= 0 ) {
			// a hard knock, longer the heavier the blade; the blade stops. If
			// the blades' meeting was felt as you saw it, just a little more.
			static const int len[3] = { 75, 100, 130 };
			const int weight = CG_VRB_Weight( qtrue );
			const qboolean felt = (qboolean)( cg.time - vrbHands.knockTime[hand] >= 0 && cg.time - vrbHands.knockTime[hand] < VRB_CONFIRM_WINDOW );
			if ( felt ) {
				CG_VRB_FeelBlade( hand, 0.6f, 15, 0.4f, 60, 0.0f );
			} else {
				CG_VRB_FeelBlade( hand, 1.0f, len[weight], 0.55f, 90, 0.0f );
				vrbHands.knockTime[hand] = cg.time;
			}
			CG_VRB_BladeImpulse( hand, qtrue, 1.0f );
			CG_VRB_Log( "event: clash on your %s blade (%s%s)\n", vrbWeightNames[weight],
				es->otherEntityNum2 == cg.clientNum ? "your swing" : "their blade", felt ? ", felt as they met" : "" );
		}
	} else if ( event == EV_SABER_BLOCK && es->generic1 == VR_EVENT_BOLT && es->otherEntityNum2 == cg.clientNum ) {
		// your blade deflected a bolt (g_vrsaber.c VRS_DeflectBolt): a sharp
		// tap in that hand - it comes a moment after it met it, so the blade
		// has to be near it still
		const int hand = ( CG_VRB_SaberHandNear( es->origin, 64.0f ) >= 0 ) ? CG_VRB_EventHand( es->weapon ) : -1;
		if ( hand >= 0 ) {
			CG_VRB_FeelBlade( hand, 0.8f, 25, 0.3f, 50, 0.0f );
			CG_VRB_Log( "event: your blade deflected a bolt\n" );
		}
	}
}

// You killed someone with the saber: a long, heavy rumble in the hand(s)
// that did it.
void CG_VR_KillFeel( const entityState_t *es ) {
	int hand = vrb.hold[0].hand;

	if ( !CG_VR() || es->otherEntityNum2 != cg.clientNum || es->otherEntityNum == cg.clientNum || es->eventParm != MOD_SABER ) {
		return;
	}
	CG_VRB_HandsInit();
	if ( vrbHands.blowEnt[VR_HAND_LEFT] == es->otherEntityNum &&
		( vrbHands.blowEnt[VR_HAND_RIGHT] != es->otherEntityNum || vrbHands.blowTime[VR_HAND_LEFT] > vrbHands.blowTime[VR_HAND_RIGHT] ) ) {
		hand = VR_HAND_LEFT;	// (the left hand's blade did it)
	}
	CG_VRB_FeelBlade( hand, 1.0f, 70, 0.85f, 380, 0.3f );
	CG_VRB_Log( "feel: you killed %i\n", es->otherEntityNum );
}

// Your blade is in a wall (CG_AddSaberBlade's sparks): a grinding buzz while it is.
void CG_VR_SaberWallFeel( centity_t *cent, int saberNum ) {
	if ( cent != vrb.bodyCent || vrb.bodyTime != cg.time || !CG_VR() ) {
		return;
	}
	CG_VRB_FeelSteady( CG_VRB_SaberHand( saberNum ), 0.35f, 0.6f );
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
	CG_VRB_UnboltHands( cent );
	if ( vrb.armsG2 == cent->ghoul2 ) {
		CG_VRB_ResetArms( cent->ghoul2 );
		vrb.armsG2 = NULL;
		vrb.poleValid[0] = vrb.poleValid[1] = qfalse;
	}
	CG_VRB_ClearExtra( cent->ghoul2 );
	CG_VRB_ClearBody( cent->ghoul2 );
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
	if ( cent->currentState.number != cg.clientNum ) {
		CG_VRB_HideHolstered( cent, qfalse );	// (yours stay so: your holsters are drawn whatever draws you)
	}
}
