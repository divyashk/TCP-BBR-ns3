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
uint64_t queueSize[3];
Ptr<OutputStreamWrapper> qSize_stream[3];

uint64_t bottleneckTransimitted;
Ptr<OutputStreamWrapper> bottleneckTransimittedStream;

uint64_t droppedPackets;
Ptr<OutputStreamWrapper> dropped_stream;

static void
plotCwndChange (uint32_t pos, uint32_t oldCwnd, uint32_t newCwnd){
    cwnd[pos] = newCwnd;
}

static void  
startTracingCwnd(uint32_t nSources){
    for( uint32_t i = 0; i < nSources; i++){
        //Trace changes to the congestion window
        Config::ConnectWithoutContext ("/NodeList/"+std::to_string(i+6)+"/$ns3::TcpL4Protocol/SocketList/0/CongestionWindow", MakeBoundCallback (&plotCwndChange, i));
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
plotQsizeChange (int index, uint32_t oldQSize, uint32_t newQSize){
    // NS_LOG_UNCOND("Queue: "<< index << "\t" << newQSize);
    queueSize[index] = newQSize;    
    
    // log in the file after every change( will increase the run time and computation )
    *qSize_stream[index]->GetStream() << Simulator::Now().GetSeconds() << "\t" << queueSize[index] << std::endl;
}

static void
TraceQueueSizeA(){
    *qSize_stream[0]->GetStream() << Simulator::Now().GetSeconds() << "\t" << queueSize[0] << std::endl;
}

static void
TraceQueueSizeB(){
    *qSize_stream[1]->GetStream() << Simulator::Now().GetSeconds() << "\t" << queueSize[1] << std::endl;
}

static void
TraceQueueSizeC(){
    *qSize_stream[2]->GetStream() << Simulator::Now().GetSeconds() << "\t" << queueSize[2] << std::endl;
}

static void
StartTracingQueueSize(){
    for(int i = 0; i<3; i++)
        Config::ConnectWithoutContext("/NodeList/"+std::to_string(i*2)+"/DeviceList/0/$ns3::PointToPointNetDevice/TxQueue/PacketsInQueue", MakeBoundCallback(&plotQsizeChange, i));
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
StartTracingTransmitedPacket(){
    bottleneckTransimitted = 0;
    Config::ConnectWithoutContext("/NodeList/0/DeviceList/0/$ns3::PointToPointNetDevice/PhyTxEnd", MakeCallback(&TxPacket));
}

int getBandwidth(std::string Bandwidth){
    return stoi(Bandwidth.substr(0, Bandwidth.length()-4));
}

int getDelay(std::string Delay){
    return stoi(Delay.substr(0, Delay.length()-2));
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
    std::string RTT = "98ms";   
    std::string sourceRate = "120Mbps";
    std::string flavour = "TcpBbr";
    std::string queueType = "DropTail";       //DropTail or CoDel
    std::string queueSize = "2084p";      //in packets

    std::string bottleneckBandwidthA = "100Mbps";
    std::string bottleneckBandwidthB = "100Mbps";
    std::string bottleneckBandwidthC = "200Mbps";

    uint32_t pktSize = 1400;        //in Bytes. 1458 to prevent fragments
    uint32_t nPackets = 2000;
    
    uint32_t enbBotTrace = 0;
    float startTime = 0;
    float simDuration = 200;        //in seconds
    uint32_t cleanup_time = 2;
    uint32_t nSources = 2;

    droppedPackets = 0;

    
    bool logging = false;

    std::string root_dir;
    std::string qsizeTrFileName[3];
    std::string cwndTrFileName;
    std::string droppedTrFileName;
    std::string bottleneckTxFileName;

    CommandLine cmd;
    cmd.AddValue ("bottleneckBandwidthA", "Bottleneck bandwidth A", bottleneckBandwidth);
    cmd.AddValue ("bottleneckBandwidthB", "Bottleneck bandwidth B", bottleneckBandwidth);
    cmd.AddValue ("bottleneckBandwidthC", "Bottleneck bandwidth C", bottleneckBandwidth);
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


    for (int i = 0; i<3 ; i++)
        qsizeTrFileName[i] = root_dir + "qsizeTrace_" + std::to_string(i);

    cwndTrFileName = root_dir + "cwndDropTail";
    droppedTrFileName = root_dir + "droppedPacketTrace";
    bottleneckTxFileName = root_dir + "bottleneckTx";

    cwnd.resize(nSources);
    cwnd_streams.resize(nSources);

    float stopTime = startTime + simDuration;
   
    std::string tcpModel ("ns3::"+flavour);
    
    Config::SetDefault ("ns3::TcpL4Protocol::SocketType", StringValue(tcpModel));
    // Configuring the packet size
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue (pktSize));

    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue (100));
    
    //Config::SetDefault("ns3::TcpSocketBase::MaxWindowSize", UintegerValue (20*1000));
    

    std::string mergeBandwidthAC = std::to_string(10*getBandwidth(bottleneckBandwidthA)) + "Mbps";
    std::string mergeBandwidthBC = std::to_string(10*getBandwidth(bottleneckBandwidthB)) + "Mbps";
    
    // Defining the nodes 
    NodeContainer nodes;
    nodes.Create (6+nSources*2);

    // Source nodes
    NodeContainer NC_sra1 [nSources/2];
    NodeContainer NC_srb1 [nSources - (nSources/2)];

    // Destination nodes
    NodeContainer NC_drc2 [nSources];


    NodeContainer NC_ra1ra2 = NodeContainer(nodes.Get(0), nodes.Get(1));
    NodeContainer NC_rb1rb2 = NodeContainer(nodes.Get(2), nodes.Get(3));
    
    // merging
    NodeContainer NC_ra2rc1 = NodeContainer(nodes.Get(1), nodes.Get(4));
    NodeContainer NC_rb2rc1 = NodeContainer(nodes.Get(3), nodes.Get(4));

    NodeContainer NC_rc1rc2 = NodeContainer(nodes.Get(4), nodes.Get(5));

    int a_pos = 0;
    int b_pos = 0;

    for( uint32_t i = 0; i< nSources ; i++){
        if ( i <= nSources/2-1)
            NC_sra1[a_pos++] = NodeContainer(nodes.Get(i+6), nodes.Get(0));
        else
            NC_srb1[b_pos++] = NodeContainer(nodes.Get(i+6), nodes.Get(2));

        NC_drc2[i] = NodeContainer(nodes.Get(6+nSources+i), nodes.Get(5));
    }
   

    // Defining the links to be used between nodes
    double min = 0.0;
    double max = double(2*stoi(RTT.substr(0, RTT.length()-2)));
    
    Ptr<UniformRandomVariable> x = CreateObject<UniformRandomVariable> ();
    x->SetAttribute ("Min", DoubleValue (min));
    x->SetAttribute ("Max", DoubleValue (max));

    PointToPointHelper bottleneck_a;
    bottleneck_a.SetDeviceAttribute("DataRate", StringValue(bottleneckBandwidthA));
    bottleneck_a.SetChannelAttribute("Delay", StringValue(bottleneckDelay));
    bottleneck_a.SetQueue ("ns3::DropTailQueue<Packet>", "MaxSize", QueueSizeValue (QueueSize (queueSize))); // p in 1000p stands for packets
    bottleneck_a.DisableFlowControl();

    PointToPointHelper bottleneck_b;
    bottleneck_b.SetDeviceAttribute("DataRate", StringValue(bottleneckBandwidthB));
    bottleneck_b.SetChannelAttribute("Delay", StringValue(bottleneckDelay));
    bottleneck_b.SetQueue ("ns3::DropTailQueue<Packet>", "MaxSize", QueueSizeValue (QueueSize (queueSize))); // p in 1000p stands for packets
    bottleneck_b.DisableFlowControl();

    PointToPointHelper bottleneck_c;
    bottleneck_c.SetDeviceAttribute("DataRate", StringValue(bottleneckBandwidthC));
    bottleneck_c.SetChannelAttribute("Delay", StringValue(bottleneckDelay));
    bottleneck_c.SetQueue ("ns3::DropTailQueue<Packet>", "MaxSize", QueueSizeValue (QueueSize (queueSize))); // p in 1000p stands for packets
    bottleneck_c.DisableFlowControl();

    PointToPointHelper merge_ac;
    merge_ac.SetDeviceAttribute("DataRate", StringValue(mergeBandwidthAC));
    merge_ac.SetChannelAttribute("Delay", StringValue(bottleneckDelay));
    merge_ac.SetQueue ("ns3::DropTailQueue<Packet>", "MaxSize", QueueSizeValue (QueueSize (std::to_string(0)+"p"))); // p in 1000p stands for packets
    merge_ac.DisableFlowControl();

    PointToPointHelper merge_bc;
    merge_bc.SetDeviceAttribute("DataRate", StringValue(mergeBandwidthBC));
    merge_bc.SetChannelAttribute("Delay", StringValue(bottleneckDelay));
    merge_bc.SetQueue ("ns3::DropTailQueue<Packet>", "MaxSize", QueueSizeValue (QueueSize (std::to_string(0)+"p"))); // p in 1000p stands for packets
    merge_bc.DisableFlowControl();

    PointToPointHelper p2p_sa[nSources/2];
    PointToPointHelper p2p_sb[nSources - nSources/2];
    PointToPointHelper p2p_d[nSources];

    a_pos = 0;
    b_pos = 0;

    for (uint32_t i = 0; i < nSources; i++)
    {
        double delay = (x->GetValue())/2;
        //std::cout << delay*2 << std::endl;
        std::string total_delay_str = std::to_string(delay) + "ms";
        std::string delay_per_access_link = std::to_string((delay-4*getDelay(bottleneckDelay))/6) + "ms";

        if ( i <= nSources/2-1 ){
            p2p_sa[a_pos].SetDeviceAttribute ("DataRate", StringValue(accessBandwidth));
            p2p_sa[a_pos].SetChannelAttribute ("Delay", StringValue(delay_per_access_link));
            p2p_sa[a_pos].SetQueue ("ns3::DropTailQueue<Packet>", "MaxSize", QueueSizeValue (QueueSize (std::to_string(0)+"p"))); // p in 1000p stands for packets
            p2p_sa[a_pos].DisableFlowControl();
            a_pos++;
        }
        else{
            p2p_sb[b_pos].SetDeviceAttribute ("DataRate", StringValue(accessBandwidth));
            p2p_sb[b_pos].SetChannelAttribute ("Delay", StringValue(delay_per_access_link));
            p2p_sb[b_pos].SetQueue ("ns3::DropTailQueue<Packet>", "MaxSize", QueueSizeValue (QueueSize (std::to_string(0)+"p"))); // p in 1000p stands for packets
            p2p_sb[b_pos].DisableFlowControl();
            b_pos++;
        }
        
        p2p_d[i].SetDeviceAttribute ("DataRate", StringValue(accessBandwidth));
        p2p_d[i].SetChannelAttribute ("Delay", StringValue(delay_per_access_link));
        p2p_d[i].SetQueue ("ns3::DropTailQueue<Packet>", "MaxSize", QueueSizeValue (QueueSize (std::to_string(0/nSources)+"p"))); // p in 1000p stands for packets
        p2p_d[i].DisableFlowControl();
    }
    
  
    
    
    // Consists of both the link and nodes
    NetDeviceContainer NDC_ra1_ra2 = bottleneck_a.Install(NC_ra1ra2);
    NetDeviceContainer NDC_rb1_rb2 = bottleneck_b.Install(NC_rb1rb2);
    NetDeviceContainer NDC_rc1_rc2 = bottleneck_c.Install(NC_rc1rc2);

    NetDeviceContainer NDC_ra2_rc1 = merge_ac.Install(NC_ra2rc1);
    NetDeviceContainer NDC_rb2_rc1 = merge_bc.Install(NC_rb2rc1);


    NetDeviceContainer NDC_s_ra1[nSources/2];
    NetDeviceContainer NDC_s_rb1[nSources-nSources/2];

    NetDeviceContainer NDC_d_rc2[nSources];

    a_pos=0, b_pos = 0;
    for( uint32_t i = 0; i<nSources; i++){
        if ( i <= nSources/2-1 ){
            NDC_s_ra1[a_pos] = p2p_sa[a_pos].Install(NC_sra1[a_pos]);
            a_pos++;
        }
        else{
            NDC_s_rb1[b_pos] = p2p_sb[b_pos].Install(NC_srb1[b_pos]);
            b_pos++;
        }    
        NDC_d_rc2[i] = p2p_d[i].Install(NC_drc2[i]);
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

    Ipv4InterfaceContainer ip_s_ra1 [nSources/2] ;
    Ipv4InterfaceContainer ip_s_rb1 [nSources-nSources/2] ;

    Ipv4InterfaceContainer ip_ra2_rc1;
    Ipv4InterfaceContainer ip_rb2_rc1;
    
    
    Ipv4InterfaceContainer ip_d_rc2 [nSources] ;
    
    a_pos = 0;
    b_pos = 0;
    for (uint32_t i = 0; i < nSources; i++)
    {   
        
        std::string ip = "10.1."+std::to_string(i+6)+".0";
        address.SetBase(ip.c_str(), "255.255.255.0");

        if ( i <= nSources/2-1 ){
            ip_s_ra1[a_pos] = address.Assign(NDC_s_ra1[a_pos]);
            a_pos++;
        }
        else{
            ip_s_rb1[b_pos] = address.Assign(NDC_s_rb1[b_pos]);
            b_pos++;
        }
        std::string ip2 = "10.1."+std::to_string(i+6+nSources)+".0";
        address.SetBase(ip2.c_str(), "255.255.255.0");
        ip_d_rc2[i] = address.Assign(NDC_d_rc2[i]);
    }
    
    address.SetBase("10.1.1.0","255.255.255.0");
    address.Assign(NDC_ra1_ra2);
    
    address.SetBase("10.1.2.0","255.255.255.0");
    address.Assign(NDC_rb1_rb2);
    
    address.SetBase("10.1.3.0","255.255.255.0");
    address.Assign(NDC_rc1_rc2);

    address.SetBase("10.1.4.0","255.255.255.0");
    address.Assign(NDC_rb2_rc1);

    address.SetBase("10.1.5.0","255.255.255.0");
    address.Assign(NDC_ra2_rc1);


    // Attaching sink to nodes
    uint16_t sinkPort = 8080;
    PacketSinkHelper packetSinkHelper ("ns3::TcpSocketFactory", InetSocketAddress( Ipv4Address::GetAny(), sinkPort));

    Address sinkAddress[nSources];
    ApplicationContainer sinkApp[nSources];

    for (uint32_t i = 0; i < nSources; i++)
    {
        sinkAddress[i] = *(new Address(InetSocketAddress(ip_d_rc2[i].GetAddress(0), sinkPort)));
        sinkApp[i] = packetSinkHelper.Install(nodes.Get(6+nSources+i));
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
        sourceApps[i] = tmp_source.Install (nodes.Get(6+i));
        
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
    
    // Configuring file stream to write the Qsize
    AsciiTraceHelper ascii_qsize[3];

    for(int i = 0; i<3 ; i++)
        qSize_stream[i] = ascii_qsize[i].CreateFileStream(qsizeTrFileName[i]+".txt");


    // Configuring file stream to write the no of packets transmitted by the bottleneck
    AsciiTraceHelper ascii_tx;
    bottleneckTransimittedStream = ascii_tx.CreateFileStream(bottleneckTxFileName+".txt");


    AsciiTraceHelper ascii_dropped;
    dropped_stream = ascii_dropped.CreateFileStream (droppedTrFileName + ".txt");

    //std::cout << stime << std::endl;
    // start tracing the congestion window size and qSize
    Simulator::Schedule( Seconds(stime), &startTracingCwnd, nSources);
    Simulator::Schedule( Seconds(stime), &StartTracingQueueSize);
    Simulator::Schedule( Seconds(stime), &StartTracingTransmitedPacket);

    // start tracing Queue Size and Dropped Files

    Simulator::Schedule( Seconds(stime), &TraceDroppedPacket, droppedTrFileName);
    

    // writing the congestion windows size, queueSize, packetTx to files periodically ( 1 sec. )
    for (uint32_t time = stime; time < stopTime; time++)
    {   
        Simulator::Schedule( Seconds(time), &writeCwndToFile, nSources);

        Simulator::Schedule( Seconds(time), &TraceQueueSizeA);
        Simulator::Schedule( Seconds(time), &TraceQueueSizeB);
        Simulator::Schedule( Seconds(time), &TraceQueueSizeC);

        Simulator::Schedule( Seconds(time), &TraceBottleneckTx);
        Simulator::Schedule( Seconds(time), &TraceDroppedPkts);
    }
    
    
    if ( enbBotTrace == 1 ){
        AsciiTraceHelper bottleneck_ascii;
        // Only tracing the bottleneck A
        std::cout << "ONLY TRACING THE BOTTLENECK A " << std::endl;
        bottleneck_a.EnableAscii(bottleneck_ascii.CreateFileStream ("bottleneck.tr"), NDC_s_ra1[0]);
    }
       


    Simulator::Stop (Seconds (stopTime+cleanup_time));
    Ipv4GlobalRoutingHelper::PopulateRoutingTables ();



    // AnimationInterface anim("tcp_ftp_n_bg.xml");
    // if ( nSources == 2){
    //       // // X (->) , Y(|) 

    //     anim.SetConstantPosition(nodes.Get(6), 0.5, 15.0);
    //     anim.SetConstantPosition(nodes.Get(7), 0.5, 55.0); 
       
    //     anim.SetConstantPosition(nodes.Get(0), 11.0, 20.0);
    //     anim.SetConstantPosition(nodes.Get(1), 18.0, 20.0);
        
    //     anim.SetConstantPosition(nodes.Get(2), 11.0, 45.0);
    //     anim.SetConstantPosition(nodes.Get(3), 18.0, 45.0);
        
    //     anim.SetConstantPosition(nodes.Get(4), 30.0, 31.0);
    //     anim.SetConstantPosition(nodes.Get(5), 43.0, 31.0);

    //     anim.SetConstantPosition(nodes.Get(8), 62.0, 16.0);
    //     anim.SetConstantPosition(nodes.Get(9), 60.0, 41.0);
    // }
    // anim.SetMaxPktsPerTraceFile(50000000000);
    
        
    // Enable flow monitor
    Ptr<FlowMonitor> monitor;
    FlowMonitorHelper flowHelper;
    monitor = flowHelper.InstallAll();
    
    
    Simulator::Run ();

     // Print the statistics for each flow
    monitor->CheckForLostPackets();
    FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats();
    for (auto it = stats.begin(); it != stats.end(); ++it) {
        Ipv4FlowClassifier classifier;
        
        uint64_t txBytes = it->second.txBytes;
        uint64_t rxBytes = it->second.rxBytes;
        double throughput = (rxBytes * 8.0) / (it->second.timeLastRxPacket.GetSeconds() - it->second.timeFirstTxPacket.GetSeconds()) / 1000000.0;


        std::cout << "  Tx bytes = " << txBytes << std::endl;
        std::cout << "  Rx bytes = " << rxBytes << std::endl;
        std::cout << "  Throughput = " << throughput << " Mbps."<< std::endl;
    }

    Simulator::Destroy ();

    return 0;

}