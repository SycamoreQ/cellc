#!/bin/bash
# ============================================================
# BENCHMARK 3: Filesystem Overhead (OverlayFS)
# Measures read/write performance inside container vs host
# ============================================================

CELLC=${CELLC_BIN:-./cellc}
RESULTS_DIR="./benchmark_results"
mkdir -p "$RESULTS_DIR"
OUTPUT="$RESULTS_DIR/03_filesystem_overhead.txt"

echo "============================================" | tee "$OUTPUT"
echo " BENCHMARK 3: Filesystem I/O Overhead"      | tee -a "$OUTPUT"
echo " Date: $(date)"                              | tee -a "$OUTPUT"
echo "============================================" | tee -a "$OUTPUT"

# ── helper: parse dd output for MB/s ──────────
parse_dd_speed() {
    echo "$1" | grep -oE '[0-9.]+ ?[MGk]B/s' | tail -1
}

# ── host baseline write ────────────────────────
echo "" | tee -a "$OUTPUT"
echo "[host] Sequential write (100MB)..." | tee -a "$OUTPUT"
HOST_WRITE=$(dd if=/dev/zero of=/tmp/cellc_bench_write bs=1M count=100 conv=fdatasync 2>&1)
HOST_WRITE_SPEED=$(parse_dd_speed "$HOST_WRITE")
echo "[host] Write speed: $HOST_WRITE_SPEED" | tee -a "$OUTPUT"
rm -f /tmp/cellc_bench_write

echo "" | tee -a "$OUTPUT"
echo "[host] Sequential read (100MB)..." | tee -a "$OUTPUT"
# write first, then read
dd if=/dev/zero of=/tmp/cellc_bench_read bs=1M count=100 conv=fdatasync 2>/dev/null
HOST_READ=$(dd if=/tmp/cellc_bench_read of=/dev/null bs=1M 2>&1)
HOST_READ_SPEED=$(parse_dd_speed "$HOST_READ")
echo "[host] Read speed: $HOST_READ_SPEED" | tee -a "$OUTPUT"
rm -f /tmp/cellc_bench_read

# ── cellc OverlayFS write ──────────────────────
echo "" | tee -a "$OUTPUT"
echo "[cellc] Starting container for filesystem benchmark..." | tee -a "$OUTPUT"

umount -l /root/container_fs/merged 2>/dev/null
rm -rf /root/container_fs/upper/* /root/container_fs/work/* 2>/dev/null
ip link delete vH_fsbench 2>/dev/null
rmdir /sys/fs/cgroup/cellc 2>/dev/null

# run write benchmark inside container
echo "[cellc] Sequential write (100MB) inside container..." | tee -a "$OUTPUT"
CELLC_WRITE=$($CELLC run fsbench /bin/sh -c "dd if=/dev/zero of=/tmp/bench_write bs=1M count=100 2>&1" 2>/dev/null)
CELLC_WRITE_SPEED=$(parse_dd_speed "$CELLC_WRITE")
echo "[cellc] Write speed: $CELLC_WRITE_SPEED" | tee -a "$OUTPUT"

umount -l /root/container_fs/merged 2>/dev/null || true
rm -rf /root/container_fs/upper/* /root/container_fs/work/* 2>/dev/null || true
ip link delete vH_fsbench 2>/dev/null || true
rmdir /sys/fs/cgroup/cellc 2>/dev/null || true
rm -rf /run/cellc/containers/fsbench 2>/dev/null || true
sleep 0.5

echo "" | tee -a "$OUTPUT"
echo "[cellc] Sequential read (100MB) inside container..." | tee -a "$OUTPUT"
CELLC_READ=$($CELLC run fsbench2 /bin/sh -c "dd if=/dev/zero of=/tmp/bench_read bs=1M count=100 2>/dev/null; dd if=/tmp/bench_read of=/dev/null bs=1M 2>&1" 2>/dev/null)
CELLC_READ_SPEED=$(parse_dd_speed "$CELLC_READ")
echo "[cellc] Read speed: $CELLC_READ_SPEED" | tee -a "$OUTPUT"

# ── small file creation (metadata overhead) ────
echo "" | tee -a "$OUTPUT"
echo "[host] Creating 1000 small files..." | tee -a "$OUTPUT"
START=$(date +%s%N)
for i in $(seq 1 1000); do touch /tmp/cellc_smallfile_$i; done
END=$(date +%s%N)
HOST_SMALLFILE_MS=$(( (END - START) / 1000000 ))
echo "[host] 1000 files in ${HOST_SMALLFILE_MS}ms" | tee -a "$OUTPUT"
rm -f /tmp/cellc_smallfile_*

# ── Summary ───────────────────────────────────
echo "" | tee -a "$OUTPUT"
echo "============================================" | tee -a "$OUTPUT"
echo " RESULTS SUMMARY"                            | tee -a "$OUTPUT"
echo "============================================" | tee -a "$OUTPUT"
printf " %-30s %-15s %-15s\n" "Operation" "Host" "cellc (OverlayFS)" | tee -a "$OUTPUT"
printf " %-30s %-15s %-15s\n" "---------" "----" "------------------" | tee -a "$OUTPUT"
printf " %-30s %-15s %-15s\n" "Sequential Write (100MB)" "$HOST_WRITE_SPEED" "$CELLC_WRITE_SPEED" | tee -a "$OUTPUT"
printf " %-30s %-15s %-15s\n" "Sequential Read (100MB)"  "$HOST_READ_SPEED"  "$CELLC_READ_SPEED"  | tee -a "$OUTPUT"
printf " %-30s %-15s %-15s\n" "Small file creation (1000)" "${HOST_SMALLFILE_MS}ms" "N/A" | tee -a "$OUTPUT"

echo "" | tee -a "$OUTPUT"
echo "Results saved to $OUTPUT"
