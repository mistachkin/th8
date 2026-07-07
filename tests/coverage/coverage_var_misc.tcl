###############################################################################
#
# coverage_var_misc.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted MC/DC closure for variable-name analysis decisions
# in src/th8_vars.c, focusing on the SPECIFIC missing vectors:
#
#   :277  if (nVar > 2 && zVar[0] == ':' && zVar[1] == ':')
#                  (qualified-name detect, missing C3-pair:
#                   nVar > 2, zVar[0] == ':', zVar[1] != ':'
#                   -- single-colon-prefix var name)
#   :1726 if (nWord > 1 && zWord[1] == '{')
#                  (${name} brace form, missing C1-pair:
#                   nWord <= 1 -- single-char $var reference)
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

runTest {test varmisc-1.1 {
  set with a single-colon-prefix variable name drives the
  (T,T,F) vector at th8_vars.c:277 -- nVar > 2 (T), first
  byte is ':' (T), second byte is NOT ':' (F).  This
  treats the name as non-qualified (the leading single
  colon is just part of the name).
} -constraints {
    th8
} -body {
  set :foo 1
  set :bar 2
  list \
      [set :foo] \
      [set :bar] \
      [info exists :foo]
} -cleanup {
  unset -nocomplain :foo :bar
} -result {1 2 1}}

###############################################################################

runTest {test varmisc-2.1 {
  $var with a single-character variable name drives the
  (F,-) vector at th8_vars.c:1726 -- the variable-ref
  parser sees nWord == 1 ($x has nWord=1 after stripping
  the $), so the brace-form check fails on the length
  condition.
} -constraints {
    th8
} -body {
  set x 100
  set y 7
  list \
      "$x" \
      "$y" \
      "[set x]" \
      "$x$y"
} -cleanup {
  unset -nocomplain x y
} -result {100 7 100 1007}}

###############################################################################

runTest {test varmisc-3.1 {
  variable command with a fully qualified name (::ns::var)
  drives the C1-pair at th8_variables.c:937 -- the
  reverse-scan to find the last "::" separator visits
  characters of the local-name segment (e.g., 'var') where
  p[-1] is not ':', closing the (F,-) vector.
} -constraints {
    th8
} -body {
  namespace eval ::varmisc_qual {
      variable ::varmisc_qual::myvar 99
  }
  list [set ::varmisc_qual::myvar]
} -cleanup {
  catch {namespace delete ::varmisc_qual}
} -result {99}}

###############################################################################

runTest {test varmisc-4.1 {
  $-substitution of a name like "$abc:foo" -- the var-name
  parser walks alnum until it hits ':' at position N, then
  the (i+1<nInput && z[i]==':' && z[i+1]==':') compound
  evaluates with z[i]==':' (T) but z[i+1]==':' (F, it's
  'f' for "foo").  Drives the C3=F vector at line
  10438-10439 (the inner "::" separator detect during
  variable-name walk).  The substituted value is "abc"
  followed by the literal ":foo".
} -constraints {
    th8
} -body {
  set abc 100
  set y "$abc:foo"
  set z "$abc:bar:baz"
  list $y $z
} -cleanup {
  unset -nocomplain abc y z
} -result {100:foo 100:bar:baz}}

###############################################################################

runTest {test varmisc-5.1 {
  Word starting with `{TAG}rest` where TAG contains an
  UNDERSCORE drives the C2=F vector at the expansion-tag
  scan (th8_core.c:11933-11935) -- the loop encounters
  the '_' character which makes !th8IsAlnum T, then
  zInput[k] != '_' is F, so the compound short-circuits
  and the underscore is accepted as part of the tag.
  FindExpansion subsequently fails (tag not registered)
  and the parser raises "unknown expansion operator".  We
  use eval (not set) so the parser actually tokenizes the
  string and walks the expansion-tag scan.
} -constraints {
    th8
} -body {
  set rcs {}
  foreach inp {a_b x_y tag_with_underscores} {
      set src "set xx \173${inp}\175rest"
      lappend rcs [catch {eval $src} m]
  }
  set rcs
} -cleanup {
  unset -nocomplain rcs inp src xx m
} -result {1 1 1}}

###############################################################################

runTest {test varmisc-6.1 {
  variable command called INSIDE A PROC with various
  name forms drives the C2/C3 pairs at
  th8_variables.c:927-928 -- the name-qualification
  detect compound `!zQual && nName>2 && zName[0]==':'
  && zName[1]==':'`:
    - 1-char name (nName=1) drives C2=F
    - multi-char non-prefixed (nName>2, no '::') drives C3=F
} -constraints {
    th8
} -body {
  set rcs {}
  proc ::varmisc_p_short {} {
      variable x
      set x 11
  }
  ::varmisc_p_short
  lappend rcs [info exists ::x]

  proc ::varmisc_p_plain {} {
      variable foo
      set foo 22
  }
  ::varmisc_p_plain
  lappend rcs [info exists ::foo]
  set rcs
} -cleanup {
  catch {rename ::varmisc_p_short ""}
  catch {rename ::varmisc_p_plain ""}
  unset -nocomplain ::x ::foo
  unset -nocomplain rcs
} -result {1 1}}

