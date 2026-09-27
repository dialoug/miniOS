#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

enum { INPUT_BUFFER_SIZE = 4096, MAX_COMMAND_LENGTH = 64 };

static volatile sig_atomic_t stop_requested;

struct command_parser {
    char command[MAX_COMMAND_LENGTH];
    size_t length;
    int discarding;
};

static void request_stop(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

static int install_signal_handlers(void)
{
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = request_stop;
    sigemptyset(&action.sa_mask);

    if (sigaction(SIGINT, &action, NULL) < 0 ||
        sigaction(SIGTERM, &action, NULL) < 0) {
        return -1;
    }
    return 0;
}

static int create_fifo(const char *path, int *created)
{
    struct stat status;

    *created = 0;
    if (mkfifo(path, 0600) == 0) {
        *created = 1;
        return 0;
    }
    if (errno != EEXIST) {
        return -1;
    }
    if (lstat(path, &status) < 0) {
        return -1;
    }
    if (!S_ISFIFO(status.st_mode)) {
        errno = EEXIST;
        return -1;
    }
    return 0;
}

static int process_command(const char *command, unsigned long long *counter,
                           int *keep_running)
{
    if (strcmp(command, "increment") == 0) {
        if (*counter == ULLONG_MAX) {
            fprintf(stderr, "counter has reached ULLONG_MAX\n");
            return -1;
        }
        ++*counter;
        printf("counter=%llu\n", *counter);
    } else if (strcmp(command, "get") == 0) {
        printf("counter=%llu\n", *counter);
    } else if (strcmp(command, "quit") == 0) {
        printf("counter=%llu; shutting down\n", *counter);
        *keep_running = 0;
    } else if (*command != '\0') {
        fprintf(stderr, "unknown command: %s\n", command);
    }
    fflush(stdout);
    return 0;
}

static int consume_bytes(struct command_parser *parser,
                         const char *buffer, size_t length,
                         unsigned long long *counter, int *keep_running)
{
    for (size_t index = 0; index < length && *keep_running; ++index) {
        unsigned char byte = (unsigned char)buffer[index];

        if (byte == '\n') {
            if (parser->discarding) {
                fprintf(stderr, "command exceeded %d bytes and was discarded\n",
                        MAX_COMMAND_LENGTH - 1);
            } else {
                parser->command[parser->length] = '\0';
                if (process_command(parser->command, counter, keep_running) < 0) {
                    return -1;
                }
            }
            parser->length = 0;
            parser->discarding = 0;
            continue;
        }

        if (parser->discarding) {
            continue;
        }
        if (parser->length + 1 >= sizeof(parser->command)) {
            parser->length = 0;
            parser->discarding = 1;
            continue;
        }
        parser->command[parser->length++] = (char)byte;
    }
    return 0;
}

static int serve_one_writer(const char *path, unsigned long long *counter,
                            int *keep_running)
{
    struct command_parser parser = { 0 };
    struct stat status;
    char buffer[INPUT_BUFFER_SIZE];
    int fifo_fd;

    do {
        fifo_fd = open(path, O_RDONLY | O_CLOEXEC);
    } while (fifo_fd < 0 && errno == EINTR && !stop_requested);

    if (fifo_fd < 0) {
        if (stop_requested && errno == EINTR) {
            return 0;
        }
        return -1;
    }
    if (fstat(fifo_fd, &status) < 0) {
        close(fifo_fd);
        return -1;
    }
    if (!S_ISFIFO(status.st_mode)) {
        errno = EINVAL;
        close(fifo_fd);
        return -1;
    }

    while (*keep_running && !stop_requested) {
        ssize_t count = read(fifo_fd, buffer, sizeof(buffer));

        if (count > 0) {
            if (consume_bytes(&parser, buffer, (size_t)count,
                              counter, keep_running) < 0) {
                close(fifo_fd);
                return -1;
            }
            continue;
        }
        if (count == 0) {
            if (parser.length != 0 || parser.discarding) {
                fprintf(stderr, "writer closed with an incomplete command\n");
            }
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        close(fifo_fd);
        return -1;
    }

    if (close(fifo_fd) < 0) {
        return -1;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    const char *fifo_path;
    unsigned long long counter = 0;
    int created_fifo = 0;
    int keep_running = 1;
    int exit_code = EXIT_FAILURE;

    if (argc != 2) {
        fprintf(stderr, "usage: %s FIFO_PATH\n", argv[0]);
        return EXIT_FAILURE;
    }
    fifo_path = argv[1];

    if (install_signal_handlers() < 0) {
        fprintf(stderr, "sigaction: %s\n", strerror(errno));
        goto cleanup;
    }
    if (create_fifo(fifo_path, &created_fifo) < 0) {
        fprintf(stderr, "create FIFO %s: %s\n", fifo_path, strerror(errno));
        goto cleanup;
    }

    printf("FIFO server listening at %s\n", fifo_path);
    fflush(stdout);
    while (keep_running && !stop_requested) {
        if (serve_one_writer(fifo_path, &counter, &keep_running) < 0) {
            fprintf(stderr, "serve FIFO %s: %s\n", fifo_path,
                    strerror(errno));
            goto cleanup;
        }
    }
    exit_code = EXIT_SUCCESS;

cleanup:
    if (created_fifo && unlink(fifo_path) < 0 && errno != ENOENT) {
        fprintf(stderr, "unlink FIFO %s: %s\n", fifo_path, strerror(errno));
        exit_code = EXIT_FAILURE;
    }
    return exit_code;
}
