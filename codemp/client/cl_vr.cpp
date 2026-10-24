/*
===========================================================================
OpenJK VR (multiplayer) - engine side OpenXR integration.

Owns the OpenXR instance/session, the per-eye render targets and
swapchains, the frame loop and head tracking. The renderer is told what to
do through vrRenderState_t (see rd-common/tr_vr.h), so the cgame/ui modules
(base or a server mod's) run unmodified.

Frame flow:
  CL_Frame -> VR_BeginFrame (xrWaitFrame/xrBeginFrame, sample head pose)
           -> CL_SendCmd   (VR_ApplyHeadAim folds the head pose into the cmd)
           -> SCR_UpdateScreen -> VR_UpdateScreen
                each eye renders into a private RGBA8 framebuffer, and only
                once it is finished is it copied into the OpenXR swapchain
                (so an error thrown mid-render never leaves a swapchain
                image acquired), then xrEndFrame.

The OpenXR session/swapchain handling started from Team Beef's JKXR
(TBXR_Common.cpp, https://github.com/Team-Beef-Studios/JKXR), which is also
GPLv2 and based on OpenJK.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

#include "client.h"

#ifdef OPENJK_VR

#include "cl_vr.h"
#include "cl_uiapi.h"
#include "sys/sys_public.h"
#include "qcommon/vr_force.h"	// the Force hand's gestures

#include <SDL.h>

#if defined(_WIN32)

#include <windows.h>
#include <unknwn.h>
#include <GL/gl.h>

// The OpenXR loader is loaded at runtime, so the game still starts (in
// desktop mode) when libopenxr_loader.dll is missing.
#define XR_NO_PROTOTYPES
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_OPENGL
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

void SCR_DrawScreenField( stereoFrame_t stereoFrame );
void Sys_QueEvent( int time, sysEventType_t type, int value, int value2, int ptrLength, void *ptr );

// Controller buttons are queued as ordinary key events and handled at the
// start of the next frame, like keyboard keys, so nothing re-enters the
// cgame/ui while they are busy (e.g. during map loading).
static void VR_QueueKey( int key, qboolean down ) {
	Sys_QueEvent( Sys_Milliseconds(), SE_KEY, key, down, 0, NULL );
}

/*
================================================================================

OpenXR entry points

================================================================================
*/

#define VR_XR_GLOBAL_FUNCS(X) \
	X( xrEnumerateInstanceExtensionProperties ) \
	X( xrCreateInstance )

#define VR_XR_INSTANCE_FUNCS(X) \
	X( xrDestroyInstance ) \
	X( xrResultToString ) \
	X( xrGetInstanceProperties ) \
	X( xrGetSystem ) \
	X( xrGetSystemProperties ) \
	X( xrEnumerateViewConfigurationViews ) \
	X( xrEnumerateSwapchainFormats ) \
	X( xrCreateSwapchain ) \
	X( xrDestroySwapchain ) \
	X( xrEnumerateSwapchainImages ) \
	X( xrAcquireSwapchainImage ) \
	X( xrWaitSwapchainImage ) \
	X( xrReleaseSwapchainImage ) \
	X( xrCreateSession ) \
	X( xrDestroySession ) \
	X( xrBeginSession ) \
	X( xrEndSession ) \
	X( xrRequestExitSession ) \
	X( xrPollEvent ) \
	X( xrCreateReferenceSpace ) \
	X( xrDestroySpace ) \
	X( xrLocateSpace ) \
	X( xrLocateViews ) \
	X( xrWaitFrame ) \
	X( xrBeginFrame ) \
	X( xrEndFrame ) \
	X( xrGetOpenGLGraphicsRequirementsKHR ) \
	X( xrStringToPath ) \
	X( xrCreateActionSet ) \
	X( xrDestroyActionSet ) \
	X( xrCreateAction ) \
	X( xrSuggestInteractionProfileBindings ) \
	X( xrAttachSessionActionSets ) \
	X( xrCreateActionSpace ) \
	X( xrSyncActions ) \
	X( xrGetActionStateBoolean ) \
	X( xrGetActionStateFloat ) \
	X( xrGetActionStateVector2f ) \
	X( xrApplyHapticFeedback ) \
	X( xrGetCurrentInteractionProfile ) \
	X( xrPathToString )

static HMODULE						xrLoaderLib;
static PFN_xrGetInstanceProcAddr	xrGetInstanceProcAddr;
#define VR_XR_DECLARE(name) static PFN_##name name;
VR_XR_GLOBAL_FUNCS( VR_XR_DECLARE )
VR_XR_INSTANCE_FUNCS( VR_XR_DECLARE )
#undef VR_XR_DECLARE

static qboolean VR_LoadLoader( void ) {
	if ( xrGetInstanceProcAddr ) {
		return qtrue;
	}
	if ( !xrLoaderLib ) {
		// only next to the game and in system32, never the current directory
		const DWORD flags = LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32;
		xrLoaderLib = LoadLibraryExA( "libopenxr_loader.dll", NULL, flags );
		if ( !xrLoaderLib ) {
			xrLoaderLib = LoadLibraryExA( "openxr_loader.dll", NULL, flags );
		}
	}
	if ( !xrLoaderLib ) {
		Com_Printf( S_COLOR_YELLOW "VR: libopenxr_loader.dll not found next to the game. Running in desktop mode.\n" );
		return qfalse;
	}
	xrGetInstanceProcAddr = (PFN_xrGetInstanceProcAddr)GetProcAddress( xrLoaderLib, "xrGetInstanceProcAddr" );
	if ( !xrGetInstanceProcAddr ) {
		Com_Printf( S_COLOR_YELLOW "VR: the OpenXR loader DLL is invalid. Running in desktop mode.\n" );
		return qfalse;
	}
#define VR_XR_LOAD(name) xrGetInstanceProcAddr( XR_NULL_HANDLE, #name, (PFN_xrVoidFunction *)&name );
	VR_XR_GLOBAL_FUNCS( VR_XR_LOAD )
#undef VR_XR_LOAD
	if ( !xrEnumerateInstanceExtensionProperties || !xrCreateInstance ) {
		Com_Printf( S_COLOR_YELLOW "VR: the OpenXR loader DLL is invalid. Running in desktop mode.\n" );
		xrGetInstanceProcAddr = NULL;
		return qfalse;
	}
	return qtrue;
}

