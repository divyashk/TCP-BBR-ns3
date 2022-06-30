puts $argc

if { $argc != 3} {
    puts stderr "Wrong number of input arguments- \nEnter arguments as $ns <filename> <flavour> <time> <queue limit>"
    exit 1
} else {
    set flavor [lindex $argv 0]
    set time [lindex $argv 1]
    set queueL [lindex $argv 2]
}


set ns [new Simulator]

$ns color 1 Blue

set tracefile1 [open out.tr w]
$ns trace-all $tracefile1

set winfile [open "tcp_${flavor}" w]

set namfile1 [open out.nam w]
$ns namtrace-all $namfile1



proc finish {} {
    global ns tracefile1 namfile1
    $ns flush-trace
    close $tracefile1
    close $namfile1
    #exec nam out.nam &
    exit 0
}


# Creating node for the simulation 

set n0 [$ns node]
set n1 [$ns node]
set n2 [$ns node]


# Creating links between nodes 

$ns duplex-link $n0 $n1 2Mb 10ms DropTail

# will act as our buffer
$ns duplex-link $n1 $n2 1Mb 10ms DropTail


# #positioning the nodes for consistency
# $ns duplex-link-op $n0 $n2 orient right-down


$ns queue-limit $n1 $n2 $queueL

if { $flavor == "Default" || $flavor == "Tahoe"} {
    set tcp [new Agent/TCP]
} else {
    set tcp [new Agent/TCP/$flavor]
}

set sink [new Agent/TCPSink]

$ns attach-agent $n0 $tcp
$ns attach-agent $n2 $sink

$ns connect $tcp $sink

$tcp set fid_ 1

# $tcp set packetSize_ 552


set ftp [new Application/FTP]


$ftp attach-agent $tcp

$ns at 1.0 "$ftp start"
$ns at $time "$ftp stop"

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
$ns at $time+1 "finish"

$ns run