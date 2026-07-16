#ifndef HASH_CACHE_H
#define HASH_CACHE_H

#include "name_globals.h"

#define HASH_SIZE 1024
#define CACHE_SIZE 5

typedef struct HashNode {
    char key[MAX_FILENAME];
    int slot_index;
    struct HashNode* next;
} HashNode;

typedef struct {
    int valid;
    char key[MAX_FILENAME];
    int slot_index;
} CacheEntry;

extern HashNode* hash_table[HASH_SIZE];
extern CacheEntry lru_cache[CACHE_SIZE];

unsigned long hash_func(char *str);
void add_to_cache(char* filename, int slot_index);
void invalidate_cache(char* filename);
void add_to_hashmap(char* filename, int slot_index);
void remove_from_hashmap(char* filename);

#endif // HASH_CACHE_H
