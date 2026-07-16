#ifndef LOCKS_H
#define LOCKS_H

#include "storage_globals.h"
#include "document.h"

typedef struct {
    int active;
    char filename[MAX_FILENAME];
    SentenceNode* sentence_ptr;
    int sock_fd;
} LockInfo;
extern LockInfo global_locks[MAX_LOCKS];

void init_locks();
int find_lock(char* filename, SentenceNode* sentence);
int create_lock(char* filename, SentenceNode* sentence, int sock);
void release_lock(char* filename, SentenceNode* sentence);

#endif // LOCKS_H
