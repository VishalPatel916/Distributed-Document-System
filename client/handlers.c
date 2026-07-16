// Implementation of client-side command handlers and network operations.

#include "handlers.h"
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>

// Send a request with just a filename payload.
void send_filename_request(int sock, MessageType type, char* filename) {
    Header header;
    header.type = type;
    header.payload_size = sizeof(Msg_Filename_Request);
    
    Msg_Filename_Request req; 
    strncpy(req.filename, filename, MAX_FILENAME);
    
    if (send(sock, &header, sizeof(header), 0) < 0) error_exit("send header");
    if (send(sock, &req, sizeof(req), 0) < 0) error_exit("send payload");
}

// Print metadata for VIEW -l command.
void print_metadata_view(Msg_Full_Metadata* meta) {
    char time_buf[50];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", localtime(&meta->last_accessed));
    printf("| %-20s | %-12s | %-6ld | %-6d | %-6d | %s |\n",
        meta->filename, meta->owner, meta->file_size,
        meta->word_count, meta->char_count, time_buf);
}

// Print header row for metadata view.
void print_metadata_header_view() {
    printf("-------------------------------------------------------------------------------------------\n");
    printf("| Filename             | Owner        | Size   | Words  | Chars  | Last Accessed       |\n");
    printf("-------------------------------------------------------------------------------------------\n");
}

// Print metadata for INFO command.
void print_metadata_info(Msg_Full_Metadata* meta) {
    char modified_buf[50], accessed_buf[50];
    strftime(modified_buf, sizeof(modified_buf), "%Y-%m-%d %H:%M:%S", localtime(&meta->last_modified));
    strftime(accessed_buf, sizeof(accessed_buf), "%Y-%m-%d %H:%M:%S", localtime(&meta->last_accessed));
    printf("| %-20s | %-12s | %-6ld | %-6d | %-6d | %s | %s |\n",
        meta->filename, meta->owner, meta->file_size,
        meta->word_count, meta->char_count, modified_buf, accessed_buf);
}

// Print header row for metadata info.
void print_metadata_header_info() {
    printf("----------------------------------------------------------------------------------------------------------------------\n");
    printf("| Filename             | Owner        | Size   | Words  | Chars  | Last Modified       | Last Accessed       |\n");
    printf("----------------------------------------------------------------------------------------------------------------------\n");
}

// Print metadata table footer.
void print_metadata_footer() {
    printf("----------------------------------------------------------------------------------------------------------------------\n");
}

// Connect directly to a Storage Server.
int connect_to_storage_server(char* ss_ip, int ss_port) {
    int ss_sock; 
    struct sockaddr_in ss_addr;
    
    printf("  -> Connecting directly to Storage Server at %s:%d...\n", ss_ip, ss_port);
    ss_sock = socket(AF_INET, SOCK_STREAM, 0); 
    if (ss_sock < 0) error_exit("ss socket");
    
    memset(&ss_addr, 0, sizeof(ss_addr));
    ss_addr.sin_family = AF_INET; 
    ss_addr.sin_port = htons(ss_port);
    if (inet_pton(AF_INET, ss_ip, &ss_addr.sin_addr) <= 0) error_exit("ss inet_pton");
    
    if (connect(ss_sock, (struct sockaddr *)&ss_addr, sizeof(ss_addr)) < 0) error_exit("ss connect");
    return ss_sock;
}

// Handle READ operation from a Storage Server.
void handle_read_from_ss(char* filename, char* ss_ip, int ss_port) {
    int ss_sock = connect_to_storage_server(ss_ip, ss_port);
    send_filename_request(ss_sock, REQ_CLIENT_READ, filename);
    
    Header ss_header;
    if (recv(ss_sock, &ss_header, sizeof(Header), 0) <= 0) { 
        printf("Error: Storage Server disconnected unexpectedly.\n"); 
        close(ss_sock); 
        return; 
    }
    
    if (ss_header.type == RES_SS_FILE_OK) {
        printf("--- Start of file '%s' ---\n", filename);
        char buffer[FILE_BUFFER_SIZE]; 
        int bytes_recvd;
        while ((bytes_recvd = recv(ss_sock, buffer, FILE_BUFFER_SIZE, 0)) > 0) {
            write(STDOUT_FILENO, buffer, bytes_recvd);
        }
        printf("\n--- End of file '%s' ---\n", filename);
    } else if (ss_header.type == RES_ERROR_NOT_FOUND) { 
        printf("Error: File '%s' not found on the Storage Server.\n", filename); 
    } else { 
        printf("Error: Unknown response %d from Storage Server.\n", ss_header.type); 
    }
    
    close(ss_sock);
}

// Handle STREAM operation from a Storage Server.
void handle_stream_from_ss(char* filename, char* ss_ip, int ss_port) {
    int ss_sock = connect_to_storage_server(ss_ip, ss_port);
    send_filename_request(ss_sock, REQ_CLIENT_STREAM, filename);
    
    Header ss_header;
    if (recv(ss_sock, &ss_header, sizeof(Header), 0) <= 0) { 
        printf("Error: Storage Server disconnected unexpectedly.\n"); 
        close(ss_sock); 
        return; 
    }
    
    if (ss_header.type == RES_SS_FILE_OK) {
        printf("--- Start of stream '%s' ---\n", filename);
        char buffer[FILE_BUFFER_SIZE]; 
        int bytes_recvd;
        while ((bytes_recvd = recv(ss_sock, buffer, FILE_BUFFER_SIZE, 0)) > 0) {
            write(STDOUT_FILENO, buffer, bytes_recvd);
            fflush(stdout); 
        }
        printf("\n--- End of stream '%s' ---\n", filename);
    } else if (ss_header.type == RES_ERROR_NOT_FOUND) { 
        printf("Error: File '%s' not found on the Storage Server.\n", filename); 
    } else { 
        printf("Error: Unknown response %d from Storage Server.\n", ss_header.type); 
    }
    
    close(ss_sock);
}

