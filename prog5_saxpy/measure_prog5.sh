#!/usr/bin/env bash
# Measurement harness for Program 5 (saxpy), following the protocol in the
# write-up: RUNS repetitions per configuration, minimum reported, spread
# recorded.
#
#   ./measure_prog5.sh shipped    5 runs of the unmodified saxpy program
#   ./measure_prog5.sh sweep      ISPC task-count granularity sweep
#   ./measure_prog5.sh study      thread sweep + non-temporal-store variant
#   ./measure_prog5.sh all        all three
#
# Environment overrides:
#   RUNS=5            repetitions per configuration
#   TASK_COUNTS=...   task counts for the sweep
#   NICE=1            run the timed binaries under sudo nice -n -20

set -u

RUNS="${RUNS:-5}"
TASK_COUNTS="${TASK_COUNTS:-1 2 4 8 16 32 64 128 256}"
NICE="${NICE:-0}"
SUFFIX=""
RUNNER=()

if [ "$NICE" = "1" ]; then
    RUNNER=(sudo nice -n -20)
    SUFFIX="_nice"
fi

export PATH="$PATH:/home/muhaimin/ispc-v1.31.0-linux/bin"

run() { "${RUNNER[@]+"${RUNNER[@]}"}" "$@"; }

# Pull "[12.345] ms" out of the line whose label matches $1.
field_ms() { sed -n "s/.*$1.*\[\([0-9.]*\)\] ms.*/\1/p"; }
field_bw() { sed -n "s/.*$1.*\] ms[^[]*\[\([0-9.]*\)\] GB\/s.*/\1/p"; }

shipped() {
    echo "== shipped saxpy, $RUNS runs =="
    make -s clean >/dev/null 2>&1
    make -s >/dev/null || { echo "build failed"; exit 1; }

    local raw="prog5_shipped_raw${SUFFIX}.csv"
    echo "run,ispc_ms,ispc_gibs,task_ms,task_gibs,task_speedup" > "$raw"
    for i in $(seq 1 "$RUNS"); do
        local out
        out=$(run ./saxpy)
        local im ig tm tg
        im=$(echo "$out" | field_ms "saxpy ispc")
        ig=$(echo "$out" | field_bw "saxpy ispc")
        tm=$(echo "$out" | field_ms "saxpy task ispc")
        tg=$(echo "$out" | field_bw "saxpy task ispc")
        local sp
        sp=$(awk -v a="$im" -v b="$tm" 'BEGIN{printf "%.4f", a/b}')
        echo "$i,$im,$ig,$tm,$tg,$sp" >> "$raw"
        printf "  run %d: ispc %s ms, tasks %s ms, %sx\n" "$i" "$im" "$tm" "$sp"
    done

    awk -F, 'NR>1 {
        if (mi=="" || $2<mi) mi=$2; if (xi=="" || $2>xi) xi=$2;
        if (mt=="" || $4<mt) mt=$4; if (xt=="" || $4>xt) xt=$4;
    } END {
        printf "\n  ispc   min %.3f ms  max %.3f ms  spread %.1f%%\n", mi, xi, 100*(xi-mi)/mi;
        printf "  tasks  min %.3f ms  max %.3f ms  spread %.1f%%\n", mt, xt, 100*(xt-mt)/mt;
        printf "  task speedup at the two minima: %.3fx\n", mi/mt;
    }' "$raw"
}

sweep() {
    echo "== ISPC task-count sweep, $RUNS runs per count =="
    local out="prog5_tasksweep${SUFFIX}.csv"
    echo "tasks,ms,gibs" > "$out"
    for t in $TASK_COUNTS; do
        make -s clean >/dev/null 2>&1
        TASKS="$t" make -s >/dev/null || { echo "build failed at TASKS=$t"; exit 1; }
        local best=""
        for i in $(seq 1 "$RUNS"); do
            local ms
            ms=$(run ./saxpy | field_ms "saxpy task ispc")
            best=$(awk -v a="$best" -v b="$ms" 'BEGIN{ if(a=="" || b<a) print b; else print a }')
        done
        local bw
        bw=$(awk -v ms="$best" 'BEGIN{printf "%.3f", (4*20000000*4)/(1024*1024*1024)/(ms/1000)}')
        echo "$t,$best,$bw" >> "$out"
        printf "  %4s tasks: %8s ms  %8s GB/s\n" "$t" "$best" "$bw"
    done
    # restore the shipped configuration
    make -s clean >/dev/null 2>&1
    make -s >/dev/null
}

study() {
    echo "== bandwidth study, $RUNS runs per configuration =="
    make -s saxpy_experiments >/dev/null || { echo "build failed"; exit 1; }
    run ./saxpy_experiments "$RUNS" "prog5_bandwidth${SUFFIX}.csv"
}

case "${1:-all}" in
    shipped) shipped ;;
    sweep)   sweep ;;
    study)   study ;;
    all)     shipped; echo; sweep; echo; study ;;
    *) echo "usage: $0 [shipped|sweep|study|all]"; exit 1 ;;
esac
