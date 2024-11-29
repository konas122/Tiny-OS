#ifndef __USERPROG_TSS_H__
#define __USERPROG_TSS_H__

#include "thread.h"

void update_tss_esp(task_struct* pthread);
void tss_init(void);

#endif
