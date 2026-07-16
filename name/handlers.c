// Message processing logic for the Naming Server (NM).

#include "handlers.h"
#include "protocol.h"
#include "name_globals.h"
#include "hash_cache.h"
#include "fault_tolerance.h"
#include "catalog.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

extern FILE* nm_log_file;
extern void log_event(const char* format, ...);
extern void handle_client_register(int sock_fd, char* peer_ip);
extern void handle_ss_register(int sock_fd, char* peer_ip);

// Handle file creation request.
static void handle_req_create(int sock_fd, Header header) {
    Msg_Filename_Request req; 
    recv(sock_fd, &req, sizeof(req), 0);
    log_event("Got REQ_CREATE for '%s' from client '%s' (Sock %d)", req.filename, client_state[sock_fd].username, sock_fd);
    
    if (find_file_slot(req.filename) != -1) { 
        log_event("  -> Error: File '%s' already exists.", req.filename); 
        send_simple_header(sock_fd, RES_ERROR_FILE_EXISTS);
        return;
    } 
    
    int ss_sock = find_available_ss();
    if (ss_sock == -1) { 
        log_event("  -> Error: No Storage Servers available."); 
        send_simple_header(sock_fd, RES_ERROR); 
        return;
    }
    
    log_event("  -> Relaying REQ_SS_CREATE to SS on socket %d", ss_sock);
    Header ss_header; 
    ss_header.type = REQ_SS_CREATE; 
    ss_header.payload_size = sizeof(Msg_SS_Create_Request);
    
    Msg_SS_Create_Request ss_req;
    strncpy(ss_req.filename, req.filename, MAX_FILENAME);
    strncpy(ss_req.owner, client_state[sock_fd].username, MAX_USERNAME);
    
    send(ss_sock, &ss_header, sizeof(ss_header), 0); 
    send(ss_sock, &ss_req, sizeof(ss_req), 0);
    
    recv(ss_sock, &header, sizeof(Header), 0); 
    if (header.type == RES_OK) {
        log_event("  -> SS confirmed creation. Updating catalog.");
        int slot = find_empty_file_slot();
        file_catalog[slot].active = 1; 
        strncpy(file_catalog[slot].filename, req.filename, MAX_FILENAME);
        file_catalog[slot].ss_sock_fd = ss_sock; 
        strncpy(file_catalog[slot].owner, client_state[sock_fd].username, MAX_USERNAME);
        file_catalog[slot].access_count = 0;
        
        if (count_active_ss() >= 2) {
            int backup = find_backup_ss(ss_sock);
            if (backup >= 0) {
                file_catalog[slot].backup_ss_sock = backup;
                replicate_to_backup(backup, &file_catalog[slot]);
            } else {
                file_catalog[slot].backup_ss_sock = -1;
            }
        } else {
            file_catalog[slot].backup_ss_sock = -1;
        }
        
        add_to_hashmap(req.filename, slot);
        send_ok_response(sock_fd);
    } else { 
        log_event("  -> SS failed to create file."); 
        send_simple_header(sock_fd, RES_ERROR); 
    }
}

// Handle file read, write, or stream requests.
static void handle_req_read_write_stream(int sock_fd, Header header) {
    Msg_Filename_Request req; 
    recv(sock_fd, &req, sizeof(req), 0);
    log_event("Got %d for '%s' from client '%s'", header.type, req.filename, client_state[sock_fd].username);
    
    int slot = find_file_slot(req.filename);
    if (slot == -1) { 
        log_event("  -> Error: File not found."); 
        send_simple_header(sock_fd, RES_ERROR_NOT_FOUND); 
        return;
    }
    
    FileMetadata* meta = &file_catalog[slot];
    int has_perm = 0;
    if (header.type == REQ_WRITE) {
        has_perm = has_write_access(meta, client_state[sock_fd].username);
    } else {
        has_perm = has_read_access(meta, client_state[sock_fd].username);
    }
    
    if (!has_perm) {
        log_event("  -> Access Denied for user '%s'.", client_state[sock_fd].username);
        send_simple_header(sock_fd, RES_ERROR_ACCESS_DENIED); 
        return;
    }
    
    SSInfo* ss = &ss_state[meta->ss_sock_fd];
    if (!ss->active && meta->backup_ss_sock >= 0 && ss_state[meta->backup_ss_sock].active) {
        log_event("  -> Primary SS offline, failing over to backup SS %d", meta->backup_ss_sock);
        ss = &ss_state[meta->backup_ss_sock];
    } else if (!ss->active) {
        log_event("  -> Error: SS for file is offline and no backup available."); 
        send_simple_header(sock_fd, RES_ERROR); 
        return;
    }
    
    log_event("  -> File found on SS %d (%s:%d)", meta->ss_sock_fd, ss->ip, ss->client_port);
    Header res_hdr; 
    res_hdr.type = RES_READ_LOCATION; 
    res_hdr.payload_size = sizeof(Msg_Read_Response);
    
    Msg_Read_Response res_payload; 
    strncpy(res_payload.ss_ip, ss->ip, MAX_IP_LEN); 
    res_payload.ss_port = ss->client_port;
    
    send(sock_fd, &res_hdr, sizeof(res_hdr), 0); 
    send(sock_fd, &res_payload, sizeof(res_payload), 0);
}

