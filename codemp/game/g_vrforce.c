/*
===========================================================================
OpenJK VR (multiplayer) - the Force hand, on the server.

A VR player with Force gestures on (vr_forceGestures - the engine's,
qcommon/vr_force.h) says with every command which hand their Force comes
from and where it points (their avatar: vrAvatar_t.forceValid, forceHand,
forceDir), and with a gesture's Push or Pull, the way it went - with its
Mind Trick, the way its wave's aimed (vrUserPose_t.thrust). With g_vrForce
on, their Push, Pull, Grip, Lightning, Drain and a level 1 Mind Trick come
from that hand, however they're used - a gesture, the Force grip's quick
squeeze, a key:

  Push, Pull         the way the hand thrust it (a gesture's), else where
                     it points
  Mind Trick         level 1's: whoever the hand waved across (a gesture's
                     wave: level from their eyes toward the hand halfway
                     across, VR_FORCE_SNAP_WAVE degrees either side of that),
                     else where it points (levels 2 and 3, the game's own:
                     all round where they look)
  Grip               whom it catches, where it points - from the hand,
                     VR_FORCE_GRIP_REACH (qcommon/vr_public.h: a quarter
                     further than the game's own), nobody further from them
                     than that (nor anyone once a Push or a Pull has gone
                     with its button held, nobody caught, till it's let
                     go); then held as long as the game holds a Grip
                     (DoGripAction: level 1 five seconds, 2 and 3 four), as
                     far as that, below level 3 only while it points within
                     the game's 25 degrees of them, side to side (InFront:
                     up and down don't count) - level 3 floats them along
                     where it points
  Lightning, Drain   where it points

each snapped onto someone within VR_FORCE_SNAP degrees of that line (Grip,
VR_FORCE_SNAP_GRIP; Mind Trick's wave, VR_FORCE_SNAP_WAVE) - anywhere on
them, their body a line up through the middle of their box
(VR_ForceBodyPoint) - the one nearest it, alive, in the power's reach and in
sight of the hand (Grip's and Mind Trick's, as their own traces see: no
player clip between), not on your side, the power working on them
(ForcePowerUsableOn: not down, not your companion, not in a duel...; Drain,
some Force in them) - but never onto, nor off, someone hidden from you
(cloaked, or mind-tricking you, while your Seeing isn't going): nearest the
line, it stays as it is (as the game's own aim would have it, hitting them
if it's on them). Their game picks whom it's aimed at (cg_vrforce.c: along
where the controller points, or where their hand is as they see it) and
points it straight at them; it draws it so too (CG_VR_ForceSnap). The rest
of each power is the game's own (w_force.c): its levels, reach and cones,
who it works on, what it costs. The hand is where the player's avatar has it
(within reach of their eyes: g_vrsaber.c), from where they are now; a hand
reaching into a wall, or into someone, gives way there. Their commands
coming without an avatar, it's gone at once; not coming, VR_FORCE_FRESH on.

Everyone else sees the Force come from that hand too: the avatar the
server passes on keeps it (cg_vrforce.c draws it). g_vrForce 0: the game's
own aim, where they look - and no Force hand passed on. "vrForce" in the
serverinfo says which (VR_FORCE_SERVED, or 0).

A Grip from the hand at level 2 or 3 moves whom it holds with the hand, too
(g_vrGripMove, and the player's own Grip Moves - cg_vrGripMove, in their
userinfo: both on, as they come), as if on a long stick from where the head
turns (the neck), swung by the hand: as the hand's way from its shoulder
swings round and rises, theirs does as much - sweep the hand and they swing
round, raise it and they rise, turn with the stick (snap or smooth) and they
come round with you - and, at level 3, pushed toward them or pulled back
(more along their way from the neck than across, over the last few
millimetres it's moved: a pull to the chest, and a push out from it, too),
they go VR_GRIP_IN_OUT as much further or nearer (about 2 to 8 metres in a
third of a metre), not swung; level 2 only lifts and lowers them, held where
they were caught side to side; never further from you than the Grip reaches
(or than they were caught). The hand's moves are judged moment by moment,
from the neck as it was and the shoulder as it is, as the player's room has
them, so their own sway - leaning, crouching, stepping, a turn of the head,
the body turning to follow it - moves no one (the hand gone a while and back:
from where it is). The server steers them toward where the
hand puts them (their velocity, VR_GRIP_FOLLOW - no faster than
VR_GRIP_SPEED), walls and floors stopping them as ever (PM_FLOAT: their own
slide); let go, they fly on as the hand has been moving them - a fling throws
them, one out to a stop too, tossed up off the ground as a Push lifts whom it
shoves along it; let go of still, they drop; carried along with you, or you
flung off from them, isn't a throw (G_VRForceGripDone). The neck: behind and
below the headset (their avatar's head - as far behind it as their
cg_vrBodyBack says - as it's turned); the shoulder below it, to the hand's
side of the way their body faces (the avatar's bodyYaw). Level 1, a ring of
Grips on each other, and the rest of any Grip - how long it holds, how far,
whom it lets go of, what it costs and does to them - the game's own
(DoGripAction: while the hand moves them, from the hand as it last was, gone
a while).

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "g_local.h"

#define VR_FORCE_FRESH		250		// ms: a Force hand not heard of for this long is gone
#define VR_FORCE_SNAP_WAVE	20.0f	// degrees: someone this near the line a Mind Trick's wave's aimed along is whom it waved
									// across (its 20 cm and more: 20 degrees or more side to side, even at arm's length)
#define VR_FORCE_LOG_MS		1000	// (the log: a power aimed at the same one again within this isn't said again...
#define VR_FORCE_LOG_HELD_MS	250		// ...nor a held one - Lightning, Drain - aimed anew within this: its hand wavering)

// The Grip that moves with the hand (G_VRForceGripMove)
#define VR_GRIP_NECK_FWD		2.7f	// units: the headset this far ahead of where the head turns (8 cm)...
#define VR_GRIP_NECK_UP			2.55f	// ...and this far above it (7.5 cm)
#define VR_GRIP_HEAD_BACK_MOST	24.0f	// their avatar's head behind the headset, the way the body faces: as far as their cg_vrBodyBack
										// says (VR_POSE_HEAD_BACK, not said), no further either way than this (VRS_TakeAvatar's head)
#define VR_GRIP_SHOULDER_DOWN	4.9f	// the hand's shoulder: this far below where the head turns (the gestures' 22 cm below the eyes)...
#define VR_GRIP_SHOULDER_SIDE	5.8f	// ...and this far to its side (17 cm: the gestures' VRF_SHOULDER_SIDE)
#define VR_GRIP_UPRIGHT			0.25f	// the hand this near straight up or down from its shoulder (the way's level part this much of it,
										// or less): which way round it is isn't read
#define VR_GRIP_IN_OUT			20.0f	// level 3: they go this much nearer or further for each unit the hand goes toward them or back...
#define VR_GRIP_ALONG			1.0f	// ...its move more that way - along the stick they're on, their way from the neck - than across,
										// this many times (a sweep, a turn, isn't) - then not swung...
#define VR_GRIP_TELL			0.15f	// ...told by its way over this much of it (4 mm: a few of the pose's steps - vr_net.h's 1/32),
										// each frame's as the last told
#define VR_GRIP_NEAR			68.0f	// ...no nearer the neck than this (2 m; or as near as they were caught)...
#define VR_GRIP_FAR				280.0f	// ...nor further (8 m; caught further, drawn in to it)...
#define VR_GRIP_KEEP			20.0f	// ...and never further from where you stand than the Grip's reach (VR_FORCE_GRIP_REACH) less this,
										// or than they were caught - DoGripAction letting go of them only this much further (as far as
										// they lag behind you going off from them at 200 a second: G_VRForceGripReach)
#define VR_GRIP_PITCH			70.0f	// degrees: they're no higher or lower from the neck than this (or than they were caught)
#define VR_GRIP_FOLLOW			10.0f	// a second: they're steered toward where the hand puts them at this much of the way...
#define VR_GRIP_SPEED			700.0f	// ...no faster than this (the game's own level 3 floats them as fast)
#define VR_GRIP_HAND_MS			60		// ms: how fast the hand's moving them, judged over about this long lately (smoothed: the stops aside)
#define VR_GRIP_TOSS			128.0f	// thrown (VR_FORCE_GRIP_THROWN) along the ground, not steeply down: going up at least this
										// fast (as a Push lifts whom it shoves along it - ForceThrow)

typedef struct {
	qboolean	valid;
	int			time;		// level.time it came
	int			hand;		// which (VR_HAND_*)
	vec3_t		ofs;		// where it is, from the player's origin
	vec3_t		dir;		// where it points (unit)
	qboolean	thrown;		// this command's Push or Pull came from a gesture: the way it went (its Mind Trick: the way
							// its wave's aimed)...
	vec3_t		thrust;		// ...(unit)
	int			logPower, logOnto, logTime;	// (what the log said last, when)
	vec3_t		head;		// their avatar's head (from the player's origin), how it's turned...
	vec3_t		headAngles;
	float		bodyYaw;	// ...where their body faces...
	float		trackYaw;	// ...and how far their turning (snap or smooth) has turned their room (the pose's: world yaw
							// minus their room's)
} vrForceHand_t;

// Someone a Grip moves with the hand (G_VRForceGripMove).
typedef struct {
	int			victim;		// whom (ENTITYNUM_NONE: nobody)...
	int			started;	// ...their forceGripStarted as it took them (another Grip of them: taken anew)...
	int			level;		// ...at what level, with which hand (another: taken anew)
	int			hand;
	int			time;		// level.time it took them
	int			through;	// the gripper's EF_TELEPORT_BIT then (another - gone through a teleporter: taken anew)
	vec3_t		handWas;	// the hand when it last moved them, from the neck then (as their room has it: VRF_Room)...
	vec3_t		yawWas;		// ...and the last time which way round it is was read (or as it took them)...
	qboolean	lost;		// ...not known last frame (back: from where it is - none of what it did meanwhile)...
	qboolean	pushing;	// ...pushing them out or pulling them in, as last told (VR_GRIP_TELL)...
	vec3_t		moved;		// ...and how it's moved since (as their room has it)
	vec3_t		ofs, dir;	// the hand as it last was, from where they stand, and where it pointed (DoGripAction's while it's
							// gone: G_VRForceHand)
	float		yaw, pitch, dist;	// their way from the neck (which way round: as their room has it) - moved as the hand moves
	float		steep, least;		// no steeper than this, nor nearer (VR_GRIP_PITCH, VR_GRIP_NEAR; or as they were caught)...
	float		reach;				// ...nor further from where you stand (VRF_GripReach; DoGripAction's own reach less VR_GRIP_KEEP)
	vec3_t		caught;		// where they were (level 2: held there, side to side)
	vec3_t		at;			// where the hand puts them, as last worked out...
	vec3_t		atWas;		// ...the command before (atTime: as of that command)...
	int			atTime;
	vec3_t		atVel;		// ...how fast it's been moving them lately (VR_GRIP_HAND_MS; the stops aside: as it would)...
	vec3_t		atOver;		// ...of it, how fast past the stops (where it would put them, beyond where it does)
	float		most;		// how far from where they were caught they've been
	qboolean	steered;	// it steered them last (no Push on them going)...
	int			teleport;	// ...their EF_TELEPORT_BIT then
} vrGripMove_t;

static vrForceHand_t	vrForce[MAX_CLIENTS];
static qboolean			vrGripSpent[MAX_CLIENTS];	// a Push or a Pull went while their Grip button was held, nobody caught: no
													// catch till it's let go (their Force hand gone a moment or not)
static vrGripMove_t		vrGrip[MAX_CLIENTS];
static qboolean			vrGripOff[MAX_CLIENTS];		// their Grip Moves off (cg_vrGripMove 0 in their userinfo)
static float			vrHeadBack[MAX_CLIENTS];	// how far behind their headset their avatar's head is (cg_vrBodyBack in their
													// userinfo: G_VRForceUserinfo)

void G_VRForceInit( void ) {
	int n;

	memset( vrForce, 0, sizeof( vrForce ) );
	memset( vrGripSpent, 0, sizeof( vrGripSpent ) );
	memset( vrGrip, 0, sizeof( vrGrip ) );
	memset( vrGripOff, 0, sizeof( vrGripOff ) );
	for ( n = 0; n < MAX_CLIENTS; n++ ) {
		vrGrip[n].victim = ENTITYNUM_NONE;
		vrHeadBack[n] = VR_POSE_HEAD_BACK;
	}
	trap->Cvar_Register( NULL, "vrForce", "0", CVAR_SERVERINFO | CVAR_ROM );
	G_VRForcePublish();
}

// Whether VR players' Force powers come from their Force hand here.
qboolean G_VRForceOn( void ) {
	return (qboolean)( G_VRPosesAvailable() && g_vrForce.integer > 0 );
}

// "vrForce" in the serverinfo: VR players' Force comes from their hand here,
// as this build aims it (VR_FORCE_SERVED - their game draws it so, and picks
// whom it's aimed at as this does); 0, not.
void G_VRForcePublish( void ) {
	trap->Cvar_Set( "vrForce", G_VRForceOn() ? va( "%i", VR_FORCE_SERVED ) : "0" );
}

// A player's slot taken or left: nothing of theirs kept.
void G_VRForceClientReset( int clientNum ) {
	if ( clientNum >= 0 && clientNum < MAX_CLIENTS ) {
		memset( &vrForce[clientNum], 0, sizeof( vrForce[clientNum] ) );
		vrGripSpent[clientNum] = qfalse;
		memset( &vrGrip[clientNum], 0, sizeof( vrGrip[clientNum] ) );
		vrGrip[clientNum].victim = ENTITYNUM_NONE;
		vrGripOff[clientNum] = qfalse;
		vrHeadBack[clientNum] = VR_POSE_HEAD_BACK;
	}
}

// ClientUserinfoChanged: whether their Grip moves whom it holds with their
// hand, as they'd have it (cg_vrGripMove: nothing said - a game from before
// it - on); and how far behind their headset their avatar's head is
// (cg_vrBodyBack: nothing said, VR_POSE_HEAD_BACK - its default - as for NaN;
// within VR_GRIP_HEAD_BACK_MOST either way).
void G_VRForceUserinfo( gentity_t *ent, const char *userinfo ) {
	const int n = ent ? ent->s.number : -1;
	const char *s;
	float back;

	if ( n >= 0 && n < MAX_CLIENTS && userinfo ) {
		s = Info_ValueForKey( userinfo, "cg_vrGripMove" );
		vrGripOff[n] = (qboolean)( s[0] && !atoi( s ) );
		s = Info_ValueForKey( userinfo, "cg_vrBodyBack" );
		back = s[0] ? atof( s ) : VR_POSE_HEAD_BACK;
		vrHeadBack[n] = ( back == back ) ? Com_Clamp( -VR_GRIP_HEAD_BACK_MOST, VR_GRIP_HEAD_BACK_MOST, back ) : VR_POSE_HEAD_BACK;
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
		( a->hand[h].grip != VR_GRIP_FREE && a->hand[h].grip != VR_GRIP_SABER ) || !VRF_UnitDir( a->forceDir, dir ) ) {
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
	VectorCopy( a->head, f->head );	// (where their neck and the hand's shoulder are: G_VRForceGripMove)
	VectorCopy( a->headAngles, f->headAngles );
	f->bodyYaw = a->bodyYaw;
	f->trackYaw = ( pose && fabsf( pose->trackYaw ) < 1e6f ) ? pose->trackYaw : 0.0f;
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

// Where the Force comes from: their hand (ofs: from where they stand), from
// where they are now - or, the way from their eyes to it blocked (a wall, a
// door, someone), just short of that.
static void VRF_From( gentity_t *self, const vec3_t ofs, vec3_t from ) {
	vec3_t eye, hand, back;
	trace_t tr;

	VectorCopy( self->client->ps.origin, eye );
	eye[2] += self->client->ps.viewheight;
	VectorAdd( self->client->ps.origin, ofs, hand );
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

// Whether a Grip holds ent from where self stands - DoGripAction lets go at
// once of one it doesn't: where they stand in sight of where self does
// (MASK_PLAYERSOLID: player clips, bodies - but their own).
qboolean G_VRForceGripHolds( gentity_t *self, gentity_t *ent ) {
	trace_t tr;

	trap->Trace( &tr, self->client->ps.origin, NULL, NULL, ent->client->ps.origin, self->s.number, MASK_PLAYERSOLID, qfalse, 0, 0 );
	return (qboolean)( tr.fraction >= 1.0f || tr.entityNum == ent->s.number );
}

// Where on someone a power aimed from 'from' along dir goes: the point of their
// body - the line up through the middle of their box (VR_ForceBodySpan) -
// nearest the aim's line.
static void VRF_BodyPoint( const gentity_t *ent, const vec3_t from, const vec3_t dir, vec3_t point ) {
	float bottom, top;

	VR_ForceBodySpan( ent->r.currentOrigin[2], ent->r.mins[2], ent->r.maxs[2], &bottom, &top );
	VR_ForceBodyPoint( from, dir, ent->r.currentOrigin[0] + ( ent->r.mins[0] + ent->r.maxs[0] ) * 0.5f,
		ent->r.currentOrigin[1] + ( ent->r.mins[1] + ent->r.maxs[1] ) * 0.5f, bottom, top, point );
}

// Someone the hand's line goes within snap degrees of, anywhere on them
// (VRF_BodyPoint) - alive, a body in the world, within range of it and in sight
// of it (Grip and Mind Trick: as their own traces see, MASK_PLAYERSOLID -
// player clips in the way, corpses not - Grip only if a Grip along it would
// hold them, G_VRForceGripHolds and DoGripAction's InFront), not on your side
// (Push and Pull: never, friendly fire or not - ForceThrow's), the power
// working on them (ForcePowerUsableOn; Grip: no one it can't catch now, nor
// further from you than it reaches; Drain: some Force in them; Push and Pull:
// none ForceThrow passes over) - the one nearest that line: the way to that
// point of them. Of them, someone hidden from you (cloaked, or - you a player -
// tricking you; your Seeing not going: it shows both) is never snapped onto,
// nor off: nearest the line, the line stays as it is. (The line as it is:
// nobody.) Anyone's: a VR player's hand, or a level 1 Push or Pull
// (G_ForceThrowNearest) - an NPC's too.
static gentity_t *VRF_Snap( gentity_t *self, int power, float range, float snap, const vec3_t from, vec3_t dir ) {
	const float least = cosf( DEG2RAD( snap ) );
	const qboolean seeing = (qboolean)( ( self->client->ps.fd.forcePowersActive & ( 1 << FP_SEE ) ) != 0 );
	int list[MAX_GENTITIES], n, i;
	vec3_t mins, maxs, best, lineAngles;
	float bestDot = least;
	gentity_t *pick = NULL;

	for ( i = 0; i < 3; i++ ) {
		mins[i] = from[i] - range;
		maxs[i] = from[i] + range;
	}
	vectoangles( dir, lineAngles );
	n = trap->EntitiesInBox( mins, maxs, list, MAX_GENTITIES );
	for ( i = 0; i < n; i++ ) {
		gentity_t *ent = &g_entities[list[i]];
		vec3_t at, to;
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
			continue;	// (absorbing it - asked here: ForcePowerUsableOn sounds that - still choking, gripped, or out of the Grip's reach)
		}
		if ( ( power == FP_DRAIN && ent->client->ps.fd.forcePower <= 0 ) || !ForcePowerUsableOn( self, ent, (forcePowers_t)power ) ) {
			continue;	// (it won't work on them: down, your companion, knocked down, in a duel, ysalamiri... - no Force to drain)
		}
		if ( ( power == FP_PUSH || power == FP_PULL ) && ( ( ent->s.eFlags & EF_NODRAW ) || ent->client->NPC_class == CLASS_GALAKMECH ||
			ent->client->NPC_class == CLASS_ATST || ent->client->NPC_class == CLASS_RANCOR ) ) {
			continue;	// (whom ForceThrow passes over: not drawn, Galak's mech, an AT-ST, a rancor)
		}
		hidden = (qboolean)( !seeing && ( ent->client->ps.powerups[PW_CLOAKED] ||
			( self->s.number < MAX_CLIENTS && G_IsMindTricked( &ent->client->ps.fd, self->s.number ) ) ) );	// (a player's tricked: an NPC's never)
		VRF_BodyPoint( ent, from, dir, at );
		VectorSubtract( at, from, to );
		d = VectorNormalize( to );
		if ( d < 1.0f || d > range ) {
			continue;
		}
		dot = DotProduct( to, dir );
		if ( dot <= bestDot ) {
			continue;
		}
		trap->Trace( &tr, from, NULL, NULL, at, self->s.number, ( power == FP_GRIP || power == FP_TELEPATHY ) ? MASK_PLAYERSOLID : MASK_SHOT,
			qfalse, 0, 0 );
		if ( tr.fraction < 1.0f && tr.entityNum != ent->s.number ) {
			continue;	// (out of sight of the hand - Grip's and Mind Trick's, as their own traces see: player clips in the way, not
						// corpses)
		}
		if ( power == FP_GRIP && ( !InFront( ent->client->ps.origin, (float *)from, lineAngles, VR_FORCE_GRIP_FRONT ) ||
			!G_VRForceGripHolds( self, ent ) ) ) {
			continue;	// (a Grip along the line wouldn't hold them: off to the side of it - steeply over or under - or where they stand
						// out of sight of where you do, behind a crate, say: DoGripAction lets go at once)
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

// Whom a level 1 Push or Pull goes at - anyone's, a VR hand's thrust or where
// they look - with nothing on its line itself (ForceThrow: its trace met
// nothing, or only the world): the one nearest that line within
// G_THROW1_NEAR degrees, as VRF_Snap picks them (anywhere on them, alive, in
// its reach and in sight, not on your side, the power working on them, never
// someone hidden from you) - dir becomes the way to them. NULL: nobody.
gentity_t *G_ForceThrowNearest( gentity_t *self, int power, float range, const vec3_t from, vec3_t dir ) {
	if ( !self || !self->client ) {
		return NULL;
	}
	return VRF_Snap( self, power, range, G_THROW1_NEAR, from, dir );
}

static const char *VRF_PowerName( int power ) {
	switch ( power ) {
	case FP_PUSH:		return "Push";
	case FP_PULL:		return "Pull";
	case FP_GRIP:		return "Grip";
	case FP_LIGHTNING:	return "Lightning";
	case FP_DRAIN:		return "Drain";
	case FP_TELEPATHY:	return "Mind Trick";
	default:			return "the Force";
	}
}

// Where a power of a VR player's goes from - their Force hand - and which way
// (w_force.c): Push and Pull the way a gesture thrust them, Mind Trick the way
// its wave's aimed, else (and the rest) where it points, snapped onto someone
// within VR_FORCE_SNAP degrees of that (Grip: VR_FORCE_SNAP_GRIP; a wave:
// VR_FORCE_SNAP_WAVE), in range of it. qfalse: no Force hand - start and dir as
// they were (the game's own: their eyes, where they look).
qboolean G_VRForceAim( gentity_t *self, int power, float range, vec3_t start, vec3_t dir ) {
	vrForceHand_t *f = VRF_Hand( self );
	const gentity_t *onto;
	vec3_t from, way;
	qboolean thrust;

	if ( !f ) {
		return qfalse;
	}
	VRF_From( self, f->ofs, from );
	if ( ( power == FP_PUSH || power == FP_PULL ) && ( self->client->pers.cmd.buttons & BUTTON_FORCEGRIP ) ) {
		// (a Push or a Pull going - nobody gripped: ForceThrow returns while
		// someone is - with the Grip button held, the gestures' Grip held on
		// through a thrust or a yank: that Grip's spent, G_VRForceGripSpent)
		vrGripSpent[self->s.number] = qtrue;	// (VRF_Hand: a client's)
	}
	thrust = (qboolean)( ( power == FP_PUSH || power == FP_PULL || power == FP_TELEPATHY ) && f->thrown );
	VectorCopy( thrust ? f->thrust : f->dir, way );
	onto = VRF_Snap( self, power, range, ( power == FP_GRIP ) ? VR_FORCE_SNAP_GRIP : ( thrust && power == FP_TELEPATHY ) ? VR_FORCE_SNAP_WAVE :
		VR_FORCE_SNAP, from, way );
	if ( g_vrSaberLog.integer ) {
		const int who = onto ? onto->s.number : -1, since = level.time - f->logTime;
		const qboolean held = (qboolean)( power == FP_LIGHTNING || power == FP_DRAIN );	// (Grip's aimed till it catches someone: said at once)
		if ( since >= VR_FORCE_LOG_MS || ( ( power != f->logPower || who != f->logOnto ) && ( !held || since >= VR_FORCE_LOG_HELD_MS ) ) ) {
			G_LogPrintf( "VRForce: %i's %s from the %s hand, %s%s\n", self->s.number, VRF_PowerName( power ),
				f->hand == VR_HAND_RIGHT ? "right" : "left", !thrust ? "where it points" : ( power == FP_TELEPATHY ) ? "the way it waved" :
				"the way it thrust",
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

// A held Grip's hand (DoGripAction), on gripEnt: from where, pointing where -
// as it is, no snapping; gone a while it moves them with the hand (held where
// it last put them: G_VRForceGripMove), as it last was there - so as far as it
// reaches, and where it pointed, hold them still. qfalse: no Force hand (start
// and dir as they were).
qboolean G_VRForceHand( gentity_t *self, gentity_t *gripEnt, vec3_t start, vec3_t dir ) {
	vrForceHand_t *f = VRF_Hand( self );
	const vrGripMove_t *g;

	if ( f ) {
		VRF_From( self, f->ofs, start );
		VectorCopy( f->dir, dir );
		return qtrue;
	}
	if ( !self || self->s.number < 0 || self->s.number >= MAX_CLIENTS || !self->client || !G_VRForceOn() || !gripEnt ||
		!gripEnt->client ) {
		return qfalse;
	}
	g = &vrGrip[self->s.number];
	if ( g->victim != gripEnt->s.number || g->started != gripEnt->client->ps.fd.forceGripStarted ) {
		return qfalse;
	}
	VRF_From( self, g->ofs, start );
	VectorCopy( g->dir, dir );
	return qtrue;
}

// How far a held Grip from the hand reaches, on gripEnt (DoGripAction lets go
// of them further from where the gripper stands): VR_FORCE_GRIP_REACH - moved
// with the hand, as far as it holds them (as caught, if further than
// VRF_GripReach's reach less VR_GRIP_KEEP) and VR_GRIP_KEEP more: room still
// for them to lag behind where the hand puts them.
float G_VRForceGripReach( gentity_t *self, gentity_t *gripEnt ) {
	const vrGripMove_t *g;

	if ( !self || self->s.number < 0 || self->s.number >= MAX_CLIENTS || !gripEnt || !gripEnt->client ) {
		return VR_FORCE_GRIP_REACH;
	}
	g = &vrGrip[self->s.number];
	if ( g->victim != gripEnt->s.number || g->started != gripEnt->client->ps.fd.forceGripStarted ) {
		return VR_FORCE_GRIP_REACH;
	}
	return Q_max( VR_FORCE_GRIP_REACH, g->reach + VR_GRIP_KEEP );
}

/*
--------------------------------------------------------------------------------
The Grip that moves with the hand
--------------------------------------------------------------------------------
*/

