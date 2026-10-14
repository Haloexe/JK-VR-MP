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
with the server's address and salt, and logs in with it there whenever
roleplay is on; /logout forgets it, and so does the server saying it's
wrong.

THE MENUS read where things stand in ui_rp* cvars, and ask for things
with "rpui <what>" (below).

SPEECH BUBBLES. With roleplay on, what someone near you says, and their
/me, shows over their head for 6 seconds (cg_rpBubbles).

CARDS. Look at someone playing a character, within 4 m, for half a
second with your own weapon away (the saber off, or fists), and their
card shows: name, faction, description (cg_rpCards). Their bio: Read
their bio on the wheel's Roleplay ring, or /bio.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "cg_local.h"
#include "ui/menudef.h"
#include "game/bg_vrcrypt.h"

qboolean CG_WorldCoordToScreenCoordFloat( vec3_t worldCoord, float *x, float *y );	// (cg_draw.c)

#define RP_PROTOCOL			2		// (as the server's, g_vrrp.c)
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
#define RP_ASK_WAIT			15000	// a request with no answer this long is given up

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
#define RP_PEOPLE_GAP		170		// ...one at a time, this far apart (the server takes 10 a second)
#define RP_NOTICE_WIDTH		900.0f
#define RP_NOTICE_TIME		5000

#define RP_TEXT_LINES		10
#define RP_LINE_MAX			128
#define RP_FONT				FONT_MEDIUM

enum { RPF_HEROES, RPF_VILLAINS, RPF_NEUTRAL, RPF_NUM };
static const char *rpFactions[RPF_NUM] = { "Heroes", "Villains", "Neutral" };

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
	qboolean	on;				// ...and it's on
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
	qboolean	autoTried;		// logged in with what this PC kept (since roleplay came on)

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

/*
================================================================================
Small things
================================================================================
*/

// A line for the player, a moment on the screen (and in the console: print).
static void CG_RP_Show( const char *text, qboolean print );

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
	memset( arg, 0, sizeof( arg ) );
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

static void CG_RP_UIServer( void ) {
	trap->Cvar_Set( "ui_rpServer", !cgrp.server ? "0" : cgrp.on ? "2" : "1" );
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
		trap->SendClientCommand( va( "rpnew %s %s", cgrp.ask.account, hex ) );
		break;
	case RPASK_LOGIN:
	case RPASK_AUTO:
		VRRP_LoginProof( cgrp.ask.key, "login", cgrp.challenge, cgrp.ask.account, proof );
		VRC_ToHex( proof, sizeof( proof ), hex );
		trap->SendClientCommand( va( "rplogin %s %s", cgrp.ask.account, hex ) );
		cgrp.challengeUsed = qtrue;
		break;
	case RPASK_PASSWORD:
		VRC_ToHex( cgrp.ask.key, VRRP_KEY_LEN, hex2 );
		VRRP_LoginProof( cgrp.ask.oldKey, va( "password %s", hex2 ), cgrp.challenge, cgrp.ask.account, proof );	// (the new key's part of it)
		VRC_ToHex( proof, sizeof( proof ), hex );
		trap->SendClientCommand( va( "rppass %s %s", hex, hex2 ) );
		cgrp.challengeUsed = qtrue;
		break;
	}
	memset( proof, 0, sizeof( proof ) );
	memset( hex, 0, sizeof( hex ) );
	memset( hex2, 0, sizeof( hex2 ) );
	cgrp.ask.sent = qtrue;
	cgrp.ask.time = cg.time;
}

// Whether something can be asked now (if not, the player's told why).
static qboolean CG_RP_Ready( void ) {
	if ( !cgrp.server ) {
		CG_RP_Show( "This server doesn't have roleplay", qtrue );
	} else if ( !cgrp.on ) {
		CG_RP_Show( "Roleplay is off on this server right now", qtrue );
	} else if ( !cgrp.hello ) {
		CG_RP_Show( "Still getting in touch with the server: try again in a moment", qtrue );
	} else if ( cgrp.ask.kind != RPASK_NONE ) {
		CG_RP_Show( "Wait a moment: the server hasn't answered yet", qtrue );
	} else {
		return qtrue;
	}
	return qfalse;
}