// Handle listing users request.
static void handle_req_list(int sock_fd, Header header) {
    log_event("Got REQ_LIST from client '%s' (Sock %d)", client_state[sock_fd].username, sock_fd);
    int count = 0; 
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (client_state[i].active) count++;
    }
    
    Header res_hdr; 
    res_hdr.type = RES_LIST_HDR; 
    res_hdr.payload_size = sizeof(Msg_List_Hdr);
    
    Msg_List_Hdr list_hdr; 
    list_hdr.user_count = count;
    
    send(sock_fd, &res_hdr, sizeof(res_hdr), 0); 
    send(sock_fd, &list_hdr, sizeof(list_hdr), 0);
    
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (client_state[i].active) { 
            Msg_List_Item item; 
            strncpy(item.username, client_state[i].username, MAX_USERNAME); 
            send(sock_fd, &item, sizeof(item), 0); 
        }
    }
}

// Handle file deletion request.
static void handle_req_delete(int sock_fd, Header header) {
    Msg_Filename_Request req; 
    recv(sock_fd, &req, sizeof(req), 0);
    log_event("Got REQ_DELETE for '%s' from client '%s'", req.filename, client_state[sock_fd].username);
    
    int slot = find_file_slot(req.filename);
    if (slot == -1) { 
        send_simple_header(sock_fd, RES_ERROR_NOT_FOUND); 
        return; 
    }
    
    FileMetadata* meta = &file_catalog[slot];
    if (strcmp(meta->owner, client_state[sock_fd].username) != 0) {
        log_event("  -> Access Denied. User '%s' is not owner '%s'.", client_state[sock_fd].username, meta->owner);
        send_simple_header(sock_fd, RES_ERROR_ACCESS_DENIED); 
        return;
    }
    
    int ss_sock = meta->ss_sock_fd;
    if (!ss_state[ss_sock].active) { 
        send_simple_header(sock_fd, RES_ERROR); 
        return; 
    }
    
    log_event("  -> Relaying REQ_SS_DELETE to SS %d", ss_sock);
    Header ss_header; 
    ss_header.type = REQ_SS_DELETE; 
    ss_header.payload_size = sizeof(Msg_Filename_Request);
    
    send(ss_sock, &ss_header, sizeof(ss_header), 0); 
    send(ss_sock, &req, sizeof(req), 0);
    
    recv(ss_sock, &header, sizeof(Header), 0);
    
    if (meta->backup_ss_sock >= 0 && ss_state[meta->backup_ss_sock].active) {
        log_event("  -> Deleting from backup SS %d", meta->backup_ss_sock);
        send(meta->backup_ss_sock, &ss_header, sizeof(ss_header), MSG_DONTWAIT);
        send(meta->backup_ss_sock, &req, sizeof(req), MSG_DONTWAIT);
    }
    
    remove_from_hashmap(file_catalog[slot].filename);
    invalidate_cache(file_catalog[slot].filename);
    
    file_catalog[slot].active = 0;
    send_ok_response(sock_fd);
}

// Handle file undo request.
static void handle_req_undo(int sock_fd, Header header) {
    Msg_Filename_Request req; 
    recv(sock_fd, &req, sizeof(req), 0);
    log_event("Got REQ_UNDO for '%s' from client '%s'", req.filename, client_state[sock_fd].username);
    
    int slot = find_file_slot(req.filename);
    if (slot == -1) { 
        send_simple_header(sock_fd, RES_ERROR_NOT_FOUND); 
        return; 
    }
    
    FileMetadata* meta = &file_catalog[slot];
    if (!has_write_access(meta, client_state[sock_fd].username)) {
        log_event("  -> Access Denied (UNDO). User '%s' does not have write access.", client_state[sock_fd].username);
        send_simple_header(sock_fd, RES_ERROR_ACCESS_DENIED); 
        return;
    }
    
    int ss_sock = meta->ss_sock_fd;
    if (!ss_state[ss_sock].active) { 
        send_simple_header(sock_fd, RES_ERROR); 
        return; 
    }
    
    log_event("  -> Relaying REQ_SS_UNDO to SS %d", ss_sock);
    Header ss_header; 
    ss_header.type = REQ_SS_UNDO; 
    ss_header.payload_size = sizeof(Msg_Filename_Request);
    
    send(ss_sock, &ss_header, sizeof(ss_header), 0); 
    send(ss_sock, &req, sizeof(req), 0);
    
    recv(ss_sock, &header, sizeof(Header), 0);
    send_ok_response(sock_fd);
}

// Handle file metadata update.
static void handle_req_update_metadata(int sock_fd, Header header) {
    Msg_Update_Metadata msg; 
    recv(sock_fd, &msg, sizeof(msg), 0);
    log_event("Got REQ_UPDATE_METADATA for '%s' from SS %d", msg.filename, sock_fd);
    
    int slot = find_file_slot(msg.filename);
    if (slot != -1) {
        file_catalog[slot].file_size = msg.file_size;
        file_catalog[slot].word_count = msg.word_count;
        file_catalog[slot].char_count = msg.char_count;
        file_catalog[slot].last_modified = msg.last_modified;
        file_catalog[slot].last_accessed = msg.last_accessed;
        log_event("  -> Metadata updated for '%s'", msg.filename);
    } else { 
        log_event("  -> ERROR: Received metadata for unknown file '%s'", msg.filename); 
    }
}

