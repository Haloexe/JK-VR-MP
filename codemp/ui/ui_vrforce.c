/*
===========================================================================
OpenJK VR (multiplayer) - the game's Force menu (ingame_playerforce) for
roleplay's Grey Jedi and Force Masters: what the character you play may be,
as the server tells the game (game/g_vrrp.c, cgame/cg_vrrp.c: ui_rpForceMay,
BG_FORCE_*).

GREY. Where the character may be one, the side picker goes on from Light
and Dark to Grey (UI_VRForce_SideMax), and with Grey picked no power row is
dimmed: both sides' powers, bought with the same points (ui_force.c). The
game's menu has the light side's rows and the dark side's in one place, one
side shown at a time: with both, the dark side's go to the left, where its
templates are - Light and Dark ones, gone while it's Grey - under a heading
(the light side's keep their place, their names blue as ever: there's no
room over them for one), with a note above it (UI_VRForce_Layout: those,
vr_rp.menu's vr_rp_force, put on the menu). A Grey setup where Grey isn't allowed counts
as the side most of its points are on (BG_ForceSideMost), as the server has
it: the menu shows it so, the other side's rows dimmed, and says so; its
side's powers are set once Light or Dark's picked, which makes it that. (It
isn't changed for being shown: what's set stays the character's, Grey for
where it may be.)

FORCE MASTER. Every power at level 3, nothing to spend: the menu shows them
so (UI_VRForce_Shown - those the server turns off, team powers outside team
games and the other side on Force-aligned teams, as the server sets them
up: BG_ForceMasterLevels) and changes nothing - the character's own setup
is kept, for when it's put back.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "ui_local.h"
#include "ui_force.h"

void Menu_ShowItemByName( menuDef_t *menu, const char *p, qboolean bShow );
void Item_UpdatePosition( itemDef_t *item );

#define VRF_MENU		"ingame_playerforce"
#define VRF_SHIFT		222.0f	// the dark side's rows this far left with both sides' (from beside the light side's to the left)
#define VRF_SHIFTED		64		// the rows moved, at most (two items each: the power's name, its level)

// what the menu's items were where the game's file has them: the dark side's rows, by where they are
static struct {
	itemDef_t	*item;
	float		x;
} vrfRows[VRF_SHIFTED];
static int vrfNumRows;

// What the character you play may be (BG_FORCE_*), as the server last told the game.
int UI_VRForce_May( void ) {
	return (int)trap->Cvar_VariableValue( "ui_rpForceMay" ) & ( BG_FORCE_GREY | BG_FORCE_MASTER );
}

// A Force Master's: nothing to set (the side and every power as they are).
qboolean UI_VRForce_Locked( void ) {
	return (qboolean)( ( UI_VRForce_May() & BG_FORCE_MASTER ) != 0 );
}

// The side picker's last side: Grey where the character may be one; Dark.
int UI_VRForce_SideMax( void ) {
	return ( UI_VRForce_May() & BG_FORCE_GREY ) ? FORCE_GREYSIDE : FORCE_DARKSIDE;
}

// The side of the team you're on with Force-aligned teams (red dark, blue
// light; 0: none), as the server sets powers up (the serverinfo, ui_myteam).
static int UI_VRForce_TeamSide( void ) {
	char info[MAX_INFO_STRING];

	trap->GetConfigString( CS_SERVERINFO, info, sizeof( info ) );
	if ( !atoi( Info_ValueForKey( info, "g_forceBasedTeams" ) ) ) {
		return 0;
	}
	switch ( (int)trap->Cvar_VariableValue( "ui_myteam" ) ) {
	case TEAM_RED:
		return FORCE_DARKSIDE;
	case TEAM_BLUE:
		return FORCE_LIGHTSIDE;
	default:
		return 0;
	}
}

// The side most of a setup's points are on (levels: its powers'), as the
// server you're on counts them - its game type and the powers it turns off,
// as its serverinfo says (BG_ForceSideMost).
int UI_VRForce_SideMost( const int *levels ) {
	char info[MAX_INFO_STRING];

	trap->GetConfigString( CS_SERVERINFO, info, sizeof( info ) );
	return BG_ForceSideMost( levels, atoi( Info_ValueForKey( info, "g_gametype" ) ), atoi( Info_ValueForKey( info, "g_forcePowerDisable" ) ) );
}

// The side the menu's setup counts as: Grey, where it may be; a Grey setup
// where it may not, the side most of its points are on; else its own.
int UI_VRForce_Counted( void ) {
	if ( uiForceSide != FORCE_GREYSIDE || ( UI_VRForce_May() & BG_FORCE_GREY ) ) {
		return uiForceSide;
	}
	return UI_VRForce_SideMost( uiForcePowersRank );
}

// Whether both sides' rows show: a Grey setup's, or a Force Master's (unless
// their team's side is all they can have).
static qboolean UI_VRForce_Both( void ) {
	if ( UI_VRForce_Locked() ) {
		return (qboolean)!UI_VRForce_TeamSide();
	}
	return (qboolean)( uiForceSide == FORCE_GREYSIDE );
}

// A power's level as the menu shows it, and whether its row's dimmed (dim): a
// Force Master's, every power's (dimmed, one they don't have); a Grey setup's,
// both sides' - where Grey isn't allowed, the side it doesn't count as dimmed;
// else the setup's side's, the other side's dimmed, as ever.
int UI_VRForce_Shown( int power, qboolean *dim ) {
	int counted;

	if ( power < 0 || power >= NUM_FORCE_POWERS ) {
		*dim = qtrue;
		return 0;
	}
	if ( UI_VRForce_Locked() ) {
		char info[MAX_INFO_STRING];
		int levels[NUM_FORCE_POWERS];
		trap->GetConfigString( CS_SERVERINFO, info, sizeof( info ) );
		BG_ForceMasterLevels( levels, UI_VRForce_TeamSide(), atoi( Info_ValueForKey( info, "g_gametype" ) ),
			atoi( Info_ValueForKey( info, "g_forcePowerDisable" ) ) );
		*dim = (qboolean)!levels[power];
		return levels[power];
	}
	counted = UI_VRForce_Counted();
	*dim = (qboolean)( uiForcePowerDarkLight[power] && counted != FORCE_GREYSIDE && uiForcePowerDarkLight[power] != counted );
	return uiForcePowersRank[power];
}

// The side picker's word for the side, where it isn't the game's own (NULL:
// Light or Dark, its own): Grey, or a Force Master.
const char *UI_VRForce_SideText( void ) {
	if ( UI_VRForce_Locked() ) {
		return "Force Master";
	}
	return ( uiForceSide == FORCE_GREYSIDE ) ? "Grey" : NULL;
}

// What the menu says under the side picker, with both sides' rows (ui_vrfNote).
static const char *UI_VRForce_Note( void ) {
	if ( UI_VRForce_Locked() ) {
		return "A Force Master: every power at level 3, nothing to spend.";
	}
	if ( !( UI_VRForce_May() & BG_FORCE_GREY ) ) {
		return ( UI_VRForce_Counted() == FORCE_DARKSIDE ) ?
			"This character can't be Grey here: it counts as Dark. Pick Light or Dark to set its powers." :
			"This character can't be Grey here: it counts as Light. Pick Light or Dark to set its powers.";
	}
	return "Grey: powers from both sides, bought with the same points.";
}

// An item's place in the menu, x (and its screen place with it).
static void UI_VRForce_Place( itemDef_t *it, float x ) {
	if ( it->window.rectClient.x != x ) {
		it->window.rectClient.x = x;
		Item_UpdatePosition( it );
	}
}

static void UI_VRForce_Show( menuDef_t *menu, const char *name, qboolean show ) {
	int i;

	Menu_ShowItemByName( menu, name, show );
	if ( show ) {
		return;
	}
	for ( i = 0; i < menu->itemCount; i++ ) {	// (gone: nothing's left focused on it)
		itemDef_t *it = menu->items[i];
		if ( ( ( it->window.name && !Q_stricmp( it->window.name, name ) ) || ( it->window.group && !Q_stricmp( it->window.group, name ) ) ) &&
			( it->window.flags & WINDOW_HASFOCUS ) ) {
			it->window.flags &= ~WINDOW_HASFOCUS;
		}
	}
}

// The menus let go of (ui_main.c's UI_MenuReset - before they're loaded
// again, and as the module stops): their items aren't the ones noted. (Menus
// loaded on top of them - the main menus over the in-game ones, as a game
// ends - leave theirs: noted still, where they are now.)
void UI_VRForce_MenusReset( void ) {
	vrfNumRows = 0;
}

// Each frame the game's Force menu is up (its side picker drawn, ui_main.c):
// the sides' rows shown as the setup has them, and laid out - with both, the
// dark side's on the left under its heading, the note over that, its
// templates gone; else as the game has it. side: the setup's, as the picker
// has it (its team's, on Force-aligned teams). qtrue: both shown here.
qboolean UI_VRForce_Layout( menuDef_t *menu, int side ) {
	const qboolean both = UI_VRForce_Both(), master = UI_VRForce_Locked();
	const qboolean light = (qboolean)( both || ( master ? UI_VRForce_TeamSide() == FORCE_LIGHTSIDE : side == FORCE_LIGHTSIDE ) );
	int i, k;

	if ( !menu ) {
		return qfalse;
	}
	// the dark side's rows: noted where the game's file has them, the first time
	for ( i = 0; i < menu->itemCount; i++ ) {
		itemDef_t *it = menu->items[i];
		if ( !it->window.group || ( Q_stricmp( it->window.group, "darkpowers" ) && Q_stricmp( it->window.group, "darkpowers_team" ) ) ||
			( ( it->window.flags & WINDOW_DECORATION ) && !it->text ) ) {
			continue;	// (not a row: the side picker's backdrop)
		}
		for ( k = 0; k < vrfNumRows && vrfRows[k].item != it; k++ ) {
		}
		if ( k == vrfNumRows && vrfNumRows < VRF_SHIFTED ) {
			vrfRows[vrfNumRows].item = it;
			vrfRows[vrfNumRows].x = it->window.rectClient.x;
			vrfNumRows++;
		}
		if ( k < vrfNumRows ) {
			UI_VRForce_Place( it, both ? vrfRows[k].x - VRF_SHIFT : vrfRows[k].x );
		}
	}
	Menu_ShowItemByName( menu, "lightpowers", light );
	Menu_ShowItemByName( menu, "lightpowers_team", light );
	Menu_ShowItemByName( menu, "darkpowers", (qboolean)( both || !light ) );
	Menu_ShowItemByName( menu, "darkpowers_team", (qboolean)( both || !light ) );
	if ( both ) {	// (no side's colour behind the picker)
		Menu_ShowItemByName( menu, "forcedivlight", qfalse );
		Menu_ShowItemByName( menu, "forcedivdark", qfalse );
	}
	// the templates (Light and Dark ones) while it isn't Grey, nor a Force Master's; the heading and the note while both show
	UI_VRForce_Show( menu, "fcflist", (qboolean)( !both && !master ) );
	UI_VRForce_Show( menu, "namefield", (qboolean)( !both && !master ) );
	if ( both || master ) {
		UI_VRForce_Show( menu, "playerforcetemplatesave", qfalse );	// (Save, and what shows as it's pointed at)
	} else {
		UI_VRForce_Show( menu, "templatesave", qtrue );
	}
	UI_VRForce_Show( menu, "vrf_both", both );	// (the dark side's heading)
	UI_VRForce_Show( menu, "vrf_note", (qboolean)( both || master ) );
	if ( both || master ) {
		char note[MAX_CVAR_VALUE_STRING];
		trap->Cvar_VariableStringBuffer( "ui_vrfNote", note, sizeof( note ) );
		if ( strcmp( note, UI_VRForce_Note() ) ) {
			trap->Cvar_Set( "ui_vrfNote", UI_VRForce_Note() );
		}
	}
	return both;
}
