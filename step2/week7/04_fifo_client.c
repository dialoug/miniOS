#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static int write_all(int fd, const char *buffer, size_t length)
{
    size_t written_total = 0;

    while (written_total < length) {
        ssize_t written = write(fd, buffer + written_total,
                                length - written_total);

        if (written > 0) {
            written_total += (size_t)written;
            continue;
        }
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written == 0) {
            errno = EIO;
        }
        return -1;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    const char *path;
    const char *command;
    struct stat status;
    char message[80];
    int message_length;
    int fifo_fd = -1;
    int exit_code = EXIT_FAILURE;

    if (argc != 3) {
        fprintf(stderr, "usage: %s FIFO_PATH increment|get|quit\n", argv[0]);
        return EXIT_FAILURE;
    }
    path = argv[1];
    command = argv[2];
    if (strcmp(command, "increment") != 0 &&
        strcmp(command, "get") != 0 &&
        strcmp(command, "quit") != 0) {
        fprintf(stderr, "unknown command: %s\n", command);
        return EXIT_FAILURE;
    }

    message_length = snprintf(message, sizeof(message), "%s\n", command);
    if (message_length < 0 || (size_t)message_length >= sizeof(message)) {
        fprintf(stderr, "command is too long\n");
        return EXIT_FAILURE;
    }

    do {
        fifo_fd = open(path, O_WRONLY | O_CLOEXEC);
    } while (fifo_fd < 0 && errno == EINTR);
    if (fifo_fd < 0) {
        fprintf(stderr, "open FIFO %s: %s\n", path, strerror(errno));
        goto cleanup;
    }
    if (fstat(fifo_fd, &status) < 0) {
        fprintf(stderr, "fstat FIFO %s: %s\n", path, strerror(errno));
        goto cleanup;
    }
    if (!S_ISFIFO(status.st_mode)) {
        errno = EINVAL;
        fprintf(stderr, "%s is not a FIFO: %s\n", path, strerror(errno));
        goto cleanup;
    }
    if (write_all(fifo_fd, message, (size_t)message_length) < 0) {
        fprintf(stderr, "write FIFO %s: %s\n", path, strerror(errno));
        goto cleanup;
    }
    exit_code = EXIT_SUCCESS;

cleanup:
    if (fifo_fd >= 0 && close(fifo_fd) < 0 && exit_code == EXIT_SUCCESS) {
        fprintf(stderr, "close FIFO %s: %s\n", path, strerror(errno));
        exit_code = EXIT_FAILURE;
    }
    return exit_code;
}
