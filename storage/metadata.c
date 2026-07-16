#include "metadata.h"
#include "document.h"
#include "locks.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>

void save_file_metadata(char* filename, char* owner, int access_count, AccessEntry* access_list) {
    FILE* f = fopen(METADATA_FILE, "r");
    FileMetadata_SS metadata[MAX_FILES_PER_SS];
    int meta_count = 0;
    
    if (f != NULL) {
        if (fscanf(f, "%d\n", &meta_count) == 1) {
            for (int i = 0; i < meta_count && i < MAX_FILES_PER_SS; i++) {
                fgets(metadata[i].filename, MAX_FILENAME, f);
                metadata[i].filename[strcspn(metadata[i].filename, "\n")] = 0;
                fgets(metadata[i].owner, MAX_USERNAME, f);
                metadata[i].owner[strcspn(metadata[i].owner, "\n")] = 0;
                fscanf(f, "%d\n", &metadata[i].access_count);
                for (int j = 0; j < metadata[i].access_count; j++) {
                    fscanf(f, "%s %d\n", metadata[i].access_list[j].username, (int*)&metadata[i].access_list[j].permission);
                }
            }
        }
        fclose(f);
    }
    
    int found = 0;
    for (int i = 0; i < meta_count; i++) {
        if (strcmp(metadata[i].filename, filename) == 0) {
            strncpy(metadata[i].owner, owner, MAX_USERNAME);
            metadata[i].access_count = access_count;
            for (int j = 0; j < access_count; j++) {
                metadata[i].access_list[j] = access_list[j];
            }
            found = 1;
            break;
        }
    }
    
    if (!found && meta_count < MAX_FILES_PER_SS) {
        strncpy(metadata[meta_count].filename, filename, MAX_USERNAME);
        strncpy(metadata[meta_count].owner, owner, MAX_USERNAME);
        metadata[meta_count].access_count = access_count;
        for (int j = 0; j < access_count; j++) {
            metadata[meta_count].access_list[j] = access_list[j];
        }
        meta_count++;
    }
    
    f = fopen(METADATA_FILE, "w");
    if (f == NULL) { log_event("ERROR: Failed to save metadata"); return; }
    fprintf(f, "%d\n", meta_count);
    for (int i = 0; i < meta_count; i++) {
        fprintf(f, "%s\n%s\n%d\n", metadata[i].filename, metadata[i].owner, metadata[i].access_count);
        for (int j = 0; j < metadata[i].access_count; j++) {
            fprintf(f, "%s %d\n", metadata[i].access_list[j].username, metadata[i].access_list[j].permission);
        }
    }
    fclose(f);
    log_event("  -> Metadata saved for '%s'", filename);
}

void load_file_metadata(char* filename, char* owner_out, int* access_count_out, AccessEntry* access_list_out) {
    FILE* f = fopen(METADATA_FILE, "r");
    if (f == NULL) {
        strcpy(owner_out, "system");
        *access_count_out = 0;
        return;
    }
    
    int meta_count;
    if (fscanf(f, "%d\n", &meta_count) != 1) {
        fclose(f);
        strcpy(owner_out, "system");
        *access_count_out = 0;
        return;
    }
    
    for (int i = 0; i < meta_count; i++) {
        char fname[MAX_FILENAME], owner[MAX_USERNAME];
        int access_count;
        
        fgets(fname, MAX_FILENAME, f);
        fname[strcspn(fname, "\n")] = 0;
        fgets(owner, MAX_USERNAME, f);
        owner[strcspn(owner, "\n")] = 0;
        fscanf(f, "%d\n", &access_count);
        
        if (strcmp(fname, filename) == 0) {
            strncpy(owner_out, owner, MAX_USERNAME);
            *access_count_out = access_count;
            for (int j = 0; j < access_count; j++) {
                fscanf(f, "%s %d\n", access_list_out[j].username, (int*)&access_list_out[j].permission);
            }
            fclose(f);
            return;
        } else {
            for (int j = 0; j < access_count; j++) {
                char dummy_user[MAX_USERNAME];
                int dummy_perm;
                fscanf(f, "%s %d\n", dummy_user, &dummy_perm);
            }
        }
    }
    
    fclose(f);
    strcpy(owner_out, "system");
    *access_count_out = 0;
}

