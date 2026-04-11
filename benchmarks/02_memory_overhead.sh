#!/bin/bash
# ============================================================
# BENCHMARK 2: Memory Overhead
# Measures memory consumed by the container runtime itself
# vs a bare process, and vs Docker
# ============================================================

CELLC=${CELLC_BIN:-./cellc}
RESULTS_DIR="./benchmark_results"
mkdir -p "$RESULTS_DIR"
OUTPUT="$RESULTS_DIR/02_memory_overhead.txt"

echo "============================================" | tee "$OUTPUT"
echo " BENCHMARK 2: Memory Overhead"               | tee -a "$OUTPUT"
echo " Date: $(date)"                              | tee -a "$OUTPUT"
echo "============================================" | tee -a "$OUTPUT"

# ── helper: get memory of a PID in KB ─────────
get_mem_kb() {
    local pid=$1
    if [ -f "/proc/$pid/status" ]; then
        grep VmRSS /proc/$pid/status | awk '{print $2}'
    else
        echo "0"
    fi
}

# ── baseline: bare /bin/sh on host ────────────
echo "" | tee -a "$OUTPUT"
echo "[baseline] Starting bare /bin/sh..." | tee -a "$OUTPUT"
/bin/sh -c "sleep 30" &
BARE_PID=$!
sleep 1
BARE_MEM=$(get_mem_kb $BARE_PID)
echo "[baseline] /bin/sh memory: ${BARE_MEM} KB" | tee -a "$OUTPUT"
kill $BARE_PID 2>/dev/null
wait $BARE_PID 2>/dev/null

# ── cellc container memory ────────────────────
echo "" | tee -a "$OUTPUT"
echo "[cellc] Starting container..." | tee -a "$OUTPUT"

umount -l /root/container_fs/merged 2>/dev/null
rm -rf /root/container_fs/upper/* /root/container_fs/work/* 2>/dev/null
ip link delete vH_memtest 2>/dev/null
rmdir /sys/fs/cgroup/cellc 2>/dev/null

$CELLC run memtest /bin/sh -c "sleep 30" &
CELLC_LAUNCHER_PID=$!
sleep 2

# get container PID from state
CONTAINER_PID=$(cat /run/cellc/containers/memtest/pid 2>/dev/null)
CELLC_MEM=0

if [ -n "$CONTAINER_PID" ] && [ "$CONTAINER_PID" -gt 0 ]; then
    CELLC_MEM=$(get_mem_kb $CONTAINER_PID)
    echo "[cellc] Container PID: $CONTAINER_PID" | tee -a "$OUTPUT"
    echo "[cellc] Container memory (RSS): ${CELLC_MEM} KB" | tee -a "$OUTPUT"

    # cgroup reported memory
    CGROUP_MEM=$(cat /sys/fs/cgroup/cellc/memory.current 2>/dev/null)
    if [ -n "$CGROUP_MEM" ]; then
        CGROUP_MEM_KB=$((CGROUP_MEM / 1024))
        echo "[cellc] cgroup memory.current: ${CGROUP_MEM_KB} KB" | tee -a "$OUTPUT"
    fi
else
    echo "[cellc] Could not get container PID" | tee -a "$OUTPUT"
fi

# kill the container
kill $CELLC_LAUNCHER_PID 2>/dev/null
wait $CELLC_LAUNCHER_PID 2>/dev/null

# ── Docker memory ─────────────────────────────
echo "" | tee -a "$OUTPUT"
DOCKER_MEM=0
if command -v docker &>/dev/null && docker info &>/dev/null 2>&1; then
    echo "[docker] Starting container..." | tee -a "$OUTPUT"
    DOCKER_CID=$(docker run -d --rm alpine:3.19 /bin/sh -c "sleep 30" 2>/dev/null)
    sleep 2

    if [ -n "$DOCKER_CID" ]; then
        DOCKER_MEM=$(docker stats --no-stream --format "{{.MemUsage}}" "$DOCKER_CID" 2>/dev/null | awk '{print $1}')
        echo "[docker] Container memory: $DOCKER_MEM" | tee -a "$OUTPUT"
        docker stop "$DOCKER_CID" &>/dev/null
    fi
else
    echo "[docker] Docker not available, skipping" | tee -a "$OUTPUT"
fi

# ── Summary ───────────────────────────────────
echo "" | tee -a "$OUTPUT"
echo "============================================" | tee -a "$OUTPUT"
echo " RESULTS SUMMARY"                            | tee -a "$OUTPUT"
echo "============================================" | tee -a "$OUTPUT"
printf " %-20s %15s\n" "Measurement"    "Memory (KB)" | tee -a "$OUTPUT"
printf " %-20s %15s\n" "-----------"    "------------" | tee -a "$OUTPUT"
printf " %-20s %15s\n" "Bare /bin/sh"   "${BARE_MEM}" | tee -a "$OUTPUT"
printf " %-20s %15s\n" "cellc container" "${CELLC_MEM}" | tee -a "$OUTPUT"
[ -n "$DOCKER_MEM" ] && printf " %-20s %15s\n" "docker container" "${DOCKER_MEM}" | tee -a "$OUTPUT"

OVERHEAD=$((CELLC_MEM - BARE_MEM))
echo "" | tee -a "$OUTPUT"
echo " cellc overhead over bare process: ${OVERHEAD} KB" | tee -a "$OUTPUT"

echo "" | tee -a "$OUTPUT"
echo "Results saved to $OUTPUT"
