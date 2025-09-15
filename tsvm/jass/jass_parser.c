
#include "jass_parser.h"
#include <StormPort.h>
#include <assert.h>
#include <locale.h>
#include <setjmp.h>
#include "parser.h"
#include "shared.h"
#include "string_utils.h"
#include "vm_ext.h"
#include "vm_public.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#define ALLOC(type) vmext_alloc(sizeof(type))
LPTOKEN alloc_token_here(TOKENTYPE type, LPPARSER p,LPSTR pline);
#define ALLOC_TOKEN(type,p) alloc_token_here(type,p,PARSERLINE())
#define FREE(val) SAFE_DELETE(val, vmext_free)
#define PARSER(NAME, ...) static LPTOKEN NAME(LPPARSER p, ##__VA_ARGS__)
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define PARSERLINE() "@"__FILE__ ":" TOSTRING(__LINE__)
#define SOURCELINE(file,line) "@"file ":" line
#define PARSER_THROW(...) do {\
    fprintf(stderr, __VA_ARGS__); \
    fprintf(stderr, "\n"); \
    parser_throw(p); \
    longjmp(exception_env, 1); \
}while(0)


static jmp_buf exception_env;
typedef LPTOKEN (*LPGRAMMARFUNC)(LPPARSER);

typedef struct {
    LPCSTR name;
    LPGRAMMARFUNC func;
} parseClass_t;

extern parseClass_t function_keywords[];
extern parseClass_t global_keywords[];

static LPTOKEN parse_logical_expression(LPPARSER p);
static LPTOKEN keyword_function(LPPARSER p);
static LPTOKEN statement_local(LPPARSER p);
static LPTOKEN statement_set(LPPARSER p);
static LPTOKEN read_string_token(LPPARSER p);
static LPSTR read_string_literal(LPPARSER p);
static LPTOKEN ts_right_value(LPPARSER p);
static LPTOKEN read_single_identifier(LPPARSER p);
static LPTOKEN parse_ts_function_params(LPPARSER p);
static BOOL parse_stmt(LPPARSER p, LPTOKEN function)
;

BOOL is_integer(LPCSTR tok);
BOOL is_float(LPCSTR tok);
BOOL is_identifier(LPCSTR str);
BOOL is_string(LPCSTR tok);
BOOL is_fourcc(LPCSTR tok);
DWORD is_modifier(LPCSTR str);

BOOL is_multiplicative_operator(LPCSTR str) {
    return !strcmp(str, "*") || !strcmp(str, "/");
}

BOOL is_additive_operator(LPCSTR str) {
    return !strcmp(str, "+") || !strcmp(str, "-");
}

BOOL is_compare_operator(LPCSTR str) {
    return !strcmp(str, ">") || !strcmp(str, "<") || !strcmp(str, "=") || !strcmp(str, "!");
}

BOOL is_logic_operator(LPCSTR str) {
    return !strcmp(str, "and") || !strcmp(str, "&&")|| !strcmp(str, "||") || !strcmp(str, "or") || !strcmp(str, "!");
}

BOOL is_typecheck_operator(LPCSTR str) {
    return !strcmp(str, "typeof") || !strcmp(str, "instanceOf");
}
BOOL is_assign_operator(LPCSTR str) {
    return !strcmp(str, "=") 
    || !strcmp(str, "+=") 
    || !strcmp(str, "*=") 
    || !strcmp(str, "/=")
    || !strcmp(str, "%=")
    || !strcmp(str, "<<=")
    || !strcmp(str, "&=");
}
LPCSTR jass_getoperator(LPCSTR str) {
    if (!strcmp(str, "+")) return "__add";
    if (!strcmp(str, "-")) return "__sub";
    if (!strcmp(str, "*")) return "__mul";
    if (!strcmp(str, "/")) return "__div";
    if (!strcmp(str, "!=")) return "__ne";
    if (!strcmp(str, "==")) return "__eq";
    if (!strcmp(str, ">=")) return "__ge";
    if (!strcmp(str, "<=")) return "__le";
    if (!strcmp(str, ">")) return "__gt";
    if (!strcmp(str, "<")) return "__lt";
    if (!strcmp(str, "and")) return "__and";
    if (!strcmp(str, "or")) return "__or";

    // javascript like
    if (!strcmp(str, "!==")) return "__ne";
    if (!strcmp(str, "===")) return "__eq"; 
    if (!strcmp(str, "&&")) return "__and";
    if (!strcmp(str, "||")) return "__or";

    if (!strcmp(str, "=")) return "__assign";
    if (!strcmp(str, "+=")) return "__add_assign";
    if (!strcmp(str, "-=")) return "__sub_assign";
    if (!strcmp(str, "*=")) return "__mul_assign";
    if (!strcmp(str, "/=")) return "__div_assign";
    if (!strcmp(str, "<<=")) return "__lshift_assign";
    if (!strcmp(str, ">>=")) return "__rshift_assign";
    if (!strcmp(str, "|=")) return "__or_assign";
    if (!strcmp(str, "&=")) return "__and_assign";
    if (!strcmp(str, "^=")) return "__xor_assign";
    assert(0);
    return str;
}

