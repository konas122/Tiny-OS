#ifndef __LIB_STDINT_H__
#define __LIB_STDINT_H__

typedef signed char             int8_t;
typedef signed short int        int16_t;
typedef signed int              int32_t;
typedef signed long long int    int64_t;
typedef unsigned char           uint8_t;
typedef unsigned short int      uint16_t;
typedef unsigned int            uint32_t;
typedef unsigned long long int  uint64_t;


#define UINT8_MIN (uint8_t)(0)
#define UINT16_MIN (uint16_t)(0)
#define UINT32_MIN (uint32_t)(0)
#define UINT64_MIN (uint64_t)(0)

#define UINT8_MAX (uint8_t)(-1)
#define UINT16_MAX (uint16_t)(-1)
#define UINT32_MAX (uint32_t)(-1)
#define UINT64_MAX (uint64_t)(-1)

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))

#endif
