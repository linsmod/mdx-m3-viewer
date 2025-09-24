// string_utils.h - JS-like String library for C
//
// Example:
//   String* s = string_new("  hello world  ");
//   String* trimmed = string_trim(s);
//   String* upper = string_toUpperCase(trimmed);
//   printf("%s\n", upper->cstr);  // "HELLO WORLD"
//   string_free(s);
//   string_free(trimmed);
//   string_free(upper);

#ifndef STRING_UTILS_H
#define STRING_UTILS_H

#include "shared.h"
#include <stdint.h>  
#include <stdarg.h>   // For va_list
#include <stdio.h>
KNOWN_AS(String, STRING);
// 如果你没有 windows.h，可以用以下替代：
// typedef unsigned long DWORD;
// typedef char* LPSTR;
// typedef const char* LPCSTR;

#ifdef __cplusplus
extern "C" {
#endif

// ========================
// String 结构体
// ========================
typedef struct String {
    LPSTR cstr;      // 字符串内容（以 \0 结尾）
    DWORD strlen;    // 字符串长度（不包含末尾 \0）
} String;

char *strndup(const char *s, size_t n);

// ========================
// 构造与析构
// ========================

// 创建新 String，深拷贝输入字符串
String* string_new(LPCSTR cstr);
String* string_from_int(int value);

// 释放 String 及其内部字符串
void string_free(String* s);


// ========================
// 属性访问
// ========================
// length 直接访问：s->strlen


// ========================
// 查找与判断
// ========================

// charAt(index) - 获取指定位置字符，越界返回 '\0'
char string_charAt(String* s, DWORD index);

// indexOf(searchValue, fromIndex) - 从 fromIndex 开始查找第一次出现的位置，未找到返回 -1
int string_indexOf(String* s, LPCSTR search, DWORD fromIndex);

// lastIndexOf(searchValue) - 查找最后一次出现的位置
int string_lastIndexOf(String* s, LPCSTR search);

// startsWith(prefix) - 是否以 prefix 开头
int string_startsWith(String* s, LPCSTR prefix);

// endsWith(suffix) - 是否以 suffix 结尾
int string_endsWith(String* s, LPCSTR suffix);

// includes(search) - 是否包含子字符串
int string_includes(String* s, LPCSTR search);


// ========================
// 截取与变换
// ========================

// substring(start, end) - 返回 [start, end) 子串
String* string_substring(String* s, DWORD start, DWORD end);

// substr(start, length) - 从 start 开始截取 length 个字符
String* string_substr(String* s, DWORD start, DWORD length);

// toUpperCase() - 返回大写副本
String* string_toUpperCase(String* s);

// toLowerCase() - 返回小写副本
String* string_toLowerCase(String* s);

// trim() - 去除首尾空白字符（空格、\t、\n 等）
String* string_trim(String* s);


// ========================
// 拼接与分割
// ========================

// concat(s1, s2, ..., NULL) - 拼接多个 String，以 NULL 结尾
String* string_concat(String* first, ...);

// split(separator) - 使用分隔符分割，返回 char** 数组，以 NULL 结尾
// 注意：需用 string_split_free() 释放结果
char** string_split(String* s, LPCSTR sep);

// 释放 string_split 返回的结果
void string_split_free(char** parts);


// ========================
// 工具宏
// ========================
#define SAFE_FREE(p) do { if(p){free(p);p=NULL;} } while(0)

#ifdef __cplusplus
}
#endif

#endif // STRING_UTILS_H