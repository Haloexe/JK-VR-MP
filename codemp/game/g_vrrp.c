/*
===========================================================================
OpenJK VR (multiplayer) - roleplay mode, the server's side: the switch,
accounts and logins, characters and factions, and roleplay chat.

THE SWITCH. g_rp 1 turns roleplay on and 0 off, at any time, with no map
change; the serverinfo "vrRP" says which (and, there at all, that this
server knows roleplay): "<on> <g_rpCharacters> <g_rpTalkRange>
<g_rpDowned> <g_rpCompanions> <g_rpCompanionsMax> <g_rpCompanionModels>"
("0": not ready), games reading the first (atoi, older builds too), the
Roleplay menu's Server page the rest (ui_vrrp.c). Off, the game plays as
before; nothing is lost.

ACCOUNTS. A player makes one with /new <name> <password> and logs in with
/login <name> <password>. Their game (cg_vrrp.c) turns the password into a
key (bg_vrcrypt.c: PBKDF2 over this server's salt and the account name),
and logs in by answering a one-time challenge with it, so the password
never leaves their PC and what crosses the network can't be replayed. The
server keeps only the key, in rp/accounts/<name>.txt with the account's
characters. Five wrong passwords, and the account (and the connection)
wait a minute. A forgotten password: the owner sets a temporary one by
rcon (rp password <account> <temporary>). Logging in (and making an
account) works with roleplay off too, where it changes nothing else: an
admin has their cheats either way.

ADMINS. The owner makes an account an admin by rcon - rp admin <account>
on|off, kept in its file; rp admins lists them. Only the server's console
does (rcon: its password; a listen server's host: their own console): a
player's own rp admin is turned away and logged. Logged in to it, a player
can use every cheat command with cheats off (G_RPCheatAllowed, asked by
ClientCommand), each one logged; nobody, admin or not, uses one while down.
Their game is told what its Admin page shows (rpadmin); nobody else is
told who's an admin. No longer one (logged out, the account logged in
somewhere else, rp admin off), their god mode, no-clip and no target end,
unless cheats are on for everyone.

CHARACTERS. Up to g_rpCharacters per account: a name (unique on the
server), a faction (Heroes, Villains, Neutral), and, optional, a one-line
description and a bio. While roleplay is on, the player is shown as the
character they play, looking as it does: what they wear while playing it
(model, saber and skin colours) is kept with it. Switching characters
respawns them, and waits until they're out of a duel and unhurt for 10 s
(and not carrying a flag, in a vehicle or, in Siege and the duel game
types, in the round), at most every 10 s.

DOWN, NOT DEAD (g_rpDowned): running out of health downs a player playing a
character until someone of their faction gets them up or they give up; and
NPCs take sides by faction - g_vrdown.c.

COMPANIONS: up to RP_COMP_MAX NPCs a character keeps with it - a name,
the NPC type it fights as, a look, its colours, whether it comes out (up
to RP_COMP_OUT_MAX do) - made on the Companions page (rpcomp), and out
with the character while roleplay is on - g_vrcomp.c.

CHAT, with roleplay on: talk (the chat key) reaches those within
g_rpTalkRange (510 units, 15 m), /me actions too; the team chat key or
/ooc goes to everyone, out of character, under the player's own name;
tell is as it was. Our clients get the speaker and the kind of line with
each, for speech bubbles; other clients see plain chat.

Client commands (most sent by the cgame or the menus, not typed):
  rpver <protocol>          this client runs this build (cg_vrrp.c); 4 and
                            later: it takes rpadmin; 5: rpcmps, rpcmp
  rpnew <account> <key>     make an account (key: 64 hex digits)
  rplogin <account> <proof> log in (proof: HMAC of the challenge)
  rppass <proof> <newkey>   change the password
  rplogout
  rp new|play|faction|desc|bio|delete|list|look|giveup ...
  rpcard <client>, rpbio <client>    what a character's card shows
  rpcomp list|set|del|out|order ...  companions (G_RP_Comp; 5 and later)
  me <action>, ooc <text>
To clients: rpch <salt> <challenge>, rpacct <account|-> <active> <count>
<most> <1: this answers a login or a new account | 0> then rpchar <i>
<faction> "name" "desc" "bio" each, rpcard, rpbio, rpmsg; rppassok, or
rpfail <1: the name or password was wrong | 0>, to end a request made
with a key. A new password's proof is over "password <its key>". An
admin's game, as it changes: rpadmin <RPA_* bits: 1 an admin, 2 god mode,
4 no-clip, 8 no target; 0: not an admin>. With the account, to a game of
protocol 5 or later: rpcmps <count | -1: no character> <how many may come
out>, then rpcmp <slot> <out> <seconds before it can come again> "name"
"type" "look" <r> <g> <b> each - the character's companions.

Rcon: rp list, rp rename <player> <name>, rp delete <account> <character>,
rp password <account> <temporary>, rp admin <account> [on|off], rp admins.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "g_local.h"
#include "bg_vrcrypt.h"
#include <time.h>
#include <stdio.h>

extern char *ConcatArgs( int start );
extern void G_Say( gentity_t *ent, gentity_t *target, int mode, const char *chatText );

static int G_RP_ClientArg( const char *arg );

#define RP_PROTOCOL			3		// (2: rpacct says when it answers a login; a new password's proof is over its key;
									// 3: down, not dead - rpdn, rpup, rp giveup, the pose: g_vrdown.c)
#define RP_PROTOCOL_ADMIN	4		// a game this new takes rpadmin (one between, as ever, plays roleplay)
#define RP_PROTOCOL_COMP	5		// a game this new takes rpcmps, rpcmp - companions (g_vrcomp.c)

// what an admin's game is told (rpadmin)
#define RPA_ADMIN			1
#define RPA_GOD				2
#define RPA_NOCLIP			4
#define RPA_NOTARGET		8
#define RP_ACCOUNT_MIN		3
#define RP_ACCOUNT_MAX		20
#define RP_NAME_MAX			32		// a character's name: visible characters...
#define RP_NAME_BUF			64		// ...stored (colour codes take room)
#define RP_DESC_MAX			60
#define RP_BIO_MAX			250		// (the menu edits it in one cvar)
#define RP_CHARS_MAX		10		// per account at most (g_rpCharacters: the server's own limit)
#define RP_INDEX_MAX		4096	// characters on the server
#define RP_TRIES			5		// wrong passwords...
#define RP_LOCKOUT			60000	// ...then this long before another try
#define RP_NEW_WAIT			10000	// between accounts made from one connection
#define RP_NEW_PER_ADDRESS	3		// accounts made from one address...
#define RP_NEW_ADDRESS_TIME	3600000	// ...in this long (ms; and the map's time)
#define RP_SWITCH_WAIT		10000	// between switching characters
#define RP_HURT_WAIT		10000	// switching characters or faction waits this long after being hurt
#define RP_CARDS_PER_SEC	10
#define RP_FILE_MAX			65536	// (ten characters, each with six companions)
#define RP_COMP_EDITS		6		// companions changed (rpcomp set | del | out)...
#define RP_COMP_EDIT_TIME	3000	// ...in this long, at most (each is the account's file written)

#define RP_SERVER_FILE		"rp/server.txt"
#define RP_SERVER_FILE_MAX	4096
#define RP_ACCOUNT_DIR		"rp/accounts"

static const char *rpFactions[RPF_NUM] = { "Heroes", "Villains", "Neutral" };	// (RPF_*: bg_public.h)

// What a character wears, kept from what the player wears while playing it.
static const char *rpLookKeys[] = { "model", "color1", "color2", "char_color_red", "char_color_green", "char_color_blue" };
#define RP_LOOK_KEYS	( (int)ARRAY_LEN( rpLookKeys ) )

typedef struct {
	char	name[RP_NAME_BUF];
	int		faction;
	char	desc[RP_DESC_MAX + 1];
	char	bio[RP_BIO_MAX + 1];
	char	look[RP_LOOK_KEYS][MAX_QPATH];	// "" for what it doesn't set
	int		numComps;
	rpComp_t	comps[RP_COMP_MAX];		// its companions (g_vrcomp.c brings them out)
} rpChar_t;

typedef struct {
	char		name[RP_ACCOUNT_MAX + 1];	// lower case
	byte		key[VRRP_KEY_LEN];
	int			last;						// the character last played (-1: none)
	qboolean	admin;						// the owner made it an admin (rp admin): every cheat, cheats on or off
	int			numChars;
	rpChar_t	chars[RP_CHARS_MAX];
} rpAccount_t;

typedef struct {
	qboolean	capable;			// runs this build (sent rpver)
	int			protocol;			// the one it said (0: not yet)
	int			adminTold;			// what rpadmin last told it (RPA_*; 0: nothing, or not an admin)
	char		challenge[VRRP_CHALLENGE_HEX + 1];
	qboolean	loggedIn;
	rpAccount_t	acc;
	int			active;				// the character they play (-1: none)
	qboolean	named;				// the name shown is that character's
	char		playerName[MAX_NETNAME];	// their own name (out of character)
	char		lookBase[RP_LOOK_KEYS][MAX_QPATH];	// what they wore when the character was taken up
	int			fails, lockUntil;	// wrong passwords on this connection
	int			newTime;			// the last account made from here
	int			switchTime;			// the last switch of characters (respawning them)
	qboolean	answering;			// the next rpacct answers their login or new account
	int			cardTime, cards;	// card requests this second
	qboolean	toldOld;			// told their build is older than this server's roleplay
	int			playGen;			// counts the characters taken up and put down: whose companions are out (g_vrcomp.c)
	int			compEditTime, compEdits;	// companions changed lately
} rpClient_t;

static struct {
	qboolean	ready;
	qboolean	on;					// as last published
	char		salt[VRRP_SALT_HEX + 1];
	byte		secret[32];
	uint32_t	counter;
	int			bootTime;
} rp;

static rpClient_t rpc[MAX_CLIENTS];

// addresses that made accounts lately
#define RP_ADDRESSES	64
static struct {
	char	address[48];
	int		count, since;
} rpMade[RP_ADDRESSES];

// accounts that took wrong passwords lately
#define RP_LOCKS	32
static struct {
	char	name[RP_ACCOUNT_MAX + 1];
	int		fails, until, time;
} rpLocks[RP_LOCKS];

// every character on the server: its name (colours stripped, lower case) and account
typedef struct {
	char	key[RP_NAME_BUF];
	char	account[RP_ACCOUNT_MAX + 1];
} rpIndex_t;
static rpIndex_t rpIndex[RP_INDEX_MAX];
static int rpIndexCount;

/*
================================================================================
Small things
================================================================================
*/

qboolean G_RPOn( void ) {
	return (qboolean)( rp.ready && g_rp.integer );
}

static void G_RP_Print( int clientNum, const char *text ) {
	trap->SendServerCommand( clientNum, va( "print \"%s\n\"", text ) );
}

// A message for the Roleplay menu's status line (and the console).
static void G_RP_Msg( int clientNum, const char *text ) {
	G_RP_Print( clientNum, text );
	if ( rpc[clientNum].capable ) {
		trap->SendServerCommand( clientNum, va( "rpmsg \"%s\"", text ) );
	}
}

// A request made with a key (a new account, a login, a new password)
// didn't go through: why, and the game's told it's over (wrong: the name
// or the password was wrong, so a login it kept is no good).
static void G_RP_Refused( int clientNum, const char *why, qboolean wrong ) {
	G_RP_Msg( clientNum, why );
	if ( rpc[clientNum].capable ) {
		trap->SendServerCommand( clientNum, va( "rpfail %i", wrong ? 1 : 0 ) );
	}
}

// Text a player typed, as stored and sent: no quotes, backslashes,
// semicolons or control characters, spaces run together, at most size-1.
static void G_RP_CleanText( const char *in, char *out, int size ) {
	int n = 0;
	qboolean space = qfalse;

	for ( ; in && *in && n < size - 1; in++ ) {
		const unsigned char ch = (unsigned char)*in;
		if ( ch == '"' || ch == '\\' || ch == ';' || ch == '%' ) {
			continue;
		}
		if ( ch < 32 || ch == ' ' ) {
			space = qtrue;
			continue;
		}
		if ( space && n > 0 && n < size - 2 ) {
			out[n++] = ' ';
		}
		space = qfalse;
		out[n++] = (char)ch;
	}
	out[n] = '\0';
}

// A character name as the index keys it: colours out, lower case.
static void G_RP_NameKey( const char *name, char *out, int size ) {
	Q_strncpyz( out, name, size );
	Q_StripColor( out );
	Q_strlwr( out );
}

// Why name won't do as a character's (NULL: it will).
static const char *G_RP_NameProblem( const char *name ) {
	const char *p;
	qboolean letter = qfalse;
	const int len = Q_PrintStrlen( name );

	if ( len < 2 || len > RP_NAME_MAX ) {
		return "A name takes 2 to 32 characters";
	}
	if ( (int)strlen( name ) >= MAX_NETNAME ) {	// (shown whole as the player's name, or cut to look like another's)
		return "That name is too long with its colour codes";
	}
	for ( p = name; *p; p++ ) {
		if ( Q_IsColorString( p ) ) {
			p++;
			continue;
		}
		if ( ( *p >= 'a' && *p <= 'z' ) || ( *p >= 'A' && *p <= 'Z' ) ) {
			letter = qtrue;
		} else if ( !( *p >= '0' && *p <= '9' ) && *p != ' ' && *p != '\'' && *p != '-' && *p != '.' ) {
			return "Names take letters, numbers, spaces and ' - . only";
		}
	}
	return letter ? NULL : "A name needs a letter";
}

