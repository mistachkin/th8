# TH8 Scripting Quick-Start Guide

A gentle, complete introduction to writing scripts for the TH8
interpreter.  No prior Tcl experience required.

---

## 1  What is TH8?

TH8 is a small, secure, embeddable scripting language based on Tcl
(Tool Command Language).  If you have used Tcl, you already know TH8.
If you haven't, TH8 is an excellent way to learn --- it has the same
syntax and most of the same commands as Tcl 8.6, but in a much
smaller, auditable package designed for embedding in C applications.

Key properties:

- **Everything is a string.**  Numbers, lists, booleans --- they are
  all represented as strings and converted on demand.
- **Commands are words.**  Every statement is a command name followed
  by arguments, separated by whitespace.
- **Braces protect, brackets substitute.**  `{...}` groups text
  literally; `[...]` evaluates a command and substitutes its result.

---

## 2  Running TH8

### Interactive shell

```
$ ./bin/th8sh
TH8: To exit shell, hit Ctrl-D.
% puts "Hello, world!"
Hello, world!
% expr {2 + 3}
5
%
```

### Evaluating a one-liner

```
$ ./bin/th8sh -eval 'puts "Hello from TH8"'
Hello from TH8
```

### Running a script file

```
$ ./bin/th8sh myscript.tcl
```

Arguments after the file name are available to the script via the
`$argv` variable.

---

## 3  Variables

### Setting and reading

```tcl
set name "Alice"
set age 30
puts "Hello, $name.  You are $age years old."
```

Output: `Hello, Alice.  You are 30 years old.`

Double quotes allow variable substitution (`$name`).  Braces do not:

```tcl
puts {Hello, $name}
```

Output: `Hello, $name`

### Unsetting

```tcl
unset name
```

### Checking existence

```tcl
if {[info exists name]} {
    puts "name is set"
} else {
    puts "name is not set"
}
```

---

## 4  Expressions and Math

The `expr` command evaluates mathematical and logical expressions.
Always brace the expression for safety and performance:

```tcl
set x [expr {3 * 4 + 1}]       ;# 13
set pi [expr {acos(-1.0)}]     ;# 3.141592653589793
set ok [expr {$x > 10}]        ;# 1 (true)
```

Operators: `+  -  *  /  %  **` (power),
`==  !=  <  >  <=  >=`, `&&  ||  !`, `eq  ne` (string
comparison), `in  ni` (list membership).

Math functions: `abs`, `sin`, `cos`, `sqrt`, `pow`, `log`,
`exp`, `int`, `double`, `round`, `rand`, `srand`, `min`, `max`,
`pi`, and many more (see `info functions` for the full list).

---

## 5  Control Flow

### if / elseif / else

```tcl
if {$age < 18} {
    puts "minor"
} elseif {$age < 65} {
    puts "adult"
} else {
    puts "senior"
}
```

### while

```tcl
set i 0
while {$i < 5} {
    puts "i = $i"
    incr i
}
```

### for

```tcl
for {set i 0} {$i < 5} {incr i} {
    puts "i = $i"
}
```

### foreach

```tcl
foreach color {red green blue} {
    puts "Color: $color"
}
```

Multiple variables:

```tcl
foreach {key value} {name Alice age 30} {
    puts "$key = $value"
}
```

### switch

```tcl
switch $color {
    red     { puts "Stop" }
    green   { puts "Go" }
    yellow  { puts "Caution" }
    default { puts "Unknown" }
}
```

Glob and regexp matching:

```tcl
switch -glob $filename {
    *.txt  { puts "Text file" }
    *.html { puts "HTML file" }
    default { puts "Other" }
}
```

### break and continue

```tcl
foreach item {a b c d e} {
    if {$item eq "c"} continue   ;# skip "c"
    if {$item eq "e"} break      ;# stop at "e"
    puts $item
}
```

---

## 6  Procedures

```tcl
proc greet {name} {
    puts "Hello, $name!"
}

greet Alice     ;# Hello, Alice!
```

### Default arguments

```tcl
proc greet {name {greeting "Hello"}} {
    puts "$greeting, $name!"
}

greet Bob           ;# Hello, Bob!
greet Bob "Hi"      ;# Hi, Bob!
```

### Variable-length arguments

```tcl
proc sum {args} {
    set total 0
    foreach n $args {
        set total [expr {$total + $n}]
    }
    return $total
}

puts [sum 1 2 3 4 5]   ;# 15
```

### Return values

Every command returns a value.  `return` exits a procedure early:

```tcl
proc abs {x} {
    if {$x < 0} {
        return [expr {-$x}]
    }
    return $x
}
```

---

## 7  Strings

Strings are the fundamental data type.  TH8 provides a rich `string`
command:

