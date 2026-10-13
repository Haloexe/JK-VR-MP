/*
===========================================================================
OpenJK VR (multiplayer) - proximity voice chat: the microphone, and the
voices coming in (vr_voice.h for what goes over the network).

With cl_voice on, and connected to a server that relays voice (sv_voice):
  - the microphone (cl_voiceMic: a name from cl_voiceMic0..9, "" the
    system's default) is read every frame, 20 ms at a time, made louder or
    quieter (cl_voiceGain) and measured (cl_voiceLevel, for the menu);
  - it's sent while you talk - when it's louder than cl_voiceThreshold
    (cl_voiceMode 0, voice activation: and for a moment after, so words
    aren't cut off), or while +voice is held (cl_voiceMode 1, push to
    talk) - encoded with Opus, with the next packet to the server;
  - other players' voices come in after the snapshots and are played from
    where they are (snd_voice.cpp; cl_voiceVolume). voice_mute and
    voice_unmute silence someone.
The microphone is also open while the menu shows the voice chat settings,
for their level meter (cl_voiceMenu).

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "client.h"
#include "qcommon/vr_voice.h"

#ifdef OPENJK_VOICE
#include <SDL.h>
#include <opus.h>
#endif

cvar_t	*cl_voice;			// 1: voice chat (talk, and hear the others)
cvar_t	*cl_voiceMode;		// 0: voice activation, 1: push to talk (+voice)
cvar_t	*cl_voiceThreshold;	// voice activation: how loud before it sends, 0..1 (as cl_voiceLevel)
cvar_t	*cl_voiceGain;		// the microphone, louder or quieter
cvar_t	*cl_voiceMic;		// which microphone ("": the system's default)
cvar_t	*cl_voiceProtocol;	// (userinfo, read only) VOICE_PROTOCOL while voice chat is on
cvar_t	*cl_voiceLevel;		// (read only) how loud the microphone is, 0..1 (-60..0 dB)
cvar_t	*cl_voiceSending;	// (read only) 1 while you're being heard
cvar_t	*cl_voiceMicStatus;	// (read only) the microphone: 1 open, -1 none would open, 0 closed
cvar_t	*cl_voiceMenu;		// (set by the menu) 1 while its voice chat settings are up: the
							// microphone is open for their meter, and the list kept fresh
cvar_t	*cl_voiceMicCount;	// (read only) the microphones there are...
#define VOICE_MAX_MICS		10
cvar_t	*cl_voiceMicName[VOICE_MAX_MICS];	// ...cl_voiceMic0..9

#ifdef OPENJK_VOICE

#define VOICE_OUT_FRAMES		16		// frames waiting for the next packet to the server (320 ms)
#define VOICE_HANGOVER_MS		400		// voice activation goes on sending this long after the last loud frame
#define VOICE_MAX_BACKLOG_MS	300		// more than this waiting in the microphone (a hitch): the older goes
#define VOICE_RETRY_MS			5000	// a microphone that didn't open is tried again this long after
#define VOICE_LIST_MS			2000	// the microphones are looked for again this often while the menu is up

static struct {
	// the microphone
	SDL_AudioDeviceID	dev;
	char				devName[MAX_CVAR_VALUE_STRING];	// cl_voiceMic it opened for
	qboolean			fallback;			// ...but that one wasn't there: the default is open
	int					fallbackTime;		// cls.realtime it was last looked for
	qboolean			sdlAudio;			// (we hold a reference to SDL's audio)
	int					retryTime;			// cls.realtime it may be tried again
	int					listTime;			// cls.realtime the microphones were last listed
	qboolean			menuShown;			// the menu's voice chat settings were up last frame
	short				frame[VOICE_FRAME_SAMPLES];	// the 20 ms being filled...
	int					fill;
	short				prev[VOICE_FRAME_SAMPLES];	// ...and the one before it (sent first when you
	qboolean			prevValid;						// start talking: the first sound of a word)
	float				peak;				// loudest since cl_voiceLevel was last set
	int					levelTime;
	// sending
	OpusEncoder			*enc;
	qboolean			talking;			// sending
	int					hangover;			// voice activation: ms more after the last loud frame
	qboolean			ptt;				// +voice held
	int					generation, sequence;
	struct {
		int		generation, sequence, length;
		byte	data[VOICE_MAX_FRAME_BYTES];
	}					out[VOICE_OUT_FRAMES];
	int					numOut;
	// receiving
	struct {
		OpusDecoder		*dec;
		qboolean		started;
		int				generation;
		int				nextSequence;
		int				time;			// cls.realtime of their last packet
	}					in[MAX_CLIENTS];
	qboolean			muted[MAX_CLIENTS];
} voice;

/*
--------------------------------------------------------------------------------
The microphone
--------------------------------------------------------------------------------
*/

