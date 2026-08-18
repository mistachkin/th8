/*
 * regc_pg_locale_th8.c -- TH8 replacement for PostgreSQL's
 * regc_pg_locale.c.
 *
 * This file is #include'd by regcomp.c (after patching).
 * It provides the character classification and case conversion
 * functions that the Spencer regex engine needs.
 *
 * In PostgreSQL, these functions delegate to the PostgreSQL
 * locale infrastructure.  In TH8, they use the TH8 character
 * classification functions (currently ASCII, extensible to
 * full Unicode via ConvertUTF_v2 tables).
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

/*
 *----------------------------------------------------------------------
 *
 * pg_set_regex_collation --
 *
 *	Called by pg_regcomp to establish the collation context for a
 *	compilation.  TH8 ignores collation.
 *
 * Why / How:
 *	In PostgreSQL this OID selects the locale that drives
 *	classification and case folding.  TH8 has a single,
 *	locale-independent (ASCII) classification, so the parameter is
 *	accepted for source compatibility with the vendored engine but
 *	is deliberately a no-op -- there is no per-compilation locale
 *	state to set.
 *
 * Parameters:
 *	collation -- collation OID (ignored).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
pg_set_regex_collation(Oid collation)
{
    /* TH8: no collation support; intentional no-op. */
}


/*
 * regc_wc_* -- character classification and case-conversion
 * helpers for the Spencer regex engine.  The engine itself calls
 * the `iscXXX` macros (mapped to `th8_regex_isXXX` by
 * `regcustom_th8.h`), but `regc_locale.c` -- which is also
 * `#include`'d by `regcomp.c` -- calls these `regc_wc_*` names
 * directly when building character-class ranges.  Each function
 * has its own per-function header below; they share the same
 * coverage scope: ASCII semantics for `c < 128` and a flat "no"
 * answer above (the TH8 build can be extended to full Unicode
 * via the ConvertUTF_v2 tables, but the current targets do not
 * require it).
 */

