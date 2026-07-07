# Helper: interp cancel -unwind should escape catch.
catch {interp cancel -unwind -- "" "unwound"}
puts "should not reach here"
