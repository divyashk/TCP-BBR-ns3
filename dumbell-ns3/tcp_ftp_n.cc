#include <iostream>
#include <fstream>
#include <string>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/error-model.h"
#include "ns3/tcp-header.h"
#include "ns3/udp-header.h"
#include "ns3/enum.h"
#include "ns3/event-id.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/traffic-control-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/config-store-module.h"
#include "ns3/node.h"
#include "ns3/netanim-module.h"

#define MAX_SOURCES 100;

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TCPSCRIPT");

std::vector<uint32_t> cwnd;
std::vector<Ptr<OutputStreamWrapper>> cwnd_streams;
uint64_t queueSize;
Ptr<OutputStreamWrapper> qSize_stream;

uint64_t bottleneckTransimitted;
Ptr<OutputStreamWrapper> bottleneckTransimittedStream;

uint64_t droppedPackets;
Ptr<OutputStreamWrapper> dropped_stream;

uint32_t nSources;

static void
plotCwndChange (uint32_t pos, uint32_t oldCwnd, uint32_t newCwnd){
    cwnd[pos] = newCwnd;
}

static void  
startTracingCwnd(uint32_t nSources){
    for( uint32_t i = 0; i < nSources; i++){
        //Trace changes to the congestion window
        Config::ConnectWithoutContext ("/NodeList/"+std::to_string(i+2)+"/$ns3::TcpL4Protocol/SocketList/0/CongestionWindow", MakeBoundCallback (&plotCwndChange, i));
    }
}

//Trace Congestion window length
static void
writeCwndToFile (uint32_t nSources)
{
    for( uint32_t i = 0; i < nSources; i++){
         
        *cwnd_streams[i]->GetStream () << Simulator::Now().GetSeconds() << "\t" << cwnd[i] << std::endl;
    }
}

static void
plotQsizeChange (uint32_t oldQSize, uint32_t newQSize){
    //NS_LOG_UNCOND(Simulator::Now().GetSeconds() << "\t" << newCwnd);
    
    queueSize = newQSize;
}

static void
RxDrop(Ptr<OutputStreamWrapper> stream,  Ptr<const Packet> p){
   // std::cout << "Packet Dropped (finally!)" << std::endl;
   //*stream->GetStream () << Simulator::Now().GetSeconds() << "\tRxDrop" << std::endl;
   droppedPackets++;
} 

static void
TxPacket(Ptr<const Packet> p){
    bottleneckTransimitted++;
}

static void
TraceDroppedPacket(std::string droppedTrFileName){
    // tracing all the dropped packets in a seperate file
    Config::ConnectWithoutContext("/NodeList/*/DeviceList/*/$ns3::PointToPointNetDevice/TxQueue/Drop", MakeBoundCallback(&RxDrop, dropped_stream));
    Config::ConnectWithoutContext("/NodeList/*/DeviceList/*/$ns3::PointToPointNetDevice/MacTxDrop", MakeBoundCallback(&RxDrop, dropped_stream));
    Config::ConnectWithoutContext("/NodeList/*/DeviceList/*/$ns3::PointToPointNetDevice/PhyRxDrop", MakeBoundCallback(&RxDrop, dropped_stream));
    Config::ConnectWithoutContext("/NodeList/*/DeviceList/*/$ns3::PointToPointNetDevice/PhyTxDrop", MakeBoundCallback(&RxDrop, dropped_stream));
    //Config::ConnectWithoutContext("/NodeList/*/DeviceList/*/$ns3::PointToPointNetDevice/TcDrop", MakeBoundCallback(&RxDrop, dropped_stream));

}

static void
TraceQueueSize(){
    *qSize_stream->GetStream() << Simulator::Now().GetSeconds() << "\t" << queueSize << std::endl;
}

static void
TraceDroppedPkts(){
    *dropped_stream->GetStream() << Simulator::Now().GetSeconds() << "\t" << droppedPackets << std::endl;
}

static void
TraceBottleneckTx(){
    *bottleneckTransimittedStream->GetStream() << Simulator::Now().GetSeconds() << "\t" << bottleneckTransimitted << std::endl;
}

