// Global state definitions for the central Naming Server (NM).

#ifndef NAME_GLOBALS_H
#define NAME_GLOBALS_H

#include "protocol.h"
#include <sys/select.h>
#include <stdio.h>
#include <time.h>

// Global pointer for the NM's log file.
extern FILE* nm_log_file;

// Maximum concurrent socket connections (default FD_SETSIZE is usually 1024)
#define MAX_CONNECTIONS FD_SETSIZE

// Tracks the state of a connected client.
typedef struct { 
    int active;                   // < 1 if client is currently connected, 0 otherwise.
    char username[MAX_USERNAME];  // < Registered username of the client.
    char ip_addr[MAX_IP_LEN];     // < IP address of the client.
} ClientInfo;

// Tracks the state of a registered Storage Server (SS).
typedef struct { 
    int active;                   // < 1 if SS is alive and registered, 0 otherwise.
    char ip[MAX_IP_LEN];          // < IP address the SS is listening on for clients.
    int client_port;              // < Port the SS is listening on for clients.
    char ip_addr[MAX_IP_LEN];     // < IP address the SS connected to the NM from.
    char ss_id[MAX_USERNAME];     // < Unique identifier (formatted as IP:port).
    time_t last_heartbeat;        // < Timestamp of the last successful heartbeat received.
    int missed_heartbeats;        // < Counter for failure detection (SS goes offline if too high).
} SSInfo;

// Catalog entry for a file in the Distributed Document System.
typedef struct {
    int active;                   // < 1 if file is currently available, 0 if deleted/offline.
    char filename[MAX_FILENAME];  // < Name of the file.
    int ss_sock_fd;               // < Socket descriptor of the primary SS hosting this file.
    char owner[MAX_USERNAME];     // < Username of the file's creator/owner.
    AccessEntry access_list[MAX_PERMISSIONS_PER_FILE]; // < List of users with access.
    int access_count;             // < Number of entries in the access list.
    long file_size;               // < Size of the file in bytes.
    int word_count;               // < Total number of words in the file.
    int char_count;               // < Total number of characters in the file.
    time_t last_modified;         // < Timestamp of last write operation.
    time_t last_accessed;         // < Timestamp of last read/stream operation.
    int backup_ss_sock;           // < Socket descriptor of the backup SS (-1 if none).
} FileMetadata;

// --- Global State Arrays ---
// These arrays map socket descriptors (indices) to state structures.
extern ClientInfo client_state[MAX_CONNECTIONS];
extern SSInfo ss_state[MAX_CONNECTIONS];
extern FileMetadata file_catalog[MAX_FILES_IN_SYSTEM];

// Logs an event to both standard output and the nm.log file.
void log_event(const char* format, ...);

// --- Access Request Tracking ---
// Used when a user requests access to a file they don't own.
#define MAX_ACCESS_REQUESTS 1000

// Tracks a pending request from a user for file access.
typedef struct {
    int active;                   // < 1 if request is pending, 0 if resolved.
    int request_id;               // < Unique identifier for the request.
    char filename[MAX_FILENAME];  // < File being requested.
    char requesting_user[MAX_USERNAME]; // < User asking for access.
    PermissionLevel requested_perm;     // < Type of permission requested (READ/WRITE).
    time_t timestamp;             // < When the request was made.
} AccessRequest;

extern AccessRequest access_requests[MAX_ACCESS_REQUESTS];
extern int next_request_id;

#endif // NAME_GLOBALS_H
