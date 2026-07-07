/*
 * th8_mem.h -- Private memory allocation wrappers for TH8.
 *
 * All TH8 code (except the platform CRT callback implementations
 * in th8_libc.c and th8_macos.c, which must call the real CRT
 * functions) should use these wrappers instead of calling malloc,
 * calloc, realloc, or free directly.  This provides a single
 * point of control for auditing, debugging, and replacing the
 * allocator.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef TH8_MEM_H
#define TH8_MEM_H

#include <stdlib.h>

/*
 * AUDIT-OK[direct-libc-malloc]: these are the project's bridge
 * AUDIT-OK[direct-libc-calloc]: wrappers used inside platform and
 * AUDIT-OK[direct-libc-realloc]: boundary code only.  The lowercase
 * `th8_*` prefix is the documented "internal-use, libc-direct"
 * namespace -- callers in core code must use TH8_ALLOC / TH8_REALLOC
 * (and friends) instead, which route through the per-interpreter
 * platform allocator and capture __FILE__ / __LINE__ for OOM
 * diagnostics.
 */
#define th8_malloc(n)     malloc((n))
#define th8_calloc(c, n)  calloc((c), (n))
#define th8_realloc(p, n) realloc((p), (n))
#define th8_free(p)       free((p))

#endif /* TH8_MEM_H */
