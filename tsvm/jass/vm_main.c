// file vm_main.c
#include "../shared.h"
#include "api_macros.h"
#include "vm_public.h"
#include "vm_ext.h"
#include "jass_parser.h"
#include "../parser.h"
#include "zhash.h"
#include <StormPort.h>
#include <asm-generic/errno.h>
#include <locale.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <ctype.h>
#include <assert.h>
#include <strings.h>
#include <sys/types.h>
#include <vm_priv.h>
#include "string_utils.h"
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

static LPHASHTABLE g_module_cache = NULL; // 所有已加载模块链表
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
    { NULL, NULL, "typecode",jasstype_typecode }, // function ref
    { NULL, NULL, "object",jasstype_object }, // javascript object, {propk:propv,...}
    { NULL, NULL, "auto",jasstype_auto },
};
BOOL jass_dofile_alias(LPJASS j, LPCSTR fileName,LPCSTR alias);
static LPJASSVAR jass_stackvalue(LPJASS j, int index);
static JASSTYPEID jass_getvarbasetype(LPCJASSVAR var);
static DWORD jass_dotoken(LPJASS j, LPCTOKEN token);
static LPJASSMODULE gcache_find_module(LPCSTR file);
void jass_pop(LPJASS j, DWORD count);
void jass_copy(LPJASS j, LPJASSVAR var, LPCJASSVAR other);
static LPJASSVAR find_var(LPJASS j,LPCSTR name);
unsigned long hash_str(const char *str);
void jass_setnull(LPJASSVAR var);
static void export_var(LPJASS j,LPCSTR name);
void eval_FUNCTION(LPJASS j, LPCTOKEN token);
DWORD jass_doonstackfunction_args(LPJASS j,LPCTOKEN tkargs);
DWORD jass_dofunction_args(LPJASS j,LPCJASSFUNC f, LPCTOKEN tkargs);
LPCJASSTYPE get_base_type(LPCJASSTYPE type) ;
void eval_TOKENS(LPJASS j, LPCTOKEN token);

LPJASSDICT alloc_dict(){
    LPJASSDICT dict= JASSALLOC(dict,JASSDICT);
    return dict;
}

LPJASSOBJECT alloc_obj(){
    LPJASSOBJECT obj = ALLOCZ(obj, JASSOBJECT);
    obj->ht = zcreate_hash_table();
    return obj;
}


BOOL atob(LPCSTR str) {
    return !strcmp(str, "true");
}
//unary minus
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
        case jasstype_typecode:
        case jasstype_function: return !memcmp(a->value, b->value, sizeof(HANDLE));
        case jasstype_object:
        case jasstype_handle: return a->value==b->value;
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
    int vi = jass_checkboolean(j, 1) ? 2 : 3;
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
DWORD __export(LPJASS j) {
    LPCSTR uri =  jass_checkstring(j, 1);
    if(!!strcmp(uri,"<this_module>")){
        jass_loadmodule(j, uri);
    }
    LPCJASSVAR var =  jass_stackvalue(j,2);
    LPCSTR name =  jass_checkstring(j, 3);
    LPJASSDICT entry  = alloc_dict();
    entry->value.type = find_typebyid(var->type->typeid);
    entry->key = strdup(name);
    jass_copy(j, &entry->value, var);
    assert(!!strcmp( entry->key,"auto"));
    ADD_TO_LIST(entry, j->this_module->exports);
    return 0;
}
static DWORD __enosuchfunction(LPJASS j) {
    fprintf(stderr, "\nExit due to calling undefined function.\n");
    jass_dumpenv(j);
    exit(1);
}
static DWORD __evalprog(LPJASS j) {
    j->base_sp = j->stack_pointer;
    DWORD old_stack = j->num_stack;
    LPCTOKEN var = jass_checkhandle(j, 1, "handle");
    eval_TOKENS(j, var);
    return j->num_stack - old_stack;
}
NATIVE corecfuncs[] = {
    {"__add",__add},
    {"__sub",__sub},
    {"__mul",__mul},
    {"__div",__div},
    {"__ne",__ne},
    {"__eq",__eq},
    {"__ge",__ge},
    {"__le",__le},
    {"__gt",__gt},
    {"__lt",__lt},
    {"__and",__and},
    {"__or",__or},
    {"__unm",__unm},
    {"__not",__not},
    {"__cond",__cond},
    {"__typeofx",__typeofx},
    {"__export",__export},
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
        return 0;
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
    fprintf(stdout, "call: %s at %s\n", j->context.func->name, JASS_DumpLocation(j->context.func->code->sref));
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
    fprintf(stdout, "call: %s at %s\n", j->context.func->name, JASS_DumpLocation(j->context.func->code->sref));
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
static LPCNATIVE find_cfunction(LPCJASS j, LPCSTR name) {
    return zhash_get(j->g_shared_natives, (LPSTR)name);
}
static LPCJASSFUNC find_function_constructor(LPJASS j, LPCSTR name) {
    int len = strlen(name);
    FOR_EACH_LIST(JASSFUNC, func, j->this_module->functions) {
        if(len-strlen(func->name)!=12){
            continue;
        }
        if (!strncmp(func->name, name,strlen(name))) {
            if(!strcmp(func->name+strlen(name),".constructor"))
                return func;
            if(!strcmp(func->name+strlen(name),"_constructor"))
                return func;
        }
    }
    // LPJASSDICT dict= find_dictvar(j,name,1);
    // if(dict && dict->value.type->typeid==jasstype_function){
    //     return (LPCJASSFUNC)dict->value.value;
    // }
   
    printf("INFO: find_function_constructor `%s` results null.\n",name);
    return NULL;
}
static LPCJASSFUNC find_function(LPJASS j, LPCSTR name) {
    FOR_EACH_LIST(JASSFUNC, func, j->this_module->functions) {
        if (!strcmp(func->name, name)) {
            return func;
        }
    }
    FOR_EACH_LIST(JASSFUNC, func, j->main_module->functions) {
        if (!strcmp(func->name, name)) {
            return func;
        }
    }
     FOR_EACH_LIST(JASSFUNC, func, j->native_functions) {
        if (!strcmp(func->name, name)) {
            return func;
        }
    }
    // LPJASSDICT dict= find_dictvar(j,name,1);
    // if(dict && dict->value.type->typeid==jasstype_function){
    //     return (LPCJASSFUNC)dict->value.value;
    // }
   
    printf("INFO: find_function `%s` results null.\n",name);
    return NULL;
}
static LPJASSVAR find_objvar(LPJASS j, LPCSTR name) {
    FOR_EACH_LIST(JASSIMPORTED, item, j->this_module->imports) {
        if (!strcmp(item->key, name)) {
            return item->var;
        }
    }
   
    printf("INFO: find_object `%s` results null.\n",name);
    return NULL;
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
        ADD_TO_LIST(curr,j->this_module->functions);
        curr = curr->next;
    }
}

