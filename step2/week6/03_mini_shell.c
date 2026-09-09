#define _POSIX_C_SOURCE 200809L

#include "command_parser.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int run_external(char *arguments[])
{
    pid_t child;
    int status;

    /* Avoid duplicating buffered output across fork(). */
    if (fflush(NULL) == EOF) {
        perror("fflush");
        return 1;
    }

    child = fork();
    if (child < 0) {
        perror("fork");
        return 1;
    }

    if (child == 0) {
        execvp(arguments[0], arguments);

        /* execvp() returns only on failure. */
        int saved_errno = errno;
        fprintf(stderr, "%s: %s\n", arguments[0], strerror(saved_errno));
        _exit(saved_errno == ENOENT ? 127 : 126);
    }

    while (waitpid(child, &status, 0) < 0) {
        if (errno == EINTR) {
            continue;
        }
        perror("waitpid");
        return 1;
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        int signal_number = WTERMSIG(status);
        fprintf(stderr, "%s: terminated by signal %d\n",
                arguments[0], signal_number);
        return 128 + signal_number;
    }

    return 1;
}

static int run_cd(char *arguments[], size_t argument_count)
{
    const char *destination;

    if (argument_count > 2) {
        fprintf(stderr, "cd: too many arguments\n");
        return 1;
    }

    if (argument_count == 1) {
        destination = getenv("HOME");
        if (destination == NULL) {
            fprintf(stderr, "cd: HOME is not set\n");
            return 1;
        }
    } else {
        destination = arguments[1];
    }

    if (chdir(destination) < 0) {
        fprintf(stderr, "cd: %s: %s\n", destination, strerror(errno));
        return 1;
    }
    return 0;
}

static int parse_exit_status(char *arguments[],
                             size_t argument_count,
                             int previous_status,
                             int *exit_status)
{
    char *end;
    long value;

    if (argument_count == 1) {
        *exit_status = previous_status;
        return 0;
    }
    if (argument_count > 2) {
        fprintf(stderr, "exit: too many arguments\n");
        return -1;
    }

    errno = 0;
    end = NULL;
    value = strtol(arguments[1], &end, 10);
    if (errno != 0 || end == arguments[1] || *end != '\0' ||
        value < 0 || value > 255) {
        fprintf(stderr, "exit: status must be an integer from 0 to 255\n");
        return -1;
    }

    *exit_status = (int)value;
    return 0;
}

int main(void)
{
    char *line = NULL;
    size_t line_capacity = 0;
    char *arguments[MAX_ARGUMENTS + 1];
    const int interactive = isatty(STDIN_FILENO);
    int last_status = 0;

    for (;;) {
        ssize_t line_length;
        size_t argument_count;

        if (interactive) {
            fputs("mini$ ", stdout);
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
            last_status = 1;
            break;
        }

        if (split_command(line, arguments,
                          sizeof(arguments) / sizeof(arguments[0]),
                          &argument_count) < 0) {
            fprintf(stderr, "too many arguments; maximum is %d\n",
                    MAX_ARGUMENTS);
            last_status = 1;
            continue;
        }
        if (argument_count == 0) {
            continue;
        }

        if (strcmp(arguments[0], "cd") == 0) {
            last_status = run_cd(arguments, argument_count);
            continue;
        }

        if (strcmp(arguments[0], "exit") == 0) {
            int exit_status;

            if (parse_exit_status(arguments, argument_count,
                                  last_status, &exit_status) == 0) {
                free(line);
                return exit_status;
            }
            last_status = 1;
            continue;
        }

        last_status = run_external(arguments);
    }

    free(line);
    return last_status;
}
