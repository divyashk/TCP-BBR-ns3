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

NS_LOG_COMPONENT_DEFINE ("TCPSCRIPT");

std::vector<uint32_t> cwnd;
Ptr<OutputStreamWrapper> cwndStream;

std::vector<double> destLostPacketsvstime;
std::vector<uint32_t> lostPacketsTotal;
Ptr<OutputStreamWrapper> dropped_stream;

uint16_t queueSize;
Ptr<OutputStreamWrapper> qsizeStream;

Ptr<FlowMonitor> flowMonitor;
FlowMonitorHelper flowHelper;

Ptr<OutputStreamWrapper> btlnckTransmittedStream;

Ptr<OutputStreamWrapper> droppedStream;

std::vector<uint64_t> totalBytesDest;
std::vector<double> destThroughputvstime;
Ptr<OutputStreamWrapper> destThroughputvstimeStream;

uint16_t nSources;

double_t btlnckThroughputvstime;
Ptr<OutputStreamWrapper> btlnckThroughputvstimeStream;

// Function prototypes
void updateCwnd (uint16_t pos, uint32_t oldCwnd, uint32_t newCwnd);
void startTracingCwnd (uint16_t numSources);
void writeCwndHeadersToFile ();
void writeCwndValuesToFile ();
void plotQueueSizeChange (uint32_t oldQSize, uint32_t newQSize);
void writeQueueSizeToFile ();
void startTracingQueueSize ();
void writeBottleneckThroughputToFile (uint16_t simDurationInSeconds);
void writeDestThroughputToFile ();
void writeDestLostPacketsToFile ();
void writeDestHeadersToFile();
void writeDestLostPacketsHeadersToFile();
void writeBtlnckHeadersToFile();
void updateFlowStatsDest ();
std::vector<std::string> splitString (std::string &str, char sep);

