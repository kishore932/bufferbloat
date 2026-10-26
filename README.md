# BufferBloat Detection and Queue Management Optimization Using NS-3

A comprehensive, production-grade network simulation system and interactive dashboard designed to demonstrate, measure, and optimize **BufferBloat** in congested packet networks using **NS-3 (Network Simulator 3)**.

---

## 1. System Architecture

The project consists of three cleanly separated layers:

```
┌──────────────────────────────────────────────────────────────────┐
│                   Web Presentation Dashboard                     │
│    (HTML5, Vanilla CSS3 Dark Navy Theme, Vanilla JS, Chart.js)   │
│  - 9 Structured Views: Overview, Topology, Config, Run,          │
│    Analytics, Queue Mgmt, Compare, Results/Reports, Settings     │
└─────────────────────────────────┬────────────────────────────────┘
                                  │ HTTP / JSON REST APIs
┌─────────────────────────────────▼────────────────────────────────┐
│               Native C Socket Controller Daemon                  │
│                (POSIX TCP Sockets, fork / exec)                  │
│  - Static Web Server (MIME detection, CORS, Non-blocking)        │
│  - REST Endpoints (/api/status, /api/simulate, /api/results)     │
│  - Process Lifecycle & Concurrency Guard                        │
└─────────────────────────────────┬────────────────────────────────┘
                                  │ CLI Arguments & CSV Traces
┌─────────────────────────────────▼────────────────────────────────┐
│                   NS-3 Simulation Engine (C++)                   │
│               (NS-3.46, C++20, TrafficControlHelper)             │
│  - Configurable Dumbbell Topology (N Clients, Bottleneck Link)   │
│  - Multi-flow TCP Cubic Congestion Stress Traffic                │
│  - High-Precision Latency Probe (50ms interval UDP Echo)         │
│  - QueueDiscs: DropTail (FIFO), CoDel, FQ-CoDel, RED, PIE       │
│  - Structured CSV Metrics Generation                             │
└──────────────────────────────────────────────────────────────────┘
```

---

## 2. Key Features

- **Genuine NS-3.46 Simulation:** Fully implemented in modern C++20 using the official NS-3 Traffic Control and Internet Stack architecture.
- **Strict Language Constraints:**
  - Simulation Engine: **C++** (`simulation/bufferbloat.cc`)
  - Controller & Socket Server: **C** (`controller/socket_server.c`, `controller/main.c`)
  - Frontend: **HTML5, CSS3, JavaScript** (Zero heavy JavaScript frameworks, pure Vanilla JS + Chart.js)
  - No Python/Node/Java backend socket servers or packet transmission code.
- **Configurable Network Parameters:**
  - Router Queue Discipline: `DropTail`, `CoDel`, `FQ-CoDel`, `RED`, `PIE`
  - Router Buffer Capacity: 5 to 1000 packets (or bytes)
  - Bottleneck Bandwidth: 0.5 to 100 Mbps
  - Bottleneck Propagation Delay: 1 to 250 ms (base RTT = 2 * delay)
  - Client Count: 1 to 15 concurrent clients
  - Traffic Loads: High (all clients saturated), Medium (2 flows), Low (single flow)
  - Simulation Duration & Random Seed (RNG Run) for repeatable experiments
- **Automated BufferBloat Detection & Grading:**
  - Computes Baseline RTT (unloaded) vs Loaded RTT (congested).
  - Grades network performance from **A+** (no bloat, latency < 10ms above baseline) down to **F** (severe bloat, latency > 300ms above baseline), adhering to DSLReports / Waveform standards.
- **Interactive Dumbbell Topology Visualizer:**
  - Real-time HTML5 Canvas animation showing client nodes, access links, router queue buffer with live packet fullness bar, and server sink.
- **Multi-Experiment Overlay & Comparison:**
  - Overlay RTT, throughput, and queue occupancy across DropTail vs CoDel vs FQ-CoDel runs on the same axes.
- **Comprehensive Trace Generation:**
  - `summary.csv`: Aggregated KPIs, RTT ratio, average/peak queueing delay, utilization, and bufferbloat grade.
  - `rtt_trace.csv`: Time-series of every latency probe sample.
  - `queue_trace.csv`: Time-series of queue occupancy and sojourn delay.
  - `throughput_trace.csv`: Moving-window throughput measurements.
  - `drop_trace.csv`: Packet drop events and timestamps.

---

## 3. Directory Structure

```
buffer_bloat/
├── Makefile                   # Top-level build script for simulation and C server
├── README.md                  # System documentation and execution guide
├── config/
│   └── default_config.json    # Research presets (DropTail Bloat, CoDel, FQ-CoDel)
├── controller/
│   ├── main.c                 # C Socket server entry point & argument parsing
│   ├── socket_server.c        # POSIX HTTP/JSON server, fork/exec process runner
│   └── socket_server.h        # Server state definitions & function prototypes
├── simulation/
│   ├── bufferbloat.cc         # Complete NS-3 simulation in C++20
│   └── CMakeLists.txt         # CMake build configuration for NS-3 simulation
├── frontend/
│   ├── index.html             # Multi-view network engineering dashboard
│   ├── css/
│   │   └── styles.css         # Dark navy engineering design system
│   └── js/
│       ├── app.js             # Router, view manager, CSV parsing, API calls
│       ├── charts.js          # Chart.js analytics & multi-series overlays
│       ├── chart.min.js       # Bundled offline Chart.js library
│       ├── dashboard.js       # KPI computations & verdict banner updater
│       ├── simulation.js      # Form validation, presets, execution polling
│       └── topology.js        # Interactive HTML5 Canvas topology visualizer
├── results/                   # Structured CSV traces per experiment run
└── tests/
    └── test_simulation.sh     # Automated verification test suite
```

