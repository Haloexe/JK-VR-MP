/*
===========================================================================
OpenJK VR (multiplayer) - roleplay mode, the player's side (the server's
is game/g_vrrp.c): logging in, the account and its characters for the
Roleplay menu, speech bubbles, and character cards.

LOGGING IN. /new <account> <password> makes an account on the server and
/login <account> <password> logs in to one; /changepassword <old> <new>
and /logout. The password is made into a key here (bg_vrcrypt.c: slowly,
over the server's salt and the account name) and never leaves this PC: a
new account sends the server its key, and a login only proves the key
against the one-time challenge the server sent. Remember me
(cg_rpRemember) keeps the key, never the password, in rp/remember.txt
with the server's address and salt, and logs in with it there once a
visit - again if the server didn't answer, three times at most - roleplay
on or off (an admin's cheats don't wait for it); /logout forgets it, and
so does the server saying it's wrong.

COMMANDS. A dedicated server takes only so many commands from a player at
once, then one more a period, and drops the rest (its flood protection:
sv_floodProtect, in the serverinfo; a listen server has none). What this
game asks by itself - cards, as you look at someone and for the menu's
People page (several in one request there), and a login Remember me kept -
waits till the server's allowance is whole again after the commands it
sent, a period for each (CG_RP_Room), leaving room in it for what the
player sends: chat, the menus (a server that takes a few at once has it).

ADMINS. An account the server's owner made an admin (rcon rp admin) can
use every cheat there, cheats on or off: the server says so (rpadmin),
and the Roleplay menu shows its Admin page - spawn an NPC or a vehicle
(tagged as the admin's: vra_<account>; an NPC on the side its Faction
picks - with the Heroes, the Villains, neutral, a creature - where the
server takes one, as rpadmin says: else its file's), kill those or every NPC, god
mode, no-clip, no target, give all, every Force power (give forcepowers).
They're the game's own cheat commands, which the server lets an admin use.
On the People page an admin makes the character of whoever's picked a Grey
Jedi or a Force Master, or neither (rp force: the server's answer to their
card says which it is).
Its Invasion part starts and stops invasions - waves of NPCs (rp
invasion: g_vrinvade.c), on their file's side or the one its Faction
picks - and shows how they go, as the server says
(rpinv, to an admin's game: and so that the server has them). Where the
server says it takes them (RPA_WARS), an invasion's lineup is of several
NPC types, mixed or each so many a wave, and wars (rp war): each side's
lineup fighting the others' where the admin stands.

THE MENUS read where things stand in ui_rp* cvars, and ask for things
with "rpui <what>" (below).

DOWN, NOT DEAD: being down, getting someone up, giving up - cg_vrdown.c.

COMPANIONS: yours for the Companions page, whose others are, stand-ins
for models this PC hasn't got - cg_vrcomp.c.

A CHARACTER'S FORCE SETUP. The server keeps one with each character, and
says which this game is to have (rpforce <number> <setup | ->): the
character's you play - it goes in forcepowers, what the Force menu sets, with
your own put by in cg_rpOwnForce - or "-": yours back. Your userinfo tells
the server which it has (rpf: "<number> <yours>", or "": yours), so what you
set in the Force menu while you play a character - forcepowers, in your
userinfo too - it takes as that character's. (Not a command: a server may
drop one, sent hard on others.) Yours comes back by itself as this game
starts and stops (on another server, say, or after playing one to the end).
It also says what the character may be (ui_rpForceMay, BG_FORCE_*: a Grey
Jedi - the Force menu offers Grey - or a Force Master: ui_vrforce.c), under
the same number again as only that changes.

SPEECH BUBBLES. With roleplay on, what someone near you says, and their
/me, shows over their head for 6 seconds (cg_rpBubbles).

CARDS. Look at someone playing a character, within 4 m, for half a
second with your own weapon away (the saber off, or fists), and their
card shows: name, faction, description (cg_rpCards). Their bio: Read
their bio on the wheel's Roleplay ring, or /bio. A player's companion's
card is its name (over its head too), whose it is and its side: known
here, nothing asked of the server.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "cg_local.h"
#include "ui/menudef.h"
#include "game/bg_vrcrypt.h"

qboolean CG_WorldCoordToScreenCoordFloat( vec3_t worldCoord, float *x, float *y );	// (cg_draw.c)

#define RP_PROTOCOL			8		// (as the server's, g_vrrp.c: 4 takes rpadmin, 5 companions, 6 a character's Force setup, 7 Grey and Force Masters, 8 rpinv)
#define RP_INV_LINES		3		// invasions the Admin page shows how they go (one a side: g_vrinvade.c)
#define RP_INV_WAVES_MOST	50		// (an invasion's waves at most, a wave's and those up at once, s between waves: the server's, g_vrinvade.c)
#define RP_INV_MOST			32
#define RP_INV_WAIT_MOST	600

// what the server says of an admin (rpadmin; as g_vrrp.c's)
#define RPA_ADMIN			1
#define RPA_GOD				2
#define RPA_NOCLIP			4
#define RPA_NOTARGET		8
#define RPA_SIDES			16		// (the server takes a side picked for an NPC: the Admin page's Faction)
#define RPA_WARS			32		// (the server takes lineups - several NPC types an invasion, how many a wave of each - and wars)
#define RP_LINEUP_KINDS		4		// a lineup's NPC types at most, how many a wave of one (as the server's)
#define RP_LINEUP_EACH_MOST	32
#define RP_LINEUP_SIZE		( RP_LINEUP_KINDS * ( RP_NPC_NAME_MAX + 4 ) + 8 )	// (room for one too long: seen whole, and refused)
#define RP_ADMIN_TAG		"vra_"	// an admin's NPCs, spawned from the Admin page: vra_<account>
#define RP_NPC_NAME_MAX		63
#define RP_CHARS_MAX		10		// (as many as a server keeps at most)
#define RP_ACCOUNT_MIN		3
#define RP_ACCOUNT_MAX		20
#define RP_PASSWORD_MIN		4		// a new password takes at least this many characters
#define RP_NAME_BUF			64
#define RP_DESC_MAX			60
#define RP_BIO_MAX			250

#define RP_SAVED_FILE		"rp/remember.txt"
#define RP_SAVED_MAX		32		// servers remembered
#define RP_SAVED_TEXT		16384

#define RP_HELLO_FIRST		500		// after the first snapshot, hello...
#define RP_HELLO_AGAIN		3000	// ...again after this long without an answer...
#define RP_HELLO_TRIES		5		// ...this many times at most
#define RP_ASK_WAIT			15000	// a request with no answer this long is given up...
#define RP_AUTO_WAIT		5000	// ...a login Remember me kept, this long - and tried again...
#define RP_AUTO_TRIES		3		// ...this many times in all, at most
#define RP_FLOOD_SPARE		100		// (ms) more than the server's flood protection's period: commands this far apart reach it so
#define RP_FLOOD_MAX		60000	// (ms) the period, at the most (as the server's engine takes it)
#define RP_FLOOD_OWED		5		// periods' worth this game reckons it can owe the server at most (as its engine starts: sv_floodProtectBurst)

#define RP_BUBBLE_TIME		6000
#define RP_BUBBLE_FADE		1000	// (the end of it)
#define RP_BUBBLE_RANGE		850.0f	// further off (25 m), not drawn
#define RP_BUBBLE_ABOVE		16.0f	// over the top of the speaker's box
#define RP_BUBBLE_WIDTH		420.0f	// its lines, at scale 1
#define RP_BUBBLE_LINES		4

#define RP_CARD_RANGE		136.0f	// 4 m
#define RP_CARD_DWELL		500		// looking this long shows a card...
#define RP_CARD_GRACE		300		// ...and looking away this long hides it
#define RP_CARD_KEEP		10000	// a card's asked for again after this
#define RP_CARD_ASK_GAP		2000	// (and not more often)
#define RP_CARD_WIDTH		520.0f	// its description's lines, at scale 1
#define RP_BIO_WIDTH		640.0f
#define RP_BIO_TIME			20000
#define RP_PEOPLE_TIME		15000	// the menu's People page: everyone's card asked for this long...
#define RP_PEOPLE_ONE		170		// ...one at a time, this far apart (the server answers 10 a second) - or, with flood protection...
#define RP_PEOPLE_ASK		8		// ...this many in one request (of its 10: some left for the card of whoever you look at)...
#define RP_PEOPLE_GAP		1200	// ...this far apart at least (more than its second)
#define RP_NOTICE_WIDTH		900.0f
#define RP_NOTICE_TIME		5000

#define RP_TEXT_LINES		10
#define RP_LINE_MAX			128
#define RP_FONT				FONT_MEDIUM

static const char *rpFactions[RPF_NUM] = { "Heroes", "Villains", "Neutral" };	// (RPF_*: bg_public.h)

enum { RPCHAT_NONE, RPCHAT_TALK, RPCHAT_ACTION, RPCHAT_OOC };

// what's asked of the server with a key, until it answers
enum { RPASK_NONE, RPASK_NEW, RPASK_LOGIN, RPASK_AUTO, RPASK_PASSWORD };

typedef struct {
	int		faction;
	char	name[RP_NAME_BUF];
	char	desc[RP_DESC_MAX + 1];
	char	bio[RP_BIO_MAX + 1];
} rpChar_t;

// Text laid out in lines (at scale 1).
typedef struct {
	int		numLines;
	char	line[RP_TEXT_LINES][RP_LINE_MAX];
	float	width;				// the widest
} rpText_t;

typedef struct {
	int			time;			// said then (0: nothing)
	int			kind;			// RPCHAT_TALK, RPCHAT_ACTION
	rpText_t	text;
} rpBubble_t;

typedef struct {
	int			time;			// the server told then (0: not yet)
	int			asked;
	char		name[MAX_QPATH];	// the name they went by then (another: another character)
	int			faction;		// -1: not playing a character
	rpText_t	desc;
} rpCard_t;

static struct {
	// the server
	qboolean	server;			// it has roleplay (serverinfo vrRP)...
	qboolean	working;		// ...that works (a lone "0": its rp/server.txt can't be read)...
	qboolean	on;				// ...and it's on
	int			compsAllowed;	// companions a player may have out there (vrRP; 0: none, or a server before them)
	int			floodPeriod;	// its flood protection's period (ms; 0: none - off, or this PC's own server)
	int			floodWhole;		// when its allowance is whole again, as far as this game's commands go (CG_RP_Send)
	char		address[MAX_QPATH];
	int			firstSnap;
	int			helloTime, helloTries;
	qboolean	hello;			// it answered: its salt, a challenge
	char		salt[VRRP_SALT_HEX + 1];
	char		challenge[VRRP_CHALLENGE_HEX + 1];
	qboolean	challengeUsed;	// answered already: the next waits for a new one

	// the account
	qboolean	known;			// the server said whether we're logged in
	char		account[RP_ACCOUNT_MAX + 1];	// "": not logged in
	int			active;			// the character played (-1: none)
	int			numChars, most;
	rpChar_t	chars[RP_CHARS_MAX];
	qboolean	autoTried;		// logged in with what this PC kept (once a visit; again if unanswered)...
	int			autoTries;		// ...this many times
	int			admin;			// what the server says of us (rpadmin: RPA_*; 0: not an admin)

	struct {
		int			kind;		// RPASK_*
		qboolean	sent;		// (no: it waits for a challenge)
		int			time;
		char		account[RP_ACCOUNT_MAX + 1];
		byte		key[VRRP_KEY_LEN];		// logs in with this / the new password's
		byte		oldKey[VRRP_KEY_LEN];	// the password now (RPASK_PASSWORD)
		qboolean	remember;
	} ask;

	// who you look at
	int			target, since, seen;
	// a bio read
	int			bioWho, bioTime;
	rpText_t	bio;
	int			menuBio;		// the bio the menu asked for (-1: none)
	// the menu's People page: everyone's card
	int			peopleUntil, peopleNext, peopleCursor;
	// a line for the player
	int			noticeTime;
	rpText_t	notice;
} cgrp;

static rpBubble_t	rpBubbles[MAX_CLIENTS];
static rpCard_t		rpCards[MAX_CLIENTS];
static rpCard_t		rpCompCard;		// a companion's (made here: CG_RP_CompCard)
static char			rpCompCardWhose[MAX_QPATH * 2];

/*
================================================================================
Small things
================================================================================
*/

// A line for the player, a moment on the screen (and in the console: print).
static void CG_RP_Show( const char *text, qboolean print );

// What this game reckons it owes the server's allowance: no more than it can
// be owed (its engine drops what's beyond it; and a period cut since owes less).
static void CG_RP_FloodCap( void ) {
	const int most = RP_FLOOD_OWED * ( cgrp.floodPeriod + RP_FLOOD_SPARE );

	if ( cgrp.floodWhole - cg.time > most ) {
		cgrp.floodWhole = cg.time + most;
	}
}

// A command to the server from roleplay here: sent, and the server's
// allowance reckoned with it - a period (and a bit) more before it's whole
// again (CG_RP_Room).
void CG_RP_Send( const char *cmd ) {
	trap->SendClientCommand( cmd );
	if ( cgrp.floodPeriod ) {
		cgrp.floodWhole = ( ( cgrp.floodWhole - cg.time > 0 ) ? cgrp.floodWhole : cg.time ) + cgrp.floodPeriod + RP_FLOOD_SPARE;
		CG_RP_FloodCap();
	}
}

