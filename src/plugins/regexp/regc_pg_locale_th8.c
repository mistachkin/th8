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
 * pg_set_regex_collation -- called by pg_regcomp to set the
 * collation context.  TH8 ignores collation.
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
 * Parameters:
 *	c -- input code point.
 *
 * Returns:
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
 * Parameters:
 *	c -- input code point.
 *
 * Returns:
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
 * Parameters:
 *	c -- input code point.
 *
 * Returns:
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
 * Parameters:
 *	c -- input code point.
 *
 * Returns:
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
 * Parameters:
 *	c -- input code point.
 *
 * Returns:
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
 * Parameters:
 *	c -- input code point.
 *
 * Returns:
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
 * Parameters:
 *	c -- input code point.
 *
 * Returns:
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
 * Parameters:
 *	c -- input code point.
 *
 * Returns:
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
 * Parameters:
 *	c -- input code point.
 *
 * Returns:
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
 * Parameters:
 *	c -- input code point.
 *
 * Returns:
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
 * Parameters:
 *	c -- input code point.
 *
 * Returns:
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
 * Parameters:
 *	c -- input code point.
 *
 * Returns:
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


/*
 * regc_ctype_get_cache -- Build a cvec of matching characters.
 *
 * PostgreSQL caches these; TH8 builds them on-demand.
 * We scan characters 0..MAX_SIMPLE_CHR (0x7FF) through the
 * probe function and construct a cvec.  This is called once
 * per character class per regex compilation.
 *
 * Returns a pointer to a cvec, or NULL on memory failure.
 * The cvec is allocated via getcvec (defined in regc_cvec.c,
 * included earlier by regcomp.c) which uses the vars struct's
 * memory management.
 */

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
 * pg_char_and_wchar_strncmp -- compare a char* string with a
 * chr* string.  Used by regc_locale.c for collation element
 * matching.
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


/*
 * GetDatabaseEncoding -- PostgreSQL function.  Return a dummy
 * value.  PG_UTF8 is 6 in PostgreSQL.
 */

#define PG_UTF8 6

static int
GetDatabaseEncoding(void)
{
    return PG_UTF8;
}