int
main (int argc, char *argv[])
{

  std::string bottleneckBandwidth = "100Mbps";
  std::string bottleneckDelay = "1ms";
  std::string accessBandwidth = "120Mbps";
  std::string rtts = "100ms_100ms";
  std::string sourceRate = "120Mbps";
  std::string flavours = "TcpBbr";
  std::string queueType = "DropTail"; //DropTail or CoDel
  std::string queueSize = "2084p"; //in packets

  uint16_t pktSize = 1400; //in Bytes. 1458 to prevent fragments
  uint16_t nPackets = 2000;

  uint16_t enbBotTrace = 0;
  float startTime = 0;
  float simDuration = 100; //in seconds
  uint16_t cleanup_time = 20;
  nSources = 60;
  btlnckThroughputvstime = 0;

  bool logging = false;

  std::string rootDir;
  std::string qsizeTrFileName;
  std::string cwndTrFileName;
  std::string droppedTrFileName;
  std::string bottleneckTxFileName;
  std::string btlnckThroughputFileName;
  std::string destBtlnckThroughputFileName;

  totalBytesDest.resize (nSources + 1, 0);
  lostPacketsTotal.resize (nSources + 1, 0);

  CommandLine cmd;
  cmd.AddValue ("bottleneckBandwidth", "Bottleneck bandwidth", bottleneckBandwidth);
  cmd.AddValue ("bottleneckDelay", "Bottleneck delay", bottleneckDelay);
  cmd.AddValue ("accessBandwidth", "Access link bandwidth", accessBandwidth);
  cmd.AddValue ("rtts", "mean rtts for random generation of delays in each link", rtts);
  cmd.AddValue ("queueType", "Queue type: DropTail, CoDel", queueType);
  cmd.AddValue ("queueSize", "Queue size in packets", queueSize);
  cmd.AddValue ("pktSize", "Packet size in bytes", pktSize);
  cmd.AddValue ("startTime", "Simulation start time", startTime);
  cmd.AddValue ("simDuration", "Simulation duration in seconds", simDuration);
  cmd.AddValue ("cwndTrFileName", "Name of cwnd trace file", cwndTrFileName);
  cmd.AddValue ("logging", "Flag to enable/disable logging", logging);
  cmd.AddValue ("flavours", "flavours to be used like - TcpBic, TcpBbr etc.", flavours);
  cmd.AddValue ("nPackets", "No. of Packets to send", nPackets);
  cmd.AddValue ("nSources", "No. of sources in the dumbell topology", nSources);
  cmd.AddValue ("sourceRate", "Rate of generating packets at the application level", sourceRate);
  cmd.AddValue ("enableBottleneckTrace", "Tracing the bottleneck packets", enbBotTrace);
  cmd.Parse (argc, argv);

  rootDir = "/mnt/Store/Project-summer/runtime/" + flavours + "/" + rtts + "/" +
            std::to_string (nSources) + "/";
  qsizeTrFileName = rootDir + "qsizeTrace";
  cwndTrFileName = rootDir + "cwndDropTail";
  droppedTrFileName = rootDir + "droppedPacketTrace";
  bottleneckTxFileName = rootDir + "bottleneckTx";
  btlnckThroughputFileName = rootDir + "btlnck_throughput";
  destBtlnckThroughputFileName = rootDir + "dest_throughput";

  destThroughputvstime.resize (nSources + 1);
  destLostPacketsvstime.resize (nSources + 1);
  cwnd.resize (nSources + 1);

  float stopTime = startTime + simDuration;

  std::string tcpModel ("ns3::" + flavours);

  // Converting the string rtts to an array of rtts
  std::vector<std::string> rttsArray = splitString (rtts, '_');
  int noOfFlowWithSimilarRTTs = nSources / rttsArray.size ();

  // Converting the string flavours to an array of flavours
  std::vector<std::string> flavoursArray = splitString (flavours, '_');
  int noOfFlowWithSimilarFlavours = nSources / flavoursArray.size ();

  if (flavoursArray.size () > 1 && rttsArray.size () > 1)
    {
      std::cout << "Can't have multiple rtts and flavours at the same time" << std::endl;
      return -1;
    }

  if (nSources % flavoursArray.size () != 0)
    {
      std::cout << "ERROR:Number of flows need to be a multiple of number of flavours" << std::endl;
      return -1;
    }

  if (nSources % rttsArray.size () != 0)
    {
      std::cout << "ERROR:Number of flows need to be a multiple of number of rtts" << std::endl;
      return -1;
    }

  // Configuring the packet size
  Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (pktSize));

  Config::SetDefault ("ns3::TcpSocket::InitialCwnd", UintegerValue (100));

  //Config::SetDefault("ns3::TcpSocketBase::MaxWindowSize", UintegerValue (20*1000));
  //Config::SetDefault ("ns3::TcpL4Protocol::SocketType", StringValue(tcpModel));

  // Defining the nodes
  NodeContainer nodes;
  nodes.Create (2 + nSources * 2);
  // Source nodes
  NodeContainer sr1[nSources];
  // Destination nodes
  NodeContainer dr2[nSources];

  NodeContainer r1r2 = NodeContainer (nodes.Get (0), nodes.Get (1));

  for (uint16_t i = 0; i < nSources; i++)
    {
      sr1[i] = NodeContainer (nodes.Get (i + 2), nodes.Get (0));
      dr2[i] = NodeContainer (nodes.Get (2 + nSources + i), nodes.Get (1));
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
  bottleneck.SetQueue ("ns3::DropTailQueue<Packet>", "MaxSize",
                       QueueSizeValue (QueueSize (queueSize))); // p in 1000p stands for packets
  bottleneck.DisableFlowControl ();

  PointToPointHelper p2p_s[nSources];
  PointToPointHelper p2p_d[nSources];
  for (uint16_t i = 0; i < nSources; i++)
    {
      // getting the appropriate delay value by using flow number
      double delay = (x[int (i / noOfFlowWithSimilarRTTs)]->GetValue ()) / 4;

      //std::cout << std::to_string(i/noOfFlowWithSimilarRTTs) + ":" << delay * 4 << std::endl;
      std::string delay_str = std::to_string (delay) + "ms";
      p2p_s[i].SetDeviceAttribute ("DataRate", StringValue (accessBandwidth));
      p2p_s[i].SetChannelAttribute ("Delay", StringValue (delay_str));
      
      
      p2p_d[i].SetDeviceAttribute ("DataRate", StringValue (accessBandwidth));
      p2p_d[i].SetChannelAttribute ("Delay", StringValue (delay_str));

      // Removing the queues from all the links except bottleneck
      p2p_s[i].SetQueue ("ns3::DropTailQueue<Packet>", "MaxSize",
                         QueueSizeValue (QueueSize (std::to_string (0) +
                                                    "p"))); // p in 1000p stands for packets
      p2p_d[i].SetQueue ("ns3::DropTailQueue<Packet>", "MaxSize",
                         QueueSizeValue (QueueSize (std::to_string (0) +
                                                    "p"))); // p in 1000p stands for packets
      p2p_s[i].DisableFlowControl ();
      p2p_d[i].DisableFlowControl ();
      
    }

  // Consists of both the link and nodes
  NetDeviceContainer r1_r2 = bottleneck.Install (r1r2);

  NetDeviceContainer s_r1[nSources];
  NetDeviceContainer d_r2[nSources];

  for (uint16_t i = 0; i < nSources; i++)
    {
      s_r1[i] = p2p_s[i].Install (sr1[i]);
      d_r2[i] = p2p_d[i].Install (dr2[i]);
    }

  //  Assigning ip address to each node.
  InternetStackHelper stack;
  stack.Install (nodes);
  Ipv4AddressHelper address;
  Ipv4InterfaceContainer ip_s_r1[nSources];
  Ipv4InterfaceContainer ip_d_r2[nSources];

  for (uint16_t i = 0; i < nSources; i++)
    {

      std::string ip = "10.1." + std::to_string (i + 2) + ".0";
      address.SetBase (ip.c_str (), "255.255.255.0");
      ip_s_r1[i] = address.Assign (s_r1[i]);

      std::string ip2 = "10.1." + std::to_string (i + 2 + nSources) + ".0";
      address.SetBase (ip2.c_str (), "255.255.255.0");
      ip_d_r2[i] = address.Assign (d_r2[i]);
    }

  address.SetBase ("10.1.1.0", "255.255.255.0");
  address.Assign (r1_r2);

  // Attaching sink to nodes
  uint16_t sinkPort = 8080;
  PacketSinkHelper packetSinkHelper ("ns3::TcpSocketFactory",
                                     InetSocketAddress (Ipv4Address::GetAny (), sinkPort));

  Address sinkAddress[nSources];
  ApplicationContainer sinkApp[nSources];

  for (uint16_t i = 0; i < nSources; i++)
    {
      sinkAddress[i] = *(new Address (InetSocketAddress (ip_d_r2[i].GetAddress (0), sinkPort)));
      sinkApp[i] = packetSinkHelper.Install (nodes.Get (2 + nSources + i));
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
  Ptr<Socket> ns3TcpSocket[nSources];
  ApplicationContainer sourceApps[nSources];
  for (uint16_t i = 0; i < nSources; i++)
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
      Ptr<Socket> localSocket =
          Socket::CreateSocket (nodes.Get (0), TcpSocketFactory::GetTypeId ());

      sourceApps[i].Start (Seconds (stime));
      sourceApps[i].Stop (Seconds (stopTime));
      double gap = expRandomVariable->GetValue ();

      stime += gap;

      // std::cout << gap << std::endl;
    }

  //Configuring file stream to write the congestion windows sizes to.
  AsciiTraceHelper asciiCwnd;
  cwndStream = asciiCwnd.CreateFileStream (cwndTrFileName + ".txt");
  writeCwndHeadersToFile();


  AsciiTraceHelper ascii_dropped;
  dropped_stream = ascii_dropped.CreateFileStream (droppedTrFileName + ".txt");
  writeDestLostPacketsHeadersToFile();

  // Configuring file stream to write the throughput
  AsciiTraceHelper asciiBtlnck;
  btlnckThroughputvstimeStream = asciiBtlnck.CreateFileStream (btlnckThroughputFileName + ".txt");
  writeBtlnckHeadersToFile();

  // Configuring file stream to write the destination throughput
  AsciiTraceHelper asciiDest;
  destThroughputvstimeStream = asciiDest.CreateFileStream (destBtlnckThroughputFileName + ".txt");
  writeDestHeadersToFile();

  // Configuring file stream to write the Qsize
  AsciiTraceHelper asciiQsize;
  qsizeStream = asciiQsize.CreateFileStream (qsizeTrFileName + ".txt");

  // Configuring file stream to write the no of packets transmitted by the bottleneck
  AsciiTraceHelper asciiQsizeTx;
  btlnckTransmittedStream = asciiQsizeTx.CreateFileStream (bottleneckTxFileName + ".txt");

  // Configuring file streams to write the packets dropped
  AsciiTraceHelper asciiDropped;
  droppedStream = asciiDropped.CreateFileStream (droppedTrFileName + ".txt");

  // start tracing the congestion window size, qSize, dropped packets
  //Simulator::Schedule (Seconds (stime), &startTracingDroppedPacket);
  Simulator::Schedule (Seconds (stime), &startTracingCwnd, nSources);
  Simulator::Schedule (Seconds (stime), &startTracingQueueSize);

  // Perform periodically every second
  for (uint16_t time = stime; time < stopTime; time++)
    {
      Simulator::Schedule (Seconds (time), &writeCwndValuesToFile);
      Simulator::Schedule (Seconds (time), &writeQueueSizeToFile);
      Simulator::Schedule (Seconds (time), &updateFlowStatsDest);
    }

  Simulator::Stop (Seconds (stopTime + cleanup_time));
  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  // Enable flow monitor
  flowMonitor = flowHelper.InstallAll ();

  Simulator::Run ();
  
  Simulator::Destroy ();

  return 0;
}



