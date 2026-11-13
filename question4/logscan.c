#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <stdint.h>
#define BUF_SIZE 16384

typedef struct {
    char **files;
    int total_files;
    int next_index;
    const char *pattern;
    FILE *report_fp;
    long long grand_total_matches;
    int files_processed;
    int report_error;
    pthread_mutex_t queue_mutex;
    pthread_mutex_t report_mutex;
    pthread_mutex_t total_mutex;
} SharedContext;

typedef struct {
    int thread_id;
    SharedContext *ctx;
} WorkerArg;

static double get_elapsed_seconds(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
}

static int append_to_line(char **line, size_t *line_len, size_t *capacity,
                          const char *chunk, size_t chunk_len) {
    if (chunk_len > SIZE_MAX - *line_len - 1) {return -1;}

    size_t needed = *line_len + chunk_len + 1;
    if (needed > *capacity) {
        size_t new_capacity = *capacity ? *capacity : 256;
        while (new_capacity < needed) {
            if (new_capacity > SIZE_MAX / 2) {
                new_capacity = needed;
                break;
            }
            new_capacity *= 2;
        }
        char *temp = realloc(*line, new_capacity);
        if (temp == NULL) {return -1;}
        *line = temp;
        *capacity = new_capacity;
    }

    memcpy(*line + *line_len, chunk, chunk_len);
    *line_len += chunk_len;
    (*line)[*line_len] = '\0';

    return 0;
}

static int process_file(const char *filename, const char *pattern,
                        long long *matches, long long *total_lines) {
    FILE *fp = fopen(filename, "r");
    if (fp == NULL) {
        perror(filename);
        return -1;
    }

    char chunk[BUF_SIZE];
    char *line = NULL;
    size_t line_len = 0;
    size_t capacity = 0;
    *matches = 0;
    *total_lines = 0;

    while (fgets(chunk, sizeof(chunk), fp) != NULL) {
        size_t len = strlen(chunk);
        if (append_to_line(&line, &line_len, &capacity, chunk, len) != 0) {
            fprintf(stderr, "Unable to allocate line buffer for %s\n",
                    filename);
            free(line);
            fclose(fp);
            return -1;
        }
        if (line_len > 0 && line[line_len - 1] == '\n') {
            (*total_lines)++;
            if (strstr(line, pattern) != NULL) {(*matches)++;}
            line_len = 0;
            line[0] = '\0';
        }
    }

    if (ferror(fp)) {
        perror(filename);
        free(line);
        fclose(fp);
        return -1;
    }

    if (line_len > 0) {
        (*total_lines)++;
        if (strstr(line, pattern) != NULL) {(*matches)++;}
    }

    free(line);
    if (fclose(fp) != 0) {
        perror(filename);
        return -1;
    }

    return 0;
}