/*
 *----------------------------------------------------------------------
 *
 * regc_wc_isdigit --
 *
 *	ASCII decimal-digit predicate (`'0'..'9'`).  Non-ASCII
 *	input (`c >= 128`) returns 0.
 *
 * Why / How:
 *	regc_locale.c calls these regc_wc_* predicates directly when
 *	building POSIX character-class cvecs (the engine core uses the
 *	separate isXXX macros).  The test is a direct ASCII range
 *	comparison rather than a C-library call so classification is
 *	locale-independent and deterministic; code points at or above
 *	128 answer 0 until Unicode tables are wired in.
 *
 * Parameters:
 *	c -- input code point.
 *
 * Results:
 *	1 if `c` is `'0'..'9'`; 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static int
regc_wc_isdigit(chr c)
{
    return (c < 128) ? (c >= '0' && c <= '9') : 0;
}


/*
 *----------------------------------------------------------------------
 *
 * regc_wc_isalpha --
 *
 *	ASCII alphabetic-letter predicate (`'a'..'z'`, `'A'..'Z'`).
 *	Non-ASCII input returns 0.
 *
 * Why / How:
 *	Same design as regc_wc_isdigit: called by regc_locale.c to
 *	build character-class cvecs, tested inline against the two
 *	ASCII letter ranges so classification stays locale-independent;
 *	code points at or above 128 answer 0.
 *
 * Parameters:
 *	c -- input code point.
 *
 * Results:
 *	1 if `c` is an ASCII letter; 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static int
regc_wc_isalpha(chr c)
{
    if (c < 128) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    }
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * regc_wc_isalnum --
 *
 *	ASCII alphanumeric predicate -- the disjunction of
 *	`regc_wc_isdigit` and `regc_wc_isalpha`.
 *
 * Why / How:
 *	Called by regc_locale.c to build character-class cvecs.
 *	Composed from the digit and letter predicates rather than
 *	duplicating their ranges, so it inherits their ASCII,
 *	locale-independent semantics automatically.
 *
 * Parameters:
 *	c -- input code point.
 *
 * Results:
 *	1 if `c` is an ASCII letter or digit; 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static int
regc_wc_isalnum(chr c)
{
    return regc_wc_isdigit(c) || regc_wc_isalpha(c);
}


/*
 *----------------------------------------------------------------------
 *
 * regc_wc_isword --
 *
 *	Word-character predicate as Tcl `[regexp]` defines it:
 *	alphanumerics plus underscore.  Mirrors `\w` in regex
 *	syntax.
 *
 * Why / How:
 *	Called by regc_locale.c when expanding the `\w` word class.
 *	Built as regc_wc_isalnum plus a literal underscore so the
 *	class exactly matches Tcl's word-character definition and
 *	inherits the ASCII, locale-independent semantics.
 *
 * Parameters:
 *	c -- input code point.
 *
 * Results:
 *	1 if `c` is alphanumeric or `'_'`; 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static int
regc_wc_isword(chr c)
{
    return regc_wc_isalnum(c) || c == '_';
}


/*
 *----------------------------------------------------------------------
 *
 * regc_wc_isupper --
 *
 *	ASCII uppercase-letter predicate (`'A'..'Z'`).
 *	Non-ASCII input returns 0.
 *
 * Why / How:
 *	Same design as regc_wc_isdigit: called by regc_locale.c to
 *	build character-class cvecs, tested inline against the ASCII
 *	uppercase range so classification stays locale-independent;
 *	code points at or above 128 answer 0.
 *
 * Parameters:
 *	c -- input code point.
 *
 * Results:
 *	1 if `c` is `'A'..'Z'`; 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static int
regc_wc_isupper(chr c)
{
    return (c < 128) ? (c >= 'A' && c <= 'Z') : 0;
}


/*
 *----------------------------------------------------------------------
 *
 * regc_wc_islower --
 *
 *	ASCII lowercase-letter predicate (`'a'..'z'`).
 *	Non-ASCII input returns 0.
 *
 * Why / How:
 *	Same design as regc_wc_isdigit: called by regc_locale.c to
 *	build character-class cvecs, tested inline against the ASCII
 *	lowercase range so classification stays locale-independent;
 *	code points at or above 128 answer 0.
 *
 * Parameters:
 *	c -- input code point.
 *
 * Results:
 *	1 if `c` is `'a'..'z'`; 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static int
regc_wc_islower(chr c)
{
    return (c < 128) ? (c >= 'a' && c <= 'z') : 0;
}


/*
 *----------------------------------------------------------------------
 *
 * regc_wc_isgraph --
 *
 *	Graphic-character predicate -- printable ASCII
 *	excluding space (`0x21..0x7E`).  Strict `<` on both
 *	ends so SPACE (`0x20`) and DEL (`0x7F`) are not
 *	graphic.
 *
 * Why / How:
 *	Called by regc_locale.c to build the `[:graph:]` class.
 *	Expressed as a single open-interval range (0x20 < c < 0x7F)
 *	so that both SPACE and the DEL control character are excluded,
 *	matching the POSIX definition of a visible glyph.
 *
 * Parameters:
 *	c -- input code point.
 *
 * Results:
 *	1 if `c` is in `(0x20, 0x7F)`; 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static int
regc_wc_isgraph(chr c)
{
    return (c > 0x20 && c < 0x7F);
}


/*
 *----------------------------------------------------------------------
 *
 * regc_wc_isprint --
 *
 *	Printable-character predicate -- graphic characters
 *	plus the space character itself (`0x20..0x7E`).
 *
 * Why / How:
 *	Called by regc_locale.c to build the `[:print:]` class.
 *	Differs from regc_wc_isgraph only in including SPACE (the
 *	lower bound is inclusive), matching the POSIX distinction that
 *	print = graph plus the space character.
 *
 * Parameters:
 *	c -- input code point.
 *
 * Results:
 *	1 if `c` is in `[0x20, 0x7F)`; 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static int
regc_wc_isprint(chr c)
{
    return (c >= 0x20 && c < 0x7F);
}


/*
 *----------------------------------------------------------------------
 *
 * regc_wc_ispunct --
 *
 *	Punctuation predicate -- graphic ASCII characters
 *	that are not alphanumeric.  Non-ASCII input returns 0.
 *
 * Why / How:
 *	Called by regc_locale.c to build the `[:punct:]` class.
 *	Derived compositionally as "graphic and not alphanumeric"
 *	rather than as an explicit character list, so it stays
 *	consistent with the graph and alnum predicates; non-ASCII
 *	input answers 0.
 *
 * Parameters:
 *	c -- input code point.
 *
 * Results:
 *	1 if `c` is ASCII punctuation; 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static int
regc_wc_ispunct(chr c)
{
    if (c < 128) {
	return regc_wc_isgraph(c) && !regc_wc_isalnum(c);
    }
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * regc_wc_isspace --
 *
 *	Whitespace predicate covering the six ASCII whitespace
 *	characters Tcl `[regexp]` recognises: space, tab,
 *	newline, carriage return, form feed, and vertical tab.
 *
 * Why / How:
 *	Called by regc_locale.c to build the `[:space:]` class.
 *	Enumerated as an explicit six-way comparison rather than a
 *	range because ASCII whitespace is not contiguous, and this
 *	exact set is what Tcl `[regexp]` treats as whitespace.
 *
 * Parameters:
 *	c -- input code point.
 *
 * Results:
 *	1 if `c` is one of the six whitespace characters;
 *	0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static int
regc_wc_isspace(chr c)
{
    return (
        c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
        c == '\v');
}


/*
 *----------------------------------------------------------------------
 *
 * regc_wc_toupper --
 *
 *	ASCII lowercase-to-uppercase conversion.  Non-letter
 *	and non-ASCII input is returned unchanged.
 *
 * Why / How:
 *	Used by regc_locale.c when case-folding characters for
 *	-nocase matching.  Only the ASCII a..z range is folded, by
 *	subtracting the fixed 'a'-'A' offset; every other code point
 *	(including all non-ASCII) is passed through unchanged so no
 *	locale-specific case mapping is assumed.
 *
 * Parameters:
 *	c -- input code point.
 *
 * Results:
 *	`c - ('a' - 'A')` if `c` is `'a'..'z'`; `c` otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static chr
regc_wc_toupper(chr c)
{
    if (c >= 'a' && c <= 'z') {
	return c - ('a' - 'A');
    }
    return c;
}


/*
 *----------------------------------------------------------------------
 *
 * regc_wc_tolower --
 *
 *	ASCII uppercase-to-lowercase conversion.  Non-letter
 *	and non-ASCII input is returned unchanged.
 *
 * Why / How:
 *	The inverse of regc_wc_toupper, used by regc_locale.c for
 *	-nocase case-folding.  Only the ASCII A..Z range is folded, by
 *	adding the fixed 'a'-'A' offset; every other code point
 *	(including all non-ASCII) is passed through unchanged.
 *
 * Parameters:
 *	c -- input code point.
 *
 * Results:
 *	`c + ('a' - 'A')` if `c` is `'A'..'Z'`; `c` otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static chr
regc_wc_tolower(chr c)
{
    if (c >= 'A' && c <= 'Z') {
	return c + ('a' - 'A');
    }
    return c;
}


/*
 * pg_reg_getcolor -- used by regexport.c to map characters to
 * their color in the compiled NFA.  We provide a minimal stub.
 */

