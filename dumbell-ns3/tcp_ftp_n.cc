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
    ++m_packetsSent;
    ScheduleTx();
    // if ( ++m_packetsSent < m_nPackets ){
    //     ScheduleTx();
    // }
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
RxDrop(Ptr<OutputStreamWrapper> stream, Ptr<const Packet> p){
   *stream->GetStream () << "RxDrop at " << Simulator::Now().GetSeconds() << std::endl;
} 

int 
main(int argc, char *argv[])
{   
    std::string bottleneckBandwidth = "10Mbps";
    std::string bottleneckDelay = "100us";
    std::string accessBandwidth = "10Mbps";
    std::string accessDelay = "100us";
    std::string sourceRate = "2Mbps";
    std::string flavour = "TcpBic";
    std::string queueType = "DropTail";       //DropTail or CoDel
    std::string queueSize = "2p";      //in packets
    uint32_t pktSize = 1400;        //in Bytes. 1458 to prevent fragments
    uint32_t nPackets = 2000;
    float startTime = 0.1;
    float simDuration = 10;        //in seconds
    uint32_t nSources = 5;
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
    cmd.AddValue ("nSources", "No. of sources in the dumbell topology", nSources);
    cmd.AddValue ("sourceRate", "Rate of generating packets at the application level", sourceRate);
    cmd.Parse (argc, argv);
   
    float stopTime = startTime + simDuration;
   
    std::string tcpModel ("ns3::"+flavour);
    Config::SetDefault ("ns3::TcpL4Protocol::SocketType", StringValue(tcpModel));
    //Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue (1));
    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue (1));
    Config::SetDefault("ns3::TcpSocketBase::MaxWindowSize", UintegerValue (20*1000));

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
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute ("DataRate", StringValue(accessBandwidth));
    p2p.SetChannelAttribute ("Delay", StringValue(accessDelay));
    p2p.SetQueue ("ns3::DropTailQueue<Packet>", "MaxSize", QueueSizeValue (QueueSize (queueSize))); // p in 1000p stands for packets


    PointToPointHelper bottleneck;
    bottleneck.SetDeviceAttribute("DataRate", StringValue(bottleneckBandwidth));
    bottleneck.SetChannelAttribute("Delay", StringValue(bottleneckDelay));
    bottleneck.SetQueue ("ns3::DropTailQueue<Packet>", "MaxSize", QueueSizeValue (QueueSize (queueSize))); // p in 1000p stands for packets


    // Consists of both the link and nodes
    NetDeviceContainer r1_r2 = bottleneck.Install(r1r2);
    NetDeviceContainer s_r1[nSources];
    NetDeviceContainer d_r2[nSources];

    for( uint32_t i = 0; i<nSources; i++){
        s_r1[i] = p2p.Install(sr1[i]);
        d_r2[i] = p2p.Install(dr2[i]);
    }

    

    Ptr<RateErrorModel> em = CreateObject<RateErrorModel> ();
    em->SetAttribute("ErrorRate", DoubleValue(0.00000));
    
    r1_r2.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(em));

    for (uint32_t i = 0; i < nSources; i++)
    {
        s_r1[i].Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(em));
        d_r2[i].Get(0)->SetAttribute("ReceiveErrorModel", PointerValue(em));
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
    
    Ptr<Socket> ns3TcpSocket[nSources];
    Ptr<MyApp> app[nSources];

    // Configuring the application at each source node.
    for (uint32_t i = 0; i < nSources; i++)
    {
        ns3TcpSocket[i] = Socket::CreateSocket (nodes.Get (2+i), TcpSocketFactory::GetTypeId ());
                
        app[i] = CreateObject<MyApp> ();
        app[i]->Setup (ns3TcpSocket[i], sinkAddress[i], pktSize, nPackets, DataRate (sourceRate));
        nodes.Get (2+i)->AddApplication (app[i]);
        app[i]->SetStartTime (Seconds (startTime));
        app[i]->SetStopTime (Seconds (stopTime));
    }

    AsciiTraceHelper ascii;
    Ptr<OutputStreamWrapper> stream = ascii.CreateFileStream ("cwnd_trace_file.txt");
    // only tracing the cwnd for a single source i.e. source -> 0 ( node - 2).
    ns3TcpSocket[0]->TraceConnectWithoutContext ("CongestionWindow", MakeBoundCallback (&plotCwndChange, stream));
    
    
    for (uint32_t i = 0; i < nSources; i++)
    {
        d_r2[i].Get (0)->TraceConnectWithoutContext ("PhyRxDrop", MakeBoundCallback (&RxDrop, stream));
    }
    
    Simulator::Stop (Seconds (stopTime));
    Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

    AnimationInterface anim("tcp_n.xml");
    anim.SetMobilityPollInterval (Seconds (1));

    if ( nSources == 2){
          // // X (->) , Y(|) 
        // // source nodes
        anim.SetConstantPosition(nodes.Get(2), 10.0, 20.0);
        anim.SetConstantPosition(nodes.Get(3), 10.0, 40.0);

        // // // router nodes
        anim.SetConstantPosition(nodes.Get(0), 30.0, 30.0);
        anim.SetConstantPosition(nodes.Get(1), 50.0, 30.0);

        // // // sink nodes
        anim.SetConstantPosition(nodes.Get(4), 70.0, 20.0);
        anim.SetConstantPosition(nodes.Get(5), 70.0, 40.0);
    }
    anim.SetMaxPktsPerTraceFile(50000000000);
    
    
    Simulator::Run ();
    Simulator::Destroy ();

    return 0;

}