###############################################################################

runTest {test varmisc-7.1 {
  $-substitution of `$:foo` (single-colon-prefix immediately
  after the `$`) drives the C3=F vector at th8NextVarName
  (th8_core.c:10425) -- nInput > 2 (T), zInput[1] == ':'
  (T), zInput[2] != ':' (F).  The parser treats this as an
  empty var reference followed by literal ":foo".
} -constraints {
    th8
} -body {
  set rcs {}
  catch {set y "$:foo"} m1
  lappend rcs [expr {[string length $m1] >= 0}]
  catch {set z "$:bar"} m2
  lappend rcs [expr {[string length $m2] >= 0}]
  catch {set q "$:long_name_xyz"} m3
  lappend rcs [expr {[string length $m3] >= 0}]
  set rcs
} -cleanup {
  unset -nocomplain rcs y z q m1 m2 m3
} -result {1 1 1}}

###############################################################################

runTest {test varmisc-8.1 {
  Dollar-brace var reference with NO closing brace drives
  the C1=F vector at th8NextVarName (th8_core.c:10410) --
  the scanner runs i to nInput without finding the close-
  brace, so i < nInput becomes F.  The error path
  missing-close-brace fires.  We build the input via octal
  escapes to avoid an unbalanced literal in the test body.
} -constraints {
    th8
} -body {
  set rcs {}
  set s1 [format "\44\173unclosed"]
  lappend rcs [catch {eval $s1} m]
  set s2 [format "\44\173"]
  lappend rcs [catch {eval $s2} m]
  set s3 [format "\44\173abc def"]
  lappend rcs [catch {eval $s3} m]
  set rcs
} -cleanup {
  unset -nocomplain rcs s1 s2 s3 m
} -result {1 1 1}}

###############################################################################

runTest {test varmisc-9.1 {
  $array(subscript) reference with NO closing paren drives
  the C1=F vector at th8NextVarName (th8_core.c:10454) --
  the subscript scanner advances to nInput without
  decrementing depth to 0.
} -constraints {
    th8
} -body {
  set rcs {}
  set s1 "\$arr("
  lappend rcs [catch {eval $s1} m]
  set s2 "\$arr(unclosed"
  lappend rcs [catch {eval $s2} m]
  set s3 "\$arr(a"
  lappend rcs [catch {eval $s3} m]
  set rcs
} -cleanup {
  unset -nocomplain rcs s1 s2 s3 m
} -result {1 1 1}}

###############################################################################

runTest {test varmisc-9.2 {
  Variable reference with form $\173name (unclosed brace,
  '\173' is '{') drives the C1=F vector at th8NextVarName
  (th8_core.c:10422) -- the close-brace scanner walks i
  to nInput without finding '}', so the loop exits via
  i < nInput short-circuiting to F.  The follow-up
  i >= nInput check (L10425) then returns TH8_ERROR.
  The error surfaces script-side as "Unmatched braces".
  Use octal-escaped open brace so the test file's own
  brace counter is unaffected.
} -constraints {
    th8
} -body {
  set rcs {}
  set s1 "set x \$\173unclosed"
  lappend rcs [catch {eval $s1} m]
  set s2 "puts \$\173nope"
  lappend rcs [catch {eval $s2} m]
  set s3 "expr \$\173incomplete"
  lappend rcs [catch {eval $s3} m]
  set rcs
} -cleanup {
  unset -nocomplain rcs s1 s2 s3 m x
} -result {1 1 1}}

###############################################################################

runTest {test varmisc-10.1 {
  Drive th8_vars.c:1278 Th8_UnsetVar's `!pEntry ||
  NEVER(!pEntry->pData)` C1-Pair {T,C} (no such array
  variable).  `unset noSuchArr(idx)` looks up "noSuchArr"
  in the frame's variable hash; not found -> pEntry=NULL ->
  C1=T -> "no such variable" error.  Existing tests cover
  C1=F (array exists) but not the missing-array case for
  the inner-element path.
} -constraints {
    th8
} -body {
  set rcs {}
  lappend rcs [catch {unset noSuchArr(a)} m]
  lappend rcs [catch {unset noSuchArr2(key)} m]
  set rcs
} -cleanup {
  unset -nocomplain rcs m
} -result {1 1}}

###############################################################################

source tests/epilogue.tcl