static qboolean G_RP_AccountNameOk( const char *name ) {
	static const char *devices[] = { "con", "prn", "aux", "nul" };	// (Windows' devices: not files, whatever follows)
	const int len = (int)strlen( name );
	const char *p;
	int i;

	if ( len < RP_ACCOUNT_MIN || len > RP_ACCOUNT_MAX ) {
		return qfalse;
	}
	for ( p = name; *p; p++ ) {
		if ( !( ( *p >= 'a' && *p <= 'z' ) || ( *p >= '0' && *p <= '9' ) || *p == '_' || *p == '-' ) ) {
			return qfalse;
		}
	}
	for ( i = 0; i < (int)ARRAY_LEN( devices ); i++ ) {
		if ( !Q_stricmp( name, devices[i] ) ) {
			return qfalse;
		}
	}
	if ( len == 4 && ( !Q_stricmpn( name, "com", 3 ) || !Q_stricmpn( name, "lpt", 3 ) ) && name[3] >= '0' && name[3] <= '9' ) {
		return qfalse;
	}
	return qtrue;
}

/*
================================================================================
Random bytes: the system's, where it gives them, mixed with what changes
================================================================================
*/

#ifdef _WIN32
__declspec( dllimport ) void * __stdcall LoadLibraryA( const char *name );
__declspec( dllimport ) void * __stdcall GetProcAddress( void *module, const char *name );
typedef unsigned char ( __stdcall *rtlGenRandom_t )( void *buffer, unsigned long length );
#endif

static qboolean G_RP_SystemRandom( byte *out, int n ) {
#ifdef _WIN32
	static rtlGenRandom_t genRandom;
	static qboolean looked;
	if ( !looked ) {
		void *advapi = LoadLibraryA( "advapi32.dll" );
		looked = qtrue;
		genRandom = advapi ? (rtlGenRandom_t)GetProcAddress( advapi, "SystemFunction036" ) : NULL;
	}
	return (qboolean)( genRandom && genRandom( out, (unsigned long)n ) );
#else
	FILE *f = fopen( "/dev/urandom", "rb" );
	qboolean ok;
	if ( !f ) {
		return qfalse;
	}
	ok = (qboolean)( fread( out, 1, n, f ) == (size_t)n );
	fclose( f );
	return ok;
#endif
}

static void G_RP_Random( byte *out, int n ) {
	static uint32_t calls;
	byte sys[32], block[VRC_SHA256_LEN];
	vrSha256_t c;
	int done = 0;

	memset( sys, 0, sizeof( sys ) );
	G_RP_SystemRandom( sys, sizeof( sys ) );
	while ( done < n ) {
		const int now = trap->Milliseconds();
		const time_t t = time( NULL );
		const clock_t ck = clock();
		const void *here = &c;
		calls++;
		VRC_Sha256Init( &c );
		VRC_Sha256Update( &c, sys, sizeof( sys ) );
		VRC_Sha256Update( &c, rp.secret, sizeof( rp.secret ) );
		VRC_Sha256Update( &c, &calls, sizeof( calls ) );
		VRC_Sha256Update( &c, &now, sizeof( now ) );
		VRC_Sha256Update( &c, &t, sizeof( t ) );
		VRC_Sha256Update( &c, &ck, sizeof( ck ) );
		VRC_Sha256Update( &c, &here, sizeof( here ) );
		VRC_Sha256Update( &c, &level.time, sizeof( level.time ) );
		VRC_Sha256Final( &c, block );
		memcpy( out + done, block, ( n - done < (int)sizeof( block ) ) ? n - done : (int)sizeof( block ) );
		done += sizeof( block );
	}
}

/*
================================================================================
Files
================================================================================
*/

// Reads path into buf: its length; -1: there's no such file; -2: it's
// there, but too big to read whole.
static int G_RP_ReadFile( const char *path, char *buf, int size ) {
	fileHandle_t f;
	int len = trap->FS_Open( path, &f, FS_READ );

	if ( len < 0 || !f ) {
		return -1;
	}
	if ( len >= size ) {
		trap->FS_Close( f );
		G_LogPrintf( "Roleplay: %s is too big to read\n", path );
		return -2;
	}
	trap->FS_Read( buf, len, f );
	trap->FS_Close( f );
	buf[len] = '\0';
	return len;
}

