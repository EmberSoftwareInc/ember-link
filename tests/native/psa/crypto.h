#pragma once
// Test-only PSA adapter: OpenSSL computes real SHA-256 on the host.
#include <stddef.h>
#include <stdint.h>
#include <openssl/evp.h>
typedef int psa_status_t;
typedef struct {
    EVP_MD_CTX *ctx;
} psa_hash_operation_t;
#define PSA_HASH_OPERATION_INIT {NULL}
#define PSA_SUCCESS 0
#define PSA_ALG_SHA_256 1
static inline psa_status_t psa_hash_setup(psa_hash_operation_t *h, int alg)
{
    (void)alg;
    h->ctx = EVP_MD_CTX_new();
    return h->ctx && EVP_DigestInit_ex(h->ctx, EVP_sha256(), NULL) == 1 ? 0 : -1;
}
static inline psa_status_t psa_hash_update(psa_hash_operation_t *h, const void *p, size_t n)
{
    return EVP_DigestUpdate(h->ctx, p, n) == 1 ? 0 : -1;
}
static inline psa_status_t psa_hash_finish(psa_hash_operation_t *h, uint8_t *p, size_t cap,
                                           size_t *n)
{
    if (cap < 32)
        return -1;
    unsigned int len = 0;
    int ok = EVP_DigestFinal_ex(h->ctx, p, &len);
    EVP_MD_CTX_free(h->ctx);
    h->ctx = NULL;
    *n = len;
    return ok == 1 ? 0 : -1;
}
static inline psa_status_t psa_hash_abort(psa_hash_operation_t *h)
{
    EVP_MD_CTX_free(h->ctx);
    h->ctx = NULL;
    return 0;
}