```tcl
string length "hello"              ;# 5
string index "hello" 1             ;# e
string range "hello" 1 3           ;# ell
string toupper "hello"             ;# HELLO
string tolower "HELLO"             ;# hello
string trim "  hello  "            ;# hello
string match "*.txt" "readme.txt"  ;# 1
string first "ll" "hello"          ;# 2
string repeat "ab" 3               ;# ababab
string replace "hello" 1 2 "a"    ;# halo
string reverse "hello"             ;# olleh
string is integer "42"             ;# 1
string is alpha "hello"            ;# 1
string compare "a" "b"             ;# -1
string equal "a" "a"               ;# 1
```

### String substitution

Double quotes allow `$variable` and `[command]` substitution:

```tcl
set name Alice
set msg "Hello, $name!  2+2 is [expr {2+2}]."
```

The `subst` command substitutes within an arbitrary string:

```tcl
set template {Name: $name, Age: $age}
set name "Bob"
set age 25
puts [subst $template]
```

---

## 8  Lists

A Tcl list is a string where elements are separated by whitespace.
Elements containing spaces are grouped with braces:

```tcl
set colors {red green blue}
set mixed {hello 42 {two words}}
```

### Common list commands

```tcl
llength $colors                ;# 3
lindex $colors 1               ;# green
lrange $colors 0 1             ;# red green
lappend colors yellow          ;# red green blue yellow
lsearch $colors green          ;# 1
lsort {banana apple cherry}    ;# apple banana cherry
lreplace $colors 1 1 cyan      ;# red cyan blue
lreverse {a b c}               ;# c b a
join {a b c} ", "              ;# a, b, c
split "a,b,c" ","              ;# a b c
```

### Building lists safely

Use `list` to construct lists with proper quoting:

```tcl
set item "has spaces"
set L [list one $item three]
# L = {one {has spaces} three}
```

### Iterating

```tcl
foreach item $colors {
    puts "Color: $item"
}
```

---

## 9  Arrays

Arrays are collections of key-value pairs (like hash maps):

```tcl
set config(host) "localhost"
set config(port) 8080
set config(debug) 0

puts $config(host)          ;# localhost
puts [array names config]   ;# host port debug
puts [array size config]    ;# 3
```

### Iterating over arrays

```tcl
foreach key [lsort [array names config]] {
    puts "$key = $config($key)"
}
```

### Checking existence

```tcl
if {[info exists config(host)]} {
    puts "host is configured"
}
```

---

## 10  Dictionaries

Dictionaries are ordered key-value structures stored as lists:

```tcl
set person [dict create name Alice age 30 city "New York"]

dict get $person name           ;# Alice
dict set person email "a@b.com"
dict exists $person age         ;# 1
dict keys $person               ;# name age city email
dict values $person             ;# Alice 30 {New York} a@b.com
dict size $person               ;# 4
dict remove $person city        ;# name Alice age 30 email a@b.com
```

### Iterating

```tcl
dict for {key value} $person {
    puts "$key: $value"
}
```

---

## 11  Error Handling

### catch

`catch` evaluates a script and captures errors:

```tcl
if {[catch {expr {1 / 0}} result]} {
    puts "Error: $result"
} else {
    puts "Result: $result"
}
```

### try

`try` provides structured error handling:

```tcl
try {
    set f [open "missing.txt"]
} on error {msg} {
    puts "Failed: $msg"
} finally {
    puts "Cleanup done"
}
```

### error

Raise an error explicitly:

```tcl
proc positive {n} {
    if {$n <= 0} {
        error "expected positive number, got $n"
    }
    return $n
}
```

---

## 12  Input and Output

### puts and gets

```tcl
puts "Enter your name:"
gets stdin name
puts "Hello, $name!"
```

`puts` writes to stdout by default.  `puts stderr "warning"` writes
to standard error.

### File I/O via source

TH8 reads script files with `source`:

```tcl
source config.tcl
```

