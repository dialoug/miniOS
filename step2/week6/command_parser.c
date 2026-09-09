#include "command_parser.h"

#include <ctype.h>
#include <errno.h>

int split_command(char *line,
                  char *arguments[],
                  size_t argument_capacity,
                  size_t *argument_count)
{
    char *cursor = line;
    size_t count = 0;

    while (*cursor != '\0') {
        while (isspace((unsigned char)*cursor)) {
            ++cursor;
        }
        if (*cursor == '\0') {
            break;
        }
        if (count + 1 >= argument_capacity) {
            arguments[0] = NULL;
            *argument_count = 0;
            errno = E2BIG;
            return -1;
        }

        arguments[count++] = cursor;
        while (*cursor != '\0' && !isspace((unsigned char)*cursor)) {
            ++cursor;
        }
        if (*cursor != '\0') {
            *cursor = '\0';
            ++cursor;
        }
    }

    arguments[count] = NULL;
    *argument_count = count;
    return 0;
}
