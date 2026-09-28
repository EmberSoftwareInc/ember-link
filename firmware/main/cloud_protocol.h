#pragma once
#include "cJSON.h"
#include "link_protocol.h"
#include <time.h>

typedef struct {
    uint32_t schema;
    bool acknowledged;
    char job_id[LINK_MAX_ID];
    char attempt_id[LINK_MAX_ID];
    char filename[LINK_MAX_FILENAME];
    char sha256[65];
    char state[32];
    char error[48];
    uint64_t ownership_generation;
    uint64_t size;
    uint64_t bytes;
} link_receipt_t;

bool link_json_u64(const cJSON *o, const char *key, uint64_t minimum, uint64_t maximum,
                   uint64_t *out);
bool link_job_parse(const cJSON *job, uint64_t generation, const char *download_host, time_t now,
                    link_receipt_t *receipt, time_t *expiry);
bool link_receipt_ack_matches(const link_receipt_t *receipt, const cJSON *ack);
