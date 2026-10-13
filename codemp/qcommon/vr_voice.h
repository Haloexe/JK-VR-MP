/*
===========================================================================
OpenJK VR (multiplayer) - proximity voice chat: what goes over the network.

A client with voice chat on captures its microphone, encodes it with Opus
(48 kHz mono, 20 ms frames) and sends the frames to a server whose
systeminfo has sv_voice == VOICE_PROTOCOL, in clc_voice messages (before
the usercmds). The server relays each packet unchanged to the players who
can hear the speaker - those near them (players), or the other spectators -
and who asked for voice (userinfo cl_voiceProtocol == VOICE_PROTOCOL), in
svc_voice messages after their next snapshot. Each client plays every
speaker's voice from where that player is, and moves their lips with it.

clc_voice:	8 bits generation (a new one each time they start talking)
			32 bits sequence number of the first frame (frames in a
				generation are numbered one after the other)
			8 bits number of frames (1..VOICE_MAX_PACKET_FRAMES)
			per frame: 8 bits length (1..VOICE_MAX_FRAME_BYTES), the Opus
				packet
svc_voice:	8 bits speaker (client number)
			3 x 18 bits where they were (their origin, rounded, + 131072:
				they may be out of sight, with no entity to play it from)
			then the same as clc_voice

A client may send no faster than it talks: about VOICE_FRAME_MS a frame
and twice VOICE_BITRATE (the server drops the rest).

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#pragma once

#define VOICE_PROTOCOL			1
#define VOICE_RATE				48000	// Hz
#define VOICE_FRAME_MS			20
#define VOICE_FRAME_SAMPLES		( VOICE_RATE / 1000 * VOICE_FRAME_MS )
#define VOICE_BITRATE			24000	// bits/s the encoder aims for
#define VOICE_MAX_FRAME_BYTES	160		// (64 kbit/s: Opus keeps to this)
#define VOICE_MAX_PACKET_FRAMES	4
#define VOICE_MAX_PACKET_BYTES	( VOICE_MAX_PACKET_FRAMES * ( 1 + VOICE_MAX_FRAME_BYTES ) )

typedef struct voicePacket_s {
	int		generation;
	int		sequence;
	int		frames;
	int		length;							// bytes of data: each frame's length byte, then its bytes
	byte	data[VOICE_MAX_PACKET_BYTES];
} voicePacket_t;

// The packet after a clc_voice / svc_voice (and speaker) byte. qfalse: it
// makes no sense (nothing after it in the message can be trusted).
static inline qboolean Voice_ReadPacket( msg_t *msg, voicePacket_t *p ) {
	int f;

	p->generation = MSG_ReadByte( msg );
	p->sequence = MSG_ReadLong( msg );
	p->frames = MSG_ReadByte( msg );
	p->length = 0;
	if ( p->frames < 1 || p->frames > VOICE_MAX_PACKET_FRAMES ) {
		return qfalse;
	}
	for ( f = 0; f < p->frames; f++ ) {
		const int len = MSG_ReadByte( msg );
		int i;
		if ( len < 1 || len > VOICE_MAX_FRAME_BYTES || msg->readcount > msg->cursize ) {
			return qfalse;
		}
		p->data[p->length++] = (byte)len;
		for ( i = 0; i < len; i++ ) {
			p->data[p->length++] = (byte)MSG_ReadByte( msg );
		}
	}
	return (qboolean)( msg->readcount <= msg->cursize );
}

// Where the speaker was (svc_voice)
#define VOICE_ORIGIN_BITS		18
#define VOICE_ORIGIN_BIAS		( 1 << ( VOICE_ORIGIN_BITS - 1 ) )

static inline void Voice_WriteOrigin( msg_t *msg, const vec3_t origin ) {
	int i;
	for ( i = 0; i < 3; i++ ) {
		const int v = (int)( origin[i] + ( origin[i] < 0.0f ? -0.5f : 0.5f ) );
		MSG_WriteBits( msg, ( v < -VOICE_ORIGIN_BIAS ? -VOICE_ORIGIN_BIAS : v >= VOICE_ORIGIN_BIAS ? VOICE_ORIGIN_BIAS - 1 : v ) + VOICE_ORIGIN_BIAS, VOICE_ORIGIN_BITS );
	}
}

static inline void Voice_ReadOrigin( msg_t *msg, vec3_t origin ) {
	int i;
	for ( i = 0; i < 3; i++ ) {
		origin[i] = (float)( MSG_ReadBits( msg, VOICE_ORIGIN_BITS ) - VOICE_ORIGIN_BIAS );
	}
}

static inline void Voice_WritePacket( msg_t *msg, const voicePacket_t *p ) {
	int i;

	MSG_WriteByte( msg, p->generation & 0xff );
	MSG_WriteLong( msg, p->sequence );
	MSG_WriteByte( msg, p->frames );
	for ( i = 0; i < p->length; i++ ) {
		MSG_WriteByte( msg, p->data[i] );
	}
}
