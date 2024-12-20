#ifndef __LIB_STDIO_H__
#define __LIB_STDIO_H__

#include "stdint.h"

typedef char *va_list;


uint32_t atoi(const char *str);
uint32_t printf(const char *str, ...);
uint32_t sprintf(char *buf, const char *format, ...);
uint32_t vsprintf(char *str, const char *format, va_list ap);

#endif
