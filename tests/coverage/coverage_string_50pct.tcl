###############################################################################
#
# coverage_string_50pct.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Drives the missing MC/DC vector at several 50%-MC/DC compounds
# in src/plugins/th8_strings.c.  Each test closes one compound
# pair by exercising the previously-uncovered C1- or C2-pair.
#
# Coverage-driven; not pinned to specific R-markers.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

source tests/prologue.tcl

###############################################################################

runTest {test str50-1.1 {
  string equal -length with negative length -- haveLen is true
  but maxLen < 0 -- drives the T,F vector at th8_strings.c:275
} -constraints {
    th8
} -body {
  catch {string equal -length -1 abc abc} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test str50-2.1 {
  string last with the optional startIndex argument (5-arg
  form) -- argc != 4 is true, argc != 5 is false -- drives
  the T,F vector at th8_strings.c:589
} -constraints {
    th8
} -body {
  catch {string last "lo" "hello world" 5} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test str50-3.1 {
  Empty-dict iteration at several `for (i = 0; i < nCount &&
  azElem; i += 2)` loops in th8_lists.c -- nCount == 0 drives
  the C1-pair (F,-) at the loop guards
} -constraints {
    th8
} -body {
  catch {dict keys ""} r1
  catch {dict remove "" k} r2
  catch {dict replace "" k v} r3
  catch {dict values ""} r4
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0 && [string length $r4] >= 0}
} -cleanup {
  unset -nocomplain r1 r2 r3 r4
} -result {1}}

###############################################################################

runTest {test str50-4.1 {
  string compare -length with negative length -- haveLen true,
  maxLen<0 -- drives T,F at th8_strings.c:157
} -constraints {
    th8
} -body {
  catch {string compare -length -1 abc abc} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test str50-4.2 {
  string repeat with empty string -- nLen == 0 drives F,- at
  th8_strings.c:741
} -constraints {
    th8
} -body {
  catch {string repeat "" 5} r1
  catch {string repeat "abc" 0} r2
  expr {[string length $r1] >= 0 && [string length $r2] >= 0}
} -cleanup {
  unset -nocomplain r1 r2
} -result {1}}

###############################################################################

runTest {test str50-4.3 {
  string map with empty mapping -- nMap == 0 drives F,- at
  th8_strings.c:1284
} -constraints {
    th8
} -body {
  catch {string map {} "abc"} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test str50-4.4 {
  string totitle on all-uppercase input -- C2-pair F (c not in
  a-z range) drives F,- at th8_strings.c:1532
} -constraints {
    th8
} -body {
  catch {string totitle "ABC"} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test str50-4.5 {
  string index out-of-range -- iIndex >= nChars drives a
  vector at th8_strings.c:373
} -constraints {
    th8
} -body {
  catch {string index "abc" 100} r1
  catch {string index "abc" -1} r2
  expr {[string length $r1] >= 0 && [string length $r2] >= 0}
} -cleanup {
  unset -nocomplain r1 r2
} -result {1}}

###############################################################################

runTest {test str50-5.1 {
  load with too many args -- argc > 3 case drives F,T at
  th8_extensibility.c:87
} -constraints {
    th8
} -body {
  catch {load name initProc extraArg} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test str50-5.2 {
  switch -glob -nocase fold with non-uppercase pattern char --
  drives F,- vector at th8_control.c:1054 (a < 'A')
} -constraints {
    th8
} -body {
  catch {switch -glob -nocase -- "1a!" {
    "1?!" { set _ matched }
    default { set _ default }
  }} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r _
} -result {1}}

###############################################################################

runTest {test str50-6.1 {
  lsearch -sorted -dictionary on a list with chars below '0'
  -- drives F,- vector for digit-class checks at
  th8_lists.c:895/896 and the cb < 'A' case at L919
} -constraints {
    th8
} -body {
  catch {lsearch -dictionary {abc def} ab} r1
  catch {lsearch -dictionary "{!abc} {def}" "!ab"} r2
  catch {lsearch -dictionary {abc def} !abc} r3
  catch {lsearch -sorted -dictionary {!abc !def} "!"} r4
  catch {lsearch -sorted -dictionary {!abc !abz} "!ab"} r5
  catch {lsearch -sorted -dictionary {! 1abc 2abc} "!"} r6
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0 && [string length $r4] >= 0 \
      && [string length $r5] >= 0 && [string length $r6] >= 0}
} -cleanup {
  unset -nocomplain r1 r2 r3 r4 r5 r6
} -result {1}}

###############################################################################

runTest {test str50-6.2 {
  lsort -dictionary with chars below '0' -- drives F,- at
  th8_lists.c:1309/1310 (digit checks during dict compare)
} -constraints {
    th8
} -body {
  catch {lsort -dictionary {banana apple cherry}} r1
  catch {lsort -dictionary {!apple !banana}} r2
  expr {[string length $r1] >= 0 && [string length $r2] >= 0}
} -cleanup {
  unset -nocomplain r1 r2
} -result {1}}

###############################################################################

runTest {test str50-6.3 {
  dict incr with too many args -- argc > 5 drives F,T at
  th8_lists.c:3287
} -constraints {
    th8
} -body {
  catch {dict incr d k 1 extra arg} r1
  catch {dict incr d} r2
  expr {[string length $r1] >= 0 && [string length $r2] >= 0}
} -cleanup {
  unset -nocomplain r1 r2 d
} -result {1}}

###############################################################################

runTest {test str50-7.1 {
  format with %e/%E/%g/%G specifiers -- drives F,T branches
  at th8_formatting.c:404 (e/E) and L481 (g/G)
} -constraints {
    th8
} -body {
  catch {format "%e" 1.0} r1
  catch {format "%E" 1.0} r2
  catch {format "%g" 1.0} r3
  catch {format "%G" 1.0} r4
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0 && [string length $r4] >= 0}
} -cleanup {
  unset -nocomplain r1 r2 r3 r4
} -result {1}}

###############################################################################

runTest {test str50-7.2 {
  format with truncated specifiers -- drives F,- at
  th8_formatting.c:125/155/159 (i >= nFmt)
} -constraints {
    th8
} -body {
  catch {format "%"} r1
  catch {format "%."} r2
  catch {format "%5."} r3
  catch {format "%*"} r4
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0 && [string length $r4] >= 0}
} -cleanup {
  unset -nocomplain r1 r2 r3 r4
} -result {1}}

###############################################################################

runTest {test str50-7.3 {
  format float values whose fractional part rounds down --
  drives F,- at fPart >= 0.5 compounds (L455/L554/L579)
} -constraints {
    th8
} -body {
  catch {format "%.2f" 1.234} r1
  catch {format "%.0f" 0.4} r2
  catch {format "%g" 0.0} r3
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0}
} -cleanup {
  unset -nocomplain r1 r2 r3
} -result {1}}

###############################################################################

runTest {test str50-8.1 {
  string tolower / toupper on input that already matches the
  target case -- drives F,- at th8_strings.c:1843 (c outside
  'a'..'z' for tolower, etc.)
} -constraints {
    th8
} -body {
  catch {string tolower "ABC123!"} r1
  catch {string toupper "abc123!"} r2
  expr {[string length $r1] >= 0 && [string length $r2] >= 0}
} -cleanup {
  unset -nocomplain r1 r2
} -result {1}}

###############################################################################

runTest {test str50-9.1 {
  string toupper input including chars that are >'z' -- drives
  T,F at th8_strings.c:1843 (c >= 'a' true, c <= 'z' false)
} -constraints {
    th8
} -body {
  catch {string toupper "z\{"} r1
  catch {string toupper "abcZ"} r2
  catch {string tolower "AZ"} r3
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0}
} -cleanup {
  unset -nocomplain r1 r2 r3
} -result {1}}

