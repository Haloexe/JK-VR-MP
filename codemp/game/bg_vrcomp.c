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
Each part letters, digits, _ and -; short enough for every name the game
makes of it (each held in MAX_QPATH): the skin's own path,
"models/players/<model>/model_<skin>.skin" or a custom character's
"models/players/<model>/|<head|torso|lower>" (a player's own custom
character's limit); the name the server's own NPC is made with,
"models/players/<model>/model.glm*<skin>" - a custom character too long for
that is made with the default skin (and told beside its owner's
configstring, CS_VR_COMPANIONS, as this build's games before CS_VR_COMPLOOKS
take it); and "models/players/<model>/model_default.skin". No game is ever
sent a look's model: every player's game is sent a companion as its type
comes, and this build's are told its look beside (CS_VR_COMPLOOKS:
"<entity>\<look>" each - BG_VRCompLooksPack, BG_VRCompLooksRead).

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

// An NPC file's text as the server reads one (NPC_LoadParms): a UTF-8
// file's mark gone (Notepad's - the game read it as its first name's),
// COM_Compress'd, a second '\0' after it (BG_VRBlocksInStep's). text[len]
// is '\0', with room after it for one more. Its length now.
int BG_VRNPCFileText( char *text, int len ) {
	if ( len >= 3 && !memcmp( text, "\xEF\xBB\xBF", 3 ) ) {
		memmove( text, text + 3, len - 3 + 1 );	// (its '\0' too)
	}
	len = COM_Compress( text );
	text[len + 1] = '\0';
	return len;
}

// How much of a file of NPC types (BG_VRNPCFileText's) the game's walk
// through them all goes through in step. NPC_ParseParms and the rest look
// for a type from the first file read on - a name, then SkipBracedSection
// (its block, or a word for one) - so one file's "" for a name, name with
// nothing after it, { never closed, } too many or quote never closed threw
// that walk out for every file after it: the game's own, after a pack's.
// Its length, if the walk ends in step; else where the entry it loses its
// way in begins (at, if not NULL: that entry's name - "/*", a comment it'd
// take into the next file). buf[len] and buf[len + 1] are '\0' (a quote
// never closed: COM_ParseExt goes past the first, and finds the second).
int BG_VRBlocksInStep( const char *buf, int len, char *at, int atSize ) {
	const char *p = buf, *tok;
	int good = 0;

	if ( at && atSize > 0 ) {
		at[0] = '\0';
	}
	COM_BeginParseSession( "NPC file" );
	while ( 1 ) {
		tok = COM_ParseExt( &p, qtrue );
		if ( !p ) {
			if ( strstr( buf + good, "/*" ) ) {
				if ( at ) {
					Q_strncpyz( at, "/*", atSize );
				}
				return good;	// (one COM_Compress left - a quote begun mid-word: it took the rest for a string - the walk takes on to a */ in a file after it)
			}
			return len;	// (its end: all of it in step)
		}
		if ( at ) {
			Q_strncpyz( at, tok, atSize );
		}
		if ( !tok[0] ) {
			return good;	// ("" for a name: the walk ends there)
		}
		SkipBracedSection( &p, 0 );
		if ( !p || p > buf + len ) {
			return good;	// (the walk on to its end - nothing after a name: the next file's first taken for its; an { never closed, a } too many; a quote never closed)
		}
		good = (int)( p - buf );
	}
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
// a custom character's three parts - each name the game makes of it short
// enough.
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
		return (qboolean)( 15 + n + 19 < MAX_QPATH );	// (models/players/ + model + /model_default.skin: its configstring's a character shorter)
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
	if ( parts == 1 ) {
		return (qboolean)( 15 + (int)strlen( look ) + 6 + 5 < MAX_QPATH );	// (the '/' between becomes "/model_", and ".skin" after)
	}
	// a custom character's: the '/' between becomes "/|"; and where the name
	// the server's NPC is made with can't carry the three (BG_VRCompSkinInCs:
	// the '/' becomes "/model.glm*"), the model's default skin as above
	return (qboolean)( 15 + (int)strlen( look ) + 1 < MAX_QPATH && ( 15 + (int)strlen( look ) + 10 < MAX_QPATH || 15 + n + 19 < MAX_QPATH ) );
}

// Whether a model's NPC name - its configstring, as an NPC type's is - can
// carry its skin after it ("models/players/<model>/model.glm*<skin>" in
// MAX_QPATH, as SetupGameGhoul2Model and every player's game hold it):
// else it carries the default skin.
qboolean BG_VRCompSkinInCs( const char *model, const char *skin ) {
	return (qboolean)( 15 + (int)strlen( model ) + 11 + (int)strlen( skin[0] ? skin : "default" ) < MAX_QPATH );
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

// The companions in looks of their own (count: an entity and a look each)
// written into their configstrings (CS_VR_COMPLOOKS, RP_COMP_LOOKS_CS of
// them): "<entity>\<look>" each, '\' between, as many in each as fit in a
// configstring a game reads (MAX_STRING_CHARS), the next in the next. All of
// them fit, as many as there can be (RP_COMP_OUT_MAX a player, the longest
// look); qfalse: one didn't (it's left out - its game draws it as its type).
qboolean BG_VRCompLooksPack( const int *ents, const char (*looks)[MAX_QPATH], int count, char (*cs)[MAX_STRING_CHARS] ) {
	qboolean all = qtrue;
	int i, n = 0;

	for ( i = 0; i < RP_COMP_LOOKS_CS; i++ ) {
		cs[i][0] = '\0';
	}
	for ( i = 0; i < count; i++ ) {
		char entry[MAX_QPATH + 16];
		Com_sprintf( entry, sizeof( entry ), "%i\\%s", ents[i], looks[i] );
		while ( n < RP_COMP_LOOKS_CS && strlen( cs[n] ) + ( cs[n][0] ? 1 : 0 ) + strlen( entry ) >= MAX_STRING_CHARS ) {
			n++;	// (full: the next)
		}
		if ( n >= RP_COMP_LOOKS_CS ) {
			all = qfalse;
			break;
		}
		if ( cs[n][0] ) {
			Q_strcat( cs[n], MAX_STRING_CHARS, "\\" );
		}
		Q_strcat( cs[n], MAX_STRING_CHARS, entry );
	}
	return all;
}

// One of the companions' looks' configstrings read (BG_VRCompLooksPack's):
// its entities and looks into ents and looks from n on, up to most - an
// entity not a companion's, or a look the game doesn't take, left out.
// Returns how many there are then.
int BG_VRCompLooksRead( const char *cs, int *ents, char (*looks)[MAX_QPATH], int n, int most ) {
	char buf[MAX_STRING_CHARS], *p = buf;

	Q_strncpyz( buf, cs ? cs : "", sizeof( buf ) );
	while ( *p && n < most ) {
		char *field[2];
		int i, ent;
		for ( i = 0; i < 2; i++ ) {
			char *end = strchr( p, '\\' );
			field[i] = p;
			if ( end ) {
				*end = '\0';
				p = end + 1;
			} else {
				p += strlen( p );
			}
		}
		ent = atoi( field[0] );
		if ( ent >= MAX_CLIENTS && ent < ENTITYNUM_MAX_NORMAL && field[1][0] && BG_VRCompLookOk( field[1] ) ) {
			ents[n] = ent;
			Q_strncpyz( looks[n], field[1], MAX_QPATH );
			n++;
		}
	}
	return n;
}
