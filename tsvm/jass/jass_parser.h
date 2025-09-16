#ifndef jass_parser_h
#define jass_parser_h

#include "../shared.h"
#include "../parser.h"
#include "string_utils.h"
typedef enum {
    TT_UNKNOWN,
    TT_VALUE,
    TT_FUNCTION,
    TT_TYPEDEF,
    TT_MEMBERDEF,
    TT_VARDECL,
    TT_GLOBAL,
    TT_IDENTIFIER,
    TT_ARRAYACCESS,
    TT_CALL,
    TT_INTEGER,
    TT_REAL,
    TT_STRING,
    TT_FOURCC,
    TT_BOOLEAN,
    TT_IF,
    TT_SET,
    TT_LOOP,
    TT_FOR,
    TT_ELSE,
    TT_EXITWHEN,
    TT_WHILE,
    TT_RETURN,
    TT_BREAK,
    TT_TERNARY,
    TT_NULL,
    TT_UNDEFINED,
    TT_IMPORT_ALL_ENTRIES,
    TT_IMPORT_ENTRY_LIST,
    TT_IMPORT_DEFAULT_ENTRY,
    TT_IMPORT_RUNONLY,
    TT_EXPORT_ALL_ENTRIES,
    TT_EXPORT_ENTRY_LIST,
    TT_EXPORT_DEFAULT_ENTRY,
    TT_EXPORT_ADD_ENTRY
} TOKENTYPE;

// Flags a token is identifier of WHAT
enum {
    TF_NATIVE        = 1 << 0,
    TF_CONSTANT      = 1 << 1,
    TF_ARRAY         = 1 << 2,
    TF_FUNCTION      = 1 << 3,
    TF_CLASS         = 1 << 6,
    TF_VARLIST       = 1 << 7, // decl multi vars expr: var a,b,c;
    TF_LET           = 1 << 8,
    TF_TYPEOF        = 1 << 9,
    TF_ANONYMOUS    = 1 << 10,
    TF_AUTOTYPE      = 1 << 11,
    TF_PUBLIC        = 1 << 12,
    TF_PRIVATE       = 1 << 13,
    TF_PROTECTED     = 1 << 14,
    TF_PROTO_FIELD   = 1 << 15,
    TF_INPLACECALL    = 1 << 16, // 声明后就地调用 function abc(){}();
    TF_NEW = 1 << 17,
    TF_BRACEOPEN = 1 << 18,
};

enum{
    PF_JASS =1,
    PF_JS=2,
};
KNOWN_AS(loop_s, LOOP)
struct token {
    TOKENTYPE ttype;
    LPSTR primary;
    LPSTR secondary;
    DWORD flags;
    LPTOKEN stmt;
    LPTOKEN next;
    LPTOKEN params; // 形参
    LPTOKEN args; // 实参
    LPTOKEN condition;
    LPTOKEN elseblock;
    LPTOKEN index;
    struct loop_s{
        LPTOKEN init; // for (init;cond;..)
        LPTOKEN condition;
        LPTOKEN increment;
        LPTOKEN stmt;
    }* loop;
    LPCSOURCEREF location;
    LPSTR pline; // optional parserline for debugging
    LPSTR sline; 
};

LPTOKEN JASS_ParseTokens(LPPARSER p);
LPCSTR PARSER_DumpLocation(LPPARSER p);
LPCSTR JASS_DumpLocation(LPCSOURCEREF loc);

#endif