void delete_file_metadata(char* filename) {
    FILE* f = fopen(METADATA_FILE, "r");
    if (f == NULL) return;
    
    FileMetadata_SS metadata[MAX_FILES_PER_SS];
    int meta_count = 0;
    
    if (fscanf(f, "%d\n", &meta_count) == 1) {
        for (int i = 0; i < meta_count && i < MAX_FILES_PER_SS; i++) {
            fgets(metadata[i].filename, MAX_FILENAME, f);
            metadata[i].filename[strcspn(metadata[i].filename, "\n")] = 0;
            fgets(metadata[i].owner, MAX_USERNAME, f);
            metadata[i].owner[strcspn(metadata[i].owner, "\n")] = 0;
            fscanf(f, "%d\n", &metadata[i].access_count);
            for (int j = 0; j < metadata[i].access_count; j++) {
                fscanf(f, "%s %d\n", metadata[i].access_list[j].username, (int*)&metadata[i].access_list[j].permission);
            }
        }
    }
    fclose(f);
    
    f = fopen(METADATA_FILE, "w");
    if (f == NULL) return;
    
    int new_count = 0;
    for (int i = 0; i < meta_count; i++) {
        if (strcmp(metadata[i].filename, filename) != 0) {
            new_count++;
        }
    }
    
    fprintf(f, "%d\n", new_count);
    for (int i = 0; i < meta_count; i++) {
        if (strcmp(metadata[i].filename, filename) != 0) {
            fprintf(f, "%s\n%s\n%d\n", metadata[i].filename, metadata[i].owner, metadata[i].access_count);
            for (int j = 0; j < metadata[i].access_count; j++) {
                fprintf(f, "%s %d\n", metadata[i].access_list[j].username, metadata[i].access_list[j].permission);
            }
        }
    }
    fclose(f);
}

void calculate_and_send_metadata(int nm_sock, char* filename, char* file_path) {
    FILE* f = fopen(file_path, "r");
    if (f == NULL) { log_event("  -> ERROR: Could not open file %s to calculate metadata.", file_path); return; }
    long file_size = 0; int word_count = 0; int char_count = 0; int in_word = 0; char c;
    fseek(f, 0, SEEK_END); file_size = ftell(f); rewind(f);
    while ((c = fgetc(f)) != EOF) {
        char_count++;
        if (isspace(c)) { in_word = 0; } else { if (in_word == 0) { word_count++; in_word = 1; } }
    }
    fclose(f);
    struct stat st; time_t mod_time = 0; time_t access_time = 0;
    if (stat(file_path, &st) == 0) { mod_time = st.st_mtime; access_time = st.st_atime; }
    log_event("  -> Calculated stats for '%s': size=%ld, words=%d, chars=%d", filename, file_size, word_count, char_count);
    Header header; header.type = REQ_UPDATE_METADATA; header.payload_size = sizeof(Msg_Update_Metadata);
    Msg_Update_Metadata msg;
    strncpy(msg.filename, filename, MAX_FILENAME);
    msg.file_size = file_size; msg.word_count = word_count; msg.char_count = char_count;
    msg.last_modified = mod_time; msg.last_accessed = access_time;
    if (send(nm_sock, &header, sizeof(header), 0) < 0) log_event("  -> ERROR: send metadata header failed");
    if (send(nm_sock, &msg, sizeof(msg), 0) < 0) log_event("  -> ERROR: send metadata payload failed");
}

void handle_create_file(char* filename, char* file_path) {
    sprintf(file_path, "%s/%s", g_storage_path, filename);
    log_event("  -> Creating file at: %s", file_path);
    FILE* f = fopen(file_path, "w");
    if (f) { fclose(f); }
}

void handle_send_file(int client_sock, char* filename) { char file_path[768]; sprintf(file_path, "%s/%s", g_storage_path, filename); int fd = open(file_path, O_RDONLY); if (fd < 0) { log_event("  -> File not found. Sending error to socket %d", client_sock); send_simple_header(client_sock, RES_ERROR_NOT_FOUND); return; } send_simple_header(client_sock, RES_SS_FILE_OK); log_event("  -> Sending file '%s' to socket %d", filename, client_sock); char buffer[FILE_BUFFER_SIZE]; int bytes_read; while ((bytes_read = read(fd, buffer, FILE_BUFFER_SIZE)) > 0) if (send(client_sock, buffer, bytes_read, 0) < 0) break; close(fd); log_event("  -> Finished sending file to socket %d", client_sock); }

