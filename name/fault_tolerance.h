#ifndef FAULT_TOLERANCE_H
#define FAULT_TOLERANCE_H

#include "name_globals.h"

int find_backup_ss(int primary_sock);
int count_active_ss();
void replicate_to_backup(int backup_sock, FileMetadata* file);
void sync_file_to_recovering_ss(int recovering_sock, int backup_sock, const char* filename);
void sync_files_to_recovering_ss(int recovering_sock);

#endif // FAULT_TOLERANCE_H
