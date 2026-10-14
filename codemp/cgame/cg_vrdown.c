/*
===========================================================================
OpenJK VR (multiplayer) - roleplay, stage 2, the player's side (the
server's is game/g_vrdown.c): being down, getting someone up, giving up.

DOWN - your own player state says so (VRPOSE_DOWNED, bg_vrpose.c): a panel
says how it ends - someone of your faction holding use on you (in the
headset, the right grip) for 3 seconds gets you up, or you hold attack (the
trigger) for 1.5 seconds to give up, a bar filling as you hold it (as the
server counts it: from a press made after you went down). In the headset
your hands feel it as you go down, and as you're got up.

SOMEONE DOWN - the server says who, and their faction (rpdn): look at them
near enough - aim at them, or (in the headset, when the server sees where
it looks: CG_VRB_GazeShared) look at them - and a line says so, and, of
your faction, how to get them up; holding use on them, a bar fills (rpup:
from when the server started counting). As the server sees it
(g_vrdown.c). Someone of your faction going down near you: a line says so
(and a light buzz). Bodies down stay facing the way they went down
(cg_players.c), as for a pose.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "cg_local.h"
#include "ui/menudef.h"

#define RPD_HELP_TIME	3000	// (as the server's: g_vrdown.c)
#define RPD_GIVEUP_TIME	1500
#define RPD_REACH		128.0f
#define RPD_WIDE		40.0f
#define RPD_SEE_DOWN	12.0f
#define RPD_NEAR		1360.0f	// someone of your faction going down this near (40 m): you're told
#define RPD_FONT		FONT_MEDIUM

static const char *rpdFactions[RPF_NUM] = { "Heroes", "Villains", "Neutral" };

static struct {
	int			down[MAX_CLIENTS];	// the faction of those down (-1: up)
	int			helping, helpFrom;	// whom you're getting up (-1: nobody), since (the server's time)
	int			helper, helperFrom;	// who's getting you up (-1: nobody), since
	qboolean	wasDown;
	qboolean	attackFree;			// attack let go of since you went down
	int			giveUpFrom;			// cg.time you've held it since (0: you haven't)
	int			look;				// someone down you look at (-1: nobody)
} rpd;

void CG_RP_DownInit( void ) {
	int i;

	memset( &rpd, 0, sizeof( rpd ) );
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		rpd.down[i] = -1;
	}
	rpd.helping = rpd.helper = rpd.look = -1;
}

// Whether you're down.
qboolean CG_RP_IsDown( void ) {
	const playerState_t *ps = &cg.predictedPlayerState;

	return (qboolean)( cg.snap && ps->clientNum == cg.clientNum && ps->pm_type != PM_DEAD && BG_VRDowned( ps ) );
}

// Whether a body is down, or getting up from it (cg_players.c: it keeps
// facing the way it went down).
qboolean CG_RP_DownBody( const centity_t *cent ) {
	const int n = cent->currentState.number;

	if ( n < 0 || n >= MAX_CLIENTS ) {
		return qfalse;
	}
	return (qboolean)( rpd.down[n] >= 0 || ( cent->poseYawSet && cent->currentState.legsAnim == BOTH_GETUP1 ) );
}

static void CG_RP_DownFeel( float peak, int hold, float tail, int decay ) {
	if ( CG_VR() ) {
		CG_VR_Feel( VR_HAND_LEFT, peak, hold, tail, decay );
		CG_VR_Feel( VR_HAND_RIGHT, peak, hold, tail, decay );
	}
}

static const char *CG_RP_DownName( int n ) {
	return ( n >= 0 && n < MAX_CLIENTS && cgs.clientinfo[n].infoValid ) ? cgs.clientinfo[n].name : "Someone";
}

/*
================================================================================
What the server says
================================================================================
*/

// rpdn <client> <faction>: client is down (of that faction), or up (-1)
void CG_RP_DownCmd( void ) {
	const int n = atoi( CG_Argv( 1 ) );
	int faction = atoi( CG_Argv( 2 ) ), was;

	if ( trap->Cmd_Argc() < 3 || n < 0 || n >= MAX_CLIENTS ) {
		return;
	}
	if ( faction < 0 || faction >= RPF_NUM ) {
		faction = -1;
	}
	was = rpd.down[n];
	rpd.down[n] = faction;
	if ( faction >= 0 && was < 0 && n != cg.clientNum && faction == CG_RP_MyFaction() && cg_entities[n].currentValid &&
		Distance( cg_entities[n].lerpOrigin, cg.predictedPlayerState.origin ) < RPD_NEAR ) {
		CG_RP_Notice( va( "%s" S_COLOR_WHITE " is down", CG_RP_DownName( n ) ) );
		CG_RP_DownFeel( 0.3f, 60, 0.0f, 0 );
	}
}

