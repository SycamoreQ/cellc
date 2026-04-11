#!/bin/bash
# ============================================================
# BENCHMARK 4: Network Throughput
# Measures bandwidth and latency of container networking
# ============================================================

CELLC=${CELLC_BIN:-./cellc}
RESULTS_DIR="./benchmark_results"
mkdir -p "$RESULTS_DIR"
OUTPUT="$RESULTS_DIR/04_network_throughput.txt"

echo "============================================" | tee "$OUTPUT"
echo " BENCHMARK 4: Network Throughput & Latency" | tee -a "$OUTPUT"
echo " Date: $(date)"                              | tee -a "$OUTPUT"
echo "============================================" | tee -a "$OUTPUT"

# ── ping latency (host to container) ──────────
echo "" | tee -a "$OUTPUT"
echo "[cellc] Starting container for network benchmark..." | tee -a "$OUTPUT"

umount -l /root/container_fs/merged 2>/dev/null
rm -rf /root/container_fs/upper/* /root/container_fs/work/* 2>/dev/null
ip link delete vH_netbench 2>/dev/null
rmdir /sys/fs/cgroup/cellc 2>/dev/null

# start container in background
$CELLC run netbench /bin/sh -c "sleep 30" &
CELLC_PID=$!
sleep 2

echo "" | tee -a "$OUTPUT"
echo "[latency] Pinging container (10.0.0.2) from host..." | tee -a "$OUTPUT"
PING_RESULT=$(ping -c 20 10.0.0.2 2>/dev/null)
if [ $? -eq 0 ]; then
    PING_STATS=$(echo "$PING_RESULT" | tail -2)
    echo "$PING_STATS" | tee -a "$OUTPUT"
    
    # extract min/avg/max
    RTT=$(echo "$PING_RESULT" | grep "rtt min" | awk -F'/' '{print "min="$4"ms avg="$5"ms max="$6"ms"}')
    echo "[latency] RTT: $RTT" | tee -a "$OUTPUT"
else
    echo "[latency] Ping failed - container may not have network" | tee -a "$OUTPUT"
fi

echo "" | tee -a "$OUTPUT"
echo "[latency] Pinging host loopback (baseline)..." | tee -a "$OUTPUT"
PING_LO=$(ping -c 20 127.0.0.1 2>/dev/null | grep "rtt min" | awk -F'/' '{print "min="$4"ms avg="$5"ms max="$6"ms"}')
echo "[latency] Loopback RTT: $PING_LO" | tee -a "$OUTPUT"

# ── bandwidth test using /dev/zero ────────────
echo "" | tee -a "$OUTPUT"
echo "[bandwidth] Testing host->container bandwidth via veth..." | tee -a "$OUTPUT"

# use nc to test if available
if command -v nc &>/dev/null; then
    # start listener on host
    nc -l -p 9999 > /dev/null &
    NC_PID=$!
    sleep 0.5
    
    START=$(date +%s%N)
    dd if=/dev/zero bs=1M count=100 2>/dev/null | nc 10.0.0.1 9999 2>/dev/null
    END=$(date +%s%N)
    
    ELAPSED_MS=$(( (END - START) / 1000000 ))
    ELAPSED_S=$(echo "scale=2; $ELAPSED_MS / 1000" | bc)
    BANDWIDTH=$(echo "scale=2; 100 / $ELAPSED_S" | bc 2>/dev/null)
    
    echo "[bandwidth] Transferred 100MB in ${ELAPSED_MS}ms (~${BANDWIDTH} MB/s)" | tee -a "$OUTPUT"
    kill $NC_PID 2>/dev/null
else
    echo "[bandwidth] nc not available, skipping bandwidth test" | tee -a "$OUTPUT"
fi

# ── cgroup network stats ──────────────────────
echo "" | tee -a "$OUTPUT"
CONTAINER_PID=$(cat /run/cellc/containers/netbench/pid 2>/dev/null)
if [ -n "$CONTAINER_PID" ]; then
    echo "[stats] Container network interface stats:" | tee -a "$OUTPUT"
    cat /proc/$CONTAINER_PID/net/dev 2>/dev/null | grep -v "lo\|Inter\|face" | tee -a "$OUTPUT"
fi

# kill container
kill $CELLC_PID 2>/dev/null
wait $CELLC_PID 2>/dev/null

# ── Summary ───────────────────────────────────
echo "" | tee -a "$OUTPUT"
echo "============================================" | tee -a "$OUTPUT"
echo " RESULTS SUMMARY"                            | tee -a "$OUTPUT"
echo "============================================" | tee -a "$OUTPUT"
echo " Loopback RTT:    $PING_LO"    | tee -a "$OUTPUT"
echo " veth RTT:        $RTT"        | tee -a "$OUTPUT"
echo "" | tee -a "$OUTPUT"
echo "Results saved to $OUTPUT"
