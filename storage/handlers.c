// Message processing logic for the Storage Server (SS).

#include "handlers.h"
#include "protocol.h"
#include "storage_globals.h"
#include "metadata.h"
#include "document.h"
#include "locks.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>

extern void log_event(const char* format, ...);

// Handle file creation request from Name Server.
static void handle_ss_create(int nm_sock) {
    Msg_SS_Create_Request req; 
    recv(nm_sock, &req, sizeof(req), 0);
    log_event("Got REQ_SS_CREATE for '%s' (owner: %s) from NM", req.filename, req.owner);
    char file_path[768];
    handle_create_file(req.filename, file_path);
    AccessEntry empty_list[MAX_PERMISSIONS_PER_FILE];
    save_file_metadata(req.filename, req.owner, 0, empty_list);
    send_simple_header(nm_sock, RES_OK);
    calculate_and_send_metadata(nm_sock, req.filename, file_path);
}

// Handle file deletion request from Name Server.
static void handle_ss_delete(int nm_sock) {
    Msg_Filename_Request req; 
    recv(nm_sock, &req, sizeof(req), 0);
    log_event("Got REQ_SS_DELETE for '%s' from NM", req.filename);
    char file_path[768]; 
    sprintf(file_path, "%s/%s", g_storage_path, req.filename);
    if (remove(file_path) == 0) { 
        log_event("  -> File deleted."); 
    } else { 
        log_event("  -> Error deleting file: %s", strerror(errno)); 
    }
    char backup_path[768]; 
    sprintf(backup_path, "%s/%s.bak", g_storage_path, req.filename); 
    remove(backup_path);
    delete_file_metadata(req.filename);
    send_simple_header(nm_sock, RES_OK);
}

// Handle undo/revert operation from Name Server.
static void handle_ss_undo(int nm_sock) {
    Msg_Filename_Request req; 
    recv(nm_sock, &req, sizeof(req), 0);
    log_event("Got REQ_SS_UNDO for '%s' from NM", req.filename);
    
    char undo_checkpoint[1024], redo_checkpoint[1024];
    sprintf(undo_checkpoint, "%s/.checkpoints/%s/__UNDO__", g_storage_path, req.filename);
    sprintf(redo_checkpoint, "%s/.checkpoints/%s/__REDO__", g_storage_path, req.filename);
    
    struct stat st;
    if (stat(undo_checkpoint, &st) == 0) {
        log_event("  -> Found __UNDO__ checkpoint, restoring pre-revert state");
        char file_path[768];
        sprintf(file_path, "%s/%s", g_storage_path, req.filename);
        
        FILE *current = fopen(file_path, "r");
        FILE *redo = fopen(redo_checkpoint, "w");
        if (current && redo) {
            char buffer[4096];
            size_t bytes;
            while ((bytes = fread(buffer, 1, sizeof(buffer), current)) > 0) {
                fwrite(buffer, 1, bytes, redo);
            }
            fclose(current);
            fclose(redo);
        }
        
        FILE *src = fopen(undo_checkpoint, "r");
        FILE *dst = fopen(file_path, "w");
        if (src && dst) {
            char buffer[4096];
            size_t bytes;
            while ((bytes = fread(buffer, 1, sizeof(buffer), src)) > 0) {
                fwrite(buffer, 1, bytes, dst);
            }
            fclose(src);
            fclose(dst);
            
            char backup_path[1024];
            sprintf(backup_path, "%s.bak", file_path);
            FILE *bak = fopen(undo_checkpoint, "r");
            FILE *bak_dst = fopen(backup_path, "w");
            if (bak && bak_dst) {
                while ((bytes = fread(buffer, 1, sizeof(buffer), bak)) > 0) {
                    fwrite(buffer, 1, bytes, bak_dst);
                }
                fclose(bak);
                fclose(bak_dst);
            }
            
            unlink(undo_checkpoint);
            log_event("  -> Restored from __UNDO__ checkpoint");
            send_simple_header(nm_sock, RES_OK);
        } else {
            if (src) fclose(src);
            if (dst) fclose(dst);
            log_event("  -> Error restoring from __UNDO__: %s", strerror(errno));
            send_simple_header(nm_sock, RES_ERROR);
        }
        char original_path[768];
        sprintf(original_path, "%s/%s", g_storage_path, req.filename);
        calculate_and_send_metadata(nm_sock, req.filename, original_path);
    } else if (stat(redo_checkpoint, &st) == 0) {
        log_event("  -> Found __REDO__ checkpoint, toggling back to reverted state");
        char file_path[768];
        sprintf(file_path, "%s/%s", g_storage_path, req.filename);
        
        FILE *current = fopen(file_path, "r");
        FILE *undo = fopen(undo_checkpoint, "w");
        if (current && undo) {
            char buffer[4096];
            size_t bytes;
            while ((bytes = fread(buffer, 1, sizeof(buffer), current)) > 0) {
                fwrite(buffer, 1, bytes, undo);
            }
            fclose(current);
            fclose(undo);
        }
        
        FILE *src = fopen(redo_checkpoint, "r");
        FILE *dst = fopen(file_path, "w");
        if (src && dst) {
            char buffer[4096];
            size_t bytes;
            while ((bytes = fread(buffer, 1, sizeof(buffer), src)) > 0) {
                fwrite(buffer, 1, bytes, dst);
            }
            fclose(src);
            fclose(dst);
            
            char backup_path[1024];
            sprintf(backup_path, "%s.bak", file_path);
            FILE *bak = fopen(redo_checkpoint, "r");
            FILE *bak_dst = fopen(backup_path, "w");
            if (bak && bak_dst) {
                while ((bytes = fread(buffer, 1, sizeof(buffer), bak)) > 0) {
                    fwrite(buffer, 1, bytes, bak_dst);
                }
                fclose(bak);
                fclose(bak_dst);
            }
            
            unlink(redo_checkpoint);
            log_event("  -> Restored from __REDO__ checkpoint");
            send_simple_header(nm_sock, RES_OK);
        } else {
            if (src) fclose(src);
            if (dst) fclose(dst);
            log_event("  -> Error restoring from __REDO__: %s", strerror(errno));
            send_simple_header(nm_sock, RES_ERROR);
        }
        char original_path[768];
        sprintf(original_path, "%s/%s", g_storage_path, req.filename);
        calculate_and_send_metadata(nm_sock, req.filename, original_path);
    } else {
        char original_path[768], backup_path[768], temp_swap_path[768];
        sprintf(original_path, "%s/%s", g_storage_path, req.filename);
        sprintf(backup_path, "%s/%s.bak", g_storage_path, req.filename);
        sprintf(temp_swap_path, "%s/%s.swap", g_storage_path, req.filename);
        rename(original_path, temp_swap_path); 
        rename(backup_path, original_path); 
        rename(temp_swap_path, backup_path);
        log_event("  -> Swapped backup file.");
        send_simple_header(nm_sock, RES_OK);
        calculate_and_send_metadata(nm_sock, req.filename, original_path);
    }
}