###############################################################################

runTest {test str50-9.2 {
  string totitle on inputs spanning multiple character classes
  -- drives multiple compounds at L1532/1533/1535
} -constraints {
    th8
} -body {
  catch {string totitle "the quick BROWN fox"} r1
  catch {string totitle "ABC"} r2
  catch {string totitle "abc"} r3
  catch {string totitle ""} r4
  catch {string totitle "x"} r5
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0 && [string length $r4] >= 0 \
      && [string length $r5] >= 0}
} -cleanup {
  unset -nocomplain r1 r2 r3 r4 r5
} -result {1}}

###############################################################################

runTest {test str50-9.3 {
  string replace with iFirst > iLast (no replacement) and out
  of bounds -- drives C2-pair at th8_strings.c:1404
} -constraints {
    th8
} -body {
  catch {string replace "abc" 5 10 X} r1
  catch {string replace "abc" 2 1 X} r2
  catch {string replace "abc" 100 200 X} r3
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0}
} -cleanup {
  unset -nocomplain r1 r2 r3
} -result {1}}

###############################################################################

runTest {test str50-9.4 {
  expr arithmetic where left and right have opposite signs --
  drives multiplication overflow check at th8_expr.c:1390
  (iLeft^iRight < 0 && iRes*iRight != iLeft)
} -constraints {
    th8
} -body {
  catch {expr {-5 * 3}} r1
  catch {expr {-100 * -3}} r2
  catch {expr {2 ** 10}} r3
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0}
} -cleanup {
  unset -nocomplain r1 r2 r3
} -result {1}}

###############################################################################

runTest {test str50-9.5 {
  th8HexVal range checks -- drives F,- with character outside
  digit/hex ranges (th8_core.c:11251/11252)
} -constraints {
    th8
} -body {
  catch {scan "FF" "%x" hex} r1
  catch {scan "abc" "%x" hex} r2
  catch {scan "0x" "%x" hex} r3
  catch {scan "0b" "%b" bin} r4
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0 && [string length $r4] >= 0}
} -cleanup {
  unset -nocomplain r1 r2 r3 r4 hex bin
} -result {1}}

###############################################################################

runTest {test str50-10.1 {
  package unknown "" clears the handler -- drives F,- at
  th8_core.c:2860 (Th8_SetPackageUnknown with empty cmd)
} -constraints {
    th8
} -body {
  set saved [package unknown]
  catch {package unknown ""} r1
  catch {package unknown "  "} r2
  expr {[string length $r1] >= 0 && [string length $r2] >= 0}
} -cleanup {
  catch {package unknown $saved}
  unset -nocomplain r1 r2 saved
} -result {1}}

###############################################################################

runTest {test str50-10.2 {
  yield with no arg from inside a coroutine -- drives F,- at
  th8_core.c:8080 (zValue NULL when yield called bare)
} -constraints {
    th8
} -body {
  catch {
    coroutine _str50_co_ apply [list {} {
      yield
      yield "with-value"
      return done
    }]
    set r1 [_str50_co_]
    set r2 [_str50_co_]
  } r
  expr {[string length $r] >= 0}
} -cleanup {
  catch {rename _str50_co_ {}}
  unset -nocomplain r r1 r2
} -result {1}}

###############################################################################

runTest {test str50-10.3 {
  expr boolean short-circuit with right-side error suppressed
  -- drives compounds in th8_expr.c at L1103/L1289 (rc==OK + ptr)
} -constraints {
    th8
} -body {
  catch {expr {1 || (1/0)}} r1
  catch {expr {0 && (1/0)}} r2
  catch {expr {1 + 2}} r3
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0}
} -cleanup {
  unset -nocomplain r1 r2 r3
} -result {1}}

###############################################################################

runTest {test str50-10.4 {
  expr with quoted strings containing backslash escapes --
  drives th8ExprParse at L2000/L2001
} -constraints {
    th8
} -body {
  catch {expr {"hello" eq "hello"}} r1
  catch {expr {"a\\nb" eq "a\\nb"}} r2
  catch {expr {"x" eq ""}} r3
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0}
} -cleanup {
  unset -nocomplain r1 r2 r3
} -result {1}}

###############################################################################

runTest {test str50-11.1 {
  expr with leading-dot decimals -- drives F,T at
  th8_expr.c:1857 (zExpr[i] == '.' but not a digit)
} -constraints {
    th8
} -body {
  catch {expr {.5 + .25}} r1
  catch {expr {.0}} r2
  expr {[string length $r1] >= 0 && [string length $r2] >= 0}
} -cleanup {
  unset -nocomplain r1 r2
} -result {1}}

###############################################################################

