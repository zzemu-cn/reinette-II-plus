#ifndef DBG_H_
#define DBG_H_

#include <stddef.h>
#include <stdio.h>

//# define MSG(X) fprintf(stderr, "[%s:%s:%d]: " X "\n", __FILE__, __FUNCTION__, __LINE__)
# define MSG(X)      printf("[%s:%s:%d]: " X "\n", __FILE__, __func__, __LINE__)
# define PRT(X, ...) printf("[%s:%s:%d]: " X "\n", __FILE__, __func__, __LINE__, __VA_ARGS__)

#ifdef ENABLE_DBG

#define DEBUG(X, ...) fprintf(stderr, "[%s:%s:%d]: " X "\n", __FILE__, __func__, __LINE__, __VA_ARGS__)
#define DBG(X, ...)   fprintf(stderr, "[%s:%s:%d]: " X "\n", __FILE__, __func__, __LINE__, __VA_ARGS__)

#define VALFMTS "[%s:%s():%d] %s: "
#define VALFMTP "\n"

#define VAL(X) fprintf(stderr, _Generic((X), \
    char:                VALFMTS "%c"   VALFMTP, \
    unsigned char:       VALFMTS "%hhu" VALFMTP, \
    short:               VALFMTS "%h"   VALFMTP, \
    unsigned short:      VALFMTS "%hu"  VALFMTP, \
    int:                 VALFMTS "%d"   VALFMTP, \
    unsigned int:        VALFMTS "%u"   VALFMTP, \
    long:                VALFMTS "%l"   VALFMTP, \
    unsigned long:       VALFMTS "%lu"  VALFMTP, \
    long long:           VALFMTS "%lld" VALFMTP, \
    unsigned long long:  VALFMTS "%llu" VALFMTP, \
    char*:               VALFMTS "'%s'" VALFMTP, \
    const char*:         VALFMTS "'%s'" VALFMTP, \
    default:             VALFMTS "0x%x" VALFMTP  \
	), __FILE__, __func__, __LINE__, #X, X)


#else

#define DEBUG(...) do{}while(0)
#define DBG(...)   do{}while(0)
#define VAL(_)     do{}while(0)

#endif

/*
    size_t:         VALFMTS "%d"   VALFMTP, \
    ssize_t:        VALFMTS "%u"   VALFMTP, \
*/

#endif	// DBG_H_
