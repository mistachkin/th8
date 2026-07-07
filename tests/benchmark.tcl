#!/usr/bin/env tclsh
# benchmark.tcl -- Portable benchmark suite for Tcl 8.x and TH8
#
# Usage:
#   tclsh benchmark.tcl
#   th8sh benchmark.tcl
#
# Each benchmark reports microseconds per iteration via [time].
# Results are collected and printed as a summary table at the end.

# --- Detect engine -----------------------------------------------------------

if {[info exists ::tcl_platform(engine)]} then {
    set engine $::tcl_platform(engine)
} else {
    set engine "Tcl (legacy)"
}

if {[info exists ::tcl_patchLevel]} then {
    set version $::tcl_patchLevel
} elseif {[info exists ::tcl_platform(patchLevel)]} then {
    set version $::tcl_platform(patchLevel)
} else {
    set version [info patchlevel]
}

puts "Engine:   $engine v$version"
puts "Platform: [info nameofexecutable]"
puts ""

set iterations(1) 100000
set iterations(2) 50000
set iterations(3) 20000
set iterations(4) 10000
set iterations(5) 5000
set iterations(6) 1000
set iterations(7) 100

if {$engine eq "Eagle"} then {; # NOTE: Yes, Eagle is *much* slower.
  set divisor 100
} elseif {$engine eq "TH8"} then {; # NOTE: Yes, TH8 is a bit slower.
  set divisor 10
} else {
  set divisor 1
}

foreach idx [lsort -integer [array names iterations]] {
  set iterations($idx) [expr {int($iterations($idx) / $divisor)}]
}

# --- Benchmark harness -------------------------------------------------------

set results {}

proc bench {name iterations script} {
    global results
    # Warm up: run once to populate any internal-representation caches
    uplevel 1 $script
    # Measure
    set usec [lindex [uplevel 1 [list time $script $iterations]] 0]
    lappend results [list $name $iterations $usec]
    puts [format "  %-40s %8.1f us/iter  (%d iters)" $name $usec $iterations]
}

# === SECTION 1: Expression evaluation ========================================

puts "--- Expressions ---"

bench "integer arithmetic (add/mul)" $iterations(1) {
    expr {((42 + 17) * 3 - 11) / 2}
}

bench "floating-point arithmetic" $iterations(1) {
    expr {3.14159 * 2.71828 + 1.41421 / 0.57722}
}

bench "comparison chain" $iterations(1) {
    expr {1 < 2 && 2 < 3 && 3 < 4 && 4 < 5 && 5 < 6}
}

bench "ternary operator" $iterations(1) {
    expr {1 > 0 ? "yes" : "no"}
}

bench "string equality (eq)" $iterations(1) {
    expr {"hello" eq "hello"}
}

bench "math: sqrt(2.0)" $iterations(1) {
    expr {sqrt(2.0)}
}

bench "math: sin/cos/atan2" $iterations(2) {
    expr {sin(0.5) + cos(0.5) + atan2(1.0, 1.0)}
}

bench "math: pow(2, 20)" $iterations(1) {
    expr {pow(2, 20)}
}

bench "bitwise ops (and/or/xor/shift)" $iterations(1) {
    expr {((0xFF & 0x0F) | 0xF0) ^ (1 << 4)}
}

# === SECTION 2: String operations ============================================

puts "\n--- Strings ---"

set longstr [string repeat "abcdefghij" $iterations(7)]

bench "string length (long string)" $iterations(1) {
    string length $longstr
}

bench "string index (middle)" $iterations(1) {
    string index $longstr 500
}

bench "string range (long string)" $iterations(1) {
    string range $longstr 100 199
}

bench "string first (near end)" $iterations(2) {
    string first "ij" $longstr 900
}

bench "string match (glob)" $iterations(1) {
    string match "*defg*" $longstr
}

bench "string map (3 replacements)" $iterations(4) {
    string map {abc ABC def DEF ghi GHI} $longstr
}

bench "string tolower (long string)" $iterations(2) {
    string tolower $longstr
}

bench "string compare" $iterations(1) {
    string compare $longstr $longstr
}

bench "string repeat (long string)" $iterations(2) {
    string repeat "0123456789" 100
}

