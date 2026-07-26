/*
 * th8_harpy.c -- Harpy plugin commands for TH8.
 *
 * Implements:
 *   - [flags] command for Harpy-compatible attribute flags
 *   - [clock ntp] subcommand (NTP time verification)
 *   - [clock https] subcommand (HTTPS time verification)
 *
 * The clock subcommand callbacks are non-static (internal
 * linkage) so the timekeeping plugin can reference them in
 * the clock ensemble's subcommand table.
 *
 * Registered via the plugin system as the "harpy" plugin.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "th8.h"
#include "th8_int.h"
#include "th8_util.h"
#include "th8_plugin.h"


#if defined(TH8_ENABLE_CRYPTOGRAPHY)
/*
 *----------------------------------------------------------------------
 *
 * th8HarpyClockNtpCommand --
 *
 *	Query NTP servers for authenticated wall-clock time.
 *	Used as a subcommand of [clock] via the timekeeping plugin.
 *
 *	clock ntp ?-server HOST? ?-timeout MS? ?-attempts N?
 *
 *----------------------------------------------------------------------
 */

int
th8HarpyClockNtpCommand(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    const char *azServers[8];
    int nServers = 0;
    int timeoutMs = 0;
    int attempts = 0; /* 0 -> NTP_DEFAULT_ATTEMPTS; 1 disables retries */
    int i;
    th8_int64_t epochSec;
    int rc;

    (void)ctx;

    for (i = 2; i < argc; i++) {
	if (argl[i] == 7 && Th8_Memcmp(interp, argv[i], "-server", 7) == 0) {
	    if (i + 1 >= argc) {
		return Th8_WrongNumArgs(
		    interp, "clock ntp ?-server host? ?-timeout ms? "
		            "?-attempts n?");
	    }
	    i++;
	    if (nServers < 8) {
		azServers[nServers++] = argv[i];
	    }
	} else if (
	    argl[i] == 8 && Th8_Memcmp(interp, argv[i], "-timeout", 8) == 0) {
	    if (i + 1 >= argc) {
		return Th8_WrongNumArgs(
		    interp, "clock ntp ?-server host? ?-timeout ms? "
		            "?-attempts n?");
	    }
	    i++;
	    {
		th8_int64_t v;

		if (Th8_ToWideInt(interp, argv[i], argl[i], &v) != TH8_OK) {
		    return TH8_ERROR;
		}
		timeoutMs = (int)v;
	    }
	} else if (
	    argl[i] == 9 &&
	    Th8_Memcmp(interp, argv[i], "-attempts", 9) == 0) {
	    if (i + 1 >= argc) {
		return Th8_WrongNumArgs(
		    interp, "clock ntp ?-server host? ?-timeout ms? "
		            "?-attempts n?");
	    }
	    i++;
	    {
		th8_int64_t v;

		if (Th8_ToWideInt(interp, argv[i], argl[i], &v) != TH8_OK) {
		    return TH8_ERROR;
		}
		attempts = (int)v;
	    }
	} else {
	    Th8_ErrorMessage(
	        interp, "clock ntp: unknown option \"", argv[i], argl[i]);
	    return TH8_ERROR;
	}
    }

    rc = th8NtpQuery(
        interp, nServers > 0 ? azServers : NULL, nServers, timeoutMs, 0,
        attempts, &epochSec);
    if (rc != TH8_OK) return rc;

    return Th8_SetResultWideInt(interp, epochSec);
}


/*
 *----------------------------------------------------------------------
 *
 * th8HarpyClockHttpsCommand --
 *
 *	Query an HTTPS time server for authenticated time.
 *	Used as a subcommand of [clock] via the timekeeping plugin.
 *
 *	clock https ?URL?
 *
 *----------------------------------------------------------------------
 */

int
th8HarpyClockHttpsCommand(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    const char *zUrl = NULL;
    size_t nUrl = 0;
    th8_int64_t epochSec;
    int rc;

    (void)ctx;

    if (argc > 3) {
	return Th8_WrongNumArgs(interp, "clock https ?url?");
    }
    if (argc == 3) {
	zUrl = argv[2];
	nUrl = argl[2];
    }

    rc = th8HttpsTimeQuery(interp, zUrl, nUrl, &epochSec);
    if (rc != TH8_OK) return rc;

    return Th8_SetResultWideInt(interp, epochSec);
}
#endif /* TH8_ENABLE_CRYPTOGRAPHY */


