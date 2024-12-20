#include "fs.h"
#include "dir.h"
#include "shell.h"
#include "stdio.h"
#include "string.h"
#include "global.h"
#include "assert.h"
#include "syscall.h"

#include "cmd.h"


// 将路径 old_abs_path 中的 .. 和 . 转换为实际路径后存入 new_abs_path
static void wash_path(char* old_abs_path, char* new_abs_path) {
    assert(old_abs_path[0] == '/');
    char name[MAX_FILE_NAME_LEN] = {0};
    char* sub_path = old_abs_path;
    sub_path = path_parse(sub_path, name);

    if (name[0] == 0) {
        new_abs_path[0] = '/';
        new_abs_path[1] = 0;
        return;
    }

    new_abs_path[0] = 0;
    strcat(new_abs_path, "/");
    while (name[0]) {
        if (!strcmp("..", name)) {
            char* slash_ptr =  strrchr(new_abs_path, '/');
            if (slash_ptr != new_abs_path) {
                *slash_ptr = 0;
            }
            else {
                *(slash_ptr + 1) = 0;
            }
        }
        else if (strcmp(".", name)) {
            if (strcmp(new_abs_path, "/")) {
                strcat(new_abs_path, "/");
            }
            strcat(new_abs_path, name);
        }

        // 继续遍历下一层路径
        memset(name, 0, MAX_FILE_NAME_LEN);
        if (sub_path) {
            sub_path = path_parse(sub_path, name);
        }
    }
}


void make_clear_abs_path(char* path, char* final_path) {
    char abs_path[MAX_PATH_LEN] = {0};

    // 先判断是否输入的是绝对路径
    if (path[0] != '/') {
        memset(abs_path, 0, MAX_PATH_LEN);
        if (getcwd(abs_path, MAX_PATH_LEN) != NULL) {
            if (!((abs_path[0] == '/') && (abs_path[1] == 0))) {
                strcat(abs_path, "/");
            }
        }
    }

    strcat(abs_path, path);
    wash_path(abs_path, final_path);
}


void buildin_pwd(uint32_t argc, char** argv UNUSED) {
    if (argc != 1) {
        printf("pwd: no argument support!\n");
        return;
    }
    else {
        if (NULL != getcwd(final_path, MAX_PATH_LEN)) {
            printf("%s\n", final_path); 
        }
        else {
            printf("pwd: get current work directory failed.\n");
        }
    }
}


char* buildin_cd(uint32_t argc, char** argv) {
    if (argc > 2) {
        printf("cd: only support 1 argument!\n");
        return NULL;
    }

    if (argc == 1) {
        final_path[0] = '/';
        final_path[1] = 0;
    }
    else {
        make_clear_abs_path(argv[1], final_path);
    }

    if (chdir(final_path) == -1) {
        printf("cd: no such directory %s\n", final_path);
        return NULL;
    }
    return final_path;
}


void buildin_ls(uint32_t argc, char** argv) {
    char* pathname = NULL;
    stat _stat;
    memset(&_stat, 0, sizeof(stat));
    bool long_info = false;
    uint32_t arg_path_nr = 0;
    uint32_t arg_idx = 1;   // 跨过argv[0], argv[0] 是字符串 "ls"
    while (arg_idx < argc) {
        if (argv[arg_idx][0] == '-') {  // 如果是选项, 单词的首字符是 -
            if (!strcmp("-l", argv[arg_idx])) {         // 如果是参数 -l
                long_info = true;
            }
            else if (!strcmp("-h", argv[arg_idx])) {  // 参数 -h
                printf("usage: -l list all infomation about the file.\n-h for help\nlist all files in the current dirctory if no option\n"); 
                return;
            }
            else {
                printf("ls: invalid option %s\nTry `ls -h` for more information.\n", argv[arg_idx]);
                return;
            }
        }
        else {  // ls 的路径参数
            if (arg_path_nr == 0) {
                pathname = argv[arg_idx];
                arg_path_nr = 1;
            }
            else {
                printf("ls: only support one path\n");
                return;
            }
        }
        arg_idx++;
    }

    if (pathname == NULL) { // 若只输入了 ls 或 ls -l, 没有输入操作路径, 默认以当前路径的绝对路径为参数.
        if (NULL != getcwd(final_path, MAX_PATH_LEN)) {
            pathname = final_path;
        }
        else {
            printf("ls: getcwd for default path failed\n");
            return;
        }
    }
    else {
        make_clear_abs_path(pathname, final_path);
        pathname = final_path;
    }

    if (file_stat(pathname, &_stat) == -1) {
        printf("ls: cannot access %s: No such file or directory\n", pathname);
        return;
    }
    if (_stat.st_filetype == FT_DIRECTORY) {
        dir* dir = opendir(pathname);
        dir_entry* dir_e = NULL;
        char sub_pathname[MAX_PATH_LEN] = {0};
        uint32_t pathname_len = strlen(pathname);
        uint32_t last_char_idx = pathname_len - 1;
        memcpy(sub_pathname, pathname, pathname_len);
        if (sub_pathname[last_char_idx] != '/') {
            sub_pathname[pathname_len] = '/';
            pathname_len++;
        }
        rewinddir(dir);
        if (long_info) {
            char ftype;
            printf("total: %d\n", _stat.st_size);
            while((dir_e = readdir(dir))) {
                ftype = 'd';
                if (dir_e->f_type == FT_REGULAR) {
                    ftype = '-';
                } 
                sub_pathname[pathname_len] = 0;
                strcat(sub_pathname, dir_e->filename);
                memset(&_stat, 0, sizeof(stat));
                if (file_stat(sub_pathname, &_stat) == -1) {
                    printf("ls: cannot access %s: No such file or directory\n", dir_e->filename);
                    return;
                }
                printf("%c  %d  %d  %s\n", ftype, dir_e->i_no, _stat.st_size, dir_e->filename);
            }
        }
        else {
            while((dir_e = readdir(dir))) {
                printf("%s ", dir_e->filename);
            }
            printf("\n");
        }
        closedir(dir);
    }
    else {
        if (long_info) {
            printf("-  %d  %d  %s\n", _stat.st_ino, _stat.st_size, pathname);
        }
        else {
            printf("%s\n", pathname);
        }
    }
}