// Handle adding access permissions on the Storage Server.
static void handle_ss_add_access(int nm_sock) {
    Msg_Access_Request req; 
    recv(nm_sock, &req, sizeof(req), 0);
    log_event("Got REQ_SS_ADD_ACCESS for '%s' (user: %s, perm: %d)", req.filename, req.username, req.perm);
    char owner[MAX_USERNAME];
    int access_count;
    AccessEntry access_list[MAX_PERMISSIONS_PER_FILE];
    load_file_metadata(req.filename, owner, &access_count, access_list);
    if (access_count < MAX_PERMISSIONS_PER_FILE) {
        strncpy(access_list[access_count].username, req.username, MAX_USERNAME);
        access_list[access_count].permission = req.perm;
        access_count++;
        save_file_metadata(req.filename, owner, access_count, access_list);
    }
}

// Handle removing access permissions on the Storage Server.
static void handle_ss_rem_access(int nm_sock) {
    Msg_Access_Request req; 
    recv(nm_sock, &req, sizeof(req), 0);
    log_event("Got REQ_SS_REM_ACCESS for '%s' (user: %s)", req.filename, req.username);
    char owner[MAX_USERNAME];
    int access_count;
    AccessEntry access_list[MAX_PERMISSIONS_PER_FILE];
    load_file_metadata(req.filename, owner, &access_count, access_list);
    int found_idx = -1;
    for (int i = 0; i < access_count; i++) {
        if (strcmp(access_list[i].username, req.username) == 0) {
            found_idx = i;
            break;
        }
    }
    if (found_idx != -1) {
        for (int i = found_idx; i < access_count - 1; i++) {
            access_list[i] = access_list[i + 1];
        }
        access_count--;
        save_file_metadata(req.filename, owner, access_count, access_list);
    }
}

