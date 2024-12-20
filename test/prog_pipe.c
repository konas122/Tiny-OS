#include "stdio.h"
#include "string.h"
#include "syscall.h"

int main(int argc, char **argv) {
    int32_t fd[2] = {-1};
    pipe(fd);

    int32_t pid = fork();
    if (pid) {
        close(fd[0]);   // 关闭输入
        write(fd[1], "Hi, my son, I love you!", 24);
        printf("\nI'm father, my pid is %d\n", getpid());

        int ret;
        int pid = wait(&ret);
        printf("\nchild %d return: %d\n", pid, ret);

        return 8;
    }
    else {
        close(fd[1]);   // 关闭输出
        char buf[32] = {0};
        read(fd[0], buf, 24);
        printf("\nI'm child, my pid is %d\n", getpid());
        printf("I'm child, my father said to me: \"%s\"\n", buf);
        return 9;
    }

    return 0;
}
