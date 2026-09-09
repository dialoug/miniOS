#define _GNU_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

enum {
    DEFAULT_PORT = 9090,
    MAX_EVENTS = 64,
    IO_CHUNK = 4096,
    SHUTDOWN_GRACE_MS = 3000
};

#define OUTPUT_HIGH_WATER ((size_t)1024 * 1024)
#define OUTPUT_LOW_WATER ((size_t)256 * 1024)

struct byte_buffer {
    unsigned char *data;
    size_t start;
    size_t end;
    size_t capacity;
};

struct client {
    int fd;
    bool read_closed;
    bool read_paused;
    struct byte_buffer output;
    struct client *next;
};

struct server {
    int epoll_fd;
    int listen_fd;
    int signal_fd;
    bool stopping;
    struct timespec shutdown_deadline;
    struct client *clients;
};

static char listen_token;
static char signal_token;

static size_t buffer_pending(const struct byte_buffer *buffer)
{
    return buffer->end - buffer->start;
}

static int buffer_append(struct byte_buffer *buffer,
                         const unsigned char *data,
                         size_t length)
{
    size_t pending = buffer_pending(buffer);
    size_t required;
    size_t new_capacity;
    unsigned char *new_data;

    if (length > SIZE_MAX - pending) {
        errno = ENOMEM;
        return -1;
    }

    if (buffer->capacity - buffer->end < length && buffer->start > 0) {
        memmove(buffer->data, buffer->data + buffer->start, pending);
        buffer->start = 0;
        buffer->end = pending;
    }
    if (buffer->capacity - buffer->end >= length) {
        memcpy(buffer->data + buffer->end, data, length);
        buffer->end += length;
        return 0;
    }

    required = buffer->end + length;
    new_capacity = buffer->capacity == 0 ? IO_CHUNK : buffer->capacity;
    while (new_capacity < required) {
        if (new_capacity > SIZE_MAX / 2) {
            new_capacity = required;
            break;
        }
        new_capacity *= 2;
    }

    new_data = realloc(buffer->data, new_capacity);
    if (new_data == NULL) {
        return -1;
    }
    buffer->data = new_data;
    buffer->capacity = new_capacity;
    memcpy(buffer->data + buffer->end, data, length);
    buffer->end += length;
    return 0;
}

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

static int add_special_fd(int epoll_fd, int fd, void *token)
{
    struct epoll_event event = {
        .events = EPOLLIN,
        .data.ptr = token,
    };

    return epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event);
}

static int create_listener(uint16_t port)
{
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
    };
    int enabled = 1;
    int fd;

    fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return -1;
    }
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) < 0 ||
        bind(fd, (const struct sockaddr *)&address, sizeof(address)) < 0 ||
        listen(fd, SOMAXCONN) < 0) {
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return -1;
    }
    return fd;
}

static int create_signal_fd(void)
{
    sigset_t signals;

    if (sigemptyset(&signals) < 0 ||
        sigaddset(&signals, SIGINT) < 0 ||
        sigaddset(&signals, SIGTERM) < 0 ||
        sigprocmask(SIG_BLOCK, &signals, NULL) < 0) {
        return -1;
    }
    return signalfd(-1, &signals, SFD_NONBLOCK | SFD_CLOEXEC);
}

static int update_client_interest(const struct server *server,
                                  const struct client *client)
{
    struct epoll_event event = {
        .events = 0,
        .data.ptr = (void *)client,
    };

    if (!client->read_closed) {
        event.events |= EPOLLRDHUP;
        if (!client->read_paused) {
            event.events |= EPOLLIN;
        }
    }
    if (buffer_pending(&client->output) > 0) {
        event.events |= EPOLLOUT;
    }
    return epoll_ctl(server->epoll_fd, EPOLL_CTL_MOD, client->fd, &event);
}

static void unlink_client(struct server *server, struct client *client)
{
    struct client **link = &server->clients;

    while (*link != NULL && *link != client) {
        link = &(*link)->next;
    }
    if (*link == client) {
        *link = client->next;
    }
}

static void destroy_client(struct server *server, struct client *client)
{
    printf("closed fd=%d\n", client->fd);
    epoll_ctl(server->epoll_fd, EPOLL_CTL_DEL, client->fd, NULL);
    close(client->fd);
    unlink_client(server, client);
    free(client->output.data);
    free(client);
}