// Whether what this game asks by itself - a card, a login Remember me kept -
// can go now: once the server's flood protection has its allowance whole
// again after the commands this game sent, a period (and a bit) for each, so
// it leaves room there for what the player sends - chat, the menus - when
// the server takes a few at once (one at a time, the old way, it can't).
static qboolean CG_RP_Room( void ) {
	return (qboolean)( !cgrp.floodPeriod || cgrp.floodWhole - cg.time <= 0 );
}

// Text as a bubble or a card shows it: colours, quotes and control characters out.
static void CG_RP_Plain( const char *in, char *out, int size ) {
	int n = 0;

	for ( ; in && *in && n < size - 1; in++ ) {
		if ( Q_IsColorString( in ) ) {
			in++;
			continue;
		}
		if ( (unsigned char)*in < 32 ) {
			out[n++] = ' ';
		} else if ( *in != '"' ) {
			out[n++] = *in;
		}
	}
	out[n] = '\0';
}

// An account name as the server takes them (lower case); qfalse: not one.
static qboolean CG_RP_AccountName( const char *in, char *out, int size ) {
	const int len = (int)strlen( in );
	int i;

	if ( len < RP_ACCOUNT_MIN || len > RP_ACCOUNT_MAX || len >= size ) {
		return qfalse;
	}
	for ( i = 0; i <= len; i++ ) {
		const char c = ( in[i] >= 'A' && in[i] <= 'Z' ) ? in[i] - 'A' + 'a' : in[i];
		if ( c && !( ( c >= 'a' && c <= 'z' ) || ( c >= '0' && c <= '9' ) || c == '_' || c == '-' ) ) {
			return qfalse;
		}
		out[i] = c;
	}
	return qtrue;
}

// The arguments from n on, as typed (a password can have spaces).
// A password's copy wiped (so the compiler can't leave that out, as it may
// a memset of what's not read again).
static void CG_RP_Wipe( char *p, int size ) {
	volatile char *v = p;

	while ( size-- > 0 ) {
		*v++ = '\0';
	}
}

static void CG_RP_ArgsFrom( int n, char *out, int size ) {
	char arg[MAX_STRING_CHARS];
	int i;

	out[0] = '\0';
	for ( i = n; i < trap->Cmd_Argc(); i++ ) {
		trap->Cmd_Argv( i, arg, sizeof( arg ) );
		if ( i > n ) {
			Q_strcat( out, size, " " );
		}
		Q_strcat( out, size, arg );
	}
	CG_RP_Wipe( arg, sizeof( arg ) );
}

/*
================================================================================
Text in lines
================================================================================
*/

// Lays text out in lines no wider than width at scale 1, breaking at
// spaces where it can, at most maxLines of them; what doesn't fit ends
// the last one in "...".
static void CG_RP_Layout( const char *text, float width, int maxLines, rpText_t *out ) {
	const char *p = text;
	int i;

	memset( out, 0, sizeof( *out ) );
	maxLines = Com_Clampi( 1, RP_TEXT_LINES, maxLines );
	while ( *p == ' ' ) {
		p++;
	}
	while ( *p && out->numLines < maxLines ) {
		char *line = out->line[out->numLines];
		int len = 0;

		while ( *p ) {
			char test[RP_LINE_MAX * 2];
			int wl = 0;
			while ( p[wl] && p[wl] != ' ' ) {
				wl++;
			}
			if ( len + 1 + wl < RP_LINE_MAX ) {
				Com_sprintf( test, sizeof( test ), "%s%s%.*s", line, len ? " " : "", wl, p );
				if ( CG_Text_Width( test, 1.0f, RP_FONT ) <= width ) {
					Q_strncpyz( line, test, RP_LINE_MAX );
					len = (int)strlen( line );
					p += wl;
					while ( *p == ' ' ) {
						p++;
					}
					continue;
				}
			}
			if ( !len ) {
				// a word wider than a line: as much of it as fits (one letter at least)
				int k = 1;
				while ( k < wl && k < RP_LINE_MAX - 1 ) {
					Com_sprintf( test, sizeof( test ), "%.*s", k + 1, p );
					if ( CG_Text_Width( test, 1.0f, RP_FONT ) > width ) {
						break;
					}
					k++;
				}
				Com_sprintf( line, RP_LINE_MAX, "%.*s", k, p );
				p += k;
				while ( *p == ' ' ) {
					p++;
				}
			}
			break;
		}
		out->numLines++;
	}
	if ( *p && out->numLines ) {
		// more than fits: the last line ends in "...", after a whole word if
		// one will do (letters off the end if not)
		char *last = out->line[out->numLines - 1];
		const int len = (int)strlen( last );
		int n, cut = -1;
		for ( n = len; n > 0 && cut < 0; n-- ) {
			if ( n == len || last[n] == ' ' ) {
				char test[RP_LINE_MAX + 8];
				Com_sprintf( test, sizeof( test ), "%.*s...", n, last );
				if ( n + 3 < RP_LINE_MAX && CG_Text_Width( test, 1.0f, RP_FONT ) <= width ) {
					cut = n;
				}
			}
		}
		for ( n = len; cut < 0 && n > 0; n-- ) {
			char test[RP_LINE_MAX + 8];
			Com_sprintf( test, sizeof( test ), "%.*s...", n, last );
			if ( n + 3 < RP_LINE_MAX && CG_Text_Width( test, 1.0f, RP_FONT ) <= width ) {
				cut = n;
			}
		}
		n = ( cut < 0 ) ? 0 : cut;
		while ( n > 0 && last[n - 1] == ' ' ) {
			n--;
		}
		last[n] = '\0';
		Q_strcat( last, RP_LINE_MAX, "..." );
	}
	for ( i = 0; i < out->numLines; i++ ) {
		const float w = (float)CG_Text_Width( out->line[i], 1.0f, RP_FONT );
		if ( w > out->width ) {
			out->width = w;
		}
	}
}

/*
================================================================================
The menus' cvars
================================================================================
*/

// ui_rpServer: 0 no roleplay here, 1 off, 2 on, 3 not working (a lone "0": no logging in there)
static void CG_RP_UIServer( void ) {
	trap->Cvar_Set( "ui_rpServer", !cgrp.server ? "0" : !cgrp.working ? "3" : cgrp.on ? "2" : "1" );
}

static void CG_RP_UIChar( int i ) {
	const rpChar_t *ch = &cgrp.chars[i];
	const qboolean has = (qboolean)( i < cgrp.numChars && ch->name[0] );

	trap->Cvar_Set( va( "ui_rpName%i", i ), has ? ch->name : "" );
	trap->Cvar_Set( va( "ui_rpFaction%i", i ), has ? rpFactions[ch->faction] : "" );
	trap->Cvar_Set( va( "ui_rpDesc%i", i ), has ? ch->desc : "" );
	trap->Cvar_Set( va( "ui_rpBio%i", i ), has ? ch->bio : "" );
}

// The character played, by itself (what the menus show of it).
static void CG_RP_UIPlaying( void ) {
	const rpChar_t *ch = ( cgrp.active >= 0 && cgrp.active < cgrp.numChars ) ? &cgrp.chars[cgrp.active] : NULL;

	trap->Cvar_Set( "ui_rpPlaying", ch ? ch->name : "" );
	trap->Cvar_Set( "ui_rpPlayingFaction", ch ? rpFactions[ch->faction] : "" );
}

static void CG_RP_UIAccount( void ) {
	int i;

	trap->Cvar_Set( "ui_rpAccount", cgrp.account );
	trap->Cvar_Set( "ui_rpChars", va( "%i", cgrp.numChars ) );
	trap->Cvar_Set( "ui_rpMost", va( "%i", cgrp.most ) );
	trap->Cvar_Set( "ui_rpActive", va( "%i", cgrp.active ) );
	for ( i = 0; i < RP_CHARS_MAX; i++ ) {
		CG_RP_UIChar( i );
	}
	CG_RP_UIPlaying();
}

// What the Admin page shows: whether we're an admin here, and our cheats.
static void CG_RP_UIAdmin( void ) {
	trap->Cvar_Set( "ui_rpAdmin", ( cgrp.admin & RPA_ADMIN ) ? "1" : "0" );
	trap->Cvar_Set( "ui_rpAdminGod", ( cgrp.admin & RPA_GOD ) ? "1" : "0" );
	trap->Cvar_Set( "ui_rpAdminNoclip", ( cgrp.admin & RPA_NOCLIP ) ? "1" : "0" );
	trap->Cvar_Set( "ui_rpAdminNotarget", ( cgrp.admin & RPA_NOTARGET ) ? "1" : "0" );
	trap->Cvar_Set( "ui_rpAdminSides", ( cgrp.admin & RPA_SIDES ) ? "1" : "0" );	// (the Faction rows: the server takes them)
	trap->Cvar_Set( "ui_rpAdminWars", ( cgrp.admin & RPA_WARS ) ? "1" : "0" );	// (a lineup's rows, the Invasion part's War mode)
}

// The Admin page's Invasion part: how the invasions go (ui_rpInvLine<n>,
// ui_rpInvOn), and whether this server has them (ui_rpInvAble) - as the
// server says (rpinv); nothing yet - another server's, or no admin here.
static void CG_RP_UIInvade( int count, char lines[RP_INV_LINES][MAX_STRING_CHARS], qboolean able ) {
	int i;

	for ( i = 0; i < RP_INV_LINES; i++ ) {
		trap->Cvar_Set( va( "ui_rpInvLine%i", i ), ( i < count ) ? lines[i] : "" );
	}
	trap->Cvar_Set( "ui_rpInvOn", count > 0 ? "1" : "0" );
	trap->Cvar_Set( "ui_rpInvAble", able ? "1" : "0" );
}

static void CG_RP_UIInvadeClear( void ) {
	CG_RP_UIInvade( 0, NULL, qfalse );
}

/*
================================================================================
Logins this PC keeps: rp/remember.txt, a line a server -
"<address>" <salt> <account> <key>
================================================================================
*/

typedef struct {
	char	address[MAX_QPATH];
	char	salt[VRRP_SALT_HEX + 1];
	char	account[RP_ACCOUNT_MAX + 1];
	char	key[VRRP_KEY_LEN * 2 + 1];
} rpSaved_t;

static rpSaved_t rpSaved[RP_SAVED_MAX];

// The logins kept, into rpSaved[]: how many (-1: the file's there but too
// big to read - and so it's never written over).
static int CG_RP_ReadSaved( void ) {
	static char text[RP_SAVED_TEXT];
	fileHandle_t f;
	const char *p;
	int len, n = 0;

	memset( rpSaved, 0, sizeof( rpSaved ) );
	len = trap->FS_Open( RP_SAVED_FILE, &f, FS_READ );
	if ( !f ) {
		return 0;
	}
	if ( len <= 0 || len >= (int)sizeof( text ) ) {
		trap->FS_Close( f );
		return ( len <= 0 ) ? 0 : -1;
	}
	trap->FS_Read( text, len, f );
	trap->FS_Close( f );
	text[len] = '\0';

	p = text;
	COM_BeginParseSession( RP_SAVED_FILE );
	while ( n < RP_SAVED_MAX ) {
		rpSaved_t *s = &rpSaved[n];
		const char *tok = COM_ParseExt( &p, qtrue );
		byte check[VRRP_KEY_LEN];
		if ( !tok[0] ) {
			break;
		}
		Q_strncpyz( s->address, tok, sizeof( s->address ) );
		Q_strncpyz( s->salt, COM_ParseExt( &p, qfalse ), sizeof( s->salt ) );
		Q_strncpyz( s->account, COM_ParseExt( &p, qfalse ), sizeof( s->account ) );
		Q_strncpyz( s->key, COM_ParseExt( &p, qfalse ), sizeof( s->key ) );
		if ( strlen( s->salt ) == VRRP_SALT_HEX && CG_RP_AccountName( s->account, s->account, sizeof( s->account ) ) &&
			VRC_FromHex( s->key, check, sizeof( check ) ) ) {
			n++;
		}
		memset( check, 0, sizeof( check ) );
	}
	memset( text, 0, sizeof( text ) );
	return n;
}

static void CG_RP_WriteSaved( int count ) {
	static char text[RP_SAVED_TEXT];
	fileHandle_t f;
	int i;

	Q_strncpyz( text, "// OpenJK VR: the roleplay logins this PC remembers. Keys made from the passwords\n"
		"// for those servers alone, not the passwords; /logout forgets a server's.\n", sizeof( text ) );
	for ( i = 0; i < count; i++ ) {
		Q_strcat( text, sizeof( text ), va( "\"%s\" %s %s %s\n", rpSaved[i].address, rpSaved[i].salt, rpSaved[i].account, rpSaved[i].key ) );
	}
	trap->FS_Open( RP_SAVED_FILE, &f, FS_WRITE );
	if ( f ) {
		trap->FS_Write( text, (int)strlen( text ), f );
		trap->FS_Close( f );
	}
	memset( text, 0, sizeof( text ) );
	memset( rpSaved, 0, sizeof( rpSaved ) );
}

// This server's in rpSaved[] (-1: it isn't).
static int CG_RP_FindSaved( int count ) {
	int i;

	for ( i = 0; i < count; i++ ) {
		if ( !Q_stricmp( rpSaved[i].address, cgrp.address ) && !Q_stricmp( rpSaved[i].salt, cgrp.salt ) ) {
			return i;
		}
	}
	return -1;
}

