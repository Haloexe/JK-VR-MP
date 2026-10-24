/*
===========================================================================
OpenJK VR (multiplayer) - the Force hand, on the server.

A VR player with Force gestures on (vr_forceGestures - the engine's,
qcommon/vr_force.h) says with every command which hand their Force comes
from and where it points (their avatar: vrAvatar_t.forceValid, forceHand,
forceDir), and with a gesture's Push or Pull, the way it went
(vrUserPose_t.thrust). With g_vrForce on, their Push, Pull, Grip,
Lightning and Drain come from that hand, however they're used - a
gesture, the Force grip's quick squeeze, a key:

  Push, Pull         the way the hand thrust it (a gesture's), else where
                     it points
  Grip               whom it catches, where it points (no one further
                     from them than the game's held Grip holds - nor anyone
                     once a Push or a Pull has gone with its button held,
                     nobody caught, till it's let go); then held
                     as long as the game holds a Grip (DoGripAction: level
                     1 five seconds, 2 and 3 four), below level 3 only
                     while it points within the game's 25 degrees of them,
                     side to side (InFront: up and down don't count) -
                     level 3 floats them along where it points
  Lightning, Drain   where it points

each snapped onto someone within VR_FORCE_SNAP degrees of that line - the
one nearest it - alive, in the power's reach and in sight of the hand, not
on your side, the power working on them (ForcePowerUsableOn: not down, not
your companion, not in a duel...; Drain, some Force in them) - but never
onto, nor off, someone hidden from you (cloaked, or mind-tricking you,
while your Seeing isn't going): nearest the line, it stays as it is (as
the game's own aim would have it, hitting them if it's on them). The cgame
draws it so too (cg_vrforce.c's CG_VR_ForceSnap). The rest of each power is
the game's own (w_force.c): its levels, reach and cones, who it works on,
what it costs. The hand is where the player's avatar has it (within reach
of their eyes: g_vrsaber.c), from where they are now; a hand reaching
into a wall, or into someone, gives way there. Their commands coming
without an avatar, it's gone at once; not coming, VR_FORCE_FRESH on.

Everyone else sees the Force come from that hand too: the avatar the
server passes on keeps it (cg_vrforce.c draws it). g_vrForce 0: the game's
own aim, where they look - and no Force hand passed on. "vrForce" in the
serverinfo says which.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "g_local.h"

#define VR_FORCE_SNAP		10.0f	// degrees: someone this near the hand's line is who it's aimed at
#define VR_FORCE_FRESH		250		// ms: a Force hand not heard of for this long is gone
#define VR_FORCE_LOG_MS		1000	// (the log: a power aimed at the same one again within this isn't said again...
#define VR_FORCE_LOG_HELD_MS	250		// ...nor a held one - Lightning, Drain - aimed anew within this: its hand wavering)

typedef struct {
	qboolean	valid;
	int			time;		// level.time it came
	int			hand;		// which (VR_HAND_*)
	vec3_t		ofs;		// where it is, from the player's origin
	vec3_t		dir;		// where it points (unit)
	qboolean	thrown;		// this command's Push or Pull came from a gesture: the way it went...
	vec3_t		thrust;		// ...(unit)
	int			logPower, logOnto, logTime;	// (what the log said last, when)
} vrForceHand_t;

static vrForceHand_t	vrForce[MAX_CLIENTS];
static qboolean			vrGripSpent[MAX_CLIENTS];	// a Push or a Pull went while their Grip button was held, nobody caught: no
													// catch till it's let go (their Force hand gone a moment or not)

void G_VRForceInit( void ) {
	memset( vrForce, 0, sizeof( vrForce ) );
	memset( vrGripSpent, 0, sizeof( vrGripSpent ) );
	trap->Cvar_Register( NULL, "vrForce", "0", CVAR_SERVERINFO | CVAR_ROM );
	G_VRForcePublish();
}

// Whether VR players' Force powers come from their Force hand here.
qboolean G_VRForceOn( void ) {
	return (qboolean)( G_VRPosesAvailable() && g_vrForce.integer > 0 );
}

// "vrForce 1" in the serverinfo: VR players' Force comes from their hand
// here (their game draws it so).
void G_VRForcePublish( void ) {
	trap->Cvar_Set( "vrForce", G_VRForceOn() ? "1" : "0" );
}

// A player's slot taken or left: nothing of theirs kept.
void G_VRForceClientReset( int clientNum ) {
	if ( clientNum >= 0 && clientNum < MAX_CLIENTS ) {
		memset( &vrForce[clientNum], 0, sizeof( vrForce[clientNum] ) );
		vrGripSpent[clientNum] = qfalse;
	}
}

// Their commands coming without an avatar (VRS_TakeAvatar): no Force hand from
// now - a Grip spent kept, its button still held.
void G_VRForceDrop( int clientNum ) {
	if ( clientNum >= 0 && clientNum < MAX_CLIENTS ) {
		memset( &vrForce[clientNum], 0, sizeof( vrForce[clientNum] ) );
	}
}

// A unit vector, or qfalse (none: NaN, infinite, nothing).
static qboolean VRF_UnitDir( const vec3_t in, vec3_t out ) {
	if ( !( fabsf( in[0] ) < 1e6f && fabsf( in[1] ) < 1e6f && fabsf( in[2] ) < 1e6f ) ) {
		return qfalse;
	}
	VectorCopy( in, out );
	return (qboolean)( VectorNormalize( out ) > 0.5f );
}

// A command's avatar, its hands made offsets from the player's origin
// (VRS_TakeAvatar): their Force hand, if they have one and the server takes it
// - else it's not passed on (a).
void G_VRForceTake( gentity_t *ent, vrAvatar_t *a, const vrUserPose_t *pose ) {
	const int n = ent ? ent->s.number : -1;
	vrForceHand_t *f;
	vec3_t dir;
	int h;

	if ( n < 0 || n >= MAX_CLIENTS ) {
		return;
	}
	f = &vrForce[n];
	h = a->forceHand;
	if ( !a->forceValid || !G_VRForceOn() || ( h != VR_HAND_LEFT && h != VR_HAND_RIGHT ) ||
		a->hand[h].grip != VR_GRIP_FREE || !VRF_UnitDir( a->forceDir, dir ) ) {
		a->forceValid = qfalse;
		a->forceHand = 0;
		VectorClear( a->forceDir );
		f->valid = f->thrown = qfalse;
		return;
	}
	VectorCopy( dir, a->forceDir );
	f->valid = qtrue;
	f->time = level.time;
	f->hand = h;
	VectorCopy( a->hand[h].origin, f->ofs );
	VectorCopy( dir, f->dir );
	f->thrown = (qboolean)( pose && pose->thrustValid && VRF_UnitDir( pose->thrust, f->thrust ) );
}

// After the command's been run (ClientThink): its Push or Pull's way is used
// up; its Grip button not held, a Grip can catch again.
void G_VRForceCmdDone( gentity_t *ent ) {
	const int n = ent ? ent->s.number : -1;

	if ( n >= 0 && n < MAX_CLIENTS ) {
		vrForce[n].thrown = qfalse;
		if ( !ent->client || !( ent->client->pers.cmd.buttons & BUTTON_FORCEGRIP ) ) {
			vrGripSpent[n] = qfalse;
		}
	}
}

// Their Force hand, said lately (NULL: none).
static vrForceHand_t *VRF_Hand( gentity_t *self ) {
	const int n = self ? self->s.number : -1;
	vrForceHand_t *f;

	if ( n < 0 || n >= MAX_CLIENTS || !self->client || !G_VRForceOn() ) {
		return NULL;
	}
	f = &vrForce[n];
	if ( !f->valid || level.time - f->time > VR_FORCE_FRESH || level.time - f->time < 0 ) {
		return NULL;
	}
	return f;
}

// Where the Force comes from: their hand, from where they are now - or, the
// way from their eyes to it blocked (a wall, a door, someone), just short of
// that.
static void VRF_From( gentity_t *self, const vrForceHand_t *f, vec3_t from ) {
	vec3_t eye, hand, back;
	trace_t tr;

	VectorCopy( self->client->ps.origin, eye );
	eye[2] += self->client->ps.viewheight;
	VectorAdd( self->client->ps.origin, f->ofs, hand );
	trap->Trace( &tr, eye, NULL, NULL, hand, self->s.number, MASK_SHOT, qfalse, 0, 0 );
	if ( tr.startsolid || tr.allsolid ) {
		VectorCopy( eye, from );
	} else if ( tr.fraction < 1.0f ) {
		VectorSubtract( eye, tr.endpos, back );
		VectorNormalize( back );
		VectorMA( tr.endpos, 1.0f, back, from );
	} else {
		VectorCopy( hand, from );
	}
}

// Someone the hand's line goes within VR_FORCE_SNAP degrees of - alive, a body
// in the world, within range of it and in sight of it, not on your side (Push
// and Pull: never, friendly fire or not - ForceThrow's), the power working on
// them (ForcePowerUsableOn; Grip: no one it can't catch now, nor further from
// you than a held Grip holds; Drain: some Force in them) - the one nearest that
// line: the way to their middle. Of them, someone hidden from you (cloaked, or
// tricking you - your Seeing not going: it shows both) is never snapped onto,
// nor off: nearest the line, the line stays as it is. (The line as it is:
// nobody.)
static gentity_t *VRF_Snap( gentity_t *self, int power, float range, const vec3_t from, vec3_t dir ) {
	const float least = cosf( DEG2RAD( VR_FORCE_SNAP ) );
	const qboolean seeing = (qboolean)( ( self->client->ps.fd.forcePowersActive & ( 1 << FP_SEE ) ) != 0 );
	int list[MAX_GENTITIES], n, i;
	vec3_t mins, maxs, best;
	float bestDot = least;
	gentity_t *pick = NULL;

	for ( i = 0; i < 3; i++ ) {
		mins[i] = from[i] - range;
		maxs[i] = from[i] + range;
	}
	n = trap->EntitiesInBox( mins, maxs, list, MAX_GENTITIES );
	for ( i = 0; i < n; i++ ) {
		gentity_t *ent = &g_entities[list[i]];
		vec3_t mid, to;
		trace_t tr;
		float d, dot;
		qboolean hidden;

		if ( ent == self || !ent->inuse || !ent->client || ent->health <= 0 || !( ent->r.contents & CONTENTS_BODY ) ) {
			continue;
		}
		if ( ( !g_friendlyFire.integer || power == FP_PUSH || power == FP_PULL ) && OnSameTeam( self, ent ) ) {
			continue;
		}
		if ( power == FP_GRIP && ( ( ent->client->ps.fd.forcePowersActive & ( 1 << FP_ABSORB ) ) || ent->client->ps.fd.forceGripCripple ||
			ent->client->ps.fd.forceGripBeingGripped >= level.time || DistanceSquared( ent->client->ps.origin, self->client->ps.origin ) > range * range ) ) {
			continue;	// (absorbing it - asked here: ForcePowerUsableOn sounds that - still choking, gripped, or out of a held Grip's reach)
		}
		if ( ( power == FP_DRAIN && ent->client->ps.fd.forcePower <= 0 ) || !ForcePowerUsableOn( self, ent, (forcePowers_t)power ) ) {
			continue;	// (it won't work on them: down, your companion, knocked down, in a duel, ysalamiri... - no Force to drain)
		}
		hidden = (qboolean)( !seeing && ( ent->client->ps.powerups[PW_CLOAKED] || G_IsMindTricked( &ent->client->ps.fd, self->s.number ) ) );
		VectorAdd( ent->r.absmin, ent->r.absmax, mid );
		VectorScale( mid, 0.5f, mid );
		VectorSubtract( mid, from, to );
		d = VectorNormalize( to );
		if ( d < 1.0f || d > range ) {
			continue;
		}
		dot = DotProduct( to, dir );
		if ( dot <= bestDot ) {
			continue;
		}
		trap->Trace( &tr, from, NULL, NULL, mid, self->s.number, MASK_SHOT, qfalse, 0, 0 );
		if ( tr.fraction < 1.0f && tr.entityNum != ent->s.number ) {
			continue;	// (out of sight of the hand)
		}
		bestDot = dot;
		VectorCopy( to, best );
		pick = hidden ? NULL : ent;	// (hidden: the line as it is - neither bent onto them, nor off them)
	}
	if ( pick ) {
		VectorCopy( best, dir );
	}
	return pick;
}

static const char *VRF_PowerName( int power ) {
	switch ( power ) {
	case FP_PUSH:		return "Push";
	case FP_PULL:		return "Pull";
	case FP_GRIP:		return "Grip";
	case FP_LIGHTNING:	return "Lightning";
	case FP_DRAIN:		return "Drain";
	default:			return "the Force";
	}
}

// Where a power of a VR player's goes from - their Force hand - and which way
// (w_force.c): Push and Pull the way a gesture thrust them, else (and the rest)
// where it points, snapped onto someone within VR_FORCE_SNAP degrees of that,
// in range of it. qfalse: no Force hand - start and dir as they were (the
// game's own: their eyes, where they look).
qboolean G_VRForceAim( gentity_t *self, int power, float range, vec3_t start, vec3_t dir ) {
	vrForceHand_t *f = VRF_Hand( self );
	const gentity_t *onto;
	vec3_t from, way;
	qboolean thrust;

	if ( !f ) {
		return qfalse;
	}
	VRF_From( self, f, from );
	if ( ( power == FP_PUSH || power == FP_PULL ) && ( self->client->pers.cmd.buttons & BUTTON_FORCEGRIP ) ) {
		// (a Push or a Pull going - nobody gripped: ForceThrow returns while
		// someone is - with the Grip button held, the gestures' Grip held on
		// through a thrust or a yank: that Grip's spent, G_VRForceGripSpent)
		vrGripSpent[self->s.number] = qtrue;	// (VRF_Hand: a client's)
	}
	thrust = (qboolean)( ( power == FP_PUSH || power == FP_PULL ) && f->thrown );
	VectorCopy( thrust ? f->thrust : f->dir, way );
	onto = VRF_Snap( self, power, range, from, way );
	if ( g_vrSaberLog.integer ) {
		const int who = onto ? onto->s.number : -1, since = level.time - f->logTime;
		const qboolean held = (qboolean)( power == FP_LIGHTNING || power == FP_DRAIN );	// (Grip's aimed till it catches someone: said at once)
		if ( since >= VR_FORCE_LOG_MS || ( ( power != f->logPower || who != f->logOnto ) && ( !held || since >= VR_FORCE_LOG_HELD_MS ) ) ) {
			G_LogPrintf( "VRForce: %i's %s from the %s hand, %s%s\n", self->s.number, VRF_PowerName( power ),
				f->hand == VR_HAND_RIGHT ? "right" : "left", thrust ? "the way it thrust" : "where it points",
				onto ? va( " - onto %i", onto->s.number ) : "" );
			f->logPower = power;
			f->logOnto = who;
			f->logTime = level.time;
		}
	}
	VectorCopy( from, start );
	VectorCopy( way, dir );
	return qtrue;
}

// Whether a VR player's Grip catches no one now (ForceGrip): a Push or a Pull
// went while they held its button, nobody caught - a Grip aimed at nobody is
// over with a thrust or a yank (the gestures hold its button on), till they let
// go of it, their Force hand there or not. (Only ever a VR player's: their Force
// hand's Push or Pull said so.)
qboolean G_VRForceGripSpent( gentity_t *self ) {
	const int n = self ? self->s.number : -1;

	return (qboolean)( n >= 0 && n < MAX_CLIENTS && vrGripSpent[n] );
}

// A held Grip's hand (DoGripAction): from where, pointing where - as it is,
// no snapping. qfalse: no Force hand (start and dir as they were).
qboolean G_VRForceHand( gentity_t *self, vec3_t start, vec3_t dir ) {
	vrForceHand_t *f = VRF_Hand( self );

	if ( !f ) {
		return qfalse;
	}
	VRF_From( self, f, start );
	VectorCopy( f->dir, dir );
	return qtrue;
}
