// file vm_main.c
#include "../shared.h"
#include "api_macros.h"
#include "vm_public.h"
#include "vm_ext.h"
#include "jass_parser.h"
#include "../parser.h"
#include <StormPort.h>
#include <asm-generic/errno.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <ctype.h>
#include <assert.h>
#include <strings.h>
#include <vm_priv.h>
VMPROGRAM VM_Compile(LPCTOKEN token);
struct keyword{
    LPCSTR kw;
    DWORD pf;
};
KNOWN_AS(keyword, KEYWORD)
KEYWORD keywords[] = {
    {.kw="elseif",.pf=PF_JASS}, 
    {.kw="else",.pf=PF_JASS|PF_JS}, 
    {.kw="endif",.pf=PF_JASS}, 
    {.kw="set",.pf=PF_JASS}, 
    {.kw="endfunction",.pf=PF_JASS}, 
    {.kw="local",.pf=PF_JASS}, 
    {.kw="then",.pf=PF_JASS}, 
    {.kw="for",.pf=PF_JS},
    {.kw="let",.pf=PF_JS}, 
    {.kw="var",.pf=PF_JS}, 
    {.kw="export",.pf=PF_JS}, 
    {.kw="import",.pf=PF_JS}, 
    {.kw="typeof",.pf=PF_JS}, 
    {.kw="instanceOf",.pf=PF_JS}, 
    {0}
};


extern JASSFUNC vmtypefuncs[];

static LPJASSMODULE g_module_cache = NULL; // 所有已加载模块链表
static __thread int depth = 0;

// must sync with JASSTYPEID
// primitive types
JASSTYPE jass_types[] = {
    { NULL, NULL, "handle",jasstype_handle },
    { NULL, NULL, "nothing",jasstype_nothing },
    { NULL, NULL, "integer",jasstype_integer },
    { NULL, NULL, "real",jasstype_real },
    { NULL, NULL, "string",jasstype_string },
    { NULL, NULL, "boolean",jasstype_boolean },
    { NULL, NULL, "code",jasstype_function }, // function ref
    { NULL, NULL, "auto",jasstype_auto },
};
BOOL jass_dofile_alias(LPJASS j, LPCSTR fileName,LPCSTR alias);
static LPJASSVAR jass_stackvalue(LPJASS j, int index);
static JASSTYPEID jass_getvarbasetype(LPCJASSVAR var);
static DWORD jass_dotoken(LPJASS j, LPCTOKEN token);
static LPJASSMODULE gcache_find_module(LPCSTR name);
void jass_pop(LPJASS j, DWORD count);
void jass_copy(LPJASS j, LPJASSVAR var, LPCJASSVAR other);
static LPJASSVAR find_var(LPJASS j,LPCSTR name);
unsigned long hash_str(const char *str);
void jass_setnull(LPJASSVAR var);
static void export_var(LPJASS j,LPCSTR name);
void eval_FUNCTION(LPJASS j, LPCTOKEN token);
DWORD jass_dofunction_args(LPJASS j,LPCJASSFUNC func,LPCTOKEN token);
LPCJASSTYPE get_base_type(LPCJASSTYPE type) ;
static LPJASSDICT find_dictvar(LPJASS j,LPCSTR name,BOOL find_exports);

LPJASSDICT alloc_dict(){
    LPJASSDICT dict= JASSALLOC(dict,JASSDICT);
    return dict;
}

BOOL atob(LPCSTR str) {
    return !strcmp(str, "true");
}

DWORD __unm(LPJASS j) {
    if (jass_gettype(j, 1) == jasstype_integer) {
        return jass_pushinteger(j, -jass_checkinteger(j, 1));
    } else {
        return jass_pushnumber(j, -jass_checknumber(j, 1));
    }
}

JASS_NUMOP(__add, +);
JASS_NUMOP(__sub, -);
JASS_NUMOP(__mul, *);
JASS_NUMOP(__div, /);
JASS_CMPOP(__le, <=);
JASS_CMPOP(__ge, >=);
JASS_CMPOP(__gt, >);
JASS_CMPOP(__lt, <);

JASS_ASSIGNOP(__add_assign, +);        // a += b
JASS_ASSIGNOP(__sub_assign, -);        // a -= b
JASS_ASSIGNOP(__mul_assign, *);        // a *= b
JASS_ASSIGNOP(__div_assign, /);        // a /= b
JASS_ASSIGNOP_INT(__mod_assign, %);    // a %= b
JASS_ASSIGNOP_INT(__lshift_assign, <<); // a <<= b
JASS_ASSIGNOP_INT(__rshift_assign, >>); // a >>= b
JASS_ASSIGNOP_INT(__and_assign, &);     // a &= b
JASS_ASSIGNOP_INT(__or_assign, |);      // a |= b
JASS_ASSIGNOP_INT(__xor_assign, ^);     // a ^= b

static BOOL var_eq(LPCJASSVAR a, LPCJASSVAR b) {
    if (jass_getvarbasetype(a) != jass_getvarbasetype(b)) {
        return false;
    }
    switch ((a->value == NULL) + (b->value == NULL)) {
        case 2: return true;
        case 1: return false;
    }
    switch (jass_getvarbasetype(a)) {
        case jasstype_integer: return !memcmp(a->value, b->value, sizeof(LONG));
        case jasstype_real: return !memcmp(a->value, b->value, sizeof(FLOAT));
        case jasstype_string: return !strcmp(a->value, b->value);
        case jasstype_boolean: return !memcmp(a->value, b->value, sizeof(BOOL));
        case jasstype_function: return !memcmp(a->value, b->value, sizeof(HANDLE));
        case jasstype_handle: return a->value==b->value;
        case jasstype_type: 
        assert(0);
        break;
        case jasstype_nothing:
        case jasstype_auto:
        // should not reach here.
            assert(0);
    }
    return false;
}


DWORD __eq(LPJASS j) {
    return jass_pushboolean(j, var_eq(jass_stackvalue(j, 1), jass_stackvalue(j, 2)));
}

DWORD __ne(LPJASS j) {
    return jass_pushboolean(j, !var_eq(jass_stackvalue(j, 1), jass_stackvalue(j, 2)));
}

DWORD __and(LPJASS j) {
    return jass_pushboolean(j, jass_toboolean(j, 1) && jass_toboolean(j, 2));
}

DWORD __or(LPJASS j) {
    return jass_pushboolean(j, jass_toboolean(j, 1) || jass_toboolean(j, 2));
}

DWORD __not(LPJASS j) {
    return jass_pushboolean(j, !jass_toboolean(j, 1));
}
//condition ? true_expression : false_expression
DWORD __cond(LPJASS j) {
    int vi = jass_checkboolean(j, 3) ? 2 : 1;
    LPJASSVAR ret = jass_stackvalue(j, vi);
    LPJASSVAR var = &j->stack[j->num_stack++];
    memset(var, 0, sizeof(*var));
    var->type = ret->type;
    jass_copy(j, var, ret);
    // jass_dumpstack(j);
    return 1;
}

DWORD __typeofx(LPJASS j) {
    LPJASSVAR var =  jass_stackvalue(j, 1);
    if(!var){
        return jass_pushstring(j,"undefined");
    }
    return jass_pushstring(j,get_base_type(var->type)->name);
}
DWORD jassrt_export(LPJASS j) {
    LPJASSVAR var= jass_stackvalue(j, 1);
    LPCSTR var_name =  jass_checkstring(j,2);
    LPJASSDICT entry  = alloc_dict();
    entry->value.type = find_typebyid(j, var->type->typeid);
    entry->key = strdup(var_name);
    jass_copy(j, &entry->value, var);
    ADD_TO_LIST(entry, j->this_module->exports);
    return 0;
}
static DWORD jassrt_enosuchfunction(LPJASS j) {
    fprintf(stderr, "\nExit due to calling undefined function.\n");
    jass_dumpstack(j);
    exit(1);
}
JASSFUNC vmfunctions[] = {
    VMFUNC(__add, jasstype_real),
    VMFUNC(__sub, jasstype_real),
    VMFUNC(__mul, jasstype_real),
    VMFUNC(__div, jasstype_real),
    VMFUNC(__ne, jasstype_boolean),
    VMFUNC(__eq, jasstype_boolean),
    VMFUNC(__ge, jasstype_boolean),
    VMFUNC(__le, jasstype_boolean),
    VMFUNC(__gt, jasstype_boolean),
    VMFUNC(__lt, jasstype_boolean),
    VMFUNC(__and, jasstype_boolean),
    VMFUNC(__or, jasstype_boolean),
    VMFUNC(__unm, jasstype_boolean),
    VMFUNC(__not, jasstype_boolean),
    VMFUNC(__cond, jasstype_boolean),
    VMFUNC(__typeofx, jasstype_string),
    { 0 },
};

