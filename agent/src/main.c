#include "common.h"
#include "identity.h"
#include "heartbeat.h"
#include "fingerprint.h"
#include <sys/stat.h>

static int node_exists() {
    struct stat st;
    return (stat(NODE_ID_FILE, &st) == 0);
}

static void cmd_init() {
    if (node_exists()) {
        printf("⚠  Node already initialized.\n");
        printf("   Run 'lithium start' to begin contributing.\n");
        return;
    }

    if (sodium_init() < 0) {
        fprintf(stderr, "Failed to initialize libsodium\n");
        exit(1);
    }

    printf("🔋 Lithium Node Setup\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  Generating keypair...\n");

    NodeIdentity id;
    if (identity_init(&id) != 0) {
        fprintf(stderr, "Failed to initialize identity\n");
        exit(1);
    }

    char fingerprint[128];
    generate_fingerprint(fingerprint, sizeof(fingerprint));

    printf("  Hardware fingerprint captured\n");
    printf("  Identity saved\n\n");
    printf("  ✅ Node registered successfully\n\n");
    identity_print(&id);
    printf("\n  Run 'lithium start' to begin contributing.\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
}

static void cmd_start() {
    if (!node_exists()) {
        printf("⚠  Node not initialized. Run 'lithium init' first.\n");
        return;
    }

    if (sodium_init() < 0) {
        fprintf(stderr, "Failed to initialize libsodium\n");
        exit(1);
    }

    NodeIdentity id;
    if (identity_load(&id) != 0) {
        fprintf(stderr, "Failed to load identity\n");
        exit(1);
    }

    heartbeat_loop(&id, 10);
}

static void cmd_status() {
    if (!node_exists()) {
        printf("⚠  Node not initialized. Run 'lithium init' first.\n");
        return;
    }

    if (sodium_init() < 0) exit(1);

    NodeIdentity id;
    if (identity_load(&id) != 0) {
        fprintf(stderr, "Failed to load identity\n");
        exit(1);
    }

    char fingerprint[128];
    load_fingerprint(fingerprint, sizeof(fingerprint));

    printf("\n🔋 Lithium Node Status\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    identity_print(&id);
    printf("  Fingerprint: %.24s...\n", fingerprint);
    printf("  Status     : Initialized\n");
    printf("  Version    : %s\n", LITHIUM_VERSION);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
}

static void cmd_help() {
    printf("\n🔋 Lithium v%s\n\n", LITHIUM_VERSION);
    printf("Usage: lithium <command>\n\n");
    printf("Commands:\n");
    printf("  init      Register this machine as a node\n");
    printf("  start     Begin contributing compute\n");
    printf("  status    View node identity and status\n");
    printf("  help      Show this message\n\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) { cmd_help(); return 0; }

    if      (strcmp(argv[1], "init")   == 0) cmd_init();
    else if (strcmp(argv[1], "start")  == 0) cmd_start();
    else if (strcmp(argv[1], "status") == 0) cmd_status();
    else if (strcmp(argv[1], "help")   == 0) cmd_help();
    else {
        printf("Unknown command: %s\n", argv[1]);
        cmd_help();
        return 1;
    }

    return 0;
}
