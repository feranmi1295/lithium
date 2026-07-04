#ifndef CLIENT_H
#define CLIENT_H

#include "common.h"

#define CLIENT_ID_FILE  ".lithium_client/client_id"
#define CLIENT_DIR      ".lithium_client"
#define CLIENT_JOBS_FILE ".lithium_client/jobs.log"

typedef struct {
    char client_id[32];
    char api_key[64];
} ClientIdentity;

void cmd_client(int argc, char *argv[]);

#endif
