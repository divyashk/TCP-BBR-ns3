if { $argc != 4} {
    puts stderr "Wrong number of input arguments- \nEnter arguments as $ns <filename> <flavour> <time> <queue limit>"
    exit 1
} else {
    set flavor [lindex $argv 2]
    set time [lindex $argv 3]
    set queueL [lindex $argv 4]
}


set ns [new Simulator]

$ns color 1 Blue

set tracefile1 [open out.tr w]
$ns trace-all $tracefile1

set winfile [open WinFile  w]

set namfile1 [open out.nam w]
$ns namtrace-all $namfile1



proc finish {} {
    global ns tracefile1 namfile1
    $ns flush-trace
    close $tracefile1
    close $namfile1
    exec nam out.nam &
    exit 0
}


# Creating node for the simulation 

set n0 [$ns node]
set n1 [$ns node]
set n2 [$ns node]
set n3 [$ns node]

# Creating links between nodes 

$ns duplex-link $n0 $n1 2Mb 10ms DropTail

# will act as our buffer
$ns simplex-link $n1 $n2 1Mb 10ms DropTail
$ns simplex-link $n2 $n1 1Mb 10ms DropTail

$ns duplex-link $n2 $n3 2Mb 10ms DropTail


# #positioning the nodes for consistency

# $ns duplex-link-op $n0 $n2 orient right-down
# $ns duplex-link-op $n1 $n2 orient right-up
# $ns duplex-link-op $n2 $n3 orient right
# $ns duplex-link-op $n3 $n2 orient left-down
# $ns duplex-link-op $n3 $n4 orient right-up
# $ns duplex-link-op $n3 $n5 orient right-down



$ns queue-limit $n1 $n2 10

set tcp [new Agent/TCP]
set sink [new Agent/TCPSink]

$ns attach-agent $n0 $tcp
$ns attach-agent $n3 $sink

$ns connect $tcp $sink

$tcp set fid_ 1

# $tcp set packetSize_ 552


set ftp [new Application/FTP]


$ftp attach-agent $tcp

$ns at 1.0 "$ftp start"
$ns at 10.0 "$ftp stop"

# Creating a recursive procedure to plot the window size.

proc plotWindow {tcpSource file} {
    global ns
    set time 0.1
    set now [$ns now]
    set cwnd [$tcpSource set cwnd_]
    puts $file "$now $cwnd"
    $ns at [expr $now+$time] "plotWindow $tcpSource $file"
}

$ns at 0.1 "plotWindow $tcp $winfile"
$ns at 11.0 "finish"

$ns run