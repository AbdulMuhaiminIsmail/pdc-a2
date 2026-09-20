#!/usr/bin/env bash
# Direct measurement of DRAM traffic, to test the read-for-ownership
# explanation for TOTAL_BYTES = 4 * N * sizeof(float).
#
# uncore_imc/data_reads/ and data_writes/ count traffic at the integrated
# memory controller, so they see actual DRAM bytes rather than a model.
#
# Array initialisation and the gold computation also touch memory, so a single
# run would mix setup traffic into the measurement. Instead each variant is run
# with LOW and HIGH repetitions and the difference is divided by (HIGH - LOW).
# Anything that happens once cancels exactly.
#
# Requires root: perf_event_paranoid is 4 and uncore events are system-wide.
#   sudo ./perf_traffic.sh

set -u

LOW="${LOW:-1}"
HIGH="${HIGH:-11}"
N=20000000
ELEM_MB=$(awk -v n="$N" 'BEGIN{printf "%.1f", n*4/1024/1024}')   # one array, MiB

if [ "$(id -u)" != "0" ]; then
    echo "needs root (uncore events + perf_event_paranoid=4): sudo $0"
    exit 1
fi

export PATH="$PATH:/home/muhaimin/ispc-v1.31.0-linux/bin"

if [ ! -x ./saxpy_experiments ]; then
    echo "build saxpy_experiments first: make saxpy_experiments"
    exit 1
fi

# Sum the MiB value for one event out of perf's CSV output.
extract() { awk -F, -v ev="$1" '$3 ~ ev { s += $1 } END { printf "%.1f", s }'; }

measure() {   # variant threads repetitions -> "reads_MiB writes_MiB"
    local out
    out=$(perf stat -a -x, \
            -e uncore_imc/data_reads/,uncore_imc/data_writes/ \
            ./saxpy_experiments "$3" - "$1" "$2" 2>&1 >/dev/null)
    echo "$(echo "$out" | extract data_reads) $(echo "$out" | extract data_writes)"
}

echo "DRAM traffic per saxpy call, N = $N elements (one array = $ELEM_MB MiB)"
echo "differential method: ($HIGH runs - $LOW runs) / $((HIGH-LOW))"
echo
printf "%-14s %10s %10s %10s %14s\n" \
       "variant" "reads" "writes" "total" "floats/elem"
printf "%-14s %10s %10s %10s %14s\n" "" "MiB" "MiB" "MiB" "(total/4N)"
echo "--------------------------------------------------------------"

OUT=prog5_perf_traffic.csv
echo "variant,reads_mib,writes_mib,total_mib,floats_per_elem" > "$OUT"

for spec in "avx2_store 1" "avx2_stream 1" "ispc 1" "serial 1"; do
    set -- $spec
    v="$1"; t="$2"
    lo=$(measure "$v" "$t" "$LOW")
    hi=$(measure "$v" "$t" "$HIGH")
    read -r lor low_w <<< "$lo"
    read -r hir hiw <<< "$hi"
    awk -v v="$v" -v lor="$lor" -v low="$low_w" -v hir="$hir" -v hiw="$hiw" \
        -v d="$((HIGH-LOW))" -v n="$N" -v out="$OUT" '
    BEGIN {
        r = (hir - lor) / d;
        w = (hiw - low) / d;
        tot = r + w;
        fpe = tot * 1024 * 1024 / (n * 4);
        printf "%-14s %10.1f %10.1f %10.1f %14.2f\n", v, r, w, tot, fpe;
        printf "%s,%.1f,%.1f,%.1f,%.3f\n", v, r, w, tot, fpe >> out;
    }'
done

echo
echo "wrote $OUT"
