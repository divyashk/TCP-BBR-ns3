if {$argc != 1} {puts stderr "ERROR! ns called with wrong number of arguments!"
    exit 1
} else {
    set val [lindex $argv 0]
} 


proc prime_check {val} {
    for { set a 2 } { $a <= $val } { incr a }  {
        set b 0
        for {set i 2} {$a <= $val} {incr i}  {
            set d [expr fmod($a, $i)]
            if {$d == 0}  {
                set b 1
            } 
        
        } 

        if {$b == 1}  {
            puts "$a is not a prime number"
        }  else {
            puts "$a is a prime number"
        } 
    } 
} 

prime_check $val



