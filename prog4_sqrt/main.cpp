#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <pthread.h>
#include <math.h>

#include "CycleTimer.h"
#include "sqrt_ispc.h"

using namespace ispc;

extern void sqrtSerial(int N, float startGuess, float* values, float* output);
extern void sqrtAvx2(int N, float startGuess, float* values, float* output);

// The ISPC target is avx2-i32x8, so a gang is eight lanes wide. The
// worst-case input below is built around this number: it is the period over
// which lanes must agree for the vector unit to be used efficiently.
static const int GANG_WIDTH = 8;

// Values the Newton iteration converges on quickly or slowly. The iteration
// starts from a guess of 1.0, so an input of exactly 1.0 converges before the
// loop body runs even once, and an input near the top of the supported (0,3)
// range takes the most iterations of any legal input.
static const float FAST_VALUE = 1.f;
static const float SLOW_VALUE = 2.999f;

// Number of Newton iterations sqrtSerial performs for one input, replicating
// its loop exactly. Used to predict the achievable SIMD speedup from the input
// data alone, independently of any timing.
static int iterationsFor(float x, float initialGuess) {
    static const float kThreshold = 0.00001f;
    float guess = initialGuess;
    float error = fabs(guess * guess * x - 1.f);
    int n = 0;
    while (error > kThreshold) {
        guess = (3.f * guess - x * guess * guess * guess) * 0.5f;
        error = fabs(guess * guess * x - 1.f);
        n++;
    }
    return n;
}

// A gang runs until its slowest lane converges, so the vector unit issues
// max(iterations) for each group of GANG_WIDTH elements while the serial code
// issues their sum. The ratio is the best SIMD speedup the input permits,
// before any memory or instruction-issue effects.
static void reportInputProfile(int N, float initialGuess, float* values) {
    long serialIters = 0, gangIters = 0;
    int minIter = 1 << 30, maxIter = 0;
    for (int i = 0; i < N; i += GANG_WIDTH) {
        int worst = 0;
        for (int j = i; j < i + GANG_WIDTH && j < N; j++) {
            int it = iterationsFor(values[j], initialGuess);
            serialIters += it;
            if (it > worst) worst = it;
            if (it < minIter) minIter = it;
            if (it > maxIter) maxIter = it;
        }
        gangIters += worst;
    }
    printf("  iterations per element: min %d, max %d\n", minIter, maxIter);
    printf("  total Newton iterations: %ld serial, %ld gang-issued\n",
           serialIters, gangIters);
    printf("  predicted SIMD ceiling from divergence alone: %.2fx (of %d)\n\n",
           gangIters ? (double)serialIters / gangIters : 0.0, GANG_WIDTH);
}

static void verifyResult(int N, float* result, float* gold) {
    for (int i=0; i<N; i++) {
        if (fabs(result[i] - gold[i]) > 1e-4) {
            printf("Error: [%d] Got %f expected %f\n", i, result[i], gold[i]);
        }
    }
}

