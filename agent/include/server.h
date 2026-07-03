#ifndef SERVER_H
#define SERVER_H

#include "common.h"

#define NODE_PORT 7701

void server_start(const NodeIdentity *id);
void server_start_port(const NodeIdentity *id, int port);

#endif
