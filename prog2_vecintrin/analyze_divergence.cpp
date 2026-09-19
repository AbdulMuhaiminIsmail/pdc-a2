// Quantifies why vector utilization falls as VECTOR_WIDTH grows.
//
// clampedExpVector's inner loop runs once per iteration of the *slowest* lane
// in the vector, so a vector covering exponents {1, 9} costs 9 iterations and
// wastes 8 of them in the first lane. This program reproduces exactly the
// exponent array main.cpp generates (same rand() sequence, same EXP_MAX) and
// reports, for each width, how much of the loop's lane capacity is useful.
//
// Build: g++ -O2 analyze_divergence.cpp -o analyze_divergence
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <algorithm>

#define EXP_MAX 10

int main(int argc, char** argv) {
    const int N = (argc > 1) ? atoi(argv[1]) : 10000;

    // main.cpp draws a float then an int per element, in that order.
    std::vector<int> exponents(N);
    for (int i = 0; i < N; i++) {
        (void)rand();                       // consumes the values[i] draw
        exponents[i] = rand() % EXP_MAX;
    }

    long totalExp = 0;
    for (int e : exponents) totalExp += e;

    const bool csv = (argc > 2 && argv[2][0] == 'c');

    if (csv) {
        printf("width,required_mults,loop_iters,lane_capacity,loop_util_pct\n");
    } else {
        printf("N = %d, exponents uniform on [0,%d]\n", N, EXP_MAX - 1);
        printf("Total multiplications actually required: %ld\n\n", totalExp);
        printf("%6s %14s %16s %14s\n",
               "width", "loop iters", "lane capacity", "loop util.");
    }
    for (int w : {2, 4, 8, 16}) {
        long iters = 0;
        for (int i = 0; i < N; i += w) {
            int hi = 0;
            for (int j = i; j < std::min(i + w, N); j++)
                hi = std::max(hi, exponents[j]);
            iters += hi;                    // the vector runs until its slowest lane
        }
        long capacity = iters * w;
        if (csv)
            printf("%d,%ld,%ld,%ld,%.1f\n",
                   w, totalExp, iters, capacity, 100.0 * totalExp / capacity);
        else
            printf("%6d %14ld %16ld %13.1f%%\n",
                   w, iters, capacity, 100.0 * totalExp / capacity);
    }
    return 0;
}
