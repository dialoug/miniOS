#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

enum {
    LISTEN_BACKLOG = 64,
    MAX_EVENTS = 64,
    MAX_REQUEST_LENGTH = 64,
    MAX_RESPONSE_LENGTH = 128
};

struct client {
    int fd;
    char input[MAX_REQUEST_LENGTH];
    size_t input_length;
    char output[MAX_RESPONSE_LENGTH];
    size_t output_offset;
    size_t output_length;
    bool reading;
    bool peer_closed;
    struct client *next;
};

struct server {
    int epoll_fd;
    int listen_fd;
    int signal_fd;
    unsigned long long counter;
    bool stopping;
    struct client *clients;
};

static char listen_token;
static char signal_token;

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
    listen_fd = socket(AF_UNIX,
                       SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
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

static int create_signal_fd(void)
{
    sigset_t mask;

    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0) {
        return -1;
    }
    return signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
}

static int add_special_fd(int epoll_fd, int fd, void *token)
{
    struct epoll_event event = {
        .events = EPOLLIN,
        .data.ptr = token,
    };

    return epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event);
}

static int update_client_interest(struct server *server,
                                  struct client *client)
{
    struct epoll_event event = {
        .events = EPOLLRDHUP,
        .data.ptr = client,
    };

    if (client->reading) {
        event.events |= EPOLLIN;
    }
    if (client->output_offset < client->output_length) {
        event.events |= EPOLLOUT;
    }
    return epoll_ctl(server->epoll_fd, EPOLL_CTL_MOD, client->fd, &event);
}

static void destroy_client(struct server *server, struct client *client)
{
    struct client **link = &server->clients;

    while (*link != NULL && *link != client) {
        link = &(*link)->next;
    }
    if (*link == client) {
        *link = client->next;
    }
    epoll_ctl(server->epoll_fd, EPOLL_CTL_DEL, client->fd, NULL);
    close(client->fd);
    free(client);
}

static void destroy_all_clients(struct server *server)
{
    while (server->clients != NULL) {
        destroy_client(server, server->clients);
    }
}