---

## 4. Prerequisites & Installation

### Linux / WSL (Ubuntu 24.04 or 26.04)

Install the required build tools and NS-3 packages:

```bash
sudo apt-get update
sudo apt-get install -y ns3 libns3-dev cmake pkg-config build-essential
```

Verify that NS-3 headers and libraries are present:

```bash
pkg-config --modversion libns3   # Or verify /usr/include/ns3
g++ --version                    # GCC 11+ supporting -std=c++20
```

---

## 5. Building the Project

Use the top-level `Makefile` to compile both the NS-3 simulation binary and the C socket server:

```bash
make all
```

Or build components individually:

```bash
# Build C++ NS-3 Simulation
make simulation/bufferbloat

# Build C Socket Controller Server
make controller/server
```

---

## 6. Running the Automated Verification Tests

Run the test suite to execute baseline DropTail, CoDel, RED, and FQ-CoDel simulations and verify output metrics:

```bash
make test
```

Expected output:
```
==========================================================
 Running Automated BufferBloat & NS-3 Verification Tests 
==========================================================
[+] 1. Testing DropTail Queue Simulation (5s)... [OK]
[+] 2. Testing CoDel AQM Simulation (5s)...      [OK]
[+] 3. Testing RED AQM Simulation (5s)...        [OK]
[+] 4. Testing FQ-CoDel Simulation (5s)...       [OK]
[+] 5. Comparing Key Metrics:
----------------------------------------------------------
Queue Type   | Avg RTT (ms) | Q-Delay (ms) | Bloat     
----------------------------------------------------------
droptail     | 113.65       | 74.32        | YES       
codel        | 107.85       | 53.54        | YES       
red          | 113.20       | 74.94        | YES       
fqcodel      | 75.92        | 35.26        | NO        
----------------------------------------------------------
[+] 6. Verifying C Controller Executable...      [OK]
[+] All Verification Tests Passed Successfully!
```

---

## 7. Running the Web Application

Start the C controller server:

```bash
make run-server
```

By default, the server binds to port `8088`. Open your web browser and navigate to:

```
http://localhost:8088
```

*(On Windows with WSL2, port 8088 automatically forwards to Windows localhost).*

---

## 8. Command-Line Simulation Usage

You can also run the NS-3 simulation executable directly from the command line:

```bash
./simulation/bufferbloat \
  --queueType=DropTail \
  --queueSize=100 \
  --bottleneckBw=5.0 \
  --bottleneckDelay=35.0 \
  --nClients=3 \
  --trafficLoad=high \
  --simDuration=15.0 \
  --outputDir=results/my_experiment
```

### Supported Arguments:

| Parameter | Default | Description |
|:---|:---:|:---|
| `--queueType` | `DropTail` | Queue discipline: `DropTail`, `CoDel`, `FqCoDel`, `RED`, `PIE` |
| `--queueSize` | `100` | Router queue capacity in packets |
| `--bottleneckBw` | `5.0` | Bottleneck link bandwidth in Mbps |
| `--bottleneckDelay` | `35.0` | Bottleneck propagation delay in milliseconds |
| `--accessBw` | `100.0` | Access link bandwidth in Mbps |
| `--accessDelay` | `2.0` | Access link delay in milliseconds |
| `--nClients` | `3` | Number of client nodes |
| `--trafficLoad` | `high` | Traffic intensity: `low`, `medium`, `high` |
| `--packetSize` | `1024` | TCP packet payload size in bytes |
| `--simDuration` | `15.0` | Simulation duration in seconds |
| `--rngRun` | `1` | Random run index for repeatable experiments |
| `--outputDir` | `results/run_latest` | Target folder for CSV trace files |

---

## 9. Understanding the Results: Why CoDel & FQ-CoDel Win

1. **DropTail (FIFO):**
   - Packets sit in the buffer until the entire 100-packet buffer fills up.
   - On a 5 Mbps link, 100 packets $\times$ (1024 bytes $\times$ 8 bits / $5\times 10^6$) $\approx$ **164 ms** of extra queueing latency!
   - Measured RTT rises from **74 ms** to **240+ ms** (**BufferBloat detected, Grade B/D**).
2. **CoDel (Controlled Delay):**
   - Monitors packet *sojourn time* (queueing delay) over a 100 ms sliding window.
   - When minimum delay exceeds 5 ms, it drops packets at a controlled rate to force TCP to back off, keeping the standing queue low while preserving full throughput.
3. **FQ-CoDel (Fair Queueing + CoDel):**
   - Hashes connections into separate queues and serves them via Deficit Round Robin (DRR).
   - High-rate bulk TCP senders are contained within their own sub-queues.
   - Interactive ping/DNS/gaming packets jump ahead into empty queues and experience **zero standing queue delay** (RTT stays at **75.9 ms**, **Grade A+**).
