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

class MyApp : public Application
{
    public:
        MyApp ();
        virtual ~MyApp();

        void Setup ( Ptr<Socket> socket, Address address, uint32_t packetSize, uint32_t nPackets, DataRate dataRate);


    private:
        virtual void StartApplication (void);
        virtual void StopApplication (void);

        void ScheduleTx (void);
        void SendPacket (void);


        Ptr<Socket> m_socket;
        Address  m_peer;
        uint32_t m_packetSize;
        uint32_t m_nPackets;
        DataRate m_dataRate;
        EventId  m_sendEvent;
        bool     m_running;
        uint32_t m_packetsSent;
        
};

MyApp::MyApp ()
    : m_socket(0),
      m_peer(),
      m_packetSize(0),
      m_nPackets (0),
      m_dataRate(0),
      m_sendEvent(),
      m_running(false),
      m_packetsSent(0)
{
}


MyApp::~MyApp()
{
    m_socket = 0;
}


void
MyApp::Setup (Ptr<Socket> socket, Address address, uint32_t packetSize, uint32_t nPackets, DataRate dataRate)
{
    m_socket = socket;
    m_peer = address;
    m_packetSize = packetSize;
    m_nPackets = nPackets;
    m_dataRate = dataRate;

}

void
MyApp::StartApplication (void)
{
    m_running = true;
    m_packetsSent = 0;
    m_socket->Bind();
    m_socket->Connect(m_peer);
    SendPacket();
}

void 
MyApp::StopApplication (void)
{
    m_running = false;
    if(m_sendEvent.IsRunning())
    {
        Simulator::Cancel ( m_sendEvent );
    }

    if (m_socket)
    {
        m_socket->Close();
    }
    
}



void
MyApp::SendPacket(void)
{
    Ptr<Packet> packet = Create<Packet> (m_packetSize);
    m_socket->Send(packet);

    if ( ++m_packetsSent < m_nPackets ){
        ScheduleTx();
    }
}

void
MyApp::ScheduleTx (void)
{
    if( m_running )
    {   // tNext is the amount of time before next packet should be sent
        Time tNext (Seconds (m_packetSize * 8 / static_cast<double> (m_dataRate.GetBitRate())));
        // schedule the sending of another packet after tNext time.
        m_sendEvent = Simulator::Schedule (tNext, &MyApp::SendPacket, this);

    }
}

static void
plotCwndChange (Ptr<OutputStreamWrapper> stream, uint32_t oldCwnd, uint32_t newCwnd){
    //NS_LOG_UNCOND(Simulator::Now().GetSeconds() << "\t" << newCwnd);
    *stream->GetStream () << Simulator::Now().GetSeconds() << "\t" << newCwnd << std::endl;
}

static void
RxDrop(Ptr<const Packet> p){
    NS_LOG_UNCOND("RxDrop at " << Simulator::Now().GetSeconds());
} 

