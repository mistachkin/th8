###############################################################################
#
# bug_uplevel_kv.tcl --
#
# Minimal reproducer for kv set2 + uplevel corruption.
#
# Conditions required (ALL must be present):
#   1. proc with uplevel 1  (uplevel 0 or direct eval: OK)
#   2. TH8_KV_SET2 via C callback matching 2+ keys  (1 key: OK)
#   3. Two "set VAR [kv get ...]" assignments after  (one: OK)
#
# Additional findings:
#   - catch is NOT required (uplevel 1 alone triggers it)
#   - Individual kv set per key (no set2) works fine
#   - Same logic at script level (kv list + foreach + kv set) works fine
#   - The bug is in the C-level SET2 path inside the xKeyValue callback
#   - ASan does not detect a heap error; second get returns {} silently
#   - Without ASan, triggers oversize string panic or exit code 3
#   - Env KV backend cannot be tested (separate kv get bug)
#
# Symptom: exit code 3 (crash/abort), or second get returns empty.
#
# Run:
#   TH8SH_NO_SCRIPT_SECURITY=1 TH8SH_YES_TESTLIB=1 \
#       ./bin/th8sh tests/helpers/bug_uplevel_kv.tcl
#
###############################################################################

source tests/prologue.tcl

proc wrapper {body} { uplevel 1 $body }

puts [wrapper {
    ::th8testlib::kv set _a v1
    ::th8testlib::kv set _b v2
    ::th8testlib::kv set2 _* NEW
    set x [::th8testlib::kv get _a]
    set y [::th8testlib::kv get _b]
    list $x $y
}]

# Expected: NEW NEW
# Actual:   crash (exit 3), or "NEW {}" under ASan