DWORD Math_random(LPJASS j) {
    return jass_pushnullhandle(j, "gamestate");
}
JASSFUNC vmtypefuncs[] = {
    VMFUNC2("Math.random",Math_random,jasstype_integer),
    {0}
};

void removeDoubleBackslashes(LPSTR str) {
    size_t len = strlen(str);
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        str[j++] = str[i];
        if (str[i] == '\\' && str[i + 1] == '\\') {
            i++; // Skip the second backslash
        }
    }
    str[j] = '\0'; // Null-terminate the modified string
}

BOOL is_integer(LPCSTR tok) {
    LPSTR endptr;
    strtol(tok, &endptr, 10);
    return *endptr == '\0';
}

BOOL is_float(LPCSTR tok) {
    LPSTR endptr;
    strtod(tok, &endptr);
    return *endptr == '\0';
}

BOOL is_fourcc(LPCSTR tok) {
    return *tok == '\'';
}

BOOL is_string(LPCSTR tok) {
    return *tok == '\"' ||  *tok == '\'';
}

BOOL is_simpleidentifier(LPCSTR str,DWORD pf) {
    if (!isalpha(*str) && *str != '_')
        return false;
    for (LPCSTR s = str; *s; ++s) {
        if (!isalnum(*s) && *s != '_')
            return false;
    }
    for (LPKEYWORD kw = keywords; kw->kw; kw++) {
        if (kw->pf == pf && !strcmp(str, kw->kw)) {
            return false;
        }
    }
    return true;
}

BOOL is_qualified_identifier(LPCSTR str,DWORD pf) {
    // Check if the string contains at least one dot and valid identifiers separated by dots
    if (!str || !strchr(str, '.')) return false;

    LPCSTR start = str;
    while (1) {
        LPCSTR dot = strchr(start, '.');
        if (!dot) {
            // Last segment, must be a valid identifier
            return is_simpleidentifier(start,pf);
        }
        // Check the segment before the dot
        char segment[128];
        size_t len = dot - start;
        if (len >= sizeof(segment)) return false;
        strncpy(segment, start, len);
        segment[len] = '\0';
        if (!is_simpleidentifier(segment,pf)) return false;
        start = dot + 1;
    }
}

BOOL is_identifier(LPCSTR str,DWORD pf) {
    if(!str || str[0]=='\0')
        return  false;
    return is_qualified_identifier(str,pf) || is_simpleidentifier(str,pf);
}


DWORD is_modifier(LPCSTR str) {
    if(!strcmp(str, "public"))
        return TF_PUBLIC;
    if(!strcmp(str, "private"))
        return TF_PRIVATE;
    if(!strcmp(str, "protected"))
        return TF_PROTECTED;
    return 0;
}

BOOL is_comma(LPCSTR str) {
    return !strcmp(str, ",");
}

static HANDLE RunAction(HANDLE handle) {
    LPJASS j = handle;
    jass_pushfunction(j, j->context.func);
#ifdef DEBUG_JASS
    fprintf(stdout,"<jass_thread> RunAction at %s:%d", __FILE__,__LINE__);
    INDENT(depth);
    fprintf(stdout, "call: %s at %s\n", j->context.func->name, JASS_DumpLocation(j->current_token->sref));
#endif
    jass_call(j, 0);
    vmext_free(handle);
    return NULL;
}

LPCJASSCONTEXT jass_getcontext(LPJASS j) {
    return &j->context;
}

void jass_startthread(LPJASS j, LPCJASSCONTEXT context) {
    LPJASS thread = jass_newstate(NULL);
    memcpy(thread, j, sizeof(JASS));
    memset(thread->stack, 0, sizeof(thread->stack));
    thread->stack_pointer = thread->stack;
    thread->num_stack = 0;
    thread->context = *context;
    vmext_createthread(RunAction, thread);
}
void jass_doclosure(LPJASS j,LPJASSFUNC func) {
    LPJASS closure = jass_newstate(NULL);
    memcpy(closure, j, sizeof(JASS));
    memset(closure->stack, 0, sizeof(closure->stack));
    closure->stack_pointer = closure->stack;
    closure->num_stack = 0;
    vmext_createthread(RunAction, closure);
}

BOOL jass_evaluatetrigger(LPJASS j, LPTRIGGER trigger) {
    if (trigger->disabled)
        return false;
    JASS tmp_state;
    FOR_EACH_LIST(TRIGGERCONDITION, cond, trigger->conditions) {
        memcpy(&tmp_state, j, sizeof(struct jass_s));
        memset(tmp_state.stack, 0, sizeof(tmp_state.stack));
        tmp_state.num_stack = 0;
        tmp_state.context.trigger = trigger;
        jass_pushfunction(&tmp_state, cond->expr);
#ifdef DEBUG_JASS
    fprintf(stdout,"<vm> jass_evaluatetrigger at %s:%d\n", __FILE__,__LINE__);
    INDENT(depth);
    fprintf(stdout, "call: %s at %s\n", cond->expr->name, JASS_DumpLocation(j->current_token->sref));
#endif
        if (jass_call(&tmp_state, 0) != 1 || !jass_popboolean(&tmp_state)) {
            return false;
        }
    }
    return true;
}

void jass_executetrigger(LPJASS j, LPTRIGGER trigger) {
    FOR_EACH_LIST(TRIGGERACTION, action, trigger->actions) {
        jass_startthread(j, &MAKE(JASSCONTEXT,
                                  .trigger = trigger,
                                  .func = action->func,
                              ));
    }
}

BOOL jass_calltrigger(LPJASS j, LPTRIGGER trigger) {
    if (jass_evaluatetrigger(j, trigger)) {
        jass_executetrigger(j, trigger);
        return true;
    } else {
        return false;
    }
}
static CFUNC find_cfunction(LPCJASS j, LPCSTR name) {
    for (LPCJASSFUNC m = vmfunctions; m->name; m++) {
        if (!strcmp(m->name, name)) {
            return m->f;
        }
    }
    for (LPCJASSFUNC m = vmtypefuncs; m->name; m++) {
        if (!strcmp(m->name, name)) {
            return m->f;
        }
    }
    return NULL;
}
static LPCSTR function_getname(CFUNC func) {
    for (LPCJASSFUNC m = vmfunctions; m->name; m++) {
        if (m->f == func) {
            return m->name;
        }
    }
    for (LPCJASSFUNC m = vmtypefuncs; m->name; m++) {
        if (m->f == func) {
            return m->name;
        }
    }
    return NULL;
}

static LPCJASSFUNC find_function(LPJASS j, LPCSTR name) {
    FOR_EACH_LIST(JASSFUNC, func, j->functions) {
        if (!strcmp(func->name, name)) {
            return func;
        }
    }
    LPJASSDICT dict= find_dictvar(j,name,1);
    if(dict && dict->value.type->typeid==jasstype_function){
        return (LPCJASSFUNC)dict->value.value;
    }
   
    jass_dumpenv(j);
    printf("INFO: find_function `%s` results null\n",name);
    assert(j->__enosuchmethod);
    return j->__enosuchmethod;
}


