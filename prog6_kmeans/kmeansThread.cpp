#include <algorithm>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <thread>

#include <cstring>
#include <vector>

#include "CycleTimer.h"

using namespace std;

// ---------------------------------------------------------------------------
// Configuration, read once from the environment.
//
// The defaults are the fast path: calling ./kmeans with no environment set
// runs the final parallel-over-M implementation on every hardware context.
// The overrides exist so that the sequence of experiments in the write-up can
// be reproduced from a single binary rather than from several source edits.
//
//   KMEANS_THREADS=<n>       worker threads (default: hardware_concurrency)
//   KMEANS_VARIANT=serial    the original implementation, unchanged
//   KMEANS_VARIANT=byk       parallel over cluster index k
//   KMEANS_VARIANT=bym       parallel over data point index m  (default)
//   KMEANS_PROFILE=1         print the per-phase timing breakdown
// ---------------------------------------------------------------------------

enum Variant { VARIANT_SERIAL, VARIANT_BY_K, VARIANT_BY_M };

static int workerThreads() {
  const char *env = getenv("KMEANS_THREADS");
  if (env) {
    int t = atoi(env);
    if (t > 0) return t;
  }
  unsigned hw = std::thread::hardware_concurrency();
  return hw > 0 ? (int)hw : 8;
}

static Variant selectedVariant() {
  const char *env = getenv("KMEANS_VARIANT");
  if (env) {
    if (strcmp(env, "serial") == 0) return VARIANT_SERIAL;
    if (strcmp(env, "byk") == 0)    return VARIANT_BY_K;
  }
  return VARIANT_BY_M;
}

static const int kThreads = workerThreads();
static const Variant kVariant = selectedVariant();

typedef struct {
  // Control work assignments
  int start, end;

  // Shared by all functions
  double *data;
  double *clusterCentroids;
  int *clusterAssignments;
  double *currCost;
  int M, N, K;
} WorkerArgs;


/**
 * Checks if the algorithm has converged.
 * 
 * @param prevCost Pointer to the K dimensional array containing cluster costs 
 *    from the previous iteration.
 * @param currCost Pointer to the K dimensional array containing cluster costs 
 *    from the current iteration.
 * @param epsilon Predefined hyperparameter which is used to determine when
 *    the algorithm has converged.
 * @param K The number of clusters.
 * 
 * NOTE: DO NOT MODIFY THIS FUNCTION!!!
 */
static bool stoppingConditionMet(double *prevCost, double *currCost,
                                 double epsilon, int K) {
  for (int k = 0; k < K; k++) {
    if (abs(prevCost[k] - currCost[k]) > epsilon)
      return false;
  }
  return true;
}

/**
 * Computes L2 distance between two points of dimension nDim.
 * 
 * @param x Pointer to the beginning of the array representing the first
 *     data point.
 * @param y Poitner to the beginning of the array representing the second
 *     data point.
 * @param nDim The dimensionality (number of elements) in each data point
 *     (must be the same for x and y).
 */
double dist(double *x, double *y, int nDim) {
  double accum = 0.0;
  for (int i = 0; i < nDim; i++) {
    accum += pow((x[i] - y[i]), 2);
  }
  return sqrt(accum);
}

/**
 * Assigns each data point to its "closest" cluster centroid.
 *
 * The original implementation (retained below as assignSerial) loops over
 * cluster index k on the outside and over data point m on the inside. That
 * ordering re-reads the whole 800 MB data array once per cluster, and it makes
 * the natural unit of parallel work the cluster index -- of which there are
 * only K = 3, so it cannot fill eight hardware contexts.
 *
 * assignRangeOfPoints inverts the loops: each data point is read once and
 * compared against all K centroids while it is still in cache. That makes m
 * the unit of work, of which there are a million, and it removes the need for
 * the shared minDist array entirely because each point's minimum is a local.
 */

// The original body, kept so that the serial baseline can be re-measured from
// this binary and so the write-up's step-by-step comparison is reproducible.
static void assignSerial(WorkerArgs *const args) {
  double *minDist = new double[args->M];

  for (int m = 0; m < args->M; m++) {
    minDist[m] = 1e30;
    args->clusterAssignments[m] = -1;
  }

  for (int k = args->start; k < args->end; k++) {
    for (int m = 0; m < args->M; m++) {
      double d = dist(&args->data[m * args->N],
                      &args->clusterCentroids[k * args->N], args->N);
      if (d < minDist[m]) {
        minDist[m] = d;
        args->clusterAssignments[m] = k;
      }
    }
  }

  delete[] minDist;
}