void handle_stream_file(int client_sock, char* filename) {
    char file_path[768]; sprintf(file_path, "%s/%s", g_storage_path, filename);
    int fd = open(file_path, O_RDONLY);
    if (fd < 0) { log_event("  -> File not found. Sending error to socket %d", client_sock); send_simple_header(client_sock, RES_ERROR_NOT_FOUND); return; }
    send_simple_header(client_sock, RES_SS_FILE_OK);
    lseek(fd, 0, SEEK_END); long file_size = lseek(fd, 0, SEEK_CUR); lseek(fd, 0, SEEK_SET);
    char* mem_buffer = (char*)malloc(file_size + 1);
    if (!mem_buffer) { perror("malloc stream buffer"); close(fd); return; }
    read(fd, mem_buffer, file_size); mem_buffer[file_size] = '\0'; close(fd);
    log_event("  -> Streaming file '%s' to socket %d", filename, client_sock);
    char* word_context = NULL; char* word = strtok_r(mem_buffer, " \n\t", &word_context);
    while (word != NULL) {
        if (send(client_sock, word, strlen(word), 0) < 0) break;
        if (send(client_sock, " ", 1, 0) < 0) break;
        usleep(100000); 
        word = strtok_r(NULL, " \n\t", &word_context);
    }
    free(mem_buffer); log_event("  -> Finished streaming file to socket %d", client_sock);
}

void scan_directory_recursive(const char* base_path, const char* relative_path, 
                               char files[][MAX_FILENAME], int* count, int max_count) {
    char full_path[1024];
    if (relative_path && strlen(relative_path) > 0) {
        snprintf(full_path, sizeof(full_path), "%s/%s", base_path, relative_path);
    } else {
        strncpy(full_path, base_path, sizeof(full_path));
        full_path[sizeof(full_path) - 1] = '\0';
    }
    
    DIR *d = opendir(full_path);
    if (!d) return;
    
    struct dirent *dir;
    while ((dir = readdir(d)) != NULL && *count < max_count) {
        if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0) continue;
        if (strcmp(dir->d_name, ".metadata") == 0) continue;
        
        // Check path length before concatenating
        if (strlen(full_path) + strlen(dir->d_name) + 2 > 1024) continue;
        
        char item_path[1024];
        snprintf(item_path, sizeof(item_path), "%s/%s", full_path, dir->d_name);
        
        struct stat statbuf;
        if (stat(item_path, &statbuf) == 0) {
            if (S_ISDIR(statbuf.st_mode)) {
                // Directory - recurse into it
                char new_relative[512];
                if (relative_path && strlen(relative_path) > 0) {
                    if (strlen(relative_path) + strlen(dir->d_name) + 2 > 512) continue;
                    snprintf(new_relative, sizeof(new_relative), "%s/%s", relative_path, dir->d_name);
                } else {
                    strncpy(new_relative, dir->d_name, sizeof(new_relative));
                    new_relative[sizeof(new_relative) - 1] = '\0';
                }
                scan_directory_recursive(base_path, new_relative, files, count, max_count);
            } else if (S_ISREG(statbuf.st_mode)) {
                // Regular file - check length before adding
                if (relative_path && strlen(relative_path) > 0) {
                    if (strlen(relative_path) + strlen(dir->d_name) + 2 > MAX_FILENAME) continue;
                    snprintf(files[*count], MAX_FILENAME, "%s/%s", relative_path, dir->d_name);
                } else {
                    if (strlen(dir->d_name) >= MAX_FILENAME) continue;
                    strncpy(files[*count], dir->d_name, MAX_FILENAME);
                    files[*count][MAX_FILENAME - 1] = '\0';
                }
                (*count)++;
            }
        }
    }
    closedir(d);
}

void handle_client_disconnect(int sock_fd, fd_set* master_set) {
    struct sockaddr_in addr; socklen_t len = sizeof(addr);
    char ip_buf[MAX_IP_LEN] = "UNKNOWN_IP";
    if (getpeername(sock_fd, (struct sockaddr*)&addr, &len) == 0) { strncpy(ip_buf, inet_ntoa(addr.sin_addr), MAX_IP_LEN); }
    log_event("Client on socket %d (%s) disconnected", sock_fd, ip_buf);
    
    if (write_sessions[sock_fd].active) {
        WriteSession* session = &write_sessions[sock_fd];
        ActiveDoc* doc = &active_documents[session->doc_index];
        log_event("  -> Client was in a write session!");
        
        release_lock(doc->filename, session->sentence_ptr);
        release_active_doc(doc);
        
        // Clean up edit operations
        if (session->edit_ops != NULL) {
            free(session->edit_ops);
            session->edit_ops = NULL;
        }
        session->active = 0;
    }
    close(sock_fd);
    FD_CLR(sock_fd, master_set);
}

