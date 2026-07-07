/*
 * th8_plat.h -- Platform type definitions for TH8.
 *
 * Provides the Th8_PlatformMutex typedef.  This header is
 * optionally included by implementation files (th8_plat.c,
 * th8_core.c, etc.) BEFORE th8.h to make Th8_Mutex resolve
 * to the concrete platform mutex type.
 *
 * When th8_plat.h is NOT included (amalgamation or embedder
 * context), th8.h defines Th8_Mutex as void, making the mutex
 * callbacks take void* pointers.  This avoids pulling
 * <pthread.h> or <windows.h> into the public header.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef TH8_PLAT_H
#define TH8_PLAT_H

/*
 *----------------------------------------------------------------------
 *
 * Th8_Mutex --
 *
 *	Platform-specific mutex type.  On Win32 this is a
 *	CRITICAL_SECTION; on POSIX it is a pthread_mutex_t.
 *	The xMutexInit callback initializes a Th8_Mutex in-place
 *	(like InitializeCriticalSection / pthread_mutex_init).
 *
 *----------------------------------------------------------------------
 */

#if defined(__COSMOPOLITAN__)
   /*
    * Cosmopolitan Libc provides pthreads on all platforms (including
    * Windows), so always use pthread_mutex_t.
    */
#  include <pthread.h>
typedef pthread_mutex_t Th8_PlatformMutex;
#elif defined(_WIN32) || defined(WIN32)
   /*
    * Inline CRITICAL_SECTION layout to avoid pulling in <windows.h>
    * from the public header.  Matches the Win32 RTL_CRITICAL_SECTION
    * struct on all supported Windows versions, i.e. NT 3.5 and later.
    */
typedef struct {
    void *DebugInfo;
    long LockCount;
    long RecursionCount;
    void *OwningThread;
    void *LockSemaphore;
    void *SpinCount;
} Th8_PlatformMutex;
#elif defined(__APPLE__) || defined(__linux__) || defined(__unix__)
#  include <pthread.h>
typedef pthread_mutex_t Th8_PlatformMutex;
#else
   /* Fallback: opaque pointer, platform must provide implementation */
typedef struct {
    void *_opaque;
} Th8_PlatformMutex;
#endif

#endif /* TH8_PLAT_H */
