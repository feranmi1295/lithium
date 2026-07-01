#include "heartbeat.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define SCHEDULER_HOST "127.0.0.1"
#define SCHEDULER_PORT 7700

static int send_heartbeat_http(const NodeIdentity *id) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(SCHEDULER_PORT);
    inet_pton(AF_INET, SCHEDULER_HOST, &addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }

    /* build JSON body */
    char pubkey_hex[crypto_sign_PUBLICKEYBYTES * 2 + 1];
    sodium_bin2hex(pubkey_hex, sizeof(pubkey_hex),
                   id->public_key, crypto_sign_PUBLICKEYBYTES);

    char body[512];
    snprintf(body, sizeof(body),
        "{\"node_id\":\"%s\",\"public_key\":\"%s\"}",
        id->node_id, pubkey_hex);

    /* build HTTP POST request */
    char request[1024];
    snprintf(request, sizeof(request),
        "POST /heartbeat HTTP/1.0\r\n"
        "Host: %s:%d\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "\r\n"
        "%s",
        SCHEDULER_HOST, SCHEDULER_PORT, strlen(body), body);

    send(sock, request, strlen(request), 0);

    /* read response status */
    char resp[256] = {0};
    recv(sock, resp, sizeof(resp) - 1, 0);
    close(sock);

    /* check for 200 */
    return (strstr(resp, "200") != NULL) ? 0 : -1;
}

void heartbeat_loop(const NodeIdentity *id, int interval_seconds) {
    printf("\n🔋 Lithium Node Agent v%s\n", LITHIUM_VERSION);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  Node     : %s\n", id->node_id);
    printf("  Scheduler: %s:%d\n", SCHEDULER_HOST, SCHEDULER_PORT);
    printf("  Status   : Active\n");
    printf("  Heartbeat: every %ds\n", interval_seconds);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");

    int tick = 0;
    while (1) {
        tick++;
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        char timebuf[32];
        strftime(timebuf, sizeof(timebuf), "%H:%M:%S", t);

        int result = send_heartbeat_http(id);
        if (result == 0) {
            printf("[%s] ♥ heartbeat #%d — %s → scheduler OK\n",
                   timebuf, tick, id->node_id);
        } else {
            printf("[%s] ✗ heartbeat #%d — scheduler unreachable\n",
                   timebuf, tick);
        }
        fflush(stdout);
        sleep(interval_seconds);
    }
}
