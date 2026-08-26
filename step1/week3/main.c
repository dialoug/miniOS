#include <stdio.h>

#include "math_ops.h"

int main(void)
{
    int left = 40;
    int right = 2;
    int sum = add(left, right);

    printf("%d + %d = %d\n", left, right, sum);
    return 0;
}
