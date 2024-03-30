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
Ptr<OutputStreamWrapper> qsize_stream;

Ptr<FlowMonitor> flowMonitor;
FlowMonitorHelper flowHelper;

uint64_t bottleneckTransimitted;
Ptr<OutputStreamWrapper> btlnck_transmitted_stream;

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
TraceQueueSize(){
    *qsize_stream->GetStream() << Simulator::Now().GetSeconds() << "\t" << queueSize << std::endl;
}


static void
StartTracingQueueSize(){
    Config::ConnectWithoutContext("/NodeList/0/DeviceList/0/$ns3::PointToPointNetDevice/TxQueue/PacketsInQueue", MakeCallback(&plotQsizeChange));
}


/// BOTTLENECK THROUGHPUT
//Store Throughput for bottleneck to file

std::vector<std::vector<double>> btlnck_throughputvstime;
Ptr<OutputStreamWrapper> btlnck_throughputvstime_stream;

static void
writeBottleneckThroughputToFile (uint32_t simDurationInSeconds)
{   
    // Write header: RouterID t0 t1 t2 ... tN
    *btlnck_throughputvstime_stream->GetStream ()  << "RouterID\t";
    for (uint32_t t = 0; t < simDurationInSeconds; ++t) {
        *btlnck_throughputvstime_stream->GetStream ()  << t << "\t";
    }
    *btlnck_throughputvstime_stream->GetStream ()  << "\n";

    // Write throughput values for each router
    for (uint32_t i = 0; i < 1; ++i) {
        *btlnck_throughputvstime_stream->GetStream ()  << i << "\t";
        for (uint32_t t = 0; t < simDurationInSeconds; ++t) {
            *btlnck_throughputvstime_stream->GetStream ()  << btlnck_throughputvstime[i][t] << "\t";
        }
        *btlnck_throughputvstime_stream->GetStream ()  << "\n";
    }
}


// This is called after DestThroughputCallback every second
void update_throughput_bottleneck(int64_t bottleneck_rx_bytes) {
    static Time lastTime = Simulator::Now();
    static int64_t prev_Rx_bottleneck = 0;
    Time currentTime = Simulator::Now();
    Time timeDiff = currentTime - lastTime;
    btlnck_throughputvstime[0][Simulator::Now().GetSeconds()] = (((bottleneck_rx_bytes-prev_Rx_bottleneck) * 8.0 / timeDiff.GetSeconds())/1024)/1024; // in Mbps
    //Reset the size of the transmitted packets
    lastTime = currentTime;
    prev_Rx_bottleneck = bottleneck_rx_bytes;
}


/// DESTINATION THROUGHPUT

std::vector<uint32_t> totalBytesDest;
std::vector<std::vector<double>> dest_throughputvstime;
Ptr<OutputStreamWrapper> dest_throughputvstime_stream;

static void
writeThroughputToFileDest (uint32_t simDurationInSeconds)
{   
    // Write header: DestID t0 t1 t2 ... tN-1
    *dest_throughputvstime_stream->GetStream ()  << "DestID\t";
    for (uint32_t t = 0; t < simDurationInSeconds; ++t) {
        *dest_throughputvstime_stream->GetStream ()  << t << "\t";
    }
    *dest_throughputvstime_stream->GetStream ()  << "\n";

    // Write throughput values for each router
    for (uint32_t i = 0; i < nSources; ++i) {
        *dest_throughputvstime_stream->GetStream ()  <<  i << "\t";
        for (uint32_t t = 0; t < simDurationInSeconds; ++t) {
            *dest_throughputvstime_stream->GetStream ()  << dest_throughputvstime[i][t] << "\t";
        }
        *dest_throughputvstime_stream->GetStream ()  << "\n";
    }
}

// This is called every second
void DestThroughputCallback() {
    static Time lastTime = Simulator::Now();
    
    // Get flow statistics
    flowMonitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());
    FlowMonitor::FlowStatsContainer stats = flowMonitor->GetFlowStats();

    Time currentTime = Simulator::Now();
    Time timeDiff = currentTime - lastTime;
    int128_t bottleneck_rxBytes = 0;
    // Iterate through flows and print throughput for the point-to-point device
    for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator flow = stats.begin(); flow != stats.end(); ++flow) {
        // Ignore the ACK flow from sink to source
        if ( flow->first % 2 == 0) continue;

        
        int32_t i = (flow->first-1)/2;
        int64_t diff_bytes = flow->second.rxBytes - totalBytesDest[i];
        dest_throughputvstime[i][Simulator::Now().GetSeconds()] = (((diff_bytes ) * 8.0 / timeDiff.GetSeconds())/1024)/1024; // in Mbps
        
        // Save the rxBytes till now
        totalBytesDest[i] = flow->second.rxBytes;

        // Update the bottleneck rX Bytes for bottleneck 
        bottleneck_rxBytes += flow->second.rxBytes;
    }

    // Update the throughput for the bottleneck 

    update_throughput_bottleneck(bottleneck_rxBytes);
    
    lastTime = currentTime;
}

