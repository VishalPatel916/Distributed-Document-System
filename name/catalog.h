// Header for Naming Server (NM) catalog management.

#ifndef CATALOG_H
#define CATALOG_H

#include "name_globals.h"

// Finds a file in the catalog.
int find_file_slot(char* filename);

// Finds an empty slot in the file catalog for a new file.
int find_empty_file_slot();

// Finds an active Storage Server (SS).
int find_available_ss();

// Sends a simple RES_OK response to a socket.
void send_ok_response(int sock);

// Handles the disconnection of a Client or Storage Server.
void handle_disconnect(int sock_fd);

// Gets a user's permission level for a specific file.
PermissionLevel get_permission(FileMetadata* meta, char* username);

// Checks if a user has read access to a file.
int has_read_access(FileMetadata* meta, char* username);

// Checks if a user has write access to a file.
int has_write_access(FileMetadata* meta, char* username);

// Formats and sends a full metadata response over a socket.
void send_full_metadata(int sock_fd, FileMetadata* meta, MessageType res_type);

// Fetches file content directly from a Storage Server (used for EXEC).
char* get_file_content_from_ss(SSInfo* ss, char* filename);

#endif // CATALOG_H
