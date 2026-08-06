# TH8 Internal API Specification

*Version 1.0 -- date 2026-06-22 (initial split from
`th8_public_c_api_specification.md`).*

This document specifies the **`TH8_INTERNAL`** helper surface
of the TH8 interpreter library.  Functions documented here are
**not** part of the public ABI; they carry the `th8` (lowercase)
prefix in `src/th8.h` rather than the `Th8_` (capitalised) prefix
reserved for `TH8_API` symbols.

Internal helpers are exposed to two consumers:

  *   **Test code** in `src/test/th8_testlib.c` and downstream
      `tests/coverage/*.tcl` test files.  Test code links against
      `libth8stub.a` and resolves internal helpers through
      `Th8_GetInternalStubs(interp)` -- the same mechanism that
      makes the helpers visible to the conformance suite.

  *   **Stub-linked extensions** that opt in to the internal
      surface deliberately.  These extensions accept the
      stability cost of building against `TH8_INTERNAL`:
      individual helpers may be added, removed, or change
      signature between TH8 minor releases.  External embedders
      that need stable behaviour should call the underlying
      platform callback directly
      (`interp->pPlatform->x<Name>(...)`) rather than depending
      on the helper.

**Naming convention.**  The requirements below cite each helper
by its `Th8_<Name>` (capitalised) name as a documentation
convention so the prose matches the spelling test descriptions
use.  In the implementation the symbol is `th8<Name>` (lowercase
first letter) and is declared `TH8_INTERNAL`, not `TH8_API`.
The `Th8_GetInternalStubs` table in `src/th8InternalStubInit.c`
is the authoritative list of which helpers are exposed.

---

## I-1  Lifecycle Helpers

R-32694-45710
:   `Th8_NotifyDeleteInterp` SHALL invoke the platform's `xDeleteInterp` callback, passing the interpreter and the caller-provided context pointer.

---

## I-2  Line-Ending Translation

R-19594-12902
:   `Th8_TranslateLineEndings` SHALL verify that all newlines in the buffer are preceded by carriage return, then translate `\r\n` to `\n` in-place, returning `TH8_ERROR` if any bare `\n` is found.

---

## I-3  Protected Memory Regions

The protected-region allocator backs sensitive-result storage
(`Th8_SetResultSensitive`; see §35.5 "Sensitive Result Storage"
of `th8_public_c_api_specification.md` for the public-facing
contract).  Embedders interact with the protected region only
through that public surface; the allocator entry points below
are internal scaffolding.

R-23343-53449
:   `Th8_ProtectedAlloc` SHALL allocate a data page flanked by guard pages and locked into physical memory via `mlock` or `VirtualLock`.
R-25984-53927
:   `Th8_ProtectedFree` SHALL securely zero the data page before unlocking and releasing the memory.

---

## I-4  Platform Wrappers (libc-equivalent dispatch)

These thin wrappers each dispatch to the corresponding
`Th8_Platform` callback (`xMemmove`, `xStrcmp`, etc.) after
fetching the platform pointer via `Th8_GetPlatform(interp)`.
Each is one or two lines of glue; they exist so that internal
lib code does not have to repeat the platform-pointer
dereference at every call site.

R-14425-19594
:   Th8_Memmove SHALL move n bytes from src to dst (overlapping regions permitted) via the platform's xMemmove callback.
R-42312-03916
:   Th8_Strcmp SHALL compare two NUL-terminated strings via the platform's xStrcmp callback and return a value less than, equal to, or greater than zero.
R-05138-38482
:   Th8_Strchr SHALL locate the first occurrence of byte c in string s via the platform's xStrchr callback.
R-51313-35869
:   Th8_Atoi SHALL convert a NUL-terminated decimal string to an integer via the platform's xAtoi callback.
R-39933-26635
:   Th8_Qsort SHALL sort an array in place via the platform's xQsort callback.
R-36320-28522
:   Th8_Vsnprintf SHALL format output into a buffer via the platform's xVsnprintf callback.
R-20586-05505
:   Th8_Snprintf SHALL be a variadic convenience wrapper that builds a va_list and delegates to Th8_Vsnprintf.

---

## I-5  Channel I/O Routing

The two channel-I/O dispatchers implement the platform-callback-
preference rule that wraps every script-visible read or write.

R-53947-42705
:   Th8_ChannelWrite SHALL prefer the platform's xOutput callback for channel writes, falling back to xChannelControl with TH8_CHANCTL_WRITE only when xOutput is not available.
R-10069-04039
:   Th8_ChannelRead SHALL prefer the platform's xInput callback for channel reads, falling back to xChannelControl with TH8_CHANCTL_READ only when xInput is not available.

---

Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
See the file `license.terms` for usage and redistribution terms
and for a DISCLAIMER OF ALL WARRANTIES.