// Handle folder creation on the Storage Server.
static void handle_ss_createfolder(int nm_sock) {
    Msg_Folder_Request req; 
    recv(nm_sock, &req, sizeof(req), 0);
    log_event("Got REQ_SS_CREATEFOLDER for '%s' from NM", req.foldername);
    char folder_path[768];
    sprintf(folder_path, "%s/%s", g_storage_path, req.foldername);
    
    struct stat st;
    if (stat(folder_path, &st) == 0 && S_ISDIR(st.st_mode)) {
        log_event("  -> Folder already exists: %s", folder_path);
        send_simple_header(nm_sock, RES_ERROR);
        return;
    }
    
    char temp_path[512];
    strncpy(temp_path, folder_path, sizeof(temp_path));
    temp_path[sizeof(temp_path) - 1] = '\0';
    
    int success = 1;
    for (char* p = temp_path + strlen(g_storage_path) + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(temp_path, 0777) != 0 && errno != EEXIST) {
                success = 0;
                break;
            }
            *p = '/';
        }
    }
    
    if (success && mkdir(folder_path, 0777) != 0) {
        success = 0;
    }
    
    if (success) {
        log_event("  -> Folder created at: %s", folder_path);
        send_simple_header(nm_sock, RES_OK);
    } else {
        log_event("  -> Error creating folder: %s", strerror(errno));
        send_simple_header(nm_sock, RES_ERROR);
    }
}

// Handle checking if a folder exists.
static void handle_ss_checkfolder(int nm_sock) {
    Msg_Folder_Request req; 
    recv(nm_sock, &req, sizeof(req), 0);
    log_event("Got REQ_SS_CHECKFOLDER for '%s' from NM", req.foldername);
    char folder_path[768];
    sprintf(folder_path, "%s/%s", g_storage_path, req.foldername);
    
    struct stat st;
    if (stat(folder_path, &st) == 0 && S_ISDIR(st.st_mode)) {
        log_event("  -> Folder exists: %s", folder_path);
        send_simple_header(nm_sock, RES_OK);
    } else {
        log_event("  -> Folder not found: %s", folder_path);
        send_simple_header(nm_sock, RES_ERROR);
    }
}

// Handle moving files on the Storage Server.
static void handle_ss_move(int nm_sock) {
    Msg_Move_Request req; 
    recv(nm_sock, &req, sizeof(req), 0);
    log_event("Got REQ_SS_MOVE for '%s' to folder '%s' from NM", req.filename, req.foldername);
    
    char* base_filename = strrchr(req.filename, '/');
    if (base_filename) {
        base_filename++;
    } else {
        base_filename = req.filename;
    }
    
    char old_path[768], new_path[768], folder_path[768];
    sprintf(old_path, "%s/%s", g_storage_path, req.filename);
    sprintf(folder_path, "%s/%s", g_storage_path, req.foldername);
    sprintf(new_path, "%s/%s/%s", g_storage_path, req.foldername, base_filename);
    
    mkdir(folder_path, 0777);
    
    if (rename(old_path, new_path) == 0) {
        log_event("  -> File moved from '%s' to '%s'", old_path, new_path);
        
        char old_backup[1024], new_backup[1024];
        sprintf(old_backup, "%s.bak", old_path);
        sprintf(new_backup, "%s.bak", new_path);
        rename(old_backup, new_backup);
        
        char old_meta_name[MAX_FILENAME], new_meta_name[MAX_FILENAME * 2];
        strncpy(old_meta_name, req.filename, MAX_FILENAME);
        snprintf(new_meta_name, sizeof(new_meta_name), "%s/%s", req.foldername, base_filename);
        
        char owner[MAX_USERNAME];
        int access_count;
        AccessEntry access_list[MAX_PERMISSIONS_PER_FILE];
        load_file_metadata(old_meta_name, owner, &access_count, access_list);
        delete_file_metadata(old_meta_name);
        save_file_metadata(new_meta_name, owner, access_count, access_list);
        
        send_simple_header(nm_sock, RES_OK);
    } else {
        log_event("  -> Error moving file: %s", strerror(errno));
        send_simple_header(nm_sock, RES_ERROR);
    }
}