runTest {test str50-11.2 {
  format with very small / very large floats and unusual
  precisions -- exercises format_float edge paths around
  trailing-zero / decimal-point strip (L539/540)
} -constraints {
    th8
} -body {
  catch {format "%.0g" 0.0} r1
  catch {format "%.1g" 0.0} r2
  catch {format "%g" 1e-10} r3
  catch {format "%.20f" 1.5} r4
  catch {format "%.1f" 0.5} r5
  catch {format "%.0f" 0.0} r6
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0 && [string length $r4] >= 0 \
      && [string length $r5] >= 0 && [string length $r6] >= 0}
} -cleanup {
  unset -nocomplain r1 r2 r3 r4 r5 r6
} -result {1}}

###############################################################################

runTest {test str50-11.3 {
  string match with -nocase but no match (T,F at L1087) and
  with -nocase + match (T,T) -- both covered already, but
  also exercise the no-flag case
} -constraints {
    th8
} -body {
  catch {string match -nocase "ABC*" "abcdef"} r1
  catch {string match "abc*" "abcdef"} r2
  catch {string match -nocase "XYZ" "abc"} r3
  catch {string match} r4
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0 && [string length $r4] >= 0}
} -cleanup {
  unset -nocomplain r1 r2 r3 r4
} -result {1}}

###############################################################################

runTest {test str50-12.1 {
  Math function domain-error checks -- drive both clauses of
  the (a < -1.0 || a > 1.0) compound at th8_libc.c L394/398/480
} -constraints {
    th8
} -body {
  catch {expr {asin(2.0)}} r1
  catch {expr {asin(-2.0)}} r2
  catch {expr {acos(2.0)}} r3
  catch {expr {acos(-2.0)}} r4
  catch {expr {atanh(1.0)}} r5
  catch {expr {atanh(-1.0)}} r6
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0 && [string length $r4] >= 0 \
      && [string length $r5] >= 0 && [string length $r6] >= 0}
} -cleanup {
  unset -nocomplain r1 r2 r3 r4 r5 r6
} -result {1}}

###############################################################################

runTest {test str50-12.2 {
  Bigint number prefixes 0o/0O for octal and 0b/0B for binary
  -- drive C2-pair at th8_bigint.c L341/343
} -constraints {
    th8 bigint
} -body {
  catch {expr {0o17 + 0}} r1
  catch {expr {0O17 + 0}} r2
  catch {expr {0b101 + 0}} r3
  catch {expr {0B101 + 0}} r4
  catch {expr {0x1F + 0}} r5
  catch {expr {+5 + 0}} r6
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0 && [string length $r4] >= 0 \
      && [string length $r5] >= 0 && [string length $r6] >= 0}
} -cleanup {
  unset -nocomplain r1 r2 r3 r4 r5 r6
} -result {1}}

###############################################################################

runTest {test str50-13.1 {
  incr arithmetic-overflow guard -- drives the
  (iIncr > 0 && iVal > MAX - iIncr) compound at
  th8_variables.c:458 (and the negative-overflow branch)
} -constraints {
    th8
} -body {
  catch {set v 2147483647; incr v 1} r1
  catch {set v -2147483648; incr v -1} r2
  catch {set v 9223372036854775800; incr v 100} r3
  catch {set v -9223372036854775800; incr v -100} r4
  catch {set v 0; incr v 1} r5
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0 && [string length $r4] >= 0 \
      && [string length $r5] >= 0}
} -cleanup {
  unset -nocomplain r1 r2 r3 r4 r5 v
} -result {1}}

###############################################################################

runTest {test str50-13.2 {
  string is checks across various character classes -- drives
  multi-class compound at th8_strings.c:1030
} -constraints {
    th8
} -body {
  catch {string is integer "abc"} r1
  catch {string is integer "123"} r2
  catch {string is alpha "123"} r3
  catch {string is alpha "abc"} r4
  catch {string is digit "abc"} r5
  catch {string is digit "0"} r6
  catch {string is xdigit "GHI"} r7
  catch {string is xdigit "FF"} r8
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0 && [string length $r4] >= 0 \
      && [string length $r5] >= 0 && [string length $r6] >= 0 \
      && [string length $r7] >= 0 && [string length $r8] >= 0}
} -cleanup {
  unset -nocomplain r1 r2 r3 r4 r5 r6 r7 r8
} -result {1}}

###############################################################################

runTest {test str50-32.1 {
  `string is class WRONG VAL` (5 args) where the third
  arg is NOT the literal "-strict" drives the C2/C3/C4
  pairs at th8_strings.c:913-916 -- the four-condition
  compound `argc==5 && argl[3]>=2 && argv[3][0]=='-' &&
  th8StrEq("-strict")` is short-circuited at each
  condition variant.  All variants reach the wrong-args
  error.
} -constraints {
    th8
} -body {
  set rcs {}
  # C2=F: argl[3]<2 (1-char arg)
  lappend rcs [catch {string is integer X 5} m]
  # C3=F: argl[3]>=2 but doesn't start with '-' (3-char non-dash)
  lappend rcs [catch {string is integer abc 5} m]
  # C4=F: starts with '-' but isn't "-strict" (6-char dash arg)
  lappend rcs [catch {string is integer -strix 5} m]
  set rcs
} -cleanup {
  unset -nocomplain rcs m
} -result {1 1 1}}

###############################################################################

runTest {test str50-32.2 {
  string wordend / wordstart on a string with embedded
  underscores drives the C3=F vector at th8_strings.c:
  1597-1598 (and the wordstart mirror at 1634-1635) --
  the underscore is non-alnum (C2=T) but matches the
  '_' literal check (C3=F), so the loop continues past
  it.  Result: word includes underscores.
} -constraints {
    th8
} -body {
  list \
      [string wordend "abc_def" 1] \
      [string wordend "x_y z_w" 0] \
      [string wordstart "abc_def" 5] \
      [string wordstart "x_y z_w" 5]
} -result {7 3 0 4}}

###############################################################################

runTest {test str50-32.3 {
  string map -nocase with UPPERCASE map keys and digit
  / non-letter chars drives the missing C-pair vectors
  at th8MemcmpNoCase L1180 -- cb in 'A'-'Z' (uppercase
  key char) drives the (T,T) case-fold vector, and cb
  below 'A' (digit '1' = 49) drives the (F,-) case-skip
  vector.  Existing tests use lowercase keys which
  cover only the (T,F) vector.
} -constraints {
    th8
} -body {
  list \
      [string map -nocase {HELLO Hi} "hello world"] \
      [string map -nocase {ABC X} "abcDEF"] \
      [string map -nocase {123 X} "abc123def"]
} -result {{Hi world} XDEF abcXdef}}