// rpup <helper> <client> <time>: helper is getting client up since time
// (the server's); 0: stopped; -1: got them up
void CG_RP_UpCmd( void ) {
	const int h = atoi( CG_Argv( 1 ) ), n = atoi( CG_Argv( 2 ) ), t = atoi( CG_Argv( 3 ) );

	if ( trap->Cmd_Argc() < 4 || h < 0 || h >= MAX_CLIENTS || n < 0 || n >= MAX_CLIENTS ) {
		return;
	}
	if ( h == cg.clientNum ) {
		if ( t > 0 ) {
			rpd.helping = n;
			rpd.helpFrom = t;
		} else if ( rpd.helping == n ) {
			rpd.helping = -1;
		}
		if ( t < 0 ) {
			CG_RP_Notice( va( "You got %s" S_COLOR_WHITE " up", CG_RP_DownName( n ) ) );
			CG_RP_DownFeel( 0.4f, 80, 0.0f, 0 );
		}
	}
	if ( n == cg.clientNum ) {
		if ( t > 0 ) {
			rpd.helper = h;
			rpd.helperFrom = t;
		} else if ( rpd.helper == h ) {
			rpd.helper = -1;
		}
		if ( t < 0 ) {
			CG_RP_Notice( va( "%s" S_COLOR_WHITE " got you up", CG_RP_DownName( h ) ) );
			CG_RP_DownFeel( 0.5f, 120, 0.2f, 200 );
		}
	}
}

/*
================================================================================
Every frame
================================================================================
*/

// Whether a look from start along dir reaches the middle of n (down) near
// enough to get them up, and how far off it (squared): as the server sees
// it (g_vrdown.c's G_RP_Sees).
static qboolean CG_RP_DownSees( int n, const vec3_t start, const vec3_t dir, float *off ) {
	vec3_t mid, d;
	float dist2, along;
	trace_t tr;

	VectorCopy( cg_entities[n].lerpOrigin, mid );
	mid[2] -= RPD_SEE_DOWN;
	VectorSubtract( mid, start, d );
	dist2 = VectorLengthSquared( d );
	along = DotProduct( d, dir );
	if ( along <= 0.0f || dist2 > RPD_REACH * RPD_REACH ) {
		return qfalse;
	}
	*off = dist2 - along * along;
	if ( *off > RPD_WIDE * RPD_WIDE ) {
		return qfalse;
	}
	CG_Trace( &tr, start, NULL, NULL, mid, cg.clientNum, MASK_SOLID );
	return (qboolean)( tr.fraction >= 1.0f );	// (no wall between: bodies aren't solid to it)
}

// Someone down you look at, near enough to get them up - by your aim (your
// eye point and view, as the server has them: a VR player's right hand,
// holding a gun) or where your headset looks (from the camera, as your
// avatar tells the server) - the nearest your look; -1: nobody.
static int CG_RP_DownLook( void ) {
	const playerState_t *ps = &cg.predictedPlayerState;
	vec3_t aimFrom, aim, headFrom, head;
	matrix3_t axis;
	qboolean gaze;
	float bestOff = 0.0f, off;
	int i, best = -1;

	if ( CG_RP_IsDown() || ps->clientNum != cg.clientNum || ps->pm_type == PM_DEAD || ps->stats[STAT_HEALTH] <= 0 ||
		( cg.snap->ps.pm_flags & PMF_FOLLOW ) || cg.snap->ps.pm_type == PM_INTERMISSION ) {
		return -1;
	}
	VectorCopy( ps->origin, aimFrom );
	aimFrom[2] += ps->viewheight;
	AngleVectors( ps->viewangles, aim, NULL, NULL );
	gaze = (qboolean)( CG_VRB_GazeShared() && CG_VR_HeadPose( headFrom, axis ) );
	if ( gaze ) {
		VectorCopy( cg.refdef.vieworg, headFrom );
		VectorCopy( axis[0], head );
	}
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		if ( rpd.down[i] < 0 || i == cg.clientNum || !cg_entities[i].currentValid ) {
			continue;
		}
		if ( CG_RP_DownSees( i, aimFrom, aim, &off ) && ( best < 0 || off < bestOff ) ) {
			best = i;
			bestOff = off;
		}
		if ( gaze && CG_RP_DownSees( i, headFrom, head, &off ) && ( best < 0 || off < bestOff ) ) {
			best = i;
			bestOff = off;
		}
	}
	return best;
}

void CG_RP_DownFrame( void ) {
	const qboolean down = CG_RP_IsDown();

	if ( !cg.snap ) {
		return;
	}
	if ( down && !rpd.wasDown ) {
		rpd.attackFree = qfalse;	// (held as you went down: let go first)
		rpd.giveUpFrom = 0;
		CG_RP_DownFeel( 0.8f, 150, 0.35f, 400 );
	}
	if ( down ) {
		usercmd_t cmd;
		if ( trap->GetUserCmd( trap->GetCurrentCmdNumber(), &cmd ) && ( cmd.buttons & BUTTON_ATTACK ) ) {
			if ( rpd.attackFree && !rpd.giveUpFrom ) {
				rpd.giveUpFrom = cg.time ? cg.time : 1;
			}
		} else {
			rpd.attackFree = qtrue;
			rpd.giveUpFrom = 0;
		}
	} else {
		rpd.giveUpFrom = 0;
		rpd.helper = -1;	// (up: nobody's getting you up)
	}
	if ( rpd.helping >= 0 && rpd.down[rpd.helping] < 0 ) {
		rpd.helping = -1;	// (up already, or gone)
	}
	rpd.wasDown = down;
	rpd.look = CG_RP_DownLook();
}