// Handle creating checkpoint on the Storage Server.
static void handle_ss_checkpoint(int nm_sock) {
    Msg_Checkpoint_Request req; 
    recv(nm_sock, &req, sizeof(req), 0);
    log_event("Got REQ_SS_CHECKPOINT for '%s' with tag '%s' from NM", req.filename, req.tag);
    
    char file_path[768], checkpoint_dir[768], checkpoint_path[1024];
    sprintf(file_path, "%s/%s", g_storage_path, req.filename);
    sprintf(checkpoint_dir, "%s/.checkpoints/%s", g_storage_path, req.filename);
    sprintf(checkpoint_path, "%s/%s", checkpoint_dir, req.tag);
    
    struct stat st;
    if (stat(file_path, &st) != 0) {
        log_event("  -> Error: File '%s' not found", req.filename);
        send_simple_header(nm_sock, RES_ERROR);
        return;
    }
    
    if (stat(checkpoint_path, &st) == 0) {
        log_event("  -> Error: Checkpoint tag '%s' already exists", req.tag);
        send_simple_header(nm_sock, RES_ERROR);
        return;
    }
    
    char temp_dir[512];
    strncpy(temp_dir, checkpoint_dir, sizeof(temp_dir));
    temp_dir[sizeof(temp_dir) - 1] = '\0';
    for (char* p = temp_dir + strlen(g_storage_path) + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(temp_dir, 0777);
            *p = '/';
        }
    }
    mkdir(checkpoint_dir, 0777);
    
    FILE *src = fopen(file_path, "r");
    FILE *dst = fopen(checkpoint_path, "w");
    if (src && dst) {
        char buffer[4096];
        size_t bytes;
        while ((bytes = fread(buffer, 1, sizeof(buffer), src)) > 0) {
            fwrite(buffer, 1, bytes, dst);
        }
        fclose(src);
        fclose(dst);
        log_event("  -> Checkpoint '%s' created for '%s'", req.tag, req.filename);
        send_simple_header(nm_sock, RES_OK);
    } else {
        if (src) fclose(src);
        if (dst) fclose(dst);
        log_event("  -> Error creating checkpoint: %s", strerror(errno));
        send_simple_header(nm_sock, RES_ERROR);
    }
}

// Handle sending checkpoint content back to Name Server.
static void handle_ss_viewcheckpoint(int nm_sock) {
    Msg_Checkpoint_Request req; 
    recv(nm_sock, &req, sizeof(req), 0);
    log_event("Got REQ_SS_VIEWCHECKPOINT for '%s' tag '%s' from NM", req.filename, req.tag);
    
    char checkpoint_path[1024];
    sprintf(checkpoint_path, "%s/.checkpoints/%s/%s", g_storage_path, req.filename, req.tag);
    
    struct stat st;
    if (stat(checkpoint_path, &st) != 0) {
        log_event("  -> Error: Checkpoint '%s' not found for file '%s'", req.tag, req.filename);
        send_simple_header(nm_sock, RES_ERROR);
        return;
    }
    
    FILE *f = fopen(checkpoint_path, "r");
    if (f) {
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        
        Header res_hdr;
        res_hdr.type = RES_SS_FILE_OK;
        res_hdr.payload_size = size;
        send(nm_sock, &res_hdr, sizeof(res_hdr), 0);
        
        char buffer[4096];
        size_t bytes;
        while ((bytes = fread(buffer, 1, sizeof(buffer), f)) > 0) {
            send(nm_sock, buffer, bytes, 0);
        }
        fclose(f);
        log_event("  -> Sent checkpoint content (%ld bytes)", size);
    } else {
        log_event("  -> Error opening checkpoint: %s", strerror(errno));
        send_simple_header(nm_sock, RES_ERROR);
    }
}

// Handle reverting file state on Storage Server.
static void handle_ss_revert(int nm_sock) {
    Msg_Checkpoint_Request req; 
    recv(nm_sock, &req, sizeof(req), 0);
    log_event("Got REQ_SS_REVERT for '%s' to tag '%s' from NM", req.filename, req.tag);
    
    char file_path[768], checkpoint_path[1024], undo_path[1024];
    sprintf(file_path, "%s/%s", g_storage_path, req.filename);
    sprintf(checkpoint_path, "%s/.checkpoints/%s/%s", g_storage_path, req.filename, req.tag);
    sprintf(undo_path, "%s/.checkpoints/%s/__UNDO__", g_storage_path, req.filename);
    
    struct stat st;
    if (stat(checkpoint_path, &st) != 0) {
        log_event("  -> Error: Checkpoint '%s' not found for file '%s'", req.tag, req.filename);
        send_simple_header(nm_sock, RES_ERROR);
        return;
    }
    
    FILE *current_file = fopen(file_path, "r");
    FILE *undo_file = fopen(undo_path, "w");
    if (current_file && undo_file) {
        char buffer[4096];
        size_t bytes;
        while ((bytes = fread(buffer, 1, sizeof(buffer), current_file)) > 0) {
            fwrite(buffer, 1, bytes, undo_file);
        }
        fclose(current_file);
        fclose(undo_file);
        log_event("  -> Saved current state to __UNDO__ checkpoint");
    } else {
        if (current_file) fclose(current_file);
        if (undo_file) fclose(undo_file);
    }
    
    FILE *src = fopen(checkpoint_path, "r");
    FILE *dst = fopen(file_path, "w");
    if (src && dst) {
        char buffer[4096];
        size_t bytes;
        while ((bytes = fread(buffer, 1, sizeof(buffer), src)) > 0) {
            fwrite(buffer, 1, bytes, dst);
        }
        fclose(src);
        fclose(dst);
        
        char backup_path[1024];
        sprintf(backup_path, "%s.bak", file_path);
        FILE *bak = fopen(checkpoint_path, "r");
        FILE *bak_dst = fopen(backup_path, "w");
        if (bak && bak_dst) {
            while ((bytes = fread(buffer, 1, sizeof(buffer), bak)) > 0) {
                fwrite(buffer, 1, bytes, bak_dst);
            }
            fclose(bak);
            fclose(bak_dst);
        }
        
        log_event("  -> File '%s' reverted to checkpoint '%s'", req.filename, req.tag);
        send_simple_header(nm_sock, RES_OK);
    } else {
        if (src) fclose(src);
        if (dst) fclose(dst);
        log_event("  -> Error reverting file: %s", strerror(errno));
        send_simple_header(nm_sock, RES_ERROR);
    }
}

