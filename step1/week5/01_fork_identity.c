#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void)
{
    int counter = 10;
    pid_t child;
    int status;

    /* 让父、子的输出立即可见，避免 stdio 缓冲使观察顺序变得模糊。 */
    setbuf(stdout, NULL);

    child = fork();
    if (child < 0) {
        fprintf(stderr, "fork: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    if (child == 0) {
        counter += 1;
        printf("child:  pid=%ld, parent=%ld, counter=%d\n",
               (long)getpid(), (long)getppid(), counter);
        _exit(EXIT_SUCCESS);
    }

    printf("parent: pid=%ld, child=%ld, counter=%d\n",
           (long)getpid(), (long)child, counter);

    if (waitpid(child, &status, 0) < 0) {
        fprintf(stderr, "waitpid: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }
    if (!WIFEXITED(status)) {
        fputs("child did not exit normally\n", stderr);
        return EXIT_FAILURE;
    }

    printf("parent: child exit=%d, counter is still %d\n",
           WEXITSTATUS(status), counter);
    return EXIT_SUCCESS;
}
