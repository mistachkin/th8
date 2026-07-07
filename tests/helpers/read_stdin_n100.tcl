# Helper: reads up to 100 chars of stdin via [read stdin 100], prints it.
# Used to drive the (T,F) vector at th8_io.c L783 and L790 where the
# numChars limit is set but the actual input is shorter.
puts -nonewline [read stdin 100]
