set ns [new Simulator]

$ns color 1 Blue
$ns color 2 Red

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
set n4 [$ns node]
set n5 [$ns node]

# Creating links between nodes 

$ns duplex-link $n0 $n2 2Mb 10ms DropTail
$ns duplex-link $n1 $n2 2Mb 10ms DropTail
$ns duplex-link $n2 $n3 0.3Mb 100ms DropTail
$ns duplex-link $n3 $n2 0.3Mb 100ms DropTail
$ns duplex-link $n3 $n4 0.5Mb 40ms DropTail
$ns duplex-link $n3 $n5 0.5Mb 30ms DropTail


#positioning the nodes for consistency

$ns duplex-link-op $n0 $n2 orient right-down
$ns duplex-link-op $n1 $n2 orient right-up
$ns duplex-link-op $n2 $n3 orient right
$ns duplex-link-op $n3 $n2 orient left-down
$ns duplex-link-op $n3 $n4 orient right-up
$ns duplex-link-op $n3 $n5 orient right-down



$ns queue-limit $n2 $n3 4

set tcp [new Agent/TCP]
set sink [new Agent/TCPSink]

set udp [new Agent/UDP]
set null [new Agent/Null]

$ns attach-agent $n0 $tcp
$ns attach-agent $n4 $sink

$ns connect $tcp $sink

$ns attach-agent $n1 $udp 
$ns attach-agent $n5 $null

$ns connect $udp $null

$tcp set fid_ 1

$tcp set packetSize_ 552


set ftp [new Application/FTP]


$ftp attach-agent $tcp



$udp set fid_ 2

set cbr [ new Application/Traffic/CBR ]
$cbr attach-agent $udp
$cbr set packetSize_ 1000
$cbr set rate_ 0.01 Mb
$cbr set random_ false


$ns at 0.1 "$cbr start"
$ns at 1.0 "$ftp start"
$ns at 124.0 "$ftp stop"
$ns at 124.5 "$cbr stop"


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
$ns at 125.0 "finish"
$ns run





