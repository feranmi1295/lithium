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

typedef struct {
    const char *command;
    int         output_fd;
} SandboxArgs;

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

/* runs inside the sandbox namespace */
static int sandbox_exec(void *arg) {
    SandboxArgs *args = (SandboxArgs *)arg;

    /* redirect stdout/stderr to pipe */
    dup2(args->output_fd, STDOUT_FILENO);
    dup2(args->output_fd, STDERR_FILENO);
    close(args->output_fd);

    /* mount proc inside new namespace */
    mount("proc", "/proc", "proc", 0, NULL);

    /* drop to nobody if possible */
    setuid(65534);
    setgid(65534);

    /* exec via shell */
    execl("/bin/sh", "sh", "-c", args->command, NULL);

    /* if execl fails */
    _exit(127);
}

int job_parse(const char *json, Job *out) {
    memset(out, 0, sizeof(Job));
    if (extract_field(json, "job_id",    out->job_id,    sizeof(out->job_id))    < 0) return -1;
    if (extract_field(json, "client_id", out->client_id, sizeof(out->client_id)) < 0) return -1;
    if (extract_field(json, "runtime",   out->runtime,   sizeof(out->runtime))   < 0) return -1;
    if (extract_field(json, "command",   out->command,   sizeof(out->command))   < 0) return -1;
    return 0;
}

int job_run(const Job *job, JobResult *result) {
    memset(result, 0, sizeof(JobResult));
    strncpy(result->job_id, job->job_id, sizeof(result->job_id) - 1);

    printf("  [sandbox] Executing job %s\n", job->job_id);
    printf("  [sandbox] Command: %s\n", job->command);
    fflush(stdout);

    /* create pipe for output capture */
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        snprintf(result->output, sizeof(result->output), "pipe failed");
        result->exit_code = -1;
        return -1;
    }

    /* allocate stack for clone */
    char *stack = malloc(STACK_SIZE);
    if (!stack) {
        close(pipefd[0]); close(pipefd[1]);
        result->exit_code = -1;
        return -1;
    }
    char *stack_top = stack + STACK_SIZE;

    SandboxArgs args = {
        .command   = job->command,
        .output_fd = pipefd[1]
    };

    /* clone with new namespaces */
    int flags = CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWNET | SIGCHLD;
    pid_t pid  = clone(sandbox_exec, stack_top, flags, &args);
    free(stack);
    close(pipefd[1]); /* close write end in parent */

    if (pid < 0) {
        /* clone failed — fall back to direct popen */
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

    /* read output from pipe */
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

    /* wait for sandboxed process */
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
        close(sock);
        return;
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