LPCJASSTYPE find_typebyid(JASSTYPEID id) {
    FOR_LOOP(i, sizeof(jass_types)/sizeof(*jass_types)) {
        if (jass_types[i].typeid==id) {
            return &jass_types[i];
        }
    }
    return NULL;
}
static LPCJASSFUNC evalprog = &VMFUNC(__evalprog, jasstype_nothing);
static LPCJASSFUNC enosuchfunc = &VMFUNC(__enosuchfunction, jasstype_nothing);
static LPCJASSFUNC export = &VMFUNC(__export, jasstype_nothing);
void jass_register_type(LPSTR typename,LPSTR ctorname, LPHASHTABLE table) {
    if(zhash_exists(table, typename)){
        printf("WARN: jass_register_type override `%s`\n",typename);
    }
    zhash_set(table, typename, ctorname);
}
void jass_register_natives(LPNATIVE cfuncs,LPHASHTABLE table) {
    for (LPNATIVE m = cfuncs; m->name; m++) { 
        if(zhash_exists(table, m->name)){
            printf("WARN: jass_register_natives override `%s`\n",m->name);
        }
        else{
            printf("INFO: jass_register_natives insert `%s`\n",m->name);
        }
        zhash_set(table, m->name, m);
    }
}
static LPJASSVAR find_dict(LPJASSDICT dict, LPCSTR name) {
    FOR_EACH_LIST(JASSDICT, item, dict) {
        if (!strcmp(item->key, name)) {
            return &item->value;
        }
    }
    return NULL;
}
static LPJASSIMPORTED alloc_imported(LPJASSMODULE module_from, LPCSTR name,DWORD type) {
    if(type==IMPORTED_VAR){
        FOR_EACH_LIST(JASSDICT, item, module_from->exports) {
            if (!strcmp(item->key, name)) {
                LPJASSIMPORTED locator  = ALLOCZ(locator,JASSIMPORTED);
                locator->key = name;
                locator->module = module_from;
                locator->var = &item->value;
                locator->importtype = type;
                return locator;
            }
        }
        return NULL;
    }
    else if(type==IMPORTED_DEFAULT){
        LPJASSDICT single = module_from->exports;
        assert(!single->next);
        LPJASSIMPORTED locator  = ALLOCZ(locator,JASSIMPORTED);
        locator->key = name;
        locator->module = module_from;
        locator->var = &single->value;
        locator->importtype = type;
        return locator;
    }
    else{
         // import all
        LPJASSIMPORTED locator  = ALLOCZ(locator,JASSIMPORTED);
        locator->key = name;
        locator->module = module_from;
        locator->importtype = IMPORTED_OBJ;
        return locator;
    }
}