// Where a VR player's head turns - their neck - and their Force hand's
// shoulder are, as their avatar has them, from where they are now (world):
// the neck behind the headset (the avatar's head is behind it, the way the
// body faces, as far as they say: vrHeadBack) and below it, as the head's
// turned; the shoulder below that, to the hand's side of the way the body
// faces. qfalse: nowhere (their head said as no number).
static qboolean VRF_GripBody( const gentity_t *self, const vrForceHand_t *f, vec3_t neck, vec3_t shoulder ) {
	const float by = DEG2RAD( f->bodyYaw ), headBack = vrHeadBack[self->s.number];
	const float side = ( f->hand == VR_HAND_RIGHT ) ? -VR_GRIP_SHOULDER_SIDE : VR_GRIP_SHOULDER_SIDE;
	vec3_t fwd, up;

	AngleVectors( f->headAngles, fwd, NULL, up );
	VectorAdd( self->client->ps.origin, f->head, neck );
	neck[0] += cosf( by ) * headBack;
	neck[1] += sinf( by ) * headBack;
	VectorMA( neck, -VR_GRIP_NECK_FWD, fwd, neck );
	VectorMA( neck, -VR_GRIP_NECK_UP, up, neck );
	shoulder[0] = neck[0] - sinf( by ) * side;
	shoulder[1] = neck[1] + cosf( by ) * side;
	shoulder[2] = neck[2] - VR_GRIP_SHOULDER_DOWN;
	return (qboolean)( Distance( neck, self->client->ps.origin ) < 1e6f && Distance( shoulder, neck ) < 1e6f );
}

