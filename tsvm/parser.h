#pragma once
#include "shared.h"

KNOWN_AS(token, TOKEN);
KNOWN_AS(parser_s, PARSER);
KNOWN_AS(srcref_s, SOURCEREF);
struct srcref_s {
    LPCSTR file;
    DWORD line;
    DWORD column;
};
struct parser_s {
    LPCSTR buffer;
    const char* delimiters;
    BOOL error;
    BOOL eat_quotes;
    LPCSTR file;
    DWORD line;
    DWORD column;
    DWORD pflags;
    char lastdilimiter;
};

LPCSTR parse_token(LPPARSER p);
LPCSTR parse_segment(LPPARSER p);
LPCSTR peek_token(LPPARSER p);
BOOL peek_token_eq(LPPARSER p,LPCSTR s);
BOOL eat_token(LPPARSER p, LPCSTR value);
void parser_error(LPPARSER parser) ;
void *find_in_array(void *array, long, LPCSTR);