#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * 练习目标：实现一个只存储 int 的动态数组。
 *
 * 所有权约定：
 * - vector_init() 之后，vector 不拥有任何堆内存。
 * - vector_push_back() 成功后，vector 拥有 values 指向的堆内存。
 * - vector_destroy() 负责释放该内存；调用后对象回到初始状态。
 */
struct int_vector {
    int *values;
    size_t size;
    size_t capacity;
};

static void vector_init(struct int_vector *vector)
{
    assert(vector != NULL);
    vector->values = NULL;
    vector->size = 0;
    vector->capacity = 0;
}

static void vector_destroy(struct int_vector *vector)
{
    assert(vector != NULL);
    free(vector->values);
    vector_init(vector);
}

static int vector_reserve(struct int_vector *vector, size_t new_capacity)
{
    int *new_values;

    assert(vector != NULL);
    if (new_capacity <= vector->capacity) {
        return 0;
    }
    if (new_capacity > SIZE_MAX / sizeof(*new_values)) {
        return EOVERFLOW;
    }

    new_values = realloc(vector->values, new_capacity * sizeof(*new_values));
    if (new_values == NULL) {
        return ENOMEM;
    }

    vector->values = new_values;
    vector->capacity = new_capacity;
    return 0;
}


static int vector_push_back(struct int_vector *vector, int value)
{
    int error;
    size_t new_capacity;

    assert(vector != NULL);
    if (vector->size == vector->capacity) {
        if (vector->capacity == 0) {
            new_capacity = 4;
        } else {
            if (vector->capacity > SIZE_MAX / 2) {
                return EOVERFLOW;
            }
            new_capacity = vector->capacity * 2;
        }

        error = vector_reserve(vector, new_capacity);
        if (error != 0) {
            return error;
        }
    }

    vector->values[vector->size++] = value;
    return 0;
}

static int vector_get(const struct int_vector *vector, size_t index,
                      int *out_value)
{
    if (vector == NULL || out_value == NULL) {
        return EINVAL;
    }
    if (index >= vector->size) {
        return ERANGE;
    }

    *out_value = vector->values[index];
    return 0;
}

static void print_vector(const struct int_vector *vector)
{
    printf("size=%zu, capacity=%zu: [", vector->size, vector->capacity);
    for (size_t index = 0; index < vector->size; ++index) {
        printf("%s%d", index == 0 ? "" : ", ", vector->values[index]);
    }
    puts("]");
}

int main(void)
{
    struct int_vector vector;
    int value;
    int error;

    vector_init(&vector);

    error = vector_reserve(&vector, SIZE_MAX);
    if (error != EOVERFLOW || vector.values != NULL || vector.size != 0 ||
        vector.capacity != 0) {
        fprintf(stderr, "overflow check failed: error=%d\n", error);
        vector_destroy(&vector);
        return EXIT_FAILURE;
    }
    puts("oversized reserve returned EOVERFLOW; vector stayed unchanged");

    for (int number = 10; number <= 60; number += 10) {
        error = vector_push_back(&vector, number);
        if (error != 0) {
            fprintf(stderr, "vector_push_back failed: %d\n", error);
            vector_destroy(&vector);
            return EXIT_FAILURE;
        }
        print_vector(&vector);
    }

    error = vector_get(&vector, 3, &value);
    if (error != 0) {
        fprintf(stderr, "vector_get failed: %d\n", error);
        vector_destroy(&vector);
        return EXIT_FAILURE;
    }
    printf("vector[3] = %d\n", value);

    value = -1;
    error = vector_get(&vector, vector.size, &value);
    if (error != ERANGE || value != -1) {
        fprintf(stderr,
                "out-of-range check failed: error=%d, value=%d\n",
                error, value);
        vector_destroy(&vector);
        return EXIT_FAILURE;
    }
    printf("vector[size] returned ERANGE; output value stayed %d\n", value);

    vector_destroy(&vector);
    return EXIT_SUCCESS;
}
