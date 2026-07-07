#!/usr/bin/env tclsh
###############################################################################
#
# audit_patterns.tcl --
#
#     Static-analysis scanner for TH8 C source code.  Walks .c/.h
#     files, applies a registry of pattern-matching rules, and
#     reports any violations with file:line context.  Exits non-zero
#     when violations are found, suitable for CI gating.
#
# Usage:
#     tclsh tools/audit_patterns.tcl [FILE ...]
#         If FILE arguments are supplied, scan exactly those files.
#         Otherwise scan all .c and .h sources under src/.
#
# Suppression:
#     A genuine false-positive can be marked safe by appending a
#     trailing comment to the offending line:
#
#         /* AUDIT-OK[<rule-name>]: <reason> */
#
#     The rule-name in brackets is REQUIRED so that a later-added
#     rule cannot be silently suppressed by an old marker.  The
#     reason should explain WHY the pattern is safe in this
#     specific context (an upstream invariant, an unreachable
#     branch, a constant operand, etc.).
#
# Exit codes:
#     0   No violations.
#     1   One or more violations found.
#     2   Tool itself errored (file unreadable, bad rule, etc.).
#
# Adding rules:
#     Call `audit_rule` once per rule near the top of the file.
#     Each rule needs a short name (used in suppression markers),
#     a Tcl regex matching the offending pattern on a single
#     comment-stripped line, a one-line description, and a
#     suggestion for how to fix it.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

set ::audit_rules [list]

#
# audit_rule --
#
#     Register a single rule.  Arguments:
#       name           -- short identifier for suppression markers and
#                         error messages.
#       regex          -- Tcl regex applied per line, post-strip.
#       description    -- human-readable summary of what's wrong.
#       suggestion     -- short hint about the canonical fix.
#       exclude_globs  -- (optional) list of glob patterns; files
#                         matching any pattern are exempt from this
#                         rule.  Used for legitimate boundary code
#                         (the libc bridge IS allowed to call libc
#                         directly; the platform layers ARE allowed
#                         to use raw POSIX/Win32 API).
#

proc audit_rule {name regex description suggestion {exclude_globs {}} {include_globs {}}} {
  lappend ::audit_rules \
      [list $name $regex $description $suggestion \
      $exclude_globs $include_globs]
}

#
# Files where boundary calls to libc / platform APIs are allowed.
# The TH8 architecture funnels low-level capability through these
# files; banning libc inside them would be circular.  Platform
# layers (th8_posix.c, th8_win32.c, etc.) are similarly allowed
# to call their native APIs directly.
#

# Files where boundary calls to libc / platform APIs are allowed.
#   Platform abstraction layers (define the bridge):
#     th8_libc.c, th8_posix.c, th8_win32.c, th8_macos.c,
#     th8_ios.c, th8_android.c, th8_cosmopolitan.c,
#     th8_mimalloc.c, th8_spilornis.c, th8_env.c, th8_nullio.c
#   External-API glue (foreign APIs use libc shapes):
#     th8_sqlite3.c, th8_curl.c, th8_protect.c
#   Shell entry point and reusable shell helpers (both talk to
#   libc-shaped argv/argc/getenv, install signal handlers, write
#   to stdout/stderr, etc., as the only legitimate consumers of
#   the platform's libc-shaped interface at the embedder boundary):
#     th8sh.c, th8_shell.c
#   Test infrastructure (must bypass to verify behaviour):
#     th8_testlib.c, th8_tcl.c (test/)
#
# Note: Tcl list-literal {...} syntax does NOT honour '#' as a
# comment marker inside the braces; each whitespace-separated
# token is a list element.  Comments stay outside the literal.

set ::PLATFORM_BOUNDARY_FILES [list \
    src/th8_libc.c \
    src/th8_posix.c \
    src/th8_win32.c \
    src/th8_macos.c \
    src/th8_ios.c \
    src/th8_android.c \
    src/th8_cosmopolitan.c \
    src/th8_mimalloc.c \
    src/th8_spilornis.c \
    src/th8_env.c \
    src/th8_nullio.c \
    src/sqlite3/th8_sqlite3.c \
    src/th8_curl.c \
    src/th8_protect.c \
    src/th8sh.c \
    src/th8_shell.c \
    src/test/th8_testlib.c \
    src/test/th8_tcl.c \
    src/th8_hash.c]


###############################################################################
#
# Rule: bare-size-multiply
#
#     Detect `a * b` arithmetic on size-typed values without a
#     companion overflow check.  Even when an upstream limit check
#     proves the multiply mathematically safe, the bare pattern is
#     a banned audit anti-signal: future refactors can break the
#     invariant silently.  Use TH8_SAFE_MUL_SIZE (for size-only
#     computation) or TH8_ALLOC_MUL / TH8_ALLOC_MUL_ADD (for
#     allocation in one shot) instead.
#
#     The rule fires on three sub-patterns:
#       1. An explicit (size_t) cast on either operand.
#       2. Either operand is an identifier whose name starts with
#          'n' followed by an uppercase letter (TH8 size convention:
#          nLen, nByte, nElem, nKey, ...).
#       3. The multiply is inside a malloc()/calloc()/realloc()
#          argument list (libc allocs are dangerous; the safe
#          wrappers are TH8_ALLOC_*).
#
#     Combined into a single regex with three alternatives.  The
#     leading `[^*]` guards prevent matching `**` (pointer-to-
#     pointer), and `(?!\*)` after the multiply rejects `* *`
#     dereference patterns that would still produce a `*` token.
#
###############################################################################

# 1. Explicit (size_t) cast adjacent to a multiply on either side.
#    The cast is the strongest single signal that an attacker-
#    influenced size is involved.
#
#    The character class between the cast and the `*` deliberately
#    EXCLUDES `(` and `)` so that pointer-difference casts like
#    `(size_t)((char *)pDst - zOut)` do not match: those have a
#    `(` immediately after the cast, and the `*` they contain is
#    the pointer-type's `char *`, not multiplication.  The class
#    permits whitespace, identifiers (\w), member access (`.`),
#    and arrow (`->`) so the rule still fires on the legitimate
#    target `(size_t)foo->bar * baz`.
audit_rule "size-cast-multiply-after" \
    {\(\s*size_t\s*\)[\s\w.>-]*\*\s*[a-zA-Z_(]} \
    "(size_t) cast followed by bare multiplication" \
    "use TH8_SAFE_MUL_SIZE for size-only computation, or TH8_ALLOC_MUL / TH8_ALLOC_MUL_ADD for one-shot allocation"

# 1b. Symmetric: multiply on the LEFT, (size_t) cast on the right.
#     The leading [a-zA-Z_] requirement on the left operand keeps
#     this off purely-numeric literal expressions like `4 * 1024 *
#     (size_t)x` (which still flags via the right-side cast, but
#     only when the left operand is variable, not constant).
audit_rule "size-cast-multiply-before" \
    {[a-zA-Z_)\]]\s*\*\s*\(\s*size_t\s*\)} \
    "(size_t) cast preceded by bare multiplication" \
    "use TH8_SAFE_MUL_SIZE for size-only computation, or TH8_ALLOC_MUL / TH8_ALLOC_MUL_ADD for one-shot allocation"

