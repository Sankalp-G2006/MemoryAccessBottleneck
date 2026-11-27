#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define BUFFER_SIZE ((size_t)64u * 1024u * 1024u)
#define CHUNK_SIZE ((size_t)1024u * 1024u)

int main(int argc, char **argv) {
    const char *output_path = (argc > 1) ? argv[1] : "random_buffer.bin";
    FILE *output = fopen(output_path, "wb");
    if (output == NULL) {
        perror("fopen output file");
        return EXIT_FAILURE;
    }

    unsigned char *chunk = (unsigned char *)malloc(CHUNK_SIZE);
    if (chunk == NULL) {
        perror("malloc chunk");
        fclose(output);
        return EXIT_FAILURE;
    }

    uint32_t state = 0xA341316Cu;
    size_t written = 0;
    while (written < BUFFER_SIZE) {
        size_t amount = BUFFER_SIZE - written;
        if (amount > CHUNK_SIZE) amount = CHUNK_SIZE;

        for (size_t i = 0; i < amount; ++i) {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            chunk[i] = (unsigned char)(state & 0xffu);
        }

        if (fwrite(chunk, 1, amount, output) != amount) {
            perror("fwrite");
            free(chunk);
            fclose(output);
            return EXIT_FAILURE;
        }
        written += amount;
    }

    free(chunk);
    if (fclose(output) != 0) {
        perror("fclose");
        return EXIT_FAILURE;
    }

    printf("Created %s (%zu bytes).\n", output_path, BUFFER_SIZE);
    return EXIT_SUCCESS;
}