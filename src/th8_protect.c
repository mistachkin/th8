/*
 * th8_protect.c --
 *
 *	Locked-page memory protection for sensitive data.  Allocates
 *	a region of physical-memory-locked pages flanked by guard pages
 *	(PROT_NONE / PAGE_NOACCESS) to prevent buffer overflows into
 *	sensitive material.
 *
 *	This is the memory protection layer extracted from th8_secure.c.
 *	It is independent of the variable system and can be used by any
 *	subsystem that needs to protect sensitive data in memory (e.g.,
 *	RSA private keys, AES key slots, session tokens).
 *
 *	Gated on TH8_ENABLE_CRYPTOGRAPHY only (no variable dependency).
 *
 *	Platform support:
 *	  - POSIX: mmap + mlock + mprotect + madvise (Linux hardening)
 *	  - Win32: VirtualAlloc + VirtualLock + VirtualProtect
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
#include "th8_int.h"

#if defined(TH8_ENABLE_CRYPTOGRAPHY)


/*
 * Constants.
 */

#  if defined(_WIN32) || defined(WIN32)
#    define TH8_PROTECT_PAGE_SIZE 4096
#  endif

/*
 * Canary value: 8 bytes written to the start of every protected
 * region.  Verified before every access.  Detects buffer overflows
 * or memory corruption.
 */

static const unsigned char th8ProtectCanary[8] = {0x77, 0x73, 0x5A, 0x9F,
                                                  0x68, 0xBF, 0x3B, 0x9B};


/*
 *----------------------------------------------------------------------
 *
 * th8ProtectedAlloc --
 *
 *	Allocate a protected memory region: one data page flanked by
 *	two guard pages (PROT_NONE / PAGE_NOACCESS).  The data page
 *	is locked into physical memory (mlock / VirtualLock) to
 *	prevent swapping to disk.
 *
 *	On success, pRegion is populated with the page pointers and
 *	sizes.  The canary is installed at the start of the data page.
 *
 *	On failure, returns TH8_ERROR.  mlock failure is non-fatal
 *	(the region is still usable but not locked -- degraded security).
 *
 * Why / How:
 *	On Win32, uses VirtualAlloc with MEM_RESERVE|MEM_COMMIT to
 *	get three contiguous pages, then VirtualProtect to set the
 *	first and third to PAGE_NOACCESS (guard pages), and VirtualLock
 *	to pin the data page.  On POSIX, uses mmap with MAP_ANONYMOUS,
 *	mprotect for PROT_NONE guard pages, and mlock for pinning.
 *	Linux-specific MADV_DONTDUMP and MADV_WIPEONFORK hardening
 *	are applied when available.  A canary pattern is written at
 *	the start of the data page for corruption detection.
 *
 *----------------------------------------------------------------------
 */

#  if defined(_WIN32) || defined(WIN32)

