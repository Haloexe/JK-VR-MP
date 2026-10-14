/*
===========================================================================
OpenJK VR (multiplayer) - the hashing roleplay logins need: SHA-256
(FIPS 180-4), HMAC-SHA256 (RFC 2104) and PBKDF2-HMAC-SHA256 (RFC 8018).

Shared by the game (it checks logins, and sets the temporary password a
server owner gives a player who forgot theirs) and the cgame (it turns a
password into its key, so the password itself never leaves the player's
PC). See bg_vrcrypt.h for how a login works.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "bg_vrcrypt.h"

static const uint32_t vrcK[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define VRC_ROTR( x, n )	( ( (x) >> (n) ) | ( (x) << ( 32 - (n) ) ) )

static void VRC_Sha256Block( vrSha256_t *c, const byte *p ) {
	uint32_t w[64], a, b, cc, d, e, f, g, h;
	int i;

	for ( i = 0; i < 16; i++ ) {
		w[i] = ( (uint32_t)p[i * 4] << 24 ) | ( (uint32_t)p[i * 4 + 1] << 16 ) | ( (uint32_t)p[i * 4 + 2] << 8 ) | p[i * 4 + 3];
	}
	for ( i = 16; i < 64; i++ ) {
		const uint32_t s0 = VRC_ROTR( w[i - 15], 7 ) ^ VRC_ROTR( w[i - 15], 18 ) ^ ( w[i - 15] >> 3 );
		const uint32_t s1 = VRC_ROTR( w[i - 2], 17 ) ^ VRC_ROTR( w[i - 2], 19 ) ^ ( w[i - 2] >> 10 );
		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}
	a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3];
	e = c->h[4]; f = c->h[5]; g = c->h[6]; h = c->h[7];
	for ( i = 0; i < 64; i++ ) {
		const uint32_t S1 = VRC_ROTR( e, 6 ) ^ VRC_ROTR( e, 11 ) ^ VRC_ROTR( e, 25 );
		const uint32_t ch = ( e & f ) ^ ( ~e & g );
		const uint32_t t1 = h + S1 + ch + vrcK[i] + w[i];
		const uint32_t S0 = VRC_ROTR( a, 2 ) ^ VRC_ROTR( a, 13 ) ^ VRC_ROTR( a, 22 );
		const uint32_t maj = ( a & b ) ^ ( a & cc ) ^ ( b & cc );
		const uint32_t t2 = S0 + maj;
		h = g; g = f; f = e; e = d + t1;
		d = cc; cc = b; b = a; a = t1 + t2;
	}
	c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d;
	c->h[4] += e; c->h[5] += f; c->h[6] += g; c->h[7] += h;
}

void VRC_Sha256Init( vrSha256_t *c ) {
	c->h[0] = 0x6a09e667; c->h[1] = 0xbb67ae85; c->h[2] = 0x3c6ef372; c->h[3] = 0xa54ff53a;
	c->h[4] = 0x510e527f; c->h[5] = 0x9b05688c; c->h[6] = 0x1f83d9ab; c->h[7] = 0x5be0cd19;
	c->bits = 0;
	c->used = 0;
}

void VRC_Sha256Update( vrSha256_t *c, const void *data, size_t len ) {
	const byte *p = (const byte *)data;

	c->bits += (uint64_t)len * 8;
	while ( len > 0 ) {
		const size_t take = ( len < (size_t)( 64 - c->used ) ) ? len : (size_t)( 64 - c->used );
		memcpy( c->buf + c->used, p, take );
		c->used += (int)take;
		p += take;
		len -= take;
		if ( c->used == 64 ) {
			VRC_Sha256Block( c, c->buf );
			c->used = 0;
		}
	}
}

void VRC_Sha256Final( vrSha256_t *c, byte out[VRC_SHA256_LEN] ) {
	const uint64_t bits = c->bits;
	int i;

	c->buf[c->used++] = 0x80;
	if ( c->used > 56 ) {
		memset( c->buf + c->used, 0, 64 - c->used );
		VRC_Sha256Block( c, c->buf );
		c->used = 0;
	}
	memset( c->buf + c->used, 0, 56 - c->used );
	for ( i = 0; i < 8; i++ ) {
		c->buf[56 + i] = (byte)( bits >> ( 56 - 8 * i ) );
	}
	VRC_Sha256Block( c, c->buf );
	for ( i = 0; i < 8; i++ ) {
		out[i * 4] = (byte)( c->h[i] >> 24 );
		out[i * 4 + 1] = (byte)( c->h[i] >> 16 );
		out[i * 4 + 2] = (byte)( c->h[i] >> 8 );
		out[i * 4 + 3] = (byte)c->h[i];
	}
	memset( c, 0, sizeof( *c ) );
}

void VRC_Sha256( const void *data, size_t len, byte out[VRC_SHA256_LEN] ) {
	vrSha256_t c;

	VRC_Sha256Init( &c );
	VRC_Sha256Update( &c, data, len );
	VRC_Sha256Final( &c, out );
}

// An HMAC keyed and ready: the hash states after the inner and outer pads,
// which every message with this key starts from.
typedef struct {
	vrSha256_t	inner, outer;
} vrHmac_t;

static void VRC_HmacInit( vrHmac_t *m, const void *key, size_t keyLen ) {
	byte block[64], pad[64];
	int i;

	memset( block, 0, sizeof( block ) );
	if ( keyLen > 64 ) {
		VRC_Sha256( key, keyLen, block );
	} else if ( keyLen ) {
		memcpy( block, key, keyLen );
	}
	for ( i = 0; i < 64; i++ ) {
		pad[i] = block[i] ^ 0x36;
	}
	VRC_Sha256Init( &m->inner );
	VRC_Sha256Update( &m->inner, pad, 64 );
	for ( i = 0; i < 64; i++ ) {
		pad[i] = block[i] ^ 0x5c;
	}
	VRC_Sha256Init( &m->outer );
	VRC_Sha256Update( &m->outer, pad, 64 );
	memset( block, 0, sizeof( block ) );
	memset( pad, 0, sizeof( pad ) );
}

// The HMAC of (part1, part2) under m's key; m is left as it was.
static void VRC_HmacTwo( const vrHmac_t *m, const void *part1, size_t len1, const void *part2, size_t len2,
	byte out[VRC_SHA256_LEN] ) {
	vrSha256_t c = m->inner;
	byte innerHash[VRC_SHA256_LEN];

	VRC_Sha256Update( &c, part1, len1 );
	if ( len2 ) {
		VRC_Sha256Update( &c, part2, len2 );
	}
	VRC_Sha256Final( &c, innerHash );
	c = m->outer;
	VRC_Sha256Update( &c, innerHash, sizeof( innerHash ) );
	VRC_Sha256Final( &c, out );
	memset( innerHash, 0, sizeof( innerHash ) );
}

void VRC_HmacSha256( const void *key, size_t keyLen, const void *msg, size_t msgLen, byte out[VRC_SHA256_LEN] ) {
	vrHmac_t m;

	VRC_HmacInit( &m, key, keyLen );
	VRC_HmacTwo( &m, msg, msgLen, NULL, 0, out );
	memset( &m, 0, sizeof( m ) );
}

void VRC_Pbkdf2Sha256( const void *password, size_t passwordLen, const void *salt, size_t saltLen,
	int iterations, byte *out, size_t outLen ) {
	vrHmac_t m;
	byte u[VRC_SHA256_LEN], t[VRC_SHA256_LEN], count[4];
	uint32_t block;
	int i, j;

	VRC_HmacInit( &m, password, passwordLen );
	if ( iterations < 1 ) {
		iterations = 1;
	}
	for ( block = 1; outLen > 0; block++ ) {
		const size_t take = ( outLen < sizeof( t ) ) ? outLen : sizeof( t );

		count[0] = (byte)( block >> 24 );
		count[1] = (byte)( block >> 16 );
		count[2] = (byte)( block >> 8 );
		count[3] = (byte)block;
		VRC_HmacTwo( &m, salt, saltLen, count, 4, u );
		memcpy( t, u, sizeof( t ) );
		for ( i = 1; i < iterations; i++ ) {
			VRC_HmacTwo( &m, u, sizeof( u ), NULL, 0, u );
			for ( j = 0; j < VRC_SHA256_LEN; j++ ) {
				t[j] ^= u[j];
			}
		}
		memcpy( out, t, take );
		out += take;
		outLen -= take;
	}
	memset( &m, 0, sizeof( m ) );
	memset( u, 0, sizeof( u ) );
	memset( t, 0, sizeof( t ) );
}

void VRC_ToHex( const byte *in, int n, char *out ) {
	static const char digits[] = "0123456789abcdef";
	int i;

	for ( i = 0; i < n; i++ ) {
		out[i * 2] = digits[in[i] >> 4];
		out[i * 2 + 1] = digits[in[i] & 15];
	}
	out[n * 2] = '\0';
}

static int VRC_HexDigit( char ch ) {
	if ( ch >= '0' && ch <= '9' ) {
		return ch - '0';
	}
	if ( ch >= 'a' && ch <= 'f' ) {
		return ch - 'a' + 10;
	}
	if ( ch >= 'A' && ch <= 'F' ) {
		return ch - 'A' + 10;
	}
	return -1;
}

qboolean VRC_FromHex( const char *in, byte *out, int n ) {
	int i;

	if ( !in || (int)strlen( in ) != n * 2 ) {
		return qfalse;
	}
	for ( i = 0; i < n; i++ ) {
		const int hi = VRC_HexDigit( in[i * 2] ), lo = VRC_HexDigit( in[i * 2 + 1] );
		if ( hi < 0 || lo < 0 ) {
			return qfalse;
		}
		out[i] = (byte)( ( hi << 4 ) | lo );
	}
	return qtrue;
}

qboolean VRC_Equal( const byte *a, const byte *b, int n ) {
	byte diff = 0;
	int i;

	for ( i = 0; i < n; i++ ) {
		diff |= a[i] ^ b[i];
	}
	return (qboolean)( diff == 0 );
}

// An account name as it's keyed: lower case, at most 63 characters.
static void VRRP_AccountKeyName( const char *account, char *out, int size ) {
	Q_strncpyz( out, account ? account : "", size );
	Q_strlwr( out );
}

void VRRP_LoginKey( const char *password, const char *serverSalt, const char *account, byte key[VRRP_KEY_LEN] ) {
	char name[64], salt[160];

	VRRP_AccountKeyName( account, name, sizeof( name ) );
	Com_sprintf( salt, sizeof( salt ), "openjkvr-rp:%s:%s", serverSalt ? serverSalt : "", name );
	VRC_Pbkdf2Sha256( password ? password : "", password ? strlen( password ) : 0, salt, strlen( salt ),
		VRRP_KEY_ITERATIONS, key, VRRP_KEY_LEN );
}

void VRRP_LoginProof( const byte key[VRRP_KEY_LEN], const char *purpose, const char *challenge, const char *account,
	byte proof[VRC_SHA256_LEN] ) {
	char name[64], msg[256];

	VRRP_AccountKeyName( account, name, sizeof( name ) );
	Com_sprintf( msg, sizeof( msg ), "%s:%s:%s", purpose ? purpose : "", challenge ? challenge : "", name );
	VRC_HmacSha256( key, VRRP_KEY_LEN, msg, strlen( msg ), proof );
}