/// LOST PACKETS

void
writeDestLostPacketsHeadersToFile ()
{
  // Write header: Time i0 t1 t2 ... tN-1
  *dropped_stream->GetStream () << "DestID";
  for (uint16_t i = 0; i < nSources; ++i)
    {
      *dropped_stream->GetStream () << "\t" << i;
    }
  *dropped_stream->GetStream () << "\n";
}

void
writeDestLostPacketsToFile ()
{
  uint16_t currTime = Simulator::Now ().GetSeconds ();
  
  // Write throughput values for each flow
  *dropped_stream->GetStream () << currTime;
  for (uint16_t i = 0; i < nSources; ++i)
    {
      *dropped_stream->GetStream () << "\t" << destLostPacketsvstime[i];
    }
  *dropped_stream->GetStream () << "\n";
}

void
startTracingCwnd (uint16_t nSources)
{
  for (uint16_t i = 0; i < nSources; i++)
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
  cwnd[pos] = newCwnd;
}

void
writeCwndHeadersToFile ()
{
  // Write header: Time i0 t1 t2 ... tN-1
  *cwndStream->GetStream () << "DestID";
  for (uint16_t i = 0; i < nSources; ++i)
    {
      *cwndStream->GetStream ()  << "\t" << i;
    }
  *cwndStream->GetStream () << "\n";
}

