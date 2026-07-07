puts "=== string bytelength ==="
catch {string bytelength hello} msg
puts "bytelength: $msg"

puts "=== string wordend/wordstart ==="
catch {string wordend {hello world} 2} msg1
catch {string wordstart {hello world} 8} msg2
puts "wordend: $msg1"
puts "wordstart: $msg2"

puts "=== array startsearch ==="
array set arr {a 1 b 2 c 3}
catch {set sid [array startsearch arr]} msg3
puts "startsearch: $msg3"