// A password's copy wiped (so the compiler can't leave that out, as it may
// a memset of what's not read again).
static void CG_RP_Wipe( char *p, int size ) {
	volatile char *v = p;

	while ( size-- > 0 ) {
		*v++ = '\0';
	}
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
	trap->SendClientCommand( "rplogout" );
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

// What a card shows of client n, if there's one to show (NULL: none).
static const rpCard_t *CG_RP_Card( int n ) {
	const rpCard_t *c;

	if ( n < 0 || n >= MAX_CLIENTS || !cgs.clientinfo[n].infoValid ) {
		return NULL;
	}
	c = &rpCards[n];
	return ( c->time && c->faction >= 0 && !strcmp( c->name, cgs.clientinfo[n].name ) ) ? c : NULL;
}

static void CG_RP_AskCard( int n ) {
	rpCard_t *c = &rpCards[n];

	if ( c->time && cg.time - c->time < RP_CARD_KEEP && !strcmp( c->name, cgs.clientinfo[n].name ) ) {
		return;
	}
	if ( c->asked && cg.time - c->asked < RP_CARD_ASK_GAP ) {
		return;
	}
	c->asked = cg.time ? cg.time : 1;
	trap->SendClientCommand( va( "rpcard %i", n ) );
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

// Read their bio (the wheel can, while a card shows).
qboolean CG_RP_CanReadBio( void ) {
	return (qboolean)( CG_RP_CardsWanted() && cgrp.target >= 0 && cg.time - cgrp.since >= RP_CARD_DWELL && CG_RP_Card( cgrp.target ) );
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
	trap->SendClientCommand( va( "rpbio %i", cgrp.target ) );
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
The passwords' cvars are emptied as they're read.
*/
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
			trap->SendClientCommand( "rp play none" );
		} else if ( n < cgrp.numChars ) {
			trap->SendClientCommand( va( "rp play %i", n + 1 ) );
		}
	} else if ( !Q_stricmp( what, "delete" ) ) {
		const char *arg = CG_Argv( 2 );
		const int n = atoi( arg );
		if ( arg[0] && n >= 0 && n < cgrp.numChars ) {
			trap->SendClientCommand( va( "rp delete %i", n + 1 ) );
		}
	} else if ( !Q_stricmp( what, "newchar" ) ) {
		char name[MAX_CVAR_VALUE_STRING];
		CG_RP_Take( "ui_rpNewName", name, sizeof( name ) );
		if ( name[0] ) {
			trap->SendClientCommand( va( "rp new %s", name ) );
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
				trap->SendClientCommand( va( "rp faction %s", rpFactions[faction] ) );
			}
			trap->Cvar_VariableStringBuffer( "ui_rpEditDesc", text, sizeof( text ) );
			if ( strcmp( text, ch->desc ) ) {
				trap->SendClientCommand( va( "rp desc %s", text ) );
			}
			trap->Cvar_VariableStringBuffer( "ui_rpEditBio", text, sizeof( text ) );
			if ( strcmp( text, ch->bio ) ) {
				trap->SendClientCommand( va( "rp bio %s", text ) );
			}
		}
	} else if ( !Q_stricmp( what, "bio" ) ) {
		const char *arg = CG_Argv( 2 );
		const int n = atoi( arg );
		if ( !arg[0] ) {
			CG_RP_ReadBio();	// (whom you look at)
		} else if ( n >= 0 && n < MAX_CLIENTS && cgs.clientinfo[n].infoValid ) {
			cgrp.menuBio = n;	// (someone in the menu's list)
			trap->SendClientCommand( va( "rpbio %i", n ) );
		}
	} else if ( !Q_stricmp( what, "people" ) ) {
		cgrp.peopleUntil = cg.time + RP_PEOPLE_TIME;	// (everyone's card, one at a time: CG_RP_People)
	}
}

/*
================================================================================
From the server
================================================================================
*/