// Assigns the data points in [mStart, mEnd) against every centroid in
// [args->start, args->end). Touches only its own slice of clusterAssignments,
// so no synchronisation is needed.
static void assignRangeOfPoints(WorkerArgs *const args, int mStart, int mEnd) {
  const int N = args->N;
  for (int m = mStart; m < mEnd; m++) {
    const double *point = &args->data[(size_t)m * N];
    double best = 1e30;
    int bestK = -1;
    for (int k = args->start; k < args->end; k++) {
      double d = dist(const_cast<double *>(point),
                      &args->clusterCentroids[k * N], N);
      if (d < best) {
        best = d;
        bestK = k;
      }
    }
    args->clusterAssignments[m] = bestK;
  }
}

// Parallel over cluster index. Each thread owns a disjoint range of k, but
// every thread would otherwise race on the shared minDist/assignment arrays,
// so each keeps private ones and a serial merge picks the winner per point.
// Included to show why this axis is the wrong one, not because it is good.
static void assignByCluster(WorkerArgs *const args) {
  const int kRange = args->end - args->start;
  const int nThreads = std::min(kThreads, std::max(kRange, 1));

  std::vector<std::vector<double> > minDist(nThreads);
  std::vector<std::vector<int> > best(nThreads);
  std::vector<std::thread> pool;

  const int per = (kRange + nThreads - 1) / nThreads;

  for (int t = 0; t < nThreads; t++) {
    minDist[t].assign(args->M, 1e30);
    best[t].assign(args->M, -1);
  }

  for (int t = 0; t < nThreads; t++) {
    int kBegin = args->start + t * per;
    int kEnd = std::min(args->end, kBegin + per);
    if (kBegin >= kEnd) break;
    pool.push_back(std::thread([=, &minDist, &best]() {
      for (int k = kBegin; k < kEnd; k++) {
        for (int m = 0; m < args->M; m++) {
          double d = dist(&args->data[(size_t)m * args->N],
                          &args->clusterCentroids[k * args->N], args->N);
          if (d < minDist[t][m]) {
            minDist[t][m] = d;
            best[t][m] = k;
          }
        }
      }
    }));
  }
  for (size_t t = 0; t < pool.size(); t++) pool[t].join();

  for (int m = 0; m < args->M; m++) {
    double bd = 1e30;
    int bk = -1;
    for (size_t t = 0; t < pool.size(); t++) {
      if (minDist[t][m] < bd) {
        bd = minDist[t][m];
        bk = best[t][m];
      }
    }
    args->clusterAssignments[m] = bk;
  }
}

// Parallel over data point index. This is the implementation that runs by
// default. M = 1,000,000 divides evenly across any thread count, every thread
// does identical work, and the ranges are contiguous so each thread streams
// its own region of the data array.
static void assignByPoint(WorkerArgs *const args) {
  const int nThreads = std::max(1, std::min(kThreads, args->M));
  if (nThreads == 1) {
    assignRangeOfPoints(args, 0, args->M);
    return;
  }

  const int per = (args->M + nThreads - 1) / nThreads;
  std::vector<std::thread> pool;
  pool.reserve(nThreads);

  for (int t = 0; t < nThreads; t++) {
    int mBegin = t * per;
    if (mBegin >= args->M) break;
    int mEnd = std::min(args->M, mBegin + per);
    pool.push_back(std::thread(assignRangeOfPoints, args, mBegin, mEnd));
  }
  for (size_t t = 0; t < pool.size(); t++) pool[t].join();
}

void computeAssignments(WorkerArgs *const args) {
  switch (kVariant) {
    case VARIANT_SERIAL: assignSerial(args);     break;
    case VARIANT_BY_K:   assignByCluster(args);  break;
    default:             assignByPoint(args);    break;
  }
}

/**
 * Given the cluster assignments, computes the new centroid locations for
 * each cluster.
 */
void computeCentroids(WorkerArgs *const args) {
  int *counts = new int[args->K];

  // Zero things out
  for (int k = 0; k < args->K; k++) {
    counts[k] = 0;
    for (int n = 0; n < args->N; n++) {
      args->clusterCentroids[k * args->N + n] = 0.0;
    }
  }


  // Sum up contributions from assigned examples
  for (int m = 0; m < args->M; m++) {
    int k = args->clusterAssignments[m];
    for (int n = 0; n < args->N; n++) {
      args->clusterCentroids[k * args->N + n] +=
          args->data[m * args->N + n];
    }
    counts[k]++;
  }

  // Compute means
  for (int k = 0; k < args->K; k++) {
    counts[k] = max(counts[k], 1); // prevent divide by 0
    for (int n = 0; n < args->N; n++) {
      args->clusterCentroids[k * args->N + n] /= counts[k];
    }
  }

  delete[] counts;
}

/**
 * Computes the per-cluster cost. Used to check if the algorithm has converged.
 */