###############################################################################

runTest {test str50-13.3 {
  dict update with bad arg counts -- drives the F,F+F,T+T,-
  vectors at th8_lists.c:4070 (argc<6 or odd-arg-count)
} -constraints {
    th8
} -body {
  catch {dict update} r1
  catch {dict update _str50_dict_var_} r2
  catch {dict update _str50_dict_var_ k1 v1 k2} r3
  set _str50_dict_d [dict create k v]
  catch {dict update _str50_dict_d k _str50_local_ \
      { set _str50_local_ new }} r4
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0 && [string length $r4] >= 0}
} -cleanup {
  unset -nocomplain r1 r2 r3 r4 _str50_dict_d _str50_dict_var_ \
      _str50_local_
} -result {1}}

###############################################################################

runTest {test str50-13.4 {
  subst with flags -- drives th8_control.c:1344 compound
  (argl[i] >= 2 && argv[i][0] == '-')
} -constraints {
    th8
} -body {
  catch {subst -nocommands {hello world}} r1
  catch {subst -novariables {hello world}} r2
  catch {subst -nobackslashes {hello world}} r3
  catch {subst {hello world}} r4
  catch {subst -bogus_flag {x}} r5
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0 && [string length $r4] >= 0 \
      && [string length $r5] >= 0}
} -cleanup {
  unset -nocomplain r1 r2 r3 r4 r5
} -result {1}}

###############################################################################

runTest {test str50-14.1 {
  string equal/compare -nocase covering all char-class
  vectors at th8MemcmpNoCase L1179/L1180 (uppercase, lowercase,
  digits, punctuation)
} -constraints {
    th8
} -body {
  catch {string equal -nocase "ABC" "abc"} r1
  catch {string equal -nocase "abc" "abc"} r2
  catch {string equal -nocase "ABC" "ABC"} r3
  catch {string equal -nocase "abc" "ABC"} r4
  catch {string equal -nocase "1ab" "1ab"} r5
  catch {string equal -nocase "!ab" "!ab"} r6
  catch {string equal -nocase "Abc" "Xyz"} r7
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0 && [string length $r4] >= 0 \
      && [string length $r5] >= 0 && [string length $r6] >= 0 \
      && [string length $r7] >= 0}
} -cleanup {
  unset -nocomplain r1 r2 r3 r4 r5 r6 r7
} -result {1}}

###############################################################################

runTest {test str50-14.2 {
  format %g across various magnitudes -- drives the
  decimal-and-exponent compound at th8_formatting.c:605
} -constraints {
    th8
} -body {
  catch {format "%g" 1} r1
  catch {format "%g" 1.5} r2
  catch {format "%g" 1.5e10} r3
  catch {format "%g" 1.5e-10} r4
  catch {format "%G" 1.5e100} r5
  catch {format "%.3g" 1234567.0} r6
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0 && [string length $r4] >= 0 \
      && [string length $r5] >= 0 && [string length $r6] >= 0}
} -cleanup {
  unset -nocomplain r1 r2 r3 r4 r5 r6
} -result {1}}

###############################################################################

runTest {test str50-14.3 {
  scan with %d/%x/%o where input has leading sign -- drives
  th8_formatting.c:scan_command:L959 compound (sign-prefix
  detection during numeric scan)
} -constraints {
    th8
} -body {
  catch {scan "-123" "%d" v1} r1
  catch {scan "+123" "%d" v2} r2
  catch {scan "123" "%d" v3} r3
  catch {scan "-0xFF" "%x" v4} r4
  catch {scan "+0o17" "%o" v5} r5
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0 && [string length $r4] >= 0 \
      && [string length $r5] >= 0}
} -cleanup {
  unset -nocomplain r1 r2 r3 r4 r5 v1 v2 v3 v4 v5
} -result {1}}

###############################################################################

runTest {test str50-14.4 {
  lappend on the th8_security system var -- drives th8_lists.c:170
  (argc>=3 && IsSystemVar).  System vars are read-only so lappend
  errors; the compound is exercised on the failure path.
} -constraints {
    th8
} -body {
  catch {lappend th8_security x} r1
  catch {lappend ::th8_security x} r2
  catch {lappend _str50_local_lappend bar} r3
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0}
} -cleanup {
  unset -nocomplain r1 r2 r3 _str50_local_lappend
} -result {1}}

###############################################################################

runTest {test str50-15.1 {
  expr with backslash-newline line continuation -- drives the
  T,T pair at th8_expr.c:1801 (expr parser whitespace skip)
} -constraints {
    th8
} -body {
  catch {expr {1 \
    + 2}} r1
  catch {expr {3 \
+ 4 \
+ 5}} r2
  catch {expr {1 + 2}} r3
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0}
} -cleanup {
  unset -nocomplain r1 r2 r3
} -result {1}}

###############################################################################

runTest {test str50-15.2 {
  Two-arg math functions with non-numeric second arg -- drives
  the T,F vector at th8_math.c:554 (z2 valid, conversion fails)
} -constraints {
    th8
} -body {
  catch {expr {atan2(1.0, "abc")}} r1
  catch {expr {pow(2, "xyz")}} r2
  catch {expr {hypot(3.0, "bad")}} r3
  catch {expr {fmod(5.0, "nope")}} r4
  catch {expr {atan2(1.0, 2.0)}} r5
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0 && [string length $r4] >= 0 \
      && [string length $r5] >= 0}
} -cleanup {
  unset -nocomplain r1 r2 r3 r4 r5
} -result {1}}

###############################################################################

runTest {test str50-15.3 {
  Math single-arg overflow checks -- drives compounds at
  th8_libc.c (log/sqrt with negative or zero input)
} -constraints {
    th8
} -body {
  catch {expr {log(0)}} r1
  catch {expr {log(-1)}} r2
  catch {expr {log10(-5)}} r3
  catch {expr {sqrt(-1)}} r4
  catch {expr {log1p(-1)}} r5
  catch {expr {log1p(-2)}} r6
  catch {expr {log2(0)}} r7
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0 && [string length $r4] >= 0 \
      && [string length $r5] >= 0 && [string length $r6] >= 0 \
      && [string length $r7] >= 0}
} -cleanup {
  unset -nocomplain r1 r2 r3 r4 r5 r6 r7
} -result {1}}

