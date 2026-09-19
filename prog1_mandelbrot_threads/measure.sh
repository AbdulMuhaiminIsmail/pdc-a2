#!/usr/bin/env bash
#
# Program 1 measurement harness.
#
# Protocol (CS3006 A2 addendum S5.2): every configuration is run RUNS times
# and the minimum is reported; min and max are both kept so the spread can be
# quoted.  Note each ./mandelbrot invocation already takes the best of 5
# internal repetitions, so a "run" here is a best-of-5, and the reported
# figure is the best of RUNS best-of-5s.
#
#   ./measure.sh sweep    full mode x view x threadcount sweep -> CSV + summary
#   ./measure.sh threads  per-thread timing evidence for parts 3 and 4
#   ./measure.sh freq     core clock during serial vs 8-thread phases
#   ./measure.sh contention  tests whether the 8-thread dip is OS preemption
#
set -u

RUNS=${RUNS:-5}
THREAD_COUNTS=${THREAD_COUNTS:-"2 3 4 5 6 7 8 16"}
BIN=./mandelbrot

[ -x "$BIN" ] || { echo "build first: make" >&2; exit 1; }

run_once() {   # mode view threads -> "serial_ms thread_ms"
    local mode=$1 view=$2 threads=$3 out
    if [ "$mode" = block ]; then
        out=$(MANDEL_BLOCK=1 $BIN -t "$threads" -v "$view" 2>/dev/null)
    else
        out=$($BIN -t "$threads" -v "$view" 2>/dev/null)
    fi
    echo "$out" | awk '
        /mandelbrot serial/ { gsub(/[][]/,"",$(NF-1)); s=$(NF-1) }
        /mandelbrot thread/ { gsub(/[][]/,"",$(NF-1)); p=$(NF-1) }
        END { if (s=="" || p=="") print "NA NA"; else print s, p }'
}

cmd_sweep() {
    local raw=prog1_raw.csv sum=prog1_summary.csv
    echo "mode,view,threads,run,serial_ms,thread_ms" > "$raw"
    for mode in cyclic block; do
      for view in 1 2; do
        for t in $THREAD_COUNTS; do
          printf '  %-6s view %s  %2s threads: ' "$mode" "$view" "$t"
          for r in $(seq 1 "$RUNS"); do
              read -r s p <<< "$(run_once "$mode" "$view" "$t")"
              echo "$mode,$view,$t,$r,$s,$p" >> "$raw"
              printf '.'
          done
          printf ' done\n'
        done
      done
    done

    # Aggregate: minimum is the reported value, min/max give the spread.
    awk -F, 'NR>1 {
        k=$1","$2","$3
        if (!(k in n) || $6+0 < tmin[k]) tmin[k]=$6+0
        if (!(k in n) || $6+0 > tmax[k]) tmax[k]=$6+0
        if (!(k in n) || $5+0 < smin[k]) smin[k]=$5+0
        n[k]++
    } END {
        print "mode,view,threads,runs,serial_min_ms,thread_min_ms,thread_max_ms,spread_pct,speedup"
        for (k in n) printf "%s,%d,%.3f,%.3f,%.3f,%.1f,%.2f\n", \
            k, n[k], smin[k], tmin[k], tmax[k], \
            100*(tmax[k]-tmin[k])/tmin[k], smin[k]/tmin[k]
    }' "$raw" | { IFS= read -r hdr; echo "$hdr"; sort -t, -k1,1r -k2,2n -k3,3n; } > "$sum"

    echo; echo "raw -> $raw   summary -> $sum"; echo
    column -s, -t < "$sum"
}

cmd_threads() {
    local log=prog1_threadtimes.txt
    : > "$log"
    for mode in block cyclic; do
      for t in 4 8; do
        {
          echo "### mode=$mode threads=$t view=1"
          if [ "$mode" = block ]; then
              MANDEL_BLOCK=1 MANDEL_THREAD_TIMES=1 $BIN -t "$t" -v 1 2>/dev/null
          else
              MANDEL_THREAD_TIMES=1 $BIN -t "$t" -v 1 2>/dev/null
          fi
          echo
        } | grep -Ev '^Wrote' >> "$log"
      done
    done
    echo "per-thread timings -> $log"
    echo "(each configuration prints 5 blocks, one per internal repetition;"
    echo " quote the last block, by then the caches and clocks have settled)"
    cat "$log"
}