static void CG_RP_Remember( const char *account, const byte *key ) {
	int n = CG_RP_ReadSaved(), i = CG_RP_FindSaved( n );

	if ( n < 0 ) {
		CG_RP_Show( "Your login can't be kept: " RP_SAVED_FILE " is too big to read", qtrue );
		return;
	}
	if ( !cgrp.address[0] || !cgrp.salt[0] ) {
		memset( rpSaved, 0, sizeof( rpSaved ) );
		return;
	}
	if ( i < 0 ) {
		if ( n == RP_SAVED_MAX ) {	// full: the first one kept goes
			memmove( &rpSaved[0], &rpSaved[1], sizeof( rpSaved[0] ) * ( RP_SAVED_MAX - 1 ) );
			n--;
		}
		i = n++;
	}
	Q_strncpyz( rpSaved[i].address, cgrp.address, sizeof( rpSaved[i].address ) );
	Q_strncpyz( rpSaved[i].salt, cgrp.salt, sizeof( rpSaved[i].salt ) );
	Q_strncpyz( rpSaved[i].account, account, sizeof( rpSaved[i].account ) );
	VRC_ToHex( key, VRRP_KEY_LEN, rpSaved[i].key );
	CG_RP_WriteSaved( n );
}

// This server's kept login forgotten (the salt not known yet: any kept for
// its address).
static void CG_RP_Forget( void ) {
	const int n = CG_RP_ReadSaved();
	int i, kept = 0;

	for ( i = 0; i < n; i++ ) {
		if ( !Q_stricmp( rpSaved[i].address, cgrp.address ) && ( !cgrp.salt[0] || !Q_stricmp( rpSaved[i].salt, cgrp.salt ) ) ) {
			continue;
		}
		rpSaved[kept++] = rpSaved[i];
	}
	if ( n <= 0 || kept == n || !cgrp.address[0] ) {
		memset( rpSaved, 0, sizeof( rpSaved ) );	// (nothing to forget; or too big to read, and so kept as it is)
		return;
	}
	CG_RP_WriteSaved( kept );
}

/*
================================================================================
Asking the server with a key: a new account, a login, a new password
================================================================================
*/

static void CG_RP_AskOver( void ) {
	memset( &cgrp.ask, 0, sizeof( cgrp.ask ) );	// (the keys with it)
}

// Sends what's asked, if it can go now: a login or a new password answers
// a challenge, and each one once, so it can wait for the next.
static void CG_RP_AskSend( void ) {
	char hex[VRRP_KEY_LEN * 2 + 1], hex2[VRRP_KEY_LEN * 2 + 1];
	byte proof[VRC_SHA256_LEN];

	if ( cgrp.ask.kind == RPASK_NONE || cgrp.ask.sent ) {
		return;
	}
	if ( cgrp.ask.kind != RPASK_NEW && ( cgrp.challengeUsed || !cgrp.challenge[0] ) ) {
		return;
	}
	switch ( cgrp.ask.kind ) {
	case RPASK_NEW:
		VRC_ToHex( cgrp.ask.key, VRRP_KEY_LEN, hex );
		CG_RP_Send( va( "rpnew %s %s", cgrp.ask.account, hex ) );
		break;
	case RPASK_LOGIN:
	case RPASK_AUTO:
		VRRP_LoginProof( cgrp.ask.key, "login", cgrp.challenge, cgrp.ask.account, proof );
		VRC_ToHex( proof, sizeof( proof ), hex );
		CG_RP_Send( va( "rplogin %s %s", cgrp.ask.account, hex ) );
		cgrp.challengeUsed = qtrue;
		break;
	case RPASK_PASSWORD:
		VRC_ToHex( cgrp.ask.key, VRRP_KEY_LEN, hex2 );
		VRRP_LoginProof( cgrp.ask.oldKey, va( "password %s", hex2 ), cgrp.challenge, cgrp.ask.account, proof );	// (the new key's part of it)
		VRC_ToHex( proof, sizeof( proof ), hex );
		CG_RP_Send( va( "rppass %s %s", hex, hex2 ) );
		cgrp.challengeUsed = qtrue;
		break;
	}
	memset( proof, 0, sizeof( proof ) );
	memset( hex, 0, sizeof( hex ) );
	memset( hex2, 0, sizeof( hex2 ) );
	cgrp.ask.sent = qtrue;
	cgrp.ask.time = cg.time;
}

// Whether something can be asked now (if not, the player's told why). With
// roleplay off too: logging in then changes nothing but an admin's cheats
// (a server older than that says no itself).
static qboolean CG_RP_Ready( void ) {
	if ( !cgrp.server ) {
		CG_RP_Show( "This server doesn't have roleplay", qtrue );
	} else if ( !cgrp.hello ) {
		CG_RP_Show( "Still getting in touch with the server: try again in a moment", qtrue );
	} else if ( cgrp.ask.kind != RPASK_NONE ) {
		CG_RP_Show( "Wait a moment: the server hasn't answered yet", qtrue );
	} else {
		return qtrue;
	}
	return qfalse;
}

// A password as it's taken: the spaces before and after it out (one slips in
// easily on the headset's keyboard, and the console drops them as it is).
static void CG_RP_Password( const char *in, char *out, int size ) {
	int n;

	while ( *in == ' ' || *in == '\t' ) {
		in++;
	}
	Q_strncpyz( out, in, size );
	for ( n = (int)strlen( out ); n > 0 && ( out[n - 1] == ' ' || out[n - 1] == '\t' ); n-- ) {
		out[n - 1] = '\0';
	}
}

static void CG_RP_NewAccount( const char *name, const char *typed, const char *typedAgain ) {
	char account[RP_ACCOUNT_MAX + 1], password[MAX_STRING_CHARS], again[MAX_STRING_CHARS];

	if ( !CG_RP_Ready() ) {
		return;
	}
	CG_RP_Password( typed, password, sizeof( password ) );
	CG_RP_Password( typedAgain, again, sizeof( again ) );
	if ( !CG_RP_AccountName( name, account, sizeof( account ) ) ) {
		CG_RP_Show( "An account name takes 3 to 20 letters, numbers, _ or -", qtrue );
	} else if ( (int)strlen( password ) < RP_PASSWORD_MIN ) {
		CG_RP_Show( "A password takes at least 4 characters", qtrue );
	} else if ( strcmp( password, again ) ) {
		CG_RP_Show( "The two passwords aren't the same", qtrue );
	} else if ( !Q_stricmp( account, cgrp.account ) ) {
		CG_RP_Show( "You're logged in to that account", qtrue );
	} else {
		cgrp.ask.kind = RPASK_NEW;
		Q_strncpyz( cgrp.ask.account, account, sizeof( cgrp.ask.account ) );
		VRRP_LoginKey( password, cgrp.salt, account, cgrp.ask.key );
		cgrp.ask.remember = (qboolean)( cg_rpRemember.integer != 0 );
		cgrp.ask.time = cg.time;
		CG_RP_AskSend();
	}
	CG_RP_Wipe( password, sizeof( password ) );
	CG_RP_Wipe( again, sizeof( again ) );
}

static void CG_RP_LogIn( const char *name, const char *typed ) {
	char account[RP_ACCOUNT_MAX + 1], password[MAX_STRING_CHARS];

	if ( !CG_RP_Ready() ) {
		return;
	}
	CG_RP_Password( typed, password, sizeof( password ) );
	if ( !CG_RP_AccountName( name, account, sizeof( account ) ) ) {
		CG_RP_Show( "An account name takes 3 to 20 letters, numbers, _ or -", qtrue );
	} else if ( !password[0] ) {
		CG_RP_Show( "Type the password too", qtrue );
	} else {
		cgrp.ask.kind = RPASK_LOGIN;
		Q_strncpyz( cgrp.ask.account, account, sizeof( cgrp.ask.account ) );
		VRRP_LoginKey( password, cgrp.salt, account, cgrp.ask.key );
		cgrp.ask.remember = (qboolean)( cg_rpRemember.integer != 0 );
		cgrp.ask.time = cg.time;
		CG_RP_AskSend();
	}
	CG_RP_Wipe( password, sizeof( password ) );
}

static void CG_RP_NewPassword( const char *typedNow, const char *typed, const char *typedAgain ) {
	char now[MAX_STRING_CHARS], password[MAX_STRING_CHARS], again[MAX_STRING_CHARS];

	if ( !CG_RP_Ready() ) {
		return;
	}
	CG_RP_Password( typedNow, now, sizeof( now ) );
	CG_RP_Password( typed, password, sizeof( password ) );
	CG_RP_Password( typedAgain, again, sizeof( again ) );
	if ( !cgrp.account[0] ) {
		CG_RP_Show( "Log in first", qtrue );
	} else if ( (int)strlen( password ) < RP_PASSWORD_MIN ) {
		CG_RP_Show( "A password takes at least 4 characters", qtrue );
	} else if ( strcmp( password, again ) ) {
		CG_RP_Show( "The two new passwords aren't the same", qtrue );
	} else {
		cgrp.ask.kind = RPASK_PASSWORD;
		Q_strncpyz( cgrp.ask.account, cgrp.account, sizeof( cgrp.ask.account ) );
		VRRP_LoginKey( now, cgrp.salt, cgrp.account, cgrp.ask.oldKey );
		VRRP_LoginKey( password, cgrp.salt, cgrp.account, cgrp.ask.key );
		cgrp.ask.time = cg.time;
		CG_RP_AskSend();
	}
	CG_RP_Wipe( now, sizeof( now ) );
	CG_RP_Wipe( password, sizeof( password ) );
	CG_RP_Wipe( again, sizeof( again ) );
}

static void CG_RP_LogOut( void ) {
	qboolean asking;

	if ( !cgrp.server ) {
		CG_RP_Show( "This server doesn't have roleplay", qtrue );
		return;
	}
	CG_RP_Forget();
	asking = (qboolean)( cgrp.ask.kind != RPASK_NONE && cgrp.ask.kind != RPASK_PASSWORD );
	CG_RP_AskOver();	// (whatever it was doesn't matter now)
	if ( cgrp.known && !cgrp.account[0] && !asking ) {
		CG_RP_Show( "You're not logged in", qtrue );
		return;
	}
	// (the server's told even when it isn't known yet whether it has us logged
	// in, or a login's on its way: it takes them in turn, and ends logged out)
	CG_RP_Send( "rplogout" );
}

/*
================================================================================
Characters' cards and bios
================================================================================
*/

static qboolean CG_RP_WeaponAway( void ) {
	const playerState_t *ps = &cg.predictedPlayerState;

	switch ( ps->weapon ) {
	case WP_NONE:
	case WP_MELEE:
		return qtrue;
	case WP_SABER:
		return (qboolean)( ps->saberHolstered >= 2 );
	default:
		return qfalse;
	}
}

static qboolean CG_RP_CardsWanted( void ) {
	return (qboolean)( cgrp.on && cg_rpCards.integer && cg.snap && cg.snap->ps.pm_type != PM_INTERMISSION && CG_RP_WeaponAway() );
}

// A companion's card: its name, whose it is, its side (its owner's, as the
// NPCs see it) - made here (NULL: n isn't a companion now).
static const rpCard_t *CG_RP_CompCard( int n ) {
	const char *name = CG_RP_CompName( n );
	const int owner = CG_RP_CompOwner( n );
	char whose[MAX_QPATH * 2], plain[MAX_QPATH * 2];

	if ( !name || owner < 0 || !cg_entities[n].currentValid ) {
		return NULL;
	}
	Com_sprintf( whose, sizeof( whose ), "%s's companion", cgs.clientinfo[owner].infoValid ? cgs.clientinfo[owner].name : "Someone" );
	if ( strcmp( rpCompCard.name, name ) || strcmp( rpCompCardWhose, whose ) ) {
		Q_strncpyz( rpCompCard.name, name, sizeof( rpCompCard.name ) );
		Q_strncpyz( rpCompCardWhose, whose, sizeof( rpCompCardWhose ) );
		CG_RP_Plain( whose, plain, sizeof( plain ) );
		CG_RP_Layout( plain, RP_CARD_WIDTH, 2, &rpCompCard.desc );
	}
	switch ( cg_entities[n].currentState.teamowner ) {
	case NPCTEAM_PLAYER:	rpCompCard.faction = RPF_HEROES; break;
	case NPCTEAM_ENEMY:		rpCompCard.faction = RPF_VILLAINS; break;
	default:				rpCompCard.faction = RPF_NEUTRAL; break;
	}
	rpCompCard.time = cg.time ? cg.time : 1;
	return &rpCompCard;
}

// What a card shows of client n (or a companion), if there's one to show (NULL: none).
static const rpCard_t *CG_RP_Card( int n ) {
	const rpCard_t *c;

	if ( n >= MAX_CLIENTS ) {
		return CG_RP_CompCard( n );
	}
	if ( n < 0 || !cgs.clientinfo[n].infoValid ) {
		return NULL;
	}
	c = &rpCards[n];
	return ( c->time && c->faction >= 0 && !strcmp( c->name, cgs.clientinfo[n].name ) ) ? c : NULL;
}

// Whether a player's card is to be asked for: not known lately (by the name
// they go by now), nor asked for lately.
static qboolean CG_RP_CardDue( int n ) {
	const rpCard_t *c = &rpCards[n];

	if ( c->time && cg.time - c->time < RP_CARD_KEEP && !strcmp( c->name, cgs.clientinfo[n].name ) ) {
		return qfalse;
	}
	return (qboolean)!( c->asked && cg.time - c->asked < RP_CARD_ASK_GAP );
}

