#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * 成功时返回 0，并将“由调用者负责 free 的堆内存”写入 *out_line。
 * 失败时返回 errno 风格的正错误码，且 *out_line 保持为 NULL。
 */
static int read_first_line(const char *path, char **out_line)
{
    char buffer[256];
    char *line = NULL;
    FILE *file = NULL;
    size_t length;
    int error = 0;

    if (path == NULL || out_line == NULL) {
        return EINVAL;
    }
    *out_line = NULL;

    file = fopen(path, "r");
    if (file == NULL) {
        return errno;
    }

    if (fgets(buffer, sizeof(buffer), file) == NULL) {
        error = ferror(file) ? errno : ENODATA;
        goto out_close_file;
    }

    length = strlen(buffer);
    line = malloc(length + 1);
    if (line == NULL) {
        error = ENOMEM;
        goto out_close_file;
    }

    memcpy(line, buffer, length + 1);
    *out_line = line;
    line = NULL; /* 所有权已经转移给调用者。 */

out_close_file:
    fclose(file);
    free(line); /* 目前失败路径没有转移所有权，释放局部资源。 */
    return error;
}

int main(int argc, char **argv)
{
    char *line = NULL;
    int error;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <file>\n", argv[0]);
        return EXIT_FAILURE;
    }

    error = read_first_line(argv[1], &line);
    if (error != 0) {
        fprintf(stderr, "Cannot read %s: %s\n", argv[1], strerror(error));
        return EXIT_FAILURE;
    }

    printf("First line: %s", line);
    free(line); /* 调用者取得所有权，因此负责释放。 */
    return EXIT_SUCCESS;
}
