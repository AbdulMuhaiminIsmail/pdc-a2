// Bandwidth study for Program 5.
//
// The shipped saxpy program answers "how fast is it?". This one answers "why
// is that the speed?". Three things are measured here that the shipped program
// cannot show:
//
//   1. A thread sweep (1..8). If saxpy were compute-bound, time would fall as
//      1/T. If it is bandwidth-bound, it flattens as soon as the memory system
//      is saturated, and the thread count at which it flattens *is* the answer
//      to "can this be made to scale linearly?".
//
//   2. A non-temporal-store variant. Ordinary stores to result[] miss in cache
//      and trigger a read-for-ownership: the line is fetched from DRAM before
//      being completely overwritten. That wasted read is the fourth N in
//      TOTAL_BYTES = 4 * N * sizeof(float). _mm256_stream_ps writes straight to
//      memory without the fetch, so if the RFO explanation is right this
//      variant must move 3N instead of 4N and run about 4/3 faster. That makes
//      the extra-credit answer testable rather than merely asserted.
//
//   3. The same ISPC entry points the shipped program calls, so the numbers in
//      both programs are directly comparable (the ISPC object file is shared).

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <string>
#include <thread>
#include <vector>
#include <immintrin.h>

#include "CycleTimer.h"
#include "saxpy_ispc.h"

extern void saxpySerial(int N, float scale, float* X, float* Y, float* result);

static const int N = 20 * 1000 * 1000;   // matches main.cpp
static const float SCALE = 2.f;

// Bytes of DRAM traffic the shipped program assumes, per element.
static const int REPORTED_FLOATS_PER_ELEM = 4;

// ---------------------------------------------------------------------------

// Chunk boundaries are forced to whole cache lines (16 floats). A non-temporal
// store that only covers part of a line still forces the line to be read, so
// splitting work mid-line would silently reintroduce the traffic we are trying
// to remove.
static int chunkFor(int threads) {
    int perThread = (N + threads - 1) / threads;
    return (perThread + 15) & ~15;
}

static void saxpyPlain(float scale, const float* X, const float* Y,
                       float* result, int begin, int end) {
    const __m256 s = _mm256_set1_ps(scale);
    int i = begin;
    for (; i + 8 <= end; i += 8) {
        __m256 x = _mm256_load_ps(X + i);
        __m256 y = _mm256_load_ps(Y + i);
        _mm256_store_ps(result + i, _mm256_fmadd_ps(s, x, y));
    }
    for (; i < end; ++i)
        result[i] = scale * X[i] + Y[i];
}

static void saxpyStream(float scale, const float* X, const float* Y,
                        float* result, int begin, int end) {
    const __m256 s = _mm256_set1_ps(scale);
    int i = begin;
    for (; i + 8 <= end; i += 8) {
        __m256 x = _mm256_load_ps(X + i);
        __m256 y = _mm256_load_ps(Y + i);
        _mm256_stream_ps(result + i, _mm256_fmadd_ps(s, x, y));
    }
    for (; i < end; ++i)
        result[i] = scale * X[i] + Y[i];
    _mm_sfence();   // non-temporal stores are weakly ordered
}

typedef void (*KernelFn)(float, const float*, const float*, float*, int, int);

static void runThreaded(KernelFn fn, int threads, float scale,
                        const float* X, const float* Y, float* result) {
    if (threads == 1) {
        fn(scale, X, Y, result, 0, N);
        return;
    }
    const int chunk = chunkFor(threads);
    std::vector<std::thread> pool;
    pool.reserve(threads);
    for (int t = 0; t < threads; ++t) {
        int begin = t * chunk;
        if (begin >= N) break;
        int end = std::min(N, begin + chunk);
        pool.push_back(std::thread(fn, scale, X, Y, result, begin, end));
    }
    for (size_t t = 0; t < pool.size(); ++t)
        pool[t].join();
}

// ---------------------------------------------------------------------------

struct Result {
    std::string variant;
    int threads;
    int floatsPerElem;   // modelled DRAM traffic: 4 normally, 3 with NT stores
    double ms;
};

static float* allocAligned() {
    void* p = aligned_alloc(64, (size_t)N * sizeof(float));
    if (!p) { fprintf(stderr, "allocation failed\n"); exit(1); }
    return (float*)p;
}

static bool verify(const float* got, const float* gold) {
    for (int i = 0; i < N; ++i) {
        if (got[i] != gold[i]) {
            fprintf(stderr, "MISMATCH at %d: got %f expected %f\n",
                    i, got[i], gold[i]);
            return false;
        }
    }
    return true;
}

// GB/s exactly as main.cpp computes it: 4*N*sizeof(float) over 1024^3.
static double reportedGBs(double sec) {
    double bytes = (double)REPORTED_FLOATS_PER_ELEM * N * sizeof(float);
    return bytes / (1024.0 * 1024.0 * 1024.0) / sec;
}

// Modelled DRAM traffic in decimal GB/s, which is the unit DDR4 peak is quoted
// in. For the NT variant the model drops to 3 floats per element.
static double dramGBs(double sec, int floatsPerElem) {
    double bytes = (double)floatsPerElem * N * sizeof(float);
    return bytes / 1e9 / sec;
}

