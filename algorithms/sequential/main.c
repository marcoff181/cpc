#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cpc/fm85.h"
#include "cpc/icon.h"

#include "xxhash.h"

/* The expected, well-formatted txt file
contains one IPv4 address per line:

    10.1.1.10
    234.192.1.22
    ...
*/

/* Define the binary representation of a IPv4 instance as
an array of 4 bytes; a more convenient way to tell octects
apart */
typedef uint8_t IPv4[4];

#define HASH_SEED 37

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

// TODO: could be moved to utils folder if needs to be used by multiple impls
bool parse_ipv4(const char *input_line, IPv4 address) {

  // Don't use uint8_t yet, you may run into overflows!
  unsigned value, digits;

  for (int octect = 0; octect < 4; octect++) {
    value = 0;
    digits = 0;

    // Make sure che read characters are actual digits
    while ('0' <= *input_line && *input_line <= '9') {
      // Good old manual parsing of decimal numbers
      value = value * 10 + (unsigned)(*input_line - '0');
      // advance to next char
      input_line++;
      // read another digit
      digits++;

      /* Make sure you read something between 0 and 255,
      i.e. at most three digits and no more than 255
      */
      if (digits > 3 || value > 255) {
        return false;
      }
    }

    // This octect does not contain digits!
    if (digits == 0) {
      return false;
    }

    address[octect] = (uint8_t)value;

    /* Make sure that the next octect (if it ever exists:
    we may have reached the fourth) is preceded by a dot. */
    if (octect < 3) {
      if (*input_line != '.') {
        return false;
      }
      input_line++;
    }
  }

  if (*input_line == '\r') {
    input_line++;
  }
  if (*input_line == '\n') {
    input_line++;
  }

  if (*input_line != '\0') {
    return false;
  }
  return true;
}

bool runWithPreload(FILE *input_file, Short lgK, struct timespec start,
                    RunResult *result) {

  char input_line[32];
  unsigned line = 0;

  IPv4 address;
  /* Addresses are first loaded in memory as a single,
  contigous array */
  IPv4 *addresses = NULL;
  uint64_t count = 0; // current number of addresses
  uint64_t capacity =
      0; // current maximum amount of addresses that the array can hold

  /* This loop reads the file and loads all IPs */
  while (fgets(input_line, sizeof(input_line), input_file)) {
    line++;
    if (!parse_ipv4(input_line, address)) {
      fprintf(stderr, "Failed to read IPv4 at line %d\n", line);
      if (NULL != addresses)
        free(addresses);
      return false;
    }

    /* The amount of addresses exceeds the capacity:
    reallocate, doubling the capacity to ensure constant amortized
    cost */
    if (count >= capacity) {
      if (capacity > SIZE_MAX / (2 * sizeof(IPv4))) {
        fprintf(stderr, "Error: memory has been saturated!");
        if (NULL != addresses)
          free(addresses);
        return false;
      }

      capacity = capacity > 0 ? 2 * capacity : 1;
      IPv4 *new_block = realloc(addresses, capacity * sizeof(IPv4));

      if (NULL == new_block) {
        fprintf(stderr, "Error: could not reallocate new blocks!");
        if (NULL != addresses)
          free(addresses);
        return false;
      }

      addresses = new_block;
    }

    memcpy(addresses[count], address, sizeof(IPv4));
    count++;
  }

  if (ferror(input_file)) {
    perror("Failed to read input file");
    free(addresses);
    return false;
  }

  struct timespec loaded = monotonic_now();
  fm85Init();
  FM85 *sketch = fm85Make(lgK);

  for (uint64_t i = 0; i < count; i++) {
    XXH128_hash_t hash =
        XXH3_128bits_withSeed(addresses[i], sizeof(IPv4), HASH_SEED);
    fm85Update(sketch, hash.low64, hash.high64);
  }

  double estimate = getIconEstimate(sketch->lgK, sketch->numCoupons);
  struct timespec estimated = monotonic_now();

  result->coupons = sketch->numCoupons;
  result->estimate = estimate;
  result->preload_seconds = elapsed_seconds(start, loaded);
  result->sketch_seconds = elapsed_seconds(loaded, estimated);

  free(addresses);
  fm85Free(sketch);

  return true;
}

bool runWithoutPreload(FILE *input_file, Short lgK, RunResult *result) {

  char input_line[32];
  unsigned line = 0;

  IPv4 address;

  // STEP: initialize sketch
  fm85Init();
  FM85 *sketch = fm85Make(lgK);

  // STEP: read IPs
  while (fgets(input_line, sizeof(input_line), input_file)) {
    line++;
    if (!parse_ipv4(input_line, address)) {
      fprintf(stderr, "Failed to read IPv4 at line %d\n", line);
      fm85Free(sketch);
      return false;
    }

    // STEP: hash incoming IPs and update sketch
    XXH128_hash_t hash =
        XXH3_128bits_withSeed(address, sizeof(address), HASH_SEED);
    fm85Update(sketch, hash.low64, hash.high64);
  }

  if (ferror(input_file)) {
    perror("Failed to read input file");
    free(sketch);
    return false;
  }

  // STEP: get estimate
  result->coupons = sketch->numCoupons;
  result->estimate = getIconEstimate(sketch->lgK, sketch->numCoupons);

  fm85Free(sketch);
  return true;
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