void jass_registerfunc(LPJASS j,LPJASSFUNC func){
    LPJASSFUNC curr = func;
    while (curr) {
        LPCJASSFUNC f = find_function(j,curr->name);
        if(f){
            printf("WARN: duplicated function register ignored");
        }
        assert(curr->name);
        assert(curr->f);
        ADD_TO_LIST(curr,j->functions);
        curr = curr->next;
    }
}

LPCJASSTYPE find_typebyid(LPCJASS j, JASSTYPEID id) {
    FOR_LOOP(i, sizeof(jass_types)/sizeof(*jass_types)) {
        if (jass_types[i].typeid==id) {
            return &jass_types[i];
        }
    }
    return NULL;
}
static LPCJASSFUNC g_rt_enosuchfunction = &VMFUNC(jassrt_enosuchfunction, jasstype_nothing);
static LPCJASSFUNC g_rt_export = &VMFUNC(jassrt_export, jasstype_nothing);
static void jass_register_vmfunctions(LPJASS j) {
    for (LPJASSFUNC m = vmfunctions; m->name; m++) { 
        LPJASSFUNC func = ALLOCZ(func, JASSFUNC);
        func->name = m->name;
        func->f = m->f;
        func->rettype = m->rettype;
        func->returns = find_typebyid(j,  m->rettype);
        PUSH_BACK(JASSFUNC,func,j->functions);
    }

    j->__enosuchmethod = g_rt_enosuchfunction;
    j->__export = g_rt_export;
    assert(j->__enosuchmethod);
}
static LPJASSVAR find_dict(LPJASSDICT dict, LPCSTR name) {
    FOR_EACH_LIST(JASSDICT, item, dict) {
        if (!strcmp(item->key, name)) {
            return &item->value;
        }
    }
    return NULL;
}
static LPJASSDICT copy_dict(LPJASSDICT dict, LPCSTR name) {
    FOR_EACH_LIST(JASSDICT, item, dict) {
        if (!strcmp(item->key, name)) {
            LPJASSDICT copy  = alloc_dict();
            copy->key = name;
            copy->value = item->value;
            return copy;
        }
    }
    return NULL;
}
static LPJASSMODULE gcache_find_module(LPCSTR name){
    FOR_EACH_LIST(JASSMODULE, item, g_module_cache){
        if(strcasecmp(item->name, name)==0){
            return item;
        }
    }
    return NULL;
}
LPCJASSTYPE find_type(LPCJASS j, LPCSTR name) {
    if(!strcmp(name, "number"))
        name= "real";

    FOR_LOOP(i, sizeof(jass_types)/sizeof(*jass_types)) {
        if (!strcmp(jass_types[i].name, name)) {
            return &jass_types[i];
        }
    }
    FOR_EACH_LIST(JASSTYPE, type, j->types) {
       size_t len = strlen(type->name);
        if (strncmp(name, type->name, len) == 0) {
            if (name[len] == '.' || name[len] == '\0') {
                return type;
            }
        }
    }
    return NULL;
}

LPCJASSTYPE get_base_type(LPCJASSTYPE type) {
    while (type->inherit) {
        type = type->inherit;
    }
    return type;
}

void jass_setreturn(LPJASS j) {
    jass_stackvalue(j, 0)->env.done = true;
    jass_stackvalue(j, 0)->env.returnstack = j->num_stack;
}

BOOL jass_mustreturn(LPJASS j) {
    return jass_stackvalue(j, 0)->env.done;
}

DWORD jass_top(LPJASS j) {
    return j->num_stack-1;
}

LPJASSVAR jass_topvalue(LPJASS j) {
    return j->stack+jass_top(j);
}

LPJASSVAR jass_stackvalue(LPJASS j, int index) {
    if (index < 0) {
        return j->stack + (j->num_stack + index);
    } else {
        return j->stack_pointer + index;
    }
}

JASSTYPEID jass_getvarbasetype(LPCJASSVAR var) {
    return (JASSTYPEID)(get_base_type(var->type)->typeid);
}

JASSTYPEID jass_gettype(LPJASS j, int index) {
    LPCJASSVAR var = jass_stackvalue(j, index);
    return jass_getvarbasetype(var);
}

BOOL jass_checktype(LPCJASSVAR var, JASSTYPEID type) {
    return get_base_type(var->type) == jass_types+type;
}

void jass_pop(LPJASS j, DWORD count) {
    j->num_stack -= count;
}


static void jass_deletedict(LPJASSDICT dict) {
    SAFE_DELETE(dict->next, jass_deletedict);
    jass_setnull(&dict->value);
    vmext_free(dict);
}

void jass_setnull(LPJASSVAR var) {
    SAFE_DELETE(var->env.locals, jass_deletedict);
    switch (jass_getvarbasetype(var)) {
        case jasstype_handle:
            if (var->refcount && *var->refcount > 0) {
                (*var->refcount)--;
                var->value = NULL;
                var->refcount = NULL;
            } else {
                SAFE_DELETE(var->value, vmext_free);
                SAFE_DELETE(var->refcount, vmext_free);
            }
            break;
        case jasstype_function:
            // skip
            break;
        default:
            SAFE_DELETE(var->value, vmext_free);
            break;
    }
}

//void jass_setenv(LPJASS j, int index, LPJASSDICT env) {
//    jass_stackvalue(j, index)->env.locals = env;
//}

BOOL is_handle_convertible(LPCJASSTYPE from, LPCJASSTYPE to) {
    if (from == to) {
        return true;
    } else if (from->inherit) {
        return is_handle_convertible(from->inherit, to);
    } else {
        return false;
    }
}

static LPJASSVAR ensure_array_value(LPJASS j, LPJASSVAR dest, DWORD index) {
    FOR_EACH_LIST(JASSARRAY, var, (LPJASSARRAY)dest->value) {
        if (var->index == index) {
            return &var->value;
        }
    }
    LPJASSARRAY jv  = JASSALLOC(jv , JASSARRAY);
    jv->value.type = dest->type;
    jv->index = index;
    PUSH_BACK(JASSARRAY,jv, dest->value);
    return &jv->value;
}

void jass_copy(LPJASS j, LPJASSVAR var, LPCJASSVAR other) {
    FLOAT fval = 0;
    jass_setnull(var);
    if(var->type->typeid==jasstype_auto){
        var->type = other->type;
    }
    // if (var->array || other->array) {
    //     var->type = other->type;
    //     ALLOCZ(var->value, JASSARRAY);
    //     FOR_EACH_LIST(JASSARRAY, srcar, (LPJASSARRAY)other->value) {
    //         jass_copy(j, ensure_array_value(j, var, srcar->index), &srcar->value);
    //     }
    //     return;
    // } else 
    if (!other->value) {
        return;
    } else {
        switch (var->type->typeid) {
            case jasstype_integer:
                assert(other->type == var->type);
                JASS_SET_VALUE(var, other->value, sizeof(LONG));
                break;
            case jasstype_handle:
                if (!is_handle_convertible(other->type, var->type)) {
                    fprintf(stderr, "Warning: Passing %s to %s type\n", other->type->name, var->type->name);
                }
                var->type = other->type;
                var->value = other->value;
                var->refcount = other->refcount;
                if (var->refcount) {
                    (*var->refcount)++;
                }
                break;
            case jasstype_real:
                switch (jass_getvarbasetype(other)) {
                    case jasstype_real:
                        fval = *(FLOAT const *)other->value;
                        break;
                    case jasstype_integer:
                        fval = *(LONG const *)other->value;
                        break;
                    default:
                        assert(false);
                        return;
                }
                JASS_SET_VALUE(var, &fval, sizeof(FLOAT));
                break;
            case jasstype_boolean:
                assert(var->type==other->type);
                JASS_SET_VALUE(var, other->value, sizeof(BOOL));
                break;
            case jasstype_type:
            case jasstype_string:
                var->type = other->type; // use explicit type if var tye is auto;
                JASS_SET_VALUE(var, other->value, strlen(other->value)+1);
                break;
            case  jasstype_function:
                var->type = other->type; // use explicit type if var tye is auto;
                var->value = other->value;
                break;
            default:
                assert(false);
                break;
        }
    }
}

