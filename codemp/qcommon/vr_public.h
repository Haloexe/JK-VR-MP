/*
===========================================================================
OpenJK VR (multiplayer) - state the engine shares with the cgame module.

The engine owns the headset and controllers. A VR-aware cgame (the one that
ships with OpenJK VR) reads this every frame through
trap->ext.VR_GetClientState, after checking that the cvar "vr_engine" equals
VR_ENGINE_TAG (so it never calls into an engine that lacks the function).

Poses are in "VR render space": Quake axes (X forward, Y left, Z up), metres,
relative to the player's recentred head position, before the yaw rotation
into the game world. VR_ToWorld() below turns them into world coordinates
exactly the way the renderer places the eyes.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#pragma once

#define VR_ENGINE_TAG			"openjkvr-21"	// -3: ext.VR_G2SetModelHidden, -4: ext.VR_G2SetBoneTransform,
												// -5: ext.VR_SetUserPose, ext.VR_Haptic,
												// -6: ext.VR_SetGrabZones, vrClientState_t.grabbing,
												// -7: vrUserPose_t.avatar, ext.VR_SetAvatar (game),
												//     ext.VR_GetAvatar (cgame)
												// -8: ext.R_Anchor2D, ext.R_Aspect2D (cgame, ui:
												//     widescreen 2D)
												// -9: vrClientState_t.stick, poseWheel (the pose
												//     wheel, Y held)
												// -10: ext.VR_SetWeaponHand; vrUserPose_t.hand,
												//     vrAvatarHand_t.saber, hilt, reversed (a saber
												//     in either hand, anywhere along its hilt, either
												//     way round)
												// -11: VR_SetWeaponHand( -1 ) (hands empty: the
												//     triggers press nothing; 2: either attacks),
												//     ext.VR_SetCmdButtons,
												//     ext.VR_GenericCmd, vrClientState_t.trigger
												//     (holsters); VR_GRAB_HOLDS (either way round
												//     with -11 builds: not set or not read, as before)
												// -12: vrClientState_t.fingers (what the controllers
												//     sense of your fingers), vrAvatarHand_t.fingers,
												//     vrAvatar_t.head (VR_POSE_PROTOCOL 5)
												// -13: ext.VR_SetEyeDrop (your height: Real Crouch)
												// -14: vrClientState_t.force (the Force hand: Force
												//     gestures), vrAvatar_t.forceValid, forceHand,
												//     forceDir, vrUserPose_t.thrust (VR_POSE_PROTOCOL 7)
												// -15: vrForceState_t.casts, cast, place (the Force
												//     hand's places: Body Gestures)
												// -16: ext.VR_SetGunHold, vrClientState_t.gunAimAxis
												//     (your gun held with both hands)
												// -17: ext.VR_SetWristHud, vrClientState_t.hudWrist,
												//     wristPass (the HUD on your wrist)
												// -18: ext.VR_SetArmsView (game: a VR player's arms
												//     as games without the VR build are sent them)
												// -19: ext.VR_TintView (cgame: a tint across the
												//     whole view - the Force's, water's, a fall's
												//     fade - round the HUD's screen as well as on it)
												// -20: ext.VR_SetConfigstringFor (game: a configstring
												//     sent otherwise to some clients - a roleplay
												//     companion's look, to the players who said they
												//     have its model)
												// -21: ext.VR_SetSaberTriggers, vrClientState_t.slide
												//     (a hand on a saber's hilt: its trigger tapped,
												//     the saber's switch; held, the hand slides along
												//     the hilt)

// ext.VR_SetGunHold( hand, how, boreLeft, boreUp ): how your other hand holds
// the gun in your gun hand - said every frame (unsaid a moment, not at all)
#define VR_GUN_HOLD_NONE		0	// it doesn't
#define VR_GUN_HOLD_STEADY		1	// round your gun hand (a pistol): the gun aims as your gun hand points it
#define VR_GUN_HOLD_AIM			2	// along its barrel (a rifle): both hands aim it - the line it holds it along runs
										// through it

// ext.VR_SetEyeDrop( drop, flags ): flags
#define VR_EYES_POSED			( 1 << 0 )	// in a pose (getting down into it, or up out of it: bg_vrpose.c)
#define VR_EYES_SWIMMING		( 1 << 1 )	// in water to the waist (as you'd stand: the game swims you)
#define VR_EYES_OWN				( 1 << 2 )	// the camera's at your character's eyes, yours to move (not from
											// behind, a demo, following someone, dead): the drop's theirs

// ext.VR_SetGrabZones( hands | VR_GRAB_HOLDS ): the grips hold what they've
// taken hold of (holsters, Saber Grip: Hold) - a grab is let go of only once
// the hand's mostly open
#define VR_GRAB_HOLDS			( 1 << 2 )

// The HUD on your wrist (vrClientState_t.hudWrist): its picture is this many
// times as wide as it's tall - all the width of the 640x480 screen, and the
// strip of its height ext.VR_SetWristHud says, what's drawn widened toward
// where it's anchored (ext.R_Anchor2D) to stay in proportion
#define VR_WRIST_ASPECT			2.0f

// ext.R_Anchor2D( anchorX ) with anchorX this far left or further: what's
// drawn is stretched across the window again, as always (any other x is a
// place to squeeze toward, off the screen too: a point in the world there)
#define VR_ANCHOR2D_STRETCH		-100000.0f

/*
--------------------------------------------------------------------------------
Physical lightsaber: where the blades you hold really are

Every usercmd a VR client sends carries the pose of the saber blades in its
hands, so the server can fight with them (to servers that put "sv_vrx" with
VR_POSE_PROTOCOL in their systeminfo; see qcommon/vr_net.h). The server
engine hands it to the game module through gameImport_t.ext.VR_GetUserPose.

Positions are world-axis offsets from the camera the player sees the hands
from (cgame refdef.vieworg). viewOffset says where that camera is: its offset
from the eye point, which is the origin plus viewheight of the player state
the pose was rendered with (on the server: before the Pmove of the command
that carries it); it is only the camera's smoothing (crouching, stairs,
prediction errors), so it is usually zero. Directions are world-axis unit
vectors along the blades. trackYaw is the yaw that turns the player's real
room (tracking space) into the world, so the server can tell real arm
movement from snap/smooth turning, running and the camera's smoothing.

valid means a lit saber is in the player's VR hands: the arm swings it and
the attack button doesn't. Its blades can still be missing for a moment (a
flip or roll animates the body, a hitch delayed the pose).

Saber s is the player's first or second saber (saber[0], saber[1]); hand[s]
says which hand holds it - either can hold either (a left-handed player's
first saber is in the left hand; a saber passed from one hand to the other).
twoHanded: the other hand holds that saber's hilt too (grip2 is that hand).
--------------------------------------------------------------------------------
*/
#define VR_POSE_PROTOCOL		7		// 2: twoHanded, grip2; 3: avatar, svc_vrAvatars; 4: hand, avatar hands' saber, hilt, reversed;
										// 5: avatar head, avatar hands' fingers; 6: the avatar's head where its body's eyes go, its
										// shift - bent - where its feet stand (the same bits as 5); 7: the avatar's Force hand, the
										// pose's thrust
