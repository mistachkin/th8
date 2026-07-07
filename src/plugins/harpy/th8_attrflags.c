/*
 * th8_attrflags.c -- Attribute flags subsystem for TH8.
 *
 * Implements an Eagle compatible attribute flags string format:
 *
 *   Simple:  "abc"          -- flag characters for the default key
 *   Complex: "{HEXKEY:abc}" -- per-key flag characters in braces
 *   Mixed:   "abc{HEXKEY:xy}" -- default flags + keyed flags
 *
 * Change specifications use prefixes: +add, -remove, =set
 *
 * This is a port of Eagle's AttributeFlags.cs to plain C.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "th8.h"
#include "th8_int.h"


/*
 *----------------------------------------------------------------------
 *
 * Constants and helpers.
 *
 *----------------------------------------------------------------------
 */

#define AF_NAME_SEP     ':'
#define AF_MAX_NAME_LEN 16 /* hex digits in a 64-bit key */

/*
 *----------------------------------------------------------------------
 *
 * th8AfFormatHex --
 *
 *	Format a 64-bit unsigned value as an uppercase hexadecimal
 *	string.  Writes into zBuf (must have room for at least 17
 *	bytes).  If bPad is true, zero-pads to exactly 16 digits.
 *
 * Why / How:
 *	Attribute flag keys are 64-bit values that appear as hex
 *	strings in the braced "{HEXKEY:flags}" format.  This helper
 *	converts a key to its hex representation without depending
 *	on sprintf or any CRT formatting function.  It builds the
 *	hex string in reverse using a temporary buffer, then copies
 *	the digits out in the correct order.
 *
 * Results:
 *	The number of characters written (not counting the NUL
 *	terminator).
 *
 * Side effects:
 *	Writes up to 17 bytes (16 hex digits + NUL) into zBuf.
 *
 *----------------------------------------------------------------------
 */

static int
th8AfFormatHex(char *zBuf, th8_uint64_t u, int bPad)
{
    static const char zHex[] = "0123456789ABCDEF";
    char tmp[16];
    int tn = 0;
    int n = 0;

    do {
	tmp[tn++] = zHex[u & 0xF];
	u >>= 4;
    } while (u > 0);
    if (bPad) {
	while (tn < 16)
	    tmp[tn++] = '0';
    }
    while (tn > 0) {
	zBuf[n++] = tmp[--tn];
    }
    zBuf[n] = 0;
    return n;
}
#define AF_ADD_CHAR    '+'
#define AF_REMOVE_CHAR '-'
#define AF_SET_CHAR    '='

/*
 *----------------------------------------------------------------------
 *
 * th8AfIsHexDigit --
 *
 *	Test whether a character is a hexadecimal digit (0-9, a-f,
 *	A-F).
 *
 * Why / How:
 *	The attribute flags parser needs to validate key characters
 *	inside braced groups without depending on ctype.h / isxdigit.
 *	This inline predicate keeps the parser self-contained.
 *
 * Results:
 *	Non-zero if c is a hex digit, zero otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8AfIsHexDigit(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

/*
 *----------------------------------------------------------------------
 *
 * th8AfIsIdentChar --
 *
 *	Test whether a character is a valid flag identifier character
 *	(alphanumeric or underscore).
 *
 * Why / How:
 *	Flag values are restricted to identifier characters to prevent
 *	injection of format-significant characters (braces, colons,
 *	operators) into flag strings.  This predicate enforces that
 *	restriction throughout the parser and change-application code
 *	without depending on ctype.h / isalnum.
 *
 * Results:
 *	Non-zero if c is alphanumeric or underscore, zero otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8AfIsIdentChar(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') || c == '_';
}

/*
 *----------------------------------------------------------------------
 *
 * th8AfIsSpace --
 *
 *	Test whether a character is ASCII whitespace (space, tab,
 *	carriage return, or newline).
 *
 * Why / How:
 *	When the bSpace option is set, the parser skips whitespace
 *	between flags and inside braced groups.  This predicate
 *	defines exactly which characters are treated as whitespace,
 *	avoiding locale-dependent behavior from ctype.h / isspace.
 *
 * Results:
 *	Non-zero if c is whitespace, zero otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8AfIsSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}


/*
 *----------------------------------------------------------------------
 *
 * Per-key flag set helpers.
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8AfFlagSetInit --
 *
 *	Zero-initialize a Th8_FlagSet structure.
 *
 * Why / How:
 *	A Th8_FlagSet holds a 128-element presence bitmap and an
 *	insertion-order array.  Both must start at zero/empty.  Using
 *	Th8_Memset ensures consistent initialization regardless of
 *	the structure's padding or alignment.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Clears all bytes of *p.
 *
 *----------------------------------------------------------------------
 */

