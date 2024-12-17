#ifndef __KERNEL_SHELL_H__
#define __KERNEL_SHELL_H__

#include "fs.h"


void my_shell(void);
void print_prompt(void);

extern char final_path[MAX_PATH_LEN];

#endif