static void *worker_thread(void *arg) {
    WorkerArg *worker = (WorkerArg *)arg;
    SharedContext *ctx = worker->ctx;
    int tid = worker->thread_id;

    while (1) {
        int index;
        /* retrieve next file from shared queue */
        pthread_mutex_lock(&ctx->queue_mutex);
        if (ctx->next_index >= ctx->total_files) {
            pthread_mutex_unlock(&ctx->queue_mutex);
            break;
        }

        index = ctx->next_index++;
        pthread_mutex_unlock(&ctx->queue_mutex);
        const char *filename = ctx->files[index];
        long long matches = 0;
        long long total_lines = 0;
        /* process file without holding any mutex */
        if (process_file(filename, ctx->pattern,
                         &matches, &total_lines) != 0) {continue;}
     
	/* protect writes to shared file */
        pthread_mutex_lock(&ctx->report_mutex);
        if (fprintf(ctx->report_fp,
                    "%s: %lld matching lines out of %lld (thread %d)\n",
                    filename, matches, total_lines, tid) < 0) {
            ctx->report_error = 1;
        }

        pthread_mutex_unlock(&ctx->report_mutex);
        pthread_mutex_lock(&ctx->total_mutex);
        ctx->grand_total_matches += matches;
        ctx->files_processed++;
        pthread_mutex_unlock(&ctx->total_mutex);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 5) {
        fprintf(stderr, "Usage: %s <pattern> <report.txt> "
                "<number_of_threads> <file1.log> [file2.log ...]\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *pattern = argv[1];
    const char *report_path = argv[2];
    char *endptr = NULL;
    errno = 0;
    long parsed_threads = strtol(argv[3], &endptr, 10);
    if (errno != 0 || endptr == argv[3] ||
        *endptr != '\0' || parsed_threads <= 0 ||
        parsed_threads > 1024) {
        fprintf(stderr, "Error: Invalid thread count '%s'\n", argv[3]);
        return EXIT_FAILURE;
    }

    int num_threads = (int)parsed_threads;
    int num_files = argc - 4;
    char **files = &argv[4];
    FILE *report_fp = fopen(report_path, "w");
    if (report_fp == NULL) {
        perror(report_path);
        return EXIT_FAILURE;
    }

    SharedContext ctx = {
        .files = files, .total_files = num_files, .next_index = 0,
        .pattern = pattern, .report_fp = report_fp, .grand_total_matches = 0,
        .files_processed = 0, .report_error = 0
    };

    /* init mutexes separate for safe cleanup */
    int rc = pthread_mutex_init(&ctx.queue_mutex, NULL);
    if (rc != 0) {
        fprintf(stderr, "Queue mutex: %s\n", strerror(rc));
        fclose(report_fp);
        return EXIT_FAILURE;
    }

    rc = pthread_mutex_init(&ctx.report_mutex, NULL);
    if (rc != 0) {
        fprintf(stderr, "Report mutex: %s\n", strerror(rc));
        pthread_mutex_destroy(&ctx.queue_mutex);
        fclose(report_fp);
        return EXIT_FAILURE;
    }

    rc = pthread_mutex_init(&ctx.total_mutex, NULL);
    if (rc != 0) {
        fprintf(stderr, "Total mutex: %s\n", strerror(rc));
        pthread_mutex_destroy(&ctx.report_mutex);
        pthread_mutex_destroy(&ctx.queue_mutex);
        fclose(report_fp);
        return EXIT_FAILURE;
    }

    pthread_t *threads = malloc(sizeof(pthread_t) * (size_t)num_threads);
    WorkerArg *args = malloc(sizeof(WorkerArg) * (size_t)num_threads);
    if (threads == NULL || args == NULL) {
        perror("malloc");
        free(threads);
        free(args);
        pthread_mutex_destroy(&ctx.total_mutex);
        pthread_mutex_destroy(&ctx.report_mutex);
        pthread_mutex_destroy(&ctx.queue_mutex);
        fclose(report_fp);
        return EXIT_FAILURE;
    }

    struct timespec start_time, end_time;
    if (clock_gettime(CLOCK_MONOTONIC, &start_time) != 0) {
        perror("clock_gettime");
        free(threads);
        free(args);
        pthread_mutex_destroy(&ctx.total_mutex);
        pthread_mutex_destroy(&ctx.report_mutex);
        pthread_mutex_destroy(&ctx.queue_mutex);
        fclose(report_fp);
        return EXIT_FAILURE;
    }

    int created = 0;
    for (int i = 0; i < num_threads; i++) {
        args[i].thread_id = i + 1;
        args[i].ctx = &ctx;
        rc = pthread_create(&threads[i], NULL, worker_thread, &args[i]);
        if (rc != 0) {
            fprintf(stderr, "pthread_create: %s\n", strerror(rc));
            break;
        }
        created++;
    }

    int join_error = 0;
    for (int i = 0; i < created; i++) {rc = pthread_join(threads[i], NULL);
        if (rc != 0) {
            fprintf(stderr, "pthread_join: %s\n", strerror(rc));
            join_error = 1;
        }
    }

    if (join_error) {return EXIT_FAILURE;}
    /* flush worker results before stopping timer */
    if (fflush(report_fp) != 0) {
        perror("fflush report");
        ctx.report_error = 1;
    }

    if (clock_gettime(CLOCK_MONOTONIC, &end_time) != 0) {
        perror("clock_gettime");
        free(threads);
        free(args);
        pthread_mutex_destroy(&ctx.total_mutex);
        pthread_mutex_destroy(&ctx.report_mutex);
        pthread_mutex_destroy(&ctx.queue_mutex);
        fclose(report_fp);
        return EXIT_FAILURE;
    }

    double elapsed = get_elapsed_seconds(start_time, end_time);
    if (fprintf(report_fp,
                "Total matching lines across %d files: %lld\n",
                ctx.files_processed, ctx.grand_total_matches) < 0) {
        ctx.report_error = 1;
    }

    printf("Total matching lines across %d files: %lld\n",
           ctx.files_processed, ctx.grand_total_matches);
    printf("Elapsed time (%d threads): %.6f seconds\n", num_threads, elapsed);
    if (fclose(report_fp) != 0) {
        perror("fclose report");
        ctx.report_error = 1;
    }

    pthread_mutex_destroy(&ctx.total_mutex);
    pthread_mutex_destroy(&ctx.report_mutex);
    pthread_mutex_destroy(&ctx.queue_mutex);
    free(threads);
    free(args);
    if (created != num_threads || ctx.report_error) {return EXIT_FAILURE;}

    return EXIT_SUCCESS;
}

