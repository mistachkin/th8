# Minimal no-op helper used by coverage_shell_subprocess.tcl
# to confirm a subprocess th8sh ran through
# Th8Shell_ApplyStandardEnvFeatures successfully.  Emits a
# fixed token on stdout so the parent can verify subprocess
# completion.
puts "SHELL_SUBPROC_OK"