#define VR_POSE_HEAD_BACK		4.0f	// (5: the avatar's head was the headset itself - the body's eyes went this far behind it,
										// the way it faced: cg_vrBodyBack as it was)
#define VR_POSE_MAX_SABERS		2		// the first saber, the second (each in either hand)
#define VR_POSE_MAX_BLADES		2		// per saber (staffs have 2)
#define VR_POSE_MAX_OFFSET		1000.0f	// units (16-bit fixed point, 1/32 unit)
#define VR_POSE_MAX_HILT		63.5f	// units: where along its hilt a hand holds a saber, either way (8 bits, 1/2 unit)

typedef struct vrBladePose_s {
	vec3_t			base;		// blade start (the muzzle point), offset from the camera
	vec3_t			dir;		// unit vector along the blade
} vrBladePose_t;

/*
--------------------------------------------------------------------------------
The VR avatar: what everyone else sees of a VR player - which way their body
faces, where their head is and where they look, and their hands, holding
what, their fingers as theirs are.

The player's game sends it with every usercmd (vrUserPose_t.avatar, offsets
from the camera like the saber pose; valid whenever their VR body is drawn,
saber or not). The server's game keeps the hands within reach and makes them
offsets from the player's origin as that usercmd found it (where their body
was when they saw their hands), so the hands move with the body, and hands
it to the engine (gameImport_t.ext.VR_SetAvatar), which sends it after each
snapshot to the other players that run this build (svc_vrAvatars, only to
clients that sent clc_moveVR). Their games pose that player's body with it,
the hands offset from where they draw the player
(cgameImport_t.ext.VR_GetAvatar). Directions are world axes. The head -
where the player's game puts their body's eyes: their headset, with the
neck just behind it - goes as the hands do (an offset from the camera, then
from the player's origin); standing still, their body bends to put its eyes
there (Body Bends: cg_vrbody.c), or is moved whole under it.
--------------------------------------------------------------------------------
*/
enum {
	VR_GRIP_NONE,		// the hand isn't on a controller: the animation has it
	VR_GRIP_FREE,		// an empty hand on the controller
	VR_GRIP_GUN,		// holding a gun: origin and axis are the gun model's
	VR_GRIP_SABER,		// holding its own saber: dir runs along the blade
	VR_GRIP_HILT,		// on the other hand's hilt: dir runs along it through the fist
	VR_NUM_GRIPS
};

