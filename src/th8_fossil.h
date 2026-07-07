/*
** th8_fossil.h -- TH8 integration with the Fossil SCM.
**
** This file provides the public API for integrating the TH8 scripting
** language into Fossil alongside the existing TH1 interpreter.
**
** Usage from Fossil's main.c:
**
**     #include "th8_fossil.h"
**
**     // During initialization (e.g., in fossil_main):
**     Th8_InitializeForFossil();
**
**     // During cleanup (e.g., in fossil_atexit handler):
**     Th8_FinalizeForFossil();
**
** The integration provides:
**
**   1. A TH8 interpreter instance managed alongside TH1's g.interp.
**
**   2. Cross-language evaluation commands:
**        [th1Eval script]    -- evaluate a TH1 script from TH8
**        [th1Invoke cmd ...] -- invoke a TH1 command from TH8
**        [th8Eval script]    -- evaluate a TH8 script from TH1
**        [th8Invoke cmd ...] -- invoke a TH8 command from TH8
**
**   3. All Fossil-specific TH1 commands are trampolined into TH8
**      so that TH8 scripts can call puts, query, html, etc.
**      directly without going through [th1Invoke].
**
**   4. A "th8-setup" repository setting for post-creation
**      initialization scripts (mirroring "th1-setup").
**
** Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
**
** See the file "license.terms" for information on usage and
** redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.
*/

#ifndef TH8_FOSSIL_H
#define TH8_FOSSIL_H

#include "th8.h"

/*
** Initialization flags for Th8_InitializeForFossil().
*/
#define TH8_FOSSIL_NONE          ((unsigned int)0x00000000)
#define TH8_FOSSIL_NEED_CONFIG   ((unsigned int)0x00000001)
#define TH8_FOSSIL_FORCE_RESET   ((unsigned int)0x00000002)
#define TH8_FOSSIL_FORCE_SETUP   ((unsigned int)0x00000004)
#define TH8_FOSSIL_NO_REPO       ((unsigned int)0x00000008)
#define TH8_FOSSIL_NO_TRAMPOLINE ((unsigned int)0x00000010)

#define TH8_FOSSIL_DEFAULT (TH8_FOSSIL_NEED_CONFIG)

/*
** Th8_InitializeForFossil --
**
**     Create and configure the TH8 interpreter for use within Fossil.
**     This is typically called lazily (on first TH8 use) rather than
**     at startup.
**
**     The interpreter is created with the POSIX or Win32 platform
**     (as appropriate), merged with the libc bridge.  All standard
**     TH8 language commands are registered.  Then:
**
**       1. Fossil-specific TH1 commands are trampolined into TH8
**          (unless TH8_FOSSIL_NO_TRAMPOLINE is set).
**
**       2. Cross-language bridge commands are registered:
**            th1Eval, th1Invoke   (TH8 -> TH1)
**            th8Eval, th8Invoke   (TH1 -> TH8, registered in TH1)
**
**       3. The "th8-setup" repository setting is evaluated if present.
**
**     Safe to call multiple times; subsequent calls are no-ops unless
**     TH8_FOSSIL_FORCE_RESET is set.
*/
void Th8_InitializeForFossil(unsigned int flags);

/*
** Th8_FinalizeForFossil --
**
**     Destroy the TH8 interpreter and release all resources.
**     Called during Fossil process cleanup (e.g., fossil_atexit).
**     Safe to call even if Th8_InitializeForFossil was never called.
*/
void Th8_FinalizeForFossil(void);

/*
** Th8_GetFossilInterp --
**
**     Return the Fossil TH8 interpreter, or NULL if not initialized.
**     For use by Fossil code that needs direct TH8 API access.
*/
Th8_Interp *Th8_GetFossilInterp(void);

/*
** Th8_FossilEval --
**
**     Convenience: initialize (if needed) and evaluate a TH8 script.
**     Returns the TH8 return code.  The result is available via
**     Th8_GetResult on the returned interpreter.
*/
int Th8_FossilEval(const char *zScript, size_t nScript);

/*
** Th8_FossilReady --
**
**     Returns non-zero if the TH8 interpreter has been initialized
**     and is ready for use.
*/
int Th8_FossilReady(void);

#endif /* TH8_FOSSIL_H */
