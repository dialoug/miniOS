#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { THREAD_COUNT = 2, ITERATIONS = 1000000 };

struct shared_counter {
    uint64_t value;
    pthread_mutex_t mutex;
    int use_mutex;
};

static void *increment_counter(void *opaque_counter)
{
    struct shared_counter *counter = opaque_counter;

    for (unsigned int index = 0; index < ITERATIONS; ++index) {
        if (counter->use_mutex) {
            pthread_mutex_lock(&counter->mutex);
            counter->value++;
            pthread_mutex_unlock(&counter->mutex);
        } else {
            counter->value++; /* Intentional data race in unsafe mode. */
        }
    }
    return NULL;
}

int main(int argc, char *argv[])
{
    struct shared_counter counter = {0, PTHREAD_MUTEX_INITIALIZER, 0};
    pthread_t workers[THREAD_COUNT];
    const uint64_t expected = (uint64_t)THREAD_COUNT * ITERATIONS;
    size_t created = 0;
    int error;

    if (argc != 2 ||
        (strcmp(argv[1], "safe") != 0 && strcmp(argv[1], "unsafe") != 0)) {
        fprintf(stderr, "usage: %s {safe|unsafe}\n", argv[0]);
        return EXIT_FAILURE;
    }
    counter.use_mutex = strcmp(argv[1], "safe") == 0;

    for (size_t index = 0; index < THREAD_COUNT; ++index) {
        error = pthread_create(&workers[index], NULL, increment_counter, &counter);
        if (error != 0) {
            fprintf(stderr, "pthread_create: %s\n", strerror(error));
            for (size_t joined = 0; joined < created; ++joined) {
                pthread_join(workers[joined], NULL);
            }
            pthread_mutex_destroy(&counter.mutex);
            return EXIT_FAILURE;
        }
        created++;
    }
    for (size_t index = 0; index < THREAD_COUNT; ++index) {
        error = pthread_join(workers[index], NULL);
        if (error != 0) {
            fprintf(stderr, "pthread_join: %s\n", strerror(error));
            return EXIT_FAILURE;
        }
    }

    printf("mode=%s, expected=%" PRIu64 ", actual=%" PRIu64 "\n",
           argv[1], expected, counter.value);
    pthread_mutex_destroy(&counter.mutex);
    return counter.use_mutex && counter.value != expected ? EXIT_FAILURE : EXIT_SUCCESS;
}
