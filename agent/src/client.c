#include "client.h"
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#define SCHEDULER_HOST "127.0.0.1"
#define SCHEDULER_PORT 7700

/* ── HTTP helper ─────────────────────────────────────── */
static int http_request(const char *method, const char *path,
                         const char *body, char *resp, size_t resp_len) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(SCHEDULER_PORT);
    inet_pton(AF_INET, SCHEDULER_HOST, &addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock); return -1;
    }

    char request[4096];
    if (body && strlen(body) > 0) {
        snprintf(request, sizeof(request),
            "%s %s HTTP/1.0\r\n"
            "Host: %s:%d\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %zu\r\n"
            "\r\n%s",
            method, path, SCHEDULER_HOST, SCHEDULER_PORT,
            strlen(body), body);
    } else {
        snprintf(request, sizeof(request),
            "%s %s HTTP/1.0\r\n"
            "Host: %s:%d\r\n"
            "\r\n",
            method, path, SCHEDULER_HOST, SCHEDULER_PORT);
    }

    send(sock, request, strlen(request), 0);

    memset(resp, 0, resp_len);
    ssize_t n = recv(sock, resp, resp_len - 1, 0);
    close(sock);
    return (n > 0) ? 0 : -1;
}

static char *get_body(char *resp) {
    char *body = strstr(resp, "\r\n\r\n");
    return body ? body + 4 : resp;
}

static int extract_field(const char *json, const char *field,
                          char *out, size_t out_len) {
    char key[128];
    snprintf(key, sizeof(key), "\"%s\":\"", field);
    const char *start = strstr(json, key);
    if (!start) return -1;
    start += strlen(key);
    const char *end = strchr(start, '"');
    if (!end) return -1;
    size_t len = end - start;
    if (len >= out_len) len = out_len - 1;
    memcpy(out, start, len);
    out[len] = '\0';
    return 0;
}

/* ── generate client ID ──────────────────────────────── */
static void generate_client_id(char *out) {
    unsigned char buf[4];
    randombytes_buf(buf, sizeof(buf));
    char hex[9];
    sodium_bin2hex(hex, sizeof(hex), buf, sizeof(buf));
    for (int i = 0; i < 8; i++)
        hex[i] = (hex[i] >= 'a') ? hex[i] - 32 : hex[i];
    snprintf(out, 32, "CLIENT-%s", hex);
}

static void generate_api_key(char *out) {
    unsigned char buf[16];
    randombytes_buf(buf, sizeof(buf));
    sodium_bin2hex(out, 33, buf, sizeof(buf));
}

/* ── save job to local log ───────────────────────────── */
static void log_job(const char *client_id, const char *job_id,
                    const char *command) {
    FILE *f = fopen(CLIENT_JOBS_FILE, "a");
    if (!f) return;
    time_t now = time(NULL);
    fprintf(f, "%ld|%s|%s|%s\n", now, client_id, job_id, command);
    fclose(f);
}

