// common functions to operation String like js
// string_utils.c
#include "shared.h"
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdarg.h>

#include "string_utils.h"

// ========================
// 1. 创建 String（从 C 字符串）
// 类似 new String("hello")
// ========================
String* string_new(LPCSTR cstr) {
    if (!cstr) cstr = "";
    String* s = (String*)malloc(sizeof(String));
    if (!s) return NULL;
    s->strlen = (DWORD)strlen(cstr);
    s->cstr = (LPSTR)malloc(s->strlen + 1);
    if (!s->cstr) {
        free(s);
        return NULL;
    }
    memcpy(s->cstr, cstr, s->strlen + 1);
    return s;
}
String* string_from_int(int value) {
    char buffer[32]; // int 最多 11 字符（包括负号）
    size_t len = snprintf(buffer, sizeof(buffer), "%d", value);
    if (len < 0 || len >= sizeof(buffer)) {
        return NULL; // 格式化失败
    }
    return string_new(buffer); // string_new 会复制字符串
}

// string_utils.c
String* string_new2(LPCSTR format, ...) {
    if (!format) {
        return string_new(""); // 或返回 NULL，看需求
    }

    va_list args;
    va_start(args, format);

    // 第一次：尝试使用 vsnprintf 计算所需长度
    va_list args_copy;
    va_copy(args_copy, args);

    int len = vsnprintf(NULL, 0, format, args_copy);
    va_end(args_copy);

    if (len < 0) {
        va_end(args);
        return NULL; // 格式错误
    }

    // 分配缓冲区（+1 为了 '\0'）
    LPSTR buffer = (LPSTR)malloc(len + 1);
    if (!buffer) {
        va_end(args);
        return NULL;
    }

    // 第二次：实际格式化
    int written = vsnprintf(buffer, len + 1, format, args);
    va_end(args);

    if (written < 0 || written != len) {
        free(buffer);
        return NULL;
    }

    // 构造 String 对象
    String* s = (String*)malloc(sizeof(String));
    if (!s) {
        free(buffer);
        return NULL;
    }

    s->cstr = buffer;
    s->strlen = (DWORD)len;

    return s;
}

// ========================
// 2. 释放 String
// ========================
void string_free(String* s) {
    if (s) {
        SAFE_FREE(s->cstr);
        free(s);
    }
}

// ========================
// 3. length 属性（直接访问即可）
// s->strlen 就是 length
// ========================

// ========================
// 4. charAt(index)
// ========================
char string_charAt(String* s, DWORD index) {
    if (!s || index >= s->strlen) {
        return '\0';  // JS 返回 undefined，C 中返回 null char
    }
    return s->cstr[index];
}

// ========================
// 5. indexOf(searchValue, fromIndex)
// ========================
int string_indexOf(String* s, LPCSTR search, DWORD fromIndex) {
    if (!s || !search || fromIndex >= s->strlen) return -1;
    LPCSTR pos = strstr(s->cstr + fromIndex, search);
    return pos ? (int)(pos - s->cstr) : -1;
}

// ========================
// 6. lastIndexOf(searchValue, fromIndex)
// ========================
int string_lastIndexOf(String* s, LPCSTR search) {
    if (!s || !search || !*search) return -1;
    int len = (int)s->strlen;
    int search_len = (int)strlen(search);
    for (int i = len - search_len; i >= 0; i--) {
        if (strncmp(s->cstr + i, search, search_len) == 0) {
            return i;
        }
    }
    return -1;
}

// ========================
// 7. substring(start, end)
// ========================
String* string_substring(String* s, DWORD start, DWORD end) {
    if (!s || start >= s->strlen) return string_new("");
    if (end > s->strlen || end == 0) end = s->strlen;
    if (start >= end) return string_new("");

    DWORD len = end - start;
    String* result = (String*)malloc(sizeof(String));
    if (!result) return NULL;
    result->strlen = len;
    result->cstr = (LPSTR)malloc(len + 1);
    if (!result->cstr) {
        free(result);
        return NULL;
    }
    memcpy(result->cstr, s->cstr + start, len);
    result->cstr[len] = '\0';
    return result;
}

// ========================
// 8. substr(start, length)
// ========================
String* string_substr(String* s, DWORD start, DWORD length) {
    if (!s || start >= s->strlen) return string_new("");
    DWORD len = s->strlen - start;
    if (length < len) len = length;
    return string_substring(s, start, start + len);
}