static int add_client(struct server *server, int client_fd)
{
    struct client *client = calloc(1, sizeof(*client));
    struct epoll_event event;

    if (client == NULL) {
        return -1;
    }
    client->fd = client_fd;
    client->reading = true;
    client->next = server->clients;

    memset(&event, 0, sizeof(event));
    event.events = EPOLLIN | EPOLLRDHUP;
    event.data.ptr = client;
    if (epoll_ctl(server->epoll_fd, EPOLL_CTL_ADD, client_fd, &event) < 0) {
        free(client);
        return -1;
    }
    server->clients = client;
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

static void stop_accepting(struct server *server)
{
    if (server->stopping) {
        return;
    }
    server->stopping = true;
    if (server->listen_fd >= 0) {
        epoll_ctl(server->epoll_fd, EPOLL_CTL_DEL, server->listen_fd, NULL);
        close(server->listen_fd);
        server->listen_fd = -1;
    }
}

static int prepare_response(struct server *server, struct client *client,
                            const char *request)
{
    int length;

    client->reading = false;
    client->output_offset = 0;

    if (strcmp(request, "increment") == 0) {
        if (server->counter == ULLONG_MAX) {
            length = snprintf(client->output, sizeof(client->output),
                              "error=counter-overflow\n");
        } else {
            ++server->counter;
            length = snprintf(client->output, sizeof(client->output),
                              "counter=%llu\n", server->counter);
        }
    } else if (strcmp(request, "get") == 0) {
        length = snprintf(client->output, sizeof(client->output),
                          "counter=%llu\n", server->counter);
    } else if (strcmp(request, "quit") == 0) {
        length = snprintf(client->output, sizeof(client->output),
                          "counter=%llu; shutting-down\n", server->counter);
        stop_accepting(server);
    } else {
        length = snprintf(client->output, sizeof(client->output),
                          "error=unknown-command\n");
    }

    if (length < 0 || (size_t)length >= sizeof(client->output)) {
        errno = EOVERFLOW;
        return -1;
    }
    client->output_length = (size_t)length;
    return 0;
}

static int prepare_invalid_response(struct client *client)
{
    static const char response[] = "error=invalid-request\n";

    client->reading = false;
    client->output_offset = 0;
    client->output_length = sizeof(response) - 1;
    memcpy(client->output, response, sizeof(response) - 1);
    return 0;
}

static int read_client(struct server *server, struct client *client)
{
    while (client->reading) {
        size_t capacity = sizeof(client->input) - client->input_length;
        ssize_t count;

        if (capacity == 0) {
            return prepare_invalid_response(client);
        }

        count = read(client->fd, client->input + client->input_length,
                     capacity);
        if (count > 0) {
            char *newline;

            client->input_length += (size_t)count;
            newline = memchr(client->input, '\n', client->input_length);
            if (newline != NULL) {
                *newline = '\0';
                return prepare_response(server, client, client->input);
            }
            continue;
        }
        if (count == 0) {
            client->peer_closed = true;
            return prepare_invalid_response(client);
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
    while (client->output_offset < client->output_length) {
        ssize_t sent = send(client->fd,
                            client->output + client->output_offset,
                            client->output_length - client->output_offset,
                            MSG_NOSIGNAL);

        if (sent > 0) {
            client->output_offset += (size_t)sent;
            continue;
        }
        if (sent < 0 && errno == EINTR) {
            continue;
        }
        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return 0;
        }
        if (sent == 0) {
            errno = EIO;
        }
        return -1;
    }
    return 0;
}

static int handle_client_event(struct server *server, struct client *client,
                               uint32_t events)
{
    if (events & EPOLLERR) {
        return 1;
    }

    if (client->reading &&
        (events & (EPOLLIN | EPOLLRDHUP | EPOLLHUP))) {
        if (read_client(server, client) < 0) {
            return 1;
        }
    }

    if (client->output_offset < client->output_length &&
        ((events & EPOLLOUT) || !client->reading)) {
        if (flush_client(client) < 0) {
            return 1;
        }
    }

    if (!client->reading &&
        client->output_offset == client->output_length) {
        return 1;
    }
    if (client->peer_closed && client->output_length == 0) {
        return 1;
    }
    if (update_client_interest(server, client) < 0) {
        return 1;
    }
    return 0;
}

static int consume_signals(struct server *server)
{
    for (;;) {
        struct signalfd_siginfo information;
        ssize_t count = read(server->signal_fd, &information,
                             sizeof(information));

        if (count == (ssize_t)sizeof(information)) {
            if (information.ssi_signo == SIGINT ||
                information.ssi_signo == SIGTERM) {
                stop_accepting(server);
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

static int run_server(struct server *server)
{
    struct epoll_event ready[MAX_EVENTS];

    for (;;) {
        int count;

        if (server->stopping && server->clients == NULL) {
            return 0;
        }

        count = epoll_wait(server->epoll_fd, ready, MAX_EVENTS, -1);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
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

                if (handle_client_event(server, client,
                                        ready[index].events) != 0) {
                    destroy_client(server, client);
                }
            }
        }
    }
}

int main(int argc, char *argv[])
{
    const char *socket_path;
    struct server server = {
        .epoll_fd = -1,
        .listen_fd = -1,
        .signal_fd = -1,
    };
    int path_created = 0;
    int exit_code = EXIT_FAILURE;

    if (argc != 2) {
        fprintf(stderr, "usage: %s SOCKET_PATH\n", argv[0]);
        return EXIT_FAILURE;
    }
    socket_path = argv[1];

    server.signal_fd = create_signal_fd();
    if (server.signal_fd < 0) {
        fprintf(stderr, "signalfd: %s\n", strerror(errno));
        goto cleanup;
    }
    server.listen_fd = create_listener(socket_path);
    if (server.listen_fd < 0) {
        fprintf(stderr, "listen on %s: %s\n", socket_path, strerror(errno));
        goto cleanup;
    }
    path_created = 1;

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

    printf("epoll Unix socket server listening at %s\n", socket_path);
    fflush(stdout);
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
    if (path_created && unlink(socket_path) < 0 && errno != ENOENT) {
        fprintf(stderr, "unlink socket %s: %s\n", socket_path,
                strerror(errno));
        exit_code = EXIT_FAILURE;
    }
    return exit_code;
}
