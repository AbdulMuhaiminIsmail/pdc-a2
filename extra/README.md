# Extra credit

Every item below is discussed in `writeup.pdf`; the section is named against
each one. The files here are copies. The authoritative versions live in their
program directories, because that is where they build and run — a copy in
`extra/` would not compile on its own. Paths to the originals are given.

| # | Program | Item | Original | Write-up |
|---|---------|------|----------|----------|
| 1 | 2 | Vectorised array sum in `O(N/W + log W)` | `prog2_vecintrin/main.cpp`, function `arraySumVector` | "Extra credit: vectorized array sum" |
| 2 | 3 | `std::thread` create/join cost vs ISPC tasks | `prog3_mandelbrot_ispc/threads_vs_tasks.cpp` | "Extra credit: threads versus ISPC tasks" |
| 3 | 4 | Hand-written AVX2 `sqrt`, beats ISPC on all three inputs | `prog4_sqrt/sqrtAvx2.cpp` | "Extra credit: hand-written AVX2 intrinsics" |
| 4 | 5 | Why `TOTAL_BYTES` multiplies by 4 (1 point) | answered in the write-up; evidence from items 5 and 6 | "Extra credit: why the multiplier is four, not three" |
| 5 | 5 | Improving `saxpy` with non-temporal stores | `prog5_saxpy/saxpy_experiments.cpp` | "Extra credit: improving saxpy" |
| 6 | 5 | DRAM traffic read from the memory controller | `prog5_saxpy/perf_traffic.sh` | "Test 3: read the memory controller directly" |

## Building and running

    # 2 -- thread create/join cost
    cd prog3_mandelbrot_ispc && make threads_vs_tasks && ./threads_vs_tasks

    # 3 -- AVX2 sqrt (built into the main sqrt binary)
    cd prog4_sqrt && make && ./sqrt random   # also: best, worst

    # 5 -- saxpy bandwidth study, including the non-temporal variant
    cd prog5_saxpy && make saxpy_experiments && ./saxpy_experiments 5

    # 6 -- DRAM traffic counters (needs root: uncore events, paranoid=4)
    cd prog5_saxpy && sudo ./perf_traffic.sh
