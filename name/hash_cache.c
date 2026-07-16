// Implementation of Naming Server (NM) fast file lookup mechanisms.

#include "hash_cache.h"
#include <string.h>
#include <stdlib.h>

HashNode* hash_table[HASH_SIZE];
CacheEntry lru_cache[CACHE_SIZE];

// Computes a hash value using the djb2 algorithm.
unsigned long hash_func(char *str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++)) hash = ((hash << 5) + hash) + c;
    return hash % HASH_SIZE;
}

// Adds an entry to the front of the LRU Cache.
void add_to_cache(char* filename, int slot_index) {
    // Check if already in cache (to avoid duplicates)
    for(int i=0; i<CACHE_SIZE; i++) {
        if(lru_cache[i].valid && strcmp(lru_cache[i].key, filename) == 0) return;
    }
    
    // Shift elements right (evicting the last/oldest element)
    for(int i=CACHE_SIZE-1; i > 0; i--) {
        lru_cache[i] = lru_cache[i-1];
    }
    
    // Insert new element at the front (Most Recently Used)
    strcpy(lru_cache[0].key, filename);
    lru_cache[0].slot_index = slot_index;
    lru_cache[0].valid = 1;
}

// Invalidates a specific file entry in the cache.
void invalidate_cache(char* filename) {
    for(int i=0; i<CACHE_SIZE; i++) {
        if(lru_cache[i].valid && strcmp(lru_cache[i].key, filename) == 0) {
            lru_cache[i].valid = 0; // Mark as invalid
        }
    }
}

// Adds a filename-to-slot mapping to the Hash Map.
void add_to_hashmap(char* filename, int slot_index) {
    unsigned long idx = hash_func(filename);
    HashNode* new_node = (HashNode*)malloc(sizeof(HashNode));
    strcpy(new_node->key, filename);
    new_node->slot_index = slot_index;
    
    // Insert at the head of the linked list (separate chaining)
    new_node->next = hash_table[idx];
    hash_table[idx] = new_node;
}

// Removes a filename mapping from the Hash Map.
void remove_from_hashmap(char* filename) {
    unsigned long idx = hash_func(filename);
    HashNode* current = hash_table[idx];
    HashNode* prev = NULL;
    
    // Traverse the linked list at this bucket
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