# 2. TH8 size-name convention: identifiers prefixed n<UpperCase>...
#    (nLen, nByte, nElem, nKey, etc.) are the project's documented
#    naming for size_t-typed values.  A multiply with such an
#    identifier on either side is an audit signal.  The closing
#    \y\w prevents the identifier-side match from also matching
#    `*foo` pointer dereference.
audit_rule "size-name-multiply" \
    {(\yn[A-Z]\w*\s*\*\s*[a-zA-Z_]|[a-zA-Z_)\]]\s*\*\s*n[A-Z]\w*\y)} \
    "identifier with size-naming convention adjacent to bare multiplication" \
    "use TH8_SAFE_MUL_SIZE / TH8_ALLOC_MUL / TH8_ALLOC_MUL_ADD"

# 3. Direct call to libc allocators with a multiplied size.
#    The TH8 platform contract forbids calling malloc/calloc/
#    realloc directly (allocations must go through the platform's
#    xMalloc); a multiplied size argument is doubly suspicious.
audit_rule "libc-alloc-with-multiply" \
    {\y(?:m|c|re)alloc\s*\([^)]*\*[^)]*\)} \
    "direct libc alloc (m/c/re)alloc with a multiplied argument" \
    "use TH8_ALLOC_MUL / TH8_ALLOC_MUL_ADD via the TH8 platform allocator" \
    $::PLATFORM_BOUNDARY_FILES


###############################################################################
#
# Rule: direct-libc-mem-alloc
#
#     Direct calls to libc malloc / calloc / realloc / free at all,
#     even with non-multiplied arguments.  The TH8 platform contract
#     routes every allocation through the per-interpreter xMalloc /
#     xFree pair; bypassing it loses tracking, the allocation
#     limit, and (on macOS) the private malloc zone.  Boundary
#     files in PLATFORM_BOUNDARY_FILES are the only legitimate
#     callers.
#
#     The "free(" pattern is intentional and matches both the
#     libc free and any Th8_*Free wrapper that ends in "free(";
#     the latter is a false positive accepted in exchange for
#     simpler regex (suppress with AUDIT-OK if needed).
#
###############################################################################

