#include "common.h"
#include "identity.h"
#include "heartbeat.h"
#include "fingerprint.h"
#include "server.h"
#include "client.h"
#include <sys/stat.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define SCHEDULER_HOST "127.0.0.1"
#define SCHEDULER_PORT 7700

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

static void cmd_recover(const char *data_dir) {
    if (sodium_init() < 0) { fprintf(stderr, "libsodium init failed\n"); exit(1); }

    printf("🔋 Lithium Node Recovery\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  Scanning hardware fingerprint...\n");

    char fingerprint[128] = {0};
    if (generate_fingerprint(fingerprint, sizeof(fingerprint)) < 0) {
        fprintf(stderr, "  Failed to generate fingerprint\n"); exit(1);
    }
    printf("  Fingerprint: %.24s...\n", fingerprint);

    NodeIdentity new_id;
    crypto_sign_keypair(new_id.public_key, new_id.secret_key);

    char pubkey_hex[crypto_sign_PUBLICKEYBYTES * 2 + 1];
    sodium_bin2hex(pubkey_hex, sizeof(pubkey_hex),
                   new_id.public_key, crypto_sign_PUBLICKEYBYTES);

    printf("  New keypair generated\n");
    printf("  Contacting scheduler...\n");

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); exit(1); }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(SCHEDULER_PORT);
    inet_pton(AF_INET, SCHEDULER_HOST, &addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "  Cannot reach scheduler\n");
        close(sock); exit(1);
    }

    char body[768];
    snprintf(body, sizeof(body),
        "{\"fingerprint\":\"%s\",\"new_public_key\":\"%s\"}",
        fingerprint, pubkey_hex);

    char request[1024];
    snprintf(request, sizeof(request),
        "POST /recover HTTP/1.0\r\n"
        "Host: %s:%d\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "\r\n%s",
        SCHEDULER_HOST, SCHEDULER_PORT, strlen(body), body);

    send(sock, request, strlen(request), 0);
    char resp[1024] = {0};
    recv(sock, resp, sizeof(resp) - 1, 0);
    close(sock);

    if (!strstr(resp, "200")) {
        printf("  ❌ Recovery failed — node not found for this hardware\n");
        printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
        exit(1);
    }

    char *body_start = strstr(resp, "\r\n\r\n");
    if (body_start) body_start += 4;

    char recovered_node_id[32] = {0};
    if (body_start) {
        char *id_start = strstr(body_start, "\"node_id\":\"");
        if (id_start) {
            id_start += 11;
            char *id_end = strchr(id_start, '"');
            if (id_end) {
                size_t len = id_end - id_start;
                if (len >= sizeof(recovered_node_id))
                    len = sizeof(recovered_node_id) - 1;
                memcpy(recovered_node_id, id_start, len);
            }
        }
    }

    mkdir(data_dir, 0700);
    strncpy(new_id.node_id, recovered_node_id, sizeof(new_id.node_id) - 1);
    identity_save_dir(&new_id, data_dir);
    save_fingerprint(fingerprint);

    printf("\n  ✅ Node recovered successfully\n\n");
    printf("  Node ID   : %s\n", recovered_node_id);
    printf("  Public Key: %.32s...\n", pubkey_hex);
    printf("\n  Run 'lithium start' to resume contributing.\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
}

static void cmd_help() {
    printf("\n🔋 Lithium v%s\n\n", LITHIUM_VERSION);
    printf("Usage: lithium <command> [options]\n\n");
    printf("Node Commands:\n");
    printf("  init    [--data DIR]               Register as a node\n");
    printf("  start   [--data DIR] [--port PORT]  Start contributing\n");
    printf("  status  [--data DIR]               View node status\n");
    printf("  recover [--data DIR]               Recover lost keys\n\n");
    printf("Client Commands:\n");
    printf("  client <subcommand>               Startup/client mode\n");
    printf("  client help                       Show client commands\n\n");
    printf("  help                              Show this message\n\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) { cmd_help(); return 0; }

    /* handle client mode */
    if (strcmp(argv[1], "client") == 0) {
        if (sodium_init() < 0) { fprintf(stderr, "libsodium init failed\n"); exit(1); }
        cmd_client(argc - 2, argv + 2);
        return 0;
    }

    const char *data_dir = ".lithium";
    int port = NODE_PORT;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--data") == 0 && i+1 < argc)
            data_dir = argv[++i];
        else if (strcmp(argv[i], "--port") == 0 && i+1 < argc)
            port = atoi(argv[++i]);
    }

    if      (strcmp(argv[1], "init")    == 0) cmd_init(data_dir);
    else if (strcmp(argv[1], "start")   == 0) cmd_start(data_dir, port);
    else if (strcmp(argv[1], "status")  == 0) cmd_status(data_dir);
    else if (strcmp(argv[1], "recover") == 0) cmd_recover(data_dir);
    else if (strcmp(argv[1], "help")    == 0) cmd_help();
    else { printf("Unknown command: %s\n", argv[1]); cmd_help(); return 1; }

    return 0;
}
