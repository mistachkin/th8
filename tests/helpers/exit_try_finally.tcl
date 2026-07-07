# Helper: verify that finally is NOT evaluated after exit.
# If finally runs, "finally_ran" is printed to stdout.
# The exit flag prevents finally from executing, so only
# "before_exit" should appear.
puts "before_exit"
try {
    exit 3
} finally {
    puts "finally_ran"
}
puts "after_try"
