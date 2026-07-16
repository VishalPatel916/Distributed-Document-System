#ifndef NAME_GLOBALS_H
#define NAME_GLOBALS_H

#include "protocol.h"
#include <sys/select.h>
#include <stdio.h>
#include <time.h>

extern FILE* nm_log_file;

#define MAX_CONNECTIONS FD_SETSIZE
typedef struct { int active; char username[MAX_USERNAME]; char ip_addr[MAX_IP_LEN]; } ClientInfo;
typedef struct { 
    int active; 
    char ip[MAX_IP_LEN]; 
    int client_port; 
    char ip_addr[MAX_IP_LEN]; 
    char ss_id[MAX_USERNAME];    // Unique identifier (IP:port)
    time_t last_heartbeat;       // Last successful heartbeat
    int missed_heartbeats;       // Counter for failure detection
} SSInfo;
typedef struct {
    int active;
    char filename[MAX_FILENAME];
    int ss_sock_fd; 
    char owner[MAX_USERNAME];
    AccessEntry access_list[MAX_PERMISSIONS_PER_FILE];
    int access_count;
    long file_size;
    int word_count;
    int char_count;
    time_t last_modified;
    time_t last_accessed;
    int backup_ss_sock;          // Socket of backup storage server
} FileMetadata;

extern ClientInfo client_state[MAX_CONNECTIONS];
extern SSInfo ss_state[MAX_CONNECTIONS];
extern FileMetadata file_catalog[MAX_FILES_IN_SYSTEM];

void log_event(const char* format, ...);

#endif // NAME_GLOBALS_H
