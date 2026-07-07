###############################################################################
#
# coverage_path_dotcomp.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/th8_posix.c th8PosixResolveAbsolute
# L4540-4541:
#
#   else if (len == 2 && start[0] == '.' && start[1] == '.')
#
# The C3-Pair (start[1] != '.', i.e., dotfile name like ".a")
# was unreached because existing tests only exercised "." and
# ".." special components.  A path with a 2-char component
# starting with '.' but not ".." (e.g., "/dir/.a/file")
# drives (T,T,F)=F: len==2 (T), first char '.' (T), second
# char NOT '.' (F).  The component is treated as a normal
# directory name rather than parent-dir, so depth increments
# and the path is preserved.
#
# Driver: `file normalize` of a nonexistent path containing
# a `.x` 2-char component.  Nonexistent forces realpath to
# fail at L4440 and the manual normalization loop at L4527+
# to execute.
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

runTest {test pathdotcomp-1.1 {
  `file normalize` on a nonexistent path with a 2-char
  component starting with '.' (".a") drives
  th8PosixResolveAbsolute L4540 (T,T,F): len==2, first
  char '.', second char not '.'.  The component is kept
  as a normal directory name in the normalized output.
} -constraints {
    th8
} -setup {
} -body {
  set r [file normalize \
      /tmp/nonexistent_th8_pathdotcomp/.a/file.txt]
  expr {[string match "*/.a/file.txt" $r]}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test pathdotcomp-1.2 {
  Multiple 2-char dot-prefix components (".b", ".c") on a
  nonexistent path -- exercises L4540 (T,T,F) repeatedly
  in the same call.
} -constraints {
    th8
} -setup {
} -body {
  set r [file normalize \
      /tmp/nonexistent_th8_pathdotcomp/.b/.c/x.y]
  expr {[string match "*/.b/.c/x.y" $r]}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

source tests/epilogue.tcl