// The way from 'from' to 'to' (world) as a VR player's room has it: turned back
// as far as their turning has turned their room (trackYaw) - their turning,
// snap or smooth, turns all of them about their camera, the hand with it: as
// their room has them, nothing's moved.
static void VRF_Room( float trackYaw, const vec3_t from, const vec3_t to, vec3_t room ) {
	const float c = cosf( DEG2RAD( trackYaw ) ), s = sinf( DEG2RAD( trackYaw ) );
	vec3_t v;

	VectorSubtract( to, from, v );
	room[0] = c * v[0] + s * v[1];
	room[1] = c * v[1] - s * v[0];
	room[2] = v[2];
}

// Which way the hand (at) is from its shoulder (from), the two from the neck:
// how far (units), how steeply up and which way round (degrees) - that read
// (yawRead) unless it's upright (VR_GRIP_UPRIGHT). qfalse: no way to it (within
// a unit of it; or not a number).
static qboolean VRF_GripWay( const vec3_t from, const vec3_t at, float *out, float *pitch, float *yaw, qboolean *yawRead ) {
	vec3_t way;
	float flat;

	VectorSubtract( at, from, way );
	*out = VectorLength( way );
	if ( !( *out >= 1.0f && *out < 1e6f ) ) {
		return qfalse;
	}
	flat = sqrtf( way[0] * way[0] + way[1] * way[1] );
	*pitch = RAD2DEG( atan2f( way[2], flat ) );
	*yaw = RAD2DEG( atan2f( way[1], way[0] ) );
	*yawRead = (qboolean)( flat > VR_GRIP_UPRIGHT * *out );
	return qtrue;
}

