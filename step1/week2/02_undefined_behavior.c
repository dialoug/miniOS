#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void heap_out_of_bounds(void)
{
    int *values = malloc(2 * sizeof(*values));

    if (values == NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    values[0] = 10;
    values[1] = 20;
    values[2] = 30; /* Intentional undefined behavior: one past the allocation. */
    free(values);
}

static void stack_out_of_bounds(void)
{
    char buffer[5] = "test";

    buffer[5] = '!'; /* Intentional undefined behavior: one past the array. */
    puts(buffer);
}

static void use_after_free(void)
{
    char *message = malloc(6);

    if (message == NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    memcpy(message, "hello", 6);
    free(message);
    puts(message); /* Intentional undefined behavior: lifetime has ended. */
}

static void signed_overflow(void)
{
    int value = INT_MAX;

    value += 1; /* Intentional undefined behavior for signed int. */
    printf("value = %d\n", value);
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr,
                "usage: %s {heap-oob|stack-oob|use-after-free|overflow}\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    if (strcmp(argv[1], "heap-oob") == 0) {
        heap_out_of_bounds();
    } else if (strcmp(argv[1], "stack-oob") == 0) {
        stack_out_of_bounds();
    } else if (strcmp(argv[1], "use-after-free") == 0) {
        use_after_free();
    } else if (strcmp(argv[1], "overflow") == 0) {
        signed_overflow();
    } else {
        fprintf(stderr, "unknown mode: %s\n", argv[1]);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