void computeCost(WorkerArgs *const args) {
  double *accum = new double[args->K];

  // Zero things out
  for (int k = 0; k < args->K; k++) {
    accum[k] = 0.0;
  }

  // Sum cost for all data points assigned to centroid
  for (int m = 0; m < args->M; m++) {
    int k = args->clusterAssignments[m];
    accum[k] += dist(&args->data[m * args->N],
                     &args->clusterCentroids[k * args->N], args->N);
  }

  // Update costs
  for (int k = args->start; k < args->end; k++) {
    args->currCost[k] = accum[k];
  }

  delete[] accum;
}

/**
 * Computes the K-Means algorithm, using std::thread to parallelize the work.
 *
 * @param data Pointer to an array of length M*N representing the M different N 
 *     dimensional data points clustered. The data is layed out in a "data point
 *     major" format, so that data[i*N] is the start of the i'th data point in 
 *     the array. The N values of the i'th datapoint are the N values in the 
 *     range data[i*N] to data[(i+1) * N].
 * @param clusterCentroids Pointer to an array of length K*N representing the K 
 *     different N dimensional cluster centroids. The data is laid out in
 *     the same way as explained above for data.
 * @param clusterAssignments Pointer to an array of length M representing the
 *     cluster assignments of each data point, where clusterAssignments[i] = j
 *     indicates that data point i is closest to cluster centroid j.
 * @param M The number of data points to cluster.
 * @param N The dimensionality of the data points.
 * @param K The number of cluster centroids.
 * @param epsilon The algorithm is said to have converged when
 *     |currCost[i] - prevCost[i]| < epsilon for all i where i = 0, 1, ..., K-1
 */
void kMeansThread(double *data, double *clusterCentroids, int *clusterAssignments,
               int M, int N, int K, double epsilon) {

  // Used to track convergence
  double *prevCost = new double[K];
  double *currCost = new double[K];

  // The WorkerArgs array is used to pass inputs to and return output from
  // functions.
  WorkerArgs args;
  args.data = data;
  args.clusterCentroids = clusterCentroids;
  args.clusterAssignments = clusterAssignments;
  args.currCost = currCost;
  args.M = M;
  args.N = N;
  args.K = K;

  // Initialize arrays to track cost
  for (int k = 0; k < K; k++) {
    prevCost[k] = 1e30;
    currCost[k] = 0.0;
  }

  /* Main K-Means Algorithm Loop */
  int iter = 0;
  double tAssign = 0.0, tCentroids = 0.0, tCost = 0.0;
  while (!stoppingConditionMet(prevCost, currCost, epsilon, K)) {
    // Update cost arrays (for checking convergence criteria)
    for (int k = 0; k < K; k++) {
      prevCost[k] = currCost[k];
    }

    // Setup args struct
    args.start = 0;
    args.end = K;

    double t0 = CycleTimer::currentSeconds();
    computeAssignments(&args);
    double t1 = CycleTimer::currentSeconds();
    computeCentroids(&args);
    double t2 = CycleTimer::currentSeconds();
    computeCost(&args);
    double t3 = CycleTimer::currentSeconds();

    tAssign    += t1 - t0;
    tCentroids += t2 - t1;
    tCost      += t3 - t2;

    iter++;
  }

  if (getenv("KMEANS_PROFILE")) {
    double total = tAssign + tCentroids + tCost;
    fprintf(stderr, "\n  iterations: %d\n", iter);
    fprintf(stderr, "  %-20s %10s %8s %12s\n",
            "function", "total ms", "share", "ms/iter");
    fprintf(stderr, "  %-20s %10.1f %7.1f%% %12.2f\n",
            "computeAssignments", tAssign * 1000, 100 * tAssign / total,
            tAssign * 1000 / iter);
    fprintf(stderr, "  %-20s %10.1f %7.1f%% %12.2f\n",
            "computeCentroids", tCentroids * 1000, 100 * tCentroids / total,
            tCentroids * 1000 / iter);
    fprintf(stderr, "  %-20s %10.1f %7.1f%% %12.2f\n",
            "computeCost", tCost * 1000, 100 * tCost / total,
            tCost * 1000 / iter);
    fprintf(stderr, "  %-20s %10.1f\n", "sum", total * 1000);

    FILE* f = fopen("prog6_profile.csv", "w");
    if (f) {
      fprintf(f, "function,total_ms,share,ms_per_iter,iterations\n");
      fprintf(f, "computeAssignments,%.3f,%.5f,%.3f,%d\n",
              tAssign*1000, tAssign/total, tAssign*1000/iter, iter);
      fprintf(f, "computeCentroids,%.3f,%.5f,%.3f,%d\n",
              tCentroids*1000, tCentroids/total, tCentroids*1000/iter, iter);
      fprintf(f, "computeCost,%.3f,%.5f,%.3f,%d\n",
              tCost*1000, tCost/total, tCost*1000/iter, iter);
      fclose(f);
    }
  }

  delete[] currCost;
  delete[] prevCost;
}
