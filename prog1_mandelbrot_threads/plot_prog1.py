#!/usr/bin/env python3
"""
Figures for Program 1.  Reads only the CSV and log files written by
measure.sh, so every plotted point traces back to a measured run.

  prog1_speedup_view1.png   speedup vs thread count, view 1 (required by part 2)
  prog1_speedup_view2.png   same for view 2
  prog1_threadbalance.png      per-thread times at 8 threads (parts 3 and 4)
  prog1_threadbalance_t4.png   same at 4 threads, free of the 8-thread
                               scheduling straggler
"""
import csv, os, re, sys
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
def path(n): return os.path.join(HERE, n)


def load_summary(fname):
    """{(mode, view, threads): speedup} from a summary CSV, or {} if absent."""
    if not os.path.exists(path(fname)):
        return {}
    out = {}
    with open(path(fname)) as f:
        for r in csv.DictReader(f):
            out[(r["mode"], r["view"], int(r["threads"]))] = float(r["speedup"])
    return out


def plot_speedup(view, normal, nice, cores=4, contexts=8):
    series = [
        ("block",  normal, "Block (contiguous rows)",   "tab:red",  "o", "-"),
        ("cyclic", normal, "Row-cyclic",                "tab:blue", "s", "-"),
        ("cyclic", nice,   "Row-cyclic, nice -20",      "tab:green","^", "--"),
    ]
    fig, ax = plt.subplots(figsize=(7.2, 4.6))
    plotted = False
    for mode, data, label, colour, marker, style in series:
        pts = sorted((t, s) for (m, v, t), s in data.items()
                     if m == mode and v == str(view))
        if not pts:
            continue
        ax.plot([t for t, _ in pts], [s for _, s in pts],
                marker=marker, linestyle=style, color=colour, label=label)
        plotted = True
    if not plotted:
        plt.close(fig)
        return None

    xs = sorted({t for (_, v, t) in normal if v == str(view)})
    # Linear speedup is only a meaningful target up to the number of hardware
    # contexts; past that the machine has nothing left to give, so the ceiling
    # is flat rather than continuing to rise.
    ideal_x = [t for t in xs if t <= contexts]
    ax.plot(ideal_x, ideal_x, color="grey", linestyle=":", linewidth=1,
            label=f"Ideal (linear to {contexts})")
    if max(xs) > contexts:
        ax.plot([contexts, max(xs)], [contexts, contexts],
                color="grey", linestyle=":", linewidth=1)
    ax.axvline(cores, color="black", linestyle=":", linewidth=0.8)
    ax.text(cores, ax.get_ylim()[1] * 0.97, f" {cores} physical cores",
            fontsize=8, va="top")
    ax.axvline(contexts, color="black", linestyle=":", linewidth=0.8)
    ax.text(contexts, ax.get_ylim()[1] * 0.83, f" {contexts} hardware threads",
            fontsize=8, va="top")

    ax.set_xlabel("Threads")
    ax.set_ylabel(r"Speedup over serial ($T_{serial}/T_{parallel}$)")
    ax.set_title(f"Program 1: Mandelbrot speedup, view {view}\n"
                 "Intel Core i7-7820HQ, 4 cores / 8 threads")
    ax.set_xscale("log", base=2)
    ax.set_xticks(xs)
    ax.set_xticklabels([str(t) for t in xs])
    ax.xaxis.set_minor_locator(matplotlib.ticker.NullLocator())
    ax.grid(alpha=0.3)
    ax.legend(fontsize=9)
    fig.tight_layout()
    out = path(f"prog1_speedup_view{view}.png")
    fig.savefig(out, dpi=150)
    plt.close(fig)
    return out


THREAD_RE = re.compile(
    r"\[thread\s+(\d+) of\s+(\d+)\]\s+(\w+)\s+(\d+) rows\s+([\d.]+) ms")
HEAD_RE = re.compile(r"### mode=(\w+) threads=(\d+) view=(\d+)")


def load_threadtimes(fname="prog1_threadtimes.txt"):
    """{(mode, threads): [{tid: ms}, ...]} - every repetition in the log."""
    if not os.path.exists(path(fname)):
        return {}
    blocks, key, seen = {}, None, {}
    for line in open(path(fname)):
        h = HEAD_RE.match(line)
        if h:
            if key and seen:
                blocks.setdefault(key, []).append(seen)
            key, seen = (h.group(1), int(h.group(2))), {}
            continue
        m = THREAD_RE.search(line)
        if m and key:
            tid = int(m.group(1))
            if tid in seen:          # a new repetition has begun
                blocks.setdefault(key, []).append(seen)
                seen = {}
            seen[tid] = float(m.group(5))
    if key and seen:
        blocks.setdefault(key, []).append(seen)
    return blocks


def plot_balance(blocks, threads=8, suffix=""):
    have = [(m, t) for (m, t) in blocks if t == threads]
    if len(have) < 2:
        return None
    fig, axes = plt.subplots(1, 2, figsize=(9.5, 4.2), sharey=True)
    for ax, mode, colour in zip(axes, ["block", "cyclic"],
                                ["tab:red", "tab:blue"]):
        reps = blocks.get((mode, threads))
        if not reps:
            continue
        # The protocol reports the minimum of several runs, so show the
        # repetition that set it: the one whose slowest thread was fastest.
        times = min(reps, key=lambda r: max(r.values()))
        tids = sorted(times)
        vals = [times[t] for t in tids]
        ax.bar(tids, vals, color=colour, alpha=0.85)
        worst, best = max(vals), min(vals)
        ax.axhline(worst, color="black", linestyle="--", linewidth=1)
        ax.text(0, worst, f"  runtime = max = {worst:.1f} ms",
                va="bottom", fontsize=8)
        ax.set_title(f"{mode.capitalize()}  ({worst/best:.1f}x spread)")
        ax.set_xlabel("Thread id")
        ax.set_xticks(tids)
        ax.grid(axis="y", alpha=0.3)
    axes[0].set_ylabel("Time in workerThreadStart (ms)")
    fig.suptitle(f"Program 1: per-thread work at {threads} threads, view 1 — "
                 "runtime is the maximum, not the mean")
    fig.tight_layout()
    out = path(f"prog1_threadbalance{suffix}.png")
    fig.savefig(out, dpi=150)
    plt.close(fig)
    return out


def main():
    normal = load_summary("prog1_summary.csv")
    nice = load_summary("prog1_summary_nice.csv")
    if not normal:
        sys.exit("prog1_summary.csv not found - run ./measure.sh sweep first")

    blocks = load_threadtimes()
    written = [plot_speedup(1, normal, nice), plot_speedup(2, normal, nice),
               plot_balance(blocks, threads=8),
               plot_balance(blocks, threads=4, suffix="_t4")]
    for w in written:
        if w:
            print("wrote", os.path.basename(w))
    if not nice:
        print("note: prog1_summary_nice.csv absent, nice -20 series omitted")


if __name__ == "__main__":
    main()
