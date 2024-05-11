#include <iostream>
#include <fstream>
#include <random>
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

NS_LOG_COMPONENT_DEFINE ("TCPSCRIPT");

std::vector<uint32_t> m_cwnd;
Ptr<OutputStreamWrapper> m_cwndStream;

std::vector<double> m_destLostPacketsvstime;
std::vector<uint32_t> m_lostPacketsTotal;
Ptr<OutputStreamWrapper> m_dropped_stream;

uint16_t m_queueSize;
Ptr<OutputStreamWrapper> m_qsizeStream;

Ptr<FlowMonitor> m_flowMonitor;
FlowMonitorHelper m_flowHelper;

Ptr<OutputStreamWrapper> m_btlnckTransmittedStream;

Ptr<OutputStreamWrapper> m_droppedStream;

std::vector<uint64_t> m_totalBytesDest;
std::vector<double> m_destThroughputvstime;
Ptr<OutputStreamWrapper> m_destThroughputvstimeStream;

uint16_t m_nSources;
uint16_t m_pktSize;

double_t btlnckThroughputvstime;
Ptr<OutputStreamWrapper> btlnckThroughputvstimeStream;

// Function prototypes
void updateCwnd (uint16_t pos, uint32_t oldCwnd, uint32_t newCwnd);
void startTracingCwnd (uint16_t numSources);
void writeCwndHeadersToFile ();
void writeCwndValuesToFile ();
void writeQueueSizeToFile (Ptr<QueueDisc> qd);
void writeBottleneckThroughputToFile (uint16_t simDurationInSeconds);
void writeDestThroughputToFile ();
void writeDestLostPacketsToFile ();
void writeDestHeadersToFile();
void writeDestLostPacketsHeadersToFile();
void writeBtlnckHeadersToFile();
void updateFlowStatsDest ();
std::vector<std::string> splitString (std::string &str, char sep);


// static std::string
// WhichVariant (TcpBbr::BbrVar variant)
// {
//   switch (variant)
//     {
//     case 0:
//       return "BBR";
//     case 1:
//       return "BBR_PRIME";
//     case 2:
//       return "BBR_PLUS";
//     case 3:
//       return "BBR_HSR";
//     case 4:
//       return "BBR_V2";
//     case 5:
//       return "BBR_DELAY";
//     }
//   NS_ASSERT (false);
// }

