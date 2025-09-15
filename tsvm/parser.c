#include "jass/vm_public.h"
#include "shared.h"
#include "jass/vm_ext.h"
#include "parser.h"
#include <assert.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdio.h>
#define MAX_SEGMENT_SIZE 1024

BOOL eat_token(LPPARSER p, LPCSTR value) {
    LPCSTR tok = peek_token(p);
    if (!strcmp(tok, value)) {
        LPCSTR eat =  parse_token(p);
        assert(strcmp(eat,tok)==0);
        return true;
    } else {
        return false;
    }
}

void skip_spaces(LPPARSER p){
    while (isspace(*(p)->buffer)) { 
        (p)->buffer++;
    }
}
void skip_comments(LPPARSER p){
    while(p->buffer[0]=='/' && p->buffer[1]=='*'){
        p->buffer+=2;
        while(p->buffer){
            if(p->buffer[0]=='*' && p->buffer[1]=='/'){
                p->buffer+=2;
                skip_spaces(p);
                break;
            }
            else{
                p->buffer+=1;
            }
        }
    }
    while(p->buffer[0]=='/' && p->buffer[1]=='/'){
        p->buffer+=2;
        while(p->buffer){
            if(p->buffer[0]=='\n'){
                p->buffer+=1;
                skip_spaces(p);
                break;
            }
            else{
                p->buffer+=1;
            }
        }
    }
}
void safeskip_spaces(LPPARSER p) {
    while (p->buffer && isspace((unsigned char)*p->buffer)) {
        p->buffer++;
    }
}


void update_location(LPCSTR start,LPPARSER p) {
    skip_spaces(p);
    skip_comments(p);
    size_t n = p->buffer - start;
    for (LPCSTR s = start; s < p->buffer; s++) {
        if (*s == '\n') {
            p->line++;
            p->column = 1;
        } else {
            p->column++;
        }
    }
}

LPCSTR parse_token(LPPARSER p) {
    static char word[MAX_SEGMENT_SIZE];
    LPCSTR start = p->buffer;
    skip_spaces(p);
    skip_comments(p);
    if (*p->buffer == '\"') {
        LPCSTR closingQuote = strchr(p->buffer+1, '"');
        size_t stringLength = closingQuote-p->buffer+1;
        assert(stringLength<=MAX_SEGMENT_SIZE);
        if (p->eat_quotes) {
            p->buffer++;
            stringLength -= 2;
        }
        memcpy(word, p->buffer, stringLength);
        word[stringLength] = '\0';
        p->buffer = ++closingQuote;
//        printf("%s\n", word);
        update_location(start, p);
        return word;
    }
    if (*p->buffer == '\'') {
        LPCSTR closingQuote = strchr(p->buffer+1, '\'');
        size_t stringLength = closingQuote-p->buffer+1;
        if (p->eat_quotes) {
            p->buffer++;
            stringLength -= 2;
        }
        memcpy(word, p->buffer, stringLength);
        word[stringLength] = '\0';
        p->buffer = ++closingQuote;
//        printf("%s\n", word);
        update_location(start, p);
        return word;
    } 
    else if (strchr(p->delimiters, *p->buffer)) {
        word[0] = *(p->buffer++);
        word[1] = '\0';
        update_location(start, p);
        return word;
    } else {
        size_t segmentLength = 0;
        while (*p->buffer && *p->buffer != '\"' && *p->buffer != '\'' &&
           (!isspace(*p->buffer) && strchr(p->delimiters, *p->buffer) == NULL) &&
               segmentLength < MAX_SEGMENT_SIZE - 1) {
            word[segmentLength++] = *(p->buffer++);
        }
        word[segmentLength] = '\0'; // Null-terminate the segment
        update_location(start, p);
        return word;
    }
}

LPCSTR peek_token(LPPARSER p) {
    PARSER tmp = *p;
    LPCSTR token = parse_token(&tmp);
    return strdup( token);
}
BOOL peek_token_eq(LPPARSER p,LPCSTR s) {
    PARSER tmp = *p;
    LPCSTR token = parse_token(&tmp);
    assert(s);
    return strcmp(token,s)==0;
}

LPCSTR parse_segment(LPPARSER p) {
    static char segment[MAX_SEGMENT_SIZE];
    memset(segment, 0, MAX_SEGMENT_SIZE);
    if (*p->buffer == '\0')
        return NULL;
    while (isspace(*p->buffer))
        ++p->buffer;
    LPCSTR start = p->buffer;
    if (*p->buffer == '\"') {
        ++start;
        p->buffer = strchr(start, '\"');
        memcpy(segment, start, p->buffer - start);
        segment[p->buffer - start] = '\0';
        p->buffer = strchr(p->buffer, ',');
    } else {
        p->buffer = strchr(p->buffer, ',');
        if (p->buffer) {
            memcpy(segment, start, p->buffer - start);
            segment[p->buffer - start] = '\0'; // Null-terminate the segment
        } else {
            strcpy(segment, start);
            p->buffer = start + strlen(start);
            return segment;
        }
    }
    ++p->buffer;
    return segment;
}

LPCSTR parse_segment2(LPPARSER p) {
    static char segment[MAX_SEGMENT_SIZE];
    memset(segment, 0, MAX_SEGMENT_SIZE);
    if (*p->buffer == '\0')
        return NULL;
    while (isspace(*p->buffer))
        ++p->buffer;
    DWORD num_quotes = 0;
    for (LPSTR out = segment; *p->buffer; ++p->buffer, ++out) {
        if (*p->buffer == ',' && (num_quotes & 1) == 0) {
            ++p->buffer;
            break;
        }
        if (*p->buffer == '"')
            ++num_quotes;
        *out = *p->buffer;
    }
    return segment;
}



void parser_error(LPPARSER parser) {
    parser->error = true;
}

void *find_in_array(void *array, long sizeofelem, LPCSTR name) {
    LPSTR str = array;
    while (*(LPCSTR *)str) {
        LPCSTR value = *(LPCSTR *)str;
        if (!strcmp(value, name)) {
            return str;
        }
        str += sizeofelem;
    }
    return NULL;
}
LPCSOURCEREF create_source_ref(LPPARSER p) {
    LPCSTR start = p->buffer;
    skip_spaces(p);
    update_location(start, p);
    LPSOURCEREF ref = vmext_alloc(sizeof(SOURCEREF));
    ref->file = p->file;
    ref->line = p->line;
    ref->column = p->column;
    return ref;
}

LPCSTR PARSER_DumpLocation(LPPARSER p) {
    if (p) {
        static char buf[256];
            snprintf(buf, sizeof(buf), "%s:%d:%d\n", p->file, p->line, p->column);
        return buf;
    }
    return "";
}
LPCSTR JASS_DumpLocation(LPCSOURCEREF loc) {
    static char buf[256];
    if (loc) {
        snprintf(buf, sizeof(buf), "%s:%d:%d\n", loc->file, loc->line, loc->column);
        return buf;
    }
    return "";
}