static qboolean CL_VoiceAudio( void ) {
	if ( voice.sdlAudio && !SDL_WasInit( SDL_INIT_AUDIO ) ) {
		voice.sdlAudio = qfalse;	// (all of SDL's audio was shut down under us)
	}
	if ( !voice.sdlAudio ) {
		if ( SDL_InitSubSystem( SDL_INIT_AUDIO ) != 0 ) {
			return qfalse;
		}
		voice.sdlAudio = qtrue;	// (the sound system's own SDL_QuitSubSystem leaves it up for us)
	}
	return qtrue;
}

// A device's name as the menu can print it (and cl_voiceMic keeps it).
static void CL_VoiceCleanName( const char *name, char *clean, int size ) {
	int j, k = 0;

	for ( j = 0; name && name[j] && k < size - 1; j++ ) {
		const unsigned char c = (unsigned char)name[j];
		clean[k++] = ( c >= 32 && c < 127 && c != '"' && c != ';' ) ? (char)c : '?';
	}
	clean[k] = 0;
}

// The microphone cl_voiceMic names, as SDL knows it (its name may have had
// characters the menu can't print). NULL: not there.
static const char *CL_VoiceRealName( const char *clean ) {
	int i;
	const int n = SDL_GetNumAudioDevices( 1 );

	for ( i = 0; i < n; i++ ) {
		const char *name = SDL_GetAudioDeviceName( i, 1 );
		char c[MAX_CVAR_VALUE_STRING];
		if ( !name ) {
			continue;
		}
		CL_VoiceCleanName( name, c, sizeof( c ) );
		if ( !strcmp( c, clean ) ) {
			return name;
		}
	}
	return NULL;
}

// The microphones there are, into cl_voiceMicCount and cl_voiceMic0..9 (the
// menu's list). verbose: and to the console.
static void CL_VoiceListMics( qboolean verbose ) {
	int i, n;

	voice.listTime = cls.realtime;
	if ( !CL_VoiceAudio() ) {
		Cvar_Set( "cl_voiceMicCount", "0" );
		if ( verbose ) {
			Com_Printf( "Voice chat: no audio (%s)\n", SDL_GetError() );
		}
		return;
	}
	n = SDL_GetNumAudioDevices( 1 );
	if ( n < 0 ) {
		n = 0;
	}
	if ( n > VOICE_MAX_MICS ) {
		n = VOICE_MAX_MICS;
	}
	for ( i = 0; i < VOICE_MAX_MICS; i++ ) {
		const char *name = ( i < n ) ? SDL_GetAudioDeviceName( i, 1 ) : NULL;
		char clean[MAX_CVAR_VALUE_STRING];
		CL_VoiceCleanName( name, clean, sizeof( clean ) );
		if ( strcmp( cl_voiceMicName[i]->string, clean ) ) {
			Cvar_Set( va( "cl_voiceMic%i", i ), clean );
		}
		if ( verbose && i < n ) {
			Com_Printf( "  %i: %s%s\n", i, clean, !Q_stricmp( clean, cl_voiceMic->string ) ? "  (chosen)" : "" );
		}
	}
	if ( cl_voiceMicCount->integer != n ) {
		Cvar_Set( "cl_voiceMicCount", va( "%i", n ) );
	}
	if ( verbose ) {
		Com_Printf( "%i microphone(s); cl_voiceMic \"%s\" (\"\": the system's default)\n", n, cl_voiceMic->string );
	}
}

