#ifndef HANDLERS_H
#define HANDLERS_H

#include "protocol.h"

void send_filename_request(int sock, MessageType type, char* filename);
void print_metadata_view(Msg_Full_Metadata* meta);
void print_metadata_header_view();
void print_metadata_info(Msg_Full_Metadata* meta);
void print_metadata_header_info();
void print_metadata_footer();
int connect_to_storage_server(char* ss_ip, int ss_port);
void handle_read_from_ss(char* filename, char* ss_ip, int ss_port);
void handle_stream_from_ss(char* filename, char* ss_ip, int ss_port);
void handle_write_to_ss(char* filename, int sentence_num, char* ss_ip, int ss_port);
int register_with_name_server(char* nm_ip, char* username);

#endif // HANDLERS_H