int
main (int argc, char *argv[])
{

  std::string bottleneckBandwidth = "100Mbps";
  std::string bottleneckDelay = "10ms";
  std::string accessBandwidth = "2Mbps";
  std::string rtts = "10ms";
  std::string flavours = "TcpBbrV2";
  std::string queueDisc = "FifoQueueDisc"; 
  std::string m_queueSize = "2084p"; //in packets

  m_pktSize = 1400; //in Bytes. 1458 to prevent fragments
  

  uint16_t enbBotTrace = 0;
  bool disableCwndTracing = false;
  float startTime = 0;
  float simDuration = 100; //in seconds
  uint16_t cleanup_time = 20;
  m_nSources = 60;
  btlnckThroughputvstime = 0;

  bool logging = false;
  bool enablePcap = false;

  std::string rootDir;
  std::string qsizeTrFileName;
  std::string cwndTrFileName;
  std::string droppedTrFileName;
  std::string bottleneckTxFileName;
  std::string btlnckThroughputFileName;
  std::string destBtlnckThroughputFileName;
  std::string pcapFileName;

  m_totalBytesDest.resize (m_nSources + 1, 0);
  m_lostPacketsTotal.resize (m_nSources + 1, 0);

  CommandLine cmd;
  cmd.AddValue ("bottleneckBandwidth", "Bottleneck bandwidth", bottleneckBandwidth);
  cmd.AddValue ("bottleneckDelay", "Bottleneck delay", bottleneckDelay);
  cmd.AddValue ("accessBandwidth", "Access link bandwidth", accessBandwidth);
  cmd.AddValue ("rtts", "mean rtts for random generation of delays in each link", rtts);
  cmd.AddValue ("queueDisc", "Queue type: DropTail, CoDel", queueDisc);
  cmd.AddValue ("queueSize", "Queue size in packets", m_queueSize);
  cmd.AddValue ("pktSize", "Packet size in bytes", m_pktSize);
  cmd.AddValue ("startTime", "Simulation start time", startTime);
  cmd.AddValue ("simDuration", "Simulation duration in seconds", simDuration);
  cmd.AddValue ("cwndTrFileName", "Name of cwnd trace file", cwndTrFileName);
  cmd.AddValue ("logging", "Flag to enable/disable logging", logging);
  cmd.AddValue ("flavours", "flavours to be used like - TcpBic, TcpBbr etc.", flavours);
  cmd.AddValue ("nSources", "No. of sources in the dumbell topology", m_nSources);
  cmd.AddValue ("enableBottleneckTrace", "Tracing the bottleneck packets", enbBotTrace);
  cmd.AddValue ("disableCwndTracing", "Skipping the cwnd tracing to avoid memory shortage", disableCwndTracing);
  cmd.Parse (argc, argv);

  rootDir = "/mnt/Store/Project-summer/runtime/" + flavours + "/" + rtts + "/" +
            std::to_string (m_nSources) + "/";
  qsizeTrFileName = rootDir + "qsizeTrace";
  cwndTrFileName = rootDir + "cwndDropTail";
  droppedTrFileName = rootDir + "droppedPacketTrace";
  bottleneckTxFileName = rootDir + "bottleneckTx";
  btlnckThroughputFileName = rootDir + "btlnck_throughput";
  destBtlnckThroughputFileName = rootDir + "dest_throughput";
  pcapFileName = rootDir + "btlnck_pcap";

  

  m_destThroughputvstime.resize (m_nSources + 1);
  m_destLostPacketsvstime.resize (m_nSources + 1);
  

  float stopTime = startTime + simDuration;

  std::string tcpModel ("ns3::" + flavours);
  queueDisc = std::string("ns3::") + queueDisc;

  // Converting the string rtts to an array of rtts
  std::vector<std::string> rttsArray = splitString (rtts, '_');
  int noOfFlowWithSimilarRTTs = m_nSources / rttsArray.size ();

  // Converting the string flavours to an array of flavours
  std::vector<std::string> flavoursArray = splitString (flavours, '_');
  int noOfFlowWithSimilarFlavours = m_nSources / flavoursArray.size ();

  if (flavoursArray.size () > 1 && rttsArray.size () > 1)
    {
      std::cout << "Can't have multiple rtts and flavours at the same time" << std::endl;
      return -1;
    }

  if (m_nSources % flavoursArray.size () != 0)
    {
      std::cout << "ERROR:Number of flows need to be a multiple of number of flavours" << std::endl;
      return -1;
    }

  if (m_nSources % rttsArray.size () != 0)
    {
      std::cout << "ERROR:Number of flows need to be a multiple of number of rtts" << std::endl;
      return -1;
    }
  
  // Configuring the packet size
  Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (m_pktSize));
  //Config::SetDefault ("ns3::TcpSocket::SndBufSize", UintegerValue (4194304));
  //Config::SetDefault ("ns3::TcpSocket::RcvBufSize", UintegerValue (6291456));
  Config::SetDefault ("ns3::TcpSocket::InitialCwnd", UintegerValue (10));
  Config::SetDefault ("ns3::DropTailQueue<Packet>::MaxSize", QueueSizeValue (QueueSize ("1p")));
  Config::SetDefault (queueDisc + "::MaxSize", QueueSizeValue (QueueSize (m_queueSize)));
  

  //Config::SetDefault("ns3::TcpSocketBase::MaxWindowSize", UintegerValue (20*1000));
  //Config::SetDefault ("ns3::TcpL4Protocol::SocketType", StringValue(tcpModel));


  // Defining the nodes
  NodeContainer nodes;
  nodes.Create (2 + m_nSources * 2);
  // Source nodes
  NodeContainer sr1[m_nSources];
  // Destination nodes
  NodeContainer dr2[m_nSources];

  NodeContainer r1r2 = NodeContainer (nodes.Get (0), nodes.Get (1));

  for (uint16_t i = 0; i < m_nSources; i++)
    {
      sr1[i] = NodeContainer (nodes.Get (i + 2), nodes.Get (0));
      dr2[i] = NodeContainer (nodes.Get (1), nodes.Get (2 + m_nSources + i));
    }

  // Using uniform distribution to randomly generate rtts corresponding with input rtts
  std::vector<Ptr<UniformRandomVariable>> x;
  for (uint16_t i = 0; i < rttsArray.size (); i++)
    {
      double min = 0.0;
      //std::cout << "RTT:" << rttsArray[i] << std::endl;
      double max = double (2 * stoi (rttsArray[i].substr (0, rttsArray[i].length () - 2)));
      //std::cout << "max:" << max << std::endl;
      x.push_back (CreateObject<UniformRandomVariable> ());
      x.back ()->SetAttribute ("Min", DoubleValue (min));
      x.back ()->SetAttribute ("Max", DoubleValue (max));
    }
  
  // Defining the links to be used between nodes

  PointToPointHelper bottleneck;
  bottleneck.SetDeviceAttribute ("DataRate", StringValue (bottleneckBandwidth));
  bottleneck.SetChannelAttribute ("Delay", StringValue (bottleneckDelay));
  //bottleneck.DisableFlowControl();
  PointToPointHelper p2p_s[m_nSources];
  PointToPointHelper p2p_d[m_nSources];
  for (uint16_t i = 0; i < m_nSources; i++)
    {
      // getting the appropriate delay value by using flow number
      double delay = (x[int (i / noOfFlowWithSimilarRTTs)]->GetValue ()) / 4;

      //std::cout << std::to_string(i/noOfFlowWithSimilarRTTs) + ":" << delay * 4 << std::endl;
      std::string delay_str = std::to_string (delay) + "ms";
      p2p_s[i].SetDeviceAttribute ("DataRate", StringValue (accessBandwidth));
      p2p_s[i].SetChannelAttribute ("Delay", StringValue (delay_str));
      //p2p_s[i].DisableFlowControl();
      
      p2p_d[i].SetDeviceAttribute ("DataRate", StringValue (accessBandwidth));
      p2p_d[i].SetChannelAttribute ("Delay", StringValue (delay_str));
      //p2p_d[i].DisableFlowControl();

      // // Removing the queues from all the links except bottleneck
      // p2p_s[i].SetQueue ("ns3::DropTailQueue<Packet>", "MaxSize",
      //                    QueueSizeValue (QueueSize ("100p"))); // p in 1000p stands for packets
      // p2p_d[i].SetQueue ("ns3::DropTailQueue<Packet>", "MaxSize",
      //                    QueueSizeValue (QueueSize ("100p"))); // p in 1000p stands for packets
      
    }

  // Consists of both the link and nodes
  NetDeviceContainer r1_r2 = bottleneck.Install (r1r2);

  NetDeviceContainer s_r1[m_nSources];
  NetDeviceContainer d_r2[m_nSources];

  for (uint16_t i = 0; i < m_nSources; i++)
    {
      s_r1[i] = p2p_s[i].Install (sr1[i]);
      d_r2[i] = p2p_d[i].Install (dr2[i]);
    }

  //  Assigning ip address to each node.
  InternetStackHelper stack;
  stack.Install (nodes);

  // Configure the queue discipline
  TrafficControlHelper tch;
  tch.SetRootQueueDisc (queueDisc);
  for (int i = 0; i < m_nSources; i++)
  {
    tch.Install(s_r1[i]);
    tch.Install(d_r2[i]);
  }
  
  
  Ipv4AddressHelper address;  
  Ipv4InterfaceContainer ip_s_r1[m_nSources];
  Ipv4InterfaceContainer ip_d_r2[m_nSources];

  for (uint16_t i = 0; i < m_nSources; i++)
    {

      std::string ip = "10.1." + std::to_string (i + 2) + ".0";
      address.SetBase (ip.c_str (), "255.255.255.0");
      ip_s_r1[i] = address.Assign (s_r1[i]);

      std::string ip2 = "10.1." + std::to_string (i + 2 + m_nSources) + ".0";
      address.SetBase (ip2.c_str (), "255.255.255.0");
      ip_d_r2[i] = address.Assign (d_r2[i]);
    }

  address.SetBase ("10.1.1.0", "255.255.255.0");
  address.Assign (r1_r2);

  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  // Attaching sink to nodes
  uint16_t sinkPort = 8080;
  PacketSinkHelper packetSinkHelper ("ns3::TcpSocketFactory",
                                     InetSocketAddress (Ipv4Address::GetAny (), sinkPort));

  Address sinkAddress[m_nSources];
  ApplicationContainer sinkApp[m_nSources];

  for (uint16_t i = 0; i < m_nSources; i++)
    {
      sinkAddress[i] = *(new Address (InetSocketAddress (ip_d_r2[i].GetAddress (1), sinkPort)));
      sinkApp[i] = packetSinkHelper.Install (nodes.Get (2 + m_nSources + i));
      sinkApp[i].Start (Seconds (startTime));
      sinkApp[i].Stop (Seconds (stopTime));
    }

  // Adding time gap between start time of each flow using poisson distribution random variable
  double mean = 0.1; // more like a ~ 0.06
  double bound = 1;
  Ptr<ExponentialRandomVariable> expRandomVariable = CreateObject<ExponentialRandomVariable> ();
  expRandomVariable->SetAttribute ("Mean", DoubleValue (mean));
  expRandomVariable->SetAttribute ("Bound", DoubleValue (bound));

  // Configuring the application at each source node and specifying tcp flavour
  double stime = startTime;
  Ptr<Socket> ns3TcpSocket[m_nSources];
  ApplicationContainer sourceApps[m_nSources];
  TcpBbrV2::BbrVar bbr_variant = TcpBbrV2::BBR_V2;
  Config::SetDefault ("ns3::TcpBbrV2::BBRVariant", EnumValue (bbr_variant));
  
  
  for (uint16_t i = 0; i < m_nSources; i++)
    {

      BulkSendHelper tmp_source ("ns3::TcpSocketFactory", sinkAddress[i]);
      // Set the amount of data to send in bytes. 0 is unlimited.
      tmp_source.SetAttribute ("MaxBytes", UintegerValue (0));
      sourceApps[i] = tmp_source.Install (nodes.Get (2 + i));

      // Configure the tcp flavour
      TypeId tid = TypeId::LookupByName ("ns3::" + flavoursArray[i / noOfFlowWithSimilarFlavours]);
      std::stringstream nodeId;
      nodeId << nodes.Get (2 + i)->GetId ();
      std::string specificNode = "/NodeList/" + nodeId.str () + "/$ns3::TcpL4Protocol/SocketType";
      Config::Set (specificNode, TypeIdValue (tid));
      
      sourceApps[i].Stop (Seconds (stopTime));
    }

  Ptr<UniformRandomVariable> uniformRandomVariable = CreateObject<UniformRandomVariable> ();

  
  // Fetch random flows without repetition and set the start time

  
  // Initialize a set to keep track of picked flows
  std::set<int> picked_flows;
  std::vector<std::pair<int, double>> N_starts;
  
  while (picked_flows.size() < m_nSources) {
      int random_flow = uniformRandomVariable->GetInteger (0, m_nSources-1);
      if (picked_flows.find(random_flow) == picked_flows.end()) {
          sourceApps[random_flow].Start (Seconds (stime));
          N_starts.push_back(std::make_pair(random_flow, stime));
          //std::cout << random_flow << ":" << stime << std::endl;

          // Difference btw consecutive start time follows exp distb.
          double gap = expRandomVariable->GetValue ();
          stime += gap;
          picked_flows.insert(random_flow);
      }
  }

  //Configuring file stream to write the congestion windows sizes to.
  AsciiTraceHelper asciiCwnd;
  m_cwndStream = asciiCwnd.CreateFileStream (cwndTrFileName + ".txt");
  writeCwndHeadersToFile();


  AsciiTraceHelper ascii_dropped;
  m_dropped_stream = ascii_dropped.CreateFileStream (droppedTrFileName + ".txt");
  writeDestLostPacketsHeadersToFile();

  // Configuring file stream to write the throughput
  AsciiTraceHelper asciiBtlnck;
  btlnckThroughputvstimeStream = asciiBtlnck.CreateFileStream (btlnckThroughputFileName + ".txt");
  writeBtlnckHeadersToFile();

  // Configuring file stream to write the destination throughput
  AsciiTraceHelper asciiDest;
  m_destThroughputvstimeStream = asciiDest.CreateFileStream (destBtlnckThroughputFileName + ".txt");
  writeDestHeadersToFile();

  // Configuring file stream to write the Qsize
  AsciiTraceHelper asciiQsize;
  m_qsizeStream = asciiQsize.CreateFileStream (qsizeTrFileName + ".txt");

  // Configuring file stream to write the no of packets transmitted by the bottleneck
  AsciiTraceHelper asciiQsizeTx;
  m_btlnckTransmittedStream = asciiQsizeTx.CreateFileStream (bottleneckTxFileName + ".txt");

  // Configuring file streams to write the packets dropped
  AsciiTraceHelper asciiDropped;
  m_droppedStream = asciiDropped.CreateFileStream (droppedTrFileName + ".txt");

  // start tracing the congestion window size, qSize, dropped packets
  //Simulator::Schedule (Seconds (stime), &startTracingDroppedPacket);

  // TURN OFF CWND TRACING HERE FOR LARGE VALUES OF N
  if (disableCwndTracing == false){
    std::cout << "Tracing CWND" << std::endl;
    m_cwnd.resize (m_nSources+ 1, 0);
    Simulator::Schedule (Seconds (stime), &startTracingCwnd, m_nSources);
  }

  // Trace the queue occupancy on the second interface of R1
  tch.Uninstall (nodes.Get (0)->GetDevice (0));
  QueueDiscContainer qd;
  qd = tch.Install (nodes.Get (0)->GetDevice (0));

  // Perform periodically every second
  for (uint16_t time = stime; time < stopTime; time++)
    {
      if (disableCwndTracing == false)
        Simulator::Schedule (Seconds (time), &writeCwndValuesToFile);

      Simulator::Schedule (Seconds (time), &writeQueueSizeToFile, qd.Get(0));
      Simulator::Schedule (Seconds (time), &updateFlowStatsDest);
    }

  Simulator::Stop (Seconds (stopTime + cleanup_time));
  
  // Generate PCAP traces for the bottleneck if enabled
  if (enablePcap)
    {
      bottleneck.EnablePcap (pcapFileName, r1_r2);
    }


  // Enable flow monitor
  m_flowMonitor = m_flowHelper.InstallAll ();

  Simulator::Run ();
  
  Simulator::Destroy ();

  return 0;
}



