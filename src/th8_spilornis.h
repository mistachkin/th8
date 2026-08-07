/*
 * th8_spilornis.h -- TH8 override header for Spilornis.
 *
 * This header is force-included (-include on GCC/Clang, /FI on
 * MSVC) before Spilornis.c to compile Eagle's Tcl list parser
 * in "narrow/UTF-8 mode."
 *
 * WHAT THIS FILE DOES:
 *
 *   1. Blocks Spilornis' own headers (_SPILORNIS_H_, etc.)
 *      and provides all needed types and constants inline.
 *
 *   2. Redefines se_WCHAR to unsigned char (from wchar_t) so the
 *      parser operates on UTF-8 byte strings.
 *
 *   3. Routes ALL C runtime function calls through bridge
 *      functions (th8_spilornis_*) that dispatch via the
 *      TH8 platform abstraction.  This ensures zero direct
 *      CRT linkage in the compiled Spilornis object.
 *
 *   4. Maps wide-char functions (se_wcslen, se_wmemcpy, se_swprintf,
 *      etc.) to the narrow bridge equivalents.
 *
 * WHY THIS IS SAFE:
 *
 *   Tcl list syntax uses only ASCII delimiters ({, }, [, ], \,
 *   ;, $, ", space, tab, newline) and UTF-8 guarantees that
 *   multi-byte sequences never contain bytes in the ASCII range
 *   (0x00-0x7F).  Therefore, the Spilornis list parser operates
 *   identically on UTF-8 and ASCII.
 *
 * BRIDGE FUNCTION LIFECYCLE:
 *
 *   A global interpreter pointer (th8_spilornis_interp) is set
 *   in th8_core.c before each call to Eagle_SplitList or Eagle_JoinList
 *   and cleared afterward.  The bridge functions use this pointer
 *   to access the platform callbacks.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef TH8_SPILORNIS_H
#define TH8_SPILORNIS_H

/*
 * Prevent Spilornis from including its own INTERNAL headers that
 * would conflict with our type overrides.  The PUBLIC header
 * (Spilornis.h) is NOT blocked -- it provides the function
 * prototypes that th8_core.c and the amalgamation need.
 */

#define _SPILORNIS_DEF_H_
#define _SPILORNIS_INT_H_
#define _PKG_VERSION_H_
#define _RC_VERSION_H_

/* Stub version strings that pkgVersion.h / rcVersion.h would provide */
#define PACKAGE_VERSION    "1.0"
#define PACKAGE_PATCHLEVEL "1.0.0.0"

/*
 * Standard headers needed by Spilornis.
 * We include them BEFORE defining our macros so that system
 * headers see the real types.
 */

#if !defined(TH8_SPILORNIS_NO_CRT_HEADERS)
#  include <stddef.h>
#  include <stdlib.h>
#  include <string.h>
#  include <limits.h>
#  include <assert.h>
#  include <stdarg.h>
#  include <stdio.h>
#endif

/* Type guard defines to prevent Spilornis headers from redefining.
 * These must match _exactly_ what SpilornisInt.h and Spilornis.h
 * check with #ifndef before each typedef. */

#define se__CONST_DEFINED
#define se__VOID_DEFINED
#define se__LPVOID_DEFINED
#define se__LPCVOID_DEFINED
#define se___SIZE_T_DEFINED
#define se__LPSIZE_T_DEFINED
#define se__LPCSIZE_T_DEFINED
#define se__WCHAR_DEFINED
#define se__LPWSTR_DEFINED
#define se__LPCWSTR_DEFINED
#define se__CHAR_DEFINED
#define se__LPSTR_DEFINED
#define se__LPCSTR_DEFINED
#define se__BYTE_DEFINED
#define se__LPBYTE_DEFINED
#define se__USHORT_DEFINED
#define se__LPUSHORT_DEFINED
#define se__UCSCHAR_DEFINED
#define se__LPUCSCHAR_DEFINED
#define se__DWORD_DEFINED
#define se__BOOL_DEFINED
#define se__LPBOOL_DEFINED
#define se__INT_DEFINED
#define se__UINT_DEFINED
#define se__BSTR_DEFINED
#define se__FLAGS_DEFINED
#define se__RETURNCODE_DEFINED