// ========================
// 9. startsWith(search)
// ========================
int string_startsWith(String* s, LPCSTR prefix) {
    if (!s || !prefix) return 0;
    DWORD prefix_len = (DWORD)strlen(prefix);
    if (prefix_len > s->strlen) return 0;
    return strncmp(s->cstr, prefix, prefix_len) == 0;
}

// ========================
// 10. endsWith(search)
// ========================
int string_endsWith(String* s, LPCSTR suffix) {
    if (!s || !suffix) return 0;
    DWORD suffix_len = (DWORD)strlen(suffix);
    DWORD len = s->strlen;
    if (suffix_len > len) return 0;
    return strncmp(s->cstr + len - suffix_len, suffix, suffix_len) == 0;
}

// ========================
// 11. includes(search)
// ========================
int string_includes(String* s, LPCSTR search) {
    return string_indexOf(s, search, 0) != -1;
}

// ========================
// 12. toUpperCase()
// ========================
String* string_toUpperCase(String* s) {
    if (!s) return NULL;
    String* result = string_new(s->cstr);
    if (!result) return NULL;
    for (DWORD i = 0; i < result->strlen; i++) {
        result->cstr[i] = (char)toupper((unsigned char)result->cstr[i]);
    }
    return result;
}

// ========================
// 13. toLowerCase()
// ========================
String* string_toLowerCase(String* s) {
    if (!s) return NULL;
    String* result = string_new(s->cstr);
    if (!result) return NULL;
    for (DWORD i = 0; i < result->strlen; i++) {
        result->cstr[i] = (char)tolower((unsigned char)result->cstr[i]);
    }
    return result;
}

// ========================
// 14. trim() - 去除首尾空白
// ========================
String* string_trim(String* s) {
    if (!s || s->strlen == 0) return string_new("");
    LPCSTR begin = s->cstr;
    LPCSTR end = s->cstr + s->strlen - 1;
    while (begin <= end && isspace((unsigned char)*begin)) begin++;
    while (end >= begin && isspace((unsigned char)*end)) end--;
    DWORD len = (DWORD)(end - begin + 1);
    return string_substring(s, (DWORD)(begin - s->cstr), (DWORD)(begin - s->cstr + len));
}

// ========================
// 15. concat(str1, str2, ...)
// 示例：string_concat(s1, s2, NULL)
// ========================
String* string_concat(String* first, ...) {
    if (!first) return NULL;

    // 第一遍：计算总长度
    va_list args;
    DWORD total_len = 0;
    String* current = first;
    va_start(args, first);
    do {
        total_len += current->strlen;
        current = va_arg(args, String*);
    } while (current);
    va_end(args);

    // 分配内存
    LPSTR buffer = (LPSTR)malloc(total_len + 1);
    if (!buffer) return NULL;
    LPSTR p = buffer;

    // 第二遍：复制内容
    current = first;
    va_start(args, first);
    do {
        memcpy(p, current->cstr, current->strlen);
        p += current->strlen;
        current = va_arg(args, String*);
    } while (current);
    *p = '\0';
    va_end(args);

    // 构造返回 String
    String* result = (String*)malloc(sizeof(String));
    if (!result) {
        free(buffer);
        return NULL;
    }
    result->cstr = buffer;
    result->strlen = total_len;
    return result;
}

// ========================
// 16. split(separator) - 返回字符串数组（简单版）
// 返回一个 char** 数组，以 NULL 结尾
// ========================
char** string_split(String* s, LPCSTR sep) {
    if (!s || !sep || !*sep) return NULL;

    // 先复制一份以便分割
    LPSTR copy = strdup(s->cstr);
    if (!copy) return NULL;

    int count = 0;
    char* parts[64] = {0}; // 限制最多 64 个部分
    char* token = strtok(copy, sep);
    while (token && count < 63) {
        parts[count++] = strdup(token); // 每个部分独立分配
        token = strtok(NULL, sep);
    }

    // 分配结果数组
    char** result = (char**)malloc((count + 1) * sizeof(char*));
    if (!result) {
        for (int i = 0; i < count; i++) SAFE_FREE(parts[i]);
        free(copy);
        return NULL;
    }
    for (int i = 0; i < count; i++) {
        result[i] = parts[i];
    }
    result[count] = NULL;

    free(copy); // 释放临时副本
    return result;
}

// ========================
// 17. split 释放函数
// ========================
void string_split_free(char** parts) {
    if (!parts) return;
    for (int i = 0; parts[i]; i++) {
        SAFE_FREE(parts[i]);
    }
    free(parts);
}