#include "identity.h"
#include "fingerprint.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

static void make_node_id(char *out, const unsigned char *pubkey) {
    /* NODE- + first 8 hex chars of public key */
    char hex[17];
    sodium_bin2hex(hex, sizeof(hex), pubkey, 4);
    /* uppercase */
    for (int i = 0; i < 8; i++)
        hex[i] = (hex[i] >= 'a') ? hex[i] - 32 : hex[i];
    snprintf(out, 32, "NODE-%s", hex);
}

int identity_init(NodeIdentity *id) {
    /* create config dir */
    mkdir(CONFIG_DIR, 0700);

    /* generate keypair */
    crypto_sign_keypair(id->public_key, id->secret_key);

    /* derive node ID from public key */
    make_node_id(id->node_id, id->public_key);

    /* generate and save fingerprint */
    char fingerprint[128];
    generate_fingerprint(fingerprint, sizeof(fingerprint));
    save_fingerprint(fingerprint);

    /* save identity */
    return identity_save(id);
}

int identity_save(const NodeIdentity *id) {
    /* save node_id */
    FILE *f = fopen(NODE_ID_FILE, "w");
    if (!f) return -1;
    fprintf(f, "%s\n", id->node_id);
    fclose(f);

    /* save keypair as binary */
    f = fopen(KEYPAIR_FILE, "wb");
    if (!f) return -1;
    fwrite(id->public_key, 1, crypto_sign_PUBLICKEYBYTES, f);
    fwrite(id->secret_key, 1, crypto_sign_SECRETKEYBYTES, f);
    fclose(f);
    chmod(KEYPAIR_FILE, 0600);
    return 0;
}

int identity_load(NodeIdentity *id) {
    /* load node_id */
    FILE *f = fopen(NODE_ID_FILE, "r");
    if (!f) return -1;
    if (!fgets(id->node_id, sizeof(id->node_id), f)) {
        fclose(f); return -1;
    }
    fclose(f);
    size_t l = strlen(id->node_id);
    if (l > 0 && id->node_id[l-1] == '\n') id->node_id[l-1] = '\0';

    /* load keypair */
    f = fopen(KEYPAIR_FILE, "rb");
    if (!f) return -1;
    fread(id->public_key, 1, crypto_sign_PUBLICKEYBYTES, f);
    fread(id->secret_key, 1, crypto_sign_SECRETKEYBYTES, f);
    fclose(f);
    return 0;
}

void identity_print(const NodeIdentity *id) {
    char pubkey_hex[crypto_sign_PUBLICKEYBYTES * 2 + 1];
    sodium_bin2hex(pubkey_hex, sizeof(pubkey_hex),
                   id->public_key, crypto_sign_PUBLICKEYBYTES);
    printf("  Node ID   : %s\n", id->node_id);
    printf("  Public Key: %.32s...\n", pubkey_hex);
}
