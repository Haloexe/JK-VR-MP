/*
===========================================================================
OpenJK VR (multiplayer) - proximity voice chat: playing the others' voices.

Each player who talks gets a stream of samples on the sound mixer's
timeline (cl_voice.cpp feeds it as their voice comes in). The mixer paints
it into every update from where that player is - louder near, quieter far,
to the side they're on - and their face moves with it (the lip sync the
game uses for its own voices: S_GetVoiceVolume). Where they are: where the
game shows them, or - out of sight, where the game has no entity for them -
where the server said they were when they spoke.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "snd_local.h"

extern int			s_soundtime;
extern matrix3_t	listener_axis;
extern int			listener_number;
extern vec3_t		s_entityPosition[MAX_GENTITIES];
extern int			s_entityWavVol[MAX_GENTITIES];
extern int			s_soundStarted;
extern qboolean		s_soundMuted;
extern cvar_t		*s_mixPreStep;
extern portable_samplepair_t paintbuffer[PAINTBUFFER_SIZE];

#define VOICE_STREAM_SAMPLES	32768		// per speaker (a power of two): ~0.7 s at 44 kHz
#define VOICE_STREAM_MASK		( VOICE_STREAM_SAMPLES - 1 )
#define VOICE_CUSHION_MS		60			// a voice starts this far after the next mix: the network's jitter
#define VOICE_MAX_DELAY_MS		350			// more than this queued up: the rest is dropped (it catches up)
#define VOICE_MOUTH				30.0f		// units above a player's origin their voice comes from
#define VOICE_NEAR				80.0f		// full volume this near...
#define VOICE_ROLLOFF			1.0f		// ...then down as the distance (half as loud twice as far)
#define VOICE_SEEN_MS			250			// a player whose entity moved this recently is in sight

typedef struct {
	short		samples[VOICE_STREAM_SAMPLES];	// mono, at dma.speed, at mixer time & VOICE_STREAM_MASK
	int			start, end;		// mixer times of the voice now in it (end == start: nothing)
	int			leftvol, rightvol;	// 0..255, where they are (S_VoiceSpatialize)
	float		frac;			// (resampling: where the next input sample falls)
	short		last;			// (resampling: the input sample before it)
	float		level;			// how loud it is now, 0..1 (S_VoiceLevel)
	vec3_t		origin;			// where the server said they were (with their voice)
	int			originTime;		// Sys_Milliseconds() then (0: never)
	int			seenTime;		// Sys_Milliseconds() their entity last moved (in sight)
} voiceStream_t;

static voiceStream_t	s_voice[MAX_CLIENTS];
static cvar_t			*s_voiceVolume;		// cl_voiceVolume

static void S_VoiceRegister( void ) {
	if ( !s_voiceVolume ) {
		s_voiceVolume = Cvar_Get( "cl_voiceVolume", "1", CVAR_ARCHIVE_ND, "How loud other players' voices are (voice chat)" );
	}
}

// That player isn't talking any more (they left).
void S_VoiceSpeakerReset( int speaker ) {
	if ( speaker < 0 || speaker >= MAX_CLIENTS ) {
		return;
	}
	s_voice[speaker].start = s_voice[speaker].end = 0;
	s_voice[speaker].level = 0.0f;
	s_voice[speaker].frac = 0.0f;
	s_voice[speaker].last = 0;
	s_voice[speaker].originTime = 0;
}

// Nobody's talking any more (the mixer's clock restarted, or we left).
void S_VoiceClear( void ) {
	int i;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		S_VoiceSpeakerReset( i );
	}
}

// Where the server said that player was when they spoke.
void S_VoiceSpeakerOrigin( int speaker, const vec3_t origin ) {
	if ( speaker < 0 || speaker >= MAX_CLIENTS ) {
		return;
	}
	VectorCopy( origin, s_voice[speaker].origin );
	s_voice[speaker].originTime = Q_max( 1, Sys_Milliseconds() );
}

// The game moved that player's entity (S_UpdateEntityPosition): in sight.
void S_VoiceEntityMoved( int entityNum ) {
	if ( entityNum >= 0 && entityNum < MAX_CLIENTS ) {
		s_voice[entityNum].seenTime = Q_max( 1, Sys_Milliseconds() );
	}
}

/*
=================
S_VoiceSamples

More of a player's voice (mono, 16 bits, at rate), to follow what came
before - or, if that has played out, to start a moment after the next mix.
=================
*/
void S_VoiceSamples( int speaker, int samples, int rate, const short *data ) {
	voiceStream_t *v;
	int startAt, maxEnd;
	float step, f;
	int i;

	if ( speaker < 0 || speaker >= MAX_CLIENTS || samples <= 0 || rate <= 0 || !s_soundStarted || s_soundMuted || dma.speed <= 0 ) {
		return;
	}
	S_VoiceRegister();
	v = &s_voice[speaker];
	startAt = s_soundtime + (int)( ( s_mixPreStep->value + VOICE_CUSHION_MS * 0.001f ) * dma.speed );
	if ( v->end < startAt - (int)( VOICE_CUSHION_MS * 0.001f * dma.speed ) ) {
		// played out (or never started): from a moment after the next mix
		v->start = v->end = startAt;
		v->frac = 1.0f;
		v->last = 0;
	}
	maxEnd = s_soundtime + (int)( ( s_mixPreStep->value + VOICE_MAX_DELAY_MS * 0.001f ) * dma.speed );
	if ( v->end >= maxEnd ) {
		return;	// (too much queued up already: this goes, so it catches up)
	}
	// resample to the mixer's rate, linearly: each output sample lies frac of
	// the way from 'last' to the input sample after it
	step = (float)rate / dma.speed;
	f = v->frac;
	i = 0;
	for ( ;; ) {
		while ( f >= 1.0f && i < samples ) {
			v->last = data[i++];
			f -= 1.0f;
		}
		if ( f >= 1.0f || i >= samples ) {
			break;	// (the input sample after it comes next time)
		}
		v->samples[v->end & VOICE_STREAM_MASK] = (short)( v->last + ( data[i] - v->last ) * f );
		v->end++;
		f += step;
		if ( v->end - v->start >= VOICE_STREAM_SAMPLES ) {
			v->start = v->end - VOICE_STREAM_SAMPLES + 1;	// (never more than the ring holds)
		}
	}
	v->frac = f;
}

