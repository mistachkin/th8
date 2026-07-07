/*
 * th8_bigint.h -- Arbitrary precision integer support for TH8.
 *
 * Internal header providing bigint operations via libtommath.
 * Only included when TH8_ENABLE_BIGINT is defined.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef TH8_BIGINT_H
#define TH8_BIGINT_H

#if defined(TH8_ENABLE_BIGINT)

#  include "tommath.h"

/*
 *----------------------------------------------------------------------
 *
 * Th8_BigInt operations --
 *
 *	These functions provide the bridge between TH8's string-based
 *	value system and libtommath's mp_int type.  All values flow
 *	through the interpreter result as decimal strings, so bigints
 *	are transparent to the script layer.
 *
 *	The expression evaluator calls these when:
 *	  - An integer literal is too large for th8_int64_t
 *	  - An arithmetic operation would overflow th8_int64_t
 *
 *----------------------------------------------------------------------
 */

/*
 * th8BigintArith --
 *
 *	Perform a bigint arithmetic operation on two string operands.
 *	Sets the interpreter result to the decimal string of the
 *	result.  The eOp parameter matches the TH8_OP_* constants.
 *
 * Returns:
 *	TH8_OK on success, TH8_ERROR on failure.
 */

int th8BigintArith(
    Th8_Interp *interp,
    const char *zLeft,
    size_t nLeft,
    const char *zRight,
    size_t nRight,
    int eOp);

/*
 * th8BigintUnary --
 *
 *	Perform a unary bigint operation (negate, complement, bool).
 */

int
th8BigintUnary(Th8_Interp *interp, const char *zVal, size_t nVal, int eOp);

/*
 * th8BigintCompare --
 *
 *	Compare two bigint string operands.  Sets *pCmp to -1, 0, 1.
 */

int th8BigintCompare(
    Th8_Interp *interp,
    const char *zLeft,
    size_t nLeft,
    const char *zRight,
    size_t nRight,
    int *pCmp);

/*
 * th8IsBigint --
 *
 *	Check if a string represents an integer that doesn't fit
 *	in th8_int64_t.  Returns 1 if it's a valid integer but
 *	too large, 0 otherwise.
 */

int th8IsBigint(Th8_Interp *interp, const char *z, size_t n);

/*
 * th8BigintToDouble --
 *
 *	Convert a bigint string to double.  Used when a bigint
 *	participates in a floating-point expression.
 */

int
th8BigintToDouble(Th8_Interp *interp, const char *z, size_t n, double *pVal);

/*
 * th8BigintToTwosComplement --
 *
 *	Encode an integer (decimal string, possibly bigint) into
 *	exactly nBytes of two's-complement binary.  Sign bit is the
 *	MSB of the most-significant byte (in big-endian view).
 *
 *	bBigEndian != 0	-> MSB at pBuf[0]; LSB at pBuf[nBytes-1]
 *	bBigEndian == 0	-> LSB at pBuf[0]; MSB at pBuf[nBytes-1]
 *
 *	Returns TH8_OK on success.  Returns TH8_ERROR with an
 *	interp result of "integer value too large for j/J field"
 *	when the value does not fit in nBytes of two's complement
 *	(i.e. value not in [-2^(nBytes*8-1), 2^(nBytes*8-1) - 1]).
 */

int th8BigintToTwosComplement(
    Th8_Interp *interp,
    const char *zVal,
    size_t nVal,
    unsigned char *pBuf,
    size_t nBytes,
    int bBigEndian);

/*
 * th8BigintFromTwosComplement --
 *
 *	Decode nBytes of two's-complement binary into a decimal
 *	integer string.  The MSB of the most-significant byte (in
 *	big-endian view) is the sign bit.
 *
 *	Stores the decoded value as the interpreter result on
 *	success (via Th8_SetResult).  Returns TH8_OK on success or
 *	TH8_ERROR on conversion failure.  An empty input (nBytes ==
 *	0) decodes to "0".
 */

int th8BigintFromTwosComplement(
    Th8_Interp *interp,
    const unsigned char *pBuf,
    size_t nBytes,
    int bBigEndian);

/*
 * th8BigintMinTwosComplementBytes --
 *
 *	Compute the minimum number of bytes needed to represent
 *	the value (decimal string) as two's complement.  Used by
 *	`binary format j*` / `J*` to derive the field width.
 *
 *	*pNeeded receives the count; at least 1 even for zero.
 *	Returns TH8_OK on success, TH8_ERROR if zVal is not a
 *	valid integer.
 */

int th8BigintMinTwosComplementBytes(
    Th8_Interp *interp,
    const char *zVal,
    size_t nVal,
    size_t *pNeeded);

#endif /* TH8_ENABLE_BIGINT */
#endif /* TH8_BIGINT_H */
