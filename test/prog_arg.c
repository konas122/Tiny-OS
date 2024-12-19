#include "stdio.h"
#include "string.h"
#include "syscall.h"


int main(int argc, char** argv) {
    int arg_idx = 0;
    while(arg_idx < argc) {
        printf("argv[%d] is %s\n", arg_idx, argv[arg_idx]);
        arg_idx++;
    }

    if (argc == 1) {
        printf("argc is %d\n, please enter more arg\n", argc);
        return -1;
    }

    int pid = fork();
    if (pid) {
        int delay = 900000;
        while(delay--);
        printf("\nI'm father prog, my pid: %d, I will show process list\n", getpid()); 
        ps();

        int ret;
        int pid = wait(&ret);
        printf("child %d return: %d\n", pid, ret);
    }
    else {
        char abs_path[512] = {0};
        printf("\nI'm child prog, my pid: %d, I will exec %s right now\n", getpid(), argv[1]); 
        if (argv[1][0] != '/') {
            getcwd(abs_path, 512);
            strcat(abs_path, "/");
            strcat(abs_path, argv[1]);
        }
    }
    return 0;
}
