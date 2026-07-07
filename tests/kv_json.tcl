###############################################################################
#
# kv_json.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Tests for the key-value platform (::th8testlib::kv) and the
# JSON command (json) provided by the SQLite extension.
# Covers basic operations, glob patterns, edge cases, security
# boundaries, and unusual inputs.
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
# Section 1 -- kv: basic set/get/exists/unset operations
#
###############################################################################

runTest {test kv-1.1 {
  R-28687-20710: kv set and get round-trip
} -constraints {
    th8 kv_sqlite
} -body {
  ::th8testlib::kv set _test_k1 hello
  set r [::th8testlib::kv get _test_k1]
  ::th8testlib::kv unset _test_k1
  set r
} -cleanup {
  unset -nocomplain r
} -result {hello}}

###############################################################################

runTest {test kv-1.2 {
  R-37121-11261: kv exists returns 1 for present key
} -constraints {
    th8 kv_sqlite
} -body {
  ::th8testlib::kv set _test_k2 val
  set r [::th8testlib::kv exists _test_k2]
  ::th8testlib::kv unset _test_k2
  set r
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test kv-1.3 {
  R-37121-11261: kv exists returns 0 for absent key
} -constraints {
    th8 kv_sqlite
} -body {
  ::th8testlib::kv exists _no_such_key_ever_12345
} -result {0}}

###############################################################################

runTest {test kv-1.4 {
  R-27832-52749: kv unset of absent key succeeds silently
} -constraints {
    th8 kv_sqlite
} -body {
  set rc [catch {::th8testlib::kv unset _no_such_key_ever_12345}]
  expr {$rc == 0}
} -cleanup {
  unset -nocomplain rc
} -result {1}}

###############################################################################

runTest {test kv-1.5 {
  R-03099-10973: kv get of absent key returns error
} -constraints {
    th8 kv_sqlite
} -body {
  catch {::th8testlib::kv get _no_such_key_ever_12345} msg
  set msg
} -cleanup {
  unset -nocomplain msg
} -result {key not found}}

###############################################################################

runTest {test kv-1.6 {
  R-28687-20710: kv set overwrites existing value
} -constraints {
    th8 kv_sqlite
} -body {
  ::th8testlib::kv set _test_k3 original
  ::th8testlib::kv set _test_k3 replaced
  set r [::th8testlib::kv get _test_k3]
  ::th8testlib::kv unset _test_k3
  set r
} -cleanup {
  unset -nocomplain r
} -result {replaced}}

###############################################################################
#
# Section 2 -- kv: list and glob patterns
#
###############################################################################

runTest {test kv-2.1 {
  R-11116-56026: kv list with glob pattern
} -constraints {
    th8 kv_sqlite
} -body {
  ::th8testlib::kv set _test_alpha 1
  ::th8testlib::kv set _test_bravo 2
  ::th8testlib::kv set _other_one 3
  set r [lsort [::th8testlib::kv list _test_*]]
  ::th8testlib::kv unset _test_alpha
  ::th8testlib::kv unset _test_bravo
  ::th8testlib::kv unset _other_one
  set r
} -cleanup {
  unset -nocomplain r
} -result {_test_alpha _test_bravo}}

###############################################################################

runTest {test kv-2.2 {
  R-11116-56026: kv list with no matches returns empty
} -constraints {
    th8 kv_sqlite
} -body {
  ::th8testlib::kv list _zzz_no_match_*
} -result {}}

###############################################################################
#
# Section 3 -- kv: *2 bulk operations
#
###############################################################################

runTest {test kv-3.1 {
  R-12345-42997: kv get2 returns dict of matching pairs
} -constraints {
    th8 kv_sqlite
} -body {
  ::th8testlib::kv set _bulk_a alpha
  ::th8testlib::kv set _bulk_b bravo
  ::th8testlib::kv set _bulk_c charlie
  set r [::th8testlib::kv get2 _bulk_*]
  ::th8testlib::kv unset _bulk_a
  ::th8testlib::kv unset _bulk_b
  ::th8testlib::kv unset _bulk_c
  # Dict should have 3 key-value pairs (6 elements)
  expr {[llength $r] == 6}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test kv-3.2 {
  R-47381-42074: kv list2 filters by value pattern
} -constraints {
    th8 kv_sqlite
} -body {
  ::th8testlib::kv set _filt_x hello
  ::th8testlib::kv set _filt_y world
  ::th8testlib::kv set _filt_z hello
  set r [lsort [::th8testlib::kv list2 _filt_* hello]]
  ::th8testlib::kv unset _filt_x
  ::th8testlib::kv unset _filt_y
  ::th8testlib::kv unset _filt_z
  set r
} -cleanup {
  unset -nocomplain r
} -result {_filt_x _filt_z}}

###############################################################################

runTest {test kv-3.3 {
  R-41253-10109: kv set2 bulk-updates matching keys
} -constraints {
    th8 kv_sqlite
} -body {
  ::th8testlib::kv set _upd_a old1
  ::th8testlib::kv set _upd_b old2
  ::th8testlib::kv set2 _upd_* REPLACED
  set a [::th8testlib::kv get _upd_a]
  set b [::th8testlib::kv get _upd_b]
  ::th8testlib::kv unset _upd_a
  ::th8testlib::kv unset _upd_b
  list $a $b
} -cleanup {
  unset -nocomplain a b
} -result {REPLACED REPLACED}}

###############################################################################

runTest {test kv-3.4 {
  R-28703-03927: kv unset2 returns deleted pairs as dict
} -constraints {
    th8 kv_sqlite
} -body {
  ::th8testlib::kv set _del_x xval
  ::th8testlib::kv set _del_y yval
  set d [::th8testlib::kv unset2 _del_*]
  set remaining [::th8testlib::kv list _del_*]
  list [expr {[llength $d] == 4}] $remaining
} -cleanup {
  unset -nocomplain d remaining
} -result {1 {}}}

###############################################################################

runTest {test kv-3.4.1 {
  R-53779-03588: kv set2 atomically updates every matched key
} -constraints {
    th8 kv_sqlite
} -body {
  #
  # Populate a known set of keys whose values are all distinct so
  # any partial update would leave at least one with its old value.
  # After set2, every matched key MUST hold the new value (the
  # transaction either committed in full or it never wrote at all,
  # in which case all keys would still hold their old values --
  # neither outcome leaves a mix).
  #
  for {set i 0} {$i < 12} {incr i} {
    ::th8testlib::kv set _atom_set_$i "old-$i"
  }
  ::th8testlib::kv set2 _atom_set_* NEW
  set values [list]
  for {set i 0} {$i < 12} {incr i} {
    lappend values [::th8testlib::kv get _atom_set_$i]
  }
  for {set i 0} {$i < 12} {incr i} {
    ::th8testlib::kv unset _atom_set_$i
  }
  #
  # Result is "all NEW" iff the transaction committed atomically.
  # Anything else (e.g., one key still old-3) would indicate a
  # rollback gap.
  #
  expr {[llength [lsearch -all -exact -not $values "NEW"]] == 0}
} -cleanup {
  for {set i 0} {$i < 12} {incr i} {
    catch {::th8testlib::kv unset _atom_set_$i}
  }
  unset -nocomplain i values
} -result 1}

###############################################################################

runTest {test kv-3.4.2 {
  R-08097-58386: kv unset2 atomically removes every matched pair
} -constraints {
    th8 kv_sqlite
} -body {
  #
  # Populate, then delete via unset2.  After the call, every
  # matched key MUST be gone AND the returned dictionary must
  # contain every (key,value) pair that disappeared.  The two
  # observations together rule out a partial-rollback state.
  #
  for {set i 0} {$i < 8} {incr i} {
    ::th8testlib::kv set _atom_unset_$i "v$i"
  }
  set deleted [::th8testlib::kv unset2 _atom_unset_*]
  set remaining [::th8testlib::kv list _atom_unset_*]
  list [expr {[llength $deleted] == 16}] [llength $remaining]
} -cleanup {
  for {set i 0} {$i < 8} {incr i} {
    catch {::th8testlib::kv unset _atom_unset_$i}
  }
  unset -nocomplain i deleted remaining
} -result {1 0}}

###############################################################################

runTest {test kv-3.5 {
  R-51464-55505: kv exists2 with value filter
} -constraints {
    th8 kv_sqlite
} -body {
  ::th8testlib::kv set _ex2_a needle
  ::th8testlib::kv set _ex2_b haystack
  set found [::th8testlib::kv exists2 _ex2_* needle]
  set notfound [::th8testlib::kv exists2 _ex2_* nothere]
  ::th8testlib::kv unset _ex2_a
  ::th8testlib::kv unset _ex2_b
  list $found $notfound
} -cleanup {
  catch {::th8testlib::kv unset _ex2_a}
  catch {::th8testlib::kv unset _ex2_b}
  unset -nocomplain found notfound
} -result {1 0}}

###############################################################################
#
# Section 4 -- kv: edge cases and unusual inputs
#
###############################################################################

runTest {test kv-4.1 {
  R-28687-20710: kv with empty string value
} -constraints {
    th8 kv_sqlite
} -body {
  ::th8testlib::kv set _empty_val ""
  set r [::th8testlib::kv get _empty_val]
  ::th8testlib::kv unset _empty_val
  expr {$r eq ""}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test kv-4.2 {
  R-28687-20710: kv with value containing special characters
} -constraints {
    th8 kv_sqlite
} -body {
  set special "hello\tworld\nnewline"
  ::th8testlib::kv set _special $special
  set r [::th8testlib::kv get _special]
  ::th8testlib::kv unset _special
  expr {$r eq $special}
} -cleanup {
  unset -nocomplain special r
} -result {1}}

###############################################################################

runTest {test kv-4.3 {
  R-28687-20710: kv with very long key name
} -constraints {
    th8 kv_sqlite
} -body {
  set longkey [string repeat _longkey_ 100]
  ::th8testlib::kv set $longkey value
  set r [::th8testlib::kv get $longkey]
  ::th8testlib::kv unset $longkey
  set r
} -cleanup {
  unset -nocomplain longkey r
} -result {value}}

###############################################################################

runTest {test kv-4.4 {
  R-28687-20710: kv with UTF-8 value
} -constraints {
    th8 kv_sqlite
} -body {
  ::th8testlib::kv set _utf8 "caf\xc3\xa9"
  set r [::th8testlib::kv get _utf8]
  ::th8testlib::kv unset _utf8
  set r
} -cleanup {
  unset -nocomplain r
} -match glob -result {caf*}}

###############################################################################

runTest {test kv-4.5 {
  R-31231-17812: kv wrong number of args
} -constraints {
    th8 kv_sqlite
} -body {
  catch {::th8testlib::kv} msg
  expr {$msg ne ""}
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################

runTest {test kv-4.6 {
  R-31231-17812: kv unknown operation
} -constraints {
    th8 kv_sqlite
} -body {
  catch {::th8testlib::kv nosuchop} msg
  set msg
} -cleanup {
  unset -nocomplain msg
} -result {kv: unknown operation}}

###############################################################################

runTest {test kv-4.7 {
  R-31231-17812: kv none operation is reserved
} -constraints {
    th8 kv_sqlite
} -body {
  catch {::th8testlib::kv none} msg
  string match {*reserved*} $msg
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################
#
# Section 5 -- kv: SQL injection resistance
#
###############################################################################

runTest {test kv-5.1 {
  R-03069-48740: SQL injection in key name is harmless
} -constraints {
    th8 kv_sqlite
} -body {
  set injkey "'; DROP TABLE kv; --"
  ::th8testlib::kv set $injkey safe
  set r [::th8testlib::kv get $injkey]
  ::th8testlib::kv unset $injkey
  set r
} -cleanup {
  unset -nocomplain injkey r
} -result {safe}}

###############################################################################

runTest {test kv-5.2 {
  R-03069-48740: SQL injection in value is harmless
} -constraints {
    th8 kv_sqlite
} -body {
  ::th8testlib::kv set _sqlinj "'; DROP TABLE kv; --"
  set r [::th8testlib::kv get _sqlinj]
  ::th8testlib::kv unset _sqlinj
  string match {*DROP*} $r
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test kv-5.3 {
  R-03069-48740: kv table survives injection attempts
} -constraints {
    th8 kv_sqlite
} -body {
  ::th8testlib::kv set _surv_test survival
  # Attempt injection via key name containing SQL metacharacters
  set inj "x; DELETE FROM kv; --"
  catch {::th8testlib::kv set $inj injected}
  catch {::th8testlib::kv unset $inj}
  # Original data should still be intact
  set r [::th8testlib::kv get _surv_test]
  ::th8testlib::kv unset _surv_test
  set r
} -cleanup {
  unset -nocomplain inj r
} -result {survival}}

###############################################################################
#
# Section 6 -- json: validation and type checking
#
###############################################################################

runTest {test json-1.1 {
  R-16174-56903: json valid accepts valid JSON object
} -constraints {
    th8 json
} -body {
  set j "{\"name\":\"TH8\"}"
  json valid $j
} -cleanup {
  unset -nocomplain j
} -result {1}}

###############################################################################

runTest {test json-1.2 {
  R-16174-56903: json valid rejects invalid JSON
} -constraints {
    th8 json
} -body {
  json valid {not json at all}
} -result {0}}

###############################################################################

runTest {test json-1.3 {
  R-16174-56903: json valid accepts empty object
} -constraints {
    th8 json
} -body {
  json valid "{}"
} -result {1}}

###############################################################################

runTest {test json-1.4 {
  R-16174-56903: json valid accepts empty array
} -constraints {
    th8 json
} -body {
  json valid "\[\]"
} -result {1}}

###############################################################################

runTest {test json-1.5 {
  R-16174-56903: json type returns correct types
} -constraints {
    th8 json
} -body {
  set obj "{\"s\":\"text\",\"n\":42,\"f\":3.14,\"b\":true,\"z\":null,\"a\":\[1\],\"o\":{}}"
  list \
      [json type $obj $.s] \
      [json type $obj $.n] \
      [json type $obj $.f] \
      [json type $obj $.b] \
      [json type $obj $.z] \
      [json type $obj $.a] \
      [json type $obj $.o]
} -cleanup {
  unset -nocomplain obj
} -result {text integer real true null array object}}

###############################################################################
#
# Section 7 -- json: extract and path operations
#
###############################################################################

runTest {test json-2.1 {
  R-16174-56903: json extract simple key
} -constraints {
    th8 json
} -body {
  json extract "{\"name\":\"TH8\"}" $.name
} -result {TH8}}

###############################################################################

runTest {test json-2.2 {
  R-16174-56903: json extract nested path
} -constraints {
    th8 json
} -body {
  json extract "{\"a\":{\"b\":{\"c\":\"deep\"}}}" $.a.b.c
} -result {deep}}

###############################################################################

runTest {test json-2.3 {
  R-16174-56903: json extract array element
} -constraints {
    th8 json
} -body {
  json extract "\[\"alpha\",\"bravo\",\"charlie\"\]" {$[1]}
} -result {bravo}}

###############################################################################

runTest {test json-2.4 {
  R-16174-56903: json extract missing path returns null
} -constraints {
    th8 json
} -body {
  json extract "{\"a\":1}" $.missing
} -result {}}

###############################################################################
#
# Section 8 -- json: mutation operations
#
###############################################################################

runTest {test json-3.1 {
  R-16174-56903: json set adds new key
} -constraints {
    th8 json
} -body {
  set r [json set "{\"a\":1}" $.b 2]
  json extract $r $.b
} -cleanup {
  unset -nocomplain r
} -result {2}}

###############################################################################

runTest {test json-3.2 {
  R-16174-56903: json insert does not overwrite existing
} -constraints {
    th8 json
} -body {
  json extract [json insert "{\"a\":1}" $.a 99] $.a
} -result {1}}

###############################################################################

runTest {test json-3.3 {
  R-16174-56903: json replace only affects existing keys
} -constraints {
    th8 json
} -body {
  set r [json replace "{\"a\":1}" $.a 99 $.b 2]
  list [json extract $r $.a] [json extract $r $.b]
} -cleanup {
  unset -nocomplain r
} -result {99 {}}}

###############################################################################

runTest {test json-3.4 {
  R-16174-56903: json remove deletes key
} -constraints {
    th8 json
} -body {
  json remove "{\"a\":1,\"b\":2,\"c\":3}" $.b
} -result {{"a":1,"c":3}}}

###############################################################################

runTest {test json-3.5 {
  R-16174-56903: json patch RFC 7396 merge
} -constraints {
    th8 json
} -body {
  set target "{\"a\":1,\"b\":2}"
  set patch  "{\"b\":null,\"c\":3}"
  json patch $target $patch
} -cleanup {
  unset -nocomplain patch target
} -result {{"a":1,"c":3}}}

###############################################################################
#
# Section 9 -- json: construction
#
###############################################################################

runTest {test json-4.1 {
  R-16174-56903: json array creates array
} -constraints {
    th8 json
} -body {
  json valid [json array 1 2 3]
} -result {1}}

###############################################################################

runTest {test json-4.2 {
  R-16174-56903: json object creates object
} -constraints {
    th8 json
} -body {
  set r [json object name TH8 year 2026]
  json extract $r $.name
} -cleanup {
  unset -nocomplain r
} -result {TH8}}

###############################################################################

runTest {test json-4.3 {
  R-16174-56903: json array with no args returns empty array
} -constraints {
    th8 json
} -body {
  json array
} -result {[]}}

###############################################################################

runTest {test json-4.4 {
  R-16174-56903: json object with no args returns empty object
} -constraints {
    th8 json
} -body {
  json object
} -result {{}}}

###############################################################################

runTest {test json-4.5 {
  R-16174-56903: json quote escapes special characters
} -constraints {
    th8 json
} -body {
  json quote "hello \"world\""
} -match glob -result {*hello*world*}}

###############################################################################
#
# Section 10 -- json: inspection
#
###############################################################################

runTest {test json-5.1 {
  R-16174-56903: json length of array
} -constraints {
    th8 json
} -body {
  json length "\[1,2,3,4,5\]"
} -result {5}}

###############################################################################

runTest {test json-5.2 {
  R-16174-56903: json keys of object
} -constraints {
    th8 json
} -body {
  lsort [json keys "{\"c\":3,\"a\":1,\"b\":2}"]
} -result {a b c}}

###############################################################################

runTest {test json-5.3 {
  R-16174-56903: json values of object
} -constraints {
    th8 json
} -body {
  json values "{\"a\":1,\"b\":2,\"c\":3}"
} -result {1 2 3}}

###############################################################################

runTest {test json-5.4 {
  R-16174-56903: json pretty indents output
} -constraints {
    th8 json
} -body {
  set r [json pretty "{\"a\":1}"]
  string match "*\n*" $r
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test json-5.5 {
  R-16174-56903: json error returns position for invalid JSON
} -constraints {
    th8 json
} -body {
  set pos [json error "{\"bad\":}"]
  expr {$pos > 0}
} -cleanup {
  unset -nocomplain pos
} -result {1}}

###############################################################################

runTest {test json-5.6 {
  R-16174-56903: json error returns 0 for valid JSON
} -constraints {
    th8 json
} -body {
  json error "{\"good\":true}"
} -result {0}}

###############################################################################
#
# Section 11 -- json: security and unusual inputs
#
###############################################################################

runTest {test json-6.1 {
  R-16174-56903: json handles deeply nested structures
} -constraints {
    th8 json
} -body {
  set j "{\"a\":{\"b\":{\"c\":{\"d\":{\"e\":\"deep\"}}}}}"
  json extract $j $.a.b.c.d.e
} -cleanup {
  unset -nocomplain j
} -result {deep}}

###############################################################################

runTest {test json-6.2 {
  R-16174-56903: json handles null values
} -constraints {
    th8 json
} -body {
  json type "{\"k\":null}" $.k
} -result {null}}

###############################################################################

runTest {test json-6.3 {
  R-16174-56903: json handles boolean values
} -constraints {
    th8 json
} -body {
  list [json type "{\"t\":true}" $.t] \
      [json type "{\"f\":false}" $.f]
} -result {true false}}

###############################################################################

runTest {test json-6.4 {
  R-16174-56903: json handles empty string value
} -constraints {
    th8 json
} -body {
  json extract "{\"k\":\"\"}" $.k
} -result {}}

###############################################################################

runTest {test json-6.5 {
  R-16174-56903: json handles special chars in strings
} -constraints {
    th8 json
} -body {
  set j "{\"k\":\"line1\\nline2\\ttab\"}"
  set r [json extract $j $.k]
  string match "*line1*line2*tab*" $r
} -cleanup {
  unset -nocomplain j r
} -result {1}}

###############################################################################

runTest {test json-6.6 {
  R-16174-56903: json unknown subcommand returns error
} -constraints {
    th8 json
} -body {
  catch {json nosuchcmd} msg
  set msg
} -cleanup {
  unset -nocomplain msg
} -result {json: unknown subcommand}}

###############################################################################

runTest {test json-6.7 {
  R-16174-56903: json with no args returns error
} -constraints {
    th8 json
} -body {
  catch {json} msg
  expr {$msg ne ""}
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################
#
# Section 12 -- kv + json: combined operations
#
###############################################################################

runTest {test kvjson-1.1 {
  R-58721-31908: store and retrieve JSON via kv
} -constraints {
    th8 kv_sqlite json
} -body {
  set j [json object name TH8 version 1.0]
  ::th8testlib::kv set _json_doc $j
  set r [::th8testlib::kv get _json_doc]
  set name [json extract $r $.name]
  ::th8testlib::kv unset _json_doc
  set name
} -cleanup {
  unset -nocomplain j r name
} -result {TH8}}

###############################################################################

runTest {test kvjson-1.2 {
  R-58721-31908: modify JSON stored in kv
} -constraints {
    th8 kv_sqlite json
} -body {
  ::th8testlib::kv set _json_mut "{\"count\":0}"
  set j [::th8testlib::kv get _json_mut]
  set j [json set $j $.count 42]
  ::th8testlib::kv set _json_mut $j
  set r [json extract [::th8testlib::kv get _json_mut] $.count]
  ::th8testlib::kv unset _json_mut
  set r
} -cleanup {
  unset -nocomplain j r
} -result {42}}

###############################################################################

source tests/epilogue.tcl
