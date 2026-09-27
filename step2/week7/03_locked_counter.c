#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

enum {
    COUNTER_BUFFER_SIZE = 64,
    MAX_ITERATIONS = 1000000,
    RACE_DELAY_NANOSECONDS = 1000000
};

static int change_lock(int fd, short lock_type, int wait)
{
    struct flock lock = {
        .l_type = lock_type,
        .l_whence = SEEK_SET,
        .l_start = 0,
        .l_len = 0,
    };
    int command = wait ? F_SETLKW : F_SETLK;

    while (fcntl(fd, command, &lock) < 0) {
        if (errno == EINTR) {
            continue;
        }
        return -1;
    }
    return 0;
}

static int read_counter(int fd, long long *value)
{
    char buffer[COUNTER_BUFFER_SIZE];
    size_t used = 0;

    if (lseek(fd, 0, SEEK_SET) < 0) {
        return -1;
    }

    for (;;) {
        ssize_t count = read(fd, buffer + used, sizeof(buffer) - 1 - used);

        if (count > 0) {
            used += (size_t)count;
            if (used == sizeof(buffer) - 1) {
                char extra;
                ssize_t extra_count;

                do {
                    extra_count = read(fd, &extra, 1);
                } while (extra_count < 0 && errno == EINTR);
                if (extra_count != 0) {
                    errno = EOVERFLOW;
                    return -1;
                }
            }
            continue;
        }
        if (count == 0) {
            break;
        }
        if (errno != EINTR) {
            return -1;
        }
    }

    if (used == 0) {
        *value = 0;
        return 0;
    }

    buffer[used] = '\0';
    {
        char *end;
        long long parsed;

        errno = 0;
        parsed = strtoll(buffer, &end, 10);
        if (errno != 0 || end == buffer || parsed < 0) {
            errno = EINVAL;
            return -1;
        }
        while (isspace((unsigned char)*end)) {
            ++end;
        }
        if (*end != '\0') {
            errno = EINVAL;
            return -1;
        }
        *value = parsed;
    }
    return 0;
}

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

static int write_counter(int fd, long long value)
{
    char buffer[COUNTER_BUFFER_SIZE];
    int length = snprintf(buffer, sizeof(buffer), "%lld\n", value);

    if (length < 0 || (size_t)length >= sizeof(buffer)) {
        errno = EOVERFLOW;
        return -1;
    }
    if (ftruncate(fd, 0) < 0 || lseek(fd, 0, SEEK_SET) < 0) {
        return -1;
    }
    return write_all(fd, buffer, (size_t)length);
}

static int parse_iterations(const char *text, unsigned long *iterations)
{
    char *end;
    unsigned long value;

    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' ||
        value == 0 || value > MAX_ITERATIONS) {
        return -1;
    }
    *iterations = value;
    return 0;
}

static void expose_race_window(void)
{
    struct timespec delay = {
        .tv_sec = 0,
        .tv_nsec = RACE_DELAY_NANOSECONDS,
    };

    while (nanosleep(&delay, &delay) < 0 && errno == EINTR) {
    }
}

int main(int argc, char *argv[])
{
    const char *path;
    unsigned long iterations;
    int use_lock = 1;
    int file_fd = -1;
    int lock_held = 0;
    int exit_code = EXIT_FAILURE;

    if (argc == 4 && strcmp(argv[1], "--no-lock") == 0) {
        use_lock = 0;
        path = argv[2];
        if (parse_iterations(argv[3], &iterations) < 0) {
            goto usage;
        }
    } else if (argc == 3) {
        path = argv[1];
        if (parse_iterations(argv[2], &iterations) < 0) {
            goto usage;
        }
    } else {
        goto usage;
    }

    file_fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0666);
    if (file_fd < 0) {
        fprintf(stderr, "open %s: %s\n", path, strerror(errno));
        goto cleanup;
    }

    for (unsigned long index = 0; index < iterations; ++index) {
        long long value;

        if (use_lock) {
            if (change_lock(file_fd, F_WRLCK, 1) < 0) {
                fprintf(stderr, "lock %s: %s\n", path, strerror(errno));
                goto cleanup;
            }
            lock_held = 1;
        }

        if (read_counter(file_fd, &value) < 0) {
            fprintf(stderr, "read counter %s: %s\n", path, strerror(errno));
            goto cleanup;
        }
        if (value == LLONG_MAX) {
            fprintf(stderr, "counter %s has reached LLONG_MAX\n", path);
            goto cleanup;
        }
        if (!use_lock) {
            expose_race_window();
        }
        if (write_counter(file_fd, value + 1) < 0) {
            fprintf(stderr, "write counter %s: %s\n", path, strerror(errno));
            goto cleanup;
        }

        if (lock_held) {
            if (change_lock(file_fd, F_UNLCK, 0) < 0) {
                fprintf(stderr, "unlock %s: %s\n", path, strerror(errno));
                goto cleanup;
            }
            lock_held = 0;
        }
    }
    exit_code = EXIT_SUCCESS;
    goto cleanup;

usage:
    fprintf(stderr, "usage: %s [--no-lock] FILE ITERATIONS\n", argv[0]);

cleanup:
    if (lock_held && change_lock(file_fd, F_UNLCK, 0) < 0) {
        fprintf(stderr, "unlock during cleanup: %s\n", strerror(errno));
    }
    if (file_fd >= 0 && close(file_fd) < 0 && exit_code == EXIT_SUCCESS) {
        fprintf(stderr, "close %s: %s\n", path, strerror(errno));
        exit_code = EXIT_FAILURE;
    }
    return exit_code;
}