/*
================================================================================
Drawing
================================================================================
*/

static void CG_RP_DownLine( float y, float scale, const vec4_t color, const char *text ) {
	const float w = (float)CG_Text_Width( text, scale, RPD_FONT );
	CG_Text_Paint( 320.0f - w * 0.5f, y, scale, (float *)color, text, 0, 0, ITEM_TEXTSTYLE_SHADOWED, RPD_FONT );
}

// A bar filling over total ms from since.
static void CG_RP_DownBar( float y, int since, int total ) {
	vec4_t back = { 0.0f, 0.0f, 0.0f, 0.6f }, fill = { 0.55f, 0.8f, 1.0f, 0.9f }, edge = { 0.8f, 0.85f, 1.0f, 0.8f };
	const float w = 200.0f, h = 8.0f;
	const float f = Com_Clamp( 0.0f, 1.0f, (float)( cg.time - since ) / (float)total );

	CG_FillRect( 320.0f - w * 0.5f, y, w, h, back );
	CG_FillRect( 320.0f - w * 0.5f, y, w * f, h, fill );
	CG_DrawRect( 320.0f - w * 0.5f, y, w, h, 1.0f, edge );
}

void CG_RP_DrawDown( void ) {
	const qboolean vr = (qboolean)( CG_VR() != NULL );
	const vec4_t red = { 1.0f, 0.45f, 0.4f, 1.0f }, white = { 1.0f, 1.0f, 1.0f, 1.0f }, soft = { 0.85f, 0.85f, 0.9f, 1.0f };
	const float scale = 0.5f, lineH = CG_Text_Height( "A", scale, RPD_FONT ) * 1.35f;
	float y = 318.0f;

	if ( !cg.snap || cg.snap->ps.pm_type == PM_INTERMISSION ) {
		return;
	}
	CG_Anchor2D( ANCHOR2D_MIDDLE );
	if ( CG_RP_IsDown() ) {
		const int mine = CG_RP_MyFaction();
		vec4_t back = { 0.05f, 0.0f, 0.0f, 0.55f };
		CG_FillRect( 160.0f, y - 6.0f, 320.0f, lineH * 4.0f + 20.0f, back );
		CG_RP_DownLine( y, 0.7f, red, "You're down" );
		y += lineH * 1.3f;
		if ( rpd.helper >= 0 ) {
			CG_RP_DownLine( y, scale, white, va( "%s" S_COLOR_WHITE " is getting you up", CG_RP_DownName( rpd.helper ) ) );
			y += lineH;
			CG_RP_DownBar( y, rpd.helperFrom, RPD_HELP_TIME );
			y += 14.0f;
		} else {
			CG_RP_DownLine( y, scale, soft, ( mine >= 0 ) ? va( "Someone of the %s can get you up", rpdFactions[mine] ) :
				"Someone of your side can get you up" );
			y += lineH;
		}
		CG_RP_DownLine( y, scale, soft, vr ? "Hold the trigger to give up" : "Hold attack to give up" );
		y += lineH;
		if ( rpd.giveUpFrom ) {
			CG_RP_DownBar( y, rpd.giveUpFrom, RPD_GIVEUP_TIME );
		}
	} else if ( rpd.helping >= 0 ) {
		CG_RP_DownLine( y, scale, white, va( "Getting %s" S_COLOR_WHITE " up", CG_RP_DownName( rpd.helping ) ) );
		y += lineH;
		CG_RP_DownBar( y, rpd.helpFrom, RPD_HELP_TIME );
	} else if ( rpd.look >= 0 && rpd.down[rpd.look] >= 0 ) {	// (up since the frame looked: not)
		const int f = rpd.down[rpd.look];
		if ( f == CG_RP_MyFaction() ) {
			CG_RP_DownLine( y, scale, white, va( "%s" S_COLOR_WHITE " is down: hold %s to get them up", CG_RP_DownName( rpd.look ),
				vr ? "the right grip" : "use" ) );
		} else {
			CG_RP_DownLine( y, scale, soft, va( "%s" S_COLOR_WHITE " is down (%s)", CG_RP_DownName( rpd.look ), rpdFactions[f] ) );
		}
	}
}

// Your side, as the NPCs see it (cg_draw.c: whom the crosshair shows as a
// friend): your faction's, as the server has it (g_vrdown.c); as ever if none.
int CG_RP_NPCSide( void ) {
	switch ( CG_RP_MyFaction() ) {
	case RPF_VILLAINS:	return NPCTEAM_ENEMY;	// (the dark side's)
	case RPF_NEUTRAL:	return NPCTEAM_NEUTRAL;
	default:			return NPCTEAM_PLAYER;	// Heroes, or no character
	}
}

// The wheel's Give up.
void CG_RP_GiveUp( void ) {
	if ( CG_RP_IsDown() ) {
		trap->SendClientCommand( "rp giveup" );
	}
}
