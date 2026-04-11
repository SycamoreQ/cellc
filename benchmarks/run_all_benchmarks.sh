#!/bin/bash
# ============================================================
# cellc Master Benchmark Runner
# Runs all benchmarks and produces a final summary report
# 
# Usage:
#   sudo ./run_all_benchmarks.sh
#
# Requirements:
#   - Must be run as root
#   - cellc binary must exist at ./cellc
#   - Alpine rootfs at /root/container_fs/alpine_base
# ============================================================

set -e

CELLC_BIN="./cellc"
RESULTS_DIR="./benchmark_results"
SUMMARY="$RESULTS_DIR/SUMMARY.txt"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ── preflight checks ──────────────────────────
echo "============================================"
echo " cellc Benchmark Suite"
echo " $(date)"
echo "============================================"
echo ""

if [ "$(id -u)" != "0" ]; then
    echo "ERROR: Must run as root"
    exit 1
fi

if [ ! -f "$CELLC_BIN" ]; then
    echo "ERROR: cellc binary not found at $CELLC_BIN"
    echo "       Run: gcc -Wall -Wextra -Iinclude -o cellc src/main.c src/container.c src/namespace.c src/utils.c src/fs.c src/cgroups.c src/net.c src/state.c"
    exit 1
fi

if [ ! -d "/root/container_fs/alpine_base" ]; then
    echo "ERROR: Alpine rootfs not found at /root/container_fs/alpine_base"
    exit 1
fi

mkdir -p "$RESULTS_DIR"

# ── system info ───────────────────────────────
echo "System Information:" | tee "$SUMMARY"
echo "-------------------" | tee -a "$SUMMARY"
echo "Kernel:  $(uname -r)" | tee -a "$SUMMARY"
echo "CPU:     $(grep 'model name' /proc/cpuinfo | head -1 | cut -d: -f2 | xargs)" | tee -a "$SUMMARY"
echo "Memory:  $(free -h | awk '/^Mem:/{print $2}')" | tee -a "$SUMMARY"
echo "OS:      $(cat /etc/os-release | grep PRETTY_NAME | cut -d= -f2 | tr -d '"')" | tee -a "$SUMMARY"
echo "" | tee -a "$SUMMARY"

# ── cleanup helper ────────────────────────────
cleanup_all() {
    umount -l /root/container_fs/merged 2>/dev/null || true
    rm -rf /root/container_fs/upper/* /root/container_fs/work/* 2>/dev/null || true
    ip link delete vH_bench   2>/dev/null || true
    ip link delete vH_memtest 2>/dev/null || true
    ip link delete vH_fsbench 2>/dev/null || true
    ip link delete vH_netbench 2>/dev/null || true
    ip link delete vH_cgbench 2>/dev/null || true
    rmdir /sys/fs/cgroup/cellc 2>/dev/null || true
    rm -rf /run/cellc/containers/* 2>/dev/null || true
}

# ── run each benchmark ────────────────────────
BENCHMARKS=(
    "01_startup_latency.sh"
    "02_memory_overhead.sh"
    "03_filesystem_overhead.sh"
    "04_network_throughput.sh"
    "05_cgroup_limits.sh"
)

PASSED=0
FAILED=0

for bench in "${BENCHMARKS[@]}"; do
    echo ""
    echo "Running: $bench"
    echo "--------------------------------------------"
    cleanup_all
    
    if bash "$SCRIPT_DIR/$bench"; then
        echo "✓ $bench PASSED"
        PASSED=$((PASSED + 1))
    else
        echo "✗ $bench FAILED"
        FAILED=$((FAILED + 1))
    fi
    
    cleanup_all
    sleep 1
done

# ── final summary ─────────────────────────────
echo "" | tee -a "$SUMMARY"
echo "============================================" | tee -a "$SUMMARY"
echo " BENCHMARK RESULTS SUMMARY"                  | tee -a "$SUMMARY"
echo "============================================" | tee -a "$SUMMARY"
echo "" | tee -a "$SUMMARY"

# pull key numbers from each result file
echo "1. Startup Latency:" | tee -a "$SUMMARY"
grep "Average startup" "$RESULTS_DIR/01_startup_latency.txt" 2>/dev/null | tee -a "$SUMMARY"

echo "" | tee -a "$SUMMARY"
echo "2. Memory Overhead:" | tee -a "$SUMMARY"
grep "overhead over bare" "$RESULTS_DIR/02_memory_overhead.txt" 2>/dev/null | tee -a "$SUMMARY"
grep "cgroup memory" "$RESULTS_DIR/02_memory_overhead.txt" 2>/dev/null | tee -a "$SUMMARY"

echo "" | tee -a "$SUMMARY"
echo "3. Filesystem I/O:" | tee -a "$SUMMARY"
grep "Write speed\|Read speed" "$RESULTS_DIR/03_filesystem_overhead.txt" 2>/dev/null | tee -a "$SUMMARY"

echo "" | tee -a "$SUMMARY"
echo "4. Network:" | tee -a "$SUMMARY"
grep "RTT\|bandwidth" "$RESULTS_DIR/04_network_throughput.txt" 2>/dev/null | tee -a "$SUMMARY"

echo "" | tee -a "$SUMMARY"
echo "5. cgroup Limits:" | tee -a "$SUMMARY"
grep "Configured limit\|cpu.max\|Max PIDs" "$RESULTS_DIR/05_cgroup_limits.txt" 2>/dev/null | tee -a "$SUMMARY"

echo "" | tee -a "$SUMMARY"
echo "============================================" | tee -a "$SUMMARY"
echo " Benchmarks passed: $PASSED / $((PASSED + FAILED))"  | tee -a "$SUMMARY"
echo "============================================" | tee -a "$SUMMARY"
echo "" | tee -a "$SUMMARY"
echo "Full results in: $RESULTS_DIR/"
echo "Summary in:      $SUMMARY"