static void
StartTracingQueueSize(){
    Config::ConnectWithoutContext("/NodeList/0/DeviceList/0/$ns3::PointToPointNetDevice/TxQueue/PacketsInQueue", MakeCallback(&plotQsizeChange));
}

static void
StartTracingTransmitedPacket(){
    bottleneckTransimitted = 0;
    Config::ConnectWithoutContext("/NodeList/0/DeviceList/0/$ns3::PointToPointNetDevice/PhyTxEnd", MakeCallback(&TxPacket));
}

/// BOTTLENECK THROUGHPUT
//Store Throughput for bottleneck to file

std::vector<uint32_t> totalBytes;
std::vector<std::vector<double>> throughputvstime;
Ptr<OutputStreamWrapper> throughputvstime_stream;

static void
writeThroughputToFile (uint32_t simDurationInSeconds)
{   
    // Write header: RouterID t0 t1 t2 ... tN
    *throughputvstime_stream->GetStream ()  << "RouterID\t";
    for (uint32_t t = 0; t < simDurationInSeconds; ++t) {
        *throughputvstime_stream->GetStream ()  << t << "\t";
    }
    *throughputvstime_stream->GetStream ()  << "\n";

    // Write throughput values for each router
    for (uint32_t i = 0; i < 1; ++i) {
        *throughputvstime_stream->GetStream ()  << i << "\t";
        for (uint32_t t = 0; t < simDurationInSeconds; ++t) {
            *throughputvstime_stream->GetStream ()  << throughputvstime[i][t] << "\t";
        }
        *throughputvstime_stream->GetStream ()  << "\n";
    }
}

void bottleneckTxCallback(uint32_t router, Ptr<const Packet> p){
    totalBytes[router] += p->GetSize();
}

// This is called every second
void ThroughputCallback() {
    static Time lastTime = Simulator::Now();

    Time currentTime = Simulator::Now();
    Time timeDiff = currentTime - lastTime;
    for (int i = 0; i<1 ;i++)
        throughputvstime[i][Simulator::Now().GetSeconds()] = ((totalBytes[i]) * 8.0 / timeDiff.GetSeconds())/1024/1024; // in MBps

    //Reset the size of the transmitted packets
    totalBytes[0] = 0;

    lastTime = currentTime;
}

static void  
startTracingThroughput(uint32_t simDurationInSeconds){
    throughputvstime.resize(1, std::vector<double>(simDurationInSeconds));

    //Trace changes to the throughput(tx) window only on the bottlenecks
    Config::ConnectWithoutContext ("/NodeList/1/DeviceList/0/PhyTxEnd", MakeBoundCallback (&bottleneckTxCallback, 0));
}

/// DESTINATION THROUGHPUT

std::vector<uint32_t> totalBytesDest;
std::vector<std::vector<double>> throughputvstimeDest;
Ptr<OutputStreamWrapper> throughputvstime_streamDest;

static void
writeThroughputToFileDest (uint32_t simDurationInSeconds)
{   
    // Write header: DestID t0 t1 t2 ... tN-1
    *throughputvstime_streamDest->GetStream ()  << "DestID\t";
    for (uint32_t t = 0; t < simDurationInSeconds; ++t) {
        *throughputvstime_streamDest->GetStream ()  << t << "\t";
    }
    *throughputvstime_streamDest->GetStream ()  << "\n";

    // Write throughput values for each router
    for (uint32_t i = 0; i < nSources; ++i) {
        *throughputvstime_streamDest->GetStream ()  <<  i << "\t";
        for (uint32_t t = 0; t < simDurationInSeconds; ++t) {
            *throughputvstime_streamDest->GetStream ()  << throughputvstimeDest[i][t] << "\t";
        }
        *throughputvstime_streamDest->GetStream ()  << "\n";
    }
}

void bottleneckDestTxCallback(uint64_t dest, Ptr<const Packet> p){
    totalBytesDest[dest] += p->GetSize();
}

