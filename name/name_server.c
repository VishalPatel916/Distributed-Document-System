// Core execution and event loop for the central Naming Server (NM).

#include "protocol.h"
#include <sys/select.h>
#include <netinet/in.h>
#include <time.h>
#include <stdarg.h>

#include "name_globals.h"
#include "hash_cache.h"
#include "fault_tolerance.h"
#include "catalog.h"
#include "handlers.h"

FILE* nm_log_file;

ClientInfo client_state[MAX_CONNECTIONS];
SSInfo ss_state[MAX_CONNECTIONS];
FileMetadata file_catalog[MAX_FILES_IN_SYSTEM];

AccessRequest access_requests[MAX_ACCESS_REQUESTS];
int next_request_id = 1;

// Log server events to stdout and nm.log.
void log_event(const char* format, ...) {
    char time_buf[50]; 
    time_t now = time(NULL); 
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    
    va_list args;
    
    printf("[%s] ", time_buf); 
    va_start(args, format); 
    vprintf(format, args); 
    va_end(args); 
    printf("\n");
    
    fprintf(nm_log_file, "[%s] ", time_buf); 
    va_start(args, format); 
    vfprintf(nm_log_file, format, args); 
    va_end(args); 
    fprintf(nm_log_file, "\n");
    
    fflush(nm_log_file);
}

// Handle client registration request.
void handle_client_register(int sock_fd, char* peer_ip) {
    Msg_Client_Register msg; 
    recv(sock_fd, &msg, sizeof(msg), 0);
    log_event("Socket %d (%s): CLIENT registered as '%s'", sock_fd, peer_ip, msg.username);
    
    client_state[sock_fd].active = 1; 
    strncpy(client_state[sock_fd].username, msg.username, MAX_USERNAME);
    strncpy(client_state[sock_fd].ip_addr, peer_ip, MAX_IP_LEN);
    ss_state[sock_fd].active = 0;
    
    send_ok_response(sock_fd);
}

// Check if a storage server is recovering after failure.
static int check_and_handle_recovery(int sock_fd, int file_count) {
    if (file_count == 0) {
        int has_backups = 0;
        for (int i = 0; i < MAX_FILES_IN_SYSTEM; i++) {
            if (!file_catalog[i].active && file_catalog[i].ss_sock_fd == sock_fd && 
                file_catalog[i].backup_ss_sock >= 0) {
                has_backups = 1;
                break;
            }
        }
        if (has_backups) {
            log_event("  -> Detected recovery! Syncing files from backups...");
            sync_files_to_recovering_ss(sock_fd);
            send_ok_response(sock_fd);
            return 1;
        }
    }
    return 0;
}

// Catalog the list of files hosted on a storage server.
static void catalog_ss_files(int sock_fd, int file_count) {
    log_event("Receiving file list from SS %d...", sock_fd);
    for (int i = 0; i < file_count; i++) {
        Msg_File_Item item; 
        recv(sock_fd, &item, sizeof(item), 0);
        
        int slot = find_empty_file_slot();
        if (slot != -1) {
            file_catalog[slot].active = 1; 
            strncpy(file_catalog[slot].filename, item.filename, MAX_FILENAME);
            file_catalog[slot].ss_sock_fd = sock_fd; 
            strncpy(file_catalog[slot].owner, item.owner, MAX_USERNAME);
            file_catalog[slot].access_count = item.access_count;
            for (int j = 0; j < item.access_count; j++) {
                file_catalog[slot].access_list[j] = item.access_list[j];
            }
            
            if (count_active_ss() >= 2) {
                int backup = find_backup_ss(sock_fd);
                if (backup >= 0) {
                    file_catalog[slot].backup_ss_sock = backup;
                    replicate_to_backup(backup, &file_catalog[slot]);
                } else {
                    file_catalog[slot].backup_ss_sock = -1;
                }
            } else {
                file_catalog[slot].backup_ss_sock = -1;
            }
            
            add_to_hashmap(item.filename, slot);
            log_event("  -> Cataloged '%s' (owner: %s, %d access entries) in slot %d", 
                item.filename, item.owner, item.access_count, slot);
        }
    }
}

// Handle storage server registration request.
void handle_ss_register(int sock_fd, char* peer_ip) {
    Msg_SS_Register msg; 
    recv(sock_fd, &msg, sizeof(msg), 0);
    log_event("Socket %d (%s): SS registered at %s:%d. Expecting %d files.", sock_fd, peer_ip, msg.ss_ip, msg.client_port, msg.file_count);
    
    ss_state[sock_fd].active = 1; 
    strncpy(ss_state[sock_fd].ip, msg.ss_ip, MAX_IP_LEN);
    ss_state[sock_fd].client_port = msg.client_port; 
    strncpy(ss_state[sock_fd].ip_addr, peer_ip, MAX_IP_LEN);
    
    snprintf(ss_state[sock_fd].ss_id, MAX_USERNAME, "%s:%d", msg.ss_ip, msg.client_port);
    ss_state[sock_fd].last_heartbeat = time(NULL);
    ss_state[sock_fd].missed_heartbeats = 0;
    client_state[sock_fd].active = 0;
    
    if (check_and_handle_recovery(sock_fd, msg.file_count)) {
        return;
    }
    
    catalog_ss_files(sock_fd, msg.file_count);
    send_ok_response(sock_fd);
}

