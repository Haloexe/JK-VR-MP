/*
===========================================================================
OpenJK VR (multiplayer) - the pose wheel: sit (on a chair or a bench), sit
cross-legged, kneel, lie down (the poses themselves: bg_vrpose.c). In the
headset, hold Y (the engine runs +posewheel), push the right stick toward
one and let go of Y; without it, hold the key bound to +posewheel and move
the mouse toward one. Let go with the stick (or the mouse) back in the
middle and nothing happens. The pose you're in reads "Stand up". What's
picked goes to the server as "pose <name>", if it takes it ("vrPose" in its
info).

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "cg_local.h"
#include "ui/menudef.h"

#define PW_STICK		0.5f	// the stick pushed this far picks...
#define PW_MOUSE		30.0f	// ...or the mouse moved this far (in 640x480 units)...
#define PW_MOUSE_MAX	90.0f	// ...and no further

#define PW_TILE_W		160.0f
#define PW_TILE_H		40.0f
#define PW_REACH_X		140.0f	// the tiles' middles this far from the wheel's, sideways...
#define PW_REACH_Y		80.0f	// ...and up and down

typedef struct {
	int			pose;		// VRPOSE_*
	float		x, y;		// which way (x right, y up)
	const char	*label;
} pwSlot_t;

static const pwSlot_t pwSlots[] = {
	{ VRPOSE_SIT,	 0.0f,	 1.0f,	"Sit" },
	{ VRPOSE_KNEEL,	 1.0f,	 0.0f,	"Kneel" },
	{ VRPOSE_LIE,	 0.0f,	-1.0f,	"Lie down" },
	{ VRPOSE_CROSS,	-1.0f,	 0.0f,	"Sit cross-legged" },
};
#define PW_SLOTS	( (int)ARRAY_LEN( pwSlots ) )

static struct {
	qboolean	open;
	qboolean	mouse;		// picking with the mouse (no headset): the cgame has the keys meanwhile
	float		mx, my;		// the mouse's way from the middle (x right, y up)
	float		wx, wy;		// which way it points now (stick or mouse), for the marker
	int			pick;		// pwSlots[] picked (-1: none)
} pw = { qfalse, qfalse, 0, 0, 0, 0, -1 };

// Whether the server takes the pose command.
static qboolean CG_PW_Served( void ) {
	return (qboolean)( atoi( Info_ValueForKey( CG_ConfigString( CS_SERVERINFO ), "vrPose" ) ) > 0 );
}

// The slot a way points to (x right, y up), when it's pushed far enough.
static int CG_PW_Slot( float x, float y, float enough ) {
	int i, best = -1;
	float bestDot = 0.70710678f;	// (within 45 degrees)

	if ( x * x + y * y < enough * enough ) {
		return -1;
	}
	for ( i = 0; i < PW_SLOTS; i++ ) {
		const float d = ( x * pwSlots[i].x + y * pwSlots[i].y ) / sqrtf( x * x + y * y );
		if ( d >= bestDot ) {
			bestDot = d;
			best = i;
		}
	}
	return best;
}

static void CG_PW_Close( void ) {
	if ( pw.open && pw.mouse ) {
		trap->Key_SetCatcher( trap->Key_GetCatcher() & ~KEYCATCH_CGAME );
	}
	pw.open = qfalse;
	pw.pick = -1;
}

// +posewheel: the wheel up
void CG_PoseWheelDown_f( void ) {
	if ( pw.open || !cg.snap ) {
		return;
	}
	pw.open = qtrue;
	pw.pick = -1;
	pw.mx = pw.my = pw.wx = pw.wy = 0.0f;
	pw.mouse = CG_VR() ? qfalse : qtrue;
	if ( pw.mouse ) {
		trap->Key_SetCatcher( trap->Key_GetCatcher() | KEYCATCH_CGAME );	// (the mouse picks, not turns)
	}
}

// -posewheel: what's picked, taken ("-posewheel cancel": nothing)
void CG_PoseWheelUp_f( void ) {
	int pose;

	if ( !pw.open ) {
		return;
	}
	if ( pw.pick >= 0 && CG_PW_Served() && Q_stricmp( CG_Argv( 1 ), "cancel" ) &&
		!( pw.mouse && ( trap->Key_GetCatcher() & ~KEYCATCH_CGAME ) ) ) {	// (not as the console or a menu took the keys)
		pose = pwSlots[pw.pick].pose;
		if ( pose == BG_VRPose( &cg.predictedPlayerState ) ) {
			trap->SendClientCommand( "pose stand" );
		} else {
			trap->SendClientCommand( va( "pose %s", bgVRPoses[pose].name ) );
		}
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

// Every frame: what's picked (felt in the right hand, in the headset).
void CG_PoseWheelFrame( void ) {
	const vrClientState_t *vr = CG_VR();
	int pick;

	if ( !pw.open ) {
		return;
	}
	if ( pw.mouse && !( trap->Key_GetCatcher() & KEYCATCH_CGAME ) ) {
		pw.open = qfalse;	// (Escape took the keys back: nothing picked)
		pw.pick = -1;
		return;
	}
	if ( pw.mouse ) {
		pw.wx = pw.mx / PW_MOUSE_MAX;
		pw.wy = pw.my / PW_MOUSE_MAX;
		pick = CG_PW_Slot( pw.mx, pw.my, PW_MOUSE );
	} else if ( vr ) {
		pw.wx = vr->stick[VR_HAND_RIGHT][0];
		pw.wy = vr->stick[VR_HAND_RIGHT][1];
		pick = CG_PW_Slot( pw.wx, pw.wy, PW_STICK );
	} else {
		pick = -1;	// (the headset went)
	}
	if ( pick != pw.pick && pick >= 0 && vr ) {
		CG_VR_Feel( VR_HAND_RIGHT, 0.15f, 20, 0.0f, 0 );	// (a tick)
	}
	pw.pick = pick;
}

// Its tile, a pose (or getting up, from the one you're in).
static void CG_PW_Tile( int i, qboolean served ) {
	const pwSlot_t *slot = &pwSlots[i];
	const qboolean picked = (qboolean)( i == pw.pick );
	const char *label = ( slot->pose == BG_VRPose( &cg.predictedPlayerState ) ) ? "Stand up" : slot->label;
	const float cx = 320.0f + slot->x * PW_REACH_X;
	const float cy = 240.0f - slot->y * PW_REACH_Y;
	float scale = 0.6f;
	vec4_t back = { 0.0f, 0.0f, 0.0f, 0.6f }, edge = { 0.6f, 0.75f, 1.0f, 0.6f }, text = { 1.0f, 1.0f, 1.0f, 1.0f };
	float w, h;

	if ( picked ) {
		VectorSet4( back, 0.2f, 0.45f, 0.85f, 0.85f );
		VectorSet4( edge, 0.8f, 0.9f, 1.0f, 1.0f );
	}
	if ( !served ) {
		text[3] = 0.4f;
	}
	CG_FillRect( cx - PW_TILE_W * 0.5f, cy - PW_TILE_H * 0.5f, PW_TILE_W, PW_TILE_H, back );
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
	const qboolean served = CG_PW_Served();
	vec4_t dim = { 0.8f, 0.8f, 0.8f, 0.9f }, mark = { 1.0f, 1.0f, 1.0f, 0.9f };
	const char *hint;
	float w;
	int i;

	if ( !pw.open || !cg.snap ) {
		return;
	}
	for ( i = 0; i < PW_SLOTS; i++ ) {
		CG_PW_Tile( i, served );
	}
	// where it points
	CG_FillRect( 320.0f - 3.0f + pw.wx * 30.0f, 240.0f - 3.0f - pw.wy * 30.0f, 6.0f, 6.0f, mark );

	if ( !served ) {
		hint = "This server doesn't have poses";
		VectorSet4( dim, 1.0f, 0.5f, 0.4f, 1.0f );
	} else if ( pw.mouse ) {
		hint = "Move the mouse to one, then let go";
	} else {
		hint = "Right stick to one, then let go of Y";
	}
	w = CG_Text_Width( hint, 0.5f, FONT_SMALL );
	CG_Text_Paint( 320.0f - w * 0.5f, 240.0f + PW_REACH_Y + PW_TILE_H * 0.5f + 10.0f, 0.5f, dim, hint, 0, 0, ITEM_TEXTSTYLE_SHADOWED, FONT_SMALL );
}
