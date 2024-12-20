#include "fs.h"
#include "cmd.h"
#include "file.h"
#include "stdio.h"
#include "stdint.h"
#include "global.h"
#include "assert.h"
#include "string.h"
#include "syscall.h"

#include "shell.h"


#define cmd_len 128     // 最大支持键入 128 个字符的命令行输入
#define MAX_ARG_NR 16   // 加上命令名外, 最多支持 15 个参数
#define CWD_CACHE_LEN 64


char cwd_cache[CWD_CACHE_LEN] = {0};    // 当前目录的缓存
char final_path[MAX_PATH_LEN] = {0};    // 用于洗路径时的缓冲
static char cmd_line[cmd_len] = {0};    // 存储输入的命令


void print_prompt(void) {
    printf("[konas @ TinyOS in %s] $ ", cwd_cache);
}


static void readline(char* buf, int32_t count) {
    assert(buf != NULL && count > 0);
    char* pos = buf;
    while (read(stdin_no, pos, 1) != -1 && (pos - buf) < count) {
        switch (*pos) {
        case '\n':
            __attribute__((fallthrough));
        case '\r':
            *pos = 0;
            putchar('\n');
            return;

        case '\b':
            if (buf[0] != '\b') {
                --pos;
                putchar('\b');
            }
            break;
        
        // ctrl+u 清掉输入
        case 'u' - 'a':
            while (buf != pos) {
                putchar('\b');
                *(pos--) = 0;
            }
            break;

        // ctrl+l 屏幕清空
        case 'l' - 'a':
            *pos = 0;
            clear();
            print_prompt();
            printf("%s", buf);
            break;

        // 非控制键则输出字符
        default:
            putchar(*pos);
            pos++;
        }
    }
    printf("readline: can't find enter_key in the cmd_line, max num of char is 128\n");
}


static int32_t cmd_parse(char* cmd_str, char** argv, char token) {
    assert(cmd_str != NULL);
    int32_t arg_idx = 0;
    while (arg_idx < MAX_ARG_NR) {
        argv[arg_idx] = NULL;
        arg_idx++;
    }
    char* next = cmd_str;
    int32_t argc = 0;

    // 外层循环处理整个命令行
    while(*next) {
        // 去除命令字或参数之间的空格
        while(*next == token) {
            next++;
        }
        // 处理最后一个参数后接空格的情况, 如"ls dir2 "
        if (*next == 0) {
            break; 
        }
        argv[argc] = next;

        // 内层循环处理命令行中的每个命令字及参数
        while (*next && *next != token) {   // 在字符串结束前找单词分隔符
            next++;
        }

        // 如果未结束(是 token 字符), 使 tocken 变成 0
        if (*next) {
            // 将 token 字符替换为字符串结束符 0, 做为一个单词的结束, 并将字符指针 next 指向下一个字符
            *next++ = 0;
        }
    
        // 避免 argv 数组访问越界, 参数过多则返回 0
        if (argc > MAX_ARG_NR) {
            return -1;
        }
        argc++;
    }
    return argc;
}


static void cmd_execute(uint32_t argc, char **argv) {
    if (!strcmp("ls", argv[0])) {
        buildin_ls(argc, argv);
    }
    else if (!strcmp("cd", argv[0])) {
        if (buildin_cd(argc, argv) != NULL) {
            memset(cwd_cache, 0, CWD_CACHE_LEN);
            strcpy(cwd_cache, final_path);
        }
    }
    else if (!strcmp("pwd", argv[0])) {
        buildin_pwd(argc, argv);
    }
    else if (!strcmp("ps", argv[0])) {
        buildin_ps(argc, argv);
    }
    else if (!strcmp("clear", argv[0])) {
        buildin_clear(argc, argv);
        return;
    }
    else if (!strcmp("mkdir", argv[0])){
        buildin_mkdir(argc, argv);
    }
    else if (!strcmp("rmdir", argv[0])){
        buildin_rmdir(argc, argv);
    }
    else if (!strcmp("help", argv[0])) {
        help();
    }
    else if (!strcmp("rm", argv[0])) {
        buildin_rm(argc, argv);
    }
    else if (!strcmp("cat", argv[0])) {
        buildin_cat(argc, argv);
    }
    // 如果是外部命令, 需要从磁盘上加载
    else {
        int32_t pid = fork();
        if (pid) {  // parent
            int32_t status;
            int32_t child_pid = wait(&status);
            if (unlikely( child_pid == -1 )) {
                panic("my_shell: no child\n");
            }
            printf("child_pid %d, it's status: %d\n", child_pid, status);
        }
        else {      // child
            make_clear_abs_path(argv[0], final_path);
            argv[0] = final_path;
            stat _stat;
            memset(&_stat, 0, sizeof(stat));
            if (file_stat(argv[0], &_stat) == -1) {
                printf("my_shell: cannot access %s: No such file or directory\n", argv[0]);
                exit(-1);
            }
            else if (_stat.st_filetype != FT_REGULAR) {
                printf("my_shell: %s is not a regular file\n", argv[0]);
                exit(-1);
            }
            else {
                execv(argv[0], argv);
            }
        }
    }
    printf("\n");
}


int32_t argc = -1;
char *argv[MAX_ARG_NR] = {0};


void my_shell(void) {
    cwd_cache[0] = '/';
    while (1) {
        print_prompt(); 
        memset(cmd_line, 0, cmd_len);
        memset(final_path, 0, MAX_PATH_LEN);
        readline(cmd_line, cmd_len);

        if (cmd_line[0] == 0) {
            continue;
        }

        // 针对管道的处理
        char *pipe_symbol = strchr(cmd_line, '|');
        if (pipe_symbol) {
            // 生成管道
            int32_t fd[2] = {-1};
            pipe(fd);
            fd_redirect(1, fd[1]);  // 将标准输出重定向到 fd[1]

            char *each_cmd = cmd_line;
            pipe_symbol = strchr(each_cmd, '|');
            *pipe_symbol = 0;

            // 执行第一个命令, 命令的输出会写入环形缓冲区
            argc = -1;
            argc = cmd_parse(each_cmd, argv, ' ');
            cmd_execute(argc, argv);

            each_cmd = pipe_symbol + 1;
            fd_redirect(0, fd[0]);  // 将标准输入重定向到 fd[0]

            while ((pipe_symbol = strchr(each_cmd, '|'))) {
                *pipe_symbol = 0;
                argc = -1;
                argc = cmd_parse(each_cmd, argv, ' ');
                cmd_execute(argc, argv);
                each_cmd = pipe_symbol + 1;
            }

            fd_redirect(1, 1);  // 将标准输出恢复屏幕

            // 执行最后一个命令
            argc = -1;
            argc = cmd_parse(each_cmd, argv, ' ');
            cmd_execute(argc, argv);

            fd_redirect(0, 0);  // 将标准输入恢复为键盘

            // close pipe
            close(fd[0]);
            close(fd[1]);
        }
        else {
            argc = -1;
            argc = cmd_parse(cmd_line, argv, ' ');
            if (argc == -1) {
                printf("num of arguments exceed %d\n", MAX_ARG_NR);
                continue;
            }
            cmd_execute(argc, argv);
        }

        for (uint32_t i = 0; i < MAX_ARG_NR; ++i) {
            argv[i] = NULL;
        }

    }
    panic("my_shell: should not be here");
}
