/*
===========================================================================
OpenJK VR (multiplayer) - the hashing roleplay logins need (bg_vrcrypt.c).

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#pragma once

#include "qcommon/q_shared.h"

#define VRC_SHA256_LEN		32

typedef struct vrSha256_s {
	uint32_t	h[8];
	uint64_t	bits;		// message length so far
	byte		buf[64];	// a block being filled...
	int			used;		// ...this far
} vrSha256_t;

void		VRC_Sha256Init( vrSha256_t *c );
void		VRC_Sha256Update( vrSha256_t *c, const void *data, size_t len );
void		VRC_Sha256Final( vrSha256_t *c, byte out[VRC_SHA256_LEN] );
void		VRC_Sha256( const void *data, size_t len, byte out[VRC_SHA256_LEN] );
void		VRC_HmacSha256( const void *key, size_t keyLen, const void *msg, size_t msgLen, byte out[VRC_SHA256_LEN] );
void		VRC_Pbkdf2Sha256( const void *password, size_t passwordLen, const void *salt, size_t saltLen,
				int iterations, byte *out, size_t outLen );

void		VRC_ToHex( const byte *in, int n, char *out );			// out: 2n + 1 chars
qboolean	VRC_FromHex( const char *in, byte *out, int n );		// exactly 2n hex digits
qboolean	VRC_Equal( const byte *a, const byte *b, int n );		// in the same time whatever differs

/*
Roleplay logins (g_vrrp.c checks them, cg_vrrp.c makes them). A password
becomes a key for one account on one server, slowly (PBKDF2), on the
player's PC; the server keeps the key, never the password. Logging in
proves the key against a one-time challenge the server sent, so what
crosses the network can't be replayed.
*/
#define VRRP_KEY_LEN			32
#define VRRP_KEY_ITERATIONS		20000
#define VRRP_SALT_HEX			32		// the server's salt, in hex
#define VRRP_CHALLENGE_HEX		32		// a challenge, in hex

void		VRRP_LoginKey( const char *password, const char *serverSalt, const char *account, byte key[VRRP_KEY_LEN] );
void		VRRP_LoginProof( const byte key[VRRP_KEY_LEN], const char *purpose, const char *challenge, const char *account,
				byte proof[VRC_SHA256_LEN] );
