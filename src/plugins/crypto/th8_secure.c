/*
 * th8_secure.c -- Secure variable subsystem for TH8.
 *
 * Implements encrypted-at-rest variable storage using AES-256-GCM.
 * Key material is held in a non-pageable (mlock'd / VirtualLock'd)
 * memory page with guard pages on both sides.  Each secure variable
 * gets its own 256-bit key, rotated on every write.
 *
 * Threat model: protect variable values against cold-boot attacks,
 * crash-dump forensics, and swap-file inspection.  The plaintext
 * transits through regular (pageable) memory during read operations;
 * this is an accepted limitation documented in
 * doc/internal/secure_variables.md.
 *
 * Maximum secure variables per interpreter: 128 (one 4096-byte
 * page / 32 bytes per key).
 *
 * Compile-time gate: TH8_ENABLE_CRYPTOGRAPHY
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

#include "th8_plat.h"
#include "th8.h"

#if defined(TH8_ENABLE_VARIABLES) && defined(TH8_ENABLE_CRYPTOGRAPHY)

#  include "th8_int.h"
#  include "th8_int_core.h"
#  include "th8_util.h"
#  include <openssl/evp.h>
#  include <openssl/crypto.h> /* OPENSSL_cleanse */

/*
 * Platform headers for locked-page allocation.
 * Included via meta-headers at the top of the compilation unit
 * that pulls in this file (th8_core.c includes th8_meta_posix.h).
 *
 * MAP_ANONYMOUS fallback for systems that only have MAP_ANON:
 */

#  if !defined(_WIN32) && !defined(WIN32)
#    if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
#      define MAP_ANONYMOUS MAP_ANON
#    endif
#  endif


/*
 *----------------------------------------------------------------------
 *
 * Constants.
 *
 *----------------------------------------------------------------------
 */

#  define TH8_SECURE_KEY_SIZE    32 /* AES-256 = 32 bytes. */
#  define TH8_SECURE_MAX_SLOTS   128 /* Fixed maximum. */
#  define TH8_SECURE_CANARY_SLOT 0 /* Slot 0: integrity canary. */
#  define TH8_SECURE_MASTER_SLOT 1 /* Slot 1: master key for persistence. */
#  define TH8_SECURE_FIRST_VAR   2 /* First usable variable slot. */
#  if defined(_WIN32)
#    define TH8_SECURE_PAGE_SIZE 4096 /* Win32 always 4K pages. */
#  endif
#  define TH8_SECURE_TAG_SIZE 16 /* GCM auth tag = 16 bytes. */
#  define TH8_SECURE_PAD_BLOCK                                               \
      64 /* Pad plaintext to this alignment
				 * for length confidentiality. */
#  define TH8_SECURE_NONCE_SIZE 12 /* 96-bit GCM nonce. */

/*
 * Canary: 8 magic bytes placed at the start of the key page.
 * Verified on every encrypt/decrypt to detect buffer overflows
 * or memory corruption that could silently compromise key
 * material.  Costs one key slot (slot 0 is reserved for the
 * canary, so usable slots are 1..MAX_SLOTS-1, giving 127).
 */

static const unsigned char th8SecureCanary[8] = {0x77, 0x73, 0x5A, 0x9F,
                                                 0x68, 0xBF, 0x3B, 0x9B};


/*
 *----------------------------------------------------------------------
 *
 * Th8_SecureVarData --
 *
 *	Per-variable metadata for an encrypted variable.  Stored in
 *	the interpreter's paSecureVar hash table, keyed by variable
 *	name.  The actual key material lives in the locked page; this
 *	struct holds only the ciphertext and bookkeeping.
 *
 *----------------------------------------------------------------------
 */

typedef struct Th8_SecureVarData {
    unsigned char *zCipher; /* Ciphertext (padded to PAD_BLOCK). */
    size_t nCipher;  /* Ciphertext length. */
    unsigned char aTag[TH8_SECURE_TAG_SIZE]; /* GCM auth tag. */
    th8_uint64_t nNonce; /* Monotonic nonce counter. */
    int iSlot;   /* Key slot index (0..MAX_SLOTS-1). */
} Th8_SecureVarData;


/*
 *----------------------------------------------------------------------
 *
 * Th8_KeyStore --
 *
 *	The locked-page key store.  One per interpreter.
 *
 *----------------------------------------------------------------------
 */

typedef struct Th8_KeyStore {
    unsigned char *pPage; /* mlock'd page (KEY_SIZE * MAX_SLOTS). */
    size_t nPageSize;  /* Actual system page size. */
    int nUsed;   /* Number of slots in use. */
    unsigned char aBitmap[TH8_SECURE_MAX_SLOTS / 8];
                                /* Slot allocation bitmap. */
#  if defined(_WIN32)
    unsigned char *pAlloc; /* Base of VirtualAlloc region. */
    size_t nAlloc;  /* Total allocated size (3 pages). */
#  else
    unsigned char *pRegion; /* Base of mmap region (3 pages). */
    size_t nRegion;  /* Total mmap size. */
#  endif
} Th8_KeyStore;


/*
 *----------------------------------------------------------------------
 *
 * th8SecureCheckCanary --
 *
 *	Verify the canary bytes at the start of the key page.
 *	Returns TH8_OK if intact, TH8_ERROR if corrupted.
 *
 * Why / How:
 *	A buffer overflow in the key page would silently corrupt
 *	key material, causing incorrect encryption/decryption
 *	without any error.  The canary detects this by comparing
 *	8 known magic bytes at slot 0 against a compile-time
 *	constant before every crypto operation.
 *
 *----------------------------------------------------------------------
 */