// Whether name is listed in dir (a file there, whether it opens or not).
static qboolean G_RP_Listed( const char *dir, const char *name ) {
	static char list[65536];
	const char *p;
	int count, i;

	count = trap->FS_GetFileList( dir, ".txt", list, sizeof( list ) );
	for ( i = 0, p = list; i < count && *p; i++, p += strlen( p ) + 1 ) {
		if ( !Q_stricmp( p, name ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

// Whether a file's there (one that won't open, say locked, too: it's never
// taken for none, and written over).
static qboolean G_RP_FileThere( const char *path ) {
	fileHandle_t f;
	const int len = trap->FS_Open( path, &f, FS_READ );
	char dir[MAX_QPATH];
	const char *slash;

	if ( f ) {
		trap->FS_Close( f );
	}
	if ( len >= 0 && f ) {
		return qtrue;
	}
	slash = strrchr( path, '/' );
	if ( !slash ) {
		return qfalse;
	}
	Q_strncpyz( dir, path, Com_Clampi( 1, sizeof( dir ), (int)( slash - path ) + 1 ) );
	return G_RP_Listed( dir, slash + 1 );
}

static qboolean G_RP_WriteFile( const char *path, const char *text ) {
	fileHandle_t f;

	trap->FS_Open( path, &f, FS_WRITE );
	if ( !f ) {
		G_LogPrintf( "Roleplay: couldn't write %s\n", path );
		return qfalse;
	}
	trap->FS_Write( text, (int)strlen( text ), f );
	trap->FS_Close( f );
	return qtrue;
}

static const char *G_RP_AccountPath( const char *name ) {
	return va( RP_ACCOUNT_DIR "/%s.txt", name );
}

static void G_RP_SaveAccount( const rpAccount_t *acc ) {
	static char text[RP_FILE_MAX];
	char hex[VRRP_KEY_LEN * 2 + 1];
	int i, k;

	VRC_ToHex( acc->key, VRRP_KEY_LEN, hex );
	Com_sprintf( text, sizeof( text ), "// OpenJK VR roleplay account\nkey %s\nlast %i\n%s", hex, acc->last, acc->admin ? "admin 1\n" : "" );
	for ( i = 0; i < acc->numChars; i++ ) {
		const rpChar_t *ch = &acc->chars[i];
		Q_strcat( text, sizeof( text ), va( "character\nname \"%s\"\nfaction %i\ndesc \"%s\"\nbio \"%s\"\n",
			ch->name, ch->faction, ch->desc, ch->bio ) );
		for ( k = 0; k < RP_LOOK_KEYS; k++ ) {
			if ( ch->look[k][0] ) {
				Q_strcat( text, sizeof( text ), va( "look %s \"%s\"\n", rpLookKeys[k], ch->look[k] ) );
			}
		}
		for ( k = 0; k < ch->numComps; k++ ) {
			const rpComp_t *cp = &ch->comps[k];
			Q_strcat( text, sizeof( text ), va( "companion\ncname \"%s\"\nctype \"%s\"\nclook \"%s\"\nccolor %i %i %i\ncout %i\n",
				cp->name, cp->type, cp->look, cp->rgb[0], cp->rgb[1], cp->rgb[2], cp->out ? 1 : 0 ) );
		}
		Q_strcat( text, sizeof( text ), "end\n" );
	}
	G_RP_WriteFile( G_RP_AccountPath( acc->name ), text );
}

static int G_RP_LookIndex( const char *key ) {
	int k;

	for ( k = 0; k < RP_LOOK_KEYS; k++ ) {
		if ( !Q_stricmp( key, rpLookKeys[k] ) ) {
			return k;
		}
	}
	return -1;
}

// Reads an account's file into acc (qfalse: there's none, or it's broken).
static qboolean G_RP_LoadAccount( const char *name, rpAccount_t *acc ) {
	static char text[RP_FILE_MAX];
	const char *p, *tok;
	rpChar_t *ch = NULL;
	rpComp_t *cp = NULL;
	qboolean haveKey = qfalse, inChar = qfalse;

	if ( !G_RP_AccountNameOk( name ) || G_RP_ReadFile( G_RP_AccountPath( name ), text, sizeof( text ) ) < 0 ) {
		return qfalse;
	}
	memset( acc, 0, sizeof( *acc ) );
	Q_strncpyz( acc->name, name, sizeof( acc->name ) );
	acc->last = -1;
	p = text;
	COM_BeginParseSession( "rp account" );
	while ( 1 ) {
		tok = COM_ParseExt( &p, qtrue );
		if ( !tok[0] ) {
			break;
		}
		if ( !Q_stricmp( tok, "key" ) ) {
			haveKey = VRC_FromHex( COM_ParseExt( &p, qfalse ), acc->key, VRRP_KEY_LEN );
		} else if ( !Q_stricmp( tok, "last" ) ) {
			acc->last = atoi( COM_ParseExt( &p, qfalse ) );
		} else if ( !inChar && !Q_stricmp( tok, "admin" ) ) {	// (the account's, never a character's)
			acc->admin = (qboolean)( atoi( COM_ParseExt( &p, qfalse ) ) == 1 );
		} else if ( !Q_stricmp( tok, "character" ) ) {
			inChar = qtrue;
			ch = ( acc->numChars < RP_CHARS_MAX ) ? &acc->chars[acc->numChars++] : NULL;
			cp = NULL;
			if ( ch ) {
				memset( ch, 0, sizeof( *ch ) );
				ch->faction = RPF_NEUTRAL;
			}
		} else if ( !Q_stricmp( tok, "end" ) ) {
			ch = NULL;
			cp = NULL;
			inChar = qfalse;
		} else if ( ch && !Q_stricmp( tok, "companion" ) ) {
			cp = ( ch->numComps < RP_COMP_MAX ) ? &ch->comps[ch->numComps++] : NULL;
			if ( cp ) {
				memset( cp, 0, sizeof( *cp ) );
				cp->rgb[0] = cp->rgb[1] = cp->rgb[2] = 255;
			}
		} else if ( cp && !Q_stricmp( tok, "cname" ) ) {
			G_RP_CleanText( COM_ParseExt( &p, qfalse ), cp->name, sizeof( cp->name ) );
		} else if ( cp && !Q_stricmp( tok, "ctype" ) ) {
			tok = COM_ParseExt( &p, qfalse );
			if ( BG_VRCompTypeOk( tok ) ) {
				Q_strncpyz( cp->type, tok, sizeof( cp->type ) );
				Q_strlwr( cp->type );
			}
		} else if ( cp && !Q_stricmp( tok, "clook" ) ) {
			tok = COM_ParseExt( &p, qfalse );
			if ( (int)strlen( tok ) < (int)sizeof( cp->look ) && BG_VRCompLookOk( tok ) ) {
				Q_strncpyz( cp->look, tok, sizeof( cp->look ) );
			}
		} else if ( cp && !Q_stricmp( tok, "ccolor" ) ) {
			int k;
			for ( k = 0; k < 3; k++ ) {
				cp->rgb[k] = (byte)Com_Clampi( 0, 255, atoi( COM_ParseExt( &p, qfalse ) ) );
			}
		} else if ( cp && !Q_stricmp( tok, "cout" ) ) {
			cp->out = (qboolean)( atoi( COM_ParseExt( &p, qfalse ) ) == 1 );
		} else if ( ch && !Q_stricmp( tok, "name" ) ) {
			G_RP_CleanText( COM_ParseExt( &p, qfalse ), ch->name, sizeof( ch->name ) );
		} else if ( ch && !Q_stricmp( tok, "faction" ) ) {
			ch->faction = Com_Clampi( 0, RPF_NUM - 1, atoi( COM_ParseExt( &p, qfalse ) ) );
		} else if ( ch && !Q_stricmp( tok, "desc" ) ) {
			G_RP_CleanText( COM_ParseExt( &p, qfalse ), ch->desc, sizeof( ch->desc ) );
		} else if ( ch && !Q_stricmp( tok, "bio" ) ) {
			G_RP_CleanText( COM_ParseExt( &p, qfalse ), ch->bio, sizeof( ch->bio ) );
		} else if ( ch && !Q_stricmp( tok, "look" ) ) {
			char key[64];
			int k;
			Q_strncpyz( key, COM_ParseExt( &p, qfalse ), sizeof( key ) );
			k = G_RP_LookIndex( key );
			tok = COM_ParseExt( &p, qfalse );
			if ( k >= 0 ) {
				G_RP_CleanText( tok, ch->look[k], sizeof( ch->look[k] ) );
			}
		} else {
			SkipRestOfLine( &p );
		}
	}
	// a character without a name (a damaged file) is dropped; and a companion
	// without one, or what it fights as - and past the most that come out,
	// the rest stay in
	{
		int i, n = 0;
		for ( i = 0; i < acc->numChars; i++ ) {
			rpChar_t *c = &acc->chars[i];
			int k, m = 0, out = 0;
			for ( k = 0; k < c->numComps; k++ ) {
				if ( !c->comps[k].name[0] || !c->comps[k].type[0] ) {
					continue;
				}
				c->comps[m] = c->comps[k];
				if ( c->comps[m].out && ++out > RP_COMP_OUT_MAX ) {
					c->comps[m].out = qfalse;
				}
				m++;
			}
			if ( m < c->numComps ) {
				memset( &c->comps[m], 0, sizeof( c->comps[0] ) * ( c->numComps - m ) );
			}
			c->numComps = m;
		}
		for ( i = 0; i < acc->numChars; i++ ) {
			if ( acc->chars[i].name[0] ) {
				if ( n != i ) {
					acc->chars[n] = acc->chars[i];
					if ( acc->last == i ) {
						acc->last = n;
					}
				}
				n++;
			} else if ( acc->last == i ) {
				acc->last = -1;
			}
		}
		acc->numChars = n;
	}
	if ( acc->last < -1 || acc->last >= acc->numChars ) {
		acc->last = -1;
	}
	return haveKey;
}

/*
================================================================================
The index of character names
================================================================================
*/

static int G_RP_IndexFind( const char *name ) {
	char key[RP_NAME_BUF];
	int i;

	G_RP_NameKey( name, key, sizeof( key ) );
	for ( i = 0; i < rpIndexCount; i++ ) {
		if ( !strcmp( rpIndex[i].key, key ) ) {
			return i;
		}
	}
	return -1;
}

static qboolean G_RP_IndexAdd( const char *name, const char *account ) {
	if ( rpIndexCount >= RP_INDEX_MAX ) {
		return qfalse;
	}
	G_RP_NameKey( name, rpIndex[rpIndexCount].key, sizeof( rpIndex[0].key ) );
	Q_strncpyz( rpIndex[rpIndexCount].account, account, sizeof( rpIndex[0].account ) );
	rpIndexCount++;
	return qtrue;
}

static void G_RP_IndexRemove( const char *name, const char *account ) {
	char key[RP_NAME_BUF];
	int i;

	G_RP_NameKey( name, key, sizeof( key ) );
	for ( i = 0; i < rpIndexCount; i++ ) {
		if ( !strcmp( rpIndex[i].key, key ) && !Q_stricmp( rpIndex[i].account, account ) ) {
			rpIndex[i] = rpIndex[--rpIndexCount];
			return;
		}
	}
}

static void G_RP_IndexBuild( void ) {
	static char list[65536];
	static rpAccount_t acc;
	const char *p;
	int count, i, c;

	rpIndexCount = 0;
	count = trap->FS_GetFileList( RP_ACCOUNT_DIR, ".txt", list, sizeof( list ) );
	for ( i = 0, p = list; i < count && *p; i++, p += strlen( p ) + 1 ) {
		char name[RP_ACCOUNT_MAX + 1];
		COM_StripExtension( p, name, sizeof( name ) );
		if ( !G_RP_LoadAccount( name, &acc ) ) {
			continue;
		}
		for ( c = 0; c < acc.numChars; c++ ) {
			if ( G_RP_IndexFind( acc.chars[c].name ) < 0 ) {
				G_RP_IndexAdd( acc.chars[c].name, acc.name );
			}
		}
	}
}

/*
================================================================================
The server's salt and secret
================================================================================
*/

// The salt and secret, read (or, the first time, made): qfalse if the file's
// there but can't be read - never made anew over it, as a new salt is every
// account's password lost.
static qboolean G_RP_LoadServer( void ) {
	static char text[RP_SERVER_FILE_MAX];
	char salt[VRRP_SALT_HEX + 1] = { 0 };
	const char *p, *tok;
	qboolean haveSalt = qfalse, haveSecret = qfalse;
	const int len = G_RP_ReadFile( RP_SERVER_FILE, text, sizeof( text ) );

	if ( len >= 0 ) {
		p = text;
		COM_BeginParseSession( "rp server" );
		while ( 1 ) {
			tok = COM_ParseExt( &p, qtrue );
			if ( !tok[0] ) {
				break;
			}
			if ( !Q_stricmp( tok, "salt" ) ) {
				byte b[VRRP_SALT_HEX / 2];
				Q_strncpyz( salt, COM_ParseExt( &p, qfalse ), sizeof( salt ) );
				haveSalt = VRC_FromHex( salt, b, sizeof( b ) );
			} else if ( !Q_stricmp( tok, "secret" ) ) {
				haveSecret = VRC_FromHex( COM_ParseExt( &p, qfalse ), rp.secret, sizeof( rp.secret ) );
			} else {
				SkipRestOfLine( &p );
			}
		}
	}
	if ( haveSalt && haveSecret ) {
		Q_strncpyz( rp.salt, salt, sizeof( rp.salt ) );
		Q_strlwr( rp.salt );
		return qtrue;
	}
	if ( len != -1 || G_RP_FileThere( RP_SERVER_FILE ) ) {
		Com_Printf( S_COLOR_RED "Roleplay is off: %s couldn't be read. Mend it, or delete it to start over (every account's "
			"password then stops working)\n", RP_SERVER_FILE );
		G_LogPrintf( "Roleplay: %s couldn't be read; roleplay is off\n", RP_SERVER_FILE );
		return qfalse;
	}
	// the first time: make them
	{
		byte b[VRRP_SALT_HEX / 2];
		char secretHex[65];
		G_RP_Random( rp.secret, sizeof( rp.secret ) );
		G_RP_Random( b, sizeof( b ) );
		VRC_ToHex( b, sizeof( b ), rp.salt );
		VRC_ToHex( rp.secret, sizeof( rp.secret ), secretHex );
		G_RP_WriteFile( RP_SERVER_FILE, va( "// OpenJK VR roleplay: this server's login salt and secret. Keep it:\n"
			"// a new salt makes every account's password stop working.\nsalt %s\nsecret %s\n", rp.salt, secretHex ) );
	}
	return qtrue;
}

static void G_RP_NewChallenge( int clientNum ) {
	rpClient_t *c = &rpc[clientNum];
	byte mac[VRC_SHA256_LEN];
	const char *msg;

	rp.counter++;
	msg = va( "challenge:%u:%i:%i:%i", rp.counter, rp.bootTime, clientNum, level.time );
	VRC_HmacSha256( rp.secret, sizeof( rp.secret ), msg, strlen( msg ), mac );
	VRC_ToHex( mac, VRRP_CHALLENGE_HEX / 2, c->challenge );
	if ( c->capable ) {
		trap->SendServerCommand( clientNum, va( "rpch %s %s", rp.salt, c->challenge ) );
	}
}

/*
================================================================================
Telling a client where they stand
================================================================================
*/

static int G_RP_MostChars( void ) {
	return Com_Clampi( 1, RP_CHARS_MAX, g_rpCharacters.integer );
}

static void G_RP_SendAccount( int clientNum ) {
	const rpClient_t *c = &rpc[clientNum];
	int i;

	if ( !c->capable ) {
		rpc[clientNum].answering = qfalse;
		return;
	}
	if ( !c->loggedIn ) {
		trap->SendServerCommand( clientNum, va( "rpacct - -1 0 %i 0", G_RP_MostChars() ) );
		G_RPSendComps( clientNum );
		return;
	}
	trap->SendServerCommand( clientNum, va( "rpacct %s %i %i %i %i", c->acc.name, c->active, c->acc.numChars, G_RP_MostChars(), c->answering ? 1 : 0 ) );
	rpc[clientNum].answering = qfalse;
	for ( i = 0; i < c->acc.numChars; i++ ) {
		const rpChar_t *ch = &c->acc.chars[i];
		trap->SendServerCommand( clientNum, va( "rpchar %i %i \"%s\" \"%s\" \"%s\"", i, ch->faction, ch->name, ch->desc, ch->bio ) );
	}
	G_RPSendComps( clientNum );
}

// The login a client keeps across a map change (the game restarts, they
// don't), and that their game runs this build: it says so again, a moment
// in, but it's known from the start - they go down, not die, from the first.
static void G_RP_WriteSession( int clientNum ) {
	const rpClient_t *c = &rpc[clientNum];

	trap->Cvar_Set( va( "vrrp%i", clientNum ), c->loggedIn ? va( "%s %i %i", c->acc.name, c->active, c->capable ? 1 : 0 ) : "" );
}

/*
================================================================================
Playing a character
================================================================================
*/

static rpChar_t *G_RP_Active( int clientNum ) {
	rpClient_t *c = &rpc[clientNum];

	if ( !G_RPOn() || !c->loggedIn || c->active < 0 || c->active >= c->acc.numChars ) {
		return NULL;
	}
	return &c->acc.chars[c->active];
}

// The faction of the character a client plays (-1: none, or roleplay's off).
int G_RPFaction( int clientNum ) {
	const rpChar_t *ch;

	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return -1;
	}
	ch = G_RP_Active( clientNum );
	return ch ? ch->faction : -1;
}

// Whether a client's game runs this build (it said rpver).
qboolean G_RPCapable( int clientNum ) {
	return (qboolean)( clientNum >= 0 && clientNum < MAX_CLIENTS && rpc[clientNum].capable );
}

// A companion of the character a client plays (NULL: no such one, no
// character, or roleplay's off).
const rpComp_t *G_RPComp( int clientNum, int slot ) {
	const rpChar_t *ch = ( clientNum >= 0 && clientNum < MAX_CLIENTS ) ? G_RP_Active( clientNum ) : NULL;

	return ( ch && slot >= 0 && slot < ch->numComps ) ? &ch->comps[slot] : NULL;
}

// The character a client plays, as their companions go by it: another
// number for every character taken up or put down (-1: none - no
// character, or roleplay's off).
int G_RPCompChar( int clientNum ) {
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS || !G_RP_Active( clientNum ) ) {
		return -1;
	}
	return rpc[clientNum].playGen & 0x7fffffff;
}

// A client's game told the companions of the character they play:
//   rpcmps <count | -1: no character> <how many may come out>
//   rpcmp <slot> <out> <seconds before it can come again> "<name>" "<type>" "<look>" <r> <g> <b>
void G_RPSendComps( int clientNum ) {
	const rpChar_t *ch;
	int k;

	if ( clientNum < 0 || clientNum >= MAX_CLIENTS || !rpc[clientNum].capable || rpc[clientNum].protocol < RP_PROTOCOL_COMP ) {
		return;
	}
	ch = G_RP_Active( clientNum );
	trap->SendServerCommand( clientNum, va( "rpcmps %i %i", ch ? ch->numComps : -1, Com_Clampi( 0, RP_COMP_OUT_MAX, g_rpCompanions.integer ) ) );
	for ( k = 0; ch && k < ch->numComps; k++ ) {
		const rpComp_t *cp = &ch->comps[k];
		trap->SendServerCommand( clientNum, va( "rpcmp %i %i %i \"%s\" \"%s\" \"%s\" %i %i %i", k, cp->out ? 1 : 0, G_RPCompWait( clientNum, k ),
			cp->name, cp->type, cp->look, cp->rgb[0], cp->rgb[1], cp->rgb[2] ) );
	}
}

static void G_RP_RememberLook( int clientNum ) {
	char userinfo[MAX_INFO_STRING];
	int k;

	trap->GetUserinfo( clientNum, userinfo, sizeof( userinfo ) );
	for ( k = 0; k < RP_LOOK_KEYS; k++ ) {
		Q_strncpyz( rpc[clientNum].lookBase[k], Info_ValueForKey( userinfo, rpLookKeys[k] ), sizeof( rpc[0].lookBase[k] ) );
	}
}

static void G_RP_Refresh( int clientNum ) {
	gentity_t *ent = &g_entities[clientNum];

	if ( ent->inuse && ent->client && ent->client->pers.connected != CON_DISCONNECTED ) {
		ClientUserinfoChanged( clientNum );
	}
}

// Whether a client's in the round, alive: switching characters respawns them.
static qboolean G_RP_InPlay( const gentity_t *ent ) {
	return (qboolean)( ent->client && ent->health > 0 && ent->client->sess.sessionTeam != TEAM_SPECTATOR &&
		ent->client->pers.connected == CON_CONNECTED );
}

// Takes up character index (-1: none) for a logged in client; respawn: the
// way switching does (and the first one taken up doesn't).
static void G_RP_Play( int clientNum, int index, qboolean respawn ) {
	rpClient_t *c = &rpc[clientNum];
	gentity_t *ent = &g_entities[clientNum];

	if ( respawn ) {
		c->switchTime = level.time ? level.time : 1;
	}
	if ( ent->client ) {
		ent->client->rpProvoked = 0;	// (the NPCs' grudges were the last character's: g_vrdown.c)
	}
	c->active = index;
	c->playGen++;	// (another character's companions, or none: g_vrcomp.c)
	c->acc.last = index;
	G_RP_RememberLook( clientNum );
	G_RP_SaveAccount( &c->acc );
	G_RP_WriteSession( clientNum );
	G_RP_Refresh( clientNum );
	G_RP_SendAccount( clientNum );
	if ( respawn && G_RPOn() && G_RP_InPlay( ent ) ) {
		ClientSpawn( ent );
	}
}

// Why a client can't switch characters (switch: what respawns them) or
// faction now (NULL: they can).
static const char *G_RP_BusyReason( int clientNum, qboolean switching ) {
	const gentity_t *ent = &g_entities[clientNum];
	const gclient_t *cl = ent->client;

	if ( !cl ) {
		return "Not now";
	}
	if ( cl->rpDowned ) {
		return "Not while you're down";
	}
	if ( cl->ps.duelInProgress ) {
		return "Not during a duel";
	}
	if ( cl->rpHurtTime && level.time - cl->rpHurtTime < RP_HURT_WAIT && ent->health > 0 ) {
		return "Not while fighting: wait until you've been unhurt for 10 seconds";
	}
	if ( !switching ) {
		return NULL;
	}
	if ( level.intermissiontime ) {
		return "Not now: the round's over";
	}
	if ( rpc[clientNum].switchTime && level.time - rpc[clientNum].switchTime < RP_SWITCH_WAIT ) {
		return "Wait a moment before switching characters again";
	}
	if ( G_RP_InPlay( ent ) ) {	// (respawned by it: what dying would sort out, it doesn't)
		if ( cl->ps.powerups[PW_REDFLAG] || cl->ps.powerups[PW_BLUEFLAG] || cl->ps.powerups[PW_NEUTRALFLAG] ) {
			return "Not while you carry a flag";
		}
		if ( cl->ps.m_iVehicleNum ) {
			return "Not in a vehicle";
		}
		if ( cl->ps.isJediMaster ) {
			return "Not while you're the Jedi Master";
		}
		if ( level.gametype == GT_SIEGE || level.gametype == GT_DUEL || level.gametype == GT_POWERDUEL ) {
			return "In this game type, switch characters while you're out of the round";
		}
	}
	return NULL;
}

/*
================================================================================
Admins: accounts the owner made admins (rcon rp admin), with every cheat
================================================================================
*/

// Whether a client is an admin now: logged in to an account the owner made one.
qboolean G_RPAdmin( int clientNum ) {
	return (qboolean)( clientNum >= 0 && clientNum < MAX_CLIENTS && rp.ready && rpc[clientNum].loggedIn && rpc[clientNum].acc.admin );
}

// Someone who's no longer an admin: what their cheats left on ends - unless
// cheats are on for everyone, when anyone may have it.
static void G_RP_AdminOff( int clientNum ) {
	gentity_t *ent = &g_entities[clientNum];

	if ( sv_cheats.integer || !ent->inuse || !ent->client ) {
		return;
	}
	ent->flags &= ~( FL_GODMODE | FL_NOTARGET );
	ent->client->noclip = qfalse;
}

// What an admin's game is told (RPA_*; 0: not an admin).
static int G_RP_AdminBits( int clientNum ) {
	const gentity_t *ent = &g_entities[clientNum];
	int bits;

	if ( !G_RPAdmin( clientNum ) || !ent->client ) {
		return 0;
	}
	bits = RPA_ADMIN;
	if ( ent->flags & FL_GODMODE ) {
		bits |= RPA_GOD;
	}
	if ( ent->client->noclip ) {
		bits |= RPA_NOCLIP;
	}
	if ( ent->flags & FL_NOTARGET ) {
		bits |= RPA_NOTARGET;
	}
	return bits;
}

// ClientCommand, a cheat command: whether it runs - cheats on, as ever; off,
// for an admin; never while down (no cheat out of it: a hand up, or giving
// up). qfalse: the player's told why.
qboolean G_RPCheatAllowed( gentity_t *ent ) {
	const int clientNum = ent - g_entities;

	if ( !sv_cheats.integer && !G_RPAdmin( clientNum ) ) {
		trap->SendServerCommand( clientNum, va( "print \"%s\n\"", G_GetStringEdString( "MP_SVGAME", "NOCHEATS" ) ) );
		return qfalse;
	}
	if ( G_RPDowned( ent ) ) {
		G_RP_Msg( clientNum, "Not while you're down" );
		return qfalse;
	}
	return qtrue;
}

// ClientCommand, a cheat about to run with cheats off: an admin's, logged.
void G_RPAdminCheat( gentity_t *ent ) {
	const int clientNum = ent - g_entities;

	G_LogPrintf( "Admin: %s" S_COLOR_WHITE " (account %s, client %i): %s\n", ent->client ? ent->client->pers.netname : "",
		rpc[clientNum].acc.name, clientNum, ConcatArgs( 0 ) );
}

// Each frame: an admin's game told what its Admin page shows as it changes
// (rpadmin), and one no longer an admin told so once (a game too old for
// it, never).
void G_RPFrame( void ) {
	int i;

	for ( i = 0; i < level.maxclients; i++ ) {
		rpClient_t *c = &rpc[i];
		const gentity_t *ent = &g_entities[i];
		int bits;
		if ( !ent->inuse || !ent->client || ent->client->pers.connected != CON_CONNECTED || c->protocol < RP_PROTOCOL_ADMIN ) {
			continue;
		}
		bits = G_RP_AdminBits( i );
		if ( bits != c->adminTold ) {
			c->adminTold = bits;
			trap->SendServerCommand( i, va( "rpadmin %i", bits ) );
		}
	}
}

/*
================================================================================
Logging in and out
================================================================================
*/

// An account's place among those with wrong passwords lately (-1: none; add:
// given one - an empty place, or the one wrong longest ago that isn't
// waiting out its lock, as wrong passwords for other names mustn't end one;
// -1 if all are).
static int G_RP_LockFor( const char *name, qboolean add ) {
	int i, take = -1;

	for ( i = 0; i < RP_LOCKS; i++ ) {
		if ( rpLocks[i].name[0] && !Q_stricmp( rpLocks[i].name, name ) ) {
			return i;
		}
	}
	if ( !add ) {
		return -1;
	}
	for ( i = 0; i < RP_LOCKS; i++ ) {
		if ( rpLocks[i].name[0] && rpLocks[i].until && level.time - rpLocks[i].until < 0 ) {
			continue;	// (waiting)
		}
		if ( take < 0 || !rpLocks[i].name[0] || ( rpLocks[take].name[0] && rpLocks[i].time < rpLocks[take].time ) ) {
			take = i;
		}
	}
	if ( take >= 0 ) {
		memset( &rpLocks[take], 0, sizeof( rpLocks[0] ) );
		Q_strncpyz( rpLocks[take].name, name, sizeof( rpLocks[0].name ) );
	}
	return take;
}

static qboolean G_RP_Locked( int clientNum, const char *account ) {
	const int i = G_RP_LockFor( account, qfalse );

	if ( rpc[clientNum].lockUntil && level.time - rpc[clientNum].lockUntil < 0 ) {
		return qtrue;
	}
	return (qboolean)( i >= 0 && rpLocks[i].until && level.time - rpLocks[i].until < 0 );
}

static void G_RP_Failed( int clientNum, const char *account ) {
	rpClient_t *c = &rpc[clientNum];
	const int i = G_RP_LockFor( account, qtrue );

	if ( ++c->fails >= RP_TRIES ) {
		c->fails = 0;
		c->lockUntil = level.time + RP_LOCKOUT;
	}
	if ( i < 0 ) {
		return;	// (every place waiting: the connection's count still holds)
	}
	rpLocks[i].time = level.time;
	if ( ++rpLocks[i].fails >= RP_TRIES ) {
		rpLocks[i].fails = 0;
		rpLocks[i].until = level.time + RP_LOCKOUT;
	}
}

static void G_RP_LogOut( int clientNum, const char *why ) {
	rpClient_t *c = &rpc[clientNum];

	if ( !c->loggedIn ) {
		return;
	}
	if ( c->acc.admin ) {
		G_RP_AdminOff( clientNum );	// (an admin's cheats go with the login)
	}
	c->loggedIn = qfalse;
	c->active = -1;
	c->playGen++;
	if ( g_entities[clientNum].client ) {
		g_entities[clientNum].client->rpProvoked = 0;	// (the character's grudges go with it: g_vrdown.c)
	}
	memset( &c->acc, 0, sizeof( c->acc ) );
	G_RP_WriteSession( clientNum );
	G_RP_Refresh( clientNum );
	G_RP_SendAccount( clientNum );
	if ( why ) {
		G_RP_Msg( clientNum, why );
	}
}

// Logs client in to an account loaded into acc.
static void G_RP_LogIn( int clientNum, const rpAccount_t *acc ) {
	rpClient_t *c = &rpc[clientNum];
	int i;

	// one place at a time: an older login of this account ends
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		if ( i != clientNum && rpc[i].loggedIn && !Q_stricmp( rpc[i].acc.name, acc->name ) ) {
			G_RP_LogOut( i, "Logged out: your account logged in somewhere else" );
		}
	}
	if ( c->loggedIn && c->acc.admin && !acc->admin ) {
		G_RP_AdminOff( clientNum );	// (logged in to another account, one that isn't an admin)
	}
	c->loggedIn = qtrue;
	c->acc = *acc;
	c->adminTold = -1;	// (another account: its game told where it stands, admin or not)
	c->fails = 0;
	c->answering = qtrue;	// (the account sent next answers the login, or the new account)
	G_RP_Play( clientNum, ( acc->last >= 0 && acc->last < acc->numChars ) ? acc->last : -1, qfalse );
}

static void G_RP_Hello( gentity_t *ent ) {
	const int clientNum = ent - g_entities;
	rpClient_t *c = &rpc[clientNum];
	char arg[16];

	trap->Argv( 1, arg, sizeof( arg ) );
	if ( atoi( arg ) < RP_PROTOCOL ) {
		// (an older build's: it'd take the answers wrong - and down, it'd
		// see itself standing: it's told, once, and left out of roleplay)
		if ( !c->toldOld ) {
			c->toldOld = qtrue;
			G_RP_Print( clientNum, "This server's roleplay is newer than your OpenJK VR: update it to play a character here" );
		}
		return;
	}
	c->capable = qtrue;
	c->protocol = atoi( arg );
	c->adminTold = 0;	// (a game started over - a vid_restart - knows nothing yet: told again)
	if ( c->challenge[0] ) {
		// a hello again (one lost, or its answer late): the same challenge, so
		// the answer to either does, until it's used
		trap->SendServerCommand( clientNum, va( "rpch %s %s", rp.salt, c->challenge ) );
	} else {
		G_RP_NewChallenge( clientNum );
	}
	G_RP_SendAccount( clientNum );
	G_RPDownHello( clientNum );	// (who's down: g_vrdown.c)
}

// A client's address, without the port (accounts made from it are counted).
static void G_RP_Address( int clientNum, char *out, int size ) {
	const gclient_t *cl = g_entities[clientNum].client;
	char *colon;

	Q_strncpyz( out, cl ? cl->sess.IP : "", size );
	if ( out[0] == '[' ) {
		char *end = strchr( out, ']' );
		if ( end ) {
			end[1] = '\0';
		}
	} else if ( ( colon = strchr( out, ':' ) ) != NULL && !strchr( colon + 1, ':' ) ) {
		*colon = '\0';
	}
}

// Whether another account may be made from address now; made: one was.
static qboolean G_RP_AddressMay( const char *address, qboolean made ) {
	int i, take = 0;

	for ( i = 0; i < RP_ADDRESSES; i++ ) {
		if ( rpMade[i].address[0] && !Q_stricmp( rpMade[i].address, address ) ) {
			break;
		}
		if ( !rpMade[i].address[0] || ( rpMade[take].address[0] && rpMade[i].since < rpMade[take].since ) ) {
			take = i;
		}
	}
	if ( i < RP_ADDRESSES && level.time - rpMade[i].since >= RP_NEW_ADDRESS_TIME ) {
		rpMade[i].count = 0;	// (a new hour)
		rpMade[i].since = level.time;
	}
	if ( !made ) {
		return (qboolean)( i == RP_ADDRESSES || rpMade[i].count < RP_NEW_PER_ADDRESS );
	}
	if ( i == RP_ADDRESSES ) {
		i = take;
		Q_strncpyz( rpMade[i].address, address, sizeof( rpMade[i].address ) );
		rpMade[i].count = 0;
		rpMade[i].since = level.time;
	}
	rpMade[i].count++;
	return qtrue;
}

static void G_RP_NewAccount( gentity_t *ent ) {
	const int clientNum = ent - g_entities;
	rpClient_t *c = &rpc[clientNum];
	static rpAccount_t acc;
	char name[64], hex[128], address[48];

	// (roleplay off too: logged in, nothing else changes then - but an admin's cheats)
	if ( G_RPDowned( ent ) ) {
		G_RP_Refused( clientNum, "Not while you're down", qfalse );	// (logging in to it: no way up but a hand, or giving up)
		return;
	}
	trap->Argv( 1, name, sizeof( name ) );
	trap->Argv( 2, hex, sizeof( hex ) );
	Q_strlwr( name );
	if ( !G_RP_AccountNameOk( name ) ) {
		G_RP_Refused( clientNum, "An account name takes 3 to 20 letters, numbers, _ or - (and isn't con, nul, aux, prn, com1 or lpt1...)", qfalse );
		return;
	}
	G_RP_Address( clientNum, address, sizeof( address ) );
	if ( ( c->newTime && level.time - c->newTime < RP_NEW_WAIT ) || !G_RP_AddressMay( address, qfalse ) ) {
		G_RP_Refused( clientNum, ( c->newTime && level.time - c->newTime < RP_NEW_WAIT ) ? "Wait a moment before making another account" :
			"Enough accounts have been made from here for now: try later", qfalse );
		return;
	}
	memset( &acc, 0, sizeof( acc ) );
	if ( !VRC_FromHex( hex, acc.key, VRRP_KEY_LEN ) ) {
		G_RP_Refused( clientNum, "Your game didn't send a proper key: update OpenJK VR", qfalse );
		return;
	}
	if ( G_RP_FileThere( G_RP_AccountPath( name ) ) ) {	// (taken, even if its file won't read: never written over)
		G_RP_Refused( clientNum, "That account name is taken", qfalse );
		return;
	}
	Q_strncpyz( acc.name, name, sizeof( acc.name ) );
	acc.last = -1;
	c->newTime = level.time;
	G_RP_AddressMay( address, qtrue );
	G_RP_SaveAccount( &acc );
	G_LogPrintf( "Roleplay: account %s made by client %i\n", acc.name, clientNum );
	G_RP_NewChallenge( clientNum );	// (before the account, as a login has it: the game knows the answer's to it)
	G_RP_LogIn( clientNum, &acc );
	G_RP_Msg( clientNum, va( "Account %s made, and you're logged in", acc.name ) );
}

static void G_RP_Login( gentity_t *ent ) {
	const int clientNum = ent - g_entities;
	rpClient_t *c = &rpc[clientNum];
	static rpAccount_t acc;
	char name[64], hex[128];
	byte proof[VRC_SHA256_LEN], want[VRC_SHA256_LEN];
	char challenge[VRRP_CHALLENGE_HEX + 1];

	// (roleplay off too, as a new account)
	if ( G_RPDowned( ent ) ) {
		G_RP_Refused( clientNum, "Not while you're down", qfalse );	// (another character: no way up but a hand, or giving up)
		return;
	}
	trap->Argv( 1, name, sizeof( name ) );
	trap->Argv( 2, hex, sizeof( hex ) );
	Q_strlwr( name );
	Q_strncpyz( challenge, c->challenge, sizeof( challenge ) );
	G_RP_NewChallenge( clientNum );	// each challenge answers once
	if ( !G_RP_AccountNameOk( name ) ) {
		G_RP_Refused( clientNum, "Wrong account name or password", qtrue );
		return;
	}
	if ( G_RP_Locked( clientNum, name ) ) {
		G_RP_Refused( clientNum, "Too many wrong passwords: wait a minute", qfalse );
		return;
	}
	if ( !challenge[0] || !VRC_FromHex( hex, proof, sizeof( proof ) ) || !G_RP_LoadAccount( name, &acc ) ) {
		G_RP_Failed( clientNum, name );
		G_RP_Refused( clientNum, "Wrong account name or password", qtrue );
		return;
	}
	VRRP_LoginProof( acc.key, "login", challenge, acc.name, want );
	if ( !VRC_Equal( proof, want, sizeof( want ) ) ) {
		G_RP_Failed( clientNum, name );
		G_LogPrintf( "Roleplay: wrong password for %s from client %i\n", name, clientNum );
		G_RP_Refused( clientNum, "Wrong account name or password", qtrue );
		return;
	}
	{
		const int i = G_RP_LockFor( name, qfalse );
		if ( i >= 0 ) {
			rpLocks[i].fails = 0;
		}
	}
	G_LogPrintf( "Roleplay: %s logged in as client %i\n", acc.name, clientNum );
	G_RP_LogIn( clientNum, &acc );
	G_RP_Msg( clientNum, va( "Logged in as %s", acc.name ) );
}

static void G_RP_ChangePassword( gentity_t *ent ) {
	const int clientNum = ent - g_entities;
	rpClient_t *c = &rpc[clientNum];
	char proofHex[128], keyHex[128], challenge[VRRP_CHALLENGE_HEX + 1];
	byte proof[VRC_SHA256_LEN], want[VRC_SHA256_LEN], key[VRRP_KEY_LEN];

	trap->Argv( 1, proofHex, sizeof( proofHex ) );
	trap->Argv( 2, keyHex, sizeof( keyHex ) );
	Q_strncpyz( challenge, c->challenge, sizeof( challenge ) );
	G_RP_NewChallenge( clientNum );
	if ( !c->loggedIn ) {
		G_RP_Refused( clientNum, "Log in first", qfalse );
		return;
	}
	if ( G_RP_Locked( clientNum, c->acc.name ) ) {
		G_RP_Refused( clientNum, "Too many wrong passwords: wait a minute", qfalse );
		return;
	}
	if ( !VRC_FromHex( proofHex, proof, sizeof( proof ) ) || !VRC_FromHex( keyHex, key, sizeof( key ) ) ) {
		G_RP_Refused( clientNum, "Your game didn't send a proper key: update OpenJK VR", qfalse );
		return;
	}
	Q_strlwr( keyHex );
	VRRP_LoginProof( c->acc.key, va( "password %s", keyHex ), challenge, c->acc.name, want );	// (the new key's part of it)
	if ( !VRC_Equal( proof, want, sizeof( want ) ) ) {
		G_RP_Failed( clientNum, c->acc.name );
		G_RP_Refused( clientNum, "That isn't your current password", qtrue );
		return;
	}
	memcpy( c->acc.key, key, sizeof( key ) );
	G_RP_SaveAccount( &c->acc );
	trap->SendServerCommand( clientNum, "rppassok" );
	G_RP_Msg( clientNum, "Password changed" );
}

/*
================================================================================
rp: characters
================================================================================
*/

static int G_RP_FindChar( const rpAccount_t *acc, const char *arg ) {
	char key[RP_NAME_BUF], other[RP_NAME_BUF];
	int i;

	if ( arg[0] >= '0' && arg[0] <= '9' ) {	// a number (no name is all digits)
		const char *p = arg;
		while ( *p >= '0' && *p <= '9' ) {
			p++;
		}
		if ( !*p ) {
			i = atoi( arg ) - 1;
			return ( i >= 0 && i < acc->numChars ) ? i : -1;
		}
	}
	G_RP_NameKey( arg, key, sizeof( key ) );
	for ( i = 0; i < acc->numChars; i++ ) {
		G_RP_NameKey( acc->chars[i].name, other, sizeof( other ) );
		if ( !strcmp( key, other ) ) {
			return i;
		}
	}
	return -1;
}

static int G_RP_FactionNamed( const char *name ) {
	int f;

	for ( f = 0; f < RPF_NUM; f++ ) {
		if ( !Q_stricmp( name, rpFactions[f] ) || ( name[0] && !Q_stricmpn( name, rpFactions[f], strlen( name ) ) ) ) {
			return f;
		}
	}
	if ( !Q_stricmp( name, "hero" ) ) return RPF_HEROES;
	if ( !Q_stricmp( name, "villain" ) ) return RPF_VILLAINS;
	return ( name[0] >= '1' && name[0] <= '3' && !name[1] ) ? name[0] - '1' : -1;
}

static void G_RP_NewChar( int clientNum, const char *rawName ) {
	rpClient_t *c = &rpc[clientNum];
	char name[RP_NAME_BUF];
	const char *problem;
	const int limit = G_RP_MostChars();
	rpChar_t *ch;
	int k;

	G_RP_CleanText( rawName, name, sizeof( name ) );
	if ( c->acc.numChars >= limit ) {
		G_RP_Msg( clientNum, va( "You have %i characters, the most this server keeps: delete one first", limit ) );
		return;
	}
	problem = G_RP_NameProblem( name );
	if ( problem ) {
		G_RP_Msg( clientNum, problem );
		return;
	}
	if ( G_RP_IndexFind( name ) >= 0 ) {
		G_RP_Msg( clientNum, "Someone on this server already has a character by that name" );
		return;
	}
	if ( !G_RP_IndexAdd( name, c->acc.name ) ) {
		G_RP_Msg( clientNum, "This server's list of characters is full" );
		return;
	}
	ch = &c->acc.chars[c->acc.numChars++];
	memset( ch, 0, sizeof( *ch ) );
	Q_strncpyz( ch->name, name, sizeof( ch->name ) );
	ch->faction = RPF_NEUTRAL;
	G_RP_RememberLook( clientNum );	// it starts out looking as they do
	for ( k = 0; k < RP_LOOK_KEYS; k++ ) {
		G_RP_CleanText( c->lookBase[k], ch->look[k], sizeof( ch->look[k] ) );	// (as stored: what it's read back as, and nothing more)
	}
	G_RP_Msg( clientNum, va( "%s" S_COLOR_WHITE " made", ch->name ) );
	G_RP_Play( clientNum, c->acc.numChars - 1, (qboolean)( c->active >= 0 ) );
}

static void G_RP_DeleteChar( rpAccount_t *acc, int index ) {
	int i;

	G_RP_IndexRemove( acc->chars[index].name, acc->name );
	for ( i = index; i < acc->numChars - 1; i++ ) {
		acc->chars[i] = acc->chars[i + 1];
	}
	acc->numChars--;
	if ( acc->last == index ) {
		acc->last = -1;
	} else if ( acc->last > index ) {
		acc->last--;
	}
}

static void G_RP_Rp( gentity_t *ent ) {
	const int clientNum = ent - g_entities;
	rpClient_t *c = &rpc[clientNum];
	char sub[64], arg[MAX_TOKEN_CHARS];
	const char *busy;
	int i;

	trap->Argv( 1, sub, sizeof( sub ) );
	if ( !Q_stricmp( sub, "look" ) ) {	// anyone may look
		trap->Argv( 2, arg, sizeof( arg ) );
		i = G_RP_ClientArg( arg );
		if ( i < 0 ) {
			G_RP_Print( clientNum, "No one by that name or number" );
		} else {
			const rpChar_t *ch = G_RP_Active( i );
			if ( !ch ) {
				G_RP_Print( clientNum, va( "%s" S_COLOR_WHITE " isn't playing a character", g_entities[i].client->pers.netname ) );
			} else {
				G_RP_Print( clientNum, va( "%s" S_COLOR_WHITE " (%s)%s%s%s%s", ch->name, rpFactions[ch->faction],
					ch->desc[0] ? ": " : "", ch->desc, ch->bio[0] ? "\n" : "", ch->bio ) );
			}
		}
		return;
	}
	if ( !Q_stricmp( sub, "admin" ) || !Q_stricmp( sub, "admins" ) ) {
		// the server's console's alone (G_RPConsoleCommand: by rcon, its password typed): a player's
		// own is turned away, roleplay on or off, and goes in the log (a listen server's host's never
		// comes here - the console's takes it first)
		G_RP_Msg( clientNum, "Only the server's owner makes admins: rcon rp admin <account> on, with its rcon password" );
		G_LogPrintf( "Roleplay: %s" S_COLOR_WHITE " (client %i%s%s) typed rp %s - refused: only the console or rcon makes admins\n",
			c->playerName[0] ? c->playerName : ent->client->pers.netname, clientNum,	// (their own name, not a character's)
			c->loggedIn ? ", account " : "", c->loggedIn ? c->acc.name : "", sub );
		return;
	}
	if ( !G_RPOn() ) {
		G_RP_Msg( clientNum, "Roleplay is off on this server right now" );
		return;
	}
	if ( !Q_stricmp( sub, "giveup" ) ) {
		G_RPGiveUpCommand( ent );	// (down: g_vrdown.c)
		return;
	}
	if ( !c->loggedIn ) {
		G_RP_Msg( clientNum, "Log in first: /login <account> <password>, or /new <account> <password> for a new one" );
		return;
	}
	if ( !Q_stricmp( sub, "list" ) || !sub[0] ) {
		G_RP_Print( clientNum, va( "Account %s, %i of %i characters:", c->acc.name, c->acc.numChars, G_RP_MostChars() ) );
		for ( i = 0; i < c->acc.numChars; i++ ) {
			G_RP_Print( clientNum, va( " %i  %s" S_COLOR_WHITE " (%s)%s", i + 1, c->acc.chars[i].name, rpFactions[c->acc.chars[i].faction],
				( i == c->active ) ? " - playing" : "" ) );
		}
		G_RP_Print( clientNum, "rp new <name> | play <name or number> | faction heroes|villains|neutral | desc <text> | bio <text> | delete <name> | giveup" );
		return;
	}
	if ( !Q_stricmp( sub, "new" ) ) {
		busy = ( c->active >= 0 ) ? G_RP_BusyReason( clientNum, qtrue ) : NULL;
		if ( busy ) {
			G_RP_Msg( clientNum, busy );
			return;
		}
		G_RP_NewChar( clientNum, ConcatArgs( 2 ) );
		return;
	}
	if ( !Q_stricmp( sub, "play" ) ) {
		Q_strncpyz( arg, ConcatArgs( 2 ), sizeof( arg ) );
		i = Q_stricmp( arg, "none" ) ? G_RP_FindChar( &c->acc, arg ) : -1;
		if ( i < 0 && Q_stricmp( arg, "none" ) ) {
			G_RP_Msg( clientNum, "You have no character by that name" );
			return;
		}
		if ( i == c->active ) {
			return;
		}
		busy = G_RP_BusyReason( clientNum, qtrue );
		if ( busy ) {
			G_RP_Msg( clientNum, busy );
			return;
		}
		G_RP_Play( clientNum, i, qtrue );
		if ( i >= 0 ) {
			G_RP_Msg( clientNum, va( "You're playing %s", c->acc.chars[i].name ) );
		}
		return;
	}
	if ( !Q_stricmp( sub, "delete" ) ) {
		i = G_RP_FindChar( &c->acc, ConcatArgs( 2 ) );
		if ( i < 0 ) {
			G_RP_Msg( clientNum, "You have no character by that name" );
			return;
		}
		if ( i == c->active && G_RPDowned( ent ) ) {
			G_RP_Msg( clientNum, "Not while you're down" );	// (no way up but a hand, or giving up)
			return;
		}
		G_RP_Msg( clientNum, va( "%s" S_COLOR_WHITE " deleted", c->acc.chars[i].name ) );
		if ( i == c->active ) {
			c->active = -1;
			c->playGen++;
		} else if ( c->active > i ) {
			c->active--;
		}
		G_RP_DeleteChar( &c->acc, i );
		G_RP_SaveAccount( &c->acc );
		G_RP_WriteSession( clientNum );
		G_RP_Refresh( clientNum );
		G_RP_SendAccount( clientNum );
		return;
	}

	// the rest change the character being played
	if ( c->active < 0 || c->active >= c->acc.numChars ) {
		G_RP_Msg( clientNum, "Play a character first (rp play <name>), or make one (rp new <name>)" );
		return;
	}
	if ( !Q_stricmp( sub, "faction" ) ) {
		trap->Argv( 2, arg, sizeof( arg ) );
		i = G_RP_FactionNamed( arg );
		if ( i < 0 ) {
			G_RP_Msg( clientNum, "Factions: heroes, villains, neutral" );
			return;
		}
		if ( i != c->acc.chars[c->active].faction ) {
			busy = G_RP_BusyReason( clientNum, qfalse );
			if ( busy ) {
				G_RP_Msg( clientNum, busy );
				return;
			}
			c->acc.chars[c->active].faction = i;
			if ( ent->client ) {
				ent->client->rpProvoked = 0;	// (the NPCs' grudges were against the other side: g_vrdown.c)
			}
		}
		G_RP_Msg( clientNum, va( "%s" S_COLOR_WHITE " is with the %s", c->acc.chars[c->active].name, rpFactions[i] ) );
	} else if ( !Q_stricmp( sub, "desc" ) ) {
		G_RP_CleanText( ConcatArgs( 2 ), c->acc.chars[c->active].desc, sizeof( c->acc.chars[0].desc ) );
		G_RP_Msg( clientNum, "Description set" );
	} else if ( !Q_stricmp( sub, "bio" ) ) {
		G_RP_CleanText( ConcatArgs( 2 ), c->acc.chars[c->active].bio, sizeof( c->acc.chars[0].bio ) );
		G_RP_Msg( clientNum, "Bio set" );
	} else {
		G_RP_Print( clientNum, "rp list | new | play | faction | desc | bio | delete | look | giveup" );
		return;
	}
	G_RP_SaveAccount( &c->acc );
	G_RP_SendAccount( clientNum );
}

/*
================================================================================
Companions: rpcomp (the Companions page, ui_vrcomp.c; the wheel's orders,
g_vrcomp.c)
================================================================================
*/

// Whether a client may change their companions now (each change is the
// account's file written): RP_COMP_EDITS in RP_COMP_EDIT_TIME at most.
static qboolean G_RP_CompEditAllowed( int clientNum ) {
	rpClient_t *c = &rpc[clientNum];

	if ( !c->compEditTime || level.time - c->compEditTime >= RP_COMP_EDIT_TIME ) {
		c->compEditTime = level.time ? level.time : 1;
		c->compEdits = 0;
	}
	if ( ++c->compEdits > RP_COMP_EDITS ) {
		G_RP_Msg( clientNum, "Not so fast: wait a moment" );
		return qfalse;
	}
	return qtrue;
}

// how many of a character's companions come out
static int G_RP_CompsOut( const rpChar_t *ch ) {
	int k, n = 0;

	for ( k = 0; k < ch->numComps; k++ ) {
		n += ch->comps[k].out ? 1 : 0;
	}
	return n;
}

// rpcomp - the companions of the character a client plays (numbered from 1):
//   rpcomp list
//   rpcomp set <number> "<name>" "<type>" "<look>" <r> <g> <b>   (the next number: a new one)
//   rpcomp del <number>
//   rpcomp out <number> 0|1        (whether it comes out with them: RP_COMP_OUT_MAX at most)
//   rpcomp order <what>            (the wheel's Companions ring: g_vrcomp.c)
static void G_RP_Comp( gentity_t *ent ) {
	const int clientNum = ent - g_entities;
	rpClient_t *c = &rpc[clientNum];
	rpChar_t *ch;
	rpComp_t *cp;
	char sub[16], arg[MAX_TOKEN_CHARS];
	const char *problem;
	int number, k;

	trap->Argv( 1, sub, sizeof( sub ) );
	if ( !Q_stricmp( sub, "order" ) ) {
		G_RPCompOrder( ent );
		return;
	}
	if ( !G_RPOn() ) {
		G_RP_Msg( clientNum, "Roleplay is off on this server right now" );
		return;
	}
	ch = G_RP_Active( clientNum );
	if ( !ch ) {
		G_RP_Msg( clientNum, c->loggedIn ? "Companions are a character's: play one first" : "Log in and play a character first" );
		return;
	}
	if ( !Q_stricmp( sub, "list" ) || !sub[0] ) {
		G_RP_Print( clientNum, va( "%s" S_COLOR_WHITE "'s companions, %i of %i:", ch->name, ch->numComps, RP_COMP_MAX ) );
		for ( k = 0; k < ch->numComps; k++ ) {
			const int wait = G_RPCompWait( clientNum, k );
			cp = &ch->comps[k];
			G_RP_Print( clientNum, va( " %i  %s" S_COLOR_WHITE " - %s%s%s%s%s", k + 1, cp->name, cp->type, cp->look[0] ? ", looks " : "", cp->look,
				cp->out ? " - comes out" : "", wait ? va( " (knocked out: %i s)", wait ) : "" ) );
		}
		G_RP_Print( clientNum, "rpcomp set <number> <name> <type> <look> <r> <g> <b> | del <number> | out <number> 0|1 | order <what>" );
		G_RPSendComps( clientNum );
		return;
	}
	trap->Argv( 2, arg, sizeof( arg ) );
	number = ( arg[0] >= '1' && arg[0] <= '9' && ( !arg[1] || ( arg[1] >= '0' && arg[1] <= '9' && !arg[2] ) ) ) ? atoi( arg ) : 0;
	if ( !Q_stricmp( sub, "set" ) ) {
		char name[RP_NAME_BUF], type[RP_COMP_TYPE_SIZE], look[MAX_QPATH];
		if ( number < 1 || number > ch->numComps + 1 ) {
			G_RP_Msg( clientNum, "No companion by that number" );
			return;
		}
		if ( number > RP_COMP_MAX ) {
			G_RP_Msg( clientNum, va( "%s" S_COLOR_WHITE " has %i companions, the most there are: delete one first", ch->name, RP_COMP_MAX ) );
			return;
		}
		if ( !G_RP_CompEditAllowed( clientNum ) ) {
			return;
		}
		trap->Argv( 3, arg, sizeof( arg ) );
		G_RP_CleanText( arg, name, sizeof( name ) );
		problem = G_RP_NameProblem( name );
		if ( problem ) {
			G_RP_Msg( clientNum, problem );
			return;
		}
		trap->Argv( 4, arg, sizeof( arg ) );
		Q_strncpyz( type, arg, sizeof( type ) );
		Q_strlwr( type );
		problem = ( strlen( arg ) >= sizeof( type ) ) ? "Pick what it fights as from the list" : G_RPCompTypeProblem( type );
		if ( problem ) {
			G_RP_Msg( clientNum, problem );
			return;
		}
		trap->Argv( 5, arg, sizeof( arg ) );
		Q_strncpyz( look, arg, sizeof( look ) );
		problem = ( strlen( arg ) >= sizeof( look ) ) ? "That look's name isn't one the game takes" : G_RPCompLookProblem( look );
		if ( problem ) {
			G_RP_Msg( clientNum, problem );
			return;
		}
		cp = &ch->comps[number - 1];
		if ( number > ch->numComps ) {
			memset( cp, 0, sizeof( *cp ) );
			cp->out = (qboolean)( G_RP_CompsOut( ch ) < RP_COMP_OUT_MAX );	// (a new one comes out, if there's room)
			ch->numComps++;
		}
		Q_strncpyz( cp->name, name, sizeof( cp->name ) );
		Q_strncpyz( cp->type, type, sizeof( cp->type ) );
		Q_strncpyz( cp->look, look, sizeof( cp->look ) );
		for ( k = 0; k < 3; k++ ) {
			trap->Argv( 6 + k, arg, sizeof( arg ) );
			cp->rgb[k] = (byte)Com_Clampi( 0, 255, arg[0] ? atoi( arg ) : 255 );
		}
		G_RP_Msg( clientNum, va( "%s" S_COLOR_WHITE " saved", cp->name ) );
	} else if ( !Q_stricmp( sub, "del" ) || !Q_stricmp( sub, "delete" ) ) {
		if ( number < 1 || number > ch->numComps ) {
			G_RP_Msg( clientNum, "No companion by that number" );
			return;
		}
		if ( !G_RP_CompEditAllowed( clientNum ) ) {
			return;
		}
		G_RP_Msg( clientNum, va( "%s" S_COLOR_WHITE " deleted", ch->comps[number - 1].name ) );
		for ( k = number - 1; k < ch->numComps - 1; k++ ) {
			ch->comps[k] = ch->comps[k + 1];
		}
		ch->numComps--;
		memset( &ch->comps[ch->numComps], 0, sizeof( ch->comps[0] ) );
		G_RPCompDeleted( clientNum, number - 1 );	// (one out goes; the others' numbers move up)
	} else if ( !Q_stricmp( sub, "out" ) ) {
		qboolean want;
		if ( number < 1 || number > ch->numComps ) {
			G_RP_Msg( clientNum, "No companion by that number" );
			return;
		}
		trap->Argv( 3, arg, sizeof( arg ) );
		want = (qboolean)( !Q_stricmp( arg, "1" ) || !Q_stricmp( arg, "on" ) );
		cp = &ch->comps[number - 1];
		if ( want == cp->out ) {
			G_RPSendComps( clientNum );	// (as it is: the page shows it so)
			return;
		}
		if ( want && G_RP_CompsOut( ch ) >= RP_COMP_OUT_MAX ) {
			G_RP_Msg( clientNum, va( "%i come out at most: untick one first", RP_COMP_OUT_MAX ) );
			G_RPSendComps( clientNum );
			return;
		}
		if ( !G_RP_CompEditAllowed( clientNum ) ) {
			G_RPSendComps( clientNum );
			return;
		}
		cp->out = want;
		G_RP_Msg( clientNum, va( want ? "%s" S_COLOR_WHITE " comes out with you" : "%s" S_COLOR_WHITE " stays in", cp->name ) );
	} else {
		G_RP_Print( clientNum, "usage: rpcomp list | set <number> <name> <type> <look> <r> <g> <b> | del <number> | out <number> 0|1 | order <what>" );
		return;
	}
	G_RP_SaveAccount( &c->acc );
	G_RPSendComps( clientNum );
}

// what a character's card shows: rpcard <client>
static qboolean G_RP_CardAllowed( int clientNum ) {
	rpClient_t *c = &rpc[clientNum];

	if ( level.time - c->cardTime >= 1000 ) {
		c->cardTime = level.time;
		c->cards = 0;
	}
	return (qboolean)( ++c->cards <= RP_CARDS_PER_SEC );
}

static void G_RP_Card( gentity_t *ent, qboolean bio ) {
	const int clientNum = ent - g_entities;
	char arg[16];
	int target;
	const rpChar_t *ch;

	trap->Argv( 1, arg, sizeof( arg ) );
	target = atoi( arg );
	if ( !arg[0] || target < 0 || target >= MAX_CLIENTS || !g_entities[target].inuse || !g_entities[target].client ||
		!G_RP_CardAllowed( clientNum ) ) {
		return;
	}
	ch = G_RP_Active( target );
	if ( bio ) {
		trap->SendServerCommand( clientNum, va( "rpbio %i \"%s\"", target, ch ? ch->bio : "" ) );
	} else {
		trap->SendServerCommand( clientNum, va( "rpcard %i %i \"%s\"", target, ch ? ch->faction : -1, ch ? ch->desc : "" ) );
	}
}

/*
================================================================================
Chat
================================================================================
*/

enum { RPCHAT_NONE, RPCHAT_TALK, RPCHAT_ACTION, RPCHAT_OOC };

static qboolean G_RP_Spectating( const gentity_t *ent ) {
	const playerState_t *ps = &ent->client->ps;
	return (qboolean)( ent->client->sess.sessionTeam == TEAM_SPECTATOR || ps->pm_type == PM_SPECTATOR || ( ps->pm_flags & PMF_FOLLOW ) );
}

// Does other hear ent talk here? As voice chat goes: players hear the
// players near where they are (a spectator: where it watches from), and
// spectators talk among themselves.
static qboolean G_RP_Hears( const gentity_t *ent, const gentity_t *other ) {
	const float range = (float)g_rpTalkRange.integer;

	if ( G_RP_Spectating( ent ) ) {
		return G_RP_Spectating( other );
	}
	if ( range <= 0.0f ) {
		return qtrue;
	}
	return (qboolean)( DistanceSquared( ent->client->ps.origin, other->client->ps.origin ) <= range * range );
}

static void G_RP_SendChat( const gentity_t *ent, const char *line, int kind, qboolean local, const char *bubble ) {
	int i;

	for ( i = 0; i < level.maxclients; i++ ) {
		gentity_t *other = &g_entities[i];
		if ( !other->inuse || !other->client || other->client->pers.connected != CON_CONNECTED ) {
			continue;
		}
		if ( local && !G_RP_Hears( ent, other ) ) {
			continue;
		}
		// our clients take the speaker, the kind and what goes in the speech
		// bubble; other clients read only the line
		if ( bubble ) {
			trap->SendServerCommand( i, va( "chat \"%s\" %i %i \"%s\"", line, ent->s.number, kind, bubble ) );
		} else {
			trap->SendServerCommand( i, va( "chat \"%s\" %i %i", line, ent->s.number, kind ) );
		}
	}
}

static void G_RP_Line( gentity_t *ent, int kind, const char *rawText ) {
	char text[MAX_SAY_TEXT], line[MAX_SAY_TEXT + 128], bubble[MAX_SAY_TEXT + 4];
	const char *name = ent->client->pers.netname;

	Q_strncpyz( text, rawText, sizeof( text ) );
	Q_strstrip( text, "\n\r\"", "   " );
	if ( !text[0] ) {
		return;
	}
	switch ( kind ) {
	case RPCHAT_ACTION:
		Com_sprintf( line, sizeof( line ), S_COLOR_YELLOW "* %s" S_COLOR_YELLOW " %s", name, text );
		Com_sprintf( bubble, sizeof( bubble ), "*%s*", text );
		G_LogPrintf( "me: %s: %s\n", name, text );
		break;
	case RPCHAT_OOC:
		// (out of character: the player's own name, as everyone sees it)
		if ( rpc[ent - g_entities].playerName[0] ) {
			name = rpc[ent - g_entities].playerName;
		}
		Com_sprintf( line, sizeof( line ), S_COLOR_GREY "[OOC] " S_COLOR_WHITE "%s" S_COLOR_WHITE EC ": " S_COLOR_WHITE "%s", name, text );
		G_LogPrintf( "ooc: %s: %s\n", name, text );
		break;
	default:
		Com_sprintf( line, sizeof( line ), "%s" S_COLOR_WHITE EC ": " S_COLOR_GREEN "%s", name, text );
		Q_strncpyz( bubble, text, sizeof( bubble ) );
		G_LogPrintf( "say: %s: %s\n", name, text );
		break;
	}
	if ( dedicated.integer ) {
		trap->Print( "%s\n", line );
	}
	G_RP_SendChat( ent, line, kind, (qboolean)( kind != RPCHAT_OOC ), ( kind == RPCHAT_OOC ) ? NULL : bubble );
}

// G_Say asks first: with roleplay on, talk is local and team chat is out
// of character. (qtrue: done.)
qboolean G_RPChat( gentity_t *ent, gentity_t *target, int mode, const char *text ) {
	if ( !G_RPOn() || target || !ent || !ent->client || ( mode != SAY_ALL && mode != SAY_TEAM ) ) {
		return qfalse;
	}
	G_RP_Line( ent, ( mode == SAY_TEAM ) ? RPCHAT_OOC : RPCHAT_TALK, text );
	return qtrue;
}

static void G_RP_Me( gentity_t *ent ) {
	char *p;

	if ( trap->Argc() < 2 ) {
		return;
	}
	p = ConcatArgs( 1 );
	if ( strlen( p ) >= MAX_SAY_TEXT ) {
		p[MAX_SAY_TEXT - 1] = '\0';
	}
	if ( G_RPOn() ) {
		G_RP_Line( ent, RPCHAT_ACTION, p );
	} else {
		char text[MAX_SAY_TEXT], line[MAX_SAY_TEXT + 128];
		Q_strncpyz( text, p, sizeof( text ) );
		Q_strstrip( text, "\n\r\"", "   " );
		if ( !text[0] ) {
			return;
		}
		Com_sprintf( line, sizeof( line ), S_COLOR_YELLOW "* %s" S_COLOR_YELLOW " %s", ent->client->pers.netname, text );
		G_LogPrintf( "me: %s: %s\n", ent->client->pers.netname, text );
		G_RP_SendChat( ent, line, RPCHAT_NONE, qfalse, NULL );	// (no bubbles: everyone sees it)
	}
}

static void G_RP_Ooc( gentity_t *ent ) {
	char *p;

	if ( trap->Argc() < 2 ) {
		return;
	}
	p = ConcatArgs( 1 );
	if ( strlen( p ) >= MAX_SAY_TEXT ) {
		p[MAX_SAY_TEXT - 1] = '\0';
	}
	if ( G_RPOn() ) {
		G_RP_Line( ent, RPCHAT_OOC, p );
	} else {
		G_Say( ent, NULL, SAY_ALL, p );
	}
}

/*
================================================================================
The game's hooks
================================================================================
*/

// Client commands; qtrue: one of ours.
void Cmd_VRRP_f( gentity_t *ent ) {
	char cmd[32];

	trap->Argv( 0, cmd, sizeof( cmd ) );
	if ( !Q_stricmp( cmd, "me" ) ) {
		G_RP_Me( ent );	// (roleplay off, or not ready: as plain chat)
		return;
	}
	if ( !Q_stricmp( cmd, "ooc" ) ) {
		G_RP_Ooc( ent );
		return;
	}
	if ( !rp.ready ) {
		if ( !Q_stricmp( cmd, "rp" ) ) {
			G_RP_Print( ent - g_entities, "Roleplay isn't working on this server: its owner's to look at rp/server.txt" );
		}
		return;
	}
	if ( !Q_stricmp( cmd, "rpver" ) ) {
		G_RP_Hello( ent );
	} else if ( !Q_stricmp( cmd, "rpnew" ) ) {
		G_RP_NewAccount( ent );
	} else if ( !Q_stricmp( cmd, "rplogin" ) ) {
		G_RP_Login( ent );
	} else if ( !Q_stricmp( cmd, "rppass" ) ) {
		G_RP_ChangePassword( ent );
	} else if ( !Q_stricmp( cmd, "rplogout" ) ) {
		if ( G_RPDowned( ent ) ) {
			G_RP_Msg( ent - g_entities, "Not while you're down" );	// (no way up but a hand, or giving up)
			return;
		}
		G_RP_LogOut( ent - g_entities, rpc[ent - g_entities].loggedIn ? "Logged out" : NULL );
	} else if ( !Q_stricmp( cmd, "rp" ) ) {
		G_RP_Rp( ent );
	} else if ( !Q_stricmp( cmd, "rpcard" ) ) {
		G_RP_Card( ent, qfalse );
	} else if ( !Q_stricmp( cmd, "rpbio" ) ) {
		G_RP_Card( ent, qtrue );
	} else if ( !Q_stricmp( cmd, "rpcomp" ) ) {
		G_RP_Comp( ent );
	}
}

// ClientUserinfoChanged, before G_RPName: netname, the player's own name
// (cleaned), changed while the name shown is a character's - announced and
// held back as any rename is (the name shown doesn't change, so the game's
// own rename check doesn't see it).
void G_RPOwnRename( int clientNum, char *userinfo, char *netname, int size ) {
	rpClient_t *c;
	gclient_t *cl;

	if ( clientNum < 0 || clientNum >= MAX_CLIENTS || !( cl = g_entities[clientNum].client ) ) {
		return;
	}
	c = &rpc[clientNum];
	if ( !c->named || !G_RP_Active( clientNum ) || !c->playerName[0] || !strcmp( c->playerName, netname ) ||
		cl->pers.connected != CON_CONNECTED ) {
		return;
	}
	if ( cl->pers.netnameTime > level.time ) {
		trap->SendServerCommand( clientNum, va( "print \"%s\n\"", G_GetStringEdString( "MP_SVGAME", "NONAMECHANGE" ) ) );
		Info_SetValueForKey( userinfo, "name", c->playerName );
		trap->SetUserinfo( clientNum, userinfo );
		Q_strncpyz( netname, c->playerName, size );
		return;
	}
	trap->SendServerCommand( -1, va( "print \"%s" S_COLOR_WHITE " %s %s" S_COLOR_WHITE " (out of character)\n\"", c->playerName,
		G_GetStringEdString( "MP_SVGAME", "PLRENAME" ), netname ) );
	G_LogPrintf( "ClientRename: %i [%s] (%s) \"%s^7\" -> \"%s^7\" (out of character)\n", clientNum, cl->sess.IP, cl->pers.guid, c->playerName, netname );
	cl->pers.netnameTime = level.time + 5000;
}

// ClientUserinfoChanged: the name shown. netname holds the player's own
// (cleaned); while they play a character, it becomes the character's.
// qtrue: the name shown is or was a character's, so the change isn't a
// rename to announce (or hold back).
qboolean G_RPName( int clientNum, char *netname, int size ) {
	rpClient_t *c;
	const rpChar_t *ch;
	qboolean was;

	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return qfalse;
	}
	c = &rpc[clientNum];
	was = c->named;
	Q_strncpyz( c->playerName, netname, sizeof( c->playerName ) );
	ch = G_RP_Active( clientNum );
	c->named = (qboolean)( ch != NULL );
	if ( ch ) {
		Q_strncpyz( netname, ch->name, size );
	}
	return (qboolean)( was || c->named );
}

