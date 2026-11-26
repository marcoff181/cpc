#include <inttypes.h>
#include <mpi.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "cpc/common.h"
#include "cpc/fm85.h"
#include "cpc/icon.h"
#include <sys/types.h>
#include <xxhash.h>

#define HASH_SEED 37 // TODO: move to caller

typedef struct {
  Long coupons;
  double estimate;
  double preload_seconds;
  double sketch_seconds;
} RunResult;

struct timespec monotonic_now(void) {
  struct timespec timestamp;
  if (clock_gettime(CLOCK_MONOTONIC, &timestamp) != 0) {
    perror("clock_gettime");
    exit(EXIT_FAILURE);
  }
  return timestamp;
}

double elapsed_seconds(struct timespec start, struct timespec end) {
  return (double)(end.tv_sec - start.tv_sec) +
         (double)(end.tv_nsec - start.tv_nsec) / 1e9;
}

int main(int argc, char **argv) {
  MPI_Init(NULL, NULL);

  int world_size, rank;

  MPI_Comm_size(MPI_COMM_WORLD, &world_size);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  if (argc < 6 || argc > 7) {
    fprintf(stderr,
            "Usage: %s <lgK> <unique_count> <total_count> <warmup_runs> "
            "<measurement_runs> \n",
            argv[0]);
    return EXIT_FAILURE;
  }

  // parse argv
  long lgK = strtol(argv[1], NULL, 10);
  long K = 1 << lgK;
  long unique = strtol(argv[2], NULL, 10);
  long total = strtol(argv[3], NULL, 10);

  // validate arguments
  if (lgK < 4 || lgK > 26) {
    fprintf(stderr, "Error: lgK must be an integer between 4 and 26\n");
    return EXIT_FAILURE;
  }

  RunResult result;

  fm85Init();
  FM85 *sketch = fm85Make(lgK);

  U64 x;
  // fake stream: a loop over the naturals up to world_size*n is split
  // between processes
  for (U64 i = rank * total; i < (rank * total) + total; i++) {
    // as long as n>m the unique count is guaranteed to be m
    x = i % unique;
    XXH128_hash_t hash = XXH3_128bits_withSeed(&x, sizeof(U64), HASH_SEED);
    fm85Update(sketch, hash.low64, hash.high64);
  }

  // get bitmap matrix (K rows, where a row is a U64)
  U64 *local = bitMatrixOfSketch(sketch);
  fm85Free(sketch);

  U64 *merged = NULL;
  if (rank == 0)
    merged = malloc(K * sizeof(U64));

  // OR the rows of the matrix while sending them over
  MPI_Reduce(local, merged, (int)K, MPI_UINT64_T, MPI_BOR, 0, MPI_COMM_WORLD);

  // count the ones
  if (rank == 0) {
    U64 coupons = 0;
    for (int i = 0; i < K; i++) {
      // your compiler supports this, right? ;)
      coupons += __builtin_popcountll(merged[i]);
    }

    double estimate = getIconEstimate(lgK, coupons);
    struct timespec estimated = monotonic_now();

    result.coupons = sketch->numCoupons;
    result.estimate = estimate;
    // result.preload_seconds = elapsed_seconds(start, loaded);
    // result.sketch_seconds = elapsed_seconds(loaded, estimated);
    //
    printf("Input:    %s\n", argv[1]);
    printf("lgK:      %ld\n", lgK);

    printf("Coupons:  %" PRId64 "\n", result.coupons);
    printf("Estimate: %lf\n", result.estimate);
    // if (preload) {
    //   printf("Preload time: %.9f s\n", result.preload_seconds);
    //   printf("Sketch time:  %.9f s\n", result.sketch_seconds);
    // } else {
    //   // printf("End-to-end time: %.9f s\n", end_to_end_seconds);
    // }
  }

  MPI_Finalize();
}