std::vector<std::string> stringToArray(std::string &str, char sep){
    std::vector<std::string> result;
    std::istringstream iss(str);
    std::string token;
    
    while (std::getline(iss, token, sep)) {
        result.push_back(token);
    }
    
    return result;
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
    std::string RTTs = "10ms_100ms_200ms";   
    std::string sourceRate = "120Mbps";
    std::string Flavours = "TcpBbr_TcpCubic";
    std::string queueType = "DropTail";       //DropTail or CoDel
    std::string queueSize = "2084p";      //in packets
    
   
    uint32_t pktSize = 1400;        //in Bytes. 1458 to prevent fragments
    uint32_t nPackets = 2000;
    
    uint32_t enbBotTrace = 0;
    float startTime = 0;
    float simDuration = 200;        //in seconds
    uint32_t cleanup_time = 2;
    nSources = 80;

    
    bool logging = false;

    std::string root_dir;
    std::string qsizeTrFileName;
    std::string cwndTrFileName;
    std::string droppedTrFileName;
    std::string bottleneckTxFileName;
    std::string throughputFileName;
    std::string destthroughputFileName;

    totalBytesDest.resize(nSources+1, 0);

    CommandLine cmd;
    cmd.AddValue ("bottleneckBandwidth", "Bottleneck bandwidth", bottleneckBandwidth);
    cmd.AddValue ("bottleneckDelay", "Bottleneck delay", bottleneckDelay);
    cmd.AddValue ("accessBandwidth", "Access link bandwidth", accessBandwidth);
    cmd.AddValue ("RTTs", "mean RTTs for random generation of delays in each link", RTTs);
    cmd.AddValue ("accessDelay", "Access link delay", accessDelay);
    cmd.AddValue ("queueType", "Queue type: DropTail, CoDel", queueType);
    cmd.AddValue ("queueSize", "Queue size in packets", queueSize);
    cmd.AddValue ("pktSize", "Packet size in bytes", pktSize); 
    cmd.AddValue ("startTime", "Simulation start time", startTime);
    cmd.AddValue ("simDuration", "Simulation duration in seconds", simDuration);
    cmd.AddValue ("cwndTrFileName", "Name of cwnd trace file", cwndTrFileName);
    cmd.AddValue ("logging", "Flag to enable/disable logging", logging);
    cmd.AddValue ("Flavours", "Flavours to be used like - TcpBic, TcpBbr etc.", Flavours);
    cmd.AddValue ("nPackets", "No. of Packets to send", nPackets);
    cmd.AddValue ("nSources", "No. of sources in the dumbell topology", nSources);
    cmd.AddValue ("sourceRate", "Rate of generating packets at the application level", sourceRate);
    cmd.AddValue ("enableBottleneckTrace", "Tracing the bottleneck packets", enbBotTrace);
    cmd.Parse (argc, argv);
    
    root_dir = "/mnt/Store/Project-summer/runtime/"+Flavours+"/"+RTTs+"/"+std::to_string(nSources)+"/";
    qsizeTrFileName = root_dir + "qsizeTrace";
    cwndTrFileName = root_dir + "cwndDropTail";
    droppedTrFileName = root_dir + "droppedPacketTrace";
    bottleneckTxFileName = root_dir + "bottleneckTx";
    throughputFileName = root_dir + "throughput";
    destthroughputFileName = root_dir + "dest_throughput";

    btlnck_throughputvstime.resize(1, std::vector<double>(simDuration+1));
    dest_throughputvstime.resize(nSources+1, std::vector<double>(simDuration+1));

    cwnd.resize(nSources);
    cwnd_streams.resize(nSources);

    float stopTime = startTime + simDuration;
   
    std::string tcpModel ("ns3::"+Flavours);


    // Converting the string RTTs to an array of rtts
    std::vector<std::string> RTTs_array = stringToArray(RTTs, '_');
    int noOfFlowWithSimilarRTTs = nSources/RTTs_array.size();

    // Converting the string Flavours to an array of flavours
    std::vector<std::string> Flavours_array = stringToArray(Flavours, '_');
    int noOfFlowWithSimilarFlavours = nSources/Flavours_array.size();

    if (Flavours_array.size() > 1 && RTTs_array.size() > 1){
        NS_LOG_ERROR("Can't have multiple RTTs and Flavours at the same time");
        return -1;
    }
    

    // Configuring the packet size
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue (pktSize));

    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue (100));
    
    //Config::SetDefault("ns3::TcpSocketBase::MaxWindowSize", UintegerValue (20*1000));
    //Config::SetDefault ("ns3::TcpL4Protocol::SocketType", StringValue(tcpModel));
    
    
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
    

    


    // Using uniform distribution to randomly generate RTTs corresponding with input RTTs
    std::vector<Ptr<UniformRandomVariable>> x;
    for ( uint64_t i = 0; i < RTTs_array.size(); i++){
        double min = 0.0;
        //std::cout << "RTT:" << RTTs_array[i] << std::endl;
        double max = double(2*stoi(RTTs_array[i].substr(0, RTTs_array[i].length()-2)));
        //std::cout << "max:" << max << std::endl;
        x.push_back(CreateObject<UniformRandomVariable> ());
        x.back()->SetAttribute ("Min", DoubleValue (min));
        x.back()->SetAttribute ("Max", DoubleValue (max));
    }

    // Defining the links to be used between nodes

    PointToPointHelper bottleneck;
    bottleneck.SetDeviceAttribute("DataRate", StringValue(bottleneckBandwidth));
    bottleneck.SetChannelAttribute("Delay", StringValue(bottleneckDelay));
    bottleneck.SetQueue ("ns3::DropTailQueue<Packet>", "MaxSize", QueueSizeValue (QueueSize (queueSize))); // p in 1000p stands for packets
    bottleneck.DisableFlowControl();

    PointToPointHelper p2p_s[nSources];
    PointToPointHelper p2p_d[nSources];
    for (uint32_t i = 0; i < nSources; i++)
    {
        // getting the appropriate delay value by using flow number
        double delay = (x[int(i/noOfFlowWithSimilarRTTs)]->GetValue())/4;
        
        //std::cout << std::to_string(i/noOfFlowWithSimilarRTTs) + ":" << delay * 4 << std::endl;
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
    

    // Adding time gap between start time of each flow using poisson distribution random variable
    double mean = 0.1;   // more like a ~ 0.06
    double bound = 1;
    Ptr<ExponentialRandomVariable> expRandomVariable = CreateObject<ExponentialRandomVariable> ();
    expRandomVariable->SetAttribute ("Mean", DoubleValue (mean));
    expRandomVariable->SetAttribute ("Bound", DoubleValue (bound));

    
    // Configuring the application at each source node and specifying tcp flavour
    double stime = startTime;
    Ptr<Socket> ns3TcpSocket[nSources];
    ApplicationContainer sourceApps[nSources];
    for (uint32_t i = 0; i < nSources; i++)
    {
        
        BulkSendHelper tmp_source("ns3::TcpSocketFactory",sinkAddress[i]);
        // Set the amount of data to send in bytes. 0 is unlimited.
        tmp_source.SetAttribute ("MaxBytes", UintegerValue (0));
        sourceApps[i] = tmp_source.Install (nodes.Get (2+i));

        // Configure the tcp flavour
        TypeId tid = TypeId::LookupByName("ns3::"+Flavours_array[i/noOfFlowWithSimilarFlavours]);
        std::stringstream nodeId;
        nodeId << nodes.Get(2+i)->GetId();
        std::string specificNode = "/NodeList/" + nodeId.str() + "/$ns3::TcpL4Protocol/SocketType";
        Config::Set(specificNode, TypeIdValue(tid));
        Ptr<Socket> localSocket =
        Socket::CreateSocket(nodes.Get(0), TcpSocketFactory::GetTypeId());

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
    btlnck_throughputvstime_stream = ascii_th.CreateFileStream (throughputFileName+".txt");

    // Configuring file streams to write the destination throughput
    AsciiTraceHelper ascii_th_dest;
    dest_throughputvstime_stream = ascii_th_dest.CreateFileStream (destthroughputFileName+".txt");

    // Configuring file stream to write the Qsize
    AsciiTraceHelper ascii_qsize;
    qsize_stream = ascii_qsize.CreateFileStream(qsizeTrFileName+".txt");


    // Configuring file stream to write the no of packets transmitted by the bottleneck
    AsciiTraceHelper ascii_qsize_tx;
    btlnck_transmitted_stream = ascii_qsize_tx.CreateFileStream(bottleneckTxFileName+".txt");

    // Configuring file streams to write the packets dropped 
    AsciiTraceHelper ascii_dropped;
    dropped_stream = ascii_dropped.CreateFileStream (droppedTrFileName + ".txt");


    // start tracing the congestion window size, qSize, dropped packets
    Simulator::Schedule( Seconds(stime), &startTracingCwnd, nSources);
    Simulator::Schedule( Seconds(stime), &StartTracingQueueSize);
    
    

    // writing the congestion windows size, queueSize, packetTx to files periodically ( 1 sec. )
    for (uint32_t time = stime; time < stopTime; time++)
    {   
        Simulator::Schedule( Seconds(time), &writeCwndToFile, nSources);
        Simulator::Schedule( Seconds(time), &TraceQueueSize);
        Simulator::Schedule( Seconds(time), &DestThroughputCallback);

    }
    
    
    if ( enbBotTrace == 1 ){
        AsciiTraceHelper bottleneck_ascii;
        bottleneck.EnableAscii(bottleneck_ascii.CreateFileStream ("bottleneck.tr"), s_r1[0]);
    }
       
    
    // Get the configuration path from the provided string
    std::string configPath = "/NodeList/1/DeviceList/0";


    Simulator::Stop (Seconds (stopTime+cleanup_time));
    Ipv4GlobalRoutingHelper::PopulateRoutingTables ();
    
    // Enable flow monitor
    flowMonitor = flowHelper.InstallAll();
    
    Simulator::Run ();


    
    Simulator::Destroy ();

    // Write the locally stored values onto a trace file at the end in a single go( decreases read-writes )
    writeBottleneckThroughputToFile(simDuration);
    writeThroughputToFileDest(simDuration);

    return 0;


}