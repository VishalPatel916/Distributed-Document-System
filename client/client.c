#include "protocol.h"
#include <netinet/in.h>
#include "handlers.h"
int main(int argc, char* argv[]) {
    // Command-line arg: ./client [nm_ip]
    char nm_ip[MAX_IP_LEN] = "127.0.0.1";
    if (argc >= 2) {
        strncpy(nm_ip, argv[1], sizeof(nm_ip) - 1);
    }
    
    char username[MAX_USERNAME];
    printf("Enter your username: ");
    fgets(username, MAX_USERNAME, stdin);
    username[strcspn(username, "\n")] = 0;
    
    int sock = register_with_name_server(nm_ip, username);
    printf("Registration successful! Welcome, %s.\n", username);

    Header header;
    char line_buffer[1024];
    while (1) {
        printf("> ");
        fflush(stdout);

        if (fgets(line_buffer, sizeof(line_buffer), stdin) == NULL) break; 

        char* command = strtok(line_buffer, " \n");
        if (command == NULL) continue;

        if (strcmp(command, "CREATE") == 0) {
            char* filename = strtok(NULL, " \n");
            if (filename == NULL) { printf("Usage: CREATE <filename>\n"); }
            else {
                send_filename_request(sock, REQ_CREATE, filename);
                recv(sock, &header, sizeof(header), 0);
                if (header.type == RES_OK) printf("File '%s' created successfully!\n", filename);
                else if (header.type == RES_ERROR_FILE_EXISTS) printf("Error: File '%s' already exists.\n", filename);
                else printf("Error: Could not create file.\n");
            }
        } 
        else if (strcmp(command, "READ") == 0) {
            char* filename = strtok(NULL, " \n");
            if (filename == NULL) { printf("Usage: READ <filename>\n"); }
            else {
                send_filename_request(sock, REQ_READ, filename);
                recv(sock, &header, sizeof(header), 0);
                if (header.type == RES_READ_LOCATION) { 
                    Msg_Read_Response resp; 
                    recv(sock, &resp, sizeof(resp), 0); 
                    handle_read_from_ss(filename, resp.ss_ip, resp.ss_port);
                } else if (header.type == RES_ERROR_NOT_FOUND) {
                    printf("Error: File '%s' not found.\n", filename);
                }
                else if (header.type == RES_ERROR_ACCESS_DENIED) printf("Error: Access denied.\n");
                else printf("Error: Could not read file.\n");
            }
        }
        else if (strcmp(command, "WRITE") == 0) {
            char* filename = strtok(NULL, " \n");
            char* sentence_str = strtok(NULL, " \n");
            if (filename == NULL || sentence_str == NULL) { printf("Usage: WRITE <filename> <sentence_number>\n"); }
            else {
                int sentence_num = atoi(sentence_str);
                send_filename_request(sock, REQ_WRITE, filename);
                recv(sock, &header, sizeof(header), 0);
                if (header.type == RES_READ_LOCATION) { 
                    Msg_Read_Response resp; 
                    recv(sock, &resp, sizeof(resp), 0); 
                    handle_write_to_ss(filename, sentence_num, resp.ss_ip, resp.ss_port);
                } else if (header.type == RES_ERROR_NOT_FOUND) {
                    printf("Error: File '%s' not found.\n", filename);
                }
                else if (header.type == RES_ERROR_ACCESS_DENIED) printf("Error: Access denied.\n");
                else printf("Error: Could not write to file (NM error).\n");
            }
        }
        else if (strcmp(command, "LIST") == 0) {
            send_simple_header(sock, REQ_LIST);
            recv(sock, &header, sizeof(header), 0);
            if (header.type == RES_LIST_HDR) {
                Msg_List_Hdr list_hdr; recv(sock, &list_hdr, sizeof(list_hdr), 0);
                printf("--- Registered Users (%d) ---\n", list_hdr.user_count);
                for (int i = 0; i < list_hdr.user_count; i++) { Msg_List_Item item; recv(sock, &item, sizeof(item), 0); printf("  - %s\n", item.username); }
            }
        }
        else if (strcmp(command, "DELETE") == 0) {
            char* filename = strtok(NULL, " \n");
            if (filename == NULL) { printf("Usage: DELETE <filename>\n"); }
            else {
                send_filename_request(sock, REQ_DELETE, filename);
                recv(sock, &header, sizeof(header), 0);
                if (header.type == RES_OK) printf("File '%s' deleted successfully.\n", filename);
                else if (header.type == RES_ERROR_NOT_FOUND) printf("Error: File not found.\n");
                else if (header.type == RES_ERROR_ACCESS_DENIED) printf("Error: Access denied (you are not the owner).\n");
                else printf("Error: Could not delete file.\n");
            }
        }
        else if (strcmp(command, "UNDO") == 0) {
            char* filename = strtok(NULL, " \n");
            if (filename == NULL) { printf("Usage: UNDO <filename>\n"); }
            else {
                send_filename_request(sock, REQ_UNDO, filename);
                recv(sock, &header, sizeof(header), 0);
                if (header.type == RES_OK) printf("Undo successful for '%s'.\n", filename);
                else if (header.type == RES_ERROR_NOT_FOUND) printf("Error: File not found.\n");
                else if (header.type == RES_ERROR_ACCESS_DENIED) printf("Error: Access denied.\n");
                else printf("Error: Could not undo file.\n");
            }
        }
        else if (strcmp(command, "STREAM") == 0) {
            char* filename = strtok(NULL, " \n");
            if (filename == NULL) { printf("Usage: STREAM <filename>\n"); }
            else {
                send_filename_request(sock, REQ_STREAM, filename);
                recv(sock, &header, sizeof(header), 0);
                if (header.type == RES_READ_LOCATION) { 
                    Msg_Read_Response resp; 
                    recv(sock, &resp, sizeof(resp), 0); 
                    handle_stream_from_ss(filename, resp.ss_ip, resp.ss_port);
                } else if (header.type == RES_ERROR_NOT_FOUND) {
                    printf("Error: File '%s' not found.\n", filename);
                }
                else printf("Error: Could not stream file.\n");
            }
        }
        else if (strcmp(command, "VIEW") == 0) {
            char* flag = strtok(NULL, " \n");
            Msg_View_Request req;
            req.flag_a = 0; req.flag_l = 0;
            if (flag != NULL) {
                if (strstr(flag, "a")) req.flag_a = 1;
                if (strstr(flag, "l")) req.flag_l = 1;
            }
            header.type = REQ_VIEW; header.payload_size = sizeof(req);
            send(sock, &header, sizeof(header), 0); send(sock, &req, sizeof(req), 0);
            recv(sock, &header, sizeof(header), 0);
            Msg_View_Hdr view_hdr; recv(sock, &view_hdr, sizeof(view_hdr), 0);
            
            if(req.flag_l) print_metadata_header_view();
            else printf("--- Files (%d) ---\n", view_hdr.file_count);

            for(int i=0; i < view_hdr.file_count; i++) {
                recv(sock, &header, sizeof(header), 0);
                if (header.type == RES_VIEW_ITEM_SHORT) {
                    Msg_View_Item_Short item; recv(sock, &item, sizeof(item), 0);
                    printf("  %s\n", item.filename);
                } else if (header.type == RES_VIEW_ITEM_LONG) {
                    Msg_Full_Metadata meta; recv(sock, &meta, sizeof(meta), 0);
                    print_metadata_view(&meta);
                    if (meta.access_count > 0) {
                        AccessEntry dummy_list[MAX_PERMISSIONS_PER_FILE];
                        recv(sock, &dummy_list, sizeof(AccessEntry) * meta.access_count, 0);
                    }
                }
            }
            if(req.flag_l) print_metadata_footer();
        }
        else if (strcmp(command, "INFO") == 0) {
            char* filename = strtok(NULL, " \n");
            if (filename == NULL) { printf("Usage: INFO <filename>\n"); }
            else {
                send_filename_request(sock, REQ_INFO, filename);
                recv(sock, &header, sizeof(header), 0);
                if (header.type == RES_INFO) {
                    Msg_Full_Metadata meta; recv(sock, &meta, sizeof(meta), 0);
                    print_metadata_header_info();
                    print_metadata_info(&meta);
                    print_metadata_footer();
                    if (meta.access_count > 0) {
                        printf("Access List:\n");
                        AccessEntry list[MAX_PERMISSIONS_PER_FILE];
                        recv(sock, &list, sizeof(AccessEntry) * meta.access_count, 0);
                        for(int i=0; i < meta.access_count; i++) {
                            printf("  - %s (%s)\n", list[i].username, list[i].permission == READ_ONLY ? "Read" : "Read/Write");
                        }
                    } else { printf("Access List: (empty)\n"); }
                } else if (header.type == RES_ERROR_NOT_FOUND) printf("Error: File not found.\n");
                else if (header.type == RES_ERROR_ACCESS_DENIED) printf("Error: Access denied.\n");
            }
        }
        else if (strcmp(command, "ADDACCESS") == 0) {
            char* flag = strtok(NULL, " \n");
            char* filename = strtok(NULL, " \n");
            // Capture the rest of the line as username (allow spaces)
            char* user = strtok(NULL, "\n");
            if (flag == NULL || filename == NULL || user == NULL) { printf("Usage: ADDACCESS -R|-W <filename> <username>\n"); }
            else {
                // Trim leading/trailing spaces around username
                while (*user == ' ') user++;
                size_t ulen = strlen(user);
                while (ulen > 0 && user[ulen - 1] == ' ') { user[--ulen] = '\0'; }
                if (ulen == 0) { printf("Usage: ADDACCESS -R|-W <filename> <username>\n"); }
                else {
                    Msg_Access_Request req;
                    strncpy(req.filename, filename, MAX_FILENAME);
                    strncpy(req.username, user, MAX_USERNAME);
                    if (strcmp(flag, "-W") == 0) req.perm = READ_WRITE;
                    else req.perm = READ_ONLY;
                    
                    header.type = REQ_ADD_ACCESS; header.payload_size = sizeof(req);
                    send(sock, &header, sizeof(header), 0); send(sock, &req, sizeof(req), 0);
                    
                    recv(sock, &header, sizeof(header), 0);
                    if (header.type == RES_OK) printf("Access granted.\n");
                    else if (header.type == RES_ERROR_ACCESS_DENIED) printf("Error: Access denied (you are not the owner).\n");
                    else printf("Error: Could not add access.\n");
                }
            }
        }
        else if (strcmp(command, "REMACCESS") == 0) {
            char* filename = strtok(NULL, " \n");
            // Capture the rest of the line as username (allow spaces)
            char* user = strtok(NULL, "\n");
            if (filename == NULL || user == NULL) { printf("Usage: REMACCESS <filename> <username>\n"); }
            else {
                while (*user == ' ') user++;
                size_t ulen = strlen(user);
                while (ulen > 0 && user[ulen - 1] == ' ') { user[--ulen] = '\0'; }
                if (ulen == 0) { printf("Usage: REMACCESS <filename> <username>\n"); }
                else {
                    Msg_Access_Request req;
                    strncpy(req.filename, filename, MAX_FILENAME);
                    strncpy(req.username, user, MAX_USERNAME);
                    
                    header.type = REQ_REM_ACCESS; header.payload_size = sizeof(req);
                    send(sock, &header, sizeof(header), 0); send(sock, &req, sizeof(req), 0);
                    
                    recv(sock, &header, sizeof(header), 0);
                    if (header.type == RES_OK) printf("Access removed.\n");
                    else if (header.type == RES_ERROR_ACCESS_DENIED) printf("Error: Access denied (you are not the owner).\n");
                    else printf("Error: Could not remove access.\n");
                }
            }
        }
        
        // --- NEW: Access Request Commands ---
        else if (strcmp(command, "REQACCESS") == 0) {
            char* flag = strtok(NULL, " \n");
            char* filename = strtok(NULL, " \n");
            if (flag == NULL || filename == NULL) { 
                printf("Usage: REQACCESS -R|-W <filename>\n"); 
            } else {
                Msg_Request_Access req;
                strncpy(req.filename, filename, MAX_FILENAME);
                strncpy(req.requesting_user, username, MAX_USERNAME);  // Use logged-in user
                if (strcmp(flag, "-W") == 0) req.requested_perm = READ_WRITE;
                else req.requested_perm = READ_ONLY;
                
                header.type = REQ_REQUEST_ACCESS; 
                header.payload_size = sizeof(req);
                send(sock, &header, sizeof(header), 0); 
                send(sock, &req, sizeof(req), 0);
                
                recv(sock, &header, sizeof(header), 0);
                if (header.type == RES_OK) 
                    printf("Access request sent to the owner of '%s'.\n", filename);
                else if (header.type == RES_ERROR_NOT_FOUND) 
                    printf("Error: File '%s' not found.\n", filename);
                else 
                    printf("Error: Could not send access request.\n");
            }
        }
        else if (strcmp(command, "CHECKREQUESTS") == 0) {
            header.type = REQ_CHECK_REQUESTS; 
            header.payload_size = 0;
            send(sock, &header, sizeof(header), 0);
            
            recv(sock, &header, sizeof(header), 0);
            if (header.type == RES_REQUEST_LIST) {
                Msg_Request_List_Hdr hdr;
                recv(sock, &hdr, sizeof(hdr), 0);
                
                if (hdr.request_count == 0) {
                    printf("No pending access requests.\n");
                } else {
                    // Receive all requests first
                    Msg_Request_Item* items = malloc(sizeof(Msg_Request_Item) * hdr.request_count);
                    for (int i = 0; i < hdr.request_count; i++) {
                        recv(sock, &items[i], sizeof(Msg_Request_Item), 0);
                    }
                    
                    printf("\n=== Pending Access Requests (%d total) ===\n", hdr.request_count);
                    
                    // Now process each request
                    for (int i = 0; i < hdr.request_count; i++) {
                        char time_str[100];
                        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", 
                                localtime(&items[i].timestamp));
                        
                        printf("\n[%d] Request ID: %d\n", i+1, items[i].request_id);
                        printf("    File: %s\n", items[i].filename);
                        printf("    User: %s\n", items[i].requesting_user);
                        printf("    Permission: %s\n", 
                              items[i].requested_perm == READ_WRITE ? "READ_WRITE" : "READ_ONLY");
                        printf("    Requested: %s\n", time_str);
                        
                        printf("    Approve this request? (yes/no): ");
                        fflush(stdout);
                        
                        char response[10];
                        if (fgets(response, sizeof(response), stdin) == NULL) break;
                        
                        // Remove newline
                        response[strcspn(response, "\n")] = 0;
                        
                        Header resp_header;
                        Msg_Request_Response resp;
                        resp.request_id = items[i].request_id;
                        
                        if (strcmp(response, "yes") == 0) {
                            resp_header.type = REQ_APPROVE_REQUEST;
                            resp_header.payload_size = sizeof(resp);
                            send(sock, &resp_header, sizeof(resp_header), 0);
                            send(sock, &resp, sizeof(resp), 0);
                            
                            recv(sock, &resp_header, sizeof(resp_header), 0);
                            if (resp_header.type == RES_OK) {
                                printf("    ✓ Access granted to %s\n", items[i].requesting_user);
                            } else {
                                printf("    ✗ Failed to grant access\n");
                            }
                        } else {
                            resp_header.type = REQ_DENY_REQUEST;
                            resp_header.payload_size = sizeof(resp);
                            send(sock, &resp_header, sizeof(resp_header), 0);
                            send(sock, &resp, sizeof(resp), 0);
                            
                            recv(sock, &resp_header, sizeof(resp_header), 0);
                            if (resp_header.type == RES_OK) {
                                printf("    ✓ Request denied/removed\n");
                            } else {
                                printf("    ✗ Failed to remove request\n");
                            }
                        }
                    }
                    printf("\n=== End of requests ===\n");
                    free(items);
                }
            } else {
                printf("Error: Could not retrieve requests.\n");
            }
        }
        
        // --- NEW FOR EXEC ---
        else if (strcmp(command, "EXEC") == 0) {
            char* filename = strtok(NULL, " \n");
            if (filename == NULL) { printf("Usage: EXEC <filename>\n"); }
            else {
                send_filename_request(sock, REQ_EXEC, filename);
                printf("--- Executing '%s' ---\n", filename);
                // Enter loop to receive output stream
                while(1) {
                    if (recv(sock, &header, sizeof(header), 0) <= 0) {
                        printf("Error: Connection lost during exec.\n");
                        break;
                    }
                    if (header.type == RES_EXEC_OUTPUT) {
                        Msg_Exec_Output msg;
                        recv(sock, &msg, sizeof(msg), 0);
                        printf("%s", msg.line); // Print the line (already has newline)
                        fflush(stdout);
                    } else if (header.type == RES_EXEC_DONE) {
                        break; // Success
                    } else if (header.type == RES_ERROR_NOT_FOUND) {
                        printf("Error: File not found.\n"); break;
                    } else if (header.type == RES_ERROR_ACCESS_DENIED) {
                        printf("Error: Access denied.\n"); break;
                    } else {
                        printf("Error: Execution failed.\n"); break;
                    }
                }
                printf("--- End of execution ---\n");
            }
        }
        // --- END EXEC ---
        
        // --- FOLDER COMMANDS ---
        else if (strcmp(command, "CREATEFOLDER") == 0) {
            char* foldername = strtok(NULL, " \n");
            if (foldername == NULL) {
                printf("Usage: CREATEFOLDER <foldername>\n");
            } else {
                header.type = REQ_CREATEFOLDER;
                header.payload_size = sizeof(Msg_Folder_Request);
                send(sock, &header, sizeof(header), 0);
                
                Msg_Folder_Request req;
                strncpy(req.foldername, foldername, MAX_FILENAME);
                send(sock, &req, sizeof(req), 0);
                
                recv(sock, &header, sizeof(header), 0);
                if (header.type == RES_OK) {
                    printf("Folder '%s' created successfully.\n", foldername);
                } else {
                    printf("Error: Failed to create folder.\n");
                }
            }
        }
        else if (strcmp(command, "MOVE") == 0) {
            char* filename = strtok(NULL, " \n");
            char* foldername = strtok(NULL, " \n");
            if (filename == NULL || foldername == NULL) {
                printf("Usage: MOVE <filename> <foldername>\n");
            } else {
                header.type = REQ_MOVE;
                header.payload_size = sizeof(Msg_Move_Request);
                send(sock, &header, sizeof(header), 0);
                
                Msg_Move_Request req;
                strncpy(req.filename, filename, MAX_FILENAME);
                strncpy(req.foldername, foldername, MAX_FILENAME);
                send(sock, &req, sizeof(req), 0);
                
                recv(sock, &header, sizeof(header), 0);
                if (header.type == RES_OK) {
                    printf("File '%s' moved to folder '%s' successfully.\n", filename, foldername);
                } else if (header.type == RES_ERROR_NOT_FOUND) {
                    printf("Error: File not found.\n");
                } else if (header.type == RES_ERROR_ACCESS_DENIED) {
                    printf("Error: Access denied. Only owner can move files.\n");
                } else {
                    printf("Error: Failed to move file.\n");
                }
            }
        }
        else if (strcmp(command, "VIEWFOLDER") == 0) {
            char* foldername = strtok(NULL, " \n");
            if (foldername == NULL) {
                printf("Usage: VIEWFOLDER <foldername>\n");
            } else {
                header.type = REQ_VIEWFOLDER;
                header.payload_size = sizeof(Msg_Folder_Request);
                send(sock, &header, sizeof(header), 0);
                
                Msg_Folder_Request req;
                strncpy(req.foldername, foldername, MAX_FILENAME);
                send(sock, &req, sizeof(req), 0);
                
                // Receive header
                recv(sock, &header, sizeof(header), 0);
                if (header.type == RES_VIEW_HDR) {
                    Msg_View_Hdr view_hdr;
                    recv(sock, &view_hdr, sizeof(view_hdr), 0);
                    
                    printf("Files in folder '%s' (%d files):\n", foldername, view_hdr.file_count);
                    for (int i = 0; i < view_hdr.file_count; i++) {
                        recv(sock, &header, sizeof(header), 0);
                        if (header.type == RES_VIEW_ITEM_SHORT) {
                            Msg_View_Item_Short item;
                            recv(sock, &item, sizeof(item), 0);
                            printf("  - %s\n", item.filename);
                        }
                    }
                } else {
                    printf("Error: Failed to view folder.\n");
                }
            }
        }
        // --- END FOLDER COMMANDS ---
        
        // --- CHECKPOINT COMMANDS ---
        else if (strcmp(command, "CHECKPOINT") == 0) {
            char* filename = strtok(NULL, " \n");
            char* tag = strtok(NULL, " \n");
            if (filename == NULL || tag == NULL) {
                printf("Usage: CHECKPOINT <filename> <checkpoint_tag>\n");
            } else {
                header.type = REQ_CHECKPOINT;
                header.payload_size = sizeof(Msg_Checkpoint_Request);
                send(sock, &header, sizeof(header), 0);
                
                Msg_Checkpoint_Request req;
                strncpy(req.filename, filename, MAX_FILENAME);
                strncpy(req.tag, tag, MAX_CHECKPOINT_TAG);
                send(sock, &req, sizeof(req), 0);
                
                recv(sock, &header, sizeof(header), 0);
                if (header.type == RES_OK) {
                    printf("Checkpoint '%s' created successfully for file '%s'.\n", tag, filename);
                } else {
                    printf("Error: Failed to create checkpoint (file not found, tag already exists, or permission denied).\n");
                }
            }
        }
        else if (strcmp(command, "VIEWCHECKPOINT") == 0) {
            char* filename = strtok(NULL, " \n");
            char* tag = strtok(NULL, " \n");
            if (filename == NULL || tag == NULL) {
                printf("Usage: VIEWcheckpoint <filename> <checkpoint_tag>\n");
            } else {
                header.type = REQ_VIEWCHECKPOINT;
                header.payload_size = sizeof(Msg_Checkpoint_Request);
                send(sock, &header, sizeof(header), 0);
                
                Msg_Checkpoint_Request req;
                strncpy(req.filename, filename, MAX_FILENAME);
                strncpy(req.tag, tag, MAX_CHECKPOINT_TAG);
                send(sock, &req, sizeof(req), 0);
                
                recv(sock, &header, sizeof(header), 0);
                if (header.type == RES_SS_FILE_OK) {
                    printf("\n=== Checkpoint '%s' content for '%s' ===\n", tag, filename);
                    char buffer[4096];
                    int remaining = header.payload_size;
                    while (remaining > 0) {
                        int to_read = (remaining < sizeof(buffer)) ? remaining : sizeof(buffer);
                        int bytes = recv(sock, buffer, to_read, 0);
                        if (bytes <= 0) break;
                        fwrite(buffer, 1, bytes, stdout);
                        remaining -= bytes;
                    }
                    printf("\n=== End of checkpoint ===\n");
                } else {
                    printf("Error: Checkpoint not found or permission denied.\n");
                }
            }
        }
        else if (strcmp(command, "REVERT") == 0) {
            char* filename = strtok(NULL, " \n");
            char* tag = strtok(NULL, " \n");
            if (filename == NULL || tag == NULL) {
                printf("Usage: REVERT <filename> <checkpoint_tag>\n");
            } else {
                header.type = REQ_REVERT;
                header.payload_size = sizeof(Msg_Checkpoint_Request);
                send(sock, &header, sizeof(header), 0);
                
                Msg_Checkpoint_Request req;
                strncpy(req.filename, filename, MAX_FILENAME);
                strncpy(req.tag, tag, MAX_CHECKPOINT_TAG);
                send(sock, &req, sizeof(req), 0);
                
                recv(sock, &header, sizeof(header), 0);
                if (header.type == RES_OK) {
                    printf("File '%s' successfully reverted to checkpoint '%s'.\n", filename, tag);
                } else {
                    printf("Error: Failed to revert (checkpoint not found or permission denied).\n");
                }
            }
        }
        else if (strcmp(command, "LISTCHECKPOINTS") == 0) {
            char* filename = strtok(NULL, " \n");
            if (filename == NULL) {
                printf("Usage: LISTCHECKPOINTS <filename>\n");
            } else {
                header.type = REQ_LISTCHECKPOINTS;
                header.payload_size = sizeof(Msg_ListCheckpoints_Request);
                send(sock, &header, sizeof(header), 0);
                
                Msg_ListCheckpoints_Request req;
                strncpy(req.filename, filename, MAX_FILENAME);
                send(sock, &req, sizeof(req), 0);
                
                recv(sock, &header, sizeof(header), 0);
                if (header.type == RES_CHECKPOINT_LIST) {
                    Msg_Checkpoint_List_Hdr hdr;
                    recv(sock, &hdr, sizeof(hdr), 0);
                    
                    if (hdr.checkpoint_count == 0) {
                        printf("No checkpoints found for file '%s'.\n", filename);
                    } else {
                        printf("Checkpoints for '%s' (%d total):\n", filename, hdr.checkpoint_count);
                        for (int i = 0; i < hdr.checkpoint_count; i++) {
                            Msg_Checkpoint_Item item;
                            recv(sock, &item, sizeof(item), 0);
                            
                            char time_str[100];
                            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&item.timestamp));
                            printf("  - Tag: %s  (Created: %s)\n", item.tag, time_str);
                        }
                    }
                } else {
                    printf("Error: File not found or permission denied.\n");
                }
            }
        }
        // --- END CHECKPOINT COMMANDS ---
        
        else if (strcmp(command, "QUIT") == 0 || strcmp(command, "EXIT") == 0) {
            break;
        }
        else {
            printf("Unknown command: %s\n", command);
        }
    }

    printf("Disconnecting...\n");
    close(sock);
    return 0;
}