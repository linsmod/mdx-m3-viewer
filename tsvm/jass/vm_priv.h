#include "jass_parser.h"
#include "shared.h"
#include <vm_public.h>
#include <parser.h>
#include "zhash.h"
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
#define VMFUNC(NAME,TYPE) (JASSFUNC){ #NAME, NAME,TYPE,0,0,0,0,0,0}
#define VMFUNC2(KEY,NAME,TYPE) { KEY, NAME,TYPE,0,0,0,0,0,0}
#define INF_LOOP_PROTECTION 1024

#define assert_type(var, type) assert(jass_checkvartype(var, type))

#define JASSALLOC(VAR,type) \
(VAR)=vmext_alloc(sizeof(type)); \
memset((VAR), 0, sizeof(type))

LPCJASSTYPE find_typebyid(JASSTYPEID id);

LPCJASSTYPE find_type(LPCJASS j, LPCSTR name);
#define JASS_ADD_STACK(j, VAR, TYPE) \
LPJASSVAR VAR = &j->stack[j->num_stack++]; \
memset(VAR, 0, sizeof(*VAR)); \
VAR->type = find_typebyid(TYPE);

#define JASS_ADD_STACK2(j, VAR, TYPE) \
LPJASSVAR VAR = &j->stack[j->num_stack++]; \
memset(VAR, 0, sizeof(*VAR)); \
VAR->type = find_type(j,TYPE);

#define JASS_SET_VALUE(VAR, VALUE, SIZE) \
jass_setnull(VAR); \
  (VAR)->value = vmext_alloc(SIZE); \
  memcpy((VAR)->value, VALUE, SIZE); \

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
typedef struct {
    LPCSTR name;
    void (*func)(LPJASS, LPJASSENV, LPPARSER);
} parseStatement_t;

struct jass_var {
    LPCJASSTYPE type;
    union{
        HANDLE value;
        LPJASSFUNC _fn;
        LPCSTR _str;
        LPJASSOBJECT _obj;
    };
    DWORD *refcount;
    BOOL constant;
    BOOL array;
    struct {
        LPJASSDICT locals;
        DWORD returnstack;
        BOOL done;
    } env;
};

struct jass_type {
    LPCJASSTYPE inherit;
    LPJASSTYPE next;
    LPCSTR name;
    JASSTYPEID typeid;
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
struct cfunction{
    LPSTR name;
    DWORD (*f)(LPJASS j);
};

struct jass_function {
    // shared fields
    LPCSTR name;
    DWORD (*f)(LPJASS j);
    JASSTYPEID rettype;
    LPJASSFUNC next;

    // code function fields
    LPCJASSTYPE returns;
    LPJASSPARAM params;
    LPCTOKEN code;
    BOOL constant;
    LPCSTR codefile;
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
};

enum{
    IMPORTED_VAR,
    IMPORTED_OBJ,
    IMPORTED_DEFAULT,
};

// for namespaced vars
struct jass_imported{
    LPJASSIMPORTED next;
    LPCSTR key;
    LPJASSMODULE module;
    DWORD importtype; // var or all
    LPJASSVAR var;
    LPJASSOBJECT obj;
};
struct jass_object{
    LPJASSDICT* props;
    DWORD num_props;
    LPHASHTABLE ht; // LPJASSVAR
    LPCTOKEN code; // object is defined by which code 
    LPJASSOBJECT next;
};

struct jass_s {
    // These are builtin c functions, registered into `jass_s` but not visible from scripts scope
    // To export a native function into scripts scope, 
    // use `constant native xxxx takes xxx,... returns xxx` in vminit.jass 
    LPHASHTABLE g_shared_natives; 

    LPJASSFUNC native_functions; 

    // global shared types
    LPHASHTABLE g_shared_types; 
    LPJASSTYPE types; // vm core types and the code registered types
    JASSVAR stack[MAX_JASS_STACK];
    DWORD num_stack;
    LPJASSVAR base_sp;
    LPJASSVAR stack_pointer;
    JASSCONTEXT context;
    LPCTOKEN current_token;
    LPJASSMODULE main_module;
    LPJASSMODULE this_module;
    LPJASSMODULE evaluted;
    LPCSTR evaluting;

    LPCJASSFUNC callee;
    LPCJASSFUNC caller;


    // global shared functions
    LPCJASSFUNC fn_enosuch;
    LPCJASSFUNC fn_export;
    LPCJASSFUNC fn_evalprog;
};

void jass_register_Array(LPJASS j);
void jass_register_Math(LPJASS j);