int main(int argc, char** argv) {
    const int runs = (argc > 1) ? atoi(argv[1]) : 5;
    const char* csvPath = (argc > 2 && strcmp(argv[2], "-") != 0) ? argv[2] : NULL;

    // Single-variant mode. perf counts everything the process does, so to
    // attribute DRAM traffic to one kernel the process must run only that
    // kernel. In this mode the arrays are still initialised and verified, but
    // the counted region is entered exactly `runs` times by one variant, and
    // the per-repetition memset is suppressed -- clearing 76 MiB between
    // repetitions is real DRAM traffic and would be attributed to saxpy.
    const char* onlyVariant = (argc > 3) ? argv[3] : NULL;
    const int onlyThreads = (argc > 4) ? atoi(argv[4]) : 1;

    float* X = allocAligned();
    float* Y = allocAligned();
    float* gold = allocAligned();
    float* out = allocAligned();

    for (int i = 0; i < N; ++i) {
        X[i] = (float)i;
        Y[i] = (float)i;
        gold[i] = 0.f;
        out[i] = 0.f;
    }
    saxpySerial(N, SCALE, X, Y, gold);

    std::vector<Result> results;

    // --- reference implementations -----------------------------------------
    struct { const char* name; int kind; } refs[] = {
        { "serial",     0 },
        { "ispc",       1 },
        { "ispc_tasks", 2 },
    };
    for (int r = 0; r < 3; ++r) {
        if (onlyVariant && strcmp(onlyVariant, refs[r].name) != 0) continue;
        double best = 1e30;
        if (onlyVariant) memset(out, 0, (size_t)N * sizeof(float));
        for (int k = 0; k < runs; ++k) {
            if (!onlyVariant) memset(out, 0, (size_t)N * sizeof(float));
            double t0 = CycleTimer::currentSeconds();
            if (refs[r].kind == 0)      saxpySerial(N, SCALE, X, Y, out);
            else if (refs[r].kind == 1) ispc::saxpy_ispc(N, SCALE, X, Y, out);
            else                        ispc::saxpy_ispc_withtasks(N, SCALE, X, Y, out);
            double t1 = CycleTimer::currentSeconds();
            best = std::min(best, t1 - t0);
        }
        if (!verify(out, gold)) return 1;
        Result res;
        res.variant = refs[r].name;
        res.threads = (refs[r].kind == 2) ? -1 : 1;   // -1: task system decides
        res.floatsPerElem = 4;
        res.ms = best * 1000.0;
        results.push_back(res);
    }

    // --- thread sweep, ordinary stores and non-temporal stores --------------
    const int threadCounts[] = { 1, 2, 3, 4, 6, 8 };
    const int numCounts = sizeof(threadCounts) / sizeof(threadCounts[0]);

    for (int variant = 0; variant < 2; ++variant) {
        KernelFn fn = (variant == 0) ? saxpyPlain : saxpyStream;
        const char* name = (variant == 0) ? "avx2_store" : "avx2_stream";
        int model = (variant == 0) ? 4 : 3;
        if (onlyVariant && strcmp(onlyVariant, name) != 0) continue;
        for (int c = 0; c < numCounts; ++c) {
            int T = threadCounts[c];
            if (onlyVariant && T != onlyThreads) continue;
            double best = 1e30;
            if (onlyVariant) memset(out, 0, (size_t)N * sizeof(float));
            for (int k = 0; k < runs; ++k) {
                if (!onlyVariant) memset(out, 0, (size_t)N * sizeof(float));
                double t0 = CycleTimer::currentSeconds();
                runThreaded(fn, T, SCALE, X, Y, out);
                double t1 = CycleTimer::currentSeconds();
                best = std::min(best, t1 - t0);
            }
            if (!verify(out, gold)) return 1;
            Result res;
            res.variant = name;
            res.threads = T;
            res.floatsPerElem = model;
            res.ms = best * 1000.0;
            results.push_back(res);
        }
    }

    // --- report -------------------------------------------------------------
    if (results.empty()) {
        fprintf(stderr, "no variant matched '%s'\n",
                onlyVariant ? onlyVariant : "(none)");
        return 1;
    }

    printf("saxpy bandwidth study   N = %d elements, %d runs per configuration,"
           " minimum reported\n\n", N, runs);
    printf("%-14s %8s %10s %14s %8s %12s\n",
           "variant", "threads", "ms", "GB/s (as", "traffic", "DRAM GB/s");
    printf("%-14s %8s %10s %14s %8s %12s\n",
           "", "", "", "reported)", "model", "(decimal)");
    printf("--------------------------------------------------"
           "--------------------------\n");
    for (size_t i = 0; i < results.size(); ++i) {
        const Result& r = results[i];
        char threads[16];
        if (r.threads < 0) snprintf(threads, sizeof(threads), "tasks");
        else               snprintf(threads, sizeof(threads), "%d", r.threads);
        printf("%-14s %8s %10.3f %14.3f %8dN %12.3f\n",
               r.variant.c_str(), threads, r.ms,
               reportedGBs(r.ms / 1000.0), r.floatsPerElem,
               dramGBs(r.ms / 1000.0, r.floatsPerElem));
    }

    if (csvPath) {
        FILE* f = fopen(csvPath, "w");
        if (!f) { fprintf(stderr, "cannot write %s\n", csvPath); return 1; }
        fprintf(f, "variant,threads,ms,reported_gibs,traffic_floats,dram_gbs\n");
        for (size_t i = 0; i < results.size(); ++i) {
            const Result& r = results[i];
            fprintf(f, "%s,%d,%.4f,%.4f,%d,%.4f\n",
                    r.variant.c_str(), r.threads, r.ms,
                    reportedGBs(r.ms / 1000.0), r.floatsPerElem,
                    dramGBs(r.ms / 1000.0, r.floatsPerElem));
        }
        fclose(f);
        printf("\nwrote %s\n", csvPath);
    }

    free(X); free(Y); free(gold); free(out);
    return 0;
}
