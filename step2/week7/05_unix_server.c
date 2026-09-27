#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

enum { LISTEN_BACKLOG = 16, MAX_REQUEST_LENGTH = 64 };

static volatile sig_atomic_t stop_requested;

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

static int ensure_path_absent(const char *path)
{
    struct stat status;

    if (lstat(path, &status) == 0) {
        errno = EADDRINUSE;
        return -1;
    }
    return errno == ENOENT ? 0 : -1;
}

static int create_listener(const char *path)
{
    struct sockaddr_un address;
    socklen_t address_length;
    int listen_fd;

    if (make_address(path, &address, &address_length) < 0 ||
        ensure_path_absent(path) < 0) {
        return -1;
    }

    listen_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listen_fd < 0) {
        return -1;
    }
    if (bind(listen_fd, (struct sockaddr *)&address, address_length) < 0) {
        int saved_errno = errno;

        close(listen_fd);
        errno = saved_errno;
        return -1;
    }
    if (listen(listen_fd, LISTEN_BACKLOG) < 0) {
        int saved_errno = errno;

        close(listen_fd);
        unlink(path);
        errno = saved_errno;
        return -1;
    }
    return listen_fd;
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

static int read_request(int fd, char request[MAX_REQUEST_LENGTH])
{
    size_t used = 0;

    for (;;) {
        char *newline;
        ssize_t count;

        if (used + 1 == MAX_REQUEST_LENGTH) {
            errno = EOVERFLOW;
            return -1;
        }

        count = read(fd, request + used, MAX_REQUEST_LENGTH - 1 - used);
        if (count > 0) {
            used += (size_t)count;
            newline = memchr(request, '\n', used);
            if (newline != NULL) {
                *newline = '\0';
                return 0;
            }
            continue;
        }
        if (count == 0) {
            errno = EPROTO;
            return -1;
        }
        if (errno == EINTR && !stop_requested) {
            continue;
        }
        return -1;
    }
}

static int make_response(const char *request, unsigned long long *counter,
                         int *keep_running, char *response,
                         size_t response_capacity)
{
    int length;

    if (strcmp(request, "increment") == 0) {
        if (*counter == ULLONG_MAX) {
            length = snprintf(response, response_capacity,
                              "error=counter-overflow\n");
        } else {
            ++*counter;
            length = snprintf(response, response_capacity, "counter=%llu\n",
                              *counter);
        }
    } else if (strcmp(request, "get") == 0) {
        length = snprintf(response, response_capacity, "counter=%llu\n",
                          *counter);
    } else if (strcmp(request, "quit") == 0) {
        length = snprintf(response, response_capacity,
                          "counter=%llu; shutting-down\n", *counter);
        *keep_running = 0;
    } else {
        length = snprintf(response, response_capacity,
                          "error=unknown-command\n");
    }

    if (length < 0 || (size_t)length >= response_capacity) {
        errno = EOVERFLOW;
        return -1;
    }
    return length;
}

static int handle_client(int client_fd, unsigned long long *counter,
                         int *keep_running)
{
    char request[MAX_REQUEST_LENGTH];
    char response[128];
    int response_length;

    if (read_request(client_fd, request) < 0) {
        static const char invalid[] = "error=invalid-request\n";

        if (!stop_requested) {
            send_all(client_fd, invalid, sizeof(invalid) - 1);
        }
        return -1;
    }
    response_length = make_response(request, counter, keep_running,
                                    response, sizeof(response));
    if (response_length < 0) {
        return -1;
    }
    return send_all(client_fd, response, (size_t)response_length);
}

int main(int argc, char *argv[])
{
    const char *socket_path;
    unsigned long long counter = 0;
    int listen_fd = -1;
    int path_created = 0;
    int keep_running = 1;
    int exit_code = EXIT_FAILURE;

    if (argc != 2) {
        fprintf(stderr, "usage: %s SOCKET_PATH\n", argv[0]);
        return EXIT_FAILURE;
    }
    socket_path = argv[1];

    if (install_signal_handlers() < 0) {
        fprintf(stderr, "sigaction: %s\n", strerror(errno));
        goto cleanup;
    }
    listen_fd = create_listener(socket_path);
    if (listen_fd < 0) {
        fprintf(stderr, "listen on %s: %s\n", socket_path, strerror(errno));
        goto cleanup;
    }
    path_created = 1;

    printf("Unix socket server listening at %s\n", socket_path);
    fflush(stdout);

    while (keep_running && !stop_requested) {
        int client_fd = accept4(listen_fd, NULL, NULL, SOCK_CLOEXEC);

        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "accept: %s\n", strerror(errno));
            goto cleanup;
        }

        if (handle_client(client_fd, &counter, &keep_running) < 0 &&
            !stop_requested) {
            fprintf(stderr, "client request: %s\n", strerror(errno));
        }
        if (close(client_fd) < 0) {
            fprintf(stderr, "close client: %s\n", strerror(errno));
        }
    }
    exit_code = EXIT_SUCCESS;

cleanup:
    if (listen_fd >= 0) {
        close(listen_fd);
    }
    if (path_created && unlink(socket_path) < 0 && errno != ENOENT) {
        fprintf(stderr, "unlink socket %s: %s\n", socket_path,
                strerror(errno));
        exit_code = EXIT_FAILURE;
    }
    return exit_code;
}