void
writeCwndValuesToFile ()
{
  // Write cwnd values for each flow
  uint32_t currTime = Simulator::Now ().GetSeconds ();
  *cwndStream->GetStream () << currTime;
  for (uint16_t i = 0; i < nSources; ++i)
    {
      *cwndStream->GetStream () << "\t" << cwnd[i];
    }
  *cwndStream->GetStream () << "\n";
}

void
plotQueueSizeChange (uint32_t oldQSize, uint32_t newQSize)
{
  //NS_LOG_UNCOND(Simulator::Now().GetSeconds() << "\t" << newCwnd);

  queueSize = newQSize;
}

void
writeQueueSizeToFile ()
{
  *qsizeStream->GetStream () << Simulator::Now ().GetSeconds () << "\t" << queueSize << std::endl;
}

void
startTracingQueueSize ()
{
  Config::ConnectWithoutContext (
      "/NodeList/0/DeviceList/0/$ns3::PointToPointNetDevice/TxQueue/PacketsInQueue",
      MakeCallback (&plotQueueSizeChange));
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

  *btlnckThroughputvstimeStream->GetStream () << "\n";
}

void
writeBottleneckThroughputToFile ()
{
  uint16_t currTime = Simulator::Now ().GetSeconds ();
  
  // Write throughput values for each flow
  *btlnckThroughputvstimeStream->GetStream () << currTime;
  
  *btlnckThroughputvstimeStream->GetStream () << "\t" << btlnckThroughputvstime;
    
  *btlnckThroughputvstimeStream->GetStream () << "\n";
}



/// DESTINATION THROUGHPUT

void
writeDestHeadersToFile ()
{
  // Write header: Time i0 t1 t2 ... tN-1
  *destThroughputvstimeStream->GetStream () << "DestID";
  for (uint16_t i = 0; i < nSources; ++i)
    {
      *destThroughputvstimeStream->GetStream () << "\t" << i;
    }
  *destThroughputvstimeStream->GetStream () << "\n";
}

void
writeDestThroughputToFile ()
{
  uint16_t currTime = Simulator::Now ().GetSeconds ();
  
  // Write throughput values for each flow
  *destThroughputvstimeStream->GetStream () << currTime;
  for (uint16_t i = 0; i < nSources; ++i)
    {
      *destThroughputvstimeStream->GetStream () << "\t" << destThroughputvstime[i];
    }
  *destThroughputvstimeStream->GetStream () << "\n";
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
  flowMonitor->CheckForLostPackets ();
  Ptr<Ipv4FlowClassifier> classifier =
      DynamicCast<Ipv4FlowClassifier> (flowHelper.GetClassifier ());
  FlowMonitor::FlowStatsContainer stats = flowMonitor->GetFlowStats ();

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
        totalBytesDest[i] = flow->second.rxBytes;
        continue;
      }
      // Calcuating
      uint64_t diff_bytes = flow->second.rxBytes - totalBytesDest[i];
      destThroughputvstime[i] = ((((diff_bytes) * 8.0)) / 1024) / 1024; // in Mbps
      // Save the rxBytes till now
      totalBytesDest[i] = flow->second.rxBytes;

      // Saving the dropped packets
      destLostPacketsvstime[i]  = flow->second.lostPackets - lostPacketsTotal[i];
      
      lostPacketsTotal[i] = flow->second.lostPackets;

      // Adding into the bottleneck throughput
      btlnckThroughputvstime += destThroughputvstime[i];
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
