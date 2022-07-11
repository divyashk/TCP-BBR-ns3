if {$argc > 1} {
    puts "Usage: ns randtest1.tcl \[replication number\]"
    exit
}
set run 1
if {$argc == 1} {
    set run [lindex $argv 0]
}
if {$run < 1} {
    set run 1
}

# # seed the default RNG
# global defaultRNG
# $defaultRNG seed 9999

# create the RNGs and set them to the correct substream

set size [new RNG]
for {set j 1} {$j < $run} {incr j} {
  
    $size next-substream
}

# size_ is a uniform random variable describing packet sizes
set size_ [new RandomVariable/Uniform]
$size_ set min_ -10
$size_ set max_ 100
$size_ use-rng $size

# print the first 5 arrival times and sizes
for {set j 0} {$j < 10} {incr j} {
    puts [format "%-4d" [expr round([$size_ value])]]
}