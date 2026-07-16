// Global definitions and state for the Storage Server (SS).

#ifndef STORAGE_GLOBALS_H
#define STORAGE_GLOBALS_H

#include "protocol.h"
#include <sys/select.h>
#include <stdio.h>

// Default configuration values
#define MY_PORT_FOR_CLIENTS 8082
#define MY_STORAGE_PATH "./ss_storage"
#define MAX_CONNECTIONS FD_SETSIZE
#define MAX_LOCKS 100

// --- Global Variables ---
extern char g_storage_path[512];    // < Physical path to the SS's root storage directory.
extern char g_metadata_path[768];   // < Physical path to the hidden metadata file.
extern FILE* ss_log_file;           // < File pointer for system logging.

// Logs formatted messages to both stdout and the log file.
void log_event(const char* format, ...);

// Attempts to determine the peer IP address from a socket connection.
void detect_ip_from_nm_socket(int nm_sock, char* ip_out, size_t ip_out_size);

// Utility function to copy a physical file from one path to another.
int copy_file(const char* src_path, const char* dst_path);

#endif // STORAGE_GLOBALS_H