// A hand's fingers, as the humanoid skeleton moves them: the thumb, the index
// finger, and the other three together - each 0..1 (qcommon/vr_fingers.h)
enum {
	VR_FINGER_THUMB,	// 0: up, lifted off the controller .. 1: down, on a button, the stick or the thumb rest
	VR_FINGER_INDEX,	// 0: straight .. 0.5: resting on the trigger, half curled .. 1: curled into a fist
	VR_FINGER_OTHERS,	// the middle, ring and little fingers: 0 open .. 1 closed (the grip squeezed)
	VR_NUM_FINGERS
};

typedef struct vrAvatarHand_s {
	int			grip;			// VR_GRIP_*
	vec3_t		origin;			// where the hand holds (the controller's grip as the arm reaches for it); GUN: the gun's origin
								// (offset from the camera, then from the player's origin: see above)
	vec3_t		axis[3];		// the controller as the hand lies on it (forward along it, left, up); GUN: the gun's axes
	vec3_t		dir;			// SABER: along the blade; HILT: along the hilt, toward the thumb
	float		blend;			// HILT: how far the hold has taken the hand over (0..1)
	int			saber;			// SABER, HILT: which of the player's sabers (0: the first, 1: the second)
	float		hilt;			// SABER: where along the hilt the fist is - from where the saber's own tag holds it, toward
								// its (first) blade (units; -VR_POSE_MAX_HILT..VR_POSE_MAX_HILT)
	qboolean	reversed;		// SABER: held the other way round - the blade out of the little finger's side of the fist
	qboolean	fingersValid;	// FREE, GUN: the fingers follow the player's (VR_NUM_FINGERS, 0..1; a gun: only the index's
	float		fingers[VR_NUM_FINGERS];	// drawn) - else the animation has them
} vrAvatarHand_t;