/// LOST PACKETS

void
writeDestLostPacketsHeadersToFile ()
{
  // Write header: Time i0 t1 t2 ... tN-1
  *m_dropped_stream->GetStream () << "DestID";
  for (uint16_t i = 0; i < m_nSources; ++i)
    {
      *m_dropped_stream->GetStream () << "\t" << i;
    }
  *m_dropped_stream->GetStream () << std::endl;
}

void
writeDestLostPacketsToFile ()
{
  uint16_t currTime = Simulator::Now ().GetSeconds ();
  
  // Write throughput values for each flow
  *m_dropped_stream->GetStream () << currTime;
  for (uint16_t i = 0; i < m_nSources; ++i)
    {
      *m_dropped_stream->GetStream () << "\t" << m_destLostPacketsvstime[i];
    }
  *m_dropped_stream->GetStream () << std::endl;
}

void
startTracingCwnd (uint16_t m_nSources)
{
  for (uint16_t i = 0; i < m_nSources; i++)
    {
      //Trace changes to the congestion window
      Config::ConnectWithoutContext ("/NodeList/" + std::to_string (i + 2) +
                                         "/$ns3::TcpL4Protocol/SocketList/0/CongestionWindow",
                                     MakeBoundCallback (&updateCwnd, i));
    }
}

