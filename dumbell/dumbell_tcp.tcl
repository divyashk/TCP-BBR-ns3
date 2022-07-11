if { $argc != 8} {
    puts stderr "Wrong number of input arguments- \nEnter arguments as $ns <filename> <flavour> <time> <queue limit> <number of source/sinks>  <total_input_bandwidth> <bottleneck_bandwidth> <RTT> <recvWindow>"
    exit 1
} else {
    set flavor [lindex $argv 0]
    set time [lindex $argv 1]
    set queueL [lindex $argv 2]
    set noOfSource [lindex $argv 3]
    set total_input_bandwidth [lindex $argv 4] 
    set bottleneck_bandwidth [lindex $argv 5]
    set RTT [lindex $argv 6]
    set recvWind [lindex $argv 7]
}

set ns [new Simulator]

# set droptail_obj [ new Queue/DropTail ]

# $droptail_obj set drop_front_ false
# $droptail_obj set summarystats_ false
# $droptail_obj set queue_in_bytes_ false
# $droptail_obj set mean_pktsize_ 1500

Queue/DropTail set drop_front_ false
Queue/DropTail set summarystats_ false
Queue/DropTail set queue_in_bytes_ false
Queue/DropTail set mean_pktsize_ 1500

set tracefile1 [open out.tr w]
$ns trace-all $tracefile1

set namfile1 [open out.nam w]
$ns namtrace-all $namfile1

set link_bandwidth [expr $total_input_bandwidth/$noOfSource ]

# set link_delay [ expr $RTT/4 ]

set link_queueL [expr 5000/$noOfSource]

proc finish {} {
    global ns tracefile1 namfile1
    $ns flush-trace
    close $tracefile1
    close $namfile1
    #exec nam out.nam &
    exit 0
}

# Creating the router nodes
set router_a [$ns node];
set router_b [$ns node];

# Creating a random variable from a uniform distribution with mean RTT.
set rand_s [new RNG]
set run 1
for {set j 1} {$j < $run} {incr j} {
    $rand_s next-substream
}
set urand [new RandomVariable/Uniform]

$urand set min_ 0
$urand set max_ [expr 2*$RTT]
$urand use-rng $rand_s

# for verifying the random RTT values obtained.
# set RTT_file [open "rtt_values" w]



# Creating the same of number of source and sink nodes and making their connections 
for {set i 0} { $i < $noOfSource } { incr i} {
    

    set winfile($i) [open "tcp_${flavor}_${i}" w]

    if { $flavor == "Default" || $flavor == "Tahoe"} {
        set tcp_source($i) [new Agent/TCP]
    } else {
        set tcp_source($i) [new Agent/TCP/$flavor]
    }

    set tcp_sink($i) [new Agent/TCPSink]

    set ftp($i) [new Application/FTP]
    
    set source_arr($i) [$ns node]

    set random_RTT [expr round([$urand value])] 
    #puts $RTT_file $random_RTT

    set per_link_delay  [expr $random_RTT/4]
    

    $ns duplex-link $source_arr($i) $router_a "${link_bandwidth}Mb" "${per_link_delay}ms" DropTail
    $ns queue-limit $source_arr($i) $router_a $link_queueL

    $ns duplex-link-op $source_arr($i) $router_a orient right
    $ns attach-agent $source_arr($i) $tcp_source($i)

    $tcp_source($i) set window_ $recvWind

    #$tcp_source($i) set packetSize_ 1500 
    
    set sink_arr($i) [$ns node];
    $ns duplex-link $sink_arr($i) $router_b "${link_bandwidth}Mb" "${per_link_delay}ms" DropTail
    $ns queue-limit $sink_arr($i) $router_b $link_queueL

    $ns duplex-link-op $sink_arr($i) $router_b orient left
    $ns attach-agent $sink_arr($i) $tcp_sink($i)

    $ns connect $tcp_source($i) $tcp_sink($i)

    $ftp($i) attach-agent $tcp_source($i)

    $tcp_source($i) set fid_ $i+1

}


# Creating link bettween router_a and router_b
$ns simplex-link $router_a $router_b "${bottleneck_bandwidth}Mb" 1ms DropTail
$ns simplex-link $router_b $router_a "${bottleneck_bandwidth}Mb" 1ms DropTail
$ns queue-limit $router_a $router_b $queueL

# Monitoring the queue between the routers
set qmon [ $ns monitor-queue $router_a $router_b [ open bottleneck_monitor.tr w ] 0.1];
[$ns link $router_a $router_b ] start-tracing

for {set i 0} { $i < $noOfSource } { incr i } {
    $ns at [expr 0.1 + [expr 0.1 * $i]] "$ftp($i) start"
    #$ns at 0.1 "$ftp($i) start"
    $ns at $time "$ftp($i) stop"
}

# Creating a recursive procedure to plot the window size.

proc plotWindow {tcpSource file} {
    global ns
    set time 0.1
    set now [$ns now]
    set cwnd [$tcpSource set cwnd_]
    puts $file "$now $cwnd"
    $ns at [expr $now+$time] "plotWindow $tcpSource $file"
}

for {set i 0} { $i < $noOfSource } { incr i} {
    $ns at 0.1 "plotWindow $tcp_source($i) $winfile($i)"
}


# set units_file [open "inputs_with_units" w]
# puts $units_file [$tcp_source(0) set window_ ]
# puts $units_file [$tcp_source(0) set packetSize_ ]
# puts $units_file []


$ns at $time+1 "finish"

$ns run