#include "debug.h"
#include "global.h"
#include "string.h"


void memset(void *dst_, uint8_t value, uint32_t size) {
    ASSERT(dst_ != NULL);
    uint8_t *dst = (uint8_t *)dst_;
    while (size-- > 0) {
        *dst++ = value;
    }
}


void memcpy(void* dst_, const void* src_, uint32_t size) {
    ASSERT(dst_ != NULL && src_ != NULL);
    uint8_t* dst = (uint8_t *)dst_;
    const uint8_t* src = (const uint8_t *)src_;
    while (size-- > 0) {
        *dst++ = *src++;
    }
}


// 若相等则返回0, 若 a_ 大于 b_ 返回 +1, 否则返回 -1
int memcmp(const void* a_, const void* b_, uint32_t size) {
    const char* a = (const char *)a_;
    const char* b = (const char *)b_;
    ASSERT(a != NULL || b != NULL);
    while (size-- > 0) {
        if (*a != *b) {
            return *a > *b ? 1 : -1; 
        }
        a++;
        b++;
    }
    return 0;
}


char* strcpy(char* dst_, const char* src_) {
    ASSERT(dst_ != NULL && src_ != NULL);
    char* r = dst_;
    while ((*dst_++ = *src_++));
    return r;
}


uint32_t strlen(const char* str) {
    ASSERT(str != NULL);
    const char* p = str;
    while (*p++);
    return (p - str - 1);
}


// 若 a_ 中的字符大于 b_ 中的字符返回 1, 相等时返回 0, 否则返回 -1.
int8_t strcmp (const char* a, const char* b) {
    ASSERT(a != NULL && b != NULL);
    while (*a != 0 && *a == *b) {
        a++;
        b++;
    }
    return *a < *b ? -1 : *a > *b;
}


// 从左到右查找字符串 str 中首次出现字符 ch 的地址
char* strchr(const char* str, const uint8_t ch) {
    ASSERT(str != NULL);
    while (*str != 0) {
        if (*str == ch) {
            return (char*)str;
        }
        str++;
    }
    return NULL;
}


// 从后往前查找字符串 str 中首次出现字符 ch 的地址
char* strrchr(const char* str, const uint8_t ch) {
    ASSERT(str != NULL);
    const char* last_char = NULL;
    while (*str != 0) {
        if (*str == ch) {
            last_char = str;
        }
        str++;
    }
    return (char*)last_char;
}


char* strcat(char* dst_, const char* src_) {
    ASSERT(dst_ != NULL && src_ != NULL);
    char *str = dst_;
    while (*str++);
    --str;
    while ((*str++ = *src_++));
    return dst_;
}


// 在字符串 str 中查找指定字符 ch 出现的次数
uint32_t strchrs(const char* str, uint8_t ch) {
    ASSERT(str != NULL);
    uint32_t ch_cnt = 0;
    const char* p = str;
    while (*p != 0) {
        if (*p == ch) {
            ch_cnt++;
        }
        p++;
    }
    return ch_cnt;
}