typedef struct vrAvatar_s {
	qboolean		valid;
	float			bodyYaw;		// where the body faces (world yaw)
	vec3_t			headAngles;		// where the head looks (pitch, yaw, roll)
	vec3_t			shift;			// how far the body is drawn from the entity origin (stood under the head; bent, where its
									// feet stand)
	vec3_t			head;			// where the body's eyes go (the headset, the neck just behind it): an offset like the hands'
									// (all 0: none - an older build's demo)
	vrAvatarHand_t	hand[2];		// VR_HAND_LEFT, VR_HAND_RIGHT
	// the hand the player's Force powers come from and aim with - their Force gestures on, that hand empty
	// (FREE): forceHand (VR_HAND_*) - and where it points (forceDir: world axes, unit; the cgame's, from its aim)
	qboolean		forceValid;
	int				forceHand;
	vec3_t			forceDir;
} vrAvatar_t;

typedef struct vrUserPose_s {
	qboolean		valid;		// a lit saber is in the player's VR hands
	float			trackYaw;	// world yaw minus tracking yaw (degrees)
	vec3_t			viewOffset;	// camera minus eye point
	int				bladeMask[VR_POSE_MAX_SABERS];	// blades with a pose (bit per blade)
	int				hand[VR_POSE_MAX_SABERS];		// the hand holding the saber (VR_HAND_*; with a blade)
	vec3_t			grip[VR_POSE_MAX_SABERS];		// the hand holding the saber, offset from the camera
	qboolean		twoHanded[VR_POSE_MAX_SABERS];	// the other hand holds it too...
	vec3_t			grip2[VR_POSE_MAX_SABERS];		// ...here (offset from the camera)
	vrBladePose_t	blade[VR_POSE_MAX_SABERS][VR_POSE_MAX_BLADES];
	vrAvatar_t		avatar;		// the whole player, for everyone else (offsets from the camera)
	// a Force gesture's Push or Pull goes with this command (its generic_cmd): the way the hand thrust it - for a
	// Pull, back toward where it was yanked from; or its Mind Trick's wave: the way it's aimed, level from the eyes
	// toward the hand halfway across (world axes, unit; the engine's: client/cl_vr.cpp). Only with the avatar, whose Force
	// hand it came from; the server's game alone sees it.
	qboolean		thrustValid;
	vec3_t			thrust;
} vrUserPose_t;

/*
--------------------------------------------------------------------------------
A VR player's arms for the games that can't draw their avatar (any other
build: theirs reads no svc_vrAvatars). The server's game works out a held
torso pose and four bones those games turn on top of it - the stock entity
fields boneIndex1..4, boneAngles1..4 and boneOrient, which every game applies
to a player (POSTMULT, cgame CG_G2ServerBoneAngles) - so that they draw the
saber where the VR player holds it, a fist round its hilt (g_vrarms.c).

The game sets it every frame for a player who has had it (ext.VR_SetArmsView;
NULL or set == qfalse: none); the engine puts it into the copies of that
player's entity it sends those games alone (sv_snapshot.cpp), never into the
entity itself, so the game and VR builds see the player as they are. A game's
bones stay turned until it's sent other angles for them, so a player whose
arms stop is sent the same bones unturned (pose qfalse, all angles 0) for as
long as they're in the game.
--------------------------------------------------------------------------------
*/
#define VR_ARMS_BONES			4		// the entity has room for this many
#define VR_ARMS_MAX_AGE			500		// ms: a pose the game hasn't set since then is left out (its bones sent unturned)
#define VR_ARMS_ANIMS			1544	// the game's animations (anims.h MAX_TOTALANIMATIONS): a game of another build
										// drops from the server on any animation number from this on

typedef struct vrArmsView_s {
	qboolean		set;				// sent as below (qfalse: the entity as it is)
	qboolean		pose;				// the held pose: legsAnim, legsFlip, torsoAnim, torsoFlip and angles replace the
	int				legsAnim;			// entity's...
	int				legsFlip;
	int				torsoAnim;
	int				torsoFlip;
	vec3_t			angles;				// ...and its apos.trBase (where the body faces, the head's pitch)
	int				boneIndex[VR_ARMS_BONES];	// CS_G2BONES indices (1..MAX_G2BONES-1; 0: none)
	vec3_t			boneAngles[VR_ARMS_BONES];	// each turned on top of the animation (POSTMULT)...
	int				boneOrient;			// ...with this orientation (forward | right << 3 | up << 6)
} vrArmsView_t;

