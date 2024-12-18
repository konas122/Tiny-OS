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


int32_t argc = -1;
char *argv[MAX_ARG_NR];


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

        argc = -1;
        argc = cmd_parse(cmd_line, argv, ' ');
        if (argc == -1) {
            printf("num of arguments exceed %d\n", MAX_ARG_NR);
            continue;
        }

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
            continue;
        }
        else if (!strcmp("mkdir", argv[0])){
            buildin_mkdir(argc, argv);
        }
        else if (!strcmp("rmdir", argv[0])){
            buildin_rmdir(argc, argv);
        }
        else if (!strcmp("rm", argv[0])) {
            buildin_rm(argc, argv);
        }
        else {
            make_clear_abs_path(argv[0], final_path);
            argv[0] = final_path;
            stat _stat;
            memset(&_stat, 0, sizeof(stat));
            if (file_stat(argv[0], &_stat) == -1) {
                printf("my_shell: cannot access %s: No such file or directory\n", argv[0]);
            }
            else {
                execv(argv[0], argv);
            }
            while(1);
        }

        for (uint32_t i = 0; i < MAX_ARG_NR; ++i) {
            argv[i] = NULL;
        }

        printf("\n");
    }
    panic("my_shell: should not be here");
}
