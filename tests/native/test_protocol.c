#include "cloud_protocol.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *valid_job =
    "{\"type\":\"download\",\"jobId\":\"job-123\",\"attemptId\":\"attempt-1\",\"filename\":\"rose."
    "pes\",\"size\":3,\"sha256\":"
    "\"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\",\"downloadUrl\":\"https:/"
    "/files.example.com/"
    "design?signature=test\",\"ownershipGeneration\":2,\"expiresAt\":2000000300}";
static bool parse(cJSON *job)
{
    link_receipt_t r;
    time_t expires;
    return link_job_parse(job, 2, "files.example.com", 2000000000, &r, &expires);
}
static void bad_string(const char *field, const char *value)
{
    cJSON *j = cJSON_Parse(valid_job);
    cJSON_ReplaceItemInObject(j, field, cJSON_CreateString(value));
    assert(!parse(j));
    cJSON_Delete(j);
}
static void bad_number(const char *field, double value)
{
    cJSON *j = cJSON_Parse(valid_job);
    cJSON_ReplaceItemInObject(j, field, cJSON_CreateNumber(value));
    assert(!parse(j));
    cJSON_Delete(j);
}
int main(void)
{
    assert(link_filename_valid("redWork (19).PES"));
    const char *bad_names[] = {
        "",       "../x.pes", ".pes",        "~link.upload", "a/b.pes",         "a\\b.pes",
        "a.pes ", "a.pes.",   "CON.pes",     "lpt1.pes",     "START HERE.html", "x.pes\n",
        "x",      "x.",       "x:stream.pes"};
    for (size_t i = 0; i < sizeof(bad_names) / sizeof(*bad_names); i++)
        assert(!link_filename_valid(bad_names[i]));
    assert(link_api_base_valid("https://api.example.com/"));
    assert(!link_api_base_valid("https://api.example.com/v1"));
    const char *urls[] = {
        "http://files.example.com/x",         "https://files.example.com.evil/x",
        "https://user@files.example.com/x",   "https://files.example.com:443/x",
        "https://files.example.com/x#frag",   "https://files.example.com/x\nAuthorization:test",
        "https://files.example.com\\@evil/x", "https://files.example.com%2fevil/x"};
    for (size_t i = 0; i < sizeof(urls) / sizeof(*urls); i++)
        assert(!link_https_url_valid(urls[i], "files.example.com"));
    assert(link_https_url_valid("https://FILES.example.com/x?X-Amz-Signature=abc",
                                "files.example.com"));
    assert(!link_integer_valid(NAN, 1, 100));
    assert(!link_integer_valid(INFINITY, 1, 100));
    assert(!link_integer_valid(1.5, 1, 100));
    cJSON *job = cJSON_Parse(valid_job);
    assert(parse(job));
    link_receipt_t r;
    time_t expiry;
    assert(link_job_parse(job, 2, "files.example.com", 2000000000, &r, &expiry));
    assert(!link_job_parse(job, 3, "files.example.com", 2000000000, &r, &expiry));
    cJSON_Delete(job);
    const char *fields[] = {"type",   "jobId",       "attemptId",           "filename", "size",
                            "sha256", "downloadUrl", "ownershipGeneration", "expiresAt"};
    for (size_t i = 0; i < sizeof(fields) / sizeof(*fields); i++) {
        job = cJSON_Parse(valid_job);
        cJSON_DeleteItemFromObject(job, fields[i]);
        assert(!parse(job));
        cJSON_Delete(job);
        job = cJSON_Parse(valid_job);
        cJSON_ReplaceItemInObject(job, fields[i], cJSON_CreateNull());
        assert(!parse(job));
        cJSON_Delete(job);
    }
    bad_string("type", "delete");
    bad_string("jobId", "../job");
    bad_string("attemptId", "a\r\nb");
    bad_string("filename", "../rose.pes");
    bad_string("sha256", "bad");
    bad_string("downloadUrl", "https://evil.example/x");
    bad_number("size", 0);
    bad_number("size", 1.5);
    bad_number("size", LINK_MAX_FILE_BYTES + 1);
    bad_number("expiresAt", 2000000000);
    bad_number("expiresAt", 2000003601);
    bad_number("ownershipGeneration", 1);
    cJSON *ack =
        cJSON_Parse("{\"jobId\":\"job-123\",\"attemptId\":\"attempt-1\",\"state\":\"done\"}");
    strcpy(r.state, "done");
    assert(link_receipt_ack_matches(&r, ack));
    cJSON_ReplaceItemInObject(ack, "attemptId", cJSON_CreateString("stale"));
    assert(!link_receipt_ack_matches(&r, ack));
    cJSON_ReplaceItemInObject(ack, "attemptId", cJSON_CreateString("attempt-1"));
    strcpy(r.state, "needs_reconciliation");
    cJSON_ReplaceItemInObject(ack, "state", cJSON_CreateString(r.state));
    assert(!link_receipt_ack_matches(&r, ack));
    cJSON_AddBoolToObject(ack, "resolved", true);
    assert(link_receipt_ack_matches(&r, ack));
    r.acknowledged = true;
    assert(!link_receipt_ack_matches(&r, ack));
    cJSON_Delete(ack);
    puts("protocol tests passed (validation, ownership, expiry, receipt replay)");
    return 0;
}