cmd_freq() {
    # With the performance governor every core sits near its ceiling whether
    # or not it is busy, so a plain mean over all 8 logical CPUs cannot tell a
    # 1-thread run from an 8-thread one.  Report the max (the core actually
    # doing work in the 1-thread case) alongside the mean.
    sample() {   # pid -> "meanMHz maxMHz"
        local pid=$1 sum=0 max=0 n=0 cur hi
        while kill -0 "$pid" 2>/dev/null; do
            read -r cur hi < <(awk '{s+=$1; if($1>m) m=$1; n++}
                               END {if(n) printf "%.0f %.0f", s/n/1000, m/1000;
                                    else printf "0 0"}' \
                               /sys/devices/system/cpu/cpu*/cpufreq/scaling_cur_freq 2>/dev/null)
            sum=$((sum + cur)); n=$((n + 1))
            [ "$hi" -gt "$max" ] && max=$hi
            sleep 0.05
        done
        [ "$n" -gt 0 ] && echo "$((sum / n)) $max" || echo "0 0"
    }

    echo "Core clock while the process runs (performance governor):"
    printf '  %-14s %-10s %s\n' "config" "mean MHz" "max MHz"
    for t in 1 2 4 8; do
        $BIN -t "$t" -v 1 >/dev/null 2>&1 &
        local pid=$! mean max
        read -r mean max < <(sample $pid)
        wait $pid
        printf '  %-14s %-10s %s\n' "${t}-thread" "$mean" "$max"
    done
    echo
    echo "If the mean falls as thread count rises, the all-core turbo ceiling"
    echo "is below the single-core one, and the serial baseline is being timed"
    echo "at a clock the parallel run cannot sustain. That caps speedup by"
    echo "roughly (1-thread clock / 8-thread clock) before any other effect."
}

cmd_contention() {
    # Cyclic gives every thread an identical mix of rows, and at 4 threads the
    # per-thread times agree to within 3%.  At 8 threads most threads still
    # land on the same figure but one or two run ~45% long, and it is a
    # different thread each repetition.  Deterministic work assignment cannot
    # produce a non-deterministic straggler, so the suspect is the OS: with 8
    # runnable compute threads on 8 hardware contexts there is no spare
    # context for the kernel, the shell or the display server, and whichever
    # compute thread gets descheduled sets the runtime for the whole image.
    #
    # Two safe probes.  Neither uses SCHED_FIFO, which on a laptop with 8
    # CPU-bound threads can starve the desktop.
    echo "=== involuntary context switches per run (view 1) ==="
    printf '  %-26s %-10s %s\n' "config" "invol-cs" "thread_ms"
    for t in 4 7 8 16; do
        local out ics ms
        out=$(/usr/bin/time -v $BIN -t "$t" -v 1 2>&1)
        ics=$(echo "$out" | awk -F: '/Involuntary context switches/{gsub(/ /,"",$2); print $2}')
        ms=$(echo "$out"  | awk '/mandelbrot thread/ {gsub(/[][]/,"",$(NF-1)); print $(NF-1)}')
        printf '  %-26s %-10s %s\n' "${t}-thread" "${ics:-n/a}" "${ms:-n/a}"
    done

    echo
    echo "=== same configs at highest normal priority (nice -20) ==="
    echo "If the dip is preemption by normal-priority system work, raising"
    echo "priority should recover most of it. If the dip is a hardware limit,"
    echo "nothing changes."
    printf '  %-26s %-12s %s\n' "config" "normal_ms" "nice-20_ms"
    for t in 4 7 8 16; do
        local a b
        a=$($BIN -t "$t" -v 1 2>/dev/null | awk '/mandelbrot thread/ {gsub(/[][]/,"",$(NF-1)); print $(NF-1)}')
        b=$(sudo nice -n -20 $BIN -t "$t" -v 1 2>/dev/null | awk '/mandelbrot thread/ {gsub(/[][]/,"",$(NF-1)); print $(NF-1)}')
        printf '  %-26s %-12s %s\n' "${t}-thread" "${a:-n/a}" "${b:-n/a}"
    done
}

case "${1:-sweep}" in
    sweep)   cmd_sweep ;;
    threads) cmd_threads ;;
    freq)       cmd_freq ;;
    contention) cmd_contention ;;
    *) echo "usage: $0 {sweep|threads|freq|contention}" >&2; exit 1 ;;
esac
