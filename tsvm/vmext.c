#define _GNU_SOURCE  // 为了使用高级特性（可选）
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jass/vm_public.h"
#include "jass/vm_ext.h"
#include "jass/vm_priv.h"
#include "parser.h"
#include "shared.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define TRUE 1
#define FALSE 0
#define PATH_MAX 1024

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/stat.h>
#include <errno.h>
#include <assert.h>
#include <stdbool.h>

// 全局变量
static char app_base[PATH_MAX] = ".";

// 扩展名列表（按优先级）
const char* extensions[] = {
    ".ts", ".tsx", ".js", ".jsx"
};
const int num_extensions = 4;

// 作用域包判断
BOOL is_scoped_package(LPCSTR name) {
    return name[0] == '@' && strchr(name + 1, '/') != NULL;
}

void split_scoped_package(const char* name, char* scope_out, char* pkg_out, size_t size) {
    const char* slash = strchr(name, '/');
    size_t scope_len = slash - name;
    strncpy(scope_out, name, scope_len);
    scope_out[scope_len] = '\0';
    strncpy(pkg_out, slash + 1, size - 1);
    pkg_out[size - 1] = '\0';
}

// 读取 package.json 字段（简化版）
char* read_package_field(const char* package_dir, const char* field) {
    char json_path[PATH_MAX];
    snprintf(json_path, sizeof(json_path), "%s/package.json", package_dir);
    
    FILE* f = fopen(json_path, "r");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* json = malloc(size + 1);
    if (!json) { fclose(f); return NULL; }
    fread(json, 1, size, f);
    json[size] = '\0';
    fclose(f);

    // 查找 "module": "xxx" 或 "main": "xxx"
    const char* ptr = strstr(json, field);
    if (!ptr) { free(json); return NULL; }
    ptr = strchr(ptr+strlen(field)+2, '"');
    if (!ptr) { free(json); return NULL; }
    ptr++;
    const char* end = strchr(ptr, '"');
    if (!end) { free(json); return NULL; }

    size_t len = end - ptr;
    char* value = malloc(len + 1);
    if (value) {
        strncpy(value, ptr, len);
        value[len] = '\0';
    }
    free(json);
    return value;
}

// 检查文件是否存在且为普通文件
BOOL file_exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}
BOOL dir_exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

// 尝试添加各种扩展名并查找文件
BOOL try_extensions(const char* base, char* output, size_t out_size) {
    char temp[PATH_MAX];

    // 1. 尝试 base + extension
    for (int i = 0; i < num_extensions; i++) {
        snprintf(temp, sizeof(temp), "%s%s", base, extensions[i]);
        if (file_exists(temp)) {
            strncpy(output, temp, out_size - 1);
            output[out_size - 1] = '\0';
            return TRUE;
        }
    }

    // 2. 尝试 base/index + extension
    for (int i = 0; i < num_extensions; i++) {
        snprintf(temp, sizeof(temp), "%s/index%s", base, extensions[i]);
        if (file_exists(temp)) {
            strncpy(output, temp, out_size - 1);
            output[out_size - 1] = '\0';
            return TRUE;
        }
    }

    return FALSE;
}

// 从 start_dir 开始向上查找 node_modules/<package>
LPSTR find_node_modules_package(const char* start_dir, const char* package_name) {
        char candidate[PATH_MAX];
        int ret = snprintf(candidate, sizeof(candidate), "%s/node_modules/%s", app_base, package_name);
        if (ret < 0 || (size_t)ret >= sizeof(candidate)) {
            // 如果结果可能被截断，返回NULL
            return NULL;
        }
        LPSTR path = strdup(candidate);
        if(dir_exists(path)){
            return path;
        }
    return NULL;
}

int endswith(const char* str, const char* suffix) {
    if (!str || !suffix) return 0;
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);
    if (suffix_len > str_len) return 0;
    return memcmp(str + str_len - suffix_len, suffix, suffix_len) == 0;
}
BOOL has_extension(const char* str){
    for (int i = 0; i < num_extensions; i++) {
            if(endswith(str, extensions[i])){
                return true;
            }
        }
        return false;
}