void parser_throw(LPPARSER p) {
    printf("at %s",PARSER_DumpLocation(p));
    assert(false); 
}

LPSTR read_identifier(LPPARSER p) {
    if (is_identifier(peek_token(p))) {
        return strdup(parse_token(p));
    } else {
        printf("Expected read_identifier, however got NULL at %s\n",PARSER_DumpLocation(p));
        return NULL;
    }
}
static parseClass_t* eat_keyword(LPPARSER p, parseClass_t *keywords) {
    for (parseClass_t *cl = keywords; cl->name; cl++) {
        if (eat_token(p, cl->name)) {
            return cl;
        }
    }
    return NULL;
}
static void parse_member(LPPARSER p, LPTOKEN tdef) {
    // [public] name[: type] = value;
    // modifier primary secondary stmt
    LPTOKEN token = ALLOC_TOKEN(TT_MEMBERDEF,p);
    LPCSTR peek =  peek_token(p);

    // modifier
    DWORD modifier = 0;
    if((modifier=is_modifier(peek))){
        token->flags |=modifier;
        eat_token(p, peek);
    }

    // primary
    token->primary = read_identifier(p); // member name
    if(eat_token(p, ":")){
        token->secondary = read_identifier(p);
    }
    else if(eat_token(p, "(")){
        token->flags |= TF_PROTO_FUNC;
        if(!eat_token(p, ")")){
            token->params = parse_ts_function_params(p);
        }
        if(eat_token(p, ":")){
            // return type
            token->secondary = read_identifier(p);
        }
        if(!eat_token(p, "{")){
            PARSER_THROW("expected '{' to start function statement block");
        }
        while (!eat_token(p, "}")) {
            parse_stmt(p,token->stmt);
        }
    }
    else{
        token->secondary = "auto";
    }
    if(eat_token(p, "=")){
        token->stmt = ts_right_value(p);
    }
    ADD_TO_LIST(token, tdef->stmt);
    while (eat_token(p, ";")) {
    
    }
}
static BOOL parse_stmt(LPPARSER p, LPTOKEN function) {
    LPTOKEN token = NULL;
    parseClass_t* passClass = eat_keyword(p, function_keywords);
    if (passClass && (token = passClass->func(p))) {
        PUSH_BACK(TOKEN, token, function->stmt);
        while (eat_token(p, ";")) {
            // skip
        }
    }
    else if(p->pflags & PF_JS){
        token = parse_logical_expression(p);
        PUSH_BACK(TOKEN, token, function->stmt);
        while (eat_token(p, ";")) {
            // skip
        }
    }
    else {
        PARSER_THROW("error parsing function");
    }
    return true;
}
LPCSOURCEREF create_source_ref(LPPARSER p);

LPTOKEN alloc_token(TOKENTYPE type, LPPARSER p) {
    LPTOKEN token = ALLOC(TOKEN);
    token->primary = NULL;
    token->secondary = NULL;
    token->ttype = type;
    token->location = create_source_ref(p);
    token->stmt = NULL;
    token->args = NULL;
    token->params = NULL;
    token->condition=NULL;
    token->elseblock = NULL;
    token->next = NULL;
    token->pline = NULL;
    return token;
}

LPSTR source_line(LPCSTR file, int line) {
    // 估算长度：@ + file + : + 最多 11 位数字 + \0
    int len = 1 + (file ? (int)strlen(file) : 0) + 1 + 11 + 1;
    LPSTR buffer = (LPSTR)malloc(len);
    if (!buffer) return NULL;

    if (file) {
        sprintf(buffer, "@%s:%d", file, line);
    } else {
        sprintf(buffer, "@?:%d", line);
    }

    return buffer;
}
LPTOKEN alloc_token_here(TOKENTYPE type, LPPARSER p,LPSTR pline){
    LPTOKEN token = alloc_token(type,p);
    token->pline = pline;
    token->sline = source_line(p->file, p->line);
    return token;
}

