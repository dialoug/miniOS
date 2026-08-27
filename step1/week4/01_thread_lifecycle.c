#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct sum_request {
    unsigned int first;
    unsigned int last;
};

struct sum_result {
    uint64_t value;
};

static void *sum_range(void *opaque_request)
{
    const struct sum_request *request = opaque_request;
    struct sum_result *result = malloc(sizeof(*result));

    if (result == NULL) {
        return NULL;
    }

    result->value = 0;
    for (unsigned int value = request->first; value <= request->last; ++value) {
        result->value += value;
    }
    return result;
}

int main(void)
{
    const struct sum_request request = {1, 100};
    pthread_t worker;
    struct sum_result *result;
    void *opaque_result;
    int error;

    error = pthread_create(&worker, NULL, sum_range, (void *)&request);
    if (error != 0) {
        fprintf(stderr, "pthread_create: %s\n", strerror(error));
        return EXIT_FAILURE;
    }

    error = pthread_join(worker, &opaque_result);
    if (error != 0) {
        fprintf(stderr, "pthread_join: %s\n", strerror(error));
        return EXIT_FAILURE;
    }
    if (opaque_result == NULL) {
        fputs("worker could not allocate its result\n", stderr);
        return EXIT_FAILURE;
    }

    result = opaque_result;
    printf("sum(1..100) = %" PRIu64 "\n", result->value);
    free(result);
    return EXIT_SUCCESS;
}