int 
main(int argc, char *argv[])
{   
    std::string bottleneckBandwidth = "5Mbps";
    std::string bottleneckDelay = "5ms";
    std::string accessBandwidth = "100Mbps";
    std::string accessDelay = "0.1ms";
    std::string flavour = "TcpBic";
    std::string queueType = "DropTail";       //DropTail or CoDel
    std::string queueSize = "1000p";      //in packets
    uint32_t pktSize = 1458;        //in Bytes. 1458 to prevent fragments
    uint32_t nPackets = 10000000;
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
    cmd.AddValue ("flavour", "Flavour to be used like - TcpBic, TcpBbr etc.", flavour);
    cmd.AddValue ("nPackets", "No. of Packets to send", nPackets);
    cmd.Parse (argc, argv);
   
    float stopTime = startTime + simDuration;
   
    // std::string tcpModel ("ns3::"+flavour);

    // Config::SetDefault ("ns3::TcpL4Protocol::SocketType", StringValue(tcpModel));

    // Defining the nodes 
    NodeContainer nodes;
    nodes.Create (6);
    NodeContainer n0n1 = NodeContainer(nodes.Get(0), nodes.Get(1));
    NodeContainer n1n2 = NodeContainer(nodes.Get(1), nodes.Get(2));
    NodeContainer n1n3 = NodeContainer(nodes.Get(1), nodes.Get(3));
    NodeContainer n3n4 = NodeContainer(nodes.Get(3), nodes.Get(4));
    NodeContainer n3n5 = NodeContainer(nodes.Get(3), nodes.Get(5));

    // Defining the links to be used between nodes
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute ("DataRate", StringValue(accessBandwidth));
    p2p.SetChannelAttribute ("Delay", StringValue(accessDelay));

    PointToPointHelper bottleneck;
    bottleneck.SetDeviceAttribute("DataRate", StringValue(bottleneckBandwidth));
    bottleneck.SetChannelAttribute("Delay", StringValue(bottleneckDelay));
    //bottleneck.SetQueue ("ns3::DropTailQueue<Packet>", "MaxSize", QueueSizeValue (QueueSize (queueSize))); // p in 1000p stands for packets

    // source - 4,5
    // router - 1,3
    // sink -   0,2

    // Consists of both the link and nodes
    NetDeviceContainer d0d1 = p2p.Install(n0n1);
    NetDeviceContainer d1d2 = p2p.Install(n1n2);
    NetDeviceContainer d3d4 = p2p.Install(n3n4);
    NetDeviceContainer d3d5 = p2p.Install(n3n5);

    // Creating the bottleneck link
    NetDeviceContainer d1d3 = bottleneck.Install(n1n3);

    Ptr<RateErrorModel> em = CreateObject<RateErrorModel> ();

    em->SetAttribute("ErrorRate", DoubleValue(0.00000));
    d0d1.Get (0)->SetAttribute("ReceiveErrorModel", PointerValue(em));
    d1d2.Get (1)->SetAttribute ("ReceiveErrorModel", PointerValue (em));
    d1d3.Get (0)->SetAttribute ("ReceiveErrorModel", PointerValue (em));
    d3d4.Get (0)->SetAttribute ("ReceiveErrorModel", PointerValue (em));
    d3d5.Get (0)->SetAttribute ("ReceiveErrorModel", PointerValue (em));  


    //  Assigning ip address to each node.
    InternetStackHelper stack;
    stack.Install(nodes);
    Ipv4AddressHelper address;

    address.SetBase("10.1.1.0", "255.255.255.252");
    Ipv4InterfaceContainer i0i1 = address.Assign(d0d1);

    address.SetBase("10.1.2.0","255.255.255.0");
    Ipv4InterfaceContainer i1i2=address.Assign(d1d2);

    address.SetBase("10.1.3.0","255.255.255.0");
    address.Assign(d1d3);

    address.SetBase("10.1.4.0","255.255.255.0");
    Ipv4InterfaceContainer i3i4=address.Assign(d3d4);

    address.SetBase("10.1.5.0","255.255.255.0");
    Ipv4InterfaceContainer i3i5 = address.Assign(d3d5);

    // Attaching sink to nodes
    uint16_t sinkPort = 8080;
    PacketSinkHelper packetSinkHelper ("ns3::TcpSocketFactory", InetSocketAddress( Ipv4Address::GetAny(), sinkPort));

    Address sink1Address (InetSocketAddress(i0i1.GetAddress(0), sinkPort));
    ApplicationContainer sinkApp1 = packetSinkHelper.Install(nodes.Get(0));
    sinkApp1.Start(Seconds(startTime));
    sinkApp1.Stop(Seconds(stopTime));


    Address sink2Address (InetSocketAddress (i1i2.GetAddress (1), sinkPort));
    ApplicationContainer sinkApp2 = packetSinkHelper.Install (nodes.Get (2));
    sinkApp2.Start (Seconds (startTime));
    sinkApp2.Stop (Seconds (stopTime));


    Ptr<Socket> ns3TcpSocket1 = Socket::CreateSocket(nodes.Get(5), TcpSocketFactory::GetTypeId());

    Ptr<MyApp> app1 = CreateObject<MyApp> ();
    app1->Setup (ns3TcpSocket1, sink1Address, pktSize, nPackets, DataRate ("1Mbps"));
    nodes.Get (5)->AddApplication (app1);
    app1->SetStartTime (Seconds (startTime));
    app1->SetStopTime (Seconds (stopTime));

    Ptr<Socket> ns3TcpSocket2 = Socket::CreateSocket (nodes.Get (4), TcpSocketFactory::GetTypeId ());
    AsciiTraceHelper ascii;
    Ptr<OutputStreamWrapper> stream = ascii.CreateFileStream ("cwnd_trace_file.txt");
    ns3TcpSocket2->TraceConnectWithoutContext ("CongestionWindow", MakeBoundCallback (&plotCwndChange, stream));
    
    Ptr<MyApp> app2 = CreateObject<MyApp> ();
    app2->Setup (ns3TcpSocket2, sink2Address, pktSize, nPackets, DataRate ("1Mbps"));
    nodes.Get (4)->AddApplication (app2);
    app2->SetStartTime (Seconds (startTime));
    app2->SetStopTime (Seconds (stopTime));

    d0d1.Get (0)->TraceConnectWithoutContext ("PhyRxDrop", MakeCallback (&RxDrop));
    d1d2.Get (1)->TraceConnectWithoutContext ("PhyRxDrop", MakeCallback (&RxDrop));
    //nodes.Get (1)->TraceConnectWithoutContext ("PhyRxDrop", MakeCallback (&RxDrop));

    Simulator::Stop (Seconds (stopTime));
    Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

    AnimationInterface anim("tcp.xml");
   
    // X (->) , Y(|) 
    // source nodes
    anim.SetConstantPosition(nodes.Get(4), 10.0, 20.0);
    anim.SetConstantPosition(nodes.Get(5), 10.0, 40.0);

    // // router nodes
    anim.SetConstantPosition(nodes.Get(3), 30.0, 30.0);
    anim.SetConstantPosition(nodes.Get(1), 50.0, 30.0);

    // // sink nodes
    anim.SetConstantPosition(nodes.Get(2), 70.0, 20.0);
    anim.SetConstantPosition(nodes.Get(0), 70.0, 40.0);
    anim.SetMaxPktsPerTraceFile(50000000000);
    
    
    Simulator::Run ();
    Simulator::Destroy ();

    return 0;

}