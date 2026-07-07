/*
 * th8_ctime.c -- Compile-time options for TH8.
 *
 * This file provides a list of compile-time options that were
 * active when TH8 was built.  The list is exposed via
 * Th8_GetCompileOptions() and used to set the
 * ::tcl_platform(compileOptions) array element at interpreter
 * creation.
 *
 * Each entry in the array has the "TH8_" prefix stripped.
 * For example, TH8_ENABLE_REGEXP becomes "ENABLE_REGEXP".
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "th8.h"


/*
 * The compile-time options array.  Each #ifdef adds its
 * corresponding string.  The array is terminated by a NULL.
 */

static const char *th8CompileOptions[] = {
#if defined(NDEBUG)
    "NDEBUG",
#endif

#if defined(TH8_AMALGAMATION)
    "AMALGAMATION",
#endif

#if defined(TH8_BENCHMARKING)
    "BENCHMARKING",
#endif

#if defined(TH8_DEBUG)
    "DEBUG",
#endif

#if defined(TH8_ENABLE_BIGINT)
    "ENABLE_BIGINT",
#endif

#if defined(TH8_ENABLE_CRYPTOGRAPHY)
    "ENABLE_CRYPTOGRAPHY",
#endif

#if defined(TH8_ENABLE_EXPRESSIONS)
    "ENABLE_EXPRESSIONS",
#endif

#if defined(TH8_ENABLE_FAULT_INJECTION)
    "ENABLE_FAULT_INJECTION",
#endif

#if defined(TH8_ENABLE_LIBCURL)
    "ENABLE_LIBCURL",
#endif

#if defined(TH8_ENABLE_LOAD)
    "ENABLE_LOAD",
#endif

#if defined(TH8_ENABLE_REGEXP)
    "ENABLE_REGEXP",
#endif

#if defined(TH8_ENABLE_TEST_KEY)
    "ENABLE_TEST_KEY",
#endif

#if defined(TH8_ENABLE_UNBOUND)
    "ENABLE_UNBOUND",
#endif

#if defined(TH8_ENABLE_VARIABLES)
    "ENABLE_VARIABLES",
#endif

#if defined(TH8_PLATFORM_COSMOPOLITAN)
    "PLATFORM_COSMOPOLITAN",
#endif

#if defined(TH8_PLATFORM_CURL)
    "PLATFORM_CURL",
#endif

#if defined(TH8_PLATFORM_LIBC)
    "PLATFORM_LIBC",
#endif

#if defined(TH8_PLATFORM_MACOS)
    "PLATFORM_MACOS",
#endif

#if defined(TH8_PLATFORM_NULLIO)
    "PLATFORM_NULLIO",
#endif

#if defined(TH8_PLATFORM_POSIX)
    "PLATFORM_POSIX",
#endif

#if defined(TH8_PLATFORM_WIN32)
    "PLATFORM_WIN32",
#endif

#if defined(TH8_PLUGIN_CONTROL)
    "PLUGIN_CONTROL",
#endif

#if defined(TH8_PLUGIN_EXPRESSIONS)
    "PLUGIN_EXPRESSIONS",
#endif

#if defined(TH8_PLUGIN_EXTENSIBILITY)
    "PLUGIN_EXTENSIBILITY",
#endif

#if defined(TH8_PLUGIN_FILE_SYSTEMS)
    "PLUGIN_FILE_SYSTEMS",
#endif

#if defined(TH8_PLUGIN_FORMATTING)
    "PLUGIN_FORMATTING",
#endif

#if defined(TH8_PLUGIN_INTROSPECTION)
    "PLUGIN_INTROSPECTION",
#endif

#if defined(TH8_PLUGIN_IO)
    "PLUGIN_IO",
#endif

#if defined(TH8_PLUGIN_LISTS)
    "PLUGIN_LISTS",
#endif

#if defined(TH8_PLUGIN_LOOPING)
    "PLUGIN_LOOPING",
#endif

#if defined(TH8_PLUGIN_MANAGEMENT)
    "PLUGIN_MANAGEMENT",
#endif

#if defined(TH8_PLUGIN_PROCEDURES)
    "PLUGIN_PROCEDURES",
#endif

#if defined(TH8_PLUGIN_STRINGS)
    "PLUGIN_STRINGS",
#endif

#if defined(TH8_PLUGIN_TIMEKEEPING)
    "PLUGIN_TIMEKEEPING",
#endif

#if defined(TH8_PLUGIN_VARIABLES)
    "PLUGIN_VARIABLES",
#endif

#if defined(TH8_TRANSLATE_EOL)
    "TRANSLATE_EOL",
#endif

#if defined(TH8_USE_MIMALLOC)
    "USE_MIMALLOC",
#endif

#if defined(TH8_USE_BESTLINE)
    "USE_BESTLINE",
#endif

    0  /* Sentinel */
};


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetCompileOptions --
 *
 *	Return a pointer to a NULL-terminated array of strings
 *	listing the compile-time options that were active when
 *	TH8 was built.  Each string has the "TH8_" prefix
 *	removed (e.g., "ENABLE_REGEXP" not "TH8_ENABLE_REGEXP").
 *
 * Why / How:
 *	Returns the address of a module-level static array that is
 *	populated at compile time by #ifdef gates.  Each active
 *	TH8_* define adds its short name to the array.  The array
 *	is used at interpreter creation to set
 *	::tcl_platform(compileOptions) and by [info compileoptions].
 *
 * Results:
 *	Pointer to a NULL-terminated array of C strings.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

const char **
Th8_GetCompileOptions(void)
{
    return th8CompileOptions;
}
