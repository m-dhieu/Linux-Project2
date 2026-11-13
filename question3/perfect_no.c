#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <pthread.h>
#include <time.h>
#include <string.h>
#define MAX_RANGE 100000
#define NUM_THREADS 8
#define MAX_PERFECT 100

int shared_perfect_numbers[MAX_PERFECT];
int global_perfect_count = 0;
int shared_error = 0;
pthread_mutex_t lock;

typedef struct {
    int start;
    int end;
} ThreadRange;

int is_perfect(int n) {if (n <= 1) {return 0;}
    int sum = 1;
    int limit = (int)sqrt((double)n);
    for (int i = 2; i <= limit; i++) {
        if (n % i == 0) {
            sum += i;
            int pair = n / i;
            if (pair != i) {sum += pair;}
        }
    }
    return (sum == n);
}

void* find_perfect_worker(void* arg) {
    ThreadRange* range = (ThreadRange*)arg;
    int local_count = 0;
    int local_found[MAX_PERFECT];
    for (int num = range->start; num <= range->end; num++) {
        if (is_perfect(num)) {
            if (local_count >= MAX_PERFECT) {
                fprintf(stderr, "Local result capacity exceeded\n");
                return NULL;
            }
            local_found[local_count] = num;
            local_count++;
        }
    }

    /* protect shared resources */
    int error = pthread_mutex_lock(&lock);
    if (error != 0) {
        fprintf(stderr, "pthread_mutex_lock: %s\n", strerror(error));
        return NULL;
    }

    if (global_perfect_count + local_count > MAX_PERFECT) {shared_error = 1;
    } else {
        for (int i = 0; i < local_count; i++) {
            shared_perfect_numbers[global_perfect_count + i] = local_found[i];
        }
        global_perfect_count += local_count;
    }

    error = pthread_mutex_unlock(&lock);
    if (error != 0) {
        fprintf(stderr, "pthread_mutex_unlock: %s\n", strerror(error));
    }

    return NULL;
}

int find_perfect_single_threaded(int max_val, int results[], int capacity) {
    int count = 0;
    for (int num = 1; num <= max_val; num++) {
        if (is_perfect(num)) {
            if (count >= capacity) {return -1;}
            results[count] = num;
            count++;
        }
    }
    return count;
}

int compare_ints(const void* a, const void* b) {
    int arg1 = *(const int*)a;
    int arg2 = *(const int*)b;
    return (arg1 > arg2) - (arg1 < arg2);
}

double get_elapsed_seconds(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
}

int main(void) {
    struct timespec start_time, end_time;
    int single_numbers[MAX_PERFECT];
    /* measure single-threaded exec */
    if (clock_gettime(CLOCK_MONOTONIC, &start_time) != 0) {
        perror("clock_gettime");
        return 1;
    }

    int single_count = find_perfect_single_threaded(
        MAX_RANGE, single_numbers, MAX_PERFECT
    );
    if (clock_gettime(CLOCK_MONOTONIC, &end_time) != 0) {
        perror("clock_gettime");
        return 1;
    }

    if (single_count < 0) {
        fprintf(stderr, "Single-threaded result capacity exceeded\n");
        return 1;
    }

    double single_duration = get_elapsed_seconds(start_time, end_time);
    int error = pthread_mutex_init(&lock, NULL);
    if (error != 0) {
        fprintf(stderr, "pthread_mutex_init: %s\n", strerror(error));
        return 1;
    }

    pthread_t threads[NUM_THREADS];
    ThreadRange ranges[NUM_THREADS];
    int segment_size = MAX_RANGE / NUM_THREADS;
    int created = 0;
    /* measure multi-threaded exec */
    if (clock_gettime(CLOCK_MONOTONIC, &start_time) != 0) {
        perror("clock_gettime");
        pthread_mutex_destroy(&lock);
        return 1;
    }

    /* create 8 threads */
    for (int i = 0; i < NUM_THREADS; i++) {
        ranges[i].start = (i * segment_size) + 1;
        ranges[i].end = (i + 1) * segment_size;
        error = pthread_create(
            &threads[i], NULL, find_perfect_worker, &ranges[i]
        );
        if (error != 0) {
            fprintf(stderr, "pthread_create: %s\n", strerror(error));
            for (int j = 0; j < created; j++) {
                pthread_join(threads[j], NULL);
            }
            pthread_mutex_destroy(&lock);
            return 1;
        }
        created++;
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        error = pthread_join(threads[i], NULL);
        if (error != 0) {
            fprintf(stderr, "pthread_join: %s\n", strerror(error));
            return 1;
        }
    }

    if (clock_gettime(CLOCK_MONOTONIC, &end_time) != 0) {
        perror("clock_gettime");
        pthread_mutex_destroy(&lock);
        return 1;
    }

    double multi_duration = get_elapsed_seconds(start_time, end_time);
    qsort(single_numbers, single_count, sizeof(int), compare_ints);
    qsort(shared_perfect_numbers, global_perfect_count,
          sizeof(int), compare_ints);
    if (shared_error || single_count != global_perfect_count) {
        fprintf(stderr, "Error: Result count mismatch\n");
        pthread_mutex_destroy(&lock);
        return 1;
    }

    for (int i = 0; i < single_count; i++) {
        if (single_numbers[i] != shared_perfect_numbers[i]) {
            fprintf(stderr, "Error: Result mismatch\n");
            pthread_mutex_destroy(&lock);
            return 1;
        }
    }

    error = pthread_mutex_destroy(&lock);
    if (error != 0) {
        fprintf(stderr, "pthread_mutex_destroy: %s\n", strerror(error));
        return 1;
    }

    printf("Perfect numbers found:");
    for (int i = 0; i < global_perfect_count; i++) {
        printf(" %d", shared_perfect_numbers[i]);
    }
    printf("\n");
    printf("The synchronized total number of perfect numbers "
       "between 1 and %d is %d\n", MAX_RANGE, global_perfect_count);
    printf("Single-threaded time: %.6f seconds\n", single_duration);
    printf("Multi-threaded time:  %.6f seconds\n", multi_duration);

    return 0;
}

