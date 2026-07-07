/*
 * th8_meta_libc.h -- Standard C Library meta-header for TH8.
 *
 * This header is internal to the TH8 build.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef TH8_META_LIBC_H
#define TH8_META_LIBC_H

/*
 * Feature test macros.  These MUST be defined before any system
 * header is included.  On glibc, _GNU_SOURCE is a superset that
 * enables POSIX, X/Open, and BSD extensions.  On FreeBSD/OpenBSD/
 * Solaris, _POSIX_C_SOURCE enables POSIX.1-2008 features.
 * _DEFAULT_SOURCE is the modern glibc equivalent of _BSD_SOURCE.
 *
 * These are not needed on Windows or macOS (where they can
 * restrict rather than expand the available API surface).
 */

#if !defined(_WIN32) && !defined(WIN32) && !defined(__APPLE__)
#  ifndef _DEFAULT_SOURCE
#    define _DEFAULT_SOURCE
#  endif
#  ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#  endif
#  ifndef _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 200809L
#  endif
#endif

/*
 * Standard C headers used by TH8.
 */

#include <stddef.h> /* size_t, ptrdiff_t, NULL */
#include <stdarg.h> /* va_list, va_start, va_end */
#include <stdint.h> /* uint8_t, int64_t, etc. */
#include <limits.h> /* INT_MAX, UCHAR_MAX, PATH_MAX */
#include <stdlib.h> /* malloc, free, realloc, atoi, qsort, exit */
#include <stdio.h> /* FILE, fprintf, snprintf, vsnprintf */
#include <string.h> /* memcpy, memset, memcmp, strlen, strcmp */
#include <math.h> /* sin, cos, sqrt, pow, fmod, etc. */
#include <errno.h> /* errno, EINTR, ENOMEM */
#include <time.h> /* time_t, struct tm, time(), gmtime() */
#include <assert.h> /* assert() */
#include <float.h> /* DBL_EPSILON, DBL_MAX, DBL_MIN */
#include <signal.h> /* signal, SIGINT, SIGTERM */

/*
 * NOTE: malloc introspection headers (<malloc/malloc.h> on macOS,
 * <malloc.h> on Linux/BSD/Windows) are NOT standard C.  They are
 * included in th8_meta_posix.h and th8_meta_win32.h respectively.
 */

#endif /* TH8_META_LIBC_H */
