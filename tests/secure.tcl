###############################################################################
#
# secure.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Tests for the TH8 secure variable subsystem (encrypted-at-rest
# variables with AES-256-GCM and mlock'd key storage).
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

source tests/prologue.tcl

###############################################################################
#
# Section 1 -- secure create: basic creation and reading
#
###############################################################################

runTest {test secure-1.1 {
  R-47030-53896: secure create with a value stores and retrieves it
} -constraints {
    th8 crypto_testlib
} -setup {
} -body {
  secure create _s1_1 "hello"
  set result $_s1_1
  secure delete _s1_1
  set result
} -cleanup {
  unset -nocomplain result
} -result {hello}}

###############################################################################

runTest {test secure-1.2 {
  R-47030-53896: secure create without a value creates an empty variable
} -constraints {
    th8 crypto_testlib
} -setup {
} -body {
  secure create _s1_2
  set result [string length $_s1_2]
  secure delete _s1_2
  set result
} -cleanup {
  unset -nocomplain result
} -result {0}}

###############################################################################

runTest {test secure-1.3 {
  R-14798-31664: secure exists returns 1 for a secure variable
} -constraints {
    th8 crypto_testlib
} -setup {
} -body {
  secure create _s1_3 "test"
  set result [secure exists _s1_3]
  secure delete _s1_3
  set result
} -cleanup {
  unset -nocomplain result
} -result {1}}

###############################################################################

runTest {test secure-1.4 {
  R-14798-31664: secure exists returns 0 for a non-secure variable
} -constraints {
    th8 crypto_testlib
} -body {
  set _s1_4 "plain"
  set result [secure exists _s1_4]
  set result
} -cleanup {
  unset -nocomplain result _s1_4
} -result {0}}

###############################################################################

runTest {test secure-1.5 {
  R-14798-31664: secure exists returns 0 for a nonexistent variable
} -constraints {
    th8 crypto_testlib
} -body {
  secure exists _s1_5_nonexistent
} -result {0}}

###############################################################################
#
# Section 2 -- secure create: error cases
#
###############################################################################

runTest {test secure-2.1 {
  R-35397-13579: secure create on an existing plain variable is an error
} -constraints {
    th8 crypto_testlib
} -setup {
} -body {
  set _s2_1 "plain"
  list [catch {secure create _s2_1 "val"} msg] $msg
} -cleanup {
  unset -nocomplain _s2_1
  unset -nocomplain msg
} -result {1 {cannot modify existing variable "_s2_1"}}}

###############################################################################

runTest {test secure-2.2 {
  R-35397-13579: secure create on an existing secure variable is an error
} -constraints {
    th8 crypto_testlib
} -setup {
} -body {
  secure create _s2_2 "first"
  set rc [catch {secure create _s2_2 "second"} msg]
  secure delete _s2_2
  list $rc $msg
} -cleanup {
  unset -nocomplain msg
  unset -nocomplain rc
} -result {1 {cannot modify existing variable "_s2_2"}}}

###############################################################################

runTest {test secure-2.3 {
  R-35314-37131: secure create rejects array element syntax
} -constraints {
    th8 crypto_testlib
} -setup {
} -body {
  catch {secure create _s2_3(key) "val"} msg
  set msg
} -cleanup {
  unset -nocomplain msg
} -result {secure: array elements not supported}}

###############################################################################
#
# Section 3 -- set/append/incr on secure variables
#
###############################################################################

runTest {test secure-3.1 {
  R-41789-50474: set on a secure variable updates the encrypted value
} -constraints {
    th8 crypto_testlib
} -setup {
} -body {
  secure create _s3_1 "original"
  set _s3_1 "modified"
  set result $_s3_1
  secure delete _s3_1
  set result
} -cleanup {
  unset -nocomplain result
} -result {modified}}

###############################################################################

runTest {test secure-3.2 {
  R-41789-50474: append on a secure variable works correctly
} -constraints {
    th8 crypto_testlib
} -setup {
} -body {
  secure create _s3_2 "hello"
  append _s3_2 " world"
  set result $_s3_2
  secure delete _s3_2
  set result
} -cleanup {
  unset -nocomplain result
} -result {hello world}}