// CWND WINDOW

void
updateCwnd (uint16_t pos, uint32_t oldCwnd, uint32_t newCwnd)
{
  m_cwnd[pos] = newCwnd/m_pktSize;
}

void
writeCwndHeadersToFile ()
{
  // Write header: Time i0 t1 t2 ... tN-1
  *m_cwndStream->GetStream () << "DestID";
  for (uint16_t i = 0; i < m_nSources; ++i)
    {
      *m_cwndStream->GetStream ()  << "\t" << i;
    }
  *m_cwndStream->GetStream () << std::endl;
}

void
writeCwndValuesToFile ()
{
  // Write cwnd values for each flow
  uint32_t currTime = Simulator::Now ().GetSeconds ();
  *m_cwndStream->GetStream () << currTime;
  for (uint16_t i = 0; i < m_nSources; ++i)
    {
      *m_cwndStream->GetStream () << "\t" << m_cwnd[i];
    }
  *m_cwndStream->GetStream () << std::endl;
}


void
writeQueueSizeToFile (Ptr<QueueDisc> qd)
{
  uint32_t qsize = qd->GetCurrentSize ().GetValue ();
  *m_qsizeStream->GetStream () << Simulator::Now ().GetSeconds () << "\t" << qsize << std::endl;
}


/// BOTTLENECK THROUGHPUT