int
th8ProtectedAlloc(Th8_Interp *interp, Th8_ProtectedRegion *pRegion)
{
    size_t nTotal;
    unsigned char *p;
    DWORD oldProt;

    if (!pRegion) return TH8_ERROR;
    memset(pRegion, 0, sizeof(*pRegion));

    nTotal = 3 * TH8_PROTECT_PAGE_SIZE;
    p = (unsigned char *)
        VirtualAlloc(NULL, nTotal, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!p) {
	TH8_TRACE_ERR(interp, "VirtualAlloc failed");
	return TH8_ERROR;
    }

    pRegion->pAlloc = p;
    pRegion->nAlloc = nTotal;

    /* Guard pages: PAGE_NOACCESS. */
    if (!VirtualProtect(p, TH8_PROTECT_PAGE_SIZE, PAGE_NOACCESS, &oldProt)) {
	TH8_TRACE_ERR(NULL, "VirtualProtect failed for guard page");
    }
    if (!VirtualProtect(
            p + 2 * TH8_PROTECT_PAGE_SIZE, TH8_PROTECT_PAGE_SIZE,
            PAGE_NOACCESS, &oldProt)) {
	TH8_TRACE_ERR(NULL, "VirtualProtect failed for guard page");
    }

    /* Data page. */
    pRegion->pPage = p + TH8_PROTECT_PAGE_SIZE;
    pRegion->nPageSize = TH8_PROTECT_PAGE_SIZE;

    /* Lock into physical memory. */
    if (!VirtualLock(pRegion->pPage, TH8_PROTECT_PAGE_SIZE)) {
	TH8_TRACE_ERR(interp, "VirtualLock failed (degraded)");
    }

    /* Install canary. */
    memcpy(pRegion->pPage, th8ProtectCanary, sizeof(th8ProtectCanary));

    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8ProtectedFree (Win32) --
 *
 *	Release a protected memory region allocated by
 *	th8ProtectedAlloc on Win32.
 *
 * Why / How:
 *	Zeroes the data page to scrub sensitive material, unlocks
 *	it via VirtualUnlock, then releases the entire allocation
 *	via VirtualFree with MEM_RELEASE.  The region struct is
 *	zeroed to prevent dangling pointer use.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees virtual memory.  Unlocks the data page from physical
 *	memory.
 *
 *----------------------------------------------------------------------
 */

void
th8ProtectedFree(Th8_Interp *interp, Th8_ProtectedRegion *pRegion)
{
    if (!pRegion) return;
    if (pRegion->pPage) {
	Th8_Memset(interp, pRegion->pPage, 0, TH8_PROTECT_PAGE_SIZE);
	if (!VirtualUnlock(pRegion->pPage, TH8_PROTECT_PAGE_SIZE)) {
	    TH8_TRACE_ERR(NULL, "VirtualUnlock failed");
	}
    }
    if (pRegion->pAlloc) {
	if (!VirtualFree(pRegion->pAlloc, 0, MEM_RELEASE)) {
	    TH8_TRACE_ERR(NULL, "VirtualFree failed");
	}
    }
    memset(pRegion, 0, sizeof(*pRegion));
}

#  else /* POSIX */

/*
 *----------------------------------------------------------------------
 *
 * th8ProtectGetPageSize --
 *
 *	Return the OS page size on POSIX systems.
 *
 * Why / How:
 *	Uses sysconf(_SC_PAGESIZE) where available; falls back to
 *	4096 if the sysconf call fails or _SC_PAGESIZE is not
 *	defined.  4096 is the correct page size on x86, x86_64,
 *	and most ARM systems.
 *
 * Results:
 *	The page size in bytes.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static long
th8ProtectGetPageSize(void)
{
#    if defined(_SC_PAGESIZE)
    long n = sysconf(_SC_PAGESIZE);
    return (n > 0) ? n : 4096;
#    else
    return 4096;
#    endif
}


/*
 *----------------------------------------------------------------------
 *
 * th8ProtectedAlloc --
 *
 *	Allocate a guarded data region for sensitive
 *	scratch storage (decrypted plaintext, intermediate
 *	key material, etc.).  Reserves three contiguous pages
 *	via `mmap(MAP_PRIVATE | MAP_ANONYMOUS)` laid out as
 *	`[guard][data][guard]`; transitions the guard pages
 *	to `PROT_NONE` so any underflow/overflow access
 *	traps; locks the data page with `mlock` so its
 *	contents never reach swap.
 *
 *	On Linux the data page is additionally tagged with
 *	`MADV_DONTDUMP` (excluded from core dumps) and
 *	`MADV_WIPEONFORK` (zeroed in any child process) for
 *	further hardening.  `mlock` / `mprotect` / `madvise`
 *	failures are non-fatal (the region keeps working in
 *	degraded mode) but logged via `TH8_TRACE_ERR`.
 *
 *	A canary pattern is written at the start of the
 *	data page; consumers verify it before relying on the
 *	region.  This makes the most common use-after-free
 *	bug class an immediate detectable corruption rather
 *	than a silent miscompare.
 *
 *	Mirror of the Win32 implementation later in this file.
 *
 * Parameters:
 *	interp  -- live interpreter (for trace messages only;
 *		the helper itself does not allocate through
 *		`interp`).
 *	pRegion -- output region; on success its
 *		`pRegion`, `nRegion`, `pPage`, and
 *		`nPageSize` fields are populated.
 *
 * Returns:
 *	`TH8_OK` on success; `TH8_ERROR` on NULL `pRegion`
 *	or `mmap` failure (trace message logged).
 *
 * Side effects:
 *	Allocates three pages of virtual memory; transitions
 *	two of them to `PROT_NONE`; `mlock`s the data page
 *	(best-effort); applies Linux-only `madvise` hardening
 *	when available; writes the canary pattern.
 *
 *----------------------------------------------------------------------
 */
int
th8ProtectedAlloc(Th8_Interp *interp, Th8_ProtectedRegion *pRegion)
{
    long pageSize;
    size_t nTotal;
    unsigned char *p;

    if (!pRegion) return TH8_ERROR;
    memset(pRegion, 0, sizeof(*pRegion));

    pageSize = th8ProtectGetPageSize();
    nTotal = 3 * (size_t)pageSize;

    p = (unsigned char *)mmap(
        NULL, nTotal, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1,
        0);
    if (p == MAP_FAILED) {
	TH8_TRACE_ERR(interp, "mmap failed");
	return TH8_ERROR;
    }

    pRegion->pRegion = p;
    pRegion->nRegion = nTotal;

    /* Guard pages: PROT_NONE. */
    if (mprotect(p, (size_t)pageSize, PROT_NONE) != 0) {
	TH8_TRACE_ERR(NULL, "mprotect failed for guard page");
    }
    if (mprotect(p + 2 * (size_t)pageSize, (size_t)pageSize, PROT_NONE) !=
        0) {
	TH8_TRACE_ERR(NULL, "mprotect failed for guard page");
    }

    /* Data page. */
    pRegion->pPage = p + (size_t)pageSize;
    pRegion->nPageSize = (size_t)pageSize;

    /* Lock into physical memory. */
    if (mlock(pRegion->pPage, (size_t)pageSize) != 0) {
	TH8_TRACE_ERR(interp, "mlock failed (degraded)");
    }

    /* Linux-specific hardening. */
#    if defined(__linux__)
#      if defined(MADV_DONTDUMP)
    madvise(pRegion->pPage, (size_t)pageSize, MADV_DONTDUMP);
#      endif
#      if defined(MADV_WIPEONFORK)
    madvise(pRegion->pPage, (size_t)pageSize, MADV_WIPEONFORK);
#      endif
#    endif

    /* Install canary. */
    memcpy(pRegion->pPage, th8ProtectCanary, sizeof(th8ProtectCanary));

    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8ProtectedFree (POSIX) --
 *
 *	Release a protected memory region allocated by
 *	th8ProtectedAlloc on POSIX systems.
 *
 * Why / How:
 *	Zeroes the data page to scrub sensitive material, unlocks
 *	it via munlock, then releases the entire mmap'd region via
 *	munmap.  The region struct is zeroed to prevent dangling
 *	pointer use.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Unmaps virtual memory.  Unlocks the data page from physical
 *	memory.
 *
 *----------------------------------------------------------------------
 */

void
th8ProtectedFree(Th8_Interp *interp, Th8_ProtectedRegion *pRegion)
{
    if (!pRegion) return;
    if (pRegion->pPage) {
	Th8_Memset(interp, pRegion->pPage, 0, pRegion->nPageSize);
	if (munlock(pRegion->pPage, pRegion->nPageSize) != 0) {
	    TH8_TRACE_ERR(NULL, "munlock failed");
	}
    }
    if (pRegion->pRegion) {
	if (munmap(pRegion->pRegion, pRegion->nRegion) != 0) {
	    TH8_TRACE_ERR(NULL, "munmap failed");
	}
    }
    memset(pRegion, 0, sizeof(*pRegion));
}

#  endif /* POSIX */


/*
 *----------------------------------------------------------------------
 *
 * th8ProtectedCheckCanary --
 *
 *	Verify the integrity canary at the start of a protected
 *	region.  Returns TH8_OK if intact, TH8_ERROR if corrupted.
 *	This detects buffer overflows or memory corruption.
 *
 * Why / How:
 *	Compares the first 8 bytes of the data page against the
 *	expected th8ProtectCanary constant using memcmp.  If they
 *	differ, something has overwritten the guard bytes, which
 *	strongly indicates a buffer overflow or use-after-free.
 *
 * Results:
 *	TH8_OK if the canary is intact, TH8_ERROR if corrupted
 *	or if the region is NULL.
 *
 * Side effects:
 *	Sets the interpreter result on failure.
 *
 *----------------------------------------------------------------------
 */

int
th8ProtectedCheckCanary(
    Th8_Interp *interp,
    const Th8_ProtectedRegion *pRegion)
{
    /* Bug 26: pRegion is from caller -- caller could pass NULL. */
    if (!pRegion || !pRegion->pPage) {
	Th8_SetResult(interp, "protected region: NULL page", TH8_NOLEN);
	return TH8_ERROR;
    }
    if (memcmp(pRegion->pPage, th8ProtectCanary, sizeof(th8ProtectCanary)) !=
        0) {
	Th8_SetResult(
	    interp,
	    "protected region: canary corrupted "
	    "(possible buffer overflow)",
	    TH8_NOLEN);
	return TH8_ERROR;
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8ProtectedPageSize --
 *
 *	Return the usable data page size for the protected region.
 *
 * Why / How:
 *	Simple accessor that returns the nPageSize field, which is
 *	set during allocation to the OS page size.  Returns 0 if
 *	the region pointer is NULL to allow safe probing.
 *
 * Results:
 *	The page size in bytes, or 0 if pRegion is NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

size_t
th8ProtectedPageSize(const Th8_ProtectedRegion *pRegion)
{
    return pRegion ? pRegion->nPageSize : 0;
}


/*
 *----------------------------------------------------------------------
 *
 * th8ProtectedData --
 *
 *	Return a pointer to the usable data area of the protected
 *	region, starting after the 8-byte canary.
 *
 * Why / How:
 *	The data page starts with the 8-byte canary, so usable data
 *	begins at pPage + sizeof(th8ProtectCanary).  Returns NULL if
 *	the region is not allocated.
 *
 * Results:
 *	Pointer to the first usable byte, or NULL if the region
 *	is invalid.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

unsigned char *
th8ProtectedData(const Th8_ProtectedRegion *pRegion)
{
    /* Bug 26: pRegion is from caller -- caller could pass NULL. */
    if (!pRegion || !pRegion->pPage) return NULL;
    return pRegion->pPage + sizeof(th8ProtectCanary);
}


/*
 *----------------------------------------------------------------------
 *
 * th8ProtectedCanarySize --
 *
 *	Return the canary size (bytes reserved at the start of the
 *	data page for the integrity canary).
 *
 * Why / How:
 *	Returns sizeof(th8ProtectCanary) so callers can compute the
 *	usable capacity of a protected region as
 *	pageSize - canarySize without hardcoding the canary length.
 *
 * Results:
 *	The canary size in bytes (currently 8).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

size_t
th8ProtectedCanarySize(void)
{
    return sizeof(th8ProtectCanary);
}

#endif /* TH8_ENABLE_CRYPTOGRAPHY */
