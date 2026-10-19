/*
===========================================================================
OpenJK VR (multiplayer) - roleplay's companions, what the server's game
(g_vrcomp.c, g_vrrp.c), the players' (cg_vrrp.c) and their menus
(ui_vrcomp.c) agree on: which of the game's NPC types a companion may fight
as, and how a companion's look is written.

FIGHTS AS: an NPC type whose class is one of people - Jedi, Reborn, troopers,
officers, mercenaries, the galaxy's peoples - every one of them on the
humanoid skeleton every look shares, so any look fits whatever it fights as.
Droids (a protocol droid's on that skeleton, but nothing ever fights it),
beasts, monsters and vehicles move on bodies of their own, and are left out.
Whether a model's on that skeleton, its model.glm says (BG_VRCompGlmHumanoid:
the game, the players' and the menus' each read its first bytes).

A LOOK: "" - as its type comes - or a player model, with a skin or a custom
character's three parts:
  model               (its default skin)
  model/skin          models/players/model/model_skin.skin
  model/head|torso|lower   a custom character's: head_a1|torso_b1|lower_e1
Each part letters, digits, _ and -; short enough that the model's
configstring - "models/players/<model>/model.glm*<skin>" - fits.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "qcommon/q_shared.h"
#include "bg_public.h"

// the classes a companion may fight as (as the NPC files write them), and
// what the Companions page calls each
static const struct { const char *name, *word; } bgVRCompClasses[] = {
	{ "CLASS_BARTENDER", "Bartender" }, { "CLASS_BESPIN_COP", "Bespin cop" }, { "CLASS_BOBAFETT", "Bounty hunter" },
	{ "CLASS_COMMANDO", "Commando" }, { "CLASS_DESANN", "Desann" }, { "CLASS_GALAK", "Galak" }, { "CLASS_GRAN", "Gran" },
	{ "CLASS_IMPERIAL", "Imperial" }, { "CLASS_IMPWORKER", "Imperial worker" }, { "CLASS_JAN", "Jan Ors" },
	{ "CLASS_JAWA", "Jawa" }, { "CLASS_JEDI", "Jedi" }, { "CLASS_KYLE", "Kyle Katarn" }, { "CLASS_LANDO", "Lando" },
	{ "CLASS_LUKE", "Luke" }, { "CLASS_MONMOTHMA", "Mon Mothma" }, { "CLASS_MORGANKATARN", "Morgan Katarn" },
	{ "CLASS_MURJJ", "Murjj" }, { "CLASS_PRISONER", "Prisoner" },
	{ "CLASS_REBEL", "Rebel" }, { "CLASS_REBORN", "Reborn" }, { "CLASS_REELO", "Reelo" }, { "CLASS_RODIAN", "Rodian" },
	{ "CLASS_SHADOWTROOPER", "Shadowtrooper" }, { "CLASS_STORMTROOPER", "Stormtrooper" },
	{ "CLASS_SWAMPTROOPER", "Swamptrooper" }, { "CLASS_TAVION", "Tavion" }, { "CLASS_TRANDOSHAN", "Trandoshan" },
	{ "CLASS_UGNAUGHT", "Ugnaught" }, { "CLASS_WEEQUAY", "Weequay" },
};

// What the Companions page calls a class a companion may fight as (NULL:
// it may not).
const char *BG_VRCompClassWord( const char *className ) {
	int i;

	for ( i = 0; className && i < (int)ARRAY_LEN( bgVRCompClasses ); i++ ) {
		if ( !Q_stricmp( className, bgVRCompClasses[i].name ) ) {
			return bgVRCompClasses[i].word;
		}
	}
	return NULL;
}

qboolean BG_VRCompClassOk( const char *className ) {
	return (qboolean)( BG_VRCompClassWord( className ) != NULL );
}

static qboolean BG_VRCompChar( char c ) {
	return (qboolean)( ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) || c == '_' || c == '-' );
}

// An NPC type's name as the files write one: letters, digits, _ - . (it's
// sent and kept in quotes, and spawned by it whole).
qboolean BG_VRCompTypeOk( const char *type ) {
	const char *p;

	if ( !type || !type[0] || (int)strlen( type ) >= RP_COMP_TYPE_SIZE ) {
		return qfalse;
	}
	for ( p = type; *p; p++ ) {
		if ( !BG_VRCompChar( *p ) && *p != '.' ) {
			return qfalse;
		}
	}
	return qtrue;
}

// how many characters from s are a part's (letters, digits, _ -)
static int BG_VRCompPart( const char *s ) {
	int n = 0;

	while ( s[n] && BG_VRCompChar( s[n] ) ) {
		n++;
	}
	return n;
}

// A look as written above: "" (as its type comes), or a model, a skin, or
// a custom character's three parts - and short enough for its configstring.
qboolean BG_VRCompLookOk( const char *look ) {
	const char *p;
	int n, parts = 0;

	if ( !look ) {
		return qfalse;
	}
	if ( !look[0] ) {
		return qtrue;
	}
	n = BG_VRCompPart( look );
	if ( !n ) {
		return qfalse;
	}
	p = look + n;
	if ( !*p ) {
		return (qboolean)( 15 + n + 11 + 7 < MAX_QPATH );	// (models/players/ + model + /model.glm* + default)
	}
	if ( *p != '/' ) {
		return qfalse;
	}
	p++;
	while ( 1 ) {
		const int k = BG_VRCompPart( p );
		if ( !k ) {
			return qfalse;
		}
		p += k;
		parts++;
		if ( !*p ) {
			break;
		}
		if ( *p != '|' || parts == 3 ) {
			return qfalse;
		}
		p++;
	}
	if ( parts == 2 ) {
		return qfalse;	// (one skin, or a custom character's three)
	}
	return (qboolean)( 15 + (int)strlen( look ) + 10 < MAX_QPATH );	// (the '/' between becomes "/model.glm*")
}

// Whether a model.glm's first bytes (BG_VRCOMP_GLM_HEAD of them: its ident,
// version, name and skeleton's name) say it's on the humanoid skeleton.
qboolean BG_VRCompGlmHumanoid( const byte *head, int len ) {
	char gla[MAX_QPATH];

	if ( !head || len < BG_VRCOMP_GLM_HEAD || memcmp( head, "2LGM", 4 ) ) {
		return qfalse;
	}
	Q_strncpyz( gla, (const char *)head + 8 + MAX_QPATH, sizeof( gla ) );
	return (qboolean)( strstr( gla, "players/_humanoid/" ) != NULL );
}

// A look's model and skin ("default" where it gives none; "" for "").
void BG_VRCompLookSplit( const char *look, char *model, int modelSize, char *skin, int skinSize ) {
	const char *slash = look ? strchr( look, '/' ) : NULL;

	if ( !look || !look[0] ) {
		model[0] = skin[0] = '\0';
		return;
	}
	if ( !slash ) {
		Q_strncpyz( model, look, modelSize );
		Q_strncpyz( skin, "default", skinSize );
		return;
	}
	Q_strncpyz( model, look, Com_Clampi( 1, modelSize, (int)( slash - look ) + 1 ) );
	Q_strncpyz( skin, slash + 1, skinSize );
}
