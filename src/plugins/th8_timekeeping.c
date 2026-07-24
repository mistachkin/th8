/*
 * th8_timekeeping.c -- Timekeeping plugin for TH8.
 *
 * Implements the time-related commands: after, clock (with
 * seconds, ntp, and https subcommands), and time.
 *
 * This file is part of the plugin architecture.  The commands
 * are registered via Th8_RegisterPlugin using the static
 * command table returned by th8TimekeepingGetCommands.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "th8.h"
#include "th8_int.h"
#include "th8_plugin.h"

#if defined(TH8_PLUGIN_TIMEKEEPING)


/*
 *----------------------------------------------------------------------
 *
 * after_command --
 *
 *	Sleep for the specified number of milliseconds.
 *
 *	after MILLISECONDS
 *
 *	Sleeps in small increments (50ms) via the platform's
 *	xSleep callback, checking Th8_Ready between each increment
 *	so that cancellation, suspension, and step-counter limits
 *	are honored.  Returns the empty string.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR if cancelled or suspended.
 *
 * Why / How:
 *	Implements the Tcl [after] command (delay-only form; event
 *	scheduling is not supported).  Sleeps in 50ms increments
 *	via the platform's xSleep callback and calls Th8_Ready
 *	between each increment so that cancellation, suspension,
 *	and step-counter limits remain responsive during long waits.
 *
 * Side effects:
 *	Blocks the interpreter for up to MILLISECONDS.
 *
 *----------------------------------------------------------------------
 */

static int
after_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    th8_int64_t totalMs;
    th8_int64_t startMs, nowMs, elapsedMs;
    int rc;

    (void)ctx;

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "after milliseconds");
    }

    rc = Th8_ToWideInt(interp, argv[1], argl[1], &totalMs);
    if (rc != TH8_OK) return rc;
    if (totalMs < 0) totalMs = 0;

    Th8_GetTimeMs(interp, &startMs);

    while (1) {
	rc = Th8_Ready(interp);
	if (rc != TH8_OK) return rc;

	Th8_GetTimeMs(interp, &nowMs);
	elapsedMs = nowMs - startMs;
	if (elapsedMs >= totalMs) break;

	{
	    th8_int64_t remaining = totalMs - elapsedMs;
	    int chunk = (remaining > 50) ? 50 : (int)remaining;

	    Th8_Sleep(interp, chunk);
	}
    }

    Th8_ClearResult(interp);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * clock_seconds_command --
 *
 *	Return the current time as Unix epoch seconds.
 *
 *	clock seconds
 *
 * Why / How:
 *	Implements the [clock seconds] subcommand.  Retrieves the
 *	current wall-clock time via Th8_GetTimeMs (milliseconds)
 *	and divides by 1000 to produce Unix epoch seconds,
 *	matching Tcl 8.x semantics.
 *
 * Results:
 *	TH8_OK with the epoch seconds as the interpreter result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
clock_seconds_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    th8_int64_t iMs = 0;

    (void)ctx;
    (void)argv;
    (void)argl;

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "clock seconds");
    }
    Th8_GetTimeMs(interp, &iMs);
    Th8_SetResultWideInt(interp, iMs / 1000);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * clock_command --
 *
 *	Dispatcher for clock sub-commands.
 *
 *	The "seconds" subcommand is built-in.  The "ntp" and "https"
 *	subcommands are provided by the Harpy plugin via internal
 *	(non-static) callbacks declared in th8_plugin.h.  This
 *	decouples the timekeeping plugin from the Harpy/crypto
 *	subsystem at the source level.
 *
 * Why / How:
 *	Implements the Tcl [clock] command as an ensemble dispatcher.
 *	Uses Th8_CallSubCommand with a static subcommand table so
 *	that adding new subcommands requires only a table entry.
 *	The "ntp" and "https" subcommands are conditionally compiled
 *	under TH8_ENABLE_CRYPTOGRAPHY and reference non-static
 *	functions from the Harpy plugin, keeping the compile-time
 *	dependency optional.
 *
 * Results:
 *	Returns the result of the dispatched subcommand, or
 *	TH8_ERROR if the subcommand is not recognized.
 *
 * Side effects:
 *	Depends on the subcommand invoked.
 *
 *----------------------------------------------------------------------
 */

