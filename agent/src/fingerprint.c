#include "common.h"
#include "fingerprint.h"
#include <sodium.h>

/* Reads a sysfs file and trims newline */
static int read_sysfile(const char *path, char *out, size_t len) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    if (!fgets(out, len, f)) { fclose(f); return -1; }
    fclose(f);
    /* trim newline */
    size_t l = strlen(out);
    if (l > 0 && out[l-1] == '\n') out[l-1] = '\0';
    return 0;
}

int generate_fingerprint(char *out, size_t out_len) {
    char machine_id[128] = {0};
    char product_uuid[128] = {0};
    char hostname[128] = {0};

    /* /etc/machine-id — unique per install */
    read_sysfile("/etc/machine-id", machine_id, sizeof(machine_id));

    /* product_uuid — motherboard UUID */
    read_sysfile("/sys/class/dmi/id/product_uuid", product_uuid, sizeof(product_uuid));

    /* hostname as fallback salt */
    gethostname(hostname, sizeof(hostname));

    /* combine all three */
    char combined[512];
    snprintf(combined, sizeof(combined), "%s|%s|%s", machine_id, product_uuid, hostname);

    /* hash it with blake2b */
    unsigned char hash[crypto_generichash_BYTES];
    crypto_generichash(hash, sizeof(hash),
                       (unsigned char *)combined, strlen(combined),
                       NULL, 0);

    /* hex encode */
    sodium_bin2hex(out, out_len, hash, sizeof(hash));
    return 0;
}

int save_fingerprint(const char *fingerprint) {
    FILE *f = fopen(FINGERPRINT_FILE, "w");
    if (!f) return -1;
    fprintf(f, "%s\n", fingerprint);
    fclose(f);
    return 0;
}

int load_fingerprint(char *out, size_t out_len) {
    return (read_sysfile(FINGERPRINT_FILE, out, out_len) == 0) ? 0 : -1;
}
