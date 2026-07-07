###############################################################################
#
# dict.tcl --
#
# Tcl Language Standard
# Conformance Test File
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
# Section 1 -- dict create: Create a new dictionary
#
###############################################################################

runTest {test dict-1.1 {
  R-22739-46547: dict create with no arguments returns empty dict
} -body {
  dict create
} -result {}}

###############################################################################

runTest {test dict-1.2 {
  R-17869-41009: dict create with key-value pairs
} -body {
  dict create a 1 b 2 c 3
} -result {a 1 b 2 c 3}}

###############################################################################

runTest {test dict-1.3 {
  R-43180-64455: dict create with odd number of arguments produces error
} -setup {
} -body {
  list [catch {dict create a 1 b} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *}}

###############################################################################
#
# Section 2 -- dict get: Retrieve values from a dictionary
#
###############################################################################

runTest {test dict-2.1 {
  R-33307-13601: dict get returns value for a single key
} -body {
  dict get {a 1 b 2 c 3} b
} -result {2}}

###############################################################################

runTest {test dict-2.2 {
  R-33307-13601: dict get with no keys returns entire dictionary
} -body {
  dict get {a 1 b 2}
} -result {a 1 b 2}}

###############################################################################

runTest {test dict-2.3 {
  R-33307-13601: dict get with nested key retrieves nested value
} -body {
  dict get {a {x 10 y 20} b 2} a y
} -result {20}}

###############################################################################

runTest {test dict-2.4 {
  R-33307-13601: dict get with missing key produces error
} -setup {
} -body {
  list [catch {dict get {a 1 b 2} z} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *}}

###############################################################################
#
# Section 3 -- dict set: Store a value in a dictionary variable
#
###############################################################################

runTest {test dict-3.1 {
  R-10262-03615: dict set adds a new key to a dictionary variable
} -setup {
} -body {
  set d {a 1 b 2}
  dict set d c 3
  set d
} -cleanup {
  unset -nocomplain d
} -result {a 1 b 2 c 3}}

###############################################################################

runTest {test dict-3.2 {
  R-37114-11559: dict set replaces an existing key
} -setup {
} -body {
  set d {a 1 b 2}
  dict set d b 99
  set d
} -cleanup {
  unset -nocomplain d
} -result {a 1 b 99}}

###############################################################################

runTest {test dict-3.3 {
  R-37114-11559: dict set with nested keys creates nested dictionary
} -setup {
} -body {
  set d {a 1}
  dict set d b x 10
  dict get $d b x
} -cleanup {
  unset -nocomplain d
} -result {10}}

###############################################################################
#
# Section 4 -- dict unset: Remove a key from a dictionary variable
#
###############################################################################

runTest {test dict-4.1 {
  R-10572-25216: dict unset removes an existing key
} -setup {
} -body {
  set d {a 1 b 2 c 3}
  dict unset d b
  set d
} -cleanup {
  unset -nocomplain d
} -result {a 1 c 3}}

###############################################################################

runTest {test dict-4.2 {
  R-10572-25216: dict unset removes a nested key
} -setup {
} -body {
  set d {a {x 10 y 20} b 2}
  dict unset d a y
  dict get $d a
} -cleanup {
  unset -nocomplain d
} -result {x 10}}

###############################################################################
#
# Section 5 -- dict exists: Check whether a key exists in a dictionary
#
###############################################################################

runTest {test dict-5.1 {
  R-04929-09801: dict exists returns 1 for existing key
} -body {
  dict exists {a 1 b 2 c 3} b
} -result {1}}

###############################################################################

runTest {test dict-5.2 {
  R-04929-09801: dict exists returns 0 for missing key
} -body {
  dict exists {a 1 b 2 c 3} z
} -result {0}}

###############################################################################

runTest {test dict-5.3 {
  R-23027-59510: dict exists with nested keys checks nested existence
} -body {
  dict exists {a {x 10 y 20} b 2} a x
} -result {1}}

###############################################################################

runTest {test dict-5.4 {
  R-04929-09801: dict exists returns 0 for missing nested key
} -body {
  dict exists {a {x 10 y 20} b 2} a z
} -result {0}}

###############################################################################
#
# Section 6 -- dict keys: Return the list of keys in a dictionary
#
###############################################################################

runTest {test dict-6.1 {
  R-19857-58268: dict keys returns all keys
} -body {
  dict keys {a 1 b 2 c 3}
} -result {a b c}}

###############################################################################

runTest {test dict-6.2 {
  R-09119-24154: dict keys with glob pattern filters keys
} -body {
  dict keys {apple 1 banana 2 avocado 3} a*
} -result {apple avocado}}

###############################################################################

runTest {test dict-6.3 {
  R-19857-58268: dict keys on empty dict returns empty list
} -body {
  dict keys {}
} -result {}}

###############################################################################
#
# Section 7 -- dict values: Return the list of values in a dictionary
#
###############################################################################

runTest {test dict-7.1 {
  R-21802-14758: dict values returns all values
} -body {
  dict values {a 1 b 2 c 3}
} -result {1 2 3}}

###############################################################################

runTest {test dict-7.2 {
  R-01218-14976: dict values with glob pattern filters values
} -body {
  dict values {a hello b world c help} h*
} -result {hello help}}

###############################################################################
#
# Section 8 -- dict size: Return the number of key-value pairs
#
###############################################################################

runTest {test dict-8.1 {
  R-29709-06850: dict size returns number of key-value pairs
} -body {
  dict size {a 1 b 2 c 3}
} -result {3}}

###############################################################################

runTest {test dict-8.2 {
  R-29709-06850: dict size of empty dict is 0
} -body {
  dict size {}
} -result {0}}

###############################################################################
#
# Section 9 -- dict remove: Return dictionary with keys removed
#
###############################################################################

runTest {test dict-9.1 {
  R-59382-21479: dict remove removes a single key
} -body {
  dict remove {a 1 b 2 c 3} b
} -result {a 1 c 3}}

###############################################################################

runTest {test dict-9.2 {
  R-59382-21479: dict remove removes multiple keys
} -body {
  dict remove {a 1 b 2 c 3 d 4} b d
} -result {a 1 c 3}}

###############################################################################
#
# Section 10 -- dict replace: Return dictionary with replacements applied
#
###############################################################################

runTest {test dict-10.1 {
  R-14524-29134: dict replace replaces an existing key
} -body {
  dict replace {a 1 b 2 c 3} b 99
} -result {a 1 b 99 c 3}}

###############################################################################

runTest {test dict-10.2 {
  R-14524-29134: dict replace adds a new key
} -body {
  dict replace {a 1 b 2} c 3
} -result {a 1 b 2 c 3}}

###############################################################################
#
# Section 11 -- dict merge: Merge multiple dictionaries
#
###############################################################################

runTest {test dict-11.1 {
  R-24769-65001: dict merge merges two dictionaries
} -body {
  dict merge {a 1 b 2} {c 3 d 4}
} -result {a 1 b 2 c 3 d 4}}

###############################################################################

runTest {test dict-11.2 {
  R-24769-65001: dict merge with overlapping keys uses last value
} -body {
  dict merge {a 1 b 2} {b 99 c 3}
} -result {a 1 b 99 c 3}}

###############################################################################

runTest {test dict-11.3 {
  R-25319-64569: dict merge with empty dict returns other dict
} -body {
  dict merge {} {a 1 b 2}
} -result {a 1 b 2}}

###############################################################################
#
# Section 12 -- dict info: Return implementation information
#
###############################################################################

runTest {test dict-12.1 {
  R-64284-56368: dict info returns a string
} -body {
  string is list [dict info {a 1 b 2 c 3}]; list ok
} -result {ok}}

###############################################################################
#
# Section 13 -- dict filter: Filter dictionary entries
#
###############################################################################

runTest {test dict-13.1 {
  R-56151-04074: dict filter key filters by key glob pattern
} -body {
  dict filter {apple 1 banana 2 avocado 3} key a*
} -result {apple 1 avocado 3}}

###############################################################################

runTest {test dict-13.2 {
  R-56151-04074: dict filter key with no matches returns empty dict
} -body {
  dict filter {apple 1 banana 2} key z*
} -result {}}

###############################################################################

runTest {test dict-13.3 {
  R-56151-04074: dict filter value filters by value glob pattern
} -body {
  dict filter {a hello b world c help} value h*
} -result {a hello c help}}

###############################################################################

runTest {test dict-13.4 {
  R-56151-04074: dict filter value with no matches returns empty dict
} -body {
  dict filter {a 1 b 2 c 3} value z*
} -result {}}

###############################################################################

runTest {test dict-13.5 {
  R-56151-04074: dict filter script filters using a script
} -setup {
} -body {
  dict filter {a 1 b 2 c 3 d 4} script {k v} {
    expr {$v > 2}
  }
} -cleanup {
  unset -nocomplain result k v
} -result {c 3 d 4}}

###############################################################################

runTest {test dict-13.6 {
  R-56151-04074: dict filter script with all entries excluded returns empty
} -body {
  dict filter {a 1 b 2} script {k v} {
    expr {$v > 10}
  }
} -cleanup {
  unset -nocomplain k v
} -result {}}

###############################################################################
#
# Section 14 -- dict append: Append a string to a dictionary value
#
###############################################################################

runTest {test dict-14.1 {
  R-04288-56669: dict append appends to an existing key value
} -setup {
} -body {
  set d {a hello b world}
  dict append d a " there"
  dict get $d a
} -cleanup {
  unset -nocomplain d
} -result {hello there}}

###############################################################################

runTest {test dict-14.2 {
  R-28867-58888: dict append creates key if it does not exist
} -setup {
} -body {
  set d {a 1}
  dict append d b "new"
  dict get $d b
} -cleanup {
  unset -nocomplain d
} -result {new}}

###############################################################################
#
# Section 15 -- dict lappend: Append list elements to a dictionary value
#
###############################################################################

runTest {test dict-15.1 {
  R-50914-32512: dict lappend appends element to existing value as list
} -setup {
} -body {
  set d {a {1 2} b x}
  dict lappend d a 3
  dict get $d a
} -cleanup {
  unset -nocomplain d
} -result {1 2 3}}

###############################################################################

runTest {test dict-15.2 {
  R-50914-32512: dict lappend creates key if it does not exist
} -setup {
} -body {
  set d {a 1}
  dict lappend d b x
  dict get $d b
} -cleanup {
  unset -nocomplain d
} -result {x}}

###############################################################################
#
# Section 16 -- dict incr: Increment a dictionary value
#
###############################################################################

runTest {test dict-16.1 {
  R-32167-11311: dict incr increments by 1 by default
} -setup {
} -body {
  set d {a 5 b 10}
  dict incr d a
  dict get $d a
} -cleanup {
  unset -nocomplain d
} -result {6}}

###############################################################################

runTest {test dict-16.2 {
  R-32167-11311: dict incr increments by a specified amount
} -setup {
} -body {
  set d {a 5 b 10}
  dict incr d b 7
  dict get $d b
} -cleanup {
  unset -nocomplain d
} -result {17}}

###############################################################################

runTest {test dict-16.3 {
  R-43364-15119: dict incr creates key with value 0 if it does not exist
} -setup {
} -body {
  set d {a 1}
  dict incr d b
  dict get $d b
} -cleanup {
  unset -nocomplain d
} -result {1}}

###############################################################################
#
# Section 17 -- dict for: Iterate over key-value pairs
#
###############################################################################

runTest {test dict-17.1 {
  R-57123-15315: dict for iterates over all key-value pairs
} -setup {
} -body {
  set result {}
  dict for {k v} {a 1 b 2 c 3} {
    lappend result $k $v
  }
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain k
  unset -nocomplain v
} -result {a 1 b 2 c 3}}

###############################################################################

runTest {test dict-17.2 {
  R-57123-15315: dict for supports break
} -setup {
} -body {
  set result {}
  dict for {k v} {a 1 b 2 c 3} {
    if {$k eq "b"} then { break }
    lappend result $k $v
  }
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain k
  unset -nocomplain v
} -result {a 1}}

###############################################################################

runTest {test dict-17.3 {
  R-26792-30421: dict for over empty dict does not execute body
} -setup {
} -body {
  set result untouched
  dict for {k v} {} {
    set result touched
  }
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain k
  unset -nocomplain v
} -result {untouched}}

###############################################################################
#
# Section 18 -- dict map: Map over key-value pairs and collect results
#
###############################################################################

runTest {test dict-18.1 {
  R-10464-48599: dict map collects transformed key-value pairs
} -setup {
} -body {
  dict map {k v} {a 1 b 2 c 3} {
    expr {$v * 10}
  }
} -cleanup {
  unset -nocomplain k
  unset -nocomplain v
} -result {a 10 b 20 c 30}}

###############################################################################

runTest {test dict-18.2 {
  R-10464-48599: dict map can transform values to strings
} -setup {
} -body {
  dict map {k v} {x hello y world} {
    string toupper $v
  }
} -cleanup {
  unset -nocomplain k
  unset -nocomplain v
} -result {x HELLO y WORLD}}

###############################################################################
#
# Section 19 -- dict update: Update dictionary with variable bindings
#
###############################################################################

runTest {test dict-19.1 {
  R-62849-08308: dict update binds keys to variables and writes back
} -setup {
} -body {
  set d {a 1 b 2 c 3}
  dict update d a a b b {
    set a 10
    set b 20
  }
  list [dict get $d a] [dict get $d b] [dict get $d c]
} -cleanup {
  unset -nocomplain d
  unset -nocomplain a
  unset -nocomplain b
} -result {10 20 3}}

###############################################################################

runTest {test dict-19.2 {
  R-62849-08308: dict update unsetting a variable removes the key
} -body {
  set d {a 1 b 2}
  dict update d a a b b {
    unset a
  }
  dict exists $d a
} -cleanup {
  unset -nocomplain d
  unset -nocomplain a
  unset -nocomplain b
} -result {0}}

###############################################################################
#
# Section 20 -- dict with: Expose dictionary keys as variables
#
###############################################################################

runTest {test dict-20.1 {
  R-62515-51147: dict with exposes keys as variables and writes back
} -setup {
} -body {
  set d {a 1 b 2}
  dict with d {
    set a 10
    set b 20
  }
  list [dict get $d a] [dict get $d b]
} -cleanup {
  unset -nocomplain d
  unset -nocomplain a
  unset -nocomplain b
} -result {10 20}}

###############################################################################

runTest {test dict-20.2 {
  R-62515-51147: dict with on nested dict uses key path
} -setup {
} -body {
  set d {outer {x 10 y 20}}
  dict with d outer {
    set x 100
  }
  dict get $d outer x
} -cleanup {
  unset -nocomplain d
  unset -nocomplain x
  unset -nocomplain y
} -result {100}}

###############################################################################
#
# Section 21 -- dict: Error cases
#
###############################################################################

runTest {test dict-21.1 {
  R-62515-51147: dict with no subcommand produces error
} -setup {
} -body {
  list [catch {dict} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *}}

###############################################################################

runTest {test dict-21.2 {
  R-62515-51147: dict with unknown subcommand produces error
} -setup {
} -body {
  list [catch {dict nosuchsubcmd} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *}}

###############################################################################

runTest {test dict-21.3 {
  R-33307-13601: dict get on non-dict value produces error
} -setup {
} -body {
  list [catch {dict get "not a dict at all x" x} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *}}

###############################################################################

runTest {test dict-21.4 {
  R-29709-06850: dict size on non-dict value produces error
} -setup {
} -body {
  list [catch {dict size "a b c"} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *}}

###############################################################################
#
# Section 22 -- R-marker coverage: dict get error and nested traversal
#
###############################################################################

runTest {test dict-22.1 {
  R-02371-22586: dict get with missing key produces error
} -setup {
} -body {
  set rc [catch {dict get {a 1 b 2 c 3} nosuchkey} msg]
  list $rc [expr {$msg ne ""}]
} -cleanup {
  unset -nocomplain rc msg
} -result {1 1}}

###############################################################################

runTest {test dict-22.2 {
  R-27642-25795: dict get with nested keys drills into sub-dicts
} -setup {
} -body {
  set result [dict get {a {x {m 42}} b 2} a x m]
} -cleanup {
  unset -nocomplain result
} -result {42}}

###############################################################################
#
# Section 23 -- R-marker coverage: dict set creates variable if nonexistent
#
###############################################################################

runTest {test dict-23.1 {
  R-29570-08733: dict set creates the variable if it does not exist
} -setup {
} -body {
  #
  # newdict does not exist yet; dict set should create it.
  #
  dict set newdict mykey myvalue
  dict get $newdict mykey
} -cleanup {
  unset -nocomplain newdict
} -result {myvalue}}

###############################################################################
#
# Section 24 -- R-marker coverage: dict unset
#
###############################################################################

runTest {test dict-24.1 {
  R-03732-31421: dict unset removes a key; nested keys drill into sub-dicts
} -setup {
} -body {
  set d {a {x 10 y 20 z 30} b 2}
  dict unset d a y
  dict get $d a
} -cleanup {
  unset -nocomplain d
} -result {x 10 z 30}}

###############################################################################

runTest {test dict-24.2 {
  R-30666-01387: dict unset with missing key is not an error
} -setup {
} -body {
  set d {a 1 b 2}
  dict unset d nosuchkey
  set d
} -cleanup {
  unset -nocomplain d
} -result {a 1 b 2}}

###############################################################################
#
# Section 25 -- R-marker coverage: dict exists
#
###############################################################################

runTest {test dict-25.1 {
  R-61636-45436: dict exists never raises an error for missing keys
} -setup {
} -body {
  set r1 [dict exists {a 1 b 2} a]
  set r2 [dict exists {a 1 b 2} z]
  #
  # Nested lookup into a value that is not a dict: must
  # return 0 without raising an error.
  #
  set r3 [dict exists {a notadict} a x]
  list $r1 $r2 $r3
} -cleanup {
  unset -nocomplain r1 r2 r3
} -result {1 0 0}}

###############################################################################
#
# Section 26 -- R-marker coverage: dict filter value pattern
#
###############################################################################

runTest {test dict-26.1 {
  R-59639-33915: dict filter with value glob pattern returns matching entries
} -setup {
} -body {
  set result [dict filter {a hello b world c help d done} value h*]
} -cleanup {
  unset -nocomplain result
} -result {a hello c help}}

###############################################################################
#
# Section 27 -- R-marker coverage: dict filter script
#
###############################################################################

runTest {test dict-27.1 {
  R-47676-13385: dict filter with script keeps entries where expression is true
} -setup {
} -body {
  set result [dict filter {a 10 b 3 c 7 d 1} script {k v} {
    expr {$v >= 5}
  }]
} -cleanup {
  unset -nocomplain result k v
} -result {a 10 c 7}}

###############################################################################
#
# Section 28 -- R-marker coverage: dict incr default and dict for break/continue
#
###############################################################################

runTest {test dict-28.1 {
  R-03007-09001: dict incr with no increment argument uses 1
} -setup {
} -body {
  set d {counter 5}
  dict incr d counter
  dict get $d counter
} -cleanup {
  unset -nocomplain d
} -result {6}}

###############################################################################

runTest {test dict-28.2 {
  R-03369-11783: dict for supports break and continue within the body
} -setup {
} -body {
  set result {}
  dict for {k v} {a 1 b 2 c 3 d 4 e 5} {
    if {$k eq "c"} then { continue }
    if {$k eq "e"} then { break }
    lappend result $k $v
  }
  set result
} -cleanup {
  unset -nocomplain result k v
} -result {a 1 b 2 d 4}}

###############################################################################
#
# Section 29 -- R-marker coverage: dict map
#
###############################################################################

runTest {test dict-29.1 {
  R-44839-33014: dict map evaluates script for each key-value, returns results
                 as dict
} -setup {
} -body {
  set result [dict map {k v} {x 1 y 2 z 3} {
    expr {$v * 10}
  }]
  set result
} -cleanup {
  unset -nocomplain k v result
} -result {x 10 y 20 z 30}}

###############################################################################

source tests/epilogue.tcl
