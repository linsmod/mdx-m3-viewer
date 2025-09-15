#include "shared.h"
#include <vm_public.h>
#include <parser.h>
#define F_END { NULL }
#define MAX_JASS_STACK 256
#define JASS_DELIM ":,;()[]+-/*=!{}"
#define JASS_CONSTANT "constant"
#define JASS_ARRAY "array"
#define JASS_NULL "null"
#define JASS_UNDEFINED "undefined"
#define JASS_FALSE "false"
#define JASS_TRUE "true"
#define JASS_UNM "-"
#define JASS_COMMA ","
#define TYPE_AUTO "auto_type"
#define JASS_OPERATOR(NAME) { #NAME, NAME }
#define INF_LOOP_PROTECTION 1024

#define assert_type(var, type) assert(jass_checktype(var, type))
#define JASSALLOC(type) vmext_alloc(sizeof(type))

#define JASS_ADD_STACK(j, VAR, TYPE) \
LPJASSVAR VAR = &j->stack[j->num_stack++]; \
memset(VAR, 0, sizeof(*VAR)); \
VAR->type = &jass_types[TYPE];

#define JASS_SET_VALUE(VAR, VALUE, SIZE) \
jass_setnull(VAR); \
if (VALUE) { \
  (VAR)->value = vmext_alloc(SIZE); \
  memcpy((VAR)->value, VALUE, SIZE); \
}

#define JASS_CMPOP(NAME, OP) \
DWORD NAME(LPJASS j) { \
    return jass_pushboolean(j, jass_checknumber(j, 1) OP jass_checknumber(j, 2)); \
}

#define JASS_NUMOP(NAME, OP) \
DWORD NAME(LPJASS j) { \
    if (jass_gettype(j, 1) == jasstype_integer && jass_gettype(j, 2) == jasstype_integer) { \
        return jass_pushinteger(j, jass_checkinteger(j, 1) OP jass_checkinteger(j, 2)); \
    } else { \
        return jass_pushnumber(j, jass_checknumber(j, 1) OP jass_checknumber(j, 2)); \
    } \
}

#define JASS_ASSIGNOP_INT(NAME, OP) \
DWORD NAME(LPJASS j) { \
    if (jass_gettype(j, 1) == jasstype_integer && jass_gettype(j, 2) == jasstype_integer) { \
        LONG value= jass_checkinteger(j, 1) OP jass_checkinteger(j, 2); \
        LPJASSVAR var = jass_stackvalue(j, 2); \
        JASS_SET_VALUE(var, &value, sizeof(LONG)); \
    } else { \
        fprintf(stderr, "Invalid operator on FLOAT: %s\n", #OP); \
    } \
    return 0; \
}

#define JASS_ASSIGNOP(NAME, OP) \
DWORD NAME(LPJASS j) { \
    if (jass_gettype(j, 1) == jasstype_integer && jass_gettype(j, 2) == jasstype_integer) { \
        jass_pushinteger(j, jass_checkinteger(j, 1) OP jass_checkinteger(j, 2)); \
    } else { \
        jass_pushnumber(j, jass_checknumber(j, 1) OP jass_checknumber(j, 2)); \
    } \
    LPJASSVAR result = jass_stackvalue(j, 1); \
    jass_pop(j,1); \
    jass_copy(j,jass_stackvalue(j, 2),result); \
    return 0; \
}


KNOWN_AS(jass_pram, JASSPARAM);
KNOWN_AS(jass_env, JASSENV);
KNOWN_AS(jass_class, JASSCLASS);
typedef struct {
    LPCSTR name;
    void (*func)(LPJASS, LPJASSENV, LPPARSER);
} parseStatement_t;

struct jass_var {
    LPCJASSTYPE type;
    HANDLE value;
    DWORD *refcount;
    BOOL constant;
    BOOL array;
    struct {
        LPJASSDICT locals;
        DWORD returnstack;
        BOOL done;
    } env;
    LPJASSARRAY _array;
};

struct jass_type {
    LPCJASSTYPE inherit;
    LPJASSTYPE next;
    LPCSTR name;
};
struct jass_class{
    LPCJASSTYPE inherit;
    LPCSTR name;
    LPJASSCLASS next;
};

struct jass_pram {
    LPJASSPARAM next;
    LPCJASSTYPE type;
    LPCSTR name;
};

struct jass_function {
    LPJASSPARAM params;
    LPCJASSTYPE returns;
    LPJASSFUNC next;
    LPCSTR name;
    LPCTOKEN code;
    DWORD (*nativefunc)(LPJASS j);
    BOOL constant;
    LPCJASSTYPE clstype;
};

struct jass_array {
    LPJASSARRAY next;
    DWORD index;
    JASSVAR value;
};

typedef enum {
    Unset,
    LocalVars,
    GlobalVars,
    ExportVars,
    ImportedVars,
}varplace;

struct jass_dict {
    LPJASSDICT next;
    LPCSTR key;
    JASSVAR value;
    varplace declScope;
};

// for namespaced vars
struct jass_nsvar{
    struct jass_nsvar* next;
    LPCSTR ns;
    LPCSTR key;
    JASSVAR value;
};

struct jass_s {
    LPJASSDICT globals;
    LPJASSTYPE types;
    LPJASSFUNC functions;
    LPJASSFUNC anonymous_functions;
    JASSVAR stack[MAX_JASS_STACK];
    DWORD num_stack;
    LPJASSVAR stack_pointer;
    JASSCONTEXT context;
    LPCTOKEN current_token;
    LPJASSMODULE this_module;
    LPJASSMODULE imports;
    LPJASSNS import_ns;
};