// Handle listing checkpoints on Storage Server.
static void handle_ss_listcheckpoints(int nm_sock) {
    Msg_ListCheckpoints_Request req; 
    recv(nm_sock, &req, sizeof(req), 0);
    log_event("Got REQ_SS_LISTCHECKPOINTS for '%s' from NM", req.filename);
    
    char checkpoint_dir[768];
    sprintf(checkpoint_dir, "%s/.checkpoints/%s", g_storage_path, req.filename);
    
    int count = 0;
    Msg_Checkpoint_Item items[MAX_FILES_PER_SS];
    
    DIR *d = opendir(checkpoint_dir);
    if (d) {
        struct dirent *dir;
        while ((dir = readdir(d)) != NULL && count < MAX_FILES_PER_SS) {
            if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0) continue;
            if (strcmp(dir->d_name, "__UNDO__") == 0) continue;
            if (strcmp(dir->d_name, "__REDO__") == 0) continue;
            
            char checkpoint_path[1024];
            sprintf(checkpoint_path, "%s/%s", checkpoint_dir, dir->d_name);
            
            struct stat st;
            if (stat(checkpoint_path, &st) == 0 && S_ISREG(st.st_mode)) {
                strncpy(items[count].tag, dir->d_name, MAX_CHECKPOINT_TAG);
                items[count].tag[MAX_CHECKPOINT_TAG - 1] = '\0';
                items[count].timestamp = st.st_mtime;
                count++;
            }
        }
        closedir(d);
    }
    
    Header res_hdr;
    res_hdr.type = RES_CHECKPOINT_LIST;
    res_hdr.payload_size = sizeof(Msg_Checkpoint_List_Hdr);
    send(nm_sock, &res_hdr, sizeof(res_hdr), 0);
    
    Msg_Checkpoint_List_Hdr hdr;
    hdr.checkpoint_count = count;
    send(nm_sock, &hdr, sizeof(hdr), 0);
    
    for (int i = 0; i < count; i++) {
        send(nm_sock, &items[i], sizeof(Msg_Checkpoint_Item), 0);
    }
    
    log_event("  -> Sent %d checkpoint(s) for '%s'", count, req.filename);
}

// Handle replicating file backup request.
static void handle_ss_replicate_file(int nm_sock) {
    Msg_Replicate_File rep_msg; 
    recv(nm_sock, &rep_msg, sizeof(rep_msg), 0);
    log_event("Got REQ_REPLICATE_FILE for '%s' (backup copy)", rep_msg.filename);
    
    save_file_metadata(rep_msg.filename, rep_msg.owner, rep_msg.access_count, rep_msg.access_list);
    
    char file_path[768];
    sprintf(file_path, "%s/%s", g_storage_path, rep_msg.filename);
    int fd = open(file_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        close(fd);
        log_event("  -> Backup file '%s' created", rep_msg.filename);
    }
}

// Handle heartbeat check from Naming Server.
static void handle_ss_heartbeat(int nm_sock) {
    Header hb_res;
    hb_res.type = RES_SS_HEARTBEAT;
    hb_res.payload_size = 0;
    send(nm_sock, &hb_res, sizeof(hb_res), 0);
}

