/*
 * regcustom_th8.h -- TH8 customization for the Spencer regex engine.
 *
 * This file replaces PostgreSQL's regcustom.h.  It maps the regex
 * engine's memory, character type, and interrupt hooks to TH8's
 * platform abstraction layer.
 *
 * Copyright (c) 1998, 1999 Henry Spencer.  All rights reserved.
 * TH8 adaptation Copyright (c) 2026 by Joe Mistachkin.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * See externals/regex/COPYRIGHT for the full Spencer license.
 */

#ifndef REGCUSTOM_TH8_H
#define REGCUSTOM_TH8_H

/*
 * Minimal standard headers.  All C runtime functions are
 * routed through TH8 bridge functions below -- we do NOT
 * include <string.h>, <stdio.h>, or <stdlib.h>.
 */

#include <stddef.h>   /* size_t, NULL */
#include <limits.h>   /* INT_MAX, UCHAR_MAX */
#include <stdarg.h>   /* va_list (for bridge sprintf) */
#include <assert.h>   /* assert() used throughout the regex engine */

/*
 * FILE type stub.  The Spencer engine references FILE only
 * in debug dump functions (gated by REG_DEBUG, which TH8
 * never defines).  We provide an opaque forward declaration
 * so the function prototypes compile.
 */

/*
 * The Spencer regex engine references FILE* in debug dump functions
 * (behind REG_DEBUG).  Include <stdio.h> unconditionally rather
 * than stubbing FILE, to avoid typedef conflicts in amalgamation
 * builds where <stdio.h> is already included.
 */
#include <stdio.h>

/*
 * TH8 provides its own memory and character classification.
 * We do NOT include postgres.h, pg_wchar.h, or any PostgreSQL
 * headers.
 */

/*
 * Forward-declare the TH8 interp for the interrupt hook.
 * The actual Th8_Interp definition is opaque; we only need
 * a pointer.
 */

struct Th8_Interp;

/*
 * Memory management -- routed through TH8's platform.
 *
 * The regex interpreter pointer and OOM flag are static
 * TH8_THREAD_LOCAL variables inside th8_regex.c.  The
 * Spencer engine accesses them only through these public
 * API functions.  th8_regex_malloc returns NULL when the
 * OOM flag is set (after an interrupt), causing the engine
 * to abort via its REG_ESPACE error path.
 */

void *th8_regex_malloc(size_t n);
void th8_regex_free(void *p);
void *th8_regex_realloc(void *p, size_t n);

#define MALLOC(n)     th8_regex_malloc(n)
#define FREE(p)       th8_regex_free(VS(p))
#define REALLOC(p, n) th8_regex_realloc(VS(p), (n))

/*
 * Interrupt check -- routed through Th8_Ready().
 *
 * The Spencer engine treats INTERRUPT as a void expression
 * (PostgreSQL's CHECK_FOR_INTERRUPTS longjmps on interrupt,
 * which the engine can't handle by return value).
 *
 * TH8 can't longjmp.  Instead, when Th8_Ready() returns
 * TH8_ERROR (step limit, cancel, or stack overflow),
 * th8_regex_interrupted() returns non-zero AND sets the
 * MALLOC function to return NULL for all subsequent calls.
 * This causes the engine to hit an out-of-memory path
 * and bail with REG_ESPACE, which our wrapper maps to the
 * real error message from the interpreter result.
 */

int th8_regex_interrupted(void);

#define INTERRUPT(re) ((void)th8_regex_interrupted())

/*
 * C runtime bridge functions.
 *
 * All standard library calls in the Spencer engine are routed
 * through these TH8 bridge functions, which dispatch via the
 * Th8_Platform abstraction.  This ensures zero direct CRT
 * linkage in the compiled regex code.
 */

void *th8_regex_memcpy(void *dst, const void *src, size_t n);
int th8_regex_memcmp(const void *a, const void *b, size_t n);
void *th8_regex_memset(void *dst, int c, size_t n);
size_t th8_regex_strlen(const char *s);
int th8_regex_strcmp(const char *s1, const char *s2);
char *th8_regex_strcpy(char *dst, const char *src);
char *th8_regex_strchr(const char *s, int c);
int th8_regex_sprintf(char *buf, const char *fmt, ...);
int th8_regex_atoi(const char *s);
void th8_regex_qsort(
    void *base,
    size_t nmemb,
    size_t size,
    int (*cmp)(const void *, const void *));

#undef memcpy
#undef memcmp
#undef memset
#undef strlen
#undef strcmp
#undef strcpy
#undef strchr
#undef sprintf
#undef atoi
#undef qsort

#define memcpy  th8_regex_memcpy
#define memcmp  th8_regex_memcmp
#define memset  th8_regex_memset
#define strlen  th8_regex_strlen
#define strcmp  th8_regex_strcmp
#define strcpy  th8_regex_strcpy
#define strchr  th8_regex_strchr
#define sprintf th8_regex_sprintf
#define atoi    th8_regex_atoi
#define qsort   th8_regex_qsort

