/*
 * th8_policy.c -- Signed-only script evaluation policy for TH8.
 *
 * Implements the unified policy callback that enforces script
 * signature verification.  When the signed-only gate is
 * enabled, every script submitted for evaluation must have a
 * companion Harpy .b64sig signature file.  The signature is
 * verified against the script text using the RSA public key
 * identified by the public key token in the signature file header.
 *
 * The public key is fetched from a well-known registry URL:
 *
 *     https://w.sb/r/pk_<publicKeyToken>
 *
 * Keys are cached per-context so that repeated evaluations with the
 * same signing key do not require repeated network fetches.
 *
 * Compile-time gate: TH8_ENABLE_CRYPTOGRAPHY
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "th8.h"
#include "th8_int.h"

#if defined(TH8_ENABLE_FAULT_INJECTION)
/*
 * Active fault-injection config, set by Th8_FaultInstall and
 * cleared by Th8_FaultUninstall (defined in src/th8_fault.c,
 * declared as extern in src/th8_int_core.h).  Re-declared here
 * (forward extern) so this translation unit can consult the
 * nFailEmbeddedKey0/Root/Test override flags from the
 * Th8_GetPublicKey* lazy-init paths without pulling in the
 * full th8_int_core.h header dependency stack.
 */
extern struct Th8_FaultConfig *th8FaultActiveCfg;
#endif

#if defined(TH8_ENABLE_CRYPTOGRAPHY)


/*
 *======================================================================
 *
 * File-local type definitions
 *
 *======================================================================
 */

/*
 * Th8_PolicyCtx --
 *	Opaque context for the signed-only policy callback.
 *	Caches the most recently used public key so that repeated
 *	evaluations signed by the same key avoid re-fetching.
 */
typedef struct Th8_PolicyCtx {
    Th8_Interp *interp;  /* Owning interpreter. */
    Th8_Hash *paKeys;  /* Token-to-key hash table.  Keys are
				 * 16-char hex token strings; values
				 * are Th8_RsaKey* pointers. */
    th8_int64_t nVerifyToken; /* Random token set after successful
				 * signature verification.  Nested evals
				 * (depth > 1) are only allowed when
				 * nVerifyOk matches nVerifyToken.  Uses
				 * the random-token pattern so a single-bit
				 * corruption cannot fake verification. */
    th8_int64_t nVerifyOk; /* Must equal nVerifyToken to pass. */
} Th8_PolicyCtx;

/*
 * Th8_ScriptAnnotations --
 *	Parsed results from a script-annotation scan
 *	(<<notBefore:...>>, <<notAfter:...>>, <<flags:...>>).
 */
typedef struct {
    int bHasNotBefore;  /* Non-zero if <<notBefore:...>> found. */
    int bHasNotAfter;  /* Non-zero if <<notAfter:...>> found. */
    int bHasFlags;  /* Non-zero if <<flags:...>> found. */
    th8_int64_t nNotBefore; /* Epoch seconds (UTC). */
    th8_int64_t nNotAfter; /* Epoch seconds (UTC). */
    char zNotBefore[21]; /* Raw timestamp string (NUL-terminated). */
    char zNotAfter[21];  /* Raw timestamp string (NUL-terminated). */
    char zFlags[256];  /* Raw flags string (NUL-terminated). */
    size_t nFlags;  /* Flags string length. */
    int bError;   /* Non-zero if a malformed annotation found. */
} Th8_ScriptAnnotations;


#  if defined(TH8_ENABLE_VARIABLES)
/*
 *----------------------------------------------------------------------
 *
 * th8PolicyResetSecurity --
 *
 *	Reset all elements of the ::th8_security array to "none".
 *	This is called at the top of every eval invocation so
 *	that each script starts with a clean security state.
 *
 * Why / How:
 *	Security metadata from a previously verified script must
 *	never be visible to an unverified one.  By resetting the
 *	array at the start of each top-level evaluation, scripts
 *	that fail verification (or run without it) cannot read
 *	stale policy, publicKeyToken, or algorithmName values.
 *
 * Results:
 *	TH8_OK when all seven elements were reset; TH8_ERROR on the
 *	first allocation failure (the array may be partially reset).
 *	A TH8_ERROR MUST fail the verification so a partial security
 *	array is never exposed to the script (TH8K-006/-020).
 *
 * Side effects:
 *	Sets every element of the ::th8_security array variable
 *	to "none".
 *
 *----------------------------------------------------------------------
 */

static int
th8PolicyResetSecurity(Th8_Interp *interp)
{
    return Th8_ResetSecurityArray(interp);
}
#  endif


/*
 *----------------------------------------------------------------------
 *
 * th8PolicySetVerified --
 *
 *	Generate a fresh random 64-bit token via the platform's
 *	xRandomBytes callback and store it in both nVerifyToken
 *	and nVerifyOk fields of the policy context, marking the
 *	current evaluation as signature-verified.
 *
 * Why / How:
 *	The random-token pattern is used instead of a simple boolean
 *	flag so that a single-bit memory corruption cannot fake a
 *	successful verification.  Both fields must be non-zero and
 *	equal to pass the IsVerified check.  The token is retried
 *	up to 100 times to avoid degenerate values (0, all-ones, 1).
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR if a valid random token could
 *	not be generated after 100 retries.
 *
 * Side effects:
 *	Overwrites p->nVerifyToken and p->nVerifyOk with a fresh
 *	random value.  Calls into the platform random-bytes provider.
 *
 *----------------------------------------------------------------------
 */

static int
th8PolicySetVerified(Th8_PolicyCtx *p)
{
    th8_int64_t tok = 0;
    int retries = 0;
    unsigned char buf[8];

    for (;;) {
	int bRegen;

	if (++retries > 100) {
	    return TH8_ERROR;
	}
	if (TH8_OK == Th8_RandomBytes(p->interp, buf, 8)) {
	    size_t j;

	    tok = 0;
	    for (j = 0; j < 8; j++) {
		tok |= ((th8_uint64_t)buf[j]) << (j * 8);
	    }
	}
	/* Bug 26 (2026-06-07): plain while -- regen IS the handler.
	 * Split per Finding 005 sec. 5b: each of the three sentinel
	 * rejections is its own decision so the (overwhelmingly
	 * intrinsic-dead) regen arms can be attributed individually
	 * by clang's MC/DC representation. */
	bRegen = 0;
	if (tok == 0) bRegen = 1;
	if (tok == ~(th8_int64_t)0) bRegen = 1;
	if (tok == 1) bRegen = 1;
	if (!bRegen) break;
    }

    p->nVerifyToken = tok;
    p->nVerifyOk = tok;
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * th8PolicyClearVerified --
 *
 *	Zero both nVerifyToken and nVerifyOk in the policy context,
 *	invalidating any prior verification state.
 *
 * Why / How:
 *	Called at the start of every new top-level evaluation and
 *	after the EVAL POST phase to ensure that a stale verification
 *	from a prior script cannot leak into subsequent evaluations.
 *	Zeroing both fields causes th8PolicyIsVerified to return
 *	false.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Clears the verification token fields.
 *
 *----------------------------------------------------------------------
 */

static void
th8PolicyClearVerified(Th8_PolicyCtx *p)
{
    p->nVerifyToken = 0;
    p->nVerifyOk = 0;
}

/*
 *----------------------------------------------------------------------
 *
 * th8PolicyIsVerified --
 *
 *	Test whether the policy context currently holds a valid
 *	verification token.  Returns non-zero only when both
 *	nVerifyOk and nVerifyToken are non-zero and equal.
 *
 * Why / How:
 *	The dual-field comparison guards against single-bit memory
 *	corruption: a zero in either field, or a mismatch between
 *	them, causes this function to return false.  This is the
 *	sole gate that allows nested evaluations to proceed without
 *	re-verifying the signature.
 *
 * Results:
 *	Non-zero if verified, zero otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8PolicyIsVerified(const Th8_PolicyCtx *p)
{
    /* Split per Finding 005 sec. 5b: the nVerifyOk != 0 arm and
     * the matched-token arm are independent (zeroing both is the
     * teardown sentinel; tamper detection nulls only Ok).  Test
     * corpus rarely sees the (nVerifyOk != 0, mismatched token)
     * combination; the split exposes the tamper arm as its own
     * decision rather than burying it as a C2-pair. */
    if (p->nVerifyOk == 0) return 0;
    if (p->nVerifyOk != p->nVerifyToken) return 0;
    return 1;
}