static void CL_VoiceMicStatus( int status ) {
	if ( cl_voiceMicStatus->integer != status ) {
		Cvar_Set( "cl_voiceMicStatus", va( "%i", status ) );
	}
}

static void CL_VoiceCloseMic( void ) {
	if ( voice.dev ) {
		SDL_CloseAudioDevice( voice.dev );
		voice.dev = 0;
	}
	voice.fill = 0;
	voice.prevValid = qfalse;
	voice.talking = qfalse;
	voice.hangover = 0;
	voice.peak = 0.0f;
	Cvar_Set( "cl_voiceLevel", "0" );
	Cvar_Set( "cl_voiceSending", "0" );
	CL_VoiceMicStatus( 0 );
}

static qboolean CL_VoiceOpenMic( void ) {
	SDL_AudioSpec want, have;
	const char *name = NULL;

	CL_VoiceCloseMic();
	Q_strncpyz( voice.devName, cl_voiceMic->string, sizeof( voice.devName ) );
	voice.fallback = qfalse;
	voice.fallbackTime = cls.realtime;
	if ( !CL_VoiceAudio() ) {
		Com_Printf( "Voice chat: no audio (%s)\n", SDL_GetError() );
		CL_VoiceMicStatus( -1 );
		return qfalse;
	}
	memset( &want, 0, sizeof( want ) );
	want.freq = VOICE_RATE;
	want.format = AUDIO_S16SYS;
	want.channels = 1;
	want.samples = VOICE_FRAME_SAMPLES / 2;
	if ( cl_voiceMic->string[0] ) {
		name = CL_VoiceRealName( cl_voiceMic->string );
		if ( name ) {
			voice.dev = SDL_OpenAudioDevice( name, 1, &want, &have, 0 );	// (SDL converts to this)
		}
		if ( !voice.dev ) {
			Com_Printf( "Voice chat: microphone \"%s\" %s - using the default one\n", cl_voiceMic->string,
				name ? "didn't open" : "isn't there" );
			name = NULL;
			voice.fallback = qtrue;	// (until it's back)
		}
	}
	if ( !voice.dev ) {
		voice.dev = SDL_OpenAudioDevice( NULL, 1, &want, &have, 0 );
	}
	if ( !voice.dev ) {
		Com_Printf( "Voice chat: no microphone (%s)\n", SDL_GetError() );
		CL_VoiceMicStatus( -1 );
		return qfalse;
	}
	SDL_PauseAudioDevice( voice.dev, 0 );
	CL_VoiceMicStatus( 1 );
	Com_Printf( "Voice chat: microphone \"%s\"\n", name ? cl_voiceMic->string : "(the system's default)" );
	return qtrue;
}

/*
--------------------------------------------------------------------------------
Sending
--------------------------------------------------------------------------------
*/

static void CL_VoiceEncode( const short *pcm ) {
	byte data[VOICE_MAX_FRAME_BYTES];
	int len;

	if ( !voice.enc ) {
		return;
	}
	len = opus_encode( voice.enc, pcm, VOICE_FRAME_SAMPLES, data, sizeof( data ) );
	if ( len < 1 ) {
		return;
	}
	if ( voice.numOut == VOICE_OUT_FRAMES ) {
		memmove( &voice.out[0], &voice.out[1], sizeof( voice.out[0] ) * ( VOICE_OUT_FRAMES - 1 ) );	// (the oldest goes)
		voice.numOut--;
	}
	voice.out[voice.numOut].generation = voice.generation;
	voice.out[voice.numOut].sequence = voice.sequence++;
	voice.out[voice.numOut].length = len;
	memcpy( voice.out[voice.numOut].data, data, len );
	voice.numOut++;
}

// Can what you say go to a server now?
static qboolean CL_VoiceCanSend( void ) {
	return (qboolean)( cl_voice->integer && cls.state == CA_ACTIVE && clc.voiceExt && !clc.demoplaying );
}

