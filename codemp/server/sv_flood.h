/*
===========================================================================
OpenJK VR (multiplayer) - flood protection's allowance (SV_ClientCommand,
sv_client.cpp): in place of one command a period, a client's commands are
taken so many at once - a menu sends a few together, and chat may follow
them - and one more each period after. It's kept as the time it's whole
again: each command taken puts that a period later (from now, if it was
whole); one that would put it more than so many periods ahead isn't taken.
Slow, as one a period was: one not taken empties it - the next is taken a
period from now - so a flood keeps itself out. One at once is the old way.
Settings cut while a client owes more than they'd let it leave it empty,
no more.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#pragma once

#define SV_FLOOD_PERIOD_MAX	60000	// (ms) the period, at the most
#define SV_FLOOD_BURST_MAX	100		// commands at once, at the most

// The period as sv_floodProtect says it: 1, a second; more, that many ms
// (none, 0 or less: no protection - not asked).
static inline int SV_FloodPeriod( int floodProtect ) {
	return ( floodProtect == 1 ) ? 1000 : ( floodProtect < 1 ) ? 1 : ( floodProtect > SV_FLOOD_PERIOD_MAX ) ? SV_FLOOD_PERIOD_MAX : floodProtect;
}

// A command, now: qtrue if it's taken (*whole: when the allowance is whole
// again; burst: commands at once - sv_floodProtectBurst; slow: sv_floodProtectSlow).
static inline qboolean SV_FloodAllow( int *whole, int now, int period, int burst, qboolean slow ) {
	if ( burst < 1 ) {
		burst = 1;
	} else if ( burst > SV_FLOOD_BURST_MAX ) {
		burst = SV_FLOOD_BURST_MAX;
	}
	if ( *whole - now > burst * period ) {
		*whole = now + burst * period;	// (more owed than it can be now: the settings were cut since - empty, no more)
	}
	if ( *whole - now > ( burst - 1 ) * period ) {
		if ( slow ) {
			*whole = now + burst * period;
		}
		return qfalse;
	}
	*whole = ( ( *whole - now > 0 ) ? *whole : now ) + period;
	return qtrue;
}
