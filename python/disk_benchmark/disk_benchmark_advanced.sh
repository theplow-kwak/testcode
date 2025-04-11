#!/bin/bash
set -e

# Default values
BLOCK_SIZE="1M"
TEST_SIZE_MB=512
OUT_DIR="./benchmark_results"
RW_MODE="write" # or randread, randwrite, read
DISK=""
RAW_IMG=""
QCOW_IMG=""
CSV_LOG=""
PLOT_DATA=""
PLOT_IMG=""
GRAPH_TYPE="iops"  # or bw

# Parse options
while [[ "$1" != "" ]]; do
  case "$1" in
    --disk) DISK="$2"; shift ;;
    --size) TEST_SIZE_MB="$2"; shift ;;
    --bs) BLOCK_SIZE="$2"; shift ;;
    --outdir) OUT_DIR="$2"; shift ;;
    --mode) RW_MODE="$2"; shift ;;
    --graph) GRAPH_TYPE="$2"; shift ;;
    -h|--help)
      echo "Usage: $0 --disk <device> [--size 512] [--bs 1M] [--mode write|randread|randwrite] [--graph iops|bw] [--outdir path]"
      exit 0 ;;
    *) echo "Unknown option: $1"; exit 1 ;;
  esac
  shift
done

if [[ -z "$DISK" ]]; then
  echo "Error: --disk <device> is required"
  exit 1
fi

# Paths
mkdir -p "$OUT_DIR"
RAW_IMG="$OUT_DIR/raw.img"
QCOW_IMG="$OUT_DIR/qcow2.img"
CSV_LOG="$OUT_DIR/report.csv"
PLOT_DATA="$OUT_DIR/perf.dat"
PLOT_IMG="$OUT_DIR/perf.png"

# Log header
echo "step,metric,value" > "$CSV_LOG"

log_metric() {
  echo "$1,$2,$3" >> "$CSV_LOG"
}

echo "[*] Disk Benchmark for $DISK"
echo "[*] Mode: $RW_MODE | Size: ${TEST_SIZE_MB}MB | Block Size: $BLOCK_SIZE"

# Step 1: FIO test
echo "[1/7] FIO test..."
fio --name=fio_test --filename="$DISK" --direct=1 --rw=$RW_MODE \
    --bs=$BLOCK_SIZE --size=${TEST_SIZE_MB}M --numjobs=1 --runtime=10s \
    --group_reporting > "$OUT_DIR/fio.log"

# Extract key info
BW=$(grep "bw=" "$OUT_DIR/fio.log" | head -n1 | sed -E 's/.*bw=([^,]+).*/\1/')
IOPS=$(grep "iops=" "$OUT_DIR/fio.log" | head -n1 | sed -E 's/.*iops=([0-9\.kK]+).*/\1/')
log_metric "fio" "bw" "$BW"
log_metric "fio" "iops" "$IOPS"

# Step 2: dd to raw
echo "[2/7] Backing up with dd..."
T0=$(date +%s.%N)
dd if="$DISK" of="$RAW_IMG" bs=$BLOCK_SIZE count=${TEST_SIZE_MB} status=none conv=fsync
T1=$(date +%s.%N)
ELAPSED=$(echo "$T1 - $T0" | bc)
log_metric "dd" "time" "$ELAPSED"

# Step 3: convert to qcow2
echo "[3/7] Converting to qcow2..."
T0=$(date +%s.%N)
qemu-img convert -c -f raw -O qcow2 "$RAW_IMG" "$QCOW_IMG"
T1=$(date +%s.%N)
ELAPSED=$(echo "$T1 - $T0" | bc)
log_metric "convert" "time" "$ELAPSED"

# Step 4: compare sizes
RAW_SIZE=$(du -b "$RAW_IMG" | cut -f1)
QCOW2_SIZE=$(du -b "$QCOW_IMG" | cut -f1)
RATIO=$(echo "scale=2; $QCOW2_SIZE / $RAW_SIZE * 100" | bc)
log_metric "image" "raw_size_bytes" "$RAW_SIZE"
log_metric "image" "qcow2_size_bytes" "$QCOW2_SIZE"
log_metric "image" "compression_ratio_pct" "$RATIO"

# Step 5: bench qcow2
echo "[4/7] Running qemu-img bench..."
qemu-img bench -c 1000 -d 64 -f qcow2 -n -s 4096 -t none "$QCOW_IMG" \
  > "$OUT_DIR/qemu-bench.log"

# Parse bench log
grep -E 'read|write' "$OUT_DIR/qemu-bench.log" | while read op iops bw _; do
  log_metric "qemu-img" "${op}_iops" "$iops"
  log_metric "qemu-img" "${op}_bw_MBps" "$bw"
  if [[ "$GRAPH_TYPE" == "iops" ]]; then
    echo "$op $iops" >> "$PLOT_DATA"
  else
    echo "$op $bw" >> "$PLOT_DATA"
  fi
done

# Step 6: plot
echo "[5/7] Generating graph..."
gnuplot <<EOF
set terminal png size 800,600
set output "$PLOT_IMG"
set title "QCOW2 ${GRAPH_TYPE^^} Performance"
set xlabel "Operation"
set ylabel "${GRAPH_TYPE^^}"
set style data histograms
set style fill solid
set boxwidth 0.5
plot "$PLOT_DATA" using 2:xtic(1) title "${GRAPH_TYPE^^}"
EOF

echo "[*] Benchmark completed."
echo "    CSV Log: $CSV_LOG"
echo "    Graph:   $PLOT_IMG"
