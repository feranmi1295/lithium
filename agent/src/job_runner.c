#include "job_runner.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sched.h>
#include <sys/mount.h>
#include <fcntl.h>
#include <signal.h>

#define STACK_SIZE (1024 * 1024)
#define SCHEDULER_PUBKEY_FILE ".lithium/scheduler_pubkey.hex"

typedef struct {
    const char *command;
    int         output_fd;
} SandboxArgs;

/* hex decode */
static int hex_decode(const char *hex, unsigned char *out, size_t out_len) {
    size_t hex_len = strlen(hex);
    if (hex_len != out_len * 2) return -1;
    for (size_t i = 0; i < out_len; i++) {
        unsigned int byte;
        if (sscanf(hex + 2 * i, "%02x", &byte) != 1) return -1;
        out[i] = (unsigned char)byte;
    }
    return 0;
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

/* fetch scheduler pubkey if not cached */
static int load_scheduler_pubkey(unsigned char *pubkey_out) {
    /* try loading from file first */
    FILE *f = fopen(SCHEDULER_PUBKEY_FILE, "r");
    if (f) {
        char hex[128] = {0};
        if (fgets(hex, sizeof(hex), f)) {
            fclose(f);
            size_t l = strlen(hex);
            if (l > 0 && hex[l-1] == '\n') hex[l-1] = '\0';
            return hex_decode(hex, pubkey_out, crypto_sign_PUBLICKEYBYTES);
        }
        fclose(f);
    }

    /* fetch from scheduler */
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(7700);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }

    const char *req = "GET /pubkey HTTP/1.0\r\nHost: 127.0.0.1:7700\r\n\r\n";
    send(sock, req, strlen(req), 0);

    char resp[512] = {0};
    recv(sock, resp, sizeof(resp) - 1, 0);
    close(sock);

    /* extract public_key from JSON */
    char hex[128] = {0};
    if (extract_field(resp, "public_key", hex, sizeof(hex)) < 0) return -1;

    /* cache to file */
    f = fopen(SCHEDULER_PUBKEY_FILE, "w");
    if (f) { fprintf(f, "%s\n", hex); fclose(f); }

    return hex_decode(hex, pubkey_out, crypto_sign_PUBLICKEYBYTES);
}

static int verify_job_signature(const Job *job) {
    unsigned char pubkey[crypto_sign_PUBLICKEYBYTES];

    if (load_scheduler_pubkey(pubkey) < 0) {
        printf("  [verify] Could not load scheduler pubkey — rejecting\n");
        return -1;
    }

    /* rebuild payload */
    char payload[768];
    snprintf(payload, sizeof(payload), "%s:%s:%s:%s",
             job->job_id, job->client_id, job->runtime, job->command);

    /* decode signature */
    unsigned char sig[crypto_sign_BYTES];
    if (hex_decode(job->signature, sig, crypto_sign_BYTES) < 0) {
        printf("  [verify] Invalid signature format\n");
        return -1;
    }

    /* verify */
    int result = crypto_sign_verify_detached(
        sig,
        (unsigned char *)payload, strlen(payload),
        pubkey);

    if (result == 0) {
        printf("  [verify] ✅ Signature valid\n");
        return 0;
    } else {
        printf("  [verify] ❌ Signature INVALID — rejecting job\n");
        return -1;
    }
}

static int sandbox_exec(void *arg) {
    SandboxArgs *args = (SandboxArgs *)arg;
    dup2(args->output_fd, STDOUT_FILENO);
    dup2(args->output_fd, STDERR_FILENO);
    close(args->output_fd);
    mount("proc", "/proc", "proc", 0, NULL);
    setuid(65534);
    setgid(65534);
    execl("/bin/sh", "sh", "-c", args->command, NULL);
    _exit(127);
}

int job_parse(const char *json, Job *out) {
    memset(out, 0, sizeof(Job));
    if (extract_field(json, "job_id",    out->job_id,    sizeof(out->job_id))    < 0) return -1;
    if (extract_field(json, "client_id", out->client_id, sizeof(out->client_id)) < 0) return -1;
    if (extract_field(json, "runtime",   out->runtime,   sizeof(out->runtime))   < 0) return -1;
    if (extract_field(json, "command",   out->command,   sizeof(out->command))   < 0) return -1;
    /* signature fields — optional, default empty */
    extract_field(json, "signature",        out->signature,        sizeof(out->signature));
    extract_field(json, "scheduler_pubkey", out->scheduler_pubkey, sizeof(out->scheduler_pubkey));
    return 0;
}

