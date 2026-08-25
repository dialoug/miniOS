#include <stddef.h>
#include <stdio.h>

#define ARRAY_LENGTH(array) (sizeof(array) / sizeof((array)[0]))
#define CONTAINER_OF(member_pointer, type, member) \
    ((type *)((char *)(member_pointer) - offsetof(type, member)))

struct list_node {
    struct list_node *next;
};

struct task {
    int process_id;
    const char *name;
    struct list_node run_queue_node;
};

static int min_int(int left, int right)
{
    return left < right ? left : right;
}

int main(void)
{
    int values[] = {4, 8, 15, 16, 23, 42};
    struct task task = {
        .process_id = 42,
        .name = "worker",
        .run_queue_node = { .next = NULL },
    };
    struct list_node *node = &task.run_queue_node;
    struct task *owner = CONTAINER_OF(node, struct task, run_queue_node);

    printf("ARRAY_LENGTH(values) = %zu\n", ARRAY_LENGTH(values));
    printf("min_int(7, 3) = %d\n", min_int(7, 3));
    printf("owner: pid=%d, name=%s\n", owner->process_id, owner->name);

    /*
     * 反例：函数式宏可能对参数求值多次，下面的写法会使 i 增加两次。
     * #define MIN_BAD(a, b) ((a) < (b) ? (a) : (b))
     * int i = 0;
     * int value = MIN_BAD(i++, 10);
     *
     * 这里使用 static 函数 min_int()，避免重复求值的问题。
     */
    return 0;
}
