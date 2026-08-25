#include <stddef.h>
#include <stdio.h>

struct student {
    const char *name;
    int score;
};

typedef void (*student_callback)(struct student *student, void *context);

static void visit_students(struct student *students, size_t count,
                           student_callback callback, void *context)
{
    for (size_t index = 0; index < count; ++index) {
        callback(&students[index], context);
    }
}

static void add_bonus(struct student *student, void *context)
{
    const int bonus = *(const int *)context;

    student->score += bonus;
}

static void print_student(struct student *student, void *context)
{
    (void)context; /* 此回调没有额外上下文，显式说明以避免警告。 */
    printf("%-5s: %d\n", student->name, student->score);
}

int main(void)
{
    struct student students[] = {
        {"Ada", 92},
        {"Linus", 98},
        {"Ken", 88},
    };
    const int bonus = 2;
    const size_t count = sizeof(students) / sizeof(students[0]);

    printf("Before bonus:\n");
    visit_students(students, count, print_student, NULL);

    visit_students(students, count, add_bonus, (void *)&bonus);
    printf("\nAfter bonus:\n");
    visit_students(students, count, print_student, NULL);
    return 0;
}