//Store Throughput for bottleneck to file
void
writeBtlnckHeadersToFile ()
{
  // Write header: Time i0 t1 t2 ... tN-1
  *btlnckThroughputvstimeStream->GetStream () << "DestID";
  // Only 1 bottleneck
  *btlnckThroughputvstimeStream->GetStream () << "\t" << 0;

  *btlnckThroughputvstimeStream->GetStream () << std::endl;
}

void
writeBottleneckThroughputToFile ()
{
  uint16_t currTime = Simulator::Now ().GetSeconds ();
  
  // Write throughput values for each flow
  *btlnckThroughputvstimeStream->GetStream () << currTime;
  
  *btlnckThroughputvstimeStream->GetStream () << "\t" << btlnckThroughputvstime;
    
  *btlnckThroughputvstimeStream->GetStream () << std::endl;
}



/// DESTINATION THROUGHPUT

void
writeDestHeadersToFile ()
{
  // Write header: Time i0 t1 t2 ... tN-1
  *m_destThroughputvstimeStream->GetStream () << "DestID";
  for (uint16_t i = 0; i < m_nSources; ++i)
    {
      *m_destThroughputvstimeStream->GetStream () << "\t" << i;
    }
  *m_destThroughputvstimeStream->GetStream () << std::endl;
}