PARSER(keyword_type) {
    LPTOKEN token = ALLOC_TOKEN(TT_TYPEDEF, p);
    token->primary = read_identifier(p);
    if (eat_token(p, "extends")) {
        token->secondary = read_identifier(p);
    } else {
        PARSER_THROW("EXTENDS expected");
    }
    return token;
}
PARSER(parse_function_params) {
    if (eat_token(p, "nothing")) {
        return NULL;
    }
    LPTOKEN params = NULL;
    while (!params || eat_token(p, ",")) {
        LPTOKEN entry = ALLOC_TOKEN(TT_VARDECL, p);
        entry->pline = PARSERLINE();
        entry->sline = source_line(p->file, p->line);
        entry->primary = read_identifier(p);
        entry->secondary = read_identifier(p);
        PUSH_BACK(TOKEN, entry, params);
    }
    return params;
}
PARSER(parse_ts_function_params) {
    LPTOKEN params = NULL;
    while (!params || eat_token(p, ",")) {
        LPTOKEN entry = ALLOC_TOKEN(TT_VARDECL, p);
        entry->pline = PARSERLINE();
        entry->sline = source_line(p->file, p->line);
        entry->secondary = read_identifier(p);
        assert(entry->secondary);
        if(eat_token(p, ":")){
            entry->primary = read_identifier(p);
            assert(entry->primary);
        }
        else{
            entry->primary = "auto";
        }
        PUSH_BACK(TOKEN, entry, params);
    }
    return params;
}

PARSER(parse_function_decl) {
    LPTOKEN token = ALLOC_TOKEN(TT_FUNCTION, p);
    token->primary = read_identifier(p);
    if(!token->primary){
        // anonymouse function
        token->primary = "<anonymous>";
    }
    if (eat_token(p, "takes")) { // jass
        token->params = parse_function_params(p);
    }
    else if(eat_token(p, "(")){ // typescript args
        if(!eat_token(p, ")")){
            token->params = parse_ts_function_params(p);
            if(!eat_token(p, ")")){
                PARSER_THROW("Expected ')' in declaring function args");
            }
        }
    }
    if (eat_token(p, "returns")) { // jass
        token->secondary = read_identifier(p);
    }
    else if (eat_token(p, ":")) { // typescript return type
        token->secondary = read_identifier(p);
        assert(token->secondary);
    }
    else{
        token->secondary = "auto";
    }
    return token;
}

PARSER(keyword_native) {
    LPTOKEN token = parse_function_decl(p);
    token->flags |= TF_NATIVE;
    return token;
}
PARSER(keyword_import){
    LPTOKEN token = ALLOC_TOKEN(TT_IMPORT_ALL_ENTRIES, p);
    LPCSTR module_name = NULL;
    LPTOKEN imports = NULL;
    // 检查是否是命名空间导入: import * as namespace from 'module'
    if (eat_token(p, "*")) {
        token->ttype = TT_IMPORT_ALL_ENTRIES;
        if (eat_token(p, "as")) {
            token->secondary = read_identifier(p); // module alias of _import as the module namespace
            if (!eat_token(p, "from")) {
                PARSER_THROW("FROM expected after import * as namespace");
            }
            token->primary = read_string_literal(p);
            return token;
        } else {
            PARSER_THROW("AS expected after import *");
        }
    }
    // 检查是否是默认导入: import xxx from 'module'
    else if (is_identifier(peek_token(p))) {
        token->ttype = TT_IMPORT_DEFAULT_ENTRY;
        token->secondary = read_identifier(p);  // default export
        if (!eat_token(p, "from")) {
            PARSER_THROW("FROM expected after import identifier");
        }
        token->primary = read_string_literal(p);
        return token;
    }
    // 检查是否是命名导入: import { export1, export2 } from 'module'
    else if (eat_token(p, "{")) {
        // 解析导入列表
        token->ttype = TT_IMPORT_ENTRY_LIST;
        while (!eat_token(p, "}")) {
            LPTOKEN item = ALLOC_TOKEN(TT_IDENTIFIER, p);
            item->primary = read_identifier(p);
            PUSH_BACK(TOKEN, item, imports);
            
            // 检查是否有别名: import { original as alias } from 'module'
            if (eat_token(p, "as")) {
                item->secondary = read_identifier(p);
            }
            if (eat_token(p, ",")) {
                continue;
            }
            else if(eat_token(p, "}")){
                break;
            }
            else{
                PARSER_THROW("Unexpected '%s' in import list",peek_token(p));
            }
        }
        token->args = imports;
        
        if (!eat_token(p, "from")) {
            PARSER_THROW("FROM expected after import list");
        }
        token->primary = read_string_literal(p);
        return token;
    }
    // 简单导入: import 'module'
    else {
        token->ttype = TT_IMPORT_RUNONLY;
        token->primary = read_string_literal(p);
        return token;
    }
}
PARSER(keyword_typedef) {
    LPTOKEN token = ALLOC_TOKEN(TT_TYPEDEF, p);
    token->primary = read_identifier(p);
    if (eat_token(p, "extends")) {
        token->secondary = read_identifier(p);
    } else {
        PARSER_THROW("EXTENDS expected");
    }
    if(!eat_token(p, "{")){
        PARSER_THROW("Expect '{");
    }
    while(!eat_token(p, "}")){
        parse_member(p,token);
    }
    return token;
    assert(false);
}

