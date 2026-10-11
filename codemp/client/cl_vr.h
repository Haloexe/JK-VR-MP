/*
===========================================================================
OpenJK VR (multiplayer) - engine side OpenXR integration.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#pragma once

#ifdef OPENJK_VR

#include "rd-common/tr_vr.h"
#include "qcommon/vr_public.h"

// Cvars and console commands. Safe to call more than once.
void		VR_Init( void );
// Destroys the OpenXR instance (call when the client shuts down).
void		VR_Shutdown( void );

// Called by the window code before creating the window: applies latched
// vr_* cvars and says whether a headset session should be attempted.
qboolean	VR_WantSession( void );

// Called by the window code right after the OpenGL context was created.
// Returns qtrue if a VR session is running; eye buffer size is returned so
// the renderer can render at headset resolution.
qboolean	VR_CreateSession( int *eyeWidth, int *eyeHeight );
// Called by the window code before the OpenGL context is destroyed.
void		VR_DestroySession( void );

// True while the renderer runs at eye-buffer resolution for a headset.
qboolean	VR_IsActive( void );
// True while the headset session is running.
qboolean	VR_IsRunning( void );
// True when the last frame was paced by the headset (xrWaitFrame), so the
// engine's own frame limiter must stay out of the way.
qboolean	VR_IsPacingFrames( void );

// Waits for the headset's next frame, samples the head pose and controllers,
// and puts the menu cursor where a controller points. Called at the top of
// CL_Frame, outside any module.
void		VR_BeginFrame( void );

// Reports every relative move of the UI module's cursor (UIVM_MouseEvent),
// so the engine knows where that cursor is.
void		VR_UICursorMoved( int dx, int dy );
// Called right after a UI module initialised: puts its cursor in a known spot.
void		VR_UIStarted( void );

// Renders and submits a complete VR frame. Returns qfalse if the frame must
// be drawn the normal desktop way instead (no session / headset idle).
qboolean	VR_UpdateScreen( void );

// Aim: folds the head (or right hand, for guns) orientation and thumbstick
// turning into cl.viewangles. Called from CL_CreateCmd after mouse/keyboard.
void		VR_ApplyHeadAim( void );

// Thumbstick movement, added to the usercmd in CL_CreateCmd.
void		VR_AddMovement( usercmd_t *cmd );

// Read by the cgame through cgameImport_t.ext.VR_GetClientState.
const vrClientState_t *VR_GetClientState( void );

// Read by the renderer through refimport_t.
const vrRenderState_t *VR_GetRenderState( void );

#else // !OPENJK_VR

static inline qboolean VR_IsRunning( void ) { return qfalse; }

#endif // OPENJK_VR
