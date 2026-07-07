# Compare expansion semantics with Tcl 8.6
# Basic expansion
puts "1: [list {*}{a b c}]"

# Variable expansion
set args {hello world}
puts "2: [list {*}$args]"

# Multiple
set a {1 2}; set b {3 4}
puts "3: [list {*}$a {*}$b]"

# Empty
puts "4: [list {*}{}]"

# Mixed
puts "5: [list before {*}{x y} after]"

# Standalone
puts "6: [set x {*}]"

# Command substitution
puts "7: [list {*}[list a b c]]"

# Single element
puts "8: [list {*}{hello}]"

# Nested braces
puts "9: [list {*}{{a b} {c d}}]"

# Elements with spaces
set _spaced [list "hello world" "foo bar"]
puts "10: [list {*}$_spaced]"
