#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void)
{
    pid_t child;
    int status;

    child = fork();
    if (child < 0) {
        fprintf(stderr, "fork: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    if (child == 0) {
        /* 成功时，这个进程的代码和数据会被 sh 完整替换。 */
        execlp("sh", "sh", "-c", "printf 'program after exec, pid=%s\\n' \"$$\"; exit 42", (char *)NULL);

        /* 只有 exec 失败才能走到这里；避免继承的 stdio 缓冲被重复刷新。 */
        fprintf(stderr, "execlp: %s\n", strerror(errno));
        _exit(127);
    }

    if (waitpid(child, &status, 0) < 0) {
        fprintf(stderr, "waitpid: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }
    if (WIFEXITED(status)) {
        printf("parent: child exited with status %d\n", WEXITSTATUS(status));
        return WEXITSTATUS(status) == 42 ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (WIFSIGNALED(status)) {
        printf("parent: child was terminated by signal %d\n", WTERMSIG(status));
        return EXIT_FAILURE;
    }

    fputs("parent: child ended in an unexpected state\n", stderr);
    return EXIT_FAILURE;
}
