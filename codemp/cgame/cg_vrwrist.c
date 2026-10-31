/*
===========================================================================
OpenJK VR (multiplayer) - the HUD on your wrist.

With the HUD On Wrist (Setup > Controls > VR View: the engine's vr_hudWrist,
vrClientState_t.hudWrist), its status - your health, armor, the Force, ammo
or saber style, the score: CG_DrawStats - isn't drawn in front of you. As it
would have been (CG_VRW_Takes, from CG_Draw2D_HUD), the engine's told it's
there to show, and in which strip of the 640x480 screen it is
(ext.VR_SetWristHud). Once the eyes are drawn the engine draws the frame
once more for it (vrClientState_t.wristPass), in which the status alone is
drawn (CG_VRW_Pass, from CG_DrawActiveFrame) into the wrist's own picture:
all the screen's width and that strip of its height, everything drawn
widened toward the edge (or the middle) its HUD menu's anchored at
(CG_AnchorForMenu: r_aspectCorrect2D's squeeze the other way round), so the
left HUD and the right meet on it in proportion. The engine shows it on the
back of your Force hand's wrist (the hand without the gun) as you turn it to
your face.

The strip (CG_VRW_Strip): the boxes of what the status draws - the HUD
menus' items as the HUD files place them (any HUD's), the score's words, the
simple HUD's numbers (cg_hudFiles 1) - and tall enough that, widened to fill
the picture's height, the left's and the right's don't overlap.

Everything else stays in front of you: the chat, kill messages, centre
prints, the scoreboard, the weapon and Force selection - and the status too
in a vehicle (its HUD: you're at its controls), as a spectator, following
someone or in a demo (theirs, not yours), with Draw HUD off, or with your
Force hand's controller gone a while (the engine says). What drawing it does
whether it's seen or not - the Force meter's warning sound - it does each
frame it would be drawn (CG_StatsElsewhere).

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "cg_local.h"
#include "ui/ui_shared.h"

// what the status draws: a box of the 640x480 screen, widened toward the x
// it's anchored at (ANCHOR2D_LEFT, _MIDDLE or _RIGHT)
typedef struct {
	float	x0, y0, x1, y1;
	float	anchor;
} vrwBox_t;

#define VRW_MAX_BOXES	4
#define VRW_MARGIN		4.0f	// what's drawn is this far from the picture's top and bottom, and the left's and right's
								// from each other twice this (in the 640x480 screen's units, before it's widened)
#define VRW_SCORE		0.7f	// the score's words: their scale (CG_DrawHUD)

/*
================
CG_VRW_Strip

The strip of the screen (top to bottom, all its width) that shows what's in
these boxes on a picture aspect times as wide as it's tall - each widened
toward its anchor as much as the strip's shorter than the picture would have
it (640 / ( ( bottom - top ) * aspect )). Tall enough for all they span, and
for the left's, the middle's and the right's not to overlap widened; around
their middle. qfalse: there's nothing in them.
================
*/
static qboolean CG_VRW_Strip( const vrwBox_t *box, int count, float aspect, float *top, float *bottom ) {
	float y0 = 0.0f, y1 = 0.0f, left = 0.0f, right = 0.0f, midLeft = 0.0f, midRight = 0.0f, widen, h, mid;
	qboolean any = qfalse, middle = qfalse;
	int i;

	for ( i = 0; i < count; i++ ) {
		const vrwBox_t *b = &box[i];
		if ( !( b->x1 > b->x0 ) || !( b->y1 > b->y0 ) ) {
			continue;	// (nothing in it)
		}
		y0 = any ? Q_min( y0, b->y0 ) : b->y0;
		y1 = any ? Q_max( y1, b->y1 ) : b->y1;
		any = qtrue;
		if ( b->anchor <= ANCHOR2D_LEFT ) {
			left = Q_max( left, b->x1 - ANCHOR2D_LEFT );
		} else if ( b->anchor >= ANCHOR2D_RIGHT ) {
			right = Q_max( right, ANCHOR2D_RIGHT - b->x0 );
		} else {
			midLeft = Q_max( midLeft, b->anchor - b->x0 );
			midRight = Q_max( midRight, b->x1 - b->anchor );
			middle = qtrue;
		}
	}
	if ( !any || !( aspect > 0.0f ) ) {
		return qfalse;
	}

	// widened, the left's from the left edge and the right's from the right,
	// the middle's either way from the middle - as far as each can be
	left += VRW_MARGIN;
	right += VRW_MARGIN;
	if ( middle ) {
		widen = Q_min( ( SCREEN_WIDTH * 0.5f ) / ( left + midLeft + VRW_MARGIN ), ( SCREEN_WIDTH * 0.5f ) / ( right + midRight + VRW_MARGIN ) );
	} else {
		widen = SCREEN_WIDTH / ( left + right );
	}
	h = Q_max( y1 - y0 + 2.0f * VRW_MARGIN, SCREEN_WIDTH / ( widen * aspect ) );
	h = Q_min( h, (float)SCREEN_HEIGHT * 2.0f );	// (a HUD all over the screen: small, all of it)
	mid = ( y0 + y1 ) * 0.5f;
	*top = mid - h * 0.5f;
	*bottom = mid + h * 0.5f;
	return qtrue;
}

// a box round both
static void CG_VRW_Grow( vrwBox_t *b, float x0, float y0, float x1, float y1 ) {
	if ( !( b->x1 > b->x0 ) ) {
		b->x0 = x0;
		b->y0 = y0;
		b->x1 = x1;
		b->y1 = y1;
		return;
	}
	b->x0 = Q_min( b->x0, x0 );
	b->y0 = Q_min( b->y0, y0 );
	b->x1 = Q_max( b->x1, x1 );
	b->y1 = Q_max( b->y1, y1 );
}

