# <<flags:+xyz>>
# <<notBefore:2020_01_01T00_00_00Z>>
puts "flags: [info exists ::th8_security(flags)]"
if {[info exists ::th8_security(flags)]} then {
    puts "  value: $::th8_security(flags)"
}