bench "append (random)" $iterations(4) {
    append s [expr {random()}]
}

# === SECTION 3: List operations ==============================================

puts "\n--- Lists ---"

set biglist [list]
for {set i 0} {$i < $iterations(6)} {incr i} {
    lappend biglist "item$i"
}

bench "llength (big list)" $iterations(1) {
    llength $biglist
}

bench "lindex (big list, middle)" $iterations(1) {
    lindex $biglist 500
}

bench "lrange (big list)" $iterations(2) {
    lrange $biglist 100 199
}

bench "lsearch -exact (near end)" $iterations(4) {
    lsearch -exact $biglist "item999"
}

bench "lsort (small strings)" $iterations(5) {
    lsort [lrange $biglist 0 99]
}

bench "lsort -integer (small list)" $iterations(5) {
    lsort -integer [list 93 17 42 88 5 71 36 60 12 99 \
        28 55 3 77 44 19 66 81 9 50 \
        37 72 14 96 23 61 8 47 84 31 \
        68 2 53 79 16 40 91 26 64 7 \
        35 78 11 58 95 22 49 86 33 70 \
        4 41 87 15 62 29 76 10 43 98 \
        25 56 83 20 67 34 73 6 51 90 \
        27 54 82 18 65 32 75 13 48 97 \
        24 59 85 21 69 38 74 1 46 92 \
        30 57 80 39 63 0 45 89 52 94]
}

bench "lappend (small list)" $iterations(4) {
    set lst {}
    for {set i 0} {$i < 100} {incr i} {
        lappend lst $i
    }
}

bench "join (big list)" $iterations(4) {
    join $biglist ","
}

bench "split (comma-separated)" $iterations(4) {
    split "a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z" ","
}

bench "list creation (small list)" $iterations(2) {
    list a b c d e f g h i j k l m n o p q r s t u v w x y z \
         A B C D E F G H I J K L M N O P Q R S T U V W X Y Z \
         0 1 2 3 4 5 6 7 8 9 aa bb cc dd ee ff gg hh ii jj kk \
         ll mm nn oo pp qq rr ss tt uu vv ww xx yy zz 00 11 22 \
         33 44 55 66 77 88 99 ab cd ef 01 23 45 67 89 aA bB cC
}

# === SECTION 4: Control flow =================================================

puts "\n--- Control Flow ---"

bench "for loop (big)" $iterations(6) {
    for {set i 0} {$i < $iterations(6)} {incr i} {}
}

bench "while loop (big)" $iterations(6) {
    set i 0
    while {$i < $iterations(6)} { incr i }
}

bench "foreach (big)" $iterations(6) {
    foreach x $biglist {}
}

bench "if/elseif/else chain (5 branches)" $iterations(1) {
    set x 3
    if {$x == 0} then {
        set r a
    } elseif {$x == 1} then {
        set r b
    } elseif {$x == 2} then {
        set r c
    } elseif {$x == 3} then {
        set r d
    } else {
        set r e
    }
}

bench "switch -exact (10 cases)" $iterations(1) {
    switch -exact "case7" {
        case0 { set r 0 } case1 { set r 1 } case2 { set r 2 }
        case3 { set r 3 } case4 { set r 4 } case5 { set r 5 }
        case6 { set r 6 } case7 { set r 7 } case8 { set r 8 }
        case9 { set r 9 }
    }
}

bench "catch (no error)" $iterations(1) {
    catch { expr {1 + 1} } result
}

bench "catch (error path)" $iterations(2) {
    catch { error "test" } result
}

# === SECTION 5: Procedures ===================================================

puts "\n--- Procedures ---"

proc add {a b} { expr {$a + $b} }

proc fib {n} {
    if {$n <= 1} then { return $n }
    expr {[fib [expr {$n - 1}]] + [fib [expr {$n - 2}]]}
}

proc identity {x} { return $x }

proc multicall {} {
    identity 1; identity 2; identity 3; identity 4; identity 5
    identity 6; identity 7; identity 8; identity 9; identity 10
}

bench "proc call (2 args)" $iterations(1) {
    add 42 17
}

bench "proc call (identity)" $iterations(1) {
    identity hello
}

