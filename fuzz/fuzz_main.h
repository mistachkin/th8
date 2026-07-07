/*
 * fuzz_main.h -- Standalone main() for fuzz harnesses.
 *
 * When TH8_FUZZ_STANDALONE is defined (no libFuzzer), provides
 * a main() that reads one input from stdin.  Compatible with
 * AFL++, honggfuzz, or manual testing.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 */

#ifndef FUZZ_MAIN_H
#define FUZZ_MAIN_H

#ifdef TH8_FUZZ_STANDALONE

#include <stdio.h>
#include <stdlib.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
int LLVMFuzzerInitialize(int *argc, char ***argv);

int main(int argc, char **argv)
{
    uint8_t *buf = NULL;
    size_t len = 0;
    size_t cap = 0;
    int ch;

    LLVMFuzzerInitialize(&argc, &argv);

    while ((ch = fgetc(stdin)) != EOF) {
	if (len >= cap) {
	    cap = cap ? cap * 2 : 4096;
	    buf = (uint8_t *)realloc(buf, cap);
	    if (!buf) return 1;
	}
	buf[len++] = (uint8_t)ch;
    }

    if (buf && len > 0) {
	LLVMFuzzerTestOneInput(buf, len);
    }

    free(buf);
    return 0;
}

#endif /* TH8_FUZZ_STANDALONE */
#endif /* FUZZ_MAIN_H */
