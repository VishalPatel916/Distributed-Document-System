#ifndef METADATA_H
#define METADATA_H

#include "storage_globals.h"
#include "document.h"
#include <sys/select.h>

#define METADATA_FILE "./ss_storage/.metadata"

typedef struct {
    char filename[MAX_FILENAME];
    char owner[MAX_USERNAME];
    int access_count;
    AccessEntry access_list[MAX_PERMISSIONS_PER_FILE];
} FileMetadata_SS;

void save_file_metadata(char* filename, char* owner, int access_count, AccessEntry* access_list);
void load_file_metadata(char* filename, char* owner_out, int* access_count_out, AccessEntry* access_list_out);
void delete_file_metadata(char* filename);
void calculate_and_send_metadata(int nm_sock, char* filename, char* file_path);
void handle_create_file(char* filename, char* file_path);
void handle_send_file(int client_sock, char* filename);
void handle_stream_file(int client_sock, char* filename);
void scan_directory_recursive(const char* base_path, const char* relative_path, char files[][MAX_FILENAME], int* count, int max_files);
void handle_client_disconnect(int sock_fd, fd_set* master_set);

#endif // METADATA_H