The platform's data-retrieval callback controls what paths are
accessible (see the Embedder's Guide).

---

## 13  Regular Expressions

Available when TH8 is compiled with `TH8_ENABLE_REGEXP`:

```tcl
regexp {^[0-9]+$} "12345"          ;# 1 (matches)
regexp {^[0-9]+$} "hello"          ;# 0 (no match)

# Capture groups
regexp {(\w+)@(\w+)} "user@host" all user host
puts "$user at $host"              ;# user at host

# Substitution
regsub {[0-9]+} "abc123def" "NUM"  ;# abcNUMdef
regsub -all {[aeiou]} "hello" "*"  ;# h*ll*
```

---

## 14  Namespaces

Namespaces prevent name collisions:

```tcl
namespace eval mylib {
    variable counter 0

    proc next {} {
        variable counter
        incr counter
    }

    proc reset {} {
        variable counter
        set counter 0
    }

    namespace export next reset
}

namespace import mylib::*

puts [next]   ;# 1
puts [next]   ;# 2
reset
puts [next]   ;# 1
```

---

## 15  Packages

Reusable libraries are distributed as packages:

```tcl
package require th8         ;# load the TH8 standard library
package names               ;# list all known packages
package scan                ;# re-scan auto_path for new packages
```

---

## 16  Time and Utilities

### Timing

```tcl
clock seconds              ;# epoch seconds (e.g. 1745280000)
time {expr {sqrt(2.0)}} 10000  ;# measure: "N microseconds per iteration"
```

### Sleeping

```tcl
after 1000   ;# pause for 1000 milliseconds (1 second)
```

### Base64

```tcl
set encoded [base64 encode "Hello, world!"]
puts $encoded                ;# SGVsbG8sIHdvcmxkIQ==
puts [base64 decode $encoded]  ;# Hello, world!
```

### Hashing

```tcl
set h [hash sha512 "Hello"]
puts $h   ;# 128-character hex SHA-512 digest
```

### Floating-point classification

```tcl
fpclassify 1.0             ;# normal
fpclassify Inf             ;# infinite
fpclassify NaN             ;# nan
fpclassify 0.0             ;# zero
```

---

## 17  Command Reference (Grouped)

### Variables
`set`, `unset`, `append`, `incr`, `global`, `upvar`, `uplevel`,
`variable`, `array` (exists, get, names, set, size, unset)

### Control Flow
`if`, `for`, `foreach`, `while`, `switch`, `break`, `continue`,
`return`, `error`, `catch`, `try`, `exit`, `after` (sleep)

### Strings
`string` (compare, equal, first, index, is, last, length, map,
match, range, repeat, replace, reverse, tolower, totitle,
toupper, trim, trimleft, trimright, wordend, wordstart,
bytelength), `format`, `scan`, `regexp`, `regsub`, `split`,
`join`, `concat`, `subst`, `base64` (encode, decode)

### Lists
`list`, `llength`, `lindex`, `lrange`, `lappend`, `lassign`,
`lreplace`, `lremove`, `lreverse`, `lsearch`, `lsort`

### Dictionaries
`dict` (create, get, set, unset, exists, keys, values, size,
remove, replace, merge, filter, append, lappend, incr, for,
map, update, with, info)

### I/O
`puts`, `gets`, `read`, `flush`, `close`, `seek`, `tell`,
`source`

### Math
`expr` (with 60+ built-in functions), `fpclassify`

### Introspection
`info` (args, body, cmdcount, commands, complete, context,
default, exists, functions, globals, level, library, loaded,
nameofexecutable, patchlevel, plugins, procs, script,
sharedlibextension, subcommands, varlinks, vars), `pid`

### Procedures
`proc`, `nproc`, `apply`, `napply`, `rename`, `tailcall`

### Namespaces
`namespace` (children, code, current, delete, eval, exists,
export, import, parent)

### Interpreter
`interp cancel`, `eval`, `time`, `clock` (seconds),
`package` (ifneeded, names, present, provide, require, scan,
forget, unknown, vcompare, versions, vsatisfies),
`load`, `unload`

### File System
`file` (channels, dirname, exists, extension, join, nativename,
normalize, pathtype, rootname, rootpath, same, separator, split,
tail, tempname, type, under, validname), `cd`, `pwd`

### Coroutines
`coroutine`, `yield`

### Advanced Control
`downlevel` (eval in caller's scope without new frame)

### Security (TH8-specific)
`hash` (SHA-512), `harpy` (sign, verify), `secure` (create,
delete, exists, save, load), `flags` (have, change, show)

---

## 18  Tips and Best Practices

1. **Always brace expressions.**  Write `expr {$a + $b}`, not
   `expr $a + $b`.  Braces prevent double substitution and enable
   bytecode optimization.

2. **Use `list` to build commands.**  Never build commands with
   string concatenation:
   ```tcl
   # Wrong:
   eval "puts $userInput"
   # Right:
   eval [list puts $userInput]
   ```

3. **Prefer `eq`/`ne` for string comparison.**  Use `==`/`!=` only
   for numbers.  `eq` and `ne` never attempt numeric conversion.

4. **Use `catch` or `try` around risky operations.**  File access,
   network operations, and user input can fail.

5. **Use namespaces for libraries.**  Avoid polluting the global
   namespace with procedure names.

6. **Keep scripts UTF-8.**  TH8 handles UTF-8 natively, including
   multi-byte characters and emoji in strings and list elements.

---

## 19  Further Reading

- [Tcl Tutorial](https://wiki.tcl-lang.org/page/Tcl+Tutorial+Index)
  --- comprehensive Tcl tutorial (most content applies to TH8)
- `tcl_language_standard_v1.md` --- the formal TH8 language
  standard with normative requirements
- `th8_public_c_api_specification.md` --- C embedding API
  reference
- `quickstart_embedding.md` --- guide for C embedders