// Their card asked for, if it's due and there's room (if not, it's asked
// for as it's looked at again).
static void CG_RP_AskCard( int n ) {
	if ( n < 0 || n >= MAX_CLIENTS ) {
		return;	// (a companion's card is made here)
	}
	if ( !CG_RP_CardDue( n ) || !CG_RP_Room() ) {
		return;
	}
	rpCards[n].asked = cg.time ? cg.time : 1;
	CG_RP_Send( va( "rpcard %i", n ) );
}

// Where you look from, and which way (the headset: where it faces).
static void CG_RP_Gaze( vec3_t start, vec3_t dir ) {
	matrix3_t axis;

	if ( CG_VR_HeadPose( start, axis ) ) {
		VectorCopy( axis[0], dir );
		return;
	}
	VectorCopy( cg.predictedPlayerState.origin, start );
	start[2] += cg.predictedPlayerState.viewheight;
	VectorCopy( cg.refdef.viewaxis[0], dir );
}

// Who you look at, and how long.
static void CG_RP_Look( void ) {
	static const vec3_t mins = { -6.0f, -6.0f, -6.0f }, maxs = { 6.0f, 6.0f, 6.0f };
	const int self = cg.snap->ps.clientNum;
	int target = -1;

	if ( CG_RP_CardsWanted() ) {
		vec3_t start, dir, end;
		trace_t tr;
		CG_RP_Gaze( start, dir );
		VectorMA( start, RP_CARD_RANGE, dir, end );
		CG_Trace( &tr, start, mins, maxs, end, self, CONTENTS_SOLID | CONTENTS_BODY );
		if ( tr.entityNum >= 0 && tr.entityNum < MAX_CLIENTS && tr.entityNum != self && cgs.clientinfo[tr.entityNum].infoValid ) {
			target = tr.entityNum;
		} else if ( CG_RP_CompName( tr.entityNum ) ) {
			target = tr.entityNum;	// (a player's companion)
		}
	}
	if ( target >= 0 ) {
		if ( target != cgrp.target ) {
			cgrp.target = target;
			cgrp.since = cg.time;
		}
		cgrp.seen = cg.time;
	} else if ( cgrp.target >= 0 && cg.time - cgrp.seen > RP_CARD_GRACE ) {
		cgrp.target = -1;
	}
	if ( cgrp.target >= 0 && cg.time - cgrp.since >= RP_CARD_DWELL / 2 ) {
		CG_RP_AskCard( cgrp.target );	// (a little early, so it's here when it's due)
	}
	if ( cgrp.bioWho >= 0 && cgrp.bioWho != cgrp.target ) {
		cgrp.bioWho = -1;	// (a bio read goes when you look away)
	}
}

// Read their bio (the wheel can, while a player's card shows: a companion has none).
qboolean CG_RP_CanReadBio( void ) {
	return (qboolean)( CG_RP_CardsWanted() && cgrp.target >= 0 && cgrp.target < MAX_CLIENTS && cg.time - cgrp.since >= RP_CARD_DWELL &&
		CG_RP_Card( cgrp.target ) );
}

void CG_RP_ReadBio( void ) {
	if ( !cgrp.on ) {
		CG_RP_Show( "Roleplay is off on this server right now", qfalse );
		return;
	}
	if ( !CG_RP_CanReadBio() ) {
		CG_RP_Show( "Look at someone playing a character first (your weapon away)", qfalse );
		return;
	}
	cgrp.bioWho = cgrp.target;
	cgrp.bioTime = 0;
	CG_RP_Send( va( "rpbio %i", cgrp.target ) );
}

/*
================================================================================
Console commands, and the menus' (rpui)
================================================================================
*/

static void CG_RP_New_f( void ) {
	char account[64], password[MAX_STRING_CHARS];

	if ( trap->Cmd_Argc() < 3 ) {
		CG_RP_Show( "/new <account> <password>: makes an account for your characters on this server", qtrue );
		return;
	}
	trap->Cmd_Argv( 1, account, sizeof( account ) );
	CG_RP_ArgsFrom( 2, password, sizeof( password ) );
	CG_RP_NewAccount( account, password, password );
	CG_RP_Wipe( password, sizeof( password ) );
}

static void CG_RP_Login_f( void ) {
	char account[64], password[MAX_STRING_CHARS];

	if ( trap->Cmd_Argc() < 3 ) {
		CG_RP_Show( "/login <account> <password>: logs in to your account on this server", qtrue );
		return;
	}
	trap->Cmd_Argv( 1, account, sizeof( account ) );
	CG_RP_ArgsFrom( 2, password, sizeof( password ) );
	CG_RP_LogIn( account, password );
	CG_RP_Wipe( password, sizeof( password ) );
}

static void CG_RP_ChangePassword_f( void ) {
	char now[MAX_STRING_CHARS], password[MAX_STRING_CHARS];

	if ( trap->Cmd_Argc() != 3 ) {
		CG_RP_Show( "/changepassword <password now> <new password> (\"in quotes\" with spaces)", qtrue );
		return;
	}
	trap->Cmd_Argv( 1, now, sizeof( now ) );
	trap->Cmd_Argv( 2, password, sizeof( password ) );
	CG_RP_NewPassword( now, password, password );
	CG_RP_Wipe( now, sizeof( now ) );
	CG_RP_Wipe( password, sizeof( password ) );
}

static void CG_RP_Logout_f( void ) {
	CG_RP_LogOut();
}

static void CG_RP_Bio_f( void ) {
	CG_RP_ReadBio();
}

// The Roleplay menu's Editor: the character played, into its fields.
static void CG_RP_UIEdit( const rpChar_t *ch ) {
	trap->Cvar_Set( "ui_rpEditName", ch->name );
	trap->Cvar_Set( "ui_rpEditFaction", va( "%i", ch->faction ) );
	trap->Cvar_Set( "ui_rpEditDesc", ch->desc );
	trap->Cvar_Set( "ui_rpEditBio", ch->bio );
}

// A cvar a menu typed into, read and emptied.
static void CG_RP_Take( const char *cvar, char *out, int size ) {
	trap->Cvar_VariableStringBuffer( cvar, out, size );
	trap->Cvar_Set( cvar, "" );
}

/*
rpui <what>: the Roleplay menu's buttons.
  login, new        ui_rpLoginAccount, ui_rpLoginPassword (new: ui_rpLoginAgain)
  password          ui_rpPassword, ui_rpNewPassword, ui_rpNewAgain
  logout
  play <0-9|-1>     the character to play (-1: none)
  delete <0-9>
  newchar           ui_rpNewName
  edit              the character played into ui_rpEditFaction, ui_rpEditDesc, ui_rpEditBio...
  save              ...and what's changed there back
  bio               read the bio of who you look at
  admin <what>      the Admin page (CG_RP_Admin)
  comp <what>       the Companions page (cg_vrcomp.c: CG_RP_CompUI)
The passwords' cvars are emptied as they're read.
*/

// An NPC's or vehicle's name as its files have it: letters, digits, _ - .
static qboolean CG_RP_NPCName( const char *name ) {
	const char *p;

	if ( !name[0] || (int)strlen( name ) > RP_NPC_NAME_MAX ) {
		return qfalse;
	}
	for ( p = name; *p; p++ ) {
		if ( !( ( *p >= 'a' && *p <= 'z' ) || ( *p >= 'A' && *p <= 'Z' ) || ( *p >= '0' && *p <= '9' ) || *p == '_' || *p == '-' || *p == '.' ) ) {
			return qfalse;
		}
	}
	return qtrue;
}

// An invasion's lineup as the Admin page sends it: an NPC type's name (as
// CG_RP_NPCName has one) - or, several (a server that takes them), up to
// RP_LINEUP_KINDS of them a comma between, each its own number a wave
// after a colon, 1 to RP_LINEUP_EACH_MOST, or none (mixed): reborn:2,jedi.
static qboolean CG_RP_LineupOk( const char *lineup, qboolean several ) {
	const char *p = lineup;
	int n;

	if ( !several ) {
		return CG_RP_NPCName( lineup );
	}
	for ( n = 0; n < RP_LINEUP_KINDS; n++ ) {
		char name[RP_NPC_NAME_MAX + 2];
		const int len = (int)strcspn( p, ",:" );
		if ( len > RP_NPC_NAME_MAX ) {
			return qfalse;
		}
		memcpy( name, p, len );
		name[len] = '\0';
		if ( !CG_RP_NPCName( name ) ) {
			return qfalse;
		}
		p += len;
		if ( *p == ':' ) {
			const int digits = (int)strspn( ++p, "0123456789" );
			const int each = ( digits > 0 && digits <= 2 ) ? atoi( p ) : 0;
			if ( each < 1 || each > RP_LINEUP_EACH_MOST ) {
				return qfalse;
			}
			p += digits;
		}
		if ( !*p ) {
			return qtrue;
		}
		if ( *p++ != ',' ) {
			return qfalse;
		}
	}
	return qfalse;	// (more than a server takes)
}

// The Admin page's Faction for an NPC (a spawn's, an invasion's): its word
// for the server (g_vrdown.c's G_RPSideWord: "" none, its NPC file's), and
// what the page then says of one spawned.
static const char *rpSideWords[] = { "", "light", "dark", "neutral", "creature" };
static const char *rpSideSaid[] = { "", " - with the Heroes", " - with the Villains", " - neutral", " - a creature, against everyone" };

// A Faction's word as the page sends it: rpSideWords' (-1: not one).
static int CG_RP_SideWord( const char *word ) {
	int i;

	for ( i = 0; i < (int)ARRAY_LEN( rpSideWords ); i++ ) {
		if ( !strcmp( word, rpSideWords[i] ) ) {
			return i;
		}
	}
	return -1;
}