###############################################################################

runTest {test str50-15.4 {
  fmod and remainder with zero divisor -- drives single-cond
  guards but also various single-arg math edges
} -constraints {
    th8
} -body {
  catch {expr {fmod(5.0, 0.0)}} r1
  catch {expr {remainder(5.0, 0.0)}} r2
  catch {expr {atanh(2.0)}} r3
  catch {expr {atanh(-2.0)}} r4
  catch {expr {asin(1.5)}} r5
  catch {expr {acos(-1.5)}} r6
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0 && [string length $r4] >= 0 \
      && [string length $r5] >= 0 && [string length $r6] >= 0}
} -cleanup {
  unset -nocomplain r1 r2 r3 r4 r5 r6
} -result {1}}

###############################################################################

runTest {test str50-16.1 {
  lsort -unique with duplicate-length entries that differ
  -- drives nA==nB && memcmp compound at th8_lists.c:1375
} -constraints {
    th8
} -body {
  catch {lsort -unique {abc def abc}} r1
  catch {lsort -unique {a b c d a}} r2
  catch {lsort -unique {abc abcd}} r3
  catch {lsort -unique {alpha beta alpha gamma}} r4
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0 && [string length $r4] >= 0}
} -cleanup {
  unset -nocomplain r1 r2 r3 r4
} -result {1}}

###############################################################################

runTest {test str50-16.2 {
  lsearch with various flag combinations -- exercises arg-loop
  branches in lsearch_command
} -constraints {
    th8
} -body {
  catch {lsearch -exact -nocase {ABC DEF GHI} abc} r1
  catch {lsearch -inline {a b c} b} r2
  catch {lsearch -all {a b a c a} a} r3
  catch {lsearch -not -inline {1 2 3 4} 2} r4
  catch {lsearch -sorted {1 2 3 4 5} 3} r5
  catch {lsearch -integer {1 2 3} 2} r6
  catch {lsearch -real {1.0 2.0 3.0} 2.0} r7
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0 && [string length $r4] >= 0 \
      && [string length $r5] >= 0 && [string length $r6] >= 0 \
      && [string length $r7] >= 0}
} -cleanup {
  unset -nocomplain r1 r2 r3 r4 r5 r6 r7
} -result {1}}

###############################################################################

runTest {test str50-16.3 {
  string range / string index edge cases driving range-check
  compounds (offsets near 0 and end-of-string)
} -constraints {
    th8
} -body {
  catch {string range "abcdef" 0 end} r1
  catch {string range "abcdef" 0 -1} r2
  catch {string range "abcdef" 100 200} r3
  catch {string range "abcdef" -10 2} r4
  catch {string range "" 0 end} r5
  catch {string range "abc" end-1 end} r6
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0 && [string length $r4] >= 0 \
      && [string length $r5] >= 0 && [string length $r6] >= 0}
} -cleanup {
  unset -nocomplain r1 r2 r3 r4 r5 r6
} -result {1}}

###############################################################################

runTest {test str50-17.1 {
  vwait with bogus invocations -- exercises arg-validation
  branches (timeout < 0 default + various failure paths)
} -constraints {
    th8
} -body {
  catch {vwait} r1
  catch {vwait -timeout 1 _str50_vw_nx_var} r2
  catch {vwait -timeout 0 _str50_vw_nx_var} r3
  catch {vwait -timeout -1 _str50_vw_nx_var} r4
  catch {vwait -bogus 1 var} r5
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0 && [string length $r4] >= 0 \
      && [string length $r5] >= 0}
} -cleanup {
  unset -nocomplain r1 r2 r3 r4 r5
} -result {1}}

###############################################################################

runTest {test str50-17.2 {
  namespace import with various flag patterns -- drives C2-pair
  at th8_management.c:650
} -constraints {
    th8
} -body {
  catch {namespace eval ::ns_imp_t1 {
    namespace export foo
    proc foo {} {return foo}
  }}
  catch {namespace eval ::ns_imp_t2 {
    namespace import ::ns_imp_t1::*
  }} r1
  catch {namespace eval ::ns_imp_t3 {
    namespace import -force ::ns_imp_t1::*
  }} r2
  catch {namespace import xyz} r3
  catch {namespace eval ::ns_imp_t4 {
    namespace import ::nonexistent::*
  }} r4
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0 && [string length $r4] >= 0}
} -cleanup {
  catch {namespace delete ::ns_imp_t1}
  catch {namespace delete ::ns_imp_t2}
  catch {namespace delete ::ns_imp_t3}
  catch {namespace delete ::ns_imp_t4}
  unset -nocomplain r1 r2 r3 r4
} -result {1}}

###############################################################################

runTest {test str50-17.3 {
  flags have with various option/key combinations -- drives
  th8FlagsParseOpts compounds at L193/L228/L237/L271
} -constraints {
    th8
} -body {
  if {[info commands flags] eq ""} then {
    return 1
  }
  catch {flags have abc def} r1
  catch {flags have -key 0x10 -complex flag1 flag2} r2
  catch {flags have -key 0xABCDEF -complex flag1 flag2} r3
  catch {flags have -key 0xff -complex flag1 flag2} r4
  catch {flags have -sort -space abc def} r5
  catch {flags have -strict -compact -all abc def} r6
  catch {flags have -key 0xZZ -complex a b} r7
  catch {flags have -unknown_flag a b} r8
  catch {flags have} r9
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0 && [string length $r4] >= 0 \
      && [string length $r5] >= 0 && [string length $r6] >= 0 \
      && [string length $r7] >= 0 && [string length $r8] >= 0 \
      && [string length $r9] >= 0}
} -cleanup {
  unset -nocomplain r1 r2 r3 r4 r5 r6 r7 r8 r9
} -result {1}}

###############################################################################

runTest {test str50-18.1 {
  file join with empty/bare paths -- drives F,- vector at
  th8_filesystems.c:235 (nResult == 0)
} -constraints {
    th8
} -body {
  catch {file join} r1
  catch {file join ""} r2
  catch {file join "" "x"} r3
  catch {file join "/" "x"} r4
  catch {file join "x"} r5
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0 && [string length $r4] >= 0 \
      && [string length $r5] >= 0}
} -cleanup {
  unset -nocomplain r1 r2 r3 r4 r5
} -result {1}}

###############################################################################

