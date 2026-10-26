#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <cmath>
#include <iomanip>
#include <filesystem>

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/traffic-control-module.h"

using namespace ns3;
namespace fs = std::filesystem;

// Global tracking variables
static std::string g_outputDir = "results/run_latest";
static uint32_t g_queueSize = 100;
static std::string g_queueType = "DropTail";
static double g_bottleneckBwMbps = 5.0;
static double g_bottleneckDelayMs = 35.0;
static double g_accessBwMbps = 100.0;
static double g_accessDelayMs = 2.0;
static uint32_t g_nClients = 3;
static std::string g_trafficLoad = "high";
static uint32_t g_packetSize = 1024;
static double g_simDuration = 15.0;
static uint32_t g_rngRun = 1;

// Metrics collectors
static std::ofstream g_rttFile;
static std::ofstream g_queueFile;
static std::ofstream g_throughputFile;
static std::ofstream g_dropFile;

static std::map<uint32_t, Time> g_probeSentTimes;
static std::vector<double> g_rttSamples;
static std::vector<double> g_baselineRttSamples;
static std::vector<double> g_loadedRttSamples;
static std::vector<double> g_queueOccupancySamples;
static std::vector<double> g_queueDelaySamples;
static std::vector<double> g_throughputSamples;

static uint64_t g_totalRxBytes = 0;
static uint64_t g_prevRxBytes = 0;
static uint64_t g_totalTxPackets = 0;
static uint64_t g_totalRxPackets = 0;
static uint64_t g_totalDroppedPackets = 0;
static double g_latestSojournMs = 0.0;
static uint32_t g_maxQueueObserved = 0;
static double g_maxQueueDelayObserved = 0.0;

// Trace callbacks
void SojournTimeTrace(Time sojourn) {
    double ms = sojourn.GetSeconds() * 1000.0;
    g_latestSojournMs = ms;
    g_queueDelaySamples.push_back(ms);
    if (ms > g_maxQueueDelayObserved) {
        g_maxQueueDelayObserved = ms;
    }
}

void QueueDropTrace(Ptr<const QueueDiscItem> item) {
    g_totalDroppedPackets++;
    double nowSec = Simulator::Now().GetSeconds();
    if (g_dropFile.is_open()) {
        g_dropFile << std::fixed << std::setprecision(4)
                   << nowSec << "," << g_totalDroppedPackets << ",QueueOverflow" << std::endl;
    }
}

void ProbeTxTrace(Ptr<const Packet> packet) {
    uint32_t uid = packet->GetUid();
    g_probeSentTimes[uid] = Simulator::Now();
    g_totalTxPackets++;
}

void ProbeRxTrace(Ptr<const Packet> packet) {
    g_totalRxPackets++;
    uint32_t uid = packet->GetUid();
    auto it = g_probeSentTimes.find(uid);
    if (it != g_probeSentTimes.end()) {
        Time now = Simulator::Now();
        double rttMs = (now - it->second).GetSeconds() * 1000.0;
        double nowSec = now.GetSeconds();
        g_rttSamples.push_back(rttMs);

        // First 1.5 seconds are before congestion starts
        if (nowSec < 1.5) {
            g_baselineRttSamples.push_back(rttMs);
        } else {
            g_loadedRttSamples.push_back(rttMs);
        }

        if (g_rttFile.is_open()) {
            g_rttFile << std::fixed << std::setprecision(4)
                      << nowSec << "," << rttMs << "," << uid << std::endl;
        }
        g_probeSentTimes.erase(it);
    }
}

void BulkTxTrace(Ptr<const Packet> packet) {
    g_totalTxPackets++;
}

void BulkRxTrace(Ptr<const Packet> packet, const Address& from) {
    g_totalRxPackets++;
    g_totalRxBytes += packet->GetSize();
}

