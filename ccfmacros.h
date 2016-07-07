
#ifndef __CC_FMACRO_H__
#define __CC_FMACRO_H__

#define _BSD_SOURCE

#if defined(LINUX)
#define _GNU_SOURCE
#define _DEFAULT_SOURCE
#endif

#if defined(AIX)
#define _ALL_SOURCE
#endif

#if defined(LINUX) || defined(__OpenBSD__)
#define _XOPEN_SOURCE 700
/*
 * On NetBSD, _XOPEN_SOURCE undefines _NETBSD_SOURCE and
 * thus hides inet_aton etc.
 */
#elif !defined(__NetBSD__)
#define _XOPEN_SOURCE
#endif

#if defined(SUNOS)
#define _POSIX_C_SOURCE 199506L
#endif

#define _LARGEFILE_SOURCE
#define _FILE_OFFSET_BITS 64

#endif
