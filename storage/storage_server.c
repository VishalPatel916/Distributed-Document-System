// Entry point and central loop for the Storage Server (SS).

#include "protocol.h"
#include "storage_globals.h"
#include "document.h"
#include "locks.h"
#include "metadata.h"
#include "handlers.h"
#include <netinet/in.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/select.h>
#include <fcntl.h>
#include <malloc.h> 
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <stdarg.h>

#define MY_IP_FOR_CLIENTS "127.0.0.1"
#define MY_PORT_FOR_CLIENTS 8082
#define MY_STORAGE_PATH "./ss_storage"

#define MAX_CONNECTIONS FD_SETSIZE
#define MAX_LOCKS 100

char g_storage_path[512] = MY_STORAGE_PATH;
char g_metadata_path[768];
LockInfo global_locks[MAX_LOCKS];
ActiveDoc active_documents[MAX_FILES_IN_SYSTEM];
WriteSession write_sessions[MAX_CONNECTIONS];

FILE* ss_log_file;

// Log storage server events to stdout and ss.log.
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
    
    fprintf(ss_log_file, "[%s] ", time_buf); 
    va_start(args, format); 
    vfprintf(ss_log_file, format, args); 
    va_end(args); 
    fprintf(ss_log_file, "\n");
    
    fflush(ss_log_file);
}

// Free only a single sentence node and its words.
void free_sentence_node(SentenceNode* sent) {
    if (!sent) return;
    WordNode* current_word = sent->word_head;
    while (current_word != NULL) {
        WordNode* next_word = current_word->next;
        free(current_word->word); 
        free(current_word);
        current_word = next_word;
    }
    free(sent);
}

// Find an empty active document slot.
int find_empty_active_doc_slot() {
    for (int i = 0; i < MAX_FILES_IN_SYSTEM; i++) { 
        if (!active_documents[i].active) return i; 
    }
    return -1;
}

// Find an active document by name.
int find_active_doc(char* filename) {
    for (int i = 0; i < MAX_FILES_IN_SYSTEM; i++) {
        if (active_documents[i].active && strcmp(active_documents[i].filename, filename) == 0) {
            return i;
        }
    }
    return -1;
}

// Connect and register with Naming Server.
static int register_with_naming_server(char* nm_ip, char* my_client_ip, int my_port, int file_count, char my_files[][MAX_FILENAME]) {
    int nm_sock = socket(AF_INET, SOCK_STREAM, 0); 
    if (nm_sock < 0) error_exit("socket");
    
    struct sockaddr_in nm_addr;
    memset(&nm_addr, 0, sizeof(nm_addr)); 
    nm_addr.sin_family = AF_INET; 
    nm_addr.sin_port = htons(NM_PORT);
    
    if (inet_pton(AF_INET, nm_ip, &nm_addr.sin_addr) <= 0) error_exit("inet_pton");
    if (connect(nm_sock, (struct sockaddr *)&nm_addr, sizeof(nm_addr)) < 0) error_exit("connect");
    log_event("Storage Server connected to Name Server on port %d", NM_PORT);
    
    Header header; 
    header.type = REQ_SS_REGISTER; 
    header.payload_size = sizeof(Msg_SS_Register);
    
    Msg_SS_Register reg_msg; 
    strncpy(reg_msg.ss_ip, my_client_ip, MAX_IP_LEN); 
    reg_msg.client_port = my_port; 
    reg_msg.file_count = file_count;
    
    if (send(nm_sock, &header, sizeof(Header), 0) < 0) error_exit("send header");
    if (send(nm_sock, &reg_msg, sizeof(reg_msg), 0) < 0) error_exit("send payload");
    
    log_event("Sending file list with metadata...");
    for (int i = 0; i < file_count; i++) {
        Msg_File_Item item; 
        strncpy(item.filename, my_files[i], MAX_FILENAME);
        load_file_metadata(my_files[i], item.owner, &item.access_count, item.access_list);
        log_event("  -> Sending '%s' (owner: %s, access_count: %d)", item.filename, item.owner, item.access_count);
        if (send(nm_sock, &item, sizeof(item), 0) < 0) error_exit("send file item"); 
    }
    
    if (recv(nm_sock, &header, sizeof(Header), 0) < 0 || header.type != RES_OK) error_exit("Registration failed");
    log_event("Registration successful!");
    
    log_event("Sending metadata for %d existing files...", file_count);
    for (int i = 0; i < file_count; i++) {
        char file_path[768];
        sprintf(file_path, "%s/%s", g_storage_path, my_files[i]);
        calculate_and_send_metadata(nm_sock, my_files[i], file_path);
    }
    return nm_sock;
}