// Handle viewing catalog request.
static void handle_req_view(int sock_fd, Header header) {
    Msg_View_Request req; 
    recv(sock_fd, &req, sizeof(req), 0);
    log_event("Got REQ_VIEW (a=%d, l=%d) from client '%s'", req.flag_a, req.flag_l, client_state[sock_fd].username);
    
    int count = 0;
    for (int i = 0; i < MAX_FILES_IN_SYSTEM; i++) {
        if (!file_catalog[i].active) continue;
        char* filename = file_catalog[i].filename;
        if (strstr(filename, ".bak") || strstr(filename, ".checkpoints/") || strstr(filename, "/.checkpoints/")) continue;
        if (req.flag_a || has_read_access(&file_catalog[i], client_state[sock_fd].username)) {
            count++;
        }
    }
    
    Header res_hdr; 
    res_hdr.type = RES_VIEW_HDR; 
    res_hdr.payload_size = sizeof(Msg_View_Hdr);
    
    Msg_View_Hdr view_hdr; 
    view_hdr.file_count = count;
    
    send(sock_fd, &res_hdr, sizeof(res_hdr), 0); 
    send(sock_fd, &view_hdr, sizeof(view_hdr), 0);
    
    for (int i = 0; i < MAX_FILES_IN_SYSTEM; i++) {
        if (!file_catalog[i].active) continue;
        char* filename = file_catalog[i].filename;
        if (strstr(filename, ".bak") || strstr(filename, ".checkpoints/") || strstr(filename, "/.checkpoints/")) continue;
        if (req.flag_a || has_read_access(&file_catalog[i], client_state[sock_fd].username)) {
            if (req.flag_l) {
                send_full_metadata(sock_fd, &file_catalog[i], RES_VIEW_ITEM_LONG);
            } else {
                res_hdr.type = RES_VIEW_ITEM_SHORT; 
                res_hdr.payload_size = sizeof(Msg_View_Item_Short);
                Msg_View_Item_Short item; 
                strncpy(item.filename, file_catalog[i].filename, MAX_FILENAME);
                send(sock_fd, &res_hdr, sizeof(res_hdr), 0); 
                send(sock_fd, &item, sizeof(item), 0);
            }
        }
    }
}

// Handle file information request.
static void handle_req_info(int sock_fd, Header header) {
    Msg_Filename_Request req; 
    recv(sock_fd, &req, sizeof(req), 0);
    log_event("Got REQ_INFO for '%s' from client '%s'", req.filename, client_state[sock_fd].username);
    
    int slot = find_file_slot(req.filename);
    if (slot == -1) { 
        send_simple_header(sock_fd, RES_ERROR_NOT_FOUND); 
        return;
    }
    
    FileMetadata* meta = &file_catalog[slot];
    send_full_metadata(sock_fd, meta, RES_INFO);
}

// Handle adding access control permissions.
static void handle_req_add_access(int sock_fd, Header header) {
    Msg_Access_Request req; 
    recv(sock_fd, &req, sizeof(req), 0);
    log_event("Got REQ_ADD_ACCESS for '%s' (user: %s, perm: %d) from client '%s'", req.filename, req.username, req.perm, client_state[sock_fd].username);
    
    int slot = find_file_slot(req.filename);
    if (slot == -1) { 
        send_simple_header(sock_fd, RES_ERROR_NOT_FOUND); 
        return; 
    }
    
    FileMetadata* meta = &file_catalog[slot];
    if (strcmp(meta->owner, client_state[sock_fd].username) != 0) {
        send_simple_header(sock_fd, RES_ERROR_ACCESS_DENIED); 
        return;
    }
    
    int existing_idx = -1;
    for (int i = 0; i < meta->access_count; i++) {
        if (strcmp(meta->access_list[i].username, req.username) == 0) {
            existing_idx = i;
            break;
        }
    }
    
    if (existing_idx != -1) {
        meta->access_list[existing_idx].permission = req.perm;
        log_event("  -> Updated existing access for user '%s'.", req.username);
    } else {
        if (meta->access_count >= MAX_PERMISSIONS_PER_FILE) {
            send_simple_header(sock_fd, RES_ERROR); 
            return;
        }
        AccessEntry* new_entry = &meta->access_list[meta->access_count];
        strncpy(new_entry->username, req.username, MAX_USERNAME);
        new_entry->permission = req.perm;
        meta->access_count++;
        log_event("  -> Access granted to new user '%s'.", req.username);
    }
    
    int ss_sock = meta->ss_sock_fd;
    if (ss_state[ss_sock].active) {
        Header ss_header; 
        ss_header.type = REQ_SS_ADD_ACCESS; 
        ss_header.payload_size = sizeof(Msg_Access_Request);
        send(ss_sock, &ss_header, sizeof(ss_header), 0);
        send(ss_sock, &req, sizeof(req), 0);
    }
    
    send_ok_response(sock_fd);
}

