#include "heartbeat.h"
#include "server.h"
#include "fingerprint.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h>
#include <ifaddrs.h>

#define SCHEDULER_HOST "127.0.0.1"
#define SCHEDULER_PORT 7700

static void get_local_ip(char *out, size_t len) {
    struct ifaddrs *ifaddr, *ifa;
    strcpy(out, "127.0.0.1");
    if (getifaddrs(&ifaddr) == -1) return;
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue;
        struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &sa->sin_addr, ip, sizeof(ip));
        if (strncmp(ip, "127.", 4) == 0) continue;
        strncpy(out, ip, len - 1);
        break;
    }
    freeifaddrs(ifaddr);
}

static int send_heartbeat_http(const NodeIdentity *id, int node_port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(SCHEDULER_PORT);
    inet_pton(AF_INET, SCHEDULER_HOST, &addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock); return -1;
    }

    char pubkey_hex[crypto_sign_PUBLICKEYBYTES * 2 + 1];
    sodium_bin2hex(pubkey_hex, sizeof(pubkey_hex),
                   id->public_key, crypto_sign_PUBLICKEYBYTES);

    char node_ip[INET_ADDRSTRLEN];
    get_local_ip(node_ip, sizeof(node_ip));

    char fingerprint[128] = {0};
    generate_fingerprint(fingerprint, sizeof(fingerprint));

    char body[768];
    snprintf(body, sizeof(body),
        "{\"node_id\":\"%s\",\"public_key\":\"%s\","
        "\"ip\":\"%s\",\"port\":\"%d\","
        "\"fingerprint\":\"%s\"}",
        id->node_id, pubkey_hex, node_ip, node_port, fingerprint);

    char request[1024];
    snprintf(request, sizeof(request),
        "POST /heartbeat HTTP/1.0\r\n"
        "Host: %s:%d\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "\r\n%s",
        SCHEDULER_HOST, SCHEDULER_PORT, strlen(body), body);

    send(sock, request, strlen(request), 0);
    char resp[256] = {0};
    recv(sock, resp, sizeof(resp) - 1, 0);
    close(sock);
    return (strstr(resp, "200") != NULL) ? 0 : -1;
}

static void run_heartbeat(const NodeIdentity *id,
                           int interval_seconds, int port) {
    char node_ip[INET_ADDRSTRLEN];
    get_local_ip(node_ip, sizeof(node_ip));

    printf("\n🔋 Lithium Node Agent v%s\n", LITHIUM_VERSION);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  Node     : %s\n", id->node_id);
    printf("  Node IP  : %s:%d\n", node_ip, port);
    printf("  Scheduler: %s:%d\n", SCHEDULER_HOST, SCHEDULER_PORT);
    printf("  Heartbeat: every %ds\n", interval_seconds);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");

    int tick = 0;
    while (1) {
        tick++;
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        char timebuf[32];
        strftime(timebuf, sizeof(timebuf), "%H:%M:%S", t);

        int result = send_heartbeat_http(id, port);
        if (result == 0)
            printf("[%s] ♥ heartbeat #%d — %s → scheduler OK\n",
                   timebuf, tick, id->node_id);
        else
            printf("[%s] ✗ heartbeat #%d — scheduler unreachable\n",
                   timebuf, tick);
        fflush(stdout);
        sleep(interval_seconds);
    }
}

void heartbeat_loop(const NodeIdentity *id, int interval_seconds) {
    run_heartbeat(id, interval_seconds, NODE_PORT);
}

void heartbeat_loop_port(const NodeIdentity *id,
                          int interval_seconds, int port) {
    run_heartbeat(id, interval_seconds, port);
}
