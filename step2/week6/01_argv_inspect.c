#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[])
{
    printf("argc = %d\n", argc);

    for (int index = 0; index < argc; ++index) {
        printf("argv[%d] = \"%s\" (length=%zu)\n",
               index, argv[index], strlen(argv[index]));
    }

    printf("argv[%d] = %p (terminator)\n",
           argc, (void *)argv[argc]);
    return EXIT_SUCCESS;
}
