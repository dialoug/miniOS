#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void)
{
    static const char message[] = "hello through a pipe\n";
    int pipefd[2];
    pid_t child;
    int status;

    if (pipe(pipefd) < 0) {
        fprintf(stderr, "pipe: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    child = fork();
    if (child < 0) {
        fprintf(stderr, "fork: %s\n", strerror(errno));
        close(pipefd[0]);
        close(pipefd[1]);
        return EXIT_FAILURE;
    }

    if (child == 0) {
        char buffer[64];
        ssize_t count;

        close(pipefd[1]); /* 子进程只读。 */
        while ((count = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
            if (write(STDOUT_FILENO, buffer, (size_t)count) != count) {
                _exit(EXIT_FAILURE);
            }
        }
        close(pipefd[0]);
        _exit(count < 0 ? EXIT_FAILURE : EXIT_SUCCESS);
    }

    close(pipefd[0]); /* 父进程只写。 */
    if (write(pipefd[1], message, sizeof(message) - 1) !=
        (ssize_t)(sizeof(message) - 1)) {
        fprintf(stderr, "write: %s\n", strerror(errno));
        close(pipefd[1]);
        waitpid(child, NULL, 0);
        return EXIT_FAILURE;
    }
    close(pipefd[1]); /* 关闭最后一个写端，子进程的 read 才会最终返回 0。 */

    if (waitpid(child, &status, 0) < 0) {
        fprintf(stderr, "waitpid: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != EXIT_SUCCESS) {
        fputs("child could not read the pipe\n", stderr);
        return EXIT_FAILURE;
    }

    puts("parent: pipe reader reached EOF and exited");
    return EXIT_SUCCESS;
}
