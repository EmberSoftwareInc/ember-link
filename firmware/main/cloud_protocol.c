#include "cloud_protocol.h"
#include <string.h>

static const char *string(const cJSON *o, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}

bool link_json_u64(const cJSON *o, const char *key, uint64_t min, uint64_t max, uint64_t *out)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!cJSON_IsNumber(v) || !link_integer_valid(v->valuedouble, min, max))
        return false;
    *out = (uint64_t)v->valuedouble;
    return true;
}

bool link_job_parse(const cJSON *job, uint64_t generation, const char *download_host, time_t now,
                    link_receipt_t *r, time_t *expiry)
{
    const char *type = string(job, "type"), *id = string(job, "jobId"),
               *attempt = string(job, "attemptId");
    const char *name = string(job, "filename"), *hash = string(job, "sha256"),
               *url = string(job, "downloadUrl");
    uint64_t size, expires, owner;
    if (!type || strcmp(type, "download") || !link_id_valid(id) || !link_id_valid(attempt) ||
        !link_filename_valid(name) || !link_sha256_valid(hash) ||
        !link_https_url_valid(url, download_host) ||
        !link_json_u64(job, "size", 1, LINK_MAX_FILE_BYTES, &size) ||
        !link_json_u64(job, "expiresAt", 1, 9007199254740991ULL, &expires) ||
        !link_json_u64(job, "ownershipGeneration", 1, 9007199254740991ULL, &owner) ||
        owner != generation || expires <= (uint64_t)now || expires > (uint64_t)now + 3600)
        return false;
    memset(r, 0, sizeof(*r));
    r->schema = 1;
    strcpy(r->job_id, id);
    strcpy(r->attempt_id, attempt);
    strcpy(r->filename, name);
    strcpy(r->sha256, hash);
    strcpy(r->state, "delivering");
    r->size = size;
    r->ownership_generation = owner;
    *expiry = (time_t)expires;
    return true;
}

bool link_receipt_ack_matches(const link_receipt_t *r, const cJSON *ack)
{
    if (!r->job_id[0] || r->acknowledged || !cJSON_IsObject(ack))
        return false;
    const char *id = string(ack, "jobId"), *attempt = string(ack, "attemptId"),
               *state = string(ack, "state");
    if (!id || !attempt || !state || strcmp(id, r->job_id) || strcmp(attempt, r->attempt_id) ||
        strcmp(state, r->state))
        return false;
    if (!strcmp(state, "delivering"))
        return false;
    if (!strcmp(state, "needs_reconciliation") &&
        !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(ack, "resolved")))
        return false;
    return !strcmp(state, "done") || !strcmp(state, "failed") ||
           !strcmp(state, "needs_reconciliation");
}