// Handle removing access control permissions.
static void handle_req_rem_access(int sock_fd, Header header) {
    Msg_Access_Request req; 
    recv(sock_fd, &req, sizeof(req), 0);
    log_event("Got REQ_REM_ACCESS for '%s' (user: %s) from client '%s'", req.filename, req.username, client_state[sock_fd].username);
    
    int slot = find_file_slot(req.filename);
    if (slot == -1) { 
        send_simple_header(sock_fd, RES_ERROR_NOT_FOUND); 
        return; 
    }
    
    FileMetadata* meta = &file_catalog[slot];
    if (strcmp(meta->owner, client_state[sock_fd].username) != 0) {
        send_simple_header(sock_fd, RES_ERROR_ACCESS_DENIED); 
        return;
    }
    
    int removed_count = 0;
    for (int i = 0; i < meta->access_count; ) {
        if (strcmp(meta->access_list[i].username, req.username) == 0) {
            for (int j = i; j < meta->access_count - 1; j++) {
                meta->access_list[j] = meta->access_list[j+1];
            }
            meta->access_count--;
            removed_count++;
        } else {
            i++;
        }
    }
    
    if (removed_count > 0) {
        log_event("  -> Access removed (%d entries).", removed_count);
        int ss_sock = meta->ss_sock_fd;
        if (ss_state[ss_sock].active) {
            Header ss_header; 
            ss_header.type = REQ_SS_REM_ACCESS; 
            ss_header.payload_size = sizeof(Msg_Access_Request);
            send(ss_sock, &ss_header, sizeof(ss_header), 0);
            send(ss_sock, &req, sizeof(req), 0);
        }
    }
    send_ok_response(sock_fd);
}

// Handle command execution request.
static void handle_req_exec(int sock_fd, Header header) {
    Msg_Filename_Request req; 
    recv(sock_fd, &req, sizeof(req), 0);
    log_event("Got REQ_EXEC for '%s' from client '%s'", req.filename, client_state[sock_fd].username);
    
    int slot = find_file_slot(req.filename);
    if (slot == -1) { 
        log_event("  -> Error: File not found."); 
        send_simple_header(sock_fd, RES_ERROR_NOT_FOUND); 
        return; 
    }
    
    FileMetadata* meta = &file_catalog[slot];
    if (!has_read_access(meta, client_state[sock_fd].username)) {
        log_event("  -> Access Denied for user '%s'.", client_state[sock_fd].username);
        send_simple_header(sock_fd, RES_ERROR_ACCESS_DENIED); 
        return;
    }

    SSInfo* ss = &ss_state[meta->ss_sock_fd];
    if (!ss->active) { 
        log_event("  -> Error: SS for file is offline."); 
        send_simple_header(sock_fd, RES_ERROR); 
        return; 
    }
    
    log_event("  -> NM acting as client to fetch file content from SS...");
    char* script_content = get_file_content_from_ss(ss, req.filename);
    if (script_content == NULL) {
        log_event("  -> ERROR: Failed to fetch script content from SS.");
        send_simple_header(sock_fd, RES_ERROR);
        return;
    }
    
    log_event("  -> Executing script: \n---\n%s\n---", script_content);
    FILE* pipe = popen(script_content, "r");
    free(script_content);
    
    if (pipe == NULL) {
        log_event("  -> ERROR: popen() failed.");
        send_simple_header(sock_fd, RES_ERROR);
        return;
    }

    char line_buffer[FILE_BUFFER_SIZE];
    while (fgets(line_buffer, sizeof(line_buffer), pipe) != NULL) {
        Header out_hdr;
        out_hdr.type = RES_EXEC_OUTPUT;
        out_hdr.payload_size = sizeof(Msg_Exec_Output);
        
        Msg_Exec_Output out_msg;
        strncpy(out_msg.line, line_buffer, FILE_BUFFER_SIZE);
        
        send(sock_fd, &out_hdr, sizeof(out_hdr), 0);
        send(sock_fd, &out_msg, sizeof(out_msg), 0);
    }
    
    pclose(pipe);
    log_event("  -> Execution finished. Sending DONE.");
    send_simple_header(sock_fd, RES_EXEC_DONE);
}

// Handle folder creation request.
static void handle_req_createfolder(int sock_fd, Header header) {
    Msg_Folder_Request req; 
    recv(sock_fd, &req, sizeof(req), 0);
    log_event("Got REQ_CREATEFOLDER for '%s' from client '%s'", req.foldername, client_state[sock_fd].username);
    
    int ss_sock = find_available_ss();
    if (ss_sock == -1) {
        log_event("  -> Error: No Storage Servers available.");
        send_simple_header(sock_fd, RES_ERROR);
        return;
    }
    
    log_event("  -> Relaying REQ_SS_CREATEFOLDER to SS on socket %d", ss_sock);
    Header ss_header; 
    ss_header.type = REQ_SS_CREATEFOLDER; 
    ss_header.payload_size = sizeof(Msg_Folder_Request);
    send(ss_sock, &ss_header, sizeof(ss_header), 0);
    send(ss_sock, &req, sizeof(req), 0);
    
    recv(ss_sock, &header, sizeof(Header), 0);
    if (header.type == RES_OK) {
        log_event("  -> Folder '%s' created successfully.", req.foldername);
        send_ok_response(sock_fd);
    } else {
        log_event("  -> SS failed to create folder.");
        send_simple_header(sock_fd, RES_ERROR);
    }
}

