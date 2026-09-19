// Extra credit evidence: what a thread costs that a task does not.
//
// ISPC tasks are queue entries executed by a fixed pool, so their marginal
// cost is an enqueue and a dequeue. std::threads are kernel scheduling
// entities, so their marginal cost is a clone(), a stack, and a scheduler
// slot. This measures the second directly; the first is measured by the task
// sweep in measure_prog3.sh, where going from 200 to 800 tasks changes the
// runtime by microseconds.
//
// Build: g++ -O2 -std=c++11 threads_vs_tasks.cpp -o threads_vs_tasks -lpthread
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <thread>
#include <vector>
#include <chrono>

static volatile long sink = 0;

int main(int argc, char** argv) {
    const int N = (argc > 1) ? atoi(argv[1]) : 10000;

    // Peak virtual address space, to show what 10000 stacks reserve.
    auto vmpeak = []() {
        FILE* f = fopen("/proc/self/status", "r");
        char line[256]; long kb = 0;
        while (f && fgets(line, sizeof line, f))
            if (sscanf(line, "VmPeak: %ld kB", &kb) == 1) break;
        if (f) fclose(f);
        return kb;
    };

    printf("Creating and joining %d std::threads, each doing trivial work.\n", N);
    printf("VmPeak before: %ld kB\n", vmpeak());

    auto t0 = std::chrono::steady_clock::now();
    const int batch = 500;              // all at once would exceed thread limits
    for (int done = 0; done < N; done += batch) {
        int n = std::min(batch, N - done);
        std::vector<std::thread> ts;
        ts.reserve(n);
        for (int i = 0; i < n; i++)
            ts.emplace_back([]{ sink += 1; });
        for (auto& t : ts) t.join();
    }
    auto t1 = std::chrono::steady_clock::now();

    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    printf("VmPeak after:  %ld kB\n", vmpeak());
    printf("Total: %.2f ms for %d threads -> %.1f us per thread\n",
           ms, N, 1000.0 * ms / N);
    printf("(created in batches of %d; %d simultaneously live threads is\n"
           " itself close to the default per-user limit)\n", batch, N);
    return 0;
}