typedef unsigned char se_WCHAR;
typedef char *se_LPWSTR;
typedef const char *se_LPCWSTR;
typedef char se_CHAR;
typedef char *se_LPSTR;
typedef const char *se_LPCSTR;
typedef unsigned char se_BYTE;
typedef unsigned char *se_LPBYTE;
typedef void *se_LPVOID;
typedef const void *se_LPCVOID;
typedef unsigned short se_USHORT;
typedef unsigned short *se_LPUSHORT;
typedef unsigned long se_UCSCHAR;
typedef unsigned long *se_LPUCSCHAR;
typedef unsigned long se_DWORD;
typedef int se_BOOL;
typedef int se_FLAGS;
typedef int se_RETURNCODE;
typedef size_t se_SIZE_T;
typedef size_t *se_LPSIZE_T;
typedef const size_t *se_LPCSIZE_T;
typedef int se_INT;
typedef unsigned int se_UINT;
typedef int *se_LPBOOL;

#define CONST const

/*
 * Constants from SpilornisInt.h.
 */

#define LIBRARY_NAME                     "TH8"
#define LIBRARY_PATCH_LEVEL              "1.0.0.0"
#define STRINGIFY1(x)                    #x
#define STRINGIFY(x)                     STRINGIFY1(x)
#define SOURCE_ID                        "th8"
#define SOURCE_TIMESTAMP                 "2026"
#define LIBRARY_UNICODE_NAME             UNICODIFY(LIBRARY_NAME)
#define LIBRARY_UNICODE_PATCH_LEVEL      UNICODIFY(STRINGIFY(LIBRARY_PATCH_LEVEL))
#define LIBRARY_UNICODE_SOURCE_ID        UNICODIFY(SOURCE_ID)
#define LIBRARY_UNICODE_SOURCE_TIMESTAMP UNICODIFY(SOURCE_TIMESTAMP)
#define LIBRARY_MAXIMUM_SIZE_T           ((se_SIZE_T)0x7FFFFFFF)
#define LIBRARY_RESULT_LENGTH            192
#define LIBRARY_LOCAL_FLAGS              20
#define LIBRARY_VAR_BUFFER_LENGTH        20
#define LIBRARY_TRACE_BUFFER_LENGTH      ((se_SIZE_T)(4096 - sizeof(se_DWORD)))
#define LIBRARY_VERSION_LENGTH           256
#define LIBRARY_VERSION_FORMAT           "%s v%s [%s %s]"
#define NO_TRACE_VAR_NAME                "NoTraceSpilornis"
#define NO_TRACE_UNICODE_VAR_NAME        UNICODIFY(NO_TRACE_VAR_NAME)
#define AllocateMemoryWrapper(size)                                          \
    th8_spilornis_calloc((size), sizeof(se_BYTE))
#define FreeMemoryWrapper(p) th8_spilornis_free(p)
#define LIBRARY_DEBUG(x)     ((void)0)
#define LIBRARY_TRACE(x)     ((void)0)
#define LIBRARY_FREED_MEMORY 0xFE

#define EAGLE_DONT_USE_BRACES  (1)
#define EAGLE_USE_BRACES       (2)
#define EAGLE_BRACES_UNMATCHED (4)
#define EAGLE_DONT_QUOTE_HASH  (8)

#ifndef _WIN32
typedef void *se_HANDLE;
#endif

#define TRUE         1
#define FALSE        0
#define VOID         void
#define EXTERN       extern
#define EAGLE_EXTERN extern

/*
 * UNICODIFY: in UTF-8 mode, string literals are narrow (no L prefix).
 */