/* pg_reg_getcolor is defined in regc_color.c (included by
 * regcomp.c).  We do NOT define it here. */


typedef int (*pg_wc_probefunc)(chr c);

/*
 * Forward declaration: getcvec is defined in regc_cvec.c
 * (which is #include'd by regcomp.c before this file).
 */

/*
 * Note: this function is called from cclasscvec() in
 * regc_locale.c, which passes v (struct vars*) implicitly.
 * We need v to call getcvec().  The caller passes it
 * indirectly -- we add a v parameter and adjust the call
 * site via a macro.
 */

/*
 *----------------------------------------------------------------------
 *
 * regc_ctype_get_cache_impl --
 *
 *	Build a cvec containing every character in 0..MAX_SIMPLE_CHR
 *	(0x7FF) for which `probefunc` returns true, i.e. the members
 *	of a POSIX character class.
 *
 * Why / How:
 *	PostgreSQL caches these vecs; TH8 builds them on demand, once
 *	per character class per regex compilation.  A first pass
 *	counts matches to size the cvec, getcvec (from regc_cvec.c,
 *	using the `vars` struct's memory management) allocates it,
 *	then a second pass fills it.  An empty match set returns a
 *	valid empty cvec rather than NULL, since NULL signals
 *	out-of-memory to the caller.
 *
 * Parameters:
 *	v          -- regex compile vars (for getcvec allocation).
 *	probefunc  -- per-character class-membership predicate.
 *	cclasscode -- class code; accepted for call-site symmetry
 *		      but not consulted by the scan.
 *
 * Results:
 *	A cvec of matching characters, or NULL on memory failure.
 *
 * Side effects:
 *	Allocates a cvec via the vars struct's memory management.
 *
 *----------------------------------------------------------------------
 */

