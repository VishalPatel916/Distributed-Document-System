// Header file for client-side command handlers and network operations.

#ifndef HANDLERS_H
#define HANDLERS_H

#include "protocol.h"

// Sends a generic filename-based request over a socket.
void send_filename_request(int sock, MessageType type, char* filename);

// Prints a detailed view of a document's metadata.
void print_metadata_view(Msg_Full_Metadata* meta);

// Prints the header row for the metadata view display.
void print_metadata_header_view();

// Prints a compact information row for a document's metadata.
void print_metadata_info(Msg_Full_Metadata* meta);

// Prints the header row for the metadata info display.
void print_metadata_header_info();

// Prints the footer for metadata tables.
void print_metadata_footer();

// Establishes a TCP connection to a Storage Server.
int connect_to_storage_server(char* ss_ip, int ss_port);

// Handles reading a complete file directly from a Storage Server.
void handle_read_from_ss(char* filename, char* ss_ip, int ss_port);

// Handles streaming audio/video data from a Storage Server.
void handle_stream_from_ss(char* filename, char* ss_ip, int ss_port);

// Handles writing a specific sentence to a file on a Storage Server.
void handle_write_to_ss(char* filename, int sentence_num, char* ss_ip, int ss_port);

// Registers the client with the Naming Server.
int register_with_name_server(char* nm_ip, char* username);

#endif // HANDLERS_H
