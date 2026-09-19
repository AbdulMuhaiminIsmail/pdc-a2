#include <cstdlib>
#include <stdio.h>
#include <algorithm>
#include <thread>

#include "CycleTimer.h"

typedef struct {
    float x0, x1;
    float y0, y1;
    unsigned int width;
    unsigned int height;
    int maxIterations;
    int* output;
    int threadId;
    int numThreads;
} WorkerArgs;


extern void mandelbrotSerial(
    float x0, float y0, float x1, float y1,
    int width, int height,
    int startRow, int numRows,
    int maxIterations,
    int output[]);


// Part 3 instrumentation.  Reporting per-thread times means a printf inside
// the region main.cpp is timing, so it is off unless MANDEL_THREAD_TIMES is
// set in the environment.  The getenv() happens once during static
// initialisation, before main() runs, so the steady-state path reads a bool.
static bool envEnabled(const char* name) {
    const char* v = getenv(name);
    return v != NULL && v[0] != '\0' && v[0] != '0';
}

static const bool reportThreadTimes = envEnabled("MANDEL_THREAD_TIMES");

// Part 4 ships row-cyclic decomposition as the default. The part 1-3 block
// decomposition stays reachable via MANDEL_BLOCK=1 so both policies can be
// measured from one binary; it is not a per-thread-count special case.
static const bool useBlockDecomposition = envEnabled("MANDEL_BLOCK");


//
// workerThreadStart --
//
// Thread entrypoint.
void workerThreadStart(WorkerArgs * const args) {

    const double threadStartTime = reportThreadTimes ? CycleTimer::currentSeconds() : 0.0;

    const int height = static_cast<int>(args->height);
    const int numThreads = args->numThreads;
    const int threadId = args->threadId;

    int rowsDone = 0;

    if (useBlockDecomposition) {

        // Contiguous block decomposition: thread i owns one uninterrupted span
        // of rows.  height does not divide evenly for every thread count we
        // test (1200 / 7, for instance), so the first (height % numThreads)
        // threads absorb one extra row rather than dropping the remainder on
        // the last one.
        const int rowsPerThread = height / numThreads;
        const int remainder = height % numThreads;

        const int startRow = threadId * rowsPerThread + std::min(threadId, remainder);
        const int numRows = rowsPerThread + (threadId < remainder ? 1 : 0);

        if (numRows > 0) {
            mandelbrotSerial(args->x0, args->y0, args->x1, args->y1,
                             args->width, height,
                             startRow, numRows,
                             args->maxIterations, args->output);
        }
        rowsDone = numRows;

    } else {

        // Row-cyclic decomposition: thread i owns rows i, i+numThreads,
        // i+2*numThreads, ...  Cost varies smoothly and symmetrically down the
        // image, so interleaving hands every thread a near-identical mix of
        // cheap and expensive rows without any synchronisation, and the same
        // rule works unchanged at every thread count.  The rows a thread owns
        // are not contiguous, so this is one mandelbrotSerial call per row.
        for (int row = threadId; row < height; row += numThreads) {
            mandelbrotSerial(args->x0, args->y0, args->x1, args->y1,
                             args->width, height,
                             row, 1,
                             args->maxIterations, args->output);
            rowsDone++;
        }
    }

    if (reportThreadTimes) {
        const double elapsed = CycleTimer::currentSeconds() - threadStartTime;
        printf("[thread %2d of %2d]\t%s\t%4d rows\t%8.3f ms\n",
               threadId, numThreads,
               useBlockDecomposition ? "block " : "cyclic",
               rowsDone, elapsed * 1000);
    }
}

//
// MandelbrotThread --
//
// Multi-threaded implementation of mandelbrot set image generation.
// Threads of execution are created by spawning std::threads.
void mandelbrotThread(
    int numThreads,
    float x0, float y0, float x1, float y1,
    int width, int height,
    int maxIterations, int output[])
{
    static constexpr int MAX_THREADS = 32;

    if (numThreads > MAX_THREADS)
    {
        fprintf(stderr, "Error: Max allowed threads is %d\n", MAX_THREADS);
        exit(1);
    }

    // Creates thread objects that do not yet represent a thread.
    std::thread workers[MAX_THREADS];
    WorkerArgs args[MAX_THREADS];

    for (int i=0; i<numThreads; i++) {
        args[i].x0 = x0;
        args[i].y0 = y0;
        args[i].x1 = x1;
        args[i].y1 = y1;
        args[i].width = width;
        args[i].height = height;
        args[i].maxIterations = maxIterations;
        args[i].numThreads = numThreads;
        args[i].output = output;

        args[i].threadId = i;
    }

    // Spawn the worker threads.  Note that only numThreads-1 std::threads
    // are created and the main application thread is used as a worker
    // as well.
    for (int i=1; i<numThreads; i++) {
        workers[i] = std::thread(workerThreadStart, &args[i]);
    }

    workerThreadStart(&args[0]);

    // join worker threads
    for (int i=1; i<numThreads; i++) {
        workers[i].join();
    }
}