// Periodic sampling functions
void SampleQueue(Ptr<QueueDisc> queueDisc) {
    if (!queueDisc) return;
    double nowSec = Simulator::Now().GetSeconds();
    uint32_t qPackets = queueDisc->GetNPackets();
    uint32_t qBytes = queueDisc->GetNBytes();

    g_queueOccupancySamples.push_back(qPackets);
    if (qPackets > g_maxQueueObserved) {
        g_maxQueueObserved = qPackets;
    }

    if (g_queueFile.is_open()) {
        g_queueFile << std::fixed << std::setprecision(4)
                    << nowSec << "," << qPackets << "," << qBytes << ","
                    << g_latestSojournMs << std::endl;
    }

    if (nowSec + 0.05 < g_simDuration) {
        Simulator::Schedule(MilliSeconds(50), &SampleQueue, queueDisc);
    }
}

void SampleThroughput(double intervalSec) {
    double nowSec = Simulator::Now().GetSeconds();
    uint64_t bytesInInterval = g_totalRxBytes - g_prevRxBytes;
    g_prevRxBytes = g_totalRxBytes;

    double throughputMbps = (bytesInInterval * 8.0) / (intervalSec * 1000000.0);
    g_throughputSamples.push_back(throughputMbps);

    if (g_throughputFile.is_open()) {
        g_throughputFile << std::fixed << std::setprecision(4)
                         << nowSec << "," << throughputMbps << "," << bytesInInterval << std::endl;
    }

    if (nowSec + intervalSec < g_simDuration) {
        Simulator::Schedule(Seconds(intervalSec), &SampleThroughput, intervalSec);
    }
}

