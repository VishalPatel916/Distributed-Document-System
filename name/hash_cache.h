// Header for Naming Server (NM) fast file lookup mechanisms.

#ifndef HASH_CACHE_H
#define HASH_CACHE_H

#include "name_globals.h"

// Define capacities for lookup structures
#define HASH_SIZE 1024
#define CACHE_SIZE 5

// A node in the separate chaining Hash Map.
typedef struct HashNode {
    char key[MAX_FILENAME];       // < The filename serving as the key.
    int slot_index;               // < The index in the global file_catalog.
    struct HashNode* next;        // < Pointer to the next node (for collision resolution).
} HashNode;

// An entry in the Least Recently Used (LRU) Cache.
typedef struct {
    int valid;                    // < 1 if the entry contains valid data, 0 otherwise.
    char key[MAX_FILENAME];       // < The filename serving as the key.
    int slot_index;               // < The index in the global file_catalog.
} CacheEntry;

extern HashNode* hash_table[HASH_SIZE];
extern CacheEntry lru_cache[CACHE_SIZE];

// Computes a hash value for a given string (djb2 algorithm).
unsigned long hash_func(char *str);

// Adds a file to the LRU Cache, evicting the oldest if full.
void add_to_cache(char* filename, int slot_index);

// Invalidates a specific file from the cache (e.g., on deletion).
void invalidate_cache(char* filename);

// Adds a file to the Hash Map.
void add_to_hashmap(char* filename, int slot_index);

// Removes a file from the Hash Map (e.g., on deletion or move).
void remove_from_hashmap(char* filename);

#endif // HASH_CACHE_H
