/*
 * th8_testlib.h --
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef TH8_TESTLIB_H
#define TH8_TESTLIB_H

#if !defined(TESTLIB_EXPORT)
#  if defined(_WIN32) || defined(__CYGWIN__)
#    if defined(TH8_BUILD_STATIC)
#      define TESTLIB_EXPORT
#    elif defined(TH8_BUILD_DLL)
#      define TESTLIB_EXPORT __declspec(dllexport)
#    else
#      define TESTLIB_EXPORT __declspec(dllimport)
#    endif
#  elif defined(__GNUC__) && __GNUC__ >= 4
#    define TESTLIB_EXPORT __attribute__((visibility("default")))
#  else
#    define TESTLIB_EXPORT
#  endif
#endif

#ifdef TH8_TESTLIB_TH8
TESTLIB_EXPORT int Th8test_Init(Th8_Interp *interp);
TESTLIB_EXPORT int Th8test_Unload(Th8_Interp *interp, int flags);
/* Independent throwaway entry point for [load]/[unload] tests. */
TESTLIB_EXPORT int Th8loadtest_Init(Th8_Interp *interp);
TESTLIB_EXPORT int Th8loadtest_Unload(Th8_Interp *interp, int flags);
#endif

#ifdef TH8_TESTLIB_TCL
TESTLIB_EXPORT int Tclth8test_Init(Tcl_Interp *interp);
TESTLIB_EXPORT int Tclth8test_Unload(Tcl_Interp *interp, int flags);
#endif

#endif /* TH8_TESTLIB_H */