// Angles that AnglesToAxis turns back into axis (forward, left, up).
static inline void VR_AxisToAngles( vec3_t axis[3], vec3_t angles ) {
	const float len = sqrtf( axis[0][0] * axis[0][0] + axis[0][1] * axis[0][1] );

	if ( len > 1e-5f ) {
		angles[PITCH] = RAD2DEG( atan2f( -axis[0][2], len ) );
		angles[YAW] = RAD2DEG( atan2f( axis[0][1], axis[0][0] ) );
		angles[ROLL] = RAD2DEG( atan2f( axis[1][2], axis[2][2] ) );
	} else {
		// straight up or down: yaw and roll are the same turn, all in roll
		const float sp = ( axis[0][2] < 0.0f ) ? 1.0f : -1.0f;
		angles[PITCH] = sp * 90.0f;
		angles[YAW] = 0.0f;
		angles[ROLL] = RAD2DEG( atan2f( axis[1][0] * sp, axis[1][1] ) );
	}
}

enum {
	VR_HAND_LEFT = 0,
	VR_HAND_RIGHT = 1
};

// What a controller senses of the fingers holding it (vrClientState_t.fingers;
// turned into how far each finger is closed by qcommon/vr_fingers.h): sensed,
// the sensors it has (bound to its inputs: the runtime says); touched, those on
// (or near) now.
#define VR_SENSE_TRIGGER_TOUCH	1		// the index finger on the trigger
#define VR_SENSE_TRIGGER_NEAR	2		// ...near it (Touch Plus, Touch Pro; Touch with its proximity extension)
#define VR_SENSE_INDEX_CURL		4		// ...how far it's curled toward it (indexCurl: Touch Plus, Touch Pro)
#define VR_SENSE_THUMB_TOUCH	8		// the thumb on a button, the stick, the thumb rest or the trackpad
#define VR_SENSE_THUMB_NEAR		16		// ...near them (Touch Plus, Touch Pro; Touch with its proximity extension)

typedef struct vrFingerSense_s {
	int			sensed;			// VR_SENSE_*: the controller has these sensors
	int			touched;		// VR_SENSE_TRIGGER_TOUCH, _TRIGGER_NEAR, _THUMB_TOUCH, _THUMB_NEAR: on, or near, now
	float		trigger;		// how far the trigger's pulled (0..1)
	float		squeeze;		// how far the grip's squeezed (0..1; a grip that only clicks: 0 or 1)
	float		indexCurl;		// VR_SENSE_INDEX_CURL: 0 the index finger straight .. 1 curled onto the trigger
} vrFingerSense_t;

// The Force hand (Force gestures, vr_forceGestures - qcommon/vr_force.h), as
// the engine reads it: what its gestures hold down now (the game's own
// buttons for them: BUTTON_FORCEGRIP, _LIGHTNING, _DRAIN)...
#define VR_FORCE_GRIP			1	// Grip: held out still, the Force grip held
#define VR_FORCE_LIGHTNING		2	// Lightning: its trigger, the Force grip held
#define VR_FORCE_DRAIN			4	// Drain: the same, the palm turned up

// ...and the rest the cgame feels in your hand (cg_vrforce.c)
typedef struct vrForceState_s {
	int			hand;		// your Force hand now - the Force grip's, gestures on, the hand empty, in a game (VR_HAND_*;
							// -1: none)
	qboolean	reaching;	// its Force grip held: reaching out with the Force
	int			held;		// VR_FORCE_*: what its gestures hold down
	int			pushes;		// Pushes, Pulls and quick squeezes (Use Force) its gestures have done, counted: one more is
	int			pulls;		// one more done (the engine's count, from when it started)
	int			taps;
	float		speed;		// the last Push's or Pull's speed (metres a second)
	int			casts;		// the places' powers it's cast (Body Gestures), counted the same...
	int			cast;		// ...the last one (FP_*; -1: none yet)
	int			place;		// the place it's held still in now, one with a power of yours there (VRF_PLACE_*,
							// qcommon/vr_force.h; 0: none)
} vrForceState_t;