// 20 ms of the microphone: louder or quieter, measured, and sent if you're
// talking.
static void CL_VoiceFrameDone( void ) {
	const float gain = Com_Clamp( 0.1f, 8.0f, cl_voiceGain->value );
	double sum = 0.0;
	float rms, db, level;
	qboolean talk;
	int i;

	for ( i = 0; i < VOICE_FRAME_SAMPLES; i++ ) {
		const int s = (int)( voice.frame[i] * gain );
		voice.frame[i] = (short)Com_Clampi( -32768, 32767, s );
		sum += (double)voice.frame[i] * voice.frame[i];
	}
	rms = (float)sqrt( sum / VOICE_FRAME_SAMPLES ) * ( 1.0f / 32768.0f );
	db = ( rms > 0.000001f ) ? 20.0f * log10f( rms ) : -120.0f;
	level = Com_Clamp( 0.0f, 1.0f, ( db + 60.0f ) / 60.0f );
	if ( level > voice.peak ) {
		voice.peak = level;
	}

	if ( cl_voiceMode->integer == 1 ) {
		talk = voice.ptt;
	} else {
		if ( level >= Com_Clamp( 0.0f, 1.0f, cl_voiceThreshold->value ) ) {
			voice.hangover = VOICE_HANGOVER_MS;
		} else if ( voice.hangover > 0 ) {
			voice.hangover -= VOICE_FRAME_MS;
		}
		talk = (qboolean)( voice.hangover > 0 );
	}

	if ( talk && CL_VoiceCanSend() ) {
		if ( !voice.talking ) {
			// starting to talk: a new generation, from the frame before (the
			// start of the first word)
			voice.generation = ( voice.generation + 1 ) & 0xff;
			voice.sequence = 0;
			opus_encoder_ctl( voice.enc, OPUS_RESET_STATE );
			if ( voice.prevValid && cl_voiceMode->integer != 1 ) {
				CL_VoiceEncode( voice.prev );
			}
			voice.talking = qtrue;
		}
		CL_VoiceEncode( voice.frame );
	} else {
		voice.talking = qfalse;
	}
	memcpy( voice.prev, voice.frame, sizeof( voice.prev ) );
	voice.prevValid = qtrue;
}

static void CL_VoiceCapture( void ) {
	short buf[VOICE_FRAME_SAMPLES * 2];
	Uint32 got;
	int i;

	if ( SDL_GetQueuedAudioSize( voice.dev ) > (Uint32)( VOICE_RATE / 1000 * VOICE_MAX_BACKLOG_MS * sizeof( short ) ) ) {
		SDL_ClearQueuedAudio( voice.dev );	// (we stalled: what's old goes)
		voice.fill = 0;
	}
	while ( ( got = SDL_DequeueAudio( voice.dev, buf, sizeof( buf ) ) ) >= sizeof( short ) ) {
		const int n = (int)( got / sizeof( short ) );
		for ( i = 0; i < n; i++ ) {
			voice.frame[voice.fill++] = buf[i];
			if ( voice.fill == VOICE_FRAME_SAMPLES ) {
				CL_VoiceFrameDone();
				voice.fill = 0;
			}
		}
	}
}