DWORD jass_pushnull(LPJASS j) {
    JASS_ADD_STACK(j, var, jasstype_handle);
    return 1;
}

DWORD jass_pushinteger(LPJASS j, LONG value) {
    JASS_ADD_STACK(j, var, jasstype_integer);
    JASS_SET_VALUE(var, &value, sizeof(value));
    return 1;
}
DWORD jass_pushhandle(LPJASS j, HANDLE value, LPCSTR type) {
    JASS_ADD_STACK(j, var, jasstype_handle);
    jass_setnull(var);
    var->type = find_type(j, type);
    
    if (value) {
        var->value = value;
        var->refcount = vmext_alloc(sizeof(DWORD));
    }
    return 1;
}
DWORD jass_pusharray(LPJASS j, HANDLE value, LPCSTR type) {
    JASS_ADD_STACK(j, var, jasstype_handle);
    jass_setnull(var);
    var->type = find_type(j, type);
    var->array = true;
    if (value) {
        var->value = value;
        var->refcount = vmext_alloc(sizeof(DWORD));
    }
    return 1;
}
DWORD jass_pushtype(LPJASS j, LPCJASSTYPE value) {
    JASS_ADD_STACK(j, var, jasstype_type);
    jass_setnull(var);
    if (value) {
        var->value = (HANDLE)value;
        var->refcount = vmext_alloc(sizeof(DWORD));
    }
    return 1;
}

DWORD jass_pushnullhandle(LPJASS j, LPCSTR type) {
    return jass_pushhandle(j, NULL, type);
}

HANDLE jass_newhandle(LPJASS j, DWORD size, LPCSTR type) {
    HANDLE data = size ? vmext_alloc(size) : NULL;
    jass_pushhandle(j, data, type);
    return data;
}

DWORD jass_pushlighthandle(LPJASS j, HANDLE value, LPCSTR type) {
    JASS_ADD_STACK(j, var, jasstype_handle);
    jass_setnull(var);
    var->type = find_type(j, type);
    var->value = value;
    var->refcount = vmext_alloc(sizeof(DWORD));
    *var->refcount = 1; // so that runtime won't ever delete it
    return 1;
}

DWORD jass_pushnumber(LPJASS j, FLOAT value) {
    JASS_ADD_STACK(j, var, jasstype_real);
    JASS_SET_VALUE(var, &value, sizeof(value));
    return 1;
}

DWORD jass_pushboolean(LPJASS j, BOOL value) {
    JASS_ADD_STACK(j, var, jasstype_boolean);
    JASS_SET_VALUE(var, &value, sizeof(value));
    return 1;
}

DWORD jass_pushstringlen(LPJASS j, LPCSTR value, DWORD len) {
    JASS_ADD_STACK(j, var, jasstype_string);
    JASS_SET_VALUE(var, value, len+1);
    ((LPSTR)var->value)[len] = '\0';
    removeDoubleBackslashes(var->value);
    return 1;
}

DWORD jass_pushstring(LPJASS j, LPCSTR value) {
    jass_pushstringlen(j, value, (DWORD)strlen(value));
    return 1;
}

// DWORD jass_pushfunction(LPJASS j, LPNATIVEFUNC func) {
//     JASS_ADD_STACK(j, var, jasstype_function);
//     JASS_SET_VALUE(var, &func, sizeof(LPNATIVEFUNC));
//     return 1;
// }
 
DWORD jass_pushfunction(LPJASS j, LPCJASSFUNC func) {
    // if (func->nativefunc) {
    //     return jass_pushfunction(j, func->nativefunc);
    // } else {
        JASS_ADD_STACK(j, var, jasstype_function);
        var->value = (LPJASSFUNC)func;
        return 1;
    // }
}

DWORD jass_pushvalue(LPJASS j, LPCJASSVAR other) {
    LPCJASSTYPE type = other->type;
    LPJASSVAR var = &j->stack[j->num_stack++];
    memset(var, 0, sizeof(*var));
    var->type = type;
    jass_copy(j, var, other);
    return 1;
}

LONG jass_checkinteger(LPJASS j, int index) {
    LPCJASSVAR var = jass_stackvalue(j, index);
    assert_type(var, jasstype_integer);
    return var->value ? *(LONG *)var->value : 0;
}

FLOAT jass_checknumber(LPJASS j, int index) {
    LPCJASSVAR var = jass_stackvalue(j, index);
    if (jass_checktype(var, jasstype_real)) {
        return var->value ? *(FLOAT *)var->value : 0;
    }
    if (jass_checktype(var, jasstype_integer)) {
        return var->value ? *(LONG *)var->value : 0;
    }
    assert(false);
}

BOOL jass_checkboolean(LPJASS j, int index) {
    LPCJASSVAR var = jass_stackvalue(j, index);
    assert_type(var, jasstype_boolean);
    return var->value ? *(BOOL *)var->value : 0;
}

BOOL jass_toboolean(LPJASS j, int index) {
    LPCJASSVAR var = jass_stackvalue(j, index);
    JASSTYPEID type = jass_getvarbasetype(var);
    if (var->value == NULL)
        return false;
    switch (type) {
        case jasstype_integer: return *(LONG *)var->value != 0;
        case jasstype_real: return *(FLOAT *)var->value != 0;
        case jasstype_string: return strlen(var->value) > 0;
        case jasstype_boolean: return *(BOOL *)var->value != 0;
        case jasstype_handle: return true;
        case jasstype_function: return true;
        default: return false;
    }
}

LPCSTR jass_checkstring(LPJASS j, int index) {
    LPCJASSVAR var = jass_stackvalue(j, index);
    assert_type(var, jasstype_string);
    return var->value;
}

LPJASSTYPE jass_checktypeof(LPJASS j, int index) {
    LPCJASSVAR var = jass_stackvalue(j, index);
    assert_type(var, jasstype_type);
    return var->value;
}

LPCJASSFUNC jass_checkcode(LPJASS j, int index) {
    LPCJASSVAR var = jass_stackvalue(j, index);
    assert_type(var, jasstype_function);
    return var->value;
}

HANDLE jass_checkhandle(LPJASS j, int index, LPCSTR type) {
    LPCJASSVAR var = jass_stackvalue(j, index);
    assert(is_handle_convertible(var->type, find_type(j, type)));
    return var->value;
}

void jass_swap(LPJASS j, int a, int b) {
    JASSVAR tmp = *jass_stackvalue(j, a);
    *jass_stackvalue(j, a) = *jass_stackvalue(j, b);
    *jass_stackvalue(j, -2) = tmp;
}

BOOL jass_popboolean(LPJASS j) {
    BOOL value = jass_toboolean(j, -1);
    jass_pop(j, 1);
    return value;
}

DWORD jass_popinteger(LPJASS j) {
    DWORD value = jass_checkinteger(j, -1);
    jass_pop(j, 1);
    return value;
}

DWORD VM_EvalInteger(LPJASS j, LPCTOKEN token) {
    return jass_pushinteger(j, atoi(token->primary));
}

DWORD VM_EvalReal(LPJASS j, LPCTOKEN token) {
    return jass_pushnumber(j, atof(token->primary));
}

DWORD VM_EvalString(LPJASS j, LPCTOKEN token) {
    return jass_pushstring(j, token->primary);
}

DWORD VM_EvalBoolean(LPJASS j, LPCTOKEN token) {
    return jass_pushboolean(j, atob(token->primary));
}