/*
 *----------------------------------------------------------------------
 *
 * th8PolicyIsRelativePath --
 *
 *	Return non-zero if the name looks like a local relative path
 *	(i.e. does not start with '/' or a drive letter on Windows).
 *
 * Why / How:
 *	The signed-only policy requires script origins to be either
 *	relative paths or HTTP(S) URIs.  Absolute local paths are
 *	rejected because the signature file lookup is always
 *	relative to the script's own path.  This function checks
 *	for leading '/', backslash, or Windows drive-letter prefixes.
 *
 * Results:
 *	Non-zero if the path is relative, zero if it is absolute.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8PolicyIsRelativePath(const char *z, size_t n)
{
    if (n == 0) return 0;
    if (z[0] == '/') return 0;
#  if defined(_WIN32)
    if (n >= 2 && z[1] == ':') return 0;
#  endif
    if (z[0] == '\\') return 0;
    return 1;
}


/*
 *----------------------------------------------------------------------
 *
 * th8PolicyIsHttpUri --
 *
 *	Return non-zero if the name starts with "http://" or
 *	"https://".
 *
 * Why / How:
 *	HTTP(S) URIs are valid script origins for the signed-only
 *	policy because the companion .b64sig file can be fetched
 *	from the same base URL.  This function performs a simple
 *	prefix check using Th8_Memcmp.
 *
 * Results:
 *	Non-zero if the name is an HTTP or HTTPS URI, zero otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
th8PolicyIsHttpUri(Th8_Interp *interp, const char *z, size_t n)
{
    if (n >= 7 && Th8_Memcmp(interp, z, "http://", 7) == 0) return 1;
    if (n >= 8 && Th8_Memcmp(interp, z, "https://", 8) == 0) return 1;
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * Script annotation parsing.
 *
 *	Scans script text for <<key:value>> annotations and extracts
 *	notBefore, notAfter (timestamps), and flags (attribute flags).
 *
 *	Timestamp format: YYYY_MM_DDThh_mm_ssZ (underscores as
 *	delimiters, trailing Z for UTC).  All components are strictly
 *	validated including leap year bounds.
 *
 *	The annotation delimiters << and >> can appear anywhere in the
 *	script text, not just at the start of a line.
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8PolicyDaysInMonth --
 *
 *	Return the number of days in a given month (1-12) for a
 *	given year, accounting for leap years.
 *
 * Why / How:
 *	Used by th8PolicyParseTimestamp to validate day-of-month
 *	ranges and to accumulate total days when converting a
 *	timestamp to epoch seconds.  Leap year logic follows the
 *	Gregorian calendar rule: divisible by 4 except centuries,
 *	unless also divisible by 400.
 *
 * Results:
 *	The number of days in the given month (28-31), or 0 if
 *	month is out of range.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
th8PolicyDaysInMonth(int year, int month)
{
    static const int days[] = {0,  31, 28, 31, 30, 31, 30,
                               31, 31, 30, 31, 30, 31};
    int n;

    if (month < 1 || month > 12) return 0;
    n = days[month];
    if (month == 2) {
	/* Leap year: divisible by 4, except centuries unless /400. */
	if ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0) {
	    n++; /* 29 */
	}
    }
    return n;
}


/*
 *----------------------------------------------------------------------
 *
 * th8PolicyParseTimestamp --
 *
 *	Parse a timestamp in the format YYYY_MM_DDThh_mm_ssZ.
 *	The format is exactly 20 characters:
 *	  positions:  0123456789012345678901
 *	  pattern:    YYYY_MM_DDThh_mm_ssZ
 *	On success, stores the Unix epoch seconds in *pEpoch.
 *
 * Why / How:
 *	Script annotations embed notBefore/notAfter timestamps
 *	in this fixed format to enable time-based policy enforcement.
 *	The parser validates structural delimiters ('_', 'T', 'Z'),
 *	digit characters, and calendar ranges (including leap-year
 *	day bounds via th8PolicyDaysInMonth).  Epoch conversion
 *	accumulates days from 1970-01-01 then adds hours/minutes/
 *	seconds.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR on format or range error.
 *
 * Side effects:
 *	Writes to *pEpoch on success.
 *
 *----------------------------------------------------------------------
 */

static int
th8PolicyParseTimestamp(const char *z, size_t n, th8_int64_t *pEpoch)
{
    int year, month, day, hour, minute, second;
    th8_int64_t epoch;
    int i, maxDay;

    if (n != 20) return TH8_ERROR;

    /* Validate structural characters. */
    if (z[4] != '_' || z[7] != '_' || z[10] != 'T' || z[13] != '_' ||
        z[16] != '_' || z[19] != 'Z') {
	return TH8_ERROR;
    }

    /* Extract and validate digit groups. */
    year = month = day = hour = minute = second = 0;

    for (i = 0; i < 4; i++) {
	if (z[i] < '0' || z[i] > '9') return TH8_ERROR;
	year = year * 10 + (z[i] - '0');
    }
    for (i = 5; i < 7; i++) {
	if (z[i] < '0' || z[i] > '9') return TH8_ERROR;
	month = month * 10 + (z[i] - '0');
    }
    for (i = 8; i < 10; i++) {
	if (z[i] < '0' || z[i] > '9') return TH8_ERROR;
	day = day * 10 + (z[i] - '0');
    }
    for (i = 11; i < 13; i++) {
	if (z[i] < '0' || z[i] > '9') return TH8_ERROR;
	hour = hour * 10 + (z[i] - '0');
    }
    for (i = 14; i < 16; i++) {
	if (z[i] < '0' || z[i] > '9') return TH8_ERROR;
	minute = minute * 10 + (z[i] - '0');
    }
    for (i = 17; i < 19; i++) {
	if (z[i] < '0' || z[i] > '9') return TH8_ERROR;
	second = second * 10 + (z[i] - '0');
    }

    /* Range validation. */
    if (year < 1970 || year > 9999) return TH8_ERROR;
    if (month < 1 || month > 12) return TH8_ERROR;
    maxDay = th8PolicyDaysInMonth(year, month);
    if (day < 1 || day > maxDay) return TH8_ERROR;
    if (hour < 0 || hour > 23) return TH8_ERROR;
    if (minute < 0 || minute > 59) return TH8_ERROR;
    if (second < 0 || second > 60) return TH8_ERROR;

    /*
     * Convert to Unix epoch seconds.
     * Simple accumulation: days from 1970-01-01 to the target date,
     * then hours/minutes/seconds.
     */
    {
	int y, m;
	th8_int64_t totalDays = 0;

	for (y = 1970; y < year; y++) {
	    totalDays += ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 366
	                                                                : 365;
	}
	for (m = 1; m < month; m++) {
	    totalDays += th8PolicyDaysInMonth(year, m);
	}
	totalDays += (day - 1);

	epoch = totalDays * 86400 + (th8_int64_t)hour * 3600 +
	        (th8_int64_t)minute * 60 + (th8_int64_t)second;
    }

    *pEpoch = epoch;
    return TH8_OK;
}


/* Th8_ScriptAnnotations -- declared at top of file. */


/*
 *----------------------------------------------------------------------
 *
 * th8PolicyScanAnnotations --
 *
 *	Scan script text for <<key:value>> annotations.  Extracts
 *	notBefore, notAfter, and flags.  Validates timestamps strictly.
 *	Validates flags via Th8_AttrFlagsParse (no spaces allowed).
 *	Unknown annotation keys are silently ignored.
 *
 * Why / How:
 *	Annotations are embedded directly in the signed script text
 *	so that the signer controls their values and they cannot be
 *	tampered with after signing.  The scanner walks the script
 *	looking for "<<" / ">>" delimiters and dispatches on the
 *	key prefix (notBefore:, notAfter:, flags:).  Each known
 *	annotation is strictly validated at parse time; malformed
 *	values cause an immediate error return.
 *
 * Results:
 *	TH8_OK on success (including no annotations found).
 *	TH8_ERROR if a malformed annotation was found (interp result
 *	set with a diagnostic).
 *
 * Side effects:
 *	Populates the Th8_ScriptAnnotations struct pointed to by
 *	pAnn.  The struct is zeroed before scanning.
 *
 *----------------------------------------------------------------------
 */

