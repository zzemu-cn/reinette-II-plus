#ifndef BYTE_TYPES_H_
#define BYTE_TYPES_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t byte;
typedef int8_t sbyte;
typedef uint16_t word;
typedef int16_t sword;
typedef uint32_t dword;
typedef int32_t sdword;

typedef unsigned int uint_t;
typedef unsigned long ulong_t;
typedef unsigned short ushort_t;


typedef int _BOOL;
#define _TRUE 1
#define _FALSE 0

#ifdef __cplusplus
}
#endif

#endif	// BYTE_TYPES_H_