// A point dist from the neck, rising pitch and turned yaw (world, degrees) -
// level 2: where they were caught side to side (caught), only lifted or
// lowered.
static void VRF_GripAt( const vec3_t neck, float dist, float pitch, float yaw, int gripLevel, const vec3_t caught, vec3_t at ) {
	if ( gripLevel >= FORCE_LEVEL_3 ) {
		const float c = cosf( DEG2RAD( pitch ) );
		at[0] = neck[0] + dist * c * cosf( DEG2RAD( yaw ) );
		at[1] = neck[1] + dist * c * sinf( DEG2RAD( yaw ) );
	} else {
		at[0] = caught[0];
		at[1] = caught[1];
	}
	at[2] = neck[2] + dist * sinf( DEG2RAD( pitch ) );
}

// The stick a Grip holds someone on (g: from the neck, turned yaw as the room
// has it, turnedBy the room's turn), kept within g->reach of where the gripper
// stands - its reach less VR_GRIP_KEEP, or as far as they were caught, if
// further (no hop in as they're caught; DoGripAction lets go of them
// VR_GRIP_KEEP further: G_VRForceGripReach) - a stop like the others, so what
// the hand does past it is what it was moving them (G_VRForceGripDone): at
// level 3 no longer than that the way it points; level 2 (held where they were
// caught, side to side) no higher or lower (caught at the edge of it, as high
// as the gripper stands: neither).
static void VRF_GripReach( const gentity_t *self, vrGripMove_t *g, const vec3_t neck, float turnedBy, int gripLevel ) {
	const float *o = self->client->ps.origin;
	vec3_t w;

	VectorSubtract( neck, o, w );
	if ( gripLevel >= FORCE_LEVEL_3 ) {
		const float c = cosf( DEG2RAD( g->pitch ) ), y = DEG2RAD( g->yaw + turnedBy );
		vec3_t u;
		float uw, room;
		VectorSet( u, c * cosf( y ), c * sinf( y ), sinf( DEG2RAD( g->pitch ) ) );
		uw = DotProduct( u, w );
		room = uw * uw - VectorLengthSquared( w ) + g->reach * g->reach;	// (|w + d u| = g->reach, the far root)
		if ( room > 0.0f ) {
			g->dist = Q_min( g->dist, -uw + sqrtf( room ) );
		}
	} else if ( g->dist >= 1.0f ) {
		const float hx = g->caught[0] - o[0], hy = g->caught[1] - o[1], room = g->reach * g->reach - ( hx * hx + hy * hy );
		if ( room > -1.0f ) {	// (caught at its edge, as high as you stand: no room up or down - rounding aside)
			const float up = sqrtf( Q_max( 0.0f, room ) );
			const float lo = Q_max( -1.0f, ( -up - w[2] ) / g->dist ), hi = Q_min( 1.0f, ( up - w[2] ) / g->dist );
			if ( lo <= hi ) {
				g->pitch = RAD2DEG( asinf( Com_Clamp( lo, hi, sinf( DEG2RAD( g->pitch ) ) ) ) );
			}
		}
	}
}