/*
=================
S_VoiceSpatialize

Once an update, before mixing: how loud each voice is in each ear, from
where its player is now.
=================
*/
void S_VoiceSpatialize( void ) {
	const float volume = s_voiceVolume ? Com_Clamp( 0.0f, 2.0f, s_voiceVolume->value ) : 1.0f;
	const int now = Sys_Milliseconds();
	int i;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		voiceStream_t *v = &s_voice[i];
		vec3_t mouth, dir;
		float dist, gain, dot, lscale, rscale;

		if ( v->end <= s_soundtime ) {
			v->leftvol = v->rightvol = 0;
			continue;
		}
		// where the game shows them; out of sight, where the server had them
		if ( v->originTime && ( !v->seenTime || now - v->seenTime > VOICE_SEEN_MS ) ) {
			VectorCopy( v->origin, mouth );
		} else {
			VectorCopy( s_entityPosition[i], mouth );
		}
		mouth[2] += VOICE_MOUTH;
		VectorSubtract( mouth, listener_origin, dir );
		dist = VectorNormalize( dir );
		gain = ( dist <= VOICE_NEAR ) ? 1.0f : VOICE_NEAR / ( VOICE_NEAR + VOICE_ROLLOFF * ( dist - VOICE_NEAR ) );
		if ( dma.channels == 1 || i == listener_number ) {
			lscale = rscale = 1.0f;
		} else {
			dot = -DotProduct( listener_axis[1], dir );
			rscale = s_separation->value + ( 1.0f - s_separation->value ) * dot;
			lscale = s_separation->value - ( 1.0f - s_separation->value ) * dot;
			rscale = Com_Clamp( 0.0f, 1.0f, rscale );
			lscale = Com_Clamp( 0.0f, 1.0f, lscale );
			// (to the side still sounds like a voice in the room, not in one ear)
			rscale = 0.3f + 0.7f * rscale;
			lscale = 0.3f + 0.7f * lscale;
		}
		v->leftvol = (int)( 255.0f * Com_Clamp( 0.0f, 2.0f, gain * lscale * volume ) );
		v->rightvol = (int)( 255.0f * Com_Clamp( 0.0f, 2.0f, gain * rscale * volume ) );
	}
}

/*
=================
S_PaintVoice

The voices for mixer times start..end, into the paint buffer (which holds
s_paintedtime onwards).
=================
*/
void S_PaintVoice( int start, int end ) {
	int i;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		const voiceStream_t *v = &s_voice[i];
		const int from = Q_max( start, v->start );
		const int to = Q_min( end, v->end );
		int t;

		if ( from >= to || ( !v->leftvol && !v->rightvol ) ) {
			continue;
		}
		for ( t = from; t < to; t++ ) {
			const int s = v->samples[t & VOICE_STREAM_MASK];
			portable_samplepair_t *p = &paintbuffer[t - s_paintedtime];
			p->left += s * v->leftvol;		// (as a sound at full volume: 16 bits << 8)
			p->right += s * v->rightvol;
		}
	}
}

/*
=================
S_VoiceLipSync

After mixing: how loud each voice is right now (what's being heard), for
their lips (the game's lip sync levels: -1 talking but quiet, 1..4) and for
S_VoiceLevel.
=================
*/
void S_VoiceLipSync( void ) {
	const int window = Q_max( 1, dma.speed / 50 );	// (20 ms)
	int i;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		voiceStream_t *v = &s_voice[i];
		int from, to, t, n = 0;
		double sum = 0.0;
		float rms, db;
		int lip;

		if ( v->end <= s_soundtime || v->start >= s_soundtime + window ) {
			v->level = 0.0f;
			continue;
		}
		from = Q_max( v->start, s_soundtime - window / 2 );
		to = Q_min( v->end, s_soundtime + window / 2 );
		for ( t = from; t < to; t++ ) {
			const float s = v->samples[t & VOICE_STREAM_MASK] * ( 1.0f / 32768.0f );
			sum += s * s;
			n++;
		}
		rms = n ? (float)sqrt( sum / n ) : 0.0f;
		db = ( rms > 0.00001f ) ? 20.0f * log10f( rms ) : -100.0f;
		v->level = Com_Clamp( 0.0f, 1.0f, ( db + 50.0f ) / 40.0f );	// (-50 dB silent .. -10 dB loud)
		lip = ( db < -45.0f ) ? -1 : ( db < -36.0f ) ? 1 : ( db < -28.0f ) ? 2 : ( db < -20.0f ) ? 3 : 4;
		if ( lip > s_entityWavVol[i] || !s_entityWavVol[i] ) {
			s_entityWavVol[i] = lip;
		}
	}
}

// Is that player talking (voice chat), and how loud: 0..1, -1 not talking.
float S_VoiceLevel( int speaker ) {
	if ( speaker < 0 || speaker >= MAX_CLIENTS || !s_soundStarted ) {
		return -1.0f;
	}
	if ( s_voice[speaker].end <= s_soundtime || s_voice[speaker].start > s_soundtime ) {
		return -1.0f;
	}
	return s_voice[speaker].level;
}
