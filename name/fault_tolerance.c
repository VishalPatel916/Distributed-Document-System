#include "fault_tolerance.h"
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>

int find_backup_ss(int primary_sock) {
    // Find a different active SS to use as backup
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (ss_state[i].active && i != primary_sock) {
            return i;
        }
    }
    return -1; // No backup available
}

int count_active_ss() {
    int count = 0;
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (ss_state[i].active) count++;
    }
    return count;
}

void replicate_to_backup(int backup_sock, FileMetadata* file) {
    // Send async replication request to backup SS
    Header header;
    header.type = REQ_REPLICATE_FILE;
    header.payload_size = sizeof(Msg_Replicate_File);
    
    Msg_Replicate_File rep_msg;
    strncpy(rep_msg.filename, file->filename, MAX_FILENAME);
    strncpy(rep_msg.owner, file->owner, MAX_USERNAME);
    rep_msg.access_count = file->access_count;
    for (int i = 0; i < file->access_count; i++) {
        rep_msg.access_list[i] = file->access_list[i];
    }
    rep_msg.file_size = file->file_size;
    
    // Use MSG_DONTWAIT for async send
    if (send(backup_sock, &header, sizeof(header), MSG_DONTWAIT) < 0) {
        log_event("  -> Warning: Failed to send replication header to backup SS");
        return;
    }
    if (send(backup_sock, &rep_msg, sizeof(rep_msg), MSG_DONTWAIT) < 0) {
        log_event("  -> Warning: Failed to send replication data to backup SS");
        return;
    }
    
    log_event("  -> Async replication initiated for '%s' to backup SS (sock %d)", 
             file->filename, backup_sock);
}

void sync_file_to_recovering_ss(int recovering_sock, int backup_sock, const char* filename) {
    // Request file content from backup SS
    Header req_header;
    req_header.type = REQ_GET_FILE_CONTENT;
    req_header.payload_size = sizeof(Msg_Sync_File);
    
    Msg_Sync_File sync_msg;
    strncpy(sync_msg.filename, filename, MAX_FILENAME);
    
    send(backup_sock, &req_header, sizeof(req_header), 0);
    send(backup_sock, &sync_msg, sizeof(sync_msg), 0);
    
    // Receive file size and content from backup
    long file_size;
    recv(backup_sock, &file_size, sizeof(long), 0);
    
    if (file_size > 0) {
        char* content = malloc(file_size);
        if (content) {
            int received = 0;
            while (received < file_size) {
                int bytes = recv(backup_sock, content + received, file_size - received, 0);
                if (bytes <= 0) break;
                received += bytes;
            }
            
            // Send to recovering SS
            Header sync_header;
            sync_header.type = REQ_SYNC_FROM_BACKUP;
            sync_header.payload_size = sizeof(Msg_Sync_File);
            
            send(recovering_sock, &sync_header, sizeof(sync_header), 0);
            send(recovering_sock, &sync_msg, sizeof(sync_msg), 0);
            send(recovering_sock, &file_size, sizeof(long), 0);
            send(recovering_sock, content, file_size, 0);
            
            free(content);
            log_event("  -> Synced file '%s' (%ld bytes) to recovering SS", filename, file_size);
        }
    }
}

void sync_files_to_recovering_ss(int recovering_sock) {
    // Find all files that belong to this SS and sync from their backups
    int synced_count = 0;
    for (int i = 0; i < MAX_FILES_IN_SYSTEM; i++) {
        if (!file_catalog[i].active && file_catalog[i].ss_sock_fd == recovering_sock && 
            file_catalog[i].backup_ss_sock >= 0 && ss_state[file_catalog[i].backup_ss_sock].active) {
            
            sync_file_to_recovering_ss(recovering_sock, file_catalog[i].backup_ss_sock, 
                                      file_catalog[i].filename);
            file_catalog[i].active = 1; // Reactivate file
            synced_count++;
        }
    }
    log_event("  -> Recovery complete: synced %d files to SS (sock %d)", synced_count, recovering_sock);
}
