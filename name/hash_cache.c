#include "hash_cache.h"
#include <string.h>
#include <stdlib.h>

HashNode* hash_table[HASH_SIZE];
CacheEntry lru_cache[CACHE_SIZE];

// Simple djb2 hash function
unsigned long hash_func(char *str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++)) hash = ((hash << 5) + hash) + c;
    return hash % HASH_SIZE;
}

void add_to_cache(char* filename, int slot_index) {
    // Check if already in cache
    for(int i=0; i<CACHE_SIZE; i++) {
        if(lru_cache[i].valid && strcmp(lru_cache[i].key, filename) == 0) return;
    }
    // Shift right (evict last)
    for(int i=CACHE_SIZE-1; i > 0; i--) {
        lru_cache[i] = lru_cache[i-1];
    }
    // Insert at front
    strcpy(lru_cache[0].key, filename);
    lru_cache[0].slot_index = slot_index;
    lru_cache[0].valid = 1;
}

void invalidate_cache(char* filename) {
    for(int i=0; i<CACHE_SIZE; i++) {
        if(lru_cache[i].valid && strcmp(lru_cache[i].key, filename) == 0) {
            lru_cache[i].valid = 0;
        }
    }
}

void add_to_hashmap(char* filename, int slot_index) {
    unsigned long idx = hash_func(filename);
    HashNode* new_node = (HashNode*)malloc(sizeof(HashNode));
    strcpy(new_node->key, filename);
    new_node->slot_index = slot_index;
    new_node->next = hash_table[idx]; // Insert at head
    hash_table[idx] = new_node;
}

void remove_from_hashmap(char* filename) {
    unsigned long idx = hash_func(filename);
    HashNode* current = hash_table[idx];
    HashNode* prev = NULL;
    while(current != NULL) {
        if(strcmp(current->key, filename) == 0) {
            if(prev == NULL) hash_table[idx] = current->next;
            else prev->next = current->next;
            free(current);
            return;
        }
        prev = current;
        current = current->next;
    }
}