static struct cvec *
regc_ctype_get_cache_impl(
    struct vars *v,
    pg_wc_probefunc probefunc,
    int cclasscode)
{
    struct cvec *cv;
    int nMatches;
    chr c;

    /*
     * First pass: count matching characters to size the cvec.
     */

    nMatches = 0;
    for (c = 0; c <= MAX_SIMPLE_CHR; c++) {
	if (probefunc(c)) {
	    nMatches++;
	}
    }
    if (nMatches == 0) {
	/*
	 * Return a valid empty cvec rather than NULL.
	 * NULL would be interpreted as out-of-memory.
	 */

	return getcvec(v, 0, 0);
    }

    /*
     * Allocate a cvec.  getcvec is provided by regc_cvec.c.
     */

    cv = getcvec(v, nMatches, 0);
    if (cv == 0) {
	return 0;
    }

    /*
     * Second pass: populate the cvec.
     */

    for (c = 0; c <= MAX_SIMPLE_CHR; c++) {
	if (probefunc(c)) {
	    addchr(cv, c);
	}
    }

    return cv;
}

/*
 * Macro to inject the v parameter at the call site.
 * regc_locale.c calls regc_ctype_get_cache(probefunc, code)
 * and v is in scope there.
 */

#define regc_ctype_get_cache(probefunc, code)                                \
    regc_ctype_get_cache_impl(v, (probefunc), (code))


/*
 *----------------------------------------------------------------------
 *
 * pg_char_and_wchar_strncmp --
 *
 *	Compare up to n characters of a narrow char* string against a
 *	wide chr* string.  Used by regc_locale.c for collation-element
 *	name matching.
 *
 * Why / How:
 *	The two strings live in different widths -- collation-element
 *	names arrive as char* while the engine's alphabet is chr* --
 *	so a plain strncmp cannot be used.  Each char byte is widened
 *	to unsigned before comparison to avoid sign issues, and the
 *	scan stops early on the first difference or on a shared NUL,
 *	giving strncmp-style ordering semantics across the two widths.
 *
 * Parameters:
 *	s1 -- narrow (char) string.
 *	s2 -- wide (chr) string.
 *	n  -- maximum number of characters to compare.
 *
 * Results:
 *	-1, 0, or 1 as s1 is less than, equal to, or greater than s2
 *	over the first n characters (or up to a shared NUL).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
pg_char_and_wchar_strncmp(const char *s1, const chr *s2, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
	unsigned int c1 = (unsigned char)s1[i];
	unsigned int c2 = s2[i];

	if (c1 < c2) return -1;
	if (c1 > c2) return 1;
	if (c1 == 0) return 0;
    }
    return 0;
}


#define PG_UTF8 6

/*
 *----------------------------------------------------------------------
 *
 * GetDatabaseEncoding --
 *
 *	Report the server encoding to the vendored engine.  A TH8
 *	stand-in for the PostgreSQL function of the same name.
 *
 * Why / How:
 *	The engine's inherited code queries this to decide how to
 *	interpret bytes.  TH8 always operates on UTF-8 (its chr arrays
 *	are decoded from UTF-8 at the boundary), so this
 *	unconditionally returns PostgreSQL's PG_UTF8 code (6) rather
 *	than consulting any real database-encoding state, which TH8
 *	does not have.
 *
 * Parameters:
 *	(none)
 *
 * Results:
 *	PG_UTF8 (6), always.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
GetDatabaseEncoding(void)
{
    return PG_UTF8;
}