void
writeDestThroughputToFile ()
{
  uint16_t currTime = Simulator::Now ().GetSeconds ();
  
  // Write throughput values for each flow
  *m_destThroughputvstimeStream->GetStream () << currTime;
  for (uint16_t i = 0; i < m_nSources; ++i)
    {
      *m_destThroughputvstimeStream->GetStream () << "\t" << m_destThroughputvstime[i];
    }
  *m_destThroughputvstimeStream->GetStream () << std::endl;
}

// This is called every second
void
updateFlowStatsDest ()
{
  static Time s_lastTime = Simulator::Now ();
  
  // Writing initial values to file ( mostly 0 )
  writeDestThroughputToFile();
  writeBottleneckThroughputToFile();
  writeDestLostPacketsToFile();

  // Get flow statistics
  m_flowMonitor->CheckForLostPackets ();
  Ptr<Ipv4FlowClassifier> classifier =
      DynamicCast<Ipv4FlowClassifier> (m_flowHelper.GetClassifier ());
  FlowMonitor::FlowStatsContainer stats = m_flowMonitor->GetFlowStats ();

  Time currentTime = Simulator::Now ();
  
  btlnckThroughputvstime = 0;
  // Iterate through flows and print throughput for the point-to-point device
  for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator flow = stats.begin ();
       flow != stats.end (); ++flow)
    {
      // Ignore the ACK flow from sink to source
      if (flow->first % 2 == 0)
        continue;
      
      uint16_t i = (flow->first - 1) / 2;
      // Skipping the first iteration so that a correct prev value of rxBytes(cumulative) can be set
      if ( currentTime == s_lastTime ) {
        m_totalBytesDest[i] = flow->second.rxBytes;
        continue;
      }
      // Calcuating
      uint64_t diff_bytes = flow->second.rxBytes - m_totalBytesDest[i];
      m_destThroughputvstime[i] = ((((diff_bytes) * 8.0)) / 1024) / 1024; // in Mbps
      // Save the rxBytes till now
      m_totalBytesDest[i] = flow->second.rxBytes;

      // Saving the dropped packets
      m_destLostPacketsvstime[i]  = flow->second.lostPackets - m_lostPacketsTotal[i];
      
      m_lostPacketsTotal[i] = flow->second.lostPackets;

      // Adding into the bottleneck throughput
      btlnckThroughputvstime += m_destThroughputvstime[i];
    }

}

std::vector<std::string>
splitString (std::string &str, char sep)
{
  std::vector<std::string> result;
  std::istringstream iss (str);
  std::string token;

  while (std::getline (iss, token, sep))
    {
      result.push_back (token);
    }

  return result;
}
