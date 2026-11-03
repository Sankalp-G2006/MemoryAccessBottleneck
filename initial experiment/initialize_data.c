/*
 * initialize_data.c
 *
 * Allocates and initializes a 64 MiB buffer, then writes it to a binary file.
 * The experiment program memory-maps this file so the data stays resident in
 * the process's address space while measurements run.
 *
 * Build:
 *   gcc -O2 -std=c11 -Wall -Wextra initialize_data.c -o initialize_data
 * Run:
 *   ./initialize_data [buffer_file]
 */

#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUFFER_SIZE ((size_t)196 * 1024 * 1024)

int main(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : "scan_buffer.bin";
    unsigned char *buffer = (unsigned char *)malloc(BUFFER_SIZE);
    if (buffer == NULL) {
        perror("malloc");
        return EXIT_FAILURE;
    }

    /* Deterministic, non-constant contents; initialization touches every byte. */
    uint32_t state = 0x12345678u;
    for (size_t i = 0; i < BUFFER_SIZE; ++i) {
        state = state * 1664525u + 1013904223u;
        buffer[i] = (unsigned char)(state >> 24);
    }

    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        perror("fopen");
        free(buffer);
        return EXIT_FAILURE;
    }

    size_t written = fwrite(buffer, 1, BUFFER_SIZE, file);
    if (written != BUFFER_SIZE) {
        fprintf(stderr, "Error: wrote %zu of %zu bytes: %s\n",
                written, BUFFER_SIZE, strerror(errno));
        fclose(file);
        free(buffer);
        return EXIT_FAILURE;
    }

    if (fclose(file) != 0) {
        perror("fclose");
        free(buffer);
        return EXIT_FAILURE;
    }

    printf("Initialized %zu bytes (196 MiB) and wrote them to %s\n",
           BUFFER_SIZE, path);
    free(buffer);
    return EXIT_SUCCESS;
}
