/*
===========================================================================
OpenJK VR (multiplayer) - roleplay mode, the server's side: the switch,
accounts and logins, characters and factions, and roleplay chat.

THE SWITCH. g_rp 1 turns roleplay on and 0 off, at any time, with no map
change; the serverinfo "vrRP" says which (and, there at all, that this
server knows roleplay). Off, the game plays as before; nothing is lost.

ACCOUNTS. A player makes one with /new <name> <password> and logs in with
/login <name> <password>. Their game (cg_vrrp.c) turns the password into a
key (bg_vrcrypt.c: PBKDF2 over this server's salt and the account name),
and logs in by answering a one-time challenge with it, so the password
never leaves their PC and what crosses the network can't be replayed. The
server keeps only the key, in rp/accounts/<name>.txt with the account's
characters. Five wrong passwords, and the account (and the connection)
wait a minute. A forgotten password: the owner sets a temporary one by
rcon (rp password <account> <temporary>).

CHARACTERS. Up to g_rpCharacters per account: a name (unique on the
server), a faction (Heroes, Villains, Neutral), and, optional, a one-line
description and a bio. While roleplay is on, the player is shown as the
character they play, looking as it does: what they wear while playing it
(model, saber and skin colours) is kept with it. Switching characters
respawns them, and waits until they're out of a duel and unhurt for 10 s
(and not carrying a flag, in a vehicle or, in Siege and the duel game
types, in the round), at most every 10 s.

CHAT, with roleplay on: talk (the chat key) reaches those within
g_rpTalkRange (510 units, 15 m), /me actions too; the team chat key or
/ooc goes to everyone, out of character, under the player's own name;
tell is as it was. Our clients get the speaker and the kind of line with
each, for speech bubbles; other clients see plain chat.

Client commands (most sent by the cgame or the menus, not typed):
  rpver <protocol>          this client runs this build (cg_vrrp.c)
  rpnew <account> <key>     make an account (key: 64 hex digits)
  rplogin <account> <proof> log in (proof: HMAC of the challenge)
  rppass <proof> <newkey>   change the password
  rplogout
  rp new|play|faction|desc|bio|delete|list|look ...
  rpcard <client>, rpbio <client>    what a character's card shows
  me <action>, ooc <text>
To clients: rpch <salt> <challenge>, rpacct <account|-> <active> <count>
<most> <1: this answers a login or a new account | 0> then rpchar <i>
<faction> "name" "desc" "bio" each, rpcard, rpbio, rpmsg; rppassok, or
rpfail <1: the name or password was wrong | 0>, to end a request made
with a key. A new password's proof is over "password <its key>".

Rcon: rp list, rp rename <player> <name>, rp delete <account> <character>,
rp password <account> <temporary>.

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

#define RP_PROTOCOL			1
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
#define RP_FILE_MAX			16384

#define RP_SERVER_FILE		"rp/server.txt"
#define RP_SERVER_FILE_MAX	4096
#define RP_ACCOUNT_DIR		"rp/accounts"

enum { RPF_HEROES, RPF_VILLAINS, RPF_NEUTRAL, RPF_NUM };
static const char *rpFactions[RPF_NUM] = { "Heroes", "Villains", "Neutral" };

// What a character wears, kept from what the player wears while playing it.
static const char *rpLookKeys[] = { "model", "color1", "color2", "char_color_red", "char_color_green", "char_color_blue" };
#define RP_LOOK_KEYS	( (int)ARRAY_LEN( rpLookKeys ) )

typedef struct {
	char	name[RP_NAME_BUF];
	int		faction;
	char	desc[RP_DESC_MAX + 1];
	char	bio[RP_BIO_MAX + 1];
	char	look[RP_LOOK_KEYS][MAX_QPATH];	// "" for what it doesn't set
} rpChar_t;

typedef struct {
	char		name[RP_ACCOUNT_MAX + 1];	// lower case
	byte		key[VRRP_KEY_LEN];
	int			last;						// the character last played (-1: none)
	int			numChars;
	rpChar_t	chars[RP_CHARS_MAX];
} rpAccount_t;

typedef struct {
	qboolean	capable;			// runs this build (sent rpver)
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

static qboolean G_RP_FileThere( const char *path ) {
	fileHandle_t f;
	const int len = trap->FS_Open( path, &f, FS_READ );

	if ( f ) {
		trap->FS_Close( f );
	}
	return (qboolean)( len >= 0 && f );
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
	Com_sprintf( text, sizeof( text ), "// OpenJK VR roleplay account\nkey %s\nlast %i\n", hex, acc->last );
	for ( i = 0; i < acc->numChars; i++ ) {
		const rpChar_t *ch = &acc->chars[i];
		Q_strcat( text, sizeof( text ), va( "character\nname \"%s\"\nfaction %i\ndesc \"%s\"\nbio \"%s\"\n",
			ch->name, ch->faction, ch->desc, ch->bio ) );
		for ( k = 0; k < RP_LOOK_KEYS; k++ ) {
			if ( ch->look[k][0] ) {
				Q_strcat( text, sizeof( text ), va( "look %s \"%s\"\n", rpLookKeys[k], ch->look[k] ) );
			}
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
	qboolean haveKey = qfalse;

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
		} else if ( !Q_stricmp( tok, "character" ) ) {
			ch = ( acc->numChars < RP_CHARS_MAX ) ? &acc->chars[acc->numChars++] : NULL;
			if ( ch ) {
				memset( ch, 0, sizeof( *ch ) );
				ch->faction = RPF_NEUTRAL;
			}
		} else if ( !Q_stricmp( tok, "end" ) ) {
			ch = NULL;
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
	// a character without a name (a damaged file) is dropped
	{
		int i, n = 0;
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
	if ( len != -1 ) {
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
		return;
	}
	trap->SendServerCommand( clientNum, va( "rpacct %s %i %i %i %i", c->acc.name, c->active, c->acc.numChars, G_RP_MostChars(), c->answering ? 1 : 0 ) );
	rpc[clientNum].answering = qfalse;
	for ( i = 0; i < c->acc.numChars; i++ ) {
		const rpChar_t *ch = &c->acc.chars[i];
		trap->SendServerCommand( clientNum, va( "rpchar %i %i \"%s\" \"%s\" \"%s\"", i, ch->faction, ch->name, ch->desc, ch->bio ) );
	}
}

// The login a client keeps across a map change (the game restarts, they don't).
static void G_RP_WriteSession( int clientNum ) {
	const rpClient_t *c = &rpc[clientNum];

	trap->Cvar_Set( va( "vrrp%i", clientNum ), c->loggedIn ? va( "%s %i", c->acc.name, c->active ) : "" );
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
	c->active = index;
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
		if ( level.gametype == GT_SIEGE || level.gametype == GT_DUEL || level.gametype == GT_POWERDUEL ) {
			return "In this game type, switch characters while you're out of the round";
		}
	}
	return NULL;
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
	c->loggedIn = qfalse;
	c->active = -1;
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
	c->loggedIn = qtrue;
	c->acc = *acc;
	c->fails = 0;
	c->answering = qtrue;	// (the account sent next answers the login, or the new account)
	G_RP_Play( clientNum, ( acc->last >= 0 && acc->last < acc->numChars ) ? acc->last : -1, qfalse );
}

static void G_RP_Hello( gentity_t *ent ) {
	const int clientNum = ent - g_entities;
	rpClient_t *c = &rpc[clientNum];
	char arg[16];

	trap->Argv( 1, arg, sizeof( arg ) );
	if ( atoi( arg ) < 1 ) {
		return;
	}
	c->capable = qtrue;
	if ( c->challenge[0] ) {
		// a hello again (one lost, or its answer late): the same challenge, so
		// the answer to either does, until it's used
		trap->SendServerCommand( clientNum, va( "rpch %s %s", rp.salt, c->challenge ) );
	} else {
		G_RP_NewChallenge( clientNum );
	}
	G_RP_SendAccount( clientNum );
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

	if ( !G_RPOn() ) {
		G_RP_Refused( clientNum, "Roleplay is off on this server right now", qfalse );
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

	if ( !G_RPOn() ) {
		G_RP_Refused( clientNum, "Roleplay is off on this server right now", qfalse );
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
		Q_strncpyz( ch->look[k], c->lookBase[k], sizeof( ch->look[k] ) );
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
	if ( !G_RPOn() ) {
		G_RP_Msg( clientNum, "Roleplay is off on this server right now" );
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
		G_RP_Print( clientNum, "rp new <name> | play <name or number> | faction heroes|villains|neutral | desc <text> | bio <text> | delete <name>" );
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
		G_RP_Msg( clientNum, va( "%s" S_COLOR_WHITE " deleted", c->acc.chars[i].name ) );
		if ( i == c->active ) {
			c->active = -1;
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
		}
		G_RP_Msg( clientNum, va( "%s" S_COLOR_WHITE " is with the %s", c->acc.chars[c->active].name, rpFactions[i] ) );
	} else if ( !Q_stricmp( sub, "desc" ) ) {
		G_RP_CleanText( ConcatArgs( 2 ), c->acc.chars[c->active].desc, sizeof( c->acc.chars[0].desc ) );
		G_RP_Msg( clientNum, "Description set" );
	} else if ( !Q_stricmp( sub, "bio" ) ) {
		G_RP_CleanText( ConcatArgs( 2 ), c->acc.chars[c->active].bio, sizeof( c->acc.chars[0].bio ) );
		G_RP_Msg( clientNum, "Bio set" );
	} else {
		G_RP_Print( clientNum, "rp list | new | play | faction | desc | bio | delete | look" );
		return;
	}
	G_RP_SaveAccount( &c->acc );
	G_RP_SendAccount( clientNum );
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
		Com_sprintf( line, sizeof( line ), S_COLOR_GREY "[OOC] " S_COLOR_WHITE "%s" S_COLOR_WHITE EC ": " S_COLOR_WHITE "%s",
			rpc[ent - g_entities].playerName[0] ? rpc[ent - g_entities].playerName : name, text );
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
	if ( !rp.ready ) {
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
		G_RP_LogOut( ent - g_entities, rpc[ent - g_entities].loggedIn ? "Logged out" : NULL );
	} else if ( !Q_stricmp( cmd, "rp" ) ) {
		G_RP_Rp( ent );
	} else if ( !Q_stricmp( cmd, "rpcard" ) ) {
		G_RP_Card( ent, qfalse );
	} else if ( !Q_stricmp( cmd, "rpbio" ) ) {
		G_RP_Card( ent, qtrue );
	} else if ( !Q_stricmp( cmd, "me" ) ) {
		G_RP_Me( ent );
	} else if ( !Q_stricmp( cmd, "ooc" ) ) {
		G_RP_Ooc( ent );
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
	memset( &rpc[clientNum], 0, sizeof( rpc[0] ) );
	rpc[clientNum].active = -1;
	trap->Cvar_Set( va( "vrrp%i", clientNum ), "" );
}

// g_rp changed (or the game started): the serverinfo follows it, and
// everyone in the game is shown as they now should be.
void G_RPPublish( void ) {
	const qboolean on = (qboolean)( g_rp.integer != 0 );
	int i;

	trap->Cvar_Set( "vrRP", ( on && rp.ready ) ? "1" : "0" );
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
		trap->Cvar_Set( "vrRP", "0" );
		return;	// (not ready: off, whatever g_rp says)
	}
	G_RP_IndexBuild();
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		rpc[i].active = -1;
	}
	rp.ready = qtrue;
	rp.on = (qboolean)( g_rp.integer != 0 );
	trap->Cvar_Set( "vrRP", rp.on ? "1" : "0" );
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
Rcon: rp list | rename | delete | password
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
	if ( !Q_stricmp( sub, "list" ) ) {
		trap->Print( "num  player                account               character\n" );
		for ( i = 0; i < level.maxclients; i++ ) {
			const rpChar_t *ch;
			if ( !g_entities[i].inuse || !g_entities[i].client || g_entities[i].client->pers.connected == CON_DISCONNECTED ) {
				continue;
			}
			ch = ( rpc[i].loggedIn && rpc[i].active >= 0 && rpc[i].active < rpc[i].acc.numChars ) ? &rpc[i].acc.chars[rpc[i].active] : NULL;
			trap->Print( "%3i  %-20s  %-20s  %s (%s)\n", i, rpc[i].playerName[0] ? rpc[i].playerName : g_entities[i].client->pers.netname,
				rpc[i].loggedIn ? rpc[i].acc.name : "-", ch ? ch->name : "-", ch ? rpFactions[ch->faction] : "" );
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
		trap->Argv( 3, temp, sizeof( temp ) );
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
		trap->Print( "%s's password is now the temporary one: they log in with it and change it on the Login page\n", acc->name );
		return qtrue;
	}
	return qfalse;	// (on a listen server the host's own rp commands go on to the game as a client's)
}
