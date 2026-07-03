#include "identity.h"
#include "fingerprint.h"
#include <sys/stat.h>

static void make_node_id(char *out, const unsigned char *pubkey) {
    char hex[17];
    sodium_bin2hex(hex, sizeof(hex), pubkey, 4);
    for (int i = 0; i < 8; i++)
        hex[i] = (hex[i] >= 'a') ? hex[i] - 32 : hex[i];
    snprintf(out, 32, "NODE-%s", hex);
}

int identity_init_dir(NodeIdentity *id, const char *dir) {
    mkdir(dir, 0700);
    crypto_sign_keypair(id->public_key, id->secret_key);
    make_node_id(id->node_id, id->public_key);

    char fingerprint[128];
    generate_fingerprint(fingerprint, sizeof(fingerprint));
    save_fingerprint(fingerprint);

    return identity_save_dir(id, dir);
}

int identity_init(NodeIdentity *id) {
    return identity_init_dir(id, CONFIG_DIR);
}

int identity_save_dir(const NodeIdentity *id, const char *dir) {
    char node_id_path[256], keypair_path[256];
    snprintf(node_id_path, sizeof(node_id_path), "%s/node_id",     dir);
    snprintf(keypair_path, sizeof(keypair_path), "%s/keypair.bin", dir);

    FILE *f = fopen(node_id_path, "w");
    if (!f) return -1;
    fprintf(f, "%s\n", id->node_id);
    fclose(f);

    f = fopen(keypair_path, "wb");
    if (!f) return -1;
    fwrite(id->public_key, 1, crypto_sign_PUBLICKEYBYTES, f);
    fwrite(id->secret_key, 1, crypto_sign_SECRETKEYBYTES, f);
    fclose(f);
    chmod(keypair_path, 0600);
    return 0;
}

int identity_save(const NodeIdentity *id) {
    return identity_save_dir(id, CONFIG_DIR);
}

int identity_load_dir(NodeIdentity *id, const char *dir) {
    char node_id_path[256], keypair_path[256];
    snprintf(node_id_path, sizeof(node_id_path), "%s/node_id",     dir);
    snprintf(keypair_path, sizeof(keypair_path), "%s/keypair.bin", dir);

    FILE *f = fopen(node_id_path, "r");
    if (!f) return -1;
    if (!fgets(id->node_id, sizeof(id->node_id), f)) {
        fclose(f); return -1;
    }
    fclose(f);
    size_t l = strlen(id->node_id);
    if (l > 0 && id->node_id[l-1] == '\n') id->node_id[l-1] = '\0';

    f = fopen(keypair_path, "rb");
    if (!f) return -1;
    fread(id->public_key, 1, crypto_sign_PUBLICKEYBYTES, f);
    fread(id->secret_key, 1, crypto_sign_SECRETKEYBYTES, f);
    fclose(f);
    return 0;
}

int identity_load(NodeIdentity *id) {
    return identity_load_dir(id, CONFIG_DIR);
}

void identity_print(const NodeIdentity *id) {
    char pubkey_hex[crypto_sign_PUBLICKEYBYTES * 2 + 1];
    sodium_bin2hex(pubkey_hex, sizeof(pubkey_hex),
                   id->public_key, crypto_sign_PUBLICKEYBYTES);
    printf("  Node ID   : %s\n", id->node_id);
    printf("  Public Key: %.32s...\n", pubkey_hex);
}