/*
 *----------------------------------------------------------------------
 *
 * Shared option state for flags sub-commands.
 *
 *----------------------------------------------------------------------
 */

typedef struct {
    int bComplex;
    int bSpace;
    int bSort;
    int bAll;
    int bStrict;
    int bLegacy;
    int bCompact;
    th8_int64_t key;
    int iArg;  /* Index of first non-option argument. */
} Th8_FlagsOpts;


/*
 *----------------------------------------------------------------------
 *
 * th8FlagsParseOpts --
 *
 *	Parse the shared -option flags from argv[2..] and populate
 *	the Th8_FlagsOpts struct.  Sets pOpts->iArg to the index of
 *	the first operand after options.
 *
 *----------------------------------------------------------------------
 */

static int
th8FlagsParseOpts(
    Th8_Interp *interp,
    int argc,
    const char **argv,
    size_t *argl,
    Th8_FlagsOpts *pOpts)
{
    Th8_Memset(interp, pOpts, 0, sizeof(*pOpts));
    pOpts->iArg = 2;

    while (pOpts->iArg < argc && argv[pOpts->iArg][0] == '-') {
	int i = pOpts->iArg;

	if (argl[i] == 8 && Th8_Memcmp(interp, argv[i], "-complex", 8) == 0) {
	    pOpts->bComplex = 1;
	} else if (
	    argl[i] == 6 && Th8_Memcmp(interp, argv[i], "-space", 6) == 0) {
	    pOpts->bSpace = 1;
	} else if (
	    argl[i] == 5 && Th8_Memcmp(interp, argv[i], "-sort", 5) == 0) {
	    pOpts->bSort = 1;
	} else if (
	    argl[i] == 4 && Th8_Memcmp(interp, argv[i], "-all", 4) == 0) {
	    pOpts->bAll = 1;
	} else if (
	    argl[i] == 7 && Th8_Memcmp(interp, argv[i], "-strict", 7) == 0) {
	    pOpts->bStrict = 1;
	} else if (
	    argl[i] == 7 && Th8_Memcmp(interp, argv[i], "-legacy", 7) == 0) {
	    pOpts->bLegacy = 1;
	} else if (
	    argl[i] == 8 && Th8_Memcmp(interp, argv[i], "-compact", 8) == 0) {
	    pOpts->bCompact = 1;
	} else if (
	    argl[i] == 4 && Th8_Memcmp(interp, argv[i], "-key", 4) == 0) {
	    pOpts->iArg++;
	    if (pOpts->iArg >= argc) {
		return Th8_WrongNumArgs(interp, "flags ... -key value ...");
	    }
	    {
		const char *zK = argv[pOpts->iArg];
		size_t nK = argl[pOpts->iArg];

		if (nK > 2 && zK[0] == '0' &&
		    (zK[1] == 'x' || zK[1] == 'X')) {
		    /*
		     * Bug 46 (UBSan): accumulate via th8_uint64_t to
		     * avoid signed-shift-overflow UB when the parsed
		     * hex value has the high bit set; cast to the
		     * signed key type at the end.  The shift `v << 4`
		     * on a th8_int64_t with v > INT64_MAX/16 is UB
		     * per C99 6.5.7p4.
		     */
		    th8_uint64_t v = 0;
		    size_t j;

		    for (j = 2; j < nK; j++) {
			char c = zK[j];
			int d;

			if (c >= '0' && c <= '9')
			    d = c - '0';
			else if (c >= 'a' && c <= 'f')
			    d = c - 'a' + 10;
			else if (c >= 'A' && c <= 'F')
			    d = c - 'A' + 10;
			else {
			    Th8_SetResultStatic(
			        interp, "flags: invalid hex key", TH8_NOLEN);
			    return TH8_ERROR;
			}
			v = (v << 4) | (th8_uint64_t)d;
		    }
		    pOpts->key = (th8_int64_t)v;
		} else {
		    if (Th8_ToWideInt(interp, zK, nK, &pOpts->key) !=
		        TH8_OK) {
			return TH8_ERROR;
		    }
		}
	    }
	} else if (
	    argl[i] == 2 && Th8_Memcmp(interp, argv[i], "--", 2) == 0) {
	    pOpts->iArg++;
	    break;
	} else {
	    Th8_ErrorMessage(
	        interp, "flags: unknown option \"", argv[i], argl[i]);
	    return TH8_ERROR;
	}
	pOpts->iArg++;
    }

    if (pOpts->key != 0 && !pOpts->bComplex) {
	Th8_SetResultStatic(
	    interp,
	    "flags: must use complex mode to use "
	    "non-default key",
	    TH8_NOLEN);
	return TH8_ERROR;
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * flags_have_command --
 *
 *	flags have ?options? flagString haveFlags
 *
 *----------------------------------------------------------------------
 */

static int
flags_have_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    Th8_FlagsOpts opts;
    Th8_AfMap map;
    int rc;

    (void)ctx;

    rc = th8FlagsParseOpts(interp, argc, argv, argl, &opts);
    if (rc != TH8_OK) return rc;

    if (argc - opts.iArg != 2) {
	return Th8_WrongNumArgs(
	    interp, "flags have ?options? flags haveFlags");
    }

    rc = Th8_AttrFlagsParse(
        interp, argv[opts.iArg], argl[opts.iArg], opts.bComplex, opts.bSpace,
        &map);
    if (rc != TH8_OK) return rc;

    Th8_SetResultInt(
        interp, Th8_AttrFlagsHave(
                    &map, opts.key, argv[opts.iArg + 1], argl[opts.iArg + 1],
                    opts.bAll, opts.bStrict));
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * flags_change_command --
 *
 *	flags change ?options? flagString changeSpec
 *
 *----------------------------------------------------------------------
 */

static int
flags_change_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    Th8_FlagsOpts opts;
    Th8_AfMap map;
    char *zOut = NULL;
    size_t nOut = 0;
    int rc;

    (void)ctx;

    rc = th8FlagsParseOpts(interp, argc, argv, argl, &opts);
    if (rc != TH8_OK) return rc;

    if (argc - opts.iArg != 2) {
	return Th8_WrongNumArgs(
	    interp, "flags change ?options? flags changeFlags");
    }

    rc = Th8_AttrFlagsParse(
        interp, argv[opts.iArg], argl[opts.iArg], opts.bComplex, opts.bSpace,
        &map);
    if (rc != TH8_OK) return rc;

    rc = Th8_AttrFlagsChange(
        interp, &map, argv[opts.iArg + 1], argl[opts.iArg + 1], opts.key);
    if (rc != TH8_OK) return rc;

    rc = Th8_AttrFlagsFormat(
        interp, &map, opts.bLegacy, opts.bCompact, opts.bSpace, opts.bSort,
        &zOut, &nOut);
    if (rc != TH8_OK) return rc;

    Th8_SetResult(interp, zOut, nOut);
    Th8_Free(interp, zOut);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * flags_show_command --
 *
 *	flags show ?options? flagString
 *
 *----------------------------------------------------------------------
 */

static int
flags_show_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    Th8_FlagsOpts opts;
    Th8_AfMap map;
    char *zOut = 0;
    size_t nOut = 0;
    int rc;
    int i;
    int iGlobal = -1;

    (void)ctx;

    rc = th8FlagsParseOpts(interp, argc, argv, argl, &opts);
    if (rc != TH8_OK) return rc;

    if (argc - opts.iArg != 1) {
	return Th8_WrongNumArgs(interp, "flags show ?options? flags");
    }

    rc = Th8_AttrFlagsParse(
        interp, argv[opts.iArg], argl[opts.iArg], opts.bComplex, opts.bSpace,
        &map);
    if (rc != TH8_OK) return rc;

    for (i = 0; i < map.n; i++) {
	if (map.a[i].key == 0) {
	    iGlobal = i;
	    break;
	}
    }

    for (i = -1; i < map.n; i++) {
	int idx;
	Th8_AfMap one;
	char *zVal = 0;
	size_t nVal = 0;
	char zKey[32];
	int nKey;

	if (i == -1) {
	    if (iGlobal < 0) continue;
	    idx = iGlobal;
	} else {
	    if (i == iGlobal) continue;
	    idx = i;
	}

	Th8_Memset(interp, &one, 0, sizeof(one));
	one.a[0] = map.a[idx];
	one.a[0].key = 0;   /* Emit bare flags; key is output separately. */
	one.n = 1;

	rc = Th8_AttrFlagsFormat(interp, &one, 0, 1, 0, 1, &zVal, &nVal);
	if (rc != TH8_OK) {
	    Th8_Free(interp, zOut);
	    return rc;
	}

	{
	    th8_int64_t k = map.a[idx].key;
	    char *p = zKey + sizeof(zKey);
	    int neg = (k < 0);

	    if (neg) k = -k;
	    *--p = '\0';
	    do {
		*--p = (char)('0' + (int)(k % 10));
		k /= 10;
	    } while (k);
	    if (neg) *--p = '-';
	    nKey = (int)(zKey + sizeof(zKey) - 1 - p);
	    Th8_Memcpy(interp, zKey, p, (size_t)nKey + 1);
	}

	Th8_ListAppend(interp, &zOut, &nOut, zKey, (size_t)nKey);
	Th8_ListAppend(interp, &zOut, &nOut, zVal ? zVal : "", nVal);
	Th8_Free(interp, zVal);
    }

    Th8_SetResult(interp, zOut ? zOut : "", nOut);
    Th8_Free(interp, zOut);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * flags_command --
 *
 *	Ensemble dispatcher for the [flags] command.
 *
 *----------------------------------------------------------------------
 */

static const Th8_SubCommand th8FlagsSub[] =
    {{0, "change", flags_change_command},
     {0, "have", flags_have_command},
     {0, "show", flags_show_command},
     {0, 0, 0}};

const Th8_SubCommand *th8_flags_aSub;

/*
 *----------------------------------------------------------------------
 *
 * flags_command --
 *
 *	Implements the script-visible `[flags ...]` ensemble
 *	used by the Harpy plugin to query and mutate the
 *	signed-script attribute flags (`change`, `have`,
 *	`show`).  Thin dispatcher into `th8FlagsSub` via
 *	`Th8_CallSubCommand`.
 *
 *	The published `th8_flags_aSub` pointer lets other
 *	parts of the plugin (e.g. introspection helpers
 *	enumerating attribute names) traverse the same table
 *	without re-defining it.
 *
 * Parameters:
 *	interp -- live interpreter.
 *	ctx    -- command context (forwarded).
 *	argc   -- argument count.
 *	argv   -- argument vector.
 *	argl   -- argument byte-length vector.
 *
 * Returns:
 *	The selected subcommand's return code, or `TH8_ERROR`
 *	with a diagnostic if the subcommand name is unknown.
 *
 * Side effects:
 *	Whatever the dispatched subcommand performs (the
 *	`change` subcommand mutates the per-interp
 *	attribute-flag state).
 *
 *----------------------------------------------------------------------
 */
static int
flags_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    return Th8_CallSubCommand(interp, ctx, argc, argv, argl, th8FlagsSub);
}


#if defined(TH8_ENABLE_CRYPTOGRAPHY)
/*
 *----------------------------------------------------------------------
 *
 * th8HarpyGetPolicyCtx --
 *
 *	Retrieve the signed-only policy context from the interp's
 *	policy callback.  Returns NULL if no policy is installed.
 *
 *----------------------------------------------------------------------
 */

static void *
th8HarpyGetPolicyCtx(Th8_Interp *interp)
{
    void *pCtx = 0;

    Th8_GetPolicyCallback(interp, 0, &pCtx);
    return pCtx;
}


/*
 *----------------------------------------------------------------------
 *
 * harpy_command --
 *
 *	The [harpy] ensemble command.
 *
 *	harpy sign   PUBLICKEYTOKEN SCRIPTTEXT
 *	harpy verify PUBLICKEYTOKEN SCRIPTTEXT SIGTEXT
 *
 *	"sign" signs the script text with the private key identified
 *	by the token, produces a .b64sig-format result, and verifies
 *	the signature before returning it.
 *
 *	"verify" parses the signature text, looks up the key by token,
 *	and verifies the signature.  Returns "ok" on success, raises
 *	an error on failure.
 *
 *----------------------------------------------------------------------
 */

static int
harpy_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    void *pPolicyCtx;

    (void)ctx;

    if (argc < 4) {
	return Th8_WrongNumArgs(
	    interp, "harpy sign|verify publicKeyToken scriptText"
	            " ?signatureText?");
    }

    pPolicyCtx = th8HarpyGetPolicyCtx(interp);
    if (!pPolicyCtx) {
	Th8_SetResultStatic(
	    interp, "harpy: signed-only policy not installed", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * harpy sign PUBLICKEYTOKEN SCRIPTTEXT
     */

    if (argl[1] == 4 && Th8_Memcmp(interp, argv[1], "sign", 5) == 0) {
	const Th8_RsaKey *pKey;
	unsigned char *pSig = 0;
	size_t nSig = 0;
	int rc;

	if (argc != 4) {
	    return Th8_WrongNumArgs(
	        interp, "harpy sign publicKeyToken scriptText");
	}

	/* Look up the key by token. */
	pKey = Th8_PolicyFindKey(interp, pPolicyCtx, argv[2], argl[2]);
	if (!pKey) {
	    Th8_ErrorMessage(
	        interp, "harpy sign: key not found for token \"", argv[2],
	        argl[2]);
	    return TH8_ERROR;
	}
	if (!Th8_RsaKeyHasPrivate(pKey)) {
	    Th8_SetResultStatic(
	        interp,
	        "harpy sign: key does not contain a "
	        "private key",
	        TH8_NOLEN);
	    return TH8_ERROR;
	}

	/* Sign the script text. */
	rc = Th8_RsaSign(
	    interp, pKey, (const unsigned char *)argv[3], argl[3], &pSig,
	    &nSig);
	if (rc != TH8_OK) return rc;

	/*
	 * Format the .b64sig output:
	 *   - Comment header with token on line 3
	 *   - Base64-encoded signature body
	 */
	{
	    char *zOut = 0;
	    size_t nOut = 0;
	    const char *zB64;
	    size_t nB64;

	    /* Encode signature to base64 (sets interp result). */
	    rc = th8Base64Encode(interp, pSig, nSig);
	    Th8_Free(interp, pSig);
	    if (rc != TH8_OK) return rc;

	    zB64 = Th8_GetResult(interp, &nB64);

	    /* Build .b64sig header. */
	    TH8_STR_APPEND(
	        interp, &zOut, &nOut,
	        "##################################"
	        "#############################################\n"
	        "#\n"
	        "# signature.b64sig -- ",
	        TH8_NOLEN);
	    TH8_STR_APPEND(interp, &zOut, &nOut, argv[2], argl[2]);
	    TH8_STR_APPEND(
	        interp, &zOut, &nOut,
	        "\n#\n"
	        "# TH8 Script Signature File (Harpy)\n"
	        "#\n"
	        "##################################"
	        "#############################################\n"
	        "\n",
	        TH8_NOLEN);

	    /* Append base64 body with leading spaces. */
	    {
		size_t k = 0;

		while (k < nB64) {
		    size_t lineEnd = k;

		    /* th8Base64Encode emits CRLF (never LF-only)
		     * at every line wrap, so the inner scan always
		     * meets '\r' first; the != '\n' sub-check is a
		     * defensive belt-and-braces test, never F at
		     * the byte the loop actually exits on. */
		    while (lineEnd < nB64 && ALWAYS(zB64[lineEnd] != '\n') &&
		           zB64[lineEnd] != '\r') {
			lineEnd++;
		    }
		    if (lineEnd > k) {
			TH8_STR_APPEND(interp, &zOut, &nOut, "  ", 2);
			TH8_STR_APPEND(
			    interp, &zOut, &nOut, zB64 + k, lineEnd - k);
			TH8_STR_APPEND(interp, &zOut, &nOut, "\n", 1);
		    }
		    k = lineEnd;
		    while (k < nB64 && (zB64[k] == '\n' || zB64[k] == '\r')) {
			k++;
		    }
		}
	    }
	    Th8_SetResult(interp, zOut, nOut);
	    Th8_Free(interp, zOut);
	    return TH8_OK;

oom:
	    Th8_Free(interp, zOut);
	    return TH8_ERROR;
	}
    }

    /*
     * harpy verify PUBLICKEYTOKEN SCRIPTTEXT SIGTEXT
     */

    if (argl[1] == 6 && Th8_Memcmp(interp, argv[1], "verify", 7) == 0) {
	const Th8_RsaKey *pKey;
	unsigned char *pSig = 0;
	size_t nSig = 0;
	char *zSigToken = 0;
	int rc;

	if (argc != 5) {
	    return Th8_WrongNumArgs(
	        interp, "harpy verify publicKeyToken scriptText"
	                " signatureText");
	}

	/* Parse the .b64sig text. */
	rc = Th8_HarpySigLoad(
	    interp, argv[4], argl[4], &pSig, &nSig, &zSigToken);
	if (rc != TH8_OK) return rc;

	/*
	 * Verify the token in the signature matches the
	 * requested token (if the signature contains one).
	 */
	if (zSigToken &&
	    Th8_Memcmp(interp, zSigToken, argv[2], argl[2]) != 0) {
	    Th8_Free(interp, pSig);
	    Th8_Free(interp, zSigToken);
	    Th8_SetResultStatic(
	        interp,
	        "harpy verify: token mismatch between "
	        "argument and signature",
	        TH8_NOLEN);
	    return TH8_ERROR;
	}
	Th8_Free(interp, zSigToken);

	/* Look up the key by token. */
	pKey = Th8_PolicyFindKey(interp, pPolicyCtx, argv[2], argl[2]);
	if (!pKey) {
	    Th8_Free(interp, pSig);
	    Th8_ErrorMessage(
	        interp,
	        "harpy verify: key not found for "
	        "token \"",
	        argv[2], argl[2]);
	    return TH8_ERROR;
	}

	/* Verify the signature. */
	rc = Th8_RsaVerify(
	    interp, pKey, (const unsigned char *)argv[3], argl[3], pSig,
	    nSig);
	Th8_Free(interp, pSig);

	if (rc != TH8_OK) return rc;

	Th8_SetResultStatic(interp, "ok", 2);
	return TH8_OK;
    }

    Th8_ErrorMessage(
        interp, "harpy: unknown subcommand \"", argv[1], argl[1]);
    return TH8_ERROR;
}
#endif /* TH8_ENABLE_CRYPTOGRAPHY */


/*
 *----------------------------------------------------------------------
 *
 * Command table and plugin registration.
 *
 *----------------------------------------------------------------------
 */

static Th8_CommandEntry th8HarpyCommands[] = {
    {1, 0, "flags", flags_command},
#if defined(TH8_ENABLE_CRYPTOGRAPHY)
    {1, 0, "harpy", harpy_command},
#endif
};

/*
 *----------------------------------------------------------------------
 *
 * th8HarpyGetCommands --
 *
 *	Return the command table for the Harpy plugin.  Called
 *	by the plugin registration system during interpreter
 *	initialization.
 *
 * Why / How:
 *	Follows the standard GetCommands protocol: when pCommand
 *	is NULL, returns the count in *pnCommand so the caller
 *	can allocate.  When non-NULL, copies the command entries.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if pnCommand is NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
th8HarpyGetCommands(Th8_CommandEntry *pCommand, int *pnCommand)
{
    int n = (int)(sizeof(th8HarpyCommands) / sizeof(th8HarpyCommands[0]));

    if (!pnCommand) return TH8_ERROR;
    if (!pCommand) {
	*pnCommand = n;
	return TH8_OK;
    }
    if (*pnCommand < n) return TH8_ERROR;
    *pnCommand = n;
    {
	int i;

	for (i = 0; i < n; i++) {
	    pCommand[i] = th8HarpyCommands[i];
	}
    }
    th8_flags_aSub = th8FlagsSub;
    return TH8_OK;
}