// _export(style,)
PARSER(keyword_export) {
    LPTOKEN token = ALLOC_TOKEN(TT_EXPORT_ALL_ENTRIES, p);
    LPTOKEN exports = NULL;
    
    // export default expression
    // export default function foo() { }
    // export default class A { }
    // export default 42;

    if(!eat_token(p, "default")){
        token->ttype = TT_EXPORT_ADD_ENTRY;
    }
    
    if(peek_token_eq(p, "const")){
        token->flags |= TF_CONSTANT;
        if(!parse_stmt(p, token)){
            PARSER_THROW("Expected expression after 'export const'");
        }
        token->primary = token->stmt->secondary;
        return token;
    }
    else if(peek_token_eq(p, "var")){
        token->flags |= TF_VAR;
        if(!parse_stmt(p, token)){
            PARSER_THROW("Expected expression after 'export var'");
        }
        token->primary = token->stmt->secondary;
        return token;
    }
    else if(peek_token_eq(p, "let")){
        token->flags |= TF_LET;
        if(!parse_stmt(p, token)){
            PARSER_THROW("Expected expression after 'export let'");
        }
        token->primary = token->stmt->secondary;
        return token;
    }
    else if (eat_token(p, "function")) {
        token->flags |= TF_FUNCTION;
        token->stmt= keyword_function(p);
        token->primary = token->stmt->primary;
        return token;
    }
    // export * as namespace from 'module'
    else if (eat_token(p, "*")) {
        token->ttype = TT_EXPORT_ALL_ENTRIES;
        if (eat_token(p, "as")) {
            token->secondary = read_identifier(p);  // namespace
            if (!eat_token(p, "from")) {
                PARSER_THROW("FROM expected after export * as namespace");
            }
            token->primary = read_string_literal(p);
            return token;
        } else {
            PARSER_THROW("AS expected after export *");
        }
    }

    // export { name1, name2 } from 'module'
    // export { name1, name2 };
    else if (eat_token(p, "{")) {
        token->ttype = TT_EXPORT_ENTRY_LIST;
        // 解析导出列表
        while (!eat_token(p, "}")) {
            LPTOKEN entry = ALLOC_TOKEN(TT_IDENTIFIER, p);
            entry->primary = read_identifier(p);
            PUSH_BACK(TOKEN, entry, exports);

            // export { name1, name2 as xxx } ...
            if (eat_token(p, "as")) {
                entry->secondary = read_identifier(p);
            }
            if (eat_token(p, ",")) {
                continue;
            }
            else if(eat_token(p, "}")){
                break;
            }
            else{
                PARSER_THROW("Unexpected '%s' in export list",peek_token(p));
            }
        }
        token->args = exports;
        if (eat_token(p, "from")) {
            token->primary = read_string_literal(p);
        }
        else{
            token->primary = "<this_module>";
        }
        return token;
    }
    else {
         PARSER_THROW("Unexpected '%s' in export list",peek_token(p));
    }
}
PARSER(keyword_let) {
    // const name=value
    LPTOKEN token= ALLOC_TOKEN(TT_VARDECL, p);
    token->pline = PARSERLINE();
    token->sline = source_line(p->file, p->line);
    token->flags |= TF_LET;
    
    token->secondary = read_identifier(p); //name
    if (eat_token(p, "=")) {
        token->stmt = parse_logical_expression(p);
        if(token->stmt->ttype==TT_INTEGER){
            token->primary = "integer";
        }
        else if(token->stmt->ttype==TT_REAL){
            token->primary = "real";
        }
        else if(token->stmt->ttype==TT_STRING){
            token->primary = "string";
        }
        else{
            PARSER_THROW("Expected string|integer|real literal in const decl");
        }
        return token;
    } else {
        PARSER_THROW("expected native after constant");
    }
}