// ClientUserinfoChanged, first: what the player put on since taking the
// character up is the character's now.
void G_RPLookSeen( int clientNum, const char *userinfo ) {
	rpClient_t *c;
	rpChar_t *ch;
	qboolean changed = qfalse;
	int k;

	if ( clientNum < 0 || clientNum >= MAX_CLIENTS || !( ch = G_RP_Active( clientNum ) ) ) {
		return;
	}
	c = &rpc[clientNum];
	for ( k = 0; k < RP_LOOK_KEYS; k++ ) {
		char now[MAX_QPATH];
		Q_strncpyz( now, Info_ValueForKey( userinfo, rpLookKeys[k] ), sizeof( now ) );	// (as kept: a longer one's cut alike)
		if ( strcmp( now, c->lookBase[k] ) ) {
			Q_strncpyz( c->lookBase[k], now, sizeof( c->lookBase[k] ) );
			G_RP_CleanText( now, ch->look[k], sizeof( ch->look[k] ) );
			changed = qtrue;
		}
	}
	if ( changed ) {
		G_RP_SaveAccount( &c->acc );
	}
}

// A VR player's two sabers changed over (g_vrholster.c: the second is the
// first now, and their userinfo's names and colours with them): their
// character's saber colours change over too, and what they wore when they
// took it up - so it's still the character's colours they wear, each on the
// saber it was on.
void G_RPSwapSaberColors( int clientNum ) {
	const int k1 = G_RP_LookIndex( "color1" ), k2 = G_RP_LookIndex( "color2" );
	rpClient_t *c;
	rpChar_t *ch;
	char t[MAX_QPATH];

	if ( clientNum < 0 || clientNum >= MAX_CLIENTS || k1 < 0 || k2 < 0 ) {
		return;
	}
	c = &rpc[clientNum];
	Q_strncpyz( t, c->lookBase[k1], sizeof( t ) );
	Q_strncpyz( c->lookBase[k1], c->lookBase[k2], sizeof( c->lookBase[k1] ) );
	Q_strncpyz( c->lookBase[k2], t, sizeof( c->lookBase[k2] ) );
	if ( !( ch = G_RP_Active( clientNum ) ) ) {
		return;
	}
	Q_strncpyz( t, ch->look[k1], sizeof( t ) );
	Q_strncpyz( ch->look[k1], ch->look[k2], sizeof( ch->look[k1] ) );
	Q_strncpyz( ch->look[k2], t, sizeof( ch->look[k2] ) );
	G_RP_SaveAccount( &c->acc );
}