###############################################################################

runTest {test secure-3.3 {
  R-41789-50474: incr on a secure variable works correctly
} -constraints {
    th8 crypto_testlib
} -setup {
} -body {
  secure create _s3_3 "10"
  incr _s3_3
  incr _s3_3 5
  set result $_s3_3
  secure delete _s3_3
  set result
} -cleanup {
  unset -nocomplain result
} -result {16}}

###############################################################################

runTest {test secure-3.4 {
  R-18042-11365: multiple reads return the same value
} -constraints {
    th8 crypto_testlib
} -setup {
} -body {
  secure create _s3_4 "stable"
  set a $_s3_4
  set b $_s3_4
  set c $_s3_4
  secure delete _s3_4
  expr {$a eq $b && $b eq $c}
} -cleanup {
  unset -nocomplain a
  unset -nocomplain b
  unset -nocomplain c
} -result {1}}

###############################################################################
#
# Section 4 -- secure delete
#
###############################################################################

runTest {test secure-4.1 {
  R-51801-42694: secure delete removes the variable
} -constraints {
    th8 crypto_testlib
} -body {
  secure create _s4_1 "temp"
  secure delete _s4_1
  info exists _s4_1
} -result {0}}

###############################################################################

runTest {test secure-4.2 {
  R-39649-05760: secure delete on a non-secure variable is an error
} -constraints {
    th8 crypto_testlib
} -body {
  set _s4_2 "plain"
  set rc [catch {secure delete _s4_2} msg]
  list $rc [string match {variable is not secure*} $msg]
} -cleanup {
  unset -nocomplain msg rc _s4_2
} -result {1 1}}

###############################################################################
#
# Section 5 -- expression and substitution contexts
#
###############################################################################

runTest {test secure-5.1 {
  R-30156-17445: secure variable in expression context
} -constraints {
    th8 crypto_testlib
} -setup {
} -body {
  secure create _s5_1 "42"
  set result [expr {$_s5_1 * 2}]
  secure delete _s5_1
  set result
} -cleanup {
  unset -nocomplain result
} -result {84}}

###############################################################################

runTest {test secure-5.2 {
  R-30156-17445: secure variable in command substitution
} -constraints {
    th8 crypto_testlib
} -setup {
} -body {
  secure create _s5_2 "abcdef"
  set result [string length $_s5_2]
  secure delete _s5_2
  set result
} -cleanup {
  unset -nocomplain result
} -result {6}}

###############################################################################
#
# Section 6 -- slot reuse
#
###############################################################################

runTest {test secure-6.1 {
  R-54390-04467: deleting a secure variable frees the slot for reuse
} -constraints {
    th8 crypto_testlib
} -setup {
} -body {
  secure create _s6_a "first"
  secure delete _s6_a
  secure create _s6_b "second"
  set result $_s6_b
  secure delete _s6_b
  set result
} -cleanup {
  unset -nocomplain result
} -result {second}}

###############################################################################
#
# Section 7 -- secure save: persist encrypted variable to KV store
#
###############################################################################

runTest {test secure-7.1 {
  secure save persists a variable that can be loaded back
} -constraints {
    th8 crypto_testlib secure_persist
} -setup {
  ::th8testlib::secure_persist enable
} -body {
  secure create _sp_test "roundtrip_value"
  secure save _sp_test
  secure delete _sp_test
  secure load _sp_test
  set result [set _sp_test]
  set result
} -cleanup {
  catch {secure delete _sp_test}
  ::th8testlib::secure_persist disable
  unset -nocomplain result
} -result {roundtrip_value}}

###############################################################################

runTest {test secure-7.2 {
  secure save fails without persistence enabled
} -constraints {
    th8 crypto_testlib
} -setup {
} -body {
  secure create _sp_nogate "secret"
  set rc [catch {secure save _sp_nogate} msg]
  list $rc [expr {$msg ne ""}]
} -cleanup {
  catch {secure delete _sp_nogate}
  unset -nocomplain rc msg
} -result {1 1}}