// Handle file move request.
static void handle_req_move(int sock_fd, Header header) {
    Msg_Move_Request req; 
    recv(sock_fd, &req, sizeof(req), 0);
    log_event("Got REQ_MOVE for '%s' to folder '%s' from client '%s'", req.filename, req.foldername, client_state[sock_fd].username);
    
    int slot = find_file_slot(req.filename);
    if (slot == -1) {
        log_event("  -> Error: File not found.");
        send_simple_header(sock_fd, RES_ERROR_NOT_FOUND);
        return;
    }
    
    FileMetadata* meta = &file_catalog[slot];
    if (strcmp(meta->owner, client_state[sock_fd].username) != 0) {
        log_event("  -> Access Denied. User '%s' is not owner.", client_state[sock_fd].username);
        send_simple_header(sock_fd, RES_ERROR_ACCESS_DENIED);
        return;
    }
    
    int ss_sock = meta->ss_sock_fd;
    if (!ss_state[ss_sock].active) {
        send_simple_header(sock_fd, RES_ERROR);
        return;
    }
    
    log_event("  -> Relaying REQ_SS_MOVE to SS %d", ss_sock);
    Header ss_header; 
    ss_header.type = REQ_SS_MOVE; 
    ss_header.payload_size = sizeof(Msg_Move_Request);
    send(ss_sock, &ss_header, sizeof(ss_header), 0);
    send(ss_sock, &req, sizeof(req), 0);
    
    recv(ss_sock, &header, sizeof(Header), 0);
    if (header.type == RES_OK) {
        char* base_filename = strrchr(req.filename, '/');
        if (base_filename) {
            base_filename++;
        } else {
            base_filename = req.filename;
        }
        
        char new_path[MAX_FILENAME * 2];
        snprintf(new_path, sizeof(new_path), "%s/%s", req.foldername, base_filename);
        
        remove_from_hashmap(file_catalog[slot].filename);
        invalidate_cache(file_catalog[slot].filename);
        
        strncpy(file_catalog[slot].filename, new_path, MAX_FILENAME);
        add_to_hashmap(new_path, slot);
        
        log_event("  -> File moved successfully. New path: '%s'", new_path);
        send_ok_response(sock_fd);
    } else {
        log_event("  -> SS failed to move file.");
        send_simple_header(sock_fd, RES_ERROR);
    }
}

// Handle folder contents listing request.
static void handle_req_viewfolder(int sock_fd, Header header) {
    Msg_Folder_Request req; 
    recv(sock_fd, &req, sizeof(req), 0);
    log_event("Got REQ_VIEWFOLDER for '%s' from client '%s'", req.foldername, client_state[sock_fd].username);
    
    int folder_exists = 0;
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (ss_state[i].active) {
            send_simple_header(i, REQ_SS_CHECKFOLDER);
            send(i, &req, sizeof(req), 0);
            
            Header check_res;
            recv(i, &check_res, sizeof(check_res), 0);
            if (check_res.type == RES_OK) {
                folder_exists = 1;
                break;
            }
        }
    }
    
    if (!folder_exists) {
        log_event("  -> Folder '%s' does not exist", req.foldername);
        send_simple_header(sock_fd, RES_ERROR);
        return;
    }
    
    int count = 0;
    char folder_prefix[MAX_FILENAME * 2];
    snprintf(folder_prefix, sizeof(folder_prefix), "%s/", req.foldername);
    
    for (int i = 0; i < MAX_FILES_IN_SYSTEM; i++) {
        if (!file_catalog[i].active) continue;
        if (strncmp(file_catalog[i].filename, folder_prefix, strlen(folder_prefix)) == 0) {
            if (has_read_access(&file_catalog[i], client_state[sock_fd].username)) {
                count++;
            }
        }
    }
    
    Header res_hdr; 
    res_hdr.type = RES_VIEW_HDR; 
    res_hdr.payload_size = sizeof(Msg_View_Hdr);
    
    Msg_View_Hdr view_hdr; 
    view_hdr.file_count = count;
    send(sock_fd, &res_hdr, sizeof(res_hdr), 0);
    send(sock_fd, &view_hdr, sizeof(view_hdr), 0);
    
    for (int i = 0; i < MAX_FILES_IN_SYSTEM; i++) {
        if (!file_catalog[i].active) continue;
        if (strncmp(file_catalog[i].filename, folder_prefix, strlen(folder_prefix)) == 0) {
            if (has_read_access(&file_catalog[i], client_state[sock_fd].username)) {
                res_hdr.type = RES_VIEW_ITEM_SHORT;
                res_hdr.payload_size = sizeof(Msg_View_Item_Short);
                Msg_View_Item_Short item;
                strncpy(item.filename, file_catalog[i].filename + strlen(folder_prefix), MAX_FILENAME);
                send(sock_fd, &res_hdr, sizeof(res_hdr), 0);
                send(sock_fd, &item, sizeof(item), 0);
            }
        }
    }
}

// Handle file checkpoint request.
static void handle_req_checkpoint(int sock_fd, Header header) {
    Msg_Checkpoint_Request req; 
    recv(sock_fd, &req, sizeof(req), 0);
    log_event("Got REQ_CHECKPOINT for '%s' tag '%s' from client '%s'", req.filename, req.tag, client_state[sock_fd].username);
    
    int slot = find_file_slot(req.filename);
    if (slot == -1) {
        log_event("  -> Error: File '%s' not found", req.filename);
        send_simple_header(sock_fd, RES_ERROR);
        return;
    }
    
    if (!has_write_access(&file_catalog[slot], client_state[sock_fd].username)) {
        log_event("  -> Error: User '%s' has no write access to '%s'", client_state[sock_fd].username, req.filename);
        send_simple_header(sock_fd, RES_ERROR);
        return;
    }
    
    int ss_sock = file_catalog[slot].ss_sock_fd;
    send_simple_header(ss_sock, REQ_SS_CHECKPOINT);
    send(ss_sock, &req, sizeof(req), 0);
    
    Header res;
    recv(ss_sock, &res, sizeof(res), 0);
    send(sock_fd, &res, sizeof(res), 0);
    
    if (res.type == RES_OK) {
        log_event("  -> Checkpoint '%s' created successfully", req.tag);
    } else {
        log_event("  -> Checkpoint creation failed");
    }
}

