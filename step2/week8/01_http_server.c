#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

enum {
        DEFAULT_PORT = 8080,
        MAX_EVENTS = 64,
        MAX_CLIENTS = 256,
        MAX_HEADER_SIZE = 8192,
        MAX_RESPONSE_SIZE = 512,
        FILE_CHUNK_SIZE = 16384,
        SEND_BUDGET = 65536,
        HEADER_TIMEOUT_SECONDS = 10,
        WRITE_IDLE_TIMEOUT_SECONDS = 30,
        FILE_REQUEST = 0
};

enum client_phase {
        READING_HEADERS,
        WRITING_HEADERS,
        WRITING_FILE_BODY
};

struct client {
        int fd;
        int file_fd;
        char request[MAX_HEADER_SIZE + 1];
        size_t request_length;
        char response[MAX_RESPONSE_SIZE];
        size_t response_length;
        size_t response_offset;
        char file_chunk[FILE_CHUNK_SIZE];
        size_t chunk_length;
        size_t chunk_offset;
        off_t file_size;
        off_t file_offset;
        struct timespec deadline;
        enum client_phase phase;
        struct client *next;
};

struct server {
        int epoll_fd;
        int listen_fd;
        int signal_fd;
        int timer_fd;
        int root_fd;
        size_t client_count;
        struct client *clients;
};

static char listen_token;
static char signal_token;
static char timer_token;

static int set_deadline(struct client *client, time_t seconds)
{
        struct timespec now;

        if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) {
                return -1;
        }
        client->deadline = now;
        client->deadline.tv_sec += seconds;
        return 0;
}

static int parse_port(const char *text, uint16_t *port)
{
        char *end;
        unsigned long value;

        errno = 0;
        value = strtoul(text, &end, 10);
        if (errno != 0 || *text == '\0' || *end != '\0' ||
            value == 0 || value > UINT16_MAX) {
                return -1;
        }
        *port = (uint16_t)value;
        return 0;
}

static int create_listener(uint16_t port)
{
        struct sockaddr_in address = {
                .sin_family = AF_INET,
                .sin_port = htons(port),
                .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
        };
        int enabled = 1;
        int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);

        if (fd < 0) {
                return -1;
        }
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enabled,
                       sizeof(enabled)) < 0 ||
            bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0 ||
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
        sigset_t mask;

        sigemptyset(&mask);
        sigaddset(&mask, SIGINT);
        sigaddset(&mask, SIGTERM);
        if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0) {
                return -1;
        }
        return signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
}

static int create_timer_fd(void)
{
        struct itimerspec interval = {
                .it_interval.tv_sec = 1,
                .it_value.tv_sec = 1,
        };
        int fd = timerfd_create(CLOCK_MONOTONIC,
                                TFD_NONBLOCK | TFD_CLOEXEC);

        if (fd < 0) {
                return -1;
        }
        if (timerfd_settime(fd, 0, &interval, NULL) < 0) {
                int saved_errno = errno;

                close(fd);
                errno = saved_errno;
                return -1;
        }
        return fd;
}

static int add_special_fd(int epoll_fd, int fd, void *token)
{
        struct epoll_event event = {
                .events = EPOLLIN,
                .data.ptr = token,
        };

        return epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event);
}

static void destroy_client(struct server *server, struct client *client)
{
        struct client **link = &server->clients;

        while (*link != NULL && *link != client) {
                link = &(*link)->next;
        }
        if (*link == client) {
                *link = client->next;
                --server->client_count;
        }
        epoll_ctl(server->epoll_fd, EPOLL_CTL_DEL, client->fd, NULL);
        close(client->fd);
        if (client->file_fd >= 0) {
                close(client->file_fd);
        }
        free(client);
}

static void destroy_all_clients(struct server *server)
{
        while (server->clients != NULL) {
                destroy_client(server, server->clients);
        }
}

