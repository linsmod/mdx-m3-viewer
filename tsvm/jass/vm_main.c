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

LPCSTR keywords[] = {
    "elseif", 
    "else", 
    "endif", 
    "set", 
    "endfunction", 
    "local", 
    "then", 
    "for",
    "let", 
    "var", 
    "export", 
    "import", 
    "typeof", 
    "instanceOf", 
    NULL
};

extern JASSNATIVEFUNC jass_funcs[];

static LPJASSMODULE g_module_cache = NULL; // 所有已加载模块链表


// must sync with JASSTYPEID
JASSTYPE jass_types[] = {
    { NULL, NULL, "integer" },
    { NULL, NULL, "real" },
    { NULL, NULL, "string" },
    { NULL, NULL, "boolean" },
    { NULL, NULL, "code" },
    { NULL, NULL, "handle" },
    { NULL, NULL, "cfunction" },
    { NULL, NULL, "auto" },
    { NULL, NULL, "type" },
    { NULL, NULL, "Float32Array" },
    { NULL, NULL, "Array" },
};

static LPJASSVAR jass_stackvalue(LPJASS j, int index);
static JASSTYPEID jass_getvarbasetype(LPCJASSVAR var);
static DWORD jass_dotoken(LPJASS j, LPCTOKEN token);
static LPJASSMODULE gcache_find_module(LPCSTR name);
void jass_pop(LPJASS j, DWORD count);
void jass_copy(LPJASS j, LPJASSVAR var, LPCJASSVAR other);
static LPCJASSTYPE find_type(LPCJASS j, LPCSTR name);
unsigned long hash_str(const char *str);
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
        case jasstype_code: return !memcmp(a->value, b->value, sizeof(HANDLE));
        case jasstype_cfunction: return !memcmp(a->value, b->value, sizeof(HANDLE));
        case jasstype_handle: return a->value==b->value;
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
    LPCJASSVAR cond_expr =  jass_stackvalue(j, 1);
    LPCJASSVAR true_expr =  jass_stackvalue(j, 2);
    LPCJASSVAR false_expr =  jass_stackvalue(j, 3);
    return 0;
}

DWORD __typeofx(LPJASS j) {
    LPCSTR typename =  jass_checkstring(j, 1);
    if(!typename){
        return jass_pushnull(j);
    }
    LPCJASSTYPE t = find_type(j, typename);
    if(!t){
        return jass_pushnull(j);
    }
    return jass_pushinteger(j,hash_str(typename));
}

JASSNATIVEFUNC jass_operators[] = {
    JASS_OPERATOR(__add),
    JASS_OPERATOR(__sub),
    JASS_OPERATOR(__mul),
    JASS_OPERATOR(__div),
    JASS_OPERATOR(__ne),
    JASS_OPERATOR(__eq),
    JASS_OPERATOR(__ge),
    JASS_OPERATOR(__le),
    JASS_OPERATOR(__gt),
    JASS_OPERATOR(__lt),
    JASS_OPERATOR(__and),
    JASS_OPERATOR(__or),
    JASS_OPERATOR(__unm),
    JASS_OPERATOR(__not),
    JASS_OPERATOR(__cond),
    JASS_OPERATOR(__typeofx),
    { 0 },
};