// Handle checkpoint viewing request.
static void handle_req_viewcheckpoint(int sock_fd, Header header) {
    Msg_Checkpoint_Request req; 
    recv(sock_fd, &req, sizeof(req), 0);
    log_event("Got REQ_VIEWCHECKPOINT for '%s' tag '%s' from client '%s'", req.filename, req.tag, client_state[sock_fd].username);
    
    int slot = find_file_slot(req.filename);
    if (slot == -1) {
        log_event("  -> Error: File '%s' not found", req.filename);
        send_simple_header(sock_fd, RES_ERROR);
        return;
    }
    
    if (!has_read_access(&file_catalog[slot], client_state[sock_fd].username)) {
        log_event("  -> Error: User '%s' has no read access to '%s'", client_state[sock_fd].username, req.filename);
        send_simple_header(sock_fd, RES_ERROR);
        return;
    }
    
    int ss_sock = file_catalog[slot].ss_sock_fd;
    send_simple_header(ss_sock, REQ_SS_VIEWCHECKPOINT);
    send(ss_sock, &req, sizeof(req), 0);
    
    Header res;
    recv(ss_sock, &res, sizeof(res), 0);
    send(sock_fd, &res, sizeof(res), 0);
    
    if (res.type == RES_SS_FILE_OK) {
        char buffer[4096];
        int remaining = res.payload_size;
        while (remaining > 0) {
            int to_read = (remaining < sizeof(buffer)) ? remaining : sizeof(buffer);
            int bytes = recv(ss_sock, buffer, to_read, 0);
            if (bytes <= 0) break;
            send(sock_fd, buffer, bytes, 0);
            remaining -= bytes;
        }
        log_event("  -> Checkpoint content sent to client");
    } else {
        log_event("  -> Checkpoint view failed");
    }
}

// Handle reverting file to checkpoint.
static void handle_req_revert(int sock_fd, Header header) {
    Msg_Checkpoint_Request req; 
    recv(sock_fd, &req, sizeof(req), 0);
    log_event("Got REQ_REVERT for '%s' to tag '%s' from client '%s'", req.filename, req.tag, client_state[sock_fd].username);
    
    int slot = find_file_slot(req.filename);
    if (slot == -1) {
        log_event("  -> Error: File '%s' not found", req.filename);
        send_simple_header(sock_fd, RES_ERROR);
        return;
    }
    
    if (!has_write_access(&file_catalog[slot], client_state[sock_fd].username)) {
        log_event("  -> Error: User '%s' has no write access to '%s'", client_state[sock_fd].username, req.filename);
        send_simple_header(sock_fd, RES_ERROR);
        return;
    }
    
    int ss_sock = file_catalog[slot].ss_sock_fd;
    send_simple_header(ss_sock, REQ_SS_REVERT);
    send(ss_sock, &req, sizeof(req), 0);
    
    Header res;
    recv(ss_sock, &res, sizeof(res), 0);
    send(sock_fd, &res, sizeof(res), 0);
    
    if (res.type == RES_OK) {
        log_event("  -> File reverted to checkpoint '%s'", req.tag);
    } else {
        log_event("  -> Revert failed");
    }
}

// Handle listing checkpoints of a file.
static void handle_req_listcheckpoints(int sock_fd, Header header) {
    Msg_ListCheckpoints_Request req; 
    recv(sock_fd, &req, sizeof(req), 0);
    log_event("Got REQ_LISTCHECKPOINTS for '%s' from client '%s'", req.filename, client_state[sock_fd].username);
    
    int slot = find_file_slot(req.filename);
    if (slot == -1) {
        log_event("  -> Error: File '%s' not found", req.filename);
        send_simple_header(sock_fd, RES_ERROR);
        return;
    }
    
    if (!has_read_access(&file_catalog[slot], client_state[sock_fd].username)) {
        log_event("  -> Error: User '%s' has no read access to '%s'", client_state[sock_fd].username, req.filename);
        send_simple_header(sock_fd, RES_ERROR);
        return;
    }
    
    int ss_sock = file_catalog[slot].ss_sock_fd;
    send_simple_header(ss_sock, REQ_SS_LISTCHECKPOINTS);
    send(ss_sock, &req, sizeof(req), 0);
    
    Header res;
    recv(ss_sock, &res, sizeof(res), 0);
    send(sock_fd, &res, sizeof(res), 0);
    
    if (res.type == RES_CHECKPOINT_LIST) {
        Msg_Checkpoint_List_Hdr hdr;
        recv(ss_sock, &hdr, sizeof(hdr), 0);
        send(sock_fd, &hdr, sizeof(hdr), 0);
        
        for (int i = 0; i < hdr.checkpoint_count; i++) {
            Msg_Checkpoint_Item item;
            recv(ss_sock, &item, sizeof(item), 0);
            send(sock_fd, &item, sizeof(item), 0);
        }
        log_event("  -> Sent %d checkpoint(s) to client", hdr.checkpoint_count);
    } else {
        log_event("  -> List checkpoints failed");
    }
}