// Send heartbeats to all active storage servers to detect failures.
static void process_ss_heartbeats() {
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (ss_state[i].active) {
            Header hb_req;
            hb_req.type = REQ_SS_HEARTBEAT;
            hb_req.payload_size = 0;
            
            if (send(i, &hb_req, sizeof(hb_req), MSG_DONTWAIT) < 0) {
                ss_state[i].missed_heartbeats++;
                log_event("  -> Heartbeat send failed to SS %d (missed: %d)", 
                         i, ss_state[i].missed_heartbeats);
            }
            
            if (ss_state[i].missed_heartbeats >= 3) {
                log_event("  -> SS %d FAILED (3 missed heartbeats). Marking offline.", i);
                ss_state[i].active = 0;
                
                for (int j = 0; j < MAX_FILES_IN_SYSTEM; j++) {
                    if (file_catalog[j].active && file_catalog[j].ss_sock_fd == i) {
                        file_catalog[j].active = 0;
                        log_event("  -> File '%s' marked inactive (SS failure)", file_catalog[j].filename);
                    }
                }
            }
        }
    }
}

// Initialize central Naming Server state.
static void init_server_state() {
    for (int i = 0; i < MAX_CONNECTIONS; i++) { 
        client_state[i].active = 0; 
        ss_state[i].active = 0; 
    }
    for (int i = 0; i < MAX_FILES_IN_SYSTEM; i++) { 
        file_catalog[i].active = 0; 
        file_catalog[i].access_count = 0; 
    }
    
    for (int i = 0; i < HASH_SIZE; i++) {
        hash_table[i] = NULL;
    }
    for (int i = 0; i < CACHE_SIZE; i++) {
        lru_cache[i].valid = 0;
    }
    log_event("Hash map and LRU cache initialized.");
}

// Main function for the Naming Server.
int main() {
    int listener_sock, new_sock, sock_fd;
    int fdmax; 
    struct sockaddr_in nm_addr, client_addr; 
    socklen_t client_len;
    fd_set master_set, read_set;

    nm_log_file = fopen("nm.log", "a"); 
    if (nm_log_file == NULL) error_exit("fopen nm.log");
    
    log_event("--- Name Server Started ---");
    log_event("Initializing state...");
    init_server_state();

    listener_sock = socket(AF_INET, SOCK_STREAM, 0); 
    if (listener_sock < 0) error_exit("socket");
    
    int yes = 1; 
    if (setsockopt(listener_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) < 0) error_exit("setsockopt");
    
    memset(&nm_addr, 0, sizeof(nm_addr)); 
    nm_addr.sin_family = AF_INET; 
    nm_addr.sin_addr.s_addr = INADDR_ANY; 
    nm_addr.sin_port = htons(NM_PORT);
    
    if (bind(listener_sock, (struct sockaddr *)&nm_addr, sizeof(nm_addr)) < 0) error_exit("bind");
    if (listen(listener_sock, 10) < 0) error_exit("listen");

    FD_ZERO(&master_set); 
    FD_SET(listener_sock, &master_set);
    fdmax = listener_sock;
    log_event("Name Server listening on port %d...", NM_PORT);

    while (1) {
        read_set = master_set;
        
        struct timeval timeout;
        timeout.tv_sec = 10;
        timeout.tv_usec = 0;
        
        int activity = select(fdmax + 1, &read_set, NULL, NULL, &timeout);
        if (activity < 0) { 
            log_event("select() error"); 
            error_exit("select"); 
        }
        
        if (activity == 0) {
            process_ss_heartbeats();
            continue;
        }
        
        for (sock_fd = 0; sock_fd <= fdmax; sock_fd++) {
            if (FD_ISSET(sock_fd, &read_set)) {
                if (sock_fd == listener_sock) {
                    client_len = sizeof(client_addr);
                    new_sock = accept(listener_sock, (struct sockaddr *)&client_addr, &client_len);
                    if (new_sock < 0) { 
                        perror("accept"); 
                    } else {
                        FD_SET(new_sock, &master_set); 
                        if (new_sock > fdmax) fdmax = new_sock;
                        log_event("New connection from %s on socket %d", inet_ntoa(client_addr.sin_addr), new_sock);
                    }
                } 
                else {
                    Header header; 
                    int nbytes = recv(sock_fd, &header, sizeof(Header), 0);
                    
                    if (nbytes <= 0) { 
                        handle_disconnect(sock_fd); 
                        FD_CLR(sock_fd, &master_set); 
                    } 
                    else {
                        struct sockaddr_in addr; 
                        socklen_t len = sizeof(addr);
                        getpeername(sock_fd, (struct sockaddr*)&addr, &len); 
                        char* peer_ip = inet_ntoa(addr.sin_addr);
                        handle_nm_message(sock_fd, header, peer_ip, &master_set);
                    }
                }
            }
        }
    }
    return 0;
}