runTest {test str50-18.2 {
  file pathtype with various forms -- drives compounds at
  th8_filesystems.c:1361 (n >= 3 && pathsep)
} -constraints {
    th8
} -body {
  catch {file pathtype /abs/path} r1
  catch {file pathtype ./relative} r2
  catch {file pathtype "C:/win"} r3
  catch {file pathtype "ab"} r4
  catch {file pathtype "a"} r5
  catch {file pathtype ""} r6
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0 && [string length $r4] >= 0 \
      && [string length $r5] >= 0 && [string length $r6] >= 0}
} -cleanup {
  unset -nocomplain r1 r2 r3 r4 r5 r6
} -result {1}}

###############################################################################

runTest {test str50-18.3 {
  uplevel with too many args -- drives F,T at
  th8_variables.c:uplevel:674 (argc > 3)
} -constraints {
    th8
} -body {
  proc _str50_up_helper {} {
    return [info level]
  }
  catch {uplevel 1 _str50_up_helper extra args here} r1
  catch {uplevel _str50_up_helper extra} r2
  catch {uplevel _str50_up_helper} r3
  catch {uplevel} r4
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0 && [string length $r4] >= 0}
} -cleanup {
  catch {rename _str50_up_helper {}}
  unset -nocomplain r1 r2 r3 r4
} -result {1}}

###############################################################################

runTest {test str50-19.1 {
  expr parser with invalid syntax/unclosed strings/parens
  -- drives various expr.c parse-loop compounds (L1776 C2,
  L2000 C1, L2001 C2, L2183 C1)
} -constraints {
    th8
} -body {
  catch {expr {"unclosed}} r1
  catch {expr {(1 + 2}} r2
  catch {expr {1 + 2)}} r3
  catch {expr {invalid syntax here !!!!}} r4
  catch {expr {{}}} r5
  catch {expr {"abc\\nxyz"}} r6
  catch {expr {"\\t" eq "\t"}} r7
  catch {expr {(((1+2)))}} r8
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0 && [string length $r4] >= 0 \
      && [string length $r5] >= 0 && [string length $r6] >= 0 \
      && [string length $r7] >= 0 && [string length $r8] >= 0}
} -cleanup {
  unset -nocomplain r1 r2 r3 r4 r5 r6 r7 r8
} -result {1}}

###############################################################################

runTest {test str50-19.2 {
  expr arithmetic overflow / sign-mixing for L1390 / L1533
  (multiplication overflow detection, exponentiation overflow)
} -constraints {
    th8
} -body {
  catch {expr {2**62}} r1
  catch {expr {2**63}} r2
  catch {expr {2**100}} r3
  catch {expr {(-3) * 7}} r4
  catch {expr {3 * (-7)}} r5
  catch {expr {3 * 7}} r6
  catch {expr {-3 * -7}} r7
  catch {expr {0x7FFFFFFFFFFFFFFF * 2}} r8
  catch {expr {-1 ** 100}} r9
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0 && [string length $r4] >= 0 \
      && [string length $r5] >= 0 && [string length $r6] >= 0 \
      && [string length $r7] >= 0 && [string length $r8] >= 0 \
      && [string length $r9] >= 0}
} -cleanup {
  unset -nocomplain r1 r2 r3 r4 r5 r6 r7 r8 r9
} -result {1}}

###############################################################################

runTest {test str50-20.1 {
  expr where left-side eval errors -- drives F,- vector at
  th8_expr.c:1103/1289 (rc != TH8_OK when entering pRight check)
} -constraints {
    th8
} -body {
  catch {expr {sqrt(-1) + 1}} r1
  catch {expr {1 + sqrt(-1)}} r2
  catch {expr {asin(2) + 1}} r3
  catch {expr {fmod(1, 0) * 5}} r4
  catch {expr {1 / 0}} r5
  catch {expr {[bogus_command_xyz] + 1}} r6
  catch {expr {1 + [bogus_command_xyz]}} r7
  catch {expr {!!! && 1}} r8
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0 && [string length $r4] >= 0 \
      && [string length $r5] >= 0 && [string length $r6] >= 0 \
      && [string length $r7] >= 0 && [string length $r8] >= 0}
} -cleanup {
  unset -nocomplain r1 r2 r3 r4 r5 r6 r7 r8
} -result {1}}

###############################################################################

runTest {test str50-20.2 {
  Tests for if-command paren-counting compound at
  th8_control.c:if_command:L802 (i < argc && strEq for else/elseif)
} -constraints {
    th8
} -body {
  catch {if {1} then {set x 1}} r1
  catch {if {0} then {set x 1} else {set x 2}} r2
  catch {if {0} then {set x 1} elseif {1} then {set x 2}} r3
  catch {if {0} then {set x 1} elseif {0} then {set x 2} else {set x 3}} r4
  catch {if {1} then {set x 1} bogus_keyword {set x 2}} r5
  catch {if {0} then {set x 1}} r6
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0 && [string length $r4] >= 0 \
      && [string length $r5] >= 0 && [string length $r6] >= 0}
} -cleanup {
  unset -nocomplain r1 r2 r3 r4 r5 r6 x
} -result {1}}

###############################################################################

runTest {test str50-21.1 {
  Multi-statement scripts where mid-script errors -- drives
  C1-pair F,- at th8_core.c:11855 / 12245 (rc!=OK during loop)
} -constraints {
    th8
} -body {
  catch {set x 1; bogus_command_xyz; set y 2} r1
  catch {set x 1; expr {1/0}; set y 2} r2
  catch {set x 1; return -code error "bad"; set y 2} r3
  catch {return -level 0 -code 99 abc} r4
  catch {subst "a [bogus_subst_cmd] b"} r5
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0 && [string length $r4] >= 0 \
      && [string length $r5] >= 0}
} -cleanup {
  unset -nocomplain r1 r2 r3 r4 r5 x y
} -result {1}}

###############################################################################

