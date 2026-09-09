#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

enum {
    DEFAULT_PORT = 9090,
    IO_CHUNK = 16384,
    RECEIVE_BUFFER = 4096
};

#define SEND_LIMIT ((size_t)16 * 1024 * 1024)

static int parse_port(const char *text, uint16_t *port)
{
    char *end;
    unsigned long value;

    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || *text == '\0' || *end != '\0' || value == 0 ||
        value > UINT16_MAX) {
        return -1;
    }
    *port = (uint16_t)value;
    return 0;
}

static int set_nonblocking(int fd, bool enabled)
{
    int flags = fcntl(fd, F_GETFL);

    if (flags < 0) {
        return -1;
    }
    if (enabled) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }
    return fcntl(fd, F_SETFL, flags);
}

static int sleep_one_second(void)
{
    struct timespec remaining = {
        .tv_sec = 1,
        .tv_nsec = 0,
    };

    while (nanosleep(&remaining, &remaining) < 0) {
        if (errno != EINTR) {
            return -1;
        }
    }
    return 0;
}

/* Returns 1 when writable, 0 after a sustained 100 ms stall, and -1 on error. */
static int wait_until_writable(int fd)
{
    struct pollfd watched = {
        .fd = fd,
        .events = POLLOUT,
    };

    for (;;) {
        int ready = poll(&watched, 1, 100);

        if (ready > 0) {
            if ((watched.revents & POLLOUT) != 0) {
                return 1;
            }
            errno = EPIPE;
            return -1;
        }
        if (ready == 0) {
            return 0;
        }
        if (errno != EINTR) {
            return -1;
        }
    }
}

int main(int argc, char *argv[])
{
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
    };
    unsigned char chunk[IO_CHUNK];
    uint16_t port = DEFAULT_PORT;
    size_t sent_total = 0;
    size_t echoed_total = 0;
    int receive_buffer = RECEIVE_BUFFER;
    bool reached_backpressure = false;
    int fd = -1;
    int exit_code = EXIT_FAILURE;

    if (argc > 2 || (argc == 2 && parse_port(argv[1], &port) < 0)) {
        fprintf(stderr, "usage: %s [PORT]\n", argv[0]);
        return EXIT_FAILURE;
    }
    address.sin_port = htons(port);
    memset(chunk, 'x', sizeof(chunk));

    fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        fprintf(stderr, "socket: %s\n", strerror(errno));
        goto cleanup;
    }
    if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF,
                   &receive_buffer, sizeof(receive_buffer)) < 0) {
        fprintf(stderr, "setsockopt SO_RCVBUF: %s\n", strerror(errno));
        goto cleanup;
    }
    if (connect(fd, (const struct sockaddr *)&address, sizeof(address)) < 0) {
        fprintf(stderr, "connect: %s\n", strerror(errno));
        goto cleanup;
    }
    if (set_nonblocking(fd, true) < 0) {
        fprintf(stderr, "set nonblocking: %s\n", strerror(errno));
        goto cleanup;
    }

    while (sent_total < SEND_LIMIT) {
        size_t remaining = SEND_LIMIT - sent_total;
        size_t requested = remaining < sizeof(chunk) ? remaining : sizeof(chunk);
        ssize_t count = send(fd, chunk, requested, MSG_NOSIGNAL);

        if (count > 0) {
            sent_total += (size_t)count;
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            int writable = wait_until_writable(fd);

            if (writable > 0) {
                continue;
            }
            if (writable == 0) {
                reached_backpressure = true;
                break;
            }
            fprintf(stderr, "poll writable: %s\n", strerror(errno));
            goto cleanup;
        }
        fprintf(stderr, "send: %s\n", strerror(errno));
        goto cleanup;
    }

    if (shutdown(fd, SHUT_WR) < 0) {
        fprintf(stderr, "shutdown write side: %s\n", strerror(errno));
        goto cleanup;
    }
    printf("queued %zu bytes; delaying reads for one second%s\n",
           sent_total, reached_backpressure ? " after EAGAIN" : "");

    if (sleep_one_second() < 0 || set_nonblocking(fd, false) < 0) {
        fprintf(stderr, "prepare receive: %s\n", strerror(errno));
        goto cleanup;
    }

    for (;;) {
        ssize_t count = recv(fd, chunk, sizeof(chunk), 0);

        if (count > 0) {
            echoed_total += (size_t)count;
            continue;
        }
        if (count == 0) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        fprintf(stderr, "receive echo: %s\n", strerror(errno));
        goto cleanup;
    }

    printf("received %zu echoed bytes\n", echoed_total);
    if (echoed_total != sent_total) {
        fprintf(stderr, "byte count mismatch: sent=%zu, received=%zu\n",
                sent_total, echoed_total);
        goto cleanup;
    }
    exit_code = EXIT_SUCCESS;

cleanup:
    if (fd >= 0) {
        close(fd);
    }
    return exit_code;
}