// Handle request from Naming Server to fetch file content.
static void handle_ss_get_file_content(int nm_sock) {
    Msg_Sync_File sync_msg;
    recv(nm_sock, &sync_msg, sizeof(sync_msg), 0);
    log_event("Got REQ_GET_FILE_CONTENT for '%s' (backup restore)", sync_msg.filename);
    
    char file_path[768];
    sprintf(file_path, "%s/%s", g_storage_path, sync_msg.filename);
    
    FILE* file = fopen(file_path, "rb");
    if (!file) {
        long zero = 0;
        send(nm_sock, &zero, sizeof(long), 0);
        log_event("  -> File not found");
        return;
    }
    
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    send(nm_sock, &file_size, sizeof(long), 0);
    
    char buffer[4096];
    int bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        send(nm_sock, buffer, bytes_read, 0);
    }
    fclose(file);
    log_event("  -> Sent %ld bytes", file_size);
}

// Handle restoring file content from backup SS.
static void handle_ss_sync_from_backup(int nm_sock) {
    Msg_Sync_File sync_msg;
    recv(nm_sock, &sync_msg, sizeof(sync_msg), 0);
    
    long file_size;
    recv(nm_sock, &file_size, sizeof(long), 0);
    
    log_event("Got REQ_SYNC_FROM_BACKUP for '%s' (%ld bytes)", sync_msg.filename, file_size);
    
    char file_path[768];
    sprintf(file_path, "%s/%s", g_storage_path, sync_msg.filename);
    
    FILE* file = fopen(file_path, "wb");
    if (!file) {
        log_event("  -> Error creating file");
        return;
    }
    
    char* content = malloc(file_size);
    if (content) {
        int received = 0;
        while (received < file_size) {
            int bytes = recv(nm_sock, content + received, file_size - received, 0);
            if (bytes <= 0) break;
            received += bytes;
        }
        fwrite(content, 1, received, file);
        free(content);
        log_event("  -> File restored (%d bytes)", received);
    }
    fclose(file);
}

// Routing function for messages from the Naming Server.
void handle_nm_message_ss(int sock_fd, Header header, int nm_sock, fd_set* master_set) {
    switch (header.type) {
        case REQ_SS_CREATE:
            handle_ss_create(nm_sock);
            break;
        case REQ_SS_DELETE:
            handle_ss_delete(nm_sock);
            break;
        case REQ_SS_UNDO:
            handle_ss_undo(nm_sock);
            break;
        case REQ_SS_ADD_ACCESS:
            handle_ss_add_access(nm_sock);
            break;
        case REQ_SS_REM_ACCESS:
            handle_ss_rem_access(nm_sock);
            break;
        case REQ_SS_CREATEFOLDER:
            handle_ss_createfolder(nm_sock);
            break;
        case REQ_SS_CHECKFOLDER:
            handle_ss_checkfolder(nm_sock);
            break;
        case REQ_SS_MOVE:
            handle_ss_move(nm_sock);
            break;
        case REQ_SS_CHECKPOINT:
            handle_ss_checkpoint(nm_sock);
            break;
        case REQ_SS_VIEWCHECKPOINT:
            handle_ss_viewcheckpoint(nm_sock);
            break;
        case REQ_SS_REVERT:
            handle_ss_revert(nm_sock);
            break;
        case REQ_SS_LISTCHECKPOINTS:
            handle_ss_listcheckpoints(nm_sock);
            break;
        case REQ_REPLICATE_FILE:
            handle_ss_replicate_file(nm_sock);
            break;
        case REQ_SS_HEARTBEAT:
            handle_ss_heartbeat(nm_sock);
            break;
        case REQ_GET_FILE_CONTENT:
            handle_ss_get_file_content(nm_sock);
            break;
        case REQ_SYNC_FROM_BACKUP:
            handle_ss_sync_from_backup(nm_sock);
            break;
        default:
            log_event("Got unknown command %d from NM", header.type);
            break;
    }
}

// Handle file read request from client.
static void handle_client_read(int sock_fd, fd_set* master_set) {
    Msg_Filename_Request req; 
    recv(sock_fd, &req, sizeof(req), 0);
    log_event("Got REQ_CLIENT_READ for '%s' from socket %d", req.filename, sock_fd);
    handle_send_file(sock_fd, req.filename);
    close(sock_fd); 
    FD_CLR(sock_fd, master_set);
}

// Handle file stream request from client.
static void handle_client_stream(int sock_fd, fd_set* master_set) {
    Msg_Filename_Request req; 
    recv(sock_fd, &req, sizeof(req), 0);
    log_event("Got REQ_CLIENT_STREAM for '%s' from socket %d", req.filename, sock_fd);
    handle_stream_file(sock_fd, req.filename);
    close(sock_fd); 
    FD_CLR(sock_fd, master_set);
}

