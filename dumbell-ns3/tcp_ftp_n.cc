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
//#include "ns3/netanim-module.h"

#define MAX_SOURCES 100;

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TCPSCRIPT");

std::vector<uint32_t> cwnd;
std::vector<Ptr<OutputStreamWrapper>> cwnd_streams;
uint64_t queueSize;
Ptr<OutputStreamWrapper> qSize_stream;

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
   *stream->GetStream () << Simulator::Now().GetSeconds() << "\tRxDrop" << std::endl;
} 

static void
TraceDroppedPacket(std::string droppedTrFileName){
    // tracing all the dropped packets in a seperate file
    AsciiTraceHelper ascii;
    Ptr<OutputStreamWrapper> dropped_stream;
    dropped_stream = ascii.CreateFileStream (droppedTrFileName + ".txt");
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
StartTracingQueueSize(){
    Config::ConnectWithoutContext("/NodeList/0/DeviceList/0/$ns3::PointToPointNetDevice/TxQueue/PacketsInQueue", MakeCallback(&plotQsizeChange));
}

int 
main(int argc, char *argv[])
{   
    std::string bottleneckBandwidth = "100Mbps";
    std::string bottleneckDelay = "1ms";
    std::string accessBandwidth = "120Mbps";
    std::string accessDelay = "2.5ms";
    std::string RTT = "10ms";   
    std::string sourceRate = "120Mbps";
    std::string flavour = "TcpNewReno";
    std::string queueType = "DropTail";       //DropTail or CoDel
    std::string queueSize = "2084p";      //in packets
    uint32_t pktSize = 1400;        //in Bytes. 1458 to prevent fragments
    uint32_t nPackets = 2000;
    uint32_t rcvBuff = 10000*pktSize;
    uint32_t sndBuff = 10000*pktSize;
    uint32_t enbBotTrace = 0;
    float startTime = 0;
    float simDuration = 10;        //in seconds
    uint32_t cleanup_time = 2;
    uint32_t nSources = 2;


    
    bool logging = false;

    std::string root_dir;
    std::string qsizeTrFileName;
    std::string cwndTrFileName;
    std::string droppedTrFileName;
 
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
    cmd.AddValue ("rcvBuff", "Size of recieve buffer in Bytes", rcvBuff);
    cmd.AddValue ("sndBuff", "Size of send buffer in Bytes", sndBuff);
    cmd.AddValue ("enableBottleneckTrace", "Tracing the bottleneck packets", enbBotTrace);
    cmd.Parse (argc, argv);

    root_dir = "/mnt/Store/Project-summer/runtime/"+flavour+"/"+RTT+"/"+std::to_string(nSources)+"/";
    qsizeTrFileName = root_dir + "qsizeTrace";
    cwndTrFileName = root_dir + "cwndDropTail";
    droppedTrFileName = root_dir + "droppedPacketTrace";
    
    cwnd.resize(nSources);
    cwnd_streams.resize(nSources);

    float stopTime = startTime + simDuration;
   
    std::string tcpModel ("ns3::"+flavour);
    
    Config::SetDefault ("ns3::TcpL4Protocol::SocketType", StringValue(tcpModel));
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue (1*pktSize));
    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue (1));
    //Config::SetDefault("ns3::TcpSocketBase::MaxWindowSize", UintegerValue (20*1000));
    Config::SetDefault ("ns3::TcpSocket::SndBufSize", UintegerValue (sndBuff));
    Config::SetDefault ("ns3::TcpSocket::RcvBufSize", UintegerValue (rcvBuff));
    

    // Defining the nodes 
    NodeContainer nodes;
    nodes.Create (2+nSources*2);
    NodeContainer sr1 [nSources];
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
        p2p_s[i].SetQueue ("ns3::DropTailQueue<Packet>", "MaxSize", QueueSizeValue (QueueSize (std::to_string(5000/nSources)+"p"))); // p in 1000p stands for packets
        p2p_s[i].DisableFlowControl();

        p2p_d[i].SetDeviceAttribute ("DataRate", StringValue(accessBandwidth));
        p2p_d[i].SetChannelAttribute ("Delay", StringValue(delay_str));
        p2p_d[i].SetQueue ("ns3::DropTailQueue<Packet>", "MaxSize", QueueSizeValue (QueueSize (std::to_string(5000/nSources)+"p"))); // p in 1000p stands for packets
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

    // Configuring the application at each source node.
    for (uint32_t i = 0; i < nSources; i++)
    {
        
        BulkSendHelper tmp_source("ns3::TcpSocketFactory",InetSocketAddress (ip_d_r2[i].GetAddress (0), sinkPort));
           
        // Set the amount of data to send in bytes.  Zero is unlimited.
        tmp_source.SetAttribute ("MaxBytes", UintegerValue (0));
        sourceApps[i] = tmp_source.Install (nodes.Get (2+i));
        sourceApps->Start (Seconds (startTime));
        sourceApps->Stop (Seconds (stopTime));

    }
    
    // Configuring file streams to write the congestion windows sizes to.
    AsciiTraceHelper ascii[nSources];
    for( uint32_t i = 0; i < nSources; i++){
        cwnd_streams[i] = ascii[i].CreateFileStream (cwndTrFileName+"_"+std::to_string(i)+".txt");
    }
    
    // Configuring file stream to write the Qsize
    AsciiTraceHelper ascii_qsize;
    qSize_stream = ascii_qsize.CreateFileStream(qsizeTrFileName+".txt");


    // start tracing the congestion window size and qSize
    Simulator::Schedule( Seconds(startTime+2), &startTracingCwnd, nSources);
    Simulator::Schedule( Seconds(startTime+2), &StartTracingQueueSize);


    // writing the congestion windows size to files periodically ( 1 sec. )
    for (uint32_t time = 2; time < stopTime; time++)
    {
        Simulator::Schedule( Seconds(startTime+time), &writeCwndToFile, nSources);
        Simulator::Schedule( Seconds(startTime+time), &TraceQueueSize);
    }
    
    // start tracing Queue Size and Dropped Files

    Simulator::Schedule( Seconds(startTime+2), &TraceDroppedPacket, droppedTrFileName);
    
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
    // anim.SetMaxPktsPerTraceFile(50000000000);s
    
    
    
    Simulator::Run ();

    Simulator::Destroy ();

    return 0;

}