static int add_client(struct server *server, int fd)
{
        struct client *client = calloc(1, sizeof(*client));
        struct epoll_event event = {
                .events = EPOLLIN | EPOLLRDHUP,
        };

        if (client == NULL) {
                return -1;
        }
        client->fd = fd;
        client->file_fd = -1;
        if (set_deadline(client, HEADER_TIMEOUT_SECONDS) < 0) {
                free(client);
                return -1;
        }
        event.data.ptr = client;
        if (epoll_ctl(server->epoll_fd, EPOLL_CTL_ADD, fd, &event) < 0) {
                free(client);
                return -1;
        }
        client->next = server->clients;
        server->clients = client;
        ++server->client_count;
        return 0;
}

static int accept_clients(struct server *server)
{
        for (;;) {
                int fd = accept4(server->listen_fd, NULL, NULL,
                                 SOCK_NONBLOCK | SOCK_CLOEXEC);

                if (fd >= 0) {
                        if (server->client_count == MAX_CLIENTS) {
                                close(fd);
                        } else if (add_client(server, fd) < 0) {
                                int saved_errno = errno;

                                close(fd);
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

static int queue_response(struct client *client, int status)
{
        const char *reason;
        const char *body;
        const char *extra = "";
        int length;

        switch (status) {
        case 200:
                reason = "OK";
                body = "hello\n";
                break;
        case 404:
                reason = "Not Found";
                body = "not found\n";
                break;
        case 405:
                reason = "Method Not Allowed";
                body = "method not allowed\n";
                extra = "Allow: GET\r\n";
                break;
        case 431:
                reason = "Request Header Fields Too Large";
                body = "request headers too large\n";
                break;
        case 500:
                reason = "Internal Server Error";
                body = "internal server error\n";
                break;
        default:
                status = 400;
                reason = "Bad Request";
                body = "bad request\n";
                break;
        }

        length = snprintf(client->response, sizeof(client->response),
                          "HTTP/1.1 %d %s\r\n"
                          "Content-Type: text/plain; charset=utf-8\r\n"
                          "Content-Length: %zu\r\n"
                          "Connection: close\r\n"
                          "%s"
                          "\r\n"
                          "%s",
                          status, reason, strlen(body), extra, body);
        if (length < 0 || (size_t)length >= sizeof(client->response)) {
                errno = EOVERFLOW;
                return -1;
        }
        client->response_length = (size_t)length;
        client->response_offset = 0;
        client->phase = WRITING_HEADERS;
        return 0;
}

static int valid_filename(const char *name)
{
        const unsigned char *cursor = (const unsigned char *)name;

        if (!((*cursor >= 'a' && *cursor <= 'z') ||
              (*cursor >= 'A' && *cursor <= 'Z') ||
              (*cursor >= '0' && *cursor <= '9') || *cursor == '_')) {
                return 0;
        }
        for (; *cursor != '\0'; ++cursor) {
                if (!((*cursor >= 'a' && *cursor <= 'z') ||
                      (*cursor >= 'A' && *cursor <= 'Z') ||
                      (*cursor >= '0' && *cursor <= '9') ||
                      *cursor == '_' || *cursor == '-' || *cursor == '.')) {
                        return 0;
                }
        }
        return 1;
}

static int queue_file_response(struct server *server, struct client *client,
                               const char *filename)
{
        struct stat file_stat;
        int file_fd;
        int length;

        if (server->root_fd < 0) {
                return queue_response(client, 404);
        }
        file_fd = openat(server->root_fd, filename,
                         O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
        if (file_fd < 0) {
                return queue_response(client,
                                      errno == ENOENT || errno == ELOOP ||
                                      errno == ENOTDIR || errno == EACCES
                                          ? 404 : 500);
        }
        if (fstat(file_fd, &file_stat) < 0) {
                close(file_fd);
                return queue_response(client, 500);
        }
        if (!S_ISREG(file_stat.st_mode) || file_stat.st_size < 0) {
                close(file_fd);
                return queue_response(client, 404);
        }

        length = snprintf(client->response, sizeof(client->response),
                          "HTTP/1.1 200 OK\r\n"
                          "Content-Type: application/octet-stream\r\n"
                          "Content-Length: %jd\r\n"
                          "Connection: close\r\n"
                          "\r\n",
                          (intmax_t)file_stat.st_size);
        if (length < 0 || (size_t)length >= sizeof(client->response)) {
                close(file_fd);
                errno = EOVERFLOW;
                return -1;
        }
        client->file_fd = file_fd;
        client->file_size = file_stat.st_size;
        client->file_offset = 0;
        client->chunk_length = 0;
        client->chunk_offset = 0;
        client->response_length = (size_t)length;
        client->response_offset = 0;
        client->phase = WRITING_HEADERS;
        return 0;
}

static int valid_line(const char *line)
{
        for (const unsigned char *cursor = (const unsigned char *)line;
             *cursor != '\0'; ++cursor) {
                if (*cursor == '\r' || *cursor == '\n' || *cursor < 0x20) {
                        return 0;
                }
        }
        return 1;
}

/* The header block has already been bounded and ended with CRLF CRLF. */
static int parse_request(char *request, size_t header_end,
                         const char **filename)
{
        char *line;
        char *line_end;
        char *method;
        char *target;
        char *version;
        char *space;
        int host_count = 0;

        request[header_end] = '\0';
        line_end = strstr(request, "\r\n");
        if (line_end != NULL) {
                *line_end = '\0';
        }
        if (!valid_line(request)) {
                return 400;
        }

        method = request;
        space = strchr(method, ' ');
        if (space == NULL || space == method) {
                return 400;
        }
        *space++ = '\0';
        target = space;
        space = strchr(target, ' ');
        if (space == NULL || space == target) {
                return 400;
        }
        *space++ = '\0';
        version = space;
        if (*version == '\0' || strchr(version, ' ') != NULL ||
            strcmp(version, "HTTP/1.1") != 0 || target[0] != '/') {
                return 400;
        }

        line = line_end == NULL ? NULL : line_end + 2;
        while (line != NULL && *line != '\0') {
                char *colon;
                char *value;

                line_end = strstr(line, "\r\n");
                if (line_end != NULL) {
                        *line_end = '\0';
                }
                if (!valid_line(line)) {
                        return 400;
                }
                colon = strchr(line, ':');
                if (colon == NULL || colon == line || colon[-1] == ' ') {
                        return 400;
                }
                *colon = '\0';
                value = colon + 1;
                while (*value == ' ' || *value == '\t') {
                        ++value;
                }
                if (strcasecmp(line, "Host") == 0) {
                        ++host_count;
                        if (*value == '\0') {
                                return 400;
                        }
                } else if (strcasecmp(line, "Content-Length") == 0 ||
                           strcasecmp(line, "Transfer-Encoding") == 0) {
                        return 400;
                }
                line = line_end == NULL ? NULL : line_end + 2;
        }

        if (host_count != 1) {
                return 400;
        }
        if (strcmp(method, "GET") != 0) {
                return 405;
        }
        if (strcmp(target, "/hello") == 0) {
                return 200;
        }
        if (strncmp(target, "/files/", 7) == 0) {
                *filename = target + 7;
                return valid_filename(*filename) ? FILE_REQUEST : 400;
        }
        return 404;
}

static int read_client(struct server *server, struct client *client)
{
        while (client->phase == READING_HEADERS) {
                ssize_t count;

                if (client->request_length == MAX_HEADER_SIZE) {
                        return queue_response(client, 431);
                }
                count = read(client->fd,
                             client->request + client->request_length,
                             MAX_HEADER_SIZE - client->request_length);
                if (count > 0) {
                        size_t end;

                        client->request_length += (size_t)count;
                        for (end = 0; end + 3 < client->request_length; ++end) {
                                if (memcmp(client->request + end,
                                           "\r\n\r\n", 4) == 0) {
                                        const char *filename = NULL;
                                        int status;

                                        for (size_t index = 0; index < end;
                                             ++index) {
                                                if (client->request[index] == '\0') {
                                                        return queue_response(client, 400);
                                                }
                                        }
                                        status = parse_request(client->request,
                                                               end, &filename);
                                        if (status == FILE_REQUEST) {
                                                return queue_file_response(server,
                                                                           client,
                                                                           filename);
                                        }
                                        return queue_response(client, status);
                                }
                        }
                        continue;
                }
                if (count == 0) {
                        return queue_response(client, 400);
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

static int flush_client(struct client *client, bool *made_progress)
{
        size_t sent_this_event = 0;

        while (client->response_offset < client->response_length) {
                ssize_t sent = send(client->fd,
                                    client->response + client->response_offset,
                                    client->response_length - client->response_offset,
                                    MSG_NOSIGNAL);

                if (sent > 0) {
                        *made_progress = true;
                        client->response_offset += (size_t)sent;
                        sent_this_event += (size_t)sent;
                        if (sent_this_event >= SEND_BUDGET) {
                                return 0;
                        }
                        continue;
                }
                if (sent < 0 && errno == EINTR) {
                        continue;
                }
                if (sent < 0 &&
                    (errno == EAGAIN || errno == EWOULDBLOCK)) {
                        return 0;
                }
                if (sent == 0) {
                        errno = EIO;
                }
                return -1;
        }

        if (client->file_fd < 0) {
                return 0;
        }
        client->phase = WRITING_FILE_BODY;

        while (sent_this_event < SEND_BUDGET) {
                ssize_t count;

                if (client->chunk_offset == client->chunk_length) {
                        off_t remaining = client->file_size - client->file_offset;
                        size_t wanted = remaining < FILE_CHUNK_SIZE
                                                ? (size_t)remaining
                                                : FILE_CHUNK_SIZE;

                        if (wanted == 0) {
                                return 0;
                        }
                        count = pread(client->file_fd, client->file_chunk,
                                      wanted, client->file_offset);
                        if (count > 0) {
                                client->file_offset += count;
                                client->chunk_length = (size_t)count;
                                client->chunk_offset = 0;
                        } else if (count == 0) {
                                errno = EIO;
                                return -1;
                        } else if (errno == EINTR) {
                                continue;
                        } else {
                                return -1;
                        }
                }

                count = send(client->fd,
                             client->file_chunk + client->chunk_offset,
                             client->chunk_length - client->chunk_offset,
                             MSG_NOSIGNAL);
                if (count > 0) {
                        *made_progress = true;
                        client->chunk_offset += (size_t)count;
                        sent_this_event += (size_t)count;
                        continue;
                }
                if (count < 0 && errno == EINTR) {
                        continue;
                }
                if (count < 0 &&
                    (errno == EAGAIN || errno == EWOULDBLOCK)) {
                        return 0;
                }
                if (count == 0) {
                        errno = EIO;
                }
                return -1;
        }
        return 0;
}

static int handle_client_event(struct server *server, struct client *client,
                               uint32_t events)
{
        struct epoll_event interest = {
                .data.ptr = client,
        };
        bool was_reading = client->phase == READING_HEADERS;
        bool made_progress = false;

        if ((events & EPOLLERR) != 0) {
                return 1;
        }
        if (client->phase == READING_HEADERS &&
            (events & (EPOLLIN | EPOLLRDHUP | EPOLLHUP)) != 0 &&
            read_client(server, client) < 0) {
                return 1;
        }
        if (client->phase != READING_HEADERS) {
                if (was_reading &&
                    set_deadline(client, WRITE_IDLE_TIMEOUT_SECONDS) < 0) {
                        return 1;
                }
                if (flush_client(client, &made_progress) < 0) {
                        return 1;
                }
                if (made_progress &&
                    set_deadline(client, WRITE_IDLE_TIMEOUT_SECONDS) < 0) {
                        return 1;
                }
        }
        if (client->phase != READING_HEADERS &&
            client->response_offset == client->response_length &&
            (client->file_fd < 0 ||
             (client->file_offset == client->file_size &&
              client->chunk_offset == client->chunk_length))) {
                return 1;
        }
        interest.events = client->phase == READING_HEADERS
                                  ? EPOLLIN | EPOLLRDHUP : EPOLLOUT;
        if (epoll_ctl(server->epoll_fd, EPOLL_CTL_MOD, client->fd,
                      &interest) < 0) {
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
                                return 1;
                        }
                        continue;
                }
                if (count < 0 && errno == EINTR) {
                        continue;
                }
                if (count < 0 &&
                    (errno == EAGAIN || errno == EWOULDBLOCK)) {
                        return 0;
                }
                errno = EIO;
                return -1;
        }
}

static int consume_timer(struct server *server)
{
        for (;;) {
                uint64_t expirations;
                ssize_t count = read(server->timer_fd, &expirations,
                                     sizeof(expirations));

                if (count == (ssize_t)sizeof(expirations)) {
                        continue;
                }
                if (count < 0 && errno == EINTR) {
                        continue;
                }
                if (count < 0 &&
                    (errno == EAGAIN || errno == EWOULDBLOCK)) {
                        return 0;
                }
                errno = EIO;
                return -1;
        }
}

static int expire_clients(struct server *server)
{
        struct timespec now;
        struct client *client;

        if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) {
                return -1;
        }
        client = server->clients;
        while (client != NULL) {
                struct client *next = client->next;

                if (now.tv_sec > client->deadline.tv_sec ||
                    (now.tv_sec == client->deadline.tv_sec &&
                     now.tv_nsec >= client->deadline.tv_nsec)) {
                        destroy_client(server, client);
                }
                client = next;
        }
        return 0;
}

static int run_server(struct server *server)
{
        struct epoll_event ready[MAX_EVENTS];

        for (;;) {
                int count = epoll_wait(server->epoll_fd, ready,
                                       MAX_EVENTS, -1);
                bool timer_ready = false;

                if (count < 0) {
                        if (errno == EINTR) {
                                continue;
                        }
                        return -1;
                }
                for (int index = 0; index < count; ++index) {
                        void *tag = ready[index].data.ptr;

                        if (tag == &listen_token) {
                                if (accept_clients(server) < 0) {
                                        fprintf(stderr, "accept: %s\n",
                                                strerror(errno));
                                }
                        } else if (tag == &signal_token) {
                                int signal_result = consume_signals(server);

                                if (signal_result != 0) {
                                        return signal_result < 0 ? -1 : 0;
                                }
                        } else if (tag == &timer_token) {
                                if (consume_timer(server) < 0) {
                                        return -1;
                                }
                                timer_ready = true;
                        } else {
                                struct client *client = tag;

                                if (handle_client_event(server, client,
                                                        ready[index].events) != 0) {
                                        destroy_client(server, client);
                                }
                        }
                }
                /* Avoid freeing clients still referenced by this event batch. */
                if (timer_ready && expire_clients(server) < 0) {
                        return -1;
                }
        }
}

int main(int argc, char *argv[])
{
        struct server server = {
                .epoll_fd = -1,
                .listen_fd = -1,
                .signal_fd = -1,
                .timer_fd = -1,
                .root_fd = -1,
        };
        uint16_t port = DEFAULT_PORT;
        int exit_code = EXIT_FAILURE;

        if (argc > 3 ||
            (argc >= 2 && parse_port(argv[1], &port) < 0)) {
                fprintf(stderr, "usage: %s [PORT [FILE_DIR]]\n", argv[0]);
                return EXIT_FAILURE;
        }

        if (argc == 3) {
                server.root_fd = open(argv[2], O_RDONLY | O_DIRECTORY |
                                               O_CLOEXEC | O_NOFOLLOW);
                if (server.root_fd < 0) {
                        fprintf(stderr, "open file directory %s: %s\n",
                                argv[2], strerror(errno));
                        goto cleanup;
                }
        }

        server.signal_fd = create_signal_fd();
        if (server.signal_fd < 0) {
                fprintf(stderr, "signalfd: %s\n", strerror(errno));
                goto cleanup;
        }
        server.timer_fd = create_timer_fd();
        if (server.timer_fd < 0) {
                fprintf(stderr, "timerfd: %s\n", strerror(errno));
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
        if (add_special_fd(server.epoll_fd, server.listen_fd,
                           &listen_token) < 0 ||
            add_special_fd(server.epoll_fd, server.signal_fd,
                           &signal_token) < 0 ||
            add_special_fd(server.epoll_fd, server.timer_fd,
                           &timer_token) < 0) {
                fprintf(stderr, "epoll_ctl: %s\n", strerror(errno));
                goto cleanup;
        }

        printf("HTTP server listening on 127.0.0.1:%u\n",
               (unsigned int)port);
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
        if (server.timer_fd >= 0) {
                close(server.timer_fd);
        }
        if (server.epoll_fd >= 0) {
                close(server.epoll_fd);
        }
        if (server.root_fd >= 0) {
                close(server.root_fd);
        }
        return exit_code;
}
