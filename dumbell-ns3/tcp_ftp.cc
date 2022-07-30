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
#include "ns3/netanim-module.h"


using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TCPSCRIPT");

static void
CwndTracer (Ptr<OutputStreamWrapper>stream, uint32_t oldval, uint32_t newval)
{
  *stream->GetStream () << oldval << " " << newval << std::endl;
}

static void
TraceCwnd (std::string cwndTrFileName)
{
  AsciiTraceHelper ascii;
  if (cwndTrFileName.compare ("") == 0)
    {
      NS_LOG_DEBUG ("No trace file for cwnd provided");
      return;
    }
  else
    {
      Ptr<OutputStreamWrapper> stream = ascii.CreateFileStream (cwndTrFileName.c_str ());
      Config::ConnectWithoutContext ("/NodeList/1/$ns3::TcpL4Protocol/SocketList/0/CongestionWindow",MakeBoundCallback (&CwndTracer, stream));
    }
}


int 
main(int argc, char *argv[])
{   
    std::string bottleneckBandwidth = "5Mbps";
    std::string bottleneckDelay = "5ms";
    std::string accessBandwidth = "100Mbps";
    std::string accessDelay = "0.1ms";
    
    std::string queueType = "DropTail";       //DropTail or CoDel
    std::string queueSize = "10p";     //in packets
    uint32_t pktSize = 1458;        //in bytes. 1458 to prevent fragments
    float startTime = 0.1;
    float simDuration = 60;        //in seconds
    
    bool isPcapEnabled = true;
    std::string pcapFileName = "pcapFileDropTail.pcap";
    std::string cwndTrFileName = "cwndDropTail.tr";
    bool logging = false;
 
    CommandLine cmd;
    cmd.AddValue ("bottleneckBandwidth", "Bottleneck bandwidth", bottleneckBandwidth);
    cmd.AddValue ("bottleneckDelay", "Bottleneck delay", bottleneckDelay);
    cmd.AddValue ("accessBandwidth", "Access link bandwidth", accessBandwidth);
    cmd.AddValue ("accessDelay", "Access link delay", accessDelay);
    cmd.AddValue ("queueType", "Queue type: DropTail, CoDel", queueType);
    cmd.AddValue ("queueSize", "Queue size in packets", queueSize);
    cmd.AddValue ("pktSize", "Packet size in bytes", pktSize);
    cmd.AddValue ("startTime", "Simulation start time", startTime);
    cmd.AddValue ("simDuration", "Simulation duration in seconds", simDuration);
    cmd.AddValue ("isPcapEnabled", "Flag to enable/disable pcap", isPcapEnabled);
    cmd.AddValue ("pcapFileName", "Name of pcap file", pcapFileName);
    cmd.AddValue ("cwndTrFileName", "Name of cwnd trace file", cwndTrFileName);
    cmd.AddValue ("logging", "Flag to enable/disable logging", logging);
    cmd.Parse (argc, argv);
   
    float stopTime = startTime + simDuration;
   
    std::string tcpModel ("ns3::TcpBic");

    Config::SetDefault ("ns3::TcpL4Protocol::SocketType", StringValue(tcpModel));

    CommandLine cmd;
    cmd.Parse( argc, argv);

    if (isPcapEnabled)
    {
        GlobalValue::Bind ("ChecksumEnabled", BooleanValue (true));
    }
    
    

    // Defining the nodes 
    NodeContainer nodes;
    nodes.Create (6);
    NodeContainer n2n0 = NodeContainer(nodes.Get(2), nodes.Get(0));
    NodeContainer n3n0 = NodeContainer(nodes.Get(3), nodes.Get(0));
    NodeContainer n0n1 = NodeContainer(nodes.Get(0), nodes.Get(1));
    NodeContainer n1n4 = NodeContainer(nodes.Get(1), nodes.Get(4));
    NodeContainer n1n5 = NodeContainer(nodes.Get(1), nodes.Get(5));

    // Defining the links to be used between nodes
    PointToPointHelper access_link;
    access_link.SetDeviceAttribute ("DataRate", StringValue(accessBandwidth));
    access_link.SetChannelAttribute ("Delay", StringValue(accessDelay));

    PointToPointHelper bottleneck;
    bottleneck.SetDeviceAttribute("DataRate", StringValue(bottleneckBandwidth));
    bottleneck.SetChannelAttribute("Delay", StringValue(bottleneckDelay));
    bottleneck.SetQueue ("ns3::DropTailQueue<Packet>", "MaxSize", QueueSizeValue (QueueSize (queueSize))); // p in 100p stands for packets

    // source - 4,5
    // router - 1,3
    // sink -   0,2

    // Consists of both the link and nodes
    NetDeviceContainer d2d0 = access_link.Install(n2n0);
    NetDeviceContainer d3d0 = access_link.Install(n3n0);
    NetDeviceContainer d1d4 = access_link.Install(n1n4);
    NetDeviceContainer d1d5 = access_link.Install(n1n5);

    // Creating the bottleneck link
    NetDeviceContainer d0d1 = bottleneck.Install(n0n1);

    Ptr<RateErrorModel> em = CreateObject<RateErrorModel> ();

    em->SetAttribute("ErrorRate", DoubleValue(0.00000));
    d0d1.Get (0)->SetAttribute("ReceiveErrorModel", PointerValue(em));
    d2d0.Get (1)->SetAttribute ("ReceiveErrorModel", PointerValue (em));
    d3d0.Get (0)->SetAttribute ("ReceiveErrorModel", PointerValue (em));
    d1d4.Get (0)->SetAttribute ("ReceiveErrorModel", PointerValue (em));
    d1d5.Get (0)->SetAttribute ("ReceiveErrorModel", PointerValue (em));  


    //  Assigning ip address to each node.
    InternetStackHelper stack;
    stack.Install(nodes);
    Ipv4AddressHelper address;

    address.SetBase("10.1.1.0", "255.255.255.252");
    Ipv4InterfaceContainer i2i0 = address.Assign(d2d0);

    address.SetBase("10.1.2.0","255.255.255.0");
    Ipv4InterfaceContainer i3i0 = address.Assign(d3d0);

    address.SetBase("10.1.3.0","255.255.255.0");
    address.Assign(d0d1);

    address.SetBase("10.1.4.0","255.255.255.0");
    Ipv4InterfaceContainer i1i4=address.Assign(d1d4);

    address.SetBase("10.1.5.0","255.255.255.0");
    Ipv4InterfaceContainer i1i5 = address.Assign(d1d5);

    // Attaching sink to nodes
    uint16_t sinkPort = 8080;
    PacketSinkHelper packetSinkHelper ("ns3::TcpSocketFactory", InetSocketAddress( Ipv4Address::GetAny(), sinkPort));

    Address sink1Address (InetSocketAddress(i1i4.GetAddress(1), sinkPort));
    ApplicationContainer sinkApp1 = packetSinkHelper.Install(nodes.Get(4));
    sinkApp1.Start(Seconds(startTime));
    sinkApp1.Stop(Seconds(stopTime));


    Address sink2Address (InetSocketAddress (i1i5.GetAddress(1), sinkPort));
    ApplicationContainer sinkApp2 = packetSinkHelper.Install (nodes.Get (5));
    sinkApp2.Start (Seconds (startTime));
    sinkApp2.Stop (Seconds (stopTime));

    // Configure application
    AddressValue remoteAddress (InetSocketAddress (sinkInterface.GetAddress (0, 0), port));
    
    Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (pktSize));
    BulkSendHelper ftp ("ns3::TcpSocketFactory", Address ());

    ftp.SetAttribute ("Remote", remoteAddress);
    ftp.SetAttribute ("SendSize", UintegerValue (pktSize));
    ftp.SetAttribute ("MaxBytes", UintegerValue (0));
    ApplicationContainer sourceApp = ftp.Install (source.Get (0));
    sourceApp.Start (Seconds (0));
    sourceApp.Stop (Seconds (stopTime - 3));
    

    Simulator::Stop (Seconds (stopTime));
    Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

    AnimationInterface anim("tcp.xml");
    // X (->) , Y(|) 
    // source nodes
    anim.SetConstantPosition(nodes.Get(4), 10.0, 20.0);
    anim.SetConstantPosition(nodes.Get(5), 10.0, 40.0);

    // router nodes
    anim.SetConstantPosition(nodes.Get(3), 30.0, 30.0);
    anim.SetConstantPosition(nodes.Get(1), 50.0, 30.0);

    // sink nodes
    anim.SetConstantPosition(nodes.Get(2), 70.0, 20.0);
    anim.SetConstantPosition(nodes.Get(0), 70.0, 40.0);
    
    
    
    Simulator::Run ();
    Simulator::Destroy ();

    return 0;

}