/*
rpui admin <what>: the Admin page's buttons, the game's own cheat commands
(the server lets an admin use them with cheats off):
  spawn <name> <npc|vehicle> [<faction>]   in front of you, tagged
                               vra_<account>; its faction (light, dark,
                               neutral, creature: npc spawn's side - not a
                               vehicle's, and where the server takes it,
                               RPA_SIDES), else its NPC file's
  killmine, killall            the NPCs spawned here with your tag / every one
  god, noclip, notarget        on or off
  give                         give all
  forcepowers                  give forcepowers: every Force power at level 3
  force <client> grey|master|normal   rp force: whoever's picked on the
                               People page - their character
  invasion start <who> <waves> <per> <cap> cleared|every <seconds> [<faction>]
  invasion stop                rp invasion (the Invasion part: g_vrinvade.c) -
                               <who>, a lineup (CG_RP_LineupOk)
  war start <light> <dark> <creatures> <waves> <per> <cap> cleared|every <seconds>
  war stop                     rp war (its War mode, a server that has them:
                               RPA_WARS) - each side's lineup, or - (not in it)
*/
static void CG_RP_Admin( void ) {
	char what[32];

	trap->Cmd_Argv( 2, what, sizeof( what ) );
	if ( !( cgrp.admin & RPA_ADMIN ) || !cgrp.account[0] ) {
		trap->Cvar_Set( "ui_rpMessage", "You're not an admin on this server" );
		return;
	}
	if ( !Q_stricmp( what, "spawn" ) ) {
		char name[MAX_QPATH * 2], kind[16], faction[MAX_QPATH];	// (room for one too long: seen whole, and refused)
		qboolean vehicle;
		int side;
		trap->Cmd_Argv( 3, name, sizeof( name ) );
		trap->Cmd_Argv( 4, kind, sizeof( kind ) );
		trap->Cmd_Argv( 5, faction, sizeof( faction ) );
		if ( !CG_RP_NPCName( name ) ) {
			trap->Cvar_Set( "ui_rpMessage", "Pick one in the list first" );
			return;
		}
		if ( ( side = CG_RP_SideWord( faction ) ) < 0 ) {
			return;	// (not the page's)
		}
		vehicle = (qboolean)!Q_stricmp( kind, "vehicle" );
		if ( vehicle || !( cgrp.admin & RPA_SIDES ) ) {
			side = 0;	// (a vehicle takes none; a server that takes none: as its NPC file says)
		}
		CG_RP_Send( va( "npc spawn %s%s " RP_ADMIN_TAG "%s%s%s", vehicle ? "vehicle " : "", name, cgrp.account, side ? " " : "", rpSideWords[side] ) );
		trap->Cvar_Set( "ui_rpMessage", va( "%s, in front of you%s", name, rpSideSaid[side] ) );
	} else if ( !Q_stricmp( what, "killmine" ) ) {
		CG_RP_Send( va( "npc kill " RP_ADMIN_TAG "%s", cgrp.account ) );
		trap->Cvar_Set( "ui_rpMessage", "The NPCs you spawned here: killed" );
	} else if ( !Q_stricmp( what, "killall" ) ) {
		CG_RP_Send( "npc kill all" );
		trap->Cvar_Set( "ui_rpMessage", "Every NPC here: killed" );
	} else if ( !Q_stricmp( what, "god" ) || !Q_stricmp( what, "noclip" ) || !Q_stricmp( what, "notarget" ) ) {
		CG_RP_Send( what );	// (on or off: the page shows it as the server says - rpadmin)
		trap->Cvar_Set( "ui_rpMessage", "" );
	} else if ( !Q_stricmp( what, "give" ) ) {
		CG_RP_Send( "give all" );
		trap->Cvar_Set( "ui_rpMessage", "Every weapon and item, full ammo, health and armor" );
	} else if ( !Q_stricmp( what, "forcepowers" ) ) {
		CG_RP_Send( "give forcepowers" );
		trap->Cvar_Set( "ui_rpMessage", "" );	// (what came of it, as the server says: rpmsg - a Siege class keeps its own)
	} else if ( !Q_stricmp( what, "force" ) ) {
		char who[16], how[16];
		int n;
		trap->Cmd_Argv( 3, who, sizeof( who ) );
		trap->Cmd_Argv( 4, how, sizeof( how ) );
		n = atoi( who );	// (a client's number, as the People page has it: nothing else)
		if ( who[0] && strlen( who ) <= 2 && strspn( who, "0123456789" ) == strlen( who ) && n < MAX_CLIENTS && cgs.clientinfo[n].infoValid &&
			( !Q_stricmp( how, "grey" ) || !Q_stricmp( how, "master" ) || !Q_stricmp( how, "normal" ) ) ) {
			CG_RP_Send( va( "rp force %i %s", n, how ) );	// (the server says what came of it: rpmsg, and their card)
		}
	} else if ( !Q_stricmp( what, "invasion" ) ) {
		char how[16];
		trap->Cmd_Argv( 3, how, sizeof( how ) );
		if ( !Q_stricmp( how, "stop" ) ) {
			CG_RP_Send( "rp invasion stop" );
			trap->Cvar_Set( "ui_rpMessage", "" );	// (what came of it, as the server says: rpmsg)
		} else if ( !Q_stricmp( how, "start" ) ) {
			char who[RP_LINEUP_SIZE], next[16], faction[MAX_QPATH];
			int side;
			trap->Cmd_Argv( 4, who, sizeof( who ) );
			trap->Cmd_Argv( 8, next, sizeof( next ) );
			trap->Cmd_Argv( 10, faction, sizeof( faction ) );
			if ( !CG_RP_LineupOk( who, (qboolean)( ( cgrp.admin & RPA_WARS ) != 0 ) ) ) {
				trap->Cvar_Set( "ui_rpMessage", "Type the NPC type's name: letters, numbers, _ - and ." );
				return;
			}
			if ( ( Q_stricmp( next, "cleared" ) && Q_stricmp( next, "every" ) ) || ( side = CG_RP_SideWord( faction ) ) < 0 ) {
				return;	// (not the page's)
			}
			if ( !( cgrp.admin & RPA_SIDES ) ) {
				side = 0;	// (a server that takes none: its NPC file's)
			}
			{	// (each read by itself: CG_Argv's one buffer)
				const int waves = Com_Clampi( 0, RP_INV_WAVES_MOST, atoi( CG_Argv( 5 ) ) );
				const int per = Com_Clampi( 1, RP_INV_MOST, atoi( CG_Argv( 6 ) ) );
				const int cap = Com_Clampi( 1, RP_INV_MOST, atoi( CG_Argv( 7 ) ) );
				const int wait = Com_Clampi( 0, RP_INV_WAIT_MOST, atoi( CG_Argv( 9 ) ) );
				CG_RP_Send( va( "rp invasion start %s waves %i per %i cap %i %s %i%s%s", who, waves, per, cap, Q_stricmp( next, "every" ) ? "cleared" : "every", wait,
					side ? " side " : "", rpSideWords[side] ) );
			}
			trap->Cvar_Set( "ui_rpMessage", "" );
		}
	} else if ( !Q_stricmp( what, "war" ) ) {
		static const char *words[3] = { "light", "dark", "creatures" };
		char how[16];
		trap->Cmd_Argv( 3, how, sizeof( how ) );
		if ( !( cgrp.admin & RPA_WARS ) ) {
			trap->Cvar_Set( "ui_rpMessage", "This server has no wars" );
			return;
		}
		if ( !Q_stricmp( how, "stop" ) ) {
			CG_RP_Send( "rp war stop" );
			trap->Cvar_Set( "ui_rpMessage", "" );	// (what came of it, as the server says: rpmsg)
		} else if ( !Q_stricmp( how, "start" ) ) {
			char who[3][RP_LINEUP_SIZE], next[16], line[MAX_STRING_CHARS];
			int s, sides = 0;
			for ( s = 0; s < 3; s++ ) {
				trap->Cmd_Argv( 4 + s, who[s], sizeof( who[s] ) );
				if ( strcmp( who[s], "-" ) ) {
					if ( !CG_RP_LineupOk( who[s], qtrue ) ) {
						trap->Cvar_Set( "ui_rpMessage", "Type the NPC type's name: letters, numbers, _ - and ." );
						return;
					}
					sides++;
				}
			}
			trap->Cmd_Argv( 10, next, sizeof( next ) );
			if ( sides < 2 || ( Q_stricmp( next, "cleared" ) && Q_stricmp( next, "every" ) ) ) {
				return;	// (not the page's)
			}
			Q_strncpyz( line, "rp war start", sizeof( line ) );
			for ( s = 0; s < 3; s++ ) {
				if ( strcmp( who[s], "-" ) ) {
					Q_strcat( line, sizeof( line ), va( " %s %s", words[s], who[s] ) );
				}
			}
			{	// (each read by itself: CG_Argv's one buffer)
				const int waves = Com_Clampi( 0, RP_INV_WAVES_MOST, atoi( CG_Argv( 7 ) ) );
				const int per = Com_Clampi( 1, RP_INV_MOST, atoi( CG_Argv( 8 ) ) );
				const int cap = Com_Clampi( 1, RP_INV_MOST, atoi( CG_Argv( 9 ) ) );
				const int wait = Com_Clampi( 0, RP_INV_WAIT_MOST, atoi( CG_Argv( 11 ) ) );
				Q_strcat( line, sizeof( line ), va( " waves %i per %i cap %i %s %i", waves, per, cap, Q_stricmp( next, "every" ) ? "cleared" : "every", wait ) );
			}
			CG_RP_Send( line );	// (where you stand: what came of it, as the server says - rpmsg)
			trap->Cvar_Set( "ui_rpMessage", "" );
		}
	}
}

static void CG_RP_UI_f( void ) {
	char what[32];

	trap->Cmd_Argv( 1, what, sizeof( what ) );
	if ( !Q_stricmp( what, "login" ) || !Q_stricmp( what, "new" ) ) {
		char account[MAX_CVAR_VALUE_STRING], password[MAX_CVAR_VALUE_STRING], again[MAX_CVAR_VALUE_STRING];
		trap->Cvar_VariableStringBuffer( "ui_rpLoginAccount", account, sizeof( account ) );
		CG_RP_Take( "ui_rpLoginPassword", password, sizeof( password ) );
		CG_RP_Take( "ui_rpLoginAgain", again, sizeof( again ) );
		if ( !Q_stricmp( what, "login" ) ) {
			CG_RP_LogIn( account, password );
		} else {
			CG_RP_NewAccount( account, password, again );
		}
		CG_RP_Wipe( password, sizeof( password ) );
		CG_RP_Wipe( again, sizeof( again ) );
	} else if ( !Q_stricmp( what, "password" ) ) {
		char now[MAX_CVAR_VALUE_STRING], password[MAX_CVAR_VALUE_STRING], again[MAX_CVAR_VALUE_STRING];
		CG_RP_Take( "ui_rpPassword", now, sizeof( now ) );
		CG_RP_Take( "ui_rpNewPassword", password, sizeof( password ) );
		CG_RP_Take( "ui_rpNewAgain", again, sizeof( again ) );
		CG_RP_NewPassword( now, password, again );
		CG_RP_Wipe( now, sizeof( now ) );
		CG_RP_Wipe( password, sizeof( password ) );
		CG_RP_Wipe( again, sizeof( again ) );
	} else if ( !Q_stricmp( what, "logout" ) ) {
		CG_RP_LogOut();
	} else if ( !Q_stricmp( what, "play" ) ) {
		const char *arg = CG_Argv( 2 );
		const int n = atoi( arg );
		if ( !arg[0] || n < 0 ) {
			CG_RP_Send( "rp play none" );
		} else if ( n < cgrp.numChars ) {
			CG_RP_Send( va( "rp play %i", n + 1 ) );
		}
	} else if ( !Q_stricmp( what, "delete" ) ) {
		const char *arg = CG_Argv( 2 );
		const int n = atoi( arg );
		if ( arg[0] && n >= 0 && n < cgrp.numChars ) {
			CG_RP_Send( va( "rp delete %i", n + 1 ) );
		}
	} else if ( !Q_stricmp( what, "newchar" ) ) {
		char name[MAX_CVAR_VALUE_STRING];
		CG_RP_Take( "ui_rpNewName", name, sizeof( name ) );
		if ( name[0] ) {
			CG_RP_Send( va( "rp new %s", name ) );
		} else {
			CG_RP_Show( "Type a name for the character", qfalse );
		}
	} else if ( !Q_stricmp( what, "edit" ) || !Q_stricmp( what, "save" ) ) {
		const rpChar_t *ch = ( cgrp.active >= 0 && cgrp.active < cgrp.numChars ) ? &cgrp.chars[cgrp.active] : NULL;
		if ( !ch ) {
			CG_RP_Show( "Play a character first", qfalse );
		} else if ( !Q_stricmp( what, "edit" ) ) {
			CG_RP_UIEdit( ch );
		} else {
			char text[MAX_CVAR_VALUE_STRING];
			int faction;
			trap->Cvar_VariableStringBuffer( "ui_rpEditName", text, sizeof( text ) );
			if ( strcmp( text, ch->name ) ) {
				// the fields were another character's: never saved to this one
				CG_RP_UIEdit( ch );
				trap->Cvar_Set( "ui_rpMessage", va( "You play %s" S_COLOR_WHITE " now: check what's here, then save", ch->name ) );
				return;
			}
			trap->Cvar_VariableStringBuffer( "ui_rpEditFaction", text, sizeof( text ) );
			faction = atoi( text );
			if ( text[0] && faction >= 0 && faction < RPF_NUM && faction != ch->faction ) {
				CG_RP_Send( va( "rp faction %s", rpFactions[faction] ) );
			}
			trap->Cvar_VariableStringBuffer( "ui_rpEditDesc", text, sizeof( text ) );
			if ( strcmp( text, ch->desc ) ) {
				CG_RP_Send( va( "rp desc %s", text ) );
			}
			trap->Cvar_VariableStringBuffer( "ui_rpEditBio", text, sizeof( text ) );
			if ( strcmp( text, ch->bio ) ) {
				CG_RP_Send( va( "rp bio %s", text ) );
			}
		}
	} else if ( !Q_stricmp( what, "bio" ) ) {
		const char *arg = CG_Argv( 2 );
		const int n = atoi( arg );
		if ( !arg[0] ) {
			CG_RP_ReadBio();	// (whom you look at)
		} else if ( n >= 0 && n < MAX_CLIENTS && cgs.clientinfo[n].infoValid ) {
			cgrp.menuBio = n;	// (someone in the menu's list)
			CG_RP_Send( va( "rpbio %i", n ) );
		}
	} else if ( !Q_stricmp( what, "people" ) ) {
		cgrp.peopleUntil = cg.time + RP_PEOPLE_TIME;	// (everyone's card: CG_RP_People)
	} else if ( !Q_stricmp( what, "admin" ) ) {
		CG_RP_Admin();
	} else if ( !Q_stricmp( what, "comp" ) ) {
		CG_RP_CompUI();	// (the Companions page: cg_vrcomp.c)
	}
}

/*
================================================================================
From the server
================================================================================
*/

// rpch <salt> <challenge> - an answer to this game's hello. One that comes
// before it's said hello answers another's: the game before a vid_restart -
// its hello said what that one had of its Force setup, not what this one has
// (its own, put back): this one says hello itself, and the server answers it
// with the same challenge.
static void CG_RP_ChallengeCmd( void ) {
	char salt[64], challenge[64];
	byte check[VRRP_SALT_HEX / 2];

	if ( !cgrp.helloTime ) {
		return;
	}
	trap->Cmd_Argv( 1, salt, sizeof( salt ) );
	trap->Cmd_Argv( 2, challenge, sizeof( challenge ) );
	if ( !VRC_FromHex( salt, check, sizeof( check ) ) || !VRC_FromHex( challenge, check, VRRP_CHALLENGE_HEX / 2 ) ) {
		return;
	}
	Q_strlwr( salt );
	Q_strncpyz( cgrp.salt, salt, sizeof( cgrp.salt ) );
	Q_strncpyz( cgrp.challenge, challenge, sizeof( cgrp.challenge ) );
	cgrp.hello = qtrue;
	cgrp.challengeUsed = qfalse;
	CG_RP_AskSend();	// (one waiting for a challenge goes)
}

