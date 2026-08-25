#include <assert.h>
#include <errno.h>
#include <stddef.h>
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

/*
 * TODO 1：实现扩容。
 *
 * 要求：
 * - 如果 new_capacity <= capacity，直接返回 0。
 * - 使用 realloc()；失败时返回 ENOMEM。
 * - realloc 失败时，原来的 vector 必须保持完全可用。
 * - 成功后更新 values 和 capacity，返回 0。
 */
static int vector_reserve(struct int_vector *vector, size_t new_capacity)
{
    (void)vector;
    (void)new_capacity;
    return ENOSYS;
}

/*
 * TODO 2：在末尾追加 value。
 *
 * 要求：
 * - 空间不足时，以 2 倍 capacity 扩容；初始 capacity 设为 4。
 * - 只有 reserve 成功后才可修改 size。
 * - 成功返回 0；失败时返回 vector_reserve 的错误码。
 */
static int vector_push_back(struct int_vector *vector, int value)
{
    (void)vector;
    (void)value;
    return ENOSYS;
}

/*
 * TODO 3：读取元素。
 *
 * 要求：
 * - index 越界时返回 ERANGE，且不得写入 *out_value。
 * - 参数为 NULL 时返回 EINVAL。
 * - 成功时写入 *out_value 并返回 0。
 */
static int vector_get(const struct int_vector *vector, size_t index,
                      int *out_value)
{
    (void)vector;
    (void)index;
    (void)out_value;
    return ENOSYS;
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

    vector_destroy(&vector);
    return EXIT_SUCCESS;
}
