/*
 * th8_meta_win32.h -- Windows / Win32 meta-header for TH8.
 *
 * This header is internal to the TH8 build.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef TH8_META_WIN32_H
#define TH8_META_WIN32_H

/*
 * Source files that need Win32 headers must include th8_meta_defs.h,
 * th8_meta_libc.h, th8_meta_msvc.h, and th8_meta_win32.h explicitly,
 * in that order.
 */

#if defined(_WIN32) || defined(WIN32)

/*
 * Reduce the Win32 API surface to speed up compilation.
 */

#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif

/*
 * Winsock2 must be included BEFORE windows.h to avoid
 * redefinition conflicts with winsock.h.
 */

#  include <winsock2.h>  /* WSAStartup, socket, sendto, recvfrom */
#  include <ws2tcpip.h>  /* getaddrinfo, freeaddrinfo, sockaddr_in6 */
#  include <windows.h>  /* Win32 API: CreateFile, ReadFile, etc. */

/*
 * Authenticode signature verification (release builds only).
 * Used by th8_win32.c xLoad callback to verify DLL signatures.
 */

#  if defined(NDEBUG)
#    include <wintrust.h> /* WinVerifyTrust */
#    include <softpub.h>  /* WINTRUST_ACTION_GENERIC_VERIFY_V2 */
#    include <mscat.h>  /* Catalog support */
#  endif

/*
 * Link the Winsock library automatically on MSVC.
 */

#  if defined(_MSC_VER)
#    pragma comment(lib, "ws2_32.lib")
#  endif

#endif /* _WIN32 || WIN32 */

#endif /* TH8_META_WIN32_H */
