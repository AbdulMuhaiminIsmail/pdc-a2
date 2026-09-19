#!/usr/bin/env python3
"""
Figure and LaTeX tables for Program 4, from measure_prog4.sh output.
Iteration counts are re-derived here by the same rule ./sqrt prints, so the
predicted ceilings in the tables and the measured times come from one place.

  prog4_inputs.png   absolute times and speedups by input pattern
  prog4_tables.tex   \\input by the write-up
"""
import csv, os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
def path(n): return os.path.join(HERE, n)

MODES = ["random", "best", "worst"]
LABEL = {"random": "random\n(as shipped)", "best": "best\n(all 2.999)",
         "worst": "worst\n(1 slow lane in 8)"}
# serial and gang-issued Newton iterations, as ./sqrt reports them
ITERS = {"random": (107131008, 23443120),
         "best":   (440000000, 55000000),
         "worst":  (55000000,  55000000)}
GANG = 8


def load(fname="prog4_summary.csv"):
    with open(path(fname)) as f:
        return {r["mode"]: r for r in csv.DictReader(f)}


def figure(d):
    fig, (a, b) = plt.subplots(1, 2, figsize=(11.5, 4.6))
    x = np.arange(len(MODES))
    w = 0.2
    series = [("serial_ms", "Serial", "tab:grey"),
              ("ispc_ms", "ISPC (SIMD)", "tab:blue"),
              ("avx2_ms", "AVX2 intrinsics", "tab:green"),
              ("task_ms", "ISPC + tasks", "tab:red")]
    for k, (key, lbl, c) in enumerate(series):
        vals = [float(d[m][key]) for m in MODES]
        a.bar(x + (k - 1.5) * w, vals, w, label=lbl, color=c)
    a.set_yscale("log")
    a.set_xticks(x); a.set_xticklabels([LABEL[m] for m in MODES], fontsize=8)
    a.set_ylabel("Time (ms, log scale)")
    a.set_title("Serial time swings 7.3$\\times$;\nvector time barely moves")
    a.legend(fontsize=8); a.grid(axis="y", alpha=0.3)
    # Annotate the flatness that is the point of the panel.
    a.annotate("", xy=(1 - 0.5 * w, float(d["best"]["ispc_ms"])),
               xytext=(2 - 0.5 * w, float(d["worst"]["ispc_ms"])),
               arrowprops=dict(arrowstyle="<->", color="tab:blue", lw=1.2))
    a.text(1.5 - 0.5 * w, float(d["best"]["ispc_ms"]) * 1.25, "0.4% apart",
           ha="center", fontsize=8, color="tab:blue")

    pred = [ITERS[m][0] / ITERS[m][1] for m in MODES]
    meas = [float(d[m]["simd_speedup"]) for m in MODES]
    avx2 = [float(d[m]["avx2_speedup"]) for m in MODES]
    b.bar(x - w, pred, w, label="Divergence ceiling (predicted)",
          color="tab:blue", alpha=0.35, edgecolor="tab:blue")
    b.bar(x, meas, w, label="ISPC, measured", color="tab:blue")
    b.bar(x + w, avx2, w, label="AVX2 intrinsics, measured", color="tab:green")
    b.axhline(GANG, color="red", linestyle=":", linewidth=1.2)
    b.text(-0.45, GANG + 0.15, "$W = 8$", color="red", fontsize=9)
    b.axhline(1.0, color="black", linestyle="--", linewidth=0.8)
    b.text(-0.45, 1.12, "no gain", fontsize=8)
    for k, v in enumerate(meas):
        b.text(k, v + 0.15, f"{v:.2f}", ha="center", fontsize=8)
    b.set_xticks(x); b.set_xticklabels([LABEL[m] for m in MODES], fontsize=8)
    b.set_ylabel("SIMD speedup over serial")
    b.set_title("Predicted from the input alone,\nbefore any timing")
    b.legend(fontsize=8, loc="upper right"); b.grid(axis="y", alpha=0.3)

    fig.suptitle("Program 4: iterative sqrt, $N = 20{,}000{,}000$, AVX2 8-wide")
    fig.tight_layout()
    fig.savefig(path("prog4_inputs.png"), dpi=150)
    plt.close(fig)


def tables(d):
    rows = []
    for m in MODES:
        r = d[m]
        rows.append(f"{LABEL[m].replace(chr(10),' ')} & {r['serial_ms']} & "
                    f"{r['ispc_ms']} & {r['task_ms']} & {r['avx2_ms']} & "
                    f"{float(r['simd_speedup']):.2f}$\\times$ & "
                    f"{float(r['task_speedup']):.2f}$\\times$ & "
                    f"{float(r['multicore_gain']):.2f}$\\times$ \\\\")
    main = ("\\begin{tabular}{l rrrr rrr}\n\\toprule\n"
            "Input & Serial & ISPC & Tasks & AVX2 & SIMD & Total & Multi-core \\\\\n"
            " & (ms) & (ms) & (ms) & (ms) & speedup & speedup & gain \\\\\n"
            "\\midrule\n" + "\n".join(rows) + "\n\\bottomrule\n\\end{tabular}")

    prows = []
    for m in MODES:
        si, gi = ITERS[m]
        r = d[m]
        sc = 1e6 * float(r["serial_ms"]) / si
        gv = 1e6 * float(r["ispc_ms"]) / gi
        prows.append(f"{LABEL[m].replace(chr(10),' ')} & {si:,} & {gi:,} & "
                     f"{si/gi:.2f}$\\times$ & {sc/gv:.3f} & "
                     f"{(si/gi)*(sc/gv):.2f}$\\times$ & "
                     f"{float(r['simd_speedup']):.2f}$\\times$ \\\\"
                     .replace(",", "\\,"))
    model = ("\\begin{tabular}{l rr rr rr}\n\\toprule\n"
             "Input & Serial & Gang-issued & Divergence & Cost & Predicted "
             "& Measured \\\\\n"
             " & iterations & iterations & ratio & ratio & speedup & speedup \\\\\n"
             "\\midrule\n" + "\n".join(prows) + "\n\\bottomrule\n\\end{tabular}")

    with open(path("prog4_tables.tex"), "w") as f:
        f.write("% Generated by plot_prog4.py from measure_prog4.sh output.\n"
                "\\newcommand{\\progFourResults}{%\n" + main + "}\n\n"
                "\\newcommand{\\progFourModel}{%\n" + model + "}\n")


def main():
    d = load()
    figure(d)
    tables(d)
    print("wrote prog4_inputs.png and prog4_tables.tex")


if __name__ == "__main__":
    main()
