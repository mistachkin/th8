/*
 * th8_vars.h --
 *
 *	Internal header for the variables subsystem.  Houses
 *	non-function declarations (structs, internal accessors,
 *	design rationale) for code shared between th8_vars.c
 *	(core variable storage), th8_core.c (interp lifecycle),
 *	and src/plugins/th8_variables.c (script-visible commands
 *	`set`, `unset`, `array`, etc.).  This file is NOT part
 *	of the public API.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef TH8_VARS_H
#define TH8_VARS_H

/*
 *======================================================================
 *
 * Array search-iteration registry: startsearch / nextelement /
 * anymore / donesearch.
 *
 * The Tcl-level surface is:
 *
 *   set sid [array startsearch arrayName]
 *   while {[array anymore arrayName $sid]} {
 *	 set name [array nextelement arrayName $sid]
 *	 ...
 *   }
 *   array donesearch arrayName $sid
 *
 * Implementation:
 *	Per-startsearch state is held in a per-interp hash keyed
 *	by the search-id string (e.g. "s-42-arrayName") and stored
 *	on the Th8_Interp via th8GetArraySearchHash.  The hash is
 *	lazily allocated on first startsearch.  Each entry's pData
 *	points to a th8ArraySearch struct.
 *
 *	Iteration is *live*, not snapshot-based: the search holds
 *	a borrowed pointer into the array's element hash plus a
 *	bucket+entry cursor.  The whole point of the search API
 *	(vs `[array names]`) is to avoid materializing a key list
 *	for very large arrays; snapshotting would defeat that.
 *
 *	Mutation safety is enforced via a per-array epoch counter
 *	(Th8_Variable.nEpoch) bumped on every element-level write
 *	(set / unset / append / lappend / incr / array set).  The
 *	search captures the epoch at startsearch and re-checks on
 *	every nextelement / anymore / donesearch; mismatch raises
 *	an error.  Full-array unset is detected via element-hash
 *	identity (the saved hash pointer no longer matches the
 *	live one).
 *
 *	Per-interp storage means an interp's searches cannot be
 *	driven by another interp, even within the same process.
 *	The find function additionally checks pInterp identity
 *	and the array-name argument as defense in depth.
 *
 *	Lifetimes: a search not closed by donesearch is freed
 *	when the interp itself is destroyed (via
 *	th8ArraySearchFreeEntry, called from the Th8_Interp
 *	teardown path).  Tests can enumerate pending searches
 *	via the public Th8_IterateArraySearches API (and the
 *	test-only [::th8testlib::array_searches] command that
 *	wraps it) for leak detection.
 *
 *======================================================================
 */

/*
 * Per-search record stored in the array search hash.  Code
 * outside th8_variables.c MUST treat this as opaque even
 * though the struct is visible here for the sake of
 * th8ArraySearchFreeEntry / iteration callbacks in th8_core.c.
 */
typedef struct th8ArraySearch {
    Th8_Interp *pInterp; /* Owning interp (defense in depth). */
    char *zArray;  /* Owning array name (own copy). */
    size_t nArray;
    Th8_Hash *pArrayHash; /* Borrowed: array's element hash. */
    int nEpoch;   /* Captured at startsearch. */
    int nGeneration;  /* Captured at startsearch: the element-hash's
				 * allocation generation.  Mismatched against
				 * the array's current Th8_Variable.nGeneration
				 * to detect unset-and-recreate scenarios that
				 * a pointer-equality check would miss when the
				 * allocator reuses the freed pHash address. */
    int iBucket;  /* Cursor: bucket index in pArrayHash. */
    Th8_HashEntry *pCursor; /* Cursor: entry within bucket, or NULL. */
} th8ArraySearch;

/*
 * Internal accessors.  Defined in th8_core.c (search-hash and
 * counter), th8_vars.c (per-array epoch and element-hash
 * lookup), and src/plugins/th8_variables.c (free-entry
 * callback used during interp teardown).
 *
 * th8GetArraySearchHash:
 *	Per-interp search hash.  bCreate=1 lazy-allocates;
 *	bCreate=0 returns NULL when the hash hasn't been used
 *	yet.
 *
 * th8NextArraySearchId:
 *	Monotonic per-interp counter for unique search-id
 *	generation.  Returns the post-increment value.
 *
 * th8GetArrayEpoch:
 *	Per-array mutation counter.  Returns -1 if the named
 *	variable does not exist as an array.  Caller MUST also
 *	check Th8_ExistsArrayVar to distinguish "epoch 0 on a
 *	fresh array" from "no such array".
 *
 * th8GetArrayElementHash:
 *	The array's underlying element hash (pHash on the parent
 *	Th8_Variable).  Used together with th8GetArrayEpoch to
 *	detect mid-iteration mutations without snapshotting the
 *	element-name list.  Returns NULL if not an array.
 *
 * th8GetArrayGeneration:
 *	Per-array element-hash allocation generation, stamped onto
 *	the Th8_Variable when its pHash was last created.  Pairs
 *	with the search-SID validation to catch unset+recreate
 *	scenarios even when the allocator reuses the freed pHash
 *	address.  Returns -1 if not an array.
 *
 * th8ArraySearchFreeEntry:
 *	Th8_HashIterate callback that releases a single search
 *	record.  Used both during interp teardown (sweep all
 *	pending searches) and when the find function lazily
 *	invalidates a stale entry.
 */
TH8_INTERNAL Th8_Hash *th8GetArraySearchHash(Th8_Interp *interp, int bCreate);
TH8_INTERNAL int th8NextArraySearchId(Th8_Interp *interp);
TH8_INTERNAL int
th8GetArrayEpoch(Th8_Interp *interp, const char *zVar, size_t nVar);
TH8_INTERNAL Th8_Hash *
th8GetArrayElementHash(Th8_Interp *interp, const char *zVar, size_t nVar);
TH8_INTERNAL int
th8GetArrayGeneration(Th8_Interp *interp, const char *zVar, size_t nVar);
TH8_INTERNAL int th8ArraySearchFreeEntry(Th8_HashEntry *pEntry, void *pCtx);

#endif /* TH8_VARS_H */
