/*
 * OpenJK VR: linked into the Linux server build (vr/build-linux-server.sh) so
 * it runs on glibc 2.34 and newer wherever it was built.
 *
 * libstdc++ is linked in statically (older distributions have an older one),
 * and a libstdc++ built on a newer glibc calls two functions that older ones
 * don't have. These stand in for them inside the server's own files (hidden:
 * nothing else sees them), so the server never asks glibc for them.
 *
 * This file is part of the OpenJK source code and is licensed under the GNU
 * General Public License version 2, like the rest of OpenJK.
 */
#include <stdint.h>
#include <sys/types.h>
#include <sys/random.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

#define VR_HIDDEN __attribute__(( visibility( "hidden" ) ))

/* the classic one, in every glibc (declared here, not from stdlib.h, which
   would redirect it to the C23 one when built on glibc 2.38+) */
unsigned long strtoul( const char *nptr, char **endptr, int base );

/* glibc 2.38: libstdc++ reads its GLIBCXX_TUNABLES setting with it */
VR_HIDDEN unsigned long __isoc23_strtoul( const char *nptr, char **endptr, int base ) {
	return strtoul( nptr, endptr, base );
}

/* glibc 2.36: std::random_device's default source (the server never uses
   one, but libstdc++ brings it in) */
VR_HIDDEN uint32_t arc4random( void ) {
	uint32_t v = 0;
	unsigned char *p = (unsigned char *)&v;
	size_t got = 0;

	while ( got < sizeof( v ) ) {
		ssize_t n = getrandom( p + got, sizeof( v ) - got, 0 );
		if ( n <= 0 ) {
			break;
		}
		got += (size_t)n;
	}
	if ( got < sizeof( v ) ) {
		int fd = open( "/dev/urandom", O_RDONLY | O_CLOEXEC );
		if ( fd >= 0 ) {
			while ( got < sizeof( v ) ) {
				ssize_t n = read( fd, p + got, sizeof( v ) - got );
				if ( n <= 0 ) {
					break;
				}
				got += (size_t)n;
			}
			close( fd );
		}
	}
	if ( got < sizeof( v ) ) {
		struct timespec ts;
		clock_gettime( CLOCK_MONOTONIC, &ts );
		v ^= (uint32_t)ts.tv_nsec * 2654435761u ^ (uint32_t)ts.tv_sec;
	}
	return v;
}