DWORD VM_EvalIdentifier(LPJASS j, LPCTOKEN token) {
    LPCJASSFUNC f = NULL;
    LPCJASSVAR v = NULL;
    // LPCJASSTYPE t = NULL;
    if (token->flags & TF_FUNCTION) {
        if ((f = find_function(j, token->primary))) {
            return jass_pushfunction(j, f);
        } else {
            assert(0);
            return jass_pushtype(j,NULL);
        }
    } else if ((v = find_var(j,token->primary))) {
        jass_dumpenv(j);
        return jass_pushvalue(j, v);
    }
    // else if((t = find_type(j, token->primary))){
    //     return jass_pushtype(j,t);
    // }
    else {
        // Not in a function call, use function as ref
        if((f = find_function(j, token->primary))){
            return jass_pushfunction(j, f);
        }
        printf("Access undefined type `%s` at %s\n",token->primary,JASS_DumpLocation(token->sref));
        assert(0);
        return jass_pushtype(j, NULL);
    }
}

DWORD VM_EvalArrayAccess(LPJASS j, LPCTOKEN token) {
    assert(jass_dotoken(j, token->index) == 1);
    DWORD index_val = jass_popinteger(j);
    VM_EvalIdentifier(j, token);
    LPJASSVAR var = jass_stackvalue(j, -1);
    LPJASSVAR item = ensure_array_value(j, var, index_val);
    jass_pop(j, 1);
    JASSVAR tmp;
    memcpy(&tmp, item, sizeof(JASSVAR));
    jass_pushvalue(j, &tmp);
    return 1;
}

DWORD VM_EvalFourCC(LPJASS j, LPCTOKEN token) {
    return jass_pushinteger(j, *(DWORD *)token->primary);
}

DWORD VM_EvalCall(LPJASS j, LPCTOKEN token) {
    LPCJASSFUNC f = NULL;
    if(token->flags & TF_NEW){
        printf("new");
    }
    if(!strcmp(token->primary, "vec3.create")){
        printf("debg");
    }
    if (!strcmp(token->primary, "CommentString") && token->args) {
        fprintf(stdout, "%s\n", token->args->primary);
        return 0;
    } else if ((f = find_function(j, token->primary))) {
        return jass_dofunction_args(j, f, token);
    }
    else {
        fprintf(stderr, "Can't find function %s\n", token->primary);
        assert(false);
        return 0;
    }
}

static struct {
    TOKENTYPE tokentype;
    DWORD (*func)(LPJASS j, LPCTOKEN token);
} 
token_types[] = {
    { TT_INTEGER, VM_EvalInteger },
    { TT_REAL, VM_EvalReal },
    { TT_STRING, VM_EvalString },
    { TT_BOOLEAN, VM_EvalBoolean },
    { TT_IDENTIFIER, VM_EvalIdentifier },
    { TT_ARRAYACCESS, VM_EvalArrayAccess },
    { TT_FOURCC, VM_EvalFourCC },
    { TT_CALL, VM_EvalCall },
};

DWORD jass_dotoken(LPJASS j, LPCTOKEN token) {
    if (!token){
        return 0;
    }
    FOR_LOOP(idx, sizeof(token_types)/sizeof(*token_types)) {
        if (token_types[idx].tokentype == token->ttype) {
            return token_types[idx].func(j, token);
        }
    }
    if(token->ttype==TT_FUNCTION){
        
        if(token->flags & TF_INPLACECALL) // var a = function{}()()...;
        {
            // Ensure inner call will push result to stack
            ((LPTOKEN)token)->flags |= TF_PUSHSTACK;

            // 1) eval tokens to declare the function
            int old=j->num_stack;
            eval_FUNCTION(j, token);
            assert(j->num_stack-old==1);

            // 2) get the declared function from returning on stack
            LPJASSVAR var= jass_topvalue(j);
            LPCJASSFUNC resultfn = var->value;

            // 3） get the first call-instruction
            LPTOKEN target = token->next; // the first call-instruction with args
            assert(target->ttype == TT_CALL);

            // 4) do the call
            DWORD stack = stack= jass_dofunction_args(j, resultfn, target);
            while (target->next) {
                target = target->next;
                resultfn = jass_checkcode(j, 1);

                stack= jass_dofunction_args(j, resultfn, target);
                jass_dumpstack(j);
                assert(stack==1);
            }
            return 1;
        }
        else{
            eval_FUNCTION(j, token);
            return 0;
        }
    }
    assert(false);
    return 0;
}

static void jass_set_value(LPJASS j, LPJASSVAR dest, LPCTOKEN init) {
    DWORD stack = jass_dotoken(j, init);
    assert(stack == 1);
    jass_copy(j, dest, j->stack + jass_top(j));
    jass_pop(j, 1);
}

static void jass_set_array_value(LPJASS j, LPJASSVAR dest, LPCTOKEN index, LPCTOKEN init) {
    assert(jass_dotoken(j, index) == 1);
    DWORD index_val = jass_popinteger(j);
    LPJASSVAR index_dest = ensure_array_value(j, dest, index_val);
    assert(jass_dotoken(j, init) == 1);
    jass_copy(j, index_dest, j->stack + jass_top(j));
    jass_pop(j, 1);
}

LPJASSDICT parse_dict(LPJASS j, LPCTOKEN token) {
    LPJASSDICT item  = alloc_dict();
    item->value.constant = token->flags & TF_CONSTANT;
    item->value.array = token->flags & TF_ARRAY;
    if(token->flags & TF_AUTOTYPE){
        //nothing to do
        //type will be set in jass_set_value
        item->value.type = find_type(j, "auto");
        assert(item->value.type);
    }
    else{
        item->value.type = find_type(j, token->primary);
        assert(item->value.type);
    }
    item->key = token->secondary;
    if (token->stmt) {
        jass_set_value(j, &item->value, token->stmt);
    }
    return item;
}

#define TOKENFUNC(NAME) void eval_##NAME(LPJASS j, LPCTOKEN token)
#define TOKENEVAL(NAME) { #NAME, TT_##NAME, eval_##NAME }

TOKENFUNC(TOKENS);
TOKENFUNC(SINGLETOKEN);

TOKENFUNC(TYPEDEF) {
    LPJASSTYPE type  = JASSALLOC(type , JASSTYPE);
    type->name = token->primary;
    type->inherit = find_type(j, token->secondary);
    type->typeid = type->inherit->typeid;
    find_cfunction(j,token->primary);
    LPCJASSFUNC func =  find_function(j, token->primary);
    if(!func){
        assert(0);
    }
    ADD_TO_LIST(type, j->types);
}

TOKENFUNC(IF) {
    while (token) {
        if (!token->condition) {
            eval_TOKENS(j, token->stmt);
            return;
        }
        jass_dotoken(j, token->condition);
        if (jass_popboolean(j)) {
            eval_TOKENS(j, token->stmt);
            return;
        }
        token = token->elseblock;
    }
}

TOKENFUNC(SET) {
    LPJASSVAR v = NULL;
    // Set an exists var
    assert(token->secondary); // varname

    if ((v = find_var(j, token->secondary))) {
        if (token->index) {
            return jass_set_array_value(j, v, token->index, token->stmt);
        } else {
            return jass_set_value(j, v, token->stmt);
        }
    }
    else if(token->stmt){
        // Declare to var
        ((LPTOKEN)token)->flags |= TF_AUTOTYPE;
        LPJASSDICT vardecl = parse_dict(j, token);
        ADD_TO_LIST(vardecl, jass_stackvalue(j, 0)->env.locals);
    }
    else {
        fprintf(stderr, "Can't find variable %s\n", token->secondary);
    }
}

TOKENFUNC(VARDECL) {
    assert(token->stmt);
    LPJASSDICT vardecl = parse_dict(j, token);
    ADD_TO_LIST(vardecl, jass_stackvalue(j, 0)->env.locals);
    if(token->flags & TF_VARLIST){ // var a,b,c
        assert(token->next);
        FOR_EACH_LIST(TOKEN, item, token->next){
            vardecl = parse_dict(j, item);
            ADD_TO_LIST(vardecl, jass_stackvalue(j, 0)->env.locals);
        }
    }
    if(token->flags & TF_PUSHSTACK){
        jass_pushvalue(j,  &vardecl->value);
    }
}

TOKENFUNC(GLOBAL) {
    LPJASSDICT global = parse_dict(j, token);
    ADD_TO_LIST(global, j->globals);
}

