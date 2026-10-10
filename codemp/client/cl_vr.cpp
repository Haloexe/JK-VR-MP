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
	X( xrGetOpenGLGraphicsRequirementsKHR )

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
} vrState_t;

static vrState_t		vr;
static vrRenderState_t	vrRender;

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

// Heading that stays well defined when looking straight up or down.
// Must match R_VR_AxisYaw in the renderer.
static float VR_AxisYaw( vec3_t axis[3] ) {
	const float hx = axis[2][2] * axis[0][0] - axis[0][2] * axis[2][0];
	const float hy = axis[2][2] * axis[0][1] - axis[0][2] * axis[2][1];
	return RAD2DEG( atan2f( hy, hx ) );
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

static void VR_DestroyInstance( void ) {
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

// Whether the head yaw is already part of the game's camera yaw (head-aim).
// When it isn't (following someone, dead, intermission, demo playback,
// paused, head-aim off) the head yaw is applied on top of the game's camera.
static qboolean VR_ViewIsLocked( void ) {
	if ( !vr_headAim->integer ) {
		return qtrue;
	}
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

	// Which tracking yaw lines up with the game's camera yaw
	const qboolean locked = VR_ViewIsLocked();
	if ( locked && !vr.viewLocked ) {
		vr.lockYaw = vr.headYaw;
	}
	vr.viewLocked = locked;
	vrRender.referenceYaw = locked ? vr.lockYaw : vr.headYaw;

	vrRender.worldScale = Com_Clamp( 1.0f, 1000.0f, vr_worldScale->value );

	const float fov = Cvar_VariableValue( "cg_fov" );
	vrRender.normalFov = Com_Clamp( 1.0f, 130.0f, ( fov > 1.0f ) ? fov : 80.0f );
	vrRender.zoomed = ( cls.state == CA_ACTIVE && cl.snap.valid && cl.snap.ps.zoomMode ) ? qtrue : qfalse;
}

void VR_BeginFrame( void ) {
	if ( vr.frameBegun ) {
		vr.paced = qfalse;	// last frame never reached SCR_UpdateScreen
		return;
	}
	vr.paced = qfalse;

	VR_PollEvents();	// only between frames
	if ( vr.session == XR_NULL_HANDLE || !vr.running ) {
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

/*
================================================================================

Head aim

================================================================================
*/

void VR_ApplyHeadAim( void ) {
	if ( !VR_IsRunning() || !vr.poseValid ) {
		vr.haveLastYaw = qfalse;
		return;
	}

	// Track yaw every frame so nothing jumps when leaving a menu.
	if ( !vr.haveLastYaw ) {
		vr.lastAimYaw = vr.headYaw;
		vr.haveLastYaw = qtrue;
	}
	const float deltaYaw = AngleSubtract( vr.headYaw, vr.lastAimYaw );
	vr.lastAimYaw = vr.headYaw;

	if ( !vr_headAim->integer || VR_UseScreenLayer() ) {
		return;
	}

	// Mouse/keys keep turning the body; the head adds on top of that.
	cl.viewangles[YAW] += deltaYaw;

	// Pitch comes straight from the headset. Compensate the server's
	// delta_angles so the in-game pitch equals the real head pitch.
	const float pitch = Com_Clamp( -88.0f, 88.0f, vr.headPitch );
	cl.viewangles[PITCH] = pitch - SHORT2ANGLE( cl.snap.ps.delta_angles[PITCH] );
}

const vrRenderState_t *VR_GetRenderState( void ) {
	return &vrRender;
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

	VR_BeginFrame();

	const qboolean inXrFrame = vr.frameBegun;
	const qboolean drawVR = ( inXrFrame && vr.frameState.shouldRender ) ? qtrue : qfalse;
	const qboolean screenLayer = VR_UseScreenLayer();
	int layer = 0;

	if ( drawVR ) {
		if ( screenLayer && !vr.lastScreenLayer ) {
			VR_PlaceMenuQuad();
		}
		vr.lastScreenLayer = screenLayer;
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
qboolean VR_UpdateScreen( void ) { return qfalse; }
void VR_ApplyHeadAim( void ) {}
const vrRenderState_t *VR_GetRenderState( void ) { return &vrRender; }

#endif // _WIN32

#endif // OPENJK_VR