/*
=================
CL_VoiceFrame

Once a client frame, before the usercmds go out: the microphone opened or
closed as needed, and read.
=================
*/
void CL_VoiceFrame( void ) {
	const qboolean ui = (qboolean)( ( Key_GetCatcher() & KEYCATCH_UI ) != 0 );
	const qboolean menu = (qboolean)( ui && cl_voiceMenu->integer );	// (the voice chat settings are up)
	const qboolean want = (qboolean)( cl_voice->integer && ( CL_VoiceCanSend() || menu ) );
	const int protocol = cl_voice->integer ? VOICE_PROTOCOL : 0;

	if ( cl_voiceProtocol->integer != protocol ) {
		Cvar_Set( "cl_voiceProtocol", va( "%i", protocol ) );	// (the server sends voice only if asked)
	}
	if ( !ui && cl_voiceMenu->integer ) {
		Cvar_Set( "cl_voiceMenu", "0" );	// (the menu's gone)
	}
	if ( menu && ( !voice.menuShown || cls.realtime - voice.listTime >= VOICE_LIST_MS || cls.realtime < voice.listTime ) ) {
		CL_VoiceListMics( qfalse );	// (as they come up, and again for one plugged in or out)
	}
	voice.menuShown = menu;

	if ( voice.dev && ( !want || strcmp( voice.devName, cl_voiceMic->string ) ) ) {
		CL_VoiceCloseMic();
		voice.retryTime = 0;
	}
	if ( voice.dev && ( !SDL_WasInit( SDL_INIT_AUDIO ) || SDL_GetAudioDeviceStatus( voice.dev ) == SDL_AUDIO_STOPPED ) ) {
		// unplugged, or closed with the sound system (snd_restart): opened
		// again (the default, if the one chosen has gone)
		Com_Printf( "Voice chat: the microphone stopped - opening it again\n" );
		CL_VoiceCloseMic();
		voice.retryTime = 0;
	}
	if ( voice.dev && voice.fallback && ( cls.realtime - voice.fallbackTime >= VOICE_RETRY_MS || cls.realtime < voice.fallbackTime ) ) {
		// using the default while the one chosen isn't there: is it back?
		voice.fallbackTime = cls.realtime;
		if ( CL_VoiceRealName( cl_voiceMic->string ) ) {
			CL_VoiceCloseMic();
			voice.retryTime = 0;
		}
	}
	if ( !want ) {
		voice.numOut = 0;
		CL_VoiceMicStatus( 0 );
		return;
	}
	if ( !voice.dev ) {
		if ( cls.realtime < voice.retryTime && !strcmp( voice.devName, cl_voiceMic->string ) ) {
			return;	// (one that didn't open: tried again in a while, or as soon as another is chosen)
		}
		if ( !CL_VoiceOpenMic() ) {
			voice.retryTime = cls.realtime + VOICE_RETRY_MS;
			return;
		}
	}
	CL_VoiceCapture();

	// (for the menu's meter, ten times a second)
	if ( cls.realtime - voice.levelTime >= 100 || cls.realtime < voice.levelTime ) {
		voice.levelTime = cls.realtime;
		Cvar_Set( "cl_voiceLevel", va( "%.2f", voice.peak ) );
		voice.peak = 0.0f;
		if ( cl_voiceSending->integer != ( voice.talking ? 1 : 0 ) ) {
			Cvar_Set( "cl_voiceSending", voice.talking ? "1" : "0" );
		}
	}
}

/*
=================
CL_VoiceWritePackets

Into the packet to the server, before its usercmds: the frames since the
last one, a clc_voice for every run of them.
=================
*/
void CL_VoiceWritePackets( msg_t *msg ) {
	int i = 0;

	if ( !voice.numOut ) {
		return;
	}
	if ( !CL_VoiceCanSend() ) {
		voice.numOut = 0;
		return;
	}
	while ( i < voice.numOut ) {
		voicePacket_t p;
		int j = i;

		p.generation = voice.out[i].generation;
		p.sequence = voice.out[i].sequence;
		p.frames = 0;
		p.length = 0;
		while ( j < voice.numOut && p.frames < VOICE_MAX_PACKET_FRAMES && voice.out[j].generation == p.generation &&
			voice.out[j].sequence == p.sequence + p.frames ) {
			p.data[p.length++] = (byte)voice.out[j].length;
			memcpy( p.data + p.length, voice.out[j].data, voice.out[j].length );
			p.length += voice.out[j].length;
			p.frames++;
			j++;
		}
		MSG_WriteByte( msg, clc_voice );
		Voice_WritePacket( msg, &p );
		i = j;
	}
	voice.numOut = 0;
}

/*
--------------------------------------------------------------------------------
Receiving
--------------------------------------------------------------------------------
*/