// ClientUserinfoChanged: what the character wears instead (qtrue: changed).
qboolean G_RPLookValue( int clientNum, const char *key, char *value, int size ) {
	const rpChar_t *ch;
	const int k = G_RP_LookIndex( key );

	if ( clientNum < 0 || clientNum >= MAX_CLIENTS || k < 0 || !( ch = G_RP_Active( clientNum ) ) || !ch->look[k][0] ) {
		return qfalse;
	}
	Q_strncpyz( value, ch->look[k], size );
	return qtrue;
}

// ClientUserinfoChanged and ClientSpawn: the character's own skin colours.
void G_RPLookRGB( int clientNum, int *rgba ) {
	static const char *keys[3] = { "char_color_red", "char_color_green", "char_color_blue" };
	char value[16];
	int c;

	for ( c = 0; c < 3; c++ ) {
		if ( G_RPLookValue( clientNum, keys[c], value, sizeof( value ) ) ) {
			rgba[c] = Com_Clampi( 0, 255, atoi( value ) );
		}
	}
}

void G_RPClientConnect( int clientNum, qboolean firstTime, qboolean isBot ) {
	rpClient_t *c = &rpc[clientNum];
	char sess[64];

	memset( c, 0, sizeof( *c ) );
	c->active = -1;
	if ( firstTime || isBot ) {
		trap->Cvar_Register( NULL, va( "vrrp%i", clientNum ), "", CVAR_INTERNAL );
		trap->Cvar_Set( va( "vrrp%i", clientNum ), "" );
		return;
	}
	// back after a map change: still logged in
	trap->Cvar_VariableStringBuffer( va( "vrrp%i", clientNum ), sess, sizeof( sess ) );
	if ( sess[0] ) {
		static rpAccount_t acc;
		char name[RP_ACCOUNT_MAX + 1];
		const char *p = sess;
		int active;
		Q_strncpyz( name, COM_Parse( &p ), sizeof( name ) );
		active = atoi( COM_Parse( &p ) );
		c->capable = (qboolean)( atoi( COM_Parse( &p ) ) == 1 );	// (an older server's session doesn't say: its hello will)
		if ( G_RP_LoadAccount( name, &acc ) ) {
			c->loggedIn = qtrue;
			c->acc = acc;
			c->active = ( active >= 0 && active < acc.numChars ) ? active : -1;
			G_RP_RememberLook( clientNum );
		}
	}
}