typedef struct vrClientState_s {
	// headset running and tracked
	qboolean	active;

	// the usercmd angles come from the gun hand's aim (ranged weapons: the
	// right hand, the left with vr_leftHanded on), so the cgame camera
	// points where the gun points while the player looks around freely
	qboolean	handAim;

	// tracking yaw (degrees) that corresponds to the game camera's yaw, and
	// game units per metre (same values the renderer uses)
	float		referenceYaw;
	float		worldScale;

	vec3_t		headOrigin;
	vec3_t		headAxis[3];

	// grip pose: centre of the palm, axis[0] along the held controller
	// aim pose: axis[0] along the pointing ray
	qboolean	handValid[2];
	vec3_t		gripOrigin[2];
	vec3_t		gripAxis[2][3];
	vec3_t		aimOrigin[2];
	vec3_t		aimAxis[2][3];

	// the hand's grip button is held as a grab (it was pressed while the
	// cgame said the hand could grab something, ext.VR_SetGrabZones), so it
	// didn't press its bound key
	qboolean	grabbing[2];

	// the thumbsticks, [VR_HAND_LEFT] and [VR_HAND_RIGHT]: x to the right,
	// y away from you (-1..1)
	float		stick[2][2];

	// Y is held for the pose wheel (the engine ran +posewheel; -posewheel
	// when it's let go): meanwhile the right stick picks in it, and doesn't
	// turn or switch weapons
	qboolean	poseWheel;

	// each hand's trigger is held (past its press point; whatever it
	// presses - or nothing, VR_SetWeaponHand( -1 ))
	qboolean	trigger[2];

	// what each controller senses of your fingers (above): the cgame's
	// hands follow them (cg_vrFingers)
	vrFingerSense_t	fingers[2];

	// your Force hand (above)
	vrForceState_t	force;

	// the gun hand's aim as that hand alone points it, vr_gunPitch's tilt in
	// - aimAxis has it as both your hands aim the gun (ext.VR_SetGunHold)
	vec3_t		gunAimAxis[3];

	// the HUD's status goes on your wrist (vr_hudWrist: On Wrist - the back of
	// the wrist of the hand your guns aren't in, the left, or left-handed the
	// right, as you turn it to your face): the cgame doesn't draw it in front
	// of you then, but says it would have (ext.VR_SetWristHud, every frame),
	// and the engine draws the frame once more for it - its wristPass, in
	// which the cgame draws the status alone. Not with that hand's controller
	// gone a while (it's in front of you again).
	qboolean	hudWrist;
	qboolean	wristPass;

	// the hand slides along the hilt it's on (ext.VR_SetSaberTriggers: its
	// trigger held VR_SLIDE_MS and on, the cgame's said it's on a saber's
	// hilt) - a tap was the saber's switch instead
	qboolean	slide[2];
} vrClientState_t;

// ext.VR_SetSaberTriggers: a trigger held this long (ms) is a slide, not a tap
#define VR_SLIDE_MS				250

// Heading of a view axis; well defined even looking straight up/down.
// Shared by the engine, renderer and cgame so they always agree.
static inline float VR_AxisYaw( vec3_t axis[3] ) {
	const float hx = axis[2][2] * axis[0][0] - axis[0][2] * axis[2][0];
	const float hy = axis[2][2] * axis[0][1] - axis[0][2] * axis[2][1];
	return RAD2DEG( atan2f( hy, hx ) );
}

