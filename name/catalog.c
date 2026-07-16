#include "catalog.h"
#include "hash_cache.h"
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

// O(1) Find Function using Cache + Hash Map
int find_file_slot(char* filename) {
    // 1. Check Cache (Fastest)
    for(int i=0; i<CACHE_SIZE; i++) {
        if(lru_cache[i].valid && strcmp(lru_cache[i].key, filename) == 0) {
            log_event("[CACHE HIT] '%s' found in cache at position %d", filename, i);
            return lru_cache[i].slot_index;
        }
    }

    // 2. Check Hash Map (Fast)
    unsigned long idx = hash_func(filename);
    HashNode* node = hash_table[idx];
    while(node != NULL) {
        if(strcmp(node->key, filename) == 0) {
            // Found! Update cache and return
            log_event("[HASH HIT] '%s' found in hash map, adding to cache", filename);
            add_to_cache(filename, node->slot_index);
            return node->slot_index;
        }
        node = node->next;
    }

    log_event("[MISS] '%s' not found in cache or hash map", filename);
    return -1; // Not found
}

int find_empty_file_slot() { for (int i = 0; i < MAX_FILES_IN_SYSTEM; i++) { if (file_catalog[i].active == 0) return i; } return -1; }
int find_available_ss() { for (int i = 0; i < MAX_CONNECTIONS; i++) { if (ss_state[i].active) return i; } return -1; }
void send_ok_response(int sock) { Header header; header.type = RES_OK; header.payload_size = 0; if (send(sock, &header, sizeof(Header), 0) < 0) { log_event("Failed to send OK response to socket %d", sock); } }

void handle_disconnect(int sock_fd) {
    if (client_state[sock_fd].active) {
        log_event("Client '%s' (Socket %d, IP: %s) disconnected.", client_state[sock_fd].username, sock_fd, client_state[sock_fd].ip_addr);
        client_state[sock_fd].active = 0;
    } else if (ss_state[sock_fd].active) {
        log_event("Storage Server (Socket %d, IP: %s) disconnected.", sock_fd, ss_state[sock_fd].ip_addr);
        ss_state[sock_fd].active = 0;
        log_event("De-listing its files from the catalog...");
        for (int i = 0; i < MAX_FILES_IN_SYSTEM; i++) {
            if (file_catalog[i].active && file_catalog[i].ss_sock_fd == sock_fd) {
                // REMOVE FROM HASHMAP & CACHE
                remove_from_hashmap(file_catalog[i].filename);
                invalidate_cache(file_catalog[i].filename);
                
                file_catalog[i].active = 0;
                log_event("  -> De-listed '%s'", file_catalog[i].filename);
            }
        }
    } else { log_event("Socket %d (unregistered) disconnected.", sock_fd); }
    close(sock_fd);
}

PermissionLevel get_permission(FileMetadata* meta, char* username) {
    if (strcmp(meta->owner, username) == 0) { return READ_WRITE; }
    for (int i = 0; i < meta->access_count; i++) {
        if (strcmp(meta->access_list[i].username, username) == 0) {
            return meta->access_list[i].permission;
        }
    }
    return NO_PERM;
}

int has_read_access(FileMetadata* meta, char* username) { return get_permission(meta, username) >= READ_ONLY; }
int has_write_access(FileMetadata* meta, char* username) { return get_permission(meta, username) == READ_WRITE; }

void send_full_metadata(int sock_fd, FileMetadata* meta, MessageType res_type) {
    Header hdr; hdr.payload_size = sizeof(Msg_Full_Metadata); hdr.type = res_type;
    Msg_Full_Metadata msg;
    strncpy(msg.filename, meta->filename, MAX_FILENAME);
    strncpy(msg.owner, meta->owner, MAX_USERNAME);
    msg.file_size = meta->file_size; msg.word_count = meta->word_count; msg.char_count = meta->char_count;
    msg.last_modified = meta->last_modified; msg.last_accessed = meta->last_accessed; msg.access_count = meta->access_count;
    send(sock_fd, &hdr, sizeof(hdr), 0); send(sock_fd, &msg, sizeof(msg), 0);
    if (msg.access_count > 0) { send(sock_fd, meta->access_list, sizeof(AccessEntry) * msg.access_count, 0); }
}

// --- NEW HELPER: NM acts as a client to get file content from SS ---
char* get_file_content_from_ss(SSInfo* ss, char* filename) {
    int ss_sock;
    struct sockaddr_in ss_addr;
    
    // 1. Connect to the SS's CLIENT port
    ss_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (ss_sock < 0) { log_event("  -> EXEC: socket() failed"); return NULL; }

    memset(&ss_addr, 0, sizeof(ss_addr));
    ss_addr.sin_family = AF_INET;
    ss_addr.sin_port = htons(ss->client_port);
    if (inet_pton(AF_INET, ss->ip, &ss_addr.sin_addr) <= 0) {
        log_event("  -> EXEC: inet_pton() failed"); close(ss_sock); return NULL;
    }
    if (connect(ss_sock, (struct sockaddr *)&ss_addr, sizeof(ss_addr)) < 0) {
        log_event("  -> EXEC: connect() to SS failed"); close(ss_sock); return NULL;
    }

    // 2. Send a normal REQ_CLIENT_READ
    Header req_hdr;
    req_hdr.type = REQ_CLIENT_READ;
    req_hdr.payload_size = sizeof(Msg_Filename_Request);
    Msg_Filename_Request req_msg;
    strncpy(req_msg.filename, filename, MAX_FILENAME);
    send(ss_sock, &req_hdr, sizeof(req_hdr), 0);
    send(ss_sock, &req_msg, sizeof(req_msg), 0);
    
    // 3. Wait for handshake
    Header res_hdr;
    if (recv(ss_sock, &res_hdr, sizeof(res_hdr), 0) <= 0 || res_hdr.type != RES_SS_FILE_OK) {
        log_event("  -> EXEC: SS did not send RES_SS_FILE_OK");
        close(ss_sock); return NULL;
    }

    // 4. Malloc a buffer and receive the file content
    int max_script_size = 16384;
    char* script_content = (char*)malloc(max_script_size);
    if (script_content == NULL) { close(ss_sock); return NULL; }
    
    int total_bytes = 0;
    int nbytes = 0;
    while(total_bytes < max_script_size - 1 &&
          (nbytes = recv(ss_sock, script_content + total_bytes, max_script_size - 1 - total_bytes, 0)) > 0) {
        total_bytes += nbytes;
    }
    script_content[total_bytes] = '\0'; // Null-terminate the script
    
    close(ss_sock);
    log_event("  -> EXEC: Successfully fetched script content (%d bytes).", total_bytes);
    return script_content;
}
