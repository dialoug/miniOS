#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

enum { BUFFER_SIZE = 4096 };

static int write_all(int fd, const char *buffer, size_t length)
{
    size_t written_total = 0;

    while (written_total < length) {
        ssize_t written = write(fd, buffer + written_total, length - written_total);

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
    const char *source_path;
    const char *destination_path;
    struct stat source_stat;
    struct stat destination_stat;
    char buffer[BUFFER_SIZE];
    int source_fd = -1;
    int destination_fd = -1;
    int exit_code = EXIT_FAILURE;

    if (argc != 3) {
        fprintf(stderr, "usage: %s SOURCE DESTINATION\n", argv[0]);
        return EXIT_FAILURE;
    }
    source_path = argv[1];
    destination_path = argv[2];

    source_fd = open(source_path, O_RDONLY);
    if (source_fd < 0) {
        fprintf(stderr, "open source %s: %s\n", source_path, strerror(errno));
        goto cleanup;
    }
    if (fstat(source_fd, &source_stat) < 0) {
        fprintf(stderr, "fstat source %s: %s\n", source_path, strerror(errno));
        goto cleanup;
    }
    if (!S_ISREG(source_stat.st_mode)) {
        fprintf(stderr, "source %s is not a regular file\n", source_path);
        goto cleanup;
    }

    /* Do not use O_TRUNC yet: DESTINATION might name the source inode. */
    destination_fd = open(destination_path, O_WRONLY | O_CREAT,
                          source_stat.st_mode & 0777);
    if (destination_fd < 0) {
        fprintf(stderr, "open destination %s: %s\n", destination_path,
                strerror(errno));
        goto cleanup;
    }
    if (fstat(destination_fd, &destination_stat) < 0) {
        fprintf(stderr, "fstat destination %s: %s\n", destination_path,
                strerror(errno));
        goto cleanup;
    }
    if (source_stat.st_dev == destination_stat.st_dev &&
        source_stat.st_ino == destination_stat.st_ino) {
        fprintf(stderr, "source and destination are the same file\n");
        goto cleanup;
    }
    if (ftruncate(destination_fd, 0) < 0) {
        fprintf(stderr, "truncate destination %s: %s\n", destination_path,
                strerror(errno));
        goto cleanup;
    }

    for (;;) {
        ssize_t bytes_read = read(source_fd, buffer, sizeof(buffer));

        if (bytes_read > 0) {
            if (write_all(destination_fd, buffer, (size_t)bytes_read) < 0) {
                fprintf(stderr, "write destination %s: %s\n", destination_path,
                        strerror(errno));
                goto cleanup;
            }
            continue;
        }
        if (bytes_read == 0) {
            exit_code = EXIT_SUCCESS;
            break;
        }
        if (errno != EINTR) {
            fprintf(stderr, "read source %s: %s\n", source_path, strerror(errno));
            goto cleanup;
        }
    }

cleanup:
    if (destination_fd >= 0 && close(destination_fd) < 0 && exit_code == EXIT_SUCCESS) {
        fprintf(stderr, "close destination %s: %s\n", destination_path,
                strerror(errno));
        exit_code = EXIT_FAILURE;
    }
    if (source_fd >= 0 && close(source_fd) < 0 && exit_code == EXIT_SUCCESS) {
        fprintf(stderr, "close source %s: %s\n", source_path, strerror(errno));
        exit_code = EXIT_FAILURE;
    }
    return exit_code;
}
