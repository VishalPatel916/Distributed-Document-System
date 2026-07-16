// Header for Naming Server (NM) fault tolerance mechanisms.

#ifndef FAULT_TOLERANCE_H
#define FAULT_TOLERANCE_H

#include "name_globals.h"

// Selects an active Storage Server to serve as a backup.
int find_backup_ss(int primary_sock);

// Counts the number of currently active Storage Servers.
int count_active_ss();

// Initiates asynchronous replication of a file to its assigned backup SS.
void replicate_to_backup(int backup_sock, FileMetadata* file);

// Coordinates the transfer of a single file from a backup SS to a recovering SS.
void sync_file_to_recovering_ss(int recovering_sock, int backup_sock, const char* filename);

// Coordinates the recovery process for an SS that just restarted.
void sync_files_to_recovering_ss(int recovering_sock);

#endif // FAULT_TOLERANCE_H
