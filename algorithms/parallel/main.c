#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpc/common.h"

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

  if (argc < 3 || argc > 4) {
    fprintf(stderr, "Usage: %s <input-file> <lgK> [--stream|--preload]\n",
            argv[0]);
    return EXIT_FAILURE;
  }

  const char *mode = argc == 4 ? argv[3] : "--stream";
  bool preload = strcmp(mode, "--preload") == 0;
  if (!preload && strcmp(mode, "--stream") != 0) {
    fprintf(stderr, "Invalid input mode '%s': expected --stream or --preload\n",
            mode);
    return EXIT_FAILURE;
  }

  // Validate lgK
  const char *str_lgK = argv[2];
  char *end;
  long parsed_lgK = strtol(str_lgK, &end, 10);
  /* Cut it short and stop here if lgK is not supported,
  instead of crashing later on when FM85 is calle. See
  FM85 source code (fm85.c and icon.c for example) for
  futher info. */
  if (*str_lgK < '0' || *str_lgK > '9' || *end != '\0' || parsed_lgK < 4 ||
      parsed_lgK > 26) {
    fprintf(stderr, "Error: lgK must be an integer between 4 and 26\n");
    return EXIT_FAILURE;
  }
  Short lgK = (Short)parsed_lgK;

  struct timespec start = monotonic_now();
  FILE *input_file = fopen(argv[1], "r");
  if (NULL == input_file) {
    perror(argv[1]); // perror is more exhaustive
    // fprintf(stderr, "Error: something went wrong while opening %s\n",
    // argv[1]);
    return EXIT_FAILURE;
  }

  RunResult result;
  bool success;
  if (preload) {
    success = runWithPreload(input_file, lgK, start, &result);
  } else {
    success = runWithoutPreload(input_file, lgK, &result);
  }

  fclose(input_file);

  double end_to_end_seconds =
      success && !preload ? elapsed_seconds(start, monotonic_now()) : 0.0;

  printf("Input:    %s\n", argv[1]);
  printf("lgK:      %d\n", lgK);

  if (!success) {
    fprintf(stderr, "Something went wrong\n");
    return EXIT_FAILURE;
  }

  printf("Coupons:  %" PRId64 "\n", result.coupons);
  printf("Estimate: %lf\n", result.estimate);
  if (preload) {
    printf("Preload time: %.9f s\n", result.preload_seconds);
    printf("Sketch time:  %.9f s\n", result.sketch_seconds);
  } else {
    printf("End-to-end time: %.9f s\n", end_to_end_seconds);
  }

  return 0;
}
