/*
 * th8_binary.c -- Binary format / scan plugin for TH8.
 *
 * Implements the Tcl 8.6-compatible `[binary format]` and
 * `[binary scan]` ensemble commands.  Phase 1 of the design
 * (see doc/internal/binary.md): integer specifiers only --
 * c, s, S, t, i, I, n, w, W, m.  Future phases add ASCII /
 * pad / position / float specifiers.
 *
 * Design highlights (per binary.md, kept here as in-file
 * commentary so future contributors do not have to chase
 * the spec):
 *
 *   - Three layers: tokeniser -> driver -> per-op functions.
 *     Each per-op function is straight-line code with at
 *     most one MC/DC decision (the count loop).
 *   - 128-entry op table indexed by the format letter.  A
 *     single table lookup replaces a 10-arm switch, holding
 *     MC/DC growth to roughly one decision per op.
 *   - Single-condition NULL guards.  No `if (!a || !b)`
 *     compounds for argument validation.
 *   - Centralised error helpers (th8BinaryError*) keep the
 *     per-op functions free of error-message branching.
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

#if defined(TH8_ENABLE_BIGINT)
#  include "th8_bigint.h"
#endif

#if defined(TH8_PLUGIN_BINARY)

/*
 *----------------------------------------------------------------------
 *
 * Compile-time invariants.
 *
 *----------------------------------------------------------------------
 */

/*
 * Native byte order detection.  TH8 targets desktop / server
 * platforms where GCC / Clang define __BYTE_ORDER__.  If the
 * macro is absent we default to little-endian (every supported
 * platform is LE today); a future cross-build to a big-endian
 * target would need to define TH8_BINARY_LITTLE_ENDIAN
 * explicitly via the build system.
 */
#  if !defined(TH8_BINARY_LITTLE_ENDIAN)
#    if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__)
#      if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#	define TH8_BINARY_LITTLE_ENDIAN 0
#      else
#	define TH8_BINARY_LITTLE_ENDIAN 1
#      endif
#    else
#      define TH8_BINARY_LITTLE_ENDIAN 1
#    endif
#  endif

/*
 * Cap on the count modifier.  Tcl 8.6 imposes no fixed cap;
 * we use a generous 2^31 - 1 to keep arithmetic in int range
 * and to bound any pathological format string.
 */
#  define TH8_BINARY_MAX_COUNT 0x3FFFFFFF

/*
 * Initial output buffer for [binary format].  Doubles on
 * growth.  64 bytes covers the most common framing headers
 * without an extra alloc.
 */
#  define TH8_BINARY_INIT_ALLOC 64

/*
 *----------------------------------------------------------------------
 *
 * Endianness-explicit byte read/write helpers.  Straight-line
 * shifts; no branches; zero MC/DC decisions.
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8BinaryWriteU8 --
 *
 *	Store the low 8 bits of `v` to `p[0]`.  A single-byte
 *	write, so byte order does not apply; provided for
 *	symmetry with the multi-byte writers.
 *
 * Parameters:
 *	p -- one-byte output buffer (caller guarantees space).
 *	v -- source value; only the low 8 bits are used.
 *
 * Returns:
 *	None.
 *
 * Side effects:
 *	Overwrites `p[0]`.
 *
 *----------------------------------------------------------------------
 */
static void
th8BinaryWriteU8(unsigned char *p, th8_uint64_t v)
{
    p[0] = (unsigned char)(v & 0xFF);
}

/*
 *----------------------------------------------------------------------
 *
 * th8BinaryWriteU16Le --
 *
 *	Store the low 16 bits of `v` to `p[0..1]` in
 *	little-endian byte order (least-significant byte
 *	first).  Straight-line shifts; no branches.
 *
 * Parameters:
 *	p -- two-byte output buffer (caller guarantees space).
 *	v -- source value; only the low 16 bits are used.
 *
 * Returns:
 *	None.
 *
 * Side effects:
 *	Overwrites `p[0]` and `p[1]`.
 *
 *----------------------------------------------------------------------
 */
static void
th8BinaryWriteU16Le(unsigned char *p, th8_uint64_t v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
}

/*
 *----------------------------------------------------------------------
 *
 * th8BinaryWriteU16Be --
 *
 *	Store the low 16 bits of `v` to `p[0..1]` in
 *	big-endian byte order (most-significant byte first).
 *	Straight-line shifts; no branches.
 *
 * Parameters:
 *	p -- two-byte output buffer (caller guarantees space).
 *	v -- source value; only the low 16 bits are used.
 *
 * Returns:
 *	None.
 *
 * Side effects:
 *	Overwrites `p[0]` and `p[1]`.
 *
 *----------------------------------------------------------------------
 */
static void
th8BinaryWriteU16Be(unsigned char *p, th8_uint64_t v)
{
    p[0] = (unsigned char)((v >> 8) & 0xFF);
    p[1] = (unsigned char)(v & 0xFF);
}

/*
 *----------------------------------------------------------------------
 *
 * th8BinaryWriteU32Le --
 *
 *	Store the low 32 bits of `v` to `p[0..3]` in
 *	little-endian byte order.  Straight-line shifts.
 *
 * Parameters:
 *	p -- four-byte output buffer.
 *	v -- source value; only the low 32 bits are used.
 *
 * Returns:
 *	None.
 *
 * Side effects:
 *	Overwrites `p[0..3]`.
 *
 *----------------------------------------------------------------------
 */