static int
clock_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    static const Th8_SubCommand aSub[] =
        {{0, "seconds", clock_seconds_command},
#  if defined(TH8_ENABLE_CRYPTOGRAPHY)
         {0, "https", th8HarpyClockHttpsCommand},
         {0, "ntp", th8HarpyClockNtpCommand},
#  endif
         {0, 0, 0}};

    return Th8_CallSubCommand(interp, ctx, argc, argv, argl, aSub);
}


/*
 *----------------------------------------------------------------------
 *
 * time_command --
 *
 *	Measure the execution time of a script.
 *
 *	time SCRIPT ?COUNT?
 *
 *	Evaluates SCRIPT COUNT times (default 1) and returns a
 *	string of the form "N microseconds per iteration".
 *
 * Results:
 *	TH8_OK on success; propagates errors from the script.
 *
 * Why / How:
 *	Implements the Tcl [time] command.  Captures the wall-clock
 *	time in microseconds via Th8_GetTimeUs before and after
 *	evaluating the script COUNT times, then computes the average
 *	microseconds per iteration.  The result string is formatted
 *	to match Tcl's "N microseconds per iteration" convention.
 *
 * Side effects:
 *	Evaluates SCRIPT up to COUNT times with all attendant
 *	side effects.
 *
 *----------------------------------------------------------------------
 */

static int
time_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    th8_int64_t count = 1;
    th8_int64_t i;
    th8_int64_t startUs, endUs, usPerIter;
    int rc = TH8_OK;
    char *zOut = NULL;
    size_t nOut = 0;

    (void)ctx;

    if (argc != 2 && argc != 3) {
	return Th8_WrongNumArgs(interp, "time script ?count?");
    }

    if (argc == 3) {
	rc = Th8_ToWideInt(interp, argv[2], argl[2], &count);
	if (rc != TH8_OK) return rc;
	if (count < 0) count = 0;
    }

    Th8_GetTimeUs(interp, &startUs);

    for (i = 0; i < count; i++) {
	rc = Th8_Eval(interp, 0, argv[1], argl[1], NULL, 0);
	if (rc != TH8_OK) return rc;
    }

    Th8_GetTimeUs(interp, &endUs);

    usPerIter = (count > 0) ? (endUs - startUs) / count : 0;

    {
	Th8_SetResultWideInt(interp, usPerIter);
	{
	    size_t nNum;
	    const char *zNum = Th8_GetResult(interp, &nNum);

	    TH8_STR_APPEND(interp, &zOut, &nOut, zNum, nNum);
	}
	TH8_STR_APPEND(
	    interp, &zOut, &nOut, " microseconds per iteration", 27);
	Th8_SetResult(interp, zOut, nOut);
	Th8_Free(interp, zOut);
    }

    return TH8_OK;

oom:
    Th8_Free(interp, zOut);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * Command table and plugin registration.
 *
 *----------------------------------------------------------------------
 */

static Th8_CommandEntry th8TimekeepingCommands[] = {
    {1, 0, "after", after_command},
    {1, 0, "clock", clock_command},
    {1, 0, "time", time_command},
};

/*
 *----------------------------------------------------------------------
 *
 * th8TimekeepingGetCommands --
 *
 *	Return the command table for the timekeeping plugin.
 *
 * Why / How:
 *	Called by the plugin registration system to discover which
 *	commands this plugin provides.  On the first call pCommand
 *	is NULL and *pnCommand is set to the count; on the second
 *	call the entries are copied into the caller-provided array.
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
th8TimekeepingGetCommands(Th8_CommandEntry *pCommand, int *pnCommand)
{
    int n = (int)(sizeof(th8TimekeepingCommands) /
                  sizeof(th8TimekeepingCommands[0]));

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
	    pCommand[i] = th8TimekeepingCommands[i];
	}
    }
    return TH8_OK;
}
#endif /* TH8_PLUGIN_TIMEKEEPING */