// Handle request access command.
static void handle_req_request_access(int sock_fd, Header header) {
    Msg_Request_Access req; 
    recv(sock_fd, &req, sizeof(req), 0);
    log_event("Got REQ_REQUEST_ACCESS for '%s' by user '%s' from client '%s'", req.filename, req.requesting_user, client_state[sock_fd].username);
    
    int slot = find_file_slot(req.filename);
    if (slot == -1) {
        log_event("  -> Error: File '%s' not found", req.filename);
        send_simple_header(sock_fd, RES_ERROR_NOT_FOUND);
        return;
    }
    
    int req_slot = -1;
    for (int i = 0; i < MAX_ACCESS_REQUESTS; i++) {
        if (!access_requests[i].active) {
            req_slot = i;
            break;
        }
    }
    
    if (req_slot == -1) {
        log_event("  -> Error: Request queue is full");
        send_simple_header(sock_fd, RES_ERROR);
        return;
    }
    
    access_requests[req_slot].active = 1;
    access_requests[req_slot].request_id = next_request_id++;
    strncpy(access_requests[req_slot].filename, req.filename, MAX_FILENAME);
    strncpy(access_requests[req_slot].requesting_user, req.requesting_user, MAX_USERNAME);
    access_requests[req_slot].requested_perm = req.requested_perm;
    access_requests[req_slot].timestamp = time(NULL);
    
    log_event("  -> Request stored with ID %d", access_requests[req_slot].request_id);
    send_ok_response(sock_fd);
}

// Handle checking pending access requests.
static void handle_req_check_requests(int sock_fd, Header header) {
    log_event("Got REQ_CHECK_REQUESTS from client '%s'", client_state[sock_fd].username);
    
    int count = 0;
    for (int i = 0; i < MAX_ACCESS_REQUESTS; i++) {
        if (!access_requests[i].active) continue;
        
        int slot = find_file_slot(access_requests[i].filename);
        if (slot != -1 && strcmp(file_catalog[slot].owner, client_state[sock_fd].username) == 0) {
            count++;
        }
    }
    
    log_event("  -> Found %d pending request(s)", count);
    
    Header res_hdr; 
    res_hdr.type = RES_REQUEST_LIST; 
    res_hdr.payload_size = sizeof(Msg_Request_List_Hdr);
    send(sock_fd, &res_hdr, sizeof(res_hdr), 0);
    
    Msg_Request_List_Hdr hdr;
    hdr.request_count = count;
    send(sock_fd, &hdr, sizeof(hdr), 0);
    
    for (int i = 0; i < MAX_ACCESS_REQUESTS; i++) {
        if (!access_requests[i].active) continue;
        
        int slot = find_file_slot(access_requests[i].filename);
        if (slot != -1 && strcmp(file_catalog[slot].owner, client_state[sock_fd].username) == 0) {
            Msg_Request_Item item;
            item.request_id = access_requests[i].request_id;
            strncpy(item.filename, access_requests[i].filename, MAX_FILENAME);
            strncpy(item.requesting_user, access_requests[i].requesting_user, MAX_USERNAME);
            item.requested_perm = access_requests[i].requested_perm;
            item.timestamp = access_requests[i].timestamp;
            
            send(sock_fd, &item, sizeof(item), 0);
        }
    }
}

// Handle approving access request.
static void handle_req_approve_request(int sock_fd, Header header) {
    Msg_Request_Response resp; 
    recv(sock_fd, &resp, sizeof(resp), 0);
    log_event("Got REQ_APPROVE_REQUEST for request ID %d from client '%s'", resp.request_id, client_state[sock_fd].username);
    
    int req_slot = -1;
    for (int i = 0; i < MAX_ACCESS_REQUESTS; i++) {
        if (access_requests[i].active && access_requests[i].request_id == resp.request_id) {
            req_slot = i;
            break;
        }
    }
    
    if (req_slot == -1) {
        log_event("  -> Error: Request not found");
        send_simple_header(sock_fd, RES_ERROR);
        return;
    }
    
    int slot = find_file_slot(access_requests[req_slot].filename);
    if (slot == -1 || strcmp(file_catalog[slot].owner, client_state[sock_fd].username) != 0) {
        log_event("  -> Error: User is not the owner");
        send_simple_header(sock_fd, RES_ERROR_ACCESS_DENIED);
        return;
    }
    
    FileMetadata* meta = &file_catalog[slot];
    int existing_idx = -1;
    for (int i = 0; i < meta->access_count; i++) {
        if (strcmp(meta->access_list[i].username, access_requests[req_slot].requesting_user) == 0) {
            existing_idx = i;
            break;
        }
    }
    
    if (existing_idx != -1) {
        meta->access_list[existing_idx].permission = access_requests[req_slot].requested_perm;
        log_event("  -> Updated existing permission for user '%s'", access_requests[req_slot].requesting_user);
    } else {
        if (meta->access_count >= MAX_PERMISSIONS_PER_FILE) {
            log_event("  -> Error: Access list is full");
            send_simple_header(sock_fd, RES_ERROR);
            return;
        }
        
        AccessEntry* new_entry = &meta->access_list[meta->access_count];
        strncpy(new_entry->username, access_requests[req_slot].requesting_user, MAX_USERNAME);
        new_entry->permission = access_requests[req_slot].requested_perm;
        meta->access_count++;
        log_event("  -> Added access for user '%s'", access_requests[req_slot].requesting_user);
    }
    
    int ss_sock = meta->ss_sock_fd;
    if (ss_state[ss_sock].active) {
        Msg_Access_Request ss_req;
        strncpy(ss_req.filename, access_requests[req_slot].filename, MAX_FILENAME);
        strncpy(ss_req.username, access_requests[req_slot].requesting_user, MAX_USERNAME);
        ss_req.perm = access_requests[req_slot].requested_perm;
        
        Header ss_header; 
        ss_header.type = REQ_SS_ADD_ACCESS; 
        ss_header.payload_size = sizeof(Msg_Access_Request);
        send(ss_sock, &ss_header, sizeof(ss_header), 0);
        send(ss_sock, &ss_req, sizeof(ss_req), 0);
    }
    
    access_requests[req_slot].active = 0;
    send_ok_response(sock_fd);
}