void buildin_ps(uint32_t argc, char** argv UNUSED) {
    if (argc != 1) {
        printf("ps: no argument support!\n");
        return;
    }
    ps();
}


void buildin_clear(uint32_t argc, char** argv UNUSED) {
    if (argc != 1) {
        printf("clear: no argument support!\n");
        return;
    }
    clear();
}


int32_t buildin_mkdir(uint32_t argc, char** argv) {
    int32_t ret = -1;
    if (argc != 2) {
        printf("mkdir: only support 1 argument!\n");
    }
    else {
        make_clear_abs_path(argv[1], final_path);
        /* 若创建的不是根目录 */
        if (strcmp("/", final_path)) {
            if (mkdir(final_path) == 0) {
                ret = 0;
            }
            else {
                printf("mkdir: create directory %s failed.\n", argv[1]);
            }
        }
    }
    return ret;
}


int32_t buildin_rmdir(uint32_t argc, char** argv) {
    int32_t ret = -1;
    if (argc != 2) {
        printf("rmdir: only support 1 argument!\n");
    }
    else {
        make_clear_abs_path(argv[1], final_path);

        if (strcmp("/", final_path)) {
            if (rmdir(final_path) == 0) {
                ret = 0;
            }
            else {
                printf("rmdir: remove %s failed.\n", argv[1]);
            }
        }
    }
    return ret;
}


int32_t buildin_rm(uint32_t argc, char** argv) {
    int32_t ret = -1;
    if (argc != 2) {
        printf("rm: only support 1 argument!\n");
    }
    else {
        make_clear_abs_path(argv[1], final_path);

        if (strcmp("/", final_path)) {
            if (unlink(final_path) == 0) {
                ret = 0;
            }
            else {
                printf("rm: delete %s failed.\n", argv[1]);
            }
        }
    }
    return ret;
}


int32_t buildin_cat(uint32_t argc, char** argv) {
    if (argc > 2 || argc == 1) {
        printf("cat: argument error\n");
        return -1;
    }

    int buf_size = 1024;
    char abs_path[512] = {0};
    void *buf = malloc(buf_size);
    if (buf == NULL) {
        printf("cat: malloc memory failed\n");
        return -1;
    }

    if (argv[1][0] != '/') {
        getcwd(abs_path, 512);
        strcat(abs_path, "/");
        strcat(abs_path, argv[1]);
    }
    else {
        strcpy(abs_path, argv[1]);
    }

    int fd = open(abs_path, O_RDONLY);
    if (fd == -1) { 
        printf("cat: open: open %s failed\n", argv[1]);
        return -1;
    }

    int read_bytes= 0;
    while (1) {
        read_bytes = read(fd, buf, buf_size);
        if (read_bytes == -1) {
            break;
        }
        write(1, buf, read_bytes);
    }

    free(buf);
    close(fd);

    return 0;
}


int32_t buildin_ldprog(uint32_t argc, char **argv) {
    if (argc > 3 || argc == 1) {
        printf("ldprog: argument error\n");
        return -1;
    }

    if (getcwd(final_path, MAX_PATH_LEN) == NULL) {
        printf("ldprog: get current work directory failed.\n");
        return -1;
    }
    if (strlen(final_path) >= (MAX_PATH_LEN - strlen(argv[1]) - 1)) {
        printf("ldprog: the length of `abs path_name` is too long.\n");
        return -1;
    }

    strcat(final_path, "/");
    strcat(final_path, argv[1]);

    if (argc == 2) {
        return ldprog(final_path, 0);
    }
    else {
        uint32_t num = atoi(argv[2]);
        return ldprog(final_path, num);
    }

    return 0;
}
