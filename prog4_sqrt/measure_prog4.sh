#!/usr/bin/env bash
#
# Program 4 measurement harness. Section 2 protocol: RUNS repetitions per
# input pattern, minimum reported, maximum kept for the spread. Each ./sqrt
# invocation already takes the best of 3 internal repetitions.
#
# NICE=1 runs under sudo nice -n -20, for the full-occupancy reason
# established in Program 1; results go to a separate _nice pair of files.
#
set -u
export PATH=$PATH:$HOME/ispc-v1.31.0-linux/bin

RUNS=${RUNS:-5}
MODES=${MODES:-"random best worst"}
BIN=./sqrt
NICE=${NICE:-0}
if [ "$NICE" != "0" ]; then SUFFIX="_nice"; else SUFFIX=""; fi

run_bin() {
    if [ "$NICE" != "0" ]; then sudo nice -n -20 $BIN "$@"; else $BIN "$@"; fi
}

[ -x "$BIN" ] || { echo "build first: make" >&2; exit 1; }

raw=prog4_raw${SUFFIX}.csv
sum=prog4_summary${SUFFIX}.csv
echo "mode,run,serial_ms,ispc_ms,task_ms,avx2_ms" > "$raw"

for mode in $MODES; do
    printf '  %-7s: ' "$mode"
    for r in $(seq 1 "$RUNS"); do
        out=$(run_bin "$mode" 2>&1)
        echo "$out" | grep -q "^Error:" && { echo "INCORRECT RESULT in $mode" >&2; exit 1; }
        read -r s i t a <<< "$(echo "$out" | awk '
            /sqrt serial/       {gsub(/[][]/,"",$(NF-1)); s=$(NF-1)}
            /sqrt ispc/         {gsub(/[][]/,"",$(NF-1)); i=$(NF-1)}
            /sqrt task ispc/    {gsub(/[][]/,"",$(NF-1)); t=$(NF-1)}
            /sqrt avx2 intrin/  {gsub(/[][]/,"",$(NF-1)); a=$(NF-1)}
            END {print s, i, t, a}')"
        echo "$mode,$r,$s,$i,$t,$a" >> "$raw"
        printf '.'
    done
    printf ' done\n'
done

awk -F, 'NR>1 {
    k=$1
    if (!(k in n) || $3+0 < smin[k]) smin[k]=$3+0
    if (!(k in n) || $4+0 < imin[k]) imin[k]=$4+0
    if (!(k in n) || $4+0 > imax[k]) imax[k]=$4+0
    if (!(k in n) || $5+0 < tmin[k]) tmin[k]=$5+0
    if (!(k in n) || $6+0 < amin[k]) amin[k]=$6+0
    n[k]++
} END {
    print "mode,runs,serial_ms,ispc_ms,task_ms,avx2_ms,ispc_spread_pct,simd_speedup,task_speedup,multicore_gain,avx2_speedup"
    for (k in n) printf "%s,%d,%.3f,%.3f,%.3f,%.3f,%.1f,%.2f,%.2f,%.2f,%.2f\n", \
        k, n[k], smin[k], imin[k], tmin[k], amin[k], \
        100*(imax[k]-imin[k])/imin[k], \
        smin[k]/imin[k], smin[k]/tmin[k], imin[k]/tmin[k], smin[k]/amin[k]
}' "$raw" > "$sum"

echo; echo "raw -> $raw   summary -> $sum"; echo
column -s, -t < "$sum"

echo
echo "Input profiles (iteration counts, independent of any timing):"
for mode in $MODES; do
    echo "--- $mode"
    $BIN "$mode" 2>/dev/null | sed -n '2,5p'
done