void G_RPClientDisconnect( int clientNum ) {
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return;
	}
	G_RPCompOwnerGone( clientNum );	// (their companions go with them: g_vrcomp.c)
	memset( &rpc[clientNum], 0, sizeof( rpc[0] ) );
	rpc[clientNum].active = -1;
	trap->Cvar_Set( va( "vrrp%i", clientNum ), "" );
}

// The serverinfo "vrRP": "0" - roleplay not ready here - or whether it's
// on, and the settings the Roleplay menu's Server page shows.
static void G_RP_ServerInfo( void ) {
	if ( !rp.ready ) {
		trap->Cvar_Set( "vrRP", "0" );	// (not ready: off, whatever g_rp says)
		return;
	}
	trap->Cvar_Set( "vrRP", va( "%i %i %i %i %i %i %i", g_rp.integer ? 1 : 0, G_RP_MostChars(), g_rpTalkRange.integer,
		g_rpDowned.integer ? 1 : 0, Com_Clampi( 0, RP_COMP_OUT_MAX, g_rpCompanions.integer ), Q_max( 0, g_rpCompanionsMax.integer ),
		g_rpCompanionModels.integer ? 1 : 0 ) );
}

// g_rpCharacters, g_rpTalkRange or g_rpDowned changed: the serverinfo follows.
void G_RPPublishSettings( void ) {
	G_RP_ServerInfo();
}

