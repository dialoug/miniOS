#define _POSIX_C_SOURCE 200809L

#include "command_parser.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int shell_interactive;
static pid_t shell_pgid;

static void set_signal_handlers(int ignore)
{
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = ignore ? SIG_IGN : SIG_DFL;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGQUIT, &action, NULL);
    sigaction(SIGTSTP, &action, NULL);
    sigaction(SIGTTIN, &action, NULL);
    sigaction(SIGTTOU, &action, NULL);
}

static void initialize_job_control(void)
{
    shell_interactive = isatty(STDIN_FILENO);
    if (!shell_interactive) {
        return;
    }

    shell_pgid = getpid();
    if (setpgid(shell_pgid, shell_pgid) < 0 && errno != EACCES) {
        perror("setpgid");
        exit(EXIT_FAILURE);
    }
    if (tcsetpgrp(STDIN_FILENO, shell_pgid) < 0) {
        perror("tcsetpgrp");
        exit(EXIT_FAILURE);
    }
    set_signal_handlers(1);
}

static void give_terminal_to(pid_t process_group)
{
    if (shell_interactive && tcsetpgrp(STDIN_FILENO, process_group) < 0) {
        perror("tcsetpgrp");
    }
}

static void prepare_child_process(pid_t process_group)
{
    if (setpgid(0, process_group) < 0) {
        _exit(126);
    }
    set_signal_handlers(0);
}

static int redirect_fd(const char *operator,
                       const char *path,
                       int target_fd)
{
    int flags = O_WRONLY | O_CREAT;
    int file_fd;

    if (strcmp(operator, ">") == 0) {
        flags |= O_TRUNC;
    } else {
        flags |= O_APPEND;
    }

    file_fd = open(path, flags, 0666);
    if (file_fd < 0) {
        fprintf(stderr, "%s: %s: %s\n", operator, path, strerror(errno));
        return -1;
    }
    if (dup2(file_fd, target_fd) < 0) {
        fprintf(stderr, "dup2: %s\n", strerror(errno));
        close(file_fd);
        return -1;
    }
    close(file_fd);
    return 0;
}

static void execute_command(char *arguments[],
                            size_t argument_count,
                            int input_fd,
                            int output_fd)
{
    char *command_arguments[MAX_ARGUMENTS + 1];
    size_t command_count = 0;

    if (input_fd >= 0 && dup2(input_fd, STDIN_FILENO) < 0) {
        fprintf(stderr, "dup2: %s\n", strerror(errno));
        _exit(126);
    }
    if (output_fd >= 0 && dup2(output_fd, STDOUT_FILENO) < 0) {
        fprintf(stderr, "dup2: %s\n", strerror(errno));
        _exit(126);
    }
    if (input_fd >= 0) {
        close(input_fd);
    }
    if (output_fd >= 0) {
        close(output_fd);
    }

    for (size_t index = 0; index < argument_count; ++index) {
        const char *operator = arguments[index];

        if (strcmp(operator, "<") == 0) {
            if (index + 1 >= argument_count) {
                fprintf(stderr, "%s: missing file operand\n", operator);
                _exit(2);
            }
            int file_fd = open(arguments[++index], O_RDONLY);
            if (file_fd < 0) {
                fprintf(stderr, "%s: %s: %s\n", operator,
                        arguments[index], strerror(errno));
                _exit(126);
            }
            if (dup2(file_fd, STDIN_FILENO) < 0) {
                fprintf(stderr, "dup2: %s\n", strerror(errno));
                close(file_fd);
                _exit(126);
            }
            close(file_fd);
            continue;
        }

        if (strcmp(operator, ">") == 0 ||
            strcmp(operator, ">>") == 0) {
            if (index + 1 >= argument_count) {
                fprintf(stderr, "%s: missing file operand\n", operator);
                _exit(2);
            }
            if (redirect_fd(operator, arguments[++index], STDOUT_FILENO) < 0) {
                _exit(126);
            }
            continue;
        }

        command_arguments[command_count++] = arguments[index];
    }
    command_arguments[command_count] = NULL;

    if (command_count == 0) {
        fprintf(stderr, "redirection requires a command\n");
        _exit(2);
    }

    execvp(command_arguments[0], command_arguments);

    /* execvp() returns only on failure. */
    int saved_errno = errno;
    fprintf(stderr, "%s: %s\n", command_arguments[0],
            strerror(saved_errno));
    _exit(saved_errno == ENOENT ? 127 : 126);
}

