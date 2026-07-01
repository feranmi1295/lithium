#include "server.h"
#include "job_runner.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

typedef struct {
    int client_fd;
    NodeIdentity id;
} HandlerArgs;

static void send_response(int fd, int code, const char *body) {
    const char *status = (code == 200) ? "OK" : "Bad Request";
    char resp[512];
    snprintf(resp, sizeof(resp),
        "HTTP/1.0 %d %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "\r\n%s",
        code, status, strlen(body), body);
    send(fd, resp, strlen(resp), 0);
}

static void *handle_connection(void *arg) {
    HandlerArgs *args = (HandlerArgs *)arg;
    int fd = args->client_fd;
    NodeIdentity id = args->id;
    free(args);

    /* read HTTP request */
    char buf[8192] = {0};
    ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) { close(fd); return NULL; }

    /* find JSON body after headers */
    char *body = strstr(buf, "\r\n\r\n");
    if (!body) {
        send_response(fd, 400, "{\"error\":\"bad request\"}");
        close(fd);
        return NULL;
    }
    body += 4;

    /* check path */
    if (strstr(buf, "POST /job")) {
        Job job;
        if (job_parse(body, &job) < 0) {
            send_response(fd, 400, "{\"error\":\"invalid job\"}");
            close(fd);
            return NULL;
        }

        send_response(fd, 200, "{\"status\":\"accepted\"}");
        close(fd);

        /* run job and report result */
        JobResult result;
        job_run(&job, &result);
        strncpy(result.node_id, id.node_id, sizeof(result.node_id) - 1);
        job_report(&id, &result);

    } else if (strstr(buf, "GET /status")) {
        char resp_body[256];
        snprintf(resp_body, sizeof(resp_body),
            "{\"node_id\":\"%s\",\"status\":\"active\",\"version\":\"%s\"}",
            id.node_id, LITHIUM_VERSION);
        send_response(fd, 200, resp_body);
        close(fd);
    } else {
        send_response(fd, 404, "{\"error\":\"not found\"}");
        close(fd);
    }

    return NULL;
}

void server_start(const NodeIdentity *id) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(NODE_PORT);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        return;
    }

    listen(server_fd, 10);
    printf("  Job receiver: listening on port %d\n", NODE_PORT);
    fflush(stdout);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd,
            (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) continue;

        HandlerArgs *args = malloc(sizeof(HandlerArgs));
        args->client_fd   = client_fd;
        args->id          = *id;

        pthread_t thread;
        pthread_create(&thread, NULL, handle_connection, args);
        pthread_detach(thread);
    }
}
