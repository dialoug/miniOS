#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
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
    struct stat file_stat;
    void *mapping = MAP_FAILED;
    int file_fd = -1;
    int exit_code = EXIT_FAILURE;

    if (argc != 2) {
        fprintf(stderr, "usage: %s FILE\n", argv[0]);
        return EXIT_FAILURE;
    }
    path = argv[1];

    file_fd = open(path, O_RDONLY | O_CLOEXEC);
    if (file_fd < 0) {
        fprintf(stderr, "open %s: %s\n", path, strerror(errno));
        goto cleanup;
    }
    if (fstat(file_fd, &file_stat) < 0) {
        fprintf(stderr, "fstat %s: %s\n", path, strerror(errno));
        goto cleanup;
    }
    if (!S_ISREG(file_stat.st_mode)) {
        fprintf(stderr, "%s is not a regular file\n", path);
        goto cleanup;
    }
    if (file_stat.st_size == 0) {
        exit_code = EXIT_SUCCESS;
        goto cleanup;
    }
    if ((uintmax_t)file_stat.st_size > SIZE_MAX) {
        fprintf(stderr, "%s is too large to map in this process\n", path);
        goto cleanup;
    }

    mapping = mmap(NULL, (size_t)file_stat.st_size, PROT_READ, MAP_PRIVATE,
                   file_fd, 0);
    if (mapping == MAP_FAILED) {
        fprintf(stderr, "mmap %s: %s\n", path, strerror(errno));
        goto cleanup;
    }

    if (write_all(STDOUT_FILENO, mapping, (size_t)file_stat.st_size) < 0) {
        fprintf(stderr, "write stdout: %s\n", strerror(errno));
        goto cleanup;
    }
    exit_code = EXIT_SUCCESS;

cleanup:
    if (mapping != MAP_FAILED &&
        munmap(mapping, (size_t)file_stat.st_size) < 0 &&
        exit_code == EXIT_SUCCESS) {
        fprintf(stderr, "munmap %s: %s\n", path, strerror(errno));
        exit_code = EXIT_FAILURE;
    }
    if (file_fd >= 0 && close(file_fd) < 0 && exit_code == EXIT_SUCCESS) {
        fprintf(stderr, "close %s: %s\n", path, strerror(errno));
        exit_code = EXIT_FAILURE;
    }
    return exit_code;
}
