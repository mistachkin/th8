/*
 * th8_sqlite3.c -- SQLite key-value platform extension for TH8.
 *
 * Provides an xKeyValue callback backed by a SQLite database table.
 * Loadable as a TH8 extension via [load].  On initialization, the
 * extension opens a SQLite database, creates the KV table, prepares
 * cached statements, and merges its platform into the interpreter.
 *
 * Schema:
 *   CREATE TABLE IF NOT EXISTS kv (
 *       name  TEXT PRIMARY KEY NOT NULL,
 *       value TEXT NOT NULL
 *   );
 *
 * Thread safety: all operations serialize on a module-static mutex.
 * Connection and statement tracking ensures clean shutdown.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "th8_meta_defs.h"
#include "th8_meta_libc.h"

#if defined(_WIN32) || defined(WIN32)
#  include "th8_meta_msvc.h"
#  include "th8_meta_win32.h"
#else
#  include "th8_meta_posix.h"
#endif

#include "th8.h"
#include "th8_int.h"

#ifdef USE_TH8_STUBS
#  include "th8Decls.h"
#endif

#include "th8_sqlite3.h"
#include "sqlite3.h"


/*
 *======================================================================
 * Context and module-static state.
 *======================================================================
 */

typedef struct Th8_SQLiteKvCtx {
    sqlite3 *pDb;  /* Database connection. */
    char *zDatabase;  /* Database file path (owned). */
    size_t nDatabase;
    char *zTable;  /* Table name (owned). */
    size_t nTable;
    sqlite3_stmt *pExists; /* SELECT 1 FROM t WHERE name=? */
    sqlite3_stmt *pGet;  /* SELECT value FROM t WHERE name=? */
    sqlite3_stmt *pSet;  /* INSERT OR REPLACE INTO t VALUES(?,?) */
    sqlite3_stmt *pUnset; /* DELETE FROM t WHERE name=? */
    sqlite3_stmt *pListAll; /* SELECT name FROM t */
    sqlite3_stmt *pGetAll; /* SELECT name, value FROM t */
    Th8_Interp *pCurrentInterp; /* set by xKeyValue under th8SqliteLock so
				 * the busy handler can call Th8_Ready to
				 * honor cancellation; NULL when no
				 * operation is in flight */
} Th8_SQLiteKvCtx;


/*
 * Module-static: the single KV context for this extension instance.
 * A future version may support multiple named databases via a hash
 * table; for now, one context suffices.
 */

static Th8_SQLiteKvCtx *th8SqliteCtx = NULL;


/*
 *======================================================================
 * Mutex (serializes all xKeyValue operations).
 *======================================================================
 */

#if !defined(_WIN32) && !defined(WIN32)

/* <pthread.h> included via th8_meta_posix.h */

static volatile int th8SqliteMutexReady = 0;
static pthread_mutex_t th8SqliteMutex;

/*
 *----------------------------------------------------------------------
 *
 * th8SqliteLock (POSIX) --
 *
 *	Acquire the per-process SQLite-backed-store mutex.
 *	Implements the canonical CAS-based lazy-init pattern:
 *
 *	  *  Fast path: `th8SqliteMutexReady == 1`, jump
 *	     straight to `pthread_mutex_lock`.
 *	  *  Slow path (first caller): CAS `0 -> -1` to claim
 *	     init; initialise the mutex; full memory fence;
 *	     publish via `th8SqliteMutexReady = 1`.
 *	  *  Slow path (subsequent racers): spin until
 *	     `th8SqliteMutexReady == 1`, then fence and lock.
 *
 *	Mirror of the Win32 implementation below.  The lock
 *	is needed because the SQLite-backed-store layer is
 *	shared across interpreters; threads can race on
 *	first registration of the shared schema.
 *
 * Parameters:
 *	(none)
 *
 * Returns:
 *	None.
 *
 * Side effects:
 *	Initialises the per-process mutex on first call;
 *	acquires it on every call.
 *
 *----------------------------------------------------------------------
 */
static void
th8SqliteLock(void)
{
    if (!th8SqliteMutexReady) {
	if (__sync_val_compare_and_swap(&th8SqliteMutexReady, 0, -1) == 0) {
	    pthread_mutex_init(&th8SqliteMutex, NULL);
	    __sync_synchronize();
	    th8SqliteMutexReady = 1;
	} else {
	    while (th8SqliteMutexReady != 1) {
		/* spin */
	    }
	    __sync_synchronize();
	}
    }
    pthread_mutex_lock(&th8SqliteMutex);
}

/*
 *----------------------------------------------------------------------
 *
 * th8SqliteUnlock (POSIX) --
 *
 *	Release the per-process SQLite-backed-store mutex
 *	previously acquired by `th8SqliteLock`.  Mirror of
 *	the Win32 implementation below.
 *
 *	Caller must hold the mutex.
 *
 * Parameters:
 *	(none)
 *
 * Returns:
 *	None.
 *
 * Side effects:
 *	Releases the per-process mutex.
 *
 *----------------------------------------------------------------------
 */
static void
th8SqliteUnlock(void)
{
    pthread_mutex_unlock(&th8SqliteMutex);
}

#else /* Win32 */

/* <windows.h> included via th8_meta_win32.h */

static CRITICAL_SECTION th8SqliteCritSec;
static volatile LONG th8SqliteCritSecReady = 0;

/*
 *----------------------------------------------------------------------
 *
 * th8SqliteLock (Win32) --
 *
 *	Acquire the per-process SQLite-backed-store critical
 *	section.  Same CAS-based lazy-init pattern as the
 *	POSIX variant above, expressed against
 *	`InterlockedCompareExchange` /
 *	`InitializeCriticalSection`.  Mirror of the POSIX
 *	implementation.
 *
 * Parameters:
 *	(none)
 *
 * Returns:
 *	None.
 *
 * Side effects:
 *	Initialises the per-process critical section on
 *	first call; enters it on every call.
 *
 *----------------------------------------------------------------------
 */
static void
th8SqliteLock(void)
{
    if (!th8SqliteCritSecReady) {
	if (InterlockedCompareExchange(&th8SqliteCritSecReady, -1, 0) == 0) {
	    InitializeCriticalSection(&th8SqliteCritSec);
	    InterlockedExchange(&th8SqliteCritSecReady, 1);
	} else {
	    while (th8SqliteCritSecReady != 1) {
		/* spin */
	    }
	}
    }
    EnterCriticalSection(&th8SqliteCritSec);
}

/*
 *----------------------------------------------------------------------
 *
 * th8SqliteUnlock (Win32) --
 *
 *	Release the per-process SQLite-backed-store critical
 *	section acquired by `th8SqliteLock`.  Mirror of the
 *	POSIX implementation above.
 *
 *	Caller must hold the critical section.
 *
 * Parameters:
 *	(none)
 *
 * Returns:
 *	None.
 *
 * Side effects:
 *	Leaves the per-process critical section.
 *
 *----------------------------------------------------------------------
 */
static void
th8SqliteUnlock(void)
{
    LeaveCriticalSection(&th8SqliteCritSec);
}

#endif /* Win32 */


/*
 *----------------------------------------------------------------------
 *
 * th8SqlitePrepare --
 *
 *	Prepare a single SQL statement, formatted with the table name.
 *	Returns SQLITE_OK or an error code.
 *
 *----------------------------------------------------------------------
 */

static int
th8SqlitePrepare(
    sqlite3 *pDb,
    const char *zFmt,  /* SQL with %s for table name. */
    const char *zTable,
    sqlite3_stmt **ppStmt)
{
    char zSql[512] = {0};

    sqlite3_snprintf(sizeof(zSql), zSql, zFmt, zTable);
    return sqlite3_prepare_v2(pDb, zSql, -1, ppStmt, 0);
}


/*
 *----------------------------------------------------------------------
 *
 * th8SqliteKeyValue --
 *
 *	xKeyValue callback backed by a SQLite database.
 *
 *----------------------------------------------------------------------
 */