bench "10 proc calls in sequence" $iterations(2) {
    multicall
}

bench "recursive fibonacci(15)" $iterations(6) {
    fib 15
}

bench "recursive fibonacci(20)" $iterations(7) {
    fib 20
}

catch {rename add ""}
catch {rename fib ""}
catch {rename identity ""}
catch {rename multicall ""}

# === SECTION 6: Variables ====================================================

puts "\n--- Variables ---"

bench "set/get variable" $iterations(1) {
    set myvar "hello world"
    set x $myvar
}

bench "incr" $iterations(1) {
    set counter 0
    incr counter
    incr counter
    incr counter
}

bench "array set/get" $iterations(2) {
    set arr(key1) value1
    set arr(key2) value2
    set x $arr(key1)
    set y $arr(key2)
}

bench "upvar (1 level)" $iterations(2) {
    proc setvia {name val} { upvar 1 $name v; set v $val }
    setvia myvar "test"
}

bench "global variable access" $iterations(1) {
    set ::gvar "global"
    set x $::gvar
}

# === SECTION 7: Format/scan =================================================

puts "\n--- Format/Scan ---"

bench "format %d" $iterations(1) {
    format "%d" 42
}

bench "format %s (string)" $iterations(1) {
    format "Hello, %s! You are %d years old." "World" 42
}

bench "format %f (float)" $iterations(2) {
    format "%.6f" 3.14159265358979
}

if {[llength [info commands scan]] > 0} then {
  bench "scan (integer)" $iterations(1) {
      scan "42" "%d" x
  }

  bench "scan (multiple fields)" $iterations(2) {
      scan "42 3.14 hello" "%d %f %s" a b c
  }

  unset -nocomplain x a b c
}

# === SECTION 8: Regex (if available) =========================================

if {[llength [info commands regexp]] > 0} then {
    puts "\n--- Regular Expressions ---"

    bench "regexp (simple match)" $iterations(2) {
        regexp {^[a-z]+$} "helloworld"
    }

    bench "regexp (capture groups)" $iterations(3) {
        regexp {(\d+)-(\d+)-(\d+)} "2026-04-04" dummy y m d
    }

    bench "regexp (alternation)" $iterations(3) {
        regexp {foo|bar|baz|qux} "the qux jumped"
    }

    bench "regsub (global replace)" $iterations(4) {
        regsub -all {[aeiou]} "hello beautiful world" "*"
    }
}

# === SECTION 9: Composite workloads ==========================================

puts "\n--- Composite Workloads ---"

bench "tokenize CSV line (split+foreach)" $iterations(4) {
    set line "name,age,city,state,zip,phone,email,dept,title,id"
    set fields [split $line ","]
    set i 0
    foreach f $fields { incr i }
}

bench "build key-value pairs (list ops)" $iterations(5) {
    set pairs {}
    for {set i 0} {$i < 50} {incr i} {
        lappend pairs "key$i" "value$i"
    }
}

bench "nested proc + expr + string" $iterations(4) {
    proc compute {x} {
        set s [format "%.2f" [expr {sqrt($x * $x + 1.0)}]]
        return [string length $s]
    }
    compute 42.5
}

bench "eval (dynamic script)" $iterations(2) {
    eval {expr {2 + 2}}
}

bench "info commands (full list)" $iterations(4) {
    info commands
}

# === Summary =================================================================

puts "\n=========================================="
puts "SUMMARY: $engine v$version (divisor: $divisor)"
puts "=========================================="
puts [format "  %-40s %10s %8s" "Benchmark" "us/iter" "iters"]

puts [format "  %-40s %10s %8s" \
    [string repeat "-" 40] [string repeat "-" 10] [string repeat "-" 8]]

foreach entry $results {
    set name [lindex $entry 0]
    set iters [lindex $entry 1]
    set usec [lindex $entry 2]
    puts [format "  %-40s %10.1f %8d" $name $usec $iters]
}
puts ""
puts "Total benchmarks: [llength $results]"

unset -nocomplain engine version iterations idx results longstr s i \
                  biglist lst x r result myvar counter arr y gvar \
                  dummy m d line fields f pairs entry name iters usec \
                  divisor