TOKENFUNC(FUNCTION) {
    LPJASSFUNC func  = JASSALLOC(func , JASSFUNC);
    func->params = NULL;
    func->name = token->primary;
    func->code = token->stmt;
    assert(token->secondary);
    func->returns = find_type(j, token->secondary);
    assert(func->returns);
    assert(!token->args);
    FOR_EACH_LIST(TOKEN, arg, token->params) {
        LPJASSPARAM param  = JASSALLOC(param , JASSPARAM);
        param->name = arg->secondary;
        param->type = find_type(j, arg->primary);
        param->next = NULL;
        assert(param->type);
        PUSH_BACK(JASSPARAM, param, func->params);
    }
   
    if (token->flags & TF_NATIVE) {
        LPJASSFUNC mod = find_in_array(vmtypefuncs, sizeof(JASSFUNC), func->name);
        if (mod) {
            func->f = mod->f;
        }
    }
    ADD_TO_LIST(func, j->functions);

    if(token->flags & TF_PUSHSTACK){
        jass_pushfunction(j, func);
    }

    // // var a = function(){}()()...
    // if(token->flags & TF_INPLACECALL){
    //     // todo call 
    //     LPCJASSFUNC resultfn = func; // function
    //     LPTOKEN target = token->next; // args
    //     DWORD stack = 0;
    //     while (target) {
            
    //         stack= jass_dofunction_args(j, resultfn, target);
    //         assert(stack==1);
    //         resultfn = jass_checkcode(j, 1);
    //         target = target->next;
    //     }
    // }
    // else{
    //     // Declare anonymous function to stack
    //     if(!strcmp(func->name, "<anonymous>")){
    //         jass_pushfunction(j, func);
    //     }
    // }
}

TOKENFUNC(CALL) {
    jass_dotoken(j, token);
}

TOKENFUNC(LOOP) {
    for (DWORD i = 0;; i++) {
        FOR_EACH_LIST(TOKEN const, tok, token->stmt) {
            if (jass_mustreturn(j)) {
                return;
            } else if (token->ttype == TT_RETURN) {
                jass_setreturn(j);
                jass_dotoken(j, token->stmt);
                return;
            } else if (tok->ttype == TT_EXITWHEN) {
                jass_dotoken(j, tok->condition);
                if (jass_popboolean(j)) {
                    return;
                }
            } else {
                eval_SINGLETOKEN(j, tok);
            }
        }
        assert(i < INF_LOOP_PROTECTION);
    }
}


TOKENFUNC(IMPORT_RUNONLY){
    // 简单导入: import 'module'
    LPJASSDICT module_dict  = alloc_dict();
    module_dict->key = strdup(token->primary);  // module name
    module_dict->value.type = find_type(j, "handle");
    jass_setnull(&module_dict->value);
    
    // 将模块字典添加到当前作用域
    PUSH_BACK(JASSDICT, module_dict, jass_stackvalue(j, 0)->env.locals);
}

TOKENFUNC(IMPORT_DEFAULT_ENTRY){
    assert(token->primary);
    LPCSTR search = token->primary;  // 模块路径
    LPJASSMODULE module = jass_loadmodule(j, search);
    if (!module) {
        fprintf(stderr, "Failed to load module: %s\n", search);
        return;
    }
    PUSH_BACK(JASSMODULE, module, j->imports);
    assert(token->secondary);
    LPJASSDICT entry = copy_dict(module->exports,token->secondary);
    assert(entry);
    PUSH_BACK(JASSDICT, entry, jass_stackvalue(j, 0)->env.locals);
}

TOKENFUNC(IMPORT_ALL_ENTRIES) {
     // import * as Namespace from 'module'
    LPSTR search = token->primary;
    LPSTR namespace = token->secondary;
    LPJASSMODULE module = jass_loadmodule(j, search);
    if (!module) return;
    FOR_EACH_LIST(JASSDICT, var, module->exports){
        // import key1,key2,...,keyn under ns
        LPJASSDICT entry = copy_dict(module->exports, var->key);
        // update entry's key to qualified
        size_t ns_len = strlen(namespace);
        size_t key_len = strlen(entry->key);
        char *qualified_key = vmext_alloc(ns_len + 1 + key_len + 1); // ns.key\0
        sprintf(qualified_key, "%s.%s", namespace, entry->key);
        entry->key = qualified_key;
        entry->declScope = ImportedVars;
        PUSH_BACK(JASSDICT, entry, j->globals);
    }
}

TOKENFUNC(IMPORT_ENTRY_LIST) {
    LPCSTR module_name = token->primary;  // 模块路径
    
    LPJASSMODULE module = jass_loadmodule(j, module_name);
    if (!module) {
        fprintf(stderr, "Failed to load module: %s\n", module_name);
        return;
    }
    PUSH_BACK(JASSMODULE, module, j->imports);
    assert(token->args);
    FOR_EACH_LIST(TOKEN, item, token->args){
        LPJASSDICT entry = copy_dict(module->exports,item->primary);
        assert(entry);
        PUSH_BACK(JASSDICT, entry, jass_stackvalue(j, 0)->env.locals);
    }
}

TOKENFUNC(EXPORT_DEFAULT_ENTRY) {
    LPJASSDICT default_export  = alloc_dict();
    default_export->key = "default";
    default_export->value = *(jass_topvalue(j)); // 假设默认导出在栈顶
    jass_pop(j, 1);

    PUSH_BACK(JASSDICT, default_export, j->this_module->exports);
}
TOKENFUNC(EXPORT_ADD_ENTRY) {
    assert(token->primary);
    assert(token->stmt);

    LPCSTR var_name = token->primary;
      if(!strcmp(var_name, "setMatrixArrayType")){
        printf("debg");
    }
    jass_pushfunction(j, j->__export);
    DWORD old_stack = j->num_stack;
    token->stmt->flags |= TF_PUSHSTACK; // pushstack after declaring
    eval_SINGLETOKEN(j, token->stmt);
    assert(j->num_stack-old_stack==1);
    jass_pushstring(j, var_name);
    jass_call(j, 2);
}

TOKENFUNC(EXPORT_ALL_ENTRIES){
    // 命名空间导出
    LPJASSDICT export_entry  = alloc_dict();
    export_entry->key = token->primary;  // namespace name
    export_entry->value.type = find_type(j, "namespace");
    jass_setnull(&export_entry->value);
    
    // 添加到全局导出表
    PUSH_BACK(JASSDICT, export_entry, j->globals);
}

TOKENFUNC(EXPORT_ENTRY_LIST){
    // 将所有导出的变量添加到导出表中
    FOR_EACH_LIST(TOKEN, var, token->args) {
        assert(var->primary);  // export name
        
        LPCSTR var_name = var->primary;
        // 如果有别名，使用别名作为key
        if (var->secondary) {
            var_name = var->secondary;// export {primary as secondary}
        }
        jass_pushfunction(j, j->__export);
        DWORD old_stack = j->num_stack;
        var->flags |= TF_PUSHSTACK; // pushstack after declaring
        jass_dotoken(j, var);
        assert(j->num_stack-old_stack==1);
        jass_pushstring(j, var_name);
        jass_call(j, 2);
    }
}

struct {
    LPCSTR name;
    TOKENTYPE type;
    void (*func)(LPJASS, LPCTOKEN);
} 
token_eval[] = {
    TOKENEVAL(TYPEDEF),
    TOKENEVAL(FUNCTION),
    TOKENEVAL(VARDECL),
    TOKENEVAL(GLOBAL),
    TOKENEVAL(CALL),
    TOKENEVAL(IF),
    TOKENEVAL(SET),
    TOKENEVAL(LOOP),
    TOKENEVAL(EXPORT_DEFAULT_ENTRY),
    TOKENEVAL(EXPORT_ALL_ENTRIES),
    TOKENEVAL(EXPORT_ENTRY_LIST),
    TOKENEVAL(EXPORT_ADD_ENTRY),
    TOKENEVAL(IMPORT_DEFAULT_ENTRY),
    TOKENEVAL(IMPORT_ALL_ENTRIES),
    TOKENEVAL(IMPORT_ENTRY_LIST),
};