// VR render space -> game world, given the game camera (refdef) the frame is
// rendered for. Identical to what the renderer does for the eyes.
// (array parameters are not const so plain C callers don't need casts)
static inline void VR_ToWorld( const vrClientState_t *vr, const vec3_t camOrigin, vec3_t camAxis[3],
	const vec3_t origin, vec3_t axis[3], vec3_t outOrigin, vec3_t outAxis[3] ) {
	const float d = DEG2RAD( VR_AxisYaw( camAxis ) - vr->referenceYaw );
	const float c = cosf( d ), s = sinf( d );
	int i;

	if ( outAxis && axis ) {
		for ( i = 0; i < 3; i++ ) {
			outAxis[i][0] = axis[i][0] * c - axis[i][1] * s;
			outAxis[i][1] = axis[i][0] * s + axis[i][1] * c;
			outAxis[i][2] = axis[i][2];
		}
	}
	if ( outOrigin && origin ) {
		outOrigin[0] = camOrigin[0] + ( origin[0] * c - origin[1] * s ) * vr->worldScale;
		outOrigin[1] = camOrigin[1] + ( origin[0] * s + origin[1] * c ) * vr->worldScale;
		outOrigin[2] = camOrigin[2] + origin[2] * vr->worldScale;
	}
}

/*
--------------------------------------------------------------------------------
The Force from a VR player's Force hand, as a server running this build aims
it (game/g_vrforce.c) - and the cgame picks it (cgame/cg_vrforce.c): the same
numbers on both sides.
--------------------------------------------------------------------------------
*/
#define VR_FORCE_SERVED			2		// "vrForce" in the serverinfo of a server aiming it so (1: one from before these
										// numbers - the cgame picks no one for it)
#define VR_FORCE_SNAP			10.0f	// degrees: someone this near the hand's line is who it's aimed at...
#define VR_FORCE_SNAP_GRIP		15.0f	// ...Grip's, wider
#define VR_FORCE_GRIP_REACH		320.0f	// how far a Grip from the hand reaches (a quarter further than the game's own,
										// MAX_GRIP_DISTANCE: 256) - and holds them
#define VR_FORCE_GRIP_FRONT		0.9f	// a Grip below level 3 holds them while where it points is this near them, side to
										// side (DoGripAction's InFront) - none's aimed at whom it wouldn't hold
#define VR_FORCE_GRIP_THROWN	250.0f	// units a second: whom a Grip moved with the hand, let go of going this fast, is thrown
										// - tossed up off the ground (game/g_vrforce.c), a swoosh in the hand (cg_vrforce.c)
#define VR_FORCE_BODY_FOOT		6.0f	// someone's body, where the Force aimed at them goes: the line up through the middle
#define VR_FORCE_BODY_HEAD		2.0f	// of their box, from this far above its bottom to this far below its top

// Someone's body, up from where they stand (z) - their box's bottom and top
// (lo, hi) from there: from just above the one to just below the other (a box
// too small for that, its middle).
static inline void VR_ForceBodySpan( float z, float lo, float hi, float *bottom, float *top ) {
	*bottom = z + lo + VR_FORCE_BODY_FOOT;
	*top = z + hi - VR_FORCE_BODY_HEAD;
	if ( *top < *bottom ) {
		*bottom = *top = z + ( lo + hi ) * 0.5f;
	}
}

// Where a power aimed from 'from' along dir (unit) goes at someone whose body
// runs straight up through (x, y), from bottom to top: the point on it
// nearest the aim's line (the two lines' nearest points - kept on the body).
static inline void VR_ForceBodyPoint( const vec3_t from, const vec3_t dir, float x, float y, float bottom, float top,
	vec3_t point ) {
	const float b = dir[2];
	const float d = dir[0] * ( from[0] - x ) + dir[1] * ( from[1] - y ) + dir[2] * from[2];
	const float denom = 1.0f - b * b;
	float z = from[2];	// (aimed straight up or down: as high as it comes from)

	if ( denom > 1e-4f ) {
		z = ( from[2] - b * d ) / denom;
	}
	point[0] = x;
	point[1] = y;
	point[2] = ( z < bottom ) ? bottom : ( z > top ) ? top : z;
}