// Handle write request (sentence locking phase) from client.
static void handle_client_write(int sock_fd, fd_set* master_set) {
    Msg_Client_Write req; 
    recv(sock_fd, &req, sizeof(req), 0);
    log_event("Got REQ_CLIENT_WRITE for '%s' (sent %d) from socket %d", req.filename, req.sentence_num, sock_fd);
    
    ActiveDoc* doc = find_or_load_active_doc(req.filename);
    if (doc == NULL) {
        log_event("  -> ERROR: Failed to load document for writing.");
        send_simple_header(sock_fd, RES_ERROR);
        close(sock_fd); 
        FD_CLR(sock_fd, master_set);
        return;
    }
    
    SentenceNode* target = doc->doc_head;
    for (int i = 0; i < req.sentence_num && target != NULL; i++) {
        target = target->next;
    }
    
    if (target == NULL) {
        log_event("  -> ERROR: Sentence index %d out of bounds.", req.sentence_num);
        send_simple_header(sock_fd, RES_ERROR_INVALID_SENTENCE);
        close(sock_fd); 
        FD_CLR(sock_fd, master_set);
        return;
    }
    
    if (find_lock(req.filename, target) != -1) {
        log_event("  -> Lock conflict! Sending error.");
        send_simple_header(sock_fd, RES_ERROR_LOCKED);
        close(sock_fd); 
        FD_CLR(sock_fd, master_set);
    } else {
        int original_word_count = 0;
        WordNode* w = target->word_head;
        while (w != NULL) { 
            original_word_count++; 
            w = w->next; 
        }
        
        create_lock(req.filename, target, sock_fd);
        doc->num_users_editing++;
        
        WriteSession* session = &write_sessions[sock_fd];
        session->active = 1;
        session->doc_index = doc - active_documents;
        session->sentence_ptr = target;
        session->original_word_count = original_word_count;
        session->edit_capacity = 100;
        session->edit_ops = (EditOperation*)malloc(session->edit_capacity * sizeof(EditOperation));
        session->edit_count = 0;
        session->virtual_word_count = 0;
        
        log_event("  -> Lock granted. Session started. Original word count: %d, Users editing: %d", 
                 original_word_count, doc->num_users_editing);
        send_simple_header(sock_fd, RES_OK_LOCKED);
    }
}

// Handle write update operation from client.
static void handle_write_update(int sock_fd) {
    WriteSession* session = &write_sessions[sock_fd];
    Msg_Write_Update req; 
    recv(sock_fd, &req, sizeof(req), 0);
    
    SentenceNode* target = session->sentence_ptr;
    if (target == NULL) {
        log_event("  -> ERROR: Locked sentence pointer is NULL");
        Header response_header;
        response_header.type = RES_ERROR;
        response_header.payload_size = 0;
        send(sock_fd, &response_header, sizeof(response_header), 0);
        return;
    }
    
    int current_word_count = 0;
    WordNode* w = target->word_head;
    while (w != NULL) { 
        current_word_count++; 
        w = w->next; 
    }
    
    int total_word_count = current_word_count + session->virtual_word_count;
    
    if (req.word_index < 0 || req.word_index > total_word_count) {
        log_event("  -> ERROR: Word index %d out of bounds (valid range: 0-%d)", req.word_index, total_word_count);
        Header response_header;
        response_header.type = RES_ERROR;
        response_header.payload_size = 0;
        send(sock_fd, &response_header, sizeof(response_header), 0);
        return;
    }
    
    if (session->edit_count >= session->edit_capacity) {
        session->edit_capacity *= 2;
        session->edit_ops = (EditOperation*)realloc(session->edit_ops, session->edit_capacity * sizeof(EditOperation));
    }
    session->edit_ops[session->edit_count].word_index = req.word_index;
    strncpy(session->edit_ops[session->edit_count].content, req.content, sizeof(session->edit_ops[0].content) - 1);
    session->edit_ops[session->edit_count].content[sizeof(session->edit_ops[0].content) - 1] = '\0';
    
    int added_words = count_words_in_string(req.content);
    session->virtual_word_count += added_words;
    session->edit_count++;
    
    log_event("  -> Recorded edit operation #%d (word %d) for socket %d", session->edit_count, req.word_index, sock_fd);
    
    Header response_header;
    response_header.type = RES_OK;
    response_header.payload_size = 0;
    send(sock_fd, &response_header, sizeof(response_header), 0);
}

