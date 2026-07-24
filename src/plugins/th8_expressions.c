/*
 * th8_expressions.c -- Expressions plugin for TH8.
 *
 * Implements the expression-related commands: expr and fpclassify.
 *
 * This file is part of the plugin architecture.  The commands
 * are registered via Th8_RegisterPlugin using the static
 * command table returned by th8ExpressionsGetCommands.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "th8.h"
#include "th8_int.h"
#include "th8_plugin.h"

#if defined(TH8_PLUGIN_EXPRESSIONS)


/*
 *----------------------------------------------------------------------
 *
 * expr_command --
 *
 *	Implements the Tcl [expr] command.  Evaluates an expression.
 *	Per Tcl 8.4, accepts an arbitrary number of arguments which
 *	are concatenated with spaces.
 *
 *	expr ARG ?ARG ...?
 *
 * Why / How:
 *	Single-argument case passes directly to Th8_Expr.  Multi-
 *	argument case concatenates all arguments with spaces into a
 *	temporary string, then evaluates.  This matches the Tcl 8.4
 *	behavior where [expr 1 + 2] is equivalent to [expr {1 + 2}].
 *
 * Results:
 *	Return code from Th8_Expr.
 *
 * Side effects:
 *	Sets interpreter result.
 *
 *----------------------------------------------------------------------
 */

static int
expr_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    if (argc < 2) {
	return Th8_WrongNumArgs(interp, "expr arg ?arg ...?");
    }
    if (argc == 2) {
	return Th8_Expr(interp, argv[1], argl[1], NULL, 0);
    } else {
	/*
	 * Concatenate all arguments with spaces, then evaluate.
	 */

	char *zExpr = 0;
	size_t nExpr = 0;
	int i;
	int rc;

	for (i = 1; i < argc; i++) {
	    if (i > 1) {
		TH8_STR_APPEND(interp, &zExpr, &nExpr, " ", 1);
	    }
	    TH8_STR_APPEND(interp, &zExpr, &nExpr, argv[i], argl[i]);
	}
	rc = Th8_Expr(interp, zExpr, nExpr, NULL, 0);
	Th8_Free(interp, zExpr);
	return rc;

oom:
	Th8_Free(interp, zExpr);
	return TH8_ERROR;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * fpclassify_command --
 *
 *	Implements the Tcl [fpclassify] command (TIP #521).
 *	Classifies a floating-point number.
 *
 *	fpclassify value
 *
 *	Returns one of: "zero", "subnormal", "normal", "infinite",
 *	"nan".
 *
 * Why / How:
 *	Parses the value as a double, then delegates to the platform's
 *	xMathFunc callback with TH8_MATH_FPCLASSIFY.  Maps the numeric
 *	result code (0-4) to the corresponding string.  Falls back to
 *	"normal" if the platform callback is unavailable.
 *
 * Results:
 *	TH8_OK.  Result is the classification string.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
fpclassify_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    double d;
    double result = 0.0;
    const Th8_Platform *pPlat;

    (void)ctx;

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "fpclassify value");
    }
    if (TH8_OK != Th8_ToDouble(interp, argv[1], argl[1], &d)) {
	return TH8_ERROR;
    }

    pPlat = Th8_GetPlatform(interp);
    if (ALWAYS(pPlat) && pPlat->xMathFunc &&
        TH8_OK ==
            pPlat->xMathFunc(
                interp, pPlat->pCtx, TH8_MATH_FPCLASSIFY, &result, d, 0.0)) {
	switch ((int)result) {
	case 0:
	    Th8_SetResultStatic(interp, "zero", 4);
	    return TH8_OK;
	case 1:
	    Th8_SetResultStatic(interp, "subnormal", 9);
	    return TH8_OK;
	case 2:
	    Th8_SetResultStatic(interp, "normal", 6);
	    return TH8_OK;
	case 3:
	    Th8_SetResultStatic(interp, "infinite", 8);
	    return TH8_OK;
	case 4:
	    Th8_SetResultStatic(interp, "nan", 3);
	    return TH8_OK;
	}
    }
    Th8_SetResultStatic(interp, "normal", 6);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Command table and plugin registration.
 *
 *----------------------------------------------------------------------
 */

static Th8_CommandEntry th8ExpressionsCommands[] = {
    {1, 0, "expr", expr_command},
    {1, 0, "fpclassify", fpclassify_command},
};

/*
 *----------------------------------------------------------------------
 *
 * th8ExpressionsGetCommands --
 *
 *	Return the command table for the expressions plugin.
 *
 * Why / How:
 *	Called by Th8_RegisterPlugin during interpreter initialization.
 *	Uses the standard two-call protocol: first call with pCommand
 *	NULL to query the count, second call to copy the entries.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if pnCommand is NULL or the
 *	output array is too small.
 *
 * Side effects:
 *	Copies command entries into the caller-provided array.
 *
 *----------------------------------------------------------------------
 */

int
th8ExpressionsGetCommands(Th8_CommandEntry *pCommand, int *pnCommand)
{
    int n = (int)(sizeof(th8ExpressionsCommands) /
                  sizeof(th8ExpressionsCommands[0]));

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
	    pCommand[i] = th8ExpressionsCommands[i];
	}
    }
    return TH8_OK;
}
#endif /* TH8_PLUGIN_EXPRESSIONS */
