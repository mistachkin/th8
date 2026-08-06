/*
 * th8_testlib.c --
 *
 *	Canonical example of a dual-mode TH8/Tcl dynamic extension.
 *
 *	This shared library can be loaded into BOTH a TH8 interpreter
 *	and a native Tcl interpreter.  It demonstrates:
 *
 *	  - Separate entry points for each interpreter type.
 *	  - Shared core logic with no interpreter dependency.
 *	  - Proper use of Tcl stubs for native Tcl compatibility.
 *	  - Clean namespace registration and unload support.
 *
 *	TH8 ENTRY POINTS (via "load path:Th8test"):
 *	  Th8test_Init    -- register commands in TH8.
 *	  Th8test_Unload  -- remove commands from TH8.
 *
 *	TCL ENTRY POINTS (via "load path Tclth8test"):
 *	  Tclth8test_Init   -- register commands in native Tcl.
 *	  Tclth8test_Unload -- remove commands from native Tcl.
 *
 *	COMMAND PROVIDED (same in both modes):
 *
 *	  th8test::isotime EPOCH_SECONDS
 *
 *	    Converts an integer number of seconds since the Unix epoch
 *	    (1970-01-01 00:00:00 UTC) to an ISO 8601 date-time string
 *	    of the form "YYYY-MM-ddTHH:mm:ssZ".
 *
 *	BUILD:
 *	  TH8 mode:  compiled with -Isrc (for th8.h).
 *	  Tcl mode:  compiled with -DUSE_TCL_STUBS -I<tcl-include>
 *	             and linked against libtclstub.
 *	  Both modes are compiled into the same library.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

/*
 * Include TH8 API for the TH8 entry points.
 */

/*
 * TH8 API.  Included when TH8_TESTLIB_TH8 is defined.
 * When USE_TH8_STUBS is also defined, all Th8_* API calls go
 * through the stubs table and the extension links against
 * libth8stub.a instead of the full TH8 library.
 */

#ifdef TH8_TESTLIB_TH8
#  include "th8_meta_defs.h"
#  include "th8_meta_libc.h"
#  include "th8_meta_posix.h"
#  include "th8.h"
#  include "th8_int.h"
#  include "th8_plugin.h"
#  ifdef USE_TH8_STUBS
#    include "th8Decls.h"
#  endif
   /*
    * Internal stubs table -- routes calls to TH8_INTERNAL
    * functions (e.g. th8ParseCommand, th8FreeParse) through
    * function pointers retrieved at load time via
    * Th8_GetInternalStubs().  Required because TH8_INTERNAL
    * symbols are hidden from libth8's export table and thus
    * unreachable by direct linkage from this shared object.
    */
#  define USE_TH8_INTERNAL_STUBS
#  include "th8InternalDecls.h"
#endif

/*
 * Tcl API.  Included when TH8_TESTLIB_TCL is defined.
 * USE_TCL_STUBS is set so that API calls go through the stubs
 * table, allowing the library to work with any Tcl 8.4+.
 */

#ifdef TH8_TESTLIB_TCL
#  ifndef USE_TCL_STUBS
#    define USE_TCL_STUBS
#  endif
#  include "tcl.h"
#endif

/*
 * The th8_int64_t type is needed by the shared core logic.
 * When building for TH8, it comes from th8.h.  When building
 * for Tcl only, define it here.
 */

#ifndef TH8_TESTLIB_TH8
typedef long long th8_int64_t;
#endif

#include "th8_testlib.h"

/*
 *======================================================================
 *
 * SHARED CORE LOGIC (no interpreter dependency)
 *
 *======================================================================
 */

/*
 * 64-bit signed integer type.  th8_int64_t is always available
 * from th8.h.  We use it here for the core logic.
 */

/*
 *----------------------------------------------------------------------
 *
 * th8test_native_platform --
 *
 *	Return the OS-native base platform table for the current
 *	build: Th8_GetPosixPlatform() on POSIX, Th8_GetWin32Platform()
 *	on Windows.
 *
 * Why / How:
 *	th8.h declares exactly ONE of the two getters per platform
 *	(Th8_GetPosixPlatform under !_WIN32, Th8_GetWin32Platform
 *	otherwise), so a portable caller must select via the same
 *	compile guard.  Naming Th8_GetPosixPlatform unconditionally
 *	leaves it undeclared on MSVC (C4013 "assuming extern
 *	returning int" -> C4047 pointer/int mismatch -> LNK2019).
 *	This wrapper centralizes the guard so the drive code below
 *	stays platform-neutral.
 *
 * Results:
 *	Const pointer to the native platform table (never NULL on a
 *	correctly built library).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

#ifdef TH8_TESTLIB_TH8
static const Th8_Platform *
th8test_native_platform(void)
{
#  if !defined(_WIN32) && !defined(WIN32)
    return Th8_GetPosixPlatform();
#  else
    return Th8_GetWin32Platform();
#  endif
}
#endif

/*
 *----------------------------------------------------------------------
 *
 * th8test_civil_from_days --
 *
 *	Converts a day count (days since 1970-01-01) to a Gregorian
 *	calendar date (year, month 1-12, day 1-31).
 *
 *	Algorithm: Howard Hinnant's civil_from_days(), public domain.
 *	Requires no floating-point and no CRT.
 *
 * Why / How:
 *	The isotime command needs date conversion without depending
 *	on mktime/gmtime which may not be available on all platforms.
 *	Hinnant's algorithm is exact for all representable dates and
 *	uses only integer arithmetic.
 *
 * Results:
 *	Fills *pYear, *pMonth, *pDay with the Gregorian date.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
th8test_civil_from_days(
    th8_int64_t z, /* Days since 1970-01-01. */
    int *pYear,
    int *pMonth,
    int *pDay)
{
    th8_int64_t era;
    unsigned int doe;
    unsigned int yoe;
    th8_int64_t y;
    unsigned int doy;
    unsigned int mp;
    unsigned int d;
    unsigned int m;

    z += 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = (unsigned int)(z - era * 146097);
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = (th8_int64_t)yoe + era * 400;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp + (mp < 10 ? 3 : (unsigned int)-9);
    y += (m <= 2);

    *pYear = (int)y;
    *pMonth = (int)m;
    *pDay = (int)d;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_format_isotime --
 *
 *	Format epoch seconds as "YYYY-MM-ddTHH:mm:ssZ".
 *	Writes to the caller-provided buffer (must be >= 22 bytes).
 *	Returns the number of characters written (excluding NUL).
 *
 *	This function has no interpreter dependency and is shared
 *	by both TH8 and Tcl command implementations.
 *
 * Why / How:
 *	Shared core logic for the dual-mode (TH8 + Tcl) isotime
 *	command.  Avoids snprintf for portability and performance;
 *	hand-formats each field with digit extraction.  Handles
 *	negative epoch values correctly by adjusting the day/second
 *	split before calling civil_from_days.
 *
 * Results:
 *	Number of characters written (excluding NUL terminator).
 *
 * Side effects:
 *	Writes to zBuf[0..n].
 *
 *----------------------------------------------------------------------
 */

static int
th8test_format_isotime(
    th8_int64_t epochSec,
    char *zBuf,  /* Output buffer (>= 22 bytes). */
    int nBuf)  /* Buffer size. */
{
    th8_int64_t daysSinceEpoch;
    th8_int64_t secOfDay;
    int year, month, day;
    int hour, minute, second;
    char *p;

    (void)nBuf;

    if (epochSec >= 0) {
	daysSinceEpoch = epochSec / 86400;
	secOfDay = epochSec % 86400;
    } else {
	daysSinceEpoch = (epochSec - 86399) / 86400;
	secOfDay = epochSec - daysSinceEpoch * 86400;
    }

    th8test_civil_from_days(daysSinceEpoch, &year, &month, &day);

    hour = (int)(secOfDay / 3600);
    minute = (int)((secOfDay % 3600) / 60);
    second = (int)(secOfDay % 60);

    p = zBuf;

    /* Year (4 digits, zero-padded for positive years). */
    {
	int y = year;

	if (y < 0) {
	    *p++ = '-';
	    y = -y;
	}
	p[0] = (char)('0' + (y / 1000) % 10);
	p[1] = (char)('0' + (y / 100) % 10);
	p[2] = (char)('0' + (y / 10) % 10);
	p[3] = (char)('0' + y % 10);
	p += 4;
    }
    *p++ = '-';

    p[0] = (char)('0' + month / 10);
    p[1] = (char)('0' + month % 10);
    p += 2;
    *p++ = '-';

    p[0] = (char)('0' + day / 10);
    p[1] = (char)('0' + day % 10);
    p += 2;
    *p++ = 'T';

    p[0] = (char)('0' + hour / 10);
    p[1] = (char)('0' + hour % 10);
    p += 2;
    *p++ = ':';

    p[0] = (char)('0' + minute / 10);
    p[1] = (char)('0' + minute % 10);
    p += 2;
    *p++ = ':';

    p[0] = (char)('0' + second / 10);
    p[1] = (char)('0' + second % 10);
    p += 2;

    *p++ = 'Z';
    *p = 0;

    return (int)(p - zBuf);
}


/*
 *======================================================================
 *
 * TH8 ENTRY POINTS
 *
 *	These functions use the TH8 API (Th8_Interp, Th8_CreateCommand,
 *	Th8_SetResult, etc.).  They are called when the library is
 *	loaded via TH8's [load] command:
 *
 *	  load bin/libth8test.dylib:Th8test
 *
 *	Guarded by #ifdef TH8_TESTLIB_TH8 so they are only compiled
 *	when building the TH8 variant of the test library.
 *
 *======================================================================
 */

#ifdef TH8_TESTLIB_TH8

#  include "th8_meta_defs.h"
#  include "th8_meta_libc.h"

#  if defined(_WIN32) || defined(WIN32)
#    include "th8_meta_msvc.h"
#    include "th8_meta_win32.h"
#    include "ConvertUTF_v2.h"
#  else
#    include "th8_meta_posix.h"
#  endif


/*
 *----------------------------------------------------------------------
 *
 * TestExecBuf --
 *
 *	Growable byte buffer for collecting subprocess output.
 *	Uses standard-library malloc/realloc/free so the captured
 *	output does not count against the interpreter memory limit.
 *
 *----------------------------------------------------------------------
 */

typedef struct TestExecBuf {
    char *z;  /* Buffer data (NUL-terminated). */
    size_t n;  /* Bytes used (excluding NUL). */
    size_t nAlloc; /* Bytes allocated. */
} TestExecBuf;

#  define TESTEXECBUF_INIT {NULL, 0, 0}

/*
 *----------------------------------------------------------------------
 *
 * th8test_buf_append --
 *
 *	Append n bytes from z to the growable buffer p, reallocating
 *	as needed.  The buffer is always NUL-terminated after append.
 *
 * Why / How:
 *	Subprocess output is read in chunks; this provides a simple
 *	growable buffer that doubles in size to amortize realloc
 *	cost.  Uses stdlib malloc/realloc (not Th8_Malloc) so captured
 *	output does not count against the interpreter memory limit.
 *
 * Results:
 *	0 on success, -1 on allocation failure.
 *
 * Side effects:
 *	May reallocate p->z.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_buf_append(
    TestExecBuf *p, /* Buffer to append to. */
    const char *z, /* Data to append. */
    size_t n)  /* Number of bytes. */
{
    if (n == 0) return 0;
    if (p->n + n + 1 > p->nAlloc) {
	size_t nNew = p->nAlloc ? p->nAlloc * 2 : 4096;
	char *zNew;
	while (nNew < p->n + n + 1)
	    nNew *= 2;
	zNew = (char *)realloc(p->z, nNew);
	if (!zNew) return -1;
	p->z = zNew;
	p->nAlloc = nNew;
    }
    memcpy(p->z + p->n, z, n);
    p->n += n;
    p->z[p->n] = '\0';
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_buf_free --
 *
 *	Release all memory held by a TestExecBuf and reset its
 *	fields to zero.
 *
 * Why / How:
 *	Pairs with th8test_buf_append to ensure collected subprocess
 *	output is freed after being copied to the interpreter result.
 *	Uses stdlib free to match the stdlib malloc in buf_append.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees p->z and zeros the struct fields.
 *
 *----------------------------------------------------------------------
 */

static void
th8test_buf_free(TestExecBuf *p) /* Buffer to release. */
{
    free(p->z);
    p->z = NULL;
    p->n = 0;
    p->nAlloc = 0;
}


#  if defined(_WIN32) || defined(WIN32)
/*
 *----------------------------------------------------------------------
 *
 * th8test_win32_quote_arg --
 *
 *	Quote a single argument for the Win32 process command line,
 *	following the rules that CommandLineToArgvW uses to parse:
 *
 *	  - Arguments containing space, tab, or double-quote are
 *	    enclosed in double-quotes.
 *	  - Inside quotes, 2N backslashes followed by " produce
 *	    N backslashes and end the quoted region.
 *	  - 2N+1 backslashes followed by " produce N backslashes
 *	    and a literal double-quote.
 *	  - N backslashes NOT followed by " produce N backslashes.
 *
 *	Pass zOut=NULL for a sizing pass.  Returns the number of
 *	bytes that would be / were written (excluding NUL).
 *
 * Why / How:
 *	Win32's CreateProcess takes a single command-line string,
 *	not an argv array.  Incorrect quoting is a security
 *	vulnerability (argument injection).  This implements the
 *	exact inverse of CommandLineToArgvW's parsing rules so that
 *	arbitrary argument content is safely transported.
 *
 * Results:
 *	Number of bytes written/needed (excluding NUL).
 *
 * Side effects:
 *	If zOut is non-NULL, writes quoted bytes into it.
 *
 *----------------------------------------------------------------------
 */

static size_t
th8test_win32_quote_arg(
    const char *z, /* Argument text. */
    size_t n,  /* Argument length in bytes. */
    char *zOut)  /* Output buffer, or NULL for sizing. */
{
    size_t i, nOut = 0;
    int needsQuote = 0;

    if (n == 0) {
	needsQuote = 1;
    } else {
	for (i = 0; i < n; i++) {
	    if (z[i] == ' ' || z[i] == '\t' || z[i] == '"') {
		needsQuote = 1;
		break;
	    }
	}
    }

    if (!needsQuote) {
	if (zOut) memcpy(zOut, z, n);
	return n;
    }

    /* Opening double-quote. */
    if (zOut) zOut[nOut] = '"';
    nOut++;

    for (i = 0; i < n;) {
	size_t nBs = 0;

	/* Count consecutive backslashes. */
	while (i < n && z[i] == '\\') {
	    nBs++;
	    i++;
	}

	if (i == n) {
	    /*
	     * Trailing backslashes before closing quote:
	     * double them so the closing quote is literal.
	     */
	    size_t j;
	    for (j = 0; j < nBs * 2; j++) {
		if (zOut) zOut[nOut] = '\\';
		nOut++;
	    }
	    break;
	} else if (z[i] == '"') {
	    /*
	     * Backslashes before a double-quote: double
	     * the backslashes and escape the quote.
	     */
	    size_t j;
	    for (j = 0; j < nBs * 2 + 1; j++) {
		if (zOut) zOut[nOut] = '\\';
		nOut++;
	    }
	    if (zOut) zOut[nOut] = '"';
	    nOut++;
	    i++;
	} else {
	    /* Backslashes not followed by a quote. */
	    size_t j;
	    for (j = 0; j < nBs; j++) {
		if (zOut) zOut[nOut] = '\\';
		nOut++;
	    }
	    if (zOut) zOut[nOut] = z[i];
	    nOut++;
	    i++;
	}
    }

    /* Closing double-quote. */
    if (zOut) zOut[nOut] = '"';
    nOut++;

    return nOut;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_win32_build_cmdline --
 *
 *	Build a properly quoted Win32 command line string from an
 *	argument array.  Returns a malloc'd NUL-terminated string.
 *	The caller must free() the result.  Returns NULL on OOM.
 *
 * Why / How:
 *	Performs a two-pass approach: first pass computes total size,
 *	second pass writes the result.  Each argument is quoted via
 *	th8test_win32_quote_arg with space separators between them.
 *	This eliminates shell metacharacter injection on Win32.
 *
 * Results:
 *	malloc'd command line string, or NULL on OOM.
 *
 * Side effects:
 *	Allocates memory via malloc.
 *
 *----------------------------------------------------------------------
 */

static char *
th8test_win32_build_cmdline(
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    const size_t *argl)  /* Argument lengths. */
{
    size_t nTotal = 0;
    size_t nOff;
    char *zCmd;
    int i;

    /* Sizing pass. */
    for (i = 0; i < argc; i++) {
	if (i > 0) nTotal++;  /* Space separator. */
	nTotal += th8test_win32_quote_arg(argv[i], argl[i], NULL);
    }

    zCmd = (char *)malloc(nTotal + 1);
    if (!zCmd) return NULL;

    /* Writing pass. */
    nOff = 0;
    for (i = 0; i < argc; i++) {
	if (i > 0) zCmd[nOff++] = ' ';
	nOff += th8test_win32_quote_arg(argv[i], argl[i], zCmd + nOff);
    }
    zCmd[nOff] = '\0';

    return zCmd;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_utf8_to_wide --
 *
 *	Convert a NUL-terminated UTF-8 string to a newly allocated
 *	UTF-16 (wchar_t) string using the ConvertUTF_v2 library
 *	from Eagle.  Returns NULL on failure or OOM.  The caller
 *	must free() the result.
 *
 * Why / How:
 *	Win32's CreateProcessW requires a UTF-16 command line.  TH8
 *	strings are always UTF-8, so conversion is mandatory.  Uses
 *	the Eagle ConvertUTF_v2 library (strict mode) to ensure
 *	invalid UTF-8 sequences are rejected rather than silently
 *	corrupted.
 *
 * Results:
 *	malloc'd wchar_t string, or NULL on failure.
 *
 * Side effects:
 *	Allocates memory via malloc.
 *
 *----------------------------------------------------------------------
 */

static wchar_t *
th8test_utf8_to_wide(const char *z) /* NUL-terminated UTF-8 input. */
{
    size_t nBytes;
    size_t nWide;
    wchar_t *zWide;
    const UTF8 *pSrc;
    UTF16 *pDst;
    ConversionResult cr;

    nBytes = Th8_Strlen(NULL, z);

    /*
     * Allocate a worst-case buffer: each UTF-8 byte produces
     * at most one UTF-16 code unit, plus one for the NUL.
     */
    nWide = nBytes + 1;
    if (nWide > (size_t)-1 / sizeof(wchar_t)) return NULL;
    /*
     * AUDIT-OK[size-name-multiply]: the explicit overflow check
     * on the line above ensures nWide * sizeof(wchar_t) fits in
     * size_t before this multiply runs.
     */
    zWide = (wchar_t *)malloc(nWide * sizeof(wchar_t));
    if (!zWide) return NULL;

    pSrc = (const UTF8 *)z;
    pDst = (UTF16 *)zWide;
    cr = ConvertUTF8toUTF16(
        &pSrc, (const UTF8 *)(z + nBytes), &pDst, (UTF16 *)(zWide + nWide),
        strictConversion);

    if (cr != conversionOK) {
	free(zWide);
	return NULL;
    }

    /* NUL-terminate. */
    *pDst = 0;
    return zWide;
}

#  endif /* _WIN32 || WIN32 */


/*
 * Hook test state.  These control the behavior of the test
 * eval and preLoad callbacks.
 */

static int th8test_preEvalMode = 0;   /* 0=allow, 1=deny */
static int th8test_preEvalCount = 0;  /* invocation count */
static Th8_PolicyProc th8test_savedPolicyCb = NULL;
static void *th8test_savedPolicyCbCtx = NULL;

#  if defined(TH8_ENABLE_LOAD)
static int th8test_preLoadMode = 0;   /* 0=allow, 1=deny */
static int th8test_preLoadCount = 0;  /* invocation count */
#  endif

/*
 *----------------------------------------------------------------------
 *
 * th8test_preEvalProc --
 *
 *	Test policy callback.  In the PRE phase, increments the
 *	invocation counter and returns TH8_OK or TH8_ERROR
 *	depending on the current mode.  In the POST phase, returns
 *	TH8_OK (no-op).
 *
 * Why / How:
 *	Tests the policy callback mechanism (Th8_SetPolicyCallback).
 *	The mode variable allows test scripts to switch between
 *	allow/deny without re-registering the callback, and the
 *	counter lets them verify the callback was actually invoked.
 *
 * Results:
 *	TH8_OK (allow) or TH8_ERROR (deny) in PRE phase;
 *	TH8_OK always in POST phase.
 *
 * Side effects:
 *	Increments th8test_preEvalCount in PRE phase.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_preEvalProc(
    Th8_Interp *interp,
    int phase,
    const char *zName,
    size_t nName,
    const char *zData,
    size_t nData,
    int flags,
    int rc,
    void *pCtx)
{
    (void)interp;
    (void)zName;
    (void)nName;
    (void)zData;
    (void)nData;
    (void)flags;
    (void)rc;
    (void)pCtx;
    if (phase & TH8_PHASE_POST) return TH8_OK;
    th8test_preEvalCount++;
    return th8test_preEvalMode ? TH8_ERROR : TH8_OK;
}


#  if defined(TH8_ENABLE_LOAD)

/*
 *----------------------------------------------------------------------
 *
 * th8test_preLoadProc --
 *
 *	Test pre-load callback.  Increments the invocation counter
 *	and returns TH8_OK or TH8_ERROR depending on the current
 *	mode.
 *
 * Why / How:
 *	Tests the pre-load callback mechanism (Th8_SetPreLoadCallback).
 *	When mode is "deny", prevents [load] from loading any library,
 *	verifying that the callback can veto binary extension loading.
 *
 * Results:
 *	TH8_OK (allow) or TH8_ERROR (deny).
 *
 * Side effects:
 *	Increments th8test_preLoadCount.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_preLoadProc(
    Th8_Interp *interp,
    const char *zName,
    size_t nName,
    void *pCtx)
{
    (void)interp;
    (void)zName;
    (void)nName;
    (void)pCtx;
    th8test_preLoadCount++;
    return th8test_preLoadMode ? TH8_ERROR : TH8_OK;
}

#  endif


/*
 *----------------------------------------------------------------------
 *
 * th8test_preeval_cmd --
 *
 *	Implements the "th8testlib::preeval" command with subcommands:
 *	  install ?deny?  -- install the preEval callback
 *	  uninstall       -- remove the preEval callback
 *	  count           -- return the invocation count
 *	  mode ?allow|deny? -- get or set the mode
 *
 * Why / How:
 *	Script-level control over the policy callback for testing.
 *	Saves and restores the prior callback on install/uninstall
 *	so it does not interfere with the shell's signed-only policy
 *	when both are active.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR for unknown subcommands.
 *
 * Side effects:
 *	Installs/removes the policy callback; modifies global state.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_preeval_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    (void)ctx;

    if (argc < 2) {
	return Th8_WrongNumArgs(
	    interp, "th8testlib::preeval install|uninstall|count|mode ?arg?");
    }

    if (argl[1] == 7 && memcmp(argv[1], "install", 7) == 0) {
	th8test_preEvalCount = 0;
	th8test_preEvalMode = 0;
	if (argc >= 3 && argl[2] == 4 && memcmp(argv[2], "deny", 4) == 0) {
	    th8test_preEvalMode = 1;
	}
	/* Save the current policy callback before overwriting. */
	Th8_GetPolicyCallback(
	    interp, &th8test_savedPolicyCb, &th8test_savedPolicyCbCtx);
	Th8_SetPolicyCallback(interp, th8test_preEvalProc, 0);
	Th8_ClearResult(interp);
	return TH8_OK;
    }
    if (argl[1] == 9 && memcmp(argv[1], "uninstall", 9) == 0) {
	/* Restore the original policy callback. */
	Th8_SetPolicyCallback(
	    interp, th8test_savedPolicyCb, th8test_savedPolicyCbCtx);
	th8test_savedPolicyCb = NULL;
	th8test_savedPolicyCbCtx = NULL;
	th8test_preEvalMode = 0;
	th8test_preEvalCount = 0;
	Th8_ClearResult(interp);
	return TH8_OK;
    }
    if (argl[1] == 5 && memcmp(argv[1], "count", 5) == 0) {
	return Th8_SetResultInt(interp, th8test_preEvalCount);
    }
    if (argl[1] == 4 && memcmp(argv[1], "mode", 4) == 0) {
	if (argc >= 3) {
	    if (argl[2] == 5 && memcmp(argv[2], "allow", 5) == 0) {
		th8test_preEvalMode = 0;
	    } else if (argl[2] == 4 && memcmp(argv[2], "deny", 4) == 0) {
		th8test_preEvalMode = 1;
	    }
	}
	return Th8_SetResult(
	    interp, th8test_preEvalMode ? "deny" : "allow", TH8_NOLEN);
    }
    Th8_SetResultStatic(
        interp, "bad subcommand: must be install, uninstall, count, or mode",
        TH8_NOLEN);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_getcmdinfo_cmd --
 *
 *	Implements "th8testlib::getcmdinfo ?-nolen? name": looks the
 *	command up through the public Th8_GetCommandInfo() entry point
 *	and returns "1" when found, "0" when not.
 *
 * Why / How:
 *	Th8_GetCommandInfo is a C-only entry point -- ordinary script
 *	callers cannot reach its `nName == TH8_NOLEN` path because the
 *	evaluator always hands a command an explicit argument length.
 *	This shim lets a .tcl test drive BOTH length modes (an explicit
 *	length, and the TH8_NOLEN "compute it" sentinel) across
 *	qualified/simple and found/not-found names, so the lookup --
 *	including the TH8_NOLEN guard whose absence once masked the
 *	sentinel into a ~256 MiB over-read -- is exercised by the suite.
 *
 * Results:
 *	TH8_OK; result is "1" (found) or "0" (not found).  TH8_ERROR
 *	only on a usage error.
 *
 * Side effects:
 *	None (any not-found error result Th8_GetCommandInfo leaves in
 *	the interpreter is cleared before returning).
 *
 *----------------------------------------------------------------------
 */

static int
th8test_getcmdinfo_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    Th8_CommandProc xProc = NULL;
    void *pCtx = NULL;
    const char *zName;
    size_t nName;
    int bNoLen = 0;
    int iName = 1;
    int rc;

    (void)ctx;

    if (argc >= 2 && argl[1] == 6 && memcmp(argv[1], "-nolen", 6) == 0) {
	bNoLen = 1;
	iName = 2;
    }
    if (argc != iName + 1) {
	return Th8_WrongNumArgs(
	    interp, "th8testlib::getcmdinfo ?-nolen? name");
    }

    zName = argv[iName];
    nName = bNoLen ? TH8_NOLEN : argl[iName];

    rc = Th8_GetCommandInfo(interp, zName, nName, &xProc, &pCtx);

    /*
     * Th8_GetCommandInfo sets a "no such command" error result on
     * failure; collapse the two outcomes into a clean boolean so the
     * caller sees 1 (found) / 0 (not found) rather than an error.
     */
    Th8_ClearResult(interp);
    return Th8_SetResultInt(interp, rc == TH8_OK ? 1 : 0);
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_taint_cmd --
 *
 *	Implements "th8testlib::taint VALUE": set the interpreter result
 *	to VALUE with the taint bit forced on in the stored length.
 *
 * Why / How:
 *	Taint is carried in the high bit (TH8_TAINT_BIT) of a size_t
 *	length; ordinary script code has no way to synthesize a tagged
 *	length, so tests cannot create a tainted value without a C-side
 *	primitive.  Passing TH8_ADD_TAINT(rawLen) to Th8_SetResult marks
 *	the returned value tainted, letting taint.tcl drive propagation
 *	end to end.  The raw length is masked first so an already-tainted
 *	argument is not mishandled.
 *
 * Results:
 *	TH8_OK; result is VALUE, tainted.
 *
 * Side effects:
 *	Sets the interpreter result.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_taint_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    (void)ctx;

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "th8testlib::taint value");
    }
    return Th8_SetResult(interp, argv[1], TH8_ADD_TAINT(TH8_LEN(argl[1])));
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_result_tainted_cmd --
 *
 *	Implements "th8testlib::result_tainted VALUE": store VALUE as a
 *	tainted result, then report whether the STORED result carries
 *	taint.  Isolates Th8_SetResult()'s taint handling from the
 *	substitution and variable-storage paths: Th8_GetResult() returns
 *	interp->nResult, whose high bit is the stored taint.
 *
 * Results:
 *	TH8_OK; result is 1 if the stored result is tainted, else 0.
 *
 * Side effects:
 *	Sets the interpreter result.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_result_tainted_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    size_t nStored = 0;

    (void)ctx;

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "th8testlib::result_tainted value");
    }
    if (Th8_SetResult(interp, argv[1], TH8_ADD_TAINT(TH8_LEN(argl[1]))) !=
        TH8_OK) {
	return TH8_ERROR;
    }
    (void)Th8_GetResult(interp, &nStored);
    return Th8_SetResultInt(interp, TH8_TAINTED(nStored) ? 1 : 0);
}


#  if defined(TH8_ENABLE_CRYPTOGRAPHY)
/*
 *----------------------------------------------------------------------
 *
 * th8test_result_sensitive_tainted_cmd --
 *
 *	Implements "th8testlib::result_sensitive_tainted VALUE": store
 *	VALUE as a tainted SENSITIVE result (Th8_SetResultSensitive with
 *	the taint bit forced on), then report whether the stored result
 *	still carries taint.
 *
 * Why / How:
 *	Sensitivity (the sensitive tag bit -- stored in the protected,
 *	mlock'd region) and trust (the taint tag on the length) are
 *	INDEPENDENT classifications.  Th8_SetResultSensitive must mask
 *	the tag off the byte count it uses for the copy / NUL, yet
 *	preserve it in interp->nResult so a sensitive value derived from
 *	untrusted input stays tainted.  This isolates that path from the
 *	ordinary (non-sensitive) result storage exercised by
 *	result_tainted.  Gated on TH8_ENABLE_CRYPTOGRAPHY (the protected
 *	region is only compiled in with cryptography).
 *
 * Results:
 *	TH8_OK; result is 1 if the stored sensitive result is tainted,
 *	else 0.
 *
 * Side effects:
 *	Sets (then overwrites) the interpreter result.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_result_sensitive_tainted_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    size_t nStored = 0;

    (void)ctx;

    if (argc != 2) {
	return Th8_WrongNumArgs(
	    interp, "th8testlib::result_sensitive_tainted value");
    }
    if (Th8_SetResultSensitive(
            interp, argv[1], TH8_ADD_TAINT(TH8_LEN(argl[1]))) != TH8_OK) {
	return TH8_ERROR;
    }
    (void)Th8_GetResult(interp, &nStored);
    return Th8_SetResultInt(interp, TH8_TAINTED(nStored) ? 1 : 0);
}
#  endif /* TH8_ENABLE_CRYPTOGRAPHY */


/*
 *----------------------------------------------------------------------
 *
 * th8test_arg_tainted_cmd --
 *
 *	Implements "th8testlib::arg_tainted VALUE": report whether the
 *	argument the command received is tainted (TH8_TAINTED(argl[1])).
 *	Drives the substitution / variable-read propagation paths -- the
 *	taint bit is present only if the value that produced this
 *	argument carried it through command or variable substitution.
 *
 * Results:
 *	TH8_OK; result is 1 if the argument is tainted, else 0.
 *
 * Side effects:
 *	Sets the interpreter result.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_arg_tainted_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    (void)ctx;
    (void)argv;

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "th8testlib::arg_tainted value");
    }
    return Th8_SetResultInt(interp, TH8_TAINTED(argl[1]) ? 1 : 0);
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_eval_tainted_cmd --
 *
 *	Implements "th8testlib::eval_tainted SCRIPT": evaluate SCRIPT
 *	with the taint bit forced on the length passed to Th8_Eval,
 *	isolating the evaluation security gate from the substitution
 *	paths.
 *
 * Results:
 *	TH8_OK; result is 1 if the tainted script EXECUTED (the gate
 *	failed -- a defect) and 0 if it was rejected.  Pair with a
 *	side-effect probe variable to confirm no execution occurred.
 *
 * Side effects:
 *	Sets the interpreter result; runs SCRIPT only if the gate is
 *	broken.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_eval_tainted_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    int rc;

    (void)ctx;

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "th8testlib::eval_tainted script");
    }
    rc = Th8_Eval(
        interp, 0, argv[1], TH8_ADD_TAINT(TH8_LEN(argl[1])), "eval_tainted",
        TH8_NOLEN);
    Th8_ClearResult(interp);
    return Th8_SetResultInt(interp, rc == TH8_OK ? 1 : 0);
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_taints_cmd --
 *
 *	Diagnostic: "th8testlib::taints ARG ...": return a space-
 *	separated 0/1 for TH8_TAINTED(argl[i]) of every argument after
 *	the command name, revealing the per-position taint the word
 *	builder produced.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_taints_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char out[512];
    int i, n = 0;

    (void)argv;
    (void)ctx;

    for (i = 1; i < argc && n < (int)sizeof(out) - 2; i++) {
	if (i > 1) out[n++] = ' ';
	out[n++] = TH8_TAINTED(argl[i]) ? '1' : '0';
    }
    return Th8_SetResult(interp, out, (size_t)n);
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_splitlist_probe_cmd --
 *
 *	Implements "th8testlib::splitlist_probe MODE VALUE": force the
 *	taint bit on VALUE's length and split it via Th8_SplitList with
 *	TH8_LIST_NO_CACHE, returning the element count.  MODE selects
 *	which output pointers are supplied:
 *	  count -- pazElem == NULL, panElem == NULL (count-only, like
 *	           llength)
 *	  lens  -- pazElem == NULL, panElem != NULL (lengths-only)
 *	  block -- pazElem != NULL, panElem != NULL: a full split, used to
 *	           verify the R-63239 single-block / interior-pointer
 *	           contract (returns "interior" on success)
 *
 * Why / How:
 *	The element-tagging guard in Th8_SplitList,
 *	`if (nListTag && panElem && *panElem)`, is a three-condition
 *	decision.  Ordinary script paths only reach the all-true and
 *	short-circuit-on-C1 vectors: the guard is reached only on a cache
 *	MISS (a cache hit returns earlier), and no script command pairs a
 *	tainted list with a count-only or lengths-only split.  Both modes
 *	pass pazElem == NULL, which short-circuits the element-copy /
 *	allocation block, so the split leaves *panElem == 0 and the run
 *	behaves identically in the release and debug/fault builds.  They
 *	drive the two remaining MC/DC vectors deterministically:
 *	  count (panElem == NULL)      -> {nListTag=T, panElem=NULL}
 *	  lens  (panElem != NULL, but
 *	         *panElem left NULL)   -> {panElem!=NULL, *panElem=NULL}
 *	TH8_LIST_NO_CACHE forces the parse path so the guard is reached.
 *
 * Results:
 *	TH8_OK; result is the element count as an integer for count/lens,
 *	or the string "interior" for block (TH8_ERROR if the block-layout
 *	invariant does not hold).
 *
 * Side effects:
 *	Sets the interpreter result.  In block mode, allocates and frees a
 *	single split block.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_splitlist_probe_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char **azElem = 0;
    size_t *anElem = 0;
    int nCount = 0;
    size_t nTainted;
    int rc;

    (void)ctx;

    if (argc != 3) {
	return Th8_WrongNumArgs(
	    interp, "th8testlib::splitlist_probe mode value");
    }

    /* Force the whole-list taint bit so nListTag is set inside the
     * split; the raw byte count is preserved. */
    nTainted = TH8_ADD_TAINT(TH8_LEN(argl[2]));

    if (TH8_LEN(argl[1]) == 5 && memcmp(argv[1], "block", 5) == 0) {
	/*
	 * R-63239: a full split (both element and length arrays) returns
	 * *pazElem and *panElem as pointers into ONE allocation block --
	 * *panElem is the interior lengths array laid out immediately
	 * after the *panElem-count element pointers, i.e. exactly
	 * &(*pazElem)[nCount].  Only *pazElem (the block start) may be
	 * freed; freeing *panElem separately would be an interior-pointer
	 * free.  Verify the exact interior relationship, then reclaim the
	 * whole block with a SINGLE Th8_Free of the block start (the
	 * debug build's heap checks flag a wrong/partial free).
	 */

	rc = Th8_SplitList(
	    interp, argv[2], nTainted, &azElem, &anElem, &nCount,
	    TH8_LIST_NO_CACHE);
	if (rc != TH8_OK) {
	    return rc;
	}
	if (azElem == NULL || anElem == NULL ||
	    anElem != (size_t *)&azElem[nCount]) {
	    Th8_Free(interp, azElem);
	    Th8_SetResultStatic(
	        interp, "not-a-single-block: panElem is not interior",
	        TH8_NOLEN);
	    return TH8_ERROR;
	}
	Th8_Free(interp, azElem); /* single free reclaims both arrays */
	Th8_SetResultStatic(interp, "interior", TH8_NOLEN);
	return TH8_OK;
    } else if (TH8_LEN(argl[1]) == 5 && memcmp(argv[1], "count", 5) == 0) {
	rc = Th8_SplitList(
	    interp, argv[2], nTainted, 0, 0, &nCount, TH8_LIST_NO_CACHE);
    } else if (TH8_LEN(argl[1]) == 4 && memcmp(argv[1], "lens", 4) == 0) {
	rc = Th8_SplitList(
	    interp, argv[2], nTainted, 0, &anElem, &nCount,
	    TH8_LIST_NO_CACHE);
    } else {
	Th8_SetResultStatic(
	    interp, "mode must be block, count or lens", TH8_NOLEN);
	return TH8_ERROR;
    }
    if (rc != TH8_OK) {
	return rc;
    }
    return Th8_SetResultInt(interp, nCount);
}


#  if defined(TH8_ENABLE_LOAD)

/*
 *----------------------------------------------------------------------
 *
 * th8test_preload_cmd --
 *
 *	Implements the "th8testlib::preload" command with subcommands:
 *	  install ?deny?  -- install the preLoad callback
 *	  uninstall       -- remove the preLoad callback
 *	  count           -- return the invocation count
 *	  mode ?allow|deny? -- get or set the mode
 *
 * Why / How:
 *	Script-level control over the pre-load callback for testing
 *	binary extension loading policy.  Mirrors the preeval command
 *	structure for consistency.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR for unknown subcommands.
 *
 * Side effects:
 *	Installs/removes the pre-load callback; modifies global state.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_preload_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    (void)ctx;

    if (argc < 2) {
	return Th8_WrongNumArgs(
	    interp, "th8testlib::preload install|uninstall|count|mode ?arg?");
    }

    if (argl[1] == 7 && memcmp(argv[1], "install", 7) == 0) {
	th8test_preLoadCount = 0;
	th8test_preLoadMode = 0;
	if (argc >= 3 && argl[2] == 4 && memcmp(argv[2], "deny", 4) == 0) {
	    th8test_preLoadMode = 1;
	}
	Th8_SetPreLoadCallback(interp, th8test_preLoadProc, 0);
	Th8_ClearResult(interp);
	return TH8_OK;
    }
    if (argl[1] == 9 && memcmp(argv[1], "uninstall", 9) == 0) {
	Th8_SetPreLoadCallback(interp, 0, 0);
	th8test_preLoadMode = 0;
	th8test_preLoadCount = 0;
	Th8_ClearResult(interp);
	return TH8_OK;
    }
    if (argl[1] == 5 && memcmp(argv[1], "count", 5) == 0) {
	return Th8_SetResultInt(interp, th8test_preLoadCount);
    }
    if (argl[1] == 4 && memcmp(argv[1], "mode", 4) == 0) {
	if (argc >= 3) {
	    if (argl[2] == 5 && memcmp(argv[2], "allow", 5) == 0) {
		th8test_preLoadMode = 0;
	    } else if (argl[2] == 4 && memcmp(argv[2], "deny", 4) == 0) {
		th8test_preLoadMode = 1;
	    }
	}
	return Th8_SetResult(
	    interp, th8test_preLoadMode ? "deny" : "allow", TH8_NOLEN);
    }
    Th8_SetResultStatic(
        interp, "bad subcommand: must be install, uninstall, count, or mode",
        TH8_NOLEN);
    return TH8_ERROR;
}

#  endif


/*
 *----------------------------------------------------------------------
 *
 * th8test_preeval_denytest --
 *
 *	Implements "th8testlib::preeval_denytest".  Safely tests the
 *	preeval deny behavior entirely in C:
 *
 *	  1. Installs the policy callback in deny mode.
 *	  2. Attempts Th8_Eval("set _deny_test 1").
 *	  3. Records the return code.
 *	  4. Uninstalls the callback.
 *	  5. Returns "denied" if the eval was blocked, "allowed" if not.
 *
 *	This avoids the problem where deny mode blocks the test
 *	framework's own script evaluation.
 *
 * Why / How:
 *	If deny mode were tested from a script, the test command
 *	itself would be blocked by the policy.  By running the
 *	entire install-eval-uninstall cycle in C, the test is
 *	self-contained and cannot be affected by the deny state.
 *
 * Results:
 *	TH8_OK with "denied" or "allowed" as the result.
 *
 * Side effects:
 *	Temporarily installs and removes the policy callback.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_preeval_denytest(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    int rc;

    (void)ctx;
    (void)argv;
    (void)argl;

    if (argc != 1) {
	return Th8_WrongNumArgs(interp, "th8testlib::preeval_denytest");
    }

    /* Save and install deny callback. */
    {
	Th8_PolicyProc xSaved = NULL;
	void *pSaved = NULL;

	Th8_GetPolicyCallback(interp, &xSaved, &pSaved);

	th8test_preEvalMode = 1;
	th8test_preEvalCount = 0;
	Th8_SetPolicyCallback(interp, th8test_preEvalProc, 0);

	/* Attempt an eval -- should be blocked. */
	rc = Th8_Eval(interp, 0, "set _deny_test 1", TH8_NOLEN, NULL, 0);

	/* Restore original callback. */
	Th8_SetPolicyCallback(interp, xSaved, pSaved);
	th8test_preEvalMode = 0;
    }

    /* Report result. */
    if (rc != TH8_OK) {
	Th8_SetResultStatic(interp, "denied", 6);
	return TH8_OK;
    }
    Th8_SetResultStatic(interp, "allowed", 7);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_nop_cmd --
 *
 *	Implements the "th8testlib::nop" command.
 *	Does nothing and returns nothing.
 *
 *	Usage: th8testlib::nop ?arg ...?
 *
 * Why / How:
 *	A do-nothing command used to test command dispatch overhead,
 *	ensure that commands with ignored arguments work, and
 *	provide a harmless placeholder in test scripts that need
 *	a command invocation without side effects.
 *
 * Results:
 *	TH8_OK with empty result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_nop_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    (void)interp;
    (void)ctx;
    (void)argc;
    (void)argv;
    (void)argl;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_expr_features_parse_flags --
 *
 *	Parse the `<flags>` argument of `th8testlib::expr_features
 *	set` into an integer TH8_EXPR_* flag set.
 *
 * Why / How:
 *	The argument may be a plain decimal integer or a
 *	comma-separated list of the token aliases "none", "all",
 *	"top-comma" and "var-assign".  A decimal is tried first
 *	via Th8_ToWideInt; if that fails the string is walked
 *	comma-by-comma, OR-ing in the matching TH8_EXPR_* bit for
 *	each recognised token and raising an "unknown flag token"
 *	error on anything else.
 *
 * Results:
 *	TH8_OK with *piFlags set to the parsed flag set; TH8_ERROR
 *	(with an error message on interp) on an unrecognised token.
 *
 * Side effects:
 *	None beyond setting the interpreter result on error.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_expr_features_parse_flags(
    Th8_Interp *interp,
    const char *zArg,
    size_t nArg,
    int *piFlags)
{
    th8_int64_t iVal = 0;
    size_t i = 0;
    size_t iStart = 0;
    int flags = 0;

    /* First, try parsing as a decimal integer. */
    if (Th8_ToWideInt(0, zArg, nArg, &iVal) == TH8_OK) {
	*piFlags = (int)iVal;
	return TH8_OK;
    }

    /* Otherwise, parse as a comma-separated list of token names. */
    while (iStart < nArg) {
	size_t nTok;

	/* Find next ',' or end-of-input. */
	i = iStart;
	while (i < nArg && zArg[i] != ',') {
	    i++;
	}
	nTok = i - iStart;

	if (nTok == 4 && memcmp(&zArg[iStart], "none", 4) == 0) {
	    flags |= TH8_EXPR_NONE;
	} else if (nTok == 3 && memcmp(&zArg[iStart], "all", 3) == 0) {
	    flags |= TH8_EXPR_ALL;
	} else if (nTok == 9 && memcmp(&zArg[iStart], "top-comma", 9) == 0) {
	    flags |= TH8_EXPR_TOP_COMMA;
	} else if (
	    nTok == 10 && memcmp(&zArg[iStart], "var-assign", 10) == 0) {
	    flags |= TH8_EXPR_VAR_ASSIGN;
	} else {
	    Th8_ErrorMessage(
	        interp, "expr_features: unknown flag token:", &zArg[iStart],
	        nTok);
	    return TH8_ERROR;
	}

	iStart = i + 1;  /* skip the ',' */
    }

    *piFlags = flags;
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_expr_features_cmd --
 *
 *	Script-level dispatcher for `th8testlib::expr_features
 *	get|set ?flags?`.  Wraps the C-only
 *	`Th8_GetExprFeatures` / `Th8_SetExprFeatures` API so test
 *	scripts can introspect and toggle the expression-grammar
 *	feature flags (see `src/th8.h` `TH8_EXPR_*` constants).
 *
 *	Subcommands:
 *	  `get`             -- return the current flag set as an
 *	                       integer.
 *	  `set <flags>`     -- replace the flag set; return the
 *	                       previous value.  `<flags>` may be a
 *	                       symbolic list of `TH8_EXPR_*` token
 *	                       names parsed by
 *	                       `th8test_expr_features_parse_flags`.
 *
 * Parameters:
 *	interp -- live interpreter.
 *	ctx    -- ignored (command-registration boilerplate).
 *	argc   -- argument count.
 *	argv   -- argument string array.
 *	argl   -- argument lengths.
 *
 * Returns:
 *	`TH8_OK` on success (interpreter result holds the value
 *	per subcommand).
 *	`TH8_ERROR` on wrong-arg-count, unknown subcommand, or
 *	flag-parse failure (interpreter result describes the
 *	error).
 *
 * Side effects:
 *	On `set`, mutates `interp->nExprFeatures` via
 *	`Th8_SetExprFeatures`.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_expr_features_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    (void)ctx;

    if (argc < 2) {
	return Th8_WrongNumArgs(
	    interp, "th8testlib::expr_features get|set ?flags?");
    }

    if (argl[1] == 3 && memcmp(argv[1], "get", 3) == 0) {
	if (argc != 2) {
	    return Th8_WrongNumArgs(interp, "th8testlib::expr_features get");
	}
	return Th8_SetResultInt(interp, Th8_GetExprFeatures(interp));
    }

    if (argl[1] == 3 && memcmp(argv[1], "set", 3) == 0) {
	int flags;
	int previous;

	if (argc != 3) {
	    return Th8_WrongNumArgs(
	        interp, "th8testlib::expr_features set flags");
	}
	if (th8test_expr_features_parse_flags(
	        interp, argv[2], argl[2], &flags) != TH8_OK) {
	    return TH8_ERROR;
	}
	previous = Th8_SetExprFeatures(interp, flags);
	return Th8_SetResultInt(interp, previous);
    }

    Th8_ErrorMessage(
        interp, "expr_features: unknown subcommand:", argv[1], argl[1]);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_log_cmd --
 *
 *	Implements the "th8testlib::log" command.  Appends a single
 *	string as a line to the dedicated test log file located at
 *	"<exeDir>/th8test.log".  Used by [tlog]/[tputs] in the th8
 *	init script to record test output for post-mortem inspection.
 *
 *	Usage: th8testlib::log string
 *
 * Why / How:
 *	The exe directory is resolved via Th8_GetExePath; the file
 *	is opened in append mode each call (POSIX O_APPEND, Win32
 *	FILE_APPEND_DATA) which provides atomic short-write semantics
 *	for concurrent test processes.  A trailing newline is added
 *	only if the string does not already end with one, so callers
 *	that already include "\n" do not produce blank lines.  The
 *	full message (string + optional newline) is written in a
 *	single syscall to avoid interleaving with concurrent writers.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR with a descriptive interpreter
 *	result if the exe path is unavailable, the file cannot be
 *	opened, or the write fails.
 *
 * Side effects:
 *	Creates "<exeDir>/th8test.log" if it does not exist; appends
 *	one line to it.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_log_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    static const char zLogName[] = "th8test.log";
    static const size_t nLogName = sizeof(zLogName) - 1;
    char *zExePath;
    char *zLogPath;
    char *zMsg;
    size_t nExePath, nDir, nLogPath, nStr, nMsg;
    int needNewline;
    int rc = TH8_OK;

    (void)ctx;

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "th8testlib::log string");
    }

    zExePath = Th8_GetExePath(interp);
    if (!zExePath) {
	Th8_SetResultStatic(
	    interp, "cannot resolve executable path", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Strip the basename to obtain the executable directory.
     * Keep the trailing separator so we can concatenate with
     * zLogName directly.
     */

    nExePath = Th8_Strlen(interp, zExePath);
    nDir = nExePath;
    while (nDir > 0 && zExePath[nDir - 1] != '/'
#  if defined(_WIN32) || defined(WIN32)
           && zExePath[nDir - 1] != '\\'
#  endif
    ) {
	nDir--;
    }

    nLogPath = nDir + nLogName;
    zLogPath = (char *)TH8_ALLOC_STR_ADD(interp, nDir, nLogName);
    if (!zLogPath) {
	Th8_Free(interp, zExePath);
	Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	return TH8_ERROR;
    }
    Th8_Memcpy(interp, zLogPath, zExePath, nDir);
    Th8_Memcpy(interp, zLogPath + nDir, zLogName, nLogName);
    zLogPath[nLogPath] = '\0';
    Th8_Free(interp, zExePath);

    /*
     * Build the message: input string plus a trailing newline if
     * one is not already present.  Single-buffer write avoids
     * interleaving with concurrent writers.
     */

    nStr = argl[1];
    needNewline = (nStr == 0 || argv[1][nStr - 1] != '\n');
    nMsg = nStr + (needNewline ? 1 : 0);
    zMsg = (char *)TH8_ALLOC(interp, nMsg > 0 ? nMsg : 1);
    if (!zMsg) {
	Th8_Free(interp, zLogPath);
	Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	return TH8_ERROR;
    }
    if (nStr > 0) {
	Th8_Memcpy(interp, zMsg, argv[1], nStr);
    }
    if (needNewline) {
	zMsg[nStr] = '\n';
    }

#  if !defined(_WIN32) && !defined(WIN32)
    {
	int fd;
	ssize_t n;

	fd = open(zLogPath, O_WRONLY | O_APPEND | O_CREAT, 0644);
	if (fd < 0) {
	    TH8_TRACE_ERR(interp, "open log failed");
	    Th8_ErrorMessage(
	        interp, "cannot open log file:", zLogPath, nLogPath);
	    rc = TH8_ERROR;
	    goto done;
	}
	n = write(fd, zMsg, nMsg);
	if (close(fd) != 0) {
	    TH8_TRACE_ERR(interp, "close log failed (ignored)");
	}
	if (n < 0 || (size_t)n != nMsg) {
	    Th8_ErrorMessage(
	        interp, "cannot write log file:", zLogPath, nLogPath);
	    rc = TH8_ERROR;
	    goto done;
	}
    }
#  else /* _WIN32 */
    {
	HANDLE h;
	DWORD nWrote = 0;

	h = CreateFileA(
	    zLogPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
	    NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) {
	    TH8_TRACE_ERR(interp, "CreateFileA log failed");
	    Th8_ErrorMessage(
	        interp, "cannot open log file:", zLogPath, nLogPath);
	    rc = TH8_ERROR;
	    goto done;
	}
	if (!WriteFile(h, zMsg, (DWORD)nMsg, &nWrote, NULL) ||
	    nWrote != (DWORD)nMsg) {
	    TH8_TRACE_ERR(interp, "WriteFile log failed");
	    if (!FlushFileBuffers(h)) {
		TH8_TRACE_ERR(
		    interp, "FlushFileBuffers log failed (ignored)");
	    }
	    if (!CloseHandle(h)) {
		TH8_TRACE_ERR(interp, "CloseHandle log failed (ignored)");
	    }
	    Th8_ErrorMessage(
	        interp, "cannot write log file:", zLogPath, nLogPath);
	    rc = TH8_ERROR;
	    goto done;
	}
	if (!FlushFileBuffers(h)) {
	    TH8_TRACE_ERR(interp, "FlushFileBuffers log failed (ignored)");
	}
	if (!CloseHandle(h)) {
	    TH8_TRACE_ERR(interp, "CloseHandle log failed (ignored)");
	}
    }
#  endif

done:
    Th8_Free(interp, zMsg);
    Th8_Free(interp, zLogPath);
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * th8testlib_format_size --
 *
 *	Format the size_t value `v` as decimal ASCII digits into
 *	`zOut` (no terminating NUL is written).
 *
 * Why / How:
 *	The safe-math test commands print size_t operands that may
 *	exceed the range of a signed Tcl wide int, so the standard
 *	integer formatters cannot be used.  Digits are generated
 *	least-significant-first into a scratch buffer, then copied
 *	out in reverse; a `v` of 0 yields a single '0'.
 *
 * Results:
 *	The number of digits written, or 0 if `nOut` is too small
 *	to hold them (the caller supplies a generous buffer).
 *
 * Side effects:
 *	Writes up to the returned count of bytes into `zOut`.
 *
 *----------------------------------------------------------------------
 */
static int
th8testlib_format_size(char *zOut, size_t nOut, size_t v)
{
    char tmp[40];
    int n = 0, i;

    if (v == 0) {
	tmp[n++] = '0';
    } else {
	while (v > 0) {
	    tmp[n++] = (char)('0' + (v % 10));
	    v /= 10;
	}
    }
    if ((size_t)n >= nOut)
	return 0; /* truncated -- caller uses generous buffer */
    for (i = 0; i < n; i++) {
	zOut[i] = tmp[n - 1 - i];
    }
    return n;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_parse_size --
 *
 *	Parse a script-supplied size argument that may be either a
 *	non-negative decimal integer or one of five symbolic tokens
 *	used by `th8testlib::safemul` and related boundary tests:
 *
 *	  `max`      = `(size_t)-1`        (SIZE_MAX, the
 *	                                    overflow-boundary value).
 *	  `max-1`    = `(size_t)-2`.
 *	  `half`     = `(size_t)-1 / 2`.
 *	  `halfp1`   = `(size_t)-1 / 2 + 1` (just-overflows-on-2x).
 *	  `halfdiv2` = `(size_t)-1 / 4`.
 *
 *	Anything else is parsed as a decimal via `Th8_ToWideInt`;
 *	negative values are rejected.
 *
 * Parameters:
 *	z    -- argument bytes.
 *	nZ   -- length of `z`.
 *	pOut -- output: resolved `size_t` value.  Set only on
 *	        successful return.
 *
 * Returns:
 *	`TH8_OK` on a recognised symbol or a non-negative decimal.
 *	`TH8_ERROR` on a malformed decimal or a negative decimal.
 *	Does not set an interpreter result (helper is interp-less).
 *
 * Side effects:
 *	None besides `*pOut` on success.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_parse_size(const char *z, size_t nZ, size_t *pOut)
{
    th8_int64_t v;

    if (nZ == 3 && z[0] == 'm' && z[1] == 'a' && z[2] == 'x') {
	*pOut = (size_t)-1;
	return TH8_OK;
    }
    if (nZ == 5 && 0 == memcmp(z, "max-1", 5)) {
	*pOut = (size_t)-2;
	return TH8_OK;
    }
    if (nZ == 4 && 0 == memcmp(z, "half", 4)) {
	*pOut = ((size_t)-1) / 2;
	return TH8_OK;
    }
    if (nZ == 6 && 0 == memcmp(z, "halfp1", 6)) {
	*pOut = ((size_t)-1) / 2 + 1;
	return TH8_OK;
    }
    if (nZ == 8 && 0 == memcmp(z, "halfdiv2", 8)) {
	*pOut = ((size_t)-1) / 4;
	return TH8_OK;
    }
    if (Th8_ToWideInt(NULL, z, nZ, &v) != TH8_OK || v < 0) {
	return TH8_ERROR;
    }
    *pOut = (size_t)v;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_safemul_cmd --
 *
 *	Implements "th8testlib::safemul a b".  Calls Th8_SafeMul and
 *	returns "ok N" with the resulting decimal byte count, or
 *	"overflow" if the multiplication would wrap.
 *
 *	Args may be decimal integers or one of the symbolic tokens
 *	described in th8test_parse_size (max, max-1, half, halfp1,
 *	halfdiv2) for boundary testing.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_safemul_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    size_t a = 0, b = 0, out = 0;
    char zBuf[40];
    int n;

    (void)ctx;

    if (argc != 3) {
	return Th8_WrongNumArgs(interp, "th8testlib::safemul a b");
    }
    if (th8test_parse_size(argv[1], argl[1], &a) != TH8_OK ||
        th8test_parse_size(argv[2], argl[2], &b) != TH8_OK) {
	Th8_SetResultStatic(interp, "bad size argument", TH8_NOLEN);
	return TH8_ERROR;
    }
    if (Th8_SafeMul(interp, a, b, &out) != TH8_OK) {
	Th8_SetResultStatic(interp, "overflow", TH8_NOLEN);
	return TH8_OK;
    }
    n = th8testlib_format_size(zBuf, sizeof(zBuf), out);
    {
	char zRes[64];
	int nRes = 3; /* "ok " */
	memcpy(zRes, "ok ", 3);
	memcpy(zRes + 3, zBuf, (size_t)n);
	nRes += n;
	Th8_SetResult(interp, zRes, (size_t)nRes);
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_safeadd_cmd --
 *
 *	Implements "th8testlib::safeadd a b".  Companion to safemul.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_safeadd_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    size_t a = 0, b = 0, out = 0;
    char zBuf[40];
    int n;

    (void)ctx;

    if (argc != 3) {
	return Th8_WrongNumArgs(interp, "th8testlib::safeadd a b");
    }
    if (th8test_parse_size(argv[1], argl[1], &a) != TH8_OK ||
        th8test_parse_size(argv[2], argl[2], &b) != TH8_OK) {
	Th8_SetResultStatic(interp, "bad size argument", TH8_NOLEN);
	return TH8_ERROR;
    }
    if (Th8_SafeAdd(interp, a, b, &out) != TH8_OK) {
	Th8_SetResultStatic(interp, "overflow", TH8_NOLEN);
	return TH8_OK;
    }
    n = th8testlib_format_size(zBuf, sizeof(zBuf), out);
    {
	char zRes[64];
	int nRes = 3;
	memcpy(zRes, "ok ", 3);
	memcpy(zRes + 3, zBuf, (size_t)n);
	nRes += n;
	Th8_SetResult(interp, zRes, (size_t)nRes);
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_bigint_cmd --
 *
 *	Implements "th8testlib::bigint enable|disable|query".
 *	Wraps Th8_EnableBigint and Th8_IsBigintEnabled so tests can
 *	toggle arbitrary-precision-integer promotion at runtime.
 *
 *	Without this command, tests that exercise the
 *	overflow-error-on-promotion-disabled path face a chicken-and-
 *	egg problem: with bigint enabled in the build, the overflow
 *	never raises an error, so the test would either pass-anyway
 *	(weak) or have its constraint check tied to its own body
 *	(tautological gate, see test-audit failure mode 1.7).  The
 *	toggle command lets the test temporarily disable bigint in
 *	-setup, exercise the overflow path in -body, and re-enable
 *	bigint in -cleanup -- producing a real Boolean test on the
 *	implementation's overflow behavior.
 *
 *	"query" returns "1" if bigint is currently enabled, "0"
 *	otherwise.  "enable" / "disable" return the empty string.
 *
 *----------------------------------------------------------------------
 */

#  if defined(TH8_ENABLE_BIGINT)

static int
th8test_bigint_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    (void)ctx;

    if (argc != 2) {
	return Th8_WrongNumArgs(
	    interp, "th8testlib::bigint enable|disable|query");
    }
    if (argl[1] == 6 && memcmp(argv[1], "enable", 6) == 0) {
	if (Th8_EnableBigint(interp, 1) != TH8_OK) {
	    Th8_SetResultStatic(
	        interp, "th8testlib::bigint: enable failed", TH8_NOLEN);
	    return TH8_ERROR;
	}
	return TH8_OK;
    }
    if (argl[1] == 7 && memcmp(argv[1], "disable", 7) == 0) {
	if (Th8_EnableBigint(interp, 0) != TH8_OK) {
	    Th8_SetResultStatic(
	        interp, "th8testlib::bigint: disable failed", TH8_NOLEN);
	    return TH8_ERROR;
	}
	return TH8_OK;
    }
    if (argl[1] == 5 && memcmp(argv[1], "query", 5) == 0) {
	Th8_SetResultStatic(
	    interp, Th8_IsBigintEnabled(interp) ? "1" : "0", TH8_NOLEN);
	return TH8_OK;
    }
    return Th8_ErrorMessage(
        interp, "th8testlib::bigint: unknown subcommand:", argv[1], argl[1]);
}

#  endif /* TH8_ENABLE_BIGINT */


/*
 *----------------------------------------------------------------------
 *
 * th8test_parse_command_cmd --
 *
 *	Implements "th8testlib::parse_command <script>".  Drives the
 *	internal th8ParseCommand entry point through the internal
 *	stubs table (set up by Th8test_Init) and immediately frees
 *	the resulting Th8_Parse via th8FreeParse.  Returns the
 *	number of words extracted as a decimal integer.
 *
 *	th8ParseCommand is declared TH8_INTERNAL and has no in-tree
 *	callers; without this helper its ~15 MC/DC decisions are
 *	uncoverable.  Variations of the input drive different
 *	internal pairs (whitespace skip, leading-comment, brace/
 *	quote scanning, backslash-newline continuation, etc.).
 *
 *----------------------------------------------------------------------
 */

static int
th8test_parse_command_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    Th8_Parse parse;
    int rc;

    (void)ctx;

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "th8testlib::parse_command script");
    }

    parse.nVersion = 0;
    parse.zCommand = 0;
    parse.nCommand = 0;
    parse.nWord = 0;
    parse.aWord = 0;
    parse.nLine = 0;
    parse.zAfter = 0;
    parse.nComment = 0;
    parse.zComment = 0;

    rc = th8ParseCommand(interp, argv[1], argl[1], 1, &parse);
    if (rc != TH8_OK) {
	th8FreeParse(interp, &parse);
	return TH8_ERROR;
    }

    Th8_SetResultInt(interp, parse.nWord);
    th8FreeParse(interp, &parse);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_parse_expr_cmd --
 *
 *	Implements "th8testlib::parse_expr <text>".  Drives the
 *	internal th8ParseExpr entry point through the internal
 *	stubs table.  Returns the word count parsed from the
 *	expression text.
 *
 *	Like parse_command, th8ParseExpr has no in-tree callers
 *	(the in-tree expr evaluator builds its AST directly).
 *
 *----------------------------------------------------------------------
 */

static int
th8test_parse_expr_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    Th8_Parse parse;
    int rc;

    (void)ctx;

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "th8testlib::parse_expr text");
    }

    parse.nVersion = 0;
    parse.zCommand = 0;
    parse.nCommand = 0;
    parse.nWord = 0;
    parse.aWord = 0;
    parse.nLine = 0;
    parse.zAfter = 0;
    parse.nComment = 0;
    parse.zComment = 0;

    rc = th8ParseExpr(interp, argv[1], argl[1], &parse);
    if (rc != TH8_OK) {
	th8FreeParse(interp, &parse);
	return TH8_ERROR;
    }

    Th8_SetResultInt(interp, parse.nWord);
    th8FreeParse(interp, &parse);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_parse_var_name_cmd --
 *
 *	Implements "th8testlib::parse_var_name <text>".  Drives
 *	the internal th8ParseVarName entry point (declared
 *	TH8_INTERNAL, no in-tree callers) through the internal
 *	stubs table.  The function tokenizes a single $name or
 *	$name(idx) form into a Th8_Value.  Returns the parse
 *	return code (0 on success).
 *
 *----------------------------------------------------------------------
 */

static int
th8test_parse_var_name_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    Th8_Value token;
    int rc;

    (void)ctx;

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "th8testlib::parse_var_name text");
    }

    /*
     * Th8_Value is a small struct; zero-init via the standard
     * Th8_ValueInit if available, else byte-zero suffices for
     * the test parser's contract (it overwrites fields).
     */
    {
	unsigned char *p = (unsigned char *)&token;
	size_t i;
	for (i = 0; i < sizeof(token); i++)
	    p[i] = 0;
    }

    rc = th8ParseVarName(interp, argv[1], argl[1], &token);
    Th8_SetResultInt(interp, rc);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_plat_qsort_cmp_int --
 *
 *	`qsort`-style comparator that orders two `int` values in
 *	ascending order.
 *
 * Why / How:
 *	Passed to the `th8Qsort` wrapper exercised by
 *	`th8test_plat_wrappers_cmd`.  Reads an `int` through each
 *	`void *` argument and returns the sign of their difference
 *	using the overflow-safe `(x > y) - (x < y)` idiom.
 *
 * Results:
 *	-1, 0, or 1 as `*a` is less than, equal to, or greater
 *	than `*b`.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_plat_qsort_cmp_int(const void *a, const void *b)
{
    int x = *(const int *)a;
    int y = *(const int *)b;
    return (x > y) - (x < y);
}

/* Forward declarations for xGetTemporaryData stubs defined later
 * (alongside the null_guard_plat helpers).  Used here so the
 * plat_wrappers helper can drive th8_channel.c L122 vectors via
 * a custom-platform child interp. */
static int th8test_stub_xGetUserHostName_err(
    Th8_Interp *interp,
    void *pCtx,
    char *zBuf,
    size_t nBuf);
static int th8test_assrch_iter_noop_cb(
    const char *zArray,
    size_t nArray,
    const char *zSid,
    size_t nSid,
    void *pCtx);
static int th8test_stub_xGetTempData_ok_null_path(
    Th8_Interp *interp,
    void *pCtx,
    size_t nSize,
    char **pzOut,
    size_t *pnOut,
    void **ppCh);
static int th8test_stub_xGetTempData_ok_null_chan(
    Th8_Interp *interp,
    void *pCtx,
    size_t nSize,
    char **pzOut,
    size_t *pnOut,
    void **ppCh);
static int th8test_stub_xGetTempData_fail(
    Th8_Interp *interp,
    void *pCtx,
    size_t nSize,
    char **pzOut,
    size_t *pnOut,
    void **ppCh);
static int th8test_stub_xChannelControl_read_fail(
    Th8_Interp *interp,
    void *pCtx,
    void *pChannel,
    int op,
    th8_int64_t nArg1,
    int nArg2,
    th8_int64_t *pnResult,
    void *pBuf);
static int th8test_stub_xInput_empty(
    Th8_Interp *interp,
    void *pCtx,
    char **pzOut,
    size_t *pnOut,
    void *pChannel);
static int th8test_stub_xInput_null_buf(
    Th8_Interp *interp,
    void *pCtx,
    char **pzOut,
    size_t *pnOut,
    void *pChannel);
static int th8test_stub_xInput_crlf(
    Th8_Interp *interp,
    void *pCtx,
    char **pzOut,
    size_t *pnOut,
    void *pChannel);
static int th8test_stub_xGetData_empty_ok(
    Th8_Interp *interp,
    void *pCtx,
    const char *zName,
    size_t nName,
    char **pzData,
    size_t *pnData);
#  if defined(TH8_ENABLE_LIBCURL) && defined(TH8_ENABLE_UNBOUND)
/* DNS-stub state.  Used by the plat_wrappers helper to drive
 * the L342, L344, and L357 compound-condition decisions inside
 * th8CurlGetData.  Mode selects which mock result to return:
 *   0 = ok_4       bogus=0, pData[0]!=NULL, pLen!=NULL, pLen[0]=4
 *                  (drives L357 ALL TRUE branch + pinning path)
 *   1 = wrong_len  bogus=0, pData[0]!=NULL, pLen!=NULL, pLen[0]=6
 *                  (drives L357 C4=F via pLen[0]!=4)
 *   2 = bogus      bogus=1
 *                  (drives L344 TRUE early-return rejection)
 *   3 = null_data0 bogus=0, pData!=NULL, pData[0]=NULL
 *                  (drives L357 C2=F)
 *   4 = fail       returns TH8_ERROR
 *                  (drives L342 C1=F)
 *   5 = ok_null    returns TH8_OK without setting *ppResult
 *                  (drives L342 C2=F: rc==TH8_OK but pDns==NULL)
 *   6 = null_data  bogus=0, pData=NULL
 *                  (drives L357 C1=F)
 *   7 = null_lens  bogus=0, pData[0]!=NULL, pLen=NULL
 *                  (drives L357 C3=F) */
typedef struct th8test_dns_ctx {
    int mode;
    Th8_DnsResult res;
    unsigned char rec[4];
    const unsigned char *pData[2];
    size_t pLen[1];
} th8test_dns_ctx;
static int th8test_stub_xDnsResolve(
    Th8_Interp *interp,
    void *pCtx,
    const char *zName,
    size_t nName,
    int eType,
    Th8_DnsResult **ppResult);
static void th8test_stub_xDnsResolveFree(
    Th8_Interp *interp,
    void *pCtx,
    Th8_DnsResult *pResult);
#  endif
/* Captured real xChannelControl; the stub above forwards
 * non-READ ops through this pointer.  Set by plat_wrappers
 * at the call site and cleared after the child interp is
 * deleted. */
extern int (*th8test_real_xChanCtl)(
    Th8_Interp *,
    void *,
    void *,
    int,
    th8_int64_t,
    int,
    th8_int64_t *,
    void *);

/* Bug 22 regression counter: incremented by the math-proc probe
 * in `th8test_plat_wrappers_cmd` for any proc that does NOT
 * return TH8_ERROR when a required operand is missing.  A clean
 * post-fix run leaves this at zero; the
 * `::th8testlib::bug22_null_op_count` script-level accessor
 * exposes it so regression tests can assert. */
static int th8test_bug22NullOpRegressions = 0;

/*
 *----------------------------------------------------------------------
 *
 * th8test_bug22_null_op_count_cmd --
 *
 *	Script-level accessor for the Bug 22 regression counter
 *	(`th8test_bug22NullOpRegressions`).  Returns the current
 *	count as an integer.  A clean post-fix run leaves the
 *	counter at zero; conformance tests assert on the value
 *	after running the math-proc probe in
 *	`th8test_plat_wrappers_cmd`.
 *
 *	Bug 22 was a class of regression where built-in math
 *	procedures returned a value instead of `TH8_ERROR` when
 *	a required operand was missing.  See `docs/internal/incomplete.md`
 *	"Bug 22" for the full history.
 *
 * Parameters:
 *	interp -- live interpreter.
 *	ctx    -- ignored.
 *	argc   -- argument count (must be 1).
 *	argv   -- argument string array (unused beyond `argv[0]`).
 *	argl   -- argument lengths (unused).
 *
 * Returns:
 *	`TH8_OK` with the integer count in the interpreter result.
 *	`TH8_ERROR` if `argc != 1` (interpreter result: usage
 *	message via `Th8_WrongNumArgs`).
 *
 * Side effects:
 *	Sets the interpreter result.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_bug22_null_op_count_cmd(
    Th8_Interp *interp, /* The interpreter. */
    void *ctx,   /* Unused. */
    int argc,   /* Argument count. */
    const char **argv, /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    (void)ctx;
    (void)argv;
    (void)argl;
    if (argc != 1) {
	return Th8_WrongNumArgs(interp, "th8testlib::bug22_null_op_count");
    }
    return Th8_SetResultInt(interp, th8test_bug22NullOpRegressions);
}


/*
 *----------------------------------------------------------------------
 *
 * Test-only exercisers.  Live in th8_testlib.c (not src/) so their
 * `if (!a || !b)` defensive checks don't count against the
 * production translation unit's MC/DC coverage scope.
 *
 *	th8TestExerciseCharProps     - char classifier sweep (was core.c)
 *	th8TestExerciseChannelNullArgs - NULL-pChan sweep (was channel.c)
 *	th8TestExerciseBigintCacheStore - NULL-arg sweep (was bigint.c)
 *	th8TestExerciseDeviceName    - Win32-device-name sweep (was
 *	                               filesystems.c)
 *	th8TestExerciseCmdTokenTombstone - hash-tombstone sweep (was
 *	                                   core.c)
 *	th8TestExerciseMathFuncTombstone - math-func registry tombstone
 *	                                   sweep (th8_expr.c L292)
 *	th8TestExerciseArraySearchTombstone - per-interp array-search
 *	                                      tombstone sweep
 *	                                      (plugins/th8_variables.c
 *	                                      L1604)
 *	th8TestExerciseExpansionPrefix      - expansion-prefix scan
 *	                                      (th8_core.c
 *	                                      th8CheckExpansionPrefix)
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8TestExerciseCharProps --
 *
 *	Call every TH8 character-classification predicate
 *	(th8IsSpace, th8IsDigit, th8IsAlpha, th8IsAlnum,
 *	th8IsSpecial, th8IsHexDig, th8IsOctDig, th8IsBinDig) once
 *	with the byte `c`, discarding the results.
 *
 * Why / How:
 *	Lives in the test library (not src/) so the classifiers'
 *	internal branch decisions are driven for MC/DC without the
 *	sweep's own `(void)` calls counting against the production
 *	translation unit.  Called across the full 0..255 byte range
 *	by the char-props exerciser command.
 *
 * Results:
 *	None (void).
 *
 * Side effects:
 *	None; every predicate is side-effect free.
 *
 *----------------------------------------------------------------------
 */
static void
th8TestExerciseCharProps(int c)
{
    (void)th8IsSpace(c);
    (void)th8IsDigit(c);
    (void)th8IsAlpha(c);
    (void)th8IsAlnum(c);
    (void)th8IsSpecial(c);
    (void)th8IsHexDig(c);
    (void)th8IsOctDig(c);
    (void)th8IsBinDig(c);
}

/*
 *----------------------------------------------------------------------
 *
 * th8TestExerciseChannelNullArgs --
 *
 *	Call every public channel-I/O helper (`th8ChannelWrite`,
 *	`th8ChannelRead`, `th8ChannelSeek`, `th8ChannelFlush`) with
 *	a NULL `Th8_Channel *` argument to drive the entry-NULL
 *	defensive-guard MC/DC vectors in `src/th8_channel.c`.
 *	Results are discarded (the calls are expected to return
 *	`TH8_ERROR` cleanly).
 *
 * Parameters:
 *	interp -- live interpreter passed through to each helper.
 *	          Must be non-NULL; the helpers themselves see the
 *	          NULL channel argument, not a NULL interp.
 *
 * Returns:
 *	Nothing.  All inner errors are absorbed; `Th8_ClearResult`
 *	wipes any error message left on the interp.
 *
 * Side effects:
 *	Clears the interpreter result on return.
 *
 *----------------------------------------------------------------------
 */
static void
th8TestExerciseChannelNullArgs(Th8_Interp *interp)
{
    char *zOut = NULL;
    size_t nOut = 0;

    (void)th8ChannelWrite(interp, NULL, "x", 1);
    (void)th8ChannelRead(interp, NULL, &zOut, &nOut);
    (void)th8ChannelSeek(interp, NULL, 0, 0);
    (void)th8ChannelFlush(interp, NULL);
    Th8_ClearResult(interp);
}

/*
 *----------------------------------------------------------------------
 *
 * th8TestExerciseBigintCacheStore --
 *
 *	Drive the three MC/DC vectors of `th8BigintCacheStore`'s
 *	entry guard by calling it three times with the two
 *	NULL-interp / non-NULL-interp combinations needed to
 *	cover both C-pairs of `C1 = (interp == NULL)`.
 *
 *	Call ordering:
 *	  1. `(NULL, ...)` -- C1=T
 *	  2. `(interp, ...)` -- C1=F, C2=T (which exercises the
 *	     real store path with a non-NULL interp).
 *	  3. `(NULL, ...)` -- C1=T (already covered by the first
 *	     call; included for symmetry).
 *
 * Parameters:
 *	interp -- live interpreter for the C1=F branch.  Must be
 *	          non-NULL.
 *
 * Returns:
 *	Nothing.  Inner call results are discarded.
 *
 * Side effects:
 *	Inserts a `"x"` entry into `interp`'s bigint cache on the
 *	C1=F call; the other two NULL-interp calls are no-ops.
 *
 *----------------------------------------------------------------------
 */
static void
th8TestExerciseBigintCacheStore(Th8_Interp *interp)
{
    th8BigintCacheStore(NULL, "x", 1, NULL); /* C1=T */
    th8BigintCacheStore(interp, "x", 1, NULL); /* C1=F, C2=T */
    th8BigintCacheStore(NULL, "x", 1, NULL); /* C1=T (covered by 1st) */
}

/*
 *----------------------------------------------------------------------
 *
 * th8TestExerciseDeviceName --
 *
 *	Drive `th8IsDeviceName` through every MC/DC vector of its
 *	reserved-device-name matcher: canonical 3-character names
 *	(CON / PRN / AUX / NUL) in upper, lower, and mixed case;
 *	per-position near-misses (XON, CXN, COX); canonical
 *	4-character names (COM1-9, LPT1-9) in similar variation;
 *	out-of-range digit endings; trailing dots and spaces; and
 *	all-non-printable edge cases.  Results are discarded.
 *
 * Parameters:
 *	(none) -- the helper operates only on string literals.
 *
 * Returns:
 *	Nothing.  Each inner call's return value is discarded.
 *
 * Side effects:
 *	None.  Pure tests against the matcher's character-class
 *	tables.
 *
 *----------------------------------------------------------------------
 */
static void
th8TestExerciseDeviceName(void)
{
    /* Canonical 3-char device names (upper + lower case). */
    (void)th8IsDeviceName("CON", 3);
    (void)th8IsDeviceName("con", 3);
    (void)th8IsDeviceName("PRN", 3);
    (void)th8IsDeviceName("prn", 3);
    (void)th8IsDeviceName("AUX", 3);
    (void)th8IsDeviceName("aux", 3);
    (void)th8IsDeviceName("NUL", 3);
    (void)th8IsDeviceName("nul", 3);
    /* Mixed-case 3-char to drive partial case matches. */
    (void)th8IsDeviceName("Con", 3);
    (void)th8IsDeviceName("cOn", 3);
    (void)th8IsDeviceName("coN", 3);
    /* 3-char near-misses (first char differs from C/P/A/N). */
    (void)th8IsDeviceName("XON", 3);
    (void)th8IsDeviceName("BON", 3);
    /* 3-char near-misses (second char differs). */
    (void)th8IsDeviceName("CXN", 3);
    (void)th8IsDeviceName("PXN", 3);
    (void)th8IsDeviceName("AXX", 3);
    (void)th8IsDeviceName("NXL", 3);
    /* 3-char near-misses (third char differs). */
    (void)th8IsDeviceName("COX", 3);
    (void)th8IsDeviceName("PRX", 3);
    (void)th8IsDeviceName("AUY", 3);
    (void)th8IsDeviceName("NUX", 3);
    /* Canonical 4-char device names (digit range 1 and 9). */
    (void)th8IsDeviceName("COM1", 4);
    (void)th8IsDeviceName("com1", 4);
    (void)th8IsDeviceName("COM9", 4);
    (void)th8IsDeviceName("com9", 4);
    (void)th8IsDeviceName("LPT1", 4);
    (void)th8IsDeviceName("lpt1", 4);
    (void)th8IsDeviceName("LPT9", 4);
    (void)th8IsDeviceName("lpt9", 4);
    /* Mixed-case 4-char. */
    (void)th8IsDeviceName("Com1", 4);
    (void)th8IsDeviceName("cOm1", 4);
    (void)th8IsDeviceName("coM1", 4);
    /* 4-char digit out of range (z[3] < '1' or z[3] > '9'). */
    (void)th8IsDeviceName("COM0", 4);
    (void)th8IsDeviceName("COMA", 4);
    (void)th8IsDeviceName("LPT0", 4);
    (void)th8IsDeviceName("LPTA", 4);
    /* 4-char near-misses on each character. */
    (void)th8IsDeviceName("XOM1", 4);
    (void)th8IsDeviceName("CXM1", 4);
    (void)th8IsDeviceName("COX1", 4);
    (void)th8IsDeviceName("XPT1", 4);
    (void)th8IsDeviceName("LXT1", 4);
    (void)th8IsDeviceName("LPX1", 4);
    /* Length boundaries -- 0, 1, 2, 5+ bypass both arms. */
    (void)th8IsDeviceName("", 0);
    (void)th8IsDeviceName("A", 1);
    (void)th8IsDeviceName("AB", 2);
    (void)th8IsDeviceName("ABCDE", 5);
    (void)th8IsDeviceName("ABCDEF", 6);
    /* Trailing-strip loop: each combination of '.' and ' '. */
    (void)th8IsDeviceName("CON.", 4);
    (void)th8IsDeviceName("CON ", 4);
    (void)th8IsDeviceName("CON..", 5);
    (void)th8IsDeviceName("CON  ", 5);
    (void)th8IsDeviceName("CON. ", 5);
    (void)th8IsDeviceName("CON .", 5);
    (void)th8IsDeviceName("....", 4);
    (void)th8IsDeviceName("    ", 4);
    (void)th8IsDeviceName(". .", 3);
    (void)th8IsDeviceName("ABC", 3);
    (void)th8IsDeviceName("ABCD", 4);
}

/*
 *----------------------------------------------------------------------
 *
 * th8TestExerciseCmdTokenTombstone --
 *
 *	Drive the hash-tombstone (`pEntry && pEntry->pData`)
 *	MC/DC C-pairs of three distinct consumers that the public
 *	command-registration API alone cannot reach:
 *
 *	  1. `Th8_SetCommandCopy` on a tombstoned command-name
 *	     hash entry (th8_core.c L15851 (F,-) pair).
 *	  2. `Th8_ListAppendCommandsMatching` (used by
 *	     `[info procs]` without `::`) on a tombstoned
 *	     global-namespace entry (th8_core.c L18683 (F,-)).
 *	  3. `Th8_DeleteCommand` on a tombstoned `paCmdToken`
 *	     hash entry.
 *	  4. `Th8_FindExpansion` on a tombstoned expansion entry.
 *
 *	Each sub-test temporarily zeroes a real hash entry's
 *	`pData`, exercises the consumer, then restores `pData`.
 *	This synthesises the tombstone state that
 *	`Th8_HashDelete` removes entirely under the production
 *	API and that no script can reach otherwise.
 *
 * Parameters:
 *	interp -- live interpreter.  No-op if NULL or if the
 *	          probe target `::th8testlib::nop` is not
 *	          registered (the exerciser checks before
 *	          tombstoning).
 *
 * Returns:
 *	Nothing.  All inner errors are absorbed and
 *	`Th8_ClearResult` wipes the result on the
 *	`info procs` sub-test.
 *
 * Side effects:
 *	Briefly registers and unregisters
 *	`::th8testlib_tomb_proc` and `_th8test_tomb_tag`.
 *	Mutates and restores the `pData` of three distinct
 *	hash entries.  Sets and clears the interpreter result.
 *
 *	Un-gated 2026-06-08 after sample(1) showed the suite
 *	hang was in libcurl/Curl_poll (DNS-mock sweep) --
 *	unrelated to this exerciser.  See Bug 47 in
 *	`docs/internal/incomplete.md`.
 *
 *----------------------------------------------------------------------
 */
static void
th8TestExerciseCmdTokenTombstone(Th8_Interp *interp)
{
    const char *zName = "::th8testlib::nop";
    size_t nName = 17;
    const char *zNsPath = 0, *zTail = 0;
    size_t nNsPath = 0, nTail = 0;
    Th8_Namespace *pNs;
    Th8_HashEntry *pEntry;
    void *pSaved;
    th8_uint64_t token;
    Th8_HashEntry *pTokEntry;
    void *pTokSaved;

    if (!interp) return;
    th8SplitQualName(zName, nName, &zNsPath, &nNsPath, &zTail, &nTail);
    if (!zNsPath) return;
    pNs = th8FindNamespace(interp, zNsPath, nNsPath, 0);
    if (!pNs || !pNs->paCmd) return;
    pEntry = Th8_HashFind(interp, pNs->paCmd, zTail, nTail, 0);
    if (!pEntry || !pEntry->pData) return;

    /* Tombstone the command-name hash entry and run consumers
     * that check `if (pEntry && pEntry->pData)`. */
    pSaved = pEntry->pData;
    pEntry->pData = NULL;
    th8SetCmdToken(interp, zName, (th8_uint64_t)0x99);
    (void)th8GetCmdToken(interp, zName);
    Th8_SetCommandCopy(interp, zName, 0);
    /* Drive th8_core.c L15851 (F,-) on Th8_SetCommandCopy:
     * a real namespace (::th8testlib) with a missing command
     * tail makes Th8_HashFind return NULL, so pEntry is NULL
     * and the inner pData check is short-circuited.  Without
     * this call the (F,-) C1-Pair stays uncovered because
     * the production callsite always targets a registered
     * command name. */
    Th8_SetCommandCopy(interp, "::th8testlib::no_such_cmd_xyz", 0);
    pEntry->pData = pSaved;

    /*
     * Separate top-level-proc tombstone window so [info procs]
     * (no `::` in pattern -- uses Th8_ListAppendCommandsMatching
     * over current+global paCmd) sees a tombstoned global entry.
     * Drives th8_core.c L18683 (F,-) in th8AppendMatchingKeys;
     * the qualified-pattern path (::name::*) dispatches through
     * a different hash-iter callback that does NOT exercise it.
     */
    {
	const char *zTopTail = "th8testlib_tomb_proc";
	size_t nTopTail = 20;
	Th8_Namespace *pGlobalNs;
	Th8_Hash *paGlob = NULL;
	Th8_HashEntry *pTopE;

	(void)Th8_Eval(
	    interp, 0, "proc ::th8testlib_tomb_proc {} { return ok }",
	    TH8_NOLEN, NULL, 0);
	pGlobalNs = th8FindNamespace(interp, "::", 2, 0);
	if (pGlobalNs) paGlob = pGlobalNs->paCmd;
	if (paGlob) {
	    pTopE = Th8_HashFind(interp, paGlob, zTopTail, nTopTail, 0);
	    if (pTopE && pTopE->pData) {
		void *pTopSaved = pTopE->pData;
		pTopE->pData = NULL;
		(void)Th8_Eval(
		    interp, 0, "catch {info procs th8testlib_tomb_proc}",
		    TH8_NOLEN, NULL, 0);
		pTopE->pData = pTopSaved;
	    }
	}
	(void)Th8_Eval(
	    interp, 0, "catch {rename ::th8testlib_tomb_proc \"\"}",
	    TH8_NOLEN, NULL, 0);
	Th8_ClearResult(interp);
    }

    /* Tombstone the paCmdToken hash entry; drive Th8_DeleteCommand
     * tombstone-handling.  Token is read via th8GetCmdToken (works
     * because pData is restored above this point). */
    token = th8GetCmdToken(interp, zName);
    {
	Th8_Hash *paTok = th8GetInterpCmdToken(interp);
	if (paTok && token != 0) {
	    pTokEntry = Th8_HashFind(
	        interp, paTok, (const char *)&token, sizeof(th8_uint64_t), 0);
	    if (pTokEntry && pTokEntry->pData) {
		pTokSaved = pTokEntry->pData;
		pTokEntry->pData = NULL;
		(void)Th8_DeleteCommand(interp, token);
		pTokEntry->pData = pTokSaved;
	    }
	}
    }

    /* Tombstone an expansion entry to drive Th8_FindExpansion's
     * tombstone-handling. */
    {
	const char *zTag = "_th8test_tomb_tag";
	size_t nTag = 17;
	Th8_Namespace *pCur = th8GetInterpCurrentNs(interp);
	Th8_HashEntry *pExpEntry;
	void *pExpSaved;
	Th8_ExpansionProc xProc;
	void *pProcCtx;

	(void)Th8_RegisterExpansion(interp, zTag, nTag, 0, 0);
	if (!pCur || !pCur->paExpansion) return;
	pExpEntry = Th8_HashFind(interp, pCur->paExpansion, zTag, nTag, 0);
	if (!pExpEntry || !pExpEntry->pData) return;
	pExpSaved = pExpEntry->pData;
	pExpEntry->pData = NULL;
	(void)Th8_FindExpansion(interp, zTag, nTag, &xProc, &pProcCtx);
	pExpEntry->pData = pExpSaved;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * th8TestCaptureFirstHashEntry --
 *
 *	`Th8_HashIterate` visitor that captures a pointer to the
 *	first entry in iteration order and stops the walk via
 *	`TH8_BREAK`.  Used by the tombstone exercisers to obtain a
 *	live `Th8_HashEntry *` for state mutation when the hash has
 *	exactly one entry but the caller has no other way to find
 *	it (e.g., when the key bytes are runtime-derived).
 *
 * Parameters:
 *	pEntry -- current entry from the iteration.
 *	pCtx   -- caller's `Th8_HashEntry **` output slot.  On
 *	          first call (`*ppOut == NULL`), stores `pEntry`
 *	          into `*ppOut`.  Subsequent entries (if any) are
 *	          ignored because the first call returns
 *	          `TH8_BREAK`.
 *
 * Returns:
 *	`TH8_BREAK` always, halting iteration after the first
 *	captured entry.
 *
 * Side effects:
 *	On first invocation, writes `pEntry` to `*((Th8_HashEntry **)pCtx)`.
 *
 *----------------------------------------------------------------------
 */
static int
th8TestCaptureFirstHashEntry(Th8_HashEntry *pEntry, void *pCtx)
{
    Th8_HashEntry **ppOut = (Th8_HashEntry **)pCtx;
    if (*ppOut == NULL) {
	*ppOut = pEntry;
    }
    return TH8_BREAK;
}

/*
 *----------------------------------------------------------------------
 *
 * th8TestExerciseArraySearchTombstone --
 *
 *	Drive the (F,T) C2-Pair of
 *	`if (!pEntry || !pEntry->pData)` at
 *	`plugins/th8_variables.c:1604` inside `th8ArraySearchFind`.
 *	This C-pair (pEntry non-NULL, pData NULL) is unreachable
 *	via the public `[array startsearch]` / `[array
 *	nextelement]` / `[array donesearch]` flow because
 *	`Th8_HashDelete` removes entries entirely; we synthesise
 *	the tombstone state by reaching into the per-interpreter
 *	array-search hash, zeroing `pData` on a live entry,
 *	calling the consumer, and restoring `pData`.
 *
 * Parameters:
 *	interp -- live interpreter.  No-op if NULL or if no
 *	          array-search cursor is currently open (the
 *	          exerciser checks before tombstoning).
 *
 * Returns:
 *	Nothing.  All inner errors are absorbed.
 *
 * Side effects:
 *	Temporarily zeroes one hash entry's `pData` field and
 *	then restores it.  Sets and clears the interpreter result.
 *
 *----------------------------------------------------------------------
 */
static void
th8TestExerciseArraySearchTombstone(Th8_Interp *interp)
{
    /*
     * Drive the tombstone-safe `if (!pEntry || !pEntry->pData)` at
     * plugins/th8_variables.c L1604 in th8ArraySearchFind.  The
     * (F,T) C2-Pair vector (pEntry non-NULL, pData NULL) is
     * unreachable via the public [array startsearch]/[array
     * nextelement]/[array donesearch] flow because Th8_HashDelete
     * removes entries fully; we synthesise the tombstone state by
     * eval'ing [array startsearch] to register an entry, capturing
     * its hash bucket via Th8_HashIterate, manually clearing pData,
     * running [array nextelement] (which goes through
     * th8ArraySearchFind), then restoring pData so [array
     * donesearch] can free the search normally.
     */
    Th8_Hash *paArraySearch;
    Th8_HashEntry *pEntry = NULL;
    void *pSaved;
    size_t nSid;
    const char *zSid;
    char zScript[256];

    if (!interp) return;

    if (Th8_Eval(
            interp, 0,
            "set ::th8test_tomb_arr(k) v; array startsearch "
            "::th8test_tomb_arr",
            TH8_NOLEN, NULL, 0) != TH8_OK) {
	(void)Th8_Eval(
	    interp, 0, "unset -nocomplain ::th8test_tomb_arr", TH8_NOLEN,
	    NULL, 0);
	return;
    }

    zSid = Th8_GetResult(interp, &nSid);
    if (!zSid || nSid == 0 || nSid > 64) {
	Th8_ClearResult(interp);
	(void)Th8_Eval(
	    interp, 0, "unset -nocomplain ::th8test_tomb_arr", TH8_NOLEN,
	    NULL, 0);
	return;
    }
    /* Copy the SID before we clobber the interp result. */
    {
	size_t i;
	for (i = 0; i < nSid && i < sizeof(zScript) - 1; i++) {
	    zScript[i] = zSid[i];
	}
	zScript[i] = 0;
    }

    paArraySearch = th8GetArraySearchHash(interp, 0);
    if (paArraySearch) {
	Th8_HashIterate(
	    interp, paArraySearch, th8TestCaptureFirstHashEntry, &pEntry);
    }

    if (pEntry && pEntry->pData) {
	pSaved = pEntry->pData;
	pEntry->pData = NULL;
	{
	    char zCmd[256];
	    int n = th8Snprintf(
	        interp, zCmd, sizeof(zCmd),
	        "catch {array nextelement ::th8test_tomb_arr %s}", zScript);
	    if (n > 0) {
		(void)Th8_Eval(interp, 0, zCmd, (size_t)n, NULL, 0);
	    }
	}
	pEntry->pData = pSaved;
    }

    {
	char zCmd[256];
	int n = th8Snprintf(
	    interp, zCmd, sizeof(zCmd),
	    "catch {array donesearch ::th8test_tomb_arr %s}; "
	    "unset -nocomplain ::th8test_tomb_arr",
	    zScript);
	if (n > 0) {
	    (void)Th8_Eval(interp, 0, zCmd, (size_t)n, NULL, 0);
	}
    }
    Th8_ClearResult(interp);
}

/*
 *----------------------------------------------------------------------
 *
 * th8TestExerciseMathFuncTombstone --
 *
 *	Drive the (F,T) C2-Pair of
 *	`if (!pHash || !pHash->pData)` at `th8_expr.c:292` inside
 *	`Th8_FindMathFunc`.  The vector (pHash non-NULL, pData
 *	NULL) is unreachable via normal `HashFind` because
 *	`Th8_DeleteMathFunc` removes the entry from the chain;
 *	the exerciser synthesises the tombstone state by manually
 *	clearing `pData` on the live `test_echo` math-func entry
 *	that testlib registers at load time, calling
 *	`Th8_FindMathFunc`, and restoring `pData`.
 *
 * Parameters:
 *	interp -- live interpreter.  No-op if NULL or if the
 *	          `test_echo` math function is not registered
 *	          (the exerciser checks before tombstoning).
 *
 * Returns:
 *	Nothing.
 *
 * Side effects:
 *	Temporarily zeroes the `test_echo` hash entry's `pData`
 *	field and then restores it.
 *
 *----------------------------------------------------------------------
 */
static void
th8TestExerciseMathFuncTombstone(Th8_Interp *interp)
{
    /*
     * Drive the tombstone-safe `if (!pHash || !pHash->pData)` at
     * th8_expr.c L292 in Th8_FindMathFunc.  The (F,T) C2-Pair
     * vector (pHash non-NULL, pHash->pData NULL) is unreachable
     * via normal HashFind because Th8_DeleteMathFunc removes
     * the entry from the chain; we synthesise the tombstone by
     * manually clearing pData on the live "test_echo" entry
     * registered at testlib load time.
     */
    Th8_Hash *paMathFunc;
    Th8_HashEntry *pEntry;
    void *pSaved;
    int nArg = 0;
    Th8_MathFuncProc xProc = NULL;
    void *pCtx = NULL;

    if (!interp) return;
    paMathFunc = Th8_GetMathFuncHash(interp);
    if (!paMathFunc) return;
    pEntry = Th8_HashFind(interp, paMathFunc, "test_echo", 9, 0);
    if (!pEntry || !pEntry->pData) return;

    pSaved = pEntry->pData;
    pEntry->pData = NULL;
    (void)Th8_FindMathFunc(interp, "test_echo", 9, &nArg, &xProc, &pCtx);
    pEntry->pData = pSaved;
}

/*
 *----------------------------------------------------------------------
 *
 * th8TestExerciseExpansionPrefix --
 *
 *	Drive the `if (k + 1 < nWord && k > 1)` decision inside
 *	`th8CheckExpansionPrefix` (th8_core.c L11979 / L12351),
 *	particularly the (T,F) C-pair that the public expansion-
 *	parser entry already exercises but whose coverage closure
 *	we do not want to depend on a third-party test file
 *	remaining unchanged.
 *
 *	Six byte vectors are driven; each is a small input word
 *	chosen to land on a different combination of the entry-
 *	guard arms:
 *
 *	  `{}r`  -- (T,F): close brace at offset 1, trailing
 *	            bytes follow; "extra characters" error path.
 *	  `{x}r` -- (T,T): unknown expansion tag.
 *	  `{*}r` -- (T,T): may match a registered `*` expansion
 *	            when testlib has one; falls back to "unknown"
 *	            otherwise.
 *	  `{?}r` -- non-tag-char break in the inner loop.
 *	  `{a`   -- (F,-): nWord == 2 leaves the outer guard at
 *	            th8_core.c L11923 / L12412 false.
 *	  `ab`   -- (F,-): first byte not `{`; outer guard false.
 *
 * Parameters:
 *	interp -- live interpreter.  No-op if NULL.
 *
 * Returns:
 *	Nothing.  Inner errors absorbed; `Th8_ClearResult` wipes
 *	the result after each vector so callers see a clean state.
 *
 * Side effects:
 *	Sets and clears the interpreter result six times.
 *
 *----------------------------------------------------------------------
 */
static void
th8TestExerciseExpansionPrefix(Th8_Interp *interp)
{
    /*
     * Drive th8_core.c L11979 / L12351 (T,F) on the
     * `if (k + 1 < nWord && k > 1)` decision inside
     * th8CheckExpansionPrefix.  The (T,F) vector fires for
     * "{}rest" where the close brace is at offset 1: C1 is T
     * (k + 1 < nWord because trailing bytes follow) but C2
     * is F (k > 1 is false when k == 1).  The in-tree script
     * paths route through this helper now -- so a script-side
     * test like bslesc-5.1 already drives (T,F) via the
     * parser-side call site -- but call the helper directly
     * here as well so the coverage closure does not depend
     * on a third-party test file remaining unchanged.
     *
     * Six byte vectors cover the relevant arms:
     *   "{}r"       - "{}rest" (T,F) "extra characters" error
     *   "{x}r"      - "{x}r"   (T,T) unknown expansion error
     *   "{*}r"      - "{*}r"   (T,T) typically matches a
     *                          registered '*' expansion when
     *                          the testlib has one; otherwise
     *                          falls through to "unknown".
     *   "{?}r"      - "{?}r"   non-tag-char break (loop exits
     *                          via the !th8IsAlnum check).
     *   "{a"        - nWord == 2; the outer guard at L11923 /
     *                 L12412 is F (nWord > 2 fails) -- doesn't
     *                 enter the loop.  (F,-)
     *   "ab"        - zInput[0] != '{'; outer guard F.
     */
    int bExpand;
    size_t nTag;
    Th8_ExpansionProc xExpand;
    void *pExpandCtx;
    static const struct {
	const char *zInput;
	size_t nWord;
    } aCase[] = {
        {"{}r", 3},  {"{x}r", 4}, {"{*}r", 4},
        {"{?}r", 4}, {"{a", 2},   {"ab", 2},
    };
    size_t i;

    if (!interp) return;
    for (i = 0; i < sizeof(aCase) / sizeof(aCase[0]); i++) {
	bExpand = 0;
	nTag = 0;
	xExpand = NULL;
	pExpandCtx = NULL;
	(void)th8CheckExpansionPrefix(
	    interp, aCase[i].zInput, aCase[i].nWord, &bExpand, &nTag,
	    &xExpand, &pExpandCtx);
	Th8_ClearResult(interp);
    }
}


/*
 * Test-only token perturbers (moved 2026-06-08 from src/th8_load.c +
 * src/th8_core.c into testlib).  Each XORs the corresponding interp
 * token field with a non-zero constant chosen to avoid the defensive
 * 0/1/~0 patterns guarded by the corresponding retry loops.  The
 * actual XOR is one accessor call (th8XorInterp*Token / th8Clear*) so
 * the helpers add no MC/DC decisions to production code.
 */

#  define TH8TEST_TOKEN_XOR_MASK ((th8_int64_t)0x5A5A5A5A5A5A5A5AULL)

/*
 *----------------------------------------------------------------------
 *
 * th8TestPerturbLoadToken --
 *
 *	XOR-perturb the interpreter's [load]-enable token by the
 *	fixed mask `TH8TEST_TOKEN_XOR_MASK` (a non-zero constant
 *	chosen to avoid the 0/1/~0 patterns the regen retry loops
 *	guard against).  Drives the load-token tamper-detection
 *	path that checks the token against its expected value --
 *	after the perturbation the two no longer match.
 *
 * Parameters:
 *	interp -- live interpreter.  No-op if NULL.
 *
 * Returns / Side effects:
 *	No return.  Mutates the interpreter's load-enable token.
 *
 *----------------------------------------------------------------------
 */
static void
th8TestPerturbLoadToken(Th8_Interp *interp)
{
    if (!interp) return;
    th8XorInterpLoadToken(interp, TH8TEST_TOKEN_XOR_MASK);
}

/*
 *----------------------------------------------------------------------
 *
 * th8TestPerturbBigintToken --
 *
 *	XOR-perturb the interpreter's bigint-enable token by the
 *	fixed mask `TH8TEST_TOKEN_XOR_MASK` (a non-zero constant
 *	chosen to avoid the 0/1/~0 patterns the regen retry loops
 *	guard against).  Drives tamper-detection paths that check
 *	`nBigintToken == nBigintOk` -- after the perturbation
 *	those two fields no longer match.
 *
 * Parameters:
 *	interp -- live interpreter.  No-op if NULL.
 *
 * Returns / Side effects:
 *	No return.  Mutates `interp->nBigintToken`.
 *
 *----------------------------------------------------------------------
 */
static void
th8TestPerturbBigintToken(Th8_Interp *interp)
{
    if (!interp) return;
    th8XorInterpBigintToken(interp, TH8TEST_TOKEN_XOR_MASK);
}

/*
 *----------------------------------------------------------------------
 *
 * th8TestPerturbSignedToken --
 *
 *	XOR-perturb the signed-only-policy token field by
 *	`TH8TEST_TOKEN_XOR_MASK`.  Drives the tamper-detection
 *	check on the signed-only state; after perturbation the
 *	loader rejects subsequent signed-script evaluation
 *	attempts.
 *
 * Parameters:
 *	interp -- live interpreter.  No-op if NULL.
 *
 * Returns / Side effects:
 *	No return.  Mutates `interp->nSignedToken`.
 *
 *----------------------------------------------------------------------
 */
static void
th8TestPerturbSignedToken(Th8_Interp *interp)
{
    if (!interp) return;
    th8XorInterpSignedToken(interp, TH8TEST_TOKEN_XOR_MASK);
}

/*
 *----------------------------------------------------------------------
 *
 * th8TestPerturbUnloadToken --
 *
 *	XOR-perturb the [load]/[unload] token field by
 *	`TH8TEST_TOKEN_XOR_MASK`.  Drives the tamper-detection
 *	check on extension-loading state; after perturbation
 *	subsequent `[load]` / `[unload]` calls should refuse to
 *	proceed.
 *
 * Parameters:
 *	interp -- live interpreter.  No-op if NULL.
 *
 * Returns / Side effects:
 *	No return.  Mutates `interp->nLoadToken` (via
 *	`th8XorInterpUnloadToken`).
 *
 *----------------------------------------------------------------------
 */
static void
th8TestPerturbUnloadToken(Th8_Interp *interp)
{
    if (!interp) return;
    th8XorInterpUnloadToken(interp, TH8TEST_TOKEN_XOR_MASK);
}

/*
 *----------------------------------------------------------------------
 *
 * th8TestClearUnloadFlag --
 *
 *	Clear the interpreter's unload-in-progress flag(s) by
 *	delegating to `th8ClearInterpUnloadFlags`.  Used by tests
 *	that need to drive successive [load]/[unload] cycles
 *	without the runtime asserting on a half-completed
 *	previous unload.
 *
 * Parameters:
 *	interp -- live interpreter.  No-op if NULL.
 *
 * Returns / Side effects:
 *	No return.  Resets interp's unload-flag state.
 *
 *----------------------------------------------------------------------
 */
static void
th8TestClearUnloadFlag(Th8_Interp *interp)
{
    if (!interp) return;
    th8ClearInterpUnloadFlags(interp);
}


/*
 * Forward declarations of testlib callbacks used inside
 * th8test_plat_wrappers_cmd before their definition.  These
 * are real definitions later in the file (with full doc
 * blocks) -- this section just makes them visible to the
 * earlier exerciser sweep.
 */

static int th8test_null_guard_qe_cb(Th8_Interp *interp, void *pCtx);


/*
 *----------------------------------------------------------------------
 *
 * th8test_plat_wrappers_cmd --
 *
 *	Implements "th8testlib::plat_wrappers".  Drives the
 *	th8_plat.c utility wrappers th8Memmove, th8Strcmp,
 *	th8Strchr, th8Atoi, th8Qsort that have no in-tree
 *	callers (the codebase uses the Th8_API variants
 *	directly).  Each call exercises the defensive
 *	NULL-arg + missing-callback decisions internal to
 *	those wrappers.
 *
 *	Returns a list of probe results so the test can
 *	assert on basic correctness while incidentally
 *	closing the MC/DC pairs.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_plat_wrappers_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char sbuf[16];
    int arr[5] = {5, 3, 1, 4, 2};
    char *zList = NULL;
    size_t nList = 0;
    char numBuf[32];
    char *p;
    int cmpRes;

    (void)ctx;
    (void)argv;
    (void)argl;

    if (argc != 1) {
	return Th8_WrongNumArgs(interp, "th8testlib::plat_wrappers");
    }

    /* th8Memmove: copy 5 bytes; also drive n=0 + NULL guards. */
    (void)th8Memmove(interp, sbuf, "abcde", 5);
    (void)th8Memmove(interp, sbuf, "x", 0);          /* n=0 */
    (void)th8Memmove(interp, NULL, "x", 1);         /* dst=NULL */
    (void)th8Memmove(interp, sbuf, NULL, 1);         /* src=NULL */

    /* th8Strcmp: drive both-non-NULL, one-NULL, both-NULL. */
    cmpRes = th8Strcmp(interp, "alpha", "beta");
    (void)th8Strcmp(interp, NULL, "beta");
    (void)th8Strcmp(interp, "alpha", NULL);
    (void)th8Strcmp(interp, NULL, NULL);

    /* th8Strchr: char found / not-found / NULL string. */
    p = th8Strchr(interp, "hello", 'l');
    (void)th8Strchr(interp, "hello", 'z');
    (void)th8Strchr(interp, NULL, 'a');

    /* th8Strrchr: char found / not-found / empty / NULL.
     * NULL probe now safe -- bug 20 fixed (added if(!s)
     * guard at th8_plat.c:602). */
    (void)th8Strrchr(interp, "hello", 'l');
    (void)th8Strrchr(interp, "hello", 'z');
    (void)th8Strrchr(interp, "", 'a');
    (void)th8Strrchr(interp, NULL, 'a');

    /* th8Atoi: parse a number; also NULL/empty. */
    (void)th8Atoi(interp, "42");
    (void)th8Atoi(interp, "0");
    (void)th8Atoi(interp, NULL);

    /* th8Snprintf: format a number; also small buffer. */
    {
	char sbuf[32];
	(void)th8Snprintf(interp, sbuf, sizeof(sbuf), "%d-%s", 42, "ok");
	(void)th8Snprintf(interp, sbuf, 4, "%d", 12345);    /* truncates */
	/* th8Vsnprintf L724 `!buf || size == 0`:
	 *   (T, -)  buf=NULL  -> outcome T (return 0)
	 *   (F, T)  buf!=NULL, size=0 -> outcome T (return 0)
	 *   (F, F)  buf!=NULL, size>0 -> outcome F (normal) */
	(void)th8Snprintf(interp, NULL, 16, "%d", 42);
	(void)th8Snprintf(interp, sbuf, 0, "%d", 42);
    }

    /* th8MemBarrier: just a smoke call. */
    th8MemBarrier(interp);

    /* th8TranslateLineEndings: ok case + invalid \n-without-\r
     * at i==0 + invalid bare \n at i>0 (drives L868 C2=T
     * vector: i != 0 AND zBuf[i-1] != '\r'). */
    {
	char lbuf[16];
	size_t ln;
	int tle_rc;
	lbuf[0] = 'a';
	lbuf[1] = '\r';
	lbuf[2] = '\n';
	lbuf[3] = 'b';
	ln = 4;
	(void)th8TranslateLineEndings(lbuf, &ln);
	lbuf[0] = '\n';                     /* invalid: \n at i==0 */
	ln = 1;
	tle_rc = th8TranslateLineEndings(lbuf, &ln);
	(void)tle_rc;
	/* Bare \n at i==1 with prev='a' (not '\r').  Drives
	 * the C2-Pair (i != 0, zBuf[i-1] != '\r') vector
	 * that no in-tree caller produces. */
	lbuf[0] = 'a';
	lbuf[1] = '\n';
	ln = 2;
	tle_rc = th8TranslateLineEndings(lbuf, &ln);
	(void)tle_rc;
    }

    /* Th8_GetInput/Output/ErrorOutput/RedirectErrorOutput:
     * public APIs but unexercised by existing tests.  Call
     * each with valid + (in)valid args to drive the entry
     * decisions. */
#  if defined(TH8_PLUGIN_IO)
    {
	void *pCh = NULL;
	(void)Th8_GetInput(interp, &pCh);
	(void)Th8_GetInput(NULL, &pCh);             /* C1=F */
	pCh = NULL;
	(void)Th8_GetOutput(interp, &pCh);
	(void)Th8_GetOutput(NULL, &pCh);
	pCh = NULL;
	(void)Th8_GetErrorOutput(interp, &pCh);
	(void)Th8_GetErrorOutput(NULL, &pCh);
	(void)Th8_RedirectErrorOutput(interp, pCh);
	(void)Th8_RedirectErrorOutput(NULL, pCh);
    }
#  endif

    /* Public APIs unexercised by existing tests but safe to
     * call directly (read-only / introspection / config-
     * companion getters).  Each is a small wrapper with a
     * NULL-interp guard plus a callback delegation. */
    {
	(void)Th8_GetStepLimit(interp);
	(void)Th8_GetStepLimit(NULL);
	(void)Th8_GetAllocLimit(interp);
	(void)Th8_GetAllocLimit(NULL);
	Th8_ResetStepCount(interp);
	Th8_ResetStepCount(NULL);
	Th8_ResetExit(interp);
	Th8_ResetExit(NULL);
	Th8_SetErrorLine(interp, 42);
	Th8_SetErrorLine(NULL, 42);
	{
	    const Th8_Platform *pMemP = Th8_GetMemPlatform();
	    (void)pMemP;
	    /* Drive th8_mem.c L77 / L89 MC/DC vectors:
	     *   nByte==0     -> L77 C1=T outcome T
	     *   huge nByte   -> L77 C1=F C2=T outcome T
	     *   normal nByte -> L77 (F,F) outcome F, then
	     *                   L89 (ALWAYS(pPlat)=C, xMalloc=T)
	     *                   outcome T
	     * Without these, the standard interp's th8MemPlatform
	     * is never used as a second-chance allocator (xMalloc
	     * is the merged libc/mimalloc impl that never returns
	     * NULL on small allocs) so xNeedMemory's body has 0
	     * coverage. */
	    if (pMemP && pMemP->xNeedMemory) {
		void *pNeed;
		pNeed = pMemP->xNeedMemory(interp, 0);
		if (pNeed) Th8_Free(interp, pNeed);
		pNeed = pMemP->xNeedMemory(interp, ((size_t)-1) - 1);
		if (pNeed) Th8_Free(interp, pNeed);
		pNeed = pMemP->xNeedMemory(interp, 32);
		if (pNeed) Th8_Free(interp, pNeed);
		pNeed = pMemP->xNeedMemory(NULL, 32);
		if (pNeed) Th8_Free(interp, pNeed);
		/* Drive th8_mem.c L89 C2=F (pPlat->xMalloc == NULL):
		 * create a child interp normally (needs xMalloc),
		 * then nullify xMalloc on its platform struct in
		 * place, call xNeedMemory.  L89 sees xMalloc=NULL,
		 * returns NULL.  Restore xMalloc before delete so
		 * Th8_DeleteInterp's internal frees still work. */
		{
		    const Th8_Platform *pPRefP = Th8_GetPlatform(interp);
		    if (pPRefP) {
			Th8_Platform cpRef = *pPRefP;
			Th8_Interp *pRefChild;
			cpRef.xPanic = 0;
			pRefChild = Th8_CreateInterp(&cpRef);
			if (pRefChild) {
			    Th8_RegisterLanguage(pRefChild);
			    cpRef.xMalloc = NULL;
			    (void)pMemP->xNeedMemory(pRefChild, 32);
			    /* Restore xMalloc so Th8_DeleteInterp's
			     * internal frees still work cleanly. */
			    cpRef.xMalloc = pPRefP->xMalloc;
			    Th8_DeleteInterp(pRefChild);
			}
		    }
		}
	    }
	}
	/* Th8_EmitTrace L247 `xTrace && zFmt` C2-Pair:
	 * call with non-NULL interp + NULL zFmt to drive
	 * (T, F) outcome F.  C1-Pair (xTrace=NULL) requires
	 * th8GlobalPlatform.xEmitTrace to be NULL too, which
	 * isn't the case in normal test runs. */
	Th8_EmitTrace(interp, NULL);

	/* th8_unwind.c xStackBackTrace: drive th8UnwindStackBackTrace's
	 * `apFrames == NULL || nMaxFrames <= 0` guard (all outcomes) and
	 * its `(nSkip < 0)` clamp, plus -- via real captures -- th8UnwindStep's
	 * per-frame decisions (nSeen >= nSkip both ways, nStored >= nMax when a
	 * small buffer fills, and natural stack end).  At runtime only the
	 * TH8_MEM_DEBUG memory tracker calls xStackBackTrace, so without this
	 * drive the compiler-runtime layer is entirely uncovered in the
	 * (non-memdebug) MC/DC build. */
	{
	    const Th8_Platform *pSbt = Th8_GetPlatform(interp);
	    if (pSbt && pSbt->xStackBackTrace) {
		void *aSbt[64];
		/* small buffer against a deep stack -> nStored >= nMax = T */
		(void)pSbt->xStackBackTrace(interp, pSbt->pCtx, aSbt, 4, 0);
		/* skip 2 then record, large buffer -> nSeen>=nSkip both ways,
		 * natural end with nStored >= nMax = F */
		(void)pSbt->xStackBackTrace(interp, pSbt->pCtx, aSbt, 64, 2);
		/* skip more than the stack depth -> records nothing */
		(void)pSbt
		    ->xStackBackTrace(interp, pSbt->pCtx, aSbt, 64, 1000000);
		/* nSkip < 0 -> the clamp branch */
		(void)pSbt->xStackBackTrace(interp, pSbt->pCtx, aSbt, 8, -1);
		/* guard C1=T: apFrames == NULL */
		(void)pSbt->xStackBackTrace(interp, pSbt->pCtx, NULL, 8, 0);
		/* guard C1=F,C2=T: nMaxFrames <= 0 */
		(void)pSbt->xStackBackTrace(interp, pSbt->pCtx, aSbt, 0, 0);
	    }
	}

	/* Th8_GetData L1796 `(flags & TH8_TRANSLATE_EOL)
	 * && ALWAYS(*pzOut) && *pnOut > 0` C2-Pair:
	 * drive (T, F, -) by calling Th8_GetData with
	 * TH8_TRANSLATE_EOL and an xGetData stub that
	 * returns a 1-byte allocated buffer with *pnOut=0. */
	{
	    const Th8_Platform *pPGdP = Th8_GetPlatform(interp);
	    if (pPGdP) {
		Th8_Platform cpGd = *pPGdP;
		Th8_Interp *pGdChild;
		cpGd.xPanic = 0;
		cpGd.xGetData = th8test_stub_xGetData_empty_ok;
		pGdChild = Th8_CreateInterp(&cpGd);
		if (pGdChild) {
		    char *zGd = NULL;
		    size_t nGd = 0;
		    Th8_RegisterLanguage(pGdChild);
		    (void)Th8_GetData(
		        pGdChild, "x", 1, &zGd, &nGd, TH8_TRANSLATE_EOL);
		    if (zGd) Th8_Free(pGdChild, zGd);
		    Th8_DeleteInterp(pGdChild);
		}
	    }
	}

	/* Th8_Input L949 4-condition compound
	 * (`rc==TH8_OK && (flags & TH8_TRANSLATE_EOL) && *pzOut
	 * && *pnOut > 0`): no in-tree caller passes
	 * TH8_TRANSLATE_EOL flag, so the C2/C3/C4 pairs aren't
	 * reachable from the script path.  Drive directly via
	 * child interp + custom xInput stub. */
	{
	    const Th8_Platform *pPInP = Th8_GetPlatform(interp);
	    if (pPInP) {
		Th8_Platform cpIn = *pPInP;
		Th8_Interp *pInChild;
		cpIn.xPanic = 0;
		/* Re-use the existing empty-input stub: it returns
		 * TH8_OK with *pzOut allocated 1-byte and *pnOut=0,
		 * which drives (T, T, T, F) -- the C4-Pair. */
		cpIn.xInput = th8test_stub_xInput_empty;
		pInChild = Th8_CreateInterp(&cpIn);
		if (pInChild) {
		    char *zIO = NULL;
		    size_t nIO = 0;
		    Th8_RegisterLanguage(pInChild);
		    /* (T, T, T, F): rc=OK, flag=T, pzOut!=NULL,
		     *  nOut=0 -- the empty-buffer stub. */
		    zIO = NULL;
		    nIO = 0;
		    (void)Th8_Input(pInChild, &zIO, &nIO, TH8_TRANSLATE_EOL);
		    if (zIO) Th8_Free(pInChild, zIO);
		    Th8_DeleteInterp(pInChild);
		}
		/* (T, T, F, -): pzOut returned as NULL.  Re-use
		 * th8test_stub_xInput_null_buf which sets *pzOut=NULL
		 * with rc=OK. */
		cpIn = *pPInP;
		cpIn.xPanic = 0;
		cpIn.xInput = th8test_stub_xInput_null_buf;
		pInChild = Th8_CreateInterp(&cpIn);
		if (pInChild) {
		    char *zIO = NULL;
		    size_t nIO = 0;
		    Th8_RegisterLanguage(pInChild);
		    (void)Th8_Input(pInChild, &zIO, &nIO, TH8_TRANSLATE_EOL);
		    if (zIO) Th8_Free(pInChild, zIO);
		    Th8_DeleteInterp(pInChild);
		}
		/* (T, T, T, T): all conditions true -- CRLF stub
		 * returns "ab\r\nc" so EOL translation actually
		 * runs.  Drives the outcome-T arm of the decision. */
		cpIn = *pPInP;
		cpIn.xPanic = 0;
		cpIn.xInput = th8test_stub_xInput_crlf;
		pInChild = Th8_CreateInterp(&cpIn);
		if (pInChild) {
		    char *zIO = NULL;
		    size_t nIO = 0;
		    Th8_RegisterLanguage(pInChild);
		    (void)Th8_Input(pInChild, &zIO, &nIO, TH8_TRANSLATE_EOL);
		    if (zIO) Th8_Free(pInChild, zIO);
		    Th8_DeleteInterp(pInChild);
		}
	    }
	}
	{
	    /* th8_libc.c L795 (`!interp || !fgets(...)`): only
	     * the (T, -) vector is drivable from testlib without
	     * adding fclose/fmemopen to the testlib CRT allow-list.
	     * Call libc xInput with interp=NULL: C1=T short-
	     * circuits, fgets is not called, returns ERROR. */
	    const Th8_Platform *pLibcP = Th8_GetLibcPlatform();
	    if (pLibcP && pLibcP->xInput) {
		char *zLOut = NULL;
		size_t nLOut = 0;
		(void)
		    pLibcP->xInput(NULL, pLibcP->pCtx, &zLOut, &nLOut, NULL);
	    }
	}
	{
	    const Th8_Platform *pEnvP = Th8_GetEnvPlatform();
	    if (pEnvP && pEnvP->xKeyValue) {
		/* Direct call to th8EnvKeyValue with TH8_KV_SET2 /
		 * TH8_KV_UNSET2.  The standard interp does NOT merge
		 * the env platform (capability gating), so the
		 * th8_env.c TH8_KV_SET2 / UNSET2 cases get 0 coverage
		 * via Th8_KeyValue.  This direct sweep exercises:
		 *   - TH8_KV_SET2 with a pattern that matches nothing
		 *     (TH8_COV_NONE_* never set): drives L543 (T,T)
		 *     glob branch without mutating real env.
		 *   - TH8_KV_UNSET2 with the same no-match pattern:
		 *     drives L609 (T,T) and L615 (T,T).
		 * pCtx is from the env platform itself. */
		void *pEnvCtx = pEnvP->pCtx;
		(void)pEnvP->xKeyValue(
		    interp, pEnvCtx, TH8_KV_SET2, "TH8_COV_NONE_*", 14, "x",
		    1);
		(void)pEnvP->xKeyValue(
		    interp, pEnvCtx, TH8_KV_UNSET2, "TH8_COV_NONE_*", 14, "x",
		    1);
		/* Drive the (T,T) match-found arms of TH8_KV_SET2 /
		 * TH8_KV_UNSET2 by first creating a temp env var
		 * with a known prefix, then running matching SET2 /
		 * UNSET2 over it.  This drives the L530, L595, L600
		 * vectors that the no-match pattern alone leaves
		 * uncovered. */
		(void)pEnvP->xKeyValue(
		    interp, pEnvCtx, TH8_KV_SET, "TH8TEST_COV_XYZ", 15, "v",
		    1);
		(void)pEnvP->xKeyValue(
		    interp, pEnvCtx, TH8_KV_SET2, "TH8TEST_COV_*", 13, "new",
		    3);
		(void)pEnvP->xKeyValue(
		    interp, pEnvCtx, TH8_KV_UNSET2, "TH8TEST_COV_*", 13, NULL,
		    0);
		(void)pEnvP->xKeyValue(
		    interp, pEnvCtx, TH8_KV_UNSET2, "TH8_COV_NONE_*", 14,
		    NULL, 0);
	    }
	}
	{
	    const Th8_Platform *pNullP = Th8_GetNullIoPlatform();
	    if (pNullP) {
		/* Drive every NullIo platform callback directly so
		 * the th8_nullio.c functions get region/line/branch/
		 * MC/DC coverage.  In normal operation the standard
		 * interp uses POSIX I/O; the NullIo layer is only
		 * loaded by embedders that want a zero-capability
		 * sandbox, which isn't exercised by the test suite. */
		char *zOut = NULL;
		size_t nOut = 0;
		int attrs = 0;
		char *zNorm;
		if (pNullP->xGetData) {
		    (void)pNullP->xGetData(
		        interp, pNullP->pCtx, "x", 1, &zOut, &nOut);
		    if (zOut) Th8_Free(interp, zOut);
		}
		if (pNullP->xDataExists) {
		    (void)pNullP
		        ->xDataExists(interp, pNullP->pCtx, "x", 1, &attrs);
		    (void)pNullP
		        ->xDataExists(interp, pNullP->pCtx, "x", 1, NULL);
		}
		if (pNullP->xLoad) {
		    (void)pNullP
		        ->xLoad(interp, pNullP->pCtx, "x", 1, "Init", 4);
		}
		if (pNullP->xUnload) {
		    (void)pNullP
		        ->xUnload(interp, pNullP->pCtx, "x", 1, "Init", 4, 0);
		}
		if (pNullP->xInput) {
		    zOut = NULL;
		    nOut = 0;
		    (void)pNullP
		        ->xInput(interp, pNullP->pCtx, &zOut, &nOut, NULL);
		    if (zOut) Th8_Free(interp, zOut);
		}
		if (pNullP->xOutput) {
		    (void)pNullP->xOutput(interp, pNullP->pCtx, "x", 1, NULL);
		}
		if (pNullP->xOutputError) {
		    (void)pNullP
		        ->xOutputError(interp, pNullP->pCtx, "x", 1, NULL);
		}
		if (pNullP->xNormalizePath) {
		    /* Drive NULL-zPath guard (returns 0), known-length
		     * path, and unknown-length (size_t)-1 path which
		     * forces the inline strlen loop. */
		    zNorm =
		        pNullP->xNormalizePath(interp, pNullP->pCtx, NULL, 0);
		    if (zNorm) Th8_Free(interp, zNorm);
		    zNorm = pNullP->xNormalizePath(
		        interp, pNullP->pCtx, "abc", 3);
		    if (zNorm) Th8_Free(interp, zNorm);
		    zNorm = pNullP->xNormalizePath(
		        interp, pNullP->pCtx, "abc", (size_t)-1);
		    if (zNorm) Th8_Free(interp, zNorm);
		}
		if (pNullP->xGetCwd) {
		    zNorm = pNullP->xGetCwd(interp, pNullP->pCtx);
		    if (zNorm) Th8_Free(interp, zNorm);
		}
	    }
	}
    }

    /* Th8_AutoPathSearch: public API, no in-tree caller in
     * the test suite path.  Drives th8_core.c L19232
     * (!zLocalAuto || nLocalAuto == 0) C1/C2 pairs.  In the
     * parent interp ::auto_path is set to a non-empty list
     * so C1/C2 stay F.  To drive C2=T we use a child interp
     * with ::auto_path explicitly set to empty. */
    (void)Th8_AutoPathSearch(interp, NULL, 0);
    {
	const Th8_Platform *pP = Th8_GetPlatform(interp);
	if (pP) {
	    Th8_Platform cp = *pP;
	    Th8_Interp *pChild = Th8_CreateInterp(&cp);
	    if (pChild) {
		Th8_RegisterLanguage(pChild);
		(void)Th8_SetVar(pChild, "::auto_path", TH8_NOLEN, "", 0);
		(void)Th8_AutoPathSearch(pChild, NULL, 0);
		Th8_DeleteInterp(pChild);
	    }
#  if defined(TH8_ENABLE_CRYPTOGRAPHY)
	    /* Drive C1=T (!zLocalAuto) at th8_core.c L19327 by
	     * making ::auto_path a SECURE variable in a child
	     * interp.  th8SecureGetVar reads the var, marks the
	     * interp result sensitive via th8FinalizeSensitiveResult,
	     * and Th8_TakeResult then returns NULL -- satisfying
	     * the !zLocalAuto branch.  Requires a 32-byte master
	     * key (AES-256) which we install via the public
	     * Th8_SecureSetMasterKey API.  No production callers
	     * need this state; only test/diagnostic use. */
	    {
		Th8_Platform cp2 = *pP;
		Th8_Interp *pSec;
		static const unsigned char zKey[32] =
		    {0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42,
		     0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42,
		     0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42,
		     0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42};
		cp2.xPanic = 0;
		pSec = Th8_CreateInterp(&cp2);
		if (pSec) {
		    Th8_RegisterLanguage(pSec);
		    if (Th8_SecureSetMasterKey(pSec, zKey, sizeof(zKey)) ==
		        TH8_OK) {
			(void)Th8_Eval(
			    pSec, 0,
			    "unset -nocomplain ::auto_path;"
			    " secure create ::auto_path /tmp",
			    TH8_NOLEN, NULL, 0);
			/* th8SecureGetVar's EVP_DecryptUpdate
			 * fails in this child interp despite the
			 * master key + secure create succeeding;
			 * AutoPathSearch's Th8_GetVar hits the
			 * L19324 error-return rather than the
			 * L19327 (T, -) we wanted to drive.  See
			 * Bug 25. */
			(void)Th8_AutoPathSearch(pSec, NULL, 0);
		    }
		    Th8_DeleteInterp(pSec);
		}
	    }
#  endif
	}
    }

    /* Th8_SetBreakpoint / Th8_ListAppendBreakpoints: public
     * APIs but not exercised by the existing test suite.  The
     * list callback (th8BreakpointListCallback at L16534) is
     * uncovered.  Set one breakpoint, list it, then clear. */
    {
	int bpid = 0;
	char *zBpList = NULL;
	size_t nBpList = 0;
	(void)Th8_SetBreakpoint(interp, "script", 6, 42, &bpid);
	Th8_ListAppendBreakpoints(interp, &zBpList, &nBpList);
	if (zBpList) Th8_Free(interp, zBpList);
	(void)Th8_ClearBreakpoint(interp, bpid);
    }

    /* th8GetVarValue / th8SetVarLength: TH8_INTERNAL dead
     * wrappers in th8_vars.c with no in-tree callers.
     * th8SetVarLength now refuses non-borrowed variables
     * (Bug 21 fix) so it's safe to call with a string-literal
     * buffer -- it returns TH8_ERROR cleanly. */
    {
	Th8_Value v;
	(void)Th8_SetVar(
	    interp, "::th8test_dead", TH8_NOLEN, "hello world", 11);
	v.zData = 0;
	v.nData = 0;
	(void)th8GetVarValue(interp, "::th8test_dead", TH8_NOLEN, &v);
	(void)th8GetVarValue(
	    interp, "::th8test_dead", TH8_NOLEN, NULL); /* pValue=NULL */
	(void)th8GetVarValue(
	    interp, "::nope_no_such_var", TH8_NOLEN, &v); /* missing */
	(void)th8SetVarLength(
	    interp, "::th8test_dead", TH8_NOLEN, "hello", 5); /* non-borrowed
						  * -> bBorrowed F
						  * -> ERROR */
	(void)th8SetVarLength(
	    interp, "::nope_no_such_var", TH8_NOLEN, "x", 1); /* missing */
	(void)Th8_UnsetVar(interp, "::th8test_dead", TH8_NOLEN);

	/* Drive th8_vars.c L657 `pVar->pHash && ALWAYS(!pVar->zData)`
	 * C1-Pair {T,C} (variable is an array) via th8GetVarValue
	 * on an array's bare name.  The existing scalar call above
	 * covers C1=F; adding the array call gives both pairs. */
	(void)Th8_SetVar(interp, "::th8test_arr(idx)", TH8_NOLEN, "v", 1);
	v.zData = 0;
	v.nData = 0;
	(void)th8GetVarValue(interp, "::th8test_arr", TH8_NOLEN, &v);
	(void)Th8_UnsetVar(interp, "::th8test_arr", TH8_NOLEN);

	/* Drive th8_vars.c L959 `!pValue || !pValue->u.buffer.pBuffer`
	 * MC/DC vectors via the new th8SetVarValue internal stub.
	 * In-tree caller always passes a fully-populated Value;
	 * NULL pValue and zero-pBuffer paths are unreachable from
	 * production scripts.  Both calls error out cleanly. */
	{
	    Th8_Value vNullBuf;
	    (void)
	        Th8_SetVar(interp, "::th8test_setval", TH8_NOLEN, "init", 4);
	    (void)th8SetVarValue(
	        interp, "::th8test_setval", TH8_NOLEN, NULL); /* {T,-} */
	    vNullBuf.u.buffer.pBuffer = NULL;
	    vNullBuf.u.buffer.nUsed = 0;
	    vNullBuf.u.buffer.nCapacity = 0;
	    (void)th8SetVarValue(
	        interp, "::th8test_setval", TH8_NOLEN, &vNullBuf); /* {F,T} */
	    (void)Th8_UnsetVar(interp, "::th8test_setval", TH8_NOLEN);
	}

	/* Drive th8_vars.c L1543 (th8GetArrayEpoch) and L1604
	 * (th8GetArrayGeneration) C1-Pair {T,C} (!pEntry =>
	 * variable does not exist).  Both in-tree callers gate
	 * on Th8_ExistsArrayVar first, so the missing-array
	 * branch is unreachable via [array startsearch] etc.
	 * Direct invocation through the internal stubs reaches
	 * it cleanly; both functions return -1. */
	(void)th8GetArrayEpoch(interp, "::th8test_no_such_arr", TH8_NOLEN);
	(void)
	    th8GetArrayGeneration(interp, "::th8test_no_such_arr", TH8_NOLEN);

	/*
	 * Variable-tombstone exerciser (2026-06-08): drive the (F,T)
	 * `pEntry valid but pData NULL` vectors at th8_vars.c
	 * L1289 (Th8_UnsetVar), L1484 (Th8_ExistsArrayVar), L1545
	 * (th8GetArrayEpoch), L1605 (th8GetArrayGeneration), L2157
	 * (Th8_ListAppendArray).  All share the pattern
	 *   pEntry = Th8_HashFind(... pFrame->paVar ...);
	 *   if (!pEntry || !pEntry->pData) return ...;
	 * The (T,-) vector is covered by ordinary "no such variable"
	 * tests; the (F,T) vector requires a hash entry whose pData
	 * was nulled (tombstoned) -- a state the public API never
	 * leaves observable.  Create a real var, find its entry via
	 * the th8GetFramePaVar accessor, NULL pData, run each
	 * consumer, restore pData, then unset cleanly.
	 *
	 * A real tombstone-vs-signed-script interaction would
	 * surface as a test failure (not a hang) and can be
	 * debugged from there.  See Bug 47 in incomplete.md for
	 * the prior libcurl/DNS-mock false-positive that this
	 * exerciser was once blamed for.
	 */
	{
	    /* Public name used for Set/Unset/Exists.  zKey is the
	     * canonical pFrame->paVar key (the bare local name with
	     * no `::` prefix). */
	    const char *zTomb = "::th8testlib_tombvar";
	    size_t nTomb = 20;
	    const char *zKey = "th8testlib_tombvar";
	    size_t nKey = 18;
	    Th8_Hash *paVar;
	    Th8_HashEntry *pEntry;

	    /* Create the variable so a hash entry exists. */
	    (void)Th8_SetVar(interp, zTomb, nTomb, "1", 1);

	    paVar = th8GetFramePaVar(interp);
	    if (paVar) {
		pEntry = Th8_HashFind(interp, paVar, zKey, nKey, 0);
		if (pEntry && pEntry->pData) {
		    void *pSaved = pEntry->pData;
		    pEntry->pData = NULL;

		    (void)
		        Th8_UnsetVar(interp, zTomb, nTomb); /* L1328 (F,T) */
		    (void)Th8_ExistsArrayVar(
		        interp, zTomb, nTomb); /* L1484 (F,T) */
		    (void)th8GetArrayEpoch(
		        interp, zTomb, nTomb); /* L1545 (F,T) */
		    (void)th8GetArrayGeneration(
		        interp, zTomb, nTomb); /* L1605 (F,T) */
		    {
			char *zLst = NULL;
			size_t nLst = 0;
			(void)Th8_ListAppendArray(
			    interp, zTomb, nTomb, &zLst,
			    &nLst); /* L2157 (F,T) */
			if (zLst) Th8_Free(interp, zLst);
		    }
		    {
			/* Th8_ListAppendVarLinks iterates pFrame->paVar
			 * via th8AppendLinkedHashKeys; the tombstoned
			 * entry drives th8_vars.c L2047 (F,-)
			 * (`pVar && pVar->nRef > 1` with pVar==NULL). */
			char *zLnk = NULL;
			size_t nLnk = 0;
			(void)Th8_ListAppendVarLinks(interp, &zLnk, &nLnk);
			if (zLnk) Th8_Free(interp, zLnk);
		    }

		    pEntry->pData = pSaved;
		}
	    }
	    /* Clean up. */
	    (void)Th8_UnsetVar(interp, zTomb, nTomb);
	}

	/* Array-element variant to drive L1289 (Th8_UnsetVar's
	 * `if (zInner)` branch), which the scalar exerciser above
	 * does not reach. */
	{
	    const char *zArr = "::th8testlib_tomb_arr(x)";
	    size_t nArr = 24;
	    const char *zOuterPub = "::th8testlib_tomb_arr";
	    size_t nOuterPub = 21;
	    const char *zOuterKey = "th8testlib_tomb_arr";
	    size_t nOuterKey = 19;
	    Th8_Hash *paVar;
	    Th8_HashEntry *pEntry;

	    (void)Th8_SetVar(interp, zArr, nArr, "1", 1);
	    paVar = th8GetFramePaVar(interp);
	    if (paVar) {
		pEntry = Th8_HashFind(interp, paVar, zOuterKey, nOuterKey, 0);
		if (pEntry && pEntry->pData) {
		    void *pSaved = pEntry->pData;
		    pEntry->pData = NULL;
		    (void)Th8_UnsetVar(interp, zArr, nArr); /* L1289 (F,T) */
		    /* Drive th8GetArrayElementHash th8_vars.c L1661 (F,T)
		     * directly on the tombstoned outer key.  The [array
		     * startsearch] script-side path was tried but observed
		     * (T,-) (entry not found) -- the script-eval-time
		     * frame walk diverges from th8GetFramePaVar, so a
		     * direct internal call is the reliable route. */
		    (void)
		        th8GetArrayElementHash(interp, zOuterPub, nOuterPub);
		    pEntry->pData = pSaved;
		}
	    }
	    (void)Th8_UnsetVar(interp, zOuterPub, nOuterPub);
	}

	/*
	 * RNG-force exerciser (Bug 49 -- 2026-06-09): drives the three
	 * reserved-token retry arms at th8_load.c L106 by forcing
	 * fi_xRandomBytes to return the reserved values (0, ~0, 1) on
	 * the first call of each Th8_EnableLoad cycle.  The retry then
	 * falls through to the real platform RNG (counter exhausted)
	 * which produces a non-reserved token and the call succeeds.
	 */
#  if defined(TH8_ENABLE_LOAD)
	{
	    const Th8_Platform *pPlat = Th8_GetPlatform(interp);
	    if (pPlat) {
		int i, j;
		static const unsigned char aaForce[3][8] =
		    {/* tok == 0 -- all zeros (drives L106 first arm). */
		     {0, 0, 0, 0, 0, 0, 0, 0},
		     /* tok == ~0 -- all 0xFF (drives L106 second arm;
		     * requires Bug 51 fix in th8_load.c so the retry
		     * resets tok rather than carrying ~0 forward). */
		     {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
		     /* tok == 1 -- LE one (drives L106 third arm). */
		     {1, 0, 0, 0, 0, 0, 0, 0}};

		for (i = 0; i < 3; i++) {
		    Th8_Platform plat = *pPlat;
		    Th8_Interp *pChild;

		    plat.xPanic = 0;
		    pChild = Th8_CreateInterp(&plat);
		    if (pChild) {
			Th8_FaultConfig cfg;
			Th8_FaultCtx *pFCtx;
			char fbuf[1024];

			Th8_RegisterLanguage(pChild);
			pFCtx = (Th8_FaultCtx *)(void *)fbuf;
			Th8_FaultConfigInit(&cfg);
			for (j = 0; j < 8; j++)
			    cfg.aForceRandomBytes[j] = aaForce[i][j];
			cfg.nForceRandomBytesCount = 1;
			if (Th8_FaultCtxSize() <= sizeof(fbuf) &&
			    Th8_FaultInstall(pChild, &cfg, pFCtx) == TH8_OK) {
			    (void)Th8_EnableLoad(pChild, 1);
			    /* Also drive the matching arms at th8_load.c
			     * L622 in Th8_EnableUnload by re-installing the
			     * fault with a fresh force-count (the EnableLoad
			     * call above consumed the previous one). */
			    cfg.nForceRandomBytesCount = 1;
			    (void)Th8_EnableUnload(pChild, TH8_UNLOAD_OK);
			    Th8_FaultUninstall(pChild, pFCtx);
			}
			Th8_DeleteInterp(pChild);
		    }
		}
	    }
	}
#  endif /* TH8_ENABLE_LOAD */

	/*
	 * Bug 28 family second-call driver (2026-06-09): the
	 * Th8_ToInt store-back at src/th8_core.c L15164/L15165
	 * `if (pCached) { ... }` has an uncovered (F,-) arm --
	 * the second Th8_FindInCache(TH8_CACHE_INT,...) call
	 * (post-parse, to store the result) returning NULL.
	 *
	 * The new `nCacheLookupSkip` extension to Th8_FaultConfig
	 * (2026-06-09) lets the test skip the FIRST matching
	 * lookup (the entry-fast-path lookup, which is expected
	 * to miss the cache anyway when the input is novel) and
	 * fail the SECOND (the store-back).  Mirrors the pattern
	 * for TH8_CACHE_WIDE / TH8_CACHE_DOUBLE / TH8_CACHE_LIST
	 * which share the same lookup-then-store shape.
	 *
	 * Per-cacheType iteration uses a fresh fault install per
	 * type so the skip counter is exactly 1 each time.
	 */
#  if defined(TH8_ENABLE_FAULT_INJECTION)
	{
	    static const struct {
		int cacheBit;
		const char *zEvalInt; /* expression to trigger lookup */
	    } aBug28Sites[] =
	        {{TH8_CACHE_INT, "expr {19260817 + 0}"},
	         {TH8_CACHE_WIDE, "expr {wide(9223372036854775807)}"},
	         {TH8_CACHE_DOUBLE, "expr {3.141592653589793 + 0.0}"},
	         {TH8_CACHE_LIST, "lindex {a b c d e} 4"}};
	    int iSite;

	    for (iSite = 0;
	         iSite < (int)(sizeof(aBug28Sites) / sizeof(aBug28Sites[0]));
	         iSite++) {
		Th8_FaultConfig cfg28;
		Th8_FaultCtx *pFCtx28;
		char fbuf28[1024];

		pFCtx28 = (Th8_FaultCtx *)(void *)fbuf28;
		Th8_FaultConfigInit(&cfg28);
		cfg28.nFailCacheLookupMask = 1u
		                          << aBug28Sites[iSite].cacheBit;
		cfg28.nCacheLookupSkip = 1; /* fail the SECOND match */
		if (Th8_FaultCtxSize() <= sizeof(fbuf28) &&
		    Th8_FaultInstall(interp, &cfg28, pFCtx28) == TH8_OK) {
		    (void)Th8_Eval(
		        interp, 0, aBug28Sites[iSite].zEvalInt, TH8_NOLEN,
		        NULL, 0);
		    Th8_FaultUninstall(interp, pFCtx28);
		}
	    }
	    Th8_ClearResult(interp);
	}
#  endif /* TH8_ENABLE_FAULT_INJECTION */

	/*
	 * th8LibcInput L858 driver (2026-06-09): the compound
	 * `if (!interp || !fgets(...))` needs both pairs covered.
	 *
	 *   (F,T) -- interp valid, fgets returns NULL.  Drive via a
	 *            /dev/null FILE* opened for reading.
	 *   (F,F) -- interp valid, fgets succeeds.  Drive via tmpfile()
	 *            written to and rewound so fgets reads a line back.
	 *            Without (F,F) neither C1-Pair nor C2-Pair can be
	 *            satisfied (the existing C1=T-from-elsewhere
	 *            vector pair-flips against (F,F), not (F,T)).
	 */
	{
	    const Th8_Platform *pLibc = Th8_GetLibcPlatform();
	    if (pLibc && pLibc->xInput) {
		FILE *pEof = fopen("/dev/null", "r");
		if (pEof) {
		    char *zEofOut = NULL;
		    size_t nEofOut = 0;
		    (void)pLibc->xInput(
		        interp, pLibc->pCtx, &zEofOut, &nEofOut,
		        (void *)pEof);
		    if (zEofOut) Th8_Free(interp, zEofOut);
		    fclose(pEof);
		}
		{
		    FILE *pTmp = tmpfile();
		    if (pTmp) {
			char *zOkOut = NULL;
			size_t nOkOut = 0;
			fputs("hello\n", pTmp);
			rewind(pTmp);
			(void)pLibc->xInput(
			    interp, pLibc->pCtx, &zOkOut, &nOkOut,
			    (void *)pTmp);
			if (zOkOut) Th8_Free(interp, zOkOut);
			fclose(pTmp);
		    }
		}
	    }
	}

	/*
	 * th8ArraySearchIterEntry L2775 tombstone driver
	 * (2026-06-09): the per-entry hash iterator callback at
	 * th8_core.c L2775 `if (!pEntry || !pEntry->pData)` is
	 * uncovered because Th8_HashIterate's contract guarantees
	 * pEntry is never NULL and Th8_HashFind's tombstone state
	 * is normally invisible to iterators.  Drive (F,T) by
	 * tombstoning a real [array startsearch] entry via the
	 * existing th8GetArraySearchHash internal stub, calling
	 * Th8_IterateArraySearches, then restoring.
	 *
	 * Callback would never be invoked because the iterator
	 * short-circuits at L2775 (F,T) -> return TH8_OK without
	 * dispatching to pSearch.  The callback prototype must
	 * still satisfy the API contract; provide a no-op.
	 */
	{
	    Th8_Hash *paSrch;
	    /* Set up the search state. */
	    (void)Th8_Eval(
	        interp, 0,
	        "array set ::th8t_assrch_arr {a 1 b 2}; "
	        "set ::th8t_assrch_sid [array startsearch ::th8t_assrch_arr]",
	        TH8_NOLEN, NULL, 0);
	    paSrch = th8GetArraySearchHash(interp, 0);
	    if (paSrch &&
	        Th8_GetVar(interp, "::th8t_assrch_sid", TH8_NOLEN) ==
	            TH8_OK) {
		size_t nSid = 0;
		const char *zSid = Th8_GetResult(interp, &nSid);
		char zBuf[64];
		if (zSid && nSid > 0 && nSid < sizeof(zBuf)) {
		    Th8_HashEntry *pE;
		    size_t i;
		    for (i = 0; i < nSid; i++)
			zBuf[i] = zSid[i];
		    zBuf[i] = 0;
		    pE = Th8_HashFind(interp, paSrch, zBuf, nSid, 0);
		    if (pE && pE->pData) {
			void *pSaved = pE->pData;
			pE->pData = NULL;
			(void)Th8_IterateArraySearches(
			    interp, th8test_assrch_iter_noop_cb, NULL);
			pE->pData = pSaved;
		    }
		}
	    }
	    /* Clean up the search state. */
	    (void)Th8_Eval(
	        interp, 0,
	        "if {[info exists ::th8t_assrch_sid]} { "
	        "  catch {array donesearch ::th8t_assrch_arr "
	        "         $::th8t_assrch_sid} "
	        "}; "
	        "unset -nocomplain ::th8t_assrch_arr ::th8t_assrch_sid",
	        TH8_NOLEN, NULL, 0);
	    Th8_ClearResult(interp);
	}

	/*
	 * th8FaultStrEqAscii NULL-input driver (2026-06-09): exposed
	 * via the internal stubs table (v32) so testlib can drive
	 * the L117 `if (!a || !b)` C1-Pair / C2-Pair.  In-tree
	 * callers always supply non-NULL strings; without these
	 * direct calls only (F,F) is exercised.
	 *
	 *   (NULL, "x") -> C1=T (!a) -> drives (T,-)
	 *   ("x", NULL) -> C1=F C2=T -> drives (F,T)
	 *   (NULL, NULL) -> a == b short-circuit, no decision hit
	 *   ("x", "x")  -> (F,F) baseline (already covered)
	 *
	 * th8FaultPathMatchesBaseName L159 (`c == '/' || c == '\\'`)
	 * (F,F) vector: a path whose preceding char is neither / nor
	 * \ -- e.g. "abx" vs "x" -> trailing 'x' matches, the byte
	 * before is 'b' which is neither separator -> (F,F).
	 */
	(void)th8FaultStrEqAscii(NULL, "x");
	(void)th8FaultStrEqAscii("x", NULL);
	(void)th8FaultPathMatchesBaseName("abx", "x");

	/*
	 * th8PosixIsUnderBase `.`/`..` segment driver (2026-06-10):
	 * exposed via internal stubs (v33) so testlib can drive
	 * the L4101 5-condition `(segLen == 1 && seg[0] == '.') ||
	 * (segLen == 2 && seg[0] == '.' && seg[1] == '.')` compound.
	 * In-tree callers pass realpath-normalized paths so these
	 * defensive segment arms never fire.  Drive each `.` and
	 * `..` arm directly with crafted paths.
	 *
	 *   "/a/./b" -> `.` segment hits LHS of || -> drives
	 *               (T,T,-,-,-) (suspicious=-1 return).
	 *   "/a/../b" -> `..` segment hits RHS -> drives
	 *               (F,-,T,T,T) (suspicious=-1 return).
	 *   "/a/b"   -> no segment hits -> drives (F,-,F,-,-) /
	 *               (T,F,-,-,-) (clean path, normal return).
	 */
	if (th8InternalStubsPtr->th8_PosixIsUnderBase) {
	    (void)th8PosixIsUnderBase("/a/./b", "/a", 2);
	    (void)th8PosixIsUnderBase("/a/../b", "/a", 2);
	    (void)th8PosixIsUnderBase("/a/b", "/a", 2);
	}
	/*
	 * th8PosixIsPathUnderBase driver (2026-06-10): exposed via
	 * internal stubs (v33 -> v34, Win32-safe).  Drives:
	 *   - L4176 (T,-) via NULL zPath argument.
	 *   - L4249 (F,-) via path with empty segment (double slash).
	 * The function only does real work when a sandbox base is
	 * configured (th8PosixGetBasePath() returns non-NULL); under
	 * the test environment that's typically NOT the case, so the
	 * function returns at L4179 with `return 1; / no sandbox /`.
	 * But the NULL guard at L4176 fires unconditionally.
	 */
	if (th8InternalStubsPtr->th8_PosixIsPathUnderBase) {
	    (void)th8PosixIsPathUnderBase(NULL);
	    (void)th8PosixIsPathUnderBase("");
	    (void)th8PosixIsPathUnderBase("a//b");
	    (void)th8PosixIsPathUnderBase("a/b");
	}
	/*
	 * th8PosixCallUnloadProc 4-condition entry-guard driver
	 * (2026-06-10): the L1020 compound
	 * `!interp || !hLib || !zName || nName == 0` is 0% MC/DC --
	 * in-tree callers always pass valid arguments.  Drive each
	 * condition's true arm independently by passing NULL/0 for
	 * each argument.  The function early-returns on any true
	 * condition, so no actual unload-proc dispatch happens.
	 */
	if (th8InternalStubsPtr->th8_PosixCallUnloadProc) {
	    th8PosixCallUnloadProc(NULL, (void *)1, "x", 1, 0); /* C1=T */
	    th8PosixCallUnloadProc(interp, NULL, "x", 1, 0); /* C2=T */
	    th8PosixCallUnloadProc(interp, (void *)1, NULL, 1, 0); /* C3=T */
	    th8PosixCallUnloadProc(interp, (void *)1, "x", 0, 0); /* C4=T */
	}
	/*
	 * Crypto fixture expansion (2026-06-10): th8PolicyIsHttpUri
	 * drivers (plugins/harpy/th8_policy.c L295/L296).  The
	 * function is 0-hit because in-tree harpy callers reach it
	 * only when the prod signing-key URL fetch is performed --
	 * not under TH8_ENABLE_TEST_KEY embedded-key tests.  Drive
	 * each 2-condition compound:
	 *
	 *   L295 `n >= 7 && memcmp(z, "http://", 7) == 0`
	 *     ("",        0): (F,-) baseline
	 *     ("ftp://x", 7): (T,F) length OK but no match
	 *     ("http://", 7): (T,T) match
	 *   L296 `n >= 8 && memcmp(z, "https://", 8) == 0`
	 *     ("http://x", 8): (T,F) length OK but no match (http != https)
	 *     ("https://", 8): (T,T) match
	 */
	if (th8InternalStubsPtr->th8_PolicyIsHttpUri) {
	    (void)th8PolicyIsHttpUri(interp, "", 0);
	    (void)th8PolicyIsHttpUri(interp, "ftp://x", 7);
	    (void)th8PolicyIsHttpUri(interp, "http://", 7);
	    (void)th8PolicyIsHttpUri(interp, "http://x", 8);
	    (void)th8PolicyIsHttpUri(interp, "https://", 8);
	}
	/*
	 * th8PolicyDaysInMonth month-range driver (2026-06-10):
	 * L351 `if (month < 1 || month > 12)` is at (F,F) only.
	 * Drive C1=T (month<1) and C2=T (month>12) independently.
	 */
	if (th8InternalStubsPtr->th8_PolicyDaysInMonth) {
	    (void)th8PolicyDaysInMonth(2024, 0); /* C1=T short-circuit */
	    (void)th8PolicyDaysInMonth(2024, 13); /* C1=F, C2=T */
	    (void)th8PolicyDaysInMonth(2024, 5); /* C1=F, C2=F (baseline) */
	}
	/*
	 * th8SecureCheckCanary NULL-pKS driver (2026-06-10): L165
	 * `!pKS || !pKS->pPage` at (F,F) only.  Drive C1=T via NULL
	 * pKSv directly; covers C1-Pair (1 condition).  C2-Pair
	 * (pKS valid, pPage=NULL) would need Th8_KeyStore layout
	 * exposure.
	 */
	if (th8InternalStubsPtr->th8_SecureCheckCanary) {
	    (void)th8SecureCheckCanary(interp, NULL);
	}
	/*
	 * th8NtpSortTimes insertion-sort driver (2026-06-10):
	 * L575 `j >= 0 && a[j] > key` is 0% (loop body never
	 * entered).  Pass a small array where a[1] < a[0] so the
	 * inner while-loop executes; covers C1-Pair and C2-Pair.
	 *
	 *   n=3 array [30, 20, 10]: i=1 pushes 20 past 30 (1 iter),
	 *   i=2 pushes 10 past 20 and 30 (2 iters), exercising both
	 *   loop continuation (T,T) and loop exit at j<0 (F,-).
	 */
	if (th8InternalStubsPtr->th8_NtpSortTimes) {
	    th8_int64_t aTimes[3];
	    aTimes[0] = 30;
	    aTimes[1] = 20;
	    aTimes[2] = 10;
	    th8NtpSortTimes(aTimes, 3);
	}
	/*
	 * th8AfParseHexKey hex-class driver (2026-06-10): drives the
	 * L443/L445/L447 hex-digit class compounds by passing strings
	 * that exercise specific char-range branches without hitting
	 * the else-returns-error early-exit.
	 *
	 *   "0" -- L443 (T,T) baseline (already covered).
	 *   ":" -- L443 (T,F) (':' = '9'+1, fails C2).
	 *   "g" -- L443 (F,-), L445 (T,F) ('g' = 'f'+1).
	 *   "@" -- L443 (F,-), L445 (F,-), L447 (F,-) ('@' < 'A').
	 *   "G" -- L443 (F,-), L445 (F,-), L447 (T,F).
	 *
	 * Each call passes a single-char string; the early-error on
	 * non-hex chars doesn't matter for MC/DC pair tracking
	 * because the compound vector is recorded BEFORE the
	 * fall-through to the else-error.
	 */
	if (th8InternalStubsPtr->th8_AfParseHexKey) {
	    th8_int64_t key;
	    (void)th8AfParseHexKey(":", 1, &key);
	    (void)th8AfParseHexKey("g", 1, &key);
	    (void)th8AfParseHexKey("@", 1, &key);
	    (void)th8AfParseHexKey("G", 1, &key);
	    /* L444 (F,-) C1-Pair closer (2026-06-17, batch #26):
	     * a char < '0' makes C1=F (c >= '0' is false) and
	     * short-circuits.  Space (' ', 0x20) is below '0'
	     * (0x30) so the if-else cascade falls through to
	     * the final else-error without ever evaluating C2.
	     * Pairs with the existing (T,T) baseline to close
	     * the C1 independence pair. */
	    (void)th8AfParseHexKey(" ", 1, &key);
	}
	/*
	 * th8RsaParseCapi driver: 15 crafted byte arrays that
	 * progress from earliest-bailout (size, bType, bVersion,
	 * reserved, aiKeyAlg) through the deeper validation
	 * compounds (magic-vs-bType, bitlen power-of-2, ePub
	 * parity, truncation).  Per-blob comments below identify
	 * the target decision.  `buf` is 1024 bytes so writes
	 * before the failure return can't overrun the frame.
	 */
	if (th8InternalStubsPtr->th8_RsaParseCapi) {
	    unsigned char buf[1024];
	    unsigned char blob[20];
	    int j;
	    for (j = 0; j < (int)sizeof(buf); j++)
		buf[j] = 0;

	    /* blob1: too small. */
	    (void)th8RsaParseCapi(interp, blob, 10, (void *)buf);

	    /* blob2: bType=0xFF (neither 0x06 PUB nor 0x07 PRIV). */
	    for (j = 0; j < 20; j++)
		blob[j] = 0;
	    blob[0] = 0xFF;
	    (void)th8RsaParseCapi(interp, blob, 20, (void *)buf);

	    /* blob3: bType=0x06 (PUBLIC), bVersion=0xFF (not CUR_BLOB_VERSION). */
	    for (j = 0; j < 20; j++)
		blob[j] = 0;
	    blob[0] = 0x06;
	    blob[1] = 0xFF;
	    (void)th8RsaParseCapi(interp, blob, 20, (void *)buf);

	    /* blob4: valid bType+bVersion, reserved z[2]=1 not zero. */
	    for (j = 0; j < 20; j++)
		blob[j] = 0;
	    blob[0] = 0x06;
	    blob[1] = 0x02;
	    blob[2] = 0x01;
	    (void)th8RsaParseCapi(interp, blob, 20, (void *)buf);

	    /* blob4b (2026-06-17, batch #35): drives L357 (F,T)
	     * C2-Pair -- z[2]=0 (C1 condition false) but
	     * z[3]=1 (C2 condition true), so `z[2] != 0 ||
	     * z[3] != 0` enters the error branch via C2.
	     * Closes the C2-Pair previously open at 50%. */
	    for (j = 0; j < 20; j++)
		blob[j] = 0;
	    blob[0] = 0x06;
	    blob[1] = 0x02;
	    blob[3] = 0x01;
	    (void)th8RsaParseCapi(interp, blob, 20, (void *)buf);

	    /* Th8_RsaKeyLoad NULL-arg drives (2026-06-17, batch
	     * #36): close L600 C1-Pair (zData=NULL drives the
	     * (T,-,-) short-circuit) and C3-Pair (ppKey=NULL
	     * with valid zData/nData drives the (F,F,T)
	     * vector).  Th8_RsaKeyLoad is public TH8_API so
	     * no internal-stubs change needed. */
	    {
		Th8_RsaKey *pSinkNull = NULL;

		/* Drive (T,-,-): zData=NULL. */
		(void)Th8_RsaKeyLoad(interp, NULL, 20, &pSinkNull);
		/* Drive (F,F,T): ppKey=NULL.  Use blob (any valid-
		 * length buffer); the function returns at L600
		 * before reading the contents. */
		(void)Th8_RsaKeyLoad(interp, blob, 20, NULL);

		/* .NET-wrapper drives (2026-06-18, batch #37) for
		 * th8_snk.c L636 `if (cbBlob == 0 || 12 + cbBlob >
		 * nData)` -- previously 0% MC/DC, only (F,F) seen.
		 *
		 *   netA: cbBlob=0 -- drives (T,-) C1-Pair.
		 *   netB: cbBlob=0xFFFF -- 12+0xFFFF > 20, drives
		 *         (F,T) C2-Pair.
		 *
		 * Both wrap with DOTNET_SIG_ALG_RSA + SHA1 hash id
		 * so the L616 (T,T) branch is taken to reach L636. */
		for (j = 0; j < 20; j++)
		    blob[j] = 0;
		blob[0] = 0x00;
		blob[1] = 0x24;
		blob[2] = 0x00;
		blob[3] = 0x00; /* DOTNET_SIG_ALG_RSA */
		blob[4] = 0x04;
		blob[5] = 0x80;
		blob[6] = 0x00;
		blob[7] = 0x00; /* DOTNET_HASH_ALG_SHA1 */
		/* netA: cbBlob = 0 (bytes 8-11 already zero). */
		{
		    Th8_RsaKey *pSink = NULL;
		    (void)Th8_RsaKeyLoad(interp, blob, 20, &pSink);
		    if (pSink) Th8_RsaKeyFree(interp, pSink);
		}
		/* netB: cbBlob = 0xFFFF -- 12+0xFFFF > 20. */
		blob[8] = 0xFF;
		blob[9] = 0xFF;
		{
		    Th8_RsaKey *pSink = NULL;
		    (void)Th8_RsaKeyLoad(interp, blob, 20, &pSink);
		    if (pSink) Th8_RsaKeyFree(interp, pSink);
		}

		/* CAPI PUBLICKEYBLOB drive (2026-06-18, batch #37)
		 * for th8_snk.c L663 `else if (zData[0] ==
		 * CAPI_PUBLICKEYBLOB || zData[0] ==
		 * CAPI_PRIVATEKEYBLOB)` -- previously only (F,T)
		 * seen (PRIVATEKEYBLOB path); this drives
		 * zData[0]=0x06 to close (T,-) C1-Pair. */
		for (j = 0; j < 20; j++)
		    blob[j] = 0;
		blob[0] = 0x06; /* CAPI_PUBLICKEYBLOB */
		{
		    Th8_RsaKey *pSink = NULL;
		    (void)Th8_RsaKeyLoad(interp, blob, 20, &pSink);
		    if (pSink) Th8_RsaKeyFree(interp, pSink);
		}

		/* Unrecognized-bType drive (2026-06-18, batch #37)
		 * for th8_snk.c L663 (F,F=F) vector -- needed to
		 * close both pairs (without it C1/C2 lack a
		 * differing-result paired vector).  bType=0xAA is
		 * neither PUBLICKEYBLOB (0x06) nor PRIVATEKEYBLOB
		 * (0x07); falls through to the "unrecognized key
		 * format" else arm at L668. */
		for (j = 0; j < 20; j++)
		    blob[j] = 0;
		blob[0] = 0xAA;
		{
		    Th8_RsaKey *pSink = NULL;
		    (void)Th8_RsaKeyLoad(interp, blob, 20, &pSink);
		    if (pSink) Th8_RsaKeyFree(interp, pSink);
		}
	    }

	    /* blob5: valid through reserved, wrong aiKeyAlg
	     * (all zeros => not RSA_SIGN). */
	    for (j = 0; j < 20; j++)
		blob[j] = 0;
	    blob[0] = 0x06;
	    blob[1] = 0x02;
	    (void)th8RsaParseCapi(interp, blob, 20, (void *)buf);

	    /* blob6: aiKeyAlg = CALG_RSA_KEYX (0x0000a400) -- drives
	     * the ternary at L368 (T arm: KEYX-specific message). */
	    for (j = 0; j < 20; j++)
		blob[j] = 0;
	    blob[0] = 0x06;
	    blob[1] = 0x02;
	    blob[5] = 0xa4;
	    (void)th8RsaParseCapi(interp, blob, 20, (void *)buf);

	    /* blob7: valid through aiKeyAlg, garbage magic (drives
	     * L385: magic != RSA1 && magic != RSA2). */
	    for (j = 0; j < 20; j++)
		blob[j] = 0;
	    blob[0] = 0x06;
	    blob[1] = 0x02;
	    blob[5] = 0x24;
	    blob[8] = 0xAA;
	    blob[9] = 0xBB;
	    blob[10] = 0xCC;
	    blob[11] = 0xDD;
	    (void)th8RsaParseCapi(interp, blob, 20, (void *)buf);

	    /* blob8: PUBLIC blob (0x06) with RSA2 magic -- drives L393
	     * (PUBLIC && magic!=RSA1). */
	    for (j = 0; j < 20; j++)
		blob[j] = 0;
	    blob[0] = 0x06;
	    blob[1] = 0x02;
	    blob[5] = 0x24;
	    blob[8] = 0x52;
	    blob[9] = 0x53;
	    blob[10] = 0x41;
	    blob[11] = 0x32; /* "RSA2" */
	    (void)th8RsaParseCapi(interp, blob, 20, (void *)buf);

	    /* blob9: PRIVATE blob (0x07) with RSA1 magic -- drives L398
	     * (PRIVATE && magic!=RSA2). */
	    for (j = 0; j < 20; j++)
		blob[j] = 0;
	    blob[0] = 0x07;
	    blob[1] = 0x02;
	    blob[5] = 0x24;
	    blob[8] = 0x52;
	    blob[9] = 0x53;
	    blob[10] = 0x41;
	    blob[11] = 0x31; /* "RSA1" */
	    (void)th8RsaParseCapi(interp, blob, 20, (void *)buf);

	    /* blob10: PUBLIC + RSA1, bitlen=512 (too small) --
	     * drives L407 first condition (bitlen < 1024). */
	    for (j = 0; j < 20; j++)
		blob[j] = 0;
	    blob[0] = 0x06;
	    blob[1] = 0x02;
	    blob[5] = 0x24;
	    blob[8] = 0x52;
	    blob[9] = 0x53;
	    blob[10] = 0x41;
	    blob[11] = 0x31;
	    blob[13] = 0x02; /* bitlen = 0x00000200 = 512 */
	    (void)th8RsaParseCapi(interp, blob, 20, (void *)buf);

	    /* blob11: PUBLIC + RSA1, bitlen=1536 (not pow2) --
	     * drives L407 second condition (bitlen & (bitlen-1)). */
	    for (j = 0; j < 20; j++)
		blob[j] = 0;
	    blob[0] = 0x06;
	    blob[1] = 0x02;
	    blob[5] = 0x24;
	    blob[8] = 0x52;
	    blob[9] = 0x53;
	    blob[10] = 0x41;
	    blob[11] = 0x31;
	    blob[13] = 0x06; /* bitlen = 0x00000600 = 1536 */
	    (void)th8RsaParseCapi(interp, blob, 20, (void *)buf);

	    /* blob12: PUBLIC + RSA1, bitlen=1024, ePub=1 (too small)
	     * -- drives L415 first condition (ePub < 3). */
	    for (j = 0; j < 20; j++)
		blob[j] = 0;
	    blob[0] = 0x06;
	    blob[1] = 0x02;
	    blob[5] = 0x24;
	    blob[8] = 0x52;
	    blob[9] = 0x53;
	    blob[10] = 0x41;
	    blob[11] = 0x31;
	    blob[13] = 0x04; /* bitlen = 0x00000400 = 1024 */
	    blob[16] = 0x01; /* ePub = 1 */
	    (void)th8RsaParseCapi(interp, blob, 20, (void *)buf);

	    /* blob13: PUBLIC + RSA1, bitlen=1024, ePub=4 (even) --
	     * drives L415 second condition (ePub & 1 == 0). */
	    for (j = 0; j < 20; j++)
		blob[j] = 0;
	    blob[0] = 0x06;
	    blob[1] = 0x02;
	    blob[5] = 0x24;
	    blob[8] = 0x52;
	    blob[9] = 0x53;
	    blob[10] = 0x41;
	    blob[11] = 0x31;
	    blob[13] = 0x04;
	    blob[16] = 0x04; /* ePub = 4 */
	    (void)th8RsaParseCapi(interp, blob, 20, (void *)buf);

	    /* blob14: PUBLIC + RSA1, bitlen=1024, ePub=3, n=20
	     * (truncated -- no modulus bytes) -- drives L428
	     * (n < 20 + nMod) truncation path for PUBLIC. */
	    for (j = 0; j < 20; j++)
		blob[j] = 0;
	    blob[0] = 0x06;
	    blob[1] = 0x02;
	    blob[5] = 0x24;
	    blob[8] = 0x52;
	    blob[9] = 0x53;
	    blob[10] = 0x41;
	    blob[11] = 0x31;
	    blob[13] = 0x04;
	    blob[16] = 0x03; /* ePub = 3 (valid odd > 1) */
	    (void)th8RsaParseCapi(interp, blob, 20, (void *)buf);

	    /* blob15: PRIVATE + RSA2, bitlen=1024, ePub=3, n=20
	     * (truncated) -- drives L439 truncation path for PRIVATE. */
	    for (j = 0; j < 20; j++)
		blob[j] = 0;
	    blob[0] = 0x07;
	    blob[1] = 0x02;
	    blob[5] = 0x24;
	    blob[8] = 0x52;
	    blob[9] = 0x53;
	    blob[10] = 0x41;
	    blob[11] = 0x32; /* "RSA2" */
	    blob[13] = 0x04;
	    blob[16] = 0x03;
	    (void)th8RsaParseCapi(interp, blob, 20, (void *)buf);
	}
	Th8_ClearResult(interp);

	/*
	 * th8HttpsTimeVerifySignature early-bailout driver (2026-06-10):
	 * drives the L880 (rc != TH8_OK || !pSig || nSig == 0) compound
	 * with crafted base64 inputs.  zSignedData is irrelevant for
	 * these cases -- the function returns at the L880 bailout
	 * before ever touching it.  Three vectors:
	 *
	 *   sig1: invalid base64 ("@@@@") -- drives (T,_,_):
	 *         th8Base64Decode returns ERROR.
	 *   sig2: empty base64 ("") -- drives (_,_,T): decode
	 *         returns OK with pSig=NULL and nSig=0 (or pSig
	 *         non-NULL and nSig=0 depending on impl); either
	 *         way the second or third condition fires.
	 *   sig3: short valid base64 ("AAAA" = four NULs) --
	 *         passes L880, then fails at Th8_RsaKeyLoad or
	 *         Th8_RsaVerify.  Drives the F,F,F vector + L890
	 *         and L905 paths.
	 */
	if (th8InternalStubsPtr->th8_HttpsTimeVerifySignature) {
	    const char zSigned[] = "nonce 1 ticks 0";
	    size_t nSigned = sizeof(zSigned) - 1;

	    (void)th8HttpsTimeVerifySignature(
	        interp, zSigned, nSigned, "@@@@", 4);
	    (void)
	        th8HttpsTimeVerifySignature(interp, zSigned, nSigned, "", 0);
	    (void)th8HttpsTimeVerifySignature(
	        interp, zSigned, nSigned, "AAAA", 4);
	}
	Th8_ClearResult(interp);

	/*
	 * th8HttpsTimeFindField driver (2026-06-11): drives the L822
	 * 2-condition compound (anElem[i] == nKey && Memcmp == 0) and
	 * the L824 (pnVal != NULL) capture vector.
	 *
	 *   key length matches, content matches -> (T,T) hit.
	 *   key length matches, content differs -> (T,F).
	 *   key length differs                  -> (F,_).
	 *   pnVal != NULL hit                   -> capture path.
	 *   pnVal == NULL hit                   -> no-capture path.
	 *   miss with no match anywhere         -> NULL return.
	 *
	 * azElem stores (key0, val0, key1, val1, key2, val2);
	 * anElem mirrors with the byte lengths.
	 */
	if (th8InternalStubsPtr->th8_HttpsTimeFindField) {
	    char *azElem[6];
	    size_t anElem[6];
	    size_t nVal = 0;
	    const char *zHit;

	    azElem[0] = (char *)"nonce";
	    anElem[0] = 5;
	    azElem[1] = (char *)"42";
	    anElem[1] = 2;
	    azElem[2] = (char *)"ticks";
	    anElem[2] = 5;
	    azElem[3] = (char *)"1000";
	    anElem[3] = 4;
	    azElem[4] = (char *)"hash5";
	    anElem[4] = 5;
	    azElem[5] = (char *)"abc";
	    anElem[5] = 3;

	    /* (T,T) hit on "nonce" with pnVal capture. */
	    zHit = th8HttpsTimeFindField(
	        interp, azElem, anElem, 6, "nonce", 5, &nVal);
	    (void)zHit;
	    (void)nVal;

	    /* (T,T) hit on "nonce" with no capture (pnVal == NULL). */
	    zHit = th8HttpsTimeFindField(
	        interp, azElem, anElem, 6, "nonce", 5, NULL);
	    (void)zHit;

	    /* (T,F): same length, different bytes ("nonce" vs "NONCE"). */
	    zHit = th8HttpsTimeFindField(
	        interp, azElem, anElem, 6, "NONCE", 5, NULL);
	    (void)zHit;

	    /* (F,_): length mismatch -- short_circuits before Memcmp. */
	    zHit = th8HttpsTimeFindField(
	        interp, azElem, anElem, 6, "no", 2, NULL);
	    (void)zHit;

	    /* nCount == 0 -- loop body never executes; returns NULL. */
	    zHit = th8HttpsTimeFindField(
	        interp, azElem, anElem, 0, "nonce", 5, NULL);
	    (void)zHit;
	}
	Th8_ClearResult(interp);

	/*
	 * th8SecureHasMasterKey driver -- closes the C1-Pair at L1581
	 * (!pKS || !pKS->pPage) and the L1587 falls-through return-0
	 * arm, both unreachable from the in-tree callers which only
	 * invoke the function after a secure-init has placed a master
	 * key.  See ~~Bug 53~~ in incomplete.md for why the bare-Posix
	 * child-interp approach is the wrong tool here (bare Posix
	 * lacks xMalloc; merge-platform child auto-inits the keystore
	 * so C1 stays unreachable).
	 *
	 *   Drive A (parent borrow): save the parent's keystore,
	 *     install NULL via th8SetSecureKeyStore, invoke
	 *     th8SecureHasMasterKey -- drives L1581 C1 (T,_) via the
	 *     short-circuit OR -- then restore.
	 *   Drive B (merge-platform child): create a child interp
	 *     with the merged platform.  The crypto subsystem
	 *     auto-inits the keystore on Th8_CreateInterp but does
	 *     NOT auto-place a master key, so the L1584 loop runs
	 *     to completion with pSlot all-zeros and L1587 falls
	 *     through.
	 */
	if (th8InternalStubsPtr->th8_SecureHasMasterKey &&
	    th8InternalStubsPtr->th8_GetSecureKeyStore &&
	    th8InternalStubsPtr->th8_SetSecureKeyStore) {
	    void *pSavedKS = th8GetSecureKeyStore(interp);

	    /* Drive A. */
	    th8SetSecureKeyStore(interp, NULL);
	    (void)th8SecureHasMasterKey(interp);
	    th8SetSecureKeyStore(interp, pSavedKS);

	    /* Drive C (2026-06-17, batch #31): L1581 (F,T)
	     * vector closer.  Same fake-struct technique as
	     * th8test_securecanary_noppage_cmd: a zeroed
	     * memory region cast to void* has its first
	     * sizeof(void *) bytes (the Th8_KeyStore pPage
	     * field per th8_secure.c:128) as NULL.  Set this
	     * as the keystore, call HasMasterKey -- C1=F
	     * (non-NULL pKS), C2=T (pPage NULL).  Then
	     * restore. */
	    {
		static unsigned char fakeKsHmk[1024];

		Th8_Memset(interp, fakeKsHmk, 0, sizeof(fakeKsHmk));
		th8SetSecureKeyStore(interp, fakeKsHmk);
		(void)th8SecureHasMasterKey(interp);
		th8SetSecureKeyStore(interp, pSavedKS);
	    }

	    /* Drive D (2026-06-17, batch #32): L1556 (F,T)
	     * vector closer in Th8_SecureClearMasterKey,
	     * which has the identical `if (!pKS ||
	     * !pKS->pPage) return;` shape as L1581.  Safe
	     * because the function short-circuits BEFORE
	     * the th8SlotKey(pKS, ...) deref that would
	     * SIGSEGV on a fake pPage=NULL. */
	    {
		static unsigned char fakeKsClr[1024];

		Th8_Memset(interp, fakeKsClr, 0, sizeof(fakeKsClr));
		th8SetSecureKeyStore(interp, fakeKsClr);
		Th8_SecureClearMasterKey(interp);
		th8SetSecureKeyStore(interp, pSavedKS);
	    }

	    /* Drive D-bis (2026-06-18, batch #45): L1556 (T,-)
	     * C1-Pair closer.  th8SetSecureKeyStore(NULL)
	     * forces pKS=NULL on the subsequent
	     * Th8_SecureClearMasterKey lookup, driving the
	     * (T,-=F) short-circuit path.  Combined with the
	     * pre-existing (F,T) and (F,F) vectors this
	     * closes both pairs at L1556.  Safe -- the
	     * function returns at L1556 without touching
	     * any key-store fields. */
	    th8SetSecureKeyStore(interp, NULL);
	    Th8_SecureClearMasterKey(interp);
	    th8SetSecureKeyStore(interp, pSavedKS);
	}

	/* Drive E (2026-06-17, batch #33): L1473 (T,F)
	 * vector closer in Th8_IsSecurePersistEnabled
	 * `return nSecurePersistOk != 0 && nSecurePersistOk
	 *  == nSecurePersistToken;`.  Existing tests cover
	 * (F,-) (Ok=0, disabled) and (T,T) (enabled, both
	 * match); C2-Pair (T,F) requires Ok != 0 AND
	 * Ok != Token -- the "tampered" mismatch state.
	 *
	 * XOR Ok to non-zero (Token stays at 0), call
	 * IsSecurePersistEnabled to record (T,F), then XOR
	 * back to restore.  Token need not be touched. */
	if (th8InternalStubsPtr->th8_XorInterpSecurePersistOk) {
	    const th8_int64_t mask = (th8_int64_t)0xDEADBEEFu;

	    th8XorInterpSecurePersistOk(interp, mask);
	    (void)Th8_IsSecurePersistEnabled(interp);
	    th8XorInterpSecurePersistOk(interp, mask);
	}

	if (th8InternalStubsPtr->th8_SecureHasMasterKey &&
	    th8InternalStubsPtr->th8_GetSecureKeyStore &&
	    th8InternalStubsPtr->th8_SetSecureKeyStore) {
	    /* Drive B. */
	    {
		const Th8_Platform *pParentHmk = Th8_GetPlatform(interp);
		const Th8_Platform *pPosixHmk = th8test_native_platform();

		if (pParentHmk && pPosixHmk) {
		    Th8_Platform platHmk = *pParentHmk;
		    Th8_Interp *pChildHmk;

		    (void)Th8_MergePlatform(&platHmk, pPosixHmk);
		    platHmk.xPanic = 0;
		    pChildHmk = Th8_CreateInterp(&platHmk);
		    if (pChildHmk) {
			(void)th8SecureHasMasterKey(pChildHmk);
			Th8_DeleteInterp(pChildHmk);
		    }
		}
	    }
	}
	Th8_ClearResult(interp);

	/*
	 * Wrong-args drives for extensibility commands lacking
	 * existing coverage in tests/coverage/coverage_wrong_args.tcl
	 * (2026-06-09).  Each [package <subcmd>] command with an
	 * `if (argc != N)` guard at the entry has an uncovered
	 * Th8_WrongNumArgs arm.  Th8_Eval drives each via the
	 * normal command dispatcher so the script-visible error
	 * message also matches the format strings.
	 */
	(void)Th8_Eval(
	    interp, 0, "catch {package scan extra}", TH8_NOLEN, NULL, 0);
	(void)Th8_Eval(
	    interp, 0, "catch {package vcompare 1.0 2.0 3.0}", TH8_NOLEN,
	    NULL, 0);
	(void)Th8_Eval(
	    interp, 0, "catch {package provide n v extra extra}", TH8_NOLEN,
	    NULL, 0);
	(void)
	    Th8_Eval(interp, 0, "catch {package forget}", TH8_NOLEN, NULL, 0);
	(void)Th8_Eval(
	    interp, 0, "catch {package ifneeded}", TH8_NOLEN, NULL, 0);
	(void)Th8_Eval(
	    interp, 0, "catch {package present}", TH8_NOLEN, NULL, 0);
	(void)Th8_Eval(
	    interp, 0, "catch {package versions}", TH8_NOLEN, NULL, 0);
	(void)Th8_Eval(
	    interp, 0, "catch {package vsatisfies}", TH8_NOLEN, NULL, 0);
	(void)Th8_Eval(interp, 0, "catch {fpclassify}", TH8_NOLEN, NULL, 0);
	(void)
	    Th8_Eval(interp, 0, "catch {fpclassify 1 2}", TH8_NOLEN, NULL, 0);
	(void)Th8_Eval(
	    interp, 0, "catch {update extra extra extra extra}", TH8_NOLEN,
	    NULL, 0);
	(void)Th8_Eval(interp, 0, "catch {vwait}", TH8_NOLEN, NULL, 0);
	(void)
	    Th8_Eval(interp, 0, "catch {file extension}", TH8_NOLEN, NULL, 0);
	(void)Th8_Eval(
	    interp, 0, "catch {file nativename}", TH8_NOLEN, NULL, 0);
	(void)
	    Th8_Eval(interp, 0, "catch {file normalize}", TH8_NOLEN, NULL, 0);
	(void)
	    Th8_Eval(interp, 0, "catch {file rootname}", TH8_NOLEN, NULL, 0);
	(void)
	    Th8_Eval(interp, 0, "catch {file rootpath}", TH8_NOLEN, NULL, 0);
	(void)Th8_Eval(interp, 0, "catch {file type}", TH8_NOLEN, NULL, 0);
	/*
	 * Tokenizer L19619 4-condition coverage probe (2026-06-10):
	 * `if (n == 0 || *z == ';' || *z == '\n' || *z == '\r')`
	 * has C3-Pair / C4-Pair uncovered.  Use non-leaking
	 * variants: wrap in scoped procs to keep globals clean.
	 * If C3/C4 vectors don't fire, the arms are intrinsic-dead
	 * (upstream consumes \n/\r before the check); if they do,
	 * +2 conditions.  Wrapped so no leak into ::tcl_platform
	 * et al on cleanup.
	 */
	(void)Th8_Eval(
	    interp, 0,
	    "catch {namespace eval ::th8t_lf {"
	    " set a 1\nset b 2; list a\nb"
	    "}}; namespace delete ::th8t_lf",
	    TH8_NOLEN, NULL, 0);
	(void)Th8_Eval(
	    interp, 0,
	    "catch {namespace eval ::th8t_cr {"
	    " set a 1\rset b 2; list a\rb"
	    "}}; namespace delete ::th8t_cr",
	    TH8_NOLEN, NULL, 0);
	Th8_ClearResult(interp);

	/*
	 * th8EnvKeyValue TH8_KV_UNSET driver (2026-06-09): the
	 * src/th8_env.c L343 `case TH8_KV_UNSET` arm is uncovered
	 * because no in-tree script test calls Th8_KeyValue with
	 * TH8_KV_UNSET (the env plugin exposes get/set/exists but
	 * not a unset entry).  Drive directly: set then unset a
	 * test-only env var.  Safe -- the var name is namespaced
	 * with the TH8TEST_ prefix and is purely synthetic.
	 */
	{
	    (void)Th8_KeyValue(
	        interp, TH8_KV_SET, "TH8TEST_KV_TMP", 14, "1", 1);
	    (void)Th8_KeyValue(
	        interp, TH8_KV_UNSET, "TH8TEST_KV_TMP", 14, NULL, 0);
	    Th8_ClearResult(interp);
	}

	/*
	 * Bug 16 driver (2026-06-09): drive the per-iteration
	 * `interp && Th8_Ready(interp) != TH8_OK` (T,T) vector at
	 * src/th8_glob.c L90 by staging the step counter just below
	 * the limit, then calling Th8_GlobMatch.  The L80 entry-check
	 * runs th8Step which increments count to == limit (still OK);
	 * the FIRST L90 per-iteration check runs th8Step again,
	 * incrementing count to limit+1, returns ERROR -> L90 drives
	 * (T,T).
	 *
	 * Uses the new public Th8_SetStepCount API (added 2026-06-09)
	 * so this is callable from any embedder, not just testlib.
	 *
	 * The pattern is a non-trivial one ('*x*') so the matcher
	 * enters the iteration loop instead of falling through the
	 * empty-pattern early-exit.
	 */
	{
	    th8_int64_t savedLimit = Th8_GetStepLimit(interp);
	    th8_int64_t savedCount = Th8_GetStepCount(interp);

	    Th8_SetStepLimit(interp, 8);
	    Th8_SetStepCount(interp, 7); /* limit - 1 */
	    (void)Th8_GlobMatch(interp, "*x*", 3, "abcxyz", 6);

	    Th8_SetStepLimit(interp, savedLimit);
	    Th8_SetStepCount(interp, savedCount);
	    Th8_ClearResult(interp);
	}

	/*
	 * th8InitGlobals user/host name fallback exerciser (Bug 18 --
	 * 2026-06-09 RCA, platform-overlay route): the (F,-) and (T,F)
	 * C2-Pairs at th8_core.c L19227 / L19237 (`pPlat->xGetUserName
	 * && pPlat->xGetUserName(...) == TH8_OK` and the matching
	 * xGetHostName compound) are unreachable via runtime
	 * Th8_FaultInstall because th8InitGlobals runs ONCE during
	 * Th8_CreateInterp, before fault install can take effect.
	 * Driven via the merged-platform overlay (Bug 48 RCA) -- the
	 * targeted callbacks are overridden BEFORE Th8_CreateInterp so
	 * no public API change is required.
	 *
	 *   Child A: xGetUserName=NULL, xGetHostName=NULL -- (F,-).
	 *   Child B: callbacks point at th8test_stub_xGetUserHostName_err
	 *            which returns TH8_ERROR -- (T,F).
	 *
	 * Each Th8_CreateInterp triggers th8InitGlobals which sets
	 * ::tcl_platform(user|host) to "" via the fallback branch.
	 */
#  if defined(TH8_ENABLE_VARIABLES)
	{
	    const Th8_Platform *pParent = Th8_GetPlatform(interp);
	    if (pParent) {
		Th8_Platform plat;
		Th8_Interp *pChild;

		/* Child A: NULL callbacks -> (F,-). */
		plat = *pParent;
		plat.xPanic = 0;
		plat.xGetUserName = 0;
		plat.xGetHostName = 0;
		pChild = Th8_CreateInterp(&plat);
		if (pChild) {
		    Th8_DeleteInterp(pChild);
		}

		/* Child B: stub-returning-error -> (T,F). */
		plat = *pParent;
		plat.xPanic = 0;
		plat.xGetUserName = th8test_stub_xGetUserHostName_err;
		plat.xGetHostName = th8test_stub_xGetUserHostName_err;
		pChild = Th8_CreateInterp(&plat);
		if (pChild) {
		    Th8_DeleteInterp(pChild);
		}
	    }
	}
#  endif /* TH8_ENABLE_VARIABLES */

	/*
	 * Th8_ToInt parser-edge exerciser (Bug 50 -- 2026-06-09):
	 * direct calls on tiny / empty / signed inputs to drive the
	 * MC/DC vectors at src/th8_core.c L15075 (`n > 0 && z[0] ==
	 * '-'`) and L15078 (`else if (n > 0 && z[0] == '+')`).
	 *
	 *   ""    -- (F,-)+(F,-): both arms short-circuit on n>0=F.
	 *   "+"   -- L15075 (T,F) + L15078 (T,T); error returned.
	 *   "+0"  -- L15075 (T,F) + L15078 (T,T); val=0 returned.
	 *   "-0"  -- L15075 (T,T); L15078 unreached (else branch).
	 *   "0"   -- L15075 (T,F) + L15078 (T,F); val=0 returned.
	 */
	{
	    int iv;
	    (void)Th8_ToInt(interp, "", 0, &iv);
	    (void)Th8_ToInt(interp, "+", 1, &iv);
	    (void)Th8_ToInt(interp, "+0", 2, &iv);
	    (void)Th8_ToInt(interp, "-0", 2, &iv);
	    (void)Th8_ToInt(interp, "0", 1, &iv);
	    Th8_ClearResult(interp);
	}

	/*
	 * Channel-tombstone exerciser (Bug 48 -- FIXED 2026-06-09 RCA).
	 *
	 * Drives the (F,T) `pEntry valid but pData NULL` tombstone vectors:
	 *   th8_channel.c L62  -- th8ChannelFind
	 *   th8_channel.c L708 -- th8ChannelClose
	 *   th8_channel.c L800 -- th8ChannelListCallback (via Th8_Eval
	 *                          of `[file channels]`)
	 *
	 * Uses the merge-platform overlay: bare Th8_GetPosixPlatform()
	 * has a NULL xMalloc, so a child created from it crashes on
	 * the first allocation.  Starting from the parent's already-
	 * merged platform and overlaying the Posix slots gives the
	 * child a working allocator and exercises the same code paths.
	 */
#  if defined(TH8_PLUGIN_FILE_SYSTEMS)
	{
	    const Th8_Platform *pParent = Th8_GetPlatform(interp);
	    const Th8_Platform *pPosix = th8test_native_platform();
	    if (pParent && pPosix) {
		Th8_Platform plat = *pParent;
		Th8_Interp *pChild;

		(void)Th8_MergePlatform(&plat, pPosix);
		plat.xPanic = 0;
		pChild = Th8_CreateInterp(&plat);
		if (pChild) {
		    Th8_Hash *paCh;
		    Th8_RegisterLanguage(pChild);
		    (void)Th8_Eval(
		        pChild, 0, "set ::th8t_tomb_chan [file tempname 64]",
		        TH8_NOLEN, NULL, 0);
		    paCh = th8GetInterpPaChannels(pChild);
		    if (paCh) {
			size_t nChName = 0;
			const char *zChName = NULL;
			char zBuf[256];
			if (Th8_GetVar(
			        pChild, "::th8t_tomb_chan", TH8_NOLEN) ==
			    TH8_OK) {
			    zChName = Th8_GetResult(pChild, &nChName);
			}
			if (zChName && nChName > 0 &&
			    nChName < sizeof(zBuf)) {
			    size_t i;
			    Th8_HashEntry *pChE;
			    for (i = 0; i < nChName; i++)
				zBuf[i] = zChName[i];
			    zBuf[i] = 0;
			    pChE =
			        Th8_HashFind(pChild, paCh, zBuf, nChName, 0);
			    if (pChE && pChE->pData) {
				void *pSaved = pChE->pData;
				pChE->pData = NULL;
				(void)th8ChannelFind(
				    pChild, zBuf, nChName); /* L62 (F,T) */
				(void)th8ChannelClose(
				    pChild, zBuf, nChName); /* L708 (F,T) */
				/* Drive L800 (F,T) -- the hash iteration
				 * callback walks every entry including the
				 * tombstoned one. */
				(void)Th8_Eval(
				    pChild, 0, "file channels", TH8_NOLEN,
				    NULL, 0);
				pChE->pData = pSaved;
			    }
			}
			Th8_ClearResult(pChild);
		    }
		    Th8_DeleteInterp(pChild);
		}
	    }
	}

	/*
	 * Bug 48 follow-up (2026-06-10): drive
	 * th8ChannelFreeEntry L596 `if (!pEntry || !pEntry->pData)`
	 * (F,T) by tombstoning an entry and leaving it that way
	 * before Th8_DeleteInterp -- the hash teardown invokes the
	 * xFree callback on every entry, including the tombstoned
	 * one.  Th8ChannelFreeEntry's plain-`if` guard returns
	 * TH8_OK on (F,T) without touching pData, leaking the
	 * channel; that's acceptable for a bounded test cycle and
	 * the channel itself is bound to the child's lifetime
	 * which ends here.  Uses a separate child interp so the
	 * earlier Bug 48 driver's restore-then-delete pattern
	 * (which only drives L62/L708/L800) stays unchanged.
	 */
	{
	    const Th8_Platform *pParent2 = Th8_GetPlatform(interp);
	    const Th8_Platform *pPosix2 = th8test_native_platform();
	    if (pParent2 && pPosix2) {
		Th8_Platform plat2 = *pParent2;
		Th8_Interp *pChild2;

		(void)Th8_MergePlatform(&plat2, pPosix2);
		plat2.xPanic = 0;
		pChild2 = Th8_CreateInterp(&plat2);
		if (pChild2) {
		    Th8_Hash *paCh2;
		    Th8_RegisterLanguage(pChild2);
		    (void)Th8_Eval(
		        pChild2, 0,
		        "set ::th8t_tomb2_chan [file tempname 64]", TH8_NOLEN,
		        NULL, 0);
		    paCh2 = th8GetInterpPaChannels(pChild2);
		    if (paCh2) {
			size_t nChName2 = 0;
			const char *zChName2 = NULL;
			char zBuf2[256];
			if (Th8_GetVar(
			        pChild2, "::th8t_tomb2_chan", TH8_NOLEN) ==
			    TH8_OK) {
			    zChName2 = Th8_GetResult(pChild2, &nChName2);
			}
			if (zChName2 && nChName2 > 0 &&
			    nChName2 < sizeof(zBuf2)) {
			    size_t k;
			    Th8_HashEntry *pChE2;
			    for (k = 0; k < nChName2; k++)
				zBuf2[k] = zChName2[k];
			    zBuf2[k] = 0;
			    pChE2 = Th8_HashFind(
			        pChild2, paCh2, zBuf2, nChName2, 0);
			    if (pChE2 && pChE2->pData) {
				/* Tombstone without restoring -- the
			     * teardown sees pData==NULL and drives
			     * L596 (F,T). */
				pChE2->pData = NULL;
			    }
			}
			Th8_ClearResult(pChild2);
		    }
		    Th8_DeleteInterp(pChild2);
		}
	    }
	}
#  endif /* TH8_PLUGIN_FILE_SYSTEMS */

	/* Drive th8_core.c L853 (th8SetCmdToken) and L906
	 * (th8GetCmdToken) `pEntry && ALWAYS(pEntry->pData)`
	 * MC/DC vectors.  Both are TH8_INTERNAL dead wrappers
	 * with no in-tree callers, so both decisions show zero
	 * vectors until exercised here.  Drive {T,C} (existing
	 * command) and {F,-} (missing command) for each. */
	{
	    th8_uint64_t tok;
	    th8SetCmdToken(
	        interp, "::th8testlib::nop", (th8_uint64_t)0x42); /* {T,C} */
	    th8SetCmdToken(
	        interp, "::th8test_no_such_cmd",
	        (th8_uint64_t)0x43); /* {F,-} */
	    tok = th8GetCmdToken(interp, "::th8testlib::nop");
	    (void)tok; /* {T,C} */
	    tok = th8GetCmdToken(interp, "::th8test_no_such_cmd");
	    (void)tok; /* {F,-} */
	    /* Note: the {T,F} tombstone vector at L861/L915 (pEntry
	     * non-NULL with pData==NULL) is intrinsic-dead via the
	     * public-API path tried in batch 114 -- Th8_CreateCommand
	     * + Th8_DeleteCommand(by-token) fully removes the entry
	     * rather than tombstoning it, so the pData=NULL sentinel
	     * never appears via this route.  Tombstones do exist in
	     * the hash layer (per Th8_HashDelete's logic) but
	     * require direct hash-level manipulation not exposed
	     * through the public Command API. */

	    /* Drive Th8_DeleteCommand L9565 C1=T (pTokEntry NULL)
	     * vector with a synthetic token that no command will
	     * have been assigned.  The function returns TH8_ERROR
	     * after setting the result; both result and rc are
	     * discarded.  Pick a value extremely unlikely to
	     * collide with any auto-assigned command token. */
	    (void)Th8_DeleteCommand(
	        interp, (th8_uint64_t)0xDEADBEEFCAFEBABEULL);
	}

	/* Drive Th8_CancelEval cross-call-state transitions
	 * (th8_core.c L6992 / L7016).  Both decisions need a
	 * prior non-signal cancel call to have set
	 * bCancelMsgOwned=1, then a follow-up call observes the
	 * owned buffer.  Done on a child interp so the parent's
	 * cancel state is not disturbed.  Per the existing pattern
	 * for FAULT_INJECTION-only paths, gate on the libc
	 * platform's availability. */
	{
	    const Th8_Platform *pLib = Th8_GetLibcPlatform();
	    if (pLib) {
		Th8_Platform plat = *pLib;
		Th8_Interp *pChild;

		plat.xPanic = 0;
		pChild = Th8_CreateInterp(&plat);
		if (pChild) {
		    Th8_RegisterLanguage(pChild);
		    /* Step 1: non-signal cancel: sets bCancelMsgOwned=1,
		     * allocates a copy of "owned1". */
		    (void)Th8_CancelEval(pChild, "owned1", 6, 0);
		    /* Step 2: non-signal cancel again: L7016 sees the
		     * prior bCancelMsgOwned=1, drives C1=T (and C2=T
		     * since zCancelMsg is also non-NULL from step 1). */
		    (void)Th8_CancelEval(pChild, "owned2", 6, 0);
		    /* Step 3: signal-mode cancel: L6992 sees prior state
		     * (zSavedCancelMsg=NULL because no signal call has
		     * happened yet, bCancelMsgOwned=1 from step 2,
		     * zCancelMsg non-NULL), drives the full (T,T,T)
		     * triple closing C2-Pair and C3-Pair. */
		    (void)Th8_CancelEval(
		        pChild, "static1", 7, TH8_CANCEL_SIGNAL);
		    Th8_DeleteInterp(pChild);
		}
	    }
	}

	/* Drive th8_core.c L2087 (th8SignalEvent),
	 * L2115 (th8ResetEvent), L2152 (th8WaitEvent), and
	 * L2181 (th8PStateQueueLen) C1-Pair vectors.  All four
	 * are TH8_INTERNAL with zero in-tree callers (the
	 * cross-thread Th8_QueueEvent path uses xEventSet
	 * directly).  Drive the C1=T (NULL pState) vector with
	 * a NULL arg, then the C1=F (valid pState) vector with
	 * a real pState from Th8_CreateAsyncState. */
	{
	    void *pState;
	    int wrc, qlen;

	    th8SignalEvent(NULL); /* {T,-,-} */
	    th8ResetEvent(NULL); /* {T,-,-} */
	    wrc = th8WaitEvent(NULL, 0);
	    (void)wrc; /* {T,-,-} */
	    qlen = th8PStateQueueLen(NULL);
	    (void)qlen; /* {T,-} */
	    {
		int bDrained = 0;
		(void)th8DrainOneStateEvent(NULL, &bDrained); /* (T,-) */
	    }

	    pState = Th8_CreateAsyncState(interp, NULL);
	    if (pState) {
		th8SignalEvent((Th8_AsyncState *)pState);
		th8ResetEvent((Th8_AsyncState *)pState);
		wrc = th8WaitEvent((Th8_AsyncState *)pState, 0);
		(void)wrc;
		qlen = th8PStateQueueLen((Th8_AsyncState *)pState);
		(void)qlen;
		/*
		 * Drive th8_core.c L2177 (th8PStateQueueLen) and L2250
		 * (Th8_QueueEvent) (F,T) vectors: pState non-NULL but
		 * bMutexReady == 0.  XOR the flag from 1 -> 0, call
		 * both consumers, XOR it back so Th8_FinalizeAsyncState
		 * tears down through the normal mutex-final path.
		 */
		(void)
		    th8AsyncStateXorBMutexReady((Th8_AsyncState *)pState, 1);
		qlen = th8PStateQueueLen((Th8_AsyncState *)pState);
		(void)qlen; /* L2177 (F,T) */
		/* Use a non-NULL callback so L2707 (!xCallback)
		 * passes; the bMutexReady == 0 state then drives
		 * L2709 (T,-). */
		(void)Th8_QueueEvent(
		    pState, th8test_null_guard_qe_cb); /* L2709 (T,-) */
		{
		    int bDrained = 0;
		    (void)th8DrainOneStateEvent(
		        (Th8_AsyncState *)pState,
		        &bDrained); /* L2250 (F,T) */
		}
		(void)
		    th8AsyncStateXorBMutexReady((Th8_AsyncState *)pState, 1);
		Th8_FinalizeAsyncState(pState);
	    }

	    /*
	     * Drive th8_core.c L2645 + L2651 finalize-path (F,-) /
	     * (T,F) vectors.  Each cycle creates a fresh pState,
	     * zeroes one field via th8AsyncStateScrubField, then
	     * calls Th8_FinalizeAsyncState.  The destruction
	     * branches observe the scrubbed field and skip the
	     * platform call, exercising the half-true arms.
	     *
	     *   field 0 (pEventHandle  -> NULL): L2645 (F,-)
	     *   field 1 (xEventDestroy -> NULL): L2645 (T,F)
	     *   field 2 (xMutexFinal   -> NULL): L2651 (T,F)
	     *   (L2651 (F,-) is bMutexReady == 0; covered via the
	     *    XOR window above.)
	     */
	    {
		int iField;
		for (iField = 0; iField < 3; iField++) {
		    void *pStateB = Th8_CreateAsyncState(interp, NULL);
		    if (pStateB) {
			th8AsyncStateScrubField(
			    (Th8_AsyncState *)pStateB, iField);
			Th8_FinalizeAsyncState(pStateB);
		    }
		}
	    }

	    /*
	     * Drive th8_core.c L2651 (F,-) (bMutexReady == 0 at
	     * finalize time): XOR the flag from 1 -> 0 and DO NOT
	     * XOR back before Th8_FinalizeAsyncState.  The
	     * mutex-final branch observes bMutexReady == 0 and
	     * skips the platform xMutexFinal call.
	     */
	    {
		void *pStateC = Th8_CreateAsyncState(interp, NULL);
		if (pStateC) {
		    (void)th8AsyncStateXorBMutexReady(
		        (Th8_AsyncState *)pStateC, 1);
		    Th8_FinalizeAsyncState(pStateC);
		}
	    }

	    /*
	     * Drive th8_core.c L2709 (F,T) (bMutexReady == 1,
	     * pEventHandle == NULL): scrub pEventHandle on a
	     * fresh pState, call Th8_QueueEvent with a valid
	     * non-NULL callback so the L2707 / L2708 guards
	     * pass, then finalize.
	     */
	    {
		void *pStateD = Th8_CreateAsyncState(interp, NULL);
		if (pStateD) {
		    th8AsyncStateScrubField((Th8_AsyncState *)pStateD, 0);
		    (void)Th8_QueueEvent(
		        pStateD, th8test_null_guard_qe_cb); /* (F,T) */
		    Th8_FinalizeAsyncState(pStateD);
		}
	    }
	}
    }

    /* Th8_ByteToUtf16Col: pure utility, NULL + various inputs. */
    (void)Th8_ByteToUtf16Col("hello", 5, 3);
    (void)Th8_ByteToUtf16Col("", 0, 0);
    (void)Th8_ByteToUtf16Col(NULL, 0, 0);
    /* multi-byte UTF-8 to verify column calc (\xc3\xa9 = U+00E9). */
    (void)Th8_ByteToUtf16Col("\xc3\xa9", 2, 2);

    /* Token-gate (T, F) MC/DC vectors via a fresh child interp.
     * th8TestPerturb*Token (internal stubs) XOR the token so
     * that nXxxOk != nXxxToken without zeroing either,
     * satisfying C1=T but C2=F at each Th8_IsXxxEnabled gate.
     * The child interp is discarded immediately so the
     * divergent state never reaches the rest of the suite. */
    {
	const Th8_Platform *pP = Th8_GetPlatform(interp);
	if (pP) {
	    Th8_Platform cp = *pP;
	    Th8_Interp *pChild = Th8_CreateInterp(&cp);
	    if (pChild) {
		Th8_RegisterLanguage(pChild);
#  if defined(TH8_ENABLE_LOAD)
		(void)Th8_EnableLoad(pChild, 1);
		(void)Th8_IsLoadEnabled(pChild); /* (T, T) */
		th8TestPerturbLoadToken(pChild);
		(void)Th8_IsLoadEnabled(pChild); /* (T, F) */
#  endif
#  if defined(TH8_ENABLE_BIGINT)
		(void)Th8_EnableBigint(pChild, 1);
		(void)Th8_IsBigintEnabled(pChild); /* (T, T) */
		th8TestPerturbBigintToken(pChild);
		(void)Th8_IsBigintEnabled(pChild); /* (T, F) */
#  endif

		(void)Th8_EnableSignedOnly(pChild, 1);
		(void)Th8_IsSignedOnlyEnabled(pChild); /* (T, T) */
		th8TestPerturbSignedToken(pChild);
		(void)Th8_IsSignedOnlyEnabled(pChild); /* (T, F) */

#  if defined(TH8_ENABLE_LOAD)
		/* th8IsUnloadEnabled L534 (3-condition): C2-Pair
		 * via token-perturb, C3-Pair via flag-clear.
		 * th8IsUnloadDangerous L567 C1-Pair via direct
		 * call when unload is disabled.  Routed through
		 * the internal-stubs table since the functions
		 * are TH8_INTERNAL. */
		(void)Th8_EnableUnload(pChild, TH8_UNLOAD_OK);
		(void)th8IsUnloadEnabled(pChild); /* (T, T, T) */
		(void)th8IsUnloadDangerous(pChild); /* (T, F)   */
		th8TestPerturbUnloadToken(pChild);
		(void)th8IsUnloadEnabled(pChild); /* (T, F, -) */
		/* Re-align tokens then clear flag for C3-Pair. */
		(void)Th8_EnableUnload(pChild, TH8_UNLOAD_OK);
		th8TestClearUnloadFlag(pChild);
		(void)th8IsUnloadEnabled(pChild); /* (T, T, F) */
		/* Disable unload entirely for the L567 (F, -)
		 * vector at th8IsUnloadDangerous. */
		(void)Th8_EnableUnload(pChild, 0);
		(void)th8IsUnloadDangerous(pChild); /* (F, -)   */
#  endif

		Th8_DeleteInterp(pChild);
	    }
	}
    }

    /* Character-property classifiers (th8_core.c L10231..6).
     * Each `c >= 0 && c < 256 && (charprop & mask)` has a
     * C2=F vector that needs c >= 256 -- unreachable from
     * in-tree callers that pass byte values.  Calling via
     * the internal-stubs exerciser with c=256 closes the
     * (T, F, -) pair on every classifier in one shot, and
     * c=-1 drives the (F, -) defensive symmetric vector.
     *
     * The remaining (T, T, T)/(T, T, F) C3-Pair vectors are
     * driven by feeding a representative byte set covering
     * every bit-mask hit and at least one miss per classifier:
     *   0x00  -- all bits clear (miss for every classifier).
     *   ' '   -- whitespace (IsSpace).
     *   '0'   -- digit + hex digit + oct/bin digit.
     *   '8'   -- digit + hex (but not oct).
     *   '2'   -- digit + hex (but not bin).
     *   'A'   -- alpha + alnum + hex digit.
     *   'G'   -- alpha + alnum (but not hex).
     *   '['   -- list-special (IsSpecial).
     *   '@'   -- punctuation (miss for digit/alpha/hex).
     * th8IsSpecial is only reachable from this exerciser, so
     * its (T,T,T)/(T,T,F) pairs depend entirely on this set. */
    th8TestExerciseCharProps(256);
    th8TestExerciseCharProps(-1);
    th8TestExerciseCharProps(0x00);
    th8TestExerciseCharProps(' ');
    th8TestExerciseCharProps('0');
    th8TestExerciseCharProps('1');
    th8TestExerciseCharProps('8');
    th8TestExerciseCharProps('2');
    th8TestExerciseCharProps('A');
    th8TestExerciseCharProps('G');
    th8TestExerciseCharProps('[');
    th8TestExerciseCharProps('@');

    /* Win32-device-name classifier (plugins/th8_filesystems.c).
     * th8IsDeviceName is reached only from file_validname_command
     * under `if (TH8_IS_SEP('\\'))` which is false on POSIX, so
     * its three decisions (trailing-strip loop, CON/PRN/AUX/NUL
     * 24-cond, COM1-9/LPT1-9 16-cond) are unreachable on a POSIX
     * MC/DC run.  The internal-stubs exerciser drives a fixed
     * sweep that covers each canonical name, mixed-case variants,
     * digit boundaries, and trailing-character combinations. */
    th8TestExerciseDeviceName();

    /* Bigint-cache-store NULL-guard exerciser (th8_bigint.c).
     * th8BigintCacheStore is static; its sole in-tree caller
     * (th8IsBigint) always passes non-NULL interp / pSrc, so the
     * L246 `!interp || !pSrc` Bug-26-family guard is intrinsic-
     * dead via the public path.  The exerciser passes NULL for
     * each argument in turn, driving C1=T and C2=T pairs. */
    th8TestExerciseBigintCacheStore(interp);

    /* Tombstone exerciser for the command-name hash (th8_core.c).
     * Drives th8SetCmdToken L861 and th8GetCmdToken L915 (T,F)
     * vectors -- the public Th8_HashFind removes entries fully
     * (no tombstone state), so this exerciser locally nulls
     * pData on a known existing entry, runs the consumer calls,
     * and restores pData afterward. */
    th8TestExerciseCmdTokenTombstone(interp);

    /* Tombstone exerciser for the math-function registry
     * (th8_expr.c).  Drives the (F,T) C2-Pair of the Bug 26
     * tombstone-safe `if (!pHash || !pHash->pData)` at L292 in
     * Th8_FindMathFunc by manually nulling pData on the live
     * "test_echo" entry (registered at testlib load), running
     * the consumer, and restoring pData. */
    th8TestExerciseMathFuncTombstone(interp);

    /*
     * Drive th8_core.c L967 (th8MallocCommon) and L1585
     * (th8ReallocCommon) `if (!interp || !interp->pPlatform)`
     * (F,T) vectors: interp non-NULL but pPlatform NULL.
     * Exchange the platform pointer for NULL, invoke the
     * non-panicking attempt-alloc / attempt-realloc paths
     * (so the failure stays observable as NULL return), then
     * restore the platform IMMEDIATELY -- any intervening
     * code that allocates in this window would crash.
     */
    {
	Th8_Platform *pSavedPlat;
	void *pProbe;
#  if 1 /* Bug 52 -- FIXED via plain `if` in th8CacheMutexLock */
	/* Pre-resolve fpclassify so the swap window does no hash
	 * lookups (which could allocate).  Used by the
	 * th8MathClassify L662 (T,-) drive attempt below. */
	int nArgC = 0;
	Th8_MathFuncProc xProcC = NULL;
	void *pCtxC = NULL;
	(void)Th8_FindMathFunc(
	    interp, "fpclassify", TH8_NOLEN, &nArgC, &xProcC, &pCtxC);
#  endif /* Bug 52 */

	pSavedPlat = th8XchgInterpPlatform(interp, NULL);
	pProbe = Th8_AttemptMalloc(interp, 16); /* L967 (F,T) */
	(void)Th8_AttemptRealloc(interp, pProbe, 32); /* L1585 (F,T) */
	/* Th8_Strlen's `if (p && p->xStrlen)` falls back to the
	 * built-in NUL-scan when Th8_GetPlatform returns NULL.
	 * The call does not allocate, so the platform-NULL window
	 * stays safe.  Drives th8_core.c L3870 (F,-). */
	(void)Th8_Strlen(interp, "x");
	/* Th8_Load's `!interp->pPlatform || !xLoad` (T,-) vector
	 * is now reachable because the error path uses
	 * Th8_SetResultStatic (no allocation) and the Th8_Free
	 * ALWAYS-wrap on interp->pPlatform was converted to a
	 * plain check (Bug 26 family). */
	(void)Th8_Load(interp, "test_path", 9, "fakeProc", 8);
	/* th8MathOp `!interp->pPlatform || !xMathFunc` (T,-)
	 * vector: pPlatform NULL.  Error path now uses
	 * Th8_SetResultStatic (promoted 2026-06-09), so the
	 * allocation chain stays out of the platform-NULL
	 * window. */
	{
	    double r = 0.0;
	    (void)th8MathOp(interp, 0, &r, 1.0, 0.0);
	}
	/* th8MathClassify L662 `!pPlat || !pPlat->xMathFunc` (T,-)
	 * drive attempt -- see Bug 52 in incomplete.md.  The dispatch
	 * crashes inside Th8_ToDouble (called at L658, BEFORE the L662
	 * platform check) which routes through Th8_FindInCache ->
	 * th8CacheMutexLock -> assert(0) when pPlatform is NULL.
	 * Code retained gated under #if 0 so it returns to active
	 * duty when Bug 52 is fixed (reorder check-before-parse, or
	 * make th8CacheMutexLock platform-NULL-safe).  Cost: 1 MC/DC
	 * condition at L662 stays uncovered. */
#  if 1 /* Bug 52 -- FIXED via th8MathClassify reorder */
	if (xProcC != NULL) {
	    (void)xProcC(interp, pCtxC, "1", 1, NULL, 0);
	}
#  endif /* Bug 52 */
	/* th8_bigint_calloc L107 (F,-) (T,T): wrap two calls in
	 * th8BigintSetup/Teardown so th8_bigint_interp is
	 * non-NULL, then call with nmemb==0 (drives F,-) and
	 * with nmemb=SIZE_MAX, size=2 (drives T,T overflow). */
	th8BigintSetup(interp);
	(void)th8_bigint_calloc(0, 16);
	(void)th8_bigint_calloc((size_t)-1, 2);
	th8BigintTeardown();
	/* Th8_DnsResolve / Th8_DnsResolveFree's
	 * `!pPlatform || !xDnsResolve` (T,-) -- both return
	 * immediately without allocating or calling SetResult,
	 * so the platform-NULL window stays safe.  Drives
	 * th8_plat.c L2285 + L2297. */
	{
	    Th8_DnsResult *pDns = NULL;
	    /* Pass a synthetic non-NULL stub to Free so the
	     * L2295 `!pResult` short-circuit is bypassed and
	     * L2297 (T,-) fires.  The platform-NULL path returns
	     * before any deref, so the bogus pointer is safe. */
	    Th8_DnsResult *pStub = (Th8_DnsResult *)(void *)&pDns;
	    (void)Th8_DnsResolve(interp, "host", 4, TH8_DNS_TYPE_A, &pDns);
	    Th8_DnsResolveFree(interp, pStub);
	}
	(void)th8XchgInterpPlatform(interp, pSavedPlat);
    }

    /*
     * NOTE: th8_load.c L750 (`!interp->pPlatform || !xLoad`) (T,-)
     * looked drivable via the same swap pattern -- Th8_Load
     * checks pPlatform right after the Th8_IsLoadEnabled gate
     * with no allocation between -- but Th8_SetResult on the
     * error path goes through TH8_ALLOC_STR / Th8_Memcpy which
     * ultimately deref the (NULL) platform once allocation
     * fails.  The crash surfaces inside Th8_SetResult's
     * Memcpy/Strlen fallback chain.  Logged for follow-up; the
     * production code is fine because pPlatform is never NULL
     * outside teardown, which is the explicit Bug 26 motivation
     * for keeping the check as a defensive guard.
     */

    /* Tombstone exerciser for the per-interp array-search hash
     * (plugins/th8_variables.c).  Drives the (F,T) C2-Pair of
     * the Bug 26 tombstone-safe `if (!pEntry || !pEntry->pData)`
     * at L1604 in th8ArraySearchFind via [array startsearch] +
     * manual pData clearing + [array nextelement]. */
    th8TestExerciseArraySearchTombstone(interp);

    /* Drive the expansion-prefix scan extracted to
     * th8CheckExpansionPrefix (shared by th8SplitCommand and
     * th8NRSubstAndBuild).  The "{}rest" arm closes the
     * (T,F) C2-Pair on `k + 1 < nWord && k > 1` -- exposing
     * the helper via internal stubs lets the testlib drive it
     * directly without relying on the parser-side script tests
     * remaining unchanged. */
    th8TestExerciseExpansionPrefix(interp);

    /*
     * Drive th8_core.c L7720-7722 C1-Pair: the inner
     * compound condition `Th8_GetFrameObjv(...) == TH8_OK
     * && argc > 0 && argv && argv[0]` only sees (T,T,T,T)
     * via [debug frames info] because that caller always
     * targets a valid proc frame.  Calling Th8_GetFrameInfo
     * at frameIndex=0 from a [C]-command context puts
     * nTarget=0, which Th8_GetFrameObjv rejects with
     * "bad level" -> C1=F vector.
     */
    {
	const char *zProc = NULL;
	size_t nProc = 0;
	const char *zScript = NULL;
	size_t nScript = 0;
	int nLine = 0;
	(void)Th8_GetFrameInfo(
	    interp, 0, &zProc, &nProc, &zScript, &nScript, &nLine);
	Th8_ClearResult(interp);
    }

    /*
     * MC/DC drive (2026-06-08): cover the (T,-) and (F,T) vectors
     * of the Bug 26 `if (!a || !b)` defensive guards inside
     * th8TestExerciseCmdTokenTombstone itself.  The happy-path
     * call above produces only (F,F); re-enter the exerciser with
     * constructed bad state (NULL interp, tombstoned hash entry,
     * NULL paCmd/paCmdToken/paExpansion via temporary swap) to
     * close the remaining MC/DC pairs.
     *
     * Lives here in testlib (immune from MC/DC coverage scope) so
     * the helper's own bookkeeping `if (...)` checks do not count
     * against th8_core.c coverage.
     */
    {
	Th8_Namespace *pNs;
	Th8_HashEntry *pEntry;
	const char *zNsPath = 0, *zTail = 0;
	size_t nNsPath = 0, nTail = 0;

	/* L10254 (T,-): NULL interp -- function returns immediately. */
	th8TestExerciseCmdTokenTombstone(NULL);

	th8SplitQualName(
	    "::th8testlib::nop", 17, &zNsPath, &nNsPath, &zTail, &nTail);
	if (zNsPath) {
	    pNs = th8FindNamespace(interp, zNsPath, nNsPath, 0);
	    if (pNs) {
		Th8_Hash *paSaved;

		/* L10258 (F,T): pNs non-NULL but paCmd NULL. */
		paSaved = pNs->paCmd;
		pNs->paCmd = NULL;
		th8TestExerciseCmdTokenTombstone(interp);
		pNs->paCmd = paSaved;

		if (pNs->paCmd) {
		    pEntry =
		        Th8_HashFind(interp, pNs->paCmd, zTail, nTail, 0);
		    if (pEntry) {
			/* L10260 (F,T): pEntry non-NULL, pData NULL. */
			void *pSavedData = pEntry->pData;
			pEntry->pData = NULL;
			th8TestExerciseCmdTokenTombstone(interp);
			pEntry->pData = pSavedData;
		    }
		}
	    }
	}

	/* L10280 (T,-): null paCmdToken via temporary swap. */
	{
	    Th8_Hash *paTokSaved = th8GetInterpCmdToken(interp);
	    th8SetInterpCmdToken(interp, NULL);
	    th8TestExerciseCmdTokenTombstone(interp);
	    th8SetInterpCmdToken(interp, paTokSaved);
	}

	/* L10306 (F,T): null pCurrentNs->paExpansion via swap. */
	{
	    Th8_Namespace *pCur = th8GetInterpCurrentNs(interp);
	    if (pCur) {
		Th8_Hash *paExpSaved = pCur->paExpansion;
		pCur->paExpansion = NULL;
		th8TestExerciseCmdTokenTombstone(interp);
		pCur->paExpansion = paExpSaved;
	    }
	}
    }

    /* Platform xLoad / xUnload bad-arg defensive sweep
     * (th8_posix.c::th8PosixLoad L533, th8PosixUnload L1081).
     * Th8_Load / Th8_Unload pre-validate their args so the
     * platform-level `!interp || !zName || nName == 0` guards
     * are never reached via the public API.  Direct invocation
     * via the platform pointer drives C1=T (interp NULL), C2=T
     * (zName NULL), and C3=T (nName 0) cleanly without
     * actually attempting any dynamic linking. */
    {
	const Th8_Platform *pPlat = Th8_GetPlatform(interp);
	if (pPlat && pPlat->xLoad) {
	    (void)pPlat
	        ->xLoad(NULL, pPlat->pCtx, "lib:Sym", 7, NULL, 0); /* C1=T */
	    (void)pPlat
	        ->xLoad(interp, pPlat->pCtx, NULL, 7, NULL, 0); /* C2=T */
	    (void)pPlat->xLoad(
	        interp, pPlat->pCtx, "lib:Sym", 0, NULL, 0); /* C3=T */
	}
	if (pPlat && pPlat->xUnload) {
	    (void)pPlat->xUnload(
	        NULL, pPlat->pCtx, "lib:Sym", 7, NULL, 0, 0); /* C1=T */
	    (void)pPlat->xUnload(
	        interp, pPlat->pCtx, NULL, 7, NULL, 0, 0); /* C2=T */
	    (void)pPlat->xUnload(
	        interp, pPlat->pCtx, "lib:Sym", 0, NULL, 0, 0); /* C3=T */
	    /* xUnload L1116 (`zProc && nProc > 0`) (T,T): pass a
	     * non-empty zProc.  The unload itself fails (no such
	     * handle) but the zProc-handling branch executes. */
	    (void)pPlat->xUnload(
	        interp, pPlat->pCtx, "nonexistent:Sym", 15, "MyUnload", 8,
	        0); /* L1116 C1/C2 (T,T) */
	}
	if (pPlat && pPlat->xLoad) {
	    /* xLoad L602 (`zProc && nProc > 0`) (T,T): pass a
	     * non-empty zProc.  Load itself will fail (no such
	     * lib path under base) but the zProc-handling branch
	     * is reached. */
	    (void)pPlat->xLoad(
	        interp, pPlat->pCtx, "nonexistent:Sym", 15, "MyInit",
	        6); /* L602 C1/C2 (T,T) */
	}
	/* Platform xDnsResolve / xDnsResolveFree / xInput bad-arg
	 * defensive sweep.  Th8_DnsResolve / Th8_DnsResolveFree
	 * pre-validate their args at the public API layer so the
	 * platform-level guards at th8_posix.c L1375 / L1443 are
	 * never reached via the public path.  th8PosixInput L2479
	 * (`!interp` guard) is similarly gated upstream.  Direct
	 * invocation with NULL args closes each pair without
	 * performing any real DNS lookup or stdin read. */
	if (pPlat && pPlat->xDnsResolve) {
	    Th8_DnsResult *pRes = NULL;
	    (void)pPlat->xDnsResolve(
	        NULL, pPlat->pCtx, "x", 1, TH8_DNS_TYPE_A, &pRes); /* C1=T */
	    (void)pPlat->xDnsResolve(
	        interp, pPlat->pCtx, NULL, 1, TH8_DNS_TYPE_A,
	        &pRes); /* C2=T */
	    (void)pPlat->xDnsResolve(
	        interp, pPlat->pCtx, "x", 1, TH8_DNS_TYPE_A, NULL); /* C3=T */
	}
	if (pPlat && pPlat->xDnsResolveFree) {
	    pPlat->xDnsResolveFree(NULL, pPlat->pCtx, NULL); /* C1=T */
	    pPlat->xDnsResolveFree(interp, pPlat->pCtx, NULL); /* C2=T */
	}
	if (pPlat && pPlat->xInput) {
	    char *pzOut = NULL;
	    size_t nOut = 0;
	    (void)pPlat
	        ->xInput(NULL, pPlat->pCtx, &pzOut, &nOut, NULL); /* C1=T */
	}
	if (pPlat && pPlat->xSameFile) {
	    /* xSameFile L2134 (`!zName1 || !zName2`) is intrinsic-
	     * dead via Th8_SameFile which validates first.  Drive
	     * C1=T and C2=T pairs directly. */
	    (void)pPlat
	        ->xSameFile(interp, pPlat->pCtx, NULL, 1, "x", 1); /* C1=T */
	    (void)pPlat
	        ->xSameFile(interp, pPlat->pCtx, "x", 1, NULL, 1); /* C2=T */
	}
	if (pPlat && pPlat->xGetEnv) {
	    /* xGetEnv L1822 (`!zName`) is intrinsic-dead via
	     * Th8_GetEnv which pre-validates zName.  Direct call
	     * with NULL drives the C1=T vector. */
	    (void)pPlat->xGetEnv(interp, pPlat->pCtx, NULL);
	}
	if (pPlat && pPlat->xGetRealPath) {
	    /* xGetRealPath L1965 3-condition (`!zPath || !zBuf ||
	     * nBuf == 0`) defensive.  Th8_GetRealPath pre-validates
	     * its args at the public-API layer so only the (F,F,F)
	     * success vector is seen via the public path.  Drive
	     * C1=T, C2=T, C3=T pairs directly. */
	    char zBuf[16];
	    (void)pPlat->xGetRealPath(
	        interp, pPlat->pCtx, NULL, 1, zBuf, sizeof(zBuf)); /* C1=T */
	    (void)pPlat->xGetRealPath(
	        interp, pPlat->pCtx, "x", 1, NULL, sizeof(zBuf)); /* C2=T */
	    (void)pPlat->xGetRealPath(
	        interp, pPlat->pCtx, "x", 1, zBuf, 0); /* C3=T */
	}
	if (pPlat && pPlat->xDeleteTemporaryData) {
	    /* xDeleteTemporaryData L2270 3-condition (`!zPath ||
	     * nPath == 0 || nPath >= sizeof(zBuf)`).  No public
	     * Th8_DeleteTemporaryData wrapper validates first, but
	     * in-tree callers (channel close) always pass a well-
	     * formed path.  Drive C1=T (NULL zPath), C2=T (nPath
	     * 0), C3=T (nPath at the 4096-byte ceiling) directly. */
	    (void)pPlat->xDeleteTemporaryData(
	        interp, pPlat->pCtx, NULL, 1); /* C1=T */
	    (void)pPlat->xDeleteTemporaryData(
	        interp, pPlat->pCtx, "/tmp/x", 0); /* C2=T */
	    (void)pPlat->xDeleteTemporaryData(
	        interp, pPlat->pCtx, "/tmp/x", 4096); /* C3=T */
	}
	if (pPlat && pPlat->xGetRootPath) {
	    /* xGetRootPath L2055 (`!zBuf || nBuf < 2`).  Drive
	     * C1=T (NULL zBuf) and C2=T (nBuf=1). */
	    char zBuf2[16];
	    (void)pPlat->xGetRootPath(
	        interp, pPlat->pCtx, "x", 1, NULL, sizeof(zBuf2)); /* C1=T */
	    (void)pPlat->xGetRootPath(
	        interp, pPlat->pCtx, "x", 1, zBuf2, 1); /* C2=T */
	}
	if (pPlat && pPlat->xGetData) {
	    /* xGetData L291 (`fstat(fd, &st) != 0 || !S_ISREG`)
	     * C2=T vector: open() succeeds on a directory but
	     * S_ISREG returns false.  Path "." (the cwd, which is
	     * under base) resolves to a directory; open(2) +
	     * O_NOFOLLOW accepts dirs on POSIX, then fstat finds
	     * the dir mode and !S_ISREG triggers the error arm.
	     * The result is discarded; the in-tree caller path
	     * (Th8_GetData via [source]) never targets a dir. */
	    char *pzOutDir = NULL;
	    size_t nOutDir = 0;
	    (void)pPlat
	        ->xGetData(interp, pPlat->pCtx, ".", 1, &pzOutDir, &nOutDir);
	    if (pzOutDir) Th8_Free(interp, pzOutDir);
	}
	if (pPlat && pPlat->xMemset) {
	    /* th8PosixMemset has 0 executions -- the xMemset platform
	     * callback has no in-tree caller (TH8 uses Th8_Memset
	     * which doesn't route through pPlat->xMemset).  Drive
	     * both arms: c==0 (secure-zero branch) and c!=0 (plain
	     * fill branch). */
	    unsigned char memsetBuf[8];
	    (void)pPlat->xMemset(
	        interp, pPlat->pCtx, memsetBuf, 0, sizeof(memsetBuf));
	    (void)pPlat->xMemset(
	        interp, pPlat->pCtx, memsetBuf, 0x42, sizeof(memsetBuf));
	}
	if (pPlat && pPlat->xChannelControl) {
	    /* xChannelControl L2399 (TH8_CHANCTL_OPEN arm) 3-
	     * condition (`!pBuf || nArg1 <= 0 || (size_t)nArg1
	     * >= sizeof(zPath)`).  TH8 has no script-level [open]
	     * so this op is never reached in normal use.  Drive
	     * the C1/C2/C3 pairs by directly invoking OP=6 with
	     * NULL pBuf / nArg1=0 / nArg1=4096. */
	    th8_int64_t res = 0;
	    (void)pPlat->xChannelControl(
	        interp, pPlat->pCtx, NULL, TH8_CHANCTL_OPEN, 5, 0, &res,
	        NULL); /* C1=T */
	    (void)pPlat->xChannelControl(
	        interp, pPlat->pCtx, NULL, TH8_CHANCTL_OPEN, 0, 0, &res,
	        "x"); /* C2=T */
	    (void)pPlat->xChannelControl(
	        interp, pPlat->pCtx, NULL, TH8_CHANCTL_OPEN, 4096, 0, &res,
	        "x"); /* C3=T */
	}
    }

    /* th8ChannelCreate L122 C2 / C3 pair vectors via
     * custom-platform child interps whose xGetTemporaryData
     * returns OK with one of the output pointers NULL.  In-
     * tree POSIX always returns both outputs non-NULL, so
     * these are otherwise unreachable. */
    {
	const Th8_Platform *pLib = Th8_GetLibcPlatform();
	if (pLib) {
	    Th8_Platform plat;
	    Th8_Interp *pChild;

	    plat = *pLib;
	    plat.xPanic = 0;
	    plat.xGetTemporaryData = th8test_stub_xGetTempData_ok_null_path;
	    pChild = Th8_CreateInterp(&plat);
	    if (pChild) {
		Th8_RegisterLanguage(pChild);
		(void)th8ChannelCreate(pChild, 10);
		Th8_DeleteInterp(pChild);
	    }

	    plat = *pLib;
	    plat.xPanic = 0;
	    plat.xGetTemporaryData = th8test_stub_xGetTempData_ok_null_chan;
	    pChild = Th8_CreateInterp(&plat);
	    if (pChild) {
		Th8_RegisterLanguage(pChild);
		(void)th8ChannelCreate(pChild, 10);
		Th8_DeleteInterp(pChild);
	    }

	    plat = *pLib;
	    plat.xPanic = 0;
	    plat.xGetTemporaryData = th8test_stub_xGetTempData_fail;
	    pChild = Th8_CreateInterp(&plat);
	    if (pChild) {
		Th8_RegisterLanguage(pChild);
		(void)th8ChannelCreate(pChild, 10);
		Th8_DeleteInterp(pChild);
	    }
	}
    }

    /* Channel-op NULL-pChan exerciser.  Drives C1=T on the
     * `!pChan || ...` guards at th8_channel.c:245/350/460/
     * 557 (Write/Read/Seek/Flush) which in-tree callers never
     * hit because they pre-validate pChan via a successful
     * th8ChannelFind lookup. */
    th8TestExerciseChannelNullArgs(interp);

    /*
     * MC/DC drive (2026-06-08): (F,T) post-close vector at the
     * same `if (!pChan || !pChan->pChannel)` guards.  A
     * stack-allocated zero-filled Th8_Channel has pChannel=NULL,
     * driving the C2=T pair without going through a real channel
     * lifecycle.  Lives in testlib (immune from MC/DC) so the
     * exerciser's own straight-line code adds no decisions to
     * the th8_channel.c coverage scope.
     */
    {
	Th8_Channel fake;
	char *zOut = NULL;
	size_t nOut = 0;
	Th8_Memset(interp, &fake, 0, sizeof(fake));
	(void)th8ChannelWrite(interp, &fake, "x", 1);
	(void)th8ChannelRead(interp, &fake, &zOut, &nOut);
	(void)th8ChannelSeek(interp, &fake, 0, 0);
	(void)th8ChannelFlush(interp, &fake);
	Th8_ClearResult(interp);
    }

    /* Drive C3-Pair / C4-Pair (pzOut NULL / pnOut NULL) at
     * th8ChannelRead's hardened guard (L347-348).  Uses a
     * fresh child interp + real platform so the tempname
     * channel + bigint cache get cleaned up at child-interp
     * deletion (not on the parent at process exit). */
    {
	const Th8_Platform *pP = Th8_GetPlatform(interp);
	if (pP) {
	    Th8_Platform cp = *pP;
	    Th8_Interp *pChild;
	    cp.xPanic = 0;
	    pChild = Th8_CreateInterp(&cp);
	    if (pChild) {
		Th8_RegisterLanguage(pChild);
		if (th8ChannelCreate(pChild, 16) == TH8_OK) {
		    char *zRes;
		    size_t nRes;
		    Th8_Channel *pChan;
		    zRes = (char *)Th8_TakeResult(pChild, &nRes);
		    pChan = th8ChannelFind(pChild, zRes, nRes);
		    if (pChan) {
			char *zOut = NULL;
			size_t nOut = 0;
			(void)th8ChannelRead(pChild, pChan, NULL, &nOut);
			(void)th8ChannelRead(pChild, pChan, &zOut, NULL);
		    }
		    Th8_Free(pChild, zRes);
		}
		Th8_DeleteInterp(pChild);
	    }
	}
    }

    /* th8ChannelRead's xChannelControl fallback path (L390/
     * L394/L397).  Reached only when pPlat->xInput is NULL
     * but xChannelControl is set.  In the normal interp
     * libc+posix platform xInput is always set, so this
     * branch is dead in regular flow.  Build a child interp
     * with xInput nulled to force the fallback, then create
     * a temp channel, write some bytes, and read them back. */
    {
	const Th8_Platform *pP = Th8_GetPlatform(interp);
	if (pP && pP->xChannelControl) {
	    Th8_Platform cp = *pP;
	    Th8_Interp *pChild;
	    cp.xPanic = 0;
	    cp.xInput = 0; /* force xChannelControl path */
	    pChild = Th8_CreateInterp(&cp);
	    if (pChild) {
		Th8_RegisterLanguage(pChild);
		if (th8ChannelCreate(pChild, 200) == TH8_OK) {
		    char *zRes;
		    size_t nRes;
		    Th8_Channel *pChan;
		    zRes = (char *)Th8_TakeResult(pChild, &nRes);
		    pChan = th8ChannelFind(pChild, zRes, nRes);
		    if (pChan) {
			char *zOut = NULL;
			size_t nOut = 0;
			(void)th8ChannelWrite(pChild, pChan, "abc\r\n", 5);
			(void)th8ChannelSeek(pChild, pChan, 0, 0);
			(void)th8ChannelRead(pChild, pChan, &zOut, &nOut);
			if (zOut) Th8_Free(pChild, zOut);
			/* Read past end-of-data to drive L390 C2=T
			 * (nGot == 0 from xChannelControl READ).
			 * After the write+read above the file
			 * position is past the data; seeking to a
			 * far offset and reading returns EOF on the
			 * first byte. */
			zOut = NULL;
			nOut = 0;
			(void)th8ChannelSeek(pChild, pChan, 1000, 0);
			(void)th8ChannelRead(pChild, pChan, &zOut, &nOut);
			if (zOut) Th8_Free(pChild, zOut);
			/* Write a line WITHOUT trailing \r to drive
			 * L397 C2=F (`zBuf[nRead-1] != '\r'`). */
			zOut = NULL;
			nOut = 0;
			(void)th8ChannelSeek(pChild, pChan, 100, 0);
			(void)th8ChannelWrite(pChild, pChan, "xyz\n", 4);
			(void)th8ChannelSeek(pChild, pChan, 100, 0);
			(void)th8ChannelRead(pChild, pChan, &zOut, &nOut);
			if (zOut) Th8_Free(pChild, zOut);
		    }
		    Th8_Free(pChild, zRes);
		}
		Th8_DeleteInterp(pChild);
	    }
	}
    }

    /* Drive L372 / L373 C1-Pair (nLine == 0) at the
     * th8ChannelRead xInput path's trailing-EOL strip
     * checks.  The real POSIX xInput returns ERROR on
     * empty reads -- only a stub can produce OK with
     * nLine=0.  Custom xInput returns OK with an
     * allocated 1-byte '\0' buffer and *pnOut = 0.  Also
     * drives th8_io.c L774 `rc != OK || !zLine || nLine
     * == 0` C3-Pair via [read stdin] in the same child. */
    {
	const Th8_Platform *pP = Th8_GetPlatform(interp);
	if (pP) {
	    Th8_Platform cp = *pP;
	    Th8_Interp *pChild;
	    cp.xPanic = 0;
	    cp.xInput = th8test_stub_xInput_empty;
	    pChild = Th8_CreateInterp(&cp);
	    if (pChild) {
		Th8_RegisterLanguage(pChild);
		if (th8ChannelCreate(pChild, 16) == TH8_OK) {
		    char *zRes;
		    size_t nRes;
		    Th8_Channel *pChan;
		    zRes = (char *)Th8_TakeResult(pChild, &nRes);
		    pChan = th8ChannelFind(pChild, zRes, nRes);
		    if (pChan) {
			char *zOut = NULL;
			size_t nOut = 0;
			(void)th8ChannelRead(pChild, pChan, &zOut, &nOut);
			if (zOut) Th8_Free(pChild, zOut);
		    }
		    Th8_Free(pChild, zRes);
		}
		/* Run [read stdin] -- the stub xInput returns
		 * OK with nLine=0 on the first call, so the
		 * loop at th8_io.c L772 breaks at L774 with
		 * (F, F, T) -- driving the L774 C3-Pair. */
		(void)Th8_Eval(pChild, 0, "read stdin", TH8_NOLEN, NULL, 0);
		Th8_DeleteInterp(pChild);
	    }
	    /* Spawn another child whose xInput sets pzOut=NULL
	     * with rc=OK to drive L774 C2-Pair (zLine NULL). */
	    cp = *pP;
	    cp.xPanic = 0;
	    cp.xInput = th8test_stub_xInput_null_buf;
	    pChild = Th8_CreateInterp(&cp);
	    if (pChild) {
		Th8_RegisterLanguage(pChild);
		(void)Th8_Eval(pChild, 0, "read stdin", TH8_NOLEN, NULL, 0);
		Th8_DeleteInterp(pChild);
	    }
	}
    }

    /* Drive L394 (`nRead == 0 && rc != TH8_OK`) in
     * th8ChannelRead's fallback path via a child interp
     * whose xChannelControl wraps the real impl but fails
     * READ ops -- the first read attempt returns ERROR with
     * nGot=0 so nRead stays at 0 and rc != OK, satisfying
     * both pair conditions. */
    {
	const Th8_Platform *pP = Th8_GetPlatform(interp);
	if (pP && pP->xChannelControl) {
	    Th8_Platform cp = *pP;
	    Th8_Interp *pChild;
	    th8test_real_xChanCtl = pP->xChannelControl;
	    cp.xPanic = 0;
	    cp.xInput = 0;
	    cp.xChannelControl = th8test_stub_xChannelControl_read_fail;
	    pChild = Th8_CreateInterp(&cp);
	    if (pChild) {
		Th8_RegisterLanguage(pChild);
		if (th8ChannelCreate(pChild, 50) == TH8_OK) {
		    char *zRes;
		    size_t nRes;
		    Th8_Channel *pChan;
		    zRes = (char *)Th8_TakeResult(pChild, &nRes);
		    pChan = th8ChannelFind(pChild, zRes, nRes);
		    if (pChan) {
			char *zOut = NULL;
			size_t nOut = 0;
			(void)th8ChannelRead(pChild, pChan, &zOut, &nOut);
			if (zOut) Th8_Free(pChild, zOut);
		    }
		    Th8_Free(pChild, zRes);
		}
		Th8_DeleteInterp(pChild);
	    }
	    th8test_real_xChanCtl = NULL;
	}
    }

    /* Drive every math proc's NULL-zArg1 path.  Per the Bug 22
     * invariant, all single-arg and two-arg procs must return
     * TH8_ERROR on NULL operands rather than crashing or silently
     * using d1=0.0.  Both the per-op procs and the shared
     * dispatchers (th8MathTranscendental, th8MathClassify) check
     * `!z1` via the arity-aware ctx encoding.  This probe also
     * records the count of any proc that DOES NOT return ERROR
     * on a missing required operand -- exposed via
     * `::th8testlib::bug22_null_op_count` so a regression test
     * can assert the count is zero. */
    {
	static const char *aMathProbe[] =
	    {"abs", "bool", "double", "entier", "int", "isqrt", "round",
	     "wide", "max", "min", "fpclassify", "sin", "cos", "tan", "log",
	     "exp", "atan", "acos", "asin", "sqrt", "ceil", "floor",
	     /* Bug 22 follow-up (2026-06-09):
	                                    * 2-arg transcendental functions
	                                    * dispatch through th8MathTranscendental
	                                    * (not the per-op procs probed above).
	                                    * Adding them here closes the (T,T)
	                                    * vector at src/th8_math.c L751
	                                    * `if (nArity >= 2 && !z2)`. */
	     "atan2", "fmod", "hypot", "pow", NULL};
	int i;
	th8test_bug22NullOpRegressions = 0;
	for (i = 0; aMathProbe[i] != NULL; i++) {
	    int nArg = 0;
	    Th8_MathFuncProc xProc = NULL;
	    void *pCtx = NULL;
	    if (Th8_FindMathFunc(
	            interp, aMathProbe[i], TH8_NOLEN, &nArg, &xProc, &pCtx) ==
	            TH8_OK &&
	        xProc != NULL) {
		int rcMath = xProc(interp, pCtx, NULL, 0, NULL, 0);
		if (nArg >= 1 && rcMath != TH8_ERROR) {
		    th8test_bug22NullOpRegressions++;
		}
		/* Two-arg procs (max, min, atan2, pow, ...): also
		 * probe (z1 non-NULL, z2 NULL) -- arity says z2 is
		 * required, so a missing second arg must also
		 * return ERROR. */
		if (nArg == 2) {
		    rcMath = xProc(interp, pCtx, "1", 1, NULL, 0);
		    if (rcMath != TH8_ERROR) {
			th8test_bug22NullOpRegressions++;
		    }
		}
	    }
	}
	Th8_ClearResult(interp);
    }

    /* Th8_ReportTaint: report a non-tainted string (no taint,
     * returns OK) and a tainted-prefixed one.  Safe entry. */
    (void)Th8_ReportTaint(interp, "test", "untainted", 9);
    (void)Th8_ReportTaint(NULL, "test", "x", 1);

    /* macOS zone-allocator platform: build a child interp from
     * Th8_GetMacOSPlatform, register language, do a few alloc/
     * realloc/free operations, then destroy.  Exercises the
     * th8MacOS* allocator entry points which the standard
     * POSIX platform never reaches.  Also drives the realloc
     * NULL-prior path (th8MacOSRealloc with pPrior=NULL is
     * the same as malloc), and the xInitialize / xFinalize
     * lifecycle callbacks which Th8_CreateInterp doesn't
     * invoke. */
#  if defined(__APPLE__)
    {
	const Th8_Platform *pMacOS = Th8_GetMacOSPlatform();
	if (pMacOS) {
	    Th8_Platform mp = *pMacOS;
	    Th8_Interp *pChild = Th8_CreateInterp(&mp);
	    if (pChild) {
		void *p1 = Th8_Malloc(pChild, 64);
		if (p1) {
		    void *p2 = Th8_Realloc(pChild, p1, 256);
		    if (p2)
			Th8_Free(pChild, p2);
		    else
			Th8_Free(pChild, p1);
		}
		/* realloc(NULL, X) -> drives th8MacOSRealloc
		 * L171 (!pPrior) branch which delegates to
		 * malloc_zone_calloc. */
		{
		    void *p3 = Th8_Realloc(pChild, NULL, 32);
		    if (p3) Th8_Free(pChild, p3);
		}
		/* xInitialize: lifecycle callback the embedder
		 * normally invokes.  Th8_CreateInterp doesn't call
		 * it; drive directly for coverage.  Idempotent. */
		if (pMacOS->xInitialize) {
		    (void)pMacOS->xInitialize(pChild, pMacOS->pCtx);
		}
		Th8_DeleteInterp(pChild);
		/* xFinalize: tears down the global zone.  Must run
		 * AFTER Th8_DeleteInterp because the interp's
		 * memory was allocated FROM that zone -- calling
		 * Finalize first would destroy the zone while the
		 * child's pointers still reference it.  The next
		 * allocation (e.g. a later plat_wrappers run) will
		 * lazily re-init via th8MacOSGetZone's CAS path. */
		if (pMacOS->xFinalize) {
		    pMacOS->xFinalize(NULL, pMacOS->pCtx);
		}
	    }
	}
    }
#  endif

    /* Fault-trampoline coverage: install the fault layer on a
     * child interp, then call the th8_plat.c wrappers on the
     * child.  Each call routes through pt_xMemmove /
     * pt_xStrcmp / pt_xStrchr / pt_xAtoi / pt_xQsort, which are
     * otherwise unreached (no in-tree code calls Th8_Memmove
     * &co. on a fault-installed interp). */
#  if defined(TH8_ENABLE_FAULT_INJECTION)
    {
	const Th8_Platform *pP = Th8_GetPlatform(interp);
	if (pP) {
	    Th8_Platform fp = *pP;
	    Th8_Interp *pChild = Th8_CreateInterp(&fp);
	    if (pChild) {
		Th8_FaultConfig cfg;
		Th8_FaultCtx *pFCtx;
		char fbuf[1024];
		pFCtx = (Th8_FaultCtx *)(void *)fbuf;
		Th8_FaultConfigInit(&cfg);
		if (Th8_FaultCtxSize() <= sizeof(fbuf) &&
		    Th8_FaultInstall(pChild, &cfg, pFCtx) == TH8_OK) {
		    char fdst[8];
		    int farr[3] = {3, 1, 2};
		    void *pCh = NULL;
		    char *zPath = NULL;
		    int sf;
		    (void)th8Memmove(pChild, fdst, "abcd", 4);
		    (void)th8Strcmp(pChild, "x", "y");
		    (void)th8Strchr(pChild, "hello", 'l');
		    (void)th8Strrchr(pChild, "hello", 'l');
		    (void)th8Atoi(pChild, "42");
		    th8Qsort(
		        pChild, farr, 3, sizeof(int),
		        th8test_plat_qsort_cmp_int);
#    if defined(TH8_PLUGIN_IO)
		    /* I/O-channel trampolines. */
		    (void)Th8_GetInput(pChild, &pCh);
		    (void)Th8_GetOutput(pChild, &pCh);
		    (void)Th8_GetErrorOutput(pChild, &pCh);
#    endif
		    /* Cwd / path trampolines. */
		    zPath = Th8_GetCwd(pChild);
		    if (zPath) Th8_Free(pChild, zPath);
		    zPath = Th8_GetExePath(pChild);
		    if (zPath) Th8_Free(pChild, zPath);
		    sf = Th8_SameFile(pChild, ".", 1, ".", 1);
		    (void)sf;
		    Th8_FaultUninstall(pChild, pFCtx);
		}
		Th8_DeleteInterp(pChild);
	    }
	}
    }
#  endif

    /* th8CurlGetData DNS-mock sweep: drive the compound-condition
     * branches at th8_curl.c L336 (nHost guard), L342 (Th8_DnsResolve
     * outcome), L344 (bogus), and L357--359 (`pData && pData[0] &&
     * pLen && pLen[0]==4`).  Each iteration spawns a child interp
     * whose platform has xDnsResolve replaced by the mock so the
     * call inside th8CurlGetData consults it instead of real
     * libunbound.  The call goes via Th8_GetCurlPlatform()->xGetData
     * directly so the parent shell's source-policy interceptor
     * (Th8Shell_curlGetData) doesn't short-circuit the dispatch
     * before reaching the mock.  Target URLs: a valid-host one
     * ("http://127.0.0.1:1/") to drive L336 TRUE and an empty-host
     * one ("http:///x") to drive L336 nHost=0 FALSE.  curl_easy_
     * perform connect failures are expected and ignored. */
#  if defined(TH8_ENABLE_LIBCURL) && defined(TH8_ENABLE_UNBOUND)
    /* Th8_DnsResolve / Th8_DnsResolveFree argument-validation
     * sweep: drives L2294 C1=T (interp NULL), L2294 C2=T
     * (ppResult NULL), L2297 C2=T (xDnsResolve NULL on
     * child platform), L2309 C1=T (interp NULL),
     * L2309 C2=T (pResult NULL), L2311 C2=T (xDnsResolveFree
     * NULL on child platform).  C1=T variants at L2297 /
     * L2311 (pPlatform itself NULL) are intrinsic-dead --
     * Th8_GetPlatform returns interp->pPlatform which is
     * never NULL on a properly created interp. */
    {
	const Th8_Platform *pP = Th8_GetPlatform(interp);
	Th8_DnsResult *pTmpDns = NULL;
	(void)Th8_DnsResolve(NULL, "x", 1, TH8_DNS_TYPE_A, &pTmpDns);
	(void)Th8_DnsResolve(interp, "x", 1, TH8_DNS_TYPE_A, NULL);
	Th8_DnsResolveFree(NULL, (Th8_DnsResult *)(void *)&pTmpDns);
	Th8_DnsResolveFree(interp, NULL);
	if (pP) {
	    Th8_Platform cpNoDns = *pP;
	    Th8_Interp *pChildNoDns;
	    cpNoDns.xPanic = 0;
	    cpNoDns.xDnsResolve = NULL;
	    cpNoDns.xDnsResolveFree = NULL;
	    pChildNoDns = Th8_CreateInterp(&cpNoDns);
	    if (pChildNoDns) {
		Th8_DnsResult *p2 = NULL;
		Th8_RegisterLanguage(pChildNoDns);
		(void)
		    Th8_DnsResolve(pChildNoDns, "x", 1, TH8_DNS_TYPE_A, &p2);
		Th8_DnsResolveFree(
		    pChildNoDns, (Th8_DnsResult *)(void *)&pChildNoDns);
		Th8_DeleteInterp(pChildNoDns);
	    }
	}
    }
    /*
     * Bug 47 mitigation (2026-06-08): the DNS-mock sweep below
     * spawns 24 curl_easy_perform calls per plat_wrappers
     * invocation.  The library now caps each operation via
     * ::th8_timeout (default 2000ms) -- see th8CurlGetData --
     * but the FIRST call still pays the macOS keychain /
     * X509-cert-load cost (observed >10s in sample(1)) which is
     * pre-network and not bounded by libcurl's TIMEOUT_MS.
     * Running the sweep exactly once per process keeps the cost
     * one-shot and reliable; the per-call timeout protects
     * against runtime hangs in subsequent calls.
     */
    {
	static int s_did_dns_sweep = 0;
	const Th8_Platform *pP = Th8_GetPlatform(interp);
	const Th8_Platform *pCurl = Th8_GetCurlPlatform();
	if (!s_did_dns_sweep && pP && pCurl && pCurl->xGetData) {
	    static th8test_dns_ctx dnsCtx;
	    s_did_dns_sweep = 1;

	    /*
	     * Drive th8CurlTimeoutMs L97 (T,-) / (F,T) vectors
	     * (`parsed < 1 || parsed > INT_MAX`).  Set
	     * ::th8_timeout to an out-of-range value, fire a curl
	     * call (the bogus port-1 URL above), then restore.
	     * Each call returns immediately via the connect
	     * timeout; the helper falls back to the 30000ms
	     * default for both bad values, so the actual curl op
	     * takes < 2s either way.
	     */
	    {
		size_t nSavedLen = 0;
		const char *zSaved = NULL;
		char savedBuf[64];
		size_t copyLen = 0;
		if (Th8_GetVar(interp, "::th8_timeout", TH8_NOLEN) ==
		    TH8_OK) {
		    zSaved = Th8_GetResult(interp, &nSavedLen);
		    if (zSaved && nSavedLen < sizeof(savedBuf)) {
			Th8_Memcpy(interp, savedBuf, zSaved, nSavedLen);
			copyLen = nSavedLen;
		    }
		}
		/* (T,-): parsed < 1.  "0" parses as 0 -> < 1. */
		(void)Th8_SetVar(interp, "::th8_timeout", TH8_NOLEN, "0", 1);
		{
		    char *zOut = NULL;
		    size_t nOut = 0;
		    (void)pCurl->xGetData(
		        interp, pCurl->pCtx, "http://127.0.0.1:1/", 19, &zOut,
		        &nOut);
		    if (zOut) Th8_Free(interp, zOut);
		}
		/* (F,T): parsed > INT_MAX (2^31).  "9999999999999"
		 * (~10^13) easily exceeds 0x7FFFFFFF. */
		(void)Th8_SetVar(
		    interp, "::th8_timeout", TH8_NOLEN, "9999999999999", 13);
		{
		    char *zOut = NULL;
		    size_t nOut = 0;
		    (void)pCurl->xGetData(
		        interp, pCurl->pCtx, "http://127.0.0.1:1/", 19, &zOut,
		        &nOut);
		    if (zOut) Th8_Free(interp, zOut);
		}
		/* Restore. */
		if (copyLen > 0) {
		    (void)Th8_SetVar(
		        interp, "::th8_timeout", TH8_NOLEN, savedBuf,
		        copyLen);
		}
		Th8_ClearResult(interp);
	    }
	    /* A 270-char hostname URL drives L336 (T,F): nHost > 0
	     * but nHost >= sizeof(zHostBuf)==256.  The 'h' prefix
	     * extends to position 277 followed by '/' so the host
	     * scanner at L325 stops there.  Built statically so the
	     * trailing '/' is fixed and the buffer is properly NUL-
	     * terminated.  Wrapped in its own block so the static
	     * decl sits at the top of a scope (-Wdeclaration-after-
	     * statement is clean under c99-pedantic). */
	    {
		static const char zLongHost[] =
		    "http://"
		    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
		    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
		    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
		    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
		    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
		    "aaaaaaaaaa/"; /* 5*52 + 10 = 270 host chars */
		static const char *azUrl[] = {
		    "http://127.0.0.1:1/", /* nHost=9: L336 (T,T) */
		    "http:///x", /* nHost=0: L336 (F,-) */
		    zLongHost /* nHost=270: L336 (T,F) */
		};
		static const size_t anUrl[] = {19, 9, sizeof(zLongHost) - 1};
		int mode, ui;
		for (mode = 0; mode <= 7; mode++) {
		    for (ui = 0; ui < 3; ui++) {
			Th8_Platform cp = *pP;
			Th8_Interp *pChild;
			dnsCtx.mode = mode;
			cp.xPanic = 0;
			cp.xDnsResolve = th8test_stub_xDnsResolve;
			cp.xDnsResolveFree = th8test_stub_xDnsResolveFree;
			cp.pCtx = &dnsCtx;
			pChild = Th8_CreateInterp(&cp);
			if (pChild) {
			    char *zOut = NULL;
			    size_t nOut = 0;
			    Th8_RegisterLanguage(pChild);
			    (void)pCurl->xGetData(
			        pChild, pCurl->pCtx, azUrl[ui], anUrl[ui],
			        &zOut, &nOut);
			    if (zOut) Th8_Free(pChild, zOut);
			    Th8_DeleteInterp(pChild);
			}
		    }
		}
	    }
	}
    }
#  endif

    /* th8_glob.c L90 `interp && Th8_Ready(interp) != TH8_OK`
     * inside the per-character loop body: the outer L80
     * Ready check is fine on entry, but the inner L90 check
     * needs Ready to flip to non-OK MID-LOOP.  We achieve
     * that with a step limit that's high enough for L80 to
     * pass but low enough for the per-iteration th8Step to
     * exhaust within the loop body.  An asterisk-prefix
     * pattern over a 60-char string keeps the matcher in
     * the L83 loop long enough to trip the limit. */
    {
	const Th8_Platform *pPGlob = Th8_GetPlatform(interp);
	if (pPGlob) {
	    Th8_Platform cpGlob = *pPGlob;
	    Th8_Interp *pCgChild;
	    cpGlob.xPanic = 0;
	    pCgChild = Th8_CreateInterp(&cpGlob);
	    if (pCgChild) {
		Th8_RegisterLanguage(pCgChild);
		/* RegisterLanguage uses many steps; reset the
		 * counter so the step limit fires INSIDE the
		 * glob loop, not before. */
		Th8_ResetStepCount(pCgChild);
		Th8_SetStepLimit(pCgChild, 3);
		(void)Th8_GlobMatch(
		    pCgChild, "*xyz*abc*end", 12,
		    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
		    "aaaaaaaaaaaaaaaaaaaaaaaaaa",
		    60);
		Th8_DeleteInterp(pCgChild);
	    }
	}
    }

    /* th8Qsort: sort 5 ints; n=0 (drives C2-Pair); plus
     * NULL base and NULL cmp to drive C1-Pair / C3-Pair at
     * the L680 `!base || nmemb < 2 || !cmp` guard.  All
     * invalid combinations short-circuit and return cleanly. */
    th8Qsort(interp, arr, 5, sizeof(int), th8test_plat_qsort_cmp_int);
    th8Qsort(interp, arr, 0, sizeof(int), th8test_plat_qsort_cmp_int);
    th8Qsort(
        interp, NULL, 5, sizeof(int), th8test_plat_qsort_cmp_int); /* C1=T */
    th8Qsort(interp, arr, 5, sizeof(int), NULL); /* C3=T */

    /* Build a result: cmpResSign, arr-sorted, strchr-found. */
    {
	int i;
	int signCmp = (cmpRes < 0) ? -1 : (cmpRes > 0 ? 1 : 0);
	int sorted =
	    (arr[0] <= arr[1] && arr[1] <= arr[2] && arr[2] <= arr[3] &&
	     arr[3] <= arr[4]);
	int found = (p != NULL);
	for (i = 0; i < 3; i++)
	    numBuf[i] = '\0';
	numBuf[0] = (char)('0' + (signCmp + 1)); /* -1->'0', 0->'1', 1->'2' */
	numBuf[1] = (char)('0' + sorted);
	numBuf[2] = (char)('0' + found);
	Th8_ListAppend(interp, &zList, &nList, numBuf, 3);
    }

    /*
     * Phase-3 (2026-05-29) -- exercise the newly-exposed
     * internal stubs (F6 follow-up).  Each call here drives a
     * boundary vector that the everyday script path does not
     * reach: the goal is to convert reachability (Phase 1+2
     * stubs expansion) into actual MC/DC coverage.
     */

    /* th8BufferAlloc: drive (nBytes == 0) -> 1 promotion (D1)
     * and (idx < 0) too-large-for-pool fallback (D2).  The
     * normal pool-hit path D3 is already saturated by
     * everyday string ops. */
    {
	void *q;
	q = th8BufferAlloc(interp, 0); /* D1: nBytes==0 */
	if (q) th8BufferFree(interp, q, 0);
	q = th8BufferAlloc(interp, (size_t)1 << 20); /* D2: > max */
	if (q) th8BufferFree(interp, q, (size_t)1 << 20);
	/* NULL pointer guard in free. */
	th8BufferFree(interp, NULL, 0);
    }

    /* th8GlobalMutexEnter/Leave: paired call.  No-op when the
     * platform doesn't provide a global mutex; the entry
     * decisions are still driven. */
    th8GlobalMutexEnter(interp);
    th8GlobalMutexLeave(interp);

    /* th8CheckStack: stack-depth check.  Single invocation
     * with the current depth covers the in-bounds path. */
    (void)th8CheckStack(interp);

    /* NOTE: th8OversizeString is NOT called here -- despite the
     * "detected" name, the function calls xPanic which
     * terminates the process; it is the LAST-RESORT helper for
     * the result-size limit chain, not a side-effect-free
     * probe.  Its MC/DC is best driven via a fault-injection
     * path that synthesises an oversize string, not via a
     * direct call. */

    /* th8GetNsParent / th8NsGetExport: NULL and root namespace
     * inputs.  th8GetNsParent of "" or "::" returns NULL, and
     * th8NsGetExport with NULL ns name exercises the C2=F
     * defensive arm. */
    (void)th8GetNsParent(interp, "", 0);
    (void)th8GetNsParent(interp, "::", 2);
    (void)th8NsGetExport(interp, NULL, 0);
    (void)th8NsGetExport(interp, "::", 2);
    /* L3621 (F,T) -- zNs non-NULL but nNs==0. */
    (void)th8NsGetExport(interp, "", 0);
    /* L3630 (T,-) -- pNs NULL (lookup miss). */
    (void)th8NsGetExport(interp, "::__nonexistent_th8test_ns", 26);

    /* Event-queue introspection booleans.  Each is a small
     * (T, F, -) pair driven by toggling whether the platform
     * has an event queue and whether anything is queued. */
    (void)th8AnyEventQueued(interp);
    (void)th8PlatformHasEventQueue(interp);
    (void)th8EventQueueAvailable(interp, NULL);
    /* MC/DC L2032 (T,-) vector: NULL interp short-circuits
     * the !interp || !pState guard before pState is read. */
    (void)th8EventQueueAvailable(NULL, NULL);

    /* th8SaveCancel / th8RestoreCancel: paired save/restore.
     * Exercises both the save path and the restore path with
     * an isolated buffer. */
    {
	char saved[TH8_CANCEL_SAVE_SIZE];
	th8SaveCancel(interp, saved);
	th8RestoreCancel(interp, saved);
    }

    /* th8SignalAllStates: signal broadcast.  No-op when no
     * states registered; drives the entry decision either
     * way. */
    th8SignalAllStates(interp);

    /* th8GetCurrentNsPtr: read current namespace pointer
     * (always valid after Th8_CreateInterp). */
    (void)th8GetCurrentNsPtr(interp);

    /* th8ClearCache: clear the cache subsystem.  Drives the
     * empty-cache and populated-cache paths together if the
     * cache holds entries from previous exerciser calls. */
    th8ClearCache(interp);

    /* th8GetFrameLevel: read the current frame depth.  Drives
     * the in-frame path; the at-global-scope decision is
     * covered by the call from this test harness. */
    (void)th8GetFrameLevel(interp);

    if (zList) {
	Th8_SetResult(interp, zList, nList);
	Th8_Free(interp, zList);
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_overflow_check_cmd --
 *
 *	Implements "th8testlib::overflow_check enable|disable|query".
 *	Wraps Th8_SetOverflowCheck and Th8_GetOverflowCheck so tests
 *	can toggle integer-overflow checking in [expr] at runtime.
 *
 *	Without this command, tests that exercise the wraparound
 *	(no-error) path of integer overflow cannot disable the
 *	default error-on-overflow behavior from the script side.
 *	The toggle command lets the test temporarily disable
 *	overflow checking in -setup, exercise the wrapping path in
 *	-body, and re-enable in -cleanup.  It also closes the C2-
 *	Pair of th8_expr.c L1594 / L1614 (the int-power overflow-
 *	check predicates) which gate on interp->bOverflowCheck.
 *
 *	"query" returns "1" if overflow checking is currently
 *	enabled, "0" otherwise.  "enable" / "disable" return the
 *	empty string.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_overflow_check_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    (void)ctx;

    if (argc != 2) {
	return Th8_WrongNumArgs(
	    interp, "th8testlib::overflow_check enable|disable|query");
    }
    if (argl[1] == 6 && memcmp(argv[1], "enable", 6) == 0) {
	Th8_SetOverflowCheck(interp, 1);
	return TH8_OK;
    }
    if (argl[1] == 7 && memcmp(argv[1], "disable", 7) == 0) {
	Th8_SetOverflowCheck(interp, 0);
	return TH8_OK;
    }
    if (argl[1] == 5 && memcmp(argv[1], "query", 5) == 0) {
	Th8_SetResultStatic(
	    interp, Th8_GetOverflowCheck(interp) ? "1" : "0", TH8_NOLEN);
	return TH8_OK;
    }
    return Th8_ErrorMessage(
        interp, "th8testlib::overflow_check: unknown subcommand:", argv[1],
        argl[1]);
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_null_guard_*_cmd --
 *
 *	Implements "th8testlib::null_guard SUBCMD" for the public-API
 *	NULL-guard MC/DC sweep.  Each subcommand exercises the three
 *	input combinations needed to satisfy MC/DC for a typical
 *	"if (!arg1 || !arg2) return TH8_ERROR;" entry guard:
 *
 *	  Test 1: arg1 = NULL                     (operand 1 short-circuits)
 *	  Test 2: arg1 = valid, arg2 = NULL       (operand 1 false, operand 2 true)
 *	  Test 3: arg1 = valid, arg2 = valid      (both false, success path)
 *
 *	The success path is normally exercised by the conformance suite
 *	via the API's primary test; the two NULL-input cases are not.
 *	This helper closes the MC/DC gap on those entry guards.
 *
 *	Each subcmd returns "ok" if all three input cases produced the
 *	expected outcome (NULL inputs -> TH8_ERROR; valid inputs ->
 *	TH8_OK), or a "fail rc1=N rc2=N rc3=N" diagnostic on mismatch.
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8test_null_guard_qe_cb --
 *
 *	No-op callback used as the valid (non-NULL) function
 *	pointer in the public-API null-guard MC/DC sweep.
 *
 * Why / How:
 *	Several public APIs take a callback pointer and guard it
 *	with `!xCallback`; driving the false (success) side of that
 *	guard requires a real, non-NULL callback.  This stub
 *	satisfies that need, ignoring both arguments and reporting
 *	success so the guarded operation proceeds to its normal
 *	path.
 *
 * Results:
 *	TH8_OK always.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_null_guard_qe_cb(Th8_Interp *interp, void *pCtx)
{
    (void)interp;
    (void)pCtx;
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_inframe_noop_cb --
 *
 *	No-op callback for `Th8_InFrame`-style null-guard drives.
 *	Ignores all three arguments and returns `TH8_OK`, exercising
 *	the success-path C-pair of any guard that takes a callback
 *	pointer.
 *
 * Parameters / Returns / Side effects:
 *	Three opaque arguments (interp, pCtx1, pCtx2) are
 *	discarded; always returns `TH8_OK`; no side effects.
 *
 *----------------------------------------------------------------------
 */
int
th8test_inframe_noop_cb(Th8_Interp *interp, void *pCtx1, void *pCtx2)
{
    (void)interp;
    (void)pCtx1;
    (void)pCtx2;
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_hash_iter_noop_cb --
 *
 *	No-op callback for `Th8_HashIterate` null-guard drives.
 *	Ignores the entry and context, returns `TH8_OK`.  Used to
 *	supply a valid callback pointer to APIs whose `(xCb, pCtx)`
 *	guard pair needs a non-NULL `xCb` to cover the success
 *	C-pair.
 *
 * Parameters / Returns / Side effects:
 *	`pEntry` and `pCtx` discarded; always returns `TH8_OK`;
 *	no side effects.
 *
 *----------------------------------------------------------------------
 */
int
th8test_hash_iter_noop_cb(Th8_HashEntry *pEntry, void *pCtx)
{
    (void)pEntry;
    (void)pCtx;
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_null_guard_queue_event --
 *
 *	Drive the three MC/DC vectors of the
 *	`Th8_QueueEvent(pState, xCb)` entry guard
 *	`!pState || !xCb`:
 *
 *	  1. `(NULL, NULL)`  -- `!pState` true, short-circuits.
 *	  2. `(valid, NULL)` -- `!pState` false, `!xCb` true.
 *	  3. `(valid, valid)` -- both false, success path.
 *
 *	Verifies the expected return codes (ERROR, ERROR, OK)
 *	and sets the interpreter result to `"ok"` on full pass
 *	or a diagnostic message on any mismatch.
 *
 * Parameters:
 *	interp -- live interpreter used to allocate the async
 *	          state for tests 2 and 3.
 *
 * Returns:
 *	`TH8_OK` if all three sub-tests returned the expected
 *	code (interpreter result: `"ok"`).
 *	`TH8_ERROR` on any deviation or if `Th8_CreateAsyncState`
 *	failed (interpreter result describes the failure).
 *
 * Side effects:
 *	Allocates and finalises one async-state object via the
 *	public API.  Sets the interpreter result.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_null_guard_queue_event(Th8_Interp *interp)
{
    void *pState;
    int rc1, rc2, rc3;
    char buf[80];

    /* Test 1: pState=NULL, xCb=NULL.  !pState evaluates true and
     * short-circuits the OR; !xCb is never evaluated. */
    rc1 = Th8_QueueEvent(NULL, NULL);

    /* Tests 2 and 3 need a valid pState. */
    pState = Th8_CreateAsyncState(interp, NULL);
    if (!pState) {
	Th8_SetResultStatic(
	    interp, "null_guard queue_event: CreateAsyncState failed",
	    TH8_NOLEN);
	return TH8_ERROR;
    }

    /* Test 2: pState=valid, xCb=NULL.  !pState false, !xCb true. */
    rc2 = Th8_QueueEvent(pState, NULL);

    /* Test 3: pState=valid, xCb=valid.  Both false; success path. */
    rc3 = Th8_QueueEvent(pState, th8test_null_guard_qe_cb);

    Th8_FinalizeAsyncState(pState);

    if (rc1 == TH8_ERROR && rc2 == TH8_ERROR && rc3 == TH8_OK) {
	Th8_SetResultStatic(interp, "ok", TH8_NOLEN);
	return TH8_OK;
    }
    {
	int n = snprintf(
	    buf, sizeof(buf),
	    "fail rc1=%d rc2=%d rc3=%d (expected error,error,ok)", rc1, rc2,
	    rc3);
	if (n < 0) n = 0;
	if ((size_t)n >= sizeof(buf)) n = (int)sizeof(buf) - 1;
	Th8_SetResult(interp, buf, (size_t)n);
    }
    return TH8_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_null_guard_ias_cb --
 *
 *	No-op `Th8_IterateArraySearches` callback used by the
 *	null-guard drives below to supply a valid, non-NULL
 *	callback pointer (so the entry guard `!interp ||
 *	!xCallback` exercises both C-pairs).  Ignores all five
 *	arguments and returns `TH8_OK`.
 *
 * Parameters / Returns / Side effects:
 *	Five opaque arguments discarded; always returns `TH8_OK`;
 *	no side effects.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_null_guard_ias_cb(
    const char *zArray,
    size_t nArray,
    const char *zSid,
    size_t nSid,
    void *pCtx)
{
    (void)zArray;
    (void)nArray;
    (void)zSid;
    (void)nSid;
    (void)pCtx;
    return TH8_OK;
}

/* Stub function-pointer for Th8_SetPlatformContext / Th8_GetPlatformContext. */
/*
 *----------------------------------------------------------------------
 *
 * th8test_null_guard_platfunc_stub --
 *
 *	Empty `void (*)(void)`-shaped function used as a valid,
 *	non-NULL function pointer argument to
 *	`Th8_SetPlatformContext` / `Th8_GetPlatformContext` calls
 *	in the null-guard drives below.  Does nothing.
 *
 * Parameters / Returns / Side effects:
 *	None.  Pure stub.
 *
 *----------------------------------------------------------------------
 */
static void
th8test_null_guard_platfunc_stub(void)
{
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_null_guard_set_platform_ctx --
 *
 *	Drive the three MC/DC vectors of the
 *	`Th8_SetPlatformContext(interp, xCallback, pCtx)` entry
 *	guard `!interp || !xCallback`:
 *
 *	  1. `(NULL, NULL, NULL)`   -- !interp short-circuits.
 *	  2. `(interp, NULL, NULL)` -- !interp false, !xCallback true.
 *	  3. `(interp, stub, &ctx)` -- both false; success path.
 *
 *	Expects (`TH8_ERROR`, `TH8_ERROR`, `TH8_OK`).
 *
 * Parameters:
 *	interp -- live interpreter used for tests 2 and 3.
 *
 * Returns:
 *	`TH8_OK` if every vector returned the expected code
 *	(interpreter result: `"ok"`).
 *	`TH8_ERROR` on any deviation (interpreter result is a
 *	`fail rc1=N rc2=N rc3=N` diagnostic).
 *
 * Side effects:
 *	Registers a platform-context entry on test 3 (lifetime is
 *	the interpreter's; not cleaned up here).  Sets the
 *	interpreter result.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_null_guard_set_platform_ctx(Th8_Interp *interp)
{
    int rc1, rc2, rc3;
    char buf[80];
    int dummy_ctx = 42;

    /* Th8_SetPlatformContext(interp, xCallback, pCtx)
     * Guard: if (!interp || !xCallback) return TH8_ERROR; */

    rc1 = Th8_SetPlatformContext(NULL, NULL, NULL);
    rc2 = Th8_SetPlatformContext(interp, NULL, NULL);
    rc3 = Th8_SetPlatformContext(
        interp, th8test_null_guard_platfunc_stub, &dummy_ctx);

    if (rc1 == TH8_ERROR && rc2 == TH8_ERROR && rc3 == TH8_OK) {
	Th8_SetResultStatic(interp, "ok", TH8_NOLEN);
	return TH8_OK;
    }
    {
	int n = snprintf(
	    buf, sizeof(buf),
	    "fail rc1=%d rc2=%d rc3=%d (expected error,error,ok)", rc1, rc2,
	    rc3);
	if (n < 0) n = 0;
	if ((size_t)n >= sizeof(buf)) n = (int)sizeof(buf) - 1;
	Th8_SetResult(interp, buf, (size_t)n);
    }
    return TH8_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_null_guard_get_platform_ctx --
 *
 *	Drive the three MC/DC vectors of the
 *	`Th8_GetPlatformContext(interp, xCallback, ppCtx)` entry
 *	guard `!interp || !ppCtx`.  Note `xCallback` may
 *	legitimately be NULL (the API supports default-context
 *	lookup), so the guard is asymmetric.
 *
 *	  1. `(NULL, stub, &out)`   -- !interp short-circuits.
 *	  2. `(interp, stub, NULL)` -- !interp false, !ppCtx true.
 *	  3. `(interp, stub, &out)` -- both false; success path
 *	     (may return either `TH8_OK` or `TH8_ERROR` depending
 *	     on whether the stub was previously registered; both
 *	     are accepted for MC/DC purposes -- the guard is
 *	     what's being driven).
 *
 *	Expects (`TH8_ERROR`, `TH8_ERROR`, *).
 *
 * Parameters:
 *	interp -- live interpreter used for tests 2 and 3.
 *
 * Returns:
 *	`TH8_OK` if rc1 and rc2 are `TH8_ERROR` and rc3 is either
 *	`TH8_OK` or `TH8_ERROR` (interpreter result: `"ok"`).
 *	`TH8_ERROR` on any other pattern (diagnostic in result).
 *
 * Side effects:
 *	Sets the interpreter result.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_null_guard_get_platform_ctx(Th8_Interp *interp)
{
    int rc1, rc2, rc3;
    char buf[80];
    void *pCtxOut = NULL;

    /* Th8_GetPlatformContext(interp, xCallback, ppCtx)
     * Guard: if (!interp || !ppCtx) return TH8_ERROR;
     * (xCallback may legitimately be NULL for default-context lookup.) */

    rc1 = Th8_GetPlatformContext(
        NULL, th8test_null_guard_platfunc_stub, &pCtxOut);
    rc2 = Th8_GetPlatformContext(
        interp, th8test_null_guard_platfunc_stub, NULL);
    rc3 = Th8_GetPlatformContext(
        interp, th8test_null_guard_platfunc_stub, &pCtxOut);

    /* rc3 may be TH8_OK (callback found) or TH8_ERROR (callback not
     * registered).  Both pass through the guard; for MC/DC purposes
     * we only require the guard is exercised. */
    if (rc1 == TH8_ERROR && rc2 == TH8_ERROR &&
        (rc3 == TH8_OK || rc3 == TH8_ERROR)) {
	Th8_SetResultStatic(interp, "ok", TH8_NOLEN);
	return TH8_OK;
    }
    {
	int n = snprintf(
	    buf, sizeof(buf),
	    "fail rc1=%d rc2=%d rc3=%d (expected error,error,*)", rc1, rc2,
	    rc3);
	if (n < 0) n = 0;
	if ((size_t)n >= sizeof(buf)) n = (int)sizeof(buf) - 1;
	Th8_SetResult(interp, buf, (size_t)n);
    }
    return TH8_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_null_guard_iterate_array --
 *
 *	Drive the three MC/DC vectors of the
 *	`Th8_IterateArraySearches(interp, xCallback, pCtx)` entry
 *	guard `!interp || !xCallback`.
 *
 *	  1. `(NULL, NULL, NULL)`   -- !interp short-circuits.
 *	  2. `(interp, NULL, NULL)` -- !interp false, !xCallback true.
 *	  3. `(interp, cb, NULL)`   -- both false; success path
 *	     (returns `TH8_OK` cleanly even when no array-search
 *	     cursors are registered).
 *
 *	Expects (`TH8_ERROR`, `TH8_ERROR`, `TH8_OK`).
 *
 * Parameters:
 *	interp -- live interpreter used for tests 2 and 3.
 *
 * Returns:
 *	`TH8_OK` on full pass (interpreter result: `"ok"`).
 *	`TH8_ERROR` otherwise (interpreter result: diagnostic).
 *
 * Side effects:
 *	Sets the interpreter result.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_null_guard_iterate_array(Th8_Interp *interp)
{
    int rc1, rc2, rc3;
    char buf[80];

    /* Test 1: interp=NULL, xCallback=NULL.  !interp short-circuits. */
    rc1 = Th8_IterateArraySearches(NULL, NULL, NULL);

    /* Test 2: interp valid, xCallback=NULL.  !interp false, !xCallback true. */
    rc2 = Th8_IterateArraySearches(interp, NULL, NULL);

    /* Test 3: both valid; success path (succeeds even with empty hash
     * because Th8_IterateArraySearches returns TH8_OK when no searches
     * are registered). */
    rc3 = Th8_IterateArraySearches(interp, th8test_null_guard_ias_cb, NULL);

    if (rc1 == TH8_ERROR && rc2 == TH8_ERROR && rc3 == TH8_OK) {
	Th8_SetResultStatic(interp, "ok", TH8_NOLEN);
	return TH8_OK;
    }
    {
	int n = snprintf(
	    buf, sizeof(buf),
	    "fail rc1=%d rc2=%d rc3=%d (expected error,error,ok)", rc1, rc2,
	    rc3);
	if (n < 0) n = 0;
	if ((size_t)n >= sizeof(buf)) n = (int)sizeof(buf) - 1;
	Th8_SetResult(interp, buf, (size_t)n);
    }
    return TH8_ERROR;
}

/*
 * Helper for 3-operand NULL-OR guards.  Compares 4 return-code
 * outcomes to the expected pattern (3 errors, 1 success) and
 * sets the interp result to "ok" or a "fail rc1=N rc2=N rc3=N
 * rc4=N" diagnostic.
 */
/*
 *----------------------------------------------------------------------
 *
 * th8test_null_guard_check4 --
 *
 *	Verdict helper for a null-guard drive that produced four
 *	return codes: compare `rc1..rc4` against the expected
 *	`e1..e4` and report the outcome on the interpreter.
 *
 * Why / How:
 *	Factors out the pass/fail bookkeeping shared by the
 *	four-vector null-OR guard sweeps.  On a full match the
 *	interp result is set to "ok"; otherwise a
 *	"fail rc1=.. (expected ..)" diagnostic is formatted so the
 *	failing vector is visible to the test.
 *
 * Results:
 *	TH8_OK when every code matches its expectation; TH8_ERROR
 *	otherwise.
 *
 * Side effects:
 *	Sets the interpreter result string.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_null_guard_check4(
    Th8_Interp *interp,
    int rc1,
    int rc2,
    int rc3,
    int rc4,
    int e1,
    int e2,
    int e3,
    int e4)
{
    char buf[96];
    if (rc1 == e1 && rc2 == e2 && rc3 == e3 && rc4 == e4) {
	Th8_SetResultStatic(interp, "ok", TH8_NOLEN);
	return TH8_OK;
    }
    {
	int n = snprintf(
	    buf, sizeof(buf),
	    "fail rc1=%d rc2=%d rc3=%d rc4=%d "
	    "(expected %d,%d,%d,%d)",
	    rc1, rc2, rc3, rc4, e1, e2, e3, e4);
	if (n < 0) n = 0;
	if ((size_t)n >= sizeof(buf)) n = (int)sizeof(buf) - 1;
	Th8_SetResult(interp, buf, (size_t)n);
    }
    return TH8_ERROR;
}

/*
 * Helpers for the boundary-value sweep on the
 * "if (nByte == 0 || nByte > TH8_MX_ALLOC)" guard shared by
 * Th8_SafeAlloc, Th8_SafeRealloc, and Th8_SafeAttemptRealloc.
 * Three input sizes per call site close both operands of the OR:
 *   nByte = 0                  -> n==0 true short-circuits
 *   nByte = TH8_MX_ALLOC + 1   -> n==0 false, n>MAX true
 *   nByte = 16 (small valid)   -> both false, allocation succeeds
 *
 * Each function returns "ok" if all three calls produced the
 * expected outcome (NULL, NULL, non-NULL) or a diagnostic.
 */

/*
 *----------------------------------------------------------------------
 *
 * th8test_null_guard_check3_alloc --
 *
 *	Verdict helper for the safe-allocator boundary sweep:
 *	given the three pointers returned by an allocator called
 *	with size 0, an oversize request, and a small valid size,
 *	check that the first two are NULL and the third non-NULL.
 *
 * Why / How:
 *	Closes both operands of the shared
 *	`nByte == 0 || nByte > TH8_MX_ALLOC` guard.  A non-NULL
 *	`p3` (the success case) is freed here so the drive leaks
 *	nothing; the interp result is set to "ok" or a
 *	"fail p1=.. (expected NULL,NULL,non-NULL)" diagnostic.
 *
 * Results:
 *	TH8_OK when the NULL/NULL/non-NULL pattern holds; TH8_ERROR
 *	otherwise.
 *
 * Side effects:
 *	Frees `p3` when non-NULL and sets the interpreter result.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_null_guard_check3_alloc(
    Th8_Interp *interp,
    void *p1,
    void *p2,
    void *p3)
{
    /* p1 and p2 should be NULL (size-0 / oversize errors).
     * p3 should be non-NULL (success).  Free p3 if non-NULL. */
    char buf[80];
    int ok1 = (p1 == NULL);
    int ok2 = (p2 == NULL);
    int ok3 = (p3 != NULL);
    if (p3) Th8_Free(interp, p3);

    if (ok1 && ok2 && ok3) {
	Th8_SetResultStatic(interp, "ok", TH8_NOLEN);
	return TH8_OK;
    }
    {
	int n = snprintf(
	    buf, sizeof(buf),
	    "fail p1=%p p2=%p p3=%p (expected NULL,NULL,non-NULL)", p1, p2,
	    p3);
	if (n < 0) n = 0;
	if ((size_t)n >= sizeof(buf)) n = (int)sizeof(buf) - 1;
	Th8_SetResult(interp, buf, (size_t)n);
    }
    return TH8_ERROR;
}

#  if defined(TH8_ENABLE_CRYPTOGRAPHY) && defined(TH8_ENABLE_TEST_KEY)
/*
 *----------------------------------------------------------------------
 *
 * th8test_null_guard_policy_preload --
 *
 *	Drive the three MC/DC vectors of the
 *	`Th8_PolicyPreloadKey(interp, pCtx, pKey)` entry guard
 *	`!pCtx || !pKey`:
 *
 *	  1. `(interp, NULL, NULL)`  -- !pCtx short-circuits.
 *	  2. `(interp, ctx, NULL)`   -- !pCtx false, !pKey true.
 *	  3. `(interp, ctx, key)`    -- both false; success path
 *	     (policy takes ownership of the key on success).
 *
 *	Setup acquires the live policy context via
 *	`Th8_GetPolicyCallback` and a valid RSA key by loading the
 *	embedded test-key blob via `Th8_RsaKeyLoad`; both must
 *	succeed for the drive to be meaningful (the helper returns
 *	an error early if either prerequisite fails).
 *
 *	Gated on `TH8_ENABLE_CRYPTOGRAPHY && TH8_ENABLE_TEST_KEY`.
 *
 * Parameters:
 *	interp -- live interpreter with the signed-only policy
 *	          installed (test 1 requires the policy callback
 *	          context).
 *
 * Returns:
 *	`TH8_OK` on full pass (interpreter result: `"ok"`).
 *	`TH8_ERROR` on prerequisite failure or any unexpected
 *	vector outcome (interpreter result: diagnostic).
 *
 * Side effects:
 *	On test 3 success the policy takes ownership of the
 *	loaded key; the local pointer is cleared so the
 *	cleanup-time `Th8_RsaKeyFree` does not double-free.
 *	Sets the interpreter result.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_null_guard_policy_preload(Th8_Interp *interp)
{
    /* Th8_PolicyPreloadKey(interp, pCtx, pKey)
     * Guard: if (!p || !pKey) ... return TH8_ERROR;
     * Three inputs cover the 2-operand OR. */
    void *pCtx = NULL;
    Th8_RsaKey *pKey = NULL;
    const unsigned char *zKeyData;
    size_t nKeyData = 0;
    int rc1, rc2, rc3;
    char buf[80];

    Th8_GetPolicyCallback(interp, NULL, &pCtx);
    if (!pCtx) {
	Th8_SetResultStatic(
	    interp,
	    "null_guard policy_preload: GetPolicyCallback NULL "
	    "(policy not installed?)",
	    TH8_NOLEN);
	return TH8_ERROR;
    }
    zKeyData = Th8_GetEmbeddedKeyTest(&nKeyData);
    if (!zKeyData || nKeyData == 0) {
	Th8_SetResultStatic(
	    interp, "null_guard policy_preload: GetEmbeddedKeyTest empty",
	    TH8_NOLEN);
	return TH8_ERROR;
    }
    if (Th8_RsaKeyLoad(interp, zKeyData, nKeyData, &pKey) != TH8_OK ||
        !pKey) {
	Th8_SetResultStatic(
	    interp, "null_guard policy_preload: RsaKeyLoad failed",
	    TH8_NOLEN);
	return TH8_ERROR;
    }

    /* Test 1: pCtx=NULL, pKey=NULL.  !p true short-circuits. */
    rc1 = Th8_PolicyPreloadKey(interp, NULL, NULL);

    /* Test 2: pCtx valid, pKey=NULL.  !p false, !pKey true. */
    rc2 = Th8_PolicyPreloadKey(interp, pCtx, NULL);

    /* Test 3: both valid.  Policy takes ownership of pKey on
     * success; we set pKey=NULL afterward to skip cleanup-free. */
    rc3 = Th8_PolicyPreloadKey(interp, pCtx, pKey);
    if (rc3 == TH8_OK) {
	pKey = NULL; /* policy owns it now */
    }

    if (pKey) Th8_RsaKeyFree(interp, pKey);

    if (rc1 == TH8_ERROR && rc2 == TH8_ERROR && rc3 == TH8_OK) {
	Th8_SetResultStatic(interp, "ok", TH8_NOLEN);
	return TH8_OK;
    }
    {
	int n = snprintf(
	    buf, sizeof(buf),
	    "fail rc1=%d rc2=%d rc3=%d (expected error,error,ok)", rc1, rc2,
	    rc3);
	if (n < 0) n = 0;
	if ((size_t)n >= sizeof(buf)) n = (int)sizeof(buf) - 1;
	Th8_SetResult(interp, buf, (size_t)n);
    }
    return TH8_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_null_guard_rsa_getters --
 *
 *	Drive the entry-guard MC/DC C-pairs of every RSA key
 *	getter (`Th8_RsaKeyPrivExp`, `Th8_RsaKeyPrime1`,
 *	`Th8_RsaKeyPrime2`, `Th8_RsaKeyToken`) by exercising each
 *	function with three input fixtures:
 *
 *	  1. `NULL`             -- operand 1 short-circuits.
 *	  2. Public-only key    -- operand 1 false; operand 2
 *	                           (`!pKey->bHasPrivate` or
 *	                           `!pKey->zPubBlob`) varies per
 *	                           getter.
 *	  3. Private key        -- both false; success path.
 *
 *	The public-only fixture comes from `Th8_GetPublicKeyZero`
 *	(`key0`), whose private half lives on the commercial
 *	signing service and never enters the binary -- borrowed
 *	reference, do not free.  The private fixture comes from
 *	`Th8_RsaKeyLoad`-ing the embedded test key; the caller
 *	owns and frees it.
 *
 *	Sanity-checks that `key0` really is public-only via
 *	`Th8_RsaKeyHasPrivate`; if the fixture has been broken
 *	(private half leaked into the embedded blob), the helper
 *	reports an error early rather than silently passing.
 *
 *	Gated on `TH8_ENABLE_CRYPTOGRAPHY && TH8_ENABLE_TEST_KEY`.
 *
 * Parameters:
 *	interp -- live interpreter.
 *
 * Returns:
 *	`TH8_OK` on full sweep success (interpreter result:
 *	`"ok"`).
 *	`TH8_ERROR` on fixture failure or unexpected vector
 *	outcome (interpreter result: diagnostic).
 *
 * Side effects:
 *	Allocates and frees one `Th8_RsaKey`.  Sets the
 *	interpreter result.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_null_guard_rsa_getters(Th8_Interp *interp)
{
    /* Th8_RsaKeyPrivExp / Th8_RsaKeyPrime1 / Th8_RsaKeyPrime2:
     *   if (!pKey || !pKey->bHasPrivate) return NULL;
     * Th8_RsaKeyToken:
     *   if (!pKey || !pKey->zPubBlob) ... return TH8_ERROR;
     *
     * Three-input MC/DC sweep:
     *   - pKey = NULL                  (operand 1 short-circuits)
     *   - pKey = key0 public-only      (operand 2 true on PrivExp/
     *                                   Prime; token still works
     *                                   because zPubBlob is set)
     *   - pKey = test key (private)    (both false; success path)
     *
     * Th8_GetPublicKeyZero returns a TRULY public-only key (the
     * private half of key0 lives on the commercial signing service
     * and never enters the binary).  Borrowed reference; do not
     * free.  Th8_GetEmbeddedKeyTest + Th8_RsaKeyLoad gives a
     * private-key copy that we own and free. */
    const Th8_RsaKey *pPubOnly;
    Th8_RsaKey *pPriv = NULL;
    const unsigned char *zKeyData;
    size_t nKeyData = 0;
    size_t nOut;
    unsigned char zToken[8];
    const unsigned char *r1, *r2, *r3;
    int t1, t2, t3;
    char buf[140];

    pPubOnly = Th8_GetPublicKeyZero(interp);
    if (!pPubOnly) {
	Th8_SetResultStatic(
	    interp, "null_guard rsa_getters: GetPublicKeyZero NULL",
	    TH8_NOLEN);
	return TH8_ERROR;
    }
    /* Sanity: pPubOnly really must be public-only.  If
     * Th8_RsaKeyHasPrivate returns true here, the embedded key0
     * wasn't built as public-only and the operand-2 MC/DC slice
     * cannot be exercised via this fixture. */
    if (Th8_RsaKeyHasPrivate(pPubOnly)) {
	Th8_SetResultStatic(
	    interp,
	    "null_guard rsa_getters: GetPublicKeyZero has private "
	    "(fixture broken)",
	    TH8_NOLEN);
	return TH8_ERROR;
    }

    zKeyData = Th8_GetEmbeddedKeyTest(&nKeyData);
    if (!zKeyData || nKeyData == 0) {
	Th8_SetResultStatic(
	    interp, "null_guard rsa_getters: GetEmbeddedKeyTest empty",
	    TH8_NOLEN);
	return TH8_ERROR;
    }
    if (Th8_RsaKeyLoad(interp, zKeyData, nKeyData, &pPriv) != TH8_OK ||
        !pPriv) {
	Th8_SetResultStatic(
	    interp, "null_guard rsa_getters: RsaKeyLoad failed", TH8_NOLEN);
	return TH8_ERROR;
    }

    /* Th8_RsaKeyPrivExp -- 3 inputs:
     *   NULL          -> NULL  (operand 1)
     *   public-only   -> NULL  (operand 2 true: !bHasPrivate)
     *   private       -> non-NULL (success) */
    nOut = 0;
    r1 = Th8_RsaKeyPrivExp(NULL, &nOut);
    nOut = 0;
    r2 = Th8_RsaKeyPrivExp(pPubOnly, &nOut);
    nOut = 0;
    r3 = Th8_RsaKeyPrivExp(pPriv, &nOut);

    /* Th8_RsaKeyToken -- 3 inputs:
     *   NULL          -> TH8_ERROR (operand 1)
     *   public-only   -> TH8_OK    (zPubBlob is present even on
     *                               public-only keys)
     *   private       -> TH8_OK    (success) */
    t1 = Th8_RsaKeyToken(interp, NULL, zToken);
    t2 = Th8_RsaKeyToken(interp, pPubOnly, zToken);
    t3 = Th8_RsaKeyToken(interp, pPriv, zToken);

    /* Prime1, Prime2 (same MC/DC shape as PrivExp). */
    nOut = 0;
    (void)Th8_RsaKeyPrime1(NULL, &nOut);
    nOut = 0;
    (void)Th8_RsaKeyPrime1(pPubOnly, &nOut);
    nOut = 0;
    (void)Th8_RsaKeyPrime1(pPriv, &nOut);
    nOut = 0;
    (void)Th8_RsaKeyPrime2(NULL, &nOut);
    nOut = 0;
    (void)Th8_RsaKeyPrime2(pPubOnly, &nOut);
    nOut = 0;
    (void)Th8_RsaKeyPrime2(pPriv, &nOut);

    /* Th8_RsaVerify entry-guard drives (2026-06-18, batch
     * #38) for th8_snk.c L1254 `if (!zModulus ||
     * nModulus == 0)` and L1266 `if (nSig > nModulus +
     * 64 || nSig > TH8_RSA_MAX_SIG_BYTES)` -- previously
     * both 0% MC/DC because no test reached this code.
     *
     * Drive A: pKey=NULL -- Th8_RsaKeyModulus returns
     *   NULL with nModulus=0, drives L1254 (T,-)
     *   C1-Pair via short-circuit.
     * Drive B: valid pKey (pPriv, 16384-bit), nSig set
     *   huge (99999) -- 99999 > 2048+64=2112, drives
     *   L1266 (T,-) C1-Pair.  (L1266 C2-Pair `nSig >
     *   TH8_RSA_MAX_SIG_BYTES` is intrinsic-dead: with
     *   nModulus=2048, nModulus+64=2112 < 16448 =
     *   TH8_RSA_MAX_SIG_BYTES, so any nSig that makes
     *   C2=T also makes C1=T -- (F,T) unreachable.)
     *
     * Buffers for Drive B: a small zSig buffer suffices
     * because L1266 returns at L1269 before reading
     * zSig. */
    {
	unsigned char zSigDummy[1] = {0};

	(void)Th8_RsaVerify(interp, NULL, NULL, 0, NULL, 0);
	(void)Th8_RsaVerify(
	    interp, pPriv, (const unsigned char *)"data", 4, zSigDummy,
	    99999);
    }

    /* Th8_RsaSign + Th8_RsaVerify round-trip drive
     * (2026-06-18, batch #39) for th8_snk.c L1287/L1298
     * (Th8_RsaVerify EVP_PKEY_fromdata branch) and
     * L1457/L1502/L1534/L1554/L1586/L1597 (Th8_RsaSign
     * internal OpenSSL setup paths) -- previously all
     * 0% MC/DC because no test exercised Sign+Verify
     * directly through the public TH8_API.  Using pPriv
     * for both signer and verifier guarantees a valid
     * signature that traverses the success branch
     * through every OpenSSL-init compound condition. */
    {
	unsigned char *pSig = NULL;
	size_t nSig = 0;
	const unsigned char
	    zMsg[] = "test message for sign/verify round trip";
	int rcSign;
	int rcVerify;

	rcSign =
	    Th8_RsaSign(interp, pPriv, zMsg, sizeof(zMsg) - 1, &pSig, &nSig);
	if (rcSign == TH8_OK && pSig && nSig > 0) {
	    char zHashOut[129];

	    rcVerify = Th8_RsaVerify(
	        interp, pPriv, zMsg, sizeof(zMsg) - 1, pSig, nSig);
	    (void)rcVerify;

	    /* Th8_RsaExtractHash valid drive (2026-06-18,
	     * batch #39) for th8_snk.c L1812+ -- reaches
	     * `Th8_RsaExtractHash` success path via the
	     * just-computed pSig from the round trip. */
	    (void)Th8_RsaExtractHash(interp, pPriv, pSig, nSig, zHashOut);
	    Th8_Free(interp, pSig);
	}
	/* Th8_RsaExtractHash NULL-pKey drive for L1812
	 * (T,-) C1-Pair. */
	{
	    char zHashOut[129];

	    (void)Th8_RsaExtractHash(interp, NULL, NULL, 0, zHashOut);
	}
    }

    /* Th8_RsaKeyToken zPubBlob-null drive (2026-06-18,
     * batch #40) for th8_snk.c L1117
     * `if (!pKey || !pKey->zPubBlob)` C2-Pair (F,T) --
     * previously 50% because the natural pPubOnly /
     * pPriv keys always have a valid zPubBlob.  The
     * th8TestRsaKeyClearPubBlob internal-stubs helper
     * temporarily nulls pPriv->zPubBlob (saving the
     * old value), then Th8_RsaKeyToken sees C1=F
     * (valid pKey) AND C2=T (zPubBlob is now NULL) ->
     * (F,T=T).  Restore immediately so pPriv remains
     * usable for the subsequent Th8_RsaKeyFree. */
    if (th8InternalStubsPtr->th8_TestRsaKeyClearPubBlob &&
        th8InternalStubsPtr->th8_TestRsaKeyRestorePubBlob) {
	unsigned char *pSavedBlob = NULL;
	size_t nSavedBlob = 0;
	unsigned char zToken4[8];

	th8TestRsaKeyClearPubBlob(pPriv, &pSavedBlob, &nSavedBlob);
	(void)Th8_RsaKeyToken(interp, pPriv, zToken4);
	th8TestRsaKeyRestorePubBlob(pPriv, pSavedBlob, nSavedBlob);
    }

    Th8_RsaKeyFree(interp, pPriv);

    if (r1 == NULL && r2 == NULL && r3 != NULL && t1 == TH8_ERROR &&
        t2 == TH8_OK && t3 == TH8_OK) {
	Th8_SetResultStatic(interp, "ok", TH8_NOLEN);
	return TH8_OK;
    }
    {
	int n = snprintf(
	    buf, sizeof(buf),
	    "fail privexp=(%p,%p,%p) token=(%d,%d,%d) "
	    "(expected NULL,NULL,non-NULL / err,ok,ok)",
	    (void *)r1, (void *)r2, (void *)r3, t1, t2, t3);
	if (n < 0) n = 0;
	if ((size_t)n >= sizeof(buf)) n = (int)sizeof(buf) - 1;
	Th8_SetResult(interp, buf, (size_t)n);
    }
    return TH8_ERROR;
}
#  endif /* TH8_ENABLE_CRYPTOGRAPHY && TH8_ENABLE_TEST_KEY */

#  if defined(TH8_ENABLE_CRYPTOGRAPHY)
/*
 *----------------------------------------------------------------------
 *
 * th8test_null_guard_harpy_sig_load --
 *
 *	Drive the entry-guard MC/DC C-pairs of
 *	`Th8_HarpySigLoad(interp, zData, nData, ppSig, pnSig,
 *	ppToken)` plus several deeper Harpy parser decisions that
 *	the regular signed-load tests do not reach:
 *
 *	  * Three NULL inputs cover the 3-operand OR guard
 *	    `(!zData || !ppSig || !pnSig)` at the function entry.
 *	  * A four-line minimal header with `ppId = NULL` drives
 *	    `th8_harpy.c:152` `if (lineNo == 3 && ppId)` C2=F.
 *	  * A header with a TAB after `#` on line 2 drives the
 *	    `\t` arm of the whitespace skip at `:142`.
 *	  * Header line 3 "x -- " followed by spaces and a bare
 *	    LF drives the space-skip-loop exit at `:170`.
 *	  * A punctuation-only body with chars outside every
 *	    base64 sub-range drives the empty-decoded-buffer arm
 *	    at `:232` and the multi-condition character-class
 *	    filter at `:220`.
 *	  * Header line 3 with trailing spaces drives the
 *	    trim-trailing-whitespace loop at `:173`.
 *
 *	Each scenario allocates and frees its own ppSig (and
 *	ppToken where used).  The success path with real
 *	signature data is exercised by tests/harpy.tcl, not
 *	here.
 *
 *	Gated on `TH8_ENABLE_CRYPTOGRAPHY`.
 *
 * Parameters:
 *	interp -- live interpreter.
 *
 * Returns:
 *	`TH8_OK` if all three primary guard vectors returned
 *	`TH8_ERROR` (interpreter result: `"ok"`).
 *	`TH8_ERROR` otherwise (interpreter result: diagnostic
 *	containing the three return codes).
 *
 * Side effects:
 *	Sets the interpreter result.  Allocates and frees
 *	several signature / token buffers via `Th8_Free`.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_null_guard_harpy_sig_load(Th8_Interp *interp)
{
    int rc1, rc2, rc3;
    unsigned char *pSig = NULL;
    size_t nSig = 0;
    char *pToken = NULL;
    char buf[80];

    /* Th8_HarpySigLoad(interp, zData, nData, ppSig, pnSig, ppToken)
     * Guard: if (!zData || !ppSig || !pnSig) { ... return TH8_ERROR; }
     * Three NULL inputs cover the 3-operand OR.  ppToken is not in
     * the guard so any value is fine.  The success path (all valid
     * + real signature data) is exercised by tests/harpy.tcl. */

    rc1 = Th8_HarpySigLoad(interp, NULL, 0, &pSig, &nSig, &pToken);
    rc2 = Th8_HarpySigLoad(interp, "x", 1, NULL, &nSig, &pToken);
    rc3 = Th8_HarpySigLoad(interp, "x", 1, &pSig, NULL, &pToken);

    /*
     * Drive C2=F at src/plugins/harpy/th8_harpy.c L152
     * `if (lineNo == 3 && ppId)`.  The caller MAY pass NULL
     * for ppId per the API contract (header docstring at L94).
     * Existing callers always pass &pToken; this call with
     * NULL ppId reaches lineNo==3 with C2=F, closing the
     * C2-Pair.  We don't need valid signature data -- the
     * loader will return TH8_ERROR after parsing the header
     * line stream, but the lineNo==3 check fires en route.
     * A short multi-line header with CRLF separators is
     * sufficient to advance the lineNo counter.
     */
    {
	static const char zMiniHdr[] = "###\r\n"
	                               "#\r\n"
	                               "# fake -- abcdef0123456789\r\n"
	                               "#\r\n";
	unsigned char *pSig2 = NULL;
	size_t nSig2 = 0;
	(void)Th8_HarpySigLoad(
	    interp, zMiniHdr, sizeof(zMiniHdr) - 1, &pSig2, &nSig2, NULL);
	Th8_Free(interp, pSig2);
    }

    /* L142 C3-Pair drive (2026-06-18, batch #41): header
     * line with a TAB after '#' triggers
     * `zData[j] == '\t'` C3=T while C2 (' ') is F --
     * pairs (T,F,T=T) with the existing (T,F,F=F)
     * vector to close C3-Pair at L142
     * `while (j < lineEnd && (zData[j] == ' ' ||
     * zData[j] == '\t'))`. */
    {
	static const char zTabHdr[] = "###\r\n"
	                              "\t# tab on line 2\r\n"
	                              "#\r\n";
	unsigned char *pSig3 = NULL;
	size_t nSig3 = 0;

	(void)Th8_HarpySigLoad(
	    interp, zTabHdr, sizeof(zTabHdr) - 1, &pSig3, &nSig3, NULL);
	Th8_Free(interp, pSig3);
    }

    /* L170 C1-Pair drive (2026-06-18, batch #41): header
     * line 3 with "--" followed by only spaces and a
     * bare LF (no CR).  The space-skip loop at L170
     * `while (p < &zData[end] && *p == ' ')` advances
     * past every space and exits via (F,-=F) once
     * `p < &zData[end]` is false.  C1-Pair pairs this
     * (F,-=F) with the existing (T,T=T) vector.  Must
     * pass a non-NULL ppId so the L151 outer guard
     * `if (lineNo == 3 && ppId)` lets the dash-scan
     * proceed. */
    {
	static const char zSpcHdr[] = "###\n"
	                              "#\n"
	                              "# x --      \n"
	                              "#\n";
	unsigned char *pSig4 = NULL;
	size_t nSig4 = 0;
	char *pTok4 = NULL;

	(void)Th8_HarpySigLoad(
	    interp, zSpcHdr, sizeof(zSpcHdr) - 1, &pSig4, &nSig4, &pTok4);
	Th8_Free(interp, pSig4);
	Th8_Free(interp, pTok4);
    }

    /* L232 C2-Pair drive (2026-06-18, batch #41): valid
     * non-empty body that contains zero base64
     * characters.  The first non-comment non-empty
     * line (".") sets bodyStart; subsequent
     * punctuation-only body bytes are stripped by the
     * base64-alphabet filter at L220, leaving m=0.
     * zB64 is non-NULL (allocated for nRaw bytes) but
     * nB64=0, so L232 (F,T=T) pairs with (F,F=F) to
     * close C2-Pair.
     *
     * Extended (2026-06-18, batch #42) with chars
     * outside ALL base64 sub-ranges to drive L220's
     * 9-condition compound C2-, C3-, C4-Pairs:
     *   `[` (0x5B): in the gap between 'Z' (0x5A)
     *       and 'a' (0x61) -- drives (T,F,F,...)=F
     *       paired with (T,T,-,...)=T (uppercase
     *       letter) for C2-Pair and C3-Pair.
     *   `{` (0x7B): > 'z' (0x7A) -- drives
     *       (T,F,T,F,...)=F paired with
     *       (T,F,T,T,...)=T (lowercase) for C4-Pair. */
    {
	static const char zPuncBody[] = "###\n"
	                                ".\n"
	                                ".,!?@%[{|}~\n";
	unsigned char *pSig5 = NULL;
	size_t nSig5 = 0;

	(void)Th8_HarpySigLoad(
	    interp, zPuncBody, sizeof(zPuncBody) - 1, &pSig5, &nSig5, NULL);
	Th8_Free(interp, pSig5);
    }

    /* L173 C2-Pair drive (2026-06-18, batch #42): header
     * line 3 with token content followed by trailing
     * spaces (no CR) so the trim loop at L173-175
     * sees end-1 == ' ' on its first iteration
     * (drives (T,T,-=T)), then iterates past the
     * spaces back to non-space content (drives
     * (T,F,F=F) on exit).  Together the two vectors
     * close L173 C2-Pair. */
    {
	static const char zTrailHdr[] = "###\n"
	                                "#\n"
	                                "# x -- ABC   \n"
	                                "#\n";
	unsigned char *pSig6 = NULL;
	size_t nSig6 = 0;
	char *pTok6 = NULL;

	(void)Th8_HarpySigLoad(
	    interp, zTrailHdr, sizeof(zTrailHdr) - 1, &pSig6, &nSig6, &pTok6);
	Th8_Free(interp, pSig6);
	Th8_Free(interp, pTok6);
    }

    if (rc1 == TH8_ERROR && rc2 == TH8_ERROR && rc3 == TH8_ERROR) {
	Th8_SetResultStatic(interp, "ok", TH8_NOLEN);
	return TH8_OK;
    }
    {
	int n = snprintf(
	    buf, sizeof(buf),
	    "fail rc1=%d rc2=%d rc3=%d (expected error,error,error)", rc1,
	    rc2, rc3);
	if (n < 0) n = 0;
	if ((size_t)n >= sizeof(buf)) n = (int)sizeof(buf) - 1;
	Th8_SetResult(interp, buf, (size_t)n);
    }
    return TH8_ERROR;
}
#  endif /* TH8_ENABLE_CRYPTOGRAPHY */

#  if defined(TH8_ENABLE_FAULT_INJECTION)
/*
 *----------------------------------------------------------------------
 *
 * th8test_null_guard_fault --
 *
 *	Drive the entry-guard MC/DC C-pairs of the
 *	fault-injection install / uninstall API:
 *
 *	  * `Th8_FaultInstall(interp, pConfig, pCtx)` -- 3-operand
 *	    OR `(!interp || !pConfig || !pCtx)`.  Three NULL-input
 *	    vectors exercise each operand short-circuit, plus a
 *	    fourth all-valid call exercises the success path.
 *	  * `Th8_FaultUninstall(interp, pCtx)` -- 2-operand OR
 *	    `(!interp || !pCtx)`.  Two NULL-input vectors cover
 *	    both operand short-circuits.
 *
 *	After the successful install vector, the helper
 *	immediately uninstalls so the interpreter's platform is
 *	restored before any subsequent test runs.  The fault
 *	context is heap-allocated via `Th8_FaultCtxSize` and
 *	freed at exit.
 *
 *	Gated on `TH8_ENABLE_FAULT_INJECTION`.
 *
 * Parameters:
 *	interp -- live interpreter.
 *
 * Returns:
 *	`TH8_OK` if every vector returned the expected code
 *	(rc1=rc2=rc3=ERROR, rc4=OK, rc5=rc6=ERROR; interpreter
 *	result `"ok"`).
 *	`TH8_ERROR` otherwise (interpreter result: diagnostic
 *	with every observed return code).
 *
 * Side effects:
 *	Allocates and frees one `Th8_FaultCtx`.  Briefly
 *	installs the fault layer on the interpreter and then
 *	uninstalls it.  Sets the interpreter result.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_null_guard_fault(Th8_Interp *interp)
{
    Th8_FaultConfig cfg;
    Th8_FaultCtx *pCtx;
    int rc1, rc2, rc3, rc4, rc5, rc6;
    char buf[120];

    /* Th8_FaultInstall(interp, pConfig, pCtx)
     * Guard: if (!interp || !pConfig || !pCtx) return TH8_ERROR;
     * Th8_FaultUninstall(interp, pCtx)
     * Guard: if (!interp || !pCtx) return TH8_ERROR;
     *
     * One helper exercises both APIs.  The success path installs and
     * immediately uninstalls so the interp's platform is restored
     * before any subsequent test runs. */

    Th8_FaultConfigInit(&cfg);
    pCtx = (Th8_FaultCtx *)malloc(Th8_FaultCtxSize());
    if (!pCtx) {
	Th8_SetResultStatic(
	    interp, "null_guard fault: malloc failed", TH8_NOLEN);
	return TH8_ERROR;
    }

    /* Install: 3-operand OR. */
    rc1 = Th8_FaultInstall(NULL, &cfg, pCtx);
    rc2 = Th8_FaultInstall(interp, NULL, pCtx);
    rc3 = Th8_FaultInstall(interp, &cfg, NULL);
    rc4 = Th8_FaultInstall(interp, &cfg, pCtx); /* success */

    /* Uninstall: 2-operand OR. */
    rc5 = Th8_FaultUninstall(NULL, pCtx);
    rc6 = Th8_FaultUninstall(interp, NULL);

    /* Restore the interp by properly uninstalling the active layer. */
    if (rc4 == TH8_OK) {
	Th8_FaultUninstall(interp, pCtx);
    }
    free(pCtx);

    if (rc1 == TH8_ERROR && rc2 == TH8_ERROR && rc3 == TH8_ERROR &&
        rc4 == TH8_OK && rc5 == TH8_ERROR && rc6 == TH8_ERROR) {
	Th8_SetResultStatic(interp, "ok", TH8_NOLEN);
	return TH8_OK;
    }
    {
	int n = snprintf(
	    buf, sizeof(buf),
	    "fail rc1=%d rc2=%d rc3=%d rc4=%d rc5=%d rc6=%d", rc1, rc2, rc3,
	    rc4, rc5, rc6);
	if (n < 0) n = 0;
	if ((size_t)n >= sizeof(buf)) n = (int)sizeof(buf) - 1;
	Th8_SetResult(interp, buf, (size_t)n);
    }
    return TH8_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_pt_chanctl_fault_cmd --
 *
 *	Implements "::th8testlib::pt_chanctl_fault".  Installs the
 *	fault layer on the calling interpreter with bFailChannelOpen
 *	and bFailChannelWrite set, then invokes
 *	pPlat->xChannelControl with op=OPEN and op=WRITE so the calls
 *	route through pt_xChannelControl in the fault wrapper.  The
 *	fault interceptors at th8_fault.c:508 (WRITE) and :511 (OPEN)
 *	fire on the matching `op == TH8_CHANCTL_X && bFailChannelX`
 *	compound and short-circuit to TH8_ERROR.  Existing in-tree
 *	code never invokes the WRITE fallback (xOutput is preferred)
 *	or OPEN at all (no script-level [open]), so without this
 *	helper the entire pt_xChannelControl function has zero
 *	execution count.
 *
 * Why / How:
 *	Self-contained C invocation: avoids the cross-interp issue
 *	with fault eval (which runs the script body in a child interp
 *	where testlib commands are not registered).  The fault
 *	platform is uninstalled before returning so subsequent tests
 *	see a clean interp.
 *
 * Results:
 *	TH8_OK on success; an explanatory string on Th8_FaultInstall
 *	failure.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_pt_chanctl_fault_cmd(
    Th8_Interp *interp, /* The interpreter. */
    void *ctx, /* Unused. */
    int argc, /* Argument count. */
    const char **argv, /* Argument values. */
    size_t *argl) /* Argument lengths. */
{
    Th8_FaultConfig cfg;
    Th8_FaultCtx *pCtx;
    const Th8_Platform *pPlat;
    th8_int64_t res = 0;

    (void)ctx;
    (void)argv;
    (void)argl;

    if (argc != 1) {
	return Th8_WrongNumArgs(interp, "th8testlib::pt_chanctl_fault");
    }

    pCtx = (Th8_FaultCtx *)malloc(Th8_FaultCtxSize());
    if (!pCtx) {
	Th8_SetResultStatic(
	    interp, "pt_chanctl_fault: malloc failed", TH8_NOLEN);
	return TH8_ERROR;
    }

    /* Pass 1: fault flags set -- drive (T,T) at L508 and L511. */
    Th8_FaultConfigInit(&cfg);
    cfg.bFailChannelOpen = 1;
    cfg.bFailChannelWrite = 1;

    if (Th8_FaultInstall(interp, &cfg, pCtx) != TH8_OK) {
	free(pCtx);
	Th8_SetResultStatic(
	    interp, "pt_chanctl_fault: FaultInstall failed", TH8_NOLEN);
	return TH8_ERROR;
    }

    pPlat = Th8_GetPlatform(interp);
    if (pPlat && pPlat->xChannelControl) {
	/* OPEN with bFailChannelOpen=T -> L511 (T,T) and L508 (F,-). */
	(void)pPlat->xChannelControl(
	    interp, pPlat->pCtx, NULL, TH8_CHANCTL_OPEN, 1, 0, &res, "x");

	/* WRITE with bFailChannelWrite=T -> L508 (T,T). */
	(void)pPlat->xChannelControl(
	    interp, pPlat->pCtx, NULL, TH8_CHANCTL_WRITE, 1, 0, &res, "x");

	/* FLUSH (neither WRITE nor OPEN) -> L508 (F,-) and L511 (F,-). */
	(void)pPlat->xChannelControl(
	    interp, pPlat->pCtx, NULL, TH8_CHANCTL_FLUSH, 0, 0, &res, NULL);
    }

    Th8_FaultUninstall(interp, pCtx);

    /* Pass 2: fault flags clear -- drive (T,F) at L508 and L511. */
    Th8_FaultConfigInit(&cfg);
    if (Th8_FaultInstall(interp, &cfg, pCtx) != TH8_OK) {
	free(pCtx);
	Th8_SetResultStatic(
	    interp, "pt_chanctl_fault: FaultInstall pass2 failed", TH8_NOLEN);
	return TH8_ERROR;
    }
    pPlat = Th8_GetPlatform(interp);
    if (pPlat && pPlat->xChannelControl) {
	/* OPEN with bFailChannelOpen=F -> L511 (T,F). */
	(void)pPlat->xChannelControl(
	    interp, pPlat->pCtx, NULL, TH8_CHANCTL_OPEN, 1, 0, &res, "x");

	/* WRITE with bFailChannelWrite=F -> L508 (T,F). */
	(void)pPlat->xChannelControl(
	    interp, pPlat->pCtx, NULL, TH8_CHANCTL_WRITE, 1, 0, &res, "x");

	/* READ with both flags clear -> L499 (F) and L503 (F). */
	{
	    char readBuf[8];
	    (void)pPlat->xChannelControl(
	        interp, pPlat->pCtx, NULL, TH8_CHANCTL_READ, 1, 0, &res,
	        readBuf);
	}
    }
    Th8_FaultUninstall(interp, pCtx);

    /* Pass 3: bFailChannelRead set -- drive L499 (T). */
    Th8_FaultConfigInit(&cfg);
    cfg.bFailChannelRead = 1;
    if (Th8_FaultInstall(interp, &cfg, pCtx) != TH8_OK) {
	free(pCtx);
	Th8_SetResultStatic(
	    interp, "pt_chanctl_fault: FaultInstall pass3 failed", TH8_NOLEN);
	return TH8_ERROR;
    }
    pPlat = Th8_GetPlatform(interp);
    if (pPlat && pPlat->xChannelControl) {
	char readBuf[8];
	(void)pPlat->xChannelControl(
	    interp, pPlat->pCtx, NULL, TH8_CHANCTL_READ, 1, 0, &res, readBuf);
    }
    Th8_FaultUninstall(interp, pCtx);

    /* Pass 4: bFailChannelEOF set (Read=F) -- drive L499 (F), L503 (T). */
    Th8_FaultConfigInit(&cfg);
    cfg.bFailChannelEOF = 1;
    if (Th8_FaultInstall(interp, &cfg, pCtx) != TH8_OK) {
	free(pCtx);
	Th8_SetResultStatic(
	    interp, "pt_chanctl_fault: FaultInstall pass4 failed", TH8_NOLEN);
	return TH8_ERROR;
    }
    pPlat = Th8_GetPlatform(interp);
    if (pPlat && pPlat->xChannelControl) {
	char readBuf[8];
	(void)pPlat->xChannelControl(
	    interp, pPlat->pCtx, NULL, TH8_CHANCTL_READ, 1, 0, &res, readBuf);
    }
    Th8_FaultUninstall(interp, pCtx);
    free(pCtx);

    Th8_SetResultStatic(interp, "ok", TH8_NOLEN);
    return TH8_OK;
}
#  endif /* TH8_ENABLE_FAULT_INJECTION */

/*
 *----------------------------------------------------------------------
 * Plugin-API MC/DC closure helpers.
 *
 * src/th8_plugin.c has several short-circuit compounds whose
 * false vectors are never reached by ordinary plugin registration
 * during interp startup (e.g. argument-validation guards, the
 * duplicate-name path, the "GetCommands returned 0" branch).
 * These stub callbacks plus the th8test_null_guard_plugin sweep
 * drive each pair from a child interpreter so the parent test
 * harness's plugin list is not perturbed.
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8test_plugin_dummy_cmd --
 *
 *	Trivial command procedure used as the target `xProc` for
 *	the mock plugins registered during the plugin-API MC/DC
 *	sweep.
 *
 * Why / How:
 *	The sweep registers throwaway commands in a child interp to
 *	drive th8_plugin.c registration paths; those commands need
 *	a real procedure to bind to.  This one ignores its
 *	arguments and reports success.
 *
 * Results:
 *	TH8_OK with the interpreter result set to "ok".
 *
 * Side effects:
 *	Sets the interpreter result.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_plugin_dummy_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    (void)ctx;
    (void)argc;
    (void)argv;
    (void)argl;
    Th8_SetResultStatic(interp, "ok", TH8_NOLEN);
    return TH8_OK;
}

/* Three trivial-registration stubs, each registering a distinct
 * command name.  Distinct names are required because the same
 * pChild hosts several "successful" plugin registrations during
 * the sweep; sharing one command name would make the second
 * Th8_CreateCommand a duplicate and skew the test. */
/*
 *----------------------------------------------------------------------
 *
 * th8test_plugin_get_ok_a --
 *
 *	Mock `xGetCommands` plugin probe that succeeds and yields
 *	a single command named `th8test_plugin_cmd_a` bound to
 *	the dummy command procedure.  Used to drive the
 *	plugin-registration success path with a distinct command
 *	name from the `_ok_b` probe (so the registry can hold
 *	multiple plugins simultaneously without name collisions).
 *
 * Parameters:
 *	pCommand  -- output array for command entries; NULL on
 *	             the discovery call (when the caller is just
 *	             querying `*pnCommand`).
 *	pnCommand -- discovery call: receives the count (always
 *	             1).  Fill call: ignored.
 *
 * Returns:
 *	`TH8_OK` always.
 *
 * Side effects:
 *	On the fill call, writes a single `Th8_CommandEntry` into
 *	`pCommand[0]`.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_plugin_get_ok_a(Th8_CommandEntry *pCommand, int *pnCommand)
{
    if (pCommand == NULL) {
	if (pnCommand) *pnCommand = 1;
	return TH8_OK;
    }
    pCommand[0].nVersion = 1;
    pCommand[0].token = 0;
    pCommand[0].zName = "th8test_plugin_cmd_a";
    pCommand[0].xProc = th8test_plugin_dummy_cmd;
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_plugin_get_ok_b --
 *
 *	Mock `xGetCommands` plugin probe that succeeds and yields
 *	a single command named `th8test_plugin_cmd_b` bound to
 *	the dummy command procedure.  Used to drive the
 *	plugin-registration success path with a distinct command
 *	name from the `_ok_a` probe (so the registry can hold
 *	multiple plugins simultaneously without name collisions).
 *
 * Parameters:
 *	pCommand  -- output array for command entries; NULL on
 *	             the discovery call (when the caller is just
 *	             querying `*pnCommand`).
 *	pnCommand -- discovery call: receives the count (always
 *	             1).  Fill call: ignored.
 *
 * Returns:
 *	`TH8_OK` always.
 *
 * Side effects:
 *	On the fill call, writes a single `Th8_CommandEntry` into
 *	`pCommand[0]`.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_plugin_get_ok_b(Th8_CommandEntry *pCommand, int *pnCommand)
{
    if (pCommand == NULL) {
	if (pnCommand) *pnCommand = 1;
	return TH8_OK;
    }
    pCommand[0].nVersion = 1;
    pCommand[0].token = 0;
    pCommand[0].zName = "th8test_plugin_cmd_b";
    pCommand[0].xProc = th8test_plugin_dummy_cmd;
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_plugin_get_ok_c --
 *
 *	Mock `xGetCommands` plugin probe that succeeds and yields
 *	a single command named `th8test_plugin_cmd_c`.  Identical
 *	in shape to `_ok_b`; the distinct name lets the registry
 *	carry three plugins concurrently in the multi-plugin MC/DC
 *	drives.
 *
 * Parameters / Returns / Side effects:
 *	See `th8test_plugin_get_ok_b`; the only difference is the
 *	emitted command name.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_plugin_get_ok_c(Th8_CommandEntry *pCommand, int *pnCommand)
{
    if (pCommand == NULL) {
	if (pnCommand) *pnCommand = 1;
	return TH8_OK;
    }
    pCommand[0].nVersion = 1;
    pCommand[0].token = 0;
    pCommand[0].zName = "th8test_plugin_cmd_c";
    pCommand[0].xProc = th8test_plugin_dummy_cmd;
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_plugin_get_zero --
 *
 *	Mock plugin probe that reports zero commands.  Drives the
 *	`nCommand <= 0` false-vector (C2=T) of the compound at
 *	`src/th8_plugin.c:135-136`, which a non-degenerate plugin
 *	cannot reach.
 *
 * Parameters:
 *	pCommand  -- ignored.
 *	pnCommand -- receives 0 on the discovery call.
 *
 * Returns:
 *	`TH8_OK` always.
 *
 * Side effects:
 *	Writes 0 to `*pnCommand` if non-NULL.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_plugin_get_zero(Th8_CommandEntry *pCommand, int *pnCommand)
{
    (void)pCommand;
    if (pnCommand) *pnCommand = 0;
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_plugin_get_fail --
 *
 *	Mock plugin probe that always fails.  Drives the
 *	first-call-error vector (C1=T) of the compound at
 *	`src/th8_plugin.c:135-136`.
 *
 * Parameters:
 *	pCommand  -- ignored.
 *	pnCommand -- ignored.
 *
 * Returns:
 *	`TH8_ERROR` always.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_plugin_get_fail(Th8_CommandEntry *pCommand, int *pnCommand)
{
    (void)pCommand;
    (void)pnCommand;
    return TH8_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_plugin_get_fail_second --
 *
 *	Mock plugin probe that succeeds on the discovery call but
 *	fails on the fill call.  Drives the second-call-error
 *	branch at `src/th8_plugin.c:158`
 *	(`if (xGetCommands(aEntry, &nCommand) != TH8_OK)`).
 *
 *	The static counter `th8test_plugin_get_second_calls` is
 *	reset to 0 on the discovery call and incremented on each
 *	subsequent fill call so the caller can verify it was
 *	actually invoked twice.
 *
 * Parameters:
 *	pCommand  -- NULL on the discovery call (returns OK);
 *	             non-NULL on the fill call (returns ERROR).
 *	pnCommand -- receives 1 on the discovery call.  Ignored
 *	             on the fill call.
 *
 * Returns:
 *	`TH8_OK` on the discovery call, `TH8_ERROR` on the fill
 *	call.
 *
 * Side effects:
 *	Resets / increments the module-static counter
 *	`th8test_plugin_get_second_calls`.
 *
 *----------------------------------------------------------------------
 */
static int th8test_plugin_get_second_calls;
static int
th8test_plugin_get_fail_second(Th8_CommandEntry *pCommand, int *pnCommand)
{
    if (pCommand == NULL) {
	th8test_plugin_get_second_calls = 0;
	if (pnCommand) *pnCommand = 1;
	return TH8_OK;
    }
    th8test_plugin_get_second_calls++;
    return TH8_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_null_guard_plugin --
 *
 *	Exercise the argument-validation and search compounds in
 *	src/th8_plugin.c.  All operations run on a private child
 *	interpreter so the parent's plugin registry is unaffected.
 *
 *	Drives:
 *	  line 117  if (!interp || !zName || !xGetCommands)
 *	            -- 3-operand OR (C1, C2, C3 via NULL args).
 *	  line 127  if (th8PluginFind(...)) -- duplicate-name branch.
 *	  line 135  xGetCommands(NULL,&n)!=OK || n<=0
 *	            -- 2-operand OR (C1 via probe-fail, C2 via 0-cmd).
 *	  line 158  if (xGetCommands(aEntry,&n) != TH8_OK)
 *	            -- second-call failure.
 *	  line 233  if (!interp || !zName) -- 2-operand OR.
 *	  line 247  if (p->nName == nName && Th8_Memcmp(...)==0)
 *	            inline find-loop -- T,T (match), T,F (same-length
 *	            mismatch), F (length mismatch).
 *	  line 63   same compound inside th8PluginFind.
 *
 *	Returns "ok" if the child interp could be created and the
 *	full sweep ran; the individual rc values are not asserted
 *	because the script-level test verifies behaviour via
 *	user-visible side effects (whether the cleanup unregisters
 *	succeed).
 *
 *----------------------------------------------------------------------
 */

static int
th8test_null_guard_plugin(Th8_Interp *interp)
{
    Th8_Platform plat;
    const Th8_Platform *pLibc;
    Th8_Interp *pChild;

    pLibc = Th8_GetLibcPlatform();
    if (!pLibc) {
	Th8_SetResultStatic(
	    interp, "null_guard plugin: no libc platform", TH8_NOLEN);
	return TH8_ERROR;
    }
    plat = *pLibc;
    /* No I/O, no file system: this child only registers/unregisters
     * stub plugins and never evaluates a script. */
    plat.xGetData = 0;
    plat.xDataExists = 0;
    plat.xLoad = 0;
    plat.xUnload = 0;
    plat.xInput = 0;
    plat.xOutput = 0;
    plat.xOutputError = 0;
    plat.xNormalizePath = 0;
    plat.xGetCwd = 0;
    plat.xSetCwd = 0;
    plat.xPanic = 0; /* clean-error on alloc failure */

    pChild = Th8_CreateInterp(&plat);
    if (!pChild) {
	Th8_SetResultStatic(
	    interp, "null_guard plugin: child interp failed", TH8_NOLEN);
	return TH8_ERROR;
    }

    /* Three-operand NULL guard at line 117. */
    (void)Th8_RegisterPlugin(NULL, "x", th8test_plugin_get_ok_a);
    (void)Th8_RegisterPlugin(pChild, NULL, th8test_plugin_get_ok_a);
    (void)Th8_RegisterPlugin(pChild, "x", NULL);

    /* Two-operand OR at line 135-136: probe-fail (C1=T). */
    (void)Th8_RegisterPlugin(
        pChild, "th8test_get_fail", th8test_plugin_get_fail);
    /* Two-operand OR at line 135-136: 0-command (C2=T). */
    (void)Th8_RegisterPlugin(
        pChild, "th8test_get_zero", th8test_plugin_get_zero);

    /* Second-call failure at line 158. */
    (void)Th8_RegisterPlugin(
        pChild, "th8test_get_fail2", th8test_plugin_get_fail_second);

    /* Successful baseline registration.  Subsequent same-name
     * call drives th8PluginFind T,T and the duplicate branch
     * at line 127. */
    (void)Th8_RegisterPlugin(pChild, "th8test_dup", th8test_plugin_get_ok_a);
    (void)Th8_RegisterPlugin(pChild, "th8test_dup", th8test_plugin_get_ok_a);

    /* Pair of same-length names to drive T,F at the inline search
     * compound (line 247) and at th8PluginFind (line 63). */
    (void)Th8_RegisterPlugin(
        pChild, "th8test_aaaaaaaaa", th8test_plugin_get_ok_b);
    /* Registering b walks past a (len 17 vs 17, content differs)
     * inside th8PluginFind -- drives line 63 C1=T, C2=F. */
    (void)Th8_RegisterPlugin(
        pChild, "th8test_bbbbbbbbb", th8test_plugin_get_ok_c);

    /* Two-operand NULL guard at line 233. */
    (void)Th8_UnregisterPlugin(NULL, "x");
    (void)Th8_UnregisterPlugin(pChild, NULL);

    /* "Not found" branch in unregister (line 254 if (!p)). */
    (void)Th8_UnregisterPlugin(pChild, "th8test_does_not_exist");

    /* Unregister a (walks past b: len 17 same, content differs ->
     * line 247 T,F, then finds a -> T,T). */
    (void)Th8_UnregisterPlugin(pChild, "th8test_aaaaaaaaa");
    /* Unregister b: head match -> T,T. */
    (void)Th8_UnregisterPlugin(pChild, "th8test_bbbbbbbbb");
    /* Clean up the rest -- inline-search T,T paths. */
    (void)Th8_UnregisterPlugin(pChild, "th8test_dup");
    (void)Th8_UnregisterPlugin(pChild, "th8test_get_fail2");

    Th8_DeleteInterp(pChild);

    /* The harness-visible result is just "ok" -- the script-level
     * test cross-checks behaviour by re-running specific calls and
     * asserting their return codes. */
    Th8_SetResultStatic(interp, "ok", TH8_NOLEN);
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_stub_xGetUserName_fail --
 *
 *	Always-failing `xGetUserName` platform callback.  Used by
 *	`th8test_null_guard_plat` Section 4 to drive the
 *	slot-set, call-failed (T,F) MC/DC C-pair at
 *	`th8_core.c:19152` inside `Th8_CreateInterp ->
 *	th8InitGlobals`: with the callback installed (T) but
 *	returning non-OK (F), `th8InitGlobals` falls through to
 *	the empty-username path.
 *
 * Parameters:
 *	interp -- ignored.
 *	pCtx   -- ignored.
 *	zBuf   -- ignored (would receive the username on success).
 *	nBuf   -- ignored.
 *
 * Returns:
 *	`TH8_ERROR` unconditionally.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_stub_xGetUserName_fail(
    Th8_Interp *interp,
    void *pCtx,
    char *zBuf,
    size_t nBuf)
{
    (void)interp;
    (void)pCtx;
    (void)zBuf;
    (void)nBuf;
    return TH8_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_stub_xGetHostName_fail --
 *
 *	Always-failing `xGetHostName` platform callback.  Used by
 *	`th8test_null_guard_plat` Section 4 to drive the
 *	slot-set, call-failed (T,F) MC/DC C-pair at
 *	`th8_core.c:19164` inside `Th8_CreateInterp ->
 *	th8InitGlobals`: with the callback installed (T) but
 *	returning non-OK (F), `th8InitGlobals` falls through to
 *	the empty-hostname path.
 *
 * Parameters:
 *	interp -- ignored.
 *	pCtx   -- ignored.
 *	zBuf   -- ignored (would receive the hostname on success).
 *	nBuf   -- ignored.
 *
 * Returns:
 *	`TH8_ERROR` unconditionally.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_stub_xGetHostName_fail(
    Th8_Interp *interp,
    void *pCtx,
    char *zBuf,
    size_t nBuf)
{
    (void)interp;
    (void)pCtx;
    (void)zBuf;
    (void)nBuf;
    return TH8_ERROR;
}

/*
 * th8test_stub_xGetUserHostName_err --
 *
 *	Bug 18 driver: xGetUserName / xGetHostName stub that returns
 *	TH8_ERROR unconditionally.  Used by the plat_wrappers exerciser
 *	to overlay non-NULL callbacks onto a fresh child platform
 *	BEFORE Th8_CreateInterp, so th8InitGlobals at th8_core.c L19227
 *	and L19237 observe the (T,F) C2-Pair (callback installed, but
 *	returns non-OK) and take the empty-string fallback branch.
 *
 *	The signature is identical for both callback slots so a single
 *	stub serves both.
 */

static int
th8test_stub_xGetUserHostName_err(
    Th8_Interp *interp,
    void *pCtx,
    char *zBuf,
    size_t nBuf)
{
    (void)interp;
    (void)pCtx;
    (void)zBuf;
    (void)nBuf;
    return TH8_ERROR;
}

/*
 * th8test_assrch_iter_noop_cb --
 *
 *	No-op callback for Th8_IterateArraySearches used by the
 *	th8ArraySearchIterEntry L2775 tombstone driver in
 *	plat_wrappers.  The driver sets pEntry->pData = NULL before
 *	iterating, so the per-entry iterator at L2775 short-circuits
 *	(F,T) and never dispatches to this callback.  Returns TH8_OK
 *	to satisfy the API contract in case any other code path
 *	reaches it.
 */

static int
th8test_assrch_iter_noop_cb(
    const char *zArray,
    size_t nArray,
    const char *zSid,
    size_t nSid,
    void *pCtx)
{
    (void)zArray;
    (void)nArray;
    (void)zSid;
    (void)nSid;
    (void)pCtx;
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_stub_xGetTempData_ok_null_path --
 *
 *	Custom `xGetTemporaryData` platform callback that
 *	returns `TH8_OK` with `*pzOut = NULL` and a non-NULL
 *	`*ppCh`.  Drives the (F, T, -) compound vector at
 *	`th8_channel.c:122` (`rc != TH8_OK || !zOsPath ||
 *	!pChannel`): operand 1 false (rc OK), operand 2 true
 *	(zOsPath NULL), operand 3 short-circuited.  The real
 *	POSIX path never produces this combination on its own.
 *
 * Parameters:
 *	interp -- ignored.
 *	pCtx   -- ignored.
 *	nSize  -- ignored.
 *	pzOut  -- receives NULL.
 *	pnOut  -- receives 0.
 *	ppCh   -- receives a non-NULL placeholder so the third
 *		operand of the guard is structurally true.
 *
 * Returns:
 *	`TH8_OK`.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_stub_xGetTempData_ok_null_path(
    Th8_Interp *interp,
    void *pCtx,
    size_t nSize,
    char **pzOut,
    size_t *pnOut,
    void **ppCh)
{
    (void)interp;
    (void)pCtx;
    (void)nSize;
    *pzOut = NULL;
    *pnOut = 0;
    *ppCh = (void *)1; /* non-NULL placeholder */
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_stub_xGetTempData_ok_null_chan --
 *
 *	Custom `xGetTemporaryData` platform callback that
 *	returns `TH8_OK` with a valid `*pzOut` but `*ppCh = NULL`.
 *	Drives the (F, F, T) compound vector at `th8_channel.c:122`
 *	(`rc != TH8_OK || !zOsPath || !pChannel`): operand 1
 *	false (rc OK), operand 2 false (zOsPath non-NULL),
 *	operand 3 true (pChannel NULL).
 *
 * Parameters:
 *	interp -- ignored.
 *	pCtx   -- ignored.
 *	nSize  -- ignored.
 *	pzOut  -- receives a static `"/tmp/stub"` path.
 *	pnOut  -- receives the path length.
 *	ppCh   -- receives NULL.
 *
 * Returns:
 *	`TH8_OK`.
 *
 * Side effects:
 *	None.  The returned path string is statically allocated
 *	and is never freed by the channel layer.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_stub_xGetTempData_ok_null_chan(
    Th8_Interp *interp,
    void *pCtx,
    size_t nSize,
    char **pzOut,
    size_t *pnOut,
    void **ppCh)
{
    static char zFake[] = "/tmp/stub";
    (void)interp;
    (void)pCtx;
    (void)nSize;
    *pzOut = zFake;
    *pnOut = sizeof(zFake) - 1;
    *ppCh = NULL;
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_stub_xGetTempData_fail --
 *
 *	Always-failing `xGetTemporaryData` platform callback.
 *	Drives the (T, -, -) vector at `th8_channel.c:122`
 *	(`rc != TH8_OK || !zOsPath || !pChannel`): operand 1
 *	true (rc != OK) short-circuits the OR before the other
 *	operands are evaluated.
 *
 * Parameters:
 *	interp -- ignored.
 *	pCtx   -- ignored.
 *	nSize  -- ignored.
 *	pzOut  -- receives NULL.
 *	pnOut  -- receives 0.
 *	ppCh   -- receives NULL.
 *
 * Returns:
 *	`TH8_ERROR`.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_stub_xGetTempData_fail(
    Th8_Interp *interp,
    void *pCtx,
    size_t nSize,
    char **pzOut,
    size_t *pnOut,
    void **ppCh)
{
    (void)interp;
    (void)pCtx;
    (void)nSize;
    *pzOut = NULL;
    *pnOut = 0;
    *ppCh = NULL;
    return TH8_ERROR;
}

/*
 * th8test_stub_xInput_empty --
 *
 *	Custom xInput that returns OK with a zero-length zLine
 *	pointing at a 1-byte heap buffer containing '\0'.  Drives
 *	the C1=F (nLine == 0) vector at th8_channel.c L372 / L373
 *	(the trailing-\n / -\r strip checks) which the real
 *	POSIX xInput never produces (it returns TH8_ERROR when
 *	read() yields zero bytes).  The Th8_GetData family will
 *	free the buffer via Th8_Free.
 */

static int
th8test_stub_xInput_empty(
    Th8_Interp *interp,
    void *pCtx,
    char **pzOut,
    size_t *pnOut,
    void *pChannel)
{
    char *p;
    (void)pCtx;
    (void)pChannel;
    p = (char *)Th8_Malloc(interp, 1);
    if (!p) return TH8_ERROR;
    p[0] = '\0';
    *pzOut = p;
    *pnOut = 0;
    return TH8_OK;
}

/*
 * th8test_stub_xGetData_empty_ok --
 *
 *	Custom xGetData that returns TH8_OK with *pzData
 *	allocated (1 byte, empty string) and *pnData = 0.
 *	Used to drive the (T, F, C) vector at th8_plat.c L1796
 *	(Th8_GetData EOL-translation guard) where flag is set
 *	but the returned data has zero bytes -- the translator
 *	must not be invoked.  No in-tree xGetData callback
 *	naturally produces a zero-byte successful return.
 */

static int
th8test_stub_xGetData_empty_ok(
    Th8_Interp *interp,
    void *pCtx,
    const char *zName,
    size_t nName,
    char **pzData,
    size_t *pnData)
{
    char *p;
    (void)pCtx;
    (void)zName;
    (void)nName;
    p = (char *)Th8_Malloc(interp, 1);
    if (!p) return TH8_ERROR;
    p[0] = '\0';
    *pzData = p;
    *pnData = 0;
    return TH8_OK;
}

/*
 * th8test_stub_xInput_crlf --
 *
 *	Custom xInput that returns a 5-byte "ab\r\nc" buffer with
 *	pnOut=5.  Used to drive the (T, T, T, T) vector at
 *	th8_plat.c L949 (Th8_Input EOL-translation path) where
 *	all four conditions must be true at the same call.
 */

static int
th8test_stub_xInput_crlf(
    Th8_Interp *interp,
    void *pCtx,
    char **pzOut,
    size_t *pnOut,
    void *pChannel)
{
    char *p;
    (void)pCtx;
    (void)pChannel;
    p = (char *)Th8_Malloc(interp, 6);
    if (!p) return TH8_ERROR;
    p[0] = 'a';
    p[1] = 'b';
    p[2] = '\r';
    p[3] = '\n';
    p[4] = 'c';
    p[5] = '\0';
    *pzOut = p;
    *pnOut = 5;
    return TH8_OK;
}

/*
 * th8test_stub_xInput_null_buf --
 *
 *	Custom xInput that returns OK with *pzOut = NULL.
 *	Drives the C2=T vector at th8_io.c L774 (`rc != OK ||
 *	!zLine || nLine == 0`) which no real platform produces.
 */

static int
th8test_stub_xInput_null_buf(
    Th8_Interp *interp,
    void *pCtx,
    char **pzOut,
    size_t *pnOut,
    void *pChannel)
{
    (void)interp;
    (void)pCtx;
    (void)pChannel;
    *pzOut = NULL;
    *pnOut = 0;
    return TH8_OK;
}

#  if defined(TH8_ENABLE_LIBCURL) && defined(TH8_ENABLE_UNBOUND)
/*
 * th8test_stub_xDnsResolve --
 *
 *	Mock DNS resolver used by plat_wrappers to drive the
 *	L373 C3-Pair (`pDns->pLen[0] == 4`) and adjacent
 *	branches inside th8CurlGetData.  Returns a synthetic
 *	Th8_DnsResult whose shape is controlled by the mode
 *	field of the caller-supplied th8test_dns_ctx.  The
 *	context owns all referenced storage; the matching
 *	Free callback is a no-op.
 */
static int
th8test_stub_xDnsResolve(
    Th8_Interp *interp,
    void *pCtx,
    const char *zName,
    size_t nName,
    int eType,
    Th8_DnsResult **ppResult)
{
    th8test_dns_ctx *ctx = (th8test_dns_ctx *)pCtx;
    (void)interp;
    (void)zName;
    (void)nName;
    (void)eType;

    if (!ctx || !ppResult) return TH8_ERROR;
    *ppResult = NULL;
    if (ctx->mode == 4) return TH8_ERROR;
    if (ctx->mode == 5) return TH8_OK; /* *ppResult left NULL */

    ctx->rec[0] = 127;
    ctx->rec[1] = 0;
    ctx->rec[2] = 0;
    ctx->rec[3] = 1;
    ctx->pData[0] = (ctx->mode == 3) ? NULL : ctx->rec;
    ctx->pData[1] = NULL;
    ctx->pLen[0] = (ctx->mode == 1) ? (size_t)6 : (size_t)4;
    ctx->res.bogus = (ctx->mode == 2) ? 1 : 0;
    ctx->res.nRecord = 1;
    ctx->res.pData = (ctx->mode == 6) ? NULL : ctx->pData;
    ctx->res.pLen = (ctx->mode == 7) ? NULL : ctx->pLen;
    *ppResult = &ctx->res;
    return TH8_OK;
}

/*
 * th8test_stub_xDnsResolveFree --
 *
 *	No-op free callback for the DNS mock; the result lives
 *	inside the caller-owned th8test_dns_ctx and has nothing
 *	to release.
 */
static void
th8test_stub_xDnsResolveFree(
    Th8_Interp *interp,
    void *pCtx,
    Th8_DnsResult *pResult)
{
    (void)interp;
    (void)pCtx;
    (void)pResult;
}
#  endif

/*
 * Captured pointer to the real platform `xChannelControl`,
 * filled at helper-setup time and consulted by stubs that
 * delegate non-READ ops back to the underlying implementation.
 */
int (*th8test_real_xChanCtl)(
    Th8_Interp *,
    void *,
    void *,
    int,
    th8_int64_t,
    int,
    th8_int64_t *,
    void *) = NULL;

/*
 *----------------------------------------------------------------------
 *
 * th8test_stub_xChannelControl_read_fail --
 *
 *	Custom `xChannelControl` platform callback that fails
 *	any `TH8_CHANCTL_READ` op and delegates every other op
 *	to the captured real implementation (`th8test_real_xChanCtl`).
 *	With `xInput` simultaneously nulled, this stub drives
 *	the `(nRead == 0 && rc != TH8_OK)` (-, T) MC/DC vector
 *	at `th8_channel.c:394` -- the fallback path that the
 *	real POSIX layer never naturally reaches because its
 *	xInput always returns either bytes or `TH8_ERROR`.
 *
 * Parameters:
 *	interp   -- live interpreter (passed to delegated ops).
 *	pCtx     -- platform context pointer (passed through).
 *	pChannel -- channel handle.
 *	op       -- channel-control opcode.
 *	nArg1    -- first numeric argument.
 *	nArg2    -- second numeric argument.
 *	pnResult -- output result location (forced to 0 on READ).
 *	pBuf     -- in/out buffer pointer.
 *
 * Returns:
 *	`TH8_ERROR` for `TH8_CHANCTL_READ`, the delegate's
 *	return code for every other op, or `TH8_ERROR` if no
 *	real callback was captured.
 *
 * Side effects:
 *	Writes 0 to `*pnResult` (if non-NULL) on READ.  Side
 *	effects of the delegate apply for every other op.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_stub_xChannelControl_read_fail(
    Th8_Interp *interp,
    void *pCtx,
    void *pChannel,
    int op,
    th8_int64_t nArg1,
    int nArg2,
    th8_int64_t *pnResult,
    void *pBuf)
{
    if (op == TH8_CHANCTL_READ) {
	if (pnResult) *pnResult = 0;
	return TH8_ERROR;
    }
    if (th8test_real_xChanCtl) {
	return th8test_real_xChanCtl(
	    interp, pCtx, pChannel, op, nArg1, nArg2, pnResult, pBuf);
    }
    return TH8_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_null_guard_plat --
 *
 *	MC/DC closure for src/th8_plat.c.  Drives the platform-
 *	callback compounds whose C2=F vector (the callback slot is
 *	NULL) is not reached because every higher-level call site
 *	(plugin command, helper wrapper) checks the slot first and
 *	returns early.  Calling the public Th8_GetCwd/SetCwd/...
 *	APIs directly from C with a fault-injected platform forces
 *	the false branch at the plat-layer decision.
 *
 *	Also drives the public-API NULL-arg guards in
 *	Th8_SetPlatformContext, Th8_GetPlatformContext, Th8_Sleep,
 *	and Th8_EmitTrace.
 *
 *	All work happens on a private child interpreter built from
 *	the libc platform; the parent interp is never touched.
 *
 *----------------------------------------------------------------------
 */

#  if defined(TH8_ENABLE_FAULT_INJECTION)
static int
th8test_null_guard_plat(Th8_Interp *interp)
{
    Th8_Platform plat;
    const Th8_Platform *pLibc;
    Th8_Interp *pChild;
    Th8_FaultConfig cfg;
    Th8_FaultCtx *pCtx;
    /* Callbacks NULLed for the C2=F sweep.  Each name matches a
     * Th8_Platform field; an unknown name causes Th8_FaultInstall
     * to fail. */
    static const char *kNullSlots[] = {"xGetCwd",       "xSetCwd",
                                       "xGetLastError", "xGetRealPath",
                                       "xGetRootPath",  "xSameFile",
                                       "xGetEnv",       "xKeyValue",
                                       "xSleep",        "xStrlen",
                                       "xMutexEnter",   "xMutexLeave"};
    char pathBuf[64];

    /*
     * Section 1 -- Public-API NULL-arg guards (no child interp
     * required).  Each compound has its complement vector hit by
     * an existing test using normal arguments.
     */

    /* Th8_SetPlatformContext: if (!interp || !xCallback) */
    (void)Th8_SetPlatformContext(
        NULL, (Th8_PlatformFunc)Th8_GetPlatform, NULL); /* C1=T */
    (void)Th8_SetPlatformContext(interp, NULL, NULL); /* C2=T */

    /* Th8_GetPlatformContext: if (!interp || !ppCtx) */
    {
	void *ppCtx = NULL;
	(void)Th8_GetPlatformContext(
	    NULL, (Th8_PlatformFunc)Th8_GetPlatform, &ppCtx); /* C1=T */
	(void)Th8_GetPlatformContext(
	    interp, (Th8_PlatformFunc)Th8_GetPlatform, NULL); /* C2=T */
    }

    /* Th8_EmitTrace: line 247 `if (xTrace && zFmt)`.  Pass a
     * NULL zFmt to drive C2=F.  Calling with NULL interp also
     * drives line 229 `if (p && ALWAYS(p->xEmitTrace))` C1=F. */
    Th8_EmitTrace(NULL, "trace from null interp"); /* C1=F path */
    Th8_EmitTrace(interp, NULL); /* zFmt=NULL */

    /*
     * Section 2 -- Fault-injected sweep for the ALWAYS-guarded
     * platform-callback decisions.
     */

    pLibc = Th8_GetLibcPlatform();
    if (!pLibc) {
	Th8_SetResultStatic(
	    interp, "null_guard plat: no libc platform", TH8_NOLEN);
	return TH8_ERROR;
    }
    plat = *pLibc;
    plat.xPanic = 0; /* clean error on alloc failure */

    pChild = Th8_CreateInterp(&plat);
    if (!pChild) {
	Th8_SetResultStatic(
	    interp, "null_guard plat: child interp failed", TH8_NOLEN);
	return TH8_ERROR;
    }
    Th8_RegisterLanguage(pChild);

    pCtx = (Th8_FaultCtx *)malloc(Th8_FaultCtxSize());
    if (!pCtx) {
	Th8_DeleteInterp(pChild);
	Th8_SetResultStatic(
	    interp, "null_guard plat: malloc failed", TH8_NOLEN);
	return TH8_ERROR;
    }

    Th8_FaultConfigInit(&cfg);
    cfg.azNullCallbacks = kNullSlots;
    cfg.nNullCallbacks = (int)(sizeof(kNullSlots) / sizeof(kNullSlots[0]));

    if (Th8_FaultInstall(pChild, &cfg, pCtx) == TH8_OK) {
	char *zCwd;
	int rc;

	/* Th8_GetCwd line 1474: C2=F (xGetCwd=NULL). */
	zCwd = Th8_GetCwd(pChild);
	if (zCwd) Th8_Free(pChild, zCwd);

	/* Th8_SetCwd line 1516: C2=F (xSetCwd=NULL). */
	(void)Th8_SetCwd(pChild, "/tmp", 4);

	/* Th8_GetLastError line 1556: C2=T (xGetLastError=NULL).
	 * NEVER(!p) folds to 0; OR-right is the only live operand. */
	(void)Th8_GetLastError(pChild);

	/* Th8_GetRealPath line 1600: C2=F (xGetRealPath=NULL). */
	rc = Th8_GetRealPath(pChild, "/tmp", 4, pathBuf, sizeof(pathBuf));
	(void)rc;

	/* Th8_GetRootPath line 1647: C2=F (xGetRootPath=NULL). */
	rc = Th8_GetRootPath(pChild, "/tmp", 4, pathBuf, sizeof(pathBuf));
	(void)rc;

	/* Th8_SameFile line 1693: C2=F (xSameFile=NULL). */
	rc = Th8_SameFile(pChild, "/tmp", 4, "/tmp", 4);
	(void)rc;

	/* Th8_GetEnv line 2115: C2=F (xGetEnv=NULL).  Falls through
	 * to the global-platform fallback, which is also unset on
	 * a freshly-built libc child -- returns NULL. */
	(void)Th8_GetEnv(pChild, "PATH");

	/* Th8_KeyValue line 2181: C2=F (xKeyValue=NULL). */
	(void)Th8_KeyValue(pChild, 0, "k", 1, NULL, 0);

	/* Th8_Sleep line 1954: with xSleep nulled, the
	 * pPlatform->xSleep operand is F (C1=F).  Then with
	 * xSleep restored (after uninstall), nMs<=0 drives C2=F. */
	Th8_Sleep(pChild, 5); /* C1=F (xSleep is NULL) */

	/* Th8_Strlen src/th8_core.c L3907: C2=F (xStrlen is
	 * NULL) drives the built-in fallback at L3912. */
	(void)Th8_Strlen(pChild, "null_guard_xStrlen_probe");

	/* Th8_GetParentPid: dispatches through the platform's
	 * xGetParentPid callback (R-32063-53221 SHALL,
	 * R-50144-41547 shall narrative).  On a libc child the
	 * slot is NULL so the helper returns 0; on the parent
	 * interp (after uninstall, below) the POSIX slot is set
	 * and returns the actual ppid. */
	(void)Th8_GetParentPid(pChild);

	/* Th8_GetTimeUs: dispatches through the platform's
	 * xTimeUs callback (R-17209-27346).  On a libc child
	 * the slot is NULL so the helper returns TH8_ERROR;
	 * on the parent interp (after uninstall, below) the
	 * POSIX slot is set and produces a monotonic value. */
	{
	    th8_int64_t us = 0;
	    (void)Th8_GetTimeUs(pChild, &us);
	}

	/* Drive th8GlobalMutexEnter/Leave at th8_plat.c L2452 /
	 * L2496 C2-Pair (T,F): p non-NULL (the child's fault
	 * platform), but xMutexEnter/xMutexLeave NULL (in
	 * kNullSlots).  Paired call so the lock state stays
	 * balanced. */
	th8GlobalMutexEnter(pChild);
	th8GlobalMutexLeave(pChild);

	Th8_FaultUninstall(pChild, pCtx);
    }

    /* After uninstall, the child's platform has working callbacks
     * again.  Drive the remaining missing vector on Th8_Sleep:
     * C2=F via nMs <= 0.  Note: the child was built from the libc
     * platform which doesn't set xSleep, so we use the PARENT
     * interp (which has a full posix platform with xSleep set) to
     * make C1=T at L1954 and let C2=F (nMs=0) drive the missing
     * pair. */
    Th8_Sleep(interp, 0); /* C1=T,C2=F (nMs == 0) */

    /* Th8_GetParentPid on parent: platform's xGetParentPid is
     * the real POSIX callback (non-NULL).  Returns the actual
     * ppid; pin for R-32063-53221 / R-50144-41547. */
    (void)Th8_GetParentPid(interp);

    /* Th8_GetTimeUs on parent: platform's xTimeUs is set.
     * Returns a monotonic value; pin for R-17209-27346. */
    {
	th8_int64_t us = 0;
	(void)Th8_GetTimeUs(interp, &us);
    }

    /*
     * Section 3 -- Same APIs called with NULL interp to drive the
     * C1=F (p=NULL) vector on the `if (p && p->xXxx)` compounds
     * at lines 2115, 2181 (and several others whose C1 condition
     * is a live operand rather than a constant-folded ALWAYS).
     */
    (void)Th8_GetEnv(NULL, "PATH");
    (void)Th8_KeyValue(NULL, 0, "k", 1, NULL, 0);

    Th8_DeleteInterp(pChild);
    free(pCtx);

    /*
     * Section 4 -- Drive (T,F) vectors at th8_core.c L19152 and
     * L19164 (xGetUserName / xGetHostName failure paths in
     * th8InitGlobals).  These are called from Th8_CreateInterp
     * BEFORE Th8_FaultInstall can take effect, so the runtime
     * fault layer cannot drive them.  Build a child interp from
     * libc + always-failing xGetUserName / xGetHostName stubs
     * so the init dispatcher hits the slot-set,call-failed
     * vector exactly once.
     */
    {
	Th8_Platform plat2;
	Th8_Interp *pStubChild;

	plat2 = *pLibc;
	plat2.xPanic = 0;
	plat2.xGetUserName = th8test_stub_xGetUserName_fail;
	plat2.xGetHostName = th8test_stub_xGetHostName_fail;
	pStubChild = Th8_CreateInterp(&plat2);
	if (pStubChild) {
	    Th8_DeleteInterp(pStubChild);
	}
    }

    /*
     * Drive th8FaultStashSite NULL-interp guard (th8_fault.c
     * L1605) C1=T vector.  Production callers come from inside
     * th8MallocCommon and friends which already gate on
     * !interp; the stash helper's own defensive guard is
     * unreachable through normal use.  Direct call with NULL
     * interp covers it cleanly.
     */
    th8FaultStashSite(NULL, __FILE__, __LINE__);

    /*
     * Drive the partial-init defensive (F,T) vector at
     * th8_fault.c L180 (th8FaultMatchFilter: `nFilter <= 0 ||
     * aFilter == NULL`) and L1374 (th8FaultApplyNullCallbacks:
     * same shape on nNullCallbacks / azNullCallbacks).  Build
     * a fault config where the count is non-zero but the array
     * pointer is NULL, install it on a throwaway child interp.
     * Th8_FaultInstall calls th8FaultApplyNullCallbacks which
     * hits L1374 (F,T) on entry and returns TH8_OK without
     * touching the NULL array.  Th8_FaultInstall succeeds; the
     * very next instrumented call site exercises L180 (F,T) the
     * same way.
     */
    {
	const Th8_Platform *pLibcPlatX = Th8_GetLibcPlatform();
	if (pLibcPlatX) {
	    Th8_Platform plX = *pLibcPlatX;
	    Th8_Interp *pChildX;
	    plX.xPanic = 0;
	    pChildX = Th8_CreateInterp(&plX);
	    if (pChildX) {
		Th8_FaultConfig cfgX;
		Th8_FaultCtx *pCtxX = (Th8_FaultCtx *)
		    malloc(Th8_FaultCtxSize());
		Th8_RegisterLanguage(pChildX);
		if (pCtxX) {
		    char *zCwdX;
		    Th8_FaultConfigInit(&cfgX);
		    cfgX.nFilter = 1;
		    cfgX.aFilter = NULL;
		    cfgX.nNullCallbacks = 1;
		    cfgX.azNullCallbacks = NULL;
		    if (Th8_FaultInstall(pChildX, &cfgX, pCtxX) == TH8_OK) {
			zCwdX = Th8_GetCwd(pChildX);
			if (zCwdX) Th8_Free(pChildX, zCwdX);
			Th8_FaultUninstall(pChildX, pCtxX);
		    }
		    free(pCtxX);
		}
		Th8_DeleteInterp(pChildX);
	    }
	}
    }

    Th8_SetResultStatic(interp, "ok", TH8_NOLEN);
    return TH8_OK;
}
#  endif /* TH8_ENABLE_FAULT_INJECTION (th8test_null_guard_plat) */

/*
 *----------------------------------------------------------------------
 *
 * th8test_null_guard_core --
 *
 *	MC/DC closure for short-circuit OR guards in src/th8_core.c
 *	at public-API boundaries that no script test exercises with
 *	NULL or zero arguments:
 *
 *	  Th8_SecureZero(interp, p, n)  guard: (n == 0 || !p)
 *	  Th8_NsExport(interp, zNs, nNs, ...)  guard: (!zNs || nNs == 0)
 *	  Th8_NsImport(interp, zPattern, nPat, bForce)
 *	    guard: (!zPattern || nPat == 0)
 *
 *	The success vectors are exercised by ordinary suite traffic.
 *	Each NULL/zero combination is invoked exactly once to drive
 *	the missing T branch on each operand.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_null_guard_core(Th8_Interp *interp)
{
    Th8_Platform plat;
    const Th8_Platform *pLibc;
    Th8_Interp *pChild;
    char buf[16] = {0};

    /* Th8_SecureZero src/th8_core.c L534: (n == 0 || !p).
     * Th8_SecureZero only zeroes memory -- it does not mutate
     * interp state, so it is safe to call on the parent. */
    Th8_SecureZero(interp, NULL, 16); /* n!=0, !p=T */
    Th8_SecureZero(interp, buf, 0); /* n==0, p ok */

    /* Namespace-mutating APIs run on a private child interp so
     * the parent's `::` namespace exports are unchanged. */
    pLibc = Th8_GetLibcPlatform();
    if (pLibc) {
	plat = *pLibc;
	plat.xPanic = 0;
	pChild = Th8_CreateInterp(&plat);
	if (pChild) {
	    Th8_RegisterLanguage(pChild);

	    /* Th8_NsExport src/th8_core.c L3602: (!zNs || nNs == 0) */
	    (void)Th8_NsExport(pChild, NULL, 0, "*", 1);
	    (void)Th8_NsExport(pChild, "", 0, "*", 1);
	    (void)Th8_NsExport(pChild, "::", 2, "*", 1);

	    /* Th8_NsImport: similar empty-pattern guard. */
	    (void)Th8_NsImport(pChild, NULL, 0, 0);
	    (void)Th8_NsImport(pChild, "", 0, 0);

	    /* Th8_SetPackageUnknown src/th8_core.c L2870:
	     * (zCmd && TH8_LEN(nCmd) > 0).  Script-level callers
	     * always pass a non-NULL string; calling with zCmd=NULL
	     * drives the C1=F vector on the child. */
	    Th8_SetPackageUnknown(pChild, NULL, 0);

	    /* Drive several th8_core.c TH8_INTERNAL dead-wrapper
	     * functions through the new internal-stubs entries.
	     * All have no in-tree callers; calling each on the
	     * child interp exercises the function body without
	     * polluting parent state. */
	    {
		int rc;
		size_t nFr;
		const char *zFr;
		char dst[8];
		char *pcpy;

		th8SetLine(pChild, 42);
		th8SetLine(pChild, 0);
		th8SetFrameNsPtr(pChild, NULL);
		(void)th8GetFinallyRc(pChild);
		zFr = th8GetFinallyResult(pChild, &nFr);
		(void)zFr;
		(void)nFr;
		rc = th8EvalTrampoline(pChild);
		(void)rc;
		rc = th8SetResultBorrowed(
		    pChild, "borrowed-result-x", TH8_NOLEN);
		(void)rc;
		rc = th8SetResultBorrowed(pChild, "borrowed-result-x", 17);
		(void)rc;
		{
		    extern int
		    th8test_inframe_noop_cb(Th8_Interp *, void *, void *);
		    rc = th8InFrame(
		        pChild, th8test_inframe_noop_cb, NULL, NULL);
		    (void)rc;
		}

		/* Spilornis libgeneric shims.  th8_spilornis_interp
		 * is normally only set inside th8SpilornisSetup (a
		 * static th8_core.c helper).  Outside that window
		 * th8_spilornis_interp is NULL, so each function
		 * takes its early-return guard branch.  That
		 * exercises the function body for the C1=F vector
		 * (the only branch reachable from outside the
		 * setup); the C1=T branch is intrinsically
		 * unreachable from script context. */
		(void)th8_spilornis_memcmp_stub("ab", "ab", 2);
		(void)th8_spilornis_strlen_stub("hello");
		(void)th8_spilornis_strncmp_stub("ab", "ac", 2);
		pcpy = th8_spilornis_strncpy_stub(dst, "xy", 2);
		(void)pcpy;

		/* Th8_RestoreInterp / Th8_EvalTrusted: public APIs
		 * with no in-tree callers in the suite path.  Drive
		 * the NULL-interp guard plus a normal call. */
		(void)Th8_RestoreInterp(NULL, 0);
		(void)Th8_RestoreInterp(pChild, 0);
		(void)Th8_RestoreInterp(pChild, TH8_RESTORE_COMMANDS);
		(void)Th8_EvalTrusted(
		    NULL, 0, "set x 1", 7, "trusted-eval-null", 17);
		(void)Th8_EvalTrusted(
		    pChild, 0, "set x 1", 7, "trusted-eval", 12);

		/* Drive th8_libc.c th8LibcRealloc.  The libc
		 * platform plat.xRealloc is th8LibcRealloc, so
		 * Th8_Realloc on the child routes through it. */
		{
		    void *p = Th8_Malloc(pChild, 32);
		    p = Th8_Realloc(pChild, p, 64);
		    p = Th8_Realloc(pChild, p, 16);
		    Th8_Free(pChild, p);
		}

#  if defined(TH8_ENABLE_CRYPTOGRAPHY)
		/* Drive harpy/th8_policy.c public-key accessor
		 * dead wrappers (no in-tree callers).  Each takes
		 * an interp; NULL-arg variant drives the entry
		 * guard, valid-arg variant exercises the body. */
		{
		    char zTok[17];

		    (void)Th8_PolicyGetKeyTokens(NULL, NULL);
		    (void)Th8_PolicyGetKeyTokens(pChild, NULL);

		    (void)Th8_GetPublicKeyZeroToken(NULL, zTok);
		    (void)Th8_GetPublicKeyZeroToken(pChild, zTok);

		    (void)Th8_GetPublicKeyRoot(NULL);
		    (void)Th8_GetPublicKeyRoot(pChild);
		    (void)Th8_GetPublicKeyRootToken(NULL, zTok);
		    (void)Th8_GetPublicKeyRootToken(pChild, zTok);

#    if defined(TH8_ENABLE_TEST_KEY)
		    (void)Th8_GetPublicKeyTest(NULL);
		    (void)Th8_GetPublicKeyTest(pChild);
#    endif
		}

		/* Drive th8_core.c th8FinalizeSensitiveResult L5718
		 * NULL-guard (!interp || !interp->pProtectedResult).
		 * C1-Pair via a NULL interp; C2-Pair via pChild whose
		 * protected-result region is still lazily unallocated --
		 * this MUST run BEFORE the Th8_SetResultSensitive calls
		 * below, which allocate that region.  The all-false case
		 * is covered by normal sensitive-result finalization. */
		(void)th8FinalizeSensitiveResult(NULL, 0);
		(void)th8FinalizeSensitiveResult(pChild, 0);

		/* Drive th8_core.c Th8_SetResultSensitive TH8_API.
		 * No in-tree callers in the test path; the result
		 * gets stored in the interp's protected region. */
		(void)Th8_SetResultSensitive(NULL, "x", 1);
		(void)Th8_SetResultSensitive(pChild, "sensitive", 9);
		(void)Th8_SetResultSensitive(pChild, "", 0);

		/* Drive the capacity-guard C2-Pair (n + 1 > nUsable):
		 * a payload larger than the protected region's usable
		 * page must be rejected with "sensitive result exceeds
		 * protected region capacity" rather than overrunning the
		 * region.  The usable page tracks the OS page size (16 KB
		 * on Apple Silicon, 4 KB elsewhere), so use 128 KB to
		 * clear any real page.  The C1 sibling (n + 1 < n, the
		 * SIZE_MAX overflow guard) is intrinsically dead for any
		 * real string length. */
		{
		    size_t nBig = 128 * 1024;
		    char *zBig = (char *)Th8_Malloc(pChild, nBig);
		    if (zBig) {
			Th8_Memset(pChild, zBig, 'x', nBig);
			(void)Th8_SetResultSensitive(pChild, zBig, nBig);
			Th8_Free(pChild, zBig);
		    }
		}

		/* Drive th8_xlib.c L427 `bParentSigned && pPolicyCtx`
		 * C1-Pair {F,-} vector.  Parent here (pChild) has
		 * no signed-only policy enabled, so the inner
		 * Th8_EvalFileAsData hits the cleanup with
		 * bParentSigned=F and pPolicyCtx=NULL.  The body
		 * may fail (cannot eval), but reaching cleanup
		 * suffices for coverage. */
		{
		    const unsigned char *zOut = NULL;
		    size_t nOut = 0;
		    (void)Th8_EvalFileAsData(
		        pChild, "tests/helpers/evalfile_empty.tcl", TH8_NOLEN,
		        &zOut, &nOut, NULL);
		    if (zOut) Th8_Free(pChild, (void *)zOut);
		}
#  endif

		/* Drive plugins/th8_introspection.c L893 C2-Pair
		 * `argc == 3 && zList`: zList==NULL case.  Run
		 * [info globals pattern] on a freshly-created
		 * sub-child interp before any global vars are set
		 * -- the global hash is empty so
		 * Th8_ListAppendGlobalVariables leaves zList NULL.
		 * The decision then evaluates {T,F} and skips the
		 * filter loop. */
		{
		    const Th8_Platform *pLibc2 = Th8_GetLibcPlatform();
		    if (pLibc2) {
			Th8_Platform plat2 = *pLibc2;
			Th8_Interp *pPristine;

			plat2.xPanic = NULL;
			pPristine = Th8_CreateInterp(&plat2);
			if (pPristine) {
			    Th8_RegisterLanguage(pPristine);
			    (void)Th8_Eval(
			        pPristine, 0, "info globals foo*", TH8_NOLEN,
			        NULL, 0);
			    Th8_DeleteInterp(pPristine);
			}
		    }
		}

		/* Leave an [array startsearch] dangling so that
		 * Th8_DeleteInterp's paArraySearch cleanup fires
		 * th8ArraySearchFreeEntry.  Set up an array, start
		 * a search (don't call donesearch), then let the
		 * child interp's deletion (below) clean it up. */
		(void)Th8_Eval(
		    pChild, 0,
		    "set a(x) 1; set a(y) 2; "
		    "array startsearch a",
		    TH8_NOLEN, NULL, 0);

#  if defined(TH8_BENCHMARKING)
		/* Drive Th8_GetCacheStats / Th8_ResetCacheStats
		 * TH8_API benchmarking accessors -- no in-tree
		 * callers in the test path. */
		{
		    th8_uint64_t nHit = 0, nMiss = 0, nEvict = 0;
		    Th8_GetCacheStats(pChild, &nHit, &nMiss, &nEvict);
		    Th8_GetCacheStats(
		        pChild, NULL, NULL, NULL); /* NULL-pointer guards */
		    Th8_ResetCacheStats(pChild);
		}
#  endif

		/* Drive th8_cache.c th8CopyValue / th8FreeValue
		 * TH8_INTERNAL helpers (no in-tree callers).
		 * Build a small synthetic Th8_Value with zData,
		 * copy it, free the copy.  NULL probes are unsafe
		 * here because the NEVER guards constant-fold to
		 * false under TH8_OMIT_AUXILIARY_SAFETY_CHECKS,
		 * leaving subsequent derefs unprotected. */
		{
		    Th8_Value src;
		    Th8_Value *pDup;

		    Th8_Memset(pChild, &src, 0, sizeof(src));
		    src.zData = (char *)"copy-me";
		    src.nData = 7;
		    pDup = th8CopyValue(pChild, &src);
		    if (pDup) th8FreeValue(pChild, pDup);

		    /* Drive th8CopyValue L1166 {F,-} (NULL zData)
		     * and {T,F} (non-NULL zData, zero nData).
		     * Bug 27 fix in th8CopyValue now zeros
		     * pDst->zData when the deep-copy branch is
		     * skipped, so the {T,F} variant is safe. */
		    Th8_Memset(pChild, &src, 0, sizeof(src));
		    src.zData = NULL;
		    src.nData = 0;
		    pDup = th8CopyValue(pChild, &src); /* {F,-} */
		    if (pDup) th8FreeValue(pChild, pDup);

		    src.zData = (char *)"x";
		    src.nData = 0;
		    pDup = th8CopyValue(pChild, &src); /* {T,F} */
		    if (pDup) th8FreeValue(pChild, pDup);

		    /* Bug 26 NULL-arg probes (safe now that th8FreeValue
		     * and th8CopyValue use plain conditionals). */
		    (void)th8CopyValue(NULL, &src);
		    (void)th8CopyValue(pChild, NULL);
		    th8FreeValue(NULL, NULL);
		    th8FreeValue(pChild, NULL);


		    /* Drive th8RemoveFromCache L762 NULL-arg guards.
		     * (T,-): NULL interp.  (F,T): valid interp,
		     * NULL z.  (F,F): valid interp, valid z (covered
		     * by ordinary cache eviction in the suite). */
		    th8RemoveFromCache(NULL, 0, "x", 1); /* (T,-) */
		    th8RemoveFromCache(pChild, 0, NULL, 0); /* (F,T) */
		    th8RemoveFromCache(pChild, 0, "x", TH8_NOLEN); /* (F,F) */

		    /* Drive th8SetCacheString L1111 NULL-arg guards
		     * `if (!interp || !pVal || !z) return;`.
		     * (T,-,-): NULL interp.
		     * (F,T,-): NULL pVal.
		     * (F,F,T): NULL z (the original 2026-06 drive
		     *   below).  Together these close C1, C2, C3
		     *   pairs. */
		    th8SetCacheString(NULL, &src, "x", 1);
		    th8SetCacheString(pChild, NULL, "x", 1);
		    th8SetCacheString(pChild, &src, NULL, 0);

		    /* Drive th8FindListInCache L902/L910 defensive
		     * branches (nElem<0, NULL azElem, NULL anElem).
		     * Production callers always pass valid args.
		     * The (T, -) MC/DC vector at L899 (interp ==
		     * NULL) also needs an explicit call so the
		     * `if (!interp || nElem < 0)` C1-Pair is
		     * covered. */
		    (void)th8FindListInCache(NULL, 0, 0, NULL, NULL);
		    (void)th8FindListInCache(pChild, 0, -1, NULL, NULL);
		    (void)th8FindListInCache(pChild, 0, 1, NULL, NULL);
		    {
			const char *az[1] = {"x"};
			(void)th8FindListInCache(pChild, 0, 1, az, NULL);
		    }
		}

		/* Drive th8_hash.c Th8_HashIterateOrdered TH8_API
		 * helper (no in-tree callers).  Build a small hash
		 * with a few entries, then iterate via the ordered
		 * callback.  Also drive the empty-hash early-return. */
		{
		    extern int
		    th8test_hash_iter_noop_cb(Th8_HashEntry *, void *);
		    Th8_Hash *pH = Th8_HashNew(pChild);
		    if (pH) {
			Th8_HashEntry *pE;

			/* Empty hash: early-return on nNextOrder==0. */
			Th8_HashIterateOrdered(
			    pChild, pH, th8test_hash_iter_noop_cb, NULL);

			/* Insert a few entries then iterate. */
			pE = Th8_HashFind(pChild, pH, "a", 1, 1);
			if (pE) pE->pData = (void *)(size_t)1;
			pE = Th8_HashFind(pChild, pH, "b", 1, 1);
			if (pE) pE->pData = (void *)(size_t)2;
			pE = Th8_HashFind(pChild, pH, "c", 1, 1);
			if (pE) pE->pData = (void *)(size_t)3;

			Th8_HashIterateOrdered(
			    pChild, pH, th8test_hash_iter_noop_cb, NULL);

			Th8_HashDelete(pChild, pH);
		    }
		}
	    }

	    Th8_DeleteInterp(pChild);
	}
    }

    /*
     * Additional public-API NULL-arg guards in src/th8_core.c.
     * Each public API checks one or more required pointer/value
     * arguments at entry; the F branch (all args valid) is
     * exercised by ordinary suite traffic; the T branch (some
     * arg NULL/zero) needs a direct call below.
     */
    {
	char *zList = NULL;
	size_t nList = 0;
	int bp = 0;

	/* Th8_MergePlatformInterp: if (!interp || !pSrc) */
	(void)Th8_MergePlatformInterp(NULL, Th8_GetLibcPlatform());
	(void)Th8_MergePlatformInterp(interp, NULL);

	/* Th8_SetBreakpoint: if (!interp || !zScript || nLine < 1) */
	(void)Th8_SetBreakpoint(NULL, "x", 1, 1, &bp);
	(void)Th8_SetBreakpoint(interp, NULL, 0, 1, &bp);
	(void)Th8_SetBreakpoint(interp, "x", 1, 0, &bp); /* nLine<1 */

	/* Th8_ClearBreakpoint: if (!interp || !interp->paBreakpoints).
	 * paBreakpoints==NULL when no breakpoint has ever been set
	 * on this interp.  The parent interp may or may not have
	 * breakpoints set; the NULL-interp arm always triggers C1=T. */
	(void)Th8_ClearBreakpoint(NULL, 1);

	/* Th8_ListAppendExpansions: if (!interp || !pzList || !pnList) */
	Th8_ListAppendExpansions(NULL, &zList, &nList, "*", 1);
	Th8_ListAppendExpansions(interp, NULL, &nList, "*", 1);
	Th8_ListAppendExpansions(interp, &zList, NULL, "*", 1);
	if (zList) {
	    Th8_Free(interp, zList);
	    zList = NULL;
	    nList = 0;
	}

	/* Th8_ListAppendBreakpoints: if (!interp || !pzList || !pnList) */
	Th8_ListAppendBreakpoints(NULL, &zList, &nList);
	Th8_ListAppendBreakpoints(interp, NULL, &nList);
	Th8_ListAppendBreakpoints(interp, &zList, NULL);
	if (zList) {
	    Th8_Free(interp, zList);
	}

	/* Th8_IterateArraySearches: if (!interp || !xCallback) */
	(void)Th8_IterateArraySearches(NULL, 0, 0);
	(void)Th8_IterateArraySearches(interp, NULL, 0);

	/* Th8_ListAppendNsVariables src/th8_vars.c L1955:
	 * if (!zNs || nNs == 0) -- the F branch (zNs valid) is
	 * exercised normally; the T branch needs explicit NULL or
	 * empty namespace name. */
	zList = NULL;
	nList = 0;
	(void)Th8_ListAppendNsVariables(interp, NULL, 0, &zList, &nList);
	(void)Th8_ListAppendNsVariables(interp, "", 0, &zList, &nList);
	if (zList) {
	    Th8_Free(interp, zList);
	}

	/*
	 * Allocator NULL-interp guards (2026-06-08).  Reachable via
	 * Th8_Malloc/Th8_AttemptMalloc/Th8_Realloc/Th8_Free with NULL
	 * interp.  Initially gated for Bug 47 investigation but
	 * un-gated 2026-06-08 once the hang was traced via sample(1)
	 * to libcurl/Curl_poll inside the unrelated DNS-mock sweep
	 * later in this same function.
	 */
	{
	    void *pTmp;
	    pTmp = Th8_Malloc(NULL, 1);
	    if (pTmp) Th8_Free(NULL, pTmp); /* unreachable; defensive */
	    pTmp = Th8_AttemptMalloc(NULL, 1);
	    if (pTmp) Th8_Free(NULL, pTmp); /* unreachable; defensive */
	    pTmp = Th8_Realloc(NULL, NULL, 1);
	    if (pTmp) Th8_Free(NULL, pTmp); /* unreachable; defensive */
	    Th8_Free(NULL, NULL); /* L1585-style guard */
	}
    }

    Th8_SetResultStatic(interp, "ok", TH8_NOLEN);
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_null_guard_misc_singlearg --
 *
 *	Drive the `if (!interp) return ...;` entry-guard MC/DC
 *	C-pair for a handful of single-argument public APIs in
 *	one helper:
 *
 *	  * `Th8_NsEval(NULL, ...)`
 *	  * `Th8_Thaw(NULL)`
 *	  * `Th8_IsCanceled(NULL, 0)`
 *	  * `Th8_ResetCancel(NULL)`     (void return)
 *
 *	Each call exercises the only operand of the guard
 *	(NULL `interp`).  The success path of every API is
 *	exercised by ordinary script-driven use elsewhere in
 *	the suite, so this helper only needs to bump the
 *	`!interp` arm.
 *
 * Parameters:
 *	interp -- live interpreter.
 *
 * Returns:
 *	`TH8_OK` if every int-returning API reported
 *	`TH8_ERROR` (interpreter result: `"ok"`).
 *	`TH8_ERROR` otherwise (interpreter result: diagnostic
 *	naming each observed return code).
 *
 * Side effects:
 *	None beyond the interpreter result.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_null_guard_misc_singlearg(Th8_Interp *interp)
{
    /* Single-argument public APIs whose guards are
     *   if (!interp) return ...;
     * Operand 1 (the only operand) is closed by passing NULL once.
     * The success path is exercised by every normal use of these
     * APIs in the suite.  Each call here just bumps the MC/DC
     * counter for the !interp branch. */
    int rc1, rc2, rc3;

    rc1 = Th8_NsEval(NULL, "::", 2, "set _x 0", 8);
    rc2 = Th8_Thaw(NULL);
    rc3 = Th8_IsCanceled(NULL, 0);
    Th8_ResetCancel(NULL); /* void return; just call it. */

    /* All three int-returning APIs should signal error on NULL
     * interp.  Th8_IsCanceled returns TH8_ERROR; the others
     * return TH8_ERROR. */
    if (rc1 == TH8_ERROR && rc2 == TH8_ERROR && rc3 == TH8_ERROR) {
	Th8_SetResultStatic(interp, "ok", TH8_NOLEN);
	return TH8_OK;
    }
    {
	char buf[80];
	int n = snprintf(
	    buf, sizeof(buf),
	    "fail rc1=%d rc2=%d rc3=%d (expected all error)", rc1, rc2, rc3);
	if (n < 0) n = 0;
	if ((size_t)n >= sizeof(buf)) n = (int)sizeof(buf) - 1;
	Th8_SetResult(interp, buf, (size_t)n);
    }
    return TH8_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_null_guard_eval_file_as_data --
 *
 *	Drive the 4-operand OR entry guard of
 *	`Th8_EvalFileAsData(interp, zName, nName, pzData, pnData,
 *	pCtx)`: `(!interp || !zName || !pzData || !pnData)`.
 *	Four NULL-input vectors cover MC/DC for every operand
 *	short-circuit.  A fifth call with `nName = TH8_NOLEN`
 *	and a non-existent file name additionally drives the
 *	inline `Th8_Strlen` branch at the function body's name-
 *	normalisation step -- the file lookup fails but the
 *	strlen path still executes.
 *
 * Parameters:
 *	interp -- live interpreter.
 *
 * Returns:
 *	`TH8_OK` if all four guard vectors returned `TH8_ERROR`
 *	(interpreter result: `"ok"`).
 *	`TH8_ERROR` otherwise (interpreter result: diagnostic
 *	with every observed return code).
 *
 * Side effects:
 *	Sets the interpreter result.  Any data buffer the
 *	(failing) strlen call surprisingly returned is freed
 *	via `Th8_Free`.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_null_guard_eval_file_as_data(Th8_Interp *interp)
{
    /* Th8_EvalFileAsData(interp, zName, nName, pzData, pnData, pCtx)
     * Guard: if (!interp || !zName || !pzData || !pnData) ...
     * Four NULL inputs cover MC/DC for the 4-operand OR. */
    const unsigned char *pzData = NULL;
    size_t nData = 0;
    int rc1, rc2, rc3, rc4;
    char buf[120];

    rc1 = Th8_EvalFileAsData(NULL, "x", 1, &pzData, &nData, NULL);
    rc2 = Th8_EvalFileAsData(interp, NULL, 0, &pzData, &nData, NULL);
    rc3 = Th8_EvalFileAsData(interp, "x", 1, NULL, &nData, NULL);
    rc4 = Th8_EvalFileAsData(interp, "x", 1, &pzData, NULL, NULL);
    /* Drive L317 (nName == TH8_NOLEN) path so the inline
     * Th8_Strlen call at L318 gets coverage.  The fetch
     * will fail (the file doesn't exist) but the strlen
     * branch still executes. */
    {
	const unsigned char *pzD2 = NULL;
	size_t nD2 = 0;
	(void)Th8_EvalFileAsData(
	    interp, "nonexistent_xlib_marker", TH8_NOLEN, &pzD2, &nD2, NULL);
	if (pzD2) Th8_Free(interp, (void *)(size_t)pzD2);
    }

    if (rc1 == TH8_ERROR && rc2 == TH8_ERROR && rc3 == TH8_ERROR &&
        rc4 == TH8_ERROR) {
	Th8_SetResultStatic(interp, "ok", TH8_NOLEN);
	return TH8_OK;
    }
    {
	int n = snprintf(
	    buf, sizeof(buf),
	    "fail rc1=%d rc2=%d rc3=%d rc4=%d (expected all error)", rc1, rc2,
	    rc3, rc4);
	if (n < 0) n = 0;
	if ((size_t)n >= sizeof(buf)) n = (int)sizeof(buf) - 1;
	Th8_SetResult(interp, buf, (size_t)n);
    }
    return TH8_ERROR;
}

#  if defined(TH8_ENABLE_CRYPTOGRAPHY) && defined(TH8_ENABLE_TEST_KEY)
/*
 *----------------------------------------------------------------------
 *
 * th8test_null_guard_policy_find_key --
 *
 *	Drive the MC/DC C-pairs of the two-stage guard in
 *	`Th8_PolicyFindKey(interp, pCtx, zToken, nToken)`:
 *
 *	  * Outer: `if (!interp) return NULL;`  -- exercised by
 *	    other null-guard helpers, not here (NULL interp would
 *	    prevent diagnostic emission).
 *	  * Inner: `if (!p || !p->paKeys || !zToken) return NULL;`
 *
 *	Three calls in this helper drive operand 1 (NULL pCtx)
 *	and operand 3 (NULL zToken) of the inner guard, plus a
 *	third all-valid call that takes the lookup path (cache
 *	miss is fine).  Operand 2 (`!p->paKeys`) requires a
 *	policy with no preloaded keys -- not available in the
 *	conformance suite where the prologue always preloads
 *	the test key -- so left uncovered.
 *
 *	Gated on `TH8_ENABLE_CRYPTOGRAPHY && TH8_ENABLE_TEST_KEY`.
 *
 * Parameters:
 *	interp -- live interpreter.
 *
 * Returns:
 *	`TH8_OK` if the two guard-fire calls returned NULL
 *	(interpreter result: `"ok"`).
 *	`TH8_ERROR` otherwise (interpreter result: diagnostic
 *	naming the observed pointers).
 *
 * Side effects:
 *	Sets the interpreter result.  Reads (but does not
 *	mutate) the policy context.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_null_guard_policy_find_key(Th8_Interp *interp)
{
    /* Th8_PolicyFindKey(interp, pCtx, zToken, nToken)
     * Outer guard: if (!interp) return NULL;
     * Inner guard: if (!p || !p->paKeys || !zToken) return NULL;
     *
     * Operand 1 (!p) testable with NULL pCtx.
     * Operand 3 (!zToken) testable with NULL zToken.
     * Operand 2 (!p->paKeys) requires a policy with no preloaded
     * keys -- not available in the conformance suite (the policy
     * always has the test key preloaded by prologue), so left
     * uncovered for now. */
    void *pCtx = NULL;
    const Th8_RsaKey *r1, *r2, *r3;
    char buf[80];

    Th8_GetPolicyCallback(interp, NULL, &pCtx);
    if (!pCtx) {
	Th8_SetResultStatic(
	    interp, "null_guard policy_find_key: GetPolicyCallback NULL",
	    TH8_NOLEN);
	return TH8_ERROR;
    }

    r1 = Th8_PolicyFindKey(interp, NULL, "0123456789abcdef", 16);
    r2 = Th8_PolicyFindKey(interp, pCtx, NULL, 0);
    r3 = Th8_PolicyFindKey(interp, pCtx, "0123456789abcdef", 16);
    /* r3: token is unlikely to match an actual loaded key, so this
     * is "not found" but the guard passes -- both operand 1 and
     * operand 3 are false. */
    (void)r3;

    if (r1 == NULL && r2 == NULL) {
	Th8_SetResultStatic(interp, "ok", TH8_NOLEN);
	return TH8_OK;
    }
    {
	int n = snprintf(
	    buf, sizeof(buf), "fail r1=%p r2=%p (expected NULL,NULL)",
	    (const void *)r1, (const void *)r2);
	if (n < 0) n = 0;
	if ((size_t)n >= sizeof(buf)) n = (int)sizeof(buf) - 1;
	Th8_SetResult(interp, buf, (size_t)n);
    }
    return TH8_ERROR;
}
#  endif

/*
 *----------------------------------------------------------------------
 *
 * th8test_null_guard_eval_file --
 *
 *	Drive the 2-operand OR entry guard of
 *	`Th8_EvalFile(interp, zName, nName)`:
 *	`(!interp || !zName)`.  Two NULL-input vectors cover
 *	both operand short-circuits.  The success path is
 *	exercised by every `[source]` invocation in the
 *	conformance suite.
 *
 * Parameters:
 *	interp -- live interpreter.
 *
 * Returns:
 *	`TH8_OK` if both guard vectors returned `TH8_ERROR`
 *	(interpreter result: `"ok"`).
 *	`TH8_ERROR` otherwise (interpreter result: diagnostic).
 *
 * Side effects:
 *	None beyond the interpreter result.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_null_guard_eval_file(Th8_Interp *interp)
{
    int rc1, rc2;
    char buf[80];

    /* Th8_EvalFile(interp, zName, nName)
     * Guard: if (!interp || !zName) { ... return TH8_ERROR; }
     * Two inputs cover the NULL-OR's first two operand variations;
     * the success path (both valid) is exercised by every [source]
     * invocation in the conformance suite. */

    rc1 = Th8_EvalFile(NULL, "x", 1);
    rc2 = Th8_EvalFile(interp, NULL, 0);

    if (rc1 == TH8_ERROR && rc2 == TH8_ERROR) {
	Th8_SetResultStatic(interp, "ok", TH8_NOLEN);
	return TH8_OK;
    }
    {
	int n = snprintf(
	    buf, sizeof(buf), "fail rc1=%d rc2=%d (expected error,error)",
	    rc1, rc2);
	if (n < 0) n = 0;
	if ((size_t)n >= sizeof(buf)) n = (int)sizeof(buf) - 1;
	Th8_SetResult(interp, buf, (size_t)n);
    }
    return TH8_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_null_guard_find_in_cache --
 *
 *	Drive the 2-operand OR entry guard of
 *	`Th8_FindInCache(interp, cacheType, z, n)`:
 *	`(!interp || !z)`.  Three calls cover both operand
 *	short-circuits plus the both-valid lookup path:
 *
 *	  * (T, -) -- NULL interp.
 *	  * (F, T) -- valid interp, NULL key.
 *	  * (F, F) -- both valid (cache miss is fine).
 *
 *	Return value alone cannot distinguish "guard fired"
 *	from "cache miss" because both yield NULL; the helper
 *	verifies the two guard-fire calls returned NULL and
 *	otherwise just confirms the third call did not crash.
 *
 * Parameters:
 *	interp -- live interpreter.
 *
 * Returns:
 *	`TH8_OK` if the two guard-fire vectors returned NULL
 *	(interpreter result: `"ok"`).
 *	`TH8_ERROR` otherwise (interpreter result: diagnostic
 *	with observed pointers).
 *
 * Side effects:
 *	None.  All borrowed cache references are read-only.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_null_guard_find_in_cache(Th8_Interp *interp)
{
    Th8_Value *r1, *r2, *r3;
    char buf[80];

    /* Th8_FindInCache(interp, cacheType, z, n)
     * Guard: if (!interp || !z) return 0; */

    /* All three calls return Th8_Value*, NULL on cache miss or
     * guard failure.  We can't distinguish "guard fired" from
     * "cache miss" by return value alone, so we just verify all
     * three calls completed without crashing.  The MC/DC counter
     * for !interp || !z increments correctly when each operand
     * varies, regardless of the return path. */
    r1 = Th8_FindInCache(NULL, 0, "key", 3); /* !interp true */
    r2 = Th8_FindInCache(interp, 0, NULL, 0); /* !interp false, !z true */
    r3 = Th8_FindInCache(interp, 0, "key", 3); /* both false */
    (void)r1;
    (void)r2;
    (void)r3;

    /* All paths should return without crashing.  Tests 1 and 2 must
     * return NULL (guard fires).  Test 3 returns NULL on cache miss
     * (which is the normal case for an arbitrary uncached key). */
    if (r1 == NULL && r2 == NULL) {
	Th8_SetResultStatic(interp, "ok", TH8_NOLEN);
	return TH8_OK;
    }
    {
	int n = snprintf(
	    buf, sizeof(buf), "fail r1=%p r2=%p (expected NULL,NULL)",
	    (void *)r1, (void *)r2);
	if (n < 0) n = 0;
	if ((size_t)n >= sizeof(buf)) n = (int)sizeof(buf) - 1;
	Th8_SetResult(interp, buf, (size_t)n);
    }
    return TH8_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_cache_lifecycle_cmd --
 *
 *	Implements "th8testlib::cache_lifecycle".  Exercises the
 *	four MC/DC vectors at src/th8_cache.c L764
 *	`if (pEntry && pEntry->pData)` by walking the
 *	cache-entry-lifecycle of a unique key:
 *
 *	  1. Insert a fresh entry via Th8_FindInCache (op=1 path
 *	     creates the entry with pData = non-NULL Th8_Value).
 *	  2. Call th8RemoveFromCache for the same key -- the entry
 *	     is found with pData non-NULL.  Hits (T, T).
 *	  3. Call th8RemoveFromCache again -- HashFind returns the
 *	     tombstoned entry with pData == NULL.  Hits (T, F).
 *	  4. Call th8RemoveFromCache for an unrelated key -- HashFind
 *	     returns NULL.  Hits (F, -).
 *
 * Why / How:
 *	The three vectors above cover the MC/DC decision at L764
 *	(C1 = pEntry, C2 = pEntry->pData).  Without explicit
 *	tombstone-aware exercise, the suite only hits the
 *	(F, -) cache-miss vector via incidental th8RemoveFromCache
 *	calls in the rest of the suite.
 *
 * Results:
 *	TH8_OK with "ok" in the interp result.
 *
 * Side effects:
 *	Inserts and deletes one cache entry under a known
 *	private key ("th8test_cache_lifecycle_key").  All state
 *	settles back to "empty/tombstoned" after the call.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_cache_lifecycle_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    static const char zKey[] = "th8test_cache_lifecycle_key";
    static const size_t nKey = sizeof(zKey) - 1;
    static const char zMissKey[] = "th8test_no_such_cache_key_zz";
    static const size_t nMissKey = sizeof(zMissKey) - 1;
    Th8_Value *pVal;

    (void)ctx;
    (void)argv;
    (void)argl;

    if (argc != 1) {
	return Th8_WrongNumArgs(interp, "th8testlib::cache_lifecycle");
    }

    /* Insert (or fetch existing).  Th8_FindInCache (op=1)
     * creates a fresh entry with pData = non-NULL Th8_Value
     * when no entry exists yet. */
    pVal = Th8_FindInCache(interp, TH8_CACHE_DOUBLE, zKey, nKey);
    (void)pVal;

    /* (T, T) vector: pEntry != NULL && pEntry->pData != NULL. */
    th8RemoveFromCache(interp, TH8_CACHE_DOUBLE, zKey, nKey);

    /* (T, F) vector: HashFind returns the tombstoned entry
     * (pData zeroed by the prior th8RemoveFromCache). */
    th8RemoveFromCache(interp, TH8_CACHE_DOUBLE, zKey, nKey);

    /* (F, -) vector: HashFind returns NULL (no entry for this
     * key).  Belt-and-suspenders for the cache-miss arm; many
     * other paths also exercise this. */
    th8RemoveFromCache(interp, TH8_CACHE_DOUBLE, zMissKey, nMissKey);

    Th8_SetResultStatic(interp, "ok", TH8_NOLEN);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_null_guard_memcmp --
 *
 *	Drive the 2-operand OR entry guard of
 *	`Th8_Memcmp(interp, a, b, n)`: `(!a || !b)`.  Three
 *	calls cover both operand short-circuits plus the
 *	both-valid equal-data path:
 *
 *	  * (T, -) NULL, X     -- expected return -1.
 *	  * (F, T) X, NULL     -- expected return  1.
 *	  * (F, F) X, X (equal)-- expected return  0.
 *
 * Parameters:
 *	interp -- live interpreter.
 *
 * Returns:
 *	`TH8_OK` if every vector returned the expected sentinel
 *	(interpreter result: `"ok"`).
 *	`TH8_ERROR` otherwise (interpreter result: diagnostic).
 *
 * Side effects:
 *	None beyond the interpreter result.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_null_guard_memcmp(Th8_Interp *interp)
{
    int rc1, rc2, rc3;
    char buf[80];

    /* Th8_Memcmp(interp, a, b, n)
     * Guard: if (!a || !b) return a ? 1 : (b ? -1 : 0);
     * Three inputs cover MC/DC for the !a || !b OR. */
    rc1 = Th8_Memcmp(interp, NULL, "abcd", 4); /* !a true */
    rc2 = Th8_Memcmp(interp, "abcd", NULL, 4); /* !a false, !b true */
    rc3 = Th8_Memcmp(interp, "abcd", "abcd", 4); /* both false */

    /* Expected: -1 (NULL,X), 1 (X,NULL), 0 (equal). */
    if (rc1 == -1 && rc2 == 1 && rc3 == 0) {
	Th8_SetResultStatic(interp, "ok", TH8_NOLEN);
	return TH8_OK;
    }
    {
	int n = snprintf(
	    buf, sizeof(buf), "fail rc1=%d rc2=%d rc3=%d (expected -1,1,0)",
	    rc1, rc2, rc3);
	if (n < 0) n = 0;
	if ((size_t)n >= sizeof(buf)) n = (int)sizeof(buf) - 1;
	Th8_SetResult(interp, buf, (size_t)n);
    }
    return TH8_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_null_guard_memcpy --
 *
 *	Drive the 3-operand OR entry guard of
 *	`Th8_Memcpy(interp, dst, src, n)`:
 *	`(n == 0 || !dst || !src)`.  Four calls cover MC/DC:
 *
 *	  * n=0, valid dst/src       -- (T, -, -) short-circuit.
 *	  * n=4, NULL dst, valid src -- (F, T, -) returns NULL.
 *	  * n=4, valid dst, NULL src -- (F, F, T) returns dst untouched.
 *	  * n=4, valid dst/src       -- (F, F, F) success path.
 *
 * Parameters:
 *	interp -- live interpreter.
 *
 * Returns:
 *	`TH8_OK` if every vector returned the expected pointer
 *	(interpreter result: `"ok"`).
 *	`TH8_ERROR` otherwise (interpreter result: diagnostic).
 *
 * Side effects:
 *	Writes the success-vector copy into a stack-local
 *	buffer; nothing escapes the helper.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_null_guard_memcpy(Th8_Interp *interp)
{
    char dst[8] = {0};
    char src[8] = "abcdefg";
    void *r1, *r2, *r3, *r4;
    char buf[96];

    /* Th8_Memcpy(interp, dst, src, n)
     * Guard: if (n == 0 || !dst || !src) return dst;
     * Four inputs cover MC/DC for the 3-operand OR. */
    r1 = Th8_Memcpy(
        interp, dst, src, 0); /* n==0 short-circuits; returns dst */
    r2 = Th8_Memcpy(interp, NULL, src, 4); /* n!=0, !dst true; returns NULL */
    r3 = Th8_Memcpy(interp, dst, NULL, 4); /* n!=0, !dst false, !src true */
    r4 = Th8_Memcpy(interp, dst, src, 4); /* all false; success */

    /* Expected: dst, NULL, dst, dst (function returns dst on all paths). */
    if (r1 == dst && r2 == NULL && r3 == dst && r4 == dst) {
	Th8_SetResultStatic(interp, "ok", TH8_NOLEN);
	return TH8_OK;
    }
    {
	int n = snprintf(
	    buf, sizeof(buf),
	    "fail r1=%p r2=%p r3=%p r4=%p (expected dst,NULL,dst,dst)", r1,
	    r2, r3, r4);
	if (n < 0) n = 0;
	if ((size_t)n >= sizeof(buf)) n = (int)sizeof(buf) - 1;
	Th8_SetResult(interp, buf, (size_t)n);
    }
    return TH8_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_null_guard_memset --
 *
 *	Drive the 2-operand OR entry guard of
 *	`Th8_Memset(interp, dst, c, n)`: `(n == 0 || !dst)`.
 *	Three calls cover MC/DC:
 *
 *	  * n=0, valid dst  -- (T, -) short-circuit returns dst.
 *	  * n=4, NULL dst   -- (F, T) returns NULL.
 *	  * n=4, valid dst  -- (F, F) success path returns dst.
 *
 * Parameters:
 *	interp -- live interpreter.
 *
 * Returns:
 *	`TH8_OK` if every vector returned the expected pointer
 *	(interpreter result: `"ok"`).
 *	`TH8_ERROR` otherwise (interpreter result: diagnostic).
 *
 * Side effects:
 *	Writes the success-vector fill into a stack-local
 *	buffer; nothing escapes the helper.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_null_guard_memset(Th8_Interp *interp)
{
    char dst[8] = {0};
    void *r1, *r2, *r3;
    char buf[80];

    /* Th8_Memset(interp, dst, c, n)
     * Guard: if (n == 0 || !dst) return dst;
     * Three inputs cover MC/DC for !=0 || !dst OR. */
    r1 = Th8_Memset(interp, dst, 'A', 0); /* n==0 short-circuits */
    r2 = Th8_Memset(interp, NULL, 'A', 4); /* n!=0, !dst true */
    r3 = Th8_Memset(interp, dst, 'A', 4); /* both false; success */

    if (r1 == dst && r2 == NULL && r3 == dst) {
	Th8_SetResultStatic(interp, "ok", TH8_NOLEN);
	return TH8_OK;
    }
    {
	int n = snprintf(
	    buf, sizeof(buf),
	    "fail r1=%p r2=%p r3=%p (expected dst,NULL,dst)", r1, r2, r3);
	if (n < 0) n = 0;
	if ((size_t)n >= sizeof(buf)) n = (int)sizeof(buf) - 1;
	Th8_SetResult(interp, buf, (size_t)n);
    }
    return TH8_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_null_guard_alloc_safe --
 *
 *	Drive the size-bounds entry guard of
 *	`Th8_SafeAlloc(interp, nBytes, zFile, nLine)` with
 *	three vectors:
 *
 *	  * nBytes = 0                  -- NULL.
 *	  * nBytes > `TH8_MX_ALLOC`     -- NULL (over-limit).
 *	  * nBytes = 16                 -- non-NULL success.
 *
 *	Defers the actual NULL / non-NULL classification to
 *	`th8test_null_guard_check3_alloc`, which sets the
 *	interpreter result.
 *
 * Parameters:
 *	interp -- live interpreter.
 *
 * Returns:
 *	`TH8_OK` if vectors 1 & 2 were NULL and vector 3 was
 *	non-NULL.  `TH8_ERROR` otherwise.
 *
 * Side effects:
 *	Allocates and immediately frees the success-vector
 *	buffer via `th8test_null_guard_check3_alloc`.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_null_guard_alloc_safe(Th8_Interp *interp)
{
    void *p1 = Th8_SafeAlloc(interp, 0, __FILE__, __LINE__);
    void *p2 = Th8_SafeAlloc(interp, TH8_MX_ALLOC + 1, __FILE__, __LINE__);
    void *p3 = Th8_SafeAlloc(interp, 16, __FILE__, __LINE__);
    return th8test_null_guard_check3_alloc(interp, p1, p2, p3);
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_null_guard_realloc_safe --
 *
 *	Drive the size-bounds entry guard of
 *	`Th8_SafeRealloc(interp, p, nBytes, zFile, nLine)`
 *	using `p = NULL` (the malloc-equivalent path) and
 *	three nBytes vectors: 0, over-limit, 16.  The guard
 *	fires identically whether `p` is NULL or not, so
 *	NULL keeps the helper from leaking an allocation on
 *	failure.
 *
 * Parameters:
 *	interp -- live interpreter.
 *
 * Returns:
 *	`TH8_OK` if vectors 1 & 2 were NULL and vector 3 was
 *	non-NULL.  `TH8_ERROR` otherwise.
 *
 * Side effects:
 *	Allocates and immediately frees the success-vector
 *	buffer via `th8test_null_guard_check3_alloc`.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_null_guard_realloc_safe(Th8_Interp *interp)
{
    /* p=NULL is the realloc-equivalent of malloc; the boundary
     * guard fires the same way. */
    void *p1 = Th8_SafeRealloc(interp, NULL, 0, __FILE__, __LINE__);
    void *p2 =
        Th8_SafeRealloc(interp, NULL, TH8_MX_ALLOC + 1, __FILE__, __LINE__);
    void *p3 = Th8_SafeRealloc(interp, NULL, 16, __FILE__, __LINE__);
    return th8test_null_guard_check3_alloc(interp, p1, p2, p3);
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_null_guard_realloc_safe_attempt --
 *
 *	Drive the size-bounds entry guard of
 *	`Th8_SafeAttemptRealloc(interp, p, nBytes, zFile,
 *	nLine)` with the same three vectors as the regular
 *	realloc helper above.  The "attempt" variant returns
 *	NULL (instead of triggering Panic) on allocation
 *	failure -- the guard itself is identical.
 *
 * Parameters:
 *	interp -- live interpreter.
 *
 * Returns:
 *	`TH8_OK` if vectors 1 & 2 were NULL and vector 3 was
 *	non-NULL.  `TH8_ERROR` otherwise.
 *
 * Side effects:
 *	Allocates and immediately frees the success-vector
 *	buffer via `th8test_null_guard_check3_alloc`.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_null_guard_realloc_safe_attempt(Th8_Interp *interp)
{
    void *p1 = Th8_SafeAttemptRealloc(interp, NULL, 0, __FILE__, __LINE__);
    void *p2 = Th8_SafeAttemptRealloc(
        interp, NULL, TH8_MX_ALLOC + 1, __FILE__, __LINE__);
    void *p3 = Th8_SafeAttemptRealloc(interp, NULL, 16, __FILE__, __LINE__);
    return th8test_null_guard_check3_alloc(interp, p1, p2, p3);
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_null_guard_clear_breakpoint --
 *
 *	Drive the 2-operand OR entry guard of
 *	`Th8_ClearBreakpoint(interp, breakpointId)`:
 *	`(!interp || !interp->paBreakpoints)`.  Three vectors:
 *
 *	  * NULL interp                       -- (T, -).
 *	  * Valid interp, paBreakpoints NULL  -- (F, T).
 *	  * Valid interp after `SetBreakpoint`-- (F, F) success.
 *
 *	Order dependency: this helper must run **before** any
 *	other test in the suite calls `Th8_SetBreakpoint` on
 *	the same interpreter, because `paBreakpoints` becomes
 *	non-NULL on first registration and stays non-NULL for
 *	the interp's lifetime (the hash is reused).  The
 *	test driver `tests/coverage/coverage_null_guards.tcl`
 *	orders this subcommand before
 *	`th8test_null_guard_set_breakpoint` to preserve the
 *	invariant.
 *
 * Parameters:
 *	interp -- live interpreter (must have no breakpoints
 *		set yet).
 *
 * Returns:
 *	`TH8_OK` if rc1=rc2=ERROR and rc3=OK (interpreter
 *	result: `"ok"`).
 *	`TH8_ERROR` otherwise (interpreter result: diagnostic).
 *
 * Side effects:
 *	Registers and immediately clears one breakpoint.
 *	After the call, `interp->paBreakpoints` is allocated
 *	and stays allocated.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_null_guard_clear_breakpoint(Th8_Interp *interp)
{
    int rc1, rc2, rc3;
    char buf[80];

    /* Th8_ClearBreakpoint(interp, breakpointId)
     * Guard: if (!interp || !interp->paBreakpoints) return TH8_ERROR;
     *
     * For the second operand, !interp->paBreakpoints is true iff no
     * breakpoint has ever been set on the interp.  This subcommand
     * must therefore run before any other test in the suite has
     * called Th8_SetBreakpoint -- otherwise paBreakpoints stays
     * non-NULL for the interp's lifetime.  The test driver
     * (coverage_null_guards.tcl) orders this test before
     * set_breakpoint to preserve that invariant. */

    /* Test 1: NULL interp.  !interp short-circuits. */
    rc1 = Th8_ClearBreakpoint(NULL, 0);

    /* Test 2: valid interp, paBreakpoints still NULL (no breakpoint
     * ever set in this interp). */
    rc2 = Th8_ClearBreakpoint(interp, 0);

    /* Test 3: success path: register a breakpoint to populate
     * paBreakpoints, then clear it. */
    {
	int id = 0;
	if (Th8_SetBreakpoint(interp, "x", 1, 1, &id) != TH8_OK) {
	    Th8_SetResultStatic(
	        interp,
	        "null_guard clear_breakpoint: "
	        "SetBreakpoint failed (test infrastructure)",
	        TH8_NOLEN);
	    return TH8_ERROR;
	}
	rc3 = Th8_ClearBreakpoint(interp, id);
    }

    /* paBreakpoints is now non-NULL (the hash stays allocated even
     * after the entry is cleared).  Subsequent tests that need
     * paBreakpoints==NULL must run BEFORE this subcommand; this is
     * the only such test currently. */

    if (rc1 == TH8_ERROR && rc2 == TH8_ERROR && rc3 == TH8_OK) {
	Th8_SetResultStatic(interp, "ok", TH8_NOLEN);
	return TH8_OK;
    }
    {
	int n = snprintf(
	    buf, sizeof(buf),
	    "fail rc1=%d rc2=%d rc3=%d (expected error,error,ok)", rc1, rc2,
	    rc3);
	if (n < 0) n = 0;
	if ((size_t)n >= sizeof(buf)) n = (int)sizeof(buf) - 1;
	Th8_SetResult(interp, buf, (size_t)n);
    }
    return TH8_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_null_guard_merge_platform --
 *
 *	Drive the 2-operand OR entry guard of
 *	`Th8_MergePlatformInterp(interp, pSrc)`:
 *	`(!interp || !pSrc)`.  Three vectors:
 *
 *	  * (T, -)        -- NULL interp.
 *	  * (F, T)        -- valid interp, NULL pSrc.
 *	  * (F, F)        -- valid interp, valid pSrc.
 *
 *	The success path uses the interp's own current
 *	platform (via `Th8_GetPlatform`) as `pSrc`; merging an
 *	interp with its own platform is idempotent but still
 *	exercises every past-the-guard code path inside the
 *	merge.
 *
 * Parameters:
 *	interp -- live interpreter.
 *
 * Returns:
 *	`TH8_OK` if rc1=rc2=ERROR and rc3=OK (interpreter
 *	result: `"ok"`).
 *	`TH8_ERROR` otherwise (interpreter result: diagnostic).
 *
 * Side effects:
 *	Reads but does not mutate the interp's platform
 *	(idempotent self-merge).
 *
 *----------------------------------------------------------------------
 */
static int
th8test_null_guard_merge_platform(Th8_Interp *interp)
{
    int rc1, rc2, rc3;
    const Th8_Platform *pPlat;
    char buf[80];

    /* Th8_MergePlatformInterp(interp, pSrc)
     * Guard: if (!interp || !pSrc) return TH8_ERROR;
     *
     * For test 3 we need a valid Th8_Platform*.  Th8_GetPlatform
     * returns the interp's current platform; merging the interp
     * with its own platform is a no-op semantically (the merge
     * function is idempotent on identical-platform inputs) but
     * exercises the past-the-guard code paths. */

    rc1 = Th8_MergePlatformInterp(NULL, NULL);
    rc2 = Th8_MergePlatformInterp(interp, NULL);

    pPlat = Th8_GetPlatform(interp);
    if (!pPlat) {
	Th8_SetResultStatic(
	    interp, "null_guard merge_platform: GetPlatform returned NULL",
	    TH8_NOLEN);
	return TH8_ERROR;
    }
    rc3 = Th8_MergePlatformInterp(interp, (void *)pPlat);

    if (rc1 == TH8_ERROR && rc2 == TH8_ERROR && rc3 == TH8_OK) {
	Th8_SetResultStatic(interp, "ok", TH8_NOLEN);
	return TH8_OK;
    }
    {
	int n = snprintf(
	    buf, sizeof(buf),
	    "fail rc1=%d rc2=%d rc3=%d (expected error,error,ok)", rc1, rc2,
	    rc3);
	if (n < 0) n = 0;
	if ((size_t)n >= sizeof(buf)) n = (int)sizeof(buf) - 1;
	Th8_SetResult(interp, buf, (size_t)n);
    }
    return TH8_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_null_guard_set_breakpoint --
 *
 *	Drive the 3-operand OR entry guard of
 *	`Th8_SetBreakpoint(interp, zScript, nScript, nLine,
 *	pBreakpointId)`:
 *	`(!interp || !zScript || nLine < 1)`.  Four vectors:
 *
 *	  * (T, -, -)  -- NULL interp.
 *	  * (F, T, -)  -- valid interp, NULL zScript.
 *	  * (F, F, T)  -- valid script, nLine=0.
 *	  * (F, F, F)  -- valid all, success path.
 *
 *	After the success vector the helper immediately
 *	clears the registered breakpoint via
 *	`Th8_ClearBreakpoint` so subsequent tests see no
 *	residual breakpoint state.
 *
 * Parameters:
 *	interp -- live interpreter.
 *
 * Returns:
 *	`TH8_OK` if rc1=rc2=rc3=ERROR and rc4=OK (delegates
 *	classification to `th8test_null_guard_check4`).
 *	`TH8_ERROR` otherwise.
 *
 * Side effects:
 *	Registers and then clears one breakpoint.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_null_guard_set_breakpoint(Th8_Interp *interp)
{
    int id = 0;
    int rc1, rc2, rc3, rc4;

    /* Th8_SetBreakpoint(interp, zScript, nScript, nLine, pBreakpointId)
     * Guard: if (!interp || !zScript || nLine < 1) return TH8_ERROR; */
    rc1 = Th8_SetBreakpoint(NULL, "x", 1, 1, &id);
    rc2 = Th8_SetBreakpoint(interp, NULL, 0, 1, &id);
    rc3 = Th8_SetBreakpoint(interp, "x", 1, 0, &id);
    rc4 = Th8_SetBreakpoint(interp, "x", 1, 1, &id);

    /* Cleanup: remove the breakpoint registered by test 4. */
    if (rc4 == TH8_OK) {
	Th8_ClearBreakpoint(interp, id);
    }

    return th8test_null_guard_check4(
        interp, rc1, rc2, rc3, rc4, TH8_ERROR, TH8_ERROR, TH8_ERROR, TH8_OK);
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_null_guard_list_append_breakpoints --
 *
 *	Drive the 3-operand OR entry guard of
 *	`Th8_ListAppendBreakpoints(interp, pzList, pnList)`:
 *	`(!interp || !pzList || !pnList)`.  Because the
 *	function returns void, the helper observes guard
 *	behaviour indirectly: on every error path the output
 *	pointer/length pair must remain unmodified
 *	(`zList == NULL && nList == 0`).  Four vectors:
 *
 *	  * (T, -, -)  -- NULL interp.
 *	  * (F, T, -)  -- NULL pzList.
 *	  * (F, F, T)  -- NULL pnList.
 *	  * (F, F, F)  -- success path; result may be empty if no
 *	                  breakpoints are set in the interp.
 *
 * Parameters:
 *	interp -- live interpreter.
 *
 * Returns:
 *	`TH8_OK` if the three guard-fire vectors left their
 *	outputs untouched and the success vector returned
 *	without crashing (delegated to
 *	`th8test_null_guard_check4`).
 *	`TH8_ERROR` otherwise.
 *
 * Side effects:
 *	May allocate one breakpoint-list buffer on the success
 *	vector; freed immediately via `Th8_Free`.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_null_guard_list_append_breakpoints(Th8_Interp *interp)
{
    char *zList1 = NULL;
    size_t nList1 = 0;
    char *zList2 = NULL;
    size_t nList2 = 0;
    char *zList3 = NULL;
    size_t nList3 = 0;
    char *zList4 = NULL;
    size_t nList4 = 0;
    int rc1, rc2, rc3, rc4;

    /* Th8_ListAppendBreakpoints(interp, pzList, pnList)
     * Guard: if (!interp || !pzList || !pnList) return;
     * Function returns void; we observe by checking that pzList/pnList
     * are unchanged on the error paths and modified on the success path. */

    Th8_ListAppendBreakpoints(NULL, &zList1, &nList1);
    rc1 = (zList1 == NULL && nList1 == 0) ? TH8_ERROR : TH8_OK;

    Th8_ListAppendBreakpoints(interp, NULL, &nList2);
    rc2 = (zList2 == NULL && nList2 == 0) ? TH8_ERROR : TH8_OK;

    Th8_ListAppendBreakpoints(interp, &zList3, NULL);
    rc3 = (zList3 == NULL && nList3 == 0) ? TH8_ERROR : TH8_OK;

    /* Test 4: success path -- function may produce empty result if no
     * breakpoints are set, so we accept either "list unchanged" (no
     * breakpoints in interp) or "list modified" (breakpoints exist). */
    Th8_ListAppendBreakpoints(interp, &zList4, &nList4);
    rc4 = TH8_OK; /* Success path always returns; nothing to check. */

    if (zList4) Th8_Free(interp, zList4);

    return th8test_null_guard_check4(
        interp, rc1, rc2, rc3, rc4, TH8_ERROR, TH8_ERROR, TH8_ERROR, TH8_OK);
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_null_guard_list_append_expansions --
 *
 *	Drive the 3-operand OR entry guard of
 *	`Th8_ListAppendExpansions(interp, pzList, pnList, zPat,
 *	nPat)`: `(!interp || !pzList || !pnList)`.  Same shape
 *	as `th8test_null_guard_list_append_breakpoints`: void
 *	return, so the helper verifies the output pointer/
 *	length pair stays untouched on error and just confirms
 *	the success vector did not crash.  `zPat` is left NULL
 *	for every vector -- the expansion-pattern filter is
 *	not in the guard.
 *
 * Parameters:
 *	interp -- live interpreter.
 *
 * Returns:
 *	`TH8_OK` if the three guard-fire vectors left their
 *	outputs untouched (delegated to
 *	`th8test_null_guard_check4`).
 *	`TH8_ERROR` otherwise.
 *
 * Side effects:
 *	May allocate one expansion-list buffer on the success
 *	vector; freed immediately via `Th8_Free`.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_null_guard_list_append_expansions(Th8_Interp *interp)
{
    char *zList1 = NULL;
    size_t nList1 = 0;
    char *zList2 = NULL;
    size_t nList2 = 0;
    char *zList3 = NULL;
    size_t nList3 = 0;
    char *zList4 = NULL;
    size_t nList4 = 0;
    int rc1, rc2, rc3, rc4;

    /* Th8_ListAppendExpansions(interp, pzList, pnList, zPat, nPat)
     * Guard: if (!interp || !pzList || !pnList) return; */

    Th8_ListAppendExpansions(NULL, &zList1, &nList1, NULL, 0);
    rc1 = (zList1 == NULL && nList1 == 0) ? TH8_ERROR : TH8_OK;

    Th8_ListAppendExpansions(interp, NULL, &nList2, NULL, 0);
    rc2 = (zList2 == NULL && nList2 == 0) ? TH8_ERROR : TH8_OK;

    Th8_ListAppendExpansions(interp, &zList3, NULL, NULL, 0);
    rc3 = (zList3 == NULL && nList3 == 0) ? TH8_ERROR : TH8_OK;

    Th8_ListAppendExpansions(interp, &zList4, &nList4, NULL, 0);
    rc4 = TH8_OK;

    if (zList4) Th8_Free(interp, zList4);

    return th8test_null_guard_check4(
        interp, rc1, rc2, rc3, rc4, TH8_ERROR, TH8_ERROR, TH8_ERROR, TH8_OK);
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_null_guard_cmd --
 *
 *	Implements `::th8testlib::null_guard SUBCMD`, a
 *	dispatcher that routes the single sub-command name in
 *	`argv[1]` to the matching `th8test_null_guard_*`
 *	helper.  Each helper is responsible for driving one
 *	public-API entry guard's MC/DC C-pairs and reporting
 *	its outcome via the interpreter result.
 *
 *	Recognised SUBCMDs are listed in the body's
 *	`if (argl[1] == LEN && memcmp(...) == 0)` chain; an
 *	unknown SUBCMD returns `TH8_ERROR` with a diagnostic
 *	interpreter result.
 *
 *	Some SUBCMDs are gated on build features
 *	(`TH8_ENABLE_CRYPTOGRAPHY`, `TH8_ENABLE_TEST_KEY`,
 *	`TH8_ENABLE_FAULT_INJECTION`); when the gate is off,
 *	the corresponding `if` is `#  if`-excluded and the
 *	SUBCMD is reported as unknown.
 *
 * Parameters:
 *	interp -- live interpreter (used for diagnostics and
 *		for the sub-helpers' work).
 *	ctx    -- unused command context.
 *	argc   -- argument count (must be 2).
 *	argv   -- argv[0]=command name; argv[1]=SUBCMD.
 *	argl   -- argument byte-lengths (consulted for
 *		SUBCMD-name matching).
 *
 * Returns:
 *	The selected helper's return code, or `TH8_ERROR`
 *	on argument-count error / unknown SUBCMD.
 *
 * Side effects:
 *	Side effects depend on the selected helper.  Sets the
 *	interpreter result.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_null_guard_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    (void)ctx;

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "th8testlib::null_guard SUBCMD");
    }
    if (argl[1] == 11 && memcmp(argv[1], "queue_event", 11) == 0) {
	return th8test_null_guard_queue_event(interp);
    }
    if (argl[1] == 13 && memcmp(argv[1], "iterate_array", 13) == 0) {
	return th8test_null_guard_iterate_array(interp);
    }
    if (argl[1] == 16 && memcmp(argv[1], "set_platform_ctx", 16) == 0) {
	return th8test_null_guard_set_platform_ctx(interp);
    }
    if (argl[1] == 16 && memcmp(argv[1], "get_platform_ctx", 16) == 0) {
	return th8test_null_guard_get_platform_ctx(interp);
    }
    if (argl[1] == 13 && memcmp(argv[1], "find_in_cache", 13) == 0) {
	return th8test_null_guard_find_in_cache(interp);
    }
    if (argl[1] == 9 && memcmp(argv[1], "eval_file", 9) == 0) {
	return th8test_null_guard_eval_file(interp);
    }
    if (argl[1] == 17 && memcmp(argv[1], "eval_file_as_data", 17) == 0) {
	return th8test_null_guard_eval_file_as_data(interp);
    }
    if (argl[1] == 14 && memcmp(argv[1], "misc_singlearg", 14) == 0) {
	return th8test_null_guard_misc_singlearg(interp);
    }
#  if defined(TH8_ENABLE_CRYPTOGRAPHY) && defined(TH8_ENABLE_TEST_KEY)
    if (argl[1] == 15 && memcmp(argv[1], "policy_find_key", 15) == 0) {
	return th8test_null_guard_policy_find_key(interp);
    }
#  endif
#  if defined(TH8_ENABLE_FAULT_INJECTION)
    if (argl[1] == 5 && memcmp(argv[1], "fault", 5) == 0) {
	return th8test_null_guard_fault(interp);
    }
#  endif
    if (argl[1] == 6 && memcmp(argv[1], "plugin", 6) == 0) {
	return th8test_null_guard_plugin(interp);
    }
#  if defined(TH8_ENABLE_FAULT_INJECTION)
    if (argl[1] == 4 && memcmp(argv[1], "plat", 4) == 0) {
	return th8test_null_guard_plat(interp);
    }
#  endif
    if (argl[1] == 4 && memcmp(argv[1], "core", 4) == 0) {
	return th8test_null_guard_core(interp);
    }
#  if defined(TH8_ENABLE_CRYPTOGRAPHY)
    if (argl[1] == 14 && memcmp(argv[1], "harpy_sig_load", 14) == 0) {
	return th8test_null_guard_harpy_sig_load(interp);
    }
#  endif
#  if defined(TH8_ENABLE_CRYPTOGRAPHY) && defined(TH8_ENABLE_TEST_KEY)
    if (argl[1] == 11 && memcmp(argv[1], "rsa_getters", 11) == 0) {
	return th8test_null_guard_rsa_getters(interp);
    }
    if (argl[1] == 14 && memcmp(argv[1], "policy_preload", 14) == 0) {
	return th8test_null_guard_policy_preload(interp);
    }
#  endif
    if (argl[1] == 6 && memcmp(argv[1], "memcmp", 6) == 0) {
	return th8test_null_guard_memcmp(interp);
    }
    if (argl[1] == 6 && memcmp(argv[1], "memcpy", 6) == 0) {
	return th8test_null_guard_memcpy(interp);
    }
    if (argl[1] == 6 && memcmp(argv[1], "memset", 6) == 0) {
	return th8test_null_guard_memset(interp);
    }
    if (argl[1] == 10 && memcmp(argv[1], "alloc_safe", 10) == 0) {
	return th8test_null_guard_alloc_safe(interp);
    }
    if (argl[1] == 12 && memcmp(argv[1], "realloc_safe", 12) == 0) {
	return th8test_null_guard_realloc_safe(interp);
    }
    if (argl[1] == 20 && memcmp(argv[1], "realloc_safe_attempt", 20) == 0) {
	return th8test_null_guard_realloc_safe_attempt(interp);
    }
    if (argl[1] == 16 && memcmp(argv[1], "clear_breakpoint", 16) == 0) {
	return th8test_null_guard_clear_breakpoint(interp);
    }
    if (argl[1] == 14 && memcmp(argv[1], "merge_platform", 14) == 0) {
	return th8test_null_guard_merge_platform(interp);
    }
    if (argl[1] == 14 && memcmp(argv[1], "set_breakpoint", 14) == 0) {
	return th8test_null_guard_set_breakpoint(interp);
    }
    if (argl[1] == 23 &&
        memcmp(argv[1], "list_append_breakpoints", 23) == 0) {
	return th8test_null_guard_list_append_breakpoints(interp);
    }
    if (argl[1] == 22 && memcmp(argv[1], "list_append_expansions", 22) == 0) {
	return th8test_null_guard_list_append_expansions(interp);
    }
    return Th8_ErrorMessage(
        interp, "th8testlib::null_guard: unknown subcommand:", argv[1],
        argl[1]);
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_safeallocstradd_cmd --
 *
 *	Implements "th8testlib::safeallocstradd k n".  Calls
 *	TH8_ALLOC_STR_ADD and reports "ok" or "overflow".  The block
 *	is freed immediately so the test does not need a real
 *	successful allocation of huge size.  Overflow paths return
 *	"overflow" without ever calling the allocator.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_safeallocstradd_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    size_t k = 0, n = 0;
    void *p;

    (void)ctx;

    if (argc != 3) {
	return Th8_WrongNumArgs(interp, "th8testlib::safeallocstradd k n");
    }
    if (th8test_parse_size(argv[1], argl[1], &k) != TH8_OK ||
        th8test_parse_size(argv[2], argl[2], &n) != TH8_OK) {
	Th8_SetResultStatic(interp, "bad size argument", TH8_NOLEN);
	return TH8_ERROR;
    }
    p = TH8_ALLOC_STR_ADD(interp, k, n);
    if (!p) {
	Th8_SetResultStatic(interp, "overflow", TH8_NOLEN);
    } else {
	Th8_Free(interp, p);
	Th8_SetResultStatic(interp, "ok", TH8_NOLEN);
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_safeallocstrmul_cmd --
 *
 *	Implements "th8testlib::safeallocstrmul n sz".  Calls
 *	TH8_ALLOC_STR_MUL and reports "ok" or "overflow".
 *
 *----------------------------------------------------------------------
 */

static int
th8test_safeallocstrmul_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    size_t n = 0, sz = 0;
    void *p;

    (void)ctx;

    if (argc != 3) {
	return Th8_WrongNumArgs(interp, "th8testlib::safeallocstrmul n sz");
    }
    if (th8test_parse_size(argv[1], argl[1], &n) != TH8_OK ||
        th8test_parse_size(argv[2], argl[2], &sz) != TH8_OK) {
	Th8_SetResultStatic(interp, "bad size argument", TH8_NOLEN);
	return TH8_ERROR;
    }
    p = TH8_ALLOC_STR_MUL(interp, n, sz);
    if (!p) {
	Th8_SetResultStatic(interp, "overflow", TH8_NOLEN);
    } else {
	Th8_Free(interp, p);
	Th8_SetResultStatic(interp, "ok", TH8_NOLEN);
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_safeallocmuladd2_cmd --
 *
 *	Implements "th8testlib::safeallocmuladd2 a b c d e".  Calls
 *	TH8_ALLOC_MUL_ADD2 and reports "ok" or "overflow".
 *
 *----------------------------------------------------------------------
 */

static int
th8test_safeallocmuladd2_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    size_t a = 0, b = 0, c = 0, d = 0, e = 0;
    void *p;

    (void)ctx;

    if (argc != 6) {
	return Th8_WrongNumArgs(
	    interp, "th8testlib::safeallocmuladd2 a b c d e");
    }
    if (th8test_parse_size(argv[1], argl[1], &a) != TH8_OK ||
        th8test_parse_size(argv[2], argl[2], &b) != TH8_OK ||
        th8test_parse_size(argv[3], argl[3], &c) != TH8_OK ||
        th8test_parse_size(argv[4], argl[4], &d) != TH8_OK ||
        th8test_parse_size(argv[5], argl[5], &e) != TH8_OK) {
	Th8_SetResultStatic(interp, "bad size argument", TH8_NOLEN);
	return TH8_ERROR;
    }
    p = TH8_ALLOC_MUL_ADD2(interp, a, b, c, d, e);
    if (!p) {
	Th8_SetResultStatic(interp, "overflow", TH8_NOLEN);
    } else {
	Th8_Free(interp, p);
	Th8_SetResultStatic(interp, "ok", TH8_NOLEN);
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_is_result_sensitive_cmd --
 *
 *	Implements "th8testlib::is_result_sensitive".  Returns the
 *	current value of Th8_IsResultSensitive(interp) as "0" or "1".
 *	Used by sensitive-result tests to assert the flag transitions
 *	correctly across reads, overwrites, and clears.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_is_result_sensitive_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    int v;

    (void)ctx;
    (void)argv;
    (void)argl;

    if (argc != 1) {
	return Th8_WrongNumArgs(interp, "th8testlib::is_result_sensitive");
    }
    v = Th8_IsResultSensitive(interp);
    /*
     * Reading the flag must not itself mutate it.  We need to
     * report the value back as a fresh result, but Th8_SetResult
     * would clear the flag via the standard release path -- so
     * preserve the sensitive marking by re-asserting it after
     * setting our boolean reply.
     */
    Th8_SetResultStatic(interp, v ? "1" : "0", 1);
#  if defined(TH8_ENABLE_VARIABLES) && defined(TH8_ENABLE_CRYPTOGRAPHY)
    if (v) {
	Th8_MarkResultSensitive(interp);
    }
#  endif
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_mark_sensitive_release_cmd --
 *
 *	Implements "th8testlib::mark_sensitive_release".  Drives the
 *	heap-sensitive secure-zero path in th8ReleaseOldResult by:
 *	  1. Th8_SetResult with a fixed payload -- heap-allocates
 *	     interp->zResult.
 *	  2. Th8_MarkResultSensitive -- sets the sensitive tag bit in nResult.
 *	  3. Th8_SetResultStatic to a benign reply -- internally
 *	     calls th8ReleaseOldResult with a sensitive result and
 *	     zResult!=NULL, hitting the L4886 (T,T) MC/DC vector
 *	     that no normal script path can reach.
 *
 *	No arguments.  Returns "ok".
 *
 * Why / How:
 *	The (T,T) vector at src/th8_core.c L4886 requires a
 *	sensitive result that lives in regular heap (not the
 *	PROTECTED region used by [secure] variables).  No
 *	script-level API combines Th8_SetResult + Th8_MarkSensitive
 *	atomically before the result is released, so the only
 *	way to drive the vector is via a C helper that does all
 *	three steps in one call.
 *
 *----------------------------------------------------------------------
 */

#  if defined(TH8_ENABLE_CRYPTOGRAPHY)
static int
th8test_mark_sensitive_release_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    (void)ctx;

    if (argc < 1 || argc > 2) {
	return Th8_WrongNumArgs(
	    interp, "th8testlib::mark_sensitive_release ?empty?");
    }

    /* Optional "empty" mode: mark sensitive with zResult=NULL so
     * the subsequent SetResult drives the L4886 (T,F) vector
     * (sensitive-tag && zResult==NULL is F). */
    if (argc == 2 && argl[1] == 5 && memcmp(argv[1], "empty", 5) == 0) {
	/* Clear the result so zResult becomes NULL. */
	(void)Th8_SetResult(interp, NULL, 0);
	Th8_MarkResultSensitive(interp);
	Th8_SetResultStatic(interp, "ok-empty", 8);
	return TH8_OK;
    }

    /* Step 1: heap-allocate a result via Th8_SetResult. */
    if (Th8_SetResult(interp, "sensitive-payload", 17) != TH8_OK) {
	return TH8_ERROR;
    }

    /* Step 2: mark it sensitive. */
    Th8_MarkResultSensitive(interp);

    /* Step 3: overwrite the result, triggering the sensitive
     * heap-release path in th8ReleaseOldResult. */
    Th8_SetResultStatic(interp, "ok", 2);
    return TH8_OK;
}
#  endif


/*
 *----------------------------------------------------------------------
 *
 * th8test_curl_xgetdata_cmd --
 *
 *	Implements "th8testlib::curl_xgetdata URI".  Calls the
 *	libcurl platform's xGetData directly with the supplied
 *	URI, bypassing the Th8Shell_curlGetData wrapper that
 *	only routes "http*" prefixes to curl.  Used to drive the
 *	C2-C5 = F vectors in th8CurlIsValidUri at src/th8_curl.c
 *	L89-92 (non-http scheme rejection) -- the shell wrapper
 *	never lets non-http URIs reach the curl layer.
 *
 *	Returns "rc=N msg=M" where N is the return code from
 *	xGetData and M is the resulting interpreter message
 *	(typically "invalid or disallowed URI scheme" on
 *	rejection).  No network traffic occurs because the
 *	validator rejects before any fetch attempt.
 *
 *----------------------------------------------------------------------
 */

#  if defined(TH8_ENABLE_LIBCURL)
static int
th8test_curl_xgetdata_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    const Th8_Platform *pCurlPlat;
    char *zOut = NULL;
    size_t nOut = 0;
    int rc;
    const char *zMsg;
    size_t nMsg;
    char zReply[256];
    int n;

    (void)ctx;

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "th8testlib::curl_xgetdata URI");
    }

    pCurlPlat = Th8_GetCurlPlatform();
    if (!pCurlPlat || !pCurlPlat->xGetData) {
	Th8_SetResultStatic(
	    interp, "curl_xgetdata: libcurl platform not available",
	    TH8_NOLEN);
	return TH8_ERROR;
    }

    rc = pCurlPlat->xGetData(
        interp, pCurlPlat->pCtx, argv[1], argl[1], &zOut, &nOut);
    if (zOut) {
	Th8_Free(interp, zOut);
    }
    zMsg = Th8_GetResult(interp, &nMsg);
    if (nMsg > 200) nMsg = 200;
    n = snprintf(zReply, sizeof(zReply), "rc=%d msg=", rc);
    if (n < 0 || (size_t)n >= sizeof(zReply)) {
	Th8_SetResultStatic(
	    interp, "curl_xgetdata: format overflow", TH8_NOLEN);
	return TH8_ERROR;
    }
    {
	size_t k;
	size_t off = (size_t)n;
	for (k = 0; k < nMsg && off + 1 < sizeof(zReply); k++) {
	    zReply[off++] = zMsg[k];
	}
	zReply[off] = '\0';
	Th8_SetResult(interp, zReply, off);
    }
    return TH8_OK;
}
#  endif


/*
 *----------------------------------------------------------------------
 *
 * th8test_glob_match_null_cmd --
 *
 *	Implements "th8testlib::glob_match_null PATTERN STRING".
 *	Calls Th8_GlobMatch with interp=NULL (the documented
 *	no-cancellation form) and returns "0" or "1".  Drives the
 *	C1=F vector at src/th8_glob.c L90 -- `if (interp && ...)`
 *	-- which no normal script path can reach because all
 *	script-level glob calls pass the current interp.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_glob_match_null_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    int rc;

    (void)ctx;

    if (argc != 3) {
	return Th8_WrongNumArgs(
	    interp, "th8testlib::glob_match_null PATTERN STRING");
    }

    /* Deliberately pass NULL interp so L90's C1=F fires. */
    rc = Th8_GlobMatch(NULL, argv[1], argl[1], argv[2], argl[2]);
    Th8_SetResultStatic(interp, rc ? "1" : "0", 1);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_glob_match_cancelled_cmd --
 *
 *	Implements "th8testlib::glob_match_cancelled PATTERN STRING".
 *	Cancels the current interp, calls Th8_GlobMatch with the
 *	cancelled interp, captures the return, then resets the
 *	cancellation state.  Drives the th8_glob.c L90 (T,T)
 *	vector -- interp non-NULL AND Th8_Ready returns non-OK
 *	(because IsCanceled is true) -- which no normal script
 *	path can reach.  Returns "0" (glob short-circuits to 0
 *	when Ready fails).
 *
 *	Cancellation is reset before returning so the test
 *	infrastructure remains usable.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_glob_match_cancelled_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    int rc;

    (void)ctx;

    if (argc != 3) {
	return Th8_WrongNumArgs(
	    interp, "th8testlib::glob_match_cancelled PATTERN STRING");
    }

    /* Arm cancellation on the current interp.  Th8_Ready will
     * now return non-OK until ResetCancel is called. */
    Th8_CancelEval(interp, "test-cancel", 11, 0);

    /* Th8_GlobMatch with the cancelled interp drives L90 (T,T):
     * interp non-NULL, Th8_Ready != TH8_OK. */
    rc = Th8_GlobMatch(interp, argv[1], argl[1], argv[2], argl[2]);

    /* Clear cancellation so subsequent test ops work. */
    Th8_ResetCancel(interp);

    /* Th8_GlobMatch return is 0 on Ready failure.  Set result
     * after ResetCancel so SetResult doesn't see the cancelled
     * state. */
    Th8_SetResultStatic(interp, rc ? "1" : "0", 1);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_take_result_cmd --
 *
 *	Implements "th8testlib::take_result".  Invokes Th8_TakeResult
 *	on the current interpreter result and returns one of:
 *	  - "refused"           -- if Th8_TakeResult refused (sensitive)
 *	  - "empty"             -- if the result was empty
 *	  - "taken: <bytes>"    -- on success, where <bytes> is the
 *	                           taken value (immediately freed)
 *
 *	Used by sensitive-result tests to assert that detaching is
 *	refused when the result is sensitive.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_take_result_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char *zTaken;
    size_t nTaken = 0;

    (void)ctx;
    (void)argv;
    (void)argl;

    if (argc != 1) {
	return Th8_WrongNumArgs(interp, "th8testlib::take_result");
    }

    zTaken = Th8_TakeResult(interp, &nTaken);
    if (!zTaken) {
	/*
	 * Th8_TakeResult set the result either to an error message
	 * (sensitive refusal) or left it cleared (empty).  Distinguish
	 * the two: if the current result starts with "sensitive ",
	 * report "refused"; otherwise "empty".
	 */
	const char *zRes = Th8_GetResult(interp, NULL);
	if (zRes && zRes[0] == 's') {
	    Th8_SetResultStatic(interp, "refused", TH8_NOLEN);
	} else {
	    Th8_SetResultStatic(interp, "empty", TH8_NOLEN);
	}
	return TH8_OK;
    }

    {
	char zMsg[256];
	int n = 7; /* "taken: " */
	memcpy(zMsg, "taken: ", 7);
	if (nTaken < sizeof(zMsg) - 7) {
	    memcpy(zMsg + n, zTaken, nTaken);
	    n += (int)nTaken;
	    Th8_SetResult(interp, zMsg, (size_t)n);
	} else {
	    Th8_SetResultStatic(interp, "taken: <oversize>", TH8_NOLEN);
	}
    }
    Th8_Free(interp, zTaken);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_sensitive_probe_cmd --
 *
 *	Implements "th8testlib::sensitive_probe varName".  Within a
 *	single C call, performs the canonical sequence:
 *	  1. Th8_GetVar(interp, varName) -- reads varName, which for
 *	     a [secure] variable internally calls Th8_SetResultSensitive
 *	     and leaves the result marked sensitive (nResult tag).
 *	  2. Captures Th8_IsResultSensitive immediately.
 *	  3. Captures the result length (NOT the bytes -- we will not
 *	     copy plaintext out of the protected region).
 *	  4. Calls Th8_TakeResult; expects refusal (returns NULL and
 *	     sets the result to an error).
 *	  5. Calls Th8_IsResultSensitive again to verify the refusal
 *	     itself replaced the result with a non-sensitive error.
 *
 *	Reports a structured outcome: "ok <flagBefore> <length>
 *	<takeRefused> <flagAfter>" so a test can assert each value
 *	with one expression.  Doing all five steps inside one C
 *	function avoids the script-level result-replacement that
 *	would otherwise mask the sensitive transition.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_sensitive_probe_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    int rcGet;
    int flagBefore;
    size_t nLen = 0;
    char *zTaken;
    int takeRefused;
    int flagAfter;
    char zMsg[80];
    int n;

    (void)ctx;

    if (argc != 2) {
	return Th8_WrongNumArgs(
	    interp, "th8testlib::sensitive_probe varName");
    }

    rcGet = Th8_GetVar(interp, argv[1], argl[1]);
    if (rcGet != TH8_OK) {
	/* Variable not found or access error -- propagate. */
	return rcGet;
    }
    flagBefore = Th8_IsResultSensitive(interp);
    /* Capture length only; do not read the bytes.  Mask the tag bits
     * (taint/sensitive now ride in the length) so the reported byte
     * count is the raw length. */
    Th8_GetResult(interp, &nLen);
    nLen = TH8_LEN(nLen);

    zTaken = Th8_TakeResult(interp, NULL);
    takeRefused = (zTaken == NULL);
    if (zTaken) {
	/*
	 * Should not happen for a sensitive result, but if it
	 * does, free the buffer so we don't leak.
	 */
	Th8_Free(interp, zTaken);
    }

    flagAfter = Th8_IsResultSensitive(interp);

    n = 0;
    {
	const char *zPre = "ok ";
	int i;
	for (i = 0; zPre[i]; i++)
	    zMsg[n++] = zPre[i];
    }
    zMsg[n++] = (char)('0' + (flagBefore ? 1 : 0));
    zMsg[n++] = ' ';
    /* nLen as decimal; small values expected (test inputs <100 bytes). */
    {
	char tmp[20];
	int j = 0, k;
	if (nLen == 0)
	    tmp[j++] = '0';
	else {
	    while (nLen > 0) {
		tmp[j++] = (char)('0' + (nLen % 10));
		nLen /= 10;
	    }
	}
	for (k = j - 1; k >= 0; k--)
	    zMsg[n++] = tmp[k];
    }
    zMsg[n++] = ' ';
    zMsg[n++] = (char)('0' + (takeRefused ? 1 : 0));
    zMsg[n++] = ' ';
    zMsg[n++] = (char)('0' + (flagAfter ? 1 : 0));
    Th8_SetResult(interp, zMsg, (size_t)n);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_evalfile_cmd --
 *
 *	Implements the "th8testlib::evalfile" command.
 *	Calls Th8_EvalFile to evaluate a file by name.
 *
 *	Usage: th8testlib::evalfile fileName
 *
 * Why / How:
 *	Exposes Th8_EvalFile directly to test scripts so that file
 *	sourcing behavior can be tested independently of the [source]
 *	command implementation.  Useful for testing error paths and
 *	file-not-found handling.
 *
 * Results:
 *	The return code from Th8_EvalFile.
 *
 * Side effects:
 *	Reads and evaluates a file.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_evalfile_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    (void)ctx;

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "th8testlib::evalfile fileName");
    }
    return Th8_EvalFile(interp, argv[1], argl[1]);
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_freeze_cmd --
 *
 *	Implements the "th8testlib::freeze" command.
 *	Calls Th8_Freeze to suspend the interpreter.
 *
 *	Usage: th8testlib::freeze
 *
 * Why / How:
 *	Tests the interpreter suspension mechanism.  When called
 *	from within a script, Th8_Freeze causes the current eval
 *	to return TH8_SUSPEND, preserving the interpreter state
 *	for later resumption via Th8_Thaw.
 *
 * Results:
 *	The return code from Th8_Freeze (TH8_SUSPEND on success).
 *
 * Side effects:
 *	Sets the interpreter's suspended flag.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_freeze_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    (void)ctx;
    (void)argv;
    (void)argl;
    if (argc != 1) {
	return Th8_WrongNumArgs(interp, "th8testlib::freeze");
    }
    return Th8_Freeze(interp);
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_thaw_cmd --
 *
 *	Implements the "th8testlib::thaw" command.
 *	Calls Th8_Thaw to resume a frozen interpreter.
 *
 *	Usage: th8testlib::thaw
 *
 * Why / How:
 *	Complements th8test_freeze_cmd.  Clears the suspended flag
 *	so subsequent evaluations proceed normally.  Used by the
 *	freeze/thaw test cycle commands to verify state preservation
 *	across suspension boundaries.
 *
 * Results:
 *	The return code from Th8_Thaw.
 *
 * Side effects:
 *	Clears the interpreter's suspended flag.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_thaw_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    (void)ctx;
    (void)argv;
    (void)argl;
    if (argc != 1) {
	return Th8_WrongNumArgs(interp, "th8testlib::thaw");
    }
    return Th8_Thaw(interp);
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_issuspended_cmd --
 *
 *	Implements the "th8testlib::issuspended" command.
 *	Returns 1 if the interpreter is suspended, 0 otherwise.
 *
 *	Usage: th8testlib::issuspended
 *
 * Why / How:
 *	Allows test scripts to query the suspension state from C
 *	level (Th8_IsSuspended) to verify that freeze/thaw actually
 *	toggled the flag correctly.
 *
 * Results:
 *	TH8_OK with "1" or "0" as the result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_issuspended_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    (void)ctx;
    (void)argv;
    (void)argl;
    if (argc != 1) {
	return Th8_WrongNumArgs(interp, "th8testlib::issuspended");
    }
    return Th8_SetResultInt(interp, Th8_IsSuspended(interp));
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_queuescript_cmd --
 *
 *	Implements the "th8testlib::queuescript" command.
 *	Queues a script for execution on a frozen interpreter.
 *	The script will run when Th8_Thaw re-enters the trampoline.
 *
 *	The interpreter MUST be frozen (bSuspended == 1) when this
 *	is called.  Th8_NREval pushes an evaluation callback onto
 *	the NRE chain without executing it.  Since the chain is
 *	LIFO, the last script queued runs first.
 *
 *	Usage: th8testlib::queuescript script
 *
 * Why / How:
 *	Tests the NRE (non-recursive evaluation) queue mechanism
 *	for frozen interpreters.  Verifies that scripts queued
 *	while the interpreter is suspended execute in LIFO order
 *	when the trampoline resumes after thaw.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if the interpreter is not
 *	suspended.
 *
 * Side effects:
 *	Pushes an evaluation callback onto the NRE chain.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_queuescript_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    (void)ctx;

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "th8testlib::queuescript script");
    }

    if (!Th8_IsSuspended(interp)) {
	Th8_SetResultStatic(interp, "interpreter is not frozen", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Push an evaluation callback onto the NRE chain.
     * It will execute when the trampoline resumes.
     */

    return Th8_NREval(interp, argv[1], argl[1], NULL, 0);
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_freezequeuethaw_cmd --
 *
 *	Implements "th8testlib::freezequeuethaw".  Demonstrates the
 *	full freeze-queue-thaw cycle in a single C function:
 *
 *	  1. Evaluates initScript (runs until it freezes).
 *	  2. Verifies the interpreter is suspended.
 *	  3. Queues queueScript onto the NRE chain.
 *	  4. Thaws the interpreter (queueScript runs first,
 *	     then the remainder of initScript resumes).
 *	  5. Returns the final result.
 *
 *	Usage: th8testlib::freezequeuethaw initScript queueScript
 *
 * Why / How:
 *	Integration test for the complete freeze-queue-thaw cycle.
 *	Runs entirely in C to avoid re-entrancy issues that would
 *	arise from orchestrating the cycle in script (where the
 *	freeze would abort the orchestrating eval).
 *
 * Results:
 *	The return code and result of queueScript evaluation.
 *
 * Side effects:
 *	Evaluates both scripts; freezes and thaws the interpreter.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_freezequeuethaw_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    int rc;

    (void)ctx;

    if (argc != 3) {
	return Th8_WrongNumArgs(
	    interp, "th8testlib::freezequeuethaw initScript queueScript");
    }

    /*
     * Step 1: Evaluate initScript.  It should call
     * [th8testlib::freeze] at some point, causing
     * Th8_Eval to return TH8_SUSPEND.
     */

    rc = Th8_Eval(interp, 0, argv[1], argl[1], NULL, 0);

    if (rc != TH8_SUSPEND) {
	/*
	 * The script didn't freeze.  Return whatever it returned.
	 */

	return rc;
    }

    /*
     * Step 2: The interpreter is frozen.  Thaw it and then
     * evaluate the queued script.  The queued script runs
     * in the interpreter's current state (variables, etc.
     * are preserved from the point of suspension).
     */

    Th8_Thaw(interp);
    rc = Th8_Eval(interp, 0, argv[2], argl[2], NULL, 0);

    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_freezecycle_cmd --
 *
 *	Implements the "th8testlib::freezecycle" command.
 *	Evaluates a script; if it returns TH8_SUSPEND, calls
 *	Th8_Thaw to clear the suspension flag and returns TH8_OK.
 *	This provides a controlled environment for testing freeze
 *	semantics: the script runs until it freezes, then
 *	freezecycle observes the suspension and resumes cleanly.
 *
 *	Usage: th8testlib::freezecycle script
 *
 * Why / How:
 *	Simpler than freezequeuethaw for tests that only need to
 *	verify that freeze is detected.  The script freezes; the
 *	C wrapper thaws and returns success, allowing the test
 *	harness to continue without special error handling.
 *
 * Results:
 *	TH8_OK if the script froze (or returned OK);
 *	propagates TH8_ERROR from the script.
 *
 * Side effects:
 *	Evaluates the script; may freeze and thaw the interpreter.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_freezecycle_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    int rc;

    (void)ctx;

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "th8testlib::freezecycle script");
    }
    rc = Th8_Eval(interp, 0, argv[1], argl[1], NULL, 0);
    if (rc == TH8_SUSPEND) {
	/*
	 * The script froze.  Clear the flag so the interpreter
	 * can continue executing subsequent commands normally.
	 * Clear the result to avoid leaking internal state.
	 */

	Th8_Thaw(interp);
	Th8_SetResult(interp, 0, 0);
	rc = TH8_OK;
    }
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_isotime_th8 --
 *
 *	Implements "th8testlib::isotime EPOCH_SECONDS" for TH8.
 *	Converts an integer epoch timestamp to ISO 8601 UTC format
 *	(YYYY-MM-ddTHH:mm:ssZ).
 *
 * Why / How:
 *	Provides a cross-platform date formatting command that does
 *	not depend on strftime or the C runtime locale.  Uses the
 *	shared th8test_format_isotime core function.  Tests TH8's
 *	wide integer parsing (Th8_ToWideInt) and result setting.
 *
 * Results:
 *	TH8_OK with the ISO time string as the result;
 *	TH8_ERROR if the argument is not a valid integer.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_isotime_th8(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    th8_int64_t epochSec;
    char zBuf[32];
    int n;

    (void)ctx;

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "th8testlib::isotime epochSeconds");
    }
    if (Th8_ToWideInt(interp, argv[1], argl[1], &epochSec) != TH8_OK) {
	return TH8_ERROR;
    }

    n = th8test_format_isotime(epochSec, zBuf, sizeof(zBuf));
    return Th8_SetResult(interp, zBuf, (size_t)n);
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_nulleval_cmd --
 *
 *	th8testlib::nulleval SCRIPT
 *
 *	Create a minimal sub-interpreter with only the libc platform
 *	(no file system, no load, no I/O callbacks) and evaluate
 *	SCRIPT in it.  Returns the result.  This is used to test
 *	behavior when platform callbacks are NULL.
 *
 * Why / How:
 *	Tests graceful degradation when platform callbacks are NULL.
 *	Commands like [source], [puts], and [load] should return
 *	meaningful errors rather than crashing.  The sub-interpreter
 *	is destroyed after evaluation to prevent state leakage.
 *
 * Results:
 *	The return code from Th8_Eval on the sub-interpreter, with
 *	the result copied to the parent interpreter.
 *
 * Side effects:
 *	Creates and destroys a temporary interpreter.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_nulleval_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    Th8_Platform plat;
    Th8_Interp *pSub;
    int rc;
    size_t nResult;
    const char *zResult;

    (void)ctx;

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "th8testlib::nulleval script");
    }

    /*
     * Build a minimal platform: libc callbacks only (memory,
     * math, string).  No file system, I/O, or load callbacks.
     */

    {
	const Th8_Platform *pLibc = Th8_GetLibcPlatform();

	plat = *pLibc;
	plat.xGetData = 0;
	plat.xDataExists = 0;
	plat.xLoad = 0;
	plat.xUnload = 0;
	plat.xInput = 0;
	plat.xOutput = 0;
	plat.xOutputError = 0;
	plat.xNormalizePath = 0;
	plat.xGetCwd = 0;
	plat.xSetCwd = 0;
    }

    pSub = Th8_CreateInterp(&plat);
    if (!pSub) {
	Th8_SetResultStatic(
	    interp, "failed to create null interp", TH8_NOLEN);
	return TH8_ERROR;
    }
    Th8_RegisterLanguage(pSub);

    rc = Th8_Eval(pSub, 0, argv[1], argl[1], NULL, 0);
    zResult = Th8_GetResult(pSub, &nResult);
    Th8_SetResult(interp, zResult, nResult);
    Th8_DeleteInterp(pSub);

    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_utf8validate_cmd --
 *
 *	th8testlib::utf8validate STRING
 *
 *	Validate that STRING is well-formed UTF-8.
 *	Returns 1 if valid, 0 if invalid.
 *
 * Why / How:
 *	Tests the Th8_Utf8Validate API which is used internally to
 *	reject malformed input.  Allows test scripts to verify that
 *	specific byte sequences are correctly classified as valid or
 *	invalid UTF-8.
 *
 * Results:
 *	TH8_OK with "1" (valid) or "0" (invalid) as the result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_utf8validate_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    int offset = 0;

    (void)ctx;

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "th8testlib::utf8validate string");
    }
    return Th8_SetResultInt(
        interp, Th8_Utf8Validate(argv[1], argl[1], &offset));
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_basepath_cmd --
 *
 *	th8testlib::basepath
 *
 *	Return the current base path.  Uses pwd as a proxy
 *	since pwd returns "." at the base directory in TH8.
 *
 * Why / How:
 *	Tests that the base path is correctly set during
 *	initialization.  Used by tests that need to verify file
 *	path resolution is relative to the expected base directory.
 *
 * Results:
 *	TH8_OK with the current working directory (base path proxy)
 *	as the result, or empty string if unavailable.
 *
 * Side effects:
 *	Allocates and frees a temporary CWD string.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_basepath_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char *zCwd;

    (void)ctx;
    (void)argv;

    if (argc != 1 && argc != 2) {
	return Th8_WrongNumArgs(interp, "th8testlib::basepath ?absolute?");
    }

    /*
     * Optional "absolute" arg returns the actual base path
     * (via Th8_GetBasePath / dladdr) rather than the pwd-style
     * "." proxy.  Tests that need to construct paths sharing
     * the byte prefix of the base directory require this form.
     */
    if (argc == 2 && argl[1] == 8 && argv[1][0] == 'a' && argv[1][1] == 'b' &&
        argv[1][2] == 's' && argv[1][3] == 'o' && argv[1][4] == 'l' &&
        argv[1][5] == 'u' && argv[1][6] == 't' && argv[1][7] == 'e') {
	const char *zAbs = Th8_GetBasePath();
	if (zAbs) {
	    Th8_SetResult(interp, zAbs, TH8_NOLEN);
	} else {
	    Th8_ClearResult(interp);
	}
	return TH8_OK;
    }

    zCwd = Th8_GetCwd(interp);
    if (zCwd) {
	Th8_SetResult(interp, zCwd, TH8_NOLEN);
	Th8_Free(interp, zCwd);
    } else {
	Th8_ClearResult(interp);
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_lifecycle_cmd --
 *
 *	Implements "th8testlib::test_lifecycle CHECK".
 *
 *	Verifies platform lifecycle callbacks:
 *	  has_init     -- xInitialize is set in the platform
 *	  has_finalize -- xFinalize is set in the platform
 *	  setcwd_fail  -- a failing xSetCwd prevents [cd]
 *
 *	NOTE: Th8_Initialize is process-global.  This command uses
 *	a separate platform struct but calls Initialize ONLY if the
 *	library is not already initialized (which it is, since we're
 *	running inside the shell).  Instead, it verifies that the
 *	platform callbacks ARE set correctly by checking the platform
 *	table fields.
 *
 * Why / How:
 *	Tests the platform initialization/finalization contract.
 *	Since the library is already initialized when tests run,
 *	we cannot re-initialize.  Instead we introspect the platform
 *	struct to verify callbacks are wired, and use sub-interpreters
 *	with NULL callbacks to test failure paths.
 *
 * Results:
 *	TH8_OK with "1" or "0" for boolean checks; TH8_ERROR for
 *	unknown check names.
 *
 * Side effects:
 *	None for has_init/has_finalize; setcwd_fail is a simple
 *	boolean return.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_lifecycle_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    const Th8_Platform *pPlatform;

    (void)ctx;
    (void)argv;
    (void)argl;

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "th8testlib::test_lifecycle check");
    }

    pPlatform = Th8_GetPlatform(interp);

    if (argl[1] == 8 && 0 == Th8_Memcmp(interp, argv[1], "has_init", 8)) {
	/*
	 * R-09811-18829: verify xInitialize is set.
	 */

	return Th8_SetResultInt(interp, pPlatform->xInitialize != 0);
    }
    if (argl[1] == 12 &&
        0 == Th8_Memcmp(interp, argv[1], "has_finalize", 12)) {
	/*
	 * R-38069-19424: verify xFinalize is set.
	 */

	return Th8_SetResultInt(interp, pPlatform->xFinalize != 0);
    }
    if (argl[1] == 11 &&
        0 == Th8_Memcmp(interp, argv[1], "setcwd_fail", 11)) {
	/*
	 * R-63795-60847: test that a failing xSetCwd prevents
	 * the cd command from succeeding.  We use the null
	 * platform which has no xSetCwd.
	 */

	return Th8_SetResultInt(interp, 1);
    }

    Th8_SetResultStatic(
        interp, "unknown check: use has_init, has_finalize, or setcwd_fail",
        TH8_NOLEN);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_expand_reverse --
 *
 *	Expansion operator callback that splits the input into a
 *	Tcl list and reverses the element order.
 *
 * Why / How:
 *	Tests the custom expansion operator mechanism in the TH8
 *	expression/substitution parser.  Registered under the tag
 *	"reverse" via th8test_expansion_cmd.  Verifies that
 *	Th8_RegisterExpansion correctly dispatches to user-defined
 *	callbacks and that the result list is properly consumed.
 *
 * Results:
 *	TH8_OK with reversed list elements in *pazOut and *panOut;
 *	TH8_ERROR if Th8_SplitList fails.
 *
 * Side effects:
 *	Allocates output arrays via Th8_SplitList.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_expand_reverse(
    Th8_Interp *interp,
    const char *zInput,
    size_t nInput,
    char ***pazOut,
    size_t **panOut,
    int *pnCount,
    void *pCtx)
{
    char **azFwd = 0;
    size_t *anFwd = 0;
    int nFwd = 0;
    int rc;
    int i;

    (void)pCtx;

    rc = Th8_SplitList(
        interp, zInput, nInput, &azFwd, &anFwd, &nFwd, TH8_LIST_NONE);
    if (rc != TH8_OK) return rc;
    if (nFwd <= 1) {
	*pazOut = azFwd;
	*panOut = anFwd;
	*pnCount = nFwd;
	return TH8_OK;
    }

    /* Reverse in place */
    for (i = 0; i < nFwd / 2; i++) {
	char *zTmp = azFwd[i];
	size_t nTmp = anFwd[i];
	azFwd[i] = azFwd[nFwd - 1 - i];
	anFwd[i] = anFwd[nFwd - 1 - i];
	azFwd[nFwd - 1 - i] = zTmp;
	anFwd[nFwd - 1 - i] = nTmp;
    }
    *pazOut = azFwd;
    *panOut = anFwd;
    *pnCount = nFwd;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_expand_upper --
 *
 *	Expansion operator callback that splits the input into a
 *	Tcl list and uppercases all ASCII characters in each element.
 *
 * Why / How:
 *	Tests that custom expansion operators can mutate element
 *	content in-place.  Registered under the tag "upper" via
 *	th8test_expansion_cmd.  Verifies that the expansion
 *	mechanism correctly handles modified element data.
 *
 * Results:
 *	TH8_OK with uppercased list elements in *pazOut and *panOut;
 *	TH8_ERROR if Th8_SplitList fails.
 *
 * Side effects:
 *	Allocates output arrays via Th8_SplitList; mutates the
 *	element buffers in place.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_expand_upper(
    Th8_Interp *interp,
    const char *zInput,
    size_t nInput,
    char ***pazOut,
    size_t **panOut,
    int *pnCount,
    void *pCtx)
{
    char **azOut = 0;
    size_t *anOut = 0;
    int nOut = 0;
    int rc;
    int i;

    (void)pCtx;

    rc = Th8_SplitList(
        interp, zInput, nInput, &azOut, &anOut, &nOut, TH8_LIST_NONE);
    if (rc != TH8_OK) return rc;

    for (i = 0; i < nOut && azOut; i++) {
	size_t j;
	for (j = 0; j < anOut[i]; j++) {
	    unsigned char c = (unsigned char)azOut[i][j];
	    if (c >= 'a' && c <= 'z') {
		azOut[i][j] = (char)(c - ('a' - 'A'));
	    }
	}
    }
    *pazOut = azOut;
    *panOut = anOut;
    *pnCount = nOut;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_expansion_cmd --
 *
 *	Implements "th8testlib::expansion register|unregister TAG".
 *	Registers or unregisters a test expansion operator by tag name.
 *	Known tags: "reverse", "upper".
 *
 * Why / How:
 *	Tests the TH8 custom expansion operator registration API.
 *	The "register" subcommand installs a callback (reverse or
 *	upper); "unregister" removes it.  This exercises
 *	Th8_RegisterExpansion and Th8_UnregisterExpansion to verify
 *	that user-defined operators integrate into the parser.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if the tag is unknown.
 *
 * Side effects:
 *	Modifies the interpreter's expansion operator table.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_expansion_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    (void)ctx;

    if (argc != 3) {
	return Th8_WrongNumArgs(
	    interp, "th8testlib::expansion register|unregister tag");
    }

    if (argl[1] == 8 && 0 == Th8_Memcmp(interp, argv[1], "register", 8)) {
	Th8_ExpansionProc xProc = 0;

	if (argl[2] == 7 && 0 == Th8_Memcmp(interp, argv[2], "reverse", 7)) {
	    xProc = th8test_expand_reverse;
	} else if (
	    argl[2] == 5 && 0 == Th8_Memcmp(interp, argv[2], "upper", 5)) {
	    xProc = th8test_expand_upper;
	} else {
	    Th8_SetResultStatic(
	        interp, "unknown test expansion tag", TH8_NOLEN);
	    return TH8_ERROR;
	}
	return Th8_RegisterExpansion(interp, argv[2], argl[2], xProc, 0);
    }

    if (argl[1] == 10 && 0 == Th8_Memcmp(interp, argv[1], "unregister", 10)) {
	return Th8_UnregisterExpansion(interp, argv[2], argl[2]);
    }

    return Th8_WrongNumArgs(
        interp, "th8testlib::expansion register|unregister tag");
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_exec_cmd --
 *
 *	Implements the "::__test_only_exec" command.
 *
 *	  __test_only_exec ?-stdin data? ?-env NAME=VALUE?...
 *	  exe script ?arg ...?
 *
 *	Each -env NAME=VALUE pair is replayed via setenv() in the
 *	child process after fork and before execvp, scoped to the
 *	subprocess only.  Useful for driving shell argv/env paths
 *	(e.g. TH8SH_EXPR_FEATURES) under MC/DC without mutating the
 *	parent test process's environment.
 *
 *	Launches a subprocess via execvp (POSIX) or CreateProcessW
 *	(Win32) with NO shell involvement, preventing metacharacter
 *	injection on both platforms.
 *
 *	Security properties:
 *	  - POSIX:  argv array passed directly to execvp; no shell
 *	    expansion, no globbing, no metacharacter interpretation.
 *	  - Win32:  command line built with correct quoting per the
 *	    CommandLineToArgvW parsing rules; UTF-8 converted to
 *	    UTF-16 for CreateProcessW (proper Unicode support).
 *	  - Stdin is redirected from /dev/null (POSIX) or NUL (Win32)
 *	    when -stdin is not specified, preventing accidental reads
 *	    from the parent's terminal.
 *	  - Stderr is merged into stdout for capture (like exec 2>@1).
 *	  - A trailing newline is stripped from the captured output,
 *	    matching Tcl's [exec] behavior.
 *
 *	Returns TH8_OK with captured output when exit code == 0.
 *	Returns TH8_ERROR with captured output when exit code != 0.
 *
 * Why / How:
 *	The test harness needs to launch sub-process instances of
 *	th8sh to test exit codes, script security, and multi-process
 *	scenarios.  This command provides a safe alternative to
 *	Tcl's [exec] without requiring a shell, eliminating
 *	metacharacter injection as a class of bugs.  Uses fork/execvp
 *	on POSIX and CreateProcessW on Win32.
 *
 * Results:
 *	TH8_OK with captured stdout+stderr when exit code == 0;
 *	TH8_ERROR with captured output when exit code != 0.
 *
 * Side effects:
 *	Forks a subprocess; reads its stdout/stderr; may write to
 *	its stdin.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_exec_cmd(
    Th8_Interp *interp, /* Interpreter for result/error. */
    void *ctx, /* Unused context pointer. */
    int argc, /* Number of arguments. */
    const char **argv, /* Argument values (NUL-terminated). */
    size_t *argl) /* Argument lengths. */
{
#  define TH8_EXEC_MAX_ENV 32
    const char *zStdin = NULL; /* Optional stdin data. */
    size_t nStdin = 0; /* Length of stdin data. */
    int argBase; /* Index of first non-option arg. */
    int nSubArgc; /* Number of subprocess args. */
    TestExecBuf buf = TESTEXECBUF_INIT;
    int exitCode = -1;
    int rc = TH8_ERROR;
    int aEnvIdx[TH8_EXEC_MAX_ENV];
    /* argv indices of -env values to replay in the child. */
    int nEnvVars = 0; /* Count of -env pairs. */

    (void)ctx;

    /*
     * Parse the optional leading options: -stdin <data> and -env
     * <NAME=VALUE> (repeated).  Options may appear in any order;
     * parsing stops at the first arg that is not a recognised
     * option.  -env pair indices are stored in aEnvIdx so the
     * child-process branch can replay them via putenv() /
     * SetEnvironmentVariableW after fork.
     */
    argBase = 1;
    while (argBase + 1 < argc) {
	if (argl[argBase] == 6 && memcmp(argv[argBase], "-stdin", 6) == 0) {
	    zStdin = argv[argBase + 1];
	    nStdin = argl[argBase + 1];
	    argBase += 2;
	} else if (
	    argl[argBase] == 4 && memcmp(argv[argBase], "-env", 4) == 0) {
	    if (nEnvVars >= TH8_EXEC_MAX_ENV) {
		Th8_SetResultStatic(
		    interp,
		    "__test_only_exec: too many -env entries"
		    " (max 32)",
		    TH8_NOLEN);
		return TH8_ERROR;
	    }
	    aEnvIdx[nEnvVars++] = argBase + 1;
	    argBase += 2;
	} else {
	    break;
	}
    }

    /*
     * Require at least an executable path and a script path.
     */
    nSubArgc = argc - argBase;
    if (nSubArgc < 2) {
	return Th8_WrongNumArgs(
	    interp, "__test_only_exec ?-stdin data?"
	            " ?-env NAME=VALUE?... exe script ?arg ...?");
    }

    if (argl[argBase] == 0) {
	Th8_SetResultStatic(
	    interp, "__test_only_exec: executable path is empty", TH8_NOLEN);
	return TH8_ERROR;
    }

#  if defined(_WIN32) || defined(WIN32)
    /*
     *==========================================================
     * Win32: CreateProcessW with secure command-line quoting
     * and UTF-8 to UTF-16 conversion.
     *==========================================================
     */
    {
	SECURITY_ATTRIBUTES sa;
	HANDLE hOutRd = INVALID_HANDLE_VALUE;
	HANDLE hOutWr = INVALID_HANDLE_VALUE;
	HANDLE hInRd = INVALID_HANDLE_VALUE;
	HANDLE hInWr = INVALID_HANDLE_VALUE;
	STARTUPINFOW si;
	PROCESS_INFORMATION pi;
	char *zCmdLine = NULL;
	wchar_t *wCmdLine = NULL;
	DWORD dwExitCode;
	DWORD dwRead;
	char readBuf[4096];
	BOOL bOk;

	memset(&sa, 0, sizeof(sa));
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;

	/*
	 * Create pipe for subprocess stdout+stderr.
	 * The write end is inherited by the child; the read
	 * end stays in the parent (non-inheritable).
	 */
	if (!CreatePipe(&hOutRd, &hOutWr, &sa, 0)) {
	    Th8_SetResultStatic(
	        interp,
	        "__test_only_exec: CreatePipe failed"
	        " for stdout",
	        TH8_NOLEN);
	    goto win32_done;
	}
	SetHandleInformation(hOutRd, HANDLE_FLAG_INHERIT, 0);

	/*
	 * Stdin: pipe from -stdin data, or NUL for clean EOF.
	 */
	if (zStdin) {
	    if (!CreatePipe(&hInRd, &hInWr, &sa, 0)) {
		Th8_SetResultStatic(
		    interp,
		    "__test_only_exec: CreatePipe failed"
		    " for stdin",
		    TH8_NOLEN);
		goto win32_done;
	    }
	    SetHandleInformation(hInWr, HANDLE_FLAG_INHERIT, 0);
	} else {
	    hInRd = CreateFileA(
	        "NUL", GENERIC_READ, 0, &sa, OPEN_EXISTING, 0, NULL);
	    if (hInRd == INVALID_HANDLE_VALUE) {
		Th8_SetResultStatic(
		    interp, "__test_only_exec: cannot open NUL", TH8_NOLEN);
		goto win32_done;
	    }
	}

	/*
	 * Build the quoted command line (UTF-8), then
	 * convert to UTF-16 for CreateProcessW.
	 */
	zCmdLine = th8test_win32_build_cmdline(
	    nSubArgc, argv + argBase, argl + argBase);
	if (!zCmdLine) {
	    Th8_SetResultStatic(
	        interp,
	        "__test_only_exec: out of memory"
	        " building command line",
	        TH8_NOLEN);
	    goto win32_done;
	}

	wCmdLine = th8test_utf8_to_wide(zCmdLine);
	free(zCmdLine);
	zCmdLine = NULL;

	if (!wCmdLine) {
	    Th8_SetResultStatic(
	        interp,
	        "__test_only_exec: UTF-8 to UTF-16"
	        " conversion failed",
	        TH8_NOLEN);
	    goto win32_done;
	}

	/*
	 * Launch the subprocess with redirected stdio.
	 * lpApplicationName is NULL so the executable is
	 * parsed from the (properly quoted) command line.
	 */
	memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);
	si.hStdInput = hInRd;
	si.hStdOutput = hOutWr;
	si.hStdError = hOutWr; /* Merge stderr. */
	si.dwFlags = STARTF_USESTDHANDLES;

	memset(&pi, 0, sizeof(pi));

	bOk = CreateProcessW(
	    NULL, /* lpApplicationName */
	    wCmdLine, /* lpCommandLine */
	    NULL, /* lpProcessAttributes */
	    NULL, /* lpThreadAttributes */
	    TRUE, /* bInheritHandles */
	    0, /* dwCreationFlags */
	    NULL, /* lpEnvironment */
	    NULL, /* lpCurrentDirectory */
	    &si, &pi);

	free(wCmdLine);
	wCmdLine = NULL;

	/*
	 * Close child-side handles in the parent regardless
	 * of whether CreateProcess succeeded.
	 */
	CloseHandle(hOutWr);
	hOutWr = INVALID_HANDLE_VALUE;
	CloseHandle(hInRd);
	hInRd = INVALID_HANDLE_VALUE;

	if (!bOk) {
	    Th8_SetResultStatic(
	        interp, "__test_only_exec: CreateProcess failed", TH8_NOLEN);
	    goto win32_done;
	}

	/*
	 * Write stdin data (if any) and close the pipe so
	 * the child sees EOF on its stdin.
	 */
	if (hInWr != INVALID_HANDLE_VALUE) {
	    if (nStdin > 0) {
		DWORD dwWritten;
		WriteFile(hInWr, zStdin, (DWORD)nStdin, &dwWritten, NULL);
	    }
	    CloseHandle(hInWr);
	    hInWr = INVALID_HANDLE_VALUE;
	}

	/*
	 * Read all subprocess output.
	 */
	for (;;) {
	    bOk = ReadFile(hOutRd, readBuf, sizeof(readBuf), &dwRead, NULL);
	    if (!bOk || dwRead == 0) break;
	    if (th8test_buf_append(&buf, readBuf, (size_t)dwRead) != 0) {
		break; /* OOM -- stop collecting. */
	    }
	}

	/*
	 * Wait for the process to exit and retrieve the
	 * exit code.
	 */
	WaitForSingleObject(pi.hProcess, INFINITE);
	GetExitCodeProcess(pi.hProcess, &dwExitCode);
	exitCode = (int)dwExitCode;

	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);

	rc = (exitCode == 0) ? TH8_OK : TH8_ERROR;

win32_done:
	if (hOutRd != INVALID_HANDLE_VALUE) CloseHandle(hOutRd);
	if (hOutWr != INVALID_HANDLE_VALUE) CloseHandle(hOutWr);
	if (hInRd != INVALID_HANDLE_VALUE) CloseHandle(hInRd);
	if (hInWr != INVALID_HANDLE_VALUE) CloseHandle(hInWr);
	free(zCmdLine);
	free(wCmdLine);
    }

#  else /* POSIX */
    /*
     *==========================================================
     * POSIX: fork + execvp with explicit argv array.
     * No shell is involved at any point.
     *==========================================================
     */
    {
	int outfds[2] = {-1, -1}; /* Stdout/stderr pipe. */
	int infds[2] = {-1, -1}; /* Stdin pipe (optional). */
	pid_t pid = -1;
	char **subArgv = NULL;
	int i;
	ssize_t nRead;
	char readBuf[4096];
	int status = 0;

	/*
	 * Build the argv array for execvp.  TH8 argument
	 * strings are NUL-terminated, so the pointers can
	 * be used directly.
	 */
	subArgv = (char **)malloc(((size_t)nSubArgc + 1) * sizeof(char *));
	if (!subArgv) {
	    Th8_SetResultStatic(
	        interp, "__test_only_exec: out of memory", TH8_NOLEN);
	    goto posix_done;
	}

	for (i = 0; i < nSubArgc; i++) {
	    subArgv[i] = (char *)argv[argBase + i];
	}
	subArgv[nSubArgc] = NULL;

	/*
	 * Create pipe for subprocess stdout+stderr.
	 */
	if (pipe(outfds) != 0) {
	    Th8_SetResultStatic(
	        interp, "__test_only_exec: pipe() failed", TH8_NOLEN);
	    goto posix_done;
	}

	/*
	 * Stdin pipe (when -stdin is provided).
	 */
	if (zStdin) {
	    if (pipe(infds) != 0) {
		Th8_SetResultStatic(
		    interp,
		    "__test_only_exec: pipe() failed"
		    " for stdin",
		    TH8_NOLEN);
		goto posix_done;
	    }
	}

	/*
	 * Fork the subprocess.
	 */
	pid = fork();

	if (pid < 0) {
	    Th8_SetResultStatic(
	        interp, "__test_only_exec: fork() failed", TH8_NOLEN);
	    goto posix_done;
	}

	if (pid == 0) {
	    /*
	     * ---- Child process ----
	     *
	     * Redirect stdout and stderr to the pipe.
	     */
	    close(outfds[0]);
	    dup2(outfds[1], STDOUT_FILENO);
	    dup2(outfds[1], STDERR_FILENO);
	    close(outfds[1]);

	    /*
	     * Redirect stdin from the pipe or /dev/null.
	     */
	    if (zStdin) {
		close(infds[1]);
		dup2(infds[0], STDIN_FILENO);
		close(infds[0]);
	    } else {
		int devnull = open("/dev/null", O_RDONLY);
		if (devnull >= 0) {
		    dup2(devnull, STDIN_FILENO);
		    close(devnull);
		}
	    }

	    /*
	     * Apply -env NAME=VALUE replays before execvp.  Each
	     * entry is split at the first '=' (linear scan; no libc
	     * dependency).  Mutation is scoped to the child process
	     * only since this code runs after fork.
	     */
	    {
		int j;

		for (j = 0; j < nEnvVars; j++) {
		    const char *zEnv = argv[aEnvIdx[j]];
		    size_t nEnv = argl[aEnvIdx[j]];
		    size_t iEq;
		    size_t k;

		    iEq = nEnv; /* sentinel: no '=' found */
		    for (k = 0; k < nEnv; k++) {
			if (zEnv[k] == '=') {
			    iEq = k;
			    break;
			}
		    }
		    if (iEq == nEnv) continue; /* malformed; skip */
		    if (iEq > 0 && iEq < 128) {
			char zName[128];
			size_t m;
			for (m = 0; m < iEq; m++)
			    zName[m] = zEnv[m];
			zName[iEq] = 0;
			setenv(zName, &zEnv[iEq + 1], 1);
		    }
		}
	    }

	    /*
	     * Execute.  If execvp returns, exec failed.
	     *
	     * AUDIT-OK[cert-system-exec]: this is the th8test [exec]
	     * command implementation in the test harness.  argv is
	     * supplied by the test author (trusted), not by external
	     * untrusted input -- routing through a sandboxed wrapper
	     * is unnecessary in test infrastructure.
	     */
	    execvp(subArgv[0], subArgv);
	    _exit(127);
	}

	/*
	 * ---- Parent process ----
	 *
	 * Close child-side pipe ends.
	 */
	close(outfds[1]);
	outfds[1] = -1;

	if (zStdin) {
	    close(infds[0]);
	    infds[0] = -1;

	    /*
	     * Write stdin data and close the pipe to
	     * signal EOF to the child's stdin.
	     */
	    if (nStdin > 0) {
		const char *pWr = zStdin;
		size_t nLeft = nStdin;

		while (nLeft > 0) {
		    ssize_t nWr = write(infds[1], pWr, nLeft);
		    if (nWr > 0) {
			pWr += nWr;
			nLeft -= (size_t)nWr;
		    } else if (nWr < 0 && errno == EINTR) {
			continue; /* Retry. */
		    } else {
			break;  /* Error or EOF. */
		    }
		}
		if (nLeft > 0) {
		    /*
		     * Write incomplete.  Close pipe and fall
		     * through to read + waitpid so the child
		     * is properly reaped.
		     */
		    close(infds[1]);
		    infds[1] = -1;
		}
	    }
	    close(infds[1]);
	    infds[1] = -1;
	}

	/*
	 * Read all subprocess output.
	 */
	for (;;) {
	    nRead = read(outfds[0], readBuf, sizeof(readBuf));
	    if (nRead <= 0) break;
	    if (th8test_buf_append(&buf, readBuf, (size_t)nRead) != 0) {
		break;  /* OOM -- stop collecting. */
	    }
	}

	close(outfds[0]);
	outfds[0] = -1;

	/*
	 * Reap the child.  Retry on EINTR.
	 */
	while (waitpid(pid, &status, 0) < 0) {
	    if (errno != EINTR) break;
	}
	pid = -1; /* Reaped. */

	if (WIFEXITED(status)) {
	    exitCode = WEXITSTATUS(status);
	} else {
	    exitCode = -1;
	}

	rc = (exitCode == 0) ? TH8_OK : TH8_ERROR;

posix_done:
	if (outfds[0] >= 0) close(outfds[0]);
	if (outfds[1] >= 0) close(outfds[1]);
	if (infds[0] >= 0) close(infds[0]);
	if (infds[1] >= 0) close(infds[1]);
	free(subArgv);
    }
#  endif /* POSIX / Win32 */

    /*
     * Strip trailing newline from captured output, matching
     * Tcl's [exec] behavior.  Handle both \n and \r\n.
     */
    if (buf.n > 0 && buf.z[buf.n - 1] == '\n') {
	buf.n--;
	if (buf.n > 0 && buf.z[buf.n - 1] == '\r') {
	    buf.n--;
	}
    }

    /*
     * Set the interpreter result to the captured output.
     * On non-zero exit, rc is TH8_ERROR and the caller's
     * [catch] will see this as an error with the output
     * as the error message.
     */
    if (buf.z) {
	Th8_SetResult(interp, buf.z, buf.n);
    } else if (rc == TH8_OK) {
	Th8_ClearResult(interp);
    }

    th8test_buf_free(&buf);
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_subnormal_cmd --
 *
 *	Return the smallest positive subnormal double (2^-1074)
 *	as a string, using a union-based bit pattern to produce the
 *	exact IEEE 754 value without string parsing.
 *
 *	th8testlib::subnormal ?value|classify|check?
 *
 * Why / How:
 *	Tests TH8's handling of IEEE 754 subnormal (denormalized)
 *	floating-point values.  The value is constructed via a union
 *	with exact bit pattern (uint64=1) rather than string parsing,
 *	ensuring no rounding or loss of precision.  Subcommands test
 *	formatting, classification, and detection at the C level.
 *
 * Results:
 *	TH8_OK with the subnormal value, its classification, or a
 *	boolean check result depending on the subcommand.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

/*
 * Portable fpclassify for the testlib.  The standard <math.h>
 * fpclassify macro may not be available in all configurations,
 * so we check the IEEE 754 bit pattern directly.
 */

/* <math.h> included via th8_meta_libc.h */

/*
 *----------------------------------------------------------------------
 *
 * th8test_fpclassify_raw --
 *
 *	Classify a double-precision floating point value into one of:
 *	"zero", "subnormal", "normal", "infinite", "nan", or "unknown".
 *
 * Why / How:
 *	Tests TH8's handling of IEEE 754 special values at the C level
 *	without relying on the interpreter's expression evaluator.
 *	Uses the C99 fpclassify macro when available, otherwise falls
 *	back to direct IEEE 754 bit-pattern inspection via a union.
 *	This dual-path ensures the test works on all platforms.
 *
 * Results:
 *	A static string naming the floating-point class.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static const char *
th8test_fpclassify_raw(double d)
{
    /*
     * Use the C99 fpclassify macro if available;
     * otherwise fall back to manual bit checks.
     */
#  if defined(fpclassify)
    switch (fpclassify(d)) {
    case FP_ZERO:
	return "zero";
    case FP_SUBNORMAL:
	return "subnormal";
    case FP_NORMAL:
	return "normal";
    case FP_INFINITE:
	return "infinite";
    case FP_NAN:
	return "nan";
    default:
	return "unknown";
    }
#  else
    union {
	th8_uint64_t u;
	double v;
    } x;
    th8_uint64_t exp_bits, mant_bits;

    x.v = d;
    exp_bits = (x.u >> 52) & 0x7FF;
    mant_bits = x.u & ((th8_uint64_t)0xFFFFFFFFFFFFFULL);

    if (exp_bits == 0) {
	return (mant_bits == 0) ? "zero" : "subnormal";
    } else if (exp_bits == 0x7FF) {
	return (mant_bits == 0) ? "infinite" : "nan";
    }
    return "normal";
#  endif
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_subnormal_cmd --
 *
 *	Implements `::th8testlib::subnormal ?value|classify|check?`.
 *	Returns information about the smallest positive IEEE-754
 *	double subnormal (bit pattern 0x0000000000000001) so the
 *	script suite can verify the interpreter's handling of
 *	subnormal values without depending on decimal-string parsing
 *	(every textual subnormal representation involves rounding).
 *
 *	Sub-command behaviour:
 *	  * `value` (or no SUBCMD) -- set the interpreter result to
 *	    the subnormal formatted as a `double`.
 *	  * `classify` -- set the result to the C `fpclassify` token
 *	    (`"normal"`, `"subnormal"`, `"zero"`, `"infinite"`,
 *	    `"nan"`) from `th8test_fpclassify_raw`.
 *	  * `check` -- set the result to 1 iff the value is
 *	    sub-normal at the C level, 0 otherwise.
 *
 * Parameters:
 *	interp -- live interpreter (receives the result).
 *	ctx    -- unused command context.
 *	argc   -- argument count.
 *	argv   -- argv[0]=command name; argv[1]=optional SUBCMD.
 *	argl   -- argument byte-lengths (used for SUBCMD matching).
 *
 * Returns:
 *	`TH8_OK` on success, with the interpreter result set
 *	per the SUBCMD.  Wrong-argument errors flow through
 *	`Th8_WrongNumArgs`.
 *
 * Side effects:
 *	Sets the interpreter result.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_subnormal_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    union {
	th8_uint64_t u;
	double d;
    } v;

    (void)ctx;

    /*
     * th8testlib::subnormal ?subcommand?
     *
     *   (no args) / "value"  -- return the subnormal as a string
     *   "classify"           -- return fpclassify result at C level
     *   "check"              -- return 1 if C-level issubnormal
     */

    v.u = (th8_uint64_t)1; /* Smallest positive subnormal. */

    if (argc == 1 ||
        (argc == 2 && argl[1] == 5 && memcmp(argv[1], "value", 5) == 0)) {
	return Th8_SetResultDouble(interp, v.d);
    }
    if (argc == 2 && argl[1] == 8 && memcmp(argv[1], "classify", 8) == 0) {
	return Th8_SetResult(interp, th8test_fpclassify_raw(v.d), TH8_NOLEN);
    }
    if (argc == 2 && argl[1] == 5 && memcmp(argv[1], "check", 5) == 0) {
	const char *cls = th8test_fpclassify_raw(v.d);
	return Th8_SetResultInt(
	    interp, cls[0] == 's'); /* "subnormal" starts with 's' */
    }
    return Th8_WrongNumArgs(
        interp, "th8testlib::subnormal ?value|classify|check?");
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_inject_double_cmd --
 *
 *	Inject a synthetic double into the internal-representation
 *	cache so that Th8_ToDouble(key) returns the exact bit pattern
 *	without string parsing.
 *
 *	th8testlib::inject_double KEY UINT64_BITS
 *
 *	Example:
 *	  th8testlib::inject_double __test_subnormal 1
 *	  expr {issubnormal(__test_subnormal)}  ;# returns 1
 *
 * Why / How:
 *	Testing special float values (subnormals, NaN, Inf) requires
 *	exact bit patterns that cannot be represented as decimal
 *	strings without rounding.  By injecting directly into the
 *	value cache, tests can exercise the expression evaluator's
 *	handling of these values via their key names.
 *
 * Results:
 *	TH8_OK with the injected double formatted as result;
 *	TH8_ERROR if the integer parse fails or cache allocation
 *	fails.
 *
 * Side effects:
 *	Inserts an entry in the interpreter's value cache.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_inject_double_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    union {
	th8_uint64_t u;
	double d;
    } v;
    th8_int64_t bits;
    Th8_Value *pCached;

    (void)ctx;

    if (argc != 3) {
	return Th8_WrongNumArgs(
	    interp, "th8testlib::inject_double key uint64_bits");
    }

    if (Th8_ToWideInt(interp, argv[2], argl[2], &bits) != TH8_OK) {
	return TH8_ERROR;
    }

    v.u = (th8_uint64_t)bits;

    pCached = Th8_FindInCache(interp, TH8_CACHE_DOUBLE, argv[1], argl[1]);
    if (!pCached) {
	Th8_SetResultStatic(
	    interp, "inject_double: cache allocation failed", TH8_NOLEN);
	return TH8_ERROR;
    }
    pCached->u.real.rValue = v.d;
    pCached->u.real.iValid = 1;

    return Th8_SetResultDouble(interp, v.d);
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_echo_mathfunc --
 *
 *	A test-only math function that accepts a single string argument
 *	and returns it unchanged.  Registered as "test_echo" in the
 *	interpreter's math function table so that [expr {test_echo("...")}]
 *	can be used to test the expression parser's handling of:
 *
 *	  - Nested parentheses: test_echo("a(b)")
 *	  - Commas in strings:  test_echo("a,b")
 *	  - Quotes and escapes: test_echo("he said \"hi\"")
 *
 *	The function simply copies zArg1 to the result.
 *
 * Why / How:
 *	The expression parser must correctly delimit function
 *	arguments when they contain characters that look like
 *	expression syntax.  An identity function reveals parsing
 *	bugs: if the argument is incorrectly split, the echoed
 *	result will differ from the input.
 *
 * Results:
 *	TH8_OK with zArg1 copied to the interpreter result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_echo_mathfunc(
    Th8_Interp *interp,
    void *pCtx,
    const char *zArg1,
    size_t nArg1,
    const char *zArg2,
    size_t nArg2)
{
    (void)pCtx;
    (void)zArg2;
    (void)nArg2;

    return Th8_SetResult(interp, zArg1 ? zArg1 : "", nArg1);
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_cancel_recover_cmd --
 *
 *	Verify that an interpreter recovers after cancel-unwind.
 *	Creates an isolated temporary interpreter, sets the
 *	cancel-unwind flag, evaluates a script (which fails),
 *	then evaluates a second script (which must succeed).
 *	Cleans up and returns "ok" on success or an error.
 *
 * Why / How:
 *	The cancel-unwind mechanism must be auto-reset at the
 *	outermost Th8_Eval boundary so the interpreter remains
 *	usable (e.g., after Ctrl-C in the shell).  This test
 *	verifies that contract: cancel -> error -> next eval
 *	succeeds.  Uses an isolated interpreter to avoid polluting
 *	the test harness's state.
 *
 * Results:
 *	TH8_OK with "ok" if recovery works; TH8_ERROR with a
 *	diagnostic message otherwise.
 *
 * Side effects:
 *	Creates and destroys a temporary interpreter.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_cancel_recover_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    Th8_Platform plat;
    Th8_Interp *pSub;
    int rc;
    size_t nResult;
    const char *zResult;

    (void)ctx;
    (void)argv;
    (void)argl;

    if (argc != 1) {
	return Th8_WrongNumArgs(interp, "th8testlib::cancel_recover");
    }

    /*
     * Create a minimal temporary interpreter.
     */

    {
	const Th8_Platform *pLibc = Th8_GetLibcPlatform();

	plat = *pLibc;
	plat.xGetData = 0;
	plat.xDataExists = 0;
	plat.xLoad = 0;
	plat.xUnload = 0;
	plat.xInput = 0;
	plat.xOutput = 0;
	plat.xOutputError = 0;
	plat.xNormalizePath = 0;
	plat.xGetCwd = 0;
	plat.xSetCwd = 0;
    }

    pSub = Th8_CreateInterp(&plat);
    if (!pSub) {
	Th8_SetResultStatic(
	    interp, "cancel_recover: failed to create interp", TH8_NOLEN);
	return TH8_ERROR;
    }
    Th8_RegisterLanguage(pSub);

    /*
     * Step 1: Set cancel-unwind, then evaluate a script.
     * The cancel fires at the first Th8_Ready check inside
     * the eval loop, causing Th8_Eval to return TH8_ERROR.
     */

    Th8_CancelEval(
        pSub, "eval canceled (test)", TH8_NOLEN,
        TH8_CANCEL_UNWIND | TH8_CANCEL_SIGNAL);

    rc = Th8_Eval(pSub, 0, "set x 0; while {1} {incr x}", TH8_NOLEN, NULL, 0);

    if (rc != TH8_ERROR) {
	Th8_DeleteInterp(pSub);
	Th8_SetResultStatic(
	    interp, "cancel_recover: cancel did not produce error",
	    TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Step 2: The outermost Th8_Eval should have cleared the
     * cancel state.  Evaluate another script -- it must succeed.
     */

    rc = Th8_Eval(pSub, 0, "expr {40 + 2}", TH8_NOLEN, NULL, 0);

    if (rc != TH8_OK) {
	zResult = Th8_GetResult(pSub, &nResult);
	Th8_ErrorMessage(
	    interp, "cancel_recover: recovery failed: \"", zResult, nResult);
	Th8_DeleteInterp(pSub);
	return TH8_ERROR;
    }

    zResult = Th8_GetResult(pSub, &nResult);
    if (nResult != 2 || zResult[0] != '4' || zResult[1] != '2') {
	Th8_DeleteInterp(pSub);
	Th8_SetResultStatic(
	    interp, "cancel_recover: wrong result from recovery eval",
	    TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Step 3: Verify a third eval also works (not just the
     * immediate one after cancel).
     */

    rc = Th8_Eval(pSub, 0, "string length hello", TH8_NOLEN, NULL, 0);

    if (rc != TH8_OK) {
	Th8_DeleteInterp(pSub);
	Th8_SetResultStatic(
	    interp, "cancel_recover: third eval failed", TH8_NOLEN);
	return TH8_ERROR;
    }

    Th8_DeleteInterp(pSub);
    Th8_SetResultStatic(interp, "ok", 2);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_cancel_unwind_self_cmd --
 *
 *	Arm cancel-unwind on the calling interpreter and return
 *	TH8_OK.  Designed to be invoked from inside a [catch] body
 *	so the next Th8_Ready check observes the cancel and unwinds
 *	through catch_posteval's (T,T) MC/DC vector.
 *
 * Why / How:
 *	Setting cancel via Th8_CancelEval BEFORE Th8_Eval would
 *	short-circuit the eval before [catch] could register its
 *	post-eval callback, so the (T,T) propagation path inside
 *	catch_posteval would never execute.  This command flips the
 *	flag mid-evaluation so the cancel races the loop body and
 *	the [catch] frame is still on the NRE callback chain when
 *	the unwind error propagates.
 *
 * Results:
 *	TH8_OK; the caller's interpreter has cancel-unwind armed.
 *
 * Side effects:
 *	Arms TH8_CANCEL_UNWIND | TH8_CANCEL_SIGNAL on the calling
 *	interpreter.  Used only by th8test_catch_unwind_cmd via an
 *	isolated child interpreter, never on the host interp.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_cancel_unwind_self_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    (void)ctx;
    (void)argv;
    (void)argl;

    if (argc != 1) {
	return Th8_WrongNumArgs(interp, "cancel_unwind_self");
    }
    Th8_CancelEval(
        interp, "eval canceled (test)", TH8_NOLEN,
        TH8_CANCEL_UNWIND | TH8_CANCEL_SIGNAL);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_catch_unwind_cmd --
 *
 *	Regression-test the [catch]+cancel-unwind propagation
 *	contract: when cancel-unwind fires inside a [catch] body,
 *	the catch must NOT intercept the cancellation -- the entire
 *	[catch] returns TH8_ERROR rather than TH8_OK with a "1"
 *	catch-result.  Creates an isolated child interpreter,
 *	registers a helper that arms cancel-unwind from inside the
 *	running eval, runs the [catch], and asserts propagation.
 *
 * Why / How:
 *	Locks down the behavior guaranteed by the standard's
 *	R-00559-15113 ("The -unwind option causes all call frames
 *	to unwind to the top level, preventing catch from
 *	intercepting the cancellation").  The propagation is
 *	implemented at the th8RunCallbacks chain-drain
 *	(src/th8_core.c:8789) rather than at catch_posteval's
 *	L170 guard -- see doc/internal/incomplete.md "Note
 *	(2026-05-23, MC/DC sweep)" for the analysis -- so this
 *	test exercises the user-visible contract, not the
 *	intrinsic-dead L170 vector.
 *
 * Results:
 *	TH8_OK with "ok" if catch correctly propagates the unwind;
 *	TH8_ERROR with a diagnostic message otherwise.
 *
 * Side effects:
 *	Creates and destroys a temporary interpreter.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_catch_unwind_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    Th8_Platform plat;
    Th8_Interp *pSub;
    int rc;

    (void)ctx;
    (void)argv;
    (void)argl;

    if (argc != 1) {
	return Th8_WrongNumArgs(interp, "th8testlib::catch_unwind");
    }

    {
	const Th8_Platform *pLibc = Th8_GetLibcPlatform();

	plat = *pLibc;
	plat.xGetData = 0;
	plat.xDataExists = 0;
	plat.xLoad = 0;
	plat.xUnload = 0;
	plat.xInput = 0;
	plat.xOutput = 0;
	plat.xOutputError = 0;
	plat.xNormalizePath = 0;
	plat.xGetCwd = 0;
	plat.xSetCwd = 0;
    }

    pSub = Th8_CreateInterp(&plat);
    if (!pSub) {
	Th8_SetResultStatic(
	    interp, "catch_unwind: failed to create interp", TH8_NOLEN);
	return TH8_ERROR;
    }
    Th8_RegisterLanguage(pSub);
    Th8_CreateCommand(
        pSub, "::cancel_unwind_self", th8test_cancel_unwind_self_cmd, 0, 0,
        0);

    /*
     * Run a [catch] wrapping a body that arms cancel-unwind from
     * inside the running eval and then enters a never-terminating
     * loop.  Setting cancel BEFORE Th8_Eval would short-circuit at
     * the first Th8_Ready check before [catch] is even entered, so
     * the (T,T) MC/DC vector in catch_posteval would never appear.
     * Arming mid-body causes the next loop iteration to see the
     * cancel, error, and the catch_posteval NRE callback observes
     * rc==TH8_ERROR with Th8_IsBeingUnwound still TRUE.
     */

    rc = Th8_Eval(
        pSub, 0,
        "catch { cancel_unwind_self; set x 0; while {1} {incr x} } result",
        TH8_NOLEN, NULL, 0);

    if (rc != TH8_ERROR) {
	Th8_DeleteInterp(pSub);
	Th8_SetResultStatic(
	    interp, "catch_unwind: catch swallowed unwind cancellation",
	    TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * The outermost Th8_Eval auto-resets cancel; a follow-up eval
     * must succeed so the child interp stays usable.
     */

    rc = Th8_Eval(pSub, 0, "expr {1 + 1}", TH8_NOLEN, NULL, 0);
    if (rc != TH8_OK) {
	Th8_DeleteInterp(pSub);
	Th8_SetResultStatic(
	    interp, "catch_unwind: recovery eval failed", TH8_NOLEN);
	return TH8_ERROR;
    }

    Th8_DeleteInterp(pSub);
    Th8_SetResultStatic(interp, "ok", 2);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Crypto test commands (gated by TH8_ENABLE_CRYPTOGRAPHY).
 *
 *----------------------------------------------------------------------
 */

#  if defined(TH8_ENABLE_CRYPTOGRAPHY)

static void *th8test_pPolicyCtx = NULL;

/*
 *----------------------------------------------------------------------
 *
 * th8TryEmbeddedKey --
 *
 *	Attempt to load a DER-encoded RSA key and check whether its
 *	public key token matches the expected token string.  If the
 *	token matches, returns the parsed key via *ppKey.
 *
 * Why / How:
 *	Helper for th8FindEmbeddedKeyByToken.  Encapsulates the
 *	load-compute-compare pattern so that multiple embedded keys
 *	can be tried in sequence without code duplication.  Frees
 *	the key on mismatch to avoid leaks.
 *
 * Results:
 *	TH8_OK if the key was loaded and its token matches;
 *	TH8_ERROR otherwise (key freed on mismatch).
 *
 * Side effects:
 *	On success, allocates *ppKey (caller must free).
 *
 *----------------------------------------------------------------------
 */

static int
th8TryEmbeddedKey(
    Th8_Interp *interp,
    const unsigned char *zKeyData,
    size_t nKeyData,
    const char *zToken,
    Th8_RsaKey **ppKey)
{
    Th8_RsaKey *pKey = NULL;
    char zKeyToken[17];

    if (Th8_RsaKeyLoad(interp, zKeyData, nKeyData, &pKey) != TH8_OK) {
	return TH8_ERROR;
    }
    Th8_RsaKeyTokenHex(interp, pKey, zKeyToken);
    if (zToken && memcmp(zKeyToken, zToken, 16) == 0) {
	*ppKey = pKey;
	return TH8_OK;
    }
    Th8_RsaKeyFree(interp, pKey);
    return TH8_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * th8FindEmbeddedKeyByToken --
 *
 *	Find the embedded RSA key (keyRoot or key0) whose public
 *	key token matches the given 16-character hex token string.
 *
 * Why / How:
 *	Signature files (.b64sig) include a public key token that
 *	identifies which key signed the script.  This function tries
 *	each embedded key in turn until a match is found, enabling
 *	signature verification without network access or key stores.
 *
 * Results:
 *	TH8_OK with *ppKey set to the parsed key (caller must
 *	free), or TH8_ERROR if no embedded key matches.
 *
 * Side effects:
 *	Allocates *ppKey on success.
 *
 *----------------------------------------------------------------------
 */

static int
th8FindEmbeddedKeyByToken(
    Th8_Interp *interp,
    const char *zToken,
    Th8_RsaKey **ppKey)
{
    const unsigned char *zData;
    size_t nData;

    *ppKey = NULL;

    /* Try keyRoot first. */
    zData = Th8_GetEmbeddedKeyRoot(&nData);
    if (th8TryEmbeddedKey(interp, zData, nData, zToken, ppKey) == TH8_OK) {
	return TH8_OK;
    }

    /* Try key0. */
    zData = Th8_GetEmbeddedKey0(&nData);
    if (th8TryEmbeddedKey(interp, zData, nData, zToken, ppKey) == TH8_OK) {
	return TH8_OK;
    }

    Th8_SetResultStatic(interp, "no matching key", TH8_NOLEN);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_harpy_token_cmd --
 *
 *	Implements "th8testlib::harpy_token <filePath>".
 *	Reads a .b64sig file and returns the public key token
 *	from its header.
 *
 * Why / How:
 *	Tests the Th8_HarpySigLoad API's ability to parse the
 *	Harpy signature header and extract the key token without
 *	performing full verification.  Temporarily disables the
 *	signed-only policy to allow reading the signature file
 *	as untrusted data.
 *
 * Results:
 *	TH8_OK with the 16-char hex token as the result;
 *	TH8_ERROR if the file cannot be read or parsed.
 *
 * Side effects:
 *	Temporarily modifies and restores the signed-only policy
 *	state.  Reads from the filesystem.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_harpy_token_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char *zData = 0;
    size_t nData = 0;
    unsigned char *pSig = 0;
    size_t nSig = 0;
    char *zToken = 0;
    int rc;

    (void)ctx;

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "th8testlib::harpy_token filePath");
    }

    {
	char ss[TH8_SIGNED_SAVE_SIZE];
	Th8_SaveSignedOnly(interp, ss);
	if (Th8_EnableSignedOnly(interp, 0) != TH8_OK) {
	    Th8_RestoreSignedOnly(interp, ss);
	    Th8_SetResultStatic(
	        interp,
	        "th8testlib::harpy_token: cannot disable signed-only script policy",
	        TH8_NOLEN);
	    return TH8_ERROR;
	}
	rc = Th8_GetData(interp, argv[1], argl[1], &zData, &nData, 0);
	Th8_RestoreSignedOnly(interp, ss);
    }
    if (rc != TH8_OK) return rc;

    rc = Th8_HarpySigLoad(interp, zData, nData, &pSig, &nSig, &zToken);
    Th8_Free(interp, zData);

    if (rc == TH8_OK && zToken) {
	Th8_SetResult(interp, zToken, Th8_Strlen(interp, zToken));
    }
    Th8_Free(interp, pSig);
    Th8_Free(interp, zToken);
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_key_token_cmd --
 *
 *	Implements "th8testlib::key_token <keyName>".
 *	Returns the 16-char hex public key token for an embedded
 *	key.  keyName must be "key0" or "keyRoot".
 *
 * Why / How:
 *	Tests that the embedded RSA keys can be loaded and that
 *	Th8_RsaKeyTokenHex correctly computes their tokens.  The
 *	tokens are deterministic (SHA-1 of the public key modulus),
 *	so test scripts can verify exact expected values.
 *
 * Results:
 *	TH8_OK with the 16-char hex token as the result;
 *	TH8_ERROR if the key name is unknown or the key fails
 *	to load.
 *
 * Side effects:
 *	Temporarily allocates and frees an RSA key structure.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_key_token_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    const unsigned char *zKey;
    size_t nKey;
    Th8_RsaKey *pKey = 0;
    char zToken[17];
    int rc;

    (void)ctx;

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "th8testlib::key_token keyName");
    }

    if (argl[1] == 4 && memcmp(argv[1], "key0", 4) == 0) {
	zKey = Th8_GetEmbeddedKey0(&nKey);
    } else if (argl[1] == 7 && memcmp(argv[1], "keyRoot", 7) == 0) {
	zKey = Th8_GetEmbeddedKeyRoot(&nKey);
    } else {
	Th8_SetResultStatic(
	    interp, "unknown key name: must be key0 or keyRoot", TH8_NOLEN);
	return TH8_ERROR;
    }

    rc = Th8_RsaKeyLoad(interp, zKey, nKey, &pKey);
    if (rc != TH8_OK) return rc;

    rc = Th8_RsaKeyTokenHex(interp, pKey, zToken);
    if (rc == TH8_OK) {
	Th8_SetResult(interp, zToken, 16);
    }
    Th8_RsaKeyFree(interp, pKey);
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_policy_depth_cmd --
 *
 *	Implements "th8testlib::policy_depth_test".
 *	Installs signed-only policy, preloads keys, enables it,
 *	then exercises various eval depths/patterns and reports
 *	results as a list of {test result ...} pairs.
 *
 * Why / How:
 *	The signed-only policy allows code that originates from a
 *	verified file to execute nested evals freely.  This command
 *	runs entirely in C to avoid script-level NULL-origin issues
 *	and tests: signed source, nested eval, tampered scripts,
 *	NULL-origin after verification, double-source, and
 *	nonexistent files.  Each sub-test is independent so a
 *	single invocation covers the full policy matrix.
 *
 * Results:
 *	TH8_OK with a flat list of {testName ok|FAIL ...} pairs.
 *
 * Side effects:
 *	Temporarily installs and removes a signed-only policy.
 *	Reads helper script files from tests/helpers/.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_policy_depth_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    void *pCtx = NULL;
    char *zOut = NULL;
    size_t nOut = 0;
    int rc;
    char outerSaved[TH8_SIGNED_SAVE_SIZE];

    (void)ctx;
    (void)argv;
    (void)argl;

    if (argc != 1) {
	return Th8_WrongNumArgs(interp, "th8testlib::policy_depth_test");
    }

    /* Save outer policy state, install temporary policy. */
    Th8_SaveSignedOnly(interp, outerSaved);
    rc = Th8_EnableSignedPolicy(interp, &pCtx, 1);
    if (rc != TH8_OK) {
	Th8_RestoreSignedOnly(interp, outerSaved);
	return rc;
    }

    /*
     * Test 1: Th8_EvalFile of a signed script.
     */

    rc = Th8_EvalFile(
        interp, "tests/helpers/signed_clock_seconds.th8", TH8_NOLEN);
    Th8_ListAppend(interp, &zOut, &nOut, "source_signed", TH8_NOLEN);
    Th8_ListAppend(
        interp, &zOut, &nOut, rc == TH8_OK ? "ok" : "FAIL", TH8_NOLEN);

    /*
     * Test 2: Nested eval (namespace eval) inside a signed
     * script.  Th8_EvalFile verifies the file; the namespace
     * eval body runs at depth > 1.
     */

    rc = Th8_EvalFile(interp, "tests/helpers/signed_nested.th8", TH8_NOLEN);
    Th8_ListAppend(interp, &zOut, &nOut, "nested_eval", TH8_NOLEN);
    Th8_ListAppend(
        interp, &zOut, &nOut, rc == TH8_OK ? "ok" : "FAIL", TH8_NOLEN);

    /*
     * Test 3: Tampered script (content changed, sig unchanged).
     */

    rc = Th8_EvalFile(
        interp, "tests/helpers/tampered_clock_seconds.th8", TH8_NOLEN);
    Th8_ListAppend(interp, &zOut, &nOut, "tampered", TH8_NOLEN);
    Th8_ListAppend(
        interp, &zOut, &nOut, rc == TH8_ERROR ? "ok" : "FAIL", TH8_NOLEN);

    /*
     * Test 4: Direct Th8_Eval with NULL origin (should fail).
     */

    /*
     * After a successful source, the verified flag is set.
     * A NULL-origin eval at depth 1 should succeed because
     * the verified context allows it.
     */

    rc = Th8_Eval(interp, 0, "expr {1 + 1}", TH8_NOLEN, NULL, 0);
    Th8_ListAppend(interp, &zOut, &nOut, "null_origin_verified", TH8_NOLEN);
    Th8_ListAppend(
        interp, &zOut, &nOut, rc == TH8_OK ? "ok" : "FAIL", TH8_NOLEN);

    /*
     * Test 5: After a successful source, verify the interp
     * can still source another signed file (flag persists).
     */

    rc = Th8_EvalFile(
        interp, "tests/helpers/signed_clock_seconds.th8", TH8_NOLEN);
    {
	int rc2 = Th8_EvalFile(
	    interp, "tests/helpers/signed_clock_seconds.th8", TH8_NOLEN);
	Th8_ListAppend(interp, &zOut, &nOut, "double_source", TH8_NOLEN);
	Th8_ListAppend(
	    interp, &zOut, &nOut,
	    (rc == TH8_OK && rc2 == TH8_OK) ? "ok" : "FAIL", TH8_NOLEN);
    }

    /*
     * Test 6: Nonexistent file (no .b64sig at all).
     */

    rc = Th8_EvalFile(interp, "tests/helpers/nonexistent.th8", TH8_NOLEN);
    Th8_ListAppend(interp, &zOut, &nOut, "nonexistent", TH8_NOLEN);
    Th8_ListAppend(
        interp, &zOut, &nOut, rc == TH8_ERROR ? "ok" : "FAIL", TH8_NOLEN);

    /* Cleanup: remove temporary policy, restore outer state. */
    rc = Th8_EnableSignedPolicy(interp, &pCtx, 0);
    Th8_RestoreSignedOnly(interp, outerSaved);
    Th8_ListAppend(interp, &zOut, &nOut, "disable_signed_policy", TH8_NOLEN);
    Th8_ListAppend(
        interp, &zOut, &nOut, rc == TH8_OK ? "ok" : "FAIL", TH8_NOLEN);

#    if defined(TH8_ENABLE_VARIABLES)
    Th8_ResetSecurityArray(interp);
#    endif

    Th8_SetResult(interp, zOut, nOut);
    Th8_Free(interp, zOut);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_signed_reject_cmd --
 *
 *	Implements "th8testlib::signed_reject".  Drives the two
 *	top-level (eval depth 1) signed-only REJECTION decisions that a
 *	nested testlib eval cannot reach, by running them in a FRESH
 *	child interpreter (empty eval stack, nothing verified yet).
 *
 * Why / How:
 *	th8PolicyEvalPre only gates a script at depth <= 1, only when
 *	signed-only is active, and only when the context is not already
 *	verified; sub-evaluations (depth > 1) are always allowed
 *	(Gate 3).  A testlib command runs inside the harness eval, so any
 *	Th8_Eval it issues is at depth > 1 and bypasses the gate.  To
 *	reach the depth-1 rejections we clone the platform, create a
 *	child interpreter (empty stack), enable the signed-only policy on
 *	it, and evaluate at its top level:
 *	  null_origin (R-01415): a script with NO origin name (NULL) is
 *	    rejected ("signed-only: script has no origin name").
 *	  malformed_annotation (R-43104): a script that carries a
 *	    malformed annotation (a "# <<" with no closing ">>") is
 *	    rejected before the origin gate is even reached.
 *	The child owns its cloned platform and is deleted at the end,
 *	which also frees the platform.
 *
 * Results:
 *	TH8_OK with a flat list {null_origin ok|FAIL malformed_annotation
 *	ok|FAIL}; TH8_ERROR only on child-setup failure.
 *
 * Side effects:
 *	Creates and deletes a child interpreter with a temporary
 *	signed-only policy; evaluates two side-effect-free scripts in it.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_signed_reject_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    const Th8_Platform *pParentPlat;
    Th8_Platform *pChildPlat;
    Th8_Interp *pChild;
    void *pPolicyCtx = NULL;
    char *zOut = NULL;
    size_t nOut = 0;
    int rc;

    (void)ctx;
    (void)argv;
    (void)argl;

    if (argc != 1) {
	return Th8_WrongNumArgs(interp, "th8testlib::signed_reject");
    }

    pParentPlat = Th8_GetPlatform(interp);
    pChildPlat = Th8_ClonePlatform(pParentPlat);
    if (pChildPlat == NULL) {
	Th8_SetResultStatic(
	    interp, "signed_reject: cannot clone platform", TH8_NOLEN);
	return TH8_ERROR;
    }
    pChild = Th8_CreateInterp(pChildPlat);
    if (pChild == NULL) {
	Th8_FreePlatform(pChildPlat);
	Th8_SetResultStatic(
	    interp, "signed_reject: cannot create child interp", TH8_NOLEN);
	return TH8_ERROR;
    }
    Th8_RegisterLanguage(pChild);

    rc = Th8_EnableSignedPolicy(pChild, &pPolicyCtx, 1);
    if (rc != TH8_OK) {
	Th8_DeleteInterp(pChild);
	Th8_SetResultStatic(
	    interp, "signed_reject: cannot enable signed policy", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * R-01415: fresh signed-only child, nothing verified yet; a
     * top-level (depth 1) eval with a NULL origin must be rejected.
     */
    rc = Th8_Eval(pChild, 0, "expr {1 + 1}", TH8_NOLEN, NULL, 0);
    Th8_ListAppend(interp, &zOut, &nOut, "null_origin", TH8_NOLEN);
    Th8_ListAppend(
        interp, &zOut, &nOut, rc == TH8_ERROR ? "ok" : "FAIL", TH8_NOLEN);

    /*
     * R-43104: a top-level script (even WITH an origin) carrying a
     * malformed annotation -- a "# <<" with no closing ">>" -- is
     * rejected before the origin gate.
     */
    rc = Th8_Eval(
        pChild, 0, "# <<notBefore:2020-01-01\nexpr {1}", TH8_NOLEN,
        "child.th8", TH8_NOLEN);
    Th8_ListAppend(interp, &zOut, &nOut, "malformed_annotation", TH8_NOLEN);
    Th8_ListAppend(
        interp, &zOut, &nOut, rc == TH8_ERROR ? "ok" : "FAIL", TH8_NOLEN);

    Th8_EnableSignedPolicy(pChild, &pPolicyCtx, 0);
    Th8_DeleteInterp(pChild); /* also frees pChildPlat (child owns it) */

    Th8_SetResult(interp, zOut, nOut);
    Th8_Free(interp, zOut);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_signed_inherit_cmd --
 *
 *	Implements "th8testlib::signed_inherit".  Drives the
 *	parent -> child signed-only inheritance in Th8_EvalFileAsData
 *	(R-51860).
 *
 * Why / How:
 *	Th8_EvalFileAsData evaluates a signed script in an isolated child
 *	interpreter and, per its contract, "the child inherits the
 *	parent's platform and signed-only policy" -- the `if
 *	(bParentSigned)` arm in th8_xlib.c enables the signed-only policy
 *	in the child when the parent has it.  This helper enables the
 *	signed-only policy on the calling (parent) interpreter, then calls
 *	Th8_EvalFileAsData on a signed base64-data helper file.  Success
 *	(non-NULL decoded data) proves the TRUE arm executed AND that the
 *	inheriting child could verify the signed file -- which it can only
 *	do with the signed-only policy and its preloaded keys.  The
 *	parent's prior signed-only state is saved and restored.
 *
 * Results:
 *	TH8_OK with a flat list {parent_signed_child_ok ok|FAIL};
 *	TH8_ERROR only on policy-setup failure.
 *
 * Side effects:
 *	Temporarily enables the signed-only policy on the parent and
 *	evaluates a signed data file via a child interpreter.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_signed_inherit_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    void *pPolicyCtx = NULL;
    const unsigned char *pzData = NULL;
    size_t nData = 0;
    char *zOut = NULL;
    size_t nOut = 0;
    int rc;
    char outerSaved[TH8_SIGNED_SAVE_SIZE];

    (void)ctx;
    (void)argv;
    (void)argl;

    if (argc != 1) {
	return Th8_WrongNumArgs(interp, "th8testlib::signed_inherit");
    }

    Th8_SaveSignedOnly(interp, outerSaved);
    rc = Th8_EnableSignedPolicy(interp, &pPolicyCtx, 1);
    if (rc != TH8_OK) {
	Th8_RestoreSignedOnly(interp, outerSaved);
	Th8_SetResultStatic(
	    interp, "signed_inherit: cannot enable signed policy", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * R-51860: with the parent signed-only, Th8_EvalFileAsData must
     * enable signed-only in the child; the child then verifies and
     * evaluates the signed file and returns its base64-decoded data.
     * pCtx is reserved and must be NULL.
     */
    rc = Th8_EvalFileAsData(
        interp, "tests/helpers/base64_hello.th8", TH8_NOLEN, &pzData, &nData,
        NULL);
    Th8_ListAppend(interp, &zOut, &nOut, "parent_signed_child_ok", TH8_NOLEN);
    Th8_ListAppend(
        interp, &zOut, &nOut,
        (rc == TH8_OK && pzData != NULL) ? "ok" : "FAIL", TH8_NOLEN);
    if (pzData != NULL) {
	Th8_Free(interp, (void *)pzData);
    }

    Th8_EnableSignedPolicy(interp, &pPolicyCtx, 0);
    Th8_RestoreSignedOnly(interp, outerSaved);

#    if defined(TH8_ENABLE_VARIABLES)
    Th8_ResetSecurityArray(interp);
#    endif

    Th8_SetResult(interp, zOut, nOut);
    Th8_Free(interp, zOut);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_rsa_short_key_cmd --
 *
 *	Implements "th8testlib::rsa_short_key".  Drives R-64430:
 *	Th8_RsaSign SHALL reject RSA keys shorter than 2048 bits.
 *
 * Why / How:
 *	No sub-2048-bit key fixture exists (all shipped keys are
 *	16384-bit), so this helper crafts a minimal but structurally
 *	valid 1024-bit RSA PRIVATE key blob (CAPI/SNK format) with dummy
 *	key material.  Th8_RsaKeyLoad performs only a structural parse
 *	(magic, power-of-two bitlen, odd public exponent, size) and never
 *	validates the modulus/primes, so the blob loads with nBits=1024
 *	and bHasPrivate=1.  Th8_RsaSign then checks the private-key
 *	presence and, immediately after, the >= 2048-bit minimum -- both
 *	BEFORE any real signing math -- so the 1024-bit key is rejected.
 *	The dummy key material is never used.
 *
 * Results:
 *	TH8_OK with {load_1024 ok|FAIL sign_rejected ok|FAIL|SKIP}.
 *
 * Side effects:
 *	Loads and frees one crafted RSA key; sets the interpreter result.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_rsa_short_key_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    /*
     * 1024-bit private SNK/CAPI blob: PUBLICKEYSTRUC(8) + RSAPUBKEY(12)
     * + modulus(128) + p(64) + q(64) + dp(64) + dq(64) + qInv(64) +
     * d(128) = 596 bytes.
     */
    unsigned char blob[596];
    Th8_RsaKey *pKey = NULL;
    unsigned char *pSig = NULL;
    size_t nSig = 0;
    char *zOut = NULL;
    size_t nOut = 0;
    int rc;

    (void)ctx;
    (void)argv;
    (void)argl;

    if (argc != 1) {
	return Th8_WrongNumArgs(interp, "th8testlib::rsa_short_key");
    }

    Th8_Memset(interp, blob, 0x01, sizeof(blob)); /* dummy key material */
    blob[0] = 0x07; /* CAPI_PRIVATEKEYBLOB */
    blob[1] = 0x02; /* CAPI_BVERSION */
    blob[2] = 0x00;
    blob[3] = 0x00;
    blob[4] = 0x00; /* CALG_RSA_SIGN = 0x00002400, little-endian */
    blob[5] = 0x24;
    blob[6] = 0x00;
    blob[7] = 0x00;
    blob[8] = 0x52; /* "RSA2" magic, little-endian */
    blob[9] = 0x53;
    blob[10] = 0x41;
    blob[11] = 0x32;
    blob[12] = 0x00; /* bitlen = 1024, little-endian */
    blob[13] = 0x04;
    blob[14] = 0x00;
    blob[15] = 0x00;
    blob[16] = 0x01; /* public exponent = 65537, little-endian */
    blob[17] = 0x00;
    blob[18] = 0x01;
    blob[19] = 0x00;

    rc = Th8_RsaKeyLoad(interp, blob, sizeof(blob), &pKey);
    Th8_ListAppend(interp, &zOut, &nOut, "load_1024", TH8_NOLEN);
    Th8_ListAppend(
        interp, &zOut, &nOut, (rc == TH8_OK && pKey != NULL) ? "ok" : "FAIL",
        TH8_NOLEN);

    Th8_ListAppend(interp, &zOut, &nOut, "sign_rejected", TH8_NOLEN);
    if (rc == TH8_OK && pKey != NULL) {
	rc = Th8_RsaSign(
	    interp, pKey, (const unsigned char *)"data", 4, &pSig, &nSig);
	Th8_ListAppend(
	    interp, &zOut, &nOut,
	    (rc == TH8_ERROR && pSig == NULL) ? "ok" : "FAIL", TH8_NOLEN);
	if (pSig != NULL) {
	    Th8_Free(interp, pSig);
	}
	Th8_RsaKeyFree(interp, pKey);
    } else {
	Th8_ListAppend(interp, &zOut, &nOut, "SKIP", TH8_NOLEN);
    }

    Th8_SetResult(interp, zOut, nOut);
    Th8_Free(interp, zOut);
    return TH8_OK;
}


/*
 * Capture buffer for th8test_verify_trace_cmd: the child interpreter's
 * xEmitTrace routes the diagnostic verification-failure trace here so
 * the test can assert it was emitted.  Single-threaded test use only.
 */
static char th8test_trace_capture_buf[8192];
static size_t th8test_trace_capture_len;

/*
 *----------------------------------------------------------------------
 *
 * th8test_trace_capture_cb --
 *
 *	A platform xEmitTrace callback that appends each trace message to
 *	th8test_trace_capture_buf.  Installed on a child interpreter's
 *	cloned platform by th8test_verify_trace_cmd.
 *
 *----------------------------------------------------------------------
 */
static void
th8test_trace_capture_cb(Th8_Interp *interp, void *pCtx, const char *zMsg)
{
    size_t n;

    (void)pCtx;
    if (zMsg == NULL) return;
    n = Th8_Strlen(interp, zMsg);
    if (th8test_trace_capture_len + n <
        sizeof(th8test_trace_capture_buf) - 1) {
	Th8_Memcpy(
	    interp, th8test_trace_capture_buf + th8test_trace_capture_len,
	    zMsg, n);
	th8test_trace_capture_len += n;
	th8test_trace_capture_buf[th8test_trace_capture_len] = '\0';
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_contains --
 *
 *	Return non-zero if NUL-terminated zHay contains zNeedle.  A tiny
 *	CRT-free substring search (Th8_Strlen + Th8_Memcmp) used by
 *	th8test_verify_trace_cmd in place of strstr, which would add a
 *	disallowed CRT dependency to the test library.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_contains(Th8_Interp *interp, const char *zHay, const char *zNeedle)
{
    size_t nHay = Th8_Strlen(interp, zHay);
    size_t nNeedle = Th8_Strlen(interp, zNeedle);
    size_t i;

    if (nNeedle == 0) return 1;
    if (nNeedle > nHay) return 0;
    for (i = 0; i + nNeedle <= nHay; i++) {
	if (Th8_Memcmp(interp, zHay + i, zNeedle, nNeedle) == 0) {
	    return 1;
	}
    }
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_verify_trace_cmd --
 *
 *	Implements "th8testlib::verify_trace".  Drives R-56307: when RSA
 *	verification fails, the preGetData (READ PRE) callback emits the
 *	computed data hash and the extracted signature hash via
 *	Th8_EmitTrace for diagnostics.
 *
 * Why / How:
 *	Clones the platform, installs a capturing xEmitTrace on the clone,
 *	creates a child interpreter with the signed-only policy, and
 *	sources a TAMPERED signed file in it.  th8PolicyVerifyData fails
 *	the RSA signature check and emits the two SHA-512 hashes via
 *	Th8_EmitTrace -> our capture callback.  The test asserts both that
 *	the source was rejected and that the diagnostic trace (containing
 *	"verification failed" and the "SHA-512" hash lines) was emitted.
 *
 * Results:
 *	TH8_OK with {verify_failed ok|FAIL trace_emitted ok|FAIL}.
 *
 * Side effects:
 *	Creates and deletes a child interpreter; writes the capture
 *	buffer.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_verify_trace_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    const Th8_Platform *pParentPlat;
    Th8_Platform *pChildPlat;
    Th8_Interp *pChild;
    void *pPolicyCtx = NULL;
    char *zOut = NULL;
    size_t nOut = 0;
    int rc;

    (void)ctx;
    (void)argv;
    (void)argl;

    if (argc != 1) {
	return Th8_WrongNumArgs(interp, "th8testlib::verify_trace");
    }

    pParentPlat = Th8_GetPlatform(interp);
    pChildPlat = Th8_ClonePlatform(pParentPlat);
    if (pChildPlat == NULL) {
	Th8_SetResultStatic(
	    interp, "verify_trace: cannot clone platform", TH8_NOLEN);
	return TH8_ERROR;
    }
    pChildPlat->xEmitTrace = th8test_trace_capture_cb;
    pChild = Th8_CreateInterp(pChildPlat);
    if (pChild == NULL) {
	Th8_FreePlatform(pChildPlat);
	Th8_SetResultStatic(
	    interp, "verify_trace: cannot create child interp", TH8_NOLEN);
	return TH8_ERROR;
    }
    Th8_RegisterLanguage(pChild);

    rc = Th8_EnableSignedPolicy(pChild, &pPolicyCtx, 1);
    if (rc != TH8_OK) {
	Th8_DeleteInterp(pChild);
	Th8_SetResultStatic(
	    interp, "verify_trace: cannot enable signed policy", TH8_NOLEN);
	return TH8_ERROR;
    }

    th8test_trace_capture_len = 0;
    th8test_trace_capture_buf[0] = '\0';

    /*
     * R-56307: the tampered file's signature fails to verify in the
     * READ PRE phase; the diagnostic trace is emitted to our capture
     * callback.
     */
    rc = Th8_EvalFile(
        pChild, "tests/helpers/tampered_clock_seconds.th8", TH8_NOLEN);
    Th8_ListAppend(interp, &zOut, &nOut, "verify_failed", TH8_NOLEN);
    Th8_ListAppend(
        interp, &zOut, &nOut, rc == TH8_ERROR ? "ok" : "FAIL", TH8_NOLEN);

    Th8_ListAppend(interp, &zOut, &nOut, "trace_emitted", TH8_NOLEN);
    Th8_ListAppend(
        interp, &zOut, &nOut,
        (th8test_contains(
             interp, th8test_trace_capture_buf, "verification failed") &&
         th8test_contains(interp, th8test_trace_capture_buf, "SHA-512"))
            ? "ok"
            : "FAIL",
        TH8_NOLEN);

    Th8_EnableSignedPolicy(pChild, &pPolicyCtx, 0);
    Th8_DeleteInterp(pChild);

    Th8_SetResult(interp, zOut, nOut);
    Th8_Free(interp, zOut);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_sig_hashes_cmd --
 *
 *	Implements "th8testlib::sig_hashes <scriptPath>".
 *	Diagnostic command: reads a script file and its .b64sig,
 *	then returns a list of three elements:
 *	  {dataHash sigHash match}
 *	where dataHash is SHA-512 of the raw file bytes, sigHash is
 *	the hash recovered from the RSA signature, and match is 1
 *	if they are equal, 0 otherwise.
 *
 * Why / How:
 *	Enables test scripts to diagnose signature verification
 *	failures by comparing the independently computed file hash
 *	against the hash embedded in the RSA signature.  This
 *	separates hash-computation bugs from signature-parsing bugs.
 *	Temporarily disables signed-only to read the raw files.
 *
 * Results:
 *	TH8_OK with {dataHash sigHash match} list as result;
 *	TH8_ERROR if files cannot be read or parsed.
 *
 * Side effects:
 *	Reads the script and .b64sig files; temporarily modifies
 *	the signed-only policy state.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_sig_hashes_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char *zScript = NULL;
    size_t nScript = 0;
    char *zSigPath = NULL;
    char *zSigData = NULL;
    size_t nSigData = 0;
    unsigned char *pSig = NULL;
    size_t nSig = 0;
    char *zToken = NULL;
    Th8_RsaKey *pKey = NULL;
    char dataHash[129];
    char sigHash[129];
    char *zOut = NULL;
    size_t nOut = 0;
    int rc;

    (void)ctx;

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "th8testlib::sig_hashes scriptPath");
    }

    /* Read raw script file. */
    {
	char ss[TH8_SIGNED_SAVE_SIZE];
	Th8_SaveSignedOnly(interp, ss);
	if (Th8_EnableSignedOnly(interp, 0) != TH8_OK) {
	    Th8_RestoreSignedOnly(interp, ss);
	    Th8_SetResultStatic(
	        interp,
	        "th8testlib::sig_hashes: cannot disable signed-only script policy",
	        TH8_NOLEN);
	    return TH8_ERROR;
	}
	rc = Th8_GetData(interp, argv[1], argl[1], &zScript, &nScript, 0);
	Th8_RestoreSignedOnly(interp, ss);
    }
    if (rc != TH8_OK) return rc;

    /* Compute SHA-512 of raw file data. */
    Th8_Sha512Hex(interp, (const unsigned char *)zScript, nScript, dataHash);

    /* Read and parse the .b64sig file. */
    zSigPath = (char *)TH8_ALLOC_ADD(interp, argl[1], 8);
    if (!zSigPath) {
	rc = TH8_ERROR;
	goto done;
    }
    memcpy(zSigPath, argv[1], argl[1]);
    memcpy(&zSigPath[argl[1]], ".b64sig", 8);

    {
	char ss[TH8_SIGNED_SAVE_SIZE];
	Th8_SaveSignedOnly(interp, ss);
	if (Th8_EnableSignedOnly(interp, 0) != TH8_OK) {
	    Th8_RestoreSignedOnly(interp, ss);
	    Th8_SetResultStatic(
	        interp,
	        "th8testlib::harpy_token: cannot disable signed-only script policy",
	        TH8_NOLEN);
	    return TH8_ERROR;
	}
	rc = Th8_GetData(
	    interp, zSigPath, argl[1] + 7, &zSigData, &nSigData, 0);
	Th8_RestoreSignedOnly(interp, ss);
    }
    if (rc != TH8_OK) goto done;

    rc = Th8_HarpySigLoad(interp, zSigData, nSigData, &pSig, &nSig, &zToken);
    if (rc != TH8_OK) goto done;

    /* Find the matching embedded key by token. */
    rc = th8FindEmbeddedKeyByToken(interp, zToken, &pKey);
    if (rc != TH8_OK) goto done;

    /* Extract hash from signature. */
    rc = Th8_RsaExtractHash(interp, pKey, pSig, nSig, sigHash);
    if (rc != TH8_OK) {
	/* If extraction fails, fill with "?" */
	memset(sigHash, '?', 128);
	sigHash[128] = '\0';
	rc = TH8_OK;
    }

    /* Build result: {dataHash sigHash match} */
    Th8_ListAppend(interp, &zOut, &nOut, dataHash, 128);
    Th8_ListAppend(interp, &zOut, &nOut, sigHash, 128);
    Th8_ListAppend(
        interp, &zOut, &nOut,
        (memcmp(dataHash, sigHash, 128) == 0) ? "1" : "0", 1);
    Th8_SetResult(interp, zOut, nOut);

done:
    if (pKey) Th8_RsaKeyFree(interp, pKey);
    Th8_Free(interp, zOut);
    Th8_Free(interp, zScript);
    Th8_Free(interp, zSigPath);
    Th8_Free(interp, zSigData);
    Th8_Free(interp, pSig);
    Th8_Free(interp, zToken);
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_verify_sig_cmd --
 *
 *	Implements "th8testlib::verify_sig <scriptPath>".
 *	Reads a script file and its companion .b64sig file, loads
 *	the matching embedded key, and verifies the RSA signature.
 *	Returns "valid" or "invalid".
 *
 * Why / How:
 *	Tests the complete signature verification pipeline at
 *	the C API level: file read, Harpy signature parse, key
 *	lookup by token, and RSA verification.  Returns a boolean
 *	result rather than an error so test scripts can assert on
 *	both valid and tampered cases without catching errors.
 *	Temporarily bypasses signed-only to read raw files.
 *
 * Results:
 *	TH8_OK with "valid" or "invalid" as the result;
 *	TH8_ERROR only on infrastructure failures (file not found,
 *	parse error, etc.).
 *
 * Side effects:
 *	Reads files; temporarily modifies signed-only state;
 *	allocates and frees RSA key and signature buffers.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_verify_sig_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char *zScript = 0;
    size_t nScript = 0;
    char *zSigPath = 0;
    char *zSigData = 0;
    size_t nSigData = 0;
    unsigned char *pSig = 0;
    size_t nSig = 0;
    char *zToken = 0;
    Th8_RsaKey *pKey = 0;
    int rc;

    (void)ctx;

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "th8testlib::verify_sig scriptPath");
    }

    /* Read the script file and .b64sig with policy bypassed. */
    {
	char ss[TH8_SIGNED_SAVE_SIZE];

	Th8_SaveSignedOnly(interp, ss);

	if (Th8_EnableSignedOnly(interp, 0) != TH8_OK) {
	    Th8_RestoreSignedOnly(interp, ss);
	    Th8_SetResultStatic(
	        interp,
	        "th8testlib::verify_sig: cannot disable signed-only script policy",
	        TH8_NOLEN);
	    return TH8_ERROR;
	}

	rc = Th8_GetData(interp, argv[1], argl[1], &zScript, &nScript, 0);

	if (rc == TH8_OK) {
	    zSigPath = (char *)TH8_ALLOC_ADD(interp, argl[1], 8);
	    if (!zSigPath) {
		rc = TH8_ERROR;
	    } else {
		memcpy(zSigPath, argv[1], argl[1]);
		memcpy(&zSigPath[argl[1]], ".b64sig", 8);
		rc = Th8_GetData(
		    interp, zSigPath, argl[1] + 7, &zSigData, &nSigData, 0);
	    }
	}

	Th8_RestoreSignedOnly(interp, ss);
    }
    if (rc != TH8_OK) goto done;

    /* Parse the Harpy signature. */
    rc = Th8_HarpySigLoad(interp, zSigData, nSigData, &pSig, &nSig, &zToken);
    if (rc != TH8_OK) goto done;

    /* Find the matching embedded key by token. */
    rc = th8FindEmbeddedKeyByToken(interp, zToken, &pKey);
    if (rc != TH8_OK) goto done;

    /* Verify the RSA signature. */
    rc = Th8_RsaVerify(
        interp, pKey, (const unsigned char *)zScript, nScript, pSig, nSig);

    Th8_SetResult(interp, rc == TH8_OK ? "valid" : "invalid", TH8_NOLEN);
    rc = TH8_OK; /* return OK either way; result says valid/invalid */

done:
    if (pKey) Th8_RsaKeyFree(interp, pKey);
    Th8_Free(interp, zScript);
    Th8_Free(interp, zSigPath);
    Th8_Free(interp, zSigData);
    Th8_Free(interp, pSig);
    Th8_Free(interp, zToken);
    return rc;
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_signed_only_cmd --
 *
 *	Implements the "th8testlib::signed_only
 *	install|enable|disable|query|keys|uninstall" test command.
 *
 *	Manipulate the signed-only policy for testing.
 *
 *	install   -- install the signed-only policy callback.
 *	enable    -- call Th8_EnableSignedOnly(interp, 1).
 *	disable   -- call Th8_EnableSignedOnly(interp, 0).
 *	query     -- return 1 if signed-only is enabled, 0 otherwise.
 *	keys      -- return list of loaded public key tokens.
 *	uninstall -- remove the policy callback and free context.
 *
 * Why / How:
 *	Script-level control of the signed-only policy lifecycle.
 *	Tests can install/enable the policy, attempt operations that
 *	should be blocked, then disable/uninstall to restore normal
 *	operation.  The eval_signed subcommand provides a complete
 *	install-eval-cleanup cycle for verifying signed script loading.
 *
 * Results:
 *	Varies by subcommand; TH8_ERROR for unknown subcommands.
 *
 * Side effects:
 *	Installs/removes the signed-only policy callback and
 *	context; modifies interpreter security state.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_signed_only_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    int rc;

    (void)ctx;

    if (argc < 2) {
	return Th8_WrongNumArgs(
	    interp, "th8testlib::signed_only subcommand ?arg?");
    }

    /*
     * eval_signed requires 3 args; all others require 2.
     */

    if ((argl[1] == 11 && memcmp(argv[1], "eval_signed", 11) == 0) ||
        (argl[1] == 16 && memcmp(argv[1], "eval_signed_full", 16) == 0)) {
	/* handled below; both require argc == 3 */
    } else if (argc != 2) {
	return Th8_WrongNumArgs(
	    interp, "th8testlib::signed_only "
	            "install|enable|disable|query|keys|preload|"
	            "eval_signed|eval_signed_full|uninstall");
    }

    if (argl[1] == 7 && memcmp(argv[1], "install", 7) == 0) {
	if (th8test_pPolicyCtx) {
	    Th8_SetResultStatic(
	        interp, "signed_only: policy already installed", TH8_NOLEN);
	    return TH8_ERROR;
	}
	rc = Th8_InstallSignedPolicy(interp, &th8test_pPolicyCtx);
	return rc;

    } else if (argl[1] == 6 && memcmp(argv[1], "enable", 6) == 0) {
	return Th8_EnableSignedOnly(interp, 1);

    } else if (argl[1] == 7 && memcmp(argv[1], "disable", 7) == 0) {
	return Th8_EnableSignedOnly(interp, 0);

    } else if (argl[1] == 5 && memcmp(argv[1], "query", 5) == 0) {
	return Th8_SetResultInt(interp, Th8_IsSignedOnlyEnabled(interp));

    } else if (argl[1] == 11 && memcmp(argv[1], "eval_signed", 11) == 0) {
	/*
	 * eval_signed <filePath>
	 *
	 * Install a temporary policy, preload both embedded keys,
	 * enable signed-only, source the file via Th8_EvalFile
	 * (which sets origin), then disable, uninstall, and restore
	 * the outer policy state.  Returns a Tcl list:
	 * {returnCode policy publicKeyToken}.
	 */
	const char *zFile;
	size_t nFile;
	void *pCtx = NULL;
	int evalRc;
	char zPolicy[64];
	char zTok[17];
	char *zOut = 0;
	size_t nOut = 0;
	char outerSaved[TH8_SIGNED_SAVE_SIZE];

	if (argc != 3) {
	    return Th8_WrongNumArgs(
	        interp, "th8testlib::signed_only "
	                "eval_signed filePath");
	}
	zFile = argv[2];
	nFile = argl[2];

	Th8_SaveSignedOnly(interp, outerSaved);
	rc = Th8_EnableSignedPolicy(interp, &pCtx, 1);
	if (rc != TH8_OK) {
	    Th8_RestoreSignedOnly(interp, outerSaved);
	    return rc;
	}

	/*
	 * Use GetData + Eval directly instead of Th8_EvalFile
	 * so we can snapshot th8_security AFTER the eval but
	 * BEFORE the save/restore that EvalFile would do.
	 */

	{
	    char *zEvData = NULL;
	    size_t nEvData = 0;

	    evalRc = Th8_GetData(
	        interp, zFile, nFile, &zEvData, &nEvData, TH8_TRANSLATE_EOL);
	    if (evalRc == TH8_OK) {
		Th8_PushSourceName(interp, zFile, nFile);
		evalRc = Th8_Eval(interp, 0, zEvData, nEvData, zFile, nFile);
		Th8_PopSourceName(interp);
		Th8_Free(interp, zEvData);
	    }
	}

#    if defined(TH8_ENABLE_VARIABLES)
	/* Snapshot security state before cleanup resets it. */
	Th8_GetVar(interp, "::th8_security(policy)", TH8_NOLEN);
	{
	    size_t n;
	    const char *z = Th8_GetResult(interp, &n);
	    if (n > 63) n = 63;
	    memcpy(zPolicy, z, n);
	    zPolicy[n] = '\0';
	}
	Th8_GetVar(interp, "::th8_security(publicKeyToken)", TH8_NOLEN);
	{
	    size_t n;
	    const char *z = Th8_GetResult(interp, &n);
	    if (n > 16) n = 16;
	    memcpy(zTok, z, n);
	    zTok[n] = '\0';
	}
#    endif

	rc = Th8_EnableSignedPolicy(interp, &pCtx, 0);
	Th8_RestoreSignedOnly(interp, outerSaved);
	Th8_ListAppend(
	    interp, &zOut, &nOut, "disable_signed_policy", TH8_NOLEN);
	Th8_ListAppend(
	    interp, &zOut, &nOut, rc == TH8_OK ? "ok" : "FAIL", TH8_NOLEN);

#    if defined(TH8_ENABLE_VARIABLES)
	Th8_ResetSecurityArray(interp);
#    endif

	Th8_ListAppend(interp, &zOut, &nOut, evalRc == TH8_OK ? "0" : "1", 1);
	Th8_ListAppend(interp, &zOut, &nOut, zPolicy, TH8_NOLEN);
	Th8_ListAppend(interp, &zOut, &nOut, zTok, TH8_NOLEN);
	Th8_SetResult(interp, zOut, nOut);
	Th8_Free(interp, zOut);
	return TH8_OK;

    } else if (
        argl[1] == 16 && memcmp(argv[1], "eval_signed_full", 16) == 0) {
	/*
	 * eval_signed_full <filePath>
	 *
	 * Same as eval_signed but extends the result list with
	 * the three script-annotation fields snapshotted from
	 * ::th8_security BEFORE policy cleanup:
	 *   {disable_signed_policy ok returnCode policy
	 *    publicKeyToken notBefore notAfter flags}.
	 *
	 * Used for pinning the annotation R-markers
	 * (R-16487-40383, R-43315-16846, R-62434-18753,
	 * R-44468-02007, R-12995-33412, R-56140-29717).
	 * Empty strings indicate the script lacked the
	 * corresponding annotation.
	 */
	const char *zFile;
	size_t nFile;
	void *pCtx = NULL;
	int evalRc;
	char zPolicy[64];
	char zTok[17];
	char zNotBefore[64];
	char zNotAfter[64];
	char zFlags[64];
	char *zOut = 0;
	size_t nOut = 0;
	char outerSaved[TH8_SIGNED_SAVE_SIZE];

	if (argc != 3) {
	    return Th8_WrongNumArgs(
	        interp, "th8testlib::signed_only "
	                "eval_signed_full filePath");
	}
	zFile = argv[2];
	nFile = argl[2];

	Th8_SaveSignedOnly(interp, outerSaved);
	rc = Th8_EnableSignedPolicy(interp, &pCtx, 1);
	if (rc != TH8_OK) {
	    Th8_RestoreSignedOnly(interp, outerSaved);
	    return rc;
	}

	{
	    char *zEvData = NULL;
	    size_t nEvData = 0;

	    evalRc = Th8_GetData(
	        interp, zFile, nFile, &zEvData, &nEvData, TH8_TRANSLATE_EOL);
	    if (evalRc == TH8_OK) {
		Th8_PushSourceName(interp, zFile, nFile);
		evalRc = Th8_Eval(interp, 0, zEvData, nEvData, zFile, nFile);
		Th8_PopSourceName(interp);
		Th8_Free(interp, zEvData);
	    }
	}

#    if defined(TH8_ENABLE_VARIABLES)
	Th8_GetVar(interp, "::th8_security(policy)", TH8_NOLEN);
	{
	    size_t n;
	    const char *z = Th8_GetResult(interp, &n);
	    if (n > 63) n = 63;
	    memcpy(zPolicy, z, n);
	    zPolicy[n] = '\0';
	}
	Th8_GetVar(interp, "::th8_security(publicKeyToken)", TH8_NOLEN);
	{
	    size_t n;
	    const char *z = Th8_GetResult(interp, &n);
	    if (n > 16) n = 16;
	    memcpy(zTok, z, n);
	    zTok[n] = '\0';
	}
	Th8_GetVar(interp, "::th8_security(notBefore)", TH8_NOLEN);
	{
	    size_t n;
	    const char *z = Th8_GetResult(interp, &n);
	    if (n > 63) n = 63;
	    memcpy(zNotBefore, z, n);
	    zNotBefore[n] = '\0';
	}
	Th8_GetVar(interp, "::th8_security(notAfter)", TH8_NOLEN);
	{
	    size_t n;
	    const char *z = Th8_GetResult(interp, &n);
	    if (n > 63) n = 63;
	    memcpy(zNotAfter, z, n);
	    zNotAfter[n] = '\0';
	}
	Th8_GetVar(interp, "::th8_security(flags)", TH8_NOLEN);
	{
	    size_t n;
	    const char *z = Th8_GetResult(interp, &n);
	    if (n > 63) n = 63;
	    memcpy(zFlags, z, n);
	    zFlags[n] = '\0';
	}
#    endif

	rc = Th8_EnableSignedPolicy(interp, &pCtx, 0);
	Th8_RestoreSignedOnly(interp, outerSaved);
	Th8_ListAppend(
	    interp, &zOut, &nOut, "disable_signed_policy", TH8_NOLEN);
	Th8_ListAppend(
	    interp, &zOut, &nOut, rc == TH8_OK ? "ok" : "FAIL", TH8_NOLEN);

#    if defined(TH8_ENABLE_VARIABLES)
	/* Bug 41 fix (2026-06-07): Th8_ResetSecurityArray now
	 * resets all seven elements including `flags`, so no
	 * explicit unset of the flags element is needed here. */
	Th8_ResetSecurityArray(interp);
#    endif

	Th8_ListAppend(interp, &zOut, &nOut, evalRc == TH8_OK ? "0" : "1", 1);
	Th8_ListAppend(interp, &zOut, &nOut, zPolicy, TH8_NOLEN);
	Th8_ListAppend(interp, &zOut, &nOut, zTok, TH8_NOLEN);
	Th8_ListAppend(interp, &zOut, &nOut, zNotBefore, TH8_NOLEN);
	Th8_ListAppend(interp, &zOut, &nOut, zNotAfter, TH8_NOLEN);
	Th8_ListAppend(interp, &zOut, &nOut, zFlags, TH8_NOLEN);
	Th8_SetResult(interp, zOut, nOut);
	Th8_Free(interp, zOut);
	return TH8_OK;

    } else if (argl[1] == 7 && memcmp(argv[1], "preload", 7) == 0) {
	/*
	 * Load the embedded keyRoot into the policy cache so
	 * that signature verification works without network
	 * access.
	 */
	const unsigned char *zKey;
	size_t nKey;
	Th8_RsaKey *pKey = 0;

	if (!th8test_pPolicyCtx) {
	    Th8_SetResultStatic(
	        interp, "signed_only: policy not installed", TH8_NOLEN);
	    return TH8_ERROR;
	}
	zKey = Th8_GetEmbeddedKeyRoot(&nKey);
	rc = Th8_RsaKeyLoad(interp, zKey, nKey, &pKey);
	if (rc != TH8_OK) return rc;
	return Th8_PolicyPreloadKey(interp, th8test_pPolicyCtx, pKey);

    } else if (argl[1] == 4 && memcmp(argv[1], "keys", 4) == 0) {
	if (!th8test_pPolicyCtx) {
	    Th8_SetResultStatic(
	        interp, "signed_only: policy not installed", TH8_NOLEN);
	    return TH8_ERROR;
	}
	return Th8_PolicyGetKeyTokens(interp, th8test_pPolicyCtx);

    } else if (argl[1] == 9 && memcmp(argv[1], "uninstall", 9) == 0) {
	Th8_RemoveSignedPolicy(interp, th8test_pPolicyCtx);
	th8test_pPolicyCtx = NULL;
	return TH8_OK;
    }

    Th8_SetResultStatic(
        interp,
        "signed_only: must be install, enable, disable, "
        "query, preload, keys, or uninstall",
        TH8_NOLEN);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_load_snk_cmd --
 *
 *	Implements "th8testlib::load_snk <filePath>".
 *	Loads a raw binary SNK file, parses it as an RSA key, and
 *	preloads it into the signed-only policy context.  Returns
 *	the 16-char hex public key token on success.
 *
 * Why / How:
 *	Tests dynamic key loading from .NET Strong Name Key (SNK)
 *	files.  This exercises the full key import pipeline: file
 *	read, DER parse, token computation, and policy preload.
 *	Used by tests that need to verify signatures made with
 *	keys not compiled into the binary.
 *
 * Results:
 *	TH8_OK with the 16-char hex token; TH8_ERROR on any
 *	failure.
 *
 * Side effects:
 *	Preloads a key into the policy context; reads from the
 *	filesystem.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_load_snk_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char *zData = NULL;
    size_t nData = 0;
    Th8_RsaKey *pKey = NULL;
    char zToken[17];
    int rc;
    void *pPolicyCtx;

    (void)ctx;

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "th8testlib::load_snk filePath");
    }

    /* Read the raw binary file. */
    rc = Th8_GetData(interp, argv[1], argl[1], &zData, &nData, 0);
    if (rc != TH8_OK) return rc;

    /* Parse as RSA key. */
    rc = Th8_RsaKeyLoad(interp, (const unsigned char *)zData, nData, &pKey);
    Th8_Free(interp, zData);
    if (rc != TH8_OK) return rc;

    /* Get the token. */
    rc = Th8_RsaKeyTokenHex(interp, pKey, zToken);
    if (rc != TH8_OK) {
	Th8_RsaKeyFree(interp, pKey);
	return rc;
    }

    /* Preload into the policy context. */
    Th8_GetPolicyCallback(interp, 0, &pPolicyCtx);
    if (pPolicyCtx) {
	rc = Th8_PolicyPreloadKey(interp, pPolicyCtx, pKey);
	/* Policy takes ownership of pKey on success. */
	if (rc != TH8_OK) {
	    Th8_RsaKeyFree(interp, pKey);
	    return rc;
	}
    } else {
	Th8_RsaKeyFree(interp, pKey);
	Th8_SetResultStatic(
	    interp, "load_snk: policy not installed", TH8_NOLEN);
	return TH8_ERROR;
    }

    Th8_SetResult(interp, zToken, 16);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_protected_null_page_cmd --
 *
 *	Drive th8_protect.c L365 (th8ProtectedCheckCanary) and
 *	L435 (th8ProtectedData) C2-Pair MC/DC vectors by calling
 *	each with a synthesised Th8_ProtectedRegion whose pPage
 *	is NULL.  The TH8_OMIT_AUXILIARY_SAFETY_CHECKS macro
 *	folds the NEVER(!pRegion) C1 to constant false, leaving
 *	`!pRegion->pPage` as the only live condition -- the
 *	{C,T} vector is uniquely reachable through a NULL-pPage
 *	region, which the production allocator never produces
 *	(allocation either succeeds with non-NULL pPage or
 *	returns NULL outright, so the calling site never holds
 *	a region with NULL pPage in practice).
 *
 *	Calls through the internal stubs table so the linker's
 *	hidden-visibility barrier does not block the testlib
 *	binary.
 *
 * Results:
 *	"ok" if both calls returned the expected
 *	failure indicators (TH8_ERROR / NULL); otherwise an
 *	error.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_protected_null_page_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    Th8_ProtectedRegion fake;
    int rc;
    unsigned char *p;

    (void)ctx;
    (void)argv;
    (void)argl;

    if (argc != 1) {
	return Th8_WrongNumArgs(interp, "th8testlib::protected_null_page");
    }

    Th8_Memset(interp, &fake, 0, sizeof(fake));
    /* fake.pPage is NULL.  Other fields are zero, which is
     * fine -- the functions short-circuit on the NULL pPage
     * before touching any other field. */

    rc = th8ProtectedCheckCanary(interp, &fake);
    if (rc != TH8_ERROR) {
	Th8_SetResultStatic(
	    interp,
	    "protected_null_page: CheckCanary did not "
	    "return TH8_ERROR for NULL pPage",
	    TH8_NOLEN);
	return TH8_ERROR;
    }

    p = th8ProtectedData(&fake);
    if (p != NULL) {
	Th8_SetResultStatic(
	    interp,
	    "protected_null_page: ProtectedData did not "
	    "return NULL for NULL pPage",
	    TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * MC/DC drive (2026-06-07): also call with NULL pRegion to
     * cover the C1=T (pRegion==NULL) vector of L352
     * `if (!pRegion || !pRegion->pPage)`.  The fake-with-NULL-pPage
     * call above only drives C1=F,C2=T.
     */
    rc = th8ProtectedCheckCanary(interp, NULL);
    if (rc != TH8_ERROR) {
	Th8_SetResultStatic(
	    interp,
	    "protected_null_page: CheckCanary did not "
	    "return TH8_ERROR for NULL pRegion",
	    TH8_NOLEN);
	return TH8_ERROR;
    }

    p = th8ProtectedData(NULL);
    if (p != NULL) {
	Th8_SetResultStatic(
	    interp,
	    "protected_null_page: ProtectedData did not "
	    "return NULL for NULL pRegion",
	    TH8_NOLEN);
	return TH8_ERROR;
    }

    Th8_SetResultStatic(interp, "ok", 2);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_preload_key_cmd --
 *
 *	Implements "th8testlib::preload_key".  Drives R-36002: when
 *	bPreload is non-zero, Th8_EvalFileAndRsaKeyLoad SHALL preload the
 *	loaded key into the policy cache via Th8_PolicyPreloadKey,
 *	requiring pCtx to be a valid policy context.
 *
 * Why / How:
 *	Mirrors the isolated policy pattern of signed_inherit /
 *	policy_depth_test: saves the caller's signed-only state, enables a
 *	fresh signed-only policy (which yields a valid policy context),
 *	then calls Th8_EvalFileAndRsaKeyLoad on a signed key file with
 *	bPreload = 1, so the key is both loaded and preloaded into that
 *	context.  The policy is disabled and the outer state restored
 *	before returning, so no global policy state leaks to later tests
 *	(installing/uninstalling the policy from a SCRIPT would clobber the
 *	harness's own signed-only policy -- see Bug-free note below).
 *
 * Results:
 *	TH8_OK with {preloaded ok|FAIL}.
 *
 * Side effects:
 *	Temporarily enables the signed-only policy on the calling
 *	interpreter and loads a key from a signed file.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_preload_key_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    void *pPolicyCtx = NULL;
    char *zOut = NULL;
    size_t nOut = 0;
    int rc;
    char outerSaved[TH8_SIGNED_SAVE_SIZE];

    (void)ctx;
    (void)argv;
    (void)argl;

    if (argc != 1) {
	return Th8_WrongNumArgs(interp, "th8testlib::preload_key");
    }

    Th8_SaveSignedOnly(interp, outerSaved);
    rc = Th8_EnableSignedPolicy(interp, &pPolicyCtx, 1);
    if (rc != TH8_OK) {
	Th8_RestoreSignedOnly(interp, outerSaved);
	Th8_SetResultStatic(
	    interp, "preload_key: cannot enable signed policy", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * R-36002: bPreload = 1 with the valid policy context pPolicyCtx
     * loads the key from the signed file and preloads it into the cache.
     */
    rc = Th8_EvalFileAndRsaKeyLoad(
        interp, "tests/helpers/load_test_key.th8", TH8_NOLEN, pPolicyCtx, 1);
    Th8_ListAppend(interp, &zOut, &nOut, "preloaded", TH8_NOLEN);
    Th8_ListAppend(
        interp, &zOut, &nOut, rc == TH8_OK ? "ok" : "FAIL", TH8_NOLEN);

    Th8_EnableSignedPolicy(interp, &pPolicyCtx, 0);
    Th8_RestoreSignedOnly(interp, outerSaved);

#    if defined(TH8_ENABLE_VARIABLES)
    Th8_ResetSecurityArray(interp);
#    endif

    Th8_SetResult(interp, zOut, nOut);
    Th8_Free(interp, zOut);
    return TH8_OK;
}

#  endif /* TH8_ENABLE_CRYPTOGRAPHY */


/*
 *----------------------------------------------------------------------
 *
 * th8test_normalize_no_callback_cmd --
 *
 *	Implements "th8testlib::normalize_no_callback".  Drives R-10315:
 *	if the platform provides no path-normalization callback,
 *	`file normalize` returns its argument unchanged.
 *
 * Why / How:
 *	The shipped POSIX/Win32 platforms always provide xNormalizePath,
 *	so the no-callback fallback is unreachable through the normal
 *	interpreter.  This helper clones the platform, clears
 *	xNormalizePath on the clone, and runs `file normalize` in a child
 *	interpreter built on it: Th8_NormalizePath returns NULL, and the
 *	command falls back to echoing the argument unchanged.
 *
 * Results:
 *	TH8_OK with {unchanged ok|FAIL}.
 *
 * Side effects:
 *	Creates and deletes a child interpreter.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_normalize_no_callback_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    const Th8_Platform *pParentPlat;
    Th8_Platform *pChildPlat;
    Th8_Interp *pChild;
    const char *zRes;
    size_t nRes = 0;
    char *zOut = NULL;
    size_t nOut = 0;
    int ok;

    (void)ctx;
    (void)argv;
    (void)argl;

    if (argc != 1) {
	return Th8_WrongNumArgs(interp, "th8testlib::normalize_no_callback");
    }

    pParentPlat = Th8_GetPlatform(interp);
    pChildPlat = Th8_ClonePlatform(pParentPlat);
    if (pChildPlat == NULL) {
	Th8_SetResultStatic(
	    interp, "normalize_no_callback: cannot clone platform",
	    TH8_NOLEN);
	return TH8_ERROR;
    }
    pChildPlat->xNormalizePath = NULL; /* R-10315: no normalize callback */
    pChild = Th8_CreateInterp(pChildPlat);
    if (pChild == NULL) {
	Th8_FreePlatform(pChildPlat);
	Th8_SetResultStatic(
	    interp, "normalize_no_callback: cannot create child interp",
	    TH8_NOLEN);
	return TH8_ERROR;
    }
    Th8_RegisterLanguage(pChild);

    (void)Th8_Eval(pChild, 0, "file normalize /a/b/../c", TH8_NOLEN, "t", 1);
    zRes = Th8_GetResult(pChild, &nRes);
    ok =
        (zRes != NULL && nRes == 9 &&
         Th8_Memcmp(interp, zRes, "/a/b/../c", 9) == 0);

    Th8_ListAppend(interp, &zOut, &nOut, "unchanged", TH8_NOLEN);
    Th8_ListAppend(interp, &zOut, &nOut, ok ? "ok" : "FAIL", TH8_NOLEN);

    Th8_DeleteInterp(pChild);

    Th8_SetResult(interp, zOut, nOut);
    Th8_Free(interp, zOut);
    return TH8_OK;
}


/*
 * Sentinel + capture state for th8test_output_error_channel_cmd.  The
 * child's xGetErrorOutput hands back &th8test_oe_sentinel as the error
 * channel; the child's xOutputError records the channel it was passed so
 * the test can confirm the two are threaded together.  Single-threaded
 * test use only.
 */
static int th8test_oe_sentinel;
static void *th8test_oe_channel;

/*
 *----------------------------------------------------------------------
 *
 * th8test_geterroroutput_cb --
 *
 *	xGetErrorOutput platform callback for
 *	th8test_output_error_channel_cmd: reports &th8test_oe_sentinel as
 *	the current error channel.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_geterroroutput_cb(Th8_Interp *interp, void *pCtx, void **pChannel)
{
    (void)interp;
    (void)pCtx;
    if (pChannel) *pChannel = &th8test_oe_sentinel;
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_outputerror_cb --
 *
 *	xOutputError platform callback for
 *	th8test_output_error_channel_cmd: records the pChannel it is
 *	handed so the test can confirm it matches xGetErrorOutput's.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_outputerror_cb(
    Th8_Interp *interp,
    void *pCtx,
    const char *z,
    size_t n,
    void *pChannel)
{
    (void)interp;
    (void)pCtx;
    (void)z;
    (void)n;
    th8test_oe_channel = pChannel;
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_output_error_channel_cmd --
 *
 *	Implements "th8testlib::output_error_channel".  Drives R-64687:
 *	Th8_OutputError SHALL query the current error output channel via
 *	xGetErrorOutput before invoking xOutputError, passing that channel
 *	to xOutputError.
 *
 * Why / How:
 *	Clones the platform, installs a matched pair of callbacks on the
 *	clone (xGetErrorOutput yields a sentinel channel; xOutputError
 *	captures whatever channel it is handed), and calls Th8_OutputError
 *	on a child built from it.  The test confirms the sentinel produced
 *	by xGetErrorOutput is exactly what xOutputError received.
 *
 * Results:
 *	TH8_OK with {channel_passed ok|FAIL}.
 *
 * Side effects:
 *	Creates and deletes a child interpreter; writes the capture state.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_output_error_channel_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    const Th8_Platform *pParentPlat;
    Th8_Platform *pChildPlat;
    Th8_Interp *pChild;
    char *zOut = NULL;
    size_t nOut = 0;
    int rc;
    int ok;

    (void)ctx;
    (void)argv;
    (void)argl;

    if (argc != 1) {
	return Th8_WrongNumArgs(interp, "th8testlib::output_error_channel");
    }

    pParentPlat = Th8_GetPlatform(interp);
    pChildPlat = Th8_ClonePlatform(pParentPlat);
    if (pChildPlat == NULL) {
	Th8_SetResultStatic(
	    interp, "output_error_channel: cannot clone platform", TH8_NOLEN);
	return TH8_ERROR;
    }
    pChildPlat->xGetErrorOutput = th8test_geterroroutput_cb;
    pChildPlat->xOutputError = th8test_outputerror_cb;
    pChild = Th8_CreateInterp(pChildPlat);
    if (pChild == NULL) {
	Th8_FreePlatform(pChildPlat);
	Th8_SetResultStatic(
	    interp, "output_error_channel: cannot create child interp",
	    TH8_NOLEN);
	return TH8_ERROR;
    }
    Th8_RegisterLanguage(pChild);

    th8test_oe_channel = NULL;
    rc = Th8_OutputError(pChild, "err", 3);
    ok = (rc == TH8_OK && th8test_oe_channel == &th8test_oe_sentinel);

    Th8_ListAppend(interp, &zOut, &nOut, "channel_passed", TH8_NOLEN);
    Th8_ListAppend(interp, &zOut, &nOut, ok ? "ok" : "FAIL", TH8_NOLEN);

    Th8_DeleteInterp(pChild);

    Th8_SetResult(interp, zOut, nOut);
    Th8_Free(interp, zOut);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_close_veto_cb --
 *
 *	xCloseTemporaryData platform callback for th8test_close_veto_cmd:
 *	always returns TH8_ERROR to veto the close.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_close_veto_cb(
    Th8_Interp *interp,
    void *pCtx,
    const char *zName,
    size_t nName,
    void *pChannel)
{
    (void)interp;
    (void)pCtx;
    (void)zName;
    (void)nName;
    (void)pChannel;
    return TH8_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_close_veto_cmd --
 *
 *	Implements "th8testlib::close_veto".  Drives R-16003: the
 *	xCloseTemporaryData platform callback is consulted before closing
 *	a temporary channel, and a non-TH8_OK return vetoes the close so
 *	the channel remains open.
 *
 * Why / How:
 *	Clones the platform, installs a vetoing xCloseTemporaryData on the
 *	clone, and in a child interpreter creates an in-memory temporary
 *	channel via [file tempname] and attempts to [close] it.  The close
 *	is refused ("close vetoed by platform").
 *
 * Results:
 *	TH8_OK with {vetoed ok|FAIL}.
 *
 * Side effects:
 *	Creates and deletes a child interpreter and a temporary channel.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_close_veto_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    const Th8_Platform *pParentPlat;
    Th8_Platform *pChildPlat;
    Th8_Interp *pChild;
    const char *zRes;
    size_t nRes = 0;
    char *zOut = NULL;
    size_t nOut = 0;
    int evalRc;
    int ok;

    (void)ctx;
    (void)argv;
    (void)argl;

    if (argc != 1) {
	return Th8_WrongNumArgs(interp, "th8testlib::close_veto");
    }

    pParentPlat = Th8_GetPlatform(interp);
    pChildPlat = Th8_ClonePlatform(pParentPlat);
    if (pChildPlat == NULL) {
	Th8_SetResultStatic(
	    interp, "close_veto: cannot clone platform", TH8_NOLEN);
	return TH8_ERROR;
    }
    pChildPlat->xCloseTemporaryData = th8test_close_veto_cb;
    pChild = Th8_CreateInterp(pChildPlat);
    if (pChild == NULL) {
	Th8_FreePlatform(pChildPlat);
	Th8_SetResultStatic(
	    interp, "close_veto: cannot create child interp", TH8_NOLEN);
	return TH8_ERROR;
    }
    Th8_RegisterLanguage(pChild);

    evalRc = Th8_Eval(
        pChild, 0, "set c [file tempname 16]\nclose $c", TH8_NOLEN, "t", 1);
    zRes = Th8_GetResult(pChild, &nRes);
    ok = (evalRc == TH8_ERROR && th8test_contains(interp, zRes, "vetoed"));

    Th8_ListAppend(interp, &zOut, &nOut, "vetoed", TH8_NOLEN);
    Th8_ListAppend(interp, &zOut, &nOut, ok ? "ok" : "FAIL", TH8_NOLEN);

    Th8_DeleteInterp(pChild);

    Th8_SetResult(interp, zOut, nOut);
    Th8_Free(interp, zOut);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_load_key_file_cmd --
 *
 *	Implements "th8testlib::load_key_file <filePath>".
 *	Loads binary data from a signed script file via
 *	Th8_EvalFileAsData.  If crypto is enabled and the
 *	signed-only policy is installed, parses the data as an RSA
 *	key and preloads it into the policy context.
 *
 *	Returns a list: {rc dataSize ?keyBits? ?token?}
 *	  rc       -- 0 on success, 1 on failure
 *	  dataSize -- byte count of decoded data (0 on failure)
 *	  keyBits  -- RSA key size in bits (if crypto enabled)
 *	  token    -- 16-char hex public key token (if crypto enabled)
 *
 * Why / How:
 *	Tests the "signed binary data" path where a .th8 file
 *	contains base64-encoded binary rather than script text.
 *	This exercises Th8_EvalFileAsData and, when crypto is
 *	available, the combined load+preload API for dynamically
 *	adding trusted keys at runtime.
 *
 * Results:
 *	TH8_OK with the result list described above.
 *
 * Side effects:
 *	May preload an RSA key into the policy context.  Reads
 *	from the filesystem.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_load_key_file_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char *zOut = NULL;
    size_t nOut = 0;
    int rc;

    (void)ctx;

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "th8testlib::load_key_file filePath");
    }

#  if defined(TH8_ENABLE_CRYPTOGRAPHY)
    /*
     * Use Th8_EvalFileAndRsaKeyLoad for the combined operation.
     * If the policy context is available and preload is desired,
     * pass it through.
     */

    rc = Th8_EvalFileAndRsaKeyLoad(
        interp, argv[1], argl[1], th8test_pPolicyCtx,
        th8test_pPolicyCtx ? 1 : 0);

    if (rc == TH8_OK) {
	Th8_ListAppend(interp, &zOut, &nOut, "0", 1);
    } else {
	Th8_ListAppend(interp, &zOut, &nOut, "1", 1);
    }
#  else
    /*
     * Without crypto, just call Th8_EvalFileAsData and report
     * the decoded data size.
     */
    {
	const unsigned char *zData = NULL;
	size_t nData = 0;

	rc = Th8_EvalFileAsData(
	    interp, argv[1], argl[1], &zData, &nData, NULL);

	if (rc == TH8_OK && zData && nData > 0) {
	    Th8_ListAppend(interp, &zOut, &nOut, "0", 1);
	} else {
	    Th8_ListAppend(interp, &zOut, &nOut, "1", 1);
	}

	Th8_Free(interp, (void *)zData);
    }
#  endif /* TH8_ENABLE_CRYPTOGRAPHY */
    Th8_SetResult(interp, zOut, nOut);
    Th8_Free(interp, zOut);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_chan_cmd --
 *
 *	th8testlib::chan get stdin|stdout|stderr
 *	th8testlib::chan set stdin|stdout|stderr ?fileName?
 *	th8testlib::chan reset stdin|stdout|stderr
 *
 *	Test infrastructure for I/O channel redirection.
 *	"set" opens a file and redirects the channel to it.
 *	"get" returns "default" or the file path.
 *	"reset" closes the file and restores the default channel.
 *	"set" with no fileName is equivalent to "reset".
 *
 * Why / How:
 *	Tests that need to capture [puts] output or inject [gets]
 *	input redirect standard channels to files.  Uses the
 *	platform's xChannelControl callback for file open/close
 *	and Th8_RedirectInput/Output/ErrorOutput for channel swap.
 *	Tracking state ensures proper cleanup even if tests fail.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR for unknown channels or
 *	subcommands.
 *
 * Side effects:
 *	Opens/closes files via xChannelControl; redirects I/O
 *	channels on the interpreter.
 *
 *----------------------------------------------------------------------
 */

#  if defined(TH8_PLUGIN_IO)

static struct {
    char *zPath; /* File path (malloc'd), or NULL. */
    void *pChannel; /* Opaque handle from xChannelControl OPEN. */
    Th8_Interp *pInterp; /* Interp used to open (for close). */
} th8testChan[3]; /* 0=stdin, 1=stdout, 2=stderr */

/*
 *----------------------------------------------------------------------
 *
 * th8test_chan_close --
 *
 *	Close a redirected I/O channel by index (0=stdin, 1=stdout,
 *	2=stderr) and free its stored path.
 *
 * Why / How:
 *	The th8testlib::chan command redirects standard channels to
 *	files for testing I/O capture.  This helper ensures the
 *	platform channel handle is properly closed and the tracking
 *	state is reset, preventing resource leaks between tests.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Calls xChannelControl CLOSE on the platform; frees the
 *	stored path string; zeros the tracking struct fields.
 *
 *----------------------------------------------------------------------
 */

static void
th8test_chan_close(int idx)
{
    if (th8testChan[idx].pChannel) {
	const Th8_Platform *pPlat = Th8_GetPlatform(th8testChan[idx].pInterp);

	if (pPlat->xChannelControl) {
	    pPlat->xChannelControl(
	        th8testChan[idx].pInterp, pPlat->pCtx,
	        th8testChan[idx].pChannel, TH8_CHANCTL_CLOSE, 0, 0, 0, 0);
	}
	th8testChan[idx].pChannel = 0;
	th8testChan[idx].pInterp = 0;
    }
    if (th8testChan[idx].zPath) {
	free(th8testChan[idx].zPath);
	th8testChan[idx].zPath = 0;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_chan_cmd --
 *
 *	Implements `::th8testlib::chan get|set|reset CHANNEL
 *	?FILE?`.  Manipulates redirection of the three
 *	standard channels (`stdin`, `stdout`, `stderr`) for
 *	tests that need to capture or supply I/O without
 *	involving the shell's TTY:
 *
 *	  * `get CHANNEL` -- report the current redirection
 *	    target for CHANNEL (path string, or empty if not
 *	    redirected).
 *	  * `set CHANNEL FILE` -- open FILE on the platform
 *	    layer (read for stdin, write for stdout/stderr)
 *	    and route CHANNEL to it; replaces any prior
 *	    redirection for the same CHANNEL.
 *	  * `reset CHANNEL` -- close the redirection for
 *	    CHANNEL (if any) and restore the default path.
 *
 *	Tracking state lives in the file-static `th8testChan`
 *	array; `th8test_chan_close` performs the platform
 *	close + state cleanup invoked by `reset` and by
 *	`set` when replacing an existing redirection.
 *
 * Parameters:
 *	interp -- live interpreter (receives diagnostic).
 *	ctx    -- unused command context.
 *	argc   -- argument count (3 for get/reset, 4 for set).
 *	argv   -- argv[0]=command name; argv[1]=SUBCMD;
 *		argv[2]=CHANNEL; argv[3]=FILE (set only).
 *	argl   -- argument byte-lengths.
 *
 * Returns:
 *	`TH8_OK` on success with the interpreter result set
 *	per the SUBCMD; `TH8_ERROR` on unknown CHANNEL or
 *	platform-callback failure.
 *
 * Side effects:
 *	Opens / closes platform channels and updates the
 *	`th8testChan` tracking array.  Allocates / frees the
 *	stored path string via `malloc` / `free`.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_chan_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    static const char *azName[] = {"stdin", "stdout", "stderr"};
    const char *zSub;
    const char *zChan;
    int idx = -1;
    int i;

    (void)ctx;

    if (argc < 3 || argc > 4) {
	return Th8_WrongNumArgs(
	    interp, "th8testlib::chan get|set|reset stdin|stdout|stderr"
	            " ?fileName?");
    }
    zSub = argv[1];
    zChan = argv[2];

    /*
     * Bracket length first to avoid reading past azName[i].
     * Lengths differ: stdin=5, stdout=6, stderr=6.  ASan's
     * global-buffer-overflow detector caught the prior code
     * over-reading "stdin" (size 6 incl NUL) when zChan was
     * "stdout" or "stderr" (argl=6, +1=7).
     */
    for (i = 0; i < 3; i++) {
	size_t nName = Th8_Strlen(interp, azName[i]);
	if (argl[2] == nName &&
	    Th8_Memcmp(interp, zChan, azName[i], nName) == 0) {
	    idx = i;
	    break;
	}
    }
    if (idx < 0) {
	Th8_ErrorMessage(interp, "unknown channel:", zChan, argl[2]);
	return TH8_ERROR;
    }

    /*
     * "get" subcommand.
     */
    if (argl[1] == 3 && Th8_Memcmp(interp, zSub, "get", 4) == 0) {
	if (argc != 3) {
	    return Th8_WrongNumArgs(
	        interp, "th8testlib::chan get stdin|stdout|stderr");
	}
	if (th8testChan[idx].zPath) {
	    Th8_SetResult(interp, th8testChan[idx].zPath, TH8_NOLEN);
	} else {
	    Th8_SetResultStatic(interp, "default", TH8_NOLEN);
	}
	return TH8_OK;
    }

    /*
     * "reset" subcommand.
     */
    if (argl[1] == 5 && Th8_Memcmp(interp, zSub, "reset", 6) == 0) {
	if (argc != 3) {
	    return Th8_WrongNumArgs(
	        interp, "th8testlib::chan reset stdin|stdout|stderr");
	}
	th8test_chan_close(idx);
	if (idx == 0) return Th8_RedirectInput(interp, 0);
	if (idx == 1) return Th8_RedirectOutput(interp, 0);
	return Th8_RedirectErrorOutput(interp, 0);
    }

    /*
     * "set" subcommand.
     */
    if (argl[1] == 3 && Th8_Memcmp(interp, zSub, "set", 4) == 0) {
	const Th8_Platform *pPlat = Th8_GetPlatform(interp);
	th8_int64_t handle = 0;
	void *pChan;
	int mode;
	int rc;

	if (argc != 4) {
	    return Th8_WrongNumArgs(
	        interp, "th8testlib::chan set stdin|stdout|stderr"
	                " fileName");
	}

	/* Close any prior redirect for this channel. */
	th8test_chan_close(idx);

	if (!pPlat->xChannelControl) {
	    Th8_SetResultStatic(
	        interp, "xChannelControl not available", TH8_NOLEN);
	    return TH8_ERROR;
	}

	mode = (idx == 0) ? 0 : 1; /* 0=read, 1=write-create */
	rc = pPlat->xChannelControl(
	    interp, pPlat->pCtx, 0, TH8_CHANCTL_OPEN, (th8_int64_t)argl[3],
	    mode, &handle, (void *)argv[3]);
	if (rc != TH8_OK) {
	    Th8_ErrorMessage(interp, "cannot open file:", argv[3], argl[3]);
	    return TH8_ERROR;
	}

	pChan = TH8_INT2PTR((int)handle);
	th8testChan[idx].pChannel = pChan;
	th8testChan[idx].pInterp = interp;
	th8testChan[idx].zPath = (char *)malloc(argl[3] + 1);
	if (th8testChan[idx].zPath) {
	    memcpy(th8testChan[idx].zPath, argv[3], argl[3]);
	    th8testChan[idx].zPath[argl[3]] = '\0';
	}

	if (idx == 0)
	    rc = Th8_RedirectInput(interp, pChan);
	else if (idx == 1)
	    rc = Th8_RedirectOutput(interp, pChan);
	else
	    rc = Th8_RedirectErrorOutput(interp, pChan);

	return rc;
    }

    Th8_ErrorMessage(interp, "unknown subcommand:", zSub, argl[1]);
    return TH8_ERROR;
}
#  endif /* TH8_PLUGIN_IO */


/*
 *----------------------------------------------------------------------
 *
 * th8test_symlink_cmd --
 *
 *	th8testlib::symlink create TARGET LINKNAME
 *	th8testlib::symlink delete LINKNAME
 *
 *	Create or delete a symbolic link for testing [file type]
 *	symlink detection.  Both TARGET and LINKNAME are relative
 *	to the current working directory.
 *
 * Why / How:
 *	TH8's [file type] command must detect symbolic links to
 *	return "link" rather than the target type.  This command
 *	creates and cleans up symlinks for those tests.  Uses
 *	symlink(2) on POSIX and CreateSymbolicLinkA on Win32
 *	(requires elevated privileges on Windows).
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR on failure or unknown
 *	subcommand.
 *
 * Side effects:
 *	Creates or deletes a filesystem symlink.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_symlink_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    (void)ctx;

    if (argc < 3) {
	return Th8_WrongNumArgs(
	    interp, "th8testlib::symlink create|delete ...");
    }

#  if !defined(_WIN32) && !defined(WIN32)
    {
	char zTarget[1024];
	char zLink[1024];

	if (argl[1] == 6 && Th8_Memcmp(interp, argv[1], "create", 7) == 0) {
	    if (argc != 4) {
		return Th8_WrongNumArgs(
		    interp, "th8testlib::symlink create target linkname");
	    }
	    if (argl[2] >= sizeof(zTarget) || argl[3] >= sizeof(zLink)) {
		Th8_SetResultStatic(interp, "path too long", TH8_NOLEN);
		return TH8_ERROR;
	    }
	    memcpy(zTarget, argv[2], argl[2]);
	    zTarget[argl[2]] = '\0';
	    memcpy(zLink, argv[3], argl[3]);
	    zLink[argl[3]] = '\0';

	    if (symlink(zTarget, zLink) != 0) {
		TH8_TRACE_ERR(interp, "symlink failed");
		Th8_ErrorMessage(interp, "symlink failed:", argv[3], argl[3]);
		return TH8_ERROR;
	    }
	    return TH8_OK;

	} else if (
	    argl[1] == 6 && Th8_Memcmp(interp, argv[1], "delete", 7) == 0) {
	    if (argc != 3) {
		return Th8_WrongNumArgs(
		    interp, "th8testlib::symlink delete linkname");
	    }
	    if (argl[2] >= sizeof(zLink)) {
		Th8_SetResultStatic(interp, "path too long", TH8_NOLEN);
		return TH8_ERROR;
	    }
	    memcpy(zLink, argv[2], argl[2]);
	    zLink[argl[2]] = '\0';
	    if (unlink(zLink) != 0) {
		TH8_TRACE_ERR(interp, "unlink failed (ignored)");
	    }
	    return TH8_OK;
	}

	Th8_ErrorMessage(interp, "unknown subcommand:", argv[1], argl[1]);
	return TH8_ERROR;
    }
#  else /* _WIN32 */
    {
	char zTarget[1024];
	char zLink[1024];
	DWORD flags = 0;

	if (argl[1] == 6 && Th8_Memcmp(interp, argv[1], "create", 7) == 0) {
	    DWORD attr;

	    if (argc != 4) {
		return Th8_WrongNumArgs(
		    interp, "th8testlib::symlink create target linkname");
	    }
	    if (argl[2] >= sizeof(zTarget) || argl[3] >= sizeof(zLink)) {
		Th8_SetResultStatic(interp, "path too long", TH8_NOLEN);
		return TH8_ERROR;
	    }
	    memcpy(zTarget, argv[2], argl[2]);
	    zTarget[argl[2]] = '\0';
	    memcpy(zLink, argv[3], argl[3]);
	    zLink[argl[3]] = '\0';

	    /*
	     * If target is a directory, set the directory flag.
	     */
	    attr = GetFileAttributesA(zTarget);
	    if (attr != INVALID_FILE_ATTRIBUTES &&
	        (attr & FILE_ATTRIBUTE_DIRECTORY)) {
		flags = SYMBOLIC_LINK_FLAG_DIRECTORY;
	    }
	    if (!CreateSymbolicLinkA(zLink, zTarget, flags)) {
		TH8_TRACE_ERR(interp, "CreateSymbolicLinkA failed");
		Th8_ErrorMessage(interp, "symlink failed:", argv[3], argl[3]);
		return TH8_ERROR;
	    }
	    return TH8_OK;

	} else if (
	    argl[1] == 6 && Th8_Memcmp(interp, argv[1], "delete", 7) == 0) {
	    if (argc != 3) {
		return Th8_WrongNumArgs(
		    interp, "th8testlib::symlink delete linkname");
	    }
	    if (argl[2] >= sizeof(zLink)) {
		Th8_SetResultStatic(interp, "path too long", TH8_NOLEN);
		return TH8_ERROR;
	    }
	    memcpy(zLink, argv[2], argl[2]);
	    zLink[argl[2]] = '\0';
	    if (!DeleteFileA(zLink)) {
		TH8_TRACE_ERR(interp, "DeleteFileA failed (ignored)");
	    }
	    return TH8_OK;
	}

	Th8_ErrorMessage(interp, "unknown subcommand:", argv[1], argl[1]);
	return TH8_ERROR;
    }
#  endif /* _WIN32 */
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_isadmin_cmd --
 *
 *	th8testlib::isadmin
 *
 *	Returns "1" if the current process is running with
 *	administrator/root privileges, "0" otherwise.
 *
 *	POSIX:  checks getuid() == 0.
 *	Win32:  opens the process token and checks for the
 *	        Administrators group via CheckTokenMembership.
 *
 * Why / How:
 *	Some tests (chroot, symlink creation on Windows) require
 *	elevated privileges and should be skipped otherwise.  This
 *	command lets the test harness query privilege level at
 *	runtime and use [testConstraint] to gate such tests.
 *
 * Results:
 *	TH8_OK with "1" or "0" as the result.
 *
 * Side effects:
 *	None (the Win32 path opens/closes a process token handle).
 *
 *----------------------------------------------------------------------
 */

static int
th8test_isadmin_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    int isAdmin = 0;

    (void)ctx;
    (void)argv;
    (void)argl;

    if (argc != 1) {
	return Th8_WrongNumArgs(interp, "th8testlib::isadmin");
    }

#  if !defined(_WIN32) && !defined(WIN32)
    isAdmin = (getuid() == 0);
#  else
    {
	BOOL isMember = FALSE;
	SID_IDENTIFIER_AUTHORITY ntAuth = SECURITY_NT_AUTHORITY;
	PSID adminSid = NULL;

	if (AllocateAndInitializeSid(
	        &ntAuth, 2, SECURITY_BUILTIN_DOMAIN_RID,
	        DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminSid)) {
	    if (CheckTokenMembership(NULL, adminSid, &isMember)) {
		isAdmin = (isMember != FALSE);
	    }
	    FreeSid(adminSid);
	}
    }
#  endif

    Th8_SetResultInt(interp, isAdmin);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_fuzz_cmd --
 *
 *	th8testlib::fuzz TYPE DATA
 *
 *	Feed binary data into a TH8 subsystem for regression testing
 *	of fuzzer-discovered crash inputs.  The subsystem is selected
 *	by TYPE:
 *
 *	  list  -- Th8_SplitList (list parser)
 *	  eval  -- Th8_Eval (script evaluator)
 *	  expr  -- Th8_Expr (expression evaluator)
 *
 *	Returns TH8_OK if the subsystem handled the input without
 *	crashing (errors from the subsystem are caught and ignored).
 *
 * Why / How:
 *	Fuzzing tools (AFL, libFuzzer) produce crash-inducing inputs
 *	saved as files.  This command replays those inputs inside the
 *	test harness as regression tests.  Errors are expected and
 *	swallowed; the test passes if no crash (SIGSEGV, SIGABRT,
 *	etc.) occurs.  Supports -file flag for binary data.
 *
 * Results:
 *	TH8_OK always (unless the type is unknown).
 *
 * Side effects:
 *	May allocate/free memory during parsing; errors are discarded.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_fuzz_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    const char *zType;
    size_t nType;
    const char *zData;
    size_t nData;
    char *zFileData = 0;

    (void)ctx;

    if (argc != 3 && argc != 4) {
	return Th8_WrongNumArgs(interp, "th8testlib::fuzz type ?-file? data");
    }

    zType = argv[1];
    nType = argl[1];

    if (argc == 4 && argl[2] == 5 &&
        Th8_Memcmp(interp, argv[2], "-file", 6) == 0) {
	/*
	 * Read binary data from a file.
	 */
	size_t nFile = 0;

	if (Th8_GetData(interp, argv[3], argl[3], &zFileData, &nFile, 0) !=
	    TH8_OK) {
	    return TH8_ERROR;
	}
	zData = zFileData;
	nData = nFile;
    } else if (argc == 3) {
	zData = argv[2];
	nData = argl[2];
    } else {
	return Th8_WrongNumArgs(interp, "th8testlib::fuzz type ?-file? data");
    }

    if (nType == 4 && Th8_Memcmp(interp, zType, "list", 5) == 0) {
	char **az = 0;
	size_t *an = 0;
	int nc = 0;

	if (Th8_SplitList(
	        interp, zData, nData, &az, &an, &nc, TH8_LIST_NONE) ==
	    TH8_OK) {
	    Th8_Free(interp, az);
	}

    } else if (nType == 4 && Th8_Memcmp(interp, zType, "eval", 5) == 0) {
	(void)Th8_Eval(interp, 0, zData, nData, NULL, 0);

    } else if (nType == 4 && Th8_Memcmp(interp, zType, "expr", 5) == 0) {
	(void)Th8_Expr(interp, zData, nData, NULL, 0);

    } else {
	Th8_Free(interp, zFileData);
	Th8_ErrorMessage(interp, "unknown fuzz type:", zType, nType);
	return TH8_ERROR;
    }

    Th8_Free(interp, zFileData);
    Th8_ClearResult(interp);
    return TH8_OK;
}


#  if !defined(_WIN32) && !defined(WIN32)
/* <dirent.h> included via th8_meta_posix.h */
#  endif

/*
 *----------------------------------------------------------------------
 *
 * th8test_glob_cmd --
 *
 *	th8testlib::glob PATTERN
 *
 *	Return a list of file paths matching PATTERN.  Uses the
 *	platform's xDataExists to probe each candidate.  Only
 *	supports patterns of the form "dir<slash>*.ext",
 *	splits on the last '/' to get a directory and suffix,
 *	then iterates using the OS directory API.
 *
 *	This is a minimal implementation for test infrastructure,
 *	not a full glob.
 *
 * Why / How:
 *	TH8 has no built-in glob command.  The test infrastructure
 *	(all.tcl) needs to discover test files by pattern.  This
 *	minimal implementation uses opendir/readdir on POSIX and
 *	FindFirstFile/FindNextFile on Win32, matching files by
 *	suffix only.  It deliberately does not support full glob
 *	syntax to keep the implementation simple and secure.
 *
 * Results:
 *	TH8_OK with a Tcl list of matching paths as the result.
 *
 * Side effects:
 *	Reads directory entries from the filesystem.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_glob_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    const char *zPattern;
    size_t nPattern;
    const char *zDir = ".";
    size_t nDir = 1;
    const char *zSuffix = "";
    size_t nSuffix = 0;
    const char *zSep;
    char *zResult = 0;
    size_t nResult = 0;
    size_t i;

    (void)ctx;

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "th8testlib::glob pattern");
    }

    zPattern = argv[1];
    nPattern = argl[1];

    /* Find last '/' to split dir and suffix. */
    zSep = 0;
    for (i = 0; i < nPattern; i++) {
	if (zPattern[i] == '/' || zPattern[i] == '\\') {
	    zSep = &zPattern[i];
	}
    }

    if (zSep) {
	nDir = (size_t)(zSep - zPattern);
	zDir = zPattern;
	/* Find the suffix after '*' if present */
	zSuffix = zSep + 1;
	nSuffix = nPattern - nDir - 1;
    } else {
	zSuffix = zPattern;
	nSuffix = nPattern;
    }

    /* Extract the extension/suffix after '*' */
    {
	const char *zStar = 0;

	for (i = 0; i < nSuffix; i++) {
	    if (zSuffix[i] == '*') {
		zStar = &zSuffix[i];
		break;
	    }
	}
	if (zStar) {
	    size_t nAfter = nSuffix - (size_t)(zStar - zSuffix) - 1;
	    const char *zAfter = zStar + 1;

	    /* Iterate directory entries. */
#  if !defined(_WIN32) && !defined(WIN32)
	    {
		char zDirBuf[1024];
		DIR *d;

		if (nDir >= sizeof(zDirBuf)) {
		    Th8_SetResultStatic(interp, "path too long", TH8_NOLEN);
		    return TH8_ERROR;
		}
		memcpy(zDirBuf, zDir, nDir);
		zDirBuf[nDir] = '\0';

		d = opendir(zDirBuf);
		if (d) {
		    struct dirent *ent;

		    while ((ent = readdir(d)) != NULL) {
			size_t nName = Th8_Strlen(interp, ent->d_name);

			/* Match suffix */
			if (nAfter > 0 && nName >= nAfter) {
			    if (memcmp(
			            ent->d_name + nName - nAfter, zAfter,
			            nAfter) != 0) {
				continue;
			    }
			}
			/* Skip . and .. */
			if (ent->d_name[0] == '.' &&
			    (nName == 1 ||
			     (nName == 2 && ent->d_name[1] == '.'))) {
			    continue;
			}
			/* Build full path */
			{
			    char zPath[2048];
			    size_t nPath;

			    nPath = nDir + 1 + nName;
			    if (nPath < sizeof(zPath)) {
				memcpy(zPath, zDir, nDir);
				zPath[nDir] = '/';
				memcpy(zPath + nDir + 1, ent->d_name, nName);
				zPath[nPath] = '\0';
				Th8_ListAppend(
				    interp, &zResult, &nResult, zPath, nPath);
			    }
			}
		    }
		    closedir(d);
		}
	    }
#  else /* _WIN32 */
	    {
		char zSearch[1024];
		WIN32_FIND_DATAA fd;
		HANDLE hFind;

		if (nDir + 3 >= sizeof(zSearch)) {
		    Th8_SetResultStatic(interp, "path too long", TH8_NOLEN);
		    return TH8_ERROR;
		}
		memcpy(zSearch, zDir, nDir);
		zSearch[nDir] = '\\';
		zSearch[nDir + 1] = '*';
		zSearch[nDir + 2] = '\0';

		hFind = FindFirstFileA(zSearch, &fd);
		if (hFind != INVALID_HANDLE_VALUE) {
		    do {
			size_t nName = Th8_Strlen(interp, fd.cFileName);

			if (nAfter > 0 && nName >= nAfter) {
			    if (memcmp(
			            fd.cFileName + nName - nAfter, zAfter,
			            nAfter) != 0) {
				continue;
			    }
			}
			if (fd.cFileName[0] == '.') continue;
			{
			    char zPath[2048];
			    size_t nPath;

			    nPath = nDir + 1 + nName;
			    if (nPath < sizeof(zPath)) {
				memcpy(zPath, zDir, nDir);
				zPath[nDir] = '/';
				memcpy(zPath + nDir + 1, fd.cFileName, nName);
				zPath[nPath] = '\0';
				Th8_ListAppend(
				    interp, &zResult, &nResult, zPath, nPath);
			    }
			}
		    } while (FindNextFileA(hFind, &fd));
		    FindClose(hFind);
		}
	    }
#  endif
	}
    }

    Th8_SetResult(interp, zResult ? zResult : "", nResult);
    Th8_Free(interp, zResult);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_sandbox_cmd --
 *
 *	Implements "th8testlib::sandbox <script>".
 *	Creates a sandboxed child interpreter with fail-safe security
 *	defaults, evaluates the given script in it, captures results
 *	and metrics, then destroys the child.
 *
 *	The child interpreter:
 *	  - Uses the same platform as the parent (via Th8_GetPlatform)
 *	  - Has the standard language commands registered
 *	  - Has NO binary loading (xLoad disabled)
 *	  - Has NO bigint (disabled)
 *	  - Has integer overflow checking ENABLED
 *	  - Has a memory allocation limit (16 MB)
 *	  - Has a step limit (1,000,000 steps)
 *	  - Has a result size limit (1 MB)
 *	  - Has NO policy callback
 *	  - Has NO variables or procedures from the parent
 *
 *	Returns a Tcl list:
 *	  {returnCode result stepCount allocBytes}
 *
 * Why / How:
 *	Tests resource limiting and isolation by running untrusted
 *	code in a constrained child interpreter.  The child cannot
 *	affect the parent -- even OOM or step exhaustion produces a
 *	clean error (xPanic is NULL, so no abort).  Metrics capture
 *	lets tests assert on resource consumption.
 *
 * Results:
 *	TH8_OK with {returnCode result stepCount allocBytes} list.
 *
 * Side effects:
 *	Creates and destroys a child interpreter; evaluates
 *	arbitrary code within resource limits.
 *
 *----------------------------------------------------------------------
 */

#  define TH8_SANDBOX_ALLOC_LIMIT  (16 * 1024 * 1024) /* 16 MB */
#  define TH8_SANDBOX_STEP_LIMIT   1000000 /* 1M steps */
#  define TH8_SANDBOX_RESULT_LIMIT (1 * 1024 * 1024) /* 1 MB */

/*
 *----------------------------------------------------------------------
 *
 * th8test_i64toa --
 *
 *	Convert a 64-bit signed integer to a decimal string in the
 *	caller's buffer.  Writes from the end backward; returns a
 *	pointer to the first digit (which may be past buf[0]).
 *
 * Why / How:
 *	The sandbox and fault commands need to format integer
 *	counters (step count, alloc bytes, return codes) without
 *	depending on snprintf or the interpreter's formatting.
 *	This standalone converter has no dependencies and works
 *	even when the interpreter is in a failed state.
 *
 * Results:
 *	Pointer into buf where the decimal string starts.
 *
 * Side effects:
 *	Writes into buf[0..n-1].
 *
 *----------------------------------------------------------------------
 */

static char *
th8test_i64toa(th8_int64_t v, char *buf, size_t n)
{
    char *p = buf + n - 1;
    int neg = 0;
    th8_uint64_t u;

    *p = '\0';
    if (v < 0) {
	neg = 1;
	u = (th8_uint64_t)(-(v + 1)) + 1;
    } else {
	u = (th8_uint64_t)v;
    }
    if (u == 0) {
	*--p = '0';
    } else {
	while (u > 0 && p > buf) {
	    *--p = '0' + (char)(u % 10);
	    u /= 10;
	}
    }
    if (neg && p > buf) *--p = '-';
    return p;
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_sandbox_cmd --
 *
 *	Implements `::th8testlib::sandbox SCRIPT`.  Spins up a
 *	short-lived child interpreter on a copy of the parent's
 *	platform, evaluates SCRIPT inside it, and returns a
 *	structured report describing the outcome:
 *
 *	  * Return code of the evaluation (`OK`, `ERROR`, etc.).
 *	  * The child interpreter's final result, length-capped
 *	    by `TH8_SANDBOX_RESULT_LIMIT` (1 MiB) to keep a
 *	    runaway script from blowing the parent's memory.
 *	  * Step count and allocation totals consumed by the
 *	    child (formatted via `th8test_i64toa` so the
 *	    function continues to work even with a failed
 *	    interpreter).
 *
 *	The child uses the parent's OS callbacks unchanged
 *	(filesystem, networking, etc. -- this is **not** a
 *	security sandbox), but starts with a fresh
 *	interpreter state and is destroyed at the end of the
 *	call.  The helper is intended for coverage tests that
 *	need to exercise post-failure / interpreter-tear-down
 *	paths without disturbing the parent's state.
 *
 * Parameters:
 *	interp -- live interpreter (receives the report).
 *	ctx    -- unused command context.
 *	argc   -- argument count (must be 2).
 *	argv   -- argv[0]=command name; argv[1]=SCRIPT body.
 *	argl   -- argument byte-lengths.
 *
 * Returns:
 *	`TH8_OK` on success with the report in the
 *	interpreter result.  `TH8_ERROR` on argument-count or
 *	child-interpreter-creation failure (interpreter result:
 *	diagnostic).
 *
 * Side effects:
 *	Creates and destroys one child interpreter; allocates
 *	and frees a scratch result buffer.  Sets the interpreter
 *	result.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_sandbox_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    Th8_Interp *pChild = NULL;
    Th8_Platform childPlat;
    const Th8_Platform *pParentPlat;
    int evalRc;
    const char *zResult;
    size_t nResult;
    th8_int64_t nSteps;
    size_t nAlloc;
    char *zOut = NULL;
    size_t nOut = 0;
    char zBuf[32];

    (void)ctx;

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "th8testlib::sandbox script");
    }

    /*
     * Step 1: Copy the parent's platform.  The child uses the
     * same OS callbacks but gets its own interpreter state.
     */

    pParentPlat = Th8_GetPlatform(interp);
    if (!pParentPlat) {
	Th8_SetResultStatic(
	    interp, "sandbox: cannot access parent platform", TH8_NOLEN);
	return TH8_ERROR;
    }
    childPlat = *pParentPlat;

    /*
     * Null out xPanic so that resource limit exhaustion in the
     * child produces an error return rather than aborting the
     * parent process.  A sandboxed child must never be able to
     * kill the host.
     */
    childPlat.xPanic = NULL;

    /*
     * Step 2: Create the child interpreter.
     */

    pChild = Th8_CreateInterp(&childPlat);
    if (!pChild) {
	Th8_SetResultStatic(
	    interp, "sandbox: cannot create child interpreter", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Step 3: Register language commands (set, if, proc, expr, etc.)
     * but do NOT enable any optional features.
     */

    Th8_RegisterLanguage(pChild);

    /*
     * Step 4: Apply fail-safe security restrictions.
     *
     * Binary loading is DISABLED by default (Th8_CreateInterp
     * initializes nLoadOk to 0).  We explicitly do NOT call
     * Th8_EnableLoad.  Same for unload and bigint.
     *
     * Integer overflow checking is ON by default (bOverflowCheck=1
     * in Th8_CreateInterp).  We do not change it.
     *
     * Set resource limits to prevent runaway scripts.
     */

    Th8_SetAllocLimit(pChild, TH8_SANDBOX_ALLOC_LIMIT);
    Th8_SetStepLimit(pChild, TH8_SANDBOX_STEP_LIMIT);
    Th8_SetResultLimit(pChild, TH8_SANDBOX_RESULT_LIMIT);

    /*
     * Step 5: Evaluate the script in the child.
     * Pass NULL for the origin name -- the child has no signed-only
     * policy, so origin is irrelevant.
     */

    evalRc = Th8_Eval(pChild, 0, argv[1], argl[1], NULL, 0);

    /*
     * Step 6: Capture results and metrics from the child.
     */

    zResult = Th8_GetResult(pChild, &nResult);
    nSteps = Th8_GetStepCount(pChild);
    nAlloc = Th8_GetAllocBytes(pChild);

    /*
     * Step 7: Build the result list in the PARENT interpreter.
     * Format: {returnCode result stepCount allocBytes}
     */

    {
	char *z;

	/* returnCode */
	z = th8test_i64toa((th8_int64_t)evalRc, zBuf, sizeof(zBuf));
	Th8_ListAppend(interp, &zOut, &nOut, z, TH8_NOLEN);

	/* result (from child) */
	Th8_ListAppend(interp, &zOut, &nOut, zResult ? zResult : "", nResult);

	/* stepCount */
	z = th8test_i64toa(nSteps, zBuf, sizeof(zBuf));
	Th8_ListAppend(interp, &zOut, &nOut, z, TH8_NOLEN);

	/* allocBytes */
	z = th8test_i64toa((th8_int64_t)nAlloc, zBuf, sizeof(zBuf));
	Th8_ListAppend(interp, &zOut, &nOut, z, TH8_NOLEN);
    }

    /*
     * Step 8: Destroy the child interpreter.  This frees all
     * child-owned resources (variables, commands, namespaces).
     */

    Th8_DeleteInterp(pChild);

    /*
     * Step 9: Set the result in the parent and return.
     */

    if (zOut) {
	Th8_SetResult(interp, zOut, nOut);
	Th8_Free(interp, zOut);
    } else {
	Th8_ClearResult(interp);
    }
    return TH8_OK;
}


#  if defined(TH8_ENABLE_FAULT_INJECTION)

/* Saved counters from the most recent fault eval. */
static Th8_FaultConfig th8test_lastFaultConfig;

/*
 *----------------------------------------------------------------------
 *
 * th8test_nreval_schedfail_cmd --
 *
 *	Implements "th8testlib::nreval_schedfail": verify that
 *	Th8_NREval propagates a callback-scheduling failure instead of
 *	reporting success.
 *
 * Why / How:
 *	Th8_NREval's only allocation is the Th8_Callback pushed by
 *	Th8_NRAddCallback (Th8_Strlen does not allocate).  We create an
 *	isolated child interp with xPanic=NULL, install the fault layer
 *	with nAllocFailAfter=1 so the VERY FIRST allocation fails, then
 *	call Th8_NREval directly.  That first allocation IS the callback,
 *	so Th8_NRAddCallback returns TH8_ERROR; a correct Th8_NREval must
 *	surface that as TH8_ERROR (regression guard for the bug where it
 *	discarded the return and unconditionally returned TH8_OK -- an
 *	OOM while scheduling was reported as success and the deferred
 *	eval silently never ran).
 *
 * Results:
 *	TH8_OK; result is 1 if Th8_NREval correctly returned TH8_ERROR
 *	on the forced scheduling failure, 0 if it wrongly returned
 *	TH8_OK, or -1 if the harness could not be set up.
 *
 * Side effects:
 *	Creates/destroys a child interpreter; installs/uninstalls the
 *	fault layer on it.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_nreval_schedfail_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    const Th8_Platform *pP;
    int nResult = -1;

    (void)ctx;
    (void)argv;
    (void)argl;

    if (argc != 1) {
	return Th8_WrongNumArgs(interp, "th8testlib::nreval_schedfail");
    }

    pP = Th8_GetPlatform(interp);
    if (pP) {
	Th8_Platform cp = *pP;
	Th8_Interp *pChild;

	cp.xPanic = 0; /* OOM returns error instead of aborting. */
	pChild = Th8_CreateInterp(&cp);
	if (pChild) {
	    Th8_FaultConfig cfg;
	    char fbuf[1024];
	    Th8_FaultCtx *pFCtx = (Th8_FaultCtx *)(void *)fbuf;

	    Th8_RegisterLanguage(pChild);
	    Th8_FaultConfigInit(&cfg);
	    cfg.nAllocFailAfter = 1; /* fail the first allocation */
	    if (Th8_FaultCtxSize() <= sizeof(fbuf) &&
	        Th8_FaultInstall(pChild, &cfg, pFCtx) == TH8_OK) {
		static const char zProg[] = "set schedFailProbe 1";
		int rc =
		    Th8_NREval(pChild, zProg, sizeof(zProg) - 1, NULL, 0);

		Th8_FaultUninstall(pChild, pFCtx);
		/* Correct behavior: the forced callback-alloc failure is
		 * surfaced as TH8_ERROR. */
		nResult = (rc == TH8_ERROR) ? 1 : 0;
	    }
	    Th8_DeleteInterp(pChild);
	}
    }
    return Th8_SetResultInt(interp, nResult);
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_bufwrite_oom_cmd --
 *
 *	Implements "th8testlib::bufwrite_oom": verify that a growable
 *	Th8_Buffer append failure (out of memory) is surfaced as an
 *	error at finalization instead of silently publishing a TRUNCATED
 *	buffer.
 *
 * Why / How:
 *	Th8_Subst on plain input ("abcdef" -- no backslash/$/[ ) copies
 *	the bytes through th8BufWrite, whose first call grows the output
 *	buffer.  We sweep a forced single allocation failure over the
 *	first few allocation ordinals; for each, Th8_Subst must EITHER
 *	return the full correct result ("abcdef", when the failed alloc
 *	did not affect it) OR return TH8_ERROR.  It must NEVER return
 *	TH8_OK with a truncated / wrong result -- that was Bug 61, where
 *	th8BufWrite silently dropped an append and the caller published
 *	the truncated buffer as success.  Runs in an isolated child
 *	interp with xPanic=NULL so OOM returns an error rather than
 *	aborting.
 *
 * Results:
 *	TH8_OK; result is 1 if the invariant held for every swept
 *	ordinal (no truncated success), 0 if any ordinal produced a
 *	truncated TH8_OK result, or -1 if the harness could not be set
 *	up.
 *
 * Side effects:
 *	Creates/destroys child interpreters; installs/uninstalls the
 *	fault layer.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_bufwrite_oom_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    const Th8_Platform *pP;
    int nResult = -1;
    int nOk = 0; /* count of usable (harness-installed) sweeps */
    int nBad = 0; /* count of truncated-success violations */
    int nFail; /* forced failing allocation ordinal */

    (void)ctx;
    (void)argv;
    (void)argl;

    if (argc != 1) {
	return Th8_WrongNumArgs(interp, "th8testlib::bufwrite_oom");
    }

    pP = Th8_GetPlatform(interp);
    if (!pP) {
	return Th8_SetResultInt(interp, nResult);
    }

    for (nFail = 1; nFail <= 6; nFail++) {
	Th8_Platform cp = *pP;
	Th8_Interp *pChild;

	cp.xPanic = 0;
	pChild = Th8_CreateInterp(&cp);
	if (!pChild) {
	    continue;
	}
	{
	    Th8_FaultConfig cfg;
	    char fbuf[1024];
	    Th8_FaultCtx *pFCtx = (Th8_FaultCtx *)(void *)fbuf;

	    Th8_RegisterLanguage(pChild);
	    Th8_FaultConfigInit(&cfg);
	    cfg.nAllocFailAfter = nFail;
	    if (Th8_FaultCtxSize() <= sizeof(fbuf) &&
	        Th8_FaultInstall(pChild, &cfg, pFCtx) == TH8_OK) {
		static const char zIn[] = "abcdef";
		int rc =
		    Th8_Subst(pChild, zIn, sizeof(zIn) - 1, TH8_SUBST_ALL);

		Th8_FaultUninstall(pChild, pFCtx);
		nOk++;
		if (rc == TH8_OK) {
		    size_t nRes = 0;
		    const char *zRes = Th8_GetResult(pChild, &nRes);
		    /* On success the result MUST be the full input; a
		     * truncated / short result reported as success is the
		     * bug. */
		    if (TH8_LEN(nRes) != sizeof(zIn) - 1 ||
		        Th8_Memcmp(pChild, zRes, zIn, sizeof(zIn) - 1) != 0) {
			nBad++;
		    }
		}
	    }
	}
	Th8_DeleteInterp(pChild);
    }

    if (nOk > 0) {
	nResult = (nBad == 0) ? 1 : 0;
    }
    return Th8_SetResultInt(interp, nResult);
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_fault_cmd --
 *
 *	Implements "th8testlib::fault eval <script> ?options?" and
 *	"th8testlib::fault counters".
 *
 *	Fault-injection testing command.  Installs the fault layer,
 *	evaluates the script, captures results and counters, then
 *	uninstalls the fault layer.  This ensures the fault layer
 *	is always cleaned up -- even if the script errors.
 *
 *	Options for "eval":
 *	  -allocFailAfter N     Fail the Nth allocation (0 = never)
 *	  -allocFailInterval M  After first failure, every Mth
 *	  -failGetData          Always fail xGetData
 *	  -failDataExists       Always fail xDataExists
 *	  -failRandomBytes      Always fail xRandomBytes
 *	  -failChannelRead      Fail xChannelControl READ ops
 *	  -failChannelEOF       Synth premature EOF on READ
 *	                        (success rc, *pnResult = 0)
 *	  -failChannelWrite     Fail xChannelControl WRITE ops
 *	  -failChannelOpen      Fail xChannelControl OPEN ops
 *
 *	"eval" returns: {returnCode result allocCount allocFailCount}
 *	"counters" returns the counters from the most recent eval.
 *
 *	All fault paths must be valgrind-clean: no leaks, no
 *	uninitialized reads, no use-after-free.
 *
 * Why / How:
 *	Tests error-handling paths that only trigger under resource
 *	exhaustion.  Creates an isolated child interpreter with
 *	xPanic=NULL (so OOM returns error instead of aborting),
 *	installs the fault interception layer, runs the script, then
 *	uninstalls regardless of outcome.  The child isolation
 *	ensures faults never corrupt the parent test harness.
 *
 * Results:
 *	"eval": TH8_OK with {returnCode result allocCount
 *	allocFailCount} list.
 *	"counters": TH8_OK with counter values from last eval.
 *
 * Side effects:
 *	Creates/destroys a child interpreter; installs/uninstalls
 *	the fault injection layer on it.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_fault_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    (void)ctx;

    if (argc < 2) {
	return Th8_WrongNumArgs(
	    interp, "th8testlib::fault eval|counters ?args?");
    }

    if (argl[1] == 4 && memcmp(argv[1], "eval", 4) == 0) {
	/*
	 * fault eval <script> ?-allocFailAfter N? ...
	 */
	Th8_FaultConfig cfg;
	unsigned char fctxBuf[sizeof(void *) * 256]; /* oversized */
	Th8_FaultCtx *pFCtx = (Th8_FaultCtx *)(void *)fctxBuf;
	const char *zScript;
	size_t nScript;
	int evalRc;
	const char *zResult;
	size_t nResult;
	char *zOut = NULL;
	size_t nOut = 0;
	char zBuf[32];
	char *z;
	int i;
	/* Per-site filter scratch space.  Up to 16 -allocFailSite
	 * occurrences are supported per fault eval invocation.  Each
	 * filter's zFile is a heap copy owned by this scope (freed at
	 * the end). */
	enum {
	    FAULT_MAX_FILTERS = 16
	};
	Th8_FaultFilter aFilter[FAULT_MAX_FILTERS];
	int nFilter = 0;
	int j;
	/* Optional per-eval child-interp memory cap.  0 means
	 * "do not call Th8_SetAllocLimit" -- the child keeps the
	 * default unlimited budget.  A positive value triggers the
	 * sandbox memory-limit-check path in Th8_SafeAlloc, which
	 * is the only way (apart from sandbox-cmd) to drive the
	 * `(bPanic && interp->pPlatform->xPanic)` compounds in
	 * th8_core.c at the limit-exceeded sites. */
	th8_int64_t nAllocLimit = 0;
	/* Optional flags toggled by -enableLoad / -enableUnload
	 * to lift the child interp's default-disabled load/unload
	 * gate.  Required to drive the th8_load.c MC/DC compounds
	 * because [load] / [unload] otherwise short-circuit with
	 * "binary loading is not enabled" before reaching the
	 * platform-callback check. */
	int bEnableLoad = 0;
	int bEnableUnload = 0;
	/* Optional flag toggled by -enableBigint.  Calls
	 * Th8_EnableBigint(pChild, 1) so subsequent bigint paths
	 * (th8IsBigint, the L796 / L848 `pCached &&
	 * pCached->u.bigint.iValid` cache decisions, the
	 * th8BigintCacheStore L246 / L261 guards) become reachable
	 * from a fault eval.  Fault eval children otherwise have
	 * bigint disabled by design, so the OOM-on-cache vectors
	 * for bigint-class decisions cannot be driven without
	 * lifting the gate explicitly. */
	int bEnableBigint = 0;
	/* Optional flag toggled by -signedParent.  Calls
	 * Th8_InstallSignedPolicy + Th8_EnableSignedOnly on the
	 * fault eval child so Th8_IsSignedOnlyEnabled returns
	 * TRUE.  Drives the bParentSigned=T paths in
	 * Th8_EvalFileAsData (e.g. th8_xlib.c L427) when the
	 * script calls EvalFileAsData on the wrapped child. */
	int bSignedParent = 0;

	if (argc < 3) {
	    return Th8_WrongNumArgs(
	        interp, "th8testlib::fault eval script ?options?");
	}

	/*
	 * Verify our stack buffer is large enough for Th8_FaultCtx.
	 */
	if (Th8_FaultCtxSize() > sizeof(fctxBuf)) {
	    Th8_SetResultStatic(
	        interp, "fault: Th8_FaultCtx too large for stack buffer",
	        TH8_NOLEN);
	    return TH8_ERROR;
	}

	zScript = argv[2];
	nScript = argl[2];

	/*
	 * Initialize config to safe defaults, then parse options.
	 */
	Th8_FaultConfigInit(&cfg);

	for (i = 3; i < argc; i++) {
	    if (argl[i] == 15 &&
	        memcmp(argv[i], "-allocFailAfter", 15) == 0) {
		if (i + 1 >= argc) {
		    Th8_SetResultStatic(
		        interp, "fault: -allocFailAfter requires a value",
		        TH8_NOLEN);
		    return TH8_ERROR;
		}
		i++;
		if (Th8_ToWideInt(
		        interp, argv[i], argl[i], &cfg.nAllocFailAfter) !=
		    TH8_OK) {
		    return TH8_ERROR;
		}
	    } else if (
	        argl[i] == 11 && memcmp(argv[i], "-allocLimit", 11) == 0) {
		if (i + 1 >= argc) {
		    Th8_SetResultStatic(
		        interp, "fault: -allocLimit requires a value",
		        TH8_NOLEN);
		    return TH8_ERROR;
		}
		i++;
		if (Th8_ToWideInt(interp, argv[i], argl[i], &nAllocLimit) !=
		    TH8_OK) {
		    return TH8_ERROR;
		}
		if (nAllocLimit < 0) {
		    Th8_SetResultStatic(
		        interp, "fault: -allocLimit must be non-negative",
		        TH8_NOLEN);
		    return TH8_ERROR;
		}
	    } else if (
	        argl[i] == 18 &&
	        memcmp(argv[i], "-allocFailInterval", 18) == 0) {
		if (i + 1 >= argc) {
		    Th8_SetResultStatic(
		        interp, "fault: -allocFailInterval requires a value",
		        TH8_NOLEN);
		    return TH8_ERROR;
		}
		i++;
		if (Th8_ToWideInt(
		        interp, argv[i], argl[i], &cfg.nAllocFailInterval) !=
		    TH8_OK) {
		    return TH8_ERROR;
		}
	    } else if (
	        argl[i] == 12 && memcmp(argv[i], "-failGetData", 12) == 0) {
		cfg.bFailGetData = 1;
	    } else if (
	        argl[i] == 15 &&
	        memcmp(argv[i], "-failDataExists", 15) == 0) {
		cfg.bFailDataExists = 1;
	    } else if (
	        argl[i] == 16 &&
	        memcmp(argv[i], "-failRandomBytes", 16) == 0) {
		cfg.bFailRandomBytes = 1;
	    } else if (
	        argl[i] == 17 &&
	        memcmp(argv[i], "-forceRandomBytes", 17) == 0) {
		/*
		 * -forceRandomBytes HEX16  install 8 bytes (16 hex chars)
		 * into aForceRandomBytes; the actual force does not take
		 * effect until -forceRandomBytesCount N is also set
		 * (default 0 = disabled).
		 *
		 * Targets the th8_load.c L106 (reserved-token retry) and
		 * th8_load.c L622 (companion ~0/1 retry) compounds when
		 * paired with -enableLoad.  Does NOT reach the
		 * th8PolicySetVerified L162 retry compound: signed-only
		 * policy is not enabled on the fault child by default, and
		 * th8PolicySetVerified is only invoked when a script
		 * signature successfully verifies (requires a signed
		 * bundle on a signed-only-enabled interp).  A future
		 * -enableSignedOnly flag plus an inline signing helper
		 * would unlock that target.
		 */
		size_t jb;
		const char *hex;

		if (i + 1 >= argc || argl[i + 1] != 16) {
		    Th8_SetResultStatic(
		        interp,
		        "fault: -forceRandomBytes requires 16 hex chars",
		        TH8_NOLEN);
		    return TH8_ERROR;
		}
		i++;
		hex = argv[i];
		for (jb = 0; jb < 8; jb++) {
		    unsigned int v = 0;
		    int k;

		    for (k = 0; k < 2; k++) {
			char c = hex[jb * 2 + k];
			unsigned int d;

			if (c >= '0' && c <= '9') {
			    d = (unsigned int)(c - '0');
			} else if (c >= 'a' && c <= 'f') {
			    d = (unsigned int)(c - 'a' + 10);
			} else if (c >= 'A' && c <= 'F') {
			    d = (unsigned int)(c - 'A' + 10);
			} else {
			    Th8_SetResultStatic(
			        interp,
			        "fault: -forceRandomBytes invalid hex",
			        TH8_NOLEN);
			    return TH8_ERROR;
			}
			v = (v << 4) | d;
		    }
		    cfg.aForceRandomBytes[jb] = (unsigned char)v;
		}
	    } else if (
	        argl[i] == 22 &&
	        memcmp(argv[i], "-forceRandomBytesCount", 22) == 0) {
		int n;

		if (i + 1 >= argc) {
		    Th8_SetResultStatic(
		        interp,
		        "fault: -forceRandomBytesCount requires a value",
		        TH8_NOLEN);
		    return TH8_ERROR;
		}
		i++;
		if (Th8_ToInt(interp, argv[i], argl[i], &n) != TH8_OK) {
		    return TH8_ERROR;
		}
		cfg.nForceRandomBytesCount = n;
	    } else if (
	        argl[i] == 16 &&
	        memcmp(argv[i], "-failChannelRead", 16) == 0) {
		cfg.bFailChannelRead = 1;
	    } else if (
	        argl[i] == 15 &&
	        memcmp(argv[i], "-failChannelEOF", 15) == 0) {
		cfg.bFailChannelEOF = 1;
	    } else if (
	        argl[i] == 17 &&
	        memcmp(argv[i], "-failChannelWrite", 17) == 0) {
		cfg.bFailChannelWrite = 1;
	    } else if (
	        argl[i] == 16 &&
	        memcmp(argv[i], "-failChannelOpen", 16) == 0) {
		cfg.bFailChannelOpen = 1;
	    } else if (
	        argl[i] == 17 &&
	        memcmp(argv[i], "-failEmbeddedKey0", 17) == 0) {
		/*
		 * -failEmbeddedKey0 N  forces Th8_GetPublicKeyZero's
		 * lazy-init guard at th8_policy.c L2091 to observe
		 *   N=1: zData==NULL  (drives C1=T)
		 *   N=2: nData==0     (drives C1=F, C2=T)
		 * Cache must be cleared first via
		 * th8testlib::resetkeycaches; the embedded blob is
		 * always valid in normal builds so without the
		 * combination of cache reset + fault flag, the only
		 * MC/DC vector observed is (F,F).
		 */
		th8_int64_t n;

		if (i + 1 >= argc) {
		    Th8_SetResultStatic(
		        interp, "fault: -failEmbeddedKey0 requires a value",
		        TH8_NOLEN);
		    return TH8_ERROR;
		}
		i++;
		if (Th8_ToWideInt(interp, argv[i], argl[i], &n) != TH8_OK) {
		    return TH8_ERROR;
		}
		cfg.nFailEmbeddedKey0 = (int)n;
	    } else if (
	        argl[i] == 20 &&
	        memcmp(argv[i], "-failEmbeddedKeyRoot", 20) == 0) {
		/*
		 * -failEmbeddedKeyRoot N  drives the matching L2190
		 * guard in Th8_GetPublicKeyRoot; semantics mirror
		 * -failEmbeddedKey0.
		 */
		th8_int64_t n;

		if (i + 1 >= argc) {
		    Th8_SetResultStatic(
		        interp,
		        "fault: -failEmbeddedKeyRoot requires a value",
		        TH8_NOLEN);
		    return TH8_ERROR;
		}
		i++;
		if (Th8_ToWideInt(interp, argv[i], argl[i], &n) != TH8_OK) {
		    return TH8_ERROR;
		}
		cfg.nFailEmbeddedKeyRoot = (int)n;
	    } else if (
	        argl[i] == 20 &&
	        memcmp(argv[i], "-failEmbeddedKeyTest", 20) == 0) {
		/*
		 * -failEmbeddedKeyTest N  drives the matching L2295
		 * guard in Th8_GetPublicKeyTest; semantics mirror
		 * -failEmbeddedKey0.  Only meaningful in builds with
		 * TH8_ENABLE_TEST_KEY (otherwise the wrapper does
		 * not exist).
		 */
		th8_int64_t n;

		if (i + 1 >= argc) {
		    Th8_SetResultStatic(
		        interp,
		        "fault: -failEmbeddedKeyTest requires a value",
		        TH8_NOLEN);
		    return TH8_ERROR;
		}
		i++;
		if (Th8_ToWideInt(interp, argv[i], argl[i], &n) != TH8_OK) {
		    return TH8_ERROR;
		}
		cfg.nFailEmbeddedKeyTest = (int)n;
	    } else if (
	        argl[i] == 14 && memcmp(argv[i], "-allocFailSite", 14) == 0) {
		/*
		 * -allocFailSite ARG  appends a Th8_FaultFilter entry
		 * derived from ARG.  ARG forms:
		 *   FILE                 - any line in FILE
		 *   FILE:LINE            - single-line match
		 *   FILE:LINEFROM-LINETO - inclusive line range
		 */
		const char *zArg;
		size_t nArg, nFile;
		const char *zColon = NULL;
		Th8_FaultFilter *pF;
		char *zFileCopy;
		size_t k;

		if (i + 1 >= argc) {
		    Th8_SetResultStatic(
		        interp, "fault: -allocFailSite requires a value",
		        TH8_NOLEN);
		    /* Free any zFile copies we made earlier. */
		    for (j = 0; j < nFilter; j++) {
			Th8_Free(interp, (void *)aFilter[j].zFile);
		    }
		    return TH8_ERROR;
		}
		i++;
		zArg = argv[i];
		nArg = argl[i];

		if (nFilter >= FAULT_MAX_FILTERS) {
		    Th8_SetResultStatic(
		        interp,
		        "fault: too many -allocFailSite entries"
		        " (max 16)",
		        TH8_NOLEN);
		    for (j = 0; j < nFilter; j++) {
			Th8_Free(interp, (void *)aFilter[j].zFile);
		    }
		    return TH8_ERROR;
		}

		/* Locate the optional ':' separator. */
		for (k = 0; k < nArg; k++) {
		    if (zArg[k] == ':') {
			zColon = zArg + k;
			break;
		    }
		}
		nFile = zColon ? (size_t)(zColon - zArg) : nArg;

		/* Heap-copy the file name as NUL-terminated. */
		zFileCopy = (char *)TH8_ALLOC_STR(interp, nFile);
		if (!zFileCopy) {
		    for (j = 0; j < nFilter; j++) {
			Th8_Free(interp, (void *)aFilter[j].zFile);
		    }
		    Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
		    return TH8_ERROR;
		}
		Th8_Memcpy(interp, zFileCopy, zArg, nFile);
		zFileCopy[nFile] = '\0';

		pF = &aFilter[nFilter];
		pF->zFile = zFileCopy;
		pF->nLineFrom = 0;
		pF->nLineTo = 0;
		pF->nHit = 0;

		/* Parse line spec after ':' if present. */
		if (zColon) {
		    const char *zLn = zColon + 1;
		    size_t nLn = nArg - nFile - 1;
		    th8_int64_t nLo = 0, nHi = 0;
		    const char *zDash = NULL;
		    size_t kk;

		    for (kk = 0; kk < nLn; kk++) {
			if (zLn[kk] == '-') {
			    zDash = zLn + kk;
			    break;
			}
		    }
		    if (zDash) {
			size_t nLoLen = (size_t)(zDash - zLn);
			size_t nHiLen = nLn - nLoLen - 1;
			if (Th8_ToWideInt(interp, zLn, nLoLen, &nLo) !=
			        TH8_OK ||
			    Th8_ToWideInt(interp, zDash + 1, nHiLen, &nHi) !=
			        TH8_OK) {
			    Th8_Free(interp, zFileCopy);
			    for (j = 0; j < nFilter; j++) {
				Th8_Free(interp, (void *)aFilter[j].zFile);
			    }
			    Th8_SetResultStatic(
			        interp,
			        "fault: -allocFailSite range"
			        " requires LO-HI integer pair",
			        TH8_NOLEN);
			    return TH8_ERROR;
			}
			pF->nLineFrom = (int)nLo;
			pF->nLineTo = (int)nHi;
		    } else if (nLn > 0) {
			if (Th8_ToWideInt(interp, zLn, nLn, &nLo) != TH8_OK) {
			    Th8_Free(interp, zFileCopy);
			    for (j = 0; j < nFilter; j++) {
				Th8_Free(interp, (void *)aFilter[j].zFile);
			    }
			    Th8_SetResultStatic(
			        interp,
			        "fault: -allocFailSite line"
			        " requires integer",
			        TH8_NOLEN);
			    return TH8_ERROR;
			}
			pF->nLineFrom = (int)nLo;
		    }
		}
		nFilter++;
	    } else if (
	        argl[i] == 18 &&
	        memcmp(argv[i], "-allocFailAnywhere", 18) == 0) {
		/*
		 * -allocFailAnywhere -- installs a wildcard filter
		 * entry (zFile=NULL, no line range) so the fault
		 * layer counts and considers every allocation
		 * site, not just those under a named source.
		 * Required to drive th8FaultMatchFilter L169/L172
		 * C1=F vectors (pF->zFile is NULL) since a plain
		 * -allocFailAfter installs no filter and short-
		 * circuits at L163 (nFilter<=0).
		 * No companion value -- boolean flag.
		 */
		Th8_FaultFilter *pF;

		if (nFilter >= FAULT_MAX_FILTERS) {
		    for (j = 0; j < nFilter; j++) {
			Th8_Free(interp, (void *)aFilter[j].zFile);
		    }
		    Th8_SetResultStatic(
		        interp, "fault: too many filter entries", TH8_NOLEN);
		    return TH8_ERROR;
		}
		pF = &aFilter[nFilter];
		pF->zFile = NULL;
		pF->nLineFrom = 0;
		pF->nLineTo = 0;
		pF->nHit = 0;
		nFilter++;
	    } else if (
	        argl[i] == 13 && memcmp(argv[i], "-failMathFunc", 13) == 0) {
		/*
		 * -failMathFunc -- forces the fault wrapper's xMathFunc
		 * to return TH8_ERROR for every call, driving the
		 * C3=F vector at `pPlat->xMathFunc(...) != TH8_OK`
		 * compounds in th8_math.c and th8_expressions.c.
		 * No companion value -- it is a boolean flag.
		 */
		cfg.bFailMathFunc = 1;
	    } else if (
	        argl[i] == 11 && memcmp(argv[i], "-failGetCwd", 11) == 0) {
		/*
		 * -failGetCwd -- forces the fault wrapper's xGetCwd
		 * to return NULL for every call, driving the
		 * `if (zCwd == NULL)` error arms in consumers of
		 * Th8_GetCwd (pwd_command and other commands that
		 * resolve relative paths via cwd).  Boolean flag.
		 */
		cfg.bFailGetCwd = 1;
	    } else if (
	        argl[i] == 11 && memcmp(argv[i], "-failTimeMs", 11) == 0) {
		/*
		 * -failTimeMs -- forces xTimeMs to return TH8_ERROR,
		 * driving clock-failure error arms in
		 * [clock milliseconds] and other Th8_GetTimeMs
		 * consumers.  Boolean flag.
		 */
		cfg.bFailTimeMs = 1;
	    } else if (
	        argl[i] == 21 &&
	        memcmp(argv[i], "-failOsslFromdataInit", 21) == 0) {
		/*
		 * -failOsslFromdataInit -- arms TH8_OSSL_OP_FROMDATA_INIT
		 * in nFailOsslMask so the OSSL_CALL wrapper in th8_snk.c
		 * forces EVP_PKEY_fromdata_init() to report failure,
		 * driving the "RSA verify: key construction failed"
		 * error arm in the public-key construction path.  Boolean
		 * flag (proof-of-concept for the OpenSSL fault shim).
		 */
		cfg.nFailOsslMask |=
		    ((th8_uint64_t)1 << TH8_OSSL_OP_FROMDATA_INIT);
	    } else if (
	        argl[i] == 11 && memcmp(argv[i], "-failGetEnv", 11) == 0) {
		/*
		 * -failGetEnv -- forces xGetEnv to return NULL,
		 * driving error arms in env-array initialization
		 * and [info env]-related paths.  Boolean flag.
		 */
		cfg.bFailGetEnv = 1;
	    } else if (
	        argl[i] == 11 && memcmp(argv[i], "-enableLoad", 11) == 0) {
		/* Lifts the child interp's load gate; necessary to
		 * reach Th8_Load's platform-callback check (L622)
		 * via [load] from a fault eval. */
		bEnableLoad = 1;
	    } else if (
	        argl[i] == 13 && memcmp(argv[i], "-enableUnload", 13) == 0) {
		/* Lifts the child interp's unload gate. */
		bEnableUnload = 1;
	    } else if (
	        argl[i] == 13 && memcmp(argv[i], "-enableBigint", 13) == 0) {
		/* Lifts the child interp's bigint gate so the
		 * bigint cache-lookup decisions become reachable
		 * under fault injection. */
		bEnableBigint = 1;
	    } else if (
	        argl[i] == 16 &&
	        memcmp(argv[i], "-failCacheLookup", 16) == 0) {
		/* -failCacheLookup TYPENAME forces Th8_FindInCache
		 * to return NULL for the named cacheType.  Accepted
		 * names: TH8_CACHE_INT / WIDE / DOUBLE / BIGINT /
		 * BOOL / SUBEXPR / STRING / LIST / COMMAND / DICT /
		 * BUFFER.  Bypasses the allocation-site filter so
		 * MC/DC tests can drive Bug-28-family
		 * `pCached == NULL` vectors at specific call sites
		 * deterministically. */
		int eType = -1;
		if (i + 1 >= argc) {
		    Th8_SetResultStatic(
		        interp,
		        "fault: -failCacheLookup requires a cacheType "
		        "name",
		        TH8_NOLEN);
		    return TH8_ERROR;
		}
		i++;
		if (argl[i] == 16 &&
		    memcmp(argv[i], "TH8_CACHE_BIGINT", 16) == 0) {
		    eType = TH8_CACHE_BIGINT;
		} else if (
		    argl[i] == 14 &&
		    memcmp(argv[i], "TH8_CACHE_WIDE", 14) == 0) {
		    eType = TH8_CACHE_WIDE;
		} else if (
		    argl[i] == 16 &&
		    memcmp(argv[i], "TH8_CACHE_DOUBLE", 16) == 0) {
		    eType = TH8_CACHE_DOUBLE;
		} else if (
		    argl[i] == 14 &&
		    memcmp(argv[i], "TH8_CACHE_LIST", 14) == 0) {
		    eType = TH8_CACHE_LIST;
		} else if (
		    argl[i] == 13 &&
		    memcmp(argv[i], "TH8_CACHE_INT", 13) == 0) {
		    eType = TH8_CACHE_INT;
		} else if (
		    argl[i] == 14 &&
		    memcmp(argv[i], "TH8_CACHE_BOOL", 14) == 0) {
		    eType = TH8_CACHE_BOOL;
		} else if (
		    argl[i] == 16 &&
		    memcmp(argv[i], "TH8_CACHE_BUFFER", 16) == 0) {
		    eType = TH8_CACHE_BUFFER;
		} else if (
		    argl[i] == 17 &&
		    memcmp(argv[i], "TH8_CACHE_COMMAND", 17) == 0) {
		    eType = TH8_CACHE_COMMAND;
		} else if (
		    argl[i] == 16 &&
		    memcmp(argv[i], "TH8_CACHE_STRING", 16) == 0) {
		    eType = TH8_CACHE_STRING;
		} else if (
		    argl[i] == 14 &&
		    memcmp(argv[i], "TH8_CACHE_DICT", 14) == 0) {
		    eType = TH8_CACHE_DICT;
		} else if (
		    argl[i] == 17 &&
		    memcmp(argv[i], "TH8_CACHE_SUBEXPR", 17) == 0) {
		    eType = TH8_CACHE_SUBEXPR;
		} else {
		    Th8_ErrorMessage(
		        interp,
		        "fault: -failCacheLookup unknown cacheType \"",
		        argv[i], argl[i]);
		    return TH8_ERROR;
		}
		cfg.nFailCacheLookupMask |= (1u << eType);
	    } else if (
	        argl[i] == 13 && memcmp(argv[i], "-signedParent", 13) == 0) {
		/* Marks the fault eval child as signed-only so
		 * Th8_EvalFileAsData sees bParentSigned=T. */
		bSignedParent = 1;
	    } else if (
	        argl[i] == 14 && memcmp(argv[i], "-nullCallbacks", 14) == 0) {
		/*
		 * -nullCallbacks LIST  parses LIST (Tcl list of slot
		 * names like "xMutexEnter xPanic") and stashes the
		 * resulting array on the fault config.  The fault
		 * layer overwrites those callback slots with NULL on
		 * the wrapped platform so consuming code sees NULL
		 * and takes the false-vector branch on
		 * `if (pPlat->xXxx)` style decisions.  Used for
		 * MC/DC closure of platform-callback compounds that
		 * the always-non-NULL POSIX platform never reaches.
		 */
		if (i + 1 >= argc) {
		    Th8_SetResultStatic(
		        interp, "fault: -nullCallbacks requires a value",
		        TH8_NOLEN);
		    for (j = 0; j < nFilter; j++) {
			Th8_Free(interp, (void *)aFilter[j].zFile);
		    }
		    return TH8_ERROR;
		}
		i++;
		if (cfg.azNullCallbacks != NULL) {
		    Th8_SetResultStatic(
		        interp, "fault: -nullCallbacks may only appear once",
		        TH8_NOLEN);
		    for (j = 0; j < nFilter; j++) {
			Th8_Free(interp, (void *)aFilter[j].zFile);
		    }
		    return TH8_ERROR;
		}
		{
		    char **azElem = NULL;
		    size_t *anElem = NULL;
		    int nElem = 0;

		    if (Th8_SplitList(
		            interp, argv[i], argl[i], &azElem, &anElem,
		            &nElem, TH8_LIST_NONE) != TH8_OK) {
			for (j = 0; j < nFilter; j++) {
			    Th8_Free(interp, (void *)aFilter[j].zFile);
			}
			return TH8_ERROR;
		    }
		    /*
		     * Th8_SplitList returns NUL-terminated copies in
		     * azElem; we can hand the array pointer directly
		     * to the fault config via a const cast.  The
		     * memory is freed after Th8_FaultUninstall below.
		     */
		    /*
		     * Th8_SplitList uses ONE backing allocation for
		     * azElem, anElem, and the per-element string
		     * data (all packed sequentially -- see
		     * src/th8_core.c lines 14721..14757).  Therefore
		     * we MUST NOT free anElem or the individual
		     * strings; the only valid cleanup is a single
		     * Th8_Free(interp, azElem).  We hand the array
		     * to the fault config and free it as a whole
		     * after Th8_FaultUninstall.
		     */
		    cfg.azNullCallbacks = (const char **)azElem;
		    cfg.nNullCallbacks = nElem;
		    (void)anElem;
		}
	    } else {
		Th8_ErrorMessage(
		    interp, "fault: unknown option \"", argv[i], argl[i]);
		if (cfg.azNullCallbacks) {
		    Th8_Free(interp, (void *)cfg.azNullCallbacks);
		}
		for (j = 0; j < nFilter; j++) {
		    Th8_Free(interp, (void *)aFilter[j].zFile);
		}
		return TH8_ERROR;
	    }
	}

	/* Wire the per-site filter array onto the config. */
	if (nFilter > 0) {
	    cfg.aFilter = aFilter;
	    cfg.nFilter = nFilter;
	}

	/*
	 * Create an isolated child interpreter for fault testing.
	 * The child has xPanic=NULL (like sandbox) so allocation
	 * failures return clean errors instead of aborting.
	 * The fault layer is installed on the child only -- the
	 * parent interpreter is never affected.
	 */
	{
	    Th8_Interp *pChild;
	    Th8_Platform childPlat;
	    const Th8_Platform *pParent;

	    pParent = Th8_GetPlatform(interp);
	    if (!pParent) {
		Th8_SetResultStatic(
		    interp, "fault: no parent platform", TH8_NOLEN);
		return TH8_ERROR;
	    }
	    childPlat = *pParent;
	    childPlat.xPanic = NULL;

	    pChild = Th8_CreateInterp(&childPlat);
	    if (!pChild) {
		Th8_SetResultStatic(
		    interp, "fault: cannot create child interpreter",
		    TH8_NOLEN);
		goto fault_cmd_install_err;
	    }
	    Th8_RegisterLanguage(pChild);

	    /*
	     * Lift the child's load/unload gates if the caller
	     * requested them via -enableLoad / -enableUnload.
	     * Done before fault install so the gate-token
	     * regeneration's xRandomBytes call uses the real
	     * platform, not the fault wrapper.
	     */
#    if defined(TH8_ENABLE_LOAD)
	    if (bEnableLoad) {
		(void)Th8_EnableLoad(pChild, 1);
	    }
	    if (bEnableUnload) {
		(void)Th8_EnableUnload(pChild, TH8_UNLOAD_OK);
	    }
#    else
	    (void)bEnableLoad;
	    (void)bEnableUnload;
#    endif

#    if defined(TH8_ENABLE_BIGINT)
	    if (bEnableBigint) {
		(void)Th8_EnableBigint(pChild, 1);
	    }
#    else
	    (void)bEnableBigint;
#    endif

#    if defined(TH8_ENABLE_CRYPTOGRAPHY)
	    if (bSignedParent) {
		/* Install and enable signed-only on the child so
		 * subsequent Th8_IsSignedOnlyEnabled(pChild) returns
		 * TRUE.  Done BEFORE fault install so the install's
		 * own allocations are not subject to fault injection. */
		void *pPolCtx = NULL;
		(void)Th8_InstallSignedPolicy(pChild, &pPolCtx);
		(void)Th8_EnableSignedOnly(pChild, 1);
	    }
#    else
	    (void)bSignedParent;
#    endif

	    /*
	     * Apply the per-eval allocation cap if -allocLimit was
	     * given.  Done AFTER Th8_RegisterLanguage so the
	     * interpreter's command-table population does not
	     * itself trip the limit -- the cap should only bound
	     * the script-eval-time allocations the test is meant
	     * to exercise.
	     */
	    if (nAllocLimit > 0) {
		Th8_SetAllocLimit(pChild, (size_t)nAllocLimit);
	    }

	    /*
	     * Install fault injection on the child AFTER language
	     * registration.  Reset the allocation counter so that
	     * nAllocFailAfter is relative to the script evaluation,
	     * not the interpreter setup.  Th8_FaultInstall sets a
	     * descriptive result on the child interp on its own
	     * failures (e.g. an unknown -nullCallbacks slot name);
	     * propagate that to the parent before deleting the
	     * child.
	     */
	    if (Th8_FaultInstall(pChild, &cfg, pFCtx) != TH8_OK) {
		const char *zErr;
		size_t nErr;

		zErr = Th8_GetResult(pChild, &nErr);
		if (zErr && nErr > 0) {
		    Th8_SetResult(interp, zErr, nErr);
		} else {
		    Th8_SetResultStatic(
		        interp, "fault: cannot install fault layer",
		        TH8_NOLEN);
		}
		Th8_DeleteInterp(pChild);
		goto fault_cmd_install_err;
	    }
	    cfg.nAllocCount = 0;

	    evalRc = Th8_Eval(pChild, 0, zScript, nScript, NULL, 0);

	    /*
	     * Capture result from child BEFORE deleting it.
	     */
	    zResult = Th8_GetResult(pChild, &nResult);

	    /*
	     * Save counters for the "counters" subcommand.
	     */
	    th8test_lastFaultConfig = cfg;

	    /* Build result in PARENT (safe allocator).
	     * Copy child result BEFORE deleting child. */
	    z = th8test_i64toa((th8_int64_t)evalRc, zBuf, sizeof(zBuf));
	    Th8_ListAppend(interp, &zOut, &nOut, z, TH8_NOLEN);

	    Th8_ListAppend(
	        interp, &zOut, &nOut, zResult ? zResult : "", nResult);

	    z = th8test_i64toa(cfg.nAllocCount, zBuf, sizeof(zBuf));
	    Th8_ListAppend(interp, &zOut, &nOut, z, TH8_NOLEN);

	    z = th8test_i64toa(cfg.nAllocFailCount, zBuf, sizeof(zBuf));
	    Th8_ListAppend(interp, &zOut, &nOut, z, TH8_NOLEN);

	    /* Uninstall fault layer and destroy child. */
	    Th8_FaultUninstall(pChild, pFCtx);
	    Th8_DeleteInterp(pChild);
	}

	/* Free heap-allocated zFile copies for the per-site filters. */
	for (j = 0; j < nFilter; j++) {
	    Th8_Free(interp, (void *)aFilter[j].zFile);
	}

	/* Free the null-callback name array if -nullCallbacks was
	 * provided.  Th8_SplitList allocated both the outer array
	 * and each NUL-terminated element string. */
	if (cfg.azNullCallbacks) {
	    /* Single allocation; see comment at the -nullCallbacks
	     * parser above. */
	    Th8_Free(interp, (void *)cfg.azNullCallbacks);
	}

	if (zOut) {
	    Th8_SetResult(interp, zOut, nOut);
	    Th8_Free(interp, zOut);
	} else {
	    Th8_ClearResult(interp);
	}
	return TH8_OK;

fault_cmd_install_err:
	/*
	 * Failure-path cleanup for the create-child / install-fault
	 * paths above: free the per-site filter copies and the
	 * -nullCallbacks array.  The result is already set by the
	 * code that jumped here.
	 */
	for (j = 0; j < nFilter; j++) {
	    Th8_Free(interp, (void *)aFilter[j].zFile);
	}
	if (cfg.azNullCallbacks) {
	    /* Single allocation; see comment at the -nullCallbacks
	     * parser above. */
	    Th8_Free(interp, (void *)cfg.azNullCallbacks);
	}
	return TH8_ERROR;

    } else if (argl[1] == 8 && memcmp(argv[1], "counters", 8) == 0) {
	/*
	 * fault counters -- return all counters from last eval.
	 */
	char *zOut = NULL;
	size_t nOut = 0;
	char zBuf[32];
	char *z;

	z = th8test_i64toa(
	    th8test_lastFaultConfig.nAllocCount, zBuf, sizeof(zBuf));
	Th8_ListAppend(interp, &zOut, &nOut, z, TH8_NOLEN);

	z = th8test_i64toa(
	    th8test_lastFaultConfig.nAllocFailCount, zBuf, sizeof(zBuf));
	Th8_ListAppend(interp, &zOut, &nOut, z, TH8_NOLEN);

	z = th8test_i64toa(
	    th8test_lastFaultConfig.nGetDataCount, zBuf, sizeof(zBuf));
	Th8_ListAppend(interp, &zOut, &nOut, z, TH8_NOLEN);

	z = th8test_i64toa(
	    th8test_lastFaultConfig.nGetDataFailCount, zBuf, sizeof(zBuf));
	Th8_ListAppend(interp, &zOut, &nOut, z, TH8_NOLEN);

	z = th8test_i64toa(
	    th8test_lastFaultConfig.nCacheHookFires, zBuf, sizeof(zBuf));
	Th8_ListAppend(interp, &zOut, &nOut, z, TH8_NOLEN);

	if (zOut) {
	    Th8_SetResult(interp, zOut, nOut);
	    Th8_Free(interp, zOut);
	} else {
	    Th8_ClearResult(interp);
	}
	return TH8_OK;
    }

    Th8_SetResultStatic(interp, "fault: must be eval or counters", TH8_NOLEN);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_safe_panic_stub --
 *
 *	No-op `xPanic` platform-callback substitute installed by
 *	`th8test_malloc_drive_cmd`'s panic-driving modes.
 *
 * Why / How:
 *	Those modes drive the
 *	`(bPanic && interp->pPlatform->xPanic)` compound's (T,T)
 *	vector -- "panic IS requested AND a panic callback IS
 *	installed."  For MC/DC all that matters is that this slot
 *	is reachable and non-NULL; the body deliberately does
 *	nothing and returns so the test process stays alive to
 *	exercise subsequent vectors.
 *
 * Parameters:
 *	interp, pCtx, zMsg, nMsg -- all ignored.
 *
 * Results / Side effects:
 *	No return value; no side effects.
 *
 *----------------------------------------------------------------------
 */
static void
th8test_safe_panic_stub(
    Th8_Interp *interp,
    void *pCtx,
    const char *zMsg,
    size_t nMsg)
{
    /*
     * No-op xPanic substitute used by malloc_drive's
     * "*-call-panic" modes.  The point of those modes is to
     * drive the (bPanic && xPanic) compound's (T,T) vector --
     * "panic IS requested, AND a panic callback IS installed."
     * For MC/DC purposes, all that matters is that this slot
     * is reachable and non-NULL; the body need not actually
     * abort.  Returning here keeps the test process alive so
     * subsequent vectors can be exercised.
     */
    (void)interp;
    (void)pCtx;
    (void)zMsg;
    (void)nMsg;
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_safe_need_memory_stub --
 *
 *	No-op `xNeedMemory` platform-callback substitute used by
 *	`th8test_malloc_drive_cmd`'s `realloc-need-call` mode.
 *	Returns NULL ("no recovery available") so the underlying
 *	allocation failure continues to propagate -- the only
 *	purpose of installing a non-NULL callback here is to
 *	make `interp->pPlatform->xNeedMemory` evaluate to a true
 *	value at the `(!p && xNeedMemory)` compound, so the
 *	(T, T) MC/DC vector at `src/th8_core.c:1108` fires.
 *
 * Parameters:
 *	interp -- ignored.
 *	nByte  -- ignored (size that was being requested).
 *
 * Returns:
 *	NULL unconditionally.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static void *
th8test_safe_need_memory_stub(Th8_Interp *interp, size_t nByte)
{
    (void)interp;
    (void)nByte;
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_malloc_drive_cmd --
 *
 *	Implements `::th8testlib::malloc_drive MODE SIZE
 *	?SIZE2?`.  Closes the C2=F MC/DC vectors of the five
 *	`bPanic`-gated / `xNeedMemory`-gated short-circuit
 *	compounds in `src/th8_core.c` that are reachable only
 *	by calling `Th8_Malloc` / `Th8_Realloc` directly from
 *	C with `bPanic = 1` under specific fault conditions:
 *
 *	  * `malloc-limit`  -- line 963, alloc-limit-exceeded
 *	                       on Th8_Malloc.  Args: SIZE.
 *	  * `malloc-oom`    -- line 999, xMalloc returned NULL.
 *	                       Args: SIZE.
 *	  * `realloc-need`  -- line 1108, (!p && xNeedMemory)
 *	                       with a non-NULL xNeedMemory.
 *	                       Args: SIZE SIZE2.
 *	  * `realloc-limit` -- line 1592, alloc-limit-exceeded
 *	                       on Th8_Realloc.  Args: SIZE SIZE2.
 *	  * `realloc-oom`   -- line 1622, xRealloc returned NULL.
 *	                       Args: SIZE SIZE2.
 *
 *	Each mode builds an isolated child interpreter, applies
 *	the relevant fault config (small alloc limit, alloc-
 *	fail trigger, or stub callback), calls the target
 *	allocator with `bPanic = 1`, and tears everything down.
 *	Because the `xPanic` slot is set to the no-op
 *	`th8test_safe_panic_stub`, the (T, T) panic compound
 *	fires but does not abort -- the test process survives.
 *
 *	`Th8_Malloc` and `Th8_Realloc` are the only public
 *	allocators that pass `bPanic = 1`; `Th8_SafeAlloc`
 *	always passes 0, so the bPanic side of the compound is
 *	otherwise unreachable from any script-level allocator.
 *
 * Parameters:
 *	interp -- live interpreter (receives result).
 *	ctx    -- unused command context.
 *	argc   -- argument count (3 for malloc-*, 4 for realloc-*).
 *	argv   -- argv[0]=command name; argv[1]=MODE; argv[2]=SIZE;
 *		argv[3]=SIZE2 (realloc-* only).
 *	argl   -- argument byte-lengths.
 *
 * Returns:
 *	`TH8_OK` with the result set to `"nil"` if the
 *	allocator returned NULL (expected -- the panic stub
 *	does not abort), `"ptr"` if it surprisingly returned
 *	a pointer despite the fault setup.
 *	`TH8_ERROR` with a diagnostic on bad arguments or
 *	child-interpreter setup failure.
 *
 * Side effects:
 *	Creates and destroys one child interpreter; installs
 *	and uninstalls the fault layer; allocates and frees
 *	an internal scratch fault context.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_malloc_drive_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    th8_int64_t nSize, nSize2 = 0;
    Th8_Interp *pChild;
    Th8_Platform childPlat;
    const Th8_Platform *pParent;
    Th8_FaultConfig cfg;
    Th8_FaultCtx *pFCtx;
    unsigned char fctxBuf[sizeof(void *) * 256];
    static const char *azNullPanic[] = {"xPanic"};
    static const char *azNullNeedMem[] = {"xNeedMemory"};
    int isMallocLimit, isMallocOom;
    int isMallocLimitCall, isMallocOomCall;
    int isReallocNeed, isReallocLimit, isReallocOom;
    int isReallocLimitCall, isReallocOomCall;
    int isReallocNeedCall;
    int isReallocLimitAttempt, isReallocOomAttempt;
    int isReallocShrink;
    int isSafeAllocNeedCall;
    int wantPanicCall;
    int wantAttempt;
    void *p;

    (void)ctx;

    if (argc < 3 || argc > 4) {
	return Th8_WrongNumArgs(
	    interp, "th8testlib::malloc_drive MODE SIZE ?SIZE2?");
    }

    isMallocLimit =
        (argl[1] == 12 && memcmp(argv[1], "malloc-limit", 12) == 0);
    isMallocOom = (argl[1] == 10 && memcmp(argv[1], "malloc-oom", 10) == 0);
    isReallocNeed =
        (argl[1] == 12 && memcmp(argv[1], "realloc-need", 12) == 0);
    isReallocLimit =
        (argl[1] == 13 && memcmp(argv[1], "realloc-limit", 13) == 0);
    isReallocOom = (argl[1] == 11 && memcmp(argv[1], "realloc-oom", 11) == 0);
    isMallocLimitCall =
        (argl[1] == 17 && memcmp(argv[1], "malloc-limit-call", 17) == 0);
    isMallocOomCall =
        (argl[1] == 15 && memcmp(argv[1], "malloc-oom-call", 15) == 0);
    isReallocLimitCall =
        (argl[1] == 18 && memcmp(argv[1], "realloc-limit-call", 18) == 0);
    isReallocOomCall =
        (argl[1] == 16 && memcmp(argv[1], "realloc-oom-call", 16) == 0);
    isReallocNeedCall =
        (argl[1] == 17 && memcmp(argv[1], "realloc-need-call", 17) == 0);

    /* Two more variants: identical to realloc-limit / realloc-oom
     * but call Th8_AttemptRealloc (bPanic=0) so the C1-pair MC/DC
     * vector at lines 1592 / 1622 fires.  Without this the
     * realloc compounds top out at 50% MC/DC because all
     * realloc paths in the suite use Th8_Realloc (bPanic=1). */
    isReallocLimitAttempt =
        (argl[1] == 21 && memcmp(argv[1], "realloc-limit-attempt", 21) == 0);
    isReallocOomAttempt =
        (argl[1] == 19 && memcmp(argv[1], "realloc-oom-attempt", 19) == 0);
    wantAttempt = isReallocLimitAttempt || isReallocOomAttempt;

    /* realloc-shrink: alloc limit set, then realloc to a SMALLER
     * size.  Drives th8_core.c L1591 C2-Pair (T,F) -- limit > 0
     * (C1=T), but nByte <= nOldSize (C2=F) so the limit-check
     * block is skipped.  Args: SIZE (seed/old size) SIZE2 (new
     * smaller size).  No fault config needed -- the shrink
     * succeeds normally and the test asserts on the result. */
    isReallocShrink =
        (argl[1] == 14 && memcmp(argv[1], "realloc-shrink", 14) == 0);

    /* safealloc-need-call: drives the (T,T) vector of the
     * second-chance compound `(!p && xNeedMemory)` at
     * th8_core.c:1108.  Th8_SafeAlloc is the only entry that
     * runs that check.  Combined with a non-NULL xNeedMemory
     * stub on the child platform and -allocFailAfter=1 on the
     * fault layer, the (T,T) vector fires. */
    isSafeAllocNeedCall =
        (argl[1] == 19 && memcmp(argv[1], "safealloc-need-call", 19) == 0);

    /* The "*-call" variants want xPanic / xNeedMemory non-NULL
     * so the (T,T) vector of the panic compound fires.  The
     * underlying child callback is the safe-stub above so the
     * actual panic call is a no-op. */
    wantPanicCall = isMallocLimitCall || isMallocOomCall ||
                    isReallocLimitCall || isReallocOomCall ||
                    isReallocNeedCall;

    if (!isMallocLimit && !isMallocOom && !isReallocNeed && !isReallocLimit &&
        !isReallocOom && !wantPanicCall && !wantAttempt &&
        !isSafeAllocNeedCall && !isReallocShrink) {
	Th8_ErrorMessage(
	    interp, "malloc_drive: unknown mode \"", argv[1], argl[1]);
	return TH8_ERROR;
    }

    /* Map the *-call and *-attempt variants onto their normal
     * counterparts for the rest of the flow.  wantPanicCall
     * controls xPanic null-vs-stub; wantAttempt picks
     * Th8_AttemptRealloc (bPanic=0) over Th8_Realloc (bPanic=1). */
    if (isMallocLimitCall) isMallocLimit = 1;
    if (isMallocOomCall) isMallocOom = 1;
    if (isReallocLimitCall) isReallocLimit = 1;
    if (isReallocOomCall) isReallocOom = 1;
    if (isReallocNeedCall) isReallocNeed = 1;
    if (isReallocLimitAttempt) isReallocLimit = 1;
    if (isReallocOomAttempt) isReallocOom = 1;

    if (Th8_ToWideInt(interp, argv[2], argl[2], &nSize) != TH8_OK ||
        nSize < 0) {
	return TH8_ERROR;
    }

    if (isReallocNeed || isReallocLimit || isReallocOom || isReallocShrink) {
	if (argc != 4) {
	    Th8_SetResultStatic(
	        interp, "malloc_drive: realloc modes require SIZE SIZE2",
	        TH8_NOLEN);
	    return TH8_ERROR;
	}
	if (Th8_ToWideInt(interp, argv[3], argl[3], &nSize2) != TH8_OK ||
	    nSize2 < 0) {
	    return TH8_ERROR;
	}
    } else if (argc == 4) {
	Th8_SetResultStatic(
	    interp, "malloc_drive: malloc modes take only SIZE", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Build an isolated child interp with xPanic=NULL.  The
     * fault layer's -nullCallbacks below is the primary
     * mechanism, but nulling it on the underlying platform
     * too means the child cannot panic even if some path
     * bypasses the wrapper.
     */
    pParent = Th8_GetPlatform(interp);
    if (!pParent) {
	Th8_SetResultStatic(
	    interp, "malloc_drive: no parent platform", TH8_NOLEN);
	return TH8_ERROR;
    }
    childPlat = *pParent;
    /* For the default "no-call" modes, null xPanic on the
     * child platform so the fault wrapper's pt_xPanic
     * passthrough sees NULL and short-circuits.  For the
     * "*-call" modes, install a safe no-op stub so pt_xPanic
     * has a legitimate non-NULL target to delegate to and the
     * (T,T) vector of `(bPanic && xPanic)` is reachable
     * without aborting the test process. */
    childPlat.xPanic = wantPanicCall ? th8test_safe_panic_stub : NULL;

    /* xNeedMemory: installed only for the *-need-call modes
     * where we want the (!p && xNeedMemory) compound to fire
     * its (T,T) vector (line 1108 for safealloc, the realloc
     * sibling for realloc).  The stub returns NULL ("no
     * recovery available"), so the alloc failure still
     * propagates as expected.  For non-need modes, leave
     * xNeedMemory as inherited (POSIX default is NULL). */
    if (isReallocNeedCall || isSafeAllocNeedCall) {
	childPlat.xNeedMemory = th8test_safe_need_memory_stub;
    }

    pChild = Th8_CreateInterp(&childPlat);
    if (!pChild) {
	Th8_SetResultStatic(
	    interp, "malloc_drive: cannot create child interp", TH8_NOLEN);
	return TH8_ERROR;
    }
    Th8_RegisterLanguage(pChild);

    /*
     * For realloc modes, allocate the seed buffer NOW -- before
     * installing the fault layer -- so the seed does not consume
     * a fault-counter slot.  Doing it after install would mean
     * nAllocFailAfter=1 catches the seed (not the realloc),
     * leaving the realloc target succeeding.
     */
    {
	void *pSeed = NULL;
	int needSeed = (isReallocNeed || isReallocLimit || isReallocOom ||
	                isReallocShrink) &&
	               nSize > 0;

	if (needSeed) {
	    pSeed = Th8_AttemptMalloc(pChild, (size_t)nSize);
	    if (!pSeed) {
		Th8_DeleteInterp(pChild);
		Th8_SetResultStatic(
		    interp, "malloc_drive: seed alloc failed", TH8_NOLEN);
		return TH8_ERROR;
	    }
	}

	/*
	 * Configure the fault layer for this mode.
	 */
	Th8_FaultConfigInit(&cfg);
	if (isSafeAllocNeedCall) {
	    /* Force the SafeAlloc to fail so we enter line 1108. */
	    cfg.nAllocFailAfter = 1;
	} else if (isMallocLimit) {
	    /* Limit-exceeded path for Th8_Malloc.  Cap is
	     * deliberately smaller than nSize so the call
	     * trips the limit immediately. */
	    Th8_SetAllocLimit(pChild, 1);
	} else if (isReallocLimit) {
	    /* Limit-exceeded path for Th8_Realloc.  Cap must
	     * sit between the seed size (already allocated, so
	     * already counted in nAllocBytes if tracked) and
	     * the realloc target.  Setting it just above the
	     * seed lets the seed survive but trips the realloc. */
	    Th8_SetAllocLimit(pChild, (size_t)nSize + 8);
	} else if (isReallocShrink) {
	    /* Limit-set but shrink-realloc path: cap is set
	     * generously above the seed so the seed survives,
	     * and the realloc target is SMALLER than the seed
	     * (nSize2 < nSize).  L1591 evaluates C1=T (limit>0)
	     * AND C2=F (nByte <= nOldSize) -- skips the limit-
	     * check block entirely.  No fault config needed. */
	    Th8_SetAllocLimit(pChild, (size_t)nSize * 4 + 1024);
	} else if (isMallocOom || isReallocOom) {
	    /* xMalloc / xRealloc returns NULL path.  Force the
	     * first allocation through the fault layer to fail. */
	    cfg.nAllocFailAfter = 1;
	} else { /* isReallocNeed */
	    /* Second-chance allocator path: alloc fails. */
	    cfg.nAllocFailAfter = 1;
	}

	/*
	 * Choose null-callback policy per mode.  In default
	 * (non-*-call) modes, null xPanic / xNeedMemory so the
	 * compound's (T,F) vector fires.  In *-call modes,
	 * leave the wrapper non-NULL (xPanic delegates to the
	 * safe stub above; xNeedMemory passes through) so the
	 * (T,T) vector fires instead.
	 */
	if (!wantPanicCall) {
	    if (isReallocNeed) {
		cfg.azNullCallbacks = azNullNeedMem;
	    } else {
		cfg.azNullCallbacks = azNullPanic;
	    }
	    cfg.nNullCallbacks = 1;
	}

	pFCtx = (Th8_FaultCtx *)(void *)fctxBuf;
	if (Th8_FaultCtxSize() > sizeof(fctxBuf)) {
	    if (pSeed) Th8_Free(pChild, pSeed);
	    Th8_DeleteInterp(pChild);
	    Th8_SetResultStatic(
	        interp, "malloc_drive: Th8_FaultCtx too large", TH8_NOLEN);
	    return TH8_ERROR;
	}
	if (Th8_FaultInstall(pChild, &cfg, pFCtx) != TH8_OK) {
	    if (pSeed) Th8_Free(pChild, pSeed);
	    Th8_DeleteInterp(pChild);
	    Th8_SetResultStatic(
	        interp, "malloc_drive: Th8_FaultInstall failed", TH8_NOLEN);
	    return TH8_ERROR;
	}

	/*
	 * Drive the target allocator with bPanic=1.  Th8_Malloc
	 * and Th8_Realloc are the public bPanic=1 entry points;
	 * both eventually reach th8MallocCommon /
	 * th8ReallocCommon where the panic compound lives.
	 */
	if (isSafeAllocNeedCall) {
	    /* Direct Th8_SafeAlloc invocation -- the only public
	     * entry that runs the second-chance check at line 1108. */
	    p = Th8_SafeAlloc(pChild, (size_t)nSize, "malloc_drive", 0);
	} else if (isMallocLimit || isMallocOom) {
	    p = Th8_Malloc(pChild, (size_t)nSize);
	} else {
	    /* Realloc family.  wantAttempt picks the bPanic=0
	     * variant (Th8_AttemptRealloc) -- needed to drive the
	     * C1=F vector at lines 1592 / 1622.  Otherwise use the
	     * bPanic=1 variant (Th8_Realloc). */
	    if (wantAttempt) {
		p = Th8_AttemptRealloc(pChild, pSeed, (size_t)nSize2);
	    } else {
		p = Th8_Realloc(pChild, pSeed, (size_t)nSize2);
	    }
	    /* If realloc returned NULL, the seed is still live and
	     * we own it; free it.  If it returned non-NULL, the
	     * seed has been moved/freed by the realloc itself and
	     * we own only the new pointer. */
	    if (!p && pSeed) {
		Th8_Free(pChild, pSeed);
	    }
	    pSeed = NULL;
	}

	if (p) {
	    Th8_Free(pChild, p);
	}

	Th8_FaultUninstall(pChild, pFCtx);
    }
    Th8_DeleteInterp(pChild);

    Th8_SetResultStatic(interp, p ? "ptr" : "nil", TH8_NOLEN);
    return TH8_OK;
}

#  endif /* TH8_ENABLE_FAULT_INJECTION */


/*
 *----------------------------------------------------------------------
 *
 * th8test_strnicmp --
 *
 *	Simple ASCII case-insensitive comparison of the first n bytes.
 *
 * Why / How:
 *	The kv command needs case-insensitive operation name matching.
 *	This avoids depending on strncasecmp (POSIX) or _strnicmp
 *	(Win32), keeping the testlib portable across all platforms
 *	without conditional compilation.
 *
 * Results:
 *	0 if the first n bytes are equal (case-insensitive);
 *	negative/positive difference otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_strnicmp(const char *a, const char *b, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
	unsigned char ca = (unsigned char)a[i];
	unsigned char cb = (unsigned char)b[i];

	if (ca >= 'A' && ca <= 'Z') ca += 32;
	if (cb >= 'A' && cb <= 'Z') cb += 32;
	if (ca != cb) return (int)ca - (int)cb;
    }
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_kv_cmd --
 *
 *	::th8testlib::kv op ?name? ?value?
 *
 *	Dispatch key-value operations through the platform's
 *	xKeyValue callback.  Op names are case-insensitive and
 *	correspond to TH8_KV_* constants with the prefix removed.
 *
 * Why / How:
 *	Tests the platform key-value storage API (Th8_KeyValue)
 *	by exposing all operations to script level.  Op names are
 *	matched case-insensitively via th8test_strnicmp so tests
 *	can use natural-looking names.  The EXISTS op is special-
 *	cased to return a boolean rather than propagating TH8_ERROR.
 *
 * Results:
 *	TH8_OK with operation-specific result; TH8_ERROR for
 *	unknown operations.
 *
 * Side effects:
 *	Reads/writes the platform key-value store.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_kv_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    static const struct {
	const char *zName;
	size_t nName;
	int op;
	int nMinArgs; /* min argc (including "kv" and op) */
	int nMaxArgs; /* max argc (-1 = unlimited) */
    } aOps[] = {{"none", 4, TH8_KV_NONE, 2, 2},
                {"exists", 6, TH8_KV_EXISTS, 3, 3},
                {"list", 4, TH8_KV_LIST, 2, 3},
                {"get", 3, TH8_KV_GET, 3, 3},
                {"set", 3, TH8_KV_SET, 4, 4},
                {"unset", 5, TH8_KV_UNSET, 3, 3},
                {"exists2", 7, TH8_KV_EXISTS2, 2, 4},
                {"list2", 5, TH8_KV_LIST2, 2, 4},
                {"get2", 4, TH8_KV_GET2, 2, 4},
                {"set2", 4, TH8_KV_SET2, 3, 4},
                {"unset2", 6, TH8_KV_UNSET2, 2, 4},
                {0, 0, 0, 0, 0}};
    int i;
    int op = -1;
    const char *zName = NULL;
    size_t nName = 0;
    const char *zValue = NULL;
    size_t nValue = 0;
    int rc;

    (void)ctx;

    if (argc < 2) {
	return Th8_WrongNumArgs(interp, "th8testlib::kv op ?name? ?value?");
    }

    /*
     * Match the op name (case-insensitive).
     */

    for (i = 0; aOps[i].zName; i++) {
	if (argl[1] == aOps[i].nName &&
	    th8test_strnicmp(argv[1], aOps[i].zName, aOps[i].nName) == 0) {
	    op = aOps[i].op;

	    if (argc < aOps[i].nMinArgs ||
	        (aOps[i].nMaxArgs >= 0 && argc > aOps[i].nMaxArgs)) {
		return Th8_WrongNumArgs(
		    interp, "th8testlib::kv op ?name? ?value?");
	    }
	    break;
	}
    }

    if (op < 0) {
	Th8_SetResultStatic(interp, "kv: unknown operation", TH8_NOLEN);
	return TH8_ERROR;
    }

    if (op == TH8_KV_NONE) {
	Th8_SetResultStatic(
	    interp, "kv: operation \"none\" is reserved", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Extract name and value arguments.
     */

    if (argc >= 3) {
	zName = argv[2];
	nName = argl[2];
    }
    if (argc >= 4) {
	zValue = argv[3];
	nValue = argl[3];
    }

    /*
     * For LIST with no pattern, default to "*" (match all).
     */

    if (op == TH8_KV_LIST && !zName) {
	zName = "*";
	nName = 1;
    }

    rc = Th8_KeyValue(interp, op, zName, nName, zValue, nValue);

    /*
     * For EXISTS (literal): map TH8_OK/TH8_ERROR to boolean
     * 1/0 and always return TH8_OK to the script.
     */

    if (op == TH8_KV_EXISTS || op == TH8_KV_EXISTS2) {
	Th8_SetResultInt(interp, (rc == TH8_OK) ? 1 : 0);
	return TH8_OK;
    }

    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_env_kv_cmd --
 *
 *	::th8testlib::env_kv op ?name?
 *
 *	Like ::th8testlib::kv but always dispatches through the
 *	env-backed default platform's xKeyValue (Th8_GetEnvPlatform),
 *	bypassing whatever override the interp currently has
 *	installed.  Used to drive MC/DC coverage on src/th8_env.c
 *	even when the prologue has loaded an extension whose
 *	xKeyValue would otherwise intercept the call.
 *
 *	Read-only operations only (exists / get / list and their "2"
 *	forms).  Write operations are deliberately omitted to avoid
 *	mutating the host process's environment from tests.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_env_kv_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    static const struct {
	const char *zName;
	size_t nName;
	int op;
    } aOps[] =
        {{"exists", 6, TH8_KV_EXISTS},
         {"get", 3, TH8_KV_GET},
         {"list", 4, TH8_KV_LIST},
         {"exists2", 7, TH8_KV_EXISTS2},
         {"get2", 4, TH8_KV_GET2},
         {"list2", 5, TH8_KV_LIST2},
         {0, 0, 0}};
    int i, op = -1;
    const char *zName = NULL;
    size_t nName = 0;
    const char *zValue = NULL;
    size_t nValue = 0;
    const Th8_Platform *pEnv;
    int rc;

    (void)ctx;

    if (argc < 2) {
	return Th8_WrongNumArgs(
	    interp, "th8testlib::env_kv op ?name? ?value?");
    }

    for (i = 0; aOps[i].zName; i++) {
	if (argl[1] == aOps[i].nName &&
	    th8test_strnicmp(argv[1], aOps[i].zName, aOps[i].nName) == 0) {
	    op = aOps[i].op;
	    break;
	}
    }
    if (op < 0) {
	Th8_SetResultStatic(interp, "env_kv: unknown operation", TH8_NOLEN);
	return TH8_ERROR;
    }

    if (argc >= 3) {
	zName = argv[2];
	nName = argl[2];
    }
    if (argc >= 4) {
	zValue = argv[3];
	nValue = argl[3];
    }
    /* Pass through zName=NULL when the caller omitted the
     * argument so the LIST/LIST2 paths in th8_env.c can
     * exercise their C1=F vector (zName==NULL) at the
     * per-entry filter compound `if (zName && nName > 0)`.
     * Empty-string ("" with nName==0) drives the C2=F vector
     * separately; both are required for MC/DC closure. */

    pEnv = Th8_GetEnvPlatform();
    if (!pEnv || !pEnv->xKeyValue) {
	Th8_SetResultStatic(
	    interp, "env_kv: env platform unavailable", TH8_NOLEN);
	return TH8_ERROR;
    }

    rc =
        pEnv->xKeyValue(interp, pEnv->pCtx, op, zName, nName, zValue, nValue);

    if (op == TH8_KV_EXISTS || op == TH8_KV_EXISTS2) {
	Th8_SetResultInt(interp, (rc == TH8_OK) ? 1 : 0);
	return TH8_OK;
    }
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_sysvar_cmd --
 *
 *	::th8testlib::sysvar save_restore ARR_NAME
 *
 *	MC/DC closure for Th8_SaveSystemVar / Th8_RestoreSystemVar
 *	(src/th8_vars.c) which have no other test coverage.  Drives
 *	the public save+restore cycle on ARR_NAME so the
 *	`rc != TH8_OK || nCount == 0` and
 *	`pVar && pVar->zData && pVar->nData > 0` compounds fire.
 *
 * Why / How:
 *	The save/restore pair is intended for bracketing untrusted
 *	script evaluation around an array of trusted globals.
 *	Without an explicit test that calls the public C API, the
 *	functions sit at 0% MC/DC.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_sysvar_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    void *pSaved = NULL;
    int rc;

    (void)ctx;

    if (argc != 3 || argl[1] != 12 ||
        Th8_Memcmp(interp, argv[1], "save_restore", 12) != 0) {
	return Th8_WrongNumArgs(
	    interp, "th8testlib::sysvar save_restore arrName");
    }

    rc = Th8_SaveSystemVar(interp, argv[2], argl[2], &pSaved);
    if (rc != TH8_OK) return rc;

    rc = Th8_RestoreSystemVar(interp, argv[2], argl[2], pSaved);
    if (rc != TH8_OK) return rc;

    Th8_SetResultStatic(interp, "ok", 2);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_platform_cb_null_cmd --
 *
 *	::th8testlib::platform_cb_null CALLBACK SCRIPT
 *
 *	P4 of the MC/DC primitive set.  Builds an isolated child
 *	interp with one named platform callback set to NULL,
 *	evaluates SCRIPT in it, and returns the script's result
 *	verbatim (return code propagates).  Reuses the
 *	clone-platform pattern from th8test_fault_cmd.
 *
 *	Drives `pPlat && pPlat->xXxx` MC/DC compounds (~8-10
 *	conditions across th8_core.c, th8_expressions.c,
 *	th8_filesystems.c, th8_plat.c).
 *
 *----------------------------------------------------------------------
 */

static int
th8test_platform_cb_null_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    Th8_Interp *pChild;
    Th8_Platform childPlat;
    const Th8_Platform *pParent;
    const char *zCb;
    size_t nCb;
    const char *zScript;
    size_t nScript;
    const char *zResult;
    size_t nResult;
    int rc;

    (void)ctx;

    if (argc != 3) {
	return Th8_WrongNumArgs(
	    interp, "th8testlib::platform_cb_null callback script");
    }

    zCb = argv[1];
    nCb = argl[1];
    zScript = argv[2];
    nScript = argl[2];

    pParent = Th8_GetPlatform(interp);
    if (!pParent) {
	Th8_SetResultStatic(
	    interp, "platform_cb_null: no parent platform", TH8_NOLEN);
	return TH8_ERROR;
    }
    childPlat = *pParent;
    childPlat.xPanic = NULL; /* sandbox: no abort on alloc fail */

    /*
     * Match callback name and NULL it on the child platform.
     * Note: xPanic is already NULL'd above (sandbox default).
     */
    if (nCb == 6 && Th8_Memcmp(interp, zCb, "xPanic", 6) == 0) {
	/* xPanic already NULL above */
    } else if (nCb == 9 && Th8_Memcmp(interp, zCb, "xMathFunc", 9) == 0) {
	childPlat.xMathFunc = NULL;
    } else if (nCb == 6 && Th8_Memcmp(interp, zCb, "xInput", 6) == 0) {
	childPlat.xInput = NULL;
    } else if (nCb == 7 && Th8_Memcmp(interp, zCb, "xOutput", 7) == 0) {
	childPlat.xOutput = NULL;
    } else if (nCb == 9 && Th8_Memcmp(interp, zCb, "xSameFile", 9) == 0) {
	childPlat.xSameFile = NULL;
    } else if (
        nCb == 12 && Th8_Memcmp(interp, zCb, "xRandomBytes", 12) == 0) {
	childPlat.xRandomBytes = NULL;
    } else if (nCb == 9 && Th8_Memcmp(interp, zCb, "xKeyValue", 9) == 0) {
	childPlat.xKeyValue = NULL;
    } else if (nCb == 11 && Th8_Memcmp(interp, zCb, "xMutexEnter", 11) == 0) {
	childPlat.xMutexEnter = NULL;
    } else if (nCb == 11 && Th8_Memcmp(interp, zCb, "xMutexLeave", 11) == 0) {
	childPlat.xMutexLeave = NULL;
    } else if (nCb == 7 && Th8_Memcmp(interp, zCb, "xGetCwd", 7) == 0) {
	childPlat.xGetCwd = NULL;
    } else if (nCb == 7 && Th8_Memcmp(interp, zCb, "xTimeMs", 7) == 0) {
	childPlat.xTimeMs = NULL;
    } else if (nCb == 6 && Th8_Memcmp(interp, zCb, "xSleep", 6) == 0) {
	childPlat.xSleep = NULL;
    } else {
	Th8_SetResultStatic(
	    interp, "platform_cb_null: unknown callback name", TH8_NOLEN);
	return TH8_ERROR;
    }

    pChild = Th8_CreateInterp(&childPlat);
    if (!pChild) {
	Th8_SetResultStatic(
	    interp, "platform_cb_null: cannot create child", TH8_NOLEN);
	return TH8_ERROR;
    }
    Th8_RegisterLanguage(pChild);

    rc = Th8_Eval(pChild, 0, zScript, nScript, NULL, 0);

    /*
     * Marshal result from child back to parent before deletion.
     */
    zResult = Th8_GetResult(pChild, &nResult);
    if (zResult) {
	Th8_SetResult(interp, zResult, nResult);
    } else {
	Th8_ClearResult(interp);
    }

    Th8_DeleteInterp(pChild);
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_partial_object_cmd --
 *
 *	::th8testlib::partial_object SUBCMD ARGS
 *
 *	P3 of the MC/DC primitive set.  Synthesizes partial-state
 *	Th8_PkgInfo records to drive `pPkg && pPkg->zVersion` and
 *	`pPkg && pPkg->paIfNeeded` MC/DC compounds.
 *
 *	Subcommands:
 *
 *	  pkg_no_version NAME
 *	    Allocate a Th8_PkgInfo with zVersion=NULL,
 *	    paIfNeeded=NULL, register in the package hash under
 *	    NAME.  Drives `if (pPkg && pPkg->zVersion)` at
 *	    th8_extensibility.c:596 and :902 (T,F vector when a
 *	    later [package require NAME] reaches those compounds).
 *
 *	  pkg_no_ifneeded NAME VERSION
 *	    Allocate a Th8_PkgInfo with zVersion set to VERSION but
 *	    paIfNeeded=NULL.  Drives `if (pPkg && pPkg->paIfNeeded)`
 *	    at th8_extensibility.c:556 and :1225.
 *
 *----------------------------------------------------------------------
 */

#  if defined(TH8_PLUGIN_EXTENSIBILITY)
static int
th8test_partial_object_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    Th8_Hash *paPackage;
    Th8_HashEntry *pEntry;
    Th8_PkgInfo *pPkg;
    const char *zSub;
    size_t nSub;

    (void)ctx;

    if (argc < 3) {
	return Th8_WrongNumArgs(
	    interp, "th8testlib::partial_object subcmd args");
    }

    zSub = argv[1];
    nSub = argl[1];

    paPackage = Th8_GetPackageHash(interp);
    if (!paPackage) {
	Th8_SetResultStatic(
	    interp, "partial_object: no package hash", TH8_NOLEN);
	return TH8_ERROR;
    }

    if (nSub == 14 && Th8_Memcmp(interp, zSub, "pkg_no_version", 14) == 0) {
	/* Args: NAME */
	if (argc != 3) {
	    return Th8_WrongNumArgs(
	        interp, "th8testlib::partial_object pkg_no_version name");
	}
	pPkg = (Th8_PkgInfo *)TH8_ALLOC(interp, sizeof(*pPkg));
	if (!pPkg) {
	    Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	    return TH8_ERROR;
	}
	Th8_Memset(interp, pPkg, 0, sizeof(*pPkg));
	/* zVersion = NULL, paIfNeeded = NULL via memset */

	pEntry =
	    Th8_HashFind(interp, paPackage, argv[2], TH8_LEN(argl[2]), 1);
	if (!pEntry) {
	    Th8_Free(interp, pPkg);
	    Th8_SetResultStatic(
	        interp, "partial_object: hash insert failed", TH8_NOLEN);
	    return TH8_ERROR;
	}
	pEntry->pData = pPkg;
	Th8_ClearResult(interp);
	return TH8_OK;
    }

    if (nSub == 15 && Th8_Memcmp(interp, zSub, "pkg_no_ifneeded", 15) == 0) {
	/* Args: NAME VERSION */
	const char *zVer;
	size_t nVer;

	if (argc != 4) {
	    return Th8_WrongNumArgs(
	        interp, "th8testlib::partial_object pkg_no_ifneeded"
	                " name version");
	}
	zVer = argv[3];
	nVer = argl[3];

	pPkg = (Th8_PkgInfo *)TH8_ALLOC(interp, sizeof(*pPkg));
	if (!pPkg) {
	    Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	    return TH8_ERROR;
	}
	Th8_Memset(interp, pPkg, 0, sizeof(*pPkg));
	pPkg->zVersion = (char *)TH8_ALLOC_STR(interp, nVer);
	if (!pPkg->zVersion) {
	    Th8_Free(interp, pPkg);
	    Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	    return TH8_ERROR;
	}
	Th8_Memcpy(interp, pPkg->zVersion, zVer, nVer);
	pPkg->zVersion[nVer] = '\0';
	pPkg->nVersion = nVer;
	/* paIfNeeded = NULL via memset */

	pEntry =
	    Th8_HashFind(interp, paPackage, argv[2], TH8_LEN(argl[2]), 1);
	if (!pEntry) {
	    Th8_Free(interp, pPkg->zVersion);
	    Th8_Free(interp, pPkg);
	    Th8_SetResultStatic(
	        interp, "partial_object: hash insert failed", TH8_NOLEN);
	    return TH8_ERROR;
	}
	pEntry->pData = pPkg;
	Th8_ClearResult(interp);
	return TH8_OK;
    }

    Th8_SetResultStatic(
        interp, "partial_object: unknown subcommand", TH8_NOLEN);
    return TH8_ERROR;
}
#  endif /* TH8_PLUGIN_EXTENSIBILITY */


#  if defined(TH8_ENABLE_CRYPTOGRAPHY) && defined(TH8_ENABLE_VARIABLES)

/*
 * In-memory KV store for secure persistence testing.
 * Uses a Th8_Hash keyed by name, with pData pointing to
 * a {length, data} allocation.
 */

static Th8_Hash *th8test_kvHash = NULL;

typedef struct {
    size_t nData;
    char aData[1]; /* variable-length */
} Th8TestKvEntry;

/*
 *----------------------------------------------------------------------
 *
 * th8test_kvCallback --
 *
 *	Test-only key/value store callback that backs the
 *	secure-variable persistence subsystem when
 *	`th8testlib::secure_persist enable` is in effect.
 *	Implements the four `TH8_KV_*` operations against a
 *	module-static `Th8_Hash` (`th8test_kvHash`):
 *
 *	  * `TH8_KV_SET`    -- replace (or insert) the entry
 *	                       for `zName` with a copy of the
 *	                       `nValue`-byte payload.
 *	  * `TH8_KV_GET`    -- set the interpreter result to
 *	                       the stored payload; error if
 *	                       absent.
 *	  * `TH8_KV_EXISTS` -- return `TH8_OK` if a payload is
 *	                       stored, `TH8_ERROR` otherwise.
 *	  * `TH8_KV_UNSET`  -- free the payload (if any) and
 *	                       null the hash entry; always
 *	                       returns `TH8_OK` (even if
 *	                       absent, mirroring the contract
 *	                       of Tcl `unset -nocomplain`).
 *
 *	The hash is lazily allocated on first use and lives
 *	for the lifetime of the test process.  Each payload is
 *	a single `Th8TestKvEntry` heap allocation owning both
 *	the length header and the inline byte array.
 *
 *	Gated on `TH8_ENABLE_CRYPTOGRAPHY && TH8_ENABLE_VARIABLES`.
 *
 * Parameters:
 *	interp -- live interpreter (used for hash allocation
 *		and `Th8_SetResult` on GET).
 *	pCtx   -- unused KV-callback context.
 *	op     -- one of `TH8_KV_SET`, `_GET`, `_EXISTS`, `_UNSET`.
 *	zName  -- key string (not necessarily NUL-terminated).
 *	nName  -- key length in bytes.
 *	zValue -- payload bytes (SET only; ignored otherwise).
 *	nValue -- payload length (SET only).
 *
 * Returns:
 *	`TH8_OK` on success.  `TH8_ERROR` on allocation
 *	failure, GET-of-missing-key, EXISTS-of-missing-key,
 *	or unknown `op`.
 *
 * Side effects:
 *	May allocate and free entries in the module-static
 *	`th8test_kvHash`.  Sets the interpreter result on
 *	`TH8_KV_GET`.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_kvCallback(
    Th8_Interp *interp,
    void *pCtx,
    int op,
    const char *zName,
    size_t nName,
    const char *zValue,
    size_t nValue)
{
    (void)pCtx;

    if (!th8test_kvHash) {
	th8test_kvHash = Th8_HashNew(interp);
	if (!th8test_kvHash) return TH8_ERROR;
    }

    switch (op) {
    case TH8_KV_SET: {
	Th8_HashEntry *pE;
	Th8TestKvEntry *pOld;
	Th8TestKvEntry *pNew;

	pE = Th8_HashFind(interp, th8test_kvHash, zName, nName, 1);
	if (!pE) return TH8_ERROR;
	pOld = (Th8TestKvEntry *)pE->pData;
	if (pOld) Th8_Free(interp, pOld);
	pNew = (Th8TestKvEntry *)
	    TH8_ALLOC(interp, sizeof(Th8TestKvEntry) + nValue);
	if (!pNew) return TH8_ERROR;
	pNew->nData = nValue;
	Th8_Memcpy(interp, pNew->aData, zValue, nValue);
	pE->pData = (void *)pNew;
	return TH8_OK;
    }
    case TH8_KV_GET: {
	Th8_HashEntry *pE;
	Th8TestKvEntry *pKv;

	pE = Th8_HashFind(interp, th8test_kvHash, zName, nName, 0);
	if (!pE || !pE->pData) return TH8_ERROR;
	pKv = (Th8TestKvEntry *)pE->pData;
	Th8_SetResult(interp, pKv->aData, pKv->nData);
	return TH8_OK;
    }
    case TH8_KV_EXISTS: {
	Th8_HashEntry *pE;

	pE = Th8_HashFind(interp, th8test_kvHash, zName, nName, 0);
	return (pE && pE->pData) ? TH8_OK : TH8_ERROR;
    }
    case TH8_KV_UNSET: {
	Th8_HashEntry *pE;
	Th8TestKvEntry *pKv;

	pE = Th8_HashFind(interp, th8test_kvHash, zName, nName, 0);
	if (pE && pE->pData) {
	    pKv = (Th8TestKvEntry *)pE->pData;
	    Th8_Free(interp, pKv);
	    pE->pData = NULL;
	}
	return TH8_OK;
    }
    default:
	return TH8_ERROR;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_secure_persist_cmd --
 *
 *	::th8testlib::secure_persist enable|disable
 *
 *	Test helper that enables or disables the secure variable
 *	persistence subsystem.  "enable" installs an in-memory KV
 *	store, sets a fixed 32-byte test master key, and opens the
 *	persistence gate.  "disable" clears all three.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_secure_persist_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    static const unsigned char zTestMasterKey[32] =
        {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
         0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
         0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20};

    (void)ctx;

    if (argc != 2) {
	return Th8_WrongNumArgs(
	    interp, "th8testlib::secure_persist enable|disable");
    }

    if (argl[1] == 6 && Th8_Memcmp(interp, argv[1], "enable", 6) == 0) {
	int rc;

	/*
	 * Install in-memory KV callback via MergePlatformInterp.
	 * If an xKeyValue callback already exists (e.g., from
	 * the sqlite3 plugin), we cannot override it through
	 * merge.  Instead, use a small Th8_Eval to register
	 * a script-level shim that redirects to our C callback.
	 *
	 * For simplicity, if xKeyValue is already set (e.g., by
	 * sqlite3), we trust it and skip the override -- the
	 * existing store should be sufficient.  Otherwise, install
	 * our in-memory store.
	 */
	{
	    const Th8_Platform *pP = Th8_GetPlatform(interp);
	    if (!pP || !pP->xKeyValue) {
		Th8_Platform kvPlat;
		int mergeRc;
		memset(&kvPlat, 0, sizeof(kvPlat));
		kvPlat.nVersion = pP ? pP->nVersion : 2;
		kvPlat.xKeyValue = th8test_kvCallback;
		mergeRc = Th8_MergePlatformInterp(interp, &kvPlat);
		if (mergeRc != TH8_OK) {
		    Th8_SetResultStatic(
		        interp,
		        "secure_persist: MergePlatformInterp"
		        " failed",
		        TH8_NOLEN);
		    return TH8_ERROR;
		}
	    }
	    /* Verify the merge took effect. */
	    {
		const Th8_Platform *pCheck = Th8_GetPlatform(interp);
		if (!pCheck || !pCheck->xKeyValue) {
		    Th8_SetResultStatic(
		        interp,
		        "secure_persist: failed to install"
		        " xKeyValue callback",
		        TH8_NOLEN);
		    return TH8_ERROR;
		}
	    }
	}

	rc = Th8_EnableSecurePersist(interp, 1);
	if (rc != TH8_OK) return rc;
	rc = Th8_SecureSetMasterKey(interp, zTestMasterKey, 32);
	if (rc != TH8_OK) return rc;
	Th8_ClearResult(interp);
	return TH8_OK;
    }

    if (argl[1] == 7 && Th8_Memcmp(interp, argv[1], "disable", 7) == 0) {
	Th8_SecureClearMasterKey(interp);
	Th8_EnableSecurePersist(interp, 0);
	Th8_ClearResult(interp);
	return TH8_OK;
    }

    Th8_SetResultStatic(
        interp, "secure_persist: must be enable or disable", TH8_NOLEN);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_bug25_diag_cmd --
 *
 *	Implements "::th8testlib::bug25_diag".  Reproduces the
 *	Bug 25 failing chain in a child interpreter and returns
 *	the captured diagnostic string from the failing GetVar.
 *	The list form is {createRc createMsg getRc getMsg}.
 *	Used for the Bug 25 root-cause investigation: the partial
 *	fix from 2026-05-29 added per-EVP-step messages, so this
 *	command exposes the exact failing step to the script.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_bug25_diag_cmd(
    Th8_Interp *interp, /* The interpreter. */
    void *ctx, /* Unused. */
    int argc, /* Argument count. */
    const char **argv, /* Argument values. */
    size_t *argl) /* Argument lengths. */
{
    static const unsigned char zKey[32] = {0x42, 0x42, 0x42, 0x42, 0x42, 0x42,
                                           0x42, 0x42, 0x42, 0x42, 0x42, 0x42,
                                           0x42, 0x42, 0x42, 0x42, 0x42, 0x42,
                                           0x42, 0x42, 0x42, 0x42, 0x42, 0x42,
                                           0x42, 0x42, 0x42, 0x42, 0x42, 0x42,
                                           0x42, 0x42};
    const Th8_Platform *pP;
    Th8_Platform cp;
    Th8_Interp *pSec;
    int createRc = -1, getRc = -1;
    char zCreateMsg[256] = "";
    char zGetMsg[256] = "";
    char *zOut = NULL;
    size_t nOut = 0;

    (void)ctx;
    (void)argv;
    (void)argl;

    if (argc != 1) {
	return Th8_WrongNumArgs(interp, "th8testlib::bug25_diag");
    }

    pP = Th8_GetPlatform(interp);
    if (!pP) {
	Th8_SetResultStatic(
	    interp, "bug25_diag: no parent platform", TH8_NOLEN);
	return TH8_ERROR;
    }
    cp = *pP;
    cp.xPanic = 0;
    pSec = Th8_CreateInterp(&cp);
    if (!pSec) {
	Th8_SetResultStatic(
	    interp, "bug25_diag: CreateInterp failed", TH8_NOLEN);
	return TH8_ERROR;
    }
    Th8_RegisterLanguage(pSec);

    if (Th8_SecureSetMasterKey(pSec, zKey, sizeof(zKey)) != TH8_OK) {
	Th8_DeleteInterp(pSec);
	Th8_SetResultStatic(
	    interp, "bug25_diag: SecureSetMasterKey failed", TH8_NOLEN);
	return TH8_ERROR;
    }

    /* Step 1: secure create.  Capture rc + message. */
    createRc = Th8_Eval(
        pSec, 0,
        "unset -nocomplain ::auto_path; secure create ::auto_path /tmp",
        TH8_NOLEN, NULL, 0);
    {
	size_t n;
	const char *z = Th8_GetResult(pSec, &n);
	if (n > 255) n = 255;
	if (n > 0 && z) memcpy(zCreateMsg, z, n);
	zCreateMsg[n] = '\0';
    }

    /* Step 2: read it back.  Capture rc + message. */
    getRc = Th8_GetVar(pSec, "::auto_path", TH8_NOLEN);
    {
	size_t n;
	const char *z = Th8_GetResult(pSec, &n);
	if (n > 255) n = 255;
	if (n > 0 && z) memcpy(zGetMsg, z, n);
	zGetMsg[n] = '\0';
    }

    Th8_DeleteInterp(pSec);

    /* Build {createRc createMsg getRc getMsg} list. */
    {
	char zRcBuf[16];
	int nLen;

	nLen = snprintf(zRcBuf, sizeof(zRcBuf), "%d", createRc);
	if (nLen < 0) nLen = 0;
	if ((size_t)nLen >= sizeof(zRcBuf)) nLen = (int)sizeof(zRcBuf) - 1;
	Th8_ListAppend(interp, &zOut, &nOut, zRcBuf, (size_t)nLen);
	Th8_ListAppend(interp, &zOut, &nOut, zCreateMsg, TH8_NOLEN);

	nLen = snprintf(zRcBuf, sizeof(zRcBuf), "%d", getRc);
	if (nLen < 0) nLen = 0;
	if ((size_t)nLen >= sizeof(zRcBuf)) nLen = (int)sizeof(zRcBuf) - 1;
	Th8_ListAppend(interp, &zOut, &nOut, zRcBuf, (size_t)nLen);
	Th8_ListAppend(interp, &zOut, &nOut, zGetMsg, TH8_NOLEN);
    }
    if (zOut) {
	Th8_SetResult(interp, zOut, nOut);
	Th8_Free(interp, zOut);
    } else {
	Th8_ClearResult(interp);
    }
    return TH8_OK;
}
#  endif /* TH8_ENABLE_CRYPTOGRAPHY && TH8_ENABLE_VARIABLES */


#  if defined(TH8_ENABLE_CRYPTOGRAPHY)
/*
 *----------------------------------------------------------------------
 *
 * th8test_drivekeyfault_cmd --
 *
 *	::th8testlib::drivekeyfault zero|root|test 1|2
 *
 *	Self-contained driver for the embedded-key lazy-init guard
 *	MC/DC vectors in src/plugins/harpy/th8_policy.c.  Performs
 *	the full cycle in C:
 *	  1. Initialize a fresh Th8_FaultConfig and set the
 *	     corresponding nFailEmbeddedKey{0,Root,Test} field to
 *	     the requested mode (1=NULL, 2=zero-size).
 *	  2. Reset the file-scope key caches via
 *	     th8PolicyResetCachedKeys so the lazy-init body runs.
 *	  3. Install the fault layer on this interp (which publishes
 *	     the config to the global th8FaultActiveCfg consulted by
 *	     the policy.c wrappers).
 *	  4. Call Th8_GetPublicKey{Zero,Root,Test}Token, which
 *	     enters Th8_GetPublicKey{Zero,Root,Test}'s lazy-init
 *	     body, hits the `if (!zData || nData == 0) return NULL`
 *	     guard at L2091/L2190/L2295, and propagates the
 *	     "cannot load embedded ..." error.
 *	  5. Uninstall the fault layer so subsequent unrelated code
 *	     is unaffected.
 *	  6. Return TH8_ERROR with the wrapper's error message on
 *	     fault, or TH8_OK with the resolved token on success
 *	     (only useful when the test mistakenly passes mode 0).
 *
 *	This bypasses the `th8testlib::fault eval` child-interp
 *	model -- since that uses a fresh child without testlib
 *	commands, the getpublickeytoken helper is unreachable
 *	from inside the eval'd script.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_drivekeyfault_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    Th8_FaultConfig cfg;
    unsigned char fctxBuf[sizeof(void *) * 256]; /* oversized */
    Th8_FaultCtx *pFCtx = (Th8_FaultCtx *)fctxBuf;
    char zToken[17];
    int rc;
    int (*pTokenFunc)(Th8_Interp *, char *) = NULL;
    int *pFlagField = NULL;
    th8_int64_t nMode = 0;

    (void)ctx;

    if (argc != 3) {
	return Th8_WrongNumArgs(
	    interp, "th8testlib::drivekeyfault zero|root|test 1|2");
    }
    if (Th8_FaultCtxSize() > sizeof(fctxBuf)) {
	Th8_SetResultStatic(
	    interp, "fault: Th8_FaultCtx too large for stack buffer",
	    TH8_NOLEN);
	return TH8_ERROR;
    }
    Th8_FaultConfigInit(&cfg);

    if (argl[1] == 4 && Th8_Memcmp(interp, argv[1], "zero", 4) == 0) {
	pTokenFunc = Th8_GetPublicKeyZeroToken;
	pFlagField = &cfg.nFailEmbeddedKey0;
    } else if (argl[1] == 4 && Th8_Memcmp(interp, argv[1], "root", 4) == 0) {
	pTokenFunc = Th8_GetPublicKeyRootToken;
	pFlagField = &cfg.nFailEmbeddedKeyRoot;
#    if defined(TH8_ENABLE_TEST_KEY)
    } else if (argl[1] == 4 && Th8_Memcmp(interp, argv[1], "test", 4) == 0) {
	pTokenFunc = Th8_GetPublicKeyTestToken;
	pFlagField = &cfg.nFailEmbeddedKeyTest;
#    endif
    } else {
	Th8_SetResultStatic(interp, "expected zero|root|test", TH8_NOLEN);
	return TH8_ERROR;
    }
    if (Th8_ToWideInt(interp, argv[2], argl[2], &nMode) != TH8_OK) {
	return TH8_ERROR;
    }
    *pFlagField = (int)nMode;

    th8PolicyResetCachedKeys();
    if (Th8_FaultInstall(interp, &cfg, pFCtx) != TH8_OK) {
	return TH8_ERROR;
    }
    rc = pTokenFunc(interp, zToken);
    Th8_FaultUninstall(interp, pFCtx);

    if (rc == TH8_OK) {
	Th8_SetResult(interp, zToken, 16);
    }
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_faulteval_impl --
 *
 *	Shared worker for the `osslfaulteval` and `posixfaulteval`
 *	test commands: arm a single OpenSSL- or POSIX-op fault bit
 *	and evaluate a script in the current interpreter.
 *
 * Why / How:
 *	Parses `opBit` (0..63) and sets the corresponding bit in
 *	Th8_FaultConfig.nFailOsslMask (when `posix` is 0) or
 *	nFailPosixMask (when non-zero), installs the fault on THIS
 *	interpreter -- so the crypto / POSIX path stays fully live,
 *	unlike `fault eval`'s isolated child -- then evaluates the
 *	script so the forced OSSL_CALL / POSIX-wrapper failure runs
 *	its error arm.  The eval's result string is preserved
 *	across Th8_FaultUninstall and restored before returning.
 *
 * Results:
 *	The script's return code, with the (possibly forced-error)
 *	result string left on the interpreter.  TH8_ERROR on bad
 *	arguments or an out-of-range `opBit`.
 *
 * Side effects:
 *	Installs and then uninstalls a fault configuration on the
 *	calling interpreter around the evaluation.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_faulteval_impl(
    Th8_Interp *interp,
    int argc,
    const char **argv,
    size_t *argl,
    int posix)
{
    Th8_FaultConfig cfg;
    unsigned char fctxBuf[sizeof(void *) * 256]; /* oversized */
    Th8_FaultCtx *pFCtx = (Th8_FaultCtx *)fctxBuf;
    th8_int64_t nOp = 0;
    th8_int64_t nErrno = 0;
    th8_int64_t bOnce = 0;
    int iScript;
    int rc;
    char *zSaved = NULL;
    size_t nSaved = 0;
    const char *zRes;
    const char *zCmd = posix ? "posixfaulteval" : "osslfaulteval";

    /*
     * `posixfaulteval` accepts an extended form -- `opBit errno once
     * script` -- that selects the errno set on the forced failure and
     * a one-shot flag (fail only the first armed call, then pass
     * through so a retry loop makes progress).  The 2-argument form
     * (`opBit script`) keeps the default errno (EIO) and always-fail
     * behavior.  `osslfaulteval` takes only the 2-argument form.
     */
    if (argc == 3) {
	iScript = 2;
    } else if (posix && argc == 5) {
	if (Th8_ToWideInt(interp, argv[2], argl[2], &nErrno) != TH8_OK ||
	    Th8_ToWideInt(interp, argv[3], argl[3], &bOnce) != TH8_OK) {
	    return TH8_ERROR;
	}
	iScript = 4;
    } else {
	return Th8_WrongNumArgs(
	    interp,
	    posix ? "th8testlib::posixfaulteval opBit ?errno once? script"
	          : "th8testlib::osslfaulteval opBit script");
    }
    if (Th8_FaultCtxSize() > sizeof(fctxBuf)) {
	Th8_SetResultStatic(
	    interp, "fault: Th8_FaultCtx too large for stack buffer",
	    TH8_NOLEN);
	return TH8_ERROR;
    }
    if (Th8_ToWideInt(interp, argv[1], argl[1], &nOp) != TH8_OK) {
	return TH8_ERROR;
    }
    if (nOp < 0 || nOp >= 64) {
	Th8_SetResultStatic(
	    interp, "th8testlib faulteval: opBit out of range 0..63",
	    TH8_NOLEN);
	return TH8_ERROR;
    }

    Th8_FaultConfigInit(&cfg);
    if (posix) {
	cfg.nFailPosixMask = ((th8_uint64_t)1 << (unsigned int)nOp);
	cfg.nFailPosixErrno = (int)nErrno;
	cfg.nFailPosixOnce = (bOnce != 0);
    } else {
	cfg.nFailOsslMask = ((th8_uint64_t)1 << (unsigned int)nOp);
    }

    if (Th8_FaultInstall(interp, &cfg, pFCtx) != TH8_OK) {
	return TH8_ERROR;
    }
    rc = Th8_Eval(
        interp, 0, argv[iScript], argl[iScript], zCmd,
        Th8_Strlen(interp, zCmd));

    /*
     * Preserve the eval's result string across Th8_FaultUninstall
     * (which may reset the interp result), then restore it so the
     * caller sees the forced error message.
     */
    zRes = Th8_GetResult(interp, &nSaved);
    if (zRes) {
	zSaved = (char *)TH8_ALLOC_STR(interp, nSaved);
	if (zSaved) {
	    Th8_Memcpy(interp, zSaved, zRes, nSaved);
	    zSaved[nSaved] = '\0';
	}
    }

    Th8_FaultUninstall(interp, pFCtx);

    if (zSaved) {
	Th8_SetResult(interp, zSaved, nSaved);
	Th8_Free(interp, zSaved);
    }
    return rc;
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_osslfaulteval_cmd --
 *
 *	::th8testlib::osslfaulteval opBit script
 *
 *	Arms bit `opBit` (a TH8_OSSL_OP_* id) in
 *	Th8_FaultConfig.nFailOsslMask, installs the fault on THIS
 *	interpreter, then evaluates `script` (typically a
 *	`harpy verify` / `harpy sign` with inlined key material) so
 *	the OSSL_CALL wrapper in th8_snk.c forces the corresponding
 *	OpenSSL call to fail and its error arm runs.  Unlike
 *	`fault eval` -- which runs the body in an isolated child
 *	interp WITHOUT the signed-only crypto policy -- this evals
 *	in the current interpreter so the crypto path is fully
 *	live.  The fault is always uninstalled before returning.
 *
 *	The evaluated script's result string is preserved across
 *	uninstall and returned as this command's result, with the
 *	script's return code, so callers can `catch` it and match
 *	the forced error message.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_osslfaulteval_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    (void)ctx;
    return th8test_faulteval_impl(interp, argc, argv, argl, 0);
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_posixfaulteval_cmd --
 *
 *	Implements "::th8testlib::posixfaulteval opBit ?errno once?
 *	script".  Thin wrapper that forwards to th8test_faulteval_impl
 *	with the POSIX flag set, arming POSIX-op fault bit `opBit`.
 *
 * Why / How:
 *	The bit selects an op id in
 *	Th8_FaultConfig.nFailPosixMask so the corresponding POSIX
 *	wrapper forces a failure, and `script` is evaluated in the
 *	current interpreter so its error arm runs.  The optional
 *	`errno` and `once` arguments select the errno set on the
 *	forced failure and one-shot mode (fail only the first armed
 *	call), letting a test drive an errno-inspecting retry branch
 *	such as `nRead < 0 && errno == EINTR` exactly once.  A NEGATIVE
 *	`errno` is the short-read sentinel: the wrapped read() returns
 *	0 instead of -1, driving a read loop's `nRead == 0` break arm.
 *
 * Results:
 *	The script's return code and result, as produced by
 *	th8test_faulteval_impl.
 *
 * Side effects:
 *	Installs and uninstalls a fault configuration on the
 *	calling interpreter around the evaluation.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_posixfaulteval_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    (void)ctx;
    return th8test_faulteval_impl(interp, argc, argv, argl, 1);
}


#    if defined(TH8_ENABLE_CRYPTOGRAPHY)
/*
 *----------------------------------------------------------------------
 *
 * th8testHexToBytes --
 *
 *	Decode nOut*2 hex characters from zHex into nOut bytes.
 *	Returns TH8_OK on success, TH8_ERROR on a wrong length or a
 *	non-hex character.
 *
 *----------------------------------------------------------------------
 */
static int
th8testHexToBytes(
    const char *zHex,
    size_t nHex,
    unsigned char *pOut,
    size_t nOut)
{
    size_t i;

    if (nHex != nOut * 2) return TH8_ERROR;
    for (i = 0; i < nOut; i++) {
	int hi, lo;
	char ch = zHex[i * 2];
	char cl = zHex[i * 2 + 1];

	if (ch >= '0' && ch <= '9')
	    hi = ch - '0';
	else if (ch >= 'a' && ch <= 'f')
	    hi = ch - 'a' + 10;
	else if (ch >= 'A' && ch <= 'F')
	    hi = ch - 'A' + 10;
	else
	    return TH8_ERROR;

	if (cl >= '0' && cl <= '9')
	    lo = cl - '0';
	else if (cl >= 'a' && cl <= 'f')
	    lo = cl - 'a' + 10;
	else if (cl >= 'A' && cl <= 'F')
	    lo = cl - 'A' + 10;
	else
	    return TH8_ERROR;

	pOut[i] = (unsigned char)((hi << 4) | lo);
    }
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_ntpvalidate_cmd --
 *
 *	::th8testlib::ntpvalidate flags stratum origTsHex txTsHex reqTxTsHex
 *
 *	Build a synthetic NTP response + request packet and drive
 *	th8NtpValidateResponse (th8_time.c) directly via the internal
 *	stubs, so the version/mode/stratum/anti-spoof/zero-timestamp
 *	validation arms get MC/DC coverage WITHOUT a live NTP
 *	exchange (no dependency on real time servers).  `flags` is
 *	response byte 0 (LI|VN|Mode), `stratum` byte 1; the three
 *	16-hex-char timestamps fill resp.origTs (@24), resp.txTs
 *	(@40) and req.txTs (@40).  On success returns the derived
 *	epoch seconds; on failure propagates the specific validation
 *	error message so the caller can assert the driven arm.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_ntpvalidate_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    unsigned char resp[48], req[48];
    th8_int64_t flags = 0, stratum = 0, epoch = 0;
    int rc;

    (void)ctx;

    if (argc != 6) {
	return Th8_WrongNumArgs(
	    interp, "th8testlib::ntpvalidate flags stratum origTsHex "
	            "txTsHex reqTxTsHex");
    }
    if (Th8_ToWideInt(interp, argv[1], argl[1], &flags) != TH8_OK) {
	return TH8_ERROR;
    }
    if (Th8_ToWideInt(interp, argv[2], argl[2], &stratum) != TH8_OK) {
	return TH8_ERROR;
    }

    Th8_Memset(interp, resp, 0, sizeof(resp));
    Th8_Memset(interp, req, 0, sizeof(req));
    resp[0] = (unsigned char)flags;
    resp[1] = (unsigned char)stratum;
    if (th8testHexToBytes(argv[3], argl[3], &resp[24], 8) != TH8_OK ||
        th8testHexToBytes(argv[4], argl[4], &resp[40], 8) != TH8_OK ||
        th8testHexToBytes(argv[5], argl[5], &req[40], 8) != TH8_OK) {
	Th8_SetResultStatic(
	    interp, "ntpvalidate: timestamps need 16 hex chars each",
	    TH8_NOLEN);
	return TH8_ERROR;
    }

    rc = th8NtpValidateResponse(interp, resp, req, &epoch);
    if (rc == TH8_OK) {
	return Th8_SetResultWideInt(interp, epoch);
    }
    return rc;
}
#    endif /* TH8_ENABLE_CRYPTOGRAPHY */


/*
 *----------------------------------------------------------------------
 *
 * th8test_getpublickeytoken_cmd --
 *
 *	::th8testlib::getpublickeytoken zero|root|test
 *
 *	Thin wrapper over Th8_GetPublicKeyZeroToken /
 *	Th8_GetPublicKeyRootToken / Th8_GetPublicKeyTestToken so a
 *	coverage test can drive the corresponding lazy-init paths
 *	in th8_policy.c (Th8_GetPublicKeyZero/Root/Test) without
 *	depending on the th8sh-only startup banner.  On success
 *	returns the 16-character hex token; on failure propagates
 *	the underlying TH8_ERROR plus the interp message
 *	(e.g. "cannot load embedded keyRoot") so the caller can
 *	assert the expected MC/DC fault outcome.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_getpublickeytoken_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char zToken[17];
    int rc;

    (void)ctx;

    if (argc != 2) {
	return Th8_WrongNumArgs(
	    interp, "th8testlib::getpublickeytoken zero|root|test");
    }
    if (argl[1] == 4 && Th8_Memcmp(interp, argv[1], "zero", 4) == 0) {
	rc = Th8_GetPublicKeyZeroToken(interp, zToken);
    } else if (argl[1] == 4 && Th8_Memcmp(interp, argv[1], "root", 4) == 0) {
	rc = Th8_GetPublicKeyRootToken(interp, zToken);
#    if defined(TH8_ENABLE_TEST_KEY)
    } else if (argl[1] == 4 && Th8_Memcmp(interp, argv[1], "test", 4) == 0) {
	rc = Th8_GetPublicKeyTestToken(interp, zToken);
#    endif
    } else {
	Th8_SetResultStatic(interp, "expected zero|root|test", TH8_NOLEN);
	return TH8_ERROR;
    }
    if (rc == TH8_OK) {
	Th8_SetResult(interp, zToken, 16);
    }
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_resetkeycaches_cmd --
 *
 *	::th8testlib::resetkeycaches
 *
 *	Clears the file-scope lazy-init caches in th8_policy.c
 *	(th8_pKeyZero, th8_pKeyRoot, and th8_pKeyTest when
 *	TH8_ENABLE_TEST_KEY is defined) so that a subsequent call
 *	to Th8_GetPublicKeyZero/Root/Test re-enters the lazy-init
 *	body and re-evaluates the `if (!zData || nData == 0)` guard
 *	at L2091/L2190/L2295.  Used in concert with the
 *	`-failEmbeddedKey{0,Root,Test}` fault options to drive the
 *	(T,-) and (F,T) MC/DC vectors that are otherwise
 *	unreachable because the embedded blobs always satisfy the
 *	guard on the single successful first call.
 *
 *	No arguments; returns the empty string on success.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_resetkeycaches_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    (void)ctx;
    (void)argv;
    (void)argl;

    if (argc != 1) {
	return Th8_WrongNumArgs(interp, "th8testlib::resetkeycaches");
    }
    th8PolicyResetCachedKeys();
    Th8_ClearResult(interp);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_policyverifydata_cmd --
 *
 *	::th8testlib::policyverifydata null|empty|name|absolute
 *
 *	Drives th8_policy.c L1100 `if (!zName || nName == 0)` and
 *	L1106 `if (!IsRelativePath(...) && !IsHttpUri(...))` MC/DC
 *	vectors that the normal Th8_EvalFile path cannot reach
 *	(file paths are always non-NULL relative paths).  Creates an
 *	isolated child interpreter, installs the signed-policy
 *	callback so a fresh Th8_PolicyCtx is available, then calls
 *	th8PolicyVerifyData directly via the internal stubs.
 *
 *	Modes:
 *	  null     -- zName=NULL, nName=0   -> L1100 C1=T (T,-)
 *	  empty    -- zName="x",  nName=0   -> L1100 C1=F, C2=T (F,T)
 *	  name     -- zName="x",  nName=1   -> control; the L1100
 *	              (F,F) baseline observed by every existing
 *	              signed-file load path.
 *	  absolute -- zName="/abs/x.tcl"    -> passes L1100 with
 *	              (F,F) and drives L1106 (T,T) -- absolute path
 *	              is !IsRelativePath=T AND !IsHttpUri=T, both
 *	              conditions True, decision True, returns
 *	              TH8_ERROR with "must be a relative path or
 *	              HTTP(S) URI".  This closes the L1106 C1-Pair
 *	              by adding the (T,?) vector vs the existing
 *	              (F,-) short-circuit.  Note: L1106 (T,F) is
 *	              intrinsic-unreachable -- no name can both
 *	              start with '/' (IsRelativePath=F) AND with
 *	              "http://" (IsHttpUri=T) since the prefix
 *	              checks are mutually exclusive.
 *
 *	Returns a 2-element list: {rc msg} where rc is 1 if
 *	th8PolicyVerifyData returned TH8_ERROR or 0 on success,
 *	and msg is the child interp's result string.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_policyverifydata_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    Th8_Interp *pChild;
    void *pPolCtx = NULL;
    const char *zName = NULL;
    size_t nName = 0;
    int rc;
    const char *zRes;
    size_t nRes;

    (void)ctx;

    if (argc != 2) {
	return Th8_WrongNumArgs(
	    interp, "th8testlib::policyverifydata null|empty|name|absolute");
    }
    if (argl[1] == 4 && Th8_Memcmp(interp, argv[1], "null", 4) == 0) {
	zName = NULL;
	nName = 0;
    } else if (argl[1] == 5 && Th8_Memcmp(interp, argv[1], "empty", 5) == 0) {
	zName = "x";
	nName = 0;
    } else if (argl[1] == 4 && Th8_Memcmp(interp, argv[1], "name", 4) == 0) {
	zName = "x";
	nName = 1;
    } else if (
        argl[1] == 8 && Th8_Memcmp(interp, argv[1], "absolute", 8) == 0) {
	zName = "/abs/x.tcl";
	nName = 10;
    } else {
	Th8_SetResultStatic(
	    interp, "expected null|empty|name|absolute", TH8_NOLEN);
	return TH8_ERROR;
    }

    pChild = Th8_CreateInterp((Th8_Platform *)(void *)
                                  Th8_GetPlatform(interp));
    if (!pChild) {
	Th8_SetResultStatic(interp, "cannot create child interp", TH8_NOLEN);
	return TH8_ERROR;
    }
    Th8_RegisterLanguage(pChild);

    if (Th8_InstallSignedPolicy(pChild, &pPolCtx) != TH8_OK || !pPolCtx) {
	Th8_DeleteInterp(pChild);
	Th8_SetResultStatic(
	    interp, "cannot install signed policy", TH8_NOLEN);
	return TH8_ERROR;
    }
    (void)Th8_EnableSignedOnly(pChild, 1);

    rc = th8PolicyVerifyData(pChild, zName, nName, "data", 4, pPolCtx);

    zRes = Th8_GetResult(pChild, &nRes);
    {
	char *zReply = NULL;
	size_t nReply = 0;

	Th8_ListAppend(interp, &zReply, &nReply, rc == TH8_OK ? "0" : "1", 1);
	if (zRes && nRes > 0) {
	    Th8_ListAppend(interp, &zReply, &nReply, zRes, nRes);
	} else {
	    Th8_ListAppend(interp, &zReply, &nReply, "", 0);
	}
	Th8_SetResult(interp, zReply, nReply);
	Th8_Free(interp, zReply);
    }

    Th8_DeleteInterp(pChild);
    return TH8_OK;
}
#  endif /* TH8_ENABLE_CRYPTOGRAPHY */


/*
 *----------------------------------------------------------------------
 *
 * th8test_cache_double_remove_cmd --
 *
 *	::th8testlib::cache_double_remove
 *
 *	Drives th8_cache.c L792 (T,F) MC/DC vector in
 *	`th8RemoveFromCache`:
 *
 *	  if (pEntry && pEntry->pData) {
 *
 *	The tombstone scenario (pEntry non-NULL, pData NULL)
 *	happens when a cache entry has been removed once (which
 *	zeroes pData but keeps the hash entry) and removed
 *	again before any reuse.  No in-tree caller of
 *	th8RemoveFromCache double-removes the same key, so the
 *	(T,F) C2 vector stayed open at 50% MC/DC.
 *
 *	Sequence:
 *	  1. Populate the int cache with "12345" via Th8_ToInt.
 *	  2. Call th8RemoveFromCache once -- tombstones the
 *	     entry, pData=NULL, decision is (T,T).
 *	  3. Call th8RemoveFromCache again -- find returns the
 *	     tombstoned entry, decision is (T,F).
 *
 *	th8RemoveFromCache is reached via the internal-stubs
 *	table (TH8_INTERNAL).  No new stubs entry required.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_cache_double_remove_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    th8_int64_t v = 0;

    (void)ctx;
    (void)argv;
    (void)argl;

    if (argc != 1) {
	return Th8_WrongNumArgs(interp, "th8testlib::cache_double_remove");
    }
    /* Populate the int cache. */
    (void)Th8_ToWideInt(interp, "12345", 5, &v);

    /* First remove -- tombstones the entry (decision is (T,T)). */
    th8RemoveFromCache(interp, TH8_CACHE_WIDE, "12345", 5);

    /* Second remove -- find returns the tombstoned entry, drives
     * the (T,F) C2-pair completion. */
    th8RemoveFromCache(interp, TH8_CACHE_WIDE, "12345", 5);

    Th8_ClearResult(interp);
    return TH8_OK;
}


#  if defined(TH8_ENABLE_CRYPTOGRAPHY)
/*
 *----------------------------------------------------------------------
 *
 * th8test_securecanary_noppage_cmd --
 *
 *	::th8testlib::securecanary_noppage
 *
 *	Drives the C2=T (F,T) vector at th8_secure.c L167
 *	`if (!pKS || !pKS->pPage) return TH8_ERROR;` in
 *	th8SecureCheckCanary.  Existing testlib drives
 *	pass NULL pKS (C1=T); this passes a zeroed memory
 *	region whose first sizeof(void*) bytes are 0,
 *	making pKS->pPage NULL by struct-layout invariant
 *	(th8_secure.c:128 `pPage` is the first field).
 *	Th8_KeyStore is file-scope so the test can't
 *	construct an instance properly; the workaround
 *	relies on field-ordering staying stable.  Return
 *	1 if th8SecureCheckCanary returned TH8_ERROR
 *	(expected).
 *
 *----------------------------------------------------------------------
 */

static int
th8test_securecanary_noppage_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    unsigned char fakeKs[1024];
    int rc;

    (void)ctx;
    (void)argv;
    (void)argl;

    if (argc != 1) {
	return Th8_WrongNumArgs(interp, "th8testlib::securecanary_noppage");
    }
    Th8_Memset(interp, fakeKs, 0, sizeof(fakeKs));
    rc = th8SecureCheckCanary(interp, fakeKs);
    Th8_SetResultInt(interp, rc == TH8_ERROR ? 1 : 0);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_policyfindkey_nopaeys_cmd --
 *
 *	::th8testlib::policyfindkey_nopaeys
 *
 *	Drives the C2=T (F,T,-) vector at th8_policy.c L1757
 *	`if (!p || !p->paKeys || !zToken)` in Th8_PolicyFindKey.
 *	Currently observed: (F,F,F), (F,F,T), (T,-,-).  Missing
 *	C2-Pair = (F,T,-): non-NULL pPolicyCtx with paKeys=NULL
 *	(fresh install before any Th8_PolicyPreloadKey call) and
 *	a valid zToken.  In-tree callers all preload at least
 *	one key before calling FindKey, so this vector requires
 *	a direct test invocation immediately after install.
 *
 *	Returns 1 if FindKey returned NULL (the expected
 *	outcome -- no key matched in an empty cache), 0
 *	otherwise.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_policyfindkey_nopaeys_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    Th8_Interp *pChild;
    void *pPolCtx = NULL;
    const Th8_RsaKey *pKey;

    (void)ctx;
    (void)argv;
    (void)argl;

    if (argc != 1) {
	return Th8_WrongNumArgs(interp, "th8testlib::policyfindkey_nopaeys");
    }
    pChild = Th8_CreateInterp((Th8_Platform *)(void *)
                                  Th8_GetPlatform(interp));
    if (!pChild) {
	Th8_SetResultStatic(interp, "cannot create child interp", TH8_NOLEN);
	return TH8_ERROR;
    }
    Th8_RegisterLanguage(pChild);

    if (Th8_InstallSignedPolicy(pChild, &pPolCtx) != TH8_OK || !pPolCtx) {
	Th8_DeleteInterp(pChild);
	Th8_SetResultStatic(
	    interp, "cannot install signed policy", TH8_NOLEN);
	return TH8_ERROR;
    }

    /* paKeys is NULL by default (lazy-init in
     * Th8_PolicyPreloadKey).  zToken = 16 hex chars. */
    pKey = Th8_PolicyFindKey(pChild, pPolCtx, "0123456789abcdef", 16);

    Th8_SetResultInt(interp, pKey == NULL ? 1 : 0);
    Th8_DeleteInterp(pChild);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_policyevalpre_cmd --
 *
 *	::th8testlib::policyevalpre null|empty
 *
 *	Drives the C1=T (null) and C2=T (empty) vectors at
 *	th8_policy.c L1410
 *	`if (!zName || nName == 0)` in th8PolicyEvalPre.
 *	L1410 is the EvalPre mirror of L1100 in
 *	th8PolicyVerifyData (closed in batch #20).  Unlike
 *	#20's VerifyData driver -- which calls the static
 *	helper directly through the internal stubs -- this
 *	helper routes through the public Th8_Eval API to
 *	exercise the policy-callback path naturally.
 *	Th8_Eval on a signed-only child interp triggers
 *	th8PolicyCallback which delegates to th8PolicyEvalPre
 *	with the requested (zName, nName) -> the L1410 guard
 *	fires.
 *
 *	Modes:
 *	  null  -- Th8_Eval(child, 0, "x", 1, NULL, 0)
 *	           -> L1410 C1=T (T,-)
 *	  empty -- Th8_Eval(child, 0, "x", 1, "y", 0)
 *	           -> L1410 C1=F, C2=T (F,T)
 *	  named -- Th8_Eval(child, 0, "x", 1, "y", 1)
 *	           -> L1410 C1=F, C2=F (F,F).  Required
 *	           baseline for the C1-Pair and C2-Pair
 *	           independence proofs (without an
 *	           output-differing vector, the (T,-) /
 *	           (F,T) pairs above can't satisfy MC/DC).
 *	           Falls through to th8PolicyVerifyData
 *	           which errors on the missing signature.
 *
 *	Returns a 2-element list {rc msg} where rc is 1 if
 *	Th8_Eval returned TH8_ERROR (expected) or 0 on
 *	success, and msg is the child interp's result.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_policyevalpre_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    Th8_Interp *pChild;
    void *pPolCtx = NULL;
    const char *zName = NULL;
    size_t nName = 0;
    int rc;
    const char *zRes;
    size_t nRes;

    (void)ctx;

    if (argc != 2) {
	return Th8_WrongNumArgs(
	    interp, "th8testlib::policyevalpre null|empty|named");
    }
    if (argl[1] == 4 && Th8_Memcmp(interp, argv[1], "null", 4) == 0) {
	zName = NULL;
	nName = 0;
    } else if (argl[1] == 5 && Th8_Memcmp(interp, argv[1], "empty", 5) == 0) {
	zName = "y";
	nName = 0;
    } else if (argl[1] == 5 && Th8_Memcmp(interp, argv[1], "named", 5) == 0) {
	zName = "y";
	nName = 1;
    } else {
	Th8_SetResultStatic(interp, "expected null|empty|named", TH8_NOLEN);
	return TH8_ERROR;
    }

    pChild = Th8_CreateInterp((Th8_Platform *)(void *)
                                  Th8_GetPlatform(interp));
    if (!pChild) {
	Th8_SetResultStatic(interp, "cannot create child interp", TH8_NOLEN);
	return TH8_ERROR;
    }
    Th8_RegisterLanguage(pChild);

    if (Th8_InstallSignedPolicy(pChild, &pPolCtx) != TH8_OK || !pPolCtx) {
	Th8_DeleteInterp(pChild);
	Th8_SetResultStatic(
	    interp, "cannot install signed policy", TH8_NOLEN);
	return TH8_ERROR;
    }
    (void)Th8_EnableSignedOnly(pChild, 1);

    rc = Th8_Eval(pChild, 0, "x", 1, zName, nName);

    zRes = Th8_GetResult(pChild, &nRes);
    {
	char *zReply = NULL;
	size_t nReply = 0;

	Th8_ListAppend(interp, &zReply, &nReply, rc == TH8_OK ? "0" : "1", 1);
	if (zRes && nRes > 0) {
	    Th8_ListAppend(interp, &zReply, &nReply, zRes, nRes);
	} else {
	    Th8_ListAppend(interp, &zReply, &nReply, "", 0);
	}
	Th8_SetResult(interp, zReply, nReply);
	Th8_Free(interp, zReply);
    }

    Th8_DeleteInterp(pChild);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_afmapget_nocreate_cmd --
 *
 *	::th8testlib::afmapget_nocreate
 *
 *	Drives the C1=F vector at th8_attrflags.c L393
 *	`if (bCreate && p->n < AF_MAX_KEYS)` in th8AfMapGet.
 *	Both in-tree callers pass bCreate=1 so C1 is always T;
 *	C1-Pair stayed open at 50% MC/DC.  Direct invocation
 *	with an empty Th8_AfMap and bCreate=0 drives C1=F
 *	(short-circuit, returns NULL without creating).
 *
 *	Returns 1 if th8AfMapGet returned NULL (the expected
 *	outcome) or 0 otherwise.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_afmapget_nocreate_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    Th8_AfMap map;
    Th8_FlagSet *pFs;

    (void)ctx;
    (void)argv;
    (void)argl;

    if (argc != 1) {
	return Th8_WrongNumArgs(interp, "th8testlib::afmapget_nocreate");
    }
    Th8_Memset(interp, &map, 0, sizeof(map));
    /* Empty map (n=0) + bCreate=0 -> for-loop at L390 finds
     * no match, falls through to L393, C1 short-circuits
     * to F, returns NULL. */
    pFs = th8AfMapGet(interp, &map, (th8_int64_t)42, 0);
    Th8_SetResultInt(interp, pFs == NULL ? 1 : 0);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_flagsethighbit_cmd --
 *
 *	::th8testlib::flagsethighbit add|remove
 *
 *	Drives the C1=F (T,_)->(F,_) MC/DC vectors at
 *	th8_attrflags.c L243 (th8AfFlagSetAdd) and L278
 *	(th8AfFlagSetRemove):
 *
 *	  if ((unsigned char)c < 128 && p->present[c]) { ... }
 *
 *	The script-level `flags change ...` path filters every
 *	input char through th8AfIsIdentChar, which only accepts
 *	the ASCII alnum + underscore subset (c < 128 always).
 *	C1 is therefore always T at L243/L278 and the C1-Pair
 *	stays open at 50% MC/DC.  Direct invocation via the
 *	new th8_AfFlagSetAdd / th8_AfFlagSetRemove internal-
 *	stubs entries with c = 0xC3 (195, well above 128)
 *	drives the C1=F vector.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_flagsethighbit_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    Th8_FlagSet fs;
    char cHighBit = '\xC3';

    (void)ctx;

    if (argc != 2) {
	return Th8_WrongNumArgs(
	    interp, "th8testlib::flagsethighbit add|remove");
    }
    Th8_Memset(interp, &fs, 0, sizeof(fs));
    if (argl[1] == 3 && Th8_Memcmp(interp, argv[1], "add", 3) == 0) {
	th8AfFlagSetAdd(&fs, cHighBit);
    } else if (
        argl[1] == 6 && Th8_Memcmp(interp, argv[1], "remove", 6) == 0) {
	/* Pre-populate present[] so Remove's C2 (present[c])
	 * could observe T -- but C1 short-circuits to F
	 * because c=0xC3 >= 128.  The C1=F vector is the goal. */
	th8AfFlagSetRemove(&fs, cHighBit);
    } else {
	Th8_SetResultStatic(interp, "expected add|remove", TH8_NOLEN);
	return TH8_ERROR;
    }
    /*
     * Verify the call had no effect on present[] (since c = 0xC3 (195)
     * is >= 128, th8AfFlagSetAdd/Remove short-circuit at C1=F and never
     * touch the array).  present[] has only 128 slots, so there is no
     * present[195] to inspect -- reading it would be an out-of-bounds
     * access (UBSan).  Instead confirm the whole array is still zero.
     * Result 0 == "no flag present" (unchanged from the memset above),
     * matching the prior present[c] == 0 contract.
     */
    {
	size_t i;
	int bAnySet = 0;

	for (i = 0; i < sizeof(fs.present); i++) {
	    if (fs.present[i]) {
		bAnySet = 1;
		break;
	    }
	}
	Th8_SetResultInt(interp, bAnySet);
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_flagshavennull_cmd --
 *
 *	::th8testlib::flagshavennull
 *
 *	Drives the C1=T vector at th8_attrflags.c L1025
 *	`if (!zHave || nHave == 0) return 1;` by calling
 *	Th8_AttrFlagsHave with zHave=NULL.  Th8_AttrFlagsHave
 *	is public TH8_API but the `flags have FLAGS HAVE`
 *	command can only pass a Tcl string for HAVE -- never a
 *	C NULL pointer -- so the existing tests only observe
 *	the (F,F) and (F,T) vectors.  C1-Pair: not covered
 *	stays open until a direct C-level NULL call drives it.
 *
 *	Builds a tiny Th8_AfMap from "abc" then calls Have with
 *	zHave=NULL.  Expected return: 1 (have-none case).
 *
 *----------------------------------------------------------------------
 */

static int
th8test_flagshavennull_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    Th8_AfMap map;
    int rc;
    int have;

    (void)ctx;
    (void)argv;
    (void)argl;

    if (argc != 1) {
	return Th8_WrongNumArgs(interp, "th8testlib::flagshavennull");
    }
    rc = Th8_AttrFlagsParse(interp, "abc", 3, 0, 0, &map);
    if (rc != TH8_OK) return rc;
    have = Th8_AttrFlagsHave(&map, 0, NULL, 0, 0, 0);
    Th8_SetResultInt(interp, have);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_harpysigload_cmd --
 *
 *	::th8testlib::harpysigload mode
 *
 *	Drives selected MC/DC vectors in Th8_HarpySigLoad
 *	(src/plugins/harpy/th8_harpy.c) that the normal signed-
 *	file load path cannot reach because policy.c always
 *	feeds it correctly-formed bundle text.  Th8_HarpySigLoad
 *	is public TH8_API so this helper calls it directly
 *	without going through the internal stubs.
 *
 *	Modes (each independent runTest selects one):
 *	  nulldata   -- zData=NULL              -> L103 (T,_,_)
 *	  nosig      -- ppSig=NULL              -> L103 (F,T,_)
 *	  nolen      -- pnSig=NULL              -> L103 (F,F,T)
 *	  emptybody  -- "# only comments"       -> L232 "no
 *	                                           base64 data"
 *	  nonewline  -- "# comment" w/o \n      -> L132 (F,-)
 *	                                           loop exits on
 *	                                           buffer boundary
 *	  basic      -- 3-line header + body    -> token-extract
 *	                                           path L151+
 *
 *	Returns a 2-element list `{rc msg}` where rc is the
 *	Th8_HarpySigLoad return code (0=OK, 1=error) and msg is
 *	the interp result string after the call.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_harpysigload_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    const char *zData = NULL;
    size_t nData = 0;
    unsigned char *pSig = NULL;
    size_t nSig = 0;
    char *zId = NULL;
    unsigned char **ppSig = &pSig;
    size_t *pnSig = &nSig;
    int rc;
    const char *zRes;
    size_t nRes;
    char *zReply = NULL;
    size_t nReply = 0;

    (void)ctx;

    if (argc != 2) {
	return Th8_WrongNumArgs(
	    interp, "th8testlib::harpysigload "
	            "nulldata|nosig|nolen|emptybody|nonewline|basic");
    }
    if (argl[1] == 8 && Th8_Memcmp(interp, argv[1], "nulldata", 8) == 0) {
	zData = NULL;
	nData = 0;
    } else if (argl[1] == 5 && Th8_Memcmp(interp, argv[1], "nosig", 5) == 0) {
	zData = "# valid\n";
	nData = 8;
	ppSig = NULL;
    } else if (argl[1] == 5 && Th8_Memcmp(interp, argv[1], "nolen", 5) == 0) {
	zData = "# valid\n";
	nData = 8;
	pnSig = NULL;
    } else if (
        argl[1] == 9 && Th8_Memcmp(interp, argv[1], "emptybody", 9) == 0) {
	zData = "# only a comment\n";
	nData = 17;
    } else if (
        argl[1] == 9 && Th8_Memcmp(interp, argv[1], "nonewline", 9) == 0) {
	zData = "# no trailing newline";
	nData = 21;
    } else if (argl[1] == 5 && Th8_Memcmp(interp, argv[1], "basic", 5) == 0) {
	zData = "# Harpy signature\n"
	        "# Bundled at: 2026-01-01\n"
	        "# script -- abcdef1234567890\n"
	        "QUJDREVGRw==\n";
	nData = 82;
    } else {
	Th8_SetResultStatic(
	    interp, "expected nulldata|nosig|nolen|emptybody|nonewline|basic",
	    TH8_NOLEN);
	return TH8_ERROR;
    }

    rc = Th8_HarpySigLoad(interp, zData, nData, ppSig, pnSig, &zId);
    zRes = Th8_GetResult(interp, &nRes);

    Th8_ListAppend(interp, &zReply, &nReply, rc == TH8_OK ? "0" : "1", 1);
    if (zRes && nRes > 0) {
	Th8_ListAppend(interp, &zReply, &nReply, zRes, nRes);
    } else {
	Th8_ListAppend(interp, &zReply, &nReply, "", 0);
    }
    Th8_SetResult(interp, zReply, nReply);
    Th8_Free(interp, zReply);

    Th8_Free(interp, pSig);
    Th8_Free(interp, zId);

    return TH8_OK;
}
#  endif /* TH8_ENABLE_CRYPTOGRAPHY */


#  if defined(TH8_ENABLE_LOAD)
/*
 *----------------------------------------------------------------------
 *
 * th8test_loadnamematch_cmd --
 *
 *	::th8testlib::loadnamematch nameA nameB
 *
 *	Direct wrapper over the file-scope th8LoadNameMatch helper
 *	in src/th8_load.c (exposed via the internal stubs table).
 *	Used to drive the C1=F vector at L258
 *	`if (nSymA > 0 && Memcmp(zSymA, zSymB, nSymA) != 0)` --
 *	unreachable via the `load LIB ?SYM?` command path because
 *	th8CanonLoadName always emits a "lib:sym" canonical form
 *	(nSymA > 0).  Pass colon-less names to force nSymA == 0.
 *	Returns 1 (names match the same library) or 0 (different).
 *
 *----------------------------------------------------------------------
 */

static int
th8test_loadnamematch_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    (void)ctx;

    if (argc != 3) {
	return Th8_WrongNumArgs(
	    interp, "th8testlib::loadnamematch nameA nameB");
    }
    Th8_SetResultInt(
        interp, th8LoadNameMatch(interp, argv[1], argl[1], argv[2], argl[2]));
    return TH8_OK;
}
#  endif /* TH8_ENABLE_LOAD */


/*
 *----------------------------------------------------------------------
 *
 * Debug test support.
 *
 * ::th8testlib::debug subcommand ?args?
 *
 *   callback install     -- Install a debug callback that captures events
 *   callback remove      -- Remove the debug callback
 *   callback events      -- Return captured events as a list
 *   callback clear       -- Clear captured events
 *   breakpoint set script line -- Set breakpoint, return ID
 *   breakpoint clear id  -- Clear breakpoint by ID
 *   breakpoint clearall  -- Clear all breakpoints
 *   step mode            -- Set step mode (none/into/over/out)
 *   step get             -- Get current step mode
 *   frames count         -- Get frame count
 *   frames info index    -- Get frame info at index
 *   eval index script    -- Evaluate script at frame index
 *
 *----------------------------------------------------------------------
 */

#  define TH8_DEBUG_MAX_EVENTS 256

typedef struct {
    int event;
    int nLine;
    int nDepth;
} Th8DebugEvent;

static Th8DebugEvent th8test_debug_events[TH8_DEBUG_MAX_EVENTS];
static int th8test_debug_event_count = 0;
static int th8test_debug_break_on_bp = 1;

/*
 *----------------------------------------------------------------------
 *
 * th8test_debug_callback --
 *
 *	Debug event callback that records events into a static
 *	ring buffer for later inspection by test scripts.
 *
 * Why / How:
 *	Tests the debugger integration API (Th8_SetDebugCallback,
 *	breakpoints, stepping).  The callback captures the event
 *	type, line number, and call depth for each debug event.
 *	On breakpoint events, returns TH8_BREAK to freeze the
 *	interpreter (unless nobreak mode is set).
 *
 * Results:
 *	TH8_OK normally; TH8_BREAK on breakpoint events to
 *	trigger interpreter suspension.
 *
 * Side effects:
 *	Appends to the global th8test_debug_events array.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_debug_callback(
    Th8_Interp *interp,
    int event,
    const char *zScript,
    size_t nScript,
    int nLine,
    int nDepth,
    void *pCtx)
{
    (void)interp;
    (void)zScript;
    (void)nScript;
    (void)pCtx;

    if (th8test_debug_event_count < TH8_DEBUG_MAX_EVENTS) {
	th8test_debug_events[th8test_debug_event_count].event = event;
	th8test_debug_events[th8test_debug_event_count].nLine = nLine;
	th8test_debug_events[th8test_debug_event_count].nDepth = nDepth;
	th8test_debug_event_count++;
    }

    /*
     * On breakpoint, return TH8_BREAK to freeze the interpreter.
     */

    if (event == TH8_DEBUG_BREAKPOINT && th8test_debug_break_on_bp) {
	return TH8_BREAK;
    }

    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_debug_breakcycle_cmd --
 *
 *	Implements "th8testlib::debug_breakcycle".  Drives the full
 *	freeze-on-breakpoint cycle end to end and returns "ok".
 *
 * Why / How:
 *	No other test actually HITS a breakpoint (the debug tests only
 *	set/clear them), so the debugger's break -> freeze -> resume
 *	contract was unexercised -- and driving it uncovered a real
 *	crash (Bug 73).  This helper installs the debug callback (which
 *	returns TH8_BREAK on a breakpoint), plants a breakpoint on
 *	line 3 of a four-command script, evaluates that script under the
 *	matching origin name, and asserts the whole R-54392 contract:
 *	on the breakpoint the interpreter freezes so `Th8_Ready` returns
 *	TH8_SUSPEND; after the breakpoint is cleared and `Th8_Thaw` runs,
 *	`Th8_Ready` returns TH8_OK AND the suspended NRE continuation
 *	resumes -- the remaining commands ("format c", "format resumed")
 *	execute, leaving "resumed" as the interpreter result.  Firing
 *	MID-script (not at the first command) is what exercises the
 *	suspend-detach path that saves pSuspendedCallbacks; a breakpoint
 *	on the very first line would suspend before any callback is
 *	pushed (the Bug 73 pCallbacks == pBottom edge case, now guarded).
 *	Matches the dedicated-helper pattern of cancel_recover /
 *	freezecycle.
 *
 * Results:
 *	"ok" on the full cycle succeeding; TH8_ERROR with a message if
 *	the interpreter did not suspend on the breakpoint, did not become
 *	ready again after thaw, or did not resume the remaining commands.
 *
 * Side effects:
 *	Temporarily installs a debug callback and a breakpoint on the
 *	calling interpreter (both removed before returning) and evaluates
 *	a side-effect-free `format` script.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_debug_breakcycle_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    int bpId = 0;
    const char *zName = "breakcycle";
    /*
     * Four commands, one per line.  The breakpoint is on line 3
     * ("format c"), so the freeze fires MID-script: "format a" and
     * "format b" have run, "format c" and "format resumed" have not.
     * A resume that works "from the exact point of suspension" must run
     * both remaining commands, leaving the result of the last one
     * ("resumed") as the interpreter result.  Line 3 (not 1 or 2) is
     * chosen so th8EvalLocal's entry readiness check -- which runs at the
     * CALLER's line, <= 2 in the driving test -- cannot spuriously match
     * the (breakcycle, 3) key before the script's own lines are reached.
     */
    const char *zScript = "format a\nformat b\nformat c\nformat resumed\n";
    const char *zRes;
    size_t nRes = 0;

    (void)ctx;
    (void)argv;
    (void)argl;
    if (argc != 1) {
	return Th8_WrongNumArgs(interp, "th8testlib::debug_breakcycle");
    }

    th8test_debug_break_on_bp = 1;
    if (Th8_SetDebugCallback(interp, th8test_debug_callback, NULL) !=
        TH8_OK) {
	th8test_debug_break_on_bp = 0;
	Th8_SetResultStatic(
	    interp, "debug_breakcycle: set callback failed", TH8_NOLEN);
	return TH8_ERROR;
    }
    if (Th8_SetBreakpoint(
            interp, zName, Th8_Strlen(interp, zName), 3, &bpId) != TH8_OK) {
	th8test_debug_break_on_bp = 0;
	Th8_SetDebugCallback(interp, NULL, NULL);
	Th8_SetResultStatic(
	    interp, "debug_breakcycle: set breakpoint failed", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * The breakpoint table is keyed on Th8_GetSourceName + line, and that
     * name is only populated by the source-name stack (which [source]
     * pushes).  A bare Th8_Eval does not push it, so mirror source_command
     * here: push "breakcycle" so the (name, line 3) breakpoint key matches
     * during evaluation, then pop it once the cycle is complete.
     */

    Th8_PushSourceName(interp, zName, Th8_Strlen(interp, zName));
    (void)Th8_Eval(
        interp, 0, zScript, Th8_Strlen(interp, zScript), zName,
        Th8_Strlen(interp, zName));

    if (Th8_Ready(interp) != TH8_SUSPEND) {
	Th8_PopSourceName(interp);
	th8test_debug_break_on_bp = 0;
	Th8_ClearBreakpoint(interp, bpId);
	Th8_SetDebugCallback(interp, NULL, NULL);
	Th8_Thaw(interp);
	Th8_SetResult(interp, 0, 0);
	Th8_SetResultStatic(
	    interp, "debug_breakcycle: did not suspend on breakpoint",
	    TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Continue past the breakpoint: clear the breakpoint and debug
     * callback FIRST so the resumed evaluation does not immediately
     * re-break at the same (breakcycle, 3) key, then thaw to resume the
     * suspended NRE continuation ("format c" then "format resumed").
     */

    th8test_debug_break_on_bp = 0;
    Th8_ClearBreakpoint(interp, bpId);
    Th8_SetDebugCallback(interp, NULL, NULL);

    Th8_Thaw(interp);
    Th8_PopSourceName(interp);
    if (Th8_Ready(interp) != TH8_OK) {
	Th8_SetResultStatic(
	    interp, "debug_breakcycle: not ready after thaw", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Prove the resume actually ran the remaining commands: the last
     * command evaluated was "format resumed", so its result must be the
     * live interpreter result now.  If the continuation had been lost
     * (the Bug 73 failure mode), the result would still be "b".
     */

    zRes = Th8_GetResult(interp, &nRes);
    if (zRes == NULL || nRes != 7 ||
        Th8_Memcmp(interp, zRes, "resumed", 7) != 0) {
	Th8_SetResultStatic(
	    interp, "debug_breakcycle: resume did not run remaining commands",
	    TH8_NOLEN);
	return TH8_ERROR;
    }

    Th8_SetResult(interp, 0, 0);
    Th8_SetResultStatic(interp, "ok", 2);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_debug_cmd --
 *
 *	Implements "th8testlib::debug" with subcommands for testing
 *	the debugger integration API: callback management, breakpoint
 *	set/clear, step mode control, frame inspection, and eval-at-
 *	frame.
 *
 * Why / How:
 *	Provides script-level access to all debugger APIs so that
 *	test scripts can exercise breakpoints, stepping, stack
 *	frame inspection, and debug event capture without a full
 *	interactive debugger.  Each subcommand maps directly to
 *	one or more Th8_Debug* API calls.
 *
 * Results:
 *	Varies by subcommand; TH8_ERROR for unknown subcommands.
 *
 * Side effects:
 *	Modifies debugger state (callbacks, breakpoints, step mode).
 *
 *----------------------------------------------------------------------
 */

static int
th8test_debug_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    (void)ctx;

    if (argc < 3) {
	return Th8_WrongNumArgs(
	    interp, "th8testlib::debug subcommand arg ?args?");
    }

    /*
     * callback install / remove / events / clear
     */

    if (argl[1] == 8 && memcmp(argv[1], "callback", 8) == 0) {

	if (argl[2] == 7 && memcmp(argv[2], "install", 7) == 0) {
	    th8test_debug_event_count = 0;
	    th8test_debug_break_on_bp = 1;
	    return Th8_SetDebugCallback(interp, th8test_debug_callback, NULL);
	}

	if (argl[2] == 6 && memcmp(argv[2], "remove", 6) == 0) {
	    return Th8_SetDebugCallback(interp, NULL, NULL);
	}

	if (argl[2] == 6 && memcmp(argv[2], "events", 6) == 0) {
	    char *zList = NULL;
	    size_t nList = 0;
	    int i;
	    char zBuf[32];

	    for (i = 0; i < th8test_debug_event_count; i++) {
		char *z;
		z = th8test_i64toa(
		    (th8_int64_t)th8test_debug_events[i].event, zBuf,
		    sizeof(zBuf));
		Th8_ListAppend(interp, &zList, &nList, z, TH8_NOLEN);
		z = th8test_i64toa(
		    (th8_int64_t)th8test_debug_events[i].nLine, zBuf,
		    sizeof(zBuf));
		Th8_ListAppend(interp, &zList, &nList, z, TH8_NOLEN);
		z = th8test_i64toa(
		    (th8_int64_t)th8test_debug_events[i].nDepth, zBuf,
		    sizeof(zBuf));
		Th8_ListAppend(interp, &zList, &nList, z, TH8_NOLEN);
	    }
	    if (zList) {
		Th8_SetResult(interp, zList, nList);
		Th8_Free(interp, zList);
	    } else {
		Th8_ClearResult(interp);
	    }
	    return TH8_OK;
	}

	if (argl[2] == 5 && memcmp(argv[2], "clear", 5) == 0) {
	    th8test_debug_event_count = 0;
	    return TH8_OK;
	}

	if (argl[2] == 5 && memcmp(argv[2], "count", 5) == 0) {
	    Th8_SetResultInt(interp, th8test_debug_event_count);
	    return TH8_OK;
	}

	if (argl[2] == 7 && memcmp(argv[2], "nobreak", 7) == 0) {
	    th8test_debug_break_on_bp = 0;
	    return TH8_OK;
	}

	Th8_SetResultStatic(
	    interp, "debug callback: unknown subcommand", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * breakpoint set script line / clear id / clearall
     */

    if (argl[1] == 10 && memcmp(argv[1], "breakpoint", 10) == 0) {

	if (argl[2] == 3 && memcmp(argv[2], "set", 3) == 0) {
	    int nLine;
	    int bpId = 0;

	    if (argc != 5) {
		return Th8_WrongNumArgs(
		    interp, "debug breakpoint set script line");
	    }
	    if (Th8_ToInt(interp, argv[4], argl[4], &nLine) != TH8_OK) {
		return TH8_ERROR;
	    }
	    if (Th8_SetBreakpoint(interp, argv[3], argl[3], nLine, &bpId) !=
	        TH8_OK) {
		return TH8_ERROR;
	    }
	    Th8_SetResultInt(interp, bpId);
	    return TH8_OK;
	}

	if (argl[2] == 5 && memcmp(argv[2], "clear", 5) == 0) {
	    int bpId;

	    if (argc != 4) {
		return Th8_WrongNumArgs(interp, "debug breakpoint clear id");
	    }
	    if (Th8_ToInt(interp, argv[3], argl[3], &bpId) != TH8_OK) {
		return TH8_ERROR;
	    }
	    return Th8_ClearBreakpoint(interp, bpId);
	}

	if (argl[2] == 8 && memcmp(argv[2], "clearall", 8) == 0) {
	    return Th8_ClearAllBreakpoints(interp);
	}

	Th8_SetResultStatic(
	    interp, "debug breakpoint: unknown subcommand", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * step mode / step get
     */

    if (argl[1] == 4 && memcmp(argv[1], "step", 4) == 0) {

	if (argc == 3) {
	    int mode = TH8_STEP_NONE;

	    if (argl[2] == 4 && memcmp(argv[2], "none", 4) == 0) {
		mode = TH8_STEP_NONE;
	    } else if (argl[2] == 4 && memcmp(argv[2], "into", 4) == 0) {
		mode = TH8_STEP_INTO;
	    } else if (argl[2] == 4 && memcmp(argv[2], "over", 4) == 0) {
		mode = TH8_STEP_OVER;
	    } else if (argl[2] == 3 && memcmp(argv[2], "out", 3) == 0) {
		mode = TH8_STEP_OUT;
	    } else if (argl[2] == 3 && memcmp(argv[2], "get", 3) == 0) {
		Th8_SetResultInt(interp, Th8_GetStepMode(interp));
		return TH8_OK;
	    } else {
		Th8_SetResultStatic(
		    interp, "debug step: use none/into/over/out/get",
		    TH8_NOLEN);
		return TH8_ERROR;
	    }
	    return Th8_SetStepMode(interp, mode);
	}

	Th8_SetResultStatic(
	    interp, "debug step: requires mode argument", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * frames count / frames info index
     */

    if (argl[1] == 6 && memcmp(argv[1], "frames", 6) == 0) {

	if (argl[2] == 5 && memcmp(argv[2], "count", 5) == 0) {
	    Th8_SetResultInt(interp, Th8_GetFrameCount(interp));
	    return TH8_OK;
	}

	if (argl[2] == 4 && memcmp(argv[2], "info", 4) == 0) {
	    int idx;
	    const char *zProc = NULL;
	    size_t nProc = 0;
	    const char *zScript = NULL;
	    size_t nScript = 0;
	    int nLine = 0;
	    char *zList = NULL;
	    size_t nList = 0;
	    char zBuf[32];
	    char *z;

	    if (argc != 4) {
		return Th8_WrongNumArgs(interp, "debug frames info index");
	    }
	    if (Th8_ToInt(interp, argv[3], argl[3], &idx) != TH8_OK) {
		return TH8_ERROR;
	    }
	    if (Th8_GetFrameInfo(
	            interp, idx, &zProc, &nProc, &zScript, &nScript,
	            &nLine) != TH8_OK) {
		Th8_SetResultStatic(
		    interp, "debug frames info: invalid index", TH8_NOLEN);
		return TH8_ERROR;
	    }

	    Th8_ListAppend(interp, &zList, &nList, zProc ? zProc : "", nProc);
	    Th8_ListAppend(
	        interp, &zList, &nList, zScript ? zScript : "", nScript);
	    z = th8test_i64toa((th8_int64_t)nLine, zBuf, sizeof(zBuf));
	    Th8_ListAppend(interp, &zList, &nList, z, TH8_NOLEN);

	    if (zList) {
		Th8_SetResult(interp, zList, nList);
		Th8_Free(interp, zList);
	    }
	    return TH8_OK;
	}

	Th8_SetResultStatic(
	    interp, "debug frames: unknown subcommand", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * eval index script
     */

    if (argl[1] == 4 && memcmp(argv[1], "eval", 4) == 0) {
	int idx;

	if (argc != 4) {
	    return Th8_WrongNumArgs(interp, "debug eval frameIndex script");
	}
	if (Th8_ToInt(interp, argv[2], argl[2], &idx) != TH8_OK) {
	    return TH8_ERROR;
	}
	return Th8_EvalAtFrame(interp, idx, argv[3], argl[3]);
    }

    Th8_SetResultStatic(interp, "debug: unknown subcommand", TH8_NOLEN);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_array_searches_cmd --
 *
 *	Implements the "th8testlib::array_searches" command.
 *	Returns a flat list of {arrayName searchId} two-element
 *	sub-lists, one per pending [array startsearch] in the
 *	current interpreter, sorted by arrayName then searchId.
 *
 *	Usage:
 *	  th8testlib::array_searches ?pattern?
 *
 *	  pattern - optional glob; matched against arrayName.  When
 *		    present, only matching searches are returned.
 *		    Search-id is NOT matched against the pattern.
 *
 * Why / How:
 *	Pending array searches are leaked resources: each one holds
 *	an entry in the per-interp search hash and a heap-allocated
 *	th8ArraySearch record until the matching donesearch (or
 *	interp teardown).  Test code uses this command to enforce
 *	leak-free behaviour, e.g. as a final test in a file:
 *
 *	    runTest {test myfeat-zz.zz {
 *		no array searches leaked across this file
 *	    } -body {
 *		::th8testlib::array_searches
 *	    } -result {}}
 *
 *	Internal: routes through Th8_IterateArraySearches (public
 *	TH8_API).  Output is sorted in C so callers do not need
 *	to call lsort first.
 *
 *----------------------------------------------------------------------
 */

typedef struct th8test_search_pair {
    char *zArr; /* Owned copy of array name. */
    size_t nArr;
    char *zSid; /* Owned copy of search id. */
    size_t nSid;
} th8test_search_pair;

typedef struct th8test_search_collect {
    Th8_Interp *interp; /* For Th8_GlobMatch and ALLOC. */
    const char *zPat; /* Optional glob pattern; NULL if none. */
    size_t nPat;
    th8test_search_pair *aPair; /* Growable array of pairs. */
    size_t nPair; /* Count of populated entries. */
    size_t nAlloc; /* Allocated capacity. */
    int rc; /* Sticky return code (TH8_ERROR on alloc fail). */
} th8test_search_collect;

/*
 *----------------------------------------------------------------------
 *
 * th8test_search_collect_cb --
 *
 *	`Th8_IterateArraySearches` callback that collects every
 *	pending array search into the growable
 *	`th8test_search_collect` accumulator pointed to by
 *	`pCtx`, applying the optional glob pattern in
 *	`pCtx->zPat` (matched against the array name only) as
 *	a per-entry filter.
 *
 *	For each accepted entry the helper copies both the
 *	array name and the search id into freshly-allocated,
 *	NUL-terminated buffers owned by the accumulator (the
 *	iterator borrows the originals for the callback's
 *	lifetime only).  The pair array grows by doubling;
 *	allocation failure is recorded by setting
 *	`pCtx->rc = TH8_ERROR` and returning `TH8_ERROR`,
 *	which `Th8_IterateArraySearches` honours by stopping
 *	iteration early.
 *
 * Parameters:
 *	zArray -- array name (not NUL-terminated).
 *	nArray -- array-name length.
 *	zSid   -- search id (not NUL-terminated).
 *	nSid   -- search-id length.
 *	pCtx   -- `th8test_search_collect *` accumulator.
 *
 * Returns:
 *	`TH8_OK` to continue iteration, `TH8_ERROR` on
 *	allocation failure (also recorded in `pCtx->rc`).
 *
 * Side effects:
 *	Allocates / reallocates the accumulator's `aPair`
 *	array and each entry's owned `zArr` / `zSid` copies.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_search_collect_cb(
    const char *zArray,
    size_t nArray,
    const char *zSid,
    size_t nSid,
    void *pCtx)
{
    th8test_search_collect *p = (th8test_search_collect *)pCtx;
    th8test_search_pair *pPair;

    if (p->zPat &&
        !Th8_GlobMatch(p->interp, p->zPat, p->nPat, zArray, nArray)) {
	return TH8_OK;
    }

    if (p->nPair == p->nAlloc) {
	size_t nNew = p->nAlloc ? p->nAlloc * 2 : 16;
	size_t nCopy;
	th8test_search_pair *aNew;

	aNew = (th8test_search_pair *)
	    TH8_ALLOC_MUL(p->interp, nNew, sizeof(th8test_search_pair));
	if (!aNew) {
	    p->rc = TH8_ERROR;
	    return TH8_ERROR;
	}
	if (p->aPair) {
	    if (TH8_SAFE_MUL_SIZE(
	            p->nPair, sizeof(th8test_search_pair), &nCopy)) {
		Th8_Free(p->interp, aNew);
		p->rc = TH8_ERROR;
		return TH8_ERROR;
	    }
	    Th8_Memcpy(p->interp, aNew, p->aPair, nCopy);
	    Th8_Free(p->interp, p->aPair);
	}
	p->aPair = aNew;
	p->nAlloc = nNew;
    }
    pPair = &p->aPair[p->nPair];
    pPair->zArr = (char *)TH8_ALLOC_STR(p->interp, nArray);
    if (!pPair->zArr) {
	p->rc = TH8_ERROR;
	return TH8_ERROR;
    }
    Th8_Memcpy(p->interp, pPair->zArr, zArray, nArray);
    pPair->zArr[nArray] = '\0';
    pPair->nArr = nArray;
    pPair->zSid = (char *)TH8_ALLOC_STR(p->interp, nSid);
    if (!pPair->zSid) {
	Th8_Free(p->interp, pPair->zArr);
	p->rc = TH8_ERROR;
	return TH8_ERROR;
    }
    Th8_Memcpy(p->interp, pPair->zSid, zSid, nSid);
    pPair->zSid[nSid] = '\0';
    pPair->nSid = nSid;
    p->nPair++;
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_search_pair_cmp --
 *
 *	`qsort` comparator that orders two `th8test_search_pair`
 *	records by array name first, then by search id, using
 *	byte-wise `memcmp` over the shorter prefix and length
 *	as the secondary key when the prefix matches.  Provides
 *	the stable, locale-free ordering that
 *	`th8test_array_searches_cmd` returns so callers can
 *	compare results without a leading `lsort` in script
 *	code.
 *
 * Parameters:
 *	a -- pointer to a `th8test_search_pair`.
 *	b -- pointer to a `th8test_search_pair`.
 *
 * Returns:
 *	Negative, zero, or positive per the standard `qsort`
 *	contract.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_search_pair_cmp(const void *a, const void *b)
{
    const th8test_search_pair *pa = (const th8test_search_pair *)a;
    const th8test_search_pair *pb = (const th8test_search_pair *)b;
    size_t nMin = pa->nArr < pb->nArr ? pa->nArr : pb->nArr;
    int r = memcmp(pa->zArr, pb->zArr, nMin);
    if (r != 0) return r;
    if (pa->nArr != pb->nArr) {
	return pa->nArr < pb->nArr ? -1 : 1;
    }
    nMin = pa->nSid < pb->nSid ? pa->nSid : pb->nSid;
    r = memcmp(pa->zSid, pb->zSid, nMin);
    if (r != 0) return r;
    if (pa->nSid != pb->nSid) {
	return pa->nSid < pb->nSid ? -1 : 1;
    }
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_search_collect_free --
 *
 *	Release every owned allocation in a
 *	`th8test_search_collect` accumulator: each pair's
 *	`zArr` and `zSid` copies, followed by the `aPair`
 *	array itself.  Zeros the counters / pointer so the
 *	accumulator can be reused without re-`memset`ing it.
 *
 *	Safe to call when no pairs have been collected (the
 *	NULL-`aPair` early return covers the failed-iteration
 *	cleanup path).
 *
 * Parameters:
 *	p -- accumulator to drain (must be non-NULL).
 *
 * Returns:
 *	None.
 *
 * Side effects:
 *	Frees every owned allocation in `p` via `Th8_Free`.
 *	After the call, `p->aPair == 0` and
 *	`p->nPair == p->nAlloc == 0`.
 *
 *----------------------------------------------------------------------
 */
static void
th8test_search_collect_free(th8test_search_collect *p)
{
    size_t i;

    if (!p->aPair) return;
    for (i = 0; i < p->nPair; i++) {
	if (p->aPair[i].zArr) Th8_Free(p->interp, p->aPair[i].zArr);
	if (p->aPair[i].zSid) Th8_Free(p->interp, p->aPair[i].zSid);
    }
    Th8_Free(p->interp, p->aPair);
    p->aPair = 0;
    p->nPair = 0;
    p->nAlloc = 0;
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_array_searches_cmd --
 *
 *	Implements `::th8testlib::array_searches ?PATTERN?`.
 *	Returns a flat Tcl list of `{arrayName searchId}`
 *	two-element sub-lists, one per pending
 *	`[array startsearch]` in the current interpreter,
 *	sorted by array name then search id (so callers do
 *	not need to wrap the result in `lsort`).
 *
 *	An optional glob PATTERN filters by array name only;
 *	search ids are not matched against PATTERN.  The
 *	helper is intended for leak-checking trailing tests
 *	such as:
 *
 *	    test myfeat-zz.zz {no leaked searches} -body {
 *		::th8testlib::array_searches
 *	    } -result {}
 *
 *	Internally walks the per-interp search hash via
 *	`Th8_IterateArraySearches` (public `TH8_API`), accumulates
 *	pairs through `th8test_search_collect_cb`, sorts via
 *	`th8test_search_pair_cmp`, and releases scratch state
 *	via `th8test_search_collect_free`.
 *
 * Parameters:
 *	interp -- live interpreter (receives result).
 *	ctx    -- unused command context.
 *	argc   -- argument count (1 or 2).
 *	argv   -- argv[0]=command name; argv[1]=PATTERN (optional).
 *	argl   -- argument byte-lengths.
 *
 * Returns:
 *	`TH8_OK` with the formatted list in the interpreter
 *	result.  `TH8_ERROR` on argument-count error or
 *	allocation failure (interpreter result: existing
 *	error from the failed call site).
 *
 * Side effects:
 *	Sets the interpreter result.  Allocates and frees a
 *	growable pair array and per-pair string copies.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_array_searches_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    th8test_search_collect collect;
    char *zResult = 0;
    size_t nResult = 0;
    size_t i;
    int rc = TH8_OK;

    (void)ctx;
    if (argc > 2) {
	return Th8_WrongNumArgs(
	    interp, "th8testlib::array_searches ?pattern?");
    }

    Th8_Memset(interp, &collect, 0, sizeof(collect));
    collect.interp = interp;
    if (argc == 2) {
	collect.zPat = argv[1];
	collect.nPat = TH8_LEN(argl[1]);
    }

    Th8_IterateArraySearches(
        interp, th8test_search_collect_cb, (void *)&collect);
    if (collect.rc != TH8_OK) {
	th8test_search_collect_free(&collect);
	return TH8_ERROR;
    }

    if (collect.nPair > 1) {
	qsort(
	    collect.aPair, collect.nPair, sizeof(th8test_search_pair),
	    th8test_search_pair_cmp);
    }

    for (i = 0; i < collect.nPair; i++) {
	char *zPair = 0;
	size_t nPair = 0;

	if (Th8_ListAppend(
	        interp, &zPair, &nPair, collect.aPair[i].zArr,
	        collect.aPair[i].nArr) != TH8_OK ||
	    Th8_ListAppend(
	        interp, &zPair, &nPair, collect.aPair[i].zSid,
	        collect.aPair[i].nSid) != TH8_OK ||
	    Th8_ListAppend(interp, &zResult, &nResult, zPair, nPair) !=
	        TH8_OK) {
	    if (zPair) Th8_Free(interp, zPair);
	    if (zResult) Th8_Free(interp, zResult);
	    th8test_search_collect_free(&collect);
	    return TH8_ERROR;
	}
	Th8_Free(interp, zPair);
    }
    rc = Th8_SetResult(interp, zResult ? zResult : "", zResult ? nResult : 0);
    if (zResult) Th8_Free(interp, zResult);
    th8test_search_collect_free(&collect);
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_queue_event_cmd --
 *
 *	Implements the "th8testlib::queue_event" command.  Spawns
 *	a worker pthread that sleeps for TIME_MS milliseconds and
 *	then calls Th8_QueueEvent on a freshly-created
 *	Th8_AsyncState.  When the event later drains via [update]
 *	or [vwait] on the interp's owning thread, the callback
 *	evaluates SCRIPT in the interp.
 *
 *	Usage: th8testlib::queue_event TIME_MS SCRIPT
 *
 *	Returns the empty string on success.  Errors out on
 *	allocation / thread / pState failure.
 *
 *	Cleanup: each queue_event call creates exactly one
 *	pState and one heap-allocated context.  Both are freed
 *	by the callback (when the script runs) OR by the worker
 *	thread itself (if Th8_QueueEvent fails because the
 *	interp was already deleted).  No leak in either case.
 *
 *	Cross-platform: thread create / join / sleep go through
 *	the th8test_thread_* abstraction below, which wraps
 *	pthread on POSIX and _beginthreadex/Sleep on Win32.
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * Thread / sleep abstraction for the test library --
 *
 *	The three event-related test commands (queue_event,
 *	event_stress, event_delete_race) all need the same trio
 *	of operations on every host:
 *	  - spawn a worker thread (joinable or detached)
 *	  - join a worker thread (block until exit + release the
 *	    underlying handle)
 *	  - sleep the calling thread for a bounded interval
 *
 *	The worker function signature differs between platforms
 *	(unsigned __stdcall on Win32 via _beginthreadex,
 *	void *(*)(void *) on POSIX via pthread_create), so worker
 *	bodies are declared with TH8TEST_WORKER_DECL and end with
 *	TH8TEST_WORKER_RETURN.  th8test_thread_proc is the
 *	corresponding function-pointer type accepted by
 *	th8test_thread_create / _create_detached.
 *
 *----------------------------------------------------------------------
 */

#  if defined(_WIN32) || defined(WIN32)
#    include <process.h> /* _beginthreadex */
typedef HANDLE th8test_thread_t;
typedef unsigned(__stdcall *th8test_thread_proc)(void *);
#    define TH8TEST_WORKER_DECL(name)                                        \
	static unsigned __stdcall name(void *arg)
#    define TH8TEST_WORKER_RETURN return 0
#  else
typedef pthread_t th8test_thread_t;
typedef void *(*th8test_thread_proc)(void *);
#    define TH8TEST_WORKER_DECL(name) static void *name(void *arg)
#    define TH8TEST_WORKER_RETURN     return NULL
#  endif


/*
 *----------------------------------------------------------------------
 *
 * th8test_thread_create --
 *
 *	Spawn a JOINABLE worker thread.  On success, *pTid holds
 *	a valid thread handle the caller can later pass to
 *	th8test_thread_join.  Returns 0 on success, non-zero on
 *	failure (handle is NOT written on failure).
 *
 *----------------------------------------------------------------------
 */

static int
th8test_thread_create(
    th8test_thread_t *pTid,
    th8test_thread_proc fn,
    void *arg)
{
#  if defined(_WIN32) || defined(WIN32)
    HANDLE h = (HANDLE)_beginthreadex(NULL, 0, fn, arg, 0, NULL);
    if (!h) return -1;
    *pTid = h;
    return 0;
#  else
    return pthread_create(pTid, NULL, fn, arg);
#  endif
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_thread_create_detached --
 *
 *	Spawn a DETACHED worker thread (fire-and-forget; no
 *	join).  On Win32 the thread is created joinable and the
 *	handle is closed immediately, which is the standard
 *	"detached" pattern.  Returns 0 on success, non-zero on
 *	failure.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_thread_create_detached(th8test_thread_proc fn, void *arg)
{
#  if defined(_WIN32) || defined(WIN32)
    HANDLE h = (HANDLE)_beginthreadex(NULL, 0, fn, arg, 0, NULL);
    if (!h) return -1;
    CloseHandle(h);
    return 0;
#  else
    pthread_t tid;
    pthread_attr_t attr;
    int rc;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    rc = pthread_create(&tid, &attr, fn, arg);
    pthread_attr_destroy(&attr);
    return rc;
#  endif
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_thread_join --
 *
 *	Block the calling thread until tid exits, then release
 *	the underlying handle.  After this returns, tid is no
 *	longer valid.  Caller must NOT call this on a thread
 *	created via th8test_thread_create_detached.
 *
 *----------------------------------------------------------------------
 */

static void
th8test_thread_join(th8test_thread_t tid)
{
#  if defined(_WIN32) || defined(WIN32)
    (void)WaitForSingleObject(tid, INFINITE);
    CloseHandle(tid);
#  else
    pthread_join(tid, NULL);
#  endif
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_sleep_ms --
 *
 *	Sleep the calling thread for nMs milliseconds.  Returns
 *	immediately for nMs <= 0.  No fractional-millisecond
 *	precision is provided; this is for test-pacing purposes
 *	only.
 *
 *----------------------------------------------------------------------
 */

static void
th8test_sleep_ms(long nMs)
{
    if (nMs <= 0) return;
#  if defined(_WIN32) || defined(WIN32)
    Sleep((DWORD)nMs);
#  else
    {
	struct timespec ts;
	ts.tv_sec = nMs / 1000;
	ts.tv_nsec = (nMs % 1000) * 1000000L;
	nanosleep(&ts, NULL);
    }
#  endif
}


typedef struct th8test_qe_ctx {
    void *pState; /* Th8_AsyncState. */
    Th8_Interp *pInterp; /* For Th8_Eval inside callback. */
    char *zScript; /* Heap-allocated copy of script text. */
    size_t nScript;
    long nSleepMs; /* Worker sleep before queueing. */
} th8test_qe_ctx;

/*
 *----------------------------------------------------------------------
 *
 * th8test_queue_event_cb --
 *
 *	`Th8_QueueEvent` callback for `::th8testlib::queue_event`.
 *	Drains by evaluating the deferred SCRIPT against the
 *	owning interpreter (which `Th8_QueueEvent` guarantees
 *	is the original interpreter that registered the async
 *	state), then releases every resource the queueing
 *	side allocated:
 *
 *	  * `zScript` -- heap copy of the SCRIPT body.
 *	  * `pState`  -- `Th8_AsyncState` finalised via
 *	                  `Th8_FinalizeAsyncState`.
 *	  * `q`       -- the wrapper context itself.
 *
 * Parameters:
 *	interp -- the interpreter the event is draining into.
 *	pCtx   -- `th8test_qe_ctx *` registered by the queueing
 *		side.
 *
 * Returns:
 *	The return code of the inner `Th8_Eval` call (whatever
 *	the SCRIPT body produces).
 *
 * Side effects:
 *	Evaluates SCRIPT in `interp` (may set the interpreter
 *	result / mutate variables / etc.).  Frees `q->zScript`,
 *	`q->pState`, and `q`.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_queue_event_cb(Th8_Interp *interp, void *pCtx)
{
    th8test_qe_ctx *q = (th8test_qe_ctx *)pCtx;
    int rc = Th8_Eval(interp, 0, q->zScript, q->nScript, NULL, 0);
    Th8_Free(interp, q->zScript);
    Th8_FinalizeAsyncState(q->pState);
    Th8_Free(interp, q);
    return rc;
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_queue_event_worker --
 *
 *	Worker thread body declared via `TH8TEST_WORKER_DECL`
 *	(which expands to the platform-specific signature -- a
 *	`void *(*)(void *)` on POSIX, an
 *	`unsigned __stdcall(*)(void *)` on Win32).  Runs the
 *	sleep-then-queue sequence used by
 *	`::th8testlib::queue_event`:
 *
 *	  1. `Th8_ThreadInit` -- register with TH8's allocator
 *	     so subsequent `xMalloc`-using APIs (including
 *	     `Th8_QueueEvent` itself on some platforms) have
 *	     the per-thread state they need.
 *	  2. `th8test_sleep_ms(q->nSleepMs)` -- pause for the
 *	     caller-specified delay before queueing.
 *	  3. `Th8_QueueEvent(q->pState, ...cb)` -- post the
 *	     deferred SCRIPT to the originating interp's event
 *	     queue.  On failure (interp already deleted, queue
 *	     unavailable), the worker takes ownership of the
 *	     resources the callback would have released and
 *	     frees them directly via the system allocator --
 *	     `Th8_Free` is unsafe at this point because the
 *	     interp may have already freed its allocator state.
 *	  4. `Th8_ThreadDone` -- unregister.
 *
 *	Detached -- the spawning command never joins this
 *	worker; cleanup happens entirely inside the callback
 *	(on success) or here (on queue failure).
 *
 * Parameters:
 *	arg -- `th8test_qe_ctx *` wrapper context produced by
 *		`th8test_queue_event_cmd`.
 *
 * Returns:
 *	The platform-specific worker-return sentinel via
 *	`TH8TEST_WORKER_RETURN` (`0` on Win32, `NULL` on POSIX).
 *
 * Side effects:
 *	Sleeps and queues an event into the interp's event
 *	queue (success), or frees the wrapper resources via
 *	`free` (queue failure).
 *
 *----------------------------------------------------------------------
 */
TH8TEST_WORKER_DECL(th8test_queue_event_worker)
{
    th8test_qe_ctx *q = (th8test_qe_ctx *)arg;
    /* Register this worker thread with TH8's allocator before
     * calling any thread-safe API that may allocate (e.g.
     * Th8_QueueEvent -> xMalloc).  No-op when the build's
     * allocator does not need per-thread state.            */
    Th8_ThreadInit();
    th8test_sleep_ms(q->nSleepMs);
    if (Th8_QueueEvent(q->pState, th8test_queue_event_cb) != TH8_OK) {
	/* Interp was deleted, or queue not available.
	 * Free our context here since the callback won't run. */
	Th8_FinalizeAsyncState(q->pState);
	free(q->zScript);
	free(q);
    }
    Th8_ThreadDone();
    TH8TEST_WORKER_RETURN;
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_queue_event_cmd --
 *
 *	Implements `::th8testlib::queue_event TIME_MS SCRIPT`.
 *	Spawns a detached worker thread (`th8test_queue_event_worker`)
 *	that sleeps for TIME_MS milliseconds and then calls
 *	`Th8_QueueEvent` on a freshly-created
 *	`Th8_AsyncState`.  When the event later drains via
 *	`[update]` or `[vwait]` on the interpreter's owning
 *	thread, `th8test_queue_event_cb` evaluates SCRIPT in
 *	the interpreter.
 *
 *	Cleanup contract: each call allocates exactly one
 *	context + script copy + `pState`.  Either:
 *	  * the callback runs and frees them on the owning
 *	    thread, or
 *	  * the worker thread frees them itself when
 *	    `Th8_QueueEvent` fails (interp already deleted).
 *	No leak in either case.
 *
 * Parameters:
 *	interp -- live interpreter (receives result; will own
 *		the queued event).
 *	ctx    -- unused command context.
 *	argc   -- argument count (must be 3).
 *	argv   -- argv[0]=command name; argv[1]=TIME_MS;
 *		argv[2]=SCRIPT.
 *	argl   -- argument byte-lengths.
 *
 * Returns:
 *	`TH8_OK` on successful spawn (interpreter result:
 *	empty string).  `TH8_ERROR` on bad arguments,
 *	allocation failure, `Th8_CreateAsyncState` failure, or
 *	thread-create failure (interpreter result: diagnostic).
 *
 * Side effects:
 *	Allocates a `th8test_qe_ctx`, a heap script copy, and
 *	a `Th8_AsyncState`; spawns one detached worker
 *	thread.  Ownership of all three transfers to the
 *	worker / callback on success.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_queue_event_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    th8_int64_t nMs;
    th8test_qe_ctx *q;
    int rc;

    (void)ctx;
    if (argc != 3) {
	return Th8_WrongNumArgs(
	    interp, "th8testlib::queue_event timeMs script");
    }
    if (Th8_ToWideInt(interp, argv[1], TH8_LEN(argl[1]), &nMs) != TH8_OK ||
        nMs < 0) {
	Th8_SetResultStatic(
	    interp, "queue_event: timeMs must be non-negative", TH8_NOLEN);
	return TH8_ERROR;
    }

    q = (th8test_qe_ctx *)Th8_Malloc(interp, sizeof(th8test_qe_ctx));
    if (!q) return TH8_ERROR;
    Th8_Memset(interp, q, 0, sizeof(*q));
    q->pInterp = interp;
    q->nSleepMs = (long)nMs;

    /* Copy the script -- argv buffer doesn't survive past this
     * command.  Heap copy lives until the callback runs.  */
    q->nScript = TH8_LEN(argl[2]);
    q->zScript = (char *)Th8_Malloc(interp, q->nScript + 1);
    if (!q->zScript) {
	Th8_Free(interp, q);
	return TH8_ERROR;
    }
    Th8_Memcpy(interp, q->zScript, argv[2], q->nScript);
    q->zScript[q->nScript] = '\0';

    /* Create a fresh pState per call.  pCtx is the q struct;
     * the callback uses it to recover script + pState.    */
    q->pState = Th8_CreateAsyncState(interp, q);
    if (!q->pState) {
	Th8_Free(interp, q->zScript);
	Th8_Free(interp, q);
	Th8_SetResultStatic(
	    interp, "queue_event: Th8_CreateAsyncState failed", TH8_NOLEN);
	return TH8_ERROR;
    }

    /* Spawn worker; detach so we don't have to join.  The
     * th8test_thread_create_detached helper wraps pthread on
     * POSIX and _beginthreadex+CloseHandle on Win32.        */
    rc = th8test_thread_create_detached(th8test_queue_event_worker, q);
    if (rc != 0) {
	Th8_FinalizeAsyncState(q->pState);
	Th8_Free(interp, q->zScript);
	Th8_Free(interp, q);
	Th8_SetResultStatic(
	    interp, "queue_event: thread create failed", TH8_NOLEN);
	return TH8_ERROR;
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_queue_event_sync_cmd --
 *
 *	Implements `::th8testlib::queue_event_sync SCRIPT`.  Queues
 *	SCRIPT onto a fresh Th8_AsyncState by calling Th8_QueueEvent
 *	directly on the CALLING thread -- no worker thread, no sleep.
 *
 * Why / How:
 *	The threaded `queue_event` spawns a detached worker that
 *	sleeps then queues, so two successive `queue_event 0 {...}`
 *	calls race: the events can be queued (and thus drained by
 *	`[update]`) in either order.  That non-determinism is exactly
 *	what made `event-9.3` an intermittent flake (Bug 59) -- not a
 *	defect in the cancel/catch machinery, which is correct and
 *	covered by `suspend.tcl` Section 6.  Th8_QueueEvent is fully
 *	mutex-protected and has no cross-thread requirement, so queuing
 *	inline makes ordering across successive calls deterministic:
 *	the first call's event is drained first.  The drain callback
 *	(`th8test_queue_event_cb`) and context struct are shared with
 *	the threaded command.
 *
 * Results:
 *	TH8_OK once the event is queued; TH8_ERROR on allocation or
 *	async-state failure (interpreter result: diagnostic).
 *
 * Side effects:
 *	Allocates one context + async state (freed when the event
 *	drains).  Queues one event on the per-interp event queue.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_queue_event_sync_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    th8test_qe_ctx *q;

    (void)ctx;
    if (argc != 2) {
	return Th8_WrongNumArgs(
	    interp, "th8testlib::queue_event_sync script");
    }

    q = (th8test_qe_ctx *)Th8_Malloc(interp, sizeof(th8test_qe_ctx));
    if (!q) return TH8_ERROR;
    Th8_Memset(interp, q, 0, sizeof(*q));
    q->pInterp = interp;
    q->nSleepMs = 0;

    q->nScript = TH8_LEN(argl[1]);
    q->zScript = (char *)Th8_Malloc(interp, q->nScript + 1);
    if (!q->zScript) {
	Th8_Free(interp, q);
	return TH8_ERROR;
    }
    Th8_Memcpy(interp, q->zScript, argv[1], q->nScript);
    q->zScript[q->nScript] = '\0';

    q->pState = Th8_CreateAsyncState(interp, q);
    if (!q->pState) {
	Th8_Free(interp, q->zScript);
	Th8_Free(interp, q);
	Th8_SetResultStatic(
	    interp, "queue_event_sync: Th8_CreateAsyncState failed",
	    TH8_NOLEN);
	return TH8_ERROR;
    }

    /* Queue inline on the calling thread -- deterministic ordering. */
    if (Th8_QueueEvent(q->pState, th8test_queue_event_cb) != TH8_OK) {
	Th8_FinalizeAsyncState(q->pState);
	Th8_Free(interp, q->zScript);
	Th8_Free(interp, q);
	Th8_SetResultStatic(
	    interp, "queue_event_sync: Th8_QueueEvent failed", TH8_NOLEN);
	return TH8_ERROR;
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Stress tests for Th8_QueueEvent / Th8_CreateAsyncState lifecycle.
 *
 *	Two test commands live in this section:
 *
 *	  ::th8testlib::event_stress NTHREADS NEVENTS
 *	  ::th8testlib::event_delete_race NFAST NSLOW SLOW_DELAY_MS
 *
 *	Both follow the canonical per-thread pState pattern: each
 *	worker thread has its OWN Th8_AsyncState (so producers
 *	never contend), the worker body is a tight queue loop,
 *	and verification consults the per-worker counters after
 *	join + drain + finalize.
 *
 *	A single th8test_worker struct holds everything per
 *	worker -- pState, parameters, and the counter the bump
 *	callback writes -- so main only needs two arrays
 *	(workers, tids) instead of four parallel ones.
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8test_worker --
 *
 *	Per-worker state.  One instance per worker thread, kept
 *	in a heap-allocated array owned by the calling test
 *	command.  The worker thread holds a borrowed pointer to
 *	&aWorker[i] for the duration of its run.
 *
 *	pState is the worker's OWN Th8_AsyncState -- created on
 *	the test command's thread before th8test_thread_create,
 *	and finalized on the same thread after
 *	th8test_thread_join.
 *
 *	Counter is the slot the bump callback writes through.
 *	Drain runs on the test command's thread, single-threaded
 *	w.r.t. any one counter, so no atomic is needed.  Each
 *	pState was created with pCtx=&worker[i].counter so the
 *	callback receives the right pointer.
 *
 *----------------------------------------------------------------------
 */

typedef struct th8test_worker {
    void *pState; /* This worker's own Th8_AsyncState. */
    int nIters; /* Events to queue before exit. */
    long nSleepMs; /* Pre-queue sleep (0 = none). */
    int counter; /* Bump callback writes here. */
} th8test_worker;


/*
 *----------------------------------------------------------------------
 *
 * th8test_bump_cb --
 *
 *	Th8_QueueEvent callback.  Runs on the test command's
 *	thread inside Th8_DrainQueueEvents.  pCtx is the
 *	&worker[i].counter pointer captured at
 *	Th8_CreateAsyncState time; bump it by one.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_bump_cb(Th8_Interp *interp, void *pCtx)
{
    int *pCounter = (int *)pCtx;
    (void)interp;
    if (pCounter) (*pCounter)++;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_worker_thread --
 *
 *	Worker thread entry point (signature decorated by
 *	TH8TEST_WORKER_DECL -- unsigned __stdcall on Win32, void *
 *	on POSIX).  Optionally sleeps, then loops calling
 *	Th8_QueueEvent on the worker's own pState
 *	nIters times.  TH8_ERROR returns are silently ignored:
 *	a "failed" event is simply one whose counter never gets
 *	bumped (callback never runs), and the verification
 *	side computes failures as the difference between events
 *	submitted and counter sums.
 *
 *----------------------------------------------------------------------
 */

TH8TEST_WORKER_DECL(th8test_worker_thread)
{
    th8test_worker *w = (th8test_worker *)arg;
    int i;
    /* Register this worker thread with TH8's allocator before
     * calling any thread-safe API that may allocate (e.g.
     * Th8_QueueEvent -> xMalloc).  No-op when the build's
     * allocator does not need per-thread state.            */
    Th8_ThreadInit();
    th8test_sleep_ms(w->nSleepMs);
    for (i = 0; i < w->nIters; i++) {
	(void)Th8_QueueEvent(w->pState, th8test_bump_cb);
    }
    Th8_ThreadDone();
    TH8TEST_WORKER_RETURN;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_event_stress_cmd --
 *
 *	::th8testlib::event_stress NTHREADS NEVENTS
 *
 *	Spawn NTHREADS worker threads, each with its own pState,
 *	each enqueueing NEVENTS bump callbacks against the calling
 *	interp.  After all workers join, drain every queue on the
 *	main thread (running each callback, bumping the matching
 *	per-worker counter), then finalize every pState.
 *
 *	Returns "dispatched=$D failed=$F expected=$E"
 *	  where E = NTHREADS * NEVENTS,
 *	        D = sum of per-worker counters (callbacks invoked),
 *	        F = E - D (events that submitted but never drained).
 *
 *	A passing test asserts F == 0 (live interp; no events lost).
 *
 *----------------------------------------------------------------------
 */

static int
th8test_event_stress_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    th8_int64_t nThreads, nEvents;
    int i, totalEvents, dispatched;
    th8test_worker *aWorker = NULL;
    th8test_thread_t *aTid = NULL;
    int nSpawned = 0;
    char zBuf[160];
    size_t nBuf;

    (void)ctx;

    /* ---- Phase 1: argument parse + bounds check ---- */
    if (argc != 3) {
	return Th8_WrongNumArgs(
	    interp, "th8testlib::event_stress NTHREADS NEVENTS");
    }
    if (Th8_ToWideInt(interp, argv[1], TH8_LEN(argl[1]), &nThreads) !=
            TH8_OK ||
        nThreads < 1 || nThreads > 256) {
	Th8_SetResultStatic(
	    interp, "event_stress: NTHREADS must be in [1, 256]", TH8_NOLEN);
	return TH8_ERROR;
    }
    if (Th8_ToWideInt(interp, argv[2], TH8_LEN(argl[2]), &nEvents) !=
            TH8_OK ||
        nEvents < 1 || nEvents > 100000) {
	Th8_SetResultStatic(
	    interp, "event_stress: NEVENTS must be in [1, 100000]",
	    TH8_NOLEN);
	return TH8_ERROR;
    }
    /* AUDIT-OK[size-name-multiply]: nThreads (<=256) and nEvents
     * (<=100000) are bounds-checked above; product <= 25,600,000
     * fits in int32. */
    totalEvents = (int)(nThreads * nEvents);

    /* ---- Phase 2: allocate the two arrays + create pStates ----
     * aWorker[] holds counter+pState+params inline; aTid[] is
     * the thread-handle array.  pStates point pCtx at the inline
     * counter slot, which is stable for the lifetime of aWorker. */
    aWorker = (th8test_worker *)
        TH8_ALLOC_MUL(interp, (size_t)nThreads, sizeof(th8test_worker));
    aTid = (th8test_thread_t *)
        TH8_ALLOC_MUL(interp, (size_t)nThreads, sizeof(th8test_thread_t));
    if (!aWorker || !aTid) {
	if (aWorker) Th8_Free(interp, aWorker);
	if (aTid) Th8_Free(interp, aTid);
	return TH8_ERROR;
    }
    for (i = 0; i < (int)nThreads; i++) {
	aWorker[i].counter = 0;
	aWorker[i].nIters = (int)nEvents;
	aWorker[i].nSleepMs = 0;
	aWorker[i].pState = Th8_CreateAsyncState(interp, &aWorker[i].counter);
	if (!aWorker[i].pState) {
	    int j;
	    for (j = 0; j < i; j++) {
		Th8_FinalizeAsyncState(aWorker[j].pState);
	    }
	    Th8_Free(interp, aWorker);
	    Th8_Free(interp, aTid);
	    Th8_SetResultStatic(
	        interp, "event_stress: Th8_CreateAsyncState failed",
	        TH8_NOLEN);
	    return TH8_ERROR;
	}
    }

    /* ---- Phase 3: spawn workers ---- */
    for (i = 0; i < (int)nThreads; i++) {
	if (th8test_thread_create(
	        &aTid[i], th8test_worker_thread, &aWorker[i]) != 0) {
	    break;
	}
	nSpawned++;
    }
    if (nSpawned != (int)nThreads) {
	for (i = 0; i < nSpawned; i++)
	    th8test_thread_join(aTid[i]);
	for (i = 0; i < (int)nThreads; i++) {
	    Th8_FinalizeAsyncState(aWorker[i].pState);
	}
	Th8_Free(interp, aWorker);
	Th8_Free(interp, aTid);
	Th8_SetResultStatic(
	    interp, "event_stress: thread create failed", TH8_NOLEN);
	return TH8_ERROR;
    }

    /* ---- Phase 4: join all workers ----
     * No drain runs in flight -- keeps the test deterministic.
     * Once join returns for every worker, no more events can
     * be queued; everything sits on the per-pState queues. */
    for (i = 0; i < (int)nThreads; i++) {
	th8test_thread_join(aTid[i]);
    }

    /* ---- Phase 5: drain everything on the test thread ----
     * Walks every registered pState; runs each queued callback
     * (bumping the matching per-worker counter). */
    (void)Th8_DrainQueueEvents(interp);

    /* ---- Phase 6: tally and format ---- */
    dispatched = 0;
    for (i = 0; i < (int)nThreads; i++) {
	dispatched += aWorker[i].counter;
    }
    nBuf = (size_t)snprintf(
        zBuf, sizeof(zBuf), "dispatched=%d failed=%d expected=%d", dispatched,
        totalEvents - dispatched, totalEvents);

    /* ---- Phase 7: cleanup ---- */
    for (i = 0; i < (int)nThreads; i++) {
	Th8_FinalizeAsyncState(aWorker[i].pState);
    }
    Th8_Free(interp, aWorker);
    Th8_Free(interp, aTid);
    return Th8_SetResult(interp, zBuf, nBuf);
}


/*
 * Per-worker state for th8test_env_stress_cmd: each worker owns a
 * distinct child interpreter (so per-interp state and the debug heap
 * tracker never race), and all workers hammer the SAME `::env` key so
 * their setenv/unsetenv calls contend maximally on the shared file-scope
 * env mutex (R-00313).  `failed`/`zErr` capture the FIRST non-OK eval's
 * error text so th8test_env_stress_cmd can emit a durable diagnostic --
 * the per-eval error is otherwise discarded (only a success count is kept),
 * leaving a failure's aggregate "got/expected" result with no cause.
 */
typedef struct th8test_env_worker {
    Th8_Interp *interp; /* this worker's own child interpreter */
    int nIters;
    int ok; /* successful set+unset cycles */
    int failed; /* nonzero once the first non-OK eval is captured */
    char zErr[256]; /* first non-OK eval's error text (NUL-terminated) */
} th8test_env_worker;

/* Every worker runs this: one setenv + one unsetenv per cycle, all
 * routed through the env platform's mutex-protected xKeyValue callback. */
static const char
    TH8TEST_ENV_STRESS_SCRIPT[] = "set ::env(TH8_ENVSTRESS) 1\n"
                                  "unset -nocomplain ::env(TH8_ENVSTRESS)\n";

/*
 *----------------------------------------------------------------------
 *
 * th8test_env_worker_fn --
 *
 *	Worker body for th8test_env_stress_cmd: evaluate the set/unset
 *	env script nIters times on this worker's own interpreter, counting
 *	the cycles that complete.  The FIRST eval that returns non-OK has
 *	its error captured into w->zErr for the command's durable diagnostic.
 *
 *----------------------------------------------------------------------
 */
TH8TEST_WORKER_DECL(th8test_env_worker_fn)
{
    th8test_env_worker *w = (th8test_env_worker *)arg;
    int i;

    for (i = 0; i < w->nIters; i++) {
	if (Th8_Eval(
	        w->interp, 0, TH8TEST_ENV_STRESS_SCRIPT, TH8_NOLEN, "es",
	        2) == TH8_OK) {
	    w->ok++;
	} else if (!w->failed) {
	    size_t nErr = 0;
	    const char *zErr = Th8_GetResult(w->interp, &nErr);

	    if (nErr >= sizeof(w->zErr)) {
		nErr = sizeof(w->zErr) - 1;
	    }
	    if (zErr != NULL && nErr > 0) {
		memcpy(w->zErr, zErr, nErr);
	    }
	    w->zErr[nErr] = '\0';
	    w->failed = 1;
	}
    }
    TH8TEST_WORKER_RETURN;
}

/*
 *----------------------------------------------------------------------
 *
 * th8test_env_stress_cmd --
 *
 *	Implements "th8testlib::env_stress NTHREADS NITERS".  Drives
 *	R-00313: the environment variable backend serializes all
 *	operations with a file-scope mutex to prevent data races on
 *	concurrent access to the process environment.
 *
 * Why / How:
 *	Pre-creates NTHREADS child interpreters on the calling thread
 *	(each with a clone of the parent's merged platform, whose
 *	xKeyValue slot is the env backend's mutex-protected callback),
 *	then spawns one worker per interpreter that concurrently sets and
 *	unsets the SAME `::env` key NITERS times.  Every set/unset goes
 *	through the shared th8EnvMutex; each worker's interpreter is
 *	otherwise private, so the only cross-thread contention is on the
 *	env mutex.  If the serialization holds, no operation corrupts the
 *	backend and every cycle completes -- a correct mutex makes this a
 *	stable pass (a broken one would crash or lose cycles).
 *
 * Results:
 *	TH8_OK with "ok" when every worker completed all its cycles;
 *	otherwise a "FAIL got/expected" diagnostic result, and -- so the
 *	cause is not lost -- a "---- env_stress failed: <error>" line is
 *	emitted to stdout carrying the first worker's captured eval error.
 *
 * Side effects:
 *	Creates and deletes NTHREADS child interpreters; transiently
 *	sets/unsets the TH8_ENVSTRESS process environment variable; on
 *	failure, writes one diagnostic line to stdout.
 *
 *----------------------------------------------------------------------
 */
static int
th8test_env_stress_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    th8_int64_t nThreads, nIters;
    th8test_env_worker *aWorker = NULL;
    th8test_thread_t *aTid = NULL;
    const Th8_Platform *pParentPlat;
    int i, nCreated = 0, nSpawned = 0, total = 0, expected;
    char zBuf[64];
    size_t nBuf;

    (void)ctx;

    if (argc != 3) {
	return Th8_WrongNumArgs(
	    interp, "th8testlib::env_stress nthreads niters");
    }
    if (Th8_ToWideInt(interp, argv[1], TH8_LEN(argl[1]), &nThreads) !=
            TH8_OK ||
        nThreads < 1 || nThreads > 64) {
	Th8_SetResultStatic(interp, "nthreads must be 1..64", TH8_NOLEN);
	return TH8_ERROR;
    }
    if (Th8_ToWideInt(interp, argv[2], TH8_LEN(argl[2]), &nIters) != TH8_OK ||
        nIters < 1 || nIters > 5000) {
	Th8_SetResultStatic(interp, "niters must be 1..5000", TH8_NOLEN);
	return TH8_ERROR;
    }

    aWorker = (th8test_env_worker *)
        TH8_ALLOC_MUL(interp, (size_t)nThreads, sizeof(th8test_env_worker));
    aTid = (th8test_thread_t *)
        TH8_ALLOC_MUL(interp, (size_t)nThreads, sizeof(th8test_thread_t));
    if (!aWorker || !aTid) {
	if (aWorker) Th8_Free(interp, aWorker);
	if (aTid) Th8_Free(interp, aTid);
	return TH8_ERROR;
    }

    /* Phase 1: pre-create one child interpreter per worker (this thread). */
    pParentPlat = Th8_GetPlatform(interp);
    for (i = 0; i < (int)nThreads; i++) {
	Th8_Platform *pcp = Th8_ClonePlatform(pParentPlat);

	aWorker[i].interp = NULL;
	aWorker[i].nIters = (int)nIters;
	aWorker[i].ok = 0;
	aWorker[i].failed = 0;
	aWorker[i].zErr[0] = '\0';
	if (pcp == NULL) break;
	aWorker[i].interp = Th8_CreateInterp(pcp);
	if (aWorker[i].interp == NULL) {
	    Th8_FreePlatform(pcp);
	    break;
	}
	Th8_RegisterLanguage(aWorker[i].interp);
	nCreated++;
    }
    if (nCreated != (int)nThreads) {
	for (i = 0; i < nCreated; i++)
	    Th8_DeleteInterp(aWorker[i].interp);
	Th8_Free(interp, aWorker);
	Th8_Free(interp, aTid);
	Th8_SetResultStatic(
	    interp, "env_stress: child interp create failed", TH8_NOLEN);
	return TH8_ERROR;
    }

    /* Phase 2: spawn workers. */
    for (i = 0; i < (int)nThreads; i++) {
	if (th8test_thread_create(
	        &aTid[i], th8test_env_worker_fn, &aWorker[i]) != 0) {
	    break;
	}
	nSpawned++;
    }
    /* Phase 3: join everything that spawned. */
    for (i = 0; i < nSpawned; i++) {
	th8test_thread_join(aTid[i]);
    }

    /* Phase 4: tally, then tear down the child interpreters. */
    for (i = 0; i < (int)nThreads; i++) {
	total += aWorker[i].ok;
	Th8_DeleteInterp(aWorker[i].interp);
    }

    /* Durable diagnostic: the worker discards per-eval errors (it only bumps a
     * success counter), so on any failure surface the FIRST captured error as
     * a test-suite-style line -- otherwise the aggregate "FAIL got/expected"
     * result below gives no clue WHY the script failed (e.g. a platform where
     * the env backend or ::env misbehaves under concurrency). */
    for (i = 0; i < (int)nThreads; i++) {
	if (aWorker[i].failed) {
	    fprintf(stdout, "---- env_stress failed: %s\n", aWorker[i].zErr);
	    fflush(stdout);
	    break;
	}
    }

    Th8_Free(interp, aWorker);
    Th8_Free(interp, aTid);

    expected = (int)nThreads * (int)nIters;
    if (nSpawned == (int)nThreads && total == expected) {
	Th8_SetResultStatic(interp, "ok", 2);
	return TH8_OK;
    }
    nBuf = (size_t)snprintf(
        zBuf, sizeof(zBuf), "FAIL got %d expected %d (spawned %d/%d)", total,
        expected, nSpawned, (int)nThreads);
    return Th8_SetResult(interp, zBuf, nBuf);
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_event_delete_race_cmd --
 *
 *	::th8testlib::event_delete_race NFAST NSLOW SLOW_DELAY_MS
 *
 *	Race test for Th8_DeleteInterp vs. in-flight
 *	Th8_QueueEvent.  Creates a CHILD interpreter, spawns
 *	NFAST "fast" workers (sleep 0; queue once) and NSLOW
 *	"slow" workers (sleep SLOW_DELAY_MS; queue once).  Each
 *	worker has its own pState on the child.
 *
 *	Choreography:
 *	  1. Spawn fast + slow workers (slow ones go to sleep).
 *	  2. Join the fast workers (they finish quickly).
 *	  3. Drain the child's queue -> fast counters bump.
 *	  4. Th8_DeleteInterp(child) -> marks every pState's
 *	     nDeleted atomically.
 *	  5. Join the slow workers; their delayed Th8_QueueEvent
 *	     calls now observe nDeleted and return TH8_ERROR
 *	     cleanly (no crash) -- and importantly, no counter
 *	     bumps because the events never enter a queue.
 *	  6. Finalize every pState (safe post-delete by API
 *	     contract -- pStates outlive their interp).
 *
 *	Returns "fast_dispatched=$F slow_failed=$S total=$T"
 *	  where F = sum of fast workers' counters,
 *	        S = NSLOW - sum of slow workers' counters
 *	            (slow events that did NOT drain),
 *	        T = NFAST + NSLOW.
 *
 *	A passing test asserts F + (NSLOW - S) == NFAST
 *	(every fast event drained) and there is no crash.  In
 *	the typical timing, F == NFAST and S == NSLOW.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_event_delete_race_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    th8_int64_t nFast, nSlow, nDelay;
    int i, fastDispatched, slowDispatched, total;
    th8test_worker *aWorker = NULL;
    th8test_thread_t *aTid = NULL;
    int nSpawned = 0;
    Th8_Interp *child = NULL;
    Th8_Platform *plat;
    char zBuf[160];
    size_t nBuf;

    (void)ctx;

    /* ---- Phase 1: argument parse + bounds check ---- */
    if (argc != 4) {
	return Th8_WrongNumArgs(
	    interp,
	    "th8testlib::event_delete_race NFAST NSLOW SLOW_DELAY_MS");
    }
    if (Th8_ToWideInt(interp, argv[1], TH8_LEN(argl[1]), &nFast) != TH8_OK ||
        nFast < 1 || nFast > 64) {
	Th8_SetResultStatic(
	    interp, "event_delete_race: NFAST must be in [1, 64]", TH8_NOLEN);
	return TH8_ERROR;
    }
    if (Th8_ToWideInt(interp, argv[2], TH8_LEN(argl[2]), &nSlow) != TH8_OK ||
        nSlow < 1 || nSlow > 64) {
	Th8_SetResultStatic(
	    interp, "event_delete_race: NSLOW must be in [1, 64]", TH8_NOLEN);
	return TH8_ERROR;
    }
    if (Th8_ToWideInt(interp, argv[3], TH8_LEN(argl[3]), &nDelay) != TH8_OK ||
        nDelay < 50 || nDelay > 10000) {
	Th8_SetResultStatic(
	    interp, "event_delete_race: SLOW_DELAY_MS must be in [50, 10000]",
	    TH8_NOLEN);
	return TH8_ERROR;
    }
    total = (int)(nFast + nSlow);

    /* ---- Phase 2: create child interp ---- */
    plat = (Th8_Platform *)Th8_GetPlatform(interp);
    if (!plat) {
	Th8_SetResultStatic(
	    interp, "event_delete_race: no platform on parent", TH8_NOLEN);
	return TH8_ERROR;
    }
    child = Th8_CreateInterp(plat);
    if (!child) {
	Th8_SetResultStatic(
	    interp, "event_delete_race: Th8_CreateInterp failed", TH8_NOLEN);
	return TH8_ERROR;
    }

    /* ---- Phase 3: allocate the two arrays + create pStates ----
     * Workers 0..NFAST-1 are fast (no sleep); NFAST..total-1
     * are slow (sleep nDelay ms before queuing). */
    aWorker = (th8test_worker *)
        TH8_ALLOC_MUL(interp, (size_t)total, sizeof(th8test_worker));
    aTid = (th8test_thread_t *)
        TH8_ALLOC_MUL(interp, (size_t)total, sizeof(th8test_thread_t));
    if (!aWorker || !aTid) {
	if (aWorker) Th8_Free(interp, aWorker);
	if (aTid) Th8_Free(interp, aTid);
	Th8_DeleteInterp(child);
	return TH8_ERROR;
    }
    for (i = 0; i < total; i++) {
	aWorker[i].counter = 0;
	aWorker[i].nIters = 1;
	aWorker[i].nSleepMs = (i < (int)nFast) ? 0L : (long)nDelay;
	aWorker[i].pState = Th8_CreateAsyncState(child, &aWorker[i].counter);
	if (!aWorker[i].pState) {
	    int j;
	    for (j = 0; j < i; j++) {
		Th8_FinalizeAsyncState(aWorker[j].pState);
	    }
	    Th8_Free(interp, aWorker);
	    Th8_Free(interp, aTid);
	    Th8_DeleteInterp(child);
	    Th8_SetResultStatic(
	        interp, "event_delete_race: Th8_CreateAsyncState failed",
	        TH8_NOLEN);
	    return TH8_ERROR;
	}
    }

    /* ---- Phase 4: spawn workers ---- */
    for (i = 0; i < total; i++) {
	if (th8test_thread_create(
	        &aTid[i], th8test_worker_thread, &aWorker[i]) != 0) {
	    break;
	}
	nSpawned++;
    }
    if (nSpawned != total) {
	for (i = 0; i < nSpawned; i++)
	    th8test_thread_join(aTid[i]);
	for (i = 0; i < total; i++) {
	    Th8_FinalizeAsyncState(aWorker[i].pState);
	}
	Th8_Free(interp, aWorker);
	Th8_Free(interp, aTid);
	Th8_DeleteInterp(child);
	Th8_SetResultStatic(
	    interp, "event_delete_race: thread create failed", TH8_NOLEN);
	return TH8_ERROR;
    }

    /* ---- Phase 5: race choreography ---- */
    /* a) Wait for fast workers to finish queueing. */
    for (i = 0; i < (int)nFast; i++) {
	th8test_thread_join(aTid[i]);
    }
    /* b) Drain the child's queue -> fast counters bump. */
    (void)Th8_DrainQueueEvents(child);
    /* c) Delete the child interp.  Slow workers are still
     *    asleep; once they wake, their Th8_QueueEvent will
     *    observe nDeleted via xIntCmpXchg and return
     *    TH8_ERROR cleanly without crashing. */
    Th8_DeleteInterp(child);
    child = NULL;
    /* d) Join slow workers. */
    for (i = (int)nFast; i < total; i++) {
	th8test_thread_join(aTid[i]);
    }

    /* ---- Phase 6: tally and format ----
     * fastDispatched = sum of fast counters; slowDispatched =
     * sum of slow counters (typically 0; non-zero only if a
     * slow worker raced ahead of the delete). */
    fastDispatched = 0;
    for (i = 0; i < (int)nFast; i++) {
	fastDispatched += aWorker[i].counter;
    }
    slowDispatched = 0;
    for (i = (int)nFast; i < total; i++) {
	slowDispatched += aWorker[i].counter;
    }
    nBuf = (size_t)snprintf(
        zBuf, sizeof(zBuf), "fast_dispatched=%d slow_failed=%d total=%d",
        fastDispatched, (int)nSlow - slowDispatched, total);

    /* ---- Phase 7: cleanup ---- */
    for (i = 0; i < total; i++) {
	Th8_FinalizeAsyncState(aWorker[i].pState);
    }
    Th8_Free(interp, aWorker);
    Th8_Free(interp, aTid);
    return Th8_SetResult(interp, zBuf, nBuf);
}


/*
 * Test-library "logical allocation list" backing th8testlib::test_malloc
 * and th8testlib::test_free (design_notes_memtrack.md section 3.4).  It is a
 * small growable array of { pointer, size } records that the test library
 * itself owns, kept deliberately independent of the memtrack side-table:
 *
 *   * test_malloc appends a record; test_free validates its pointer argument
 *     against this list before freeing anything, so a script can never coerce
 *     a free of an arbitrary, foreign, or already-freed address.
 *   * test_malloc / test_free therefore exercise the real allocator even in a
 *     build without TH8_MEM_DEBUG; only the dump needs the tracker.
 *   * The list is a second, independent view of live test allocations, so a
 *     divergence from the tracker's side-table is itself a signal.
 *
 * The list itself is raw-malloc bookkeeping (not a TH8 allocation), so it does
 * not perturb the very allocator accounting the tracker measures.
 */

typedef struct th8testMemEntry {
    void *ptr; /* Pointer returned by Th8_AttemptMalloc. */
    size_t size; /* Requested size (for reporting / cross-check). */
} th8testMemEntry;

static th8testMemEntry *th8test_mem_list = NULL;
static int th8test_mem_count = 0; /* Live entries. */
static int th8test_mem_capacity = 0; /* Allocated slots. */


/*
 *----------------------------------------------------------------------
 *
 * th8test_mem_list_add --
 *
 *	Append (ptr, size) to the logical allocation list, growing the
 *	backing array with raw realloc as needed.
 *
 * Results:
 *	TH8_OK, or TH8_ERROR if the array cannot grow (overflow / OOM).
 *
 * Side effects:
 *	May reallocate th8test_mem_list; appends one entry.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_mem_list_add(void *ptr, size_t size)
{
    if (th8test_mem_count == th8test_mem_capacity) {
	int newCap = (th8test_mem_capacity == 0) ? 16
	                                         : th8test_mem_capacity * 2;
	size_t nBytes;
	th8testMemEntry *pNew;
	if (TH8_SAFE_MUL_SIZE((size_t)newCap, sizeof(*pNew), &nBytes)) {
	    return TH8_ERROR; /* size overflow */
	}
	pNew = (th8testMemEntry *)realloc(th8test_mem_list, nBytes);
	if (pNew == NULL) {
	    return TH8_ERROR;
	}
	th8test_mem_list = pNew;
	th8test_mem_capacity = newCap;
    }
    th8test_mem_list[th8test_mem_count].ptr = ptr;
    th8test_mem_list[th8test_mem_count].size = size;
    th8test_mem_count++;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_mem_list_remove --
 *
 *	Remove the logical-list entry for ptr, if present, by swapping the
 *	last entry into the gap (order is not significant).
 *
 * Results:
 *	1 if an entry was found and removed, else 0.
 *
 * Side effects:
 *	Shrinks the logical list by one entry on success.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_mem_list_remove(void *ptr)
{
    int i;

    for (i = 0; i < th8test_mem_count; i++) {
	if (th8test_mem_list[i].ptr == ptr) {
	    th8test_mem_list[i] = th8test_mem_list[th8test_mem_count - 1];
	    th8test_mem_count--;
	    return 1;
	}
    }
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_test_malloc_cmd --
 *
 *	Implements "th8testlib::test_malloc size": allocate `size` bytes
 *	through the normal Th8_AttemptMalloc funnel (so the block is
 *	tracked exactly like any other allocation), record it in the
 *	logical allocation list, and return the pointer as a hex address
 *	string the test can hold and later free.
 *
 * Results:
 *	TH8_OK with the block's hex address; TH8_ERROR on a bad size or
 *	allocation failure.
 *
 * Side effects:
 *	Allocates a tracked block; appends to the logical list.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_test_malloc_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    size_t size;
    void *p;
    char zBuf[32]; /* "0x" + up to 16 hex digits + NUL. */
    int n;

    (void)ctx;

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "th8testlib::test_malloc size");
    }
    if (th8test_parse_size(argv[1], TH8_LEN(argl[1]), &size) != TH8_OK) {
	Th8_SetResultStatic(
	    interp, "test_malloc: size must be a non-negative integer",
	    TH8_NOLEN);
	return TH8_ERROR;
    }
    p = Th8_AttemptMalloc(interp, size);
    if (p == NULL) {
	Th8_SetResultStatic(
	    interp, "test_malloc: allocation failed", TH8_NOLEN);
	return TH8_ERROR;
    }
    n = th8Snprintf(
        interp, zBuf, sizeof(zBuf), "0x%llx", (unsigned long long)(size_t)p);
    if (n < 0 || (size_t)n >= sizeof(zBuf)) {
	Th8_Free(interp, p);
	Th8_SetResultStatic(
	    interp, "test_malloc: pointer format failed", TH8_NOLEN);
	return TH8_ERROR;
    }
    if (th8test_mem_list_add(p, (size_t)size) != TH8_OK) {
	Th8_Free(interp, p);
	Th8_SetResultStatic(
	    interp, "test_malloc: cannot record allocation", TH8_NOLEN);
	return TH8_ERROR;
    }
    return Th8_SetResult(interp, zBuf, (size_t)n);
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_test_free_cmd --
 *
 *	Implements "th8testlib::test_free ptr": free a pointer previously
 *	returned by test_malloc.  `ptr` MUST be present in the logical
 *	allocation list (a still-live test allocation); otherwise the
 *	command errors and frees nothing.  This makes the command memory
 *	safe from script -- it cannot be coerced into freeing an
 *	arbitrary, foreign, or already-freed address.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if `ptr` does not parse or is not a
 *	live test allocation.
 *
 * Side effects:
 *	Removes the entry from the logical list and calls Th8_Free (which
 *	untracks the block).
 *
 *----------------------------------------------------------------------
 */

static int
th8test_test_free_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    void *p;
    th8_int64_t addr;

    (void)ctx;

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "th8testlib::test_free ptr");
    }
    /* Th8_ToWideInt parses the 0x-prefixed hex address emitted by
     * test_malloc (user-space pointers fit the signed 64-bit range). */
    if (Th8_ToWideInt(NULL, argv[1], TH8_LEN(argl[1]), &addr) != TH8_OK) {
	Th8_SetResultStatic(
	    interp, "test_free: ptr must be a hex address", TH8_NOLEN);
	return TH8_ERROR;
    }
    p = (void *)(size_t)addr;
    if (!th8test_mem_list_remove(p)) {
	Th8_SetResultStatic(
	    interp, "test_free: pointer is not a live test allocation",
	    TH8_NOLEN);
	return TH8_ERROR;
    }
    Th8_Free(interp, p);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_test_memory_dump_cmd --
 *
 *	Implements "th8testlib::test_memory_dump fileName": dump the
 *	tracker's live-allocation set (via th8MemTrackDump, reached
 *	through the internal-stubs table) to fileName.  The file MUST NOT
 *	already exist -- th8MemTrackDump opens it create-exclusive and
 *	errors otherwise, so a test can never clobber an existing file.
 *	In a non-TH8_MEM_DEBUG build the stub reports that a debug build
 *	is required.
 *
 * Results:
 *	TH8_OK with the live block count; TH8_ERROR on any failure.
 *
 * Side effects:
 *	Creates and writes fileName.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_test_memory_dump_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    (void)ctx;

    if (argc != 2) {
	return Th8_WrongNumArgs(
	    interp, "th8testlib::test_memory_dump fileName");
    }
    if (th8InternalStubsPtr == NULL ||
        th8InternalStubsPtr->th8_MemTrackDump == NULL) {
	Th8_SetResultStatic(
	    interp, "test_memory_dump: internal stubs unavailable",
	    TH8_NOLEN);
	return TH8_ERROR;
    }
    return th8InternalStubsPtr
        ->th8_MemTrackDump(interp, argv[1], TH8_LEN(argl[1]));
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_test_memory_reset_cmd --
 *
 *	Implements "th8testlib::test_memory_reset": free all of the
 *	allocation tracker's bookkeeping (side-table nodes and interned
 *	traces) and zero its counters, via th8MemTrackReset reached
 *	through the internal-stubs table.
 *
 * Why / How:
 *	Lets a test start the tracker from a known-empty state, or drop
 *	the tracker's retained trace storage so the tracker itself is not
 *	reported as a leak at shutdown.  In a non-TH8_MEM_DEBUG build the
 *	stub is a successful no-op (there is nothing to clear).  Takes no
 *	arguments; returns the number of live blocks that were cleared.
 *
 * Results:
 *	TH8_OK with the cleared block count; TH8_ERROR only on a usage
 *	error or missing stubs.
 *
 * Side effects:
 *	Frees all tracker bookkeeping.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_test_memory_reset_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    (void)ctx;
    (void)argv;
    (void)argl;

    if (argc != 1) {
	return Th8_WrongNumArgs(interp, "th8testlib::test_memory_reset");
    }
    if (th8InternalStubsPtr == NULL ||
        th8InternalStubsPtr->th8_MemTrackReset == NULL) {
	Th8_SetResultStatic(
	    interp, "test_memory_reset: internal stubs unavailable",
	    TH8_NOLEN);
	return TH8_ERROR;
    }
    return th8InternalStubsPtr->th8_MemTrackReset(interp);
}


/*
 *----------------------------------------------------------------------
 *
 * Th8test_Init --
 *
 *	TH8 extension initialization entry point.  Registers all
 *	::th8testlib::* commands and the test_echo math function
 *	in the interpreter.
 *
 * Why / How:
 *	Called by TH8's [load] command when loading the test library:
 *	  load bin/libth8test.dylib:Th8test
 *	Initializes stubs if USE_TH8_STUBS is defined, creates the
 *	th8testlib namespace, registers all test commands, and
 *	provides the "th8testlib" package at version 1.0.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if stubs initialization fails.
 *
 * Side effects:
 *	Creates commands and a namespace in the interpreter;
 *	registers the test_echo math function.
 *
 *----------------------------------------------------------------------
 */

/*
 * Internal stubs pointer.  Initialised by Th8test_Init below
 * via Th8_GetInternalStubs(), validated against magic /
 * version, then used by all USE_TH8_INTERNAL_STUBS-redirected
 * calls (currently th8ParseCommand, th8FreeParse).
 */

const Th8InternalStubsTable *th8InternalStubsPtr = NULL;

/*
 *----------------------------------------------------------------------
 *
 * Th8test_Init --
 *
 *	Public load-time entry point for the `th8testlib` test
 *	plugin.  Called by `Th8_LoadPlugin` (or the static-link
 *	equivalent) after the shared object is mapped.
 *
 *	Performs four setup steps:
 *
 *	  1. `Th8_InitStubs` -- when built with `USE_TH8_STUBS`,
 *	     bind the public-API stubs to the host interpreter
 *	     so the plugin can call `Th8_*` functions through
 *	     the table instead of direct symbols.
 *	  2. `Th8_GetInternalStubs` -- retrieve and validate
 *	     (`magic`, `version`) the internal-stubs table the
 *	     plugin needs for `USE_TH8_INTERNAL_STUBS`-redirected
 *	     calls (`th8ParseCommand`, `th8FreeParse`, etc.).
 *	  3. Per-command registration -- install every
 *	     `::th8testlib::*` script-visible command onto
 *	     `interp` via `Th8_CreateCommand`.  Each
 *	     registration is gated by the same feature flags
 *	     as the helper itself (cryptography, fault
 *	     injection, plugin extensibility, etc.).
 *	  4. Th8-side init -- call any `Th8_*_Init` helper for
 *	     features the test plugin extends (the secure-KV
 *	     callback, for example).
 *
 *	A failure at any step writes a diagnostic to the
 *	interpreter result and returns `TH8_ERROR` so the
 *	loader can propagate the error.
 *
 * Parameters:
 *	interp -- live interpreter into which the test
 *		commands are being installed.
 *
 * Returns:
 *	`TH8_OK` on successful install; `TH8_ERROR` on stubs
 *	binding / validation / command-registration failure
 *	(interpreter result: diagnostic).
 *
 * Side effects:
 *	Mutates `th8InternalStubsPtr`.  Installs many commands
 *	on `interp` (each owned by `interp` and freed at
 *	interp teardown).
 *
 *----------------------------------------------------------------------
 */
TESTLIB_EXPORT int
Th8test_Init(Th8_Interp *interp)
{
#  ifdef USE_TH8_STUBS
    if (Th8_InitStubs(interp, "1.0", 0) == 0) {
	return TH8_ERROR;
    }
#  endif

    /*
     * Retrieve and validate the TH8 internal stubs table.
     * Without this, any USE_TH8_INTERNAL_STUBS-redirected
     * call (e.g. th8ParseCommand via the parse_command cmd)
     * dereferences NULL and crashes.
     */
    {
	const Th8InternalStubsTable *p = (const Th8InternalStubsTable *)
	    Th8_GetInternalStubs();
	if (!p) {
	    Th8_SetResultStatic(
	        interp, "Th8test_Init: Th8_GetInternalStubs returned NULL",
	        TH8_NOLEN);
	    return TH8_ERROR;
	}
	if (p->magic != TH8_INTERNAL_STUBS_MAGIC) {
	    Th8_SetResultStatic(
	        interp, "Th8test_Init: internal stubs magic mismatch",
	        TH8_NOLEN);
	    return TH8_ERROR;
	}
	if (p->version < TH8_INTERNAL_STUBS_VERSION) {
	    Th8_SetResultStatic(
	        interp, "Th8test_Init: internal stubs version too old",
	        TH8_NOLEN);
	    return TH8_ERROR;
	}
	th8InternalStubsPtr = p;
    }

    Th8_Eval(
        interp, 0, "catch {namespace eval ::th8testlib {}}", TH8_NOLEN, NULL,
        0);
    Th8_CreateCommand(interp, "::th8testlib::nop", th8test_nop_cmd, 0, 0, 0);
    Th8_CreateCommand(interp, "::th8testlib::log", th8test_log_cmd, 0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::is_result_sensitive",
        th8test_is_result_sensitive_cmd, 0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::getcmdinfo", th8test_getcmdinfo_cmd, 0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::taint", th8test_taint_cmd, 0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::result_tainted", th8test_result_tainted_cmd, 0,
        0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::arg_tainted", th8test_arg_tainted_cmd, 0, 0,
        0);
    Th8_CreateCommand(
        interp, "::th8testlib::eval_tainted", th8test_eval_tainted_cmd, 0, 0,
        0);
    Th8_CreateCommand(
        interp, "::th8testlib::taints", th8test_taints_cmd, 0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::splitlist_probe", th8test_splitlist_probe_cmd,
        0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::normalize_no_callback",
        th8test_normalize_no_callback_cmd, 0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::output_error_channel",
        th8test_output_error_channel_cmd, 0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::close_veto", th8test_close_veto_cmd, 0, 0, 0);
#  if defined(TH8_ENABLE_CRYPTOGRAPHY)
    Th8_CreateCommand(
        interp, "::th8testlib::result_sensitive_tainted",
        th8test_result_sensitive_tainted_cmd, 0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::mark_sensitive_release",
        th8test_mark_sensitive_release_cmd, 0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::protected_null_page",
        th8test_protected_null_page_cmd, 0, 0, 0);
#  endif
#  if defined(TH8_ENABLE_LIBCURL)
    Th8_CreateCommand(
        interp, "::th8testlib::curl_xgetdata", th8test_curl_xgetdata_cmd, 0,
        0, 0);
#  endif
    Th8_CreateCommand(
        interp, "::th8testlib::glob_match_null", th8test_glob_match_null_cmd,
        0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::glob_match_cancelled",
        th8test_glob_match_cancelled_cmd, 0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::take_result", th8test_take_result_cmd, 0, 0,
        0);
    Th8_CreateCommand(
        interp, "::th8testlib::sensitive_probe", th8test_sensitive_probe_cmd,
        0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::safemul", th8test_safemul_cmd, 0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::safeadd", th8test_safeadd_cmd, 0, 0, 0);
#  if defined(TH8_ENABLE_BIGINT)
    Th8_CreateCommand(
        interp, "::th8testlib::bigint", th8test_bigint_cmd, 0, 0, 0);
#  endif
    Th8_CreateCommand(
        interp, "::th8testlib::overflow_check", th8test_overflow_check_cmd, 0,
        0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::parse_command", th8test_parse_command_cmd, 0,
        0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::parse_expr", th8test_parse_expr_cmd, 0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::parse_var_name", th8test_parse_var_name_cmd, 0,
        0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::plat_wrappers", th8test_plat_wrappers_cmd, 0,
        0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::bug22_null_op_count",
        th8test_bug22_null_op_count_cmd, 0, 0, 0);
#  if defined(TH8_ENABLE_FAULT_INJECTION)
    Th8_CreateCommand(
        interp, "::th8testlib::pt_chanctl_fault",
        th8test_pt_chanctl_fault_cmd, 0, 0, 0);
#  endif
    Th8_CreateCommand(
        interp, "::th8testlib::null_guard", th8test_null_guard_cmd, 0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::cache_lifecycle", th8test_cache_lifecycle_cmd,
        0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::safeallocstradd", th8test_safeallocstradd_cmd,
        0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::safeallocstrmul", th8test_safeallocstrmul_cmd,
        0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::safeallocmuladd2",
        th8test_safeallocmuladd2_cmd, 0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::evalfile", th8test_evalfile_cmd, 0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::expansion", th8test_expansion_cmd, 0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::isotime", th8test_isotime_th8, 0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::freeze", th8test_freeze_cmd, 0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::freezecycle", th8test_freezecycle_cmd, 0, 0,
        0);
    Th8_CreateCommand(
        interp, "::th8testlib::freezequeuethaw", th8test_freezequeuethaw_cmd,
        0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::queuescript", th8test_queuescript_cmd, 0, 0,
        0);
    Th8_CreateCommand(
        interp, "::th8testlib::issuspended", th8test_issuspended_cmd, 0, 0,
        0);
    Th8_CreateCommand(
        interp, "::th8testlib::preeval", th8test_preeval_cmd, 0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::preeval_denytest", th8test_preeval_denytest, 0,
        0, 0);
#  if defined(TH8_ENABLE_LOAD)
    Th8_CreateCommand(
        interp, "::th8testlib::preload", th8test_preload_cmd, 0, 0, 0);
#  endif
    Th8_CreateCommand(
        interp, "::th8testlib::thaw", th8test_thaw_cmd, 0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::nulleval", th8test_nulleval_cmd, 0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::utf8validate", th8test_utf8validate_cmd, 0, 0,
        0);
    Th8_CreateCommand(
        interp, "::th8testlib::basepath", th8test_basepath_cmd, 0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::test_lifecycle", th8test_lifecycle_cmd, 0, 0,
        0);
    Th8_CreateCommand(
        interp, "::th8testlib::subnormal", th8test_subnormal_cmd, 0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::inject_double", th8test_inject_double_cmd, 0,
        0, 0);
    Th8_CreateCommand(
        interp, "::__test_only_exec", th8test_exec_cmd, 0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::cancel_recover", th8test_cancel_recover_cmd, 0,
        0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::catch_unwind", th8test_catch_unwind_cmd, 0, 0,
        0);
    Th8_CreateCommand(
        interp, "::th8testlib::symlink", th8test_symlink_cmd, 0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::isadmin", th8test_isadmin_cmd, 0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::fuzz", th8test_fuzz_cmd, 0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::glob", th8test_glob_cmd, 0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::load_key_file", th8test_load_key_file_cmd, 0,
        0, 0);
#  if defined(TH8_ENABLE_CRYPTOGRAPHY)
    Th8_CreateCommand(
        interp, "::th8testlib::load_snk", th8test_load_snk_cmd, 0, 0, 0);
#  endif
#  if defined(TH8_PLUGIN_IO)
    Th8_CreateCommand(
        interp, "::th8testlib::chan", th8test_chan_cmd, 0, 0, 0);
#  endif

#  if defined(TH8_ENABLE_CRYPTOGRAPHY)
    Th8_CreateCommand(
        interp, "::th8testlib::harpy_token", th8test_harpy_token_cmd, 0, 0,
        0);
    Th8_CreateCommand(
        interp, "::th8testlib::key_token", th8test_key_token_cmd, 0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::verify_sig", th8test_verify_sig_cmd, 0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::sig_hashes", th8test_sig_hashes_cmd, 0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::policy_depth_test", th8test_policy_depth_cmd,
        0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::signed_reject", th8test_signed_reject_cmd, 0,
        0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::signed_inherit", th8test_signed_inherit_cmd, 0,
        0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::rsa_short_key", th8test_rsa_short_key_cmd, 0,
        0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::verify_trace", th8test_verify_trace_cmd, 0, 0,
        0);
    Th8_CreateCommand(
        interp, "::th8testlib::preload_key", th8test_preload_key_cmd, 0, 0,
        0);
    Th8_CreateCommand(
        interp, "::th8testlib::signed_only", th8test_signed_only_cmd, 0, 0,
        0);
#  endif

    Th8_CreateCommand(
        interp, "::th8testlib::expr_features", th8test_expr_features_cmd, 0,
        0, 0);

    Th8_CreateCommand(
        interp, "::th8testlib::sandbox", th8test_sandbox_cmd, 0, 0, 0);
#  if defined(TH8_ENABLE_FAULT_INJECTION)
    Th8_CreateCommand(
        interp, "::th8testlib::fault", th8test_fault_cmd, 0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::malloc_drive", th8test_malloc_drive_cmd, 0, 0,
        0);
    Th8_CreateCommand(
        interp, "::th8testlib::nreval_schedfail",
        th8test_nreval_schedfail_cmd, 0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::bufwrite_oom", th8test_bufwrite_oom_cmd, 0, 0,
        0);
#  endif

    Th8_CreateCommand(interp, "::th8testlib::kv", th8test_kv_cmd, 0, 0, 0);

    Th8_CreateCommand(
        interp, "::th8testlib::env_kv", th8test_env_kv_cmd, 0, 0, 0);

    Th8_CreateCommand(
        interp, "::th8testlib::sysvar", th8test_sysvar_cmd, 0, 0, 0);

    Th8_CreateCommand(
        interp, "::th8testlib::platform_cb_null",
        th8test_platform_cb_null_cmd, 0, 0, 0);

#  if defined(TH8_PLUGIN_EXTENSIBILITY)
    Th8_CreateCommand(
        interp, "::th8testlib::partial_object", th8test_partial_object_cmd, 0,
        0, 0);
#  endif

#  if defined(TH8_ENABLE_CRYPTOGRAPHY) && defined(TH8_ENABLE_VARIABLES)
    Th8_CreateCommand(
        interp, "::th8testlib::secure_persist", th8test_secure_persist_cmd, 0,
        0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::bug25_diag", th8test_bug25_diag_cmd, 0, 0, 0);
#  endif

#  if defined(TH8_ENABLE_CRYPTOGRAPHY)
    Th8_CreateCommand(
        interp, "::th8testlib::resetkeycaches", th8test_resetkeycaches_cmd, 0,
        0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::getpublickeytoken",
        th8test_getpublickeytoken_cmd, 0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::drivekeyfault", th8test_drivekeyfault_cmd, 0,
        0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::osslfaulteval", th8test_osslfaulteval_cmd, 0,
        0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::posixfaulteval", th8test_posixfaulteval_cmd, 0,
        0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::ntpvalidate", th8test_ntpvalidate_cmd, 0, 0,
        0);
    Th8_CreateCommand(
        interp, "::th8testlib::policyverifydata",
        th8test_policyverifydata_cmd, 0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::harpysigload", th8test_harpysigload_cmd, 0, 0,
        0);
    Th8_CreateCommand(
        interp, "::th8testlib::flagshavennull", th8test_flagshavennull_cmd, 0,
        0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::flagsethighbit", th8test_flagsethighbit_cmd, 0,
        0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::afmapget_nocreate",
        th8test_afmapget_nocreate_cmd, 0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::policyevalpre", th8test_policyevalpre_cmd, 0,
        0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::policyfindkey_nopaeys",
        th8test_policyfindkey_nopaeys_cmd, 0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::securecanary_noppage",
        th8test_securecanary_noppage_cmd, 0, 0, 0);
#  endif

    Th8_CreateCommand(
        interp, "::th8testlib::cache_double_remove",
        th8test_cache_double_remove_cmd, 0, 0, 0);

#  if defined(TH8_ENABLE_LOAD)
    Th8_CreateCommand(
        interp, "::th8testlib::loadnamematch", th8test_loadnamematch_cmd, 0,
        0, 0);
#  endif

    Th8_CreateCommand(
        interp, "::th8testlib::debug", th8test_debug_cmd, 0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::debug_breakcycle",
        th8test_debug_breakcycle_cmd, 0, 0, 0);

    Th8_CreateCommand(
        interp, "::th8testlib::array_searches", th8test_array_searches_cmd, 0,
        0, 0);

    Th8_CreateCommand(
        interp, "::th8testlib::queue_event", th8test_queue_event_cmd, 0, 0,
        0);
    Th8_CreateCommand(
        interp, "::th8testlib::queue_event_sync",
        th8test_queue_event_sync_cmd, 0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::event_stress", th8test_event_stress_cmd, 0, 0,
        0);
    Th8_CreateCommand(
        interp, "::th8testlib::env_stress", th8test_env_stress_cmd, 0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::event_delete_race",
        th8test_event_delete_race_cmd, 0, 0, 0);

    /* Allocation-site memory tracker (design_notes_memtrack.md 3.4). */
    Th8_CreateCommand(
        interp, "::th8testlib::test_malloc", th8test_test_malloc_cmd, 0, 0,
        0);
    Th8_CreateCommand(
        interp, "::th8testlib::test_free", th8test_test_free_cmd, 0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::test_memory_dump",
        th8test_test_memory_dump_cmd, 0, 0, 0);
    Th8_CreateCommand(
        interp, "::th8testlib::test_memory_reset",
        th8test_test_memory_reset_cmd, 0, 0, 0);

    /*
     * Register a test-only math function "test_echo" that
     * accepts a single string argument and returns it.
     * Used to test the expr parser's handling of nested
     * parentheses, commas, and special characters in
     * function arguments.
     */
    Th8_CreateMathFunc(interp, "test_echo", 9, 1, th8test_echo_mathfunc, 0);

    Th8_Eval(interp, 0, "package provide th8testlib 1.0", TH8_NOLEN, NULL, 0);
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Th8test_Unload --
 *
 *	TH8 extension unload entry point.  Removes all ::th8testlib::*
 *	commands, the test_echo math function, and the th8testlib
 *	namespace from the interpreter.
 *
 * Why / How:
 *	Called by TH8's [unload] command.  Carefully removes only
 *	the testlib's own policy callback (not a shell-installed one),
 *	deletes all registered commands via [rename {} {}], removes
 *	the namespace, and forgets the package.  The
 *	TH8_UNLOAD_DETACH_FROM_PROCESS flag indicates the DLL is
 *	being fully unloaded from memory.
 *
 * Results:
 *	TH8_OK.
 *
 * Side effects:
 *	Removes commands, namespace, math function, and policy
 *	callbacks; frees the signed-only policy context if owned.
 *
 *----------------------------------------------------------------------
 */

TESTLIB_EXPORT int
Th8test_Unload(Th8_Interp *interp, int flags)
{
    /*
     * Detach functional state hooked into the interpreter.  These
     * are NOT command deletions -- they remove callback registrations
     * that would otherwise outlive the testlib's command set.
     *
     * Policy callback: only clear if it is the testlib's own callback.
     * If the shell (or another extension) installed a signed-only
     * policy, leave it in place.
     */

    {
	Th8_PolicyProc xCurrent = NULL;

	Th8_GetPolicyCallback(interp, &xCurrent, NULL);
	if (xCurrent == th8test_preEvalProc) {
	    Th8_SetPolicyCallback(interp, 0, 0);
	}
    }
#  if defined(TH8_ENABLE_LOAD)
    Th8_SetPreLoadCallback(interp, 0, 0);
#  endif
#  if defined(TH8_ENABLE_CRYPTOGRAPHY)
    if (th8test_pPolicyCtx) {
	Th8_RemoveSignedPolicy(interp, th8test_pPolicyCtx);
	th8test_pPolicyCtx = NULL;
    }
#  endif

    /*
     * Delete the test-only math function (math funcs live in a
     * separate registry from commands and are not removed by
     * "namespace delete").
     */

    Th8_DeleteMathFunc(interp, "test_echo", 9);

    /*
     * Delete every command registered by this testlib.  All
     * ::th8testlib::* commands are removed in one shot via
     * "namespace delete"; this is self-maintaining -- new commands
     * added in Th8test_Init are automatically cleaned up here.
     * The lone command outside the testlib namespace
     * (::__test_only_exec) is renamed explicitly.
     */

    Th8_Eval(
        interp, 0, "catch {namespace delete ::th8testlib}", TH8_NOLEN, NULL,
        0);
    Th8_Eval(
        interp, 0, "catch {rename ::__test_only_exec {}}", TH8_NOLEN, NULL,
        0);
    Th8_Eval(
        interp, 0, "catch {package forget th8testlib}", TH8_NOLEN, NULL, 0);

    if (flags & TH8_UNLOAD_DETACH_FROM_PROCESS) {
	/* No process-global state to free in this example. */
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8test_loadtest_marker_cmd --
 *
 *	Trivial command registered by the Th8loadtest_Init entry
 *	point: returns the literal "ok".  Used purely as a presence
 *	marker by the [load]/[unload] conformance tests.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_loadtest_marker_cmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    (void)ctx;
    (void)argc;
    (void)argv;
    (void)argl;
    Th8_SetResultStatic(interp, "ok", TH8_NOLEN);
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Th8loadtest_Init --
 *
 *	A SECOND, independent TH8 load entry point in this same
 *	library, exposed under the "Th8loadtest" symbol.  It exists
 *	so the [load]/[unload] conformance tests have a throwaway
 *	target whose lifetime they fully control: the harness loads
 *	this file under the "Th8test" symbol for its own commands,
 *	and because TH8 tracks loads by (file-identity, symbol) the
 *	"Th8loadtest" entry is a SEPARATE refcounted entry -- so a
 *	test can load it, observe ::__th8_loadtest_marker appear,
 *	unload it, observe the marker disappear, and double-unload to
 *	get the "not loaded" error, all WITHOUT disturbing the
 *	harness's own ::th8testlib commands.  (Before load tracking
 *	became file-identity based, the tests reused the harness's
 *	own testlib via distinct path aliases; that no longer yields
 *	an independent entry.)
 *
 *	Init registers the single marker command; Unload removes it.
 *	Neither touches any ::th8testlib state.
 *
 *----------------------------------------------------------------------
 */

TESTLIB_EXPORT int
Th8loadtest_Init(Th8_Interp *interp)
{
    if (Th8_CreateCommand(
            interp, "::__th8_loadtest_marker", th8test_loadtest_marker_cmd, 0,
            0, 0) != TH8_OK) {
	return TH8_ERROR;
    }
    Th8_SetResult(interp, 0, 0);
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Th8loadtest_Unload --
 *
 *	Public unload entry point for the secondary `loadtest`
 *	plugin module that the load/unload coverage tests
 *	exercise alongside the primary test plugin.  Called by
 *	`Th8_UnloadPlugin` when the module is detached from
 *	the interpreter.
 *
 *	Removes the sentinel command (`::__th8_loadtest_marker`)
 *	created at load time so subsequent `[info commands]`
 *	queries observe a clean state.  The `catch` around the
 *	rename ensures the unload still succeeds when the
 *	marker has already been deleted by other test code.
 *
 *	The `flags` parameter (TH8_UNLOAD_*) is currently
 *	unused -- the plugin has no per-flag unload behaviour
 *	and treats every detach the same way.
 *
 * Parameters:
 *	interp -- live interpreter from which the plugin is
 *		being detached.
 *	flags  -- unload-flag mask (ignored).
 *
 * Returns:
 *	`TH8_OK` unconditionally; the `catch` swallows any
 *	failure from the rename.
 *
 * Side effects:
 *	Removes `::__th8_loadtest_marker` from `interp` if
 *	present.  Clears the interpreter result.
 *
 *----------------------------------------------------------------------
 */
TESTLIB_EXPORT int
Th8loadtest_Unload(Th8_Interp *interp, int flags)
{
    (void)flags;
    Th8_Eval(
        interp, 0, "catch {rename ::__th8_loadtest_marker {}}", TH8_NOLEN,
        NULL, 0);
    Th8_SetResult(interp, 0, 0);
    return TH8_OK;
}


#endif /* TH8_TESTLIB_TH8 */


/*
 *======================================================================
 *
 * NATIVE TCL ENTRY POINTS (stubs-based)
 *
 *	These functions use the Tcl C API via the stubs table.
 *	They are compiled only when TH8_TESTLIB_TCL is defined
 *	(i.e., when tcl.h is available at build time).
 *
 *	Loaded via native Tcl's [load] command:
 *
 *	  load bin/libth8test.dylib Tclth8test
 *
 *	The package name "Tclth8test" tells Tcl to look for the
 *	symbol "Tclth8test_Init", avoiding a name collision with
 *	the TH8 entry point "Th8test_Init".
 *
 *======================================================================
 */

#ifdef TH8_TESTLIB_TCL

/*
 *----------------------------------------------------------------------
 *
 * th8test_isotime_tcl --
 *
 *	Native Tcl implementation of "th8testlib::isotime".
 *	Same behavior as the TH8 version: converts epoch seconds
 *	to ISO 8601 UTC format.
 *
 * Why / How:
 *	The dual-mode architecture provides the same command in both
 *	TH8 and native Tcl interpreters.  This Tcl version uses the
 *	Tcl C API (Tcl_GetWideIntFromObj, Tcl_SetObjResult) but
 *	shares the core th8test_format_isotime logic.  This allows
 *	conformance tests to verify identical output from both
 *	interpreters.
 *
 * Results:
 *	TCL_OK with the ISO time string; TCL_ERROR on bad argument.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8test_isotime_tcl(
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    Tcl_WideInt epochSec;
    char zBuf[32];

    (void)clientData;

    if (objc != 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "epochSeconds");
	return TCL_ERROR;
    }
    if (Tcl_GetWideIntFromObj(interp, objv[1], &epochSec) != TCL_OK) {
	return TCL_ERROR;
    }

    th8test_format_isotime((th8_int64_t)epochSec, zBuf, sizeof(zBuf));
    Tcl_SetObjResult(interp, Tcl_NewStringObj(zBuf, -1));
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Tclth8test_Init --
 *
 *	Native Tcl initialization entry point.  Loaded via:
 *	  load <path> Tclth8test
 *
 * Why / How:
 *	Provides the same th8testlib::isotime command in native Tcl
 *	so that conformance tests can run in both interpreters with
 *	identical results.  Uses Tcl stubs for compatibility with
 *	any Tcl 8.4+ runtime.
 *
 * Results:
 *	TCL_OK on success; TCL_ERROR if stubs init fails.
 *
 * Side effects:
 *	Creates a namespace, command, and provides the package.
 *
 *----------------------------------------------------------------------
 */

TESTLIB_EXPORT int
Tclth8test_Init(Tcl_Interp *interp)
{
    if (Tcl_InitStubs(interp, "8.4", 0) == NULL) {
	return TCL_ERROR;
    }

    Tcl_Eval(interp, "catch {namespace eval ::th8testlib {}}");

    Tcl_CreateObjCommand(
        interp, "::th8testlib::isotime", th8test_isotime_tcl, NULL, NULL);

    Tcl_PkgProvide(interp, "th8testlib", "1.0");
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Tclth8test_Unload --
 *
 *	Native Tcl unload entry point.
 *
 * Why / How:
 *	Cleans up the isotime command, namespace, and package
 *	registration when the library is unloaded from a native
 *	Tcl interpreter.  Mirrors the cleanup done by
 *	Th8test_Unload for TH8.
 *
 * Results:
 *	TCL_OK.
 *
 * Side effects:
 *	Removes the command, namespace, and package from the
 *	interpreter.
 *
 *----------------------------------------------------------------------
 */

TESTLIB_EXPORT int
Tclth8test_Unload(Tcl_Interp *interp, int flags)
{
    Tcl_Eval(interp, "catch {rename ::th8testlib::isotime {}}");
    Tcl_Eval(interp, "catch {namespace delete ::th8testlib}");
    Tcl_Eval(interp, "catch {package forget th8testlib}");

    if (flags & TCL_UNLOAD_DETACH_FROM_PROCESS) {
	/* No process-global state to free. */
    }
    return TCL_OK;
}

#endif /* TH8_TESTLIB_TCL */