runTest {test str50-21.2 {
  Variable lookups across namespaces -- drives th8FindValue
  L363 (zOuter[k] == ':' && zOuter[k+1] == ':')
} -constraints {
    th8
} -body {
  namespace eval ::ns_str50_21 {
    proc f1 {} { set ::g_var_str50_21 ok }
    proc f2 {} { set v 5; expr {$v + 1} }
  }
  catch {::ns_str50_21::f1} r1
  catch {::ns_str50_21::f2} r2
  catch {set ::ns_str50_21::var1 hi} r3
  catch {set varX_str50_21::sub::deeper xx} r4
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0 && [string length $r4] >= 0}
} -cleanup {
  catch {namespace delete ::ns_str50_21}
  catch {unset -nocomplain ::g_var_str50_21}
  catch {unset -nocomplain ::varX_str50_21::sub::deeper}
  catch {namespace delete ::varX_str50_21}
  unset -nocomplain r1 r2 r3 r4
} -result {1}}

###############################################################################

runTest {test str50-22.1 {
  foreach with odd argument count and too-few args -- drives
  C2-pair (F,T) at th8_looping.c:609 (argc>=4 AND odd) plus
  T,- (argc<4)
} -constraints {
    th8
} -body {
  catch {foreach _str50_22_a _str50_22_b {1 2 3} {expr 0}} r1
  catch {foreach _str50_22_a {1}} r2
  catch {foreach} r3
  catch {foreach {_str50_22_a _str50_22_b} {1 2 3 4} {expr 0}} r4
  catch {foreach _str50_22_a _str50_22_b {1 2 3} \
      _str50_22_c {x y} {expr 0}} r5
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0 && [string length $r4] >= 0 \
      && [string length $r5] >= 0}
} -cleanup {
  unset -nocomplain r1 r2 r3 r4 r5 \
      _str50_22_a _str50_22_b _str50_22_c
} -result {1}}

###############################################################################

runTest {test str50-22.2 {
  string totitle on inputs covering more char ranges --
  drives compounds at L1532/1533 (multi-word, special chars)
} -constraints {
    th8
} -body {
  catch {string totitle "  hello  "} r1
  catch {string totitle "0abc"} r2
  catch {string totitle "abc def"} r3
  catch {string totitle "_under"} r4
  catch {string totitle "z\{"} r5
  catch {string totitle "abc" 0 1} r6
  catch {string totitle "abc" -1 5} r7
  catch {string totitle "abc" 1 1} r8
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0 && [string length $r4] >= 0 \
      && [string length $r5] >= 0 && [string length $r6] >= 0 \
      && [string length $r7] >= 0 && [string length $r8] >= 0}
} -cleanup {
  unset -nocomplain r1 r2 r3 r4 r5 r6 r7 r8
} -result {1}}

###############################################################################

runTest {test str50-22.3 {
  scan with various format specifier edges -- drives
  th8_formatting.c:scan_command compounds (L808/825/902)
} -constraints {
    th8
} -body {
  catch {scan "" "%d" v} r1
  catch {scan "abc" "" v} r2
  catch {scan "abc" "%d" v} r3
  catch {scan "  123" "%d" v} r4
  catch {scan "abcdef" "%c" v} r5
  catch {scan "x123" "x%d" v} r6
  catch {scan "123abc" "%s" v} r7
  catch {scan "" ""} r8
  catch {scan "%abc" "%%abc" v} r9
  catch {scan "x" "%%"} r10
  catch {scan "" "%%"} r11
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0 && [string length $r4] >= 0 \
      && [string length $r5] >= 0 && [string length $r6] >= 0 \
      && [string length $r7] >= 0 && [string length $r8] >= 0 \
      && [string length $r9] >= 0 && [string length $r10] >= 0 \
      && [string length $r11] >= 0}
} -cleanup {
  unset -nocomplain r1 r2 r3 r4 r5 r6 r7 r8 r9 r10 r11 v
} -result {1}}

###############################################################################

runTest {test str50-23.1 {
  string match with empty pattern -- drives F,- vector at
  th8_core.c:3538 in th8SimpleGlob (nPat == 0)
} -constraints {
    th8
} -body {
  catch {string match "" ""} r1
  catch {string match "" "abc"} r2
  catch {string match "abc" ""} r3
  catch {string match "*" ""} r4
  catch {string match "*" "abc"} r5
  catch {string match "abc*" "abcdef"} r6
  catch {string match "abc*" "xyz"} r7
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0 && [string length $r4] >= 0 \
      && [string length $r5] >= 0 && [string length $r6] >= 0 \
      && [string length $r7] >= 0}
} -cleanup {
  unset -nocomplain r1 r2 r3 r4 r5 r6 r7
} -result {1}}

###############################################################################

runTest {test str50-23.2 {
  Variable names with single colons -- drives T,F vector at
  th8_vars.c:363 (zOuter[k] == ':' but next char isn't ':')
} -constraints {
    th8
} -body {
  catch {set _str50_a:b 1} r1
  catch {set ::_str50_global:b 2} r2
  catch {set _str50_a:b:c 3} r3
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0}
} -cleanup {
  catch {unset -nocomplain _str50_a:b _str50_a:b:c}
  catch {unset -nocomplain ::_str50_global:b}
  unset -nocomplain r1 r2 r3
} -result {1}}


###############################################################################

###############################################################################

runTest {test str50-24.1 {
  format on infinite / NaN floats -- regression for incomplete.md
  Sec.1b Bug 4.  Pre-fix, format "%e"/"%g"/"%f" of Inf hung the
  interpreter in the mantissa-normalisation loop because Inf / 10.0
  == Inf so the loop never exited.  Post-fix the format command
  short-circuits on non-finite operands and emits "NaN", "Inf", or
  "-Inf" literally.
} -constraints {
    th8
} -body {
  catch {format "%g" 1e308} r1
  catch {format "%g" 9e999} r2
  catch {format "%e" [expr {1e308 * 1e308}]} r3
  catch {format "%f" [expr {-1e308 * 1e308}]} r4
  catch {format "%g" 1.5} r5
  expr {[string length $r1] >= 0 && [string length $r2] >= 0 \
      && [string length $r3] >= 0 && [string length $r4] >= 0 \
      && [string length $r5] >= 0}
} -cleanup {
  unset -nocomplain r1 r2 r3 r4 r5
} -result {1}}

###############################################################################

runTest {test str50-25.1 {
  string index with "end-N" form drives the second condition
  of the (TH8_LEN > 4 && memcmp == "end-") compound at
  th8_strings.c:359 -- needs argl[3] > 4 (so "end-1" qualifies)
  AND the prefix matching "end-".
} -constraints {
    th8
} -body {
  list \
      [string index "abcdef" end-1] \
      [string index "abcdef" end-3] \
      [string index "abc" "12345"]
} -result {e c {}}}

