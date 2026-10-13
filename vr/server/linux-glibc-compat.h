/*
 * OpenJK VR: built into every source file of the Linux server build
 * (vr/build-linux-server.sh, gcc -include) so a server built on a new
 * distribution still runs on older ones (glibc 2.34+: Ubuntu 22.04, Debian 12,
 * RHEL 9 and newer). linux-glibc-compat.c covers what the static libstdc++
 * brings in; the build script checks the result.
 *
 * This file is part of the OpenJK source code and is licensed under the GNU
 * General Public License version 2, like the rest of OpenJK.
 */
#include <features.h>

/*
 * From glibc 2.38, C++ (which always has _GNU_SOURCE) gets the C23 versions
 * of strtol and the scanf family (__isoc23_strtol, __isoc23_sscanf...), which
 * only exist in glibc 2.38 and later. The game parses nothing that needs C23's
 * binary literals, so keep the classic ones. (glibc 2.38-2.39 call the switch
 * C2X_STRTOL, later ones C23_STRTOL.)
 */
#undef __GLIBC_USE_C2X_STRTOL
#define __GLIBC_USE_C2X_STRTOL 0
#undef __GLIBC_USE_C23_STRTOL
#define __GLIBC_USE_C23_STRTOL 0

/*
 * glibc 2.38 gave fmod and fmodf new versions (same results, faster); link
 * against the versions every glibc has.
 */
#if defined( __x86_64__ )
#define VR_GLIBC_BASE "GLIBC_2.2.5"
#elif defined( __aarch64__ )
#define VR_GLIBC_BASE "GLIBC_2.17"
#endif
#ifdef VR_GLIBC_BASE
__asm__( ".symver fmod,fmod@" VR_GLIBC_BASE );
__asm__( ".symver fmodf,fmodf@" VR_GLIBC_BASE );
#endif