audit_rule "direct-libc-malloc" \
    {\ymalloc\s*\(} \
    "direct libc malloc() bypasses the TH8 platform allocator" \
    "use TH8_ALLOC / TH8_ALLOC_STR / TH8_ALLOC_MUL via Th8_SafeAlloc" \
    $::PLATFORM_BOUNDARY_FILES

audit_rule "direct-libc-calloc" \
    {\ycalloc\s*\(} \
    "direct libc calloc() bypasses the TH8 platform allocator" \
    "use TH8_ALLOC_MUL (zero-init is provided by TH8 alloc)" \
    $::PLATFORM_BOUNDARY_FILES

audit_rule "direct-libc-realloc" \
    {\yrealloc\s*\(} \
    "direct libc realloc() bypasses the TH8 platform allocator" \
    "use Th8_Realloc / Th8_AttemptRealloc" \
    $::PLATFORM_BOUNDARY_FILES


###############################################################################
#
# Rule: direct-libc-mem-ops
#
#     The platform layer exposes Th8_Memcpy / Th8_Memmove /
#     Th8_Memset / Th8_Memcmp through the xMem* callbacks so the
#     allocator's tracing, secure-zero, and macOS bzero hooks
#     stay engaged.  Calling libc memcpy / memmove / memset /
#     memcmp directly skips that layer.  Boundary files are
#     exempt because they implement the bridge.
#
###############################################################################

audit_rule "direct-libc-memcpy" \
    {\ymemcpy\s*\(} \
    "direct libc memcpy() bypasses the TH8 platform layer" \
    "use Th8_Memcpy(interp, dst, src, n)" \
    $::PLATFORM_BOUNDARY_FILES

audit_rule "direct-libc-memmove" \
    {\ymemmove\s*\(} \
    "direct libc memmove() bypasses the TH8 platform layer" \
    "use Th8_Memmove(interp, dst, src, n)" \
    $::PLATFORM_BOUNDARY_FILES

audit_rule "direct-libc-memset" \
    {\ymemset\s*\(} \
    "direct libc memset() bypasses the TH8 platform layer" \
    "use Th8_Memset(interp, dst, c, n) -- routes c==0 through the platform's secure-zero implementation" \
    $::PLATFORM_BOUNDARY_FILES

audit_rule "direct-libc-memcmp" \
    {\ymemcmp\s*\(} \
    "direct libc memcmp() bypasses the TH8 platform layer" \
    "use Th8_Memcmp(interp, a, b, n)" \
    $::PLATFORM_BOUNDARY_FILES


###############################################################################
#
# Rule: direct-libc-strops
#
#     Same rationale as direct-libc-mem-ops, for the string ops
#     that the platform exposes (xStrlen / xStrcmp / xStrchr /
#     xAtoi / xVsnprintf / xQsort).  Direct libc strlen, strcmp,
#     etc. do not pass through any TH8 hook so are banned outside
#     boundary files.
#
###############################################################################

audit_rule "direct-libc-strlen" \
    {\ystrlen\s*\(} \
    "direct libc strlen() bypasses the TH8 platform layer" \
    "use Th8_Strlen(interp, z) or pass an explicit length" \
    $::PLATFORM_BOUNDARY_FILES

audit_rule "direct-libc-strcmp" \
    {\ystrcmp\s*\(} \
    "direct libc strcmp() bypasses the TH8 platform layer" \
    "use Th8_Strcmp(interp, a, b)" \
    $::PLATFORM_BOUNDARY_FILES

audit_rule "direct-libc-strchr" \
    {\ystrchr\s*\(} \
    "direct libc strchr() bypasses the TH8 platform layer" \
    "use Th8_Strchr(interp, z, c)" \
    $::PLATFORM_BOUNDARY_FILES

audit_rule "direct-libc-vsnprintf" \
    {\y(?:vsnprintf|snprintf|vsprintf|sprintf)\s*\(} \
    "direct libc vsnprintf/snprintf/vsprintf/sprintf bypasses TH8 formatting" \
    "use the th8_spilornis_*snprintf bridge or Th8_*Result formatting helpers" \
    $::PLATFORM_BOUNDARY_FILES


###############################################################################
#
# Rule: direct-libc-process-control
#
#     Process-control libc calls (abort, raise, _exit, exit, getenv,
#     putenv, setenv, unsetenv, chdir, system, execve, exec*) bypass
#     the platform layer's xPanic, xKeyValue / xGetEnv / xSetCwd
#     callbacks (and on iOS / Android they may be sandboxed or
#     deprecated entirely).  Boundary files are exempt because they
#     implement the bridge.
#
###############################################################################

audit_rule "direct-libc-abort" \
    {\y(?:abort|raise|_exit|exit)\s*\(} \
    "direct libc abort()/raise()/exit()/_exit() bypasses xPanic" \
    "use Th8_Panic / Th8_Fatal / xPanic via the platform vtable" \
    $::PLATFORM_BOUNDARY_FILES

audit_rule "direct-libc-env" \
    {\y(?:getenv|putenv|setenv|unsetenv)\s*\(} \
    "direct libc env access bypasses the xKeyValue/xGetEnv platform layer" \
    "use Th8_GetEnv (which routes through the platform-supplied xGetEnv callback)" \
    $::PLATFORM_BOUNDARY_FILES

audit_rule "direct-libc-cwd" \
    {\y(?:chdir|getcwd|_chdir|_getcwd)\s*\(} \
    "direct libc chdir/getcwd bypasses the platform xSetCwd/xGetCwd callbacks" \
    "use Th8_SetCwd / Th8_GetCwd via the platform layer" \
    $::PLATFORM_BOUNDARY_FILES

audit_rule "direct-libc-fileio" \
    {\y(?:fopen|freopen|popen|tmpfile|mkstemp|unlink|remove|rename|access|stat|lstat|fstat)\s*\(} \
    "direct libc filesystem call bypasses platform xGetData/xSameFile/etc." \
    "use Th8_GetData / Th8_GetTemporaryData / Th8_DeleteTemporaryData" \
    $::PLATFORM_BOUNDARY_FILES


###############################################################################
#
# Rule: dangerous-libc-call
#
#     Some libc functions are banned outright -- not because they
#     bypass the platform layer, but because they are intrinsically
#     unsafe (gets), or are the canonical buffer-overflow primitive
#     (strcpy / strcat / sprintf without size guard).  Even within
#     boundary files these should be replaced with explicit-length
#     or safe-formatter equivalents.
#
###############################################################################

audit_rule "dangerous-libc-gets" \
    {\ygets\s*\(} \
    "libc gets() is unsafe -- has no length parameter and was removed in C11" \
    "use fgets() with explicit buffer size, or read via the platform layer"

audit_rule "dangerous-libc-strcpy-strcat" \
    {\y(?:strcpy|strcat)\s*\(} \
    "libc strcpy/strcat is unsafe -- no length parameter; common buffer-overflow vector" \
    "use Th8_Memcpy with an explicit byte count, or Th8_StringAppend"


###############################################################################
#
# Rule: alloc-str-misuse
#
#     The TH8_ALLOC family includes TH8_ALLOC_STR(interp, nLen) which
#     allocates nLen+1 bytes (the +1 is for the trailing NUL),
#     overflow-safe.  Writing TH8_ALLOC(interp, n + 1) instead of
#     TH8_ALLOC_STR(interp, n) circumvents that overflow protection
#     because the bare `n + 1` add can overflow when n is SIZE_MAX.
#     The same pattern applies to ALLOC_ADD vs ALLOC_STR_ADD and
#     ALLOC_MUL vs ALLOC_STR_MUL.
#
###############################################################################

audit_rule "alloc-str-misuse" \
    {\yTH8_ALLOC\s*\([^,]+,[^)+]+\+\s*1\s*\)} \
    "TH8_ALLOC(interp, X+1) circumvents the overflow-safe +1 in TH8_ALLOC_STR" \
    "use TH8_ALLOC_STR(interp, X) for NUL-terminated buffers"

audit_rule "alloc-add-str-misuse" \
    {\yTH8_ALLOC_ADD\s*\([^,]+,[^,]+,[^)+]+\+\s*1\s*\)} \
    "TH8_ALLOC_ADD(interp, k, n+1) circumvents the overflow-safe +1 in TH8_ALLOC_STR_ADD" \
    "use TH8_ALLOC_STR_ADD(interp, k, n) for NUL-terminated buffers"


###############################################################################
#
# Rule: platform-version-init
#
#     A static initializer for Th8_Platform must use the current
#     ABI version number.  Mismatched versions silently break
#     Th8_MergePlatform calls (which check version compatibility).
#     The CURRENT version is 3 (set after the xKeyValue addition;
#     bump this when the next ABI break lands).  Detect:
#
#         static Th8_Platform foo = {
#             N,                       <-- if N != 3, flag
#             ...
#         };
#
#     Heuristic: a line declaring a Th8_Platform variable followed
#     by an opening `{` and a numeric literal that is not 3.
#
###############################################################################

# The trailing `\s*,` requires a comma after the leading integer,
# which indicates an explicit field-list initializer.  This avoids
# false-flagging the idiomatic zero-clear `Th8_Platform foo = {0};`
# (no comma -- a single-element initializer that zeros every field
# via aggregate-initialization rules).
audit_rule "platform-version-init" \
    {\yTh8_Platform\s+\w+\s*=\s*\{\s*[0-2]\s*,} \
    "Th8_Platform initializer uses outdated nVersion (current ABI is 3)" \
    "set the leading integer to 3 to match the current Th8_Platform ABI"


###############################################################################
#
# Rule: direct-libc-stdio
#
#     Direct stdio calls (printf, fprintf, puts, putchar, fputs,
#     fputc, getchar, fgetc, fread, fwrite) bypass the platform's
#     xInput / xOutput / xOutputError callbacks.  The codebase
#     currently has zero hits in non-boundary code; this rule
#     keeps it that way.  Boundary files are exempt.
#
###############################################################################

audit_rule "direct-libc-stdio" \
    {\y(?:printf|fprintf|puts|putchar|fputs|fputc|getchar|fgetc|fread|fwrite)\s*\(} \
    "direct libc stdio bypasses the TH8 platform xInput/xOutput layer" \
    "use Th8_AppendOutput / Th8_AppendErrorOutput / Th8_GetInputLine via the platform layer" \
    $::PLATFORM_BOUNDARY_FILES


###############################################################################
#
# Rule: direct-libc-time
#
#     Direct time-related libc calls (time, clock, gmtime,
#     localtime, mktime, gettimeofday, clock_gettime) bypass the
#     platform's xTimeMs / xTimeUs callbacks.  Time-of-day reads
#     are also a side-channel concern -- centralising them lets a
#     test harness or sandbox stub the clock for deterministic
#     evaluation.
#
###############################################################################

audit_rule "direct-libc-time" \
    {\y(?:time|clock|gmtime|localtime|mktime|gettimeofday|clock_gettime)\s*\(} \
    "direct libc time/clock call bypasses the platform xTimeMs/xTimeUs layer" \
    "use Th8_TimeMs / Th8_TimeUs via the platform layer" \
    $::PLATFORM_BOUNDARY_FILES


###############################################################################
#
# Rule: direct-libc-signal-jmp
#
#     Direct signal-handling and non-local-jump calls (signal,
#     sigaction, setjmp, longjmp, sigsetjmp, siglongjmp).  TH8
#     does not use these primitives in core code -- error
#     propagation is via TH8_OK / TH8_ERROR return codes through
#     the call chain, and panics go through xPanic (which can
#     abort but never longjmp's out of the interpreter).
#     Introducing a signal handler or a non-local jump in core
#     code would silently break the deterministic-error-path
#     model the rest of the codebase relies on.
#
###############################################################################

audit_rule "direct-libc-signal-jmp" \
    {\y(?:signal|sigaction|setjmp|longjmp|sigsetjmp|siglongjmp|_setjmp|_longjmp)\s*\(} \
    "direct signal/setjmp/longjmp call breaks the TH8 deterministic-error-path model" \
    "propagate errors via TH8_ERROR return codes; for unrecoverable failures call Th8_Panic which routes through xPanic" \
    $::PLATFORM_BOUNDARY_FILES


###############################################################################
#
# Rule: pthread-direct-call
#
#     Direct pthread API calls bypass the platform's xMutexInit /
#     xMutexEnter / xMutexLeave / xIntCmpXchg / xMemBarrier
#     callbacks.  TH8 supports both pthread (POSIX) and Win32
#     critical-section synchronisation by routing through these
#     callbacks; a bare pthread_* call would compile only on
#     POSIX and break the multi-platform contract.
#
###############################################################################

audit_rule "pthread-direct-call" \
    {\ypthread_(?:create|join|detach|cancel|mutex_(?:init|lock|unlock|destroy)|cond_(?:init|wait|signal|broadcast|destroy)|key_(?:create|delete)|getspecific|setspecific|once|self|equal|atfork)\s*\(} \
    "direct pthread API call bypasses the TH8 platform threading layer" \
    "use Th8_MutexInit / Th8_MutexEnter / Th8_MutexLeave / Th8_IntCmpXchg / Th8_MemBarrier" \
    $::PLATFORM_BOUNDARY_FILES


###############################################################################
#
# Rule: windows-h-in-public-header
#
#     The TH8 public header chain is documented as not requiring
#     any platform-specific system headers (portability.md, and
#     the explicit claim in section 7 of the conference paper:
#     `<windows.h>` is not included; CRITICAL_SECTION is defined
#     inline in th8_plat.h via opaque void*-sized storage).
#     Including <windows.h> in any of the public-facing headers
#     would expose every embedder's translation units to the
#     entire Win32 namespace -- a known portability and
#     compilation-time hazard.
#
#     Only th8_meta_win32.h is allowed to include <windows.h>;
#     it is itself a private build-time meta-header included by
#     .c files that need Win32 API access.
#
###############################################################################

audit_rule "windows-h-in-public-header" \
    {^\s*#\s*include\s+<windows\.h>} \
    "<windows.h> in a public-facing header pollutes embedder translation units" \
    "remove the include; use opaque void*-sized storage in th8_plat.h instead" \
    {} \
    {src/th8.h src/th8_int.h src/th8_int_core.h src/th8_plat.h src/th8Decls.h src/th8_mem.h src/th8_keys.h src/th8_secure.h src/th8_protect.h}


###############################################################################
#
# Industry secure-coding rules.
#
#     The following rules codify well-known C secure-coding banned-
#     function lists from CERT C, CWE Top 25, and Microsoft's Security
#     Development Lifecycle.  Each ban has a CERT-C reference for the
#     audit trail.  These rules deliberately do NOT exempt boundary
#     files: the named functions are dangerous everywhere, and where
#     legitimately needed (which is nowhere in TH8 today) an explicit
#     AUDIT-OK marker with a citation is the right way to document
#     the exception.
#
###############################################################################

# CERT MSC30-C / CWE-338: rand()/srand() are NOT cryptographically
# secure and must not be used in any context where unpredictability
# matters (key generation, nonce generation, sandbox-resistant tag
# selection, security-sensitive randomization, etc.).
audit_rule "cert-rand-prng" \
    {\y(?:rand|srand)\s*\(} \
    "libc rand()/srand() is not a cryptographically secure PRNG" \
    "use Th8_RandomBytes via the platform xRandomBytes callback (kernel-grade entropy)"

# CERT FIO21-C, FIO47-C / CWE-377: tmpnam(), tempnam(), mktemp() are
# vulnerable to TOCTOU race conditions -- the name is generated at
# one moment and the file created at another, and the gap is enough
# for an attacker on the same filesystem to predict and pre-create
# the path with a symlink to an attacker-chosen target.
audit_rule "cert-insecure-tempfile" \
    {\y(?:tmpnam|tempnam|mktemp|_tempnam)\s*\(} \
    "insecure temp-file creation -- TOCTOU vulnerable name-then-create" \
    "use mkstemp() / mkstemps() (atomic name+open) or the platform's xGetTemporaryData callback"

# CERT MEM05-C / CWE-789: alloca() and dynamic-size VLAs perform
# unbounded stack growth.  An attacker-controlled size leads to a
# stack overflow with no allocator failure path -- the process
# simply jumps off the bottom of its stack.  Use the heap.
audit_rule "cert-alloca-stack-growth" \
    {\y(?:alloca|_alloca|__builtin_alloca)\s*\(} \
    "alloca() bypasses heap and risks unbounded stack growth on attacker-controlled sizes" \
    "use TH8_ALLOC / TH8_ALLOC_STR via the platform allocator (heap, with size checks)"

# CERT ENV33-C / CWE-78: system() invokes the user shell with the
# given command string and is the canonical command-injection
# primitive when any part of the string is attacker-influenced.
# Even when the string is a literal, system() pulls in the entire
# shell-startup environment, $PATH search, etc.  TH8 has no
# legitimate use; if shelling out becomes necessary, route it
# through the platform's xLoad/xUnload (for libraries) or a
# future xSystem callback that can be sandboxed.
audit_rule "cert-system-exec" \
    {\y(?:system|popen|_popen|execlp|execl|execvp|execv|execle|execve)\s*\(} \
    "system()/popen()/exec*() invokes the shell or replaces the process; either is a command-injection primitive" \
    "for child processes use a sandboxed platform callback; for library loading use Th8_Load"

# CWE-242 (Use of Inherently Dangerous Functions) plus the C11
# Annex K rationale: scanf-family without explicit %s width
# specifiers is a buffer-overflow primitive identical to gets().
# Even WITH width specifiers the API is error-prone (no return-
# value check leaves the destination undefined on conversion
# failure).  Banned outright.
audit_rule "cert-scanf-banned" \
    {\y(?:scanf|sscanf|fscanf|vscanf|vsscanf|vfscanf)\s*\(} \
    "scanf-family is a buffer-overflow / undefined-conversion primitive" \
    "parse explicitly: read into a sized buffer with fgets/Th8_GetInputLine, then strtol/strtod/Th8_ToInt with explicit error handling"

# CERT STR07-C / CWE-170: strncpy() does NOT NUL-terminate when the
# source length is >= the destination size.  strncat() interprets
# its size argument as the maximum number of bytes to APPEND, not
# the destination buffer size, leading to off-by-one overflows in
# nearly every textbook-correct usage.  Both are banned.
audit_rule "cert-strncpy-truncation" \
    {\y(?:strncpy|strncat)\s*\(} \
    "strncpy() may not NUL-terminate; strncat() length argument is widely misunderstood" \
    "compute length explicitly and use Th8_Memcpy with an explicit byte count, or Th8_StringAppend"

# CERT EXP45-C / CWE-481: assignment in a conditional context is
# the textbook == vs = typo.  The rule fires on `if (x = y)`,
# `while (x = y)`, etc., where the operator is a single `=` not
# followed by another `=`.  False-positive risk: cases like
# `if ((p = malloc(...)) != NULL)` use the parenthesised-and-
# compared idiom, which the regex correctly does NOT flag (the
# `=` is inside an inner paren and followed by `)` not the
# closing `)` of the if).
#
# The `[^=!<>]\s*=\s*[^=]` core requires:
#   - the char before `=` is NOT one of `=`, `!`, `<`, `>` (rules
#     out `==`, `!=`, `<=`, `>=`)
#   - the char after `=` is NOT another `=` (rules out `==`)
audit_rule "cert-assignment-in-conditional" \
    {\y(?:if|while)\s*\(\s*[a-zA-Z_]\w*\s*=\s*[^=]} \
    "assignment in if/while conditional -- likely a == typo" \
    "use == for comparison, or wrap intentional assignment in extra parens: if ((x = expr) != 0)"


###############################################################################
#
# Rule: internal-uppercase-name
#
#     Per the project naming convention (feedback_internal_naming),
#     INTERNAL helpers use the lowercase th8Whatever spelling; the
#     uppercase Th8_Whatever spelling is reserved for the public
#     API surface.  A `static` function definition with `Th8_` is
#     therefore an audit signal: either it should be renamed to
#     th8Whatever, or the static keyword is wrong (and the
#     function should be exposed in th8.h).
#
###############################################################################

audit_rule "internal-uppercase-name" \
    {^\s*static\s+[\w*\s]+\yTh8_\w+\s*\(} \
    "static function uses Th8_ public-API naming convention" \
    "rename to th8Whatever (lowercase prefix) or expose via th8.h"


###############################################################################
#
# Rule: bare-th8-alloc-primitive
#
#     Direct calls to Th8_Malloc / Th8_Realloc / Th8_AttemptMalloc /
#     Th8_AttemptRealloc bypass the TH8_ALLOC* macro family that
#     captures __FILE__ and __LINE__ for diagnostic tracing AND
#     enforces overflow-checked size arithmetic.  The core must use
#     TH8_ALLOC, TH8_ALLOC_STR, TH8_ALLOC_MUL, TH8_ALLOC_MUL_ADD,
#     etc. -- the bare primitives are reserved for the platform
#     bridge and for callbacks bound to foreign-library signatures
#     (e.g. libtommath / Spencer-regex realloc hooks where the
#     signature is dictated by the upstream library).
#
###############################################################################

# The leading `^[^#]+` requirement filters out:
#   * macro definitions (`#define Th8_Malloc(interp, n) ...`)
#   * function definitions whose name appears at column 0
#     (TH8 convention is to put the return-type on a separate
#     line, so the definition's function-name line begins with
#     `Th8_X(` -- no preceding non-whitespace).
# Real calls always have at least one non-`#` character before
# the function name (e.g. `aNew = (T*)Th8_AttemptRealloc(...)`,
# `return Th8_Malloc(...)`).  Header-file declarations are
# excluded via exclude_globs since they list every prototype.
audit_rule "bare-th8-alloc-primitive" \
    {^[^#]+\yTh8_(Attempt)?(Malloc|Realloc)\s*\(} \
    "direct call to bare Th8_(Attempt)?(Malloc|Realloc) primitive" \
    "use TH8_ALLOC / TH8_ALLOC_MUL / TH8_ALLOC_MUL_ADD (or add a TH8_REALLOC* macro family if reallocating)" \
    [concat $::PLATFORM_BOUNDARY_FILES \
    {*.h src/th8Decls.h src/th8.h src/th8_int.h src/th8_int_core.h}]


###############################################################################
#
# Rule: interp-null-mem-noop
#
#     The Th8_Mem* family (Memcpy/Memmove/Memset/Memcmp) silently
#     returns its destination/zero when interp is NULL, because
#     the platform callbacks need a valid interp to dispatch the
#     trace/secure-zero hooks.  Passing a literal NULL or 0 as
#     interp therefore SILENTLY makes the call a no-op and the
#     destination buffer is left uninitialised -- a real bug found
#     in Phase 3 of the binary plugin (float bitcast via
#     Th8_Memcpy(NULL, ...) left float buffers indeterminate).
#     Pass a valid interp through, or perform the byte copy via a
#     type-punning union when a "memcpy without an interp" is what
#     you actually want.
#
###############################################################################

audit_rule "interp-null-memcpy" \
    {\yTh8_Memcpy\s*\(\s*(NULL|0)\s*,} \
    "Th8_Memcpy(NULL, ...) silently no-ops -- destination left uninitialised" \
    "thread a valid interp through, or use a type-punning union if you specifically want a plain byte copy" \
    $::PLATFORM_BOUNDARY_FILES

audit_rule "interp-null-memmove" \
    {\yTh8_Memmove\s*\(\s*(NULL|0)\s*,} \
    "Th8_Memmove(NULL, ...) silently no-ops -- destination left unchanged" \
    "thread a valid interp through" \
    $::PLATFORM_BOUNDARY_FILES

audit_rule "interp-null-memset" \
    {\yTh8_Memset\s*\(\s*(NULL|0)\s*,} \
    "Th8_Memset(NULL, ...) silently no-ops -- destination left uninitialised" \
    "thread a valid interp through (or use a volatile-zero loop directly if there is genuinely no interp at the call site)" \
    $::PLATFORM_BOUNDARY_FILES

audit_rule "interp-null-memcmp" \
    {\yTh8_Memcmp\s*\(\s*(NULL|0)\s*,} \
    "Th8_Memcmp(NULL, ...) silently returns 0 -- comparison result is meaningless" \
    "thread a valid interp through" \
    $::PLATFORM_BOUNDARY_FILES


###############################################################################
#
# Rule: meta-header-cross-include
#
#     Per feedback_meta_headers, the th8_meta_*.h headers must be
#     included only by .c files in the order they document, and
#     must NOT include each other.  Cross-includes between meta
#     headers create the "kitchen-sink" header anti-pattern this
#     project explicitly rejects.
#
###############################################################################

audit_rule "meta-header-cross-include" \
    {^#\s*include\s+"th8_meta_\w+\.h"} \
    "meta-header includes another meta-header" \
    "remove the include; .c files must list meta-headers directly in their canonical order" \
    {} \
    {*/th8_meta_*.h src/th8_meta_*.h th8_meta_*.h}

#
# no-non-ascii --
#     Reject any byte > 0x7F in a TH8 source file.  Per
#     doc/c_source_code_style.md Sec. 7 the source tree is
#     7-bit ASCII; non-ASCII data goes through `\xNN` /
#     `\uNNNN` string-literal escapes.
#
#     This rule scans the RAW line (not the comment-stripped
#     line) so it catches non-ASCII inside comments and string
#     literals too.  scan_file dispatches it via the
#     ::audit_raw_rules side-table; suppression via
#     `/* AUDIT-OK[no-non-ascii]: reason */` works the same way
#     as for normal rules.
#
set ::audit_raw_rules [list \
    [list \
	"no-non-ascii" \
	{[^\x00-\x7f]} \
	"non-ASCII byte in source (style guide Sec. 7 forbids)" \
	"replace with the ASCII equivalent or a \\xNN / \\uNNNN escape inside a string literal" \
	{} \
	{}] \
    [list \
	"trailing-whitespace" \
	{[ \t]+$} \
	"trailing whitespace at end of line" \
	"strip the trailing space/tab; configure your editor to do this on save" \
	{} \
	{}] \
]


###############################################################################
#
# strip_non_code --
#
#     Walk the input character-by-character with a tiny state machine
#     and replace the contents of /* ... */ block comments, // line
#     comments, "..." string literals, and '...' character literals
#     with spaces, preserving line breaks and byte length.  After
#     this pass, regex rules run only against actual code tokens
#     and cannot false-positive on text that just happens to live
#     inside a string or comment.
#
#     The state machine is hand-rolled rather than reused via
#     regsub because regex-driven comment stripping is famously
#     unreliable in the face of strings containing /* sequences,
#     etc.  This is the same approach Fossil's codecheck1.c takes
#     (token_length() with TK_STR / TK_SPACE classification) but
#     compressed to the minimum needed for line-oriented scans.
#
#     Byte length is preserved so any downstream column-number
#     reporting still maps back to the original source.
#
###############################################################################

proc strip_non_code {text} {
  set out ""
  set i 0
  set n [string length $text]
  set state code
  set quote ""
  while {$i < $n} {
    set c [string index $text $i]
    set c2 [string index $text [expr {$i + 1}]]
    switch $state {
      code {
        if {$c eq "/" && $c2 eq "*"} then {
          append out "  "
          incr i 2
          set state block
        } elseif {$c eq "/" && $c2 eq "/"} then {
          append out "  "
          incr i 2
          set state line
        } elseif {$c eq "\"" || $c eq "'"} then {
          append out $c
          incr i
          set state literal
          set quote $c
        } else {
          append out $c
          incr i
        }
      }
      block {
        if {$c eq "*" && $c2 eq "/"} then {
          append out "  "
          incr i 2
          set state code
        } elseif {$c eq "\n"} then {
          append out "\n"
          incr i
        } else {
          append out " "
          incr i
        }
      }
      line {
        if {$c eq "\n"} then {
          append out "\n"
          set state code
        } else {
          append out " "
        }
        incr i
      }
      literal {
        if {$c eq "\\" && $c2 ne ""} then {
          # Escaped char inside a literal: blank both
          # bytes (and preserve newlines if the escape
          # is a line continuation).
          if {$c2 eq "\n"} then {
            append out " \n"
          } else {
            append out "  "
          }
          incr i 2
        } elseif {$c eq $quote} then {
          append out $c
          incr i
          set state code
        } elseif {$c eq "\n"} then {
          # Unterminated literal -- bail to code mode so
          # we don't swallow the rest of the file.
          append out "\n"
          incr i
          set state code
        } else {
          append out " "
          incr i
        }
      }
    }
  }
  return $out
}


###############################################################################
#
# extract_audit_ok --
#
#     Read the ORIGINAL (un-stripped) source line and return a list
#     of rule names suppressed by AUDIT-OK markers on that line.
#     Marker format:
#
#         /* AUDIT-OK[rule-name]: reason text */
#
#     Multiple markers (different rules) on one line are allowed.
#     The reason text is required but not validated -- this is a
#     human-readable audit trail, not a structured field.
#
###############################################################################

proc extract_audit_ok {rawLine} {
  set names [list]
  # Capture the bracketed list -- one or more rule names separated
  # by commas (no spaces inside the brackets so the parser stays
  # unambiguous).  Match only as far as the closing bracket; we do
  # not validate the trailing reason text (or its closing */)
  # because reasons may legitimately contain * (e.g., a pointer
  # type, an identifier ending in a wildcard glyph, a math
  # expression).
  set re {AUDIT-OK\[([A-Za-z0-9_,-]+)\]}
  set start 0
  while {1} {
    if {![regexp -indices -start $start $re $rawLine matchRange nameRange]} then { break }
    set raw [string range $rawLine [lindex $nameRange 0] [lindex $nameRange 1]]
    foreach n [split $raw ,] {
      set n [string trim $n]
      if {$n ne ""} then {
        lappend names $n
      }
    }
    set start [expr {[lindex $matchRange 1] + 1}]
  }
  return $names
}


###############################################################################
#
# scan_file --
#
#     Apply every registered rule to a single file, returning a
#     list of {lineNo ruleName rawLine description suggestion}
#     tuples for each violation.  Comments are stripped before
#     pattern matching to avoid in-comment false positives, but
#     suppression-marker scanning uses the original line so the
#     marker itself does not need to dodge the strip.
#
###############################################################################

#
# Return 1 if the rule should NOT be applied to the given path.
# A rule is skipped when:
#   * include_globs is non-empty AND the path matches none of them, OR
#   * exclude_globs is non-empty AND the path matches one of them.
# include_globs is checked first because it is the stricter scope.
#
proc rule_excludes_file {rule path} {
  set excludes [lindex $rule 4]
  set includes ""
  if {[llength $rule] > 5} then {
    set includes [lindex $rule 5]
  }
  if {[llength $includes] > 0} then {
    set matched 0
    foreach glob $includes {
      if {[string match $glob $path]} then {
        set matched 1
        break
      }
    }
    if {$matched == 0} then {
      return 1
    }
  }
  foreach glob $excludes {
    if {[string match $glob $path]} then {
      return 1
    }
  }
  return 0
}

#
# Collect AUDIT-OK suppression markers that apply to a given line.
#
# A marker on the same line (trailing inline comment) applies, AND
# a marker in the contiguous block of comment-only / blank lines
# IMMEDIATELY preceding the line also applies.  This lets reason
# text be wrapped across multiple lines for readability:
#
#     /* AUDIT-OK[rule-name]: a long-form reason that wraps at
#     ** ~78 columns to remain readable inside an editor without
#     ** horizontal scrolling. */
#     offending_line();
#
# The scan walks backward from the line of interest and stops at
# the first line whose stripped (comment-blanked) form contains
# any non-whitespace -- i.e. the first line containing actual
# code.  Blank lines and comment-only lines are transparent.
#
proc collect_audit_ok {rawArr strippedArr idx} {
  upvar 1 $rawArr rawLines
  upvar 1 $strippedArr strippedLines
  set names [extract_audit_ok [lindex $rawLines $idx]]
  #
  # Walk backward through:
  #   * comment-only / blank lines (stripped form is whitespace), AND
  #   * continuation lines of a multi-line statement (lines whose
  #     LAST non-whitespace character is NOT a statement
  #     terminator -- not `;`, `{`, or `}`).
  # Stop at the first line that actually ends a previous statement
  # (those markers belong to that statement, not this one).
  #
  # This covers the three idiomatic placements of an AUDIT-OK
  # block comment:
  #   1. Inline on the violation line itself.
  #   2. In a comment block immediately above the violation line.
  #   3. In a comment block above the first line of a multi-line
  #      function call whose intermediate line is the violation.
  #
  set i [expr {$idx - 1}]
  while {$i >= 0} {
    set s [string trimright [lindex $strippedLines $i]]
    if {$s ne ""} then {
      set last [string index $s end]
      if {$last eq ";" || $last eq "\{" || $last eq "\}"} then {
        break
      }
    }
    foreach n [extract_audit_ok [lindex $rawLines $i]] {
      lappend names $n
    }
    incr i -1
  }
  return $names
}

proc scan_file {path} {
  set fd [open $path r]
  set content [read $fd]
  close $fd

  set stripped [strip_non_code $content]
  set rawLines [split $content \n]
  set strippedLines [split $stripped \n]
  set violations [list]
  set nLines [llength $rawLines]

  for {set idx 0} {$idx < $nLines} {incr idx} {
    set raw [lindex $rawLines $idx]
    set strippedLine [lindex $strippedLines $idx]
    set suppressed [collect_audit_ok rawLines strippedLines $idx]
    foreach rule $::audit_rules {
      lassign $rule name regex desc fix
      if {[lsearch -exact $suppressed $name] >= 0} then { continue }
      if {[rule_excludes_file $rule $path]} then { continue }
      if {[regexp $regex $strippedLine]} then {
        lappend violations \
            [list [expr {$idx + 1}] $name $raw $desc $fix]
      }
    }
    #
    # Raw-line rules: same shape as $::audit_rules but the regex
    # is matched against the unstripped raw line (so it sees byte
    # content of comments and string literals).  Used by rules
    # that police "everywhere" properties such as ASCII purity.
    #
    foreach rule $::audit_raw_rules {
      lassign $rule name regex desc fix
      if {[lsearch -exact $suppressed $name] >= 0} then { continue }
      if {[rule_excludes_file $rule $path]} then { continue }
      if {[regexp $regex $raw]} then {
        lappend violations \
            [list [expr {$idx + 1}] $name $raw $desc $fix]
      }
    }
  }
  return $violations
}


###############################################################################
#
# Object-level CRT scan.
#
#     Replaces the per-platform shell drivers (formerly
#     tools/check_crt.sh and tools/check_crt_msvc.bat) with one
#     Tcl implementation that auto-detects the host's symbol
#     extractor (nm on POSIX, dumpbin on Windows).
#
#     Banned-function list:    tools/data/crt_functions.txt
#     Allow-list / exceptions: tools/data/crt_exceptions.txt
#
###############################################################################

#
# crt_basename --
#     Return the object-file basename used as the lookup key
#     for the allow-list and per-file exceptions.  Strips the
#     trailing .o / .obj suffix and the optional .pic infix
#     used by PIC builds.
#
proc crt_basename {obj} {
  set base [file tail $obj]
  regsub {\.(o|obj)$} $base "" base
  regsub {\.pic$} $base "" base
  return $base
}

#
# crt_load_data --
#     Read the banned-function list and the allow-list /
#     exception data.  Returns a dict with three keys:
#
#       banned       : list of CRT function names to flag
#       prefix       : list of file-prefix exemptions
#       compiler_ok  : list of always-allowed symbols
#       allow        : dict mapping basename -> list of symbols
#
proc crt_load_data {scriptdir} {
  set crtFile [file join $scriptdir data crt_functions.txt]
  set excFile [file join $scriptdir data crt_exceptions.txt]
  if {![file readable $crtFile]} then {
    error "audit_patterns: missing $crtFile"
  }
  if {![file readable $excFile]} then {
    error "audit_patterns: missing $excFile"
  }

  set banned [list]
  set fd [open $crtFile r]
  while {[gets $fd line] >= 0} {
    set line [string trim $line]
    if {$line eq "" || [string index $line 0] eq "#"} then { continue }
    lappend banned $line
  }
  close $fd

  set prefix [list]
  set compilerOk [list]
  set allow [dict create]
  set fd [open $excFile r]
  while {[gets $fd line] >= 0} {
    set raw [string trim $line]
    if {$raw eq "" || [string index $raw 0] eq "#"} then { continue }
    set tokens $raw
    set kind [lindex $tokens 0]
    switch -- $kind {
      prefix {
        lappend prefix [lindex $tokens 1]
      }
      compiler-ok {
        lappend compilerOk [lindex $tokens 1]
      }
      allow {
        set base [lindex $tokens 1]
        set syms [lrange $tokens 2 end]
        if {[dict exists $allow $base]} then {
          set existing [dict get $allow $base]
        } else {
          set existing [list]
        }
        foreach s $syms { lappend existing $s }
        dict set allow $base $existing
      }
      default {
        error "audit_patterns: unknown directive '$kind' in $excFile"
      }
    }
  }
  close $fd

  return [dict create \
      banned $banned \
      prefix $prefix \
      compiler_ok $compilerOk \
      allow $allow]
}

#
# crt_extract_undefined --
#     Run the host's symbol extractor on $obj and return the list
#     of undefined external symbol names with leading underscores
#     stripped (so the key matches the data file).
#
proc crt_extract_undefined {obj} {
  global tcl_platform
  set syms [list]
  if {$tcl_platform(platform) eq "windows"} then {
    # MSVC dumpbin: lines like
    #     007 00000000 UNDEF  notype ()    External     | _malloc
    if {[catch {exec dumpbin /symbols $obj} out]} then {
      return [list]
    }
    foreach line [split $out \n] {
      if {[regexp {UNDEF.*External.*\|\s*(\S+)} $line -> sym]} then {
        regsub {^_} $sym "" sym
        lappend syms $sym
      }
    }
  } else {
    # POSIX nm -u: each line is a single undefined symbol.
    if {[catch {exec nm -u $obj 2>/dev/null} out]} then {
      return [list]
    }
    foreach line [split $out \n] {
      set s [string trim $line]
      if {$s eq ""} then { continue }
      # Strip exactly ONE leading underscore (Mach-O / COFF
      # C-symbol convention).  Mirrors shell `sed 's/^_//'`;
      # do NOT use `^_+` here because `__exit` should remain
      # `_exit` (a different function from `exit`).
      regsub {^_} $s "" s
      lappend syms $s
    }
  }
  return $syms
}

#
# crt_scan_object --
#     Apply the allow-list / per-file exception rules to one
#     object file.  Returns the list of unexpected CRT symbols
#     found (empty list = no violations for this file).
#
proc crt_scan_object {obj data} {
  set base [crt_basename $obj]

  # Whole-file exemption.
  foreach p [dict get $data prefix] {
    if {[string match "${p}*" $base]} then {
      return [list]
    }
  }

  set bannedSet [dict create]
  foreach b [dict get $data banned] { dict set bannedSet $b 1 }
  set compilerOk [dict get $data compiler_ok]
  set allow [dict get $data allow]
  set perFile [list]
  if {[dict exists $allow $base]} then {
    set perFile [dict get $allow $base]
  }

  set undefined [crt_extract_undefined $obj]
  set bad [list]
  foreach s $undefined {
    if {![dict exists $bannedSet $s]} then { continue }
    if {[lsearch -exact $compilerOk $s] >= 0} then { continue }
    if {[lsearch -exact $perFile $s] >= 0} then { continue }
    lappend bad $s
  }
  return $bad
}

#
# run_crt_objects --
#     Walk $bindir for .o (POSIX) or .obj (MSVC) files and
#     report any unexpected CRT-symbol references.  Mirrors the
#     output format of the legacy check_crt.sh / .bat scripts so
#     the new tool can be substituted byte-for-byte.
#
proc run_crt_objects {bindir scriptdir} {
  global tcl_platform
  set ext [expr {$tcl_platform(platform) eq "windows" ? "obj" : "o"}]
  if {![file isdirectory $bindir]} then {
    puts stderr "audit_patterns: $bindir is not a directory"
    return 2
  }

  set data [crt_load_data $scriptdir]
  puts "CRT dependency scan: $bindir"
  puts ""

  set errors 0
  set objs [lsort [glob -nocomplain -directory $bindir *.$ext]]
  foreach obj $objs {
    if {[catch {crt_scan_object $obj $data} bad]} then {
      puts stderr "audit_patterns: error scanning $obj: $bad"
      incr errors
      continue
    }
    if {[llength $bad] > 0} then {
      # Match shell parity: leading space before first symbol,
      # so the output is "FAIL: name.o: sym1 sym2 ..." not
      # "FAIL: name.o:sym1 sym2 ...".
      puts "FAIL: [file tail $obj]: [join $bad { }]"
      incr errors
    }
  }

  puts ""
  if {$errors == 0} then {
    puts "OK: No unexpected CRT dependencies."
    return 0
  } else {
    puts "FAIL: $errors file(s) with unexpected CRT dependencies."
    return 1
  }
}


###############################################################################
#
# Source-level scan (existing 28+ rules; subcommand `source`).
#
###############################################################################

#
# default_source_files --
#     Default file set when no FILE arguments are given to the
#     `source` subcommand.  Mirrors the historical behaviour.
#
proc default_source_files {} {
  set files [list]
  foreach pattern {
    src/*.c
    src/*.h
    src/plugins/*.c
    src/plugins/*/*.c
    src/sqlite3/*.c
    src/test/*.c
  } {
    foreach f [glob -nocomplain $pattern] {
      lappend files $f
    }
  }
  return $files
}

#
# run_source_scan --
#     Apply the audit_rule registry to each file.  Returns 0 on
#     no violations, 1 on violations, 2 on tool error.
#
proc run_source_scan {fileArgs} {
  if {[llength $fileArgs] > 0} then {
    set files $fileArgs
  } else {
    set files [default_source_files]
  }

  set total 0
  foreach f $files {
    if {[catch {scan_file $f} violations]} then {
      puts stderr "audit_patterns: error scanning $f: $violations"
      return 2
    }
    foreach v $violations {
      lassign $v lineNo name raw desc fix
      puts "$f:$lineNo: \[$name\] $desc"
      puts "  > [string trim $raw]"
      puts "  fix: $fix"
      puts ""
      incr total
    }
  }

  if {$total == 0} then {
    puts "audit_patterns: 0 violations across [llength $files] files"
    return 0
  } else {
    puts "audit_patterns: $total violation(s) across [llength $files] files"
    return 1
  }
}


###############################################################################
#
# Format check (subcommand `format`).
#
#     Defers to tools/format_code.tcl --check, which drives
#     clang-format with the project's .clang-format settings.
#     A separate driver script keeps clang-format integration in
#     one place.
#
###############################################################################

proc run_format_check {fileArgs scriptdir} {
  set driver [file join [file dirname $scriptdir] tools format_code.tcl]
  if {![file readable $driver]} then {
    puts stderr "audit_patterns: missing $driver"
    return 2
  }
  set cmd [list [info nameofexecutable] $driver --check]
  foreach f $fileArgs { lappend cmd $f }
  set rc 0
  if {[catch {exec {*}$cmd >@stdout 2>@stderr} err]} then {
    set rc 1
  }
  return $rc
}


###############################################################################
#
# Main.
#
###############################################################################

set ::SCRIPT_DIR [file normalize [file dirname [info script]]]

#
# Default subcommand and argument parsing.
#
#   tclsh audit_patterns.tcl                  -> source (legacy default)
#   tclsh audit_patterns.tcl FILE [FILE ...]  -> source FILE [FILE ...]
#   tclsh audit_patterns.tcl source [FILE ...]
#   tclsh audit_patterns.tcl format [FILE ...]
#   tclsh audit_patterns.tcl crt-objects [BINDIR]
#   tclsh audit_patterns.tcl all [BINDIR] [FILE ...]
#

set ::SUBCOMMAND_NAMES {source format crt-objects all}

set subcmd "source"
set objdir "bin"
set fileArgs [list]

if {[llength $::argv] > 0} then {
  set first [lindex $::argv 0]
  if {[lsearch -exact $::SUBCOMMAND_NAMES $first] >= 0} then {
    set subcmd $first
    set rest [lrange $::argv 1 end]
    if {$subcmd in {crt-objects all} && [llength $rest] > 0} then {
      set candidate [lindex $rest 0]
      if {[file isdirectory $candidate]} then {
        set objdir $candidate
        set rest [lrange $rest 1 end]
      }
    }
    set fileArgs $rest
  } else {
    # Backwards compatibility: positional file args.
    set fileArgs $::argv
  }
}

set rc 0
switch -- $subcmd {
  source {
    set rc [run_source_scan $fileArgs]
  }
  format {
    set rc [run_format_check $fileArgs $::SCRIPT_DIR]
  }
  crt-objects {
    set rc [run_crt_objects $objdir $::SCRIPT_DIR]
  }
  all {
    set rcSource [run_source_scan $fileArgs]
    set rcFormat [run_format_check $fileArgs $::SCRIPT_DIR]
    set rcCrt    [run_crt_objects $objdir $::SCRIPT_DIR]
    foreach r [list $rcSource $rcFormat $rcCrt] {
      if {$r > $rc} then { set rc $r }
    }
  }
  default {
    puts stderr "audit_patterns: unknown subcommand '$subcmd'"
    set rc 2
  }
}

exit $rc