static void
th8AfFlagSetInit(Th8_Interp *interp, Th8_FlagSet *p)
{
    Th8_Memset(interp, p, 0, sizeof(Th8_FlagSet));
}

/*
 *----------------------------------------------------------------------
 *
 * th8AfFlagSetAdd --
 *
 *	Add a flag character to a flag set if it is not already
 *	present.  Only 7-bit ASCII characters (< 128) are accepted.
 *
 * Why / How:
 *	Flags are tracked in a flat boolean array (present[]) indexed
 *	by the character's unsigned value for O(1) membership testing.
 *	An insertion-order array (order[]) records the sequence in
 *	which flags were added, so that formatting can reproduce the
 *	original order when bSort is false.  The character is silently
 *	ignored if it is already present or if the order array is
 *	full.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May set p->present[c] and append c to p->order[].
 *
 *----------------------------------------------------------------------
 */

void
th8AfFlagSetAdd(Th8_FlagSet *p, char c)
{
    if ((unsigned char)c < 128 && !p->present[(unsigned char)c]) {
	p->present[(unsigned char)c] = 1;
	if (p->nOrder < 128) {
	    p->order[p->nOrder++] = c;
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * th8AfFlagSetRemove --
 *
 *	Remove a flag character from a flag set.  No-op if the
 *	character is not present or is outside 7-bit ASCII.
 *
 * Why / How:
 *	Change specifications with the '-' prefix need to remove
 *	individual flags.  This function clears the presence bit and
 *	compacts the insertion-order array by shifting subsequent
 *	entries down, preserving the relative order of remaining
 *	flags.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May clear p->present[c] and compact p->order[].
 *
 *----------------------------------------------------------------------
 */

void
th8AfFlagSetRemove(Th8_FlagSet *p, char c)
{
    if ((unsigned char)c < 128 && p->present[(unsigned char)c]) {
	int i, j;

	p->present[(unsigned char)c] = 0;
	for (i = 0, j = 0; i < p->nOrder; i++) {
	    if (p->order[i] != c) {
		p->order[j++] = p->order[i];
	    }
	}
	p->nOrder = j;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * th8AfFlagSetHas --
 *
 *	Test whether a flag character is present in a flag set.
 *
 * Why / How:
 *	Th8_AttrFlagsHave iterates the query string and needs O(1)
 *	membership testing for each character.  This function indexes
 *	directly into the presence bitmap, returning zero for out-of-
 *	range characters without branching into undefined memory.
 *
 * Results:
 *	Non-zero if c is present, zero otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8AfFlagSetHas(const Th8_FlagSet *p, char c)
{
    return ((unsigned char)c < 128) ? p->present[(unsigned char)c] : 0;
}

/*
 *----------------------------------------------------------------------
 *
 * Key-to-flagset map -- array of (key, FlagSet) pairs.
 * Key 0 is the "default" (non-keyed) flags.
 *
 *----------------------------------------------------------------------
 */

/* Types defined in th8.h: Th8_FlagSet, Th8_AfKeyEntry, Th8_AfMap */

#define AF_MAX_KEYS TH8_AF_MAX_KEYS

/*
 *----------------------------------------------------------------------
 *
 * th8AfMapInit --
 *
 *	Initialize a key-to-flagset map to empty.
 *
 * Why / How:
 *	The map is a small fixed-size array of (key, FlagSet) pairs.
 *	Setting n to zero marks it as empty; no memory allocation is
 *	needed because the array is embedded in the Th8_AfMap struct.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets p->n to 0.
 *
 *----------------------------------------------------------------------
 */

static void
th8AfMapInit(Th8_AfMap *p)
{
    p->n = 0;
}

/*
 *----------------------------------------------------------------------
 *
 * th8AfMapGet --
 *
 *	Look up or create a flag set for a given 64-bit key in the
 *	map.  Key 0 represents the default (non-keyed) flag set.
 *
 * Why / How:
 *	Attribute flags can be partitioned by key (e.g. per signing
 *	key).  The map is a linear array searched by key value; this
 *	is efficient because the number of distinct keys is small
 *	(bounded by AF_MAX_KEYS).  When bCreate is true and the key
 *	is not found, a new entry is appended and its flag set is
 *	zero-initialized.
 *
 * Results:
 *	Pointer to the Th8_FlagSet for the key, or NULL if the key
 *	is not found and bCreate is false (or the map is full).
 *
 * Side effects:
 *	May grow the map by one entry when bCreate is true.
 *
 *----------------------------------------------------------------------
 */

Th8_FlagSet *
th8AfMapGet(Th8_Interp *interp, Th8_AfMap *p, th8_int64_t key, int bCreate)
{
    int i;

    for (i = 0; i < p->n; i++) {
	if (p->a[i].key == key) return &p->a[i].flags;
    }
    if (bCreate && p->n < AF_MAX_KEYS) {
	i = p->n++;
	p->a[i].key = key;
	th8AfFlagSetInit(interp, &p->a[i].flags);
	return &p->a[i].flags;
    }
    return NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * th8AfParseHexKey --
 *
 *	Parse a hex string into a 64-bit key value.
 *
 * Why / How:
 *	Inside braced flag groups, the key name is a hex-encoded
 *	64-bit value (e.g. a public key token).  This function
 *	converts the hex string digit-by-digit into a th8_int64_t,
 *	rejecting any non-hex character immediately.  It avoids
 *	strtoll / CRT dependencies for portability.
 *
 * Results:
 *	TH8_OK with *pKey set on success.  TH8_ERROR if any
 *	character is not a valid hex digit.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
th8AfParseHexKey(const char *z, size_t n, th8_int64_t *pKey)
{
    /*
     * Bug 46 (UBSan): accumulate into th8_uint64_t to avoid
     * signed-shift-overflow UB for hex strings whose value
     * has the high bit set.  Cast to the signed key type
     * once at the end.  Mirrors the fix in th8_harpy.c
     * flags_have_command.
     */
    th8_uint64_t val = 0;
    size_t i;

    for (i = 0; i < n; i++) {
	char c = z[i];
	int d;

	if (c >= '0' && c <= '9')
	    d = c - '0';
	else if (c >= 'a' && c <= 'f')
	    d = c - 'a' + 10;
	else if (c >= 'A' && c <= 'F')
	    d = c - 'A' + 10;
	else
	    return TH8_ERROR;
	val = (val << 4) | (th8_uint64_t)d;
    }
    *pKey = (th8_int64_t)val;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_AttrFlagsParse --
 *
 *	Parse an attribute flags string into a key-to-flagset map.
 *
 * Why / How:
 *	Harpy certificates carry attribute flags that control what a
 *	signed script is allowed to do, scoped by signing key.  The
 *	string format supports simple flags ("abc"), keyed flags
 *	("{HEXKEY:xy}"), and mixed forms.  This single-pass parser
 *	walks the input character-by-character, tracking brace depth
 *	and key-name state, populating the map with one flag set per
 *	distinct key.  Strict validation rejects malformed input
 *	(nested braces, missing keys, invalid characters) rather than
 *	silently ignoring it, which is critical for security policy
 *	enforcement.
 *
 * Results:
 *	TH8_OK on success with *pMap populated.  TH8_ERROR with an
 *	error message in interp if the input is malformed.
 *
 * Side effects:
 *	None beyond filling in *pMap.
 *
 *----------------------------------------------------------------------
 */

int
Th8_AttrFlagsParse(
    Th8_Interp *interp,
    const char *zText,
    size_t nText,
    int bComplex,
    int bSpace,
    Th8_AfMap *pMap)
{
    size_t i = 0;
    char zName[AF_MAX_NAME_LEN + 1];
    int nName = 0;
    int bOpen = 0;
    int bHaveName = 0;
    int bNameSepOk = 1;
    th8_int64_t key = 0;

    th8AfMapInit(pMap);
    if (!zText) return TH8_OK;
    if (nText == TH8_NOLEN) nText = Th8_Strlen(interp, zText);

    for (i = 0; i < nText; i++) {
	char c = zText[i];

	if (c == '{') {
	    if (!bComplex) {
		Th8_SetResultStatic(
		    interp, "flags: unexpected '{' in simple mode",
		    TH8_NOLEN);
		return TH8_ERROR;
	    }
	    if (bOpen) {
		Th8_SetResultStatic(interp, "flags: nested '{'", TH8_NOLEN);
		return TH8_ERROR;
	    }
	    bOpen = 1;
	    nName = 0;
	    bHaveName = 0;
	    bNameSepOk = 1;
	    continue;
	}

	if (c == '}') {
	    if (!bComplex || !bOpen) {
		Th8_SetResultStatic(
		    interp, "flags: unexpected '}'", TH8_NOLEN);
		return TH8_ERROR;
	    }
	    /* Split per Finding 005 sec. 5b: C2 (bHaveName=T with
	     * nName=0) is intrinsic-dead -- the parser only sets
	     * bHaveName=1 after collecting at least one name char,
	     * so nName grows along with bHaveName. */
	    if (!bHaveName) {
		Th8_SetResultStatic(
		    interp, "flags: '}' without complete key", TH8_NOLEN);
		return TH8_ERROR;
	    }
	    if (nName == 0) {
		Th8_SetResultStatic(
		    interp, "flags: '}' without complete key", TH8_NOLEN);
		return TH8_ERROR;
	    }
	    /* Key was already parsed when bHaveName was set. */
	    bOpen = 0;
	    bHaveName = 0;
	    bNameSepOk = 1;
	    continue;
	}

	if (bOpen && !bHaveName) {
	    /* Inside braces, collecting hex key name. */
	    if (bSpace && th8AfIsSpace(c)) continue;

	    if (c == AF_NAME_SEP) {
		if (nName == 0) {
		    Th8_SetResultStatic(
		        interp, "flags: ':' without key name", TH8_NOLEN);
		    return TH8_ERROR;
		}
		zName[nName] = 0;
		if (th8AfParseHexKey(zName, (size_t)nName, &key) != TH8_OK) {
		    Th8_SetResultStatic(
		        interp, "flags: invalid hex key", TH8_NOLEN);
		    return TH8_ERROR;
		}
		bHaveName = 1;
		bNameSepOk = 0;
		continue;
	    }

	    if (!th8AfIsHexDigit(c)) {
		Th8_SetResultStatic(
		    interp, "flags: invalid key character", TH8_NOLEN);
		return TH8_ERROR;
	    }

	    if (nName >= AF_MAX_NAME_LEN) {
		Th8_SetResultStatic(interp, "flags: key too long", TH8_NOLEN);
		return TH8_ERROR;
	    }
	    zName[nName++] = c;

	    /* Legacy format: 16 hex digits with no colon.  bNameSepOk
	     * is structurally always T at this point: entry to the
	     * !bHaveName block above required bOpen=1 from the `{`
	     * branch at L526 (which resets bNameSepOk=1), and the
	     * only inner site that clears it (L565) also sets
	     * bHaveName=1 -- exiting this block before we get here. */
	    if (nName == AF_MAX_NAME_LEN && ALWAYS(bNameSepOk)) {
		zName[nName] = 0;
		if (th8AfParseHexKey(zName, (size_t)nName, &key) != TH8_OK) {
		    Th8_SetResultStatic(
		        interp, "flags: invalid hex key", TH8_NOLEN);
		    return TH8_ERROR;
		}
		bHaveName = 1;
	    }
	    continue;
	}

	/* Value character (flag). */
	if (bSpace && th8AfIsSpace(c)) continue;

	if (c == AF_NAME_SEP && bComplex) {
	    /* Colon outside braces in complex mode. */
	    if (!bNameSepOk) {
		Th8_SetResultStatic(
		    interp, "flags: unexpected ':'", TH8_NOLEN);
		return TH8_ERROR;
	    }
	    bNameSepOk = 0;
	    continue;
	}

	if (!th8AfIsIdentChar(c)) {
	    Th8_SetResultStatic(
	        interp, "flags: invalid flag character", TH8_NOLEN);
	    return TH8_ERROR;
	}

	{
	    th8_int64_t useKey = bOpen ? key : 0;
	    Th8_FlagSet *pFs = th8AfMapGet(interp, pMap, useKey, 1);

	    if (!pFs) {
		Th8_SetResultStatic(
		    interp, "flags: too many keys", TH8_NOLEN);
		return TH8_ERROR;
	    }
	    th8AfFlagSetAdd(pFs, c);
	}
    }

    if (bOpen) {
	Th8_SetResultStatic(interp, "flags: missing '}'", TH8_NOLEN);
	return TH8_ERROR;
    }

    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_AttrFlagsFormat --
 *
 *	Format a flag map back to a string representation.
 *
 * Why / How:
 *	After programmatic manipulation (parse, change, check), the
 *	flag map needs to be serialized back to the canonical string
 *	format for storage in certificates or display.  This function
 *	iterates every key in the map: key-0 entries emit bare flags,
 *	non-zero keys emit "{HEXKEY:flags}" groups.  The bLegacy flag
 *	controls whether keys are zero-padded without a colon (Eagle
 *	legacy format) or use the modern colon-separated form.  The
 *	bSort flag controls whether flags appear in ASCII order or
 *	insertion order.
 *
 * Results:
 *	TH8_OK always.  *pzOut and *pnOut are set to the allocated
 *	string and its byte length.  The caller must free *pzOut
 *	with Th8_Free.
 *
 * Side effects:
 *	Allocates memory for the output string via Th8_StringAppend.
 *
 *----------------------------------------------------------------------
 */

int
Th8_AttrFlagsFormat(
    Th8_Interp *interp,
    const Th8_AfMap *pMap,
    int bLegacy,
    int bCompact,
    int bSpace,
    int bSort,
    char **pzOut,
    size_t *pnOut)
{
    char *zOut = NULL;
    size_t nOut = 0;
    int i, c;

    (void)bCompact; /* reserved */

    for (i = 0; i < pMap->n; i++) {
	th8_int64_t key = pMap->a[i].key;
	const Th8_FlagSet *pFs = &pMap->a[i].flags;

	if (bSpace && nOut > 0) {
	    Th8_StringAppend(interp, &zOut, &nOut, " ", 1);
	}

	if (key != 0) {
	    char zBuf[18];  /* 16 hex + ':' + NUL */
	    int n;

	    Th8_StringAppend(interp, &zOut, &nOut, "{", 1);
	    n = th8AfFormatHex(zBuf, (th8_uint64_t)key, bLegacy);
	    if (!bLegacy) {
		zBuf[n++] = ':';
		zBuf[n] = 0;
	    }
	    Th8_StringAppend(interp, &zOut, &nOut, zBuf, (size_t)n);

	    if (bSort) {
		for (c = 0; c < 128; c++) {
		    if (pFs->present[c]) {
			char ch = (char)c;

			Th8_StringAppend(interp, &zOut, &nOut, &ch, 1);
		    }
		}
	    } else {
		/* Insertion order. */
		for (c = 0; c < pFs->nOrder; c++) {
		    Th8_StringAppend(interp, &zOut, &nOut, &pFs->order[c], 1);
		}
	    }
	    Th8_StringAppend(interp, &zOut, &nOut, "}", 1);
	} else {
	    /* Default key: emit flags. */
	    if (bSort) {
		for (c = 0; c < 128; c++) {
		    if (pFs->present[c]) {
			char ch = (char)c;

			Th8_StringAppend(interp, &zOut, &nOut, &ch, 1);
		    }
		}
	    } else {
		for (c = 0; c < pFs->nOrder; c++) {
		    Th8_StringAppend(interp, &zOut, &nOut, &pFs->order[c], 1);
		}
	    }
	}
    }

    *pzOut = zOut;
    *pnOut = nOut;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Wildcard expansion.
 *
 *	*  = all alphanumeric (0-9, A-Z, a-z)
 *	#  = all digits (0-9)
 *	!  = all letters (A-Z, a-z)
 *	$  = uppercase letters (A-Z)
 *	@  = lowercase letters (a-z)
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8AfIsWildcard --
 *
 *	Test whether a character is a wildcard flag specifier.
 *
 * Why / How:
 *	Five wildcard characters expand to predefined character sets:
 *	'*' (all alphanumeric), '#' (digits), '!' (letters), '$'
 *	(uppercase), '@' (lowercase).  This predicate identifies them
 *	so the parser, change, and query functions can dispatch to the
 *	appropriate expansion logic.
 *
 * Results:
 *	Non-zero if c is a wildcard character, zero otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8AfIsWildcard(char c)
{
    return c == '*' || c == '#' || c == '!' || c == '$' || c == '@';
}

/*
 *----------------------------------------------------------------------
 *
 * th8AfExpandWildcard --
 *
 *	Expand a wildcard character into its full set of flag
 *	characters, applying the given operation (add or remove)
 *	to each one.
 *
 * Why / How:
 *	Change specifications like "+*" or "-#" need to add or remove
 *	entire character classes in one step.  Rather than duplicating
 *	the expansion loops for add and remove, this function accepts
 *	a function pointer (xOp) and calls it for every character in
 *	the wildcard's expansion.  The wildcard definitions match
 *	Eagle's AttributeFlags.cs behavior.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies *pFs by calling xOp for each expanded character.
 *
 *----------------------------------------------------------------------
 */

static void
th8AfExpandWildcard(
    char wc,
    Th8_FlagSet *pFs,
    void (*xOp)(Th8_FlagSet *, char))
{
    int c;

    switch (wc) {
    case '*':
	for (c = '0'; c <= '9'; c++)
	    xOp(pFs, (char)c);
	for (c = 'A'; c <= 'Z'; c++)
	    xOp(pFs, (char)c);
	for (c = 'a'; c <= 'z'; c++)
	    xOp(pFs, (char)c);
	break;
    case '#':
	for (c = '0'; c <= '9'; c++)
	    xOp(pFs, (char)c);
	break;
    case '!':
	for (c = 'A'; c <= 'Z'; c++)
	    xOp(pFs, (char)c);
	for (c = 'a'; c <= 'z'; c++)
	    xOp(pFs, (char)c);
	break;
    case '$':
	for (c = 'A'; c <= 'Z'; c++)
	    xOp(pFs, (char)c);
	break;
    case '@':
	for (c = 'a'; c <= 'z'; c++)
	    xOp(pFs, (char)c);
	break;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * th8AfWildcardHaveAll --
 *
 *	Check if a flag set contains ALL characters in a wildcard
 *	expansion.
 *
 * Why / How:
 *	When Th8_AttrFlagsHave is called with bAll=1 and encounters
 *	a wildcard in the query string, every character in the
 *	wildcard's expansion must be present for the test to pass.
 *	This function iterates the expansion and returns 0 as soon
 *	as any character is missing (short-circuit).
 *
 * Results:
 *	1 if all expanded characters are present, 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8AfWildcardHaveAll(const Th8_FlagSet *pFs, char wc)
{
    int c;

    switch (wc) {
    case '*':
	for (c = '0'; c <= '9'; c++)
	    if (!pFs->present[c]) return 0;
	for (c = 'A'; c <= 'Z'; c++)
	    if (!pFs->present[c]) return 0;
	for (c = 'a'; c <= 'z'; c++)
	    if (!pFs->present[c]) return 0;
	return 1;
    case '#':
	for (c = '0'; c <= '9'; c++)
	    if (!pFs->present[c]) return 0;
	return 1;
    case '!':
	for (c = 'A'; c <= 'Z'; c++)
	    if (!pFs->present[c]) return 0;
	for (c = 'a'; c <= 'z'; c++)
	    if (!pFs->present[c]) return 0;
	return 1;
    case '$':
	for (c = 'A'; c <= 'Z'; c++)
	    if (!pFs->present[c]) return 0;
	return 1;
    case '@':
	for (c = 'a'; c <= 'z'; c++)
	    if (!pFs->present[c]) return 0;
	return 1;
    }
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * th8AfWildcardHaveAny --
 *
 *	Check if a flag set contains ANY character in a wildcard
 *	expansion.
 *
 * Why / How:
 *	When Th8_AttrFlagsHave is called with bAll=0 and encounters
 *	a wildcard, a single matching character is sufficient.  This
 *	function iterates the expansion and returns 1 as soon as any
 *	character is found (short-circuit).
 *
 * Results:
 *	1 if at least one expanded character is present, 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8AfWildcardHaveAny(const Th8_FlagSet *pFs, char wc)
{
    int c;

    switch (wc) {
    case '*':
	for (c = '0'; c <= '9'; c++)
	    if (pFs->present[c]) return 1;
	for (c = 'A'; c <= 'Z'; c++)
	    if (pFs->present[c]) return 1;
	for (c = 'a'; c <= 'z'; c++)
	    if (pFs->present[c]) return 1;
	return 0;
    case '#':
	for (c = '0'; c <= '9'; c++)
	    if (pFs->present[c]) return 1;
	return 0;
    case '!':
	for (c = 'A'; c <= 'Z'; c++)
	    if (pFs->present[c]) return 1;
	for (c = 'a'; c <= 'z'; c++)
	    if (pFs->present[c]) return 1;
	return 0;
    case '$':
	for (c = 'A'; c <= 'Z'; c++)
	    if (pFs->present[c]) return 1;
	return 0;
    case '@':
	for (c = 'a'; c <= 'z'; c++)
	    if (pFs->present[c]) return 1;
	return 0;
    }
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_AttrFlagsHave --
 *
 *	Check if a flag set contains the requested flags.
 *
 * Why / How:
 *	Certificate validation needs to verify that a script's
 *	required capability flags are present in the certificate's
 *	granted flags for a specific signing key.  This function
 *	looks up the flag set for the given key, then iterates the
 *	query string.  In "have-all" mode (bAll=1) every queried
 *	flag must be present; in "have-any" mode (bAll=0) a single
 *	match suffices.  Wildcard characters in the query expand to
 *	their full character classes via th8AfWildcardHaveAll /
 *	th8AfWildcardHaveAny.  Strict mode rejects query strings
 *	containing invalid characters.
 *
 * Results:
 *	Non-zero if the flags are satisfied, zero otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Th8_AttrFlagsHave(
    const Th8_AfMap *pMap,
    th8_int64_t key,
    const char *zHave,
    size_t nHave,
    int bAll,
    int bStrict)
{
    const Th8_FlagSet *pFs;
    size_t i;

    pFs = NULL;
    {
	int j;

	for (j = 0; j < pMap->n; j++) {
	    if (pMap->a[j].key == key) {
		pFs = &pMap->a[j].flags;
		break;
	    }
	}
    }

    if (!zHave || nHave == 0) return 1; /* have-none */
    if (!pFs) return 0;

    /* In strict mode, reject if any character is invalid. */
    if (bStrict) {
	for (i = 0; i < nHave; i++) {
	    if (!th8AfIsIdentChar(zHave[i]) && !th8AfIsWildcard(zHave[i])) {
		return 0;
	    }
	}
    }

    for (i = 0; i < nHave; i++) {
	char c = zHave[i];

	if (th8AfIsWildcard(c)) {
	    if (bAll) {
		if (!th8AfWildcardHaveAll(pFs, c)) return 0;
	    } else {
		if (th8AfWildcardHaveAny(pFs, c)) return 1;
	    }
	    continue;
	}
	if (!th8AfIsIdentChar(c)) {
	    if (bStrict) return 0;
	    continue;
	}
	if (th8AfFlagSetHas(pFs, c)) {
	    if (!bAll) return 1; /* have-any */
	} else {
	    if (bAll) return 0; /* not-have-all */
	}
    }
    return bAll; /* have-all if bAll, else not-have-any */
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_AttrFlagsChange --
 *
 *	Apply a change specification to a flag map.
 *	The change string is parsed character-by-character:
 *	  +X  = add flag X
 *	  -X  = remove flag X
 *	  =X  = set (clear all, then add X)
 *	  X   = add flag X (default is add)
 *
 * Why / How:
 *	Certificate management tools need to programmatically modify
 *	flags without rebuilding the entire string.  This function
 *	walks the change specification maintaining a current mode
 *	(add, remove, or set).  The '=' prefix clears all existing
 *	flags for the key before adding, enabling atomic replacement.
 *	Wildcard characters in the change spec are expanded via
 *	th8AfExpandWildcard so that "+*" adds all alphanumeric flags
 *	and "-#" removes all digit flags in one operation.
 *
 * Results:
 *	TH8_OK on success.  TH8_ERROR with an error message if the
 *	change string contains an invalid character or the map is
 *	full.
 *
 * Side effects:
 *	Modifies the flag set for the given key in *pMap.
 *
 *----------------------------------------------------------------------
 */

int
Th8_AttrFlagsChange(
    Th8_Interp *interp,
    Th8_AfMap *pMap,
    const char *zChange,
    size_t nChange,
    th8_int64_t key)
{
    size_t i;
    int mode = AF_ADD_CHAR;  /* default: add */
    Th8_FlagSet *pFs;

    if (!zChange) return TH8_OK;
    if (nChange == TH8_NOLEN) nChange = Th8_Strlen(interp, zChange);

    pFs = th8AfMapGet(interp, pMap, key, 1);
    if (!pFs) {
	Th8_SetResultStatic(interp, "flags change: too many keys", TH8_NOLEN);
	return TH8_ERROR;
    }

    for (i = 0; i < nChange; i++) {
	char c = zChange[i];

	if (c == AF_ADD_CHAR || c == AF_REMOVE_CHAR || c == AF_SET_CHAR) {
	    if (c == AF_SET_CHAR) {
		/* Clear all flags for this key. */
		th8AfFlagSetInit(interp, pFs);
	    }
	    mode = c;
	    continue;
	}
	if (th8AfIsWildcard(c)) {
	    if (mode == AF_REMOVE_CHAR) {
		th8AfExpandWildcard(c, pFs, th8AfFlagSetRemove);
	    } else {
		th8AfExpandWildcard(c, pFs, th8AfFlagSetAdd);
	    }
	    continue;
	}
	if (!th8AfIsIdentChar(c)) {
	    Th8_SetResultStatic(
	        interp, "flags change: invalid character", TH8_NOLEN);
	    return TH8_ERROR;
	}
	if (mode == AF_ADD_CHAR || mode == AF_SET_CHAR) {
	    th8AfFlagSetAdd(pFs, c);
	} else {
	    th8AfFlagSetRemove(pFs, c);
	}
    }
    return TH8_OK;
}
