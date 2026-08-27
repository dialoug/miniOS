#include <stdio.h>

#include "math_ops.h"

static int initialized_counter = 7;
static int zero_counter;

int main(void)
{
    int left = 40;
    int right = 2;
    int sum = add(left, right);

    printf("%d + %d = %d\n", left, right, sum);
    printf("initialized=%d, zero=%d\n",
       initialized_counter, zero_counter);
    return 0;
}
