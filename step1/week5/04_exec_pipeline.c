#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int wait_for_child(pid_t child, int *status)
{
    while (waitpid(child, status, 0) < 0) {
        if (errno != EINTR) {
            return -1;
        }
    }
    return 0;
}

static int child_succeeded(int status)
{
    return WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS;
}

int main(void)
{
    int pipefd[2];
    pid_t producer;
    pid_t consumer;
    int producer_status;
    int consumer_status;

    if (pipe(pipefd) < 0) {
        fprintf(stderr, "pipe: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    producer = fork();
    if (producer < 0) {
        fprintf(stderr, "fork producer: %s\n", strerror(errno));
        close(pipefd[0]);
        close(pipefd[1]);
        return EXIT_FAILURE;
    }
    if (producer == 0) {
        close(pipefd[0]);
        if (dup2(pipefd[1], STDOUT_FILENO) < 0) {
            _exit(126);
        }
        close(pipefd[1]);

        execlp("ls", "ls", "-1", (char *)NULL);
        _exit(127);
    }

    consumer = fork();
    if (consumer < 0) {
        fprintf(stderr, "fork consumer: %s\n", strerror(errno));
        close(pipefd[0]);
        close(pipefd[1]);
        wait_for_child(producer, &producer_status);
        return EXIT_FAILURE;
    }
    if (consumer == 0) {
        close(pipefd[1]);
        if (dup2(pipefd[0], STDIN_FILENO) < 0) {
            _exit(126);
        }
        close(pipefd[0]);

        execlp("wc", "wc", "-l", (char *)NULL);
        _exit(127);
    }

    /* shell 自己不参与传输，必须关闭两端。 */
    close(pipefd[0]);
    close(pipefd[1]);

    if (wait_for_child(producer, &producer_status) < 0 ||
        wait_for_child(consumer, &consumer_status) < 0) {
        fprintf(stderr, "waitpid: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    if (!child_succeeded(producer_status) || !child_succeeded(consumer_status)) {
        fputs("pipeline command failed\n", stderr);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
