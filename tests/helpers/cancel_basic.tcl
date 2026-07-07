# Helper: interp cancel stops execution; code after cancel does not run.
set x before
interp cancel
set x after
puts $x
