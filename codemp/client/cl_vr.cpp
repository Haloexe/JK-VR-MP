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
	X( xrApplyHapticFeedback )

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
static cvar_t *vr_moveDirection;
static cvar_t *vr_deadzone;
static cvar_t *vr_gunPitch;

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
	return qtrue;
}

/*
================================================================================

State

================================================================================
*/

// Controller buttons arrive as ordinary bindable keys AUX16..AUX27 (AUX0+
// are used by extra mouse buttons), so players can rebind them like any key.
#define VR_FIRST_KEY	( A_AUX0 + 16 )
enum {
	VR_KEY_TRIGGER_R,		// AUX16
	VR_KEY_TRIGGER_L,		// AUX17
	VR_KEY_GRIP_R,			// AUX18
	VR_KEY_GRIP_L,			// AUX19
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

static const char *vrDefaultBinds[VR_NUM_KEYS] = {
	"+attack",				// right trigger
	"+altattack",			// left trigger
	"+use",					// right grip
	"+useforce",			// left grip
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
	XrSpace					gripSpace[2], aimSpace[2];
	XrPosef					gripPose[2], aimPose[2];
	qboolean				handValid[2];
	int						handTrackTime[2];	// last time each hand was tracked (ms)
	float					moveX, moveY;	// left stick
	float					turnX, turnY;	// right stick
	qboolean				turnLatched;	// snap turn fired, waiting for the stick to recentre
	qboolean				weapLatched;	// stick-up/down weapon switch fired
	float					pendingTurn;	// degrees to add to the body yaw this frame
	qboolean				keyDown[VR_NUM_BUTTONS];
	int						keySent[VR_NUM_BUTTONS];	// key each held button sent when pressed (0 = none)
	qboolean				keyBlocked[VR_NUM_BUTTONS];	// held through a focus loss: ignored until let go
	qboolean				grabbing[2];	// the hand's grip is held as a grab, not its key
	int						grabZones;		// hands the cgame says could grab now (bit per hand)...
	int						grabZoneTime;	// ...as of this cls.realtime
	qboolean				topOfFrame;		// this poll runs at the top of CL_Frame (not nested)

	// menu pointer: a controller points at the menu screen like a mouse
	qboolean				pointerMode;	// the menu screen is up and takes the pointer
	qboolean				pointerHit;		// the pointing controller is on the screen
	qboolean				pointerLeft;	// the left hand points (the last to pull its trigger)
	int						pointerLastHand;
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

	// aim source this frame: head, or the right hand (ranged weapons)
	qboolean				aimFromHand;
	float					handAimYaw, handAimPitch;
	int						handSeenTime;	// last time the right hand was tracked (ms)
	int						cgameQueryTime;	// last time the cgame read the VR state (cls.realtime)
	int						lastPollTime;	// for smooth turning (ms)
	float					lastRefYaw;		// referenceYaw of the previous usercmd
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

	const char *extensions[] = { XR_KHR_OPENGL_ENABLE_EXTENSION_NAME };

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
	ici.enabledExtensionCount = ARRAY_LEN( extensions );
	ici.enabledExtensionNames = extensions;

	XrResult res = xrCreateInstance( &ici, &vr.instance );
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

static void VR_SuggestBindings( const char *profile, const vrBinding_t *bindings, int count ) {
	XrActionSuggestedBinding sb[32];
	int n = 0;
	for ( int i = 0; i < count && n < (int)ARRAY_LEN( sb ); i++ ) {
		if ( *bindings[i].action == XR_NULL_HANDLE ) {
			continue;
		}
		sb[n].action = *bindings[i].action;
		sb[n].binding = VR_Path( bindings[i].path );
		n++;
	}

	XrInteractionProfileSuggestedBinding ipsb;
	memset( &ipsb, 0, sizeof( ipsb ) );
	ipsb.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
	ipsb.interactionProfile = VR_Path( profile );
	ipsb.countSuggestedBindings = n;
	ipsb.suggestedBindings = sb;
	XrResult res = xrSuggestInteractionProfileBindings( vr.instance, &ipsb );
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

	// Meta Quest (1/2/3/Pro) Touch and Touch Plus controllers
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
	VR_SuggestBindings( "/interaction_profiles/oculus/touch_controller", touch, ARRAY_LEN( touch ) );

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
	VR_SuggestBindings( "/interaction_profiles/valve/index_controller", index, ARRAY_LEN( index ) );

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
	VR_SuggestBindings( "/interaction_profiles/htc/vive_controller", vive, ARRAY_LEN( vive ) );

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
	VR_SuggestBindings( "/interaction_profiles/microsoft/motion_controller", wmr, ARRAY_LEN( wmr ) );

	// Fallback for anything else
	const vrBinding_t simple[] = {
		VR_BOTH_HAPTICS( vr.actHaptic ),
		VR_BOTH_HANDS( vr.actGripPose, "grip/pose" ),
		VR_BOTH_HANDS( vr.actAimPose, "aim/pose" ),
		VR_BOTH_HANDS( vr.actTrigger, "select/click" ),
		{ &vr.actMenu, "/user/hand/left/input/menu/click" },
	};
	VR_SuggestBindings( "/interaction_profiles/khr/simple_controller", simple, ARRAY_LEN( simple ) );

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
	}
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

// A controller trigger is holding the menu's mouse button down (a click, or
// dragging a slider or scroll bar).
static qboolean VR_Clicking( void ) {
	for ( int k = 0; k < VR_NUM_BUTTONS; k++ ) {
		if ( vr.keySent[k] == A_MOUSE1 ) {
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
		// click where the controller points (pointing past the screen clicks nothing)
		return vr.pointerHit ? A_MOUSE1 : 0;
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

// The hand whose grip button this is, or -1.
static int VR_GripHand( int key ) {
	return ( key == VR_KEY_GRIP_L ) ? VR_HAND_LEFT : ( key == VR_KEY_GRIP_R ) ? VR_HAND_RIGHT : -1;
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
		if ( down && !vr.pointerMode && cls.state == CA_ACTIVE && ( vr.grabZones & ( 1 << gripHand ) ) &&
			cls.realtime - vr.grabZoneTime >= 0 && cls.realtime - vr.grabZoneTime < 200 ) {
			vr.grabbing[gripHand] = qtrue;
			vr.keySent[key] = 0;
			return;
		}
	}

	if ( !down ) {
		// the release goes wherever the press went, even if the menu
		// opened or closed in between
		if ( vr.keySent[key] ) {
			VR_QueueKey( vr.keySent[key], qfalse );
			vr.keySent[key] = 0;
		}
		return;
	}

	int code = VR_ButtonKeyCode( key );
	for ( int k = 0; k < VR_NUM_BUTTONS && code; k++ ) {
		if ( k != key && vr.keySent[k] == code ) {
			code = 0;	// another held button already sent it (both triggers clicking)
		}
	}
	vr.keySent[key] = code;
	if ( !code ) {
		return;
	}
	VR_QueueKey( code, qtrue );
	if ( code == A_MOUSE1 ) {
		vr.pointerForce = qtrue;	// click exactly where it points, even if the mouse moved
		VR_Pulse( vr.pointerLastHand, 0.02f, 0.5f );
	}
}

static void VR_TapKey( int key ) {
	VR_SetKey( key, qtrue );
	VR_SetKey( key, qfalse );
}

// Lets go of every held controller button (focus lost, session ended).
// Whatever is still held when input comes back is not a new press.
static void VR_ReleaseAllKeys( void ) {
	for ( int k = 0; k < VR_NUM_BUTTONS; k++ ) {
		if ( vr.keyDown[k] ) {
			VR_SetKey( k, qfalse );
			vr.keyBlocked[k] = qtrue;
		}
	}
	vr.moveX = vr.moveY = vr.turnX = vr.turnY = 0.0f;
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

While the menu screen is up, the controller that last pulled its trigger
points at it like a mouse: the trigger clicks, B goes back, either stick
scrolls. The UI module only knows relative mouse moves, so the engine keeps
a copy of its cursor (VR_UICursorMoved) and moves it by the difference.

================================================================================
*/

static qboolean VR_UseScreenLayer( void );

// The UI module's cursor moves only by relative mouse moves, clamped to the
// 640x480 menu like UI_MouseEvent does. Every move goes through
// UIVM_MouseEvent, which reports it here.
void VR_UICursorMoved( int dx, int dy ) {
	vr.uiCursorX = Com_Clampi( 0, SCREEN_WIDTH, vr.uiCursorX + dx );
	vr.uiCursorY = Com_Clampi( 0, SCREEN_HEIGHT, vr.uiCursorY + dy );
}

// Whether a controller drives the menu cursor right now: a menu (or the
// engine's download prompt) is on the menu screen, and the console isn't.
static qboolean VR_MenuPointerWanted( void ) {
	if ( !vr.lastScreenLayer || !VR_UseScreenLayer() ) {
		return qfalse;	// no menu screen (or it hasn't been placed yet)
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
	const float h = Com_Clamp( 0.2f, 20.0f, vr_menuHeight->value );	// as VR_EndFrame sizes it
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

// A trigger that crosses this clicks (VR_AnalogKey).
#define VR_TRIGGER_PRESS	0.6f

// Where the menu cursor goes this frame. Runs before the buttons are read,
// which click where it points.
static void VR_UpdateMenuPointer( const float trigger[2], qboolean wanted ) {
	const qboolean wasOnScreen = (qboolean)( vr.pointerMode && vr.pointerHit );
	if ( wanted && !vr.pointerMode ) {
		vr.scrollLatched = qtrue;	// a stick already pushed (walking) doesn't scroll
	}
	vr.pointerMode = wanted;
	vr.pointerHit = qfalse;
	vr.pointerFresh = qtrue;
	vr.pressing = qfalse;
	if ( !vr.pointerMode ) {
		for ( int h = 0; h < 2; h++ ) {
			vr.pullArmed[h] = vr.pulling[h] = qfalse;
		}
		return;
	}

	// Pulling a trigger tips the controller a little, so from the moment a
	// pull starts (a rise from rest) until it clicks, at most 300 ms, the
	// cursor holds still.
	const int now = Sys_Milliseconds();
	qboolean pull[2];
	for ( int h = 0; h < 2; h++ ) {
		const int key = ( h == VR_HAND_LEFT ) ? VR_KEY_TRIGGER_L : VR_KEY_TRIGGER_R;
		const qboolean up = (qboolean)( !vr.keyDown[key] && !vr.keyBlocked[key] );
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

	// Pulling a trigger hands the pointer to that hand, unless the other
	// hand is busy clicking (dragging a slider). Both at once: no change.
	if ( pull[VR_HAND_LEFT] && !pull[VR_HAND_RIGHT] && vr.keySent[VR_KEY_TRIGGER_R] != A_MOUSE1 ) {
		vr.pointerLeft = qtrue;
	} else if ( pull[VR_HAND_RIGHT] && !pull[VR_HAND_LEFT] && vr.keySent[VR_KEY_TRIGGER_L] != A_MOUSE1 ) {
		vr.pointerLeft = qfalse;
	}

	// The pointing hand; a short tracking dropout just holds the cursor
	// rather than jumping to wherever the other hand happens to point.
	int hand = vr.pointerLeft ? VR_HAND_LEFT : VR_HAND_RIGHT;
	if ( !vr.handValid[hand] ) {
		const int other = ( hand == VR_HAND_LEFT ) ? VR_HAND_RIGHT : VR_HAND_LEFT;
		if ( now - vr.handTrackTime[hand] < 500 || !vr.handValid[other] ) {
			return;
		}
		hand = other;
	}
	const qboolean hold = (qboolean)( vr.pulling[hand] && now - vr.pullStart[hand] < 300 );

	float x, y;
	if ( !VR_PointerHit( hand, &x, &y ) ) {
		return;
	}
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

// A stick held up or down in the menu turns the mouse wheel: one step, a
// pause, then repeating. (stickX/Y: whichever stick is pushed further.)
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

// Analog trigger/grip to a button, with hysteresis so it doesn't chatter.
static void VR_AnalogKey( int key, float value ) {
	VR_SetKey( key, (qboolean)( value > VR_TRIGGER_PRESS || ( vr.keyDown[key] && value > 0.4f ) ) );
}

static qboolean VR_GameOwnsView( void );

static void VR_PollInput( void ) {
	if ( !vr.inputAttached ) {
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
		// not focused (system menu open, headset off): drop all input
		VR_ReleaseAllKeys();
		vr.handValid[0] = vr.handValid[1] = qfalse;
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
		}
	}

	// sticks
	VR_GetVec2( vr.actMove, &vr.moveX, &vr.moveY );
	VR_GetVec2( vr.actTurn, &vr.turnX, &vr.turnY );

	float trigger[2];
	trigger[VR_HAND_LEFT] = VR_GetFloat( vr.actTrigger, vr.handPath[VR_HAND_LEFT] );
	trigger[VR_HAND_RIGHT] = VR_GetFloat( vr.actTrigger, vr.handPath[VR_HAND_RIGHT] );

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
			right ? VR_HAND_RIGHT : VR_HAND_LEFT );
		vr.weapLatched = qtrue;	// a stick still held as the menu closes doesn't switch weapons
	} else {
		vr.scrollDir = 0;
	}

	// buttons -> bindable keys (in the menu, see VR_ButtonKeyCode)
	VR_AnalogKey( VR_KEY_TRIGGER_R, trigger[VR_HAND_RIGHT] );
	VR_AnalogKey( VR_KEY_TRIGGER_L, trigger[VR_HAND_LEFT] );
	VR_AnalogKey( VR_KEY_GRIP_R, VR_GetFloat( vr.actSqueeze, vr.handPath[VR_HAND_RIGHT] ) );
	VR_AnalogKey( VR_KEY_GRIP_L, VR_GetFloat( vr.actSqueeze, vr.handPath[VR_HAND_LEFT] ) );
	VR_SetKey( VR_KEY_A, VR_GetBool( vr.actA, XR_NULL_PATH ) );
	VR_SetKey( VR_KEY_B, VR_GetBool( vr.actB, XR_NULL_PATH ) );
	VR_SetKey( VR_KEY_X, VR_GetBool( vr.actX, XR_NULL_PATH ) );
	VR_SetKey( VR_KEY_Y, VR_GetBool( vr.actY, XR_NULL_PATH ) );
	VR_SetKey( VR_KEY_STICK_L, VR_GetBool( vr.actStickClick, vr.handPath[VR_HAND_LEFT] ) );
	VR_SetKey( VR_KEY_STICK_R, VR_GetBool( vr.actStickClick, vr.handPath[VR_HAND_RIGHT] ) );
	VR_SetKey( VR_BUTTON_MENU, VR_GetBool( vr.actMenu, XR_NULL_PATH ) );	// Escape

	// right stick: left/right turns, up/down switches weapon (in the menu
	// it scrolls instead, and turning is dropped by VR_ApplyHeadAim)
	if ( VR_GameOwnsView() ) {
		vr.pendingTurn = 0.0f;	// e.g. paused: don't pile up turns
	}
	const float ax = fabsf( vr.turnX ), ay = fabsf( vr.turnY );
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
		// smooth turn
		if ( ax > ay ) {
			vr.pendingTurn -= vr.turnX * vr_turnSpeed->value * dt;
		}
	} else {
		// snap turn
		if ( ax > ay && ax > 0.7f ) {
			if ( !vr.turnLatched ) {
				vr.pendingTurn -= ( vr.turnX > 0.0f ? 1.0f : -1.0f ) * Com_Clamp( 1.0f, 180.0f, vr_turnAngle->value );
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
	// The headset decides the field of view; keep the cgame's fov plain.
	Cvar_Set( "cg_fovAspectAdjust", "0" );
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

static qboolean VR_UseScreenLayer( void ) {
	if ( cls.state != CA_ACTIVE ) {
		return qtrue;
	}
	if ( Key_GetCatcher() & ( KEYCATCH_UI | KEYCATCH_CONSOLE ) ) {
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

// Ranged weapons aim with the right hand; the saber, fists, scopes, vehicles
// and emplaced guns keep aiming with the head.
static qboolean VR_WantHandAim( void ) {
	// a short tracking dropout doesn't flip the aim back to the head
	if ( !vr_handAim->integer || Sys_Milliseconds() - vr.handSeenTime > 500 ) {
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

	if ( vr.recenterPending && vr.poseValid ) {
		vr.positionOrigin = vr.headPose.position;
		vr.recenterPending = qfalse;
	}

	// Head angles in tracking space
	vec3_t headAxis[3];
	VR_QuatToAxis( &vr.headPose.orientation, headAxis );
	vr.headYaw = VR_AxisYaw( headAxis );
	vr.headPitch = RAD2DEG( asinf( Com_Clamp( -1.0f, 1.0f, -headAxis[0][2] ) ) );

	// Positional offset of the head from the recentred origin (clamped)
	const float headOfs[3] = {
		vr.headPose.position.x - vr.positionOrigin.x,
		vr.headPose.position.y - vr.positionOrigin.y,
		vr.headPose.position.z - vr.positionOrigin.z
	};
	const float len = sqrtf( headOfs[0] * headOfs[0] + headOfs[1] * headOfs[1] + headOfs[2] * headOfs[2] );
	const float maxLen = Q_max( 0.0f, vr_positionalMax->value );
	float posScale = vr_positional->integer ? 1.0f : 0.0f;
	if ( len > maxLen && len > 0.0f ) {
		posScale *= maxLen / len;
	}

	for ( int e = 0; e < 2; e++ ) {
		const XrPosef *p = &vr.views[e].pose;
		VR_QuatToAxis( &p->orientation, vrRender.eyeAxis[e] );

		// eye = (clamped head offset) + (eye relative to head, i.e. IPD)
		const float eyePos[3] = {
			headOfs[0] * posScale + ( p->position.x - vr.headPose.position.x ),
			headOfs[1] * posScale + ( p->position.y - vr.headPose.position.y ),
			headOfs[2] * posScale + ( p->position.z - vr.headPose.position.z )
		};
		VR_XrToQuake( eyePos, vrRender.eyeOrigin[e] );

		// Tell the compositor the pose the image is really rendered from.
		vr.submitPose[e].orientation = p->orientation;
		vr.submitPose[e].position.x = vr.positionOrigin.x + eyePos[0];
		vr.submitPose[e].position.y = vr.positionOrigin.y + eyePos[1];
		vr.submitPose[e].position.z = vr.positionOrigin.z + eyePos[2];

		vrRender.tanLeft[e] = tanf( vr.views[e].fov.angleLeft );
		vrRender.tanRight[e] = tanf( vr.views[e].fov.angleRight );
		vrRender.tanUp[e] = tanf( vr.views[e].fov.angleUp );
		vrRender.tanDown[e] = tanf( vr.views[e].fov.angleDown );
	}

	for ( int e = 0; e < 2; e++ ) {
		VR_ComputeHudRect( e );
	}

	// Controllers (buttons become key events, poses are sampled)
	VR_PollInput();

	// Hands in the same space as the eyes. A hand that briefly loses
	// tracking keeps its last pose for a moment.
	if ( vr.handValid[VR_HAND_RIGHT] ) {
		vr.handSeenTime = Sys_Milliseconds();
	}
	for ( int h = 0; h < 2; h++ ) {
		if ( !vr.handValid[h] ) {
			if ( h == VR_HAND_LEFT || Sys_Milliseconds() - vr.handSeenTime > 500 ) {
				vrClient.handValid[h] = qfalse;
			}
			continue;
		}
		vrClient.handValid[h] = qtrue;
		const XrPosef *g = &vr.gripPose[h], *a = &vr.aimPose[h];
		const float gp[3] = {
			headOfs[0] * posScale + ( g->position.x - vr.headPose.position.x ),
			headOfs[1] * posScale + ( g->position.y - vr.headPose.position.y ),
			headOfs[2] * posScale + ( g->position.z - vr.headPose.position.z )
		};
		const float ap[3] = {
			headOfs[0] * posScale + ( a->position.x - vr.headPose.position.x ),
			headOfs[1] * posScale + ( a->position.y - vr.headPose.position.y ),
			headOfs[2] * posScale + ( a->position.z - vr.headPose.position.z )
		};
		VR_XrToQuake( gp, vrClient.gripOrigin[h] );
		VR_XrToQuake( ap, vrClient.aimOrigin[h] );
		VR_QuatToAxis( &g->orientation, vrClient.gripAxis[h] );
		VR_QuatToAxis( &a->orientation, vrClient.aimAxis[h] );
	}
	if ( vr.handValid[VR_HAND_RIGHT] ) {
		// optional tilt of the gun (and its aim) in the hand; + = barrel up
		const float tilt = vr_gunPitch->value;
		if ( tilt != 0.0f ) {
			vec3_t *ax = vrClient.aimAxis[VR_HAND_RIGHT];
			vec3_t f, u;
			RotatePointAroundVector( f, ax[1], ax[0], -tilt );
			RotatePointAroundVector( u, ax[1], ax[2], -tilt );
			VectorCopy( f, ax[0] );
			VectorCopy( u, ax[2] );
		}
		vr.handAimYaw = VR_ForwardYaw( vrClient.aimAxis[VR_HAND_RIGHT], vr.handAimYaw );
		vr.handAimPitch = RAD2DEG( asinf( Com_Clamp( -1.0f, 1.0f, -vrClient.aimAxis[VR_HAND_RIGHT][0][2] ) ) );
	}

	// Who aims, and which tracking yaw lines up with the game camera's yaw:
	//  - game owns the view (dead, spectating, ...): yaw frozen at that moment
	//  - right hand aims (ranged weapon): the hand's yaw
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
	vrClient.referenceYaw = vrRender.referenceYaw;
	vrClient.worldScale = vrRender.worldScale;
	const float hp[3] = { headOfs[0] * posScale, headOfs[1] * posScale, headOfs[2] * posScale };
	VR_XrToQuake( hp, vrClient.headOrigin );
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
	if ( !VR_IsRunning() || !vr.inputAttached || VR_UseScreenLayer() || ( Key_GetCatcher() & KEYCATCH_MESSAGE ) ) {
		return;
	}
	if ( vr.moveX == 0.0f && vr.moveY == 0.0f ) {
		return;
	}

	// The stick moves relative to where you look (or where your left hand
	// points); the usercmd is relative to the game camera's yaw.
	float moveYaw = vr.headYaw;
	if ( vr_moveDirection->integer == 1 && vr.handValid[VR_HAND_LEFT] ) {
		moveYaw = VR_ForwardYaw( vrClient.aimAxis[VR_HAND_LEFT], vr.headYaw );
	}
	const float rel = DEG2RAD( AngleSubtract( moveYaw, vrRender.referenceYaw ) );
	const float c = cosf( rel ), sn = sinf( rel );
	const float fwd = vr.moveY * c + vr.moveX * sn;
	const float right = -vr.moveY * sn + vr.moveX * c;

	// walking (+speed with cl_run, or cl_run 0) caps the speed like the keys do
	const float speed = ( cmd->buttons & BUTTON_WALKING ) ? 64.0f : 127.0f;
	cmd->forwardmove = ClampChar( cmd->forwardmove + (int)( fwd * speed ) );
	cmd->rightmove = ClampChar( cmd->rightmove + (int)( right * speed ) );
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
	if ( !pose || !pose->valid ) {
		Com_Memset( &vrUserPose, 0, sizeof( vrUserPose ) );
		return;
	}
	vrUserPose = *pose;
	vrUserPoseTime = cls.realtime;
}

void VR_FillCmdPose( usercmd_t *cmd, vrUserPose_t *out ) {
	Com_Memset( out, 0, sizeof( *out ) );
	if ( !VR_IsRunning() || cls.state != CA_ACTIVE || !vrUserPose.valid ) {
		return;	// no lit saber in your VR hands
	}
	const int age = cls.realtime - vrUserPoseTime;
	if ( age < 0 || age > 1000 ) {
		return;	// the cgame stopped reporting long ago
	}
	if ( age <= 100 ) {
		*out = vrUserPose;
	} else {
		// No fresh report (a hitch): the saber is still in your hands, but
		// where its blades were then is no use to the server any more.
		out->valid = qtrue;
		out->trackYaw = vrUserPose.trackYaw;
	}

	// When the server's game fights with your real blade ("vrSaber 1" in its
	// serverinfo), your swings are the attacks: the trigger doesn't also
	// start the game's canned swing animations. (Elsewhere it still does.)
	// The server does the same with every command that has a pose.
	if ( clc.vrPoseExt ) {
		const char *serverInfo = cl.gameState.stringData + cl.gameState.stringOffsets[CS_SERVERINFO];
		if ( atoi( Info_ValueForKey( serverInfo, "vrSaber" ) ) > 0 ) {
			cmd->buttons &= ~BUTTON_ATTACK;
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
	vr.grabZoneTime = cls.realtime;
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

static void VR_PlaceMenuQuad( void ) {
	// Snapshot the head when a menu appears and put the quad in front of it.
	vec3_t axis[3];
	VR_QuatToAxis( &vr.headPose.orientation, axis );
	const float yaw = DEG2RAD( VR_AxisYaw( axis ) );
	const float dist = Com_Clamp( 0.5f, 20.0f, vr_menuDistance->value );

	vr.quadPose.position.x = vr.headPose.position.x - sinf( yaw ) * dist;
	vr.quadPose.position.y = vr.headPose.position.y;
	vr.quadPose.position.z = vr.headPose.position.z - cosf( yaw ) * dist;
	vr.quadPose.orientation.x = 0.0f;
	vr.quadPose.orientation.y = sinf( yaw * 0.5f );
	vr.quadPose.orientation.z = 0.0f;
	vr.quadPose.orientation.w = cosf( yaw * 0.5f );
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
		const float h = Com_Clamp( 0.2f, 20.0f, vr_menuHeight->value );
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
		}
		vr.lastScreenLayer = screenLayer;
	}
	if ( screenLayer ) {
		vrClient.active = qfalse;	// flat view under the menu: no gun-in-hand
	}

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
				VR_UnbindTarget();
				if ( !VR_CopyToSwapchain( eye ) ) {
					copied = qfalse;
				}
			}
			vrRender.active = qfalse;
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
	vr_handAim			= Cvar_Get( "vr_handAim",			"1",	CVAR_ARCHIVE, "Aim guns with your right hand" );
	vr_turnMode			= Cvar_Get( "vr_turnMode",			"0",	CVAR_ARCHIVE, "Right stick turning: 0 = snap, 1 = smooth" );
	vr_turnAngle		= Cvar_Get( "vr_turnAngle",			"45",	CVAR_ARCHIVE, "Snap turn angle in degrees" );
	vr_turnSpeed		= Cvar_Get( "vr_turnSpeed",			"180",	CVAR_ARCHIVE, "Smooth turn speed in degrees per second" );
	vr_moveDirection	= Cvar_Get( "vr_moveDirection",		"0",	CVAR_ARCHIVE, "Left stick moves: 0 = where you look, 1 = where your left hand points" );
	vr_deadzone			= Cvar_Get( "vr_deadzone",			"0.2",	CVAR_ARCHIVE, "Thumbstick deadzone" );
	vr_gunPitch			= Cvar_Get( "vr_gunPitch",			"0",	CVAR_ARCHIVE, "Tilt guns in your hand (degrees, + = barrel up); also tilts the aim" );

	// tells a VR-aware cgame that trap->ext.VR_GetClientState exists
	Cvar_Get( "vr_engine", VR_ENGINE_TAG, CVAR_ROM, "OpenJK VR engine interface version" );
	Cvar_Set( "vr_engine", VR_ENGINE_TAG );

	Cmd_AddCommand( "vr_recenter", VR_Recenter_f, "Re-centre the VR position and menu screen" );
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

#endif // _WIN32

#endif // OPENJK_VR
