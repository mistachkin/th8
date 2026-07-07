/*
 * th8_crypto_cmds.c -- Cryptography plugin commands for TH8.
 *
 * Implements the [hash] and [secure] commands.
 * Registered via the plugin system as the "cryptography" plugin.
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
#include "th8_plugin.h"

#if defined(TH8_ENABLE_CRYPTOGRAPHY)


/*
 *----------------------------------------------------------------------
 *
 * hash_command --
 *
 *	Compute a cryptographic hash of a string.
 *
 *	hash normal ALGORITHM STRING
 *
 *	Currently only SHA512 is supported (case-insensitive).
 *	Returns the 128-character lowercase hex digest.
 *
 *	Only available when TH8_ENABLE_CRYPTOGRAPHY is defined.
 *
 * Why / How:
 *	Implements the [hash normal] command.  Validates the
 *	subcommand ("normal") and algorithm name (SHA512,
 *	case-insensitive), then delegates to Th8_Sha512Hex for
 *	the actual digest computation.  Only SHA512 is currently
 *	supported; other algorithms are rejected with an error.
 *
 * Results:
 *	TH8_OK with the 128-character hex digest as the result,
 *	or TH8_ERROR on bad subcommand, unsupported algorithm,
 *	or wrong argument count.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
hash_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char zOut[129];

    (void)ctx;

    if (argc != 4) {
	return Th8_WrongNumArgs(interp, "hash normal algorithm string");
    }

    /*
     * Subcommand must be "normal".
     */

    if (argl[1] != 6 || Th8_Memcmp(interp, argv[1], "normal", 6) != 0) {
	Th8_SetResultStatic(interp, "hash: must be normal", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Only SHA512 is supported (case-insensitive).  Each
     * character compared separately rather than as one
     * compound to keep the decision instrumentable under
     * clang's `-fcoverage-mcdc` (the compact MC/DC truth-
     * table representation cannot encode the 8-condition
     * compound this would otherwise be).  See FINDINGS.md
     * Finding 005.
     */

    {
	const char *z = argv[2];
	size_t n = argl[2];
	int ok = 0;

	if (n == 6) {
	    if (z[0] == 'S' || z[0] == 's') {
		if (z[1] == 'H' || z[1] == 'h') {
		    if (z[2] == 'A' || z[2] == 'a') {
			if (z[3] == '5') {
			    if (z[4] == '1') {
				if (z[5] == '2') {
				    ok = 1;
				}
			    }
			}
		    }
		}
	    }
	}
	if (!ok) {
	    Th8_SetResultStatic(interp, "unsupported algorithm", TH8_NOLEN);
	    return TH8_ERROR;
	}
    }

    Th8_Sha512Hex(interp, (const unsigned char *)argv[3], argl[3], zOut);
    Th8_SetResult(interp, zOut, 128);
    return TH8_OK;
}


#  if defined(TH8_ENABLE_VARIABLES)
/*
 *----------------------------------------------------------------------
 *
 * secure_command --
 *
 *	Manage encrypted-at-rest (secure) variables.
 *
 *	secure create VARNAME ?VALUE?
 *	secure exists VARNAME
 *	secure delete VARNAME
 *	secure save VARNAME
 *	secure load VARNAME
 *
 *	Only available when TH8_ENABLE_CRYPTOGRAPHY is defined.
 *	save/load require the secure persistence gate AND a master
 *	key to be configured by the embedder.
 *
 * Why / How:
 *	Implements the [secure] command for encrypted-at-rest
 *	variables.  Dispatches on the subcommand keyword (create,
 *	exists, delete, save, load) with simple length-and-memcmp
 *	checks.
 *	Each subcommand delegates to the corresponding
 *	th8SecureVar* internal function.  Requires both
 *	TH8_ENABLE_CRYPTOGRAPHY and TH8_ENABLE_VARIABLES.
 *
 * Results:
 *	TH8_OK on success.  For "exists", the result is 1 or 0.
 *	TH8_ERROR on unrecognized subcommand or wrong argument
 *	count.
 *
 * Side effects:
 *	"create" allocates an encrypted variable.  "delete" frees
 *	it.  "exists" has no side effects.
 *
 *----------------------------------------------------------------------
 */

static int
secure_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    (void)ctx;

    if (argc < 3) {
	return Th8_WrongNumArgs(interp, "secure option varName ?value?");
    }

    if (argl[1] == 6 && Th8_Memcmp(interp, argv[1], "create", 6) == 0) {
	if (argc != 3 && argc != 4) {
	    return Th8_WrongNumArgs(interp, "secure create varName ?value?");
	}
	return th8SecureVarCreate(
	    interp, argv[2], argl[2], argc == 4 ? argv[3] : NULL,
	    argc == 4 ? argl[3] : 0);

    } else if (
        argl[1] == 6 && Th8_Memcmp(interp, argv[1], "exists", 6) == 0) {
	if (argc != 3) {
	    return Th8_WrongNumArgs(interp, "secure exists varName");
	}
	Th8_SetResultInt(interp, th8IsSecureVar(interp, argv[2], argl[2]));
	return TH8_OK;

    } else if (
        argl[1] == 6 && Th8_Memcmp(interp, argv[1], "delete", 6) == 0) {
	if (argc != 3) {
	    return Th8_WrongNumArgs(interp, "secure delete varName");
	}
	return th8SecureVarDelete(interp, argv[2], argl[2]);

    } else if (argl[1] == 4 && Th8_Memcmp(interp, argv[1], "save", 4) == 0) {
	if (argc != 3) {
	    return Th8_WrongNumArgs(interp, "secure save varName");
	}
	return th8SecureSave(interp, argv[2], argl[2]);

    } else if (argl[1] == 4 && Th8_Memcmp(interp, argv[1], "load", 4) == 0) {
	if (argc != 3) {
	    return Th8_WrongNumArgs(interp, "secure load varName");
	}
	return th8SecureLoad(interp, argv[2], argl[2]);

    } else {
	Th8_SetResultStatic(
	    interp,
	    "secure: must be create, exists, delete, "
	    "load, or save",
	    TH8_NOLEN);
	return TH8_ERROR;
    }
}
#  endif /* TH8_ENABLE_VARIABLES */


/*
 *----------------------------------------------------------------------
 *
 * Command table and plugin registration.
 *
 *----------------------------------------------------------------------
 */

static Th8_CommandEntry th8CryptoCommands[] = {
    {1, 0, "hash", hash_command},
#  if defined(TH8_ENABLE_VARIABLES)
    {1, 0, "secure", secure_command},
#  endif
};

/*
 *----------------------------------------------------------------------
 *
 * th8CryptoGetCommands --
 *
 *	Return the command table for the cryptography plugin.
 *
 * Why / How:
 *	Called by the plugin registration system to discover which
 *	commands this plugin provides.  On the first call pCommand
 *	is NULL and *pnCommand is set to the count; on the second
 *	call the entries are copied into the caller-provided array.
 *	The "secure" command is conditionally included only when
 *	TH8_ENABLE_VARIABLES is defined.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR if pnCommand is NULL or the
 *	caller's buffer is too small.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
th8CryptoGetCommands(Th8_CommandEntry *pCommand, int *pnCommand)
{
    int n = (int)(sizeof(th8CryptoCommands) / sizeof(th8CryptoCommands[0]));

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
	    pCommand[i] = th8CryptoCommands[i];
	}
    }
    return TH8_OK;
}


#endif /* TH8_ENABLE_CRYPTOGRAPHY */