PARSER(keyword_new) {
    LPTOKEN token = ALLOC_TOKEN(TT_NEW, p);
    assert(0);
    token->secondary = read_identifier(p); // typename
    
    if (eat_token(p, ":")) {
        token->primary = read_identifier(p);
    }

    if (eat_token(p, "=")) {
        token->stmt = keyword_function(p);
        
        if (!token->primary) {
            if (token->stmt->ttype == TT_INTEGER) {
                token->primary = "integer";
            }
            else if (token->stmt->ttype == TT_REAL) {
                token->primary = "real";
            }
            else if (token->stmt->ttype == TT_STRING) {
                token->primary = "string";
            }
            else if (token->stmt->ttype == TT_BOOLEAN) {
                token->primary = "boolean";
            }
            else {
                token->flags |= TF_AUTOTYPE;
                token->primary = "auto"; 
            }
        }
    } else {
        // 没有初始化表达式，必须要有显式类型
        if (!token->primary) {
            PARSER_THROW("Variable declaration without initializer must have explicit type");
        }
    }
    
    return token;
}
PARSER(keyword_var) {
    // var name = "value"
    // var count = 42
    // var name: string = "value"
    // var count: integer = 42
    // var name: string
    // var x=0,y=1;
    LPTOKEN token = ALLOC_TOKEN(TT_VARDECL, p);
    token->flags |= TF_VAR;
    
    token->secondary = read_identifier(p); // name
    
    if (eat_token(p, ":")) {
        token->primary = read_identifier(p);
    }

    if (eat_token(p, "=")) {
        token->stmt = parse_logical_expression(p);
        
        if (!token->primary) {
            if (token->stmt->ttype == TT_INTEGER) {
                token->primary = "integer";
            }
            else if (token->stmt->ttype == TT_REAL) {
                token->primary = "real";
            }
            else if (token->stmt->ttype == TT_STRING) {
                token->primary = "string";
            }
            else if (token->stmt->ttype == TT_BOOLEAN) {
                token->primary = "boolean";
            }
            else {
                token->flags |= TF_AUTOTYPE;
                token->primary = "auto"; 
            }
        }
    } else {
        // 没有初始化表达式，必须要有显式类型
        if (!token->primary) {
            PARSER_THROW("Variable declaration without initializer must have explicit type");
        }
    }
    
    return token;
}

PARSER(keyword_const) {
    // const name=value
    LPTOKEN token= ALLOC_TOKEN(TT_VARDECL, p);
    token->pline = PARSERLINE();
    token->sline = source_line(p->file, p->line);
    token->flags |= TF_CONSTANT;
    
    token->secondary = read_identifier(p); //name
    if (eat_token(p, "=")) {
        token->stmt = parse_logical_expression(p);
        if(token->stmt->ttype==TT_INTEGER){
            token->primary = "integer";
        }
        else if(token->stmt->ttype==TT_REAL){
            token->primary = "real";
        }
        else if(token->stmt->ttype==TT_STRING){
            token->primary = "string";
        }
        else if(token->stmt->ttype==TT_CALL){
            token->primary = token->stmt->secondary; // return type
        }
        if(!token->primary){
            token->flags |= TF_AUTOTYPE;
            token->primary = "auto";
            // PARSER_THROW("Undetermined type in const decl");
        }
        return token;
    } else {
        PARSER_THROW("expected native after constant");
    }
}
PARSER(keyword_constant) {
    if (eat_token(p, "native")) {
        LPTOKEN token = keyword_native(p);
        token->flags |= TF_CONSTANT;
        return token;
    } else {
        PARSER_THROW("expected native after constant");
    }
}

void remove_quotes(LPSTR str, char quote) {
    size_t len = strlen(str);
    if (len >= 2 && str[0] == quote && str[len - 1] == quote) {
        memmove(str, str + 1, len - 2);
        str[len - 2] = '\0';
    }
}

LPTOKEN alloc_ident_token(LPPARSER p, TOKENTYPE tt) {
    LPTOKEN t = alloc_token(tt, p);
    t->primary = strdup(parse_token(p));
    return t;
}

LPTOKEN parse_operator_token(LPPARSER p) {
    TEXT128 op = { 0 };
    strcpy(op, parse_token(p)); // !
    if (eat_token(p, "=")) {
        op[1] = '=';
    }
    if (eat_token(p, "=")) {
        op[2] = '=';
    }
    if(!strcmp(op, "=")){ // assignment
        LPTOKEN t = ALLOC_TOKEN(TT_SET, p);
        return t;
    }
    LPCSTR operatorid = jass_getoperator(op);
    LPTOKEN t = ALLOC_TOKEN(TT_CALL, p);
    t->primary = strdup(operatorid);
    return t;
}
LPSTR read_string_literal(LPPARSER p){
    LPCSTR peek = peek_token(p);
    if (is_string(peek)) {
        LPSTR literal= strdup(parse_token(p));
        remove_quotes(literal, literal[0]);
        return literal;
    }
    else{
        PARSER_THROW("String literal expected.");
    }
}
PARSER(read_string_token){
    LPCSTR peek = peek_token(p);
    LPTOKEN token = NULL;
    if (is_string(peek)) {
        token = alloc_ident_token(p, TT_STRING);
        assert(token->primary[0]=='\"' || token->primary[0]=='\'');
        remove_quotes(token->primary, token->primary[0]);
        return token;
    }
    else{
        PARSER_THROW("String literal expected.");
    }
}