// This is called every second
void DestThroughputCallback() {
    static Time lastTime = Simulator::Now();

    Time currentTime = Simulator::Now();
    Time timeDiff = currentTime - lastTime;
    
    for (uint64_t i = 0; i < nSources; i++)
       throughputvstimeDest[i][Simulator::Now().GetSeconds()] += ((totalBytesDest[i]) * 8.0 / timeDiff.GetSeconds())/1024/1024; // in Mbps
      
    //Reset the size of the transmitted packets at destination hosts
    totalBytesDest.resize(nSources, 0);
 
    lastTime = currentTime;
}


static void  
startTracingThroughputDest(uint32_t simDurationInSeconds){
    throughputvstimeDest.resize(nSources+1, std::vector<double>(simDurationInSeconds));

    //Trace changes to the throughput(tx) window only on the destination hosts
    for ( uint64_t i = 0; i < nSources; i++)
        Config::ConnectWithoutContext ("/NodeList/"+std::to_string(i+nSources+2)+"/DeviceList/0/PhyTxEnd", MakeBoundCallback (&bottleneckDestTxCallback, i));
    
}



int 
main(int argc, char *argv[])
{   
    ConfigStore config;
    config.ConfigureDefaults ();
    config.ConfigureAttributes ();

    std::string bottleneckBandwidth = "100Mbps";
    std::string bottleneckDelay = "1ms";
    std::string accessBandwidth = "120Mbps";
    std::string accessDelay = "2.5ms";
    std::string RTT = "10ms";   
    std::string sourceRate = "120Mbps";
    std::string flavour = "TcpBbr";
    std::string queueType = "DropTail";       //DropTail or CoDel
    std::string queueSize = "2084p";      //in packets
    
   
    uint32_t pktSize = 1400;        //in Bytes. 1458 to prevent fragments
    uint32_t nPackets = 2000;
    
    uint32_t enbBotTrace = 0;
    float startTime = 0;
    float simDuration = 200;        //in seconds
    uint32_t cleanup_time = 2;
    nSources = 80;

    droppedPackets = 0;

    
    bool logging = false;

    std::string root_dir;
    std::string qsizeTrFileName;
    std::string cwndTrFileName;
    std::string droppedTrFileName;
    std::string bottleneckTxFileName;
    std::string throughputFileName;
    std::string destthroughputFileName;

    totalBytes.resize(1, 0);
    totalBytesDest.resize(nSources+1, 0);

    CommandLine cmd;
    cmd.AddValue ("bottleneckBandwidth", "Bottleneck bandwidth", bottleneckBandwidth);
    cmd.AddValue ("bottleneckDelay", "Bottleneck delay", bottleneckDelay);
    cmd.AddValue ("accessBandwidth", "Access link bandwidth", accessBandwidth);
    cmd.AddValue ("RTT", "mean RTT for random generation of delays in each link", RTT);
    cmd.AddValue ("accessDelay", "Access link delay", accessDelay);
    cmd.AddValue ("queueType", "Queue type: DropTail, CoDel", queueType);
    cmd.AddValue ("queueSize", "Queue size in packets", queueSize);
    cmd.AddValue ("pktSize", "Packet size in bytes", pktSize); 
    cmd.AddValue ("startTime", "Simulation start time", startTime);
    cmd.AddValue ("simDuration", "Simulation duration in seconds", simDuration);
    cmd.AddValue ("cwndTrFileName", "Name of cwnd trace file", cwndTrFileName);
    cmd.AddValue ("logging", "Flag to enable/disable logging", logging);
    cmd.AddValue ("flavour", "Flavour to be used like - TcpBic, TcpBbr etc.", flavour);
    cmd.AddValue ("nPackets", "No. of Packets to send", nPackets);
    cmd.AddValue ("nSources", "No. of sources in the dumbell topology", nSources);
    cmd.AddValue ("sourceRate", "Rate of generating packets at the application level", sourceRate);
    cmd.AddValue ("enableBottleneckTrace", "Tracing the bottleneck packets", enbBotTrace);
    cmd.Parse (argc, argv);

    root_dir = "/mnt/Store/Project-summer/runtime/"+flavour+"/"+RTT+"/"+std::to_string(nSources)+"/";
    qsizeTrFileName = root_dir + "qsizeTrace";
    cwndTrFileName = root_dir + "cwndDropTail";
    droppedTrFileName = root_dir + "droppedPacketTrace";
    bottleneckTxFileName = root_dir + "bottleneckTx";
    throughputFileName = root_dir + "throughput";
    destthroughputFileName = root_dir + "dest_throughput";

    throughputvstime.resize(1, std::vector<double>(simDuration+1));
    throughputvstimeDest.resize(nSources+1, std::vector<double>(simDuration+1));

    cwnd.resize(nSources);
    cwnd_streams.resize(nSources);

    float stopTime = startTime + simDuration;
   
    std::string tcpModel ("ns3::"+flavour);
    
    Config::SetDefault ("ns3::TcpL4Protocol::SocketType", StringValue(tcpModel));
    // Configuring the packet size
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue (pktSize));

    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue (100));
    
    //Config::SetDefault("ns3::TcpSocketBase::MaxWindowSize", UintegerValue (20*1000));
    
    
    // Defining the nodes 
    NodeContainer nodes;
    nodes.Create (2+nSources*2);
    // Source nodes
    NodeContainer sr1 [nSources];
    // Destination nodes
    NodeContainer dr2 [nSources];

    NodeContainer r1r2 = NodeContainer(nodes.Get(0), nodes.Get(1));

    for( uint32_t i = 0; i< nSources ; i++){
        sr1[i] = NodeContainer(nodes.Get(i+2), nodes.Get(0));
        dr2[i] = NodeContainer(nodes.Get(2+nSources+i), nodes.Get(1));
    }
   

    // Defining the links to be used between nodes
    double min = 0.0;
    double max = double(2*stoi(RTT.substr(0, RTT.length()-2)));
    
    Ptr<UniformRandomVariable> x = CreateObject<UniformRandomVariable> ();
    x->SetAttribute ("Min", DoubleValue (min));
    x->SetAttribute ("Max", DoubleValue (max));

    PointToPointHelper bottleneck;
    bottleneck.SetDeviceAttribute("DataRate", StringValue(bottleneckBandwidth));
    bottleneck.SetChannelAttribute("Delay", StringValue(bottleneckDelay));
    bottleneck.SetQueue ("ns3::DropTailQueue<Packet>", "MaxSize", QueueSizeValue (QueueSize (queueSize))); // p in 1000p stands for packets
    bottleneck.DisableFlowControl();

    PointToPointHelper p2p_s[nSources];
    PointToPointHelper p2p_d[nSources];
    for (uint32_t i = 0; i < nSources; i++)
    {
        double delay = (x->GetValue())/2;
        //std::cout << delay*2 << std::endl;
        std::string delay_str = std::to_string(delay) + "ms";
        p2p_s[i].SetDeviceAttribute ("DataRate", StringValue(accessBandwidth));
        p2p_s[i].SetChannelAttribute ("Delay", StringValue(delay_str));
        p2p_s[i].SetQueue ("ns3::DropTailQueue<Packet>", "MaxSize", QueueSizeValue (QueueSize (std::to_string(0/nSources)+"p"))); // p in 1000p stands for packets
        p2p_s[i].DisableFlowControl();
        
        p2p_d[i].SetDeviceAttribute ("DataRate", StringValue(accessBandwidth));
        p2p_d[i].SetChannelAttribute ("Delay", StringValue(delay_str));
        p2p_d[i].SetQueue ("ns3::DropTailQueue<Packet>", "MaxSize", QueueSizeValue (QueueSize (std::to_string(0/nSources)+"p"))); // p in 1000p stands for packets
        p2p_d[i].DisableFlowControl();
    }
    
  
    
    
    // Consists of both the link and nodes
    NetDeviceContainer r1_r2 = bottleneck.Install(r1r2);
    
    NetDeviceContainer s_r1[nSources];
    NetDeviceContainer d_r2[nSources];

    
    for( uint32_t i = 0; i<nSources; i++){
        s_r1[i] = p2p_s[i].Install(sr1[i]);
        d_r2[i] = p2p_d[i].Install(dr2[i]);
    }

     
    // //Adding Error into the  network 
    // Ptr<RateErrorModel> em = CreateObject<RateErrorModel> ();
    // em->SetAttribute("ErrorRate", DoubleValue(0.00001));
    
    // r1_r2.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(em));

    // for (uint32_t i = 0; i < nSources; i++)
    // {
    //     s_r1[i].Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(em));
    //     d_r2[i].Get(0)->SetAttribute("ReceiveErrorModel", PointerValue(em));
    // }
       

    //  Assigning ip address to each node.
    InternetStackHelper stack;
    stack.Install(nodes);
    Ipv4AddressHelper address;

    

    Ipv4InterfaceContainer ip_s_r1 [nSources] ;
    Ipv4InterfaceContainer ip_d_r2 [nSources] ;

    for (uint32_t i = 0; i < nSources; i++)
    {   
        
        std::string ip = "10.1."+std::to_string(i+2)+".0";
        address.SetBase(ip.c_str(), "255.255.255.0");
        ip_s_r1[i] = address.Assign(s_r1[i]);

        std::string ip2 = "10.1."+std::to_string(i+2+nSources)+".0";
        address.SetBase(ip2.c_str(), "255.255.255.0");
        ip_d_r2[i] = address.Assign(d_r2[i]);
    }
    
    address.SetBase("10.1.1.0","255.255.255.0");
    address.Assign(r1_r2);

    

    // Attaching sink to nodes
    uint16_t sinkPort = 8080;
    PacketSinkHelper packetSinkHelper ("ns3::TcpSocketFactory", InetSocketAddress( Ipv4Address::GetAny(), sinkPort));

    Address sinkAddress[nSources];
    ApplicationContainer sinkApp[nSources];

    for (uint32_t i = 0; i < nSources; i++)
    {
        sinkAddress[i] = *(new Address(InetSocketAddress(ip_d_r2[i].GetAddress(0), sinkPort)));
        sinkApp[i] = packetSinkHelper.Install(nodes.Get(2+nSources+i));
        sinkApp[i].Start(Seconds(startTime));
        sinkApp[i].Stop(Seconds(stopTime));
    }
    
    Ptr<Socket> ns3TcpSocket[nSources];
    ApplicationContainer sourceApps[nSources];

    double mean = 0.1;   // more like a ~ 0.06
    double bound = 1;
    Ptr<ExponentialRandomVariable> expRandomVariable = CreateObject<ExponentialRandomVariable> ();
    expRandomVariable->SetAttribute ("Mean", DoubleValue (mean));
    expRandomVariable->SetAttribute ("Bound", DoubleValue (bound));

    double stime = startTime;
    // Configuring the application at each source node.
    for (uint32_t i = 0; i < nSources; i++)
    {
        
        BulkSendHelper tmp_source("ns3::TcpSocketFactory",sinkAddress[i]);
           
        // Set the amount of data to send in bytes.  Zero is unlimited.
        tmp_source.SetAttribute ("MaxBytes", UintegerValue (0));
        sourceApps[i] = tmp_source.Install (nodes.Get (2+i));
        
        sourceApps[i].Start (Seconds (stime));
        sourceApps[i].Stop (Seconds (stopTime));
        double gap = expRandomVariable->GetValue();

        stime += gap;
        
        // std::cout << gap << std::endl;

    }
    
    // Configuring file streams to write the congestion windows sizes to.
    AsciiTraceHelper ascii[nSources];
    for( uint32_t i = 0; i < nSources; i++){
        cwnd_streams[i] = ascii[i].CreateFileStream (cwndTrFileName+"_"+std::to_string(i)+".txt");
    }
    
    // Configuring file streams to write the throughput
    AsciiTraceHelper ascii_th;
    throughputvstime_stream = ascii_th.CreateFileStream (throughputFileName+".txt");


    AsciiTraceHelper ascii_th_dest;
    throughputvstime_streamDest = ascii_th_dest.CreateFileStream (destthroughputFileName+".txt");

    // Configuring file stream to write the Qsize
    AsciiTraceHelper ascii_qsize;
    qSize_stream = ascii_qsize.CreateFileStream(qsizeTrFileName+".txt");


    // Configuring file stream to write the no of packets transmitted by the bottleneck
    AsciiTraceHelper ascii_qsize_tx;
    bottleneckTransimittedStream = ascii_qsize_tx.CreateFileStream(bottleneckTxFileName+".txt");


    AsciiTraceHelper ascii_dropped;
    dropped_stream = ascii_dropped.CreateFileStream (droppedTrFileName + ".txt");

    //std::cout << stime << std::endl;
    // start tracing the congestion window size and qSize
    Simulator::Schedule( Seconds(stime), &startTracingCwnd, nSources);
    Simulator::Schedule( Seconds(stime), &startTracingThroughput, simDuration);
    Simulator::Schedule( Seconds(stime), &startTracingThroughputDest, simDuration);
    Simulator::Schedule( Seconds(stime), &StartTracingQueueSize);
    Simulator::Schedule( Seconds(stime), &StartTracingTransmitedPacket);

    // start tracing Queue Size and Dropped Files

    Simulator::Schedule( Seconds(stime), &TraceDroppedPacket, droppedTrFileName);
    

    // writing the congestion windows size, queueSize, packetTx to files periodically ( 1 sec. )
    for (uint32_t time = stime; time < stopTime; time++)
    {   
        Simulator::Schedule( Seconds(time), &writeCwndToFile, nSources);
        Simulator::Schedule( Seconds(time), &TraceQueueSize);
        Simulator::Schedule( Seconds(time), &TraceBottleneckTx);
        Simulator::Schedule( Seconds(time), &TraceDroppedPkts);
        Simulator::Schedule( Seconds(time), &ThroughputCallback);
        Simulator::Schedule( Seconds(time), &DestThroughputCallback);

    }
    
    
    if ( enbBotTrace == 1 ){
        AsciiTraceHelper bottleneck_ascii;
        bottleneck.EnableAscii(bottleneck_ascii.CreateFileStream ("bottleneck.tr"), s_r1[0]);
    }
       


    Simulator::Stop (Seconds (stopTime+cleanup_time));
    Ipv4GlobalRoutingHelper::PopulateRoutingTables ();



    // AnimationInterface anim("tcp_ftp_n.xml");
    // anim.SetMobilityPollInterval (Seconds (1));
    // if ( nSources == 2){
    //       // // X (->) , Y(|) 
    //     // // source nodes
    //     anim.SetConstantPosition(nodes.Get(2), 10.0, 20.0);
    //     anim.SetConstantPosition(nodes.Get(3), 10.0, 40.0);
    //     // // // router nodes
    //     anim.SetConstantPosition(nodes.Get(0), 30.0, 30.0);
    //     anim.SetConstantPosition(nodes.Get(1), 50.0, 30.0);
    //     // // // sink nodes
    //     anim.SetConstantPosition(nodes.Get(4), 70.0, 20.0);
    //     anim.SetConstantPosition(nodes.Get(5), 70.0, 40.0);
    // }
    // anim.SetMaxPktsPerTraceFile(50000000000);
    
        
    // Enable flow monitor
    Ptr<FlowMonitor> monitor;
    FlowMonitorHelper flowHelper;
    monitor = flowHelper.InstallAll();
    
    
    Simulator::Run ();

    //  // Print the statistics for each flow
    // monitor->CheckForLostPackets();
    // FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats();
    // for (auto it = stats.begin(); it != stats.end(); ++it) {
    //     Ipv4FlowClassifier classifier;
    //     // FlowId flowId = it->first;

    //     uint64_t txBytes = it->second.txBytes;
    //     uint64_t rxBytes = it->second.rxBytes;
    //     double throughput = (rxBytes * 8.0) / (it->second.timeLastRxPacket.GetSeconds() - it->second.timeFirstTxPacket.GetSeconds()) / 1000000.0;


    //     std::cout << "  Tx bytes = " << txBytes << std::endl;
    //     std::cout << "  Rx bytes = " << rxBytes << std::endl;
    //     std::cout << "  Throughput = " << throughput << " Mbps."<< std::endl;
    // }

    Simulator::Destroy ();

    writeThroughputToFile(simDuration);
    writeThroughputToFileDest(simDuration);
    return 0;

}