// Whether a Grip of n's on ent comes round to n again - ent, or whom ent's Grip
// holds, and so on, holding n in a Grip that moves whom it holds (level 2 or
// 3): each would chase where the other puts them. (Level 1 moves no one.)
static qboolean VRF_GripRing( int n, const gentity_t *ent ) {
	int i, next;

	for ( i = 0; i < MAX_CLIENTS && ent->inuse && ent->client; i++ ) {
		next = ent->client->ps.fd.forceGripEntityNum;
		if ( !( ent->client->ps.fd.forcePowersActive & ( 1 << FP_GRIP ) ) || ent->client->ps.fd.forcePowerLevel[FP_GRIP] < FORCE_LEVEL_2 ||
			next < 0 || next >= ENTITYNUM_MAX_NORMAL ) {
			return qfalse;
		}
		if ( next == n ) {
			return qtrue;
		}
		ent = &g_entities[next];
	}
	return qfalse;
}

// Whom a VR player's Grip moved with the hand (vrGrip[n]), not now: if it's
// ent, the game's own from here, from rest - a Push on them going, as it has
// them (taken anew after).
static void VRF_GripDrop( int n, gentity_t *ent ) {
	vrGripMove_t *g = &vrGrip[n];

	if ( g->victim == ent->s.number ) {
		if ( ent->client->pushEffectTime <= level.time ) {
			VectorClear( ent->client->ps.velocity );
		}
		memset( g, 0, sizeof( *g ) );
		g->victim = ENTITYNUM_NONE;
	}
}

