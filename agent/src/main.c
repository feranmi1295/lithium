#include "common.h"
#include "identity.h"
#include "heartbeat.h"
#include "fingerprint.h"
#include "server.h"
#include <sys/stat.h>
#include <pthread.h>

typedef struct {
    NodeIdentity id;
    int port;
} StartArgs;

static void *heartbeat_thread(void *arg) {
    StartArgs *a = (StartArgs *)arg;
    heartbeat_loop_port(&a->id, 10, a->port);
    return NULL;
}

static void cmd_init(const char *data_dir) {
    char node_id_path[256];
    snprintf(node_id_path, sizeof(node_id_path), "%s/node_id", data_dir);
    struct stat st;
    if (stat(node_id_path, &st) == 0) {
        printf("⚠  Node already initialized in %s\n", data_dir);
        return;
    }
    if (sodium_init() < 0) { fprintf(stderr, "libsodium init failed\n"); exit(1); }
    mkdir(data_dir, 0700);

    printf("🔋 Lithium Node Setup\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  Data dir : %s\n", data_dir);
    printf("  Generating keypair...\n");

    NodeIdentity id;
    if (identity_init_dir(&id, data_dir) != 0) {
        fprintf(stderr, "identity init failed\n"); exit(1);
    }
    printf("  Hardware fingerprint captured\n");
    printf("  Identity saved\n\n");
    printf("  ✅ Node registered successfully\n\n");
    identity_print(&id);
    printf("\n  Run 'lithium start --data %s' to begin.\n", data_dir);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
}

static void cmd_start(const char *data_dir, int port) {
    char node_id_path[256];
    snprintf(node_id_path, sizeof(node_id_path), "%s/node_id", data_dir);
    struct stat st;
    if (stat(node_id_path, &st) != 0) {
        printf("⚠  Node not initialized. Run 'lithium init --data %s'\n", data_dir);
        return;
    }
    if (sodium_init() < 0) { fprintf(stderr, "libsodium init failed\n"); exit(1); }

    static StartArgs args;
    if (identity_load_dir(&args.id, data_dir) != 0) {
        fprintf(stderr, "Failed to load identity\n"); exit(1);
    }
    args.port = port;

    pthread_t hb_thread;
    pthread_create(&hb_thread, NULL, heartbeat_thread, &args);
    pthread_detach(hb_thread);

    server_start_port(&args.id, port);
}

static void cmd_status(const char *data_dir) {
    if (sodium_init() < 0) exit(1);
    NodeIdentity id;
    if (identity_load_dir(&id, data_dir) != 0) {
        printf("⚠  Node not initialized in %s\n", data_dir); return;
    }
    char fingerprint[128];
    load_fingerprint(fingerprint, sizeof(fingerprint));
    printf("\n🔋 Lithium Node Status\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    identity_print(&id);
    printf("  Data dir   : %s\n", data_dir);
    printf("  Fingerprint: %.24s...\n", fingerprint);
    printf("  Version    : %s\n", LITHIUM_VERSION);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
}

static void cmd_help() {
    printf("\n🔋 Lithium v%s\n\n", LITHIUM_VERSION);
    printf("Usage: lithium <command> [options]\n\n");
    printf("Commands:\n");
    printf("  init   [--data DIR]               Register as a node\n");
    printf("  start  [--data DIR] [--port PORT]  Start contributing\n");
    printf("  status [--data DIR]               View node status\n");
    printf("  help                              Show this message\n\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) { cmd_help(); return 0; }

    const char *data_dir = ".lithium";
    int port = NODE_PORT;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--data") == 0 && i+1 < argc)
            data_dir = argv[++i];
        else if (strcmp(argv[i], "--port") == 0 && i+1 < argc)
            port = atoi(argv[++i]);
    }

    if      (strcmp(argv[1], "init")   == 0) cmd_init(data_dir);
    else if (strcmp(argv[1], "start")  == 0) cmd_start(data_dir, port);
    else if (strcmp(argv[1], "status") == 0) cmd_status(data_dir);
    else if (strcmp(argv[1], "help")   == 0) cmd_help();
    else { printf("Unknown command: %s\n", argv[1]); cmd_help(); return 1; }

    return 0;
}