DWORD import_module(LPJASS j) {
    return jass_pushnullhandle(j, "gamestate");
}
JASSNATIVEFUNC jass_funcs[] = {
    {.name="_import",.func=import_module},
    {.name="_export",.func=import_module},
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

BOOL is_simpleidentifier(LPCSTR str) {
    if (!isalpha(*str) && *str != '_')
        return false;
    for (LPCSTR s = str; *s; ++s) {
        if (!isalnum(*s) && *s != '_')
            return false;
    }
    for (LPCSTR *kw = keywords; *kw; kw++) {
        if (!strcmp(str, *kw)) {
            return false;
        }
    }
    return true;
}

BOOL is_qualified_identifier(LPCSTR str) {
    // Check if the string contains at least one dot and valid identifiers separated by dots
    if (!str || !strchr(str, '.')) return false;

    LPCSTR start = str;
    while (1) {
        LPCSTR dot = strchr(start, '.');
        if (!dot) {
            // Last segment, must be a valid identifier
            return is_simpleidentifier(start);
        }
        // Check the segment before the dot
        char segment[128];
        size_t len = dot - start;
        if (len >= sizeof(segment)) return false;
        strncpy(segment, start, len);
        segment[len] = '\0';
        if (!is_simpleidentifier(segment)) return false;
        start = dot + 1;
    }
}

BOOL is_identifier(LPCSTR str) {
    return is_qualified_identifier(str) || is_simpleidentifier(str);
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
    fprintf(stdout, "call: %s at %s\n", j->context.func->name, JASS_DumpLocation(j->current_token->location));
#endif
    jass_call(j, 0);
    vmext_free(handle);
    return NULL;
}

LPCJASSCONTEXT jass_getcontext(LPJASS j) {
    return &j->context;
}

void jass_startthread(LPJASS j, LPCJASSCONTEXT context) {
    LPJASS thread = jass_newstate();
    memcpy(thread, j, sizeof(JASS));
    memset(thread->stack, 0, sizeof(thread->stack));
    thread->stack_pointer = thread->stack;
    thread->num_stack = 0;
    thread->context = *context;
    vmext_createthread(RunAction, thread);
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
    fprintf(stdout, "call: %s at %s\n", cond->expr->name, JASS_DumpLocation(j->current_token->location));
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

static LPNATIVEFUNC find_cfunction(LPCJASS j, LPCSTR name) {
    for (LPCJASSNATIVEFUNC m = jass_operators; m->name; m++) {
        if (!strcmp(m->name, name)) {
            return m->func;
        }
    }
    for (LPCJASSNATIVEFUNC m = jass_funcs; m->name; m++) {
        if (!strcmp(m->name, name)) {
            return m->func;
        }
    }
    return NULL;
}
static LPCSTR cfunction_getname(LPNATIVEFUNC func) {
    for (LPCJASSNATIVEFUNC m = jass_operators; m->name; m++) {
        if (m->func == func) {
            return m->name;
        }
    }
    for (LPCJASSNATIVEFUNC m = jass_funcs; m->name; m++) {
        if (m->func == func) {
            return m->name;
        }
    }
    return NULL;
}

static LPCJASSFUNC find_function(LPCJASS j, LPCSTR name) {
    FOR_EACH_LIST(JASSFUNC, func, j->functions) {
        if (!strcmp(func->name, name)) {
            return func;
        }
    }
    return NULL;
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
            LPJASSDICT copy = JASSALLOC(JASSDICT);
            copy->key = name;
            copy->next = NULL;
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
static LPCJASSTYPE find_type(LPCJASS j, LPCSTR name) {
    if(!strcmp(name, "number"))
        name= "real";

    FOR_LOOP(i, sizeof(jass_types)/sizeof(*jass_types)) {
        if (!strcmp(jass_types[i].name, name)) {
            return &jass_types[i];
        }
    }
    FOR_EACH_LIST(JASSTYPE, type, j->types) {
        if (!strcmp(type->name, name)) {
            return type;
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
    return (JASSTYPEID)(get_base_type(var->type) - jass_types);
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
        case jasstype_type:
        case jasstype_code:
        case jasstype_cfunction:
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
    FOR_EACH_LIST(JASSARRAY, var, dest->_array) {
        if (var->index == index) {
            return &var->value;
        }
    }
    LPJASSARRAY jv = JASSALLOC(JASSARRAY);
    jv->value.type = dest->type;
    jv->index = index;
    ADD_TO_LIST(jv, dest->_array);
    return &jv->value;
}

void jass_copy(LPJASS j, LPJASSVAR var, LPCJASSVAR other) {
    FLOAT fval = 0;
    jass_setnull(var);
    if (other->_array) {
        var->type = other->type;
        FOR_EACH_LIST(JASSARRAY, srcar, other->_array) {
            jass_copy(j, ensure_array_value(j, var, srcar->index), &srcar->value);
        }
        return;
    } else if (!other->value) {
        return;
    } else {
        JASSTYPEID type = jass_getvarbasetype(var);
        if(type==jasstype_auto){
            type = jass_getvarbasetype(other);
        }
        switch (type) {
            case jasstype_integer:
                assert(other->type == var->type);
                JASS_SET_VALUE(var, other->value, sizeof(LONG));
                break;
            case jasstype_handle:
                if (!is_handle_convertible(other->type, var->type)) {
                    fprintf(stderr, "Warning: Passing %s to %s type\n", other->type->name, var->type->name);
                }
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
                assert(other->type == var->type);
                JASS_SET_VALUE(var, other->value, sizeof(BOOL));
                break;
            case jasstype_string:
                assert(other->type == var->type);
                JASS_SET_VALUE(var, other->value, strlen(other->value)+1);
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

DWORD jass_pushcfunction(LPJASS j, LPNATIVEFUNC func) {
    JASS_ADD_STACK(j, var, jasstype_cfunction);
    JASS_SET_VALUE(var, &func, sizeof(LPNATIVEFUNC));
    return 1;
}
 
DWORD jass_pushfunction(LPJASS j, LPCJASSFUNC func) {
    if (func->nativefunc) {
        return jass_pushcfunction(j, func->nativefunc);
    } else {
        JASS_ADD_STACK(j, var, jasstype_code);
        var->value = (LPJASSFUNC)func;
        return 1;
    }
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
        case jasstype_code: return true;
        default: return false;
    }
}

LPCSTR jass_checkstring(LPJASS j, int index) {
    LPCJASSVAR var = jass_stackvalue(j, index);
    assert_type(var, jasstype_string);
    return var->value;
}

LPCJASSFUNC jass_checkcode(LPJASS j, int index) {
    LPCJASSVAR var = jass_stackvalue(j, index);
    assert_type(var, jasstype_code);
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
   
    if (token->flags & TF_FUNCTION) {
        if ((f = find_function(j, token->primary))) {
            return jass_pushfunction(j, f);
        } else {
            return jass_pushnull(j);
        }
    } else if ((v = find_dict(j->globals, token->primary))) {
        return jass_pushvalue(j, v);
    } else if ((v = find_dict(jass_stackvalue(j, 0)->env.locals, token->primary))) {
        return jass_pushvalue(j, v);
    }
    else {
        LPCJASSTYPE t = find_type(j, token->primary);
        if(!t){
            printf("Access undefined `%s at %s\n",token->primary,JASS_DumpLocation(token->location));
            return jass_pushnull(j);
        }
        return jass_pushstring(j,token->primary);
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
    LPNATIVEFUNC cf = NULL;
    DWORD stacksize = j->num_stack;
    if (!strcmp(token->primary, "CommentString") && token->args) {
        fprintf(stdout, "%s\n", token->args->primary);
        return 0;
    } else if ((f = find_function(j, token->primary))) {
        DWORD args = 0;
        jass_pushfunction(j, f);
        FOR_EACH_LIST(TOKEN, arg, token->args) {
            j->current_token = token;
#ifdef DEBUG_JASS
            INDENT(depth);
            fprintf(stdout, "push arg%d: %s %s\n", args, arg->primary, arg->secondary ? arg->secondary : "");
#endif
            jass_dotoken(j, arg);
            args++;
        }
        j->current_token = token;
#ifdef DEBUG_JASS
            INDENT(depth);
            fprintf(stdout, "call: %s at %s\n", token->primary, JASS_DumpLocation(token->location));
#endif
        jass_call(j, args);
        return j->num_stack - stacksize;
    } else if ((cf = find_cfunction(j, token->primary))) {
        // jass_dumpstack(j);
        DWORD args = 0;
        jass_pushcfunction(j, cf);
        FOR_EACH_LIST(TOKEN, arg, token->args) {
            j->current_token = token;
#ifdef DEBUG_JASS
            INDENT(depth);
            fprintf(stdout, "push arg%d: %s %s\n", args, arg->primary, arg->secondary ? arg->secondary : "");
#endif
            jass_dotoken(j, arg);
            args++;
        }
        j->current_token = token;
#ifdef DEBUG_JASS
        // jass_dumpstack(j);
            INDENT(depth);
            fprintf(stdout, "call %s at %s\n", token->primary, JASS_DumpLocation(token->location));
#endif
        jass_call(j, args);
#if 1
        // jass_dumpstack(j);
#endif        
        return j->num_stack - stacksize;
    } else {
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
    if (!token)
        return 0;
    FOR_LOOP(idx, sizeof(token_types)/sizeof(*token_types)) {
        if (token_types[idx].tokentype == token->ttype) {
            return token_types[idx].func(j, token);
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
    LPJASSDICT item = JASSALLOC(JASSDICT);
    item->value.constant = token->flags & TF_CONSTANT;
    item->value.array = token->flags & TF_ARRAY;
    if(token->flags & TF_AUTOTYPE){
        //nothing to do
        //type will be set in jass_set_value
        item->value.type = find_type(j, token->primary);
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
    LPJASSTYPE type = JASSALLOC(JASSTYPE);
    type->name = token->primary;
    type->inherit = find_type(j, token->secondary);
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
    if ((v = find_dict(j->globals, token->secondary))) {
        if (token->index) {
            return jass_set_array_value(j, v, token->index, token->stmt);
        } else {
            return jass_set_value(j, v, token->stmt);
        }
    } else if ((v = find_dict(jass_stackvalue(j, 0)->env.locals, token->secondary))) {
        if (token->index) {
            return jass_set_array_value(j, v, token->index, token->stmt);
        } else {
            return jass_set_value(j, v, token->stmt);
        }
    } else {
        fprintf(stderr, "Can't find variable %s\n", token->primary);
    }
}

TOKENFUNC(VARDECL) {
    assert(token->stmt);
    LPJASSDICT vardecl = parse_dict(j, token);
    ADD_TO_LIST(vardecl, jass_stackvalue(j, 0)->env.locals);
}

TOKENFUNC(GLOBAL) {
    LPJASSDICT global = parse_dict(j, token);
    ADD_TO_LIST(global, j->globals);
}

TOKENFUNC(FUNCTION) {
    LPJASSFUNC func = JASSALLOC(JASSFUNC);
    func->name = token->primary;
    func->code = token->stmt;
    func->returns = find_type(j, token->secondary);
    assert(func->returns);
    FOR_EACH_LIST(TOKEN, arg, token->params) {
        LPJASSPARAM param = JASSALLOC(JASSPARAM);
        param->name = arg->secondary;
        param->type = find_type(j, arg->primary);
        assert(param->type);
        PUSH_BACK(JASSPARAM, param, func->params);
    }
    if (token->flags & TF_NATIVE) {
        LPJASSNATIVEFUNC mod = find_in_array(jass_funcs, sizeof(JASSNATIVEFUNC), func->name);
        if (mod) {
            func->nativefunc = mod->func;
        }
    }
    ADD_TO_LIST(func, j->functions);
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
    LPJASSDICT module_dict = JASSALLOC(JASSDICT);
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
    LPJASSNS ns= JASSALLOC(JASSNS);
    LPJASSNSDICT nsvar = JASSALLOC(JASSNSDICT);
    FOR_EACH_LIST(JASSDICT, var, module->exports){
        // import key1,key2,...,keyn under ns
        var->value.refcount+=1;
        nsvar->value = var->value;
        nsvar->key=strdup(var->key);
        nsvar->ns = strdup(namespace);
        PUSH_BACK(JASSNSDICT, nsvar, ns->vars);
    }
    ns->module = module;
    PUSH_BACK(JASSNS, ns, j->import_ns);
    PUSH_BACK(JASSDICT, module->exports, jass_stackvalue(j, 0)->env.locals);
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
    LPJASSDICT default_export = JASSALLOC(JASSDICT);
    default_export->key = "default";
    default_export->value = *(jass_topvalue(j)); // 假设默认导出在栈顶
    jass_pop(j, 1);

    PUSH_BACK(JASSDICT, default_export, j->this_module->exports);
}
TOKENFUNC(EXPORT_ADD_ENTRY) {
    assert(token->primary);
    assert(token->stmt);

    LPCSTR var_name = token->primary;
    eval_SINGLETOKEN(j, token->stmt);
    LPJASSVAR var = find_dict(jass_stackvalue(j, 0)->env.locals, var_name);
    if (!var) {
        fprintf(stderr, "Cannot export '%s': not found in globals\n", var_name);
        return;
    }

    // 添加到模块的导出表
    LPJASSDICT export_entry = JASSALLOC(JASSDICT);
    export_entry->key = var_name;
    export_entry->value = *var; 

    PUSH_BACK(JASSDICT, export_entry, j->this_module->exports);
}

TOKENFUNC(EXPORT_ALL_ENTRIES){
    // 命名空间导出
    LPJASSDICT export_entry = JASSALLOC(JASSDICT);
    export_entry->key = token->primary;  // namespace name
    export_entry->value.type = find_type(j, "namespace");
    jass_setnull(&export_entry->value);
    
    // 添加到全局导出表
    PUSH_BACK(JASSDICT, export_entry, j->globals);
}

TOKENFUNC(EXPORT_ENTRY_LIST){
    // 将所有导出的变量添加到导出表中
    FOR_EACH_LIST(TOKEN, export_item, token->args) {
        LPJASSDICT entry = JASSALLOC(JASSDICT);
        entry->key = export_item->primary;  // export name
        
        // 如果有别名，使用别名作为key
        if (export_item->secondary) {
            entry->key = export_item->secondary;// export {primary as secondary}
        }
        entry->value = *find_dict(jass_stackvalue(j, 0)->env.locals, entry->key);
        entry->value.refcount++;
        
        // 添加到全局导出表
        PUSH_BACK(JASSDICT, entry, j->this_module->exports);
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

BOOL jass_dobuffer(LPJASS j, LPSTR buffer2,LPCSTR fileName) {
    LPSTR buffer = buffer2;
    vmext_skipbom(buffer);
    LPTOKEN program = JASS_ParseTokens(&MAKE(PARSER, 
        .buffer = buffer, 
        .delimiters = JASS_DELIM,
        .file = fileName,
        .line = 1,
        .column = 1,
    ));
//    VM_Compile(program);
    eval_TOKENS(j, program);
    return true;
}
LPJASS jass_newstate() {
    LPJASS j = JASSALLOC(JASS);
    j->stack_pointer = j->stack;

    LPJASSMODULE module = JASSALLOC(JASSMODULE);
    module->evaluating = true;
    module->loaded = true;
    module->exports = JASSALLOC(JASSDICT);
    module->name = NULL;
    module->file = NULL;
    module->state = j;
    j->this_module = module;
    return j;
}

LPJASS jass_newstate2(LPJASSMODULE module) {
    LPJASS j = JASSALLOC(JASS);
    j->stack_pointer = j->stack;
    j->this_module = module;
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
    module = JASSALLOC(JASSMODULE);
    module->name = strdup(search);
    module->state = jass_newstate2(module);
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

BOOL jass_dofile(LPJASS j, LPCSTR fileName) {
    j->this_module->file = fileName;
    LPSTR buffer = vmext_readalltext(fileName);
    if (buffer) {
        BOOL success = jass_dobuffer(j, buffer,fileName);
        vmext_free(buffer);
        return success;
    } else {
        fprintf(stderr, "vm_dofile: Invalid file %s\n", fileName);
        return false;
    }
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

DWORD jass_call(LPJASS j, DWORD args) {
    LPJASSVAR root = &j->stack[j->num_stack - args - 1];
    LPJASSVAR old_stack_pointer = j->stack_pointer;
    DWORD ret = 0;
    j->stack_pointer = &j->stack[j->num_stack - args - 1];
#ifdef DEBUG_JASS
    depth++;
#endif
    if (jass_getvarbasetype(root) == jasstype_cfunction) {
        LPNATIVEFUNC func = *(LPNATIVEFUNC *)root->value;
#ifdef DEBUG_JASS
        jass_dumpstack(j);
#endif
        ret = func(j);
    } else {
        LPCJASSFUNC func = root->value;
        LPJASSDICT locals = NULL;
        DWORD argnum = 1;
// #ifdef DEBUG_JASS
//         printf("%s\n", func->name);
// #endif
        FOR_EACH_LIST(JASSPARAM, arg, func->params) {
            LPJASSDICT local = JASSALLOC(JASSDICT);
            local->key = arg->name;
            local->value.type = arg->type;
            jass_copy(j, &local->value, &j->stack_pointer[argnum]);
            PUSH_BACK(JASSDICT, local, locals);
            argnum++;
        }
//        printf("%s\n", func->name);
        root->env.done = false;
        root->env.returnstack = -1;
        root->env.locals = locals;
        eval_TOKENS(j, func->code);
        if (root->env.returnstack != -1) {
            ret = j->num_stack - root->env.returnstack;
        }
    }
    LPJASSVAR last = &j->stack[j->num_stack - ret];
    for (LPJASSVAR it = root; it < last; it++) jass_setnull(it);
    memmove(root, last, ret * sizeof(JASSVAR));
    j->num_stack -= last - root;
    j->stack_pointer = old_stack_pointer;
#ifdef DEBUG_JASS
    depth--;
#endif
    return ret;
}

void jass_dumpstack(LPJASS j) {
    fprintf(stdout, "Stack dump (size=%d):\n", j->num_stack);
    FOR_LOOP(i, j->num_stack) {
        LPCJASSVAR var = &j->stack[i];
        fprintf(stdout, "  %s%p [%d] type=%s value= ",&j->stack[i]==j->stack_pointer?">":" ", &j->stack[i], i, var->type->name);
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
            case jasstype_code:
                fprintf(stdout, "code %s", var->value ? ((LPJASSFUNC)var->value)->name : "<null>");
                break;
            case jasstype_type:
                 fprintf(stdout, "type %s", var->value ? ((LPJASSTYPE)var->value)->name : "<null>");
                break;
            case jasstype_cfunction: {
                LPCSTR name = cfunction_getname(var->value ? *(LPNATIVEFUNC *)var->value : NULL);
                if (name) {
                    fprintf(stdout, "cfunction %s", name);
                } else  {
                    fprintf(stdout, "cfunction %p", var->value ? *(LPNATIVEFUNC *)var->value : NULL);
                }
                break;
            }
            default:
                fprintf(stdout, "<unknown>");
                break;
        }
        if (var->_array) {
            fprintf(stdout, " [");
            FOR_EACH_LIST(JASSARRAY, item, var->_array) {
                fprintf(stdout, " %d=", item->index);
                JASSVAR tmp_stack[1];
                memcpy(&tmp_stack[0], &item->value, sizeof(JASSVAR));
                jass_dumpstack(&MAKE(JASS,
                    .stack_pointer = tmp_stack,
                    .num_stack = 1,
                ));
            }
            fprintf(stdout, " ]");
        }
        fprintf(stdout, "\n");
    }
}

void jass_callbyname(LPJASS j, LPCSTR name, BOOL async) {
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
        jass_call(j, 0);
    }
}
unsigned long hash_str(const char *str) {
    unsigned long hash = 5381;  // 初始值
    int c;

    while ((c = *str++))
        hash = ((hash << 5) + hash) + c; // hash * 33 + c

    return hash;
}