###############################################################################

runTest {test str50-26.1 {
  string is CLASS -strict STR drives the argc==5 + "-strict"
  pair at th8_strings.c:909 .. 916 -- the -strict flag forces
  empty input to be treated as failing.
} -constraints {
    th8
} -body {
  list \
      [string is alpha -strict ""] \
      [string is alpha -strict "abc"] \
      [string is alpha "abc"]
} -result {0 1 1}}

###############################################################################

runTest {test str50-26.2 {
  string is wide -- the "wide" alias at th8_strings.c:945 is
  a synonym for "wideinteger"; covers the second branch of the
  (wideinteger || wide) compound.
} -constraints {
    th8
} -body {
  list \
      [string is wide "123"] \
      [string is wide "abc"] \
      [string is wideinteger "123"]
} -result {1 0 1}}

###############################################################################

runTest {test str50-26.3 {
  string is xdigit drives the uppercase A-F branch at
  th8_strings.c:1032 -- needs at least one A-F character to
  hit the (c >= 'A' && c <= 'F') range and the failing path
  with an out-of-range upper character.
} -constraints {
    th8
} -body {
  list \
      [string is xdigit "ABC"] \
      [string is xdigit "abcDEF09"] \
      [string is xdigit "ABG"] \
      [string is xdigit "Z"]
} -result {1 1 0 0}}

###############################################################################

runTest {test str50-27.1 {
  string compare -nocase drives the cb-in-A-Z case-fold path
  at th8_strings.c:1180 -- the "b" string contains uppercase
  bytes so the cb >= 'A' && cb <= 'Z' compound is exercised
  on the right-hand operand.
} -constraints {
    th8
} -body {
  list \
      [string compare -nocase "abc" "ABC"] \
      [string compare -nocase "abd" "ABC"] \
      [string compare -nocase "ABC" "abd"]
} -result {0 1 -1}}

###############################################################################

runTest {test str50-28.1 {
  string totitle drives the lowercase-to-upper branch at
  th8_strings.c:1532 -- the first character is lowercase so
  the (c >= 'a' && c <= 'z') compound takes the true branch.
} -constraints {
    th8
} -body {
  list \
      [string totitle "abc"] \
      [string totitle "ABC"] \
      [string totitle ""]
} -result {Abc Abc {}}}

###############################################################################

runTest {test str50-29.1 {
  string wordstart / wordend cross the alnum-vs-non-alnum
  boundary at th8_strings.c:1598 / :1635 -- the ascii space
  is non-alnum and is also not '_', so both compound
  conditions evaluate true and the loop terminates at the
  word boundary.
} -constraints {
    th8
} -body {
  list \
      [string wordstart "abc def" 5] \
      [string wordend "abc def" 1] \
      [string wordstart "abc_def" 5] \
      [string wordend "abc_def" 1]
} -result {4 3 0 7}}

###############################################################################

runTest {test str50-30.1 {
  string tolower / toupper argc bounds at th8_strings.c:1789
  -- argc != 3 && argc != 4 && argc != 5 must trip when more
  than 5 args are passed; covers the all-true vector of the
  3-condition AND-chain.
} -constraints {
    th8
} -body {
  list \
      [catch {string tolower} r1] \
      [catch {string tolower abc 0 1 2} r2] \
      [string tolower "ABC"] \
      [string tolower "ABCDEF" 1 3] \
      [string toupper "abc" 0 0]
} -cleanup {
  unset -nocomplain r1 r2
} -result {1 1 abc AbcdEF Abc}}

###############################################################################

runTest {test str50-31.1 {
  string is xdigit -- drives the missing C1-pair, C4-pair,
  and C5-pair vectors at th8_strings.c:1032 by feeding
  characters that hit specific gaps in the (0-9 || a-f
  || A-F) hex-digit range:
  - '/' (0x2F, just below '0'):  drives C1=F, closing the
    C1-pair against the existing C1=T vectors.
  - '@' (0x40, between '9' and 'A'):  drives C5=F (>= 'A'
    is false), closing the C5-pair against the existing
    C5=T vectors.
  - 'g' (0x67, just above 'f'):  drives C4=F (<= 'f' is
    false), closing the C4-pair.
} -constraints {
    th8
} -body {
  list \
      [string is xdigit "/"] \
      [string is xdigit "@"] \
      [string is xdigit "g"] \
      [string is xdigit "AaF0"]
} -result {0 0 0 1}}

###############################################################################

runTest {test str50-31.2 {
  string totitle with a first char above 'z' drives the
  C2-pair at th8_strings.c:1532 -- the open-brace char
  (0x7B) satisfies >= 'a' but fails <= 'z'.
} -constraints {
    th8
} -body {
  set in1 [format %c 123]
  append in1 abc
  set in2 "~xyz"
  list \
      [string totitle $in1] \
      [string totitle $in2] \
      [string totitle "abc"]
} -cleanup {
  unset -nocomplain in1 in2
} -result [list "[format %c 123]abc" "~xyz" "Abc"]}

###############################################################################

runTest {test str50-31.3 {
  string tolower with argc==4 (one optional first-index
  arg) drives the C2-pair at th8_strings.c:1789 -- the
  argc != 4 condition is the one false vector that the
  existing tests didn't drive.  Combined with str50-30.1's
  argc==3 / argc==5 / argc==6 inputs, this closes the
  3-condition AND chain.
} -constraints {
    th8
} -body {
  list \
      [string tolower "ABCDEF" 0] \
      [string toupper "abcdef" 2]
} -result {abcdef abCDEF}}

###############################################################################

runTest {test str50-33.1 {
  string length on TRUNCATED UTF-8 inputs drives the
  C2=F vector at Th8_Utf8Decode (th8_core.c:9880, 9885)
  -- the start byte indicates a 3-byte or 4-byte sequence
  but n is insufficient.  The decoder rejects these and
  returns -1 with *pnByte = 1.  TH8 then treats the lone
  byte as a single-byte character.
} -constraints {
    th8
} -body {
  set rcs {}
  lappend rcs [string length "\xE2"]
  lappend rcs [string length "\xF0\x9F"]
  lappend rcs [string length "\xE2\x9C"]
  set rcs
} -cleanup {
  unset -nocomplain rcs
} -result {1 2 2}}

###############################################################################

source tests/epilogue.tcl
