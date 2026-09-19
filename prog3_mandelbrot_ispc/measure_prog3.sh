#!/usr/bin/env bash
#
# Program 3 measurement harness. Same protocol as Program 1: RUNS repetitions
# per configuration, report the minimum, keep the maximum for the spread.
# Each ./mandelbrot_ispc invocation already takes the best of 3 internal
# repetitions, so a reported figure is the best of RUNS best-of-3s.
#
#   ./measure_prog3.sh sweep   task-count sweep, both views -> CSV + summary
#   ./measure_prog3.sh final   the chosen count, both views, verbose
#
set -u
export PATH=$PATH:$HOME/ispc-v1.31.0-linux/bin

RUNS=${RUNS:-5}
TASK_COUNTS=${TASK_COUNTS:-"2 4 8 16 32 64 100 128 200 256 400 800"}
BIN=./mandelbrot_ispc

# The ISPC task runtime spawns one worker per hardware context, so the
# full-occupancy preemption effect measured in Program 1 applies here too.
# NICE=1 runs under sudo nice -n -20; results go to a separate _nice pair.
NICE=${NICE:-0}
if [ "$NICE" != "0" ]; then SUFFIX="_nice"; else SUFFIX=""; fi

run_bin() {
    if [ "$NICE" != "0" ]; then sudo nice -n -20 $BIN "$@"; else $BIN "$@"; fi
}

parse() {  # stdin -> "serial ispc tasks"
    awk '/mandelbrot serial/         {gsub(/[][]/,"",$(NF-1)); s=$(NF-1)}
         /mandelbrot ispc/           {gsub(/[][]/,"",$(NF-1)); i=$(NF-1)}
         /mandelbrot multicore ispc/ {gsub(/[][]/,"",$(NF-1)); t=$(NF-1)}
         END {print (s==""?"NA":s), (i==""?"NA":i), (t==""?"NA":t)}'
}

cmd_sweep() {
    local raw=prog3_raw${SUFFIX}.csv sum=prog3_summary${SUFFIX}.csv
    echo "tasks,view,run,serial_ms,ispc_ms,tasks_ms" > "$raw"
    for n in $TASK_COUNTS; do
        make clean >/dev/null 2>&1
        make TASKS=$n >/dev/null 2>&1 || { echo "build failed at $n tasks" >&2; exit 1; }
        for view in 1 2; do
            printf '  %4s tasks, view %s: ' "$n" "$view"
            for r in $(seq 1 "$RUNS"); do
                out=$(run_bin --tasks -v "$view" 2>&1)
                echo "$out" | grep -q "differs from sequential" && {
                    echo "INCORRECT OUTPUT at $n tasks" >&2; exit 1; }
                read -r s i t <<< "$(echo "$out" | parse)"
                echo "$n,$view,$r,$s,$i,$t" >> "$raw"
                printf '.'
            done
            printf ' done\n'
        done
    done

    awk -F, 'NR>1 {
        k=$1","$2
        if (!(k in n) || $6+0 < tmin[k]) tmin[k]=$6+0
        if (!(k in n) || $6+0 > tmax[k]) tmax[k]=$6+0
        if (!(k in n) || $4+0 < smin[k]) smin[k]=$4+0
        if (!(k in n) || $5+0 < imin[k]) imin[k]=$5+0
        n[k]++
    } END {
        print "tasks,view,runs,serial_min_ms,ispc_min_ms,tasks_min_ms,tasks_max_ms,spread_pct,ispc_speedup,task_speedup"
        for (k in n) printf "%s,%d,%.3f,%.3f,%.3f,%.3f,%.1f,%.2f,%.2f\n", \
            k, n[k], smin[k], imin[k], tmin[k], tmax[k], \
            100*(tmax[k]-tmin[k])/tmin[k], smin[k]/imin[k], smin[k]/tmin[k]
    }' "$raw" | { IFS= read -r h; echo "$h"; sort -t, -k2,2n -k1,1n; } > "$sum"

    echo; echo "raw -> $raw   summary -> $sum"; echo
    column -s, -t < "$sum"

    # Restore the default build.
    make clean >/dev/null 2>&1 && make >/dev/null 2>&1
}

cmd_final() {
    make clean >/dev/null 2>&1 && make >/dev/null 2>&1
    for view in 1 2; do
        echo "### view $view, default task count"
        run_bin --tasks -v "$view" 2>&1 | grep -v "^Wrote"
        echo
    done
}

case "${1:-sweep}" in
    sweep) cmd_sweep ;;
    final) cmd_final ;;
    *) echo "usage: $0 {sweep|final}" >&2; exit 1 ;;
esac
