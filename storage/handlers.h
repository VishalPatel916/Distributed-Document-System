// Header for Storage Server (SS) request handlers.

#ifndef SS_HANDLERS_H
#define SS_HANDLERS_H

#include "protocol.h"
#include <sys/select.h>

// Handles incoming orchestration and administrative messages from the NM.
void handle_nm_message_ss(int sock_fd, Header header, int nm_sock, fd_set* master_set);

// Handles incoming direct I/O messages (read/write/stream) from Clients.
void handle_client_message_ss(int sock_fd, Header header, int nm_sock, fd_set* master_set);

#endif // SS_HANDLERS_H
