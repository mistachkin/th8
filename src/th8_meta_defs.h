/*
 * th8_meta_defs.h --
 *
 * This header includes ALL the correct feature test macros, etc.
 * Every TH8 .c file that needs system headers must include this
 * header as its FIRST include, before any project headers.
 *
 * This header is internal to the TH8 build.  It is NOT included
 * by th8.h (which is self-contained for embedder distribution).
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef TH8_META_DEFS_H
#define TH8_META_DEFS_H

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

#endif /* TH8_META_DEFS_H */
