lappend ::auto_path [file join [pwd] lib th8]
lappend ::auto_path [file join [pwd] lib Standard1.0]
lappend ::auto_path bin
package require th8
package require th8test
package require th8test_load
detectLoadLib
testLoadLib

puts "step1"
::th8testlib::kv set _TH8BUG_X hello
puts "step2"
puts [::th8testlib::kv get _TH8BUG_X]
puts "step3"