static qboolean VR_LoadInstanceFunctions( XrInstance instance ) {
	qboolean ok = qtrue;
#define VR_XR_LOAD(name) \
	if ( XR_FAILED( xrGetInstanceProcAddr( instance, #name, (PFN_xrVoidFunction *)&name ) ) || !name ) { \
		Com_Printf( S_COLOR_YELLOW "VR: OpenXR function %s unavailable\n", #name ); ok = qfalse; }
	VR_XR_INSTANCE_FUNCS( VR_XR_LOAD )
#undef VR_XR_LOAD
	return ok;
}

/*
================================================================================

Cvars

================================================================================
*/

static cvar_t *vr_enabled;
static cvar_t *vr_resolutionScale;
static cvar_t *vr_worldScale;
static cvar_t *vr_positional;
static cvar_t *vr_positionalMax;
static cvar_t *vr_hudFov;
static cvar_t *vr_hudDepth;
static cvar_t *vr_menuDistance;
static cvar_t *vr_menuHeight;
static cvar_t *vr_headAim;
static cvar_t *vr_mirror;
static cvar_t *vr_handAim;
static cvar_t *vr_turnMode;
static cvar_t *vr_turnAngle;
static cvar_t *vr_turnSpeed;
static cvar_t *vr_snapFade;
static cvar_t *vr_vignette;
static cvar_t *vr_seated;
static cvar_t *vr_height;
static cvar_t *vr_realCrouch;
static cvar_t *vr_moveDirection;
static cvar_t *vr_swapSticks;
static cvar_t *vr_deadzone;
static cvar_t *vr_gunPitch;
static cvar_t *vr_leftHanded;
static cvar_t *vr_gripPress;
static cvar_t *vr_forceGestures;
static cvar_t *vr_forceThrust;

/*
================================================================================

Minimal OpenGL framebuffer object entry points (loaded through SDL)

================================================================================
*/

#ifndef APIENTRY
#define APIENTRY
#endif

#define VR_GL_FRAMEBUFFER					0x8D40
#define VR_GL_READ_FRAMEBUFFER				0x8CA8
#define VR_GL_DRAW_FRAMEBUFFER				0x8CA9
#define VR_GL_RENDERBUFFER					0x8D41
#define VR_GL_COLOR_ATTACHMENT0				0x8CE0
#define VR_GL_DEPTH_STENCIL_ATTACHMENT		0x821A
#define VR_GL_DEPTH24_STENCIL8				0x88F0
#define VR_GL_FRAMEBUFFER_COMPLETE			0x8CD5
#define VR_GL_SRGB8_ALPHA8					0x8C43
#define VR_GL_RGBA8							0x8058
#define VR_GL_FRAMEBUFFER_SRGB				0x8DB9
#define VR_GL_CLAMP_TO_EDGE					0x812F
// (what the renderer may leave on that VR_DrawComfort turns off while it draws)
#define VR_GL_TEXTURE0						0x84C0
#define VR_GL_MAX_TEXTURE_UNITS				0x84E2
#define VR_GL_TEXTURE_3D					0x806F
#define VR_GL_TEXTURE_CUBE_MAP				0x8513
#define VR_GL_TEXTURE_RECTANGLE				0x84F5
#define VR_GL_VERTEX_PROGRAM_ARB			0x8620
#define VR_GL_FRAGMENT_PROGRAM_ARB			0x8804
#define VR_GL_REGISTER_COMBINERS_NV			0x8522

typedef void	(APIENTRY *vrGenFramebuffers_t)( GLsizei n, GLuint *ids );
typedef void	(APIENTRY *vrDeleteFramebuffers_t)( GLsizei n, const GLuint *ids );
typedef void	(APIENTRY *vrBindFramebuffer_t)( GLenum target, GLuint id );
typedef void	(APIENTRY *vrFramebufferTexture2D_t)( GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level );
typedef void	(APIENTRY *vrGenRenderbuffers_t)( GLsizei n, GLuint *ids );
typedef void	(APIENTRY *vrDeleteRenderbuffers_t)( GLsizei n, const GLuint *ids );
typedef void	(APIENTRY *vrBindRenderbuffer_t)( GLenum target, GLuint id );
typedef void	(APIENTRY *vrRenderbufferStorage_t)( GLenum target, GLenum format, GLsizei w, GLsizei h );
typedef void	(APIENTRY *vrFramebufferRenderbuffer_t)( GLenum target, GLenum attachment, GLenum rbtarget, GLuint rb );
typedef GLenum	(APIENTRY *vrCheckFramebufferStatus_t)( GLenum target );
typedef void	(APIENTRY *vrBlitFramebuffer_t)( GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1, GLint dy1, GLbitfield mask, GLenum filter );
typedef void	(APIENTRY *vrCopyImageSubData_t)( GLuint srcName, GLenum srcTarget, GLint srcLevel, GLint srcX, GLint srcY, GLint srcZ,
										GLuint dstName, GLenum dstTarget, GLint dstLevel, GLint dstX, GLint dstY, GLint dstZ,
										GLsizei width, GLsizei height, GLsizei depth );
typedef void	(APIENTRY *vrActiveTexture_t)( GLenum texture );

static vrGenFramebuffers_t			vglGenFramebuffers;
static vrDeleteFramebuffers_t		vglDeleteFramebuffers;
static vrBindFramebuffer_t			vglBindFramebuffer;
static vrFramebufferTexture2D_t		vglFramebufferTexture2D;
static vrGenRenderbuffers_t			vglGenRenderbuffers;
static vrDeleteRenderbuffers_t		vglDeleteRenderbuffers;
static vrBindRenderbuffer_t			vglBindRenderbuffer;
static vrRenderbufferStorage_t		vglRenderbufferStorage;
static vrFramebufferRenderbuffer_t	vglFramebufferRenderbuffer;
static vrCheckFramebufferStatus_t	vglCheckFramebufferStatus;
static vrBlitFramebuffer_t			vglBlitFramebuffer;
static vrCopyImageSubData_t			vglCopyImageSubData;	// optional (GL 4.3 / ARB_copy_image)
static vrActiveTexture_t			vglActiveTexture;		// optional (GL 1.3; without it, one texture unit)

// What the driver has that the renderer may leave on (VR_DrawComfort turns it
// off while it draws; asked once a session has its context, VR_ComfortGL).
static qboolean						vrGLAsked;
static int							vrGLTextureUnits;		// fixed-function texture units, each one's texturing off
static qboolean						vrGLRectangle, vrGLVertexProgram, vrGLFragmentProgram, vrGLCombiners;

static qboolean VR_LoadGL( void ) {
#define VRGL(name, type) v##name = (type)SDL_GL_GetProcAddress( #name ); if ( !v##name ) { Com_Printf( S_COLOR_YELLOW "VR: missing OpenGL function %s\n", #name ); return qfalse; }
	VRGL( glGenFramebuffers,			vrGenFramebuffers_t );
	VRGL( glDeleteFramebuffers,			vrDeleteFramebuffers_t );
	VRGL( glBindFramebuffer,			vrBindFramebuffer_t );
	VRGL( glFramebufferTexture2D,		vrFramebufferTexture2D_t );
	VRGL( glGenRenderbuffers,			vrGenRenderbuffers_t );
	VRGL( glDeleteRenderbuffers,		vrDeleteRenderbuffers_t );
	VRGL( glBindRenderbuffer,			vrBindRenderbuffer_t );
	VRGL( glRenderbufferStorage,		vrRenderbufferStorage_t );
	VRGL( glFramebufferRenderbuffer,	vrFramebufferRenderbuffer_t );
	VRGL( glCheckFramebufferStatus,		vrCheckFramebufferStatus_t );
	VRGL( glBlitFramebuffer,			vrBlitFramebuffer_t );
#undef VRGL
	vglCopyImageSubData = (vrCopyImageSubData_t)SDL_GL_GetProcAddress( "glCopyImageSubData" );
	vglActiveTexture = (vrActiveTexture_t)SDL_GL_GetProcAddress( "glActiveTexture" );
	if ( !vglActiveTexture ) {
		vglActiveTexture = (vrActiveTexture_t)SDL_GL_GetProcAddress( "glActiveTextureARB" );
	}
	vrGLAsked = qfalse;	// (asked again of this context)
	return qtrue;
}

static qboolean VR_GLHasExtension( const char *name ) {
	const char *all = (const char *)glGetString( GL_EXTENSIONS );
	const size_t len = strlen( name );

	for ( const char *p = all; p && ( p = strstr( p, name ) ) != NULL; p += len ) {
		if ( ( p == all || p[-1] == ' ' ) && ( p[len] == ' ' || p[len] == '\0' ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

// What VR_DrawComfort turns off is only what the driver has: turning off what
// it hasn't would be a GL error (fatal with r_ignoreGLErrors 0).
static void VR_ComfortGL( void ) {
	if ( vrGLAsked ) {
		return;
	}
	vrGLAsked = qtrue;
	vrGLTextureUnits = 1;
	if ( vglActiveTexture ) {
		GLint units = 1;
		glGetIntegerv( VR_GL_MAX_TEXTURE_UNITS, &units );
		vrGLTextureUnits = Com_Clampi( 1, 32, units );
	}
	vrGLRectangle = (qboolean)( VR_GLHasExtension( "GL_ARB_texture_rectangle" ) || VR_GLHasExtension( "GL_EXT_texture_rectangle" ) ||
		VR_GLHasExtension( "GL_NV_texture_rectangle" ) );
	vrGLVertexProgram = VR_GLHasExtension( "GL_ARB_vertex_program" );
	vrGLFragmentProgram = VR_GLHasExtension( "GL_ARB_fragment_program" );
	vrGLCombiners = VR_GLHasExtension( "GL_NV_register_combiners" );
}

/*
================================================================================

State

================================================================================
*/

// Controller buttons arrive as ordinary bindable keys AUX16..AUX27 (AUX0+
// are used by extra mouse buttons), so players can rebind them like any key.
// The triggers and grips go by what each hand does, not which hand it is:
// AUX16 is the trigger of the hand holding your weapon (the right, or the
// left: VR_WeaponHand), AUX17 the other one; AUX18 is the Use grip and AUX19
// the Force grip (the right and the left - swapped, vr_leftHanded).
#define VR_FIRST_KEY	( A_AUX0 + 16 )
enum {
	VR_KEY_TRIGGER_R,		// AUX16 (the weapon hand's trigger)
	VR_KEY_TRIGGER_L,		// AUX17 (the other trigger)
	VR_KEY_GRIP_R,			// AUX18 (the Use grip)
	VR_KEY_GRIP_L,			// AUX19 (the Force grip)
	VR_KEY_A,				// AUX20
	VR_KEY_B,				// AUX21
	VR_KEY_X,				// AUX22
	VR_KEY_Y,				// AUX23
	VR_KEY_STICK_L,			// AUX24
	VR_KEY_STICK_R,			// AUX25
	VR_KEY_STICK_R_UP,		// AUX26
	VR_KEY_STICK_R_DOWN,	// AUX27
	VR_NUM_KEYS,

	VR_BUTTON_MENU = VR_NUM_KEYS,	// always Escape, not bindable
	VR_NUM_BUTTONS
};

// what each hand has that presses a key by what that hand does (VR_HandControl)
enum {
	VR_CTL_TRIGGER,
	VR_CTL_GRIP,
	VR_NUM_CONTROLS
};

static const char *vrDefaultBinds[VR_NUM_KEYS] = {
	"+attack",				// the weapon hand's trigger (right)
	"+altattack",			// the other trigger (left)
	"+use",					// the Use grip (right; left-handed, the left)
	"+useforce",			// the Force grip (left; left-handed, the right)
	"+moveup",				// A: jump
	"+movedown",			// B: crouch
	"forceprev",			// X
	"forcenext",			// Y
	"+speed",				// left stick click: walk
	"saberAttackCycle",		// right stick click: saber style
	"weapnext",				// right stick up
	"weapprev",				// right stick down
};

// Private per-eye render target: the engine draws here, then the finished
// image is copied into the swapchain.
typedef struct vrTarget_s {
	GLuint		fbo;
	GLuint		color;
	GLuint		depthStencil;
} vrTarget_t;

typedef struct vrSwapchain_s {
	XrSwapchain					handle;
	uint32_t					imageCount;
	XrSwapchainImageOpenGLKHR	*images;
	GLuint						*fbos;		// only for the glBlitFramebuffer fallback
} vrSwapchain_t;

typedef struct vrState_s {
	// instance
	qboolean				instanceTried;
	XrInstance				instance;
	XrSystemId				systemId;
	char					runtimeName[XR_MAX_RUNTIME_NAME_SIZE];
	char					systemName[XR_MAX_SYSTEM_NAME_SIZE];
	XrViewConfigurationView	viewConfig[2];
	qboolean				instanceLost;	// runtime quit/restarted: recreate on next vid_restart
	qboolean				extTouchPlus;	// XR_META_touch_controller_plus enabled (Quest 3: the index finger's curl...)
	qboolean				extTouchPro;	// XR_FB_touch_controller_pro enabled (...Touch Pro the same...)
	qboolean				extProximity;	// XR_FB_touch_controller_proximity enabled (...Touch: the thumb and finger near)

	// render targets (exist as long as the renderer runs at eye resolution)
	qboolean				targetsReady;
	int						width, height;
	vrTarget_t				targets[2];

	// session
	XrSession				session;
	XrSessionState			sessionState;
	qboolean				running;		// between xrBeginSession and xrEndSession
	qboolean				focused;
	qboolean				lost;			// runtime ended the session (needs vid_restart)
	XrSpace					localSpace;
	XrSpace					viewSpace;
	XrSpace					stageSpace;		// the floor (XR_NULL_HANDLE: the runtime hasn't one)
	int64_t					colorFormat;
	vrSwapchain_t			swapchains[2];

	// frame
	qboolean				paced;			// the last frame was paced by xrWaitFrame
	qboolean				frameWaited;	// xrWaitFrame done, xrBeginFrame pending
	qboolean				frameBegun;		// xrBeginFrame done, xrEndFrame pending
	XrFrameState			frameState;
	XrView					views[2];
	XrPosef					submitPose[2];	// eye poses the images were actually rendered with
	qboolean				poseValid;
	XrPosef					headPose;
	qboolean				recenterPending;
	XrVector3f				positionOrigin;
	qboolean				floorValid;		// your head's height above the floor this frame...
	float					floorHeadY;		// ...(metres, the stage space's)
	qboolean				floorLocalKnown;	// the floor's height in the local space, as last located...
	float					floorLocalY;	// ...(kept while it isn't)
	qboolean				seatedWas;		// Play Seated was on (switched on: your head then is how you sit)
	float					headV;			// your height known: your head's height, as standing, this frame (metres)
	qboolean				crouching;		// Real Crouch: your head low, the commands crouch you
	// the game's eyes, as the cgame says each frame (VR_SetEyeDrop)
	float					eyeDrop;		// how far below its standing eyes they are now (units: crouched, a pose)...
	int						eyeDropTime;	// ...said then (cls.realtime)
	qboolean				gamePosed;		// in a pose (getting down into it, or up out of it)...
	float					poseRefV;		// ...your head's height (headV) as you took it, or recentred in it
	qboolean				gameSwimming;	// in water to the waist
	qboolean				gameOwnEyes;	// the camera at your character's eyes, yours to move (first person)
	qboolean				crouchOwnsDrop;	// they're down because Real Crouch crouched you: your view stays at your head
	qboolean				keyCrouch;		// the crouch key (B) down in the last command
	float					viewOfs[3];		// your head's offset from the game's eyes, as far as it goes (tracking space, metres)
	// a switch in how high your view goes (your height set, a pose begun or
	// over, whose the crouch is) eased over from where it was, not jumped
	// (VR_RiseSwitch)
	float					riseH;			// the height it went by...
	float					riseCarry;		// ...what's still to ease out (metres)...
	int						riseTime;		// ...as of (ms)
	float					upReach;		// how far above the game's eyes your view goes now (metres, eased)

	// head angles, tracking space, Quake convention (degrees)
	float					headYaw;
	float					headPitch;

	// head-aim bookkeeping
	qboolean				haveLastYaw;
	float					lastAimYaw;

	// view lock (spectating, dead, intermission, demos, head-aim off)
	qboolean				viewLocked;
	float					lockYaw;

	// menu quad placement
	qboolean				lastScreenLayer;
	XrPosef					quadPose;
	XrVector3f				quadOrigin;		// the head as the quad was placed, and the way it faced:
	float					quadYaw;		// the quad's that far in front of it, that tall
	float					quadDist, quadHeight;
	float					quadWantDist, quadWantHeight;	// Menu Distance and Size changed while it's up:
	int						quadWantTime;					// as they were, since when

	qboolean				inUpdateScreen;

	// controllers
	qboolean				inputReady;		// actions + bindings created (per instance)
	qboolean				inputAttached;	// attached to the current session
	XrActionSet				actionSet;
	XrPath					handPath[2];
	XrAction				actGripPose, actAimPose, actMove, actTurn;
	XrAction				actTrigger, actSqueeze, actStickClick;
	XrAction				actA, actB, actX, actY, actMenu;
	XrAction				actHaptic;
	XrAction				actTriggerTouch, actThumbTouch;			// your fingers (vrFingerSense_t): the touch sensors...
	XrAction				actTriggerNear, actThumbNear, actTriggerCurl;	// ...and, where offered, near and curled
	vrFingerSense_t			fingers[2];		// what each controller senses of your fingers, as of the last poll
	char					profile[2][XR_MAX_PATH_LENGTH];	// each hand's controllers (their interaction profile), as last said
	XrSpace					gripSpace[2], aimSpace[2];
	XrPosef					gripPose[2], aimPose[2];
	qboolean				handValid[2];
	qboolean				handTracked[2];		// ...and really tracked this poll, not a last known or guessed place (VR_NoteTracking)...
	qboolean				handTrackedSeen[2];	// ...as the runtime has ever said of it
	int						handTrackTime[2];	// last time each hand was tracked (ms)
	vec3_t					handAimAxis[2][3];	// each hand's aim, as it points (vrClient.aimAxis: the gun's tilted)
	float					stick[2][2];	// each hand's stick, as it is
	float					moveX, moveY;	// the stick that moves you (the left; vr_swapSticks, the right)
	float					turnX, turnY;	// the one that turns you (the right, or the left)
	qboolean				turnLatched;	// snap turn fired, waiting for the stick to recentre
	qboolean				moveLatched;	// the moving stick picked in the pose wheel (Swap Sticks): waiting for it to recentre
	qboolean				weapLatched;	// stick-up/down weapon switch fired
	float					pendingTurn;	// degrees to add to the body yaw this frame
	qboolean				keyDown[VR_NUM_BUTTONS];
	int						keySent[VR_NUM_BUTTONS];	// key each held button sent when pressed (0 = none)
	qboolean				keyBlocked[VR_NUM_BUTTONS];	// held through a focus loss: ignored until let go
	int						keyHand[VR_NUM_BUTTONS];	// a grip key: the hand whose grip presses it now
	qboolean				ctlDown[VR_NUM_CONTROLS][2];	// a hand's trigger, grip: held (VR_HandControl)...
	int						ctlKey[VR_NUM_CONTROLS][2];		// ...pressing this key + 1, chosen as it was pressed (0: none)
	int						weaponHand;		// the hand the cgame says holds your weapon (-1: hands empty; 2: both)...
	int						weaponHandTime;	// ...as of this cls.realtime...
	qboolean				weaponHandSaid;	// ...(it has said)
	int						cmdButtons;		// buttons the cgame sets on every command (BUTTON_VR_*)...
	int						cmdButtonsTime;	// ...as of this cls.realtime
	int						genCmds[4];		// generic commands the cgame asked for, each going with the next
	int						numGenCmds;		// command that has none, in turn - and the Force gestures' Push and
	qboolean				genThrown[4];	// Pull (VR_ForceThrow): which they are, and the way each went (VR render
	vec3_t					genThrust[4];	// space: the controllers' axes)
	// the Force hand (Force gestures: VR_ForcePoll, qcommon/vr_force.h)
	vrfState_t				force;
	int						forceHand;		// the Force hand, as the last poll found it (-1: none)
	qboolean				forceGrip;		// the Force grip's press went to the gestures, not its key (VR_SetKey): it's
	int						forceGripHand;	// reaching out, with that hand, till it's let go...
	qboolean				forceGripDead;	// ...or it counts no more (the hand stopped being the Force hand): nothing then
	qboolean				forceTrigger[2];	// a hand's trigger press went to the gestures (VR_HandControl), till let go
	int						forceHeld;		// VR_FORCE_*: the buttons the gestures hold down
	int						forceTapUntil;	// a quick squeeze: Use Force held till then (Sys_Milliseconds; 0: not)
	int						forcePushes, forcePulls, forceTaps;	// counted, for the cgame (vrForceState_t)
	float					forceSpeed;		// the last Push's or Pull's speed (metres a second)
	qboolean				grabbing[2];	// the hand's grip is held as a grab, not its key
	qboolean				gripFresh[2];	// the hand's grip let go since it was squeezed: squeezed now, it may grab
	qboolean				grabHolds;		// ...and grabs hold what they've taken (VR_GRAB_HOLDS): let go of only well open
	int						wheelState;		// Y: VR_WHEEL_* (tapped, or held for the pose wheel)
	int						wheelPressTime;	// ...pressed then (ms)
	int						grabZones;		// hands the cgame says could grab now (bit per hand)...
	int						grabZoneTime;	// ...as of this cls.realtime
	qboolean				topOfFrame;		// this poll runs at the top of CL_Frame (not nested)

	// menu pointer: a controller points at the menu screen like a mouse
	qboolean				pointerMode;	// the menu screen is up and takes the pointer
	qboolean				pointerHit;		// the pointing controller is on the screen
	qboolean				pointerLeft;	// the left hand points (VR_PointingHand)
	int						pointerLastHand;
	qboolean				aimed[2];		// each hand aimed at the menu screen at the last poll...
	int						aimSince[2];	// ...since (ms)...
	int						aimLast[2];		// ...last (ms)
	float					pointerX, pointerY;		// on the screen, in menu units (640x480)
	qboolean				pointerFresh;	// polled this frame, not yet applied
	qboolean				pointerForce;	// move the cursor even if the controller is still
	int						pointerSentX, pointerSentY;	// where the cursor was last put
	qboolean				pullArmed[2];	// trigger at rest: the next rise is a pull
	qboolean				pulling[2];		// trigger on its way from rest to a click...
	int						pullStart[2];	// ...since (ms)
	qboolean				pressing;		// a trigger crosses the click point this poll
	int						uiCursorX, uiCursorY;	// the UI module's own cursor, mirrored
	int						scrollDir;		// a stick held up (+1) or down (-1) in the menu
	int						scrollTime;		// next mouse wheel repeat (ms)
	int						scrollStepTime;	// last wheel step (ms)
	qboolean				scrollLatched;	// a stick already pushed when the menu came up

	// aim source this frame: head, or the gun hand (ranged weapons: the
	// right hand, the left when left-handed - VR_GunHand)
	qboolean				aimFromHand;
	float					handAimYaw, handAimPitch;
	int						cgameQueryTime;	// last time the cgame read the VR state (cls.realtime)
	int						lastPollTime;	// for smooth turning (ms)
	float					lastRefYaw;		// referenceYaw of the previous usercmd

	// comfort: the Comfort Vignette (vr_vignette) and Snap Fade (vr_snapFade)
	float					comfortMove;	// how hard the moving stick moved you (1: running flat out)...
	int						comfortMoveTime;	// ...last (ms; VR_AddMovement)
	float					comfortTurn;	// how fast smooth turning turned you at the last poll (1: VR_COMFORT_TURN_FULL)
	float					vignette;		// the vignette as it's drawn (0 none, 1 as strong as vr_vignette has it)...
	int						vignetteTime;	// ...as of (ms)
	float					snapHeld;		// snap turns held till the view's dark (degrees, as pendingTurn)
	int						snapPhase;		// VR_SNAP_*
	float					snapFade;		// how dark the view is (0 not, 1 black)...
	int						snapFadeTime;	// ...as of (ms)
	int						snapTurn;		// black, its turn in pendingTurn: VR_SNAP_TURN_* ...
	int						snapBlackTime;	// ...black since (ms)
} vrState_t;

static vrState_t		vr;
static vrRenderState_t	vrRender;
static vrClientState_t	vrClient;

static const char *VR_ResultString( XrResult res ) {
	static char buf[XR_MAX_RESULT_STRING_SIZE];
	if ( vr.instance != XR_NULL_HANDLE && xrResultToString && XR_SUCCEEDED( xrResultToString( vr.instance, res, buf ) ) ) {
		return buf;
	}
	Com_sprintf( buf, sizeof( buf ), "XrResult %d", (int)res );
	return buf;
}

static qboolean VR_Check( XrResult res, const char *what ) {
	if ( XR_FAILED( res ) ) {
		Com_Printf( S_COLOR_YELLOW "VR: %s failed: %s\n", what, VR_ResultString( res ) );
		return qfalse;
	}
	return qtrue;
}

/*
================================================================================

Math helpers (OpenXR: +X right, +Y up, -Z forward  ->  Quake: +X forward, +Y left, +Z up)

================================================================================
*/

static void VR_XrRotate( const XrQuaternionf *q, const float v[3], float out[3] ) {
	const float tx = 2.0f * ( q->y * v[2] - q->z * v[1] );
	const float ty = 2.0f * ( q->z * v[0] - q->x * v[2] );
	const float tz = 2.0f * ( q->x * v[1] - q->y * v[0] );
	out[0] = v[0] + q->w * tx + ( q->y * tz - q->z * ty );
	out[1] = v[1] + q->w * ty + ( q->z * tx - q->x * tz );
	out[2] = v[2] + q->w * tz + ( q->x * ty - q->y * tx );
}

static void VR_XrToQuake( const float xr[3], vec3_t out ) {
	out[0] = -xr[2];
	out[1] = -xr[0];
	out[2] = xr[1];
}

static void VR_QuatToAxis( const XrQuaternionf *q, vec3_t axis[3] ) {
	static const float fwd[3] = { 0.0f, 0.0f, -1.0f };
	static const float left[3] = { -1.0f, 0.0f, 0.0f };
	static const float up[3] = { 0.0f, 1.0f, 0.0f };
	float t[3];

	VR_XrRotate( q, fwd, t );	VR_XrToQuake( t, axis[0] );
	VR_XrRotate( q, left, t );	VR_XrToQuake( t, axis[1] );
	VR_XrRotate( q, up, t );	VR_XrToQuake( t, axis[2] );
}

// (VR_AxisYaw lives in qcommon/vr_public.h, shared with the renderer and cgame)

// Heading of the forward axis, for hands: unlike VR_AxisYaw it doesn't flip
// when the wrist rolls past 90 degrees. Keeps the previous heading when
// pointing (almost) straight up or down.
static float VR_ForwardYaw( vec3_t axis[3], float prev ) {
	if ( fabsf( axis[0][2] ) > 0.99f ) {
		return prev;
	}
	return RAD2DEG( atan2f( axis[0][1], axis[0][0] ) );
}

static XrPosef VR_IdentityPose( void ) {
	XrPosef p;
	memset( &p, 0, sizeof( p ) );
	p.orientation.w = 1.0f;
	return p;
}

/*
================================================================================

Instance

================================================================================
*/

static qboolean VR_HasExtension( const char *name ) {
	uint32_t count = 0;
	if ( XR_FAILED( xrEnumerateInstanceExtensionProperties( NULL, 0, &count, NULL ) ) || !count ) {
		return qfalse;
	}
	XrExtensionProperties *props = (XrExtensionProperties *)Z_Malloc( sizeof( *props ) * count, TAG_CLIENTS, qtrue );
	for ( uint32_t i = 0; i < count; i++ ) {
		props[i].type = XR_TYPE_EXTENSION_PROPERTIES;
	}
	qboolean found = qfalse;
	if ( XR_SUCCEEDED( xrEnumerateInstanceExtensionProperties( NULL, count, &count, props ) ) ) {
		for ( uint32_t i = 0; i < count; i++ ) {
			if ( !strcmp( props[i].extensionName, name ) ) {
				found = qtrue;
				break;
			}
		}
	}
	Z_Free( props );
	return found;
}

static void VR_CreateInput( void );
static void VR_DestroyInput( void );

static void VR_DestroyInstance( void ) {
	VR_DestroyInput();
	if ( vr.instance != XR_NULL_HANDLE && xrDestroyInstance ) {
		xrDestroyInstance( vr.instance );
	}
	vr.instance = XR_NULL_HANDLE;
	vr.instanceTried = qfalse;
	vr.instanceLost = qfalse;
}

static qboolean VR_CreateInstance( void ) {
	if ( vr.instance != XR_NULL_HANDLE ) {
		return qtrue;
	}
	if ( vr.instanceTried ) {
		return qfalse;	// don't retry (and spam) on every vid_restart
	}
	vr.instanceTried = qtrue;

	if ( !VR_LoadLoader() ) {
		return qfalse;
	}

	if ( !VR_HasExtension( XR_KHR_OPENGL_ENABLE_EXTENSION_NAME ) ) {
		Com_Printf( S_COLOR_YELLOW "VR: no OpenXR runtime with OpenGL support was found.\n"
			"VR: Start SteamVR (or the Oculus/Meta app) and make it the active OpenXR runtime. Running in desktop mode.\n" );
		return qfalse;
	}

	// what the controllers sense of your fingers, beyond the core touch
	// sensors: Touch Plus (Quest 3) and Touch Pro read the index finger's curl
	// and sense the thumb and finger near; Touch, with the proximity extension,
	// senses them near - each where the runtime offers it
	const char *extensions[4];
	uint32_t numExtensions = 0;
	extensions[numExtensions++] = XR_KHR_OPENGL_ENABLE_EXTENSION_NAME;
	vr.extTouchPlus = VR_HasExtension( XR_META_TOUCH_CONTROLLER_PLUS_EXTENSION_NAME );
	vr.extTouchPro = VR_HasExtension( XR_FB_TOUCH_CONTROLLER_PRO_EXTENSION_NAME );
	vr.extProximity = VR_HasExtension( XR_FB_TOUCH_CONTROLLER_PROXIMITY_EXTENSION_NAME );
	if ( vr.extTouchPlus ) {
		extensions[numExtensions++] = XR_META_TOUCH_CONTROLLER_PLUS_EXTENSION_NAME;
	}
	if ( vr.extTouchPro ) {
		extensions[numExtensions++] = XR_FB_TOUCH_CONTROLLER_PRO_EXTENSION_NAME;
	}
	if ( vr.extProximity ) {
		extensions[numExtensions++] = XR_FB_TOUCH_CONTROLLER_PROXIMITY_EXTENSION_NAME;
	}

	XrInstanceCreateInfo ici;
	memset( &ici, 0, sizeof( ici ) );
	ici.type = XR_TYPE_INSTANCE_CREATE_INFO;
	Q_strncpyz( ici.applicationInfo.applicationName, "OpenJK VR", sizeof( ici.applicationInfo.applicationName ) );
	ici.applicationInfo.applicationVersion = 1;
	Q_strncpyz( ici.applicationInfo.engineName, "OpenJK", sizeof( ici.applicationInfo.engineName ) );
	ici.applicationInfo.engineVersion = 1;
#ifdef XR_API_VERSION_1_0
	ici.applicationInfo.apiVersion = XR_API_VERSION_1_0;	// widest runtime compatibility
#else
	ici.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
#endif
	ici.enabledExtensionCount = numExtensions;
	ici.enabledExtensionNames = extensions;

	XrResult res = xrCreateInstance( &ici, &vr.instance );
	if ( XR_FAILED( res ) && numExtensions > 1 ) {
		// (a runtime that lists an extension it then won't enable: without them)
		Com_Printf( S_COLOR_YELLOW "VR: xrCreateInstance with the controller extensions failed (%d); trying without\n", (int)res );
		vr.extTouchPlus = vr.extTouchPro = vr.extProximity = qfalse;
		ici.enabledExtensionCount = 1;
		vr.instance = XR_NULL_HANDLE;
		res = xrCreateInstance( &ici, &vr.instance );
	}
	if ( XR_FAILED( res ) ) {
		Com_Printf( S_COLOR_YELLOW "VR: xrCreateInstance failed (%d). Running in desktop mode.\n", (int)res );
		vr.instance = XR_NULL_HANDLE;
		return qfalse;
	}
	if ( !VR_LoadInstanceFunctions( vr.instance ) ) {
		VR_DestroyInstance();
		vr.instanceTried = qtrue;
		return qfalse;
	}

	XrInstanceProperties ip;
	memset( &ip, 0, sizeof( ip ) );
	ip.type = XR_TYPE_INSTANCE_PROPERTIES;
	if ( XR_SUCCEEDED( xrGetInstanceProperties( vr.instance, &ip ) ) ) {
		Q_strncpyz( vr.runtimeName, ip.runtimeName, sizeof( vr.runtimeName ) );
		Com_Printf( "VR: OpenXR runtime: %s %u.%u.%u\n", ip.runtimeName,
			XR_VERSION_MAJOR( ip.runtimeVersion ), XR_VERSION_MINOR( ip.runtimeVersion ), XR_VERSION_PATCH( ip.runtimeVersion ) );
	}
	if ( vr.extTouchPlus || vr.extTouchPro || vr.extProximity ) {
		Com_Printf( "VR: finger sensing beyond touch: %s%s%s\n", vr.extTouchPlus ? "Touch Plus " : "",
			vr.extTouchPro ? "Touch Pro " : "", vr.extProximity ? "Touch proximity" : "" );
	}

	XrSystemGetInfo sgi;
	memset( &sgi, 0, sizeof( sgi ) );
	sgi.type = XR_TYPE_SYSTEM_GET_INFO;
	sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	res = xrGetSystem( vr.instance, &sgi, &vr.systemId );
	if ( XR_FAILED( res ) ) {
		Com_Printf( S_COLOR_YELLOW "VR: no headset found (%s). Is it connected and switched on? Running in desktop mode.\n", VR_ResultString( res ) );
		VR_DestroyInstance();	// allows a retry on the next vid_restart
		return qfalse;
	}

	XrSystemProperties sp;
	memset( &sp, 0, sizeof( sp ) );
	sp.type = XR_TYPE_SYSTEM_PROPERTIES;
	if ( XR_SUCCEEDED( xrGetSystemProperties( vr.instance, vr.systemId, &sp ) ) ) {
		Q_strncpyz( vr.systemName, sp.systemName, sizeof( vr.systemName ) );
		Com_Printf( "VR: headset: %s\n", sp.systemName );
	}

	uint32_t viewCount = 0;
	if ( XR_FAILED( xrEnumerateViewConfigurationViews( vr.instance, vr.systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, NULL ) ) || viewCount != 2 ) {
		Com_Printf( S_COLOR_YELLOW "VR: headset does not offer a stereo view configuration. Running in desktop mode.\n" );
		VR_DestroyInstance();
		vr.instanceTried = qtrue;
		return qfalse;
	}
	vr.viewConfig[0].type = vr.viewConfig[1].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
	vr.viewConfig[0].next = vr.viewConfig[1].next = NULL;
	xrEnumerateViewConfigurationViews( vr.instance, vr.systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &viewCount, vr.viewConfig );

	VR_CreateInput();
	return qtrue;
}

/*
================================================================================

Render targets and swapchains

================================================================================
*/

static qboolean VR_CreateTarget( vrTarget_t *t, int width, int height ) {
	memset( t, 0, sizeof( *t ) );

	glGenTextures( 1, &t->color );
	glBindTexture( GL_TEXTURE_2D, t->color );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, VR_GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, VR_GL_CLAMP_TO_EDGE );
	glTexImage2D( GL_TEXTURE_2D, 0, VR_GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL );
	glBindTexture( GL_TEXTURE_2D, 0 );

	// stencil is needed for stencil shadows
	vglGenRenderbuffers( 1, &t->depthStencil );
	vglBindRenderbuffer( VR_GL_RENDERBUFFER, t->depthStencil );
	vglRenderbufferStorage( VR_GL_RENDERBUFFER, VR_GL_DEPTH24_STENCIL8, width, height );
	vglBindRenderbuffer( VR_GL_RENDERBUFFER, 0 );

	vglGenFramebuffers( 1, &t->fbo );
	vglBindFramebuffer( VR_GL_FRAMEBUFFER, t->fbo );
	vglFramebufferTexture2D( VR_GL_FRAMEBUFFER, VR_GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->color, 0 );
	vglFramebufferRenderbuffer( VR_GL_FRAMEBUFFER, VR_GL_DEPTH_STENCIL_ATTACHMENT, VR_GL_RENDERBUFFER, t->depthStencil );
	GLenum status = vglCheckFramebufferStatus( VR_GL_FRAMEBUFFER );
	vglBindFramebuffer( VR_GL_FRAMEBUFFER, 0 );
	if ( status != VR_GL_FRAMEBUFFER_COMPLETE ) {
		Com_Printf( S_COLOR_YELLOW "VR: eye framebuffer incomplete (0x%x)\n", status );
		return qfalse;
	}
	return qtrue;
}

static void VR_DestroyTarget( vrTarget_t *t ) {
	if ( t->fbo ) vglDeleteFramebuffers( 1, &t->fbo );
	if ( t->depthStencil ) vglDeleteRenderbuffers( 1, &t->depthStencil );
	if ( t->color ) glDeleteTextures( 1, &t->color );
	memset( t, 0, sizeof( *t ) );
}

static void VR_DestroyTargets( void ) {
	if ( !vr.targetsReady ) {
		return;
	}
	for ( int e = 0; e < 2; e++ ) {
		VR_DestroyTarget( &vr.targets[e] );
	}
	vr.targetsReady = qfalse;
}

static int64_t VR_ChooseColorFormat( void ) {
	uint32_t count = 0;
	if ( XR_FAILED( xrEnumerateSwapchainFormats( vr.session, 0, &count, NULL ) ) || !count ) {
		return 0;
	}
	int64_t *formats = (int64_t *)Z_Malloc( sizeof( int64_t ) * count, TAG_CLIENTS, qtrue );
	xrEnumerateSwapchainFormats( vr.session, count, &count, formats );

	// The engine renders gamma-space colours like it does for a monitor; an
	// sRGB swapchain filled with a raw copy makes the compositor show them
	// exactly as they look on the desktop.
	int64_t chosen = 0;
	const int64_t preferred[] = { VR_GL_SRGB8_ALPHA8, VR_GL_RGBA8 };
	for ( size_t p = 0; p < ARRAY_LEN( preferred ) && !chosen; p++ ) {
		for ( uint32_t i = 0; i < count; i++ ) {
			if ( formats[i] == preferred[p] ) {
				chosen = formats[i];
				break;
			}
		}
	}
	Z_Free( formats );
	return chosen;
}

static qboolean VR_CreateSwapchain( vrSwapchain_t *sc ) {
	memset( sc, 0, sizeof( *sc ) );

	XrSwapchainCreateInfo sci;
	memset( &sci, 0, sizeof( sci ) );
	sci.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
	sci.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
	sci.format = vr.colorFormat;
	sci.sampleCount = 1;
	sci.width = vr.width;
	sci.height = vr.height;
	sci.faceCount = 1;
	sci.arraySize = 1;
	sci.mipCount = 1;
	if ( !VR_Check( xrCreateSwapchain( vr.session, &sci, &sc->handle ), "xrCreateSwapchain" ) ) {
		sc->handle = XR_NULL_HANDLE;
		return qfalse;
	}

	if ( XR_FAILED( xrEnumerateSwapchainImages( sc->handle, 0, &sc->imageCount, NULL ) ) || !sc->imageCount ) {
		return qfalse;
	}
	sc->images = (XrSwapchainImageOpenGLKHR *)Z_Malloc( sizeof( XrSwapchainImageOpenGLKHR ) * sc->imageCount, TAG_CLIENTS, qtrue );
	for ( uint32_t i = 0; i < sc->imageCount; i++ ) {
		sc->images[i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
	}
	if ( !VR_Check( xrEnumerateSwapchainImages( sc->handle, sc->imageCount, &sc->imageCount, (XrSwapchainImageBaseHeader *)sc->images ), "xrEnumerateSwapchainImages" ) ) {
		return qfalse;
	}

	if ( !vglCopyImageSubData ) {
		// fallback: blit into the swapchain images through framebuffers
		sc->fbos = (GLuint *)Z_Malloc( sizeof( GLuint ) * sc->imageCount, TAG_CLIENTS, qtrue );
		vglGenFramebuffers( sc->imageCount, sc->fbos );
		for ( uint32_t i = 0; i < sc->imageCount; i++ ) {
			vglBindFramebuffer( VR_GL_FRAMEBUFFER, sc->fbos[i] );
			vglFramebufferTexture2D( VR_GL_FRAMEBUFFER, VR_GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sc->images[i].image, 0 );
		}
		vglBindFramebuffer( VR_GL_FRAMEBUFFER, 0 );
	}
	return qtrue;
}

static void VR_DestroySwapchain( vrSwapchain_t *sc ) {
	if ( sc->fbos ) {
		vglDeleteFramebuffers( sc->imageCount, sc->fbos );
		Z_Free( sc->fbos );
	}
	if ( sc->images ) {
		Z_Free( sc->images );
	}
	if ( sc->handle != XR_NULL_HANDLE ) {
		xrDestroySwapchain( sc->handle );
	}
	memset( sc, 0, sizeof( *sc ) );
}

// Copies a finished eye image into the swapchain. Acquire/wait/release all
// happen here, so a swapchain image is never left acquired.
static qboolean VR_CopyToSwapchain( int eye ) {
	vrSwapchain_t *sc = &vr.swapchains[eye];
	uint32_t index = 0;

	XrSwapchainImageAcquireInfo ai;
	memset( &ai, 0, sizeof( ai ) );
	ai.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;
	if ( !VR_Check( xrAcquireSwapchainImage( sc->handle, &ai, &index ), "xrAcquireSwapchainImage" ) ) {
		return qfalse;
	}

	XrSwapchainImageWaitInfo wi;
	memset( &wi, 0, sizeof( wi ) );
	wi.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
	wi.timeout = XR_INFINITE_DURATION;
	XrResult res = xrWaitSwapchainImage( sc->handle, &wi );
	qboolean ok = ( res == XR_SUCCESS ) ? qtrue : qfalse;

	if ( ok ) {
		if ( vglCopyImageSubData ) {
			// raw copy: RGBA8 and SRGB8_ALPHA8 share a view class, no conversion
			vglCopyImageSubData( vr.targets[eye].color, GL_TEXTURE_2D, 0, 0, 0, 0,
				sc->images[index].image, GL_TEXTURE_2D, 0, 0, 0, 0, vr.width, vr.height, 1 );
		} else {
			GLboolean scissor = glIsEnabled( GL_SCISSOR_TEST );
			glDisable( GL_SCISSOR_TEST );
			glDisable( VR_GL_FRAMEBUFFER_SRGB );
			vglBindFramebuffer( VR_GL_READ_FRAMEBUFFER, vr.targets[eye].fbo );
			vglBindFramebuffer( VR_GL_DRAW_FRAMEBUFFER, sc->fbos[index] );
			vglBlitFramebuffer( 0, 0, vr.width, vr.height, 0, 0, vr.width, vr.height, GL_COLOR_BUFFER_BIT, GL_NEAREST );
			vglBindFramebuffer( VR_GL_FRAMEBUFFER, 0 );
			if ( scissor ) {
				glEnable( GL_SCISSOR_TEST );
			}
		}
	} else {
		VR_Check( res, "xrWaitSwapchainImage" );
	}

	if ( ok ) {
		// (an image whose wait failed can't be released; the session is
		// being lost in that case anyway)
		XrSwapchainImageReleaseInfo ri;
		memset( &ri, 0, sizeof( ri ) );
		ri.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
		xrReleaseSwapchainImage( sc->handle, &ri );
	}
	return ok;
}


/*
================================================================================

Controllers

Actions and suggested bindings are created once per OpenXR instance (the spec
forbids suggesting bindings after any session attached them), then attached
to every session.

================================================================================
*/

static XrPath VR_Path( const char *str ) {
	XrPath p = XR_NULL_PATH;
	if ( XR_FAILED( xrStringToPath( vr.instance, str, &p ) ) ) {
		Com_Printf( S_COLOR_YELLOW "VR: bad OpenXR path %s\n", str );
	}
	return p;
}

static XrAction VR_CreateAction( const char *name, const char *localized, XrActionType type, qboolean perHand ) {
	XrActionCreateInfo aci;
	memset( &aci, 0, sizeof( aci ) );
	aci.type = XR_TYPE_ACTION_CREATE_INFO;
	aci.actionType = type;
	Q_strncpyz( aci.actionName, name, sizeof( aci.actionName ) );
	Q_strncpyz( aci.localizedActionName, localized, sizeof( aci.localizedActionName ) );
	if ( perHand ) {
		aci.countSubactionPaths = 2;
		aci.subactionPaths = vr.handPath;
	}
	XrAction action = XR_NULL_HANDLE;
	if ( !VR_Check( xrCreateAction( vr.actionSet, &aci, &action ), name ) ) {
		return XR_NULL_HANDLE;
	}
	return action;
}

typedef struct vrBinding_s {
	XrAction	*action;
	const char	*path;
} vrBinding_t;

#define VR_BOTH_HANDS(act, sub) { &act, "/user/hand/left/input/" sub }, { &act, "/user/hand/right/input/" sub }
#define VR_BOTH_HAPTICS(act) { &act, "/user/hand/left/output/haptic" }, { &act, "/user/hand/right/output/haptic" }

// One profile's bindings: the controls, and what senses your fingers. A
// runtime that rejects them (an input it doesn't know - one set for a
// profile is all or nothing) gets the controls alone.
#define VR_MAX_BINDINGS		64
static void VR_SuggestBindings( const char *profile, const vrBinding_t *bindings, int count,
	const vrBinding_t *fingers, int numFingers ) {
	XrActionSuggestedBinding sb[VR_MAX_BINDINGS];
	XrResult res = XR_SUCCESS;
	int n = 0;

	for ( int i = 0; i < count + numFingers && n < VR_MAX_BINDINGS; i++ ) {
		const vrBinding_t *b = ( i < count ) ? &bindings[i] : &fingers[i - count];
		if ( *b->action == XR_NULL_HANDLE ) {
			continue;
		}
		sb[n].action = *b->action;
		sb[n].binding = VR_Path( b->path );
		n++;
	}

	for ( int pass = 0; pass < 2; pass++ ) {
		XrInteractionProfileSuggestedBinding ipsb;
		memset( &ipsb, 0, sizeof( ipsb ) );
		ipsb.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
		ipsb.interactionProfile = VR_Path( profile );
		ipsb.countSuggestedBindings = n;
		ipsb.suggestedBindings = sb;
		res = xrSuggestInteractionProfileBindings( vr.instance, &ipsb );
		if ( XR_SUCCEEDED( res ) || !numFingers || pass ) {
			break;
		}
		Com_Printf( S_COLOR_YELLOW "VR: finger sensing for %s rejected (%s): the controls without it\n", profile,
			VR_ResultString( res ) );
		// (the controls come first: drop what senses your fingers)
		n = 0;
		for ( int i = 0; i < count && n < VR_MAX_BINDINGS; i++ ) {
			if ( *bindings[i].action != XR_NULL_HANDLE ) {
				sb[n].action = *bindings[i].action;
				sb[n].binding = VR_Path( bindings[i].path );
				n++;
			}
		}
	}
	if ( XR_FAILED( res ) ) {
		Com_Printf( S_COLOR_YELLOW "VR: controller bindings for %s rejected: %s\n", profile, VR_ResultString( res ) );
	}
}

static void VR_CreateInput( void ) {
	if ( vr.inputReady ) {
		return;
	}

	vr.handPath[VR_HAND_LEFT] = VR_Path( "/user/hand/left" );
	vr.handPath[VR_HAND_RIGHT] = VR_Path( "/user/hand/right" );

	XrActionSetCreateInfo asci;
	memset( &asci, 0, sizeof( asci ) );
	asci.type = XR_TYPE_ACTION_SET_CREATE_INFO;
	Q_strncpyz( asci.actionSetName, "gameplay", sizeof( asci.actionSetName ) );
	Q_strncpyz( asci.localizedActionSetName, "Gameplay", sizeof( asci.localizedActionSetName ) );
	asci.priority = 0;
	if ( !VR_Check( xrCreateActionSet( vr.instance, &asci, &vr.actionSet ), "xrCreateActionSet" ) ) {
		vr.actionSet = XR_NULL_HANDLE;
		return;
	}

	vr.actGripPose		= VR_CreateAction( "grip_pose",		"Hand grip pose",	XR_ACTION_TYPE_POSE_INPUT,		qtrue );
	vr.actAimPose		= VR_CreateAction( "aim_pose",		"Hand aim pose",	XR_ACTION_TYPE_POSE_INPUT,		qtrue );
	vr.actMove			= VR_CreateAction( "move",			"Move",				XR_ACTION_TYPE_VECTOR2F_INPUT,	qfalse );
	vr.actTurn			= VR_CreateAction( "turn",			"Turn / switch weapon", XR_ACTION_TYPE_VECTOR2F_INPUT, qfalse );
	vr.actTrigger		= VR_CreateAction( "trigger",		"Trigger",			XR_ACTION_TYPE_FLOAT_INPUT,		qtrue );
	vr.actSqueeze		= VR_CreateAction( "squeeze",		"Grip",				XR_ACTION_TYPE_FLOAT_INPUT,		qtrue );
	vr.actStickClick	= VR_CreateAction( "stick_click",	"Stick click",		XR_ACTION_TYPE_BOOLEAN_INPUT,	qtrue );
	vr.actA				= VR_CreateAction( "button_a",		"A (jump)",			XR_ACTION_TYPE_BOOLEAN_INPUT,	qfalse );
	vr.actB				= VR_CreateAction( "button_b",		"B (crouch)",		XR_ACTION_TYPE_BOOLEAN_INPUT,	qfalse );
	vr.actX				= VR_CreateAction( "button_x",		"X",				XR_ACTION_TYPE_BOOLEAN_INPUT,	qfalse );
	vr.actY				= VR_CreateAction( "button_y",		"Y",				XR_ACTION_TYPE_BOOLEAN_INPUT,	qfalse );
	vr.actMenu			= VR_CreateAction( "menu",			"Menu",				XR_ACTION_TYPE_BOOLEAN_INPUT,	qfalse );
	vr.actHaptic		= VR_CreateAction( "haptic",		"Vibration",		XR_ACTION_TYPE_VIBRATION_OUTPUT, qtrue );
	// what the controllers sense of your fingers (vrFingerSense_t)
	vr.actTriggerTouch	= VR_CreateAction( "trigger_touch",	"Finger on the trigger",	XR_ACTION_TYPE_BOOLEAN_INPUT,	qtrue );
	vr.actThumbTouch	= VR_CreateAction( "thumb_touch",	"Thumb on the controller",	XR_ACTION_TYPE_BOOLEAN_INPUT,	qtrue );
	vr.actTriggerNear	= VR_CreateAction( "trigger_near",	"Finger near the trigger",	XR_ACTION_TYPE_BOOLEAN_INPUT,	qtrue );
	vr.actThumbNear		= VR_CreateAction( "thumb_near",	"Thumb near the buttons",	XR_ACTION_TYPE_BOOLEAN_INPUT,	qtrue );
	vr.actTriggerCurl	= VR_CreateAction( "trigger_curl",	"Finger curled to the trigger", XR_ACTION_TYPE_FLOAT_INPUT,	qtrue );

	// Meta Quest (1/2/3/Pro) Touch and Touch Plus controllers (and, below,
	// the profiles that sense more of your fingers: the same controls)
	const vrBinding_t touch[] = {
		VR_BOTH_HAPTICS( vr.actHaptic ),
		VR_BOTH_HANDS( vr.actGripPose, "grip/pose" ),
		VR_BOTH_HANDS( vr.actAimPose, "aim/pose" ),
		{ &vr.actMove, "/user/hand/left/input/thumbstick" },
		{ &vr.actTurn, "/user/hand/right/input/thumbstick" },
		VR_BOTH_HANDS( vr.actTrigger, "trigger/value" ),
		VR_BOTH_HANDS( vr.actSqueeze, "squeeze/value" ),
		VR_BOTH_HANDS( vr.actStickClick, "thumbstick/click" ),
		{ &vr.actA, "/user/hand/right/input/a/click" },
		{ &vr.actB, "/user/hand/right/input/b/click" },
		{ &vr.actX, "/user/hand/left/input/x/click" },
		{ &vr.actY, "/user/hand/left/input/y/click" },
		{ &vr.actMenu, "/user/hand/left/input/menu/click" },
	};
	// your fingers: the trigger touched; the thumb on a button, the stick or
	// the thumb rest; (XR_FB_touch_controller_proximity, last) both near
	const vrBinding_t touchFingers[] = {
		VR_BOTH_HANDS( vr.actTriggerTouch, "trigger/touch" ),
		VR_BOTH_HANDS( vr.actThumbTouch, "thumbstick/touch" ),
		VR_BOTH_HANDS( vr.actThumbTouch, "thumbrest/touch" ),
		{ &vr.actThumbTouch, "/user/hand/left/input/x/touch" },
		{ &vr.actThumbTouch, "/user/hand/left/input/y/touch" },
		{ &vr.actThumbTouch, "/user/hand/right/input/a/touch" },
		{ &vr.actThumbTouch, "/user/hand/right/input/b/touch" },
		VR_BOTH_HANDS( vr.actTriggerNear, "trigger/proximity_fb" ),
		VR_BOTH_HANDS( vr.actThumbNear, "thumb_fb/proximity_fb" ),
	};
	VR_SuggestBindings( "/interaction_profiles/oculus/touch_controller", touch, ARRAY_LEN( touch ),
		touchFingers, ARRAY_LEN( touchFingers ) - ( vr.extProximity ? 0 : 4 ) );

	// Meta Quest 3 Touch Plus (XR_META_touch_controller_plus): the index
	// finger's curl as well, and both near
	const vrBinding_t touchPlusFingers[] = {
		VR_BOTH_HANDS( vr.actTriggerTouch, "trigger/touch" ),
		VR_BOTH_HANDS( vr.actThumbTouch, "thumbstick/touch" ),
		VR_BOTH_HANDS( vr.actThumbTouch, "thumbrest/touch" ),
		{ &vr.actThumbTouch, "/user/hand/left/input/x/touch" },
		{ &vr.actThumbTouch, "/user/hand/left/input/y/touch" },
		{ &vr.actThumbTouch, "/user/hand/right/input/a/touch" },
		{ &vr.actThumbTouch, "/user/hand/right/input/b/touch" },
		VR_BOTH_HANDS( vr.actTriggerNear, "trigger/proximity_meta" ),
		VR_BOTH_HANDS( vr.actThumbNear, "thumb_meta/proximity_meta" ),
		VR_BOTH_HANDS( vr.actTriggerCurl, "trigger/curl_meta" ),
	};
	if ( vr.extTouchPlus ) {
		VR_SuggestBindings( "/interaction_profiles/meta/touch_controller_plus", touch, ARRAY_LEN( touch ),
			touchPlusFingers, ARRAY_LEN( touchPlusFingers ) );
	}

	// Meta Quest Touch Pro (XR_FB_touch_controller_pro): the same
	const vrBinding_t touchProFingers[] = {
		VR_BOTH_HANDS( vr.actTriggerTouch, "trigger/touch" ),
		VR_BOTH_HANDS( vr.actThumbTouch, "thumbstick/touch" ),
		VR_BOTH_HANDS( vr.actThumbTouch, "thumbrest/touch" ),
		{ &vr.actThumbTouch, "/user/hand/left/input/x/touch" },
		{ &vr.actThumbTouch, "/user/hand/left/input/y/touch" },
		{ &vr.actThumbTouch, "/user/hand/right/input/a/touch" },
		{ &vr.actThumbTouch, "/user/hand/right/input/b/touch" },
		VR_BOTH_HANDS( vr.actTriggerNear, "trigger/proximity_fb" ),
		VR_BOTH_HANDS( vr.actThumbNear, "thumb_fb/proximity_fb" ),
		VR_BOTH_HANDS( vr.actTriggerCurl, "trigger/curl_fb" ),
	};
	if ( vr.extTouchPro ) {
		VR_SuggestBindings( "/interaction_profiles/facebook/touch_controller_pro", touch, ARRAY_LEN( touch ),
			touchProFingers, ARRAY_LEN( touchProFingers ) );
	}

	// Valve Index
	const vrBinding_t index[] = {
		VR_BOTH_HAPTICS( vr.actHaptic ),
		VR_BOTH_HANDS( vr.actGripPose, "grip/pose" ),
		VR_BOTH_HANDS( vr.actAimPose, "aim/pose" ),
		{ &vr.actMove, "/user/hand/left/input/thumbstick" },
		{ &vr.actTurn, "/user/hand/right/input/thumbstick" },
		VR_BOTH_HANDS( vr.actTrigger, "trigger/value" ),
		VR_BOTH_HANDS( vr.actSqueeze, "squeeze/value" ),
		VR_BOTH_HANDS( vr.actStickClick, "thumbstick/click" ),
		{ &vr.actA, "/user/hand/right/input/a/click" },
		{ &vr.actB, "/user/hand/right/input/b/click" },
		{ &vr.actX, "/user/hand/left/input/a/click" },
		{ &vr.actY, "/user/hand/left/input/b/click" },
		{ &vr.actMenu, "/user/hand/left/input/trackpad/force" },
	};
	const vrBinding_t indexFingers[] = {
		VR_BOTH_HANDS( vr.actTriggerTouch, "trigger/touch" ),
		VR_BOTH_HANDS( vr.actThumbTouch, "thumbstick/touch" ),
		VR_BOTH_HANDS( vr.actThumbTouch, "trackpad/touch" ),
		VR_BOTH_HANDS( vr.actThumbTouch, "a/touch" ),
		VR_BOTH_HANDS( vr.actThumbTouch, "b/touch" ),
	};
	VR_SuggestBindings( "/interaction_profiles/valve/index_controller", index, ARRAY_LEN( index ),
		indexFingers, ARRAY_LEN( indexFingers ) );

	// HTC Vive wands
	const vrBinding_t vive[] = {
		VR_BOTH_HAPTICS( vr.actHaptic ),
		VR_BOTH_HANDS( vr.actGripPose, "grip/pose" ),
		VR_BOTH_HANDS( vr.actAimPose, "aim/pose" ),
		{ &vr.actMove, "/user/hand/left/input/trackpad" },
		{ &vr.actTurn, "/user/hand/right/input/trackpad" },
		VR_BOTH_HANDS( vr.actTrigger, "trigger/value" ),
		VR_BOTH_HANDS( vr.actSqueeze, "squeeze/click" ),
		VR_BOTH_HANDS( vr.actStickClick, "trackpad/click" ),
		{ &vr.actA, "/user/hand/right/input/menu/click" },
		{ &vr.actMenu, "/user/hand/left/input/menu/click" },
	};
	const vrBinding_t viveFingers[] = {
		VR_BOTH_HANDS( vr.actThumbTouch, "trackpad/touch" ),
	};
	VR_SuggestBindings( "/interaction_profiles/htc/vive_controller", vive, ARRAY_LEN( vive ),
		viveFingers, ARRAY_LEN( viveFingers ) );

	// Windows Mixed Reality
	const vrBinding_t wmr[] = {
		VR_BOTH_HAPTICS( vr.actHaptic ),
		VR_BOTH_HANDS( vr.actGripPose, "grip/pose" ),
		VR_BOTH_HANDS( vr.actAimPose, "aim/pose" ),
		{ &vr.actMove, "/user/hand/left/input/thumbstick" },
		{ &vr.actTurn, "/user/hand/right/input/thumbstick" },
		VR_BOTH_HANDS( vr.actTrigger, "trigger/value" ),
		VR_BOTH_HANDS( vr.actSqueeze, "squeeze/click" ),
		VR_BOTH_HANDS( vr.actStickClick, "thumbstick/click" ),
		{ &vr.actA, "/user/hand/right/input/trackpad/click" },
		{ &vr.actB, "/user/hand/right/input/menu/click" },
		{ &vr.actX, "/user/hand/left/input/trackpad/click" },
		{ &vr.actMenu, "/user/hand/left/input/menu/click" },
	};
	// (its trackpad is the only thing that feels the thumb, which mostly
	// rests on the stick: it can't tell where your fingers are)
	VR_SuggestBindings( "/interaction_profiles/microsoft/motion_controller", wmr, ARRAY_LEN( wmr ), NULL, 0 );

	// Fallback for anything else
	const vrBinding_t simple[] = {
		VR_BOTH_HAPTICS( vr.actHaptic ),
		VR_BOTH_HANDS( vr.actGripPose, "grip/pose" ),
		VR_BOTH_HANDS( vr.actAimPose, "aim/pose" ),
		VR_BOTH_HANDS( vr.actTrigger, "select/click" ),
		{ &vr.actMenu, "/user/hand/left/input/menu/click" },
	};
	VR_SuggestBindings( "/interaction_profiles/khr/simple_controller", simple, ARRAY_LEN( simple ), NULL, 0 );

	vr.inputReady = qtrue;
}

static void VR_DestroyInput( void ) {
	if ( vr.actionSet != XR_NULL_HANDLE && xrDestroyActionSet ) {
		xrDestroyActionSet( vr.actionSet );	// also destroys its actions
	}
	vr.actionSet = XR_NULL_HANDLE;
	vr.actGripPose = vr.actAimPose = vr.actMove = vr.actTurn = XR_NULL_HANDLE;
	vr.actTrigger = vr.actSqueeze = vr.actStickClick = XR_NULL_HANDLE;
	vr.actA = vr.actB = vr.actX = vr.actY = vr.actMenu = XR_NULL_HANDLE;
	vr.actHaptic = XR_NULL_HANDLE;
	vr.actTriggerTouch = vr.actThumbTouch = vr.actTriggerNear = vr.actThumbNear = vr.actTriggerCurl = XR_NULL_HANDLE;
	memset( vr.fingers, 0, sizeof( vr.fingers ) );
	vr.inputReady = qfalse;
}

static void VR_AttachInput( void ) {
	vr.inputAttached = qfalse;
	if ( !vr.inputReady || vr.session == XR_NULL_HANDLE ) {
		return;
	}

	XrSessionActionSetsAttachInfo ai;
	memset( &ai, 0, sizeof( ai ) );
	ai.type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO;
	ai.countActionSets = 1;
	ai.actionSets = &vr.actionSet;
	if ( !VR_Check( xrAttachSessionActionSets( vr.session, &ai ), "xrAttachSessionActionSets" ) ) {
		Com_Printf( S_COLOR_YELLOW "VR: controllers unavailable this session (restart the game)\n" );
		return;
	}

	for ( int h = 0; h < 2; h++ ) {
		XrActionSpaceCreateInfo asci;
		memset( &asci, 0, sizeof( asci ) );
		asci.type = XR_TYPE_ACTION_SPACE_CREATE_INFO;
		asci.subactionPath = vr.handPath[h];
		asci.poseInActionSpace = VR_IdentityPose();
		asci.action = vr.actGripPose;
		if ( !VR_Check( xrCreateActionSpace( vr.session, &asci, &vr.gripSpace[h] ), "xrCreateActionSpace(grip)" ) ) {
			vr.gripSpace[h] = XR_NULL_HANDLE;
		}
		asci.action = vr.actAimPose;
		if ( !VR_Check( xrCreateActionSpace( vr.session, &asci, &vr.aimSpace[h] ), "xrCreateActionSpace(aim)" ) ) {
			vr.aimSpace[h] = XR_NULL_HANDLE;
		}
	}
	vr.inputAttached = qtrue;
}

static void VR_DestroyInputSpaces( void ) {
	for ( int h = 0; h < 2; h++ ) {
		if ( vr.gripSpace[h] != XR_NULL_HANDLE ) {
			xrDestroySpace( vr.gripSpace[h] );
			vr.gripSpace[h] = XR_NULL_HANDLE;
		}
		if ( vr.aimSpace[h] != XR_NULL_HANDLE ) {
			xrDestroySpace( vr.aimSpace[h] );
			vr.aimSpace[h] = XR_NULL_HANDLE;
		}
		vr.handValid[h] = qfalse;
		vr.profile[h][0] = 0;
	}
	memset( vr.fingers, 0, sizeof( vr.fingers ) );
	vr.inputAttached = qfalse;
}

// A short buzz in one hand.
static void VR_Pulse( int hand, float seconds, float amplitude ) {
	if ( !vr.inputAttached || vr.actHaptic == XR_NULL_HANDLE ) {
		return;
	}
	XrHapticActionInfo hai;
	memset( &hai, 0, sizeof( hai ) );
	hai.type = XR_TYPE_HAPTIC_ACTION_INFO;
	hai.action = vr.actHaptic;
	hai.subactionPath = vr.handPath[hand];
	XrHapticVibration vib;
	memset( &vib, 0, sizeof( vib ) );
	vib.type = XR_TYPE_HAPTIC_VIBRATION;
	vib.duration = (XrDuration)( seconds * 1.0e9f );
	vib.frequency = XR_FREQUENCY_UNSPECIFIED;
	vib.amplitude = amplitude;
	xrApplyHapticFeedback( vr.session, &hai, (const XrHapticBaseHeader *)&vib );
}

// A controller trigger is holding a mouse button down on the menu screen (a
// click, a right click, or dragging a slider or scroll bar).
static qboolean VR_Clicking( void ) {
	for ( int k = 0; k < VR_NUM_BUTTONS; k++ ) {
		if ( vr.keySent[k] == A_MOUSE1 || vr.keySent[k] == A_MOUSE2 ) {
			return qtrue;
		}
	}
	return qfalse;
}

// The key a controller button sends when pressed: its own bindable key, or
// while the menu screen is up, what the mouse/keyboard would do there.
// 0 = nothing.
static int VR_ButtonKeyCode( int key ) {
	if ( key == VR_BUTTON_MENU ) {
		return A_ESCAPE;	// opens/closes the menu like on the desktop
	}
	if ( !vr.pointerMode ) {
		return VR_FIRST_KEY + key;
	}
	if ( key == VR_KEY_TRIGGER_R || key == VR_KEY_TRIGGER_L ) {
		// where the controller points (pointing past the screen, nothing):
		// the pointing hand's trigger clicks, the other's right-clicks - a
		// point off a Force power, a choice stepped back (a button: clicked)
		if ( !vr.pointerHit ) {
			return 0;
		}
		return ( vr.keyHand[key] == vr.pointerLastHand ) ? A_MOUSE1 : A_MOUSE2;
	}
	if ( VR_Clicking() ) {
		return 0;	// any other key would end the drag under the held trigger
	}
	if ( key == VR_KEY_B ) {
		// back, like Escape (but not on the engine's download prompt, where
		// Escape disconnects)
		return cls.cursorActive ? 0 : A_ESCAPE;
	}
	return VR_FIRST_KEY + key;	// the rest stay bindable in the Controls menu
}

// The hand whose grip presses this key (VR_HandControl), or -1: not a grip's.
static int VR_GripHand( int key ) {
	if ( key != VR_KEY_GRIP_L && key != VR_KEY_GRIP_R ) {
		return -1;
	}
	return ( vr.keyHand[key] == VR_HAND_LEFT ) ? VR_HAND_LEFT : VR_HAND_RIGHT;
}

static int VR_ForceHand( void );
static void VR_ForceTapRelease( qboolean now );
static void VR_ForceDrop( void );

// Whether hand h's grip takes hold of something if squeezed now: the cgame
// said so lately (VR_SetGrabZones), in a game, out of the menu.
static qboolean VR_GrabZone( int h ) {
	return (qboolean)( !vr.pointerMode && cls.state == CA_ACTIVE && ( vr.grabZones & ( 1 << h ) ) &&
		cls.realtime - vr.grabZoneTime >= 0 && cls.realtime - vr.grabZoneTime < 200 );
}

static void VR_SetKey( int key, qboolean down ) {
	const int gripHand = VR_GripHand( key );

	if ( vr.keyBlocked[key] ) {
		// held through a focus loss: only letting go counts
		if ( !down ) {
			vr.keyBlocked[key] = qfalse;
		}
		return;
	}
	if ( vr.keyDown[key] == down ) {
		return;
	}
	vr.keyDown[key] = down;

	if ( gripHand >= 0 ) {
		// A grip squeezed where the cgame says the hand can take hold of
		// something (the other hand's saber hilt) grabs it instead of
		// pressing its key; letting go lets go.
		if ( !down && vr.grabbing[gripHand] ) {
			vr.grabbing[gripHand] = qfalse;
			return;
		}
		if ( down && VR_GrabZone( gripHand ) ) {
			vr.grabbing[gripHand] = qtrue;
			vr.keySent[key] = 0;
			return;
		}
	}
	if ( key == VR_KEY_GRIP_L ) {
		// The Force grip of the Force hand reaches out with the Force (the
		// gestures: VR_ForcePoll) instead of pressing its key, till it's let go.
		if ( !down && vr.forceGrip ) {
			vr.forceGrip = vr.forceGripDead = qfalse;
			return;
		}
		if ( down && gripHand >= 0 && gripHand == VR_ForceHand() ) {
			vr.forceGrip = qtrue;
			vr.forceGripDead = qfalse;
			vr.forceGripHand = gripHand;
			vr.keySent[key] = 0;
			return;
		}
		if ( down ) {
			VR_ForceTapRelease( qtrue );	// (a quick squeeze's Use Force still held: let go of it first)
		}
	}

	if ( !down ) {
		if ( key == VR_KEY_TRIGGER_R || key == VR_KEY_TRIGGER_L ) {
			VRK_Release();	// (Backspace stops repeating)
		}
		// the release goes wherever the press went, even if the menu
		// opened or closed in between
		if ( vr.keySent[key] ) {
			VR_QueueKey( vr.keySent[key], qfalse );
			vr.keySent[key] = 0;
		}
		return;
	}

	if ( ( key == VR_KEY_TRIGGER_R || key == VR_KEY_TRIGGER_L ) && vr.pointerMode && vr.pointerHit &&
		!VR_Clicking() && VRK_Press( vr.pointerX, vr.pointerY ) ) {
		vr.keySent[key] = 0;	// typed on the keyboard (either trigger): no click for what's under it
		VR_Pulse( vr.keyHand[key], 0.02f, 0.5f );
		return;
	}
	int code = VR_ButtonKeyCode( key );
	if ( ( code == A_MOUSE1 || code == A_MOUSE2 ) && VR_Clicking() ) {
		code = 0;	// the other trigger's holding its click (dragging a slider): nothing till let go
	}
	for ( int k = 0; k < VR_NUM_BUTTONS && code; k++ ) {
		if ( k != key && vr.keySent[k] == code ) {
			code = 0;	// another held button already sent it
		}
	}
	vr.keySent[key] = code;
	if ( !code ) {
		return;
	}
	VR_QueueKey( code, qtrue );
	if ( code == A_MOUSE1 || code == A_MOUSE2 ) {
		vr.pointerForce = qtrue;	// click exactly where it points, even if the mouse moved
		VR_Pulse( vr.keyHand[key], 0.02f, 0.5f );	// (in the hand that clicked)
	}
}

static void VR_TapKey( int key ) {
	VR_SetKey( key, qtrue );
	VR_SetKey( key, qfalse );
}

/*
Y: tapped, it's what it's bound to (the next Force power), as it's let go;
held, it's the pose wheel (the cgame's +posewheel) until it's let go -
meanwhile the right stick picks in the wheel. Bound to something that's
held itself (a +command), or in the menu, it's an ordinary button.
*/
#define VR_WHEEL_HOLD_MS	300		// held this long: the wheel
enum {
	VR_WHEEL_IDLE,
	VR_WHEEL_PENDING,		// pressed: a tap, or the wheel?
	VR_WHEEL_OPEN
};

struct cmd_function_s *Cmd_FindCommand( const char *cmd_name );	// (cmd.cpp)

static qboolean VR_YIsPlain( void ) {
	const char *b;

	if ( vr.pointerMode || cls.state != CA_ACTIVE || !cls.cgameStarted || !Cmd_FindCommand( "+posewheel" ) ) {
		return qtrue;	// (in the menu, not in a game, a game without the wheel)
	}
	b = Key_GetBinding( VR_FIRST_KEY + VR_KEY_Y );
	return ( b && b[0] == '+' ) ? qtrue : qfalse;
}

// The hand whose stick moves you (the left; vr_swapSticks: the right - the
// other's turns you and switches weapon). The sticks' clicks go with them:
// "left stick click" is the moving stick's, whichever hand it's in.
static int VR_MoveStickHand( void ) {
	return ( vr_swapSticks && vr_swapSticks->integer ) ? VR_HAND_RIGHT : VR_HAND_LEFT;
}

static int VR_TurnStickHand( void ) {
	return ( VR_MoveStickHand() == VR_HAND_LEFT ) ? VR_HAND_RIGHT : VR_HAND_LEFT;
}

// The wheel shut - what it points at taken (the cgame's -posewheel), or not
// (the menu came up over it, the headset came off) - or a press that hadn't
// decided dropped. Y counts again once it's let go. The right stick, still
// held as it shuts, waits to come back before it does what it does: turning
// (and switching weapon) - or, Swap Sticks, moving you (a pose picked with
// it walked you straight out of it).
static void VR_DropWheel( qboolean take ) {
	const qboolean wasOpen = (qboolean)( vr.wheelState == VR_WHEEL_OPEN );

	if ( wasOpen && cls.cgameStarted ) {
		Cbuf_AddText( take ? "-posewheel\n" : "-posewheel cancel\n" );
	}
	if ( vr.wheelState != VR_WHEEL_IDLE ) {
		vr.keyBlocked[VR_KEY_Y] = qtrue;
	}
	vr.wheelState = VR_WHEEL_IDLE;
	if ( VR_TurnStickHand() == VR_HAND_RIGHT ) {
		vr.turnLatched = vr.weapLatched = qtrue;
	} else if ( wasOpen ) {
		vr.moveLatched = qtrue;
	}
}

static void VR_WheelButton( qboolean down ) {
	if ( vr.wheelState == VR_WHEEL_IDLE ) {
		if ( !down || vr.keyDown[VR_KEY_Y] || vr.keyBlocked[VR_KEY_Y] || VR_YIsPlain() ) {
			VR_SetKey( VR_KEY_Y, down );	// (an ordinary button)
			return;
		}
		vr.wheelState = VR_WHEEL_PENDING;
		vr.wheelPressTime = Sys_Milliseconds();
		return;
	}
	if ( vr.pointerMode || cls.state != CA_ACTIVE ) {
		VR_DropWheel( qfalse );	// (the menu came up over it)
		if ( !down ) {
			vr.keyBlocked[VR_KEY_Y] = qfalse;
		}
		return;
	}
	if ( down ) {
		if ( vr.wheelState == VR_WHEEL_PENDING && Sys_Milliseconds() - vr.wheelPressTime >= VR_WHEEL_HOLD_MS ) {
			vr.wheelState = VR_WHEEL_OPEN;
			Cbuf_AddText( "+posewheel\n" );
		}
		return;
	}
	if ( vr.wheelState == VR_WHEEL_PENDING ) {
		vr.wheelState = VR_WHEEL_IDLE;
		VR_TapKey( VR_KEY_Y );	// a tap: what Y is bound to
		return;
	}
	VR_DropWheel( qtrue );
	vr.keyBlocked[VR_KEY_Y] = qfalse;	// (let go already)
}

// Lets go of every held controller button's key (focus lost, session
// ended). Whatever is still held when input comes back is not a new press. A
// grab - a hilt held - goes on till the grip's let go, input back: a saber
// held by its grip isn't let go of for a system menu.
static void VR_ReleaseKeys( void ) {
	for ( int k = 0; k < VR_NUM_BUTTONS; k++ ) {
		const int gripHand = VR_GripHand( k );
		if ( vr.keyDown[k] && !( gripHand >= 0 && vr.grabbing[gripHand] ) ) {
			VR_SetKey( k, qfalse );
			vr.keyBlocked[k] = qtrue;
		}
	}
}

static void VR_ReleaseAllKeys( void ) {
	VR_DropWheel( qfalse );
	VR_ForceDrop();		// (first: the Force grip let go of now isn't a quick squeeze)
	VR_ReleaseKeys();
	vr.moveX = vr.moveY = vr.turnX = vr.turnY = 0.0f;
	memset( vr.stick, 0, sizeof( vr.stick ) );
	// snap turns, weapon switches and menu scrolling wait for their stick to recentre
	vr.turnLatched = vr.weapLatched = vr.scrollLatched = qtrue;
	vr.pointerHit = qfalse;
	vr.pressing = qfalse;
	for ( int h = 0; h < 2; h++ ) {
		vr.pullArmed[h] = vr.pulling[h] = qfalse;
	}
	vr.scrollDir = 0;
}

/*
================================================================================

Menu pointer

While the menu screen is up, a controller points at it like a mouse - your
main hand, or the other while the main one's aimed away (VR_PointingHand):
its trigger clicks, the other trigger right-clicks, B goes back, either
stick scrolls. The UI module only knows relative mouse moves, so the engine
keeps a copy of its cursor (VR_UICursorMoved) and moves it by the
difference.

================================================================================
*/

static qboolean VR_UseScreenLayer( void );
static int VR_ControlKey( int control, int h );
static int VR_GunHand( void );

// The UI module's cursor moves only by relative mouse moves, clamped to the
// 640x480 menu like UI_MouseEvent does. Every move goes through
// UIVM_MouseEvent, which reports it here.
void VR_UICursorMoved( int dx, int dy ) {
	vr.uiCursorX = Com_Clampi( 0, SCREEN_WIDTH, vr.uiCursorX + dx );
	vr.uiCursorY = Com_Clampi( 0, SCREEN_HEIGHT, vr.uiCursorY + dy );
}

// Whether a controller drives the menu cursor right now: a menu (or the
// engine's download prompt) is on the menu screen, and the console isn't;
// or there's typing to do on the keyboard.
static qboolean VR_MenuPointerWanted( void ) {
	if ( !vr.lastScreenLayer || !VR_UseScreenLayer() ) {
		return qfalse;	// no menu screen (or it hasn't been placed yet)
	}
	if ( VRK_Wanted() ) {
		return qtrue;
	}
	const int catcher = Key_GetCatcher();
	if ( catcher & KEYCATCH_CONSOLE ) {
		return qfalse;
	}
	if ( cls.cursorActive ) {
		return qtrue;
	}
	return ( cls.uiStarted && ( catcher & KEYCATCH_UI ) ) ? qtrue : qfalse;
}

// Where a hand's pointing ray meets the menu screen, in menu units (0..640,
// 0..480 from the top left). The screen is the quad VR_EndFrame submits;
// both poses are in the local space.
static qboolean VR_PointerHit( int hand, float *x, float *y ) {
	static const float fwd[3] = { 0.0f, 0.0f, -1.0f };
	static const float ex[3] = { 1.0f, 0.0f, 0.0f };
	static const float ey[3] = { 0.0f, 1.0f, 0.0f };
	static const float ez[3] = { 0.0f, 0.0f, 1.0f };
	const XrPosef *aim = &vr.aimPose[hand];
	const XrPosef *quad = &vr.quadPose;
	float dir[3], right[3], up[3], normal[3];

	VR_XrRotate( &aim->orientation, fwd, dir );
	VR_XrRotate( &quad->orientation, ex, right );
	VR_XrRotate( &quad->orientation, ey, up );
	VR_XrRotate( &quad->orientation, ez, normal );	// the screen's visible side faces +Z

	const float toQuad[3] = {
		quad->position.x - aim->position.x,
		quad->position.y - aim->position.y,
		quad->position.z - aim->position.z
	};
	const float facing = DotProduct( dir, normal );
	if ( facing > -0.01f ) {
		return qfalse;	// pointing away from the screen, or along it
	}
	const float t = DotProduct( toQuad, normal ) / facing;
	if ( t <= 0.0f ) {
		return qfalse;	// the screen is behind the hand
	}
	const float rel[3] = {	// hit point relative to the screen's centre
		dir[0] * t - toQuad[0],
		dir[1] * t - toQuad[1],
		dir[2] * t - toQuad[2]
	};
	const float h = vr.quadHeight > 0.0f ? vr.quadHeight : Com_Clamp( 0.2f, 20.0f, vr_menuHeight->value );	// as VR_EndFrame sizes it
	const float u = DotProduct( rel, right ) / ( h * ( 4.0f / 3.0f ) ) + 0.5f;
	const float v = 0.5f - DotProduct( rel, up ) / h;
	const float margin = 0.04f;	// just past the edge still reaches the buttons along it
	if ( u < -margin || u > 1.0f + margin || v < -margin || v > 1.0f + margin ) {
		return qfalse;
	}
	*x = Com_Clamp( 0.0f, (float)SCREEN_WIDTH, u * SCREEN_WIDTH );
	*y = Com_Clamp( 0.0f, (float)SCREEN_HEIGHT, v * SCREEN_HEIGHT );
	return qtrue;
}

// A trigger that crosses this clicks (VR_HandControl) - and a grip takes hold
// of something there; for Use and the Force it's pressed further
// (vr_gripPress, up to VR_GRIP_PRESS_MAX).
#define VR_TRIGGER_PRESS		0.6f
#define VR_GRIP_PRESS_DEFAULT	0.85f
#define VR_GRIP_PRESS_MAX		0.95f
// A grip held as a grab that holds (a saber held by its grip: the cgame's,
// VR_GRAB_HOLDS) is let go of only under this - others under 0.4: a hand
// holding a saber needn't keep squeezing hard.
#define VR_GRAB_LET_GO		0.25f

// Where the menu cursor goes this frame. Runs before the buttons are read,
// which click where it points.
// Whether hand h's trigger is up - its own: the other hand may be holding
// down the key this one would press (held since before the menu came up).
static qboolean VR_TriggerUp( int h ) {
	const int key = VR_ControlKey( VR_CTL_TRIGGER, h );	// (-1: none - your hands empty, out of the menu)

	return (qboolean)( !vr.ctlDown[VR_CTL_TRIGGER][h] && ( key < 0 || !vr.keyBlocked[key] ) );
}

#define VR_POINTER_HANDOVER_MS	250	// a hand aimed at the menu screen (or away) this long: the pointer goes over

// Which hand points at the menu screen this poll, from whether each is aimed
// at it (aimed[h]): your main hand (the right; the left, left-handed) - or
// the other, while the main hand's aimed away (a moment: running off the
// screen's edge doesn't hand it over) and the other's aimed at it, until the
// main hand's aimed at it again (a moment - at once, the other aimed away).
// Its trigger clicks, the other's right-clicks. Never handed over while a
// trigger holds its click (a slider being dragged).
static int VR_PointingHand( const qboolean aimed[2], int now ) {
	const int main = VR_GunHand(), off = ( main == VR_HAND_LEFT ) ? VR_HAND_RIGHT : VR_HAND_LEFT;
	int hand = vr.pointerLeft ? VR_HAND_LEFT : VR_HAND_RIGHT;

	for ( int h = 0; h < 2; h++ ) {
		if ( aimed[h] && !vr.aimed[h] ) {
			vr.aimSince[h] = now;
		}
		if ( aimed[h] ) {
			vr.aimLast[h] = now;
		}
		vr.aimed[h] = aimed[h];
	}
	if ( !VR_Clicking() ) {
		if ( hand == main ) {
			if ( aimed[off] && !aimed[main] && now - vr.aimLast[main] >= VR_POINTER_HANDOVER_MS ) {
				hand = off;
			}
		} else if ( aimed[main] && ( !aimed[off] || now - vr.aimSince[main] >= VR_POINTER_HANDOVER_MS ) ) {
			hand = main;
		}
	}
	vr.pointerLeft = (qboolean)( hand == VR_HAND_LEFT );
	return hand;
}

static void VR_UpdateMenuPointer( const float trigger[2], qboolean wanted ) {
	const qboolean wasOnScreen = (qboolean)( vr.pointerMode && vr.pointerHit );
	if ( wanted && !vr.pointerMode ) {
		vr.scrollLatched = qtrue;	// a stick already pushed (walking) doesn't scroll
		vr.pointerLeft = (qboolean)( VR_GunHand() == VR_HAND_LEFT );	// (your main hand points first)
	}
	vr.pointerMode = wanted;
	vr.pointerHit = qfalse;
	vr.pointerFresh = qtrue;
	vr.pressing = qfalse;
	if ( !vr.pointerMode ) {
		for ( int h = 0; h < 2; h++ ) {
			vr.pullArmed[h] = vr.pulling[h] = qfalse;
			vr.aimed[h] = qfalse;
		}
		return;
	}

	// Pulling a trigger tips the controller a little, so from the moment a
	// pull starts (a rise from rest) until it clicks, at most 300 ms, the
	// cursor holds still.
	const int now = Sys_Milliseconds();
	qboolean pull[2];
	for ( int h = 0; h < 2; h++ ) {
		const qboolean up = VR_TriggerUp( h );
		pull[h] = (qboolean)( up && trigger[h] > VR_TRIGGER_PRESS );	// clicks this poll
		if ( !up ) {
			vr.pullArmed[h] = vr.pulling[h] = qfalse;
		} else if ( trigger[h] < 0.15f ) {
			vr.pullArmed[h] = qtrue;
			vr.pulling[h] = qfalse;
		} else if ( vr.pullArmed[h] ) {
			vr.pullArmed[h] = qfalse;
			vr.pulling[h] = qtrue;
			vr.pullStart[h] = now;
		}
	}
	vr.pressing = (qboolean)( pull[VR_HAND_LEFT] || pull[VR_HAND_RIGHT] );

	// The pointing hand (VR_PointingHand). Aimed away - or its tracking lost
	// a moment - the cursor holds where it is, and a trigger clicks nothing.
	float ax[2], ay[2];
	qboolean aimed[2];
	for ( int h = 0; h < 2; h++ ) {
		aimed[h] = (qboolean)( vr.handValid[h] && VR_PointerHit( h, &ax[h], &ay[h] ) );
	}
	const int hand = VR_PointingHand( aimed, now );
	if ( !aimed[hand] ) {
		return;
	}
	const qboolean hold = (qboolean)( vr.pulling[hand] && now - vr.pullStart[hand] < 300 );
	const float x = ax[hand], y = ay[hand];
	vr.pointerHit = qtrue;
	if ( !wasOnScreen || hand != vr.pointerLastHand ) {
		vr.pointerX = x;	// (re)starts pointing: jump there
		vr.pointerY = y;
		vr.pointerForce = qtrue;
	} else if ( !hold ) {
		// steady the cursor: hand tremor is smoothed away, real moves aren't
		const float dx = x - vr.pointerX, dy = y - vr.pointerY;
		const float follow = Com_Clamp( 0.25f, 1.0f, sqrtf( dx * dx + dy * dy ) / 8.0f );
		vr.pointerX += dx * follow;
		vr.pointerY += dy * follow;
	}
	vr.pointerLastHand = hand;
}

// Puts the menu cursor where the controller points. Called at the top of the
// client frame (never from inside a module), after the poll that aimed it.
static void VR_ApplyMenuPointer( void ) {
	if ( !vr.pointerFresh ) {
		return;	// nothing was polled this frame
	}
	vr.pointerFresh = qfalse;
	if ( !vr.pointerMode || !vr.pointerHit ) {
		return;
	}
	const int x = (int)( vr.pointerX + 0.5f );
	const int y = (int)( vr.pointerY + 0.5f );
	if ( x == vr.pointerSentX && y == vr.pointerSentY && !vr.pointerForce ) {
		return;	// the controller is still: leave the cursor to the mouse
	}
	vr.pointerSentX = x;
	vr.pointerSentY = y;
	vr.pointerForce = qfalse;

	if ( cls.cursorActive ) {
		// the engine's own prompt (downloads) keeps an absolute cursor
		cls.cursorX = x;
		cls.cursorY = y;
		return;
	}
	const int catcher = Key_GetCatcher();
	if ( !cls.uiStarted || !( catcher & KEYCATCH_UI ) || ( catcher & KEYCATCH_CONSOLE ) ) {
		return;
	}
	const int dx = x - vr.uiCursorX, dy = y - vr.uiCursorY;
	if ( dx || dy ) {
		UIVM_MouseEvent( dx, dy );	// VR_UICursorMoved follows it
	}
}

// Each hand's stick (vr.stick: the cgame has them so, VR_HAND_RIGHT's picking
// in the pose wheel), and the moving and turning sticks from them.
static void VR_SetSticks( const float left[2], const float right[2] ) {
	vr.stick[VR_HAND_LEFT][0] = left[0];
	vr.stick[VR_HAND_LEFT][1] = left[1];
	vr.stick[VR_HAND_RIGHT][0] = right[0];
	vr.stick[VR_HAND_RIGHT][1] = right[1];
	vr.moveX = vr.stick[VR_MoveStickHand()][0];
	vr.moveY = vr.stick[VR_MoveStickHand()][1];
	vr.turnX = vr.stick[VR_TurnStickHand()][0];
	vr.turnY = vr.stick[VR_TurnStickHand()][1];
}

// A stick held up or down in the menu turns the mouse wheel: one step, a
// pause, then repeating. (stickX/Y: whichever stick is pushed further; hand:
// the hand it's in, for the pulse.)
static void VR_MenuScroll( float stickX, float stickY, int hand ) {
	const float ax = fabsf( stickX ), ay = fabsf( stickY );
	if ( vr.scrollLatched ) {
		// pushed before the menu came up: wait for it to come back first
		if ( ay < 0.3f ) {
			vr.scrollLatched = qfalse;
		}
		vr.scrollDir = 0;
		return;
	}

	int dir = vr.scrollDir;
	if ( ay < 0.4f || ay < ax * ( vr.scrollDir ? 0.6f : 1.0f ) ) {
		dir = 0;	// centred, or pushed sideways (with some slack once scrolling)
	} else if ( ay > 0.6f ) {
		dir = ( stickY > 0.0f ) ? 1 : -1;
	}
	if ( VR_Clicking() || vr.pressing ) {
		dir = 0;	// a wheel step would end the drag under the held trigger, or
					// scroll the list just before this poll's click lands
	}

	const int now = Sys_Milliseconds();
	if ( dir != vr.scrollDir ) {
		vr.scrollDir = dir;
		if ( !dir || now - vr.scrollStepTime < 100 ) {
			return;
		}
		vr.scrollTime = now + 400;	// one step, then a pause before it repeats
	} else if ( !dir || now - vr.scrollTime < 0 ) {
		return;
	} else {
		vr.scrollTime = now + 100;
	}
	vr.scrollStepTime = now;

	const int key = ( dir > 0 ) ? A_MWHEELUP : A_MWHEELDOWN;
	VR_QueueKey( key, qtrue );
	VR_QueueKey( key, qfalse );
	VR_Pulse( hand, 0.01f, 0.25f );
}

// A UI module (re)started: park its cursor in the top left corner, so the
// engine's copy (VR_UICursorMoved) starts out where the UI's cursor is.
void VR_UIStarted( void ) {
	UIVM_MouseEvent( -SCREEN_WIDTH * 4, -SCREEN_HEIGHT * 4 );
	vr.pointerForce = qtrue;	// and put it back where the controller points
}

// The pose wheel on a key, for playing without the headset (in it, Y holds
// it): once, on the first of these that's free, unless it's bound already.
static void VR_DefaultPoseWheelBind( void ) {
	static const char *keys[] = { "p", "o", "i" };
	cvar_t *applied = Cvar_Get( "vr_poseBindApplied", "0", CVAR_ARCHIVE );

	if ( applied->integer >= 1 ) {
		return;
	}
	Cvar_Set( "vr_poseBindApplied", "1" );
	if ( Key_GetKey( "+posewheel" ) >= 0 ) {
		return;
	}
	for ( size_t i = 0; i < ARRAY_LEN( keys ); i++ ) {
		const int k = Key_StringToKeynum( (char *)keys[i] );
		const char *b = Key_GetBinding( k );
		if ( k >= 0 && ( !b || !b[0] ) ) {
			Key_SetBinding( k, "+posewheel" );
			Com_Printf( "Pose wheel: hold %s (bind <key> +posewheel to change it)\n", keys[i] );
			return;
		}
	}
}

static void VR_SetDefaultBinds( void ) {
	// Once per install, and only for keys the player hasn't bound, so their
	// own binds (and deliberate unbinds) survive.
	cvar_t *applied = Cvar_Get( "vr_bindsApplied", "0", CVAR_ARCHIVE );
	if ( applied->integer >= 1 ) {
		return;
	}
	for ( int k = 0; k < VR_NUM_KEYS; k++ ) {
		const char *b = Key_GetBinding( VR_FIRST_KEY + k );
		if ( !b || !b[0] ) {
			Key_SetBinding( VR_FIRST_KEY + k, vrDefaultBinds[k] );
		}
	}
	Cvar_Set( "vr_bindsApplied", "1" );
}

static float VR_GetFloat( XrAction action, XrPath sub ) {
	if ( action == XR_NULL_HANDLE ) {
		return 0.0f;
	}
	XrActionStateGetInfo gi;
	memset( &gi, 0, sizeof( gi ) );
	gi.type = XR_TYPE_ACTION_STATE_GET_INFO;
	gi.action = action;
	gi.subactionPath = sub;
	XrActionStateFloat st;
	memset( &st, 0, sizeof( st ) );
	st.type = XR_TYPE_ACTION_STATE_FLOAT;
	if ( XR_FAILED( xrGetActionStateFloat( vr.session, &gi, &st ) ) || !st.isActive ) {
		return 0.0f;
	}
	return st.currentState;
}

static qboolean VR_GetBool( XrAction action, XrPath sub ) {
	if ( action == XR_NULL_HANDLE ) {
		return qfalse;
	}
	XrActionStateGetInfo gi;
	memset( &gi, 0, sizeof( gi ) );
	gi.type = XR_TYPE_ACTION_STATE_GET_INFO;
	gi.action = action;
	gi.subactionPath = sub;
	XrActionStateBoolean st;
	memset( &st, 0, sizeof( st ) );
	st.type = XR_TYPE_ACTION_STATE_BOOLEAN;
	if ( XR_FAILED( xrGetActionStateBoolean( vr.session, &gi, &st ) ) || !st.isActive ) {
		return qfalse;
	}
	return st.currentState ? qtrue : qfalse;
}

// Whether a sensor's bound on hand h's controller (isActive: the current
// interaction profile has an input for it), and its state.
static qboolean VR_GetSensor( XrAction action, XrPath sub, qboolean *active ) {
	*active = qfalse;
	if ( action == XR_NULL_HANDLE ) {
		return qfalse;
	}
	XrActionStateGetInfo gi;
	memset( &gi, 0, sizeof( gi ) );
	gi.type = XR_TYPE_ACTION_STATE_GET_INFO;
	gi.action = action;
	gi.subactionPath = sub;
	XrActionStateBoolean st;
	memset( &st, 0, sizeof( st ) );
	st.type = XR_TYPE_ACTION_STATE_BOOLEAN;
	if ( XR_FAILED( xrGetActionStateBoolean( vr.session, &gi, &st ) ) || !st.isActive ) {
		return qfalse;
	}
	*active = qtrue;
	return st.currentState ? qtrue : qfalse;
}

static float VR_GetSensorFloat( XrAction action, XrPath sub, qboolean *active ) {
	*active = qfalse;
	if ( action == XR_NULL_HANDLE ) {
		return 0.0f;
	}
	XrActionStateGetInfo gi;
	memset( &gi, 0, sizeof( gi ) );
	gi.type = XR_TYPE_ACTION_STATE_GET_INFO;
	gi.action = action;
	gi.subactionPath = sub;
	XrActionStateFloat st;
	memset( &st, 0, sizeof( st ) );
	st.type = XR_TYPE_ACTION_STATE_FLOAT;
	if ( XR_FAILED( xrGetActionStateFloat( vr.session, &gi, &st ) ) || !st.isActive ) {
		return 0.0f;
	}
	*active = qtrue;
	return st.currentState;
}

// What hand h's controller senses of its fingers (vrFingerSense_t): the
// trigger and the grip as read, and each sensor it has - on, or not.
static void VR_ReadFingers( int h, float trigger, float squeeze ) {
	static const struct {
		XrAction	*action;
		int			bit;
	} sensors[] = {
		{ &vr.actTriggerTouch, VR_SENSE_TRIGGER_TOUCH },
		{ &vr.actTriggerNear, VR_SENSE_TRIGGER_NEAR },
		{ &vr.actThumbTouch, VR_SENSE_THUMB_TOUCH },
		{ &vr.actThumbNear, VR_SENSE_THUMB_NEAR },
	};
	vrFingerSense_t *fs = &vr.fingers[h];
	qboolean active;

	memset( fs, 0, sizeof( *fs ) );
	fs->trigger = Com_Clamp( 0.0f, 1.0f, trigger );
	fs->squeeze = Com_Clamp( 0.0f, 1.0f, squeeze );
	for ( size_t i = 0; i < ARRAY_LEN( sensors ); i++ ) {
		if ( VR_GetSensor( *sensors[i].action, vr.handPath[h], &active ) ) {
			fs->touched |= sensors[i].bit;
		}
		if ( active ) {
			fs->sensed |= sensors[i].bit;
		}
	}
	fs->indexCurl = Com_Clamp( 0.0f, 1.0f, VR_GetSensorFloat( vr.actTriggerCurl, vr.handPath[h], &active ) );
	if ( active ) {
		fs->sensed |= VR_SENSE_INDEX_CURL;
	}
	fs->touched &= fs->sensed;
}

static void VR_GetVec2( XrAction action, float *x, float *y ) {
	*x = *y = 0.0f;
	if ( action == XR_NULL_HANDLE ) {
		return;
	}
	XrActionStateGetInfo gi;
	memset( &gi, 0, sizeof( gi ) );
	gi.type = XR_TYPE_ACTION_STATE_GET_INFO;
	gi.action = action;
	XrActionStateVector2f st;
	memset( &st, 0, sizeof( st ) );
	st.type = XR_TYPE_ACTION_STATE_VECTOR2F;
	if ( XR_FAILED( xrGetActionStateVector2f( vr.session, &gi, &st ) ) || !st.isActive ) {
		return;
	}

	// radial deadzone, rescaled so movement starts smoothly at its edge
	const float dz = Com_Clamp( 0.0f, 0.9f, vr_deadzone->value );
	const float len = sqrtf( st.currentState.x * st.currentState.x + st.currentState.y * st.currentState.y );
	if ( len <= dz ) {
		return;
	}
	const float scale = Q_min( 1.0f, ( len - dz ) / ( 1.0f - dz ) ) / len;
	*x = st.currentState.x * scale;
	*y = st.currentState.y * scale;
}

/*
Hands: which one holds your weapon, and what each trigger and grip does.

Guns go in your right hand, or your left with vr_leftHanded on (VR_GunHand:
it aims them). The saber goes in either - the cgame says which hand holds
your weapon every frame (ext.VR_SetWeaponHand); without word from it lately,
the gun hand. That hand's trigger attacks (lights and puts out a saber, as
the game's attack button does in your hands), the other one alt-attacks.
With your hands empty (holsters: every weapon on your body) neither trigger
presses anything; with a saber in each, either one attacks (lights or puts
out both).
The grips are Use and the Force: the right and the left, or the left and the
right when left-handed. A trigger or grip goes on pressing the key it
pressed until it's let go, whatever changes meanwhile.
*/
static int VR_GunHand( void ) {
	return ( vr_leftHanded && vr_leftHanded->integer ) ? VR_HAND_LEFT : VR_HAND_RIGHT;
}

// (-1: your hands are empty; 2: either hand - a saber in each)
static int VR_WeaponHand( void ) {
	if ( vr.weaponHandSaid && cls.realtime - vr.weaponHandTime >= 0 && cls.realtime - vr.weaponHandTime < 500 ) {
		return vr.weaponHand;
	}
	return VR_GunHand();
}

void VR_SetWeaponHand( int hand ) {
	vr.weaponHand = ( hand == -1 || hand == 2 ) ? hand : ( hand == VR_HAND_LEFT ) ? VR_HAND_LEFT : VR_HAND_RIGHT;
	vr.weaponHandTime = cls.realtime;
	vr.weaponHandSaid = qtrue;
}

// The key hand h's trigger or grip presses if pressed now (in the menu each
// trigger is its own: either clicks); -1: none (a trigger, your hands empty).
static int VR_RoleKey( int control, int h ) {
	if ( control == VR_CTL_TRIGGER ) {
		if ( vr.pointerMode ) {
			return ( h == VR_HAND_LEFT ) ? VR_KEY_TRIGGER_L : VR_KEY_TRIGGER_R;
		}
		if ( VR_WeaponHand() < 0 ) {
			return -1;
		}
		return ( h == VR_WeaponHand() || VR_WeaponHand() == 2 ) ? VR_KEY_TRIGGER_R : VR_KEY_TRIGGER_L;
	}
	return ( ( h == VR_HAND_LEFT ) == ( VR_GunHand() == VR_HAND_LEFT ) ) ? VR_KEY_GRIP_R : VR_KEY_GRIP_L;
}

// The key hand h's trigger or grip presses: while held, the one it pressed;
// else the one it would.
static int VR_ControlKey( int control, int h ) {
	return vr.ctlKey[control][h] ? vr.ctlKey[control][h] - 1 : VR_RoleKey( control, h );
}

// How far a grip must be pressed for Use and the Force (vr_gripPress): a
// lighter squeeze only curls your fingers (a point, a thumbs up, a fist).
static float VR_GripPress( void ) {
	const float p = vr_gripPress ? vr_gripPress->value : VR_GRIP_PRESS_DEFAULT;

	if ( !( p >= VR_TRIGGER_PRESS ) ) {
		return VR_TRIGGER_PRESS;	// (and NaN)
	}
	return ( p < VR_GRIP_PRESS_MAX ) ? p : VR_GRIP_PRESS_MAX;
}

static qboolean VR_ForceTakesTrigger( int h );

// Hand h's trigger or grip, read as value (0..1): pressed past VR_TRIGGER_PRESS
// - a grip past vr_gripPress, unless it's squeezed afresh where it can take
// hold of something (VR_GrabZone: it grabs, VR_SetKey) - let go under 0.4 (a
// grip held as a grab that holds, under VR_GRAB_LET_GO). Pressed, it presses
// the key for what it does then (the other one if the other hand's is
// pressing that already); let go, that key goes up. (The Force hand's trigger
// pressed while its Force grip's held is the gestures', and presses nothing.)
static void VR_HandControl( int control, int h, float value ) {
	const qboolean was = vr.ctlDown[control][h];
	const float letGo = ( control == VR_CTL_GRIP && vr.grabbing[h] && vr.grabHolds ) ? VR_GRAB_LET_GO : 0.4f;
	float pressAt = VR_TRIGGER_PRESS;
	int key;

	if ( control == VR_CTL_GRIP ) {
		if ( value <= 0.4f ) {
			vr.gripFresh[h] = qtrue;	// (let go: the next squeeze is a fresh one)
		}
		if ( !was ) {
			if ( !vr.gripFresh[h] || !VR_GrabZone( h ) ) {
				pressAt = VR_GripPress();
			}
			if ( value > VR_TRIGGER_PRESS && value <= pressAt ) {
				// squeezed past where a grab takes it, nothing there to take
				// hold of: not a grab, moved onto something now (only pressed on)
				vr.gripFresh[h] = qfalse;
			}
		}
	}
	const qboolean down = (qboolean)( value > pressAt || ( was && value > letGo ) );

	if ( down && !was ) {
		const int other = h ^ 1;
		if ( control == VR_CTL_TRIGGER && VR_ForceTakesTrigger( h ) ) {
			// (the Force hand's trigger, its Force grip held: Lightning or
			// Drain - the gestures', till it's let go)
			vr.forceTrigger[h] = qtrue;
			vr.ctlDown[control][h] = down;
			return;
		}
		key = VR_RoleKey( control, h );
		if ( key < 0 ) {
			// (presses nothing: nothing till it's let go, whatever changes meanwhile)
			vr.ctlDown[control][h] = down;
			return;
		}
		if ( vr.ctlKey[control][other] == key + 1 && control == VR_CTL_TRIGGER && VR_WeaponHand() == 2 && !vr.pointerMode ) {
			// (a saber in each hand, the other's trigger attacking already: this one does nothing till let go)
			vr.ctlDown[control][h] = down;
			return;
		}
		if ( vr.ctlKey[control][other] == key + 1 ) {
			// (the other hand's still pressing it: this one the other key)
			key = ( control == VR_CTL_TRIGGER ) ? ( key == VR_KEY_TRIGGER_R ? VR_KEY_TRIGGER_L : VR_KEY_TRIGGER_R ) :
				( key == VR_KEY_GRIP_R ? VR_KEY_GRIP_L : VR_KEY_GRIP_R );
		}
		vr.ctlKey[control][h] = key + 1;
		vr.keyHand[key] = h;
	}
	vr.ctlDown[control][h] = down;
	if ( control == VR_CTL_TRIGGER && !down ) {
		vr.forceTrigger[h] = qfalse;
	}
	if ( !vr.ctlKey[control][h] ) {
		return;
	}
	key = vr.ctlKey[control][h] - 1;
	VR_SetKey( key, down );
	if ( !down ) {
		vr.ctlKey[control][h] = 0;
	}
}

static qboolean VR_GameOwnsView( void );

/*
================================================================================

The Force hand

Force gestures (vr_forceGestures, qcommon/vr_force.h): your Force hand - the
hand whose grip is the Force grip, while it's empty - reaches out with the
Force while its grip is held, and pushes, pulls, grips, shoots lightning or
drains as it moves; a quick squeeze of it is Use Force (the power picked). What
it does goes to the game as the game's own inputs (VR_FillCmdPose: generic
commands and buttons, Use Force held a moment), so it works on any server - and
one running this build aims it from your hand: a Push's or Pull's way goes with
its command (vrUserPose_t.thrust), where the hand points with the avatar (the
cgame's: vrAvatar_t.forceDir).

The Force grip's press goes to the gestures, not its key (VR_SetKey) - a grab's
still a grab - and so does that hand's trigger pressed while it's held
(VR_HandControl), each till it's let go.

================================================================================
*/

#define VR_FORCE_LOST_MS	200		// a hand not tracked this long isn't the Force hand

// The Force hand: the hand whose grip is the Force grip (the left; the right,
// left-handed), while Force gestures are on and it's empty - not holding a
// saber, a hilt or your gun, not taking hold of one - and tracked, in a game,
// out of the menu, your view yours, with the Force grip bound to Use Force
// (+useforce, as it is unless you change it). -1: none.
static int VR_ForceHand( void ) {
	const int h = ( VR_GunHand() == VR_HAND_LEFT ) ? VR_HAND_RIGHT : VR_HAND_LEFT;
	const int weaponHand = VR_WeaponHand();
	const char *b;

	if ( !vr_forceGestures || vr_forceGestures->integer <= VRF_MODE_OFF || vr_forceGestures->integer > VRF_MODE_OPEN ) {
		return -1;
	}
	if ( vr.pointerMode || cls.state != CA_ACTIVE || VR_GameOwnsView() ) {
		return -1;
	}
	if ( weaponHand == h || weaponHand == 2 || vr.grabbing[h] || !vr.handTrackTime[h] ||
		Sys_Milliseconds() - vr.handTrackTime[h] > VR_FORCE_LOST_MS ) {
		return -1;
	}
	b = Key_GetBinding( VR_FIRST_KEY + VR_KEY_GRIP_L );
	return ( b && !Q_stricmp( b, "+useforce" ) ) ? h : -1;
}

// Whether hand h's trigger, pressed now, is the gestures' (VR_HandControl):
// the Force hand's, its Force grip held - reaching out.
static qboolean VR_ForceTakesTrigger( int h ) {
	return (qboolean)( vr.forceGrip && !vr.forceGripDead && vr.forceGripHand == h && h == VR_ForceHand() );
}

// A quick squeeze of the Force grip: what it's bound to (Use Force) held for a
// moment, so the server sees it whatever its pace - and let go.
static void VR_ForceTap( void ) {
	if ( vr.forceTapUntil ) {
		return;	// (one's held already)
	}
	VR_QueueKey( VR_FIRST_KEY + VR_KEY_GRIP_L, qtrue );
	vr.forceTapUntil = Q_max( 1, Sys_Milliseconds() + VRF_TAP_HOLD_MS );
	vr.forceTaps++;
}

static void VR_ForceTapRelease( qboolean now ) {
	if ( vr.forceTapUntil && ( now || Sys_Milliseconds() - vr.forceTapUntil >= 0 ) ) {
		VR_QueueKey( VR_FIRST_KEY + VR_KEY_GRIP_L, qfalse );
		vr.forceTapUntil = 0;
	}
}

// A Push or a Pull: its generic command, queued with the cgame's, the way it
// went with it (VR_FillCmdPose). (Only ever from a Force hand: in a game.)
static void VR_ForceThrow( int genCmd, const vec3_t thrust, float speed ) {
	if ( vr.numGenCmds >= (int)ARRAY_LEN( vr.genCmds ) ) {
		return;	// (a full queue: it's lost - as a key pressed again too soon)
	}
	vr.genThrown[vr.numGenCmds] = qtrue;
	VectorCopy( thrust, vr.genThrust[vr.numGenCmds] );
	vr.genCmds[vr.numGenCmds++] = genCmd;
	if ( genCmd == GENCMD_FORCE_PULL ) {
		vr.forcePulls++;
	} else {
		vr.forcePushes++;
	}
	vr.forceSpeed = speed;
}

// Everything the Force hand does stops now (input lost, the session over),
// and its grip held now counts no more till it's let go.
static void VR_ForceDrop( void ) {
	VRF_Clear( &vr.force );
	vr.forceHeld = 0;
	if ( vr.forceGrip ) {
		vr.forceGripDead = qtrue;
	}
	vr.forceTrigger[VR_HAND_LEFT] = vr.forceTrigger[VR_HAND_RIGHT] = qfalse;
	vr.forceHand = -1;
	VR_ForceTapRelease( qtrue );
}

// A hand's pose read this poll (VR_PollInput): whether the runtime tracks it
// now - its TRACKED bit - or only gives where it was last seen, or guesses
// (a controller hidden from the cameras, say: its pose valid all the same).
// The Force hand's gestures read only a tracked hand (VR_ForcePoll); a runtime
// that's never said it tracks a hand has it tracked whenever it's valid.
static void VR_NoteTracking( int h, XrSpaceLocationFlags flags ) {
	vr.handTracked[h] = (qboolean)( ( flags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT ) != 0 );
	if ( vr.handTracked[h] ) {
		vr.handTrackedSeen[h] = qtrue;
	}
}

// Once a poll, after the buttons: the Force hand as it is now (its pose in the
// controllers' space, your head's with it), what its gestures do.
static void VR_ForcePoll( const float trigger[2], const float squeeze[2] ) {
	const int h = VR_ForceHand();
	vrfInput_t in;
	vrfOutput_t out;

	if ( h != vr.forceHand ) {
		// the Force hand went, came or changed: its grip held now isn't
		// reaching out - not till it's let go
		if ( vr.forceGrip ) {
			vr.forceGripDead = qtrue;
		}
		vr.forceHand = h;
	}
	memset( &in, 0, sizeof( in ) );
	in.time = Sys_Milliseconds();
	in.mode = vr_forceGestures ? vr_forceGestures->integer : VRF_MODE_OFF;
	in.free = (qboolean)( h >= 0 );
	if ( in.free ) {
		const float pos[3] = { vr.gripPose[h].position.x, vr.gripPose[h].position.y, vr.gripPose[h].position.z };
		const float head[3] = { vr.headPose.position.x, vr.headPose.position.y, vr.headPose.position.z };
		vec3_t axis[3];
		in.hand = h;
		in.tracked = (qboolean)( vr.handValid[h] && ( vr.handTracked[h] || !vr.handTrackedSeen[h] ) );	// (not: where it was last seen, or a guess)
		VR_XrToQuake( pos, in.pos );
		VR_XrToQuake( head, in.head );
		in.headYaw = vr.headYaw;
		// (OpenXR's grip pose: the left palm faces the controller's right, the
		// right palm its left)
		VR_QuatToAxis( &vr.gripPose[h].orientation, axis );
		in.palmUp = ( h == VR_HAND_LEFT ) ? -axis[1][2] : axis[1][2];
		// (the Force grip held is this hand's: squeezed as the Force hand, made
		// dead above as that changed; its trigger the gestures' till let go)
		in.grip = (qboolean)( vr.forceGrip && !vr.forceGripDead );
		in.trigger = vr.forceTrigger[h];
		in.open = (qboolean)( squeeze[h] < VRF_OPEN && trigger[h] < VRF_OPEN );
		in.thrust = vr_forceThrust ? vr_forceThrust->value : VRF_THRUST_DEFAULT;
	}
	VRF_Update( &vr.force, &in, &out );
	if ( out.events & VRF_TAP ) {
		VR_ForceTap();
	}
	if ( out.events & VRF_PUSH ) {
		VR_ForceThrow( GENCMD_FORCE_THROW, out.thrust, out.speed );
	}
	if ( out.events & VRF_PULL ) {
		VR_ForceThrow( GENCMD_FORCE_PULL, out.thrust, out.speed );
	}
	vr.forceHeld = out.held;
	VR_ForceTapRelease( qfalse );
}

/*
================================================================================

Comfort: the Comfort Vignette and Snap Fade

The Comfort Vignette (vr_vignette: Low, Medium or High) darkens the edges of
your view while the game moves you - the stick, smooth turning, and your speed
(running, a jump, a fall, a knock) - more the faster you go. Nothing while you
stand still or move in real life: your head moving doesn't move you in the
game. Snap Fade (vr_snapFade) dips the view to dark as you snap turn, and turns
you while it's dark, so you never see the world jump.

Both are drawn over each eye's finished picture (VR_DrawComfort): a soft dark
ring round where your head faces, at the same angles in both eyes - so it sits
far off, like the rim of a visor, not in front of your nose - and for the fade,
all of it.

================================================================================
*/

// How strong the vignette is (0..1) goes by whichever of these is most: the
// moving stick's push (half, walking), while it moves you (VR_AddMovement) and
// a moment after; smooth turning's speed (VR_COMFORT_TURN_FULL degrees a
// second: full); your speed in the game (under VR_COMFORT_SPEED_LOW units a
// second: nothing; VR_COMFORT_SPEED_FULL, running: full). It comes on quickly
// and goes off more gently.
#define VR_COMFORT_STICK_MS		100
#define VR_COMFORT_TURN_FULL	180.0f
#define VR_COMFORT_SPEED_LOW	30.0f
#define VR_COMFORT_SPEED_FULL	250.0f
#define VR_COMFORT_IN			0.06f		// seconds from nothing to full
#define VR_COMFORT_OUT			0.25f		// ...and from full to nothing
#define VR_COMFORT_STALE_MS		250			// the latest snapshot this far behind: its speed doesn't count

// The ring at full strength (Low, Medium, High): clear out to the first angle
// from where your head faces, black from the second, softening between them.
// Weaker, it's as much fainter, and further out (VR_VIGNETTE_OPENING degrees,
// at nothing: past the edges of most headsets' view).
static const float vrVignetteFull[3][2] = { { 45.0f, 65.0f }, { 35.0f, 55.0f }, { 25.0f, 45.0f } };
#define VR_VIGNETTE_OPENING		15.0f
#define VR_VIGNETTE_SEGMENTS	64			// round it
#define VR_VIGNETTE_BANDS		8			// from clear to black

// Snap Fade: dark in VR_SNAP_FADE_OUT seconds, turned, light again in
// VR_SNAP_FADE_IN - once the frame that turns you has been drawn (black till
// then, VR_SNAP_HOLD_MS at most).
#define VR_SNAP_FADE_OUT		0.04f
#define VR_SNAP_FADE_IN			0.07f
#define VR_SNAP_HOLD_MS			100
enum {
	VR_SNAP_IDLE,
	VR_SNAP_DARKENING,		// turns held (snapHeld) till it's dark
	VR_SNAP_LIGHTENING		// turned
};
enum {
	VR_SNAP_TURN_DRAWN,		// the frame that turns you drawn (black): it can lighten
	VR_SNAP_TURN_HELD,		// the turn in pendingTurn, for the next command...
	VR_SNAP_TURN_TAKEN		// ...taken by it (VR_ApplyHeadAim): its frame not drawn yet (VR_UpdateScreen)
};

// Whether the latest snapshot's speed is how fast the game moves your view:
// not once snapshots stop coming (Connection Interrupted: it's the last one's),
// nor at the intermission, frozen or paused (where nothing clears it).
// Following someone, it's theirs: your view moves with them.
static qboolean VR_SnapSpeedCounts( void ) {
	if ( cls.state != CA_ACTIVE || !cl.snap.valid || cl.serverTime - cl.snap.serverTime >= VR_COMFORT_STALE_MS ) {
		return qfalse;
	}
	if ( com_sv_running->integer && sv_paused->integer && cl_paused->integer ) {
		return qfalse;
	}
	switch ( cl.snap.ps.pm_type ) {
	case PM_FREEZE:
	case PM_INTERMISSION:
	case PM_SPINTERMISSION:
		return qfalse;
	default:
		return qtrue;
	}
}

// How strong the vignette would be now, as the game moves you (0..1).
static float VR_ComfortTarget( int now ) {
	float target = vr.comfortTurn;

	if ( now - vr.comfortMoveTime < VR_COMFORT_STICK_MS && !VR_GameOwnsView() ) {
		target = Q_max( target, vr.comfortMove );	// (dead, or following someone, the stick doesn't move you)
	}
	if ( VR_SnapSpeedCounts() ) {
		const float speed = VectorLength( cl.snap.ps.velocity );
		target = Q_max( target, ( speed - VR_COMFORT_SPEED_LOW ) / ( VR_COMFORT_SPEED_FULL - VR_COMFORT_SPEED_LOW ) );
	}
	return Com_Clamp( 0.0f, 1.0f, target );
}

// Once a frame (VR_UpdateScreen): the vignette eased towards how strong it
// would be - in the game's own view, with the setting on. Off, or in a menu:
// none, at once.
static void VR_ComfortFrame( qboolean inGame ) {
	const int now = Sys_Milliseconds();
	const float dt = Com_Clamp( 0.0f, 0.1f, ( now - vr.vignetteTime ) * 0.001f );

	vr.vignetteTime = now;
	if ( !inGame || vr_vignette->integer <= 0 ) {
		vr.vignette = 0.0f;
		return;
	}
	const float target = VR_ComfortTarget( now );
	if ( target > vr.vignette ) {
		vr.vignette = Q_min( target, vr.vignette + dt / VR_COMFORT_IN );
	} else {
		vr.vignette = Q_max( target, vr.vignette - dt / VR_COMFORT_OUT );
	}
}

// Snap Fade's turns held, and its dark, dropped (as pendingTurn is: in a menu,
// the game steering the view, the session stopped - VR_PollInput,
// VR_ApplyHeadAim).
static void VR_DropSnapTurn( void ) {
	vr.snapHeld = vr.snapFade = 0.0f;
	vr.snapPhase = VR_SNAP_IDLE;
}

// Once a frame, after the controllers' poll (VR_UpdateTracking): Snap Fade.
// Snap turns held while it darkens (snapHeld: VR_PollInput) go to pendingTurn
// as it's black, and it stays black till the frame whose command took them
// has been drawn - frames begun late (polled after their command) too - so
// the frame that turns you is black; then it lightens. In a menu, or with the
// game steering the view, they're dropped; Snap Fade switched off midway,
// they turn you at once.
static void VR_SnapFadeFrame( void ) {
	const int now = Sys_Milliseconds();
	const float dt = Q_max( 0.0f, ( now - vr.snapFadeTime ) * 0.001f );

	vr.snapFadeTime = now;
	if ( vr.snapPhase == VR_SNAP_IDLE ) {
		return;
	}
	const qboolean dropped = (qboolean)( VR_UseScreenLayer() || VR_GameOwnsView() );
	if ( dropped || !vr_snapFade->integer ) {
		if ( !dropped ) {
			vr.pendingTurn += vr.snapHeld;
		}
		VR_DropSnapTurn();
		return;
	}
	if ( vr.snapPhase == VR_SNAP_DARKENING ) {
		vr.snapFade += dt / VR_SNAP_FADE_OUT;
		if ( vr.snapFade >= 1.0f ) {
			vr.snapFade = 1.0f;
			vr.pendingTurn += vr.snapHeld;
			vr.snapHeld = 0.0f;
			vr.snapPhase = VR_SNAP_LIGHTENING;
			vr.snapTurn = VR_SNAP_TURN_HELD;
			vr.snapBlackTime = now;
		}
	} else if ( vr.snapTurn == VR_SNAP_TURN_DRAWN || now - vr.snapBlackTime >= VR_SNAP_HOLD_MS ) {
		vr.snapFade -= dt / VR_SNAP_FADE_IN;
		if ( vr.snapFade <= 0.0f ) {
			vr.snapFade = 0.0f;
			vr.snapPhase = VR_SNAP_IDLE;
		}
	}
}

// The head's right, up and back (OpenXR's x, y and z) as an eye has them: a
// canted display's eyes look a little outwards. As the eye's own if they're
// more than 30 degrees apart (one of them not tracked a moment).
static void VR_EyeBasis( int eye, float basis[3][3] ) {
	const XrQuaternionf *q = &vr.views[eye].pose.orientation;
	const XrQuaternionf eyeInverse = { -q->x, -q->y, -q->z, q->w };

	for ( int i = 0; i < 3; i++ ) {
		const float unit[3] = { i == 0 ? 1.0f : 0.0f, i == 1 ? 1.0f : 0.0f, i == 2 ? 1.0f : 0.0f };
		float local[3];
		VR_XrRotate( &vr.headPose.orientation, unit, local );
		VR_XrRotate( &eyeInverse, local, basis[i] );
	}
	if ( basis[2][2] < 0.866f ) {	// (cos 30 degrees)
		for ( int i = 0; i < 3; i++ ) {
			basis[i][0] = basis[i][1] = basis[i][2] = 0.0f;
			basis[i][i] = 1.0f;
		}
	}
}

// Where a direction from your head (OpenXR's: x right, y up, -z where it
// faces) falls in an eye's picture: -1..1 across and up, as GL draws it. One
// more than 85 degrees from where the eye looks goes in to 85 - still far off
// the picture; past 90 it would be behind the eye.
static void VR_EyeNDC( int eye, const float basis[3][3], const float dir[3], float out[2] ) {
	float d[3];

	for ( int i = 0; i < 3; i++ ) {
		d[i] = dir[0] * basis[0][i] + dir[1] * basis[1][i] + dir[2] * basis[2][i];
	}
	const float side = sqrtf( d[0] * d[0] + d[1] * d[1] );
	const float ahead = Q_max( -d[2], Q_max( side * 0.0875f, 0.0001f ) );	// (0.0875: the cotangent of 85 degrees)
	const float tl = vrRender.tanLeft[eye], tr = vrRender.tanRight[eye], tu = vrRender.tanUp[eye], td = vrRender.tanDown[eye];
	out[0] = ( 2.0f * d[0] / ahead - ( tr + tl ) ) / ( tr - tl );
	out[1] = ( 2.0f * d[1] / ahead - ( tu + td ) ) / ( tu - td );
}

// The vignette in an eye (VR_DrawComfort's GL state): its soft edge, band by
// band from clear to black, then black from there out past the picture's edges.
static void VR_DrawVignette( int eye, float strength ) {
	const int level = Com_Clampi( 1, 3, vr_vignette->integer ) - 1;
	const float open = ( 1.0f - strength ) * VR_VIGNETTE_OPENING;
	const float clear = vrVignetteFull[level][0] + open, dark = vrVignetteFull[level][1] + open;
	float basis[3][3], center[2], ring[VR_VIGNETTE_BANDS + 1][VR_VIGNETTE_SEGMENTS][2], alpha[VR_VIGNETTE_BANDS + 1];
	static const float ahead[3] = { 0.0f, 0.0f, -1.0f };

	VR_EyeBasis( eye, basis );
	VR_EyeNDC( eye, basis, ahead, center );
	for ( int b = 0; b <= VR_VIGNETTE_BANDS; b++ ) {
		const float f = (float)b / VR_VIGNETTE_BANDS;
		const float angle = DEG2RAD( clear + ( dark - clear ) * f );
		alpha[b] = strength * f * f * ( 3.0f - 2.0f * f );	// (smoothly)
		for ( int i = 0; i < VR_VIGNETTE_SEGMENTS; i++ ) {
			const float az = DEG2RAD( 360.0f * i / VR_VIGNETTE_SEGMENTS );	// (round from the right)
			const float dir[3] = { sinf( angle ) * cosf( az ), sinf( angle ) * sinf( az ), -cosf( angle ) };
			VR_EyeNDC( eye, basis, dir, ring[b][i] );
		}
	}
	// (neighbouring strips share their edge's points exactly: no gap, no overlap)
	for ( int b = 0; b < VR_VIGNETTE_BANDS; b++ ) {
		glBegin( GL_TRIANGLE_STRIP );
		for ( int i = 0; i <= VR_VIGNETTE_SEGMENTS; i++ ) {
			const int s = i % VR_VIGNETTE_SEGMENTS;
			glColor4f( 0.0f, 0.0f, 0.0f, alpha[b] );
			glVertex2f( ring[b][s][0], ring[b][s][1] );
			glColor4f( 0.0f, 0.0f, 0.0f, alpha[b + 1] );
			glVertex2f( ring[b + 1][s][0], ring[b + 1][s][1] );
		}
		glEnd();
	}
	glColor4f( 0.0f, 0.0f, 0.0f, strength );
	glBegin( GL_TRIANGLE_STRIP );
	for ( int i = 0; i <= VR_VIGNETTE_SEGMENTS; i++ ) {
		const float *p = ring[VR_VIGNETTE_BANDS][i % VR_VIGNETTE_SEGMENTS];
		const float dx = p[0] - center[0], dy = p[1] - center[1];
		const float len = sqrtf( dx * dx + dy * dy );
		const float further = 8.0f / len;	// (8 across: the picture's 2; len is a ring's radius, 25 degrees at least)
		glVertex2f( p[0], p[1] );
		glVertex2f( p[0] + dx * further, p[1] + dy * further );
	}
	glEnd();
}

// Over an eye's finished picture, still in its framebuffer (VR_UpdateScreen,
// once the renderer's done with it): the vignette, then the fade, in black.
// Everything of GL's this changes is put back as it was: the renderer keeps a
// copy of what it last set, and goes on from it.
static void VR_DrawComfort( int eye ) {
	const float vignette = vr.vignette, fade = vr.snapFade;	// (both 0..1)

	if ( vignette < 0.01f && fade < 0.01f ) {
		return;
	}
	if ( vrRender.tanRight[eye] - vrRender.tanLeft[eye] < 0.001f || vrRender.tanUp[eye] - vrRender.tanDown[eye] < 0.001f ) {
		return;	// (the eyes not located yet)
	}
	VR_ComfortGL();

	vglBindFramebuffer( VR_GL_FRAMEBUFFER, vr.targets[eye].fbo );
	glPushAttrib( GL_ALL_ATTRIB_BITS );	// (texture units' too, and which is active)
	glMatrixMode( GL_PROJECTION );
	glPushMatrix();
	glLoadIdentity();
	glMatrixMode( GL_MODELVIEW );
	glPushMatrix();
	glLoadIdentity();

	// plain black, blended over all of the picture
	for ( int u = vrGLTextureUnits - 1; u >= 0; u-- ) {
		if ( vglActiveTexture ) {
			vglActiveTexture( VR_GL_TEXTURE0 + u );
		}
		glDisable( GL_TEXTURE_1D );
		glDisable( GL_TEXTURE_2D );
		glDisable( VR_GL_TEXTURE_3D );
		glDisable( VR_GL_TEXTURE_CUBE_MAP );
		if ( vrGLRectangle ) {
			glDisable( VR_GL_TEXTURE_RECTANGLE );
		}
	}
	if ( vrGLVertexProgram ) {
		glDisable( VR_GL_VERTEX_PROGRAM_ARB );
	}
	if ( vrGLFragmentProgram ) {
		glDisable( VR_GL_FRAGMENT_PROGRAM_ARB );
	}
	if ( vrGLCombiners ) {
		glDisable( VR_GL_REGISTER_COMBINERS_NV );
	}
	glDisable( GL_SCISSOR_TEST );
	glDisable( GL_DEPTH_TEST );
	glDisable( GL_STENCIL_TEST );
	glDisable( GL_ALPHA_TEST );
	glDisable( GL_CULL_FACE );
	glDisable( GL_FOG );
	glDisable( GL_LIGHTING );
	glDisable( GL_CLIP_PLANE0 );
	glDisable( GL_COLOR_LOGIC_OP );
	glDisable( GL_POLYGON_STIPPLE );
	glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE );
	glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
	glShadeModel( GL_SMOOTH );
	glEnable( GL_BLEND );
	glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
	glViewport( 0, 0, vr.width, vr.height );

	if ( vignette >= 0.01f ) {
		VR_DrawVignette( eye, vignette );
	}
	if ( fade >= 0.01f ) {
		glColor4f( 0.0f, 0.0f, 0.0f, fade );
		glBegin( GL_TRIANGLE_STRIP );
		glVertex2f( -1.0f, -1.0f );
		glVertex2f( 1.0f, -1.0f );
		glVertex2f( -1.0f, 1.0f );
		glVertex2f( 1.0f, 1.0f );
		glEnd();
	}

	glMatrixMode( GL_PROJECTION );
	glPopMatrix();
	glMatrixMode( GL_MODELVIEW );
	glPopMatrix();
	glPopAttrib();
}

static void VR_PollInput( void ) {
	vr.comfortTurn = 0.0f;	// (smooth turning says how fast it's turning you, polled)
	if ( !vr.inputAttached ) {
		if ( vr.forceHand >= 0 || vr.forceHeld || vr.forceTapUntil ) {
			VR_ForceDrop();	// (no controllers: no Force hand)
		}
		return;
	}

	XrActiveActionSet active;
	active.actionSet = vr.actionSet;
	active.subactionPath = XR_NULL_PATH;
	XrActionsSyncInfo si;
	memset( &si, 0, sizeof( si ) );
	si.type = XR_TYPE_ACTIONS_SYNC_INFO;
	si.countActiveActionSets = 1;
	si.activeActionSets = &active;
	if ( xrSyncActions( vr.session, &si ) != XR_SUCCESS ) {
		// not focused (system menu open, headset off): drop all input (a
		// grip held as it comes back is squeezed afresh)
		VR_ReleaseAllKeys();
		vr.handValid[0] = vr.handValid[1] = qfalse;
		vr.gripFresh[0] = vr.gripFresh[1] = qtrue;
		memset( vr.fingers, 0, sizeof( vr.fingers ) );
		return;
	}

	// hand poses
	const XrTime t = vr.frameState.predictedDisplayTime;
	for ( int h = 0; h < 2; h++ ) {
		XrSpaceLocation grip, aim;
		memset( &grip, 0, sizeof( grip ) );
		memset( &aim, 0, sizeof( aim ) );
		grip.type = aim.type = XR_TYPE_SPACE_LOCATION;
		const XrSpaceLocationFlags need = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
		vr.handValid[h] = (qboolean)(
			vr.gripSpace[h] != XR_NULL_HANDLE && vr.aimSpace[h] != XR_NULL_HANDLE &&
			XR_SUCCEEDED( xrLocateSpace( vr.gripSpace[h], vr.localSpace, t, &grip ) ) &&
			XR_SUCCEEDED( xrLocateSpace( vr.aimSpace[h], vr.localSpace, t, &aim ) ) &&
			( grip.locationFlags & need ) == need && ( aim.locationFlags & need ) == need );
		if ( vr.handValid[h] ) {
			vr.gripPose[h] = grip.pose;
			vr.aimPose[h] = aim.pose;
			vr.handTrackTime[h] = Sys_Milliseconds();
			VR_NoteTracking( h, grip.locationFlags );
		} else {
			vr.handTracked[h] = qfalse;
		}
	}

	// sticks (the move action's the left one, the turn action's the right:
	// which moves you, and which turns you, as vr_swapSticks has it)
	{
		float left[2], right[2];
		VR_GetVec2( vr.actMove, &left[0], &left[1] );
		VR_GetVec2( vr.actTurn, &right[0], &right[1] );
		VR_SetSticks( left, right );
		if ( vr.moveLatched && fabsf( vr.moveX ) < 0.3f && fabsf( vr.moveY ) < 0.3f ) {
			vr.moveLatched = qfalse;
		}
	}

	float trigger[2], squeeze[2];
	trigger[VR_HAND_LEFT] = VR_GetFloat( vr.actTrigger, vr.handPath[VR_HAND_LEFT] );
	trigger[VR_HAND_RIGHT] = VR_GetFloat( vr.actTrigger, vr.handPath[VR_HAND_RIGHT] );
	squeeze[VR_HAND_LEFT] = VR_GetFloat( vr.actSqueeze, vr.handPath[VR_HAND_LEFT] );
	squeeze[VR_HAND_RIGHT] = VR_GetFloat( vr.actSqueeze, vr.handPath[VR_HAND_RIGHT] );

	// your fingers, as the controllers sense them (the cgame's hands follow them)
	VR_ReadFingers( VR_HAND_LEFT, trigger[VR_HAND_LEFT], squeeze[VR_HAND_LEFT] );
	VR_ReadFingers( VR_HAND_RIGHT, trigger[VR_HAND_RIGHT], squeeze[VR_HAND_RIGHT] );

	// The menu screen takes the pointer: buttons click where the controller
	// points, and only a poll at the top of the frame puts the cursor there
	// before its clicks arrive (VR_ApplyMenuPointer). A nested poll (frame
	// started late, inside SCR_UpdateScreen) leaves the buttons for the next.
	const qboolean menuPointer = VR_MenuPointerWanted();
	if ( menuPointer && !vr.topOfFrame ) {
		return;
	}
	VR_UpdateMenuPointer( trigger, menuPointer );

	// In the menu either stick scrolls. That goes before the buttons: a click
	// can close the menu, and whatever this poll sends after it reaches the game.
	if ( vr.pointerMode ) {
		const qboolean right = (qboolean)( fabsf( vr.turnY ) >= fabsf( vr.moveY ) );
		VR_MenuScroll( right ? vr.turnX : vr.moveX, right ? vr.turnY : vr.moveY,
			right ? VR_TurnStickHand() : VR_MoveStickHand() );
		vr.weapLatched = qtrue;	// a stick still held as the menu closes doesn't switch weapons
	} else {
		vr.scrollDir = 0;
	}

	// buttons -> bindable keys (in the menu, see VR_ButtonKeyCode); the
	// triggers and grips by what each hand does (VR_HandControl)
	VR_HandControl( VR_CTL_TRIGGER, VR_HAND_RIGHT, trigger[VR_HAND_RIGHT] );
	VR_HandControl( VR_CTL_TRIGGER, VR_HAND_LEFT, trigger[VR_HAND_LEFT] );
	VR_HandControl( VR_CTL_GRIP, VR_HAND_RIGHT, squeeze[VR_HAND_RIGHT] );
	VR_HandControl( VR_CTL_GRIP, VR_HAND_LEFT, squeeze[VR_HAND_LEFT] );
	VR_SetKey( VR_KEY_A, VR_GetBool( vr.actA, XR_NULL_PATH ) );
	VR_SetKey( VR_KEY_B, VR_GetBool( vr.actB, XR_NULL_PATH ) );
	VR_SetKey( VR_KEY_X, VR_GetBool( vr.actX, XR_NULL_PATH ) );
	VR_WheelButton( VR_GetBool( vr.actY, XR_NULL_PATH ) );	// (tapped, or held: the pose wheel)
	VR_SetKey( VR_KEY_STICK_L, VR_GetBool( vr.actStickClick, vr.handPath[VR_MoveStickHand()] ) );	// (the moving stick's)
	VR_SetKey( VR_KEY_STICK_R, VR_GetBool( vr.actStickClick, vr.handPath[VR_TurnStickHand()] ) );
	VR_SetKey( VR_BUTTON_MENU, VR_GetBool( vr.actMenu, XR_NULL_PATH ) );	// Escape
	VR_ForcePoll( trigger, squeeze );	// (the Force hand: what its gestures do)

	// the turning stick: left/right turns, up/down switches weapon (in the
	// menu it scrolls instead, and turning is dropped by VR_ApplyHeadAim)
	if ( VR_GameOwnsView() ) {
		vr.pendingTurn = 0.0f;	// e.g. paused: don't pile up turns
	}
	const float ax = fabsf( vr.turnX ), ay = fabsf( vr.turnY );
	if ( vr.wheelState == VR_WHEEL_OPEN ) {
		vr.lastPollTime = Sys_Milliseconds();
		return;	// (the right stick picks in the pose wheel: the cgame reads it)
	}
	if ( !vr.pointerMode ) {
		if ( ay > ax && ay > 0.7f ) {
			if ( !vr.weapLatched ) {
				VR_TapKey( vr.turnY > 0.0f ? VR_KEY_STICK_R_UP : VR_KEY_STICK_R_DOWN );
				vr.weapLatched = qtrue;
			}
		} else if ( ay < 0.3f ) {
			vr.weapLatched = qfalse;
		}
	}

	const int now = Sys_Milliseconds();
	const float dt = Com_Clamp( 0.0f, 0.1f, ( now - vr.lastPollTime ) * 0.001f );
	vr.lastPollTime = now;
	if ( vr_turnMode->integer == 1 ) {
		// smooth turn (once a stick held as the pose wheel shut has come back)
		if ( vr.turnLatched && ax < 0.3f ) {
			vr.turnLatched = qfalse;
		}
		if ( ax > ay && !vr.turnLatched ) {
			vr.pendingTurn -= vr.turnX * vr_turnSpeed->value * dt;
			if ( !VR_GameOwnsView() ) {
				vr.comfortTurn = ax * vr_turnSpeed->value / VR_COMFORT_TURN_FULL;	// (the Comfort Vignette)
			}
		}
	} else {
		// snap turn (Snap Fade: held till the view's dark - VR_SnapFadeFrame)
		if ( ax > ay && ax > 0.7f ) {
			if ( !vr.turnLatched ) {
				const float turn = ( vr.turnX > 0.0f ? 1.0f : -1.0f ) * Com_Clamp( 1.0f, 180.0f, vr_turnAngle->value );
				if ( vr_snapFade->integer ) {
					vr.snapHeld -= turn;
					vr.snapPhase = VR_SNAP_DARKENING;
				} else {
					vr.pendingTurn -= turn;
				}
				vr.turnLatched = qtrue;
			}
		} else if ( ax < 0.3f ) {
			vr.turnLatched = qfalse;
		}
	}
}

/*
================================================================================

Session

================================================================================
*/

static void VR_DestroyXrSession( void ) {
	if ( vr.session == XR_NULL_HANDLE ) {
		return;
	}
	if ( vr.frameBegun ) {
		XrFrameEndInfo fei;
		memset( &fei, 0, sizeof( fei ) );
		fei.type = XR_TYPE_FRAME_END_INFO;
		fei.displayTime = vr.frameState.predictedDisplayTime;
		fei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
		xrEndFrame( vr.session, &fei );
	}
	vr.frameBegun = qfalse;
	vr.frameWaited = qfalse;
	VR_ReleaseAllKeys();
	VR_DestroyInputSpaces();
	memset( &vrClient, 0, sizeof( vrClient ) );
	for ( int e = 0; e < 2; e++ ) {
		VR_DestroySwapchain( &vr.swapchains[e] );
	}
	if ( vr.viewSpace != XR_NULL_HANDLE ) {
		xrDestroySpace( vr.viewSpace );
		vr.viewSpace = XR_NULL_HANDLE;
	}
	if ( vr.stageSpace != XR_NULL_HANDLE ) {
		xrDestroySpace( vr.stageSpace );
		vr.stageSpace = XR_NULL_HANDLE;
	}
	vr.floorValid = qfalse;
	vr.floorLocalKnown = qfalse;
	if ( vr.localSpace != XR_NULL_HANDLE ) {
		xrDestroySpace( vr.localSpace );
		vr.localSpace = XR_NULL_HANDLE;
	}
	xrDestroySession( vr.session );
	vr.session = XR_NULL_HANDLE;
	vr.running = qfalse;
	vr.focused = qfalse;
}

static void VR_HandleSessionState( XrSessionState state ) {
	vr.sessionState = state;

	switch ( state ) {
	case XR_SESSION_STATE_READY: {
		XrSessionBeginInfo sbi;
		memset( &sbi, 0, sizeof( sbi ) );
		sbi.type = XR_TYPE_SESSION_BEGIN_INFO;
		sbi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
		if ( VR_Check( xrBeginSession( vr.session, &sbi ), "xrBeginSession" ) ) {
			vr.running = qtrue;
			vr.recenterPending = qtrue;
			Com_Printf( "VR: session running\n" );
		}
		break;
	}
	case XR_SESSION_STATE_STOPPING:
		vr.running = qfalse;
		vr.focused = qfalse;
		vr.frameWaited = qfalse;
		VR_ReleaseAllKeys();
		memset( &vrClient, 0, sizeof( vrClient ) );
		xrEndSession( vr.session );
		Com_Printf( "VR: session stopped (headset idle?) - showing the desktop view meanwhile\n" );
		break;
	case XR_SESSION_STATE_FOCUSED:
		vr.focused = qtrue;
		break;
	case XR_SESSION_STATE_VISIBLE:
		vr.focused = qfalse;
		break;
	case XR_SESSION_STATE_EXITING:
	case XR_SESSION_STATE_LOSS_PENDING:
		Com_Printf( S_COLOR_YELLOW "VR: the runtime ended the VR session; showing the desktop view. Use vid_restart to reconnect.\n" );
		vr.lost = qtrue;
		VR_DestroyXrSession();
		break;
	default:
		break;
	}
}

// The controllers each hand has now (their interaction profile: what the
// runtime binds them as - which decides what they sense of your fingers),
// said in the console as they change.
static void VR_NoteControllers( void ) {
	if ( vr.session == XR_NULL_HANDLE || !vr.inputAttached ) {
		return;
	}
	for ( int h = 0; h < 2; h++ ) {
		XrInteractionProfileState ps;
		char name[XR_MAX_PATH_LENGTH];
		uint32_t len = 0;
		memset( &ps, 0, sizeof( ps ) );
		ps.type = XR_TYPE_INTERACTION_PROFILE_STATE;
		name[0] = 0;
		if ( XR_SUCCEEDED( xrGetCurrentInteractionProfile( vr.session, vr.handPath[h], &ps ) ) &&
			ps.interactionProfile != XR_NULL_PATH &&
			XR_FAILED( xrPathToString( vr.instance, ps.interactionProfile, sizeof( name ), &len, name ) ) ) {
			name[0] = 0;
		}
		if ( strcmp( name, vr.profile[h] ) ) {
			Q_strncpyz( vr.profile[h], name, sizeof( vr.profile[h] ) );
			Com_Printf( "VR: %s hand: %s\n", h == VR_HAND_RIGHT ? "right" : "left", name[0] ? name : "no controller" );
		}
	}
}

static void VR_PollEvents( void ) {
	if ( vr.instance == XR_NULL_HANDLE || vr.instanceLost ) {
		return;
	}

	XrEventDataBuffer ev;
	for ( ;; ) {
		memset( &ev, 0, sizeof( ev ) );
		ev.type = XR_TYPE_EVENT_DATA_BUFFER;
		if ( xrPollEvent( vr.instance, &ev ) != XR_SUCCESS ) {
			break;
		}

		switch ( ev.type ) {
		case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
			const XrEventDataSessionStateChanged *e = (const XrEventDataSessionStateChanged *)&ev;
			if ( vr.session != XR_NULL_HANDLE && e->session == vr.session ) {
				VR_HandleSessionState( e->state );
			}
			break;
		}
		case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
			vr.recenterPending = qtrue;
			break;
		case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
			VR_NoteControllers();
			break;
		case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
			Com_Printf( S_COLOR_YELLOW "VR: OpenXR runtime is shutting down; showing the desktop view. Use vid_restart to reconnect.\n" );
			vr.lost = qtrue;
			vr.instanceLost = qtrue;
			VR_DestroyXrSession();
			return;	// the instance must not be used any more
		default:
			break;
		}
	}
}

// Settings that make the flat game comfortable in a headset.
static void VR_ApplyComfortDefaults( void ) {
	// Once per install: the player may change these later.
	cvar_t *applied = Cvar_Get( "vr_defaultsApplied", "0", CVAR_ARCHIVE );
	if ( !applied->integer ) {
		Cvar_Set( "cg_bobPitch", "0" );		// view bobbing is a classic VR nausea trigger
		Cvar_Set( "cg_bobRoll", "0" );
		Cvar_Set( "cg_bobUp", "0" );
		Cvar_Set( "cg_thirdPersonTargetDamp", "1" );
		Cvar_Set( "vr_defaultsApplied", "1" );
	}

	// Every session (not archived by the game): no lagging third-person camera.
	Cvar_Set( "cg_thirdPersonCameraDamp", "1" );
	// (The headset decides the field of view: the cgame never widens it for a
	// widescreen in one - cg_fovAspectAdjust - so that's left as it is, for
	// playing on the monitor.)
	// The headset paces frames; vsync on the desktop mirror would cap VR at
	// the monitor's refresh rate.
	Cvar_Set( "r_swapInterval", "0" );
}

qboolean VR_WantSession( void ) {
	VR_Init();
	// re-register so changes to these latched cvars apply on vid_restart
	vr_enabled = Cvar_Get( "vr_enabled", "1", CVAR_ARCHIVE | CVAR_LATCH );
	vr_resolutionScale = Cvar_Get( "vr_resolutionScale", "1.0", CVAR_ARCHIVE | CVAR_LATCH );
	if ( !vr_enabled->integer ) {
		Com_Printf( "VR: disabled (vr_enabled 0)\n" );
		return qfalse;
	}
	return qtrue;
}

qboolean VR_CreateSession( int *eyeWidth, int *eyeHeight ) {
	if ( !VR_WantSession() ) {
		return qfalse;
	}

	// (WIN_Shutdown already destroyed any previous session; this is a no-op
	// safety net and runs before the renderer creates any GL objects)
	VR_DestroySession();
	vr.lost = qfalse;
	vr.pendingTurn = 0.0f;
	VR_DropSnapTurn();

	// Drain events left over from a previous session; if the runtime quit or
	// restarted meanwhile, start over with a fresh instance.
	VR_PollEvents();
	if ( vr.instanceLost ) {
		VR_DestroyInstance();
	}

	if ( !VR_CreateInstance() ) {
		return qfalse;
	}
	if ( !VR_LoadGL() ) {
		return qfalse;
	}

	// Required before xrCreateSession
	XrGraphicsRequirementsOpenGLKHR reqs;
	memset( &reqs, 0, sizeof( reqs ) );
	reqs.type = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR;
	xrGetOpenGLGraphicsRequirementsKHR( vr.instance, vr.systemId, &reqs );

	XrGraphicsBindingOpenGLWin32KHR binding;
	memset( &binding, 0, sizeof( binding ) );
	binding.type = XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR;
	binding.hDC = wglGetCurrentDC();
	binding.hGLRC = wglGetCurrentContext();
	if ( !binding.hDC || !binding.hGLRC ) {
		Com_Printf( S_COLOR_YELLOW "VR: no current OpenGL context\n" );
		return qfalse;
	}

	XrSessionCreateInfo sci;
	memset( &sci, 0, sizeof( sci ) );
	sci.type = XR_TYPE_SESSION_CREATE_INFO;
	sci.next = &binding;
	sci.systemId = vr.systemId;
	XrResult res = xrCreateSession( vr.instance, &sci, &vr.session );
	if ( XR_FAILED( res ) ) {
		VR_Check( res, "xrCreateSession" );
		vr.session = XR_NULL_HANDLE;
		if ( res == XR_ERROR_INSTANCE_LOST ) {
			VR_DestroyInstance();	// retry from scratch on the next vid_restart
		}
		return qfalse;
	}

	XrReferenceSpaceCreateInfo rsci;
	memset( &rsci, 0, sizeof( rsci ) );
	rsci.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
	rsci.poseInReferenceSpace = VR_IdentityPose();
	rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	if ( !VR_Check( xrCreateReferenceSpace( vr.session, &rsci, &vr.localSpace ), "xrCreateReferenceSpace(LOCAL)" ) ) {
		VR_DestroySession();
		return qfalse;
	}
	rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
	if ( !VR_Check( xrCreateReferenceSpace( vr.session, &rsci, &vr.viewSpace ), "xrCreateReferenceSpace(VIEW)" ) ) {
		VR_DestroySession();
		return qfalse;
	}
	// (the floor, where the runtime has one: your height above it - Set Height,
	// standing play, Real Crouch)
	rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
	if ( XR_FAILED( xrCreateReferenceSpace( vr.session, &rsci, &vr.stageSpace ) ) ) {
		vr.stageSpace = XR_NULL_HANDLE;
		Com_Printf( "VR: the runtime doesn't say where the floor is: Set Height can't measure (vr_height sets it)\n" );
	}

	vr.colorFormat = VR_ChooseColorFormat();
	if ( !vr.colorFormat ) {
		Com_Printf( S_COLOR_YELLOW "VR: runtime offers no 8-bit RGBA swapchain format\n" );
		VR_DestroySession();
		return qfalse;
	}

	// One size for both eyes (the renderer has a single glConfig size)
	float scale = Com_Clamp( 0.25f, 2.0f, vr_resolutionScale->value );
	int w = (int)( Q_max( vr.viewConfig[0].recommendedImageRectWidth, vr.viewConfig[1].recommendedImageRectWidth ) * scale );
	int h = (int)( Q_max( vr.viewConfig[0].recommendedImageRectHeight, vr.viewConfig[1].recommendedImageRectHeight ) * scale );
	w = Com_Clampi( 64, (int)Q_min( vr.viewConfig[0].maxImageRectWidth, vr.viewConfig[1].maxImageRectWidth ), w );
	h = Com_Clampi( 64, (int)Q_min( vr.viewConfig[0].maxImageRectHeight, vr.viewConfig[1].maxImageRectHeight ), h );
	vr.width = w;
	vr.height = h;

	for ( int e = 0; e < 2; e++ ) {
		if ( !VR_CreateSwapchain( &vr.swapchains[e] ) || !VR_CreateTarget( &vr.targets[e], w, h ) ) {
			vr.targetsReady = qtrue;	// so VR_DestroySession frees what was made
			VR_DestroySession();
			return qfalse;
		}
	}
	vr.targetsReady = qtrue;

	for ( int e = 0; e < 2; e++ ) {
		memset( &vr.views[e], 0, sizeof( vr.views[e] ) );
		vr.views[e].type = XR_TYPE_VIEW;
		vr.views[e].pose = VR_IdentityPose();
		vr.views[e].fov.angleLeft = vr.views[e].fov.angleDown = -0.8f;
		vr.views[e].fov.angleRight = vr.views[e].fov.angleUp = 0.8f;
		vr.submitPose[e] = VR_IdentityPose();
	}
	vr.headPose = VR_IdentityPose();
	vr.quadPose = VR_IdentityPose();
	vr.poseValid = qfalse;
	vr.frameBegun = qfalse;
	vr.frameWaited = qfalse;
	vr.running = qfalse;
	vr.haveLastYaw = qfalse;
	vr.lastScreenLayer = qfalse;
	vr.inUpdateScreen = qfalse;

	VR_AttachInput();
	VR_SetDefaultBinds();

	*eyeWidth = w;
	*eyeHeight = h;

	Com_Printf( "VR: rendering %dx%d per eye (swapchain format 0x%x, %s)\n", w, h, (unsigned)vr.colorFormat,
		vglCopyImageSubData ? "copy" : "blit" );

	VR_ApplyComfortDefaults();

	// Pump events now so the session reaches READY quickly.
	VR_PollEvents();
	return qtrue;
}

void VR_DestroySession( void ) {
	VR_DestroyXrSession();
	VR_DestroyTargets();
	vrRender.active = qfalse;
	vrRender.offscreen = qfalse;
}

qboolean VR_IsActive( void ) {
	return vr.targetsReady;
}

qboolean VR_IsRunning( void ) {
	return ( vr.session != XR_NULL_HANDLE && vr.running ) ? qtrue : qfalse;
}

qboolean VR_IsPacingFrames( void ) {
	return ( VR_IsRunning() && vr.paced ) ? qtrue : qfalse;
}

/*
================================================================================

Per-frame tracking

================================================================================
*/

// The menus, console and loading screens go on the floating screen (after
// JKXR's VR_UseScreenLayer, JKXR_SurfaceView.cpp, by Team Beef), and the
// chat line, to type on the keyboard there (cl_vrkeyboard.cpp).
static qboolean VR_UseScreenLayer( void ) {
	if ( cls.state != CA_ACTIVE ) {
		return qtrue;
	}
	if ( Key_GetCatcher() & ( KEYCATCH_UI | KEYCATCH_CONSOLE | KEYCATCH_MESSAGE ) ) {
		return qtrue;
	}
	if ( cls.uiStarted && UIVM_IsFullscreen() ) {
		return qtrue;
	}
	return qfalse;
}

// Whether the game, not the player, steers the camera right now (following
// someone, dead, intermission, demo playback, paused).
static qboolean VR_GameOwnsView( void ) {
	if ( cls.state != CA_ACTIVE || !cl.snap.valid || clc.demoplaying ) {
		return qtrue;
	}
	if ( com_sv_running->integer && sv_paused->integer && cl_paused->integer ) {
		return qtrue;
	}
	const playerState_t *ps = &cl.snap.ps;
	if ( ps->pm_flags & PMF_FOLLOW ) {
		return qtrue;
	}
	switch ( ps->pm_type ) {
	case PM_DEAD:
	case PM_FREEZE:
	case PM_INTERMISSION:
	case PM_SPINTERMISSION:
		return qtrue;
	default:
		return qfalse;
	}
}

// Ranged weapons aim with the gun hand (VR_GunHand); the saber, fists,
// scopes, vehicles and emplaced guns keep aiming with the head.
static qboolean VR_WantHandAim( void ) {
	// a short tracking dropout doesn't flip the aim back to the head
	if ( !vr_handAim->integer || Sys_Milliseconds() - vr.handTrackTime[VR_GunHand()] > 500 ) {
		return qfalse;
	}
	// only with a cgame that draws the gun in the hand (it asks for the VR
	// state every frame); a mod's own cgame would show a misleading view
	if ( cls.realtime - vr.cgameQueryTime > 1000 ) {
		return qfalse;
	}
	const playerState_t *ps = &cl.snap.ps;
	if ( ps->zoomMode || ps->m_iVehicleNum || ps->emplacedIndex ) {
		return qfalse;
	}
	// spectators and saber-only modes (duels, Jedi Master, true Jedi)
	if ( ps->pm_type == PM_SPECTATOR || ps->persistant[PERS_TEAM] == TEAM_SPECTATOR ||
		ps->duelInProgress || ps->isJediMaster || ps->trueJedi ) {
		return qfalse;
	}
	// While a weapon switch is under way, follow the weapon being switched to
	// (if the player owns it) so the aim changes without a ping of lag.
	int weapon = ps->weapon;
	const int wanted = cl.cgameUserCmdValue;
	if ( ( ps->weaponstate == WEAPON_DROPPING || ps->weaponstate == WEAPON_RAISING ) &&
		wanted > WP_NONE && wanted < WP_NUM_WEAPONS && ( ps->stats[STAT_WEAPONS] & ( 1 << wanted ) ) ) {
		weapon = wanted;
	}
	switch ( weapon ) {
	case WP_NONE:
	case WP_SABER:
	case WP_MELEE:
	case WP_EMPLACED_GUN:
	case WP_TURRET:
		return qfalse;
	default:
		return ( weapon > WP_NONE && weapon < WP_NUM_WEAPONS ) ? qtrue : qfalse;
	}
}

// A hand that isn't tracked this frame is gone for the game - but the hand
// holding your gun or your saber (a saber in each: either), briefly lost,
// keeps its last pose for a moment (the aim doesn't jump back to the head,
// the blade doesn't drop into the animation).
static qboolean VR_HandGone( int h ) {
	const int weaponHand = VR_WeaponHand();

	return ( ( h != VR_GunHand() && h != weaponHand && weaponHand != 2 ) || Sys_Milliseconds() - vr.handTrackTime[h] > 500 ) ?
		qtrue : qfalse;
}

// The gun hand's aim, guns being aimed with it: its aim pose tilted by
// vr_gunPitch (+ = barrel up: the gun drawn in the hand tilts with it), and
// the yaw and pitch it points (VR_ApplyHeadAim).
static void VR_GunAim( void ) {
	const int gunHand = VR_GunHand();

	// (each hand's aim as it points, before the tilt: where Move Direction's
	// "where your hand points" takes you)
	memcpy( vr.handAimAxis, vrClient.aimAxis, sizeof( vr.handAimAxis ) );
	if ( !vr.handValid[gunHand] ) {
		return;
	}
	const float tilt = vr_gunPitch->value;
	if ( tilt != 0.0f ) {
		vec3_t *ax = vrClient.aimAxis[gunHand];
		vec3_t f, u;
		RotatePointAroundVector( f, ax[1], ax[0], -tilt );
		RotatePointAroundVector( u, ax[1], ax[2], -tilt );
		VectorCopy( f, ax[0] );
		VectorCopy( u, ax[2] );
	}
	vr.handAimYaw = VR_ForwardYaw( vrClient.aimAxis[gunHand], vr.handAimYaw );
	vr.handAimPitch = RAD2DEG( asinf( Com_Clamp( -1.0f, 1.0f, -vrClient.aimAxis[gunHand][0][2] ) ) );
}

static void VR_ComputeHudRect( int eye ) {
	const float tl = vrRender.tanLeft[eye], tr = vrRender.tanRight[eye];
	const float tu = vrRender.tanUp[eye], td = vrRender.tanDown[eye];

	const float hudFov = Com_Clamp( 10.0f, 120.0f, vr_hudFov->value );
	const float depth = Com_Clamp( 0.3f, 100.0f, vr_hudDepth->value );

	// Half-IPD in metres from the two eye positions
	vec3_t d;
	VectorSubtract( vrRender.eyeOrigin[1], vrRender.eyeOrigin[0], d );
	const float halfIpd = VectorLength( d ) * 0.5f;

	// A 4:3 plane straight ahead of the head at 'depth' metres.
	const float th = tanf( DEG2RAD( hudFov * 0.5f ) );
	const float tw = th * ( 4.0f / 3.0f );
	const float cx = ( eye == 0 ? 1.0f : -1.0f ) * halfIpd / depth;

	const float x0 = ( ( cx - tw ) - tl ) / ( tr - tl ) * vr.width;
	const float x1 = ( ( cx + tw ) - tl ) / ( tr - tl ) * vr.width;
	const float y0 = ( tu - th ) / ( tu - td ) * vr.height;	// top edge, from the top
	const float y1 = ( tu + th ) / ( tu - td ) * vr.height;

	vrRender.hudX[eye] = (int)x0;
	vrRender.hudY[eye] = (int)y0;
	vrRender.hudW[eye] = Q_max( 1, (int)( x1 - x0 ) );
	vrRender.hudH[eye] = Q_max( 1, (int)( y1 - y0 ) );
}

/*
================================================================================

Your height: Set Height, Play Seated, Real Crouch

Your view goes up and down with your head (vr_positional, as far as
vr_positionalMax: Head Reach) from where the game's eyes are: from your head
as it was at the last Recenter (as the game starts, or you press it) -
unless the game knows your height (vr_height: how high your eyes are,
standing, above the floor; Set Height measures it). Then it goes by how high
your head is as if you stood (VR_HeightFrame): above the floor, standing
(where the runtime has one - a moment it's lost, where it was); playing
seated (vr_seated), from where you sat at Recenter (or as you switched it
on), that at your height: your character stands. Your eyes at your height
are the game's standing eyes, and ducking, your view follows you down past
Head Reach (VR_DOWN_REACH of your height).

Real Crouch (vr_realCrouch, your height known): your head below
VR_CROUCH_DOWN of your height crouches you - the game's crouch, as B's - and
above VR_CROUCH_UP stands you up again. The cgame says each frame how far
below standing its camera has the game's eyes (VR_SetEyeDrop): crouched by
Real Crouch, your view stays at your head as they go down - and wherever
the game puts them while you're down (it rolls you, a saber lock stands you
up, you jump; a low ceiling keeps you down as you stand: no more than Head
Reach above them). Crouched by B with your head up, your view goes down with
them, as ever (VR_DropIsYours).

In a pose (bg_vrpose.c) your view starts at your character's eyes as you
take it, and goes with your head from there (Recenter in it: from there);
Real Crouch waits - crouching gets you up out of a pose.

================================================================================
*/

#define VR_HEIGHT_MIN		1.0f		// a height outside these (metres): not set
#define VR_HEIGHT_MAX		2.5f
#define VR_CROUCH_DOWN		0.70f		// your head below this much of your height: crouched...
#define VR_CROUCH_UP		0.75f		// ...above this much: standing again
#define VR_DOWN_REACH		0.5f		// how far below the game's eyes your view follows you down, of your height
#define VR_UP_HELD			3.0f		// held down by the game, your view goes this far above its eyes at most (units: inside its crouched body)
#define VR_DROP_NONE		0.5f		// the game's eyes this close to standing (units): standing
#define VR_EYES_FRESH		1000		// ms the cgame's word on the game's eyes holds (then: standing, no pose, dry, the camera at them)
#define VR_RISE_EASE		0.15f		// seconds a switch in how high your view goes eases out over (time constant)

// Your height (vr_height), 0 if it's not set.
static float VR_Height( void ) {
	const float h = vr_height->value;

	return ( h >= VR_HEIGHT_MIN && h <= VR_HEIGHT_MAX ) ? h : 0.0f;
}

// Whether you're in a pose (bg_vrpose.c: sitting, kneeling, lying down, down
// in a fight - or getting into one), on a server that has them ("vrPose" in
// its info): which, the player state's userInt1..3 say.
static qboolean VR_InPose( const playerState_t *ps ) {
	if ( !( ( ps->userInt1 | ps->userInt2 | ps->userInt3 ) & 1 ) ) {
		return qfalse;
	}
	const char *serverInfo = cl.gameState.stringData + cl.gameState.stringOffsets[CS_SERVERINFO];
	return (qboolean)( atoi( Info_ValueForKey( serverInfo, "vrPose" ) ) > 0 );
}

// Whether the cgame's word on the game's eyes (VR_SetEyeDrop) holds: said
// lately, in a game.
static qboolean VR_EyesSaid( void ) {
	return (qboolean)( cls.state == CA_ACTIVE && cls.realtime - vr.eyeDropTime <= VR_EYES_FRESH );
}

// Whether your head may crouch you now (your height known: VR_HeightFrame
// asks only then): Real Crouch on; the game yours - the headset's focus, the
// controllers, nothing else taking the keys (the menu screen, the chat line:
// the game's own crouch waits then too), not a demo; the cgame saying where
// its eyes are (VR_SetEyeDrop: an older cgame or a mod's doesn't - your view
// would go down twice; B only); on your feet - not dead, spectating or flying
// about (a jetpack, a grip), not swimming (crouching would take you down),
// not following someone, in a vehicle or at an emplaced gun - and not in a
// pose (crouching gets you up out of one).
static qboolean VR_RealCrouchAllowed( void ) {
	if ( !vr_realCrouch->integer || !vr.focused || !vr.inputAttached || VR_UseScreenLayer() || Key_GetCatcher() || clc.demoplaying ||
		!cl.snap.valid || !VR_EyesSaid() || vr.gamePosed || vr.gameSwimming ) {
		return qfalse;
	}
	const playerState_t *ps = &cl.snap.ps;
	return (qboolean)( ps->pm_type == PM_NORMAL && !( ps->pm_flags & PMF_FOLLOW ) && !ps->m_iVehicleNum && !ps->emplacedIndex &&
		!VR_InPose( ps ) );
}

// How far below its standing eyes the game's eyes are now (metres), as the
// cgame said lately (VR_SetEyeDrop) - at standing height if it hasn't.
static float VR_EyeDrop( void ) {
	if ( !VR_EyesSaid() ) {
		return 0.0f;
	}
	return vr.eyeDrop / Com_Clamp( 1.0f, 1000.0f, vr_worldScale->value );
}

// How far down the game has its eyes, as the cgame said lately: 0 standing
// (or higher), 1 crouched (or lower).
static float VR_HeldDown( void ) {
	return VR_EyesSaid() ? Com_Clamp( 0.0f, 1.0f, vr.eyeDrop / (float)( DEFAULT_VIEWHEIGHT - CROUCH_VIEWHEIGHT ) ) : 0.0f;
}

// How high your head is as if you stood (metres), your height H: above the
// floor where it is, or was a moment ago; playing seated, or no floor ever,
// your head at Recenter at your height.
static float VR_HeadV( float H ) {
	const float head = vr.headPose.position.y;

	if ( !vr_seated->integer && vr.floorLocalKnown ) {
		return head - vr.floorLocalY;
	}
	return H + ( head - vr.positionOrigin.y );
}

// How far your head is up from the game's eyes (metres, - down), each way,
// before a switch between them has eased out: from your head at Recenter
// (your height H not known); in a pose, from your head as you took it;
// else from your height - the game's eyes down yours (owns) or not.
static float VR_RawRise( float H, qboolean posed, qboolean owns ) {
	if ( H <= 0.0f ) {
		return vr.headPose.position.y - vr.positionOrigin.y;
	}
	if ( posed ) {
		return VR_HeadV( H ) - vr.poseRefV;
	}
	return VR_HeadV( H ) - H + ( owns ? VR_EyeDrop() : 0.0f );
}

// Whether you're in a pose, as the cgame said lately.
static qboolean VR_Posed( void ) {
	return (qboolean)( VR_EyesSaid() && vr.gamePosed );
}

// Whether the camera's at your character's eyes, yours to move (first person:
// not from behind, a demo, following someone, dead), as the cgame said lately
// - not said, as if it were.
static qboolean VR_OwnEyes( void ) {
	return (qboolean)( !VR_EyesSaid() || vr.gameOwnEyes );
}

// How far above and below the game's eyes your view may go (metres), your
// height H, in a pose or not, the game's eyes going down yours (owns) or not:
// as far as Head Reach - or, your height known, up as far as Head Reach (held
// down by the game with your head up, less), down past it (VR_DOWN_REACH of
// your height; not as B takes the game's eyes down); in a pose, down past it.
// Never lower than the game's own lowest eyes, lying down, above your
// character's feet (DEAD_VIEWHEIGHT: a low pose, crouched and tall - not
// under the floor). The camera not at your character's eyes (from behind,
// dead, following someone): as far as Head Reach, whatever else.
static void VR_RiseLimits( float H, qboolean posed, qboolean owns, float *up, float *down ) {
	const float reach = Q_max( 0.0f, vr_positionalMax->value );

	*up = *down = reach;
	if ( !VR_OwnEyes() ) {
		return;
	}
	if ( posed ) {
		*down = Q_max( reach, VR_DOWN_REACH * H );	// (your height not known: 0)
	} else if ( H > 0.0f ) {
		*down = Q_max( reach, VR_DOWN_REACH * H * ( owns ? 1.0f : 1.0f - VR_HeldDown() ) );
		*up = Q_min( reach, vr.upReach );
	}
	*down = Q_min( *down, Q_max( 0.0f, (float)( DEFAULT_VIEWHEIGHT - DEAD_VIEWHEIGHT ) / Com_Clamp( 1.0f, 1000.0f, vr_worldScale->value ) - VR_EyeDrop() ) );
}

// A switch in how high your view goes - your height set, changed or let go
// of, a pose begun or over, whose the crouch is - from things as they were
// (H, posed, owns) to as they are (toH, toPosed, toOwns): what's still to ease
// out (riseCarry) set so your view's drawn where it was - your head's rise as
// it was, where that's drawn the same now; else (it goes further now: B let
// go of, down for real) where it was drawn - easing over from there. (No
// switch, nothing changes.)
static void VR_RiseSwitch( float H, qboolean posed, qboolean owns, float toH, qboolean toPosed, qboolean toOwns ) {
	const float rise = VR_RawRise( H, posed, owns ) + vr.riseCarry;
	float up, down, toUp, toDown;

	VR_RiseLimits( H, posed, owns, &up, &down );
	VR_RiseLimits( toH, toPosed, toOwns, &toUp, &toDown );
	const float drawn = Com_Clamp( -down, up, rise );
	vr.riseCarry = ( Com_Clamp( -toDown, toUp, rise ) == drawn ? rise : drawn ) - VR_RawRise( toH, toPosed, toOwns );
}

// Your height this frame, once as it begins: a switch easing out, Play
// Seated switched on taken, the floor kept, how high your head is as if you
// stood (headV), whether Real Crouch has you crouched, and how far above the
// game's eyes your view goes.
static void VR_HeightFrame( qboolean recentred ) {
	const float H = VR_Height();
	const float head = vr.headPose.position.y;
	const float reach = Q_max( 0.0f, vr_positionalMax->value );
	const int now = Sys_Milliseconds();
	const float ease = expf( -Q_max( 0.0f, ( now - vr.riseTime ) * 0.001f ) / VR_RISE_EASE );

	vr.riseTime = now;
	vr.riseCarry = ( recentred || Q_isnan( vr.riseCarry ) ) ? 0.0f : vr.riseCarry * ease;	// (Recenter: at once; broken - a bad number: gone)
	// (Play Seated switched on: your head now is how you sit)
	if ( vr.poseValid ) {
		if ( vr_seated->integer && !vr.seatedWas ) {
			vr.positionOrigin.y = head;
		}
		vr.seatedWas = (qboolean)( vr_seated->integer != 0 );
	}
	// (the floor in the local space as it's located: while it isn't, where it
	// was)
	if ( vr.floorValid ) {
		vr.floorLocalY = head - vr.floorHeadY;
		vr.floorLocalKnown = qtrue;
	}
	// (your height set, changed or let go of: eased over)
	if ( !recentred ) {
		VR_RiseSwitch( vr.riseH, VR_Posed(), vr.crouchOwnsDrop, H, VR_Posed(), vr.crouchOwnsDrop );
	}
	vr.riseH = H;
	float upTarget = reach;
	if ( H <= 0.0f ) {
		vr.crouching = qfalse;
	} else {
		vr.headV = VR_HeadV( H );
		if ( recentred ) {
			vr.poseRefV = vr.headV;	// (Recenter in a pose: your view back at your character's eyes)
		}
		if ( !VR_RealCrouchAllowed() ) {
			vr.crouching = qfalse;
		} else if ( vr.headV < VR_CROUCH_DOWN * H ) {
			vr.crouching = qtrue;
		} else if ( vr.headV > VR_CROUCH_UP * H ) {
			vr.crouching = qfalse;
		}
		// held down by the game with your head up (B, a low ceiling): up not
		// out of the top of its crouched body (VR_RiseLimits: Head Reach at most)
		if ( vr.headV >= VR_CROUCH_UP * H ) {
			upTarget = reach - ( reach - VR_UP_HELD / Com_Clamp( 1.0f, 1000.0f, vr_worldScale->value ) ) * VR_HeldDown();
		}
	}
	vr.upReach = ( recentred || Q_isnan( vr.upReach ) ) ? upTarget : upTarget + ( vr.upReach - upTarget ) * ease;
}

// How far your head is up from the game's eyes now (metres, - down), where
// those eyes are in tracking space (eyesY: the local space's height), and how
// far above and below them your view may go (up, down: VR_RiseLimits) - from
// your head at Recenter, or your height, or in a pose your head as you took
// it (VR_RawRise), with what's still to ease out.
static float VR_HeadRise( float *eyesY, float *up, float *down ) {
	const float H = VR_Height();
	const qboolean posed = VR_Posed();
	const float rise = VR_RawRise( H, posed, vr.crouchOwnsDrop ) + vr.riseCarry;

	VR_RiseLimits( H, posed, vr.crouchOwnsDrop, up, down );
	*eyesY = vr.headPose.position.y - rise;
	return rise;
}

// Your head's offset from the game's eyes (viewOfs), as far as it goes:
// sideways as far as Head Reach, up and down as VR_HeadRise says, each on its
// own (Head Movement off: none) - and the eyes the picture's drawn from, and
// what the compositor's told (where they really are, as far as the view
// follows you): both, or one (-1: both) - the eye the cgame's drawing for.
static void VR_PlaceEyes( int only ) {
	float eyesY, up, down;
	const float rise = VR_HeadRise( &eyesY, &up, &down );
	const float ofs[3] = {
		vr.headPose.position.x - vr.positionOrigin.x,
		rise,
		vr.headPose.position.z - vr.positionOrigin.z
	};
	const float reach = Q_max( 0.0f, vr_positionalMax->value );
	const float side = sqrtf( ofs[0] * ofs[0] + ofs[2] * ofs[2] );
	float scale[3] = { 1.0f, 1.0f, 1.0f };

	if ( !vr_positional->integer ) {
		scale[0] = scale[1] = scale[2] = 0.0f;
	} else {
		if ( side > reach ) {
			scale[0] = scale[2] = reach / side;
		}
		if ( rise > up ) {
			scale[1] = up / rise;
		} else if ( -rise > down ) {
			scale[1] = down / -rise;
		}
	}
	for ( int i = 0; i < 3; i++ ) {
		vr.viewOfs[i] = ofs[i] * scale[i];
	}
	VR_XrToQuake( vr.viewOfs, vrClient.headOrigin );

	for ( int e = 0; e < 2; e++ ) {
		if ( only >= 0 && e != only ) {
			continue;
		}
		const XrPosef *p = &vr.views[e].pose;
		// eye = (the head's offset) + (the eye from the head: the IPD)
		const float eyePos[3] = {
			vr.viewOfs[0] + ( p->position.x - vr.headPose.position.x ),
			vr.viewOfs[1] + ( p->position.y - vr.headPose.position.y ),
			vr.viewOfs[2] + ( p->position.z - vr.headPose.position.z )
		};
		VR_XrToQuake( eyePos, vrRender.eyeOrigin[e] );

		// Tell the compositor the pose the image is really rendered from.
		vr.submitPose[e].orientation = p->orientation;
		vr.submitPose[e].position.x = vr.positionOrigin.x + eyePos[0];
		vr.submitPose[e].position.y = eyesY + eyePos[1];
		vr.submitPose[e].position.z = vr.positionOrigin.z + eyePos[2];
	}
}

// The hands in the same space as the eyes: from the head as the controllers
// are, with the head's offset (one a moment lost keeps where it was).
static void VR_PlaceHands( void ) {
	for ( int h = 0; h < 2; h++ ) {
		if ( !vr.handValid[h] ) {
			continue;
		}
		const XrPosef *g = &vr.gripPose[h], *a = &vr.aimPose[h];
		const float gp[3] = {
			vr.viewOfs[0] + ( g->position.x - vr.headPose.position.x ),
			vr.viewOfs[1] + ( g->position.y - vr.headPose.position.y ),
			vr.viewOfs[2] + ( g->position.z - vr.headPose.position.z )
		};
		const float ap[3] = {
			vr.viewOfs[0] + ( a->position.x - vr.headPose.position.x ),
			vr.viewOfs[1] + ( a->position.y - vr.headPose.position.y ),
			vr.viewOfs[2] + ( a->position.z - vr.headPose.position.z )
		};
		VR_XrToQuake( gp, vrClient.gripOrigin[h] );
		VR_XrToQuake( ap, vrClient.aimOrigin[h] );
	}
}

// Whether the game's eyes going down is yours from now on: Real Crouch on,
// and your head down by its line (below where it stands you up; your height
// not known, the floor) - your view stays at your head as they go. (Settled
// while they're at standing height - B pressed with your head up, they take
// your view down with them; with your head down, not - and yours once your
// head's down with B not held.)
static qboolean VR_DropIsYours( void ) {
	return (qboolean)( vr_realCrouch->integer && vr.headV < VR_CROUCH_UP * VR_Height() );
}

// The cgame, each frame, as it places its camera for an eye (ext.VR_SetEyeDrop):
// how far below standing the game's eyes are (units: crouched, a pose - its
// smoothing too; 0 when the view isn't yours to move: dead, following
// someone, a demo, a view from behind) and VR_EYES_*. That eye and the hands
// placed again with it, for its picture.
void VR_SetEyeDrop( float drop, int flags ) {
	const qboolean posed = ( flags & VR_EYES_POSED ) ? qtrue : qfalse;
	const qboolean wasSaid = VR_EyesSaid();
	const qboolean wasPosed = VR_Posed();
	const qboolean wasOwned = vr.crouchOwnsDrop;
	const float H = VR_Height();

	vr.eyeDrop = Q_isnan( drop ) ? 0.0f : Com_Clamp( -64.0f, 128.0f, drop );
	vr.eyeDropTime = cls.realtime;
	vr.gameSwimming = ( flags & VR_EYES_SWIMMING ) ? qtrue : qfalse;
	vr.gameOwnEyes = ( flags & VR_EYES_OWN ) ? qtrue : qfalse;
	if ( posed && !vr.gamePosed ) {
		vr.poseRefV = vr.headV;	// (a pose taken - not the one you were in, said again after a while: your view at your character's eyes)
	}
	// (whose the game's eyes going down is: settled at standing height; your
	// head down, B not held - let go of, or the game keeping you down - yours)
	if ( fabsf( vr.eyeDrop ) < VR_DROP_NONE ) {
		vr.crouchOwnsDrop = VR_DropIsYours();
	} else if ( !vr.keyCrouch && VR_DropIsYours() ) {
		vr.crouchOwnsDrop = qtrue;
	}
	vr.gamePosed = posed;
	// (a switch - a pose begun or over, whose the crouch is - eased over from
	// where your view is, the game's eyes as they are now; said again after a
	// while - the menus - as it is now)
	if ( wasSaid ) {
		VR_RiseSwitch( H, wasPosed, wasOwned, H, posed, vr.crouchOwnsDrop );
	}
	if ( VR_IsRunning() && vr.poseValid ) {
		VR_PlaceEyes( vrRender.eye );	// (the eye it's drawing for: the other's drawn, or placed in its turn)
		VR_PlaceHands();
	}
}

static void VR_UpdateTracking( void ) {
	const XrTime t = vr.frameState.predictedDisplayTime;

	XrSpaceLocation loc;
	memset( &loc, 0, sizeof( loc ) );
	loc.type = XR_TYPE_SPACE_LOCATION;
	if ( XR_SUCCEEDED( xrLocateSpace( vr.viewSpace, vr.localSpace, t, &loc ) ) &&
		( loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT ) ) {
		vr.headPose.orientation = loc.pose.orientation;
		if ( loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT ) {
			vr.headPose.position = loc.pose.position;
		}
		vr.poseValid = qtrue;
	}
	// (your head's height above the floor)
	vr.floorValid = qfalse;
	if ( vr.stageSpace != XR_NULL_HANDLE ) {
		XrSpaceLocation floor;
		memset( &floor, 0, sizeof( floor ) );
		floor.type = XR_TYPE_SPACE_LOCATION;
		if ( XR_SUCCEEDED( xrLocateSpace( vr.viewSpace, vr.stageSpace, t, &floor ) ) && ( floor.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT ) ) {
			vr.floorHeadY = floor.pose.position.y;
			vr.floorValid = qtrue;
		}
	}

	XrViewLocateInfo vli;
	memset( &vli, 0, sizeof( vli ) );
	vli.type = XR_TYPE_VIEW_LOCATE_INFO;
	vli.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	vli.displayTime = t;
	vli.space = vr.localSpace;
	XrViewState vs;
	memset( &vs, 0, sizeof( vs ) );
	vs.type = XR_TYPE_VIEW_STATE;
	XrView views[2];
	memset( views, 0, sizeof( views ) );
	views[0].type = views[1].type = XR_TYPE_VIEW;
	uint32_t count = 0;
	if ( XR_SUCCEEDED( xrLocateViews( vr.session, &vli, &vs, 2, &count, views ) ) && count == 2 &&
		( vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT ) ) {
		vr.views[0] = views[0];
		vr.views[1] = views[1];
	}

	qboolean recentred = qfalse;
	if ( vr.recenterPending && vr.poseValid ) {
		vr.positionOrigin = vr.headPose.position;
		vr.recenterPending = qfalse;
		recentred = qtrue;
	}

	// Head angles in tracking space
	vec3_t headAxis[3];
	VR_QuatToAxis( &vr.headPose.orientation, headAxis );
	vr.headYaw = VR_AxisYaw( headAxis );
	vr.headPitch = RAD2DEG( asinf( Com_Clamp( -1.0f, 1.0f, -headAxis[0][2] ) ) );

	// Your height (Real Crouch decided), and the eyes: the head's offset from
	// the game's eyes - placed again as the cgame says where they are now
	// (VR_SetEyeDrop)
	VR_HeightFrame( recentred );
	for ( int e = 0; e < 2; e++ ) {
		VR_QuatToAxis( &vr.views[e].pose.orientation, vrRender.eyeAxis[e] );
		vrRender.tanLeft[e] = tanf( vr.views[e].fov.angleLeft );
		vrRender.tanRight[e] = tanf( vr.views[e].fov.angleRight );
		vrRender.tanUp[e] = tanf( vr.views[e].fov.angleUp );
		vrRender.tanDown[e] = tanf( vr.views[e].fov.angleDown );
	}
	VR_PlaceEyes( -1 );

	for ( int e = 0; e < 2; e++ ) {
		VR_ComputeHudRect( e );
	}

	// Controllers (buttons become key events, poses are sampled), and a snap
	// turn's fade (it turns you as it's dark)
	VR_PollInput();
	VR_SnapFadeFrame();

	// Hands in the same space as the eyes. The gun hand, briefly losing
	// tracking, keeps its last pose for a moment.
	for ( int h = 0; h < 2; h++ ) {
		if ( !vr.handValid[h] ) {
			if ( VR_HandGone( h ) ) {
				vrClient.handValid[h] = qfalse;
			}
			continue;
		}
		vrClient.handValid[h] = qtrue;
		VR_QuatToAxis( &vr.gripPose[h].orientation, vrClient.gripAxis[h] );
		VR_QuatToAxis( &vr.aimPose[h].orientation, vrClient.aimAxis[h] );
	}
	VR_PlaceHands();
	VR_GunAim();

	// Who aims, and which tracking yaw lines up with the game camera's yaw:
	//  - game owns the view (dead, spectating, ...): yaw frozen at that moment
	//  - the gun hand aims (ranged weapon): the hand's yaw
	//  - head aims: the head's yaw
	//  - head-aim off (mouse aims): frozen, the head just looks around
	const qboolean gameOwns = VR_GameOwnsView();
	vr.aimFromHand = ( !gameOwns && VR_WantHandAim() ) ? qtrue : qfalse;
	const qboolean locked = (qboolean)( gameOwns || ( !vr.aimFromHand && !vr_headAim->integer ) );
	if ( locked && !vr.viewLocked ) {
		vr.lockYaw = vrRender.referenceYaw;	// freeze where it was: no jump
	}
	vr.viewLocked = locked;
	vrRender.referenceYaw = locked ? vr.lockYaw : ( vr.aimFromHand ? vr.handAimYaw : vr.headYaw );

	vrRender.worldScale = Com_Clamp( 1.0f, 1000.0f, vr_worldScale->value );

	const float fov = Cvar_VariableValue( "cg_fov" );
	vrRender.normalFov = Com_Clamp( 1.0f, 130.0f, ( fov > 1.0f ) ? fov : 80.0f );
	vrRender.zoomed = ( cls.state == CA_ACTIVE && cl.snap.valid && cl.snap.ps.zoomMode ) ? qtrue : qfalse;

	// What the cgame needs to put the gun in your hand
	vrClient.active = vr.poseValid;
	vrClient.handAim = vr.aimFromHand;
	vrClient.grabbing[VR_HAND_LEFT] = vr.grabbing[VR_HAND_LEFT];
	vrClient.grabbing[VR_HAND_RIGHT] = vr.grabbing[VR_HAND_RIGHT];
	vrClient.trigger[VR_HAND_LEFT] = vr.ctlDown[VR_CTL_TRIGGER][VR_HAND_LEFT];
	vrClient.trigger[VR_HAND_RIGHT] = vr.ctlDown[VR_CTL_TRIGGER][VR_HAND_RIGHT];
	vrClient.stick[VR_HAND_LEFT][0] = vr.stick[VR_HAND_LEFT][0];	// (each hand's, whichever moves you)
	vrClient.stick[VR_HAND_LEFT][1] = vr.stick[VR_HAND_LEFT][1];
	vrClient.stick[VR_HAND_RIGHT][0] = vr.stick[VR_HAND_RIGHT][0];
	vrClient.stick[VR_HAND_RIGHT][1] = vr.stick[VR_HAND_RIGHT][1];
	vrClient.fingers[VR_HAND_LEFT] = vr.fingers[VR_HAND_LEFT];
	vrClient.fingers[VR_HAND_RIGHT] = vr.fingers[VR_HAND_RIGHT];
	vrClient.force.hand = vr.forceHand;
	vrClient.force.reaching = (qboolean)( vr.forceHand >= 0 && vr.forceGrip && !vr.forceGripDead );
	vrClient.force.held = vr.forceHeld;
	vrClient.force.pushes = vr.forcePushes;
	vrClient.force.pulls = vr.forcePulls;
	vrClient.force.taps = vr.forceTaps;
	vrClient.force.speed = vr.forceSpeed;
	vrClient.poseWheel = ( vr.wheelState == VR_WHEEL_OPEN ) ? qtrue : qfalse;
	vrClient.referenceYaw = vrRender.referenceYaw;
	vrClient.worldScale = vrRender.worldScale;
	VectorCopy( headAxis[0], vrClient.headAxis[0] );
	VectorCopy( headAxis[1], vrClient.headAxis[1] );
	VectorCopy( headAxis[2], vrClient.headAxis[2] );
}

// topOfFrame: called by VR_BeginFrame at the top of CL_Frame, not from
// VR_UpdateScreen (which may run nested inside a module).
static void VR_StartFrame( qboolean topOfFrame ) {
	if ( vr.frameBegun ) {
		if ( topOfFrame ) {
			vr.paced = qfalse;	// last frame never reached SCR_UpdateScreen
		}
		return;	// (from VR_UpdateScreen: already begun at the top of this frame)
	}
	vr.paced = qfalse;
	vr.topOfFrame = topOfFrame;

	VR_PollEvents();	// only between frames
	if ( vr.session == XR_NULL_HANDLE || !vr.running ) {
		vrClient.active = qfalse;
		return;
	}

	if ( !vr.frameWaited ) {
		XrFrameWaitInfo fwi;
		memset( &fwi, 0, sizeof( fwi ) );
		fwi.type = XR_TYPE_FRAME_WAIT_INFO;
		memset( &vr.frameState, 0, sizeof( vr.frameState ) );
		vr.frameState.type = XR_TYPE_FRAME_STATE;
		if ( !VR_Check( xrWaitFrame( vr.session, &fwi, &vr.frameState ), "xrWaitFrame" ) ) {
			return;
		}
		vr.frameWaited = qtrue;
		vr.paced = qtrue;
	}

	XrFrameBeginInfo fbi;
	memset( &fbi, 0, sizeof( fbi ) );
	fbi.type = XR_TYPE_FRAME_BEGIN_INFO;
	if ( !VR_Check( xrBeginFrame( vr.session, &fbi ), "xrBeginFrame" ) ) {
		return;	// retried (without waiting again) next time
	}
	vr.frameWaited = qfalse;
	vr.frameBegun = qtrue;

	VR_UpdateTracking();
}

// Called at the top of CL_Frame. VR_UpdateScreen starts the frame itself if
// this didn't, but may run inside a module (loading screens), so only this
// one moves the menu cursor.
void VR_BeginFrame( void ) {
	VR_StartFrame( qtrue );
	VR_ApplyMenuPointer();
	VRK_Frame();	// (the keyboard's Backspace, held)
}

void VR_DrawOverlay( void ) {
	if ( !vrRender.active || !vrRender.screenLayer || !VRK_Wanted() ) {
		return;	// (the desktop's own window, or nothing to type)
	}
	VRK_Draw( vr.pointerX, vr.pointerY, (qboolean)( vr.pointerMode && vr.pointerHit ) );
}

/*
================================================================================

Head aim

================================================================================
*/

void VR_ApplyHeadAim( void ) {
	if ( !VR_IsRunning() || !vr.poseValid ) {
		vr.haveLastYaw = qfalse;
		vr.pendingTurn = 0.0f;
		VR_DropSnapTurn();
		return;
	}

	// The game camera's yaw must change exactly as much as the reference yaw
	// (the tracking yaw the renderer lines up with it), so the rendered view
	// only ever moves with the head. This covers head-aim, hand-aim, switching
	// between them, and leaving a locked view, without any jumps.
	const float refYaw = vrRender.referenceYaw;
	if ( !vr.haveLastYaw ) {
		vr.lastRefYaw = refYaw;
		vr.haveLastYaw = qtrue;
	}
	const float deltaYaw = AngleSubtract( refYaw, vr.lastRefYaw );
	vr.lastRefYaw = refYaw;

	const float turn = Com_Clamp( -180.0f, 180.0f, vr.pendingTurn );
	vr.pendingTurn = 0.0f;
	if ( vr.snapTurn == VR_SNAP_TURN_HELD ) {
		vr.snapTurn = VR_SNAP_TURN_TAKEN;	// (Snap Fade's, taken - or dropped: its frame's next)
	}

	if ( VR_UseScreenLayer() ) {
		return;
	}

	// thumbstick turning rotates the body; the head/hand adds on top
	cl.viewangles[YAW] += turn + deltaYaw;

	if ( vr.viewLocked ) {
		return;	// the game (or the mouse) owns pitch
	}

	// Pitch comes straight from the head/hand. Compensate the server's
	// delta_angles so the in-game pitch equals the real pitch.
	const float srcPitch = vr.aimFromHand ? vr.handAimPitch : vr.headPitch;
	const float pitch = Com_Clamp( -88.0f, 88.0f, srcPitch );
	cl.viewangles[PITCH] = pitch - SHORT2ANGLE( cl.snap.ps.delta_angles[PITCH] );
}

void VR_AddMovement( usercmd_t *cmd ) {
	vr.keyCrouch = (qboolean)( cmd->upmove < 0 );	// (B, as the keys have it: VR_SetEyeDrop)
	if ( !VR_IsRunning() || !vr.inputAttached || VR_UseScreenLayer() || ( Key_GetCatcher() & KEYCATCH_MESSAGE ) ) {
		return;
	}
	// Real Crouch: crouched by your head (VR_HeadRise) - jumping (A) stands
	// you up to jump
	if ( vr.crouching && cmd->upmove <= 0 ) {
		cmd->upmove = -127;
	}
	if ( vr.moveX == 0.0f && vr.moveY == 0.0f ) {
		return;
	}
	const int hand = VR_MoveStickHand();
	if ( vr.wheelState == VR_WHEEL_OPEN && hand == VR_HAND_RIGHT ) {
		return;	// (the right stick's picking in the pose wheel)
	}
	if ( vr.moveLatched ) {
		return;	// (...it was, as it shut: till it's back in the middle)
	}

	// The stick moves relative to where you look (or where the hand holding
	// it points); the usercmd is relative to the game camera's yaw.
	float moveYaw = vr.headYaw;
	if ( vr_moveDirection->integer == 1 && vr.handValid[hand] ) {
		moveYaw = VR_ForwardYaw( vr.handAimAxis[hand], vr.headYaw );
	}
	const float rel = DEG2RAD( AngleSubtract( moveYaw, vrRender.referenceYaw ) );
	const float c = cosf( rel ), sn = sinf( rel );
	const float fwd = vr.moveY * c + vr.moveX * sn;
	const float right = -vr.moveY * sn + vr.moveX * c;

	// walking (+speed with cl_run, or cl_run 0) caps the speed like the keys do
	const float speed = ( cmd->buttons & BUTTON_WALKING ) ? 64.0f : 127.0f;
	cmd->forwardmove = ClampChar( cmd->forwardmove + (int)( fwd * speed ) );
	cmd->rightmove = ClampChar( cmd->rightmove + (int)( right * speed ) );

	// (the Comfort Vignette: as hard as the stick's moving you)
	vr.comfortMove = Q_min( 1.0f, sqrtf( vr.moveX * vr.moveX + vr.moveY * vr.moveY ) ) * ( speed / 127.0f );
	vr.comfortMoveTime = Sys_Milliseconds();
}

const vrClientState_t *VR_GetClientState( void ) {
	vr.cgameQueryTime = cls.realtime;
	return &vrClient;
}

const vrRenderState_t *VR_GetRenderState( void ) {
	return &vrRender;
}

/*
================================================================================

Physical lightsaber

The cgame reports where the blades in your hands are each frame; every
usercmd carries the latest report to the server (clc_moveVR, vr_net.h).

================================================================================
*/

static vrUserPose_t	vrUserPose;			// latest report from the cgame
static int			vrUserPoseTime;		// cls.realtime of that report

void VR_SetUserPose( const vrUserPose_t *pose ) {
	if ( !pose || ( !pose->valid && !pose->avatar.valid ) ) {
		Com_Memset( &vrUserPose, 0, sizeof( vrUserPose ) );
		return;
	}
	vrUserPose = *pose;
	vrUserPoseTime = cls.realtime;
}

// The trigger and the saber, where the server fights with your real blade:
// with the saber lit in your hands the trigger switches it off (the game's
// own switch, as the saber's weapon key does); with it off, the trigger
// lights it (the attack button, as in the normal game: predicted, so the
// blade is there at once).
static qboolean		vrAttackHeld;		// the attack button was down for the last command
static qboolean		vrAttackSwallowed;	// ...and switched the saber off: it does nothing more until let go
static qboolean		vrSaberSwitchQueued;	// switching it off waits for a command with no other generic command

void VR_FillCmdPose( usercmd_t *cmd, vrUserPose_t *out ) {
	const qboolean attack = ( cmd->buttons & BUTTON_ATTACK ) ? qtrue : qfalse;
	const qboolean pressed = ( attack && !vrAttackHeld ) ? qtrue : qfalse;
	qboolean thrown = qfalse;
	vec3_t thrust = { 0.0f, 0.0f, 0.0f };

	vrAttackHeld = attack;
	if ( !attack ) {
		vrAttackSwallowed = qfalse;
	}
	Com_Memset( out, 0, sizeof( *out ) );
	// the cgame's buttons on every command (holsters) - your body drawn or
	// not: dead, they're what you respawn with; in from loading, what you're
	// first spawned with
	if ( VR_IsRunning() && cls.state >= CA_PRIMED && cls.realtime - vr.cmdButtonsTime >= 0 &&
		cls.realtime - vr.cmdButtonsTime < 500 ) {
		cmd->buttons |= vr.cmdButtons;
	}
	if ( !VR_IsRunning() || cls.state != CA_ACTIVE ) {
		vrSaberSwitchQueued = qfalse;
		vr.numGenCmds = 0;
		return;
	}
	if ( vrAttackSwallowed ) {
		// the press that switched the saber off: holding on doesn't light it
		// again once the switch has gone through
		cmd->buttons &= ~BUTTON_ATTACK;
	}
	if ( vrSaberSwitchQueued && !cmd->generic_cmd ) {
		cmd->generic_cmd = GENCMD_SABERSWITCH;
		vrSaberSwitchQueued = qfalse;
	}
	// the cgame's own generic commands (and the Force gestures' Push and
	// Pull), one to a command
	if ( vr.numGenCmds && !cmd->generic_cmd ) {
		cmd->generic_cmd = (byte)vr.genCmds[0];
		thrown = vr.genThrown[0];
		VectorCopy( vr.genThrust[0], thrust );
		vr.numGenCmds--;
		memmove( vr.genCmds, vr.genCmds + 1, vr.numGenCmds * sizeof( vr.genCmds[0] ) );
		memmove( vr.genThrown, vr.genThrown + 1, vr.numGenCmds * sizeof( vr.genThrown[0] ) );
		memmove( vr.genThrust, vr.genThrust + 1, vr.numGenCmds * sizeof( vr.genThrust[0] ) );
	}
	// the Force gestures' Grip, Lightning and Drain, held
	if ( vr.forceHeld & VR_FORCE_GRIP ) {
		cmd->buttons |= BUTTON_FORCEGRIP;
	}
	if ( vr.forceHeld & VR_FORCE_LIGHTNING ) {
		cmd->buttons |= BUTTON_FORCE_LIGHTNING;
	}
	if ( vr.forceHeld & VR_FORCE_DRAIN ) {
		cmd->buttons |= BUTTON_FORCE_DRAIN;
	}
	if ( !vrUserPose.valid && !vrUserPose.avatar.valid ) {
		return;	// no VR body drawn (and so no lit saber in your VR hands)
	}
	const int age = cls.realtime - vrUserPoseTime;
	if ( age < 0 || age > 1000 ) {
		return;	// the cgame stopped reporting long ago
	}
	if ( age <= 100 ) {
		*out = vrUserPose;
	} else {
		// No fresh report (a hitch): the saber is still in your hands, but
		// where its blades were then is no use to the server any more (nor
		// where your arms were, to anyone else).
		out->valid = vrUserPose.valid;
		out->trackYaw = vrUserPose.trackYaw;
	}
	// a Force gesture's Push or Pull in this command: the way it went, as the
	// world has it (by the pose's yaw - its avatar's, whose Force hand it was)
	out->thrustValid = qfalse;
	VectorClear( out->thrust );
	if ( thrown && out->avatar.valid ) {
		const float c = cosf( DEG2RAD( out->trackYaw ) ), sn = sinf( DEG2RAD( out->trackYaw ) );
		out->thrust[0] = thrust[0] * c - thrust[1] * sn;
		out->thrust[1] = thrust[0] * sn + thrust[1] * c;
		out->thrust[2] = thrust[2];
		out->thrustValid = qtrue;
	}

	// When the server's game fights with your real blade ("vrSaber 1" in its
	// serverinfo), your swings are the attacks: the trigger doesn't also
	// start the game's canned swing animations, it switches the saber off.
	// (Elsewhere it still swings.) The server drops the attack button from
	// every command that has a pose, too.
	if ( clc.vrPoseExt && out->valid ) {
		const char *serverInfo = cl.gameState.stringData + cl.gameState.stringOffsets[CS_SERVERINFO];
		if ( atoi( Info_ValueForKey( serverInfo, "vrSaber" ) ) > 0 ) {
			cmd->buttons &= ~BUTTON_ATTACK;
			if ( pressed && !cl.snap.ps.saberInFlight ) {
				// (with the saber thrown, the game's switch would knock it out
				// of the air instead)
				if ( !cmd->generic_cmd ) {
					cmd->generic_cmd = GENCMD_SABERSWITCH;
				} else {
					vrSaberSwitchQueued = qtrue;	// (a Force power went in this one)
				}
				vrAttackSwallowed = qtrue;
			}
		}
	}
}

void VR_Haptic( int hand, float seconds, float amplitude ) {
	if ( !VR_IsRunning() || hand < VR_HAND_LEFT || hand > VR_HAND_RIGHT ) {
		return;
	}
	VR_Pulse( hand, Com_Clamp( 0.0f, 1.0f, seconds ), Com_Clamp( 0.0f, 1.0f, amplitude ) );
}

void VR_SetGrabZones( int handMask ) {
	vr.grabZones = handMask & ( ( 1 << VR_HAND_LEFT ) | ( 1 << VR_HAND_RIGHT ) );
	vr.grabHolds = ( handMask & VR_GRAB_HOLDS ) ? qtrue : qfalse;
	vr.grabZoneTime = cls.realtime;
}

void VR_SetCmdButtons( int buttons ) {
	vr.cmdButtons = buttons & BUTTONS_VR;
	vr.cmdButtonsTime = cls.realtime;
}

void VR_GenericCmd( int genCmd ) {
	if ( genCmd <= 0 || genCmd > 255 || vr.numGenCmds >= (int)ARRAY_LEN( vr.genCmds ) || cls.state != CA_ACTIVE ) {
		return;	// (a full queue: the cgame asks again if it still wants it)
	}
	vr.genThrown[vr.numGenCmds] = qfalse;
	vr.genCmds[vr.numGenCmds++] = genCmd;
}

/*
================================================================================

Rendering

================================================================================
*/

static void VR_BindTarget( int eye ) {
	vglBindFramebuffer( VR_GL_FRAMEBUFFER, vr.targets[eye].fbo );

	// Clear colour/depth/stencil without disturbing the renderer's cached state.
	GLboolean depthMask;
	const GLboolean scissor = glIsEnabled( GL_SCISSOR_TEST );
	glGetBooleanv( GL_DEPTH_WRITEMASK, &depthMask );
	glDisable( GL_SCISSOR_TEST );
	glDepthMask( GL_TRUE );
	glViewport( 0, 0, vr.width, vr.height );
	glClearColor( 0.0f, 0.0f, 0.0f, 1.0f );
	glClearDepth( 1.0 );
	glClearStencil( 0 );
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );
	glDepthMask( depthMask );
	if ( scissor ) {
		glEnable( GL_SCISSOR_TEST );
	}
	glDisable( VR_GL_FRAMEBUFFER_SRGB );

	vrRender.eye = eye;
}

static void VR_UnbindTarget( void ) {
	vglBindFramebuffer( VR_GL_FRAMEBUFFER, 0 );
}

// Copies eye 0 into the desktop window.
//   crop:  fill the window with the middle of the eye image (in-game)
//   else:  letterbox 4:3 content (menus, and the flat fallback view)
static void VR_Mirror( qboolean crop, qboolean force ) {
	if ( !vr_mirror->integer && !force ) {
		return;
	}

	int ww = 0, wh = 0;
	SDL_GL_GetDrawableSize( SDL_GL_GetCurrentWindow(), &ww, &wh );
	if ( ww <= 0 || wh <= 0 ) {
		return;
	}

	int sx0 = 0, sy0 = 0, sx1 = vr.width, sy1 = vr.height;
	int dx0 = 0, dy0 = 0, dx1 = ww, dy1 = wh;

	if ( crop ) {
		const float windowAspect = (float)ww / (float)wh;
		const float eyeAspect = (float)vr.width / (float)vr.height;
		if ( windowAspect > eyeAspect ) {
			const int h = (int)( vr.width / windowAspect );
			sy0 = ( vr.height - h ) / 2;
			sy1 = sy0 + h;
		} else {
			const int w = (int)( vr.height * windowAspect );
			sx0 = ( vr.width - w ) / 2;
			sx1 = sx0 + w;
		}
	} else {
		const int w = wh * 4 / 3;
		if ( w <= ww ) {
			dx0 = ( ww - w ) / 2;
			dx1 = dx0 + w;
		} else {
			const int h = ww * 3 / 4;
			dy0 = ( wh - h ) / 2;
			dy1 = dy0 + h;
		}
	}

	const GLboolean scissor = glIsEnabled( GL_SCISSOR_TEST );
	glDisable( GL_SCISSOR_TEST );
	glDisable( VR_GL_FRAMEBUFFER_SRGB );

	vglBindFramebuffer( VR_GL_FRAMEBUFFER, 0 );
	glViewport( 0, 0, ww, wh );
	glClearColor( 0.0f, 0.0f, 0.0f, 1.0f );
	glClear( GL_COLOR_BUFFER_BIT );

	vglBindFramebuffer( VR_GL_READ_FRAMEBUFFER, vr.targets[0].fbo );
	vglBlitFramebuffer( sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1, GL_COLOR_BUFFER_BIT, GL_LINEAR );
	vglBindFramebuffer( VR_GL_FRAMEBUFFER, 0 );

	if ( scissor ) {
		glEnable( GL_SCISSOR_TEST );
	}
}

static float VR_MenuDistance( void ) {
	return Com_Clamp( 0.5f, 20.0f, vr_menuDistance->value );
}

static float VR_MenuHeight( void ) {
	return Com_Clamp( 0.2f, 20.0f, vr_menuHeight->value );
}

// The quad as far in front of where the head was as it was placed, and as
// tall, as Menu Distance and Menu Size have it.
static void VR_SetMenuQuadDistance( void ) {
	const float yaw = vr.quadYaw;
	vr.quadDist = vr.quadWantDist = VR_MenuDistance();
	vr.quadHeight = vr.quadWantHeight = VR_MenuHeight();
	vr.quadPose.position.x = vr.quadOrigin.x - sinf( yaw ) * vr.quadDist;
	vr.quadPose.position.y = vr.quadOrigin.y;
	vr.quadPose.position.z = vr.quadOrigin.z - cosf( yaw ) * vr.quadDist;
	vr.quadPose.orientation.x = 0.0f;
	vr.quadPose.orientation.y = sinf( yaw * 0.5f );
	vr.quadPose.orientation.z = 0.0f;
	vr.quadPose.orientation.w = cosf( yaw * 0.5f );
}

static void VR_PlaceMenuQuad( void ) {
	// Snapshot the head when a menu appears and put the quad in front of it.
	vec3_t axis[3];
	VR_QuatToAxis( &vr.headPose.orientation, axis );
	vr.quadYaw = DEG2RAD( VR_AxisYaw( axis ) );
	vr.quadOrigin = vr.headPose.position;
	VR_SetMenuQuadDistance();
}

// Menu Distance or Menu Size changed with the menu screen up (VR View's own
// rows): it moves (nearer or further, the same way) and grows or shrinks once
// they've stayed put a moment, nothing held - no trigger, no stick held
// scrolling - not as they're dragged or stepped (moving, the screen moved the
// pointer onto another value, which the held trigger or stick then changed).
#define VR_MENU_SETTLE_MS	600
static void VR_SettleMenuQuad( void ) {
	const float dist = VR_MenuDistance(), h = VR_MenuHeight();
	const int now = Sys_Milliseconds();

	if ( dist == vr.quadDist && h == vr.quadHeight ) {
		vr.quadWantDist = dist;
		vr.quadWantHeight = h;
		return;
	}
	if ( dist != vr.quadWantDist || h != vr.quadWantHeight || VR_Clicking() || vr.pressing || vr.scrollDir ) {
		vr.quadWantDist = dist;
		vr.quadWantHeight = h;
		vr.quadWantTime = now;	// (changing still, or held: from now)
		return;
	}
	if ( now - vr.quadWantTime >= VR_MENU_SETTLE_MS ) {
		VR_SetMenuQuadDistance();
	}
}

// layer: 0 = none, 1 = stereo projection, 2 = menu quad
static void VR_EndFrame( int layer ) {
	if ( !vr.frameBegun ) {
		return;
	}

	XrFrameEndInfo fei;
	memset( &fei, 0, sizeof( fei ) );
	fei.type = XR_TYPE_FRAME_END_INFO;
	fei.displayTime = vr.frameState.predictedDisplayTime;
	fei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;

	XrCompositionLayerProjection proj;
	XrCompositionLayerProjectionView projViews[2];
	XrCompositionLayerQuad quad;
	const XrCompositionLayerBaseHeader *layers[1];

	if ( layer == 1 ) {
		memset( &proj, 0, sizeof( proj ) );
		memset( projViews, 0, sizeof( projViews ) );
		proj.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
		proj.space = vr.localSpace;
		proj.viewCount = 2;
		proj.views = projViews;
		for ( int e = 0; e < 2; e++ ) {
			projViews[e].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
			projViews[e].pose = vr.submitPose[e];
			projViews[e].fov = vr.views[e].fov;
			projViews[e].subImage.swapchain = vr.swapchains[e].handle;
			projViews[e].subImage.imageRect.extent.width = vr.width;
			projViews[e].subImage.imageRect.extent.height = vr.height;
		}
		layers[0] = (const XrCompositionLayerBaseHeader *)&proj;
		fei.layerCount = 1;
		fei.layers = layers;
	} else if ( layer == 2 ) {
		const float h = vr.quadHeight > 0.0f ? vr.quadHeight : VR_MenuHeight();
		memset( &quad, 0, sizeof( quad ) );
		quad.type = XR_TYPE_COMPOSITION_LAYER_QUAD;
		quad.space = vr.localSpace;
		quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
		quad.subImage.swapchain = vr.swapchains[0].handle;
		quad.subImage.imageRect.extent.width = vr.width;
		quad.subImage.imageRect.extent.height = vr.height;
		quad.pose = vr.quadPose;
		quad.size.width = h * ( 4.0f / 3.0f );
		quad.size.height = h;
		layers[0] = (const XrCompositionLayerBaseHeader *)&quad;
		fei.layerCount = 1;
		fei.layers = layers;
	}

	VR_Check( xrEndFrame( vr.session, &fei ), "xrEndFrame" );
	vr.frameBegun = qfalse;
}

// Leaves the GL/OpenXR state consistent if an error is thrown mid-render
// (server kick, cgame error: Com_Error throws).
static void VR_AbortFrame( void ) {
	VR_UnbindTarget();
	vrRender.active = qfalse;
	vrRender.offscreen = qfalse;
	VR_EndFrame( 0 );
	vr.inUpdateScreen = qfalse;
}

qboolean VR_UpdateScreen( void ) {
	if ( !vr.targetsReady ) {
		return qfalse;	// no VR this run: plain desktop rendering
	}
	if ( vr.inUpdateScreen ) {
		return qtrue;	// nested SCR_UpdateScreen from inside a frame: skip it
	}

	VR_StartFrame( qfalse );

	const qboolean inXrFrame = vr.frameBegun;
	const qboolean drawVR = ( inXrFrame && vr.frameState.shouldRender ) ? qtrue : qfalse;
	if ( !drawVR ) {
		vrClient.active = qfalse;	// the cgame draws the plain desktop way
	}
	const qboolean screenLayer = VR_UseScreenLayer();
	int layer = 0;

	if ( drawVR ) {
		if ( screenLayer && !vr.lastScreenLayer ) {
			VR_PlaceMenuQuad();
		} else if ( screenLayer ) {
			VR_SettleMenuQuad();	// (moved by the menu's own Menu Distance or Size)
		}
		vr.lastScreenLayer = screenLayer;
	}
	if ( screenLayer ) {
		vrClient.active = qfalse;	// flat view under the menu: no gun-in-hand
	}
	VR_ComfortFrame( (qboolean)( drawVR && !screenLayer ) );	// (the vignette: in the game's own view)

	vr.inUpdateScreen = qtrue;
	try {
		vrRender.offscreen = qtrue;

		if ( drawVR && !screenLayer ) {
			// In the game: one view per eye
			vrRender.active = qtrue;
			vrRender.screenLayer = qfalse;

			// the cgame would otherwise add its own (wrong) eye separation
			if ( Cvar_VariableValue( "cg_stereoSeparation" ) != 0.0f ) {
				Cvar_Set( "cg_stereoSeparation", "0" );
			}

			qboolean copied = qtrue;
			for ( int eye = 0; eye < 2; eye++ ) {
				VR_BindTarget( eye );
				SCR_DrawScreenField( eye == 0 ? STEREO_LEFT : STEREO_RIGHT );
				re->EndFrame( NULL, NULL );
				VR_DrawComfort( eye );	// (the Comfort Vignette and Snap Fade, over it)
				VR_UnbindTarget();
				if ( !VR_CopyToSwapchain( eye ) ) {
					copied = qfalse;
				}
			}
			vrRender.active = qfalse;
			if ( vr.snapTurn == VR_SNAP_TURN_TAKEN ) {
				vr.snapTurn = VR_SNAP_TURN_DRAWN;	// (Snap Fade's turn drawn, black: it can lighten)
			}
			VR_Mirror( qtrue, qfalse );
			layer = copied ? 1 : 0;
		} else {
			// Menus/console/loading (shown on a floating screen), or the
			// headset is idle and only the desktop window shows anything.
			// The game still runs every frame either way.
			vrRender.active = drawVR;
			vrRender.screenLayer = qtrue;
			VR_BindTarget( 0 );
			SCR_DrawScreenField( STEREO_CENTER );
			re->EndFrame( NULL, NULL );
			VR_UnbindTarget();
			vrRender.active = qfalse;
			if ( drawVR && VR_CopyToSwapchain( 0 ) ) {
				layer = 2;
			}
			VR_Mirror( qfalse, drawVR ? qfalse : qtrue );
		}

		vrRender.offscreen = qfalse;
	} catch ( ... ) {
		VR_AbortFrame();
		throw;
	}
	vr.inUpdateScreen = qfalse;

	VR_EndFrame( layer );

	if ( vr_mirror->integer || !drawVR ) {
		// vsync on the mirror would cap the headset at the monitor's rate
		if ( drawVR && Cvar_VariableIntegerValue( "r_swapInterval" ) ) {
			Cvar_Set( "r_swapInterval", "0" );
		}
		window_t window;
		memset( &window, 0, sizeof( window ) );
		window.api = GRAPHICS_API_OPENGL;
		WIN_Present( &window );	// swap + fullscreen toggling, like the desktop path
	}
	return qtrue;
}

/*
================================================================================

Commands

================================================================================
*/

static void VR_Recenter_f( void ) {
	vr.recenterPending = qtrue;
	vr.lastScreenLayer = qfalse;	// re-place the menu screen too
	Com_Printf( "VR: recentred\n" );
}

// Set Height: how high your eyes are above the floor, standing up straight,
// becomes your height (vr_height) - felt in both hands, a short firm buzz;
// a long soft one, it couldn't (no floor, or not standing).
static void VR_SetHeight_f( void ) {
	if ( !VR_IsRunning() || !vr.poseValid ) {
		Com_Printf( "VR: Set Height needs your headset on\n" );
		return;
	}
	if ( !vr.floorValid || vr.floorHeadY < VR_HEIGHT_MIN || vr.floorHeadY > VR_HEIGHT_MAX ) {
		if ( !vr.floorValid ) {
			Com_Printf( "VR: your headset doesn't say where the floor is - set it up, or set Eye Height yourself (Setup > Controls, VR Movement)\n" );
		} else {
			Com_Printf( "VR: your eyes are %.2f m up - stand up straight, and Set Height again\n", vr.floorHeadY );
		}
		VR_Pulse( VR_HAND_LEFT, 0.4f, 0.2f );	// (a long soft buzz: not set)
		VR_Pulse( VR_HAND_RIGHT, 0.4f, 0.2f );
		return;
	}
	Cvar_Set( "vr_height", va( "%.2f", vr.floorHeadY ) );
	VR_Pulse( VR_HAND_LEFT, 0.1f, 0.5f );	// (a short firm one: set)
	VR_Pulse( VR_HAND_RIGHT, 0.1f, 0.5f );
	Com_Printf( "VR: your height set: your eyes %.2f m up\n", vr.floorHeadY );
}

static void VR_Status_f( void ) {
	if ( vr.instance == XR_NULL_HANDLE ) {
		Com_Printf( "VR: no OpenXR instance (vr_enabled %d)\n", vr_enabled ? vr_enabled->integer : 0 );
		return;
	}
	Com_Printf( "VR: runtime \"%s\", headset \"%s\"\n", vr.runtimeName, vr.systemName );
	Com_Printf( "VR: session %s, state %d, %s, %s%s\n",
		vr.session != XR_NULL_HANDLE ? "created" : "none", (int)vr.sessionState,
		vr.running ? "running" : "not running", vr.focused ? "focused" : "not focused",
		vr.lost ? " (lost - vid_restart to reconnect)" : "" );
	if ( vr.targetsReady ) {
		Com_Printf( "VR: eye buffers %dx%d, head yaw %.1f pitch %.1f, view %s, %s\n",
			vr.width, vr.height, vr.headYaw, vr.headPitch, vr.viewLocked ? "locked" : "head-aim",
			vglCopyImageSubData ? "copy" : "blit" );
		for ( int e = 0; e < 2; e++ ) {
			Com_Printf( "VR: eye %d fov L%.1f R%.1f U%.1f D%.1f, hud %d,%d %dx%d\n", e,
				RAD2DEG( vr.views[e].fov.angleLeft ), RAD2DEG( vr.views[e].fov.angleRight ),
				RAD2DEG( vr.views[e].fov.angleUp ), RAD2DEG( vr.views[e].fov.angleDown ),
				vrRender.hudX[e], vrRender.hudY[e], vrRender.hudW[e], vrRender.hudH[e] );
		}
	}
	{
		char floor[64] = "the floor not known";
		if ( vr.floorValid ) {
			Com_sprintf( floor, sizeof( floor ), "your eyes %.2f m above the floor", vr.floorHeadY );
		}
		Com_Printf( "VR: height %s, %s, %s; Real Crouch %s%s; the game's eyes %.0f below standing%s%s%s\n",
			VR_Height() > 0.0f ? va( "%.2f m", VR_Height() ) : "not set", vr_seated->integer ? "playing seated" : "standing", floor,
			vr_realCrouch->integer ? "on" : "off", vr.crouching ? " (crouched)" : "", vr.eyeDrop, vr.gamePosed ? ", in a pose" : "",
			vr.gameSwimming ? ", swimming" : "", vr.gameOwnEyes ? "" : ", the camera not at them" );
	}
	for ( int h = 0; h < 2; h++ ) {
		static const struct {
			int			bit;
			const char	*text;
		} senses[] = {
			{ VR_SENSE_TRIGGER_TOUCH, "the trigger touched" },
			{ VR_SENSE_TRIGGER_NEAR, "the finger near it" },
			{ VR_SENSE_INDEX_CURL, "the finger's curl" },
			{ VR_SENSE_THUMB_TOUCH, "the thumb on the controller" },
			{ VR_SENSE_THUMB_NEAR, "the thumb near it" },
		};
		const vrFingerSense_t *fs = &vr.fingers[h];
		char list[160] = "";
		for ( size_t i = 0; i < ARRAY_LEN( senses ); i++ ) {
			if ( fs->sensed & senses[i].bit ) {
				Q_strcat( list, sizeof( list ), list[0] ? ", " : "" );
				Q_strcat( list, sizeof( list ), senses[i].text );
			}
		}
		Com_Printf( "VR: %s hand: %s; senses %s (trigger %.2f, grip %.2f)\n", h == VR_HAND_RIGHT ? "right" : "left",
			vr.profile[h][0] ? vr.profile[h] : "no controller", list[0] ? list : "nothing of your fingers", fs->trigger, fs->squeeze );
	}
}

void VR_Init( void ) {
	static qboolean initialised = qfalse;
	if ( initialised ) {
		return;
	}
	initialised = qtrue;

	vr_enabled			= Cvar_Get( "vr_enabled",			"1",	CVAR_ARCHIVE | CVAR_LATCH, "Use a VR headset through OpenXR (needs vid_restart)" );
	vr_resolutionScale	= Cvar_Get( "vr_resolutionScale",	"1.0",	CVAR_ARCHIVE | CVAR_LATCH, "Eye buffer size relative to the headset's recommended size (needs vid_restart)" );
	vr_worldScale		= Cvar_Get( "vr_worldScale",		"34",	CVAR_ARCHIVE, "Game units per metre" );
	vr_positional		= Cvar_Get( "vr_positional",		"1",	CVAR_ARCHIVE, "Move the camera with your head (6DoF)" );
	vr_positionalMax	= Cvar_Get( "vr_positionalMax",		"0.25",	CVAR_ARCHIVE, "How far (metres) the camera may follow your head" );
	vr_hudFov			= Cvar_Get( "vr_hudFov",			"50",	CVAR_ARCHIVE, "Vertical size of the HUD in degrees" );
	vr_hudDepth			= Cvar_Get( "vr_hudDepth",			"3",	CVAR_ARCHIVE, "Distance of the HUD in metres" );
	vr_menuDistance		= Cvar_Get( "vr_menuDistance",		"3",	CVAR_ARCHIVE, "Distance of the menu screen in metres" );
	vr_menuHeight		= Cvar_Get( "vr_menuHeight",		"2.2",	CVAR_ARCHIVE, "Height of the menu screen in metres" );
	vr_headAim			= Cvar_Get( "vr_headAim",			"1",	CVAR_ARCHIVE, "Aim with your head; the mouse turns" );
	vr_mirror			= Cvar_Get( "vr_mirror",			"1",	CVAR_ARCHIVE, "Show the left eye in the desktop window" );
	vr_handAim			= Cvar_Get( "vr_handAim",			"1",	CVAR_ARCHIVE, "Aim guns with the hand holding them" );
	vr_turnMode			= Cvar_Get( "vr_turnMode",			"0",	CVAR_ARCHIVE, "Right stick turning: 0 = snap, 1 = smooth" );
	vr_turnAngle		= Cvar_Get( "vr_turnAngle",			"45",	CVAR_ARCHIVE, "Snap turn angle in degrees" );
	vr_turnSpeed		= Cvar_Get( "vr_turnSpeed",			"180",	CVAR_ARCHIVE, "Smooth turn speed in degrees per second" );
	vr_snapFade			= Cvar_Get( "vr_snapFade",			"0",	CVAR_ARCHIVE, "Snap Fade: the view dips to dark for a moment as you snap turn, turning you while it's dark" );
	vr_vignette			= Cvar_Get( "vr_vignette",			"0",	CVAR_ARCHIVE, "Comfort Vignette: the edges of your view darken while the game moves you - the stick, smooth turning, a jump, a fall - more the faster (0 = off, 1 = low, 2 = medium, 3 = high)" );
	vr_seated			= Cvar_Get( "vr_seated",			"0",	CVAR_ARCHIVE, "Play Seated: you sit, your character stands - your head as it is at Recenter (sitting up straight) is your standing height" );
	vr_height			= Cvar_Get( "vr_height",			"0",	CVAR_ARCHIVE, "How high your eyes are when you stand, in metres above the floor (vr_setHeight measures it; 0 = not set)" );
	vr_realCrouch		= Cvar_Get( "vr_realCrouch",		"1",	CVAR_ARCHIVE, "Real Crouch: your head below 70% of your height (vr_height) crouches your character" );
	vr_moveDirection	= Cvar_Get( "vr_moveDirection",		"0",	CVAR_ARCHIVE, "The moving stick moves you: 0 = where you look, 1 = where the hand holding it points" );
	vr_swapSticks		= Cvar_Get( "vr_swapSticks",		"0",	CVAR_ARCHIVE, "1: move with the right stick, turn and switch weapon with the left (their clicks swap too)" );
	vr_deadzone			= Cvar_Get( "vr_deadzone",			"0.2",	CVAR_ARCHIVE, "Thumbstick deadzone" );
	vr_gunPitch			= Cvar_Get( "vr_gunPitch",			"0",	CVAR_ARCHIVE, "Tilt guns in your hand (degrees, + = barrel up); also tilts the aim" );
	vr_leftHanded		= Cvar_Get( "vr_leftHanded",		"0",	CVAR_ARCHIVE, "Left-handed: guns and the saber in your left hand, the Force on the right grip and Use on the left" );
	vr_gripPress		= Cvar_Get( "vr_gripPress",			"0.85",	CVAR_ARCHIVE, "How far a grip must be pressed for Use and the Force (0.6 to 0.95): a lighter squeeze only curls your fingers" );
	vr_forceGestures	= Cvar_Get( "vr_forceGestures",		"1",	CVAR_ARCHIVE, "Force gestures with your Force hand (the Force grip's, empty): 1 = while the Force grip's held - thrust it out "
		"to Push, reach out and yank it back to Pull, hold it out still to Grip, its trigger for Lightning (palm up: Drain), a quick squeeze "
		"for the power picked; 2 = Open hand: Push and Pull with the hand open too; 0 = off (the Force grip uses the power picked)" );
	vr_forceThrust		= Cvar_Get( "vr_forceThrust",		"1.6",	CVAR_ARCHIVE, "Force gestures: how fast a thrust (Push) or a yank (Pull) goes at least, in metres a second (0.8 to 3)" );
	vr.pointerLeft = vr_leftHanded->integer ? qtrue : qfalse;	// (the menu's pointer: the hand you use)
	vr.forceHand = -1;	// (none yet)
	VRF_Clear( &vr.force );

	// tells a VR-aware cgame that trap->ext.VR_GetClientState exists
	Cvar_Get( "vr_engine", VR_ENGINE_TAG, CVAR_ROM, "OpenJK VR engine interface version" );
	Cvar_Set( "vr_engine", VR_ENGINE_TAG );

	VR_DefaultPoseWheelBind();

	Cmd_AddCommand( "vr_recenter", VR_Recenter_f, "Re-centre the VR position and menu screen" );
	Cmd_AddCommand( "vr_setHeight", VR_SetHeight_f, "Stand up straight: your eyes' height above the floor becomes vr_height" );
	Cmd_AddCommand( "vr_status", VR_Status_f, "Print VR runtime and headset status" );
}

void VR_Shutdown( void ) {
	VR_DestroySession();
	VR_DestroyInstance();
}

#else // !_WIN32

// VR is only implemented for Windows (OpenGL + Win32 OpenXR binding) so far.
static vrRenderState_t vrRender;
void VR_Init( void ) {}
void VR_Shutdown( void ) {}
qboolean VR_CreateSession( int *eyeWidth, int *eyeHeight ) { return qfalse; }
void VR_DestroySession( void ) {}
qboolean VR_IsActive( void ) { return qfalse; }
qboolean VR_IsRunning( void ) { return qfalse; }
qboolean VR_IsPacingFrames( void ) { return qfalse; }
qboolean VR_WantSession( void ) { return qfalse; }
void VR_BeginFrame( void ) {}
void VR_UICursorMoved( int dx, int dy ) {}
void VR_UIStarted( void ) {}
qboolean VR_UpdateScreen( void ) { return qfalse; }
void VR_ApplyHeadAim( void ) {}
void VR_AddMovement( usercmd_t *cmd ) {}
static vrClientState_t vrClient;
const vrClientState_t *VR_GetClientState( void ) { return &vrClient; }
const vrRenderState_t *VR_GetRenderState( void ) { return &vrRender; }
void VR_SetUserPose( const vrUserPose_t *pose ) {}
void VR_FillCmdPose( usercmd_t *cmd, vrUserPose_t *out ) { Com_Memset( out, 0, sizeof( *out ) ); }
void VR_Haptic( int hand, float seconds, float amplitude ) {}
void VR_SetGrabZones( int handMask ) {}
void VR_SetWeaponHand( int hand ) {}
void VR_SetCmdButtons( int buttons ) {}
void VR_SetEyeDrop( float drop, int flags ) {}
void VR_GenericCmd( int genCmd ) {}
void VR_DrawOverlay( void ) {}

#endif // _WIN32

#endif // OPENJK_VR