static void CL_VoicePlay( int speaker, const voicePacket_t *p ) {
	short pcm[VOICE_FRAME_SAMPLES];
	int f, pos = 0, missing;

	if ( speaker < 0 || speaker >= MAX_CLIENTS || speaker == clc.clientNum || !cl_voice->integer || voice.muted[speaker] ) {
		return;
	}
	if ( !voice.in[speaker].dec ) {
		int err = OPUS_OK;
		voice.in[speaker].dec = opus_decoder_create( VOICE_RATE, 1, &err );
		if ( err != OPUS_OK || !voice.in[speaker].dec ) {
			voice.in[speaker].dec = NULL;
			return;
		}
		voice.in[speaker].started = qfalse;
	}
	if ( !voice.in[speaker].started || p->generation != voice.in[speaker].generation ||
		p->sequence > voice.in[speaker].nextSequence + 50 || p->sequence + p->frames < voice.in[speaker].nextSequence - 50 ||
		cls.realtime - voice.in[speaker].time > 2000 || cls.realtime < voice.in[speaker].time ) {
		// they started talking again (or it's all new): from here
		opus_decoder_ctl( voice.in[speaker].dec, OPUS_RESET_STATE );
		voice.in[speaker].started = qtrue;
		voice.in[speaker].generation = p->generation;
		voice.in[speaker].nextSequence = p->sequence;
	}
	voice.in[speaker].time = cls.realtime;
	// a few frames lost on the way: guessed (Opus fills them in)
	missing = p->sequence - voice.in[speaker].nextSequence;
	if ( missing > 0 && missing <= 3 ) {
		while ( missing-- > 0 ) {
			const int n = opus_decode( voice.in[speaker].dec, NULL, 0, pcm, VOICE_FRAME_SAMPLES, 0 );
			if ( n > 0 ) {
				S_VoiceSamples( speaker, n, VOICE_RATE, pcm );
			}
		}
	}
	for ( f = 0; f < p->frames; f++ ) {
		const int len = p->data[pos++];
		const byte *data = p->data + pos;
		const int seq = p->sequence + f;
		int n;
		pos += len;
		if ( seq < voice.in[speaker].nextSequence ) {
			continue;	// (had it already)
		}
		n = opus_decode( voice.in[speaker].dec, data, len, pcm, VOICE_FRAME_SAMPLES, 0 );
		if ( n > 0 ) {
			S_VoiceSamples( speaker, n, VOICE_RATE, pcm );
		}
		voice.in[speaker].nextSequence = seq + 1;
	}
}

/*
--------------------------------------------------------------------------------
Commands
--------------------------------------------------------------------------------
*/

static void CL_VoicePTTDown_f( void ) {
	voice.ptt = qtrue;
}

static void CL_VoicePTTUp_f( void ) {
	voice.ptt = qfalse;
}

static void CL_VoiceMics_f( void ) {
	CL_VoiceListMics( qtrue );
}

// "voice_mic <number>": that one of voice_mics' list (-1: the default).
static void CL_VoiceMic_f( void ) {
	int n;

	if ( Cmd_Argc() != 2 ) {
		Com_Printf( "usage: voice_mic <number from voice_mics> (-1: the system's default)\n" );
		return;
	}
	CL_VoiceListMics( qfalse );
	n = atoi( Cmd_Argv( 1 ) );
	if ( n < 0 ) {
		Cvar_Set( "cl_voiceMic", "" );
	} else if ( n < cl_voiceMicCount->integer ) {
		Cvar_Set( "cl_voiceMic", cl_voiceMicName[n]->string );
	} else {
		Com_Printf( "There's no microphone %i (voice_mics lists them)\n", n );
	}
}

#else // !OPENJK_VOICE

// (built without voice chat: nothing to capture or send, and the server is
// never asked for voices)
void CL_VoiceFrame( void ) {}
void CL_VoiceWritePackets( msg_t *msg ) {}

#endif // OPENJK_VOICE

// A player by number or (part of their) name, from the configstrings.
static int CL_VoiceFindPlayer( const char *s ) {
	int i, found = -1;

	if ( s[0] >= '0' && s[0] <= '9' ) {
		i = atoi( s );
		return ( i >= 0 && i < MAX_CLIENTS ) ? i : -1;
	}
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		const char *info = cl.gameState.stringData + cl.gameState.stringOffsets[CS_PLAYERS + i];
		char name[MAX_NAME_LENGTH];
		if ( !info[0] ) {
			continue;
		}
		Q_strncpyz( name, Info_ValueForKey( info, "n" ), sizeof( name ) );
		Q_StripColor( name );
		if ( !Q_stricmp( name, s ) ) {
			return i;
		}
		if ( found < 0 && Q_stristr( name, s ) ) {
			found = i;
		}
	}
	return found;
}