PARSER(read_single_identifier) {
    LPCSTR tok = peek_token(p);
    LPTOKEN left = NULL;
    if (eat_token(p, "typeof")) {
        left = ALLOC_TOKEN(TT_CALL, p);
        left->primary = strdup("__typeofx");
        left->args = read_single_identifier(p);
        assert(left->args);
    }
    else if (eat_token(p, "function")) {
        left = alloc_ident_token(p, TT_IDENTIFIER);
        left->flags |= TF_FUNCTION;
    }
    else if (eat_token(p, "new")) {
        left = ALLOC_TOKEN(TT_CALL, p);
        left->flags |= TF_NEW;
        left->primary = read_identifier(p);
        left->args = read_single_identifier(p);
    }
    else if (eat_token(p, "-")) {
        left = ALLOC_TOKEN(TT_CALL, p);
        left->primary = strdup("__unm");
        left->args = read_single_identifier(p);
    } else if (eat_token(p, "not")) {
        left = ALLOC_TOKEN(TT_CALL, p);
        left->primary = strdup("__not");
        left->args = read_single_identifier(p);
    } else if (eat_token(p, "!")) {
        left = ALLOC_TOKEN(TT_CALL, p);
        left->primary = strdup("__not");
        left->args = read_single_identifier(p);
        assert(left->args);
    } else if (eat_token(p, "(")) {
        left = parse_logical_expression(p);
    } else if (is_integer(tok)) {
        left = alloc_ident_token(p, TT_INTEGER);
    } else if (is_float(tok)) {
        left = alloc_ident_token(p, TT_REAL);
    } else if (is_string(tok)) {
        left = alloc_ident_token(p, TT_STRING);
        assert(left->primary[0]=='\"' || left->primary[0]=='\'');
        remove_quotes(left->primary, left->primary[0]);
    } else if (is_fourcc(tok)) {
        left = alloc_ident_token(p, TT_FOURCC);
        remove_quotes(left->primary, '\'');
    } else if (!strcmp(tok, "true") || !strcmp(tok, "false")) {
        left = alloc_ident_token(p, TT_BOOLEAN);
    } 
    else if (!strcmp(tok, "undefined")) {
        left = alloc_ident_token(p, TT_UNDEFINED);
    }
    else if (!strcmp(tok, "null")) {
        left = alloc_ident_token(p, TT_NULL);
    } else if (is_identifier(tok)) {
        left = alloc_ident_token(p, TT_IDENTIFIER);
        if (eat_token(p, "(")) {
            left->ttype = TT_CALL;
            if (!eat_token(p, ")")) {
                left->args = parse_logical_expression(p);
            }
        }
        if (eat_token(p, "[")) {
            left->ttype = TT_ARRAYACCESS;
            left->index = parse_logical_expression(p);
        }
    } 
    else if(eat_token(p, "[")) {
        left = ALLOC_TOKEN(TT_CALL, p);
        left->primary = "Array.constructor";
        left->args = read_single_identifier(p);
    }
    else {
        return NULL;
    }
    return left;
}

PARSER(parse_multiplicative_expression) {
    LPTOKEN left = read_single_identifier(p);
    if (is_multiplicative_operator(peek_token(p))) {
        LPTOKEN oper = parse_operator_token(p);
        LPTOKEN right = parse_multiplicative_expression(p);
        PUSH_BACK(TOKEN, left, oper->args);
        PUSH_BACK(TOKEN, right, oper->args);
        return oper;
    }
    return left;
}

PARSER(parse_additive_expression) {
    LPTOKEN left = parse_multiplicative_expression(p);
    if (is_additive_operator(peek_token(p))) {
        LPTOKEN oper = parse_operator_token(p);
        LPTOKEN right = parse_additive_expression(p);
        PUSH_BACK(TOKEN, left, oper->args);
        PUSH_BACK(TOKEN, right, oper->args);
        return oper;
    }
    return left;
}

PARSER(parse_comparison_expression) {
    LPTOKEN left = parse_additive_expression(p);
    if (is_compare_operator(peek_token(p))) {
        LPTOKEN oper = parse_operator_token(p);
        
        if(oper->ttype==TT_SET){
            assert(left->ttype == TT_IDENTIFIER || left->ttype ==TT_ARRAYACCESS);
            oper->secondary = left->primary;
            oper->stmt = ts_right_value(p);
            assert(oper->stmt);
            return oper;
        }
        LPTOKEN right = parse_comparison_expression(p);
        PUSH_BACK(TOKEN, left, oper->args);
        PUSH_BACK(TOKEN, right, oper->args);
        return oper;
    }
    return left;
}

