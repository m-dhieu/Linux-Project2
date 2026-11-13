#define _POSIX_C_SOURCE 199309L
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <string.h>

static size_t parse_buf_size(const char *str)
{
    char *endptr;
    errno = 0;
    unsigned long long value = strtoull(str, &endptr, 10);
    if (errno != 0 || endptr == str || *endptr != '\0' ||
        value == 0 || value > SIZE_MAX || value > SSIZE_MAX) {return 0;}
    return (size_t)value;
}

int main(int argc, char *argv[])
{
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <src> <dst> <buffer_size>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *src_path = argv[1];
    const char *dst_path = argv[2];
    if (strcmp(src_path, dst_path) == 0) {
        fprintf(stderr, "Error: Source & destination paths are identical\n");
        return EXIT_FAILURE;
    }

    size_t buf_size = parse_buf_size(argv[3]);
    if (buf_size == 0) {
        fprintf(stderr, "Error: Invalid buffer size\n");
        return EXIT_FAILURE;
    }

    char *buf = malloc(buf_size);
    if (buf == NULL) {
        perror("malloc");
        return EXIT_FAILURE;
    }

    struct timespec start, end;
    if (clock_gettime(CLOCK_MONOTONIC, &start) == -1) {
        perror("clock_gettime");
        free(buf);
        return EXIT_FAILURE;
    }

    int src_fd = open(src_path, O_RDONLY);
    if (src_fd == -1) {
        perror("open source");
        free(buf);
        return EXIT_FAILURE;
    }

    int dst_fd = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst_fd == -1) {
        perror("open destination");
        close(src_fd);
        free(buf);
        return EXIT_FAILURE;
    }

    int status = EXIT_SUCCESS;
    ssize_t bytes_read;
    while (1) {
        bytes_read = read(src_fd, buf, buf_size);
        if (bytes_read == -1) {
	    if (errno == EINTR) continue;
            perror("read");
            status = EXIT_FAILURE;
            break;
        }

        if (bytes_read == 0) break;

        ssize_t total_written = 0;
        while (total_written < bytes_read) {
            ssize_t written = write(
                dst_fd, buf + total_written, 
		(size_t)(bytes_read - total_written)
            );
            if (written == -1) {
		if (errno == EINTR) continue;
                perror("write");
                status = EXIT_FAILURE;
                break;
            }
            if (written == 0) {
                fprintf(stderr, "Error: write returned zero bytes\n");
                status = EXIT_FAILURE;
                break;
            }
            total_written += written;
        }

        if (status != EXIT_SUCCESS) break;
    }

    if (close(src_fd) == -1) {
        perror("close source");
        status = EXIT_FAILURE;
    }

    if (close(dst_fd) == -1) {
        perror("close destination");
        status = EXIT_FAILURE;
    }

    if (clock_gettime(CLOCK_MONOTONIC, &end) == -1) {
        perror("clock_gettime");
        status = EXIT_FAILURE;
    }

    free(buf);

    if (status == EXIT_SUCCESS) {
        double elapsed =
            (double)(end.tv_sec - start.tv_sec) +
            (double)(end.tv_nsec - start.tv_nsec) / 1e9;
        printf("Elapsed time: %.6f seconds\n", elapsed);
    }

    return status;
}