// Handle interactive WRITE loop with Storage Server.
void handle_write_to_ss(char* filename, int sentence_num, char* ss_ip, int ss_port) {
    int ss_sock = connect_to_storage_server(ss_ip, ss_port);
    
    Header header; 
    header.type = REQ_CLIENT_WRITE; 
    header.payload_size = sizeof(Msg_Client_Write);
    
    Msg_Client_Write req; 
    strncpy(req.filename, filename, MAX_FILENAME); 
    req.sentence_num = sentence_num;
    
    if (send(ss_sock, &header, sizeof(header), 0) < 0) error_exit("send write header");
    if (send(ss_sock, &req, sizeof(req), 0) < 0) error_exit("send write payload");
    
    if (recv(ss_sock, &header, sizeof(Header), 0) <= 0) { 
        printf("Error: SS disconnected while waiting for lock.\n"); 
        close(ss_sock); 
        return; 
    }
    
    if (header.type == RES_OK_LOCKED) {
        printf("Lock acquired for '%s' (sent %d). Enter <word_index> <content>.\n", filename, sentence_num);
        printf("Type 'ETIRW' to save and exit.\n");
        char write_buffer[1024];
        
        while (1) {
            printf("(writing)> "); 
            fflush(stdout);
            if (fgets(write_buffer, sizeof(write_buffer), stdin) == NULL) break;
            
            char* command = strtok(write_buffer, " \n");
            if (command == NULL) continue;
            
            if (strcmp(command, "ETIRW") == 0) {
                send_simple_header(ss_sock, REQ_ETIRW);
                recv(ss_sock, &header, sizeof(header), 0);
                
                if (header.type == RES_OK) {
                    printf("Write successful!\n");
                } else if (header.type == RES_ERROR) {
                    printf("Error: Write failed to commit. Check word indices are valid.\n");
                } else {
                    printf("Error: Write failed to commit (response %d).\n", header.type);
                }
                break; 
            } else {
                char* content = strtok(NULL, "\n");
                if (content == NULL) { 
                    printf("Usage: <word_index> <content>\n"); 
                    continue; 
                }
                
                int word_index = atoi(command);
                header.type = REQ_WRITE_UPDATE; 
                header.payload_size = sizeof(Msg_Write_Update);
                
                Msg_Write_Update update_req; 
                update_req.word_index = word_index; 
                strncpy(update_req.content, content, MAX_WORD_CONTENT);
                
                if (send(ss_sock, &header, sizeof(header), 0) < 0) break;
                if (send(ss_sock, &update_req, sizeof(update_req), 0) < 0) break;
                
                if (recv(ss_sock, &header, sizeof(header), 0) < 0) break;
                
                if (header.type == RES_ERROR) {
                    printf("Error: Word index %d is out of bounds. Please try again.\n", word_index);
                } else if (header.type == RES_ERROR_INVALID_WORD) {
                    printf("Error: Word index out of range.\n");
                } else if (header.type != RES_OK) {
                    printf("Error: Update failed (response %d).\n", header.type);
                } else {
                    printf("Update recorded.\n");
                }
            }
        }
    } else if (header.type == RES_ERROR_LOCKED) { 
        printf("Error: Sentence is locked by another user.\n"); 
    } else if (header.type == RES_ERROR_INVALID_SENTENCE) { 
        printf("Error: Sentence index out of range.\n"); 
    } else { 
        printf("Error: Failed to acquire lock (unknown response %d).\n", header.type); 
    }
    
    close(ss_sock);
}

// Register Client with central Naming Server.
int register_with_name_server(char* nm_ip, char* username) {
    int sock; 
    struct sockaddr_in nm_addr;
    
    sock = socket(AF_INET, SOCK_STREAM, 0); 
    if (sock < 0) error_exit("socket");
    
    memset(&nm_addr, 0, sizeof(nm_addr)); 
    nm_addr.sin_family = AF_INET; 
    nm_addr.sin_port = htons(NM_PORT);
    
    if (inet_pton(AF_INET, nm_ip, &nm_addr.sin_addr) <= 0) error_exit("inet_pton");
    if (connect(sock, (struct sockaddr *)&nm_addr, sizeof(nm_addr)) < 0) error_exit("connect");
    printf("Connected to Name Server at %s...\n", nm_ip);
    
    Header header; 
    header.type = REQ_CLIENT_REGISTER; 
    header.payload_size = sizeof(Msg_Client_Register);
    
    Msg_Client_Register reg_msg; 
    strncpy(reg_msg.username, username, MAX_USERNAME);
    
    if (send(sock, &header, sizeof(Header), 0) < 0) error_exit("send header");
    if (send(sock, &reg_msg, sizeof(reg_msg), 0) < 0) error_exit("send payload");
    
    if (recv(sock, &header, sizeof(Header), 0) < 0 || header.type != RES_OK) error_exit("Registration failed");
    
    return sock;
}