static void CL_VoiceMute_f( void ) {
	const qboolean mute = (qboolean)!Q_stricmp( Cmd_Argv( 0 ), "voice_mute" );
	int n;

	if ( Cmd_Argc() != 2 ) {
		Com_Printf( "usage: %s <player name or number>%s\n", Cmd_Argv( 0 ), mute ? "" : " (or all)" );
		return;
	}
#ifdef OPENJK_VOICE
	if ( !mute && !Q_stricmp( Cmd_Argv( 1 ), "all" ) ) {
		memset( voice.muted, 0, sizeof( voice.muted ) );
		Com_Printf( "Nobody is muted\n" );
		return;
	}
	n = CL_VoiceFindPlayer( Cmd_Argv( 1 ) );
	if ( n < 0 ) {
		Com_Printf( "No player \"%s\"\n", Cmd_Argv( 1 ) );
		return;
	}
	voice.muted[n] = mute;
	Com_Printf( "Player %i %s\n", n, mute ? "muted" : "unmuted" );
#else
	(void)n;
	Com_Printf( "This build has no voice chat\n" );
#endif
}

/*
=================
CL_ParseVoice

svc_voice: another player's voice.
=================
*/
void CL_ParseVoice( msg_t *msg ) {
	voicePacket_t p;
	vec3_t origin;
	const int speaker = MSG_ReadByte( msg );

	Voice_ReadOrigin( msg, origin );
	if ( !Voice_ReadPacket( msg, &p ) ) {
		Com_Error( ERR_DROP, "CL_ParseVoice: bad voice data" );
		return;
	}
#ifdef OPENJK_VOICE
	if ( speaker >= 0 && speaker < MAX_CLIENTS ) {
		S_VoiceSpeakerOrigin( speaker, origin );	// (they may be out of sight)
	}
	CL_VoicePlay( speaker, &p );
#endif
}

// A player left (their configstring emptied): whoever takes their place
// isn't muted, and their voice is a new one.
void CL_VoiceSpeakerGone( int speaker ) {
	if ( speaker < 0 || speaker >= MAX_CLIENTS ) {
		return;
	}
#ifdef OPENJK_VOICE
	voice.in[speaker].started = qfalse;
	voice.muted[speaker] = qfalse;
#endif
	S_VoiceSpeakerReset( speaker );
}

// Left the server: nobody's voice carries on, nothing waits to go.
void CL_VoiceDisconnect( void ) {
#ifdef OPENJK_VOICE
	int i;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		voice.in[i].started = qfalse;
	}
	memset( voice.muted, 0, sizeof( voice.muted ) );
	voice.numOut = 0;
	voice.talking = qfalse;
	voice.hangover = 0;
#endif
	S_VoiceClear();
}

