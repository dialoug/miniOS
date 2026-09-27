#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

enum { MAX_DEPTH = 64 };

static const char *file_type(mode_t mode)
{
    if (S_ISREG(mode)) {
        return "file";
    }
    if (S_ISDIR(mode)) {
        return "directory";
    }
    if (S_ISLNK(mode)) {
        return "symlink";
    }
    if (S_ISCHR(mode)) {
        return "character-device";
    }
    if (S_ISBLK(mode)) {
        return "block-device";
    }
    if (S_ISFIFO(mode)) {
        return "fifo";
    }
    if (S_ISSOCK(mode)) {
        return "socket";
    }
    return "unknown";
}

static void format_mode(mode_t mode, char text[11])
{
    static const mode_t read_bits[] = { S_IRUSR, S_IRGRP, S_IROTH };
    static const mode_t write_bits[] = { S_IWUSR, S_IWGRP, S_IWOTH };
    static const mode_t execute_bits[] = { S_IXUSR, S_IXGRP, S_IXOTH };

    text[0] = S_ISDIR(mode) ? 'd' : S_ISLNK(mode) ? 'l' : '-';
    for (size_t index = 0; index < 3; ++index) {
        text[1 + index * 3] = (mode & read_bits[index]) ? 'r' : '-';
        text[2 + index * 3] = (mode & write_bits[index]) ? 'w' : '-';
        text[3 + index * 3] = (mode & execute_bits[index]) ? 'x' : '-';
    }
    text[10] = '\0';
}

static char *join_path(const char *parent, const char *name)
{
    size_t parent_length = strlen(parent);
    size_t name_length = strlen(name);
    int needs_separator = parent_length > 0 && parent[parent_length - 1] != '/';
    size_t total_length = parent_length + (size_t)needs_separator + name_length + 1;
    char *path = malloc(total_length);

    if (path == NULL) {
        return NULL;
    }
    snprintf(path, total_length, "%s%s%s", parent,
             needs_separator ? "/" : "", name);
    return path;
}

/* Takes ownership of directory_fd and closes it through closedir(). */
static int walk_directory(int directory_fd, const char *display_path,
                          unsigned int depth)
{
    DIR *directory;
    int result = 0;

    if (depth > MAX_DEPTH) {
        fprintf(stderr, "%s: maximum directory depth reached\n", display_path);
        close(directory_fd);
        return 1;
    }

    directory = fdopendir(directory_fd);
    if (directory == NULL) {
        fprintf(stderr, "fdopendir %s: %s\n", display_path, strerror(errno));
        close(directory_fd);
        return 1;
    }

    for (;;) {
        struct dirent *entry;
        struct stat entry_stat;
        char *entry_path;
        int parent_fd;

        errno = 0;
        entry = readdir(directory);
        if (entry == NULL) {
            if (errno != 0) {
                fprintf(stderr, "readdir %s: %s\n", display_path,
                        strerror(errno));
                result = 1;
            }
            break;
        }
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        parent_fd = dirfd(directory);
        if (fstatat(parent_fd, entry->d_name, &entry_stat,
                    AT_SYMLINK_NOFOLLOW) < 0) {
            fprintf(stderr, "fstatat %s/%s: %s\n", display_path,
                    entry->d_name, strerror(errno));
            result = 1;
            continue;
        }

        entry_path = join_path(display_path, entry->d_name);
        if (entry_path == NULL) {
            fprintf(stderr, "malloc path for %s/%s: %s\n", display_path,
                    entry->d_name, strerror(errno));
            result = 1;
            continue;
        }

        {
            char mode_text[11];

            format_mode(entry_stat.st_mode, mode_text);
            printf("%-16s %s inode=%ju size=%jd %s\n",
                   file_type(entry_stat.st_mode), mode_text,
                   (uintmax_t)entry_stat.st_ino, (intmax_t)entry_stat.st_size,
                   entry_path);
        }

        if (S_ISDIR(entry_stat.st_mode)) {
            struct stat opened_stat;
            int child_fd = openat(parent_fd, entry->d_name,
                                  O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);

            if (child_fd < 0) {
                fprintf(stderr, "openat %s: %s\n", entry_path,
                        strerror(errno));
                result = 1;
            } else if (fstat(child_fd, &opened_stat) < 0) {
                fprintf(stderr, "fstat %s: %s\n", entry_path,
                        strerror(errno));
                close(child_fd);
                result = 1;
            } else if (entry_stat.st_dev != opened_stat.st_dev ||
                       entry_stat.st_ino != opened_stat.st_ino) {
                fprintf(stderr, "%s: directory changed during traversal\n",
                        entry_path);
                close(child_fd);
                result = 1;
            } else if (walk_directory(child_fd, entry_path, depth + 1) != 0) {
                result = 1;
            }
        }
        free(entry_path);
    }

    if (closedir(directory) < 0) {
        fprintf(stderr, "closedir %s: %s\n", display_path, strerror(errno));
        result = 1;
    }
    return result;
}

int main(int argc, char *argv[])
{
    const char *root_path = argc == 2 ? argv[1] : ".";
    int root_fd;

    if (argc > 2) {
        fprintf(stderr, "usage: %s [DIRECTORY]\n", argv[0]);
        return EXIT_FAILURE;
    }

    root_fd = open(root_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (root_fd < 0) {
        fprintf(stderr, "open directory %s: %s\n", root_path, strerror(errno));
        return EXIT_FAILURE;
    }

    return walk_directory(root_fd, root_path, 0) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