PARSER(parse_logical_expression) {

            if(p->line==367){
                printf("debuggerBreak\n");
            }
    LPTOKEN left = parse_comparison_expression(p);
    assert(left);
    if (is_logic_operator(peek_token(p))) {
        LPTOKEN oper = parse_operator_token(p);
        LPTOKEN right = parse_logical_expression(p);
        PUSH_BACK(TOKEN, left, oper->args);
        PUSH_BACK(TOKEN, right, oper->args);
        return oper;
    }

    //condition ? true_expression : false_expression
    if (eat_token(p, "?")) {
        LPTOKEN cond_expr = ALLOC_TOKEN(TT_CALL, p);
        cond_expr->primary = "__cond";
        ADD_TO_LIST(left, cond_expr->args); // condition
        LPTOKEN true_expr = parse_logical_expression(p);
        ADD_TO_LIST(true_expr, cond_expr->args); // true expr
        
        if (!eat_token(p, ":")) {
            FREE(cond_expr);
            PARSER_THROW("Expected ':' in ternary expression");
        }
        LPTOKEN false_expr= parse_logical_expression(p);
        ADD_TO_LIST(false_expr, cond_expr->args); // false expr
        return cond_expr;
    }
    if (eat_token(p, ",")) {
        left->next = parse_logical_expression(p);
        return left;
    }
    if (eat_token(p, ")") || eat_token(p, "]")) {
        return left;
    }
    return left;
}

PARSER(keyword_globals) {
    LPTOKEN globals = NULL;
    while (!eat_token(p, "endglobals")) {
        LPTOKEN token = ALLOC_TOKEN(TT_GLOBAL,p);
        if (eat_token(p, "constant")) {
            token->flags |= TF_CONSTANT;
        }
        token->primary = read_identifier(p);
        if(!token->primary){
            PARSER_THROW("Expected an identifier or endglobals");
        }
        if (eat_token(p, "array")) {
            token->flags |= TF_ARRAY;
        }
        token->secondary = read_identifier(p);
       if(!token->secondary){
            PARSER_THROW("Expected an identifier or endglobals");
        }
        if (eat_token(p, "=")) {
            token->stmt = parse_logical_expression(p);
        }
        PUSH_BACK(TOKEN, token, globals);
    }
    return globals;
}
PARSER(ts_right_value){
    if (eat_token(p, "function")) {
        return keyword_function(p);
    }
    else{
        return parse_logical_expression(p);
    }
}

PARSER(statement_set) {
    LPTOKEN token = ALLOC_TOKEN(TT_SET,p);
    token->secondary = read_identifier(p);
    // secondary[index]
    if (eat_token(p, "[")) {
        token->index = parse_logical_expression(p);

    }
    if (eat_token(p, "=")) { // assignment
        // typescript
        if (eat_token(p, "function")) {
            token->flags |= TF_FUNCTION;
            token->primary = PARSERLINE();
            token->stmt= keyword_function(p);
            return token;
        }
        else{
            token->stmt = parse_logical_expression(p);
        }
    }
    return token;
}

PARSER(statement_call) {
    return parse_logical_expression(p);
}
PARSER(statement_for) {
    assert(false);
}
PARSER(statement_local) {
    LPTOKEN token = ALLOC_TOKEN(TT_VARDECL,p);
    token->pline = PARSERLINE();
    token->sline = source_line(p->file, p->line);
    token->primary = read_identifier(p);
    if (eat_token(p, "array")) {
        token->flags |= TF_ARRAY;
    }
    token->secondary = read_identifier(p);
    if (eat_token(p, "=")) {
        token->stmt = parse_logical_expression(p);
    }
    return token;
}

