#!/usr/bin/env bash
# Direct measurement of DRAM traffic, to test the read-for-ownership
# explanation for TOTAL_BYTES = 4 * N * sizeof(float).
#
# uncore_imc/data_reads/ and data_writes/ count traffic at the integrated
# memory controller, so they see actual DRAM bytes rather than a model.
#
# Two corrections are needed to get a per-call figure out of them.
#
#   1. Fixed cost. Array allocation, initialisation, the gold computation and
#      verification all touch memory once. Each variant is therefore run with
#      LOW and HIGH repetitions and the difference divided by (HIGH - LOW), so
#      anything that happens once cancels exactly.
#
#   2. Background. These counters are system-wide -- there is no way to ask the
#      memory controller which process a transfer belonged to -- so everything
#      else running on the machine is included, in proportion to elapsed time.
#      The idle rate is measured separately and subtracted using the measured
#      elapsed times of the two runs.
#
# Requires root: perf_event_paranoid is 4 and uncore events are system-wide.
#   sudo ./perf_traffic.sh

set -u

LOW="${LOW:-1}"
HIGH="${HIGH:-21}"
BG_SECONDS="${BG_SECONDS:-5}"
N=20000000

if [ "$(id -u)" != "0" ]; then
    echo "needs root (uncore events + perf_event_paranoid=4): sudo $0"
    exit 1
fi

export PATH="$PATH:/home/muhaimin/ispc-v1.31.0-linux/bin"

if [ ! -x ./saxpy_experiments ]; then
    echo "build saxpy_experiments first: make saxpy_experiments"
    exit 1
fi

EVENTS=uncore_imc/data_reads/,uncore_imc/data_writes/
ONE_MIB=$(awk -v n="$N" 'BEGIN{printf "%.1f", n*4/1024/1024}')

extract() { awk -F, -v ev="$1" '$3 ~ ev { s += $1 } END { printf "%.2f", s }'; }

# Runs a command under perf and echoes "reads_MiB writes_MiB elapsed_seconds".
counted() {
    local t0 t1 out
    t0=$(date +%s%N)
    out=$(perf stat -a -x, -e "$EVENTS" "$@" 2>&1 >/dev/null)
    t1=$(date +%s%N)
    echo "$(echo "$out" | extract data_reads) $(echo "$out" | extract data_writes)" \
         "$(awk -v a="$t0" -v b="$t1" 'BEGIN{printf "%.4f", (b-a)/1e9}')"
}

echo "DRAM traffic per saxpy call, N = $N elements (one array = $ONE_MIB MiB)"
echo

# --- background rate ------------------------------------------------------
read -r BGR BGW BGS <<< "$(counted sleep "$BG_SECONDS")"
BG_R_RATE=$(awk -v v="$BGR" -v s="$BGS" 'BEGIN{printf "%.2f", v/s}')
BG_W_RATE=$(awk -v v="$BGW" -v s="$BGS" 'BEGIN{printf "%.2f", v/s}')
printf "idle background: %.1f MiB/s read, %.1f MiB/s written (%ss sample)\n\n" \
       "$BG_R_RATE" "$BG_W_RATE" "$BG_SECONDS"

printf "%-13s %9s %9s %9s %9s %9s\n" \
       "variant" "reads" "writes" "total" "floats" "predicted"
printf "%-13s %9s %9s %9s %9s %9s\n" \
       "" "MiB" "MiB" "MiB" "per elem" "per elem"
echo "-----------------------------------------------------------------"

OUT=prog5_perf_traffic.csv
echo "variant,reads_mib,writes_mib,total_mib,floats_per_elem,predicted" > "$OUT"

for spec in "serial 4" "ispc 4" "avx2_store 4" "avx2_stream 3"; do
    set -- $spec
    v="$1"; pred="$2"
    read -r LOR LOW_W LOS <<< "$(counted ./saxpy_experiments "$LOW"  - "$v" 1)"
    read -r HIR HIW HIS  <<< "$(counted ./saxpy_experiments "$HIGH" - "$v" 1)"
    awk -v v="$v" -v pred="$pred" \
        -v lor="$LOR" -v low="$LOW_W" -v los="$LOS" \
        -v hir="$HIR" -v hiw="$HIW" -v his="$HIS" \
        -v br="$BG_R_RATE" -v bw="$BG_W_RATE" \
        -v d="$((HIGH-LOW))" -v n="$N" -v out="$OUT" '
    BEGIN {
        dt = (his - los) / d;                 # elapsed seconds per call
        r  = (hir - lor) / d - br * dt;       # background removed
        w  = (hiw - low) / d - bw * dt;
        tot = r + w;
        fpe = tot * 1024 * 1024 / (n * 4);
        printf "%-13s %9.1f %9.1f %9.1f %9.2f %9d\n", v, r, w, tot, fpe, pred;
        printf "%s,%.1f,%.1f,%.1f,%.2f,%d\n", v, r, w, tot, fpe, pred >> out;
    }'
done

echo
echo "wrote $OUT"
