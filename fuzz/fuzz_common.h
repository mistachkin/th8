/*
 * fuzz_common.h -- Shared initialization for fuzz harnesses.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 */

#ifndef FUZZ_COMMON_H
#define FUZZ_COMMON_H

#include "th8.h"
#include <stdint.h>
#include <stddef.h>

/* libFuzzer entry points (prototypes suppress -Wmissing-prototypes). */
int LLVMFuzzerInitialize(int *argc, char ***argv);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

#if defined(__GNUC__) || defined(__clang__)
__attribute__((unused))
#endif
static Th8_Interp *
th8FuzzCreateInterp(int bRegisterLang)
{
    static Th8_Platform plat;
    Th8_Interp *interp;

    if (Th8_UseDefaultPlatform(&plat) != TH8_OK) return NULL;
    if (Th8_Initialize(&plat) != TH8_OK) return NULL;

    interp = Th8_CreateInterp(&plat);
    if (interp && bRegisterLang) {
	/* A partially-registered interpreter must not be handed to a fuzz
	 * target: Th8_RegisterLanguage returns TH8_ERROR (leaving a partial,
	 * inconsistent language) on any registration failure, so discard the
	 * interpreter and return NULL rather than fuzzing partial state
	 * (TH8K-006). */
	if (Th8_RegisterLanguage(interp) != TH8_OK) {
	    Th8_DeleteInterp(interp);
	    return NULL;
	}
    }

    return interp;
}

#endif /* FUZZ_COMMON_H */
