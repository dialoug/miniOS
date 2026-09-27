#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

static int make_address(const char *path, struct sockaddr_un *address,
                        socklen_t *address_length)
{
    size_t path_length = strlen(path);

    if (path_length == 0 || path_length >= sizeof(address->sun_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memset(address, 0, sizeof(*address));
    address->sun_family = AF_UNIX;
    memcpy(address->sun_path, path, path_length + 1);
    *address_length = (socklen_t)(offsetof(struct sockaddr_un, sun_path) +
                                  path_length + 1);
    return 0;
}

static int send_all(int fd, const char *buffer, size_t length)
{
    size_t sent_total = 0;

    while (sent_total < length) {
        ssize_t sent = send(fd, buffer + sent_total, length - sent_total,
                            MSG_NOSIGNAL);

        if (sent > 0) {
            sent_total += (size_t)sent;
            continue;
        }
        if (sent < 0 && errno == EINTR) {
            continue;
        }
        if (sent == 0) {
            errno = EIO;
        }
        return -1;
    }
    return 0;
}

static int copy_response_to_stdout(int fd)
{
    char buffer[4096];

    for (;;) {
        ssize_t count = read(fd, buffer, sizeof(buffer));

        if (count > 0) {
            size_t written_total = 0;

            while (written_total < (size_t)count) {
                ssize_t written = write(STDOUT_FILENO,
                                        buffer + written_total,
                                        (size_t)count - written_total);
                if (written > 0) {
                    written_total += (size_t)written;
                } else if (written < 0 && errno == EINTR) {
                    continue;
                } else {
                    if (written == 0) {
                        errno = EIO;
                    }
                    return -1;
                }
            }
            continue;
        }
        if (count == 0) {
            return 0;
        }
        if (errno != EINTR) {
            return -1;
        }
    }
}

int main(int argc, char *argv[])
{
    const char *socket_path;
    const char *command;
    struct sockaddr_un address;
    socklen_t address_length;
    char request[80];
    int request_length;
    int socket_fd = -1;
    int exit_code = EXIT_FAILURE;

    if (argc != 3) {
        fprintf(stderr, "usage: %s SOCKET_PATH increment|get|quit\n", argv[0]);
        return EXIT_FAILURE;
    }
    socket_path = argv[1];
    command = argv[2];
    if (strcmp(command, "increment") != 0 &&
        strcmp(command, "get") != 0 &&
        strcmp(command, "quit") != 0) {
        fprintf(stderr, "unknown command: %s\n", command);
        return EXIT_FAILURE;
    }
    if (make_address(socket_path, &address, &address_length) < 0) {
        fprintf(stderr, "socket path %s: %s\n", socket_path, strerror(errno));
        return EXIT_FAILURE;
    }

    request_length = snprintf(request, sizeof(request), "%s\n", command);
    if (request_length < 0 || (size_t)request_length >= sizeof(request)) {
        fprintf(stderr, "command is too long\n");
        return EXIT_FAILURE;
    }

    socket_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (socket_fd < 0) {
        fprintf(stderr, "socket: %s\n", strerror(errno));
        goto cleanup;
    }
    if (connect(socket_fd, (struct sockaddr *)&address, address_length) < 0) {
        fprintf(stderr, "connect %s: %s\n", socket_path, strerror(errno));
        goto cleanup;
    }
    if (send_all(socket_fd, request, (size_t)request_length) < 0) {
        fprintf(stderr, "send request: %s\n", strerror(errno));
        goto cleanup;
    }
    if (shutdown(socket_fd, SHUT_WR) < 0) {
        fprintf(stderr, "shutdown write side: %s\n", strerror(errno));
        goto cleanup;
    }
    if (copy_response_to_stdout(socket_fd) < 0) {
        fprintf(stderr, "read response: %s\n", strerror(errno));
        goto cleanup;
    }
    exit_code = EXIT_SUCCESS;

cleanup:
    if (socket_fd >= 0 && close(socket_fd) < 0 && exit_code == EXIT_SUCCESS) {
        fprintf(stderr, "close socket: %s\n", strerror(errno));
        exit_code = EXIT_FAILURE;
    }
    return exit_code;
}