PARSER(statement_if) {
    LPTOKEN token = ALLOC_TOKEN(TT_IF,p);
    LPTOKEN target = token;
    token->condition = parse_logical_expression(p);
    while (eat_token(p, "{")) {
        while (!eat_token(p, "}")){
            parse_stmt(p, target);
        }
        if (eat_token(p, "else")){
            if(eat_token(p, "if")){
                LPTOKEN next = ALLOC_TOKEN(TT_ELSE,p);
                next->condition = parse_logical_expression(p);
                if (!eat_token(p, "{")) {
                    FREE(token);
                    PARSER_THROW("else if mssing block");
                }
                target->elseblock = next;
                target = next;
            }
            else{
                LPTOKEN next = ALLOC_TOKEN(TT_ELSE,p);
                target->elseblock = next;
                target = next;
            }
        }
    }
    if(p->pflags & PF_JS){
        return token;
    }
    if (!eat_token(p, "then")) {
        // if(condition) body;
        parse_stmt(p, target);
        return token;
    }
    while (!eat_token(p, "endif")) {
        if (eat_token(p, "elseif")) {
            LPTOKEN next = ALLOC_TOKEN(TT_ELSE,p);
            next->condition = parse_logical_expression(p);
            if (!eat_token(p, "then")) {
                FREE(token);
                PARSER_THROW("THEN expected");
            }
            target->elseblock = next;
            target = next;
        } else if (eat_token(p, "else")) {
            LPTOKEN next = ALLOC_TOKEN(TT_ELSE,p);
            target->elseblock = next;
            target = next;
        } else if (!parse_stmt(p, target)) {
            FREE(token);
            PARSER_THROW("broken if statement");
        }
    }
    return token;
}

PARSER(statement_exitwhen) {
    LPTOKEN token = ALLOC_TOKEN(TT_EXITWHEN,p);
    token->condition = parse_logical_expression(p);
    return token;
}
PARSER(statement_while) {
    LPTOKEN token = ALLOC_TOKEN(TT_WHILE,p);
    token->condition = parse_logical_expression(p);
    if (eat_token(p, "{")) {
        while (!eat_token(p, "}")){
            parse_stmt(p, token);
        }
    }
    else{
        // while(condition) statement;
        parse_stmt(p, token);
        return token;
    }
    return token;
}
PARSER(statement_loop) {
    LPTOKEN loop = ALLOC_TOKEN(TT_LOOP,p);
    while (!eat_token(p, "endloop")) {
        if (eat_token(p, "exitwhen")) {
            LPTOKEN exitwhen = statement_exitwhen(p);
            PUSH_BACK(TOKEN, exitwhen, loop->stmt);
        } else if (!parse_stmt(p, loop)) {
            FREE(loop);
            return NULL;
        }
    }
    return loop;
}

PARSER(statement_return) {
    LPTOKEN ret = ALLOC_TOKEN(TT_RETURN,p);
    ret->stmt = parse_logical_expression(p);
    return ret;
}
PARSER(statement_break) {
    LPTOKEN ret = ALLOC_TOKEN(TT_BREAK,p);
    return ret;
}
parseClass_t function_keywords[] = {
    { "set", statement_set },
    { "call", statement_call },
    { "local", statement_local },
    { "if", statement_if },
    { "loop", statement_loop },
    { "return", statement_return },
    { "exitwhen", statement_exitwhen },

    // typescript like
    { "var", keyword_var },
     { "new", keyword_new },
    { "let", keyword_let },
    { "for", statement_for },
    { "const", keyword_const },
     { "while", statement_while },
     { "break", statement_break },
    { 0 },
};

PARSER(keyword_function) {
    LPTOKEN function = parse_function_decl(p);
    if(p->pflags & PF_JS){
        if(!eat_token(p, "{")){
            PARSER_THROW("Expect `{` to start a function body");
        }
        while(!eat_token(p, "}")){
            parse_stmt(p, function);
        }
    }
    else{
        while (!eat_token(p, "endfunction")) {
            if (!parse_stmt(p, function)) {
                FREE(function);
                return NULL;
            }
        }
    }
    return function;
}

parseClass_t global_keywords[] = {
    // typescript like
    { "var", keyword_var },
    { "let", keyword_let },
    { "const", keyword_const },
    { "import",keyword_import},
    { "export", keyword_export },
    { "class", keyword_typedef },
    { "struct", keyword_typedef },
    { "if", statement_if },

    { "globals", keyword_globals },
    { "function", keyword_function },
    { "type", keyword_type },
    { "native", keyword_native },
    { "constant", keyword_constant },
    { 0 },
};

LPTOKEN JASS_ParseTokens(LPPARSER p) {
    LPTOKEN tokens = NULL;
    if (setjmp(exception_env) == 0) {
        LPTOKEN token = NULL;
        while (*peek_token(p)) {
            parseClass_t* parseClass = eat_keyword(p, global_keywords);
            if (parseClass && (token = parseClass->func(p))) {
                PUSH_BACK(TOKEN, token, tokens);
                while (eat_token(p, ";")) {
                    // skip
                }
            }
            else{
                token = parse_logical_expression(p);
                PUSH_BACK(TOKEN, token, tokens);
                while (eat_token(p, ";")) {
                    // skip
                }
            }
        }
        return tokens;
    } else {
        FREE(tokens);
        fprintf(stderr, "Parser Error\n");
        return NULL;
    }
}
