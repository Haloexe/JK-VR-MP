/*
===========================================================================
OpenJK VR (multiplayer) - the wheel. In the headset, hold Y (the engine
runs +posewheel), push the right stick toward a slot and let go of Y;
without it, hold the key bound to +posewheel and move the mouse toward
one. Out of the middle:

  up      Say...         the chat line (in the headset, with its keyboard)
  right   Poses >        sit (on a chair or a bench), kneel, lie down, sit
                         cross-legged (the poses: bg_vrpose.c); the pose
                         you're in reads "Stand up"
  down    Companions >   (to come)
  left    Roleplay >     read the bio of whom you look at, challenge them
                         to a duel, give up (downed: to come), the Roleplay
                         menu

Resting on a ring (>) for a moment opens it; Back, up and to the left,
goes back. A ring that opens waits for the stick to move before it picks
anything: the stick still points the way it was opened, maybe at one of
the new ring's slots, and that's not picked until it points elsewhere (or
comes back to the middle). What can't be
done now is greyed, and stays where it is; pointed at, the line under the
wheel says why. Let go with the stick (or the mouse) back in the middle
and nothing happens. Poses go to the server as "pose <name>", if it takes
them ("vrPose" in its info).

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "cg_local.h"
#include "ui/menudef.h"

#define PW_STICK		0.5f	// the stick pushed this far picks...
#define PW_MOUSE		30.0f	// ...or the mouse moved this far (in 640x480 units)...
#define PW_MOUSE_MAX	90.0f	// ...and no further
#define PW_REST			400		// ms resting on a ring opens it

#define PW_TILE_W		140.0f
#define PW_TILE_H		34.0f
#define PW_REACH_X		170.0f	// the tiles' middles this far from the wheel's, sideways...
#define PW_REACH_Y		125.0f	// ...and up and down (round an ellipse, eight ways)

// what a slot does
enum {
	PWA_SAY,		// the chat line
	PWA_RING,		// opens a ring (arg)
	PWA_BACK,		// back to the first ring
	PWA_POSE,		// a pose (arg: VRPOSE_*)
	PWA_BIO,		// read their bio
	PWA_DUEL,		// challenge them to a duel
	PWA_GIVEUP,		// give up, downed (to come)
	PWA_MENU,		// the Roleplay menu
	PWA_COMPANION	// an order to your companions (to come)
};

enum { PW_MAIN, PW_POSES, PW_COMPANIONS, PW_ROLEPLAY, PW_RINGS };

typedef struct {
	int			way;		// 0 up, 1 up and right, 2 right... 7 up and left
	const char	*label;
	int			action;		// PWA_*
	int			arg;
} pwSlot_t;

static const pwSlot_t pwMain[] = {
	{ 0, "Say...",			PWA_SAY,		0 },
	{ 2, "Poses",			PWA_RING,		PW_POSES },
	{ 4, "Companions",		PWA_RING,		PW_COMPANIONS },
	{ 6, "Roleplay",		PWA_RING,		PW_ROLEPLAY },
};
static const pwSlot_t pwPoses[] = {
	{ 0, "Sit",				PWA_POSE,		VRPOSE_SIT },
	{ 2, "Kneel",			PWA_POSE,		VRPOSE_KNEEL },
	{ 4, "Lie down",		PWA_POSE,		VRPOSE_LIE },
	{ 6, "Sit cross-legged", PWA_POSE,		VRPOSE_CROSS },
	{ 7, "Back",			PWA_BACK,		0 },
};
static const pwSlot_t pwCompanions[] = {
	{ 0, "Follow me",		PWA_COMPANION,	0 },
	{ 1, "Formation",		PWA_COMPANION,	1 },
	{ 2, "Attack",			PWA_COMPANION,	2 },
	{ 3, "Defend me",		PWA_COMPANION,	3 },
	{ 4, "Hold here",		PWA_COMPANION,	4 },
	{ 5, "Come to me",		PWA_COMPANION,	5 },
	{ 6, "Pose with me",	PWA_COMPANION,	6 },
	{ 7, "Back",			PWA_BACK,		0 },
};
static const pwSlot_t pwRoleplay[] = {
	{ 0, "Read their bio",	PWA_BIO,		0 },
	{ 2, "Duel",			PWA_DUEL,		0 },
	{ 4, "Give up",			PWA_GIVEUP,		0 },
	{ 6, "Roleplay menu",	PWA_MENU,		0 },
	{ 7, "Back",			PWA_BACK,		0 },
};

static const struct {
	const char		*title;
	const pwSlot_t	*slot;
	int				count;
} pwRings[PW_RINGS] = {
	{ "",			pwMain,			(int)ARRAY_LEN( pwMain ) },
	{ "Poses",		pwPoses,		(int)ARRAY_LEN( pwPoses ) },
	{ "Companions",	pwCompanions,	(int)ARRAY_LEN( pwCompanions ) },
	{ "Roleplay",	pwRoleplay,		(int)ARRAY_LEN( pwRoleplay ) },
};

static struct {
	qboolean	open;
	qboolean	mouse;		// picking with the mouse (no headset): the cgame has the keys meanwhile
	int			ring;		// PW_*
	float		mx, my;		// the mouse's way from the middle (x right, y up)
	float		wx, wy;		// which way it points now (stick or mouse), for the marker
	int			pick;		// the ring's slot picked (-1: none)
	int			pickTime;	// cg.time it was
	qboolean	waiting;	// a ring just opened: not picking until the stick moves...
	int			waitSlot;	// ...off the slot it pointed at then
} pw = { qfalse, qfalse, PW_MAIN, 0, 0, 0, 0, -1, 0, qfalse, -1 };

// Whether the server takes the pose command.
static qboolean CG_PW_Served( void ) {
	return (qboolean)( atoi( Info_ValueForKey( CG_ConfigString( CS_SERVERINFO ), "vrPose" ) ) > 0 );
}

// Which way a slot is (x right, y up).
static void CG_PW_Way( int way, float *x, float *y ) {
	const float a = DEG2RAD( 90.0f - 45.0f * way );
	*x = cosf( a );
	*y = sinf( a );
}

// The slot of the ring a way points to (x right, y up), pushed far enough:
// the nearest, within 45 degrees.
static int CG_PW_Slot( int ring, float x, float y, float enough ) {
	const float len = sqrtf( x * x + y * y );
	float bestDot = 0.70710678f;
	int i, best = -1;

	if ( len < enough ) {
		return -1;
	}
	for ( i = 0; i < pwRings[ring].count; i++ ) {
		float sx, sy, d;
		CG_PW_Way( pwRings[ring].slot[i].way, &sx, &sy );
		d = ( x * sx + y * sy ) / len;
		if ( d > bestDot ) {
			bestDot = d;
			best = i;
		}
	}
	return best;
}

// Why a slot can't be used now (NULL: it can).
static const char *CG_PW_Why( const pwSlot_t *slot ) {
	switch ( slot->action ) {
	case PWA_RING:
		if ( slot->arg == PW_POSES && !CG_PW_Served() ) {
			return "This server doesn't have poses";
		}
		if ( slot->arg == PW_COMPANIONS ) {
			return "Companions are still to come";
		}
		return NULL;
	case PWA_POSE:
		return CG_PW_Served() ? NULL : "This server doesn't have poses";
	case PWA_BIO:
		if ( !CG_RP_On() ) {
			return "Roleplay is off";
		}
		return CG_RP_CanReadBio() ? NULL : "Look at someone playing a character, your weapon away";
	case PWA_DUEL:
		return ( CG_VR_DuelWheelTarget() >= 0 ) ? NULL : "Look at someone near, your saber in hand";
	case PWA_GIVEUP:
		return "Only when you're down";
	case PWA_MENU:
		return CG_RP_On() ? NULL : "Roleplay is off";
	case PWA_COMPANION:
		return "Companions are still to come";
	default:
		return NULL;
	}
}

static void CG_PW_Close( void ) {
	if ( pw.open && pw.mouse ) {
		trap->Key_SetCatcher( trap->Key_GetCatcher() & ~KEYCATCH_CGAME );
	}
	pw.open = qfalse;
	pw.pick = -1;
	pw.ring = PW_MAIN;
}

// Another ring: what the stick points at in it now isn't picked until it
// moves off it (the mouse starts again from the middle).
static void CG_PW_Ring( int ring ) {
	pw.ring = ring;
	pw.pick = -1;
	pw.pickTime = cg.time;
	pw.mx = pw.my = 0.0f;
	pw.waitSlot = pw.mouse ? -1 : CG_PW_Slot( ring, pw.wx, pw.wy, PW_STICK );
	pw.waiting = (qboolean)( pw.waitSlot >= 0 );
	if ( CG_VR() ) {
		CG_VR_Feel( VR_HAND_RIGHT, 0.35f, 40, 0.0f, 0 );	// (a ring opened)
	}
}

// +posewheel: the wheel up
void CG_PoseWheelDown_f( void ) {
	if ( pw.open || !cg.snap ) {
		return;
	}
	pw.open = qtrue;
	pw.ring = PW_MAIN;
	pw.pick = -1;
	pw.waiting = qfalse;
	pw.mx = pw.my = pw.wx = pw.wy = 0.0f;
	pw.mouse = CG_VR() ? qfalse : qtrue;
	if ( pw.mouse ) {
		trap->Key_SetCatcher( trap->Key_GetCatcher() | KEYCATCH_CGAME );	// (the mouse picks, not turns)
	}
}

// What the slot does, let go on it.
static void CG_PW_Do( const pwSlot_t *slot ) {
	switch ( slot->action ) {
	case PWA_SAY:
		trap->SendConsoleCommand( "messagemode\n" );
		break;
	case PWA_POSE:
		if ( slot->arg == BG_VRPose( &cg.predictedPlayerState ) ) {
			trap->SendClientCommand( "pose stand" );
		} else {
			trap->SendClientCommand( va( "pose %s", bgVRPoses[slot->arg].name ) );
		}
		break;
	case PWA_BIO:
		CG_RP_ReadBio();
		break;
	case PWA_DUEL:
		CG_VR_DuelWheel( CG_VR_DuelWheelTarget() );
		break;
	case PWA_MENU:
		trap->SendConsoleCommand( "ui_openmenu vr_rp\n" );	// (ui/vr_rp.menu)
		break;
	default:
		break;	// (a ring, Back: rested on, not let go on)
	}
}

// -posewheel: what's picked, taken ("-posewheel cancel": nothing)
void CG_PoseWheelUp_f( void ) {
	const pwSlot_t *slot;

	if ( !pw.open ) {
		return;
	}
	slot = ( pw.pick >= 0 ) ? &pwRings[pw.ring].slot[pw.pick] : NULL;
	if ( slot && !CG_PW_Why( slot ) && Q_stricmp( CG_Argv( 1 ), "cancel" ) &&
		!( pw.mouse && ( trap->Key_GetCatcher() & ~KEYCATCH_CGAME ) ) ) {	// (not as the console or a menu took the keys)
		CG_PW_Close();	// (first: the chat line or the menu takes the keys next)
		CG_PW_Do( slot );
		return;
	}
	CG_PW_Close();
}

qboolean CG_PoseWheelMouse( int x, int y ) {
	float len;

	if ( !pw.open || !pw.mouse ) {
		return qfalse;
	}
	pw.mx += x * 0.5f;
	pw.my -= y * 0.5f;
	len = sqrtf( pw.mx * pw.mx + pw.my * pw.my );
	if ( len > PW_MOUSE_MAX ) {
		pw.mx *= PW_MOUSE_MAX / len;
		pw.my *= PW_MOUSE_MAX / len;
	}
	return qtrue;
}

qboolean CG_PoseWheelKey( int key, qboolean down ) {
	return pw.open;	// (while it's up, keys don't close it: let go of its key, or Escape)
}

void CG_PoseWheelCancel( void ) {
	CG_PW_Close();
}

// Every frame: what's picked (felt in the right hand, in the headset), and
// a ring rested on opened.
void CG_PoseWheelFrame( void ) {
	const vrClientState_t *vr = CG_VR();
	int pick;

	if ( !pw.open ) {
		return;
	}
	if ( pw.mouse && !( trap->Key_GetCatcher() & KEYCATCH_CGAME ) ) {
		CG_PW_Close();	// (Escape took the keys back: nothing picked)
		pw.mouse = qfalse;
		return;
	}
	if ( pw.mouse ) {
		pw.wx = pw.mx / PW_MOUSE_MAX;
		pw.wy = pw.my / PW_MOUSE_MAX;
		pick = CG_PW_Slot( pw.ring, pw.mx, pw.my, PW_MOUSE );
	} else if ( vr ) {
		pw.wx = vr->stick[VR_HAND_RIGHT][0];
		pw.wy = vr->stick[VR_HAND_RIGHT][1];
		pick = CG_PW_Slot( pw.ring, pw.wx, pw.wy, PW_STICK );
		if ( pw.waiting && pick != pw.waitSlot ) {
			pw.waiting = qfalse;	// (it moved)
		}
		if ( pw.waiting ) {
			pick = -1;
		}
	} else {
		pick = -1;	// (the headset went)
	}
	if ( pick != pw.pick ) {
		if ( pick >= 0 && vr ) {
			CG_VR_Feel( VR_HAND_RIGHT, 0.15f, 20, 0.0f, 0 );	// (a tick)
		}
		pw.pick = pick;
		pw.pickTime = cg.time;
	}
	// resting on a ring (or Back): it opens
	if ( pw.pick >= 0 && cg.time - pw.pickTime >= PW_REST ) {
		const pwSlot_t *slot = &pwRings[pw.ring].slot[pw.pick];
		if ( slot->action == PWA_RING && !CG_PW_Why( slot ) ) {
			CG_PW_Ring( slot->arg );
		} else if ( slot->action == PWA_BACK ) {
			CG_PW_Ring( PW_MAIN );
		}
	}
}

// Where a slot's tile is.
static void CG_PW_Place( int way, float *cx, float *cy ) {
	float x, y;

	CG_PW_Way( way, &x, &y );
	*cx = 320.0f + x * PW_REACH_X;
	*cy = 240.0f - y * PW_REACH_Y;
}

static void CG_PW_Tile( int i ) {
	const pwSlot_t *slot = &pwRings[pw.ring].slot[i];
	const qboolean picked = (qboolean)( i == pw.pick );
	const qboolean usable = (qboolean)!CG_PW_Why( slot );
	const qboolean opens = (qboolean)( slot->action == PWA_RING || slot->action == PWA_BACK );
	vec4_t back = { 0.0f, 0.0f, 0.0f, 0.6f }, edge = { 0.6f, 0.75f, 1.0f, 0.6f }, text = { 1.0f, 1.0f, 1.0f, 1.0f };
	char label[64];
	float cx, cy, scale = 0.55f, w, h;

	if ( slot->action == PWA_POSE && slot->arg == BG_VRPose( &cg.predictedPlayerState ) ) {
		Q_strncpyz( label, "Stand up", sizeof( label ) );
	} else {
		Q_strncpyz( label, slot->label, sizeof( label ) );
	}
	if ( slot->action == PWA_RING ) {
		Q_strcat( label, sizeof( label ), "  >" );
	}
	CG_PW_Place( slot->way, &cx, &cy );
	if ( !usable ) {
		text[3] = 0.35f;
		back[3] = 0.35f;
		edge[3] = 0.25f;
	}
	if ( picked ) {
		if ( usable ) {
			VectorSet4( back, 0.2f, 0.45f, 0.85f, 0.85f );
			VectorSet4( edge, 0.8f, 0.9f, 1.0f, 1.0f );
		} else {
			VectorSet4( edge, 0.8f, 0.8f, 0.8f, 0.6f );
		}
	}
	CG_FillRect( cx - PW_TILE_W * 0.5f, cy - PW_TILE_H * 0.5f, PW_TILE_W, PW_TILE_H, back );
	if ( picked && usable && opens ) {
		// resting on it: it fills, and opens full
		const float f = Com_Clamp( 0.0f, 1.0f, (float)( cg.time - pw.pickTime ) / PW_REST );
		vec4_t fill = { 0.55f, 0.75f, 1.0f, 0.5f };
		CG_FillRect( cx - PW_TILE_W * 0.5f, cy + PW_TILE_H * 0.5f - 4.0f, PW_TILE_W * f, 4.0f, fill );
	}
	CG_DrawRect( cx - PW_TILE_W * 0.5f, cy - PW_TILE_H * 0.5f, PW_TILE_W, PW_TILE_H, picked ? 2.0f : 1.0f, edge );
	w = CG_Text_Width( label, scale, FONT_MEDIUM );
	if ( w > PW_TILE_W - 12.0f ) {
		scale *= ( PW_TILE_W - 12.0f ) / w;	// (fits)
		w = CG_Text_Width( label, scale, FONT_MEDIUM );
	}
	h = CG_Text_Height( label, scale, FONT_MEDIUM );
	CG_Text_Paint( cx - w * 0.5f, cy - h * 0.5f - 2.0f, scale, text, label, 0, 0, ITEM_TEXTSTYLE_SHADOWED, FONT_MEDIUM );
}

void CG_DrawPoseWheel( void ) {
	vec4_t dim = { 0.8f, 0.8f, 0.8f, 0.9f }, mark = { 1.0f, 1.0f, 1.0f, 0.9f };
	const char *hint, *title = pwRings[pw.ring].title;
	float w;
	int i;

	if ( !pw.open || !cg.snap ) {
		return;
	}
	for ( i = 0; i < pwRings[pw.ring].count; i++ ) {
		CG_PW_Tile( i );
	}
	if ( title[0] ) {
		w = CG_Text_Width( title, 0.45f, FONT_MEDIUM );
		CG_Text_Paint( 320.0f - w * 0.5f, 240.0f - 30.0f, 0.45f, dim, title, 0, 0, ITEM_TEXTSTYLE_SHADOWED, FONT_MEDIUM );
	}
	// where it points
	CG_FillRect( 320.0f - 3.0f + pw.wx * 30.0f, 240.0f - 3.0f - pw.wy * 30.0f, 6.0f, 6.0f, mark );

	hint = ( pw.pick >= 0 ) ? CG_PW_Why( &pwRings[pw.ring].slot[pw.pick] ) : NULL;
	if ( hint ) {
		VectorSet4( dim, 1.0f, 0.6f, 0.45f, 1.0f );	// (why it's greyed)
	} else if ( pw.pick >= 0 && pwRings[pw.ring].slot[pw.pick].action == PWA_RING ) {
		hint = "Hold it there to open it";
	} else if ( pw.pick >= 0 && pwRings[pw.ring].slot[pw.pick].action == PWA_BACK ) {
		hint = "Hold it there to go back";
	} else if ( pw.waiting ) {
		hint = "Move the stick to one";
	} else if ( pw.mouse ) {
		hint = "Move the mouse to one, then let go";
	} else {
		hint = "Right stick to one, then let go of Y";
	}
	w = CG_Text_Width( hint, 0.5f, FONT_SMALL );
	CG_Text_Paint( 320.0f - w * 0.5f, 240.0f + PW_REACH_Y + PW_TILE_H * 0.5f + 10.0f, 0.5f, dim, hint, 0, 0, ITEM_TEXTSTYLE_SHADOWED, FONT_SMALL );
}
