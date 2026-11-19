#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define IPV4_COUNT (UINT64_C(1) << 32)
#define BUFFER_SIZE (64 * 1024)
#define MAX_LINE_LENGTH (sizeof("255.255.255.255\n") - 1)


/* SplitMix64 keeps seeded datasets reproducible across platforms. */
uint64_t random_u64(uint64_t *state) {
    *state += UINT64_C(0x9e3779b97f4a7c15);
    uint64_t value = *state;
    value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31);
}

uint64_t random_below(uint64_t *state, uint64_t bound) {
    uint64_t value = random_u64(state);

    /* Reject the short initial interval to avoid modulo bias. */
    if (value < bound) {
        uint64_t threshold = -bound % bound;
        while (value < threshold) {
            value = random_u64(state);
        }
    }
    return value % bound;
}

bool parse_u64(const char *text, uint64_t *value) {
    if (!*text || text[strspn(text, "0123456789")] != '\0') {
        return false;
    }

    errno = 0;
    uintmax_t parsed = strtoumax(text, NULL, 10);
    if (errno == ERANGE || parsed > UINT64_MAX) {
        return false;
    }
    *value = (uint64_t)parsed;
    return true;
}


bool generate(FILE *output, uint64_t total, uint64_t distinct, uint64_t seed) {
    char buffer[BUFFER_SIZE];
    char octets[256][4] = {0};
    unsigned char lengths[256];
    size_t used = 0;

    /* Preformat each octet with its trailing delimiter. */
    for (unsigned i = 0; i < 256; ++i) {
        int digits = snprintf(octets[i], sizeof(octets[i]), "%u", i);
        octets[i][digits] = '.';
        lengths[i] = (unsigned char)(digits + 1);
    }

    uint64_t required = distinct;
    for (uint64_t remaining = total; remaining > 0; remaining--) {
        uint64_t id;

        /* Select mandatory slots uniformly as we go. Each gets the next ID,
         * so every ID occurs at least once. Other slots sample the whole pool. */
        if (required && (required == remaining || random_below(&seed, remaining) < required)) {
            id = distinct - required;
            required--;
        } else {
            id = random_below(&seed, distinct);
        }

        if (BUFFER_SIZE - used < MAX_LINE_LENGTH) {
            if (fwrite(buffer, 1, used, output) != used) {
                return false;
            }
            used = 0;
        }

        /* The offset preserves uniqueness modulo 2^32 and starts at 1.0.0.0. */
        uint32_t ip = (uint32_t)(0x01000000U + id);
        /* Four fixed-size copies fit within the reserved 16 bytes.
         * The next octet overwrites any padding from the previous copy. */
        for (int shift = 24; shift >= 0; shift -= 8) {
            unsigned octet = (ip >> shift) & 255U;
            memcpy(buffer + used, octets[octet], sizeof(octets[octet]));
            used += lengths[octet];
        }
        buffer[used - 1] = '\n';
    }

    return fwrite(buffer, 1, used, output) == used;
}


int main(int argc, char **argv) {
    if (argc != 4 && argc != 5) {
        fprintf(stderr, "Usage: %s N D output.txt [seed]\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *path = argv[3];
    uint64_t total, distinct, seed = 42;
    if (!parse_u64(argv[1], &total) || !parse_u64(argv[2], &distinct) ||
        (argc == 5 && !parse_u64(argv[4], &seed))) {
        fputs("N, D and seed must be unsigned decimal integers.\n", stderr);
        return EXIT_FAILURE;
    }
    if (distinct == 0 || distinct > total || distinct > IPV4_COUNT || !*path) {
        fputs("Require 1 <= D <= N, D <= 4294967296 and a nonempty output path.\n", stderr);
        return EXIT_FAILURE;
    }

    struct timespec start, end;
    if (clock_gettime(CLOCK_MONOTONIC, &start) != 0) {
        perror("clock_gettime");
        return EXIT_FAILURE;
    }

    /* Exclusive creation protects existing datasets. */
    FILE *output = fopen(path, "wx");
    if (!output) {
        perror(path);
        return EXIT_FAILURE;
    }

    bool written = generate(output, total, distinct, seed);
    if (fclose(output) != 0 || !written) {
        fprintf(stderr, "Output write failed. %s may contain an incomplete dataset.\n", path);
        return EXIT_FAILURE;
    }

    if (clock_gettime(CLOCK_MONOTONIC, &end) != 0) {
        perror("clock_gettime");
        return EXIT_FAILURE;
    }
    double seconds = (double)(end.tv_sec - start.tv_sec) +
                     (double)(end.tv_nsec - start.tv_nsec) / 1e9;
    printf("Done: %" PRIu64 " lines, exactly %" PRIu64 " distinct IPv4 addresses. Elapsed: %.6f s\n", total, distinct, seconds);

    return 0;
}