int main(int argc, char* argv[]) {
    CommandLine cmd(__FILE__);
    cmd.AddValue("nClients", "Number of client nodes", g_nClients);
    cmd.AddValue("bottleneckBw", "Bottleneck bandwidth (Mbps)", g_bottleneckBwMbps);
    cmd.AddValue("bottleneckDelay", "Bottleneck propagation delay (ms)", g_bottleneckDelayMs);
    cmd.AddValue("accessBw", "Access link bandwidth (Mbps)", g_accessBwMbps);
    cmd.AddValue("accessDelay", "Access link delay (ms)", g_accessDelayMs);
    cmd.AddValue("queueType", "Queue Discipline (DropTail, CoDel, RED, FqCoDel, PIE)", g_queueType);
    cmd.AddValue("queueSize", "Queue limit in packets", g_queueSize);
    cmd.AddValue("trafficLoad", "Traffic load (low, medium, high)", g_trafficLoad);
    cmd.AddValue("packetSize", "Packet size in bytes", g_packetSize);
    cmd.AddValue("simDuration", "Simulation duration in seconds", g_simDuration);
    cmd.AddValue("rngRun", "RNG run index / seed", g_rngRun);
    cmd.AddValue("outputDir", "Output directory for CSV results", g_outputDir);
    cmd.Parse(argc, argv);

    // Set Random Seed
    SeedManager::SetSeed(1);
    SeedManager::SetRun(g_rngRun);

    // Ensure output directory exists
    fs::create_directories(g_outputDir);

    // Open trace files
    g_rttFile.open(g_outputDir + "/rtt_trace.csv");
    g_rttFile << "time_sec,rtt_ms,probe_seq" << std::endl;

    g_queueFile.open(g_outputDir + "/queue_trace.csv");
    g_queueFile << "time_sec,packets_in_queue,bytes_in_queue,queue_delay_ms" << std::endl;

    g_throughputFile.open(g_outputDir + "/throughput_trace.csv");
    g_throughputFile << "time_sec,throughput_mbps,bytes_in_window" << std::endl;

    g_dropFile.open(g_outputDir + "/drop_trace.csv");
    g_dropFile << "time_sec,drop_count,reason" << std::endl;

    // TCP configuration
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(g_packetSize));
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(2097152)); // 2MB buffer to allow high BDP
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(2097152));
    Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpCubic"));

    // 1. Create Nodes
    NodeContainer clients;
    clients.Create(g_nClients);

    Ptr<Node> router = CreateObject<Node>();
    Ptr<Node> server = CreateObject<Node>();

    // 2. Setup Point-to-Point Links
    // Access Links: Clients -> Router
    PointToPointHelper accessLink;
    accessLink.SetDeviceAttribute("DataRate", StringValue(std::to_string(g_accessBwMbps) + "Mbps"));
    accessLink.SetChannelAttribute("Delay", StringValue(std::to_string(g_accessDelayMs) + "ms"));

    std::vector<NetDeviceContainer> clientDevices(g_nClients);
    for (uint32_t i = 0; i < g_nClients; ++i) {
        clientDevices[i] = accessLink.Install(clients.Get(i), router);
    }

    // Bottleneck Link: Router -> Server
    PointToPointHelper bottleneckLink;
    bottleneckLink.SetDeviceAttribute("DataRate", StringValue(std::to_string(g_bottleneckBwMbps) + "Mbps"));
    bottleneckLink.SetChannelAttribute("Delay", StringValue(std::to_string(g_bottleneckDelayMs) + "ms"));
    // Set low internal device queue so that TrafficControl / QueueDisc handles the buffering
    bottleneckLink.SetQueue("ns3::DropTailQueue<Packet>", "MaxSize", QueueSizeValue(QueueSize("1p")));

    NetDeviceContainer bottleneckDevices = bottleneckLink.Install(router, server);

    // 3. Install Internet Stack
    InternetStackHelper stack;
    stack.Install(clients);
    stack.Install(router);
    stack.Install(server);

    // 4. Install Queue Discipline on Router Bottleneck Interface
    TrafficControlHelper tch;
    std::string qTypeUpper = g_queueType;
    for (auto &c : qTypeUpper) c = toupper(c);

    if (qTypeUpper == "CODEL") {
        tch.SetRootQueueDisc("ns3::CoDelQueueDisc",
                             "MaxSize", QueueSizeValue(QueueSize(QueueSizeUnit::PACKETS, g_queueSize)),
                             "Target", StringValue("5ms"),
                             "Interval", StringValue("100ms"));
    } else if (qTypeUpper == "RED") {
        tch.SetRootQueueDisc("ns3::RedQueueDisc",
                             "MaxSize", QueueSizeValue(QueueSize(QueueSizeUnit::PACKETS, g_queueSize)),
                             "MinTh", DoubleValue(std::max(5.0, g_queueSize * 0.2)),
                             "MaxTh", DoubleValue(std::max(15.0, g_queueSize * 0.8)));
    } else if (qTypeUpper == "FQCODEL" || qTypeUpper == "FQ-CODEL") {
        tch.SetRootQueueDisc("ns3::FqCoDelQueueDisc",
                             "MaxSize", QueueSizeValue(QueueSize(QueueSizeUnit::PACKETS, g_queueSize)));
    } else if (qTypeUpper == "PIE") {
        tch.SetRootQueueDisc("ns3::PieQueueDisc",
                             "MaxSize", QueueSizeValue(QueueSize(QueueSizeUnit::PACKETS, g_queueSize)));
    } else {
        // Default: DropTail (FIFO)
        tch.SetRootQueueDisc("ns3::FifoQueueDisc",
                             "MaxSize", QueueSizeValue(QueueSize(QueueSizeUnit::PACKETS, g_queueSize)));
    }

    QueueDiscContainer qdiscs = tch.Install(bottleneckDevices.Get(0));
    Ptr<QueueDisc> routerQueueDisc = qdiscs.Get(0);

    // Hook queue traces
    routerQueueDisc->TraceConnectWithoutContext("SojournTime", MakeCallback(&SojournTimeTrace));
    routerQueueDisc->TraceConnectWithoutContext("Drop", MakeCallback(&QueueDropTrace));

    // 5. IP Addresses
    Ipv4AddressHelper ipv4;
    std::vector<Ipv4InterfaceContainer> clientInterfaces(g_nClients);
    for (uint32_t i = 0; i < g_nClients; ++i) {
        std::string subnet = "10.1." + std::to_string(i + 1) + ".0";
        ipv4.SetBase(Ipv4Address(subnet.c_str()), Ipv4Mask("255.255.255.0"));
        clientInterfaces[i] = ipv4.Assign(clientDevices[i]);
    }

    ipv4.SetBase("10.2.0.0", "255.255.255.0");
    Ipv4InterfaceContainer bottleneckInterfaces = ipv4.Assign(bottleneckDevices);
    Ipv4Address serverIp = bottleneckInterfaces.GetAddress(1);

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // 6. Server Sink Applications
    uint16_t tcpSinkPort = 8080;
    PacketSinkHelper tcpSink("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), tcpSinkPort));
    ApplicationContainer tcpSinkApp = tcpSink.Install(server);
    tcpSinkApp.Start(Seconds(0.0));
    tcpSinkApp.Stop(Seconds(g_simDuration));

    // Connect bulk packet receive trace
    Ptr<PacketSink> sinkPtr = DynamicCast<PacketSink>(tcpSinkApp.Get(0));
    if (sinkPtr) {
        sinkPtr->TraceConnectWithoutContext("Rx", MakeCallback(&BulkRxTrace));
    }

    // UDP Echo Server for RTT probes
    uint16_t udpEchoPort = 9000;
    UdpEchoServerHelper echoServer(udpEchoPort);
    ApplicationContainer echoServerApp = echoServer.Install(server);
    echoServerApp.Start(Seconds(0.0));
    echoServerApp.Stop(Seconds(g_simDuration));

    // 7. Client Applications
    // Client 0 always runs the high-precision RTT probe
    UdpEchoClientHelper echoClient(serverIp, udpEchoPort);
    echoClient.SetAttribute("MaxPackets", UintegerValue(1000000));
    echoClient.SetAttribute("Interval", TimeValue(MilliSeconds(50))); // 50ms probe interval
    echoClient.SetAttribute("PacketSize", UintegerValue(64));
    ApplicationContainer echoClientApp = echoClient.Install(clients.Get(0));
    echoClientApp.Start(Seconds(0.1));
    echoClientApp.Stop(Seconds(g_simDuration - 0.1));

    // Hook probe client traces
    Ptr<UdpEchoClient> clientPtr = DynamicCast<UdpEchoClient>(echoClientApp.Get(0));
    if (clientPtr) {
        clientPtr->TraceConnectWithoutContext("Tx", MakeCallback(&ProbeTxTrace));
        clientPtr->TraceConnectWithoutContext("Rx", MakeCallback(&ProbeRxTrace));
    }

    // Bulk Traffic Senders (creating congestion at bottleneck)
    uint32_t activeSenders = 1;
    if (g_trafficLoad == "medium") {
        activeSenders = std::min(g_nClients, 2u);
    } else if (g_trafficLoad == "high") {
        activeSenders = g_nClients;
    }

    double bulkStartTime = 2.0; // Starts at 2s to allow baseline RTT measurement first
    double bulkStopTime = g_simDuration - 0.5;

    for (uint32_t i = 0; i < activeSenders; ++i) {
        BulkSendHelper bulkSend("ns3::TcpSocketFactory", InetSocketAddress(serverIp, tcpSinkPort));
        bulkSend.SetAttribute("MaxBytes", UintegerValue(0)); // unlimited
        bulkSend.SetAttribute("SendSize", UintegerValue(g_packetSize));
        ApplicationContainer bulkApp = bulkSend.Install(clients.Get(i));
        // Stagger sender start slightly
        bulkApp.Start(Seconds(bulkStartTime + i * 0.1));
        bulkApp.Stop(Seconds(bulkStopTime));

        Ptr<BulkSendApplication> bulkPtr = DynamicCast<BulkSendApplication>(bulkApp.Get(0));
        if (bulkPtr) {
            bulkPtr->TraceConnectWithoutContext("Tx", MakeCallback(&BulkTxTrace));
        }
    }

    // 8. Schedule periodic monitors
    Simulator::Schedule(MilliSeconds(100), &SampleQueue, routerQueueDisc);
    Simulator::Schedule(Seconds(0.2), &SampleThroughput, 0.2); // 200ms throughput window

    // 9. Run Simulation
    Simulator::Stop(Seconds(g_simDuration));
    Simulator::Run();
    Simulator::Destroy();

    // Close trace streams
    if (g_rttFile.is_open()) g_rttFile.close();
    if (g_queueFile.is_open()) g_queueFile.close();
    if (g_throughputFile.is_open()) g_throughputFile.close();
    if (g_dropFile.is_open()) g_dropFile.close();

    // 10. Compute Summary Statistics
    auto calcMean = [](const std::vector<double>& v) -> double {
        if (v.empty()) return 0.0;
        double sum = 0.0;
        for (double val : v) sum += val;
        return sum / v.size();
    };

    auto calcMin = [](const std::vector<double>& v) -> double {
        if (v.empty()) return 0.0;
        double m = v[0];
        for (double val : v) if (val < m) m = val;
        return m;
    };

    auto calcMax = [](const std::vector<double>& v) -> double {
        if (v.empty()) return 0.0;
        double m = v[0];
        for (double val : v) if (val > m) m = val;
        return m;
    };

    double baselineRtt = calcMean(g_baselineRttSamples);
    if (baselineRtt <= 0.0) {
        // theoretical baseline: 2 * (accessDelay + bottleneckDelay)
        baselineRtt = 2.0 * (g_accessDelayMs + g_bottleneckDelayMs);
    }

    double loadedRtt = calcMean(g_loadedRttSamples);
    if (loadedRtt <= 0.0) loadedRtt = baselineRtt;

    double peakRtt = calcMax(g_rttSamples);
    double minRtt = calcMin(g_rttSamples);
    double rttIncreaseRatio = (baselineRtt > 0.0) ? (loadedRtt / baselineRtt) : 1.0;

    double avgQueueOccupancy = calcMean(g_queueOccupancySamples);
    double avgQueueDelay = calcMean(g_queueDelaySamples);
    double avgThroughput = calcMean(g_throughputSamples);
    double peakThroughput = calcMax(g_throughputSamples);

    double bottleneckUtilization = (avgThroughput / g_bottleneckBwMbps) * 100.0;
    if (bottleneckUtilization > 100.0) bottleneckUtilization = 100.0;

    double packetLossPercent = 0.0;
    if (g_totalTxPackets > 0) {
        packetLossPercent = ((double)g_totalDroppedPackets / (double)g_totalTxPackets) * 100.0;
    }

    // BufferBloat Detection Logic (Documented Thresholds)
    // Detection criterion:
    // 1. RTT loaded is at least 2x baseline RTT (rttIncreaseRatio >= 2.0)
    // 2. Average queueing delay exceeds 50ms
    bool bufferbloatDetected = (rttIncreaseRatio >= 2.0 && avgQueueDelay >= 40.0) || (peakRtt > baselineRtt + 150.0);

    // BufferBloat Grade (Industry standard like DSLReports/Waveform)
    // Extra latency added under load:
    double deltaRtt = std::max(0.0, loadedRtt - baselineRtt);
    std::string grade = "A+";
    if (deltaRtt < 10.0) {
        grade = "A+";
    } else if (deltaRtt < 30.0) {
        grade = "A";
    } else if (deltaRtt < 70.0) {
        grade = "B";
    } else if (deltaRtt < 150.0) {
        grade = "C";
    } else if (deltaRtt < 300.0) {
        grade = "D";
    } else {
        grade = "F";
    }

    // Write summary.csv
    std::ofstream summaryFile(g_outputDir + "/summary.csv");
    summaryFile << "metric,value,unit" << std::endl;
    summaryFile << "queue_type," << g_queueType << ",string" << std::endl;
    summaryFile << "queue_capacity_packets," << g_queueSize << ",packets" << std::endl;
    summaryFile << "bottleneck_bw_mbps," << std::fixed << std::setprecision(2) << g_bottleneckBwMbps << ",Mbps" << std::endl;
    summaryFile << "bottleneck_delay_ms," << std::fixed << std::setprecision(2) << g_bottleneckDelayMs << ",ms" << std::endl;
    summaryFile << "access_bw_mbps," << std::fixed << std::setprecision(2) << g_accessBwMbps << ",Mbps" << std::endl;
    summaryFile << "access_delay_ms," << std::fixed << std::setprecision(2) << g_accessDelayMs << ",ms" << std::endl;
    summaryFile << "num_clients," << g_nClients << ",nodes" << std::endl;
    summaryFile << "traffic_load," << g_trafficLoad << ",level" << std::endl;
    summaryFile << "sim_duration_sec," << std::fixed << std::setprecision(1) << g_simDuration << ",s" << std::endl;
    summaryFile << "baseline_rtt_ms," << std::fixed << std::setprecision(2) << baselineRtt << ",ms" << std::endl;
    summaryFile << "avg_loaded_rtt_ms," << std::fixed << std::setprecision(2) << loadedRtt << ",ms" << std::endl;
    summaryFile << "peak_rtt_ms," << std::fixed << std::setprecision(2) << peakRtt << ",ms" << std::endl;
    summaryFile << "min_rtt_ms," << std::fixed << std::setprecision(2) << minRtt << ",ms" << std::endl;
    summaryFile << "rtt_increase_ratio," << std::fixed << std::setprecision(2) << rttIncreaseRatio << ",ratio" << std::endl;
    summaryFile << "avg_queue_occupancy_pkts," << std::fixed << std::setprecision(2) << avgQueueOccupancy << ",packets" << std::endl;
    summaryFile << "max_queue_occupancy_pkts," << g_maxQueueObserved << ",packets" << std::endl;
    summaryFile << "avg_queueing_delay_ms," << std::fixed << std::setprecision(2) << avgQueueDelay << ",ms" << std::endl;
    summaryFile << "max_queueing_delay_ms," << std::fixed << std::setprecision(2) << g_maxQueueDelayObserved << ",ms" << std::endl;
    summaryFile << "avg_throughput_mbps," << std::fixed << std::setprecision(2) << avgThroughput << ",Mbps" << std::endl;
    summaryFile << "peak_throughput_mbps," << std::fixed << std::setprecision(2) << peakThroughput << ",Mbps" << std::endl;
    summaryFile << "bottleneck_utilization_percent," << std::fixed << std::setprecision(2) << bottleneckUtilization << ",%" << std::endl;
    summaryFile << "total_packets_sent," << g_totalTxPackets << ",packets" << std::endl;
    summaryFile << "total_packets_received," << g_totalRxPackets << ",packets" << std::endl;
    summaryFile << "packets_dropped," << g_totalDroppedPackets << ",packets" << std::endl;
    summaryFile << "packet_loss_percent," << std::fixed << std::setprecision(2) << packetLossPercent << ",%" << std::endl;
    summaryFile << "bufferbloat_detected," << (bufferbloatDetected ? "YES" : "NO") << ",flag" << std::endl;
    summaryFile << "bufferbloat_grade," << grade << ",grade" << std::endl;
    summaryFile.close();

    // Print summary to stdout for process controller / CLI
    std::cout << "=== NS-3 BufferBloat Simulation Completed Successfully ===" << std::endl;
    std::cout << "Queue Type: " << g_queueType << " | Buffer Size: " << g_queueSize << " pkts" << std::endl;
    std::cout << "Baseline RTT: " << baselineRtt << " ms | Loaded RTT: " << loadedRtt << " ms" << std::endl;
    std::cout << "Peak RTT: " << peakRtt << " ms | RTT Ratio: " << rttIncreaseRatio << "x" << std::endl;
    std::cout << "Avg Queueing Delay: " << avgQueueDelay << " ms | Peak Queueing Delay: " << g_maxQueueDelayObserved << " ms" << std::endl;
    std::cout << "Avg Throughput: " << avgThroughput << " Mbps | Utilization: " << bottleneckUtilization << "%" << std::endl;
    std::cout << "Packet Loss: " << packetLossPercent << "% (" << g_totalDroppedPackets << " dropped)" << std::endl;
    std::cout << "BufferBloat Detected: " << (bufferbloatDetected ? "YES" : "NO") << " | Grade: " << grade << std::endl;
    std::cout << "Results saved to: " << g_outputDir << std::endl;

    return 0;
}