TOKENFUNC(SINGLETOKEN) {
    FOR_LOOP(index, sizeof(token_eval) / sizeof(*token_eval)) {
        if (token->ttype == token_eval[index].type) {
            token_eval[index].func(j, token);
            return;
        }
    }
    fprintf(stderr, "Can't evaluate token of type %d\n", token->ttype);
    assert(false);
}

TOKENFUNC(TOKENS) {
    FOR_EACH_LIST(TOKEN const, tok, token) {
        if (jass_mustreturn(j)) {
            return;
        } else if (tok->ttype == TT_RETURN) {
            jass_setreturn(j);
            jass_dotoken(j, tok->stmt);
        } else {
            eval_SINGLETOKEN(j, tok);
        }
    }
}

BOOL jass_dobuffer(LPJASS j, LPSTR buffer2,LPCSTR srcfilename,DWORD pflags) {
    LPSTR buffer = buffer2;
    vmext_skipbom(buffer);
    LPTOKEN program = JASS_ParseTokens(&MAKE(PARSER, 
        .buffer = buffer, 
        .delimiters = JASS_DELIM,
        .file = srcfilename,
        .line = 1,
        .column = 1,
        .pflags = pflags
    ));
//    VM_Compile(program);
    eval_TOKENS(j, program);
    return true;
}


LPJASSMODULE jass_newmodule(LPCSTR filname,LPCSTR alias) {
    LPJASSMODULE module  = JASSALLOC(module , JASSMODULE);
    
    module->evaluating = true;
    module->loaded = true;
    module->exports  = alloc_dict();
    module->name = alias;
    module->file = filname;
    module->state = NULL;
    return module;
}

LPJASS jass_newmodulestate(LPCSTR filname,LPCSTR alias){
    LPJASSMODULE m  = jass_newmodule(filname,alias);
    return jass_newstate(m);
}
static LPSTR vminitscript = "./src/vminit.jass";
DWORD filepflags(LPCSTR fileName){
    LPSTR ext = strrchr(fileName, '.');
    DWORD pflags = PF_JASS;
    if(ext && (!_stricmp(ext, ".js")||!_stricmp(ext, ".ts"))){
        pflags = PF_JS;
    }
    return pflags;
}
LPJASS jass_newstate(LPJASSMODULE module) {
    if(!module)
        module = jass_newmodule(vminitscript,vminitscript);
    LPJASS j  = JASSALLOC(j , JASS);
    j->stack_pointer = j->stack;
    j->this_module = module;
    j->this_module->state = j;
    jass_register_vmfunctions(j);
    jass_register_Array(j);
    jass_register_Math(j);
    jass_dofile_alias(j,vminitscript,vminitscript);
    return j;
}

LPJASSMODULE jass_loadmodule(LPJASS loader,LPCSTR search) {
    printf("jass_loadmodule %s by %s\n",search,loader->this_module->name);
    LPSTR path = vmext_resolvepath(search,loader->this_module->file);
    if(!path){
        fprintf(stderr, "Can't resolve module: %s\n", search);
        exit(1);
    }
    // 1. 检查缓存
    LPJASSMODULE module = gcache_find_module(path);
    if (module) {
        if (module->evaluating) {
            // 循环依赖：允许，但跳过执行
            return module;
        }
        return module; // 已加载
    }

    // 2. 创建新模块
    JASSALLOC(module , JASSMODULE);
    module->name = strdup(search);
    module->state = jass_newstate(module);
    module->exports = NULL;
    module->loaded = false;
    module->evaluating = true;
    ADD_TO_LIST(module, g_module_cache);

    // 3. 读取并执行模块代码
    if (!jass_dofile(module->state, path)) {
        fprintf(stderr, "Failed to execute module: %s\n", search);
        return NULL;
    }

    module->loaded = true;
    module->evaluating = false;

    return module;
}
static void jass_deletemodule(LPJASSMODULE module){
    // 清理导出表
    SAFE_DELETE(module->exports, jass_deletedict);

    // 清理模块状态
    jass_close(module->state);

    vmext_free(module);
}
void jass_unloadmodule(LPJASSMODULE module) {
    if (!module) return;
    // 从全局缓存中移除
    REMOVE_FROM_LIST(JASSMODULE, module, g_module_cache,jass_deletemodule);
}

void jass_close(LPJASS j) {
    vmext_free(j);
}

#define EXTRACT_DIR "build"



BOOL jass_dofile_alias(LPJASS j, LPCSTR fileName,LPCSTR src_alias){
    j->this_module->file = fileName;
    LPSTR buffer = vmext_readalltext(fileName);
    if (buffer) {
        BOOL success = jass_dobuffer(j, buffer,src_alias,filepflags(fileName));
        vmext_free(buffer);
        return success;
    } else {
        fprintf(stderr, "vm_dofile: Invalid file %s\n", fileName);
        return false;
    }
}
BOOL jass_dofile(LPJASS j, LPCSTR fileName) {
    return jass_dofile_alias(j, fileName, fileName);
}

//BOOL jass_dofilenative(LPJASS j, LPCSTR fileName) {
//    FILE *file = fopen(fileName, "rb");
//    if (file == NULL) {
//        fprintf(stderr, "Error opening the file.\n");
//        return false;
//    }
//    fseek(file, 0, SEEK_END);
//    long file_size = ftell(file);
//    fseek(file, 0, SEEK_SET);
//    LPSTR buffer = vmext_alloc(file_size + 1); // +1 for null-terminator
//    if (buffer == NULL) {
//        fprintf(stderr, "Memory allocation failed.\n");
//        fclose(file);
//        return false;
//    }
//    fread(buffer, 1, file_size, file);
//    buffer[file_size] = '\0';
//
//    fclose(file);
//
//    BOOL success = jass_dobuffer(j, fileName, buffer);
//
//    // Free the buffer memory
//    vmext_free(buffer);
//
//    return success;
//}

DWORD jass_dofunction_args(LPJASS j,LPCJASSFUNC func,LPCTOKEN tkargs){
    DWORD stacksize = j->num_stack;
    DWORD args = 0;
    jass_pushfunction(j, func);
    // assert(!strcmp(func->name, tkargs->primary));
    FOR_EACH_LIST(TOKEN, arg, tkargs->args) {
#ifdef DEBUG_JASS
        if(arg->ttype==TT_STRING)
            fprintf(stdout, "[vm_main.c] push arg_%d: \"%s\" %s\n", args, arg->primary, arg->secondary ? arg->secondary : "");
        else
            fprintf(stdout, "[vm_main.c] push arg_%d: %s %s\n", args, arg->primary, arg->secondary ? arg->secondary : "");
#endif
        jass_dotoken(j, arg);
        args++;
    }
#ifdef DEBUG_JASS
    fprintf(stdout, "[vm_main.c] eval `%s` at %s", tkargs->primary, JASS_DumpLocation(tkargs->sref));
#endif
    
    jass_call(j, args);
    return j->num_stack - stacksize;
}