int main(int argc, char** argv) {

    const unsigned int N = 20 * 1000 * 1000;
    const float initialGuess = 1.0f;

    // Input pattern: "random" (as shipped), "best" or "worst".
    const char* mode = (argc > 1) ? argv[1] : "random";

    float* values = new float[N];
    float* output = new float[N];
    float* gold = new float[N];

    if (strcmp(mode, "best") == 0) {

        // Best case: every lane gets the same slowly-converging value. Two
        // things matter, and they are separate. Uniformity removes divergence
        // entirely, so no lane ever idles while its neighbours iterate. And
        // choosing the *slowest* legal value maximises arithmetic per element,
        // so the loop body dominates the one load and one store that bracket
        // it. A uniform array of FAST_VALUE would also be divergence-free but
        // would do no arithmetic at all, leaving only memory traffic to
        // parallelise.
        for (unsigned int i = 0; i < N; i++)
            values[i] = SLOW_VALUE;

    } else if (strcmp(mode, "worst") == 0) {

        // Worst case: one slow lane per gang, the rest converging instantly.
        // The gang runs until its slowest lane finishes, so every gang pays
        // the full iteration count of SLOW_VALUE while only one lane in eight
        // needed it. The vector unit therefore performs the same number of
        // useful iterations as the serial code and SIMD speedup collapses
        // towards 1x. Alignment matters: the pattern must have period
        // GANG_WIDTH and be phase-aligned to gang boundaries, or the slow
        // elements would spread across gangs and the effect would weaken.
        for (unsigned int i = 0; i < N; i++)
            values[i] = (i % GANG_WIDTH == 0) ? SLOW_VALUE : FAST_VALUE;

    } else {

        // Starter code: uniformly random values across the supported range.
        for (unsigned int i = 0; i < N; i++)
            values[i] = .001f + 2.998f * static_cast<float>(rand()) / RAND_MAX;
    }

    printf("[input pattern]:\t%s\n", mode);
    reportInputProfile(N, initialGuess, values);

    // generate a gold version to check results
    for (unsigned int i=0; i<N; i++)
        gold[i] = sqrt(values[i]);

    //
    // And run the serial implementation 3 times, again reporting the
    // minimum time.
    //
    double minSerial = 1e30;
    for (int i = 0; i < 3; ++i) {
        double startTime = CycleTimer::currentSeconds();
        sqrtSerial(N, initialGuess, values, output);
        double endTime = CycleTimer::currentSeconds();
        minSerial = std::min(minSerial, endTime - startTime);
    }

    printf("[sqrt serial]:\t\t[%.3f] ms\n", minSerial * 1000);

    verifyResult(N, output, gold);

    //
    // Compute the image using the ispc implementation; report the minimum
    // time of three runs.
    //
    double minISPC = 1e30;
    for (int i = 0; i < 3; ++i) {
        double startTime = CycleTimer::currentSeconds();
        sqrt_ispc(N, initialGuess, values, output);
        double endTime = CycleTimer::currentSeconds();
        minISPC = std::min(minISPC, endTime - startTime);
    }

    printf("[sqrt ispc]:\t\t[%.3f] ms\n", minISPC * 1000);

    verifyResult(N, output, gold);

    // Clear out the buffer
    for (unsigned int i = 0; i < N; ++i)
        output[i] = 0;

    //
    // Tasking version of the ISPC code
    //
    double minTaskISPC = 1e30;
    for (int i = 0; i < 3; ++i) {
        double startTime = CycleTimer::currentSeconds();
        sqrt_ispc_withtasks(N, initialGuess, values, output);
        double endTime = CycleTimer::currentSeconds();
        minTaskISPC = std::min(minTaskISPC, endTime - startTime);
    }

    printf("[sqrt task ispc]:\t[%.3f] ms\n", minTaskISPC * 1000);

    verifyResult(N, output, gold);

    // Clear out the buffer
    for (unsigned int i = 0; i < N; ++i)
        output[i] = 0;

    //
    // Hand-written AVX2 intrinsics version (extra credit)
    //
    double minAvx2 = 1e30;
    for (int i = 0; i < 3; ++i) {
        double startTime = CycleTimer::currentSeconds();
        sqrtAvx2(N, initialGuess, values, output);
        double endTime = CycleTimer::currentSeconds();
        minAvx2 = std::min(minAvx2, endTime - startTime);
    }

    printf("[sqrt avx2 intrin]:\t[%.3f] ms\n", minAvx2 * 1000);

    verifyResult(N, output, gold);

    printf("\t\t\t\t(%.2fx speedup from ISPC)\n", minSerial/minISPC);
    printf("\t\t\t\t(%.2fx speedup from task ISPC)\n", minSerial/minTaskISPC);
    printf("\t\t\t\t(%.2fx speedup from AVX2 intrinsics)\n", minSerial/minAvx2);

    delete [] values;
    delete [] output;
    delete [] gold;

    return 0;
}