static int add_client(struct server *server, int client_fd)
{
    struct client *client = calloc(1, sizeof(*client));
    struct epoll_event event;

    if (client == NULL) {
        return -1;
    }
    client->fd = client_fd;
    event.events = EPOLLIN | EPOLLRDHUP;
    event.data.ptr = client;

    if (epoll_ctl(server->epoll_fd, EPOLL_CTL_ADD, client_fd, &event) < 0) {
        free(client);
        return -1;
    }
    client->next = server->clients;
    server->clients = client;
    printf("accepted fd=%d\n", client_fd);
    return 0;
}

static int accept_clients(struct server *server)
{
    for (;;) {
        int client_fd = accept4(server->listen_fd, NULL, NULL,
                                SOCK_NONBLOCK | SOCK_CLOEXEC);

        if (client_fd >= 0) {
            if (add_client(server, client_fd) < 0) {
                int saved_errno = errno;
                close(client_fd);
                errno = saved_errno;
                return -1;
            }
            continue;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        return -1;
    }
}

static int read_client(struct client *client)
{
    unsigned char chunk[IO_CHUNK];

    while (!client->read_closed && !client->read_paused) {
        ssize_t count = recv(client->fd, chunk, sizeof(chunk), 0);

        if (count > 0) {
            if (buffer_append(&client->output, chunk, (size_t)count) < 0) {
                return -1;
            }
            if (buffer_pending(&client->output) >= OUTPUT_HIGH_WATER) {
                client->read_paused = true;
                printf("paused reads fd=%d, pending=%zu\n", client->fd,
                       buffer_pending(&client->output));
            }
            continue;
        }
        if (count == 0) {
            client->read_closed = true;
            return 0;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        return -1;
    }
    return 0;
}

static int flush_client(struct client *client)
{
    while (buffer_pending(&client->output) > 0) {
        size_t pending = buffer_pending(&client->output);
        ssize_t count = send(client->fd, client->output.data + client->output.start,
                             pending, MSG_NOSIGNAL);

        if (count > 0) {
            client->output.start += (size_t)count;
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        }
        if (count == 0) {
            errno = EPIPE;
        }
        return -1;
    }

    if (buffer_pending(&client->output) == 0) {
        client->output.start = 0;
        client->output.end = 0;
    }
    if (client->read_paused &&
        buffer_pending(&client->output) <= OUTPUT_LOW_WATER) {
        client->read_paused = false;
        printf("resumed reads fd=%d, pending=%zu\n", client->fd,
               buffer_pending(&client->output));
    }
    return 0;
}

/* Returns 1 when the client should be destroyed, 0 when it stays registered. */
static int handle_client_event(struct server *server,
                               struct client *client,
                               uint32_t events)
{
    if ((events & EPOLLERR) != 0) {
        int socket_error = 0;
        socklen_t length = sizeof(socket_error);

        if (getsockopt(client->fd, SOL_SOCKET, SO_ERROR,
                       &socket_error, &length) == 0 && socket_error != 0) {
            fprintf(stderr, "socket fd=%d: %s\n", client->fd,
                    strerror(socket_error));
        }
        return 1;
    }

    if (!client->read_closed &&
        (events & (EPOLLIN | EPOLLRDHUP | EPOLLHUP)) != 0 &&
        read_client(client) < 0) {
        fprintf(stderr, "read fd=%d: %s\n", client->fd, strerror(errno));
        return 1;
    }

    /* A read may have queued output even if this event did not contain EPOLLOUT. */
    if (buffer_pending(&client->output) > 0 && flush_client(client) < 0) {
        fprintf(stderr, "write fd=%d: %s\n", client->fd, strerror(errno));
        return 1;
    }

    if (client->read_closed && buffer_pending(&client->output) == 0) {
        return 1;
    }
    if (update_client_interest(server, client) < 0) {
        fprintf(stderr, "epoll modify fd=%d: %s\n", client->fd,
                strerror(errno));
        return 1;
    }
    return 0;
}

static void start_shutdown(struct server *server)
{
    if (server->stopping) {
        return;
    }
    server->stopping = true;
    if (clock_gettime(CLOCK_MONOTONIC, &server->shutdown_deadline) < 0) {
        server->shutdown_deadline.tv_sec = 0;
        server->shutdown_deadline.tv_nsec = 0;
    } else {
        server->shutdown_deadline.tv_sec += SHUTDOWN_GRACE_MS / 1000;
        server->shutdown_deadline.tv_nsec +=
            (long)(SHUTDOWN_GRACE_MS % 1000) * 1000000L;
        if (server->shutdown_deadline.tv_nsec >= 1000000000L) {
            server->shutdown_deadline.tv_sec += 1;
            server->shutdown_deadline.tv_nsec -= 1000000000L;
        }
    }

    if (server->listen_fd >= 0) {
        epoll_ctl(server->epoll_fd, EPOLL_CTL_DEL, server->listen_fd, NULL);
        close(server->listen_fd);
        server->listen_fd = -1;
    }
    puts("shutdown requested; no new clients, grace period is 3 seconds");
}

static int remaining_shutdown_ms(const struct server *server)
{
    struct timespec now;
    int64_t nanoseconds;
    int64_t milliseconds;

    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) {
        return 0;
    }
    nanoseconds = (int64_t)(server->shutdown_deadline.tv_sec - now.tv_sec) *
                      1000000000LL +
                  (server->shutdown_deadline.tv_nsec - now.tv_nsec);
    if (nanoseconds <= 0) {
        return 0;
    }
    milliseconds = (nanoseconds + 999999LL) / 1000000LL;
    return milliseconds > INT_MAX ? INT_MAX : (int)milliseconds;
}

static int consume_signals(struct server *server)
{
    for (;;) {
        struct signalfd_siginfo info;
        ssize_t count = read(server->signal_fd, &info, sizeof(info));

        if (count == (ssize_t)sizeof(info)) {
            if (info.ssi_signo == SIGINT || info.ssi_signo == SIGTERM) {
                start_shutdown(server);
            }
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return 0;
        }
        errno = EIO;
        return -1;
    }
}

static void destroy_all_clients(struct server *server)
{
    while (server->clients != NULL) {
        destroy_client(server, server->clients);
    }
}

static int run_server(struct server *server)
{
    struct epoll_event ready[MAX_EVENTS];

    for (;;) {
        int timeout = -1;
        int count;

        if (server->stopping) {
            if (server->clients == NULL) {
                return 0;
            }
            timeout = remaining_shutdown_ms(server);
            if (timeout == 0) {
                fputs("shutdown grace period expired\n", stderr);
                return 0;
            }
        }

        count = epoll_wait(server->epoll_fd, ready, MAX_EVENTS, timeout);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (count == 0 && server->stopping) {
            fputs("shutdown grace period expired\n", stderr);
            return 0;
        }

        for (int index = 0; index < count; ++index) {
            void *tag = ready[index].data.ptr;

            if (tag == &listen_token) {
                if (!server->stopping && accept_clients(server) < 0) {
                    fprintf(stderr, "accept: %s\n", strerror(errno));
                }
            } else if (tag == &signal_token) {
                if (consume_signals(server) < 0) {
                    return -1;
                }
            } else {
                struct client *client = tag;

                if (handle_client_event(server, client, ready[index].events) != 0) {
                    destroy_client(server, client);
                }
            }
        }
    }
}

int main(int argc, char *argv[])
{
    struct server server = {
        .epoll_fd = -1,
        .listen_fd = -1,
        .signal_fd = -1,
    };
    uint16_t port = DEFAULT_PORT;
    int exit_code = EXIT_FAILURE;

    if (argc > 2 || (argc == 2 && parse_port(argv[1], &port) < 0)) {
        fprintf(stderr, "usage: %s [PORT]\n", argv[0]);
        return EXIT_FAILURE;
    }

    server.signal_fd = create_signal_fd();
    if (server.signal_fd < 0) {
        fprintf(stderr, "signalfd: %s\n", strerror(errno));
        goto cleanup;
    }
    server.listen_fd = create_listener(port);
    if (server.listen_fd < 0) {
        fprintf(stderr, "listen socket: %s\n", strerror(errno));
        goto cleanup;
    }
    server.epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (server.epoll_fd < 0) {
        fprintf(stderr, "epoll_create1: %s\n", strerror(errno));
        goto cleanup;
    }
    if (add_special_fd(server.epoll_fd, server.listen_fd, &listen_token) < 0 ||
        add_special_fd(server.epoll_fd, server.signal_fd, &signal_token) < 0) {
        fprintf(stderr, "epoll_ctl: %s\n", strerror(errno));
        goto cleanup;
    }

    printf("echo server listening on 127.0.0.1:%u\n", (unsigned int)port);
    if (run_server(&server) < 0) {
        fprintf(stderr, "event loop: %s\n", strerror(errno));
        goto cleanup;
    }
    exit_code = EXIT_SUCCESS;

cleanup:
    destroy_all_clients(&server);
    if (server.listen_fd >= 0) {
        close(server.listen_fd);
    }
    if (server.signal_fd >= 0) {
        close(server.signal_fd);
    }
    if (server.epoll_fd >= 0) {
        close(server.epoll_fd);
    }
    return exit_code;
}
