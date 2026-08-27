#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { QUEUE_CAPACITY = 3, ITEM_COUNT = 10 };

struct bounded_queue {
    int values[QUEUE_CAPACITY];
    size_t head;
    size_t tail;
    size_t count;
    int closed;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
};

static int queue_init(struct bounded_queue *queue)
{
    int error;

    *queue = (struct bounded_queue){0};
    error = pthread_mutex_init(&queue->mutex, NULL);
    if (error != 0) {
        return error;
    }
    error = pthread_cond_init(&queue->not_empty, NULL);
    if (error != 0) {
        pthread_mutex_destroy(&queue->mutex);
        return error;
    }
    error = pthread_cond_init(&queue->not_full, NULL);
    if (error != 0) {
        pthread_cond_destroy(&queue->not_empty);
        pthread_mutex_destroy(&queue->mutex);
        return error;
    }
    return 0;
}

static void queue_destroy(struct bounded_queue *queue)
{
    pthread_cond_destroy(&queue->not_full);
    pthread_cond_destroy(&queue->not_empty);
    pthread_mutex_destroy(&queue->mutex);
}

static void queue_close(struct bounded_queue *queue)
{
    pthread_mutex_lock(&queue->mutex);
    queue->closed = 1;
    pthread_cond_broadcast(&queue->not_empty);
    pthread_cond_broadcast(&queue->not_full);
    pthread_mutex_unlock(&queue->mutex);
}

static int queue_push(struct bounded_queue *queue, int value)
{
    int error = pthread_mutex_lock(&queue->mutex);

    if (error != 0) {
        return error;
    }
    while (queue->count == QUEUE_CAPACITY && !queue->closed) {
        error = pthread_cond_wait(&queue->not_full, &queue->mutex);
        if (error != 0) {
            pthread_mutex_unlock(&queue->mutex);
            return error;
        }
    }
    if (queue->closed) {
        pthread_mutex_unlock(&queue->mutex);
        return ECANCELED;
    }
    queue->values[queue->tail] = value;
    queue->tail = (queue->tail + 1) % QUEUE_CAPACITY;
    queue->count++;
    pthread_cond_signal(&queue->not_empty);
    return pthread_mutex_unlock(&queue->mutex);
}

static int queue_pop(struct bounded_queue *queue, int *out_value)
{
    int error = pthread_mutex_lock(&queue->mutex);

    if (error != 0) {
        return error;
    }
    while (queue->count == 0 && !queue->closed) {
        error = pthread_cond_wait(&queue->not_empty, &queue->mutex);
        if (error != 0) {
            pthread_mutex_unlock(&queue->mutex);
            return error;
        }
    }
    if (queue->closed) {
        pthread_mutex_unlock(&queue->mutex);
        return ECANCELED;
    }
    *out_value = queue->values[queue->head];
    queue->head = (queue->head + 1) % QUEUE_CAPACITY;
    queue->count--;
    pthread_cond_signal(&queue->not_full);
    return pthread_mutex_unlock(&queue->mutex);
}

static void *produce(void *opaque_queue)
{
    struct bounded_queue *queue = opaque_queue;

    for (int value = 1; value <= ITEM_COUNT; ++value) {
        int error = queue_push(queue, value);

        if (error != 0) {
            fprintf(stderr, "queue_push: %s\n", strerror(error));
            return queue;
        }
        printf("produced %d\n", value);
    }
    return NULL;
}

static void *consume(void *opaque_queue)
{
    struct bounded_queue *queue = opaque_queue;

    for (int index = 0; index < ITEM_COUNT; ++index) {
        int value;
        int error = queue_pop(queue, &value);

        if (error != 0) {
            fprintf(stderr, "queue_pop: %s\n", strerror(error));
            return queue;
        }
        printf("consumed %d\n", value);
    }
    return NULL;
}

int main(void)
{
    struct bounded_queue queue;
    pthread_t producer;
    pthread_t consumer;
    void *producer_result;
    void *consumer_result;
    int error;

    error = queue_init(&queue);
    if (error != 0) {
        fprintf(stderr, "queue_init: %s\n", strerror(error));
        return EXIT_FAILURE;
    }
    error = pthread_create(&producer, NULL, produce, &queue);
    if (error != 0) {
        fprintf(stderr, "pthread_create: %s\n", strerror(error));
        queue_destroy(&queue);
        return EXIT_FAILURE;
    }
    error = pthread_create(&consumer, NULL, consume, &queue);
    if (error != 0) {
        fprintf(stderr, "pthread_create: %s\n", strerror(error));
        queue_close(&queue);
        pthread_join(producer, &producer_result);
        queue_destroy(&queue);
        return EXIT_FAILURE;
    }

    pthread_join(producer, &producer_result);
    pthread_join(consumer, &consumer_result);
    queue_destroy(&queue);
    return producer_result == NULL && consumer_result == NULL ? EXIT_SUCCESS
                                                               : EXIT_FAILURE;
}
