#ifndef COMMON_H
#define COMMON_H

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sodium.h>

#define LITHIUM_VERSION     "1.0.0"
#define NODE_ID_FILE        ".lithium/node_id"
#define KEYPAIR_FILE        ".lithium/keypair.bin"
#define FINGERPRINT_FILE    ".lithium/fingerprint"
#define CONFIG_DIR          ".lithium"

typedef struct {
    char node_id[32];
    unsigned char public_key[crypto_sign_PUBLICKEYBYTES];
    unsigned char secret_key[crypto_sign_SECRETKEYBYTES];
} NodeIdentity;

#endif
