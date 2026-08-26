#include <stddef.h>
#include <stdio.h>
#include <string.h>
struct compact
{
       char tag;
       int id;
       double score;
};

struct scattered
{
       char tag;
       double score;
       int id;
};
struct optimized
{
       double score;
       int id;
       char tag;
};

static void show_layout(void)
{
       struct compact compact = {'A', 42, 98.5};
       struct scattered scattered = {'B', 87.5, 7};
       struct optimized optimized = {76.3, 13, 'C'};

       struct optimized control;

       memset(&control, 0, sizeof(control));
       control.score = 76.3;
       control.id = 13;
       control.tag = 'C';

       printf("struct compact: size=%zu, alignment=%zu\n",
              sizeof(compact), _Alignof(struct compact));
       printf("  offsets: tag=%zu, id=%zu, score=%zu\n",
              offsetof(struct compact, tag), offsetof(struct compact, id),
              offsetof(struct compact, score));
       printf("  addresses: object=%p, tag=%p, id=%p, score=%p\n",
              (void *)&compact, (void *)&compact.tag, (void *)&compact.id,
              (void *)&compact.score);

       printf("struct scattered: size=%zu, alignment=%zu\n",
              sizeof(scattered), _Alignof(struct scattered));
       printf("  offsets: tag=%zu, score=%zu, id=%zu\n",
              offsetof(struct scattered, tag), offsetof(struct scattered, score),
              offsetof(struct scattered, id));
       printf("  addresses: object=%p, tag=%p, score=%p, id=%p\n",
              (void *)&scattered, (void *)&scattered.tag, (void *)&scattered.score,
              (void *)&scattered.id);

       printf("struct optimized: size=%zu, alignment=%zu\n",
              sizeof(optimized), _Alignof(struct optimized));
       printf("  offsets: score=%zu, id=%zu, tag=%zu\n",
              offsetof(struct optimized, score), offsetof(struct optimized, id),
              offsetof(struct optimized, tag));
       printf("  addresses: object=%p, score=%p, id=%p, tag=%p\n",
              (void *)&optimized, (void *)&optimized.score, (void *)&optimized.id,
              (void *)&optimized.tag);

       printf("field equality: %d\n",
              optimized.score == control.score &&
                  optimized.id == control.id &&
                  optimized.tag == control.tag);

       printf("byte equality: %d\n",
              memcmp(&optimized, &control, sizeof(optimized)) == 0);
}

int main(void)
{
       static int static_value = 1;
       int stack_value = 2;
       int *stack_alias = NULL;

       stack_alias = &stack_value;
       printf("storage examples: static=%p, stack=%p\n",
              (void *)&static_value, (void *)&stack_value);
       printf("stack_value through a pointer = %d\n", *stack_alias);
       show_layout();
       return 0;
}
