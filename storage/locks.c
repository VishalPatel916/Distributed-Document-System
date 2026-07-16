#include "locks.h"

void init_locks() { for (int i = 0; i < MAX_LOCKS; i++) global_locks[i].active = 0; }

int find_lock(char* f, SentenceNode* s) { for (int i = 0; i < MAX_LOCKS; i++) if (global_locks[i].active && !strcmp(global_locks[i].filename, f) && global_locks[i].sentence_ptr == s) return i; return -1; }

int create_lock(char* f, SentenceNode* s, int sock) { if (find_lock(f, s) != -1) return 0; for (int i = 0; i < MAX_LOCKS; i++) if (!global_locks[i].active) { global_locks[i].active = 1; strncpy(global_locks[i].filename, f, MAX_FILENAME); global_locks[i].sentence_ptr = s; global_locks[i].sock_fd = sock; log_event("  -> Lock CREATED for '%s' (sentence ptr %p) by socket %d", f, (void*)s, sock); return 1; } return -1; }

void release_lock(char* f, SentenceNode* s) { int i = find_lock(f, s); if (i != -1) { global_locks[i].active = 0; log_event("  -> Lock RELEASED for '%s' (sentence ptr %p)", f, (void*)s); } }

