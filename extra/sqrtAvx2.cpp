// Extra credit: the same Newton iteration written directly with AVX2
// intrinsics, to compare against what ISPC generates from sqrt.ispc.
//
// The structure mirrors the ISPC kernel exactly so the comparison is of code
// generation and not of algorithm: eight floats per vector, a mask of lanes
// that have not yet converged, and a loop that runs until every lane in the
// vector is done.
//
// The Makefile already builds everything with -march=native, so the scalar
// baseline gets FMA too; checking the disassembly of sqrtSerial.o confirms it
// contains FMA instructions but no 256-bit (ymm) registers, because the
// data-dependent while loop blocks auto-vectorization. The comparison is
// therefore scalar-with-FMA against vector-with-FMA, which is the fair one.
#include <immintrin.h>
#include <math.h>

void sqrtAvx2(int N, float initialGuess, float values[], float output[]) {

    static const float kThreshold = 0.00001f;

    const __m256 vThreshold = _mm256_set1_ps(kThreshold);
    const __m256 vOne       = _mm256_set1_ps(1.f);
    const __m256 vThree     = _mm256_set1_ps(3.f);
    const __m256 vHalf      = _mm256_set1_ps(0.5f);
    // Clearing the sign bit is fabs() without a branch or a call.
    const __m256 vAbsMask   =
        _mm256_castsi256_ps(_mm256_set1_epi32(0x7fffffff));

    int i = 0;
    for (; i + 8 <= N; i += 8) {

        __m256 x     = _mm256_loadu_ps(values + i);
        __m256 guess = _mm256_set1_ps(initialGuess);

        __m256 g2   = _mm256_mul_ps(guess, guess);
        __m256 pred = _mm256_and_ps(
            _mm256_sub_ps(_mm256_mul_ps(g2, x), vOne), vAbsMask);

        // A lane stays active while its error is above the threshold. The
        // loop continues while any lane is active, which is the same
        // all-lanes-wait-for-the-slowest behaviour the ISPC gang has.
        __m256 active = _mm256_cmp_ps(pred, vThreshold, _CMP_GT_OQ);

        while (_mm256_movemask_ps(active) != 0) {

            // newGuess = (3*guess - x*guess^3) * 0.5
            __m256 g3 = _mm256_mul_ps(g2, guess);
            __m256 ng = _mm256_mul_ps(
                _mm256_sub_ps(_mm256_mul_ps(vThree, guess),
                              _mm256_mul_ps(x, g3)),
                vHalf);

            // Converged lanes keep their existing guess untouched.
            guess = _mm256_blendv_ps(guess, ng, active);

            g2   = _mm256_mul_ps(guess, guess);
            pred = _mm256_and_ps(
                _mm256_sub_ps(_mm256_mul_ps(g2, x), vOne), vAbsMask);
            active = _mm256_cmp_ps(pred, vThreshold, _CMP_GT_OQ);
        }

        _mm256_storeu_ps(output + i, _mm256_mul_ps(x, guess));
    }

    // Tail, for an N that is not a multiple of eight.
    for (; i < N; i++) {
        float x = values[i];
        float guess = initialGuess;
        float error = fabs(guess * guess * x - 1.f);
        while (error > kThreshold) {
            guess = (3.f * guess - x * guess * guess * guess) * 0.5f;
            error = fabs(guess * guess * x - 1.f);
        }
        output[i] = x * guess;
    }
}
