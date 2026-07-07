/*
 * th8_tcl.h --
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef TH8_TCL_H
#define TH8_TCL_H

#if !defined(BRIDGE_EXPORT)
#  if defined(_WIN32) || defined(__CYGWIN__)
#    if defined(TH8_BUILD_STATIC)
#      define BRIDGE_EXPORT
#    elif defined(TH8_BUILD_DLL)
#      define BRIDGE_EXPORT __declspec(dllexport)
#    else
#      define BRIDGE_EXPORT __declspec(dllimport)
#    endif
#  elif defined(__GNUC__) && __GNUC__ >= 4
#    define BRIDGE_EXPORT __attribute__((visibility("default")))
#  else
#    define BRIDGE_EXPORT
#  endif
#endif

#ifdef TH8_TCL_BRIDGE_TCL
BRIDGE_EXPORT int Tclth8bridge_Init(Tcl_Interp *tclInterp);
BRIDGE_EXPORT int Tclth8bridge_Unload(Tcl_Interp *tclInterp, int flags);
#endif

#ifdef TH8_TCL_BRIDGE_TH8
BRIDGE_EXPORT int Th8bridge_Init(Th8_Interp *interp);
BRIDGE_EXPORT int Th8bridge_Unload(Th8_Interp *interp, int flags);
#endif

#endif /* TH8_TCL_H */