// A VR player's Grip holding gripEnt at gripLevel (DoGripAction), moved with
// their hand: at level 2 or 3, the server's g_vrGripMove and their own
// cg_vrGripMove on, their Force hand there as it took them - their velocity set
// to steer them toward where the hand puts them (the hand gone a while since,
// where it last did). qfalse: not so (the game's own).
qboolean G_VRForceGripMove( gentity_t *self, gentity_t *gripEnt, int gripLevel ) {
	const int n = self ? self->s.number : -1;
	const vrForceHand_t *f;
	vrGripMove_t *g;
	vec3_t neck, shoulder, to, hand, want;
	float out1 = 0.0f, pitch1 = 0.0f, yaw1 = 0.0f, out0, pitch0, yaw0, speed, away, dist, pitch;
	qboolean read1 = qfalse, read0, body, known, rebased, taken = qfalse;
	int now;

	if ( n < 0 || n >= MAX_CLIENTS || !self->client || !gripEnt || !gripEnt->client ) {
		return qfalse;
	}
	g = &vrGrip[n];
	if ( g_vrGripMove.integer <= 0 || vrGripOff[n] || !G_VRForceOn() || gripLevel < FORCE_LEVEL_2 || VRF_GripRing( n, gripEnt ) ) {
		// off (the server's switch, or theirs; or the Force hand's, g_vrForce),
		// the Grip brought down to level 1, or a ring of Grips on each other (each
		// would chase where the other's hand puts them): the game's own
		VRF_GripDrop( n, gripEnt );
		return qfalse;
	}
	f = VRF_Hand( self );
	body = (qboolean)( f && VRF_GripBody( self, f, neck, shoulder ) );
	if ( body ) {
		// the hand and its shoulder from the neck, as their room has them: their
		// whole body moving - leaning, crouching, stepping, turning - moves no one
		VectorAdd( self->client->ps.origin, f->ofs, hand );
		VRF_Room( f->trackYaw, neck, hand, hand );
		VRF_Room( f->trackYaw, neck, shoulder, shoulder );
	}
	known = (qboolean)( body && VRF_GripWay( shoulder, hand, &out1, &pitch1, &yaw1, &read1 ) );
	now = self->client->pers.cmd.serverTime;

	// taken - a new Grip of them, at another level, from the other hand, or you
	// gone through a teleporter - as the hand is, them as they are (no hand: the
	// game's own, till there's one)
	if ( g->victim != gripEnt->s.number || g->started != gripEnt->client->ps.fd.forceGripStarted || g->level != gripLevel ||
		( f && g->hand != f->hand ) || ( self->client->ps.eFlags & EF_TELEPORT_BIT ) != g->through ) {
		if ( !known ) {
			VRF_GripDrop( n, gripEnt );
			return qfalse;
		}
		memset( g, 0, sizeof( *g ) );
		taken = qtrue;
		g->victim = gripEnt->s.number;
		g->started = gripEnt->client->ps.fd.forceGripStarted;
		g->level = gripLevel;
		g->hand = f->hand;
		g->time = level.time;
		g->through = self->client->ps.eFlags & EF_TELEPORT_BIT;
		VectorCopy( hand, g->handWas );
		VectorCopy( hand, g->yawWas );
		VectorSubtract( gripEnt->client->ps.origin, neck, to );
		g->dist = VectorLength( to );
		g->pitch = RAD2DEG( atan2f( to[2], sqrtf( to[0] * to[0] + to[1] * to[1] ) ) );
		g->yaw = AngleSubtract( RAD2DEG( atan2f( to[1], to[0] ) ), f->trackYaw );	// (as their room has it)
		g->steep = Q_max( VR_GRIP_PITCH, fabsf( g->pitch ) );
		g->least = Q_min( VR_GRIP_NEAR, g->dist );
		g->reach = Com_Clamp( VR_FORCE_GRIP_REACH - VR_GRIP_KEEP, VR_FORCE_GRIP_REACH, Distance( gripEnt->client->ps.origin, self->client->ps.origin ) );
		VectorCopy( gripEnt->client->ps.origin, g->caught );
		g->atTime = now;
		if ( g_vrSaberLog.integer ) {
			G_LogPrintf( "VRForce: %i's Grip moves %i with the %s hand (level %i, %.0f from its neck)\n", n, g->victim,
				f->hand == VR_HAND_RIGHT ? "right" : "left", gripLevel, g->dist );
		}
	}

	// moved as the hand has moved since it last moved them (each server frame:
	// WP_ForcePowersUpdate): pushing them out or pulling them in - its moves,
	// over the last VR_GRIP_TELL of them, more toward them or back than across
	// (VR_GRIP_ALONG) - at level 3 they go VR_GRIP_IN_OUT as much further or
	// nearer as it went along the stick they're on (their way from the neck as
	// it was: between g->least and VR_GRIP_FAR - caught further, drawn in to it),
	// and aren't swung; else, and ever at level 2, swung by its way from its
	// shoulder as the shoulder is now (so the shoulder's own sway - the body
	// turning to follow the head - moves no one): their way from the neck rises
	// as much as the hand's way from its shoulder has (no steeper than g->steep),
	// and turns as much as it has since it was last read which way round
	// (upright, it isn't). The hand back after a while gone (or within a unit of
	// its shoulder): from where it is - none of what it did meanwhile, nor
	// which way it was going. (dist, pitch: as far and as high, the stops aside
	// - how fast the hand moves them.)
	dist = g->dist;
	pitch = g->pitch;
	rebased = (qboolean)( known && g->lost );
	if ( rebased ) {
		VectorCopy( hand, g->yawWas );
		VectorClear( g->moved );
		g->pushing = qfalse;
	} else if ( known && VRF_GripWay( shoulder, g->handWas, &out0, &pitch0, &yaw0, &read0 ) ) {
		const float c = cosf( DEG2RAD( g->pitch ) );
		vec3_t move, stick;
		float len2, a;

		VectorSet( stick, c * cosf( DEG2RAD( g->yaw ) ), c * sinf( DEG2RAD( g->yaw ) ), sinf( DEG2RAD( g->pitch ) ) );
		VectorSubtract( hand, g->handWas, move );
		VectorAdd( g->moved, move, g->moved );
		len2 = VectorLengthSquared( g->moved );
		if ( len2 >= VR_GRIP_TELL * VR_GRIP_TELL ) {
			a = DotProduct( g->moved, stick );
			g->pushing = (qboolean)( a * a > VR_GRIP_ALONG * VR_GRIP_ALONG * ( len2 - a * a ) );
			VectorClear( g->moved );
		}
		if ( gripLevel >= FORCE_LEVEL_3 && g->pushing ) {
			dist += VR_GRIP_IN_OUT * DotProduct( move, stick );
			if ( read1 ) {
				VectorCopy( hand, g->yawWas );
			}
		} else {
			pitch += pitch1 - pitch0;
			if ( read1 ) {
				if ( VRF_GripWay( shoulder, g->yawWas, &out0, &pitch0, &yaw0, &read0 ) && read0 ) {
					g->yaw += AngleSubtract( yaw1, yaw0 );
				}
				VectorCopy( hand, g->yawWas );
			}
		}
	}
	g->dist = ( gripLevel >= FORCE_LEVEL_3 ) ? Com_Clamp( g->least, VR_GRIP_FAR, dist ) : dist;	// (level 2: as caught)
	g->pitch = Com_Clamp( -g->steep, g->steep, pitch );
	if ( known ) {
		VectorCopy( hand, g->handWas );
	}
	g->lost = (qboolean)!known;
	if ( f ) {
		VectorCopy( f->ofs, g->ofs );
		VectorCopy( f->dir, g->dir );
	}

	// where it puts them, from the neck as it is now (their way turned with
	// their room: their turning swings them as much), within the Grip's reach
	// of you - and where it would, the stops aside (no hand, or no neck: where it
	// last did)
	if ( body ) {
		VRF_GripReach( self, g, neck, f->trackYaw, gripLevel );
		VRF_GripAt( neck, g->dist, g->pitch, g->yaw + f->trackYaw, gripLevel, g->caught, g->at );
		VRF_GripAt( neck, dist, pitch, g->yaw + f->trackYaw, gripLevel, g->caught, want );
	} else {
		VectorCopy( g->at, want );
	}
	if ( taken ) {
		VectorCopy( g->at, g->atWas );	// (from there: drawn in to the stops as they're caught isn't the hand moving them)
	}
	// ...how fast it's moved them lately (over about VR_GRIP_HAND_MS: as they're
	// let go of, they go no faster - G_VRForceGripDone), the stops aside, and of
	// that how fast past the stops (a fling to them, let go there, still throws
	// them) - by their commands' times (no new command since: nothing new; their
	// clock set back: nothing till it's past where it was; the hand back after a
	// while gone, as if still - none of where they're put from where it last did)
	if ( now > g->atTime ) {
		const float k = 1.0f - expf( -( now - g->atTime ) / (float)VR_GRIP_HAND_MS ), per = rebased ? 0.0f : 1000.0f / ( now - g->atTime );
		vec3_t v;
		VectorSubtract( want, g->atWas, v );
		VectorScale( v, per, v );
		VectorSubtract( v, g->atVel, v );
		VectorMA( g->atVel, k, v, g->atVel );
		VectorSubtract( want, g->at, v );
		VectorScale( v, per, v );
		VectorSubtract( v, g->atOver, v );
		VectorMA( g->atOver, k, v, g->atOver );
		VectorCopy( g->at, g->atWas );
		g->atTime = now;
	}

	// steered there - no faster than VR_GRIP_SPEED; a Push on them going
	// (pushEffectTime), it has them till it's over
	g->steered = (qboolean)( gripEnt->client->pushEffectTime <= level.time );
	if ( g->steered ) {
		VectorSubtract( g->at, gripEnt->client->ps.origin, to );
		VectorScale( to, VR_GRIP_FOLLOW, gripEnt->client->ps.velocity );
		speed = VectorLength( gripEnt->client->ps.velocity );
		if ( speed > VR_GRIP_SPEED ) {
			VectorScale( gripEnt->client->ps.velocity, VR_GRIP_SPEED / speed, gripEnt->client->ps.velocity );
		}
		g->teleport = gripEnt->client->ps.eFlags & EF_TELEPORT_BIT;
	}
	away = Distance( gripEnt->client->ps.origin, g->caught );
	if ( away > g->most ) {
		g->most = away;
	}
	return qtrue;
}

