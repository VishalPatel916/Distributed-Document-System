// Header for Storage Server (SS) sentence-level locking.

#ifndef LOCKS_H
#define LOCKS_H

#include "storage_globals.h"
#include "document.h"

// Represents an active write lock on a specific sentence.
typedef struct {
    int active;                     // < 1 if lock is held, 0 if free.
    char filename[MAX_FILENAME];    // < Document being edited.
    SentenceNode* sentence_ptr;     // < Pointer to the locked sentence.
    int sock_fd;                    // < Client holding the lock.
} LockInfo;
extern LockInfo global_locks[MAX_LOCKS];

// Initializes the global locks array.
void init_locks();

// Checks if a specific sentence is currently locked.
int find_lock(char* filename, SentenceNode* sentence);

// Attempts to acquire a lock on a sentence for a client.
int create_lock(char* filename, SentenceNode* sentence, int sock);

// Releases a lock once a client finishes their write session.
void release_lock(char* filename, SentenceNode* sentence);

#endif // LOCKS_H