// 解析路径主函数
char* vmext_resolvepath(const char* name, const char* initiator) {
    assert(name != NULL);
    char fullPath[PATH_MAX] = {0};
    char resolvedPath[PATH_MAX] = {0};

    BOOL has_ext = has_extension(name);

    // 第一步：绝对路径
    if (name[0] == '/') {
        if (realpath(name, resolvedPath) && file_exists(resolvedPath)) {
            return strdup(resolvedPath);
        }
        if(!has_ext){
            for (int i = 0; i < num_extensions; i++) {
                snprintf(fullPath, sizeof(fullPath), "%s%s", name, extensions[i]);
                if (realpath(fullPath, resolvedPath) && file_exists(resolvedPath)) {
                    return strdup(resolvedPath);
                }
            }
        }
        // 尝试目录
        if (realpath(name, resolvedPath)) {
            if (try_extensions(resolvedPath, resolvedPath, sizeof(resolvedPath))) {
                return strdup(resolvedPath);
            }
        }
        return NULL;
    }

    // 第二步：相对路径（./xxx, ../xxx）
    if (name[0] == '.') {
        char baseDir[PATH_MAX] = ".";
        if (initiator != NULL) {
            char temp[PATH_MAX];
            strncpy(temp, initiator, sizeof(temp) - 1);
            temp[sizeof(temp) - 1] = '\0';
            char* dir = dirname(temp);
            strncpy(baseDir, dir, sizeof(baseDir) - 1);
        }

        if(has_ext){
            int ret = snprintf(fullPath, sizeof(fullPath), "%s/%s", baseDir, name);
            if (ret >= 0 && (size_t)ret < sizeof(fullPath)) {
                if (realpath(fullPath, resolvedPath)) {
                    if (file_exists(resolvedPath)) {
                        return strdup(resolvedPath);
                    }
                    // 尝试加扩展名
                    if (try_extensions(resolvedPath, resolvedPath, sizeof(resolvedPath))) {
                        return strdup(resolvedPath);
                    }
                }
            }
        }
        else{
            int ret = snprintf(fullPath, sizeof(fullPath)-1, "%s/%s", baseDir, name);
            if (ret >= 0 && (size_t)ret < sizeof(fullPath)-1) {
                fullPath[sizeof(fullPath)-1] = '\0'; // 确保字符串终止
                if (realpath(fullPath, resolvedPath)) {
                    if (file_exists(resolvedPath)) {
                        return strdup(resolvedPath);
                    }
                }
                // 尝试加扩展名
                if (try_extensions(resolvedPath, resolvedPath, sizeof(resolvedPath))) {
                    return strdup(resolvedPath);
                }
            }
        }
        return NULL;
    }

    // 第三步：模块导入（node_modules）
    char pkg_name[256];

    if (is_scoped_package(name)) {
        char scope[64], pkg[64];
        split_scoped_package(name, scope, pkg, sizeof(pkg));
        snprintf(pkg_name, sizeof(pkg_name), "%s/%s", scope, pkg);
    } else {
        strncpy(pkg_name, name, sizeof(pkg_name) - 1);
        pkg_name[sizeof(pkg_name) - 1] = '\0';
    }

    const char* lookup_start = initiator ? initiator : app_base;
    LPSTR package_dir = find_node_modules_package(lookup_start, pkg_name);
    if (!package_dir) {
        return NULL;
    }

    // 读取 package.json
    char* entry = read_package_field(package_dir, "\"module\"");
    if (!entry) {
        entry = read_package_field(package_dir, "\"main\"");
    }

    if (entry) {
        // 解析相对路径
        snprintf(fullPath, sizeof(fullPath), "%s/%s", package_dir, entry);
        free(entry);
        if (realpath(fullPath, resolvedPath) && file_exists(resolvedPath)) {
            free(package_dir);
            return strdup(resolvedPath);
        }
        // 尝试加扩展名
        if (try_extensions(fullPath, resolvedPath, sizeof(resolvedPath))) {
            free(package_dir);
            return strdup(resolvedPath);
        }
    } else {
        // 默认尝试 index
        if (try_extensions(package_dir, resolvedPath, sizeof(resolvedPath))) {
            free(package_dir);
            return strdup(resolvedPath);
        }
    }

    free(package_dir);
    return NULL;
}

// 初始化
void vmext_init(const char* appbase_path) {
    if (appbase_path == NULL) {
        return;
    }
    if (realpath(appbase_path, app_base) == NULL) {
        fprintf(stderr, "Warning: Cannot resolve appbase path '%s': %s\n", 
                appbase_path, strerror(errno));
        strncpy(app_base, ".", sizeof(app_base) - 1);
    }
}

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
    assert(0);
    return -1;
}

void vmext_free(HANDLE ptr) {
    free(ptr);
}

HANDLE vmext_alloc(DWORD size) {
    return malloc(size);
}