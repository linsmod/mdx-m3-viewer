#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 简化的哈希表结构
struct ZHashEntry {
    char *key;
    void *val;
    struct ZHashEntry *next;
};

struct ZHashTable {
    size_t size_index;
    size_t entry_count;
    struct ZHashEntry **entries;
};

// 简化的哈希函数
size_t zgenerate_hash(struct ZHashTable *hash_table, char *key) {
    size_t hash = 0;
    char ch;
    while ((ch = *key++)) hash = (17 * hash + ch) % 53; // 使用第一个大小
    return hash;
}

// 创建哈希表
struct ZHashTable *zcreate_hash_table(void) {
    struct ZHashTable *hash_table = malloc(sizeof(struct ZHashTable));
    if (!hash_table) return NULL;
    
    hash_table->size_index = 0;
    hash_table->entry_count = 0;
    hash_table->entries = calloc(53, sizeof(struct ZHashEntry*));
    if (!hash_table->entries) {
        free(hash_table);
        return NULL;
    }
    
    return hash_table;
}

// 创建条目
struct ZHashEntry *zcreate_entry(char *key, void *val) {
    struct ZHashEntry *entry = malloc(sizeof(struct ZHashEntry));
    if (!entry) return NULL;
    
    entry->key = malloc(strlen(key) + 1);
    if (!entry->key) {
        free(entry);
        return NULL;
    }
    strcpy(entry->key, key);
    
    entry->val = val;
    entry->next = NULL;
    
    return entry;
}

// 设置键值对
void zhash_set(struct ZHashTable *hash_table, char *key, void *val) {
    size_t hash = zgenerate_hash(hash_table, key);
    struct ZHashEntry *entry = hash_table->entries[hash];
    
    // 检查是否已存在
    while (entry) {
        if (strcmp(key, entry->key) == 0) {
            entry->val = val;
            return;
        }
        entry = entry->next;
    }
    
    // 创建新条目
    entry = zcreate_entry(key, val);
    if (!entry) return;
    
    // 插入到链表头部
    entry->next = hash_table->entries[hash];
    hash_table->entries[hash] = entry;
    hash_table->entry_count++;
}

// 获取值
void *zhash_get(struct ZHashTable *hash_table, char *key) {
    size_t hash = zgenerate_hash(hash_table, key);
    struct ZHashEntry *entry = hash_table->entries[hash];
    
    while (entry) {
        if (strcmp(key, entry->key) == 0) {
            return entry->val;
        }
        entry = entry->next;
    }
    
    return NULL;
}

int main() {
    struct ZHashTable *ht = zcreate_hash_table();
    if (!ht) {
        printf("Failed to create hash table\n");
        return 1;
    }
    
    // 测试数据
    struct {
        char *name;
        int value;
    } test_data[] = {
        {"init", 1},
        {NULL, 0}
    };
    
    // 插入数据
    for (int i = 0; test_data[i].name != NULL; i++) {
        printf("Inserting: %s\n", test_data[i].name);
        zhash_set(ht, test_data[i].name, &test_data[i].value);
    }
    
    // 查找数据
    printf("Looking up: init\n");
    int *result = zhash_get(ht, "init");
    if (result) {
        printf("Found value: %d\n", *result);
    } else {
        printf("Not found\n");
    }
    
    free(ht);
    return 0;
}