/* ── commands ────────────────────────────────────────── */
static void client_init() {
    struct stat st;
    if (stat(CLIENT_ID_FILE, &st) == 0) {
        /* load and show existing */
        FILE *f = fopen(CLIENT_ID_FILE, "r");
        if (!f) return;
        char client_id[32] = {0}, api_key[64] = {0};
        fscanf(f, "%31s\n%63s\n", client_id, api_key);
        fclose(f);
        printf("⚠  Client already initialized.\n\n");
        printf("  Client ID : %s\n", client_id);
        printf("  API Key   : %s\n", api_key);
        return;
    }

    mkdir(CLIENT_DIR, 0700);

    char client_id[32], api_key[64];
    generate_client_id(client_id);
    generate_api_key(api_key);

    FILE *f = fopen(CLIENT_ID_FILE, "w");
    if (!f) { fprintf(stderr, "Failed to save client identity\n"); return; }
    fprintf(f, "%s\n%s\n", client_id, api_key);
    fclose(f);
    chmod(CLIENT_ID_FILE, 0600);

    printf("🔋 Lithium Client Setup\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  ✅ Client registered\n\n");
    printf("  Client ID : %s\n", client_id);
    printf("  API Key   : %.16s...\n", api_key);
    printf("\n  Run 'lithium client deploy' to submit a job.\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
}

static void client_deploy(int argc, char *argv[]) {
    /* load client identity */
    FILE *f = fopen(CLIENT_ID_FILE, "r");
    if (!f) {
        printf("⚠  Not initialized. Run 'lithium client init' first.\n");
        return;
    }
    char client_id[32] = {0}, api_key[64] = {0};
    fscanf(f, "%31s\n%63s\n", client_id, api_key);
    fclose(f);

    /* parse deploy args */
    const char *runtime = "shell";
    const char *command = NULL;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--runtime") == 0 && i+1 < argc)
            runtime = argv[++i];
        else if (strcmp(argv[i], "--cmd") == 0 && i+1 < argc)
            command = argv[++i];
        else if (command == NULL && argv[i][0] != '-')
            command = argv[i];
    }

    if (!command) {
        printf("Usage: lithium client deploy --cmd \"<command>\" [--runtime shell]\n");
        printf("Example: lithium client deploy --cmd \"echo hello world\"\n");
        return;
    }

    /* check scheduler reachable */
    char resp[2048];
    if (http_request("GET", "/ping", NULL, resp, sizeof(resp)) < 0) {
        printf("❌ Cannot reach scheduler at %s:%d\n",
               SCHEDULER_HOST, SCHEDULER_PORT);
        return;
    }

    /* submit job */
    char body[512];
    snprintf(body, sizeof(body),
        "{\"client_id\":\"%s\",\"runtime\":\"%s\",\"command\":\"%s\"}",
        client_id, runtime, command);

    memset(resp, 0, sizeof(resp));
    if (http_request("POST", "/jobs", body, resp, sizeof(resp)) < 0) {
        printf("❌ Failed to submit job\n"); return;
    }

    char *json = get_body(resp);
    char job_id[64] = {0}, assigned_to[32] = {0};
    extract_field(json, "job_id",      job_id,      sizeof(job_id));
    extract_field(json, "assigned_to", assigned_to, sizeof(assigned_to));

    if (strlen(job_id) == 0) {
        printf("❌ Scheduler returned an error\n"); return;
    }

    /* log job locally */
    log_job(client_id, job_id, command);

    printf("\n🔋 Job Deployed\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  Job ID    : %s\n", job_id);
    printf("  Command   : %s\n", command);
    printf("  Runtime   : %s\n", runtime);
    printf("  Assigned  : %s\n", assigned_to);
    printf("\n  Check status: lithium client logs %s\n", job_id);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
}

static void client_status() {
    FILE *f = fopen(CLIENT_ID_FILE, "r");
    if (!f) {
        printf("⚠  Not initialized. Run 'lithium client init' first.\n");
        return;
    }
    char client_id[32] = {0};
    fscanf(f, "%31s\n", client_id);
    fclose(f);

    /* fetch all jobs */
    char resp[8192];
    if (http_request("GET", "/jobs", NULL, resp, sizeof(resp)) < 0) {
        printf("❌ Cannot reach scheduler\n"); return;
    }

    char *json = get_body(resp);

    printf("\n🔋 Lithium Client Status — %s\n", client_id);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");

    /* count and show this client's jobs */
    int count = 0;
    char *ptr = json;
    while ((ptr = strstr(ptr, "\"job_id\"")) != NULL) {
        char job_id[64] = {0}, status[32] = {0};
        char node_id[32] = {0}, command[256] = {0};

        /* find the opening { for this job */
        char *job_start = ptr;
        while (job_start > json && *job_start != '{') job_start--;

        extract_field(job_start, "job_id",  job_id,  sizeof(job_id));
        extract_field(job_start, "status",  status,  sizeof(status));
        extract_field(job_start, "node_id", node_id, sizeof(node_id));
        extract_field(job_start, "command", command, sizeof(command));

        /* filter by this client's jobs using local log */
        FILE *log = fopen(CLIENT_JOBS_FILE, "r");
        int is_mine = 0;
        if (log) {
            char line[256];
            while (fgets(line, sizeof(line), log)) {
                if (strstr(line, job_id)) { is_mine = 1; break; }
            }
            fclose(log);
        }

        if (is_mine) {
            count++;
            const char *icon = strcmp(status, "complete") == 0 ? "✅" :
                               strcmp(status, "failed")   == 0 ? "❌" :
                               strcmp(status, "assigned") == 0 ? "⚡" : "⏳";
            printf("  %s %s\n", icon, job_id);
            printf("     Command : %s\n", command);
            printf("     Status  : %s\n", status);
            printf("     Node    : %s\n\n", node_id);
        }
        ptr++;
    }

    if (count == 0)
        printf("  No jobs yet. Run 'lithium client deploy' to start.\n\n");

    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
}