static void
th8BinaryWriteU32Le(unsigned char *p, th8_uint64_t v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

/*
 *----------------------------------------------------------------------
 *
 * th8BinaryWriteU32Be --
 *
 *	Store the low 32 bits of `v` to `p[0..3]` in
 *	big-endian byte order.  Straight-line shifts.
 *
 * Parameters:
 *	p -- four-byte output buffer.
 *	v -- source value; only the low 32 bits are used.
 *
 * Returns:
 *	None.
 *
 * Side effects:
 *	Overwrites `p[0..3]`.
 *
 *----------------------------------------------------------------------
 */
static void
th8BinaryWriteU32Be(unsigned char *p, th8_uint64_t v)
{
    p[0] = (unsigned char)((v >> 24) & 0xFF);
    p[1] = (unsigned char)((v >> 16) & 0xFF);
    p[2] = (unsigned char)((v >> 8) & 0xFF);
    p[3] = (unsigned char)(v & 0xFF);
}

/*
 *----------------------------------------------------------------------
 *
 * th8BinaryWriteU64Le --
 *
 *	Store all 64 bits of `v` to `p[0..7]` in little-endian
 *	byte order.  Straight-line shifts.
 *
 * Parameters:
 *	p -- eight-byte output buffer.
 *	v -- source value.
 *
 * Returns:
 *	None.
 *
 * Side effects:
 *	Overwrites `p[0..7]`.
 *
 *----------------------------------------------------------------------
 */
static void
th8BinaryWriteU64Le(unsigned char *p, th8_uint64_t v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
    p[4] = (unsigned char)((v >> 32) & 0xFF);
    p[5] = (unsigned char)((v >> 40) & 0xFF);
    p[6] = (unsigned char)((v >> 48) & 0xFF);
    p[7] = (unsigned char)((v >> 56) & 0xFF);
}

/*
 *----------------------------------------------------------------------
 *
 * th8BinaryWriteU64Be --
 *
 *	Store all 64 bits of `v` to `p[0..7]` in big-endian
 *	byte order.  Straight-line shifts.
 *
 * Parameters:
 *	p -- eight-byte output buffer.
 *	v -- source value.
 *
 * Returns:
 *	None.
 *
 * Side effects:
 *	Overwrites `p[0..7]`.
 *
 *----------------------------------------------------------------------
 */
static void
th8BinaryWriteU64Be(unsigned char *p, th8_uint64_t v)
{
    p[0] = (unsigned char)((v >> 56) & 0xFF);
    p[1] = (unsigned char)((v >> 48) & 0xFF);
    p[2] = (unsigned char)((v >> 40) & 0xFF);
    p[3] = (unsigned char)((v >> 32) & 0xFF);
    p[4] = (unsigned char)((v >> 24) & 0xFF);
    p[5] = (unsigned char)((v >> 16) & 0xFF);
    p[6] = (unsigned char)((v >> 8) & 0xFF);
    p[7] = (unsigned char)(v & 0xFF);
}

/*
 *----------------------------------------------------------------------
 *
 * th8BinaryReadI8 --
 *
 *	Read one signed byte from `p[0]` and sign-extend it to
 *	`th8_int64_t`.  Single-byte access; endianness does not
 *	apply.
 *
 * Parameters:
 *	p -- one-byte input buffer.
 *
 * Returns:
 *	`p[0]` interpreted as a `signed char`, widened to
 *	`th8_int64_t` with sign preserved.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static th8_int64_t
th8BinaryReadI8(const unsigned char *p)
{
    return (th8_int64_t)(signed char)p[0];
}

/*
 *----------------------------------------------------------------------
 *
 * th8BinarySignExtend16 --
 *
 *	Mask `u` to its low 16 bits and sign-extend that
 *	16-bit value to a 64-bit signed integer.  Used by the
 *	little-endian and big-endian 16-bit signed readers to
 *	share their sign-extension logic.
 *
 * Parameters:
 *	u -- input value; only the low 16 bits are inspected.
 *
 * Returns:
 *	Sign-extended `th8_int64_t`: `(int16_t)(u & 0xFFFF)`
 *	widened to 64 bits with sign preserved.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static th8_int64_t
th8BinarySignExtend16(th8_uint64_t u)
{
    u &= 0xFFFFu;
    if (u & 0x8000u) u |= ~(th8_uint64_t)0xFFFFu;
    return (th8_int64_t)u;
}

/*
 *----------------------------------------------------------------------
 *
 * th8BinarySignExtend32 --
 *
 *	Mask `u` to its low 32 bits and sign-extend that
 *	32-bit value to a 64-bit signed integer.  Shared by
 *	the LE/BE 32-bit signed readers.
 *
 * Parameters:
 *	u -- input value; only the low 32 bits are inspected.
 *
 * Returns:
 *	Sign-extended `th8_int64_t`.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static th8_int64_t
th8BinarySignExtend32(th8_uint64_t u)
{
    u &= 0xFFFFFFFFu;
    if (u & 0x80000000u) u |= ~(th8_uint64_t)0xFFFFFFFFu;
    return (th8_int64_t)u;
}

/*
 *----------------------------------------------------------------------
 *
 * th8BinaryReadI16Le --
 *
 *	Read two bytes from `p[0..1]` as a little-endian
 *	signed 16-bit value and sign-extend to 64 bits via
 *	`th8BinarySignExtend16`.
 *
 * Parameters:
 *	p -- two-byte input buffer.
 *
 * Returns:
 *	Sign-extended `th8_int64_t`.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static th8_int64_t
th8BinaryReadI16Le(const unsigned char *p)
{
    th8_uint64_t u = (th8_uint64_t)p[0] | ((th8_uint64_t)p[1] << 8);
    return th8BinarySignExtend16(u);
}

/*
 *----------------------------------------------------------------------
 *
 * th8BinaryReadI16Be --
 *
 *	Read two bytes from `p[0..1]` as a big-endian signed
 *	16-bit value and sign-extend to 64 bits via
 *	`th8BinarySignExtend16`.
 *
 * Parameters:
 *	p -- two-byte input buffer.
 *
 * Returns:
 *	Sign-extended `th8_int64_t`.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static th8_int64_t
th8BinaryReadI16Be(const unsigned char *p)
{
    th8_uint64_t u = ((th8_uint64_t)p[0] << 8) | (th8_uint64_t)p[1];
    return th8BinarySignExtend16(u);
}

/*
 *----------------------------------------------------------------------
 *
 * th8BinaryReadI32Le --
 *
 *	Read four bytes from `p[0..3]` as a little-endian
 *	signed 32-bit value and sign-extend to 64 bits via
 *	`th8BinarySignExtend32`.
 *
 * Parameters:
 *	p -- four-byte input buffer.
 *
 * Returns:
 *	Sign-extended `th8_int64_t`.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static th8_int64_t
th8BinaryReadI32Le(const unsigned char *p)
{
    th8_uint64_t u = (th8_uint64_t)p[0] | ((th8_uint64_t)p[1] << 8) |
                     ((th8_uint64_t)p[2] << 16) | ((th8_uint64_t)p[3] << 24);
    return th8BinarySignExtend32(u);
}

/*
 *----------------------------------------------------------------------
 *
 * th8BinaryReadI32Be --
 *
 *	Read four bytes from `p[0..3]` as a big-endian signed
 *	32-bit value and sign-extend to 64 bits via
 *	`th8BinarySignExtend32`.
 *
 * Parameters:
 *	p -- four-byte input buffer.
 *
 * Returns:
 *	Sign-extended `th8_int64_t`.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static th8_int64_t
th8BinaryReadI32Be(const unsigned char *p)
{
    th8_uint64_t u = ((th8_uint64_t)p[0] << 24) | ((th8_uint64_t)p[1] << 16) |
                     ((th8_uint64_t)p[2] << 8) | (th8_uint64_t)p[3];
    return th8BinarySignExtend32(u);
}

/*
 *----------------------------------------------------------------------
 *
 * th8BinaryReadI64Le --
 *
 *	Read eight bytes from `p[0..7]` as a little-endian
 *	signed 64-bit value.  No sign extension is needed --
 *	the bit pattern is already 64 bits wide -- so the cast
 *	is a re-interpretation of the unsigned accumulator as
 *	signed.
 *
 * Parameters:
 *	p -- eight-byte input buffer.
 *
 * Returns:
 *	`p[0..7]` interpreted as a signed 64-bit integer.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static th8_int64_t
th8BinaryReadI64Le(const unsigned char *p)
{
    th8_uint64_t u = (th8_uint64_t)p[0] | ((th8_uint64_t)p[1] << 8) |
                     ((th8_uint64_t)p[2] << 16) | ((th8_uint64_t)p[3] << 24) |
                     ((th8_uint64_t)p[4] << 32) | ((th8_uint64_t)p[5] << 40) |
                     ((th8_uint64_t)p[6] << 48) | ((th8_uint64_t)p[7] << 56);
    return (th8_int64_t)u;
}

/*
 *----------------------------------------------------------------------
 *
 * th8BinaryReadI64Be --
 *
 *	Read eight bytes from `p[0..7]` as a big-endian signed
 *	64-bit value.  Like `th8BinaryReadI64Le`, no
 *	sign-extension step is required.
 *
 * Parameters:
 *	p -- eight-byte input buffer.
 *
 * Returns:
 *	`p[0..7]` interpreted as a signed 64-bit integer.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static th8_int64_t
th8BinaryReadI64Be(const unsigned char *p)
{
    th8_uint64_t u = ((th8_uint64_t)p[0] << 56) | ((th8_uint64_t)p[1] << 48) |
                     ((th8_uint64_t)p[2] << 40) | ((th8_uint64_t)p[3] << 32) |
                     ((th8_uint64_t)p[4] << 24) | ((th8_uint64_t)p[5] << 16) |
                     ((th8_uint64_t)p[6] << 8) | (th8_uint64_t)p[7];
    return (th8_int64_t)u;
}

/*
 *----------------------------------------------------------------------
 *
 * Phase 3 -- IEEE 754 binary32 / binary64 helpers.
 *
 * The transport layer is the existing U32/U64 read/write
 * helpers; the helpers here add the bitcast and the NaN
 * canonicalisation.  Compile-time assertions enforce the
 * IEEE 754 layout assumptions: if a future target violates
 * them, the build fails loudly rather than silently
 * corrupting binary data.
 *
 *----------------------------------------------------------------------
 */

typedef int Th8_BinaryAssertFloat32[(sizeof(float) == 4) ? 1 : -1];
typedef int Th8_BinaryAssertFloat64[(sizeof(double) == 8) ? 1 : -1];

/*
 * Canonical NaN bit patterns.  Single qNaN is `0x7FC00000`
 * (sign=0, exp=all-ones, MSB of mantissa=1, all other
 * mantissa bits zero).  Double is the analogous
 * `0x7FF8000000000000`.
 */
#  define TH8_BINARY_NAN_F32 ((th8_uint64_t)0x7FC00000u)
#  define TH8_BINARY_NAN_F64 ((th8_uint64_t)0x7FF8000000000000ULL)

/*
 *----------------------------------------------------------------------
 *
 * th8BinaryIsNaN32 --
 *
 *	Classify an IEEE 754 binary32 bit pattern as NaN.
 *	A pattern is a NaN iff:
 *	  * the exponent field (bits 30..23) is all-ones
 *	    (`0xFF`), AND
 *	  * the mantissa field (bits 22..0) is non-zero
 *	    (else the pattern is +/-Infinity).
 *
 *	The sign bit is irrelevant for the classification.
 *	Mirror of `th8BinaryIsNaN64`.
 *
 * Parameters:
 *	bits -- 32-bit pattern held in a 64-bit accumulator
 *		(typically from `th8BinaryFloat32Bits`); only the
 *		low 32 bits are inspected.
 *
 * Returns:
 *	1 if `bits` denotes a NaN; 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static int
th8BinaryIsNaN32(th8_uint64_t bits)
{
    if ((bits & 0x7F800000u) != 0x7F800000u) return 0;
    if ((bits & 0x007FFFFFu) == 0) return 0;
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * th8BinaryIsNaN64 --
 *
 *	Classify an IEEE 754 binary64 bit pattern as NaN.
 *	A pattern is a NaN iff:
 *	  * the exponent field (bits 62..52) is all-ones
 *	    (`0x7FF`), AND
 *	  * the mantissa field (bits 51..0) is non-zero
 *	    (else the pattern is +/-Infinity).
 *
 *	The sign bit is irrelevant for the classification.
 *	Mirror of `th8BinaryIsNaN32`.
 *
 * Parameters:
 *	bits -- 64-bit pattern (typically obtained via
 *		`th8BinaryFloat64Bits`).
 *
 * Returns:
 *	1 if `bits` denotes a NaN; 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static int
th8BinaryIsNaN64(th8_uint64_t bits)
{
    if ((bits & 0x7FF0000000000000ULL) != 0x7FF0000000000000ULL) return 0;
    if ((bits & 0x000FFFFFFFFFFFFFULL) == 0) return 0;
    return 1;
}

/*
 * Type-punning via union (well-defined in C11 and de-facto
 * supported on every compiler TH8 builds with).  Replaces the
 * `Th8_Memcpy(NULL, ...)` calls used in an earlier draft --
 * Th8_Memcpy is interp-driven via the platform's xMemcpy and
 * silently no-ops when interp is NULL, which left the buffer
 * uninitialised.
 */
typedef union {
    float f;
    th8_uint64_t u;
} Th8_BinaryF32Bits;

typedef union {
    double d;
    th8_uint64_t u;
} Th8_BinaryF64Bits;

/*
 *----------------------------------------------------------------------
 *
 * th8BinaryFloat32Bits --
 *
 *	Extract the IEEE 754 binary32 bit pattern from a
 *	`float` and canonicalise any NaN to
 *	`TH8_BINARY_NAN_F32` (the standard quiet-NaN
 *	bit pattern `0x7FC00000`).  Canonicalisation
 *	makes `[binary format f]` output stable across hosts
 *	that may differ in which NaN payload their FPU
 *	produces.
 *
 *	Type punning is done through the `Th8_BinaryF32Bits`
 *	union (well-defined in C11 and de-facto supported by
 *	every TH8 toolchain) instead of `memcpy`, which avoids
 *	any dependency on `interp` state.
 *
 * Parameters:
 *	f -- source IEEE 754 binary32 value.
 *
 * Returns:
 *	The 32 low bits of the IEEE 754 representation, in a
 *	64-bit accumulator.  NaN payloads collapse to the
 *	canonical pattern.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static th8_uint64_t
th8BinaryFloat32Bits(float f)
{
    Th8_BinaryF32Bits cvt;

    cvt.u = 0;
    cvt.f = f;
    cvt.u &= 0xFFFFFFFFu;
    if (th8BinaryIsNaN32(cvt.u)) cvt.u = TH8_BINARY_NAN_F32;
    return cvt.u;
}

/*
 *----------------------------------------------------------------------
 *
 * th8BinaryBitsToFloat32 --
 *
 *	Reinterpret the low 32 bits of `bits` as an IEEE 754
 *	binary32 `float` via the `Th8_BinaryF32Bits` union.
 *	Inverse of `th8BinaryFloat32Bits`; no canonicalisation
 *	step is needed on the read side because consumers
 *	expect the exact pattern written.
 *
 * Parameters:
 *	bits -- 64-bit input; only the low 32 bits are used.
 *
 * Returns:
 *	The `float` value whose IEEE 754 binary32 representation
 *	matches `bits & 0xFFFFFFFFu`.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static float
th8BinaryBitsToFloat32(th8_uint64_t bits)
{
    Th8_BinaryF32Bits cvt;

    cvt.u = bits & 0xFFFFFFFFu;
    return cvt.f;
}

/*
 *----------------------------------------------------------------------
 *
 * th8BinaryFloat64Bits --
 *
 *	Extract the IEEE 754 binary64 bit pattern from a
 *	`double` and canonicalise any NaN to
 *	`TH8_BINARY_NAN_F64` (`0x7FF8000000000000`).
 *	Type punning through `Th8_BinaryF64Bits`.
 *
 * Parameters:
 *	d -- source IEEE 754 binary64 value.
 *
 * Returns:
 *	The 64-bit IEEE 754 representation.  NaN payloads
 *	collapse to the canonical pattern.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static th8_uint64_t
th8BinaryFloat64Bits(double d)
{
    Th8_BinaryF64Bits cvt;

    cvt.u = 0;
    cvt.d = d;
    if (th8BinaryIsNaN64(cvt.u)) cvt.u = TH8_BINARY_NAN_F64;
    return cvt.u;
}

/*
 *----------------------------------------------------------------------
 *
 * th8BinaryBitsToFloat64 --
 *
 *	Reinterpret all 64 bits of `bits` as an IEEE 754
 *	binary64 `double` via the `Th8_BinaryF64Bits` union.
 *	Inverse of `th8BinaryFloat64Bits`.
 *
 * Parameters:
 *	bits -- 64-bit IEEE 754 binary64 representation.
 *
 * Returns:
 *	The `double` value whose IEEE 754 representation
 *	matches `bits`.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static double
th8BinaryBitsToFloat64(th8_uint64_t bits)
{
    Th8_BinaryF64Bits cvt;

    cvt.u = bits;
    return cvt.d;
}

/*
 *----------------------------------------------------------------------
 *
 * Op table.  Each row describes one format-letter specifier:
 * its element byte width, signedness, byte order, and the
 * `*` acceptance flag.  All work flows through three
 * dispatch functions (writeInt, readInt) so per-op functions
 * stay straight-line.
 *
 *----------------------------------------------------------------------
 */

#  define TH8_BINARY_LE 0  /* little-endian */
#  define TH8_BINARY_BE 1  /* big-endian */

#  if TH8_BINARY_LITTLE_ENDIAN
#    define TH8_BINARY_NATIVE TH8_BINARY_LE
#  else
#    define TH8_BINARY_NATIVE TH8_BINARY_BE
#  endif

/*
 * Op kind selector.  The driver dispatches on this to pick the
 * right per-op routine.  Each kind has straight-line per-op code;
 * the integer kind is the only one that walks an inner count
 * loop in the driver itself.
 */
#  define TH8_BINARY_KIND_NONE    0
#  define TH8_BINARY_KIND_INT     1
#  define TH8_BINARY_KIND_STR_NUL 2 /* `a` */
#  define TH8_BINARY_KIND_STR_SP  3 /* `A` */
#  define TH8_BINARY_KIND_BITS_LO 4 /* `b` */
#  define TH8_BINARY_KIND_BITS_HI 5 /* `B` */
#  define TH8_BINARY_KIND_HEX_LO  6 /* `h` */
#  define TH8_BINARY_KIND_HEX_HI  7 /* `H` */
#  define TH8_BINARY_KIND_PAD     8 /* `x` */
#  define TH8_BINARY_KIND_BACK    9 /* `X` */
#  define TH8_BINARY_KIND_SEEK    10 /* `@` */
#  define TH8_BINARY_KIND_FLOAT   11 /* `f` / `r` / `R` -- IEEE 754 single */
#  define TH8_BINARY_KIND_DOUBLE  12 /* `d` / `q` / `Q` -- IEEE 754 double */
/*
 * `BIGINT` is not a separate format-letter; it is the
 * routing tag chosen at dispatch time when the integer-
 * specifier input is detected as an arbitrary-precision
 * (bignum) literal and TH8_ENABLE_BIGINT + the runtime
 * Th8_IsBigintEnabled(interp) gate are both on.  See
 * th8BinaryParseTrunc + binary_format_command for the
 * dispatch.
 */
#  define TH8_BINARY_KIND_BIGINT 13
/* TH8_BINARY_KIND_BIGINT_FIXED is the primary kind for the
 * arbitrary-width `j` (little-endian) and `J` (big-endian)
 * specifiers.  nWidth on the op-table entry is ignored; the
 * width comes from the parsed count (or, for `j*`/`J*`, from
 * the input value's natural two's-complement size). */
#  define TH8_BINARY_KIND_BIGINT_FIXED 14

typedef struct Th8_BinaryOpDef {
    char letter; /* The format-letter; 0 means no op. */
    unsigned char kind; /* TH8_BINARY_KIND_*; 0 means no op. */
    unsigned char nWidth; /* INT: element width in bytes (1, 2, 4, 8). */
    unsigned char eOrder; /* INT: TH8_BINARY_LE or TH8_BINARY_BE. */
    unsigned char bStarFmt; /* 1 if `*` count is accepted on format. */
    unsigned char bStarScn; /* 1 if `*` count is accepted on scan. */
} Th8_BinaryOpDef;

/*
 * 128-entry table indexed by the lower 7 bits of the letter.
 * Letters outside [0, 127] (i.e. with the high bit set) are
 * always invalid -- the tokeniser rejects them before lookup.
 *
 * Star-acceptance flags (bStarFmt / bStarScn) follow the
 * binary.md spec: `*` is a scan-only count modifier for the
 * variable-length specifiers; on format it is rejected.  The
 * `x` / `X` / `@` cursor ops accept no `*` at all.
 */
static const Th8_BinaryOpDef aBinaryOps[128] = {
    ['c'] = {'c', TH8_BINARY_KIND_INT, 1, TH8_BINARY_NATIVE, 1, 1},
    ['s'] = {'s', TH8_BINARY_KIND_INT, 2, TH8_BINARY_LE, 1, 1},
    ['S'] = {'S', TH8_BINARY_KIND_INT, 2, TH8_BINARY_BE, 1, 1},
    ['t'] = {'t', TH8_BINARY_KIND_INT, 2, TH8_BINARY_NATIVE, 1, 1},
    ['i'] = {'i', TH8_BINARY_KIND_INT, 4, TH8_BINARY_LE, 1, 1},
    ['I'] = {'I', TH8_BINARY_KIND_INT, 4, TH8_BINARY_BE, 1, 1},
    ['n'] = {'n', TH8_BINARY_KIND_INT, 4, TH8_BINARY_NATIVE, 1, 1},
    ['w'] = {'w', TH8_BINARY_KIND_INT, 8, TH8_BINARY_LE, 1, 1},
    ['W'] = {'W', TH8_BINARY_KIND_INT, 8, TH8_BINARY_BE, 1, 1},
    ['m'] = {'m', TH8_BINARY_KIND_INT, 8, TH8_BINARY_NATIVE, 1, 1},
    ['a'] = {'a', TH8_BINARY_KIND_STR_NUL, 0, 0, 1, 1},
    ['A'] = {'A', TH8_BINARY_KIND_STR_SP, 0, 0, 1, 1},
    ['b'] = {'b', TH8_BINARY_KIND_BITS_LO, 0, 0, 1, 1},
    ['B'] = {'B', TH8_BINARY_KIND_BITS_HI, 0, 0, 1, 1},
    ['h'] = {'h', TH8_BINARY_KIND_HEX_LO, 0, 0, 1, 1},
    ['H'] = {'H', TH8_BINARY_KIND_HEX_HI, 0, 0, 1, 1},
    ['x'] = {'x', TH8_BINARY_KIND_PAD, 0, 0, 0, 0},
    ['X'] = {'X', TH8_BINARY_KIND_BACK, 0, 0, 1, 0},
    ['@'] = {'@', TH8_BINARY_KIND_SEEK, 0, 0, 0, 0},
    ['f'] = {'f', TH8_BINARY_KIND_FLOAT, 4, TH8_BINARY_NATIVE, 1, 1},
    ['r'] = {'r', TH8_BINARY_KIND_FLOAT, 4, TH8_BINARY_LE, 1, 1},
    ['R'] = {'R', TH8_BINARY_KIND_FLOAT, 4, TH8_BINARY_BE, 1, 1},
    ['d'] = {'d', TH8_BINARY_KIND_DOUBLE, 8, TH8_BINARY_NATIVE, 1, 1},
    ['q'] = {'q', TH8_BINARY_KIND_DOUBLE, 8, TH8_BINARY_LE, 1, 1},
    ['Q'] = {'Q', TH8_BINARY_KIND_DOUBLE, 8, TH8_BINARY_BE, 1, 1},
    ['j'] = {'j', TH8_BINARY_KIND_BIGINT_FIXED, 0, TH8_BINARY_LE, 1, 1},
    ['J'] = {'J', TH8_BINARY_KIND_BIGINT_FIXED, 0, TH8_BINARY_BE, 1, 1},
};

/*
 * th8BinaryOpFor -- look up the op for a format letter.
 * Returns a pointer to the matching aBinaryOps entry, or NULL
 * if the letter is not a Phase 1 op.  One-decision lookup.
 */
static const Th8_BinaryOpDef *
th8BinaryOpFor(int letter)
{
    const Th8_BinaryOpDef *p;

    if (letter < 0) return NULL;
    if (letter > 127) return NULL;
    p = &aBinaryOps[letter];
    if (p->kind == TH8_BINARY_KIND_NONE) return NULL;
    return p;
}

/*
 *----------------------------------------------------------------------
 *
 * Error helpers.  Centralised here so per-op functions never
 * touch Th8_SetResultStatic / Th8_ErrorMessage directly.
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8BinaryErrBadFormat --
 *
 *	Set the interpreter result to the standard "bad field
 *	specifier" diagnostic naming the offending format letter
 *	and return `TH8_ERROR`.  Raised by `th8BinaryNextToken`
 *	when a format byte has no op-table entry.  A
 *	non-printable letter is rendered as `?` so the message
 *	stays clean for control bytes.
 *
 * Parameters:
 *	interp -- live interpreter (receives the message).
 *	letter -- the rejected format letter (byte value).
 *
 * Returns:
 *	`TH8_ERROR` unconditionally.
 *
 * Side effects:
 *	Sets the interpreter result.
 *
 *----------------------------------------------------------------------
 */
static int
th8BinaryErrBadFormat(Th8_Interp *interp, int letter)
{
    char buf[40];
    int n;

    n = th8Snprintf(
        interp, buf, sizeof(buf), "bad field specifier \"%c\"",
        (letter >= 32 && letter < 127) ? letter : '?');
    if (n < 0) n = 0;
    if ((size_t)n >= sizeof(buf)) n = (int)sizeof(buf) - 1;
    Th8_SetResult(interp, buf, (size_t)n);
    return TH8_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * th8BinaryErrNotEnoughArgs --
 *
 *	Set the interpreter result to the standard "not enough
 *	arguments for all format specifiers" diagnostic and
 *	return `TH8_ERROR`.  Centralised so every caller emits
 *	an identical message.
 *
 * Parameters:
 *	interp -- live interpreter (receives the message).
 *
 * Returns:
 *	`TH8_ERROR` unconditionally.
 *
 * Side effects:
 *	Sets the interpreter result.
 *
 *----------------------------------------------------------------------
 */
static int
th8BinaryErrNotEnoughArgs(Th8_Interp *interp)
{
    Th8_SetResultStatic(
        interp, "not enough arguments for all format specifiers", TH8_NOLEN);
    return TH8_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * th8BinaryErrCountOverflow --
 *
 *	Set the interpreter result to "binary count value out
 *	of range" and return `TH8_ERROR`.  Raised by
 *	`th8BinaryNextToken` when a decimal count following a
 *	format letter exceeds `TH8_BINARY_MAX_COUNT`.
 *
 * Parameters:
 *	interp -- live interpreter (receives the message).
 *
 * Returns:
 *	`TH8_ERROR` unconditionally.
 *
 * Side effects:
 *	Sets the interpreter result.
 *
 *----------------------------------------------------------------------
 */
static int
th8BinaryErrCountOverflow(Th8_Interp *interp)
{
    Th8_SetResultStatic(interp, "binary count value out of range", TH8_NOLEN);
    return TH8_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * th8BinaryErrStarFormat --
 *
 *	Set the interpreter result to "cannot use \"*\" in
 *	binary format field" and return `TH8_ERROR`.  Raised
 *	when a `*` count appears with a format letter that
 *	cannot consume an unknown-length list (the op table's
 *	`acceptStar` flag is 0).
 *
 * Parameters:
 *	interp -- live interpreter (receives the message).
 *
 * Returns:
 *	`TH8_ERROR` unconditionally.
 *
 * Side effects:
 *	Sets the interpreter result.
 *
 *----------------------------------------------------------------------
 */
static int
th8BinaryErrStarFormat(Th8_Interp *interp)
{
    Th8_SetResultStatic(
        interp, "cannot use \"*\" in binary format field", TH8_NOLEN);
    return TH8_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * Tokeniser.  Reads one (letter, count) pair from the format
 * string.  Returns:
 *
 *   TH8_OK with *pLetter == 0 if the end of the format string
 *     was reached.
 *   TH8_OK with *pLetter != 0 and *pCount filled on a valid
 *     specifier.  *pBStar is set to 1 if the count was `*`.
 *   TH8_ERROR with the interp result set on a parse error.
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8BinaryIsDigit --
 *
 *	ASCII decimal-digit predicate used by the binary-format
 *	tokeniser to parse the count modifier that may follow a
 *	format letter.
 *
 * Parameters:
 *	c -- byte value (typically promoted from `unsigned char`).
 *
 * Returns:
 *	1 if `c` is one of `'0'..'9'`; 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static int
th8BinaryIsDigit(int c)
{
    return c >= '0' && c <= '9';
}

/*
 *----------------------------------------------------------------------
 *
 * th8BinaryIsSpace --
 *
 *	ASCII whitespace predicate used by the binary-format
 *	tokeniser to skip inter-field padding.  Recognises the
 *	four ASCII whitespace characters that may appear in a
 *	user-supplied `[binary format]` / `[binary scan]`
 *	format string: space, tab, newline, carriage return.
 *
 *	The tab/newline/CR arms are split out (rather than
 *	folded into a single range test) per FINDINGS.md
 *	Finding 005 sec. 5b -- the conformance corpus only
 *	exercises the space arm, so each non-space arm is
 *	intrinsic-dead at the C-pair level but kept because
 *	the format string is user-supplied and may
 *	legitimately contain any of these characters.
 *
 * Parameters:
 *	c -- byte value (typically promoted from `unsigned char`).
 *
 * Returns:
 *	1 if `c` is one of `' '`, `'\t'`, `'\n'`, `'\r'`;
 *	0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static int
th8BinaryIsSpace(int c)
{
    if (c == ' ') return 1;
    if (c == '\t') return 1;
    if (c == '\n') return 1;
    if (c == '\r') return 1;
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * th8BinaryNextToken --
 *
 *	Read one `(letter, count)` pair from the binary format
 *	string `zFmt[*piPos..nFmt)` and advance `*piPos` past
 *	the consumed bytes:
 *
 *	  1. Skip leading whitespace via `th8BinaryIsSpace`.
 *	  2. If at end-of-string, report no-more-tokens by
 *	     setting `*pLetter = 0` and `*pCount = 0`.
 *	  3. Otherwise the next byte is the format letter;
 *	     look it up in the op table.  Unknown letters
 *	     route through `th8BinaryErrBadFormat`.
 *	  4. Default `*pCount = 1`.  A `*` count sets
 *	     `*pBStar = 1` and keeps `*pCount = 0`; a decimal
 *	     digit string parses as the count (rejecting any
 *	     value greater than `TH8_BINARY_MAX_COUNT` via
 *	     `th8BinaryErrCountOverflow`).
 *
 * Parameters:
 *	interp  -- live interpreter (for error messages).
 *	zFmt    -- format string buffer.
 *	nFmt    -- format-string byte length.
 *	piPos   -- in/out cursor into `zFmt`.
 *	pLetter -- receives the format letter (or 0 at EOS).
 *	pCount  -- receives the count (`0` when `*` is used or at EOS).
 *	pBStar  -- receives 1 if `*` count, 0 otherwise.
 *
 * Returns:
 *	`TH8_OK` on a parsed token (or at EOS).
 *	`TH8_ERROR` with the interpreter result set on a parse
 *	error (unknown letter, count overflow).
 *
 * Side effects:
 *	Mutates `*piPos`, `*pLetter`, `*pCount`, `*pBStar`.
 *	May set the interpreter result on the error path.
 *
 *----------------------------------------------------------------------
 */
static int
th8BinaryNextToken(
    Th8_Interp *interp,
    const char *zFmt,
    size_t nFmt,
    size_t *piPos,
    int *pLetter,
    th8_int64_t *pCount,
    int *pBStar)
{
    size_t i = *piPos;
    int c;
    const Th8_BinaryOpDef *pOp;

    /* Skip leading whitespace. */
    while (i < nFmt) {
	if (!th8BinaryIsSpace((unsigned char)zFmt[i])) break;
	i++;
    }
    if (i >= nFmt) {
	*pLetter = 0;
	*pCount = 0;
	*pBStar = 0;
	*piPos = i;
	return TH8_OK;
    }

    c = (unsigned char)zFmt[i];
    pOp = th8BinaryOpFor(c);
    if (!pOp) return th8BinaryErrBadFormat(interp, c);
    i++;

    /* Default count = 1. */
    *pLetter = c;
    *pCount = 1;
    *pBStar = 0;

    if (i >= nFmt) {
	*piPos = i;
	return TH8_OK;
    }
    if (zFmt[i] == '*') {
	i++;
	*pCount = 0;
	*pBStar = 1;
    } else if (th8BinaryIsDigit((unsigned char)zFmt[i])) {
	th8_int64_t n = 0;

	while (i < nFmt) {
	    int d = (unsigned char)zFmt[i];
	    if (!th8BinaryIsDigit(d)) break;
	    if (n > (th8_int64_t)TH8_BINARY_MAX_COUNT / 10) {
		return th8BinaryErrCountOverflow(interp);
	    }
	    n = n * 10 + (d - '0');
	    if (n > (th8_int64_t)TH8_BINARY_MAX_COUNT) {
		return th8BinaryErrCountOverflow(interp);
	    }
	    i++;
	}
	*pCount = n;
    }
    *piPos = i;
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Format-direction implementation.
 *
 *----------------------------------------------------------------------
 */

typedef struct Th8_BinaryFormatBuf {
    unsigned char *zBuf;
    size_t iCur; /* current write cursor */
    size_t nUsed; /* high-water mark = result length */
    size_t nAlloc;
} Th8_BinaryFormatBuf;

/*
 * th8BinaryReserveAt -- ensure pBuf->zBuf has at least iPos + nMore
 * bytes of allocated space, growing by doubling.  If iPos > nUsed
 * (i.e. an `@` seek opened a gap), zero-fill the gap before
 * returning.  Caller then writes at zBuf[iPos..iPos+nMore].
 */
static int
th8BinaryReserveAt(
    Th8_Interp *interp,
    Th8_BinaryFormatBuf *pBuf,
    size_t iPos,
    size_t nMore)
{
    size_t nNeed;
    size_t nNew;
    unsigned char *zNew;

    if (TH8_SAFE_ADD_SIZE(iPos, nMore, &nNeed)) {
	Th8_SetResultStatic(interp, "binary format: overflow", TH8_NOLEN);
	return TH8_ERROR;
    }
    if (nNeed > pBuf->nAlloc) {
	nNew = pBuf->nAlloc;
	if (nNew == 0) nNew = TH8_BINARY_INIT_ALLOC;
	while (nNew < nNeed) {
	    if (nNew > (size_t)-1 / 2) {
		Th8_SetResultStatic(
		    interp, "binary format: alloc overflow", TH8_NOLEN);
		return TH8_ERROR;
	    }
	    nNew *= 2;
	}
	zNew = (unsigned char *)TH8_ALLOC(interp, nNew);
	if (!zNew) return TH8_ERROR;
	if (pBuf->zBuf) {
	    Th8_Memcpy(interp, zNew, pBuf->zBuf, pBuf->nUsed);
	    Th8_Free(interp, pBuf->zBuf);
	}
	pBuf->zBuf = zNew;
	pBuf->nAlloc = nNew;
    }
    /* Zero-fill any gap between nUsed and iPos. */
    if (iPos > pBuf->nUsed) {
	Th8_Memset(interp, pBuf->zBuf + pBuf->nUsed, 0, iPos - pBuf->nUsed);
	pBuf->nUsed = iPos;
    }
    return TH8_OK;
}

/*
 * th8BinaryReserve -- legacy single-cursor reserve, used by the
 * integer fast path.  Reserves nMore bytes at pBuf->iCur and
 * updates nUsed when the write extends past it.  Callers do the
 * actual write after this returns, then advance pBuf->iCur and
 * pBuf->nUsed themselves.
 */
static int
th8BinaryReserve(Th8_Interp *interp, Th8_BinaryFormatBuf *pBuf, size_t nMore)
{
    return th8BinaryReserveAt(interp, pBuf, pBuf->iCur, nMore);
}

#  if defined(TH8_ENABLE_BIGINT)
/*
 * Expression opcode constants used by th8BigintArith.  Mirror
 * the file-private definitions in src/th8_bigint.c.
 */
#    define TH8_BINARY_OP_BITWISE_AND 21

/*
 * th8BinaryParseBigint -- truncate an arbitrary-precision
 * integer string modulo 2^64 using the native bigint
 * arithmetic engine (`th8BigintArith` with bitwise AND).
 * The caller has already detected via th8IsBigint that the
 * string is a valid integer too large for Th8_ToWideInt; this
 * routine computes (value & 0xFFFFFFFFFFFFFFFF) symbolically
 * as a bigint, then re-parses the result -- which is now in
 * 64-bit range -- via Th8_ToWideInt.  This is the
 * KIND_BIGINT-dispatch path: no Th8_Eval round-trip, no
 * temporary variables, no nested expression parser.
 *
 * Runtime gate: caller checks Th8_IsBigintEnabled(interp)
 * before invoking this routine, so we never reach
 * th8BigintArith with bigint disabled.  Compile gate:
 * TH8_ENABLE_BIGINT.
 */
static int
th8BinaryParseBigint(
    Th8_Interp *interp,
    const char *zArg,
    size_t nArg,
    th8_int64_t *pV)
{
    static const char zMask[] = "18446744073709551615"; /* 2^64 - 1 */
    const char *zRes;
    size_t nRes;
    int rc;

    rc = th8BigintArith(
        interp, zArg, nArg, zMask, sizeof(zMask) - 1,
        TH8_BINARY_OP_BITWISE_AND);
    if (rc != TH8_OK) return rc;
    zRes = Th8_GetResult(interp, &nRes);
    return Th8_ToWideInt(interp, zRes, nRes, pV);
}
#  endif /* TH8_ENABLE_BIGINT */

/*
 * th8BinaryParseTrunc -- parse `zArg` as an integer, accepting
 * Th8_ToWideInt-compatible inputs on the fast path AND
 * bignum inputs that exceed the 64-bit range via the
 * first-class KIND_BIGINT dispatch.
 *
 * Routing:
 *
 *   1. Fast path: Th8_ToWideInt.  Zero bigint overhead for the
 *      common case.
 *
 *   2. If the fast path failed, ask th8IsBigint whether the
 *      input is a *valid* integer that simply exceeded
 *      64-bit range (vs. a syntactically malformed number).
 *      The branch is compile-gated by TH8_ENABLE_BIGINT and
 *      runtime-gated by Th8_IsBigintEnabled(interp).
 *
 *   3. If yes, dispatch into th8BinaryParseBigint (the
 *      KIND_BIGINT routine): truncate modulo 2^64 via
 *      th8BigintArith and re-parse the now-in-range result.
 *
 *   4. If bigint is disabled or the input is genuinely
 *      malformed, restore the original Th8_ToWideInt error
 *      message and return TH8_ERROR.
 */
static int
th8BinaryParseTrunc(
    Th8_Interp *interp,
    const char *zArg,
    size_t nArg,
    th8_int64_t *pV)
{
    /* Step 1: fast path. */
    if (Th8_ToWideInt(interp, zArg, nArg, pV) == TH8_OK) return TH8_OK;

#  if defined(TH8_ENABLE_BIGINT)
    /* Step 2: detect bigint -- compile- and runtime-gated. */
    if (Th8_IsBigintEnabled(interp) && th8IsBigint(interp, zArg, nArg)) {
	const char *zSavedErr;
	size_t nSavedErr;
	char *zCopy;
	int rc;

	/* Save the Th8_ToWideInt error message so it can be
	 * restored if the bigint path also fails. */
	zSavedErr = Th8_GetResult(interp, &nSavedErr);
	zCopy = (char *)TH8_ALLOC_STR(interp, nSavedErr);
	if (!zCopy) return TH8_ERROR;
	Th8_Memcpy(interp, zCopy, zSavedErr, nSavedErr);
	zCopy[nSavedErr] = '\0';

	/* Step 3: KIND_BIGINT dispatch. */
	rc = th8BinaryParseBigint(interp, zArg, nArg, pV);
	if (rc != TH8_OK) {
	    Th8_SetResult(interp, zCopy, nSavedErr);
	}
	Th8_Free(interp, zCopy);
	return rc;
    }
#  endif /* TH8_ENABLE_BIGINT */

    /* Step 4: malformed integer; Th8_ToWideInt's error stays. */
    return TH8_ERROR;
}

/*
 * th8BinaryAdvanceCursor -- advance pBuf->iCur by n bytes and
 * bump nUsed if the write extended past it.
 */
static void
th8BinaryAdvanceCursor(Th8_BinaryFormatBuf *pBuf, size_t n)
{
    pBuf->iCur += n;
    if (pBuf->iCur > pBuf->nUsed) pBuf->nUsed = pBuf->iCur;
}

/*
 * th8BinaryWriteInt -- emit one integer value at the buffer's
 * cursor.  Caller has already reserved pOp->nWidth bytes.
 * Straight-line code; zero MC/DC decisions.
 */
static void
th8BinaryWriteInt(
    Th8_BinaryFormatBuf *pBuf,
    const Th8_BinaryOpDef *pOp,
    th8_int64_t v)
{
    unsigned char *p = pBuf->zBuf + pBuf->iCur;
    th8_uint64_t u = (th8_uint64_t)v;

    switch (pOp->nWidth * 2 + pOp->eOrder) {
    case 1 * 2 + TH8_BINARY_LE:
    case 1 * 2 + TH8_BINARY_BE:
	th8BinaryWriteU8(p, u);
	break;
    case 2 * 2 + TH8_BINARY_LE:
	th8BinaryWriteU16Le(p, u);
	break;
    case 2 * 2 + TH8_BINARY_BE:
	th8BinaryWriteU16Be(p, u);
	break;
    case 4 * 2 + TH8_BINARY_LE:
	th8BinaryWriteU32Le(p, u);
	break;
    case 4 * 2 + TH8_BINARY_BE:
	th8BinaryWriteU32Be(p, u);
	break;
    case 8 * 2 + TH8_BINARY_LE:
	th8BinaryWriteU64Le(p, u);
	break;
    case 8 * 2 + TH8_BINARY_BE:
	th8BinaryWriteU64Be(p, u);
	break;
    }
    th8BinaryAdvanceCursor(pBuf, pOp->nWidth);
}

/*
 * th8BinaryWriteFloat32 -- write a single-precision IEEE 754
 * value at the buffer cursor.  NaN inputs are normalised to
 * the canonical pattern via th8BinaryFloat32Bits.
 */
static void
th8BinaryWriteFloat32(
    Th8_BinaryFormatBuf *pBuf,
    unsigned char eOrder,
    double d)
{
    unsigned char *p = pBuf->zBuf + pBuf->iCur;
    th8_uint64_t bits = th8BinaryFloat32Bits((float)d);

    if (eOrder == TH8_BINARY_LE) {
	th8BinaryWriteU32Le(p, bits);
    } else {
	th8BinaryWriteU32Be(p, bits);
    }
    th8BinaryAdvanceCursor(pBuf, 4);
}

/*
 * th8BinaryWriteFloat64 -- write a double-precision IEEE 754
 * value at the buffer cursor.  NaN inputs are normalised to
 * the canonical pattern.
 */
static void
th8BinaryWriteFloat64(
    Th8_BinaryFormatBuf *pBuf,
    unsigned char eOrder,
    double d)
{
    unsigned char *p = pBuf->zBuf + pBuf->iCur;
    th8_uint64_t bits = th8BinaryFloat64Bits(d);

    if (eOrder == TH8_BINARY_LE) {
	th8BinaryWriteU64Le(p, bits);
    } else {
	th8BinaryWriteU64Be(p, bits);
    }
    th8BinaryAdvanceCursor(pBuf, 8);
}

/*
 *----------------------------------------------------------------------
 *
 * Phase 2 format helpers -- string, bits, hex, pad, back, seek.
 * Each routine assumes the caller has done the
 * th8BinaryReserveAt() call up front so the bytes are addressable
 * and the cursor-gap (if any) has already been zeroed.
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8BinaryStrPadByte --
 *
 *	Pick the byte used to pad the tail of an `a`/`A` string
 *	field when the source is shorter than the field count:
 *	NUL for `a` (KIND_STR_NUL) and space for `A`
 *	(KIND_STR_SP).
 *
 * Parameters:
 *	kind -- the op kind (TH8_BINARY_KIND_STR_NUL or _STR_SP).
 *
 * Returns:
 *	`' '` for TH8_BINARY_KIND_STR_SP; `0` (NUL) otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static unsigned char
th8BinaryStrPadByte(unsigned char kind)
{
    if (kind == TH8_BINARY_KIND_STR_SP) return ' ';
    return 0;
}

/*
 * th8BinaryFormatStr -- copy `count` bytes from zSrc/nSrc into the
 * output, padding with the kind-appropriate byte if nSrc < count.
 * Truncates if nSrc > count.
 */
static void
th8BinaryFormatStr(
    Th8_Interp *interp,
    Th8_BinaryFormatBuf *pBuf,
    unsigned char kind,
    th8_int64_t count,
    const char *zSrc,
    size_t nSrc)
{
    unsigned char *p = pBuf->zBuf + pBuf->iCur;
    size_t nCopy = (size_t)count;
    unsigned char pad = th8BinaryStrPadByte(kind);

    if (nSrc < nCopy) nCopy = nSrc;
    if (nCopy > 0) Th8_Memcpy(interp, p, zSrc, nCopy);
    if ((size_t)count > nCopy) {
	Th8_Memset(interp, p + nCopy, pad, (size_t)count - nCopy);
    }
    th8BinaryAdvanceCursor(pBuf, (size_t)count);
}

/*
 *----------------------------------------------------------------------
 *
 * th8BinaryBitValue --
 *
 *	Map one character of a `b`/`B` bit-string field to its
 *	bit value.  Per Tcl 8.6, only `'0'` denotes a zero bit;
 *	every other character is treated as a one bit.
 *
 * Parameters:
 *	c -- byte value taken from the source bit string.
 *
 * Returns:
 *	0 if `c` is `'0'`; 1 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static int
th8BinaryBitValue(int c)
{
    return c == '0' ? 0 : 1;
}

/*
 *----------------------------------------------------------------------
 *
 * th8BinaryHexValue --
 *
 *	Convert a single ASCII hex digit to its 0..15 value.
 *	Used by the H/h format-letter packers when scanning
 *	the source hex string nibble-by-nibble.  Non-hex
 *	characters (or characters outside the accepted ranges)
 *	yield 0.
 *
 *	The digit-range C1 arm (`c < '0'`) is intrinsic-dead
 *	because hex strings accepted by `[binary format H/h]`
 *	/ `[binary scan H/h]` are pre-filtered upstream to
 *	characters >= `'0'`; kept defensive against future
 *	callers per FINDINGS.md Finding 005 sec. 5b.
 *
 * Parameters:
 *	c -- byte value (typically promoted from `unsigned char`).
 *
 * Returns:
 *	0..15 for hex digits (`'0'..'9'`, `'a'..'f'`, `'A'..'F'`);
 *	0 for any other input.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static int
th8BinaryHexValue(int c)
{
    if (c >= '0')
	if (c <= '9') return c - '0';
    if (c >= 'a')
	if (c <= 'f') return c - 'a' + 10;
    if (c >= 'A')
	if (c <= 'F') return c - 'A' + 10;
    return 0;
}

/*
 * th8BinaryFormatBits -- pack `count` bits drawn from zSrc[0..]
 * (one char per bit) into ceil(count/8) bytes at the cursor.  Pad
 * bits beyond nSrc are 0.  bHiFirst selects high-bit-first (`B`)
 * vs low-bit-first (`b`).
 */
static void
th8BinaryFormatBits(
    Th8_Interp *interp,
    Th8_BinaryFormatBuf *pBuf,
    int bHiFirst,
    th8_int64_t count,
    const char *zSrc,
    size_t nSrc)
{
    size_t nBytes = (size_t)((count + 7) / 8);
    unsigned char *p = pBuf->zBuf + pBuf->iCur;
    size_t iBit;

    Th8_Memset(interp, p, 0, nBytes);
    for (iBit = 0; iBit < (size_t)count; iBit++) {
	int bit = (iBit < nSrc) ? th8BinaryBitValue((unsigned char)zSrc[iBit])
	                        : 0;
	size_t iByte = iBit / 8;
	int iShift = bHiFirst ? (7 - (int)(iBit % 8)) : (int)(iBit % 8);

	if (bit) p[iByte] |= (unsigned char)(1u << iShift);
    }
    th8BinaryAdvanceCursor(pBuf, nBytes);
}

/*
 * th8BinaryFormatHex -- pack `count` hex digits drawn from zSrc
 * (one char per nibble) into ceil(count/2) bytes at the cursor.
 * Pad nibbles beyond nSrc are 0.  bHiFirst selects high-nibble-
 * first (`H`) vs low-nibble-first (`h`).
 */
static void
th8BinaryFormatHex(
    Th8_Interp *interp,
    Th8_BinaryFormatBuf *pBuf,
    int bHiFirst,
    th8_int64_t count,
    const char *zSrc,
    size_t nSrc)
{
    size_t nBytes = (size_t)((count + 1) / 2);
    unsigned char *p = pBuf->zBuf + pBuf->iCur;
    size_t iNib;

    Th8_Memset(interp, p, 0, nBytes);
    for (iNib = 0; iNib < (size_t)count; iNib++) {
	int v = (iNib < nSrc) ? th8BinaryHexValue((unsigned char)zSrc[iNib])
	                      : 0;
	size_t iByte = iNib / 2;
	int iShift;

	if (bHiFirst) {
	    iShift = (iNib & 1u) ? 0 : 4;
	} else {
	    iShift = (iNib & 1u) ? 4 : 0;
	}
	p[iByte] |= (unsigned char)((v & 0xF) << iShift);
    }
    th8BinaryAdvanceCursor(pBuf, nBytes);
}

/*
 * binary_format_command -- implementation of [binary format].
 */
static int
binary_format_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    Th8_BinaryFormatBuf out;
    const char *zFmt;
    size_t nFmt;
    size_t iFmt = 0;
    int iArg = 3; /* index into argv of next value to consume */
    int rc = TH8_OK;

    (void)ctx;
    if (argc < 3) {
	return Th8_WrongNumArgs(
	    interp, "binary format formatString ?arg arg ...?");
    }

    zFmt = argv[2];
    nFmt = TH8_LEN(argl[2]);

    out.zBuf = NULL;
    out.iCur = 0;
    out.nUsed = 0;
    out.nAlloc = 0;

    while (iFmt < nFmt) {
	int letter = 0;
	th8_int64_t count = 0;
	int bStar = 0;
	const Th8_BinaryOpDef *pOp;
	th8_int64_t i;

	rc = th8BinaryNextToken(
	    interp, zFmt, nFmt, &iFmt, &letter, &count, &bStar);
	if (rc != TH8_OK) goto done;
	if (letter == 0) break;

	pOp = th8BinaryOpFor(letter);
	if (!pOp) {
	    rc = th8BinaryErrBadFormat(interp, letter);
	    goto done;
	}
	if (bStar && !pOp->bStarFmt) {
	    rc = th8BinaryErrStarFormat(interp);
	    goto done;
	}

	switch (pOp->kind) {
	case TH8_BINARY_KIND_INT: {
	    if (bStar) {
		/* `*` on integer format: consume ONE list arg, pack
		 * each element.  Matches Tcl 8.6 semantics for
		 * `binary format c*`, `i*`, `w*`, etc. */
		char **azElem = NULL;
		size_t *anElem = NULL;
		int nList = 0;
		int j;
		size_t nReserve;

		if (iArg >= argc) {
		    rc = th8BinaryErrNotEnoughArgs(interp);
		    goto done;
		}
		rc = Th8_SplitList(
		    interp, argv[iArg], TH8_LEN(argl[iArg]), &azElem, &anElem,
		    &nList, 0);
		if (rc != TH8_OK) goto done;
		iArg++;
		if (TH8_SAFE_MUL_SIZE(
		        (size_t)nList, (size_t)pOp->nWidth, &nReserve)) {
		    Th8_Free(interp, azElem);
		    rc = th8BinaryErrCountOverflow(interp);
		    goto done;
		}
		rc = th8BinaryReserve(interp, &out, nReserve);
		for (j = 0; rc == TH8_OK && j < nList; j++) {
		    th8_int64_t v = 0;

		    rc =
		        th8BinaryParseTrunc(interp, azElem[j], anElem[j], &v);
		    if (rc == TH8_OK) th8BinaryWriteInt(&out, pOp, v);
		}
		Th8_Free(interp, azElem);
		if (rc != TH8_OK) goto done;
		break;
	    }
	    if (count > (th8_int64_t)TH8_BINARY_MAX_COUNT / pOp->nWidth) {
		rc = th8BinaryErrCountOverflow(interp);
		goto done;
	    }
	    rc = th8BinaryReserve(
	        interp, &out, (size_t)(count * (th8_int64_t)pOp->nWidth));
	    if (rc != TH8_OK) goto done;
	    for (i = 0; i < count; i++) {
		th8_int64_t v = 0;

		if (iArg >= argc) {
		    rc = th8BinaryErrNotEnoughArgs(interp);
		    goto done;
		}
		rc = th8BinaryParseTrunc(
		    interp, argv[iArg], TH8_LEN(argl[iArg]), &v);
		if (rc != TH8_OK) goto done;
		iArg++;
		th8BinaryWriteInt(&out, pOp, v);
	    }
	    break;
	}
	case TH8_BINARY_KIND_STR_NUL:
	case TH8_BINARY_KIND_STR_SP: {
	    const char *zSrc;
	    size_t nSrc;

	    if (iArg >= argc) {
		rc = th8BinaryErrNotEnoughArgs(interp);
		goto done;
	    }
	    zSrc = argv[iArg];
	    nSrc = TH8_LEN(argl[iArg]);
	    iArg++;
	    /* `a*` / `A*` on format: count = input string length;
	     * no padding/truncation. */
	    if (bStar) count = (th8_int64_t)nSrc;
	    rc = th8BinaryReserve(interp, &out, (size_t)count);
	    if (rc != TH8_OK) goto done;
	    th8BinaryFormatStr(interp, &out, pOp->kind, count, zSrc, nSrc);
	    break;
	}
	case TH8_BINARY_KIND_BITS_LO:
	case TH8_BINARY_KIND_BITS_HI: {
	    const char *zSrc;
	    size_t nSrc;
	    size_t nBytes;

	    if (iArg >= argc) {
		rc = th8BinaryErrNotEnoughArgs(interp);
		goto done;
	    }
	    zSrc = argv[iArg];
	    nSrc = TH8_LEN(argl[iArg]);
	    iArg++;
	    /* `b*` / `B*` on format: count = source bit count. */
	    if (bStar) count = (th8_int64_t)nSrc;
	    nBytes = (size_t)((count + 7) / 8);
	    rc = th8BinaryReserve(interp, &out, nBytes);
	    if (rc != TH8_OK) goto done;
	    th8BinaryFormatBits(
	        interp, &out, pOp->kind == TH8_BINARY_KIND_BITS_HI, count,
	        zSrc, nSrc);
	    break;
	}
	case TH8_BINARY_KIND_HEX_LO:
	case TH8_BINARY_KIND_HEX_HI: {
	    const char *zSrc;
	    size_t nSrc;
	    size_t nBytes;

	    if (iArg >= argc) {
		rc = th8BinaryErrNotEnoughArgs(interp);
		goto done;
	    }
	    zSrc = argv[iArg];
	    nSrc = TH8_LEN(argl[iArg]);
	    iArg++;
	    /* `h*` / `H*` on format: count = source nibble count. */
	    if (bStar) count = (th8_int64_t)nSrc;
	    nBytes = (size_t)((count + 1) / 2);
	    rc = th8BinaryReserve(interp, &out, nBytes);
	    if (rc != TH8_OK) goto done;
	    th8BinaryFormatHex(
	        interp, &out, pOp->kind == TH8_BINARY_KIND_HEX_HI, count,
	        zSrc, nSrc);
	    break;
	}
	case TH8_BINARY_KIND_PAD: {
	    rc = th8BinaryReserve(interp, &out, (size_t)count);
	    if (rc != TH8_OK) goto done;
	    Th8_Memset(interp, out.zBuf + out.iCur, 0, (size_t)count);
	    th8BinaryAdvanceCursor(&out, (size_t)count);
	    break;
	}
	case TH8_BINARY_KIND_BACK: {
	    /* `X*` on format rewinds to start; `XN` backs up by N
	     * (capped at 0). */
	    if (bStar) {
		out.iCur = 0;
	    } else if ((size_t)count > out.iCur) {
		out.iCur = 0;
	    } else {
		out.iCur -= (size_t)count;
	    }
	    break;
	}
	case TH8_BINARY_KIND_SEEK: {
	    rc = th8BinaryReserveAt(interp, &out, (size_t)count, 0);
	    if (rc != TH8_OK) goto done;
	    out.iCur = (size_t)count;
	    break;
	}
	case TH8_BINARY_KIND_FLOAT:
	case TH8_BINARY_KIND_DOUBLE: {
	    size_t nElem = pOp->kind == TH8_BINARY_KIND_FLOAT ? 4 : 8;

	    if (bStar) {
		/* `*` on float format: consume ONE list arg, pack each
		 * element.  Matches Tcl 8.6 semantics for `binary
		 * format f*`, `d*`, etc. */
		char **azElem = NULL;
		size_t *anElem = NULL;
		int nList = 0;
		int j;
		size_t nReserve;

		if (iArg >= argc) {
		    rc = th8BinaryErrNotEnoughArgs(interp);
		    goto done;
		}
		rc = Th8_SplitList(
		    interp, argv[iArg], TH8_LEN(argl[iArg]), &azElem, &anElem,
		    &nList, 0);
		if (rc != TH8_OK) goto done;
		iArg++;
		if (TH8_SAFE_MUL_SIZE((size_t)nList, nElem, &nReserve)) {
		    Th8_Free(interp, azElem);
		    rc = th8BinaryErrCountOverflow(interp);
		    goto done;
		}
		rc = th8BinaryReserve(interp, &out, nReserve);
		/* Skip-write-on-error mirrors the int-list path above
		 * so the for-condition's rc-mutation C-pair stays
		 * exercisable.  An explicit `break` here would make
		 * C1=F intrinsic-dead. */
		for (j = 0; rc == TH8_OK && j < nList; j++) {
		    double d = 0.0;

		    rc = Th8_ToDouble(interp, azElem[j], anElem[j], &d);
		    if (rc == TH8_OK) {
			if (pOp->kind == TH8_BINARY_KIND_FLOAT) {
			    th8BinaryWriteFloat32(&out, pOp->eOrder, d);
			} else {
			    th8BinaryWriteFloat64(&out, pOp->eOrder, d);
			}
		    }
		}
		Th8_Free(interp, azElem);
		if (rc != TH8_OK) goto done;
		break;
	    }
	    if (count >
	        (th8_int64_t)TH8_BINARY_MAX_COUNT / (th8_int64_t)nElem) {
		rc = th8BinaryErrCountOverflow(interp);
		goto done;
	    }
	    {
		size_t nReserve;

		if (TH8_SAFE_MUL_SIZE((size_t)count, nElem, &nReserve)) {
		    rc = th8BinaryErrCountOverflow(interp);
		    goto done;
		}
		rc = th8BinaryReserve(interp, &out, nReserve);
		if (rc != TH8_OK) goto done;
	    }
	    for (i = 0; i < count; i++) {
		double d = 0.0;

		if (iArg >= argc) {
		    rc = th8BinaryErrNotEnoughArgs(interp);
		    goto done;
		}
		rc =
		    Th8_ToDouble(interp, argv[iArg], TH8_LEN(argl[iArg]), &d);
		if (rc != TH8_OK) goto done;
		iArg++;
		if (pOp->kind == TH8_BINARY_KIND_FLOAT) {
		    th8BinaryWriteFloat32(&out, pOp->eOrder, d);
		} else {
		    th8BinaryWriteFloat64(&out, pOp->eOrder, d);
		}
	    }
	    break;
	}
	case TH8_BINARY_KIND_BIGINT_FIXED: {
#  if defined(TH8_ENABLE_BIGINT)
	    int bBigEndian = (pOp->eOrder == TH8_BINARY_BE);
	    size_t nWidth;

	    if (iArg >= argc) {
		rc = th8BinaryErrNotEnoughArgs(interp);
		goto done;
	    }

	    if (bStar) {
		/* `j*` / `J*`: pack in the value's natural minimum
		 * two's-complement byte width. */
		if (!Th8_IsBigintEnabled(interp)) {
		    /* Without bigint runtime, the value must fit in
		     * an int64; min-width comes from the value's
		     * absolute magnitude.  Fall back to a fixed
		     * 8-byte field. */
		    nWidth = 8;
		} else {
		    rc = th8BigintMinTwosComplementBytes(
		        interp, argv[iArg], TH8_LEN(argl[iArg]), &nWidth);
		    if (rc != TH8_OK) goto done;
		}
	    } else {
		if (count <= 0) {
		    rc = th8BinaryErrCountOverflow(interp);
		    goto done;
		}
		if (count > (th8_int64_t)TH8_BINARY_MAX_COUNT) {
		    rc = th8BinaryErrCountOverflow(interp);
		    goto done;
		}
		nWidth = (size_t)count;
	    }

	    rc = th8BinaryReserve(interp, &out, nWidth);
	    if (rc != TH8_OK) goto done;

	    if (!Th8_IsBigintEnabled(interp)) {
		/* Without bigint, accept int64-range values via the
		 * existing parse-trunc fast path and pack via a
		 * synthesised mp_int through the helper. */
		th8_int64_t v = 0;
		size_t k;

		rc = Th8_ToWideInt(
		    interp, argv[iArg], TH8_LEN(argl[iArg]), &v);
		if (rc != TH8_OK) goto done;
		Th8_Memset(interp, out.zBuf + out.iCur, 0, nWidth);
		/* Two's-complement byte serialise of v in big-endian
		 * view first. */
		{
		    unsigned char tmp[8];
		    th8_uint64_t u = (th8_uint64_t)v;

		    for (k = 0; k < 8; k++) {
			tmp[7 - k] = (unsigned char)(u & 0xFFu);
			u >>= 8;
		    }
		    /* If v < 0, the high bytes must sign-extend to 0xFF;
		     * the cast already gives that because u was the
		     * unsigned reinterpretation. */
		    if (nWidth >= 8) {
			size_t pad = nWidth - 8;

			if (v < 0) {
			    Th8_Memset(
			        interp, out.zBuf + out.iCur, 0xFF, pad);
			}
			for (k = 0; k < 8; k++) {
			    out.zBuf[out.iCur + pad + k] = tmp[k];
			}
		    } else {
			/* Range check: value must fit. */
			th8_int64_t maxPos =
			    (nWidth == 8)
			        ? (th8_int64_t)0x7FFFFFFFFFFFFFFFLL
			        : ((th8_int64_t)1 << (nWidth * 8 - 1)) - 1;
			th8_int64_t minNeg = -maxPos - 1;

			if (v > maxPos || v < minNeg) {
			    Th8_SetResultStatic(
			        interp,
			        "integer value too large for j/J field",
			        TH8_NOLEN);
			    rc = TH8_ERROR;
			    goto done;
			}
			for (k = 0; k < nWidth; k++) {
			    out.zBuf[out.iCur + k] = tmp[8 - nWidth + k];
			}
		    }
		    /* Reverse to LE if requested. */
		    if (!bBigEndian) {
			size_t i2;

			for (i2 = 0; i2 < nWidth / 2; i2++) {
			    unsigned char t = out.zBuf[out.iCur + i2];

			    out.zBuf[out.iCur + i2] =
			        out.zBuf[out.iCur + nWidth - 1 - i2];
			    out.zBuf[out.iCur + nWidth - 1 - i2] = t;
			}
		    }
		}
	    } else {
		rc = th8BigintToTwosComplement(
		    interp, argv[iArg], TH8_LEN(argl[iArg]),
		    out.zBuf + out.iCur, nWidth, bBigEndian);
		if (rc != TH8_OK) goto done;
	    }
	    iArg++;
	    th8BinaryAdvanceCursor(&out, nWidth);
#  else /* !TH8_ENABLE_BIGINT */
	    (void)pOp;
	    Th8_SetResultStatic(
	        interp, "j/J specifier requires TH8_ENABLE_BIGINT",
	        TH8_NOLEN);
	    rc = TH8_ERROR;
	    goto done;
#  endif
	    break;
	}
	}
    }

    /* Success -- emit the result buffer. */
    if (out.nUsed == 0) {
	Th8_SetResultStatic(interp, "", 0);
    } else {
	Th8_SetResult(interp, (const char *)out.zBuf, out.nUsed);
    }

done:
    if (out.zBuf) Th8_Free(interp, out.zBuf);
    return rc;
}

/*
 *----------------------------------------------------------------------
 *
 * Scan-direction implementation.
 *
 *----------------------------------------------------------------------
 */

/*
 * th8BinaryReadInt -- read one integer value at the buffer's
 * cursor.  Caller has already ensured at least pOp->nWidth
 * bytes are available.  Returns the sign-extended value as
 * th8_int64_t.
 */
static th8_int64_t
th8BinaryReadInt(const unsigned char *p, const Th8_BinaryOpDef *pOp)
{
    switch (pOp->nWidth * 2 + pOp->eOrder) {
    case 1 * 2 + TH8_BINARY_LE:
    case 1 * 2 + TH8_BINARY_BE:
	return th8BinaryReadI8(p);
    case 2 * 2 + TH8_BINARY_LE:
	return th8BinaryReadI16Le(p);
    case 2 * 2 + TH8_BINARY_BE:
	return th8BinaryReadI16Be(p);
    case 4 * 2 + TH8_BINARY_LE:
	return th8BinaryReadI32Le(p);
    case 4 * 2 + TH8_BINARY_BE:
	return th8BinaryReadI32Be(p);
    case 8 * 2 + TH8_BINARY_LE:
	return th8BinaryReadI64Le(p);
    case 8 * 2 + TH8_BINARY_BE:
	return th8BinaryReadI64Be(p);
    }
    return 0;
}

/*
 * th8BinaryAssignInt -- format value as a decimal string and
 * write it to varName.  Returns TH8_OK / TH8_ERROR.
 */
static int
th8BinaryAssignInt(
    Th8_Interp *interp,
    const char *zVarName,
    size_t nVarName,
    th8_int64_t v)
{
    char buf[32];
    int n;

    n = th8Snprintf(interp, buf, sizeof(buf), "%lld", (long long)v);
    if (n < 0) n = 0;
    if ((size_t)n >= sizeof(buf)) n = (int)sizeof(buf) - 1;
    return Th8_SetVar(interp, zVarName, nVarName, buf, (size_t)n);
}

/*
 * th8BinaryAssignIntList -- format `count` values as a Tcl
 * list and write to varName.  Used for the `*` count case
 * on scan.  Returns TH8_OK / TH8_ERROR.
 */
static int
th8BinaryAssignIntList(
    Th8_Interp *interp,
    const char *zVarName,
    size_t nVarName,
    const unsigned char *p,
    const Th8_BinaryOpDef *pOp,
    th8_int64_t count)
{
    char *zList = NULL;
    size_t nList = 0;
    th8_int64_t i;
    int rc;
    char buf[32];

    for (i = 0; i < count; i++) {
	int n;
	th8_int64_t v = th8BinaryReadInt(p + (size_t)(i * pOp->nWidth), pOp);

	n = th8Snprintf(interp, buf, sizeof(buf), "%lld", (long long)v);
	if (n < 0) n = 0;
	if ((size_t)n >= sizeof(buf)) n = (int)sizeof(buf) - 1;
	Th8_ListAppend(interp, &zList, &nList, buf, (size_t)n);
    }
    if (zList) {
	rc = Th8_SetVar(interp, zVarName, nVarName, zList, nList);
	Th8_Free(interp, zList);
    } else {
	rc = Th8_SetVar(interp, zVarName, nVarName, "", 0);
    }
    return rc;
}

/*
 * th8BinaryScanStr -- assign `nRead` bytes of input to varName,
 * stripping trailing space/NUL when the kind is `A`.
 */
static int
th8BinaryScanStr(
    Th8_Interp *interp,
    const char *zVarName,
    size_t nVarName,
    unsigned char kind,
    const unsigned char *zSrc,
    size_t nRead)
{
    size_t n = nRead;

    if (kind == TH8_BINARY_KIND_STR_SP) {
	while (n > 0 && (zSrc[n - 1] == ' ' || zSrc[n - 1] == 0)) {
	    n--;
	}
    }
    return Th8_SetVar(interp, zVarName, nVarName, (const char *)zSrc, n);
}

/*
 * th8BinaryScanBits -- assign a `count`-character "01" string
 * decoded from `nBytes` of input.  bHiFirst selects high-bit-
 * first (`B`) vs low-bit-first (`b`).
 */
static int
th8BinaryScanBits(
    Th8_Interp *interp,
    const char *zVarName,
    size_t nVarName,
    int bHiFirst,
    th8_int64_t count,
    const unsigned char *zSrc)
{
    char *zOut;
    size_t iBit;
    int rc;

    if (count == 0) return Th8_SetVar(interp, zVarName, nVarName, "", 0);
    zOut = (char *)TH8_ALLOC_STR(interp, (size_t)count);
    if (!zOut) return TH8_ERROR;
    for (iBit = 0; iBit < (size_t)count; iBit++) {
	size_t iByte = iBit / 8;
	int iShift = bHiFirst ? (7 - (int)(iBit % 8)) : (int)(iBit % 8);
	int bit = (zSrc[iByte] >> iShift) & 1;

	zOut[iBit] = (char)('0' + bit);
    }
    rc = Th8_SetVar(interp, zVarName, nVarName, zOut, (size_t)count);
    Th8_Free(interp, zOut);
    return rc;
}

/*
 * th8BinaryScanHex -- assign a `count`-character hex string
 * decoded from ceil(count/2) bytes of input.  bHiFirst selects
 * high-nibble-first (`H`) vs low-nibble-first (`h`).
 */
static int
th8BinaryScanHex(
    Th8_Interp *interp,
    const char *zVarName,
    size_t nVarName,
    int bHiFirst,
    th8_int64_t count,
    const unsigned char *zSrc)
{
    static const char kHex[] = "0123456789abcdef";
    char *zOut;
    size_t iNib;
    int rc;

    if (count == 0) return Th8_SetVar(interp, zVarName, nVarName, "", 0);
    zOut = (char *)TH8_ALLOC_STR(interp, (size_t)count);
    if (!zOut) return TH8_ERROR;
    for (iNib = 0; iNib < (size_t)count; iNib++) {
	size_t iByte = iNib / 2;
	int iShift;
	int v;

	if (bHiFirst) {
	    iShift = (iNib & 1u) ? 0 : 4;
	} else {
	    iShift = (iNib & 1u) ? 4 : 0;
	}
	v = (zSrc[iByte] >> iShift) & 0xF;
	zOut[iNib] = kHex[v];
    }
    rc = Th8_SetVar(interp, zVarName, nVarName, zOut, (size_t)count);
    Th8_Free(interp, zOut);
    return rc;
}

/*
 * th8BinaryFormatDoubleString -- render a double as a decimal
 * string suitable for round-trip back through Th8_ToDouble.
 * NaN renders as "NaN"; +/-Inf renders as "Inf"/"-Inf"; finite
 * values use %.17g (binary64) or %.9g (binary32).
 */
static int
th8BinaryFormatDoubleString(
    Th8_Interp *interp,
    double d,
    int bSingle,
    char *zBuf,
    size_t nBuf)
{
    th8_uint64_t bits;
    int isNaN;
    int isInf = 0;

    if (bSingle) {
	bits = th8BinaryFloat32Bits((float)d);
	isNaN = th8BinaryIsNaN32(bits);
	if (!isNaN) {
	    th8_uint64_t e = bits & 0x7F800000u;
	    th8_uint64_t m = bits & 0x007FFFFFu;
	    /* Split per Finding 005 sec. 5b: the m == 0 arm of
	     * this Inf check is intrinsic-dead at the C-pair
	     * level because the !isNaN guard already excludes
	     * the e == max && m != 0 case (that *is* NaN). */
	    if (e == 0x7F800000u)
		if (m == 0) isInf = 1;
	}
    } else {
	bits = th8BinaryFloat64Bits(d);
	isNaN = th8BinaryIsNaN64(bits);
	if (!isNaN) {
	    th8_uint64_t e = bits & 0x7FF0000000000000ULL;
	    th8_uint64_t m = bits & 0x000FFFFFFFFFFFFFULL;
	    /* Same Finding 005 sec. 5b split as the bSingle arm. */
	    if (e == 0x7FF0000000000000ULL)
		if (m == 0) isInf = 1;
	}
    }
    if (isNaN) {
	if (nBuf < 4) return -1;
	zBuf[0] = 'N';
	zBuf[1] = 'a';
	zBuf[2] = 'N';
	zBuf[3] = '\0';
	return 3;
    }
    if (isInf) {
	const char *zSrc = (d < 0.0) ? "-Inf" : "Inf";
	size_t nSrc = (d < 0.0) ? 4 : 3;

	if (nBuf < nSrc + 1) return -1;
	Th8_Memcpy(interp, zBuf, zSrc, nSrc);
	zBuf[nSrc] = '\0';
	return (int)nSrc;
    }
    return th8Snprintf(interp, zBuf, nBuf, bSingle ? "%.9g" : "%.17g", d);
}

/*
 *----------------------------------------------------------------------
 *
 * th8BinaryAssignDouble --
 *
 *	Format the IEEE 754 `d` as a decimal string and assign
 *	it to the named scalar variable.  Used by
 *	`[binary scan f]` / `[binary scan d]` per-element
 *	output; the matching list variant lives in
 *	`th8BinaryAssignDoubleList`.
 *
 *	The precision (`%.9g` for `bSingle`, `%.17g` for
 *	double) is chosen to round-trip every binary-exact
 *	value losslessly per IEEE 754; NaN and Infinity are
 *	emitted as `"NaN"` / `"Inf"` / `"-Inf"` by
 *	`th8BinaryFormatDoubleString`.
 *
 * Parameters:
 *	interp   -- live interpreter.
 *	zVarName -- variable name (not necessarily NUL-terminated).
 *	nVarName -- variable-name length.
 *	d        -- value to format.
 *	bSingle  -- 1 for `float`-equivalent precision, 0 for full
 *		double precision.
 *
 * Returns:
 *	`Th8_SetVar`'s return code (`TH8_OK` / `TH8_ERROR`).
 *
 * Side effects:
 *	Mutates the named variable; sets the interpreter
 *	result on failure.
 *
 *----------------------------------------------------------------------
 */
static int
th8BinaryAssignDouble(
    Th8_Interp *interp,
    const char *zVarName,
    size_t nVarName,
    double d,
    int bSingle)
{
    char buf[40];
    int n = th8BinaryFormatDoubleString(interp, d, bSingle, buf, sizeof(buf));

    if (n < 0) n = 0;
    if ((size_t)n >= sizeof(buf)) n = (int)sizeof(buf) - 1;
    return Th8_SetVar(interp, zVarName, nVarName, buf, (size_t)n);
}

/*
 *----------------------------------------------------------------------
 *
 * th8BinaryAssignDoubleList --
 *
 *	Walk `count` IEEE 754 elements at `p` (each `nElem` =
 *	4 for `bSingle`, 8 otherwise) in the given byte order
 *	`eOrder`, format each as a decimal string via
 *	`th8BinaryFormatDoubleString`, and assign the resulting
 *	whitespace-separated Tcl list to the named scalar
 *	variable.  Used by `[binary scan]` letters that take a
 *	count > 1 with a single output-variable name.
 *
 * Parameters:
 *	interp   -- live interpreter.
 *	zVarName -- variable name.
 *	nVarName -- variable-name length.
 *	p        -- byte pointer to the first element.
 *	bSingle  -- 1 for `float`-equivalent precision (4 bytes
 *		each), 0 for full double precision (8 bytes each).
 *	eOrder   -- `TH8_BINARY_LE` or `TH8_BINARY_BE`.
 *	count    -- number of elements to consume.
 *
 * Returns:
 *	`Th8_SetVar`'s return code on the final assignment;
 *	`TH8_ERROR` if any intermediate `Th8_ListAppend` /
 *	formatting step fails.
 *
 * Side effects:
 *	Allocates and frees a temporary list buffer.  Mutates
 *	the named variable.  Sets the interpreter result on
 *	failure.
 *
 *----------------------------------------------------------------------
 */
static int
th8BinaryAssignDoubleList(
    Th8_Interp *interp,
    const char *zVarName,
    size_t nVarName,
    const unsigned char *p,
    int bSingle,
    unsigned char eOrder,
    th8_int64_t count)
{
    char *zList = NULL;
    size_t nList = 0;
    th8_int64_t i;
    int rc;
    char buf[40];
    size_t nElem = bSingle ? 4 : 8;

    for (i = 0; i < count; i++) {
	const unsigned char *q = p + (size_t)(i * (th8_int64_t)nElem);
	th8_uint64_t bits;
	double d;
	int n;

	if (bSingle) {
	    bits = (eOrder == TH8_BINARY_LE)
	             ? (th8_uint64_t)th8BinaryReadI32Le(q)
	             : (th8_uint64_t)th8BinaryReadI32Be(q);
	    bits &= 0xFFFFFFFFu;
	    d = (double)th8BinaryBitsToFloat32(bits);
	} else {
	    bits = (eOrder == TH8_BINARY_LE)
	             ? (th8_uint64_t)th8BinaryReadI64Le(q)
	             : (th8_uint64_t)th8BinaryReadI64Be(q);
	    d = th8BinaryBitsToFloat64(bits);
	}
	n = th8BinaryFormatDoubleString(interp, d, bSingle, buf, sizeof(buf));
	if (n < 0) n = 0;
	if ((size_t)n >= sizeof(buf)) n = (int)sizeof(buf) - 1;
	Th8_ListAppend(interp, &zList, &nList, buf, (size_t)n);
    }
    if (zList) {
	rc = Th8_SetVar(interp, zVarName, nVarName, zList, nList);
	Th8_Free(interp, zList);
    } else {
	rc = Th8_SetVar(interp, zVarName, nVarName, "", 0);
    }
    return rc;
}

/*
 * binary_scan_command -- implementation of [binary scan].
 *
 * Returns the number of variables successfully assigned (as
 * a decimal string in the interp result).  Partial scans
 * succeed; the caller compares the return value to the
 * number of varName args supplied.
 */
static int
binary_scan_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    const unsigned char *zIn;
    size_t nIn;
    const char *zFmt;
    size_t nFmt;
    size_t iFmt = 0;
    size_t iCur = 0;
    int iArg = 4; /* index into argv of next varName */
    int nAssigned = 0;
    int rc = TH8_OK;

    (void)ctx;
    if (argc < 4) {
	return Th8_WrongNumArgs(
	    interp,
	    "binary scan binaryData formatString ?varName varName ...?");
    }

    zIn = (const unsigned char *)argv[2];
    nIn = TH8_LEN(argl[2]);
    zFmt = argv[3];
    nFmt = TH8_LEN(argl[3]);

    while (iFmt < nFmt) {
	int letter = 0;
	th8_int64_t count = 0;
	int bStar = 0;
	const Th8_BinaryOpDef *pOp;
	th8_int64_t avail;
	th8_int64_t i;

	rc = th8BinaryNextToken(
	    interp, zFmt, nFmt, &iFmt, &letter, &count, &bStar);
	if (rc != TH8_OK) return rc;
	if (letter == 0) break;

	pOp = th8BinaryOpFor(letter);
	if (!pOp) return th8BinaryErrBadFormat(interp, letter);

	/* Cursor ops consume no varName args and are dispatched
	 * before the iArg-exhaustion check. */
	if (pOp->kind == TH8_BINARY_KIND_PAD) {
	    /* Skip `count` bytes (or to end if `*`); cap at nIn.
	     * Split into an else-if ladder so each leg is a
	     * single-condition decision under clang MC/DC.  See
	     * FINDINGS.md Finding 005. */
	    if (bStar) {
		iCur = nIn;
	    } else if (iCur + (size_t)count > nIn) {
		iCur = nIn;
	    } else {
		iCur += (size_t)count;
	    }
	    continue;
	}
	if (pOp->kind == TH8_BINARY_KIND_BACK) {
	    if ((size_t)count > iCur) {
		iCur = 0;
	    } else {
		iCur -= (size_t)count;
	    }
	    continue;
	}
	if (pOp->kind == TH8_BINARY_KIND_SEEK) {
	    if ((size_t)count > nIn) {
		iCur = nIn;
	    } else {
		iCur = (size_t)count;
	    }
	    continue;
	}

	if (iArg >= argc) {
	    /* No more varName args -- scan stops here. */
	    break;
	}

	switch (pOp->kind) {
	case TH8_BINARY_KIND_INT: {
	    if (bStar) {
		/* `*` reads as many elements as fit; the result is
		 * a single list variable assignment.  Per Tcl 8.6
		 * the list-form `*` succeeds even when the buffer
		 * is empty (assigns an empty list). */
		count = (th8_int64_t)((nIn - iCur) / pOp->nWidth);
		rc = th8BinaryAssignIntList(
		    interp, argv[iArg], TH8_LEN(argl[iArg]), zIn + iCur, pOp,
		    count);
		if (rc != TH8_OK) return rc;
		iArg++;
		iCur += (size_t)(count * (th8_int64_t)pOp->nWidth);
		nAssigned++;
		break;
	    }
	    (void)avail;
	    for (i = 0; i < count; i++) {
		th8_int64_t v;

		if (iArg >= argc) goto out;
		if (iCur + pOp->nWidth > nIn) goto out;
		v = th8BinaryReadInt(zIn + iCur, pOp);
		rc = th8BinaryAssignInt(
		    interp, argv[iArg], TH8_LEN(argl[iArg]), v);
		if (rc != TH8_OK) return rc;
		iArg++;
		iCur += pOp->nWidth;
		nAssigned++;
	    }
	    break;
	}
	case TH8_BINARY_KIND_STR_NUL:
	case TH8_BINARY_KIND_STR_SP: {
	    size_t nRead;

	    if (bStar) {
		nRead = nIn - iCur;
	    } else {
		if (iCur + (size_t)count > nIn) goto out;
		nRead = (size_t)count;
	    }
	    rc = th8BinaryScanStr(
	        interp, argv[iArg], TH8_LEN(argl[iArg]), pOp->kind,
	        zIn + iCur, nRead);
	    if (rc != TH8_OK) return rc;
	    iArg++;
	    iCur += nRead;
	    nAssigned++;
	    break;
	}
	case TH8_BINARY_KIND_BITS_LO:
	case TH8_BINARY_KIND_BITS_HI: {
	    size_t nBytes;

	    if (bStar) count = (th8_int64_t)((nIn - iCur) * 8);
	    nBytes = (size_t)((count + 7) / 8);
	    if (iCur + nBytes > nIn) goto out;
	    rc = th8BinaryScanBits(
	        interp, argv[iArg], TH8_LEN(argl[iArg]),
	        pOp->kind == TH8_BINARY_KIND_BITS_HI, count, zIn + iCur);
	    if (rc != TH8_OK) return rc;
	    iArg++;
	    iCur += nBytes;
	    nAssigned++;
	    break;
	}
	case TH8_BINARY_KIND_HEX_LO:
	case TH8_BINARY_KIND_HEX_HI: {
	    size_t nBytes;

	    if (bStar) count = (th8_int64_t)((nIn - iCur) * 2);
	    nBytes = (size_t)((count + 1) / 2);
	    if (iCur + nBytes > nIn) goto out;
	    rc = th8BinaryScanHex(
	        interp, argv[iArg], TH8_LEN(argl[iArg]),
	        pOp->kind == TH8_BINARY_KIND_HEX_HI, count, zIn + iCur);
	    if (rc != TH8_OK) return rc;
	    iArg++;
	    iCur += nBytes;
	    nAssigned++;
	    break;
	}
	case TH8_BINARY_KIND_FLOAT:
	case TH8_BINARY_KIND_DOUBLE: {
	    int bSingle = (pOp->kind == TH8_BINARY_KIND_FLOAT);
	    size_t nElem = bSingle ? 4 : 8;

	    if (bStar) {
		size_t nRem = nIn - iCur;
		/* count = bytes / element width; the byte total
		 * consumed equals nRem - (nRem % nElem), expressed
		 * without a bare size_t multiplication. */
		size_t nConsume = nRem - (nRem % nElem);

		count = (th8_int64_t)(nRem / nElem);
		rc = th8BinaryAssignDoubleList(
		    interp, argv[iArg], TH8_LEN(argl[iArg]), zIn + iCur,
		    bSingle, pOp->eOrder, count);
		if (rc != TH8_OK) return rc;
		iArg++;
		iCur += nConsume;
		nAssigned++;
		break;
	    }
	    for (i = 0; i < count; i++) {
		th8_uint64_t bits;
		double d;

		if (iArg >= argc) goto out;
		if (iCur + nElem > nIn) goto out;
		if (bSingle) {
		    bits = (pOp->eOrder == TH8_BINARY_LE)
		             ? (th8_uint64_t)th8BinaryReadI32Le(zIn + iCur)
		             : (th8_uint64_t)th8BinaryReadI32Be(zIn + iCur);
		    bits &= 0xFFFFFFFFu;
		    d = (double)th8BinaryBitsToFloat32(bits);
		} else {
		    bits = (pOp->eOrder == TH8_BINARY_LE)
		             ? (th8_uint64_t)th8BinaryReadI64Le(zIn + iCur)
		             : (th8_uint64_t)th8BinaryReadI64Be(zIn + iCur);
		    d = th8BinaryBitsToFloat64(bits);
		}
		rc = th8BinaryAssignDouble(
		    interp, argv[iArg], TH8_LEN(argl[iArg]), d, bSingle);
		if (rc != TH8_OK) return rc;
		iArg++;
		iCur += nElem;
		nAssigned++;
	    }
	    break;
	}
	case TH8_BINARY_KIND_BIGINT_FIXED: {
#  if defined(TH8_ENABLE_BIGINT)
	    int bBigEndian = (pOp->eOrder == TH8_BINARY_BE);
	    size_t nWidth;

	    if (bStar) {
		/* `j*` / `J*` on scan: consume all remaining bytes as
		 * a single signed two's-complement bigint and assign to
		 * one variable. */
		nWidth = nIn - iCur;
	    } else {
		if (count <= 0) goto out;
		nWidth = (size_t)count;
		if (iCur + nWidth > nIn) goto out;
	    }

	    if (!Th8_IsBigintEnabled(interp)) {
		/* Without bigint runtime, decode as signed int64 only
		 * (nWidth <= 8); else error. */
		th8_int64_t v = 0;
		th8_uint64_t u = 0;
		size_t k;
		unsigned char tmp[8];

		if (nWidth == 0) {
		    rc = Th8_SetVar(
		        interp, argv[iArg], TH8_LEN(argl[iArg]), "0", 1);
		    if (rc != TH8_OK) return rc;
		    iArg++;
		    nAssigned++;
		    break;
		} else if (nWidth > 8) {
		    Th8_SetResultStatic(
		        interp,
		        "j/J scan width exceeds int64 with bigint disabled",
		        TH8_NOLEN);
		    return TH8_ERROR;
		} else {
		    /* Copy nWidth bytes to a big-endian buffer, sign-
		     * extending to 8 bytes if the high bit is set. */
		    if (bBigEndian) {
			for (k = 0; k < nWidth; k++)
			    tmp[8 - nWidth + k] = ((const unsigned char *)
			                               zIn)[iCur + k];
		    } else {
			for (k = 0; k < nWidth; k++)
			    tmp[8 - nWidth +
			        k] = ((const unsigned char *)
			                  zIn)[iCur + nWidth - 1 - k];
		    }
		    {
			unsigned char signFill = (tmp[8 - nWidth] & 0x80)
			                           ? 0xFF
			                           : 0x00;

			for (k = 0; k < 8 - nWidth; k++)
			    tmp[k] = signFill;
		    }
		    for (k = 0; k < 8; k++) {
			u = (u << 8) | (th8_uint64_t)tmp[k];
		    }
		    v = (th8_int64_t)u;
		    rc = th8BinaryAssignInt(
		        interp, argv[iArg], TH8_LEN(argl[iArg]), v);
		    if (rc != TH8_OK) return rc;
		}
	    } else {
		/* With bigint runtime, decode into a decimal string
		 * via th8BigintFromTwosComplement and assign. */
		const char *zRes;
		size_t nRes;
		Th8_Interp *tmpInterp = interp;

		rc = th8BigintFromTwosComplement(
		    tmpInterp, (const unsigned char *)zIn + iCur, nWidth,
		    bBigEndian);
		if (rc != TH8_OK) return rc;
		zRes = Th8_GetResult(interp, &nRes);
		/* Copy the result string to a stack buffer (the next
		 * SetVar may overwrite the interp result). */
		{
		    char tmpBuf[80];
		    char *pAlloc = NULL;
		    const char *pSrc;
		    size_t k;

		    if (nRes < sizeof(tmpBuf)) {
			for (k = 0; k < nRes; k++)
			    tmpBuf[k] = zRes[k];
			tmpBuf[nRes] = 0;
			pSrc = tmpBuf;
		    } else {
			pAlloc = (char *)TH8_ALLOC_STR(interp, nRes);
			if (!pAlloc) return TH8_ERROR;
			for (k = 0; k < nRes; k++)
			    pAlloc[k] = zRes[k];
			pAlloc[nRes] = 0;
			pSrc = pAlloc;
		    }
		    rc = Th8_SetVar(
		        interp, argv[iArg], TH8_LEN(argl[iArg]), pSrc, nRes);
		    if (pAlloc) Th8_Free(interp, pAlloc);
		    if (rc != TH8_OK) return rc;
		}
	    }
	    iArg++;
	    iCur += nWidth;
	    nAssigned++;
#  else /* !TH8_ENABLE_BIGINT */
	    Th8_SetResultStatic(
	        interp, "j/J specifier requires TH8_ENABLE_BIGINT",
	        TH8_NOLEN);
	    return TH8_ERROR;
#  endif
	    break;
	}
	}
    }

out:
    Th8_SetResultInt(interp, nAssigned);
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Ensemble dispatch.
 *
 *----------------------------------------------------------------------
 */

static const Th8_SubCommand th8BinarySub[] = {
    {0, "format", binary_format_command},
    {0, "scan", binary_scan_command},
    {0, 0, 0},
};

/*
 *----------------------------------------------------------------------
 *
 * binary_command --
 *
 *	Implements the script-visible `[binary ...]` ensemble.
 *	Pure thin wrapper that hands the (`argc`, `argv`, `argl`)
 *	tuple off to `Th8_CallSubCommand`, which performs the
 *	usual ensemble-name-resolution against `th8BinarySub`
 *	(format, scan) and dispatches to the matching
 *	subcommand handler.
 *
 *	Diagnostics for unknown / ambiguous subcommands are
 *	emitted by `Th8_CallSubCommand` itself.
 *
 * Parameters:
 *	interp -- live interpreter.
 *	ctx    -- command context (forwarded to the subcommand).
 *	argc   -- argument count.
 *	argv   -- argument vector.
 *	argl   -- argument byte-length vector.
 *
 * Returns:
 *	The subcommand handler's return code, or `TH8_ERROR`
 *	with a diagnostic if the subcommand name is unknown.
 *
 * Side effects:
 *	Whatever the dispatched subcommand performs.
 *
 *----------------------------------------------------------------------
 */
static int
binary_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    return Th8_CallSubCommand(interp, ctx, argc, argv, argl, th8BinarySub);
}

/*
 *----------------------------------------------------------------------
 *
 * Plugin registration.
 *
 *----------------------------------------------------------------------
 */

static Th8_CommandEntry th8BinaryCommands[] = {
    {1, 0, "binary", binary_command},
};

/*
 *----------------------------------------------------------------------
 *
 * th8BinaryGetCommands --
 *
 *	Plugin-registration entry point: report (or copy out)
 *	the `th8BinaryCommands` table that the extensibility
 *	loader uses to install `[binary]` on the interpreter.
 *
 *	Standard `Th8_CommandEntry` reporter contract:
 *	  * NULL `pCommand` -- store count in `*pnCommand` and
 *	    return `TH8_OK`.
 *	  * Non-NULL `pCommand` -- caller-provided buffer of at
 *	    least `*pnCommand` entries; copy in the table and
 *	    return `TH8_OK`.  Insufficient buffer returns
 *	    `TH8_ERROR` without touching `pCommand`.
 *
 *	NULL `pnCommand` is always an error.
 *
 * Parameters:
 *	pCommand  -- caller-supplied output buffer or NULL to
 *		query the count only.
 *	pnCommand -- in/out count; receives the table size on
 *		query, must be >= table size on copy.
 *
 * Returns:
 *	`TH8_OK` on success; `TH8_ERROR` on missing `pnCommand`
 *	or insufficient `*pnCommand`.
 *
 * Side effects:
 *	May overwrite `*pnCommand` and `pCommand[0..n-1]`.
 *
 *----------------------------------------------------------------------
 */
int
th8BinaryGetCommands(Th8_CommandEntry *pCommand, int *pnCommand)
{
    int n = (int)(sizeof(th8BinaryCommands) / sizeof(th8BinaryCommands[0]));

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
	    pCommand[i] = th8BinaryCommands[i];
	}
    }
    return TH8_OK;
}

#endif /* TH8_PLUGIN_BINARY */