// g_rp changed (or the game started): the serverinfo follows it, and
// everyone in the game is shown as they now should be.
void G_RPPublish( void ) {
	const qboolean on = (qboolean)( g_rp.integer != 0 );
	int i;

	G_RP_ServerInfo();
	if ( !rp.ready || on == rp.on ) {
		return;
	}
	rp.on = on;
	for ( i = 0; i < level.maxclients; i++ ) {
		gentity_t *ent = &g_entities[i];
		if ( !ent->inuse || !ent->client || ent->client->pers.connected == CON_DISCONNECTED ) {
			continue;
		}
		if ( on && rpc[i].loggedIn ) {
			G_RP_RememberLook( i );	// what they wore with it off is theirs, not the character's
		}
		ClientUserinfoChanged( i );
		G_RP_SendAccount( i );
	}
	trap->SendServerCommand( -1, va( "cp \"Roleplay is %s\n\"", on ? "on" : "off" ) );
}

void G_RPInit( void ) {
	int i;

	trap->Cvar_Register( NULL, "vrRP", "0", CVAR_SERVERINFO | CVAR_ROM );
	memset( &rp, 0, sizeof( rp ) );
	rp.bootTime = (int)time( NULL );
	memset( rpLocks, 0, sizeof( rpLocks ) );
	memset( rpMade, 0, sizeof( rpMade ) );
	if ( !G_RP_LoadServer() ) {
		G_RP_ServerInfo();
		return;	// (not ready: off, whatever g_rp says)
	}
	G_RP_IndexBuild();
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		rpc[i].active = -1;
	}
	rp.ready = qtrue;
	rp.on = (qboolean)( g_rp.integer != 0 );
	G_RP_ServerInfo();
}