// Setup TCP listener for client connections.
static int setup_client_listener(int my_port) {
    int client_listener_sock = socket(AF_INET, SOCK_STREAM, 0); 
    if (client_listener_sock < 0) error_exit("client listener socket");
    
    int yes = 1; 
    if (setsockopt(client_listener_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) < 0) error_exit("setsockopt");
    
    struct sockaddr_in client_listen_addr;
    memset(&client_listen_addr, 0, sizeof(client_listen_addr)); 
    client_listen_addr.sin_family = AF_INET; 
    client_listen_addr.sin_addr.s_addr = INADDR_ANY; 
    client_listen_addr.sin_port = htons(my_port);
    
    if (bind(client_listener_sock, (struct sockaddr *)&client_listen_addr, sizeof(client_listen_addr)) < 0) error_exit("client listener bind");
    if (listen(client_listener_sock, 10) < 0) error_exit("client listener listen");
    
    log_event("Storage Server now listening for clients on port %d...", my_port);
    return client_listener_sock;
}

// Main execution entry point for the Storage Server.
int main(int argc, char* argv[]) {
    int my_port = MY_PORT_FOR_CLIENTS;
    char my_client_ip[MAX_IP_LEN] = MY_IP_FOR_CLIENTS;
    char nm_ip[MAX_IP_LEN] = "127.0.0.1";
    
    if (argc >= 2) my_port = atoi(argv[1]);
    if (argc >= 3) strncpy(g_storage_path, argv[2], sizeof(g_storage_path) - 1);
    if (argc >= 4) strncpy(my_client_ip, argv[3], sizeof(my_client_ip) - 1);
    if (argc >= 5) strncpy(nm_ip, argv[4], sizeof(nm_ip) - 1);
    
    ss_log_file = fopen("ss.log", "a"); 
    if (ss_log_file == NULL) error_exit("fopen ss.log");
    
    log_event("--- Storage Server Started ---");
    log_event("Port: %d, Storage: %s, Client IP: %s, NM IP: %s", my_port, g_storage_path, my_client_ip, nm_ip);
    
    int nm_sock, client_listener_sock, new_client_sock;
    struct sockaddr_in client_addr;
    socklen_t client_len; 
    fd_set master_set, read_set; 
    int fdmax;
    
    init_locks(); 
    for (int i = 0; i < MAX_CONNECTIONS; i++) { 
        write_sessions[i].active = 0; 
        write_sessions[i].virtual_word_count = 0; 
    }
    for (int i = 0; i < MAX_FILES_IN_SYSTEM; i++) {
        active_documents[i].active = 0; 
    }
    
    char my_files[MAX_FILES_PER_SS][MAX_FILENAME]; 
    int file_count = 0; 
    mkdir(g_storage_path, 0777);
    
    log_event("Scanning storage directory: %s", g_storage_path);
    scan_directory_recursive(g_storage_path, "", my_files, &file_count, MAX_FILES_PER_SS);
    log_event("Found %d files.", file_count);
    
    nm_sock = register_with_naming_server(nm_ip, my_client_ip, my_port, file_count, my_files);
    client_listener_sock = setup_client_listener(my_port);
    
    FD_ZERO(&master_set); 
    FD_SET(nm_sock, &master_set); 
    FD_SET(client_listener_sock, &master_set);
    fdmax = (nm_sock > client_listener_sock) ? nm_sock : client_listener_sock;

    while (1) {
        read_set = master_set;
        if (select(fdmax + 1, &read_set, NULL, NULL, NULL) < 0) { 
            log_event("select() error"); 
            error_exit("select"); 
        }
        
        for (int sock_fd = 0; sock_fd <= fdmax; sock_fd++) {
            if (FD_ISSET(sock_fd, &read_set)) {
                if (sock_fd == nm_sock) {
                    Header header;
                    if (recv(nm_sock, &header, sizeof(Header), 0) <= 0) { 
                        log_event("Name Server disconnected!"); 
                        error_exit("Name Server disconnected"); 
                    }
                    handle_nm_message_ss(sock_fd, header, nm_sock, &master_set);
                }
                else if (sock_fd == client_listener_sock) {
                    client_len = sizeof(client_addr);
                    new_client_sock = accept(client_listener_sock, (struct sockaddr *)&client_addr, &client_len);
                    if (new_client_sock < 0) { 
                        perror("accept new client"); 
                    } else {
                        FD_SET(new_client_sock, &master_set);
                        if (new_client_sock > fdmax) fdmax = new_client_sock;
                        log_event("New client connection from %s on socket %d", inet_ntoa(client_addr.sin_addr), new_client_sock);
                    }
                }
                else {
                    Header header;
                    if (recv(sock_fd, &header, sizeof(Header), 0) <= 0) {
                        handle_client_disconnect(sock_fd, &master_set);
                    } else {
                        handle_client_message_ss(sock_fd, header, nm_sock, &master_set);
                    }
                }
            }
        }
    } 
    return 0;
}