// What a HUD menu draws (CG_DrawHUD): its items where the HUD file has them,
// round the menu's own box - anchored where CG_DrawHUD anchors it.
static qboolean CG_VRW_MenuBox( const menuDef_t *menu, vrwBox_t *b ) {
	int i;

	memset( b, 0, sizeof( *b ) );
	if ( !menu ) {
		return qfalse;
	}
	b->anchor = CG_AnchorForMenu( menu );
	if ( menu->window.rect.w > 0.0f && menu->window.rect.h > 0.0f ) {
		CG_VRW_Grow( b, menu->window.rect.x, menu->window.rect.y, menu->window.rect.x + menu->window.rect.w,
			menu->window.rect.y + menu->window.rect.h );
	}
	for ( i = 0; i < menu->itemCount; i++ ) {
		const rectDef_t *r = &menu->items[i]->window.rect;
		const float x0 = ( r->w < 0.0f ) ? r->x + r->w : r->x, x1 = ( r->w < 0.0f ) ? r->x : r->x + r->w;
		const float y0 = ( r->h < 0.0f ) ? r->y + r->h : r->y, y1 = ( r->h < 0.0f ) ? r->y : r->y + r->h;
		if ( r->w == 0.0f && r->h == 0.0f ) {
			continue;	// (a place, not a box: the score's, below)
		}
		CG_VRW_Grow( b, x0, y0, Q_max( x1, x0 + 1.0f ), Q_max( y1, y0 + 1.0f ) );
	}
	return ( b->x1 > b->x0 ) ? qtrue : qfalse;
}

// What the status draws this frame, box by box (CG_DrawHUD's): the number
// of them.
static int CG_VRW_Boxes( vrwBox_t *box ) {
	menuDef_t *menu;
	itemDef_t *item;
	int n = 0;

	if ( cg_hudFiles.integer ) {
		// the simple HUD's numbers: health and armor at the left, the Force and
		// ammo or saber style at the right (CG_DrawHUD, CG_DrawSimple*)
		vrwBox_t l = { 12.0f, SCREEN_HEIGHT - 44.0f, 64.0f, SCREEN_HEIGHT - 4.0f, ANCHOR2D_LEFT };
		vrwBox_t r = { SCREEN_WIDTH - 72.0f, SCREEN_HEIGHT - 44.0f, SCREEN_WIDTH - 8.0f, SCREEN_HEIGHT - 4.0f, ANCHOR2D_RIGHT };
		box[n++] = l;
		box[n++] = r;
		return n;
	}
	if ( CG_VRW_MenuBox( Menus_FindByName( "lefthud" ), &box[n] ) ) {
		n++;
	}
	menu = Menus_FindByName( "righthud" );
	if ( CG_VRW_MenuBox( menu, &box[n] ) ) {
		// the score's words, round the score's place (UI_RIGHT: their middle
		// there, CG_DrawScaledProportionalString) - as long as it gets
		item = ( cgs.gametype != GT_POWERDUEL ) ? Menu_FindItemByName( menu, "score_line" ) : NULL;
		if ( item ) {
			const char *words = va( "%s: 9999/9999", CG_GetStringEdString( "MP_INGAME", "SCORE" ) );
			const float w = (float)CG_Text_Width( words, VRW_SCORE, FONT_MEDIUM ), h = (float)CG_Text_Height( words, VRW_SCORE, FONT_MEDIUM );
			CG_VRW_Grow( &box[n], item->window.rect.x - w * 0.5f, item->window.rect.y, item->window.rect.x + w * 0.5f, item->window.rect.y + h );
		}
		n++;
	}
	return n;
}

/*
================
CG_VRW_Takes

The status goes on your wrist this frame, not in front of you (CG_Draw2D_HUD:
it's not drawn there): the engine's told it's there to show, and where. In a
vehicle, a spectator, following someone, a demo, the engine not saying it's
on, with nothing to show: it's drawn in front of you.
================
*/
qboolean CG_VRW_Takes( void ) {
	const vrClientState_t *vr = CG_VR();
	vrwBox_t box[VRW_MAX_BOXES];
	float top, bottom;
	int n;

	if ( !vr || !vr->hudWrist || !trap->ext.VR_SetWristHud ) {
		return qfalse;
	}
	if ( cg.predictedPlayerState.m_iVehicleNum || cg.predictedPlayerState.pm_type == PM_SPECTATOR ) {
		return qfalse;	// (a vehicle's HUD, at its controls; a spectator's none)
	}
	if ( ( cg.snap && ( cg.snap->ps.pm_flags & PMF_FOLLOW ) ) || cg.demoPlayback ) {
		return qfalse;	// (following someone, a demo: theirs, not yours)
	}
	n = CG_VRW_Boxes( box );
	if ( !CG_VRW_Strip( box, n, VR_WRIST_ASPECT, &top, &bottom ) ) {
		return qfalse;
	}
	trap->ext.VR_SetWristHud( top, bottom );
	return qtrue;
}

/*
================
CG_VRW_Pass

The frame the engine draws once more for your wrist: the status alone
(CG_DrawActiveFrame - qtrue: it's this one, and it's drawn).
================
*/
qboolean CG_VRW_Pass( void ) {
	const vrClientState_t *vr = CG_VR();

	if ( !vr || !vr->wristPass ) {
		return qfalse;
	}
	if ( cg.snap ) {
		CG_DrawStatsOnly();
	}
	return qtrue;
}
