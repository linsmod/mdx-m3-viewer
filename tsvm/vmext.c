#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jass/vm_public.h"
#include "jass/vm_ext.h"
#include "parser.h"
JASSMODULE jass_funcs[] = {

};

// vmext 函数的简单实现
LPSTR vmext_readalltext(LPCSTR fileName) {
    FILE *file = fopen(fileName, "r");
    if (!file) {
        return NULL;
    }
    
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    LPSTR buffer = malloc(size + 1);
    if (!buffer) {
        fclose(file);
        return NULL;
    }
    
    fread(buffer, 1, size, file);
    buffer[size] = '\0';
    fclose(file);
    
    return buffer;
}

void vmext_skipbom(LPSTR buffer) {
    if (buffer && strncmp(buffer, "\xEF\xBB\xBF", 3) == 0) {
        memmove(buffer, buffer + 3, strlen(buffer + 3) + 1);
    }
}

DWORD vmext_createthread(HANDLE (func)(HANDLE), HANDLE args) {
    // 简单的线程实现 - 在实际应用中需要真正的线程支持
    return (DWORD)func(args);
}

void vmext_free(HANDLE ptr) {
    free(ptr);
}

HANDLE vmext_alloc(DWORD size) {
    return malloc(size);
}