void G_RPShutdown( void ) {
	int i;

	for ( i = 0; i < level.maxclients; i++ ) {
		if ( g_entities[i].inuse && g_entities[i].client && g_entities[i].client->pers.connected != CON_DISCONNECTED ) {
			G_RP_WriteSession( i );
		}
	}
	rp.ready = qfalse;
}

/*
================================================================================
Rcon: rp list | rename | delete | password | admin | admins
================================================================================
*/

static int G_RP_ClientArg( const char *arg ) {
	int i;
	char want[MAX_NETNAME], name[MAX_NETNAME];

	if ( arg[0] >= '0' && arg[0] <= '9' ) {
		i = atoi( arg );
		return ( i >= 0 && i < level.maxclients && g_entities[i].inuse && g_entities[i].client ) ? i : -1;
	}
	Q_strncpyz( want, arg, sizeof( want ) );
	Q_StripColor( want );
	for ( i = 0; i < level.maxclients; i++ ) {
		if ( !g_entities[i].inuse || !g_entities[i].client ) {
			continue;
		}
		Q_strncpyz( name, g_entities[i].client->pers.netname, sizeof( name ) );
		Q_StripColor( name );
		if ( !Q_stricmp( name, want ) ) {
			return i;
		}
		Q_strncpyz( name, rpc[i].playerName, sizeof( name ) );
		Q_StripColor( name );
		if ( name[0] && !Q_stricmp( name, want ) ) {
			return i;
		}
	}
	return -1;
}

// The account named, whether its player is here (online: their copy) or not.
static rpAccount_t *G_RP_AccountAnywhere( const char *name, int *clientNum ) {
	static rpAccount_t acc;
	char lower[RP_ACCOUNT_MAX + 1];
	int i;

	Q_strncpyz( lower, name, sizeof( lower ) );
	Q_strlwr( lower );
	*clientNum = -1;
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		if ( rpc[i].loggedIn && !Q_stricmp( rpc[i].acc.name, lower ) ) {
			*clientNum = i;
			return &rpc[i].acc;
		}
	}
	return G_RP_LoadAccount( lower, &acc ) ? &acc : NULL;
}

qboolean G_RPConsoleCommand( void ) {
	char sub[64], arg[MAX_TOKEN_CHARS];
	int i, n;

	trap->Argv( 1, sub, sizeof( sub ) );
	if ( !rp.ready && ( !Q_stricmp( sub, "list" ) || !Q_stricmp( sub, "rename" ) || !Q_stricmp( sub, "delete" ) || !Q_stricmp( sub, "password" ) ||
		!Q_stricmp( sub, "admin" ) || !Q_stricmp( sub, "admins" ) ) ) {
		trap->Print( "Roleplay isn't working: %s couldn't be read (mend it, or delete it to start over)\n", RP_SERVER_FILE );
		return qtrue;	// (a key made now would be over no salt)
	}
	if ( !Q_stricmp( sub, "list" ) ) {
		trap->Print( "num  player                account               character\n" );
		for ( i = 0; i < level.maxclients; i++ ) {
			const rpChar_t *ch;
			if ( !g_entities[i].inuse || !g_entities[i].client || g_entities[i].client->pers.connected == CON_DISCONNECTED ) {
				continue;
			}
			ch = ( rpc[i].loggedIn && rpc[i].active >= 0 && rpc[i].active < rpc[i].acc.numChars ) ? &rpc[i].acc.chars[rpc[i].active] : NULL;
			trap->Print( "%3i  %-20s  %-20s  %s (%s)%s\n", i, rpc[i].playerName[0] ? rpc[i].playerName : g_entities[i].client->pers.netname,
				rpc[i].loggedIn ? rpc[i].acc.name : "-", ch ? ch->name : "-", ch ? rpFactions[ch->faction] : "", G_RPAdmin( i ) ? "  - an admin" : "" );
		}
		trap->Print( "Roleplay is %s; %i characters on this server\n", G_RPOn() ? "on" : "off", rpIndexCount );
		return qtrue;
	}
	if ( !Q_stricmp( sub, "rename" ) ) {
		char name[RP_NAME_BUF];
		const char *problem;
		trap->Argv( 2, arg, sizeof( arg ) );
		i = G_RP_ClientArg( arg );
		G_RP_CleanText( ConcatArgs( 3 ), name, sizeof( name ) );
		if ( i < 0 || !rpc[i].loggedIn || rpc[i].active < 0 || rpc[i].active >= rpc[i].acc.numChars ) {
			trap->Print( "usage: rp rename <player> <new name> (a player playing a character)\n" );
			return qtrue;
		}
		problem = G_RP_NameProblem( name );
		if ( problem ) {
			trap->Print( "%s\n", problem );
			return qtrue;
		}
		{	// (a name another character has - theirs too - won't do; the same one in other colours will)
			char now[RP_NAME_BUF], want[RP_NAME_BUF];
			G_RP_NameKey( rpc[i].acc.chars[rpc[i].active].name, now, sizeof( now ) );
			G_RP_NameKey( name, want, sizeof( want ) );
			n = G_RP_IndexFind( name );
			if ( n >= 0 && strcmp( want, now ) ) {
				trap->Print( "Someone already has a character by that name\n" );
				return qtrue;
			}
		}
		G_RP_IndexRemove( rpc[i].acc.chars[rpc[i].active].name, rpc[i].acc.name );
		G_RP_IndexAdd( name, rpc[i].acc.name );	// (room: one just went)
		G_RP_Msg( i, va( "The server renamed %s" S_COLOR_WHITE " to %s", rpc[i].acc.chars[rpc[i].active].name, name ) );
		Q_strncpyz( rpc[i].acc.chars[rpc[i].active].name, name, sizeof( rpc[i].acc.chars[0].name ) );
		G_RP_SaveAccount( &rpc[i].acc );
		G_RP_Refresh( i );
		G_RP_SendAccount( i );
		trap->Print( "Renamed\n" );
		return qtrue;
	}
	if ( !Q_stricmp( sub, "delete" ) ) {
		rpAccount_t *acc;
		int who;
		trap->Argv( 2, arg, sizeof( arg ) );
		acc = G_RP_AccountAnywhere( arg, &who );
		if ( !acc ) {
			trap->Print( "usage: rp delete <account> <character> (no account %s)\n", arg );
			return qtrue;
		}
		i = G_RP_FindChar( acc, ConcatArgs( 3 ) );
		if ( i < 0 ) {
			trap->Print( "%s has no character by that name\n", acc->name );
			return qtrue;
		}
		trap->Print( "Deleted %s\n", acc->chars[i].name );
		if ( who >= 0 ) {
			G_RP_Msg( who, va( "The server deleted %s", acc->chars[i].name ) );
			if ( rpc[who].active == i ) {
				rpc[who].active = -1;
				rpc[who].playGen++;
			} else if ( rpc[who].active > i ) {
				rpc[who].active--;
			}
		}
		G_RP_DeleteChar( acc, i );
		G_RP_SaveAccount( acc );
		if ( who >= 0 ) {
			G_RP_WriteSession( who );
			G_RP_Refresh( who );
			G_RP_SendAccount( who );
		}
		return qtrue;
	}
	if ( !Q_stricmp( sub, "password" ) ) {
		rpAccount_t *acc;
		char temp[MAX_TOKEN_CHARS];
		int who;
		trap->Argv( 2, arg, sizeof( arg ) );
		{	// the rest, without the spaces around it (as the player's game takes a password)
			const char *t = ConcatArgs( 3 );
			int n;
			while ( *t == ' ' || *t == '\t' ) {
				t++;
			}
			Q_strncpyz( temp, t, sizeof( temp ) );
			for ( n = (int)strlen( temp ); n > 0 && ( temp[n - 1] == ' ' || temp[n - 1] == '\t' ); n-- ) {
				temp[n - 1] = '\0';
			}
		}
		acc = G_RP_AccountAnywhere( arg, &who );
		if ( !acc || !temp[0] ) {
			trap->Print( "usage: rp password <account> <temporary password>%s\n", acc ? "" : va( " (no account %s)", arg ) );
			return qtrue;
		}
		VRRP_LoginKey( temp, rp.salt, acc->name, acc->key );
		G_RP_SaveAccount( acc );
		{	// and nothing waits on old wrong guesses
			const int l = G_RP_LockFor( acc->name, qfalse );
			if ( l >= 0 ) {
				rpLocks[l].fails = 0;
				rpLocks[l].until = 0;
			}
		}
		trap->Print( "%s's password is now the temporary one: they log in with it, then set their own (New password, in the Roleplay menu)\n", acc->name );
		return qtrue;
	}
	if ( !Q_stricmp( sub, "admin" ) ) {
		rpAccount_t *acc;
		char how[16];
		qboolean on;
		int who = -1;
		trap->Argv( 2, arg, sizeof( arg ) );
		trap->Argv( 3, how, sizeof( how ) );
		acc = arg[0] ? G_RP_AccountAnywhere( arg, &who ) : NULL;
		if ( !acc || ( how[0] && Q_stricmp( how, "on" ) && Q_stricmp( how, "off" ) && Q_stricmp( how, "1" ) && Q_stricmp( how, "0" ) ) ) {
			trap->Print( "usage: rp admin <account> [on|off]%s\n", ( arg[0] && !acc ) ? va( " (no account %s)", arg ) : "" );
			return qtrue;
		}
		if ( !how[0] ) {
			trap->Print( "%s %s an admin\n", acc->name, acc->admin ? "is" : "isn't" );
			return qtrue;
		}
		on = (qboolean)( !Q_stricmp( how, "on" ) || !Q_stricmp( how, "1" ) );
		if ( on == acc->admin ) {
			trap->Print( "%s %s an admin already\n", acc->name, on ? "is" : "isn't" );
			return qtrue;
		}
		acc->admin = on;
		G_RP_SaveAccount( acc );
		G_LogPrintf( "Roleplay: %s %s an admin (rcon)\n", acc->name, on ? "made" : "is no longer" );
		trap->Print( on ? "%s is an admin now: logged in, they can use every cheat\n" : "%s isn't an admin any more\n", acc->name );
		if ( who >= 0 ) {
			if ( !on ) {
				G_RP_AdminOff( who );
			}
			G_RP_Msg( who, on ? "You're an admin on this server now: your cheats work here, and the Admin page is at the top of the Roleplay menu" :
				"You're no longer an admin on this server" );
		}
		return qtrue;
	}
	if ( !Q_stricmp( sub, "admins" ) ) {
		static char list[65536];
		const char *p;
		const int count = trap->FS_GetFileList( RP_ACCOUNT_DIR, ".txt", list, sizeof( list ) );
		n = 0;
		for ( i = 0, p = list; i < count && *p; i++, p += strlen( p ) + 1 ) {
			char name[RP_ACCOUNT_MAX + 1];
			const rpAccount_t *acc;
			int who;
			COM_StripExtension( p, name, sizeof( name ) );
			acc = G_RP_AccountAnywhere( name, &who );	// (a player's copy, if they're on)
			if ( acc && acc->admin ) {
				trap->Print( "%s%s\n", acc->name, ( who >= 0 && g_entities[who].client ) ? va( "  - here: %s", g_entities[who].client->pers.netname ) : "" );
				n++;
			}
		}
		trap->Print( "%i admin%s (rp admin <account> on|off)\n", n, n == 1 ? "" : "s" );
		return qtrue;
	}
	return qfalse;	// (on a listen server the host's own rp commands go on to the game as a client's)
}