// rpch <salt> <challenge>
static void CG_RP_ChallengeCmd( void ) {
	char salt[64], challenge[64];
	byte check[VRRP_SALT_HEX / 2];

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
		memset( cgrp.chars, 0, sizeof( cgrp.chars ) );	// (another account's come)
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

// rpcard <client> <faction|-1> "<desc>"
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
	if ( !cgrp.server || cgrp.hello || cgrp.helloTries >= RP_HELLO_TRIES ) {
		return;
	}
	if ( !cgrp.firstSnap ) {
		cgrp.firstSnap = cg.time ? cg.time : 1;
	}
	if ( cg.time - cgrp.firstSnap < RP_HELLO_FIRST || ( cgrp.helloTime && cg.time - cgrp.helloTime < RP_HELLO_AGAIN ) ) {
		return;
	}
	trap->SendClientCommand( va( "rpver %i", RP_PROTOCOL ) );
	cgrp.helloTime = cg.time ? cg.time : 1;
	cgrp.helloTries++;
}

// The menu's People page is up: everyone's card asked for, one at a time.
static void CG_RP_People( void ) {
	int i;

	if ( !cgrp.on || cg.time - cgrp.peopleUntil >= 0 || cg.time - cgrp.peopleNext < 0 ) {
		return;
	}
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		const int n = ( cgrp.peopleCursor + i ) % MAX_CLIENTS;
		const rpCard_t *c = &rpCards[n];
		if ( !cgs.clientinfo[n].infoValid ) {
			continue;
		}
		if ( c->time && cg.time - c->time < RP_CARD_KEEP && !strcmp( c->name, cgs.clientinfo[n].name ) ) {
			continue;	// (known lately)
		}
		if ( c->asked && cg.time - c->asked < RP_CARD_ASK_GAP ) {
			continue;
		}
		CG_RP_AskCard( n );
		cgrp.peopleCursor = n + 1;
		cgrp.peopleNext = cg.time + RP_PEOPLE_GAP;
		return;
	}
}

// With roleplay on and not logged in: what this PC kept for this server.
static void CG_RP_AutoLogin( void ) {
	int n, i;

	if ( !cgrp.server || !cgrp.on || !cgrp.hello || !cgrp.known || cgrp.account[0] || cgrp.autoTried || cgrp.ask.kind != RPASK_NONE ) {
		return;
	}
	cgrp.autoTried = qtrue;
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
	if ( cgrp.ask.kind != RPASK_NONE && cg.time - cgrp.ask.time > RP_ASK_WAIT ) {
		// no answer: given up; and hello again, for a challenge that's good for sure
		if ( cgrp.ask.kind != RPASK_AUTO ) {
			CG_RP_Show( "The server didn't answer: try again", qtrue );
		}
		CG_RP_AskOver();
		cgrp.hello = qfalse;
		cgrp.helloTries = 0;
		cgrp.helloTime = 0;
	}
	CG_RP_AutoLogin();
	CG_RP_Look();
	CG_RP_People();
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
	name = cgs.clientinfo[t].name;
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

	if ( !on ) {
		cgrp.autoTried = qfalse;	// (when it's on again, log in again)
		if ( cgrp.on ) {
			memset( rpBubbles, 0, sizeof( rpBubbles ) );
		}
	}
	cgrp.server = server;
	cgrp.on = on;
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
	CG_RP_UIClear();	// (another server's)
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
}

void CG_RP_Shutdown( void ) {
	CG_RP_AskOver();
	memset( cgrp.salt, 0, sizeof( cgrp.salt ) );
	memset( cgrp.challenge, 0, sizeof( cgrp.challenge ) );
	cgrp.server = cgrp.on = qfalse;
	cgrp.account[0] = '\0';
	cgrp.numChars = cgrp.most = 0;
	cgrp.active = -1;
	CG_RP_UIServer();
	CG_RP_UIAccount();
	CG_RP_UIClear();
}

// For the wheel: roleplay's on here.
qboolean CG_RP_On( void ) {
	return cgrp.on;
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