// rpacct <account|-> <active> <count> <most> <1: this answers a login or new account | 0>
static void CG_RP_AccountCmd( void ) {
	char account[64];
	const qboolean answer = (qboolean)( atoi( CG_Argv( 5 ) ) == 1 );
	int i;

	trap->Cmd_Argv( 1, account, sizeof( account ) );
	if ( !CG_RP_AccountName( account, account, sizeof( account ) ) ) {
		account[0] = '\0';	// ("-": none)
	}
	cgrp.known = qtrue;
	if ( Q_stricmp( account, cgrp.account ) ) {
		memset( cgrp.chars, 0, sizeof( cgrp.chars ) );	// (another account's come...)
		if ( cgrp.admin ) {
			cgrp.admin = 0;	// (...not an admin until the server says so: rpadmin, a frame on)
			CG_RP_UIAdmin();
			CG_RP_UIInvadeClear();
		}
	}
	Q_strncpyz( cgrp.account, account, sizeof( cgrp.account ) );
	cgrp.numChars = account[0] ? Com_Clampi( 0, RP_CHARS_MAX, atoi( CG_Argv( 3 ) ) ) : 0;
	cgrp.active = account[0] ? Com_Clampi( -1, cgrp.numChars - 1, atoi( CG_Argv( 2 ) ) ) : -1;
	cgrp.most = Com_Clampi( 0, RP_CHARS_MAX, atoi( CG_Argv( 4 ) ) );
	for ( i = cgrp.numChars; i < RP_CHARS_MAX; i++ ) {
		memset( &cgrp.chars[i], 0, sizeof( cgrp.chars[i] ) );
	}

	// a login or a new account went through (not just where things stand: a
	// late answer to a hello names the account too)
	if ( account[0] && answer && ( cgrp.ask.kind == RPASK_NEW || cgrp.ask.kind == RPASK_LOGIN || cgrp.ask.kind == RPASK_AUTO ) &&
		cgrp.ask.sent && !Q_stricmp( cgrp.ask.account, account ) ) {
		if ( cgrp.ask.kind != RPASK_AUTO ) {
			if ( cgrp.ask.remember ) {
				CG_RP_Remember( account, cgrp.ask.key );
			} else {
				CG_RP_Forget();
			}
		}
		CG_RP_AskOver();
	}
	CG_RP_UIAccount();
}

// rpchar <i> <faction> "<name>" "<desc>" "<bio>"
static void CG_RP_CharCmd( void ) {
	const int i = atoi( CG_Argv( 1 ) );
	rpChar_t *ch;

	if ( i < 0 || i >= RP_CHARS_MAX ) {
		return;
	}
	ch = &cgrp.chars[i];
	ch->faction = Com_Clampi( 0, RPF_NUM - 1, atoi( CG_Argv( 2 ) ) );
	trap->Cmd_Argv( 3, ch->name, sizeof( ch->name ) );
	trap->Cmd_Argv( 4, ch->desc, sizeof( ch->desc ) );
	trap->Cmd_Argv( 5, ch->bio, sizeof( ch->bio ) );
	CG_RP_UIChar( i );
	if ( i == cgrp.active ) {
		CG_RP_UIPlaying();
	}
}

// rpcard <client> <faction|-1> "<desc>" [<role>]: to an admin, what an admin
// made the character (0 neither, 1 a Grey Jedi, 2 a Force Master - the People
// page's Force line: ui_rpWhoR<client>, "" not said)
static void CG_RP_CardCmd( void ) {
	const int n = atoi( CG_Argv( 1 ) );
	char desc[RP_DESC_MAX * 2], plain[RP_DESC_MAX * 2];
	rpCard_t *c;

	if ( n < 0 || n >= MAX_CLIENTS ) {
		return;
	}
	c = &rpCards[n];
	c->faction = atoi( CG_Argv( 2 ) );
	if ( c->faction < 0 || c->faction >= RPF_NUM ) {
		c->faction = -1;
	}
	trap->Cmd_Argv( 3, desc, sizeof( desc ) );
	CG_RP_Plain( desc, plain, sizeof( plain ) );
	CG_RP_Layout( plain, RP_CARD_WIDTH, 2, &c->desc );
	Q_strncpyz( c->name, cgs.clientinfo[n].name, sizeof( c->name ) );
	c->time = cg.time ? cg.time : 1;
	trap->Cvar_Set( va( "ui_rpWhoF%i", n ), va( "%i", c->faction ) );	// (the menu's People page)
	trap->Cvar_Set( va( "ui_rpWhoD%i", n ), plain );
	trap->Cvar_Set( va( "ui_rpWhoR%i", n ), ( trap->Cmd_Argc() > 4 ) ? va( "%i", Com_Clampi( 0, 2, atoi( CG_Argv( 4 ) ) ) ) : "" );
}

// rpbio <client> "<bio>": for the menu's People page, or the card
static void CG_RP_BioCmd( void ) {
	const int n = atoi( CG_Argv( 1 ) );
	char bio[RP_BIO_MAX * 2], plain[RP_BIO_MAX * 2];

	if ( n < 0 || n >= MAX_CLIENTS ) {
		return;
	}
	trap->Cmd_Argv( 2, bio, sizeof( bio ) );
	CG_RP_Plain( bio, plain, sizeof( plain ) );
	if ( n == cgrp.menuBio ) {
		trap->Cvar_Set( "ui_rpBioRead", plain[0] ? plain : "(no bio)" );
		cgrp.menuBio = -1;
	}
	if ( n != cgrp.bioWho ) {
		return;
	}
	if ( !plain[0] ) {
		CG_RP_Show( va( "%s" S_COLOR_WHITE " has no bio", cgs.clientinfo[n].name ), qfalse );
		cgrp.bioWho = -1;
		return;
	}
	CG_RP_Layout( plain, RP_BIO_WIDTH, RP_TEXT_LINES, &cgrp.bio );
	cgrp.bioTime = cg.time ? cg.time : 1;
}

// rpmsg "<text>": for the menu's status line (the server printed it)
static void CG_RP_MessageCmd( void ) {
	char text[MAX_STRING_CHARS];

	trap->Cmd_Argv( 1, text, sizeof( text ) );
	trap->Cvar_Set( "ui_rpMessage", text );
	CG_RP_Show( text, qfalse );
}

// rpadmin <RPA_* bits>: whether we're an admin here, and our cheats on or off
static void CG_RP_AdminCmd( void ) {
	cgrp.admin = atoi( CG_Argv( 1 ) ) & ( RPA_ADMIN | RPA_GOD | RPA_NOCLIP | RPA_NOTARGET | RPA_SIDES | RPA_WARS );
	if ( !( cgrp.admin & RPA_ADMIN ) ) {
		cgrp.admin = 0;
		CG_RP_UIInvadeClear();	// (an admin's alone: told again as we're one)
	}
	CG_RP_UIAdmin();
}

// rpinv <count> "<line>"...: how the invasions on go, for the Admin page's
// Invasion part (to an admin: g_vrinvade.c) - and so, that this server has them.
static void CG_RP_InvadeCmd( void ) {
	char lines[RP_INV_LINES][MAX_STRING_CHARS];
	const int count = Com_Clampi( 0, RP_INV_LINES, atoi( CG_Argv( 1 ) ) );
	int i;

	if ( !cgrp.admin ) {
		return;	// (an admin's: one sent as we stopped being one)
	}
	for ( i = 0; i < count; i++ ) {
		trap->Cmd_Argv( 2 + i, lines[i], sizeof( lines[i] ) );
	}
	CG_RP_UIInvade( count, lines, qtrue );
}

// rppassok: the new password's the one now
static void CG_RP_PasswordCmd( void ) {
	if ( cgrp.ask.kind == RPASK_PASSWORD && cgrp.ask.sent ) {
		const int n = CG_RP_ReadSaved(), i = CG_RP_FindSaved( n );
		if ( n > 0 && i >= 0 && !Q_stricmp( rpSaved[i].account, cgrp.ask.account ) ) {
			VRC_ToHex( cgrp.ask.key, VRRP_KEY_LEN, rpSaved[i].key );	// (what's kept logs in with the new one)
			CG_RP_WriteSaved( n );
		} else {
			memset( rpSaved, 0, sizeof( rpSaved ) );
		}
		CG_RP_AskOver();
	}
}

// rpfail <1: the name or the password was wrong | 0>
static void CG_RP_FailCmd( void ) {
	const qboolean wrong = (qboolean)( atoi( CG_Argv( 1 ) ) != 0 );

	cgrp.challengeUsed = qfalse;	// (a new one came first, or the one we had is still good)
	if ( cgrp.ask.kind != RPASK_NONE && cgrp.ask.sent ) {
		if ( wrong && cgrp.ask.kind == RPASK_AUTO ) {
			CG_RP_Forget();
			CG_RP_Show( "The login this PC kept for this server doesn't work any more: log in again", qtrue );
		}
		CG_RP_AskOver();
	}
}

// Whether s is a Force setup as the forcepowers cvar has one (as the server
// takes one: g_vrrp.c's G_RP_ForceOk) - "<rank>-<side>-" and a level for
// each power: a rank of 0 to 7, light, dark or Grey, levels of 0 to 3.
static qboolean CG_RP_ForceOk( const char *s ) {
	int i;

	if ( strlen( s ) != DEFAULT_FORCEPOWERS_LEN || s[0] < '0' || s[0] > '0' + FORCE_MASTERY_JEDI_MASTER || s[1] != '-' ||
		s[2] < '0' + FORCE_LIGHTSIDE || s[2] > '0' + FORCE_GREYSIDE || s[3] != '-' ) {
		return qfalse;
	}
	for ( i = 4; i < DEFAULT_FORCEPOWERS_LEN; i++ ) {
		if ( s[i] < '0' || s[i] > '0' + FORCE_LEVEL_3 ) {
			return qfalse;
		}
	}
	return qtrue;
}

// Your own Force setup back, where a character's was (put by in
// cg_rpOwnForce while you played it); and your userinfo says it's yours (rpf).
// Yours is Light or Dark, as ever (ui_rpForceMay).
static void CG_RP_ForceOwnBack( void ) {
	char own[MAX_CVAR_VALUE_STRING];

	trap->Cvar_VariableStringBuffer( "cg_rpOwnForce", own, sizeof( own ) );
	if ( own[0] ) {
		trap->Cvar_Set( "forcepowers", own );
		trap->Cvar_Set( "cg_rpOwnForce", "" );
	}
	trap->Cvar_Set( "rpf", "" );
	trap->Cvar_Set( "ui_rpForceMay", "0" );
}

// rpforce <number> <setup | -> <may>: the Force setup this game's to have -
// the character's you play (yours put by till you're yourself again), or "-":
// yours back. Your userinfo says which it has (rpf "<number> <yours>": the
// server takes what's in forcepowers then - set anew in the Force menu, say -
// as the character's; "": yours). And what the character may be, for the
// Force menu (ui_rpForceMay: BG_FORCE_*) - under the number this game has,
// only that: what's in forcepowers is the character's still, set since maybe.
static void CG_RP_ForceCmd( void ) {
	char number[32], setup[MAX_CVAR_VALUE_STRING], own[MAX_CVAR_VALUE_STRING], rpf[MAX_CVAR_VALUE_STRING];
	unsigned n;
	const int may = atoi( CG_Argv( 3 ) ) & ( BG_FORCE_GREY | BG_FORCE_MASTER );

	trap->Cmd_Argv( 1, number, sizeof( number ) );
	trap->Cmd_Argv( 2, setup, sizeof( setup ) );
	if ( !strcmp( setup, "-" ) ) {
		CG_RP_ForceOwnBack();
		return;
	}
	n = (unsigned)strtoul( number, NULL, 10 );
	if ( !n || !CG_RP_ForceOk( setup ) ) {
		return;
	}
	trap->Cvar_Set( "ui_rpForceMay", va( "%i", may ) );
	trap->Cvar_VariableStringBuffer( "rpf", rpf, sizeof( rpf ) );
	if ( rpf[0] && (unsigned)strtoul( rpf, NULL, 10 ) == n ) {
		return;	// (the setup this game has: only what it may be changed)
	}
	trap->Cvar_VariableStringBuffer( "cg_rpOwnForce", own, sizeof( own ) );
	if ( !own[0] ) {	// (yours, put by - not another character's)
		trap->Cvar_VariableStringBuffer( "forcepowers", own, sizeof( own ) );
		if ( !own[0] ) {
			Q_strncpyz( own, DEFAULT_FORCEPOWERS, sizeof( own ) );
		}
		trap->Cvar_Set( "cg_rpOwnForce", own );
	}
	trap->Cvar_Set( "forcepowers", setup );
	trap->Cvar_Set( "rpf", va( "%u %s", n, CG_RP_ForceOk( own ) ? own : "-" ) );
}

qboolean CG_RP_ServerCommand( const char *cmd ) {
	if ( !Q_stricmp( cmd, "rpch" ) ) {
		CG_RP_ChallengeCmd();
	} else if ( !Q_stricmp( cmd, "rpacct" ) ) {
		CG_RP_AccountCmd();
	} else if ( !Q_stricmp( cmd, "rpchar" ) ) {
		CG_RP_CharCmd();
	} else if ( !Q_stricmp( cmd, "rpcard" ) ) {
		CG_RP_CardCmd();
	} else if ( !Q_stricmp( cmd, "rpbio" ) ) {
		CG_RP_BioCmd();
	} else if ( !Q_stricmp( cmd, "rpmsg" ) ) {
		CG_RP_MessageCmd();
	} else if ( !Q_stricmp( cmd, "rppassok" ) ) {
		CG_RP_PasswordCmd();
	} else if ( !Q_stricmp( cmd, "rpfail" ) ) {
		CG_RP_FailCmd();
	} else if ( !Q_stricmp( cmd, "rpadmin" ) ) {
		CG_RP_AdminCmd();
	} else if ( !Q_stricmp( cmd, "rpinv" ) ) {
		CG_RP_InvadeCmd();	// (how the invasions go: the Admin page)
	} else if ( !Q_stricmp( cmd, "rpdn" ) ) {
		CG_RP_DownCmd();	// (who's down: cg_vrdown.c)
	} else if ( !Q_stricmp( cmd, "rpup" ) ) {
		CG_RP_UpCmd();
	} else if ( !Q_stricmp( cmd, "rpcmps" ) ) {
		CG_RP_CompsCmd();	// (your character's companions: cg_vrcomp.c)
	} else if ( !Q_stricmp( cmd, "rpcmp" ) ) {
		CG_RP_CompCmd();
	} else if ( !Q_stricmp( cmd, "rpforce" ) ) {
		CG_RP_ForceCmd();	// (your character's Force setup)
	} else {
		return qfalse;
	}
	return qtrue;
}