static int run_external(char *arguments[], size_t argument_count)
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
        prepare_child_process(0);
        execute_command(arguments, argument_count, -1, -1);
    }

    if (setpgid(child, child) < 0 && errno != EACCES) {
        perror("setpgid");
    }
    give_terminal_to(child);

    while (waitpid(child, &status, WUNTRACED) < 0) {
        if (errno == EINTR) {
            continue;
        }
        perror("waitpid");
        return 1;
    }
    give_terminal_to(shell_pgid);

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        int signal_number = WTERMSIG(status);
        fprintf(stderr, "%s: terminated by signal %d\n",
                arguments[0], signal_number);
        return 128 + signal_number;
    }
    if (WIFSTOPPED(status)) {
        fprintf(stderr, "%s: stopped by signal %d\n",
                arguments[0], WSTOPSIG(status));
        return 128 + WSTOPSIG(status);
    }

    return 1;
}

static int run_pipeline(char *arguments[],
                        size_t argument_count,
                        size_t pipe_index)
{
    int pipe_fds[2];
    pid_t left_child;
    pid_t right_child;
    int left_status;
    int right_status;

    if (pipe_index == 0 || pipe_index + 1 == argument_count) {
        fprintf(stderr, "pipe requires a command on both sides\n");
        return 2;
    }
    if (pipe(pipe_fds) < 0) {
        perror("pipe");
        return 1;
    }

    if (fflush(NULL) == EOF) {
        perror("fflush");
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return 1;
    }

    left_child = fork();
    if (left_child < 0) {
        perror("fork");
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return 1;
    }
    if (left_child == 0) {
        close(pipe_fds[0]);
        prepare_child_process(0);
        execute_command(arguments, pipe_index, -1, pipe_fds[1]);
    }

    if (setpgid(left_child, left_child) < 0 && errno != EACCES) {
        perror("setpgid");
    }

    right_child = fork();
    if (right_child < 0) {
        perror("fork");
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        waitpid(left_child, NULL, 0);
        return 1;
    }
    if (right_child == 0) {
        close(pipe_fds[1]);
        prepare_child_process(left_child);
        execute_command(arguments + pipe_index + 1,
                        argument_count - pipe_index - 1,
                        pipe_fds[0], -1);
    }

    close(pipe_fds[0]);
    close(pipe_fds[1]);
    while (waitpid(left_child, &left_status, WUNTRACED) < 0) {
        if (errno != EINTR) {
            perror("waitpid");
            return 1;
        }

        if (setpgid(right_child, left_child) < 0 && errno != EACCES) {
            perror("setpgid");
        }
        give_terminal_to(left_child);
    }
    while (waitpid(right_child, &right_status, WUNTRACED) < 0) {
        if (errno != EINTR) {
            perror("waitpid");
            return 1;
        }
    }
    give_terminal_to(shell_pgid);

    if (WIFEXITED(right_status)) {
        return WEXITSTATUS(right_status);
    }
    if (WIFSIGNALED(right_status)) {
        return 128 + WTERMSIG(right_status);
    }
    if (WIFSTOPPED(right_status)) {
        return 128 + WSTOPSIG(right_status);
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
    int last_status = 0;

    initialize_job_control();

    for (;;) {
        ssize_t line_length;
        size_t argument_count;

        if (shell_interactive) {
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

        size_t pipe_count = 0;
        size_t pipe_index = 0;
        for (size_t index = 0; index < argument_count; ++index) {
            if (strcmp(arguments[index], "|") == 0) {
                ++pipe_count;
                pipe_index = index;
            }
        }
        if (pipe_count > 1) {
            fprintf(stderr, "only one pipe is supported\n");
            last_status = 2;
            continue;
        }
        if (pipe_count == 1) {
            last_status = run_pipeline(arguments, argument_count, pipe_index);
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

        last_status = run_external(arguments, argument_count);
    }

    free(line);
    return last_status;
}
