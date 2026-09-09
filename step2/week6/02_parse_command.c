#define _POSIX_C_SOURCE 200809L

#include "command_parser.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void)
{
    char *line = NULL;
    size_t line_capacity = 0;
    char *arguments[MAX_ARGUMENTS + 1];
    const int interactive = isatty(STDIN_FILENO);

    for (;;) {
        ssize_t line_length;
        size_t argument_count;

        if (interactive) {
            fputs("parse> ", stdout);
            fflush(stdout);
        }

        errno = 0;
        line_length = getline(&line, &line_capacity, stdin);
        if (line_length < 0) {
            if (feof(stdin)) {
                break;
            }
            if (errno == EINTR) {
                clearerr(stdin);
                continue;
            }
            perror("getline");
            free(line);
            return EXIT_FAILURE;
        }

        if (split_command(line, arguments,
                          sizeof(arguments) / sizeof(arguments[0]),
                          &argument_count) < 0) {
            fprintf(stderr, "too many arguments; maximum is %d\n",
                    MAX_ARGUMENTS);
            continue;
        }

        printf("argc = %zu\n", argument_count);
        for (size_t index = 0; index < argument_count; ++index) {
            printf("argv[%zu] = \"%s\"\n", index, arguments[index]);
        }
        printf("argv[%zu] = %p (terminator)\n",
               argument_count, (void *)arguments[argument_count]);
    }

    free(line);
    return EXIT_SUCCESS;
}