void CL_VoiceInit( void ) {
	int i;

	cl_voice = Cvar_Get( "cl_voice", "1", CVAR_ARCHIVE_ND, "Voice chat: talk, and hear the players near you" );
	cl_voiceMode = Cvar_Get( "cl_voiceMode", "0", CVAR_ARCHIVE_ND, "Voice chat: 0 sends when you talk, 1 while +voice is held (push to talk)" );
	cl_voiceThreshold = Cvar_Get( "cl_voiceThreshold", "0.33", CVAR_ARCHIVE_ND, "Voice chat: how loud you have to be before it sends (0..1, as cl_voiceLevel)" );
	cl_voiceGain = Cvar_Get( "cl_voiceGain", "1", CVAR_ARCHIVE_ND, "Voice chat: microphone volume" );
	cl_voiceMic = Cvar_Get( "cl_voiceMic", "", CVAR_ARCHIVE_ND, "Voice chat: the microphone (voice_mics lists them; empty: the system's default)" );
	cl_voiceProtocol = Cvar_Get( "cl_voiceProtocol", "0", CVAR_USERINFO | CVAR_ROM, "Voice chat protocol (tells the server to send voice)" );
	cl_voiceLevel = Cvar_Get( "cl_voiceLevel", "0", CVAR_ROM, "Voice chat: how loud the microphone is now (0..1)" );
	cl_voiceSending = Cvar_Get( "cl_voiceSending", "0", CVAR_ROM, "Voice chat: 1 while you're being heard" );
	cl_voiceMicStatus = Cvar_Get( "cl_voiceMicStatus", "0", CVAR_ROM, "Voice chat: the microphone is open (1), none would open (-1), or closed (0)" );
	cl_voiceMenu = Cvar_Get( "cl_voiceMenu", "0", CVAR_TEMP, "Voice chat: the menu shows its settings (the microphone is open for their meter)" );
	cl_voiceMicCount = Cvar_Get( "cl_voiceMicCount", "0", CVAR_ROM, "Voice chat: how many microphones there are" );
	for ( i = 0; i < VOICE_MAX_MICS; i++ ) {
		cl_voiceMicName[i] = Cvar_Get( va( "cl_voiceMic%i", i ), "", CVAR_ROM );
	}
	Cvar_Get( "cl_voiceVolume", "1", CVAR_ARCHIVE_ND, "How loud other players' voices are (voice chat)" );

	Cmd_AddCommand( "voice_mute", CL_VoiceMute_f, "Voice chat: silence a player" );
	Cmd_AddCommand( "voice_unmute", CL_VoiceMute_f, "Voice chat: hear a player again (or all)" );
#ifdef OPENJK_VOICE
	Cmd_AddCommand( "+voice", CL_VoicePTTDown_f, "Voice chat: push to talk (cl_voiceMode 1)" );
	Cmd_AddCommand( "-voice", CL_VoicePTTUp_f, NULL );
	Cmd_AddCommand( "voice_mics", CL_VoiceMics_f, "Voice chat: list the microphones" );
	Cmd_AddCommand( "voice_mic", CL_VoiceMic_f, "Voice chat: pick a microphone from voice_mics' list" );

	{
		int err = OPUS_OK;
		voice.enc = opus_encoder_create( VOICE_RATE, 1, OPUS_APPLICATION_VOIP, &err );
		if ( err != OPUS_OK || !voice.enc ) {
			Com_Printf( "Voice chat: the Opus encoder didn't start (%s)\n", opus_strerror( err ) );
			voice.enc = NULL;
		} else {
			opus_encoder_ctl( voice.enc, OPUS_SET_BITRATE( VOICE_BITRATE ) );
			opus_encoder_ctl( voice.enc, OPUS_SET_VBR_CONSTRAINT( 1 ) );	// (no frame much bigger than the rest)
			opus_encoder_ctl( voice.enc, OPUS_SET_COMPLEXITY( 5 ) );
			opus_encoder_ctl( voice.enc, OPUS_SET_SIGNAL( OPUS_SIGNAL_VOICE ) );
		}
	}
	voice.listTime = -VOICE_LIST_MS;
#endif
}

void CL_VoiceShutdown( void ) {
#ifdef OPENJK_VOICE
	int i;

	CL_VoiceCloseMic();
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		if ( voice.in[i].dec ) {
			opus_decoder_destroy( voice.in[i].dec );
			voice.in[i].dec = NULL;
		}
	}
	if ( voice.enc ) {
		opus_encoder_destroy( voice.enc );
		voice.enc = NULL;
	}
	if ( voice.sdlAudio ) {
		SDL_QuitSubSystem( SDL_INIT_AUDIO );
		voice.sdlAudio = qfalse;
	}
	Cmd_RemoveCommand( "+voice" );
	Cmd_RemoveCommand( "-voice" );
	Cmd_RemoveCommand( "voice_mics" );
	Cmd_RemoveCommand( "voice_mic" );
#endif
	Cmd_RemoveCommand( "voice_mute" );
	Cmd_RemoveCommand( "voice_unmute" );
}
