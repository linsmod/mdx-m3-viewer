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

#define MAKE(TYPE,...)(TYPE){__VA_ARGS__}

#define SAFE_DELETE(x, func) if (x) { func(x); (x) = NULL; }

#define FOR_LOOP(property, max) \
for (DWORD property = 0, end = max; property < end; ++property)


#define FOR_EACH_LIST(type, property, list) \
for (type *property = list, *next = list ? (list)->next : NULL; \
property; \
property = next, next = next ? next->next : NULL)

#define ADD_TO_LIST(VAR, LIST) VAR->next = LIST; LIST = VAR;

#define FOR_EACH(type, property, array, num) \
for (type *property = array; property - array < num; property++)

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

#define PUSH_BACK(TYPE, VAR, LIST) \
if (LIST) { \
    TYPE *last##TYPE = LIST; \
    while (last##TYPE->next) last##TYPE = last##TYPE->next; \
    last##TYPE->next = VAR; \
} else { \
    LIST = VAR; \
}