// Handle denying access request.
static void handle_req_deny_request(int sock_fd, Header header) {
    Msg_Request_Response resp; 
    recv(sock_fd, &resp, sizeof(resp), 0);
    log_event("Got REQ_DENY_REQUEST for request ID %d from client '%s'", resp.request_id, client_state[sock_fd].username);
    
    int req_slot = -1;
    for (int i = 0; i < MAX_ACCESS_REQUESTS; i++) {
        if (access_requests[i].active && access_requests[i].request_id == resp.request_id) {
            req_slot = i;
            break;
        }
    }
    
    if (req_slot == -1) {
        log_event("  -> Error: Request not found");
        send_simple_header(sock_fd, RES_ERROR);
        return;
    }
    
    int slot = find_file_slot(access_requests[req_slot].filename);
    if (slot == -1 || strcmp(file_catalog[slot].owner, client_state[sock_fd].username) != 0) {
        log_event("  -> Error: User is not the owner");
        send_simple_header(sock_fd, RES_ERROR_ACCESS_DENIED);
        return;
    }
    
    access_requests[req_slot].active = 0;
    log_event("  -> Request denied and removed");
    send_ok_response(sock_fd);
}

// Handle storage server heartbeat response.
static void handle_res_ss_heartbeat(int sock_fd, Header header) {
    if (ss_state[sock_fd].active) {
        ss_state[sock_fd].last_heartbeat = time(NULL);
        ss_state[sock_fd].missed_heartbeats = 0;
        log_event("  -> Heartbeat received from SS %d (%s)", sock_fd, ss_state[sock_fd].ss_id);
    }
}

// Core request router for the Naming Server.
void handle_nm_message(int sock_fd, Header header, char* peer_ip, fd_set* master_set) {
    switch (header.type) {
        case REQ_CLIENT_REGISTER:
            handle_client_register(sock_fd, peer_ip);
            break;
            
        case REQ_SS_REGISTER:
            handle_ss_register(sock_fd, peer_ip);
            break;
            
        case REQ_CREATE:
            handle_req_create(sock_fd, header);
            break;
            
        case REQ_READ:
        case REQ_WRITE:
        case REQ_STREAM:
            handle_req_read_write_stream(sock_fd, header);
            break;
            
        case REQ_LIST:
            handle_req_list(sock_fd, header);
            break;
            
        case REQ_DELETE:
            handle_req_delete(sock_fd, header);
            break;
            
        case REQ_UNDO:
            handle_req_undo(sock_fd, header);
            break;
            
        case REQ_UPDATE_METADATA:
            handle_req_update_metadata(sock_fd, header);
            break;
            
        case REQ_VIEW:
            handle_req_view(sock_fd, header);
            break;
            
        case REQ_INFO:
            handle_req_info(sock_fd, header);
            break;
            
        case REQ_ADD_ACCESS:
            handle_req_add_access(sock_fd, header);
            break;
            
        case REQ_REM_ACCESS:
            handle_req_rem_access(sock_fd, header);
            break;
            
        case REQ_EXEC:
            handle_req_exec(sock_fd, header);
            break;
            
        case REQ_CREATEFOLDER:
            handle_req_createfolder(sock_fd, header);
            break;
            
        case REQ_MOVE:
            handle_req_move(sock_fd, header);
            break;
            
        case REQ_VIEWFOLDER:
            handle_req_viewfolder(sock_fd, header);
            break;
            
        case REQ_CHECKPOINT:
            handle_req_checkpoint(sock_fd, header);
            break;
            
        case REQ_VIEWCHECKPOINT:
            handle_req_viewcheckpoint(sock_fd, header);
            break;
            
        case REQ_REVERT:
            handle_req_revert(sock_fd, header);
            break;
            
        case REQ_LISTCHECKPOINTS:
            handle_req_listcheckpoints(sock_fd, header);
            break;
            
        case REQ_REQUEST_ACCESS:
            handle_req_request_access(sock_fd, header);
            break;
            
        case REQ_CHECK_REQUESTS:
            handle_req_check_requests(sock_fd, header);
            break;
            
        case REQ_APPROVE_REQUEST:
            handle_req_approve_request(sock_fd, header);
            break;
            
        case REQ_DENY_REQUEST:
            handle_req_deny_request(sock_fd, header);
            break;
            
        case RES_SS_HEARTBEAT:
            handle_res_ss_heartbeat(sock_fd, header);
            break;
            
        default:
            log_event("Socket %d: Unknown message type %d", sock_fd, header.type);
            break;
    }
}