static LPJASSMODULE gcache_find_module(LPCSTR file){
    if(!g_module_cache)
        g_module_cache = zcreate_hash_table();
    return zhash_get(g_module_cache, (LPSTR)file);
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

BOOL jass_checkvartype(LPCJASSVAR var, JASSTYPEID typeid) {
    return jass_getvarbasetype(var) == typeid;
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
        case jasstype_object:
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
                    assert(0);
                }
                var->type = other->type;
                var->value = other->value;
                var->refcount = other->refcount;
                if (var->refcount) {
                    (*var->refcount)++;
                }
                break;
            case jasstype_object:
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
            case jasstype_typecode:
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
DWORD jass_pushobject(LPJASS j, LPJASSOBJECT value) {
    JASS_ADD_STACK(j, var, jasstype_object);
    jass_setnull(var);
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
DWORD jass_pushtype(LPJASS j, LPCSTR value) {
    int len = strlen(value);
    JASS_ADD_STACK(j, var, jasstype_typecode);
    JASS_SET_VALUE(var, value, len+1);
    ((LPSTR)var->value)[len] = '\0';
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
    if (jass_checkvartype(var, jasstype_real)) {
        return var->value ? *(FLOAT *)var->value : 0;
    }
    if (jass_checkvartype(var, jasstype_integer)) {
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
    LPCJASSTYPE t = NULL;
    assert(token->primary);
    if(!strcmp("vec4.clone", token->primary)){
        printf("dbg");
    }

    // Explict a function 
    if (token->flags & TF_FUNCTION) {
        if ((f = find_function(j, token->primary))) {
            return jass_pushfunction(j, f);
        } else {
            assert(0);
            return jass_pushtype(j,NULL);
        }
    }
    else if ((v = find_var(j,token->primary))) {
        if(v->type->typeid>1000 || !v->type->name){
            jass_dumpenv(j);
            v = find_var(j,token->primary);
        }
        return jass_pushvalue(j, v);
    }
    else if((t = find_type(j, token->primary))){ // types are declared in vminit.jass
        // find the type declared in native c
        if(!zhash_exists(j->g_shared_types, token->primary)){
            printf("find the type but mappingless `%s` at %s\n",token->primary,JASS_DumpLocation(token->sref));
            return jass_pushtype(j,token->primary);
        }
        
        LPSTR ctorname;
        if((ctorname = zhash_get(j->g_shared_types, token->primary))){
            // type has a constructor
            if ((f = find_function(j, ctorname))) {
                return jass_pushfunction(j, f);
            } else {
                printf("find the function but ctorless `%s` at %s\n",token->primary,JASS_DumpLocation(token->sref));
                return jass_pushtype(j,token->primary);
            }
        }
        else{
            printf("only find a script-scope type `%s` at %s\n",token->primary,JASS_DumpLocation(token->sref));
            return jass_pushtype(j,token->primary);
        }
    }
    else {
        // Not in a function call, use function as ref
        if((f = find_function(j, token->primary))){
            return jass_pushfunction(j, f);
        }
        jass_dumpenv(j);
        printf("Access undefined identifier `%s` at %s\n",token->primary,JASS_DumpLocation(token->sref));
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
    if(!strcmp(token->primary, "vec3.fromValues")){
        printf("debg");
    }
    if(token->flags& TF_CALLONSTACK){
        f = jass_checkcode(j,-1);
        assert(strcmp("<f_onstack>", token->primary)==0);
        DWORD ret= jass_doonstackfunction_args(j, token);

        return ret;
    }
    else if (!strcmp(token->primary, "CommentString") && token->args) {
        fprintf(stdout, "%s\n", token->args->primary);
        return 0;
    }
    else if((f=find_function(j, token->primary))){
        return jass_dofunction_args(j, f,token);
    }
    else{
        LPCJASSVAR var= find_var(j, token->primary);
        if(var && var->type->typeid==jasstype_function){
            f = var->value;
            return jass_dofunction_args(j, f,token);
        }
        else{
            jass_dumpenv(j);
            jass_dumpstack(j);
            fprintf(stderr, "Can't find function %s\n", token->primary);
            assert(false);
            return 0;
        }
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
    LPCTOKEN target = token;
    if(target->ttype==TT_FUNCTION){
        eval_FUNCTION(j, target);
        return token->flags & TF_ANONYMOUS?1:0;
    }
    FOR_LOOP(idx, sizeof(token_types)/sizeof(*token_types)) {
        if (token_types[idx].tokentype == target->ttype) {
            return token_types[idx].func(j, target);
        }
    }
    return 0;
}

static void jass_set_value(LPJASS j, LPJASSVAR dest, LPCTOKEN init) {
    if(init->ttype == TT_FUNCTION){
        ((LPTOKEN)init)->flags |= TF_PUSHSTACK;
    }
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
    //token->flags & TF_SETVALUE && 
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
    // LPCNATIVE func =  find_cfunction(j, token->primary);
    // type->f = func;
    if(!zhash_exists(j->g_shared_types, token->primary)){
        zhash_set(j->g_shared_types, token->primary, NULL/*ctor_function_name*/);
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

    // set value of exists var
    if ((v = find_var(j, token->secondary))) {
        if (token->index) {
            return jass_set_array_value(j, v, token->index, token->stmt);
        } else {
            return jass_set_value(j, v, token->stmt);
        }
    }
    else if(token->stmt){
        // Declare to a local var
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
    if(token->flags & TF_VARLIST){ // var a,b,c
        assert(token->next);
        FOR_EACH_LIST(TOKEN, item, token->next){
            vardecl = parse_dict(j, item);
        }
    }
    
    ADD_TO_LIST(vardecl, jass_stackvalue(j, 0)->env.locals);
}

TOKENFUNC(GLOBAL) {
    LPJASSDICT global = parse_dict(j, token);
    ADD_TO_LIST(global, j->this_module->globals);
}

TOKENFUNC(FUNCTION) {
    LPJASSFUNC func  = JASSALLOC(func , JASSFUNC);
    func->params = NULL;
    func->name = token->primary;
    func->code = token->stmt;
    assert(token->secondary);
    func->returns = find_type(j, token->secondary);
    func->codefile = token->sref->file;
    if(!func->returns){
        jass_dumpenv2txt(j);
    }
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
        LPCNATIVE cfunction = find_cfunction(j, func->name);
        assert(cfunction);
        func->f = cfunction->f;
        // todo jassfreetoken
        func->code = NULL;
    }
    // // var a = function(){}()()...
    if(token->flags & TF_ANONYMOUS){
        jass_pushfunction(j, func);
    }
    else if(func->code){
        ADD_TO_LIST(func, j->this_module->functions);
    }
    else{
        ADD_TO_LIST(func, j->native_functions);
    }
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
            }
            else if(tok->ttype==TT_BREAK){
                return;
            }
            else if(tok->ttype==TT_WHILE){
                jass_dotoken(j, tok->loop->condition);
                if (jass_popboolean(j)) {
                    return;
                }
            }
            else {
                eval_SINGLETOKEN(j, tok);
            }
        }
        assert(i < INF_LOOP_PROTECTION);
    }
}


TOKENFUNC(IMPORT_RUNONLY){
    assert(0);
}

// import xxx from 'module'
TOKENFUNC(IMPORT_DEFAULT_ENTRY){
    assert(token->primary);
    LPCSTR search = token->primary;  // 模块路径
    LPJASSMODULE module = jass_loadmodule(j, search);
    if (!module) {
        fprintf(stderr, "Failed to load module: %s\n", search);
        return;
    }
    assert(token->secondary);
    LPJASSIMPORTED entry = alloc_imported(module,token->secondary,IMPORTED_DEFAULT);
    assert(entry);
    PUSH_BACK(JASSIMPORTED, entry, j->this_module->imports);
    // zhash_set(j->this_module->importedvars, item->primary, entry->var);
}

TOKENFUNC(IMPORT_ALL_ENTRIES) {
     // import * as Namespace from 'module'
    LPSTR search = token->primary;
    LPSTR objalias = token->secondary;
    if(!strcmp(search, "./quat.js")){
        printf("dbg");
    }
    LPJASSMODULE module = jass_loadmodule(j, search);
    assert (module);

    LPJASSIMPORTED imported = alloc_imported(module, objalias,IMPORTED_OBJ);
    
    PUSH_BACK(JASSIMPORTED, imported, j->this_module->imports);

   
    int num = 0;
    FOR_EACH_LIST(JASSDICT, dict, module->exports){
        // // import key1,key2,...,keyn under ns
        // // LPJASSIMPORTED entry = alloc_imported(module, var->key);
        // // update entry's key to qualified
        size_t ns_len = strlen(objalias);
        size_t key_len = strlen(dict->key);
        char *qualified_key = vmext_alloc(ns_len + 1 + key_len + 1); // ns.key\0
        sprintf(qualified_key, "%s.%s", objalias, dict->key);
        // // entry->key = qualified_key;
        // // PUSH_BACK(JASSIMPORTED, entry, j->this_module->imports);
        
        // LPJASSVAR item;
        // jass_copy(j, item, &dict->value);
        zhash_set(j->this_module->importedvars, qualified_key, &dict->value);
        num++;
    }
    LPJASSOBJECT obj = alloc_obj();
    obj->code = token;
    obj->num_props = num;
    obj->props = vmext_alloc(sizeof(JASSDICT) * num);
    obj->ht = zcreate_hash_table();
    num=0;
    FOR_EACH_LIST(JASSDICT, dict, module->exports){
        obj->props[num] = dict;
        zhash_set(obj->ht, (LPSTR)dict->key, &dict->value);
        num+=1;
    }
    jass_pushobject(j, obj);
    LPJASSVAR var = ALLOCZ(var, JASSVAR);
    var->type = find_typebyid(jasstype_object);
    jass_copy(j,var, jass_topvalue(j));
    jass_pop(j, 1);
    imported->var =var;
    imported->obj = obj;
    zhash_set(j->this_module->importedvars, objalias, var);
    assert(num>0);
}

// import { export1, export2 } from 'xxx'
TOKENFUNC(IMPORT_ENTRY_LIST) {
    LPCSTR module_name = token->primary;  // 模块路径
    
    LPJASSMODULE module = jass_loadmodule(j, module_name);
    if (!module) {
        fprintf(stderr, "Failed to load module: %s\n", module_name);
        return;
    }
    assert(token->args);
    FOR_EACH_LIST(TOKEN, item, token->args){
        LPJASSIMPORTED entry = alloc_imported(module,item->primary,IMPORTED_VAR);
        assert(entry);
        PUSH_BACK(JASSIMPORTED, entry, j->this_module->imports);
        zhash_set(j->this_module->importedvars, item->primary, entry->var);
    }
}

TOKENFUNC(EXPORT_DEFAULT_ENTRY) {
    assert(0);
    LPJASSDICT default_export  = alloc_dict();
    default_export->key = "default";
    default_export->value = *(jass_topvalue(j)); // 假设默认导出在栈顶
    jass_pop(j, 1);
    
    PUSH_BACK(JASSDICT, default_export, j->this_module->exports);
}
TOKENFUNC(EXPORT_ADD_ENTRY) {
    // layout: 
    //  - token.stmt: declaring vars target of exports and declare module uri
    //  - token.next: call __export(uri,varname,nullable alias)
    //  - token.next.next ...: call __export(uri,varname,nullable alias)
    assert(j->num_stack>0);
    LPTOKEN vars = token->stmt; 
    // declare varsvb                            
    eval_TOKENS(j,vars); 
    // call export
    // eval_TOKENS(j,token->next);
    
    // jass_dumpenv(j);
}

TOKENFUNC(EXPORT_ALL_ENTRIES){
    // 命名空间导出
    LPJASSDICT export_entry  = alloc_dict();
    export_entry->key = token->primary;  // namespace name
    export_entry->value.type = find_type(j, "namespace");
    jass_setnull(&export_entry->value);
    
    // 添加到全局导出表
    PUSH_BACK(JASSDICT, export_entry, j->this_module->globals);
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
        
        DWORD old_stack = j->num_stack;
        eval_SINGLETOKEN(j, var->stmt);
        jass_pushfunction(j, j->fn_export);
        jass_pushstring(j, var_name);
        DWORD stack = jass_call(j, 1);
        jass_pop(j, stack);
        assert(j->num_stack==old_stack);
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
    j->evaluting = srcfilename;
    jass_pushfunction(j, j->fn_evalprog);
    jass_pushhandle(j, program, "handle");
    jass_call(j, 1);
    j->evaluting = NULL;
    return true;
}


LPJASSMODULE jass_newmodule(LPCSTR filname,LPCSTR alias) {
    LPJASSMODULE module  = JASSALLOC(module , JASSMODULE);
    module->evaluating = true;
    module->loaded = true;
    module->exports = NULL;
    module->displayname = alias;
    module->filename = filname;
    module->initiator = NULL;
    module->importedvars = zcreate_hash_table();
    return module;
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
static LPHASHTABLE g_jass_natives;
static LPHASHTABLE g_jass_types;
LPJASS jass_newstate(LPJASSMODULE module) {

    LPJASS j  = JASSALLOC(j , JASS);
    j->stack_pointer = j->stack;
    j->this_module = module;
    j->main_module = module;
    j->fn_enosuch = enosuchfunc;
    j->fn_export = export;
    j->fn_evalprog = evalprog;

    if(!g_jass_natives){
        g_jass_natives = zcreate_hash_table();
        g_jass_types = zcreate_hash_table();
        jass_register_natives(corecfuncs,g_jass_natives);
        jass_register_Math(g_jass_natives,g_jass_types);
        jass_register_Array(g_jass_natives,g_jass_types);
        printf("INFO: jass_register_natives added %ld entries\n",g_jass_natives->entry_count);
    }

    j->g_shared_natives = g_jass_natives;
    j->g_shared_types = g_jass_types;
    // LPCNATIVE entry= find_cfunction(j,"Array");
    // if(!!strcmp(entry->name,"Array")){
    //     printf("vm internal error");
    //     exit(1);
    // }
    
    if(!j->this_module){
        j->this_module = jass_newmodule(vminitscript,vminitscript);
        j->main_module = j->this_module;
        jass_dofile_alias(j,j->this_module->filename,j->this_module->filename);
    }
    return j;
}
DWORD eval_module(LPJASS j){
    return 0;
}

LPJASSMODULE jass_loadmodule(LPJASS j,LPCSTR search) {
    LPSTR path = vmext_resolvepath(search,j->this_module->filename);
    if(!path){
        fprintf(stderr, "Can't resolve module: %s\n", search);
        exit(1);
    }
    printf("start jass_loadmodule %s \n\t\tby %s\n",search,j->this_module->displayname);
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
    module->displayname = search;
    module->exports = NULL;
    module->loaded = false;
    module->evaluating = true;
    module->initiator = j->this_module->filename;
    module->filename = path;
    module->importedvars = zcreate_hash_table();

    zhash_set(g_module_cache, (LPSTR)module->filename, module);

    // 3. 读取并执行模块代码
    LPJASSMODULE old_module = j->this_module;
    j->this_module = module;

    if (!jass_dofile(j, path)) {
        fprintf(stderr, "Failed to execute module: %s\n", search);
        assert(0);
        return NULL;
    }
    PUSH_BACK(JASSMODULE, module, j->evaluted);
    j->this_module = old_module;

    module->loaded = true;
    module->evaluating = false;

    printf("jass_loadmodule success: %s\n\n",module->filename);
    return module;
}
static void jass_deletemodule(LPJASSMODULE module){
    // 清理导出表
    SAFE_DELETE(module->exports, jass_deletedict);

    vmext_free(module);
}
void jass_unloadmodule(LPJASSMODULE module) {
    if (!module) return;
    // 从全局缓存中移除
    zhash_delete(g_module_cache, (LPSTR)module->filename);
    jass_deletemodule(module);
}

void jass_close(LPJASS j) {
    vmext_free(j);
}

#define EXTRACT_DIR "build"



BOOL jass_dofile_alias(LPJASS j, LPCSTR fileName,LPCSTR src_alias){
    LPSTR buffer = vmext_readalltext(fileName);
    if (buffer) {
        BOOL success = jass_dobuffer(j, buffer,fileName,filepflags(fileName));
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

DWORD jass_doonstackfunction_args(LPJASS j, LPCTOKEN tkargs){
    DWORD args = 0;
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
    fprintf(stdout, "[vm_main.c] preparing to eval `%s` at %s", tkargs->primary, JASS_DumpLocation(tkargs->sref));
#endif
    
    if(!tkargs->primary)
        assert(0);
    return jass_call(j, args);
}

DWORD jass_dofunction_args(LPJASS j, LPCJASSFUNC f, LPCTOKEN tkargs){
    DWORD args = 0;
    // assert(!strcmp(func->name, tkargs->primary));
    jass_pushfunction(j, f);
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
    fprintf(stdout, "[vm_main.c] preparing to eval `%s` at %s", tkargs->primary, JASS_DumpLocation(tkargs->sref));
#endif
    
    if(!tkargs->primary)
        assert(0);
    return jass_call(j, args);
}

DWORD jass_call(LPJASS j, DWORD argc) {
    LPJASSVAR root = &j->stack[j->num_stack - argc - 1];
    LPJASSVAR old_stack = j->stack_pointer;
    j->caller = j->callee;
    DWORD ret = 0;
    LPCJASSFUNC func = root->value;
    j->callee = func;
    LPJASSDICT locals = NULL;
// #ifdef DEBUG_JASS_STACK
//         printf("[vm_main.c] dumpstack BEFORE_BACKUP_STACK for call:%s\n", func->name);
//         jass_dumpstack(j);
// #endif
    j->stack_pointer = &j->stack[j->num_stack - argc - 1];
    depth++;
    
    
#ifdef DEBUG_JASS_STACK
    printf("[vm_main.c] dumpstack AFTER_BACKUP_STACK before call %s: \n", func->name);
    jass_dumpstack(j);
#endif
    // 为堆栈上的实际参数设置形式参数名称并放入<locals>
    DWORD argnum = 1;
    FOR_EACH_LIST(JASSPARAM, param, func->params) {
        LPJASSDICT local  = alloc_dict();
        local->key = param->name;
        local->value.type = param->type;
        jass_copy(j, &local->value, &j->stack_pointer[argnum]);
        PUSH_BACK(JASSDICT, local, locals);
        argnum++;
    }
    root->env.done = false;
    root->env.returnstack = -1;
    root->env.locals = locals;
    if(func->f){
        ret= func->f(j);
    }
    else{
        LPJASSMODULE old_this = j->this_module;
        j->this_module = gcache_find_module(func->code->sref->file);
        assert(j->this_module);
        eval_TOKENS(j, func->code);
        j->this_module = old_this;
    }

    if (root->env.returnstack != (DWORD)-1) {
        ret = j->num_stack - root->env.returnstack;
    }
#ifdef DEBUG_JASS_STACK
    printf("[vm_main.c] dumpstack BEFORE_RESTORE_STACK after call %s: \n", func->name);
    jass_dumpstack(j);
#endif    
    LPJASSVAR last = &j->stack[j->num_stack - ret];
    for (LPJASSVAR it = root; it < last; it++) jass_setnull(it);
    memmove(root, last, ret * sizeof(JASSVAR));
    j->num_stack -= last - root;
    j->stack_pointer = old_stack;
    j->callee = j->caller;
    depth--;
#ifdef DEBUG_JASS_STACK
    printf("[vm_main.c] dumpstack AFTER_RESTORE_STACK after %s: \n", func->name);
    jass_dumpstack(j);
#endif
    return ret;
}

void jass_dumpvarf(LPJASS j,LPCJASSVAR var,FILE* f){
        switch (jass_getvarbasetype(var)) {
            case jasstype_integer:
                fprintf(f, "%d", var->value ? *(LONG *)var->value : 0);
                break;
            case jasstype_real:
                fprintf(f, "%f", var->value ? *(FLOAT *)var->value : 0.0);
                break;
            case jasstype_boolean:
                fprintf(f, "%s", var->value && *(BOOL *)var->value ? "true" : "false");
                break;
            case jasstype_string:
                fprintf(f, "\"%s\"", var->value ? (LPCSTR)var->value : "");
                break;
            case jasstype_handle:
                fprintf(f, "%p", var->value);
                break;
            case jasstype_function: {
                LPJASSFUNC func = var->value ? ((LPJASSFUNC)var->value) : NULL;
                LPCSTR name = func ? func->name : NULL;
                fprintf(f, "function `%s` %p %s", name,func,func->code?func->code->sline:"");
                break;
            }
            case jasstype_object:{
               LPJASSOBJECT obj = var->value ? ((LPJASSOBJECT)var->value) : NULL;
               fprintf(f, "object %p %s", obj,obj->code?obj->code->sline:"");
               break;
            }
            case jasstype_typecode:
                fprintf(f, "jasscode \"%s\"", var->value ? (LPCSTR)var->value : "");
                break;
            default:
                fprintf(f, "<unknown>");
                break;
        }
        if (var->array) {
            fprintf(f, " [");
            FOR_EACH_LIST(JASSARRAY, item, (LPJASSARRAY)var->value) {
                fprintf(f, " %d=", item->index);
                JASSVAR tmp_stack[1];
                memcpy(&tmp_stack[0], &item->value, sizeof(JASSVAR));
                // jass_dumpstack(&MAKE(JASS,
                //     .stack_pointer = tmp_stack,
                //     .num_stack = 1,
                // ));
            }
            fprintf(f, " ]");
        }
        fprintf(f, "\n");
}

void jass_dumpimportedvar(LPJASS j,LPJASSIMPORTED imported,FILE* f){
    LPJASSVAR var = imported->var;
    switch (jass_getvarbasetype(var)) {
        case jasstype_integer:
            fprintf(f, "%d %s", var->value ? *(LONG *)var->value : 0,imported->module->filename);
            break;
        case jasstype_real:
            fprintf(f, "%f %s", var->value ? *(FLOAT *)var->value : 0.0,imported->module->filename);
            break;
        case jasstype_boolean:
            fprintf(f, "%s %s", var->value && *(BOOL *)var->value ? "true" : "false",imported->module->filename);
            break;
        case jasstype_string:
            fprintf(f, "\"%s\" %s", var->value ? (LPCSTR)var->value : "",imported->module->filename);
            break;
        case jasstype_handle:
            fprintf(f, "%p %s", var->value,imported->module->filename);
            break;
        case jasstype_function: {
            LPJASSFUNC func = var->value ? ((LPJASSFUNC)var->value) : NULL;
            LPCSTR name = func ? func->name : NULL;
            fprintf(f, "function `%s` %p %s %s", name,func,func->code?func->code->sline:"",imported->module->filename);
            break;
        }
        case jasstype_object:{
           LPJASSOBJECT obj = var->value ? ((LPJASSOBJECT)var->value) : NULL;
           fprintf(f, "object %p %s %s", obj,obj->code?obj->code->sline:"",imported->module->filename);
           break;
        }
        default:
            fprintf(f, "<unknown> %s",imported->module->filename);
            break;
    }
    if (var->array) {
        fprintf(f, " [");
        FOR_EACH_LIST(JASSARRAY, item, (LPJASSARRAY)var->value) {
            fprintf(f, " %d=", item->index);
            JASSVAR tmp_stack[1];
            memcpy(&tmp_stack[0], &item->value, sizeof(JASSVAR));
            // jass_dumpstack(&MAKE(JASS,
            //     .stack_pointer = tmp_stack,
            //     .num_stack = 1,
            // ));
        }
        fprintf(f, " ]");
    }
    fprintf(f, "\n");
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
                fprintf(stdout, "function `%s` %p %s", name,func,func->code?func->code->sline:"");
                break;
            }
            case jasstype_object:{
               LPJASSOBJECT obj = var->value ? ((LPJASSOBJECT)var->value) : NULL;
               fprintf(stdout, "object %p %s", obj,obj->code?obj->code->sline:"");
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

static LPJASSVAR find_var(LPJASS j,LPCSTR name) {
    LPCSTR objname = NULL;
    if(!!(objname=strchr(name, '.'))){
        LPJASSVAR obj =  find_objvar(j, strndup(name,objname-name));
        if(obj){
            // ht.key is qualified key
            // ht.value is typeof LPJASSVAR
            return zhash_get(((LPJASSOBJECT)obj->value)->ht, (LPSTR)objname+1); 
        }
    }

    FOR_EACH_LIST(JASSDICT, dict, jass_stackvalue(j, 0)->env.locals){
        if(!strcmp(name, dict->key)){
            return &dict->value;
        }
    }

    LPJASSVAR var = zhash_get(j->this_module->importedvars,(LPSTR)name);
    if(var){
        return var;
    }
    FOR_EACH_LIST(JASSDICT, dict, j->this_module->exports){
        if(!strcmp(name, dict->key)){
            return &dict->value;
        }
        LPCSTR typename = strchr(dict->key, '.');
        if(typename && !strcmp(name,typename)){
            
        }
    }
    // only jass module code has globals section
    FOR_EACH_LIST(JASSDICT, dict, j->main_module->globals){
        if(!strcmp(name, dict->key)){
            return &dict->value;
        }
    }
    return NULL;
}
void jass_dumpmoduleenvf(LPJASS j, LPJASSMODULE module,FILE *f){
    fprintf(f, "\n[globals]\n");
    FOR_EACH_LIST(JASSDICT, dict, module->globals){
        fprintf(f,"`%s`: ",dict->key);
        jass_dumpvarf(j, &dict->value,f);
    }
    fprintf(f, "\n[exports]\n");
    FOR_EACH_LIST(JASSDICT, dict, module->exports){
        fprintf(f,"`%s`: ",dict->key);
        jass_dumpvarf(j, &dict->value,f);
    }

    fprintf(f, "\n[imports]\n");
    FOR_EACH_LIST(JASSIMPORTED, imported, module->imports){
        fprintf(f,"`%s`: ",imported->key);
        jass_dumpimportedvar(j, imported,f);
        fprintf(f, "\n");
    }

    fprintf(f, "\n[functions]\n");
    FOR_EACH_LIST(JASSFUNC, dict, module->functions){
        fprintf(f,"`%s`\n",dict->name);
    }
    fprintf(f,"\n[typedefs]\n");
    FOR_EACH_LIST(JASSTYPE, dict, j->types){
       fprintf(f,"`%s`\n",dict->name);
    }
    LPJASSVAR zero = jass_stackvalue(j,0);
    if(zero->value == j->callee) //locals alwasy bound to the target function
    {
       fprintf(f, "\n[locals at %s %p]\n",j->callee->name,j->callee);
        FOR_EACH_LIST(JASSDICT, dict, zero->env.locals){
            fprintf(f,"`%s`: ",dict->key);
            jass_dumpvarf(j, &dict->value,f);
        }
    }
    
   fprintf(f, "\n");
}
void jass_dumpenvf(LPJASS j,FILE *f) {
    fprintf(f, "\n/////// LPJASS jass_dumpenv ///////\n");


    fprintf(f,"\n");
    fprintf(f, "- main_module: %s\n",j->main_module->filename);
    fprintf(f, "- this_module: %s\n",j->this_module->filename);
    fprintf(f, "- evaluting: %s\n",j->evaluting);

    fprintf(f, "\n=================================================\n");
    fprintf(f, "[Global shared native functions]");
    fprintf(f, "\n=================================================\n");
    FOR_EACH_LIST(JASSFUNC, dict, j->native_functions){
        fprintf(f,"`%s`\n",dict->name);
    }

    fprintf(f, "\n=================================================\n");
    fprintf(f, "[main_module] %p %s",j->main_module, j->main_module->filename);
    fprintf(f, "\n=================================================\n");
    jass_dumpmoduleenvf(j, j->main_module, f);

    FOR_EACH_LIST(JASSMODULE, module, j->evaluted){
        fprintf(f, "\n=================================================\n");
        fprintf(f, "[module] %p %s",module, module->filename);
        fprintf(f, "\n=================================================\n");
        jass_dumpmoduleenvf(j, module, f);
    }
    fprintf(f, "\n\n");
}
void jass_dumpenv2txt(LPJASS j){
    FILE* f = fopen("jass_dumpenv.txt", "w");
    jass_dumpenvf(j,f);
    fclose(f);
}
void jass_dumpenv(LPJASS j) {
    return jass_dumpenvf(j, stdout);
}
static LPCSTR prefixdump(LPJASS j,LPCJASSVAR var,LPCJASSVAR ret){
    if(j->stack_pointer==var && var==ret){
        return "<>";
    }
    else if(j->stack_pointer==var){
        return ">>";
    }
    else if(j->stack_pointer==ret){
        return "<<";
    }
    else{
        return "  ";
    }
}
void jass_dumpstackf(LPJASS j,FILE* f) {
    fprintf(stdout, "jassvm_stack_dump (num_stack=%d): sp=%p\n", j->num_stack,j->stack_pointer);
    LPCJASSVAR ret = NULL;
    DWORD max = j->num_stack;
    FOR_LOOP(i, j->num_stack) {
        LPCJASSVAR var = &j->stack[i];
        if(var->env.done){
            ret = &j->stack[ var->env.returnstack];
            if(max<var->env.returnstack){
                max = var->env.returnstack;
            }
            fprintf(f, "ret=%p\n", ret);
        }
    }
    FOR_LOOP(i,  max) {
        LPCJASSVAR var = &j->stack[i];
        fprintf(f, " %s %p [%d] type=%s value= ",
                prefixdump(j,var,ret), var, i, var->type->name);
        jass_dumpvarf(j, var,f);
    }


    LPJASSVAR zero = jass_stackvalue(j,0);
    if(zero->value == j->callee) //locals alwasy bound to the target function
    {
       fprintf(f, "\n[locals at %s %p]\n",j->callee->name,j->callee);
        FOR_EACH_LIST(JASSDICT, dict, zero->env.locals){
            fprintf(f,"`%s`: ",dict->key);
            jass_dumpvarf(j, &dict->value,f);
        }
    }
}

void jass_dumpstack(LPJASS j){
    jass_dumpstackf(j,stdout);
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