int
th8SecureCheckCanary(Th8_Interp *interp, const void *pKSv)
{
    const Th8_KeyStore *pKS = (const Th8_KeyStore *)pKSv;

    if (!pKS || !pKS->pPage) return TH8_ERROR;

    if (Th8_Memcmp(
            interp, pKS->pPage, th8SecureCanary, sizeof(th8SecureCanary)) !=
        0) {
	Th8_SetResultStatic(
	    interp,
	    "secure variable: key page canary corrupted "
	    "(possible buffer overflow)",
	    TH8_NOLEN);
	return TH8_ERROR;
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8SlotIsUsed / th8SlotSetUsed / th8SlotClearUsed / th8SlotKey --
 *
 *	Bitmap-based slot allocation for the key store.  Each secure
 *	variable gets one 32-byte slot in the locked page.  The bitmap
 *	tracks which slots are in use.
 *
 * Why / How:
 *	A fixed-size bitmap avoids heap allocation for slot management,
 *	keeping the entire key-store metadata out of pageable memory.
 *	Slot 0 is reserved for the canary; usable range is 1..MAX-1.
 *
 *----------------------------------------------------------------------
 */

static int
th8SlotIsUsed(const Th8_KeyStore *pKS, int i)
{
    return (pKS->aBitmap[i / 8] >> (i % 8)) & 1;
}

/*
 *----------------------------------------------------------------------
 *
 * th8SlotSetUsed --
 *
 *	Mark key-store slot `i` as occupied by setting its bit
 *	in `pKS->aBitmap`.  Companion to `th8SlotIsUsed`
 *	(query) and `th8SlotClearUsed` (release); the trio
 *	implements the bitmap-based slot allocator.
 *
 *	The caller is responsible for ensuring `i` is in the
 *	usable range `[1, TH8_SECURE_MAX_SLOTS-1]` -- slot 0
 *	is reserved for the canary and indices outside the
 *	bitmap would smash neighbouring fields.
 *
 * Parameters:
 *	pKS -- key store.
 *	i   -- slot index.
 *
 * Returns:
 *	None.
 *
 * Side effects:
 *	Sets one bit in `pKS->aBitmap`.
 *
 *----------------------------------------------------------------------
 */
static void
th8SlotSetUsed(Th8_KeyStore *pKS, int i)
{
    pKS->aBitmap[i / 8] |= (unsigned char)(1 << (i % 8));
}

/*
 *----------------------------------------------------------------------
 *
 * th8SlotClearUsed --
 *
 *	Release key-store slot `i` by clearing its bit in
 *	`pKS->aBitmap`.  Caller is responsible for having
 *	already zeroed the slot's key bytes if the key was
 *	sensitive (slot allocation only manages presence, not
 *	key material lifecycle).
 *
 * Parameters:
 *	pKS -- key store.
 *	i   -- slot index.
 *
 * Returns:
 *	None.
 *
 * Side effects:
 *	Clears one bit in `pKS->aBitmap`.
 *
 *----------------------------------------------------------------------
 */
static void
th8SlotClearUsed(Th8_KeyStore *pKS, int i)
{
    pKS->aBitmap[i / 8] &= (unsigned char)~(1 << (i % 8));
}

/*
 *----------------------------------------------------------------------
 *
 * th8SlotKey --
 *
 *	Return a pointer to the `TH8_SECURE_KEY_SIZE`-byte key
 *	material for slot `i` within the locked data page.
 *	The page is `mlock`-pinned (POSIX) or `VirtualLock`-pinned
 *	(Win32) and is bracketed by `PROT_NONE`/`PAGE_NOACCESS`
 *	guard pages, so any access past the slot triggers a
 *	hardware fault rather than corrupting neighbouring
 *	data.
 *
 *	Direct pointer access -- there is no defensive copy --
 *	so the caller must treat the returned buffer as
 *	sensitive material and avoid reading/writing past
 *	`TH8_SECURE_KEY_SIZE` bytes.
 *
 * Parameters:
 *	pKS -- key store.
 *	i   -- slot index.
 *
 * Returns:
 *	Pointer into the locked data page.  Never NULL when
 *	`pKS` is initialised.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static unsigned char *
th8SlotKey(Th8_KeyStore *pKS, int i)
{
    return &pKS->pPage[i * TH8_SECURE_KEY_SIZE];
}


#  if defined(_WIN32)

/*
 *----------------------------------------------------------------------
 *
 * th8SecureAllocPages (Win32) --
 *
 *	Reserve and commit three contiguous Win32 pages laid
 *	out as `[guard][data][guard]`.  The two guard pages
 *	are set to `PAGE_NOACCESS` so any underflow / overflow
 *	from the data page traps; the data page is locked via
 *	`VirtualLock` so its contents never reach the swap
 *	file (a `VirtualLock` failure degrades to "unlocked
 *	but still usable" rather than aborting, because
 *	`RLIMIT_MEMLOCK`-equivalent failures on Windows are
 *	configuration-dependent and the data is still
 *	functionally correct in unlocked form).
 *
 *	Key material must never reach swap or core dumps;
 *	guard pages with no access permissions create a
 *	hardware trap on overflow / underflow.  The mirror
 *	POSIX implementation lives below.
 *
 * Parameters:
 *	pKS -- key store; on success `pKS->pAlloc`,
 *		`pKS->nAlloc`, `pKS->pPage`, and
 *		`pKS->nPageSize` are populated.
 *
 * Returns:
 *	`TH8_OK` on success; `TH8_ERROR` if `VirtualAlloc`
 *	fails.
 *
 * Side effects:
 *	Allocates `3 * TH8_SECURE_PAGE_SIZE` of virtual
 *	address space; transitions two pages to PAGE_NOACCESS;
 *	`VirtualLock`s the data page (best-effort).
 *
 *----------------------------------------------------------------------
 */
static int
th8SecureAllocPages(Th8_KeyStore *pKS)
{
    size_t nTotal = 3 * TH8_SECURE_PAGE_SIZE;
    unsigned char *p;
    DWORD oldProt;

    p = (unsigned char *)
        VirtualAlloc(NULL, nTotal, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!p) return TH8_ERROR;

    pKS->pAlloc = p;
    pKS->nAlloc = nTotal;

    /* Guard pages: PAGE_NOACCESS. */
    VirtualProtect(p, TH8_SECURE_PAGE_SIZE, PAGE_NOACCESS, &oldProt);
    VirtualProtect(
        p + 2 * TH8_SECURE_PAGE_SIZE, TH8_SECURE_PAGE_SIZE, PAGE_NOACCESS,
        &oldProt);

    /* Data page: lock into physical memory. */
    pKS->pPage = p + TH8_SECURE_PAGE_SIZE;
    pKS->nPageSize = TH8_SECURE_PAGE_SIZE;
    if (!VirtualLock(pKS->pPage, TH8_SECURE_PAGE_SIZE)) {
	/* Degraded: unlocked but still usable. */
    }
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * th8SecureFreePages (Win32) --
 *
 *	Tear down the three-page region built by the Win32
 *	`th8SecureAllocPages`.  Securely zeros the data page
 *	via `Th8_SecureZero` (memset that the compiler cannot
 *	elide), unlocks it from physical memory, and releases
 *	the entire reservation via `VirtualFree(MEM_RELEASE)`.
 *
 *	Safe to call against a partially-initialised store --
 *	each NULL guard skips its block independently.
 *
 * Parameters:
 *	interp -- live interpreter (forwarded to
 *		`Th8_SecureZero`).
 *	pKS    -- key store.
 *
 * Returns:
 *	None.
 *
 * Side effects:
 *	Overwrites the data page with zeroes; unlocks and
 *	releases the entire three-page region.
 *
 *----------------------------------------------------------------------
 */
static void
th8SecureFreePages(Th8_Interp *interp, Th8_KeyStore *pKS)
{
    if (pKS->pPage) {
	Th8_SecureZero(interp, pKS->pPage, TH8_SECURE_PAGE_SIZE);
	VirtualUnlock(pKS->pPage, TH8_SECURE_PAGE_SIZE);
    }
    if (pKS->pAlloc) {
	VirtualFree(pKS->pAlloc, 0, MEM_RELEASE);
    }
}

#  else /* POSIX */

/*
 *----------------------------------------------------------------------
 *
 * th8SecureGetPageSize --
 *
 *	Return the system page size in bytes.  Used to compute
 *	the guard-page aligned allocation for the locked key store.
 *
 * Why / How:
 *	Queries sysconf(_SC_PAGESIZE) on POSIX systems.  Falls back
 *	to 4096 if the query fails (defensive default that works on
 *	all modern platforms).
 *
 * Results:
 *	Page size in bytes.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static long
th8SecureGetPageSize(void)
{
    long ps = sysconf(_SC_PAGESIZE);

    return (ps > 0) ? ps : 4096;
}

/*
 *----------------------------------------------------------------------
 *
 * th8SecureAllocPages (POSIX) --
 *
 *	Reserve a three-page region (`[guard][data][guard]`)
 *	via `mmap(MAP_PRIVATE | MAP_ANONYMOUS)`, transition
 *	the bracketing guard pages to `PROT_NONE`, and lock
 *	the data page in physical memory with `mlock` so it
 *	never reaches swap.  Hardware faults catch any
 *	overflow/underflow access.
 *
 *	`mlock` failure (typically `RLIMIT_MEMLOCK` too low)
 *	is non-fatal: the store keeps working in degraded
 *	mode without the swap-out guarantee.  On Linux the
 *	data page is additionally tagged with `MADV_DONTDUMP`
 *	(excluded from core dumps) and `MADV_WIPEONFORK`
 *	(zeroed in any child process) for further hardening;
 *	both `madvise` flags are best-effort.
 *
 *	Mirror of the Win32 implementation above.
 *
 * Parameters:
 *	pKS -- key store; on success `pKS->pRegion`,
 *		`pKS->nRegion`, `pKS->pPage`, and
 *		`pKS->nPageSize` are populated.
 *
 * Returns:
 *	`TH8_OK` on success; `TH8_ERROR` if `mmap` fails.
 *
 * Side effects:
 *	Allocates three pages of virtual memory; transitions
 *	two of them to `PROT_NONE`; `mlock`s the data page
 *	(best-effort); applies Linux-only `madvise` hardening
 *	when available.
 *
 *----------------------------------------------------------------------
 */
static int
th8SecureAllocPages(Th8_KeyStore *pKS)
{
    long pageSize = th8SecureGetPageSize();
    size_t nTotal = 3 * (size_t)pageSize;
    unsigned char *p;

    p = (unsigned char *)mmap(
        NULL, nTotal, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1,
        0);
    if (p == MAP_FAILED) return TH8_ERROR;

    pKS->pRegion = p;
    pKS->nRegion = nTotal;

    /* Guard pages: PROT_NONE (page-aligned by construction). */
    mprotect(p, (size_t)pageSize, PROT_NONE);
    mprotect(p + 2 * (size_t)pageSize, (size_t)pageSize, PROT_NONE);

    /* Data page: lock into physical memory. */
    pKS->pPage = p + (size_t)pageSize;
    if (mlock(pKS->pPage, (size_t)pageSize) != 0) {
	/* Degraded: unlocked but still usable.
	 * RLIMIT_MEMLOCK may be too low. */
    }
    pKS->nPageSize = (size_t)pageSize;

    /*
     * Linux-specific hardening:
     *   MADV_DONTDUMP  -- exclude from core dumps
     *   MADV_WIPEONFORK -- zero the page on fork()
     */

#    if defined(__linux__)
#      if defined(MADV_DONTDUMP)
    madvise(pKS->pPage, (size_t)pageSize, MADV_DONTDUMP);
#      endif
#      if defined(MADV_WIPEONFORK)
    madvise(pKS->pPage, (size_t)pageSize, MADV_WIPEONFORK);
#      endif
#    endif

    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * th8SecureFreePages (POSIX) --
 *
 *	Tear down the POSIX three-page region built by
 *	`th8SecureAllocPages`.  Securely zeros the data page
 *	(`Th8_SecureZero`), unlocks it via `munlock`, then
 *	releases the entire region with `munmap`.
 *
 *	The zeroed range is `TH8_SECURE_MAX_SLOTS *
 *	TH8_SECURE_KEY_SIZE` -- the actual key area, not the
 *	whole locked page -- since the guard pages are
 *	already inaccessible.
 *
 *	Safe to call against a partially-initialised store --
 *	each NULL guard skips its block independently.
 *
 * Parameters:
 *	interp -- live interpreter (forwarded to
 *		`Th8_SecureZero`).
 *	pKS    -- key store.
 *
 * Returns:
 *	None.
 *
 * Side effects:
 *	Overwrites the data page with zeroes; unlocks and
 *	unmaps the entire three-page region.
 *
 *----------------------------------------------------------------------
 */
static void
th8SecureFreePages(Th8_Interp *interp, Th8_KeyStore *pKS)
{
    if (pKS->pPage) {
	size_t nDataSize = TH8_SECURE_MAX_SLOTS * TH8_SECURE_KEY_SIZE;

	Th8_SecureZero(interp, pKS->pPage, nDataSize);
	munlock(pKS->pPage, pKS->nPageSize);
    }
    if (pKS->pRegion) {
	munmap(pKS->pRegion, pKS->nRegion);
    }
}

#  endif /* POSIX */


/*
 *----------------------------------------------------------------------
 *
 * th8SecureDeriveNonce --
 *
 *	Derive the 12-byte GCM nonce from the monotonic counter.
 *
 * Why / How:
 *	AES-GCM requires a unique nonce per (key, plaintext) pair.
 *	The 8-byte counter is stored little-endian in the first 8
 *	bytes of the nonce; the remaining 4 bytes are zero.  Since
 *	the key is rotated on every write, the counter could safely
 *	restart at 0; we keep incrementing for defense in depth.
 *
 *----------------------------------------------------------------------
 */

static void
th8SecureDeriveNonce(
    th8_uint64_t nCounter,
    unsigned char aNonce[TH8_SECURE_NONCE_SIZE])
{
    int i;

    for (i = 0; i < 8; i++) {
	aNonce[i] = (unsigned char)(nCounter >> (i * 8));
    }
    aNonce[8] = 0;
    aNonce[9] = 0;
    aNonce[10] = 0;
    aNonce[11] = 0;
}


/*
 *----------------------------------------------------------------------
 *
 * th8SecurePadSize --
 *
 *	Round up to the next PAD_BLOCK boundary for length
 *	confidentiality.  The padding byte count is stored as the
 *	last byte of the padded buffer (PKCS#7 style).
 *
 * Why / How:
 *	Without padding, ciphertext length reveals plaintext length.
 *	Padding to a 64-byte boundary quantizes lengths, preventing
 *	an attacker from distinguishing between e.g., a 3-char and
 *	a 4-char password based on ciphertext size alone.
 *
 *----------------------------------------------------------------------
 */

static size_t
th8SecurePadSize(size_t nPlain)
{
    size_t nPad = TH8_SECURE_PAD_BLOCK - (nPlain % TH8_SECURE_PAD_BLOCK);

    if (nPad == 0) nPad = TH8_SECURE_PAD_BLOCK;
    return nPlain + nPad;
}


/*
 *----------------------------------------------------------------------
 *
 * th8SecureEncrypt --
 *
 *	Encrypt a plaintext value using AES-256-GCM with the key
 *	from the locked page at the variable's assigned slot.
 *
 * Why / How:
 *	The plaintext is PKCS#7-padded for length confidentiality,
 *	then encrypted.  The variable name is bound as Additional
 *	Authenticated Data (AAD) so that swapping ciphertext
 *	between variables is detected.  The canary is verified
 *	before touching key material.  On success, ownership of
 *	the ciphertext buffer is transferred to pData; the padded
 *	plaintext is securely zeroed regardless of outcome.
 *
 *----------------------------------------------------------------------
 */

static int
th8SecureEncrypt(
    Th8_Interp *interp,
    Th8_KeyStore *pKS,
    Th8_SecureVarData *pData,
    const char *zName,  /* Variable name (AAD). */
    size_t nName,
    const char *zPlain,  /* Plaintext value. */
    size_t nPlain)
{
    EVP_CIPHER_CTX *ctx = NULL;
    unsigned char aNonce[TH8_SECURE_NONCE_SIZE];
    size_t nPadded;
    unsigned char *zPadBuf = NULL;
    unsigned char *zOut = NULL;
    int outLen = 0, tmpLen = 0;
    int rc = TH8_ERROR;

    /* Verify canary integrity before accessing key material. */
    if (th8SecureCheckCanary(interp, pKS) != TH8_OK) {
	return TH8_ERROR;
    }

    /* Bug 25 defense-in-depth (2026-06-07): normalise the
     * TH8_NOLEN sentinel for both length parameters before
     * either reaches the int cast at the EVP boundary.
     * Callers are expected to normalise (the th8SecureSetVar /
     * th8SecureVarCreate / th8SecureSave entry points all do),
     * but a sentinel slipping through would be cast to a huge
     * negative int and corrupt the AAD/ciphertext processing. */
    if (nName == TH8_NOLEN) nName = Th8_Strlen(interp, zName);
    if (nPlain == TH8_NOLEN) nPlain = Th8_Strlen(interp, zPlain);

    nPadded = th8SecurePadSize(nPlain);

    /* Build padded plaintext (PKCS#7). */
    zPadBuf = (unsigned char *)TH8_ALLOC(interp, nPadded);
    if (!zPadBuf) goto done;
    if (nPlain > 0) {
	Th8_Memcpy(interp, zPadBuf, zPlain, nPlain);
    }
    {
	unsigned char padVal = (unsigned char)(nPadded - nPlain);
	size_t i;

	for (i = nPlain; i < nPadded; i++) {
	    zPadBuf[i] = padVal;
	}
    }

    zOut = (unsigned char *)TH8_ALLOC(interp, nPadded);
    if (!zOut) goto done;

    /* Derive nonce from counter. */
    pData->nNonce++;
    th8SecureDeriveNonce(pData->nNonce, aNonce);

    ctx = EVP_CIPHER_CTX_new();
    if (!ctx) goto done;

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1)
	goto done;

    if (EVP_CIPHER_CTX_ctrl(
            ctx, EVP_CTRL_GCM_SET_IVLEN, TH8_SECURE_NONCE_SIZE, NULL) != 1)
	goto done;

    if (EVP_EncryptInit_ex(
            ctx, NULL, NULL, th8SlotKey(pKS, pData->iSlot), aNonce) != 1)
	goto done;

    /* AAD: variable name (not encrypted, but authenticated). */
    if (nName > 0) {
	if (EVP_EncryptUpdate(
	        ctx, NULL, &tmpLen, (const unsigned char *)zName,
	        (int)nName) != 1)
	    goto done;
    }

    if (EVP_EncryptUpdate(ctx, zOut, &outLen, zPadBuf, (int)nPadded) != 1)
	goto done;

    if (EVP_EncryptFinal_ex(ctx, zOut + outLen, &tmpLen) != 1) goto done;
    outLen += tmpLen;

    if (EVP_CIPHER_CTX_ctrl(
            ctx, EVP_CTRL_GCM_GET_TAG, TH8_SECURE_TAG_SIZE, pData->aTag) != 1)
	goto done;

    Th8_Free(interp, pData->zCipher);
    pData->zCipher = zOut;
    pData->nCipher = (size_t)outLen;
    zOut = NULL;  /* Ownership transferred. */
    rc = TH8_OK;

done:
    if (ctx) EVP_CIPHER_CTX_free(ctx);
    if (zPadBuf) {
	Th8_SecureZero(interp, zPadBuf, nPadded);
	Th8_Free(interp, zPadBuf);
    }
    Th8_Free(interp, zOut);
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * th8SecureDecrypt --
 *
 *	Decrypt a ciphertext value using AES-256-GCM, writing the
 *	unpadded plaintext into a caller-provided protected region.
 *	Returns the plaintext pointer (into the region's data area,
 *	just past the canary) and length via *pzPlain / *pnPlain.
 *	The caller does NOT free the returned pointer; the region
 *	is owned by the interpreter.  The caller MUST securely zero
 *	the data area before any subsequent use of the same region.
 *
 * Why / How:
 *	Writing decrypted plaintext into a regular heap buffer would
 *	leave it in pageable, swap-eligible memory between decrypt
 *	and the eventual secure-zero -- the exact exposure the
 *	protected-region machinery exists to prevent.  Both callers
 *	(getVar and save) share the per-interp pProtectedResult
 *	region: getVar makes the decrypted bytes the live sensitive
 *	interpreter result (zero copies), and save uses the same
 *	region as transient scratch (Th8_ClearResult before, secure-
 *	zero after re-encryption) so the plaintext never reaches
 *	pageable heap.
 *
 *	The GCM authentication tag is verified BEFORE the plaintext
 *	is finalized.  If verification fails, the data area is left
 *	with intermediate decrypt output but NOT exposed to the
 *	caller (rc = TH8_ERROR, *pzPlain remains NULL).  PKCS#7
 *	padding is stripped using constant-time comparison.  The
 *	variable name is authenticated via AAD.
 *
 *	Capacity check: the region's usable area (page size minus
 *	canary) MUST be at least pData->nCipher + 1 bytes.  If not,
 *	the function returns TH8_ERROR with a descriptive result.
 *
 *----------------------------------------------------------------------
 */

static int
th8SecureDecrypt(
    Th8_Interp *interp,
    Th8_KeyStore *pKS,
    const Th8_SecureVarData *pData,
    const char *zName,  /* Variable name (AAD). */
    size_t nName,
    Th8_ProtectedRegion *pDest, /* Destination protected region. */
    char **pzPlain,  /* OUT: pointer into pDest. */
    size_t *pnPlain)
{
    EVP_CIPHER_CTX *ctx = NULL;
    unsigned char aNonce[TH8_SECURE_NONCE_SIZE];
    unsigned char *zOut; /* Points into pDest's data area. */
    size_t nUsable;
    size_t nCanary;
    int outLen = 0, tmpLen = 0;
    int rc = TH8_ERROR;

    *pzPlain = NULL;
    *pnPlain = 0;

    if (!pDest) {
	Th8_SetResultStatic(
	    interp, "secure variable: missing destination region", TH8_NOLEN);
	return TH8_ERROR;
    }

    /* Bug 25 defense-in-depth (2026-06-07): normalise the
     * TH8_NOLEN sentinel before it reaches the int cast at
     * the EVP AAD boundary.  See the matching note in
     * th8SecureEncrypt for the full root-cause analysis. */
    if (nName == TH8_NOLEN) nName = Th8_Strlen(interp, zName);

    /* Verify canary integrity before accessing key material. */
    if (th8SecureCheckCanary(interp, pKS) != TH8_OK) {
	return TH8_ERROR;
    }
    if (th8ProtectedCheckCanary(interp, pDest) != TH8_OK) {
	/* th8ProtectedCheckCanary set the result. */
	return TH8_ERROR;
    }

    nCanary = th8ProtectedCanarySize();
    nUsable = th8ProtectedPageSize(pDest);
    if (nUsable < nCanary) {
	Th8_SetResultStatic(
	    interp, "secure variable: protected region too small", TH8_NOLEN);
	return TH8_ERROR;
    }
    nUsable -= nCanary;

    {
	unsigned char *pData8 = th8ProtectedData(pDest);

	if (!pData8) {
	    Th8_SetResultStatic(
	        interp, "secure variable: no data pointer in region",
	        TH8_NOLEN);
	    return TH8_ERROR;
	}
	zOut = pData8;
    }

    if (!pData->zCipher || pData->nCipher == 0) {
	/* Empty value. */
	if (nUsable < 1) {
	    Th8_SetResultStatic(
	        interp, "secure variable: protected region too small",
	        TH8_NOLEN);
	    return TH8_ERROR;
	}
	zOut[0] = 0;
	*pzPlain = (char *)zOut;
	*pnPlain = 0;
	return TH8_OK;
    }

    /*
     * Capacity check: AES-256-GCM produces ciphertext-length bytes
     * of plaintext (no expansion); we need that plus a NUL.
     */

    if (pData->nCipher + 1 > nUsable) {
	Th8_SetResultStatic(
	    interp,
	    "secure variable: ciphertext exceeds "
	    "protected region capacity",
	    TH8_NOLEN);
	return TH8_ERROR;
    }

    th8SecureDeriveNonce(pData->nNonce, aNonce);

    /* Bug 25 fix: every EVP-failure goto here used to skip
     * setting an interp result, so callers (th8SecureGetVar
     * etc.) returned TH8_ERROR with an empty error message --
     * particularly visible in the child-interp decrypt path
     * the original report flagged.  Each site now sets a
     * specific message identifying which EVP step failed
     * so future regressions are diagnosable without re-
     * instrumenting.  The DecryptFinal authentication path
     * below already had its own message; left as-is. */
    ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
	Th8_SetResultStatic(
	    interp, "secure variable: EVP_CIPHER_CTX_new failed", TH8_NOLEN);
	goto done;
    }

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1) {
	Th8_SetResultStatic(
	    interp, "secure variable: EVP_DecryptInit_ex (cipher) failed",
	    TH8_NOLEN);
	goto done;
    }

    if (EVP_CIPHER_CTX_ctrl(
            ctx, EVP_CTRL_GCM_SET_IVLEN, TH8_SECURE_NONCE_SIZE, NULL) != 1) {
	Th8_SetResultStatic(
	    interp, "secure variable: EVP_CIPHER_CTX_ctrl SET_IVLEN failed",
	    TH8_NOLEN);
	goto done;
    }

    if (EVP_DecryptInit_ex(
            ctx, NULL, NULL, th8SlotKey(pKS, pData->iSlot), aNonce) != 1) {
	Th8_SetResultStatic(
	    interp, "secure variable: EVP_DecryptInit_ex (key/nonce) failed",
	    TH8_NOLEN);
	goto done;
    }

    /* AAD. */
    if (nName > 0) {
	if (EVP_DecryptUpdate(
	        ctx, NULL, &tmpLen, (const unsigned char *)zName,
	        (int)nName) != 1) {
	    Th8_SetResultStatic(
	        interp, "secure variable: EVP_DecryptUpdate (AAD) failed",
	        TH8_NOLEN);
	    goto done;
	}
    }

    /* Decrypt directly into the protected region. */
    if (EVP_DecryptUpdate(
            ctx, zOut, &outLen, pData->zCipher, (int)pData->nCipher) != 1) {
	Th8_SetResultStatic(
	    interp, "secure variable: EVP_DecryptUpdate (ciphertext) failed",
	    TH8_NOLEN);
	goto done;
    }

    /* Verify authentication tag. */
    if (EVP_CIPHER_CTX_ctrl(
            ctx, EVP_CTRL_GCM_SET_TAG, TH8_SECURE_TAG_SIZE,
            (void *)pData->aTag) != 1) {
	Th8_SetResultStatic(
	    interp, "secure variable: EVP_CIPHER_CTX_ctrl SET_TAG failed",
	    TH8_NOLEN);
	goto done;
    }

    if (EVP_DecryptFinal_ex(ctx, zOut + outLen, &tmpLen) != 1) {
	Th8_SetResultStatic(
	    interp,
	    "secure variable: authentication failed "
	    "(data may be corrupted)",
	    TH8_NOLEN);
	goto done;
    }
    outLen += tmpLen;

    /*
     * Strip PKCS#7 padding (constant-time to avoid
     * padding oracle side channels).
     */
    if (outLen > 0) {
	unsigned char padVal = zOut[outLen - 1];
	unsigned char padMask = 0;
	int i;
	int padOk;

	for (i = 0; i < (int)padVal && i < outLen; i++) {
	    padMask |= (zOut[outLen - 1 - i] ^ padVal);
	}
	padOk = (padMask == 0) & (padVal > 0) &
	        (padVal <= TH8_SECURE_PAD_BLOCK) &
	        (padVal <= (unsigned char)outLen);
	outLen -= padOk ? (int)padVal : 0;
    }

    zOut[outLen] = 0; /* NUL-terminate. */
    *pzPlain = (char *)zOut;
    *pnPlain = (size_t)outLen;
    rc = TH8_OK;

done:
    if (ctx) EVP_CIPHER_CTX_free(ctx);
    if (rc != TH8_OK) {
	/*
	 * Decrypt failed (auth or otherwise) -- securely zero any
	 * partial plaintext bytes that were written to the region
	 * before returning.  The region itself remains allocated
	 * for reuse.
	 */
	if (zOut) {
	    Th8_SecureZero(interp, zOut, nUsable);
	}
    }
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * th8SecureInit --
 *
 *	Initialize the secure variable subsystem for an interpreter.
 *	Allocates the locked key page and installs the canary.
 *
 * Why / How:
 *	Called once during interpreter creation.  The key store is
 *	a single locked page with guard pages on both sides.  Slot
 *	0 is reserved for the canary; slots 1..127 are available
 *	for secure variables.
 *
 *----------------------------------------------------------------------
 */

int
th8SecureInit(Th8_Interp *interp)
{
    Th8_KeyStore *pKS;

    if (!interp) return TH8_ERROR;

    pKS = (Th8_KeyStore *)TH8_ALLOC(interp, sizeof(Th8_KeyStore));
    if (!pKS) return TH8_ERROR;
    Th8_Memset(interp, pKS, 0, sizeof(Th8_KeyStore));

    if (th8SecureAllocPages(pKS) != TH8_OK) {
	Th8_Free(interp, pKS);
	return TH8_ERROR;
    }

    /*
     * Install the canary in slot 0 and mark it as used.
     * This reserves slot 0 so that key material never
     * overlaps the canary bytes.
     */

    Th8_Memcpy(interp, pKS->pPage, th8SecureCanary, sizeof(th8SecureCanary));
    th8SlotSetUsed(pKS, 0);

    th8SetSecureKeyStore(interp, pKS);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8SecureCleanupEntry --
 *
 *	Hash iteration callback: securely zero and free one secure
 *	variable's ciphertext and metadata.
 *
 * Why / How:
 *	Used by th8SecureFinish to walk the entire secure variable
 *	hash.  Each entry's ciphertext is zeroed before freeing to
 *	minimize forensic exposure in freed heap memory.
 *
 *----------------------------------------------------------------------
 */

static int
th8SecureCleanupEntry(Th8_HashEntry *pEntry, void *pCtx)
{
    Th8_Interp *interp = (Th8_Interp *)pCtx;
    Th8_SecureVarData *pData = (Th8_SecureVarData *)pEntry->pData;

    if (pData) {
	if (pData->zCipher) {
	    Th8_SecureZero(interp, pData->zCipher, pData->nCipher);
	    Th8_Free(interp, pData->zCipher);
	}
	Th8_SecureZero(interp, pData, sizeof(Th8_SecureVarData));
	Th8_Free(interp, pData);
	pEntry->pData = NULL;
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8SecureFinish --
 *
 *	Shut down the secure variable subsystem.  Securely zeroes
 *	all key material, frees ciphertext, and releases the locked
 *	key page.
 *
 * Why / How:
 *	Called during interpreter deletion.  All secure variable
 *	metadata is iterated and zeroed, then the locked page is
 *	zeroed, unlocked, and unmapped.  The order matters: key
 *	material is zeroed while the page is still locked (pinned
 *	in RAM) to prevent the zeroing from being paged to swap.
 *
 *----------------------------------------------------------------------
 */

void
th8SecureFinish(Th8_Interp *interp)
{
    Th8_KeyStore *pKS;
    Th8_Hash *paSecure;

    if (!interp) return;

    paSecure = th8GetSecureVarHash(interp);
    if (paSecure) {
	Th8_HashIterate(interp, paSecure, th8SecureCleanupEntry, interp);
	Th8_HashDelete(interp, paSecure);
	th8SetSecureVarHash(interp, NULL);
    }

    /* Wipe and free the key store. */
    pKS = (Th8_KeyStore *)th8GetSecureKeyStore(interp);
    if (pKS) {
	th8SecureFreePages(interp, pKS);
	Th8_SecureZero(interp, pKS, sizeof(Th8_KeyStore));
	Th8_Free(interp, pKS);
	th8SetSecureKeyStore(interp, NULL);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8SecureVarCreate --
 *
 *	Create a new secure (encrypted-at-rest) variable.  Allocates
 *	a key slot, generates a random 256-bit key, encrypts the
 *	initial value, and registers the variable.
 *
 * Why / How:
 *	The key is generated directly into the locked page via
 *	Th8_RandomBytes (platform CSPRNG).  The variable is created
 *	as a normal Tcl variable with an empty sentinel value; the
 *	actual data lives in the encrypted metadata hash.  If any
 *	step fails, all partially-allocated resources (key slot,
 *	metadata, ciphertext) are cleaned up before returning.
 *
 *----------------------------------------------------------------------
 */

int
th8SecureVarCreate(
    Th8_Interp *interp,
    const char *zVar,
    size_t nVar,
    const char *zVal,
    size_t nVal)
{
    Th8_KeyStore *pKS;
    Th8_Hash *paSecure;
    Th8_HashEntry *pEntry;
    Th8_SecureVarData *pData;
    int iSlot;
    int rc;

    if (!interp) return TH8_ERROR;

    if (nVar == TH8_NOLEN) nVar = Th8_Strlen(interp, zVar);
    if (nVal == TH8_NOLEN) nVal = Th8_Strlen(interp, zVal);

    pKS = (Th8_KeyStore *)th8GetSecureKeyStore(interp);
    if (!pKS) {
	Th8_SetResultStatic(
	    interp, "secure variables not initialized", TH8_NOLEN);
	return TH8_ERROR;
    }

    if (Th8_ExistsVar(interp, zVar, nVar)) {
	Th8_ErrorMessage(
	    interp, "cannot modify existing variable \"", zVar, nVar);
	return TH8_ERROR;
    }

    /* Reject array syntax. */
    {
	size_t k;

	for (k = 0; k < nVar; k++) {
	    if (zVar[k] == '(') {
		Th8_SetResultStatic(
		    interp, "secure: array elements not supported",
		    TH8_NOLEN);
		return TH8_ERROR;
	    }
	}
    }

    /* Allocate a key slot.  Slot 0 is reserved for the canary. */
    if (pKS->nUsed >= TH8_SECURE_MAX_SLOTS - 1) {
	Th8_SetResultStatic(
	    interp,
	    "secure: maximum number of secure "
	    "variables reached (127)",
	    TH8_NOLEN);
	return TH8_ERROR;
    }

    iSlot = -1;
    {
	int i;

	for (i = TH8_SECURE_FIRST_VAR; i < TH8_SECURE_MAX_SLOTS; i++) {
	    if (!th8SlotIsUsed(pKS, i)) {
		iSlot = i;
		break;
	    }
	}
    }
    if (iSlot < 0) {
	Th8_SetResultStatic(interp, "secure: no free key slots", TH8_NOLEN);
	return TH8_ERROR;
    }

    /* Generate random key into the locked page. */
    rc = Th8_RandomBytes(interp, th8SlotKey(pKS, iSlot), TH8_SECURE_KEY_SIZE);
    if (rc != TH8_OK) {
	Th8_SetResultStatic(
	    interp, "secure: cannot generate key material", TH8_NOLEN);
	return TH8_ERROR;
    }
    th8SlotSetUsed(pKS, iSlot);
    pKS->nUsed++;

    pData = (Th8_SecureVarData *)TH8_ALLOC(interp, sizeof(Th8_SecureVarData));
    if (!pData) {
	Th8_SecureZero(interp, th8SlotKey(pKS, iSlot), TH8_SECURE_KEY_SIZE);
	th8SlotClearUsed(pKS, iSlot);
	pKS->nUsed--;
	return TH8_ERROR;
    }
    Th8_Memset(interp, pData, 0, sizeof(Th8_SecureVarData));
    pData->iSlot = iSlot;

    /* Encrypt the initial value. */
    rc = th8SecureEncrypt(
        interp, pKS, pData, zVar, nVar, zVal ? zVal : "", zVal ? nVal : 0);
    if (rc != TH8_OK) {
	Th8_SecureZero(interp, th8SlotKey(pKS, iSlot), TH8_SECURE_KEY_SIZE);
	th8SlotClearUsed(pKS, iSlot);
	pKS->nUsed--;
	Th8_Free(interp, pData);
	return TH8_ERROR;
    }

    /* Create the Tcl variable with a sentinel value. */
    rc = Th8_SetVar(interp, zVar, nVar, "", 0);
    if (rc != TH8_OK) {
	Th8_SecureZero(interp, th8SlotKey(pKS, iSlot), TH8_SECURE_KEY_SIZE);
	th8SlotClearUsed(pKS, iSlot);
	pKS->nUsed--;
	Th8_Free(interp, pData->zCipher);
	Th8_Free(interp, pData);
	return TH8_ERROR;
    }

    /* Register in the secure variable hash. */
    paSecure = th8GetSecureVarHash(interp);
    if (!paSecure) {
	paSecure = Th8_HashNew(interp);
	if (!paSecure) {
	    Th8_SecureZero(
	        interp, th8SlotKey(pKS, iSlot), TH8_SECURE_KEY_SIZE);
	    th8SlotClearUsed(pKS, iSlot);
	    pKS->nUsed--;
	    Th8_Free(interp, pData->zCipher);
	    Th8_Free(interp, pData);
	    Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	    return TH8_ERROR;
	}
	th8SetSecureVarHash(interp, paSecure);
    }
    pEntry = Th8_HashFind(interp, paSecure, zVar, nVar, 1);
    if (pEntry) {
	pEntry->pData = pData;
    }

    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8SecureVarDelete --
 *
 *	Delete a secure variable.  Securely zeroes the key slot,
 *	ciphertext, and metadata, then removes the Tcl variable.
 *
 * Why / How:
 *	The key material in the locked page is zeroed BEFORE the
 *	slot is marked free, ensuring no window where a freed slot
 *	still contains valid key bytes.  The ciphertext is also
 *	zeroed before freeing to minimize forensic exposure.
 *
 *----------------------------------------------------------------------
 */

int
th8SecureVarDelete(Th8_Interp *interp, const char *zVar, size_t nVar)
{
    Th8_KeyStore *pKS;
    Th8_Hash *paSecure;
    Th8_HashEntry *pEntry;
    Th8_SecureVarData *pData;

    if (!interp) return TH8_ERROR;

    if (nVar == TH8_NOLEN) nVar = Th8_Strlen(interp, zVar);

    paSecure = th8GetSecureVarHash(interp);
    if (!paSecure) {
	Th8_ErrorMessage(interp, "variable is not secure: \"", zVar, nVar);
	return TH8_ERROR;
    }

    pEntry = Th8_HashFind(interp, paSecure, zVar, nVar, 0);
    if (!pEntry || !pEntry->pData) {
	Th8_ErrorMessage(interp, "variable is not secure: \"", zVar, nVar);
	return TH8_ERROR;
    }

    pData = (Th8_SecureVarData *)pEntry->pData;

    /* Zero and free the key slot. */
    pKS = (Th8_KeyStore *)th8GetSecureKeyStore(interp);
    if (pKS && pData->iSlot >= 0 && pData->iSlot < TH8_SECURE_MAX_SLOTS) {
	Th8_SecureZero(
	    interp, th8SlotKey(pKS, pData->iSlot), TH8_SECURE_KEY_SIZE);
	th8SlotClearUsed(pKS, pData->iSlot);
	pKS->nUsed--;
    }

    /* Zero and free the ciphertext. */
    if (pData->zCipher) {
	Th8_SecureZero(interp, pData->zCipher, pData->nCipher);
	Th8_Free(interp, pData->zCipher);
    }
    Th8_SecureZero(interp, pData, sizeof(Th8_SecureVarData));
    Th8_Free(interp, pData);

    /*
     * Remove the entry from the secure-var hash entirely.  Leaving
     * a NULL-pData tombstone would cause th8IsSecureVar to find
     * the entry on a later lookup and trip ALWAYS(pEntry->pData).
     */
    Th8_HashRemove(interp, paSecure, zVar, nVar);

    /* Unset the Tcl variable. */
    Th8_UnsetVar(interp, zVar, nVar);

    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8IsSecureVar --
 *
 *	Test whether a variable is a secure (encrypted) variable.
 *	Returns 1 if the variable has secure metadata, 0 otherwise.
 *
 *----------------------------------------------------------------------
 */

int
th8IsSecureVar(Th8_Interp *interp, const char *zVar, size_t nVar)
{
    Th8_Hash *paSecure;
    Th8_HashEntry *pEntry;

    if (!interp) return 0;

    if (nVar == TH8_NOLEN) nVar = Th8_Strlen(interp, zVar);
    paSecure = th8GetSecureVarHash(interp);
    if (!paSecure) return 0;

    pEntry = Th8_HashFind(interp, paSecure, zVar, nVar, 0);
    return (pEntry && ALWAYS(pEntry->pData)) ? 1 : 0;
}


/*
 *----------------------------------------------------------------------
 *
 * th8SecureGetVar --
 *
 *	Called from Th8_GetVar when a secure variable is read.
 *	Decrypts the value, sets the interpreter result, and
 *	securely zeros the temporary plaintext buffer.
 *
 * Why / How:
 *	The plaintext transits through pageable heap memory during
 *	the read.  To minimize exposure, the temporary buffer is
 *	zeroed immediately after copying to the result.  The result
 *	itself is marked "sensitive" so Th8_SetResult will zero it
 *	on the next result change.
 *
 *----------------------------------------------------------------------
 */

int
th8SecureGetVar(Th8_Interp *interp, const char *zVar, size_t nVar)
{
    Th8_Hash *paSecure;
    Th8_HashEntry *pEntry;
    Th8_SecureVarData *pData;
    Th8_KeyStore *pKS;
    Th8_ProtectedRegion *pPR;
    char *zPlain = NULL;
    size_t nPlain = 0;
    int rc;

    if (!interp) return TH8_ERROR;

    /* Bug 25 root cause (2026-06-07): callers (notably
     * Th8_GetVar reached via the var resolver) may pass
     * nVar == TH8_NOLEN to request "interpret zVar as a
     * NUL-terminated C string".  Without normalisation here
     * that sentinel value (SIZE_MAX) flows through to
     * th8SecureDecrypt and is cast to int as the EVP AAD
     * length -- OpenSSL correctly rejects the result
     * (error:030000DD invalid length) and decryption fails
     * with no recoverable plaintext.  The encrypt counterpart
     * (th8SecureVarCreate) normalises at entry; mirror it
     * here so the AAD bytes match the encrypt side. */
    if (nVar == TH8_NOLEN) nVar = Th8_Strlen(interp, zVar);

    paSecure = th8GetSecureVarHash(interp);
    if (!paSecure) return TH8_ERROR;
    pEntry = Th8_HashFind(interp, paSecure, zVar, nVar, 0);
    if (!pEntry || !pEntry->pData) return TH8_ERROR;
    pData = (Th8_SecureVarData *)pEntry->pData;

    pKS = (Th8_KeyStore *)th8GetSecureKeyStore(interp);
    if (!pKS) return TH8_ERROR;

    /*
     * Decrypt directly into the interpreter's protected result
     * region.  The plaintext therefore never transits pageable
     * heap; AES-256-GCM writes its output straight into the
     * mlock'd, guard-paged page that will back the sensitive
     * result.  We must clear any prior result first because
     * decrypt is about to overwrite the bytes that any active
     * borrowed-from-region zResult would still be pointing at.
     */

    pPR = th8GetProtectedResultRegion(interp);
    if (!pPR) return TH8_ERROR;

    Th8_ClearResult(interp);

    rc = th8SecureDecrypt(
        interp, pKS, pData, zVar, nVar, pPR, &zPlain, &nPlain);
    if (rc != TH8_OK) {
	return rc;
    }
    (void)zPlain; /* Pointer into the region; finalize by length. */
    return th8FinalizeSensitiveResult(interp, nPlain);
}


/*
 *----------------------------------------------------------------------
 *
 * th8SecureSetVar --
 *
 *	Called from Th8_SetVar when a secure variable is written.
 *	Rotates the key and encrypts the new value.
 *
 * Why / How:
 *	The key is rotated BEFORE encryption: the old key is zeroed,
 *	a fresh random key is generated into the same slot, and the
 *	new value is encrypted with the fresh key.  Key rotation on
 *	every write ensures that capturing ciphertext at two points
 *	in time reveals nothing about the relationship between the
 *	two values (forward secrecy at the variable level).
 *
 *----------------------------------------------------------------------
 */

int
th8SecureSetVar(
    Th8_Interp *interp,
    const char *zVar,
    size_t nVar,
    const char *zNewVal,
    size_t nNewVal)
{
    Th8_Hash *paSecure;
    Th8_HashEntry *pEntry;
    Th8_SecureVarData *pData;
    Th8_KeyStore *pKS;
    int rc;

    if (!interp) return TH8_ERROR;

    /* Bug 25 root cause (2026-06-07): same as th8SecureGetVar
     * -- normalise the TH8_NOLEN sentinel before it reaches
     * th8SecureEncrypt's AAD cast.  Without this, set on a
     * NUL-terminated zVar would pass SIZE_MAX as AAD length
     * to EVP_EncryptUpdate, which would fail. */
    if (nVar == TH8_NOLEN) nVar = Th8_Strlen(interp, zVar);
    if (nNewVal == TH8_NOLEN) nNewVal = Th8_Strlen(interp, zNewVal);

    paSecure = th8GetSecureVarHash(interp);
    if (!paSecure) return TH8_OK;  /* Not a secure var. */
    pEntry = Th8_HashFind(interp, paSecure, zVar, nVar, 0);
    if (!pEntry || !pEntry->pData) return TH8_OK;
    pData = (Th8_SecureVarData *)pEntry->pData;

    pKS = (Th8_KeyStore *)th8GetSecureKeyStore(interp);
    if (!pKS) return TH8_ERROR;

    /* Bug 65: bound the slot index before writing the key page.  iSlot
     * is always a valid created slot, so an out-of-range value here is
     * a "must never happen" corruption -- without this guard it would
     * be a wild TH8_SECURE_KEY_SIZE-byte CSPRNG write at an arbitrary
     * key-page offset.  NEVER() asserts it in debug, keeps the check in
     * release, and compiles to a constant under the MC/DC build so it
     * adds no uncoverable decision.  Fail closed. */
    if (NEVER(pData->iSlot < 0 || pData->iSlot >= TH8_SECURE_MAX_SLOTS)) {
	return TH8_ERROR;
    }

    /* Rotate the key FIRST: zero old, generate new.
     * The new ciphertext is then encrypted with the fresh key,
     * which is the same key that th8SecureGetVar will use for
     * the next decryption. */
    Th8_SecureZero(
        interp, th8SlotKey(pKS, pData->iSlot), TH8_SECURE_KEY_SIZE);
    rc = Th8_RandomBytes(
        interp, th8SlotKey(pKS, pData->iSlot), TH8_SECURE_KEY_SIZE);
    if (rc != TH8_OK) return rc;

    /* Encrypt the new value with the fresh key. */
    rc = th8SecureEncrypt(interp, pKS, pData, zVar, nVar, zNewVal, nNewVal);

    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * th8SecureVarCleanup --
 *
 *	Called from th8FreeVariable when a variable with secure
 *	metadata is being freed.
 *
 * Why / How:
 *	This is the variable-deletion hook.  It locates the secure
 *	metadata in the hash, zeroes the key slot and ciphertext,
 *	and frees the metadata.  This ensures that deleting a
 *	variable via [unset] properly cleans up all crypto state.
 *
 *----------------------------------------------------------------------
 */

void
th8SecureVarCleanup(Th8_Interp *interp, const char *zVar, size_t nVar)
{
    Th8_Hash *paSecure;
    Th8_HashEntry *pEntry;
    Th8_SecureVarData *pData;
    Th8_KeyStore *pKS;

    if (!interp) return;

    paSecure = th8GetSecureVarHash(interp);
    if (!paSecure) return;

    pEntry = Th8_HashFind(interp, paSecure, zVar, nVar, 0);
    if (!pEntry || !pEntry->pData) return;
    pData = (Th8_SecureVarData *)pEntry->pData;

    pKS = (Th8_KeyStore *)th8GetSecureKeyStore(interp);
    if (pKS && pData->iSlot >= 0 && pData->iSlot < TH8_SECURE_MAX_SLOTS) {
	Th8_SecureZero(
	    interp, th8SlotKey(pKS, pData->iSlot), TH8_SECURE_KEY_SIZE);
	th8SlotClearUsed(pKS, pData->iSlot);
	pKS->nUsed--;
    }

    /* Zero and free the metadata. */
    if (pData->zCipher) {
	Th8_SecureZero(interp, pData->zCipher, pData->nCipher);
	Th8_Free(interp, pData->zCipher);
    }
    Th8_SecureZero(interp, pData, sizeof(Th8_SecureVarData));
    Th8_Free(interp, pData);
    pEntry->pData = NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_EnableSecurePersist --
 *
 *	Public API: open or close the security gate that
 *	allows scripts to use the secure-variable persistence
 *	subsystem.  Implements the dual-field random-token
 *	pattern that defends against script-side
 *	self-promotion:
 *
 *	  * `bEnable = 1` -- generate a fresh 64-bit random
 *	    token (rejecting `0`, `1`, and all-ones, which
 *	    are the trivially-guessable values) and store it
 *	    in **both** `interp->nSecurePersistToken` and
 *	    `interp->nSecurePersistOk`.  Persistence is then
 *	    allowed.
 *	  * `bEnable = 0` -- zero both fields.  Persistence
 *	    is denied.
 *
 *	Scripts cannot self-enable persistence because the
 *	token is unguessable and a single write through the
 *	script-visible API only affects one of the two
 *	fields; the gate (`Th8_IsSecurePersistEnabled`) only
 *	opens when both match.  Only embedders (C code with
 *	`interp` access) can call this function.
 *
 *	The retry loop reseeds on `0`, `1`, and all-ones
 *	values per Bug 26 (2026-06-07): plain `while` --
 *	regenerating is itself the corrective handler.  After
 *	100 retries the helper gives up with a diagnostic
 *	rather than spinning forever.
 *
 * Parameters:
 *	interp  -- live interpreter.
 *	bEnable -- 1 to open the gate; 0 to close it.
 *
 * Returns:
 *	`TH8_OK` on success; `TH8_ERROR` on NULL `interp` or
 *	on RNG exhaustion (rare: 100 retries; interpreter
 *	result: "unable to generate persistence token").
 *
 * Side effects:
 *	Mutates `interp->nSecurePersistToken` and
 *	`interp->nSecurePersistOk`.  May set the interpreter
 *	result on failure.
 *
 *----------------------------------------------------------------------
 */
int
Th8_EnableSecurePersist(Th8_Interp *interp, int bEnable)
{
    if (!interp) return TH8_ERROR;
    if (bEnable) {
	th8_int64_t tok = 0;
	int retries = 0;
	unsigned char buf[8];

	do {
	    if (++retries > 100) {
		Th8_SetResultStatic(
		    interp, "unable to generate persistence token",
		    TH8_NOLEN);
		return TH8_ERROR;
	    }
	    if (TH8_OK == Th8_RandomBytes(interp, buf, 8)) {
		size_t j;

		tok = 0;
		for (j = 0; j < 8; j++) {
		    tok |= ((th8_uint64_t)buf[j]) << (j * 8);
		}
	    }
	    /* Bug 26 (2026-06-07): plain while -- regen IS the handler. */
	} while (tok == 0 || tok == ~(th8_int64_t)0 || tok == 1);

	interp->nSecurePersistToken = tok;
	interp->nSecurePersistOk = tok;
    } else {
	interp->nSecurePersistToken = 0;
	interp->nSecurePersistOk = 0;
    }
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Th8_IsSecurePersistEnabled --
 *
 *	Public API: query whether the secure-variable
 *	persistence gate is currently open.  Implements the
 *	read-side of the dual-field random-token pattern
 *	(see `Th8_EnableSecurePersist`):
 *
 *	  Gate open iff `nSecurePersistOk != 0` **AND**
 *	  `nSecurePersistOk == nSecurePersistToken`.
 *
 *	The two-field check is the security property: a
 *	script that manages to overwrite one field through any
 *	mechanism still cannot match the random token in the
 *	other.
 *
 * Parameters:
 *	interp -- live interpreter (or NULL).
 *
 * Returns:
 *	1 if the gate is open; 0 otherwise (including NULL
 *	interp).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
int
Th8_IsSecurePersistEnabled(Th8_Interp *interp)
{
    if (!interp) return 0;
    return interp->nSecurePersistOk != 0 &&
           interp->nSecurePersistOk == interp->nSecurePersistToken;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_SecureSetMasterKey --
 *
 *	Copy the embedder-provided master key (32 bytes) into the
 *	locked key page at slot 1 (reserved for master key).
 *
 * Why / How:
 *	The master key gets the same mlock/guard-page protection as
 *	per-variable keys.  Slot 1 is special-cased (not tracked in
 *	the bitmap).  The key is validated to be exactly 32 bytes.
 *
 *----------------------------------------------------------------------
 */

int
Th8_SecureSetMasterKey(
    Th8_Interp *interp,
    const unsigned char *pKey,
    size_t nKey)
{
    Th8_KeyStore *pKS;

    if (!interp) return TH8_ERROR;
    if (nKey != TH8_SECURE_KEY_SIZE) {
	Th8_SetResultStatic(
	    interp, "master key must be exactly 32 bytes", TH8_NOLEN);
	return TH8_ERROR;
    }
    if (!pKey) {
	Th8_SetResultStatic(interp, "master key pointer is NULL", TH8_NOLEN);
	return TH8_ERROR;
    }

    pKS = (Th8_KeyStore *)th8GetSecureKeyStore(interp);
    if (!pKS) {
	Th8_SetResultStatic(
	    interp, "secure variables not initialized", TH8_NOLEN);
	return TH8_ERROR;
    }

    /* Verify canary before writing to key page. */
    if (th8SecureCheckCanary(interp, pKS) != TH8_OK) {
	return TH8_ERROR;
    }

    /* Copy master key into slot 1. */
    Th8_Memcpy(
        interp, th8SlotKey(pKS, TH8_SECURE_MASTER_SLOT), pKey,
        TH8_SECURE_KEY_SIZE);

    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_SecureClearMasterKey --
 *
 *	Securely zero the master key in slot 1 of the locked page.
 *
 * Why / How:
 *	After clearing, all subsequent save/load operations fail
 *	until a new master key is set.  The key is zeroed in-place
 *	while the page is still locked (pinned in RAM).
 *
 *----------------------------------------------------------------------
 */

void
Th8_SecureClearMasterKey(Th8_Interp *interp)
{
    Th8_KeyStore *pKS;

    if (!interp) return;
    pKS = (Th8_KeyStore *)th8GetSecureKeyStore(interp);
    if (!pKS || !pKS->pPage) return;

    Th8_SecureZero(
        interp, th8SlotKey(pKS, TH8_SECURE_MASTER_SLOT), TH8_SECURE_KEY_SIZE);
}


/*
 *----------------------------------------------------------------------
 *
 * th8SecureHasMasterKey --
 *
 *	Check if a master key has been set (slot 1 is non-zero).
 *
 *----------------------------------------------------------------------
 */

int
th8SecureHasMasterKey(Th8_Interp *interp)
{
    Th8_KeyStore *pKS;
    unsigned char *pSlot;
    int i;

    pKS = (Th8_KeyStore *)th8GetSecureKeyStore(interp);
    if (!pKS || !pKS->pPage) return 0;

    pSlot = th8SlotKey(pKS, TH8_SECURE_MASTER_SLOT);
    for (i = 0; i < TH8_SECURE_KEY_SIZE; i++) {
	if (pSlot[i] != 0) return 1;
    }
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * th8SecureSave --
 *
 *	Persist a secure variable to xKeyValue under master-key
 *	encryption.
 *
 * Why / How:
 *	1. Gate check (Th8_IsSecurePersistEnabled)
 *	2. Master key presence check
 *	3. Decrypt plaintext using per-variable key
 *	4. Re-encrypt under master key (fresh nonce, var name as AAD)
 *	5. Serialize to blob (magic + version + nonce + tag + cipher)
 *	6. Store via Th8_KeyValue(TH8_KV_SET, "th8:secure:name", blob)
 *	7. Securely zero temporary plaintext
 *
 *----------------------------------------------------------------------
 */

#  define TH8_SECURE_BLOB_MAGIC   "\x54\x38\x53\x56" /* "T8SV" */
#  define TH8_SECURE_BLOB_VERSION 1
#  define TH8_SECURE_BLOB_HEADER                                             \
      40 /* magic(4)+ver(1)+reserved(3)+nonce(12)+tag(16)+len(4) */
#  define TH8_SECURE_KV_PREFIX     "th8:secure:"
#  define TH8_SECURE_KV_PREFIX_LEN 11

/*
 *----------------------------------------------------------------------
 *
 * th8SecureSave --
 *
 *	Internal: re-encrypt a secure variable under the
 *	embedder-supplied master key and hand the resulting
 *	binary blob to the embedder's KV callback for
 *	persistence.  Called by the public `secure persist`
 *	command path; not exported in the public API.
 *
 *	Pipeline (security-critical, order matters):
 *
 *	  1. Gate checks: persistence enabled
 *	     (`Th8_IsSecurePersistEnabled`) and master key set
 *	     (`th8SecureHasMasterKey`).  Both fail closed with
 *	     a script-visible diagnostic.
 *	  2. Look up the variable in the per-interp secure
 *	     hash; refuse if absent or non-secure.
 *	  3. Decrypt the variable's per-variable plaintext
 *	     into the per-interp `Th8_ProtectedRegion` scratch
 *	     area, **after** clearing any pending interpreter
 *	     result that might be borrowing bytes from that
 *	     same region.
 *	  4. Pad the plaintext to a fixed block boundary
 *	     (`th8SecurePadSize`) and allocate a blob buffer
 *	     of `TH8_SECURE_BLOB_HEADER + nPadded` bytes.
 *	  5. AES-GCM-encrypt under the master key with a
 *	     freshly-derived nonce and authentication tag
 *	     written into the header.
 *	  6. Build the KV-store key as `"th8:secure:<varname>"`
 *	     and hand `(zKvKey, zBlob, nBlob)` to the
 *	     embedder via the KV callback.
 *	  7. Securely zero the scratch region and zBlob,
 *	     `Th8_Free` both, and reset the interpreter
 *	     result.
 *
 *	The plaintext never reaches pageable heap: the
 *	decrypt target is the locked `Th8_ProtectedRegion`,
 *	and `zBlob` is freed via `Th8_SecureZero` + `Th8_Free`
 *	in the unwind.
 *
 *	Gated on `TH8_ENABLE_CRYPTOGRAPHY && TH8_ENABLE_VARIABLES`.
 *
 * Parameters:
 *	interp -- live interpreter.
 *	zVar   -- variable name (not necessarily NUL-terminated).
 *	nVar   -- variable-name length.
 *
 * Returns:
 *	`TH8_OK` on successful persist; `TH8_ERROR` on any
 *	gate / lookup / crypto / KV failure (interpreter
 *	result: diagnostic from the failing stage).
 *
 * Side effects:
 *	Invokes the embedder's KV callback for the variable;
 *	allocates / zeros / frees a temporary blob buffer and
 *	the per-interp protected scratch region; updates the
 *	interpreter result.
 *
 *----------------------------------------------------------------------
 */
int
th8SecureSave(Th8_Interp *interp, const char *zVar, size_t nVar)
{
    Th8_KeyStore *pKS;
    Th8_Hash *paSecure;
    Th8_HashEntry *pEntry;
    Th8_SecureVarData *pData;
    char *zPlain = NULL;
    size_t nPlain = 0;
    unsigned char *zBlob = NULL;
    size_t nBlob;
    size_t nPadded;
    unsigned char aNonce[TH8_SECURE_NONCE_SIZE];
    unsigned char aTag[TH8_SECURE_TAG_SIZE];
    EVP_CIPHER_CTX *ctx = NULL;
    int outLen = 0, tmpLen = 0;
    int rc = TH8_ERROR;
    char *zKvKey = NULL;

    if (!interp) return TH8_ERROR;

    /* Gate checks. */
    if (!Th8_IsSecurePersistEnabled(interp)) {
	Th8_SetResultStatic(
	    interp, "secure persistence not enabled", TH8_NOLEN);
	return TH8_ERROR;
    }
    if (!th8SecureHasMasterKey(interp)) {
	Th8_SetResultStatic(interp, "master key not set", TH8_NOLEN);
	return TH8_ERROR;
    }

    /* Look up the secure variable. */
    paSecure = th8GetSecureVarHash(interp);
    if (!paSecure) {
	Th8_SetResultStatic(interp, "no secure variables exist", TH8_NOLEN);
	return TH8_ERROR;
    }
    pEntry = Th8_HashFind(interp, paSecure, zVar, nVar, 0);
    if (!pEntry || !pEntry->pData) {
	Th8_ErrorMessage(interp, "variable is not secure: \"", zVar, nVar);
	return TH8_ERROR;
    }
    pData = (Th8_SecureVarData *)pEntry->pData;

    pKS = (Th8_KeyStore *)th8GetSecureKeyStore(interp);
    if (!pKS) return TH8_ERROR;

    /*
     * Decrypt the per-variable plaintext into the per-interp
     * protected region used as scratch.  Clear any active
     * sensitive result first because we are about to overwrite
     * the very bytes a borrowed-from-region zResult would point
     * into.  After re-encryption we securely zero the region
     * and leave the result empty.  This keeps the plaintext
     * out of pageable heap entirely.
     */

    {
	Th8_ProtectedRegion *pPR = th8GetProtectedResultRegion(interp);
	if (!pPR) return TH8_ERROR;
	Th8_ClearResult(interp);
	rc = th8SecureDecrypt(
	    interp, pKS, pData, zVar, nVar, pPR, &zPlain, &nPlain);
	if (rc != TH8_OK) return rc;
    }

    nPadded = th8SecurePadSize(nPlain);
    if (Th8_SafeAdd(interp, TH8_SECURE_BLOB_HEADER, nPadded, &nBlob) !=
        TH8_OK) {
	goto done;
    }
    zBlob = (unsigned char *)TH8_ALLOC(interp, nBlob);
    if (!zBlob) goto done;

    if (Th8_RandomBytes(interp, aNonce, TH8_SECURE_NONCE_SIZE) != TH8_OK)
	goto done;

    /* PKCS#7 pad into the blob's ciphertext region. */
    {
	unsigned char *zPad = zBlob + TH8_SECURE_BLOB_HEADER;
	unsigned char padVal = (unsigned char)(nPadded - nPlain);
	size_t i;

	if (nPlain > 0) {
	    Th8_Memcpy(interp, zPad, zPlain, nPlain);
	}
	for (i = nPlain; i < nPadded; i++) {
	    zPad[i] = padVal;
	}
    }

    ctx = EVP_CIPHER_CTX_new();
    if (!ctx) goto done;
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1)
	goto done;
    if (EVP_CIPHER_CTX_ctrl(
            ctx, EVP_CTRL_GCM_SET_IVLEN, TH8_SECURE_NONCE_SIZE, NULL) != 1)
	goto done;
    if (EVP_EncryptInit_ex(
            ctx, NULL, NULL, th8SlotKey(pKS, TH8_SECURE_MASTER_SLOT),
            aNonce) != 1)
	goto done;

    /* AAD: variable name. */
    if (nVar > 0) {
	if (EVP_EncryptUpdate(
	        ctx, NULL, &tmpLen, (const unsigned char *)zVar, (int)nVar) !=
	    1)
	    goto done;
    }

    /* In-place encrypt: ciphertext overwrites padded plaintext in blob. */
    if (EVP_EncryptUpdate(
            ctx, zBlob + TH8_SECURE_BLOB_HEADER, &outLen,
            zBlob + TH8_SECURE_BLOB_HEADER, (int)nPadded) != 1)
	goto done;
    if (EVP_EncryptFinal_ex(
            ctx, zBlob + TH8_SECURE_BLOB_HEADER + outLen, &tmpLen) != 1)
	goto done;
    outLen += tmpLen;

    if (EVP_CIPHER_CTX_ctrl(
            ctx, EVP_CTRL_GCM_GET_TAG, TH8_SECURE_TAG_SIZE, aTag) != 1)
	goto done;

    /* Blob header: magic, version, nonce, tag, plaintext length. */
    Th8_Memcpy(interp, zBlob, TH8_SECURE_BLOB_MAGIC, 4);
    zBlob[4] = TH8_SECURE_BLOB_VERSION;
    zBlob[5] = 0; /* reserved */
    zBlob[6] = 0;
    zBlob[7] = 0;
    Th8_Memcpy(interp, zBlob + 8, aNonce, TH8_SECURE_NONCE_SIZE);
    Th8_Memcpy(interp, zBlob + 20, aTag, TH8_SECURE_TAG_SIZE);
    zBlob[36] = (unsigned char)(nPlain & 0xFF);
    zBlob[37] = (unsigned char)((nPlain >> 8) & 0xFF);
    zBlob[38] = (unsigned char)((nPlain >> 16) & 0xFF);
    zBlob[39] = (unsigned char)((nPlain >> 24) & 0xFF);

    /* Build "th8:secure:varName" KV key and persist the blob. */
    zKvKey = (char *)
        TH8_ALLOC_STR_ADD(interp, TH8_SECURE_KV_PREFIX_LEN, nVar);
    if (!zKvKey) goto done;
    Th8_Memcpy(
        interp, zKvKey, TH8_SECURE_KV_PREFIX, TH8_SECURE_KV_PREFIX_LEN);
    Th8_Memcpy(interp, zKvKey + TH8_SECURE_KV_PREFIX_LEN, zVar, nVar);
    zKvKey[TH8_SECURE_KV_PREFIX_LEN + nVar] = '\0';

    rc = Th8_KeyValue(
        interp, TH8_KV_SET, zKvKey, TH8_SECURE_KV_PREFIX_LEN + nVar,
        (const char *)zBlob, nBlob);

done:
    if (ctx) EVP_CIPHER_CTX_free(ctx);
    /*
     * Securely zero the protected region's data area.  zPlain
     * pointed into the region; the region itself remains allocated
     * for reuse by subsequent decrypt-or-result operations.
     */
    if (zPlain) {
	Th8_ProtectedRegion *pPR = (Th8_ProtectedRegion *)
	                               interp->pProtectedResult;
	if (pPR) {
	    size_t nUsable = th8ProtectedPageSize(pPR);
	    size_t nCanary = th8ProtectedCanarySize();
	    unsigned char *pData8 = th8ProtectedData(pPR);
	    if (pData8 && nUsable >= nCanary) {
		Th8_SecureZero(interp, pData8, nUsable - nCanary);
	    }
	}
    }
    if (zBlob) {
	Th8_SecureZero(interp, zBlob, nBlob);
	Th8_Free(interp, zBlob);
    }
    Th8_Free(interp, zKvKey);
    if (rc == TH8_OK) {
	Th8_ClearResult(interp);
    }
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * th8SecureLoad --
 *
 *	Load a secure variable from xKeyValue, decrypting the blob
 *	with the master key and creating a fresh in-memory secure
 *	variable.
 *
 * Why / How:
 *	1. Gate check (Th8_IsSecurePersistEnabled)
 *	2. Master key presence check
 *	3. Retrieve blob via Th8_KeyValue(TH8_KV_GET)
 *	4. Validate magic + version
 *	5. Decrypt under master key (verify AAD = var name)
 *	6. Strip PKCS#7 padding
 *	7. Create new secure variable with decrypted value
 *	8. Securely zero temporary buffers
 *
 *----------------------------------------------------------------------
 */

int
th8SecureLoad(Th8_Interp *interp, const char *zVar, size_t nVar)
{
    Th8_KeyStore *pKS;
    char *zKvKey = NULL;
    const char *zResult = NULL;
    unsigned char *zBlob = NULL; /* owned heap copy of the blob */
    size_t nBlob = 0;
    unsigned char *zDecrypted = NULL;
    size_t nDecrypted;
    unsigned char aNonce[TH8_SECURE_NONCE_SIZE];
    unsigned char aTag[TH8_SECURE_TAG_SIZE];
    size_t nPlainLen;
    size_t nCipher;
    EVP_CIPHER_CTX *ctx = NULL;
    int outLen = 0, tmpLen = 0;
    int rc = TH8_ERROR;

    if (!interp) return TH8_ERROR;

    /* Gate checks. */
    if (!Th8_IsSecurePersistEnabled(interp)) {
	Th8_SetResultStatic(
	    interp, "secure persistence not enabled", TH8_NOLEN);
	return TH8_ERROR;
    }
    if (!th8SecureHasMasterKey(interp)) {
	Th8_SetResultStatic(interp, "master key not set", TH8_NOLEN);
	return TH8_ERROR;
    }

    pKS = (Th8_KeyStore *)th8GetSecureKeyStore(interp);
    if (!pKS) return TH8_ERROR;

    zKvKey = (char *)
        TH8_ALLOC_STR_ADD(interp, TH8_SECURE_KV_PREFIX_LEN, nVar);
    if (!zKvKey) return TH8_ERROR;
    Th8_Memcpy(
        interp, zKvKey, TH8_SECURE_KV_PREFIX, TH8_SECURE_KV_PREFIX_LEN);
    Th8_Memcpy(interp, zKvKey + TH8_SECURE_KV_PREFIX_LEN, zVar, nVar);
    zKvKey[TH8_SECURE_KV_PREFIX_LEN + nVar] = '\0';

    /* Retrieve blob from KV store. */
    rc = Th8_KeyValue(
        interp, TH8_KV_GET, zKvKey, TH8_SECURE_KV_PREFIX_LEN + nVar, NULL, 0);
    if (rc != TH8_OK) {
	Th8_Free(interp, zKvKey);
	Th8_ErrorMessage(interp, "secure load: not found: \"", zVar, nVar);
	return TH8_ERROR;
    }

    /*
     * The result contains the blob.  Copy it to a private heap
     * allocation IMMEDIATELY: the result pointer is a borrowed
     * reference into interp->zResult, which Th8_ClearResult below
     * will free.  Without this copy, every subsequent read of
     * zBlob (header parsing AND the EVP_DecryptUpdate at the
     * bottom that consumes the ciphertext bytes) would be a use-
     * after-free.  On macOS the freed bytes happened to remain
     * intact long enough for decryption to succeed; on Win32 the
     * heap reuses small allocations more aggressively and the
     * decryption sees scrambled ciphertext, producing variables
     * that never get created (manifests as "no such variable"
     * when the script tries to read the loaded value).
     */
    zResult = Th8_GetResult(interp, &nBlob);
    if (!zResult || nBlob < TH8_SECURE_BLOB_HEADER) {
	Th8_Free(interp, zKvKey);
	Th8_SetResultStatic(
	    interp, "secure load: invalid blob (too short)", TH8_NOLEN);
	return TH8_ERROR;
    }
    zBlob = (unsigned char *)TH8_ALLOC(interp, nBlob);
    if (!zBlob) {
	Th8_Free(interp, zKvKey);
	Th8_ClearResult(interp);
	Th8_SetResultStatic(
	    interp, "secure load: out of memory copying blob", TH8_NOLEN);
	return TH8_ERROR;
    }
    Th8_Memcpy(interp, zBlob, zResult, nBlob);
    Th8_ClearResult(interp); /* zResult is now invalid; zBlob owns the data */

    /* Validate magic and version. */
    if (Th8_Memcmp(interp, zBlob, TH8_SECURE_BLOB_MAGIC, 4) != 0) {
	Th8_SetResultStatic(
	    interp, "secure load: invalid blob magic", TH8_NOLEN);
	goto done;
    }
    if ((unsigned char)zBlob[4] != TH8_SECURE_BLOB_VERSION) {
	Th8_SetResultStatic(
	    interp, "secure load: unsupported blob version", TH8_NOLEN);
	goto done;
    }

    /* Extract header fields. */
    Th8_Memcpy(interp, aNonce, zBlob + 8, TH8_SECURE_NONCE_SIZE);
    Th8_Memcpy(interp, aTag, zBlob + 20, TH8_SECURE_TAG_SIZE);
    nPlainLen = (size_t)(unsigned char)zBlob[36] |
                ((size_t)(unsigned char)zBlob[37] << 8) |
                ((size_t)(unsigned char)zBlob[38] << 16) |
                ((size_t)(unsigned char)zBlob[39] << 24);

    nCipher = nBlob - TH8_SECURE_BLOB_HEADER;
    if (nCipher == 0 || nPlainLen > nCipher) {
	Th8_SetResultStatic(interp, "secure load: corrupted blob", TH8_NOLEN);
	goto done;
    }

    /*
     * Master-key decrypt directly into the per-interpreter
     * protected region (the same region used as the sensitive-
     * result backing store).  The plaintext therefore never
     * touches pageable heap: it is decrypted into protected
     * memory, read by th8SecureVarCreate (which re-encrypts
     * under a fresh per-variable key into the variable's heap-
     * resident ciphertext), and secure-zeroed when this function
     * returns.  Th8_ClearResult releases any active sensitive
     * result that aliases this region before we overwrite it.
     */

    {
	Th8_ProtectedRegion *pPR;
	unsigned char *pData;
	size_t nUsable;
	size_t nCanary;

	pPR = th8GetProtectedResultRegion(interp);
	if (!pPR) goto done;
	if (th8ProtectedCheckCanary(interp, pPR) != TH8_OK) goto done;
	nCanary = th8ProtectedCanarySize();
	nUsable = th8ProtectedPageSize(pPR);
	if (nUsable < nCanary || nCipher + 1 > nUsable - nCanary) {
	    Th8_SetResultStatic(
	        interp,
	        "secure load: blob exceeds "
	        "protected region capacity",
	        TH8_NOLEN);
	    goto done;
	}
	pData = th8ProtectedData(pPR);
	if (!pData) goto done;

	zDecrypted = pData; /* points INTO protected region */
    }

    ctx = EVP_CIPHER_CTX_new();
    if (!ctx) goto done;
    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1)
	goto done;
    if (EVP_CIPHER_CTX_ctrl(
            ctx, EVP_CTRL_GCM_SET_IVLEN, TH8_SECURE_NONCE_SIZE, NULL) != 1)
	goto done;
    if (EVP_DecryptInit_ex(
            ctx, NULL, NULL, th8SlotKey(pKS, TH8_SECURE_MASTER_SLOT),
            aNonce) != 1)
	goto done;

    /* AAD: variable name. */
    if (nVar > 0) {
	if (EVP_DecryptUpdate(
	        ctx, NULL, &tmpLen, (const unsigned char *)zVar, (int)nVar) !=
	    1)
	    goto done;
    }

    /* Decrypt directly into the protected region. */
    if (EVP_DecryptUpdate(
            ctx, zDecrypted, &outLen,
            (const unsigned char *)(zBlob + TH8_SECURE_BLOB_HEADER),
            (int)nCipher) != 1)
	goto done;

    /* Verify auth tag. */
    if (EVP_CIPHER_CTX_ctrl(
            ctx, EVP_CTRL_GCM_SET_TAG, TH8_SECURE_TAG_SIZE, (void *)aTag) !=
        1)
	goto done;
    if (EVP_DecryptFinal_ex(ctx, zDecrypted + outLen, &tmpLen) != 1) {
	Th8_SetResultStatic(
	    interp,
	    "secure load: authentication failed "
	    "(wrong master key or corrupted data)",
	    TH8_NOLEN);
	goto done;
    }
    outLen += tmpLen;
    nDecrypted = (size_t)outLen;

    /* Use nPlainLen (from header) as the actual plaintext size. */
    if (nPlainLen > nDecrypted) {
	Th8_SetResultStatic(
	    interp, "secure load: plaintext length mismatch", TH8_NOLEN);
	goto done;
    }

    rc = th8SecureVarCreate(
        interp, zVar, nVar, (const char *)zDecrypted, nPlainLen);

done:
    if (ctx) EVP_CIPHER_CTX_free(ctx);
    /*
     * Secure-zero the protected region's data area (zDecrypted
     * points into it; the region itself remains allocated for
     * reuse).  Note: the region is owned by the interpreter, so
     * we do NOT call Th8_Free on zDecrypted.
     */
    if (zDecrypted) {
	Th8_ProtectedRegion *pPR = (Th8_ProtectedRegion *)
	                               interp->pProtectedResult;
	if (pPR) {
	    size_t nUsable2 = th8ProtectedPageSize(pPR);
	    size_t nCanary2 = th8ProtectedCanarySize();
	    unsigned char *pData2 = th8ProtectedData(pPR);
	    if (pData2 && nUsable2 >= nCanary2) {
		Th8_SecureZero(interp, pData2, nUsable2 - nCanary2);
	    }
	}
    }
    Th8_Free(interp, zKvKey);
    if (zBlob) {
	/*
	 * The blob is ciphertext + auth tag, not plaintext, but
	 * zero before free anyway: defense in depth, cheap, and
	 * matches the secure-zero pattern used everywhere else
	 * in this file.
	 */
	Th8_SecureZero(interp, zBlob, nBlob);
	Th8_Free(interp, zBlob);
    }
    if (rc == TH8_OK) {
	Th8_ClearResult(interp);
    }
    return rc;
}


#endif /* TH8_ENABLE_VARIABLES && TH8_ENABLE_CRYPTOGRAPHY */