static int
th8SqliteKeyValue(
    Th8_Interp *interp,
    void *pCtx,
    int op,
    const char *zName,
    size_t nName,
    const char *zValue,
    size_t nValue)
{
    Th8_SQLiteKvCtx *ctx = (Th8_SQLiteKvCtx *)pCtx;
    int rc = TH8_ERROR;
    int bInTxn = 0;

    if (!ctx || !ctx->pDb) {
	Th8_SetResultStatic(interp, "kv: no database connection", TH8_NOLEN);
	return TH8_ERROR;
    }

    th8SqliteLock();

    /*
     * Stash the invoking interpreter so the busy handler installed
     * during open can call Th8_Ready to detect cancellation while
     * SQLite is retrying a SQLITE_BUSY.  Cleared on the way out so
     * the handler does not see a stale pointer if SQLite ever
     * invokes it outside an active operation (defensive).
     */

    ctx->pCurrentInterp = interp;

    switch (op) {

    case TH8_KV_EXISTS: {
	int sqlRc;

	if (sqlite3_reset(ctx->pExists) != SQLITE_OK) goto kv_err;
	if (sqlite3_bind_text(
	        ctx->pExists, 1, zName, (int)nName, SQLITE_TRANSIENT) !=
	    SQLITE_OK) {
	    goto kv_err;
	}
	sqlRc = sqlite3_step(ctx->pExists);
	rc = (sqlRc == SQLITE_ROW) ? TH8_OK : TH8_ERROR;
	if (sqlRc != SQLITE_ROW && sqlRc != SQLITE_DONE) goto kv_err;
	sqlite3_clear_bindings(ctx->pExists);
	sqlite3_reset(ctx->pExists);
	break;
    }

    case TH8_KV_GET: {
	int sqlRc;

	if (sqlite3_reset(ctx->pGet) != SQLITE_OK) goto kv_err;
	if (sqlite3_bind_text(
	        ctx->pGet, 1, zName, (int)nName, SQLITE_TRANSIENT) !=
	    SQLITE_OK) {
	    goto kv_err;
	}
	sqlRc = sqlite3_step(ctx->pGet);
	if (sqlRc == SQLITE_ROW) {
	    const char *zVal = (const char *)
	        sqlite3_column_text(ctx->pGet, 0);
	    int nVal = sqlite3_column_bytes(ctx->pGet, 0);
	    Th8_SetResult(interp, zVal, (size_t)nVal);
	    rc = TH8_OK;
	} else if (sqlRc == SQLITE_DONE) {
	    Th8_SetResultStatic(interp, "key not found", TH8_NOLEN);
	} else {
	    goto kv_err;
	}
	sqlite3_clear_bindings(ctx->pGet);
	sqlite3_reset(ctx->pGet);
	break;
    }

    case TH8_KV_SET: {
	int sqlRc;

	if (sqlite3_reset(ctx->pSet) != SQLITE_OK) goto kv_err;
	if (sqlite3_bind_text(
	        ctx->pSet, 1, zName, (int)nName, SQLITE_TRANSIENT) !=
	    SQLITE_OK) {
	    goto kv_err;
	}
	if (sqlite3_bind_text(
	        ctx->pSet, 2, zValue, (int)nValue, SQLITE_TRANSIENT) !=
	    SQLITE_OK) {
	    goto kv_err;
	}
	sqlRc = sqlite3_step(ctx->pSet);
	if (sqlRc == SQLITE_DONE) {
	    rc = TH8_OK;
	} else {
	    Th8_SetResultStatic(interp, "kv set failed", TH8_NOLEN);
	}
	sqlite3_clear_bindings(ctx->pSet);
	sqlite3_reset(ctx->pSet);
	break;
    }

    case TH8_KV_UNSET: {
	int sqlRc;

	if (sqlite3_reset(ctx->pUnset) != SQLITE_OK) goto kv_err;
	if (sqlite3_bind_text(
	        ctx->pUnset, 1, zName, (int)nName, SQLITE_TRANSIENT) !=
	    SQLITE_OK) {
	    goto kv_err;
	}
	sqlRc = sqlite3_step(ctx->pUnset);
	if (sqlRc == SQLITE_DONE) {
	    rc = TH8_OK;
	} else {
	    goto kv_err;
	}
	sqlite3_clear_bindings(ctx->pUnset);
	sqlite3_reset(ctx->pUnset);
	break;
    }

    case TH8_KV_LIST: {
	char *zList = NULL;
	size_t nList = 0;
	int sqlRc;

	if (sqlite3_reset(ctx->pListAll) != SQLITE_OK) goto kv_err;
	while ((sqlRc = sqlite3_step(ctx->pListAll)) == SQLITE_ROW) {
	    const char *zKey = (const char *)
	        sqlite3_column_text(ctx->pListAll, 0);
	    int nKey = sqlite3_column_bytes(ctx->pListAll, 0);

	    if (zName && nName > 0) {
		if (!Th8_GlobMatch(
		        interp, zName, nName, zKey, (size_t)nKey)) {
		    continue;
		}
	    }
	    Th8_ListAppend(interp, &zList, &nList, zKey, (size_t)nKey);
	}
	sqlite3_reset(ctx->pListAll);
	if (sqlRc != SQLITE_DONE) {
	    if (zList) Th8_Free(interp, zList);
	    goto kv_err;
	}

	if (zList) {
	    Th8_SetResult(interp, zList, nList);
	    Th8_Free(interp, zList);
	} else {
	    Th8_ClearResult(interp);
	}
	rc = TH8_OK;
	break;
    }

    case TH8_KV_EXISTS2: {
	int found = 0;
	int sqlRc;

	if (sqlite3_reset(ctx->pGetAll) != SQLITE_OK) goto kv_err;
	while ((sqlRc = sqlite3_step(ctx->pGetAll)) == SQLITE_ROW && !found) {
	    const char *zK = (const char *)
	        sqlite3_column_text(ctx->pGetAll, 0);
	    int nK = sqlite3_column_bytes(ctx->pGetAll, 0);
	    const char *zV = (const char *)
	        sqlite3_column_text(ctx->pGetAll, 1);
	    int nV = sqlite3_column_bytes(ctx->pGetAll, 1);

	    if (zName && nName > 0 &&
	        !Th8_GlobMatch(interp, zName, nName, zK, (size_t)nK)) {
		continue;
	    }
	    if (zValue && nValue > 0 &&
	        !Th8_GlobMatch(interp, zValue, nValue, zV, (size_t)nV)) {
		continue;
	    }
	    found = 1;
	}
	sqlite3_reset(ctx->pGetAll);
	if (!found && sqlRc != SQLITE_DONE && sqlRc != SQLITE_ROW) {
	    goto kv_err;
	}
	rc = found ? TH8_OK : TH8_ERROR;
	break;
    }

    case TH8_KV_LIST2: {
	char *zList = NULL;
	size_t nList = 0;
	int sqlRc;

	if (sqlite3_reset(ctx->pGetAll) != SQLITE_OK) goto kv_err;
	while ((sqlRc = sqlite3_step(ctx->pGetAll)) == SQLITE_ROW) {
	    const char *zK = (const char *)
	        sqlite3_column_text(ctx->pGetAll, 0);
	    int nK = sqlite3_column_bytes(ctx->pGetAll, 0);
	    const char *zV = (const char *)
	        sqlite3_column_text(ctx->pGetAll, 1);
	    int nV = sqlite3_column_bytes(ctx->pGetAll, 1);

	    if (zName && nName > 0 &&
	        !Th8_GlobMatch(interp, zName, nName, zK, (size_t)nK)) {
		continue;
	    }
	    if (zValue && nValue > 0 &&
	        !Th8_GlobMatch(interp, zValue, nValue, zV, (size_t)nV)) {
		continue;
	    }
	    Th8_ListAppend(interp, &zList, &nList, zK, (size_t)nK);
	}
	sqlite3_reset(ctx->pGetAll);
	if (sqlRc != SQLITE_DONE) {
	    if (zList) Th8_Free(interp, zList);
	    goto kv_err;
	}

	if (zList) {
	    Th8_SetResult(interp, zList, nList);
	    Th8_Free(interp, zList);
	} else {
	    Th8_ClearResult(interp);
	}
	rc = TH8_OK;
	break;
    }

    case TH8_KV_GET2: {
	char *zDict = NULL;
	size_t nDict = 0;
	int sqlRc;

	if (sqlite3_reset(ctx->pGetAll) != SQLITE_OK) goto kv_err;
	while ((sqlRc = sqlite3_step(ctx->pGetAll)) == SQLITE_ROW) {
	    const char *zK = (const char *)
	        sqlite3_column_text(ctx->pGetAll, 0);
	    int nK = sqlite3_column_bytes(ctx->pGetAll, 0);
	    const char *zV = (const char *)
	        sqlite3_column_text(ctx->pGetAll, 1);
	    int nV = sqlite3_column_bytes(ctx->pGetAll, 1);

	    if (zName && nName > 0 &&
	        !Th8_GlobMatch(interp, zName, nName, zK, (size_t)nK)) {
		continue;
	    }
	    if (zValue && nValue > 0 &&
	        !Th8_GlobMatch(interp, zValue, nValue, zV, (size_t)nV)) {
		continue;
	    }
	    Th8_ListAppend(interp, &zDict, &nDict, zK, (size_t)nK);
	    Th8_ListAppend(interp, &zDict, &nDict, zV, (size_t)nV);
	}
	sqlite3_reset(ctx->pGetAll);
	if (sqlRc != SQLITE_DONE) {
	    if (zDict) Th8_Free(interp, zDict);
	    goto kv_err;
	}

	if (zDict) {
	    Th8_SetResult(interp, zDict, nDict);
	    Th8_Free(interp, zDict);
	} else {
	    Th8_ClearResult(interp);
	}
	rc = TH8_OK;
	break;
    }

    case TH8_KV_SET2: {
	/*
	 * Collect matching keys, then update in a second pass.
	 * TH8_LIST_NO_CACHE is required because zKeys is a
	 * temporary buffer freed after the split.
	 *
	 * The scan + write are wrapped in a single BEGIN IMMEDIATE
	 * transaction.  This guarantees atomicity (either every
	 * matched key is updated or none are -- a mid-loop failure,
	 * a SQLITE_BUSY on retry exhaustion, or a process crash
	 * cannot leave the KV store half-updated) and gives a single
	 * journal flush instead of one per key.  IMMEDIATE rather
	 * than DEFERRED because the scan implies a subsequent write
	 * to the same table; acquiring the write lock at BEGIN time
	 * avoids the upgrade-busy race where another writer steals
	 * the lock between the scan and the first sqlite3_step(pSet).
	 */

	char *zKeys = NULL;
	size_t nKeys = 0;
	int sqlRc;
	int bWriteFailed = 0;

	if (sqlite3_exec(ctx->pDb, "BEGIN IMMEDIATE", 0, 0, 0) != SQLITE_OK)
	    goto kv_err;
	bInTxn = 1;

	if (sqlite3_reset(ctx->pGetAll) != SQLITE_OK) goto kv_err;
	while ((sqlRc = sqlite3_step(ctx->pGetAll)) == SQLITE_ROW) {
	    const char *zK = (const char *)
	        sqlite3_column_text(ctx->pGetAll, 0);
	    int nK = sqlite3_column_bytes(ctx->pGetAll, 0);

	    if (zName && nName > 0 &&
	        !Th8_GlobMatch(interp, zName, nName, zK, (size_t)nK)) {
		continue;
	    }
	    Th8_ListAppend(interp, &zKeys, &nKeys, zK, (size_t)nK);
	}
	sqlite3_reset(ctx->pGetAll);
	if (sqlRc != SQLITE_DONE) {
	    if (zKeys) Th8_Free(interp, zKeys);
	    goto kv_err;
	}

	if (zKeys) {
	    char **azElem = NULL;
	    size_t *anElem = NULL;
	    int nCount = 0;
	    int j;

	    if (Th8_SplitList(
	            interp, zKeys, nKeys, &azElem, &anElem, &nCount,
	            TH8_LIST_NO_CACHE) == TH8_OK) {
		for (j = 0; j < nCount; j++) {
		    if (sqlite3_reset(ctx->pSet) != SQLITE_OK) {
			bWriteFailed = 1;
			break;
		    }
		    if (sqlite3_bind_text(
		            ctx->pSet, 1, azElem[j], (int)anElem[j],
		            SQLITE_TRANSIENT) != SQLITE_OK) {
			bWriteFailed = 1;
			break;
		    }
		    if (sqlite3_bind_text(
		            ctx->pSet, 2, zValue, (int)nValue,
		            SQLITE_TRANSIENT) != SQLITE_OK) {
			bWriteFailed = 1;
			break;
		    }
		    sqlRc = sqlite3_step(ctx->pSet);
		    sqlite3_clear_bindings(ctx->pSet);
		    sqlite3_reset(ctx->pSet);
		    if (sqlRc != SQLITE_DONE) {
			bWriteFailed = 1;
			break;
		    }
		}
		Th8_Free(interp, azElem);
		/* anElem is interior to the azElem block; do NOT free. */
	    } else {
		bWriteFailed = 1;
	    }
	    Th8_Free(interp, zKeys);
	}

	if (bWriteFailed) goto kv_err;

	if (sqlite3_exec(ctx->pDb, "COMMIT", 0, 0, 0) != SQLITE_OK)
	    goto kv_err;
	bInTxn = 0;

	Th8_ClearResult(interp);
	rc = TH8_OK;
	break;
    }

    case TH8_KV_UNSET2: {
	/*
	 * Same transaction discipline as SET2: BEGIN IMMEDIATE wraps
	 * the scan + per-key DELETE so that either every matched
	 * (key,value) pair is removed or none are.  See the SET2
	 * comment above for the rationale on IMMEDIATE vs DEFERRED.
	 */

	char *zDict = NULL;
	size_t nDict = 0;
	char *zKeys = NULL;
	size_t nKeys = 0;
	int sqlRc;
	int bWriteFailed = 0;

	if (sqlite3_exec(ctx->pDb, "BEGIN IMMEDIATE", 0, 0, 0) != SQLITE_OK)
	    goto kv_err;
	bInTxn = 1;

	if (sqlite3_reset(ctx->pGetAll) != SQLITE_OK) goto kv_err;
	while ((sqlRc = sqlite3_step(ctx->pGetAll)) == SQLITE_ROW) {
	    const char *zK = (const char *)
	        sqlite3_column_text(ctx->pGetAll, 0);
	    int nK = sqlite3_column_bytes(ctx->pGetAll, 0);
	    const char *zV = (const char *)
	        sqlite3_column_text(ctx->pGetAll, 1);
	    int nV = sqlite3_column_bytes(ctx->pGetAll, 1);

	    if (zName && nName > 0 &&
	        !Th8_GlobMatch(interp, zName, nName, zK, (size_t)nK)) {
		continue;
	    }
	    if (zValue && nValue > 0 &&
	        !Th8_GlobMatch(interp, zValue, nValue, zV, (size_t)nV)) {
		continue;
	    }
	    Th8_ListAppend(interp, &zDict, &nDict, zK, (size_t)nK);
	    Th8_ListAppend(interp, &zDict, &nDict, zV, (size_t)nV);
	    Th8_ListAppend(interp, &zKeys, &nKeys, zK, (size_t)nK);
	}
	sqlite3_reset(ctx->pGetAll);
	if (sqlRc != SQLITE_DONE) {
	    if (zDict) Th8_Free(interp, zDict);
	    if (zKeys) Th8_Free(interp, zKeys);
	    goto kv_err;
	}

	if (zKeys) {
	    char **azElem = NULL;
	    size_t *anElem = NULL;
	    int nCount = 0;
	    int j;

	    if (Th8_SplitList(
	            interp, zKeys, nKeys, &azElem, &anElem, &nCount,
	            TH8_LIST_NO_CACHE) == TH8_OK) {
		for (j = 0; j < nCount; j++) {
		    if (sqlite3_reset(ctx->pUnset) != SQLITE_OK) {
			bWriteFailed = 1;
			break;
		    }
		    if (sqlite3_bind_text(
		            ctx->pUnset, 1, azElem[j], (int)anElem[j],
		            SQLITE_TRANSIENT) != SQLITE_OK) {
			bWriteFailed = 1;
			break;
		    }
		    sqlRc = sqlite3_step(ctx->pUnset);
		    sqlite3_clear_bindings(ctx->pUnset);
		    sqlite3_reset(ctx->pUnset);
		    if (sqlRc != SQLITE_DONE) {
			bWriteFailed = 1;
			break;
		    }
		}
		Th8_Free(interp, azElem);
		/* anElem is interior to the azElem block. */
	    } else {
		bWriteFailed = 1;
	    }
	    Th8_Free(interp, zKeys);
	}

	if (bWriteFailed) {
	    if (zDict) Th8_Free(interp, zDict);
	    goto kv_err;
	}

	if (sqlite3_exec(ctx->pDb, "COMMIT", 0, 0, 0) != SQLITE_OK) {
	    if (zDict) Th8_Free(interp, zDict);
	    goto kv_err;
	}
	bInTxn = 0;

	if (zDict) {
	    Th8_SetResult(interp, zDict, nDict);
	    Th8_Free(interp, zDict);
	} else {
	    Th8_ClearResult(interp);
	}
	rc = TH8_OK;
	break;
    }

    default:
	Th8_SetResultStatic(interp, "unknown kv operation", TH8_NOLEN);
	break;
    }

    ctx->pCurrentInterp = NULL;
    th8SqliteUnlock();
    return rc;

kv_err:
    /*
     * Capture the originating error message BEFORE attempting any
     * rollback: sqlite3_errmsg() reports the most recent error on
     * the connection, and a "ROLLBACK" issued when no transaction
     * is active will overwrite it with "cannot rollback - no
     * transaction is active", which is useless to the caller.
     */
    Th8_SetResult(interp, sqlite3_errmsg(ctx->pDb), TH8_NOLEN);

    /*
     * If a SET2/UNSET2 transaction is still open, the loop has
     * bailed out partway and the partial writes MUST be rolled
     * back so the KV store stays consistent.  sqlite3_exec(
     * "ROLLBACK") may itself return non-OK if SQLite already
     * auto-aborted (e.g. SQLITE_FULL / IOERR / NOMEM); the
     * caller-visible guarantee -- "no partial state survives" --
     * is met either way, so the return code is ignored.
     */
    if (bInTxn) {
	(void)sqlite3_exec(ctx->pDb, "ROLLBACK", 0, 0, 0);
	bInTxn = 0;
    }
    ctx->pCurrentInterp = NULL;
    th8SqliteUnlock();
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * th8SqliteCleanupCtx --
 *
 *	Finalize all cached statements, close the database, and
 *	free the context.
 *
 *----------------------------------------------------------------------
 */

static void
th8SqliteCleanupCtx(Th8_SQLiteKvCtx *ctx)
{
    if (!ctx) return;

    if (ctx->pDb) {
	sqlite3_stmt *pStmt;

	/*
	 * Finalize ALL statements on this connection,
	 * including any we did not cache.
	 */

	while ((pStmt = sqlite3_next_stmt(ctx->pDb, NULL)) != NULL) {
	    sqlite3_finalize(pStmt);
	}
	sqlite3_close_v2(ctx->pDb);
	ctx->pDb = NULL;
    }
    if (ctx->zDatabase) {
	free(ctx->zDatabase);
	ctx->zDatabase = NULL;
    }
    if (ctx->zTable) {
	free(ctx->zTable);
	ctx->zTable = NULL;
    }
    free(ctx);
}


/*
 *----------------------------------------------------------------------
 *
 * th8SqliteIntegrityCallback --
 *
 *	sqlite3_exec callback that validates the result rows of
 *	`PRAGMA integrity_check`.  The pragma returns the literal
 *	string "ok" as its only row when the database is intact,
 *	or one row per detected problem otherwise.  We require the
 *	exact single-row "ok" answer; anything else is treated as
 *	corruption and aborts the open.
 *
 * Why / How:
 *	`pCtx` is `int *piOk`.  Initialized to -1 by the caller so
 *	we can distinguish "no row produced" (still -1, treated as
 *	failure) from "produced an 'ok' row" (set to 1) and
 *	"produced any other row" (set to 0).  Returns 0 to keep
 *	exec going so all rows are observed.
 *
 *----------------------------------------------------------------------
 */

static int
th8SqliteIntegrityCallback(void *pCtx, int nCol, char **azVal, char **azCol)
{
    int *piOk = (int *)pCtx;

    (void)azCol;
    if (nCol >= 1 && azVal && azVal[0] && strcmp(azVal[0], "ok") == 0) {
	if (*piOk == -1) *piOk = 1;
    } else {
	*piOk = 0;
    }
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * th8SqliteBusyHandler --
 *
 *	sqlite3_busy_handler callback.  Called repeatedly when SQLite
 *	wants to retry a SQLITE_BUSY operation.  Returns non-zero to
 *	keep retrying, zero to abort the operation with SQLITE_BUSY.
 *
 * Why / How:
 *	A KV store backed by a single connection inside a single
 *	process generally never sees SQLITE_BUSY (the module-static
 *	mutex serializes us against ourselves).  The exceptions are:
 *	  - Another process / tool has the database file open.
 *	  - A future configuration enables WAL mode and a checkpoint
 *	    races with our access.
 *	  - A flaky network filesystem (NFS, SMB) returns spurious
 *	    contention on locking.
 *	In all of these cases we want to retry briefly with backoff
 *	rather than fail immediately, but we MUST honor cancellation:
 *	if the host interpreter has been canceled (Th8_Ready returns
 *	non-OK), we abandon the retry so a Ctrl-C reaches the script
 *	level promptly.  We cap total wait at ~1 second to keep
 *	failure modes bounded.
 *
 *	pCtx is the Th8_SQLiteKvCtx*; we read ctx->pCurrentInterp
 *	(set under th8SqliteLock by th8SqliteKeyValue and by the
 *	open path) to find the active interpreter.  During the
 *	open sequence pCurrentInterp is the interp passed to
 *	th8SqliteOpenAndPrepare; during normal operation it is the
 *	xKeyValue caller's interp.  If NULL (no operation in flight,
 *	which should not happen) we fall back to the timeout-only
 *	policy.
 *
 *----------------------------------------------------------------------
 */

static int
th8SqliteBusyHandler(void *pCtx, int nRetries)
{
    Th8_SQLiteKvCtx *ctx = (Th8_SQLiteKvCtx *)pCtx;
    Th8_Interp *interp;

    if (ctx) {
	interp = ctx->pCurrentInterp;
	if (interp && Th8_Ready(interp) != TH8_OK) {
	    return 0; /* canceled / step-limit / OOM -- give up */
	}
    }

    /*
     * Cap retries so any pathological contention fails in bounded
     * time.  ~50 retries x ~20ms = ~1 second worst case.
     */

    if (nRetries >= 50) {
	return 0;
    }

    /*
     * sqlite3_sleep is portable across all SQLite builds and is
     * safe to call from inside a busy handler.  It returns the
     * actual sleep granted (some platforms round up to ~1 ms).
     */

    sqlite3_sleep(20);
    return 1;
}


/*
 *----------------------------------------------------------------------
 *
 * th8SqliteAuthorizer --
 *
 *	sqlite3_set_authorizer callback.  Vets every operation the
 *	statement compiler is about to encode into a prepared
 *	statement.  Returns SQLITE_OK to allow, SQLITE_DENY to
 *	cause the prepare to fail with an authorization error, or
 *	SQLITE_IGNORE for column reads (treated as NULL).
 *
 * Why / How:
 *	After the open sequence completes, the only legitimate
 *	statements this connection prepares are the cached six
 *	(EXISTS / GET / SET / UNSET / LIST / GETALL) plus possibly
 *	transaction-control statements added in the future.  All
 *	of those operate on exactly one table -- ctx->zTable.  An
 *	authorizer that whitelists those operations on that table
 *	turns the connection into a single-purpose key-value
 *	endpoint: any attempt to ATTACH a second database, CREATE a
 *	trigger, ALTER the schema, or even read another table fails
 *	loudly during prepare.  This complements the
 *	SQLITE_DBCONFIG_DEFENSIVE flag (which prevents corruption-
 *	capable SQL) and the trusted_schema=OFF pragma (which blocks
 *	stored attacks via schema-attached functions).
 *
 *	Action codes that would arise during the open sequence
 *	(SQLITE_PRAGMA, SQLITE_CREATE_TABLE, SQLITE_ANALYZE,
 *	SQLITE_REINDEX) are not authorized here because the
 *	authorizer is installed AFTER the open sequence completes.
 *	By the time the first user statement is prepared, the
 *	allowed action set is final.
 *
 *----------------------------------------------------------------------
 */

static int
th8SqliteAuthorizer(
    void *pCtx,
    int iAction,
    const char *zArg1, /* table name, BEGIN/COMMIT label, etc. */
    const char *zArg2, /* column or function name (action-dependent) */
    const char *zArg3, /* database name (e.g. "main") */
    const char *zArg4) /* trigger / inner-context name */
{
    Th8_SQLiteKvCtx *ctx = (Th8_SQLiteKvCtx *)pCtx;

    (void)zArg2;
    (void)zArg3;
    (void)zArg4;

    switch (iAction) {

    case SQLITE_SELECT:
	/* Bare SELECT (no specific table) -- allowed; the per-row
	 * SQLITE_READ checks below enforce the table whitelist. */
	return SQLITE_OK;

    case SQLITE_TRANSACTION:
	/* BEGIN / COMMIT / ROLLBACK / SAVEPOINT-RELEASE.  Required
	 * for the SET2 and UNSET2 implementations, which wrap their
	 * scan + per-key write loop in a BEGIN IMMEDIATE / COMMIT
	 * pair so the bulk operation is atomic.  See th8SqliteKeyValue
	 * for details. */
	return SQLITE_OK;

    case SQLITE_FUNCTION:
	/* Built-in SQL function call (json_*, glob, like, hex,
	 * length, abs, length, etc.).  TH8 never registers user-
	 * defined SQL functions via sqlite3_create_function and
	 * load_extension is disabled via SQLITE_DBCONFIG_
	 * ENABLE_LOAD_EXTENSION, so the only functions reachable
	 * are SQLite built-ins, which are trustworthy.  The [json]
	 * command's ad-hoc SELECT json_valid(?) / json_extract(?,?)
	 * statements rely on this. */
	return SQLITE_OK;

    case SQLITE_READ:
	/* zArg1 is the table name.  Allowed: our KV table, plus
	 * the built-in JSON1 table-valued functions json_each /
	 * json_tree (used by the [json] command's iteration paths
	 * such as `SELECT key, value FROM json_each(?)`).  These
	 * are pure-function virtual tables provided by the JSON1
	 * module compiled into SQLite; they do not touch on-disk
	 * data and cannot read other tables. */
	if (zArg1 && ctx && ctx->zTable && strcmp(zArg1, ctx->zTable) == 0) {
	    return SQLITE_OK;
	}
	if (zArg1 && (strcmp(zArg1, "json_each") == 0 ||
	              strcmp(zArg1, "json_tree") == 0)) {
	    return SQLITE_OK;
	}
	return SQLITE_DENY;

    case SQLITE_INSERT:
    case SQLITE_UPDATE:
    case SQLITE_DELETE:
	/* Mutation: only our KV table is allowed. */
	if (zArg1 && ctx && ctx->zTable && strcmp(zArg1, ctx->zTable) == 0) {
	    return SQLITE_OK;
	}
	return SQLITE_DENY;

    default:
	/* Everything else is denied: ATTACH, DETACH, CREATE_*,
	 * DROP_*, ALTER_TABLE, CREATE_TRIGGER, CREATE_VIEW,
	 * CREATE_VTABLE, REINDEX, ANALYZE, PRAGMA, SAVEPOINT,
	 * RECURSIVE, COPY, etc.  This is the default-deny posture
	 * for a connection that has finished its one-time setup
	 * and should now do nothing but
	 * SELECT/INSERT/UPDATE/DELETE on the KV table (plus call
	 * built-in SQL functions on those rows). */
	return SQLITE_DENY;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Module-static array of SQL statements executed (in order) during
 * connection open, after sqlite3_open_v2 returns successfully.
 *
 * Ordering is security-first: any failure halts the open process and
 * the database is not used.  A failure earlier in the sequence costs
 * less than a failure later, so the most critical (and least
 * destructive) statements are run first.
 *
 *  Tier 1 -- Read-only verification.
 *    These pragmas perform diagnostic-only scans of the database;
 *    they NEVER mutate the file.  Failure here aborts the open
 *    before any later step can mutate on-disk state.
 *      - integrity_check: walks every B-tree page checking for
 *        structural damage; we require the literal single-row
 *        "ok" result.
 *      - foreign_key_check: verifies every foreign-key constraint
 *        is satisfied.  Our schema declares no FKs (so this is a
 *        no-op today), but it complements integrity_check at
 *        zero cost and remains correct if we ever add FKs.
 *
 *  Tier 2 -- Persistent on-disk metadata.
 *    These pragmas mutate the database file itself.  They run only
 *    after Tier 1 has confirmed the file is sound.  Each is
 *    idempotent on an already-configured database.
 *      - journal_mode = DELETE: classic rollback-journal mode.
 *        Single .sqlite file with no .wal/.shm companions; simpler
 *        and more predictable I/O footprint than WAL.  Persistent.
 *      - encoding = "UTF-8": fixes the on-disk encoding.  Effective
 *        only on a brand-new database; harmless no-op otherwise.
 *      - auto_vacuum = INCREMENTAL: enables incremental vacuum so
 *        deleted pages can be reclaimed without a full VACUUM.
 *        Effective only on a brand-new database (or after a full
 *        VACUUM).
 *
 *  Tier 3 -- Transient security / correctness settings.
 *    Per-connection settings that harden this connection.  None of
 *    them mutate the file; they take effect immediately and are
 *    inexpensive.
 *      - secure_delete = ON: overwrites freed pages with zeros so
 *        deleted KV values do not linger as recoverable bytes on
 *        disk.  Critical for a key-value store that may hold
 *        (master-encrypted) secret material.
 *      - trusted_schema = OFF: refuses to invoke schema-attached
 *        SQL functions or virtual tables that come from the
 *        database file itself, blocking a class of stored attacks.
 *      - cell_size_check = ON: validates B-tree cell sizes on
 *        every page access; catches in-memory corruption that
 *        slipped past integrity_check or arose during operation.
 *      - foreign_keys = ON: defense in depth even though our
 *        schema does not declare any.
 *      - defer_foreign_keys = ON: defers FK enforcement to commit
 *        time inside transactions; relevant only when FKs exist
 *        and a transaction is active, but harmless to set here.
 *      - case_sensitive_like = ON: forces LIKE to be case-
 *        sensitive (default is case-insensitive for ASCII).  Our
 *        cached statements use exact-match and GLOB, but this
 *        prevents any future LIKE query from acquiring surprise
 *        case-folding semantics.
 *      - writable_schema = OFF: explicit defense in depth (default
 *        but stated explicitly so it is impossible to silently
 *        flip via attached database or recovery script).
 *      - legacy_alter_table = OFF: explicit (default in modern
 *        SQLite); refuses the pre-3.25 in-place schema rewriting
 *        behavior of ALTER TABLE ... RENAME COLUMN.
 *      - read_uncommitted = OFF: explicit (default); blocks dirty
 *        reads from concurrent uncommitted transactions.
 *      - recursive_triggers = OFF: triggers are entirely
 *        disabled via SQLITE_DBCONFIG_ENABLE_TRIGGER, but stating
 *        the recursion control too is belt-and-suspenders.
 *      - full_column_names = ON: returns column names as
 *        "table.column" rather than the bare column name in
 *        result rows.
 *
 *  Tier 4 -- Transient performance / durability settings.
 *      - temp_store = MEMORY: keeps temporary tables and indices
 *        in RAM so transient KV operations never spill to disk.
 *      - mmap_size = 0: disables memory-mapped I/O.  All page
 *        access goes through ordinary read/write syscalls.
 *        Avoids leaking plaintext-bearing pages into the
 *        process's mmap region (visible to /proc/<pid>/maps and
 *        more easily seen by debuggers).
 *      - threads = 0: SQLite serializes externally via the
 *        module-static mutex; the SQLite worker-thread pool adds
 *        no concurrency benefit and increases footprint.
 *      - hard_heap_limit = 64 MB: caps SQLite's total memory use
 *        per process at 64 MiB.  Hard limit -- allocations beyond
 *        this fail with SQLITE_NOMEM.  Bounds DoS via runaway
 *        sort/cache growth.
 *      - synchronous = EXTRA: maximum durability.  Like FULL plus
 *        an extra fsync of the containing directory after journal
 *        deletion (DELETE mode) or after a checkpoint (WAL mode).
 *      - fullfsync = ON: macOS-only -- requests F_FULLFSYNC, which
 *        flushes the drive's hardware write cache rather than just
 *        the OS-level page cache.  No-op on platforms without
 *        F_FULLFSYNC support.
 *      - checkpoint_fullfsync = ON: like fullfsync but specifically
 *        for WAL checkpoints.  No-op in DELETE mode (no
 *        checkpoints exist), but harmless to set defensively in
 *        case the journal mode is ever changed.
 *
 *  Tier 5 -- Maintenance.
 *      - ANALYZE: updates sqlite_stat1/sqlite_stat4 so the query
 *        planner has fresh statistics.  Modest benefit on a
 *        PK-only schema today; cheap to run.
 *      - VACUUM: defragments and reclaims free space.  Runs after
 *        all settings are applied and before the schema is
 *        created so a previously over-sized file is shrunk before
 *        the application starts using it.  Cannot run inside a
 *        transaction; always safe at this point because nothing
 *        above this opens one.
 *      - PRAGMA optimize: SQLite's recommended "do whatever
 *        maintenance is currently needed" pragma.  Runs after
 *        VACUUM so its decisions reflect the post-VACUUM state.
 *
 *  Tier 6 -- Schema.
 *    The CREATE TABLE statement is last so that all hardening
 *    settings are in effect before any application table exists.
 *    On a fresh database this also means the new table inherits
 *    the chosen encoding, journal mode, and auto_vacuum mode.
 *----------------------------------------------------------------------
 */

typedef struct Th8_SqliteOpenStep {
    const char *zSql;  /* SQL; "%w" substituted with table name. */
    unsigned char
        wantsTable; /* 1 = format with sqlite3_snprintf("%w", ...) */
    unsigned char isIntegrity; /* 1 = use integrity-validating callback */
    const char *zStep;  /* short label for error messages */
} Th8_SqliteOpenStep;

static const Th8_SqliteOpenStep th8SqliteOpenSteps[] = {
    /* Tier 1: read-only verification. */
    {"PRAGMA integrity_check;", 0, 1, "integrity check"},
    {"PRAGMA foreign_key_check;", 0, 0, "foreign-key check"},

    /* Tier 2: persistent on-disk metadata. */
    {"PRAGMA journal_mode = DELETE;", 0, 0, "set DELETE journal mode"},
    {"PRAGMA encoding = 'UTF-8';", 0, 0, "set encoding to UTF-8"},
    {"PRAGMA auto_vacuum = INCREMENTAL;", 0, 0,
     "set auto_vacuum to INCREMENTAL"},

    /* Tier 3: transient security / correctness settings. */
    {"PRAGMA secure_delete = ON;", 0, 0, "enable secure_delete"},
    {"PRAGMA trusted_schema = OFF;", 0, 0, "disable trusted_schema"},
    {"PRAGMA cell_size_check = ON;", 0, 0, "enable cell_size_check"},
    {"PRAGMA foreign_keys = ON;", 0, 0, "enable foreign_keys"},
    {"PRAGMA defer_foreign_keys = ON;", 0, 0, "enable defer_foreign_keys"},
    {"PRAGMA case_sensitive_like = ON;", 0, 0, "enable case_sensitive_like"},
    {"PRAGMA writable_schema = OFF;", 0, 0, "disable writable_schema"},
    {"PRAGMA legacy_alter_table = OFF;", 0, 0, "disable legacy_alter_table"},
    {"PRAGMA read_uncommitted = OFF;", 0, 0, "disable read_uncommitted"},
    {"PRAGMA recursive_triggers = OFF;", 0, 0, "disable recursive_triggers"},
    {"PRAGMA full_column_names = ON;", 0, 0, "enable full_column_names"},

    /* Tier 4: transient performance / durability settings. */
    {"PRAGMA temp_store = MEMORY;", 0, 0, "set temp_store to MEMORY"},
    {"PRAGMA mmap_size = 0;", 0, 0, "disable memory-mapped I/O"},
    {"PRAGMA threads = 0;", 0, 0, "disable SQLite worker threads"},
    {"PRAGMA hard_heap_limit = 67108864;", 0, 0,
     "set hard heap limit (64 MB)"},
    {"PRAGMA synchronous = EXTRA;", 0, 0, "set synchronous to EXTRA"},
    {"PRAGMA fullfsync = ON;", 0, 0, "enable fullfsync"},
    {"PRAGMA checkpoint_fullfsync = ON;", 0, 0,
     "enable checkpoint_fullfsync"},

    /* Tier 5: maintenance. */
    {"ANALYZE;", 0, 0, "analyze database"},
    {"VACUUM;", 0, 0, "vacuum database"},
    {"PRAGMA optimize;", 0, 0, "run optimize pragma"},

    /* Tier 6: schema (LAST). */
    {"CREATE TABLE IF NOT EXISTS \"%w\" ("
     "name TEXT PRIMARY KEY NOT NULL, "
     "value TEXT NOT NULL"
     ");",
     1, 0, "create KV table"}};


/*
 *----------------------------------------------------------------------
 *
 * th8SqliteOpenAndPrepare --
 *
 *	Open the database, run the security-ordered open sequence
 *	(integrity check, persistent settings, transient settings,
 *	vacuum, schema), and prepare all cached statements.
 *	Returns TH8_OK or TH8_ERROR.
 *
 *----------------------------------------------------------------------
 */

static int
th8SqliteOpenAndPrepare(Th8_Interp *interp, Th8_SQLiteKvCtx *ctx)
{
    int sqlRc;
    char *zErr = NULL;
    char zSql[512] = {0};
    int iStep;
    int nSteps;

    /*
     * Open flags chosen for safety:
     *   SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE -- standard
     *      read/write with create-on-missing semantics.
     *   SQLITE_OPEN_NOFOLLOW -- refuse to open the database via a
     *      symbolic link.  Mitigates symlink-based redirection
     *      attacks where an attacker who can place a symlink in
     *      the database path tricks the open into pointing at a
     *      file under their control.  Available in SQLite 3.31+.
     */

    sqlRc = sqlite3_open_v2(
        ctx->zDatabase, &ctx->pDb,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOFOLLOW,
        NULL);
    if (sqlRc != SQLITE_OK) {
	Th8_SetResultStatic(interp, "kv: cannot open database", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Install the cancellation-aware busy handler immediately so
     * even the open-time PRAGMAs and CREATE TABLE benefit from it.
     * The handler reads ctx->pCurrentInterp to detect Th8 cancel;
     * during the open path the relevant interp is the one passed
     * in here, so we set it now and clear it before returning.
     */

    ctx->pCurrentInterp = interp;
    sqlRc = sqlite3_busy_handler(ctx->pDb, th8SqliteBusyHandler, ctx);
    if (sqlRc != SQLITE_OK) {
	Th8_SetResultStatic(
	    interp, "kv: cannot install busy handler", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Connection-level hardening via sqlite3_db_config.  These run
     * before any pragma so that the entire pragma sequence below
     * is itself subject to the defensive constraints.  Each option
     * is set to the most restrictive value compatible with our
     * key-value workload (no triggers, no views, no extensions, no
     * dangerous SQL, no double-quoted-string misfeature).
     */

    {
	static const struct {
	    int op;
	    int value;
	    const char *zStep;
	} aDbConfig[] =
	    {     /* Refuse SQL that can corrupt the file (writes to
	     * sqlite_master, PRAGMA journal_mode=OFF, shadow-table
	     * writes, etc.).  Highest-impact single hardening. */
	     {SQLITE_DBCONFIG_DEFENSIVE, 1, "DEFENSIVE"},

	     /* Disable the double-quoted-string-as-literal misfeature
	     * in DDL and DML.  Modern SQL semantics: double quotes
	     * are identifier delimiters, period. */
	     {SQLITE_DBCONFIG_DQS_DDL, 0, "DQS_DDL"},
	     {SQLITE_DBCONFIG_DQS_DML, 0, "DQS_DML"},

	     /* Block load_extension() at the C-API level even if the
	     * SQLite build accidentally included extension support. */
	     {SQLITE_DBCONFIG_ENABLE_LOAD_EXTENSION, 0,
	      "ENABLE_LOAD_EXTENSION"},

	     /* Our schema has no triggers and no views; refuse them
	     * entirely so that a future schema-injection attempt
	     * cannot create them either. */
	     {SQLITE_DBCONFIG_ENABLE_TRIGGER, 0, "ENABLE_TRIGGER"},
	     {SQLITE_DBCONFIG_ENABLE_VIEW, 0, "ENABLE_VIEW"}};
	int i;
	int nDbConfig = (int)(sizeof(aDbConfig) / sizeof(aDbConfig[0]));

	for (i = 0; i < nDbConfig; i++) {
	    sqlRc = sqlite3_db_config(
	        ctx->pDb, aDbConfig[i].op, aDbConfig[i].value, NULL);
	    if (sqlRc != SQLITE_OK) {
		Th8_ErrorMessage(
		    interp, "kv: db_config failed:", aDbConfig[i].zStep,
		    TH8_NOLEN);
		return TH8_ERROR;
	    }
	}
    }

    /*
     * Walk the th8SqliteOpenSteps array.  Any failure aborts the
     * open with an error result naming the step that failed.  The
     * security-first ordering is documented at the array
     * definition above.
     */

    nSteps = (int)(sizeof(th8SqliteOpenSteps) /
                   sizeof(th8SqliteOpenSteps[0]));
    for (iStep = 0; iStep < nSteps; iStep++) {
	const Th8_SqliteOpenStep *pStep = &th8SqliteOpenSteps[iStep];
	const char *zExec;

	if (pStep->wantsTable) {
	    sqlite3_snprintf(sizeof(zSql), zSql, pStep->zSql, ctx->zTable);
	    zExec = zSql;
	} else {
	    zExec = pStep->zSql;
	}

	if (pStep->isIntegrity) {
	    int iOk = -1;
	    sqlRc = sqlite3_exec(
	        ctx->pDb, zExec, th8SqliteIntegrityCallback, &iOk, &zErr);
	    if (sqlRc == SQLITE_OK && iOk != 1) {
		sqlRc = SQLITE_CORRUPT;
	    }
	} else {
	    sqlRc = sqlite3_exec(ctx->pDb, zExec, NULL, NULL, &zErr);
	}

	if (sqlRc != SQLITE_OK) {
	    if (zErr) sqlite3_free(zErr);
	    Th8_ErrorMessage(
	        interp, "kv: open step failed:", pStep->zStep, TH8_NOLEN);
	    return TH8_ERROR;
	}
    }

    /*
     * The open sequence (PRAGMAs, ANALYZE, VACUUM, optimize,
     * CREATE TABLE) has completed.  From this point on every SQL
     * statement on this connection is vetted by the authorizer:
     * only SELECT/INSERT/UPDATE/DELETE on ctx->zTable plus
     * transaction control are allowed.  Anything else -- ATTACH,
     * additional PRAGMAs, schema modification, function calls --
     * fails at prepare time with SQLITE_AUTH.
     *
     * Installed here (after the open SQL loop, before our cached
     * statements are prepared) so that:
     *   - The open-time statements are NOT subject to the
     *     authorizer (they would otherwise fail because PRAGMA
     *     and CREATE TABLE are denied).
     *   - The cached statements ARE subject to the authorizer
     *     (they pass because they operate on ctx->zTable only).
     */

    sqlRc = sqlite3_set_authorizer(ctx->pDb, th8SqliteAuthorizer, ctx);
    if (sqlRc != SQLITE_OK) {
	Th8_SetResultStatic(
	    interp, "kv: cannot install authorizer", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Prepare cached statements.
     */

    if (th8SqlitePrepare(
            ctx->pDb, "SELECT 1 FROM \"%w\" WHERE name=?", ctx->zTable,
            &ctx->pExists) != SQLITE_OK) {
	return TH8_ERROR;
    }
    if (th8SqlitePrepare(
            ctx->pDb, "SELECT value FROM \"%w\" WHERE name=?", ctx->zTable,
            &ctx->pGet) != SQLITE_OK) {
	return TH8_ERROR;
    }
    if (th8SqlitePrepare(
            ctx->pDb,
            "INSERT OR REPLACE INTO \"%w\" (name, value) VALUES (?, ?)",
            ctx->zTable, &ctx->pSet) != SQLITE_OK) {
	return TH8_ERROR;
    }
    if (th8SqlitePrepare(
            ctx->pDb, "DELETE FROM \"%w\" WHERE name=?", ctx->zTable,
            &ctx->pUnset) != SQLITE_OK) {
	return TH8_ERROR;
    }
    if (th8SqlitePrepare(
            ctx->pDb, "SELECT name FROM \"%w\"", ctx->zTable,
            &ctx->pListAll) != SQLITE_OK) {
	return TH8_ERROR;
    }
    if (th8SqlitePrepare(
            ctx->pDb, "SELECT name, value FROM \"%w\"", ctx->zTable,
            &ctx->pGetAll) != SQLITE_OK) {
	return TH8_ERROR;
    }

    /*
     * Clear the current-interp pointer.  Open is done; no operation
     * is in flight.  xKeyValue will set it again on each call.
     * (On any earlier error path the caller invokes
     * th8SqliteCleanupCtx, which destroys the entire context, so
     * we do not need to clear pCurrentInterp on those paths.)
     */

    ctx->pCurrentInterp = NULL;
    return TH8_OK;
}


/*
 *======================================================================
 * Platform struct.
 *======================================================================
 */

static Th8_Platform th8SqlitePlatformData = {
    1, /* nVersion */
    0,
    0,
    0,
    0, /* xInitialize, xFinalize, xPreDeleteInterp, xDeleteInterp */
    0,
    0,
    0,
    0, /* xMalloc, xRealloc, xFree, xMemorySize */
    0, /* xNeedMemory */
    0,
    0,
    0,
    0, /* xMemcpy, xMemmove, xMemcmp, xMemset */
    0,
    0,
    0,
    0,
    0,
    0, /* xStrlen .. xVsnprintf */
    0,
    0,
    0,
    0,
    0,
    0, /* xMutexNew .. xMemBarrier */
    0,
    0,
    0,
    0,
    0, /* xEventCreate, xEventDestroy, xEventSet,
			   xEventReset, xEventWait */
    0,
    0,
    0, /* xInput, xOutput, xOutputError */
    0,
    0,
    0,
    0,
    0,
    0, /* channel redirection */
    0,
    0,
    0,
    0,
    0, /* channel control / temp data */
    0,
    0,
    0,
    0, /* path ops */
    0,
    0, /* xGetRealPath, xGetRootPath */
    0, /* xSameFile */
    0,
    0, /* xGetData, xDataExists */
    0,
    0, /* xLoad, xUnload */
    0,
    0,
    0, /* xTimeMs, xTimeUs, xSleep */
    0, /* xGetPid */
    0,
    0, /* xGetUserName, xGetHostName */
    0, /* xGetEnv */
    th8SqliteKeyValue, /* xKeyValue */
    0, /* xGetStackBounds */
    0,
    0, /* xGetParentPid, xGetThreadId */
    0,
    0, /* xGetLastError, xSetLastError */
    0, /* xEmitTrace */
    0, /* xPanic */
    0, /* xMathFunc */
    0, /* xRandomBytes */
    0,
    0, /* xDnsResolve, xDnsResolveFree */
    0, /* xStackBackTrace */
    0, /* xIntCmpXchg64 */
    0 /* pCtx -- set dynamically in _Init */
};


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetSQLitePlatform --
 *
 *	Return the SQLite-backed platform layer.
 *	The pCtx field points to the module-static Th8_SQLiteKvCtx.
 *
 *----------------------------------------------------------------------
 */

TH8_SQLITE3_EXPORT const Th8_Platform *
Th8_GetSQLitePlatform(void)
{
    return &th8SqlitePlatformData;
}


/*
 *======================================================================
 * JSON command (wraps SQLite JSON functions).
 *======================================================================
 */


/*
 *----------------------------------------------------------------------
 *
 * th8SqliteJsonExec --
 *
 *	Execute a SQL statement that returns a single text value and
 *	set the interpreter result.  Returns TH8_OK or TH8_ERROR.
 *
 *----------------------------------------------------------------------
 */

static int
th8SqliteJsonExec(
    Th8_Interp *interp,
    sqlite3 *pDb,
    const char *zSql,
    int nBind,
    const char **azBind,
    const size_t *anBind)
{
    sqlite3_stmt *pStmt = NULL;
    int sqlRc;
    int i;

    sqlRc = sqlite3_prepare_v2(pDb, zSql, -1, &pStmt, 0);
    if (sqlRc != SQLITE_OK || !pStmt) {
	Th8_SetResult(interp, sqlite3_errmsg(pDb), TH8_NOLEN);
	return TH8_ERROR;
    }

    for (i = 0; i < nBind; i++) {
	sqlRc = sqlite3_bind_text(
	    pStmt, i + 1, azBind[i], (int)anBind[i], SQLITE_TRANSIENT);
	if (sqlRc != SQLITE_OK) {
	    Th8_SetResult(interp, sqlite3_errmsg(pDb), TH8_NOLEN);
	    sqlite3_finalize(pStmt);
	    return TH8_ERROR;
	}
    }

    sqlRc = sqlite3_step(pStmt);
    if (sqlRc == SQLITE_ROW) {
	const char *zVal = (const char *)sqlite3_column_text(pStmt, 0);
	int nVal = sqlite3_column_bytes(pStmt, 0);

	if (zVal) {
	    Th8_SetResult(interp, zVal, (size_t)nVal);
	} else {
	    Th8_ClearResult(interp);
	}
    } else if (sqlRc == SQLITE_DONE) {
	Th8_ClearResult(interp);
    } else {
	Th8_SetResult(interp, sqlite3_errmsg(pDb), TH8_NOLEN);
	sqlite3_finalize(pStmt);
	return TH8_ERROR;
    }

    sqlite3_finalize(pStmt);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8SqliteJsonMultiRow --
 *
 *	Execute a SQL statement that returns multiple rows with a
 *	single text column.  Build a Tcl list of the results.
 *
 *----------------------------------------------------------------------
 */

static int
th8SqliteJsonMultiRow(
    Th8_Interp *interp,
    sqlite3 *pDb,
    const char *zSql,
    const char *zJson,
    size_t nJson,
    const char *zPath,
    size_t nPath)
{
    sqlite3_stmt *pStmt = NULL;
    char *zList = NULL;
    size_t nList = 0;
    int sqlRc;

    sqlRc = sqlite3_prepare_v2(pDb, zSql, -1, &pStmt, 0);
    if (sqlRc != SQLITE_OK || !pStmt) {
	Th8_SetResult(interp, sqlite3_errmsg(pDb), TH8_NOLEN);
	return TH8_ERROR;
    }

    sqlRc = sqlite3_bind_text(pStmt, 1, zJson, (int)nJson, SQLITE_TRANSIENT);
    if (sqlRc != SQLITE_OK) goto json_multi_err;
    if (zPath) {
	sqlRc =
	    sqlite3_bind_text(pStmt, 2, zPath, (int)nPath, SQLITE_TRANSIENT);
	if (sqlRc != SQLITE_OK) goto json_multi_err;
    }

    while ((sqlRc = sqlite3_step(pStmt)) == SQLITE_ROW) {
	const char *zVal = (const char *)sqlite3_column_text(pStmt, 0);
	int nVal = sqlite3_column_bytes(pStmt, 0);

	if (zVal) {
	    Th8_ListAppend(interp, &zList, &nList, zVal, (size_t)nVal);
	}
    }

    if (sqlRc != SQLITE_DONE) goto json_multi_err;

    sqlite3_finalize(pStmt);

    if (zList) {
	Th8_SetResult(interp, zList, nList);
	Th8_Free(interp, zList);
    } else {
	Th8_ClearResult(interp);
    }
    return TH8_OK;

json_multi_err:
    Th8_SetResult(interp, sqlite3_errmsg(pDb), TH8_NOLEN);
    sqlite3_finalize(pStmt);
    if (zList) Th8_Free(interp, zList);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * th8SqliteJsonBuildSql --
 *
 *	Build a SQL statement for variadic JSON functions like
 *	json_set, json_insert, json_replace, json_remove.
 *	Returns an allocated SQL string or NULL on error.
 *
 *	For path+value functions (json_set, json_insert, json_replace):
 *	  SELECT func(?, ?, ?, ?, ?, ...)
 *	  Binds: json, path1, val1, path2, val2, ...
 *
 *	For path-only functions (json_remove):
 *	  SELECT func(?, ?, ?, ...)
 *	  Binds: json, path1, path2, ...
 *
 *----------------------------------------------------------------------
 */

static char *
th8SqliteJsonBuildSql(
    Th8_Interp *interp,
    const char *zFunc,
    int nParams) /* Total bind parameters including json. */
{
    char *zSql;
    size_t nSql = 0;
    int i;

    /* SELECT func( + nParams * ", ?" + ) */
    zSql = (char *)
        TH8_ALLOC_MUL_ADD(interp, (size_t)nParams, 4, strlen(zFunc) + 20);
    if (!zSql) return NULL;

    nSql = 0;
    nSql += strlen(zFunc) + 8;
    memcpy(zSql, "SELECT ", 7);
    memcpy(zSql + 7, zFunc, strlen(zFunc));
    nSql = 7 + strlen(zFunc);
    zSql[nSql++] = '(';
    for (i = 0; i < nParams; i++) {
	if (i > 0) {
	    zSql[nSql++] = ',';
	}
	zSql[nSql++] = '?';
    }
    zSql[nSql++] = ')';
    zSql[nSql] = '\0';

    return zSql;
}


/*
 *----------------------------------------------------------------------
 *
 * th8SqliteJsonCmd --
 *
 *	json subcommand ...
 *
 *	Dispatch JSON operations via SQLite's built-in JSON functions.
 *
 *----------------------------------------------------------------------
 */

static int
th8SqliteJsonCmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    Th8_SQLiteKvCtx *kvCtx = th8SqliteCtx;
    sqlite3 *pDb;

    (void)ctx;

    if (argc < 2) {
	return Th8_WrongNumArgs(interp, "json subcommand ?args?");
    }

    if (!kvCtx || !kvCtx->pDb) {
	Th8_SetResultStatic(
	    interp, "json: no database connection", TH8_NOLEN);
	return TH8_ERROR;
    }
    pDb = kvCtx->pDb;

    /*
     * json valid jsonText
     */

    if (argl[1] == 5 && memcmp(argv[1], "valid", 5) == 0) {
	int sqlRc;
	sqlite3_stmt *pStmt = NULL;

	if (argc != 3) {
	    return Th8_WrongNumArgs(interp, "json valid jsonText");
	}
	sqlRc =
	    sqlite3_prepare_v2(pDb, "SELECT json_valid(?)", -1, &pStmt, 0);
	if (sqlRc != SQLITE_OK) goto sql_err;
	sqlRc = sqlite3_bind_text(
	    pStmt, 1, argv[2], (int)argl[2], SQLITE_TRANSIENT);
	if (sqlRc != SQLITE_OK) {
	    sqlite3_finalize(pStmt);
	    goto sql_err;
	}
	sqlRc = sqlite3_step(pStmt);
	if (sqlRc == SQLITE_ROW) {
	    Th8_SetResultInt(interp, sqlite3_column_int(pStmt, 0));
	} else if (sqlRc != SQLITE_DONE) {
	    sqlite3_finalize(pStmt);
	    goto sql_err;
	}
	sqlite3_finalize(pStmt);
	return TH8_OK;
    }

    /*
     * json type jsonText ?path?
     */

    if (argl[1] == 4 && memcmp(argv[1], "type", 4) == 0) {
	if (argc < 3 || argc > 4) {
	    return Th8_WrongNumArgs(interp, "json type jsonText ?path?");
	}
	if (argc == 3) {
	    return th8SqliteJsonExec(
	        interp, pDb, "SELECT json_type(?)", 1, &argv[2], &argl[2]);
	}
	return th8SqliteJsonExec(
	    interp, pDb, "SELECT json_type(?,?)", 2, &argv[2], &argl[2]);
    }

    /*
     * json extract jsonText path ?path...?
     */

    if (argl[1] == 7 && memcmp(argv[1], "extract", 7) == 0) {
	char *zSql;
	int rc;

	if (argc < 4) {
	    return Th8_WrongNumArgs(
	        interp, "json extract jsonText path ?path...?");
	}
	zSql = th8SqliteJsonBuildSql(interp, "json_extract", argc - 2);
	if (!zSql) return TH8_ERROR;
	rc = th8SqliteJsonExec(
	    interp, pDb, zSql, argc - 2, &argv[2], &argl[2]);
	Th8_Free(interp, zSql);
	return rc;
    }

    /*
     * json set / insert / replace: jsonText path value ?path value...?
     */

    if ((argl[1] == 3 && memcmp(argv[1], "set", 3) == 0) ||
        (argl[1] == 6 && memcmp(argv[1], "insert", 6) == 0) ||
        (argl[1] == 7 && memcmp(argv[1], "replace", 7) == 0)) {
	const char *zFunc;
	char *zSql;
	int rc;

	if (argc < 5 || (argc - 3) % 2 != 0) {
	    return Th8_WrongNumArgs(
	        interp, "json set|insert|replace jsonText "
	                "path value ?path value...?");
	}
	if (argl[1] == 3)
	    zFunc = "json_set";
	else if (argl[1] == 6)
	    zFunc = "json_insert";
	else
	    zFunc = "json_replace";

	zSql = th8SqliteJsonBuildSql(interp, zFunc, argc - 2);
	if (!zSql) return TH8_ERROR;
	rc = th8SqliteJsonExec(
	    interp, pDb, zSql, argc - 2, &argv[2], &argl[2]);
	Th8_Free(interp, zSql);
	return rc;
    }

    /*
     * json remove jsonText path ?path...?
     */

    if (argl[1] == 6 && memcmp(argv[1], "remove", 6) == 0) {
	char *zSql;
	int rc;

	if (argc < 4) {
	    return Th8_WrongNumArgs(
	        interp, "json remove jsonText path ?path...?");
	}
	zSql = th8SqliteJsonBuildSql(interp, "json_remove", argc - 2);
	if (!zSql) return TH8_ERROR;
	rc = th8SqliteJsonExec(
	    interp, pDb, zSql, argc - 2, &argv[2], &argl[2]);
	Th8_Free(interp, zSql);
	return rc;
    }

    /*
     * json patch targetJson patchJson
     */

    if (argl[1] == 5 && memcmp(argv[1], "patch", 5) == 0) {
	if (argc != 4) {
	    return Th8_WrongNumArgs(
	        interp, "json patch targetJson patchJson");
	}
	return th8SqliteJsonExec(
	    interp, pDb, "SELECT json_patch(?,?)", 2, &argv[2], &argl[2]);
    }

    /*
     * json array ?value...?
     */

    if (argl[1] == 5 && memcmp(argv[1], "array", 5) == 0) {
	char *zSql;
	int rc;

	if (argc == 2) {
	    Th8_SetResultStatic(interp, "[]", 2);
	    return TH8_OK;
	}
	zSql = th8SqliteJsonBuildSql(interp, "json_array", argc - 2);
	if (!zSql) return TH8_ERROR;
	rc = th8SqliteJsonExec(
	    interp, pDb, zSql, argc - 2, &argv[2], &argl[2]);
	Th8_Free(interp, zSql);
	return rc;
    }

    /*
     * json object ?key value...?
     */

    if (argl[1] == 6 && memcmp(argv[1], "object", 6) == 0) {
	char *zSql;
	int rc;

	if (argc == 2) {
	    Th8_SetResultStatic(interp, "{}", 2);
	    return TH8_OK;
	}
	if ((argc - 2) % 2 != 0) {
	    return Th8_WrongNumArgs(interp, "json object ?key value...?");
	}
	zSql = th8SqliteJsonBuildSql(interp, "json_object", argc - 2);
	if (!zSql) return TH8_ERROR;
	rc = th8SqliteJsonExec(
	    interp, pDb, zSql, argc - 2, &argv[2], &argl[2]);
	Th8_Free(interp, zSql);
	return rc;
    }

    /*
     * json quote value
     */

    if (argl[1] == 5 && memcmp(argv[1], "quote", 5) == 0) {
	if (argc != 3) {
	    return Th8_WrongNumArgs(interp, "json quote value");
	}
	return th8SqliteJsonExec(
	    interp, pDb, "SELECT json_quote(?)", 1, &argv[2], &argl[2]);
    }

    /*
     * json length jsonText ?path?
     */

    if (argl[1] == 6 && memcmp(argv[1], "length", 6) == 0) {
	if (argc < 3 || argc > 4) {
	    return Th8_WrongNumArgs(interp, "json length jsonText ?path?");
	}
	if (argc == 3) {
	    return th8SqliteJsonExec(
	        interp, pDb, "SELECT json_array_length(?)", 1, &argv[2],
	        &argl[2]);
	}
	return th8SqliteJsonExec(
	    interp, pDb, "SELECT json_array_length(?,?)", 2, &argv[2],
	    &argl[2]);
    }

    /*
     * json error jsonText
     */

    if (argl[1] == 5 && memcmp(argv[1], "error", 5) == 0) {
	if (argc != 3) {
	    return Th8_WrongNumArgs(interp, "json error jsonText");
	}
	return th8SqliteJsonExec(
	    interp, pDb, "SELECT json_error_position(?)", 1, &argv[2],
	    &argl[2]);
    }

    /*
     * json pretty jsonText
     */

    if (argl[1] == 6 && memcmp(argv[1], "pretty", 6) == 0) {
	if (argc != 3) {
	    return Th8_WrongNumArgs(interp, "json pretty jsonText");
	}
	return th8SqliteJsonExec(
	    interp, pDb, "SELECT json_pretty(?)", 1, &argv[2], &argl[2]);
    }

    /*
     * json keys jsonText ?path?
     */

    if (argl[1] == 4 && memcmp(argv[1], "keys", 4) == 0) {
	if (argc < 3 || argc > 4) {
	    return Th8_WrongNumArgs(interp, "json keys jsonText ?path?");
	}
	if (argc == 3) {
	    return th8SqliteJsonMultiRow(
	        interp, pDb, "SELECT key FROM json_each(?)", argv[2], argl[2],
	        NULL, 0);
	}
	return th8SqliteJsonMultiRow(
	    interp, pDb, "SELECT key FROM json_each(?,?)", argv[2], argl[2],
	    argv[3], argl[3]);
    }

    /*
     * json values jsonText ?path?
     */

    if (argl[1] == 6 && memcmp(argv[1], "values", 6) == 0) {
	if (argc < 3 || argc > 4) {
	    return Th8_WrongNumArgs(interp, "json values jsonText ?path?");
	}
	if (argc == 3) {
	    return th8SqliteJsonMultiRow(
	        interp, pDb, "SELECT value FROM json_each(?)", argv[2],
	        argl[2], NULL, 0);
	}
	return th8SqliteJsonMultiRow(
	    interp, pDb, "SELECT value FROM json_each(?,?)", argv[2], argl[2],
	    argv[3], argl[3]);
    }

    Th8_SetResultStatic(interp, "json: unknown subcommand", TH8_NOLEN);
    return TH8_ERROR;

sql_err:
    Th8_SetResult(interp, sqlite3_errmsg(pDb), TH8_NOLEN);
    return TH8_ERROR;
}


/*
 *======================================================================
 * Extension entry points.
 *======================================================================
 */


/*
 *----------------------------------------------------------------------
 *
 * Th8sqlite3_Init --
 *
 *	Extension initialization.  Called by [load].
 *	Opens the SQLite database, creates the KV table,
 *	prepares cached statements, and merges the platform.
 *
 *----------------------------------------------------------------------
 */

TH8_SQLITE3_EXPORT int
Th8sqlite3_Init(Th8_Interp *interp)
{
    Th8_SQLiteKvCtx *ctx;
    char *zDbPath;

#ifdef USE_TH8_STUBS
    if (Th8_InitStubs(interp, "1.0", 0) == 0) {
	return TH8_ERROR;
    }
#endif

    if (sqlite3_initialize() != SQLITE_OK) {
	Th8_SetResultStatic(
	    interp, "kv: sqlite3_initialize failed", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Allocate and populate the context.
     * Default database: ":memory:" (or TH8_SQLITE_DB env var).
     * Default table: "kv".
     */

    ctx = (Th8_SQLiteKvCtx *)calloc(1, sizeof(Th8_SQLiteKvCtx));
    if (!ctx) {
	Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	return TH8_ERROR;
    }

    zDbPath = Th8_GetEnv(interp, "TH8_SQLITE_DB");
    if (zDbPath) {
	ctx->zDatabase = zDbPath; /* Th8_GetEnv returns owned */
	ctx->nDatabase = strlen(zDbPath);
    } else {
	ctx->zDatabase = (char *)malloc(9);
	if (!ctx->zDatabase) {
	    free(ctx);
	    Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	    return TH8_ERROR;
	}
	memcpy(ctx->zDatabase, ":memory:", 9);
	ctx->nDatabase = 8;
    }

    ctx->zTable = (char *)malloc(3);
    if (!ctx->zTable) {
	free(ctx->zDatabase);
	free(ctx);
	Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	return TH8_ERROR;
    }
    memcpy(ctx->zTable, "kv", 3);
    ctx->nTable = 2;

    /*
     * Open database and prepare statements.
     */

    if (th8SqliteOpenAndPrepare(interp, ctx) != TH8_OK) {
	th8SqliteCleanupCtx(ctx);
	return TH8_ERROR;
    }

    /*
     * Set the platform context and merge into the interpreter.
     */

    th8SqliteCtx = ctx;

    if (Th8_MergePlatformInterp(interp, &th8SqlitePlatformData) != TH8_OK) {
	th8SqliteCleanupCtx(ctx);
	th8SqliteCtx = NULL;
	Th8_SetResultStatic(interp, "kv: platform merge failed", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Register a per-callback context so the xKeyValue callback
     * receives the SQLite context instead of the platform's
     * default pCtx.
     */

    if (Th8_SetPlatformContext(
            interp, (Th8_PlatformFunc)th8SqliteKeyValue, ctx) != TH8_OK) {
	th8SqliteCleanupCtx(ctx);
	th8SqliteCtx = NULL;
	Th8_SetResultStatic(
	    interp, "kv: cannot set callback context", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Register the json command.
     */

    Th8_CreateCommand(interp, "json", th8SqliteJsonCmd, 0, 0, 0);

    Th8_Eval(interp, 0, "package provide th8sqlite3 1.0", TH8_NOLEN, NULL, 0);

    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8sqlite3_Unload --
 *
 *	Extension cleanup.  Finalizes all statements, closes all
 *	connections, and shuts down SQLite.
 *
 *----------------------------------------------------------------------
 */

TH8_SQLITE3_EXPORT int
Th8sqlite3_Unload(Th8_Interp *interp, int flags)
{
    (void)interp;
    (void)flags;

    th8SqliteLock();
    if (th8SqliteCtx) {
	th8SqliteCleanupCtx(th8SqliteCtx);
	th8SqliteCtx = NULL;
    }
    th8SqliteUnlock();

    sqlite3_shutdown();

    Th8_Eval(
        interp, 0, "catch {package forget th8sqlite3}", TH8_NOLEN, NULL, 0);

    return TH8_OK;
}