#define UNICODIFY(x)  x
#define UNICODIFY1(x) x

/*
 * Return codes.
 */

#define EAGLE_OK    0
#define EAGLE_ERROR 1

/*
 * Disable features not needed for TH8.
 */

#define NO_INLINE_WIN32_PROTOTYPES 1
#define NO_SIZEOF_ASSERTS          1
#define NO_ISW_MACROS              1
#define USE_NARROW_CHAR_T          1
#define USE_TRACE                  0
#define USE_SYSSTRINGLEN           0
#define USE_HEAPAPI                0

/*
 * Error message format for EagleFindElement.  This MUST be selected
 * AFTER USE_NARROW_CHAR_T is defined (above): TH8's se_WCHAR is a
 * narrow, single-byte char, so the narrow %s/%.*s conversions are
 * required.  If this #if is evaluated while USE_NARROW_CHAR_T is not
 * yet defined, the wide %ls/%.*ls variant is wrongly chosen; passing a
 * single-byte string to %ls then makes vsnprintf fail (returns -1) and
 * the list-parse error message comes out EMPTY -- a defect that
 * surfaces only in builds where the mis-ordered definition wins (it
 * bit the debug build; the release build happened to bind narrow).
 */

#if defined(USE_NARROW_CHAR_T)
#  define ERRONEOUS_STRING_FORMAT "\"%.*s\" %s"
#else
#  define ERRONEOUS_STRING_FORMAT "\"%.*ls\" %ls"
#endif

/*
 * UTFXBOOL -- used for Boolean return values in ConvertUTF
 * integration (not used here but defined to satisfy Spilornis).
 * Guarded to avoid conflict with ConvertUTF_v2.h's own typedef
 * in the amalgamation build.
 */

#ifndef CONVERT_TYPEDEF_UTFXBOOL
#  define CONVERT_TYPEDEF_UTFXBOOL
typedef int UTFXBOOL;
#endif

/*
 * C runtime bridge functions for Spilornis.
 *
 * All CRT calls are routed through these TH8 bridges, which
 * dispatch via the Th8_Platform abstraction when an interpreter
 * is active.  This ensures Spilornis has zero direct CRT
 * linkage at the source level.
 */

extern void *th8_spilornis_calloc(size_t count, size_t size);
extern void th8_spilornis_free(void *p);
extern void *th8_spilornis_memcpy(void *d, const void *s, size_t n);
extern void *th8_spilornis_memset(void *d, int c, size_t n);
extern int th8_spilornis_memcmp(const void *a, const void *b, size_t n);
extern size_t th8_spilornis_strlen(const char *s);
extern int th8_spilornis_strncmp(const char *a, const char *b, size_t n);
extern char *th8_spilornis_strncpy(char *d, const char *s, size_t n);
extern int
th8_spilornis_snprintf(char *buf, size_t size, const char *fmt, ...);
extern int
th8_spilornis_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
extern size_t th8_spilornis_memsize(void *p);
extern int th8_spilornis_isspace(int c);

/*
 * Undefine all of our CRT (wrapper?) macro names.
 */

#undef se_calloc
#undef se_free
#undef se_memcpy
#undef se_memset
#undef se_memcmp
#undef se_strlen
#undef se_strncmp
#undef se_strncpy
#undef se_snprintf
#undef se_vsnprintf
#undef se_wcslen
#undef se_wcsncmp
#undef se_wmemcpy
#undef se_wmemset
#undef se_swprintf
#undef se_iswspace

/*
 * Remap CRT function names to bridges.
 * These #defines come AFTER the system headers above,
 * so system header declarations are already processed.
 * Subsequent uses in Spilornis.c are redirected.
 *
 * In the amalgamation, these redirections are suppressed here
 * (gated out by TH8_AMALGAMATION) and are instead emitted by
 * mkamal.tcl's spilornisBegin block, which wraps only Spilornis.c.
 * This prevents CRT macro pollution across the translation unit.
 */

