#define _POSIX_C_SOURCE 200809L
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define BUFFER_SIZE ((size_t)64u * 1024u * 1024u)
#define EVICTION_BUFFER_SIZE ((size_t)64u * 1024u * 1024u)
#define ITERATIONS 200000u
#define REPEATS 500u
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

static uint32_t next_random(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static void evict_cache_best_effort(unsigned char *evict, size_t size) {
    volatile unsigned char *p = (volatile unsigned char *)evict;
    for (size_t i = 0; i < size; i += CACHE_LINE_BYTES) {
        p[i] = (unsigned char)(p[i] + 1u);
    }
}

int main(int argc, char **argv) {
    const char *buffer_path = (argc > 1) ? argv[1] : "random_buffer.bin";
    const char *results_path = (argc > 2) ? argv[2] : "random_results.csv";

    FILE *input = fopen(buffer_path, "rb");
    if (input == NULL) {
        perror("fopen input buffer (run ./initialize_data first)");
        return EXIT_FAILURE;
    }

    unsigned char *allocated_data = (unsigned char *)malloc(BUFFER_SIZE);
    if (allocated_data == NULL) {
        perror("malloc 64 MiB data buffer");
        fclose(input);
        return EXIT_FAILURE;
    }

    size_t bytes_read = fread(allocated_data, 1, BUFFER_SIZE, input);
    int extra_data = fgetc(input);
    fclose(input);
    if (bytes_read != BUFFER_SIZE || extra_data != EOF) {
        fprintf(stderr, "Expected exactly %zu bytes; read %zu bytes.\n",
                BUFFER_SIZE, bytes_read);
        free(allocated_data);
        return EXIT_FAILURE;
    }

    const volatile unsigned char *data =
        (const volatile unsigned char *)allocated_data;
    unsigned char *evict = (unsigned char *)malloc(EVICTION_BUFFER_SIZE);
    if (evict == NULL) {
        perror("malloc eviction buffer");
        free(allocated_data);
        return EXIT_FAILURE;
    }

    for (size_t i = 0; i < EVICTION_BUFFER_SIZE; ++i) {
        evict[i] = (unsigned char)(i & 0xffu);
    }

    uint32_t *indices = (uint32_t *)malloc((size_t)ITERATIONS * sizeof(uint32_t));
    if (indices == NULL) {
        perror("malloc random indices");
        free(evict);
        free(allocated_data);
        return EXIT_FAILURE;
    }

    const size_t working_sets[] = {
        16u * 1024u, 32u * 1024u, 48u * 1024u, 64u * 1024u,
        96u * 1024u, 128u * 1024u, 256u * 1024u, 512u * 1024u,
        1u * 1024u * 1024u, 2u * 1024u * 1024u,
        4u * 1024u * 1024u, 8u * 1024u * 1024u,
        16u * 1024u * 1024u, 24u * 1024u * 1024u,
        32u * 1024u * 1024u, 64u * 1024u * 1024u
    };
    const size_t working_set_count = sizeof(working_sets) / sizeof(working_sets[0]);

    FILE *out = fopen(results_path, "w");
    if (out == NULL) {
        perror("fopen results CSV");
        free(indices);
        free(evict);
        free(allocated_data);
        return EXIT_FAILURE;
    }

    if (fprintf(out, "working_set_bytes,run,iterations,elapsed_seconds,checksum\n") < 0) {
        perror("write CSV header");
        fclose(out);
        free(indices);
        free(evict);
        free(allocated_data);
        return EXIT_FAILURE;
    }

    for (size_t s = 0; s < working_set_count; ++s) {
        const size_t working_set = working_sets[s];
        const uint32_t line_count = (uint32_t)(working_set / CACHE_LINE_BYTES);
        uint32_t random_state = 0x9E3779B9u;

        for (unsigned int i = 0; i < ITERATIONS; ++i) {
            indices[i] = next_random(&random_state) % line_count;
        }

        for (unsigned int run = 1; run <= REPEATS; ++run) {
            evict_cache_best_effort(evict, EVICTION_BUFFER_SIZE);

            uint64_t sum = 0;
            const double start = now_seconds();
            for (unsigned int i = 0; i < ITERATIONS; ++i) {
                const size_t index = (size_t)indices[i] * CACHE_LINE_BYTES;
                sum += data[index];
            }
            const double elapsed = now_seconds() - start;
            checksum_sink ^= sum;

            if (fprintf(out, "%zu,%u,%u,%.9f,%" PRIu64 "\n",
                        working_set, run, ITERATIONS, elapsed, sum) < 0) {
                perror("write benchmark result");
                fclose(out);
                free(indices);
                free(evict);
                free(allocated_data);
                return EXIT_FAILURE;
            }
        }

        fflush(out);
        printf("Finished working set %zu bytes (%zu/%zu)\n",
               working_set, s + 1, working_set_count);
    }

    if (fclose(out) != 0) {
        perror("close results CSV");
        free(indices);
        free(evict);
        free(allocated_data);
        return EXIT_FAILURE;
    }

    printf("Results written to %s (checksum sink=%" PRIu64 ")\n",
           results_path, checksum_sink);
    free(indices);
    free(evict);
    free(allocated_data);
    return EXIT_SUCCESS;
}