static void client_logs(const char *job_id) {
    if (!job_id) {
        printf("Usage: lithium client logs <job-id>\n");
        return;
    }

    char path[128];
    snprintf(path, sizeof(path), "/jobs/%s", job_id);

    char resp[4096];
    if (http_request("GET", path, NULL, resp, sizeof(resp)) < 0) {
        printf("❌ Cannot reach scheduler\n"); return;
    }

    if (!strstr(resp, "200")) {
        printf("❌ Job not found: %s\n", job_id); return;
    }

    char *json = get_body(resp);
    char status[32] = {0}, result[2048] = {0};
    char node_id[32] = {0}, command[256] = {0};

    extract_field(json, "status",  status,  sizeof(status));
    extract_field(json, "result",  result,  sizeof(result));
    extract_field(json, "node_id", node_id, sizeof(node_id));
    extract_field(json, "command", command, sizeof(command));

    printf("\n🔋 Job Logs — %s\n", job_id);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  Command : %s\n", command);
    printf("  Node    : %s\n", node_id);
    printf("  Status  : %s\n\n", status);

    if (strcmp(status, "complete") == 0) {
        printf("  Output:\n");
        printf("  ┌─────────────────────────────\n");
        if (strlen(result) > 0)
            printf("  │ %s\n", result);
        else
            printf("  │ (no output)\n");
        printf("  └─────────────────────────────\n");
    } else if (strcmp(status, "failed") == 0) {
        printf("  ❌ Job failed\n");
    } else {
        printf("  ⏳ Job still running...\n");
        printf("  Run this command again in a moment.\n");
    }
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
}

static void client_billing() {
    FILE *f = fopen(CLIENT_ID_FILE, "r");
    if (!f) {
        printf("⚠  Not initialized. Run 'lithium client init' first.\n");
        return;
    }
    char client_id[32] = {0};
    fscanf(f, "%31s\n", client_id);
    fclose(f);

    /* count jobs from local log */
    FILE *log = fopen(CLIENT_JOBS_FILE, "r");
    int total = 0, complete = 0, failed = 0;
    char line[256];

    if (log) {
        while (fgets(line, sizeof(line), log)) total++;
        fclose(log);
    }

    /* fetch all jobs to count statuses */
    char resp[8192];
    if (http_request("GET", "/jobs", NULL, resp, sizeof(resp)) < 0) {
        printf("❌ Cannot reach scheduler\n"); return;
    }

    char *ptr = get_body(resp);
    while ((ptr = strstr(ptr, "\"status\"")) != NULL) {
        char status[32] = {0};
        char *start = ptr;
        while (start > resp && *start != '{') start--;
        extract_field(start, "status", status, sizeof(status));

        /* check if it's our job */
        char job_id[64] = {0};
        extract_field(start, "job_id", job_id, sizeof(job_id));
        FILE *jlog = fopen(CLIENT_JOBS_FILE, "r");
        if (jlog) {
            char jline[256];
            while (fgets(jline, sizeof(jline), jlog)) {
                if (strstr(jline, job_id)) {
                    if (strcmp(status, "complete") == 0) complete++;
                    else if (strcmp(status, "failed") == 0) failed++;
                    break;
                }
            }
            fclose(jlog);
        }
        ptr++;
    }

    /* estimate cost — $0.001 per job for now */
    double cost = complete * 0.001;

    printf("\n🔋 Billing — %s\n", client_id);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  Jobs submitted : %d\n", total);
    printf("  Completed      : %d\n", complete);
    printf("  Failed         : %d\n", failed);
    printf("  ─────────────────────────────────\n");
    printf("  Estimated cost : $%.4f USDC\n", cost);
    printf("  ─────────────────────────────────\n");
    printf("  Free tier      : 30 days / 1 CPU\n");
    printf("  Plan           : Free\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
}

static void client_help() {
    printf("\n🔋 Lithium Client v%s\n\n", LITHIUM_VERSION);
    printf("Usage: lithium client <command> [options]\n\n");
    printf("Commands:\n");
    printf("  init                          Register as a client\n");
    printf("  deploy --cmd \"<cmd>\"          Deploy a job\n");
    printf("         [--runtime shell]      Runtime (default: shell)\n");
    printf("  status                        List your deployments\n");
    printf("  logs <job-id>                 View job output\n");
    printf("  billing                       View usage and costs\n");
    printf("  help                          Show this message\n\n");
    printf("Examples:\n");
    printf("  lithium client init\n");
    printf("  lithium client deploy --cmd \"echo hello world\"\n");
    printf("  lithium client deploy --cmd \"python3 server.py\" --runtime python3\n");
    printf("  lithium client logs JOB-ABC123\n");
    printf("  lithium client billing\n\n");
}

void cmd_client(int argc, char *argv[]) {
    if (argc < 1) { client_help(); return; }

    const char *sub = argv[0];

    if      (strcmp(sub, "init")    == 0) client_init();
    else if (strcmp(sub, "deploy")  == 0) client_deploy(argc - 1, argv + 1);
    else if (strcmp(sub, "status")  == 0) client_status();
    else if (strcmp(sub, "logs")    == 0) client_logs(argc > 1 ? argv[1] : NULL);
    else if (strcmp(sub, "billing") == 0) client_billing();
    else if (strcmp(sub, "help")    == 0) client_help();
    else {
        printf("Unknown client command: %s\n", sub);
        client_help();
    }
}
