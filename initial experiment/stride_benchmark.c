/*
 * stride_benchmark.c
 *
 * Measures 200,000 one-byte reads for each stride from 1 through 256 bytes,
 * repeating each stride 5 times. Results are written as CSV.
 *
 * Build:
 *   gcc -O2 -std=c11 -Wall -Wextra stride_benchmark.c -o stride_benchmark
 * Run:
 *   ./stride_benchmark [buffer_file] [results_csv]
 *
 * Cache handling:
 * - The input file is read into a malloc-allocated 64 MiB memory buffer before
 *   timing, so the measured scan reads from the in-memory buffer, not via file I/O.
 * - Before each timed run, the program touches a separate 64 MiB eviction buffer
 *   one byte per assumed 64-byte cache line to put pressure on CPU caches.
 * This is best-effort only; normal portable C cannot guarantee that every CPU
 * cache is completely empty.
 */

#define _POSIX_C_SOURCE 200809L
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define BUFFER_SIZE ((size_t)196 * 1024 * 1024)
#define ITERATIONS 200000u
#define REPEATS 10
#define MAX_STRIDE 1024u

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
    for (size_t i = 0; i < size; i += 64) {
        p[i] = (unsigned char)(p[i] + 1u);
    }
}

int main(int argc, char **argv) {
    const char *buffer_path = (argc > 1) ? argv[1] : "scan_buffer.bin";
    const char *results_path = (argc > 2) ? argv[2] : "stride_results.csv";

    FILE *input = fopen(buffer_path, "rb");
    if (input == NULL) {
        perror("open buffer file; run ./initialize_data first");
        return EXIT_FAILURE;
    }

    unsigned char *allocated_data = (unsigned char *)malloc(BUFFER_SIZE);
    if (allocated_data == NULL) {
        perror("malloc data buffer");
        fclose(input);
        return EXIT_FAILURE;
    }

    size_t bytes_read = fread(allocated_data, 1, BUFFER_SIZE, input);
    fclose(input);
    if (bytes_read != BUFFER_SIZE) {
        fprintf(stderr, "Error: expected %zu bytes but read %zu\n",
                BUFFER_SIZE, bytes_read);
        free(allocated_data);
        return EXIT_FAILURE;
    }

    /* Volatile-qualified view ensures each source-level access is an actual load. */
    const volatile unsigned char *data =
        (const volatile unsigned char *)allocated_data;

    unsigned char *evict = (unsigned char *)malloc(BUFFER_SIZE);
    if (evict == NULL) {
        perror("malloc eviction buffer");
        free(allocated_data);
        return EXIT_FAILURE;
    }

    /* Touch the full eviction allocation before measurements. */
    for (size_t i = 0; i < BUFFER_SIZE; ++i) {
        evict[i] = (unsigned char)(i & 0xffu);
    }

    FILE *out = fopen(results_path, "w");
    if (out == NULL) {
        perror("fopen results");
        free(evict);
        free(allocated_data);
        return EXIT_FAILURE;
    }

    fprintf(out, "stride_bytes,run,iterations,elapsed_seconds,checksum\n");

    for (unsigned int stride = 1; stride <= MAX_STRIDE; stride *= 2) {
        for (unsigned int run = 1; run <= REPEATS; ++run) {
            evict_cache_best_effort(evict, BUFFER_SIZE);

            uint64_t sum = 0;
            size_t index = 0;

            double start = now_seconds();
            for (unsigned int i = 0; i < ITERATIONS; ++i) {
                sum += data[index];
                index += stride;
                // As Index wraps around, it will eventually exceed BUFFER_SIZE, but we don't need to reset it because the buffer should be large enough to accommodate the maximum stride and number of iterations. The buffer size is 196 MiB, which is sufficient for the maximum stride of 1024 bytes and 200,000 iterations.
                // if (index >= BUFFER_SIZE) {
                //     index %= BUFFER_SIZE;
                // }
            }
            double elapsed = now_seconds() - start;

            checksum_sink ^= sum;
            fprintf(out, "%u,%u,%u,%.9f,%" PRIu64 "\n",
                    stride, run, ITERATIONS, elapsed, sum);
            fflush(out);
        }
        printf("Finished stride %u bytes\n", stride);
    }

    printf("Results written to %s (checksum sink=%" PRIu64 ")\n",
           results_path, checksum_sink);

    fclose(out);
    free(evict);
    free(allocated_data);
    return EXIT_SUCCESS;
}
