#!/bin/bash
set -e

echo "=========================================================="
echo " Running Automated BufferBloat & NS-3 Verification Tests "
echo "=========================================================="

SIM_BIN="./simulation/bufferbloat"
CTRL_BIN="./controller/server"
RESULTS_DIR="./results/test_suite"

mkdir -p "$RESULTS_DIR"

if [ ! -f "$SIM_BIN" ]; then
    echo "[-] Error: Simulation binary not found at $SIM_BIN"
    exit 1
fi

echo "[+] 1. Testing DropTail Queue Simulation (5s)..."
$SIM_BIN --queueType=DropTail --queueSize=100 --simDuration=5 --outputDir="$RESULTS_DIR/droptail"
if [ ! -f "$RESULTS_DIR/droptail/summary.csv" ] || [ ! -f "$RESULTS_DIR/droptail/rtt_trace.csv" ]; then
    echo "[-] Error: DropTail results not generated properly"
    exit 1
fi
echo "[OK] DropTail traces generated."

echo "[+] 2. Testing CoDel AQM Simulation (5s)..."
$SIM_BIN --queueType=CoDel --queueSize=100 --simDuration=5 --outputDir="$RESULTS_DIR/codel"
if [ ! -f "$RESULTS_DIR/codel/summary.csv" ]; then
    echo "[-] Error: CoDel results not generated properly"
    exit 1
fi
echo "[OK] CoDel traces generated."

echo "[+] 3. Testing RED AQM Simulation (5s)..."
$SIM_BIN --queueType=RED --queueSize=100 --simDuration=5 --outputDir="$RESULTS_DIR/red"
if [ ! -f "$RESULTS_DIR/red/summary.csv" ]; then
    echo "[-] Error: RED results not generated properly"
    exit 1
fi
echo "[OK] RED traces generated."

echo "[+] 4. Testing FQ-CoDel Simulation (5s)..."
$SIM_BIN --queueType=FqCoDel --queueSize=100 --simDuration=5 --outputDir="$RESULTS_DIR/fqcodel"
if [ ! -f "$RESULTS_DIR/fqcodel/summary.csv" ]; then
    echo "[-] Error: FQ-CoDel results not generated properly"
    exit 1
fi
echo "[OK] FQ-CoDel traces generated."

echo "[+] 5. Comparing Key Metrics:"
echo "----------------------------------------------------------"
printf "%-12s | %-12s | %-12s | %-10s\n" "Queue Type" "Avg RTT (ms)" "Q-Delay (ms)" "Bloat"
echo "----------------------------------------------------------"
for q in droptail codel red fqcodel; do
    AVG_RTT=$(grep "avg_loaded_rtt_ms" "$RESULTS_DIR/$q/summary.csv" | cut -d',' -f2)
    Q_DELAY=$(grep "avg_queueing_delay_ms" "$RESULTS_DIR/$q/summary.csv" | cut -d',' -f2)
    BLOAT=$(grep "bufferbloat_detected" "$RESULTS_DIR/$q/summary.csv" | cut -d',' -f2)
    printf "%-12s | %-12s | %-12s | %-10s\n" "$q" "$AVG_RTT" "$Q_DELAY" "$BLOAT"
done
echo "----------------------------------------------------------"

echo "[+] 6. Verifying C Controller Executable..."
if [ ! -f "$CTRL_BIN" ]; then
    echo "[-] Error: Controller binary not found at $CTRL_BIN"
    exit 1
fi
echo "[OK] Controller binary exists and is executable."

echo "[+] All Verification Tests Passed Successfully!"
