#pragma once
#include <ctype.h>
#define KNOWN_AS(STRUCT, TYPE) \
typedef struct STRUCT TYPE; \
typedef struct STRUCT *LP##TYPE; \
typedef struct STRUCT const *LPC##TYPE;

typedef const char   * LPCSTR;
typedef char   * LPSTR;
typedef unsigned int   DWORD;
typedef unsigned char  BOOL;
typedef int            LONG;
typedef float          FLOAT;
typedef void         * HANDLE;
typedef char TEXT128[128];
typedef char TEXT256[256];

// strdup函数声明，解决隐式声明警告
extern char *strdup(const char *s);
#define MAKE(TYPE,...)(TYPE){__VA_ARGS__}

#define SAFE_DELETE(x, func) if (x) { func(x); (x) = NULL; }

#define ALLOCZ(VAR,type) \
(VAR)=vmext_alloc(sizeof(type)); \
memset((VAR), 0, sizeof(type))

#define ALLOCZN(VAR,type,n) \
(VAR)=vmext_alloc(sizeof(type)); \
memset((VAR), 0, sizeof(type)*(n))

#define FOR_LOOP(property, max) \
for (DWORD property = 0, end = max; property < end; ++property)

#define FOR_LOOP_FROM(property,start, max) \
for (DWORD property = start, end = max; property < end; ++property)

#define HASH_STR(str, hash)            \
    do {                               \
        const char *s = (str);         \
        (hash) = 5381;                 \
        int c;                         \
        while ((c = *s++))             \
            (hash) = ((hash) << 5) + (hash) + c; \
    } while(0)

#define FOR_EACH_LIST(type, property, list) \
for (type *property = list, *next = list ? (list)->next : NULL; \
property; \
property = next, next = next ? next->next : NULL)

#define ADD_TO_LIST(VAR, LIST) VAR->next = LIST; (LIST) = VAR;\

#define FOR_EACH(type, property, array, num) \
for (type *property = array; property - array < (long long)(num); property++)

#define REMOVE_FROM_LIST(TYPE, VAR, LIST, DELETER) \
TYPE **prev = &LIST; \
FOR_EACH_LIST(TYPE, it, LIST) { \
    if (it == VAR) { \
        *prev = it->next; \
        DELETER(it); \
        break; \
    } \
    prev = &it->next; \
}

#define DELETE_LIST(TYPE, LIST, DELETER) \
for (TYPE *it = LIST; it;) { \
    TYPE *next = it->next; \
    DELETER(it); \
    it = next; \
}

#include "assert.h"
#include "stdio.h"
#include "stdlib.h"
#define PUSH_BACK(TYPE, VAR, LIST) do{ \
    if (LIST) { \
        TYPE *last##TYPE = LIST; \
        while (last##TYPE->next){  \
            last##TYPE = last##TYPE->next; \
        }\
        last##TYPE->next = VAR; \
        if(last##TYPE->next->next==last##TYPE){\
            fprintf(stderr, "E: VAR copy required on `%s` at %s:%d\n", #TYPE, __FILE__,__LINE__);exit(1);\
        };\
    } else { \
        LIST = VAR; \
    } \
}while(0)  