#define se_calloc(n, sz)    th8_spilornis_calloc((n), (sz))
#define se_free(p)          th8_spilornis_free(p)
#define se_memcpy(d, s, n)  th8_spilornis_memcpy((d), (s), (n))
#define se_memset(d, c, n)  th8_spilornis_memset((d), (c), (n))
#define se_memcmp(a, b, n)  th8_spilornis_memcmp((a), (b), (n))
#define se_strlen(s)        th8_spilornis_strlen(s)
#define se_strncmp(a, b, n) th8_spilornis_strncmp((a), (b), (n))
#define se_strncpy(d, s, n) th8_spilornis_strncpy((d), (s), (n))
#define se_snprintf         th8_spilornis_snprintf
#define se_vsnprintf        th8_spilornis_vsnprintf

/*
 * Map wide-char functions to (already-remapped) narrow equivalents.
 * Spilornis uses these throughout for string operations.
 */

#define se_wcslen(s)        th8_spilornis_strlen(s)
#define se_wcsncmp(a, b, n) th8_spilornis_strncmp((a), (b), (n))
#define se_wmemcpy(d, s, n) th8_spilornis_memcpy((d), (s), (n))
#define se_wmemset(d, c, n) th8_spilornis_memset((d), (c), (n))
#define se_swprintf         th8_spilornis_snprintf

/*
 * Character classification -- map to narrow equivalents.
 * se_iswspace is the only one Spilornis uses extensively.
 */

#define se_iswspace(c) th8_spilornis_isspace(c)

/*
 * Memory allocation.
 * Use standard se_calloc/se_free.  TH8's platform layer is not
 * available inside Spilornis, but the allocations are short-
 * lived (freed by Eagle_FreeElements / caller).
 */

#define EagleAllocateMemory(size)                                            \
    th8_spilornis_calloc((size), sizeof(se_BYTE))
#define EagleFreeMemory(pMemory) th8_spilornis_free((pMemory))

/*
 * Memory size tracking.  Routed through bridge function.
 */

#define EagleMemorySize(p)   th8_spilornis_memsize(p)
#define MemorySizeWrapper(p) th8_spilornis_memsize(p)

/*
 * se_vswprintf / se_wcsncpy -- narrow replacements.
 * se_vswprintf is used by EaglePrintf for error messages.
 * se_wcsncpy is used in list joining.
 */

#define se_vswprintf        th8_spilornis_vsnprintf
#define se_wcsncpy(d, s, n) th8_spilornis_strncpy((d), (s), (n))

/*
 * Fix format specifiers: in narrow mode, %ls (wide string)
 * must become %s (narrow string).  Spilornis uses %ls and
 * %.*ls in error messages.  We define a macro to fix this
 * at the preprocessor level.
 *
 * This is done by overriding the UNICODIFY macro for format
 * strings -- since all format strings go through UNICODIFY,
 * and we've already mapped UNICODIFY to passthrough, the
 * %ls in the source is preserved.  We need a different approach:
 * patch the format strings in EaglePrintf and error messages.
 *
 * The simplest fix: since se_swprintf is now se_snprintf, and the
 * arguments are char*, change %ls to %s at the point of use.
 * We can't easily do this with macros, so we override the
 * specific error format strings.
 */

/*
 * Disable tracing.
 */

#define EAGLE_TRACE_ENTRY() ((void)0)
#define EAGLE_TRACE_EXIT()  ((void)0)
#define EAGLE_TRACE(x)      ((void)0)

/*
 * Avoid including <windows.h> here and just define this API directly.
 */

#if defined(_WIN32)
#  if defined(_MSC_VER)
#    pragma warning(disable : 4131)
#  endif
extern __declspec(dllimport) VOID __stdcall OutputDebugStringA(LPCSTR);
#  if defined(_MSC_VER)
#    pragma warning(default : 4131)
#  endif
#endif

#endif /* TH8_SPILORNIS_H */