// chat "<line>" <speaker> <kind> "<bubble>": what goes over their head
void CG_RP_Chat( void ) {
	char text[MAX_SAY_TEXT * 2], plain[MAX_SAY_TEXT * 2];
	int speaker, kind;
	rpBubble_t *b;

	if ( trap->Cmd_Argc() < 5 ) {
		return;
	}
	speaker = atoi( CG_Argv( 2 ) );
	kind = atoi( CG_Argv( 3 ) );
	if ( speaker < 0 || speaker >= MAX_CLIENTS || ( kind != RPCHAT_TALK && kind != RPCHAT_ACTION ) ) {
		return;
	}
	trap->Cmd_Argv( 4, text, sizeof( text ) );
	CG_RP_Plain( text, plain, sizeof( plain ) );
	if ( !plain[0] ) {
		return;
	}
	b = &rpBubbles[speaker];
	b->kind = kind;
	CG_RP_Layout( plain, RP_BUBBLE_WIDTH, RP_BUBBLE_LINES, &b->text );
	b->time = cg.time ? cg.time : 1;
}

/*
================================================================================
Every frame
================================================================================
*/

static void CG_RP_Hello( void ) {
	char rpf[MAX_CVAR_VALUE_STRING];

	if ( !cgrp.server || cgrp.hello || cgrp.helloTries >= RP_HELLO_TRIES ) {
		return;
	}
	if ( !cgrp.firstSnap ) {
		cgrp.firstSnap = cg.time ? cg.time : 1;
	}
	if ( cg.time - cgrp.firstSnap < RP_HELLO_FIRST || ( cgrp.helloTime && cg.time - cgrp.helloTime < RP_HELLO_AGAIN ) ) {
		return;
	}
	// (with the number of the rpforce whose Force setup this game has - 0: yours - so the server tells it again what it hasn't)
	trap->Cvar_VariableStringBuffer( "rpf", rpf, sizeof( rpf ) );
	CG_RP_Send( va( "rpver %i %u", RP_PROTOCOL, (unsigned)strtoul( rpf, NULL, 10 ) ) );
	cgrp.helloTime = cg.time ? cg.time : 1;
	cgrp.helloTries++;
}

// The menu's People page is up: everyone's card asked for - one at a time, as
// ever; with flood protection, several in one request (a server before that
// answers the first: the rest are asked again).
static void CG_RP_People( void ) {
	const int from = cgrp.peopleCursor, most = cgrp.floodPeriod ? RP_PEOPLE_ASK : 1;
	char ask[MAX_STRING_CHARS];
	int i, n = 0;

	if ( !cgrp.on || cg.time - cgrp.peopleUntil >= 0 || cg.time - cgrp.peopleNext < 0 || !CG_RP_Room() ) {
		return;
	}
	Q_strncpyz( ask, "rpcard", sizeof( ask ) );
	for ( i = 0; i < MAX_CLIENTS && n < most; i++ ) {
		const int k = ( from + i ) % MAX_CLIENTS;
		if ( !cgs.clientinfo[k].infoValid || !CG_RP_CardDue( k ) ) {
			continue;
		}
		rpCards[k].asked = cg.time ? cg.time : 1;
		Q_strcat( ask, sizeof( ask ), va( " %i", k ) );
		cgrp.peopleCursor = k + 1;	// (the next request goes on from there)
		n++;
	}
	if ( n ) {
		CG_RP_Send( ask );
		cgrp.peopleNext = cg.time + ( cgrp.floodPeriod ? RP_PEOPLE_GAP : RP_PEOPLE_ONE );
	}
}

// Not logged in, roleplay on or off: what this PC kept for this server, once a
// visit (logged out by the server - the account logged in somewhere else -
// it isn't tried again: no taking turns with that other place); again if the
// server didn't answer (CG_RP_Frame). With room for it, after the hello.
static void CG_RP_AutoLogin( void ) {
	int n, i;

	if ( !cgrp.server || !cgrp.hello || !cgrp.known || cgrp.account[0] || cgrp.autoTried || cgrp.ask.kind != RPASK_NONE ||
		!CG_RP_Room() ) {
		return;
	}
	cgrp.autoTried = qtrue;
	cgrp.autoTries++;
	n = CG_RP_ReadSaved();
	i = CG_RP_FindSaved( n );
	if ( i >= 0 && VRC_FromHex( rpSaved[i].key, cgrp.ask.key, VRRP_KEY_LEN ) ) {
		cgrp.ask.kind = RPASK_AUTO;
		Q_strncpyz( cgrp.ask.account, rpSaved[i].account, sizeof( cgrp.ask.account ) );
		cgrp.ask.time = cg.time;
		CG_RP_AskSend();
	} else {
		memset( cgrp.ask.key, 0, sizeof( cgrp.ask.key ) );
	}
	memset( rpSaved, 0, sizeof( rpSaved ) );
}

void CG_RP_Frame( void ) {
	if ( !cg.snap || cg.demoPlayback ) {
		return;
	}
	CG_RP_Hello();
	if ( cgrp.ask.kind != RPASK_NONE && cg.time - cgrp.ask.time > ( cgrp.ask.kind == RPASK_AUTO ? RP_AUTO_WAIT : RP_ASK_WAIT ) ) {
		// no answer: given up; and hello again, for a challenge that's good for sure (a login
		// Remember me kept is tried again after it - unless its answer says it got there after all)
		if ( cgrp.ask.kind != RPASK_AUTO ) {
			CG_RP_Show( "The server didn't answer: try again", qtrue );
		} else if ( cgrp.autoTries < RP_AUTO_TRIES ) {
			cgrp.autoTried = qfalse;
		}
		CG_RP_AskOver();
		cgrp.hello = qfalse;
		cgrp.helloTries = 0;
		cgrp.helloTime = 0;
	}
	CG_RP_AutoLogin();
	CG_RP_Look();
	CG_RP_People();
	CG_RP_DownFrame();	// (down, getting someone up: cg_vrdown.c)
	CG_RP_CompFrame();	// (companions: cg_vrcomp.c)
}

/*
================================================================================
Drawing: bubbles, the card, a bio, a notice
================================================================================
*/

static float CG_RP_HudFov( void ) {
	char text[32];

	trap->Cvar_VariableStringBuffer( "vr_hudFov", text, sizeof( text ) );
	return text[0] ? Com_Clamp( 10.0f, 120.0f, (float)atof( text ) ) : 50.0f;
}

// Where a point in the world shows on the 640x480 screen (in the headset:
// on the HUD, a 4:3 plane straight ahead of the head); qfalse: behind.
static qboolean CG_RP_Project( const vec3_t point, float *x, float *y ) {
	vec3_t head, d;
	matrix3_t axis;

	if ( CG_VR_HeadPose( head, axis ) ) {
		const float th = tanf( DEG2RAD( CG_RP_HudFov() * 0.5f ) ), tw = th * ( 4.0f / 3.0f );
		float f;
		VectorSubtract( point, head, d );
		f = DotProduct( d, axis[0] );
		if ( f < 1.0f ) {
			return qfalse;
		}
		*x = 320.0f - DotProduct( d, axis[1] ) / f / tw * 320.0f;
		*y = 240.0f - DotProduct( d, axis[2] ) / f / th * 240.0f;
		return qtrue;
	}
	VectorCopy( point, d );
	return CG_WorldCoordToScreenCoordFloat( d, x, y );
}

static void CG_RP_Eye( vec3_t eye ) {
	matrix3_t axis;

	if ( !CG_VR_HeadPose( eye, axis ) ) {
		VectorCopy( cg.refdef.vieworg, eye );
	}
}

// Over the speaker's head (the top of their box, as it's sent).
static qboolean CG_RP_BubblePoint( int n, vec3_t point ) {
	const centity_t *cent = &cg_entities[n];
	float top = DEFAULT_MAXS_2;

	if ( n == cg.snap->ps.clientNum ) {
		if ( !cg.renderingThirdPerson ) {
			return qfalse;	// (your own, you don't see from inside)
		}
		VectorCopy( cg.predictedPlayerState.origin, point );
		if ( cg.predictedPlayerState.pm_flags & PMF_DUCKED ) {
			top = CROUCH_MAXS_2;
		}
	} else {
		if ( !cent->currentValid || cent->currentState.eType != ET_PLAYER ) {
			return qfalse;
		}
		VectorCopy( cent->lerpOrigin, point );
		if ( cent->currentState.solid && cent->currentState.solid != SOLID_BMODEL ) {
			top = (float)( ( ( cent->currentState.solid >> 16 ) & 255 ) - 32 );
		}
	}
	point[2] += top + RP_BUBBLE_ABOVE;
	return qtrue;
}

static void CG_RP_DrawLines( const rpText_t *t, float cx, float y, float scale, float lineH, vec4_t color ) {
	int i;

	for ( i = 0; i < t->numLines; i++ ) {
		const float w = (float)CG_Text_Width( t->line[i], scale, RP_FONT );
		CG_Text_Paint( cx - w * 0.5f, y + i * lineH, scale, color, t->line[i], 0, 0, ITEM_TEXTSTYLE_SHADOWED, RP_FONT );
	}
}

static void CG_RP_DrawBubble( int n, float dist ) {
	const rpBubble_t *b = &rpBubbles[n];
	const int age = cg.time - b->time;
	const float scale = 0.55f - 0.2f * Com_Clamp( 0.0f, 1.0f, ( dist - 100.0f ) / 400.0f );
	const float pad = 10.0f * scale;
	vec4_t back = { 0.04f, 0.04f, 0.06f, 0.65f }, edge = { 0.85f, 0.85f, 0.9f, 0.5f };
	vec4_t text = { 1.0f, 1.0f, 1.0f, 1.0f };
	vec3_t point;
	float x, y, w, h, lineH, alpha;

	if ( !CG_RP_BubblePoint( n, point ) || !CG_RP_Project( point, &x, &y ) ) {
		return;
	}
	lineH = CG_Text_Height( "A", scale, RP_FONT ) * 1.1f;
	w = b->text.width * scale + pad * 2.0f;
	h = b->text.numLines * lineH + pad * 2.0f;
	if ( x + w * 0.5f < 0.0f || x - w * 0.5f > SCREEN_WIDTH || y < 0.0f || y - h > SCREEN_HEIGHT ) {
		return;	// (off the screen)
	}
	alpha = ( age > RP_BUBBLE_TIME - RP_BUBBLE_FADE ) ? (float)( RP_BUBBLE_TIME - age ) / RP_BUBBLE_FADE : 1.0f;
	back[3] *= alpha;
	edge[3] *= alpha;
	text[3] = alpha;
	if ( b->kind == RPCHAT_ACTION ) {
		VectorSet( text, 1.0f, 0.85f, 0.4f );	// (an action, as its chat line)
	}
	CG_AnchorAt( x );
	CG_FillRect( x - w * 0.5f, y - h, w, h, back );
	CG_DrawRect( x - w * 0.5f, y - h, w, h, 1.0f, edge );
	CG_RP_DrawLines( &b->text, x, y - h + pad, scale, lineH, text );
}

static void CG_RP_DrawBubbles( void ) {
	int order[MAX_CLIENTS], count = 0, i, j;
	float dist[MAX_CLIENTS];
	vec3_t eye;

	if ( !cg_rpBubbles.integer ) {
		return;
	}
	CG_RP_Eye( eye );
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		const rpBubble_t *b = &rpBubbles[i];
		vec3_t point, head;
		trace_t tr;
		if ( !b->time || cg.time - b->time >= RP_BUBBLE_TIME || cg.time < b->time || !CG_RP_BubblePoint( i, point ) ) {
			continue;
		}
		dist[i] = Distance( eye, point );
		if ( dist[i] > RP_BUBBLE_RANGE ) {
			continue;
		}
		VectorCopy( point, head );
		head[2] -= RP_BUBBLE_ABOVE + 8.0f;	// (their head: a low ceiling over it doesn't hide them)
		CG_Trace( &tr, eye, NULL, NULL, head, cg.snap->ps.clientNum, CONTENTS_SOLID );
		if ( tr.fraction < 1.0f ) {
			continue;	// (behind a wall)
		}
		// furthest first, so the nearer ones are drawn over them
		for ( j = count; j > 0 && dist[order[j - 1]] < dist[i]; j-- ) {
			order[j] = order[j - 1];
		}
		order[j] = i;
		count++;
	}
	for ( i = 0; i < count; i++ ) {
		CG_RP_DrawBubble( order[i], dist[order[i]] );
	}
}

