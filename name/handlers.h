// Header for Naming Server (NM) request handlers.

#ifndef NM_HANDLERS_H
#define NM_HANDLERS_H

#include "protocol.h"
#include <sys/select.h>

// Processes an incoming message on the Naming Server.
void handle_nm_message(int sock_fd, Header header, char* peer_ip, fd_set* master_set);

#endif // NM_HANDLERS_H
