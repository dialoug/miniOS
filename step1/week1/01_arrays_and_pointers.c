#include <stddef.h>
#include <stdio.h>

#define ARRAY_LENGTH(array) (sizeof(array) / sizeof((array)[0]))

/*
 * 注意：这里的 numbers 已经不是“数组”，而是指向首元素的指针。
 * 因此必须额外传入 length，不能在函数内用 ARRAY_LENGTH(numbers)。
 */
static void print_numbers(const int *numbers, size_t length)
{
    for (size_t index = 0; index < length; ++index) {
        printf("numbers[%zu] = %d, address = %p\n", index, numbers[index],
               (const void *)&numbers[index]);
    }
}

static void increment_all(int *numbers, size_t length)
{
    for (size_t index = 0; index < length; ++index) {
        numbers[index] += 1;
    }
}

int main(void)
{
    int numbers[] = {10, 20, 30, 40};
    char writable_message[] = "kernel";
    const char *read_only_message = "linux";

    printf("array bytes: %zu\n", sizeof(numbers));
    printf("element count: %zu\n\n", ARRAY_LENGTH(numbers));

    print_numbers(numbers, ARRAY_LENGTH(numbers));
    increment_all(numbers, ARRAY_LENGTH(numbers));
    printf("\nAfter increment_all:\n");
    print_numbers(numbers, ARRAY_LENGTH(numbers));

    writable_message[0] = 'K';
    printf("\nwritable_message: %s\n", writable_message);
    printf("read_only_message: %s\n", read_only_message);

    /*
     * 不要这样做：字符串字面量通常位于只读区域，修改它是未定义行为。
     * read_only_message[0] = 'L';
     */
    return 0;
}
