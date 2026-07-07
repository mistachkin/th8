/*
 * regex_th8.h -- TH8-adapted regex interface header.
 *
 * Derived from PostgreSQL's src/include/regex/regex.h.
 * Removes PostgreSQL dependencies (pg_wchar, Oid, text, etc.)
 * and uses TH8's chr type from regcustom_th8.h.
 *
 * Copyright (c) 1998, 1999 Henry Spencer.  All rights reserved.
 * TH8 adaptation Copyright (c) 2026 by Joe Mistachkin.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef REGEX_TH8_H
#define REGEX_TH8_H

/*
 * This header is included by regcustom_th8.h AFTER it defines
 * chr, size_t (via stddef.h), and other base types.  It should
 * not be included directly.
 */

#ifndef REGCUSTOM_TH8_H
#  include <stddef.h>
#  ifndef _TH8_CHR_DEFINED
#    define _TH8_CHR_DEFINED
typedef unsigned int chr;
typedef unsigned int uchr;
#  endif
#endif

/*
 * interface types
 */

typedef long th8_regoff_t;

/* a compiled RE */
typedef struct {
    int re_magic; /* magic number */
    size_t re_nsub; /* number of subexpressions */
    long re_info; /* info flags (REG_U* below) */
#define REG_UBACKREF    000001
#define REG_ULOOKAROUND 000002
#define REG_UBOUNDS     000004
#define REG_UBRACES     000010
#define REG_UBSALNUM    000020
#define REG_UPBOTCH     000040
#define REG_UBBS        000100
#define REG_UNONPOSIX   000200
#define REG_UUNSPEC     000400
#define REG_UUNPORT     001000
#define REG_ULOCALE     002000
#define REG_UEMPTYMATCH 004000
#define REG_UIMPOSSIBLE 010000
#define REG_USHORTEST   020000
    int re_csize; /* sizeof(character) */
    char *re_endp; /* backward compat kludge */
    unsigned int re_collation; /* collation (unused in TH8) */
    char *re_guts; /* opaque internals */
    char *re_fns;  /* opaque internals */
} th8_regex_t;

/* match result */
typedef struct {
    th8_regoff_t rm_so;  /* start of substring */
    th8_regoff_t rm_eo;  /* end of substring */
} th8_regmatch_t;

/* supplementary detail */
typedef struct {
    th8_regmatch_t rm_extend;
} th8_rm_detail_t;


/*
 * compilation flags
 */

#define REG_BASIC    000000
#define REG_EXTENDED 000001
#define REG_ADVF     000002
#define REG_ADVANCED 000003
#define REG_QUOTE    000004
#define REG_NOSPEC   REG_QUOTE
#define REG_ICASE    000010
#define REG_NOSUB    000020
#define REG_EXPANDED 000040
#define REG_NLSTOP   000100
#define REG_NLANCH   000200
#define REG_NEWLINE  000300
#define REG_PEND     000400
#define REG_EXPECT   001000
#define REG_BOSONLY  002000
#define REG_DUMP     004000
#define REG_FAKE     010000
#define REG_PROGRESS 020000

/*
 * execution flags
 */

#define REG_NOTBOL   0001
#define REG_NOTEOL   0002
#define REG_STARTEND 0004
#define REG_FTRACE   0010
#define REG_MTRACE   0020
#define REG_SMALL    0040

/*
 * error codes
 */

#define REG_OKAY     0
#define REG_NOMATCH  1
#define REG_BADPAT   2
#define REG_ECOLLATE 3
#define REG_ECTYPE   4
#define REG_EESCAPE  5
#define REG_ESUBREG  6
#define REG_EBRACK   7
#define REG_EPAREN   8
#define REG_EBRACE   9
#define REG_BADBR    10
#define REG_ERANGE   11
#define REG_ESPACE   12
#define REG_BADRPT   13
#define REG_ASSERT   15
#define REG_INVARG   16
#define REG_MIXED    17
#define REG_BADOPT   18
#define REG_ETOOBIG  19
#define REG_ECOLORS  20
#define REG_CANCEL   21 /* TH8: interrupted by Th8_Ready */
#define REG_ATOI     101
#define REG_ITOA     102
#define REG_PREFIX   (-1)
#define REG_EXACT    (-2)

/*
 * Redirect standard typenames to our typenames.
 */

#define regoff_t   th8_regoff_t
#define regex_t    th8_regex_t
#define regmatch_t th8_regmatch_t

/*
 * Function prototypes.
 *
 * These use 'chr' (defined in regcustom_th8.h as unsigned int)
 * instead of pg_wchar, and drop the Oid collation parameter.
 */

int th8_regcomp(
    regex_t *re,
    const chr *string,
    size_t len,
    int flags,
    unsigned int collation);
int th8_regexec(
    regex_t *re,
    const chr *string,
    size_t len,
    size_t search_start,
    th8_rm_detail_t *details,
    size_t nmatch,
    regmatch_t pmatch[],
    int flags);
int th8_regprefix(regex_t *re, chr **string, size_t *slength);
void th8_regfree(regex_t *re);
size_t th8_regerror(
    int errcode,
    const regex_t *preg,
    char *errbuf,
    size_t errbuf_size);

#endif /* REGEX_TH8_H */