static int
th8PolicyScanAnnotations(
    Th8_Interp *interp,
    const char *z,  /* Script text. */
    size_t n,   /* Script length. */
    Th8_ScriptAnnotations *pAnn)
{
    size_t i;

    Th8_Memset(interp, pAnn, 0, sizeof(*pAnn));

    for (i = 0; i + 3 < n; i++) {
	const char *zVal;
	size_t nVal;
	size_t j;

	if (z[i + 0] != '#' || z[i + 1] != ' ' || z[i + 2] != '<' ||
	    z[i + 3] != '<') {
	    continue;
	}

	/*
	 * Found "<<".  Look for ">>" to delimit the value.
	 */
	j = i + 2;
	/* Refactored from a 3-condition while-compound into a
	 * loop with an inner break so clang's MC/DC encoder can
	 * represent each decision.  See FINDINGS.md Finding 005. */
	while (j + 1 < n) {
	    if (z[j] == '>' && z[j + 1] == '>') break;
	    j++;
	}
	if (j + 1 >= n) {
	    Th8_SetResultStatic(
	        interp, "annotation: malformed, missing closing marker",
	        TH8_NOLEN);
	    return TH8_ERROR;
	}

	/*
	 * The annotation content is z[i+4..j-1].
	 * Check for known keys.
	 */
	{
	    const char *zContent = z + i + 4;
	    size_t nContent = j - (i + 4);

	    if (nContent > 10 &&
	        Th8_Memcmp(interp, zContent, "notBefore:", 10) == 0) {
		zVal = zContent + 10;
		nVal = nContent - 10;
		if (th8PolicyParseTimestamp(zVal, nVal, &pAnn->nNotBefore) !=
		    0) {
		    pAnn->bError = 1;
		    Th8_SetResultStatic(
		        interp, "annotation: invalid notBefore timestamp",
		        TH8_NOLEN);
		    return TH8_ERROR;
		}
		if (nVal < sizeof(pAnn->zNotBefore)) {
		    Th8_Memcpy(interp, pAnn->zNotBefore, zVal, nVal);
		    pAnn->zNotBefore[nVal] = '\0';
		}
		pAnn->bHasNotBefore = 1;

	    } else if (
	        nContent > 9 &&
	        Th8_Memcmp(interp, zContent, "notAfter:", 9) == 0) {
		zVal = zContent + 9;
		nVal = nContent - 9;
		if (th8PolicyParseTimestamp(zVal, nVal, &pAnn->nNotAfter) !=
		    0) {
		    pAnn->bError = 1;
		    Th8_SetResultStatic(
		        interp, "annotation: invalid notAfter timestamp",
		        TH8_NOLEN);
		    return TH8_ERROR;
		}
		if (nVal < sizeof(pAnn->zNotAfter)) {
		    Th8_Memcpy(interp, pAnn->zNotAfter, zVal, nVal);
		    pAnn->zNotAfter[nVal] = '\0';
		}
		pAnn->bHasNotAfter = 1;

	    } else if (
	        nContent > 6 &&
	        Th8_Memcmp(interp, zContent, "flags:", 6) == 0) {
		Th8_AfMap map;

		zVal = zContent + 6;
		nVal = nContent - 6;

		/* Validate: no spaces allowed, complex flags OK. */
		if (Th8_AttrFlagsParse(
		        interp, zVal, nVal, 1 /* bComplex */, 0 /* bSpace */,
		        &map) != TH8_OK) {
		    pAnn->bError = 1;
		    Th8_SetResultStatic(
		        interp, "annotation: invalid flags value", TH8_NOLEN);
		    return TH8_ERROR;
		}

		if (nVal < sizeof(pAnn->zFlags)) {
		    Th8_Memcpy(interp, pAnn->zFlags, zVal, nVal);
		    pAnn->zFlags[nVal] = '\0';
		    pAnn->nFlags = nVal;
		}
		pAnn->bHasFlags = 1;
	    }
	    /* Unknown annotations are silently ignored. */
	}

	/* Advance past the closing >>. */
	i = j + 1;
    }

    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8PolicyCheckAnnotations --
 *
 *	After scanning, enforce time-based constraints.  If notBefore
 *	or notAfter are present, check the current time against the
 *	specified range.  Also populates ::th8_security annotation
 *	elements (notBefore, notAfter, flags).
 *
 * Why / How:
 *	Time enforcement uses a remote time source (NTP or HTTPS)
 *	when TH8_ENABLE_CRYPTOGRAPHY is defined, falling back to
 *	local time via Th8_GetTimeMs if the remote query fails.
 *	This prevents bypassing notBefore/notAfter by adjusting
 *	the local system clock.  The environment variable
 *	TH8_FORCE_HTTPS_TIME selects HTTPS over the default NTP.
 *	Annotations are stored in ::th8_security regardless of
 *	whether the time check passes.
 *
 * Results:
 *	TH8_OK if the script is allowed, TH8_ERROR if the current
 *	time is outside the notBefore/notAfter window or if time
 *	cannot be determined.
 *
 * Side effects:
 *	Sets ::th8_security(notBefore), ::th8_security(notAfter),
 *	and ::th8_security(flags) variables when annotations are
 *	present.  May issue NTP or HTTPS network queries.
 *
 *----------------------------------------------------------------------
 */

static int
th8PolicyCheckAnnotations(
    Th8_Interp *interp,
    const Th8_ScriptAnnotations *pAnn)
{
    /* Populate ::th8_security with annotation values. */
#  if defined(TH8_ENABLE_VARIABLES)
    if (pAnn->bHasNotBefore) {
	Th8_SetVar(
	    interp, "::th8_security(notBefore)", TH8_NOLEN, pAnn->zNotBefore,
	    TH8_NOLEN);
    }
    if (pAnn->bHasNotAfter) {
	Th8_SetVar(
	    interp, "::th8_security(notAfter)", TH8_NOLEN, pAnn->zNotAfter,
	    TH8_NOLEN);
    }
    if (pAnn->bHasFlags) {
	Th8_SetVar(
	    interp, "::th8_security(flags)", TH8_NOLEN, pAnn->zFlags,
	    pAnn->nFlags);
    }
#  endif

    /* Time-based enforcement. */
    if (pAnn->bHasNotBefore || pAnn->bHasNotAfter) {
	char *zForceHttpsTime = Th8_GetEnv(interp, "TH8_FORCE_HTTPS_TIME");
	th8_int64_t nowSec = 0;
	int bHaveRemoteTime = 0;

	/*
	 * Synchronize with a remote time source before checking
	 * time-based annotations.  This prevents a script from
	 * bypassing notBefore/notAfter constraints by adjusting
	 * the local system clock.
	 *
	 * Default: NTP.  If the environment variable
	 * TH8_FORCE_HTTPS_TIME is set, use HTTPS time instead.
	 */

	if (zForceHttpsTime) {
	    Th8_Free(interp, zForceHttpsTime);
	    bHaveRemoteTime =
	        (th8HttpsTimeQuery(interp, NULL, 0, &nowSec) == TH8_OK);
	    if (!bHaveRemoteTime) {
		Th8_SetResultStatic(
		    interp,
		    "annotation: cannot determine current time via HTTPS",
		    TH8_NOLEN);
		return TH8_ERROR;
	    }
	} else {
	    /* Default server, DNSSEC-required (1): a policy time check must
	     * not silently accept an unvalidated answer. */
	    bHaveRemoteTime =
	        (th8NtpQuery(interp, NULL, 0, 5000, 0, 0, 1, &nowSec) ==
	         TH8_OK);
	}

	/*
	 * Fall back to local machine time if remote sync failed
	 * or cryptography is not enabled.
	 */
	if (!bHaveRemoteTime) {
	    th8_int64_t nowMs = 0;

	    if (Th8_GetTimeMs(interp, &nowMs) != TH8_OK) {
		Th8_SetResultStatic(
		    interp, "annotation: cannot determine current time",
		    TH8_NOLEN);
		return TH8_ERROR;
	    }
	    nowSec = nowMs / 1000;
	}

	if (pAnn->bHasNotBefore && nowSec < pAnn->nNotBefore) {
	    Th8_SetResultStatic(
	        interp,
	        "annotation: script is not yet valid "
	        "(current time is before notBefore)",
	        TH8_NOLEN);
	    return TH8_ERROR;
	}
	if (pAnn->bHasNotAfter && nowSec > pAnn->nNotAfter) {
	    Th8_SetResultStatic(
	        interp,
	        "annotation: script has expired "
	        "(current time is after notAfter)",
	        TH8_NOLEN);
	    return TH8_ERROR;
	}
	if (pAnn->bHasNotBefore && pAnn->bHasNotAfter &&
	    pAnn->nNotBefore > pAnn->nNotAfter) {
	    Th8_SetResultStatic(
	        interp, "annotation: script has bad time range", TH8_NOLEN);
	    return TH8_ERROR;
	}
    }

    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8PolicyFetchKey --
 *
 *	Fetch the RSA public key for the given hex token from the
 *	well-known registry.  The URL is:
 *
 *	    https://w.sb/r/pk_<hexToken>
 *
 *	The fetched data is parsed as an SNK/CAPI public key blob.
 *	On success, *ppKey is set to the parsed key (caller must
 *	free with Th8_RsaKeyFree).
 *
 * Why / How:
 *	Public keys are stored in a well-known registry so that
 *	any interpreter can verify signatures without embedding
 *	every possible signing key.  The URL is constructed by
 *	concatenating the fixed prefix with the 16-character hex
 *	token.  The raw bytes are fetched via Th8_GetData and
 *	parsed as an SNK/CAPI blob via Th8_RsaKeyLoad.
 *
 * Results:
 *	TH8_OK on success (with *ppKey set), TH8_ERROR on network
 *	or parse failure (with interp result set).
 *
 * Side effects:
 *	Allocates an Th8_RsaKey (caller-owned on success).
 *	Performs a network fetch.
 *
 *----------------------------------------------------------------------
 */

static int
th8PolicyFetchKey(
    Th8_Interp *interp,
    const char *zToken, /* 16-char hex token (NUL-terminated). */
    Th8_RsaKey **ppKey)
{
    /*
     * Build URL: "https://w.sb/r/pk_" + token.
     * Fixed prefix is 19 chars, token is 16, plus NUL = 36.
     */

    char zUrl[36];
    char *zData = NULL;
    size_t nData = 0;
    int rc;

    Th8_Memcpy(interp, zUrl, "https://w.sb/r/pk_", 18);
    Th8_Memcpy(interp, &zUrl[18], zToken, 16);
    zUrl[34] = '\0';

    rc = Th8_GetData(interp, zUrl, 34, &zData, &nData, 0);
    if (rc != TH8_OK) {
	Th8_ErrorMessage(
	    interp, "signed-only: cannot fetch public key from \"", zUrl, 34);
	return TH8_ERROR;
    }

    rc = Th8_RsaKeyLoad(interp, (const unsigned char *)zData, nData, ppKey);
    Th8_Free(interp, zData);

    if (rc != TH8_OK) {
	Th8_SetResultStatic(
	    interp, "signed-only: invalid public key blob from registry",
	    TH8_NOLEN);
    }
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * th8PolicyVerifyToken --
 *
 *	Verify that the actual token computed from the fetched key
 *	matches the expected token from the signature header.
 *
 * Why / How:
 *	After fetching a key from the registry by its claimed token,
 *	this function recomputes the token from the actual key data
 *	via Th8_RsaKeyTokenHex and compares it to the expected
 *	value.  This guards against a registry that returns the
 *	wrong key for a given token, ensuring that the key used
 *	for verification is the one the signer intended.
 *
 * Results:
 *	TH8_OK if the tokens match, TH8_ERROR on mismatch or if
 *	token computation fails.
 *
 * Side effects:
 *	Sets the interp result on mismatch.
 *
 *----------------------------------------------------------------------
 */

static int
th8PolicyVerifyToken(
    Th8_Interp *interp,
    const Th8_RsaKey *pKey,
    const char *zExpected) /* 16-char hex (NUL-terminated). */
{
    char zActual[17];
    int rc;

    rc = Th8_RsaKeyTokenHex(interp, pKey, zActual);
    if (rc != TH8_OK) return rc;

    if (Th8_Memcmp(interp, zActual, zExpected, 16) != 0) {
	Th8_SetResultStatic(
	    interp, "signed-only: public key token mismatch", TH8_NOLEN);
	return TH8_ERROR;
    }
    return TH8_OK;
}


#  if defined(TH8_ENABLE_VARIABLES)
/*
 *----------------------------------------------------------------------
 *
 * th8PolicyPopulateSecurity --
 *
 *	Set the ::th8_security array to reflect a successfully
 *	verified script.  Populates algorithmName (e.g. "RSA-16384"),
 *	policy ("signedOnly"), publicKeyToken, and dataName.
 *
 * Why / How:
 *	After signature verification succeeds, scripts need access
 *	to metadata about the signing key and policy for introspection
 *	(e.g. to display the algorithm or token to the user).  The
 *	algorithm name is constructed by formatting "RSA-<bits>" from
 *	the key's bit length.  This function is only called on the
 *	success path of th8PolicyVerifyData.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets ::th8_security(algorithmName), ::th8_security(policy),
 *	::th8_security(publicKeyToken), and ::th8_security(dataName).
 *
 *----------------------------------------------------------------------
 */

static void
th8PolicyPopulateSecurity(
    Th8_Interp *interp,
    const Th8_RsaKey *pKey,
    const char *zToken, /* 16-char hex (NUL-terminated). */
    const char *zName, /* Verified script name. */
    size_t nName) /* Name length. */
{
    /*
     * algorithmName: "RSA-<bits>"
     */

    {
	char zAlg[16];
	int nBits = Th8_RsaKeyBitLen(pKey);
	int i = 0;

	Th8_Memcpy(interp, zAlg, "RSA-", 4);
	i = 4;
	if (nBits >= 10000) zAlg[i++] = '0' + (nBits / 10000) % 10;
	if (nBits >= 1000) zAlg[i++] = '0' + (nBits / 1000) % 10;
	if (nBits >= 100) zAlg[i++] = '0' + (nBits / 100) % 10;
	if (nBits >= 10) zAlg[i++] = '0' + (nBits / 10) % 10;
	zAlg[i++] = '0' + nBits % 10;
	zAlg[i] = '\0';

	Th8_SetVar(
	    interp, "::th8_security(algorithmName)", TH8_NOLEN, zAlg,
	    (size_t)i);
    }

    Th8_SetVar(
        interp, "::th8_security(policy)", TH8_NOLEN, "signedOnly", TH8_NOLEN);
    Th8_SetVar(
        interp, "::th8_security(publicKeyToken)", TH8_NOLEN, zToken, 16);
    Th8_SetVar(
        interp, "::th8_security(dataName)", TH8_NOLEN, zName ? zName : "none",
        zName ? nName : TH8_NOLEN);
}
#  endif


/*
 *----------------------------------------------------------------------
 *
 * th8PolicyVerifyData --
 *
 *	Signature verification logic for the READ PRE phase of the
 *	unified policy callback.  Called after raw file data is read
 *	but before EOL translation.
 *
 *	Logic:
 *	1.  Clears the verification flag.
 *	2.  Scans and validates script annotations.
 *	2b. If signed-only is not enabled, returns TH8_OK.
 *	3.  Validates zName is a relative path or HTTP(S) URI.
 *	4.  Reads the companion .b64sig file (zName + ".b64sig").
 *	5.  Parses the signature and extracts the public key token.
 *	6.  Fetches (or uses cached) public key from the registry.
 *	7.  Verifies the RSA signature over the script text.
 *	8.  Populates ::th8_security on success; returns TH8_ERROR
 *	    with a diagnostic on any failure.
 *
 * Why / How:
 *	This is the core security enforcement function.  It is
 *	called from both the READ PRE phase (for file-sourced
 *	scripts) and the EVAL PRE phase (for direct Th8_Eval calls
 *	with an origin name).  The verification flag is cleared
 *	first so that a failed verification cannot leave a stale
 *	"verified" state from a prior script.  The policy callback
 *	is temporarily disabled while reading the .b64sig file to
 *	prevent infinite recursion.  Keys are cached in the policy
 *	context's hash table so that repeated evaluations with the
 *	same signing key avoid redundant network fetches.  On
 *	verification failure, SHA-512 hashes of both the data and
 *	the extracted signature digest are emitted as trace
 *	diagnostics.
 *
 * Results:
 *	TH8_OK if the script is verified (or signed-only is not
 *	enabled), TH8_ERROR on any verification failure.
 *
 * Side effects:
 *	Reads the .b64sig companion file.  May fetch a public key
 *	from the network.  Populates ::th8_security on success.
 *	Sets the verification token on success.  Allocates and
 *	frees temporary buffers for signature path, data, and token.
 *
 *----------------------------------------------------------------------
 */

int
th8PolicyVerifyData(
    Th8_Interp *interp,
    const char *zName, /* File name. */
    size_t nName, /* File name length. */
    const char *zData, /* Raw file data. */
    size_t nData, /* Raw data length. */
    void *pCtx) /* Th8_PolicyCtx*. */
{
    Th8_PolicyCtx *p = (Th8_PolicyCtx *)pCtx;
    Th8_ScriptAnnotations ann;
    int annOk = 0;
    char *zSigPath = NULL;
    char *zSigData = NULL;
    size_t nSigData = 0;
    unsigned char *pSig = NULL;
    size_t nSig = 0;
    char *zToken = NULL;
    Th8_RsaKey *pKey = NULL;
    int ownKey = 0;
    int rc;

    /*
     * Step 1: Reset security array and clear the verification
     * flag.  The flag is re-set on success at step 8.  If
     * verification fails, the flag remains cleared -- this
     * prevents a caught [source] error from leaving a stale
     * "verified" state that could allow subsequent nested
     * evals to bypass verification.
     */

    th8PolicyClearVerified(p);

    /*
     * NOTE: th8PolicyResetSecurity is NOT called here.
     * It is deferred to step 8 (success path) so that a
     * failed verification (e.g., from a caught [source]
     * of an unsigned URL) does not clobber the th8_security
     * state from a prior successful verification.
     */

    /*
     * Step 2a: Scan for script annotations regardless of whether
     * signed-only is enabled.  The parsed values are stored in
     * ::th8_security and made available to the policy callback.
     *
     * If any annotation is malformed AND signed-only is enabled,
     * the script is rejected.  If signed-only is disabled,
     * malformed annotations are silently ignored.
     *
     * Time-based enforcement (notBefore/notAfter) is only applied
     * when signed-only is enabled.
     */
    {
	int annTimeOk = TH8_OK;

	annOk =
	    (th8PolicyScanAnnotations(interp, zData, nData, &ann) == TH8_OK);
	if (annOk) {
	    /*
	     * Validate time constraints now (before the heavy
	     * signature verification) so that expired scripts
	     * are rejected early.  The annotation values are NOT
	     * written to ::th8_security yet -- that is deferred
	     * to step 8 so that Th8_ResetSecurityArray does not
	     * overwrite them.
	     */
	    annTimeOk = th8PolicyCheckAnnotations(interp, &ann);
	}
	if (Th8_IsSignedOnlyEnabled(interp)) {
	    if (!annOk) {
		/* Malformed annotation with policy enabled. */
		return TH8_ERROR;
	    }
	    if (annTimeOk != TH8_OK) {
		/* Time out of range with policy enabled. */
		return TH8_ERROR;
	    }
	}
    }

    /*
     * Step 2b: If signed-only is not enabled, allow the script.
     * Annotations have already been extracted above.
     */

    if (!Th8_IsSignedOnlyEnabled(interp)) {
	return TH8_OK;
    }

    /*
     * Step 3: Validate the script origin.  Every script must
     * have a non-NULL origin name when signed-only is active.
     */

    if (!zName || nName == 0) {
	Th8_SetResultStatic(
	    interp, "signed-only: script has no origin name", TH8_NOLEN);
	return TH8_ERROR;
    }

    if (!th8PolicyIsRelativePath(zName, nName) &&
        !th8PolicyIsHttpUri(interp, zName, nName)) {
	Th8_SetResultStatic(
	    interp,
	    "signed-only: origin must be a relative path "
	    "or HTTP(S) URI",
	    TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Step 4: Build the signature file path and read it.
     * Signature file = zName + ".b64sig"
     *
     * IMPORTANT: Temporarily disable the policy callback
     * while reading the .b64sig file to avoid infinite recursion
     * (the .b64sig file itself is not signed).
     */

    zSigPath = (char *)TH8_ALLOC_ADD(interp, nName, 8);
    if (!zSigPath) return TH8_ERROR;
    Th8_Memcpy(interp, zSigPath, zName, nName);
    Th8_Memcpy(interp, &zSigPath[nName], ".b64sig", 8);

    {
	Th8_PolicyProc xSaved;
	void *pSaved;

	Th8_GetPolicyCallback(interp, &xSaved, &pSaved);
	Th8_SetPolicyCallback(interp, NULL, NULL);

	rc =
	    Th8_GetData(interp, zSigPath, nName + 7, &zSigData, &nSigData, 0);

	Th8_SetPolicyCallback(interp, xSaved, pSaved);
    }

    if (rc != TH8_OK) {
	Th8_ErrorMessage(
	    interp, "signed-only: cannot read signature file \"", zSigPath,
	    nName + 7);
	goto error;
    }

    /*
     * Step 5: Parse the Harpy signature file.
     */

    rc = Th8_HarpySigLoad(interp, zSigData, nSigData, &pSig, &nSig, &zToken);
    if (rc != TH8_OK) {
	Th8_ErrorMessage(
	    interp, "signed-only: invalid signature file for \"", zName,
	    nName);
	goto error;
    }
    if (!zToken || Th8_Strlen(interp, zToken) != 16) {
	Th8_ErrorMessage(
	    interp,
	    "signed-only: missing public key token in "
	    "signature header for \"",
	    zName, nName);
	goto error;
    }

    /*
     * Step 6: Obtain the public key from the hash table cache.
     * If not found, fetch from the registry and cache it.
     */

    {
	Th8_HashEntry *pEntry = NULL;

	if (p->paKeys) {
	    pEntry = Th8_HashFind(interp, p->paKeys, zToken, 16, 0);
	}

	if (pEntry) {
	    pKey = (Th8_RsaKey *)pEntry->pData;
	} else {
	    rc = th8PolicyFetchKey(interp, zToken, &pKey);
	    if (rc != TH8_OK) goto error;
	    ownKey = 1;

	    rc = th8PolicyVerifyToken(interp, pKey, zToken);
	    if (rc != TH8_OK) goto error;

	    /*
	     * Insert into the cache hash table.
	     */

	    if (!p->paKeys) {
		p->paKeys = Th8_HashNew(interp);
		if (!p->paKeys) goto error;
	    }
	    pEntry = Th8_HashFind(interp, p->paKeys, zToken, 16, 1);
	    if (pEntry) {
		pEntry->pData = pKey;
		ownKey = 0;
	    }
	}
    }

    /*
     * Step 7: Verify the RSA signature over the script text.
     */

    rc = Th8_RsaVerify(
        interp, pKey, (const unsigned char *)zData, nData, pSig, nSig);
    if (rc != TH8_OK) {
	char dataHash[129];
	char sigHash[129];

	Th8_Sha512Hex(interp, (const unsigned char *)zData, nData, dataHash);

	if (Th8_RsaExtractHash(interp, pKey, pSig, nSig, sigHash) != TH8_OK) {
	    Th8_Memset(interp, sigHash, '?', 128);
	    sigHash[128] = '\0';
	}

	Th8_EmitTrace(
	    interp,
	    "signed-only: verification failed for \"%.*s\"\n"
	    "      calculated data SHA-512: %s\n"
	    "  extracted signature SHA-512: %s\n",
	    (int)nName, zName, dataHash, sigHash);

	Th8_ErrorMessage(
	    interp,
	    "signed-only: script signature verification "
	    "failed for \"",
	    zName, nName);
	goto error;
    }

    /*
     * Step 8: Populate ::th8_security with the verified details.
     */

#  if defined(TH8_ENABLE_VARIABLES)
    if (th8PolicyResetSecurity(interp) != TH8_OK) {
	/*
	 * The security array could not be cleanly reset (allocation
	 * failure).  Reject the eval rather than expose a possibly
	 * partial security array to the just-verified script
	 * (TH8K-006/-020).
	 */
	Th8_SetResultStatic(
	    interp, "signed-only: unable to reset security state", TH8_NOLEN);
	goto error;
    }
    th8PolicyPopulateSecurity(interp, pKey, zToken, zName, nName);

    /*
     * Re-apply annotation values that were parsed at step 2a.
     * Th8_ResetSecurityArray cleared them; now that the security
     * array is populated with the verified key details, set
     * the annotation fields from the saved parse result.  Bug 41
     * fix (2026-06-07): `flags` is re-applied here alongside
     * notBefore/notAfter to maintain the seven-element invariant.
     */
    if (annOk) {
	if (ann.bHasNotBefore) {
	    Th8_SetVar(
	        interp, "::th8_security(notBefore)", TH8_NOLEN,
	        ann.zNotBefore, TH8_NOLEN);
	}
	if (ann.bHasNotAfter) {
	    Th8_SetVar(
	        interp, "::th8_security(notAfter)", TH8_NOLEN, ann.zNotAfter,
	        TH8_NOLEN);
	}
	if (ann.bHasFlags) {
	    Th8_SetVar(
	        interp, "::th8_security(flags)", TH8_NOLEN, ann.zFlags,
	        ann.nFlags);
	}
    }
#  endif

    if (th8PolicySetVerified(p) != TH8_OK) {
	Th8_SetResultStatic(
	    interp, "unable to generate random policy token", TH8_NOLEN);
	goto error;
    }

    Th8_Free(interp, zSigPath);
    Th8_Free(interp, zSigData);
    Th8_Free(interp, pSig);
    Th8_Free(interp, zToken);
    return TH8_OK;

error:
    if (ownKey && pKey) Th8_RsaKeyFree(interp, pKey);
    Th8_Free(interp, zSigPath);
    Th8_Free(interp, zSigData);
    Th8_Free(interp, pSig);
    Th8_Free(interp, zToken);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * th8PolicyEvalPre --
 *
 *	EVAL PRE phase logic for the unified policy callback.
 *	Enforces the origin-name gate: when signed-only is active,
 *	every script must have a non-NULL origin.  The actual
 *	signature verification is done by th8PolicyVerifyData at
 *	file-read time; this function handles the policy-level
 *	access control.
 *
 * Why / How:
 *	The EVAL PRE phase runs before every script evaluation and
 *	implements a multi-gate security model:
 *
 *	  Gate 1 - If signed-only is disabled (e.g. REPL mode),
 *	    allow unconditionally.
 *	  Gate 2 - If the READ PRE phase already verified this
 *	    script (th8PolicyIsVerified), allow.
 *	  Gate 3 - Nested evaluations (depth > 1) from if, catch,
 *	    eval, proc bodies, etc. are always allowed because
 *	    they are part of an already-verified top-level script.
 *	  Gate 4 - Top-level eval without prior verification:
 *	    (A) No origin name -> reject unless TH8_EVAL_TRUSTED.
 *	    (B) Has origin name -> perform inline verification
 *	        via th8PolicyVerifyData.
 *
 *	Annotations are scanned only at the top level to avoid
 *	overwriting values set by the enclosing script.
 *
 * Results:
 *	TH8_OK if the script is allowed to proceed, TH8_ERROR
 *	if the script is rejected.
 *
 * Side effects:
 *	May invoke th8PolicyVerifyData (with all its side effects).
 *	May scan and check annotations at the top level.
 *
 *----------------------------------------------------------------------
 */

static int
th8PolicyEvalPre(
    Th8_Interp *interp,
    const char *zName,
    size_t nName,
    const char *zScript,
    size_t nScript,
    int flags,
    void *pCtx)
{
    Th8_PolicyCtx *p = (Th8_PolicyCtx *)pCtx;

    /*
     * Scan annotations only at the top level (depth <= 1).
     * Sub-evaluations (if, catch, proc bodies, etc.) do not
     * carry annotations and should not overwrite the values
     * set by the top-level script.
     */
    if (Th8_GetEvalDepth(interp) <= 1) {
	Th8_ScriptAnnotations ann;
	int annOk;
	int annTimeOk = TH8_OK;

	annOk =
	    (th8PolicyScanAnnotations(interp, zScript, nScript, &ann) ==
	     TH8_OK);
	if (annOk) {
	    annTimeOk = th8PolicyCheckAnnotations(interp, &ann);
	}
	if (Th8_IsSignedOnlyEnabled(interp)) {
	    if (!annOk) return TH8_ERROR;
	    if (annTimeOk != TH8_OK) return TH8_ERROR;
	}
    }

    /*
     * Gate 1: signed-only disabled (e.g., temporarily via
     * Th8_SaveSignedOnly for REPL or -eval) -- allow all.
     */

    if (!Th8_IsSignedOnlyEnabled(interp)) {
	return TH8_OK;
    }

    /*
     * Gate 2: the READ PRE phase already verified the raw file
     * data for this evaluation context.  The flag persists
     * through sub-evaluations (namespace eval, catch, if, etc.).
     */

    if (th8PolicyIsVerified(p)) {
	return TH8_OK;
    }

    /*
     * Gate 3: non-top-level eval (depth > 1).  These are
     * internal sub-evaluations required by the language
     * (if/catch/eval/uplevel/namespace eval/proc bodies/etc.)
     * and must always succeed.
     */

    if (Th8_GetEvalDepth(interp) > 1) {
	return TH8_OK;
    }

    /*
     * Top-level eval (depth <= 1) with no prior verification.
     *
     * Case A: no origin name -- reject unless the embedder
     * asserted trust via Th8_EvalTrusted (TH8_EVAL_TRUSTED).
     */

    if (!zName || nName == 0) {
	if (flags & TH8_EVAL_TRUSTED) {
	    return TH8_OK;
	}
	Th8_SetResultStatic(
	    interp, "signed-only: script has no origin name", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Case B: has origin name but no READ PRE verification
     * (e.g., host called Th8_Eval directly, not Th8_EvalFile).
     *
     * Verify the exact script bytes passed to Th8_Eval.  The
     * signature must match this data as-is, including whatever
     * line endings it contains.  If the caller passes EOL-
     * translated text, the signature must have been computed
     * over that same form.
     */

    return th8PolicyVerifyData(interp, zName, nName, zScript, nScript, pCtx);
}


/*
 *----------------------------------------------------------------------
 *
 * th8PolicyCallback --
 *
 *	Unified policy callback for the signed-only policy.
 *	Dispatches on the phase bitmask to the appropriate
 *	handler:
 *
 *	  TH8_PHASE_PRE | TH8_PHASE_READ  -- signature verification
 *	  TH8_PHASE_PRE | TH8_PHASE_EVAL  -- origin-name gate
 *	  TH8_PHASE_POST | TH8_PHASE_EVAL -- clear verification flag
 *	  TH8_PHASE_POST | TH8_PHASE_READ -- no-op
 *
 * Why / How:
 *	A single callback function handles all four phase
 *	combinations to keep the policy logic cohesive.  The POST
 *	EVAL phase clears the verification flag only at the top
 *	level (depth <= 1) so that nested evaluations within a
 *	verified script remain trusted.  Unknown phase combinations
 *	are silently accepted (return TH8_OK) for forward
 *	compatibility.
 *
 * Results:
 *	TH8_OK if the operation is allowed, TH8_ERROR if rejected
 *	(with a diagnostic in the interp result).
 *
 * Side effects:
 *	Delegates to th8PolicyVerifyData or th8PolicyEvalPre for
 *	the PRE phases.  Clears the verification flag on POST EVAL.
 *
 *----------------------------------------------------------------------
 */

static int
th8PolicyCallback(
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
    Th8_PolicyCtx *p = (Th8_PolicyCtx *)pCtx;

    if (phase == (TH8_PHASE_POST | TH8_PHASE_EVAL)) {
	/*
	 * Eval scope closing.  Only clear the verification flag
	 * at the top level (depth <= 1).  Nested evals (if, catch,
	 * proc, uplevel, source inside source, etc.) must NOT clear
	 * the flag -- the outer eval's signed verification is still
	 * valid for the entire call tree.
	 */
	if (Th8_GetEvalDepth(interp) <= 1) {
	    th8PolicyClearVerified(p);
	}
	return TH8_OK;
    }

    if (phase == (TH8_PHASE_POST | TH8_PHASE_READ)) {
	/* Data read complete -- no action needed. */
	(void)rc;
	return TH8_OK;
    }

    if (phase == (TH8_PHASE_PRE | TH8_PHASE_READ)) {
	return th8PolicyVerifyData(interp, zName, nName, zData, nData, pCtx);
    }

    if (phase == (TH8_PHASE_PRE | TH8_PHASE_EVAL)) {
	return th8PolicyEvalPre(
	    interp, zName, nName, zData, nData, flags, pCtx);
    }

    (void)rc;
    (void)flags;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_InstallSignedPolicy --
 *
 *	Install the unified policy callback on the interpreter.
 *	This replaces any previously installed callback.  The
 *	callback remains active until removed with
 *	Th8_RemoveSignedPolicy or until the interpreter is deleted.
 *
 * Why / How:
 *	Allocates a Th8_PolicyCtx, zeroes it, and registers
 *	th8PolicyCallback as the interpreter's policy callback.
 *	The context holds the key cache and verification token
 *	state.  The caller receives the opaque context pointer
 *	for later use with Th8_PolicyPreloadKey and
 *	Th8_RemoveSignedPolicy.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR on allocation failure.
 *
 * Side effects:
 *	Allocates a Th8_PolicyCtx.  Replaces any previously
 *	installed policy callback on the interpreter.
 *
 *----------------------------------------------------------------------
 */

int
Th8_InstallSignedPolicy(
    Th8_Interp *interp,
    void **ppCtx) /* OUT: opaque context (or NULL). */
{
    Th8_PolicyCtx *p;

    if (!interp) return TH8_ERROR;

    p = (Th8_PolicyCtx *)TH8_ALLOC(interp, sizeof(Th8_PolicyCtx));
    if (!p) return TH8_ERROR;

    Th8_Memset(interp, p, 0, sizeof(Th8_PolicyCtx));
    p->interp = interp;

    Th8_SetPolicyCallback(interp, th8PolicyCallback, p);
    if (ppCtx) *ppCtx = p;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8PolicyFreeKeyEntry --
 *
 *	Hash-iteration callback that frees a single cached RSA key
 *	entry.  Called for each entry in the key cache during
 *	policy teardown.
 *
 * Why / How:
 *	The policy context's key cache (paKeys) stores RSA key
 *	pointers as hash-entry data.  During Th8_RemoveSignedPolicy,
 *	Th8_HashIterate walks every entry and calls this function
 *	to free the key before the hash table itself is destroyed.
 *	The entry's pData pointer is NULLed after freeing to guard
 *	against double-free.
 *
 * Results:
 *	TH8_OK (always; required by the Th8_HashIterate callback
 *	signature).
 *
 * Side effects:
 *	Frees the Th8_RsaKey stored in pEntry->pData.
 *
 *----------------------------------------------------------------------
 */

static int
th8PolicyFreeKeyEntry(Th8_HashEntry *pEntry, void *pCtx)
{
    Th8_Interp *interp = (Th8_Interp *)pCtx;

    if (pEntry->pData) {
	Th8_RsaKeyFree(interp, (Th8_RsaKey *)pEntry->pData);
	pEntry->pData = NULL;
    }
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Th8_RemoveSignedPolicy --
 *
 *	Remove the unified policy callback and free the context
 *	(including any cached keys).  Safe to call even if no policy
 *	is installed (a no-op in that case, provided pCtx is NULL).
 *
 * Why / How:
 *	Unregisters the policy callback from the interpreter, then
 *	walks the key cache to free every cached RSA key via
 *	th8PolicyFreeKeyEntry, destroys the hash table, and frees
 *	the Th8_PolicyCtx struct itself.  Passing NULL for pCtx is
 *	explicitly supported for convenience during error-recovery
 *	paths where the policy may not have been fully installed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees all cached RSA keys and the policy context.  Clears
 *	the interpreter's policy callback to NULL.
 *
 *----------------------------------------------------------------------
 */

void
Th8_RemoveSignedPolicy(
    Th8_Interp *interp,
    void *pCtx) /* The Th8_PolicyCtx* from install. */
{
    Th8_PolicyCtx *p = (Th8_PolicyCtx *)pCtx;

    if (!interp) return;
    Th8_SetPolicyCallback(interp, NULL, NULL);

    if (p) {
	if (p->paKeys) {
	    Th8_HashIterate(interp, p->paKeys, th8PolicyFreeKeyEntry, interp);
	    Th8_HashDelete(interp, p->paKeys);
	}
	Th8_Free(interp, p);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_PolicyPreloadKey --
 *
 *	Pre-load an RSA key into the policy context cache.  This
 *	avoids a network fetch for the key when the token matches.
 *	The policy takes ownership of pKey; the caller must NOT
 *	free it.  Any previously cached key for the same token is
 *	freed and replaced.
 *
 * Why / How:
 *	Embedded keys (keyRoot, key0, keyTest) are preloaded at
 *	policy-install time so that signature verification can
 *	proceed without a network round-trip.  The key's 16-char
 *	hex token is computed via Th8_RsaKeyTokenHex and used as
 *	the hash-table key.  If an entry already exists for that
 *	token, its previous key is freed before being replaced.
 *	The hash table is created lazily on first preload.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR on invalid arguments, token
 *	computation failure, or allocation failure.
 *
 * Side effects:
 *	Transfers ownership of pKey to the cache.  May create the
 *	key hash table.  May free a previously cached key for the
 *	same token.
 *
 *----------------------------------------------------------------------
 */

int
Th8_PolicyPreloadKey(Th8_Interp *interp, void *pCtx, Th8_RsaKey *pKey)
{
    Th8_PolicyCtx *p = (Th8_PolicyCtx *)pCtx;
    char zToken[17];
    Th8_HashEntry *pEntry;
    int rc;

    if (!interp) return TH8_ERROR;
    if (!p || !pKey) {
	Th8_SetResultStatic(
	    interp, "PolicyPreloadKey: invalid arguments", TH8_NOLEN);
	return TH8_ERROR;
    }

    rc = Th8_RsaKeyTokenHex(interp, pKey, zToken);
    if (rc != TH8_OK) return rc;

    if (!p->paKeys) {
	p->paKeys = Th8_HashNew(interp);
	if (!p->paKeys) return TH8_ERROR;
    }

    pEntry = Th8_HashFind(interp, p->paKeys, zToken, 16, 1);
    if (!pEntry) return TH8_ERROR;

    if (pEntry->pData) {
	Th8_RsaKeyFree(interp, (Th8_RsaKey *)pEntry->pData);
    }
    pEntry->pData = pKey;

    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_PolicyFindKey --
 *
 *	Look up an RSA key by its 16-character hex public key token
 *	in the policy's key cache.  Returns a borrowed pointer (owned
 *	by the cache).
 *
 * Why / How:
 *	Provides read-only access to cached keys for callers that
 *	need to inspect a key without performing a full verification
 *	(e.g. the [th8 keyinfo] command).  The token length is
 *	validated as exactly 16 characters.  If nToken is TH8_NOLEN,
 *	the length is computed via Th8_Strlen.  Returns NULL on any
 *	invalid input or cache miss rather than setting an error.
 *
 * Results:
 *	Borrowed pointer to the Th8_RsaKey if found, NULL otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

const Th8_RsaKey *
Th8_PolicyFindKey(
    Th8_Interp *interp,
    void *pCtx,
    const char *zToken,
    size_t nToken)
{
    Th8_PolicyCtx *p = (Th8_PolicyCtx *)pCtx;
    Th8_HashEntry *pEntry;

    if (!interp) return NULL;

    if (!p || !p->paKeys || !zToken) return NULL;
    if (nToken == TH8_NOLEN) nToken = Th8_Strlen(interp, zToken);
    if (nToken != 16) return NULL;

    pEntry = Th8_HashFind(interp, p->paKeys, zToken, 16, 0);
    if (!pEntry) return NULL;
    return (const Th8_RsaKey *)pEntry->pData;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_PolicyGetKeyTokens --
 *
 *	Return a Tcl list of all public key tokens currently loaded
 *	in the policy's key cache.  Each element is a 16-character
 *	lowercase hex string.  The result is set in the interpreter.
 *
 * Why / How:
 *	Enables introspection of the loaded key set, used by the
 *	[th8 keys] command.  Iterates all hash-table buckets and
 *	appends each entry's key string to a Tcl list via
 *	Th8_ListAppend.  Returns an empty string (not an error) when
 *	no keys are cached.
 *
 * Results:
 *	TH8_OK on success (even if the list is empty).
 *	TH8_ERROR if the policy context is NULL.
 *
 * Side effects:
 *	Sets the interpreter result to the token list.  Allocates
 *	and frees a temporary list buffer.
 *
 *----------------------------------------------------------------------
 */

int
Th8_PolicyGetKeyTokens(Th8_Interp *interp, void *pCtx)
{
    Th8_PolicyCtx *p = (Th8_PolicyCtx *)pCtx;
    char *zList = NULL;
    size_t nList = 0;

    if (!interp) return TH8_ERROR;
    if (!p) {
	Th8_SetResultStatic(
	    interp, "PolicyGetKeyTokens: no policy context", TH8_NOLEN);
	return TH8_ERROR;
    }

    if (p->paKeys) {
	int i;

	for (i = 0; i < TH8_HASH_SIZE; i++) {
	    Th8_HashEntry *pEntry;

	    for (pEntry = p->paKeys->aBucket[i]; pEntry;
	         pEntry = pEntry->pNext) {
		if (pEntry->zKey && pEntry->nKey == 16) {
		    Th8_ListAppend(interp, &zList, &nList, pEntry->zKey, 16);
		}
	    }
	}
    }

    if (zList) {
	Th8_SetResult(interp, zList, nList);
	Th8_Free(interp, zList);
    } else {
	Th8_ClearResult(interp);
    }

    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_EnableSignedPolicy --
 *
 *	One-call convenience function to install or remove the
 *	signed-only policy, preload all embedded keys, and enable
 *	the gate.  Equivalent to the sequence:
 *
 *	    Th8_InstallSignedPolicy(interp, &ppCtx)
 *	    for each embedded key:
 *	        Th8_RsaKeyLoad  -->  Th8_PolicyPreloadKey
 *	    Th8_EnableSignedOnly(interp, 1)
 *
 *      -OR- The inverse of this for removal.
 *
 *	On failure at any step, the partially installed policy is
 *	removed and TH8_ERROR is returned.  On success, *ppCtx is
 *	set to the opaque context for later use with
 *	Th8_RemoveSignedPolicy.
 *
 * Why / How:
 *	Provides a single entry point for embedders to fully enable
 *	or disable signed-only mode.  On enable, installs the policy
 *	callback, preloads keyRoot, key0, and (if TH8_ENABLE_TEST_KEY)
 *	keyTest, then activates the signed-only gate.  On disable,
 *	removes the policy and deactivates the gate.  The atomic
 *	setup-or-rollback pattern ensures that a partial failure
 *	(e.g. key parse error) does not leave the interpreter in an
 *	inconsistent state.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR on failure at any step.
 *
 * Side effects:
 *	Installs or removes the policy callback.  Preloads embedded
 *	keys into the cache.  Enables or disables the signed-only
 *	gate on the interpreter.
 *
 *----------------------------------------------------------------------
 */

int
Th8_EnableSignedPolicy(
    Th8_Interp *interp, /* Interpreter. */
    void **ppCtx, /* Context pointer. */
    int bEnable) /* 1=enable, 0=disable. */
{
    void *pCtx = NULL;
    const unsigned char *zKeyData;
    size_t nKeyData;
    Th8_RsaKey *pKey = NULL;
    int rc;

    if (!interp) return TH8_ERROR;

    if (!bEnable) {
	if (ppCtx) pCtx = *ppCtx;
	Th8_RemoveSignedPolicy(interp, pCtx);
	rc = Th8_EnableSignedOnly(interp, 0);
	return rc;
    }

    rc = Th8_InstallSignedPolicy(interp, &pCtx);
    if (rc != TH8_OK) return rc;

    /* Preload keyRoot. */
    zKeyData = Th8_GetEmbeddedKeyRoot(&nKeyData);
    rc = Th8_RsaKeyLoad(interp, zKeyData, nKeyData, &pKey);
    if (rc != TH8_OK) goto fail;
    rc = Th8_PolicyPreloadKey(interp, pCtx, pKey);
    if (rc != TH8_OK) {
	Th8_RsaKeyFree(interp, pKey);
	goto fail;
    }
    /* pKey ownership transferred to cache. */

    /* Preload key0. */
    zKeyData = Th8_GetEmbeddedKey0(&nKeyData);
    rc = Th8_RsaKeyLoad(interp, zKeyData, nKeyData, &pKey);
    if (rc != TH8_OK) goto fail;
    rc = Th8_PolicyPreloadKey(interp, pCtx, pKey);
    if (rc != TH8_OK) {
	Th8_RsaKeyFree(interp, pKey);
	goto fail;
    }

#  if defined(TH8_ENABLE_TEST_KEY)
    zKeyData = Th8_GetEmbeddedKeyTest(&nKeyData);
    rc = Th8_RsaKeyLoad(interp, zKeyData, nKeyData, &pKey);
    if (rc != TH8_OK) goto fail;
    rc = Th8_PolicyPreloadKey(interp, pCtx, pKey);
    if (rc != TH8_OK) {
	Th8_RsaKeyFree(interp, pKey);
	goto fail;
    }
#  endif

    /* Enable the signed-only gate. */
    rc = Th8_EnableSignedOnly(interp, 1);
    if (rc != TH8_OK) goto fail;

    if (ppCtx) *ppCtx = pCtx;
    return TH8_OK;

fail:
    Th8_RemoveSignedPolicy(interp, pCtx);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_EvalFileAndRsaKeyLoad --
 *
 *	Load an RSA public key by evaluating a signed script file.
 *	Combines Th8_EvalFileAsData (to retrieve the base64-encoded
 *	key blob via a child interpreter) with Th8_RsaKeyLoad (to
 *	parse the decoded data as an SNK/CAPI key).  Optionally
 *	preloads the key into the signed-only policy cache via
 *	Th8_PolicyPreloadKey.
 *
 *	pCtx is the opaque policy context (from Th8_EnableSignedPolicy
 *	or Th8_InstallSignedPolicy).  Required when bPreload is
 *	non-zero; may be NULL otherwise.
 *
 *	If any step fails, TH8_ERROR is returned and no key is loaded.
 *	The interpreter result contains a diagnostic message.
 *
 * Why / How:
 *	Enables loading additional signing keys from signed script
 *	files at runtime.  The script is evaluated in a child
 *	interpreter via Th8_EvalFileAsData, which returns the
 *	base64-decoded result.  The decoded bytes are then parsed
 *	as an SNK/CAPI public key blob.  If bPreload is set, the
 *	key is inserted into the policy cache so that subsequent
 *	scripts signed with that key can be verified without a
 *	network fetch.  The key is freed on failure or when
 *	bPreload is false.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR on failure.
 *
 * Side effects:
 *	May create and destroy a child interpreter (via
 *	Th8_EvalFileAsData).  May preload a key into the policy
 *	cache (ownership transferred).
 *
 *----------------------------------------------------------------------
 */

int
Th8_EvalFileAndRsaKeyLoad(
    Th8_Interp *interp, /* Parent interpreter. */
    const char *zName, /* Script file name. */
    size_t nName, /* Byte length, or TH8_NOLEN. */
    void *pCtx, /* Policy context (for preload). */
    int bPreload) /* Non-zero to preload into cache. */
{
    const unsigned char *zData = NULL;
    size_t nData = 0;
    Th8_RsaKey *pKey = NULL;
    int rc;

    if (!interp) return TH8_ERROR;

    /*
     * Step 1: Evaluate the script in a child interpreter and
     * base64-decode the result.
     */

    rc = Th8_EvalFileAsData(interp, zName, nName, &zData, &nData, NULL);
    if (rc != TH8_OK) return rc;

    /*
     * Step 2: Parse the decoded data as an RSA key.
     */

    rc = Th8_RsaKeyLoad(interp, zData, nData, &pKey);
    Th8_Free(interp, (void *)zData);

    if (rc != TH8_OK) {
	Th8_SetResultStatic(
	    interp,
	    "Th8_EvalFileAndRsaKeyLoad: "
	    "invalid RSA key data",
	    TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Step 3: Optionally preload the key into the policy cache.
     */

    if (bPreload) {
	if (!pCtx) {
	    Th8_RsaKeyFree(interp, pKey);
	    Th8_SetResultStatic(
	        interp,
	        "Th8_EvalFileAndRsaKeyLoad: "
	        "preload requested but no policy context",
	        TH8_NOLEN);
	    return TH8_ERROR;
	}
	rc = Th8_PolicyPreloadKey(interp, pCtx, pKey);
	/* pKey ownership transferred to cache on success. */
	if (rc != TH8_OK) {
	    Th8_RsaKeyFree(interp, pKey);
	}
    } else {
	Th8_RsaKeyFree(interp, pKey);
    }

    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * Key zero (built-in public key) management.
 *
 *	The embedded key0 is loaded once and cached in a file-scope
 *	static so that every interpreter can access it without
 *	redundant parsing.  The key is never freed.
 *
 *----------------------------------------------------------------------
 */

static Th8_RsaKey *th8_pKeyZero = NULL;


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetPublicKeyZero --
 *
 *	Return the parsed RSA key for the embedded key0.  Parses on
 *	first call; subsequent calls return the cached result.
 *	The returned pointer is owned by the library and must NOT
 *	be freed by the caller.
 *
 * Why / How:
 *	The embedded key0 is the primary public signing key compiled
 *	into the library.  This function provides lazy initialization:
 *	Th8_GetEmbeddedKey0 retrieves the raw blob, Th8_RsaKeyLoad
 *	parses it, and the result is stored in the file-scope static
 *	th8_pKeyZero for all subsequent calls.  The key is never
 *	freed (it persists for the lifetime of the process).
 *
 * Results:
 *	Pointer to the parsed Th8_RsaKey, or NULL on failure.
 *
 * Side effects:
 *	On first call, parses the embedded key blob and stores the
 *	result in the file-scope static th8_pKeyZero.
 *
 *----------------------------------------------------------------------
 */

Th8_RsaKey *
Th8_GetPublicKeyZero(Th8_Interp *interp)
{
    if (!interp) return NULL;
    if (!th8_pKeyZero) {
	size_t nData;
	const unsigned char *zData;

	zData = Th8_GetEmbeddedKey0(&nData);
#  if defined(TH8_ENABLE_FAULT_INJECTION)
	if (th8FaultActiveCfg) {
	    if (th8FaultActiveCfg->nFailEmbeddedKey0 == 1) {
		zData = NULL;
	    } else if (th8FaultActiveCfg->nFailEmbeddedKey0 == 2) {
		nData = 0;
	    }
	}
#  endif
	if (!zData || nData == 0) return NULL;

	if (Th8_RsaKeyLoad(interp, zData, nData, &th8_pKeyZero) != TH8_OK) {
	    return NULL;
	}
    }
    return th8_pKeyZero;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetPublicKeyZeroToken --
 *
 *	Compute and return the 16-character hex public key token for
 *	the embedded key0.  Writes to zOut (at least 17 bytes).
 *
 * Why / How:
 *	Convenience wrapper that obtains the key via
 *	Th8_GetPublicKeyZero and computes its token via
 *	Th8_RsaKeyTokenHex.  Used by commands that need to
 *	display or compare the key0 token without managing
 *	the key object directly.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR if the key cannot be loaded
 *	or the token cannot be computed.
 *
 * Side effects:
 *	Writes 16 hex characters plus NUL to zOut.  May trigger
 *	lazy initialization of the key0 static.
 *
 *----------------------------------------------------------------------
 */

int
Th8_GetPublicKeyZeroToken(Th8_Interp *interp, char zOut[17])
{
    Th8_RsaKey *pKey;

    if (!interp) return TH8_ERROR;
    pKey = Th8_GetPublicKeyZero(interp);

    if (!pKey) {
	Th8_SetResultStatic(interp, "cannot load embedded key0", TH8_NOLEN);
	return TH8_ERROR;
    }
    return Th8_RsaKeyTokenHex(interp, pKey, zOut);
}


/*
 *----------------------------------------------------------------------
 *
 * Key root (built-in root public key) management.
 *
 *----------------------------------------------------------------------
 */

static Th8_RsaKey *th8_pKeyRoot = NULL;


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetPublicKeyRoot --
 *
 *	Return the parsed RSA key for the embedded keyRoot.  Parses
 *	on first call; subsequent calls return the cached result.
 *	The returned pointer is owned by the library and must NOT
 *	be freed by the caller.
 *
 * Why / How:
 *	The embedded keyRoot is the 16384-bit RSA root signing key.
 *	This function mirrors Th8_GetPublicKeyZero but operates on
 *	the root key blob via Th8_GetEmbeddedKeyRoot.  Lazy
 *	initialization stores the parsed key in th8_pKeyRoot for
 *	process-lifetime reuse.
 *
 * Results:
 *	Pointer to the parsed Th8_RsaKey, or NULL on failure.
 *
 * Side effects:
 *	On first call, parses the embedded key blob and stores the
 *	result in the file-scope static th8_pKeyRoot.
 *
 *----------------------------------------------------------------------
 */

Th8_RsaKey *
Th8_GetPublicKeyRoot(Th8_Interp *interp)
{
    if (!interp) return NULL;
    if (!th8_pKeyRoot) {
	size_t nData;
	const unsigned char *zData;

	zData = Th8_GetEmbeddedKeyRoot(&nData);
#  if defined(TH8_ENABLE_FAULT_INJECTION)
	if (th8FaultActiveCfg) {
	    if (th8FaultActiveCfg->nFailEmbeddedKeyRoot == 1) {
		zData = NULL;
	    } else if (th8FaultActiveCfg->nFailEmbeddedKeyRoot == 2) {
		nData = 0;
	    }
	}
#  endif
	if (!zData || nData == 0) return NULL;

	if (Th8_RsaKeyLoad(interp, zData, nData, &th8_pKeyRoot) != TH8_OK) {
	    return NULL;
	}
    }
    return th8_pKeyRoot;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetPublicKeyRootToken --
 *
 *	Compute and return the 16-character hex public key token for
 *	the embedded keyRoot.  Writes to zOut (at least 17 bytes).
 *
 * Why / How:
 *	Convenience wrapper that obtains the key via
 *	Th8_GetPublicKeyRoot and computes its token via
 *	Th8_RsaKeyTokenHex.  Used by commands that need to
 *	display or compare the keyRoot token without managing
 *	the key object directly.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR if the key cannot be loaded
 *	or the token cannot be computed.
 *
 * Side effects:
 *	Writes 16 hex characters plus NUL to zOut.  May trigger
 *	lazy initialization of the keyRoot static.
 *
 *----------------------------------------------------------------------
 */

int
Th8_GetPublicKeyRootToken(Th8_Interp *interp, char zOut[17])
{
    Th8_RsaKey *pKey;

    if (!interp) return TH8_ERROR;
    pKey = Th8_GetPublicKeyRoot(interp);

    if (!pKey) {
	Th8_SetResultStatic(
	    interp, "cannot load embedded keyRoot", TH8_NOLEN);
	return TH8_ERROR;
    }
    return Th8_RsaKeyTokenHex(interp, pKey, zOut);
}


/*
 *----------------------------------------------------------------------
 *
 * Key test (embedded test signing key) management.
 *
 *	Only available when TH8_ENABLE_TEST_KEY is defined.
 *	This key is for development and testing only.
 *
 *----------------------------------------------------------------------
 */

#  if defined(TH8_ENABLE_TEST_KEY)

static Th8_RsaKey *th8_pKeyTest = NULL;


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetPublicKeyTest --
 *
 *	Return the parsed RSA key for the embedded test key.
 *	Parses on first call; subsequent calls return the cached
 *	result.  The returned pointer is owned by the library and
 *	must NOT be freed by the caller.
 *
 * Why / How:
 *	The embedded test key is only available when
 *	TH8_ENABLE_TEST_KEY is defined.  This function mirrors
 *	Th8_GetPublicKeyZero but operates on the test key blob
 *	via Th8_GetEmbeddedKeyTest.  Lazy initialization stores
 *	the parsed key in th8_pKeyTest for process-lifetime reuse.
 *
 * Results:
 *	Pointer to the parsed Th8_RsaKey, or NULL on failure.
 *
 * Side effects:
 *	On first call, parses the embedded key blob and stores the
 *	result in the file-scope static th8_pKeyTest.
 *
 *----------------------------------------------------------------------
 */

Th8_RsaKey *
Th8_GetPublicKeyTest(Th8_Interp *interp)
{
    if (!interp) return NULL;
    if (!th8_pKeyTest) {
	size_t nData;
	const unsigned char *zData;

	zData = Th8_GetEmbeddedKeyTest(&nData);
#    if defined(TH8_ENABLE_FAULT_INJECTION)
	if (th8FaultActiveCfg) {
	    if (th8FaultActiveCfg->nFailEmbeddedKeyTest == 1) {
		zData = NULL;
	    } else if (th8FaultActiveCfg->nFailEmbeddedKeyTest == 2) {
		nData = 0;
	    }
	}
#    endif
	if (!zData || nData == 0) return NULL;

	if (Th8_RsaKeyLoad(interp, zData, nData, &th8_pKeyTest) != TH8_OK) {
	    return NULL;
	}
    }
    return th8_pKeyTest;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetPublicKeyTestToken --
 *
 *	Compute and return the 16-character hex public key token for
 *	the embedded test key.  Writes to zOut (at least 17 bytes).
 *
 * Why / How:
 *	Convenience wrapper that obtains the key via
 *	Th8_GetPublicKeyTest and computes its token via
 *	Th8_RsaKeyTokenHex.  Only available when
 *	TH8_ENABLE_TEST_KEY is defined.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR if the key cannot be loaded
 *	or the token cannot be computed.
 *
 * Side effects:
 *	Writes 16 hex characters plus NUL to zOut.  May trigger
 *	lazy initialization of the test key static.
 *
 *----------------------------------------------------------------------
 */

int
Th8_GetPublicKeyTestToken(Th8_Interp *interp, char zOut[17])
{
    Th8_RsaKey *pKey;

    if (!interp) return TH8_ERROR;
    pKey = Th8_GetPublicKeyTest(interp);

    if (!pKey) {
	Th8_SetResultStatic(
	    interp, "cannot load embedded test key", TH8_NOLEN);
	return TH8_ERROR;
    }
    return Th8_RsaKeyTokenHex(interp, pKey, zOut);
}

#  endif /* TH8_ENABLE_TEST_KEY */


/*
 *----------------------------------------------------------------------
 *
 * th8PolicyResetCachedKeys --
 *
 *	Reset the file-scope lazy-init caches for the embedded
 *	keyZero, keyRoot, and (when TH8_ENABLE_TEST_KEY is defined)
 *	keyTest RSA key objects to NULL.  Intended exclusively for
 *	MC/DC-coverage fault tests that need to re-trigger the
 *	`if (!zData || nData == 0)` guards at L2091/L2190/L2295 on
 *	a subsequent call to Th8_GetPublicKeyZero/Root/Test after a
 *	fault flag (nFailEmbeddedKey0/Root/Test) has been set on the
 *	active Th8_FaultConfig.
 *
 * Why / How:
 *	The Th8_GetPublicKey* wrappers cache parsed keys for process
 *	lifetime in static pointers, gated by `if (!th8_pKey*)`.
 *	Once the first successful call has populated the cache, the
 *	lazy-init body (and the embedded-key getter consult below
 *	it) never runs again.  Coverage tests that want to observe
 *	the (T,-) C1 or (F,T) C2 MC/DC vectors at the guard must
 *	clear the cache pointers before installing the fault and
 *	re-calling the getter.  Exposed via the internal-stubs table
 *	(Th8_GetInternalStubs) for testlib consumption; not part of
 *	the public API.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	NULLs th8_pKeyZero, th8_pKeyRoot, and th8_pKeyTest (when
 *	TH8_ENABLE_TEST_KEY is defined).  The previously cached
 *	Th8_RsaKey objects are intentionally NOT freed (they
 *	persist for process lifetime per the existing contract, and
 *	losing the pointer is the price of using this hook --
 *	confined to test runs).
 *
 *----------------------------------------------------------------------
 */

TH8_INTERNAL void
th8PolicyResetCachedKeys(void)
{
    th8_pKeyZero = NULL;
    th8_pKeyRoot = NULL;
#  if defined(TH8_ENABLE_TEST_KEY)
    th8_pKeyTest = NULL;
#  endif
}


#endif /* TH8_ENABLE_CRYPTOGRAPHY */
