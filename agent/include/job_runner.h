#ifndef JOB_RUNNER_H
#define JOB_RUNNER_H

#include "common.h"

typedef struct {
    char job_id[64];
    char client_id[64];
    char runtime[32];
    char command[512];
    char signature[256];
    char scheduler_pubkey[256];
} Job;

typedef struct {
    char job_id[64];
    char node_id[32];
    int  exit_code;
    char output[4096];
} JobResult;

int  job_parse(const char *json, Job *out);
int  job_run(const Job *job, JobResult *result);
void job_report(const NodeIdentity *id, const JobResult *result);

#endif
