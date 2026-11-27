/*
 * stride_benchmark.c
 *
 * Experiment B: measure 200,000 one-byte reads at each configured stride
 * from a 1 GiB in-memory data buffer. Each stride is measured 30 times.
 * Results are written to CSV.
 *
 * Build:
 *   gcc -O2 -std=c11 -Wall -Wextra stride_benchmark.c -o stride_benchmark
 * Run:
 *   ./stride_benchmark [buffer_file] [results_csv]
 *
 * The input file is loaded into malloc-allocated memory before timing, so
 * file I/O is excluded from the timed region. Before each run, a separate
 * 64 MiB buffer is touched to put pressure on the CPU caches. This is
 * best-effort cache eviction, not a guarantee that all caches are empty.
 */

#define _POSIX_C_SOURCE 200809L
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define BUFFER_SIZE ((size_t)1024 * 1024 * 1024)
#define EVICTION_BUFFER_SIZE ((size_t)64 * 1024 * 1024)
#define ITERATIONS 200000u
#define REPEATS 200u
#define CACHE_LINE_BYTES 64u

static volatile uint64_t checksum_sink = 0;

static double now_seconds(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void evict_cache_best_effort(unsigned char *evict, size_t size) {
    volatile unsigned char *p = (volatile unsigned char *)evict;
    for (size_t i = 0; i < size; i += CACHE_LINE_BYTES) {
        p[i] = (unsigned char)(p[i] + 1u);
    }
}

int main(int argc, char **argv) {
    const char *buffer_path = (argc > 1) ? argv[1] : "scan_buffer.bin";
    const char *results_path = (argc > 2) ? argv[2] : "stride_results.csv";

    FILE *input = fopen(buffer_path, "rb");
    if (input == NULL) {
        perror("fopen input buffer (run ./initialize_data first)");
        return EXIT_FAILURE;
    }

    unsigned char *allocated_data = (unsigned char *)malloc(BUFFER_SIZE);
    if (allocated_data == NULL) {
        perror("malloc 1 GiB data buffer");
        fclose(input);
        return EXIT_FAILURE;
    }

    size_t bytes_read = fread(allocated_data, 1, BUFFER_SIZE, input);
    int extra_data = fgetc(input);
    fclose(input);
    if (bytes_read != BUFFER_SIZE || extra_data != EOF) {
        fprintf(stderr,
                "Error: expected an input file of exactly %zu bytes; read %zu bytes.\n",
                BUFFER_SIZE, bytes_read);
        free(allocated_data);
        return EXIT_FAILURE;
    }

    /* Volatile-qualified view prevents the compiler removing measured loads. */
    const volatile unsigned char *data =
        (const volatile unsigned char *)allocated_data;

    unsigned char *evict = (unsigned char *)malloc(EVICTION_BUFFER_SIZE);
    if (evict == NULL) {
        perror("malloc 64 MiB eviction buffer");
        free(allocated_data);
        return EXIT_FAILURE;
    }

    /* First-touch the eviction allocation before measurements begin. */
    for (size_t i = 0; i < EVICTION_BUFFER_SIZE; ++i) {
        evict[i] = (unsigned char)(i & 0xffu);
    }

    FILE *out = fopen(results_path, "w");
    if (out == NULL) {
        perror("fopen results CSV");
        free(evict);
        free(allocated_data);
        return EXIT_FAILURE;
    }

    if (fprintf(out, "stride_bytes,run,iterations,elapsed_seconds,checksum\n") < 0) {
        perror("write CSV header");
        fclose(out);
        free(evict);
        free(allocated_data);
        return EXIT_FAILURE;
    }

    const unsigned int strides[] = {
        1, 2, 4, 8, 16, 24, 32, 40,
        48, 56, 60, 64, 68, 72, 80,
        96, 112, 120, 128, 160, 192, 256,
        512, 1024, 2048, 4096
    };
    const size_t stride_count = sizeof(strides) / sizeof(strides[0]);

    for (size_t s = 0; s < stride_count; ++s) {
        const unsigned int stride = strides[s];

        /* Prevent an out-of-bounds access if experiment parameters change. */
        const size_t last_index = (size_t)(ITERATIONS - 1u) * stride;
        if (last_index >= BUFFER_SIZE) {
            fprintf(stderr, "Skipping stride %u: final index exceeds buffer.\n", stride);
            continue;
        }

        for (unsigned int run = 1; run <= REPEATS; ++run) {
            evict_cache_best_effort(evict, EVICTION_BUFFER_SIZE);

            uint64_t sum = 0;
            size_t index = 0;
            const double start = now_seconds();

            for (unsigned int i = 0; i < ITERATIONS; ++i) {
                sum += data[index];
                index += stride;
            }

            const double elapsed = now_seconds() - start;
            checksum_sink ^= sum;

            if (fprintf(out, "%u,%u,%u,%.9f,%" PRIu64 "\n",
                        stride, run, ITERATIONS, elapsed, sum) < 0) {
                perror("write benchmark result");
                fclose(out);
                free(evict);
                free(allocated_data);
                return EXIT_FAILURE;
            }
        }

        fflush(out);
        printf("Finished stride %u bytes (%u/%zu)\n",
               stride, (unsigned int)(s + 1), stride_count);
    }

    if (fclose(out) != 0) {
        perror("close results CSV");
        free(evict);
        free(allocated_data);
        return EXIT_FAILURE;
    }

    printf("Results written to %s (checksum sink=%" PRIu64 ")\n",
           results_path, checksum_sink);

    free(evict);
    free(allocated_data);
    return EXIT_SUCCESS;
}
