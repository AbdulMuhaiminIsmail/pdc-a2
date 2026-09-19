#!/usr/bin/env bash
#
# Program 2 width sweep.
#
# The CS149 intrinsics are a simulator: "Total Vector Instructions" and
# "Vector Utilization" are exact counts of simulated operations, not wall-clock
# timings. They are bit-for-bit reproducible and unaffected by machine load, so
# the five-run minimum protocol used elsewhere in this report does not apply
# and a single run per width is exhaustive.
#
# Note printStats() runs before arraySumVector() is called, so these figures
# describe clampedExpVector() alone.
set -u
N=${N:-10000}
OUT=prog2_widths.csv

echo "width,N,total_vector_instructions,utilized_lanes,total_lanes,utilization_pct" > "$OUT"
for w in 2 4 8 16; do
    make clean >/dev/null 2>&1
    make VW=$w >/dev/null 2>&1 || { echo "build failed at width $w" >&2; exit 1; }
    out=$(./myexp -s "$N" 2>&1)
    echo "$out" | grep -q "Results matched" || { echo "width $w produced wrong results" >&2; exit 1; }
    ins=$(echo "$out" | awk '/Total Vector Instructions/ {print $4}')
    uti=$(echo "$out" | awk '/Utilized Vector Lanes/     {print $4}')
    tot=$(echo "$out" | awk '/Total Vector Lanes/        {print $4}')
    pct=$(echo "$out" | awk '/Vector Utilization/        {gsub(/%/,"",$3); print $3}')
    echo "$w,$N,$ins,$uti,$tot,$pct" >> "$OUT"
    printf '  width %-2s  instructions %-8s  lanes %s/%s  utilization %s%%\n' \
           "$w" "$ins" "$uti" "$tot" "$pct"
done
echo
echo "-> $OUT"

# Why utilization moves the way it does: reproduce main.cpp's exponent array
# and count how much of the inner loop's lane capacity is actually useful.
g++ -O2 analyze_divergence.cpp -o analyze_divergence
./analyze_divergence "$N" csv > prog2_divergence.csv
./analyze_divergence "$N"
echo "-> prog2_divergence.csv"
# Leave the tree at the shipped default so a plain `make` reproduces it.
make clean >/dev/null 2>&1 && make >/dev/null 2>&1