// Whether someone's on the ground, or nearly (within a step of it).
static qboolean VRF_NearGround( const gentity_t *ent ) {
	vec3_t down;
	trace_t tr;

	VectorCopy( ent->client->ps.origin, down );
	down[2] -= STEPSIZE;
	trap->Trace( &tr, ent->client->ps.origin, ent->r.mins, ent->r.maxs, down, ent->s.number, MASK_PLAYERSOLID, qfalse, 0, 0 );
	return (qboolean)( tr.fraction < 1.0f || tr.startsolid );
}

// A VR player's Grip over (WP_ForcePowerStop): whom it moved with the hand,
// let go of - flying on as they were going, and as fast as the hand was moving
// them past the stops too (a fling out to the far one throws them on), but no
// faster than the hand has moved them lately (steered somewhere it put them a
// while ago, or against a wall, they're let go of as slow as it's moving), nor
// VR_GRIP_SPEED; thrown - VR_FORCE_GRIP_THROWN or faster, through the world
// and from whoever let go of them alike (carried along as they go, or they
// flung off by a Push, a fall, a teleporter, isn't a throw) - along the
// ground, or a step over it, not steeply down (more than 30 degrees), tossed
// up off it (VR_GRIP_TOSS, no faster for it than VR_GRIP_SPEED). Not steered by
// it last, or a Push on them now (since: a Push landing as they're let go of
// has them), dead or gone through a teleporter: as they go. The log says how
// fast.
void G_VRForceGripDone( gentity_t *self ) {
	const int n = self ? self->s.number : -1;
	vrGripMove_t *g;
	gentity_t *v;
	float speed = 0.0f;

	if ( n < 0 || n >= MAX_CLIENTS || vrGrip[n].victim < 0 || vrGrip[n].victim >= ENTITYNUM_MAX_NORMAL ) {
		return;
	}
	g = &vrGrip[n];
	v = &g_entities[g->victim];
	if ( v->inuse && v->client ) {
		float *vel = v->client->ps.velocity;
		speed = VectorLength( vel );
		if ( g->steered && v->client->pushEffectTime <= level.time && v->health > 0 &&
			( v->client->ps.eFlags & EF_TELEPORT_BIT ) == g->teleport ) {
			const float hand = Q_min( VectorLength( g->atVel ), VR_GRIP_SPEED );
			vec3_t rel;
			VectorAdd( vel, g->atOver, vel );
			speed = VectorLength( vel );
			if ( speed > hand ) {
				VectorScale( vel, hand / speed, vel );
				speed = hand;
			}
			VectorSubtract( vel, self->client->ps.velocity, rel );
			if ( Q_min( speed, VectorLength( rel ) ) >= VR_FORCE_GRIP_THROWN && vel[2] < VR_GRIP_TOSS && vel[2] >= -0.5f * speed &&
				VRF_NearGround( v ) ) {
				vel[2] = VR_GRIP_TOSS;
				speed = VectorLength( vel );
				if ( speed > VR_GRIP_SPEED ) {
					const float flat = sqrtf( vel[0] * vel[0] + vel[1] * vel[1] ), most = sqrtf( VR_GRIP_SPEED * VR_GRIP_SPEED - VR_GRIP_TOSS * VR_GRIP_TOSS );
					vel[0] *= most / flat;
					vel[1] *= most / flat;
					speed = VR_GRIP_SPEED;
				}
			}
		}
	}
	if ( g_vrSaberLog.integer ) {
		G_LogPrintf( "VRForce: %i's Grip let %i go after %.1f s, at %.0f a second (moved as far as %.0f from where it caught them)\n",
			n, g->victim, ( level.time - g->time ) * 0.001f, speed, g->most );
	}
	memset( g, 0, sizeof( *g ) );
	g->victim = ENTITYNUM_NONE;
}
