#ifndef CATALOG_H
#define CATALOG_H

#include "name_globals.h"

int find_file_slot(char* filename);
int find_empty_file_slot();
int find_available_ss();
void send_ok_response(int sock);
void handle_disconnect(int sock_fd);
PermissionLevel get_permission(FileMetadata* meta, char* username);
int has_read_access(FileMetadata* meta, char* username);
int has_write_access(FileMetadata* meta, char* username);
void send_full_metadata(int sock_fd, FileMetadata* meta, MessageType res_type);
char* get_file_content_from_ss(SSInfo* ss, char* filename);

#endif // CATALOG_H