int job_run(const Job *job, JobResult *result) {
    memset(result, 0, sizeof(JobResult));
    strncpy(result->job_id, job->job_id, sizeof(result->job_id) - 1);

    /* verify signature before executing */
    if (strlen(job->signature) > 0) {
        if (verify_job_signature(job) < 0) {
            snprintf(result->output, sizeof(result->output),
                     "job rejected: invalid signature");
            result->exit_code = -1;
            return -1;
        }
    } else {
        printf("  [verify] ⚠ No signature present\n");
    }

    printf("  [sandbox] Executing job %s\n", job->job_id);
    printf("  [sandbox] Command: %s\n", job->command);
    fflush(stdout);

    int pipefd[2];
    if (pipe(pipefd) < 0) {
        snprintf(result->output, sizeof(result->output), "pipe failed");
        result->exit_code = -1;
        return -1;
    }

    char *stack = malloc(STACK_SIZE);
    if (!stack) { close(pipefd[0]); close(pipefd[1]); result->exit_code = -1; return -1; }
    char *stack_top = stack + STACK_SIZE;

    SandboxArgs args = { .command = job->command, .output_fd = pipefd[1] };

    int flags = CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWNET | SIGCHLD;
    pid_t pid = clone(sandbox_exec, stack_top, flags, &args);
    free(stack);
    close(pipefd[1]);

    if (pid < 0) {
        printf("  [sandbox] Namespace isolation unavailable, using popen\n");
        close(pipefd[0]);
        FILE *pipe = popen(job->command, "r");
        if (!pipe) {
            snprintf(result->output, sizeof(result->output), "execution failed");
            result->exit_code = -1;
            return -1;
        }
        size_t total = 0;
        char buf[256];
        while (fgets(buf, sizeof(buf), pipe) &&
               total < sizeof(result->output) - 1) {
            size_t len = strlen(buf);
            if (total + len >= sizeof(result->output) - 1)
                len = sizeof(result->output) - 1 - total;
            memcpy(result->output + total, buf, len);
            total += len;
        }
        result->output[total] = '\0';
        if (total > 0 && result->output[total-1] == '\n')
            result->output[total-1] = '\0';
        result->exit_code = pclose(pipe);
        goto done;
    }

    {
        size_t total = 0;
        char buf[256];
        ssize_t n;
        while ((n = read(pipefd[0], buf, sizeof(buf))) > 0 &&
                total < sizeof(result->output) - 1) {
            size_t len = (size_t)n;
            if (total + len >= sizeof(result->output) - 1)
                len = sizeof(result->output) - 1 - total;
            memcpy(result->output + total, buf, len);
            total += len;
        }
        result->output[total] = '\0';
        if (total > 0 && result->output[total-1] == '\n')
            result->output[total-1] = '\0';
    }
    close(pipefd[0]);

    {
        int status;
        waitpid(pid, &status, 0);
        result->exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }

done:
    printf("  [sandbox] Output: %s\n", result->output);
    printf("  [sandbox] Exit code: %d\n", result->exit_code);
    fflush(stdout);
    return 0;
}

void job_report(const NodeIdentity *id, const JobResult *result) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return;

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(7700);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock); return;
    }

    char safe_output[4096];
    strncpy(safe_output, result->output, sizeof(safe_output) - 1);
    for (char *p = safe_output; *p; p++)
        if (*p == '"') *p = '\'';

    char body[5120];
    snprintf(body, sizeof(body),
        "{\"job_id\":\"%s\",\"node_id\":\"%s\","
        "\"exit_code\":%d,\"output\":\"%s\"}",
        result->job_id, id->node_id,
        result->exit_code, safe_output);

    char request[6144];
    snprintf(request, sizeof(request),
        "POST /job_result HTTP/1.0\r\n"
        "Host: 127.0.0.1:7700\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "\r\n%s",
        strlen(body), body);

    send(sock, request, strlen(request), 0);
    char resp[256] = {0};
    recv(sock, resp, sizeof(resp) - 1, 0);
    close(sock);

    if (strstr(resp, "200"))
        printf("  [sandbox] Result reported to scheduler\n");
    else
        printf("  [sandbox] Failed to report result\n");
    fflush(stdout);
}
