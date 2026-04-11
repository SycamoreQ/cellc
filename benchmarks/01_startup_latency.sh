#!/bin/bash
# ============================================================
# BENCHMARK 1: Container Startup Latency
# Measures time from run command to first process executing
# Compares cellc vs Docker
# ============================================================

CELLC=${CELLC_BIN:-./cellc}
RUNS=10
RESULTS_DIR="./benchmark_results"
mkdir -p "$RESULTS_DIR"
OUTPUT="$RESULTS_DIR/01_startup_latency.txt"

echo "============================================" | tee "$OUTPUT"
echo " BENCHMARK 1: Container Startup Latency"    | tee -a "$OUTPUT"
echo " Runs: $RUNS"                                | tee -a "$OUTPUT"
echo " Date: $(date)"                              | tee -a "$OUTPUT"
echo "============================================" | tee -a "$OUTPUT"

# ── cellc startup ──────────────────────────────
echo "" | tee -a "$OUTPUT"
echo "[cellc] Measuring startup latency..." | tee -a "$OUTPUT"

cleanup() {
    umount -l /root/container_fs/merged 2>/dev/null || true
    rm -rf /root/container_fs/upper/* /root/container_fs/work/* 2>/dev/null || true
    ip link delete vH_bench 2>/dev/null || true
    rmdir /sys/fs/cgroup/cellc 2>/dev/null || true
    rm -rf /run/cellc/containers/bench 2>/dev/null || true
}

CELLC_TIMES=()
for i in $(seq 1 $RUNS); do
    cleanup
    sleep 0.3

    START=$(date +%s%N)
    $CELLC run bench /bin/sh -c "exit" 2>/dev/null
    END=$(date +%s%N)

    MS=$(( (END - START) / 1000000 ))
    CELLC_TIMES+=("$MS")
    echo "  Run $i: ${MS}ms" | tee -a "$OUTPUT"
done

CELLC_SUM=0
for t in "${CELLC_TIMES[@]}"; do CELLC_SUM=$((CELLC_SUM + t)); done
CELLC_AVG=$((CELLC_SUM / RUNS))

# warm average excluding first cold run
WARM_SUM=0
for t in "${CELLC_TIMES[@]:1}"; do WARM_SUM=$((WARM_SUM + t)); done
CELLC_WARM_AVG=$((WARM_SUM / (RUNS - 1)))

echo "" | tee -a "$OUTPUT"
echo "[cellc] Average startup (all runs):   ${CELLC_AVG}ms"      | tee -a "$OUTPUT"
echo "[cellc] Average startup (warm, 2-10): ${CELLC_WARM_AVG}ms" | tee -a "$OUTPUT"

# ── Docker startup ─────────────────────────────
echo "" | tee -a "$OUTPUT"
if command -v docker &>/dev/null && docker info &>/dev/null 2>&1; then
    echo "[docker] Measuring startup latency..." | tee -a "$OUTPUT"

    # warm up docker (first pull)
    docker pull alpine:3.19 &>/dev/null

    DOCKER_TIMES=()
    for i in $(seq 1 $RUNS); do
        START=$(date +%s%N)
        docker run --rm alpine:3.19 /bin/sh -c "exit" 2>/dev/null
        END=$(date +%s%N)

        MS=$(( (END - START) / 1000000 ))
        DOCKER_TIMES+=("$MS")
        echo "  Run $i: ${MS}ms" | tee -a "$OUTPUT"
    done

    DOCKER_SUM=0
    for t in "${DOCKER_TIMES[@]}"; do DOCKER_SUM=$((DOCKER_SUM + t)); done
    DOCKER_AVG=$((DOCKER_SUM / RUNS))

    echo "" | tee -a "$OUTPUT"
    echo "[docker] Average startup: ${DOCKER_AVG}ms" | tee -a "$OUTPUT"

    echo "" | tee -a "$OUTPUT"
    echo "============================================" | tee -a "$OUTPUT"
    echo " RESULTS SUMMARY"                            | tee -a "$OUTPUT"
    echo "============================================" | tee -a "$OUTPUT"
    printf " %-15s %10s\n" "Runtime" "Avg Startup" | tee -a "$OUTPUT"
    printf " %-15s %10s\n" "-------" "-----------" | tee -a "$OUTPUT"
    printf " %-15s %9sms\n" "cellc" "$CELLC_AVG"   | tee -a "$OUTPUT"
    printf " %-15s %9sms\n" "docker" "$DOCKER_AVG" | tee -a "$OUTPUT"
else
    echo "[docker] Docker not available, skipping comparison" | tee -a "$OUTPUT"
    echo "" | tee -a "$OUTPUT"
    echo "============================================" | tee -a "$OUTPUT"
    echo " RESULTS SUMMARY"                            | tee -a "$OUTPUT"
    echo "============================================" | tee -a "$OUTPUT"
    printf " %-15s %10s\n" "Runtime" "Avg Startup" | tee -a "$OUTPUT"
    printf " %-15s %10s\n" "-------" "-----------" | tee -a "$OUTPUT"
    printf " %-15s %9sms\n" "cellc" "$CELLC_AVG"   | tee -a "$OUTPUT"
fi

echo "" | tee -a "$OUTPUT"
echo "Results saved to $OUTPUT"
