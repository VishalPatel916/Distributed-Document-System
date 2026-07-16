#ifndef STORAGE_GLOBALS_H
#define STORAGE_GLOBALS_H

#include "protocol.h"
#include <sys/select.h>
#include <stdio.h>

#define MY_PORT_FOR_CLIENTS 8082
#define MY_STORAGE_PATH "./ss_storage"
#define MAX_CONNECTIONS FD_SETSIZE
#define MAX_LOCKS 100

extern char g_storage_path[512];
extern char g_metadata_path[768];
extern FILE* ss_log_file;

void log_event(const char* format, ...);
void detect_ip_from_nm_socket(int nm_sock, char* ip_out, size_t ip_out_size);
int copy_file(const char* src_path, const char* dst_path);

#endif // STORAGE_GLOBALS_H