/*
 * PostgreSQL compatibility defines.
 */

#define FLEXIBLE_ARRAY_MEMBER /* empty -- C99 flexible array */

#ifndef __cplusplus
#  ifndef bool
typedef int bool;
#    define true  1
#    define false 0
#  endif
#endif

/* FILE is provided by <stdio.h> above. */

/* VS() cast used by FREE macro */
#ifndef VS
#  define VS(x) ((void *)(x))
#endif

/* pg_fallthrough -- compiler hint for switch fall-through */
#if defined(__GNUC__) && __GNUC__ >= 7
#  define pg_fallthrough __attribute__((fallthrough))
#elif defined(__clang__)
#  define pg_fallthrough __attribute__((fallthrough))
#else
#  define pg_fallthrough ((void)0)
#endif

/*
 * stack_is_too_deep -- PostgreSQL's stack depth check.
 * TH8 doesn't have this; we rely on th8CheckStack via
 * the INTERRUPT macro.  Return false (0) = stack is OK.
 */

/*
 * stack_is_too_deep -- wire to TH8's th8CheckStack when
 * an interpreter is available.  Returns 1 if too deep.
 */

int th8_regex_stack_too_deep(void);

static inline int
stack_is_too_deep(void)
{
    return th8_regex_stack_too_deep();
}

/* rstacktoodeep is a real function defined in regcomp.c.
 * It calls stack_is_too_deep() defined above. */

/*
 * Override REG_MAX_COMPILE_SPACE to a smaller value suitable
 * for an embedded interpreter.  The PostgreSQL default is
 * ~192 MB on 64-bit, which is excessive.  50000 units gives
 * approximately 19 MB -- still generous but bounded.
 */

#define REG_MAX_COMPILE_SPACE                                                \
    (50000 * (sizeof(struct state) + 4 * sizeof(struct arc)))

/*
 * Function pointer declaration style.
 */

#define FUNCPTR(name, args) (*name) args

/*
 * Assert -- use C assert in debug builds, nothing in release.
 */

/*
 * assert() is provided by <assert.h> included at the top of this
 * file.  When NDEBUG is defined (release builds), assert() is a
 * no-op.  When NDEBUG is not defined (debug/sanitizer builds),
 * assert() is active.  No custom override is needed.
 */

/*
 * Internal character type.
 *
 * TH8 uses UTF-8 externally, but the Spencer engine works on
 * fixed-width character arrays internally.  We use 32-bit
 * characters (Unicode code points) as the internal type,
 * matching PostgreSQL's pg_wchar approach.
 *
 * The TH8 regex wrapper converts UTF-8 strings to/from
 * chr arrays at the API boundary.
 */

#ifndef _TH8_CHR_DEFINED
#  define _TH8_CHR_DEFINED
typedef unsigned int chr; /* 32-bit Unicode code point */
typedef unsigned int uchr; /* unsigned chr */
#endif

#define CHR(c)      ((unsigned char)(c))
#define DIGITVAL(c) ((c) - '0')
#define CHRBITS     32
#define CHR_MIN     0x00000000
#define CHR_MAX     0x7ffffffe

#define CHR_IS_IN_RANGE(c) ((c) <= CHR_MAX)

/*
 * MAX_SIMPLE_CHR -- cutoff for "simple" vs "complicated"
 * color map processing.  0x7FF covers all of ASCII + Latin-1
 * + common Unicode.
 */

#define MAX_SIMPLE_CHR 0x7FF

/*
 * Character classification -- use TH8's Unicode-aware functions.
 * These operate on chr (32-bit code points).
 */

int th8_regex_isalnum(chr c);
int th8_regex_isalpha(chr c);
int th8_regex_isdigit(chr c);
int th8_regex_isspace(chr c);

#define iscalnum(x) th8_regex_isalnum(x)
#define iscalpha(x) th8_regex_isalpha(x)
#define iscdigit(x) th8_regex_isdigit(x)
#define iscspace(x) th8_regex_isspace(x)

/*
 * Pull in the TH8-adapted regex header (NOT the PostgreSQL one).
 * REG_CANCEL is defined there alongside all other REG_* codes.
 */

#include "regex_th8.h"

/*
 * Map PostgreSQL function names to TH8 names so that the Spencer
 * engine source (which uses pg_regcomp etc.) compiles without
 * modification.
 */

#define pg_regcomp   th8_regcomp
#define pg_regexec   th8_regexec
#define pg_regfree   th8_regfree
#define pg_regerror  th8_regerror
#define pg_regprefix th8_regprefix

/*
 * Map PostgreSQL types used by the engine internals.
 */

#define pg_wchar      chr
#define pg_regoff_t   th8_regoff_t
#define pg_regex_t    th8_regex_t
#define pg_regmatch_t th8_regmatch_t
#define rm_detail_t   th8_rm_detail_t

/*
 * Oid is PostgreSQL's object identifier type.
 * The Spencer engine uses it only for the collation parameter,
 * which TH8 ignores.
 */

typedef unsigned int Oid;

#endif /* REGCUSTOM_TH8_H */