static void CG_RP_DrawCard( void ) {
	const int t = cgrp.target;
	const rpCard_t *c;
	const char *name, *faction;
	const float nameScale = 0.7f, scale = 0.5f, pad = 8.0f, top = 300.0f;
	vec4_t back = { 0.04f, 0.04f, 0.06f, 0.7f }, edge = { 0.85f, 0.85f, 0.9f, 0.6f };
	vec4_t white = { 1.0f, 1.0f, 1.0f, 1.0f }, grey = { 0.75f, 0.75f, 0.8f, 1.0f };
	float w, h, nameH, lineH, y;
	qboolean bio;

	if ( t < 0 || cg.time - cgrp.since < RP_CARD_DWELL || !CG_RP_CardsWanted() || !( c = CG_RP_Card( t ) ) ) {
		return;
	}
	bio = (qboolean)( cgrp.bioWho == t && cgrp.bioTime && cg.time - cgrp.bioTime < RP_BIO_TIME );
	name = ( t < MAX_CLIENTS ) ? cgs.clientinfo[t].name : c->name;
	faction = rpFactions[c->faction];
	nameH = CG_Text_Height( "A", nameScale, RP_FONT ) * 1.1f;
	lineH = CG_Text_Height( "A", scale, RP_FONT ) * 1.1f;
	w = (float)CG_Text_Width( name, nameScale, RP_FONT );
	w = Q_max( w, (float)CG_Text_Width( faction, scale, RP_FONT ) );
	w = Q_max( w, c->desc.width * scale );
	if ( bio ) {
		w = Q_max( w, cgrp.bio.width * scale );
	}
	w = Q_max( w, 160.0f ) + pad * 2.0f;
	h = pad * 2.0f + nameH + lineH + c->desc.numLines * lineH + ( bio ? lineH * 0.5f + cgrp.bio.numLines * lineH : 0.0f );

	CG_Anchor2D( ANCHOR2D_MIDDLE );
	CG_FillRect( 320.0f - w * 0.5f, top, w, h, back );
	CG_DrawRect( 320.0f - w * 0.5f, top, w, h, 1.0f, edge );
	y = top + pad;
	CG_Text_Paint( 320.0f - CG_Text_Width( name, nameScale, RP_FONT ) * 0.5f, y, nameScale, white, name, 0, 0, ITEM_TEXTSTYLE_SHADOWED, RP_FONT );
	y += nameH;
	CG_Text_Paint( 320.0f - CG_Text_Width( faction, scale, RP_FONT ) * 0.5f, y, scale, grey, faction, 0, 0, ITEM_TEXTSTYLE_SHADOWED, RP_FONT );
	y += lineH;
	CG_RP_DrawLines( &c->desc, 320.0f, y, scale, lineH, white );
	y += c->desc.numLines * lineH;
	if ( bio ) {
		y += lineH * 0.5f;
		CG_RP_DrawLines( &cgrp.bio, 320.0f, y, scale, lineH, grey );
	}
	if ( t >= MAX_CLIENTS ) {
		// a companion's name over its head too (the top of its box, as it's sent)
		const centity_t *cent = &cg_entities[t];
		vec3_t point;
		float x, hy, head = DEFAULT_MAXS_2;
		if ( cent->currentState.solid && cent->currentState.solid != SOLID_BMODEL ) {
			head = (float)( ( ( cent->currentState.solid >> 16 ) & 255 ) - 32 );
		}
		VectorCopy( cent->lerpOrigin, point );
		point[2] += head + RP_BUBBLE_ABOVE;
		if ( CG_RP_Project( point, &x, &hy ) && x > 0.0f && x < SCREEN_WIDTH && hy > 0.0f && hy < SCREEN_HEIGHT ) {
			const float tw = (float)CG_Text_Width( name, scale, RP_FONT );
			CG_AnchorAt( x );
			CG_Text_Paint( x - tw * 0.5f, hy - CG_Text_Height( name, scale, RP_FONT ), scale, white, name, 0, 0, ITEM_TEXTSTYLE_SHADOWED, RP_FONT );
		}
	}
}

static void CG_RP_DrawNotice( void ) {
	const int age = cg.time - cgrp.noticeTime;
	const float scale = 0.55f, lineH = CG_Text_Height( "A", scale, RP_FONT ) * 1.1f;
	vec4_t color = { 1.0f, 1.0f, 1.0f, 1.0f };

	if ( !cgrp.noticeTime || age < 0 || age >= RP_NOTICE_TIME ) {
		return;
	}
	if ( age > RP_NOTICE_TIME - 1000 ) {
		color[3] = (float)( RP_NOTICE_TIME - age ) / 1000.0f;
	}
	CG_Anchor2D( ANCHOR2D_MIDDLE );
	CG_RP_DrawLines( &cgrp.notice, 320.0f, 96.0f, scale, lineH, color );
}

void CG_RP_Draw( void ) {
	const float was = CG_Anchored();

	if ( !cg.snap || cg.levelShot || !cg_draw2D.integer ) {
		return;
	}
	if ( cg.snap->ps.pm_type != PM_INTERMISSION && !cg.scoreBoardShowing ) {
		if ( cgrp.on ) {
			CG_RP_DrawBubbles();
		}
		CG_RP_DrawCard();
		CG_RP_DrawDown();	// (down, getting someone up: cg_vrdown.c)
	}
	CG_RP_DrawNotice();
	CG_Anchor2D( was );
}

static void CG_RP_Show( const char *text, qboolean print ) {
	if ( print ) {
		trap->Print( "%s\n", text );
	}
	CG_RP_Layout( text, RP_NOTICE_WIDTH, 3, &cgrp.notice );
	cgrp.noticeTime = cg.time ? cg.time : 1;
}

// A line for the player a moment, and in the console (cg_vrdown.c's).
void CG_RP_Notice( const char *text ) {
	CG_RP_Show( text, qtrue );
}

// The faction of the character you play (-1: none, or roleplay's off).
int CG_RP_MyFaction( void ) {
	if ( !cgrp.on || cgrp.active < 0 || cgrp.active >= cgrp.numChars ) {	// (no account: none played - rpacct)
		return -1;
	}
	return cgrp.chars[cgrp.active].faction;	// (in range: rpchar)
}

/*
================================================================================
Starting and stopping
================================================================================
*/

// The serverinfo: roleplay here, on or off.
void CG_RP_ServerInfo( const char *info ) {
	const char *value = Info_ValueForKey( info, "vrRP" );
	const qboolean server = (qboolean)( value[0] != '\0' );
	const qboolean on = (qboolean)( server && atoi( value ) == 1 );

	if ( !on && cgrp.on ) {
		memset( rpBubbles, 0, sizeof( rpBubbles ) );
	}
	cgrp.server = server;
	// working: on, or its settings said ("<on> <characters> <talk range> <downed> <companions>
	// <companions on the server> <their models>"); a lone "0" is a server whose roleplay isn't (it
	// answers nothing - or a first build's, off: no logging in then)
	cgrp.working = (qboolean)( on || strchr( value, ' ' ) != NULL );
	cgrp.on = on;
	{
		int fields[7] = { 0, 0, 0, 0, 0, 0, 0 };
		const int n = sscanf( value, "%d %d %d %d %d %d %d", &fields[0], &fields[1], &fields[2], &fields[3], &fields[4], &fields[5], &fields[6] );
		cgrp.compsAllowed = ( n >= 5 ) ? Com_Clampi( 0, RP_COMP_OUT_MAX, fields[4] ) : 0;	// (a server before companions: none)
	}
	{	// its flood protection's period (sv_floodProtect: 1 a second, more: that many ms; CG_RP_Room) - none on
		// this PC's own server; not there, as on (a serverinfo too long goes without some of its keys)
		const char *value = Info_ValueForKey( info, "sv_floodProtect" );
		const int flood = value[0] ? atoi( value ) : 1;
		char running[16];
		trap->Cvar_VariableStringBuffer( "sv_running", running, sizeof( running ) );
		cgrp.floodPeriod = ( flood <= 0 || atoi( running ) ) ? 0 : ( flood == 1 ) ? 1000 : Q_min( flood, RP_FLOOD_MAX );
		CG_RP_FloodCap();	// (a period cut: less owed)
	}
	CG_RP_UIServer();
}

// map_restart: the server's game started over, and forgot this client
// runs this build; hello again.
void CG_RP_Restart( void ) {
	cgrp.hello = qfalse;
	cgrp.helloTries = 0;
	cgrp.helloTime = 0;
	cgrp.firstSnap = 0;
	cgrp.challenge[0] = '\0';
	cgrp.challengeUsed = qfalse;
	CG_RP_AskOver();
	memset( rpBubbles, 0, sizeof( rpBubbles ) );
	memset( rpCards, 0, sizeof( rpCards ) );
	CG_RP_DownInit();	// (everyone up again; the server says who's down when we hello)
	cgrp.admin = 0;		// (and whether we're an admin)
	CG_RP_UIAdmin();
	CG_RP_UIInvadeClear();	// (and how invasions go: an admin's told)
	CG_RP_CompInit();	// (and our companions)
}

// The menus' cvars of a server's (its people, the editor's fields, the
// last word, what was typed for a password), emptied.
static void CG_RP_UIClear( void ) {
	static const char *cvars[] = { "ui_rpEditName", "ui_rpEditFaction", "ui_rpEditDesc", "ui_rpEditBio", "ui_rpBioRead", "ui_rpBioName",
		"ui_rpMessage", "ui_rpLoginPassword", "ui_rpLoginAgain", "ui_rpPassword", "ui_rpNewPassword", "ui_rpNewAgain" };
	int i;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		trap->Cvar_Set( va( "ui_rpWhoF%i", i ), "" );
		trap->Cvar_Set( va( "ui_rpWhoD%i", i ), "" );
		trap->Cvar_Set( va( "ui_rpWhoR%i", i ), "" );
	}
	for ( i = 0; i < (int)ARRAY_LEN( cvars ); i++ ) {
		trap->Cvar_Set( cvars[i], "" );
	}
}

void CG_RP_Init( void ) {
	char *p;

	memset( &cgrp, 0, sizeof( cgrp ) );
	memset( rpBubbles, 0, sizeof( rpBubbles ) );
	memset( rpCards, 0, sizeof( rpCards ) );
	cgrp.active = -1;
	cgrp.target = -1;
	cgrp.bioWho = -1;
	cgrp.menuBio = -1;
	CG_RP_DownInit();
	CG_RP_CompInit();
	CG_RP_UIClear();	// (another server's)
	trap->Cvar_Register( NULL, "rpf", "", CVAR_USERINFO | CVAR_TEMP );	// (the Force setup you have: yours, or a character's - CG_RP_ForceCmd)
	CG_RP_ForceOwnBack();	// (your own Force setup, wherever this is: a server playing a character says so - rpforce)
	trap->Cvar_VariableStringBuffer( "cl_currentServerIP", cgrp.address, sizeof( cgrp.address ) );
	if ( !cgrp.address[0] ) {
		trap->Cvar_VariableStringBuffer( "cl_currentServerAddress", cgrp.address, sizeof( cgrp.address ) );
	}
	for ( p = cgrp.address; *p; p++ ) {
		if ( *p == '"' || *p == ' ' || (unsigned char)*p < 32 ) {
			*p = '_';
		}
	}
	Q_strlwr( cgrp.address );
	CG_RP_UIServer();
	CG_RP_UIAccount();
	CG_RP_UIAdmin();
	CG_RP_UIInvadeClear();
}

void CG_RP_Shutdown( void ) {
	CG_RP_AskOver();
	memset( cgrp.salt, 0, sizeof( cgrp.salt ) );
	memset( cgrp.challenge, 0, sizeof( cgrp.challenge ) );
	cgrp.server = cgrp.working = cgrp.on = qfalse;
	cgrp.account[0] = '\0';
	cgrp.numChars = cgrp.most = 0;
	cgrp.active = -1;
	cgrp.admin = 0;
	cgrp.compsAllowed = 0;
	CG_RP_UIServer();
	CG_RP_UIAccount();
	CG_RP_UIAdmin();
	CG_RP_UIInvadeClear();
	CG_RP_UIClear();
	CG_RP_CompInit();
	CG_RP_ForceOwnBack();	// (yours again, out of the game too)
}

// For the wheel: roleplay's on here.
qboolean CG_RP_On( void ) {
	return cgrp.on;
}

// For the wheel: how many companions a player may have out here (0: none).
int CG_RP_CompsAllowed( void ) {
	return cgrp.on ? cgrp.compsAllowed : 0;
}

// For the wheel: why the Roleplay menu's of no use here (NULL: it is - roleplay
// on or off: off, it still logs you in - an admin's cheats work then too - and
// its owner turns it on there).
const char *CG_RP_MenuWhy( void ) {
	if ( !cgrp.server ) {
		return "This server doesn't have roleplay";
	}
	if ( !cgrp.working ) {
		return "Roleplay isn't working on this server";
	}
	return NULL;
}

// The console commands this has.
static const struct {
	const char	*name;
	void		(*func)( void );
} rpCommands[] = {
	{ "bio",			CG_RP_Bio_f },
	{ "changepassword",	CG_RP_ChangePassword_f },
	{ "login",			CG_RP_Login_f },
	{ "logout",			CG_RP_Logout_f },
	{ "new",			CG_RP_New_f },
	{ "rpui",			CG_RP_UI_f },
};

qboolean CG_RP_ConsoleCommand( const char *cmd ) {
	int i;

	for ( i = 0; i < (int)ARRAY_LEN( rpCommands ); i++ ) {
		if ( !Q_stricmp( cmd, rpCommands[i].name ) ) {
			if ( !cgrp.server && Q_stricmp( cmd, "rpui" ) ) {
				return qfalse;	// (a server without roleplay may have its own: /login on it is its own)
			}
			rpCommands[i].func();
			return qtrue;
		}
	}
	return qfalse;
}

void CG_RP_AddCommands( void ) {
	int i;

	for ( i = 0; i < (int)ARRAY_LEN( rpCommands ); i++ ) {
		trap->AddCommand( rpCommands[i].name );
	}
}