###############################################################################

runTest {test secure-7.3 {
  secure load fails for non-existent KV key
} -constraints {
    th8 crypto_testlib secure_persist
} -setup {
  ::th8testlib::secure_persist enable
} -body {
  set rc [catch {secure load _sp_nonexistent} msg]
  list $rc [expr {$msg ne ""}]
} -cleanup {
  ::th8testlib::secure_persist disable
  unset -nocomplain rc msg
} -result {1 1}}

###############################################################################

runTest {test secure-7.4 {
  secure save then load preserves value across delete
} -constraints {
    th8 crypto_testlib secure_persist
} -setup {
  ::th8testlib::secure_persist enable
} -body {
  secure create _sp_del "before_delete"
  secure save _sp_del
  set val1 [set _sp_del]
  secure delete _sp_del
  secure load _sp_del
  set val2 [set _sp_del]
  list $val1 $val2 [expr {$val1 eq $val2}]
} -cleanup {
  catch {secure delete _sp_del}
  ::th8testlib::secure_persist disable
  unset -nocomplain val1 val2
} -result {before_delete before_delete 1}}

###############################################################################

runTest {test secure-7.5 {
  secure save updates persisted value on re-save
} -constraints {
    th8 crypto_testlib secure_persist
} -setup {
  ::th8testlib::secure_persist enable
} -body {
  secure create _sp_upd "original"
  secure save _sp_upd
  set _sp_upd "updated"
  secure save _sp_upd
  secure delete _sp_upd
  secure load _sp_upd
  set result [set _sp_upd]
  set result
} -cleanup {
  catch {secure delete _sp_upd}
  ::th8testlib::secure_persist disable
  unset -nocomplain result
} -result {updated}}

###############################################################################
#
# Section 8 -- secure save/load with SQLite xKeyValue backend
#
# These tests require the th8sqlite3 plugin (provides [json] and
# the SQLite-backed xKeyValue platform callback).  They verify
# that encrypted blobs survive a real storage backend round-trip.
#
###############################################################################

runTest {test secure-8.1 {
  secure save/load roundtrip via SQLite xKeyValue backend
} -constraints {
    th8 crypto_testlib kv_sqlite secure_persist
} -setup {
  ::th8testlib::secure_persist enable
} -body {
  secure create _sq1 "sqlite_roundtrip"
  secure save _sq1
  secure delete _sq1
  secure load _sq1
  set result [set _sq1]
  set result
} -cleanup {
  catch {secure delete _sq1}
  ::th8testlib::secure_persist disable
  unset -nocomplain result
} -result {sqlite_roundtrip}}

###############################################################################

runTest {test secure-8.2 {
  secure save to SQLite then overwrite and reload
} -constraints {
    th8 crypto_testlib kv_sqlite secure_persist
} -setup {
  ::th8testlib::secure_persist enable
} -body {
  secure create _sq2 "first"
  secure save _sq2
  set _sq2 "second"
  secure save _sq2
  secure delete _sq2
  secure load _sq2
  set result [set _sq2]
  set result
} -cleanup {
  catch {secure delete _sq2}
  ::th8testlib::secure_persist disable
  unset -nocomplain result
} -result {second}}

###############################################################################

runTest {test secure-8.3 {
  secure save multiple variables to SQLite, load independently
} -constraints {
    th8 crypto_testlib kv_sqlite secure_persist
} -setup {
  ::th8testlib::secure_persist enable
} -body {
  secure create _sqa "alpha"
  secure create _sqb "beta"
  secure save _sqa
  secure save _sqb
  secure delete _sqa
  secure delete _sqb
  secure load _sqb
  secure load _sqa
  set r1 [set _sqa]
  set r2 [set _sqb]
  list $r1 $r2
} -cleanup {
  catch {secure delete _sqa}
  catch {secure delete _sqb}
  ::th8testlib::secure_persist disable
  unset -nocomplain r1 r2
} -result {alpha beta}}

###############################################################################

source tests/epilogue.tcl