// Handle end of write update and commit from client.
static void handle_etirw(int sock_fd, int nm_sock, fd_set* master_set) {
    WriteSession* session = &write_sessions[sock_fd];
    ActiveDoc* doc = &active_documents[session->doc_index];
    log_event("Got REQ_ETIRW from socket %d. Committing %d edit operations for '%s'.", sock_fd, session->edit_count, doc->filename);
    
    SentenceNode* target = session->sentence_ptr;
    if (target == NULL) {
        log_event("  -> ERROR: Locked sentence pointer is NULL");
        send_simple_header(sock_fd, RES_ERROR);
        return;
    }
    
    int current_word_count = 0;
    WordNode* w = target->word_head;
    while (w != NULL) { 
        current_word_count++; 
        w = w->next; 
    }
    
    int word_offset = current_word_count - session->original_word_count;
    log_event("  -> Original word count: %d, Current: %d, Offset: %d", session->original_word_count, current_word_count, word_offset);
    
    int sentence_idx = 0;
    SentenceNode* temp = doc->doc_head;
    while (temp != NULL && temp != target) {
        sentence_idx++;
        temp = temp->next;
    }
    
    if (temp == NULL) {
        log_event("  -> ERROR: Could not find locked sentence in document");
        send_simple_header(sock_fd, RES_ERROR);
        return;
    }
    
    int validation_failed = 0;
    
    int doc_words_before = 0;
    SentenceNode* tmp = doc->doc_head;
    while (tmp) { 
        WordNode* tw = tmp->word_head; 
        while (tw) { 
            doc_words_before++; 
            tw = tw->next; 
        } 
        tmp = tmp->next; 
    }
    
    if (session->edit_count > 0 && word_offset != 0) {
        session->edit_ops[0].word_index += word_offset;
        log_event("  -> Adjusted first edit index by offset %d", word_offset);
    }
    
    if (apply_session_edits_to_sentence(doc, target, session) != 0) {
        log_event("  -> ERROR: Failed to apply session edits atomically");
        send_simple_header(sock_fd, RES_ERROR);
        validation_failed = 1;
    }
    
    if (!validation_failed) {
        int doc_words_after = 0;
        tmp = doc->doc_head;
        while (tmp) { 
            WordNode* tw = tmp->word_head; 
            while (tw) { 
                doc_words_after++; 
                tw = tw->next; 
            } 
            tmp = tmp->next; 
        }
        int added = doc_words_after - doc_words_before;
        
        if (added != session->virtual_word_count) {
            log_event("  -> ERROR: Virtual added words (%d) != actual added (%d). Rejecting.", session->virtual_word_count, added);
            send_simple_header(sock_fd, RES_ERROR_INVALID_SENTENCE);
            validation_failed = 1;
        } else {
            rename(doc->original_path, doc->backup_path);
            flush_list_to_file(doc->doc_head, doc->original_path);
        }
    }
    
    release_lock(doc->filename, session->sentence_ptr);
    free(session->edit_ops);
    session->edit_ops = NULL;
    session->active = 0;
    release_active_doc(doc);
    
    calculate_and_send_metadata(nm_sock, doc->filename, doc->original_path);
    send_simple_header(sock_fd, RES_OK);
    log_event("  -> Commit successful for socket %d", sock_fd);
    close(sock_fd);
    FD_CLR(sock_fd, master_set);
}

// Routing function for messages from the connected client.
void handle_client_message_ss(int sock_fd, Header header, int nm_sock, fd_set* master_set) {
    switch (header.type) {
        case REQ_CLIENT_READ:
            handle_client_read(sock_fd, master_set);
            break;
        case REQ_CLIENT_STREAM:
            handle_client_stream(sock_fd, master_set);
            break;
        case REQ_CLIENT_WRITE:
            handle_client_write(sock_fd, master_set);
            break;
        case REQ_WRITE_UPDATE:
            if (!write_sessions[sock_fd].active) { 
                log_event("Error: Got REQ_WRITE_UPDATE from socket %d with no active session.", sock_fd); 
                break; 
            }
            handle_write_update(sock_fd);
            break;
        case REQ_ETIRW:
            if (!write_sessions[sock_fd].active) { 
                log_event("Error: Got REQ_ETIRW from socket %d with no active session.", sock_fd); 
                break; 
            }
            handle_etirw(sock_fd, nm_sock, master_set);
            break;
        default:
            log_event("Got unknown command %d from client %d", header.type, sock_fd);
    }
}
