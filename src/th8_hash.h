/*
 * th8_hash.h -- Standalone hash table interface.
 *
 * Defines the Th8_Hash and Th8_HashEntry types and the public API
 * for hash table operations (new, delete, find, remove, iterate).
 *
 * This header is designed to be usable by non-TH8 projects.  When
 * included from th8.h, the prerequisite types are already defined.
 * When included standalone, it provides its own minimal definitions.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef TH8_HASH_H
#define TH8_HASH_H

#include <stddef.h>  /* size_t */

/*
 * When included standalone (outside th8.h), provide minimal
 * definitions for the types and macros this header requires.
 */

#ifndef TH8_H

#  ifndef TH8_API
#    if defined(_WIN32) || defined(__CYGWIN__)
#      ifdef TH8_BUILD_DLL
#	define TH8_API __declspec(dllexport)
#      else
#	define TH8_API __declspec(dllimport)
#      endif
#    elif defined(__GNUC__) && __GNUC__ >= 4
#      define TH8_API __attribute__((visibility("default")))
#    else
#      define TH8_API
#    endif
#  endif

   /*
    * 64-bit integer types (must match th8.h's definitions).
    */
#  if defined(_MSC_VER)
typedef __int64 th8_int64_t;
typedef unsigned __int64 th8_uint64_t;
#  else
typedef long long th8_int64_t;
typedef unsigned long long th8_uint64_t;
#  endif

   /*
    * TH8_NOLEN sentinel (must match th8.h).
    */
#  ifndef TH8_NOLEN
#    define TH8_NOLEN ((size_t)-1)
#  endif

#  ifdef TH8_HASH_STANDALONE
   /*
    * Standalone mode: Th8_Interp and Th8_Platform are opaque
    * void pointers.  The implementation maps TH8 API calls to
    * CRT equivalents via macros in th8_hash.c.
    */
typedef void Th8_Interp;
typedef void Th8_Platform;
#  else
   /*
    * Non-standalone, non-th8.h: forward declarations so client
    * code can hold pointers without the full struct definition.
    */
typedef struct Th8_Interp Th8_Interp;
typedef struct Th8_Platform Th8_Platform;
#  endif

#endif /* !TH8_H */


/*
 *----------------------------------------------------------------------
 *
 * Th8_HashEntry --
 *
 *	A single entry in a Th8_Hash table.  The pData field is an
 *	opaque user pointer; the hash table does not manage it.
 *
 *----------------------------------------------------------------------
 */

#define TH8_HASH_SIZE 257

typedef struct Th8_Hash Th8_Hash;
typedef struct Th8_HashEntry Th8_HashEntry;

struct Th8_Hash {
    Th8_HashEntry *aBucket[TH8_HASH_SIZE];
    int nNextOrder;  /* Next insertion-order counter. */
};

struct Th8_HashEntry {
    th8_int64_t nVersion; /* Struct version (must be first). */
    void *pData;  /* User data pointer. */
    char *zKey;   /* Key string (owned). */
    size_t nKey;  /* Byte length of key. */
    Th8_HashEntry *pNext; /* Internal use only. */
    int nInsertOrder;  /* Insertion sequence number. */
};

/*
 *----------------------------------------------------------------------
 *
 * Public API --
 *
 *----------------------------------------------------------------------
 */

/*
 * Th8_HashNew --
 *	Create a new, empty hash table.  Returns NULL on allocation
 *	failure.
 */
TH8_API Th8_Hash *Th8_HashNew(Th8_Interp *interp);

/*
 * Th8_HashDelete --
 *	Destroy a hash table and free all its entries.  Does NOT free
 *	the pData pointers in entries; the caller is responsible for
 *	freeing those first (e.g., via Th8_HashIterate).
 */
TH8_API void Th8_HashDelete(Th8_Interp *interp, Th8_Hash *pHash);

/*
 * Th8_HashIterate --
 *	Call xCallback for every entry in pHash, passing the entry and
 *	pCtx.  xCallback returns TH8_OK to continue; any other value
 *	(e.g. TH8_BREAK, TH8_ERROR) stops iteration early.
 */
TH8_API void Th8_HashIterate(
    Th8_Interp *interp,
    Th8_Hash *pHash,
    int (*xCallback)(Th8_HashEntry *, void *),
    void *pCtx);

/*
 * Th8_HashIterateOrdered --
 *	Like Th8_HashIterate, but visits entries in insertion order.
 *	Used by dict commands to preserve key order per Tcl semantics.
 *	Allocates a temporary sort array; falls back to unordered
 *	iteration on allocation failure.
 */
TH8_API void Th8_HashIterateOrdered(
    Th8_Interp *interp,
    Th8_Hash *pHash,
    int (*xCallback)(Th8_HashEntry *, void *),
    void *pCtx);

/*
 * Th8_HashFind --
 *	Look up or create a hash entry.  zKey/nKey is the key.
 *	op controls behavior: 0 = lookup only (returns NULL if not
 *	found), >0 = create if not found (returns the new entry
 *	with pData==NULL), <0 = remove (internal use by
 *	Th8_HashRemove).
 */
TH8_API Th8_HashEntry *Th8_HashFind(
    Th8_Interp *interp,
    Th8_Hash *pHash,
    const char *zKey,
    size_t nKey,
    int op);

/*
 * Th8_HashRemove --
 *	Remove a single entry from a hash table by key.  Frees the
 *	entry struct and its key copy.  Does NOT free pData (caller
 *	is responsible for that).  No-op if the key is not found.
 */
TH8_API void Th8_HashRemove(
    Th8_Interp *interp,
    Th8_Hash *pHash,
    const char *zKey,
    size_t nKey);

/*
 * th8SeedHash --
 *	Initialize the global hash seeds from the platform's random
 *	bytes callback.  Called once during interpreter creation.
 *	Thread-safe (uses global mutex internally).
 */
TH8_API void th8SeedHash(Th8_Platform *pPlatform);

#endif /* TH8_HASH_H */