DWORD jass_call(LPJASS j, DWORD argc) {
    LPJASSVAR root = &j->stack[j->num_stack - argc - 1];
    LPJASSVAR old_stack_pointer = j->stack_pointer;
    DWORD ret = 0;
    j->stack_pointer = &j->stack[j->num_stack - argc - 1];
    depth++;

//     if (jass_getvarbasetype(root) == jasstype_function) {
//         LPNATIVEFUNC func = *(LPNATIVEFUNC *)root->value;
// #ifdef DEBUG_JASS
//         jass_dumpstack(j);
// #endif
//         ret = func(j);
//     } else {
        LPCJASSFUNC func = root->value;
        LPJASSDICT locals = NULL;
        
#ifdef DEBUG_JASS_DUMPSTACK
        printf("[vm_main.c] dumpstack before call %s: \n", func->name);
        jass_dumpstack(j);
#endif
       
        root->env.done = false;
        root->env.returnstack = -1;
        root->env.locals = locals;
        if(func->f){
            ret= func->f(j);
        }
        else{
            DWORD argnum = 1;
             FOR_EACH_LIST(JASSPARAM, arg, func->params) {
                LPJASSDICT local  = alloc_dict();
                local->key = arg->name;
                local->value.type = arg->type;
                jass_copy(j, &local->value, &j->stack_pointer[argnum]);
                PUSH_BACK(JASSDICT, local, locals);
                argnum++;
            }
            eval_TOKENS(j, func->code);
        }
        if (root->env.returnstack != (DWORD)-1) {
            ret = j->num_stack - root->env.returnstack;
        }
    // }
#ifdef DEBUG_JASS_DUMPSTACK
    printf("[vm_main.c] dumpstack after call %s: \n", func->name);
    jass_dumpstack(j);
#endif    
    LPJASSVAR last = &j->stack[j->num_stack - ret];
    for (LPJASSVAR it = root; it < last; it++) jass_setnull(it);
    memmove(root, last, ret * sizeof(JASSVAR));
    j->num_stack -= last - root;
    j->stack_pointer = old_stack_pointer;
    depth--;
#ifdef DEBUG_JASS_DUMPSTACK
    printf("[vm_main.c] dumpstack leaving boundary %s: \n", func->name);
    jass_dumpstack(j);
#endif
    return ret;
}
void jass_dumpvar(LPJASS j,LPCJASSVAR var){
        switch (jass_getvarbasetype(var)) {
            case jasstype_integer:
                fprintf(stdout, "%d", var->value ? *(LONG *)var->value : 0);
                break;
            case jasstype_real:
                fprintf(stdout, "%f", var->value ? *(FLOAT *)var->value : 0.0);
                break;
            case jasstype_boolean:
                fprintf(stdout, "%s", var->value && *(BOOL *)var->value ? "true" : "false");
                break;
            case jasstype_string:
                fprintf(stdout, "\"%s\"", var->value ? (LPCSTR)var->value : "");
                break;
            case jasstype_handle:
                fprintf(stdout, "%p", var->value);
                break;
            case jasstype_function: {
                LPJASSFUNC func = var->value ? ((LPJASSFUNC)var->value) : NULL;
                LPCSTR name = func ? func->name : NULL;
                fprintf(stdout, "function `%s` %p", name,func);
                break;
            }
            default:
                fprintf(stdout, "<unknown>");
                break;
        }
        if (var->array) {
            fprintf(stdout, " [");
            FOR_EACH_LIST(JASSARRAY, item, (LPJASSARRAY)var->value) {
                fprintf(stdout, " %d=", item->index);
                JASSVAR tmp_stack[1];
                memcpy(&tmp_stack[0], &item->value, sizeof(JASSVAR));
                // jass_dumpstack(&MAKE(JASS,
                //     .stack_pointer = tmp_stack,
                //     .num_stack = 1,
                // ));
            }
            fprintf(stdout, " ]");
        }
        fprintf(stdout, "\n");
}

static LPJASSDICT find_dictvar(LPJASS j,LPCSTR name,BOOL find_exports) {
    FOR_EACH_LIST(JASSDICT, dict, jass_stackvalue(j, 0)->env.locals){
        if(!strcmp(name, dict->key)){
            dict->declScope = LocalVars;
            return dict;
        }
    }
    FOR_EACH_LIST(JASSDICT, dict, j->globals){
        if(!strcmp(name, dict->key)){
            dict->declScope = GlobalVars;
            return dict;
        }
    }
    if(find_exports)
        FOR_EACH_LIST(JASSDICT, dict, j->this_module->exports){
            if(dict->key && !strcmp(name, dict->key)){
                dict->declScope = ExportVars;
                return dict;
            }
        }
    return NULL;
}
static void export_var(LPJASS j,LPCSTR name) {
    LPJASSDICT dict = find_dictvar(j,name,false);
    if(!dict){
        LPCJASSFUNC func =  find_function(j, name);
        if(func){
            LPJASSDICT entry  = alloc_dict();
            entry->value.type = find_type(j, "code");
            entry->value.value = &func;
            assert(entry->value.type);
            entry->key = strdup(name);
            entry->declScope = ExportVars;
            ADD_TO_LIST(entry, j->this_module->exports);
            return;
        }
        printf("INFO: Access undefined var when do export: `%s\n",name);
        return;
    }
    LPJASSDICT entry  = alloc_dict();
    entry->value.type = dict->value.type;
    entry->key = strdup(name);
    jass_copy(j, &entry->value, &dict->value);
    ADD_TO_LIST(entry, j->this_module->exports);
    // if(dict->declScope==LocalVars)
    // {
    //     REMOVE_FROM_LIST(JASSDICT, dict, jass_stackvalue(j, 0)->env.locals,jass_deletedict);
    // }
    // else if(dict->declScope==GlobalVars)
    // {
    //     REMOVE_FROM_LIST(JASSDICT, dict, j->globals,jass_deletedict);
    // }
    // printf("INFO: Var `%s is exported\n",name);
}
static LPJASSVAR find_var(LPJASS j,LPCSTR name) {
    FOR_EACH_LIST(JASSDICT, dict, jass_stackvalue(j, 0)->env.locals){
        if(!strcmp(name, dict->key)){
            return &dict->value;
        }
    }
    FOR_EACH_LIST(JASSDICT, dict, j->globals){
        if(!strcmp(name, dict->key)){
            return &dict->value;
        }
    }
    return NULL;
}

void jass_dumpenv(LPJASS j) {
    fprintf(stdout, "LPJASS env dump ===========\n");
    fprintf(stdout, "[locals]\n");
    FOR_EACH_LIST(JASSDICT, dict, jass_stackvalue(j, 0)->env.locals){
        printf("`%s`: ",dict->key);
        jass_dumpvar(j, &dict->value);
    }
    fprintf(stdout, "\n[exports by %s]\n",j->this_module->name);
    FOR_EACH_LIST(JASSDICT, dict, j->this_module->exports){
        if(!dict->key) continue;
        printf("`%s`: ",dict->key);
        jass_dumpvar(j, &dict->value);
    }
    fprintf(stdout, "\n[globals]\n");
    FOR_EACH_LIST(JASSDICT, dict, j->globals){
        printf("`%s`: ",dict->key);
        jass_dumpvar(j, &dict->value);
    }
    fprintf(stdout, "\n[functions]\n");
    FOR_EACH_LIST(JASSFUNC, dict, j->functions){
        printf("`%s`\n",dict->name);
    }
    fprintf(stdout, "\n[imports]\n");
    FOR_EACH_LIST(JASSMODULE, dict, j->imports){
        printf("`%s` %s\n",dict->name,dict->file);
    }
    fprintf(stdout, "\n");
}
void jass_dumpstack(LPJASS j) {
    fprintf(stdout, "jassvm stack dump (num_stack=%d):\n", j->num_stack);
    FOR_LOOP(i, j->num_stack) {
        LPCJASSVAR var = &j->stack[i];
        fprintf(stdout, "  %s%p [%d] type=%s value= ",&j->stack[i]==j->stack_pointer?">":" ", &j->stack[i], i, var->type->name);
        jass_dumpvar(j, var);
    }
}

// push arg first if argc>0
void jass_callbyname(LPJASS j, LPCSTR name,DWORD argc, BOOL async) {
    LPCJASSFUNC func = find_function(j, name);
    if (!func) {
        fprintf(stderr, "Function not found %s\n", name);
        return;
    }
    if (async) {
        jass_startthread(j, &MAKE(JASSCONTEXT, .func = func));
    } else {
        jass_pushfunction(j, func);
#ifdef DEBUG_JASS
            INDENT(depth);
            fprintf(stdout, "call: %s at %s:%d\n", name, __FILE__, __LINE__);
#endif        
        jass_call(j, argc);
    }
}
